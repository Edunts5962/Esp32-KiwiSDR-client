// stations.cpp - annuaire de stations de radio
#include "stations.h"
#include "stations_liste.h"
#include "serveurs.h"
#include "reglages.h"
#include "plateforme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sta {

const char *const FICHIER = "/kiwi/stations.csv";

static Station *s_liste = nullptr;
static int s_n = 0;
static bool s_sd = false;
static volatile int s_occupe = 0;

static void verrou(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe, 0, __ATOMIC_RELEASE);
}

static int mode_de(const char *m)
{
    for (int i = 0; i < cfg::NB_MODES; i++)
        if (!strcasecmp(m, cfg::nom_mode(i))) return i;
    return cfg::AM;
}

static void copier(char *dst, size_t n, const char *src)
{
    while (*src == ' ') src++;
    size_t l = strlen(src);
    while (l && (src[l - 1] == ' ' || src[l - 1] == '\r')) l--;
    if (l >= n) {
        l = n - 1;
        while (l > 0 && ((uint8_t)src[l] & 0xC0) == 0x80) l--;   // ne pas couper un caractere UTF-8
    }
    memcpy(dst, src, l);
    dst[l] = 0;
}

int analyser(const char *texte, Station *dst, int nmax)
{
    int n = 0;
    const char *p = texte;
    char ligne[256];
    while (*p && n < nmax) {
        const char *fin = strchr(p, '\n');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        if (l >= sizeof ligne) l = sizeof ligne - 1;
        memcpy(ligne, p, l);
        ligne[l] = 0;
        p = fin ? fin + 1 : p + strlen(p);
        if (!ligne[0] || ligne[0] == '#' || ligne[0] == '\r') continue;
        char *ch[8] = {};
        char *q = ligne;
        int k = 0;
        for (; k < 8 && q; k++) { ch[k] = q; q = strchr(q, ';'); if (q) *q++ = 0; }
        if (k < 3 || !ch[0][0]) continue;
        Station &s = dst[n];
        memset(&s, 0, sizeof s);
        copier(s.nom, sizeof s.nom, ch[0]);
        s.khz = (float)atof(ch[1]);
        if (s.khz <= 0 || s.khz > 32000) continue;
        char m[8];
        copier(m, sizeof m, ch[2]);
        s.mode = (uint8_t)mode_de(m);
        copier(s.langue, sizeof s.langue, ch[3] ? ch[3] : "");
        for (char *c = s.langue; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        copier(s.pays, sizeof s.pays, ch[4] ? ch[4] : "");
        bool pos = ch[5] && ch[6] && ch[5][0] && ch[6][0];
        s.lat = pos ? (float)atof(ch[5]) : 999;
        s.lon = pos ? (float)atof(ch[6]) : 999;
        copier(s.info, sizeof s.info, ch[7] ? ch[7] : "");
        n++;
    }
    return n;
}

int charger()
{
    if (!s_liste) {
        s_liste = (Station *)malloc(sizeof(Station) * MAX_STATIONS);   // PSRAM sur la carte
        if (!s_liste) return 0;
    }
    Station *tmp = (Station *)malloc(sizeof(Station) * MAX_STATIONS);
    if (!tmp) return s_n;
    int n = 0;
    bool sd = false;
    char *t = plat_texte_lire(FICHIER, 64 * 1024);
    if (t) {
        n = analyser(t, tmp, MAX_STATIONS);
        free(t);
        sd = n > 0;
    }
    if (!sd) {
        n = analyser(STATIONS_INTEGREES, tmp, MAX_STATIONS);
        if (plat_sd() && !t) plat_texte_ecrire(FICHIER, STATIONS_INTEGREES, strlen(STATIONS_INTEGREES));
    }
    verrou(true);
    memcpy(s_liste, tmp, sizeof(Station) * n);
    s_n = n;
    s_sd = sd;
    verrou(false);
    free(tmp);
    return n;
}

int reinitialiser()
{
    if (plat_sd()) plat_texte_ecrire(FICHIER, STATIONS_INTEGREES, strlen(STATIONS_INTEGREES));
    return charger();
}

int nb() { return s_n; }
const Station *station(int i) { return (i >= 0 && i < s_n) ? &s_liste[i] : nullptr; }
bool position_connue(const Station &s) { return s.lat >= -90 && s.lat <= 90 && s.lon >= -180 && s.lon <= 180; }
const char *source() { return s_sd ? "carte SD" : "liste intégrée"; }

static bool dans_filtre(const Station &s, const char *f)
{
    if (!f || !f[0]) return true;
    if (!strcmp(f, "autres"))
        return strcmp(s.langue, "en") && strcmp(s.langue, "fr") && strcmp(s.langue, "zh") && strcmp(s.langue, "--");
    return !strcmp(s.langue, f);
}

int chercher(const char *texte, const char *filtre, int *indices, int nmax)
{
    char plie[96];
    srv::plier(texte ? texte : "", plie, sizeof plie);
    char mots[6][24];
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 6; p = strtok_r(nullptr, " ,;", &ctx)) {
        snprintf(mots[nm], sizeof mots[0], "%s", p);
        nm++;
    }
    int n = 0;
    verrou(true);
    for (int i = 0; i < s_n && n < nmax; i++) {
        const Station &s = s_liste[i];
        if (!dans_filtre(s, filtre)) continue;
        if (nm) {
            char brut[200], t[200];
            snprintf(brut, sizeof brut, "%s %s %s %s %g", s.nom, s.pays, s.info, s.langue, s.khz);
            srv::plier(brut, t, sizeof t);
            bool ok = true;
            for (int k = 0; k < nm && ok; k++) ok = strstr(t, mots[k]) != nullptr;
            if (!ok) continue;
        }
        indices[n++] = i;
    }
    verrou(false);
    return n;
}

size_t ecrire_csv(char *dst, size_t n)
{
    size_t o = (size_t)snprintf(dst, n, "# Stations KiwiSDR - nom;kHz;mode;langue;pays;lat;lon;infos\n");
    verrou(true);
    for (int i = 0; i < s_n && o < n; i++) {
        const Station &s = s_liste[i];
        char lat[16] = "", lon[16] = "";
        if (position_connue(s)) { snprintf(lat, sizeof lat, "%.2f", s.lat); snprintf(lon, sizeof lon, "%.2f", s.lon); }
        o += (size_t)snprintf(dst + o, n - o, "%s;%g;%s;%s;%s;%s;%s;%s\n", s.nom, s.khz, cfg::nom_mode(s.mode),
                              s.langue, s.pays, lat, lon, s.info);
    }
    verrou(false);
    return o < n ? o : n - 1;
}

}  // namespace sta
