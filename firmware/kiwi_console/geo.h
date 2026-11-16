// geo.h - position de la carte (tri des recepteurs par distance)
//
// Trois origines :
//   AUTO    par l'adresse Internet (ip-api.com, gratuit, en http) : precision de la ville, parfois 20 a 50 km
//   VILLE   nom de ville cherche dans geocoding-api.open-meteo.com (gratuit, sans cle)
//   MANUEL  latitude et longitude saisies sur la page web
// En AUTO la position est redemandee une fois par demarrage ; un choix VILLE ou MANUEL reste prioritaire.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace geo {

enum { AUTO = 0, VILLE = 1, MANUEL = 2 };

void init();                                // apres cfg::charger()
int mode();
const char *lieu();                         // "Lyon, France" ("" si inconnu)
void etiquette(char *dst, size_t n);        // "Lyon, France (par Internet)", "45,76 N 4,84 E (réglée à la main)"
const char *etat();                         // "", "Recherche…", "Ville introuvable"...
bool en_cours();
uint32_t version();                         // change quand la position change

void localiser();                           // par l'adresse Internet, maintenant (repasse en AUTO)
void chercher_ville(const char *nom);       // devient VILLE si elle est trouvee
void manuel(float lat, float lon);          // saisie de la latitude et de la longitude

// plateforme (tache reseau) : adresse a telecharger, puis reponse
bool a_demander(char *url, size_t n);       // true : telecharger url puis appeler reponse()
void reponse(int code_http, const char *json);

}  // namespace geo
