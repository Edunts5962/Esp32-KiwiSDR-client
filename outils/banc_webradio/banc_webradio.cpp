// banc webradio : flux MP3 et AAC (ADTS) avec metadonnees ICY, decoupes en morceaux de tailles au hasard
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "webradio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <random>
#include <map>
#include <string>
std::vector<float> g_son; float g_hz = 0;
namespace aud { void pousser(const int16_t *s, int n) { for (int i = 0; i < n; i++) g_son.push_back(s[i]); } void debit_entree(float h) { g_hz = h; } void vider() {} }
void *plat_malloc_ps(size_t n) { return calloc(1, n); }
void plat_free_ps(void *p) { free(p); }
void *plat_malloc_interne(size_t n) { return calloc(1, n); }
uint32_t plat_ms() { return 0; }
std::map<std::string, std::vector<uint8_t>> g_nvs;
bool plat_nvs_lire(const char *k, void *b, size_t n) { auto it = g_nvs.find(k); if (it == g_nvs.end() || it->second.size() < n) return false; memcpy(b, it->second.data(), n); return true; }
void plat_nvs_ecrire(const char *k, const void *b, size_t n) { g_nvs[k].assign((const uint8_t *)b, (const uint8_t *)b + n); }
size_t plat_nvs_taille(const char *k) { auto it = g_nvs.find(k); return it == g_nvs.end() ? 0 : it->second.size(); }
static double raie(const std::vector<float> &x, double f, double fs)   // puissance a f (Goertzel sur tout)
{ double w = 2 * M_PI * f / fs, c = 2 * cos(w), s1 = 0, s2 = 0; for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; } return (s1 * s1 + s2 * s2 - c * s1 * s2) / x.size() * 2 / x.size(); }
int main(int argc, char **argv)
{
    wr::init();
    const char *f = argv[1], *type = argv[2];
    FILE *h = fopen(f, "rb"); std::vector<uint8_t> d(4 << 20); size_t n = fread(d.data(), 1, d.size(), h); fclose(h); d.resize(n);
    // flux ICY : 8000 octets de son, 1 octet de longueur, les metadonnees
    const int METAINT = 8000;
    std::vector<uint8_t> flux; int k = 0, titres = 0;
    for (size_t i = 0; i < d.size(); i++) {
        flux.push_back(d[i]);
        if (++k == METAINT) {
            k = 0;
            char m[64]; int l = 0;
            if (titres++ % 3 == 0) l = snprintf(m, sizeof m, "StreamTitle='Caf\xe9 du matin - %d';", titres);     // Latin-1
            int bl = (l + 15) / 16; flux.push_back((uint8_t)bl);
            for (int j = 0; j < bl * 16; j++) flux.push_back(j < l ? (uint8_t)m[j] : 0);
        }
    }
    wr::Station s = {}; snprintf(s.nom, sizeof s.nom, "Test"); snprintf(s.codec, sizeof s.codec, "%s", strstr(type, "aac") ? "AAC" : "MP3"); s.kbps = 128;
    wr::jouer(s);
    char url[256]; uint32_t g; wr::a_ouvrir(url, sizeof url, &g);
    wr::flux_debut(type, METAINT, "");
    std::mt19937 rng(1); size_t pos = 0; uint32_t vt = wr::version_titre(); int changements = 0;
    while (pos < flux.size()) { size_t c = 1 + rng() % 3000; if (pos + c > flux.size()) c = flux.size() - pos; wr::flux_octets(flux.data() + pos, c); pos += c; if (wr::version_titre() != vt) { vt = wr::version_titre(); changements++; } }
    double fs = g_hz, tot = 0; for (float v : g_son) tot += (double)v * v / g_son.size();
    double p1 = raie(g_son, 440, fs), p2 = raie(g_son, 1000, fs);
    printf("%s : %.1f s decodees a %.0f Hz, etat %d, format \"%s\", titre \"%s\" (%d changements), raies %.1f %% de l'energie (pure : %.1f dB)\n",
           f, g_son.size() / fs, fs, (int)wr::etat(), wr::format(), wr::titre(), changements, 100 * (p1 + p2) / tot, 10 * log10((p1 + p2) / (tot - p1 - p2)));
}
