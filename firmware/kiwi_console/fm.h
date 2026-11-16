// fm.h - client FM-DX Webserver : tuners FM partages sur Internet (TEF6686, XDR...)
//
// Deux WebSocket vers le serveur (ouverts par la plateforme, comme pour le KiwiSDR) :
//   <base>text   etat du tuner en JSON (frequence, signal, RDS, emetteur), ordres "T98500"
//   <base>audio  son en MP3 apres la demande {"type":"fallback","data":"mp3"}
// Le MP3 est decode ici (minimp3) et envoye a la reserve audio (aud::).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace fm {

enum Canal { TEXTE = 0, SON = 1 };
enum Etat { ARRET = 0, CONNEXION, ACTIF, PERDU };

struct Info {
    int etat;
    char message[48];
    char serveur[48];
    char url[100];
    float mhz;              // frequence du tuner
    float sig;              // dBf
    bool stereo, rds;
    char ps[12];            // nom RDS de la station
    char rt[68];            // texte RDS
    char pi[6];
    int auditeurs;
    char tx[40];            // emetteur identifie (base FMLIST)
    char tx_ville[32];
    char tx_pays[24];
    int tx_km;
    float tx_erp;           // kW
    int son_hz;             // debit du MP3 decode (0 = pas encore de son)
    int kbps;               // debit MP3 recu
    uint32_t trames;        // trames MP3 decodees
    int charge;             // % du temps passe a decoder le MP3
    float mhz_serveur;      // frequence annoncee par le serveur (0 tant qu'il n'a rien dit)
    uint32_t maj;           // nombre d'etats recus du serveur (autoscan : mesures neuves)
};

void init();
void connecter(const char *url, const char *nom);
void deconnecter();
bool actif();                       // FM choisie (meme pendant la connexion)
void accorder(float mhz);           // change la frequence du tuner (si le serveur l'autorise)
void info(Info *dst);               // copie de l'etat
const char *message();

// plateforme
uint32_t generation();
bool cible(char *hote, size_t nh, uint16_t *port, bool *tls, char *base, size_t nb);
void ouvert(int canal);
void ferme(int canal);
void recu(int canal, const uint8_t *d, size_t n);
bool sortant(int canal, char *buf, size_t n);
void tic(uint32_t ms);

}  // namespace fm
