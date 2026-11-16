// owrxliste.cpp - liste des recepteurs OpenWebRX VHF / UHF (voir owrxliste.h)
#include "owrxliste.h"
#include "serveurs.h"
#include "reglages.h"
#include "plateforme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace owl {

const char *const URL_VHF = "https://www.receiverbook.de/map?type=openwebrx&band=fr-vhf";
const char *const URL_UHF = "https://www.receiverbook.de/map?type=openwebrx&band=fr-uhf";
static const char *const FICHIER = "/kiwi/vhf.txt";

static RecepteurVHF *s_liste = nullptr, *s_neuf = nullptr;
static int s_n = 0, s_nneuf = 0;
static char s_etat[56] = "Liste VHF/UHF pas encore chargée";
static volatile int s_occupe = 0;

static void verrou(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe, 0, __ATOMIC_RELEASE);
}

static bool tableaux()
{
    if (!s_liste) s_liste = (RecepteurVHF *)plat_malloc_ps(sizeof(RecepteurVHF) * MAX_VHF);
    if (!s_neuf) s_neuf = (RecepteurVHF *)plat_malloc_ps(sizeof(RecepteurVHF) * MAX_VHF);
    return s_liste && s_neuf;
}

// ---------------------------------------------------------------- petits outils JSON (tolerants aux espaces)
static const char *cle(const char *j, const char *fin, const char *c)
{
    char motif[24];
    snprintf(motif, sizeof motif, "\"%s\"", c);
    size_t lm = strlen(motif);
    for (const char *p = j; p && p + lm < fin; ) {
        p = strstr(p, motif);
        if (!p || p >= fin) return nullptr;
        const char *q = p + lm;
        while (*q == ' ') q++;
        if (*q == ':') { q++; while (*q == ' ') q++; return q; }
        p += lm;
    }
    return nullptr;
}

static bool chaine(const char *p, char *dst, size_t n)
{
    dst[0] = 0;
    if (!p || *p != '"') return false;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        unsigned u = (uint8_t)*p++;
        if (u == '\\' && *p) {
            char e = *p++;
            if (e == 'u') {
                u = 0;
                for (int k = 0; k < 4 && *p; k++, p++) {
                    char h = *p;
                    u = u * 16 + (h >= '0' && h <= '9' ? h - '0' : ((h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0));
                }
                if (u >= 0xD800 && u <= 0xDFFF) continue;      // emoji : retire
            } else {
                u = (e == 'n' || e == 't' || e == 'r') ? ' ' : (uint8_t)e;
            }
            if (u < 0x20) u = ' ';
            if (u < 0x80) { if (o + 1 < n) dst[o++] = (char)u; }
            else if (u < 0x800) { if (o + 2 < n) { dst[o++] = (char)(0xC0 | (u >> 6)); dst[o++] = (char)(0x80 | (u & 63)); } }
            else if (o + 3 < n) { dst[o++] = (char)(0xE0 | (u >> 12)); dst[o++] = (char)(0x80 | ((u >> 6) & 63)); dst[o++] = (char)(0x80 | (u & 63)); }
        } else if (o + 1 < n) {
            dst[o++] = (char)u;
        }
    }
    while (o > 0 && ((uint8_t)dst[o - 1] & 0xC0) == 0xC0) o--;        // pas de caractere coupe
    dst[o] = 0;
    while (o > 0 && dst[o - 1] == ' ') dst[--o] = 0;
    return true;
}

static bool contient_sans_casse(const char *s, const char *motif)
{
    size_t lm = strlen(motif);
    for (; *s; s++)
        if (!strncasecmp(s, motif, lm)) return true;
    return false;
}

static void ajouter(const char *nom, const char *url, float lat, float lon, uint8_t bande)
{
    if (!url[0] || strncmp(url, "http", 4)) return;
    for (int i = 0; i < s_nneuf; i++)
        if (!strcmp(s_neuf[i].url, url)) { s_neuf[i].bandes |= bande; return; }
    if (s_nneuf >= MAX_VHF) return;
    RecepteurVHF &r = s_neuf[s_nneuf++];
    memset(&r, 0, sizeof r);
    snprintf(r.nom, sizeof r.nom, "%s", nom[0] ? nom : url);
    snprintf(r.url, sizeof r.url, "%s", url);
    size_t l = strlen(r.url);
    if (l && r.url[l - 1] != '/' && l + 1 < sizeof r.url) { r.url[l] = '/'; r.url[l + 1] = 0; }
    r.lat = lat;
    r.lon = lon;
    r.km = (lat == 0 && lon == 0) ? 65000 : srv::distance_km(cfg::R.lat, cfg::R.lon, lat, lon);
    r.bandes = bande;
}

