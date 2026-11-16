// quoi.cpp - "Quoi écouter" : estimation par l'heure, la saison et la position ; comptage des signaux
#include "quoi.h"
#include "reglages.h"
#include "vhf.h"
#include "stations.h"
#include "serveurs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace quoi {

static const float PI_F = 3.14159265f;

// ---- soleil ----
// jour de l'annee (0..365) et heure UTC (0..24) depuis l'heure Unix
static void jour_heure(uint32_t epoch, int *jour, float *h_utc)
{
    uint32_t j = epoch / 86400u;                    // jours depuis le 1er janvier 1970
    *h_utc = (epoch % 86400u) / 3600.0f;
    int annee = 1970;
    for (;;) {
        int n = (annee % 4 == 0 && (annee % 100 != 0 || annee % 400 == 0)) ? 366 : 365;
        if (j < (uint32_t)n) break;
        j -= n;
        annee++;
    }
    *jour = (int)j;
}

float heure_solaire(uint32_t epoch, float lon)
{
    int j; float h;
    jour_heure(epoch, &j, &h);
    float s = fmodf(h + lon / 15.0f + 48.0f, 24.0f);
    return s;
}

float soleil(uint32_t epoch, float lat, float lon)
{
    int j; float h;
    jour_heure(epoch, &j, &h);
    float decl = -23.44f * cosf(2 * PI_F * (j + 10) / 365.0f) * PI_F / 180;   // declinaison du soleil
    float angle = (heure_solaire(epoch, lon) - 12.0f) * 15.0f * PI_F / 180;   // angle horaire
    float la = lat * PI_F / 180;
    float s = sinf(la) * sinf(decl) + cosf(la) * cosf(decl) * cosf(angle);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return asinf(s) * 180 / PI_F;
}

// 1 = plein jour, 0 = nuit noire, entre les deux au crepuscule (soleil de -8 a +4 degres)
static float lumiere(uint32_t epoch, float lat, float lon)
{
    float e = soleil(epoch, lat, lon);
    float d = (e + 8.0f) / 12.0f;
    return d < 0 ? 0 : (d > 1 ? 1 : d);
}

// +1 en plein ete, -1 en plein hiver (selon l'hemisphere du recepteur)
static float saison(uint32_t epoch, float lat)
{
    int j; float h;
    jour_heure(epoch, &j, &h);
    float s = cosf(2 * PI_F * (j - 172) / 365.0f);
    return lat < 0 ? -s : s;
}

const char *moment(const Contexte &c)
{
    if (!c.epoch) return "";
    float h = heure_solaire(c.epoch, c.lon);
    if (h >= 5 && h < 10) return "Matin";
    if (h >= 10 && h < 17) return "Jour";
    if (h >= 17 && h < 22) return "Soir";
    return "Nuit";
}

// ---- propagation du jour ----
const char *const URL_FLUX = "https://services.swpc.noaa.gov/products/summary/10cm-flux.json";
const char *const URL_KP = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
static volatile float s_flux = -1, s_kp = -1;
static uint32_t s_sol_t = 0;
static bool s_sol_demande = false, s_sol_ok = false;

// nombre qui suit la cle (entre guillemets ou non) ; cherche la DERNIERE occurrence de la cle, sans tenir compte des majuscules
static bool dernier_nombre(const char *j, const char *cle, float *v)
{
    size_t lc = strlen(cle);
    const char *trouve = nullptr;
    for (const char *p = j; *p; p++) {
        if (*p != '"') continue;
        size_t k = 0;
        while (k < lc && p[1 + k] && (p[1 + k] | 32) == (cle[k] | 32)) k++;
        if (k == lc && p[1 + lc] == '"') trouve = p + lc + 2;
    }
    if (!trouve) return false;
    while (*trouve == ' ' || *trouve == ':' || *trouve == '"') trouve++;
    char *fin;
    double d = strtod(trouve, &fin);
    if (fin == trouve) return false;
    *v = (float)d;
    return true;
}

