// portail.h - portail web embarque (telephone ou PC, meme reseau que la carte)
//   - WiFi de la maison (si la carte est en point d'acces "KiwiSDR-ES3C28P")
//   - position de l'auditeur (pour trier les recepteurs par distance)
//   - recepteurs perso (adresse + mot de passe eventuel), ecoute directe
//   - adresse de la liste publique
#pragma once
#include <WebServer.h>

void portail_init(WebServer &s);
void plat_wifi_enregistrer(const char *ssid, const char *pass);   // dans kiwi_console.ino
