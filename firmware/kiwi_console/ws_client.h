// ws_client.h - petit client WebSocket (RFC 6455) pour le KiwiSDR
//
// Remplace la bibliotheque WebSockets : celle-ci refuse les messages de plus
// de 15 Ko, or le KiwiSDR envoie sa configuration (souvent 20 a 60 Ko) des la
// connexion. Ici les messages sont reassembles dans un tampon qui grandit a la
// demande (PSRAM), jusqu'a 512 Ko.
//
// Utilisation (une seule tache) :
//   ws.connecter(hote, port, "/123/SND", origine)   bloquant, ~5 s maximum
//   while (ws.lire(&d, &n) > 0) { ... }             non bloquant, a appeler souvent
//   ws.envoyer_texte("SET ...")
#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

class WsClient {
public:
    // tls : 1 = chiffre (wss), 0 = non, -1 = selon le port (443 = chiffre)
    bool connecter(const char *hote, uint16_t port, const char *chemin, const char *origine, int tls = -1);
    void fermer();
    bool ouvert() { return m_ouvert; }
    bool envoyer_texte(const char *t);
    // 1 = un message complet dans *d / *n (valable jusqu'au prochain appel),
    // 0 = rien de neuf, -1 = connexion fermee (voir code_fermeture())
    int lire(const uint8_t **d, size_t *n);
    uint16_t code_fermeture() const { return m_code; }
    const char *erreur() const { return m_err; }
    const char *redirection() const { return m_redir; }   // "Location" d'une reponse 301/302/307/308, sinon ""

private:
    bool envoyer_trame(uint8_t opcode, const uint8_t *d, size_t n);
    bool reserver(uint8_t **buf, size_t *cap, size_t besoin);
    void echec(const char *e);

    WiFiClient m_tcp;
    WiFiClientSecure m_tls;
    Client *m_c = nullptr;
    bool m_ouvert = false;
    uint16_t m_code = 0;
    char m_err[96] = "";
    char m_redir[160] = "";

    // reception brute (octets pas encore decoupes en trames)
    uint8_t *m_rx = nullptr;
    size_t m_rx_cap = 0, m_rx_n = 0;
    // message en cours de reassemblage (trames fragmentees)
    uint8_t *m_msg = nullptr;
    size_t m_msg_cap = 0, m_msg_n = 0;
    bool m_frag = false;
    size_t m_a_retirer = 0;     // trame rendue au dernier lire(), a retirer au suivant
};
