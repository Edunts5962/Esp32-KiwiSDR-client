// sim_main.cpp - simulateur PC du client KiwiSDR (meme code que la carte pour
// le protocole, l'audio, le waterfall, les decodeurs et les ecrans).
//
// Il joue le role du recepteur KiwiSDR : il fabrique de vraies trames
// "MSG", "SND" (son compresse IMA-ADPCM) et "W/F" (waterfall), les donne au
// code de la carte, rejoue un scenario tactile et enregistre des captures.
//
//   g++ -std=c++17 -O2 -I../../firmware/kiwi_console sim_main.cpp \
//       ../../firmware/kiwi_console/{gfx,polices,interface,kiwi,fm,fmliste,owrxliste,vhf,geo,audio,cascade,serveurs,stations,decodeurs,moteur,reglages}.cpp -o sim
//   ./sim <dossier_images>
#include "gfx.h"
#include "interface.h"
#include "kiwi.h"
#include "audio.h"
#include "cascade.h"
#include "serveurs.h"
#include "decodeurs.h"
#include "moteur.h"
#include "reglages.h"
#include "plateforme.h"
#include "vhf.h"
#include "geo.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static uint32_t g_ms = 0;
static std::map<std::string, std::vector<uint8_t>> g_nvs;
static int g_wifi = WIFI_OK;
static std::vector<uint8_t> g_fichier;
static std::string g_nom_fichier;

uint32_t plat_ms() { return g_ms; }
void plat_journal(const char *, const char *, const char *) {}
void plat_luminosite(int) {}
void plat_volume(int) {}
void plat_ampli(bool) {}
void plat_etalonner() {}
bool plat_nvs_lire(const char *k, void *b, size_t n)
{
    auto it = g_nvs.find(k);
    if (it == g_nvs.end() || it->second.size() != n) return false;
    memcpy(b, it->second.data(), n);
    return true;
}
void plat_nvs_ecrire(const char *k, const void *b, size_t n)
{
    g_nvs[k] = std::vector<uint8_t>((const uint8_t *)b, (const uint8_t *)b + n);
}
size_t plat_nvs_taille(const char *k) { auto it = g_nvs.find(k); return it == g_nvs.end() ? 0 : it->second.size(); }
int plat_wifi_etat() { return g_wifi; }
const char *plat_wifi_nom() { return g_wifi == WIFI_POINT_ACCES ? "KiwiSDR-ES3C28P" : "Maison"; }
const char *plat_wifi_ip() { return g_wifi == WIFI_POINT_ACCES ? "192.168.4.1" : "192.168.1.42"; }
int plat_wifi_rssi() { return -58; }
const char *plat_wifi_mdp() { return "kiwisdr123"; }
void plat_wifi_oublier() {}
static bool g_liste = false;
void plat_liste_telecharger() { g_liste = true; }
bool plat_liste_en_cours() { return g_liste; }
bool plat_sd() { return true; }
const char *plat_sd_etat() { return "29,6 Go libres sur 31,9 Go"; }
bool plat_sd_formater() { return true; }
bool plat_fichier_ouvrir(const char *c) { g_nom_fichier = c; g_fichier.clear(); return true; }
bool plat_fichier_ecrire(const void *d, size_t n) { g_fichier.insert(g_fichier.end(), (const uint8_t *)d, (const uint8_t *)d + n); return true; }
bool plat_fichier_ecrire_a(size_t p, const void *d, size_t n) { if (p + n > g_fichier.size()) return false; memcpy(&g_fichier[p], d, n); return true; }
void plat_fichier_fermer() { printf("[SIM] fichier %s : %zu octets\n", g_nom_fichier.c_str(), g_fichier.size()); }
static std::map<std::string, std::string> g_textes;      // petits fichiers de la "carte SD"
char *plat_texte_lire(const char *chemin, size_t max)
{
    auto it = g_textes.find(chemin);
    if (it == g_textes.end()) return nullptr;
    size_t n = it->second.size() < max ? it->second.size() : max;
    char *t = (char *)malloc(n + 1);
    memcpy(t, it->second.data(), n);
    t[n] = 0;
    return t;
}
bool plat_texte_ecrire(const char *chemin, const char *d, size_t n) { g_textes[chemin] = std::string(d, n); return true; }
bool plat_texte_ajouter(const char *chemin, const char *d, size_t n) { g_textes[chemin] += std::string(d, n); return true; }
void *plat_malloc_ps(size_t n) { return malloc(n); }
void plat_free_ps(void *p) { free(p); }
void *plat_malloc_interne(size_t n) { return malloc(n); }
void plat_fm_liste_telecharger() {}
bool plat_fm_liste_en_cours() { return false; }
int g_mol_crans = 0; bool g_mol_appui = false;   // bouton rotatif simule
bool plat_molette(int *crans, bool *appui) { *crans = g_mol_crans; g_mol_crans = 0; *appui = g_mol_appui; return true; }
void plat_vhf_liste_telecharger() {}
bool plat_vhf_liste_en_cours() { return false; }
uint32_t g_epoch = 1794514320;   // 12 novembre 2026, 20:12 UTC
uint32_t plat_epoch() { return g_epoch; }
uint64_t plat_epoch_ms() { return g_epoch ? (uint64_t)g_epoch * 1000 + g_ms : 0; }   // avance avec le temps simule
const char *plat_raison_demarrage() { return "mise sous tension"; }
int plat_noaa() { return 0; }
void plat_noaa_regler(bool) {}
void plat_horodatage(char *s, size_t n) { snprintf(s, n, "20261108-0612"); }

