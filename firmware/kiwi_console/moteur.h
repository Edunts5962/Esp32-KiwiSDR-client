// moteur.h - travail de fond (tache "moteur") : decodeurs et enregistrement
// WAV, nourris par le son brut du KiwiSDR. Tout ce qui ecrit sur la carte SD
// passe par ici (une seule tache touche aux fichiers).
#pragma once
#include <stdint.h>

namespace mot {

void init();
uint32_t etape(uint32_t ms);          // a appeler en boucle ; renvoie l'attente conseillee (ms)

// enregistrement du son (WAV mono 16 bits au debit du KiwiSDR)
void enregistrer(bool oui);
bool enregistre();
// enregistrement a la voix : seuls les passages ou il y a un signal sont ecrits ; un journal texte
// (/kiwi/rec_....txt : heure, frequence, duree de chaque passage) est ecrit a la fin
void voix(bool oui);                  // mode choisi avant de lancer l'enregistrement
bool voix();
void signal_present(bool oui, const char *quoi);   // a chaque image : y a-t-il un signal ? quoi : "145,500 MHz FM"
uint32_t duree_ecrite_s();            // son reellement ecrit
uint32_t duree_s();

// sauvegarde du decodeur courant (texte ou image)
void sauver_decodage();

// dernier compte rendu a afficher ("Enregistré : /kiwi/...") ; vide si rien de neuf
bool compte_rendu(char *s, int n);

}  // namespace mot
