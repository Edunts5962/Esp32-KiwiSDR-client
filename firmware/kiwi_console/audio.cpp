// audio.cpp - reserve, reechantillonnage 12 kHz -> 48 kHz, filtre doux
#include "audio.h"
#include "plateforme.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace aud {

static const uint32_t N = 262144;              // ~5 s a 48 kHz (FM), ~20 s a 12 kHz ; en PSRAM
static const uint32_t MASQUE = N - 1;
static int16_t *s_anneau = nullptr;
static volatile uint32_t s_ecrit = 0;          // total ecrit (producteur)
static volatile uint32_t s_lu = 0;             // position entiere de lecture (consommateur)
static volatile float s_fs = 12000.0f;
static volatile int s_vise_ms = 350;
static volatile bool s_lecture = false;
static volatile bool s_raz = false;
static volatile uint32_t s_coupures = 0;
static volatile int s_crete = 0;
// reserve qui s'adapte : chaque coupure rapprochee l'agrandit, un long calme la reduit
static volatile int s_extra_ms = 0;
static uint32_t s_t_coupure = 0, s_t_calme = 0;
static const int EXTRA_MAX = 1000;          // au plus 1 s de plus : au-dela, un changement de frequence s'entendrait trop tard
// fondu (5 ms) : le son ne s'arrete et ne repart jamais d'un coup
static float s_fondu = 0;
static float s_dernier = 0;
static bool s_saut = false;                     // rattrapage demande (fait quand le son est descendu)

static double s_frac = 0;                      // phase fractionnaire (consommateur)
static float s_corr = 1.0f;
static float s_lp1 = 0, s_lp2 = 0;             // filtre passe-bas (images du reechantillonnage)
static volatile bool s_limiteur = true;
static float s_env = 0;                         // enveloppe du limiteur

void limiteur(bool oui) { s_limiteur = oui; }

static volatile int s_gain_niv = 0;
static const float GAINS[3] = { 1.0f, 2.0f, 4.0f };   // 0, +6, +12 dB
void gain_kiwi(int n) { s_gain_niv = n < 0 ? 0 : (n > 2 ? 2 : n); }
int gain_kiwi() { return s_gain_niv; }

void init()
{
    if (!s_anneau) s_anneau = (int16_t *)plat_malloc_ps(N * sizeof(int16_t));
    if (s_anneau) memset(s_anneau, 0, N * sizeof(int16_t));
}

void vider() { s_raz = true; }
void debit_entree(float hz) { if (hz > 4000 && hz < 50000) s_fs = hz; }
float debit() { return s_fs; }
void reserve_visee(int ms) { s_vise_ms = ms < 80 ? 80 : (ms > 2000 ? 2000 : ms); }
int reserve_auto_ms() { return s_extra_ms; }

static void coupure()
{
    uint32_t t = plat_ms();
    s_coupures = s_coupures + 1;
    // deux coupures a moins de 60 s : le reseau est irregulier, on garde plus d'avance
    if (s_t_coupure && t - s_t_coupure < 60000 && s_extra_ms < EXTRA_MAX) s_extra_ms = s_extra_ms + (s_extra_ms < 400 ? 200 : 100);
    s_t_coupure = t ? t : 1;
    s_t_calme = s_t_coupure;
}
bool en_lecture() { return s_lecture; }
uint32_t coupures() { return s_coupures; }
int niveau_crete() { return s_crete; }

int reserve_ms()
{
    uint32_t n = s_ecrit - s_lu;
    if (n > N) n = N;
    return (int)(n * 1000.0f / s_fs);
}

void pousser(const int16_t *s, int n)
{
    if (!s_anneau) return;
    uint32_t e = s_ecrit;
    for (int i = 0; i < n; i++) s_anneau[(e + i) & MASQUE] = s[i];
    s_ecrit = e + n;
}

uint32_t curseur_brut() { return s_ecrit; }

int lire_brut(uint32_t &c, int16_t *dst, int nmax)
{
    if (!s_anneau) return 0;
    uint32_t e = s_ecrit;
    if (e - c > N / 2) c = e - N / 4;          // lecteur trop en retard : on saute
    int n = (int)(e - c);
    if (n > nmax) n = nmax;
    for (int i = 0; i < n; i++) dst[i] = s_anneau[(c + i) & MASQUE];
    c += n;
    return n;
}

// ---- tonalite : deux filtres en plateau (graves 250 Hz, aigus 2,5 kHz), a la cadence de sortie ----
struct Biquad { float b0, b1, b2, a1, a2, z1, z2; };
static Biquad s_grave = { 1, 0, 0, 0, 0, 0, 0 }, s_aigu = { 1, 0, 0, 0, 0, 0, 0 };
static volatile bool s_ton = false;
static void plateau(Biquad &q, float f0, float gain_db, bool haut)
{
    float A = powf(10.0f, gain_db / 40.0f), w = 2 * 3.14159265f * f0 / FS_SORTIE;
    float c = cosf(w), s = sinf(w), al = s / 2 * 1.41421356f, r = 2 * sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    if (!haut) {
        b0 = A * ((A + 1) - (A - 1) * c + r); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - r);
        a0 = (A + 1) + (A - 1) * c + r; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - r;
    } else {
        b0 = A * ((A + 1) + (A - 1) * c + r); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - r);
        a0 = (A + 1) - (A - 1) * c + r; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - r;
    }
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}
void tonalite(int g, int a)
{
    g = g < -8 ? -8 : (g > 8 ? 8 : g);
    a = a < -8 ? -8 : (a > 8 ? 8 : a);
    s_ton = false;
    plateau(s_grave, 250, (float)g, false);
    plateau(s_aigu, 2500, (float)a, true);
    s_ton = g != 0 || a != 0;
}
static inline float biquad(Biquad &q, float x)
{
    float y = q.b0 * x + q.z1;
    q.z1 = q.b1 * x - q.a1 * y + q.z2;
    q.z2 = q.b2 * x - q.a2 * y;
    return y;
}