// {"label":"...","location":{"type":"Point","coordinates":[lon,lat]},"url":"...","receivers":[{"label":..,"url":..,"type":"openwebrx"},...]}
int lire_objet(const char *obj, uint8_t bande)
{
    if (!s_neuf && !tableaux()) return 0;
    const char *fin = obj + strlen(obj);
    int avant = s_nneuf;
    char station[56], nom[56], url[96];
    chaine(cle(obj, fin, "label"), station, sizeof station);
    float lat = 0, lon = 0;
    const char *c = cle(obj, fin, "coordinates");
    if (c && *c == '[') {
        lon = (float)atof(c + 1);
        const char *v = strchr(c, ',');
        if (v) lat = (float)atof(v + 1);
    }
    const char *rs = cle(obj, fin, "receivers");
    if (rs && *rs == '[') {
        const char *p = rs + 1;
        while (true) {
            const char *o = strchr(p, '{');
            const char *ferme = strchr(p, ']');
            if (!o || (ferme && ferme < o)) break;
            const char *f = strchr(o, '}');
            if (!f) break;
            char type[24] = "";
            chaine(cle(o, f, "type"), type, sizeof type);
            chaine(cle(o, f, "label"), nom, sizeof nom);
            chaine(cle(o, f, "url"), url, sizeof url);
            if (!type[0] || contient_sans_casse(type, "openwebrx")) ajouter(nom[0] ? nom : station, url, lat, lon, bande);
            p = f + 1;
        }
    } else {
        chaine(cle(obj, fin, "url"), url, sizeof url);
        ajouter(station, url, lat, lon, bande);
    }
    return s_nneuf - avant;
}

// ---------------------------------------------------------------- lecture au fil du telechargement
static const char MARQUE[] = "var receivers";
static char *s_obj = nullptr;
static const size_t TAILLE_OBJ = 6144;
static size_t s_lobj = 0;
static int s_phase = 0;          // 0 cherche la marque, 1 cherche '[', 2 dans le tableau, 3 fini
static size_t s_vu = 0;          // caracteres de la marque deja reconnus
static int s_prof = 0;
static bool s_dans_chaine = false, s_echap = false, s_trop = false;
static uint8_t s_bande = 1;
static int s_pages_ok = 0;

void debut_liste()
{
    tableaux();
    if (!s_obj) s_obj = (char *)plat_malloc_ps(TAILLE_OBJ);
    s_nneuf = 0;
    s_pages_ok = 0;
    snprintf(s_etat, sizeof s_etat, "Téléchargement de la liste VHF/UHF…");
}

void debut_page(uint8_t bande)
{
    s_bande = bande;
    s_phase = 0;
    s_vu = 0;
    s_prof = 0;
    s_lobj = 0;
    s_dans_chaine = s_echap = s_trop = false;
}

void octets(const char *d, size_t n)
{
    if (!s_neuf || !s_obj) return;
    for (size_t i = 0; i < n; i++) {
        char c = d[i];
        switch (s_phase) {
        case 0:
            if (c == MARQUE[s_vu]) { if (++s_vu == sizeof MARQUE - 1) s_phase = 1; }
            else s_vu = (c == MARQUE[0]) ? 1 : 0;
            break;
        case 1:
            if (c == '[') { s_phase = 2; s_prof = 0; s_pages_ok++; }
            break;
        case 2:
            if (s_prof > 0) {
                if (s_lobj + 1 < TAILLE_OBJ) s_obj[s_lobj++] = c;
                else s_trop = true;
            }
            if (s_dans_chaine) {
                if (s_echap) s_echap = false;
                else if (c == '\\') s_echap = true;
                else if (c == '"') s_dans_chaine = false;
                break;
            }
            if (c == '"') { if (s_prof > 0) s_dans_chaine = true; break; }
            if (c == '{' || c == '[') {
                if (s_prof == 0 && c == '{') { s_lobj = 0; s_trop = false; s_obj[s_lobj++] = c; }
                if (s_prof > 0 || c == '{') s_prof++;
            } else if (c == '}' || c == ']') {
                if (s_prof == 0 && c == ']') { s_phase = 3; break; }   // fin du tableau
                s_prof--;
                if (s_prof == 0) {
                    s_obj[s_lobj] = 0;
                    if (!s_trop) lire_objet(s_obj, s_bande);
                    s_lobj = 0;
                }
            }
            break;
        default: break;
        }
    }
}