bool lire_flux(const char *j, float *flux)
{
    float v;
    if (!j || !dernier_nombre(j, "flux", &v) || v < 50 || v > 500) return false;
    *flux = v;
    return true;
}

bool lire_kp(const char *j, float *kp)
{
    float v;
    if (!j) return false;
    if (dernier_nombre(j, "kp", &v) && v >= 0 && v <= 9) { *kp = v; return true; }
    // ancien format : tableau de tableaux, ["date","Kp",...] ; derniere ligne, 2e valeur
    const char *l = strrchr(j, '[');
    if (!l) return false;
    const char *p = strchr(l, ',');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '"') p++;
    char *fin;
    double d = strtod(p, &fin);
    if (fin == p || d < 0 || d > 9) return false;
    *kp = (float)d;
    return true;
}

bool solaire_a_demander(uint32_t ms)
{
    if (s_sol_demande) return false;
    uint32_t attente = s_sol_ok ? 3u * 3600u * 1000u : 10u * 60u * 1000u;   // 3 h ; nouvel essai apres 10 min si echec
    if (s_sol_t && ms - s_sol_t < attente) return false;
    if (!s_sol_t && ms < 20000) return false;            // pas dans les premieres secondes du demarrage
    s_sol_demande = true;
    return true;
}

void solaire_reponse(const char *jf, const char *jk, uint32_t ms)
{
    float f, k;
    bool a = lire_flux(jf, &f), b = lire_kp(jk, &k);
    if (a) s_flux = f;
    if (b) s_kp = k;
    s_sol_ok = a || b;
    s_sol_t = ms ? ms : 1;
    s_sol_demande = false;
}

bool solaire(float *flux, float *kp)
{
    if (flux) *flux = s_flux;
    if (kp) *kp = s_kp;
    return s_flux >= 0 || s_kp >= 0;
}

void solaire_fixer(float flux, float kp) { s_flux = flux; s_kp = kp; }

// ---- ecriture ----
static const char *nom_etat(int niveau)
{
    static const char *const N[6] = { "fermée", "calme", "par moments", "moyen", "actif", "très actif" };
    return N[niveau < 0 ? 0 : (niveau > 5 ? 5 : niveau)];
}

static void milliers(char *s, size_t n, long v)
{
    if (v >= 1000) snprintf(s, n, "%ld %03ld", v / 1000, v % 1000);
    else snprintf(s, n, "%ld", v);
}

// "7 000 – 7 200 kHz" ou "144 – 146 MHz" ; une frequence : "5 505 kHz" ou "145,500 MHz"
static void ecrire_plage(char *s, size_t n, double a, double b)
{
    if (a >= 30000) {
        char x[20], y[20];
        auto mhz = [](char *d, size_t nd, double khz, bool precis) {
            double m = khz / 1000.0;
            if (precis) snprintf(d, nd, "%.3f", m);
            else if (fabs(m - floor(m + 0.5)) < 0.0005) snprintf(d, nd, "%.0f", m);
            else snprintf(d, nd, "%.1f", m);
            for (char *q = d; *q; q++) if (*q == '.') *q = ',';
        };
        if (a == b) { mhz(x, sizeof x, a, true); snprintf(s, n, "%s MHz", x); }
        else { mhz(x, sizeof x, a, false); mhz(y, sizeof y, b, false); snprintf(s, n, "%s – %s MHz", x, y); }
        return;
    }
    char x[20], y[20];
    milliers(x, sizeof x, lround(a));
    if (a == b) { snprintf(s, n, "%s kHz", x); return; }
    milliers(y, sizeof y, lround(b));
    snprintf(s, n, "%s – %s kHz", x, y);
}

