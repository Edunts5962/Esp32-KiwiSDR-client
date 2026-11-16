// ft8.h - decodeur FT8 embarque (reception seulement), sur la bibliotheque ft8_lib (Karlis Goba, licence MIT).
// Le son du recepteur (USB, 200 a 3000 Hz) est range par periodes de 15 s calees sur l'heure UTC (NTP) ;
// a la fin de chaque periode, une tache a part la decode (1 a 3 s sur l'ESP32-S3) pendant que la suivante s'enregistre.
#pragma once
#include <stdint.h>

namespace f8 {

struct Message {
    uint32_t periode;        // heure UTC du debut de la periode (secondes)
    int8_t snr;              // dB dans 2500 Hz (comme WSJT-X)
    int8_t dt10;             // decalage en dixiemes de seconde
    uint16_t hz;             // frequence audio
    uint8_t cq;              // 1 : appel general (CQ)
    char texte[36];          // message tel quel
    char indicatif[14];      // station qui emet (si on la reconnait)
    char loc[7];             // locator (4 caracteres) s'il y en a un
    float lat, lon;          // centre du carre locator (si loc)
    uint16_t km;             // distance au recepteur (0 : inconnue)
    const char *pays;        // d'apres le prefixe de l'indicatif (approximatif)
};

void init();
void actif(bool a);                          // enregistre et decode seulement quand on s'en sert
bool actif();
void traiter(const int16_t *s, int n, float fs);   // son brut du recepteur (tache moteur)
void position(float lat, float lon);         // position du recepteur, pour les distances

void tache_un_tour();                        // a appeler en boucle par une tache a part (decodage)

int nb();                                    // messages gardes (les plus recents d'abord)
const Message &message(int i);
uint32_t version();                          // change a chaque nouvelle periode decodee
void effacer();
int secondes_avant();                        // avant la fin de la periode en cours (-1 : heure inconnue)
int derniers();                              // messages decodes dans la derniere periode
uint32_t duree_ms();                         // temps du dernier decodage
bool heure_ok();

// coeur, utilise aussi par le banc d'essai : decode 15 s de son a 12 kHz
int decoder(const int16_t *x, int n, uint32_t periode, Message *sortie, int max);

// utilitaires
bool locator(const char *loc, float *lat, float *lon);
const char *pays(const char *indicatif);

// frequences FT8 habituelles (kHz)
const int NB_BANDES = 11;
extern const int BANDES_KHZ[NB_BANDES];
extern const char *const BANDES_NOM[NB_BANDES];

}  // namespace f8
