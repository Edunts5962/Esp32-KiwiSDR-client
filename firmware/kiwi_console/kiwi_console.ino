// =====================================================================
// kiwi_console.ino - client KiwiSDR autonome (version : KIWI_VERSION dans reglages.h)
// Carte ES3C28P (ESP32-S3, ecran 2,8" ILI9341 tactile FT6336, SD,
// codec audio ES8311 + haut-parleur).
//
// La carte se connecte en WiFi a n'importe quel recepteur KiwiSDR public
// (0-30 MHz), joue le son sur son haut-parleur et affiche un waterfall
// couleur tactile : on glisse le doigt pour accorder.
//
// Architecture :
//   - coeur 0 : tache "audio" (I2S 48 kHz), tache "reseau" (WiFi, deux
//               WebSocket SND + W/F, portail web), tache "ecran" (envoi SPI),
//               tache "liste" (telechargement de la liste publique, a la demande)
//   - coeur 1 : loop() lit le tactile et dessine l'image suivante ;
//               tache "moteur" (decodeurs CW/RTTY/FAX, enregistrement WAV)
//
// Fichiers du croquis :
//   interface.cpp/.h  ecrans et gestes
//   kiwi.cpp/.h       protocole KiwiSDR (commandes, son ADPCM, waterfall)
//   audio.cpp/.h      reserve et reechantillonnage 12 -> 48 kHz
//   cascade.cpp/.h    waterfall pret a afficher (palettes, contraste auto)
//   serveurs.cpp/.h   liste publique, recepteurs perso, distances
//   decodeurs.cpp/.h  CW, RTTY, FAX
//   moteur.cpp/.h     tache de fond (decodeurs, enregistrement SD)
//   reglages.cpp/.h   reglages enregistres
//   portail.cpp/.h    interface web complete (API) ; page_web.h : la page elle-meme
//   ws_client.cpp/.h  client WebSocket (messages de toutes tailles)
//   gfx.cpp/.h        moteur de dessin lisse (identique aux autres projets)
//   polices.cpp/.h    polices Inter lissees avec accents
//   ft6336_touch.*    pilote tactile (identique aux autres projets)
//   plateforme.h      ce que l'interface demande au materiel (ici)
//
// Reglages Arduino IDE (identiques aux autres projets) :
//   paquet esp32 3.x, carte "ESP32S3 Dev Module", PSRAM "OPI PSRAM",
//   Flash 16 Mo, partition "16M Flash (3MB APP/9.9MB FATFS)".
//   Bibliotheque : TFT_eSPI (meme User_Setup.h que d'habitude). Le client
//   WebSocket est integre au projet (ws_client.cpp) : aucune autre bibliotheque.
// =====================================================================
#include <Arduino.h>
// Ordre important : FS.h doit etre inclus AVANT TFT_eSPI. Si SMOOTH_FONT est actif
// dans User_Setup.h, TFT_eSPI definit FS_NO_GLOBALS et WebServer ne compile plus.
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "ws_client.h"
#include <TFT_eSPI.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "mbedtls/platform.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "ft6336_touch.h"
#include "gfx.h"
#include "interface.h"
#include "kiwi.h"
#include "audio.h"
#include "cascade.h"
#include "serveurs.h"
#include "stations.h"
#include "fm.h"
#include "fmliste.h"
#include "owrxliste.h"
#include "vhf.h"
#include "geo.h"
#include "quoi.h"
#include "decodeurs.h"
#include "ft8.h"
#include "webradio.h"
#include "moteur.h"
#include "reglages.h"
#include "portail.h"
#include "plateforme.h"

// ---------------- Broches ES3C28P ----------------------------------------
#define LCD_BL_PIN     45
#define I2C_SDA        16              // bus partage : tactile FT6336 + codec ES8311
#define I2C_SCL        15
#define TOUCH_RST      18
#define SD_SCK         38
#define SD_CMD         40
#define SD_D0          39
#define SD_D1          41
#define SD_D2          48
#define SD_D3          47
#define I2S_MCLK       4
#define I2S_BCLK       5
#define I2S_WS         7
#define I2S_DOUT       8               // ESP32 -> codec (haut-parleur)
#define PA_PIN         1               // ampli du haut-parleur
#define ES8311_ADR     0x18
#define CH             240             // trames I2S par passage (5 ms a 48 kHz)

#define AP_NOM         "KiwiSDR-ES3C28P"
#define AP_MDP         "kiwisdr123"

TFT_eSPI tft = TFT_eSPI();

// =========================================================================
// Plateforme (voir plateforme.h)
// =========================================================================
static bool s_sd_ok = false;
static Preferences s_prefs;

// ---- moniteur serie : chaque ligne commence par le temps ecoule depuis le demarrage ("  83.4 s") ----
class JournalSerie : public Print {
    bool debut = true;
public:
    size_t write(uint8_t c) override
    {
        if (debut && c != '\n' && c != '\r') { Serial.printf("%7.1f s ", millis() / 1000.0); debut = false; }
        Serial.write(c);
        if (c == '\n') debut = true;
        return 1;
    }
    size_t write(const uint8_t *b, size_t n) override { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
};
static JournalSerie LOG;

// pourquoi la carte a (re)demarre
static char s_raison[48] = "";
static bool s_plantage = false;                 // le demarrage precedent s'est termine par une panne du programme
static void lire_raison()
{
    const char *t = "inconnue";
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: t = "mise sous tension"; break;
    case ESP_RST_EXT: t = "bouton RESET"; break;
    case ESP_RST_SW: t = "redémarrage demandé"; break;
    case ESP_RST_PANIC: t = "plantage du programme"; s_plantage = true; break;
    case ESP_RST_INT_WDT: t = "chien de garde (interruptions)"; s_plantage = true; break;
    case ESP_RST_TASK_WDT: t = "chien de garde (tâche bloquée)"; s_plantage = true; break;
    case ESP_RST_WDT: t = "chien de garde"; s_plantage = true; break;
    case ESP_RST_BROWNOUT: t = "alimentation trop faible"; break;
    case ESP_RST_DEEPSLEEP: t = "réveil"; break;
    case ESP_RST_USB: t = "port USB (moniteur série ou téléversement)"; break;
    case ESP_RST_JTAG: t = "JTAG"; break;
    default: break;
    }
    snprintf(s_raison, sizeof s_raison, "%s", t);
}
const char *plat_raison_demarrage() { return s_raison; }
static volatile bool s_etalonner = false;

uint32_t plat_ms() { return millis(); }
void plat_journal(const char *canal, const char *nom, const char *valeur)
{
    LOG.printf("[KIWI] %s %s=%.60s\n", canal, nom, valeur ? valeur : "");
}
void plat_luminosite(int pct) { ledcWrite(LCD_BL_PIN, (uint32_t)(pct * 255 / 100)); }
bool plat_sd() { return s_sd_ok; }
void plat_etalonner() { s_etalonner = true; }

bool plat_nvs_lire(const char *cle, void *buf, size_t n)
{
    if (s_prefs.getBytesLength(cle) != n) return false;
    return s_prefs.getBytes(cle, buf, n) == n;
}

void plat_nvs_ecrire(const char *cle, const void *buf, size_t n) { s_prefs.putBytes(cle, buf, n); }
size_t plat_nvs_taille(const char *cle) { return s_prefs.isKey(cle) ? s_prefs.getBytesLength(cle) : 0; }

void plat_ampli(bool actif_bas) { digitalWrite(PA_PIN, actif_bas ? LOW : HIGH); }

uint64_t plat_epoch_ms()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec > 1700000000 ? (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 : 0;
}

uint32_t plat_epoch()
{
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;       // avant la mise a l'heure par le reseau : inconnue
}

void plat_horodatage(char *s, size_t n)
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year + 1900 >= 2024) strftime(s, n, "%Y%m%d-%H%M%S", &tm);
    else snprintf(s, n, "boot%lu", (unsigned long)(millis() / 1000));
}

// ---- fichiers (utilises seulement par la tache moteur) ----
static fs::File s_fichier;

bool plat_fichier_ouvrir(const char *chemin)
{
    if (!s_sd_ok) return false;
    if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi");
    s_fichier = SD_MMC.open(chemin, FILE_WRITE);
    return (bool)s_fichier;
}

bool plat_fichier_ecrire(const void *d, size_t n)
{
    if (!s_fichier) return false;
    return s_fichier.write((const uint8_t *)d, n) == n;
}

bool plat_fichier_ecrire_a(size_t pos, const void *d, size_t n)
{
    if (!s_fichier) return false;
    size_t fin = s_fichier.position();
    if (!s_fichier.seek(pos)) return false;
    bool ok = s_fichier.write((const uint8_t *)d, n) == n;
    s_fichier.seek(fin);
    return ok;
}

void plat_fichier_fermer()
{
    if (s_fichier) s_fichier.close();
}