static Sugg *ajouter(Sugg *dst, int *n, int nmax, const char *nom, double a, double b, double f, int mode,
                     int genre, int cat, int niveau, bool continu, float larg, const char *note)
{
    if (*n >= nmax) return nullptr;
    Sugg &s = dst[(*n)++];
    memset(&s, 0, sizeof s);
    snprintf(s.nom, sizeof s.nom, "%s", nom);
    char pl[40];
    ecrire_plage(pl, sizeof pl, a, b);
    if (note && note[0]) snprintf(s.detail, sizeof s.detail, "%s · %s · %s", pl, cfg::nom_mode(mode), note);
    else snprintf(s.detail, sizeof s.detail, "%s · %s", pl, cfg::nom_mode(mode));
    s.a = a; s.b = b; s.f = f; s.mode = (uint8_t)mode; s.genre = (uint8_t)genre; s.cat = (uint8_t)cat;
    s.niveau = (uint8_t)(niveau < 0 ? 0 : (niveau > 5 ? 5 : niveau));
    s.continu = continu;
    s.larg = larg;
    snprintf(s.etat, sizeof s.etat, "%s", continu ? "continu" : nom_etat(s.niveau));
    return &s;
}

// ---- ondes courtes ----
// j / n : activite habituelle de jour et de nuit (0..5) ; les notes disent a quoi s'attendre
struct BandeOC { const char *nom; float a, b, f; uint8_t mode, genre, j, n; const char *note_j, *note_n; };
static const BandeOC OC_BANDES[] = {
    { "Grandes ondes · radiodiffusion", 148.5f, 283.5f, 225, cfg::AM, RADIODIF, 2, 3, "quelques stations", "quelques stations" },
    { "Petites ondes · radiodiffusion", 531, 1602, 999, cfg::AM, RADIODIF, 2, 5, "stations proches", "stations lointaines" },
    { "160 m · radioamateurs", 1810, 2000, 1850, cfg::LSB, RADIOAM, 0, 3, "fermée le jour", "régional" },
    { "80 m · radioamateurs", 3500, 3800, 3700, cfg::LSB, RADIOAM, 1, 4, "très courte distance", "régional" },
    { "49 m · radiodiffusion", 5900, 6200, 6000, cfg::AM, RADIODIF, 3, 5, "", "" },
    { "40 m · radioamateurs", 7000, 7200, 7100, cfg::LSB, RADIOAM, 4, 4, "régional", "longue distance" },
    { "41 m · radiodiffusion", 7200, 7450, 7300, cfg::AM, RADIODIF, 2, 4, "", "" },
    { "31 m · radiodiffusion", 9400, 9900, 9600, cfg::AM, RADIODIF, 3, 4, "", "" },
    { "30 m · radioamateurs", 10100, 10150, 10120, cfg::CW, RADIOAM, 2, 2, "morse et numérique", "morse et numérique" },
    { "25 m · radiodiffusion", 11600, 12100, 11800, cfg::AM, RADIODIF, 4, 2, "", "" },
    { "22 m · radiodiffusion", 13570, 13870, 13700, cfg::AM, RADIODIF, 3, 1, "", "" },
    { "20 m · radioamateurs", 14000, 14350, 14200, cfg::USB, RADIOAM, 5, 1, "longue distance", "rare la nuit" },
    { "19 m · radiodiffusion", 15100, 15800, 15400, cfg::AM, RADIODIF, 4, 1, "", "" },
    { "16 m · radiodiffusion", 17480, 17900, 17700, cfg::AM, RADIODIF, 3, 0, "", "fermée la nuit" },
    { "17 m · radioamateurs", 18068, 18168, 18130, cfg::USB, RADIOAM, 3, 0, "longue distance", "fermée la nuit" },
    { "15 m · radioamateurs", 21000, 21450, 21250, cfg::USB, RADIOAM, 3, 0, "longue distance", "fermée la nuit" },
    { "CB · 27 MHz", 26965, 27405, 27185, cfg::AM, TALKIE, 2, 0, "canal 19 : routiers", "fermée la nuit" },
    { "10 m · radioamateurs", 28000, 29700, 28500, cfg::USB, RADIOAM, 2, 0, "ouvertures surtout l'été", "fermée la nuit" },
};

