// cascade.h - waterfall (cascade) et spectrum du KiwiSDR, prets a afficher
// Chaque ligne recue (1024 points de 0 a 255) est reduite a la largeur de
// l'ecran, mise en couleur avec un contraste automatique, et rangee dans un
// anneau de lignes RGB565.
#pragma once
#include <stdint.h>

namespace wf {

const int L = 320;           // largeur affichee
const int H = 120;           // lignes gardees (l'ecran en montre moins)

void init();
void ligne(const uint8_t *bins, int n);    // une ligne du KiwiSDR (tache reseau)
void decaler(int px);                      // accord : l'image glisse (px > 0 vers la droite)
void effacer();
void palette(int p);                       // 0..3
uint16_t couleur(int p, int v);            // palette p, niveau 0..255 (apercus)

// lecture pour l'affichage
const uint16_t *rangee(int age);           // age 0 = ligne la plus recente
const float *spectre();                    // L valeurs en dB (lissees)
float plancher_db();                       // niveau de bruit estime
uint32_t compteur();                       // lignes recues depuis le debut

}  // namespace wf
