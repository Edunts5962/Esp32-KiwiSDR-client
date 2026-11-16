// interface.h - ecrans tactiles du client KiwiSDR (320 x 240)
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ui {

void init();
void second_doigt(bool present, int x, int y);   // a appeler avant toucher() (pincement : bande passante)
void toucher(bool appui, int x, int y);   // a chaque image : etat du doigt
void image(uint32_t ms);                  // dessine une image complete dans gfx::fb
void message(const char *texte);          // bandeau temporaire

// ---- pour le portail web (autre tache) ----
// les ordres sont mis en file et executes par l'interface a l'image suivante
void commande(const char *ordre);
bool suit_page_web();                         // l'ecran suit les onglets de la page web
// "Quoi écouter" : suggestions de la categorie (quoi::OC...), triees comme a l'ecran ; contexte : "Soir · Twente" ;
// fait / total : avancement de la mesure en cours (0 / 0 sinon)
struct QuoiWeb { char nom[40]; char detail[64]; char etat[24]; double a, b; uint8_t genre, niveau, dispo, mesure; };
int quoi_web(int cat, QuoiWeb *dst, int nmax, char *contexte, size_t nc, int *fait, int *total);
// v0.14 : tonalite (dB), mode nuit (0 arret, 1 = 1 min, 2 = 5 min, 3 = 15 min)
void options2(int *graves, int *aigus, int *nuit, int *cwtxt);   // cwtxt : texte morse sur l'ecran Radio en CW
// journal d'ecoute : les dernieres ecoutes (la plus recente d'abord) ; cours : l'ecoute en cours (f = 0 s'il n'y en a pas)
struct EcouteWeb { char quand[16]; double f; uint8_t mode; uint16_t duree_s; int16_t rssi; char rx[32]; char quoi[32]; };
int journal_web(EcouteWeb *dst, int nmax, EcouteWeb *cours);
void options(int *squelch, int *sam, int *reprise, int *vox, int *muet);   // v0.13 : squelch 0..3, AM synchrone, reprise auto, enregistrement a la voix ; muet : le squelch coupe le son
int signal_bruit();
int ft8_bande_web();                          // bande FT8 ecoutee (-1 : aucune)                           // v0.16 : rapport signal/bruit en dB (-1 inconnu)
int nb_favoris();
bool favori(int i, double *khz, int *mode, char *nom, int nn, char *hote, int nh);
int nb_presets();
const char *preset(int i, double *khz, int *mode);
float images_par_seconde();
void scan_etat(int *sens, int *ecoute, int *reste_s, int *sensibilite);   // autoscan (sens 0 = arret)
int scan_vitesse();                           // 1..4 pendant un scan, 0 sinon
bool scan_liste(char *canal, int n);
int canal_etat(double khz);                   // canal VHF : 2 dans la bande en cours, 1 autre bande du recepteur, 0 pas capte
int scan_fm(int *vitesse, int *reste_s);       // autoscan FM : sens (0 = arret), vitesse, secondes d'ecoute restantes
int fm_trouvees(float *mhz, float *sig, char (*ps)[10], int nmax);   // stations trouvees par l'autoscan FM          // scan de la liste des canaux VHF en cours (canal ecoute ou nombre)

}  // namespace ui
