// decodeurs.h - decodeurs embarques : morse (CW), telex radio (RTTY), fac-simile
// meteo (FAX). Ils lisent le son brut du KiwiSDR (~12 kHz) dans la tache
// "moteur" ; l'interface lit leurs resultats.
#pragma once
#include <stdint.h>

namespace dec {

enum { CW = 0, RTTY, FAX, FT8, NB };          // FT8 : voir ft8.h (decodage par periodes de 15 s)

void init();
void choisir(int m);
int choisi();
void actif(bool a);                         // decodage seulement sur l'ecran Decodeurs
void traiter(const int16_t *s, int n, float fs);

void effacer();
void caler();                               // recherche de la tonalite / nouvelle image
void regler_tonalite(float hz);             // choix a la main (toucher l'apercu)
void inverser();
bool inverse();
void vitesse_suivante();                    // RTTY : 45,45 / 50 / 75 bauds
void shift_suivant();                       // RTTY : 170 / 425 / 450 / 850 Hz

const char *texte();                        // derniers caracteres decodes
uint32_t version_texte();
const char *info1();                        // ex. "18 mots/min"
const char *info2();                        // ex. "Tonalité 700 Hz"
bool cale();                                // signal accroche
float tonalite_hz();                        // CW : note ; RTTY : centre ; FAX : 1900

// apercu du son (0..3 kHz) : lignes de SP_N niveaux 0..255
const int SP_N = 128;
const int SP_H = 48;
const uint8_t *spectre(int age);
float hz_par_point();

// FAX : image en niveaux de gris, FAX_L points par ligne
const int FAX_L = 320;
const int FAX_H = 480;
const uint8_t *fax_ligne(int age);
int fax_nb_lignes();
void fax_decaler(int px);

bool sauver(char *nom, int n);              // sur la SD : texte ou image BMP

}  // namespace dec