// mbedTLS (connexions https) : les gros tampons (16 Ko par connexion) vont en PSRAM.
// Le coeur Arduino ne les met qu'en memoire interne : deux connexions chiffrees (son + texte FM)
// n'y tiennent pas a cote du WiFi. Les petits blocs restent en memoire interne (plus rapide).
static void *tls_calloc(size_t n, size_t t)
{
    void *p = nullptr;
    if (n * t >= 2048) p = heap_caps_calloc(n, t, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, t, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, t, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
}
static void tls_free(void *p) { heap_caps_free(p); }

void *plat_malloc_interne(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

void plat_free_ps(void *p) { heap_caps_free(p); }   // PSRAM ou memoire interne : heap_caps_free sait

void *plat_malloc_ps(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

char *plat_texte_lire(const char *chemin, size_t max)
{
    if (!s_sd_ok) return nullptr;
    fs::File f = SD_MMC.open(chemin, FILE_READ);
    if (!f) return nullptr;
    size_t n = f.size();
    if (n > max) n = max;
    char *t = (char *)malloc(n + 1);
    if (t) {
        size_t l = f.read((uint8_t *)t, n);
        t[l] = 0;
    }
    f.close();
    return t;
}

bool plat_texte_ecrire(const char *chemin, const char *d, size_t n)
{
    if (!s_sd_ok) return false;
    if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi");
    fs::File f = SD_MMC.open(chemin, FILE_WRITE);
    if (!f) return false;
    bool ok = f.write((const uint8_t *)d, n) == n;
    f.close();
    return ok;
}

bool plat_texte_ajouter(const char *chemin, const char *d, size_t n)
{
    if (!s_sd_ok) return false;
    if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi");
    fs::File f = SD_MMC.open(chemin, FILE_APPEND);
    if (!f) return false;
    bool ok = f.write((const uint8_t *)d, n) == n;
    f.close();
    return ok;
}

// =========================================================================
// Codec ES8311 (I2C) : esclave, MCLK = 256 x 48 kHz, I2S 16 bits
// (sequence validee sur la carte dans les projets precedents)
// =========================================================================
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_codec = NULL;

static bool es_ecrire(uint8_t reg, uint8_t val)
{
    if (!s_codec) return false;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_codec, b, 2, 50) == ESP_OK;
}

static int es_lire(uint8_t reg)
{
    uint8_t v = 0;
    if (i2c_master_transmit_receive(s_codec, &reg, 1, &v, 1, 50) != ESP_OK) return -1;
    return v;
}

void plat_volume(int pct)
{
    // registre 0x32 : 0xBF = 0 dB, pas de 0,5 dB ; 0 = muet
    uint8_t v = pct <= 0 ? 0 : (uint8_t)(0x90 + (pct > 100 ? 100 : pct) * (0xD0 - 0x90) / 100);
    es_ecrire(0x32, v);
}

static bool init_codec()
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = ES8311_ADR;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(s_bus, &cfg, &s_codec) != ESP_OK) return false;
    int id1 = es_lire(0xFD), id2 = es_lire(0xFE);
    LOG.printf("[KIWI] ES8311 identifiant %02X %02X\n", id1, id2);
    if (id1 < 0) return false;
    es_ecrire(0x00, 0x1F); delay(20); es_ecrire(0x00, 0x00);
    es_ecrire(0x45, 0x00);
    es_ecrire(0x01, 0x30); es_ecrire(0x02, 0x00); es_ecrire(0x03, 0x10);
    es_ecrire(0x16, 0x24); es_ecrire(0x04, 0x10); es_ecrire(0x05, 0x00);
    es_ecrire(0x0B, 0x00); es_ecrire(0x0C, 0x00); es_ecrire(0x10, 0x1F); es_ecrire(0x11, 0x7F);
    es_ecrire(0x00, 0x80);
    es_ecrire(0x01, 0x3F);
    es_ecrire(0x02, 0x00); es_ecrire(0x05, 0x00);
    es_ecrire(0x03, 0x10); es_ecrire(0x04, 0x10);
    es_ecrire(0x07, 0x00); es_ecrire(0x08, 0xFF); es_ecrire(0x06, 0x03);
    es_ecrire(0x09, 0x0C); es_ecrire(0x0A, 0x0C);
    es_ecrire(0x13, 0x10);
    es_ecrire(0x1B, 0x0A); es_ecrire(0x1C, 0x6A);
    es_ecrire(0x17, 0xBF);
    es_ecrire(0x0E, 0x02);
    es_ecrire(0x12, 0x00);
    es_ecrire(0x0D, 0x01);
    es_ecrire(0x15, 0x40);
    es_ecrire(0x37, 0x08);
    es_ecrire(0x45, 0x00);
    plat_volume(cfg::R.volume);
    return true;
}

// =========================================================================
// I2S (coeur 0, haute priorite) : 48 kHz stereo, alimente par aud::
// =========================================================================
static i2s_chan_handle_t s_tx = NULL;

static bool init_i2s()
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6;
    cc.dma_frame_num = CH;
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK) return false;
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(aud::FS_SORTIE);   // MCLK = 256 x 48 kHz
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = (gpio_num_t)I2S_MCLK;
    sc.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK;
    sc.gpio_cfg.ws = (gpio_num_t)I2S_WS;
    sc.gpio_cfg.dout = (gpio_num_t)I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK) return false;
    i2s_channel_enable(s_tx);
    return true;
}

static void tache_audio(void *)
{
    static int16_t sortie[2 * CH];
    for (;;) {
        aud::lire_sortie(sortie, CH);
        size_t n = 0;
        i2s_channel_write(s_tx, sortie, sizeof sortie, &n, portMAX_DELAY);
    }
}

// =========================================================================
// WiFi : station si un reseau est enregistre, sinon point d'acces
// =========================================================================
static volatile int s_wifi = WIFI_AUCUN;
static char s_wifi_nom[40] = "";
static char s_wifi_ip[20] = "0.0.0.0";
static WebServer s_web(80);

int plat_wifi_etat() { return s_wifi; }
const char *plat_wifi_nom() { return s_wifi_nom; }
const char *plat_wifi_ip() { return s_wifi_ip; }
const char *plat_wifi_mdp() { return AP_MDP; }
int plat_wifi_rssi() { return s_wifi == WIFI_OK ? WiFi.RSSI() : 0; }

void plat_wifi_oublier()
{
    s_prefs.remove("ssid");
    s_prefs.remove("pass");
    delay(800);
    ESP.restart();
}

void plat_wifi_enregistrer(const char *ssid, const char *pass)
{
    s_prefs.putString("ssid", ssid);
    s_prefs.putString("pass", pass);
    delay(500);
    ESP.restart();
}

static void point_acces()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_NOM, AP_MDP);
    snprintf(s_wifi_nom, sizeof s_wifi_nom, "%s", AP_NOM);
    snprintf(s_wifi_ip, sizeof s_wifi_ip, "%s", WiFi.softAPIP().toString().c_str());
    s_wifi = WIFI_POINT_ACCES;
    LOG.printf("[KIWI] Point d'acces %s (mot de passe %s) - portail http://%s/\n", AP_NOM, AP_MDP, s_wifi_ip);
}

static void wifi_suivre()
{
    static uint32_t t0 = 0;
    static bool demarre = false;
    if (!demarre) {
        demarre = true;
        String ssid = s_prefs.getString("ssid", ""), pass = s_prefs.getString("pass", "");
        if (!ssid.length()) { point_acces(); return; }
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);                         // latence plus faible pour le son
        WiFi.setHostname("kiwi-es3c28p");
        WiFi.begin(ssid.c_str(), pass.c_str());
        snprintf(s_wifi_nom, sizeof s_wifi_nom, "%s", ssid.c_str());
        s_wifi = WIFI_CONNEXION;
        t0 = millis();
        return;
    }
    if (s_wifi == WIFI_POINT_ACCES) return;
    if (WiFi.status() == WL_CONNECTED) {
        if (s_wifi != WIFI_OK) {
            s_wifi = WIFI_OK;
            snprintf(s_wifi_ip, sizeof s_wifi_ip, "%s", WiFi.localIP().toString().c_str());
            configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
            LOG.printf("[KIWI] WiFi connecte, portail http://%s/\n", s_wifi_ip);
        }
    } else {
        if (s_wifi == WIFI_OK) { s_wifi = WIFI_CONNEXION; t0 = millis(); }
        if (millis() - t0 > 20000) point_acces();     // reseau introuvable : point d'acces de secours
    }
}

// =========================================================================
// Liste publique (tache a la demande)
// =========================================================================
static volatile bool s_liste_demande = false, s_liste_cours = false;

void plat_liste_telecharger() { s_liste_demande = true; }
bool plat_liste_en_cours() { return s_liste_cours || s_liste_demande; }

