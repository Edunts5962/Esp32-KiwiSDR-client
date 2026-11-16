// ft8.cpp - voir ft8.h
#include "ft8.h"
#include "plateforme.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

extern "C" {
#include "src/ft8/decode.h"
#include "src/ft8/encode.h"
#include "src/ft8/message.h"
#include "src/ft8/constants.h"
#include "src/ft8/monitor.h"
}

// grands tableaux de ft8_lib : en PSRAM
extern "C" void *ft8_malloc(size_t n) { return plat_malloc_ps(n); }

namespace f8 {

const int BANDES_KHZ[NB_BANDES] = { 1840, 3573, 5357, 7074, 10136, 14074, 18100, 21074, 24915, 28074, 50313 };
const char *const BANDES_NOM[NB_BANDES] = { "160 m", "80 m", "60 m", "40 m", "30 m", "20 m", "17 m", "15 m", "12 m", "10 m", "6 m" };

static const int FS = 12000, NPER = 15 * FS;
static const int MAX_CAND = 140, LDPC_ITER = 25, MIN_SCORE = 10, MAX_DEC = 50;
static const int NMSG = 120;

// --------------------------------------------------------------- etat partage
static volatile bool s_actif = false;
static int16_t *s_tampon[2] = { nullptr, nullptr };   // une periode s'enregistre pendant que l'autre se decode
static int s_ecrit = 0;                // tampon en cours d'enregistrement
static int s_pos = 0;
static uint32_t s_periode = 0;         // periode en cours (secondes UTC / 15 * 15)
static bool s_complete = false;        // la periode en cours a ete prise des son debut
static float s_frac = 0, s_prec = 0;   // reechantillonnage vers 12 kHz
static volatile int s_a_decoder = -1;  // tampon a decoder (-1 : rien)
static volatile int s_n_decoder = 0;
static volatile uint32_t s_p_decoder = 0;
static float s_lat = 0, s_lon = 0;
static bool s_pos_ok = false;

static Message *s_msg = nullptr;       // anneau (PSRAM)
static volatile int s_nmsg = 0, s_tete = 0;
static volatile uint32_t s_version = 0;
static volatile int s_derniers = 0;
static volatile uint32_t s_duree = 0;
static volatile bool s_effacer = false;

void init()
{
    if (!s_msg) s_msg = (Message *)plat_malloc_ps(sizeof(Message) * NMSG);
}
void actif(bool a)
{
    if (a && !s_actif) { s_complete = false; s_pos = 0; s_periode = 0; }
    s_actif = a;
}
bool actif() { return s_actif; }
void position(float lat, float lon) { s_lat = lat; s_lon = lon; s_pos_ok = lat != 0 || lon != 0; }
int nb() { return s_nmsg; }
const Message &message(int i) { return s_msg[(s_tete - 1 - i + 2 * NMSG) % NMSG]; }
uint32_t version() { return s_version; }
void effacer() { s_effacer = true; }
int derniers() { return s_derniers; }
uint32_t duree_ms() { return s_duree; }
bool heure_ok() { return plat_epoch_ms() != 0; }
int secondes_avant()
{
    uint64_t t = plat_epoch_ms();
    if (!t) return -1;
    return (int)((15000 - t % 15000 + 999) / 1000);
}

// --------------------------------------------------------------- enregistrement
void traiter(const int16_t *s, int n, float fs)
{
    if (!s_actif || n <= 0 || fs < 4000) return;
    if (!s_tampon[0]) {
        s_tampon[0] = (int16_t *)plat_malloc_ps(NPER * 2);
        s_tampon[1] = (int16_t *)plat_malloc_ps(NPER * 2);
        if (!s_tampon[0] || !s_tampon[1]) return;
    }
    uint64_t t = plat_epoch_ms();
    if (!t) return;                                 // heure pas encore mise a jour par le reseau
    uint32_t p = (uint32_t)(t / 15000) * 15;
    if (p != s_periode) {
        // fin de periode : on la passe a la tache de decodage si elle est (presque) entiere
        if (s_periode && s_complete && s_pos > 12 * FS && s_a_decoder < 0) {
            s_n_decoder = s_pos;
            s_p_decoder = s_periode;
            s_a_decoder = s_ecrit;
            s_ecrit ^= 1;
        }
        s_complete = s_periode != 0 || (t % 15000) < 1500;   // arrivee en cours de periode : celle-ci ne compte pas
        s_periode = p;
        s_pos = 0;
    }
    int16_t *b = s_tampon[s_ecrit];
    float pas = fs / FS;                            // lecture a 12 kHz, interpolation lineaire
    if (fabsf(pas - 1) < 0.002f) {
        int k = n < NPER - s_pos ? n : NPER - s_pos;
        memcpy(b + s_pos, s, k * 2);
        s_pos += k;
        return;
    }
    for (int i = 0; i < n; i++) {
        float v = s[i];
        while (s_frac < 1.0f) {
            if (s_pos < NPER) b[s_pos++] = (int16_t)(s_prec + (v - s_prec) * s_frac);
            s_frac += pas;
        }
        s_frac -= 1.0f;
        s_prec = v;
    }
}

// --------------------------------------------------------------- indicatifs (table de hachage de ft8_lib)
#define HT 256
static struct { char indicatif[12]; uint32_t hash; } s_ht[HT];
static int s_ht_n = 0;
static void ht_nettoyer(uint8_t age_max)
{
    for (int i = 0; i < HT; i++) {
        if (!s_ht[i].indicatif[0]) continue;
        uint8_t age = (uint8_t)(s_ht[i].hash >> 24);
        if (age > age_max) { s_ht[i].indicatif[0] = 0; s_ht[i].hash = 0; s_ht_n--; }
        else s_ht[i].hash = (((uint32_t)age + 1u) << 24) | (s_ht[i].hash & 0x3FFFFFu);
    }
}
static void ht_ajouter(const char *c, uint32_t hash)
{
    if (s_ht_n >= HT - 8) return;
    int i = (((hash >> 12) & 0x3FFu) * 23) % HT;
    while (s_ht[i].indicatif[0]) {
        if ((s_ht[i].hash & 0x3FFFFFu) == hash && !strcmp(s_ht[i].indicatif, c)) { s_ht[i].hash &= 0x3FFFFFu; return; }
        i = (i + 1) % HT;
    }
    s_ht_n++;
    strncpy(s_ht[i].indicatif, c, 11);
    s_ht[i].indicatif[11] = 0;
    s_ht[i].hash = hash;
}
static bool ht_chercher(ftx_callsign_hash_type_t type, uint32_t hash, char *c)
{
    uint8_t dec = type == FTX_CALLSIGN_HASH_10_BITS ? 12 : (type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    int i = (((hash >> (12 - dec)) & 0x3FFu) * 23) % HT;
    for (int k = 0; k < HT && s_ht[i].indicatif[0]; k++) {
        if (((s_ht[i].hash & 0x3FFFFFu) >> dec) == hash) { strcpy(c, s_ht[i].indicatif); return true; }
        i = (i + 1) % HT;
    }
    c[0] = 0;
    return false;
}
static ftx_callsign_hash_interface_t s_hash_if = { ht_chercher, ht_ajouter };

// --------------------------------------------------------------- locator et pays
bool locator(const char *l, float *lat, float *lon)
{
    if (strlen(l) < 4) return false;
    char a = toupper(l[0]), b = toupper(l[1]);
    if (a < 'A' || a > 'R' || b < 'A' || b > 'R' || !isdigit((unsigned char)l[2]) || !isdigit((unsigned char)l[3])) return false;
    *lon = -180 + (a - 'A') * 20 + (l[2] - '0') * 2 + 1;
    *lat = -90 + (b - 'A') * 10 + (l[3] - '0') * 1 + 0.5f;
    return true;
}

// prefixes -> pays (les plus courants ; le plus long prefixe gagne). Approximatif : les prefixes speciaux,
// les portables (/P, /MM) et les petites entites ne sont pas tous la.
static const char *const PAYS[] = {
    "F", "France", "TM", "France", "TK", "Corse", "FG", "Guadeloupe", "FM", "Martinique", "FY", "Guyane", "FR", "Réunion", "FK", "Nouv.-Calédonie", "FO", "Polynésie", "FP", "St-Pierre-et-Miquelon", "FH", "Mayotte",
    "ON", "Belgique", "OO", "Belgique", "OR", "Belgique", "OT", "Belgique", "HB9", "Suisse", "HB3", "Suisse", "HB0", "Liechtenstein", "LX", "Luxembourg", "3A", "Monaco",
    "G", "Angleterre", "M", "Angleterre", "2E", "Angleterre", "GW", "Galles", "MW", "Galles", "GM", "Écosse", "MM", "Écosse", "GI", "Irl. du Nord", "MI", "Irl. du Nord", "GD", "Île de Man", "GJ", "Jersey", "MJ", "Jersey", "GU", "Guernesey", "EI", "Irlande", "EJ", "Irlande",
    "D", "Allemagne", "PA", "Pays-Bas", "PB", "Pays-Bas", "PD", "Pays-Bas", "PE", "Pays-Bas", "PH", "Pays-Bas",
    "I", "Italie", "IS0", "Sardaigne", "IT9", "Sicile", "T7", "Saint-Marin", "HV", "Vatican", "9H", "Malte",
    "EA", "Espagne", "EB", "Espagne", "EC", "Espagne", "ED", "Espagne", "EE", "Espagne", "EF", "Espagne", "EG", "Espagne", "EH", "Espagne", "EA6", "Baléares", "EA8", "Canaries", "EA9", "Ceuta-Melilla",
    "CT", "Portugal", "CS", "Portugal", "CU", "Açores", "CT3", "Madère", "C3", "Andorre",
    "OE", "Autriche", "OK", "Tchéquie", "OL", "Tchéquie", "OM", "Slovaquie", "SP", "Pologne", "SQ", "Pologne", "SN", "Pologne", "SO", "Pologne", "3Z", "Pologne", "HF", "Pologne",
    "HA", "Hongrie", "HG", "Hongrie", "S5", "Slovénie", "9A", "Croatie", "E7", "Bosnie", "YU", "Serbie", "YT", "Serbie", "4O", "Monténégro", "Z3", "Macédoine du N.", "ZA", "Albanie",
    "YO", "Roumanie", "YP", "Roumanie", "YQ", "Roumanie", "YR", "Roumanie", "LZ", "Bulgarie", "SV", "Grèce", "SW", "Grèce", "SX", "Grèce", "SY", "Grèce", "SZ", "Grèce", "SV5", "Dodécanèse", "SV9", "Crète", "5B", "Chypre", "C4", "Chypre", "P3", "Chypre",
    "TA", "Turquie", "TB", "Turquie", "TC", "Turquie", "YM", "Turquie",
    "OZ", "Danemark", "5P", "Danemark", "5Q", "Danemark", "OU", "Danemark", "OV", "Danemark", "OX", "Groenland", "OY", "Féroé",
    "LA", "Norvège", "LB", "Norvège", "LC", "Norvège", "LI", "Norvège", "LN", "Norvège", "JW", "Svalbard", "JX", "Jan Mayen",
    "SM", "Suède", "SA", "Suède", "SB", "Suède", "SE", "Suède", "SF", "Suède", "SG", "Suède", "SK", "Suède", "SL", "Suède", "7S", "Suède", "8S", "Suède",
    "OH", "Finlande", "OF", "Finlande", "OG", "Finlande", "OI", "Finlande", "OH0", "Åland", "OJ0", "Market Reef", "TF", "Islande",
    "ES", "Estonie", "YL", "Lettonie", "LY", "Lituanie", "EW", "Biélorussie", "EU", "Biélorussie", "ER", "Moldavie",
    "UR", "Ukraine", "US", "Ukraine", "UT", "Ukraine", "UU", "Ukraine", "UV", "Ukraine", "UW", "Ukraine", "UX", "Ukraine", "UY", "Ukraine", "UZ", "Ukraine", "EM", "Ukraine", "EN", "Ukraine", "EO", "Ukraine",
    "R", "Russie", "U", "Russie", "UA2", "Kaliningrad", "RA2", "Kaliningrad", "R2F", "Kaliningrad",
    "UA9", "Russie (Asie)", "UA0", "Russie (Asie)", "RA9", "Russie (Asie)", "RA0", "Russie (Asie)", "RU9", "Russie (Asie)", "RV9", "Russie (Asie)", "RW9", "Russie (Asie)", "RX9", "Russie (Asie)", "RZ9", "Russie (Asie)", "R9", "Russie (Asie)", "R0", "Russie (Asie)", "RK9", "Russie (Asie)", "RL9", "Russie (Asie)", "RN9", "Russie (Asie)", "RT9", "Russie (Asie)", "UB9", "Russie (Asie)", "UI9", "Russie (Asie)",
    "4L", "Géorgie", "EK", "Arménie", "4J", "Azerbaïdjan", "4K", "Azerbaïdjan", "UN", "Kazakhstan", "UP", "Kazakhstan", "UK", "Ouzbékistan", "EX", "Kirghizistan", "EY", "Tadjikistan", "EZ", "Turkménistan",
    "4X", "Israël", "4Z", "Israël", "OD", "Liban", "JY", "Jordanie", "HZ", "Arabie saoudite", "7Z", "Arabie saoudite", "A4", "Oman", "A6", "Émirats", "A7", "Qatar", "A9", "Bahreïn", "9K", "Koweït", "YI", "Irak", "EP", "Iran", "AP", "Pakistan", "YA", "Afghanistan",
    "VU", "Inde", "4S", "Sri Lanka", "9N", "Népal", "S2", "Bangladesh", "8Q", "Maldives",
    "BY", "Chine", "BG", "Chine", "BH", "Chine", "BD", "Chine", "BA", "Chine", "BI", "Chine", "BV", "Taïwan", "BX", "Taïwan", "VR", "Hong Kong", "XX9", "Macao",
    "JA", "Japon", "JE", "Japon", "JF", "Japon", "JG", "Japon", "JH", "Japon", "JI", "Japon", "JJ", "Japon", "JK", "Japon", "JL", "Japon", "JM", "Japon", "JN", "Japon", "JO", "Japon", "JP", "Japon", "JQ", "Japon", "JR", "Japon", "JS", "Japon", "7J", "Japon", "7K", "Japon", "7L", "Japon", "7M", "Japon", "7N", "Japon", "8J", "Japon", "8N", "Japon",
    "HL", "Corée du Sud", "DS", "Corée du Sud", "DT", "Corée du Sud", "6K", "Corée du Sud", "6L", "Corée du Sud", "D7", "Corée du Sud", "D9", "Corée du Sud", "P5", "Corée du Nord",
    "BU", "Taïwan", "DU", "Philippines", "DV", "Philippines", "DW", "Philippines", "DX", "Philippines", "DY", "Philippines", "DZ", "Philippines", "4D", "Philippines", "4F", "Philippines", "4I", "Philippines",
    "HS", "Thaïlande", "E2", "Thaïlande", "XV", "Viêt Nam", "3W", "Viêt Nam", "XU", "Cambodge", "XW", "Laos", "XZ", "Birmanie",
    "9M2", "Malaisie", "9M4", "Malaisie", "9W2", "Malaisie", "9M6", "Malaisie (Est)", "9M8", "Malaisie (Est)", "9V", "Singapour",
    "YB", "Indonésie", "YC", "Indonésie", "YD", "Indonésie", "YE", "Indonésie", "YF", "Indonésie", "YG", "Indonésie", "YH", "Indonésie", "PK", "Indonésie", "PM", "Indonésie",
    "VK", "Australie", "AX", "Australie", "ZL", "Nouvelle-Zélande", "ZM", "Nouvelle-Zélande", "KH6", "Hawaï", "WH6", "Hawaï", "AH6", "Hawaï", "NH6", "Hawaï", "KH2", "Guam", "P2", "Papouasie", "3D2", "Fidji", "5W", "Samoa", "A3", "Tonga", "YJ", "Vanuatu", "H4", "Salomon", "T2", "Tuvalu", "T3", "Kiribati", "V7", "Marshall", "V6", "Micronésie", "KH0", "Mariannes",
    "K", "États-Unis", "W", "États-Unis", "N", "États-Unis", "AA", "États-Unis", "AB", "États-Unis", "AC", "États-Unis", "AD", "États-Unis", "AE", "États-Unis", "AF", "États-Unis", "AG", "États-Unis", "AI", "États-Unis", "AJ", "États-Unis", "AK", "États-Unis", "AL", "Alaska", "KL", "Alaska", "WL", "Alaska", "NL", "Alaska", "KP4", "Porto Rico", "WP4", "Porto Rico", "NP4", "Porto Rico", "KP2", "Îles Vierges US",
    "VE", "Canada", "VA", "Canada", "VO", "Canada", "VY", "Canada", "CY", "Canada", "CF", "Canada", "XE", "Mexique", "XF", "Mexique", "4A", "Mexique", "6D", "Mexique",
    "CO", "Cuba", "CM", "Cuba", "CL", "Cuba", "HI", "Rép. dominicaine", "HH", "Haïti", "6Y", "Jamaïque", "C6", "Bahamas", "8P", "Barbade", "9Y", "Trinité", "J3", "Grenade", "J6", "Sainte-Lucie", "J7", "Dominique", "J8", "St-Vincent", "V2", "Antigua", "V4", "St-Kitts", "PJ2", "Curaçao", "PJ4", "Bonaire", "P4", "Aruba", "FJ", "St-Barthélemy", "FS", "St-Martin", "ZF", "Caïmans", "VP2E", "Anguilla", "VP2V", "Îles Vierges GB", "VP5", "Turks-et-Caïcos", "VP9", "Bermudes",
    "TI", "Costa Rica", "TG", "Guatemala", "YN", "Nicaragua", "HR", "Honduras", "YS", "Salvador", "HP", "Panama", "V3", "Belize",
    "PY", "Brésil", "PP", "Brésil", "PQ", "Brésil", "PR", "Brésil", "PS", "Brésil", "PT", "Brésil", "PU", "Brésil", "PV", "Brésil", "PW", "Brésil", "ZV", "Brésil", "ZW", "Brésil", "ZX", "Brésil", "ZY", "Brésil", "ZZ", "Brésil",
    "LU", "Argentine", "LW", "Argentine", "LO", "Argentine", "LP", "Argentine", "LQ", "Argentine", "LR", "Argentine", "LS", "Argentine", "LT", "Argentine", "AY", "Argentine", "AZ", "Argentine", "L2", "Argentine", "L3", "Argentine", "L4", "Argentine", "L5", "Argentine", "L6", "Argentine", "L7", "Argentine", "L8", "Argentine", "L9", "Argentine",
    "CE", "Chili", "CA", "Chili", "CB", "Chili", "CC", "Chili", "CD", "Chili", "XQ", "Chili", "XR", "Chili", "3G", "Chili", "CX", "Uruguay", "CV", "Uruguay", "ZP", "Paraguay", "CP", "Bolivie", "OA", "Pérou", "OB", "Pérou", "OC", "Pérou", "4T", "Pérou", "HC", "Équateur", "HD", "Équateur", "HK", "Colombie", "HJ", "Colombie", "5J", "Colombie", "5K", "Colombie", "YV", "Venezuela", "YW", "Venezuela", "YY", "Venezuela", "4M", "Venezuela", "8R", "Guyana", "PZ", "Suriname", "VP8", "Malouines",
    "CN", "Maroc", "5C", "Maroc", "7X", "Algérie", "3V", "Tunisie", "5A", "Libye", "SU", "Égypte", "ST", "Soudan", "ET", "Éthiopie", "E3", "Érythrée", "J2", "Djibouti", "6O", "Somalie", "T5", "Somalie",
    "5Z", "Kenya", "5Y", "Kenya", "5H", "Tanzanie", "5X", "Ouganda", "9X", "Rwanda", "9U", "Burundi", "9Q", "RD Congo", "TN", "Congo", "TR", "Gabon", "TJ", "Cameroun", "TL", "Centrafrique", "TT", "Tchad", "5U", "Niger", "TZ", "Mali", "XT", "Burkina Faso", "6W", "Sénégal", "C5", "Gambie", "J5", "Guinée-Bissau", "3X", "Guinée", "9L", "Sierra Leone", "EL", "Liberia", "TU", "Côte d'Ivoire", "9G", "Ghana", "5V", "Togo", "TY", "Bénin", "5N", "Nigeria", "3C", "Guinée équat.", "S9", "São Tomé", "D4", "Cap-Vert", "5T", "Mauritanie",
    "ZS", "Afrique du Sud", "ZR", "Afrique du Sud", "ZT", "Afrique du Sud", "ZU", "Afrique du Sud", "V5", "Namibie", "A2", "Botswana", "Z2", "Zimbabwe", "9J", "Zambie", "7Q", "Malawi", "C9", "Mozambique", "D2", "Angola", "7P", "Lesotho", "3DA", "Eswatini", "5R", "Madagascar", "3B8", "Maurice", "3B9", "Rodrigues", "S7", "Seychelles", "D6", "Comores", "FT", "Terres australes", "ZD7", "Sainte-Hélène", "ZD8", "Ascension",
    "KC4", "Antarctique", "DP0", "Antarctique", "VP8/", "Antarctique",
};
const char *pays(const char *c)
{
    if (!c || !c[0]) return "";
    // un prefixe en "/" (ex. F/ON4XX, EA8/DL1AB) indique ou est la station
    char base[16];
    const char *barre = strchr(c, '/');
    if (barre && barre - c <= 4 && strlen(barre + 1) > 3) { int k = (int)(barre - c); memcpy(base, c, k); base[k] = 0; }
    else { snprintf(base, sizeof base, "%s", c); char *b = strchr(base, '/'); if (b) *b = 0; }
    const char *res = "";
    size_t meilleur = 0;
    for (size_t i = 0; i + 1 < sizeof PAYS / sizeof PAYS[0]; i += 2) {
        size_t l = strlen(PAYS[i]);
        if (l > meilleur && !strncmp(base, PAYS[i], l)) { meilleur = l; res = PAYS[i + 1]; }
    }
    return res;
}

static uint16_t distance_km(float la1, float lo1, float la2, float lo2)
{
    const float R = 6371, D = (float)M_PI / 180;
    float a = sinf((la2 - la1) * D / 2), b = sinf((lo2 - lo1) * D / 2);
    float h = a * a + cosf(la1 * D) * cosf(la2 * D) * b * b;
    float d = 2 * R * asinf(sqrtf(h > 1 ? 1 : h));
    return (uint16_t)(d > 65000 ? 65000 : d);
}

// indicatif qui emet et locator, d'apres le texte du message
static bool est_locator(const char *t)
{
    return strlen(t) == 4 && t[0] >= 'A' && t[0] <= 'R' && t[1] >= 'A' && t[1] <= 'R' && isdigit((unsigned char)t[2]) && isdigit((unsigned char)t[3]) && strcmp(t, "RR73");
}
static void analyser(Message &m)
{
    char mots[4][16];
    int n = 0;
    const char *p = m.texte;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ') { if (k < 15) mots[n][k++] = *p; p++; }
        mots[n][k] = 0;
        n++;
    }
    m.cq = 0;
    m.indicatif[0] = m.loc[0] = 0;
    int i = 0;
    if (n >= 2 && !strcmp(mots[0], "CQ")) {
        m.cq = 1;
        i = 1;
        // "CQ DX F4ABC JN18", "CQ POTA ..." : le mot apres CQ est un indicatif s'il contient un chiffre
        if (n >= 3) { bool chiffre = false; for (const char *q = mots[1]; *q; q++) chiffre |= isdigit((unsigned char)*q) != 0; if (!chiffre || strlen(mots[1]) <= 2) i = 2; }
    } else if (n >= 2) i = 1;                       // "F4ABC DL1XYZ -12" : c'est DL1XYZ qui emet
    if (i < n) {
        const char *c = mots[i];
        if (c[0] == '<') c++;
        snprintf(m.indicatif, sizeof m.indicatif, "%s", c);
        char *f = strchr(m.indicatif, '>');
        if (f) *f = 0;
    }
    if (i + 1 < n && est_locator(mots[i + 1])) snprintf(m.loc, sizeof m.loc, "%s", mots[i + 1]);
    m.pays = pays(m.indicatif);
    m.km = 0;
    m.lat = m.lon = 0;
    if (m.loc[0] && locator(m.loc, &m.lat, &m.lon) && s_pos_ok) m.km = distance_km(s_lat, s_lon, m.lat, m.lon);
}

// S/B comme WSJT-X (dB dans 2500 Hz) : puissance des tonalites du message, contre le bruit de fond de la bande
// (mediane de chaque ligne du waterfall : les signaux n'occupent qu'une petite partie des 2800 Hz)
static int estimer_snr(const ftx_waterfall_t *wf, const ftx_candidate_t *c, const ftx_message_t *msg)
{
    uint8_t tons[FT8_NN];
    ft8_encode(msg->payload, tons);
    double s = 0, b = 0;
    int ns = 0, nb = 0;
    static uint16_t hist[256];
    for (int k = 0; k < FT8_NN; k++) {
        int bloc = c->time_offset + k;
        if (bloc < 0 || bloc >= wf->num_blocks) continue;
        int ligne = ((bloc * wf->time_osr + c->time_sub) * wf->freq_osr + c->freq_sub) * wf->num_bins;
        int f = c->freq_offset + tons[k];
        if (f < 0 || f >= wf->num_bins) continue;
        s += pow(10.0, (wf->mag[ligne + f] * 0.5 - 120) / 10);
        ns++;
        if (k % 4 == 0) {                               // bruit : une ligne sur quatre suffit
            memset(hist, 0, sizeof hist);
            for (int x = 0; x < wf->num_bins; x++) hist[wf->mag[ligne + x]]++;
            int cible = wf->num_bins / 2, cum = 0, v = 0;
            for (; v < 256; v++) { cum += hist[v]; if (cum >= cible) break; }
            b += pow(10.0, (v * 0.5 - 120) / 10) / 0.693;   // mediane -> moyenne (bruit : loi exponentielle)
            nb++;
        }
    }
    if (!ns || !nb || b <= 0) return -24;
    double ps = s / ns, pb = b / nb;
    double r = (ps - pb) / pb;
    double brut = 10 * log10(r > 1e-3 ? r : 1e-3) - 26.0;            // 1 point = 6,25 Hz ; 2500 Hz = 400 points
    // recale sur WSJT-X (27 enregistrements reels de ft8_lib : ecart restant ~6 dB)
    int snr = (int)lround(0.7 * brut - 5.0);
    return snr < -30 ? -30 : (snr > 30 ? 30 : snr);
}

// --------------------------------------------------------------- coeur du decodage
int decoder(const int16_t *x, int n, uint32_t periode, Message *sortie, int max)
{
    monitor_t mon;
    monitor_config_t cfg;
    cfg.f_min = 200;
    cfg.f_max = 3000;
    cfg.sample_rate = FS;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;
    cfg.protocol = FTX_PROTOCOL_FT8;
    monitor_init(&mon, &cfg);
    if (!mon.wf.mag) { monitor_free(&mon); return 0; }
    static float *bloc = nullptr;                       // tableaux de travail en PSRAM (la memoire interne garde le WiFi)
    static ftx_candidate_t *cand = nullptr;
    static ftx_message_t *vus = nullptr;
    if (!bloc) bloc = (float *)plat_malloc_ps(2048 * sizeof(float));
    if (!cand) cand = (ftx_candidate_t *)plat_malloc_ps(MAX_CAND * sizeof(ftx_candidate_t));
    if (!vus) vus = (ftx_message_t *)plat_malloc_ps(MAX_DEC * sizeof(ftx_message_t));
    if (!bloc || !cand || !vus || mon.block_size > 2048) { monitor_free(&mon); return 0; }
    for (int p = 0; p + mon.block_size <= n; p += mon.block_size) {
        for (int i = 0; i < mon.block_size; i++) bloc[i] = x[p + i] / 32768.0f;
        monitor_process(&mon, bloc);
    }
    int nc = ftx_find_candidates(&mon.wf, MAX_CAND, cand, MIN_SCORE);
    int nv = 0, nout = 0;
    for (int i = 0; i < nc && nout < max && nv < MAX_DEC; i++) {
        ftx_message_t msg;
        ftx_decode_status_t st;
        if (!ftx_decode_candidate(&mon.wf, &cand[i], LDPC_ITER, &msg, &st)) continue;
        bool double_ = false;
        for (int k = 0; k < nv && !double_; k++) double_ = vus[k].hash == msg.hash && !memcmp(vus[k].payload, msg.payload, sizeof msg.payload);
        if (double_) continue;
        vus[nv++] = msg;
        Message &m = sortie[nout];
        memset(&m, 0, sizeof m);
        ftx_message_offsets_t off;
        if (ftx_message_decode(&msg, &s_hash_if, m.texte, &off) != FTX_MESSAGE_RC_OK) continue;
        float hz = (mon.min_bin + cand[i].freq_offset + (float)cand[i].freq_sub / mon.wf.freq_osr) / mon.symbol_period;
        float dt = (cand[i].time_offset + (float)cand[i].time_sub / mon.wf.time_osr) * mon.symbol_period - 0.5f;   // 0 : a l'heure
        m.periode = periode;
        m.hz = (uint16_t)lroundf(hz);
        m.dt10 = (int8_t)(dt * 10 > 99 ? 99 : (dt * 10 < -99 ? -99 : lroundf(dt * 10)));
        m.snr = (int8_t)estimer_snr(&mon.wf, &cand[i], &msg);
        analyser(m);
        nout++;
    }
    monitor_free(&mon);
    ht_nettoyer(10);
    return nout;
}

void tache_un_tour()
{
    if (s_effacer) { s_effacer = false; s_nmsg = 0; s_tete = 0; s_derniers = 0; s_version = s_version + 1; }
    int b = s_a_decoder;
    if (b < 0 || !s_msg) return;
    static Message *res = nullptr;
    if (!res) res = (Message *)plat_malloc_ps(sizeof(Message) * MAX_DEC);
    if (!res) { s_a_decoder = -1; return; }
    uint32_t t0 = plat_ms();
    int n = decoder(s_tampon[b], s_n_decoder, s_p_decoder, res, MAX_DEC);
    s_duree = plat_ms() - t0;
    // les plus forts en dernier : ils restent en haut de la liste
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (res[j].snr < res[i].snr) { Message t = res[i]; res[i] = res[j]; res[j] = t; }
    for (int i = 0; i < n; i++) {
        s_msg[s_tete] = res[i];
        s_tete = (s_tete + 1) % NMSG;
        if (s_nmsg < NMSG) s_nmsg = s_nmsg + 1;
    }
    s_derniers = n;
    s_version = s_version + 1;
    s_a_decoder = -1;
}

}  // namespace f8
