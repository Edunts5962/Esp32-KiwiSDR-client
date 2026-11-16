// stations.h - annuaire de stations de radio (liste integree + fichier sur la carte SD)
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace sta {

struct Station {
    char nom[44];
    char pays[24];
    char langue[4];         // "en", "fr", "zh"... ou "--" (signal sans parole)
    char info[60];          // horaires UTC ou description
    float khz;
    float lat, lon;         // emetteur ; 999 = inconnu
    uint8_t mode;           // cfg::Mode
};

const int MAX_STATIONS = 400;
extern const char *const FICHIER;   // "/kiwi/stations.csv"

// charge la liste : fichier de la carte SD s'il existe, sinon liste integree
// (et la copie sur la SD si la carte est presente). Renvoie le nombre de stations.
int charger();
// remplace le fichier de la SD par la liste integree, puis recharge
int reinitialiser();

int nb();
const Station *station(int i);
bool position_connue(const Station &s);
const char *source();               // "carte SD" ou "liste intégrée"

// recherche (mots dans nom, pays, infos, langue ou frequence ; sans majuscules ni accents)
// filtre : "" toutes, "en", "fr", "zh", "--" signaux, "autres"
int chercher(const char *texte, const char *filtre, int *indices, int nmax);

// la liste au format CSV (pour la telecharger depuis la page web)
size_t ecrire_csv(char *dst, size_t n);

// analyse d'un texte CSV (exposee pour les tests)
int analyser(const char *texte, Station *dst, int nmax);

}  // namespace sta
