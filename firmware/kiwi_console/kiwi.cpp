// kiwi.cpp - protocole KiwiSDR : commandes, messages, son, waterfall
#include "kiwi.h"
#include "audio.h"
#include "nr.h"
#include "cascade.h"
#include "reglages.h"
#include "plateforme.h"
#include "serveurs.h"
#include "fmliste.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace kw {

// ------------------------------------------------------------------ ADPCM
static const int16_t PAS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t AJUST[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

int16_t Adpcm::un(uint8_t code)
{
    int pas = PAS[index];
    index += AJUST[code & 15];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    int diff = pas >> 3;
    if (code & 1) diff += pas >> 2;
    if (code & 2) diff += pas >> 1;
    if (code & 4) diff += pas;
    if (code & 8) diff = -diff;
    int s = prec + diff;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    prec = s;
    return (int16_t)s;
}

// ------------------------------------------------------------------ etat
static volatile Etat s_etat = ARRET;
static char s_msg[64] = "Aucun récepteur";
static char s_hote[64] = "", s_nom[48] = "", s_mdp[24] = "";
static uint16_t s_port = 8073;
static bool s_chiffre = false;                  // https (wss) apres une redirection du relais kiwisdr.com
static int s_proto = cfg::KIWI;                 // KiwiSDR ou OpenWebRX
static char s_base[48] = "/";                   // chemin de base de l'adresse (OpenWebRX)
static int s_echecs = 0;                         // essais de connexion rates (son)
static const int MAX_ESSAIS = 3;
static int s_redirections = 0;
static volatile uint32_t s_gen = 0;
static uint32_t s_horodatage = 1;
static bool s_voulu = false;
static bool s_ouvert[2] = { false, false };

static volatile float s_rssi = -127;
static volatile bool s_ovfl = false;
static float s_bw_max = 30000.0f;              // kHz
static float s_foff = 0;                       // kHz
static volatile float s_wf_cf = 7074, s_wf_span = 234.375f;
static int s_limite = 0;
static Adpcm s_adpcm;
static bool s_compresse = true;
static uint32_t s_octets = 0, s_debit = 0, s_t_debit = 0;
static uint32_t s_t_vie[2] = { 0, 0 };
static uint32_t s_t_ouvert = 0;
static uint32_t s_t_connexion = 0;
static bool s_snd_pret = false, s_wf_pret = false;

// ---- OpenWebRX ----
static int s_ow_etape = 0;                      // 0 poignee de main, 1 attente de la configuration, 2 en ecoute
static double s_ow_cf = 0, s_ow_sr = 0;         // centre et largeur de la bande (Hz)
static int s_ow_fft = 0;                        // points du spectre
static bool s_ow_fft_adpcm = false, s_ow_son_adpcm = false;
static double s_ow_f_depart = 0;                // frequence de depart proposee (Hz)
static char s_ow_m_depart[12] = "";
static char s_ow_profil[64] = "";               // "sdr|profil" en cours
static Profil *s_prof = nullptr;                // MAX_PROFILS (PSRAM)
static int s_nprof = 0;
static volatile uint32_t s_vprof = 0;
static int s_ow_fs = 0;                         // debit du son recu (12 000 ou 48 000)
static uint32_t s_ow_trames = 0;
static float *s_bande_db = nullptr;             // NB_BANDE points (PSRAM)
struct Plage { char id[64]; double a, b; };
static Plage s_plages[MAX_PROFILS];             // plages des bandes deja visitees (ce recepteur)
static int s_nplages = 0;
static float s_rx_lat = 999, s_rx_lon = 999;
struct PlageNom { char nom[48]; double a, b; };
static PlageNom s_pnom[MAX_PROFILS];            // plages lues dans status.json, par nom complet "sdr profil"
static int s_npnom = 0;
static volatile uint32_t s_vplages = 0;
static volatile bool s_statut_demande = false;
static volatile bool s_pause_mkr = false;
static volatile int s_bande_n = 0;
static volatile uint32_t s_bande_compte = 0;
struct AdpcmSync { int phase = 0, sync = 0, compte = 0, k = 0; uint8_t tampon[8]; Adpcm a, b; };
static AdpcmSync s_ow_dec;

// demandes de l'interface (appliquees dans tic, cote reseau)
static volatile bool d_mod = false, d_wf = false, d_agc = false, d_comp = false, d_bruit = false, d_mkr = false;
static volatile bool s_force = false;          // mesure des bandes : vue imposee
static volatile float s_force_cf = 0;
static volatile int s_force_zoom = 0;
static int s_wf_zoom = 0;
static uint32_t s_t_mkr = 0;
static uint32_t s_t_mod = 0, s_t_wf = 0;

// file des messages a envoyer, par canal
static const int NF = 40, LF = 160;
static char (*s_file)[NF][LF] = nullptr;      // [2][NF][LF] sur le tas (PSRAM) : 13 Ko de moins en memoire interne
static int s_tete[2] = { 0, 0 }, s_queue[2] = { 0, 0 };

static void envoyer(int c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void envoyer(int c, const char *fmt, ...)
{
    int suiv = (s_queue[c] + 1) % NF;
    if (suiv == s_tete[c] || !s_file) return; // file pleine : message perdu (ne devrait pas arriver)
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_file[c][s_queue[c]], LF, fmt, ap);
    va_end(ap);
    s_queue[c] = suiv;
}

static void vider_files()
{
    s_tete[0] = s_queue[0] = s_tete[1] = s_queue[1] = 0;
}

// ------------------------------------------------------------------ journal de connexion
static LigneJ s_j[MAX_J];
static volatile int s_nj = 0;
static uint32_t s_t_j = 0, s_t_actif = 0, s_t_wf1 = 0;
static bool s_son1 = false;
static volatile int s_jverrou = 0;
static void jv(bool p)
{
    if (p) { while (__atomic_exchange_n(&s_jverrou, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_jverrou, 0, __ATOMIC_RELEASE);
}
static void j_raz()
{
    jv(true);
    s_nj = 0;
    jv(false);
    s_t_j = plat_ms();
    s_t_actif = s_t_wf1 = 0;
    s_son1 = false;
}
// la derniere ligne "en cours" prend l'etat donne
static void j_conclure(uint8_t e)
{
    jv(true);
    for (int i = s_nj - 1; i >= 0; i--)
        if (s_j[i].e == J_COURS) { s_j[i].e = e; break; }
    jv(false);
}
static void j_ajout(uint8_t e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void j_ajout(uint8_t e, const char *fmt, ...)
{
    char t[56];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    jv(true);
    if (s_nj > 0 && !strcmp(s_j[s_nj - 1].t, t)) { s_j[s_nj - 1].e = e; jv(false); return; }   // pas de doublon
    if (s_nj == MAX_J) { memmove(s_j, s_j + 1, sizeof(LigneJ) * (MAX_J - 1)); s_nj = MAX_J - 1; }
    snprintf(s_j[s_nj].t, sizeof s_j[0].t, "%s", t);
    s_j[s_nj].e = e;
    s_nj = s_nj + 1;
    jv(false);
}

int journal(LigneJ *dst, int nmax)
{
    jv(true);
    int n = s_nj < nmax ? s_nj : nmax;
    memcpy(dst, s_j + (s_nj - n), sizeof(LigneJ) * n);
    jv(false);
    return n;
}

static void etat_msg(Etat e, const char *m);

void echec_ouverture(int c, const char *raison)
{
    if (!s_voulu) return;
    j_conclure(J_ECHEC);
    if (c == SND) s_echecs++;
    int essai = s_echecs < 1 ? 1 : s_echecs;
    char base[56];
    snprintf(base, sizeof base, "%s : %s", c == SND ? "Son" : "Waterfall", raison);
    // la meme erreur deja affichee : on met a jour son numero d'essai au lieu d'ajouter une ligne
    jv(true);
    bool vu = false;
    for (int i = 0; i < s_nj && !vu; i++)
        if (!strncmp(s_j[i].t, base, strlen(base) < 40 ? strlen(base) : 40)) {
            snprintf(s_j[i].t, sizeof s_j[0].t, "%.40s (essai %d/%d)", base, essai, MAX_ESSAIS);
            s_j[i].e = J_ECHEC;
            vu = true;
        }
    jv(false);
    if (!vu) j_ajout(J_ECHEC, "%.40s (essai %d/%d)", base, essai, MAX_ESSAIS);
    if (c == SND && s_echecs >= MAX_ESSAIS) {
        etat_msg(PANNE, "Récepteur injoignable (hors ligne ?)");
        s_voulu = false;
        s_gen = s_gen + 1;
    }
}

bool rediriger(const char *url)
{
    if (!s_voulu || s_redirections >= 3) return false;
    char h[64], base[64];
    uint16_t p = 0;
    bool tls = false;
    if (!fml::analyser_url(url, h, sizeof h, &p, &tls, base, sizeof base)) return false;
    if (s_proto == cfg::OWRX) {                         // "https://hote/chemin/ws/" -> chemin de base "/chemin/"
        size_t lb = strlen(base);
        if (lb >= 3 && !strcmp(base + lb - 3, "ws/")) base[lb - 3] = 0;
        if (!strcmp(h, s_hote) && p == s_port && tls == s_chiffre && !strcmp(base, s_base)) return false;
        snprintf(s_base, sizeof s_base, "%s", base[0] == '/' ? base : "/");
    } else if (!strcmp(h, s_hote) && p == s_port && tls == s_chiffre) return false;
    s_redirections++;
    snprintf(s_hote, sizeof s_hote, "%s", h);
    s_port = p;
    s_chiffre = tls;
    s_echecs = 0;
    j_conclure(J_ECHEC);
    j_ajout(J_OK, "Redirigé vers %s%s:%u", tls ? "https://" : "", h, (unsigned)p);
    j_ajout(J_COURS, "Connexion à %s:%u", h, (unsigned)p);
    s_gen = s_gen + 1;                                   // la plateforme rouvre les deux canaux
    return true;
}

static void etat_msg(Etat e, const char *m)
{
    if (e == OCCUPE || e == REFUS || e == PANNE || e == PERDU) { j_conclure(J_ECHEC); j_ajout(J_ECHEC, "%s", m); }
    if (e == ACTIF && !s_t_actif) s_t_actif = plat_ms();
    s_etat = e;
    snprintf(s_msg, sizeof s_msg, "%s", m);
}

void init()
{
    s_etat = ARRET;
    if (!s_file) s_file = (char (*)[NF][LF])calloc(2, sizeof(char[NF][LF]));
}

Etat etat() { return s_etat; }
int protocole() { return s_proto; }
const char *message() { return s_msg; }
const char *nom() { return s_nom[0] ? s_nom : s_hote; }
const char *hote() { return s_hote; }
uint16_t port() { return s_port; }
float rssi_dbm() { return s_rssi; }
float cascade_centre_khz() { return s_wf_cf; }
float cascade_largeur_khz() { return s_wf_span; }
float bande_max_khz() { return s_bw_max + s_foff; }
float bande_min_khz() { return s_foff; }
uint32_t debit_octets() { return s_debit; }
int limite_minutes() { return s_limite; }
bool surcharge_adc() { return s_ovfl; }
uint32_t generation() { return s_gen; }

static int s_canal = -1, s_nb_cascades = -1;       // annonces par le KiwiSDR (canal W/F)

bool journal_visible()
{
    if (!s_nj) return false;
    uint32_t ms = plat_ms();
    if (s_etat == ARRET) return false;
    if (s_etat == ACTIF) {
        if (s_t_wf1 && ms - s_t_wf1 > 1200) return false;                  // le waterfall arrive : on le montre
        if (s_t_actif && ms - s_t_actif > 10000) return false;             // au plus 10 s apres la connexion
        if (s_canal >= 0 && s_nb_cascades >= 0 && s_canal >= s_nb_cascades && s_t_actif && ms - s_t_actif > 3000)
            return false;                                                   // le message "pas de waterfall" prend le relais
    }
    return true;                                                            // connexion en cours ou echec
}

bool sans_cascade() { return s_etat == ACTIF && s_canal >= 0 && s_nb_cascades >= 0 && s_canal >= s_nb_cascades; }
int canal_rx() { return s_canal; }
int canaux_cascade() { return s_nb_cascades; }

void connecter(const char *hote, uint16_t port, const char *mdp, const char *nom, int proto, bool chiffre, const char *chemin)
{
    s_proto = proto == cfg::OWRX ? cfg::OWRX : cfg::KIWI;
    snprintf(s_base, sizeof s_base, "%s", chemin && chemin[0] == '/' ? chemin : "/");
    s_ow_etape = 0;
    s_ow_cf = s_ow_sr = 0;
    s_ow_profil[0] = 0;
    s_nprof = 0;
    s_nplages = 0;
    s_npnom = 0;
    s_statut_demande = false;
    s_vplages = s_vplages + 1;
    s_rx_lat = s_rx_lon = 999;
    s_force = false;
    s_vprof = s_vprof + 1;
    if (s_proto == cfg::KIWI) { s_foff = 0; s_bw_max = 30000.0f; }
    s_redirections = 0;
    s_canal = s_nb_cascades = -1;
    s_chiffre = chiffre;
    s_echecs = 0;
    j_raz();
    j_ajout(J_COURS, "Connexion à %s:%u", hote, (unsigned)port);
    snprintf(s_hote, sizeof s_hote, "%s", hote);
    snprintf(s_nom, sizeof s_nom, "%s", nom ? nom : "");
    snprintf(s_mdp, sizeof s_mdp, "%s", mdp ? mdp : "");
    s_port = port ? port : (s_proto == cfg::OWRX ? (chiffre ? 443 : 80) : 8073);
    s_horodatage = 1000000u + plat_ms() / 10;  // meme nombre pour SND et W/F
    s_voulu = true;
    etat_msg(CONNEXION, "Connexion…");
    s_t_connexion = plat_ms();
    s_gen = s_gen + 1;
}

void deconnecter()
{
    s_voulu = false;
    etat_msg(ARRET, "Déconnecté");
    s_gen = s_gen + 1;
}

void regler() { d_mod = true; }
void regler_cascade() { d_wf = true; }
void cascade_forcer(float centre_khz, int zoom) { s_force_cf = centre_khz; s_force_zoom = zoom < 0 ? 0 : (zoom > 14 ? 14 : zoom); s_force = true; d_wf = true; }
void cascade_liberer() { if (s_force) { s_force = false; d_wf = true; } }
bool cascade_forcee() { return s_force; }
void regler_agc() { d_agc = true; }
void regler_compression() { d_comp = true; }
void regler_bruit() { d_bruit = true; }
void regler_etiquettes() { d_mkr = true; }

// ------------------------------------------------------------------ etiquettes
static Marqueur s_mkr[MAX_MARQUEURS];
static int s_nmkr = 0;
static volatile uint32_t s_vmkr = 0;
static volatile int s_occupe_mkr = 0;
static void verrou_mkr(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe_mkr, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe_mkr, 0, __ATOMIC_RELEASE);
}

int marqueurs(Marqueur *dst, int nmax)
{
    verrou_mkr(true);
    int n = s_nmkr < nmax ? s_nmkr : nmax;
    memcpy(dst, s_mkr, n * sizeof(Marqueur));
    verrou_mkr(false);
    return n;
}
uint32_t version_marqueurs() { return s_vmkr; }

static void cmd_marqueurs()
{
    if (!cfg::R.etiquettes) {
        verrou_mkr(true); s_nmkr = 0; verrou_mkr(false);
        s_vmkr = s_vmkr + 1;
        return;
    }
    static const int DB[4] = { 0, 1, 2, 0 };            // 1 EiBi, 2 communaute, 3 liste du proprietaire
    float a = s_wf_cf - s_wf_span / 2 - s_foff, b = s_wf_cf + s_wf_span / 2 - s_foff;
    // filter_tod=1 : seulement les stations qui emettent a cette heure (EiBi)
    envoyer(WF, "SET MARKER db=%d min=%.3f max=%.3f zoom=%d width=1024 eibi_types_mask=0xffffffff filter_tod=1 anti_clutter=0",
            DB[cfg::R.etiquettes & 3], a < 0 ? 0 : a, b, (int)cfg::R.zoom);
}

// "mkr=[{"t":4},{"g":12,"f":7074.000,...,"fl":2,...,"i":"FT8%20...",...},...]"
static void lire_marqueurs(const char *v)
{
    static Marqueur tmp[MAX_MARQUEURS];
    int n = 0;
    const char *p = v;
    while (n < MAX_MARQUEURS && (p = strchr(p, '{')) != nullptr) {
        const char *fin = strchr(p, '}');
        if (!fin) break;
        const char *f = strstr(p, "\"f\":");
        const char *fl = strstr(p, "\"fl\":");
        const char *id = strstr(p, "\"i\":\"");
        if (f && f < fin && id && id < fin) {
            Marqueur &m = tmp[n];
            m.khz = (float)atof(f + 4) + s_foff;
            int drap = (fl && fl < fin) ? atoi(fl + 5) : 0;
            // modes du KiwiSDR : AM AMN USB LSB CW CWN NBFM IQ DRM USN LSN SAM SAU SAL SAS QAM NNFM
            static const uint8_t M[17] = { cfg::AM, cfg::AM, cfg::USB, cfg::LSB, cfg::CW, cfg::CW, cfg::FM, cfg::USB, cfg::AM,
                                           cfg::USB, cfg::LSB, cfg::AM, cfg::AM, cfg::AM, cfg::AM, cfg::AM, cfg::FM };
            int im = (drap & 0xF) | ((drap & 0x40000) ? 0x10 : 0);
            m.mode = im < 17 ? M[im] : cfg::AM;
            const char *s = id + 5;
            int k = 0;
            while (s < fin && *s != '"' && k < (int)sizeof m.nom - 1) {
                if (*s == '%' && s + 2 < fin) {
                    char h[3] = { s[1], s[2], 0 };
                    char c = (char)strtol(h, nullptr, 16);
                    m.nom[k++] = (c == '\n' || c == '\r') ? ' ' : c;
                    s += 3;
                } else {
                    m.nom[k++] = *s++;
                }
            }
            while (k > 0 && ((uint8_t)m.nom[k - 1] & 0xC0) == 0xC0) k--;   // pas de caractere coupe
            m.nom[k] = 0;
            if (k) n++;
        }
        p = fin + 1;
    }
    verrou_mkr(true);
    memcpy(s_mkr, tmp, n * sizeof(Marqueur));
    s_nmkr = n;
    verrou_mkr(false);
    s_vmkr = s_vmkr + 1;
}

// ------------------------------------------------------------------ recherche EiBi
static volatile int s_eb_etat = EIBI_RIEN;
static char s_eb_mots[6][24];
static int s_eb_nm = 0;
static float s_eb_pos = 0, s_eb_pas = 1000, s_eb_pas_envoye = 1000, s_eb_fin = 30000;
static bool s_eb_attente = false;
static uint32_t s_eb_t = 0;
static int s_eb_echecs = 0;
static Eibi *s_eb_res = nullptr;               // MAX_EIBI resultats sur le tas (PSRAM)
static int s_eb_n = 0;
static const char *s_eb_err = "";

static uint8_t mode_marqueur(int drap)
{
    // modes du KiwiSDR : AM AMN USB LSB CW CWN NBFM IQ DRM USN LSN SAM SAU SAL SAS QAM NNFM
    static const uint8_t M[17] = { cfg::AM, cfg::AM, cfg::USB, cfg::LSB, cfg::CW, cfg::CW, cfg::FM, cfg::USB, cfg::AM,
                                   cfg::USB, cfg::LSB, cfg::AM, cfg::AM, cfg::AM, cfg::AM, cfg::AM, cfg::FM };
    int im = (drap & 0xF) | ((drap & 0x40000) ? 0x10 : 0);
    return im < 17 ? M[im] : cfg::AM;
}

// copie un champ JSON "cle":"texte%20code" en clair
static void champ_url(const char *p, const char *fin, const char *cle, char *dst, size_t n)
{
    dst[0] = 0;
    const char *d = strstr(p, cle);
    if (!d || d > fin) return;
    const char *s = d + strlen(cle);
    size_t k = 0;
    while (s < fin && *s != '"' && k + 1 < n) {
        if (*s == '%' && s + 2 < fin) {
            char h[3] = { s[1], s[2], 0 };
            char c = (char)strtol(h, nullptr, 16);
            dst[k++] = (c == '\n' || c == '\r') ? ' ' : c;
            s += 3;
        } else {
            dst[k++] = *s++;
        }
    }
    while (k > 0 && ((uint8_t)dst[k - 1] & 0xC0) == 0xC0) k--;
    dst[k] = 0;
}

bool eibi_chercher(const char *texte)
{
    if (!s_wf_pret || s_proto == cfg::OWRX) return false;
    if (!s_eb_res) s_eb_res = (Eibi *)calloc(MAX_EIBI, sizeof(Eibi));
    if (!s_eb_res) return false;
    char plie[96];
    srv::plier(texte ? texte : "", plie, sizeof plie);
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 6; p = strtok_r(nullptr, " ,;", &ctx))
        snprintf(s_eb_mots[nm++], sizeof s_eb_mots[0], "%s", p);
    verrou_mkr(true);
    s_eb_nm = nm;
    s_eb_n = 0;
    verrou_mkr(false);
    s_eb_pos = bande_min_khz() < 0 ? 0 : bande_min_khz();
    s_eb_fin = bande_max_khz();
    s_eb_pas = 1000;
    s_eb_attente = false;
    s_eb_echecs = 0;
    s_eb_err = "";
    s_eb_etat = nm ? EIBI_COURS : EIBI_RIEN;
    return true;
}

int eibi_etat(int *avance, const char **err)
{
    if (avance) {
        float t = s_eb_fin > 0 ? s_eb_pos / s_eb_fin : 1;
        *avance = s_eb_etat == EIBI_FINI ? 100 : (int)(100 * (t > 1 ? 1 : t));
    }
    if (err) *err = s_eb_err;
    return s_eb_etat;
}

int eibi_resultats(Eibi *dst, int nmax)
{
    verrou_mkr(true);
    int n = s_eb_n < nmax ? s_eb_n : nmax;
    if (s_eb_res) memcpy(dst, s_eb_res, n * sizeof(Eibi));
    else n = 0;
    verrou_mkr(false);
    return n;
}

static void lire_eibi(const char *v)
{
    const char *p = v;
    while ((p = strchr(p, '{')) != nullptr) {
        const char *fin = strchr(p, '}');
        if (!fin) break;
        const char *f = strstr(p, "\"f\":");
        if (f && f < fin) {
            Eibi e;
            e.khz = (float)atof(f + 4) + s_foff;
            const char *fl = strstr(p, "\"fl\":");
            e.mode = mode_marqueur((fl && fl < fin) ? atoi(fl + 5) : 0);
            champ_url(p, fin, "\"i\":\"", e.nom, sizeof e.nom);
            champ_url(p, fin, "\"n\":\"", e.note, sizeof e.note);
            char brut[100], t[100];
            snprintf(brut, sizeof brut, "%s %s", e.nom, e.note);
            srv::plier(brut, t, sizeof t);
            bool ok = e.nom[0] != 0;
            for (int k = 0; k < s_eb_nm && ok; k++) ok = strstr(t, s_eb_mots[k]) != nullptr;
            if (ok && s_eb_res) {
                verrou_mkr(true);
                bool double_ = false;
                for (int k = 0; k < s_eb_n && !double_; k++)
                    double_ = fabsf(s_eb_res[k].khz - e.khz) < 0.05f && !strcmp(s_eb_res[k].nom, e.nom);
                if (!double_ && s_eb_n < MAX_EIBI) s_eb_res[s_eb_n++] = e;
                verrou_mkr(false);
            }
        }
        p = fin + 1;
    }
    // tranche suivante : plus etroite si la reponse etait grosse, plus large si elle etait petite
    size_t l = strlen(v);
    if (l > 30000 && s_eb_pas > 125) s_eb_pas /= 2;
    else if (l < 6000 && s_eb_pas < 4000) s_eb_pas *= 2;
}

static void eibi_avancer(uint32_t ms)
{
    if (s_eb_etat != EIBI_COURS) return;
    if (!s_wf_pret) {
        s_eb_etat = EIBI_ERREUR;
        s_eb_err = "Connexion au récepteur perdue";
        return;
    }
    if (s_eb_attente) {
        if (ms - s_eb_t > 5000) {                       // pas de reponse : tranche suivante
            s_eb_attente = false;
            if (++s_eb_echecs >= 3) {
                s_eb_etat = EIBI_ERREUR;
                s_eb_err = "Le récepteur ne répond pas aux demandes de noms de stations";
                d_mkr = true;
                return;
            }
            s_eb_pos += s_eb_pas_envoye;
        } else {
            return;
        }
    }
    if (s_eb_pos >= s_eb_fin) {
        s_eb_etat = EIBI_FINI;
        d_mkr = true;                                   // remet les etiquettes de l'ecran
        return;
    }
    if (ms - s_t_mkr < 300) return;                     // laisse passer une demande d'etiquettes en cours
    float a = s_eb_pos - s_foff, b = s_eb_pos + s_eb_pas - s_foff;
    envoyer(WF, "SET MARKER db=1 min=%.3f max=%.3f zoom=14 width=1024 eibi_types_mask=0xffffffff filter_tod=1 anti_clutter=0",
            a < 0 ? 0 : a, b);
    s_eb_pas_envoye = s_eb_pas;
    s_eb_attente = true;
    s_eb_t = ms;
    s_t_mkr = ms;
}

int chemins(char *ch0, char *ch1, size_t n)
{
    if (s_proto == cfg::OWRX) {
        snprintf(ch0, n, "%sws/", s_base);
        ch1[0] = 0;
        return 1;
    }
    snprintf(ch0, n, "/%lu/SND", (unsigned long)s_horodatage);
    snprintf(ch1, n, "/%lu/W/F", (unsigned long)s_horodatage);
    return 2;
}

bool cible(char *hote, size_t nh, uint16_t *port, uint32_t *horo, bool *chiffre)
{
    if (!s_voulu || !s_hote[0]) return false;
    snprintf(hote, nh, "%s", s_hote);
    *port = s_port;
    *chiffre = s_chiffre || s_port == 443;
    *horo = s_horodatage;
    return true;
}

// ------------------------------------------------------------------ commandes
static void cmd_auth(int c)
{
    envoyer(c, "SET auth t=kiwi p=%s", s_mdp[0] ? s_mdp : "");
}

static volatile int s_sam = 0;
void am_synchrone(int b) { b = b < 0 ? 0 : (b > 3 ? 3 : b); if (s_sam != b) { s_sam = b; d_mod = true; } }
bool am_synchrone() { return s_sam != 0; }
int am_bande() { return s_sam; }
static volatile int s_agc_seuil = -100, s_gain_man = 50;
void agc_niveaux(int seuil, int gain)
{
    seuil = seuil < -130 ? -130 : (seuil > -60 ? -60 : seuil);
    gain = gain < 0 ? 0 : (gain > 120 ? 120 : gain);
    if (seuil != s_agc_seuil || gain != s_gain_man) { s_agc_seuil = seuil; s_gain_man = gain; d_agc = true; }
}
int agc_seuil() { return s_agc_seuil; }
int gain_manuel() { return s_gain_man; }

static void ow_mod();
static void cmd_mod()
{
    if (s_proto == cfg::OWRX) { ow_mod(); return; }
    int bas, haut, m = cfg::R.mode;
    cfg::bande_passante(m, cfg::R.bp[m], &bas, &haut);
    double f = cfg::R.freq_khz - s_foff;       // le protocole veut la frequence "bande de base"
    if (m == cfg::CW) f -= cfg::NOTE_CW / 1000.0;   // CW : la station affichee s'entend a 500 Hz
    if (f < 0) f = 0;
    const char *nom = cfg::nom_mode_kiwi(m);
    if (m == cfg::AM && s_sam) {
        static const char *SAM[4] = { "am", "sam", "sau", "sal" };
        nom = SAM[s_sam];
        // une seule bande : l'autre cote est presque ferme (la porteuse reste dans le filtre pour la boucle de phase)
        if (s_sam == 2 && bas < -500) bas = -500;
        if (s_sam == 3 && haut > 500) haut = 500;
    }
    envoyer(SND, "SET mod=%s low_cut=%d high_cut=%d freq=%.3f", nom, bas, haut, f);
}

static void cmd_agc()
{
    static const int DECAY[4] = { 1000, 2000, 1000, 300 };   // off / lent / moyen / rapide
    static const int HANG[4] = { 0, 1, 0, 0 };
    int a = cfg::R.agc & 3;
    envoyer(SND, "SET agc=%d hang=%d thresh=%d slope=6 decay=%d manGain=%d", a ? 1 : 0, HANG[a], s_agc_seuil, DECAY[a], s_gain_man);
}

// traitements du KiwiSDR (memes commandes que son interface web)
static void cmd_bruit()
{
    int nr = cfg::R.nr, notch = cfg::R.notch;
    // la methode spectrale (forte) ne sait pas filtrer les sifflements : avec l'anti-sifflement on reste en LMS
    int algo = (nr == 2 && !notch) ? 3 : ((nr || notch) ? 1 : 0);
    envoyer(SND, "SET nr algo=%d", algo);
    if (algo == 1) {                                   // wdsp LMS, valeurs par defaut du KiwiSDR
        for (int t = 0; t < 2; t++) {
            envoyer(SND, "SET nr type=%d param=0 pval=64", t);
            envoyer(SND, "SET nr type=%d param=1 pval=16", t);
            envoyer(SND, "SET nr type=%d param=2 pval=0.00008", t);
            envoyer(SND, "SET nr type=%d param=3 pval=0.125", t);
        }
        envoyer(SND, "SET nr type=0 en=%d", nr ? 1 : 0);
        envoyer(SND, "SET nr type=1 en=%d", notch ? 1 : 0);
    } else if (algo == 3) {                            // spectrale : gain 0 dB, alpha 0,95, SNR actif 30 dB
        envoyer(SND, "SET nr type=0 param=0 pval=1");
        envoyer(SND, "SET nr type=0 param=1 pval=0.95");
        envoyer(SND, "SET nr type=0 param=2 pval=1000");
        envoyer(SND, "SET nr type=0 en=1");
        envoyer(SND, "SET nr type=1 en=0");
    }
    if (cfg::R.nb) {                                   // anti-parasites : faible / moyen / fort (porte en us, seuil : plus bas = plus de coupes)
        static const int PORTE[4] = { 0, 100, 100, 200 }, SEUIL[4] = { 0, 70, 50, 30 };
        int n = cfg::R.nb > 3 ? 2 : cfg::R.nb;
        envoyer(SND, "SET nb algo=1");
        envoyer(SND, "SET nb type=0 param=0 pval=%d", PORTE[n]);
        envoyer(SND, "SET nb type=0 param=1 pval=%d", SEUIL[n]);
        envoyer(SND, "SET nb type=0 en=1");
    } else {
        envoyer(SND, "SET nb algo=0");
    }
}

static void calc_cascade()
{
    int z = s_force ? s_force_zoom : cfg::R.zoom;
    if (s_proto == cfg::OWRX) {                        // zoom fait par la carte : au moins 256 points du spectre a l'ecran
        int zmax = 0;
        while (s_ow_fft >> (zmax + 1) >= 256) zmax++;
        if (z > zmax) z = zmax;
    }
    float span = s_bw_max / (float)(1 << z);
    float cf = s_force ? s_force_cf : (float)cfg::R.freq_khz;
    s_wf_zoom = z;
    // le waterfall ne sort pas de la bande du recepteur
    if (cf - span / 2 < s_foff) cf = s_foff + span / 2;
    if (cf + span / 2 > s_foff + s_bw_max) cf = s_foff + s_bw_max - span / 2;
    s_wf_span = span;
    s_wf_cf = cf;
}

static void cmd_cascade()
{
    calc_cascade();
    if (s_proto == cfg::OWRX) return;
    envoyer(WF, "SET zoom=%d cf=%.3f", s_wf_zoom, s_wf_cf - s_foff);
}

static void params_snd()
{
    envoyer(SND, "SET squelch=0 max=0");
    envoyer(SND, "SET genattn=0");
    envoyer(SND, "SET gen=0 mix=-1");
    envoyer(SND, "SET ident_user=ES3C28P");
    cmd_mod();
    cmd_agc();
    s_compresse = cfg::R.adpcm != 0;
    envoyer(SND, "SET compression=%d", s_compresse ? 1 : 0);
    cmd_bruit();
    envoyer(SND, "SET keepalive");
    s_snd_pret = true;
}

static void params_wf()
{
    cmd_cascade();
    cmd_marqueurs();
    s_t_mkr = plat_ms();
    envoyer(WF, "SET maxdb=-10 mindb=-110");
    envoyer(WF, "SET wf_speed=%d", (int)(cfg::R.wf_vitesse < 1 ? 1 : (cfg::R.wf_vitesse > 4 ? 4 : cfg::R.wf_vitesse)));
    envoyer(WF, "SET wf_comp=0");
    envoyer(WF, "SET interp=13");
    envoyer(WF, "SET keepalive");
    s_wf_pret = true;
}

// ------------------------------------------------------------------ evenements reseau
static void ow_ouvert();
void ouvert(int c)
{
    if (c < 0 || c > 1) return;
    if (s_proto == cfg::OWRX) { s_ouvert[c] = true; s_echecs = 0; ow_ouvert(); return; }
    s_ouvert[c] = true;
    if (c == SND) s_echecs = 0;
    jv(true);                                           // "Connexion a ..." : reussie
    for (int i = 0; i < s_nj; i++)
        if (s_j[i].e == J_COURS && !strncmp(s_j[i].t, "Connexion à", 12)) s_j[i].e = J_OK;
    jv(false);
    j_ajout(J_OK, "%s : canal ouvert", c == SND ? "Son" : "Waterfall");
    if (c == SND) j_ajout(J_COURS, "Attente de l'accès…");
    if (c == SND) {
        s_adpcm = Adpcm();
        s_snd_pret = false;
        aud::vider();
        s_rssi = -127;
        etat_msg(ATTENTE, "Ouverture de la session…");
        s_t_ouvert = plat_ms();
    } else {
        s_wf_pret = false;
        wf::effacer();
    }
    cmd_auth(c);
    s_t_vie[c] = plat_ms();
}

void ferme(int c)
{
    if (c < 0 || c > 1) return;
    bool etait = s_ouvert[c];
    s_ouvert[c] = false;
    if (c == SND) s_snd_pret = false;
    if (c == WF) s_wf_pret = false;
    if (etait && s_voulu && (s_etat == ACTIF || s_etat == ATTENTE))
        etat_msg(PERDU, "Connexion perdue");
}

static void decoder_url(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            char h[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(h, nullptr, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = 0;
}

static void param(int c, const char *n, char *v)
{
    if (v) decoder_url(v);
    // journal des messages du KiwiSDR (sauf les gros blocs de configuration)
    if (strncmp(n, "load_", 5) && strcmp(n, "extint_list_json") && strncmp(n, "last_community", 14))
        plat_journal(c ? "W/F" : "SND", n, v);
    if (!strcmp(n, "too_busy")) {
        etat_msg(OCCUPE, "Récepteur plein, réessaie plus tard");
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(n, "badp")) {
        int code = v ? atoi(v) : 1;
        if (code == 0) return;                 // badp=0 : mot de passe accepte
        if (code == 1) etat_msg(REFUS, "Mot de passe refusé ou places réservées");
        else if (code == 5) etat_msg(REFUS, "Une seule connexion par adresse");
        else if (code == 6) etat_msg(REFUS, "Mise à jour en cours, réessaie dans 1 min");
        else etat_msg(REFUS, "Accès refusé par le récepteur");
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(n, "down")) {
        etat_msg(PANNE, "Récepteur en maintenance");
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(n, "redirect")) {
        // le recepteur est plein (ou a demenage) et indique un autre recepteur : on y va (3 fois au plus)
        char url[160] = "", h[64];
        uint16_t p = 0;
        if (v) { snprintf(url, sizeof url, "%s", v); decoder_url(url); }
        if (url[0] && s_redirections < 3 && srv::analyser_adresse(url, h, sizeof h, &p) &&
            (strcmp(h, s_hote) || p != s_port)) {
            s_redirections++;
            char nom[sizeof s_nom], mdp[sizeof s_mdp];
            snprintf(nom, sizeof nom, "%s", s_nom);
            snprintf(mdp, sizeof mdp, "%s", s_mdp);
            int r = s_redirections;
            connecter(h, p, mdp, nom);
            s_redirections = r;
            char m[64];
            snprintf(m, sizeof m, "Redirigé vers %s:%u…", h, (unsigned)p);
            etat_msg(CONNEXION, m);
            j_ajout(J_OK, "Récepteur plein : redirection n°%d", r);
        } else {
            etat_msg(PANNE, "Récepteur plein (redirection impossible)");
            s_voulu = false; s_gen = s_gen + 1;
        }
    } else if (!strncmp(n, "kick", 4) || !strcmp(n, "inactivity_kick") || !strcmp(n, "session_timeout")) {
        etat_msg(PERDU, "Session terminée par le récepteur");
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(n, "inactivity_timeout") && v) {
        s_limite = atoi(v);
    } else if (!strcmp(n, "audio_rate") && v && c == SND) {
        envoyer(SND, "SET AR OK in=%d out=%d", atoi(v), aud::FS_SORTIE);
    } else if (!strcmp(n, "sample_rate") && v && c == SND) {
        aud::debit_entree((float)atof(v));
        params_snd();
        jv(true);                                       // "Attente de l'acces..." devient "Acces accepte"
        bool remplace = false;
        for (int i = s_nj - 1; i >= 0 && !remplace; i--)
            if (!strcmp(s_j[i].t, "Attente de l'accès…")) {
                snprintf(s_j[i].t, sizeof s_j[0].t, "Accès accepté, son à %.0f Hz", atof(v));
                s_j[i].e = J_OK;
                remplace = true;
            }
        jv(false);
        if (!remplace) j_ajout(J_OK, "Accès accepté, son à %.0f Hz", atof(v));
        j_ajout(J_COURS, "Attente du son…");
        etat_msg(ACTIF, "En écoute");
    } else if (!strcmp(n, "rx_chan") && v && c == WF) {
        s_canal = atoi(v);
    } else if (!strcmp(n, "wf_chans") && v && c == WF) {
        s_nb_cascades = atoi(v);
    } else if (!strcmp(n, "wf_setup") && c == WF) {
        params_wf();
        if (s_canal >= 0 && s_nb_cascades >= 0) {
            if (s_canal >= s_nb_cascades) j_ajout(J_ECHEC, "Canal %d : pas de waterfall (réservé 1 à %d)", s_canal + 1, s_nb_cascades);
            else { j_ajout(J_OK, "Canal %d : waterfall disponible", s_canal + 1); j_ajout(J_COURS, "Attente du waterfall…"); }
        }
    } else if (!strcmp(n, "bandwidth") && v) {
        float b = (float)atof(v) / 1000.0f;
        if (b > 1000 && b < 70000) s_bw_max = b;
    } else if (!strcmp(n, "freq_offset") && v) {
        s_foff = (float)atof(v);
    } else if (!strcmp(n, "audio_adpcm_state") && v && c == SND) {
        s_adpcm.index = atoi(v);
        const char *k = strchr(v, ',');
        s_adpcm.prec = k ? atoi(k + 1) : 0;
        if (s_adpcm.index < 0 || s_adpcm.index > 88) s_adpcm.index = 0;
    }
}

static void msg(int c, const uint8_t *d, size_t n)
{
    // "MSG" + 1 octet + "nom=valeur nom=valeur ..."
    // assez grand pour les reponses de la recherche EiBi (PSRAM sur la carte)
    static char *buf = nullptr;
    static size_t taille = 0;
    if (!buf) {
        taille = 65536;
        buf = (char *)malloc(taille);
        if (!buf) { taille = 12288; buf = (char *)malloc(taille); }
        if (!buf) return;
    }
    if (n <= 4) return;
    size_t l = n - 4;
    if (l >= taille) l = taille - 1;
    memcpy(buf, d + 4, l);
    buf[l] = 0;
    char *p = buf;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char *fin = strchr(p, ' ');
        if (fin) *fin = 0;
        char *eg = strchr(p, '=');
        char *v = nullptr;
        if (eg) { *eg = 0; v = eg + 1; }
        if (!strcmp(p, "mkr") && v) {                  // JSON brut (noms deja codes %xx)
            if (s_eb_attente && s_eb_etat == EIBI_COURS) {
                lire_eibi(v);
                s_eb_attente = false;
                s_eb_echecs = 0;
                s_eb_pos += s_eb_pas_envoye;
            } else {
                lire_marqueurs(v);
            }
        }
        else param(c, p, v);
        if (!fin) break;
        p = fin + 1;
    }
}

static void son(const uint8_t *d, size_t n)
{
    // "SND" + drapeaux(1) + sequence(4, petit-boutiste) + S-metre(2, gros-boutiste) + donnees
    if (n < 10) return;
    uint8_t drapeaux = d[3];
    uint16_t sm = (uint16_t)(d[8] << 8 | d[9]);
    s_rssi = 0.1f * sm - 127.0f;
    s_ovfl = (drapeaux & 0x02) != 0;
    const uint8_t *p = d + 10;
    size_t l = n - 10;
    if (drapeaux & 0x08) return;               // stereo / IQ : pas utilise ici
    static int16_t *ech = nullptr;                    // 4 Ko en PSRAM
    if (!ech) ech = (int16_t *)plat_malloc_ps(2048 * sizeof(int16_t));
    if (!ech) return;
    int k = 0;
    if (drapeaux & 0x10) {                     // compresse : 2 echantillons par octet
        for (size_t i = 0; i < l && k < 2046; i++) {
            ech[k++] = s_adpcm.un(p[i] & 0x0F);
            ech[k++] = s_adpcm.un(p[i] >> 4);
        }
    } else {
        for (size_t i = 0; i + 1 < l && k < 2048; i += 2) ech[k++] = (int16_t)(p[i] << 8 | p[i + 1]);
    }
    aud::pousser(ech, k);
    if (!s_son1 && k > 0) {
        s_son1 = true;
        jv(true);                                       // "Attente du son..." -> recu
        for (int i = s_nj - 1; i >= 0; i--)
            if (!strcmp(s_j[i].t, "Attente du son…")) { snprintf(s_j[i].t, sizeof s_j[0].t, "Son reçu"); s_j[i].e = J_OK; break; }
        jv(false);
    }
}

static uint32_t s_diag_wf = 0, s_diag_wf_n = 0, s_diag_autre = 0;
static uint8_t s_diag_debut[8];

void diag_cascade(uint32_t *trames, uint32_t *taille, uint32_t *autres, uint8_t *debut8)
{
    *trames = s_diag_wf;
    *taille = s_diag_wf_n;
    *autres = s_diag_autre;
    memcpy(debut8, s_diag_debut, 8);
    s_diag_wf = 0;
    s_diag_autre = 0;
}

static void cascade(const uint8_t *d, size_t n)
{
    // "W/F" + 1 octet + x_bin(4) + drapeaux_zoom(4) + sequence(4) + donnees
    s_diag_wf++;
    s_diag_wf_n = (uint32_t)n;
    if (n < 16 + 256) return;
    if (!s_t_wf1) {
        s_t_wf1 = plat_ms();
        jv(true);
        for (int i = s_nj - 1; i >= 0; i--)
            if (!strcmp(s_j[i].t, "Attente du waterfall…")) { snprintf(s_j[i].t, sizeof s_j[0].t, "Waterfall reçu"); s_j[i].e = J_OK; break; }
        jv(false);
    }
    wf::ligne(d + 16, (int)(n - 16));
}

static void ow_recu(const uint8_t *d, size_t n);
void recu(int c, const uint8_t *d, size_t n)
{
    s_octets += n;
    if (s_proto == cfg::OWRX) { if (c == SND) ow_recu(d, n); return; }
    if (n < 3) return;
    if (!memcmp(d, "MSG", 3)) msg(c, d, n);
    else if (!memcmp(d, "SND", 3)) son(d, n);
    else if (!memcmp(d, "W/F", 3)) cascade(d, n);
    else if (c == WF) { s_diag_autre++; memcpy(s_diag_debut, d, n < 8 ? n : 8); }
}

bool sortant(int c, char *buf, size_t n)
{
    if (c < 0 || c > 1 || !s_file || s_tete[c] == s_queue[c]) return false;
    snprintf(buf, n, "%s", s_file[c][s_tete[c]]);
    s_tete[c] = (s_tete[c] + 1) % NF;
    return true;
}

void tic(uint32_t ms)
{
    if (!s_voulu) {
        vider_files();
        if (d_mod || d_wf) { d_mod = false; d_wf = false; calc_cascade(); }   // l'affichage suit quand meme
        return;
    }
    // reglages demandes par l'interface (au plus 8 par seconde)
    if (d_mod && s_snd_pret && ms - s_t_mod > 60) {
        d_mod = false;
        s_t_mod = ms;
        cmd_mod();
        if (!d_wf) {
            // le waterfall suit la frequence (meme centre que l'interface)
            float ancien = s_wf_cf;
            calc_cascade();
            if (fabsf(s_wf_cf - ancien) > 0.0005f) d_wf = true;
            s_wf_cf = ancien;
        }
    }
    if (d_wf && s_wf_pret && ms - s_t_wf > 150) {
        d_wf = false;
        s_t_wf = ms;
        cmd_cascade();
        if (s_proto == cfg::KIWI) envoyer(WF, "SET wf_speed=%d", s_force ? 4 : (int)cfg::R.wf_vitesse);
        d_mkr = true;
    } else if (d_wf && !s_wf_pret) {
        calc_cascade();
    }
    if (s_proto == cfg::OWRX) {
        // OpenWebRX : ni AGC, ni etiquettes, ni keepalive a gerer ici
        d_agc = d_bruit = d_comp = d_mkr = false;
        if (ms - s_t_debit >= 1000) { s_debit = s_octets * 1000 / (ms - s_t_debit + 1); s_octets = 0; s_t_debit = ms; }
        if (s_etat == CONNEXION && ms - s_t_connexion > 20000) etat_msg(PERDU, "Récepteur injoignable, nouvel essai…");
        if (s_etat == ATTENTE && s_ouvert[SND] && ms - s_t_ouvert > 15000 && s_ow_etape < 2)
            etat_msg(PERDU, "Pas de réponse du récepteur");
        return;
    }
    if (d_agc && s_snd_pret) { d_agc = false; cmd_agc(); }
    if (d_bruit && s_snd_pret) { d_bruit = false; cmd_bruit(); }
    // etiquettes : apres un changement (au plus 1 par seconde) et toutes les 2 minutes (horaires EiBi)
    eibi_avancer(ms);
    if (!s_pause_mkr && s_eb_etat != EIBI_COURS && s_wf_pret && ((d_mkr && ms - s_t_mkr > 1000) || ms - s_t_mkr > 120000)) {
        d_mkr = false;
        s_t_mkr = ms;
        cmd_marqueurs();
    }
    if (d_comp && s_snd_pret) {
        d_comp = false;
        s_compresse = cfg::R.adpcm != 0;
        s_adpcm = Adpcm();
        envoyer(SND, "SET compression=%d", s_compresse ? 1 : 0);
    }
    for (int c = 0; c < 2; c++) {
        if (s_ouvert[c] && ms - s_t_vie[c] > 2000) {
            s_t_vie[c] = ms;
            envoyer(c, "SET keepalive");
        }
    }
    if (ms - s_t_debit >= 1000) {
        s_debit = s_octets * 1000 / (ms - s_t_debit + 1);
        s_octets = 0;
        s_t_debit = ms;
    }
    // le recepteur ne repond pas du tout (la bibliotheque reessaie toute seule)
    if (s_etat == CONNEXION && ms - s_t_connexion > 20000) etat_msg(PERDU, "Récepteur injoignable, nouvel essai…");
    // la session SND n'a jamais repondu : on le dit
    if (s_etat == ATTENTE && s_ouvert[SND] && ms - s_t_ouvert > 15000 && !s_snd_pret)
        etat_msg(PERDU, "Pas de réponse du récepteur");
}

// =================================================================== OpenWebRX
// Protocole (code source d'OpenWebRX et OpenWebRX+, identique) : une seule WebSocket "<base>ws/".
//  -> "SERVER DE CLIENT client=openwebrx.js type=receiver"   <- "CLIENT DE SERVER server=openwebrx ..."
//  -> {"type":"connectionproperties","params":{"output_rate":12000,"hd_output_rate":48000}}
//  <- {"type":"config","value":{"center_freq":..., "samp_rate":..., "fft_size":..., "start_freq":..., ...}}
//  -> {"type":"dspcontrol","params":{"offset_freq":..., "mod":"nfm", "low_cut":..., "high_cut":...}}
//  -> {"type":"dspcontrol","action":"start"}
//  <- binaire : 1 = spectre, 2 = son (12 kHz), 4 = son HD (48 kHz, FM large), 6 = son HD stereo
//  <- {"type":"smeter","value":puissance lineaire}, {"type":"profiles","value":[{"id":"sdr|profil","name":...}]}
//  -> {"type":"selectprofile","params":{"profile":"sdr|profil"}}
// Les JSON de Python ont des espaces apres ':' et ',' : lecture tolerante.

static const char *ow_cle(const char *j, const char *cle)
{
    char motif[40];
    snprintf(motif, sizeof motif, "\"%s\"", cle);
    const char *p = strstr(j, motif);
    if (!p) return nullptr;
    p += strlen(motif);
    while (*p == ' ') p++;
    if (*p != ':') return nullptr;
    p++;
    while (*p == ' ') p++;
    return p;
}

static bool ow_nombre(const char *j, const char *cle, double *v)
{
    const char *p = ow_cle(j, cle);
    if (!p || !((*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return false;
    *v = atof(p);
    return true;
}

// chaine JSON decodee (\" \\ \uXXXX -> UTF-8) ; false si absente
static bool ow_lire_chaine(const char *p, char *dst, size_t n)
{
    if (!p || *p != '"') return false;
    p++;
    size_t k = 0;
    while (*p && *p != '"') {
        uint32_t c = (uint8_t)*p++;
        if (c == '\\' && *p) {
            char e = *p++;
            if (e == 'u') {
                char h[5] = { 0 };
                for (int i = 0; i < 4 && *p; i++) h[i] = *p++;
                c = (uint32_t)strtol(h, nullptr, 16);
            } else c = e == 'n' ? ' ' : (e == 't' ? ' ' : (uint8_t)e);
            char u[4];
            int lu = 0;
            if (c < 0x80) u[lu++] = (char)c;
            else if (c < 0x800) { u[lu++] = (char)(0xC0 | (c >> 6)); u[lu++] = (char)(0x80 | (c & 63)); }
            else { u[lu++] = (char)(0xE0 | (c >> 12)); u[lu++] = (char)(0x80 | ((c >> 6) & 63)); u[lu++] = (char)(0x80 | (c & 63)); }
            if (k + lu < n) { memcpy(dst + k, u, lu); k += lu; }
        } else if (k + 1 < n) {
            dst[k++] = (char)c;
        }
    }
    dst[k] = 0;
    return true;
}

static bool ow_chaine(const char *j, const char *cle, char *dst, size_t n) { return ow_lire_chaine(ow_cle(j, cle), dst, n); }

static const char *ow_nom_mode(int m)
{
    static const char *N[] = { "am", "lsb", "usb", "cw", "nfm", "wfm" };
    return (m >= 0 && m < cfg::NB_MODES) ? N[m] : "am";
}

static int ow_mode_de(const char *m)
{
    if (!strcmp(m, "lsb")) return cfg::LSB;
    if (!strcmp(m, "usb")) return cfg::USB;
    if (!strcmp(m, "cw")) return cfg::CW;
    if (!strcmp(m, "nfm") || !strncmp(m, "dmr", 3) || !strcmp(m, "ysf") || !strcmp(m, "dstar") || !strcmp(m, "nxdn")) return cfg::FM;
    if (!strcmp(m, "wfm") || !strcmp(m, "bcfm")) return cfg::WFM;
    return cfg::AM;
}

static void ow_bande()
{
    // la bande (profil) se comporte comme la couverture d'un KiwiSDR avec decalage
    s_foff = (float)((s_ow_cf - s_ow_sr / 2) / 1000.0);
    s_bw_max = (float)(s_ow_sr / 1000.0);
}

static void ow_mod()
{
    if (s_ow_etape < 2) return;
    int bas, haut, m = cfg::R.mode;
    cfg::bande_passante_ecran(m, cfg::R.bp[m], &bas, &haut);   // OpenWebRX : filtre autour de la porteuse (CW centre)
    long off = lround(cfg::R.freq_khz * 1000.0 - s_ow_cf);
    nr::raz();                                           // autre frequence ou autre mode : le bruit local est re-appris
    envoyer(SND, "{\"type\":\"dspcontrol\",\"params\":{\"offset_freq\":%ld,\"mod\":\"%s\",\"low_cut\":%d,\"high_cut\":%d,\"squelch_level\":-150}}",
            off, ow_nom_mode(m), bas, haut);
}

static void ow_ouvert()
{
    s_ow_etape = 0;
    s_ow_dec = AdpcmSync();
    s_ow_fs = 0;
    s_snd_pret = s_wf_pret = false;
    aud::vider();
    nr::vider();
    wf::effacer();
    s_rssi = -127;
    jv(true);
    for (int i = 0; i < s_nj; i++)
        if (s_j[i].e == J_COURS && !strncmp(s_j[i].t, "Connexion à", 12)) s_j[i].e = J_OK;
    jv(false);
    j_ajout(J_OK, "OpenWebRX : canal ouvert");
    j_ajout(J_COURS, "Attente de l'accès…");
    etat_msg(ATTENTE, "Ouverture de la session…");
    s_t_ouvert = plat_ms();
    envoyer(SND, "SERVER DE CLIENT client=openwebrx.js type=receiver");
    envoyer(SND, "{\"type\":\"connectionproperties\",\"params\":{\"output_rate\":12000,\"hd_output_rate\":48000}}");
}

static bool ow_dans_bande(double khz)
{
    return khz >= (s_ow_cf - s_ow_sr / 2) / 1000.0 && khz <= (s_ow_cf + s_ow_sr / 2) / 1000.0;
}

static void ow_config(const char *j)
{
    double v;
    bool bande = false;
    if (ow_nombre(j, "center_freq", &v)) { s_ow_cf = v; bande = true; }
    if (ow_nombre(j, "samp_rate", &v) && v > 0) { s_ow_sr = v; bande = true; }
    if (ow_nombre(j, "fft_size", &v) && v >= 64) s_ow_fft = (int)v;
    if (ow_nombre(j, "start_freq", &v)) s_ow_f_depart = v;
    char t[64];
    if (ow_chaine(j, "start_mod", t, sizeof t)) snprintf(s_ow_m_depart, sizeof s_ow_m_depart, "%s", t);
    if (ow_chaine(j, "fft_compression", t, sizeof t)) s_ow_fft_adpcm = !strcmp(t, "adpcm");
    if (ow_chaine(j, "audio_compression", t, sizeof t)) s_ow_son_adpcm = !strcmp(t, "adpcm");
    char sdr[32] = "", pr[32] = "";
    bool a = ow_chaine(j, "sdr_id", sdr, sizeof sdr), b = ow_chaine(j, "profile_id", pr, sizeof pr);
    if (a || b) {
        char ancien[64];
        snprintf(ancien, sizeof ancien, "%s", s_ow_profil);
        char *barre = strchr(ancien, '|');
        if (!a && barre) { *barre = 0; snprintf(sdr, sizeof sdr, "%s", ancien); }
        if (!b && barre) snprintf(pr, sizeof pr, "%s", barre + 1);
        snprintf(s_ow_profil, sizeof s_ow_profil, "%s|%s", sdr, pr);
        s_vprof = s_vprof + 1;
    }
    if (!bande || s_ow_sr <= 0 || s_ow_cf <= 0) return;
    ow_bande();
    // frequence hors de la bande : on prend celle proposee par le recepteur
    if (!ow_dans_bande(cfg::R.freq_khz)) {
        double f = s_ow_f_depart > 0 && ow_dans_bande(s_ow_f_depart / 1000.0) ? s_ow_f_depart / 1000.0 : s_ow_cf / 1000.0;
        cfg::R.freq_khz = f;
        if (s_ow_m_depart[0]) {
            cfg::R.mode = (uint8_t)ow_mode_de(s_ow_m_depart);
            cfg::R.pas = (uint8_t)cfg::pas_par_defaut(cfg::R.mode, f);
        }
        cfg::marquer();
    }
    if (s_ow_profil[0]) {                              // on retient la plage de cette bande
        int i = 0;
        while (i < s_nplages && strcmp(s_plages[i].id, s_ow_profil)) i++;
        if (i < MAX_PROFILS) {
            snprintf(s_plages[i].id, sizeof s_plages[i].id, "%s", s_ow_profil);
            s_plages[i].a = (s_ow_cf - s_ow_sr / 2) / 1000.0;
            s_plages[i].b = (s_ow_cf + s_ow_sr / 2) / 1000.0;
            if (i == s_nplages) s_nplages++;
            s_vplages = s_vplages + 1;
        }
    }
    calc_cascade();
    wf::effacer();
    s_bande_n = 0;                                      // nouvelle bande : spectre a recevoir
    if (s_ow_etape < 2) {
        s_ow_etape = 2;
        s_statut_demande = true;                        // plages de toutes les bandes (status.json)
        s_snd_pret = s_wf_pret = true;
        ow_mod();
        envoyer(SND, "{\"type\":\"dspcontrol\",\"action\":\"start\"}");
        jv(true);
        for (int i = s_nj - 1; i >= 0; i--)
            if (!strcmp(s_j[i].t, "Attente de l'accès…")) { s_j[i].e = J_OK; snprintf(s_j[i].t, sizeof s_j[0].t, "Accès accepté"); break; }
        jv(false);
        j_ajout(J_OK, "Bande %.3f à %.3f MHz", (s_ow_cf - s_ow_sr / 2) / 1e6, (s_ow_cf + s_ow_sr / 2) / 1e6);
        j_ajout(J_COURS, "Attente du son…");
        etat_msg(ACTIF, "En écoute");
        // bande choisie la derniere fois sur ce recepteur
        if (cfg::R.profil[0] && strcmp(cfg::R.profil, s_ow_profil))
            envoyer(SND, "{\"type\":\"selectprofile\",\"params\":{\"profile\":\"%s\"}}", cfg::R.profil);
    } else {
        ow_mod();                                       // changement de bande : decalage recalcule
    }
}

static void ow_profils(const char *j)
{
    if (!s_prof) s_prof = (Profil *)plat_malloc_ps(sizeof(Profil) * MAX_PROFILS);
    if (!s_prof) return;
    const char *p = ow_cle(j, "value");
    int n = 0;
    while (p && n < MAX_PROFILS) {
        const char *o = strchr(p, '{');
        if (!o) break;
        const char *f = strchr(o, '}');
        if (!f) break;
        // copie de l'objet pour ne pas lire les cles de l'objet suivant
        char obj[256];
        size_t l = (size_t)(f - o + 1);
        if (l >= sizeof obj) l = sizeof obj - 1;
        memcpy(obj, o, l);
        obj[l] = 0;
        Profil pr;
        if (ow_chaine(obj, "id", pr.id, sizeof pr.id) && ow_chaine(obj, "name", pr.nom, sizeof pr.nom)) {
            verrou_mkr(true);
            s_prof[n++] = pr;
            verrou_mkr(false);
        }
        p = f + 1;
    }
    verrou_mkr(true);
    s_nprof = n;
    verrou_mkr(false);
    s_vprof = s_vprof + 1;
}

static void ow_position(const char *j)
{
    const char *g = ow_cle(j, "receiver_gps");
    if (!g || *g != '{') return;
    double la, lo;
    if (ow_nombre(g, "lat", &la) && ow_nombre(g, "lon", &lo) && !(la == 47.0 && lo == 19.0) && (la != 0 || lo != 0)) {
        s_rx_lat = (float)la;                          // (47, 19) : valeur par defaut d'OpenWebRX, non reglee
        s_rx_lon = (float)lo;
    }
}

static void ow_texte(const char *j)
{
    if (!strncmp(j, "CLIENT DE SERVER", 16)) {
        j_ajout(J_OK, "Serveur OpenWebRX%s", strstr(j, "version=") ? "" : "");
        s_ow_etape = 1;
        return;
    }
    char type[24];
    if (!ow_chaine(j, "type", type, sizeof type)) return;
    if (!strcmp(type, "config")) { ow_position(j); ow_config(j); }
    else if (!strcmp(type, "smeter")) {
        double v;
        if (ow_nombre(j, "value", &v)) s_rssi = v > 1e-20 ? (float)(10.0 * log10(v)) : -127.0f;
    } else if (!strcmp(type, "profiles")) ow_profils(j);
    else if (!strcmp(type, "backoff")) {
        etat_msg(OCCUPE, "Récepteur plein, réessaie plus tard");
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(type, "sdr_error")) {
        char m[64] = "Erreur du récepteur";
        ow_chaine(j, "value", m, sizeof m);
        if (strstr(m, "No SDR")) snprintf(m, sizeof m, "Aucune clé SDR disponible");
        etat_msg(PANNE, m);
        s_voulu = false; s_gen = s_gen + 1;
    } else if (!strcmp(type, "receiver_details")) {
        ow_position(j);
        char nom[48];
        if (!s_nom[0] && ow_chaine(j, "receiver_name", nom, sizeof nom)) snprintf(s_nom, sizeof s_nom, "%s", nom);
    }
}

// son compresse avec mots de synchronisation "SYNC" + etat du codeur, toutes les 1000 octets
static int ow_adpcm(const uint8_t *p, size_t l, int16_t *o, int nmax, bool stereo)
{
    AdpcmSync &d = s_ow_dec;
    int k = 0;
    const int lsync = stereo ? 8 : 4;
    for (size_t i = 0; i < l && k < nmax - 2; i++) {
        uint8_t c = p[i];
        switch (d.phase) {
        case 0:
            if (c != (uint8_t)"SYNC"[d.sync++]) d.sync = (c == 'S') ? 1 : 0;
            if (d.sync == 4) { d.k = 0; d.phase = 1; }
            break;
        case 1:
            d.tampon[d.k++] = c;
            if (d.k == lsync) {
                d.a.index = (int16_t)(d.tampon[0] | d.tampon[1] << 8);
                d.a.prec = (int16_t)(d.tampon[2] | d.tampon[3] << 8);
                if (stereo) { d.b.index = (int16_t)(d.tampon[4] | d.tampon[5] << 8); d.b.prec = (int16_t)(d.tampon[6] | d.tampon[7] << 8); }
                if (d.a.index < 0 || d.a.index > 88) d.a.index = 0;
                if (d.b.index < 0 || d.b.index > 88) d.b.index = 0;
                d.compte = 1000;
                d.phase = 2;
            }
            break;
        default:
            if (!stereo) {
                o[k++] = d.a.un(c & 0x0F);
                o[k++] = d.a.un(c >> 4);
            } else if (i + 1 < l) {                  // gauche puis droite : on garde la moyenne
                int16_t g0 = d.a.un(c & 0x0F), g1 = d.a.un(c >> 4);
                uint8_t c2 = p[++i];
                int16_t d0 = d.b.un(c2 & 0x0F), d1 = d.b.un(c2 >> 4);
                o[k++] = (int16_t)((g0 + d0) / 2);
                o[k++] = (int16_t)((g1 + d1) / 2);
            }
            if (d.compte-- == 0) { d.sync = 0; d.phase = 0; }
            break;
        }
    }
    return k;
}

static void ow_son(const uint8_t *p, size_t l, int fs, bool stereo)
{
    if (fs != s_ow_fs) {                                 // passage FM large <-> autres modes
        s_ow_fs = fs;
        s_ow_dec = AdpcmSync();
        aud::vider();
        aud::debit_entree((float)fs);
    }
    static int16_t *ech = nullptr;
    if (!ech) ech = (int16_t *)plat_malloc_ps(8192 * sizeof(int16_t));
    if (!ech) return;
    int k = 0;
    if (s_ow_son_adpcm) k = ow_adpcm(p, l, ech, 8192, stereo);
    else if (!stereo) for (size_t i = 0; i + 1 < l && k < 8192; i += 2) ech[k++] = (int16_t)(p[i] | p[i + 1] << 8);
    else for (size_t i = 0; i + 3 < l && k < 8192; i += 4) ech[k++] = (int16_t)(((int16_t)(p[i] | p[i + 1] << 8) + (int16_t)(p[i + 2] | p[i + 3] << 8)) / 2);
    if (k <= 0) return;
    // OpenWebRX n'a pas de reduction de bruit : la carte la fait (pas en FM large, deja propre et a 48 kHz)
    nr::niveau(fs < 24000 ? cfg::R.nr : 0);
    nr::traiter(ech, k);
    aud::pousser(ech, k);
    if (!s_son1) {
        s_son1 = true;
        jv(true);
        for (int i = s_nj - 1; i >= 0; i--)
            if (!strcmp(s_j[i].t, "Attente du son…")) { snprintf(s_j[i].t, sizeof s_j[0].t, "Son reçu"); s_j[i].e = J_OK; break; }
        jv(false);
    }
}

// spectre (dB) -> 1024 points de la partie affichee (zoom de la carte), au format des lignes du KiwiSDR
static void ow_spectre(const uint8_t *p, size_t l)
{
    if (s_ow_sr <= 0) return;
    static float *db = nullptr;
    static uint8_t *ligne = nullptr;
    const int NMAX = 65536;
    if (!db) db = (float *)plat_malloc_ps(NMAX * sizeof(float));
    if (!ligne) ligne = (uint8_t *)plat_malloc_ps(1024);
    if (!db || !ligne) return;
    int n = 0;
    if (s_ow_fft_adpcm) {
        Adpcm a;                                         // codeur remis a zero a chaque spectre
        const int PAD = 10;                              // COMPRESS_FFT_PAD_N
        int k = 0;
        for (size_t i = 0; i < l; i++) {
            for (int h = 0; h < 2; h++) {
                int16_t v = a.un(h ? p[i] >> 4 : p[i] & 0x0F);
                if (k++ >= PAD && n < NMAX) db[n++] = v / 100.0f;
            }
        }
    } else {
        for (size_t i = 0; i + 3 < l && n < NMAX; i += 4) { float f; memcpy(&f, p + i, 4); db[n++] = f; }
    }
    if (n < 64) return;
    // toute la bande, reduite a NB_BANDE points (autoscan)
    if (!s_bande_db) s_bande_db = (float *)plat_malloc_ps(NB_BANDE * sizeof(float));
    if (s_bande_db) {
        for (int k = 0; k < NB_BANDE; k++) {
            int b0 = (int)((long)k * n / NB_BANDE), b1 = (int)((long)(k + 1) * n / NB_BANDE);
            if (b1 <= b0) b1 = b0 + 1;
            float m = -200;
            for (int b = b0; b < b1 && b < n; b++) if (db[b] > m) m = db[b];
            s_bande_db[k] = m;
        }
        s_bande_n = NB_BANDE;
        s_bande_compte = s_bande_compte + 1;
    }
    // vitesse du waterfall : on garde une ligne sur 6, 3, 2 ou toutes
    static const int GARDE[5] = { 3, 6, 3, 2, 1 };
    int g = GARDE[cfg::R.wf_vitesse <= 4 ? cfg::R.wf_vitesse : 2];
    if (++s_ow_trames % g) return;
    double debut = (s_ow_cf - s_ow_sr / 2) / 1000.0;              // kHz du premier point
    double kpp = s_ow_sr / 1000.0 / n;                             // kHz par point
    double a0 = s_wf_cf - s_wf_span / 2;
    for (int k = 0; k < 1024; k++) {
        double f0 = a0 + s_wf_span * k / 1024.0, f1 = a0 + s_wf_span * (k + 1) / 1024.0;
        int b0 = (int)floor((f0 - debut) / kpp), b1 = (int)floor((f1 - debut) / kpp);
        if (b1 <= b0) b1 = b0 + 1;
        float m = -200;
        for (int b = b0; b < b1; b++) if (b >= 0 && b < n && db[b] > m) m = db[b];
        int v = (int)lroundf(m + 268.0f);                          // meme echelle que le KiwiSDR (dB + 268)
        ligne[k] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    if (!s_t_wf1) {
        s_t_wf1 = plat_ms();
        jv(true);
        for (int i = s_nj - 1; i >= 0; i--)
            if (!strcmp(s_j[i].t, "Attente du waterfall…")) { snprintf(s_j[i].t, sizeof s_j[0].t, "Waterfall reçu"); s_j[i].e = J_OK; break; }
        jv(false);
    }
    s_diag_wf++;
    s_diag_wf_n = (uint32_t)l;
    wf::ligne(ligne, 1024);
}

static void ow_recu(const uint8_t *d, size_t n)
{
    if (n == 0) return;
    if (d[0] == '{' || d[0] == 'C') {                  // texte (JSON ou poignee de main)
        static char *buf = nullptr;
        static const size_t T = 65536;
        if (!buf) buf = (char *)malloc(T);
        if (!buf) return;
        size_t l = n < T - 1 ? n : T - 1;
        memcpy(buf, d, l);
        buf[l] = 0;
        ow_texte(buf);
        return;
    }
    switch (d[0]) {
    case 1: ow_spectre(d + 1, n - 1); break;
    case 2: ow_son(d + 1, n - 1, 12000, false); break;
    case 4: ow_son(d + 1, n - 1, 48000, false); break;
    case 6: ow_son(d + 1, n - 1, 48000, true); break;
    default: s_diag_autre++; memcpy(s_diag_debut, d, n < 8 ? n : 8); break;
    }
}

int profils(Profil *dst, int nmax)
{
    verrou_mkr(true);
    int n = s_nprof < nmax ? s_nprof : nmax;
    if (s_prof) memcpy(dst, s_prof, n * sizeof(Profil));
    else n = 0;
    verrou_mkr(false);
    return n;
}

int profil_courant()
{
    for (int i = 0; i < s_nprof; i++) if (s_prof && !strcmp(s_prof[i].id, s_ow_profil)) return i;
    return -1;
}

const char *nom_profil()
{
    int i = profil_courant();
    return i >= 0 ? s_prof[i].nom : "";
}

uint32_t version_profils() { return s_vprof; }

bool plage_profil(const char *id, double *a, double *b)
{
    for (int i = 0; i < s_nplages; i++)
        if (!strcmp(s_plages[i].id, id)) { *a = s_plages[i].a; *b = s_plages[i].b; return true; }
    // sinon d'apres status.json : le nom affiche d'une bande est "nom du SDR + espace + nom du profil"
    bool ok = false;
    verrou_mkr(true);
    for (int i = 0; i < s_nprof && s_prof && !ok; i++) {
        if (strcmp(s_prof[i].id, id)) continue;
        for (int k = 0; k < s_npnom; k++)
            if (!strncmp(s_pnom[k].nom, s_prof[i].nom, sizeof s_prof[i].nom - 1)) { *a = s_pnom[k].a; *b = s_pnom[k].b; ok = true; break; }
    }
    verrou_mkr(false);
    return ok;
}

uint32_t version_plages() { return s_vplages + s_vprof; }
void pause_etiquettes(bool pause) { if (s_pause_mkr && !pause) d_mkr = true; s_pause_mkr = pause; }

bool statut_a_demander(char *url, size_t n)
{
    if (!s_statut_demande || s_proto != cfg::OWRX || !s_voulu) return false;
    s_statut_demande = false;
    bool defaut = s_port == (s_chiffre ? 443 : 80);
    if (defaut) snprintf(url, n, "%s://%s%sstatus.json", s_chiffre ? "https" : "http", s_hote, s_base);
    else snprintf(url, n, "%s://%s:%u%sstatus.json", s_chiffre ? "https" : "http", s_hote, (unsigned)s_port, s_base);
    return true;
}

// {"receiver": {..., "gps": {"lat": .., "lon": ..}}, "sdrs": [{"name": "RTL", "type": .., "profiles":
//   [{"name": "2m", "center_freq": 145000000, "sample_rate": 2400000}, ...]}, ...]}
int statut_json(const char *j)
{
    const char *g = ow_cle(j, "gps");
    if (g && *g == '{' && s_rx_lat == 999) {
        double la, lo;
        if (ow_nombre(g, "lat", &la) && ow_nombre(g, "lon", &lo) && !(la == 47.0 && lo == 19.0) && (la != 0 || lo != 0)) { s_rx_lat = (float)la; s_rx_lon = (float)lo; }
    }
    const char *p = ow_cle(j, "sdrs");
    if (!p || *p != '[') return 0;
    PlageNom lu[MAX_PROFILS];
    int n = 0;
    p++;
    while (n < MAX_PROFILS) {
        const char *o = strchr(p, '{');
        if (!o) break;
        char sdr[40] = "";
        ow_chaine(o, "name", sdr, sizeof sdr);
        const char *pr = ow_cle(o, "profiles");
        if (!pr || *pr != '[') break;
        const char *q = pr + 1;
        while (n < MAX_PROFILS) {
            const char *ob = strchr(q, '{'), *fin = strchr(q, ']');
            if (!ob || (fin && fin < ob)) { q = fin ? fin + 1 : q + strlen(q); break; }
            const char *f = strchr(ob, '}');
            if (!f) { q = ob + strlen(ob); break; }
            char obj[256];
            size_t l = (size_t)(f - ob + 1);
            if (l >= sizeof obj) l = sizeof obj - 1;
            memcpy(obj, ob, l);
            obj[l] = 0;
            char nom[40] = "";
            double cf = 0, sr = 0;
            if (ow_chaine(obj, "name", nom, sizeof nom) && ow_nombre(obj, "center_freq", &cf) && ow_nombre(obj, "sample_rate", &sr) && sr > 0) {
                snprintf(lu[n].nom, sizeof lu[n].nom, "%s %s", sdr, nom);
                lu[n].nom[sizeof s_prof[0].nom - 1] = 0;         // meme longueur que le nom recu par la WebSocket
                lu[n].a = (cf - sr / 2) / 1000.0;
                lu[n].b = (cf + sr / 2) / 1000.0;
                n++;
            }
            q = f + 1;
        }
        const char *suite = strchr(q, '}');                      // fin de l'objet SDR
        if (!suite) break;
        p = suite + 1;
    }
    verrou_mkr(true);
    memcpy(s_pnom, lu, sizeof(PlageNom) * n);
    s_npnom = n;
    verrou_mkr(false);
    s_vplages = s_vplages + 1;
    return n;
}

bool position_recepteur(float *lat, float *lon)
{
    if (s_proto != cfg::OWRX || s_rx_lat == 999) return false;
    *lat = s_rx_lat;
    *lon = s_rx_lon;
    return true;
}

int spectre_bande(const float **db, uint32_t *compte)
{
    if (db) *db = s_bande_db;
    if (compte) *compte = s_bande_compte;
    return (s_proto == cfg::OWRX && s_ow_etape >= 2) ? s_bande_n : 0;
}

void choisir_profil(const char *id)
{
    if (s_proto != cfg::OWRX || !id || !id[0]) return;
    snprintf(cfg::R.profil, sizeof cfg::R.profil, "%s", id);
    cfg::marquer();
    if (s_ow_etape >= 2) envoyer(SND, "{\"type\":\"selectprofile\",\"params\":{\"profile\":\"%s\"}}", id);
}

}  // namespace kw
