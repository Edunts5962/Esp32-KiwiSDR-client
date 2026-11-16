// fmliste.cpp - liste des recepteurs FM-DX
#include "fmliste.h"
#include "serveurs.h"
#include "reglages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace fml {

const char *const URL_LISTE = "https://servers.fmdx.org/api/";

static ServeurFM *s_liste = nullptr, *s_neuf = nullptr;
static int s_n = 0, s_nneuf = 0;
static char s_etat[160] = "Liste FM pas encore chargée";
static volatile int s_occupe = 0;

static void verrou(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe, 0, __ATOMIC_RELEASE);
}

// ---------------------------------------------------------------- analyse au fil de l'eau
static char *s_obj = nullptr;    // texte JSON d'un serveur (tas)
static const size_t TAILLE_OBJ = 4096;
static size_t s_lobj = 0;
static bool s_trop = false;
static int s_prof = 0;
static bool s_chaine = false, s_echap = false;

// valeur d'une cle chaine, decodee en UTF-8 (les emoji et caracteres hors BMP sont retires)
static void lire_chaine(const char *obj, const char *cle, char *dst, size_t n)
{
    dst[0] = 0;
    char motif[32];
    snprintf(motif, sizeof motif, "\"%s\":\"", cle);
    const char *p = strstr(obj, motif);
    if (!p) return;
    p += strlen(motif);
    size_t o = 0;
    while (*p && *p != '"' && o + 4 < n) {
        if (*p == '\\' && p[1]) {
            p++;
            char c = *p++;
            if (c == 'u') {
                unsigned u = 0;
                for (int k = 0; k < 4 && *p; k++, p++) {
                    char h = *p;
                    u = u * 16 + (h >= '0' && h <= '9' ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0);
                }
                if (u >= 0xD800 && u <= 0xDFFF) continue;              // emoji (paire de substitution) : retire
                if (u < 0x20) { dst[o++] = ' '; continue; }
                if (u < 0x80) dst[o++] = (char)u;
                else if (u < 0x800) { dst[o++] = (char)(0xC0 | (u >> 6)); dst[o++] = (char)(0x80 | (u & 63)); }
                else {
                    if (u >= 0x2000 && u != 0x20AC) continue;          // symboles divers : retires (sauf l'euro)
                    dst[o++] = (char)(0xE0 | (u >> 12)); dst[o++] = (char)(0x80 | ((u >> 6) & 63)); dst[o++] = (char)(0x80 | (u & 63));
                }
            } else {
                dst[o++] = c == 'n' || c == 't' || c == 'r' ? ' ' : c;  // \" \\ \/ ...
            }
        } else {
            dst[o++] = *p++;
        }
    }
    // pas de caractere UTF-8 coupe, pas d'espaces aux bords
    while (o > 0 && ((uint8_t)dst[o - 1] & 0xC0) == 0xC0) o--;
    dst[o] = 0;
    while (o > 0 && dst[o - 1] == ' ') dst[--o] = 0;
    size_t d = 0;
    while (dst[d] == ' ') d++;
    if (d) memmove(dst, dst + d, o - d + 1);
}

static long lire_nombre(const char *obj, const char *cle, long defaut)
{
    char motif[32];
    snprintf(motif, sizeof motif, "\"%s\":", cle);
    const char *p = strstr(obj, motif);
    if (!p) return defaut;
    p += strlen(motif);
    if (*p == '"') p++;
    return strtol(p, nullptr, 10);
}

static void terminer_objet()
{
    if (s_trop || !s_neuf || !s_obj || s_nneuf >= MAX_FM) return;
    s_obj[s_lobj] = 0;
    ServeurFM &s = s_neuf[s_nneuf];
    memset(&s, 0, sizeof s);
    lire_chaine(s_obj, "url", s.url, sizeof s.url);
    if (strncmp(s.url, "http", 4)) return;
    lire_chaine(s_obj, "name", s.nom, sizeof s.nom);
    if (!s.nom[0]) snprintf(s.nom, sizeof s.nom, "%s", s.url);
    lire_chaine(s_obj, "city", s.ville, sizeof s.ville);
    lire_chaine(s_obj, "countryName", s.pays, sizeof s.pays);
    lire_chaine(s_obj, "country", s.code, sizeof s.code);
    lire_chaine(s_obj, "bwLimit", s.plage, sizeof s.plage);
    char q[12];
    lire_chaine(s_obj, "audioQuality", q, sizeof q);
    s.kbps = (uint16_t)atoi(q);
    s.etat = (uint8_t)lire_nombre(s_obj, "status", 1);
    const char *c = strstr(s_obj, "\"coords\":[");
    s.lat = s.lon = 0;
    if (c) {
        c += 10;
        if (*c == '"') c++;
        s.lat = (float)atof(c);
        c = strchr(c, ',');
        if (c) { c++; if (*c == '"') c++; s.lon = (float)atof(c); }
    }
    s.km = (s.lat == 0 && s.lon == 0) ? 65000 : srv::distance_km(cfg::R.lat, cfg::R.lon, s.lat, s.lon);
    s_nneuf++;
}

void json_chaine(const char *json, const char *cle, char *dst, size_t n) { lire_chaine(json, cle, dst, n); }

double json_nombre(const char *json, const char *cle, double defaut)
{
    char motif[32];
    snprintf(motif, sizeof motif, "\"%s\":", cle);
    const char *p = strstr(json, motif);
    if (!p) return defaut;
    p += strlen(motif);
    if (*p == '"') p++;
    if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return defaut;
    return atof(p);
}

bool json_vrai(const char *json, const char *cle)
{
    char motif[32];
    snprintf(motif, sizeof motif, "\"%s\":true", cle);
    return strstr(json, motif) != nullptr;
}

