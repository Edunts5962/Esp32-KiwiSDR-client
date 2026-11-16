// interface.cpp - ecrans du client KiwiSDR
//
//   RADIO       frequence, S-metre, spectre, waterfall tactile, barre de boutons
//   MENU        8 tuiles
//   RECEPTEURS  liste publique triee par distance, filtres par region, perso
//   FREQUENCE   pave numerique + raccourcis de bandes
//   DECODEURS   CW / RTTY / FAX
//   REGLAGES    liste a faire defiler
//   FAVORIS     stations memorisees
//   WIFI        etat du reseau et adresse du portail web
//
// Gestes : toucher = action ; glisser sur le waterfall = accorder ;
// toucher le waterfall = aller sur ce signal ; appui long = action secondaire.
#include "interface.h"
#include "gfx.h"
#include "kiwi.h"
#include "fm.h"
#include "fmliste.h"
#include "owrxliste.h"
#include "vhf.h"
#include "geo.h"
#include "quoi.h"
#include "stations.h"
#include "carte_monde.h"
#include <algorithm>
#include <ctype.h>
#include "audio.h"
#include "cascade.h"
#include "serveurs.h"
#include "decodeurs.h"
#include "ft8.h"
#include "webradio.h"
#include "moteur.h"
#include "reglages.h"
#include "plateforme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace gfx;

namespace ui {

// ============================ Palette (maquette) ============================
static const uint16_t FOND    = rgb(11, 18, 32);
static const uint16_t PAN     = rgb(18, 26, 43);
static const uint16_t LIGNE   = rgb(34, 48, 74);
static const uint16_t BTN     = rgb(26, 36, 56);
static const uint16_t TXT     = rgb(230, 237, 247);
static const uint16_t TXT2    = rgb(143, 163, 191);
static const uint16_t CYAN    = rgb(56, 189, 248);
static const uint16_t BLEU    = rgb(14, 165, 233);
static const uint16_t SOMBRE  = rgb(6, 18, 31);
static const uint16_t AMBRE   = rgb(245, 158, 11);
static const uint16_t VERT    = rgb(34, 197, 94);
static const uint16_t JAUNE   = rgb(250, 204, 21);
static const uint16_t ROUGE   = rgb(239, 68, 68);
static const uint16_t PUCE    = rgb(30, 58, 95);
static const uint16_t PUCE_T  = rgb(125, 211, 252);
static const uint16_t NOIR    = rgb(5, 10, 20);
static const uint16_t VERT_T  = rgb(74, 222, 128);

// ============================ Ecrans ========================================
enum { E_RADIO = 0, E_MENU, E_RECEPTEURS, E_FREQ, E_DECODEURS, E_REGLAGES, E_FAVORIS, E_WIFI, E_FM, E_FM_LISTE, E_CANAUX, E_FM_TROUVEES, E_QUOI, E_CARTE, E_WEBRADIO };
static int ecran = E_RADIO;
static uint32_t t_ms = 0;
static uint32_t t_dec_web = 0;      // derniere demande du portail (decodeurs)
static float s_ips = 0;
static uint32_t s_ips_t = 0;
static int s_ips_n = 0;

// ============================ Zones tactiles ================================
enum { Z_GLISSE = 1, Z_LONG = 2, Z_APPUI = 4, Z_COUPE_HAUT = 8, Z_COUPE_BAS = 16 };   // Z_APPUI : agit des le contact du doigt ; Z_COUPE_* : en partie cachee (liste qui defile)
struct Zone { int16_t x, y, l, h; int16_t id; uint8_t fl; int16_t c0, c1; };   // c0, c1 : haut et bas de la zone de dessin
static Zone zs[96], zs_prec[96];
static int nz = 0, nz_prec = 0;

static void zone(int x, int y, int l, int h, int id, int fl = 0)
{
    // une zone tactile ne depasse pas la zone de dessin en cours : une ligne de liste a moitie cachee
    // sous l'en-tete ne doit pas prendre les touchers du bouton Retour
    int x0, y0, x1, y1;
    gfx::decoupe_lire(&x0, &y0, &x1, &y1);
    if (x < x0) { l -= x0 - x; x = x0; }
    if (y < y0) { h -= y0 - y; y = y0; fl |= Z_COUPE_HAUT; }
    if (x + l > x1) l = x1 - x;
    if (y + h > y1) { h = y1 - y; fl |= Z_COUPE_BAS; }
    if (l <= 0 || h <= 0) return;
    if (nz < 96) zs[nz++] = { (int16_t)x, (int16_t)y, (int16_t)l, (int16_t)h, (int16_t)id, (uint8_t)fl, (int16_t)y0, (int16_t)y1 };
}

// curseurs : position et nombre de pas, notes au dessin (le bouton rotatif s'en sert)
struct CurInfo { int16_t id, x, l, pas; float t; };
static CurInfo cur_inf[12], cur_prec[12];
static int ncur = 0, ncur_prec = 0;
static void reg_curseur(int id, int x, int l, float t, int pas)
{
    if (ncur < 12) cur_inf[ncur++] = CurInfo{ (int16_t)id, (int16_t)x, (int16_t)l, (int16_t)(pas < 1 ? 1 : pas), t };
}

static const Zone *chercher(int x, int y)
{
    for (int i = nz_prec - 1; i >= 0; i--) {
        const Zone &z = zs_prec[i];
        if (x >= z.x && x < z.x + z.l && y >= z.y && y < z.y + z.h) return &z;
    }
    return nullptr;
}

static bool tc_appui = false, tc_glisse = false, tc_long = false;
static int tc_x = 0, tc_y = 0, tc_x0 = 0, tc_y0 = 0, tc_xp = 0, tc_yp = 0;
static uint32_t tc_t0 = 0;
static Zone tc_z = { 0, 0, 0, 0, -1, 0 };

// ============================ Identifiants ===================================
enum {
    B_RETOUR = 1, B_MENU, B_MOINS, B_PLUS, B_MODE, B_BANDE, B_FAV, B_FREQ, B_PAS, B_ZOOM_M, B_ZOOM_P,
    B_CASCADE, B_SERVEUR, B_VOLUME, B_CHOISIR, B_REESSAYER, B_ACTUALISER,
    B_TUILE = 40,                  // + 0..8
    B_REGION = 60,                 // + 0..6
    B_LISTE = 80,                  // + ligne visible
    B_TOUCHE = 120,                // + 0..14 (pave)
    B_BANDE_RAC = 140,             // + 0..11
    B_ONGLET = 160,                // + 0..2
    B_DEC_EFFACER = 170, B_DEC_CALER, B_DEC_INV, B_DEC_SAUVER, B_DEC_INFO1, B_DEC_INFO2, B_DEC_SPECTRE, B_DEC_IMAGE, B_DEC_LISTE, B_DEC_CARTE,
    B_REG = 200,                   // + ligne de reglage
    B_REG_LISTE = 260,
    B_FAV_LIGNE = 280,             // + ligne
    B_OUBLIER_WIFI = 320,
    B_AUDIO = 330, B_AUD_VOL, B_AUD_NOTCH, B_AUD_NB, B_AUD_FOND, B_AUD_GAIN, B_VOL_BORD, B_PROCHE,
    B_AUD_AGC = 340,               // + 0..3
    B_AUD_NR = 350,                // + 0..2
    B_ETIQ = 360,                  // + 0..47 (noms des stations sur le waterfall)
    B_PRESET = 420,                // + preregages
    B_FM_FREQ = 470, B_FM_SERVEURS, B_FM_ARRET, B_FM_CHOISIR, B_FM_MEILLEUR,
    B_SCAN_BAS = 480, B_SCAN_HAUT, B_SCAN_SENS = 484,   // + 0..2
    B_LISTE_HF = 490, B_LISTE_VHF, B_CANAUX, B_CAN_SCAN,
    B_CAN_CAT = 500,               // + 0..4
    B_MOL_REG = 506,               // + 0..2
    B_FMSCAN_BAS = 510, B_FMSCAN_HAUT, B_FM_TROUVEES, B_FMT_EFFACER, B_CAN_ICI, B_LISTE_PROCHES, B_LOCALISER, B_BP, B_BP_CURS, B_BP_PRE0, B_BP_PRE1, B_BP_PRE2, B_BP_ORIG, B_BP_DECAL, B_SMETRE,
    B_Q_CAT = 530,                 // + 0..3 ("Quoi écouter")
    B_Q_MESURER = 534, B_Q_RECEPTEURS, B_QUOI, B_FAV_SCAN, B_OPT_SAM, B_OPT_REPRISE, B_OPT_VOX,
    B_SQUELCH = 544,               // + 0..3
    B_NUIT = 548,                  // + 0..3
    B_CW_BANDEAU = 570, B_OPT_CWTXT, B_AUD_SEUIL,
    B_SAM_SEG = 576,               // + 0..3 (AM synchrone : arret, deux bandes, haute, basse)
    B_WR_STOP = 600, B_WR_FAV, B_WR_FM, B_WR_VOL, B_FM_WEB,
    B_WR_RAPIDE = 610,             // + 0..5
    B_WR_ETOILE = 620,             // + ligne visible
    B_CARTE = 552, B_CARTE_KIWI, B_CARTE_VHF, B_CARTE_MONDE, B_CARTE_ECOUTER, B_CARTE_OUVRIR, B_MEILLEUR, B_CARTE_PLUS, B_CARTE_MOINS,
};

// ============================ Petits outils ==================================
static char toast_txt[96] = "";
static uint32_t toast_t0 = 0;

void message(const char *t)
{
    snprintf(toast_txt, sizeof toast_txt, "%s", t);
    toast_t0 = plat_ms() ? plat_ms() : 1;
}

// copie s dans out en coupant avec "…" pour tenir dans maxl pixels
static const char *tronquer(const char *s, const PoliceAA &p, int maxl)
{
    static char out[128];
    snprintf(out, sizeof out, "%s", s);
    if (largeur_texte(out, p) <= maxl) return out;
    int n = strlen(out);
    while (n > 0) {
        n--;
        while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;
        out[n] = 0;
        char t[132];
        snprintf(t, sizeof t, "%s…", out);
        if (largeur_texte(t, p) <= maxl) { snprintf(out, sizeof out, "%s", t); return out; }
    }
    return "…";
}

// Texte defilant (facon autoradio) : fixe s'il tient dans maxl pixels, sinon
// pause de 2 s sur le debut puis defilement de droite a gauche a 30 px/s, en boucle.
// Chaque emplacement (slot) repart du debut quand son texte change.
enum { DEF_RECEPTEUR, DEF_FM_SERVEUR, DEF_FM_RT, DEF_NB };
static void texte_defile(int slot, int x, int y, int maxl, const char *s, const PoliceAA &p, uint16_t c, uint16_t fond)
{
    static uint32_t empreinte[DEF_NB], t0[DEF_NB];
    uint32_t h = 2166136261u;                           // empreinte FNV-1a du texte
    for (const char *q = s; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    if (h != empreinte[slot]) { empreinte[slot] = h; t0[slot] = t_ms; }
    int l = largeur_texte(s, p);
    if (l <= maxl) { texte(x, y, s, p, c); return; }
    const int ECART = 36, VITESSE = 30, PAUSE = 2000;
    int cycle = l + ECART;
    uint32_t periode = PAUSE + (uint32_t)cycle * 1000 / VITESSE;
    uint32_t ph = (t_ms - t0[slot]) % periode;
    int dx = ph < (uint32_t)PAUSE ? 0 : (int)((ph - PAUSE) * VITESSE / 1000);
    decoupe(x, y - 2, x + maxl, y + hauteur(p) + 3);
    texte(x - dx, y, s, p, c);
    if (dx > cycle - maxl) texte(x - dx + cycle, y, s, p, c);   // reprise du debut
    int ht = hauteur(p) + 5;
    for (int k = 0; k < 10; k++) {                      // fondu sur les bords
        uint8_t a = (uint8_t)(255 - k * 25);
        rect(x + maxl - 1 - k, y - 2, 1, ht, fond, a);
        if (dx > 0) rect(x + k, y - 2, 1, ht, fond, a);
    }
    sans_decoupe();
}

static void bouton(int x, int y, int l, int h, const char *t, int id, int style = 0,
                   const PoliceAA &p = POLICE_13, int fl = 0)
{
    bool appuye = tc_appui && tc_z.id == id && !tc_glisse;
    uint16_t fond = style == 1 ? BLEU : (style == 2 ? ROUGE : (style == 3 ? BLEU : BTN));
    uint16_t txt = (style == 1 || style == 3) ? SOMBRE : (style == 2 ? TXT : TXT);
    if (appuye) fond = melange(fond, TXT, 90);
    rect_arrondi(x, y, l, h, 5, fond);
    if (style == 0) contour_arrondi(x, y, l, h, 5, 1, appuye ? CYAN : LIGNE);
    else if (appuye) contour_arrondi(x, y, l, h, 5, 1.5f, TXT);
    if (t && *t) texte(x + l / 2, y + (h - hauteur(p)) / 2, t, p, txt, 255, CENTRE);
    zone(x, y, l, h, id, fl);
}

static void etoile(float cx, float cy, float r, uint16_t c, bool pleine)
{
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float a = -1.5708f + i * 0.6283f;
        float rr = (i & 1) ? r * 0.45f : r;
        px[i] = cx + rr * cosf(a);
        py[i] = cy + rr * sinf(a);
    }
    if (pleine) {
        for (int i = 0; i < 10; i++) triangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
    } else {
        for (int i = 0; i < 10; i++) trait(px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], 1.3f, c);
    }
}

static void icone_wifi(int x, int y, uint16_t c)
{
    arc(x, y + 8, 9, 1.6f, 3.93f, 5.50f, c);
    arc(x, y + 8, 6, 1.6f, 3.93f, 5.50f, c);
    arc(x, y + 8, 3, 1.6f, 3.93f, 5.50f, c);
    disque(x, y + 8, 1.3f, c);
}

static void icone_hp(int x, int y, uint16_t c, bool muet)
{
    rect(x, y + 3, 3, 5, c);
    triangle(x + 2, y + 5.5f, x + 7, y, x + 7, y + 11, c);
    if (muet) { trait(x + 9, y + 3, x + 13, y + 8, 1.4f, ROUGE); trait(x + 13, y + 3, x + 9, y + 8, 1.4f, ROUGE); }
    else arc(x + 7, y + 5.5f, 5, 1.4f, -0.9f, 0.9f, c);
}

static void chevron_retour(int x, int y, uint16_t c)
{
    trait(x + 4, y, x, y + 5, 1.8f, c);
    trait(x, y + 5, x + 4, y + 10, 1.8f, c);
}

// "7 074,00" (kHz, 10 Hz pres)
static void fmt_freq(char *s, int n, double khz, int dec = 2)
{
    if (khz < 0) khz = 0;
    long long c = (long long)llround(khz * (dec == 2 ? 100.0 : (dec == 1 ? 10.0 : 1.0)));
    long long div = dec == 2 ? 100 : (dec == 1 ? 10 : 1);
    long long e = c / div, f = c % div;
    char ent[24];
    if (e >= 1000) snprintf(ent, sizeof ent, "%lld %03lld", e / 1000, e % 1000);
    else snprintf(ent, sizeof ent, "%lld", e);
    if (dec == 2) snprintf(s, n, "%s,%02lld", ent, f);
    else if (dec == 1) snprintf(s, n, "%s,%lld", ent, f);
    else snprintf(s, n, "%s", ent);
}

// au-dessus de 30 MHz (OpenWebRX) : en MHz, "145,5000" (100 Hz pres) ou "1 296,000" ; sinon en kHz
static const char *fmt_freq_aff(char *s, int n, double khz)
{
    if (khz < 30000) { fmt_freq(s, n, khz); return "kHz"; }
    // 1 kHz pres (100 Hz si le pas est plus fin) ; un chiffre de moins au-dessus de 1 GHz (place a l'ecran)
    double mhz = khz / 1000.0;
    int dec = cfg::PAS_HZ[cfg::R.pas] < 1000 ? 4 : 3;
    if (mhz >= 1000) dec--;
    long long div = 1;
    for (int i = 0; i < dec; i++) div *= 10;
    long long c = llround(mhz * (double)div);
    snprintf(s, n, "%lld,%0*lld", c / div, dec, c % div);
    return "MHz";
}

// meme chose avec l'unite : "7 074,00 kHz", "145,5000 MHz"
static void fmt_freq_u(char *s, int n, double khz)
{
    char f[24];
    const char *u = fmt_freq_aff(f, sizeof f, khz);
    snprintf(s, n, "%s %s", f, u);
}

static void fmt_hz(char *s, int n, int hz)
{
    if (hz < 1000) snprintf(s, n, "%d Hz", hz);
    else if (hz % 1000 == 0) snprintf(s, n, "%d kHz", hz / 1000);
    else if (hz % 100 == 0) snprintf(s, n, "%d,%d kHz", hz / 1000, (hz % 1000) / 100);
    else snprintf(s, n, "%d,%02d kHz", hz / 1000, (hz % 1000) / 10);
}

struct Bande { float a, b; const char *nom; };
static const Bande BANDES[] = {
    { 0, 148, "VLF/LF" }, { 148, 284, "GO" }, { 526, 1607, "PO" }, { 1810, 2000, "160 m" }, { 2300, 2495, "120 m" },
    { 3200, 3400, "90 m" }, { 3500, 3800, "80 m" }, { 3900, 4000, "75 m" }, { 4750, 5060, "60 m" },
    { 5351, 5367, "60 m" }, { 5900, 6200, "49 m" }, { 7000, 7200, "40 m" }, { 7200, 7450, "41 m" },
    { 9400, 9900, "31 m" }, { 10100, 10150, "30 m" }, { 11600, 12100, "25 m" }, { 13570, 13870, "22 m" },
    { 14000, 14350, "20 m" }, { 15100, 15800, "19 m" }, { 17480, 17900, "16 m" }, { 18068, 18168, "17 m" },
    { 21000, 21450, "15 m" }, { 21450, 21850, "13 m" }, { 24890, 24990, "12 m" }, { 25670, 26100, "11 m" },
    { 26965, 27405, "CB" }, { 28000, 29700, "10 m" } };

static const Bande BANDES_VHF[] = {
    { 50000, 54000, "6 m" }, { 70000, 70500, "4 m" }, { 87500, 108000, "FM" }, { 108000, 137000, "Aviation" },
    { 137000, 138000, "Satellites" }, { 144000, 146000, "2 m" }, { 156000, 162050, "Marine" },
    { 430000, 440000, "70 cm" }, { 446000, 446200, "PMR" }, { 1240000, 1300000, "23 cm" } };

static const char *nom_bande(double f)
{
    if (f >= 30000) {
        if (cfg::R.proto == cfg::OWRX && kw::nom_profil()[0]) return kw::nom_profil();
        for (auto &b : BANDES_VHF) if (f >= b.a && f <= b.b) return b.nom;
        return f < 300000 ? "VHF" : "UHF";
    }
    for (auto &b : BANDES) if (f >= b.a && f <= b.b) return b.nom;
    return "OC";
}

// ============================ Tonalite, mode nuit (v0.14) =====================
struct Options2 { int8_t graves; int8_t aigus; uint8_t nuit; uint8_t sans_cw; };   // nuit : 0 arret, 1 = 1 min, 2 = 5 min, 3 = 15 min ; sans_cw : pas de texte morse sur l'ecran Radio
static Options2 opt2 = { 0, 0, 0, 0 };
// options (v0.16) : AGC du KiwiSDR (seuil, gain fixe sans AGC), NVS "options3"
struct Options3 { int8_t agc_seuil; uint8_t gain_man; uint8_t libre[2]; };
static Options3 opt3 = { -100, 50, { 0, 0 } };
static void opt3_sauver() { plat_nvs_ecrire("options3", &opt3, sizeof opt3); }
static uint32_t t_opt3 = 0;                     // curseur AGC bouge : enregistre 2 s apres
static void opt3_appliquer() { kw::agc_niveaux(opt3.agc_seuil, opt3.gain_man); }
static void opt2_sauver() { plat_nvs_ecrire("options2", &opt2, sizeof opt2); }
static void opt2_appliquer() { aud::tonalite(opt2.graves, opt2.aigus); }
static uint32_t t_geste = 0;            // dernier toucher ou tour de bouton
static bool nuit_actif = false;

// ============================ Memoire par bande ================================
// Pour chaque bande (40 m, 49 m, 2 m...), la derniere frequence ecoutee et son mode. On y revient par les
// raccourcis de bandes du clavier et par les bandes de "Quoi écouter".
struct MemBande { float f; uint8_t mode; uint8_t libre[3]; };
static const int NB_MEM = (int)(sizeof BANDES / sizeof BANDES[0]) + (int)(sizeof BANDES_VHF / sizeof BANDES_VHF[0]);
static MemBande mem_b[NB_MEM];
static bool mem_change = false;
static uint32_t mem_t_change = 0;

static int mem_index(double f)
{
    int n = (int)(sizeof BANDES / sizeof BANDES[0]);
    if (f < 30000) { for (int i = 0; i < n; i++) if (f >= BANDES[i].a && f <= BANDES[i].b) return i; return -1; }
    for (int i = 0; i < (int)(sizeof BANDES_VHF / sizeof BANDES_VHF[0]); i++) if (f >= BANDES_VHF[i].a && f <= BANDES_VHF[i].b) return n + i;
    return -1;
}

// derniere frequence ecoutee dans la bande de f_defaut (si elle est entre a et b) ; sinon f_defaut
static double mem_rappel(double f_defaut, double a, double b, uint8_t *mode)
{
    int i = mem_index(f_defaut);
    if (i < 0 || mem_b[i].f <= 0 || mem_b[i].mode >= cfg::NB_MODES) return f_defaut;
    if (mem_b[i].f < a || mem_b[i].f > b) return f_defaut;
    if (mode) *mode = mem_b[i].mode;
    return mem_b[i].f;
}

// a chaque image : une frequence ecoutee 3 secondes est retenue pour sa bande
static void mem_tic()
{
    static double f_vue = 0;
    static int m_vu = -1;
    static uint32_t t_vue = 0;
    if (kw::etat() != kw::ACTIF) { f_vue = 0; return; }
    if (cfg::R.freq_khz != f_vue || cfg::R.mode != m_vu) { f_vue = cfg::R.freq_khz; m_vu = cfg::R.mode; t_vue = t_ms; }
    else if (t_vue && t_ms - t_vue > 3000) {
        t_vue = 0;
        int i = mem_index(f_vue);
        if (i >= 0 && (mem_b[i].f != (float)f_vue || mem_b[i].mode != m_vu)) {
            mem_b[i].f = (float)f_vue;
            mem_b[i].mode = (uint8_t)m_vu;
            mem_change = true;
            mem_t_change = t_ms;
        }
    }
    if (mem_change && t_ms - mem_t_change > 20000 && !cfg::gele()) { mem_change = false; plat_nvs_ecrire("bandes", mem_b, sizeof mem_b); }   // sans user la flash
}

// ============================ Journal d'ecoute =================================
// Toute frequence ecoutee au moins 30 secondes est notee : heure, frequence, mode, duree, signal le plus fort,
// recepteur, station (si elle est dans l'annuaire). Les 60 dernieres ecoutes restent en memoire (page web) ;
// avec une carte SD, tout est ajoute a /kiwi/journal.csv (s'ouvre dans un tableur).
struct Ecoute { char quand[16]; double f; uint8_t mode; uint16_t duree_s; int16_t rssi; char rx[32]; char quoi[32]; };
static const int MAX_JOURNAL = 60;
static Ecoute *jr = nullptr;
static int jr_n = 0, jr_debut = 0;                  // anneau : jr_n entrees a partir de jr_debut
static Ecoute jr_cours = { "", 0, 0, 0, -127, "", "" };
static uint32_t jr_t0 = 0;
static const char *const JOURNAL_CSV = "/kiwi/journal.csv";
static const uint32_t JOURNAL_MIN_MS = 30000;

static void journal_ecrire(const Ecoute &e)
{
    if (!jr) jr = (Ecoute *)plat_malloc_ps(sizeof(Ecoute) * MAX_JOURNAL);
    if (!jr) return;
    int k = (jr_debut + jr_n) % MAX_JOURNAL;
    if (jr_n == MAX_JOURNAL) jr_debut = (jr_debut + 1) % MAX_JOURNAL; else jr_n++;
    jr[k] = e;
    if (!plat_sd()) return;
    char l[200];
    static bool entete_vue = false;
    if (!entete_vue) {
        entete_vue = true;
        char *t = plat_texte_lire(JOURNAL_CSV, 64);
        if (t) free(t);
        else { const char *h = "date;heure;frequence_khz;mode;duree_s;signal_max_dbm;recepteur;station\r\n"; plat_texte_ajouter(JOURNAL_CSV, h, strlen(h)); }
    }
    // quand = "20261114-113605"
    char date[12] = "", heure[10] = "";
    if (strlen(e.quand) >= 15) { snprintf(date, sizeof date, "%.4s-%.2s-%.2s", e.quand, e.quand + 4, e.quand + 6); snprintf(heure, sizeof heure, "%.2s:%.2s:%.2s", e.quand + 9, e.quand + 11, e.quand + 13); }
    else snprintf(date, sizeof date, "%.11s", e.quand);
    char f[24];
    snprintf(f, sizeof f, "%.2f", e.f);
    for (char *q = f; *q; q++) if (*q == '.') *q = ',';              // virgule : tableur francais
    char rx[32], quoi[32];
    snprintf(rx, sizeof rx, "%s", e.rx); snprintf(quoi, sizeof quoi, "%s", e.quoi);
    for (char *q = rx; *q; q++) if (*q == ';') *q = ',';
    for (char *q = quoi; *q; q++) if (*q == ';') *q = ',';
    int n = snprintf(l, sizeof l, "%s;%s;%s;%s;%u;%d;%s;%s\r\n", date, heure, f, cfg::nom_mode(e.mode), e.duree_s, e.rssi, rx, quoi);
    if (n > 0) plat_texte_ajouter(JOURNAL_CSV, l, (size_t)n < sizeof l ? n : sizeof l - 1);
}

static void journal_clore()
{
    if (jr_cours.f > 0 && jr_t0 && t_ms - jr_t0 >= JOURNAL_MIN_MS) {
        jr_cours.duree_s = (uint16_t)((t_ms - jr_t0) / 1000 > 65535 ? 65535 : (t_ms - jr_t0) / 1000);
        journal_ecrire(jr_cours);
    }
    jr_cours.f = 0;
    jr_t0 = 0;
}

// a chaque image
static void journal_tic()
{
    bool actif = kw::etat() == kw::ACTIF;
    if (!actif) { if (jr_cours.f > 0) journal_clore(); return; }
    if (jr_cours.f != cfg::R.freq_khz || jr_cours.mode != cfg::R.mode || strncmp(jr_cours.rx, kw::nom(), sizeof jr_cours.rx - 1)) {
        journal_clore();
        jr_cours.f = cfg::R.freq_khz;
        jr_cours.mode = cfg::R.mode;
        jr_cours.rssi = -127;
        snprintf(jr_cours.rx, sizeof jr_cours.rx, "%s", kw::nom());
        plat_horodatage(jr_cours.quand, sizeof jr_cours.quand);
        jr_cours.quoi[0] = 0;
        for (int i = 0; i < sta::nb(); i++) {                           // station de l'annuaire a cette frequence
            const sta::Station *st = sta::station(i);
            if (st && fabs(st->khz - cfg::R.freq_khz) < 0.6) { snprintf(jr_cours.quoi, sizeof jr_cours.quoi, "%s", st->nom); break; }
        }
        if (!jr_cours.quoi[0]) snprintf(jr_cours.quoi, sizeof jr_cours.quoi, "%s", nom_bande(cfg::R.freq_khz));
        jr_t0 = t_ms ? t_ms : 1;
    }
    int r = (int)lroundf(kw::rssi_dbm());
    if (r > jr_cours.rssi && r < 0) jr_cours.rssi = (int16_t)r;
}

// ============================ Signal dans le temps =============================
// Le S-metre est note deux fois par seconde pendant 2 minutes : on voit le fading et si la station monte ou descend.
static const int NB_SIG = 240;
static int8_t sig_h[NB_SIG];                        // dBm + 127 (0 = pas de mesure)
static int sig_n = 0, sig_i = 0;
static bool sig_affiche = false;
static void signal_tic()
{
    static uint32_t t = 0;
    static double f_prec = 0;
    if (cfg::R.freq_khz != f_prec) { f_prec = cfg::R.freq_khz; sig_n = 0; sig_i = 0; }      // autre frequence : on repart
    if (t_ms - t < 500) return;
    t = t_ms;
    if (kw::etat() != kw::ACTIF) return;
    float r = kw::rssi_dbm();
    int v = (int)lroundf(r) + 127;
    sig_h[sig_i] = (int8_t)(v < 1 ? 1 : (v > 127 ? 127 : v));
    sig_i = (sig_i + 1) % NB_SIG;
    if (sig_n < NB_SIG) sig_n++;
}

// ============================ Favoris ========================================
struct Favori { double f; uint8_t mode; uint8_t libre; uint16_t bp; uint16_t port; char hote[64]; char nom[48]; char mdp[24]; char chemin[48]; };
struct FavoriV1 { double f; uint8_t mode; uint8_t libre; uint16_t bp; uint16_t port; char hote[64]; char nom[32]; char mdp[24]; };   // jusqu'a la v0.10.2
static const int MAX_FAV = 20;
static Favori favs[MAX_FAV];
static int nfav = 0;

static void fav_sauver()
{
    // pas de copie : les cases inutilisees sont remises a zero puis le tableau est ecrit tel quel
    memset(&favs[nfav], 0, sizeof(Favori) * (MAX_FAV - nfav));
    plat_nvs_ecrire("favoris2", favs, sizeof favs);
}

static void fav_charger()
{
    nfav = 0;
    if (!plat_nvs_lire("favoris2", favs, sizeof favs)) {
        memset(favs, 0, sizeof favs);
        // favoris des versions precedentes : repris une fois (OpenWebRX : le chemin etait range dans le mot de passe)
        FavoriV1 *a = (FavoriV1 *)plat_malloc_ps(sizeof(FavoriV1) * MAX_FAV);
        if (a && plat_nvs_lire("favoris", a, sizeof(FavoriV1) * MAX_FAV)) {
            for (int i = 0; i < MAX_FAV; i++) {
                Favori &f = favs[i];
                f.f = a[i].f; f.mode = a[i].mode; f.libre = a[i].libre; f.bp = a[i].bp; f.port = a[i].port;
                memcpy(f.hote, a[i].hote, 64);
                a[i].nom[31] = 0; a[i].mdp[23] = 0;
                snprintf(f.nom, sizeof f.nom, "%s", a[i].nom);
                if (f.libre & 1) snprintf(f.chemin, sizeof f.chemin, "%s", a[i].mdp);
                else snprintf(f.mdp, sizeof f.mdp, "%s", a[i].mdp);
            }
        }
        if (a) free(a);
    }
    for (int i = 0; i < MAX_FAV; i++)                 // lecture sur place, en tassant les cases vides
        if (favs[i].f > 0) {
            if (nfav != i) favs[nfav] = favs[i];
            favs[nfav].hote[63] = 0; favs[nfav].nom[47] = 0; favs[nfav].mdp[23] = 0; favs[nfav].chemin[47] = 0;
            nfav++;
        }
}

static bool fav_existe()
{
    for (int i = 0; i < nfav; i++)
        if (fabs(favs[i].f - cfg::R.freq_khz) < 0.005 && favs[i].mode == cfg::R.mode && !strcmp(favs[i].hote, cfg::R.hote)) return true;
    return false;
}

static void fav_retirer(int i)
{
    if (i < 0 || i >= nfav) return;
    for (int k = i; k < nfav - 1; k++) favs[k] = favs[k + 1];
    nfav--;
    fav_sauver();
}

static void fav_ajouter();
static void fav_basculer()
{
    for (int i = 0; i < nfav; i++)
        if (fabs(favs[i].f - cfg::R.freq_khz) < 0.005 && favs[i].mode == cfg::R.mode && !strcmp(favs[i].hote, cfg::R.hote)) {
            fav_retirer(i);
            message("Favori retiré");
            return;
        }
    fav_ajouter();
}

static void fav_ajouter()
{
    for (int i = 0; i < nfav; i++)
        if (fabs(favs[i].f - cfg::R.freq_khz) < 0.005 && favs[i].mode == cfg::R.mode && !strcmp(favs[i].hote, cfg::R.hote)) {
            message("Déjà dans les favoris");
            return;
        }
    if (nfav >= MAX_FAV) { message("20 favoris maximum : appui long pour en retirer"); return; }
    Favori &f = favs[nfav++];
    memset(&f, 0, sizeof f);
    f.f = cfg::R.freq_khz;
    f.mode = cfg::R.mode;
    f.bp = cfg::R.bp[cfg::R.mode];
    f.port = cfg::R.port;
    if (f.mode == cfg::WFM) f.bp = 0;                   // largeur FM large : trop grande pour le champ (gardee dans les reglages)
    f.libre = (uint8_t)((cfg::R.proto == cfg::OWRX ? 1 : 0) | (cfg::R.chiffre ? 2 : 0));
    snprintf(f.hote, sizeof f.hote, "%s", cfg::R.hote);
    snprintf(f.nom, sizeof f.nom, "%s", cfg::R.nom);
    snprintf(f.mdp, sizeof f.mdp, "%s", cfg::R.proto == cfg::OWRX ? "" : cfg::R.mdp);
    snprintf(f.chemin, sizeof f.chemin, "%s", cfg::R.proto == cfg::OWRX ? cfg::R.chemin : "");
    fav_sauver();
    char s[32], m[72];
    fmt_freq_u(s, sizeof s, f.f);
    snprintf(m, sizeof m, "Favori ajouté : %s %s", s, cfg::nom_mode(f.mode));
    message(m);
}

// ============================ Preregages (stations qui ne changent pas) =======
struct Preset { const char *nom; double f; int mode; };
static const Preset PRESETS[] = {
    { "WWV · signal horaire (USA)", 5000, cfg::AM }, { "WWV · signal horaire", 10000, cfg::AM },
    { "WWV · signal horaire", 15000, cfg::AM }, { "WWV · signal horaire", 2500, cfg::AM },
    { "MSF · horloge (Royaume-Uni)", 60, cfg::CW }, { "Shannon Volmet · météo aviation", 5505, cfg::USB },
    { "DCF77 · horloge (Allemagne)", 77.5, cfg::CW }, { "DWD · météo marine RTTY", 10100.8, cfg::USB },
    { "DWD · carte météo FAX", 7878.1, cfg::USB }, { "FT8 · radioamateurs 40 m", 7074, cfg::USB },
    { "FT8 · radioamateurs 20 m", 14074, cfg::USB } };
static const int NB_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ============================ Actions radio ==================================
static void accorder(double khz)
{
    float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
    if (khz < bmin) khz = bmin;
    if (khz > bmax) khz = bmax;
    cfg::R.freq_khz = khz;
    cfg::marquer();
    kw::regler();
}

static void changer_mode(int m)
{
    int nb = cfg::R.proto == cfg::OWRX ? cfg::NB_MODES : cfg::WFM;   // pas de FM large sur un KiwiSDR
    cfg::R.mode = (uint8_t)((m % nb + nb) % nb);
    cfg::R.pas = (uint8_t)cfg::pas_par_defaut(cfg::R.mode, cfg::R.freq_khz);       // pas adapte au mode (modifiable)
    cfg::marquer();
    kw::regler();
}

// options (v0.13), enregistrees en NVS ("options")
struct Options { uint8_t squelch; uint8_t sam; uint8_t reprise; uint8_t vox; };   // squelch : 0 arret, 1 bas, 2 moyen, 3 haut
static Options opt = { 0, 0, 1, 0 };
static void opt_sauver() { plat_nvs_ecrire("options", &opt, sizeof opt); }
static void opt_appliquer() { kw::am_synchrone(opt.sam); mot::voix(opt.vox != 0); }
static quoi::Contexte q_ctx = { 0, 0, 0 };      // heure et position du recepteur ecoute ("Quoi écouter", reprise)
// reprise automatique : on ecoutait, la connexion tombe
static bool rp_ecoutait = false, rp_interne = false;
static int rp_essais = 0;
static uint32_t rp_t = 0;

static void connecter(const char *hote, uint16_t port, const char *mdp, const char *nom,
                      int proto = cfg::KIWI, bool chiffre = false, const char *chemin = "/")
{
    if (!rp_interne) { rp_ecoutait = false; rp_essais = 0; }    // choix de l'utilisateur : pas de reprise sur l'ancien
    if (strcmp(cfg::R.hote, hote) || cfg::R.port != port || cfg::R.proto != proto) cfg::R.profil[0] = 0;   // autre recepteur : bande par defaut
    cfg::R.proto = (uint8_t)proto;
    cfg::R.chiffre = chiffre;
    char ch[sizeof cfg::R.chemin];
    snprintf(ch, sizeof ch, "%s", chemin && chemin[0] == '/' ? chemin : "/");
    snprintf(cfg::R.chemin, sizeof cfg::R.chemin, "%s", ch);
    if (proto == cfg::KIWI && cfg::R.freq_khz > 32000) cfg::R.freq_khz = 7074.0;   // retour aux ondes courtes
    if (proto == cfg::KIWI && cfg::R.mode == cfg::WFM) cfg::R.mode = cfg::FM;
    snprintf(cfg::R.hote, sizeof cfg::R.hote, "%s", hote);
    snprintf(cfg::R.nom, sizeof cfg::R.nom, "%s", nom);
    snprintf(cfg::R.mdp, sizeof cfg::R.mdp, "%s", mdp ? mdp : "");
    cfg::R.port = port;
    cfg::marquer();
    fm::deconnecter();                                 // un seul son a la fois : KiwiSDR ou FM
    kw::connecter(hote, port, mdp, nom, proto, chiffre, cfg::R.chemin);
}

static void reconnecter()
{
    char h[64], n[48], m[24], c[48];
    snprintf(h, sizeof h, "%s", cfg::R.hote); snprintf(n, sizeof n, "%s", cfg::R.nom);
    snprintf(m, sizeof m, "%s", cfg::R.mdp); snprintf(c, sizeof c, "%s", cfg::R.chemin);
    connecter(h, cfg::R.port, m, n, cfg::R.proto, cfg::R.chiffre, c);
}

// recepteur OpenWebRX par son adresse ("https://sdr.exemple.org/" ou "hote:8073")
static bool connecter_url(const char *url, const char *nom)
{
    char h[64], base[48], u[128];
    uint16_t p = 0;
    bool tls = false;
    if (!strstr(url, "://")) snprintf(u, sizeof u, "http://%s", url);
    else snprintf(u, sizeof u, "%s", url);
    if (!fml::analyser_url(u, h, sizeof h, &p, &tls, base, sizeof base)) { message("Adresse OpenWebRX invalide"); return false; }
    connecter(h, p, "", nom && nom[0] ? nom : h, cfg::OWRX, tls, base);
    return true;
}

static void aller(int e)
{
    ecran = e;
    dec::actif(e == E_DECODEURS);
}

static void rappeler_preset(int i)
{
    if (i < 0 || i >= NB_PRESETS) return;
    cfg::R.mode = (uint8_t)PRESETS[i].mode;
    cfg::R.pas = (uint8_t)cfg::pas_par_defaut(cfg::R.mode, cfg::R.freq_khz);
    accorder(PRESETS[i].f);
    aller(E_RADIO);
}

// ============================ RADIO ==========================================
static const int Y_SPEC = 70, H_SPEC = 35, Y_WF = 105, H_WF = 89, Y_BAS = 194;
static bool panneau_audio = false;
static const int L_BORD = 30;                   // bande du bord droit : glisser haut/bas = volume
static int glisse_vol0 = 0;
static uint32_t t_vol_bord = 0;                  // dernier geste (affichage de la jauge) ; 0 = jamais
static float cf_aff = -1, span_aff = -1;
static double glisse_f0 = 0;

static float khz_par_px() { return kw::cascade_largeur_khz() / wf::L; }
static float x_de_f(double f) { return wf::L / 2 + (float)((f - kw::cascade_centre_khz()) / khz_par_px()); }
static double f_de_x(float x) { return kw::cascade_centre_khz() + (x - wf::L / 2) * khz_par_px(); }

// ============================ Bande passante sur l'ecran Radio ================
// Trois facons de la regler en ecoutant :
//  - deux doigts sur le waterfall : les ecarter elargit, les rapprocher resserre ;
//  - toucher "BP ..." sous la frequence : curseur et trois valeurs rapides ;
//  - bouton KY-040, 3e mode (appui long : frequence -> volume -> bande passante).
static uint32_t t_bp = 0;               // dernier changement : valeur affichee en grand un instant
static uint32_t bp_popup_jusqua = 0;    // curseur ouvert jusqu'a cette date (0 = ferme)
static bool pince = false, d2_present = false;
static int d2_x = 0, d2_y = 0;
static float pince_d0 = 1;
static uint32_t pince_bp0 = 0;
static int doigts_max = 0;              // 2 des que la dalle a vu deux doigts (temoin dans Menu > WiFi)

static int bp_pas(int m) { return m == cfg::CW ? 50 : (m == cfg::WFM ? 10000 : ((m == cfg::AM || m == cfg::FM) ? 500 : 100)); }
static const int *bp_rapides(int m)
{
    static const int V[cfg::NB_MODES][3] = { { 4000, 6000, 9000 }, { 1800, 2400, 2800 }, { 1800, 2400, 2800 },
                                             { 100, 250, 500 }, { 8000, 12000, 16000 }, { 100000, 150000, 200000 } };
    return V[m < cfg::NB_MODES ? m : 0];
}

static void bp_regler(int hz)
{
    int m = cfg::R.mode, p = bp_pas(m);
    hz = (hz + p / 2) / p * p;
    if (hz < cfg::bp_min(m)) hz = cfg::bp_min(m);
    if (hz > cfg::bp_max(m)) hz = cfg::bp_max(m);
    t_bp = t_ms ? t_ms : 1;
    if ((uint32_t)hz == cfg::R.bp[m]) return;
    cfg::R.bp[m] = (uint32_t)hz;
    cfg::marquer();
    kw::regler();
}

// ---- AM : une bande passante pour les ondes courtes, une autre au-dessus de 30 MHz (aviation : 6 kHz) ----
// cfg::R.bp[AM] est la valeur en service ; les deux valeurs sont gardees a part (NVS "bp_am").
struct BpAm { uint32_t val[2]; uint8_t dom; };          // dom : 0 sous 30 MHz, 1 au-dessus
static BpAm bp_am = { { 9000, 6000 }, 0 };
static bool bp_am_change = false;
static uint32_t bp_am_t = 0;
static const uint32_t BP_AM_VHF_ORIGINE = 6000;

static void bp_am_init()
{
    BpAm m;
    int dom = cfg::R.freq_khz >= 30000 ? 1 : 0;
    if (plat_nvs_lire("bp_am", &m, sizeof m) && m.dom <= 1 && m.val[0] >= 2000 && m.val[1] >= 2000) bp_am = m;
    else { bp_am.val[0] = cfg::R.bp[cfg::AM]; bp_am.val[1] = BP_AM_VHF_ORIGINE; bp_am.dom = 0; }   // premiere fois
    if (bp_am.dom != dom) { bp_am.dom = (uint8_t)dom; cfg::R.bp[cfg::AM] = bp_am.val[dom]; }
}

static void bp_am_tic()
{
    int dom = cfg::R.freq_khz >= 30000 ? 1 : 0;
    if (dom != bp_am.dom) {                             // passage ondes courtes <-> VHF : l'autre valeur
        bp_am.dom = (uint8_t)dom;
        cfg::R.bp[cfg::AM] = bp_am.val[dom];
        cfg::marquer();
        if (cfg::R.mode == cfg::AM) kw::regler();
        bp_am_change = true;
        bp_am_t = t_ms;
    } else if (bp_am.val[dom] != cfg::R.bp[cfg::AM]) {  // reglee par l'utilisateur : retenue pour ce domaine
        bp_am.val[dom] = cfg::R.bp[cfg::AM];
        bp_am_change = true;
        bp_am_t = t_ms;
    }
    if (bp_am_change && t_ms - bp_am_t > 3000) { bp_am_change = false; plat_nvs_ecrire("bp_am", &bp_am, sizeof bp_am); }
}

static int bp_origine_ici(int m) { return m == cfg::AM && cfg::R.freq_khz >= 30000 ? (int)BP_AM_VHF_ORIGINE : cfg::bp_origine(m); }

// valeurs d'origine : du mode en cours, ou de tous les modes
static void bp_decalage(int x)          // curseur de decalage : x de 20 a 300, cran de 50 Hz, aimante au centre
{
    int m = cfg::R.mode, dmax = cfg::decal_max(m);
    if (!dmax) return;
    float t = (x - 20) / 280.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    int d = (int)lroundf((t * 2 - 1) * dmax / 50.0f) * 50;
    if (abs(d) < dmax / 12) d = 0;
    if (d != cfg::decal[m]) { cfg::decal[m] = d; kw::regler(); }
    bp_popup_jusqua = t_ms + 5000;
}

static void bp_origine(bool tous)
{
    for (int k = 0; k < cfg::NB_MODES; k++) if (tous || k == cfg::R.mode) cfg::decal[k] = 0;
    if (tous) {
        for (int m = 0; m < cfg::NB_MODES; m++) cfg::R.bp[m] = (uint32_t)cfg::bp_origine(m);
        bp_am.val[0] = (uint32_t)cfg::bp_origine(cfg::AM);
        bp_am.val[1] = BP_AM_VHF_ORIGINE;
        cfg::R.bp[cfg::AM] = bp_am.val[bp_am.dom];
        bp_am_change = true;
        bp_am_t = t_ms;
        message("Bandes passantes d'origine pour tous les modes");
    } else {
        cfg::R.bp[cfg::R.mode] = (uint32_t)bp_origine_ici(cfg::R.mode);
    }
    cfg::marquer();
    kw::regler();
    t_bp = t_ms ? t_ms : 1;
}

static void bp_curseur(int x)           // curseur du panneau : x de 20 a 300
{
    int m = cfg::R.mode;
    float t = (x - 20) / 280.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    bp_regler(cfg::bp_min(m) + (int)(t * (cfg::bp_max(m) - cfg::bp_min(m))));
    bp_popup_jusqua = t_ms + 5000;
}

static void bp_dessin()
{
    bool popup = bp_popup_jusqua && (int32_t)(bp_popup_jusqua - t_ms) > 0;
    if (!popup) bp_popup_jusqua = 0;
    bool recent = t_bp && t_ms - t_bp < 1500;
    if (!popup && !recent && !pince) return;
    int m = cfg::R.mode;
    char v[16], t[40];
    fmt_hz(v, sizeof v, cfg::R.bp[m]);
    if (!popup) {                                       // pincement ou bouton : valeur en grand
        snprintf(t, sizeof t, "BP %s", v);
        int l = largeur_texte(t, POLICE_22) + 28;
        rect_arrondi(160 - l / 2, Y_WF + 4, l, 32, 8, PAN, 235);
        contour_arrondi(160 - l / 2, Y_WF + 4, l, 32, 8, 1, AMBRE, 220);
        texte(160, Y_WF + 8, t, POLICE_22, AMBRE, 255, CENTRE);
        return;
    }
    int dmax = cfg::decal_max(m);
    const int h = dmax ? 116 : 74, y = Y_BAS - 3 - h;
    rect_arrondi(6, y, 308, h, 8, PAN, 245);
    contour_arrondi(6, y, 308, h, 8, 1, AMBRE, 200);
    zone(6, y, 308, h, B_BP);                           // toucher le panneau ne traverse pas vers le waterfall
    snprintf(t, sizeof t, "Bande passante %s", v);
    texte(160, y + 4, t, POLICE_13, AMBRE, 255, CENTRE);
    float f = (float)(cfg::R.bp[m] - cfg::bp_min(m)) / (float)(cfg::bp_max(m) - cfg::bp_min(m));
    rect_arrondi(20, y + 31, 280, 8, 4, LIGNE);
    rect_arrondi(20, y + 31, (int)(280 * f) + 4, 8, 4, AMBRE);
    disque(20 + 280 * f, y + 35, 9, TXT);
    zone(8, y + 20, 304, 28, B_BP_CURS, Z_GLISSE | Z_APPUI);
    reg_curseur(B_BP_CURS, 20, 280, f, (cfg::bp_max(m) - cfg::bp_min(m)) / 100);
    int yb = y + 50;
    if (dmax) {                                         // decalage du filtre : ecarte un brouilleur voisin
        int d = cfg::decal[m];
        if (d) snprintf(t, sizeof t, "Décalage du filtre %s%d Hz", d > 0 ? "+" : "−", abs(d));
        else snprintf(t, sizeof t, "Décalage du filtre : centré");
        texte(160, y + 48, t, POLICE_11, d ? AMBRE : TXT2, 255, CENTRE);
        float fd = (float)(d + dmax) / (2.0f * dmax);
        rect_arrondi(20, y + 73, 280, 6, 3, LIGNE);
        rect(160, y + 69, 1, 14, TXT2);                 // centre
        int xa = 160, xb = 20 + (int)(280 * fd);
        if (xb < xa) { int k = xa; xa = xb; xb = k; }
        if (xb > xa) rect(xa, y + 73, xb - xa, 6, AMBRE);
        disque(20 + 280 * fd, y + 76, 8, TXT);
        zone(8, y + 62, 304, 28, B_BP_DECAL, Z_GLISSE | Z_APPUI);
        reg_curseur(B_BP_DECAL, 20, 280, fd, 2 * dmax / 50);
        yb = y + 92;
    }
    const int *r = bp_rapides(m);
    for (int k = 0; k < 3; k++) {
        char b[16];
        fmt_hz(b, sizeof b, r[k]);
        bouton(12 + k * 74, yb, 70, 20, b, B_BP_PRE0 + k, (uint32_t)r[k] == cfg::R.bp[m] ? 3 : 0, POLICE_11, Z_APPUI);
    }
    // valeur d'origine du mode, filtre recentre (appui long : tous les modes)
    bouton(12 + 3 * 74, yb, 74, 20, "Origine", B_BP_ORIG, (int)cfg::R.bp[m] == bp_origine_ici(m) && !cfg::decal[m] ? 3 : 0, POLICE_11, Z_LONG);
    if (tc_appui && tc_z.id == B_BP_ORIG) bp_popup_jusqua = t_ms + 5000;
}

// deux doigts sur le waterfall : appele avant toucher()
static bool pince_tic(bool appui, int x, int y)
{
    if (d2_present && appui) doigts_max = 2;
    else if (appui && doigts_max < 1) doigts_max = 1;
    bool zone_ok = ecran == E_RADIO && !panneau_audio;
    if (!pince) {
        if (!(d2_present && appui && zone_ok)) return false;
        if (y < Y_SPEC || y >= Y_BAS || d2_y < Y_SPEC || d2_y >= Y_BAS) return false;
        float d = hypotf((float)(x - d2_x), (float)(y - d2_y));
        if (d < 24) return false;                       // doigts colles : la dalle les confond
        pince = true;
        pince_d0 = d;
        pince_bp0 = cfg::R.bp[cfg::R.mode];
        bp_popup_jusqua = 0;
        return true;
    }
    if (!appui) { pince = false; return true; }         // tous les doigts leves : fin
    if (d2_present) {
        float d = hypotf((float)(x - d2_x), (float)(y - d2_y));
        if (d >= 16) bp_regler((int)lroundf(pince_bp0 * d / pince_d0));
    }
    return true;                                        // un doigt reste : on attend qu'il se leve, sans rien regler
}

// ============================ Autoscan ======================================
// Recherche de la prochaine station audible vers le haut (+1) ou le bas (-1).
// Le waterfall montre deja la bande autour de la frequence : on y cherche le
// prochain pic qui depasse le bruit (seuil selon la sensibilite), on s'y cale,
// on verifie qu'il est toujours la, on ecoute 5 s puis on repart.
// Sans waterfall (canal sans cascade) : pas a pas avec le S-metre.
enum { SC_ARRET = 0, SC_CHERCHE, SC_ATTENTE_WF, SC_VERIFIE, SC_ECOUTE, SC_PAS, SC_GLISSE };
static int sc_sens = 0;                 // +1 / -1 ; 0 = arret
static bool sc_liste = false;           // scan de la liste des canaux VHF (au lieu de la recherche de pics)
static int fs_sens = 0;                 // autoscan FM (serveur FM-DX) : +1 / -1, 0 = arret
static int mol_reglage = 1;             // bouton rotatif KY-040 : 0 arret, 1 normal, 2 sens inverse
static int sc_phase = SC_ARRET;
static uint32_t sc_t = 0, sc_n0 = 0;    // debut de phase, compteur de lignes du waterfall
static float sc_cf0 = 0;                // centre du waterfall au debut de l'attente
static bool sc_sauter = false;          // passer d'abord le signal sur lequel on est
static double sc_cible = 0;
static float sc_bruit = 0;              // mode S-metre : niveau de bruit estime
static uint8_t sc_sensibilite = 1;      // 0 faible, 1 moyen, 2 fort
static const uint32_t SC_ECOUTE_MS = 5000;
static float sc_seuil_db() { static const float S[3] = { 22, 15, 9 }; return S[sc_sensibilite % 3]; }
static const char *const NOMS_SENSIB[3] = { "Faible", "Moyen", "Fort" };
static int sc_vitesse = 1;              // 1..4 : chaque appui sur le meme bouton accelere
static int sc_zoom0 = -1;               // zoom de l'utilisateur, remis a l'arret (KiwiSDR : vue elargie pour aller plus vite)
static uint32_t sc_cpt_bande = 0;       // OpenWebRX : dernier spectre de bande examine
// selon la vitesse : lignes de waterfall attendues et delais (ms)
static int sc_lignes() { static const int V[4] = { 4, 3, 2, 2 }; return V[(sc_vitesse - 1) & 3]; }
static uint32_t sc_attente() { static const uint32_t V[4] = { 400, 300, 200, 150 }; return V[(sc_vitesse - 1) & 3]; }
static uint32_t sc_verif() { static const uint32_t V[4] = { 600, 450, 350, 300 }; return V[(sc_vitesse - 1) & 3]; }

// KiwiSDR : la vue s'elargit d'un cran par vitesse (chaque saut couvre plus de frequences)
static bool sc_affiner = false;         // station trouvee en vue large : recalage au zoom de l'utilisateur
static float sc_kpp_large = 0;

static bool regler_zoom_scan(int z)
{
    if (z < 0) z = 0;
    if (z == cfg::R.zoom) return false;
    cfg::R.zoom = (uint8_t)z;
    kw::regler_cascade();
    return true;
}
static bool scan_zoom()       // true si la vue change (il faut attendre des lignes neuves)
{
    if (cfg::R.proto == cfg::OWRX || sc_zoom0 < 0) return false;
    return regler_zoom_scan(sc_zoom0 - (sc_vitesse - 1));
}

static void scan_sensibilite(int v)
{
    sc_sensibilite = (uint8_t)(v < 0 ? 0 : (v > 2 ? 2 : v));
    uint8_t b = sc_sensibilite + 1;                    // 0 = jamais enregistre
    plat_nvs_ecrire("scan_sens", &b, 1);
}

static void scan_arreter(const char *pourquoi = nullptr)
{
    if (!sc_sens) return;
    sc_sens = 0;
    sc_phase = SC_ARRET;
    sc_liste = false;
    if (sc_zoom0 >= 0 && cfg::R.zoom != sc_zoom0) { cfg::R.zoom = (uint8_t)sc_zoom0; kw::regler_cascade(); }
    sc_zoom0 = -1;
    if (pourquoi) message(pourquoi);
}

static void scan_lancer(int sens)
{
    if (kw::etat() != kw::ACTIF) { message("Connecte-toi d'abord à un récepteur"); return; }
    if (kw::cascade_forcee()) { message("Mesure des bandes en cours"); return; }    // le waterfall regarde ailleurs
    if (sc_sens) scan_arreter();                       // l'autre sens : on repart de zero
    sc_sens = sens > 0 ? 1 : -1;
    sc_phase = kw::sans_cascade() ? SC_PAS : SC_CHERCHE;
    sc_sauter = true;                                  // on part peut-etre d'une station
    sc_t = t_ms;
    sc_bruit = 0;
    sc_vitesse = 1;
    sc_zoom0 = cfg::R.zoom;
    sc_cpt_bande = 0;
}

// bouton ou commande de scan : lance, accelere (vitesses 1 a 4) ou arrete (5e appui)
static void scan_appui(int sens)
{
    int d = sens > 0 ? 1 : -1;
    if (sc_liste) { scan_arreter(); return; }           // scan de la liste : le bouton l'arrete
    if (sc_sens != d) { scan_lancer(d); return; }
    if (++sc_vitesse > 4) { scan_arreter(); return; }
    if (sc_phase == SC_CHERCHE || sc_phase == SC_ATTENTE_WF) {
        if (scan_zoom()) { sc_phase = SC_ATTENTE_WF; sc_t = t_ms; sc_n0 = wf::compteur() ? wf::compteur() : 1; sc_cf0 = kw::cascade_centre_khz(); }
    }
}

// frequence a regler pour un pic vu a f_pic (selon le mode)
static double grille(double f, double pas_khz) { return floor(f / pas_khz + 0.5) * pas_khz; }

static double scan_caler(double f_pic)
{
    if (f_pic >= 30000) {                               // VHF / UHF : grilles de canaux habituelles
        switch (cfg::R.mode) {
        case cfg::WFM: return grille(f_pic, 50);                       // radio FM : 50 kHz (100 kHz le plus souvent)
        case cfg::FM: return grille(f_pic, 12.5);                      // canaux de 12,5 kHz
        case cfg::AM: return grille(f_pic, 25.0 / 3);                  // aviation : 8,33 kHz (multiple de 25 kHz)
        default: break;
        }
    }
    switch (cfg::R.mode) {
    case cfg::USB: return floor((f_pic - 1.0) * 10 + 0.5) / 10;      // la voix est au-dessus de la porteuse
    case cfg::LSB: return floor((f_pic + 1.0) * 10 + 0.5) / 10;
    case cfg::AM: {
        double r5 = floor(f_pic / 5 + 0.5) * 5;
        if (f_pic >= 1800 && fabs(r5 - f_pic) <= 1.5) return r5;    // ondes courtes : grille de 5 kHz
        return floor(f_pic + 0.5);
    }
    default: return floor(f_pic * 100 + 0.5) / 100;
    }
}

// cherche dans le spectre visible ; renvoie true et la frequence du pic trouve
static bool scan_chercher_spectre(double *f_pic)
{
    const float *sp = wf::spectre();
    float seuil = wf::plancher_db() + sc_seuil_db();
    float kpp = khz_par_px();
    if (kpp <= 0) return false;
    float bmin = kw::bande_min_khz() + 2, bmax = kw::bande_max_khz() - 2;
    int x = (int)lroundf(x_de_f(cfg::R.freq_khz));
    int d = sc_sens;
    auto dedans = [&](int xx) {
        if (xx < 2 || xx > wf::L - 3) return false;
        double f = f_de_x((float)xx);
        return f > bmin && f < bmax;
    };
    // a cote de la frequence actuelle (moitie de la bande passante, au moins 2 px)
    int bas_hz, haut_hz;
    cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas_hz, &haut_hz);
    int ecart = (int)ceilf(((haut_hz - bas_hz) / 2000.0f) / kpp);
    if (ecart < 2) ecart = 2;
    if (sc_sauter) {
        x += d * ecart;
        while (dedans(x) && sp[x] > seuil) x += d;     // fin du signal sur lequel on est
    } else {
        x += d;
    }
    while (dedans(x) && sp[x] <= seuil) x += d;        // bruit
    if (!dedans(x)) return false;
    // sommet du signal trouve (au plus 12 kHz plus loin)
    int xm = x, lim = (int)(12 / kpp) + 1;
    for (int k = 0; k < lim && dedans(x) && sp[x] > seuil; k++, x += d)
        if (sp[x] > sp[xm]) xm = x;
    *f_pic = f_de_x((float)xm);
    return true;
}

static float *tri_bande()                // tableau de travail (tri du spectre de bande) en PSRAM
{
    static float *t = nullptr;
    if (!t) t = (float *)plat_malloc_ps(sizeof(float) * kw::NB_BANDE);
    return t;
}

// OpenWebRX : recherche dans le spectre de toute la bande (en boucle d'un bout a l'autre)
static bool scan_chercher_bande(const float *db, int n, double *f_pic)
{
    float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
    double kpb = (bmax - bmin) / n;                     // kHz par point
    if (kpb <= 0) return false;
    float *tri = tri_bande();
    if (!tri) return false;
    memcpy(tri, db, n * sizeof(float));
    std::nth_element(tri, tri + n / 4, tri + n);
    float seuil = tri[n / 4] + sc_seuil_db();
    int marge = n / 40;                                  // bords de la bande (filtre du recepteur) ignores
    auto dedans = [&](int i) { return i >= marge && i < n - marge; };
    int d = sc_sens;
    int i = (int)((cfg::R.freq_khz - bmin) / kpb);
    int bas_hz, haut_hz;
    cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas_hz, &haut_hz);
    int ecart = (int)ceil(((haut_hz - bas_hz) / 2000.0) / kpb);
    if (ecart < 1) ecart = 1;
    int pas_faits = 0;
    auto avancer = [&]() { i += d; pas_faits++; if (!dedans(i)) i = d > 0 ? marge : n - marge - 1; };
    if (sc_sauter) {
        for (int k = 0; k < ecart; k++) avancer();
        while (pas_faits < n && db[i] > seuil) avancer();   // fin du signal sur lequel on est
    } else {
        avancer();
    }
    while (pas_faits < n && db[i] <= seuil) avancer();      // bruit
    if (pas_faits >= n) return false;
    int im = i, lim = (int)(12 / kpb) + 1;                  // sommet du signal
    for (int k = 0; k < lim && dedans(i) && db[i] > seuil; k++, i += d)
        if (db[i] > db[im]) im = i;
    *f_pic = bmin + (im + 0.5) * kpb;
    return true;
}

// ---- scan de la liste des canaux VHF (OpenWebRX) : comme un scanner radio ----
// On passe en revue les canaux de la liste qui sont dans la bande ; dans le spectre de toute la bande,
// un canal ou quelqu'un parle depasse le bruit : on s'y cale et on ecoute tant qu'il parle (+ 3 s).
struct CanalScan { double khz; uint8_t mode; char nom[40]; char type[24]; };
static const int MAX_SCAN_LISTE = 96;
static CanalScan *sl = nullptr;           // [MAX_SCAN_LISTE] en PSRAM
static int sl_n = 0, sl_i = -1;
static uint32_t sl_t_actif = 0;
static const uint32_t SL_MAINTIEN_MS = 3000;

static bool sl_actif(const float *db, int nb, float seuil, double khz, int mode)
{
    float bmin = kw::bande_min_khz(), kpb = (kw::bande_max_khz() - bmin) / nb;
    if (kpb <= 0) return false;
    int i = (int)((khz - bmin) / kpb);
    float largeur = mode == cfg::WFM ? 60 : 3.5f;       // demi-largeur du canal (kHz)
    int r = (int)(largeur / kpb) + 1;
    for (int k = i - r; k <= i + r; k++) if (k >= 0 && k < nb && db[k] > seuil) return true;
    return false;
}

static void liste_tic()
{
    const float *db;
    uint32_t cpt;
    int nb = kw::spectre_bande(&db, &cpt);
    if (nb <= 0 || !db || !sl_n) return;
    if (cpt == sc_cpt_bande) return;                    // un spectre neuf a chaque fois
    sc_cpt_bande = cpt;
    float *tri = tri_bande();
    if (!tri) return;
    memcpy(tri, db, nb * sizeof(float));
    std::nth_element(tri, tri + nb / 4, tri + nb);
    float seuil = tri[nb / 4] + sc_seuil_db();
    if (sc_phase == SC_ECOUTE) {                        // le canal ecoute parle-t-il encore ?
        if (sl_i >= 0 && sl_actif(db, nb, seuil, sl[sl_i].khz, sl[sl_i].mode)) { sl_t_actif = t_ms; return; }
        if (t_ms - sl_t_actif < SL_MAINTIEN_MS) return;
        sc_phase = SC_CHERCHE;
    }
    for (int k = 1; k <= sl_n; k++) {                   // le suivant qui parle, apres le canal ecoute
        int i = ((sl_i < 0 ? -1 : sl_i) + k) % sl_n;
        if (!sl_actif(db, nb, seuil, sl[i].khz, sl[i].mode)) continue;
        sl_i = i;
        cfg::R.mode = sl[i].mode;
        accorder(sl[i].khz);
        sc_phase = SC_ECOUTE;
        sl_t_actif = t_ms;
        return;
    }
}

static bool sl_fav = false;               // la liste scannee est celle des favoris
static void fav_tic();

static void scan_liste_lancer(int cats)
{
    sl_fav = false;
    if (kw::etat() != kw::ACTIF || cfg::R.proto != cfg::OWRX) { message("Scan des canaux : connecte-toi à un récepteur VHF/UHF"); return; }
    if (!sl) sl = (CanalScan *)plat_malloc_ps(sizeof(CanalScan) * MAX_SCAN_LISTE);
    static const vhf::Canal **v = nullptr;
    if (!v) v = (const vhf::Canal **)plat_malloc_ps(sizeof(void *) * 2000);
    if (!sl || !v) return;
    int n = vhf::chercher(cats, "", v, 2000);
    float a = kw::bande_min_khz(), b = kw::bande_max_khz();
    sl_n = 0;
    for (int i = 0; i < n && sl_n < MAX_SCAN_LISTE; i++) {
        if (v[i]->khz < a || v[i]->khz > b) continue;
        bool double_ = false;                           // une frequence partagee par deux aerodromes : une fois
        for (int k = 0; k < sl_n && !double_; k++) double_ = fabs(sl[k].khz - v[i]->khz) < 0.5;
        if (double_) continue;
        CanalScan &c = sl[sl_n++];
        c.khz = v[i]->khz;
        c.mode = v[i]->mode;
        snprintf(c.nom, sizeof c.nom, "%s", v[i]->nom);
        snprintf(c.type, sizeof c.type, "%s", v[i]->type);
    }
    if (!sl_n) { message("Aucun canal de la liste dans cette bande : choisis une autre bande"); return; }
    if (sc_sens) scan_arreter();
    sc_sens = 1;
    sc_liste = true;
    sc_phase = SC_CHERCHE;
    sl_i = -1;
    sc_cpt_bande = 0;
    sc_t = t_ms;
}

// ---- defilement : le scan passe par les frequences intermediaires (affichage et son), comme un vieux scanner ----
static double sg_f0 = 0, sg_f1 = 0;
static uint32_t sg_t = 0, sg_duree = 0;
static int sg_suite = SC_CHERCHE;

static double sg_grille()               // pas des frequences intermediaires (kHz)
{
    double f = cfg::R.freq_khz;
    if (f >= 30000) return cfg::R.mode == cfg::WFM ? 100 : 12.5;
    switch (cfg::R.mode) {
    case cfg::AM: return f < 1800 ? 9 : 5;
    case cfg::CW: return 0.5;
    case cfg::FM: return 5;
    default: return 1;
    }
}

static void sg_arrivee()
{
    accorder(sg_f1);
    sc_phase = sg_suite;
    sc_t = t_ms;
    sc_n0 = wf::compteur();
    sc_cf0 = kw::cascade_centre_khz();
    uint32_t cpt;
    if (kw::spectre_bande(nullptr, &cpt) > 0 && sg_suite == SC_VERIFIE) sc_cpt_bande = cpt;   // spectres neufs apres l'arrivee
}

// va vers "cible" en passant par les frequences intermediaires, puis passe a la phase "suite"
static void glisser(double cible, int suite)
{
    sg_f0 = cfg::R.freq_khz;
    sg_f1 = cible;
    sg_suite = suite;
    double dist = (cible - sg_f0) * sc_sens;                // dans le sens du scan
    bool owrx = kw::spectre_bande(nullptr, nullptr) > 0;
    double ref = owrx ? (kw::bande_max_khz() - kw::bande_min_khz()) * 0.25 : kw::cascade_largeur_khz() * 0.45;
    if (dist <= sg_grille() * 1.5 || ref <= 0 || (!owrx && dist > ref * 1.6)) { sg_arrivee(); return; }   // tout pres, ou bout de bande
    static const uint32_t DMAX[4] = { 1400, 900, 550, 320 };
    double r = dist / ref;
    if (r > 1) r = 1;
    sg_duree = (uint32_t)(DMAX[(sc_vitesse - 1) & 3] * (0.35 + 0.65 * r));
    sg_t = t_ms;
    sc_phase = SC_GLISSE;
}

static void sg_tic()
{
    uint32_t e = t_ms - sg_t;
    if (e >= sg_duree) { sg_arrivee(); return; }
    double g = sg_grille();
    double f = sg_f0 + (sg_f1 - sg_f0) * e / sg_duree;
    f = floor(f / g + 0.5) * g;
    if (fabs(f - cfg::R.freq_khz) >= g * 0.5) accorder(f);
}

static void molette_dessin();
static void signal_dessin();
static void cw_bandeau();
static bool cw_bandeau_visible();
static void mr_dessin();
static void fb_dessin();
static void fb_lancer();
static int mfm_phase();
static void scan_tic()
{
    kw::pause_etiquettes(sc_sens && !sc_liste && sc_phase != SC_ECOUTE);   // pas d'etiquettes demandees pendant la recherche
    if (!sc_sens) return;
    if (sc_phase == SC_GLISSE) { if (kw::etat() != kw::ACTIF) scan_arreter(); else sg_tic(); return; }
    if (sc_liste) {
        if (kw::etat() != kw::ACTIF) { scan_arreter(); return; }
        if (sl_fav && cfg::R.proto != cfg::OWRX) fav_tic(); else liste_tic();
        return;
    }
    if (kw::etat() != kw::ACTIF) { scan_arreter(); return; }
    uint32_t age = t_ms - sc_t;
    switch (sc_phase) {
    case SC_CHERCHE: {
        if (kw::sans_cascade()) { sc_phase = SC_PAS; sc_t = t_ms; break; }
        if (scan_zoom()) {                              // vue elargie selon la vitesse : lignes neuves d'abord
            sc_phase = SC_ATTENTE_WF; sc_t = t_ms; sc_n0 = wf::compteur() ? wf::compteur() : 1; sc_cf0 = kw::cascade_centre_khz();
            break;
        }
        double fp;
        const float *bdb;
        uint32_t cpt;
        int nb = kw::spectre_bande(&bdb, &cpt);
        if (nb > 0 && bdb) {                            // OpenWebRX : toute la bande d'un coup
            if (cpt == sc_cpt_bande) break;             // on attend un spectre neuf
            sc_cpt_bande = cpt;
            if (scan_chercher_bande(bdb, nb, &fp)) {
                sc_cible = scan_caler(fp);
                sc_t = t_ms;
                sc_n0 = wf::compteur();
                glisser(sc_cible, SC_VERIFIE);
            }
            break;
        }
        if (scan_chercher_spectre(&fp)) {
            sc_cible = scan_caler(fp);
            sc_affiner = false;
            if (sc_zoom0 >= 0 && cfg::R.zoom != sc_zoom0) {   // vue large : on revient au zoom de l'utilisateur pour viser juste
                sc_kpp_large = khz_par_px();
                regler_zoom_scan(sc_zoom0);
                sc_affiner = true;
            }
            sc_t = t_ms;
            sc_n0 = wf::compteur();
            sc_cf0 = kw::cascade_centre_khz();
            glisser(sc_cible, SC_VERIFIE);
            break;
        } else {
            // rien dans la partie visible : on avance de presque une demi-largeur
            float sp = kw::cascade_largeur_khz();
            double f = cfg::R.freq_khz + sc_sens * sp * 0.45;
            float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
            if (f > bmax - 1) f = bmin + 1;                // bout de bande : on repart de l'autre cote
            else if (f < bmin + 1) f = bmax - 1;
            sc_sauter = false;
            sc_t = t_ms;
            sc_n0 = wf::compteur();
            sc_cf0 = kw::cascade_centre_khz();
            glisser(f, SC_ATTENTE_WF);
        }
        break;
    }
    case SC_ATTENTE_WF: {
        // le waterfall doit s'etre deplace, puis recu quelques lignes neuves
        static uint32_t n_depl = 0;
        if (fabsf(kw::cascade_centre_khz() - sc_cf0) > 1e-3f) { sc_cf0 = kw::cascade_centre_khz(); n_depl = wf::compteur(); sc_n0 = 0; }
        uint32_t ref = sc_n0 ? sc_n0 : n_depl;
        if ((wf::compteur() - ref >= (uint32_t)sc_lignes() + 1 && age > sc_attente()) || age > 6000) { sc_phase = SC_CHERCHE; sc_t = t_ms; }
        break;
    }
    case SC_VERIFIE: {
        if (kw::spectre_bande(nullptr, nullptr) > 0) {   // OpenWebRX : verification dans le spectre de bande
            const float *bdb;
            uint32_t cpt;
            int nb = kw::spectre_bande(&bdb, &cpt);
            if (cpt - sc_cpt_bande < 2 || age < sc_verif()) break;
            float bmin = kw::bande_min_khz(), kpb = (kw::bande_max_khz() - bmin) / nb;
            float *tri = tri_bande();
            if (!tri) break;
            memcpy(tri, bdb, nb * sizeof(float));
            std::nth_element(tri, tri + nb / 4, tri + nb);
            float seuil = tri[nb / 4] + sc_seuil_db();
            double fv = cfg::R.mode == cfg::USB ? sc_cible + 1.0 : (cfg::R.mode == cfg::LSB ? sc_cible - 1.0 : sc_cible);
            int i = (int)((fv - bmin) / kpb), r = 2 + (int)(1.5f / kpb);
            bool ok = false;
            for (int k = i - r; k <= i + r; k++) if (k >= 0 && k < nb && bdb[k] > seuil) ok = true;
            sc_cpt_bande = cpt;
            sc_sauter = true;
            sc_phase = ok ? SC_ECOUTE : SC_CHERCHE;
            sc_t = t_ms;
            break;
        }
        uint32_t lignes = sc_affiner ? 5 : (uint32_t)(sc_lignes() > 2 ? 3 : 2);
        if (wf::compteur() - sc_n0 < lignes || age < (sc_affiner ? 900 : sc_verif())) break;
        // toujours au-dessus du bruit autour de la cible ?
        const float *sp = wf::spectre();
        float seuil = wf::plancher_db() + sc_seuil_db();
        double fv = cfg::R.mode == cfg::USB ? sc_cible + 1.0 : (cfg::R.mode == cfg::LSB ? sc_cible - 1.0 : sc_cible);
        int x = (int)lroundf(x_de_f(fv));
        int r = 2 + (int)((sc_affiner ? sc_kpp_large * 1.5f : 1.5f) / khz_par_px());
        bool ok = false;
        int xm = -1;
        for (int k = x - r; k <= x + r; k++)
            if (k >= 0 && k < wf::L && sp[k] > seuil) { ok = true; if (xm < 0 || sp[k] > sp[xm]) xm = k; }
        if (ok && sc_affiner && xm >= 0) {             // recalage sur le sommet vu avec plus de detail
            double fp = f_de_x((float)xm);
            sc_cible = scan_caler(fp);
            accorder(sc_cible);
        }
        sc_affiner = false;
        sc_sauter = true;
        sc_phase = ok ? SC_ECOUTE : SC_CHERCHE;
        sc_t = t_ms;
        break;
    }
    case SC_ECOUTE:
        if (age >= SC_ECOUTE_MS) { sc_phase = kw::sans_cascade() ? SC_PAS : SC_CHERCHE; sc_sauter = true; sc_t = t_ms; }
        break;
    case SC_PAS: {
        // sans waterfall : un pas, puis lecture du S-metre 400 ms plus tard
        if (!kw::sans_cascade()) { sc_phase = SC_CHERCHE; sc_t = t_ms; break; }
        if (age < 400) break;
        float r = kw::rssi_dbm();
        if (sc_bruit == 0) sc_bruit = r;
        else if (r < sc_bruit) sc_bruit += (r - sc_bruit) * 0.5f;
        else sc_bruit += (r - sc_bruit) * 0.05f;
        if (!sc_sauter && r > sc_bruit + sc_seuil_db()) { sc_phase = SC_ECOUTE; sc_t = t_ms; break; }
        sc_sauter = false;
        double p = cfg::R.mode == cfg::AM ? (cfg::R.freq_khz < 1800 ? 9 : 5) : (cfg::R.mode == cfg::CW ? 0.5 : 2);
        double f = cfg::R.freq_khz + sc_sens * p;
        float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
        if (f > bmax - 1) f = bmin + 1;
        else if (f < bmin + 1) f = bmax - 1;
        accorder(f);
        sc_t = t_ms;
        break;
    }
    default: break;
    }
}

// bandeau sur le waterfall pendant le scan
static void scan_dessin()
{
    if (!sc_sens) return;
    if (sc_liste) {
        bool ec = sc_phase == SC_ECOUTE && sl_i >= 0;
        char t[80], l2[80];
        if (ec) {
            snprintf(t, sizeof t, "%s", tronquer(sl[sl_i].nom, POLICE_13, 270));
            if (sl_fav && cfg::R.proto != cfg::OWRX) snprintf(l2, sizeof l2, "favori · 10 s au plus · touche l'écran pour rester");
            else snprintf(l2, sizeof l2, "%s · touche l'écran pour arrêter", sl[sl_i].type);
        } else {
            int pts = (t_ms / 350) % 4;
            snprintf(t, sizeof t, sl_fav ? "Scan de %d favoris%.*s" : "Scan de %d canaux%.*s", sl_n, pts, "...");
            snprintf(l2, sizeof l2, sl_fav ? "Arrêt sur le favori qui parle · touche l'écran pour arrêter" : "Arrêt sur le canal qui parle · touche l'écran pour arrêter");
        }
        int l = largeur_texte(t, POLICE_13) + 24;
        int l2l = largeur_texte(l2, POLICE_11) + 24;
        if (l < l2l) l = l2l;
        if (l > 316) l = 316;
        int x = 160 - l / 2, y = Y_WF + 30;
        uint16_t c = ec ? VERT : AMBRE;
        rect_arrondi(x, y, l, 40, 8, PAN, 225);
        contour_arrondi(x, y, l, 40, 8, 1, c, 220);
        texte(160, y + 4, t, POLICE_13, c, 255, CENTRE);
        texte(160, y + 22, tronquer(l2, POLICE_11, l - 8), POLICE_11, TXT2, 255, CENTRE);
        return;
    }
    char t[64];
    const char *fl = sc_sens > 0 ? "▲" : "▼";
    if (sc_phase == SC_ECOUTE) {
        int reste = (int)((SC_ECOUTE_MS - (t_ms - sc_t) + 999) / 1000);
        if (reste < 1) reste = 1;
        snprintf(t, sizeof t, "Station trouvée · reprise dans %d s", reste);
    } else {
        int pts = (t_ms / 350) % 4;
        snprintf(t, sizeof t, "Recherche %s · vitesse %d/4%.*s", fl, sc_vitesse, pts, "...");
    }
    int l = largeur_texte(t, POLICE_13) + 24;
    if (l < 250) l = 250;
    int x = 160 - l / 2, y = Y_WF + 30;
    rect_arrondi(x, y, l, 40, 8, PAN, 225);
    contour_arrondi(x, y, l, 40, 8, 1, sc_phase == SC_ECOUTE ? VERT : AMBRE, 220);
    texte(160, y + 4, t, POLICE_13, sc_phase == SC_ECOUTE ? VERT : AMBRE, 255, CENTRE);
    char l2[64];
    snprintf(l2, sizeof l2, "%s", sc_vitesse < 4 && sc_phase != SC_ECOUTE ? "Même bouton : plus vite · ailleurs : arrêt"
                                                                          : "Touche l'écran pour arrêter");
    texte(160, y + 22, l2, POLICE_11, TXT2, 255, CENTRE);
}

// boutons de recherche (double triangle, facon autoradio)
static void bouton_scan(int x, int y, int sens, int id)
{
    bool fmid = id == B_FMSCAN_BAS || id == B_FMSCAN_HAUT;
    bool actif = (fmid ? fs_sens : sc_sens) == sens;
    bouton(x, y, 28, 20, "", id, actif ? 3 : 0, POLICE_16, Z_APPUI);
    uint16_t c = actif ? SOMBRE : TXT;
    float cx = x + 14, cy = y + 10;
    for (int k = 0; k < 2; k++) {
        float ox = cx + (sens > 0 ? (k ? 0 : -6) : (k ? 0 : 6));
        triangle(ox, cy - 5, ox, cy + 5, ox + 6 * sens, cy, c);
    }
}

static double arrondi_pas(double f, int pas_hz)
{
    double p = pas_hz / 1000.0;
    return floor(f / p + 0.5) * p;
}

// le waterfall suit les changements de centre / de zoom envoyes au KiwiSDR
static void suivre_cascade()
{
    float cf = kw::cascade_centre_khz(), sp = kw::cascade_largeur_khz();
    if (span_aff < 0 || fabsf(sp - span_aff) > 1e-4f) {
        if (span_aff >= 0) wf::effacer();
    } else if (fabsf(cf - cf_aff) > 1e-5f) {
        int px = (int)lroundf((cf_aff - cf) / (sp / wf::L));
        if (px) wf::decaler(px);
        else return;                                   // decalage < 1 pixel : on attend
    }
    cf_aff = cf;
    span_aff = sp;
}

// ============================ Signal present ? (squelch, scan des favoris, enregistrement a la voix) ==========
// On regarde le waterfall a l'endroit du filtre : un signal y depasse le bruit de fond (25e centile de la vue).
// connu = false quand on ne peut pas savoir (pas de waterfall, vue occupee par une mesure) : on laisse alors passer le son.
static bool signal_ici(float seuil_db, bool *connu)
{
    *connu = false;
    if (kw::etat() != kw::ACTIF || kw::sans_cascade() || kw::cascade_forcee() || wf::compteur() == 0) return true;
    float kpp = khz_par_px();
    if (kpp <= 0) return true;
    int bas, haut;
    cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas, &haut);
    double f0 = kw::cascade_centre_khz() - kw::cascade_largeur_khz() / 2;
    double a = cfg::R.freq_khz + bas / 1000.0, b = cfg::R.freq_khz + haut / 1000.0;
    if (b - a < 3 * kpp) { double m = (a + b) / 2; a = m - 1.5 * kpp; b = m + 1.5 * kpp; }   // au moins 3 points
    int n = quoi::compter(wf::spectre(), wf::L, f0, kpp, a, b, seuil_db, 0, nullptr, nullptr);
    if (n < 0) return true;
    *connu = true;
    return n > 0;
}

static const uint32_t FAV_ECOUTE_MS = 10000;
// ---- scan des favoris sur un KiwiSDR : on va sur chaque favori, on regarde s'il y a un signal, on ecoute tant qu'il dure ----
static void fav_tic()
{
    uint32_t age = t_ms - sc_t;
    bool connu;
    switch (sc_phase) {
    case SC_CHERCHE:
        sl_i = (sl_i + 1) % sl_n;
        cfg::R.mode = sl[sl_i].mode;
        cfg::R.pas = (uint8_t)cfg::pas_par_defaut(cfg::R.mode, sl[sl_i].khz);
        accorder(sl[sl_i].khz);
        sc_phase = SC_ATTENTE_WF;
        sc_t = t_ms;
        break;
    case SC_ATTENTE_WF:                                 // le temps que le waterfall arrive sur la nouvelle frequence
        if (age < 450) break;
        wf::effacer();
        span_aff = -1;
        sc_n0 = wf::compteur();
        sc_phase = SC_VERIFIE;
        sc_t = t_ms;
        break;
    case SC_VERIFIE: {
        bool lignes = wf::compteur() - sc_n0 >= 3;
        if (!lignes && age < 2500) break;
        bool s = lignes && signal_ici(sc_seuil_db(), &connu) && connu;
        if (s) { sc_phase = SC_ECOUTE; sl_t_actif = t_ms; sc_t = t_ms; }
        else sc_phase = SC_CHERCHE;
        break;
    }
    case SC_ECOUTE:                                     // tant que ca parle, 10 s au plus (une station en continu ne bloque pas le scan)
        if (signal_ici(sc_seuil_db(), &connu)) sl_t_actif = t_ms;
        if (t_ms - sl_t_actif > SL_MAINTIEN_MS || age > FAV_ECOUTE_MS) sc_phase = SC_CHERCHE;
        break;
    default: sc_phase = SC_CHERCHE; break;
    }
}

static void scan_favoris_lancer()
{
    if (kw::etat() != kw::ACTIF) { message("Connecte-toi d'abord à un récepteur"); return; }
    if (kw::cascade_forcee()) { message("Mesure des bandes en cours"); return; }
    if (!sl) sl = (CanalScan *)plat_malloc_ps(sizeof(CanalScan) * MAX_SCAN_LISTE);
    if (!sl) return;
    bool owrx = cfg::R.proto == cfg::OWRX;
    float a = kw::bande_min_khz(), b = kw::bande_max_khz();
    sl_n = 0;
    for (int i = 0; i < nfav && sl_n < MAX_SCAN_LISTE; i++) {
        if (favs[i].f < a || favs[i].f > b) continue;
        if (favs[i].mode == cfg::WFM && !owrx) continue;
        bool double_ = false;
        for (int k = 0; k < sl_n && !double_; k++) double_ = fabs(sl[k].khz - favs[i].f) < 0.05;
        if (double_) continue;
        CanalScan &c = sl[sl_n++];
        c.khz = favs[i].f;
        c.mode = favs[i].mode;
        char f[32];
        fmt_freq_u(f, sizeof f, favs[i].f);
        snprintf(c.nom, sizeof c.nom, "%s %s", f, cfg::nom_mode(favs[i].mode));
        snprintf(c.type, sizeof c.type, "favori");
    }
    if (sl_n < 2) {
        message(sl_n ? "Un seul favori ici : rien à scanner" : (owrx ? "Aucun favori dans cette bande" : "Aucun favori pour ce récepteur"));
        sl_n = 0;
        return;
    }
    if (sc_sens) scan_arreter();
    sc_sens = 1;
    sc_liste = true;
    sl_fav = true;
    sc_phase = SC_CHERCHE;
    sl_i = -1;
    sc_cpt_bande = 0;
    sc_t = t_ms;
}

// ---- squelch : le haut-parleur se tait quand il n'y a personne ; sert aussi a l'enregistrement a la voix ----
static bool sq_present = true, sq_muet = false;
static uint32_t sq_t_signal = 0;
static const uint32_t SQ_MAINTIEN_MS = 2000;            // reste ouvert pendant les pauses entre deux phrases

static void squelch_tic()
{
    static const float SEUIL[4] = { 12, 8, 12, 18 };    // arret (detection pour l'enregistrement), bas, moyen, haut
    static double f_prec = 0;
    static int mode_prec = -1;
    static uint32_t t_chg = 0, n_prec = 0, t_ligne = 0;
    if (cfg::R.freq_khz != f_prec || cfg::R.mode != mode_prec) { f_prec = cfg::R.freq_khz; mode_prec = cfg::R.mode; t_chg = t_ms; }
    if (wf::compteur() != n_prec) { n_prec = wf::compteur(); t_ligne = t_ms; }
    bool connu = false;
    bool s = signal_ici(SEUIL[opt.squelch & 3], &connu);
    if (t_ms - t_ligne > 3000) connu = false;           // waterfall fige : on ne sait plus
    if (t_ms - t_chg < 1500) connu = false;             // on vient de changer de frequence : le spectre n'a pas suivi
    if (!connu || s) sq_t_signal = t_ms;
    sq_present = t_ms - sq_t_signal < SQ_MAINTIEN_MS;
    bool kiwi_actif = kw::etat() == kw::ACTIF && !fm::actif();
    bool muet = kiwi_actif && opt.squelch && !sq_present;
    if (kiwi_actif && sc_sens && sc_liste && sl_fav && cfg::R.proto != cfg::OWRX && sc_phase != SC_ECOUTE) muet = true;   // scan des favoris : silence entre deux
    sq_muet = muet;
    aud::silence(muet);
    char q[40], f[24];
    fmt_freq_u(f, sizeof f, cfg::R.freq_khz);
    snprintf(q, sizeof q, "%s %s", f, cfg::nom_mode(cfg::R.mode));
    mot::signal_present(kiwi_actif ? sq_present : true, q);
}

// ---- reprise automatique : la connexion tombe pendant l'ecoute -> on se reconnecte, sinon un autre KiwiSDR proche ----
static void reprise_tic()
{
    kw::Etat e = kw::etat();
    if (e == kw::ACTIF) { rp_ecoutait = true; rp_essais = 0; rp_t = 0; return; }
    if (!opt.reprise || !rp_ecoutait) return;
    if (e == kw::ARRET) { rp_ecoutait = false; return; }                // arret demande
    if (e != kw::PERDU && e != kw::OCCUPE && e != kw::REFUS && e != kw::PANNE) { rp_t = 0; return; }   // connexion en cours
    if (!rp_t) { rp_t = t_ms ? t_ms : 1; return; }
    if (t_ms - rp_t < 4000) return;                                     // le temps de lire le message
    rp_t = 0;
    rp_essais++;
    rp_interne = true;
    plat_journal("REPRISE", "apres", kw::message());   // moniteur serie : pourquoi la carte se reconnecte
    if (e == kw::PERDU && rp_essais <= 2) {
        message("Reprise : nouvelle connexion…");
        reconnecter();
    } else if (cfg::R.proto == cfg::KIWI && rp_essais <= 5) {
        uint16_t km = 0;
        float la = q_ctx.lat, lo = q_ctx.lon;
        if (la == 0 && lo == 0) { la = cfg::R.lat; lo = cfg::R.lon; }
        const srv::Serveur *r = srv::plus_proche_libre(la, lo, &km, cfg::R.hote);
        if (r) {
            char m[96];
            snprintf(m, sizeof m, "Reprise sur %s", r->nom);
            message(m);
            connecter(r->hote, r->port, r->mdp, r->nom);
        } else { rp_ecoutait = false; message("Reprise impossible : aucun récepteur libre"); }
    } else {
        rp_ecoutait = false;
        message("Reprise impossible : choisis un récepteur");
    }
    rp_interne = false;
}

static void barre_haut()
{
    rect(0, 0, 320, 22, PAN);
    rect(0, 22, 320, 1, LIGNE);
    kw::Etat e = kw::etat();
    uint16_t c = e == kw::ACTIF ? VERT : ((e == kw::CONNEXION || e == kw::ATTENTE) ? AMBRE : (e == kw::ARRET ? TXT2 : ROUGE));
    if ((e == kw::CONNEXION || e == kw::ATTENTE) && (t_ms / 300) % 2) c = melange(c, PAN, 140);
    disque(9, 11, 3.5f, c);
    int x = 314;
    // volume
    char v[8];
    snprintf(v, sizeof v, "%d", cfg::R.volume);
    int lv = texte(x, 5, v, POLICE_11, TXT, 255, DROITE);
    icone_hp(x - lv - 17, 5, TXT, cfg::R.volume == 0);
    zone(x - lv - 20, 0, lv + 26, 22, B_VOLUME);
    x -= lv + 26;
    // wifi
    int w = plat_wifi_etat();
    icone_wifi(x - 8, 3, w == WIFI_OK ? TXT : (w == WIFI_POINT_ACCES ? AMBRE : ROUGE));
    x -= 24;
    // temoins des traitements du KiwiSDR
    {
        const char *t[5];
        uint16_t c[5];
        int n = 0;
        bool kiwi = cfg::R.proto == cfg::KIWI;          // OpenWebRX : ces traitements n'existent pas
        if (opt.squelch) { t[n] = "SQ"; c[n++] = sq_muet ? AMBRE : VERT; }     // ambre : le squelch coupe le son
        if (kiwi && opt.sam && cfg::R.mode == cfg::AM) { static const char *SY[4] = { "", "SYNC", "SYNC+", "SYNC-" }; t[n] = SY[opt.sam & 3]; c[n++] = CYAN; }
        if ((kiwi && (cfg::R.nr || cfg::R.notch)) || (cfg::R.proto == cfg::OWRX && cfg::R.nr && cfg::R.mode != cfg::WFM)) { t[n] = "NR"; c[n++] = VERT; }
        if (kiwi && cfg::R.nb) { t[n] = "NB"; c[n++] = VERT; }
        if (kiwi && cfg::R.agc) { t[n] = "AGC"; c[n++] = CYAN; }
        for (int i = n - 1; i >= 0; i--) {
            int l = largeur_texte(t[i], POLICE_11) + 8;
            rect_arrondi(x - l, 5, l, 13, 3, melange(PAN, c[i], 60));
            texte(x - l / 2, 5, t[i], POLICE_11, c[i], 255, CENTRE);
            x -= l + 4;
        }
    }
    // reserve audio / enregistrement
    if (mot::enregistre()) {
        rect_arrondi(x - 30, 5, 30, 13, 3, ROUGE);
        texte(x - 15, 5, "REC", POLICE_11, TXT, 255, CENTRE);
        x -= 36;
    }
    // nom du recepteur dans la place qui reste (defile s'il est trop long)
    const char *n = cfg::R.hote[0] ? (cfg::R.nom[0] ? cfg::R.nom : cfg::R.hote) : "Aucun récepteur";
    texte_defile(DEF_RECEPTEUR, 17, 4, x - 4 - 17, n, POLICE_13, TXT, PAN);
    zone(0, 0, x, 22, B_SERVEUR);
}

// rapport signal/bruit : le plus fort du filtre d'ecoute, compare au bruit de fond de l'ecran (5e des 20 % les plus bas)
static float sb_db = -1;                         // < 0 : inconnu
static uint32_t sb_t = 0, sb_n0 = 0;
static void sb_tic()
{
    if (t_ms - sb_t < 250) return;
    sb_t = t_ms;
    if (kw::etat() != kw::ACTIF || wf::compteur() == sb_n0 || kw::cascade_largeur_khz() <= 0 || kw::sans_cascade()) {
        if (kw::etat() != kw::ACTIF || kw::sans_cascade()) sb_db = -1;
        return;
    }
    sb_n0 = wf::compteur();
    const float *sp = wf::spectre();
    int bas, haut;
    cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas, &haut);
    int x0 = (int)floorf(x_de_f(cfg::R.freq_khz + bas / 1000.0)), x1 = (int)ceilf(x_de_f(cfg::R.freq_khz + haut / 1000.0));
    if (x1 < x0 + 1) x1 = x0 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > wf::L - 1) x1 = wf::L - 1;
    if (x0 > x1) return;
    float pic = -999;
    for (int x = x0; x <= x1; x++) if (sp[x] > pic) pic = sp[x];
    // bruit : valeur au 20e centile des points hors du filtre (histogramme 0,5 dB)
    static uint16_t hist[400];
    memset(hist, 0, sizeof hist);
    int n = 0; float mn = 1e9f;
    for (int x = 0; x < wf::L; x++) if (x < x0 - 2 || x > x1 + 2) { if (sp[x] < mn) mn = sp[x]; }
    for (int x = 0; mn < 1e8f && x < wf::L; x++) if (x < x0 - 2 || x > x1 + 2) { int k = (int)((sp[x] - mn) * 2); if (k > 399) k = 399; if (k < 0) k = 0; hist[k]++; n++; }
    // zoom serre : le signal peut remplir l'ecran ; on garde le bruit le plus bas vu recemment sur cette frequence
    // (il remonte de 0,2 dB/s au plus, jusqu'a la mesure du moment)
    static float b_suivi = 0;
    static double f_suivi = 0;
    static uint32_t gen_suivi = 0xFFFFFFFF;
    bool meme = gen_suivi == kw::generation() && fabs(cfg::R.freq_khz - f_suivi) <= 50 && b_suivi != 0;
    float bruit;
    if (n >= 20) {
        int cible = n / 5, c = 0, k = 0;
        for (; k < 400; k++) { c += hist[k]; if (c >= cible) break; }
        bruit = mn + k * 0.5f;
        if (!meme || bruit < b_suivi) b_suivi = bruit;
        else if (b_suivi + 0.05f < bruit) b_suivi += 0.05f;
        else b_suivi = bruit;
    } else if (!meme) { sb_db = -1; return; }           // le filtre couvre tout l'ecran et aucune reference : inconnu
    gen_suivi = kw::generation();
    f_suivi = cfg::R.freq_khz;
    bruit = b_suivi;
    float v = pic - bruit;
    if (v < 0) v = 0;
    sb_db = sb_db < 0 ? v : sb_db + (v - sb_db) * 0.35f;
}

int signal_bruit() { return sb_db < 0 ? -1 : (int)lroundf(sb_db); }
static int ft8_bande_proche();
int ft8_bande_web() { return ft8_bande_proche(); }

static void ligne_frequence()
{
    char s[24];
    const char *unite = fmt_freq_aff(s, sizeof s, cfg::R.freq_khz);
    // pendant la demande de connexion, la frequence clignote ; elle reprend sa couleur une fois connecte
    kw::Etat ek = kw::etat();
    uint16_t cf = AMBRE;
    if ((ek == kw::CONNEXION || ek == kw::ATTENTE) && (t_ms / 300) % 2) cf = melange(AMBRE, FOND, 150);
    int l = texte(6, 11, s, POLICE_40, cf);
    texte(6 + l + 3, 34, unite, POLICE_13, TXT2);
    // toucher = pave ; glisser = reglage fin (1 pas tous les 10 pixels) ; appui long = favori
    zone(0, 23, 6 + l + 28, 30, B_FREQ, Z_LONG | Z_GLISSE);

    // bouton du pas + bande et filtre
    char p[16], b[16], t[48];
    fmt_hz(p, sizeof p, cfg::PAS_HZ[cfg::R.pas]);
    snprintf(t, sizeof t, "Pas %s", p);
    bouton(5, 52, 86, 17, t, B_PAS, 0, POLICE_11, Z_APPUI);
    fmt_hz(b, sizeof b, cfg::R.bp[cfg::R.mode]);
    if (cfg::decal[cfg::R.mode]) snprintf(t, sizeof t, "BP %s %s%d", b, cfg::decal[cfg::R.mode] > 0 ? "+" : "−", abs(cfg::decal[cfg::R.mode]));   // filtre decale
    else snprintf(t, sizeof t, "%s · BP %s", nom_bande(cfg::R.freq_khz), b);
    int xm0 = 6 + l + 36;
    texte(97, 54, tronquer(t, POLICE_11, (xm0 > 206 ? 206 : xm0) - 101), POLICE_11, bp_popup_jusqua || cfg::decal[cfg::R.mode] ? AMBRE : TXT2);
    zone(93, 50, (xm0 > 206 ? 206 : xm0) - 93, 20, B_BP);   // toucher : curseur de bande passante

    // puce du mode + etoile des favoris
    int xm = 6 + l + 36;
    if (xm > 206) xm = 206;
    bool ap = tc_appui && tc_z.id == B_MODE;
    rect_arrondi(xm, 25, 42, 18, 4, ap ? melange(PUCE, TXT, 60) : PUCE);
    texte(xm + 21, 26, cfg::nom_mode(cfg::R.mode), POLICE_13, PUCE_T, 255, CENTRE);
    zone(xm - 2, 23, 46, 22, B_MODE, Z_APPUI);
    bool deja = fav_existe();
    bouton(xm, 46, 42, 22, "", B_FAV, 0, POLICE_11, Z_APPUI);
    etoile(xm + 21, 57, 8, AMBRE, deja);

    // S-metre (le toucher : graphique du signal sur 2 minutes)
    zone(250, 20, 70, 30, B_SMETRE);
    float r = kw::rssi_dbm();
    bool actif = kw::etat() == kw::ACTIF;
    int sv = (int)((r + 127) / 6);
    if (sv < 0) sv = 0;
    if (sv > 9) sv = 9;
    int plus = r > -73 ? (int)((r + 73) / 10) : 0;
    if (plus > 3) plus = 3;
    char sm[16];
    if (!actif) snprintf(sm, sizeof sm, "S–");
    else if (plus > 0 || r > -73) snprintf(sm, sizeof sm, "S9+%d", (int)(r + 73));
    else snprintf(sm, sizeof sm, "S%d", sv);
    texte(314, 23, sm, POLICE_16, TXT, 255, DROITE);
    int allumes = actif ? sv + plus : 0;
    for (int i = 0; i < 12; i++) {
        uint16_t c = i < allumes ? (i < 7 ? VERT : (i < 9 ? JAUNE : ROUGE)) : LIGNE;
        rect(254 + i * 5, 44, 4, 10, c);
    }
    if (actif) {
        char d[16], sb[16];
        snprintf(d, sizeof d, "%d dBm", (int)lroundf(r));
        snprintf(sb, sizeof sb, "S/B %d dB", (int)lroundf(sb_db));
        int ld = largeur_texte(d, POLICE_11), ls = largeur_texte(sb, POLICE_11);
        uint16_t csb = sb_db >= 20 ? VERT : (sb_db >= 8 ? JAUNE : AMBRE);
        if (sb_db < 0) texte(314, 57, d, POLICE_11, kw::surcharge_adc() ? ROUGE : TXT2, 255, DROITE);
        else if (314 - ld - 6 - ls >= xm + 46) {        // la place pour les deux
            texte(314, 57, d, POLICE_11, kw::surcharge_adc() ? ROUGE : TXT2, 255, DROITE);
            texte(314 - ld - 6, 57, sb, POLICE_11, csb, 255, DROITE);
        } else if ((t_ms / 2500) % 2) texte(314, 57, sb, POLICE_11, csb, 255, DROITE);   // sinon les deux a tour de role
        else texte(314, 57, d, POLICE_11, kw::surcharge_adc() ? ROUGE : TXT2, 255, DROITE);
    }
}

static void graduation()
{
    float sp = kw::cascade_largeur_khz();
    static const float PAS[] = { 0.1f, 0.2f, 0.5f, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    float pas = PAS[0];
    for (float p : PAS) { pas = p; if (sp / p <= 4.5f) break; }
    double f0 = f_de_x(0), f1 = f_de_x(wf::L);
    double f = ceil(f0 / pas) * pas;
    for (; f < f1; f += pas) {
        float x = x_de_f(f);
        rect((int)x, Y_SPEC + H_SPEC - 4, 1, 4, TXT2);
        char s[24];
        if (f >= 30000) {                               // en MHz au-dessus de 30 MHz
            int dec = pas >= 1000 ? 0 : (pas >= 100 ? 1 : (pas >= 10 ? 2 : 3));
            snprintf(s, sizeof s, "%.*f", dec, f / 1000.0);
            for (char *q = s; *q; q++) if (*q == '.') *q = ',';
        } else {
            fmt_freq(s, sizeof s, f, pas < 1 ? 1 : 0);
        }
        texte((int)x, Y_SPEC + H_SPEC - 17, s, POLICE_11, rgb(195, 208, 226), 220, CENTRE);
    }
}

// noms des stations envoyes par le KiwiSDR, sur deux rangees en haut du waterfall
static kw::Marqueur *mk = nullptr;              // [MAX_MARQUEURS] en PSRAM
static int nmk = 0;
static uint32_t v_mk = 0xFFFFFFFF;

static void etiquettes()
{
    if (!cfg::R.etiquettes || kw::etat() != kw::ACTIF) return;
    if (kw::version_marqueurs() != v_mk) {
        v_mk = kw::version_marqueurs();
        nmk = mk ? kw::marqueurs(mk, kw::MAX_MARQUEURS) : 0;
        // tri par frequence (peu d'elements)
        for (int i = 1; i < nmk; i++)
            for (int j = i; j > 0 && mk[j].khz < mk[j - 1].khz; j--) { kw::Marqueur t = mk[j]; mk[j] = mk[j - 1]; mk[j - 1] = t; }
    }
    int fin[2] = { -100, -100 };
    for (int i = 0; i < nmk; i++) {
        int x = (int)x_de_f(mk[i].khz);
        if (x < 0 || x >= 320) continue;
        const char *t = tronquer(mk[i].nom, POLICE_11, 74);
        int l = largeur_texte(t, POLICE_11) + 6;
        int x0 = x - l / 2;
        if (x0 < 0) x0 = 0;
        if (x0 + l > 320) x0 = 320 - l;
        int r = x0 > fin[0] + 2 ? 0 : (x0 > fin[1] + 2 ? 1 : -1);
        if (r < 0) continue;                                // pas de place : on n'encombre pas
        fin[r] = x0 + l;
        int y = Y_WF + 1 + r * 15;
        rect(x, Y_WF, 1, y - Y_WF + 1, rgb(255, 255, 255), 160);
        rect_arrondi(x0, y, l, 13, 3, rgb(12, 20, 40), 210);
        texte(x0 + 3, y, t, POLICE_11, rgb(255, 230, 160));
        zone(x0, y - 1, l, 15, B_ETIQ + i, Z_APPUI);
    }
}

static void dessin_journal(kw::Etat e);

static void spectre_et_cascade()
{
    suivre_cascade();
    // spectre
    rect(0, Y_SPEC, 320, H_SPEC, rgb(13, 21, 38));
    const float *sp = wf::spectre();
    float bas = wf::plancher_db() - 6;
    for (int x = 0; x < wf::L; x++) {
        int h = (int)((sp[x] - bas) * H_SPEC / 60.0f);
        if (h < 1) h = 1;
        if (h > H_SPEC) h = H_SPEC;
        rect(x, Y_SPEC + H_SPEC - h, 1, h, CYAN, 150);
        pixel(x, Y_SPEC + H_SPEC - h, TXT, 200);
    }
    graduation();
    // largeur affichee + zoom
    char z[24];
    float spk = kw::cascade_largeur_khz();
    if (spk >= 10) snprintf(z, sizeof z, "%d kHz", (int)lroundf(spk));
    else snprintf(z, sizeof z, "%.1f kHz", spk);
    texte(4, Y_SPEC + 2, z, POLICE_11, TXT2);
    bouton_scan(190, Y_SPEC + 1, -1, B_SCAN_BAS);
    bouton_scan(222, Y_SPEC + 1, +1, B_SCAN_HAUT);
    bouton(258, Y_SPEC + 1, 28, 20, "−", B_ZOOM_M, 0, POLICE_16, Z_APPUI);
    bouton(290, Y_SPEC + 1, 28, 20, "+", B_ZOOM_P, 0, POLICE_16, Z_APPUI);

    // waterfall
    for (int r = 0; r < H_WF; r++) memcpy(fb + (Y_WF + r) * L, wf::rangee(r), wf::L * sizeof(uint16_t));

    // filtre et repere d'accord
    int bas_hz, haut_hz;
    cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas_hz, &haut_hz);
    float xa = x_de_f(cfg::R.freq_khz + bas_hz / 1000.0), xb = x_de_f(cfg::R.freq_khz + haut_hz / 1000.0);
    if (xb - xa < 2) { float c = (xa + xb) / 2; xa = c - 1; xb = c + 1; }
    decoupe(0, Y_SPEC, 320, Y_WF + H_WF);
    bool bp_actif = pince || bp_popup_jusqua || (t_bp && t_ms - t_bp < 1500);
    rect((int)xa, Y_SPEC, (int)(xb - xa + 1), H_SPEC + H_WF, AMBRE, bp_actif ? 95 : 48);
    if (bp_actif) { rect((int)xa, Y_SPEC, 1, H_SPEC + H_WF, AMBRE, 255); rect((int)xb, Y_SPEC, 1, H_SPEC + H_WF, AMBRE, 255); }
    float xf = x_de_f(cfg::R.freq_khz);
    rect((int)xf, Y_SPEC, 2, H_SPEC + H_WF, AMBRE, 230);
    sans_decoupe();
    if (!panneau_audio) {
        zone(0, Y_SPEC + 22, 320 - L_BORD, H_SPEC - 22 + H_WF, B_CASCADE, Z_GLISSE);
        zone(320 - L_BORD, Y_SPEC + 22, L_BORD, H_SPEC - 22 + H_WF, B_VOL_BORD, Z_GLISSE);   // volume au doigt
    }
    if (!panneau_audio) etiquettes();
    if (!panneau_audio) scan_dessin();
    if (!panneau_audio) { cw_bandeau(); signal_dessin(); molette_dessin(); mr_dessin(); }
    if (!panneau_audio) bp_dessin();

    // pas connecte : on explique quoi faire
    kw::Etat e = kw::etat();
    if (e != kw::ACTIF && !panneau_audio) {
        rect(0, Y_WF, 320, H_WF, FOND, 200);
        int w = plat_wifi_etat();
        if (w == WIFI_POINT_ACCES) {
            texte(160, Y_WF + 4, "Configure le WiFi", POLICE_16, TXT, 255, CENTRE);
            char s[96];
            snprintf(s, sizeof s, "Connecte-toi au réseau « %s »", plat_wifi_nom());
            texte(160, Y_WF + 28, s, POLICE_11, TXT2, 255, CENTRE);
            snprintf(s, sizeof s, "mot de passe %s", plat_wifi_mdp());
            texte(160, Y_WF + 44, s, POLICE_11, TXT2, 255, CENTRE);
            snprintf(s, sizeof s, "puis ouvre http://%s", plat_wifi_ip());
            texte(160, Y_WF + 60, s, POLICE_11, TXT2, 255, CENTRE);
        } else if (w != WIFI_OK) {
            texte(160, Y_WF + 30, "Connexion au WiFi…", POLICE_16, TXT, 255, CENTRE);
        } else if (kw::journal_visible()) {
            dessin_journal(e);
        } else {
            texte(160, Y_WF + 8, tronquer(kw::message(), POLICE_16, 300), POLICE_16, e == kw::CONNEXION || e == kw::ATTENTE ? TXT : AMBRE, 255, CENTRE);
            if (e == kw::CONNEXION || e == kw::ATTENTE) {
                for (int i = 0; i < 3; i++) {
                    float a = (sinf(t_ms / 180.0f - i * 0.8f) + 1) / 2;
                    disque(148 + i * 12, Y_WF + 46, 3.5f, CYAN, (uint8_t)(60 + a * 195));
                }
            } else {
                bouton(cfg::R.hote[0] ? 60 : 112, Y_WF + 36, 96, 32, "Récepteurs", B_CHOISIR, 1);
                if (cfg::R.hote[0]) bouton(164, Y_WF + 36, 96, 32, "Réessayer", B_REESSAYER, 0);
            }
        }
    }
}

// journal de connexion, a la place du waterfall
static void dessin_journal(kw::Etat e)
{
    bool echec = !(e == kw::CONNEXION || e == kw::ATTENTE || e == kw::ACTIF);
    rect(0, Y_WF, 320, H_WF, FOND);
    rect(0, Y_WF, 320, 1, LIGNE);
    kw::LigneJ j[kw::MAX_J];
    int max = echec ? 4 : 6;
    int n = kw::journal(j, max);
    for (int i = 0; i < n; i++) {
        int y = Y_WF + 4 + i * 14;
        float cx = 10, cy = y + 7;
        if (j[i].e == kw::J_OK) {
            trait(cx - 4, cy, cx - 1, cy + 3, 1.6f, VERT);
            trait(cx - 1, cy + 3, cx + 4, cy - 3, 1.6f, VERT);
        } else if (j[i].e == kw::J_ECHEC) {
            trait(cx - 3, cy - 3, cx + 3, cy + 3, 1.6f, ROUGE);
            trait(cx - 3, cy + 3, cx + 3, cy - 3, 1.6f, ROUGE);
        } else {
            float a0 = t_ms / 120.0f;
            arc(cx, cy, 4, 1.5f, a0, a0 + 4.2f, AMBRE);
        }
        texte(20, y, tronquer(j[i].t, POLICE_11, 296), POLICE_11, j[i].e == kw::J_ECHEC ? AMBRE : (j[i].e == kw::J_OK ? TXT : TXT2));
    }
    if (echec) {
        int y = Y_WF + H_WF - 26;
        bouton(6, y, 100, 24, "Récepteurs", B_CHOISIR, 1, POLICE_13);
        if (cfg::R.hote[0]) bouton(110, y, 100, 24, "Réessayer", B_REESSAYER, 0, POLICE_13);
        bouton(214, y, 100, 24, "Plus proche", B_PROCHE, 0, POLICE_13);
    }
}

// gros bouton de la barre du bas (symbole - / + dessine, net a toutes tailles)
static void gros_bouton(int x, int y, int l, int h, const char *t, int id, int style, int signe = 0)
{
    bouton(x, y, l, h, signe ? "" : t, id, style, POLICE_16, Z_APPUI | (signe ? Z_LONG : 0));
    if (signe) {
        uint16_t c = TXT;
        rect_arrondi(x + l / 2 - 10, y + h / 2 - 2, 20, 4, 2, c);
        if (signe > 0) rect_arrondi(x + l / 2 - 2, y + h / 2 - 10, 4, 20, 2, c);
    }
}

static void barre_bas()
{
    rect(0, Y_BAS, 320, 240 - Y_BAS, PAN);
    rect(0, Y_BAS, 320, 1, LIGNE);
    int y = Y_BAS + 3, h = 240 - Y_BAS - 6;
    gros_bouton(4, y, 56, h, "", B_MOINS, 0, -1);
    gros_bouton(64, y, 56, h, "", B_PLUS, 0, +1);
    gros_bouton(124, y, 60, h, "MODE", B_MODE, 0);
    gros_bouton(188, y, 64, h, "AUDIO", B_AUDIO, panneau_audio ? 3 : 0);
    gros_bouton(256, y, 60, h, "MENU", B_MENU, 1);
}

static void segment_audio(int x, int y, int l, const char *const *noms, int n, int sel, int id0)
{
    int w = (l - (n - 1) * 4) / n;
    for (int i = 0; i < n; i++) bouton(x + i * (w + 4), y, w, 22, noms[i], id0 + i, i == sel ? 3 : 0, POLICE_13, Z_APPUI);
}

// curseur AGC : avec l'AGC, son seuil (-130 a -60 dB) ; sans AGC, le gain fixe (0 a 120 dB)
static float seuil_pos() { return cfg::R.agc ? (opt3.agc_seuil + 130) / 70.0f : opt3.gain_man / 120.0f; }
static void reglage_seuil(int x)
{
    float t = (x - 86) / 180.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    if (cfg::R.agc) opt3.agc_seuil = (int8_t)(-130 + (int)lroundf(t * 14) * 5);
    else opt3.gain_man = (uint8_t)((int)lroundf(t * 24) * 5);
    opt3_appliquer();
    t_opt3 = t_ms ? t_ms : 1;                           // enregistre quand on lache (voir tic)
}

static void panneau()
{
    const int y0 = Y_SPEC, h = Y_BAS - Y_SPEC;
    rect(0, y0, 320, h, FOND, 240);
    zone(0, y0, 320, h, B_AUD_FOND);                    // avale les touchers hors des commandes
    // volume
    int y = y0 + 1;
    texte(8, y + 5, "Volume", POLICE_13, TXT);
    float t = cfg::R.volume / 100.0f;
    rect_arrondi(86, y + 9, 180, 8, 4, LIGNE);
    rect_arrondi(86, y + 9, (int)(180 * t) + 4, 8, 4, CYAN);
    disque(86 + 180 * t, y + 13, 9, TXT);
    char v[12];
    snprintf(v, sizeof v, "%d", cfg::R.volume);
    texte(314, y + 5, v, POLICE_16, TXT, 255, DROITE);
    zone(70, y, 212, 25, B_AUD_VOL, Z_GLISSE | Z_APPUI);
    reg_curseur(B_AUD_VOL, 86, 180, t, 50);
    // AGC
    y += 26;
    texte(8, y + 4, "AGC", POLICE_13, TXT);
    static const char *AGC[4] = { "Off", "Lent", "Moyen", "Rapide" };
    segment_audio(86, y, 228, AGC, 4, cfg::R.agc, B_AUD_AGC);
    // seuil de l'AGC (ou gain fixe sans AGC)
    y += 24;
    texte(8, y + 5, cfg::R.agc ? "Seuil AGC" : "Gain fixe", POLICE_11, TXT2);
    t = seuil_pos();
    rect_arrondi(86, y + 9, 180, 6, 3, LIGNE);
    rect_arrondi(86, y + 9, (int)(180 * t) + 4, 6, 3, AMBRE);
    disque(86 + 180 * t, y + 12, 7, TXT);
    if (cfg::R.agc) snprintf(v, sizeof v, "%d dB", opt3.agc_seuil);
    else snprintf(v, sizeof v, "%d dB", opt3.gain_man);
    texte(314, y + 5, v, POLICE_11, TXT, 255, DROITE);
    zone(70, y, 212, 23, B_AUD_SEUIL, Z_GLISSE | Z_APPUI);
    reg_curseur(B_AUD_SEUIL, 86, 180, t, cfg::R.agc ? 14 : 24);
    // reduction de bruit
    y += 24;
    texte(8, y + 4, "Réd. bruit", POLICE_13, TXT);
    static const char *NR[3] = { "Off", "Douce", "Forte" };
    segment_audio(86, y, 228, NR, 3, cfg::R.nr, B_AUD_NR);
    // interrupteurs
    y += 24;
    bouton(6, y, 100, 24, "Anti-siffl.", B_AUD_NOTCH, cfg::R.notch ? 3 : 0, POLICE_13, Z_APPUI);
    static const char *NB[4] = { "Anti-paras.", "Paras. faible", "Paras. moyen", "Paras. fort" };
    bouton(110, y, 100, 24, NB[cfg::R.nb > 3 ? 2 : cfg::R.nb], B_AUD_NB, cfg::R.nb ? 3 : 0, cfg::R.nb ? POLICE_11 : POLICE_13, Z_APPUI);
    static const char *G[3] = { "Gain normal", "Gain +6 dB", "Gain +12 dB" };
    bouton(214, y, 100, 24, G[aud::gain_kiwi()], B_AUD_GAIN, aud::gain_kiwi() ? 3 : 0, POLICE_11, Z_APPUI);
}

// ---- texte morse en bas du waterfall (ecoute en CW) ----
static bool cw_bandeau_visible()
{
    return ecran == E_RADIO && cfg::R.mode == cfg::CW && !opt2.sans_cw && kw::etat() == kw::ACTIF && !panneau_audio;
}

static void cw_bandeau()
{
    if (!cw_bandeau_visible()) return;
    const int y = Y_BAS - 19, h = 17;
    rect(0, y, 320, h, rgb(4, 8, 16), 225);
    rect(0, y, 320, 1, LIGNE);
    bool cale = dec::cale();
    disque(8, y + 9, 3.5f, cale ? VERT : TXT2);         // vert : un signal morse est accroche
    char v[16];
    snprintf(v, sizeof v, "%s", dec::info1());           // "20 mots/min"
    if (char *p = strstr(v, " mots/min")) strcpy(p, " m/m");
    texte(16, y + 3, cale ? v : "CW", POLICE_11, cale ? VERT : TXT2);
    const int x0 = 74, l = 320 - x0 - 6;
    // les derniers caracteres qui tiennent, alignes a droite (le texte defile vers la gauche)
    const char *t = dec::texte();
    int n = (int)strlen(t);
    static char ligne[128];
    int k = 0, debut = n > 100 ? n - 100 : 0;
    for (int i = debut; i < n && k < (int)sizeof ligne - 1; i++) ligne[k++] = (t[i] == '\n' || t[i] == '\r') ? ' ' : t[i];
    ligne[k] = 0;
    const char *aff = ligne;
    while (*aff && largeur_texte(aff, POLICE_13) > l) aff++;
    if (*aff) texte(320 - 6, y + 1, aff, POLICE_13, TXT, 255, DROITE);
    else texte(x0, y + 3, "en attente d'un signal morse…", POLICE_11, TXT2);
    zone(0, y, 320 - L_BORD, h, B_CW_BANDEAU);          // le toucher ouvre l'ecran Decodeurs
}

// graphique du signal sur 2 minutes (toucher le S-metre)
static void signal_dessin()
{
    if (!sig_affiche) return;
    const int x0 = 6, y0 = Y_WF + 4, l = 308, h = Y_BAS - 3 - y0;
    rect_arrondi(x0, y0, l, h, 8, PAN, 245);
    contour_arrondi(x0, y0, l, h, 8, 1, CYAN, 200);
    zone(x0, y0, l, h, B_SMETRE);
    int mn = 127, mx = 0;
    for (int k = 0; k < sig_n; k++) { int v = sig_h[(sig_i - 1 - k + 2 * NB_SIG) % NB_SIG]; if (v < mn) mn = v; if (v > mx) mx = v; }
    char t[64];
    if (sig_n < 4) { texte(160, y0 + h / 2 - 7, "Signal : la mesure commence…", POLICE_13, TXT2, 255, CENTRE); return; }
    snprintf(t, sizeof t, "Signal sur %d s · de %d à %d dBm (écart %d dB)", sig_n / 2, mn - 127, mx - 127, mx - mn);
    texte(160, y0 + 3, t, POLICE_11, CYAN, 255, CENTRE);
    // echelle : au moins 30 dB, calee sur des dizaines
    int bas = (mn - 127 - 5) / 10 * 10 - 10, haut = bas + 30;
    while (haut < mx - 127 + 3) haut += 10;
    const int gx = x0 + 34, gy = y0 + 20, gl = l - 42, gh = h - 28;
    for (int d = bas; d <= haut; d += 10) {
        int y = gy + gh - (d - bas) * gh / (haut - bas);
        rect(gx, y, gl, 1, LIGNE);
        snprintf(t, sizeof t, "%d", d);
        texte(gx - 4, y - 6, t, POLICE_11, TXT2, 255, DROITE);
    }
    float xp = 0, yp = 0;
    for (int k = 0; k < sig_n; k++) {                   // le plus recent a droite
        int v = sig_h[(sig_i - 1 - k + 2 * NB_SIG) % NB_SIG] - 127;
        float x = gx + gl - 1 - k * (float)(gl - 1) / (NB_SIG - 1);
        float y = gy + gh - (float)(v - bas) * gh / (haut - bas);
        if (k) trait(xp, yp, x, y, 1.6f, VERT);
        xp = x; yp = y;
    }
}

// mode nuit : ecran attenue, heure et frequence en grand ; un toucher ou un cran du bouton reveille
static void ecran_nuit()
{
    rect(0, 0, 320, 240, rgb(0, 0, 0));
    char h[24], heure[8] = "";
    plat_horodatage(h, sizeof h);                       // "20261114-113605"
    char hh[4] = "", mm[4] = "";
    if (strlen(h) >= 13 && h[8] == '-') { snprintf(hh, sizeof hh, "%.2s", h + 9); snprintf(mm, sizeof mm, "%.2s", h + 11); snprintf(heure, sizeof heure, "%s%s", hh, mm); }
    // l'ensemble se deplace lentement : l'image ne reste pas au meme endroit
    int dx = (int)((t_ms / 60000) % 5) * 8 - 16, dy = (int)((t_ms / 60000) % 3) * 10 - 10;
    uint16_t c1 = rgb(150, 110, 40), c2 = rgb(90, 96, 110);
    int y = 60 + dy;
    if (heure[0]) {                                     // "06:12" : la grande police n'a que les chiffres, les deux points sont dessines
        int l1 = largeur_texte(hh, POLICE_40), l2 = largeur_texte(mm, POLICE_40), x0 = 160 + dx - (l1 + 16 + l2) / 2;
        texte(x0, y, hh, POLICE_40, c1);
        disque(x0 + l1 + 8, y + 16, 3, c1);
        disque(x0 + l1 + 8, y + 32, 3, c1);
        texte(x0 + l1 + 16, y, mm, POLICE_40, c1);
        y += 56;
    }
    else y += 20;
    if (kw::etat() == kw::ACTIF || fm::actif()) {
        char f[24], t[64];
        if (fm::actif()) { fm::Info i; fm::info(&i); snprintf(t, sizeof t, "%.1f MHz · FM", i.mhz); for (char *q = t; *q; q++) if (*q == '.') *q = ','; }
        else { fmt_freq_u(f, sizeof f, cfg::R.freq_khz); snprintf(t, sizeof t, "%s · %s", f, cfg::nom_mode(cfg::R.mode)); }
        texte(160 + dx, y, t, POLICE_22, c2, 255, CENTRE);
        if (!fm::actif()) texte(160 + dx, y + 30, tronquer(kw::nom(), POLICE_11, 280), POLICE_11, rgb(60, 66, 80), 255, CENTRE);
    } else texte(160 + dx, y, "pas de récepteur", POLICE_13, c2, 255, CENTRE);
    zone(0, 0, 320, 240, B_SMETRE);                     // tout l'ecran reveille (le toucher est avale avant)
}

static void nuit_tic()
{
    static const uint32_t DELAI[4] = { 0, 60000, 300000, 900000 };
    if (nuit_actif || !opt2.nuit) return;
    if (t_ms - t_geste < DELAI[opt2.nuit & 3]) return;
    if (ecran != E_RADIO && ecran != E_FM) return;      // seulement pendant l'ecoute, pas au milieu d'un menu
    if (sc_sens || fs_sens || kw::cascade_forcee()) return;
    nuit_actif = true;
    plat_luminosite(6);
}

static void ecran_radio()
{
    rect(0, 0, 320, 240, FOND);
    barre_haut();
    ligne_frequence();
    spectre_et_cascade();
    if (kw::etat() == kw::ACTIF && !panneau_audio && kw::journal_visible()) dessin_journal(kw::ACTIF);
    else if (kw::sans_cascade()) {                          // le KiwiSDR n'envoie pas de waterfall a ce canal
        rect_arrondi(20, Y_WF + 18, 280, 52, 8, PAN, 235);
        char l1[64];
        snprintf(l1, sizeof l1, "Pas de waterfall sur le canal %d", kw::canal_rx() + 1);
        texte(160, Y_WF + 24, l1, POLICE_13, AMBRE, 255, CENTRE);
        char l2[80];
        snprintf(l2, sizeof l2, "Le waterfall est réservé aux canaux 1 à %d (le son marche)", kw::canaux_cascade());
        texte(160, Y_WF + 44, l2, POLICE_11, TXT2, 255, CENTRE);
    }
    if (!panneau_audio) {
        // repere discret du bord droit ; jauge de volume pendant le geste et 1 s apres
        bool actif = (tc_appui && tc_z.id == B_VOL_BORD) || (t_vol_bord && t_ms - t_vol_bord < 1000);
        int y0 = Y_SPEC + 26, h = H_SPEC - 30 + H_WF;
        if (!actif) {
            rect_arrondi(316, y0 + h / 2 - 20, 3, 40, 1, TXT2, 90);
        } else {
            rect_arrondi(286, y0, 30, h, 8, PAN, 230);
            int hv = (h - 30) * cfg::R.volume / 100;
            rect_arrondi(296, y0 + 6, 10, h - 30, 4, LIGNE);
            rect_arrondi(296, y0 + 6 + (h - 30 - hv), 10, hv + 1, 4, CYAN);
            char v[8];
            snprintf(v, sizeof v, "%d", cfg::R.volume);
            texte(301, y0 + h - 20, v, POLICE_11, TXT, 255, CENTRE);
        }
    }
    if (panneau_audio) panneau();
    barre_bas();
}

static void pas_freq(int sens)
{
    int p = cfg::PAS_HZ[cfg::R.pas];
    double f = arrondi_pas(cfg::R.freq_khz, p) + sens * p / 1000.0;
    if (fabs(arrondi_pas(cfg::R.freq_khz, p) - cfg::R.freq_khz) > 1e-6 && ((sens > 0) == (arrondi_pas(cfg::R.freq_khz, p) > cfg::R.freq_khz)))
        f = arrondi_pas(cfg::R.freq_khz, p);       // d'abord se caler sur le pas
    accorder(f);
}

static void zoom(int d)
{
    int z = cfg::R.zoom + d;
    if (z < 0) z = 0;
    if (z > 14) z = 14;
    if (z == cfg::R.zoom) return;
    cfg::R.zoom = (uint8_t)z;
    cfg::marquer();
    kw::regler_cascade();
}

// ============================ En-tete commun ================================
static void entete(const char *titre, const char *droite = nullptr, uint16_t cd = AMBRE)
{
    rect(0, 0, 320, 26, PAN);
    rect(0, 26, 320, 1, LIGNE);
    bool ap = tc_appui && tc_z.id == B_RETOUR;
    rect_arrondi(5, 3, 26, 20, 4, ap ? melange(BTN, TXT, 60) : BTN);
    chevron_retour(15, 8, TXT);
    zone(0, 0, 40, 26, B_RETOUR);
    texte(38, 5, titre, POLICE_16, TXT);
    if (droite) texte(314, 7, droite, POLICE_13, cd, 255, DROITE);
}

static void freq_mode(char *s, int n)
{
    char f[24];
    fmt_freq_aff(f, sizeof f, cfg::R.freq_khz);
    snprintf(s, n, "%s %s", f, cfg::nom_mode(cfg::R.mode));
}

// ============================ MENU ==========================================
static void dessin_tuile(int i, float cx, float cy)
{
    uint16_t c = CYAN;
    switch (i) {
    case 8:   // FM : antenne et ondes
        trait(cx, cy - 2, cx, cy + 12, 1.8f, c);
        trait(cx - 5, cy + 12, cx + 5, cy + 12, 1.6f, c);
        disque(cx, cy - 4, 2.5f, c);
        arc(cx, cy - 4, 7, 1.5f, -0.9f, 0.9f, c);
        arc(cx, cy - 4, 7, 1.5f, 2.24f, 4.04f, c);
        arc(cx, cy - 4, 12, 1.5f, -0.8f, 0.8f, c);
        arc(cx, cy - 4, 12, 1.5f, 2.34f, 3.94f, c);
        break;
    case 0:   // radio
        rect_arrondi(cx - 13, cy - 6, 26, 16, 3, PAN);
        contour_arrondi(cx - 13, cy - 6, 26, 16, 3, 1.6f, c);
        trait(cx - 9, cy - 6, cx + 7, cy - 13, 1.6f, c);
        anneau(cx + 5, cy + 2, 4, 1.5f, c);
        trait(cx - 9, cy - 1, cx - 3, cy - 1, 1.4f, c);
        trait(cx - 9, cy + 4, cx - 3, cy + 4, 1.4f, c);
        break;
    case 1:   // globe
        anneau(cx, cy, 12, 1.6f, c);
        trait(cx - 12, cy, cx + 12, cy, 1.4f, c);
        arc(cx - 9, cy, 15, 1.4f, -0.93f, 0.93f, c);
        arc(cx + 9, cy, 15, 1.4f, 2.21f, 4.07f, c);
        break;
    case 2:   // quoi ecouter : niveaux d'activite et une etoile
        for (int k = 0; k < 4; k++) rect_arrondi(cx - 13 + k * 6, cy + 10 - (5 + k * 5), 4, 5 + k * 5, 1, k < 3 ? c : melange(PAN, c, 110));
        etoile(cx + 9, cy - 8, 5, AMBRE, true);
        break;
    case 3: etoile(cx, cy, 12, AMBRE, false); break;
    case 4:   // lignes de texte
        for (int j = 0; j < 3; j++) {
            trait(cx - 12, cy - 6 + j * 6, cx - 12 + 8 + j * 4, cy - 6 + j * 6, 1.6f, c);
            trait(cx - 1 + j * 2, cy - 6 + j * 6, cx + 12, cy - 6 + j * 6, 1.6f, c);
        }
        break;
    case 5: {   // enregistrer
        bool rec = mot::enregistre();
        anneau(cx, cy, 11, 1.6f, ROUGE);
        float r = rec ? 5 + 1.5f * sinf(t_ms / 200.0f) : 5;
        disque(cx, cy, r, ROUGE);
        break;
    }
    case 6:   // curseurs
        trait(cx - 12, cy - 5, cx + 12, cy - 5, 1.6f, c);
        trait(cx - 12, cy + 5, cx + 12, cy + 5, 1.6f, c);
        disque(cx + 4, cy - 5, 3.2f, c);
        disque(cx - 4, cy + 5, 3.2f, c);
        break;
    case 7: icone_wifi((int)cx, (int)cy - 6, c); break;
    }
}

static void ecran_menu()
{
    rect(0, 0, 320, 240, FOND);
    char fm[40];
    freq_mode(fm, sizeof fm);
    entete("Menu", fm);
    static const char *T[9] = { "Radio", "Récepteurs", "Quoi écouter", "Favoris", "Décodeurs", "Enregistrer", "Réglages", "WiFi", "Radio FM" };
    char sous[9][40];
    snprintf(sous[0], 40, "%s", kw::etat() == kw::ACTIF ? "En écoute" : "Écoute");
    snprintf(sous[1], 40, "%s", srv::nb_publics() ? "Monde" : "À charger");
    snprintf(sous[2], 40, "Suggestions");
    snprintf(sous[3], 40, "%d station%s", nfav, nfav > 1 ? "s" : "");
    snprintf(sous[4], 40, "CW · RTTY · FAX");
    if (mot::enregistre()) snprintf(sous[5], 40, "En cours %u:%02u", (unsigned)(mot::duree_s() / 60), (unsigned)(mot::duree_s() % 60));
    else snprintf(sous[5], 40, "%s", plat_sd() ? "Carte SD" : "Pas de SD");
    snprintf(sous[6], 40, "Audio · Écran");
    snprintf(sous[7], 40, "%s", plat_wifi_etat() == WIFI_OK ? "Portail web" : "À configurer");
    snprintf(sous[8], 40, "%s", fm::actif() ? "En écoute" : (wr::actif() ? "Webradio" : "FM-DX · Web"));
    // 3 x 3 tuiles ; la radio FM juste apres la radio KiwiSDR
    static const int ORDRE[9] = { 0, 8, 1, 2, 3, 4, 5, 6, 7 };
    const int l = 99, h = 66, g = 5;
    for (int k = 0; k < 9; k++) {
        int i = ORDRE[k];
        int x = 6 + (k % 3) * (l + g), y = 31 + (k / 3) * (h + g);
        bool ap = tc_appui && tc_z.id == B_TUILE + i;
        rect_arrondi(x, y, l, h, 8, ap ? melange(PAN, TXT, 40) : PAN);
        contour_arrondi(x, y, l, h, 8, 1, (i == 5 && mot::enregistre()) || (i == 8 && (fm::actif() || wr::actif())) ? (i == 5 ? ROUGE : VERT) : LIGNE);
        dessin_tuile(i, x + l / 2.0f, y + 20);
        texte(x + l / 2, y + 34, T[i], POLICE_13, TXT, 255, CENTRE);
        texte(x + l / 2, y + 50, tronquer(sous[i], POLICE_11, l - 6), POLICE_11, TXT2, 255, CENTRE);
        zone(x, y, l, h, B_TUILE + i);
    }
}

// ============================ RECEPTEURS ====================================
static int region = srv::PROCHES;
static float liste_dec = 0, liste_v = 0;
static const srv::Serveur **vue = nullptr;      // [MAX_PUBLICS + MAX_PERSO] en PSRAM
static int nvue = 0;
static uint32_t t_vue = 0;
static const int Y_LISTE = 54, H_LIGNE = 31;

static bool liste_vhf = false;                  // Recepteurs : liste OpenWebRX VHF/UHF au lieu des KiwiSDR
static const owl::RecepteurVHF **vue_vhf = nullptr;   // [MAX_VHF] en PSRAM
static int nvue_vhf = 0;

static void maj_vue()
{
    nvue = srv::filtrer(region, vue, srv::MAX_PUBLICS + srv::MAX_PERSO);
    if (!vue_vhf) vue_vhf = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    nvue_vhf = vue_vhf ? owl::chercher("", vue_vhf, owl::MAX_VHF) : 0;
    t_vue = t_ms;
}

// bascule HF (KiwiSDR) / VHF-UHF (OpenWebRX) dans l'en-tete
// ---- "Pres de moi" : les recepteurs les plus proches de la position de la carte, tous types confondus ----
static bool liste_proches = false;
struct LigneProche { uint8_t type; const void *p; };    // 0 titre, 1 KiwiSDR, 2 OpenWebRX VHF/UHF, 3 serveur FM
static const int MAX_PROCHES = 24, PAR_TYPE = 5;
static LigneProche proches[MAX_PROCHES];
static const char *titre_proche[MAX_PROCHES];
static int nproches = 0;
static uint32_t t_proches = 0;

static void maj_proches()
{
    t_proches = t_ms;
    int n = 0;
    static const srv::Serveur **vk = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    static const owl::RecepteurVHF **vv = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    static const fml::ServeurFM **vf = (const fml::ServeurFM **)plat_malloc_ps(sizeof(void *) * 700);
    if (!vk || !vv || !vf) { nproches = 0; return; }
    auto titre = [&](const char *t) { titre_proche[n] = t; proches[n++] = LigneProche{ 0, nullptr }; };
    // KiwiSDR avec une place libre
    int nk = srv::filtrer(srv::PROCHES, vk, srv::MAX_PUBLICS + srv::MAX_PERSO), k = 0;
    titre(nk ? "Ondes courtes (KiwiSDR, place libre)" : (plat_liste_en_cours() ? "Ondes courtes : chargement…" : "Ondes courtes : liste vide"));
    for (int i = 0; i < nk && k < PAR_TYPE; i++)
        if (!vk[i]->perso && vk[i]->users < vk[i]->umax && vk[i]->km < 65000) { proches[n++] = LigneProche{ 1, vk[i] }; k++; }
    int nv = owl::chercher("", vv, owl::MAX_VHF);
    titre(nv ? "VHF / UHF (OpenWebRX)" : (plat_vhf_liste_en_cours() ? "VHF / UHF : chargement…" : "VHF / UHF : liste vide"));
    for (int i = 0; i < nv && i < PAR_TYPE; i++) proches[n++] = LigneProche{ 2, vv[i] };
    int nf = fml::chercher("", vf, 700);
    k = 0;
    titre(nf ? "Radio FM (FM-DX)" : (plat_fm_liste_en_cours() ? "Radio FM : chargement…" : "Radio FM : liste vide"));
    for (int i = 0; i < nf && k < PAR_TYPE; i++)
        if (vf[i]->etat == 1 && vf[i]->km < 65000) { proches[n++] = LigneProche{ 3, vf[i] }; k++; }
    nproches = n;
}

static void ouvrir_proches()
{
    liste_proches = true;
    liste_dec = 0;
    liste_v = 0;
    if (plat_wifi_etat() == WIFI_OK) {                  // les trois listes, si elles manquent
        if (!srv::nb_publics() && !plat_liste_en_cours()) plat_liste_telecharger();
        if (!owl::nb() && !plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
        if (!fml::nb() && !plat_fm_liste_en_cours()) plat_fm_liste_telecharger();
    }
    maj_proches();
}

static void proches_dessin()
{
    static uint32_t v_geo = 0xFFFFFFFF;
    if (t_ms - t_proches > 1500 || v_geo != geo::version()) { v_geo = geo::version(); maj_proches(); }
    // position de la carte
    char e[96];
    geo::etiquette(e, sizeof e);
    texte(8, 27, geo::etat()[0] ? tronquer(geo::etat(), POLICE_11, 222) : "Ma position", POLICE_11, geo::etat()[0] ? AMBRE : TXT2);
    texte(8, 39, tronquer(e, POLICE_13, 222), POLICE_13, TXT);
    bouton(236, 31, 80, 22, geo::en_cours() ? "…" : "Localiser", B_LOCALISER, 0, POLICE_13);
    decoupe(0, Y_LISTE, 320, 220);
    int max_dec = nproches * H_LIGNE - (220 - Y_LISTE) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    for (int i = premier; i < nproches && i < premier + 8; i++) {
        int y = Y_LISTE + i * H_LIGNE - (int)liste_dec;
        const LigneProche &l = proches[i];
        if (l.type == 0) {
            texte(8, y + 9, titre_proche[i], POLICE_13, CYAN);
            rect(8, y + H_LIGNE - 5, 304, 1, LIGNE);
            zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE);
            continue;
        }
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (i - premier);
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        const char *nom = "";
        char l2[80];
        unsigned km = 0;
        if (l.type == 1) {
            const srv::Serveur *r = (const srv::Serveur *)l.p;
            nom = r->nom; km = r->km;
            snprintf(l2, sizeof l2, "%s · %d/%d auditeurs", r->lieu, r->users, r->umax);
        } else if (l.type == 2) {
            const owl::RecepteurVHF *r = (const owl::RecepteurVHF *)l.p;
            nom = r->nom; km = r->km;
            snprintf(l2, sizeof l2, "%s", r->bandes == 3 ? "VHF · UHF" : (r->bandes & 2 ? "UHF" : "VHF"));
        } else {
            const fml::ServeurFM *r = (const fml::ServeurFM *)l.p;
            nom = r->nom; km = r->km;
            snprintf(l2, sizeof l2, "%s%s%s", r->ville, r->ville[0] && r->pays[0] ? ", " : "", r->pays);
        }
        disque(14, y + 14, 4, l.type == 1 ? VERT : (l.type == 2 ? CYAN : AMBRE));
        texte(24, y + 2, tronquer(nom, POLICE_13, 222), POLICE_13, TXT);
        texte(24, y + 16, tronquer(l2, POLICE_11, 222), POLICE_11, TXT2);
        char d[16];
        snprintf(d, sizeof d, "%u km", km);
        texte(308, y + 8, d, POLICE_13, TXT, 255, DROITE);
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE);
    }
    sans_decoupe();
    // raccourci : ce qu'il y a a ecouter maintenant
    bool apq = tc_appui && tc_z.id == B_QUOI;
    rect(0, 220, 320, 20, PAN);
    if (apq) rect(0, 221, 160, 19, melange(PAN, TXT, 40));
    rect(0, 220, 320, 1, LIGNE);
    rect(160, 223, 1, 14, LIGNE);
    texte(80, 223, "Quoi écouter  ›", POLICE_11, CYAN, 255, CENTRE);
    zone(0, 216, 160, 24, B_QUOI);
    bool apc = tc_appui && tc_z.id == B_CARTE_OUVRIR;
    if (apc) rect(161, 221, 159, 19, melange(PAN, TXT, 40));
    texte(240, 223, "Carte du monde  ›", POLICE_11, CYAN, 255, CENTRE);
    zone(160, 216, 160, 24, B_CARTE_OUVRIR);
}

static void choix_liste()
{
    static const char *N[3] = { "Proches", "KiwiSDR", "VHF/UHF" };
    static const int ID[3] = { B_LISTE_PROCHES, B_LISTE_HF, B_LISTE_VHF };
    int x = 34;
    for (int i = 0; i < 3; i++) {
        int l = largeur_texte(N[i], POLICE_11) + 14;
        bool on = i == 0 ? liste_proches : (!liste_proches && (i == 2) == liste_vhf);
        rect_arrondi(x, 4, l, 19, 9, on ? BLEU : PAN);
        if (!on) contour_arrondi(x, 4, l, 19, 9, 1, LIGNE);
        texte(x + l / 2, 7, N[i], POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
        zone(x, 0, l, 26, ID[i]);
        x += l + 3;
    }
}

// pied des listes de recepteurs : un ou deux raccourcis
static void pied_liste(const char *gauche, int id_g, const char *droite, int id_d)
{
    rect(0, 220, 320, 20, PAN);
    rect(0, 220, 320, 1, LIGNE);
    if (gauche) {
        if (tc_appui && tc_z.id == id_g) rect(0, 221, 160, 19, melange(PAN, TXT, 40));
        if (tc_appui && tc_z.id == id_d) rect(161, 221, 159, 19, melange(PAN, TXT, 40));
        rect(160, 223, 1, 14, LIGNE);
        texte(80, 223, tronquer(gauche, POLICE_11, 152), POLICE_11, CYAN, 255, CENTRE);
        zone(0, 216, 160, 24, id_g);
        texte(240, 223, droite, POLICE_11, CYAN, 255, CENTRE);
        zone(160, 216, 160, 24, id_d);
    } else {
        if (tc_appui && tc_z.id == id_d) rect(0, 221, 320, 19, melange(PAN, TXT, 40));
        texte(160, 223, droite, POLICE_11, CYAN, 255, CENTRE);
        zone(0, 216, 320, 24, id_d);
    }
}

static void liste_vhf_dessin(bool charge)
{
    texte(8, 34, "OpenWebRX · receiverbook.de · triés par distance", POLICE_11, TXT2);
    decoupe(0, Y_LISTE, 320, 220);
    int max_dec = nvue_vhf * H_LIGNE - (220 - Y_LISTE) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    for (int i = premier; i < nvue_vhf && i < premier + 8; i++) {
        const owl::RecepteurVHF *r = vue_vhf[i];
        int y = Y_LISTE + i * H_LIGNE - (int)liste_dec;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (i - premier);
        bool courant = cfg::R.proto == cfg::OWRX && strstr(r->url, cfg::R.hote) != nullptr;
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        if (courant) contour_arrondi(4, y, 312, H_LIGNE - 3, 6, 1.2f, BLEU);
        disque(14, y + 14, 4, CYAN);
        texte(24, y + 2, tronquer(r->nom, POLICE_13, 230), POLICE_13, TXT);
        char l2[64];
        const char *b = r->bandes == 3 ? "VHF · UHF" : (r->bandes & 2 ? "UHF" : "VHF");
        if (r->km < 65000) snprintf(l2, sizeof l2, "%s · %u km", b, r->km);
        else snprintf(l2, sizeof l2, "%s", b);
        texte(24, y + 16, l2, POLICE_11, TXT2);
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE);
    }
    sans_decoupe();
    pied_liste(nullptr, 0, "Carte du monde  ›", B_CARTE_OUVRIR);
    if (nvue_vhf == 0) {
        const char *t1 = charge ? "Chargement de la liste VHF/UHF…" : (plat_wifi_etat() != WIFI_OK ? "Pas de connexion Internet" : "Liste VHF/UHF vide");
        const char *t2 = charge ? "receiverbook.de, quelques secondes" : "Touche la flèche en haut à droite";
        texte(160, 110, t1, POLICE_16, TXT, 255, CENTRE);
        texte(160, 134, t2, POLICE_11, TXT2, 255, CENTRE);
    }
}

static void ecran_recepteurs()
{
    rect(0, 0, 320, 240, FOND);
    if (t_ms - t_vue > 700) maj_vue();
    entete("");
    choix_liste();
    bool charge = liste_proches ? (plat_liste_en_cours() || plat_vhf_liste_en_cours() || plat_fm_liste_en_cours())
                : (liste_vhf ? plat_vhf_liste_en_cours() : plat_liste_en_cours());
    // bouton actualiser (fleche circulaire)
    bool ap = tc_appui && tc_z.id == B_ACTUALISER;
    rect_arrondi(286, 3, 28, 20, 4, ap ? melange(BTN, TXT, 60) : BTN);
    float a0 = charge ? t_ms / 150.0f : 0;
    arc(300, 13, 6, 1.6f, a0 + 0.5f, a0 + 5.6f, charge ? CYAN : TXT);
    triangle(300 + 6 * cosf(a0 + 0.5f) + 3, 13 + 6 * sinf(a0 + 0.5f), 300 + 6 * cosf(a0 + 0.5f) - 3,
             13 + 6 * sinf(a0 + 0.5f), 300 + 6 * cosf(a0 + 0.5f), 13 + 6 * sinf(a0 + 0.5f) - 4, charge ? CYAN : TXT);
    zone(280, 0, 40, 26, B_ACTUALISER);
    if (liste_proches) { proches_dessin(); return; }
    texte(278, 7, tronquer(liste_vhf ? owl::etat_liste() : srv::etat_liste(), POLICE_11, 52), POLICE_11, TXT2, 255, DROITE);
    if (liste_vhf) { liste_vhf_dessin(charge); return; }

    // regions
    int x = 4;
    for (int r = 0; r < srv::NB_REGIONS; r++) {
        const char *n = srv::nom_region(r);
        bool perso = r == srv::PERSO;
        int l = perso ? 24 : largeur_texte(n, POLICE_11) + 10;
        bool on = r == region;
        rect_arrondi(x, 31, l, 18, 9, on ? BLEU : PAN);
        if (!on) contour_arrondi(x, 31, l, 18, 9, 1, LIGNE);
        if (perso) etoile(x + l / 2.0f, 40, 6, on ? SOMBRE : AMBRE, true);
        else texte(x + l / 2, 33, n, POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
        zone(x, 29, l, 22, B_REGION + r);
        x += l + 3;
    }

    // liste
    decoupe(0, Y_LISTE, 320, 220);
    int max_dec = nvue * H_LIGNE - (220 - Y_LISTE) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    for (int i = premier; i < nvue && i < premier + 8; i++) {
        const srv::Serveur *s = vue[i];
        int y = Y_LISTE + i * H_LIGNE - (int)liste_dec;
        bool plein = s->umax && s->users >= s->umax;
        bool ap2 = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (i - premier);
        bool courant = !strcmp(s->hote, cfg::R.hote) && s->port == cfg::R.port;
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap2 ? melange(PAN, TXT, 40) : PAN);
        if (courant) contour_arrondi(4, y, 312, H_LIGNE - 3, 6, 1.2f, BLEU);
        uint16_t c = plein ? ROUGE : (s->umax && s->users + 1 >= s->umax ? JAUNE : VERT);
        if (s->perso) c = CYAN;
        disque(14, y + 14, 4, c, plein ? 140 : 255);
        texte(24, y + 2, tronquer(s->nom, POLICE_13, 220), POLICE_13, plein ? TXT2 : TXT);
        char l2[80];
        if (s->perso) snprintf(l2, sizeof l2, "%s:%u", s->hote, s->port);
        else if (s->km < 65000) snprintf(l2, sizeof l2, "%s · %u km", s->lieu, s->km);
        else snprintf(l2, sizeof l2, "%s", s->lieu);
        texte(24, y + 16, tronquer(l2, POLICE_11, 230), POLICE_11, TXT2);
        if (!s->perso && s->umax) {
            char u[12];
            snprintf(u, sizeof u, "%u/%u", s->users, s->umax);
            texte(308, y + 7, plein ? "plein" : u, POLICE_13, plein ? ROUGE : TXT, 255, DROITE);
        }
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE | Z_LONG);
    }
    sans_decoupe();
    pied_liste("Meilleur récepteur  ›", B_MEILLEUR, "Carte du monde  ›", B_CARTE_OUVRIR);
    if (nvue == 0) {
        const char *t1, *t2;
        if (region == srv::PERSO) { t1 = "Aucun récepteur perso"; t2 = "Ajoute-les depuis le portail web (menu WiFi)"; }
        else if (charge) { t1 = "Chargement de la liste…"; t2 = "Quelques secondes"; }
        else if (plat_wifi_etat() != WIFI_OK) { t1 = "Pas de connexion Internet"; t2 = "Configure le WiFi d'abord"; }
        else { t1 = "Liste vide"; t2 = "Touche la flèche en haut à droite"; }
        texte(160, 110, t1, POLICE_16, TXT, 255, CENTRE);
        texte(160, 134, t2, POLICE_11, TXT2, 255, CENTRE);
        // la raison d'un echec (chaque site essaye) ; un nouvel essai se fait tout seul
        const char *r = srv::etat_liste();
        if (!charge && region != srv::PERSO && r && (strstr(r, " : ") || strstr(r, "Échec"))) {
            char a[200];
            snprintf(a, sizeof a, "%s", r);
            char *p2 = strstr(a, " ; ");
            if (p2) *p2 = 0;
            texte(160, 156, tronquer(a, POLICE_11, 310), POLICE_11, AMBRE, 255, CENTRE);
            if (p2) texte(160, 170, tronquer(p2 + 3, POLICE_11, 310), POLICE_11, AMBRE, 255, CENTRE);
            texte(160, 188, "Nouvel essai automatique dans 30 s (3 fois)", POLICE_11, TXT2, 255, CENTRE);
        }
    }
}

// ============================ RADIO FM (reseau FM-DX) =========================
static const fml::ServeurFM **vue_fm = nullptr;  // [MAX_FM] en PSRAM
static int nvue_fm = 0;
static uint32_t t_vue_fm = 0;
static float glisse_mhz = 0;

static void maj_vue_fm()
{
    nvue_fm = fml::chercher("", vue_fm, fml::MAX_FM);
    t_vue_fm = t_ms;
}

static void fm_pas(int sens)
{
    fm::Info i;
    fm::info(&i);
    if (!fm::actif() || i.mhz <= 0) return;
    float f = roundf(i.mhz * 10) / 10 + sens * 0.1f;
    if (f < 64) f = 64;
    if (f > 108) f = 108;
    fm::accorder(f);
}

// ============================ Autoscan FM (serveur FM-DX) ====================
// Pas de waterfall : on avance de 0,1 MHz, on attend que le serveur confirme la frequence et
// envoie des mesures neuves, et on garde la frequence si le signal depasse nettement le bruit.
// Si la frequence suivante est encore plus forte, on monte jusqu'au sommet avant de s'arreter.
static int fs_vitesse = 1, fs_phase = 0;             // phase : 0 pas suivant, 1 mesure, 2 ecoute
static float fs_f = 0, fs_sig = -99, fs_bruit = 0;
static float fs_cand = 0, fs_cand_sig = -99;          // meilleure frequence de la station en cours de montee
static bool fs_sauter = false;                       // on part d'une station : la passer d'abord
static uint32_t fs_t = 0, fs_t_conf = 0, fs_maj0 = 0;
static int fs_refus = 0;
struct FmTrouvee { float mhz; float sig; char ps[10]; };
static const int MAX_FMT = 80;
static FmTrouvee fmt[MAX_FMT];
static int nfmt = 0;
static char fmt_serveur[100] = "";

static float fs_seuil() { static const float S[3] = { 20, 13, 7 }; return S[sc_sensibilite % 3]; }
static uint32_t fs_mesure_ms() { static const uint32_t V[4] = { 350, 220, 120, 60 }; return V[(fs_vitesse - 1) & 3]; }
static uint32_t fs_mesures() { return fs_vitesse <= 2 ? 2 : 1; }   // etats neufs a attendre apres le changement

static void fs_arreter(const char *pourquoi = nullptr)
{
    if (!fs_sens) return;
    fs_sens = 0;
    if (pourquoi) message(pourquoi);
}

static void fs_noter(float mhz, float sig, const char *ps)
{
    for (int i = 0; i < nfmt; i++)
        if (fabsf(fmt[i].mhz - mhz) < 0.05f) {
            if (sig > fmt[i].sig) fmt[i].sig = sig;
            if (ps && ps[0]) snprintf(fmt[i].ps, sizeof fmt[i].ps, "%s", ps);
            return;
        }
    if (nfmt >= MAX_FMT) return;
    FmTrouvee &t = fmt[nfmt++];
    t.mhz = mhz;
    t.sig = sig;
    snprintf(t.ps, sizeof t.ps, "%s", ps ? ps : "");
    for (int i = nfmt - 1; i > 0 && fmt[i].mhz < fmt[i - 1].mhz; i--) { FmTrouvee x = fmt[i]; fmt[i] = fmt[i - 1]; fmt[i - 1] = x; }
}

static void fs_lancer(int sens)
{
    fm::Info i;
    fm::info(&i);
    if (!fm::actif() || i.etat != fm::ACTIF || i.mhz <= 0) { message("Connecte-toi d'abord à un serveur FM"); return; }
    if (strcmp(fmt_serveur, i.url)) { nfmt = 0; snprintf(fmt_serveur, sizeof fmt_serveur, "%s", i.url); }
    fs_sens = sens > 0 ? 1 : -1;
    fs_vitesse = 1;
    fs_phase = 0;
    fs_f = roundf(i.mhz * 10) / 10;
    fs_sauter = true;
    fs_cand = 0;
    fs_cand_sig = -99;                                  // sinon une station moins forte que la precedente serait sautee
    fs_bruit = 0;
    fs_refus = 0;
}

static void fs_appui(int sens)
{
    int d = sens > 0 ? 1 : -1;
    if (fs_sens != d) { fs_lancer(d); return; }
    if (++fs_vitesse > 4) fs_arreter();
}

static void fs_aller(float f)
{
    if (f > 108.05f) f = 87.5f;                          // bout de bande : on repart de l'autre cote
    if (f < 87.45f) f = 108.0f;
    fs_f = roundf(f * 10) / 10;
    fm::accorder(fs_f);
    fm::Info i;
    fm::info(&i);
    fs_maj0 = i.maj;
    fs_t = t_ms;
    fs_t_conf = 0;
    fs_sig = -99;
    fs_phase = 1;
}

static void fs_tic()
{
    if (!fs_sens) return;
    fm::Info i;
    fm::info(&i);
    if (!fm::actif() || i.etat != fm::ACTIF) { fs_arreter(); return; }
    switch (fs_phase) {
    case 0:
        fs_aller(fs_f + fs_sens * 0.1f);
        break;
    case 1: {
        // le serveur doit confirmer la frequence, puis envoyer au moins deux mesures
        if (!fs_t_conf) {
            if (fabsf(i.mhz_serveur - fs_f) < 0.03f) { fs_t_conf = t_ms; fs_maj0 = i.maj; fs_refus = 0; }
            else if (t_ms - fs_t > 2500) {
                if (++fs_refus >= 3) { fs_arreter("Ce serveur ne laisse pas changer la fréquence"); return; }
                fs_aller(fs_f);                          // on redemande
            }
            break;
        }
        if (i.maj - fs_maj0 >= 1 && i.sig > fs_sig) fs_sig = i.sig;
        if (i.maj - fs_maj0 < fs_mesures() || t_ms - fs_t_conf < fs_mesure_ms()) break;
        // niveau du bruit : suit vite les baisses, lentement les hausses
        if (fs_bruit == 0) fs_bruit = fs_sig < 20 ? fs_sig : 20;   // depart pres d'une station : pas pris pour du bruit
        else if (fs_sig < fs_bruit) fs_bruit += (fs_sig - fs_bruit) * 0.5f;
        else fs_bruit += (fs_sig - fs_bruit) * 0.03f;
        bool fort = fs_sig >= fs_bruit + fs_seuil();
        if (fs_sauter) { if (!fort) fs_sauter = false; fs_phase = 0; break; }
        if (fort && fs_sig > fs_cand_sig) { fs_cand = fs_f; fs_cand_sig = fs_sig; fs_phase = 0; break; }   // on monte
        if (fs_cand > 0) {                               // le sommet est passe : retour sur la meilleure frequence
            fs_f = fs_cand;
            fm::accorder(fs_cand);
            fs_noter(fs_cand, fs_cand_sig, nullptr);
            fs_cand = 0;
            fs_cand_sig = -99;
            fs_phase = 2;
            fs_t = t_ms;
            break;
        }
        fs_phase = 0;
        break;
    }
    case 2:                                              // ecoute de 5 s, on note le nom RDS
        if (t_ms - fs_t > 1500 && i.ps[0] && fabsf(i.mhz_serveur - fs_f) < 0.03f) fs_noter(fs_f, i.sig, i.ps);
        if (t_ms - fs_t >= SC_ECOUTE_MS) { fs_sauter = true; fs_phase = 0; }
        break;
    }
}

static void fs_dessin()
{
    if (!fs_sens) return;
    char t[64], l2[64];
    bool ec = fs_phase == 2;
    fm::Info i;
    fm::info(&i);
    if (ec) {
        int reste = (int)((SC_ECOUTE_MS - (t_ms - fs_t) + 999) / 1000);
        snprintf(t, sizeof t, "Station trouvée%s%s · reprise dans %d s", i.ps[0] ? " : " : "", i.ps, reste < 1 ? 1 : reste);
        snprintf(l2, sizeof l2, "Touche l'écran pour arrêter");
    } else {
        int pts = (t_ms / 350) % 4;
        snprintf(t, sizeof t, "Recherche %s · vitesse %d/4%.*s", fs_sens > 0 ? "▲" : "▼", fs_vitesse, pts, "...");
        snprintf(l2, sizeof l2, "%s", fs_vitesse < 4 ? "Même bouton : plus vite · ailleurs : arrêt" : "Touche l'écran pour arrêter");
    }
    uint16_t c = ec ? VERT : AMBRE;
    rect_arrondi(8, 106, 304, 40, 8, PAN);
    contour_arrondi(8, 108, 304, 38, 8, 1, c, 220);
    texte(160, 111, tronquer(t, POLICE_13, 296), POLICE_13, c, 255, CENTRE);
    texte(160, 129, l2, POLICE_11, TXT2, 255, CENTRE);
}

static void ecran_fm_trouvees()
{
    rect(0, 0, 320, 240, FOND);
    char d[32];
    snprintf(d, sizeof d, "%d station%s", nfmt, nfmt > 1 ? "s" : "");
    entete("Stations trouvées", d, TXT2);
    decoupe(0, 30, 320, 240);
    int h = 30, max_dec = (nfmt + 1) * h - (240 - 30) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / h);
    fm::Info i;
    fm::info(&i);
    for (int k = premier; k < nfmt && k < premier + 8; k++) {
        int y = 32 + k * h - (int)liste_dec;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (k - premier);
        bool cour = fabsf(i.mhz - fmt[k].mhz) < 0.05f;
        rect_arrondi(4, y, 312, h - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        if (cour) contour_arrondi(4, y, 312, h - 3, 6, 1.2f, BLEU);
        char f[16];
        snprintf(f, sizeof f, "%.1f", fmt[k].mhz);
        for (char *q = f; *q; q++) if (*q == '.') *q = ',';
        texte(12, y + 5, f, POLICE_16, AMBRE);
        texte(90, y + 6, fmt[k].ps[0] ? fmt[k].ps : "(sans RDS)", POLICE_13, fmt[k].ps[0] ? TXT : TXT2);
        char sv[16];
        snprintf(sv, sizeof sv, "%.0f dBf", fmt[k].sig);
        texte(308, y + 8, sv, POLICE_11, TXT2, 255, DROITE);
        zone(4, y, 312, h - 3, B_LISTE + (k - premier), Z_GLISSE);
    }
    int yb = 32 + nfmt * h - (int)liste_dec;
    if (nfmt) bouton(90, yb + 2, 140, 24, "Effacer la liste", B_FMT_EFFACER, 2, POLICE_13);
    sans_decoupe();
    if (!nfmt) {
        texte(160, 100, "Aucune station trouvée", POLICE_16, TXT, 255, CENTRE);
        texte(160, 124, "Lance un scan avec ◀◀ ou ▶▶ sur l'écran Radio FM", POLICE_11, TXT2, 255, CENTRE);
    }
}

static void ecran_fm()
{
    rect(0, 0, 320, 240, FOND);
    fm::Info i;
    fm::info(&i);
    bool on = fm::actif();
    uint16_t ce = !on ? TXT2 : i.etat == fm::ACTIF ? VERT : (i.etat == fm::PERDU ? ROUGE : AMBRE);
    entete(on ? "" : "Radio FM", on ? tronquer(i.message, POLICE_13, 105) : nullptr, ce);
    if (on) texte_defile(DEF_FM_SERVEUR, 38, 5, 165, i.serveur, POLICE_16, TXT, PAN);
    if (!on) {
        texte(160, 40, "Aucun serveur FM", POLICE_22, TXT, 255, CENTRE);
        texte(160, 68, "Tuners FM partagés sur Internet (FM-DX)", POLICE_11, TXT2, 255, CENTRE);
        bouton(70, 84, 180, 34, "Choisir un serveur", B_FM_CHOISIR, 1, POLICE_16, Z_APPUI);
        bouton(70, 124, 180, 30, wr::actif() ? "Webradio en cours  ›" : "Webradios  ›", B_FM_WEB, wr::actif() ? 3 : 0, POLICE_13, Z_APPUI);
    } else {
        // frequence (glisser = 0,1 MHz tous les 12 pixels)
        char f[16];
        if (i.mhz > 0) {
            snprintf(f, sizeof f, "%.2f", i.mhz);
            int l = (int)strlen(f);
            if (l > 1 && f[l - 1] == '0') f[l - 1] = 0;       // 105,10 -> 105,1
            char *pt = strchr(f, '.');
            if (pt) *pt = ',';
        } else {
            snprintf(f, sizeof f, "–––,–");
        }
        int lf = largeur_texte(f, POLICE_40);
        texte(10, 30, f, POLICE_40, AMBRE);
        texte(14 + lf, 50, "MHz", POLICE_16, TXT2);
        zone(0, 28, 230, 50, B_FM_FREQ, Z_GLISSE);
        if (i.stereo) {
            rect_arrondi(250, 34, 62, 18, 5, PUCE);
            texte(281, 36, "STÉRÉO", POLICE_11, PUCE_T, 255, CENTRE);
        }
        if (i.auditeurs > 0) {
            char u[24];
            snprintf(u, sizeof u, "%d auditeur%s", i.auditeurs, i.auditeurs > 1 ? "s" : "");
            texte(312, 58, u, POLICE_11, TXT2, 255, DROITE);
        }
        // RDS
        texte(10, 78, i.ps[0] ? i.ps : (i.rds ? "…" : "pas de RDS"), i.ps[0] ? POLICE_30 : POLICE_13, i.ps[0] ? TXT : TXT2);
        texte_defile(DEF_FM_RT, 10, 113, nfmt ? 234 : 300, i.rt, POLICE_13, TXT, FOND);
        bouton_scan(250, 82, -1, B_FMSCAN_BAS);         // autoscan de la bande FM
        bouton_scan(284, 82, +1, B_FMSCAN_HAUT);
        if (nfmt) {
            char t[16];
            snprintf(t, sizeof t, "Trouvées %d", nfmt);
            bouton(250, 108, 62, 20, t, B_FM_TROUVEES, 0, POLICE_11);
        }
        if (i.tx[0]) {
            char t[96];
            snprintf(t, sizeof t, "%s%s%s%s", i.tx, i.tx_pays[0] ? " (" : "", i.tx_pays, i.tx_pays[0] ? ")" : "");
            if (i.tx_km) { char k[16]; snprintf(k, sizeof k, " · %d km", i.tx_km); strncat(t, k, sizeof t - strlen(t) - 1); }
            texte(10, 131, tronquer(t, POLICE_11, 232), POLICE_11, TXT2);
        }
        bouton(250, 128, 62, 18, "Meilleur", B_FM_MEILLEUR, mfm_phase() ? 3 : 0, POLICE_11, Z_APPUI);
        // signal
        float sg = i.sig / 80.0f;
        if (sg < 0) sg = 0;
        if (sg > 1) sg = 1;
        rect_arrondi(10, 150, 240, 6, 3, LIGNE);
        rect_arrondi(10, 150, (int)(240 * sg) + 2, 6, 3, i.sig > 45 ? VERT : (i.sig > 25 ? JAUNE : ROUGE));
        char sv[16];
        snprintf(sv, sizeof sv, "%.0f dBf", i.sig);
        texte(312, 145, sv, POLICE_11, TXT2, 255, DROITE);
    }
    if (on) fs_dessin();
    fb_dessin();
    // volume
    int y = 162;
    texte(8, y + 6, "Volume", POLICE_13, TXT);
    float t = cfg::R.volume / 100.0f;
    rect_arrondi(86, y + 11, 180, 8, 4, LIGNE);
    rect_arrondi(86, y + 11, (int)(180 * t) + 4, 8, 4, CYAN);
    disque(86 + 180 * t, y + 15, 9, TXT);
    char v[8];
    snprintf(v, sizeof v, "%d", cfg::R.volume);
    texte(314, y + 6, v, POLICE_16, TXT, 255, DROITE);
    zone(70, y, 212, 30, B_AUD_VOL, Z_GLISSE | Z_APPUI);
    reg_curseur(B_AUD_VOL, 86, 180, t, 50);
    // barre du bas
    rect(0, Y_BAS, 320, 240 - Y_BAS, PAN);
    rect(0, Y_BAS, 320, 1, LIGNE);
    int yb = Y_BAS + 3, hb = 240 - Y_BAS - 6;
    gros_bouton(4, yb, 56, hb, "", B_MOINS, 0, -1);
    gros_bouton(64, yb, 56, hb, "", B_PLUS, 0, +1);
    gros_bouton(124, yb, 92, hb, "LISTES", B_FM_SERVEURS, 0);
    if (on) {
        gros_bouton(220, yb, 36, hb, "", B_FM_ARRET, 0);
        rect_arrondi(232, yb + hb / 2 - 6, 12, 12, 2, ROUGE);       // carre "arreter"
    }
    gros_bouton(260, yb, 56, hb, "MENU", B_MENU, 1);
}

// onglets "Serveurs FM | Webradios" en haut des deux listes (on passe de l'une a l'autre meme pendant l'ecoute)
static void onglets_fm_web(bool web)
{
    static const char *N[2] = { "Serveurs FM", "Webradios" };
    int x = 38;
    for (int i = 0; i < 2; i++) {
        int l = largeur_texte(N[i], POLICE_13) + 16;
        bool on = (i == 1) == web;
        rect_arrondi(x, 3, l, 21, 10, on ? BLEU : PAN);
        if (!on) contour_arrondi(x, 3, l, 21, 10, 1, LIGNE);
        texte(x + l / 2, 6, N[i], POLICE_13, on ? SOMBRE : TXT, 255, CENTRE);
        if (i == 1 && !web && wr::actif()) disque(x + l - 6, 7, 3, VERT);        // une webradio joue
        if (i == 0 && web && fm::actif()) disque(x + l - 6, 7, 3, VERT);
        zone(x, 0, l, 26, i ? B_FM_WEB : B_WR_FM);
        x += l + 4;
    }
}

static void ecran_fm_liste()
{
    rect(0, 0, 320, 240, FOND);
    if (t_ms - t_vue_fm > 700) maj_vue_fm();
    entete("");
    onglets_fm_web(false);
    bool charge = plat_fm_liste_en_cours();
    bool ap = tc_appui && tc_z.id == B_ACTUALISER;
    rect_arrondi(286, 3, 28, 20, 4, ap ? melange(BTN, TXT, 60) : BTN);
    float a0 = charge ? t_ms / 150.0f : 0;
    arc(300, 13, 6, 1.6f, a0 + 0.5f, a0 + 5.6f, charge ? CYAN : TXT);
    zone(280, 0, 40, 26, B_ACTUALISER);
    char et[32];
    if (charge) snprintf(et, sizeof et, "Chargement…");
    else if (fml::nb()) snprintf(et, sizeof et, "%d", fml::nb());
    else snprintf(et, sizeof et, "Liste vide");
    texte(278, 7, et, POLICE_11, TXT2, 255, DROITE);

    const int Y0 = 30;
    decoupe(0, Y0, 320, 240);
    int max_dec = nvue_fm * H_LIGNE - (240 - Y0) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    fm::Info inf;
    fm::info(&inf);
    for (int k = premier; k < nvue_fm && k < premier + 9; k++) {
        const fml::ServeurFM *s = vue_fm[k];
        int y = Y0 + k * H_LIGNE - (int)liste_dec;
        bool ap2 = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (k - premier);
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap2 ? melange(PAN, TXT, 40) : PAN);
        if (fm::actif() && !strcmp(s->url, inf.url)) contour_arrondi(4, y, 312, H_LIGNE - 3, 6, 1.2f, BLEU);
        uint16_t c = s->etat == 1 ? VERT : (s->etat == 2 ? AMBRE : TXT2);
        disque(14, y + 14, 4, c, s->etat ? 255 : 120);
        texte(24, y + 2, tronquer(s->nom, POLICE_13, 230), POLICE_13, s->etat ? TXT : TXT2);
        char l2[96];
        if (s->km < 65000) snprintf(l2, sizeof l2, "%s%s%s · %u km", s->ville, s->ville[0] ? ", " : "", s->pays, s->km);
        else snprintf(l2, sizeof l2, "%s%s%s", s->ville, s->ville[0] ? ", " : "", s->pays);
        texte(24, y + 16, tronquer(l2, POLICE_11, 230), POLICE_11, TXT2);
        char d[16];
        if (s->etat == 2) snprintf(d, sizeof d, "écoute");
        else if (s->etat == 0) snprintf(d, sizeof d, "hors ligne");
        else snprintf(d, sizeof d, "%u k", s->kbps);
        texte(308, y + 7, d, POLICE_11, s->etat == 1 ? TXT : TXT2, 255, DROITE);
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (k - premier), Z_GLISSE);
    }
    sans_decoupe();
    if (nvue_fm == 0) {
        const char *t1 = charge ? "Chargement de la liste FM…" : (plat_wifi_etat() != WIFI_OK ? "Pas de connexion Internet" : "Liste FM vide");
        const char *t2 = charge ? "servers.fmdx.org, quelques secondes" : "Touche la flèche en haut à droite";
        texte(160, 110, t1, POLICE_16, TXT, 255, CENTRE);
        texte(160, 134, t2, POLICE_11, TXT2, 255, CENTRE);
        // la raison d'un echec (connexion, memoire, reponse du site) ; un nouvel essai se fait tout seul
        const char *r = fml::etat_liste();
        const char *p = r ? strchr(r, '(') : nullptr;
        if (!charge && p) {
            char t[96];
            snprintf(t, sizeof t, "%s", p + 1);
            char *f = strrchr(t, ')');
            if (f) *f = 0;
            texte(160, 160, "Dernier essai : échec", POLICE_11, AMBRE, 255, CENTRE);
            texte(160, 175, tronquer(t, POLICE_11, 310), POLICE_11, AMBRE, 255, CENTRE);
            texte(160, 194, "Nouvel essai automatique dans 20 s (3 fois)", POLICE_11, TXT2, 255, CENTRE);
        }
    }
}

// ============================ FREQUENCE ======================================
static char saisie[12] = "";
struct Raccourci { const char *nom; double f; int mode; };
static const Raccourci RACC[12] = {
    { "160 m", 1850, cfg::LSB }, { "80 m", 3700, cfg::LSB }, { "40 m", 7150, cfg::LSB }, { "30 m", 10120, cfg::CW },
    { "20 m", 14230, cfg::USB }, { "17 m", 18130, cfg::USB }, { "15 m", 21300, cfg::USB }, { "10 m", 28500, cfg::USB },
    { "GO", 162, cfg::AM }, { "PO", 1008, cfg::AM }, { "49 m", 6070, cfg::AM }, { "31 m", 9650, cfg::AM } };

static int page_profils = 0;
static kw::Profil *profs = nullptr;             // [MAX_PROFILS] en PSRAM
static int nprofs = 0;
static uint32_t v_profs = 0xFFFFFFFF;

static void lire_profils()
{
    if (!profs) profs = (kw::Profil *)plat_malloc_ps(sizeof(kw::Profil) * kw::MAX_PROFILS);
    if (profs && kw::version_profils() != v_profs) {
        v_profs = kw::version_profils();
        nprofs = kw::profils(profs, kw::MAX_PROFILS);
    }
}

// OpenWebRX : bandes du recepteur a la place des raccourcis (12 par page)
static void bandes_owrx()
{
    lire_profils();
    texte(8, 82, nprofs ? "Bandes de ce récepteur" : "Bandes : attente du récepteur…", POLICE_11, TXT2);
    int cour = kw::profil_courant();
    int n0 = page_profils * 9;
    bool pages = nprofs > 10;
    if (n0 >= nprofs) { page_profils = 0; n0 = 0; }
    for (int k = 0; k < 10; k++) {
        int i = n0 + k;
        bool suite = pages && k == 9;
        if (!suite && i >= nprofs) break;
        int x = 6 + (k % 2) * 76, y = 97 + (k / 2) * 23;
        bool ap = tc_appui && tc_z.id == B_BANDE_RAC + k;
        bool on = !suite && i == cour;
        rect_arrondi(x, y, 73, 21, 5, on ? BLEU : (ap ? melange(BTN, TXT, 60) : BTN));
        if (!on) contour_arrondi(x, y, 73, 21, 5, 1, LIGNE);
        const char *t = suite ? "Suite…" : profs[i].nom;
        texte(x + 37, y + 4, tronquer(t, POLICE_11, 68), POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
        zone(x, y, 73, 21, B_BANDE_RAC + k);
    }
    bouton(6, 213, 149, 22, "Canaux VHF ›", B_CANAUX, 1, POLICE_13);
}

static void ecran_freq()
{
    rect(0, 0, 320, 240, FOND);
    bool owrx = cfg::R.proto == cfg::OWRX;
    char plage[40];
    if (owrx && kw::etat() == kw::ACTIF)
        snprintf(plage, sizeof plage, "%.3f – %.3f MHz", kw::bande_min_khz() / 1000.0, kw::bande_max_khz() / 1000.0);
    else snprintf(plage, sizeof plage, "%s", owrx ? "en MHz" : "0 – 30 MHz");
    for (char *q = plage; *q; q++) if (*q == '.') *q = ',';
    entete("Fréquence", plage, TXT2);
    // affichage
    rect_arrondi(6, 32, 150, 44, 7, PAN);
    contour_arrondi(6, 32, 150, 44, 7, 1, LIGNE);
    char s[24];
    if (saisie[0]) {
        // regroupe les milliers de la partie entiere
        char ent[12], dec[8] = "";
        const char *v = strchr(saisie, ',');
        int le = v ? (int)(v - saisie) : (int)strlen(saisie);
        snprintf(ent, sizeof ent, "%.*s", le, saisie);
        if (v) snprintf(dec, sizeof dec, "%s", v);
        if (le > 3) snprintf(s, sizeof s, "%.*s %s%s", le - 3, ent, ent + le - 3, dec);
        else snprintf(s, sizeof s, "%s%s", ent, dec);
    } else if (owrx) {
        fmt_freq_aff(s, sizeof s, cfg::R.freq_khz);
    } else {
        fmt_freq(s, sizeof s, cfg::R.freq_khz);
    }
    int l = largeur_texte(s, POLICE_22);
    if (l > 116) l = 116;
    texte(122 - l, 42, s, POLICE_22, saisie[0] ? AMBRE : TXT2);
    if (saisie[0] && (t_ms / 450) % 2) rect(124, 43, 2, 22, AMBRE);
    texte(150, 50, owrx ? "MHz" : "kHz", POLICE_11, TXT2, 255, DROITE);
    if (owrx) bandes_owrx();
    else texte(8, 82, "Raccourcis bandes", POLICE_11, TXT2);
    for (int i = 0; i < 12 && !owrx; i++) {
        int x = 6 + (i % 3) * 51, y = 98 + (i / 3) * 35;
        bool ap = tc_appui && tc_z.id == B_BANDE_RAC + i;
        rect_arrondi(x, y, 48, 32, 6, ap ? melange(BTN, TXT, 60) : BTN);
        contour_arrondi(x, y, 48, 32, 6, 1, LIGNE);
        texte(x + 24, y + 2, RACC[i].nom, POLICE_13, TXT, 255, CENTRE);
        char f[16];
        fmt_freq(f, sizeof f, mem_rappel(RACC[i].f, 0, 1e9, nullptr), 0);   // la derniere frequence ecoutee dans cette bande
        texte(x + 24, y + 17, f, POLICE_11, TXT2, 255, CENTRE);
        zone(x, y, 48, 32, B_BANDE_RAC + i);
    }
    // pave
    static const char *TOUCHES[15] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ",", "0", "⌫", "OK", "", "" };
    for (int i = 0; i < 12; i++) {
        int x = 163 + (i % 3) * 51, y = 32 + (i / 3) * 41;
        const char *t = TOUCHES[i];
        if (i == 11) {
            bool ap = tc_appui && tc_z.id == B_TOUCHE + i;
            rect_arrondi(x, y, 48, 38, 7, ap ? melange(PAN, TXT, 60) : PAN);
            contour_arrondi(x, y, 48, 38, 7, 1, LIGNE);
            // touche effacer dessinee (la police n'a pas le symbole)
            float cx = x + 24, cy = y + 19;
            trait(cx - 9, cy, cx - 3, cy - 6, 1.6f, TXT); trait(cx - 9, cy, cx - 3, cy + 6, 1.6f, TXT);
            trait(cx - 3, cy - 6, cx + 10, cy - 6, 1.6f, TXT); trait(cx - 3, cy + 6, cx + 10, cy + 6, 1.6f, TXT);
            trait(cx + 10, cy - 6, cx + 10, cy + 6, 1.6f, TXT);
            trait(cx + 1, cy - 3, cx + 6, cy + 3, 1.4f, TXT); trait(cx + 6, cy - 3, cx + 1, cy + 3, 1.4f, TXT);
            zone(x, y, 48, 38, B_TOUCHE + i, Z_LONG);
        } else {
            bool ap = tc_appui && tc_z.id == B_TOUCHE + i;
            rect_arrondi(x, y, 48, 38, 7, ap ? melange(PAN, TXT, 60) : PAN);
            contour_arrondi(x, y, 48, 38, 7, 1, LIGNE);
            texte(x + 24, y + 7, t, POLICE_22, TXT, 255, CENTRE);
            zone(x, y, 48, 38, B_TOUCHE + i);
        }
    }
    bouton(163, 198, 150, 36, "OK", B_TOUCHE + 12, 1, POLICE_16);
}

// ============================ CANAUX VHF ======================================
// bande du recepteur OpenWebRX qui couvre une frequence : plage deja vue, sinon d'apres le nom de la bande
// Plage de chaque bande du recepteur, en kHz : exacte (bande deja visitee, ou status.json du recepteur),
// sinon estimee d'apres son nom ("118-137 MHz", "2m", "Airband", "Marine"...). Refaite quand les bandes changent.
static double prof_a[kw::MAX_PROFILS], prof_b[kw::MAX_PROFILS];
static uint8_t prof_sur[kw::MAX_PROFILS];           // 2 exacte, 1 estimee, 0 inconnue
static uint32_t v_plages = 0xFFFFFFFF;

static void plages_profils()
{
    lire_profils();
    if (kw::version_plages() == v_plages) return;
    v_plages = kw::version_plages();
    struct Motif { const char *m; double a, b; };
    static const Motif MOTS[] = {
        { "air", 108, 137 }, { "avia", 108, 137 }, { "aero", 108, 137 }, { "flug", 108, 137 }, { "luft", 108, 137 },
        { "atc", 108, 137 }, { "marin", 156, 163 }, { "marit", 156, 163 }, { "seefunk", 156, 163 }, { "ship", 156, 163 },
        { "boat", 156, 163 }, { "bateau", 156, 163 }, { "pmr", 446, 446.2 }, { "broadcast", 87.5, 108 }, { "rundfunk", 87.5, 108 },
        { "wfm", 87.5, 108 } };
    for (int i = 0; i < nprofs && i < kw::MAX_PROFILS; i++) {
        prof_sur[i] = 0;
        if (kw::plage_profil(profs[i].id, &prof_a[i], &prof_b[i])) { prof_sur[i] = 2; continue; }
        char t[48];
        snprintf(t, sizeof t, "%s", profs[i].nom);
        double n[4];
        int nn = 0;
        for (char *p = t; *p && nn < 4; ) {
            if (*p >= '0' && *p <= '9') {
                char *fin;
                for (char *q = p; *q && (isdigit((unsigned char)*q) || *q == ',' || *q == '.'); q++) if (*q == ',') *q = '.';
                double v = strtod(p, &fin);
                bool cm = (fin[0] == 'c' && fin[1] == 'm') || (fin[0] == ' ' && fin[1] == 'c' && fin[2] == 'm');
                bool m = !cm && (fin[0] == 'm' || fin[0] == 'M') && !(fin[1] == 'H' || fin[1] == 'h');
                if (cm) v = v == 70 ? 435 : (v == 23 ? 1296 : (v == 13 ? 2320 : 0));   // longueur d'onde -> MHz
                else if (m) v = v == 2 ? 145 : (v == 4 ? 70.25 : (v == 6 ? 51 : (v == 10 ? 29 : 0)));
                if (v > 25) n[nn++] = v;
                p = fin;
            } else p++;
        }
        for (int k = 0; k + 1 < nn && !prof_sur[i]; k++)
            if (n[k] < n[k + 1]) { prof_a[i] = n[k] * 1000; prof_b[i] = n[k + 1] * 1000; prof_sur[i] = 1; }
        if (!prof_sur[i] && nn == 1) { prof_a[i] = (n[0] - 1.5) * 1000; prof_b[i] = (n[0] + 1.5) * 1000; prof_sur[i] = 1; }
        if (prof_sur[i]) continue;
        srv::plier(profs[i].nom, t, sizeof t);
        for (const Motif &m : MOTS)
            if (strstr(t, m.m)) { prof_a[i] = m.a * 1000; prof_b[i] = m.b * 1000; prof_sur[i] = 1; break; }
    }
}

// bande du recepteur OpenWebRX qui couvre une frequence (les plages exactes d'abord) ; -1 si aucune
static int trouver_profil(double khz, bool maj = true)
{
    if (maj) plages_profils();
    for (int sur = 2; sur >= 1; sur--)
        for (int i = 0; i < nprofs && i < kw::MAX_PROFILS; i++)
            if (prof_sur[i] == sur && khz >= prof_a[i] && khz <= prof_b[i]) return i;
    return -1;
}

// 2 : dans la bande en cours ; 1 : dans une autre bande de ce recepteur ; 0 : ce recepteur ne la capte pas
static int canal_ecoutable(double khz, bool maj = true)   // maj = false : depuis le portail web (autre tache), table deja faite
{
    if (cfg::R.proto != cfg::OWRX || kw::etat() != kw::ACTIF) return 0;
    if (khz >= kw::bande_min_khz() && khz <= kw::bande_max_khz()) return 2;
    return trouver_profil(khz, maj) >= 0 ? 1 : 0;
}

static double pend_khz = 0;             // frequence a regler une fois la bande changee
static int pend_mode = 0;
static uint32_t pend_t = 0;

static void regler_canal(double khz, int mode)
{
    char f[32];
    fmt_freq_u(f, sizeof f, khz);
    if (cfg::R.proto != cfg::OWRX) { message("Canaux VHF : connecte-toi à un récepteur VHF/UHF (Récepteurs)"); return; }
    if (sc_sens) scan_arreter();
    cfg::R.pas = (uint8_t)cfg::pas_par_defaut(mode, khz);
    if (kw::etat() == kw::ACTIF && khz >= kw::bande_min_khz() && khz <= kw::bande_max_khz()) {
        cfg::R.mode = (uint8_t)mode;
        accorder(khz);
        aller(E_RADIO);
        return;
    }
    int p = trouver_profil(khz);
    if (p < 0) {
        double mhz = khz / 1000.0;
        const char *quoi = mhz >= 108 && mhz <= 137 ? "l'aviation" : (mhz >= 156 && mhz <= 163 ? "la marine" : (mhz >= 446 && mhz < 446.3 ? "le PMR446" : nullptr));
        char m[96];
        if (quoi) snprintf(m, sizeof m, "Ce récepteur ne capte pas %s : essaie un autre récepteur", quoi);
        else snprintf(m, sizeof m, "Ce récepteur ne capte pas %s", f);
        message(m);
        return;
    }
    kw::choisir_profil(profs[p].id);
    pend_khz = khz;
    pend_mode = mode;
    pend_t = t_ms;
    char m[96];
    snprintf(m, sizeof m, "Bande %s pour %s", profs[p].nom, f);
    message(m);
    aller(E_RADIO);
}

// appele a chaque image : fin du changement de bande, position de reference de la liste
static void canaux_tic()
{
    if (pend_khz > 0) {
        if (kw::etat() == kw::ACTIF && pend_khz >= kw::bande_min_khz() && pend_khz <= kw::bande_max_khz()) {
            cfg::R.mode = (uint8_t)pend_mode;
            cfg::R.pas = (uint8_t)cfg::pas_par_defaut(pend_mode, pend_khz);
            accorder(pend_khz);
            pend_khz = 0;
        } else if (t_ms - pend_t > 10000) {
            message("La bande choisie ne couvre pas cette fréquence");
            pend_khz = 0;
        }
    }
    static uint32_t t_ref = 0;
    if (t_ms - t_ref < 2000) return;
    t_ref = t_ms;
    if (cfg::R.proto == cfg::OWRX) plages_profils();   // table des bandes a jour (ecran et portail web)
    float la = cfg::R.lat, lo = cfg::R.lon;
    if (!kw::position_recepteur(&la, &lo) && cfg::R.proto == cfg::OWRX) {
        static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);   // position donnee par la liste ReceiverBook
        if (!v) return;
        int n = owl::chercher("", v, owl::MAX_VHF);
        for (int i = 0; i < n; i++)
            if (strstr(v[i]->url, cfg::R.hote) && (v[i]->lat != 0 || v[i]->lon != 0)) { la = v[i]->lat; lo = v[i]->lon; break; }
    }
    if (vhf::reference_changee(la, lo)) vhf::reference(la, lo);
}

static int can_cat = 0;                 // 0 tous, 1 aviation, 2 marine, 3 radioamateurs, 4 PMR
static const vhf::Canal **vue_can = nullptr;
static int nvue_can = 0;
static uint32_t t_vue_can = 0;
static int cats_de(int c) { static const int C[5] = { vhf::TOUS, vhf::AVIA, vhf::MARINE, vhf::RADIOAM, vhf::PMR }; return C[c % 5]; }

static bool can_ici = true;             // ne montrer que les canaux que ce recepteur peut capter
static uint8_t *etat_can = nullptr;     // par ligne : 2 bande en cours, 1 autre bande, 0 pas capte
static uint32_t v_can = 0xFFFFFFFF, v_can_pl = 0xFFFFFFFF;
static int n_can_total = 0;

static void maj_vue_can()
{
    if (!vue_can) vue_can = (const vhf::Canal **)plat_malloc_ps(sizeof(void *) * 2000);
    if (!etat_can) etat_can = (uint8_t *)plat_malloc_ps(2000);
    v_can = vhf::version();
    v_can_pl = kw::version_plages();
    t_vue_can = t_ms;
    nvue_can = 0;
    if (!vue_can || !etat_can) return;
    int n = vhf::chercher(cats_de(can_cat), "", vue_can, 2000);
    n_can_total = n;
    bool filtre = can_ici && cfg::R.proto == cfg::OWRX && kw::etat() == kw::ACTIF;
    int k = 0;
    for (int i = 0; i < n; i++) {
        int e = canal_ecoutable(vue_can[i]->khz);
        if (filtre && !e) continue;
        vue_can[k] = vue_can[i];
        etat_can[k++] = (uint8_t)e;
    }
    nvue_can = k;
}

static void ecran_canaux()
{
    rect(0, 0, 320, 240, FOND);
    if (t_ms - t_vue_can > 1500 || v_can != vhf::version() || v_can_pl != kw::version_plages()) maj_vue_can();   // jamais de liste perimee
    entete("Canaux VHF");
    bool owrx = cfg::R.proto == cfg::OWRX && kw::etat() == kw::ACTIF;
    bouton(232, 3, 82, 20, sc_liste ? "Arrêter" : "Scanner", B_CAN_SCAN, sc_liste ? 2 : 1, POLICE_13);
    static const char *N[5] = { "Tous", "Avions", "Marine", "Radioam.", "PMR" };
    int x = 4;
    for (int i = 0; i < 5; i++) {
        int l = largeur_texte(N[i], POLICE_11) + 14;
        bool on = i == can_cat;
        rect_arrondi(x, 31, l, 18, 9, on ? BLEU : PAN);
        if (!on) contour_arrondi(x, 31, l, 18, 9, 1, LIGNE);
        texte(x + l / 2, 33, N[i], POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
        zone(x, 29, l, 22, B_CAN_CAT + i);
        x += l + 4;
    }
    if (owrx) {                                         // filtre : seulement ce que ce recepteur capte
        const char *t = "Ici";
        int l = largeur_texte(t, POLICE_11) + 26;
        rect_arrondi(316 - l, 31, l, 18, 9, can_ici ? VERT : PAN);
        if (!can_ici) contour_arrondi(316 - l, 31, l, 18, 9, 1, LIGNE);
        if (can_ici) { trait(316 - l + 7, 40, 316 - l + 10, 43, 1.6f, SOMBRE); trait(316 - l + 10, 43, 316 - l + 15, 36, 1.6f, SOMBRE); }
        else anneau(316 - l + 11, 40, 4, 1.2f, TXT2);
        texte(316 - l + 19, 33, t, POLICE_11, can_ici ? SOMBRE : TXT);
        zone(316 - l - 2, 29, l + 4, 22, B_CAN_ICI);
    }
    decoupe(0, Y_LISTE, 320, 240);
    int max_dec = nvue_can * H_LIGNE - (240 - Y_LISTE) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    float a = kw::bande_min_khz(), b = kw::bande_max_khz();
    for (int i = premier; i < nvue_can && i < premier + 8; i++) {
        const vhf::Canal *c = vue_can[i];
        int y = Y_LISTE + i * H_LIGNE - (int)liste_dec;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (i - premier);
        bool courant = owrx && fabs(c->khz - cfg::R.freq_khz) < 0.5;
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        if (courant) contour_arrondi(4, y, 312, H_LIGNE - 3, 6, 1.2f, BLEU);
        int ec = etat_can[i];
        disque(12, y + 14, 3.5f, ec == 2 ? VERT : (ec == 1 ? CYAN : TXT2), ec ? 255 : 120);
        char f[16];
        snprintf(f, sizeof f, "%.3f", c->khz / 1000.0);
        for (char *q = f; *q; q++) if (*q == '.') *q = ',';
        texte(20, y + 7, f, POLICE_13, AMBRE);
        texte(84, y + 2, tronquer(c->nom, POLICE_13, 228), POLICE_13, TXT);
        char l2[64];
        if (c->lat != 999) snprintf(l2, sizeof l2, "%s · %u km", c->type, c->km);
        else snprintf(l2, sizeof l2, "%s · %s", vhf::nom_categorie(c->cat), c->type);
        texte(84, y + 16, tronquer(l2, POLICE_11, 228), POLICE_11, TXT2);
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE);
    }
    sans_decoupe();
    if (nvue_can == 0) {
        bool filtre = can_ici && owrx && n_can_total > 0;
        texte(160, 100, filtre ? "Ce récepteur ne capte aucun de ces canaux" : "Aucun canal", filtre ? POLICE_13 : POLICE_16, TXT, 255, CENTRE);
        texte(160, 122, filtre ? "Choisis un autre récepteur VHF/UHF," : "Aucun aérodrome à moins de 200 km du récepteur", POLICE_11, TXT2, 255, CENTRE);
        if (filtre) texte(160, 137, "ou touche « Ici » pour voir toute la liste", POLICE_11, TXT2, 255, CENTRE);
    } else if (!owrx) {
        rect(0, 222, 320, 18, PAN, 235);
        texte(160, 224, "Pour écouter : Récepteurs > VHF/UHF", POLICE_11, AMBRE, 255, CENTRE);
    }
}

// ============================ QUOI ÉCOUTER ====================================
// Suggestions selon l'heure, la saison et la position du recepteur (quoi.cpp), puis, sur demande, mesure
// de l'activite reelle : le waterfall du KiwiSDR visite chaque bande (le son n'est pas touche) ; sur un
// recepteur OpenWebRX, c'est le spectre de la bande en cours (changer de bande derangerait les autres auditeurs).
struct QLigne { quoi::Sugg s; uint8_t dispo; uint8_t mesure; int16_t sig; float db; double f_fort; };
static QLigne *ql = nullptr;                    // [quoi::MAX_SUGG] en PSRAM
static int nq = 0, q_cat = quoi::OC, q_dispo = 0;
static uint32_t t_q = 0, v_q_vhf = 0xFFFFFFFF, v_q_pl = 0xFFFFFFFF;
static bool q_rx = false;                       // position : celle du recepteur (sinon celle de la carte)
// resultats de mesure, gardes 15 minutes pour le meme recepteur
struct QMes { double a, b; int16_t sig; float db; double f_fort; };
static QMes q_mes[quoi::MAX_SUGG];
static int nq_mes = 0;
static uint32_t q_mes_t = 0, q_mes_gen = 0;
static const uint32_t Q_MES_VALIDE_MS = 15u * 60u * 1000u;
// mesure en cours (KiwiSDR)
static bool qm_web = false;                     // mesure demandee par la page web : continue quel que soit l'ecran
static int qm_phase = 0, qm_i = -1, qm_fait = 0, qm_total = 0;   // phase : 0 arret, 1 attente de la vue, 2 lignes du waterfall
static uint32_t qm_t = 0, qm_n0 = 0;
static const float Q_SEUIL_DB = 12;
static const int Y_QUOI = 68;

static bool q_mes_valide() { return nq_mes > 0 && q_mes_gen == kw::generation() && t_ms - q_mes_t < Q_MES_VALIDE_MS; }

// position du recepteur ecoute : annoncee par lui (OpenWebRX) ou lue dans les listes ; sinon celle de la carte
static void q_contexte()
{
    static uint32_t gen = 0xFFFFFFFF, t = 0;
    static float la = 0, lo = 0;
    static bool rx = false;
    q_ctx.epoch = plat_epoch();
    if (gen != kw::generation() || t_ms - t > 60000 || (!rx && t_ms - t > 5000)) {    // la recherche dans les listes n'est pas refaite sans cesse
        gen = kw::generation();
        t = t_ms;
        la = cfg::R.lat; lo = cfg::R.lon; rx = false;
        if (kw::etat() != kw::ARRET && cfg::R.hote[0]) {
            if (cfg::R.proto == cfg::OWRX) {
                if (kw::position_recepteur(&la, &lo)) rx = true;
                else {
                    static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
                    int n = v ? owl::chercher("", v, owl::MAX_VHF) : 0;
                    for (int i = 0; i < n; i++)
                        if (strstr(v[i]->url, cfg::R.hote) && (v[i]->lat != 0 || v[i]->lon != 0)) { la = v[i]->lat; lo = v[i]->lon; rx = true; break; }
                }
            } else {
                const srv::Serveur *v[8];
                int n = srv::chercher(cfg::R.hote, v, 8);
                for (int i = 0; i < n; i++)
                    if (!strcmp(v[i]->hote, cfg::R.hote) && (v[i]->lat != 0 || v[i]->lon != 0)) { la = v[i]->lat; lo = v[i]->lon; rx = true; break; }
            }
        }
    }
    q_ctx.lat = la; q_ctx.lon = lo;
    q_rx = rx;
}

// 2 : ecoutable tout de suite ; 1 : dans une autre bande de ce recepteur (OpenWebRX) ; 0 : pas capte
static int q_ecoutable(quoi::Sugg &s, bool maj = true)
{
    if (kw::etat() != kw::ACTIF) return 0;
    float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
    if (cfg::R.proto != cfg::OWRX) {
        if (s.mode == cfg::WFM) return 0;
        return s.f >= bmin && s.f <= bmax ? 2 : 0;
    }
    int e = canal_ecoutable(s.f, maj);
    if (e) return e;
    if (s.a < s.b) {                                    // la frequence habituelle n'est pas couverte : une autre de la plage ?
        static const float T[3] = { 0.5f, 0.25f, 0.75f };
        for (float t : T) {
            double f = s.a + (s.b - s.a) * t;
            int e2 = canal_ecoutable(f, maj);
            if (e2) { s.f = f; return e2; }
        }
    }
    return 0;
}

static int q_niveau(const QLigne &l)
{
    if (!l.mesure) return l.s.niveau;
    if (l.s.a == l.s.b) return l.sig > 0 ? (l.db >= 25 ? 5 : 4) : 0;
    return quoi::niveau_mesure(l.sig);
}

static void q_trier()
{
    std::stable_sort(ql, ql + nq, [](const QLigne &x, const QLigne &y) {
        if ((x.dispo > 0) != (y.dispo > 0)) return x.dispo > 0;
        return q_niveau(x) > q_niveau(y);
    });
}

static void q_maj()
{
    if (!ql) ql = (QLigne *)plat_malloc_ps(sizeof(QLigne) * quoi::MAX_SUGG);
    if (!ql) return;
    if (qm_phase) return;                               // pas de nouvelle liste pendant la mesure
    t_q = t_ms;
    v_q_vhf = vhf::version();
    v_q_pl = kw::version_plages();
    q_contexte();
    static quoi::Sugg *tmp = (quoi::Sugg *)plat_malloc_ps(sizeof(quoi::Sugg) * quoi::MAX_SUGG);
    if (!tmp) return;
    int n = quoi::proposer(q_ctx, q_cat, tmp, quoi::MAX_SUGG);
    bool mes = q_mes_valide();
    if (!mes) nq_mes = 0;
    q_dispo = 0;
    for (int i = 0; i < n; i++) {
        QLigne &l = ql[i];
        l.s = tmp[i];
        l.dispo = (uint8_t)q_ecoutable(l.s);
        if (l.dispo) q_dispo++;
        l.mesure = 0; l.sig = 0; l.db = 0; l.f_fort = 0;
        for (int k = 0; mes && k < nq_mes; k++)
            if (q_mes[k].a == l.s.a && q_mes[k].b == l.s.b) { l.mesure = 1; l.sig = q_mes[k].sig; l.db = q_mes[k].db; l.f_fort = q_mes[k].f_fort; break; }
    }
    nq = n;
    q_trier();
}

static void q_noter(QLigne &l, int sig, float db, double f_fort)
{
    l.mesure = 1; l.sig = (int16_t)sig; l.db = db; l.f_fort = f_fort;
    for (int k = 0; k < nq_mes; k++)
        if (q_mes[k].a == l.s.a && q_mes[k].b == l.s.b) { q_mes[k].sig = (int16_t)sig; q_mes[k].db = db; q_mes[k].f_fort = f_fort; return; }
    if (nq_mes < quoi::MAX_SUGG) { q_mes[nq_mes].a = l.s.a; q_mes[nq_mes].b = l.s.b; q_mes[nq_mes].sig = (int16_t)sig; q_mes[nq_mes].db = db; q_mes[nq_mes].f_fort = f_fort; nq_mes++; }
}

// zoom du KiwiSDR qui montre toute la plage (une frequence seule : une trentaine de kHz autour)
static int q_zoom(const quoi::Sugg &s)
{
    float bw = kw::bande_max_khz() - kw::bande_min_khz();
    float besoin = s.a < s.b ? (float)(s.b - s.a) * 1.1f : 20.0f;
    int z = 0;
    while (z < 14 && bw / (float)(1 << (z + 1)) >= besoin) z++;
    return z;
}

static void q_mesure_fin(const char *pourquoi)
{
    if (!qm_phase) return;
    qm_phase = 0;
    qm_i = -1;
    kw::cascade_liberer();
    wf::effacer();
    span_aff = -1;                                      // l'ecran Radio repart d'un waterfall propre
    if (qm_fait > 0) { q_mes_t = t_ms; q_mes_gen = kw::generation(); }
    q_trier();
    liste_dec = 0; liste_v = 0;
    if (pourquoi) message(pourquoi);
}

static bool q_suivant()
{
    for (int i = qm_i + 1; i < nq; i++)
        if (ql[i].dispo == 2 && !ql[i].mesure) {
            qm_i = i;
            const quoi::Sugg &s = ql[i].s;
            kw::cascade_forcer((float)((s.a + s.b) / 2), q_zoom(s));
            qm_phase = 1;
            qm_t = t_ms;
            return true;
        }
    return false;
}

static void q_mesurer(bool web = false)
{
    if (qm_phase) { q_mesure_fin("Mesure arrêtée"); return; }
    qm_web = web;
    if (kw::etat() != kw::ACTIF) { message("Connecte-toi d'abord à un récepteur"); return; }
    q_maj();
    if (!ql) return;
    if (sc_sens) scan_arreter();
    if (cfg::R.proto != cfg::OWRX || !q_mes_valide()) {   // OpenWebRX : les mesures des autres bandes restent
        nq_mes = 0;
        for (int i = 0; i < nq; i++) ql[i].mesure = 0;
    }
    if (cfg::R.proto == cfg::OWRX) {                    // tout de suite : le spectre de la bande en cours
        const float *db = nullptr;
        uint32_t cpt = 0;
        int n = kw::spectre_bande(&db, &cpt);
        if (n <= 0 || !db) { message("Spectre pas encore reçu : réessaie"); return; }
        float bmin = kw::bande_min_khz(), bmax = kw::bande_max_khz();
        int faits = 0, ailleurs = 0;
        for (int i = 0; i < nq; i++) {
            if (ql[i].dispo == 1) ailleurs++;
            if (ql[i].dispo != 2) continue;
            double ff = 0; float au = 0;
            int sig = quoi::compter(db, n, bmin, (bmax - bmin) / n, ql[i].s.a, ql[i].s.b, Q_SEUIL_DB, ql[i].s.larg, &ff, &au);
            if (sig < 0) continue;
            q_noter(ql[i], sig, au, ff);
            faits++;
        }
        if (!faits) { message("Rien à mesurer dans la bande en cours"); return; }
        q_mes_t = t_ms; q_mes_gen = kw::generation();
        q_trier();
        liste_dec = 0; liste_v = 0;
        message(ailleurs ? "Mesuré : bande en cours seulement" : "Mesure faite");
        return;
    }
    if (kw::sans_cascade()) { message("Pas de waterfall : mesure impossible"); return; }
    qm_total = 0;
    for (int i = 0; i < nq; i++) if (ql[i].dispo == 2) qm_total++;
    if (!qm_total) { message("Rien à mesurer ici"); return; }
    qm_fait = 0;
    qm_i = -1;
    kw::pause_etiquettes(true);
    if (web && ecran != E_QUOI) message("Mesure des bandes…");
    q_suivant();
}

// a chaque image : avance la mesure (KiwiSDR)
static void quoi_tic()
{
    static uint32_t t_ctx = 0;
    if (t_ms - t_ctx > 2000 || !t_ctx) { t_ctx = t_ms ? t_ms : 1; if (!qm_phase) q_contexte(); }   // aussi pour la page web
    if (!qm_phase) return;
    if (kw::etat() != kw::ACTIF || !ql) { q_mesure_fin("Mesure arrêtée : déconnecté"); return; }
    if (ecran != E_QUOI && !qm_web) { q_mesure_fin(nullptr); return; }     // on a quitte l'ecran
    uint32_t age = t_ms - qm_t;
    if (qm_phase == 1) {
        if (age < 500) return;                          // le temps que la nouvelle vue arrive
        wf::effacer();
        qm_n0 = wf::compteur();
        qm_phase = 2;
        qm_t = t_ms;
        return;
    }
    bool pret = wf::compteur() - qm_n0 >= 4;
    if (!pret && age < 4000) return;
    if (pret && qm_i >= 0 && qm_i < nq) {
        QLigne &l = ql[qm_i];
        float kpp = khz_par_px();
        double f0 = kw::cascade_centre_khz() - kw::cascade_largeur_khz() / 2;
        double ff = 0; float au = 0;
        int sig = quoi::compter(wf::spectre(), wf::L, f0, kpp, l.s.a, l.s.b, Q_SEUIL_DB, l.s.larg, &ff, &au);
        if (sig >= 0) { q_noter(l, sig, au, ff); qm_fait++; }
    }
    if (!q_suivant()) {
        q_mesure_fin(qm_fait ? "Mesure faite" : "Pas de waterfall : mesure impossible");
    }
}

// ---- redirection : ce recepteur ne capte pas la suggestion -> le recepteur adapte le plus proche, puis la frequence ----
static struct {
    bool actif;
    quoi::Sugg s;
    double f;
    int essais;
    uint32_t t, t_actif, gen;
    char quitte[64];                // recepteur qu'on quitte (il ne capte pas la bande)
    char vus[5][64];                // recepteurs deja essayes (hote ou adresse)
    int nvus;
} qr;

static bool qr_deja(const char *h) { for (int i = 0; i < qr.nvus; i++) if (!strcmp(qr.vus[i], h)) return true; return false; }
static void qr_noter(const char *h) { if (qr.nvus < 5) snprintf(qr.vus[qr.nvus++], 64, "%.63s", h); }

static void q_regler_kiwi(const quoi::Sugg &s, double f)
{
    if (sc_sens) scan_arreter();
    cfg::R.mode = s.mode;
    cfg::R.pas = (uint8_t)cfg::pas_par_defaut(s.mode, f);
    if (s.a < s.b) { int z = q_zoom(s); cfg::R.zoom = (uint8_t)(z > 0 ? z : 0); }   // toute la bande a l'ecran
    accorder(f);
    kw::regler_cascade();
}

// connexion au prochain recepteur adapte, du plus proche au plus lointain ; false s'il n'y en a plus
static bool qr_suivant()
{
    float la = q_ctx.lat, lo = q_ctx.lon;
    if (la == 0 && lo == 0) { la = cfg::R.lat; lo = cfg::R.lon; }
    char m[96];
    if (qr.s.cat == quoi::OC) {
        static const srv::Serveur **v = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
        if (!v) return false;
        const srv::Serveur *choix = nullptr;
        uint16_t km_choix = 65535;
        for (int r = 0; r < srv::NB_REGIONS; r++) {
            if (r == srv::PROCHES) continue;                // les autres regions couvrent toute la liste
            int n = srv::filtrer(r, v, srv::MAX_PUBLICS + srv::MAX_PERSO);
            for (int i = 0; i < n; i++) {
                if (!v[i]->perso && v[i]->umax && v[i]->users >= v[i]->umax) continue;      // plein
                if (v[i]->lat == 0 && v[i]->lon == 0) continue;
                if (qr_deja(v[i]->hote)) continue;
                uint16_t km = srv::distance_km(la, lo, v[i]->lat, v[i]->lon);
                if (km < km_choix) { km_choix = km; choix = v[i]; }
            }
        }
        if (!choix) return false;
        qr_noter(choix->hote);
        snprintf(m, sizeof m, "Connexion à %s", choix->nom);
        message(m);
        connecter(choix->hote, choix->port, choix->mdp, choix->nom);
    } else {
        static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
        if (!v) return false;
        int n = owl::chercher("", v, owl::MAX_VHF);
        uint8_t bit = qr.s.cat == quoi::UHF ? 2 : 1;
        const owl::RecepteurVHF *choix = nullptr;
        uint16_t km_choix = 65535;
        for (int i = 0; i < n; i++) {
            if (!(v[i]->bandes & bit) || qr_deja(v[i]->url)) continue;
            if (qr.quitte[0] && strstr(v[i]->url, qr.quitte)) continue;
            if (v[i]->lat == 0 && v[i]->lon == 0) continue;
            uint16_t km = srv::distance_km(la, lo, v[i]->lat, v[i]->lon);
            if (km < km_choix) { km_choix = km; choix = v[i]; }
        }
        if (!choix) return false;
        qr_noter(choix->url);
        snprintf(m, sizeof m, "Connexion à %s", choix->nom);
        message(m);
        if (!connecter_url(choix->url, choix->nom)) return qr_suivant();
    }
    qr.essais++;
    qr.t = t_ms;
    qr.t_actif = 0;
    qr.gen = kw::generation();
    return true;
}

static void q_rediriger(const quoi::Sugg &s, double f)
{
    memset(&qr, 0, sizeof qr);
    qr.s = s;
    qr.f = f;
    if (s.cat == quoi::OC ? srv::nb_publics() + srv::nb_perso() == 0 : owl::nb() == 0) {
        message(s.cat == quoi::OC ? "Liste des KiwiSDR vide : ouvre Récepteurs" : "Liste VHF/UHF vide : ouvre Récepteurs");
        return;
    }
    if (kw::etat() != kw::ARRET && cfg::R.hote[0]) { snprintf(qr.quitte, sizeof qr.quitte, "%s", cfg::R.hote); qr_noter(cfg::R.hote); }   // pas celui qu'on quitte
    qr.actif = true;
    if (!qr_suivant()) { qr.actif = false; message("Aucun récepteur adapté dans la liste"); return; }
    aller(E_RADIO);
}

// a chaque image : suite de la redirection (recepteur connecte ? capte-t-il la bande ? sinon le suivant)
static void qr_tic()
{
    if (!qr.actif) return;
    kw::Etat e = kw::etat();
    bool echec = e == kw::PERDU || e == kw::OCCUPE || e == kw::REFUS || e == kw::PANNE || (e != kw::ACTIF && t_ms - qr.t > 25000);
    if (e == kw::ARRET) { qr.actif = false; return; }               // l'utilisateur a coupe
    if (e == kw::ACTIF) {
        if (!qr.t_actif) qr.t_actif = t_ms ? t_ms : 1;
        if (cfg::R.proto != cfg::OWRX) {                            // KiwiSDR : toutes les ondes courtes
            q_regler_kiwi(qr.s, qr.f);
            qr.actif = false;
            return;
        }
        quoi::Sugg s = qr.s;
        s.f = qr.f;
        if (q_ecoutable(s)) { qr.actif = false; regler_canal(s.f, s.mode); return; }
        if (t_ms - qr.t_actif < 8000) return;                       // ses bandes arrivent (status.json)
        echec = true;                                               // ce recepteur ne capte pas cette bande
    }
    if (!echec) return;
    rp_ecoutait = false;                                            // pas de reprise sur un essai rate
    if (qr.essais >= 4 || !qr_suivant()) {
        qr.actif = false;
        message(qr.s.cat == quoi::OC ? "Aucun KiwiSDR libre trouvé" : "Aucun récepteur proche ne capte cette bande");
    }
}

// ============================ CARTE DU MONDE ==================================
// Les recepteurs sur une carte, avec la zone de nuit a l'heure qu'il est.
//  - zoom : le bouton rotatif KY-040 (un cran = un niveau) ou les boutons + et - a l'ecran ; 7 niveaux, du monde
//    entier (360 degres de large) a x64 (5,6 degres, une region) ;
//  - deplacement : on fait glisser la carte du doigt ;
//  - choix : un toucher bref prend le recepteur le plus proche du doigt ; s'il n'y en a pas, la vue zoome a cet endroit ;
//  - ecoute : bouton Ecouter, ou appui sur le bouton rotatif.
static int carte_niv = 0;                               // 0 : monde ; n : zoom 2^n
static bool carte_vhf = false;
static bool carte_ft8 = false;                        // carte ouverte depuis FT8 : les stations entendues dessus
static char carte_ft8_ind[14] = "";                   // station FT8 touchee (son indicatif : la liste avance toutes les 15 s)
static int carte_ft8_sel_i()
{
    if (!carte_ft8_ind[0]) return -1;
    for (int i = 0; i < f8::nb(); i++) if (f8::message(i).loc[0] && !strcmp(f8::message(i).indicatif, carte_ft8_ind)) return i;
    return -1;
}
static float carte_clon = 5, carte_clat = 30;           // centre de la vue
static bool carte_placee = false;                       // la vue a deja ete centree sur ma position
static const srv::Serveur *carte_k = nullptr;           // recepteur choisi
static const owl::RecepteurVHF *carte_v = nullptr;
static const int Y_CARTE = 28, H_CARTE = 160, CARTE_NIV_MAX = 6;
// les recepteurs de la liste affichee (relus une fois par seconde, pas a chaque image)
struct CartePt { float lat, lon; uint16_t c; uint8_t plein; const void *p; };
static CartePt *carte_pts = nullptr;
static int carte_npts = 0;
static uint32_t carte_t_pts = 0;
static bool carte_pts_vhf = false;

static void carte_vue(float *lon0, float *lat_haut, float *span)
{
    float sp = 360.0f / (float)(1 << carte_niv);
    float lo = carte_clon - sp / 2, la = carte_clat + sp / 4;
    if (lo < -180) lo = -180;
    if (lo > 180 - sp) lo = 180 - sp;
    if (la > 90) la = 90;
    if (la < -90 + sp / 2) la = -90 + sp / 2;
    if (carte_niv) {                                    // le centre reste dans la carte (en vue Monde, on garde l'endroit vise pour le prochain zoom)
        carte_clon = lo + sp / 2;
        carte_clat = la - sp / 4;
    }
    *lon0 = lo; *lat_haut = la; *span = sp;
}

static void carte_points()
{
    const int MAXP = srv::MAX_PUBLICS + srv::MAX_PERSO;
    if (!carte_pts) carte_pts = (CartePt *)plat_malloc_ps(sizeof(CartePt) * MAXP);
    if (!carte_pts) return;
    if (carte_t_pts && t_ms - carte_t_pts < 1000 && carte_pts_vhf == carte_vhf) return;
    carte_t_pts = t_ms ? t_ms : 1;
    carte_pts_vhf = carte_vhf;
    carte_npts = 0;
    if (!carte_vhf) {
        static const srv::Serveur **v = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * MAXP);
        for (int reg = 0; reg < srv::NB_REGIONS && v; reg++) {
            if (reg == srv::PROCHES) continue;
            int n = srv::filtrer(reg, v, MAXP);
            for (int i = 0; i < n && carte_npts < MAXP; i++) {
                if (v[i]->lat == 0 && v[i]->lon == 0) continue;
                bool plein = !v[i]->perso && v[i]->umax && v[i]->users >= v[i]->umax;
                carte_pts[carte_npts++] = CartePt{ v[i]->lat, v[i]->lon, plein ? rgb(120, 126, 140) : VERT, (uint8_t)plein, v[i] };
            }
        }
    } else {
        static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
        int n = v ? owl::chercher("", v, owl::MAX_VHF) : 0;
        for (int i = 0; i < n && carte_npts < MAXP; i++) {
            if (v[i]->lat == 0 && v[i]->lon == 0) continue;
            carte_pts[carte_npts++] = CartePt{ v[i]->lat, v[i]->lon, CYAN, 0, v[i] };
        }
    }
}

// position du soleil : declinaison (rad) et longitude du point ou il est midi (deg)
static bool carte_soleil(float *decl, float *lon_midi)
{
    uint32_t ep = plat_epoch();
    if (!ep) return false;
    uint32_t j = (ep / 86400u) % 1461u;                 // cycle de 4 ans : le jour de l'annee a un jour pres
    float jour = fmodf((float)j, 365.25f);
    *decl = -23.44f * cosf(6.2831853f * (jour + 10) / 365.25f) * 3.14159265f / 180;
    float h = (ep % 86400u) / 3600.0f;
    *lon_midi = (12.0f - h) * 15.0f;
    return true;
}

// > 0 : jour ; < 0 : nuit (sinus de la hauteur du soleil)
static float carte_lumiere(float lat, float lon, float decl, float lon_midi)
{
    float la = lat * 3.14159265f / 180, an = (lon - lon_midi) * 3.14159265f / 180;
    return sinf(la) * sinf(decl) + cosf(la) * cosf(decl) * cosf(an);
}

// zoom d'un niveau ; autour du recepteur choisi s'il y en a un, sinon du centre de l'ecran
static void carte_zoomer(int d)
{
    int n = carte_niv + d;
    if (n < 0) n = 0;
    if (n > CARTE_NIV_MAX) n = CARTE_NIV_MAX;
    if (n == carte_niv) return;
    if (d > 0 && (carte_k || carte_v)) {
        carte_clon = carte_k ? carte_k->lon : carte_v->lon;
        carte_clat = carte_k ? carte_k->lat : carte_v->lat;
    }
    carte_niv = n;
}

static void carte_ecouter()
{
    if (carte_k) connecter(carte_k->hote, carte_k->port, carte_k->mdp, carte_k->nom);
    else if (carte_v) connecter_url(carte_v->url, carte_v->nom);
    else { message("Touche d'abord un récepteur sur la carte"); return; }
    aller(E_RADIO);
}

static void ecran_carte()
{
    rect(0, 0, 320, 240, FOND);
    entete("Carte");
    {   // listes affichees, niveau de zoom
        static const char *N[2] = { "KiwiSDR", "VHF/UHF" };
        int x = 96;
        for (int i = 0; i < 2; i++) {
            int l = largeur_texte(N[i], POLICE_11) + 14;
            bool on = (i == 1) == carte_vhf;
            rect_arrondi(x, 4, l, 19, 9, on ? BLEU : PAN);
            if (!on) contour_arrondi(x, 4, l, 19, 9, 1, LIGNE);
            texte(x + l / 2, 7, N[i], POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
            zone(x, 0, l, 26, B_CARTE_KIWI + i);
            x += l + 3;
        }
        if (carte_niv) {
            char z[12];
            snprintf(z, sizeof z, "×%d", 1 << carte_niv);
            texte(250, 8, z, POLICE_11, AMBRE, 255, DROITE);
            bouton(256, 3, 60, 20, "Monde", B_CARTE_MONDE, 0, POLICE_11);
        }
    }
    float lon0, lat_haut, span;
    carte_vue(&lon0, &lat_haut, &span);
    float dpp = span / 320.0f;                          // degres par pixel
    float decl = 0, lon_midi = 0;
    bool heure = carte_soleil(&decl, &lon_midi);
    // fond : terre / mer, jour / crepuscule / nuit, dessine par segments de meme couleur
    static const uint16_t MER[3] = { rgb(4, 10, 24), rgb(10, 26, 52), rgb(16, 44, 84) };
    static const uint16_t TERRE[3] = { rgb(38, 52, 50), rgb(46, 72, 54), rgb(62, 100, 66) };
    static float ccol[320];
    static int16_t mcol[320];                           // colonne de la carte pour chaque colonne de l'ecran
    const float ppd = CARTE_L / 360.0f;                 // points de la carte par degre
    for (int x = 0; x < 320; x++) {
        ccol[x] = cosf((lon0 + (x + 0.5f) * dpp - lon_midi) * 3.14159265f / 180);
        int mx = (int)((lon0 + (x + 0.5f) * dpp + 180) * ppd);
        mcol[x] = (int16_t)(mx < 0 ? 0 : (mx >= CARTE_L ? CARTE_L - 1 : mx));
    }
    for (int y = 0; y < H_CARTE; y++) {
        float lat = lat_haut - (y + 0.5f) * dpp;
        int my = (int)((90 - lat) * ppd);
        if (my < 0) my = 0;
        if (my >= CARTE_H) my = CARTE_H - 1;
        const uint8_t *ligne = CARTE_TERRE + my * (CARTE_L / 8);
        float la = lat * 3.14159265f / 180, a = sinf(la) * sinf(decl), b = cosf(la) * cosf(decl);
        int debut = 0;
        uint16_t c_prec = 0;
        for (int x = 0; x <= 320; x++) {
            uint16_t c = 0;
            if (x < 320) {
                int mx = mcol[x];
                bool terre = (ligne[mx >> 3] >> (7 - (mx & 7))) & 1;
                float s = heure ? a + b * ccol[x] : 1;
                int niv = s > 0.03f ? 2 : (s > -0.12f ? 1 : 0);
                c = terre ? TERRE[niv] : MER[niv];
            }
            if (x == 0) c_prec = c;
            if (x == 320 || c != c_prec) { rect(debut, Y_CARTE + y, x - debut, 1, c_prec); debut = x; c_prec = c; }
        }
    }
    decoupe(0, Y_CARTE, 320, Y_CARTE + H_CARTE);
    auto vers_ecran = [&](float lat, float lon, float *x, float *y) {
        *x = (lon - lon0) / dpp;
        *y = Y_CARTE + (lat_haut - lat) / dpp;
        return *x >= -6 && *x < 326 && *y >= Y_CARTE - 6 && *y < Y_CARTE + H_CARTE + 6;
    };
    float px, py;
    if (heure && vers_ecran(decl * 180 / 3.14159265f, lon_midi < -180 ? lon_midi + 360 : lon_midi, &px, &py)) {   // le soleil
        halo(px, py, 9, JAUNE, 70);
        disque(px, py, 3.5f, JAUNE);
    }
    // les recepteurs : plus gros quand on zoome
    carte_points();
    static const float RAYON[CARTE_NIV_MAX + 1] = { 0, 1.8f, 2.4f, 3.0f, 3.8f, 4.6f, 5.2f };
    int visibles = 0;
    for (int i = 0; i < carte_npts && carte_pts && !carte_ft8; i++) {
        const CartePt &q = carte_pts[i];
        if (!vers_ecran(q.lat, q.lon, &px, &py)) continue;
        visibles++;
        if (carte_niv == 0) rect((int)px - 1, (int)py - 1, 2, 2, q.c);       // vue Monde : un petit carre, plus rapide
        else {
            if (carte_niv >= 3) disque(px, py, RAYON[carte_niv] + 1.2f, rgb(0, 0, 0), 150);     // lisere : les points se detachent
            disque(px, py, RAYON[carte_niv], q.c);
        }
    }
    // peu de recepteurs a l'ecran : leurs noms (sans se chevaucher)
    if (carte_niv >= 2 && visibles > 0 && visibles <= 14 && !carte_ft8) {
        int ey[14], ex0[14], ex1[14], ne = 0;
        for (int i = 0; i < carte_npts && carte_pts; i++) {
            const CartePt &q = carte_pts[i];
            if (!vers_ecran(q.lat, q.lon, &px, &py)) continue;
            const char *nom = carte_vhf ? ((const owl::RecepteurVHF *)q.p)->nom : ((const srv::Serveur *)q.p)->nom;
            const char *t = tronquer(nom, POLICE_11, 96);
            int l = largeur_texte(t, POLICE_11), x = (int)px + 8, y = (int)py - 7;
            int droite = y < Y_CARTE + 80 ? 278 : 316;  // en haut a droite : les boutons de zoom
            if (x + l > droite) x = (int)px - 8 - l;    // pres du bord droit : a gauche du point
            if (x < 2 || y < Y_CARTE + 1 || y > Y_CARTE + H_CARTE - 13) continue;
            bool gene = false;
            for (int k = 0; k < ne && !gene; k++) gene = abs(ey[k] - y) < 12 && x < ex1[k] + 4 && x + l > ex0[k] - 4;
            if (gene || ne >= 14) continue;
            ey[ne] = y; ex0[ne] = x; ex1[ne] = x + l; ne++;
            rect_arrondi(x - 3, y - 1, l + 6, 14, 4, rgb(0, 0, 0), 150);
            texte(x, y, t, POLICE_11, q.plein ? TXT2 : TXT);
        }
    }
    if (carte_niv == 0 && vers_ecran(carte_clat, carte_clon, &px, &py)) {      // cadre : ce que montrera le prochain cran de zoom
        float l = 160, h = 80;
        float cx = px < l / 2 ? l / 2 : (px > 320 - l / 2 ? 320 - l / 2 : px);
        float cy = py < Y_CARTE + h / 2 ? Y_CARTE + h / 2 : (py > Y_CARTE + H_CARTE - h / 2 ? Y_CARTE + H_CARTE - h / 2 : py);
        contour_arrondi((int)(cx - l / 2), (int)(cy - h / 2), (int)l, (int)h, 4, 1, TXT, 90);
    }
    // FT8 : les stations entendues (30 dernieres minutes), en jaune ; les appels generaux (CQ) en vert
    if (carte_ft8) {
        uint32_t maintenant = plat_epoch();
        int noms = 0;
        for (int i = 0; i < f8::nb(); i++) {
            const f8::Message &m = f8::message(i);
            if (!m.loc[0] || (maintenant && maintenant - m.periode > 1800)) continue;
            bool deja = false;                          // une seule fois par station (la plus recente)
            for (int k = 0; k < i && !deja; k++) deja = !strcmp(f8::message(k).indicatif, m.indicatif) && f8::message(k).loc[0];
            if (deja || !vers_ecran(m.lat, m.lon, &px, &py)) continue;
            uint16_t c = m.cq ? VERT : JAUNE;
            rect((int)px - 3, (int)py - 3, 7, 7, rgb(0, 0, 0), 160);
            rect((int)px - 2, (int)py - 2, 5, 5, c);
            if (carte_niv >= 1 && noms < 25) {
                int l = largeur_texte(m.indicatif, POLICE_11);
                if (px + 6 + l < 278 && py > Y_CARTE + 6) { texte((int)px + 6, (int)py - 7, m.indicatif, POLICE_11, c, 230); noms++; }
            }
        }
        if (carte_ft8_sel_i() >= 0) {
            const f8::Message &m = f8::message(carte_ft8_sel_i());
            if (vers_ecran(m.lat, m.lon, &px, &py)) anneau(px, py, 7, 2, TXT);
        }
        if (kw::etat() == kw::ACTIF && q_rx && vers_ecran(q_ctx.lat, q_ctx.lon, &px, &py)) {   // le recepteur : d'ou l'on entend
            disque(px, py, 4, AMBRE);
        }
    }
    // ma position, le recepteur ecoute, le recepteur choisi
    if (vers_ecran(cfg::R.lat, cfg::R.lon, &px, &py)) { rect((int)px - 5, (int)py, 11, 1, TXT); rect((int)px, (int)py - 5, 1, 11, TXT); }
    if (kw::etat() != kw::ARRET && q_rx && vers_ecran(q_ctx.lat, q_ctx.lon, &px, &py)) anneau(px, py, carte_niv ? 9 : 5, 1.6f, AMBRE);
    float sla = 0, slo = 0;
    bool choisi = carte_k || carte_v;
    if (carte_k) { sla = carte_k->lat; slo = carte_k->lon; }
    if (carte_v) { sla = carte_v->lat; slo = carte_v->lon; }
    if (choisi && vers_ecran(sla, slo, &px, &py)) {
        float rr = RAYON[carte_niv] + 5;
        anneau(px, py, rr, 2, TXT);
        anneau(px, py, rr + 3 + 2 * sinf(t_ms / 300.0f), 1, TXT, 120);       // l'anneau respire : on le repere tout de suite
    }
    sans_decoupe();
    zone(0, Y_CARTE, 320, H_CARTE, B_CARTE, Z_GLISSE);
    // boutons de zoom, pour qui n'a pas le bouton rotatif
    for (int k = 0; k < 2; k++) {
        int by = Y_CARTE + 6 + k * 36, id = k ? B_CARTE_MOINS : B_CARTE_PLUS;
        bool possible = k ? carte_niv > 0 : carte_niv < CARTE_NIV_MAX;
        bool ap = tc_appui && tc_z.id == id;
        rect_arrondi(284, by, 30, 30, 7, ap ? melange(BTN, TXT, 80) : rgb(10, 16, 30), 215);
        contour_arrondi(284, by, 30, 30, 7, 1, LIGNE, 230);
        texte(299, by + 2, k ? "−" : "+", POLICE_22, possible ? TXT : TXT2, possible ? 255 : 110, CENTRE);
        zone(280, by - 3, 40, 36, id, Z_APPUI);
    }
    // bas : le recepteur choisi, ou le mode d'emploi
    rect(0, Y_CARTE + H_CARTE, 320, 240 - Y_CARTE - H_CARTE, PAN);
    rect(0, Y_CARTE + H_CARTE, 320, 1, LIGNE);
    int yb = Y_CARTE + H_CARTE + 4;
    if (carte_ft8 && carte_ft8_sel_i() >= 0) {
        const f8::Message &m = f8::message(carte_ft8_sel_i());
        char t[96];
        snprintf(t, sizeof t, "%s · %s", m.indicatif, m.pays[0] ? m.pays : m.loc);
        texte(8, yb + 2, tronquer(t, POLICE_13, 300), POLICE_13, m.cq ? VERT : JAUNE);
        uint32_t age = plat_epoch() > m.periode ? plat_epoch() - m.periode : 0;
        snprintf(t, sizeof t, "%s · %+d dB · %u km du récepteur · il y a %u min", m.loc, m.snr, m.km, (unsigned)(age / 60));
        texte(8, yb + 22, tronquer(t, POLICE_11, 300), POLICE_11, TXT2);
    } else if (carte_ft8 && !choisi) {
        int n = 0;
        for (int i = 0; i < f8::nb(); i++) n += f8::message(i).loc[0] != 0;
        char t[64];
        snprintf(t, sizeof t, "FT8 : %d message%s avec locator", n, n > 1 ? "s" : "");
        texte(160, yb + 4, t, POLICE_13, TXT, 255, CENTRE);
        texte(160, yb + 24, "Vert : appel général (CQ) · touche une station", POLICE_11, TXT2, 255, CENTRE);
    } else if (choisi) {
        const char *nom = carte_k ? carte_k->nom : carte_v->nom;
        texte(8, yb + 2, tronquer(nom, POLICE_13, 222), POLICE_13, TXT);
        char t[96], d[24] = "";
        uint16_t km = srv::distance_km(cfg::R.lat, cfg::R.lon, sla, slo);
        if (km >= 1000) snprintf(d, sizeof d, "%u %03u km", km / 1000, km % 1000); else snprintf(d, sizeof d, "%u km", km);
        float lu = carte_lumiere(sla, slo, decl, lon_midi);
        const char *jn = !heure ? "" : (lu > 0.03f ? " · jour" : (lu > -0.12f ? " · crépuscule" : " · nuit"));
        if (carte_k && carte_k->umax) snprintf(t, sizeof t, "%s · %u/%u auditeurs%s", d, carte_k->users, carte_k->umax, jn);
        else snprintf(t, sizeof t, "%s%s", d, jn);
        texte(8, yb + 22, tronquer(t, POLICE_11, 222), POLICE_11, TXT2);
        bouton(238, yb + 2, 76, 38, "Écouter", B_CARTE_ECOUTER, 1, POLICE_13);
    } else {
        texte(160, yb + 4, "Touche un récepteur pour le choisir", POLICE_13, TXT, 255, CENTRE);
        const char *l2 = mol_reglage ? "Bouton rotatif : zoom · doigt qui glisse : déplacer la carte"
                                     : "+ et − : zoom · doigt qui glisse : déplacer la carte";
        texte(160, yb + 24, tronquer(l2, POLICE_11, 310), POLICE_11, TXT2, 255, CENTRE);
    }
}

// le doigt glisse sur la carte : elle suit
static void carte_glisser(int dx, int dy)
{
    float lon0, lat_haut, span;
    carte_vue(&lon0, &lat_haut, &span);
    float dpp = span / 320.0f;
    if (!carte_niv) return;                             // vue Monde : tout est deja a l'ecran
    carte_clon -= dx * dpp;
    carte_clat += dy * dpp;
}

// toucher bref : le recepteur le plus proche du doigt ; sinon, zoom a cet endroit
static void carte_toucher(int x, int y)
{
    float lon0, lat_haut, span;
    carte_vue(&lon0, &lat_haut, &span);
    float dpp = span / 320.0f;
    float lon = lon0 + x * dpp, lat = lat_haut - (y - Y_CARTE) * dpp;
    if (carte_ft8) {                                    // d'abord les stations FT8
        float mieux = 16.0f * 16.0f;
        int sel = -1;
        for (int i = 0; i < f8::nb(); i++) {
            const f8::Message &m = f8::message(i);
            if (!m.loc[0]) continue;
            float dx = (m.lon - lon) / dpp, dy = (m.lat - lat) / dpp, d = dx * dx + dy * dy;
            if (d < mieux) { mieux = d; sel = i; }
        }
        if (sel >= 0) { snprintf(carte_ft8_ind, sizeof carte_ft8_ind, "%s", f8::message(sel).indicatif); carte_k = nullptr; carte_v = nullptr; return; }
        carte_ft8_ind[0] = 0;
        carte_clon = lon; carte_clat = lat;             // rien sous le doigt : on se rapproche
        if (carte_niv < CARTE_NIV_MAX) carte_niv++;
        return;
    }
    carte_points();
    float mieux = 20.0f * 20.0f;                        // 20 pixels autour du doigt
    const CartePt *q = nullptr;
    for (int i = 0; i < carte_npts && carte_pts; i++) {
        float dx = (carte_pts[i].lon - lon) / dpp, dy = (carte_pts[i].lat - lat) / dpp, d = dx * dx + dy * dy;
        if (carte_pts[i].plein) d += 40;                // a distance egale, un recepteur libre
        if (d < mieux) { mieux = d; q = &carte_pts[i]; }
    }
    if (q) {
        carte_k = carte_vhf ? nullptr : (const srv::Serveur *)q->p;
        carte_v = carte_vhf ? (const owl::RecepteurVHF *)q->p : nullptr;
        return;
    }
    carte_k = nullptr; carte_v = nullptr;               // rien sous le doigt : on se rapproche de cet endroit
    carte_clon = lon; carte_clat = lat;
    if (carte_niv < CARTE_NIV_MAX) carte_niv++;
}

static int premier_liste = 0;                     // premiere ligne visible de la liste de l'ecran

// ============================ WEBRADIOS =====================================
// En haut la station qui joue ; dessous, des choix rapides (favoris, France, genres) et la liste.
// La recherche par nom se fait sur la page web (clavier).
static int wr_rapide = 0;                  // 0 favoris, 1 France, 2 info...
static bool wr_vue_fav() { return wr_rapide == 0; }   // -1 : resultats d'une recherche faite sur la page web
static int wr_nb() { return wr_vue_fav() ? wr::nb_favoris() : wr::nb_resultats(); }
static const wr::Station &wr_st(int i) { return wr_vue_fav() ? wr::favori(i) : wr::resultat(i); }
static void wr_choisir_rapide(int k)
{
    wr_rapide = k;
    liste_dec = 0; liste_v = 0;
    static const char *const PAYS[wr::NB_RAPIDES] = { "", "FR", "", "", "", "" };
    static const char *const GENRE[wr::NB_RAPIDES] = { "", "", "news", "pop", "jazz", "classical" };
    if (k > 0) wr::chercher("", PAYS[k], GENRE[k]);
}
static void wr_jouer(const wr::Station &s)
{
    rp_ecoutait = false;
    if (kw::etat() != kw::ARRET) kw::deconnecter();
    if (fm::actif()) fm::deconnecter();
    aud::reserve_visee(1500);                        // une webradio n'est pas pressee : grande reserve, pas de coupure
    wr::jouer(s);
}
static void wr_arreter()
{
    wr::arreter();
    aud::reserve_visee(cfg::R.tampon_ms);
}
// une autre source demarre (KiwiSDR, OpenWebRX, FM) : la webradio s'arrete
static void wr_tic()
{
    if (wr::actif() && (kw::etat() != kw::ARRET || fm::actif())) wr_arreter();
}

static void ecran_webradio()
{
    rect(0, 0, 320, 240, FOND);
    entete("");
    onglets_fm_web(true);
    // la station qui joue
    const int YC = 30, HC = 58;
    rect_arrondi(4, YC, 312, HC, 8, PAN);
    if (wr::actif()) {
        const wr::Station &s = wr::en_cours();
        wr::Etat e = wr::etat();
        uint16_t c = e == wr::LECTURE ? VERT : (e == wr::ERREUR ? ROUGE : AMBRE);
        disque(14, YC + 12, 4, c);
        texte(24, YC + 3, tronquer(s.nom, POLICE_16, 200), POLICE_16, TXT);
        const char *t = wr::titre();
        if (e != wr::LECTURE && wr::message()[0]) texte(24, YC + 24, tronquer(wr::message(), POLICE_13, 220), POLICE_13, c);
        else if (t[0]) texte_defile(DEF_FM_RT, 24, YC + 24, 220, t, POLICE_13, CYAN, PAN);
        char l[80];
        snprintf(l, sizeof l, "%s%s%s · réserve %.1f s", wr::format()[0] ? wr::format() : s.codec, s.pays[0] ? " · " : "", s.pays, aud::reserve_ms() / 1000.0f);
        texte(24, YC + 42, tronquer(l, POLICE_11, 220), POLICE_11, TXT2);
        bool fav = wr::est_favori(s.url);
        bouton(250, YC + 4, 30, 26, "", B_WR_FAV, 0, POLICE_13);
        etoile(265, YC + 17, 8, fav ? JAUNE : TXT2, fav);
        bouton(284, YC + 4, 30, 26, "", B_WR_STOP, 2, POLICE_13);
        rect(294, YC + 12, 10, 10, TXT);                // carre : arret
        // volume
        float v = cfg::R.volume / 100.0f;
        rect_arrondi(250, YC + 41, 62, 6, 3, LIGNE);
        rect_arrondi(250, YC + 41, (int)(62 * v) + 3, 6, 3, CYAN);
        zone(244, YC + 32, 74, 24, B_WR_VOL, Z_GLISSE | Z_APPUI);
        reg_curseur(B_WR_VOL, 250, 62, v, 50);
    } else {
        texte(160, YC + 10, "Aucune webradio", POLICE_16, TXT, 255, CENTRE);
        texte(160, YC + 32, "Choisis une station · recherche par nom : page web", POLICE_11, TXT2, 255, CENTRE);
    }
    // choix rapides
    const int YR = YC + HC + 4;
    int w = (312 - 5 * 3) / wr::NB_RAPIDES;
    for (int k = 0; k < wr::NB_RAPIDES; k++)
        bouton(4 + k * (w + 3), YR, w, 20, wr::RAPIDES_NOM[k], B_WR_RAPIDE + k, wr_rapide == k ? 3 : 0, POLICE_11);
    // liste
    const int Y0 = YR + 24, HL = 27;
    int n = wr_nb();
    decoupe(0, Y0, 320, 240);
    int max_dec = n * HL - (240 - Y0) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / HL);
    premier_liste = premier;
    for (int k = premier; k < n && k < premier + 8; k++) {
        const wr::Station &s = wr_st(k);
        int y = Y0 + k * HL - (int)liste_dec;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (k - premier);
        rect_arrondi(4, y, 312, HL - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        bool joue = wr::actif() && !strcmp(s.url, wr::en_cours().url);
        if (joue) contour_arrondi(4, y, 312, HL - 3, 6, 1.2f, BLEU);
        texte(12, y + 4, tronquer(s.nom, POLICE_13, 172), POLICE_13, TXT);
        char d[32];
        if (s.kbps) snprintf(d, sizeof d, "%s %u k%s%s", s.codec, s.kbps, s.pays[0] ? " · " : "", s.pays);
        else snprintf(d, sizeof d, "%s%s%s", s.codec, s.pays[0] ? " · " : "", s.pays);
        texte(278, y + 6, tronquer(d, POLICE_11, 94), POLICE_11, TXT2, 255, DROITE);
        zone(4, y, 280, HL - 3, B_LISTE + (k - premier), Z_GLISSE);
        bool fav = wr::est_favori(s.url);
        etoile(299, y + 12, 7, fav ? JAUNE : TXT2, fav);
        zone(284, y, 32, HL - 3, B_WR_ETOILE + (k - premier));
    }
    sans_decoupe();
    if (n == 0) {
        const char *m = wr_vue_fav() ? "Pas encore de favori : touche l'étoile d'une station"
                       : (wr::recherche_en_cours() ? "Recherche dans l'annuaire…" : (plat_wifi_etat() != WIFI_OK ? "Pas de WiFi" : "Aucune station trouvée"));
        texte(160, Y0 + 30, m, POLICE_13, TXT2, 255, CENTRE);
        if (!wr_vue_fav()) texte(160, Y0 + 52, "Annuaire libre radio-browser.info", POLICE_11, TXT2, 255, CENTRE);
    }
}

// ============================ MEILLEUR RECEPTEUR ==============================
// Pour la frequence en cours (ondes courtes) : la carte essaie le recepteur ecoute et quelques KiwiSDR libres
// places a des endroits differents, mesure sur chacun de combien le signal depasse le bruit (waterfall, a
// l'endroit du filtre), puis reste sur le meilleur.
// v0.16 : aussi en VHF/UHF, entre recepteurs OpenWebRX qui captent la frequence (chacun est regle sur la bonne bande)
struct MrCand { char nom[48]; char hote[64]; char mdp[24]; char chemin[48]; uint16_t port; uint16_t km; float snr; uint8_t etat, proto, tls, bande; };   // etat : 0 a faire, 1 en cours, 2 mesure, 3 echec, 4 ne capte pas
static const int MR_MAX = 5;
static struct {
    int phase;                  // 0 arret, 1 connexion, 2 stabilisation, 3 mesure, 4 retour au meilleur
    MrCand c[MR_MAX];
    int n, i, meilleur;
    uint32_t t;
    char depart[64];            // recepteur ecoute au depart
    bool vhf;                   // comparaison de recepteurs OpenWebRX
    double f;                   // frequence et mode a comparer
    int mode;
} mr;

static bool mr_sur(const MrCand &c) { return kw::etat() == kw::ACTIF && cfg::R.proto == c.proto && !strcmp(cfg::R.hote, c.hote); }
static void mr_relier(const MrCand &c)
{
    rp_interne = true;
    connecter(c.hote, c.port, c.mdp, c.nom, c.proto, c.tls != 0, c.chemin[0] ? c.chemin : "/");
    rp_interne = false;
}
// OpenWebRX : la frequence est-elle prete a mesurer ? (regle la bonne bande et la frequence si besoin)
// 1 prete, 0 patienter, -1 ce recepteur ne la capte pas
static int mr_vhf_prete(MrCand &c, uint32_t age)
{
    if (kw::bande_max_khz() > 0 && mr.f >= kw::bande_min_khz() && mr.f <= kw::bande_max_khz()) {
        if (fabs(cfg::R.freq_khz - mr.f) > 0.01 || cfg::R.mode != mr.mode) {
            cfg::R.mode = (uint8_t)mr.mode;
            cfg::R.pas = (uint8_t)cfg::pas_par_defaut(mr.mode, mr.f);
            accorder(mr.f);
            mr.t = t_ms;                                    // le son et le waterfall se recalent
            return 0;
        }
        return 1;
    }
    if (!c.bande) { int p = trouver_profil(mr.f); if (p >= 0) { c.bande = 1; mr.t = t_ms; kw::choisir_profil(profs[p].id); return 0; } }
    if (c.bande && age < 12000) return 0;                   // la bande change
    return age > 8000 ? -1 : 0;                             // ses bandes arrivent (status.json)
}

static void mr_fin(const char *msg)
{
    mr.phase = 0;
    if (msg) message(msg);
}

static void mr_connecter(int i)
{
    mr.i = i;
    mr.c[i].etat = 1;
    mr.c[i].bande = 0;
    mr.t = t_ms;
    if (mr_sur(mr.c[i])) { mr.phase = 2; return; }
    mr_relier(mr.c[i]);
    mr.phase = 1;
}

static void mr_lancer_vhf()
{
    if (owl::nb() == 0) { message("Liste VHF/UHF vide : ouvre Récepteurs"); return; }
    if (sc_sens) scan_arreter();
    memset(&mr, 0, sizeof mr);
    mr.vhf = true;
    mr.f = cfg::R.freq_khz;
    mr.mode = cfg::R.mode;
    uint8_t bit = mr.f < 300000 ? 1 : 2;
    if (kw::etat() == kw::ACTIF && cfg::R.proto == cfg::OWRX) {           // 1er : celui qu'on ecoute
        MrCand &c = mr.c[mr.n++];
        snprintf(c.nom, sizeof c.nom, "%s", kw::nom());
        snprintf(c.hote, sizeof c.hote, "%s", cfg::R.hote);
        snprintf(c.chemin, sizeof c.chemin, "%s", cfg::R.chemin);
        c.port = cfg::R.port; c.proto = cfg::OWRX; c.tls = cfg::R.chiffre;
        snprintf(mr.depart, sizeof mr.depart, "%s", cfg::R.hote);
    }
    // puis les plus proches de ma position qui annoncent cette bande (en VHF, la portee est courte)
    static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    int n = v ? owl::chercher("", v, owl::MAX_VHF) : 0;      // tries par distance
    for (int i = 0; i < n && mr.n < MR_MAX; i++) {
        if (!(v[i]->bandes & bit) || v[i]->km >= 65000) continue;
        MrCand c;
        memset(&c, 0, sizeof c);
        char u[128];
        if (!strstr(v[i]->url, "://")) snprintf(u, sizeof u, "http://%s", v[i]->url); else snprintf(u, sizeof u, "%s", v[i]->url);
        bool tls = false;
        if (!fml::analyser_url(u, c.hote, sizeof c.hote, &c.port, &tls, c.chemin, sizeof c.chemin)) continue;
        bool pris = false;
        for (int k = 0; k < mr.n && !pris; k++) pris = !strcmp(mr.c[k].hote, c.hote) && mr.c[k].port == c.port;
        if (pris) continue;
        snprintf(c.nom, sizeof c.nom, "%s", v[i]->nom);
        c.km = v[i]->km; c.proto = cfg::OWRX; c.tls = tls;
        mr.c[mr.n++] = c;
    }
    if (mr.n < 2) { mr.phase = 0; message("Pas d'autre récepteur VHF/UHF à comparer"); return; }
    mr.meilleur = -1;
    qr.actif = false;
    mr_connecter(0);
}

static void mr_lancer()
{
    if (mr.phase) { mr_fin("Recherche arrêtée"); return; }
    if (kw::cascade_forcee()) { message("Mesure des bandes en cours"); return; }
    if (cfg::R.freq_khz > 30000 || (kw::etat() == kw::ACTIF && cfg::R.proto == cfg::OWRX)) { mr_lancer_vhf(); return; }
    if (srv::nb_publics() + srv::nb_perso() == 0) { message("Liste des KiwiSDR vide : ouvre Récepteurs"); return; }
    if (kw::cascade_forcee()) { message("Mesure des bandes en cours"); return; }
    if (sc_sens) scan_arreter();
    memset(&mr, 0, sizeof mr);
    mr.f = cfg::R.freq_khz;
    mr.mode = cfg::R.mode;
    // point de reference : l'emetteur s'il est dans l'annuaire, sinon le recepteur ecoute, sinon la carte
    float la = cfg::R.lat, lo = cfg::R.lon;
    if (q_rx) { la = q_ctx.lat; lo = q_ctx.lon; }
    for (int i = 0; i < sta::nb(); i++) {
        const sta::Station *st = sta::station(i);
        if (st && fabs(st->khz - cfg::R.freq_khz) < 0.6 && st->lat != 999) { la = st->lat; lo = st->lon; break; }
    }
    // 1er candidat : le recepteur ecoute
    if (kw::etat() == kw::ACTIF && cfg::R.proto == cfg::KIWI) {
        MrCand &c = mr.c[mr.n++];
        snprintf(c.nom, sizeof c.nom, "%s", kw::nom());
        snprintf(c.hote, sizeof c.hote, "%s", cfg::R.hote);
        snprintf(c.mdp, sizeof c.mdp, "%s", cfg::R.mdp);
        c.port = cfg::R.port;
        c.proto = cfg::KIWI;
        c.km = q_rx ? srv::distance_km(la, lo, q_ctx.lat, q_ctx.lon) : 0;
        snprintf(mr.depart, sizeof mr.depart, "%s", cfg::R.hote);
    }
    // puis des recepteurs libres, du plus proche au plus lointain, a 250 km au moins les uns des autres
    static const srv::Serveur **v = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    float pla[MR_MAX], plo[MR_MAX];
    int np = 0;
    if (mr.n && q_rx) { pla[np] = q_ctx.lat; plo[np] = q_ctx.lon; np++; }
    while (v && mr.n < MR_MAX - 1 + (mr.depart[0] ? 1 : 0) && mr.n < MR_MAX) {
        const srv::Serveur *choix = nullptr;
        uint16_t km_choix = 65535;
        for (int reg = 0; reg < srv::NB_REGIONS; reg++) {
            if (reg == srv::PROCHES) continue;
            int n = srv::filtrer(reg, v, srv::MAX_PUBLICS + srv::MAX_PERSO);
            for (int i = 0; i < n; i++) {
                if (!v[i]->perso && v[i]->umax && v[i]->users >= v[i]->umax) continue;
                if (v[i]->lat == 0 && v[i]->lon == 0) continue;
                bool pris = false;
                for (int k = 0; k < mr.n && !pris; k++) pris = !strcmp(mr.c[k].hote, v[i]->hote);
                for (int k = 0; k < np && !pris; k++) pris = srv::distance_km(pla[k], plo[k], v[i]->lat, v[i]->lon) < 250;
                if (pris) continue;
                uint16_t km = srv::distance_km(la, lo, v[i]->lat, v[i]->lon);
                if (km < km_choix) { km_choix = km; choix = v[i]; }
            }
        }
        if (!choix) break;
        MrCand &c = mr.c[mr.n++];
        snprintf(c.nom, sizeof c.nom, "%s", choix->nom);
        snprintf(c.hote, sizeof c.hote, "%s", choix->hote);
        snprintf(c.mdp, sizeof c.mdp, "%s", choix->mdp);
        c.port = choix->port;
        c.proto = cfg::KIWI;
        c.km = km_choix;
        if (np < MR_MAX) { pla[np] = choix->lat; plo[np] = choix->lon; np++; }
    }
    if (mr.n < 2) { mr.phase = 0; message("Pas d'autre récepteur libre à comparer"); return; }
    mr.meilleur = -1;
    qr.actif = false;
    mr_connecter(0);
}

static void mr_suivant()
{
    for (int i = mr.i + 1; i < mr.n; i++) if (mr.c[i].etat == 0) { mr_connecter(i); return; }
    // tout est mesure : le meilleur
    int b = -1;
    for (int i = 0; i < mr.n; i++) if (mr.c[i].etat == 2 && (b < 0 || mr.c[i].snr > mr.c[b].snr)) b = i;
    mr.meilleur = b;
    if (b < 0) {                                        // personne n'a repondu : retour au recepteur de depart
        if (mr.depart[0] && !mr_sur(mr.c[0])) mr_relier(mr.c[0]);
        mr_fin(mr.vhf ? "Aucun récepteur ne capte cette fréquence" : "Aucun récepteur n'a pu être mesuré");
        return;
    }
    if (!mr_sur(mr.c[b])) mr_relier(mr.c[b]);
    mr.c[b].bande = 0;
    mr.phase = 4;
    mr.t = t_ms;
}

// a chaque image
static void mr_tic()
{
    if (!mr.phase) return;
    rp_ecoutait = false;                                // pas de reprise automatique pendant les essais
    kw::Etat e = kw::etat();
    uint32_t age = t_ms - mr.t;
    MrCand &c = mr.c[mr.i];
    bool connecte = mr_sur(c);
    switch (mr.phase) {
    case 1:
        if (connecte) { mr.phase = 2; mr.t = t_ms; break; }
        if (e == kw::PERDU || e == kw::OCCUPE || e == kw::REFUS || e == kw::PANNE || e == kw::ARRET || age > 15000) { c.etat = 3; mr_suivant(); }
        break;
    case 2:                                             // le son et le waterfall s'installent
        if (!connecte) { c.etat = 3; mr_suivant(); break; }
        if (mr.vhf) {
            int p = mr_vhf_prete(c, age);
            if (p < 0) { c.etat = 4; mr_suivant(); break; }
            if (p == 0) break;
            age = t_ms - mr.t;
        }
        if (age < 1800) break;
        if (kw::sans_cascade()) { c.etat = 3; mr_suivant(); break; }
        c.snr = 0;
        mr.phase = 3;
        mr.t = t_ms;
        break;
    case 3: {
        if (!connecte) { c.etat = 3; mr_suivant(); break; }
        float kpp = khz_par_px();
        int bas, haut;
        cfg::bande_passante_ecran(cfg::R.mode, cfg::R.bp[cfg::R.mode], &bas, &haut);
        double f0 = kw::cascade_centre_khz() - kw::cascade_largeur_khz() / 2;
        double a = cfg::R.freq_khz + bas / 1000.0, b = cfg::R.freq_khz + haut / 1000.0;
        if (b - a < 3 * kpp) { double m = (a + b) / 2; a = m - 1.5 * kpp; b = m + 1.5 * kpp; }
        float au = 0;
        if (kpp > 0 && quoi::compter(wf::spectre(), wf::L, f0, kpp, a, b, 1, 0, nullptr, &au) > 0 && au > c.snr) c.snr = au;
        if (age < 3000) break;
        c.etat = 2;
        mr_suivant();
        break;
    }
    case 4:
        if (mr_sur(mr.c[mr.meilleur])) {
            if (mr.vhf && mr_vhf_prete(mr.c[mr.meilleur], age) == 0 && age < 20000) break;
            if (!mr.vhf && fabs(cfg::R.freq_khz - mr.f) > 0.01) accorder(mr.f);
            char m[96];
            snprintf(m, sizeof m, "Meilleur : %.40s (+%.0f dB)", mr.c[mr.meilleur].nom, mr.c[mr.meilleur].snr);
            mr_fin(m);
        } else if (e == kw::PERDU || e == kw::OCCUPE || e == kw::REFUS || e == kw::PANNE || age > 15000) mr_fin("Le meilleur récepteur ne répond plus");
        break;
    }
}

// panneau sur l'ecran Radio pendant la recherche
static void mr_dessin()
{
    if (!mr.phase || ecran != E_RADIO) return;
    const int x0 = 6, y0 = Y_SPEC + 2, l = 308, h = 26 + mr.n * 17 + 4;
    rect_arrondi(x0, y0, l, h, 8, PAN, 248);
    contour_arrondi(x0, y0, l, h, 8, 1, CYAN, 220);
    zone(x0, y0, l, h, B_MEILLEUR);
    char t[64], f[24];
    fmt_freq_u(f, sizeof f, cfg::R.freq_khz);
    snprintf(t, sizeof t, "Meilleur pour %s", f);
    texte(14, y0 + 5, t, POLICE_13, CYAN);
    texte(x0 + l - 8, y0 + 7, "toucher : arrêter", POLICE_11, TXT2, 255, DROITE);
    for (int i = 0; i < mr.n; i++) {
        const MrCand &c = mr.c[i];
        int y = y0 + 26 + i * 17;
        bool cours = c.etat == 1;
        char km[16];
        if (c.km >= 1000) snprintf(km, sizeof km, "%u %03u km", c.km / 1000, c.km % 1000); else snprintf(km, sizeof km, "%u km", c.km);
        texte(14, y, tronquer(c.nom, POLICE_11, 150), POLICE_11, cours ? AMBRE : (c.etat == 3 ? TXT2 : TXT));
        texte(222, y, i == 0 && mr.depart[0] ? "actuel" : km, POLICE_11, TXT2, 255, DROITE);
        if (c.etat == 2 || (cours && mr.phase == 3)) {
            int niv = (int)(c.snr / 6);
            if (niv > 8) niv = 8;
            for (int k = 0; k < 8; k++) rect(230 + k * 5, y + 3, 4, 8, k < niv ? (c.snr >= 20 ? VERT : AMBRE) : LIGNE);
            snprintf(t, sizeof t, "+%.0f dB", c.snr);
            texte(x0 + l - 8, y, t, POLICE_11, c.etat == 2 ? TXT : AMBRE, 255, DROITE);
        } else texte(x0 + l - 8, y, cours ? (c.bande ? "bande…" : ((t_ms / 400) % 2 ? "connexion…" : "connexion")) : (c.etat == 3 ? "pas de réponse" : (c.etat == 4 ? "ne capte pas" : "à faire")), POLICE_11, cours ? AMBRE : TXT2, 255, DROITE);
    }
}

// ============================ MEILLEUR SERVEUR FM ==============================
// Pour la station ecoutee : la carte essaie quelques serveurs FM-DX proches, libres (personne d'autre a
// l'ecoute : on ne change jamais la frequence d'un tuner qu'un autre utilise), mesure le signal (dBf) et le
// code PI du RDS, et reste sur le meilleur. Un serveur qui recoit un autre emetteur (PI different) est
// classe apres ceux qui recoivent le meme.
struct FbCand { char nom[48]; char url[100]; uint16_t km; float sig; int nsig; char pi[6]; uint8_t etat; };   // 0 a faire, 1 en cours, 2 mesure, 3 pas de reponse, 4 occupe, 5 hors bande
static const int FB_MAX = 5;
static struct { int phase; FbCand c[FB_MAX]; int n, i, meilleur; uint32_t t; float mhz; char pi[6]; uint32_t maj0; } mfm;   // phase 0 arret, 1 connexion, 2 accord, 3 mesure, 4 retour

static int mfm_phase() { return mfm.phase; }
static void fb_fin(const char *m) { mfm.phase = 0; if (m) message(m); }
static bool fb_plage(const char *p, float mhz)              // "87.5 - 108 MHz" ; vide = on essaie
{
    if (!p || !p[0]) return true;
    float a = 0, b = 0;
    if (sscanf(p, "%f - %f", &a, &b) == 2 && a > 0 && b > a) return mhz >= a - 0.05f && mhz <= b + 0.05f;
    return true;
}
static void fb_connecter(int i)
{
    mfm.i = i;
    mfm.c[i].etat = 1;
    mfm.t = t_ms;
    fm::Info in; fm::info(&in);
    if (in.etat == fm::ACTIF && !strcmp(in.url, mfm.c[i].url)) { mfm.phase = 2; return; }
    fm::connecter(mfm.c[i].url, mfm.c[i].nom);
    mfm.phase = 1;
}
static void fb_suivant()
{
    for (int i = mfm.i + 1; i < mfm.n; i++) if (mfm.c[i].etat == 0) { fb_connecter(i); return; }
    // le meilleur : d'abord le meme emetteur (PI), puis le signal
    int b = -1;
    for (int i = 0; i < mfm.n; i++) {
        if (mfm.c[i].etat != 2) continue;
        float s = mfm.c[i].sig - (mfm.pi[0] && mfm.c[i].pi[0] && strcmp(mfm.pi, mfm.c[i].pi) ? 200 : 0);
        float sb = b < 0 ? -1e9f : mfm.c[b].sig - (mfm.pi[0] && mfm.c[b].pi[0] && strcmp(mfm.pi, mfm.c[b].pi) ? 200 : 0);
        if (s > sb) b = i;
    }
    mfm.meilleur = b;
    if (b < 0) { fm::connecter(mfm.c[0].url, mfm.c[0].nom); mfm.phase = 4; mfm.meilleur = 0; mfm.t = t_ms; return; }
    fm::Info in; fm::info(&in);
    if (strcmp(in.url, mfm.c[b].url)) fm::connecter(mfm.c[b].url, mfm.c[b].nom);
    mfm.phase = 4;
    mfm.t = t_ms;
}
static void fb_lancer()
{
    if (mfm.phase) { fb_fin("Recherche arrêtée"); return; }
    fm::Info in; fm::info(&in);
    if (!fm::actif() || in.etat != fm::ACTIF || in.mhz < 60) { message("Meilleur serveur : écoute d'abord une station FM"); return; }
    if (fml::nb() == 0) { message("Liste FM vide : ouvre Serveurs"); return; }
    fs_arreter();
    memset(&mfm, 0, sizeof mfm);
    mfm.mhz = in.mhz;
    snprintf(mfm.pi, sizeof mfm.pi, "%s", in.pi);
    FbCand &c0 = mfm.c[mfm.n++];
    snprintf(c0.nom, sizeof c0.nom, "%s", in.serveur);
    snprintf(c0.url, sizeof c0.url, "%s", in.url);
    static const fml::ServeurFM **v = (const fml::ServeurFM **)plat_malloc_ps(sizeof(void *) * fml::MAX_FM);
    int n = v ? fml::chercher("", v, fml::MAX_FM) : 0;      // disponibles par distance
    for (int i = 0; i < n && mfm.n < FB_MAX; i++) {
        if (v[i]->etat != 1 || v[i]->km >= 65000 || !strcmp(v[i]->url, in.url) || !fb_plage(v[i]->plage, mfm.mhz)) continue;
        FbCand &c = mfm.c[mfm.n++];
        memset(&c, 0, sizeof c);
        snprintf(c.nom, sizeof c.nom, "%s", v[i]->nom);
        snprintf(c.url, sizeof c.url, "%s", v[i]->url);
        c.km = v[i]->km;
    }
    if (mfm.n < 2) { mfm.phase = 0; message("Pas d'autre serveur FM libre à comparer"); return; }
    mfm.meilleur = -1;
    fb_connecter(0);
}
static void fb_tic()
{
    if (!mfm.phase) return;
    fm::Info in; fm::info(&in);
    uint32_t age = t_ms - mfm.t;
    FbCand &c = mfm.c[mfm.i];
    bool sur = in.etat == fm::ACTIF && !strcmp(in.url, mfm.phase == 4 ? mfm.c[mfm.meilleur].url : c.url);
    if (!fm::actif()) { fb_fin(nullptr); return; }          // l'utilisateur a arrete la FM
    switch (mfm.phase) {
    case 1:
        if (sur) { mfm.phase = 2; mfm.t = t_ms; break; }
        if (in.etat == fm::PERDU || age > 15000) { c.etat = 3; fb_suivant(); }
        break;
    case 2:                                             // qui ecoute ? puis la frequence
        if (!sur) { c.etat = 3; fb_suivant(); break; }
        if (age < 1500 || in.mhz_serveur <= 0) { if (age > 8000) { c.etat = 3; fb_suivant(); } break; }
        if (fabsf(in.mhz - mfm.mhz) > 0.04f) {
            if (in.auditeurs > 1) { c.etat = 4; fb_suivant(); break; }   // quelqu'un d'autre ecoute : on ne touche pas a son tuner
            fm::accorder(mfm.mhz);
            mfm.t = t_ms;
            mfm.phase = 3;
            mfm.maj0 = in.maj;
            c.sig = 0; c.nsig = 0;
            break;
        }
        mfm.phase = 3; mfm.t = t_ms; mfm.maj0 = in.maj; c.sig = 0; c.nsig = 0;
        break;
    case 3:                                             // 3 s de mesure une fois la frequence prise
        if (!sur) { c.etat = 3; fb_suivant(); break; }
        if (fabsf(in.mhz - mfm.mhz) > 0.04f) { if (age > 5000) { c.etat = 5; fb_suivant(); } break; }
        if (in.maj != mfm.maj0) { mfm.maj0 = in.maj; if (age > 1200) { c.sig += in.sig; c.nsig++; } }
        if (in.pi[0]) snprintf(c.pi, sizeof c.pi, "%s", in.pi);
        if (age < 4500) break;
        if (c.nsig) { c.sig /= c.nsig; c.etat = 2; } else c.etat = 3;
        fb_suivant();
        break;
    case 4:
        if (sur) {
            if (fabsf(in.mhz - mfm.mhz) > 0.04f && in.mhz_serveur > 0 && in.auditeurs <= 1) fm::accorder(mfm.mhz);
            char m[96];
            if (mfm.c[mfm.meilleur].etat == 2) snprintf(m, sizeof m, "Meilleur : %.40s (%.0f dBf)", mfm.c[mfm.meilleur].nom, mfm.c[mfm.meilleur].sig);
            else snprintf(m, sizeof m, "Aucun autre serveur n'a pu être mesuré");
            fb_fin(m);
        } else if (in.etat == fm::PERDU || age > 15000) fb_fin("Le meilleur serveur ne répond plus");
        break;
    }
}
static void fb_dessin()
{
    if (!mfm.phase || ecran != E_FM) return;
    const int x0 = 6, y0 = 76, l = 308, h = 26 + mfm.n * 15 + 4;
    rect_arrondi(x0, y0, l, h, 8, PAN, 248);
    contour_arrondi(x0, y0, l, h, 8, 1, CYAN, 220);
    zone(x0, y0, l, h, B_FM_MEILLEUR);
    char t[64];
    snprintf(t, sizeof t, "Meilleur serveur pour %.1f MHz%s%s", mfm.mhz, mfm.pi[0] ? " · PI " : "", mfm.pi);
    texte(14, y0 + 5, t, POLICE_13, CYAN);
    for (int i = 0; i < mfm.n; i++) {
        const FbCand &c = mfm.c[i];
        int y = y0 + 25 + i * 15;
        bool cours = c.etat == 1;
        char km[16];
        if (i == 0) snprintf(km, sizeof km, "actuel"); else snprintf(km, sizeof km, "%u km", c.km);
        texte(14, y, tronquer(c.nom, POLICE_11, 150), POLICE_11, cours ? AMBRE : (c.etat >= 3 ? TXT2 : TXT));
        texte(222, y, km, POLICE_11, TXT2, 255, DROITE);
        if (c.etat == 2) {
            bool autre = mfm.pi[0] && c.pi[0] && strcmp(mfm.pi, c.pi);
            snprintf(t, sizeof t, "%.0f dBf%s", c.sig, autre ? " (autre PI)" : "");
            texte(x0 + l - 8, y, t, POLICE_11, autre ? AMBRE : TXT, 255, DROITE);
        } else {
            static const char *E[6] = { "à faire", "", "", "pas de réponse", "occupé", "hors bande" };
            texte(x0 + l - 8, y, cours ? (mfm.phase == 3 ? "mesure…" : "connexion…") : E[c.etat], POLICE_11, cours ? AMBRE : TXT2, 255, DROITE);
        }
    }
}

// regle la radio sur une suggestion
static void q_ecouter(int i)
{
    if (i < 0 || i >= nq || !ql) return;
    if (qm_phase) q_mesure_fin(nullptr);
    QLigne &l = ql[i];
    const quoi::Sugg &s = l.s;
    if (s.genre == quoi::RADIO_FM && !l.dispo) {        // pas de bande FM ici : la radio FM par Internet
        message("Radio FM : par les serveurs FM-DX");
        aller(E_FM);
        return;
    }
    if (kw::etat() != kw::ACTIF || !l.dispo) { q_rediriger(s, s.f); return; }   // le recepteur adapte le plus proche
    qr.actif = false;
    double f = s.f;
    if (l.mesure && l.sig > 0 && l.f_fort > 0) {        // mesure : sur le signal le plus fort
        uint8_t m0 = cfg::R.mode;
        cfg::R.mode = s.mode;
        f = s.a < s.b ? scan_caler(l.f_fort) : s.f;
        cfg::R.mode = m0;
        if (f < s.a || f > s.b) f = s.f;
    }
    if (cfg::R.proto == cfg::OWRX) { regler_canal(f, s.mode); return; }
    if (s.a < s.b && f == s.f) {                         // une bande, sans mesure : la ou on l'avait laissee
        uint8_t md = s.mode;
        double fm_ = mem_rappel(s.f, s.a, s.b, &md);
        if (fm_ != s.f) { quoi::Sugg s2 = s; s2.mode = md; q_regler_kiwi(s2, fm_); aller(E_RADIO); return; }
    }
    q_regler_kiwi(s, f);
    aller(E_RADIO);
}

static uint16_t q_couleur(int genre)
{
    switch (genre) {
    case quoi::RADIOAM: case quoi::TALKIE: return VERT;
    case quoi::RADIODIF: case quoi::RADIO_FM: return AMBRE;
    default: return CYAN;
    }
}

// texte de droite d'une ligne (etat estime ou resultat de la mesure)
static void q_etat(const QLigne &l, char *s, size_t n)
{
    if (!l.dispo && kw::etat() == kw::ACTIF) { snprintf(s, n, "%s", l.s.genre == quoi::RADIO_FM ? "par Internet" : "autre récepteur"); return; }
    if (!l.mesure) { snprintf(s, n, "%s", l.s.etat); return; }
    if (l.s.a == l.s.b) snprintf(s, n, "%s", l.sig > 0 ? "signal reçu" : "rien reçu");
    else if (l.sig <= 0) snprintf(s, n, "vide");
    else snprintf(s, n, "%d signa%s", l.sig, l.sig > 1 ? "ux" : "l");
}

static void q_ligne_contexte(char *s, size_t n, bool mes)
{
    const char *m = quoi::moment(q_ctx);
    char ou[64];
    if (kw::etat() != kw::ARRET && cfg::R.hote[0]) snprintf(ou, sizeof ou, "%s", kw::nom());
    else snprintf(ou, sizeof ou, "ma position (pas de récepteur)");
    if (mes && !qm_phase) {
        unsigned mn = (unsigned)((t_ms - q_mes_t) / 60000);
        if (mn == 0) snprintf(s, n, "Mesuré à l'instant · %s", ou);
        else snprintf(s, n, "Mesuré il y a %u min · %s", mn, ou);
    } else if (!m[0]) snprintf(s, n, "Heure inconnue · %s", ou);
    else snprintf(s, n, "%s · %s", m, ou);
    float flux, kp;
    if (quoi::solaire(&flux, &kp) && q_cat != quoi::VHF && q_cat != quoi::UHF) {       // propagation du jour, en tete
        char p[48], t[120];
        if (flux >= 0 && kp >= 0) snprintf(p, sizeof p, "Flux %.0f · Kp %.0f", flux, kp);
        else if (flux >= 0) snprintf(p, sizeof p, "Flux %.0f", flux);
        else snprintf(p, sizeof p, "Kp %.0f", kp);
        snprintf(t, sizeof t, "%s · %s", p, s);
        snprintf(s, n, "%s", t);
    }
}

static void ecran_quoi()
{
    rect(0, 0, 320, 240, FOND);
    if (!qm_phase && (t_ms - t_q > 3000 || v_q_vhf != vhf::version() || v_q_pl != kw::version_plages())) q_maj();
    entete("Quoi écouter");
    {   // saisie d'une frequence au clavier
        bool ap = tc_appui && tc_z.id == B_FREQ;
        rect_arrondi(226, 3, 88, 20, 5, ap ? melange(BTN, TXT, 60) : BTN);
        contour_arrondi(226, 3, 88, 20, 5, 1, ap ? CYAN : LIGNE);
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) rect_arrondi(233 + a * 5, 7 + b * 5, 3, 3, 1, CYAN);
        texte(280, 6, "Fréquence", POLICE_11, TXT, 255, CENTRE);
        zone(222, 0, 98, 26, B_FREQ);
    }
    static const char *N[4] = { "Ondes courtes", "VHF", "UHF", "Tout" };
    int x = 4;
    for (int i = 0; i < 4; i++) {
        int l = largeur_texte(N[i], POLICE_11) + 14;
        bool on = i == q_cat;
        rect_arrondi(x, 31, l, 18, 9, on ? BLEU : PAN);
        if (!on) contour_arrondi(x, 31, l, 18, 9, 1, LIGNE);
        texte(x + l / 2, 33, N[i], POLICE_11, on ? SOMBRE : TXT, 255, CENTRE);
        zone(x, 29, l, 22, B_Q_CAT + i);
        x += l + 4;
    }
    bouton(248, 29, 68, 22, qm_phase ? "Arrêter" : "Mesurer", B_Q_MESURER, qm_phase ? 2 : 1, POLICE_13);
    char ctx[112];
    bool mesure = false;
    for (int i = 0; i < nq && ql; i++) if (ql[i].mesure) { mesure = true; break; }
    q_ligne_contexte(ctx, sizeof ctx, mesure);
    texte(8, 54, tronquer(ctx, POLICE_11, 304), POLICE_11, TXT2);

    bool aucun = nq > 0 && q_dispo == 0 && !qm_phase;   // rien d'ecoutable ici : bandeau en bas
    int bas = aucun ? 220 : 240;
    decoupe(0, Y_QUOI, 320, bas);
    int max_dec = nq * H_LIGNE - (bas - Y_QUOI) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui || tc_z.id < B_LISTE || tc_z.id >= B_LISTE + 40) {
        liste_dec += liste_v;
        liste_v *= 0.9f;
        if (fabsf(liste_v) < 0.2f) liste_v = 0;
    }
    if (liste_dec < 0) { liste_dec = 0; liste_v = 0; }
    if (liste_dec > max_dec) { liste_dec = (float)max_dec; liste_v = 0; }
    int premier = (int)(liste_dec / H_LIGNE);
    for (int i = premier; i < nq && i < premier + 8 && ql; i++) {
        const QLigne &l = ql[i];
        int y = Y_QUOI + i * H_LIGNE - (int)liste_dec;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_LISTE + (i - premier);
        bool en_cours = qm_phase && i == qm_i;
        rect_arrondi(4, y, 312, H_LIGNE - 3, 6, ap ? melange(PAN, TXT, 40) : PAN);
        if (en_cours) contour_arrondi(4, y, 312, H_LIGNE - 3, 6, 1.2f, AMBRE);
        bool net = l.dispo || kw::etat() != kw::ACTIF;   // sans recepteur : l'estimation, sans rien griser
        uint8_t al = net ? 255 : 120;
        disque(13, y + 14, 4, net ? q_couleur(l.s.genre) : TXT2, al);
        int niv = q_niveau(l);
        char e[24];
        q_etat(l, e, sizeof e);
        int le = largeur_texte(e, POLICE_11);
        if (net) {
            uint16_t c = niv >= 4 ? VERT : (niv >= 2 ? AMBRE : TXT2);
            for (int k = 0; k < 5; k++) rect_arrondi(263 + k * 9, y + 5, 6, 8, 2, k < niv ? c : LIGNE);
        }
        texte(24, y + 2, tronquer(l.s.nom, POLICE_13, net ? 232 : 284), POLICE_13, TXT, al);
        texte(24, y + 16, tronquer(l.s.detail, POLICE_11, 308 - 24 - le - 8), POLICE_11, TXT2, al);
        texte(308, y + 16, e, POLICE_11, net && niv >= 4 ? VERT : TXT2, al, DROITE);
        zone(4, y, 312, H_LIGNE - 3, B_LISTE + (i - premier), Z_GLISSE);
    }
    sans_decoupe();
    if (aucun) {
        bool ap = tc_appui && tc_z.id == B_Q_RECEPTEURS;
        rect(0, 220, 320, 20, ap ? melange(PAN, TXT, 40) : PAN);
        rect(0, 220, 320, 1, LIGNE);
        const char *t = "Toucher une ligne change de récepteur · Liste  ›";
        texte(160, 223, t, POLICE_11, AMBRE, 255, CENTRE);
        zone(0, 216, 320, 24, B_Q_RECEPTEURS);
    }
    if (qm_phase) {                                     // avancement de la mesure
        rect_arrondi(4, 182, 312, 54, 8, PAN, 245);
        contour_arrondi(4, 182, 312, 54, 8, 1, AMBRE);
        char t[80];
        const char *nom = qm_i >= 0 && qm_i < nq ? ql[qm_i].s.nom : "";
        char court[28];
        snprintf(court, sizeof court, "%s", nom);
        if (char *p = strstr(court, " ·")) *p = 0;      // "40 m · radioamateurs" -> "40 m"
        snprintf(t, sizeof t, "Mesure : %s… %d sur %d", court, qm_fait + 1 > qm_total ? qm_total : qm_fait + 1, qm_total);
        texte(160, 187, tronquer(t, POLICE_13, 296), POLICE_13, AMBRE, 255, CENTRE);
        rect_arrondi(20, 210, 280, 8, 4, LIGNE);
        int l = qm_total ? 280 * qm_fait / qm_total : 0;
        if (l > 6) rect_arrondi(20, 210, l, 8, 4, AMBRE);
        texte(160, 220, "le son continue · Arrêter garde ce qui est mesuré", POLICE_11, TXT2, 255, CENTRE);
    }
}

static void touche_pave(int i)
{
    int n = strlen(saisie);
    if (i == 11) { if (n) saisie[n - 1] = 0; return; }
    if (i == 12) {
        if (!saisie[0]) { aller(E_RADIO); return; }
        char t[12];
        snprintf(t, sizeof t, "%s", saisie);
        for (char *p = t; *p; p++) if (*p == ',') *p = '.';
        double f = atof(t);
        if (cfg::R.proto == cfg::OWRX) {                // saisie en MHz
            f *= 1000.0;
            if (kw::etat() == kw::ACTIF && (f < kw::bande_min_khz() || f > kw::bande_max_khz())) {
                message("Hors de la bande : choisis d'abord une autre bande à gauche");
                return;
            }
        } else if (f < 0 || f > 32000) { message("Fréquence hors bande (0 à 30 000 kHz)"); return; }
        saisie[0] = 0;
        accorder(f);
        aller(E_RADIO);
        return;
    }
    const char *v = strchr(saisie, ',');
    if (i == 9) { if (!v && n < 9) { if (!n) strcpy(saisie, "0"); strcat(saisie, ","); } return; }
    char c = i == 10 ? '0' : (char)('1' + i);
    bool mhz = cfg::R.proto == cfg::OWRX;
    if (v && strlen(v) >= (mhz ? 5u : 3u)) return;  // 10 Hz pres (kHz) ou 100 Hz pres (MHz)
    if (!v && n >= (mhz ? 4 : 5)) return;
    if (n < 10) { saisie[n] = c; saisie[n + 1] = 0; }
}

// ============================ DECODEURS =====================================
static void choisir_decodeur(int m)
{
    dec::choisir(m);
    // mode de reception conseille pour chaque decodeur
    if (m == dec::CW && cfg::R.mode != cfg::CW) changer_mode(cfg::CW);
    if (m == dec::RTTY && cfg::R.mode != cfg::USB && cfg::R.mode != cfg::LSB) changer_mode(cfg::USB);
    if (m == dec::FAX && cfg::R.mode != cfg::USB) changer_mode(cfg::USB);
    if (m == dec::FT8 && cfg::R.mode != cfg::USB) changer_mode(cfg::USB);
}

// ---------------------------------------------------------------- FT8
static int ft8_defil = 0;                            // lignes de defilement (0 : les plus recentes en haut)
static float ft8_defil_px = 0;
static int ft8_bande_proche()                        // bande FT8 de la frequence ecoutee (-1 : aucune)
{
    for (int i = 0; i < f8::NB_BANDES; i++) if (fabs(cfg::R.freq_khz - f8::BANDES_KHZ[i]) < 4) return i;
    return -1;
}
static void ft8_bande(int i)
{
    if (i < 0 || i >= f8::NB_BANDES) return;
    if (cfg::R.mode != cfg::USB) changer_mode(cfg::USB);
    if (cfg::R.bp[cfg::USB] < 2700) { cfg::R.bp[cfg::USB] = 2700; cfg::marquer(); }   // filtre 300..3000 Hz : toute la sous-bande FT8
    accorder(f8::BANDES_KHZ[i]);
    f8::effacer();
    ft8_defil = 0;
    char m[48];
    snprintf(m, sizeof m, "FT8 %s : %d kHz USB", f8::BANDES_NOM[i], f8::BANDES_KHZ[i]);
    message(m);
}
static void ft8_bande_suivante()
{
    int b = ft8_bande_proche();
    if (b < 0) {                                     // la bande FT8 la plus proche de la frequence ecoutee
        double mieux = 1e9;
        for (int i = 0; i < f8::NB_BANDES; i++) { double d = fabs(cfg::R.freq_khz - f8::BANDES_KHZ[i]); if (d < mieux) { mieux = d; b = i; } }
        ft8_bande(b);
    } else ft8_bande((b + 1) % f8::NB_BANDES);
}
static void ft8_liste(int YT, int HT)
{
    const int LH = 15;
    int visibles = (HT - 6) / LH;
    int n = f8::nb();
    if (ft8_defil > n - visibles) ft8_defil = n - visibles;
    if (ft8_defil < 0) ft8_defil = 0;
    decoupe(4, YT + 1, 316, YT + HT - 1);
    uint32_t per_prec = 0;
    for (int k = 0; k < visibles && ft8_defil + k < n; k++) {
        const f8::Message &m = f8::message(ft8_defil + k);
        int y = YT + 3 + k * LH;
        if (k > 0 && m.periode != per_prec) rect(10, y - 1, 300, 1, LIGNE);   // separation entre periodes
        per_prec = m.periode;
        char t[16];
        snprintf(t, sizeof t, "%+d", m.snr);
        texte(34, y, t, POLICE_11, m.snr >= -10 ? VERT : (m.snr >= -18 ? JAUNE : AMBRE), 255, DROITE);
        snprintf(t, sizeof t, "%u", m.hz);
        texte(64, y, t, POLICE_11, TXT2, 255, DROITE);
        texte(70, y, tronquer(m.texte, POLICE_11, 150), POLICE_11, m.cq ? VERT_T : TXT);
        char d[48];
        if (m.km) snprintf(d, sizeof d, "%s %u km", m.pays, m.km); else snprintf(d, sizeof d, "%s", m.pays);
        texte(312, y, tronquer(d, POLICE_11, 90), POLICE_11, TXT2, 255, DROITE);
    }
    sans_decoupe();
    if (n == 0) {
        const char *a = !f8::heure_ok() ? "Heure inconnue : en attente de l'heure Internet" :
                        (ft8_bande_proche() < 0 ? "Touche la bande pour aller sur une fréquence FT8" : "En attente de la fin de la période…");
        texte(160, YT + HT / 2 - 8, a, POLICE_13, TXT2, 255, CENTRE);
    } else if (ft8_defil > 0) {
        char t[24];
        snprintf(t, sizeof t, "▲ %d plus récents", ft8_defil);
        texte(312, YT + HT - 15, t, POLICE_11, CYAN, 255, DROITE);
    }
    zone(4, YT, 312, HT, B_DEC_LISTE, Z_GLISSE);
}

// decoupe le texte en lignes qui tiennent dans la largeur, garde les dernieres
static int couper_lignes(const char *t, const PoliceAA &p, int largeur, char lignes[][64], int max)
{
    static char tmp[64];
    int n = 0;
    const char *debut = t;
    while (*debut) {
        int k = 0, dernier_espace = -1;
        const char *q = debut;
        while (*q && *q != '\n' && k < 62) {
            tmp[k] = *q;
            tmp[k + 1] = 0;
            if (largeur_texte(tmp, p) > largeur) break;
            if (*q == ' ') dernier_espace = k;
            k++;
            q++;
        }
        int pris = k;
        if (*q && *q != '\n' && dernier_espace > 0) pris = dernier_espace + 1;
        if (pris == 0 && *q && *q != '\n') pris = 1;
        char *dst = lignes[n % max];
        memcpy(dst, debut, pris);
        dst[pris] = 0;
        n++;
        debut += pris;
        if (*debut == '\n') debut++;
    }
    return n;
}

static void ecran_decodeurs()
{
    rect(0, 0, 320, 240, FOND);
    rect(0, 0, 320, 26, PAN);
    rect(0, 26, 320, 1, LIGNE);
    bool ap = tc_appui && tc_z.id == B_RETOUR;
    rect_arrondi(5, 3, 26, 20, 4, ap ? melange(BTN, TXT, 60) : BTN);
    chevron_retour(15, 8, TXT);
    zone(0, 0, 40, 26, B_RETOUR);
    static const char *ONG[4] = { "CW", "RTTY", "FAX", "FT8" };
    for (int i = 0; i < 4; i++) {
        bool on = dec::choisi() == i;
        bouton(38 + i * 46, 3, 42, 20, ONG[i], B_ONGLET + i, on ? 3 : 0, POLICE_13);
    }
    char fm[40];
    freq_mode(fm, sizeof fm);
    texte(314, 7, fm, POLICE_11, AMBRE, 255, DROITE);

    // apercu du son 0..3 kHz
    const int YS = 28, HS = 30;
    float hpp = dec::hz_par_point();
    for (int r = 0; r < HS; r++) {
        const uint8_t *l = dec::spectre(r);
        uint16_t *dst = fb + (YS + r) * L;
        for (int x = 0; x < 320; x++) dst[x] = wf::couleur(cfg::R.palette, l[x * dec::SP_N / 320]);
    }
    float bw = hpp * dec::SP_N;                       // Hz couverts
    float ton = dec::tonalite_hz();
    float xt = ton * 320 / bw;
    rect((int)xt, YS, 2, HS, AMBRE, 220);
    zone(0, YS, 320, HS, B_DEC_SPECTRE, Z_GLISSE);
    rect(0, YS + HS, 320, 1, LIGNE);

    // informations
    const int YI = YS + HS + 2;
    if (dec::choisi() == dec::FT8) {
        int b = ft8_bande_proche();
        char t[40];
        if (b >= 0) snprintf(t, sizeof t, "Bande %s  ›", f8::BANDES_NOM[b]); else snprintf(t, sizeof t, "Aller sur FT8  ›");
        bouton(4, YI, 120, 20, t, B_DEC_INFO1, b < 0 ? 1 : 0, POLICE_11);
        int s = f8::secondes_avant();
        if (s < 0) snprintf(t, sizeof t, "Heure inconnue");
        else snprintf(t, sizeof t, "Décodage dans %d s", s);
        texte(132, YI + 3, t, POLICE_11, s < 0 ? AMBRE : TXT);
        // barre de la periode de 15 s
        if (s >= 0) { rect(132, YI + 17, 104, 2, LIGNE); rect(132, YI + 17, (15 - s) * 104 / 15, 2, CYAN); }
        if (f8::version()) {
            snprintf(t, sizeof t, "%d msg · %.1f s", f8::derniers(), f8::duree_ms() / 1000.0f);
            texte(314, YI + 3, t, POLICE_11, f8::derniers() ? VERT : TXT2, 255, DROITE);
        }
    } else {
        bouton(4, YI, 120, 20, dec::info1(), B_DEC_INFO1, 0, POLICE_11);
        bouton(128, YI, 110, 20, dec::info2(), B_DEC_INFO2, 0, POLICE_11);
        bool cale = dec::cale();
        disque(250, YI + 10, 4, cale ? VERT : TXT2);
        texte(258, YI + 3, cale ? "Calé" : "Cherche", POLICE_11, cale ? VERT : TXT2);
    }

    // zone de texte ou image
    const int YT = YI + 24, HT = 240 - 34 - YT;
    rect_arrondi(4, YT, 312, HT, 6, NOIR);
    contour_arrondi(4, YT, 312, HT, 6, 1, LIGNE);
    if (dec::choisi() == dec::FT8) {
        ft8_liste(YT, HT);
    } else if (dec::choisi() == dec::FAX) {
        int n = dec::fax_nb_lignes();
        int h = HT - 4;
        for (int r = 0; r < h && r < n; r++) {
            const uint8_t *l = dec::fax_ligne(r);
            uint16_t *dst = fb + (YT + 2 + h - 1 - r) * L;
            for (int x = 6; x < 314; x++) {
                uint8_t v = l[x];
                dst[x] = rgb(v, v, v);
            }
        }
        if (n == 0) texte(160, YT + HT / 2 - 16, "En attente d'une image…", POLICE_13, TXT2, 255, CENTRE);
        texte(160, YT + HT / 2 + 2, n ? "" : "USB, 1,9 kHz sous la fréquence annoncée", POLICE_11, TXT2, 255, CENTRE);
        zone(4, YT, 312, HT, B_DEC_IMAGE, Z_GLISSE);
    } else {
        static char lignes[12][64];
        int nl = couper_lignes(dec::texte(), POLICE_13, 300, lignes, 12);
        int visibles = (HT - 8) / 17;
        int d = nl > visibles ? nl - visibles : 0;
        for (int i = d; i < nl; i++) {
            const char *t = lignes[i % 12];
            int y = YT + 4 + (i - d) * 17;
            int l = texte(10, y, t, POLICE_13, VERT_T);
            if (i == nl - 1 && (t_ms / 400) % 2) rect(11 + l, y + 2, 7, 13, VERT_T, 180);
        }
        if (nl == 0) {
            const char *aide = dec::choisi() == dec::CW ? "Cale le signal CW dans le filtre" : "Signal RTTY : USB ou LSB";
            texte(160, YT + HT / 2 - 8, aide, POLICE_13, TXT2, 255, CENTRE);
        }
    }

    // boutons
    int y = 240 - 31;
    if (dec::choisi() == dec::FT8) {
        bouton(4, y, 100, 27, "Effacer", B_DEC_EFFACER);
        bouton(108, y, 100, 27, "Carte", B_DEC_CARTE, f8::nb() ? 1 : 0);
        bouton(212, y, 104, 27, "Sauver SD", B_DEC_SAUVER, 2);
    } else if (dec::choisi() == dec::RTTY) {
        bouton(4, y, 74, 27, "Effacer", B_DEC_EFFACER);
        bouton(82, y, 74, 27, "Calage", B_DEC_CALER);
        bouton(160, y, 74, 27, "Inverser", B_DEC_INV, dec::inverse() ? 3 : 0);
        bouton(238, y, 78, 27, "Sauver SD", B_DEC_SAUVER, 2);
    } else {
        bouton(4, y, 100, 27, dec::choisi() == dec::FAX ? "Nouvelle" : "Effacer", B_DEC_EFFACER);
        bouton(108, y, 100, 27, "Calage", B_DEC_CALER);
        bouton(212, y, 104, 27, "Sauver SD", B_DEC_SAUVER, 2);
    }
}

// l'ecran de la carte suit les onglets de la page web (reglage enregistre en NVS)
static bool s_suivre_web = true;
static void suivre_web(bool oui)
{
    s_suivre_web = oui;
    uint8_t v = oui ? 1 : 2;                         // 0 = jamais enregistre (actif par defaut)
    plat_nvs_ecrire("suivre_web", &v, 1);
}
bool suit_page_web() { return s_suivre_web; }

// ============================ REGLAGES ======================================
static const int R_ETALONNER = 0, R_VOLUME = 1, R_BP = 2, R_VITESSE = 3, R_LUMIERE = 4, R_AGC = 5, R_PALETTE = 6,
                 R_ETIQ = 7, R_ADPCM = 8, R_TAMPON = 9, R_AMPLI = 10, R_LIMITEUR = 11, R_SUIVRE = 12, R_SCAN = 13, R_MOLETTE = 14,
                 R_SQUELCH = 15, R_SAM = 16, R_REPRISE = 17, R_VOX = 18, R_GRAVES = 19, R_AIGUS = 20, R_NUIT = 21, R_CWTXT = 22, R_SD = 23, R_INFO = 24, NB_R = 25;
static uint32_t t_confirme = 0;     // confirmation (etalonnage, formatage) : 2e toucher dans les 3 s
static int a_confirmer = -1;
static float reg_dec = 0, reg_v = 0;
static const int H_REG = 34;

static void curseur(int x, int y, int l, float t, const char *val, int id)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    rect_arrondi(x, y + 12, l, 6, 3, LIGNE);
    rect_arrondi(x, y + 12, (int)(l * t) + 3, 6, 3, CYAN);
    disque(x + l * t, y + 15, 7, TXT);
    texte(314, y + 8, val, POLICE_13, TXT, 255, DROITE);
    zone(x - 8, y, l + 16, H_REG - 4, id, Z_GLISSE);
    static const int PAS[NB_R] = { 1, 50, 40, 1, 19, 1, 1, 1, 1, 27, 1, 1, 1, 1, 1, 1, 1, 1, 1, 8, 8, 1, 1, 1, 1 };   // un cran = un pas utile
    int i = id - B_REG;
    reg_curseur(id, x, l, t, i >= 0 && i < NB_R ? PAS[i] : 40);
}

static void segments(int x, int y, int l, const char *const *noms, int n, int sel, int id0)
{
    int w = (l - (n - 1) * 4) / n;
    for (int i = 0; i < n; i++) bouton(x + i * (w + 4), y + 4, w, 22, noms[i], id0 + i, i == sel ? 3 : 0, POLICE_11);
}

static void interrupteur(int x, int y, bool on, int id)
{
    rect_arrondi(x, y + 6, 40, 20, 10, on ? VERT : LIGNE);
    disque(x + (on ? 30 : 10), y + 16, 8, TXT);
    zone(x - 10, y, 60, H_REG - 4, id);
}

static void ecran_reglages()
{
    rect(0, 0, 320, 240, FOND);
    entete("Réglages", KIWI_VERSION, TXT2);
    const int y0 = 30;
    int max_dec = NB_R * H_REG - (240 - y0) + 4;
    if (!tc_appui) { reg_dec += reg_v; reg_v *= 0.9f; if (fabsf(reg_v) < 0.2f) reg_v = 0; }
    if (reg_dec < 0) reg_dec = 0;
    if (reg_dec > max_dec) reg_dec = (float)max_dec;
    decoupe(0, y0, 320, 240);
    for (int i = 0; i < NB_R; i++) {
        int y = y0 + i * H_REG - (int)reg_dec;
        if (y > 240 || y + H_REG < y0) continue;
        rect_arrondi(4, y, 312, H_REG - 4, 6, PAN);
        zone(4, y, 312, H_REG - 4, B_REG_LISTE, Z_GLISSE);
        static const char *N[NB_R] = { "Étalonner l'écran", "Volume", "Bande passante", "Vitesse waterfall", "Luminosité",
                                       "AGC", "Palette", "Noms des stations", "Audio compressé", "Réserve audio",
                                       "Ampli actif bas", "Limiteur de volume", "Suit la page web", "Sensibilité scan",
                                       "Bouton rotatif", "Squelch", "AM synchrone", "Reprise automatique", "Enregistrer à la voix",
                                       "Graves", "Aigus", "Mode nuit", "Texte morse (Radio)",
                                       "Carte SD", "Version" };
        texte(12, y + 8, N[i], POLICE_13, TXT);
        char v[40];
        int id = B_REG + i;
        switch (i) {
        case R_VOLUME: snprintf(v, sizeof v, "%d %%", cfg::R.volume); curseur(140, y, 110, cfg::R.volume / 100.0f, v, id); break;
        case R_BP: {
            int m = cfg::R.mode;
            fmt_hz(v, sizeof v, cfg::R.bp[m]);
            curseur(140, y, 110, (float)(cfg::R.bp[m] - cfg::bp_min(m)) / (cfg::bp_max(m) - cfg::bp_min(m)), v, id);
            break;
        }
        case R_VITESSE: { static const char *S[4] = { "1", "2", "3", "4" }; segments(150, y, 162, S, 4, cfg::R.wf_vitesse - 1, B_REG + 26); break; }
        case R_LUMIERE: snprintf(v, sizeof v, "%d %%", cfg::R.luminosite); curseur(140, y, 110, (cfg::R.luminosite - 5) / 95.0f, v, id); break;
        case R_AGC: { static const char *S[4] = { "Off", "Lent", "Moyen", "Rapide" }; segments(110, y, 202, S, 4, cfg::R.agc, B_REG + 30); break; }
        case R_PALETTE:
            for (int p = 0; p < 4; p++) {
                int x = 118 + p * 50;
                for (int k = 0; k < 44; k++) rect(x + k, y + 7, 1, 16, wf::couleur(p, k * 255 / 43));
                if (p == cfg::R.palette) contour_arrondi(x - 2, y + 5, 48, 20, 3, 2, TXT);
                zone(x - 2, y, 48, H_REG - 4, B_REG + 40 + p);
            }
            break;
        case R_ADPCM: texte(250, y + 8, cfg::R.adpcm ? "÷4" : "brut", POLICE_11, TXT2, 255, DROITE); interrupteur(266, y, cfg::R.adpcm, id); break;
        case R_TAMPON: snprintf(v, sizeof v, "%d ms", cfg::R.tampon_ms); curseur(140, y, 110, (cfg::R.tampon_ms - 150) / 1350.0f, v, id); break;
        case R_AMPLI: interrupteur(266, y, cfg::R.ampli_bas, id); break;
        case R_LIMITEUR: interrupteur(266, y, cfg::R.limiteur, id); break;
        case R_SUIVRE: interrupteur(266, y, s_suivre_web, id); break;
        case R_SCAN: segments(150, y, 162, NOMS_SENSIB, 3, sc_sensibilite, B_SCAN_SENS); break;
        case R_MOLETTE: { static const char *const M[3] = { "Arrêt", "Normal", "Inversé" };
                          segments(150, y, 162, M, 3, mol_reglage, B_MOL_REG); break; }
        case R_SQUELCH: { static const char *const S[4] = { "Arrêt", "Bas", "Moyen", "Haut" }; segments(110, y, 202, S, 4, opt.squelch, B_SQUELCH); break; }
        case R_SAM: { static const char *const S[4] = { "Arrêt", "2 bandes", "Haute", "Basse" }; segments(110, y, 202, S, 4, opt.sam > 3 ? 1 : opt.sam, B_SAM_SEG); break; }
        case R_REPRISE: interrupteur(266, y, opt.reprise, B_OPT_REPRISE); break;
        case R_VOX: interrupteur(266, y, opt.vox, B_OPT_VOX); break;
        case R_GRAVES: snprintf(v, sizeof v, "%+d dB", opt2.graves); curseur(140, y, 110, (opt2.graves + 8) / 16.0f, v, id); break;
        case R_AIGUS: snprintf(v, sizeof v, "%+d dB", opt2.aigus); curseur(140, y, 110, (opt2.aigus + 8) / 16.0f, v, id); break;
        case R_NUIT: { static const char *const S[4] = { "Arrêt", "1 min", "5 min", "15 min" }; segments(110, y, 202, S, 4, opt2.nuit, B_NUIT); break; }
        case R_CWTXT: texte(250, y + 8, "en CW", POLICE_11, TXT2, 255, DROITE); interrupteur(266, y, !opt2.sans_cw, B_OPT_CWTXT); break;
        case R_ETALONNER: {
            bool c = a_confirmer == R_ETALONNER && t_ms - t_confirme < 3000;
            bouton(204, y + 3, 108, 24, c ? "Confirmer ?" : "Lancer", id, c ? 2 : 0);
            break;
        }
        case R_ETIQ: { static const char *S[4] = { "Non", "EiBi", "Commu.", "Propr." }; segments(150, y, 162, S, 4, cfg::R.etiquettes, B_REG + 50); break; }
        case R_SD: {
            texte(150, y + 9, tronquer(plat_sd_etat(), POLICE_11, plat_sd() ? 160 : 70), POLICE_11, plat_sd() ? VERT : AMBRE);
            if (!plat_sd()) {
                bool c = a_confirmer == R_SD && t_ms - t_confirme < 3000;
                bouton(224, y + 3, 88, 24, c ? "Confirmer ?" : "Formater", id, c ? 2 : 0);
            }
            break;
        }
        case R_INFO: texte(308, y + 9, KIWI_VERSION " · ES3C28P", POLICE_13, CYAN, 255, DROITE); break;
        }
    }
    sans_decoupe();
}

static void reglage_curseur(int i, int x)
{
    float t = (x - 140) / 110.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    switch (i) {
    case R_VOLUME: cfg::R.volume = (uint8_t)lroundf(t * 100); plat_volume(cfg::R.volume); break;
    case R_BP: {
        int m = cfg::R.mode;
        int v = cfg::bp_min(m) + (int)(t * (cfg::bp_max(m) - cfg::bp_min(m)));
        v = (v + 50) / 100 * 100;
        if (v != cfg::R.bp[m]) { cfg::R.bp[m] = (uint32_t)v; kw::regler(); }
        break;
    }
    case R_LUMIERE: cfg::R.luminosite = (uint8_t)(5 + lroundf(t * 95)); plat_luminosite(cfg::R.luminosite); break;
    case R_TAMPON: cfg::R.tampon_ms = (uint16_t)((150 + (int)(t * 1350)) / 50 * 50); aud::reserve_visee(cfg::R.tampon_ms); break;
    case R_GRAVES: case R_AIGUS: {                      // -8 .. +8 dB par pas de 2, aimante sur 0
        int d = (int)lroundf((t * 16 - 8) / 2) * 2;
        int8_t &val = i == R_GRAVES ? opt2.graves : opt2.aigus;
        if (d != val) { val = (int8_t)d; opt2_appliquer(); opt2_sauver(); }
        return;
    }
    default: return;
    }
    cfg::marquer();
}

static void regler_option(const char *nom, int v)
{
    if (!strcmp(nom, "sq")) opt.squelch = (uint8_t)(v < 0 ? 0 : (v > 3 ? 3 : v));
    else if (!strcmp(nom, "sam")) {
        opt.sam = (uint8_t)(v < 0 ? 0 : (v > 3 ? 3 : v));
        if (opt.sam && cfg::R.proto != cfg::KIWI) message("AM synchrone : sur un KiwiSDR seulement");
    }
    else if (!strcmp(nom, "reprise")) opt.reprise = v ? 1 : 0;
    else if (!strcmp(nom, "vox")) {
        if (mot::enregistre()) { message("Arrête d'abord l'enregistrement"); return; }
        opt.vox = v ? 1 : 0;
    }
    else if (!strcmp(nom, "cwtxt")) { opt2.sans_cw = v ? 0 : 1; opt2_sauver(); return; }
    else if (!strcmp(nom, "nuit")) { opt2.nuit = (uint8_t)(v < 0 ? 0 : (v > 3 ? 3 : v)); opt2_sauver(); t_geste = t_ms; return; }
    else if (!strcmp(nom, "graves")) { opt2.graves = (int8_t)(v < -8 ? -8 : (v > 8 ? 8 : v)); opt2_sauver(); opt2_appliquer(); return; }
    else if (!strcmp(nom, "aigus")) { opt2.aigus = (int8_t)(v < -8 ? -8 : (v > 8 ? 8 : v)); opt2_sauver(); opt2_appliquer(); return; }
    else return;
    opt_sauver();
    opt_appliquer();
}

// ============================ FAVORIS =======================================
static float fav_dec = 0, fav_v = 0;

// distance de chaque favori (son recepteur cherche dans les listes KiwiSDR et VHF/UHF) ; 65000 = inconnue
static uint16_t fav_km[MAX_FAV];
static void maj_fav_km()
{
    static const srv::Serveur **vk = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    static const owl::RecepteurVHF **vv = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    int nk = 0;                                         // toutes les regions (la vue "Proches" est limitee a 40)
    for (int r = 0; vk && r < srv::NB_REGIONS; r++)
        if (r != srv::PROCHES) nk += srv::filtrer(r, vk + nk, srv::MAX_PUBLICS + srv::MAX_PERSO - nk);
    int nv = vv ? owl::chercher("", vv, owl::MAX_VHF) : 0;
    for (int i = 0; i < nfav; i++) {
        fav_km[i] = 65000;
        if (!favs[i].hote[0]) continue;
        if (favs[i].libre & 1) { for (int k = 0; k < nv; k++) if (strstr(vv[k]->url, favs[i].hote)) { fav_km[i] = vv[k]->km; break; } }
        else for (int k = 0; k < nk; k++) if (!strcmp(vk[k]->hote, favs[i].hote)) { fav_km[i] = vk[k]->km; break; }
    }
}

static void ecran_favoris()
{
    rect(0, 0, 320, 240, FOND);
    { static uint32_t t = 0; if (!t || t_ms - t > 3000) { t = t_ms ? t_ms : 1; maj_fav_km(); } }
    char n[24];
    snprintf(n, sizeof n, "%d / %d", nfav, MAX_FAV);
    entete("Favoris");
    texte(226, 8, n, POLICE_11, TXT2, 255, DROITE);
    bouton(232, 3, 82, 20, sc_liste && sl_fav ? "Arrêter" : "Scanner", B_FAV_SCAN, sc_liste && sl_fav ? 2 : 1, POLICE_13);
    const int y0 = 30, h = 36, hp = 30;
    int total = (nfav ? nfav * h : 50) + 24 + NB_PRESETS * hp;
    int max_dec = total - (240 - y0) + 4;
    if (max_dec < 0) max_dec = 0;
    if (!tc_appui) { fav_dec += fav_v; fav_v *= 0.9f; }
    if (fav_dec < 0) fav_dec = 0;
    if (fav_dec > max_dec) fav_dec = (float)max_dec;
    decoupe(0, y0, 320, 240);
    int y = y0 - (int)fav_dec;
    for (int i = 0; i < nfav; i++, y += h) {
        if (y > 240 || y + h < y0) continue;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_FAV_LIGNE + i;
        rect_arrondi(4, y, 312, h - 4, 6, ap ? melange(PAN, TXT, 40) : PAN);
        char f[24];
        fmt_freq_aff(f, sizeof f, favs[i].f);
        texte(12, y + 6, f, POLICE_22, AMBRE);
        rect_arrondi(128, y + 8, 40, 16, 4, PUCE);
        texte(148, y + 8, cfg::nom_mode(favs[i].mode), POLICE_11, PUCE_T, 255, CENTRE);
        texte(176, y + 2, nom_bande(favs[i].f), POLICE_11, TXT2);
        char nr[72];
        const char *nb = favs[i].nom[0] ? favs[i].nom : (favs[i].hote[0] ? favs[i].hote : "récepteur courant");
        if (fav_km[i] < 65000) { char k[16]; snprintf(k, sizeof k, "%u km · ", fav_km[i]); snprintf(nr, sizeof nr, "%s%s", k, nb); }
        else snprintf(nr, sizeof nr, "%s", nb);
        texte(176, y + 16, tronquer(nr, POLICE_11, 132), POLICE_11, TXT);
        zone(4, y, 312, h - 4, B_FAV_LIGNE + i, Z_GLISSE | Z_LONG);
    }
    if (!nfav) {
        texte(160, y + 6, "Aucun favori : touche l'étoile sur l'écran Radio", POLICE_11, TXT2, 255, CENTRE);
        texte(160, y + 22, "(appui long sur un favori pour le retirer)", POLICE_11, TXT2, 255, CENTRE);
        y += 50;
    }
    texte(8, y + 6, "Préréglages · stations fixes", POLICE_13, CYAN);
    y += 24;
    for (int i = 0; i < NB_PRESETS; i++, y += hp) {
        if (y > 240 || y + hp < y0) continue;
        bool ap = tc_appui && !tc_glisse && tc_z.id == B_PRESET + i;
        rect_arrondi(4, y, 312, hp - 4, 6, ap ? melange(PAN, TXT, 40) : PAN);
        char f[24];
        fmt_freq(f, sizeof f, PRESETS[i].f, PRESETS[i].f < 1000 ? 1 : 1);
        texte(12, y + 5, f, POLICE_16, AMBRE);
        texte(100, y + 6, cfg::nom_mode(PRESETS[i].mode), POLICE_11, PUCE_T);
        texte(134, y + 6, tronquer(PRESETS[i].nom, POLICE_11, 176), POLICE_11, TXT);
        zone(4, y, 312, hp - 4, B_PRESET + i, Z_GLISSE);
    }
    sans_decoupe();
}

static void ouvrir_favori(int i)
{
    if (i < 0 || i >= nfav) return;
    Favori &f = favs[i];
    cfg::R.mode = f.mode;
    cfg::R.bp[f.mode] = f.bp ? f.bp : cfg::R.bp[f.mode];
    accorder(f.f);
    bool owrx = f.libre & 1;
    bool autre = f.hote[0] && (strcmp(f.hote, cfg::R.hote) || f.port != cfg::R.port || owrx != (cfg::R.proto == cfg::OWRX));
    if (autre || (f.hote[0] && kw::etat() != kw::ACTIF)) {
        if (owrx) connecter(f.hote, f.port, "", f.nom, cfg::OWRX, (f.libre & 2) != 0, f.chemin);
        else connecter(f.hote, f.port, f.mdp, f.nom);
        cfg::R.freq_khz = f.f;                          // la connexion a pu remettre la frequence a zero
    }
    aller(E_RADIO);
}

// ============================ WIFI ==========================================
static void ecran_wifi()
{
    rect(0, 0, 320, 240, FOND);
    entete("WiFi et portail web");
    int w = plat_wifi_etat();
    static const char *ETATS[4] = { "Hors ligne", "Connexion…", "Connecté", "Point d'accès" };
    char l[7][2][64];
    snprintf(l[0][0], 64, "État");          snprintf(l[0][1], 64, "%s", ETATS[w & 3]);
    snprintf(l[1][0], 64, "Réseau");        snprintf(l[1][1], 64, "%s", plat_wifi_nom());
    snprintf(l[2][0], 64, "Adresse");       snprintf(l[2][1], 64, "%s", plat_wifi_ip());
    snprintf(l[3][0], 64, "Signal WiFi");   if (w == WIFI_OK) snprintf(l[3][1], 64, "%d dBm", plat_wifi_rssi()); else snprintf(l[3][1], 64, "–");
    snprintf(l[4][0], 64, "Flux · écran");  snprintf(l[4][1], 64, "%u,%u ko/s · %d images/s", (unsigned)(kw::debit_octets() / 1000), (unsigned)(kw::debit_octets() % 1000 / 100), (int)(s_ips + 0.5f));
    if (kw::etat() == kw::ACTIF) {
        snprintf(l[5][0], 64, "Réserve son"); snprintf(l[5][1], 64, "%d ms%s · %u coupures", aud::reserve_ms(), aud::en_lecture() ? "" : " (remplissage)", (unsigned)aud::coupures());
    } else {
        snprintf(l[5][0], 64, "Réserve son"); snprintf(l[5][1], 64, "– · %u coupures", (unsigned)aud::coupures());
    }
    snprintf(l[6][0], 64, "Écran tactile");
    snprintf(l[6][1], 64, "%s", doigts_max >= 2 ? "2 doigts vus : pincement OK" : "pose 2 doigts pour tester");
    rect_arrondi(6, 30, 308, 126, 8, PAN);
    for (int i = 0; i < 7; i++) {
        texte(16, 34 + i * 17, l[i][0], POLICE_13, TXT2);
        texte(304, 34 + i * 17, tronquer(l[i][1], POLICE_13, 200), POLICE_13, i == 6 && doigts_max >= 2 ? VERT : TXT, 255, DROITE);
    }
    rect_arrondi(6, 160, 308, 40, 8, PUCE);
    char u[64];
    snprintf(u, sizeof u, "http://%s/", plat_wifi_ip());
    texte(160, 164, "Portail : ouvre cette adresse sur ton téléphone", POLICE_11, PUCE_T, 255, CENTRE);
    texte(160, 179, u, POLICE_16, TXT, 255, CENTRE);
    bouton(6, 206, 308, 28, "Oublier ce WiFi (appui long)", B_OUBLIER_WIFI, 0, POLICE_13, Z_LONG);
}

// ============================ Navigation ====================================
static int ecran_prec = E_RADIO;
static int vol_avant_muet = 70;
static uint32_t t_repet = 0;

static void ouvrir(int e)
{
    if (e != ecran) ecran_prec = ecran;
    aller(e);
    if (e == E_RECEPTEURS) {
        liste_dec = 0;
        liste_v = 0;
        liste_vhf = cfg::R.proto == cfg::OWRX;
        if (liste_proches) ouvrir_proches();
        if (srv::nb_publics() == 0 && srv::nb_perso() > 0) region = srv::PERSO;
        if (plat_wifi_etat() == WIFI_OK && !plat_liste_en_cours() && srv::age_liste_ms() > 15u * 60u * 1000u)
            plat_liste_telecharger();
        maj_vue();
    }
    if (e == E_FREQ) saisie[0] = 0;
    if (e == E_CANAUX) { liste_dec = 0; liste_v = 0; maj_vue_can(); }
    if (e == E_QUOI) {
        liste_dec = 0; liste_v = 0;
        if (ecran_prec != E_FREQ) {                     // categorie du recepteur ecoute (sauf au retour du clavier)
            if (kw::etat() == kw::ACTIF && cfg::R.proto == cfg::OWRX) q_cat = cfg::R.freq_khz < 30000 ? quoi::OC : (cfg::R.freq_khz < 300000 ? quoi::VHF : quoi::UHF);
            else if (kw::etat() == kw::ACTIF) q_cat = quoi::OC;
        }
        q_maj();
    }
    if (e == E_FM_TROUVEES) { liste_dec = 0; liste_v = 0; }
    if (e == E_FM_LISTE) {
        liste_dec = 0;
        liste_v = 0;
        if (!fml::nb() && plat_wifi_etat() == WIFI_OK && !plat_fm_liste_en_cours()) plat_fm_liste_telecharger();
        maj_vue_fm();
    }
}

static void retour()
{
    int e = ecran == E_MENU ? E_RADIO : (ecran_prec == ecran ? E_RADIO : ecran_prec);
    ecran_prec = E_RADIO;
    aller(e);
}

// ============================ Gestes ========================================
// gain du son KiwiSDR (0, +6, +12 dB), enregistre en NVS
static void regler_gain(int n)
{
    aud::gain_kiwi(n);
    uint8_t v = (uint8_t)(n + 1);                  // 0 = jamais enregistre
    plat_nvs_ecrire("gain_kiwi", &v, 1);
}

static void reglage_volume(int x)
{
    float t = (x - 86) / 180.0f;
    int v = (int)lroundf((t < 0 ? 0 : (t > 1 ? 1 : t)) * 100);
    if (v != cfg::R.volume) { cfg::R.volume = (uint8_t)v; plat_volume(v); cfg::marquer(); }
}

static void action(const Zone &z, int x, int y);

static bool cw_accrocher(double f_khz, double rayon_khz, bool annoncer);
static void presse(const Zone &z, int x, int y)
{
    if (z.id == B_BP_CURS) { bp_curseur(x); return; }
    if (z.id == B_BP_DECAL) { bp_decalage(x); return; }
    if (z.fl & Z_APPUI) { action(z, x, y); return; }      // reponse immediate
    if (z.id == B_CASCADE || z.id == B_FREQ) glisse_f0 = cfg::R.freq_khz;
    if (z.id == B_VOL_BORD) { glisse_vol0 = cfg::R.volume; t_vol_bord = t_ms; }
    if (z.id == B_FM_FREQ) { fm::Info i; fm::info(&i); glisse_mhz = i.mhz; }
    if (z.id >= B_REG && z.id < B_REG + NB_R) reglage_curseur(z.id - B_REG, x);
}

static void glisse(const Zone &z, int x, int y, int dx, int dy)
{
    (void)y;
    if (z.id == B_WR_VOL) {
        reglage_volume(86 + (x - 250) * 180 / 62);
    } else if (z.id == B_AUD_VOL) {
        reglage_volume(x);
    } else if (z.id == B_AUD_SEUIL) {
        reglage_seuil(x);
    } else if (z.id == B_BP_CURS) {
        bp_curseur(x);
    } else if (z.id == B_BP_DECAL) {
        bp_decalage(x);
    } else if (z.id == B_VOL_BORD) {
        int v = glisse_vol0 + (tc_y0 - y) * 100 / 110;     // 110 pixels = 0 a 100 %
        v = v < 0 ? 0 : (v > 100 ? 100 : v);
        if (v != cfg::R.volume) { cfg::R.volume = (uint8_t)v; plat_volume(v); cfg::marquer(); }
        t_vol_bord = t_ms;
    } else if (z.id == B_FREQ) {
        int pas = cfg::PAS_HZ[cfg::R.pas];
        int n = (x - tc_x0) / 10;                       // un pas tous les 10 pixels
        accorder(arrondi_pas(glisse_f0, pas) + n * pas / 1000.0);
    } else if (z.id == B_CASCADE) {
        double f = glisse_f0 - (x - tc_x0) * khz_par_px();
        int p = cfg::PAS_HZ[cfg::R.pas] < 100 ? cfg::PAS_HZ[cfg::R.pas] : 100;
        accorder(arrondi_pas(f, p));
    } else if (z.id == B_FM_FREQ) {
        int n = (x - tc_x0) / 12;                       // 0,1 MHz tous les 12 pixels
        if (glisse_mhz > 0 && n) {
            float f = roundf(glisse_mhz * 10) / 10 + n * 0.1f;
            fm::accorder(f < 64 ? 64 : (f > 108 ? 108 : f));
        }
    } else if (z.id == B_CARTE) {
        carte_glisser(dx, dy);
    } else if (z.id >= B_LISTE && z.id < B_LISTE + 40) {
        liste_dec -= dy;
        liste_v = -(float)dy;
    } else if (z.id == B_REG_LISTE) {
        reg_dec -= dy;
        reg_v = -(float)dy;
    } else if ((z.id >= B_FAV_LIGNE && z.id < B_FAV_LIGNE + MAX_FAV) || (z.id >= B_PRESET && z.id < B_PRESET + NB_PRESETS)) {
        fav_dec -= dy;
        fav_v = -(float)dy;
    } else if (z.id >= B_REG && z.id < B_REG + NB_R) {
        reglage_curseur(z.id - B_REG, x);
    } else if (z.id == B_DEC_SPECTRE) {
        dec::regler_tonalite(x * dec::hz_par_point() * dec::SP_N / 320.0f);
    } else if (z.id == B_DEC_IMAGE) {
        dec::fax_decaler(-dx);
    } else if (z.id == B_DEC_LISTE) {                   // le doigt descend : on remonte vers les plus anciens
        ft8_defil_px -= dy;
        int k = (int)(ft8_defil_px / 15);
        if (k) { ft8_defil += k; ft8_defil_px -= k * 15; if (ft8_defil < 0) ft8_defil = 0; }
    }
}

static void appui_long(const Zone &z)
{
    switch (z.id) {
    case B_FREQ: fav_ajouter(); break;
    case B_FAV: ouvrir(E_FAVORIS); break;
    case B_BP_ORIG: bp_origine(true); bp_popup_jusqua = t_ms + 5000; break;
    case B_OUBLIER_WIFI: message("WiFi oublié : redémarrage en point d'accès"); plat_wifi_oublier(); break;
    case B_TOUCHE + 11: saisie[0] = 0; break;
    default:
        if (z.id >= B_FAV_LIGNE && z.id < B_FAV_LIGNE + MAX_FAV) {
            int i = z.id - B_FAV_LIGNE;
            if (i < nfav) {
                for (int k = i; k < nfav - 1; k++) favs[k] = favs[k + 1];
                nfav--;
                fav_sauver();
                message("Favori retiré");
            }
        } else if (z.id >= B_LISTE && z.id < B_LISTE + 40) {
            int i = premier_liste + z.id - B_LISTE;
            if (i < nvue && vue[i]->perso) {
                for (int k = 0; k < srv::nb_perso(); k++)
                    if (srv::perso(k) == vue[i]) { srv::retirer_perso(k); message("Récepteur perso retiré"); break; }
                maj_vue();
            }
        }
        break;
    }
}

static void action(const Zone &z, int x, int y)
{
    (void)y;
    int id = z.id;
    switch (id) {
    case B_RETOUR: retour(); return;
    case B_MENU: panneau_audio = false; ouvrir(E_MENU); return;
    case B_MOINS: if (ecran == E_FM) fm_pas(-1); else pas_freq(-1); return;
    case B_PLUS: if (ecran == E_FM) fm_pas(+1); else pas_freq(+1); return;
    case B_FM_SERVEURS: case B_FM_CHOISIR: ouvrir(E_FM_LISTE); return;
    case B_FM_ARRET: fm::deconnecter(); return;
    case B_FM_MEILLEUR: fb_lancer(); return;
    case B_FM_WEB: ouvrir(E_WEBRADIO); return;
    case B_WR_FM: ouvrir(ecran == E_WEBRADIO ? E_FM_LISTE : E_FM); return;
    case B_WR_STOP: wr_arreter(); return;
    case B_WR_FAV: wr::basculer_favori(wr::en_cours()); return;
    case B_WR_VOL: reglage_volume(86 + (x - 250) * 180 / 62); return;
    case B_MODE: changer_mode(cfg::R.mode + 1); return;
    case B_BANDE: case B_FREQ: ouvrir(E_FREQ); return;
    case B_FAV: fav_basculer(); return;
    case B_AUDIO: panneau_audio = !panneau_audio; return;
    case B_AUD_FOND: return;
    case B_AUD_VOL: reglage_volume(x); return;
    case B_AUD_SEUIL: reglage_seuil(x); return;
    case B_AUD_NOTCH: cfg::R.notch = !cfg::R.notch; cfg::marquer(); kw::regler_bruit(); return;
    case B_AUD_NB: cfg::R.nb = (uint8_t)((cfg::R.nb + 1) % 4); cfg::marquer(); kw::regler_bruit(); return;   // arret, faible, moyen, fort
    case B_AUD_GAIN: regler_gain((aud::gain_kiwi() + 1) % 3); return;
    case B_PROCHE: {                                   // recepteur libre le plus proche de moi (sauf celui qui a echoue)
        uint16_t km = 0;
        if (cfg::R.proto == cfg::OWRX) {               // le recepteur VHF/UHF le plus proche, autre que celui-ci
            static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
            if (!v) return;
            int n = owl::chercher("", v, owl::MAX_VHF);
            for (int i = 0; i < n; i++)
                if (!strstr(v[i]->url, cfg::R.hote)) { connecter_url(v[i]->url, v[i]->nom); return; }
            message("Liste VHF/UHF vide : ouvre Récepteurs");
            return;
        }
        const srv::Serveur *r = srv::plus_proche_libre(cfg::R.lat, cfg::R.lon, &km, cfg::R.hote);
        if (!r) { message("Liste des récepteurs vide : ouvre Récepteurs"); return; }
        connecter(r->hote, r->port, "", r->nom);
        return;
    }
    case B_PAS: cfg::R.pas = (uint8_t)((cfg::R.pas + 1) % cfg::NB_PAS); cfg::marquer(); return;
    case B_ZOOM_M: zoom(-1); return;
    case B_ZOOM_P: zoom(+1); return;
    case B_CASCADE:
        if (cfg::R.mode == cfg::CW) {                   // en CW : on accroche le trait le plus proche du doigt
            double r = 12 * kw::cascade_largeur_khz() / wf::L;
            if (r < 0.15) r = 0.15;
            if (cw_accrocher(f_de_x((float)x), r, false)) return;
        }
        accorder(arrondi_pas(f_de_x((float)x), cfg::PAS_HZ[cfg::R.pas]));
        return;
    case B_VOLUME:
        if (cfg::R.volume) { vol_avant_muet = cfg::R.volume; cfg::R.volume = 0; }
        else cfg::R.volume = (uint8_t)(vol_avant_muet ? vol_avant_muet : 50);
        plat_volume(cfg::R.volume);
        cfg::marquer();
        return;
    case B_SERVEUR:
        if (kw::etat() == kw::ACTIF || !cfg::R.hote[0]) ouvrir(E_RECEPTEURS);
        else reconnecter();
        return;
    case B_CHOISIR: ouvrir(E_RECEPTEURS); return;
    case B_REESSAYER: reconnecter(); return;
    case B_ACTUALISER:
        if (ecran == E_FM_LISTE) {
            if (plat_wifi_etat() != WIFI_OK) message("Pas de connexion Internet");
            else if (!plat_fm_liste_en_cours()) plat_fm_liste_telecharger();
            return;
        }
        if (plat_wifi_etat() != WIFI_OK) message("Pas de connexion Internet");
        else if (ecran == E_RECEPTEURS && liste_proches) {
            if (!plat_liste_en_cours()) plat_liste_telecharger();
            if (!plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
            if (!plat_fm_liste_en_cours()) plat_fm_liste_telecharger();
        }
        else if (ecran == E_RECEPTEURS && liste_vhf) { if (!plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger(); }
        else if (!plat_liste_en_cours()) plat_liste_telecharger();
        return;
    case B_DEC_EFFACER: if (dec::choisi() == dec::FAX) dec::caler(); else if (dec::choisi() == dec::FT8) { f8::effacer(); ft8_defil = 0; } else dec::effacer(); return;
    case B_DEC_CARTE:
        carte_vhf = false; carte_k = nullptr; carte_v = nullptr; carte_ft8 = true;
        if (!carte_placee) { carte_placee = true; carte_clon = cfg::R.lon; carte_clat = cfg::R.lat; }
        carte_niv = 0;
        ouvrir(E_CARTE);
        return;
    case B_DEC_CALER: dec::caler(); return;
    case B_DEC_INV: dec::inverser(); return;
    case B_DEC_SAUVER: mot::sauver_decodage(); return;
    case B_DEC_INFO1: if (dec::choisi() == dec::RTTY) dec::vitesse_suivante(); else if (dec::choisi() == dec::FT8) ft8_bande_suivante(); return;
    case B_DEC_INFO2: if (dec::choisi() == dec::RTTY) dec::shift_suivant(); return;
    case B_DEC_SPECTRE: dec::regler_tonalite(x * dec::hz_par_point() * dec::SP_N / 320.0f); return;
    }
    if (id >= B_ETIQ && id < B_ETIQ + kw::MAX_MARQUEURS) {
        int i = id - B_ETIQ;
        if (i < nmk) {
            cfg::R.mode = mk[i].mode;
            cfg::R.pas = (uint8_t)cfg::pas_par_defaut(cfg::R.mode, cfg::R.freq_khz);
            accorder(mk[i].khz);
            char m[64];
            snprintf(m, sizeof m, "%s", mk[i].nom);
            message(m);
        }
        return;
    }
    if (id >= B_PRESET && id < B_PRESET + NB_PRESETS) { rappeler_preset(id - B_PRESET); return; }
    if (id >= B_AUD_AGC && id < B_AUD_AGC + 4) { cfg::R.agc = (uint8_t)(id - B_AUD_AGC); cfg::marquer(); kw::regler_agc(); return; }
    if (id >= B_AUD_NR && id < B_AUD_NR + 3) { cfg::R.nr = (uint8_t)(id - B_AUD_NR); cfg::marquer(); kw::regler_bruit(); return; }
    if (id >= B_TUILE && id < B_TUILE + 9) {
        static const int E[9] = { E_RADIO, E_RECEPTEURS, E_QUOI, E_FAVORIS, E_DECODEURS, -1, E_REGLAGES, E_WIFI, E_FM };
        int i = id - B_TUILE;
        if (i == 8 && wr::actif() && !fm::actif()) { ouvrir(E_WEBRADIO); return; }
        if (i == 5) {
            if (!mot::enregistre() && kw::etat() != kw::ACTIF) message("Connecte-toi d'abord à un récepteur");
            else mot::enregistrer(!mot::enregistre());
        } else {
            ouvrir(E[i]);
        }
        return;
    }
    if (id >= B_REGION && id < B_REGION + (int)srv::NB_REGIONS) {
        region = id - B_REGION;
        liste_dec = 0;
        liste_v = 0;
        maj_vue();
        return;
    }
    if (id >= B_WR_RAPIDE && id < B_WR_RAPIDE + wr::NB_RAPIDES) { wr_choisir_rapide(id - B_WR_RAPIDE); return; }
    if (id >= B_WR_ETOILE && id < B_WR_ETOILE + 10) { int i = premier_liste + id - B_WR_ETOILE; if (i < wr_nb()) { wr::Station s = wr_st(i); wr::basculer_favori(s); } return; }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_WEBRADIO) {
        int i = premier_liste + id - B_LISTE;
        if (i < wr_nb()) { wr::Station s = wr_st(i); wr_jouer(s); }
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_FM_LISTE) {
        int i = premier_liste + id - B_LISTE;
        if (i >= nvue_fm) return;
        const fml::ServeurFM *s = vue_fm[i];
        if (s->etat == 0) message("Serveur hors ligne : nouvel essai quand même");
        else if (s->etat == 2) message("Serveur verrouillé : écoute seule");
        kw::deconnecter();
        fm::connecter(s->url, s->nom);
        aller(E_FM);
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_CANAUX) {
        int i = premier_liste + id - B_LISTE;
        if (v_can != vhf::version()) { maj_vue_can(); return; }      // liste refaite entre-temps : on ne regle pas au hasard
        if (i < nvue_can) regler_canal(vue_can[i]->khz, vue_can[i]->mode);
        return;
    }
    if (id >= B_CAN_CAT && id < B_CAN_CAT + 5) { can_cat = id - B_CAN_CAT; liste_dec = 0; liste_v = 0; maj_vue_can(); return; }
    if (id == B_CANAUX) { ouvrir(E_CANAUX); return; }
    if (id == B_QUOI) { ouvrir(E_QUOI); return; }
    if (id == B_CARTE_OUVRIR) {
        carte_vhf = liste_vhf && !liste_proches; carte_k = nullptr; carte_v = nullptr; carte_ft8 = false;
        if (!carte_placee) { carte_placee = true; carte_clon = cfg::R.lon; carte_clat = cfg::R.lat; carte_niv = 0; }   // le zoom part de chez moi
        ouvrir(E_CARTE);
        return;
    }
    if (id == B_CARTE) { carte_toucher(x, y); return; }
    if (id == B_CARTE_KIWI || id == B_CARTE_VHF) {
        carte_vhf = id == B_CARTE_VHF; carte_k = nullptr; carte_v = nullptr;
        if (carte_vhf && !owl::nb() && plat_wifi_etat() == WIFI_OK && !plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
        return;
    }
    if (id == B_CARTE_MONDE) { carte_niv = 0; return; }
    if (id == B_CARTE_PLUS) { carte_zoomer(1); return; }
    if (id == B_CARTE_MOINS) { carte_zoomer(-1); return; }
    if (id == B_CARTE_ECOUTER) { carte_ecouter(); return; }
    if (id == B_MEILLEUR) { if (ecran != E_RADIO) aller(E_RADIO); mr_lancer(); return; }
    if (id >= B_Q_CAT && id < B_Q_CAT + 4) {
        if (qm_phase) q_mesure_fin(nullptr);
        q_cat = id - B_Q_CAT; liste_dec = 0; liste_v = 0; q_maj();
        return;
    }
    if (id == B_Q_MESURER) { q_mesurer(); return; }
    if (id == B_Q_RECEPTEURS) {                         // la liste de recepteurs qui convient a la categorie
        bool vhf_voulu = q_cat == quoi::VHF || q_cat == quoi::UHF;
        ouvrir(E_RECEPTEURS);
        liste_proches = q_cat == quoi::TOUT;
        if (liste_proches) ouvrir_proches();
        else {
            liste_vhf = vhf_voulu;
            maj_vue();
            if (liste_vhf && !owl::nb() && plat_wifi_etat() == WIFI_OK && !plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
        }
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_QUOI) { q_ecouter(premier_liste + id - B_LISTE); return; }
    if (id == B_CAN_ICI) { can_ici = !can_ici; liste_dec = 0; liste_v = 0; maj_vue_can(); return; }
    if (id == B_FMSCAN_BAS || id == B_FMSCAN_HAUT) { fs_appui(id == B_FMSCAN_HAUT ? 1 : -1); return; }
    if (id == B_FM_TROUVEES) { ouvrir(E_FM_TROUVEES); return; }
    if (id == B_FMT_EFFACER) { nfmt = 0; return; }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_FM_TROUVEES) {
        int i = premier_liste + id - B_LISTE;
        if (i < nfmt) { fm::accorder(fmt[i].mhz); aller(E_FM); }
        return;
    }
    if (id == B_CAN_SCAN) {
        if (sc_liste) scan_arreter();
        else { scan_liste_lancer(cats_de(can_cat)); if (sc_liste) aller(E_RADIO); }
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40 && liste_vhf && !liste_proches && ecran == E_RECEPTEURS) {
        int i = premier_liste + id - B_LISTE;
        if (i >= nvue_vhf) return;
        if (connecter_url(vue_vhf[i]->url, vue_vhf[i]->nom)) aller(E_RADIO);
        return;
    }
    if (id == B_BP) { if (ecran == E_RADIO && !bp_popup_jusqua) bp_popup_jusqua = t_ms + 5000; else if (bp_popup_jusqua) bp_popup_jusqua = t_ms + 5000; return; }
    if (id == B_BP_ORIG) { bp_origine(false); bp_popup_jusqua = t_ms + 5000; return; }
    if (id >= B_BP_PRE0 && id <= B_BP_PRE2) { bp_regler(bp_rapides(cfg::R.mode)[id - B_BP_PRE0]); bp_popup_jusqua = t_ms + 5000; return; }
    if (id == B_LISTE_PROCHES) { ouvrir_proches(); return; }
    if (id == B_LOCALISER) {
        if (plat_wifi_etat() != WIFI_OK) message("Pas de connexion Internet");
        else if (!geo::en_cours()) geo::localiser();
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40 && ecran == E_RECEPTEURS && liste_proches) {
        int i = premier_liste + id - B_LISTE;
        if (i >= nproches || proches[i].type == 0) return;
        const LigneProche &l = proches[i];
        if (l.type == 1) { const srv::Serveur *r = (const srv::Serveur *)l.p; connecter(r->hote, r->port, r->mdp, r->nom); aller(E_RADIO); }
        else if (l.type == 2) { const owl::RecepteurVHF *r = (const owl::RecepteurVHF *)l.p; if (connecter_url(r->url, r->nom)) aller(E_RADIO); }
        else { const fml::ServeurFM *r = (const fml::ServeurFM *)l.p; kw::deconnecter(); fm::connecter(r->url, r->nom); aller(E_FM); }
        return;
    }
    if (id == B_LISTE_HF || id == B_LISTE_VHF) {
        liste_proches = false;
        liste_vhf = id == B_LISTE_VHF;
        liste_dec = 0;
        liste_v = 0;
        maj_vue();
        if (liste_vhf && !owl::nb() && plat_wifi_etat() == WIFI_OK && !plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
        return;
    }
    if (id >= B_LISTE && id < B_LISTE + 40) {
        int i = premier_liste + id - B_LISTE;
        if (i >= nvue) return;
        const srv::Serveur *s = vue[i];
        if (!s->perso && s->umax && s->users >= s->umax) { message("Ce récepteur est plein"); return; }
        connecter(s->hote, s->port, s->mdp, s->nom);
        aller(E_RADIO);
        return;
    }
    if (id >= B_TOUCHE && id < B_TOUCHE + 13) { touche_pave(id - B_TOUCHE); return; }
    if (id >= B_BANDE_RAC && id < B_BANDE_RAC + 12 && cfg::R.proto == cfg::OWRX) {
        int k = id - B_BANDE_RAC;
        if (nprofs > 10 && k == 9) { page_profils = (page_profils + 1) % ((nprofs + 8) / 9); return; }
        int i = page_profils * 9 + k;
        if (i < nprofs) {
            kw::choisir_profil(profs[i].id);
            char m[64];
            snprintf(m, sizeof m, "Bande %s (aussi pour les autres auditeurs)", profs[i].nom);
            message(m);
            aller(E_RADIO);
        }
        return;
    }
    if (id >= B_BANDE_RAC && id < B_BANDE_RAC + 12) {
        const Raccourci &r = RACC[id - B_BANDE_RAC];
        uint8_t md = (uint8_t)r.mode;
        double fr = mem_rappel(r.f, 0, 1e9, &md);          // la derniere frequence ecoutee dans cette bande
        cfg::R.mode = md;
        accorder(fr);
        saisie[0] = 0;
        aller(E_RADIO);
        return;
    }
    if (id >= B_ONGLET && id < B_ONGLET + 3) { choisir_decodeur(id - B_ONGLET); return; }
    if (id >= B_FAV_LIGNE && id < B_FAV_LIGNE + MAX_FAV) { ouvrir_favori(id - B_FAV_LIGNE); return; }
    // reglages
    if (id == B_REG + R_ADPCM) { cfg::R.adpcm = !cfg::R.adpcm; cfg::marquer(); kw::regler_compression(); return; }
    if (id == B_REG + R_AMPLI) { cfg::R.ampli_bas = !cfg::R.ampli_bas; cfg::marquer(); plat_ampli(cfg::R.ampli_bas); return; }
    if (id == B_REG + R_ETALONNER || id == B_REG + R_SD) {
        int r = id - B_REG;
        if (a_confirmer != r || t_ms - t_confirme > 3000) { a_confirmer = r; t_confirme = t_ms; return; }
        a_confirmer = -1;
        if (r == R_ETALONNER) { plat_etalonner(); return; }
        if (mot::enregistre()) { message("Arrête d'abord l'enregistrement"); return; }
        message(plat_sd_formater() ? "Carte SD formatée (FAT32)" : "Échec du formatage : vérifie la carte");
        return;
    }
    if (id >= B_REG + 50 && id < B_REG + 54) { cfg::R.etiquettes = (uint8_t)(id - B_REG - 50); cfg::marquer(); kw::regler_etiquettes(); return; }
    if (id == B_REG + R_SUIVRE) { suivre_web(!s_suivre_web); return; }
    if (id >= B_SQUELCH && id < B_SQUELCH + 4) { regler_option("sq", id - B_SQUELCH); return; }
    if (id >= B_NUIT && id < B_NUIT + 4) { regler_option("nuit", id - B_NUIT); return; }
    if (id == B_OPT_CWTXT) { regler_option("cwtxt", opt2.sans_cw ? 1 : 0); return; }
    if (id == B_CW_BANDEAU) { ouvrir(E_DECODEURS); return; }
    if (id == B_SMETRE) { if (ecran == E_RADIO) sig_affiche = !sig_affiche; return; }
    if (id == B_OPT_SAM) { regler_option("sam", opt.sam ? 0 : 1); return; }
    if (id >= B_SAM_SEG && id < B_SAM_SEG + 4) { regler_option("sam", id - B_SAM_SEG); return; }
    if (id == B_OPT_REPRISE) { regler_option("reprise", !opt.reprise); return; }
    if (id == B_OPT_VOX) { regler_option("vox", !opt.vox); return; }
    if (id == B_FAV_SCAN) {
        if (sc_liste) scan_arreter();
        else { scan_favoris_lancer(); if (sc_liste) aller(E_RADIO); }
        return;
    }
    if (id >= B_SCAN_SENS && id < B_SCAN_SENS + 3) { scan_sensibilite(id - B_SCAN_SENS); return; }
    if (id >= B_MOL_REG && id < B_MOL_REG + 3) {
        mol_reglage = id - B_MOL_REG;
        uint8_t b = (uint8_t)(mol_reglage + 1);
        plat_nvs_ecrire("molette", &b, 1);
        return;
    }
    if (id == B_SCAN_BAS || id == B_SCAN_HAUT) { scan_appui(id == B_SCAN_HAUT ? 1 : -1); return; }
    if (id == B_REG + R_LIMITEUR) { cfg::R.limiteur = !cfg::R.limiteur; cfg::marquer(); aud::limiteur(cfg::R.limiteur); return; }
    if (id >= B_REG + 26 && id < B_REG + 30) { cfg::R.wf_vitesse = (uint8_t)(id - B_REG - 26 + 1); cfg::marquer(); kw::regler_cascade(); return; }
    if (id >= B_REG + 30 && id < B_REG + 34) { cfg::R.agc = (uint8_t)(id - B_REG - 30); cfg::marquer(); kw::regler_agc(); return; }
    if (id >= B_REG + 40 && id < B_REG + 44) { cfg::R.palette = (uint8_t)(id - B_REG - 40); cfg::marquer(); wf::palette(cfg::R.palette); return; }
}

static void clic(const Zone &z, int x, int y)
{
    if (!(z.fl & Z_APPUI)) action(z, x, y);          // les zones "a l'appui" ont deja agi
}

static int tc_manque = 0;

// ============================ Bouton rotatif KY-040 ============================
// Quand on tourne le bouton, une bande fine s'affiche en bas du waterfall (le centre reste visible) :
//  - frequence : un trait part du milieu vers le + ou vers le -, d'autant plus loin qu'on tourne vite, avec le saut ;
//  - volume, bande passante : une jauge avec la valeur.
// Elle s'efface 1,2 s apres le dernier cran. (Jusqu'a la v0.13, c'etait une grande molette ronde au milieu de l'ecran.)
static uint32_t mol_jusqua = 0, mol_t_cran = 0;
static float mol_rythme = 0;                            // crans par seconde (lisse)
static int mol_mode = 0;                                // le bouton regle : 0 la frequence, 1 le volume, 2 la bande passante
#define mol_volume (mol_mode == 1)
static char mol_l1[24] = "", mol_l2[24] = "";
static int mol_sens = 0;                                // dernier sens tourne (+1 / -1), 0 : annonce d'un mode
static float mol_jauge = -1;                            // 0..1 : jauge (volume, bande passante) ; -1 : frequence
static const uint32_t MOL_DUREE_MS = 1200;

// ============================ ACCROCHE CW ===================================
// Cale la radio sur la station morse la plus forte pres d'une frequence : le pic du spectre (waterfall),
// affine entre deux points ; il faut au moins 6 dB au-dessus du bruit. En CW, la frequence affichee est
// celle de la station : la note entendue est alors de 500 Hz, la ou le decodeur l'attend.
static bool cw_accrocher(double f_khz, double rayon_khz, bool annoncer)
{
    if (kw::etat() != kw::ACTIF || kw::cascade_largeur_khz() <= 0) return false;
    const float *sp = wf::spectre();
    int x0 = (int)floorf(x_de_f(f_khz - rayon_khz)), x1 = (int)ceilf(x_de_f(f_khz + rayon_khz));
    if (x0 < 1) x0 = 1;
    if (x1 > wf::L - 2) x1 = wf::L - 2;
    if (x1 < x0) return false;
    int xp = x0;
    for (int x = x0; x <= x1; x++) if (sp[x] > sp[xp]) xp = x;
    // bruit : mediane de l'ecran
    static uint16_t hist[400];
    memset(hist, 0, sizeof hist);
    float mn = 1e9f;
    for (int x = 0; x < wf::L; x++) if (sp[x] < mn) mn = sp[x];
    for (int x = 0; x < wf::L; x++) { int k = (int)((sp[x] - mn) * 2); hist[k < 0 ? 0 : (k > 399 ? 399 : k)]++; }
    int c = 0, k = 0;
    for (; k < 400; k++) { c += hist[k]; if (c >= wf::L / 2) break; }
    float bruit = mn + k * 0.5f;
    if (sp[xp] - bruit < 6) {
        if (annoncer) message("Pas de station morse assez forte ici");
        return false;
    }
    float a = sp[xp - 1], b = sp[xp], cc = sp[xp + 1], den = a - 2 * b + cc;
    float d = den < -1e-3f ? 0.5f * (a - cc) / den : 0;                 // sommet de la parabole
    if (d > 0.5f) d = 0.5f;
    if (d < -0.5f) d = -0.5f;
    double f = f_de_x(xp + d);
    f = floor(f * 100 + 0.5) / 100;                                    // au 10 Hz
    accorder(f);
    if (dec::choisi() == dec::CW) dec::caler();                        // le decodeur recale sa note
    mol_jusqua = t_ms + 1800; mol_sens = 0; mol_jauge = -1;
    char h[24];
    snprintf(h, sizeof h, "%.2f kHz", f);
    for (char *p = h; *p; p++) if (*p == '.') *p = ',';
    snprintf(mol_l1, sizeof mol_l1, "Accroché %s", h);
    snprintf(mol_l2, sizeof mol_l2, "+%.0f dB sur le bruit", sp[xp] - bruit);
    return true;
}

// en CW, l'anti-sifflement effacerait la note du morse : coupe en CW, remis en quittant ce mode
static void cw_tic()
{
    static int mode_prec = -1;
    static bool notch_coupe = false;
    if (cfg::R.mode == mode_prec) return;
    if (cfg::R.mode == cfg::CW && cfg::R.notch) { cfg::R.notch = 0; notch_coupe = true; kw::regler_bruit(); message("Anti-sifflement coupé en CW"); }
    else if (cfg::R.mode != cfg::CW && notch_coupe) { notch_coupe = false; cfg::R.notch = 1; kw::regler_bruit(); }
    mode_prec = cfg::R.mode;
}

// ============================ CONTROLE AU BOUTON ROTATIF ====================
// Partout (sauf sur l'accord de l'ecran Radio et le zoom de la carte) : tourner promene un cadre sur les zones
// touchables de l'ecran, dans l'ordre de lecture ; un appui court fait comme un doigt ; un appui long revient.
// Sur un curseur, l'appui entre dans le reglage (cadre orange) : tourner change la valeur, un appui valide.
// Les listes defilent pour garder la ligne choisie visible.
static bool nav_on = false, nav_regle = false, nav_synth = false;
static int nav_ecran = -1, nav_fx = 0, nav_fy = 0;
static uint32_t t_nav = 0;
static struct { bool actif; float dec0; int id; } nav_att = { false, 0, -1 };
static const int MOL_BOUTONS = 3;               // mode du bouton sur les ecrans Radio et FM : parcourir les boutons

static float *nav_defil()                       // la valeur de defilement de l'ecran (nullptr : pas de liste)
{
    switch (ecran) {
    case E_REGLAGES: reg_v = 0; return &reg_dec;
    case E_FAVORIS: fav_v = 0; return &fav_dec;
    case E_RECEPTEURS: case E_FM_LISTE: case E_WEBRADIO: case E_CANAUX: case E_FM_TROUVEES: case E_QUOI: liste_v = 0; return &liste_dec;
    default: return nullptr;
    }
}
static bool nav_exclue(int i)
{
    const Zone &z = zs_prec[i];
    switch (z.id) {
    case B_CASCADE: case B_VOL_BORD: case B_CARTE: case B_DEC_LISTE: case B_DEC_SPECTRE: case B_DEC_IMAGE: case B_REG_LISTE: case B_BP:
        return true;
    }
    if ((int)z.l * z.h > 320 * 240 / 3) return true;
    for (int k = 0; k < nz_prec; k++) {          // un fond qui contient d'autres zones (panneau, ligne de reglage...)
        if (k == i) continue;
        const Zone &o = zs_prec[k];
        if (o.x >= z.x && o.y >= z.y && o.x + o.l <= z.x + z.l && o.y + o.h <= z.y + z.h && (o.l < z.l || o.h < z.h)) return true;
    }
    return false;
}
static int nav_ordre(int *ord)                  // zones a parcourir, dans l'ordre de lecture
{
    int n = 0;
    for (int i = 0; i < nz_prec && n < 96; i++) {
        if (nav_exclue(i)) continue;
        bool double_ = false;                    // meme id deja vu (zone dessinee deux fois)
        for (int k = 0; k < n && !double_; k++) double_ = zs_prec[ord[k]].id == zs_prec[i].id;
        if (!double_) ord[n++] = i;
    }
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0; b--) {
            const Zone &p = zs_prec[ord[b - 1]], &q = zs_prec[ord[b]];
            int cp = p.y + p.h / 2, cq = q.y + q.h / 2, tol = (p.h < q.h ? p.h : q.h) / 2;
            bool apres = abs(cp - cq) <= tol ? p.x > q.x : cp > cq;
            if (!apres) break;
            int t = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = t;
        }
    return n;
}
static int nav_courant(const int *ord, int n)
{
    int best = -1;
    long db = 1L << 30;
    for (int k = 0; k < n; k++) {
        const Zone &z = zs_prec[ord[k]];
        if (nav_fx >= z.x && nav_fx < z.x + z.l && nav_fy >= z.y && nav_fy < z.y + z.h) return k;
        long dx = nav_fx - (z.x + z.l / 2), dy = nav_fy - (z.y + z.h / 2), d = dx * dx + dy * dy;
        if (d < db) { db = d; best = k; }
    }
    return best;
}
static void nav_viser(const Zone &z) { nav_fx = z.x + z.l / 2; nav_fy = z.y + z.h / 2; }
static void nav_premier(const int *ord, int n)
{
    for (int k = 0; k < n; k++) if (zs_prec[ord[k]].id != B_RETOUR) { nav_viser(zs_prec[ord[k]]); return; }
    if (n) nav_viser(zs_prec[ord[0]]);
}
static bool nav_dans_liste(const Zone &z) { return z.c0 > 0 || z.c1 < 240; }   // dessinee dans une zone qui defile
static bool meme_cadre(const Zone &a, const Zone &b) { return a.c0 == b.c0 && a.c1 == b.c1; }

static void nav_deplacer(int dir)
{
    static int ord[96];
    int n = nav_ordre(ord);
    t_nav = t_ms;
    if (!n) return;
    if (!nav_on || nav_ecran != ecran) { nav_on = true; nav_ecran = ecran; nav_regle = false; nav_premier(ord, n); return; }
    if (!dir) return;
    int c = nav_courant(ord, n);
    if (c < 0) { nav_premier(ord, n); return; }
    const Zone &zc = zs_prec[ord[c]];
    int t = c + dir;
    float *dv = nav_defil();
    bool liste = dv && nav_dans_liste(zc);
    float pas = zc.h + 4.0f;                      // une ligne de la liste
    if (liste) for (int k = 0; k < n; k++) if (meme_cadre(zs_prec[ord[k]], zc) && zs_prec[ord[k]].h + 4.0f > pas && !(zs_prec[ord[k]].fl & (Z_COUPE_HAUT | Z_COUPE_BAS))) pas = zs_prec[ord[k]].h + 4.0f;
    if (t >= 0 && t < n) {
        const Zone &zt = zs_prec[ord[t]];
        bool dedans = meme_cadre(zt, zc);
        if (liste && dedans && dir > 0 && (zt.fl & Z_COUPE_BAS)) {          // a moitie cachee en bas : on la remonte
            *dv += pas;
            nav_fx = zt.x + zt.l / 2; nav_fy = zt.y + 6 - (int)pas;
            nav_att = { true, *dv - pas, zt.id };
            return;
        }
        if (liste && dedans && dir < 0 && (zt.fl & Z_COUPE_HAUT)) {
            *dv -= pas;
            nav_fx = zt.x + zt.l / 2; nav_fy = zt.y + zt.h - 6 + (int)pas;
            return;
        }
        if (liste && !dedans && dir < 0 && *dv > 0.5f) {                   // des lignes cachees au-dessus
            *dv -= pas;
            return;                                   // la ligne precedente vient a la place de la ligne choisie
        }
        if (liste && !dedans && dir > 0) {                                  // peut-etre des lignes cachees en dessous
            *dv += pas;
            nav_att = { true, *dv - pas, zt.id };     // si la liste n'a pas bouge : on passe a la suite
            return;
        }
        nav_viser(zt);
        return;
    }
    if (liste && dir > 0) { *dv += pas; return; }
    if (liste && dir < 0 && *dv > 0.5f) { *dv -= pas; return; }
}

static const CurInfo *nav_curseur(int id)
{
    for (int i = 0; i < ncur_prec; i++) if (cur_prec[i].id == id) return &cur_prec[i];
    return nullptr;
}
static const Zone *nav_zone()
{
    static int ord[96];
    int n = nav_ordre(ord);
    int c = n ? nav_courant(ord, n) : -1;
    return c >= 0 ? &zs_prec[ord[c]] : nullptr;
}
static void nav_regler(int crans)
{
    const Zone *z = nav_zone();
    const CurInfo *ci = z ? nav_curseur(z->id) : nullptr;
    if (!ci) { nav_regle = false; return; }
    float t = ci->t + (float)crans / ci->pas;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    int x = ci->x + (int)lroundf(ci->l * t);
    if (t > 0 && t < 1) x += crans > 0 ? 1 : 0;   // contre l'arrondi vers le bas
    Zone zz = *z;
    if (zz.fl & Z_APPUI) presse(zz, x, zz.y + zz.h / 2);
    else glisse(zz, x, zz.y + zz.h / 2, 0, 0);
    t_nav = t_ms;
}
static void nav_valider()
{
    t_nav = t_ms;
    if (nav_regle) { nav_regle = false; return; }
    if (!nav_on || nav_ecran != ecran) { nav_deplacer(0); return; }
    const Zone *z = nav_zone();
    if (!z) return;
    if (nav_curseur(z->id)) { nav_regle = true; return; }
    int x = z->x + z->l / 2, y = z->y + z->h / 2;
    nav_synth = true;
    toucher(true, x, y);
    toucher(false, x, y);                        // deux fois : le relache demande deux lectures sans doigt
    toucher(false, x, y);
    nav_synth = false;
}
// apres le dessin d'une image : le cadre, et ce qui attendait l'image (liste arrivee en bout, nouvel ecran)
static void nav_apres_image()
{
    memcpy(cur_prec, cur_inf, sizeof(CurInfo) * ncur);
    ncur_prec = ncur;
    if (!nav_on) return;
    if (nav_ecran != ecran) {                        // un appui a ouvert un autre ecran : le cadre y va aussi
        nav_ecran = ecran;
        nav_regle = false;
        nav_att.actif = false;
        static int ord[96];
        memcpy(zs_prec, zs, sizeof(Zone) * nz);
        nz_prec = nz;
        int n = nav_ordre(ord);
        nav_premier(ord, n);
        return;
    }
    if (nav_att.actif) {
        nav_att.actif = false;
        float *dv = nav_defil();
        if (dv && fabsf(*dv - nav_att.dec0) < 0.5f)   // la liste n'a pas bouge : fin de liste, on passe a la suite
            for (int i = 0; i < nz; i++) if (zs[i].id == nav_att.id) { nav_viser(zs[i]); break; }
    }
}
static void nav_dessin()
{
    if (!nav_on || nav_ecran != ecran || (ecran == E_RADIO || ecran == E_FM ? mol_mode != MOL_BOUTONS : false)) return;
    const Zone *z = nullptr;
    for (int i = nz - 1; i >= 0 && !z; i--) {
        const Zone &c = zs[i];
        if (nav_fx >= c.x && nav_fx < c.x + c.l && nav_fy >= c.y && nav_fy < c.y + c.h) {
            // la zone la plus petite sous le point (pas le fond d'un panneau)
            const Zone *m = &c;
            for (int k = 0; k < nz; k++) {
                const Zone &o = zs[k];
                if (nav_fx >= o.x && nav_fx < o.x + o.l && nav_fy >= o.y && nav_fy < o.y + o.h && (int)o.l * o.h < (int)m->l * m->h && o.id != B_CASCADE) m = &o;
            }
            z = m;
        }
    }
    if (!z) return;
    decoupe(0, z->c0, 320, z->c1);
    uint16_t c = nav_regle ? AMBRE : CYAN;
    contour_arrondi(z->x - 2, z->y - 2, z->l + 4, z->h + 4, 7, 2, c);
    sans_decoupe();
}

static void molette_crans(int n)
{
    if (!n) return;
    uint32_t dt = t_ms - mol_t_cran;
    mol_t_cran = t_ms;
    float inst = dt > 0 ? 1000.0f * abs(n) / dt : 50;
    mol_rythme = dt > 400 ? inst * 0.5f : mol_rythme * 0.6f + inst * 0.4f;
    if (ecran == E_CARTE) { carte_zoomer(n > 0 ? 1 : -1); return; }     // sur la carte : le bouton zoome
    // ailleurs (et sur Radio / FM en mode "Boutons") : le cadre de selection, ou le curseur choisi
    if ((ecran != E_RADIO && ecran != E_FM) || mol_mode == MOL_BOUTONS) {
        if (nav_regle) { nav_regler(n); return; }
        for (int k = 0; k < abs(n) && k < 4; k++) nav_deplacer(n > 0 ? 1 : -1);
        return;
    }
    if (ecran == E_FM && mol_volume) {                  // appui long fait avant : le bouton regle le son, ici aussi
        int v = cfg::R.volume + 2 * n;
        v = v < 0 ? 0 : (v > 100 ? 100 : v);
        cfg::R.volume = (uint8_t)v;
        plat_volume(v);
        cfg::marquer();
        return;
    }
    if (ecran == E_FM) { fs_arreter(); for (int k = 0; k < abs(n); k++) fm_pas(n > 0 ? 1 : -1); return; }
    if (ecran != E_RADIO) return;
    if (sc_sens && !sc_liste && mol_mode == 0) {         // pendant un autoscan : meme sens = plus vite, sens contraire = moins vite puis demi-tour
        int d = n > 0 ? 1 : -1;
        if (d == sc_sens) { if (sc_vitesse < 4) scan_appui(d); }
        else if (sc_vitesse > 1) sc_vitesse--;
        else scan_lancer(d);
        return;
    }
    if (sc_sens) scan_arreter();
    mol_jusqua = t_ms + MOL_DUREE_MS;
    mol_sens = n > 0 ? 1 : -1;
    mol_jauge = -1;
    if (mol_mode == 2) {
        mol_jauge = (float)((int)cfg::R.bp[cfg::R.mode] + n * bp_pas(cfg::R.mode) - cfg::bp_min(cfg::R.mode)) / (float)(cfg::bp_max(cfg::R.mode) - cfg::bp_min(cfg::R.mode));                                 // bande passante : un cran = un pas du filtre
        bp_regler((int)cfg::R.bp[cfg::R.mode] + n * bp_pas(cfg::R.mode));
        char h[16];
        fmt_hz(h, sizeof h, cfg::R.bp[cfg::R.mode]);
        snprintf(mol_l1, sizeof mol_l1, "BP %s", h);
        snprintf(mol_l2, sizeof mol_l2, "règle le filtre");
        t_bp = 0;                                       // la molette affiche deja la valeur
        return;
    }
    if (mol_volume) {
        int v = cfg::R.volume + 2 * n;
        v = v < 0 ? 0 : (v > 100 ? 100 : v);
        cfg::R.volume = (uint8_t)v;
        plat_volume(v);
        cfg::marquer();
        snprintf(mol_l1, sizeof mol_l1, "Volume %d", v);
        snprintf(mol_l2, sizeof mol_l2, "règle le son");
        mol_jauge = v / 100.0f;
        return;
    }
    int mult = cfg::R.mode == cfg::CW ? 1 : (mol_rythme < 6 ? 1 : (mol_rythme < 14 ? 5 : 20));   // en CW : un cran = un pas, toujours
    int p = cfg::PAS_HZ[cfg::R.pas];
    double pas = (double)p * mult / 1000.0;
    double f = arrondi_pas(cfg::R.freq_khz, p) + n * pas;
    accorder(f);
    char h[16];
    fmt_hz(h, sizeof h, p * mult);
    snprintf(mol_l1, sizeof mol_l1, "%s%s", n > 0 ? "+" : "−", h);
    snprintf(mol_l2, sizeof mol_l2, "%s", mult == 1 ? "lent" : (mult == 5 ? "rapide" : "très rapide"));
}

static void molette_dessin()
{
    if (ecran != E_RADIO || (int32_t)(mol_jusqua - t_ms) <= 0) return;
    const int y = Y_BAS - 19, h = 17;
    rect(0, y, 320, h, rgb(6, 10, 20), 215);
    rect(0, y, 320, 1, LIGNE);
    if (mol_jauge >= 0) {                               // volume, bande passante : jauge et valeur
        float f = mol_jauge > 1 ? 1 : mol_jauge;
        rect_arrondi(8, y + 6, 150, 6, 3, LIGNE);
        if (f > 0.02f) rect_arrondi(8, y + 6, (int)(150 * f), 6, 3, AMBRE);
        texte(168, y + 2, mol_l1, POLICE_13, AMBRE);
        texte(312, y + 3, mol_l2, POLICE_11, TXT2, 255, DROITE);
        return;
    }
    if (!mol_sens) {                                    // annonce : ce que le bouton regle, ou le nouveau pas
        texte(8, y + 2, mol_l1, POLICE_13, AMBRE);
        texte(312, y + 3, mol_l2, POLICE_11, TXT2, 255, DROITE);
        return;
    }
    // frequence : du milieu vers le + ou le -, plus loin quand on tourne vite ; le trait s'estompe
    texte(8, y + 1, "−", POLICE_13, mol_sens < 0 ? AMBRE : TXT2);
    texte(312, y + 1, "+", POLICE_13, mol_sens > 0 ? AMBRE : TXT2, 255, DROITE);
    rect_arrondi(22, y + 7, 276, 4, 2, LIGNE);
    rect(159, y + 3, 2, 12, TXT);
    float r = mol_rythme < 2 ? 2 : (mol_rythme > 24 ? 24 : mol_rythme);
    int l = 16 + (int)(120.0f * (r - 2) / 22.0f);
    int reste = (int32_t)(mol_jusqua - t_ms);
    uint8_t al = reste > 600 ? 255 : (uint8_t)(reste * 255 / 600);
    if (mol_sens > 0) rect_arrondi(161, y + 6, l, 6, 3, AMBRE, al);
    else rect_arrondi(159 - l, y + 6, l, 6, 3, AMBRE, al);
    // le saut, du cote oppose au trait
    char t[56];
    snprintf(t, sizeof t, "%s · %s", mol_l1, mol_l2);
    if (mol_sens > 0) texte(150, y + 3, t, POLICE_11, TXT, 255, DROITE);
    else texte(170, y + 3, t, POLICE_11, TXT);
}

static void molette_bouton()
{
    if (!mol_reglage) return;
    int crans = 0;
    bool appui = false;
    if (!plat_molette(&crans, &appui)) return;
    if (mol_reglage == 2) crans = -crans;
    if (crans || appui) {
        t_geste = t_ms;
        if (nuit_actif) { nuit_actif = false; plat_luminosite(cfg::R.luminosite); return; }   // reveille l'ecran, sans rien regler
    }
    if ((ecran == E_RADIO || ecran == E_FM) && mol_mode == MOL_BOUTONS && t_ms - t_nav > 10000) {   // 10 s sans rien : retour a l'accord
        mol_mode = 0; nav_on = false; nav_regle = false;
        mol_jusqua = t_ms + 1500; mol_sens = 0; mol_jauge = -1;
        snprintf(mol_l1, sizeof mol_l1, "Bouton : fréquence");
        snprintf(mol_l2, sizeof mol_l2, "tourne pour accorder");
    }
    if (crans) molette_crans(crans);
    static bool b_prec = false, b_long = false;
    static uint32_t b_t0 = 0;
    if (appui && !b_prec) { b_t0 = t_ms; b_long = false; }
    if (appui && !b_long && t_ms - b_t0 > 600) {        // appui long
        b_long = true;
        if (ecran == E_RADIO || ecran == E_FM) {        // Radio : frequence -> volume -> bande passante -> boutons ; FM : sans la bande passante
            do mol_mode = (mol_mode + 1) % 4; while (ecran == E_FM && mol_mode == 2);
            nav_regle = false;
            if (mol_mode == MOL_BOUTONS) { nav_on = false; nav_deplacer(0); }
            static const char *const M1[4] = { "Bouton : fréquence", "Bouton : volume", "Bouton : bande passante", "Bouton : boutons" },
                              *const M2[4] = { "tourne pour accorder", "tourne pour le son", "tourne pour le filtre", "tourne : choisir · appui : valider" };
            mol_jusqua = t_ms + 2000;
            mol_sens = 0; mol_jauge = -1;
            snprintf(mol_l1, sizeof mol_l1, "%s", M1[mol_mode]);
            snprintf(mol_l2, sizeof mol_l2, "%s", M2[mol_mode]);
            if (ecran == E_FM) message(M1[mol_mode]);
        } else if (nav_regle) nav_regle = false;        // dans un curseur : on en sort
        else retour();                                  // ailleurs (carte comprise) : retour
    }
    if (!appui && b_prec && !b_long && t_ms - b_t0 > 30) {   // appui court
        if (ecran == E_CARTE) carte_ecouter();          // sur la carte : ecouter le recepteur choisi
        else if (ecran != E_RADIO && ecran != E_FM) nav_valider();   // ailleurs : comme un doigt sur la zone choisie
        else if (mol_mode == MOL_BOUTONS) nav_valider();
        else if (ecran == E_RADIO && cfg::R.mode == cfg::CW && mol_mode == 0) cw_accrocher(cfg::R.freq_khz, 1.0, true);   // CW : accroche
        else if (ecran == E_RADIO) {
            cfg::R.pas = (uint8_t)((cfg::R.pas + 1) % cfg::NB_PAS);
            cfg::marquer();
            mol_jusqua = t_ms + 1500;
            mol_sens = 0; mol_jauge = -1;
            char h[16];
            fmt_hz(h, sizeof h, cfg::PAS_HZ[cfg::R.pas]);
            snprintf(mol_l1, sizeof mol_l1, "Pas %s", h);
            snprintf(mol_l2, sizeof mol_l2, "pas suivant");
        }
    }
    b_prec = appui;
}

void second_doigt(bool present, int x, int y)
{
    d2_present = present;
    d2_x = x;
    d2_y = y;
}

void toucher(bool appui, int x, int y)
{
    uint32_t ms = plat_ms();
    bool etait_pince = pince;
    if (pince_tic(appui, x, y)) {                       // deux doigts : bande passante ; le toucher simple est annule
        if (!etait_pince) {
            if (sc_sens) scan_arreter();
            if (tc_appui && tc_z.id == B_CASCADE && tc_glisse) accorder(glisse_f0);   // le 1er doigt avait commence a faire glisser : annule
            tc_z = Zone{ 0, 0, 0, 0, -1, 0 };
            tc_glisse = false;
            tc_long = true;
        }
        tc_appui = pince;
        tc_manque = 0;
        return;
    }
    // le capteur rate parfois une lecture : on attend 2 lectures sans doigt avant de conclure au relache
    if (!appui && tc_appui && ++tc_manque < 2) return;
    if (appui) tc_manque = 0;
    if (appui) t_geste = t_ms;
    if (nuit_actif) {                                   // mode nuit : le premier toucher reveille l'ecran, rien d'autre
        if (appui) { nuit_actif = false; plat_luminosite(cfg::R.luminosite); tc_z = Zone{ 0, 0, 0, 0, -1, 0 }; tc_appui = true; tc_glisse = false; tc_long = true; tc_t0 = ms; }
        return;
    }
    if (appui && !tc_appui && fs_sens) {               // autoscan FM : meme bouton = plus vite, ailleurs = arret
        const Zone *z = chercher(x, y);
        if (!(z && z->id == (fs_sens > 0 ? B_FMSCAN_HAUT : B_FMSCAN_BAS))) {
            fs_arreter();
            tc_z = Zone{ 0, 0, 0, 0, -1, 0 };
            tc_appui = true;
            tc_glisse = false;
            tc_long = true;
            tc_t0 = ms;
            return;
        }
    }
    const Zone *zsc = (appui && !tc_appui && sc_sens) ? chercher(x, y) : nullptr;
    bool accelere = zsc && !sc_liste && zsc->id == (sc_sens > 0 ? B_SCAN_HAUT : B_SCAN_BAS);
    if (appui && !tc_appui && sc_sens && !accelere) {
        // un doigt sur l'ecran arrete le scan (ce toucher ne fait rien d'autre)
        scan_arreter();
        tc_z = Zone{ 0, 0, 0, 0, -1, 0 };
        tc_appui = true;
        tc_glisse = false;
        tc_long = true;
        tc_t0 = ms;
        return;
    }
    if (appui && !tc_appui) {
        if (!nav_synth) { nav_on = false; nav_regle = false; }   // un doigt : le cadre du bouton rotatif s'efface
        const Zone *z = chercher(x, y);
        tc_z = z ? *z : Zone{ 0, 0, 0, 0, -1, 0 };
        tc_appui = true;
        tc_glisse = false;
        tc_long = false;
        tc_x0 = tc_xp = tc_x = x;
        tc_y0 = tc_yp = tc_y = y;
        tc_t0 = ms;
        t_repet = ms;
        if (z) presse(*z, x, y);
        return;
    }
    if (appui && tc_appui) {
        tc_x = x;
        tc_y = y;
        if (!tc_glisse && (abs(x - tc_x0) > 6 || abs(y - tc_y0) > 6) && (tc_z.fl & Z_GLISSE)) tc_glisse = true;
        if (tc_glisse && tc_z.id >= 0) glisse(tc_z, x, y, x - tc_xp, y - tc_yp);
        tc_xp = x;
        tc_yp = y;
        // auto-repetition des boutons - / +
        if ((tc_z.id == B_MOINS || tc_z.id == B_PLUS) && ms - tc_t0 > 450 && ms - t_repet > 90) {
            t_repet = ms;
            tc_long = true;
            if (ecran == E_FM) fm_pas(tc_z.id == B_MOINS ? -1 : +1);
            else pas_freq(tc_z.id == B_MOINS ? -1 : +1);
        } else if (!tc_glisse && !tc_long && (tc_z.fl & Z_LONG) && ms - tc_t0 > 650) {
            tc_long = true;
            appui_long(tc_z);
        }
        return;
    }
    if (!appui && tc_appui) {
        tc_appui = false;
        if (!tc_glisse && !tc_long && tc_z.id >= 0) {
            const Zone *z = chercher(tc_x, tc_y);
            if (z && z->id == tc_z.id) clic(tc_z, tc_x, tc_y);
        }
        tc_z.id = -1;
    }
}

// ============================ Ordres du portail web ==========================

static void ecran_web(const char *onglet)
{
    if (!s_suivre_web) return;
    struct { const char *nom; int e; } T[] = {
        { "Radio", E_RADIO }, { "Récepteurs", E_RECEPTEURS }, { "Stations", E_RADIO }, { "Quoi écouter", E_QUOI }, { "FM", E_FM },
        { "Favoris", E_FAVORIS }, { "Décodeurs", E_DECODEURS }, { "Réglages", E_REGLAGES }, { "Système", E_WIFI } };
    for (auto &t : T)
        if (!strcmp(onglet, t.nom)) {
            if (t.e != ecran) { panneau_audio = false; ouvrir(t.e); }
            return;
        }
}
static const int NCMD = 16, LCMD = 200;
static char (*s_cmd)[LCMD] = nullptr;           // [NCMD][LCMD] en PSRAM
static volatile int s_cmd_tete = 0, s_cmd_queue = 0;

void commande(const char *o)
{
    int q = s_cmd_queue, suiv = (q + 1) % NCMD;
    if (suiv == s_cmd_tete) return;                  // file pleine
    if (!s_cmd) return;
    snprintf(s_cmd[q], LCMD, "%s", o);
    __atomic_store_n(&s_cmd_queue, suiv, __ATOMIC_RELEASE);
}

// "Quoi écouter" pour la page web (appele par la tache du portail : ses propres tableaux)
int quoi_web(int cat, QuoiWeb *dst, int nmax, char *contexte, size_t nc, int *fait, int *total)
{
    static quoi::Sugg *tmp = (quoi::Sugg *)plat_malloc_ps(sizeof(quoi::Sugg) * quoi::MAX_SUGG);
    static QLigne *l = (QLigne *)plat_malloc_ps(sizeof(QLigne) * quoi::MAX_SUGG);
    *fait = qm_phase ? qm_fait : 0;
    *total = qm_phase ? qm_total : 0;
    if (contexte && nc) contexte[0] = 0;
    if (!tmp || !l || cat < 0 || cat > quoi::TOUT) return 0;
    quoi::Contexte c = q_ctx;
    int n = quoi::proposer(c, cat, tmp, quoi::MAX_SUGG);
    bool mes = q_mes_valide(), une = false;
    for (int i = 0; i < n; i++) {
        l[i].s = tmp[i];
        l[i].dispo = (uint8_t)q_ecoutable(l[i].s, false);
        l[i].mesure = 0; l[i].sig = 0; l[i].db = 0; l[i].f_fort = 0;
        for (int k = 0; mes && k < nq_mes; k++)
            if (q_mes[k].a == l[i].s.a && q_mes[k].b == l[i].s.b) { l[i].mesure = 1; l[i].sig = q_mes[k].sig; l[i].db = q_mes[k].db; une = true; break; }
    }
    std::stable_sort(l, l + n, [](const QLigne &x, const QLigne &y) {
        if ((x.dispo > 0) != (y.dispo > 0)) return x.dispo > 0;
        return q_niveau(x) > q_niveau(y);
    });
    if (contexte && nc) q_ligne_contexte(contexte, nc, une);
    if (n > nmax) n = nmax;
    for (int i = 0; i < n; i++) {
        QuoiWeb &w = dst[i];
        snprintf(w.nom, sizeof w.nom, "%s", l[i].s.nom);
        snprintf(w.detail, sizeof w.detail, "%s", l[i].s.detail);
        q_etat(l[i], w.etat, sizeof w.etat);
        w.a = l[i].s.a; w.b = l[i].s.b;
        w.genre = l[i].s.genre;
        w.niveau = (uint8_t)q_niveau(l[i]);
        w.dispo = l[i].dispo;
        w.mesure = l[i].mesure;
    }
    return n;
}

void options2(int *graves, int *aigus, int *nuit, int *cwtxt) { *graves = opt2.graves; *aigus = opt2.aigus; *nuit = opt2.nuit; *cwtxt = !opt2.sans_cw; }

int journal_web(EcouteWeb *dst, int nmax, EcouteWeb *cours)
{
    static_assert(sizeof(EcouteWeb) == sizeof(Ecoute), "meme structure");
    if (cours) {
        memcpy(cours, &jr_cours, sizeof(Ecoute));
        if (jr_cours.f > 0 && jr_t0) cours->duree_s = (uint16_t)((t_ms - jr_t0) / 1000 > 65535 ? 65535 : (t_ms - jr_t0) / 1000);
    }
    int n = 0;
    for (int k = jr_n - 1; k >= 0 && n < nmax && jr; k--) memcpy(&dst[n++], &jr[(jr_debut + k) % MAX_JOURNAL], sizeof(Ecoute));
    return n;
}

void options(int *squelch, int *sam, int *reprise, int *vox, int *muet)
{
    *squelch = opt.squelch; *sam = opt.sam; *reprise = opt.reprise; *vox = opt.vox; *muet = sq_muet;
}
int nb_favoris() { return nfav; }
bool favori(int i, double *khz, int *mode, char *nom, int nn, char *hote, int nh)
{
    if (i < 0 || i >= nfav) return false;
    *khz = favs[i].f;
    *mode = favs[i].mode;
    snprintf(nom, nn, "%s", favs[i].nom);
    snprintf(hote, nh, "%s", favs[i].hote);
    return true;
}
int nb_presets() { return NB_PRESETS; }
const char *preset(int i, double *khz, int *mode)
{
    if (i < 0 || i >= NB_PRESETS) return nullptr;
    *khz = PRESETS[i].f;
    *mode = PRESETS[i].mode;
    return PRESETS[i].nom;
}
void scan_etat(int *sens, int *ecoute, int *reste_s, int *sensibilite)
{
    *sens = sc_sens;
    *ecoute = sc_sens && sc_phase == SC_ECOUTE;
    *reste_s = *ecoute ? (int)((SC_ECOUTE_MS - (t_ms - sc_t) + 999) / 1000) : 0;
    *sensibilite = sc_sensibilite;
}
int scan_vitesse() { return sc_sens ? sc_vitesse : 0; }
int canal_etat(double khz) { return canal_ecoutable(khz, false); }
int scan_fm(int *vitesse, int *reste_s)
{
    *vitesse = fs_sens ? fs_vitesse : 0;
    *reste_s = fs_sens && fs_phase == 2 ? (int)((SC_ECOUTE_MS - (t_ms - fs_t) + 999) / 1000) : 0;
    return fs_sens;
}
int fm_trouvees(float *mhz, float *sig, char (*ps)[10], int nmax)
{
    int n = nfmt < nmax ? nfmt : nmax;
    for (int i = 0; i < n; i++) { mhz[i] = fmt[i].mhz; sig[i] = fmt[i].sig; memcpy(ps[i], fmt[i].ps, 10); }
    return n;
}
bool scan_liste(char *canal, int n)
{
    if (!sc_liste) return false;
    if (sc_phase == SC_ECOUTE && sl_i >= 0) snprintf(canal, n, "%s · %s", sl[sl_i].nom, sl[sl_i].type);
    else snprintf(canal, n, sl_fav ? "%d favoris" : "%d canaux", sl_n);
    return true;
}

float images_par_seconde() { return s_ips; }

static void executer(char *o)
{
    char *a = strchr(o, ' ');
    if (a) *a++ = 0; else a = (char *)"";
    int n = atoi(a);
    static const char *const ARRETENT_SCAN[] = { "f", "m", "plus", "moins", "preset", "co", "station", "favgo", "deco", "fmco", "owrx", "profil", "zoom", "vhf" };
    static const char *const ARRETENT_FM[] = { "fmf", "fmco", "fmdeco", "fmtrouve", "station" };
    for (const char *c : ARRETENT_FM) if (!strcmp(o, c)) fs_arreter();
    for (const char *c : ARRETENT_SCAN) if (!strcmp(o, c)) scan_arreter();
    if (!strcmp(o, "scan")) { if (n == 0) scan_arreter(); else scan_appui(n); return; }
    if (!strcmp(o, "scansens")) { scan_sensibilite(n); return; }
    if (!strcmp(o, "f")) { double f = atof(a); if (f >= 0 && f <= cfg::FREQ_MAX_KHZ) accorder(f); }
    else if (!strcmp(o, "owrx")) {
        // owrx <adresse>[<TAB>nom]  (portail web, recepteurs VHF/UHF)
        char *nom = strchr(a, '\t');
        if (nom) *nom++ = 0;
        if (a[0] && connecter_url(a, nom ? nom : "")) aller(E_RADIO);
    }
    else if (!strcmp(o, "profil")) kw::choisir_profil(a);
    else if (!strcmp(o, "bporig")) bp_origine(n != 0);
    else if (!strcmp(o, "geoip")) geo::localiser();
    else if (!strcmp(o, "geoville")) geo::chercher_ville(a);
    else if (!strcmp(o, "fmscan")) { if (n == 0) fs_arreter(); else fs_appui(n); }
    else if (!strcmp(o, "fmtrouve")) { float f = (float)atof(a); if (f > 60 && f < 110) { fs_arreter(); fm::accorder(f); } }
    else if (!strcmp(o, "fmtefface")) nfmt = 0;
    else if (!strcmp(o, "vhf")) { double f = 0; int m = 0; if (sscanf(a, "%lf %d", &f, &m) == 2 && f > 0 && m >= 0 && m < cfg::NB_MODES) regler_canal(f, m); }
    else if (!strcmp(o, "scanliste")) { int c = n > 0 ? n : vhf::TOUS; if (sc_liste) scan_arreter(); else scan_liste_lancer(c); }
    else if (!strcmp(o, "vhfliste")) plat_vhf_liste_telecharger();
    else if (!strcmp(o, "quoi")) {                     // quoi <categorie> <debut kHz> <fin kHz> : ecouter cette suggestion
        int c = 0; double qa = 0, qb = 0;
        if (sscanf(a, "%d %lf %lf", &c, &qa, &qb) == 3 && c >= 0 && c <= quoi::TOUT) {
            if (qm_phase) q_mesure_fin(nullptr);
            q_cat = c;
            q_maj();
            for (int i = 0; i < nq && ql; i++)
                if (fabs(ql[i].s.a - qa) < 0.01 && fabs(ql[i].s.b - qb) < 0.01) { q_ecouter(i); break; }
        }
    }
    else if (!strcmp(o, "sq") || !strcmp(o, "sam") || !strcmp(o, "reprise") || !strcmp(o, "vox") || !strcmp(o, "nuit")
             || !strcmp(o, "graves") || !strcmp(o, "aigus") || !strcmp(o, "cwtxt")) regler_option(o, n);
    else if (!strcmp(o, "journalvide")) {
        jr_n = 0; jr_debut = 0;
        if (plat_sd()) { const char *h = "date;heure;frequence_khz;mode;duree_s;signal_max_dbm;recepteur;station\r\n"; plat_texte_ecrire(JOURNAL_CSV, h, strlen(h)); }
    }
    else if (!strcmp(o, "noaa")) plat_noaa_regler(n != 0);
    else if (!strcmp(o, "meilleur")) { if (fm::actif()) fb_lancer(); else mr_lancer(); }
    else if (!strcmp(o, "scanfav")) { if (sc_liste) scan_arreter(); else scan_favoris_lancer(); }
    else if (!strcmp(o, "quoimes")) {                  // quoimes <categorie> : mesurer (ou arreter la mesure en cours)
        if (qm_phase) q_mesure_fin("Mesure arrêtée");
        else if (n >= 0 && n <= quoi::TOUT) { q_cat = n; q_mesurer(true); }
    }
    else if (!strcmp(o, "m")) changer_mode(n);
    else if (!strcmp(o, "pas")) { if (n >= 0 && n < cfg::NB_PAS) { cfg::R.pas = (uint8_t)n; cfg::marquer(); } }
    else if (!strcmp(o, "plus")) pas_freq(+1);
    else if (!strcmp(o, "moins")) pas_freq(-1);
    else if (!strcmp(o, "zoom")) zoom(n);
    else if (!strcmp(o, "vol")) { cfg::R.volume = (uint8_t)(n < 0 ? 0 : (n > 100 ? 100 : n)); plat_volume(cfg::R.volume); cfg::marquer(); }
    else if (!strcmp(o, "agc")) { cfg::R.agc = (uint8_t)(n & 3); cfg::marquer(); kw::regler_agc(); }
    else if (!strcmp(o, "nr")) { cfg::R.nr = (uint8_t)(n > 2 ? 0 : n); cfg::marquer(); kw::regler_bruit(); }
    else if (!strcmp(o, "notch")) { cfg::R.notch = n != 0; cfg::marquer(); kw::regler_bruit(); }
    else if (!strcmp(o, "nb")) { cfg::R.nb = (uint8_t)(n < 0 ? 0 : (n > 3 ? 3 : n)); cfg::marquer(); kw::regler_bruit(); }
    else if (!strcmp(o, "agcseuil")) { opt3.agc_seuil = (int8_t)(n < -130 ? -130 : (n > -60 ? -60 : n)); opt3_sauver(); opt3_appliquer(); }
    else if (!strcmp(o, "gainman")) { opt3.gain_man = (uint8_t)(n < 0 ? 0 : (n > 120 ? 120 : n)); opt3_sauver(); opt3_appliquer(); }
    else if (!strcmp(o, "lim")) { cfg::R.limiteur = n != 0; cfg::marquer(); aud::limiteur(cfg::R.limiteur); }
    else if (!strcmp(o, "favadd")) { if (!fav_existe()) fav_ajouter(); }
    else if (!strcmp(o, "favgo")) ouvrir_favori(n);
    else if (!strcmp(o, "favdel")) { fav_retirer(n); message("Favori retiré"); }
    else if (!strcmp(o, "favnom")) {
        char *nom = strchr(a, ' ');
        if (n >= 0 && n < nfav && nom) { snprintf(favs[n].nom, sizeof favs[n].nom, "%s", nom + 1); fav_sauver(); }
    }
    else if (!strcmp(o, "preset")) rappeler_preset(n);
    else if (!strcmp(o, "co")) {
        // co<TAB>hote<TAB>port<TAB>mot de passe<TAB>nom
        char *ch[4] = { nullptr, nullptr, nullptr, nullptr };
        char *p = a;
        for (int k = 0; k < 4 && p; k++) { ch[k] = p; p = strchr(p, '\t'); if (p) *p++ = 0; }
        if (ch[0] && ch[1] && ch[0][0]) connecter(ch[0], (uint16_t)atoi(ch[1]), ch[2] ? ch[2] : "", ch[3] && ch[3][0] ? ch[3] : ch[0]);
    }
    else if (!strcmp(o, "station")) {
        // station <kHz> <mode>[<TAB>hote<TAB>port<TAB>nom du recepteur]  (portail web, onglet Stations)
        char *t = strchr(a, '\t');
        if (t) *t++ = 0;
        double f = 0;
        int m = 0;
        if (sscanf(a, "%lf %d", &f, &m) != 2 || f <= 0 || f > 32000 || m < 0 || m >= cfg::NB_MODES) return;
        cfg::R.mode = (uint8_t)m;
        cfg::R.pas = (uint8_t)cfg::pas_par_defaut(m, f);
        accorder(f);
        if (t) {
            char *ch[3] = { t, nullptr, nullptr };
            for (int k = 1; k < 3 && ch[k - 1]; k++) { char *p = strchr(ch[k - 1], '\t'); if (p) { *p++ = 0; ch[k] = p; } }
            if (ch[0][0] && ch[1]) connecter(ch[0], (uint16_t)atoi(ch[1]), "", ch[2] && ch[2][0] ? ch[2] : ch[0]);
        } else if (kw::etat() != kw::ACTIF && cfg::R.hote[0]) {
            message("Connecte-toi d'abord à un récepteur");
        }
        aller(E_RADIO);
    }
    else if (!strcmp(o, "fmco")) {
        // fmco <url>[<TAB>nom]  (portail web, onglet FM)
        char *nom = strchr(a, '\t');
        if (nom) *nom++ = 0;
        if (!a[0]) return;
        kw::deconnecter();
        fm::connecter(a, nom && nom[0] ? nom : a);
        aller(E_FM);
    }
    else if (!strcmp(o, "fmdeco")) fm::deconnecter();
    else if (!strcmp(o, "fmf")) { double f = atof(a); if (f > 0) fm::accorder((float)f); }
    else if (!strcmp(o, "ecran")) ecran_web(a);
    else if (!strcmp(o, "suivre")) suivre_web(n != 0);
    else if (!strcmp(o, "gain")) regler_gain(n);
    else if (!strcmp(o, "deco")) kw::deconnecter();
    else if (!strcmp(o, "dec")) { if (n >= 0 && n < dec::NB) choisir_decodeur(n); }
    else if (!strcmp(o, "decactif")) t_dec_web = t_ms;
    else if (!strcmp(o, "decclr")) { if (dec::choisi() == dec::FAX) dec::caler(); else if (dec::choisi() == dec::FT8) f8::effacer(); else dec::effacer(); }
    else if (!strcmp(o, "wrcherche")) {                // "nom|pays|genre"
        char t[120];
        snprintf(t, sizeof t, "%s", a);
        char *p1 = strchr(t, '|'), *p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
        if (p1) *p1++ = 0;
        if (p2) *p2++ = 0;
        wr::chercher(t, p1 ? p1 : "", p2 ? p2 : "");
        wr_rapide = -1;                                // l'ecran montre ces resultats
    }
    else if (!strcmp(o, "wrjoue") || !strcmp(o, "wrfav")) {   // "r 3" (resultat) ou "f 0" (favori)
        int i = atoi(a + 1);
        const wr::Station *s = nullptr;
        if (a[0] == 'r' && i >= 0 && i < wr::nb_resultats()) s = &wr::resultat(i);
        if (a[0] == 'f' && i >= 0 && i < wr::nb_favoris()) s = &wr::favori(i);
        if (s) { wr::Station c = *s; if (o[2] == 'j') { wr_jouer(c); if (s_suivre_web && ecran != E_WEBRADIO) ouvrir(E_WEBRADIO); } else wr::basculer_favori(c); }
    }
    else if (!strcmp(o, "wrstop")) wr_arreter();
    else if (!strcmp(o, "ft8bande")) { if (dec::choisi() != dec::FT8) choisir_decodeur(dec::FT8); ft8_bande(n); }
    else if (!strcmp(o, "deccal")) dec::caler();
    else if (!strcmp(o, "decinv")) dec::inverser();
    else if (!strcmp(o, "decvit")) dec::vitesse_suivante();
    else if (!strcmp(o, "decshift")) dec::shift_suivant();
    else if (!strcmp(o, "decsave")) mot::sauver_decodage();
    else if (!strcmp(o, "rec")) {
        if (n && kw::etat() != kw::ACTIF) message("Connecte-toi d'abord à un récepteur");
        else mot::enregistrer(n != 0);
    }
    else if (!strcmp(o, "reg")) {
        char cle[16];
        int v = 0;
        if (sscanf(a, "%15s %d", cle, &v) != 2) return;
        if (!strcmp(cle, "bp")) { int m = cfg::R.mode; if (v < cfg::bp_min(m)) v = cfg::bp_min(m); if (v > cfg::bp_max(m)) v = cfg::bp_max(m); cfg::R.bp[m] = (uint32_t)v; kw::regler(); }
        else if (!strcmp(cle, "wfv")) { cfg::R.wf_vitesse = (uint8_t)(v < 1 ? 1 : (v > 4 ? 4 : v)); kw::regler_cascade(); }
        else if (!strcmp(cle, "lum")) { cfg::R.luminosite = (uint8_t)(v < 5 ? 5 : (v > 100 ? 100 : v)); plat_luminosite(cfg::R.luminosite); }
        else if (!strcmp(cle, "pal")) { cfg::R.palette = (uint8_t)(v & 3); wf::palette(cfg::R.palette); }
        else if (!strcmp(cle, "adpcm")) { cfg::R.adpcm = v != 0; kw::regler_compression(); }
        else if (!strcmp(cle, "tampon")) { cfg::R.tampon_ms = (uint16_t)(v < 150 ? 150 : (v > 1500 ? 1500 : v)); aud::reserve_visee(cfg::R.tampon_ms); }
        else if (!strcmp(cle, "ampli")) { cfg::R.ampli_bas = v != 0; plat_ampli(cfg::R.ampli_bas); }
        else if (!strcmp(cle, "etiq")) { cfg::R.etiquettes = (uint8_t)(v & 3); kw::regler_etiquettes(); }
        else return;
        cfg::marquer();
    }
    else if (!strcmp(o, "etalon")) plat_etalonner();
    else if (!strcmp(o, "sdformat")) {
        if (mot::enregistre()) message("Arrête d'abord l'enregistrement");
        else message(plat_sd_formater() ? "Carte SD formatée (FAT32)" : "Échec du formatage : vérifie la carte");
    }
    else if (!strcmp(o, "liste")) plat_liste_telecharger();
    else if (!strcmp(o, "msg")) message(a);
}

static void ordres_web()
{
    while (s_cmd_tete != __atomic_load_n(&s_cmd_queue, __ATOMIC_ACQUIRE)) {
        char o[LCMD];
        snprintf(o, sizeof o, "%s", s_cmd[s_cmd_tete]);
        s_cmd_tete = (s_cmd_tete + 1) % NCMD;
        executer(o);
    }
}

// ============================ Image ==========================================
void init()
{
    if (!vue) vue = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    if (!vue_fm) vue_fm = (const fml::ServeurFM **)plat_malloc_ps(sizeof(void *) * fml::MAX_FM);
    if (!s_cmd) s_cmd = (char (*)[LCMD])plat_malloc_ps((size_t)NCMD * LCMD);
    if (!mk) mk = (kw::Marqueur *)plat_malloc_ps(sizeof(kw::Marqueur) * kw::MAX_MARQUEURS);
    fav_charger();
    { uint8_t v = 0; if (plat_nvs_lire("suivre_web", &v, 1)) s_suivre_web = v != 2; }
    { uint8_t v = 0; if (plat_nvs_lire("gain_kiwi", &v, 1) && v >= 1 && v <= 3) aud::gain_kiwi(v - 1); }
    { uint8_t v = 0; if (plat_nvs_lire("scan_sens", &v, 1) && v >= 1 && v <= 3) sc_sensibilite = v - 1; }
    bp_am_init();
    { uint8_t v = 0; if (plat_nvs_lire("molette", &v, 1) && v >= 1 && v <= 3) mol_reglage = v - 1; }
    { Options o; if (plat_nvs_lire("options", &o, sizeof o) && o.squelch <= 3) opt = o; }
    { Options3 o; if (plat_nvs_lire("options3", &o, sizeof o) && o.agc_seuil >= -130 && o.agc_seuil <= -60 && o.gain_man <= 120) opt3 = o; }
    opt3_appliquer();
    { Options2 o; if (plat_nvs_lire("options2", &o, sizeof o) && o.nuit <= 3 && o.graves >= -8 && o.graves <= 8 && o.aigus >= -8 && o.aigus <= 8) opt2 = o; }
    opt2_appliquer();
    if (!plat_nvs_lire("bandes", mem_b, sizeof mem_b)) memset(mem_b, 0, sizeof mem_b);
    {   // v0.15.2 : en CW, la frequence affichee devient celle de la station (avant : 0,5 kHz en dessous).
        // Les frequences CW deja enregistrees sont remontees une fois, pour qu'elles tombent toujours sur la meme station.
        uint8_t fait = 0;
        if (!plat_nvs_lire("cw500", &fait, 1) || !fait) {
            const double D = cfg::NOTE_CW / 1000.0;
            bool fav = false, mem = false;
            for (int i = 0; i < nfav; i++) if (favs[i].mode == cfg::CW) { favs[i].f += D; fav = true; }
            for (int i = 0; i < NB_MEM; i++) if (mem_b[i].f > 0 && mem_b[i].mode == cfg::CW) { mem_b[i].f += (float)D; mem = true; }
            if (cfg::R.mode == cfg::CW) { cfg::R.freq_khz += D; plat_nvs_ecrire("reglages", &cfg::R, sizeof cfg::R); }   // tout de suite, avant la marque
            if (fav) fav_sauver();
            if (mem) plat_nvs_ecrire("bandes", mem_b, sizeof mem_b);
            fait = 1;
            plat_nvs_ecrire("cw500", &fait, 1);
        }
    }
    opt_appliquer();
    wf::palette(cfg::R.palette);
    aud::reserve_visee(cfg::R.tampon_ms);
    aud::limiteur(cfg::R.limiteur);
    dec::choisir(dec::CW);
    if (cfg::R.hote[0]) kw::connecter(cfg::R.hote, cfg::R.port, cfg::R.mdp, cfg::R.nom, cfg::R.proto, cfg::R.chiffre, cfg::R.chemin);
    else ecran = E_RADIO;
}

static void bandeau()
{
    char cr[96];
    if (mot::compte_rendu(cr, sizeof cr)) message(cr);
    if (!toast_t0) return;
    uint32_t age = t_ms - toast_t0;
    if (age > 2800) { toast_t0 = 0; return; }
    uint8_t a = age < 2400 ? 255 : (uint8_t)(255 - (age - 2400) * 255 / 400);
    const char *t = tronquer(toast_txt, POLICE_13, 290);
    int l = largeur_texte(t, POLICE_13) + 20;
    rect_arrondi(160 - l / 2, 160, l, 26, 8, rgb(30, 41, 64), (uint8_t)(a * 240 / 255));
    contour_arrondi(160 - l / 2, 160, l, 26, 8, 1, CYAN, a);
    texte(160, 165, t, POLICE_13, TXT, a, CENTRE);
}

void image(uint32_t ms)
{
    t_ms = ms;
    ordres_web();
    bp_am_tic();
    scan_tic();
    fs_tic();
    fb_tic();
    canaux_tic();
    quoi_tic();
    qr_tic();
    mr_tic();
    squelch_tic();
    cw_tic();
    wr_tic();
    sb_tic();
    if (t_opt3 && t_ms - t_opt3 > 2000) { t_opt3 = 0; opt3_sauver(); }
    reprise_tic();
    mem_tic();
    journal_tic();
    signal_tic();
    nuit_tic();
    molette_bouton();
    bool cw_radio = cw_bandeau_visible();
    if (cw_radio && dec::choisi() != dec::CW) dec::choisir(dec::CW);   // ecoute en CW : le decodeur morse
    dec::actif(ecran == E_DECODEURS || (t_dec_web && t_ms - t_dec_web < 5000) || cw_radio || (carte_ft8 && ecran == E_CARTE && dec::choisi() == dec::FT8));
    {   // FT8 : enregistre tant qu'on le regarde (ecran, carte ou page web) ; distances depuis le recepteur ecoute
        bool f = dec::choisi() == dec::FT8 && kw::etat() == kw::ACTIF && (ecran == E_DECODEURS || (t_dec_web && t_ms - t_dec_web < 5000) || (carte_ft8 && ecran == E_CARTE));
        f8::actif(f);
        static uint32_t t_pos = 0;
        if (f && t_ms - t_pos > 5000) { t_pos = t_ms; q_contexte(); f8::position(q_ctx.lat, q_ctx.lon); }
    }
    if (++s_ips_n >= 20) {                           // images par seconde (affichees dans Menu > WiFi)
        s_ips = s_ips_n * 1000.0f / (ms - s_ips_t + 1);
        s_ips_t = ms;
        s_ips_n = 0;
    }
    nz = 0;
    ncur = 0;
    if (nuit_actif) { ecran_nuit(); goto fin_image; }
    switch (ecran) {
    case E_RADIO: ecran_radio(); break;
    case E_MENU: ecran_menu(); break;
    case E_RECEPTEURS: ecran_recepteurs(); premier_liste = (int)(liste_dec / H_LIGNE); break;
    case E_FREQ: ecran_freq(); break;
    case E_DECODEURS: ecran_decodeurs(); break;
    case E_REGLAGES: ecran_reglages(); break;
    case E_FAVORIS: ecran_favoris(); break;
    case E_WIFI: ecran_wifi(); break;
    case E_FM: ecran_fm(); break;
    case E_FM_LISTE: ecran_fm_liste(); premier_liste = (int)(liste_dec / H_LIGNE); break;
    case E_WEBRADIO: ecran_webradio(); break;
    case E_CANAUX: ecran_canaux(); premier_liste = (int)(liste_dec / H_LIGNE); break;
    case E_FM_TROUVEES: ecran_fm_trouvees(); premier_liste = (int)(liste_dec / 30); break;
    case E_QUOI: ecran_quoi(); premier_liste = (int)(liste_dec / H_LIGNE); break;
    case E_CARTE: ecran_carte(); break;
    }
    bandeau();
    nav_dessin();
fin_image:
    nav_apres_image();
    memcpy(zs_prec, zs, sizeof(Zone) * nz);
    nz_prec = nz;
    cfg::sauver();
}

}  // namespace ui