static volatile bool s_silence = false;
static float s_sil_gain = 1.0f;
void silence(bool oui) { s_silence = oui; }
bool silence() { return s_silence; }

void lire_sortie(int16_t *st, int trames)
{
    if (s_raz) {
        s_raz = false;
        s_lu = s_ecrit;
        s_frac = 0;
        s_lecture = false;
        s_corr = 1.0f;
        s_fondu = 0;
        s_t_coupure = 0;
        s_saut = false;
    }
    {   // 3 minutes sans coupure : la reserve ajoutee redescend doucement
        uint32_t t = plat_ms();
        if (s_extra_ms > 0 && t - s_t_calme > 180000) { s_extra_ms = s_extra_ms > 100 ? s_extra_ms - 100 : 0; s_t_calme = t; }
    }
    float fs = s_fs;
    uint32_t vise = (uint32_t)(fs * (s_vise_ms + s_extra_ms) / 1000.0f);
    if (vise > N / 4) vise = N / 4;
    uint32_t dispo = s_ecrit - s_lu;
    if (dispo > N - 4096) { s_lu = s_ecrit - vise; dispo = vise; }           // debordement : on rattrape tout de suite
    else if (s_lecture && dispo > 3 * vise + (uint32_t)(fs / 2)) s_saut = true;   // trop de retard : on rattrape, en fondu
    if (s_saut && s_fondu <= 0) {                                            // le son est descendu : on saute, puis il remonte
        s_lu = s_ecrit - vise;
        dispo = vise;
        s_saut = false;
    }
    if (!s_lecture && dispo >= vise) s_lecture = true;

    // correction lente de la derive : +-1 % au maximum, inaudible
    float err = ((float)dispo - (float)vise) / (float)(vise + 1);
    float cible = 1.0f + 0.01f * (err > 1 ? 1 : (err < -1 ? -1 : err));
    s_corr += (cible - s_corr) * 0.02f;
    double pas = (double)fs / FS_SORTIE * s_corr;

    int crete = 0;
    const float pas_fondu = 1.0f / (FS_SORTIE * 0.005f);   // 5 ms
    const uint32_t bas = (uint32_t)(fs * 0.006f) + 2;       // moins de 6 ms d'avance : on baisse le son avant le manque
    for (int i = 0; i < trames; i++) {
        float v = 0;
        if (s_lecture && s_anneau) {
            uint32_t reste = s_ecrit - s_lu;
            if (reste < 2) {                    // plus rien (le son est deja descendu) : on se remet en reserve
                s_lecture = false;
                coupure();
            } else {
                int16_t a = s_anneau[s_lu & MASQUE], b = s_anneau[(s_lu + 1) & MASQUE];
                v = a + (float)((b - a) * s_frac);
                s_frac += pas;
                while (s_frac >= 1.0) { s_frac -= 1.0; s_lu = s_lu + 1; }
                s_dernier = v;
            }
            float cible = (reste < bas || s_saut) ? 0.0f : 1.0f;
            if (s_fondu < cible) { s_fondu += pas_fondu; if (s_fondu > 1) s_fondu = 1; }
            else if (s_fondu > cible) { s_fondu -= pas_fondu; if (s_fondu < 0) s_fondu = 0; }
        } else {                                // en reserve : on finit de descendre depuis le dernier echantillon
            if (s_fondu > 0) { s_fondu -= pas_fondu; if (s_fondu < 0) s_fondu = 0; }
            v = s_dernier;
        }
        v *= s_fondu;
        // deux poles a ~5,5 kHz : enleve le grain du reechantillonnage lineaire
        float y = v;
        if (fs < 24000) {
            // son du KiwiSDR (~12 kHz) : gain choisi puis filtre ; la FM (48 kHz) passe telle quelle
            v *= GAINS[s_gain_niv];
            s_lp1 += (v - s_lp1) * 0.52f;
            s_lp2 += (s_lp1 - s_lp2) * 0.52f;
            y = s_lp2;
        }
        if (s_ton) y = biquad(s_aigu, biquad(s_grave, y));
        if (s_limiteur || (fs < 24000 && s_gain_niv)) {   // avec du gain, le limiteur reste toujours actif
            // limiteur : attaque immediate, retour lent (~0,3 s) ; plafond ~ -9 dB pleine echelle
            float a = y < 0 ? -y : y;
            s_env = a > s_env ? a : s_env * 0.99993f;
            if (s_env > 11500.0f) y *= 11500.0f / s_env;
        }
        // squelch : fondu d'environ 10 ms, sans claquement
        s_sil_gain += ((s_silence ? 0.0f : 1.0f) - s_sil_gain) * 0.004f;
        y *= s_sil_gain;
        int s = (int)y;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        int a = s < 0 ? -s : s;
        if (a > crete) crete = a;
        st[2 * i] = st[2 * i + 1] = (int16_t)s;
    }
    int c = crete * 100 / 32768;
    s_crete = c > s_crete ? c : (s_crete * 7 + c) / 8;
}

}  // namespace aud
