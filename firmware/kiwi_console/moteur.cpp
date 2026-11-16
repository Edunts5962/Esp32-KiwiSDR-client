// moteur.cpp - decodeurs + enregistrement WAV (tache de fond)
#include "moteur.h"
#include "audio.h"
#include "decodeurs.h"
#include "plateforme.h"
#include <stdio.h>
#include <string.h>

namespace mot {

static uint32_t s_curseur = 0;
static bool s_init = false;
static volatile bool d_rec = false, d_sauver = false;
static bool s_rec = false;
static uint32_t s_rec_octets = 0, s_rec_debit = 12000, s_rec_t0 = 0;
static char s_cr[80] = "";
// enregistrement a la voix
static volatile bool s_voix = false, s_signal = true;
static bool s_rec_voix = false, s_passage = false;
static char s_quoi[40] = "", s_nom_txt[64] = "";
static char *s_journal = nullptr;                // texte du journal (PSRAM)
static size_t s_journal_n = 0;
static const size_t JOURNAL_MAX = 16000;
static uint32_t s_passage_octets = 0;
static char s_passage_ligne[80] = "";
static volatile bool s_cr_neuf = false;

void init()
{
    s_curseur = aud::curseur_brut();
    s_init = true;
}

static void rendu(const char *fmt, const char *a)
{
    snprintf(s_cr, sizeof s_cr, fmt, a);
    s_cr_neuf = true;
}

bool compte_rendu(char *s, int n)
{
    if (!s_cr_neuf) return false;
    s_cr_neuf = false;
    snprintf(s, n, "%s", s_cr);
    return true;
}

void enregistrer(bool oui) { d_rec = oui; }
bool enregistre() { return s_rec; }
uint32_t duree_s() { return s_rec ? (plat_ms() - s_rec_t0) / 1000 : 0; }
void sauver_decodage() { d_sauver = true; }
void voix(bool oui) { s_voix = oui; }
bool voix() { return s_voix; }
void signal_present(bool oui, const char *quoi)
{
    if (quoi && oui && !s_signal) snprintf(s_quoi, sizeof s_quoi, "%s", quoi);
    s_signal = oui;
}
uint32_t duree_ecrite_s() { return s_rec ? s_rec_octets / 2 / (s_rec_debit ? s_rec_debit : 1) : 0; }

// journal : une ligne par passage, "06:12:40  145,500 MHz FM  12 s"
static void passage_fin()
{
    if (!s_passage) return;
    s_passage = false;
    if (!s_journal) return;
    unsigned d = (unsigned)((s_rec_octets - s_passage_octets) / 2 / (s_rec_debit ? s_rec_debit : 1));
    int k = snprintf(s_journal + s_journal_n, JOURNAL_MAX - s_journal_n, "%s  %u s\r\n", s_passage_ligne, d);
    if (k > 0 && s_journal_n + k < JOURNAL_MAX) s_journal_n += k;
}
static void passage_debut()
{
    s_passage = true;
    s_passage_octets = s_rec_octets;
    char h[24];
    plat_horodatage(h, sizeof h);                    // "20261108-061240"
    if (strlen(h) >= 15 && h[8] == '-') snprintf(s_passage_ligne, sizeof s_passage_ligne, "%.2s:%.2s:%.2s  %s", h + 9, h + 11, h + 13, s_quoi);
    else snprintf(s_passage_ligne, sizeof s_passage_ligne, "%s  %s", h, s_quoi);
}

static void le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void entete_wav(uint8_t *e, uint32_t debit, uint32_t donnees)
{
    memcpy(e, "RIFF", 4); le32(e + 4, 36 + donnees); memcpy(e + 8, "WAVEfmt ", 8);
    le32(e + 16, 16); e[20] = 1; e[21] = 0; e[22] = 1; e[23] = 0;      // PCM, mono
    le32(e + 24, debit); le32(e + 28, debit * 2); e[32] = 2; e[33] = 0; e[34] = 16; e[35] = 0;
    memcpy(e + 36, "data", 4); le32(e + 40, donnees);
}

static void debut_rec()
{
    static char nom[64];
    if (!plat_sd()) { d_rec = false; rendu("%s", "Pas de carte SD"); return; }
    char h[24];
    plat_horodatage(h, sizeof h);
    snprintf(nom, sizeof nom, "/kiwi/rec_%s.wav", h);
    snprintf(s_nom_txt, sizeof s_nom_txt, "/kiwi/rec_%s.txt", h);
    s_rec_voix = s_voix;
    s_passage = false;
    s_journal_n = 0;
    if (s_rec_voix && !s_journal) s_journal = (char *)plat_malloc_ps(JOURNAL_MAX);
    if (!plat_fichier_ouvrir(nom)) { d_rec = false; rendu("%s", "Impossible de créer le fichier"); return; }
    s_rec_debit = (uint32_t)(aud::debit() + 0.5f);
    uint8_t e[44];
    entete_wav(e, s_rec_debit, 0);
    plat_fichier_ecrire(e, 44);
    s_rec = true;
    s_rec_octets = 0;
    s_rec_t0 = plat_ms();
    rendu(s_rec_voix ? "Enregistrement à la voix : %s" : "Enregistrement : %s", nom);
}

static void fin_rec()
{
    passage_fin();
    uint8_t e[44];
    entete_wav(e, s_rec_debit, s_rec_octets);
    plat_fichier_ecrire_a(0, e, 44);
    plat_fichier_fermer();
    s_rec = false;
    if (s_rec_voix && s_journal && s_journal_n) plat_texte_ecrire(s_nom_txt, s_journal, s_journal_n);
    char d[24];
    snprintf(d, sizeof d, "%u s", (unsigned)(s_rec_octets / 2 / (s_rec_debit ? s_rec_debit : 1)));
    rendu("Enregistrement terminé (%s)", d);
}

uint32_t etape(uint32_t ms)
{
    (void)ms;
    if (!s_init) init();
    static int16_t tampon[2048];
    int n = aud::lire_brut(s_curseur, tampon, 2048);
    if (n > 0) {
        dec::traiter(tampon, n, aud::debit());
        bool ecrire = s_rec && (!s_rec_voix || s_signal);
        if (s_rec && s_rec_voix) {                       // debut et fin de chaque passage
            if (ecrire && !s_passage) passage_debut();
            if (!ecrire && s_passage) passage_fin();
        }
        if (s_rec && !ecrire) {
            // a la voix, rien a ecrire pendant les silences
        } else if (s_rec && !plat_fichier_ecrire(tampon, n * 2)) {
            fin_rec();
            d_rec = false;
            rendu("%s", "Carte SD pleine ou retirée");
        } else if (s_rec) {
            s_rec_octets += n * 2;
        }
    }
    if (d_rec && !s_rec) debut_rec();
    if (!d_rec && s_rec) fin_rec();
    if (d_sauver) {
        d_sauver = false;
        static char nom[64];
        if (s_rec) rendu("%s", "Arrête d'abord l'enregistrement");
        else if (dec::sauver(nom, sizeof nom)) rendu("Enregistré : %s", nom);
        else rendu("%s", "Échec de la sauvegarde (carte SD ?)");
    }
    return n >= 2048 ? 1 : 20;
}

}  // namespace mot
