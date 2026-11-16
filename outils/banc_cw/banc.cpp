// banc d'essai du decodeur CW : morse synthetique -> dec::traiter, comme dans la tache moteur
#include "decodeurs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <random>
#include <map>
#include <algorithm>
#include <stdlib.h>
static std::mt19937 rng(1234);
static const float FS = 12000;
static std::map<char, std::string> M = {
 {'A',".-"},{'B',"-..."},{'C',"-.-."},{'D',"-.."},{'E',"."},{'F',"..-."},{'G',"--."},{'H',"...."},{'I',".."},{'J',".---"},
 {'K',"-.-"},{'L',".-.."},{'M',"--"},{'N',"-."},{'O',"---"},{'P',".--."},{'Q',"--.-"},{'R',".-."},{'S',"..."},{'T',"-"},
 {'U',"..-"},{'V',"...-"},{'W',".--"},{'X',"-..-"},{'Y',"-.--"},{'Z',"--.."},{'0',"-----"},{'1',".----"},{'2',"..---"},
 {'3',"...--"},{'4',"....-"},{'5',"....."},{'6',"-...."},{'7',"--..."},{'8',"---.."},{'9',"----."},{'/',"-..-."},{'?',"..--.."},{'=',"-...-"}};
struct Param { float wpm = 20, ton = 500, snr = 30, jit = 0, qsb = 0, dash = 3.0f, decal = 0; };
// enveloppe de manipulation (0/1) a FS
static std::vector<float> manip(const std::string &t, const Param &p)
{
    std::vector<float> e;
    std::normal_distribution<float> g(0, 1);
    float u = 1.2f / p.wpm;                       // duree d'un point (s)
    auto ajoute = [&](float v, float d) { float j = 1 + p.jit * g(rng); if (j < 0.5f) j = 0.5f; int n = (int)(d * j * FS); for (int i = 0; i < n; i++) e.push_back(v); };
    ajoute(0, 0.8f);
    for (char c : t) {
        if (c == ' ') { ajoute(0, 4 * u); continue; }       // 3 + 4 = 7 points
        auto it = M.find(c); if (it == M.end()) continue;
        for (size_t k = 0; k < it->second.size(); k++) { ajoute(1, it->second[k] == '.' ? u : p.dash * u); ajoute(0, u); }
        ajoute(0, 2 * u);
    }
    ajoute(0, 1.0f);
    return e;
}
static std::vector<int16_t> signal(const std::string &t, const Param &p)
{
    std::vector<float> e = manip(t, p);
    // flancs adoucis (5 ms)
    int r = (int)(0.005f * FS); std::vector<float> l(e.size());
    float acc = 0; for (size_t i = 0; i < e.size(); i++) { acc += e[i]; if (i >= (size_t)r) acc -= e[i - r]; l[i] = acc / r; }
    std::normal_distribution<float> g(0, 1);
    // bruit blanc filtre 300-700 Hz (bande CW du KiwiSDR), niveau regle pour le rapport signal/bruit demande dans 400 Hz
    float A = 0.3f;                                   // crete du signal (pleine echelle = 1)
    float pS = A * A / 2;
    float pN = pS / powf(10, p.snr / 10);
    // passe-bande biquad (RBJ, f0 = 500, Q = 1.25 -> ~400 Hz)
    float w0 = 2 * M_PI * 500 / FS, al = sinf(w0) / (2 * 1.25f);
    float b0 = al, b2 = -al, a0 = 1 + al, a1 = -2 * cosf(w0), a2 = 1 - al;
    std::vector<float> n(e.size()); float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < e.size(); i++) { float x = g(rng); float y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = x; y2 = y1; y1 = y; n[i] = y; }
    double pn = 0; for (float v : n) pn += v * v; pn /= n.size();
    float k = p.snr > 90 ? 0 : sqrtf(pN / pn);
    std::vector<int16_t> s(e.size());
    float ph = 0;
    for (size_t i = 0; i < e.size(); i++) {
        float f = p.qsb > 0 ? powf(10, -p.qsb / 20 * (0.5f + 0.5f * sinf(2 * M_PI * 0.25f * i / FS))) : 1;
        ph += 2 * M_PI * (p.ton + p.decal) / FS; if (ph > 2 * M_PI) ph -= 2 * M_PI;
        float v = A * f * l[i] * sinf(ph) + k * n[i];
        int q = (int)(v * 32767); s[i] = q > 32767 ? 32767 : (q < -32768 ? -32768 : q);
    }
    return s;
}
static int lev(const std::string &a, const std::string &b)
{
    std::vector<int> d(b.size() + 1); for (size_t j = 0; j <= b.size(); j++) d[j] = j;
    for (size_t i = 1; i <= a.size(); i++) { int p = d[0]; d[0] = i; for (size_t j = 1; j <= b.size(); j++) { int t = d[j]; d[j] = std::min(std::min(d[j] + 1, d[j - 1] + 1), p + (int)(a[i - 1] != b[j - 1])); p = t; } }
    return d[b.size()];
}
static std::string net(const char *s) { std::string r; bool sp = true; for (const char *p = s; *p; p++) { char c = *p; if (c == ' ') { if (!sp) r += ' '; sp = true; } else { r += c; sp = false; } } while (!r.empty() && r.back() == ' ') r.pop_back(); return r; }
// mode : 0 = ouverture de l'ecran avec le signal deja la (calage immediat), 1 = bouton Caler apres 2 s, 2 = note choisie a la main
static float essai(const char *titre, const std::string &t, Param p, int mode, bool montrer)
{
    dec::init(); dec::choisir(dec::CW); dec::effacer(); dec::actif(true);
    std::string pre = "VVV VVV ";                 // de quoi caler
    auto s = signal(pre + t, p);
    size_t i = 0; bool cal = false;
    if (mode == 2) dec::regler_tonalite(p.ton + p.decal);
    if (mode == 4) dec::regler_tonalite(p.ton);
    while (i < s.size()) {
        int n = std::min<size_t>(256, s.size() - i);
        dec::traiter(&s[i], n, FS);
        i += n;
        if (mode == 1 && !cal && i > 2 * FS) { dec::caler(); cal = true; }
        if (mode == 1 && cal && i > 2 * FS && i < 2 * FS + 300) { }
    }
    std::string d = net(dec::texte());
    // on ne compte que la partie apres le preambule : on retire le debut decode jusqu'a la longueur equivalente
    std::string ref = t;
    // aligne : meilleur suffixe de d par rapport a ref
    int best = 1 << 30; size_t bk = 0;
    for (size_t k = 0; k <= d.size(); k++) { int v = lev(d.substr(k), ref); if (v < best) { best = v; bk = k; } }
    float cer = 100.0f * best / ref.size();
    if (montrer) printf("%-34s note %4.0f Hz  %-12s CER %5.1f %%  [%s]\n", titre, dec::tonalite_hz(), dec::info1(), cer, d.substr(bk).c_str());
    return cer;
}
extern int g_dbg;
int main(int argc, char **argv)
{
    if (getenv("DBG")) g_dbg = atoi(getenv("DBG"));
    if (getenv("SEED")) rng.seed(atoi(getenv("SEED")));
    std::string T = "CQ CQ DE F4ABC F4ABC PSE K TNX FER QSO UR RST 599 5NN QTH PARIS NAME JEAN HW? 73 SK";
    Param p;
    if (argc > 2) {                       // un seul essai, dans un processus neuf (spectre moyen vide, comme au demarrage de la carte)
        p.snr = atof(argv[2]); p.decal = argc > 3 ? atof(argv[3]) : 0; if (argc > 4) p.wpm = atof(argv[4]); if (argc > 5) p.jit = atof(argv[5]); if (argc > 6) p.qsb = atof(argv[6]); if (argc > 7) p.dash = atof(argv[7]);
        char b[64]; snprintf(b, 64, "1re ouverture, S/B %.0f dB, note %+.0f", p.snr, p.decal);
        essai(b, T, p, getenv("MAIN") ? 2 : 0, true);
        return 0;
    }
    printf("=== 1. Ouverture de l'ecran Decodeurs, signal fort (S/B 30 dB) a 500 Hz (note du KiwiSDR), 20 mots/min ===\n");
    essai("ouverture, calage immediat", T, p, 0, true);
    essai("bouton Caler apres 2 s", T, p, 1, true);
    essai("note reglee a la main (500 Hz)", T, p, 2, true);
    printf("\n=== 1b. Meme chose avec un signal moyen (S/B 12 dB) ===\n");
    p = Param(); p.snr = 12;
    essai("ouverture, calage immediat", T, p, 0, true);
    essai("bouton Caler apres 2 s", T, p, 1, true);
    p.snr = 8; essai("ouverture, S/B 8 dB", T, p, 0, true);
    if (argc > 1) return 0;
    printf("\n=== 2. Vitesses (S/B 30 dB, note bien reglee) ===\n");
    for (float w : { 8.f, 12.f, 18.f, 25.f, 30.f, 35.f, 40.f }) { p = Param(); p.wpm = w; char b[64]; snprintf(b, 64, "%.0f mots/min", w); essai(b, T, p, 2, true); }
    printf("\n=== 3. Bruit (20 mots/min, rapport signal/bruit dans 400 Hz) ===\n");
    for (float s : { 20.f, 12.f, 8.f, 5.f, 3.f, 0.f }) { p = Param(); p.snr = s; char b[64]; snprintf(b, 64, "S/B %+.0f dB", s); essai(b, T, p, 2, true); }
    printf("\n=== 4. Manipulation humaine (20 mots/min, S/B 15 dB) ===\n");
    for (float j : { 0.10f, 0.20f, 0.30f }) { p = Param(); p.snr = 15; p.jit = j; char b[64]; snprintf(b, 64, "irregularite %.0f %%", j * 100); essai(b, T, p, 2, true); }
    p = Param(); p.snr = 15; p.dash = 4.0f; essai("traits longs (rapport 4)", T, p, 2, true);
    printf("\n=== 5. Fading QSB 0,25 Hz (20 mots/min, S/B 15 dB) ===\n");
    for (float q : { 10.f, 20.f }) { p = Param(); p.snr = 15; p.qsb = q; char b[64]; snprintf(b, 64, "QSB %.0f dB", q); essai(b, T, p, 2, true); }
    printf("\n=== 6. Note decalee (signal a 600 / 650 Hz, decodeur a 500) ===\n");
    for (float d : { 50.f, 100.f, 150.f }) { p = Param(); p.snr = 15; p.decal = d; p.ton = 500; char b[64]; snprintf(b, 64, "decalage %+.0f Hz", d);
        essai(b, T, p, 4, true); }
    printf("\n=== 7. Bruit seul, sans signal (le decodeur invente-t-il du texte ?) ===\n");
    { dec::init(); dec::choisir(dec::CW); dec::effacer(); dec::actif(true); dec::regler_tonalite(500);
      Param q; q.snr = 0; auto s = signal("", q); // signal vide : que du bruit... mais amplitude du bruit regle sur A
      std::vector<int16_t> b(FS * 20); std::normal_distribution<float> g(0, 3000); for (auto &v : b) v = (int16_t)g(rng);
      for (size_t i = 0; i < b.size(); i += 256) dec::traiter(&b[i], 256, FS);
      printf("20 s de bruit -> [%s] (%zu caracteres), cale=%d\n", net(dec::texte()).c_str(), strlen(dec::texte()), dec::cale()); }
}
