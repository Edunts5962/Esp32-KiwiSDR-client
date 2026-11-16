// banc de la reduction de bruit : parole synthetique + bruit blanc a 12 kHz.
// Les gains calcules sur le melange sont appliques aussi a la parole seule et au bruit seul : on mesure donc
// exactement ce que devient chacun (rapport signal/bruit de sortie, parole attenuee, bruit enleve).
#include "nr.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <random>
using namespace nr;
static void stft_applique(const float *x, float *y, int n, const float *gains_toutes, int nt)
{
    // meme decoupage que nr::trame
    float prec[H] = {0}, neuf[H], queue[H] = {0};
    int t = 0;
    for (int b = 0; b + H <= n; b += H) {
        memcpy(neuf, x + b, sizeof neuf);
        float re[NF], im[NF];
        for (int i = 0; i < H; i++) { re[i] = prec[i] * s_fen[i]; re[i + H] = neuf[i] * s_fen[i + H]; }
        memset(im, 0, sizeof im); fft(re, im, false);
        const float *g = gains_toutes + (size_t)(t < nt ? t : nt - 1) * NB;
        for (int k = 0; k < NB; k++) { re[k] *= g[k]; im[k] *= g[k]; }
        for (int k = 1; k < NF / 2; k++) { re[NF - k] = re[k]; im[NF - k] = -im[k]; }
        fft(re, im, true);
        for (int i = 0; i < H; i++) { y[b + i] = queue[i] + re[i] * s_fen[i] / NF; queue[i] = re[i + H] * s_fen[i + H] / NF; }
        memcpy(prec, neuf, sizeof prec); t++;
    }
}
int main(int argc, char **argv)
{
    int niv = atoi(argv[1]); float snr_db = atof(argv[2]); int graine = argc > 3 ? atoi(argv[3]) : 1;
    const int fs = 12000, n = fs * 30;
    std::mt19937 rng(graine); std::normal_distribution<float> gs(0, 1); std::uniform_real_distribution<float> u(0, 1);
    float *s = new float[n], *w = new float[n], *mx = new float[n];
    // parole : voyelles (harmoniques de f0, formants), syllabes de 150 a 350 ms, pauses
    double ph = 0; float f0 = 140, env = 0; int reste = 0; bool voix = true;
    float F1 = 600, F2 = 1500;
    for (int i = 0; i < n; i++) {
        if (--reste <= 0) { voix = u(rng) < 0.7f; reste = (int)(fs * (0.15f + 0.2f * u(rng))); f0 = 110 + 90 * u(rng); F1 = 400 + 500 * u(rng); F2 = 1000 + 1500 * u(rng); }
        env += ((voix ? 1.f : 0.f) - env) * 0.004f;
        ph += 2 * M_PI * f0 / fs;
        float v = 0;
        for (int h = 1; h * f0 < 3200; h++) {
            float f = h * f0, a = 1.0f / (1 + powf((f - F1) / 150, 2)) + 0.6f / (1 + powf((f - F2) / 200, 2)) + 0.05f;
            v += a * sinf((float)(h * ph));
        }
        s[i] = v * env;
        w[i] = gs(rng);
    }
    double ps = 0, pw = 0; for (int i = 0; i < n; i++) { ps += s[i] * s[i]; pw += w[i] * w[i]; }
    float k = sqrtf((float)(ps / pw) / powf(10, snr_db / 10));
    double nrm = 3000 / sqrt(ps / n);                 // niveau realiste en entiers 16 bits
    for (int i = 0; i < n; i++) { s[i] *= nrm; w[i] *= k * nrm; mx[i] = s[i] + w[i]; }
    // passe "vraie" : nr::traiter sur le melange en entiers, en recueillant les gains
    niveau(niv); raz();
    int nt = n / H; float *gt = new float[(size_t)nt * NB]; int t = 0;
    int16_t *x16 = new int16_t[n];
    for (int i = 0; i < n; i++) { float v = mx[i]; x16[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
    for (int b = 0; b + H <= n; b += H) { traiter(x16 + b, H); if (t < nt) memcpy(gt + (size_t)t * NB, s_gain, sizeof(float) * NB); t++; }
    float *so = new float[n](), *wo = new float[n]();
    stft_applique(s, so, n, gt, t); stft_applique(w, wo, n, gt, t);
    // mesures sur les 25 dernieres secondes (apres l'apprentissage), retard NF compense
    int d = 5 * fs; double a = 0, b2 = 0, c = 0, e = 0, ws = 0, wsi = 0;
    for (int i = d; i < n - NF; i++) { a += so[i + H] * so[i + H]; b2 += wo[i + H] * wo[i + H]; c += s[i] * s[i]; e += w[i] * w[i];
        double er = so[i + H] - s[i]; ws += er * er; wsi += s[i] * s[i]; }
    // controle : la sortie reelle = parole + bruit traites
    double err = 0, ref = 0; for (int i = d; i < n - NF; i++) { double r = so[i + H] + wo[i + H]; err += (x16[i + NF] - r) * (x16[i + NF] - r); ref += r * r; }
    printf("niveau %d entree %+5.1f dB : sortie %+5.1f dB (gain %+4.1f dB), parole %+5.1f dB, bruit %+5.1f dB, distorsion parole %5.1f dB, controle %.0f dB\n",
           niv, 10 * log10(c / e), 10 * log10(a / b2), 10 * log10(a / b2) - 10 * log10(c / e), 10 * log10(a / c), 10 * log10(b2 / e), 10 * log10(wsi / ws), 10 * log10(ref / err));
}
