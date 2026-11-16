// decodeurs.cpp - CW (filtre etroit qui suit la vitesse et la note, niveaux appris, anti-rebond), RTTY (2 filtres + UART),
// FAX (demodulation FM 1500-2300 Hz, 120 lignes/min, IOC 576)
#include "decodeurs.h"
#include "ft8.h"
#include "plateforme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace dec {

static int s_mode = CW;
static bool s_actif = false;
static bool s_inv = false;
static volatile bool d_caler = true, d_effacer = false;
static volatile bool s_cal_auto = false;          // calage automatique a l'ouverture : la note seulement, on garde ce qui est appris
static int s_nblocs = 0;                         // blocs de spectre mesures depuis l'activation
static const int BLOCS_CALAGE = 30;              // ~1,3 s : on ne cale pas sur un spectre vide

// ------------------------------------------------------------------ texte
static const int TXT = 1200;
static char s_txt[TXT + 1];
static int s_ntxt = 0;
static volatile uint32_t s_vtxt = 0;
static char s_i1[32] = "", s_i2[32] = "";
static bool s_cale = false;
static float s_ton = 500;                       // CW : la note du KiwiSDR

static void ajouter(char c)
{
    if (s_ntxt >= TXT) {
        memmove(s_txt, s_txt + 300, TXT - 300);
        s_ntxt -= 300;
    }
    s_txt[s_ntxt++] = c;
    s_txt[s_ntxt] = 0;
    s_vtxt = s_vtxt + 1;
}

const char *texte() { return s_txt; }
uint32_t version_texte() { return s_vtxt; }
const char *info1() { return s_i1; }
const char *info2() { return s_i2; }
bool cale() { return s_cale; }
float tonalite_hz() { return s_ton; }
bool inverse() { return s_inv; }

// ------------------------------------------------------------------ FFT (apercu, recherche)
static const int NFFT = 512;
static float s_re[NFFT], s_im[NFFT], s_fen[NFFT];
static float s_moy[NFFT / 2];                  // spectre moyen (recherche de tonalite)
static int16_t s_bloc[NFFT];
static int s_nbloc = 0;
static uint8_t *s_sp = nullptr;                // SP_H lignes de SP_N
static int s_sp_tete = 0;
static float s_fs = 12000;

static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int b = n >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j ^= b;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float a = -2 * (float)M_PI / len;
        float wr = cosf(a), wi = sinf(a);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k, v = i + k + len / 2;
                float tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                re[v] = re[u] - tr; im[v] = im[u] - ti;
                re[u] += tr; im[u] += ti;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

float hz_par_point() { return s_fs / NFFT; }

const uint8_t *spectre(int age)
{
    int r = s_sp_tete - age;
    while (r < 0) r += SP_H;
    return s_sp + (r % SP_H) * SP_N;
}

static void bloc_fft()
{
    for (int i = 0; i < NFFT; i++) { s_re[i] = s_bloc[i] * s_fen[i]; s_im[i] = 0; }
    fft(s_re, s_im, NFFT);
    int t = (s_sp_tete + 1) % SP_H;
    uint8_t *l = s_sp + t * SP_N;
    if (s_nblocs < 1000) s_nblocs++;
    for (int k = 0; k < NFFT / 2; k++) {
        float p = s_re[k] * s_re[k] + s_im[k] * s_im[k];
        float db = 10 * log10f(p + 1.0f);
        s_moy[k] = s_nblocs == 1 ? db : s_moy[k] * 0.9f + db * 0.1f;
        if (k < SP_N) {
            int v = (int)((db - 60) * 255 / 60);
            l[k] = v < 0 ? 0 : (v > 255 ? 255 : v);
        }
    }
    s_sp_tete = t;
}

static int pic(float f0, float f1)
{
    int a = (int)(f0 / hz_par_point()), b = (int)(f1 / hz_par_point());
    if (b >= NFFT / 2) b = NFFT / 2 - 1;
    int m = a;
    for (int k = a; k <= b; k++) if (s_moy[k] > s_moy[m]) m = k;
    return m;
}

