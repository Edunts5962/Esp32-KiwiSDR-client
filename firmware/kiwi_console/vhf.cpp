// vhf.cpp - canaux VHF/UHF (voir vhf.h)
#include "vhf.h"
#include "aero_liste.h"
#include "reglages.h"
#include "serveurs.h"
#include "plateforme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

namespace vhf {

// En memoire : seulement les canaux communs et les aerodromes a moins de KM_AVIA de la reference.
// Le texte complet (integre, ou lu sur la carte SD) est garde et relu quand la reference change.
// Deux tableaux en alternance : les pointeurs donnes par chercher() restent lisibles apres une mise a jour.
static const int MAX_CANAUX = 1600;
static Canal *s_c = nullptr, *s_autre = nullptr;
static int s_n = 0;
static const char *s_texte = nullptr;
static float s_lat = 999, s_lon = 999;
static char s_source[32] = "";
static volatile int s_occupe = 0;
static volatile uint32_t s_version = 0;

static void verrou(bool pris)
{
    if (pris) { while (__atomic_exchange_n(&s_occupe, 1, __ATOMIC_ACQUIRE)) { } }
    else __atomic_store_n(&s_occupe, 0, __ATOMIC_RELEASE);
}

struct Commun { double khz; uint8_t cat, mode; const char *nom, *type; };
static const Commun COMMUNS[] = {
    { 121500, AVIA, cfg::AM, "Détresse aviation", "fréquence internationale" },
    { 123450, AVIA, cfg::AM, "Air-air", "entre pilotes" },
    { 156800, MARINE, cfg::FM, "Canal 16", "détresse et appel" },
    { 156300, MARINE, cfg::FM, "Canal 6", "bateau-bateau" },
    { 156400, MARINE, cfg::FM, "Canal 8", "bateau-bateau" },
    { 156450, MARINE, cfg::FM, "Canal 9", "ports de plaisance" },
    { 156500, MARINE, cfg::FM, "Canal 10", "ports" },
    { 156600, MARINE, cfg::FM, "Canal 12", "ports" },
    { 156650, MARINE, cfg::FM, "Canal 13", "passerelle à passerelle" },
    { 156700, MARINE, cfg::FM, "Canal 14", "ports" },
    { 156375, MARINE, cfg::FM, "Canal 67", "ports, sécurité" },
    { 156425, MARINE, cfg::FM, "Canal 68", "plaisance" },
    { 156475, MARINE, cfg::FM, "Canal 69", "ports" },
    { 156625, MARINE, cfg::FM, "Canal 72", "bateau-bateau" },
    { 156875, MARINE, cfg::FM, "Canal 77", "bateau-bateau" },
    { 145500, RADIOAM, cfg::FM, "Appel 2 m", "radioamateurs" },
    { 433500, RADIOAM, cfg::FM, "Appel 70 cm", "radioamateurs" },
    { 145800, RADIOAM, cfg::FM, "ISS", "station spatiale (passages)" },
};

static int ajouter_communs(Canal *d)
{
    int n = 0;
    for (const Commun &c : COMMUNS) {
        Canal &k = d[n++];
        memset(&k, 0, sizeof k);
        k.khz = c.khz; k.cat = c.cat; k.mode = c.mode; k.lat = k.lon = 999;
        snprintf(k.nom, sizeof k.nom, "%s", c.nom);
        snprintf(k.type, sizeof k.type, "%s", c.type);
    }
    for (int i = 0; i < 16; i++) {                       // PMR446 : 446,00625 MHz + 12,5 kHz par canal
        Canal &k = d[n++];
        memset(&k, 0, sizeof k);
        k.khz = 446006.25 + i * 12.5; k.cat = PMR; k.mode = cfg::FM; k.lat = k.lon = 999;
        snprintf(k.nom, sizeof k.nom, "PMR446 canal %d", i + 1);
        snprintf(k.type, sizeof k.type, "talkies-walkies");
    }
    return n;
}

static void copier_champ(char *dst, size_t n, const char *a, const char *b)
{
    size_t l = (size_t)(b - a);
    if (l >= n) { l = n - 1; while (l > 0 && ((uint8_t)a[l] & 0xC0) == 0x80) l--; }
    memcpy(dst, a, l);
    dst[l] = 0;
}

// aerodromes du texte a moins de KM_AVIA de (rlat, rlon), ajoutes a d[n...] ; renvoie le nouveau n
static int lire_proches(const char *t, Canal *d, int n, float rlat, float rlon)
{
    float lat = 999, lon = 999;
    uint16_t km = 65000;
    char code[8] = "", nom[40] = "";
    const char *p = t;
    while (*p) {
        const char *fin = strchr(p, '\n');
        if (!fin) fin = p + strlen(p);
        const char *ch[5] = { nullptr };
        const char *q = p;
        int k = 0;
        ch[k++] = q;
        while (k < 5 && (q = (const char *)memchr(q, '\t', fin - q)) != nullptr) ch[k++] = ++q;
        if (p[0] == 'A' && k >= 5) {
            lat = (float)atof(ch[1]);
            lon = (float)atof(ch[2]);
            // tri rapide avant le vrai calcul : 2 degres de latitude = 222 km
            km = fabsf(lat - rlat) > 2.0f ? 65000 : srv::distance_km(rlat, rlon, lat, lon);
            if (km <= KM_AVIA) {
                copier_champ(code, sizeof code, ch[3], ch[4] - 1);
                copier_champ(nom, sizeof nom, ch[4], fin);
            }
        } else if (p[0] == 'F' && k >= 3 && km <= KM_AVIA && n < MAX_CANAUX) {
            Canal &c = d[n++];
            memset(&c, 0, sizeof c);
            c.khz = atof(ch[1]);
            c.lat = lat; c.lon = lon; c.km = km; c.cat = AVIA; c.mode = cfg::AM;
            snprintf(c.nom, sizeof c.nom, "%s %s", code, nom);
            copier_champ(c.type, sizeof c.type, ch[2], fin);
        }
        p = *fin ? fin + 1 : fin;
    }
    return n;
}

static int ordre(const void *a, const void *b)
{
    const Canal *x = (const Canal *)a, *y = (const Canal *)b;
    bool cx = x->lat == 999, cy = y->lat == 999;
    if (cx != cy) return cx ? -1 : 1;                    // communs d'abord
    if (cx) { if (x->cat != y->cat) return (int)x->cat - (int)y->cat; return x->khz < y->khz ? -1 : (x->khz > y->khz); }
    if (x->km != y->km) return (int)x->km - (int)y->km;
    int n = strcmp(x->nom, y->nom);
    if (n) return n;
    return x->khz < y->khz ? -1 : (x->khz > y->khz);
}

void reference(float lat, float lon)
{
    if (!s_c) s_c = (Canal *)plat_malloc_ps(sizeof(Canal) * MAX_CANAUX);
    if (!s_autre) s_autre = (Canal *)plat_malloc_ps(sizeof(Canal) * MAX_CANAUX);
    if (!s_c || !s_autre) return;
    Canal *d = s_autre;
    int n = ajouter_communs(d);
    if (s_texte) n = lire_proches(s_texte, d, n, lat, lon);
    qsort(d, n, sizeof(Canal), ordre);
    verrou(true);
    s_autre = s_c;
    s_c = d;
    s_n = n;
    s_lat = lat;
    s_lon = lon;
    verrou(false);
    s_version = s_version + 1;
}

bool reference_changee(float lat, float lon)
{
    return fabsf(lat - s_lat) > 0.05f || fabsf(lon - s_lon) > 0.05f;
}

void init()
{
    // carte SD : liste du monde entier, gardee en texte (environ 1 Mo de PSRAM) ; sinon l'Europe integree
    char *t = plat_sd() ? plat_texte_lire("/kiwi/aero.txt", 3 * 1024 * 1024) : nullptr;
    if (t && t[0] == 'A') { s_texte = t; snprintf(s_source, sizeof s_source, "carte SD (monde)"); }
    else { if (t) free(t); s_texte = AERO_EUROPE; snprintf(s_source, sizeof s_source, "Europe (intégrée)"); }
    reference(cfg::R.lat, cfg::R.lon);
}

const char *source() { return s_source; }
int nb() { return s_n; }
uint32_t version() { return s_version; }

const char *nom_categorie(int cat)
{
    switch (cat) {
    case AVIA: return "Aviation";
    case MARINE: return "Marine";
    case RADIOAM: return "Radioamateurs";
    case PMR: return "PMR446";
    default: return "";
    }
}

int chercher(int cats, const char *texte, const Canal **sortie, int nmax)
{
    char plie[64], mots[4][24];
    srv::plier(texte ? texte : "", plie, sizeof plie);
    int nm = 0;
    char *ctx = nullptr;
    for (char *p = strtok_r(plie, " ,;", &ctx); p && nm < 4; p = strtok_r(nullptr, " ,;", &ctx))
        snprintf(mots[nm++], sizeof mots[0], "%s", p);
    int n = 0;
    verrou(true);
    for (int i = 0; i < s_n && n < nmax; i++) {
        const Canal &c = s_c[i];
        if (!(c.cat & cats)) continue;
        bool ok = true;
        if (nm) {
            char brut[96], t[96];
            snprintf(brut, sizeof brut, "%s %s %.3f %s", c.nom, c.type, c.khz / 1000.0, nom_categorie(c.cat));
            srv::plier(brut, t, sizeof t);
            for (int k = 0; k < nm && ok; k++) ok = strstr(t, mots[k]) != nullptr;
        }
        if (ok) sortie[n++] = &c;
    }
    verrou(false);
    return n;
}

}  // namespace vhf
