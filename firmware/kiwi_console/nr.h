// nr.h - reduction de bruit faite par la carte (son des recepteurs OpenWebRX, qui n'en ont pas)
// Methode : spectre glissant (256 points, recouvrement de moitie), bruit estime en continu
// (minimum suivi par bande de frequence), gain de Wiener "dirige par la decision" (peu de bruit musical).
// Retard ajoute : 256 echantillons (21 ms a 12 kHz).
#pragma once
#include <stdint.h>

namespace nr {

void niveau(int n);                 // 0 arret, 1 douce, 2 forte
int niveau();
void raz();                         // nouvelle frequence : le bruit est re-appris (sans coupure du son)
void vider();                       // nouvelle connexion : tout repart a zero
void traiter(int16_t *x, int n);    // sur place (meme nombre d'echantillons en sortie)

}  // namespace nr