void debut_liste()
{
    if (!s_neuf) s_neuf = (ServeurFM *)malloc(sizeof(ServeurFM) * MAX_FM);   // PSRAM
    if (!s_obj) s_obj = (char *)malloc(TAILLE_OBJ);
    s_nneuf = 0;
    s_lobj = 0;
    s_trop = false;
    s_prof = 0;
    s_chaine = s_echap = false;
    snprintf(s_etat, sizeof s_etat, "Téléchargement de la liste FM…");
}

void octets(const char *d, size_t n)
{
    if (!s_obj) return;
    for (size_t i = 0; i < n; i++) {
        char c = d[i];
        bool dans_objet = s_prof >= 3;
        if (s_chaine) {
            if (s_echap) s_echap = false;
            else if (c == '\\') s_echap = true;
            else if (c == '"') s_chaine = false;
        } else if (c == '"') {
            s_chaine = true;
        } else if (c == '{' || c == '[') {
            s_prof++;
            if (s_prof == 3) { s_lobj = 0; s_trop = false; dans_objet = true; }
        } else if (c == '}' || c == ']') {
            if (s_prof == 3 && dans_objet) {
                if (s_lobj < TAILLE_OBJ - 1) s_obj[s_lobj++] = c; else s_trop = true;
                s_prof--;
                terminer_objet();
                continue;
            }
            if (s_prof > 0) s_prof--;
        }
        if (dans_objet) {
            if (s_lobj < TAILLE_OBJ - 1) s_obj[s_lobj++] = c; else s_trop = true;
        }
    }
}

void fin_liste(bool ok)
{
    if (ok && s_nneuf > 0) {
        verrou(true);
        ServeurFM *t = s_liste;
        s_liste = s_neuf;
        s_n = s_nneuf;
        s_neuf = t;
        verrou(false);
        int dispo = 0;
        for (int i = 0; i < s_n; i++) dispo += s_liste[i].etat == 1;
        snprintf(s_etat, sizeof s_etat, "%d serveurs FM, %d disponibles", s_n, dispo);
    } else {
        snprintf(s_etat, sizeof s_etat, "%s", s_n ? "Échec de la mise à jour (ancienne liste gardée)" : "Liste FM indisponible");
    }
}

void echec(const char *raison)
{
    if (s_n) snprintf(s_etat, sizeof s_etat, "Mise à jour impossible (%s)", raison);
    else snprintf(s_etat, sizeof s_etat, "Liste FM indisponible (%s)", raison);
}

void noter_incomplete()
{
    size_t l = strlen(s_etat);
    snprintf(s_etat + l, sizeof s_etat - l, " (liste incomplète)");
}

int nb() { return s_n; }
const char *etat_liste() { return s_etat; }

void recalculer_distances()
{
    verrou(true);
    for (int i = 0; i < s_n; i++) {
        ServeurFM &s = s_liste[i];
        s.km = (s.lat == 0 && s.lon == 0) ? 65000 : srv::distance_km(cfg::R.lat, cfg::R.lon, s.lat, s.lon);
    }
    verrou(false);
}

int chercher(const char *texte, const ServeurFM **sortie, int nmax)
{
    char plie[96];
    srv::plier(texte ? texte : "", plie, sizeof plie);
    char mots[6][24];
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 6; p = strtok_r(nullptr, " ,;", &ctx))
        snprintf(mots[nm++], sizeof mots[0], "%s", p);
    int n = 0;
    verrou(true);
    for (int i = 0; i < s_n && n < nmax; i++) {
        const ServeurFM &s = s_liste[i];
        if (nm) {
            char brut[200], t[200];
            snprintf(brut, sizeof brut, "%s %s %s %s %s", s.nom, s.ville, s.pays, s.code, s.url);
            srv::plier(brut, t, sizeof t);
            bool ok = true;
            for (int k = 0; k < nm && ok; k++) ok = strstr(t, mots[k]) != nullptr;
            if (!ok) continue;
        }
        sortie[n++] = &s;
    }
    verrou(false);
    std::sort(sortie, sortie + n, [](const ServeurFM *a, const ServeurFM *b) {
        bool ia = a->etat == 0, ib = b->etat == 0;
        if (ia != ib) return ib;
        return a->km < b->km;
    });
    return n;
}

const ServeurFM *serveur_url(const char *url)
{
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_liste[i].url, url)) return &s_liste[i];
    return nullptr;
}

bool analyser_url(const char *url, char *hote, size_t nh, uint16_t *port, bool *tls, char *base, size_t nb)
{
    while (*url == ' ') url++;
    *tls = false;
    if (!strncmp(url, "https://", 8)) { *tls = true; url += 8; }
    else if (!strncmp(url, "http://", 7)) url += 7;
    else if (!strncmp(url, "wss://", 6)) { *tls = true; url += 6; }
    else if (!strncmp(url, "ws://", 5)) url += 5;
    const char *fin_hote = url;
    while (*fin_hote && *fin_hote != ':' && *fin_hote != '/') fin_hote++;
    size_t l = (size_t)(fin_hote - url);
    if (!l || l >= nh) return false;
    memcpy(hote, url, l);
    hote[l] = 0;
    *port = *tls ? 443 : 80;
    const char *p = fin_hote;
    if (*p == ':') { *port = (uint16_t)atoi(p + 1); while (*p && *p != '/') p++; }
    // chemin de base, toujours termine par "/"
    snprintf(base, nb, "%s", *p == '/' ? p : "/");
    size_t lb = strlen(base);
    char *q = strchr(base, '?');
    if (q) { *q = 0; lb = strlen(base); }
    if (lb == 0 || base[lb - 1] != '/') { if (lb + 1 < nb) { base[lb] = '/'; base[lb + 1] = 0; } }
    return *port != 0;
}

}  // namespace fml