static int par_distance(const void *a, const void *b)
{
    const RecepteurVHF *x = (const RecepteurVHF *)a, *y = (const RecepteurVHF *)b;
    return (int)x->km - (int)y->km;
}

static void publier(const char *source)
{
    qsort(s_neuf, s_nneuf, sizeof(RecepteurVHF), par_distance);
    verrou(true);
    RecepteurVHF *t = s_liste;
    s_liste = s_neuf;
    s_neuf = t;
    s_n = s_nneuf;
    verrou(false);
    snprintf(s_etat, sizeof s_etat, "%d récepteurs%s", s_n, source);
}

static void sauver_sd()
{
    if (!plat_sd() || !s_n) return;
    size_t cap = (size_t)s_n * 180 + 64;
    char *t = (char *)plat_malloc_ps(cap);
    if (!t) return;
    size_t o = 0;
    verrou(true);
    for (int i = 0; i < s_n && o + 180 < cap; i++)
        o += snprintf(t + o, cap - o, "%u\t%.4f\t%.4f\t%s\t%s\n", s_liste[i].bandes, s_liste[i].lat, s_liste[i].lon, s_liste[i].url, s_liste[i].nom);
    verrou(false);
    plat_texte_ecrire(FICHIER, t, o);
    free(t);
}

void fin_liste(bool ok, const char *raison)
{
    if (ok && s_nneuf > 0) {
        publier("");
        sauver_sd();
        return;
    }
    if (s_n) snprintf(s_etat, sizeof s_etat, "%d récepteurs (liste gardée)", s_n);
    else snprintf(s_etat, sizeof s_etat, "Liste indisponible : %.36s", raison && raison[0] ? raison : (s_pages_ok ? "format inconnu" : "site injoignable"));
}

void charger_sd()
{
    if (!tableaux()) return;
    char *t = plat_texte_lire(FICHIER, 256 * 1024);
    if (!t) return;
    s_nneuf = 0;
    char *ctx = nullptr;
    for (char *l = strtok_r(t, "\n", &ctx); l; l = strtok_r(nullptr, "\n", &ctx)) {
        char *ch[5] = { nullptr };
        char *p = l;
        for (int k = 0; k < 5 && p; k++) { ch[k] = p; p = strchr(p, '\t'); if (p && k < 4) *p++ = 0; }
        if (!ch[4]) continue;
        ajouter(ch[4], ch[3], (float)atof(ch[1]), (float)atof(ch[2]), (uint8_t)atoi(ch[0]));
    }
    free(t);
    if (s_nneuf) publier(" (carte SD)");
}

int nb() { return s_n; }
const char *etat_liste() { return s_etat; }

void recalculer_distances()
{
    verrou(true);
    for (int i = 0; i < s_n; i++)
        s_liste[i].km = (s_liste[i].lat == 0 && s_liste[i].lon == 0) ? 65000 : srv::distance_km(cfg::R.lat, cfg::R.lon, s_liste[i].lat, s_liste[i].lon);
    if (s_n) qsort(s_liste, s_n, sizeof(RecepteurVHF), par_distance);
    verrou(false);
}

int chercher(const char *texte, const RecepteurVHF **sortie, int nmax)
{
    char plie[96], mots[6][24];
    srv::plier(texte ? texte : "", plie, sizeof plie);
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 6; p = strtok_r(nullptr, " ,;", &ctx))
        snprintf(mots[nm++], sizeof mots[0], "%s", p);
    int n = 0;
    verrou(true);
    for (int i = 0; i < s_n && n < nmax; i++) {
        bool ok = true;
        if (nm) {
            char brut[160], t[160];
            snprintf(brut, sizeof brut, "%s %s", s_liste[i].nom, s_liste[i].url);
            srv::plier(brut, t, sizeof t);
            for (int k = 0; k < nm && ok; k++) ok = strstr(t, mots[k]) != nullptr;
        }
        if (ok) sortie[n++] = &s_liste[i];
    }
    verrou(false);
    return n;
}

}  // namespace owl
