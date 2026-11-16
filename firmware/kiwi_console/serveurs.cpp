// serveurs.cpp - lecture de la liste publique, distances, regions
#include "serveurs.h"
#include "reglages.h"
#include "plateforme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

namespace srv {

static Serveur *s_pub = nullptr;               // tableau en PSRAM
static int s_npub = 0;                          // liste affichee
static Serveur *s_neuf = nullptr;               // liste en cours de lecture
static int s_nneuf = 0;
static Serveur s_perso[MAX_PERSO];
static int s_nperso = 0;
static uint32_t s_t_liste = 0;
static bool s_jamais = true;
static char s_etat[200] = "Liste pas encore chargée";

// entree en cours de lecture
static Serveur s_cur;
static bool s_dans = false;
static bool s_hors_ligne = false;

const char *nom_region(int r)
{
    static const char *N[] = { "Perso", "Proches", "Europe", "Amér.", "Asie", "Océan.", "Afrique" };
    return (r >= 0 && r < NB_REGIONS) ? N[r] : "?";
}

static int region_de(float lat, float lon)
{
    if (lat == 0 && lon == 0) return AMERIQUES + 100;   // inconnue
    if (lon < -25) return AMERIQUES;
    if (lat > 34 && lon < 45) return EUROPE;
    if (lat <= 34 && lat > -38 && lon < 52) return AFRIQUE;
    if (lat < -10 && lon > 110) return OCEANIE;
    if (lon > 165 || (lat < 0 && lon > 140)) return OCEANIE;
    return ASIE;
}

uint16_t distance_km(float la1, float lo1, float la2, float lo2)
{
    const float R = 6371.0f, K = (float)M_PI / 180.0f;
    float dla = (la2 - la1) * K, dlo = (lo2 - lo1) * K;
    float a = sinf(dla / 2) * sinf(dla / 2) + cosf(la1 * K) * cosf(la2 * K) * sinf(dlo / 2) * sinf(dlo / 2);
    float d = 2 * R * atan2f(sqrtf(a), sqrtf(1 - a));
    return d > 65000 ? 65000 : (uint16_t)d;
}

static void completer(Serveur &s)
{
    s.km = (s.lat == 0 && s.lon == 0) ? 65000 : distance_km(cfg::R.lat, cfg::R.lon, s.lat, s.lon);
    int r = region_de(s.lat, s.lon);
    s.region = r > 50 ? 0xFF : r;
}

void recalculer_distances()
{
    for (int i = 0; i < s_npub; i++) completer(s_pub[i]);
    for (int i = 0; i < s_nperso; i++) completer(s_perso[i]);
}

bool analyser_adresse(const char *a, char *hote, size_t nh, uint16_t *port)
{
    while (*a == ' ') a++;
    const char *p = strstr(a, "://");
    p = p ? p + 3 : a;
    const char *fin = p;
    while (*fin && *fin != '/' && *fin != ':' && *fin != ' ' && *fin != '"') fin++;
    size_t l = fin - p;
    if (l == 0 || l >= nh) return false;
    memcpy(hote, p, l);
    hote[l] = 0;
    *port = 8073;
    if (*fin == ':') {
        int v = atoi(fin + 1);
        if (v > 0 && v < 65536) *port = (uint16_t)v;
    } else if (!strncmp(a, "https", 5)) {
        *port = 443;
    } else if (!strncmp(a, "http:", 5)) {
        *port = 80;
    }
    return true;
}

// ------------------------------------------------------------ perso (NVS)
struct Perso { char nom[48]; char hote[64]; char mdp[24]; uint16_t port; };

static void sauver_perso()
{
    static Perso t[MAX_PERSO];
    memset(t, 0, sizeof t);
    for (int i = 0; i < s_nperso; i++) {
        memcpy(t[i].nom, s_perso[i].nom, sizeof t[i].nom);
        memcpy(t[i].hote, s_perso[i].hote, sizeof t[i].hote);
        memcpy(t[i].mdp, s_perso[i].mdp, sizeof t[i].mdp);
        t[i].port = s_perso[i].port;
    }
    plat_nvs_ecrire("perso", t, sizeof t);
}

void init()
{
    static Perso t[MAX_PERSO];
    s_nperso = 0;
    if (plat_nvs_lire("perso", t, sizeof t)) {
        for (int i = 0; i < MAX_PERSO; i++) {
            if (!t[i].hote[0]) continue;
            Serveur &s = s_perso[s_nperso++];
            memset(&s, 0, sizeof s);
            memcpy(s.nom, t[i].nom, sizeof s.nom - 1);
            memcpy(s.hote, t[i].hote, sizeof s.hote - 1);
            memcpy(s.mdp, t[i].mdp, sizeof s.mdp - 1);
            s.port = t[i].port;
            s.perso = 1;
            snprintf(s.lieu, sizeof s.lieu, "Ajouté à la main");
            completer(s);
        }
    }
    if (!s_pub) s_pub = (Serveur *)malloc(sizeof(Serveur) * MAX_PUBLICS);
}

int nb_perso() { return s_nperso; }
const Serveur *perso(int i) { return (i >= 0 && i < s_nperso) ? &s_perso[i] : nullptr; }

bool ajouter_perso(const char *nom, const char *adresse, const char *mdp)
{
    if (s_nperso >= MAX_PERSO) return false;
    Serveur s;
    memset(&s, 0, sizeof s);
    if (!analyser_adresse(adresse, s.hote, sizeof s.hote, &s.port)) return false;
    snprintf(s.nom, sizeof s.nom, "%s", (nom && nom[0]) ? nom : s.hote);
    snprintf(s.mdp, sizeof s.mdp, "%s", mdp ? mdp : "");
    snprintf(s.lieu, sizeof s.lieu, "Ajouté à la main");
    s.perso = 1;
    completer(s);
    s_perso[s_nperso++] = s;
    sauver_perso();
    return true;
}

void retirer_perso(int i)
{
    if (i < 0 || i >= s_nperso) return;
    for (int k = i; k < s_nperso - 1; k++) s_perso[k] = s_perso[k + 1];
    s_nperso--;
    sauver_perso();
}

// ------------------------------------------------------------ liste publique
int nb_publics() { return s_npub; }
uint32_t age_liste_ms() { return s_jamais ? 0xFFFFFFFFu : plat_ms() - s_t_liste; }
const char *etat_liste() { return s_etat; }
void noter_etat(const char *t) { snprintf(s_etat, sizeof s_etat, "%s", t); }

void debut_liste()
{
    if (!s_neuf) s_neuf = (Serveur *)malloc(sizeof(Serveur) * MAX_PUBLICS);
    s_nneuf = 0;
    s_dans = false;
    snprintf(s_etat, sizeof s_etat, "Téléchargement de la liste…");
}

static void terminer_entree()
{
    if (!s_dans) return;
    s_dans = false;
    if (s_hors_ligne || !s_cur.hote[0] || !s_neuf || s_nneuf >= MAX_PUBLICS) return;
    if (!s_cur.nom[0]) snprintf(s_cur.nom, sizeof s_cur.nom, "%s", s_cur.hote);
    completer(s_cur);
    s_neuf[s_nneuf++] = s_cur;
}

static void commencer_entree()
{
    terminer_entree();
    memset(&s_cur, 0, sizeof s_cur);
    s_hors_ligne = false;
    s_dans = true;
}

// copie une valeur en enlevant les entites HTML les plus courantes et en
// gardant un UTF-8 valide meme tronque
static void copier(char *dst, size_t n, const char *v, size_t lv)
{
    size_t o = 0;
    for (size_t i = 0; i < lv && o + 1 < n; i++) {
        char c = v[i];
        if (c == '\\' && i + 1 < lv) { c = v[++i]; }
        else if (c == '&') {
            const char *e = v + i;
            if (!strncmp(e, "&amp;", 5)) { c = '&'; i += 4; }
            else if (!strncmp(e, "&#39;", 5)) { c = '\''; i += 4; }
            else if (!strncmp(e, "&quot;", 6)) { c = '"'; i += 5; }
            else if (!strncmp(e, "&lt;", 4)) { c = '<'; i += 3; }
            else if (!strncmp(e, "&gt;", 4)) { c = '>'; i += 3; }
        }
        dst[o++] = c;
    }
    while (o > 0 && ((uint8_t)dst[o - 1] & 0xC0) == 0x80) o--;     // fin de caractere coupe
    if (o > 0 && ((uint8_t)dst[o - 1] & 0xC0) == 0xC0) o--;
    dst[o] = 0;
}

static void champ(const char *k, size_t lk, const char *v, size_t lv)
{
    if (!s_dans) commencer_entree();
#define EST(x) (lk == sizeof(x) - 1 && !memcmp(k, x, lk))
    char tmp[128];
    if (EST("name")) copier(s_cur.nom, sizeof s_cur.nom, v, lv);
    else if (EST("loc")) copier(s_cur.lieu, sizeof s_cur.lieu, v, lv);
    else if (EST("users")) s_cur.users = (uint8_t)atoi(v);
    else if (EST("users_max")) s_cur.umax = (uint8_t)atoi(v);
    else if (EST("offline")) s_hors_ligne = (lv >= 3 && !memcmp(v, "yes", 3));
    else if (EST("gps")) {
        copier(tmp, sizeof tmp, v, lv);
        const char *p = tmp;
        while (*p && !(*p == '-' || (*p >= '0' && *p <= '9'))) p++;
        s_cur.lat = (float)atof(p);
        while (*p && (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9'))) p++;
        while (*p && !(*p == '-' || (*p >= '0' && *p <= '9'))) p++;
        s_cur.lon = (float)atof(p);
    } else if (EST("url")) {
        copier(tmp, sizeof tmp, v, lv);
        analyser_adresse(tmp, s_cur.hote, sizeof s_cur.hote, &s_cur.port);
    }
#undef EST
}

void ligne_liste(const char *l)
{
    // format HTML (kiwisdr.com/public)
    if (strstr(l, "<div class='cl-info'>")) { commencer_entree(); return; }
    const char *c = strstr(l, "<!-- ");
    if (c) {
        c += 5;
        const char *eg = strchr(c, '=');
        const char *fin = strstr(c, " -->");
        if (eg && fin && eg < fin) champ(c, eg - c, eg + 1, fin - eg - 1);
        return;
    }
    if (s_dans && !s_cur.hote[0]) {
        const char *a = strstr(l, ">http");
        if (a) {
            const char *f = strstr(a, "</a>");
            if (f) champ("url", 3, a + 1, f - a - 1);
            return;
        }
    }
    // format JSON / JS (rx.linkfanel.net) : "cle":"valeur" (plusieurs par ligne possibles)
    const char *p = l;
    while ((p = strchr(p, '"')) != nullptr) {
        const char *k = p + 1;
        const char *kf = strchr(k, '"');
        if (!kf) break;
        const char *q = kf + 1;
        while (*q == ' ') q++;
        if (*q != ':') { p = kf + 1; continue; }
        q++;
        while (*q == ' ') q++;
        if (*q != '"') { p = q; continue; }
        const char *v = q + 1, *vf = v;
        while (*vf && !(*vf == '"' && vf[-1] != '\\')) vf++;
        if (!*vf) break;
        // une nouvelle entree commence quand on revoit "name" alors que l'entree a deja un nom
        if (kf - k == 4 && !memcmp(k, "name", 4) && s_dans && s_cur.nom[0]) commencer_entree();
        champ(k, kf - k, v, vf - v);
        p = vf + 1;
    }
    if (strchr(l, '}')) terminer_entree();
}

void fin_liste(bool ok)
{
    terminer_entree();
    if (ok && s_nneuf > 0 && s_neuf && s_pub) {
        memcpy(s_pub, s_neuf, sizeof(Serveur) * s_nneuf);
        s_npub = s_nneuf;
        s_t_liste = plat_ms();
        s_jamais = false;
        snprintf(s_etat, sizeof s_etat, "%d récepteurs en ligne", s_npub);
    } else {
        snprintf(s_etat, sizeof s_etat, s_npub ? "Échec : ancienne liste gardée" : "Échec du téléchargement");
    }
}

// ------------------------------------------------------------ vue
static bool moins_bon(const Serveur *a, const Serveur *b)
{
    bool pa = a->umax && a->users >= a->umax, pb = b->umax && b->users >= b->umax;
    if (pa != pb) return pb;                  // les recepteurs pleins a la fin
    return a->km < b->km;
}

// minuscules sans accents (UTF-8 francais / europeen courant) -> ASCII
void plier(const char *src, char *dst, size_t n)
{
    static const char *const ACC[] = {
        "àáâãäå", "a", "çć", "c", "èéêë", "e", "ìíîï", "i", "ñń", "n", "òóôõöø", "o", "ùúûü", "u", "ýÿ", "y",
        "ÀÁÂÃÄÅ", "a", "ÇĆ", "c", "ÈÉÊË", "e", "ÌÍÎÏ", "i", "ÑŃ", "n", "ÒÓÔÕÖØ", "o", "ÙÚÛÜ", "u", "Ý", "y",
        "ßš", "s", "Šś", "s", "žźż", "z", "ŽŹŻ", "z", "łŁ", "l",
    };
    size_t o = 0;
    while (*src && o + 1 < n) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x80) {
            dst[o++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
            src++;
            continue;
        }
        int l = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
        char r = '?';
        for (size_t k = 0; k < sizeof(ACC) / sizeof(ACC[0]); k += 2) {
            const char *p = ACC[k];
            while (*p) {
                int lp = ((unsigned char)*p >= 0xE0) ? 3 : 2;
                if (lp == l && strncmp(p, src, l) == 0) { r = ACC[k + 1][0]; break; }
                p += lp;
            }
            if (r != '?') break;
        }
        dst[o++] = r;
        for (int k = 0; k < l && *src; k++) src++;
    }
    dst[o] = 0;
}

static bool correspond(const Serveur &s, char mots[][24], int nm)
{
    char t[sizeof(s.nom) + sizeof(s.lieu) + sizeof(s.hote) + 4];
    char brut[sizeof(t)];
    snprintf(brut, sizeof(brut), "%s %s %s", s.nom, s.lieu, s.hote);
    plier(brut, t, sizeof(t));
    for (int i = 0; i < nm; i++)
        if (!strstr(t, mots[i])) return false;
    return true;
}

int chercher(const char *texte, const Serveur **sortie, int nmax)
{
    char plie[96];
    plier(texte ? texte : "", plie, sizeof(plie));
    char mots[6][24];
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 6; p = strtok_r(nullptr, " ,;", &ctx)) {
        strncpy(mots[nm], p, sizeof(mots[0]) - 1);
        mots[nm][sizeof(mots[0]) - 1] = 0;
        nm++;
    }
    if (!nm) return 0;
    int n = 0;
    for (int i = 0; i < s_nperso && n < nmax; i++)
        if (correspond(s_perso[i], mots, nm)) sortie[n++] = &s_perso[i];
    int np = n;
    for (int i = 0; i < s_npub && n < nmax; i++)
        if (correspond(s_pub[i], mots, nm)) sortie[n++] = &s_pub[i];
    std::sort(sortie + np, sortie + n, moins_bon);   // les perso d'abord, puis par distance
    return n;
}

const Serveur *plus_proche_libre(float lat, float lon, uint16_t *km, const char *sauf_hote)
{
    const Serveur *m = nullptr;
    uint16_t best = 0xFFFF;
    for (int i = 0; i < s_npub; i++) {
        const Serveur &s = s_pub[i];
        if (s.umax && s.users >= s.umax) continue;        // plein
        if (sauf_hote && !strcmp(s.hote, sauf_hote)) continue;   // celui qui vient d'echouer
        if (s.lat == 0 && s.lon == 0) continue;           // position inconnue
        uint16_t d = distance_km(lat, lon, s.lat, s.lon);
        if (d < best) { best = d; m = &s; }
    }
    if (km) *km = best;
    return m;
}

int filtrer(int region, const Serveur **sortie, int nmax)
{
    int n = 0;
    if (region == PERSO) {
        for (int i = 0; i < s_nperso && n < nmax; i++) sortie[n++] = &s_perso[i];
        return n;
    }
    for (int i = 0; i < s_npub && n < nmax; i++) {
        const Serveur &s = s_pub[i];
        if (region == PROCHES || s.region == region) sortie[n++] = &s;
    }
    std::sort(sortie, sortie + n, [](const Serveur *a, const Serveur *b) {
        bool pa = a->umax && a->users >= a->umax, pb = b->umax && b->users >= b->umax;
        if (pa != pb) return pb;                  // les recepteurs pleins a la fin
        return a->km < b->km;
    });
    if (region == PROCHES && n > 40) n = 40;
    return n;
}

}  // namespace srv
