// nr.cpp - voir nr.h
#include "nr.h"
#include <math.h>
#include <string.h>

namespace nr {

static const int NF = 256, H = NF / 2, NB = NF / 2 + 1;
static volatile int s_niv = 0;
static volatile bool s_raz = true;
static volatile bool s_reapprendre = false;

static float s_fen[NF];                // racine de Hann : analyse et synthese (reconstruction exacte)
static float s_cos[NF / 2], s_sin[NF / 2];
static bool s_init = false;

static float s_prec[H], s_neuf[H];     // demi-trames d'entree
static float s_sortie[H], s_queue[H];  // sortie prete, recouvrement a ajouter
static int s_h = 0;
static float s_pl[NB];                 // puissance lissee
static float s_bruit[NB];              // bruit estime
static float s_g2[NB];                 // gain precedent au carre x SNR a posteriori precedent (decision dirigee)
static float s_gain[NB];
static int s_trames = 0;

void niveau(int n) { n = n < 0 ? 0 : (n > 2 ? 2 : n); if (n != s_niv) { s_niv = n; s_raz = true; } }
int niveau() { return s_niv; }
void raz() { s_reapprendre = true; }
void vider() { s_raz = true; }

static void preparer()
{
    for (int i = 0; i < NF; i++) s_fen[i] = sqrtf(0.5f - 0.5f * cosf(2 * (float)M_PI * i / NF));
    for (int i = 0; i < NF / 2; i++) { s_cos[i] = cosf(2 * (float)M_PI * i / NF); s_sin[i] = -sinf(2 * (float)M_PI * i / NF); }
    s_init = true;
}

static void fft(float *re, float *im, bool inverse)
{
    for (int i = 1, j = 0; i < NF; i++) {             // permutation
        int b = NF >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j |= b;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int l = 2; l <= NF; l <<= 1) {
        int pas = NF / l;
        for (int i = 0; i < NF; i += l)
            for (int k = 0; k < l / 2; k++) {
                float c = s_cos[k * pas], s = inverse ? -s_sin[k * pas] : s_sin[k * pas];
                float xr = re[i + k + l / 2] * c - im[i + k + l / 2] * s;
                float xi = re[i + k + l / 2] * s + im[i + k + l / 2] * c;
                re[i + k + l / 2] = re[i + k] - xr; im[i + k + l / 2] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}

// gains d'une trame (aussi utilise par le banc d'essai)
static void calculer_gains(const float *re, const float *im)
{
    const float plancher = s_niv >= 2 ? 0.10f : 0.22f;      // -20 dB / -13 dB : jamais de silence total (son naturel)
    const float surest = s_niv >= 2 ? 1.6f : 1.1f;          // le bruit compte un peu plus en "forte"
    for (int k = 0; k < NB; k++) {
        float p = re[k] * re[k] + im[k] * im[k] + 1e-3f;
        if (s_trames == 0) { s_pl[k] = p; s_bruit[k] = p; s_g2[k] = 1; }
        s_pl[k] = 0.8f * s_pl[k] + 0.2f * p;
        // bruit : suit vite les baisses, remonte lentement (~ +2 dB/s a 12 kHz) ; apprentissage rapide au debut
        if (s_trames < 30) s_bruit[k] = s_bruit[k] < s_pl[k] ? 0.7f * s_bruit[k] + 0.3f * s_pl[k] : s_pl[k];
        else if (s_pl[k] < s_bruit[k]) s_bruit[k] = 0.9f * s_bruit[k] + 0.1f * s_pl[k];
        else s_bruit[k] *= 1.005f;
        float n = s_bruit[k] * surest;
        float post = p / n;
        float prio = 0.97f * s_g2[k] + 0.03f * (post > 1 ? post - 1 : 0);
        float g = prio / (1 + prio);
        if (g < plancher) g = plancher;
        s_gain[k] = g;
        s_g2[k] = g * g * post;
    }
    // lissage leger entre bandes voisines : moins de "gazouillis"
    float a = s_gain[0];
    for (int k = 1; k < NB - 1; k++) { float b = s_gain[k]; s_gain[k] = 0.25f * a + 0.5f * b + 0.25f * s_gain[k + 1]; a = b; }
    s_trames++;
}

static void trame()
{
    float re[NF], im[NF];
    for (int i = 0; i < H; i++) { re[i] = s_prec[i] * s_fen[i]; re[i + H] = s_neuf[i] * s_fen[i + H]; }
    memset(im, 0, sizeof im);
    fft(re, im, false);
    calculer_gains(re, im);
    for (int k = 0; k < NB; k++) { re[k] *= s_gain[k]; im[k] *= s_gain[k]; }
    for (int k = 1; k < NF / 2; k++) { re[NF - k] = re[k]; im[NF - k] = -im[k]; }   // spectre d'un signal reel
    fft(re, im, true);
    for (int i = 0; i < H; i++) {
        s_sortie[i] = s_queue[i] + re[i] * s_fen[i] / NF;
        s_queue[i] = re[i + H] * s_fen[i + H] / NF;
    }
    memcpy(s_prec, s_neuf, sizeof s_prec);
}

void traiter(int16_t *x, int n)
{
    if (!s_init) preparer();
    if (s_raz) {
        s_raz = false;
        memset(s_prec, 0, sizeof s_prec); memset(s_neuf, 0, sizeof s_neuf);
        memset(s_sortie, 0, sizeof s_sortie); memset(s_queue, 0, sizeof s_queue);
        s_h = 0; s_trames = 0;
    }
    if (s_reapprendre) { s_reapprendre = false; s_trames = 0; }   // le son continue, seul le bruit est re-appris
    if (!s_niv) return;
    for (int i = 0; i < n; i++) {
        float y = s_sortie[s_h];
        s_neuf[s_h] = x[i];
        if (++s_h == H) { s_h = 0; trame(); }
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        x[i] = (int16_t)lrintf(y);
    }
}

}  // namespace nr
