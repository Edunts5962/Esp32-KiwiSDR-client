// quoi.h - "Quoi écouter" : ce qu'il y a de plus interessant a ecouter maintenant
//
// Deux niveaux :
//  - proposer() : une estimation calculee, sans rien capter. Elle part de l'heure, de la saison et de la
//    position du recepteur (hauteur du soleil) : les bandes basses portent loin la nuit, les bandes hautes
//    le jour ; en VHF/UHF, ce sont surtout les habitudes (aviation le jour, radioamateurs le soir).
//  - compter() : la mesure. On lui donne un spectre (waterfall du KiwiSDR ou spectre de bande OpenWebRX)
//    et elle compte les signaux qui depassent le bruit dans une plage de frequences.
// Ce fichier ne depend pas du materiel : il tourne aussi dans le simulateur PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace quoi {

enum { OC = 0, VHF = 1, UHF = 2, TOUT = 3 };                      // categories
enum { RADIOAM = 0, RADIODIF, UTILITAIRE, AVIATION, MARINE, TALKIE, RADIO_FM };   // genre (couleur du point)

struct Sugg {
    char nom[40];           // "40 m · radioamateurs"
    char detail[64];        // "7 000 – 7 200 kHz · LSB · longue distance"
    char etat[20];          // "très actif", "continu", "fermée"...
    double a, b;            // plage en kHz (a == b : une seule frequence)
    double f;               // frequence a regler
    float larg;             // largeur habituelle d'un signal (kHz), pour les compter
    uint8_t mode;           // cfg::Mode
    uint8_t genre;
    uint8_t cat;            // OC, VHF ou UHF
    uint8_t niveau;         // 0 (rien a attendre) .. 5 (tres actif)
    uint8_t continu;        // emission en boucle (ATIS, VOLMET, radio FM)
};

struct Contexte {
    uint32_t epoch;         // heure UTC en secondes depuis 1970 ; 0 = inconnue
    float lat, lon;         // position du recepteur ecoute
};

const int MAX_SUGG = 40;

// suggestions de la categorie, de la plus prometteuse a la moins prometteuse ; renvoie leur nombre
int proposer(const Contexte &c, int cat, Sugg *dst, int nmax);

// hauteur du soleil au-dessus de l'horizon (degres) et heure solaire (0..24) a cet endroit
float soleil(uint32_t epoch, float lat, float lon);
float heure_solaire(uint32_t epoch, float lon);
const char *moment(const Contexte &c);      // "Matin", "Jour", "Soir", "Nuit" ("" si l'heure est inconnue)

// Mesure : compte les signaux d'une plage [a, b] kHz dans un spectre de n points en dB ; le point i est a
// f0 + i * kpp kHz. Le bruit de fond est le 25e centile de tout le spectre ; un signal le depasse de
// seuil_db. Un signal plus large que larg kHz compte pour plusieurs. Renvoie le nombre de signaux
// (-1 si la plage n'est pas dans le spectre) ; f_fort = frequence du plus fort.
int compter(const float *db, int n, double f0, double kpp, double a, double b, float seuil_db, float larg,
            double *f_fort, float *db_au_dessus);
int niveau_mesure(int signaux);             // nombre de signaux -> 0..5

// Propagation du jour (NOAA, centre de prevision de la meteo spatiale) :
//   flux solaire a 10,7 cm : plus il est haut, mieux les bandes hautes (14 MHz et plus) passent ;
//   indice Kp (0..9) : a partir de 5, orage magnetique, les ondes courtes passent mal.
// La plateforme telecharge les deux fichiers (a_demander / reponse) toutes les 3 heures.
extern const char *const URL_FLUX;
extern const char *const URL_KP;
bool solaire_a_demander(uint32_t ms);       // true : telecharger URL_FLUX et URL_KP puis appeler solaire_reponse()
void solaire_reponse(const char *json_flux, const char *json_kp, uint32_t ms);
bool solaire(float *flux, float *kp);       // false si inconnu (valeur inconnue : -1)
void solaire_fixer(float flux, float kp);   // tests
bool lire_flux(const char *json, float *flux);   // analyse (exposee pour les tests)
bool lire_kp(const char *json, float *kp);

}  // namespace quoi
