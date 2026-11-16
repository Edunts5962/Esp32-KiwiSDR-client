// geo.cpp - position de la carte (voir geo.h)
#include "geo.h"
#include "reglages.h"
#include "serveurs.h"
#include "fmliste.h"
#include "owrxliste.h"
#include "plateforme.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace geo {

struct Memo { uint8_t mode; char lieu[48]; };
static Memo s_m = { AUTO, "" };
static char s_etat[48] = "";
static volatile int s_demande = 0;          // 0 rien, 1 adresse Internet, 2 ville
static volatile int s_cours = 0;            // type de la demande partie
static char s_ville[48] = "";
static volatile uint32_t s_version = 0;
static int s_essais = 0;
static bool s_explicite = false;             // demandee par l'utilisateur (sinon : au demarrage, en mode AUTO)
static uint32_t s_t_essai = 0;

static void sauver() { plat_nvs_ecrire("position", &s_m, sizeof s_m); }

static void appliquer(float lat, float lon, int mode, const char *lieu)
{
    cfg::R.lat = lat;
    cfg::R.lon = lon;
    cfg::marquer();
    s_m.mode = (uint8_t)mode;
    snprintf(s_m.lieu, sizeof s_m.lieu, "%s", lieu ? lieu : "");
    sauver();
    srv::recalculer_distances();
    owl::recalculer_distances();
    fml::recalculer_distances();
    s_version = s_version + 1;
}

void init()
{
    Memo m;
    if (plat_nvs_lire("position", &m, sizeof m) && m.mode <= MANUEL) {
        m.lieu[sizeof m.lieu - 1] = 0;
        s_m = m;
    } else {
        // premiere fois : une position deja reglee a la main (autre que Paris par defaut) est gardee
        bool defaut = fabsf(cfg::R.lat - 48.85f) < 0.005f && fabsf(cfg::R.lon - 2.35f) < 0.005f;
        s_m.mode = defaut ? AUTO : MANUEL;
        s_m.lieu[0] = 0;
    }
    if (s_m.mode == AUTO) s_demande = 1;
}

int mode() { return s_m.mode; }
const char *lieu() { return s_m.lieu; }
const char *etat() { return s_etat; }
bool en_cours() { return s_cours != 0 || s_demande == 2 || (s_demande == 1 && s_essais == 0); }   // pas pendant l'attente d'un nouvel essai
uint32_t version() { return s_version; }

void etiquette(char *dst, size_t n)
{
    char pos[32];
    snprintf(pos, sizeof pos, "%.2f %c %.2f %c", fabsf(cfg::R.lat), cfg::R.lat >= 0 ? 'N' : 'S', fabsf(cfg::R.lon), cfg::R.lon >= 0 ? 'E' : 'O');
    for (char *q = pos; *q; q++) if (*q == '.') *q = ',';
    const char *comment = s_m.mode == AUTO ? "par Internet" : (s_m.mode == VILLE ? "choisie" : "réglée à la main");
    if (s_m.mode == AUTO && !s_m.lieu[0]) snprintf(dst, n, "%s (pas encore localisée)", pos);
    else snprintf(dst, n, "%s (%s)", s_m.lieu[0] ? s_m.lieu : pos, comment);
}

void localiser()
{
    s_essais = 0;
    s_explicite = true;
    s_demande = 1;
    snprintf(s_etat, sizeof s_etat, "Recherche de la position…");
}

void chercher_ville(const char *nom)
{
    while (*nom == ' ') nom++;
    if (strlen(nom) < 2) { snprintf(s_etat, sizeof s_etat, "Nom de ville trop court"); return; }
    snprintf(s_ville, sizeof s_ville, "%s", nom);
    s_demande = 2;
    snprintf(s_etat, sizeof s_etat, "Recherche de « %.24s »…", s_ville);
}

void manuel(float lat, float lon)
{
    s_demande = 0;
    s_etat[0] = 0;
    appliquer(lat, lon, MANUEL, "");
}

