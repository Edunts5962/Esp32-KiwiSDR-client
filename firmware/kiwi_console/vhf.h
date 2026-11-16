// vhf.h - canaux VHF/UHF a ecouter : frequences communes (aviation, marine, radioamateurs, PMR446)
// et frequences des aerodromes (OurAirports, domaine public), triees par distance au recepteur.
//
// Aerodromes : liste d'Europe integree au firmware (aero_liste.h) ; si la carte SD contient
// /kiwi/aero.txt (liste du monde entier, dossier carte_sd du projet), c'est elle qui est utilisee.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace vhf {

enum { AVIA = 1, MARINE = 2, RADIOAM = 4, PMR = 8, TOUS = 15 };

struct Canal {
    double khz;
    float lat, lon;         // aerodrome ; 999 pour les canaux communs
    uint16_t km;            // distance a la position de reference (0 pour les canaux communs)
    uint8_t cat;            // AVIA, MARINE...
    uint8_t mode;           // cfg::Mode
    char nom[40];           // "LFRS Nantes Atlantique Airport" ou "Canal 16"
    char type[24];          // "Tour", "ATIS"... ou "détresse et appel"
};

const float KM_AVIA = 200;  // aerodromes au-dela : pas proposes

void init();                                    // charge la liste (carte SD sinon integree)
const char *source();                           // "Europe (intégrée)" ou "carte SD (monde)"
int nb();                                       // canaux en memoire (communs + aerodromes proches)
uint32_t version();                             // change quand la liste est refaite : les pointeurs de chercher() sont a redemander
// position de reference : le recepteur ecoute (sinon la position reglee) ; recalcule distances et ordre
void reference(float lat, float lon);
bool reference_changee(float lat, float lon);   // true si la position differe nettement de la reference
// canaux des categories demandees : communs d'abord, puis aerodromes a moins de KM_AVIA, du plus proche
// au plus lointain ; texte : filtre (nom, type, frequence), sans majuscules ni accents
int chercher(int cats, const char *texte, const Canal **sortie, int nmax);
const char *nom_categorie(int cat);


}  // namespace vhf
