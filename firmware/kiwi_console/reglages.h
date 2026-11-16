// reglages.h - reglages enregistres en memoire flash (NVS)
#pragma once
#include <stdint.h>

#define KIWI_VERSION "v0.18.1-BETA"

namespace cfg {

enum Mode { AM = 0, LSB, USB, CW, FM, WFM, NB_MODES };   // WFM : FM large (OpenWebRX seulement)
enum Proto { KIWI = 0, OWRX = 1 };                        // type de recepteur

struct Reglages {
    uint16_t version;
    uint8_t volume;          // 0..100
    uint8_t luminosite;      // 5..100
    uint8_t agc;             // 0 off, 1 lent, 2 moyen, 3 rapide
    uint8_t wf_vitesse;      // 1..4
    uint8_t palette;         // 0..3
    uint8_t adpcm;           // audio compresse
    uint8_t ampli_bas;       // ampli du haut-parleur actif a l'etat bas
    uint8_t zoom;            // 0..14
    uint8_t mode;            // Mode
    uint8_t pas;             // index dans PAS_HZ
    uint16_t tampon_ms;      // reserve audio visee
    uint32_t bp[NB_MODES];   // largeur de bande par mode (Hz)
    double freq_khz;         // derniere frequence
    float lat, lon;          // position de l'auditeur (distances)
    char hote[64];           // dernier recepteur
    uint16_t port;
    char nom[48];
    char mdp[24];
    char url_liste[96];      // adresse de la liste publique
    // v0.2 : traitements faits par le KiwiSDR (ajoutes a la fin : les anciens reglages restent lisibles)
    uint8_t nr;              // reduction de bruit : 0 off, 1 douce (LMS), 2 forte (spectrale)
    uint8_t notch;           // anti-sifflement (auto-notch)
    uint8_t nb;              // anti-parasites (noise blanker) : 0 arret, 1 faible, 2 moyen, 3 fort
    uint8_t limiteur;        // limiteur de volume sur la carte
    // v0.3
    uint8_t etiquettes;      // noms des stations sur le waterfall : 0 non, 1 EiBi (en ondes), 2 communaute, 3 proprietaire
    // v0.9 (reglages version 6) : recepteurs OpenWebRX (VHF/UHF)
    uint8_t proto;           // Proto du dernier recepteur
    uint8_t chiffre;         // https (wss)
    char chemin[48];         // chemin de base de l'adresse ("/" ou "/sdr/")
    char profil[64];         // bande choisie (OpenWebRX : "sdr|profil")
};

const double FREQ_MAX_KHZ = 6000000.0;   // 6 GHz (OpenWebRX) ; les KiwiSDR s'arretent vers 30 MHz

int pas_par_defaut(int mode, double khz = 0);   // index dans PAS_HZ adapte au mode (et a la frequence)

extern Reglages R;

void charger();
void sauver();               // ecrit si quelque chose a change (appel frequent possible)
void marquer();              // signale un changement a sauver plus tard

extern const int PAS_HZ[];   // pas d'accord
extern const int NB_PAS;
const char *nom_mode(int m);       // "LSB"
const char *nom_mode_kiwi(int m);  // "lsb"
void bande_passante(int mode, int largeur, int *bas, int *haut);
// En CW, la frequence affichee est celle de la station (sa porteuse) et on l'entend a NOTE_CW Hz : le filtre
// envoye au KiwiSDR est 300..700 Hz au-dessus de la frequence d'accord, qui est donc 0,5 kHz plus bas.
const int NOTE_CW = 500;
// meme filtre, mais par rapport a la frequence affichee (dessin du filtre, mesures dans le waterfall)
void bande_passante_ecran(int mode, int l, int *bas, int *haut);
// decalage du filtre (Hz), par mode, pour ecarter un brouilleur voisin sans changer la frequence ;
// pris en compte par bande_passante(). Pas enregistre : il revient a 0 au redemarrage.
extern int decal[NB_MODES];
int decal_max(int mode);              // 0 : pas de decalage dans ce mode (FM)
bool gele();
void geler();                         // plus aucune ecriture des reglages (restauration d'une sauvegarde, avant de redemarrer)
int bp_min(int mode);
int bp_max(int mode);
int bp_origine(int mode);        // bande passante d'origine du mode (Hz)

}  // namespace cfg
