// reglages.cpp - valeurs par defaut et sauvegarde des reglages
#include "reglages.h"
#include "plateforme.h"
#include <string.h>
#include <stddef.h>

namespace cfg {

static const uint16_t VERSION_REGLAGES = 6;
Reglages R;
static bool s_change = false;
static uint32_t s_t_change = 0;

const int PAS_HZ[] = { 10, 100, 500, 1000, 5000, 9000, 10000, 12500, 25000, 100000 };
const int NB_PAS = sizeof(PAS_HZ) / sizeof(PAS_HZ[0]);

static void defauts()
{
    memset(&R, 0, sizeof R);
    R.version = VERSION_REGLAGES;
    R.volume = 70;
    R.luminosite = 85;
    R.agc = 2;
    R.wf_vitesse = 2;
    R.palette = 0;
    R.adpcm = 1;
    R.ampli_bas = 1;            // valide sur la carte (projets audio precedents)
    R.zoom = 7;                 // ~234 kHz de large
    R.mode = LSB;
    R.pas = 1;                  // 100 Hz (LSB)
    R.tampon_ms = 350;
    R.bp[AM] = 9000;
    R.bp[LSB] = 2400;
    R.bp[USB] = 2400;
    R.bp[CW] = 400;
    R.bp[FM] = 12000;
    R.bp[WFM] = 150000;
    R.proto = KIWI;
    strcpy(R.chemin, "/");
    R.freq_khz = 7074.0;
    R.lat = 48.85f;             // Paris par defaut (modifiable dans le portail web)
    R.lon = 2.35f;
    R.port = 8073;
    strcpy(R.url_liste, "http://rx.linkfanel.net/kiwisdr_com.js");
    R.nr = 0;
    R.notch = 0;
    R.nb = 0;
    R.limiteur = 1;
    R.etiquettes = 1;
}

// reglages version 5 (jusqu'a la v0.8) : 5 modes, pas de champs OpenWebRX
struct ReglagesV5 {
    uint16_t version;
    uint8_t volume, luminosite, agc, wf_vitesse, palette, adpcm, ampli_bas, zoom, mode, pas;
    uint16_t tampon_ms;
    uint16_t bp[5];
    double freq_khz;
    float lat, lon;
    char hote[64];
    uint16_t port;
    char nom[48];
    char mdp[24];
    char url_liste[96];
    uint8_t nr, notch, nb, limiteur, etiquettes;
};

static void depuis_v5(const ReglagesV5 &a)
{
    R.volume = a.volume; R.luminosite = a.luminosite; R.agc = a.agc; R.wf_vitesse = a.wf_vitesse;
    R.palette = a.palette; R.adpcm = a.adpcm; R.ampli_bas = a.ampli_bas; R.zoom = a.zoom;
    R.mode = a.mode; R.pas = a.pas; R.tampon_ms = a.tampon_ms;
    for (int i = 0; i < 5; i++) R.bp[i] = a.bp[i];
    R.freq_khz = a.freq_khz; R.lat = a.lat; R.lon = a.lon;
    memcpy(R.hote, a.hote, sizeof R.hote); R.port = a.port;
    memcpy(R.nom, a.nom, sizeof R.nom); memcpy(R.mdp, a.mdp, sizeof R.mdp);
    memcpy(R.url_liste, a.url_liste, sizeof R.url_liste);
    R.nr = a.nr; R.notch = a.notch; R.nb = a.nb; R.limiteur = a.limiteur; R.etiquettes = a.etiquettes;
}

void charger()
{
    defauts();
    Reglages lu;
    if (plat_nvs_lire("reglages", &lu, sizeof lu) && lu.version == VERSION_REGLAGES) {
        R = lu;
    } else {
        // anciennes versions 3 a 5 : memes champs au debut (les nouveaux ajoutes a la fin)
        ReglagesV5 a;
        auto taille = [](size_t fin) { return (fin + alignof(ReglagesV5) - 1) / alignof(ReglagesV5) * alignof(ReglagesV5); };
        const size_t v4 = taille(offsetof(ReglagesV5, etiquettes)), v3 = taille(offsetof(ReglagesV5, nr));
        memset(&a, 0, sizeof a);
        if (plat_nvs_lire("reglages", &a, sizeof a) && a.version == 5) {
            depuis_v5(a);
        } else {
            ReglagesV5 d = a;
            memset(&a, 0, sizeof a);
            if (plat_nvs_lire("reglages", &a, v4) && a.version == 4) {
                a.etiquettes = 1; depuis_v5(a);
            } else if (plat_nvs_lire("reglages", &a, v3) && a.version == 3) {
                a.nr = 0; a.notch = 0; a.nb = 0; a.limiteur = 1; a.etiquettes = 1; depuis_v5(a);
            }
            (void)d;
        }
        R.version = VERSION_REGLAGES;
    }
    if (R.nr > 2) R.nr = 0;
    if (R.etiquettes > 3) R.etiquettes = 1;
    R.hote[sizeof R.hote - 1] = 0;
    R.nom[sizeof R.nom - 1] = 0;
    R.mdp[sizeof R.mdp - 1] = 0;
    R.url_liste[sizeof R.url_liste - 1] = 0;
    if (R.mode >= NB_MODES) R.mode = LSB;
    if (R.pas >= NB_PAS) R.pas = 3;
    if (R.zoom > 14) R.zoom = 7;
    if (R.proto > OWRX) R.proto = KIWI;
    R.chemin[sizeof R.chemin - 1] = 0;
    R.profil[sizeof R.profil - 1] = 0;
    if (R.chemin[0] != '/') strcpy(R.chemin, "/");
    if (R.bp[WFM] < 50000) R.bp[WFM] = 150000;
    if (R.freq_khz < 0 || R.freq_khz > FREQ_MAX_KHZ || (R.proto == KIWI && R.freq_khz > 32000)) R.freq_khz = 7074.0;
}

static bool s_gele = false;
void geler() { s_gele = true; }
bool gele() { return s_gele; }

void marquer()
{
    s_change = true;
    s_t_change = plat_ms();
}

void sauver()
{
    // on attend 3 s de calme : un curseur qu'on fait glisser n'use pas la flash
    if (s_gele || !s_change || plat_ms() - s_t_change < 3000) return;
    s_change = false;
    plat_nvs_ecrire("reglages", &R, sizeof R);
}

int pas_par_defaut(int mode, double khz)
{
    switch (mode) {
    case CW: return 0;          // 10 Hz
    case LSB: case USB: return 1;   // 100 Hz
    case FM: return khz > 30000 ? 7 : 4;   // 12,5 kHz en VHF/UHF, 5 kHz en ondes courtes
    case WFM: return 9;         // 100 kHz
    default: return khz > 30000 ? 8 : 3;   // AM : 25 kHz (aviation), 1 kHz en ondes courtes
    }
}

const char *nom_mode(int m)
{
    static const char *N[] = { "AM", "LSB", "USB", "CW", "FM", "WFM" };
    return (m >= 0 && m < NB_MODES) ? N[m] : "?";
}

const char *nom_mode_kiwi(int m)
{
    static const char *N[] = { "am", "lsb", "usb", "cw", "nbfm", "nbfm" };   // pas de FM large sur un KiwiSDR
    return (m >= 0 && m < NB_MODES) ? N[m] : "am";
}

int bp_min(int mode)
{
    static const int V[] = { 2000, 1000, 1000, 50, 6000, 50000 };
    return V[mode];
}

int bp_origine(int mode)
{
    static const int V[] = { 9000, 2400, 2400, 400, 12000, 150000 };
    return V[mode < NB_MODES ? mode : 0];
}

int bp_max(int mode)
{
    static const int V[] = { 12000, 4000, 4000, 1500, 16000, 200000 };
    return V[mode];
}

// bornes du filtre envoyees au KiwiSDR (Hz, par rapport a la frequence affichee)
void bande_passante(int mode, int l, int *bas, int *haut)
{
    if (l < bp_min(mode)) l = bp_min(mode);
    if (l > bp_max(mode)) l = bp_max(mode);
    switch (mode) {
    case LSB: *bas = -(300 + l); *haut = -300; break;
    case USB: *bas = 300; *haut = 300 + l; break;
    case CW:  *bas = 500 - l / 2; *haut = 500 + l / 2; break;   // note CW a 500 Hz
    default:  *bas = -l / 2; *haut = l / 2; break;              // AM, FM
    }
    if (mode >= 0 && mode < NB_MODES) {
        int d = decal[mode], m = decal_max(mode);
        if (d > m) d = m;
        if (d < -m) d = -m;
        *bas += d;
        *haut += d;
    }
}

void bande_passante_ecran(int mode, int l, int *bas, int *haut)
{
    bande_passante(mode, l, bas, haut);
    if (mode == CW) { *bas -= NOTE_CW; *haut -= NOTE_CW; }
}

int decal[NB_MODES] = { 0 };
int decal_max(int mode)
{
    switch (mode) {
    case AM: return 3000;
    case LSB: case USB: return 1000;
    case CW: return 400;
    default: return 0;
    }
}

}  // namespace cfg
