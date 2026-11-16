// webradio.h - radios sur Internet : recherche dans l'annuaire libre radio-browser.info, lecture des flux
// MP3 (minimp3) et AAC / HE-AAC (decodeur Helix de RealNetworks), titre du morceau (metadonnees ICY), favoris.
// La tache reseau de la plateforme ouvre les flux et passe les octets ici ; le son va dans la meme reserve
// que celui des recepteurs (volume, tonalite, limiteur, anti-coupures).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace wr {

struct Station {
    char nom[56];
    char url[200];
    char codec[8];           // "MP3", "AAC", "AAC+"
    uint16_t kbps;
    char pays[3];            // code ISO (FR, BE...)
    char genres[48];
};

enum Etat { ARRET = 0, CONNEXION, LECTURE, ERREUR };

void init();

// ---- recherche (annuaire radio-browser.info)
void chercher(const char *nom, const char *pays, const char *genre);   // vides : pas de filtre
bool recherche_en_cours();
int nb_resultats();
const Station &resultat(int i);
const char *recherche_texte();                // ce qui a ete cherche (pour l'affichage)
uint32_t version_resultats();
// pour la plateforme
bool requete(char *url, size_t n);           // une recherche a faire ? (URL a telecharger)
void resultats_debut();
void resultats_octets(const char *b, size_t n);
void resultats_fin(bool ok);

// ---- lecture
void jouer(const Station &s);
void arreter();
bool actif();                                 // une station est demandee (connexion ou lecture)
Etat etat();
const char *message();
const Station &en_cours();
const char *titre();                          // titre du morceau (ICY), vide sinon
uint32_t version_titre();
const char *format();                         // ex. "AAC+ 64 kb/s · 44,1 kHz"
// pour la plateforme
bool a_ouvrir(char *url, size_t n, uint32_t *generation);   // une station a ouvrir ?
bool continuer(uint32_t generation);          // faux : la station a change ou on arrete
void flux_debut(const char *type_contenu, int metaint, const char *icy_nom);
void flux_octets(const uint8_t *b, size_t n);
void flux_fin(const char *erreur);            // nullptr : fin normale (on reconnecte)

// ---- favoris (NVS "wrfav")
const int MAX_FAV = 16;
int nb_favoris();
const Station &favori(int i);
bool est_favori(const char *url);
void basculer_favori(const Station &s);
void retirer_favori(int i);

// pays et genres proposes sur l'ecran (sans clavier)
const int NB_RAPIDES = 6;
extern const char *const RAPIDES_NOM[NB_RAPIDES];

}  // namespace wr