static int oc(const Contexte &c, Sugg *dst, int nmax)
{
    int n = 0;
    bool heure = c.epoch != 0;
    float d = heure ? lumiere(c.epoch, c.lat, c.lon) : 0.5f;
    // l'ionosphere garde les bandes hautes ouvertes un moment apres le coucher du soleil
    float d_haut = heure ? std::max(d, lumiere(c.epoch - 5400, c.lat, c.lon)) : 0.5f;
    float sa = heure ? saison(c.epoch, c.lat) : 0;
    bool ete = sa > 0.5f, hiver = sa < -0.5f;
    bool region1 = c.lon > -30 && c.lon < 60;           // Europe, Afrique, Moyen-Orient
    for (const BandeOC &b : OC_BANDES) {
        if (b.a < 300 && !region1) continue;            // grandes ondes : Europe seulement
        float j = b.j, nu = b.n;
        if (b.b <= 4000) { if (hiver) nu += 1; if (ete) nu -= 1; }               // parasites d'ete, longues nuits d'hiver
        if (b.a >= 10000 && b.a < 19000 && ete) nu += 2;                         // ete : les bandes hautes restent ouvertes
        if (b.a >= 26000 && ete) j += 1;                                         // ouvertures sporadiques
        if (b.a >= 20000 && b.a < 26000 && hiver) j += 1;                        // 15 m : meilleur les jours d'hiver
        // propagation du jour : le flux solaire ouvre ou ferme les bandes hautes
        float flux = s_flux, kp = s_kp;
        if (flux >= 150 && b.a >= 14000) j += b.a >= 20000 ? 2 : 1;
        if (flux >= 0 && flux < 90 && b.a >= 17000) j -= 1;
        if (j > 5) j = 5;
        if (j < 0) j = 0;
        float dd = b.a >= 10000 ? d_haut : d;
        float v = j * dd + nu * (1 - dd);
        if (heure && d > 0.1f && d < 0.9f && b.genre == RADIOAM && b.b <= 7200 && b.a >= 1800) v += 1;   // crepuscule : les bandes basses portent tres loin
        if (b.a >= 1800) { if (kp >= 7) v -= 2; else if (kp >= 5) v -= 1; }        // orage magnetique : tout passe moins bien
        int niv = (int)floorf(v + 0.5f);
        if (heure && b.genre != RADIODIF && niv > 1) {  // en pleine nuit, la bande est ouverte mais les operateurs dorment
            float h = heure_solaire(c.epoch, c.lon);
            if (h >= 0.5f && h < 5.5f) niv = niv > 3 ? niv - 2 : 1;
        }
        if (niv > 5) niv = 5;
        if (niv < 0) niv = 0;
        const char *note = dd >= 0.5f ? b.note_j : b.note_n;
        if (kp >= 5 && b.a >= 1800) note = "orage magnétique";
        char tmp[32];
        if (b.genre == RADIODIF && !note[0]) {          // combien de stations de l'annuaire dans cette bande
            int k = 0;
            for (int i = 0; i < sta::nb(); i++) { const sta::Station *s = sta::station(i); if (s && s->khz >= b.a && s->khz <= b.b) k++; }
            if (k > 0) { snprintf(tmp, sizeof tmp, "%d dans l'annuaire", k); note = tmp; }
        }
        bool po_amer = b.a == 531 && c.lon <= -30;      // Ameriques : petites ondes jusqu'a 1 700 kHz, canaux de 10 kHz
        ajouter(dst, &n, nmax, b.nom, b.a, po_amer ? 1700 : b.b, po_amer ? 1000 : b.f, b.mode, b.genre, OC, niv,
                false, b.genre == RADIODIF ? 10 : 3, note);
    }
    // emissions en continu
    bool jour = d >= 0.5f;
    if (srv::distance_km(c.lat, c.lon, 52.7f, -8.9f) < 3500) {          // Shannon (Irlande) : meteo aviation et trafic de l'Atlantique
        ajouter(dst, &n, nmax, "VOLMET Shannon", jour ? 8957 : 5505, jour ? 8957 : 5505, jour ? 8957 : 5505, cfg::USB, UTILITAIRE, OC, 3, true, 3, "météo aviation");
        ajouter(dst, &n, nmax, "Avions sur l'Atlantique", jour ? 8879 : 5649, jour ? 8879 : 5649, jour ? 8879 : 5649, cfg::USB, AVIATION, OC, 2, false, 3, "contrôle Shanwick");
    }
    if (c.lon <= -30)                                                    // Ameriques : signal horaire WWV
        ajouter(dst, &n, nmax, "WWV · signal horaire", jour ? 10000 : 5000, jour ? 10000 : 5000, jour ? 10000 : 5000, cfg::AM, UTILITAIRE, OC, 3, true, 6, "top chaque seconde");
    ajouter(dst, &n, nmax, jour ? "FT8 · 20 m" : "FT8 · 40 m", jour ? 14074 : 7074, jour ? 14074 : 7074, jour ? 14074 : 7074, cfg::USB, UTILITAIRE, OC, 3, true, 3,
            "numérique, pas de voix");
    return n;
}

