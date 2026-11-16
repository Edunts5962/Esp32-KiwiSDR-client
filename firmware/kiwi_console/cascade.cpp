// cascade.cpp - waterfall : reduction, contraste auto, palettes
#include "cascade.h"
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace wf {

static uint16_t *s_img = nullptr;             // H rangees de L pixels
static volatile int s_tete = 0;               // rangee la plus recente
static float s_spec[L];
static float s_plancher = -120;
static volatile uint32_t s_n = 0;
static int s_pal = 0;
static uint16_t s_couleurs[4][256];

static uint16_t interp(const uint8_t (*pts)[4], int np, int v)
{
    for (int i = 1; i < np; i++) {
        if (v <= pts[i][0]) {
            int a = pts[i - 1][0], b = pts[i][0];
            int t = b > a ? (v - a) * 255 / (b - a) : 0;
            uint8_t r = pts[i - 1][1] + ((pts[i][1] - pts[i - 1][1]) * t) / 255;
            uint8_t g = pts[i - 1][2] + ((pts[i][2] - pts[i - 1][2]) * t) / 255;
            uint8_t bb = pts[i - 1][3] + ((pts[i][3] - pts[i - 1][3]) * t) / 255;
            return gfx::rgb(r, g, bb);
        }
    }
    return gfx::rgb(pts[np - 1][1], pts[np - 1][2], pts[np - 1][3]);
}

static void faire_palettes()
{
    // niveau, r, g, b
    static const uint8_t KIWI[][4] = { {0, 6, 10, 32}, {60, 20, 40, 130}, {120, 14, 150, 230},
                                       {185, 250, 204, 21}, {255, 239, 68, 68} };
    static const uint8_t VERT[][4] = { {0, 2, 12, 6}, {90, 22, 101, 52}, {170, 74, 222, 128}, {255, 236, 252, 203} };
    static const uint8_t GRIS[][4] = { {0, 0, 0, 0}, {128, 107, 114, 128}, {255, 255, 255, 255} };
    static const uint8_t AMBRE[][4] = { {0, 20, 8, 0}, {90, 154, 52, 18}, {180, 245, 158, 11}, {255, 254, 243, 199} };
    for (int v = 0; v < 256; v++) {
        s_couleurs[0][v] = interp(KIWI, 5, v);
        s_couleurs[1][v] = interp(VERT, 4, v);
        s_couleurs[2][v] = interp(GRIS, 3, v);
        s_couleurs[3][v] = interp(AMBRE, 4, v);
    }
}

void init()
{
    faire_palettes();
    if (!s_img) s_img = (uint16_t *)malloc(L * H * sizeof(uint16_t));
    effacer();
}

void effacer()
{
    if (s_img)
        for (int i = 0; i < L * H; i++) s_img[i] = s_couleurs[s_pal][0];
    for (int i = 0; i < L; i++) s_spec[i] = -130;
}

void palette(int p)
{
    if (p < 0 || p > 3 || p == s_pal) return;
    s_pal = p;
    effacer();                                // les anciennes lignes gardent leurs couleurs sinon
}

uint16_t couleur(int p, int v) { return s_couleurs[p & 3][v & 255]; }
uint32_t compteur() { return s_n; }
float plancher_db() { return s_plancher; }
const float *spectre() { return s_spec; }

const uint16_t *rangee(int age)
{
    int r = s_tete - age;
    while (r < 0) r += H;
    return s_img + (r % H) * L;
}

void ligne(const uint8_t *bins, int n)
{
    if (!s_img || n < L) return;
    float db[L];
    for (int x = 0; x < L; x++) {
        int a = x * n / L, b = (x + 1) * n / L;
        int m = 0;
        for (int i = a; i < b; i++) if (bins[i] > m) m = bins[i];
        db[x] = (float)m - 255.0f - 13.0f;     // dBm (etalonnage habituel du Kiwi)
    }
    // plancher de bruit : 25e centile de la ligne, lisse dans le temps
    float tri[L];
    memcpy(tri, db, sizeof tri);
    std::nth_element(tri, tri + L / 4, tri + L);
    float p = tri[L / 4];
    s_plancher = s_n == 0 ? p : s_plancher + (p - s_plancher) * 0.1f;

    int t = (s_tete + 1) % H;
    uint16_t *row = s_img + t * L;
    float bas = s_plancher - 4, plage = 60;
    for (int x = 0; x < L; x++) {
        int v = (int)((db[x] - bas) * 255.0f / plage);
        row[x] = s_couleurs[s_pal][v < 0 ? 0 : (v > 255 ? 255 : v)];
        s_spec[x] = (s_n == 0 || s_spec[x] < -129) ? db[x] : s_spec[x] * 0.5f + db[x] * 0.5f;   // zone neuve (apres decalage) : valeur directe
    }
    s_tete = t;
    s_n = s_n + 1;
}

void decaler(int px)
{
    if (!s_img || px == 0) return;
    if (px >= L || px <= -L) { effacer(); return; }
    uint16_t fond = s_couleurs[s_pal][0];
    for (int r = 0; r < H; r++) {
        uint16_t *row = s_img + r * L;
        if (px > 0) {
            memmove(row + px, row, (L - px) * sizeof(uint16_t));
            for (int x = 0; x < px; x++) row[x] = fond;
        } else {
            int q = -px;
            memmove(row, row + q, (L - q) * sizeof(uint16_t));
            for (int x = L - q; x < L; x++) row[x] = fond;
        }
    }
    float sp[L];
    for (int x = 0; x < L; x++) {
        int s = x - px;
        sp[x] = (s >= 0 && s < L) ? s_spec[s] : -130;
    }
    memcpy(s_spec, sp, sizeof sp);
}

}  // namespace wf
