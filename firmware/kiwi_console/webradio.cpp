// webradio.cpp - voir webradio.h
#include "webradio.h"
#include "audio.h"
#include "plateforme.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "minimp3.h"                    // l'implementation est dans fm.cpp
extern "C" {
#include "src/helix_aac/aacdec.h"
}

// memoire du decodeur AAC : en PSRAM (plus de 50 ko avec le SBR)
extern "C" void *helix_malloc(int size) { return plat_malloc_ps((size_t)size); }
extern "C" void helix_free(void *p) { plat_free_ps(p); }

namespace wr {

const char *const RAPIDES_NOM[NB_RAPIDES] = { "Favoris", "France", "Info", "Musique", "Jazz", "Classique" };

static volatile uint32_t s_gen = 1;     // change a chaque nouvelle station ou arret
static volatile bool s_ouvrir = false;
static volatile Etat s_etat = ARRET;
static Station s_station;
static char s_msg[64] = "";
static char s_titre[128] = "";
static volatile uint32_t s_vtitre = 0;
static char s_format[48] = "";

// ---------------------------------------------------------------- favoris
static Station *s_fav = nullptr;
static int s_nfav = 0;
struct FavNvs { char nom[56]; char url[200]; char codec[8]; uint16_t kbps; char pays[3]; };   // sans les genres
static void fav_sauver()
{
    FavNvs *t = (FavNvs *)plat_malloc_ps(sizeof(FavNvs) * MAX_FAV);
    if (!t) return;
    memset(t, 0, sizeof(FavNvs) * MAX_FAV);
    for (int i = 0; i < s_nfav; i++) {
        memcpy(t[i].nom, s_fav[i].nom, sizeof t[i].nom);
        memcpy(t[i].url, s_fav[i].url, sizeof t[i].url);
        memcpy(t[i].codec, s_fav[i].codec, sizeof t[i].codec);
        t[i].kbps = s_fav[i].kbps;
        memcpy(t[i].pays, s_fav[i].pays, sizeof t[i].pays);
    }
    plat_nvs_ecrire("wrfav", t, sizeof(FavNvs) * s_nfav);
    plat_free_ps(t);
}
static void fav_charger()
{
    s_nfav = 0;
    size_t n = plat_nvs_taille("wrfav");
    if (!n || n % sizeof(FavNvs) || n > sizeof(FavNvs) * MAX_FAV) return;
    FavNvs *t = (FavNvs *)plat_malloc_ps(n);
    if (!t) return;
    if (plat_nvs_lire("wrfav", t, n)) {
        for (size_t i = 0; i < n / sizeof(FavNvs); i++) {
            Station &s = s_fav[s_nfav];
            memset(&s, 0, sizeof s);
            memcpy(s.nom, t[i].nom, sizeof s.nom); s.nom[sizeof s.nom - 1] = 0;
            memcpy(s.url, t[i].url, sizeof s.url); s.url[sizeof s.url - 1] = 0;
            memcpy(s.codec, t[i].codec, sizeof s.codec); s.codec[sizeof s.codec - 1] = 0;
            s.kbps = t[i].kbps;
            memcpy(s.pays, t[i].pays, sizeof s.pays); s.pays[2] = 0;
            if (s.url[0]) s_nfav++;
        }
    }
    plat_free_ps(t);
}
int nb_favoris() { return s_nfav; }
const Station &favori(int i) { return s_fav[i]; }
bool est_favori(const char *url)
{
    for (int i = 0; i < s_nfav; i++) if (!strcmp(s_fav[i].url, url)) return true;
    return false;
}
void retirer_favori(int i)
{
    if (i < 0 || i >= s_nfav) return;
    memmove(&s_fav[i], &s_fav[i + 1], sizeof(Station) * (s_nfav - i - 1));
    s_nfav--;
    fav_sauver();
}
void basculer_favori(const Station &s)
{
    for (int i = 0; i < s_nfav; i++) if (!strcmp(s_fav[i].url, s.url)) { retirer_favori(i); return; }
    if (s_nfav >= MAX_FAV || !s.url[0]) return;
    s_fav[s_nfav++] = s;
    fav_sauver();
}

// ---------------------------------------------------------------- recherche
static const int MAX_RES = 40;
static Station *s_res = nullptr;
static volatile int s_nres = 0;
static volatile bool s_req = false, s_cours = false;
static volatile uint32_t s_vres = 0;
static char s_url_req[300];
static char s_cherche[64] = "";

void init()
{
    if (!s_res) s_res = (Station *)plat_malloc_ps(sizeof(Station) * MAX_RES);
    if (!s_fav) { s_fav = (Station *)plat_malloc_ps(sizeof(Station) * MAX_FAV); if (s_fav) fav_charger(); }
}

static void encoder(char *&d, char *fin, const char *s)       // pour une URL
{
    static const char *H = "0123456789ABCDEF";
    for (; *s && d < fin - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.') *d++ = (char)c;
        else { *d++ = '%'; *d++ = H[c >> 4]; *d++ = H[c & 15]; }
    }
    *d = 0;
}
void chercher(const char *nom, const char *pays, const char *genre)
{
    char *d = s_url_req, *fin = s_url_req + sizeof s_url_req;
    d += snprintf(d, fin - d, "https://all.api.radio-browser.info/json/stations/search?hidebroken=true&order=clickcount&reverse=true&limit=80");
    if (nom && nom[0]) { d += snprintf(d, fin - d, "&name="); encoder(d, fin, nom); }
    if (pays && pays[0]) { d += snprintf(d, fin - d, "&countrycode="); encoder(d, fin, pays); }
    if (genre && genre[0]) { d += snprintf(d, fin - d, "&tag="); encoder(d, fin, genre); }
    snprintf(s_cherche, sizeof s_cherche, "%s%s%s%s%s", nom && nom[0] ? nom : "", pays && pays[0] ? (nom && nom[0] ? " · " : "") : "", pays ? pays : "",
             genre && genre[0] ? " · " : "", genre ? genre : "");
    s_req = true;
}
bool recherche_en_cours() { return s_req || s_cours; }
int nb_resultats() { return s_nres; }
const Station &resultat(int i) { return s_res[i]; }
const char *recherche_texte() { return s_cherche; }
uint32_t version_resultats() { return s_vres; }
bool requete(char *url, size_t n)
{
    if (!s_req) return false;
    s_req = false;
    s_cours = true;
    snprintf(url, n, "%s", s_url_req);
    return true;
}

// petit lecteur JSON au fil de l'eau : un tableau d'objets plats
static struct {
    int prof;                  // profondeur {} []
    bool chaine, echap;
    int u;                     // \uXXXX : chiffres restants
    uint32_t cp;
    char cle[24];
    char val[220];
    int nv;
    bool est_cle, attend_val;
    char brut[16];             // nombre ou booleen
    int nb;
    Station cour;
    bool hls;
} J;
static void j_utf8(uint32_t c)
{
    char *v = J.val;
    int &n = J.nv;
    if (c < 0x80) { if (n < (int)sizeof J.val - 1) v[n++] = (char)c; }
    else if (c < 0x800) { if (n < (int)sizeof J.val - 2) { v[n++] = (char)(0xC0 | (c >> 6)); v[n++] = (char)(0x80 | (c & 0x3F)); } }
    else { if (n < (int)sizeof J.val - 3) { v[n++] = (char)(0xE0 | (c >> 12)); v[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); v[n++] = (char)(0x80 | (c & 0x3F)); } }
}
static void j_valeur(const char *k, const char *v)
{
    Station &s = J.cour;
    if (!strcmp(k, "name")) { snprintf(s.nom, sizeof s.nom, "%s", v); char *p = s.nom; while (*p == ' ') p++; if (p != s.nom) memmove(s.nom, p, strlen(p) + 1); }
    else if (!strcmp(k, "url_resolved")) snprintf(s.url, sizeof s.url, "%s", v);
    else if (!strcmp(k, "url") && !s.url[0]) snprintf(s.url, sizeof s.url, "%s", v);
    else if (!strcmp(k, "codec")) snprintf(s.codec, sizeof s.codec, "%s", v);
    else if (!strcmp(k, "countrycode")) snprintf(s.pays, sizeof s.pays, "%s", v);
    else if (!strcmp(k, "tags")) snprintf(s.genres, sizeof s.genres, "%s", v);
    else if (!strcmp(k, "bitrate")) s.kbps = (uint16_t)atoi(v);
    else if (!strcmp(k, "hls")) J.hls = atoi(v) != 0;
}
static bool lisible(const Station &s)
{
    if (!s.url[0] || !s.nom[0] || J.hls) return false;
    if (strstr(s.url, ".m3u8")) return false;                   // HLS : pas lu
    return !strcmp(s.codec, "MP3") || !strcmp(s.codec, "AAC") || !strcmp(s.codec, "AAC+") || !s.codec[0];
}
void resultats_debut()
{
    memset(&J, 0, sizeof J);
    s_nres = 0;
}
void resultats_octets(const char *b, size_t n)
{
    if (!s_res) return;
    for (size_t i = 0; i < n; i++) {
        char c = b[i];
        if (J.chaine) {
            if (J.u) {
                int h = isdigit((unsigned char)c) ? c - '0' : (tolower(c) - 'a' + 10);
                J.cp = J.cp * 16 + (uint32_t)(h & 15);
                if (--J.u == 0) j_utf8(J.cp);
            } else if (J.echap) {
                J.echap = false;
                if (c == 'u') { J.u = 4; J.cp = 0; }
                else j_utf8(c == 'n' ? ' ' : (c == 't' ? ' ' : (c == 'r' ? ' ' : (uint8_t)c)));
            } else if (c == '\\') J.echap = true;
            else if (c == '"') {
                J.chaine = false;
                J.val[J.nv] = 0;
                if (J.est_cle) { snprintf(J.cle, sizeof J.cle, "%s", J.val); }
                else if (J.prof == 2) j_valeur(J.cle, J.val);
            } else if (J.nv < (int)sizeof J.val - 1) J.val[J.nv++] = c;
            continue;
        }
        switch (c) {
        case '"':
            J.chaine = true; J.nv = 0; J.est_cle = !J.attend_val; J.attend_val = false;
            break;
        case ':': J.attend_val = true; J.nb = 0; break;
        case '{': case '[':
            J.prof++;
            J.attend_val = false;
            if (c == '{' && J.prof == 2) { memset(&J.cour, 0, sizeof J.cour); J.hls = false; }
            break;
        case '}': case ']': case ',':
            if (J.nb && J.prof == 2) { J.brut[J.nb] = 0; j_valeur(J.cle, J.brut); }
            J.nb = 0;
            J.attend_val = false;
            if (c == '}' || c == ']') {
                if (c == '}' && J.prof == 2 && lisible(J.cour) && s_nres < MAX_RES) {
                    bool double_ = false;                          // meme nom et meme debit : une seule fois
                    for (int k = 0; k < s_nres && !double_; k++) double_ = !strcmp(s_res[k].nom, J.cour.nom) && s_res[k].kbps == J.cour.kbps;
                    if (!double_) { s_res[s_nres] = J.cour; s_nres = s_nres + 1; }
                }
                J.prof--;
            }
            break;
        default:
            if (J.attend_val && !isspace((unsigned char)c) && J.nb < (int)sizeof J.brut - 1) J.brut[J.nb++] = c;
            break;
        }
    }
}
void resultats_fin(bool ok)
{
    s_cours = false;
    if (!ok && !s_nres) snprintf(s_msg, sizeof s_msg, "Annuaire injoignable");
    s_vres = s_vres + 1;
}

// ---------------------------------------------------------------- lecture
void jouer(const Station &s)
{
    s_station = s;
    s_titre[0] = 0;
    s_format[0] = 0;
    s_vtitre = s_vtitre + 1;
    snprintf(s_msg, sizeof s_msg, "Connexion…");
    s_etat = CONNEXION;
    s_gen = s_gen + 1;
    s_ouvrir = true;
}
void arreter()
{
    if (s_etat == ARRET) return;
    s_etat = ARRET;
    s_ouvrir = false;
    s_gen = s_gen + 1;
    s_msg[0] = 0;
    aud::vider();
}
bool actif() { return s_etat != ARRET; }
Etat etat() { return s_etat; }
const char *message() { return s_msg; }
const Station &en_cours() { return s_station; }
const char *titre() { return s_titre; }
uint32_t version_titre() { return s_vtitre; }
const char *format() { return s_format; }
bool a_ouvrir(char *url, size_t n, uint32_t *g)
{
    if (!s_ouvrir) return false;
    s_ouvrir = false;
    snprintf(url, n, "%s", s_station.url);
    *g = s_gen;
    return true;
}
bool continuer(uint32_t g) { return g == s_gen && s_etat != ARRET; }

// decodeurs
enum { C_AUCUN, C_MP3, C_AAC };
static int s_codec = C_AUCUN;
static mp3dec_t *s_mp3 = nullptr;
static HAACDecoder s_aac = nullptr;
static uint8_t *s_in = nullptr;          // octets compresses en attente
static size_t s_nin = 0;
static const size_t CAP_IN = 16384;
static int16_t *s_pcm = nullptr;         // une trame decodee (stereo, 2048 x 2 avec le SBR)
static int16_t *s_mono = nullptr;
static int s_hz = 0;
static int s_metaint = 0, s_avant_meta = 0, s_meta_reste = -1, s_meta_n = 0;
static char s_meta[512];
static uint32_t s_octets = 0, s_t0 = 0;
static bool s_essai_autre = false;      // rien de decode : on essaie l'autre format (annuaire ou serveur qui se trompe)

static bool ouvrir_codec(int c)
{
    s_codec = c;
    if (c == C_MP3) {
        if (!s_mp3) s_mp3 = (mp3dec_t *)plat_malloc_interne(sizeof(mp3dec_t));
        if (!s_mp3) return false;
        mp3dec_init(s_mp3);
        return true;
    }
    if (s_aac) AACFreeDecoder(s_aac);
    s_aac = AACInitDecoder();
    return s_aac != nullptr;
}

static void latin1_vers_utf8(const char *s, char *d, size_t n)
{
    // UTF-8 valide : on garde ; sinon on considere du Latin-1 (vieux serveurs Shoutcast)
    bool utf8 = true;
    for (const unsigned char *p = (const unsigned char *)s; *p && utf8; p++) {
        if (*p < 0x80) continue;
        int k = (*p & 0xE0) == 0xC0 ? 1 : ((*p & 0xF0) == 0xE0 ? 2 : ((*p & 0xF8) == 0xF0 ? 3 : -1));
        if (k < 0) { utf8 = false; break; }
        for (int j = 1; j <= k; j++) if ((p[j] & 0xC0) != 0x80) { utf8 = false; break; }
        p += k;
    }
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o < n - 3; p++) {
        if (utf8 || *p < 0x80) d[o++] = (char)*p;
        else { d[o++] = (char)(0xC0 | (*p >> 6)); d[o++] = (char)(0x80 | (*p & 0x3F)); }
    }
    d[o] = 0;
}
static void meta_fin()
{
    s_meta[s_meta_n] = 0;
    const char *p = strstr(s_meta, "StreamTitle='");
    if (!p) return;
    p += 13;
    const char *f = strstr(p, "';");
    if (!f) f = p + strlen(p);
    char t[200];
    size_t l = (size_t)(f - p) < sizeof t - 1 ? (size_t)(f - p) : sizeof t - 1;
    memcpy(t, p, l);
    t[l] = 0;
    char u[128];
    latin1_vers_utf8(t, u, sizeof u);
    if (strcmp(u, s_titre)) { snprintf(s_titre, sizeof s_titre, "%s", u); s_vtitre = s_vtitre + 1; }
}

