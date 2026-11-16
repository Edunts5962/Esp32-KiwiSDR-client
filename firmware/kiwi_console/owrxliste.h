// owrxliste.h - liste des recepteurs OpenWebRX qui captent au-dessus de 30 MHz (VHF / UHF)
//
// Source : la carte de ReceiverBook (www.receiverbook.de/map?type=openwebrx&band=fr-vhf, puis fr-uhf).
// La page contient la liste en JSON ("var receivers = [...]") ; elle est lue au fil du
// telechargement, un recepteur a la fois. La liste est aussi gardee sur la carte SD
// (/kiwi/vhf.txt) : elle reste disponible si le site change ou ne repond pas.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace owl {

struct RecepteurVHF {
    char nom[56];
    char url[96];          // "https://hote:port/chemin/"
    float lat, lon;
    uint16_t km;           // distance depuis la position reglee (65000 = inconnue)
    uint8_t bandes;        // 1 VHF (30-300 MHz), 2 UHF (300 MHz - 3 GHz)
};

const int MAX_VHF = 600;
extern const char *const URL_VHF;     // les deux pages a telecharger
extern const char *const URL_UHF;

// telechargement (tache reseau) : debut, morceaux de chaque page, fin
void debut_liste();
void debut_page(uint8_t bande);       // 1 = VHF, 2 = UHF
void octets(const char *d, size_t n);
void fin_liste(bool ok, const char *raison = nullptr);
void charger_sd();                    // au demarrage
int nb();
const char *etat_liste();
void recalculer_distances();

// recherche (nom, adresse ; sans majuscules ni accents) triee par distance ; texte vide = tous
int chercher(const char *texte, const RecepteurVHF **sortie, int nmax);

// analyse de texte (tests) : un objet JSON de la carte -> recepteurs ajoutes
int lire_objet(const char *obj, uint8_t bande);

}  // namespace owl
