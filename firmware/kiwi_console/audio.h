// audio.h - reserve audio entre le reseau (KiwiSDR, ~12 kHz) et le codec
// (48 kHz stereo). Un seul producteur (tache reseau), un seul consommateur
// (tache I2S) : pas de verrou. Le reechantillonnage suit le debit reel du
// KiwiSDR et corrige doucement la derive pour garder la reserve stable.
#pragma once
#include <stdint.h>

namespace aud {

const int FS_SORTIE = 48000;

void init();
void vider();                                   // nouvelle connexion : on repart a zero
void debit_entree(float hz);                    // "sample_rate" annonce par le KiwiSDR
float debit();
void pousser(const int16_t *s, int n);          // echantillons recus (tache reseau)
void lire_sortie(int16_t *stereo, int trames);  // tache I2S : remplit toujours (silence si vide)
void reserve_visee(int ms);                     // reserve choisie (Reglages) : le minimum
int reserve_auto_ms();                          // ce que la reserve a ajoute d'elle-meme (reseau irregulier)
int reserve_ms();                               // ce qui attend d'etre joue
bool en_lecture();                              // false pendant la mise en reserve
uint32_t coupures();                            // nombre de manques (reseau trop lent)
int niveau_crete();                             // 0..100, vu-metre
void limiteur(bool oui);
void tonalite(int graves_db, int aigus_db);     // -8 .. +8 dB ; graves : sous 250 Hz, aigus : au-dessus de 2,5 kHz
void silence(bool oui);                         // squelch : coupe le haut-parleur en douceur (le son brut n'est pas touche)
bool silence();
void gain_kiwi(int niveau);      // 0 normal, 1 fort (+6 dB), 2 tres fort (+12 dB) ; son du KiwiSDR seulement
int gain_kiwi();                        // baisse automatiquement les passages trop forts

// lecture des echantillons bruts (decodeurs, enregistrement) : chaque
// lecteur garde son curseur. Renvoie le nombre lu (0 si rien de neuf).
uint32_t curseur_brut();
int lire_brut(uint32_t &curseur, int16_t *dst, int nmax);

}  // namespace aud