// ------------------------------------------------------------- captures
static uint16_t fb[320 * 240];
static const char *g_dossier = ".";

static void ppm(const char *nom)
{
    char chemin[512];
    snprintf(chemin, sizeof chemin, "%s/%s.ppm", g_dossier, nom);
    FILE *f = fopen(chemin, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
        r |= r >> 5; g |= g >> 6; b |= b >> 5;
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

// ------------------------------------------------------------- faux KiwiSDR
static const int16_t PAS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t AJ[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
struct Codeur { int idx = 0, prec = 0; };

static uint8_t coder(Codeur &e, int s)
{
    int pas = PAS[e.idx], d = s - e.prec;
    uint8_t c = 0;
    if (d < 0) { c = 8; d = -d; }
    int diff = pas >> 3;
    if (d >= pas) { c |= 4; d -= pas; diff += pas; }
    pas >>= 1;
    if (d >= pas) { c |= 2; d -= pas; diff += pas; }
    pas >>= 1;
    if (d >= pas) { c |= 1; diff += pas; }
    e.prec += (c & 8) ? -diff : diff;
    if (e.prec > 32767) e.prec = 32767;
    if (e.prec < -32768) e.prec = -32768;
    e.idx += AJ[c];
    if (e.idx < 0) e.idx = 0;
    if (e.idx > 88) e.idx = 88;
    return c;
}

static Codeur g_codeur;
static uint32_t g_seq = 0;

static void msg_kiwi(int canal, const char *t)
{
    std::vector<uint8_t> m = { 'M', 'S', 'G', ' ' };
    m.insert(m.end(), t, t + strlen(t));
    kw::recu(canal, m.data(), m.size());
}

static void snd(const int16_t *s, int n, float rssi)
{
    std::vector<uint8_t> f = { 'S', 'N', 'D', 0x10 };
    for (int i = 0; i < 4; i++) f.push_back((g_seq >> (8 * i)) & 255);
    g_seq++;
    uint16_t sm = (uint16_t)((rssi + 127) * 10);
    f.push_back(sm >> 8);
    f.push_back(sm & 255);
    for (int i = 0; i + 1 < n; i += 2) {
        uint8_t a = coder(g_codeur, s[i]), b = coder(g_codeur, s[i + 1]);
        f.push_back(a | (b << 4));
    }
    kw::recu(kw::SND, f.data(), f.size());
}

static float bruit() { return (rand() / (float)RAND_MAX - 0.5f) * 2; }

float g_sim_niveau = 0;        // les signaux simules sont plus forts (+) ou plus faibles (-) : un recepteur mieux ou moins bien place
static void ligne_wf(int t)
{
    std::vector<uint8_t> f = { 'W', '/', 'F', 0 };
    for (int i = 0; i < 12; i++) f.push_back(0);
    float cf = kw::cascade_centre_khz(), sp = kw::cascade_largeur_khz();
    for (int k = 0; k < 1024; k++) {
        float fk = cf - sp / 2 + sp * k / 1024;
        float db = -118 + 4 * bruit();
        auto sig = [&](float f0, float larg, float niv) {
            float d = (fk - f0) / larg;
            if (d * d < 9) db = fmaxf(db, niv + g_sim_niveau - 6 * d * d);
        };
        sig(7074.0f + 1.0f, 1.2f, -85 + 3 * bruit());                 // FT8
        if ((t / 3) % 4) sig(7030.5f, 0.15f, -80);                      // CW
        sig(7093.0f, 1.4f, ((t / 6) % 5) ? -78 : -110);                 // phonie
        sig(7121.0f, 0.12f, -90); sig(7121.17f, 0.12f, -92);            // RTTY
        sig(7150.0f, 4.0f, -95 + 2 * bruit());
        if (t % 40 > 20) sig(7040.0f + (t % 40) * 0.05f, 0.4f, -100);
        int v = (int)(db + 255 + 13);
        f.push_back(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    kw::recu(kw::WF, f.data(), f.size());
}

// ---- morse et RTTY synthetiques
static std::vector<int16_t> morse(const char *txt, float fhz, float wpm, float fs, float snr)
{
    static const char *T[36] = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
                                 "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
                                 "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----." };
    std::vector<int16_t> o;
    float pt = 1.2f / wpm;
    double ph = 0;
    auto ton = [&](float d, bool on) {
        int n = (int)(d * fs);
        for (int i = 0; i < n; i++) {
            float v = on ? 0.5f * sinf((float)ph) : 0;
            ph += 2 * M_PI * fhz / fs;
            o.push_back((int16_t)((v + 0.5f * bruit() / snr) * 20000));
        }
    };
    ton(0.5f, false);
    for (const char *p = txt; *p; p++) {
        if (*p == ' ') { ton(pt * 4, false); continue; }
        int i = (*p >= 'A' && *p <= 'Z') ? *p - 'A' : (*p >= '0' && *p <= '9' ? 26 + *p - '0' : -1);
        if (i < 0) continue;
        for (const char *c = T[i]; *c; c++) { ton(*c == '.' ? pt : 3 * pt, true); ton(pt, false); }
        ton(pt * 2, false);
    }
    ton(0.6f, false);
    return o;
}

static std::vector<int16_t> rtty(const char *txt, float centre, float shift, float baud, float fs)
{
    static const char *LT = "\0E\nA SIU\rDRJNFCKTZLWHYPQOBG\0MXV";
    std::vector<int16_t> o;
    double ph = 0;
    float mark = centre - shift / 2, space = centre + shift / 2;
    auto bit = [&](bool m, float nb) {
        int n = (int)(nb * fs / baud);
        for (int i = 0; i < n; i++) {
            ph += 2 * M_PI * (m ? mark : space) / fs;
            o.push_back((int16_t)((0.5f * sinf((float)ph) + 0.05f * bruit()) * 20000));
        }
    };
    bit(true, 20);
    for (const char *p = txt; *p; p++) {
        int code = 4;
        for (int k = 0; k < 32; k++) if (LT[k] == *p && k) { code = k; break; }
        bit(false, 1);
        for (int b = 0; b < 5; b++) bit((code >> b) & 1, 1);
        bit(true, 1.5f);
    }
    bit(true, 10);
    return o;
}

// ------------------------------------------------------------- scenario
static void avancer(int ms_total, const std::vector<int16_t> *audio = nullptr, size_t *pos = nullptr)
{
    // une image toutes les 33 ms ; le faux Kiwi envoie le son (blocs de 1024) et le waterfall
    static int t_wf = 0;
    static int16_t sortie[2 * 240];
    for (int t = 0; t < ms_total; t += 33) {
        g_ms += 33;
        // ~400 echantillons a 12 kHz toutes les 33 ms
        static int16_t bloc[400];
        for (int i = 0; i < 400; i++) {
            int16_t v = (int16_t)(800 * bruit());
            if (audio && pos && *pos < audio->size()) v = (*audio)[(*pos)++];
            bloc[i] = v;
        }
        snd(bloc, 400, -79);
        if ((g_ms / 120) % 2 == 0) ligne_wf(t_wf++);
        for (int k = 0; k < 6; k++) aud::lire_sortie(sortie, 240);         // 33 ms a 48 kHz
        kw::tic(g_ms);
        char m[192];
        for (int c = 0; c < 2; c++) while (kw::sortant(c, m, sizeof m)) printf("[%s] %s\n", c ? "W/F" : "SND", m);
        for (int k = 0; k < 3; k++) mot::etape(g_ms);
    }
}

static int g_anim = 0;
static void film(int images, int dx)
{
    // images pour l'animation : le waterfall defile, puis on glisse le doigt
    char nom[64];
    for (int i = 0; i < images; i++) {
        avancer(66);
        gfx::fb = fb;
        if (dx && i == images / 3) ui::toucher(true, 220, 160);
        if (dx && i > images / 3 && i < images / 3 + 20) ui::toucher(true, 220 - (i - images / 3) * dx / 20, 160);
        if (dx && i == images / 3 + 20) ui::toucher(false, 220 - dx, 160);
        ui::image(g_ms);
        snprintf(nom, sizeof nom, "anim_%04d", g_anim++);
        ppm(nom);
    }
}

static void image(const char *nom)
{
    gfx::fb = fb;
    ui::image(g_ms);
    if (nom) ppm(nom);
}

static void tap(int x, int y, int duree = 80)
{
    gfx::fb = fb;
    ui::toucher(true, x, y);
    for (int t = 0; t < duree; t += 33) { g_ms += 33; ui::toucher(true, x, y); ui::image(g_ms); }
    for (int k = 0; k < 2; k++) { g_ms += 33; ui::toucher(false, x, y); ui::image(g_ms); }
}

static void glisser(int x0, int y0, int x1, int y1)
{
    gfx::fb = fb;
    ui::toucher(true, x0, y0);
    for (int i = 1; i <= 10; i++) {
        g_ms += 33;
        ui::toucher(true, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10);
        ui::image(g_ms);
    }
    for (int k = 0; k < 2; k++) { g_ms += 33; ui::toucher(false, x1, y1); ui::image(g_ms); }
}

// tuile du menu (3 x 3) : 0 Radio, 1 Radio FM, 2 Recepteurs, 3 Quoi ecouter, 4 Favoris, 5 Decodeurs,
// 6 Enregistrer, 7 Reglages, 8 WiFi
static void tuile(int k) { tap(6 + (k % 3) * 104 + 49, 31 + (k / 3) * 71 + 33); }

// le temps passe et l'ecran est redessine (ce qui avance aussi la mesure des bandes)
static void vivre(int ms) { for (int t = 0; t < ms; t += 33) { avancer(33); ui::image(g_ms); } }

static void liste_factice()
{
    // une entree au format de rx.linkfanel.net, une au format de kiwisdr.com/public
    srv::debut_liste();
    const char *js[] = {
        "var kiwisdr_com =", "[", "\t{",
        "\t\t\"name\":\"Récepteur Bretagne, boucle magnétique\",", "\t\t\"loc\":\"Rennes, France\",",
        "\t\t\"gps\":\"(48.11, -1.68)\",", "\t\t\"users\":\"3\",", "\t\t\"users_max\":\"8\",",
        "\t\t\"offline\":\"no\",", "\t\t\"url\":\"http://bretagne.example.net:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Twente &amp; co\",", "\t\t\"loc\":\"Enschede\",", "\t\t\"gps\":\"(52.24, 6.85)\",",
        "\t\t\"users\":\"7\",", "\t\t\"users_max\":\"8\",", "\t\t\"url\":\"http://twente.example.org:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Hors ligne\",", "\t\t\"offline\":\"yes\",", "\t\t\"url\":\"http://off.example:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Jura suisse\",", "\t\t\"loc\":\"Neuchâtel\",", "\t\t\"gps\":\"(46.99, 6.93)\",",
        "\t\t\"users\":\"5\",", "\t\t\"users_max\":\"8\",", "\t\t\"url\":\"http://jura.example.ch:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Cornouailles (plein)\",", "\t\t\"loc\":\"Truro\",", "\t\t\"gps\":\"(50.26, -5.05)\",",
        "\t\t\"users\":\"4\",", "\t\t\"users_max\":\"4\",", "\t\t\"url\":\"http://cornwall.example.uk:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Bavière\",", "\t\t\"loc\":\"Munich\",", "\t\t\"gps\":\"(48.14, 11.58)\",",
        "\t\t\"users\":\"2\",", "\t\t\"users_max\":\"8\",", "\t\t\"url\":\"http://bayern.example.de:8074\"", "\t},",
        "\t{", "\t\t\"name\":\"Nouvelle-Zélande\",", "\t\t\"loc\":\"Wellington\",", "\t\t\"gps\":\"(-41.29, 174.78)\",",
        "\t\t\"users\":\"1\",", "\t\t\"users_max\":\"4\",", "\t\t\"url\":\"http://nz.example.nz:8073\"", "\t},",
        "\t{", "\t\t\"name\":\"Virginie\",", "\t\t\"loc\":\"USA\",", "\t\t\"gps\":\"(37.5, -77.4)\",",
        "\t\t\"users\":\"0\",", "\t\t\"users_max\":\"4\",", "\t\t\"url\":\"https://virginia.example.com\"", "\t},",
        "]", ";",
        // format HTML
        "<div class='cl-entry'>", "<div class='cl-info'>", "<!-- name=Récepteur des Alpes -->",
        "<!-- loc=Grenoble -->", "<!-- gps=(45.19, 5.72) -->", "<!-- users=1 -->", "<!-- users_max=4 -->",
        "<!-- offline=no -->", "<a href='http://alpes.example.fr:8073' target='_blank'>http://alpes.example.fr:8073</a>",
        nullptr };
    for (int i = 0; js[i]; i++) srv::ligne_liste(js[i]);
    srv::fin_liste(true);
    g_liste = false;
}

int main(int argc, char **argv)
{
    if (argc > 1) g_dossier = argv[1];
    gfx::init(fb);
    cfg::charger();
    geo::init();
    aud::init();
    wf::init();
    srv::init();
    dec::init();
    kw::init();
    mot::init();
    vhf::init();
    ui::init();

    // --- 1. ecran d'accueil sans recepteur
    image("01_sans_recepteur");

    // --- 2. liste publique (les deux formats)
    liste_factice();
    printf("[SIM] %d recepteurs dans la liste : %s\n", srv::nb_publics(), srv::etat_liste());
    for (int i = 0; i < srv::nb_publics(); i++) {
        const srv::Serveur *v[1000];
        int n = srv::filtrer(srv::PROCHES, v, 1000);
        if (i == 0) for (int k = 0; k < n; k++) printf("       %-40s %s:%u  %u km  %u/%u  region %u\n", v[k]->nom, v[k]->hote, v[k]->port, v[k]->km, v[k]->users, v[k]->umax, v[k]->region);
    }
    tap(160, 157);                                      // bouton "Recepteurs"
    image("02_recepteurs");

    // --- 3. connexion au premier de la liste (le plus proche)
    tap(150, 56 + 14);
    kw::tic(g_ms);
    printf("[SIM] etat : %s\n", kw::message());
    kw::ouvert(kw::SND);
    kw::ouvert(kw::WF);
    msg_kiwi(kw::SND, "version_maj=1 version_min=826");
    msg_kiwi(kw::SND, "bandwidth=30000000");
    msg_kiwi(kw::SND, "audio_rate=12000");
    msg_kiwi(kw::SND, "sample_rate=12001.135");
    msg_kiwi(kw::WF, "wf_setup");
    avancer(4000);
    printf("[SIM] etat : %s, reserve %d ms, coupures %u\n", kw::message(), aud::reserve_ms(), (unsigned)aud::coupures());
    msg_kiwi(kw::WF, "mkr=[{\"t\":4},{\"g\":1,\"f\":7093.000,\"lo\":0,\"hi\":0,\"o\":0,\"s\":0,\"fl\":3,\"b\":0,\"e\":2400,"
                     "\"c\":\"F\",\"l\":\"F\",\"t\":\"\",\"i\":\"Radioamateurs%20LSB\"},{\"g\":2,\"f\":7030.500,\"fl\":4,\"i\":\"Balise%20CW\"},"
                     "{\"g\":3,\"f\":7121.000,\"fl\":2,\"i\":\"DDK9%20RTTY%20m%C3%A9t%C3%A9o\"},{\"g\":4,\"f\":7150.000,\"fl\":0,\"i\":\"Radio%20Exemple\"},"
                     "{\"g\":5,\"f\":7074.000,\"fl\":2,\"i\":\"FT8\"}]");
    avancer(300);
    image("03_radio");
    film(70, 90);

    // --- 4. glisser sur le waterfall (accord), zoom
    glisser(200, 160, 120, 160);
    avancer(1500);
    image("04_radio_accord");
    printf("[SIM] frequence apres glisser : %.3f kHz\n", cfg::R.freq_khz);
    tap(304, 80);                                       // zoom +
    avancer(1500);
    image("05_radio_zoom");

    // --- 5. menu
    tap(283, 223);
    image("06_menu");

    // --- 6. frequence
    tuile(3);                                           // Quoi ecouter : estimation, puis mesure
    vivre(300);
    image("18_quoi_ecouter");
    tap(280, 40);                                       // Mesurer
    vivre(4000);
    image("19_quoi_mesure");
    for (int i = 0; i < 60 && kw::cascade_forcee(); i++) vivre(1000);
    vivre(3500);
    image("20_quoi_mesure_faite");
    tap(270, 12);                                       // bouton Frequence (clavier)
    tap(163 + 24, 32 + 19);                             // 1
    tap(163 + 51 + 24, 32 + 41 * 2 + 19);               // 8
    tap(163 + 24, 32 + 19);                             // 1
    tap(163 + 51 + 24, 32 + 41 * 3 + 19);               // 0
    tap(163 + 24, 32 + 41 * 3 + 19);                    // ,
    tap(163 + 51 * 2 + 24, 32 + 41 * 1 + 19);           // 6
    image("07_frequence");
    tap(238, 216);                                      // OK
    printf("[SIM] frequence saisie : %.3f kHz\n", cfg::R.freq_khz);

    // --- 7. decodeur CW : son morse a 700 Hz, 18 mots/min
    tap(283, 223);                                      // menu
    tuile(5);                                           // Decodeurs
    std::vector<int16_t> cw = morse("CQ CQ DE F4XYZ F4XYZ K", 700, 18, 12000, 6);
    size_t pos = 0;
    avancer(1200, &cw, &pos);
    dec::caler();
    pos = 0;
    avancer((int)(cw.size() / 12.0) + 3000, &cw, &pos);
    image("08_decodeur_cw");
    printf("[SIM] CW decode : \"%s\" (%s)\n", dec::texte(), dec::info1());

    // --- 8. RTTY 45,45 bauds, shift 170 Hz
    tap(38 + 52 + 24, 13);                              // onglet RTTY
    std::vector<int16_t> rt = rtty("RYRYRY CQ CQ DE DDK9 QSL ", 1500, 170, 45.45f, 12000);
    pos = 0;
    avancer(1500, &rt, &pos);
    dec::caler();
    pos = 0;
    avancer((int)(rt.size() / 12.0) + 1500, &rt, &pos);
    image("09_decodeur_rtty");
    printf("[SIM] RTTY decode : \"%s\" (tonalite %.0f Hz)\n", dec::texte(), dec::tonalite_hz());

    // --- 9. FAX : barres verticales
    tap(38 + 104 + 24, 13);
    std::vector<int16_t> fax;
    {
        double ph = 0;
        for (int l = 0; l < 120; l++)
            for (int i = 0; i < 6000; i++) {
                float x = i / 6000.0f;
                float g = (fmodf(x * 8 + l * 0.02f, 1.0f) < 0.5f) ? 1 : 0;
                if (l > 40 && l < 60 && x > 0.3f && x < 0.7f) g = 0.5f;
                ph += 2 * M_PI * (1500 + 800 * g) / 12000;
                fax.push_back((int16_t)(0.5 * sin(ph) * 20000));
            }
    }
    pos = 0;
    avancer((int)(fax.size() / 12.0), &fax, &pos);
    image("10_decodeur_fax");
    tap(260, 222);                                      // Sauver SD
    avancer(200);

    // --- 10. reglages, favoris, wifi
    tap(15, 13);                                        // retour (menu)
    tuile(7);                                           // Reglages
    image("11_reglages");
    tap(15, 13);
    tuile(8);                                           // WiFi
    image("12_wifi");
    tap(15, 13);
    tuile(0);                                           // Radio
    tap(227, 57);                                       // etoile : favori
    image("13_radio_favori");
    tap(220, 217);                                      // AUDIO : ouvre le panneau
    tap(220 - 132 + 160, 70 + 4 + 33 + 13);             // AGC Moyen -> Rapide ?
    tap(86 + 2 * 78 + 30, 70 + 4 + 33 + 30 + 13);       // reduction de bruit Forte
    tap(80, 70 + 4 + 33 + 30 + 30 + 13);                // anti-sifflement
    glisser(200, 89, 150, 89);                          // volume
    image("17_audio");
    tap(220, 217);                                      // referme
    tap(283, 223);
    tuile(4);                                           // Favoris
    image("14_favoris");

    // --- 11. recepteur plein
    tap(15, 13);
    tuile(0);
    msg_kiwi(kw::SND, "too_busy=4");
    avancer(300);
    image("15_occupe");
    printf("[SIM] etat : %s\n", kw::message());

    // --- 12. point d'acces
    g_wifi = WIFI_POINT_ACCES;
    kw::deconnecter();
    avancer(200);
    image("16_point_acces");
    printf("[SIM] fin\n");
    return 0;
}