static void sortir(const int16_t *pcm, int trames, int canaux, int hz)
{
    if (trames <= 0) return;
    if (hz != s_hz) {
        s_hz = hz;
        aud::debit_entree((float)hz);
        char k[16];
        snprintf(k, sizeof k, "%d,%d kHz", hz / 1000, (hz % 1000) / 100);
        int kb = s_station.kbps;
        const char *c = s_codec == C_MP3 ? "MP3" : (!strncmp(s_station.codec, "AAC", 3) ? s_station.codec : "AAC");
        if (kb) snprintf(s_format, sizeof s_format, "%s %d kb/s · %s", c, kb, k);
        else snprintf(s_format, sizeof s_format, "%s · %s", c, k);
    }
    const int16_t *src = pcm;
    if (canaux == 2) {
        for (int i = 0; i < trames; i++) s_mono[i] = (int16_t)(((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2);
        src = s_mono;
    }
    aud::pousser(src, trames);
    if (s_etat == CONNEXION) { s_etat = LECTURE; s_msg[0] = 0; }
}

static void decoder()
{
    if (s_codec == C_MP3) {
        const size_t MARGE = 2 * 1441 + 8;                 // minimp3 doit voir l'en-tete de la trame suivante
        size_t pos = 0;
        while (s_nin - pos >= MARGE) {
            mp3dec_frame_info_t fi;
            int n = mp3dec_decode_frame(s_mp3, s_in + pos, (int)(s_nin - pos), s_pcm, &fi);
            if (fi.frame_bytes == 0) break;
            pos += (size_t)fi.frame_bytes;
            if (n > 0) sortir(s_pcm, n, fi.channels, fi.hz);
        }
        if (pos) { memmove(s_in, s_in + pos, s_nin - pos); s_nin -= pos; }
    } else if (s_codec == C_AAC) {
        unsigned char *p = s_in;
        int reste = (int)s_nin;
        while (reste > 2100) {                             // au moins une trame ADTS entiere
            int off = AACFindSyncWord(p, reste);
            if (off < 0) { reste = 0; break; }
            p += off; reste -= off;
            int avant = reste;
            int err = AACDecode(s_aac, &p, &reste, s_pcm);
            if (err == ERR_AAC_INDATA_UNDERFLOW) { break; }
            if (err) { if (reste == avant) { p++; reste--; } continue; }   // trame abimee : on passe
            AACFrameInfo fi;
            AACGetLastFrameInfo(s_aac, &fi);
            if (fi.nChans > 0) sortir(s_pcm, fi.outputSamps / fi.nChans, fi.nChans, fi.sampRateOut);
        }
        size_t lu = (size_t)(p - s_in);
        if (lu) { memmove(s_in, s_in + lu, s_nin - lu); s_nin -= lu; }
    }
}

void flux_debut(const char *type, int metaint, const char *icy_nom)
{
    if (!s_in) s_in = (uint8_t *)plat_malloc_ps(CAP_IN);
    if (!s_pcm) s_pcm = (int16_t *)plat_malloc_interne(2048 * 2 * sizeof(int16_t));
    if (!s_mono) s_mono = (int16_t *)plat_malloc_interne(2048 * sizeof(int16_t));
    s_nin = 0;
    s_hz = 0;
    s_metaint = metaint > 0 ? metaint : 0;
    s_avant_meta = s_metaint;
    s_meta_reste = -1;
    s_octets = 0;
    s_t0 = plat_ms();
    // le format : d'apres l'annonce du serveur, sinon d'apres l'annuaire
    bool aac = (type && (strstr(type, "aac") || strstr(type, "aacp") || strstr(type, "mp4"))) ||
               (!(type && strstr(type, "mpeg")) && (!strncmp(s_station.codec, "AAC", 3)));
    s_essai_autre = false;
    bool ok = ouvrir_codec(aac ? C_AAC : C_MP3);
    if (!s_in || !s_pcm || !s_mono || !ok) {
        snprintf(s_msg, sizeof s_msg, "Mémoire insuffisante");
        s_etat = ERREUR;
        s_codec = C_AUCUN;
        return;
    }
    aud::vider();
    if (icy_nom && icy_nom[0] && !s_station.nom[0]) snprintf(s_station.nom, sizeof s_station.nom, "%s", icy_nom);
    snprintf(s_msg, sizeof s_msg, "Mise en réserve…");
}

void flux_octets(const uint8_t *b, size_t n)
{
    if (s_codec == C_AUCUN) return;
    s_octets += n;
    while (n > 0) {
        if (s_meta_reste >= 0) {                           // dans un bloc de metadonnees ICY
            size_t k = (size_t)s_meta_reste < n ? (size_t)s_meta_reste : n;
            for (size_t i = 0; i < k; i++) if (s_meta_n < (int)sizeof s_meta - 1) s_meta[s_meta_n++] = (char)b[i];
            b += k; n -= k;
            s_meta_reste -= (int)k;
            if (s_meta_reste == 0) { meta_fin(); s_meta_reste = -1; s_avant_meta = s_metaint; }
            continue;
        }
        if (s_metaint && s_avant_meta == 0) {              // l'octet de longueur
            s_meta_reste = b[0] * 16;
            s_meta_n = 0;
            b++; n--;
            if (s_meta_reste == 0) { s_meta_reste = -1; s_avant_meta = s_metaint; }
            continue;
        }
        size_t k = n;
        if (s_metaint && (size_t)s_avant_meta < k) k = (size_t)s_avant_meta;
        if (k > CAP_IN - s_nin) k = CAP_IN - s_nin;
        memcpy(s_in + s_nin, b, k);
        s_nin += k;
        b += k; n -= k;
        if (s_metaint) s_avant_meta -= (int)k;
        if (s_nin > CAP_IN - 4096 || n > 0) decoder();
        if (s_nin >= CAP_IN) s_nin = 0;                     // flux illisible : on repart
    }
    decoder();
    if (!s_hz && s_octets > 24000) {                       // toujours rien : l'autre format, une fois
        if (s_essai_autre) { snprintf(s_msg, sizeof s_msg, "Format non lu"); s_etat = ERREUR; s_codec = C_AUCUN; return; }
        s_essai_autre = true;
        s_octets = 0;
        if (!ouvrir_codec(s_codec == C_MP3 ? C_AAC : C_MP3)) { s_codec = C_AUCUN; return; }
    }
}

void flux_fin(const char *erreur)
{
    if (s_etat == ARRET) return;
    if (erreur) {
        snprintf(s_msg, sizeof s_msg, "%s", erreur);
        s_etat = ERREUR;
    } else {                                               // le serveur a coupe : on reconnecte
        snprintf(s_msg, sizeof s_msg, "Reconnexion…");
        s_etat = CONNEXION;
        s_ouvrir = true;
    }
}

}  // namespace wr