// ---- VHF / UHF ----
static bool contient(const char *s, const char *mot) { return strstr(s, mot) != nullptr; }

// "LFPB Paris-Le Bourget International Airport" -> "Paris-Le Bourget"
static void nom_court(const char *nom, char *dst, size_t n)
{
    if (strlen(nom) > 5 && nom[4] == ' ') nom += 5;                 // code OACI
    char t[64];
    snprintf(t, sizeof t, "%s", nom);
    char *dst0 = dst;
    dst = t;
    static const char *const FIN[] = { " Airport", " Airfield", " Air Base", " Aerodrome", " International", " Intl" };
    for (int tour = 0; tour < 2; tour++)
        for (const char *f : FIN) {
            size_t l = strlen(dst), lf = strlen(f);
            if (l > lf + 2 && !strcmp(dst + l - lf, f)) dst[l - lf] = 0;
        }
    // nom coupe a 39 caracteres dans la liste : "... Internationa" -> la fin entamee part aussi
    static const char *const ENTAME[] = { "International Airport", "International", "Airport", "Airfield", "Air Base", "Aerodrome" };
    for (char *e = t; (e = strchr(e, ' ')) != nullptr; e++) {
        size_t reste = strlen(e + 1);
        if (reste < 2 || e - t < 3) continue;
        bool coupe = false;
        for (const char *f : ENTAME) if (!strncmp(f, e + 1, reste)) coupe = true;
        if (coupe) { *e = 0; break; }
    }
    snprintf(dst0, n, "%s", t);
}

