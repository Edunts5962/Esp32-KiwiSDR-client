// kiwi.h - client du protocole KiwiSDR (WebSocket), sans dependance materielle
//
// Deux connexions WebSocket vers le meme recepteur, avec le meme horodatage :
//   /<horodatage>/SND : le son (12 kHz, compresse IMA-ADPCM) + S-metre
//   /<horodatage>/W/F : le waterfall (1024 points par ligne)
// Les commandes sont des textes "SET ..." (voir kiwiclient, le client Python
// officiel : github.com/jks-prv/kiwiclient). Ce module fabrique les messages
// a envoyer et decode ceux qui arrivent ; kiwi_console.ino (ou le simulateur)
// s'occupe des sockets.
//
// Regle de partage : l'interface (coeur 1) n'appelle que connecter(),
// deconnecter(), regler*() et les lectures d'etat ; la tache reseau (coeur 0)
// appelle tout le reste.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace kw {

enum Etat { ARRET = 0, CONNEXION, ATTENTE, ACTIF, OCCUPE, REFUS, PANNE, PERDU };
enum Canal { SND = 0, WF = 1 };

void init();

// ---- cote interface ----
void connecter(const char *hote, uint16_t port, const char *mdp, const char *nom,
               int proto = 0, bool chiffre = false, const char *chemin = "/");   // proto : cfg::KIWI ou cfg::OWRX
int protocole();             // cfg::KIWI ou cfg::OWRX (recepteur demande)
void deconnecter();
void regler();               // frequence / mode / filtre (lus dans cfg::R)
void regler_cascade();       // zoom, centre, vitesse
// mesure des bandes ("Quoi écouter") : le waterfall regarde ailleurs, a la vitesse maximale, sans toucher au son
void cascade_forcer(float centre_khz, int zoom);
void cascade_liberer();
bool cascade_forcee();
void regler_agc();
void regler_compression();
void am_synchrone(int bande);  // KiwiSDR : AM synchrone, 0 non, 1 deux bandes ("sam"), 2 bande haute ("sau"), 3 bande basse ("sal")
bool am_synchrone();
int am_bande();
void agc_niveaux(int seuil_db, int gain_man);   // AGC : seuil (-130..-60 dB) ; sans AGC : gain fixe (0..120 dB)
int agc_seuil();
int gain_manuel();
void regler_bruit();         // reduction de bruit, anti-sifflement, anti-parasites

Etat etat();
const char *message();       // etat en toutes lettres
const char *nom();           // nom du recepteur
const char *hote();
uint16_t port();
float rssi_dbm();            // S-metre (dBm)
float cascade_centre_khz();  // centre du waterfall demande
float cascade_largeur_khz(); // largeur du waterfall (selon le zoom)
float bande_max_khz();       // 30 000 kHz en general
float bande_min_khz();       // decalage de frequence eventuel du recepteur
uint32_t debit_octets();     // octets recus par seconde
int limite_minutes();        // limite de session annoncee (0 = aucune)
bool surcharge_adc();

// ---- cote reseau ----
// generation : change a chaque demande de (de)connexion
uint32_t generation();
bool cible(char *hote, size_t nh, uint16_t *port, uint32_t *horodatage, bool *chiffre);   // false = rester deconnecte
// chemins WebSocket des canaux ; renvoie le nombre de canaux (KiwiSDR : 2 (SND, W/F) ; OpenWebRX : 1 ("/ws/"))
int chemins(char *ch0, char *ch1, size_t n);
bool rediriger(const char *url);                  // la plateforme a recu une redirection HTTP (301/302/307/308)
void ouvert(int canal);
void ferme(int canal);
void recu(int canal, const uint8_t *d, size_t n);
bool sortant(int canal, char *buf, size_t n);  // prochain message a envoyer
void tic(uint32_t ms);

// noms des stations (etiquettes du KiwiSDR : EiBi, communaute ou liste du proprietaire)
struct Marqueur { float khz; uint8_t mode; char nom[30]; };
const int MAX_MARQUEURS = 48;
int marqueurs(Marqueur *dst, int nmax);    // copie ; renvoie le nombre
uint32_t version_marqueurs();
void regler_etiquettes();
// journal de connexion (affiche a la place du waterfall pendant la connexion)
enum { J_COURS = 0, J_OK, J_ECHEC };
struct LigneJ { char t[56]; uint8_t e; };
const int MAX_J = 10;
int journal(LigneJ *dst, int nmax);          // les lignes, de la plus ancienne a la plus recente
bool journal_visible();                       // a afficher a la place du waterfall
void echec_ouverture(int canal, const char *raison);   // plateforme : WebSocket impossible a ouvrir
bool sans_cascade();                       // le recepteur n'envoie pas de waterfall a notre canal (trop d'auditeurs)
int canal_rx();
int canaux_cascade();
void diag_cascade(uint32_t *trames, uint32_t *taille, uint32_t *autres, uint8_t *debut8);   // diagnostic (remet a zero)

// recherche dans la base EiBi du recepteur connecte (emissions a l'antenne en ce moment).
// Le recepteur est interroge par tranches de frequences ; les resultats arrivent au fur et a mesure.
struct Eibi { float khz; uint8_t mode; char nom[40]; char note[40]; };
const int MAX_EIBI = 150;
enum { EIBI_RIEN = 0, EIBI_COURS, EIBI_FINI, EIBI_ERREUR };
bool eibi_chercher(const char *texte);       // false si pas connecte
int eibi_etat(int *avance_pct, const char **erreur);
int eibi_resultats(Eibi *dst, int nmax);

// ---- OpenWebRX : bandes (profils) proposees par le recepteur ----
struct Profil { char id[64]; char nom[40]; };
const int MAX_PROFILS = 32;
int profils(Profil *dst, int nmax);
int profil_courant();                        // index dans profils(), -1 si inconnu
void choisir_profil(const char *id);         // change de bande (pour tous les auditeurs de ce recepteur)
const char *nom_profil();                    // bande en cours ("" si aucune)
uint32_t version_profils();
bool plage_profil(const char *id, double *min_khz, double *max_khz);   // plage d'une bande deja visitee
// status.json du recepteur OpenWebRX : plage exacte de chaque bande (et position). La plateforme le telecharge
// une fois par connexion : statut_a_demander() donne l'adresse (true une seule fois), statut_json() le lit.
bool statut_a_demander(char *url, size_t n);
int statut_json(const char *j);                 // renvoie le nombre de bandes dont la plage est connue
uint32_t version_plages();                      // change quand une plage de bande est apprise
void pause_etiquettes(bool pause);              // autoscan : pas de demande d'etiquettes pendant le defilement
bool position_recepteur(float *lat, float *lon);                       // annoncee par le recepteur OpenWebRX
// spectre de toute la bande (OpenWebRX seulement) : NB_BANDE points en dB, de bande_min a bande_max ;
// renvoie le nombre de points (0 si indisponible) et le compteur de spectres recus
const int NB_BANDE = 2048;
int spectre_bande(const float **db, uint32_t *compte);

// decodeur IMA-ADPCM (public pour les tests)
struct Adpcm { int index = 0; int prec = 0; int16_t un(uint8_t code); };

}  // namespace kw
