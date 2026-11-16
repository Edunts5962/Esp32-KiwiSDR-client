// ws_client.cpp - voir ws_client.h
#include "ws_client.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const size_t MAX_MSG = 512 * 1024;

static void base64(const uint8_t *in, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

void WsClient::echec(const char *e)
{
    snprintf(m_err, sizeof m_err, "%s", e);
}

bool WsClient::reserver(uint8_t **buf, size_t *cap, size_t besoin)
{
    if (besoin <= *cap) return true;
    if (besoin > MAX_MSG + 16) return false;
    size_t c = *cap ? *cap : 4096;
    while (c < besoin) c *= 2;
    uint8_t *p = (uint8_t *)realloc(*buf, c);
    if (!p) return false;
    *buf = p;
    *cap = c;
    return true;
}

bool WsClient::connecter(const char *hote, uint16_t port, const char *chemin, const char *origine, int chiffre)
{
    fermer();
    m_code = 0;
    m_err[0] = 0;
    m_redir[0] = 0;
    bool tls = chiffre < 0 ? port == 443 : chiffre != 0;
    if (tls) {
        m_tls.setInsecure();
        m_c = &m_tls;
    } else {
        m_c = &m_tcp;
    }
    // 10 s pour les recepteurs lointains ou charges (3 s par defaut)
    if (!(tls ? m_c->connect(hote, port) : m_tcp.connect(hote, port, 10000))) {
        if (tls) {
            char e[64] = "";
            m_tls.lastError(e, sizeof e);
            snprintf(m_err, sizeof m_err, "connexion chiffree impossible : %s (memoire interne %u o)", e[0] ? e : "?",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        } else {
            echec("connexion TCP impossible");
        }
        return false;
    }
    if (!tls) m_tcp.setNoDelay(true);

    uint8_t cle[16];
    for (int i = 0; i < 16; i++) cle[i] = (uint8_t)esp_random();
    char cle64[32];
    base64(cle, 16, cle64);
    char hote_port[8] = "";
    if (port != 80 && port != 443) snprintf(hote_port, sizeof hote_port, ":%u", port);
    char req[512];
    int l = snprintf(req, sizeof req,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s%s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "Origin: %s\r\n"
                     "User-Agent: ES3C28P-KiwiClient/0.1\r\n\r\n",
                     chemin, hote, hote_port, cle64, origine);
    if (m_c->write((const uint8_t *)req, l) != (size_t)l) { echec("envoi de la requete"); fermer(); return false; }

    // reponse : "HTTP/1.1 101 ..." puis les en-tetes jusqu'a la ligne vide
    char ligne[160];
    size_t n = 0;
    int num = 0;
    bool ok = false;
    bool redir = false;
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        if (!m_c->available()) {
            if (!m_c->connected()) break;
            delay(2);
            continue;
        }
        int c = m_c->read();
        if (c < 0) continue;
        if (c == '\n') {
            ligne[n] = 0;
            if (n && ligne[n - 1] == '\r') ligne[--n] = 0;
            if (num == 0) {
                ok = strstr(ligne, " 101") != nullptr;
                redir = strstr(ligne, " 301") || strstr(ligne, " 302") || strstr(ligne, " 307") || strstr(ligne, " 308");
                if (!ok) snprintf(m_err, sizeof m_err, "refus : %.36s", ligne);
                if (!ok && !redir) break;
            } else if (redir && !strncasecmp(ligne, "Location:", 9)) {
                const char *v = ligne + 9;
                while (*v == ' ') v++;
                snprintf(m_redir, sizeof m_redir, "%s", v);
                snprintf(m_err, sizeof m_err, "redirection vers %.70s", v);
                break;
            }
            num++;
            if (n == 0) break;                    // fin des en-tetes
            n = 0;
        } else if (n < sizeof ligne - 1) {
            ligne[n++] = (char)c;
        }
    }
    if (!ok || num == 0) {
        if (!m_err[0]) echec("pas de reponse du serveur");
        fermer();
        return false;
    }
    m_ouvert = true;
    m_rx_n = 0;
    m_msg_n = 0;
    m_frag = false;
    m_a_retirer = 0;
    return true;
}

void WsClient::fermer()
{
    if (m_c) {
        if (m_ouvert) {
            uint8_t code[2] = { 0x03, 0xE8 };     // 1000 : fermeture normale
            envoyer_trame(0x8, code, 2);
        }
        m_c->stop();
    }
    m_ouvert = false;
}

bool WsClient::envoyer_trame(uint8_t op, const uint8_t *d, size_t n)
{
    if (!m_c) return false;
    uint8_t h[14];
    size_t hl = 0;
    h[hl++] = 0x80 | op;                          // FIN + opcode
    if (n < 126) {
        h[hl++] = 0x80 | (uint8_t)n;              // client : toujours masque
    } else if (n < 65536) {
        h[hl++] = 0x80 | 126;
        h[hl++] = n >> 8;
        h[hl++] = n & 255;
    } else {
        return false;                             // jamais utile ici
    }
    uint32_t r = esp_random();
    uint8_t m[4] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24) };
    memcpy(h + hl, m, 4);
    hl += 4;
    // la trame entiere en un seul envoi : en https, deux envois font deux paquets chiffres,
    // et OpenWebRX ferme la connexion s'il lit l'en-tete sans le contenu
    uint8_t tmp[14 + 512];
    if (hl + n > sizeof tmp) return false;
    memcpy(tmp, h, hl);
    for (size_t i = 0; i < n; i++) tmp[hl + i] = d[i] ^ m[i & 3];
    return m_c->write(tmp, hl + n) == hl + n;
}