static int vhf_uhf(const Contexte &c, int cat, Sugg *dst, int nmax)
{
    int n = 0;
    bool heure = c.epoch != 0;
    float d = heure ? lumiere(c.epoch, c.lat, c.lon) : 0.5f;
    float h = heure ? heure_solaire(c.epoch, c.lon) : 12;
    bool soir = heure && h >= 17 && h < 22;
    bool nuit = heure && (h >= 22.5f || h < 5.5f);
    bool ete = heure && saison(c.epoch, c.lat) > 0.5f;
    bool amer = c.lon <= -30;
    // activite humaine : pleine de 7 h a 21 h, faible la nuit
    int jour3 = nuit ? 1 : 3, jour4 = nuit ? 1 : 4;
    if (cat == VHF) {
        ajouter(dst, &n, nmax, "Radio FM", 87500, 108000, 100000, cfg::WFM, RADIO_FM, VHF, 5, true, 200, "musique, infos");
        // aerodromes les plus proches du recepteur : ATIS (message en boucle), tour et approche
        const vhf::Canal *can[120];                                 // les plus proches d'abord
        int nc = vhf::chercher(vhf::AVIA, "", can, 120);
        int n_atis = 0, n_ctl = 0;
        for (int i = 0; i < nc && (n_atis < 2 || n_ctl < 3); i++) {
            const vhf::Canal *k = can[i];
            if (k->lat == 999) continue;                                // canaux communs : plus bas
            bool atis = contient(k->type, "ATIS");
            bool ctl = contient(k->type, "Tour") || contient(k->type, "Approche");
            if (atis && n_atis < 2 && k->km <= 100) {
                char nom[40], note[40], court[34];
                nom_court(k->nom, court, sizeof court);
                snprintf(nom, sizeof nom, "ATIS %s", court);
                snprintf(note, sizeof note, "en boucle · %u km", k->km);
                ajouter(dst, &n, nmax, nom, k->khz, k->khz, k->khz, cfg::AM, AVIATION, VHF, k->km <= 50 ? 5 : 4, true, 25, note);
                n_atis++;
            } else if (ctl && n_ctl < 3 && k->km <= 80) {
                char nom[40], note[40], court[28];
                nom_court(k->nom, court, sizeof court);
                snprintf(nom, sizeof nom, "%.10s %s", k->type, court);
                bool deja = false;                                      // une seule frequence par aerodrome et par service
                for (int q = 0; q < n; q++) if (!strcmp(dst[q].nom, nom)) deja = true;
                if (deja) continue;
                snprintf(note, sizeof note, "%u km", k->km);
                int niv = (k->km <= 30 ? jour4 : jour3);
                if (heure && !nuit && d < 0.5f && niv > 2) niv--;       // moins de vols a la tombee de la nuit
                ajouter(dst, &n, nmax, nom, k->khz, k->khz, k->khz, cfg::AM, AVIATION, VHF, niv, false, 25, note);
                n_ctl++;
            }
        }
        ajouter(dst, &n, nmax, "Bande aviation", 118000, 137000, 121500, cfg::AM, AVIATION, VHF, nuit ? 1 : 3, false, 25, "toute la bande");
        ajouter(dst, &n, nmax, "2 m · radioamateurs", 144000, amer ? 148000 : 146000, amer ? 146520 : 145500, cfg::FM, RADIOAM, VHF, nuit ? 1 : (soir ? 3 : 2), false, 12.5f,
                soir ? "relais, le soir surtout" : "relais et appels");
        ajouter(dst, &n, nmax, "Marine canal 16", 156800, 156800, 156800, cfg::FM, MARINE, VHF, nuit ? 1 : 2, false, 25, "appel, près des côtes");
        ajouter(dst, &n, nmax, "6 m · radioamateurs", 50000, amer ? 54000 : 52000, amer ? 50125 : 50150, cfg::USB, RADIOAM, VHF, ete && d > 0.5f ? 3 : (ete ? 1 : 0), false, 3,
                "ouvertures surtout l'été");
        ajouter(dst, &n, nmax, "Station spatiale ISS", 145800, 145800, 145800, cfg::FM, UTILITAIRE, VHF, 1, false, 12.5f, "lors des passages");
    } else {
        if (amer)
            ajouter(dst, &n, nmax, "FRS/GMRS · talkies-walkies", 462562.5, 462725, 462562.5, cfg::FM, TALKIE, UHF, nuit ? 1 : (soir ? 2 : 3), false, 12.5f, "22 canaux");
        else
            ajouter(dst, &n, nmax, "PMR446 · talkies-walkies", 446000, 446200, 446006.25, cfg::FM, TALKIE, UHF, nuit ? 1 : (soir ? 2 : 3), false, 12.5f, "16 canaux");
        ajouter(dst, &n, nmax, "70 cm · radioamateurs", 430000, amer ? 450000 : 440000, amer ? 446000 : 433500, cfg::FM, RADIOAM, UHF, nuit ? 1 : (soir ? 3 : 2), false, 12.5f,
                soir ? "relais, le soir surtout" : "relais et appels");
        if (!amer)
            ajouter(dst, &n, nmax, "433 MHz · capteurs", 433050, 434790, 433920, cfg::FM, UTILITAIRE, UHF, 3, false, 25, "télécommandes, bips");
        ajouter(dst, &n, nmax, "23 cm · radioamateurs", 1240000, 1300000, 1297500, cfg::FM, RADIOAM, UHF, soir ? 1 : 0, false, 12.5f, "rare");
    }
    return n;
}

