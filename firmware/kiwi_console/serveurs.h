// serveurs.h - liste des recepteurs KiwiSDR : liste publique + recepteurs perso
//
// La liste publique est lue ligne par ligne pendant le telechargement
// (jamais entierement en memoire). Deux formats sont compris :
//   - le fichier de la carte rx.linkfanel.net (kiwisdr_com.js) : "cle":"valeur"
//   - la page kiwisdr.com/public/ : <!-- cle=valeur --> puis l'adresse en lien
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace srv {

enum Region { PERSO = 0, PROCHES, EUROPE, AMERIQUES, ASIE, OCEANIE, AFRIQUE, NB_REGIONS };

struct Serveur {
    char nom[48];
    char lieu[40];
    char hote[64];
    char mdp[24];
    uint16_t port;
    float lat, lon;
    uint16_t km;
    uint8_t users, umax;
    uint8_t region;
    uint8_t perso;          // ajoute a la main (portail web)
};

const int MAX_PUBLICS = 900;
const int MAX_PERSO = 12;

void init();
const char *nom_region(int r);

// recepteurs perso (portail web), enregistres en NVS
int nb_perso();
const Serveur *perso(int i);
bool ajouter_perso(const char *nom, const char *adresse, const char *mdp);
void retirer_perso(int i);

// liste publique : alimentee par la tache reseau
void debut_liste();
void ligne_liste(const char *l);
void fin_liste(bool ok);
int nb_publics();
uint32_t age_liste_ms();        // depuis le dernier telechargement reussi (0xFFFFFFFF = jamais)
const char *etat_liste();
void noter_etat(const char *t);  // message libre (raison d'un echec, copie de la carte SD...)

// vue filtree et triee par distance (interface)
int filtrer(int region, const Serveur **sortie, int nmax);
void recalculer_distances();    // apres changement de position

// recherche dans toutes les regions (nom, lieu, adresse) ; plusieurs mots = tous presents ;
// sans tenir compte des majuscules ni des accents. Trie comme filtrer().
int chercher(const char *texte, const Serveur **sortie, int nmax);

// recepteur public libre le plus proche d'un point (emetteur d'une station), nullptr si aucun
const Serveur *plus_proche_libre(float lat, float lon, uint16_t *km, const char *sauf_hote = nullptr);

// outils : minuscules sans accents (recherche) ; distance en km
void plier(const char *src, char *dst, size_t n);
uint16_t distance_km(float la1, float lo1, float la2, float lo2);

// "http://hote:8073/" -> hote, port
bool analyser_adresse(const char *a, char *hote, size_t nh, uint16_t *port);

}  // namespace srv
