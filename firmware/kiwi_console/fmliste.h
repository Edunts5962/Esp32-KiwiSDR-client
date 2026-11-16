// fmliste.h - liste des recepteurs FM publics du reseau FM-DX Webserver
//
// Source : https://servers.fmdx.org/api/ (JSON d'environ 300 Ko, une seule ligne).
// Le JSON est analyse au fil du telechargement : seul un serveur a la fois
// est garde en tampon, puis resume dans le tableau (PSRAM).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace fml {

struct ServeurFM {
    char nom[48];
    char ville[32];
    char pays[28];          // nom du pays
    char code[4];           // "fr", "de"...
    char url[100];          // "https://paris-headlesstef.example.com/"
    char plage[24];         // "87.5 - 108 MHz" (peut etre vide)
    float lat, lon;
    uint16_t km;            // distance depuis la position reglee (65000 = inconnue)
    uint16_t kbps;          // qualite du son MP3
    uint8_t etat;           // 1 disponible, 2 verrouille (ecoute seule), 0 injoignable
};

const int MAX_FM = 800;
extern const char *const URL_LISTE;

void debut_liste();
void octets(const char *d, size_t n);       // morceau du JSON telecharge
void fin_liste(bool ok);
void echec(const char *raison);             // telechargement impossible : raison affichee
void noter_incomplete();                     // le telechargement a ete coupe (attente trop longue) : liste peut-etre partielle
int nb();
const char *etat_liste();
void recalculer_distances();

// recherche (nom, ville, pays ; sans majuscules ni accents) ; texte vide = tous.
// Tri : disponibles et verrouilles par distance, injoignables a la fin.
int chercher(const char *texte, const ServeurFM **sortie, int nmax);
const ServeurFM *serveur_url(const char *url);

// petits outils JSON (valeur d'une cle dans un texte JSON ; chaines decodees en UTF-8)
void json_chaine(const char *json, const char *cle, char *dst, size_t n);
double json_nombre(const char *json, const char *cle, double defaut);
bool json_vrai(const char *json, const char *cle);

// "https://hote:port/chemin/" -> hote, port, chiffre, chemin de base ("/chemin/")
bool analyser_url(const char *url, char *hote, size_t nh, uint16_t *port, bool *tls, char *base, size_t nb);

}  // namespace fml