bool WsClient::envoyer_texte(const char *t)
{
    if (!m_ouvert) return false;
    if (!envoyer_trame(0x1, (const uint8_t *)t, strlen(t))) {
        echec("envoi impossible");
        fermer();
        return false;
    }
    return true;
}

int WsClient::lire(const uint8_t **d, size_t *n)
{
    if (!m_c) return -1;
    // retire la trame rendue au dernier appel
    if (m_a_retirer) {
        memmove(m_rx, m_rx + m_a_retirer, m_rx_n - m_a_retirer);
        m_rx_n -= m_a_retirer;
        m_a_retirer = 0;
    }
    if (!m_ouvert) return -1;

    // lit ce qui est arrive (au plus 16 Ko par appel, pour rester reactif)
    int dispo = m_c->available();
    if (dispo > 0) {
        size_t a = dispo > 16384 ? 16384 : (size_t)dispo;
        if (!reserver(&m_rx, &m_rx_cap, m_rx_n + a)) { echec("message trop gros"); fermer(); return -1; }
        int lu = m_c->read(m_rx + m_rx_n, a);
        if (lu > 0) m_rx_n += lu;
    } else if (!m_c->connected()) {
        if (!m_err[0]) echec("connexion fermee par le serveur");
        m_ouvert = false;
        m_c->stop();
        return -1;
    }

    // une trame complete ?
    for (;;) {
        if (m_rx_n < 2) return 0;
        uint8_t b0 = m_rx[0], b1 = m_rx[1];
        uint8_t op = b0 & 0x0F;
        bool fin = (b0 & 0x80) != 0, masque = (b1 & 0x80) != 0;
        uint64_t len = b1 & 0x7F;
        size_t hl = 2;
        if (len == 126) {
            if (m_rx_n < 4) return 0;
            len = (uint64_t)m_rx[2] << 8 | m_rx[3];
            hl = 4;
        } else if (len == 127) {
            if (m_rx_n < 10) return 0;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | m_rx[2 + i];
            hl = 10;
        }
        if (len > MAX_MSG) { echec("message trop gros"); fermer(); return -1; }
        uint8_t cle[4] = { 0, 0, 0, 0 };
        if (masque) {
            if (m_rx_n < hl + 4) return 0;
            memcpy(cle, m_rx + hl, 4);
            hl += 4;
        }
        if (m_rx_n < hl + len) {
            if (!reserver(&m_rx, &m_rx_cap, hl + (size_t)len)) { echec("memoire insuffisante"); fermer(); return -1; }
            return 0;                             // la suite arrivera
        }
        uint8_t *p = m_rx + hl;
        if (masque) for (size_t i = 0; i < len; i++) p[i] ^= cle[i & 3];
        size_t total = hl + (size_t)len;

        if (op == 0x8) {                          // fermeture demandee par le serveur
            m_code = len >= 2 ? (uint16_t)(p[0] << 8 | p[1]) : 1005;
            snprintf(m_err, sizeof m_err, "fermee par le serveur (code %u)", m_code);
            envoyer_trame(0x8, p, len >= 2 ? 2 : 0);
            m_ouvert = false;
            m_c->stop();
            return -1;
        }
        if (op == 0x9) {                          // ping -> pong
            envoyer_trame(0xA, p, len < 126 ? (size_t)len : 0);
            memmove(m_rx, m_rx + total, m_rx_n - total);
            m_rx_n -= total;
            continue;
        }
        if (op == 0xA) {                          // pong : ignore
            memmove(m_rx, m_rx + total, m_rx_n - total);
            m_rx_n -= total;
            continue;
        }
        if (op == 0x1 || op == 0x2) {             // debut d'un message
            if (fin && !m_frag) {                 // cas courant : un message en une trame
                *d = p;
                *n = (size_t)len;
                m_a_retirer = total;
                return 1;
            }
            m_frag = true;
            m_msg_n = 0;
        }
        if (op == 0x0 || op == 0x1 || op == 0x2) {   // fragment a reassembler
            if (!reserver(&m_msg, &m_msg_cap, m_msg_n + (size_t)len)) { echec("memoire insuffisante"); fermer(); return -1; }
            memcpy(m_msg + m_msg_n, p, (size_t)len);
            m_msg_n += (size_t)len;
            memmove(m_rx, m_rx + total, m_rx_n - total);
            m_rx_n -= total;
            if (fin) {
                m_frag = false;
                *d = m_msg;
                *n = m_msg_n;
                return 1;
            }
            continue;
        }
        // opcode inconnu : on saute la trame
        memmove(m_rx, m_rx + total, m_rx_n - total);
        m_rx_n -= total;
    }
}
