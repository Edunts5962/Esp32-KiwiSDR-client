// fm.cpp - client FM-DX Webserver (etat JSON + son MP3)
#include "fm.h"
#include "fmliste.h"
#include "audio.h"
#include "plateforme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ALLOC(n) plat_malloc_interne(n)    // zone de travail en memoire interne : decodage plus rapide
#include "minimp3.h"

namespace fm {

static volatile bool s_voulu = false;
static volatile uint32_t s_gen = 0;
static char s_url[100], s_nom[48];
static bool s_ouvert[2] = { false, false };
static bool s_demande_mp3 = false;
static uint32_t s_t_connexion = 0, s_t_texte = 0, s_t_son = 0;

static Info s_info;
static volatile int s_occupe = 0;
static void verrou(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe, 0, __ATOMIC_RELEASE);
}

static void etat_msg(int e, const char *m)
{
    verrou(true);
    s_info.etat = e;
    snprintf(s_info.message, sizeof s_info.message, "%s", m);
    verrou(false);
}

// ordres vers le tuner (file courte)
static char s_ordre[48] = "";

// ---------------------------------------------------------------- MP3
static mp3dec_t *s_dec = nullptr;
static uint8_t *s_mp3 = nullptr;                // octets MP3 pas encore decodes
static size_t s_nmp3 = 0;
static const size_t CAP_MP3 = 32768;
static int16_t *s_pcm = nullptr, *s_mono = nullptr;   // tampons sur le tas (PSRAM)
static char *s_json = nullptr;
static uint32_t s_octets_son = 0, s_t_debit = 0;

static void raz_son()
{
    if (s_dec) mp3dec_init(s_dec);
    s_nmp3 = 0;
    s_octets_son = 0;
    verrou(true);
    s_info.son_hz = 0;
    s_info.kbps = 0;
    s_info.trames = 0;
    verrou(false);
}

static uint32_t s_ms_decodage = 0;              // temps passe a decoder (mesure de charge)

static void decoder()
{
    uint32_t t0 = plat_ms();
    // minimp3 doit voir l'en-tete de la trame suivante : sinon il saute des octets.
    // On ne decode donc que si au moins deux trames completes (1441 octets au plus) attendent.
    const size_t MARGE = 2 * 1441 + 8;
    size_t pos = 0;
    while (s_nmp3 - pos >= MARGE) {
        mp3dec_frame_info_t fi;
        int n = mp3dec_decode_frame(s_dec, s_mp3 + pos, (int)(s_nmp3 - pos), s_pcm, &fi);
        if (fi.frame_bytes == 0) break;                  // trame incomplete : attendre la suite
        pos += (size_t)fi.frame_bytes;
        if (n <= 0) continue;
        if (fi.hz != s_info.son_hz) {
            aud::debit_entree((float)fi.hz);
            verrou(true);
            s_info.son_hz = fi.hz;
            verrou(false);
        }
        const int16_t *src = s_pcm;
        if (fi.channels == 2) {
            for (int i = 0; i < n; i++) s_mono[i] = (int16_t)(((int32_t)s_pcm[2 * i] + s_pcm[2 * i + 1]) / 2);
            src = s_mono;
        }
        aud::pousser(src, n);
        s_info.trames++;
    }
    if (pos) {
        memmove(s_mp3, s_mp3 + pos, s_nmp3 - pos);
        s_nmp3 -= pos;
    }
    s_ms_decodage += plat_ms() - t0;
}

// ---------------------------------------------------------------- etat JSON
static void lire_etat(const char *j)
{
    Info t;
    verrou(true);
    t = s_info;
    verrou(false);
    double f = fml::json_nombre(j, "freq", -1);
    if (f > 0) { t.mhz = (float)f; t.mhz_serveur = (float)f; }
    t.maj++;
    t.sig = (float)fml::json_nombre(j, "sig", t.sig);
    t.stereo = fml::json_vrai(j, "st");
    t.rds = fml::json_vrai(j, "rds");
    char ps[16];
    fml::json_chaine(j, "ps", ps, sizeof ps);
    snprintf(t.ps, sizeof t.ps, "%s", strcmp(ps, "?") ? ps : "");
    char rt0[68], rt1[68];
    fml::json_chaine(j, "rt0", rt0, sizeof rt0);
    fml::json_chaine(j, "rt1", rt1, sizeof rt1);
    snprintf(t.rt, sizeof t.rt, "%s", rt0[0] ? rt0 : rt1);
    fml::json_chaine(j, "pi", t.pi, sizeof t.pi);
    if (!strcmp(t.pi, "?")) t.pi[0] = 0;
    t.auditeurs = (int)fml::json_nombre(j, "users", t.auditeurs);
    const char *tx = strstr(j, "\"txInfo\":{");
    t.tx[0] = t.tx_ville[0] = t.tx_pays[0] = 0;
    t.tx_km = 0;
    t.tx_erp = 0;
    if (tx) {
        fml::json_chaine(tx, "tx", t.tx, sizeof t.tx);
        fml::json_chaine(tx, "city", t.tx_ville, sizeof t.tx_ville);
        fml::json_chaine(tx, "itu", t.tx_pays, sizeof t.tx_pays);
        t.tx_km = (int)fml::json_nombre(tx, "dist", 0);
        t.tx_erp = (float)fml::json_nombre(tx, "erp", 0);
    }
    t.etat = ACTIF;
    const char *m = !s_ouvert[SON] ? "Connexion du son…"
                  : (t.son_hz && plat_ms() - s_t_son < 8000) ? "En écoute"
                  : t.son_hz ? "Pas de son du serveur" : "Attente du son…";
    snprintf(t.message, sizeof t.message, "%s", m);
    verrou(true);
    s_info = t;
    verrou(false);
}