// "flux" qui decoupe ce que HTTPClient recoit en lignes pour srv::ligne_liste
class Lignes : public Stream {
public:
    size_t write(uint8_t c) override
    {
        if (c == '\n' || n >= sizeof buf - 1) {
            buf[n] = 0;
            srv::ligne_liste(buf);
            n = 0;
            lignes++;
            if (lignes % 64 == 0) vTaskDelay(1);       // laisse respirer le coeur 0 (chien de garde)
        } else if (c != '\r') {
            buf[n++] = (char)c;
        }
        return 1;
    }
    size_t write(const uint8_t *b, size_t l) override
    {
        for (size_t i = 0; i < l; i++) write(b[i]);
        return l;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override { if (n) { buf[n] = 0; srv::ligne_liste(buf); n = 0; } }
    char buf[768];
    size_t n = 0;
    uint32_t lignes = 0;
};

// copie de la derniere liste reussie sur la carte SD : servira si les sites ne repondent plus
static const char *COPIE_LISTE = "/kiwi/liste_kiwisdr.txt", *COPIE_TMP = "/kiwi/liste_kiwisdr.tmp", *COPIE_DATE = "/kiwi/liste_kiwisdr.date";

// raison : ce qui n'a pas marche (pour l'ecran)
static bool telecharger(const char *url, char *raison, size_t nr)
{
    // objets sur le tas, pas sur la pile de la tache (le client TLS est gros)
    static HTTPClient http;
    static WiFiClient c;
    static WiFiClientSecure cs;
    static Lignes l;
    bool tls = !strncmp(url, "https", 5);
    if (tls) cs.setInsecure();
    l.n = 0;
    l.lignes = 0;
    const char *h = strstr(url, "://");
    char site[48];
    snprintf(site, sizeof site, "%s", h ? h + 3 : url);
    char *bs = strchr(site, '/');
    if (bs) *bs = 0;
    LOG.printf("[KIWI] Liste : telechargement de %s (pile libre %u)\n", url, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    http.useHTTP10(true);                             // pas de decoupage "chunked"
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setUserAgent("ES3C28P-KiwiClient/" KIWI_VERSION);
    if (!(tls ? http.begin(cs, url) : http.begin(c, url))) { snprintf(raison, nr, "%s : adresse non comprise", site); return false; }
    int code = http.GET();
    LOG.printf("[KIWI] Liste %s : HTTP %d\n", url, code);
    if (code != 200) {
        http.end();
        if (code < 0) snprintf(raison, nr, "%s : pas de réponse", site);
        else snprintf(raison, nr, "%s : HTTP %d", site, code);
        return false;
    }
    // copie brute sur la carte SD pendant la reception
    fs::File copie;
    if (s_sd_ok) { if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi"); copie = SD_MMC.open(COPIE_TMP, FILE_WRITE); }
    srv::debut_liste();
    // lecture avec limites de temps : un site peut garder la connexion ouverte sans plus rien envoyer
    WiFiClient *st = http.getStreamPtr();
    int taille = http.getSize();
    static uint8_t b[1024];
    size_t total = 0;
    uint32_t t0 = millis(), dernier = t0;
    bool coupe = false;
    while (st) {
        int a = st->available();
        if (a > 0) {
            int k = st->read(b, a < (int)sizeof b ? a : (int)sizeof b);
            if (k > 0) {
                l.write(b, (size_t)k);
                if (copie) copie.write(b, (size_t)k);
                total += (size_t)k;
                dernier = millis();
            }
            continue;
        }
        if (!st->connected() || (taille > 0 && (int)total >= taille)) break;   // fin normale
        if (millis() - dernier > 15000 || millis() - t0 > 120000) { coupe = true; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    l.flush();
    http.end();
    bool ok = l.lignes > 10;
    srv::fin_liste(ok);
    int n = srv::nb_publics();
    LOG.printf("[KIWI] Liste : %u octets en %u ms%s, %u lignes, %d recepteurs\n", (unsigned)total, (unsigned)(millis() - t0),
               coupe ? " (attente trop longue : arretee)" : "", (unsigned)l.lignes, n);
    ok = ok && n > 0;
    if (copie) {
        copie.close();
        if (ok && !coupe) {                               // liste complete : elle remplace l'ancienne copie
            SD_MMC.remove(COPIE_LISTE);
            SD_MMC.rename(COPIE_TMP, COPIE_LISTE);
            fs::File d = SD_MMC.open(COPIE_DATE, FILE_WRITE);
            if (d) { d.printf("%lu\n", (unsigned long)plat_epoch()); d.close(); }
        } else SD_MMC.remove(COPIE_TMP);
    }
    if (!ok) snprintf(raison, nr, "%s : %s", site, coupe ? "n'envoie plus rien" : (total ? "liste illisible" : "réponse vide"));
    else if (coupe) srv::noter_etat("Liste incomplète (site trop lent)");
    return ok;
}

// la copie de la carte SD, si les deux sites ne repondent pas
static bool charger_copie_sd()
{
    if (!s_sd_ok || !SD_MMC.exists(COPIE_LISTE)) return false;
    fs::File f = SD_MMC.open(COPIE_LISTE, FILE_READ);
    if (!f) return false;
    static Lignes l;
    l.n = 0;
    l.lignes = 0;
    srv::debut_liste();
    static uint8_t b[1024];
    int k;
    while ((k = f.read(b, sizeof b)) > 0) l.write(b, (size_t)k);
    f.close();
    l.flush();
    srv::fin_liste(l.lignes > 10);
    uint32_t quand = 0;
    fs::File d = SD_MMC.open(COPIE_DATE, FILE_READ);
    if (d) { char t[16] = ""; d.readBytesUntil('\n', t, sizeof t - 1); quand = strtoul(t, nullptr, 10); d.close(); }
    LOG.printf("[KIWI] Liste : copie de la carte SD chargee, %d recepteurs\n", srv::nb_publics());
    return srv::nb_publics() > 0;
}

// ---- liste des serveurs FM-DX (JSON) ----
class FluxFM : public Stream {
public:
    size_t total = 0;
    size_t write(uint8_t c) override { char ch = (char)c; fml::octets(&ch, 1); total++; return 1; }
    size_t write(const uint8_t *b, size_t n) override { fml::octets((const char *)b, n); total += n; return n; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override { }
};
static void tache_ft8(void *);
static volatile bool s_fml_demande = false, s_fml_cours = false;
static volatile uint32_t s_fml_relance = 0;        // echec : heure du nouvel essai prevu
static int s_fml_essais = 0;
static bool s_fml_incomplete = false;
// heure de depart de chaque telechargement : au-dela d'une minute, il ne bloque plus les autres
static uint32_t s_t_geo = 0, s_t_statut = 0, s_t_vhf = 0, s_t_fml = 0, s_t_liste = 0;
static bool occupe(bool cours, uint32_t t0) { return cours && millis() - t0 < 60000; }
void plat_fm_liste_telecharger() { s_fml_demande = true; }
bool plat_fm_liste_en_cours() { return s_fml_cours || s_fml_demande; }

// ---- liste des recepteurs OpenWebRX VHF/UHF (page carte de ReceiverBook) ----
class FluxVHF : public Stream {
public:
    size_t total = 0;
    size_t write(uint8_t c) override { char ch = (char)c; owl::octets(&ch, 1); total++; return 1; }
    size_t write(const uint8_t *b, size_t n) override { owl::octets((const char *)b, n); total += n; return n; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override { }
};
static volatile bool s_vhf_demande = false, s_vhf_cours = false;
void plat_vhf_liste_telecharger() { s_vhf_demande = true; }
bool plat_vhf_liste_en_cours() { return s_vhf_cours || s_vhf_demande; }

static void tache_vhf_liste(void *)
{
    static HTTPClient http;
    static WiFiClientSecure cs;
    static FluxVHF f;
    cs.setInsecure();
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.setUserAgent("ES3C28P-KiwiClient/0.9");
    owl::debut_liste();
    int pages = 0;
    char raison[48] = "site injoignable";
    const char *URL[2] = { owl::URL_VHF, owl::URL_UHF };
    for (int b = 0; b < 2; b++) {
        f.total = 0;
        owl::debut_page((uint8_t)(b + 1));
        if (!http.begin(cs, URL[b])) continue;
        int code = http.GET();
        LOG.printf("[KIWI] Liste %s : HTTP %d\n", b ? "UHF" : "VHF", code);
        if (code == 200) {
            http.writeToStream(&f);
            if (f.total > 1000) pages++;
        } else if (code > 0) {
            snprintf(raison, sizeof raison, "le site répond HTTP %d", code);
        }
        http.end();
        LOG.printf("[KIWI] Liste %s : %u octets, %d recepteurs\n", b ? "UHF" : "VHF", (unsigned)f.total, owl::nb());
    }
    owl::fin_liste(pages > 0, raison);
    LOG.printf("[KIWI] Liste VHF/UHF : %s\n", owl::etat_liste());
    s_vhf_cours = false;
    vTaskDelete(NULL);
}

// =========================================================================
// Webradios : recherche dans l'annuaire radio-browser.info et lecture des flux (HTTP ou HTTPS, ICY)
// Client HTTP fait main : les vieux serveurs Shoutcast repondent "ICY 200 OK", que HTTPClient refuse.
// =========================================================================
class FluxWR : public Stream {
public:
    size_t total = 0;
    size_t write(uint8_t c) override { char ch = (char)c; wr::resultats_octets(&ch, 1); total++; return 1; }
    size_t write(const uint8_t *b, size_t n) override { wr::resultats_octets((const char *)b, n); total += n; if (total % 8192 < n) vTaskDelay(1); return n; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override { }
};

static bool wr_couper_url(const char *url, bool *tls, char *hote, size_t nh, uint16_t *port, char *chemin, size_t nc)
{
    const char *p = url;
    *tls = false;
    if (!strncmp(p, "https://", 8)) { *tls = true; p += 8; }
    else if (!strncmp(p, "http://", 7)) p += 7;
    else return false;
    const char *f = p;
    while (*f && *f != '/' && *f != ':' && *f != '?') f++;
    size_t l = (size_t)(f - p);
    if (!l || l >= nh) return false;
    memcpy(hote, p, l);
    hote[l] = 0;
    *port = *tls ? 443 : 80;
    if (*f == ':') { *port = (uint16_t)atoi(f + 1); while (*f && *f != '/' && *f != '?') f++; }
    snprintf(chemin, nc, "%s%s", *f == '?' ? "/" : "", *f ? f : "/");
    return true;
}

// lit une ligne d'en-tete (sans \r\n) ; faux si rien dans le delai
static bool wr_ligne(Client &c, char *l, size_t n, uint32_t delai)
{
    size_t k = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < delai) {
        int ch = c.read();
        if (ch < 0) { if (!c.connected()) return k > 0; vTaskDelay(2); continue; }
        if (ch == '\n') { l[k] = 0; if (k && l[k - 1] == '\r') l[k - 1] = 0; return true; }
        if (k < n - 1) l[k++] = (char)ch;
    }
    return false;
}

static void wr_lire(const char *url0, uint32_t gen)
{
    static WiFiClient c;
    static WiFiClientSecure cs;
    static uint8_t *buf = nullptr;
    if (!buf) buf = (uint8_t *)plat_malloc_interne(4096);
    if (!buf) { wr::flux_fin("Mémoire insuffisante"); return; }
    cs.setInsecure();
    static char url[300];
    snprintf(url, sizeof url, "%s", url0);
    for (int saut = 0; saut < 6 && wr::continuer(gen); saut++) {
        bool tls;
        char hote[96], chemin[256];
        uint16_t port;
        if (!wr_couper_url(url, &tls, hote, sizeof hote, &port, chemin, sizeof chemin)) { wr::flux_fin("Adresse non comprise"); return; }
        Client &cl = tls ? (Client &)cs : (Client &)c;
        LOG.printf("[KIWI] Webradio : %s://%s:%u%s\n", tls ? "https" : "http", hote, port, chemin);
        bool ok = tls ? cs.connect(hote, port, 8000) : c.connect(hote, port, 8000);
        if (!ok) { wr::flux_fin("Station injoignable"); return; }
        char req[512];
        int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ES3C28P-Kiwi/%s\r\nIcy-MetaData: 1\r\nAccept: */*\r\nConnection: close\r\n\r\n", chemin, hote, KIWI_VERSION);
        cl.write((const uint8_t *)req, n);
        char l[300], type[64] = "", nom[64] = "", loc[300] = "";
        int code = 0, metaint = 0;
        if (wr_ligne(cl, l, sizeof l, 8000)) {
            const char *e = strchr(l, ' ');
            if (e) code = atoi(e + 1);                  // "HTTP/1.1 200 OK" ou "ICY 200 OK"
        }
        while (code && wr_ligne(cl, l, sizeof l, 5000) && l[0]) {
            char *dp = strchr(l, ':');
            if (!dp) continue;
            *dp = 0;
            const char *v = dp + 1;
            while (*v == ' ') v++;
            if (!strcasecmp(l, "content-type")) snprintf(type, sizeof type, "%s", v);
            else if (!strcasecmp(l, "icy-metaint")) metaint = atoi(v);
            else if (!strcasecmp(l, "icy-name")) snprintf(nom, sizeof nom, "%s", v);
            else if (!strcasecmp(l, "location")) snprintf(loc, sizeof loc, "%s", v);
        }
        for (char *q = type; *q; q++) *q = (char)tolower((unsigned char)*q);
        LOG.printf("[KIWI] Webradio : HTTP %d, %s, metaint %d\n", code, type, metaint);
        if (code >= 300 && code < 400 && loc[0]) {      // redirection
            cl.stop();
            if (loc[0] == '/') { char u[300]; snprintf(u, sizeof u, "%s://%s:%u%s", tls ? "https" : "http", hote, port, loc); snprintf(url, sizeof url, "%s", u); }
            else snprintf(url, sizeof url, "%s", loc);
            continue;
        }
        if (code != 200) {
            cl.stop();
            char m[48];
            snprintf(m, sizeof m, code ? "La station répond HTTP %d" : "Pas de réponse", code);
            wr::flux_fin(m);
            return;
        }
        // liste de lecture (.m3u, .pls) : on prend la premiere adresse
        if (strstr(type, "mpegurl") || strstr(type, "scpls") || strstr(type, "text/")) {
            char corps[2048];
            size_t k = 0;
            uint32_t t0 = millis();
            while (k < sizeof corps - 1 && millis() - t0 < 5000) {
                int ch = cl.read();
                if (ch < 0) { if (!cl.connected()) break; vTaskDelay(2); continue; }
                corps[k++] = (char)ch;
            }
            corps[k] = 0;
            cl.stop();
            if (strstr(corps, "#EXT-X-")) { wr::flux_fin("Flux HLS : pas lu par la carte"); return; }
            char *h = strstr(corps, "http");
            if (!h) { wr::flux_fin("Liste de lecture vide"); return; }
            char *f = h;
            while (*f && *f != '\r' && *f != '\n') f++;
            *f = 0;
            snprintf(url, sizeof url, "%s", h);
            continue;
        }
        wr::flux_debut(type, metaint, nom);
        uint32_t dernier = millis();
        while (wr::continuer(gen)) {
            int a = cl.available();
            if (a > 0) {
                int k = cl.read(buf, a < 4096 ? a : 4096);
                if (k > 0) { wr::flux_octets(buf, (size_t)k); dernier = millis(); continue; }
            }
            if (!cl.connected() || millis() - dernier > 10000) break;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        cl.stop();
        if (wr::continuer(gen)) wr::flux_fin(nullptr);   // coupure du serveur : on reconnecte
        return;
    }
    if (wr::continuer(gen)) wr::flux_fin("Trop de redirections");
}

static void tache_webradio(void *)
{
    static HTTPClient http;
    static WiFiClientSecure cs;
    static FluxWR f;
    static char url[300];
    uint32_t echecs = 0, gen_prec = 0;
    for (;;) {
        if (s_wifi != WIFI_OK) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (wr::requete(url, sizeof url)) {
            cs.setInsecure();
            http.useHTTP10(true);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.setTimeout(15000);
            http.setUserAgent("ES3C28P-Kiwi/" KIWI_VERSION);
            wr::resultats_debut();
            f.total = 0;
            bool ok = false;
            if (http.begin(cs, url)) {
                int code = http.GET();
                LOG.printf("[KIWI] Annuaire webradios : HTTP %d\n", code);
                if (code == 200) { http.writeToStream(&f); ok = true; }
                http.end();
            }
            wr::resultats_fin(ok);
            LOG.printf("[KIWI] Annuaire webradios : %u octets, %d stations\n", (unsigned)f.total, wr::nb_resultats());
            continue;
        }
        uint32_t gen;
        if (wr::a_ouvrir(url, sizeof url, &gen)) {
            if (gen != gen_prec) { echecs = 0; gen_prec = gen; }
            uint32_t t0 = millis();
            wr_lire(url, gen);
            if (millis() - t0 < 15000 && wr::etat() == wr::CONNEXION && ++echecs >= 3) wr::flux_fin("La station coupe sans cesse");
            else if (wr::etat() == wr::CONNEXION) vTaskDelay(pdMS_TO_TICKS(1000));   // avant de reconnecter
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ---- position de la carte : par l'adresse Internet (ip-api.com) ou par un nom de ville (open-meteo.com) ----
static char s_geo_url[256];
static volatile bool s_geo_cours = false;
static void tache_geo(void *)
{
    HTTPClient http;
    WiFiClientSecure cs;
    WiFiClient c;
    cs.setInsecure();
    http.useHTTP10(true);
    http.setTimeout(10000);
    http.setUserAgent("ES3C28P-KiwiClient/0.11");
    bool tls = !strncmp(s_geo_url, "https", 5);
    int code = -1;
    static char t[2048];
    t[0] = 0;
    if (tls ? http.begin(cs, s_geo_url) : http.begin(c, s_geo_url)) {
        code = http.GET();
        if (code == 200) {
            WiFiClient *flux = http.getStreamPtr();
            size_t l = 0;
            uint32_t t0 = millis();
            while (l < sizeof t - 1 && millis() - t0 < 10000 && (http.connected() || flux->available())) {
                int k = flux->read((uint8_t *)t + l, sizeof t - 1 - l);
                if (k > 0) l += k; else delay(10);
                int total = http.getSize();
                if (total > 0 && l >= (size_t)total) break;
            }
            t[l] = 0;
        }
        http.end();
    }
    geo::reponse(code, t);
    char e[96];
    geo::etiquette(e, sizeof e);
    LOG.printf("[KIWI] Position : HTTP %d -> %s %s\n", code, e, geo::etat());
    s_geo_cours = false;
    vTaskDelete(NULL);
}

// ---- propagation du jour : flux solaire et indice Kp (NOAA), toutes les 3 heures ----
static volatile bool s_sol_cours = false;
static bool telecharger_texte(const char *url, char *t, size_t n)
{
    HTTPClient http;
    WiFiClientSecure cs;
    cs.setInsecure();
    http.useHTTP10(true);
    http.setTimeout(10000);
    http.setUserAgent("ES3C28P-KiwiClient/0.13");
    t[0] = 0;
    int code = -1;
    if (http.begin(cs, url)) {
        code = http.GET();
        if (code == 200) {
            WiFiClient *flux = http.getStreamPtr();
            size_t l = 0;
            uint32_t t0 = millis();
            while (l < n - 1 && millis() - t0 < 10000 && (http.connected() || flux->available())) {
                int k = flux->read((uint8_t *)t + l, n - 1 - l);
                if (k > 0) l += k; else delay(10);
                int total = http.getSize();
                if (total > 0 && l >= (size_t)total) break;
            }
            t[l] = 0;
        }
        http.end();
    }
    LOG.printf("[KIWI] %s : HTTP %d, %u octets\n", url, code, (unsigned)strlen(t));
    return code == 200;
}

static volatile uint8_t s_noaa = 0;              // 0 active, 2 coupee par l'utilisateur, 3 coupee apres un redemarrage en pleine lecture
int plat_noaa() { return s_noaa; }
void plat_noaa_regler(bool active)
{
    s_noaa = active ? 0 : 2;
    s_prefs.putUChar("noaa", s_noaa);
}

static void tache_solaire(void *)
{
    s_prefs.putUChar("noaa", 1);                 // lecture en cours (voir setup)
    LOG.printf("[KIWI] Propagation NOAA : lecture (interne libre %u Ko)\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    static char *jf = nullptr, *jk = nullptr;
    if (!jf) jf = (char *)heap_caps_malloc(512, MALLOC_CAP_SPIRAM);
    if (!jk) jk = (char *)heap_caps_malloc(12000, MALLOC_CAP_SPIRAM);
    if (jf && jk) {
        telecharger_texte(quoi::URL_FLUX, jf, 512);
        telecharger_texte(quoi::URL_KP, jk, 12000);
        quoi::solaire_reponse(jf, jk, millis());
        float f, k;
        quoi::solaire(&f, &k);
        LOG.printf("[KIWI] Propagation : flux %.0f, Kp %.1f (-1 = inconnu)\n", f, k);
    } else quoi::solaire_reponse("", "", millis());
    LOG.printf("[KIWI] Propagation NOAA : fini, pile restante %u octets\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    s_prefs.putUChar("noaa", s_noaa == 2 ? 2 : 0);
    s_sol_cours = false;
    vTaskDelete(NULL);
}

// ---- status.json d'un recepteur OpenWebRX (plage de chaque bande), une fois par connexion ----
static char s_statut_url[160];
static volatile bool s_statut_cours = false;
static void tache_statut(void *)
{
    HTTPClient http;
    WiFiClientSecure cs;
    WiFiClient c;
    cs.setInsecure();
    http.useHTTP10(true);                             // pas de decoupage "chunked"
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    bool tls = !strncmp(s_statut_url, "https", 5);
    int n = -1;
    if (tls ? http.begin(cs, s_statut_url) : http.begin(c, s_statut_url)) {
        int code = http.GET();
        if (code == 200) {
            const size_t CAP = 48 * 1024;
            char *t = (char *)plat_malloc_ps(CAP);
            if (t) {
                WiFiClient *flux = http.getStreamPtr();
                size_t l = 0;
                uint32_t t0 = millis();
                while (l < CAP - 1 && millis() - t0 < 10000 && (http.connected() || flux->available())) {
                    int k = flux->read((uint8_t *)t + l, CAP - 1 - l);
                    if (k > 0) l += k; else delay(10);
                    int total = http.getSize();
                    if (total > 0 && l >= (size_t)total) break;
                }
                t[l] = 0;
                n = kw::statut_json(t);
                free(t);
            }
        }
        LOG.printf("[KIWI] status.json : HTTP %d, %d bandes\n", code, n);
        http.end();
    }
    s_statut_cours = false;
    vTaskDelete(NULL);
}

static void tache_fm_liste(void *)
{
    static HTTPClient http;
    static WiFiClientSecure cs;
    static FluxFM f;
    cs.setInsecure();
    f.total = 0;
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setUserAgent("ES3C28P-KiwiClient/0.7");
    bool ok = false;
    char raison[48] = "connexion impossible";
    if (http.begin(cs, fml::URL_LISTE)) {
        int code = http.GET();
        char e[64] = "";
        if (code < 0) cs.lastError(e, sizeof e);
        LOG.printf("[KIWI] Liste FM : HTTP %d %s %s (memoire interne %u o)\n", code, code < 0 ? http.errorToString(code).c_str() : "",
                      e, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (code == 200) {
            fml::debut_liste();
            // lecture avec limites de temps : le site peut garder la connexion ouverte sans plus rien envoyer
            WiFiClient *st = http.getStreamPtr();
            int taille = http.getSize();                           // -1 si le site ne l'annonce pas
            static uint8_t b[1024];
            uint32_t t0 = millis(), dernier = t0;
            bool coupe = false;
            while (st) {
                int a = st->available();
                if (a > 0) {
                    int k = st->read(b, a < (int)sizeof b ? a : (int)sizeof b);
                    if (k > 0) { f.write(b, (size_t)k); dernier = millis(); }
                    if (f.total % 16384 < (size_t)k) vTaskDelay(1);
                    continue;
                }
                if (!st->connected() || (taille > 0 && (int)f.total >= taille)) break;   // fin normale
                if (millis() - dernier > 15000 || millis() - t0 > 90000) { coupe = true; break; }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            LOG.printf("[KIWI] Liste FM : %u octets en %u ms%s\n", (unsigned)f.total, (unsigned)(millis() - t0), coupe ? " (attente trop longue : arretee)" : "");
            ok = f.total > 1000;
            if (!ok) snprintf(raison, sizeof raison, coupe ? "le site n'envoie plus rien" : "réponse vide");
            if (ok && coupe) s_fml_incomplete = true;
        } else if (code > 0) {
            snprintf(raison, sizeof raison, "le site répond HTTP %d", code);
        } else {
            snprintf(raison, sizeof raison, "connexion chiffrée impossible, mémoire %u Ko",
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
        }
        http.end();
    }
    if (ok) {
        fml::fin_liste(true);
        if (s_fml_incomplete) fml::noter_incomplete();
        s_fml_incomplete = false;
        s_fml_essais = 0;
        s_fml_relance = 0;
    }
    else { fml::echec(raison); s_fml_relance = millis() ? millis() : 1; }   // nouvel essai automatique dans 20 s
    LOG.printf("[KIWI] Liste FM : %u octets, %s\n", (unsigned)f.total, fml::etat_liste());
    s_fml_cours = false;
    vTaskDelete(NULL);
}

static volatile uint32_t s_liste_relance = 0;     // echec : nouvel essai prevu
static int s_liste_essais = 0;
static void tache_liste(void *)
{
    // liste vide (demarrage) : la copie de la carte SD tout de suite, le temps du telechargement
    static bool copie_vue = false;
    if (!copie_vue && srv::nb_publics() == 0) {
        copie_vue = true;
        if (charger_copie_sd()) srv::noter_etat("Copie de la carte SD, mise à jour…");
    }
    static char r1[96], r2[96];
    r1[0] = r2[0] = 0;
    bool ok = telecharger(cfg::R.url_liste, r1, sizeof r1);
    if (!ok && strcmp(cfg::R.url_liste, "http://kiwisdr.com/public/")) ok = telecharger("http://kiwisdr.com/public/", r2, sizeof r2);   // le secours
    if (ok) { s_liste_essais = 0; s_liste_relance = 0; }
    else {
        bool copie = srv::nb_publics() == 0 && charger_copie_sd();
        char m[200];
        snprintf(m, sizeof m, "%s%s%s%s", copie ? "Copie de la carte SD (sites injoignables) · " : (srv::nb_publics() ? "Ancienne liste gardée · " : ""),
                 r1, r2[0] ? " ; " : "", r2);
        srv::noter_etat(m);
        LOG.printf("[KIWI] Liste : echec (%s)\n", m);
        s_liste_relance = millis() ? millis() : 1;   // nouvel essai automatique dans 30 s
    }
    s_liste_cours = false;
    vTaskDelete(NULL);
}

// =========================================================================
// Reseau : deux WebSocket vers le KiwiSDR, portail web
// =========================================================================
static WsClient s_ws[2];
static uint32_t s_gen_vue = 0xFFFFFFFF;
static bool s_voulu = false;
static char s_hote[64];
static uint16_t s_port = 8073;
static bool s_chiffre = false;
static char s_chemin[2][64];
static int s_nb_canaux = 2;                     // KiwiSDR : SND + W/F ; OpenWebRX : une seule WebSocket
static char s_origine[96];
static uint32_t s_t_essai[2] = { 0, 0 };
static const char *NOM_CANAL[2] = { "SND", "W/F" };

static void connexions()
{
    uint32_t g = kw::generation();
    if (g != s_gen_vue) {
        s_gen_vue = g;
        for (int c = 0; c < 2; c++) s_ws[c].fermer();
        uint32_t horo;
        s_voulu = kw::cible(s_hote, sizeof s_hote, &s_port, &horo, &s_chiffre);
        if (s_voulu) {
            s_nb_canaux = kw::chemins(s_chemin[0], s_chemin[1], sizeof s_chemin[0]);
            (void)horo;
            if (s_port == (s_chiffre ? 443 : 80)) snprintf(s_origine, sizeof s_origine, "http%s://%s", s_chiffre ? "s" : "", s_hote);
            else snprintf(s_origine, sizeof s_origine, "http%s://%s:%u", s_chiffre ? "s" : "", s_hote, s_port);
            s_t_essai[0] = s_t_essai[1] = millis() - 10000;    // essai immediat
            LOG.printf("[KIWI] Connexion a %s:%u%s (%s)\n", s_hote, s_port, s_chemin[0],
                          kw::protocole() == cfg::OWRX ? "OpenWebRX" : "KiwiSDR");
        }
    }
    if (!s_voulu || s_wifi != WIFI_OK) return;
    for (int c = 0; c < s_nb_canaux; c++) {
        if (s_ws[c].ouvert() || millis() - s_t_essai[c] < 5000) continue;
        s_t_essai[c] = millis();
        if (s_ws[c].connecter(s_hote, s_port, s_chemin[c], s_origine, s_chiffre ? 1 : 0)) {
            LOG.printf("[KIWI] WebSocket %s ouvert\n", NOM_CANAL[c]);
            kw::ouvert(c);
        } else {
            LOG.printf("[KIWI] WebSocket %s : %s\n", NOM_CANAL[c], s_ws[c].erreur());
            // redirection HTTP (relais proxy.kiwisdr.com) : on suit l'adresse indiquee
            if (s_ws[c].redirection()[0] && kw::rediriger(s_ws[c].redirection())) {
                LOG.printf("[KIWI] Redirection vers %s\n", s_ws[c].redirection());
                return;
            }
            kw::echec_ouverture(c, s_ws[c].erreur());
        }
    }
}

static void recevoir()
{
    for (int c = 0; c < 2; c++) {
        if (!s_ws[c].ouvert()) continue;
        for (int k = 0; k < 16; k++) {             // au plus 16 messages par tour
            const uint8_t *d;
            size_t n;
            int r = s_ws[c].lire(&d, &n);
            if (r == 1) {
                kw::recu(c, d, n);
            } else {
                if (r < 0) {
                    LOG.printf("[KIWI] WebSocket %s ferme : %s\n", NOM_CANAL[c], s_ws[c].erreur());
                    kw::ferme(c);
                    s_t_essai[c] = millis();          // nouvel essai dans 5 s
                }
                break;
            }
        }
    }
}

// ---- FM-DX : deux WebSocket (texte + son MP3), souvent chiffres (https) ----
static WsClient s_wsfm[2];
static uint32_t s_fm_gen_vue = 0xFFFFFFFF;
static bool s_fm_voulu = false, s_fm_tls = false;
static char s_fm_hote[96], s_fm_base[64], s_fm_chemin[2][80], s_fm_origine[128];
static uint16_t s_fm_port = 443;
static uint32_t s_fm_essai[2] = { 0, 0 };
static const char *NOM_FM[2] = { "FM texte", "FM son" };

static void fm_connexions()
{
    uint32_t g = fm::generation();
    if (g != s_fm_gen_vue) {
        s_fm_gen_vue = g;
        for (int c = 0; c < 2; c++) s_wsfm[c].fermer();
        s_fm_voulu = fm::cible(s_fm_hote, sizeof s_fm_hote, &s_fm_port, &s_fm_tls, s_fm_base, sizeof s_fm_base);
        if (s_fm_voulu) {
            snprintf(s_fm_chemin[0], sizeof s_fm_chemin[0], "%stext", s_fm_base);
            snprintf(s_fm_chemin[1], sizeof s_fm_chemin[1], "%saudio", s_fm_base);
            if (s_fm_port == (s_fm_tls ? 443 : 80)) snprintf(s_fm_origine, sizeof s_fm_origine, "http%s://%s", s_fm_tls ? "s" : "", s_fm_hote);
            else snprintf(s_fm_origine, sizeof s_fm_origine, "http%s://%s:%u", s_fm_tls ? "s" : "", s_fm_hote, s_fm_port);
            s_fm_essai[0] = s_fm_essai[1] = millis() - 10000;
            LOG.printf("[KIWI] FM : connexion a %s (port %u%s)\n", s_fm_hote, s_fm_port, s_fm_tls ? ", chiffre" : "");
        }
    }
    if (!s_fm_voulu || s_wifi != WIFI_OK) return;
    // une seule ouverture par tour (texte d'abord, puis le son) : une poignee de main chiffree a la fois
    for (int c = 0; c < 2; c++) {
        if (s_wsfm[c].ouvert()) continue;
        if (millis() - s_fm_essai[c] < 5000) return;
        s_fm_essai[c] = millis();
        if (s_wsfm[c].connecter(s_fm_hote, s_fm_port, s_fm_chemin[c], s_fm_origine, s_fm_tls ? 1 : 0)) {
            LOG.printf("[KIWI] WebSocket %s ouvert (memoire interne libre %u o)\n", NOM_FM[c],
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            fm::ouvert(c);
        } else {
            LOG.printf("[KIWI] WebSocket %s : %s\n", NOM_FM[c], s_wsfm[c].erreur());
        }
        return;
    }
}

static void fm_recevoir()
{
    for (int c = 0; c < 2; c++) {
        if (!s_wsfm[c].ouvert()) continue;
        for (int k = 0; k < 16; k++) {
            const uint8_t *d;
            size_t n;
            int r = s_wsfm[c].lire(&d, &n);
            if (r == 1) {
                fm::recu(c, d, n);
            } else {
                if (r < 0) {
                    LOG.printf("[KIWI] WebSocket %s ferme : %s\n", NOM_FM[c], s_wsfm[c].erreur());
                    fm::ferme(c);
                    s_fm_essai[c] = millis();
                }
                break;
            }
        }
    }
}

static void tache_reseau(void *)
{
    bool web = false;
    char msg[192];
    for (;;) {
        wifi_suivre();
        if (!web && (s_wifi == WIFI_OK || s_wifi == WIFI_POINT_ACCES)) {
            portail_init(s_web);
            s_web.begin();
            web = true;
        }
        connexions();
        recevoir();
        kw::tic(millis());
        for (int c = 0; c < 2; c++)
            while (kw::sortant(c, msg, sizeof msg))
                if (s_ws[c].ouvert()) s_ws[c].envoyer_texte(msg);
        fm_connexions();
        fm_recevoir();
        static uint32_t t_wf = 0;
        if (kw::etat() == kw::ACTIF && millis() - t_wf > 10000) {   // diagnostic du waterfall
            t_wf = millis();
            uint32_t tr, ta, au;
            uint8_t d8[8];
            kw::diag_cascade(&tr, &ta, &au, d8);
            LOG.printf("[KIWI] W/F : %u trames en 10 s (taille %u o), %u autres messages binaires [%02X %02X %02X %02X], lignes affichees %u\n",
                          (unsigned)tr, (unsigned)ta, (unsigned)au, d8[0], d8[1], d8[2], d8[3], (unsigned)wf::compteur());
        }
        static uint32_t t_fm = 0;
        if (fm::actif() && millis() - t_fm > 5000) {    // diagnostic du son FM
            t_fm = millis();
            fm::Info fi;
            fm::info(&fi);
            LOG.printf("[KIWI] FM son : %d Hz, %d kbit/s, %u trames, decodage %d %% du temps, reserve %d ms, coupures %u\n",
                          fi.son_hz, fi.kbps, (unsigned)fi.trames, fi.charge, aud::reserve_ms(), (unsigned)aud::coupures());
        }
        static uint32_t t_mem = 0;
        if (millis() - t_mem > 30000) {                // diagnostic memoire toutes les 30 s
            t_mem = millis();
            LOG.printf("[KIWI] Memoire interne libre %u o (plus grand bloc %u), PSRAM libre %u Ko\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        }
        fm::tic(millis());
        for (int c = 0; c < 2; c++)
            while (fm::sortant(c, msg, sizeof msg))
                if (s_wsfm[c].ouvert()) s_wsfm[c].envoyer_texte(msg);
        // FT8 et webradio : leur tache n'est creee qu'au premier usage (memoire interne gardee pour le HTTPS)
        static bool ft8_lance = false, wr_lance = false;
        if (!ft8_lance && f8::actif()) {
            ft8_lance = true;
            // pile en PSRAM : le decodage ne touche ni a la flash ni au WiFi
            if (xTaskCreatePinnedToCoreWithCaps(tache_ft8, "ft8", 16384, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
                xTaskCreatePinnedToCore(tache_ft8, "ft8", 16384, NULL, 1, NULL, 0);
            LOG.println("[KIWI] FT8 : tache de decodage creee");
        }
        if (!wr_lance && (wr::actif() || wr::recherche_en_cours())) {
            wr_lance = true;
            // pile en PSRAM si possible : la memoire interne reste aux connexions chiffrees (listes, FM-DX)
            if (xTaskCreatePinnedToCoreWithCaps(tache_webradio, "webradio", 16384, NULL, 3, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
                xTaskCreatePinnedToCore(tache_webradio, "webradio", 16384, NULL, 3, NULL, 0);   // TLS, decodage MP3/AAC
            LOG.println("[KIWI] Webradio : tache creee");
        }
        if (s_liste_relance && !s_liste_demande && !s_liste_cours && millis() - s_liste_relance > 30000) {
            s_liste_relance = 0;
            if (++s_liste_essais <= 3) { s_liste_demande = true; LOG.printf("[KIWI] Liste KiwiSDR : nouvel essai %d/3\n", s_liste_essais); }
        }
        if (s_fml_relance && !s_fml_demande && millis() - s_fml_relance > 20000) {
            s_fml_relance = 0;
            if (++s_fml_essais <= 3) { s_fml_demande = true; LOG.printf("[KIWI] Liste FM : nouvel essai %d/3\n", s_fml_essais); }
        }
        if (!s_geo_cours && !s_statut_cours && !s_vhf_cours && !s_fml_cours && s_wifi == WIFI_OK && geo::a_demander(s_geo_url, sizeof s_geo_url)) {
            s_geo_cours = true;
            s_t_geo = millis();
            xTaskCreatePinnedToCore(tache_geo, "geo", 16384, NULL, 1, NULL, 1);
        }
        if (!s_geo_cours && !s_statut_cours && !s_vhf_cours && !s_fml_cours && s_wifi == WIFI_OK && kw::statut_a_demander(s_statut_url, sizeof s_statut_url)) {
            s_statut_cours = true;
            s_t_statut = millis();
            xTaskCreatePinnedToCore(tache_statut, "statut", 16384, NULL, 1, NULL, 1);
        }
        if (!s_sol_cours && !s_geo_cours && !s_statut_cours && !s_vhf_cours && !s_fml_cours && !s_liste_cours && s_wifi == WIFI_OK && s_noaa == 0 && millis() > 90000 && quoi::solaire_a_demander(millis())) {
            s_sol_cours = true;
            xTaskCreatePinnedToCore(tache_solaire, "solaire", 32768, NULL, 1, NULL, 1);
        }
        if (s_vhf_demande && !s_vhf_cours && !occupe(s_fml_cours, s_t_fml) && !occupe(s_statut_cours, s_t_statut) && !occupe(s_geo_cours, s_t_geo) &&
            !occupe(s_liste_cours, s_t_liste) && s_wifi == WIFI_OK) {
            s_vhf_demande = false;
            s_vhf_cours = true;
            s_t_vhf = millis();
            xTaskCreatePinnedToCore(tache_vhf_liste, "vhfliste", 16384, NULL, 1, NULL, 1);
        }
        // une seule connexion chiffree a la fois (memoire), mais un telechargement bloque ne bloque plus les autres
        if (s_fml_demande && !s_fml_cours && !occupe(s_vhf_cours, s_t_vhf) && !occupe(s_geo_cours, s_t_geo) && !occupe(s_statut_cours, s_t_statut) &&
            !occupe(s_liste_cours, s_t_liste) && s_wifi == WIFI_OK) {
            s_fml_demande = false;
            s_fml_cours = true;
            s_t_fml = millis();
            LOG.printf("[KIWI] Liste FM : telechargement (memoire interne libre %u o, plus grand bloc %u)\n",
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            xTaskCreatePinnedToCore(tache_fm_liste, "fmliste", 16384, NULL, 1, NULL, 1);
        }
        if (web) s_web.handleClient();
        if (s_liste_demande && !s_liste_cours && s_wifi == WIFI_OK) {
            s_liste_demande = false;
            s_liste_cours = true;
            s_t_liste = millis();
            xTaskCreatePinnedToCore(tache_liste, "liste", 16384, NULL, 1, NULL, 1);
        }
        vTaskDelay(1);
    }
}

// FT8 : le decodage d'une periode (1 a 3 s de calcul) tourne a part, quand le coeur 0 n'a rien d'autre a faire
static void tache_ft8(void *)
{
    for (;;) {
        f8::tache_un_tour();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void tache_moteur(void *)
{
    for (;;) {
        uint32_t attente = mot::etape(millis());
        vTaskDelay(pdMS_TO_TICKS(attente ? attente : 1));
    }
}

// =========================================================================
// Ecran : deux tampons, envoi sur le coeur 0
// =========================================================================
static uint16_t *s_tampon[2] = { nullptr, nullptr };
static bool s_double = false;
static int s_courant = 0;
static volatile int s_a_envoyer = 0;
static SemaphoreHandle_t s_sem_pret, s_sem_libre;

static void envoyer(const uint16_t *img) { tft.pushImage(0, 0, 320, 240, (uint16_t *)img); }

static void tache_ecran(void *)
{
    for (;;) {
        xSemaphoreTake(s_sem_pret, portMAX_DELAY);
        envoyer(s_tampon[s_a_envoyer]);
        xSemaphoreGive(s_sem_libre);
        vTaskDelay(1);
    }
}

static bool init_tampons()
{
    const size_t taille = 320 * 240 * sizeof(uint16_t);
    s_tampon[0] = (uint16_t *)heap_caps_malloc(taille, MALLOC_CAP_SPIRAM);
    s_tampon[1] = (uint16_t *)heap_caps_malloc(taille, MALLOC_CAP_SPIRAM);
    if (s_tampon[0] && s_tampon[1]) {
        s_double = true;
    } else {
        LOG.println("[KIWI] PSRAM absente : un seul tampon (active OPI PSRAM dans l'IDE)");
        if (s_tampon[0]) heap_caps_free(s_tampon[0]);
        if (s_tampon[1]) heap_caps_free(s_tampon[1]);
        s_tampon[1] = nullptr;
        s_tampon[0] = (uint16_t *)heap_caps_malloc(taille, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_tampon[0]) return false;
    }
    return true;
}

// =========================================================================
// Tactile (meme etalonnage que les autres projets : cle NVS partagee)
// =========================================================================
static bool s_tactile_ok = false;

static bool init_bus()
{
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = (gpio_num_t)I2C_SDA;
    cfg.scl_io_num = (gpio_num_t)I2C_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&cfg, &s_bus) == ESP_OK;
}

static bool lire_appui_brut(int *rx, int *ry)
{
    static int xs[64], ys[64];
    while (ft6336_touch_get_raw(rx, ry)) delay(10);
    uint32_t t0 = millis();
    while (!ft6336_touch_get_raw(rx, ry)) {
        if (millis() - t0 > 30000) return false;
        delay(10);
    }
    int n = 0;
    while (ft6336_touch_get_raw(&xs[n % 64], &ys[n % 64])) { n++; delay(10); }
    if (n > 64) n = 64;
    if (n < 3) return false;
    qsort(xs, n, sizeof(int), [](const void *a, const void *b) { return *(int *)a - *(int *)b; });
    qsort(ys, n, sizeof(int), [](const void *a, const void *b) { return *(int *)a - *(int *)b; });
    *rx = xs[n / 2];
    *ry = ys[n / 2];
    return true;
}

static void etalonner()
{
    bool prise = s_double && s_sem_libre;
    if (prise) xSemaphoreTake(s_sem_libre, portMAX_DELAY);       // l'ecran est a nous
    const int px[3] = { 30, 290, 160 }, py[3] = { 30, 120, 210 };
    int rx[3], ry[3];
    for (;;) {
        bool ok = true;
        for (int i = 0; i < 3 && ok; i++) {
            gfx::fb = s_tampon[0];
            gfx::rect(0, 0, 320, 240, gfx::rgb(11, 18, 32));
            gfx::texte(160, 96, "Étalonnage de l'écran", POLICE_16, gfx::rgb(230, 237, 247), 255, gfx::CENTRE);
            gfx::texte(160, 120, "Touche le centre de la croix", POLICE_13, gfx::rgb(143, 163, 191), 255, gfx::CENTRE);
            gfx::halo(px[i], py[i], 18, gfx::rgb(245, 158, 11), 160);
            gfx::trait(px[i] - 12, py[i], px[i] + 12, py[i], 2, gfx::rgb(245, 158, 11));
            gfx::trait(px[i], py[i] - 12, px[i], py[i] + 12, 2, gfx::rgb(245, 158, 11));
            envoyer(s_tampon[0]);
            ok = lire_appui_brut(&rx[i], &ry[i]);
        }
        if (ok && ft6336_touch_set_calibration(rx[0], ry[0], px[0], py[0], rx[1], ry[1], px[1], py[1],
                                               rx[2], ry[2], px[2], py[2])) {
            ft6336_touch_save_calibration();
            break;
        }
    }
    if (prise) xSemaphoreGive(s_sem_libre);
}

// =========================================================================
static char s_sd_etat[48] = "pas encore lue";

const char *plat_sd_etat() { return s_sd_etat; }

static void sd_resume()
{
    uint64_t total = SD_MMC.totalBytes(), utilise = SD_MMC.usedBytes();
    snprintf(s_sd_etat, sizeof s_sd_etat, "%.1f Go libres sur %.1f Go",
             (total - utilise) / 1e9, total / 1e9);
    for (char *p = s_sd_etat; *p; p++) if (*p == '.') *p = ',';
}

static void init_sd()
{
    s_sd_ok = false;
    if (!SD_MMC.setPins(SD_SCK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)) { snprintf(s_sd_etat, sizeof s_sd_etat, "broches refusées"); return; }
    // essais : 4 bits rapide, puis 1 bit, puis 1 bit lent (cartes capricieuses)
    bool ok = SD_MMC.begin("/sdcard", false);
    if (!ok) { SD_MMC.end(); ok = SD_MMC.begin("/sdcard", true); }
    if (!ok) { SD_MMC.end(); ok = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT); }
    if (!ok) {
        snprintf(s_sd_etat, sizeof s_sd_etat, "absente ou illisible (format ?)");
        LOG.println("[KIWI] Carte SD absente ou illisible : formate-la en FAT32 (ou bouton Formater)");
        return;
    }
    s_sd_ok = true;
    if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi");
    sd_resume();
    LOG.printf("[KIWI] Carte SD : %s\n", s_sd_etat);
}

bool plat_sd_formater()
{
    plat_fichier_fermer();
    SD_MMC.end();
    s_sd_ok = false;
    snprintf(s_sd_etat, sizeof s_sd_etat, "formatage…");
    SD_MMC.setPins(SD_SCK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    // format_if_mount_failed : la bibliotheque formate (FAT32) une carte qu'elle ne sait pas lire
    bool ok = SD_MMC.begin("/sdcard", true, true);
    if (!ok) { snprintf(s_sd_etat, sizeof s_sd_etat, "échec du formatage"); return false; }
    s_sd_ok = true;
    if (!SD_MMC.exists("/kiwi")) SD_MMC.mkdir("/kiwi");
    sd_resume();
    LOG.printf("[KIWI] Carte SD formatee : %s\n", s_sd_etat);
    return true;
}

// ---- bouton rotatif KY-040 : CLK -> IO2, DT -> IO14, SW -> IO21 (connecteur d'extension), + -> 3,3 V ----
// Les crans sont comptes par le compteur d'impulsions materiel (PCNT), en quadrature, avec filtre anti-rebond :
// aucun cran perdu, meme quand l'ecran est occupe. Un cran du KY-040 = 4 fronts.
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
static const int MOL_A = 2, MOL_B = 14, MOL_SW = 21;
static pcnt_unit_handle_t s_mol = nullptr;
static int s_mol_reste = 0;

static void molette_init()
{
    pcnt_unit_config_t uc = {};
    uc.low_limit = -30000;
    uc.high_limit = 30000;
    if (pcnt_new_unit(&uc, &s_mol) != ESP_OK) { s_mol = nullptr; return; }
    pcnt_glitch_filter_config_t fc = {};
    fc.max_glitch_ns = 1000;
    pcnt_unit_set_glitch_filter(s_mol, &fc);
    pcnt_chan_config_t ca = {}, cb = {};
    ca.edge_gpio_num = MOL_A; ca.level_gpio_num = MOL_B;
    cb.edge_gpio_num = MOL_B; cb.level_gpio_num = MOL_A;
    pcnt_channel_handle_t a = nullptr, b = nullptr;
    if (pcnt_new_channel(s_mol, &ca, &a) != ESP_OK || pcnt_new_channel(s_mol, &cb, &b) != ESP_OK) return;
    pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    gpio_pullup_en((gpio_num_t)MOL_A);                 // le KY-040 a deja ses resistances : sans effet genant
    gpio_pullup_en((gpio_num_t)MOL_B);
    pinMode(MOL_SW, INPUT_PULLUP);                     // SW n'a souvent pas de resistance sur le module
    pcnt_unit_enable(s_mol);
    pcnt_unit_clear_count(s_mol);
    pcnt_unit_start(s_mol);
    LOG.println("[KIWI] Bouton rotatif : CLK IO2, DT IO14, SW IO21");
}

bool plat_molette(int *crans, bool *appui)
{
    *crans = 0;
    *appui = false;
    if (!s_mol) return false;
    static int dernier = 0;                            // on ne remet pas a zero a chaque lecture : aucun front perdu
    int v = 0;
    pcnt_unit_get_count(s_mol, &v);
    s_mol_reste += v - dernier;
    dernier = v;
    if (v > 20000 || v < -20000) { pcnt_unit_clear_count(s_mol); dernier = 0; }
    *crans = s_mol_reste / 4;                          // 4 fronts par cran
    s_mol_reste -= *crans * 4;
    *appui = digitalRead(MOL_SW) == LOW;
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    lire_raison();
    LOG.printf("\n[KIWI] Client KiwiSDR %s\n", KIWI_VERSION);
    LOG.printf("[KIWI] Demarrage : %s\n", s_raison);
    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);   // avant toute connexion chiffree

    s_prefs.begin("kiwi", false);
    // Propagation du jour (NOAA) : "noaa" = 1 pendant la lecture. S'il vaut encore 1 ici, la carte a redemarre
    // en pleine lecture : on la coupe (3) au lieu de recommencer a chaque demarrage. 2 : coupee par l'utilisateur.
    {
        uint8_t g = s_prefs.getUChar("noaa", 0);
        if (g == 1) { g = 3; s_prefs.putUChar("noaa", 3); LOG.println("[KIWI] Propagation NOAA : la carte a redemarre pendant la lecture -> lecture coupee (page web, Reglages, pour la remettre)"); }
        s_noaa = g;
    }
    cfg::charger();
    geo::init();

    ledcAttachChannel(LCD_BL_PIN, 2000, 8, 0);
    plat_luminosite(cfg::R.luminosite);
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    if (!init_tampons()) {
        tft.drawString("Memoire insuffisante", 10, 10, 2);
        for (;;) delay(1000);
    }
    gfx::init(s_tampon[0]);

    if (init_bus()) {
        s_tactile_ok = ft6336_touch_init(s_bus, (gpio_num_t)TOUCH_RST, 320, 240);
        if (!s_tactile_ok) LOG.println("[KIWI] Tactile introuvable");
        else if (!ft6336_touch_load_calibration()) etalonner();
    }

    init_sd();
    aud::init();
    wf::init();
    srv::init();
    { int n = sta::charger(); LOG.printf("[KIWI] Stations : %d (%s)\n", n, sta::source()); }
    owl::charger_sd();
    molette_init();
    vhf::init();
    LOG.printf("[KIWI] Canaux VHF : %d (%s)\n", vhf::nb(), vhf::source());
    dec::init();
    f8::init();
    wr::init();
    kw::init();
    fm::init();
    mot::init();

    pinMode(PA_PIN, OUTPUT);
    plat_ampli(cfg::R.ampli_bas);
    if (init_i2s()) {
        xTaskCreatePinnedToCore(tache_audio, "audio", 4096, NULL, 6, NULL, 0);
        delay(30);                                    // MCLK doit tourner avant le codec
        if (!s_bus || !init_codec()) LOG.println("[KIWI] Codec ES8311 absent");
    } else {
        LOG.println("[KIWI] I2S en echec");
    }

    ui::init();

    xTaskCreatePinnedToCore(tache_reseau, "reseau", 16384, NULL, 3, NULL, 0);   // TLS (FM-DX en https)
    xTaskCreatePinnedToCore(tache_moteur, "moteur", 8192, NULL, 1, NULL, 1);
    if (s_double) {
        s_sem_pret = xSemaphoreCreateBinary();
        s_sem_libre = xSemaphoreCreateBinary();
        xSemaphoreGive(s_sem_libre);
        xTaskCreatePinnedToCore(tache_ecran, "ecran", 4096, NULL, 2, NULL, 0);
    }
    LOG.printf("[KIWI] Pret - PSRAM libre %u Ko, interne %u Ko\n",
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

void loop()
{
    if (s_etalonner) {
        s_etalonner = false;
        if (s_tactile_ok) etalonner();
    }
    int x = 0, y = 0;
    int x2 = 0, y2 = 0;
    int doigts = s_tactile_ok ? ft6336_touch_read2(&x, &y, &x2, &y2) : 0;
    ui::second_doigt(doigts == 2, x2, y2);              // avant toucher() : pincement a deux doigts
    ui::toucher(doigts >= 1, x, y);

    gfx::fb = s_tampon[s_courant];
    ui::image(millis());

    if (s_double) {
        xSemaphoreTake(s_sem_libre, portMAX_DELAY);
        s_a_envoyer = s_courant;
        xSemaphoreGive(s_sem_pret);
        s_courant ^= 1;
    } else {
        envoyer(s_tampon[0]);
    }
}