bool a_demander(char *url, size_t n)
{
    if (s_cours || !s_demande) return false;
    if (s_demande == 1 && s_essais > 0 && plat_ms() - s_t_essai < 60000) return false;   // nouvel essai : une minute plus tard
    if (s_demande == 1) {
        snprintf(url, n, "http://ip-api.com/json/?fields=status,message,country,city,lat,lon&lang=fr");
        s_essais++;
        s_t_essai = plat_ms();
    } else {
        size_t o = (size_t)snprintf(url, n, "https://geocoding-api.open-meteo.com/v1/search?count=1&language=fr&format=json&name=");
        for (const unsigned char *p = (const unsigned char *)s_ville; *p && o + 4 < n; p++) {
            bool simple = (*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'z') || *p == '-';
            if (simple) url[o++] = (char)*p;
            else o += (size_t)snprintf(url + o, n - o, "%%%02X", *p);
        }
        url[o] = 0;
    }
    s_cours = s_demande;
    s_demande = 0;
    return true;
}

static bool position_valide(double la, double lo) { return la >= -90 && la <= 90 && lo >= -180 && lo <= 180 && (la != 0 || lo != 0); }

// retire les espaces hors des chaines : les cles deviennent "cle":valeur
static void compacter(char *t)
{
    char *o = t;
    bool chaine = false;
    for (char *p = t; *p; p++) {
        if (chaine) { *o++ = *p; if (*p == '\\' && p[1]) *o++ = *++p; else if (*p == '"') chaine = false; continue; }
        if (*p == '"') chaine = true;
        if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') continue;
        *o++ = *p;
    }
    *o = 0;
}

void reponse(int code, const char *json)
{
    int type = s_cours;
    s_cours = 0;
    static char j[2048];
    snprintf(j, sizeof j, "%s", json ? json : "");
    compacter(j);
    if (type == 1) {
        if (s_m.mode != AUTO && !s_explicite) return;      // l'utilisateur a choisi sa position entre-temps
        s_explicite = false;
        char st[16] = "", ville[32] = "", pays[32] = "";
        if (code == 200) fml::json_chaine(j, "status", st, sizeof st);
        double la = fml::json_nombre(j, "lat", 999), lo = fml::json_nombre(j, "lon", 999);
        if (!strcmp(st, "success") && position_valide(la, lo)) {
            fml::json_chaine(j, "city", ville, sizeof ville);
            fml::json_chaine(j, "country", pays, sizeof pays);
            char l[48];
            snprintf(l, sizeof l, "%s%s%s", ville, ville[0] && pays[0] ? ", " : "", pays);
            appliquer((float)la, (float)lo, AUTO, l);
            s_etat[0] = 0;
            s_essais = 0;
        } else {
            snprintf(s_etat, sizeof s_etat, code == 200 ? "Position par Internet non trouvée" : "Service de position injoignable");
            if (s_essais < 3) s_demande = 1;               // nouvel essai dans une minute, 3 au plus
        }
        return;
    }
    if (type == 2) {
        const char *r = code == 200 ? strstr(j, "\"results\"") : nullptr;
        double la = r ? fml::json_nombre(r, "latitude", 999) : 999, lo = r ? fml::json_nombre(r, "longitude", 999) : 999;
        if (r && position_valide(la, lo)) {
            char nom[32] = "", pays[32] = "";
            fml::json_chaine(r, "name", nom, sizeof nom);
            fml::json_chaine(r, "country", pays, sizeof pays);
            char l[48];
            snprintf(l, sizeof l, "%s%s%s", nom[0] ? nom : s_ville, pays[0] ? ", " : "", pays);
            appliquer((float)la, (float)lo, VILLE, l);
            s_etat[0] = 0;
        } else {
            snprintf(s_etat, sizeof s_etat, code == 200 ? "Ville introuvable : « %.20s »" : "Recherche de ville impossible", s_ville);
        }
    }
}

}  // namespace geo