// ---------------------------------------------------------------- API
void init()
{
    memset(&s_info, 0, sizeof s_info);
    snprintf(s_info.message, sizeof s_info.message, "FM arrêtée");
    if (!s_mp3) s_mp3 = (uint8_t *)malloc(CAP_MP3);
    if (!s_dec) s_dec = (mp3dec_t *)plat_malloc_interne(sizeof(mp3dec_t));
    if (!s_pcm) s_pcm = (int16_t *)plat_malloc_interne(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!s_mono) s_mono = (int16_t *)plat_malloc_interne(MINIMP3_MAX_SAMPLES_PER_FRAME / 2 * sizeof(int16_t));
    if (!s_json) s_json = (char *)malloc(4096);
    if (s_dec) mp3dec_init(s_dec);
}

void connecter(const char *url, const char *nom)
{
    snprintf(s_url, sizeof s_url, "%s", url);
    snprintf(s_nom, sizeof s_nom, "%s", nom && nom[0] ? nom : url);
    verrou(true);
    memset(&s_info, 0, sizeof s_info);
    snprintf(s_info.serveur, sizeof s_info.serveur, "%s", s_nom);
    snprintf(s_info.url, sizeof s_info.url, "%s", s_url);
    s_info.etat = CONNEXION;
    snprintf(s_info.message, sizeof s_info.message, "Connexion…");
    verrou(false);
    s_ordre[0] = 0;
    aud::vider();
    raz_son();
    s_voulu = true;
    s_t_connexion = plat_ms();
    s_t_texte = s_t_son = 0;
    s_gen = s_gen + 1;
}

void deconnecter()
{
    if (!s_voulu) return;
    s_voulu = false;
    etat_msg(ARRET, "FM arrêtée");
    s_gen = s_gen + 1;
}

bool actif() { return s_voulu; }

void accorder(float mhz)
{
    if (mhz < 0.1f || mhz > 200) return;
    snprintf(s_ordre, sizeof s_ordre, "T%lu", (unsigned long)(mhz * 1000.0f + 0.5f));
    verrou(true);
    s_info.mhz = mhz;                         // affichage immediat, corrige par le serveur
    verrou(false);
}

void info(Info *dst)
{
    verrou(true);
    *dst = s_info;
    verrou(false);
}

const char *message() { return s_info.message; }
uint32_t generation() { return s_gen; }

bool cible(char *hote, size_t nh, uint16_t *port, bool *tls, char *base, size_t nb)
{
    if (!s_voulu) return false;
    return fml::analyser_url(s_url, hote, nh, port, tls, base, nb);
}

void ouvert(int c)
{
    if (c < 0 || c > 1) return;
    s_ouvert[c] = true;
    if (c == SON) {
        s_demande_mp3 = true;
        raz_son();
        s_t_son = plat_ms();
    } else {
        s_t_texte = plat_ms();
    }
}

void ferme(int c)
{
    if (c < 0 || c > 1) return;
    bool etait = s_ouvert[c];
    s_ouvert[c] = false;
    if (etait && s_voulu) etat_msg(PERDU, c == SON ? "Son coupé, nouvel essai…" : "Connexion perdue, nouvel essai…");
}

void recu(int c, const uint8_t *d, size_t n)
{
    if (!s_voulu || !n) return;
    if (c == TEXTE) {
        if (!s_json) return;
        if (n >= 4096) n = 4095;
        memcpy(s_json, d, n);
        s_json[n] = 0;
        if (s_json[0] == '{') lire_etat(s_json);
        s_t_texte = plat_ms();
        return;
    }
    if (d[0] == '{' || !s_mp3 || !s_dec || !s_pcm || !s_mono) return;                   // message texte du flux son
    s_octets_son += n;
    s_t_son = plat_ms();
    if (s_nmp3 + n > CAP_MP3) s_nmp3 = 0;               // retard : on repart de zero
    if (n > CAP_MP3) return;
    memcpy(s_mp3 + s_nmp3, d, n);
    s_nmp3 += n;
    decoder();
}

bool sortant(int c, char *buf, size_t n)
{
    if (c == SON && s_ouvert[SON] && s_demande_mp3) {
        s_demande_mp3 = false;
        snprintf(buf, n, "{\"type\":\"fallback\",\"data\":\"mp3\"}");
        return true;
    }
    if (c == TEXTE && s_ouvert[TEXTE] && s_ordre[0]) {
        snprintf(buf, n, "%s", s_ordre);
        s_ordre[0] = 0;
        return true;
    }
    return false;
}

void tic(uint32_t ms)
{
    if (!s_voulu) return;
    if (ms - s_t_debit >= 2000) {
        verrou(true);
        s_info.kbps = (int)(s_octets_son * 8 / (ms - s_t_debit + 1));
        s_info.charge = (int)(s_ms_decodage * 100 / (ms - s_t_debit + 1));
        s_ms_decodage = 0;
        verrou(false);
        s_octets_son = 0;
        s_t_debit = ms;
    }
    if (s_info.etat == CONNEXION && ms - s_t_connexion > 20000 && !s_ouvert[TEXTE])
        etat_msg(CONNEXION, "Serveur injoignable, nouvel essai…");
}

}  // namespace fm