// ------------------------------------------------------------------ CW
static const char *MORSE[][2] = {
    {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"}, {"..-.", "F"}, {"--.", "G"},
    {"....", "H"}, {"..", "I"}, {".---", "J"}, {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"},
    {"---", "O"}, {".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"}, {"..-", "U"},
    {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"}, {"--..", "Z"},
    {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"}, {"....-", "4"}, {".....", "5"},
    {"-....", "6"}, {"--...", "7"}, {"---..", "8"}, {"----.", "9"},
    {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {"-..-.", "/"}, {"-...-", "="}, {".-.-.", "+"},
    {"-....-", "-"}, {"---...", ":"}, {".----.", "'"}, {"-.--.", "("}, {"-.--.-", ")"}, {".--.-.", "@"},
    {"...-.-", "<SK>"}, {"-.-.-", "<KA>"}, {"........", "<HH>"} };

// Detecteur : melange a la note (I/Q) + deux passe-bas en cascade, dont la largeur suit la vitesse
// (filtre etroit = beaucoup moins de bruit) ; une mesure toutes les 2 ms ; niveaux du bruit et du signal
// appris chacun dans son etat ; anti-rebond ; la note suit le signal (AFC).
static struct {
    float oc = 1, os = 0, rc = 1, rs = 0;   // oscillateur local (vecteur tournant) et sa rotation par echantillon
    int norm = 0;
    float i1 = 0, q1 = 0, i2 = 0, q2 = 0, a = 0.05f;
    int dec = 24, pos = 0;
    float bruit = 0, sig = 0;
    float macc = 0; int mn = 0;            // niveau moyen du trait en cours
    float init_acc = 0; int init_n = 0;    // apprentissage du bruit au depart (200 ms)
    bool cle = false, cand = false;
    float duree = 0, cand_ms = 0;
    float point_ms = 60, biais = 0;        // duree d'un point ; allongement des traits par le seuil (et raccourcissement des blancs)
    float hist[16]; int nh = 0, ih = 0;    // durees des derniers traits
    float gaps[16]; int ng = 0, ig = 0;    // durees des derniers blancs (hors blancs entre mots)
    float coupe = 120;                     // limite point / trait
    char code[10];
    int ncode = 0;
    bool espace_mis = true;
    float t_afc = 0;
    float fs = 12000;
} cw;

static void cw_filtre()
{
    float tau = cw.point_ms / 10000.0f;                // constante de temps par pole : un dixieme de point
    if (tau < 0.003f) tau = 0.003f;
    if (tau > 0.012f) tau = 0.012f;
    cw.a = 1.0f - expf(-1.0f / (tau * cw.fs));
}

static void cw_note()                                   // l'oscillateur suit s_ton, sans rien oublier du reste
{
    cw.rc = cosf(2 * (float)M_PI * s_ton / cw.fs);
    cw.rs = sinf(2 * (float)M_PI * s_ton / cw.fs);
}

static void cw_reglage(float fs)                        // tout reprendre a zero (niveaux, vitesse)
{
    cw.fs = fs;
    cw_note();
    cw.dec = (int)(fs * 0.002f);
    cw.point_ms = 60; cw.biais = 0; cw.nh = 0; cw.ih = 0; cw.ng = 0; cw.ig = 0; cw.coupe = 120;
    cw.cle = cw.cand = false;
    cw.ncode = 0;
    cw.bruit = cw.sig = 0;
    cw.init_acc = 0; cw.init_n = 0;
    cw_filtre();
}

static void cw_lettre()
{
    if (!cw.ncode) return;
    cw.code[cw.ncode] = 0;
    const char *r = "*";
    for (auto &m : MORSE) if (!strcmp(m[0], cw.code)) { r = m[1]; break; }
    if (s_cale) for (const char *p = r; *p; p++) ajouter(*p);
    cw.ncode = 0;
    cw.espace_mis = !s_cale;
}

// Separe des durees en deux groupes (Otsu) dont le rapport des moyennes est compris entre rmin et rmax
static bool deux_groupes(const float *src, int n, float rmin, float rmax, float *A, float *B)
{
    if (n < 4) return false;
    float t[16];
    for (int i = 0; i < n; i++) t[i] = src[i];
    for (int i = 1; i < n; i++) for (int k = i; k > 0 && t[k] < t[k - 1]; k--) { float x = t[k]; t[k] = t[k - 1]; t[k - 1] = x; }
    float best = -1;
    for (int c = 1; c < n; c++) {
        float a = 0, b = 0;
        for (int i = 0; i < c; i++) a += t[i];
        for (int i = c; i < n; i++) b += t[i];
        a /= c; b /= (n - c);
        if (b < rmin * a || b > rmax * a) continue;
        float v = (float)c * (n - c) * (b - a) * (b - a);
        if (v > best) { best = v; *A = a; *B = b; }
    }
    return best >= 0;
}

// Vitesse. Un point mesure dure u + b, un blanc entre elements u - b (le seuil allonge les traits et raccourcit
// les blancs d'autant) : u = (point + blanc) / 2, quelle que soit la longueur des traits de l'operateur.
static void cw_vitesse()
{
    float S, L, G, G3;
    bool m = deux_groupes(cw.hist, cw.nh, 1.8f, 6.0f, &S, &L);
    bool g = deux_groupes(cw.gaps, cw.ng, 1.8f, 6.0f, &G, &G3);
    float u;
    if (m) cw.coupe += (sqrtf(S * L) - cw.coupe) * 0.3f;          // limite point / trait : entre les deux groupes
    if (!m && cw.nh >= 3 && cw.nh <= 6) {               // tout debut, un seul groupe : tout est bien plus long que prevu ?
        float mn = cw.hist[0];
        for (int i = 1; i < cw.nh; i++) if (cw.hist[i] < mn) mn = cw.hist[i];
        if (mn > 2.2f * cw.point_ms) {                  // alors ce sont des points d'un operateur lent
            cw.point_ms = mn;
            cw.coupe = 2 * mn;
            cw_filtre();
            return;
        }
    }
    if (m && g) u = (S + G) / 2;
    else if (m) u = (L - S) / 2;
    else return;                                        // un seul groupe (que des E ou des T) : on garde la vitesse
    if (u < 20) u = 20;
    if (u > 200) u = 200;
    cw.point_ms += (u - cw.point_ms) * 0.25f;
    float b = m && g ? (S - G) / 2 : S - u;
    cw.biais += (b - cw.biais) * 0.25f;
    if (cw.biais < -0.4f * cw.point_ms) cw.biais = -0.4f * cw.point_ms;
    if (cw.biais > 0.6f * cw.point_ms) cw.biais = 0.6f * cw.point_ms;
    if (!m) cw.coupe = 2 * cw.point_ms + cw.biais;
    cw_filtre();
}

static void cw_element(bool etait_cle, float ms)
{
    if (!etait_cle) {                                   // blanc : on garde ceux entre elements et entre lettres
        if (ms < 5 * cw.point_ms) { cw.gaps[cw.ig] = ms; cw.ig = (cw.ig + 1) % 16; if (cw.ng < 16) cw.ng++; }
        return;
    }
    if (ms < cw.coupe) { if (cw.ncode < 8) cw.code[cw.ncode++] = '.'; }
    else if (ms < 9 * cw.point_ms) { if (cw.ncode < 8) cw.code[cw.ncode++] = '-'; }
    if (cw.mn && ms > 0.5f * cw.point_ms) cw.sig += (cw.macc / cw.mn - cw.sig) * 0.3f;
    cw.hist[cw.ih] = ms;
    cw.ih = (cw.ih + 1) % 16;
    if (cw.nh < 16) cw.nh++;
    cw_vitesse();
}

static void cw_mesure(float e)
{
    const float pas = 2.0f;                             // ms
    if (cw.init_n < 100) {                              // 200 ms pour apprendre le niveau de depart
        cw.init_acc += e;
        if (++cw.init_n == 100) { cw.bruit = cw.init_acc / 100; cw.sig = cw.bruit * 2; }   // pas accroche tant qu'aucun vrai trait n'est vu
        return;
    }
    if (!cw.cle) cw.bruit += (e - cw.bruit) * (e < cw.bruit ? 0.05f : 0.01f);
    else if (cw.duree > 8 * cw.point_ms && cw.duree > 1000) {   // trait de plus d'1 s et de 8 points : pas du morse (porteuse), on reapprend
        cw.bruit = e; cw.sig = 4 * e; cw.cle = false; cw.duree = 0; cw.ncode = 0;
    }
    cw.sig -= (cw.sig - cw.bruit) * 0.0005f;            // sans trait, le seuil redescend (fading, fin d'emission)
    if (cw.sig < 2 * cw.bruit) cw.sig = 2 * cw.bruit;
    s_cale = cw.sig > 3.0f * cw.bruit;                  // un vrai signal : ses traits valent 3 fois le bruit
    float seuil = cw.bruit + (cw.sig - cw.bruit) * 0.5f;
    bool k = e > seuil;
    if (cw.cle) { cw.macc += e; cw.mn++; }
    float mini = cw.point_ms * 0.2f;                    // anti-rebond : plus court, c'est un parasite (borne : un blanc a 45 mots/min en dure ~25)
    if (mini < 6) mini = 6;
    if (mini > 14) mini = 14;
    if (k != cw.cle) {
        if (k != cw.cand) { cw.cand = k; cw.cand_ms = 0; }
        cw.cand_ms += pas;
        if (cw.cand_ms >= mini) {                       // l'etat qui se termine a dure jusqu'au debut du candidat
            cw_element(cw.cle, cw.duree);
            cw.cle = k;
            cw.duree = cw.cand_ms;
            cw.cand_ms = 0;
            cw.macc = 0; cw.mn = 0;
        }
    } else { cw.duree += cw.cand_ms + pas; cw.cand = k; cw.cand_ms = 0; }   // parasite trop court : rendu a l'etat en cours
    if (!cw.cle) {
        // blancs mesures : u - b entre elements, 3u - b entre lettres, 7u - b entre mots
        if (cw.ncode && cw.duree > 2.0f * cw.point_ms - cw.biais) cw_lettre();
        if (!cw.espace_mis && cw.duree > 5.0f * cw.point_ms - cw.biais) { ajouter(' '); cw.espace_mis = true; }
    }
}

static void cw_afc()
{
    // toutes les secondes : le pic du spectre moyen a +-150 Hz de la note, s'il depasse nettement le fond
    int c = (int)(s_ton / hz_par_point());
    int a = c - (int)(150 / hz_par_point()), b = c + (int)(150 / hz_par_point());
    if (a < 2) a = 2;
    if (b > NFFT / 2 - 2) b = NFFT / 2 - 2;
    int m = a;
    float fond = 0;
    for (int k = a; k <= b; k++) { if (s_moy[k] > s_moy[m]) m = k; fond += s_moy[k]; }
    fond /= (b - a + 1);
    if (s_moy[m] - fond < 6) return;
    // interpolation parabolique : la note a mieux que 23 Hz pres
    float y0 = s_moy[m - 1], y1 = s_moy[m], y2 = s_moy[m + 1], d = y0 - 2 * y1 + y2;
    float f = (m + (d != 0 ? 0.5f * (y0 - y2) / d : 0)) * hz_par_point();
    if (fabsf(f - s_ton) > 8) {
        s_ton += (f - s_ton) * 0.5f;
        cw_note();
    }
}

static void cw_traiter(const int16_t *s, int n, float fs)
{
    if (fs != cw.fs) cw_reglage(fs);
    for (int i = 0; i < n; i++) {
        float x = s[i] * (1.0f / 32768);
        float c = cw.oc, sn = cw.os;
        cw.oc = c * cw.rc - sn * cw.rs;
        cw.os = c * cw.rs + sn * cw.rc;
        if (++cw.norm >= 512) {                         // le vecteur garde une longueur de 1
            cw.norm = 0;
            float k = 1.0f / sqrtf(cw.oc * cw.oc + cw.os * cw.os);
            cw.oc *= k; cw.os *= k;
        }
        cw.i1 += (x * c - cw.i1) * cw.a; cw.q1 += (x * sn - cw.q1) * cw.a;
        cw.i2 += (cw.i1 - cw.i2) * cw.a; cw.q2 += (cw.q1 - cw.q2) * cw.a;
        if (++cw.pos >= cw.dec) { cw.pos = 0; cw_mesure(sqrtf(cw.i2 * cw.i2 + cw.q2 * cw.q2)); }
    }
    cw.t_afc += n / fs;
    if (cw.t_afc > 1.0f) { cw.t_afc = 0; cw_afc(); }
    snprintf(s_i1, sizeof s_i1, "%d mots/min", (int)(1200 / cw.point_ms + 0.5f));
    snprintf(s_i2, sizeof s_i2, "Note %d Hz", (int)s_ton);
}

// ------------------------------------------------------------------ RTTY
static const float BAUDS[] = { 45.45f, 50.0f, 75.0f };
static const int SHIFTS[] = { 170, 425, 450, 850 };
static int s_ib = 0, s_is = 0;
static const char LTRS[32] = { 0, 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
                               'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O', 'B', 'G', 0, 'M', 'X', 'V', 0 };
static const char FIGS[32] = { 0, '3', '\n', '-', ' ', '\'', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
                               '5', '"', ')', '2', '#', '6', '0', '1', '9', '?', '&', 0, '.', '/', ';', 0 };

static struct {
    float phm = 0, phs = 0, wm = 0, ws = 0;
    float mi = 0, mq = 0, si = 0, sq = 0, a = 0.1f;
    float d = 0, d_prec = 0;
    int etat = 0;               // 0 attente, 1 bit de start, 2 donnees, 3 stop
    float t = 0, bit = 264;     // echantillons
    int nb = 0, code = 0;
    bool chiffres = false;
    float qual = 0;
} rt;

void vitesse_suivante() { s_ib = (s_ib + 1) % 3; d_caler = true; }
void shift_suivant() { s_is = (s_is + 1) % 4; d_caler = true; }

static void rt_reglage(float fs)
{
    float sh = (float)SHIFTS[s_is];
    float mark = s_ton - sh / 2, space = s_ton + sh / 2;
    if (s_inv) { float t = mark; mark = space; space = t; }
    rt.wm = 2 * (float)M_PI * mark / fs;
    rt.ws = 2 * (float)M_PI * space / fs;
    rt.bit = fs / BAUDS[s_ib];
    rt.a = 1.0f - expf(-1.0f / (rt.bit / 3.0f));    // filtre ~1/3 de bit
    rt.etat = 0;
}

static void rt_car(int code)
{
    code &= 31;
    if (code == 31) { rt.chiffres = false; return; }
    if (code == 27) { rt.chiffres = true; return; }
    char c = rt.chiffres ? FIGS[code] : LTRS[code];
    if (c == ' ') rt.chiffres = false;              // "unshift on space"
    if (c == '\r' || !c) return;
    ajouter(c);
}

static void rt_traiter(const int16_t *s, int n, float fs)
{
    (void)fs;
    for (int i = 0; i < n; i++) {
        float x = s[i] * (1.0f / 32768);
        rt.mi += (x * cosf(rt.phm) - rt.mi) * rt.a;
        rt.mq += (x * sinf(rt.phm) - rt.mq) * rt.a;
        rt.si += (x * cosf(rt.phs) - rt.si) * rt.a;
        rt.sq += (x * sinf(rt.phs) - rt.sq) * rt.a;
        rt.phm += rt.wm; if (rt.phm > 2 * (float)M_PI) rt.phm -= 2 * (float)M_PI;
        rt.phs += rt.ws; if (rt.phs > 2 * (float)M_PI) rt.phs -= 2 * (float)M_PI;
        float em = rt.mi * rt.mi + rt.mq * rt.mq, es = rt.si * rt.si + rt.sq * rt.sq;
        rt.d = (em - es) / (em + es + 1e-9f);      // +1 mark, -1 space
        float ad = fabsf(rt.d);
        rt.qual += (ad - rt.qual) * 0.0005f;
        switch (rt.etat) {
        case 0:                                    // attente du front mark -> space
            if (rt.d_prec > 0 && rt.d <= 0) { rt.etat = 1; rt.t = rt.bit / 2; }
            break;
        case 1:
            if (--rt.t <= 0) {
                if (rt.d < 0) { rt.etat = 2; rt.t = rt.bit; rt.nb = 0; rt.code = 0; }
                else rt.etat = 0;                  // faux depart
            }
            break;
        case 2:
            if (--rt.t <= 0) {
                if (rt.d > 0) rt.code |= 1 << rt.nb;
                rt.nb++;
                rt.t = rt.bit;
                if (rt.nb == 5) rt.etat = 3;
            }
            break;
        case 3:
            if (--rt.t <= 0) {
                if (rt.d > 0 && rt.qual > 0.5f) rt_car(rt.code);   // stop correct, signal present
                rt.etat = 0;
            }
            break;
        }
        rt.d_prec = rt.d;
    }
    s_cale = rt.qual > 0.5f;
    snprintf(s_i1, sizeof s_i1, "%s bauds%s", s_ib == 0 ? "45,45" : (s_ib == 1 ? "50" : "75"), s_inv ? " · inv." : "");
    snprintf(s_i2, sizeof s_i2, "Shift %d Hz", SHIFTS[s_is]);
}

// ------------------------------------------------------------------ FAX
static uint8_t *s_fax = nullptr;
static int s_fax_tete = 0, s_fax_n = 0;
static volatile int s_fax_dec = 0;
static struct {
    float ph = 0, i1 = 0, q1 = 0, i2 = 0, q2 = 0, pi = 0, pq = 0;
    double pos = 0;             // position dans la ligne (echantillons)
    float acc = 0;
    int nacc = 0, px = 0;
    uint8_t ligne[FAX_L];
} fx;

const uint8_t *fax_ligne(int age)
{
    int r = s_fax_tete - age;
    while (r < 0) r += FAX_H;
    return s_fax + (r % FAX_H) * FAX_L;
}
int fax_nb_lignes() { return s_fax_n; }
void fax_decaler(int px) { s_fax_dec = ((s_fax_dec + px) % FAX_L + FAX_L) % FAX_L; }

static void fax_traiter(const int16_t *s, int n, float fs)
{
    const float w = 2 * (float)M_PI * 1900.0f / fs;
    const float a = 0.35f;                         // passe-bas ~800 Hz (2 poles)
    const double par_ligne = fs * 0.5;             // 120 lignes/min
    const double par_px = par_ligne / FAX_L;
    for (int i = 0; i < n; i++) {
        float x = s[i] * (1.0f / 32768);
        float ci = x * cosf(fx.ph), cq = -x * sinf(fx.ph);
        fx.ph += w; if (fx.ph > 2 * (float)M_PI) fx.ph -= 2 * (float)M_PI;
        fx.i1 += (ci - fx.i1) * a; fx.q1 += (cq - fx.q1) * a;
        fx.i2 += (fx.i1 - fx.i2) * a; fx.q2 += (fx.q1 - fx.q2) * a;
        // frequence instantanee : angle entre deux echantillons
        float re = fx.i2 * fx.pi + fx.q2 * fx.pq, im = fx.q2 * fx.pi - fx.i2 * fx.pq;
        fx.pi = fx.i2; fx.pq = fx.q2;
        float df = atan2f(im, re) * fs / (2 * (float)M_PI);    // -400 noir .. +400 blanc
        fx.acc += df;
        fx.nacc++;
        fx.pos += 1;
        if (fx.pos >= (fx.px + 1) * par_px) {
            float m = fx.acc / (fx.nacc ? fx.nacc : 1);
            int v = (int)((m + 400) * 255 / 800);
            if (fx.px < FAX_L) fx.ligne[fx.px] = v < 0 ? 0 : (v > 255 ? 255 : v);
            fx.acc = 0; fx.nacc = 0; fx.px++;
            if (fx.px >= FAX_L) {
                int t = (s_fax_tete + 1) % FAX_H;
                memcpy(s_fax + t * FAX_L, fx.ligne, FAX_L);
                s_fax_tete = t;
                if (s_fax_n < FAX_H) s_fax_n++;
                fx.px = 0;
                fx.pos -= par_ligne;
            }
        }
    }
    s_cale = true;
    snprintf(s_i1, sizeof s_i1, "120 lignes/min");
    snprintf(s_i2, sizeof s_i2, "Ligne %d", s_fax_n);
}

// ------------------------------------------------------------------ commun
void init()
{
    for (int i = 0; i < NFFT; i++) s_fen[i] = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / (NFFT - 1));
    if (!s_sp) s_sp = (uint8_t *)calloc(SP_N * SP_H, 1);
    if (!s_fax) s_fax = (uint8_t *)calloc(FAX_L * FAX_H, 1);
    s_txt[0] = 0;
}

void choisir(int m)
{
    if (m < 0 || m >= NB) return;
    if (m != s_mode) d_effacer = true;
    s_mode = m;
    d_caler = true;
}
int choisi() { return s_mode; }
void actif(bool a)
{
    if (a && !s_actif) {                            // on (re)commence : le spectre d'avant ne vaut plus (autre frequence)
        s_nblocs = 0;
        s_cal_auto = true;
        d_caler = true;
    }
    s_actif = a;
}
void effacer() { d_effacer = true; }
void caler() { s_cal_auto = false; d_caler = true; }
static volatile float d_ton = 0;
void regler_tonalite(float hz) { d_ton = hz; }
void inverser() { s_inv = !s_inv; d_caler = true; }

static void appliquer_calage(float fs)
{
    s_cale = false;
    if (s_mode == CW) {
        // le pic le plus fort, s'il depasse nettement le fond ; sinon la note du KiwiSDR (500 Hz)
        int m = pic(300, 1200), a = (int)(300 / hz_par_point()), b = (int)(1200 / hz_par_point());
        float fond = 0;
        for (int k = a; k <= b; k++) fond += s_moy[k];
        fond /= (b - a + 1);
        s_ton = s_moy[m] - fond >= 6 ? m * hz_par_point() : 500;
        if (s_ton < 300) s_ton = 500;
        if (s_cal_auto && cw.fs == fs) cw_note();       // ouverture : le detecteur tournait deja sur 500 Hz
        else cw_reglage(fs);                            // bouton Caler : on reapprend tout
    } else if (s_mode == RTTY) {
        // paire de pics separee du shift : on maximise la somme des deux
        float sh = (float)SHIFTS[s_is];
        int dk = (int)(sh / hz_par_point() + 0.5f);
        int a = (int)(400 / hz_par_point()), b = (int)(2800 / hz_par_point()) - dk;
        int m = a;
        float best = -1e9f;
        for (int k = a; k < b; k++) {
            float v = s_moy[k] + s_moy[k + dk];
            if (v > best) { best = v; m = k; }
        }
        s_ton = (m + dk / 2.0f) * hz_par_point();
        rt_reglage(fs);
    } else if (s_mode == FT8) {
        s_ton = 0;                                      // FT8 : toute la bande 200..3000 Hz est decodee
    } else {
        s_ton = 1900;
        fx.px = 0; fx.pos = 0; fx.acc = 0; fx.nacc = 0;
        s_fax_n = 0;
        memset(s_fax, 0, FAX_L * FAX_H);
    }
}

void traiter(const int16_t *s, int n, float fs)
{
    if (!s_sp || n <= 0 || !s_actif) return;        // en pause hors de l'ecran Decodeurs
    s_fs = fs;
    // apercu du spectre (toujours : utile pour caler)
    for (int i = 0; i < n; i++) {
        s_bloc[s_nbloc++] = s[i];
        if (s_nbloc == NFFT) { bloc_fft(); s_nbloc = 0; }
    }
    if (d_effacer) { d_effacer = false; s_ntxt = 0; s_txt[0] = 0; s_vtxt = s_vtxt + 1; }
    if (d_caler && s_nblocs >= BLOCS_CALAGE) { d_caler = false; appliquer_calage(fs); }
    else if (d_caler && s_mode == CW && cw.fs != fs) cw_reglage(fs);   // en attendant le calage : la note du KiwiSDR
    if (d_ton > 0) {
        s_ton = d_ton;
        d_ton = 0;
        if (s_mode == CW) cw_reglage(fs);
        else if (s_mode == RTTY) rt_reglage(fs);
    }
    if (s_mode == CW) cw_traiter(s, n, fs);
    else if (s_mode == RTTY) rt_traiter(s, n, fs);
    else if (s_mode == FAX) fax_traiter(s, n, fs);
    else if (s_mode == FT8) f8::traiter(s, n, fs);
}

// ------------------------------------------------------------------ sauvegarde
static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

bool sauver(char *nom, int n)
{
    if (!plat_sd()) return false;
    char h[24];
    plat_horodatage(h, sizeof h);
    if (s_mode == FT8) {                        // journal des messages, du plus ancien au plus recent
        snprintf(nom, n, "/kiwi/ft8_%s.txt", h);
        if (!plat_fichier_ouvrir(nom)) return false;
        bool ok = true;
        char l[128];
        for (int i = f8::nb() - 1; i >= 0 && ok; i--) {
            const f8::Message &m = f8::message(i);
            uint32_t s = m.periode % 86400;
            char km[16] = "";
            if (m.km) snprintf(km, sizeof km, " %u km", m.km);
            int k = snprintf(l, sizeof l, "%02u%02u%02u %3d %4.1f %4u ~  %-24s %s%s\r\n", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60),
                             m.snr, m.dt10 / 10.0, m.hz, m.texte, m.pays ? m.pays : "", km);
            ok = plat_fichier_ecrire(l, k);
        }
        plat_fichier_fermer();
        return ok;
    }
    if (s_mode != FAX) {
        snprintf(nom, n, "/kiwi/%s_%s.txt", s_mode == CW ? "cw" : "rtty", h);
        if (!plat_fichier_ouvrir(nom)) return false;
        bool ok = plat_fichier_ecrire(s_txt, s_ntxt);
        plat_fichier_fermer();
        return ok;
    }
    int lignes = s_fax_n;
    if (lignes < 1) return false;
    snprintf(nom, n, "/kiwi/fax_%s.bmp", h);
    if (!plat_fichier_ouvrir(nom)) return false;
    uint8_t e[54 + 1024];
    memset(e, 0, sizeof e);
    uint32_t taille_img = (uint32_t)FAX_L * lignes;
    e[0] = 'B'; e[1] = 'M';
    le32(e + 2, sizeof e + taille_img);
    le32(e + 10, sizeof e);
    le32(e + 14, 40);
    le32(e + 18, FAX_L);
    le32(e + 22, (uint32_t)lignes);            // de bas en haut : on ecrit la plus ancienne en dernier
    le16(e + 26, 1);
    le16(e + 28, 8);
    le32(e + 34, taille_img);
    le32(e + 46, 256);
    for (int i = 0; i < 256; i++) { e[54 + 4 * i] = e[55 + 4 * i] = e[56 + 4 * i] = i; }
    bool ok = plat_fichier_ecrire(e, sizeof e);
    uint8_t tmp[FAX_L];
    for (int age = 0; age < lignes && ok; age++) {
        const uint8_t *l = fax_ligne(age);
        for (int x = 0; x < FAX_L; x++) tmp[x] = l[(x + s_fax_dec) % FAX_L];
        ok = plat_fichier_ecrire(tmp, FAX_L);
    }
    plat_fichier_fermer();
    return ok;
}

}  // namespace dec