int proposer(const Contexte &c, int cat, Sugg *dst, int nmax)
{
    int n = 0;
    if (cat == OC || cat == TOUT) n += oc(c, dst + n, nmax - n);
    if (cat == VHF || cat == TOUT) n += vhf_uhf(c, VHF, dst + n, nmax - n);
    if (cat == UHF || cat == TOUT) n += vhf_uhf(c, UHF, dst + n, nmax - n);
    std::stable_sort(dst, dst + n, [](const Sugg &x, const Sugg &y) { return x.niveau > y.niveau; });
    return n;
}

// ---- mesure ----
int compter(const float *db, int n, double f0, double kpp, double a, double b, float seuil_db, float larg,
            double *f_fort, float *db_au_dessus)
{
    if (f_fort) *f_fort = 0;
    if (db_au_dessus) *db_au_dessus = 0;
    if (!db || n < 8 || kpp <= 0) return -1;
    // bruit de fond : 25e centile, par histogramme de 1 dB (-170 .. 0 dBm)
    uint16_t histo[171];
    memset(histo, 0, sizeof histo);
    int valides = 0;
    for (int i = 0; i < n; i++) {
        if (db[i] < -129.5f) continue;                  // point pas encore recu
        int k = (int)lroundf(-db[i]);
        histo[k < 0 ? 0 : (k > 170 ? 170 : k)]++;
        valides++;
    }
    if (valides < n / 2) return -1;
    int cumul = 0, kb = 170;
    for (int k = 170; k >= 0; k--) { cumul += histo[k]; if (cumul >= valides / 4) { kb = k; break; } }
    float seuil = -(float)kb + seuil_db;
    // une seule frequence : un demi-canal de chaque cote
    if (a == b) { a -= larg / 2; b += larg / 2; }
    int i0 = (int)ceil((a - f0) / kpp), i1 = (int)floor((b - f0) / kpp);
    if (i0 < 0) i0 = 0;
    if (i1 > n - 1) i1 = n - 1;
    if (i1 < i0) {                                      // plage plus etroite qu'un point : le point le plus proche
        int i = (int)lround(((a + b) / 2 - f0) / kpp);
        if (i < 0 || i >= n) return -1;
        i0 = i1 = i;
    }
    int signaux = 0, debut = -1, imax = -1;
    for (int i = i0; i <= i1 + 1; i++) {
        bool haut = i <= i1 && db[i] > seuil;
        if (haut) {
            if (debut < 0) debut = i;
            if (imax < 0 || db[i] > db[imax]) imax = i;
        } else if (debut >= 0) {
            double l = (i - debut) * kpp;
            int k = larg > 0 ? (int)floor(l / larg + 0.5) : 1;
            signaux += k < 1 ? 1 : k;
            debut = -1;
        }
    }
    if (imax >= 0) {
        if (f_fort) *f_fort = f0 + (imax + 0.5) * kpp;
        if (db_au_dessus) *db_au_dessus = db[imax] - (seuil - seuil_db);
    }
    return signaux;
}

int niveau_mesure(int s)
{
    if (s <= 0) return 0;
    if (s == 1) return 1;
    if (s <= 3) return 2;
    if (s <= 6) return 3;
    if (s <= 10) return 4;
    return 5;
}

}  // namespace quoi
