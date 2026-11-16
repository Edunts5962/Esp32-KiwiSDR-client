// plateforme.h - ce que le coeur du programme demande au materiel.
// Implemente par kiwi_console.ino (carte ES3C28P) et par le simulateur PC
// (outils/simulateur/sim_main.cpp). Tout le reste (protocole KiwiSDR,
// audio, waterfall, decodeurs, ecrans) est identique sur les deux.
#pragma once
#include <stdint.h>
#include <stddef.h>

uint32_t plat_ms();
void *plat_malloc_ps(size_t n);
void plat_free_ps(void *p);              // libere ce que plat_malloc_ps a donne
void *plat_malloc_interne(size_t n);      // memoire interne (rapide) : decodeur MP3            // memoire en PSRAM (garde la memoire interne pour le WiFi et le TLS)                       // millisecondes depuis le demarrage
void plat_journal(const char *canal, const char *nom, const char *valeur);   // moniteur serie
void plat_luminosite(int pct);            // retroeclairage 5..100 %
void plat_volume(int pct);                // volume du codec ES8311, 0 = muet
void plat_ampli(bool actif_bas);          // niveau de la broche d'ampli du haut-parleur
void plat_etalonner();                    // relance l'etalonnage du tactile

bool plat_nvs_lire(const char *cle, void *buf, size_t n);
void plat_nvs_ecrire(const char *cle, const void *buf, size_t n);

// ---- reseau ----
enum { WIFI_AUCUN = 0, WIFI_CONNEXION, WIFI_OK, WIFI_POINT_ACCES };
int plat_wifi_etat();
const char *plat_wifi_nom();              // reseau (ou point d'acces) en cours
const char *plat_wifi_ip();               // adresse du portail web
const char *plat_wifi_mdp();              // mot de passe du point d'acces de la carte
int plat_wifi_rssi();                     // dBm, 0 si inconnu
void plat_wifi_oublier();                 // efface le WiFi enregistre, redemarre en point d'acces

// demande (non bloquante) de telechargement de la liste publique des KiwiSDR
void plat_liste_telecharger();
bool plat_liste_en_cours();
void plat_fm_liste_telecharger();         // liste des serveurs FM-DX (servers.fmdx.org)
bool plat_fm_liste_en_cours();
// bouton rotatif KY-040 (CLK IO2, DT IO14, SW IO21) : crans depuis le dernier appel, bouton appuye ;
// false si absent
bool plat_molette(int *crans, bool *appui);
void plat_vhf_liste_telecharger();        // recepteurs OpenWebRX VHF/UHF (receiverbook.de)
bool plat_vhf_liste_en_cours();

// ---- carte SD ----
bool plat_sd();
const char *plat_sd_etat();               // "29,7 Go libres" ou la raison de l'echec
bool plat_sd_formater();                  // formate en FAT32 (carte illisible), bloquant
// enregistrement : ouvre / ajoute / ferme un fichier sur la SD
bool plat_fichier_ouvrir(const char *chemin);
bool plat_fichier_ecrire(const void *d, size_t n);
bool plat_fichier_ecrire_a(size_t pos, const void *d, size_t n);   // reecrit (entete WAV/BMP)
void plat_fichier_fermer();
// petits fichiers texte de la carte SD (independants du fichier ci-dessus)
char *plat_texte_lire(const char *chemin, size_t max);   // malloc (a liberer par free), nullptr si absent
bool plat_texte_ecrire(const char *chemin, const char *d, size_t n);
const char *plat_raison_demarrage();      // "mise sous tension", "plantage du programme"...
int plat_noaa();                          // lecture de la propagation (NOAA) : 0 active, 2 coupee, 3 coupee apres un redemarrage en pleine lecture
void plat_noaa_regler(bool active);
size_t plat_nvs_taille(const char *cle);  // taille de ce qui est enregistre sous cette cle (0 : rien)
bool plat_texte_ajouter(const char *chemin, const char *d, size_t n);   // ajoute a la fin du fichier (carte SD)
uint32_t plat_epoch();                    // heure UTC (secondes depuis 1970, heure reseau) ; 0 si inconnue
uint64_t plat_epoch_ms();                 // la meme en millisecondes (FT8) ; 0 si inconnue
void plat_horodatage(char *s, size_t n);  // "20261108-0612" (heure reseau) ou compteur
