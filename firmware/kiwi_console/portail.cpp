// portail.cpp - interface web complete : la meme radio que sur l'ecran de la carte
//
// Une page unique (page_web.h) dialogue avec la carte par une petite API :
//   GET  /api/etat              etat de la radio (JSON), interroge 2 fois par seconde
//   GET  /api/cascade?n=N       lignes de waterfall recues depuis le compteur N (binaire)
//   GET  /api/recepteurs?r=R    liste des recepteurs (region R)
//   GET  /api/recepteurs?q=txt  recherche dans toutes les regions (nom, lieu, adresse)
//   GET  /api/stations?q=&l=    annuaire de stations (recherche, filtre de langue)
//   POST /api/station i, ou     ecouter une station (ou=ici | proche : recepteur libre le plus proche)
//   GET  /api/stations.csv      la liste au format CSV ; POST /api/stations/recharger | defaut
//   POST /api/eibi q            recherche EiBi sur le recepteur connecte ; GET /api/eibi : avancement
//   GET  /api/fm/etat           tuner FM-DX : frequence, signal, RDS, emetteur, son
//   GET  /api/fm/serveurs?q=    liste des serveurs FM-DX (servers.fmdx.org), recherche
//   POST /api/fm/connecter u,nom  ecouter un serveur FM ; POST /api/fm/deconnecter ; POST /api/fm/liste
//   GET  /api/favoris           favoris + preregages
//   GET  /api/dec               texte des decodeurs ; /api/fax : image FAX (binaire)
//   GET  /api/reglages          tous les reglages + etat du systeme
//   POST /api/cmd  o=...        un ordre, execute par l'interface (meme effet qu'un toucher)
//   POST /api/perso/ajouter, GET /api/perso/retirer, POST /api/position, /api/liste_url,
//   POST /api/wifi, POST /api/redemarrer
//   GET  /api/son?c=C          son brut recu depuis le curseur C (pour le traducteur Python)
//   POST /api/trad, GET /api/trad   lignes traduites envoyees par le traducteur Python
// Les ordres passent par ui::commande() : c'est la tache de l'ecran qui les applique,
// donc la carte et la page web restent toujours d'accord.
#include "portail.h"
#include <WiFi.h>
#include "kiwi.h"
#include "audio.h"
#include "cascade.h"
#include "serveurs.h"
#include "stations.h"
#include "fm.h"
#include "fmliste.h"
#include "decodeurs.h"
#include "ft8.h"
#include "webradio.h"
#include "moteur.h"
#include "interface.h"
#include "owrxliste.h"
#include "vhf.h"
#include "geo.h"
#include "quoi.h"
#include "fmliste.h"
#include "reglages.h"
#include "plateforme.h"
#include "page_web.h"
#include <Update.h>

static WebServer *W = nullptr;

// ------------------------------------------------------------------ JSON
static void js(String &o, const char *s)
{
    o += '"';
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    o += '"';
}

static void cle(String &o, const char *k) { o += '"'; o += k; o += "\":"; }
static void champ(String &o, const char *k, const char *v) { cle(o, k); js(o, v); o += ','; }
static void champ(String &o, const char *k, long v) { cle(o, k); o += String(v); o += ','; }
static void champf(String &o, const char *k, double v, int dec) { cle(o, k); o += String(v, dec); o += ','; }
static void fin_objet(String &o) { if (o.endsWith(",")) o.remove(o.length() - 1); o += '}'; }

static void envoyer_json(const String &o)
{
    W->sendHeader("Cache-Control", "no-store");
    W->send(200, "application/json; charset=utf-8", o);
}

static void ok() { W->send(200, "text/plain", "ok"); }

// ------------------------------------------------------------------ etat
static void api_etat()
{
    String o;
    o.reserve(1400);
    o += '{';
    champ(o, "version", KIWI_VERSION);
    if (fm::actif()) {
        fm::Info fi;
        fm::info(&fi);
        champ(o, "fm", 1L);
        champ(o, "fm_nom", fi.serveur);
        champ(o, "fm_etat", (long)fi.etat);
        champ(o, "fm_msg", fi.message);
        champf(o, "fm_mhz", fi.mhz, 2);
        champ(o, "fm_ps", fi.ps);
    }
    champ(o, "etat", (long)kw::etat());
    champ(o, "message", kw::message());
    champ(o, "nom", cfg::R.hote[0] ? (cfg::R.nom[0] ? cfg::R.nom : cfg::R.hote) : "");
    champ(o, "hote", cfg::R.hote);
    champ(o, "port", (long)cfg::R.port);
    champf(o, "f", cfg::R.freq_khz, 3);
    champ(o, "mode", (long)cfg::R.mode);
    champ(o, "pas", (long)cfg::R.pas);
    champ(o, "pas_hz", (long)cfg::PAS_HZ[cfg::R.pas]);
    champ(o, "bp", (long)cfg::R.bp[cfg::R.mode]);
    champ(o, "bp_min", (long)cfg::bp_min(cfg::R.mode));
    champ(o, "bp_max", (long)cfg::bp_max(cfg::R.mode));
    champ(o, "zoom", (long)cfg::R.zoom);
    champ(o, "proto", (long)cfg::R.proto);
    champf(o, "bmin", kw::bande_min_khz(), 3);
    champf(o, "bmax", kw::bande_max_khz(), 3);
    if (cfg::R.proto == cfg::OWRX) {                  // bandes (profils) du recepteur OpenWebRX
        static kw::Profil *pr = (kw::Profil *)plat_malloc_ps(sizeof(kw::Profil) * kw::MAX_PROFILS);
        int np = pr ? kw::profils(pr, kw::MAX_PROFILS) : 0;
        champ(o, "profil", (long)kw::profil_courant());
        cle(o, "profils");
        o += '[';
        for (int i = 0; i < np; i++) {
            if (i) o += ',';
            o += '{';
            champ(o, "id", pr[i].id);
            champ(o, "n", pr[i].nom);
            fin_objet(o);
        }
        o += "],";
    }
    champf(o, "cf", kw::cascade_centre_khz(), 3);
    champf(o, "span", kw::cascade_largeur_khz(), 3);
    champf(o, "rssi", kw::rssi_dbm(), 1);
    champ(o, "sb", (long)ui::signal_bruit());
    champ(o, "ovfl", (long)kw::surcharge_adc());
    champ(o, "volume", (long)cfg::R.volume);
    champ(o, "agc", (long)cfg::R.agc);
    champ(o, "nr", (long)cfg::R.nr);
    champ(o, "notch", (long)cfg::R.notch);
    champ(o, "nb", (long)cfg::R.nb);
    champ(o, "agc_seuil", (long)kw::agc_seuil());
    champ(o, "gain_man", (long)kw::gain_manuel());
    champ(o, "lim", (long)cfg::R.limiteur);
    champ(o, "wf_non", (long)kw::sans_cascade());
    champ(o, "gain", (long)aud::gain_kiwi());
    {
        int sens, ec, reste, sb;
        ui::scan_etat(&sens, &ec, &reste, &sb);
        champ(o, "scan", (long)sens);
        champ(o, "scan_ec", (long)ec);
        champ(o, "scan_reste", (long)reste);
        champ(o, "scan_sens", (long)sb);
        champ(o, "scan_v", (long)ui::scan_vitesse());
    }
    {
        kw::LigneJ j[kw::MAX_J];
        int nj = kw::journal(j, kw::MAX_J);
        champ(o, "jv", (long)kw::journal_visible());
        cle(o, "journal");
        o += '[';
        for (int i = 0; i < nj; i++) {
            if (i) o += ',';
            o += '{';
            champ(o, "t", j[i].t);
            champ(o, "e", (long)j[i].e);
            fin_objet(o);
        }
        o += "],";
    }
    champ(o, "canal", (long)kw::canal_rx());
    champ(o, "wfch", (long)kw::canaux_cascade());
    champ(o, "reserve", (long)aud::reserve_ms());
    champ(o, "lecture", (long)aud::en_lecture());
    champ(o, "rec", (long)mot::enregistre());
    champ(o, "rec_s", (long)mot::duree_s());
    champ(o, "sd", (long)plat_sd());
    champ(o, "liste_cours", (long)plat_liste_en_cours());
    // noms des stations visibles
    static kw::Marqueur *m = (kw::Marqueur *)plat_malloc_ps(sizeof(kw::Marqueur) * kw::MAX_MARQUEURS);   // PSRAM
    int n = m && cfg::R.etiquettes && kw::etat() == kw::ACTIF ? kw::marqueurs(m, kw::MAX_MARQUEURS) : 0;
    cle(o, "stations");
    o += '[';
    for (int i = 0; i < n; i++) {
        if (i) o += ',';
        o += "{\"f\":" + String(m[i].khz, 3) + ",\"m\":" + String(m[i].mode) + ",\"n\":";
        js(o, m[i].nom);
        o += '}';
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// ------------------------------------------------------------------ waterfall (binaire)
// en-tete : compteur(u32) nombre(u16) centre(f32) largeur(f32) puis 320 octets de spectre
// (dB + 160) puis les lignes RGB565 (petit-boutiste), de la plus ancienne a la plus recente
static void api_cascade()
{
    uint32_t c = wf::compteur();
    uint32_t depuis = (uint32_t)W->arg("n").toInt();
    int n = (depuis == 0 || c - depuis > (uint32_t)wf::H || depuis > c) ? wf::H : (int)(c - depuis);
    if ((uint32_t)n > c) n = (int)c;
    size_t taille = 14 + wf::L + (size_t)n * wf::L * 2;
    uint8_t *b = (uint8_t *)malloc(taille);
    if (!b) { W->send(503, "text/plain", "memoire"); return; }
    float cf = kw::cascade_centre_khz(), sp = kw::cascade_largeur_khz();
    memcpy(b, &c, 4);
    uint16_t nn = (uint16_t)n;
    memcpy(b + 4, &nn, 2);
    memcpy(b + 6, &cf, 4);
    memcpy(b + 10, &sp, 4);
    const float *s = wf::spectre();
    for (int x = 0; x < wf::L; x++) {
        int v = (int)(s[x] + 160);
        b[14 + x] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    uint8_t *p = b + 14 + wf::L;
    for (int age = n - 1; age >= 0; age--, p += wf::L * 2) memcpy(p, wf::rangee(age), wf::L * 2);
    W->sendHeader("Cache-Control", "no-store");
    W->send_P(200, "application/octet-stream", (const char *)b, taille);
    free(b);
}

// ------------------------------------------------------------------ recepteurs
static void api_recepteurs()
{
    static const srv::Serveur **v = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    if (!v) { W->send(503, "text/plain", "memoire"); return; }
    int r = W->hasArg("r") ? W->arg("r").toInt() : srv::PROCHES;
    if (r < 0 || r >= srv::NB_REGIONS) r = srv::PROCHES;
    String q = W->hasArg("q") ? W->arg("q") : String();
    q.trim();
    bool recherche = q.length() > 0;
    int n = recherche ? srv::chercher(q.c_str(), v, srv::MAX_PUBLICS + srv::MAX_PERSO)
                      : srv::filtrer(r, v, srv::MAX_PUBLICS + srv::MAX_PERSO);
    int total = n;
    if (n > 80) n = 80;
    String o;
    o.reserve(200 + n * 150);
    o += '{';
    champ(o, "etat", srv::etat_liste());
    champ(o, "cours", (long)plat_liste_en_cours());
    champ(o, "total", (long)total);
    champ(o, "recherche", (long)recherche);
    cle(o, "regions");
    o += '[';
    for (int i = 0; i < srv::NB_REGIONS; i++) { if (i) o += ','; js(o, srv::nom_region(i)); }
    o += "],";
    cle(o, "liste");
    o += '[';
    for (int i = 0; i < n; i++) {
        const srv::Serveur *s = v[i];
        if (i) o += ',';
        o += '{';
        champ(o, "nom", s->nom);
        champ(o, "lieu", s->lieu);
        champ(o, "hote", s->hote);
        champ(o, "port", (long)s->port);
        champ(o, "km", (long)s->km);
        champ(o, "u", (long)s->users);
        champ(o, "umax", (long)s->umax);
        champ(o, "perso", (long)s->perso);
        if (s->perso) {
            for (int k = 0; k < srv::nb_perso(); k++)
                if (srv::perso(k) == s) { champ(o, "ip", (long)k); break; }
            champ(o, "mdp", s->mdp[0] ? "1" : "");
        }
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// connexion a un recepteur de la liste (le mot de passe perso reste sur la carte)
static void api_connecter()
{
    char hote[64], mdp[24] = "", nom[48];
    snprintf(hote, sizeof hote, "%s", W->arg("h").c_str());
    snprintf(nom, sizeof nom, "%s", W->arg("nom").c_str());
    int port = W->arg("p").toInt();
    if (W->hasArg("ip")) {
        const srv::Serveur *s = srv::perso(W->arg("ip").toInt());
        if (s) snprintf(mdp, sizeof mdp, "%s", s->mdp);
    } else if (W->hasArg("mdp")) {
        snprintf(mdp, sizeof mdp, "%s", W->arg("mdp").c_str());
    }
    if (!hote[0]) {                                   // adresse saisie a la main
        uint16_t p;
        if (!srv::analyser_adresse(W->arg("a").c_str(), hote, sizeof hote, &p)) { W->send(400, "text/plain", "adresse"); return; }
        port = p;
        snprintf(nom, sizeof nom, "%s", hote);
    }
    for (char *q = nom; *q; q++) if (*q == '\t') *q = ' ';
    String o = String("co ") + hote + "\t" + String(port ? port : 8073) + "\t" + mdp + "\t" + nom;
    ui::commande(o.c_str());
    ok();
}

// ------------------------------------------------------------------ favoris
// ------------------------------------------------------------------ FM (reseau FM-DX Webserver)
static void api_fm_etat()
{
    fm::Info i;
    fm::info(&i);
    String o;
    o.reserve(900);
    o += '{';
    champ(o, "actif", (long)fm::actif());
    champ(o, "etat", (long)i.etat);
    champ(o, "message", i.message);
    champ(o, "serveur", i.serveur);
    champ(o, "url", i.url);
    champf(o, "mhz", i.mhz, 2);
    champf(o, "sig", i.sig, 1);
    champ(o, "st", (long)i.stereo);
    champ(o, "rds", (long)i.rds);
    champ(o, "ps", i.ps);
    champ(o, "rt", i.rt);
    champ(o, "pi", i.pi);
    champ(o, "users", (long)i.auditeurs);
    champ(o, "tx", i.tx);
    champ(o, "tx_ville", i.tx_ville);
    champ(o, "tx_pays", i.tx_pays);
    champ(o, "tx_km", (long)i.tx_km);
    champf(o, "tx_erp", i.tx_erp, 2);
    champ(o, "hz", (long)i.son_hz);
    champ(o, "kbps", (long)i.kbps);
    champ(o, "reserve", (long)aud::reserve_ms());
    champ(o, "volume", (long)cfg::R.volume);
    {
        int v, r;
        int sens = ui::scan_fm(&v, &r);
        champ(o, "scan", (long)sens);
        champ(o, "scan_v", (long)v);
        champ(o, "scan_reste", (long)r);
        static float mhz[80], sig[80];
        static char ps[80][10];
        int n = ui::fm_trouvees(mhz, sig, ps, 80);
        cle(o, "trouvees");
        o += '[';
        for (int k = 0; k < n; k++) {
            if (k) o += ',';
            o += '{';
            champf(o, "f", mhz[k], 1);
            champf(o, "s", sig[k], 0);
            champ(o, "ps", ps[k]);
            fin_objet(o);
        }
        o += "],";
    }
    fin_objet(o);
    envoyer_json(o);
}

static void api_canaux()
{
    static const vhf::Canal **v = (const vhf::Canal **)plat_malloc_ps(sizeof(void *) * 2000);
    if (!v) { W->send(503, "text/plain", "memoire"); return; }
    int cats = W->hasArg("c") ? W->arg("c").toInt() : vhf::TOUS;
    if (cats <= 0) cats = vhf::TOUS;
    String q = W->arg("q");
    q.trim();
    int n = vhf::chercher(cats, q.c_str(), v, 2000);
    bool owrx = cfg::R.proto == cfg::OWRX && kw::etat() == kw::ACTIF;
    bool ici = owrx && W->arg("e") == "1";            // seulement les canaux que ce recepteur capte
    static uint8_t *etats = (uint8_t *)plat_malloc_ps(2000);
    if (!etats) { W->send(503, "text/plain", "memoire"); return; }
    int k2 = 0;
    for (int k = 0; k < n; k++) {
        int e = ui::canal_etat(v[k]->khz);
        if (ici && !e) continue;
        v[k2] = v[k];
        etats[k2++] = (uint8_t)e;
    }
    n = k2;
    int total = n;
    if (n > 300) n = 300;
    String o;
    o.reserve(300 + n * 120);
    o += '{';
    champ(o, "source", vhf::source());
    champ(o, "total", (long)total);
    champ(o, "owrx", (long)owrx);
    char sl[96];
    if (ui::scan_liste(sl, sizeof sl)) champ(o, "scan", sl);
    cle(o, "liste");
    o += '[';
    for (int k = 0; k < n; k++) {
        if (k) o += ',';
        o += '{';
        champf(o, "f", v[k]->khz, 3);
        champ(o, "m", (long)v[k]->mode);
        champ(o, "n", v[k]->nom);
        champ(o, "t", v[k]->type);
        champ(o, "c", (long)v[k]->cat);
        champ(o, "km", (long)(v[k]->lat == 999 ? -1 : v[k]->km));
        champ(o, "in", (long)etats[k]);
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// ---- journal d'ecoute ----
static ui::EcouteWeb *s_jw = nullptr;
static void journal_ligne(String &o, const ui::EcouteWeb &e, bool csv)
{
    char date[12] = "", heure[10] = "";
    if (strlen(e.quand) >= 15) { snprintf(date, sizeof date, "%.4s-%.2s-%.2s", e.quand, e.quand + 4, e.quand + 6); snprintf(heure, sizeof heure, "%.2s:%.2s:%.2s", e.quand + 9, e.quand + 11, e.quand + 13); }
    else snprintf(date, sizeof date, "%.11s", e.quand);
    if (csv) {
        char l[220], f[24];
        snprintf(f, sizeof f, "%.2f", e.f);
        for (char *q = f; *q; q++) if (*q == '.') *q = ',';
        char rx[32], quoi[32];
        snprintf(rx, sizeof rx, "%s", e.rx); snprintf(quoi, sizeof quoi, "%s", e.quoi);
        for (char *q = rx; *q; q++) if (*q == ';') *q = ',';
        for (char *q = quoi; *q; q++) if (*q == ';') *q = ',';
        snprintf(l, sizeof l, "%s;%s;%s;%s;%u;%d;%s;%s\r\n", date, heure, f, cfg::nom_mode(e.mode), e.duree_s, e.rssi, rx, quoi);
        o += l;
        return;
    }
    o += '{';
    champ(o, "d", date);
    champ(o, "h", heure);
    champf(o, "f", e.f, 2);
    champ(o, "m", cfg::nom_mode(e.mode));
    champ(o, "s", (long)e.duree_s);
    champ(o, "r", (long)e.rssi);
    champ(o, "rx", e.rx);
    champ(o, "q", e.quoi);
    fin_objet(o);
}

static void api_journal()
{
    if (!s_jw) s_jw = (ui::EcouteWeb *)plat_malloc_ps(sizeof(ui::EcouteWeb) * 60);
    if (!s_jw) { W->send(503, "text/plain", "memoire"); return; }
    ui::EcouteWeb cours;
    int n = ui::journal_web(s_jw, 60, &cours);
    bool csv = W->uri().endsWith(".csv");
    String o;
    o.reserve(200 + n * 190);
    if (csv) {
        o += "date;heure;frequence_khz;mode;duree_s;signal_max_dbm;recepteur;station\r\n";
        for (int k = n - 1; k >= 0; k--) journal_ligne(o, s_jw[k], true);       // la plus ancienne d'abord
        W->sendHeader("Content-Disposition", "attachment; filename=\"journal-kiwi.csv\"");
        W->send(200, "text/csv; charset=utf-8", o);
        return;
    }
    o += '{';
    champ(o, "sd", (long)plat_sd());
    cle(o, "cours");
    if (cours.f > 0) journal_ligne(o, cours, false); else o += "null";
    o += ',';
    cle(o, "liste");
    o += '[';
    for (int k = 0; k < n; k++) { if (k) o += ','; journal_ligne(o, s_jw[k], false); }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// ---- sauvegarde et restauration des reglages et des favoris ----
// Un fichier texte : chaque bloc enregistre dans la carte, en hexadecimal. Le WiFi n'y est pas (ni son mot de passe).
static const char *const CLES_SAUVEGARDE[] = { "reglages", "favoris2", "options", "options2", "options3", "bp_am", "molette", "gain_kiwi",
                                               "scan_sens", "suivre_web", "position", "perso", "bandes" };
static void api_sauvegarde()
{
    static uint8_t *buf = (uint8_t *)plat_malloc_ps(8192);
    if (!buf) { W->send(503, "text/plain", "memoire"); return; }
    String o;
    o.reserve(20000);
    o += "{\"type\":\"sauvegarde-kiwi-es3c28p\",\"version\":\"" KIWI_VERSION "\",\"cles\":{";
    bool premier = true;
    for (const char *c : CLES_SAUVEGARDE) {
        size_t n = plat_nvs_taille(c);
        if (!n || n > 8192 || !plat_nvs_lire(c, buf, n)) continue;
        if (!premier) o += ',';
        premier = false;
        o += '"'; o += c; o += "\":\"";
        static const char H[] = "0123456789abcdef";
        for (size_t i = 0; i < n; i++) { o += H[buf[i] >> 4]; o += H[buf[i] & 15]; }
        o += '"';
    }
    o += "}}";
    W->sendHeader("Content-Disposition", "attachment; filename=\"sauvegarde-kiwi.json\"");
    W->send(200, "application/json", o);
}

static int hexa(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }

static void api_restaurer()
{
    static uint8_t *buf = (uint8_t *)plat_malloc_ps(8192);
    String t = W->arg("plain");
    if (!buf || t.indexOf("sauvegarde-kiwi-es3c28p") < 0) { W->send(400, "text/plain; charset=utf-8", "Ce fichier n'est pas une sauvegarde de la carte"); return; }
    // tout verifier avant d'ecrire quoi que ce soit
    int faits = 0;
    for (int passe = 0; passe < 2; passe++) {
        for (const char *c : CLES_SAUVEGARDE) {
            String motif = String("\"") + c + "\":\"";
            int i = t.indexOf(motif);
            if (i < 0) continue;
            i += motif.length();
            int j = t.indexOf('"', i);
            if (j < 0 || (j - i) % 2 || (j - i) / 2 > 8192 || j == i) { W->send(400, "text/plain; charset=utf-8", "Sauvegarde abîmée : rien n'a été modifié"); return; }
            size_t n = (j - i) / 2;
            for (size_t k = 0; k < n; k++) {
                int a = hexa(t[i + 2 * k]), b = hexa(t[i + 2 * k + 1]);
                if (a < 0 || b < 0) { W->send(400, "text/plain; charset=utf-8", "Sauvegarde abîmée : rien n'a été modifié"); return; }
                buf[k] = (uint8_t)(a * 16 + b);
            }
            if (passe == 1) { plat_nvs_ecrire(c, buf, n); faits++; }
        }
        if (passe == 0) cfg::geler();                   // plus aucune ecriture des reglages en cours par-dessus
    }
    if (!faits) { W->send(400, "text/plain; charset=utf-8", "Sauvegarde vide"); return; }
    W->send(200, "text/plain", "ok");
    ui::message("Sauvegarde restaurée : redémarrage");
    delay(1200);
    ESP.restart();
}

// ---- mise a jour du programme par la page web ----
// Le fichier arrive par morceaux ; il est ecrit dans l'autre emplacement de programme (la carte en a deux).
// La carte ne redemarre dessus que si tout le fichier est arrive et verifie : sinon l'ancien programme reste.
static char s_maj_err[96] = "";
static size_t s_maj_recu = 0;
static void maj_morceau()
{
    HTTPUpload &u = W->upload();
    if (u.status == UPLOAD_FILE_START) {
        s_maj_err[0] = 0;
        s_maj_recu = 0;
        String nom = u.filename;
        nom.toLowerCase();
        if (nom.indexOf("merged") >= 0 || nom.indexOf("bootloader") >= 0 || nom.indexOf("partitions") >= 0 || !nom.endsWith(".bin")) {
            snprintf(s_maj_err, sizeof s_maj_err, "Mauvais fichier : il faut kiwi_console.ino.bin");
            return;
        }
        ui::message("Mise à jour en cours…");
        kw::deconnecter();                              // toute la place pour l'ecriture
        fm::deconnecter();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) snprintf(s_maj_err, sizeof s_maj_err, "Mise à jour impossible : %s", Update.errorString());
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (s_maj_err[0]) return;
        if (s_maj_recu == 0) {
            // un programme ESP32 commence par 0xE9 et porte sa description a l'octet 32 (0xABCD5432)
            bool appli = u.currentSize >= 36 && u.buf[0] == 0xE9 && u.buf[32] == 0x32 && u.buf[33] == 0x54 && u.buf[34] == 0xCD && u.buf[35] == 0xAB;
            if (!appli) { snprintf(s_maj_err, sizeof s_maj_err, "Ce fichier n'est pas un programme pour la carte"); Update.abort(); return; }
        }
        if (Update.write(u.buf, u.currentSize) != u.currentSize) { snprintf(s_maj_err, sizeof s_maj_err, "Écriture impossible : %s", Update.errorString()); Update.abort(); return; }
        s_maj_recu += u.currentSize;
    } else if (u.status == UPLOAD_FILE_END) {
        if (s_maj_err[0]) return;
        if (!Update.end(true)) snprintf(s_maj_err, sizeof s_maj_err, "Fichier incomplet ou abîmé : %s", Update.errorString());
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        snprintf(s_maj_err, sizeof s_maj_err, "Envoi interrompu");
    }
}

// "Quoi écouter" : suggestions selon l'heure, la saison et la position du recepteur (et mesure)
static void api_quoi()
{
    static ui::QuoiWeb *v = (ui::QuoiWeb *)plat_malloc_ps(sizeof(ui::QuoiWeb) * 40);
    if (!v) { W->send(503, "text/plain", "memoire"); return; }
    int cat = W->hasArg("c") ? W->arg("c").toInt() : 0;
    char ctx[112];
    int fait = 0, total = 0;
    int n = ui::quoi_web(cat, v, 40, ctx, sizeof ctx, &fait, &total);
    String o;
    o.reserve(300 + n * 220);
    o += '{';
    champ(o, "ctx", ctx);
    champ(o, "actif", (long)(kw::etat() == kw::ACTIF));
    champ(o, "owrx", (long)(cfg::R.proto == cfg::OWRX));
    champ(o, "fait", (long)fait);
    champ(o, "total", (long)total);
    cle(o, "liste");
    o += '[';
    for (int k = 0; k < n; k++) {
        if (k) o += ',';
        o += '{';
        champ(o, "n", v[k].nom);
        champ(o, "d", v[k].detail);
        champ(o, "e", v[k].etat);
        champf(o, "a", v[k].a, 3);
        champf(o, "b", v[k].b, 3);
        champ(o, "g", (long)v[k].genre);
        champ(o, "niv", (long)v[k].niveau);
        champ(o, "in", (long)v[k].dispo);
        champ(o, "mes", (long)v[k].mesure);
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// "Pres de moi" : les 5 plus proches de chaque type
static void api_proches()
{
    static const srv::Serveur **vk = (const srv::Serveur **)plat_malloc_ps(sizeof(void *) * (srv::MAX_PUBLICS + srv::MAX_PERSO));
    static const owl::RecepteurVHF **vv = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    static const fml::ServeurFM **vf = (const fml::ServeurFM **)plat_malloc_ps(sizeof(void *) * 700);
    if (!vk || !vv || !vf) { W->send(503, "text/plain", "memoire"); return; }
    if (!srv::nb_publics() && !plat_liste_en_cours()) plat_liste_telecharger();
    static uint32_t t_essai = 0;
    if (!t_essai || plat_ms() - t_essai > 60000) {
        t_essai = plat_ms();
        if (!owl::nb() && !plat_vhf_liste_en_cours()) plat_vhf_liste_telecharger();
        if (!fml::nb() && !plat_fm_liste_en_cours()) plat_fm_liste_telecharger();
    }
    String o;
    o.reserve(4000);
    o += '{';
    { char e[96]; geo::etiquette(e, sizeof e); champ(o, "pos", e); champ(o, "pos_etat", geo::etat()); }
    champ(o, "cours", (long)(plat_liste_en_cours() || plat_vhf_liste_en_cours() || plat_fm_liste_en_cours() || geo::en_cours()));
    int n = srv::filtrer(srv::PROCHES, vk, srv::MAX_PUBLICS + srv::MAX_PERSO), k = 0;
    cle(o, "kiwi");
    o += '[';
    for (int i = 0; i < n && k < 5; i++) {
        if (vk[i]->perso || vk[i]->users >= vk[i]->umax || vk[i]->km >= 65000) continue;
        if (k++) o += ',';
        o += '{';
        champ(o, "nom", vk[i]->nom); champ(o, "lieu", vk[i]->lieu); champ(o, "hote", vk[i]->hote);
        champ(o, "port", (long)vk[i]->port); champ(o, "km", (long)vk[i]->km);
        champ(o, "u", (long)vk[i]->users); champ(o, "um", (long)vk[i]->umax);
        fin_objet(o);
    }
    o += "],";
    n = owl::chercher("", vv, owl::MAX_VHF);
    cle(o, "vhf");
    o += '[';
    for (int i = 0; i < n && i < 5; i++) {
        if (i) o += ',';
        o += '{';
        champ(o, "nom", vv[i]->nom); champ(o, "url", vv[i]->url); champ(o, "km", (long)vv[i]->km); champ(o, "b", (long)vv[i]->bandes);
        fin_objet(o);
    }
    o += "],";
    n = fml::chercher("", vf, 700);
    k = 0;
    cle(o, "fm");
    o += '[';
    for (int i = 0; i < n && k < 5; i++) {
        if (vf[i]->etat != 1 || vf[i]->km >= 65000) continue;
        if (k++) o += ',';
        o += '{';
        champ(o, "nom", vf[i]->nom); champ(o, "url", vf[i]->url); champ(o, "ville", vf[i]->ville); champ(o, "pays", vf[i]->pays);
        champ(o, "km", (long)vf[i]->km);
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

static void api_vhf()
{
    static uint32_t t_essai = 0;
    if (!owl::nb() && !plat_vhf_liste_en_cours() && (!t_essai || plat_ms() - t_essai > 60000)) {
        t_essai = plat_ms();
        plat_vhf_liste_telecharger();
    }
    static const owl::RecepteurVHF **v = (const owl::RecepteurVHF **)plat_malloc_ps(sizeof(void *) * owl::MAX_VHF);
    if (!v) { W->send(503, "text/plain", "memoire"); return; }
    String q = W->arg("q");
    q.trim();
    int n = owl::chercher(q.c_str(), v, owl::MAX_VHF);
    int total = n;
    if (n > 150) n = 150;
    String o;
    o.reserve(300 + n * 200);
    o += '{';
    champ(o, "etat", owl::etat_liste());
    champ(o, "cours", (long)plat_vhf_liste_en_cours());
    champ(o, "total", (long)total);
    cle(o, "liste");
    o += '[';
    for (int k = 0; k < n; k++) {
        if (k) o += ',';
        o += '{';
        champ(o, "nom", v[k]->nom);
        champ(o, "url", v[k]->url);
        champ(o, "km", (long)v[k]->km);
        champ(o, "b", (long)v[k]->bandes);
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

static void api_fm_serveurs()
{
    static uint32_t t_essai = 0;                     // premier usage ; apres un echec, pas plus d'un essai par minute
    if (!fml::nb() && !plat_fm_liste_en_cours() && (!t_essai || plat_ms() - t_essai > 60000)) {
        t_essai = plat_ms();
        plat_fm_liste_telecharger();
    }
    static const fml::ServeurFM **v = (const fml::ServeurFM **)plat_malloc_ps(sizeof(void *) * fml::MAX_FM);
    if (!v) { W->send(503, "text/plain", "memoire"); return; }
    String q = W->arg("q");
    q.trim();
    int n = fml::chercher(q.c_str(), v, fml::MAX_FM);
    int total = n;
    if (n > 100) n = 100;
    String o;
    o.reserve(300 + n * 260);
    o += '{';
    champ(o, "etat", fml::etat_liste());
    champ(o, "cours", (long)plat_fm_liste_en_cours());
    champ(o, "total", (long)total);
    cle(o, "liste");
    o += '[';
    for (int k = 0; k < n; k++) {
        const fml::ServeurFM *s = v[k];
        if (k) o += ',';
        o += '{';
        champ(o, "nom", s->nom);
        champ(o, "ville", s->ville);
        champ(o, "pays", s->pays);
        champ(o, "code", s->code);
        champ(o, "url", s->url);
        champ(o, "plage", s->plage);
        champ(o, "km", (long)s->km);
        champ(o, "kbps", (long)s->kbps);
        champ(o, "etat", (long)s->etat);
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// ------------------------------------------------------------------ stations
static void api_stations()
{
    static int *idx = (int *)plat_malloc_ps(sizeof(int) * sta::MAX_STATIONS);
    if (!idx) { W->send(503, "text/plain", "memoire"); return; }
    String q = W->arg("q"), l = W->arg("l");
    q.trim();
    int n = sta::chercher(q.c_str(), l.c_str(), idx, sta::MAX_STATIONS);
    int total = n;
    if (n > 150) n = 150;
    String o;
    o.reserve(300 + n * 170);
    o += '{';
    champ(o, "source", sta::source());
    champ(o, "nb", (long)sta::nb());
    champ(o, "total", (long)total);
    champ(o, "connecte", (long)(kw::etat() == kw::ACTIF));
    champ(o, "listes", (long)srv::nb_publics());
    cle(o, "liste");
    o += '[';
    for (int i = 0; i < n; i++) {
        const sta::Station *s = sta::station(idx[i]);
        if (!s) continue;
        if (i) o += ',';
        o += '{';
        champ(o, "i", (long)idx[i]);
        champ(o, "nom", s->nom);
        champf(o, "f", s->khz, 3);
        champ(o, "m", (long)s->mode);
        champ(o, "lg", s->langue);
        champ(o, "pays", s->pays);
        champ(o, "info", s->info);
        champ(o, "pos", (long)sta::position_connue(*s));
        fin_objet(o);
    }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

// ecouter une station : ici (recepteur actuel) ou sur le recepteur libre le plus proche de l'emetteur
static void api_station_ecouter()
{
    const sta::Station *s = sta::station(W->arg("i").toInt());
    if (!s) { W->send(404, "text/plain", "Station inconnue"); return; }
    bool proche = W->arg("ou") == "proche";
    char c[200];
    String o = "{";
    if (proche) {
        if (!sta::position_connue(*s)) { W->send(400, "text/plain", "Position de l'émetteur inconnue"); return; }
        uint16_t km = 0;
        const srv::Serveur *r = srv::plus_proche_libre(s->lat, s->lon, &km);
        if (!r) { W->send(409, "text/plain", "Aucun récepteur libre : charge d'abord la liste (onglet Récepteurs)"); return; }
        snprintf(c, sizeof c, "station %.3f %d\t%s\t%u\t%s", s->khz, s->mode, r->hote, (unsigned)r->port, r->nom);
        champ(o, "recepteur", r->nom);
        champ(o, "lieu", r->lieu);
        champ(o, "km", (long)km);
    } else {
        snprintf(c, sizeof c, "station %.3f %d", s->khz, s->mode);
    }
    ui::commande(c);
    champ(o, "station", s->nom);
    fin_objet(o);
    envoyer_json(o);
}

static void api_stations_csv()
{
    size_t n = 120 + (size_t)sta::nb() * 200;
    char *t = (char *)malloc(n);
    if (!t) { W->send(500, "text/plain", "Mémoire insuffisante"); return; }
    size_t l = sta::ecrire_csv(t, n);
    W->sendHeader("Content-Disposition", "attachment; filename=stations.csv");
    W->send_P(200, "text/csv; charset=utf-8", t, l);
    free(t);
}

static void api_eibi()
{
    kw::Eibi *r = (kw::Eibi *)malloc(sizeof(kw::Eibi) * kw::MAX_EIBI);
    if (!r) { W->send(500, "text/plain", "Mémoire insuffisante"); return; }
    int avance = 0;
    const char *err = "";
    int e = kw::eibi_etat(&avance, &err);
    int n = kw::eibi_resultats(r, kw::MAX_EIBI);
    static const char *ETATS[4] = { "rien", "cours", "fini", "erreur" };
    String o;
    o.reserve(200 + n * 120);
    o += '{';
    champ(o, "etat", ETATS[e & 3]);
    champ(o, "avance", (long)avance);
    champ(o, "erreur", err);
    cle(o, "liste");
    o += '[';
    for (int i = 0; i < n; i++) {
        if (i) o += ',';
        o += '{';
        champf(o, "f", r[i].khz, 2);
        champ(o, "m", (long)r[i].mode);
        champ(o, "nom", r[i].nom);
        champ(o, "note", r[i].note);
        fin_objet(o);
    }
    free(r);
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

static void api_favoris()
{
    String o;
    o.reserve(2500);
    o += "{\"fav\":[";
    for (int i = 0; i < ui::nb_favoris(); i++) {
        double f;
        int m;
        char nom[32], hote[64];
        if (!ui::favori(i, &f, &m, nom, sizeof nom, hote, sizeof hote)) continue;
        if (i) o += ',';
        o += "{\"f\":" + String(f, 3) + ",\"m\":" + String(m) + ",\"nom\":";
        js(o, nom);
        o += ",\"hote\":";
        js(o, hote);
        o += '}';
    }
    o += "],\"presets\":[";
    for (int i = 0; i < ui::nb_presets(); i++) {
        double f;
        int m;
        const char *n = ui::preset(i, &f, &m);
        if (i) o += ',';
        o += "{\"f\":" + String(f, 3) + ",\"m\":" + String(m) + ",\"nom\":";
        js(o, n);
        o += '}';
    }
    o += "]}";
    envoyer_json(o);
}

// ------------------------------------------------------------------ decodeurs
static void station_json(String &o, const wr::Station &s, bool genres)
{
    o += '{';
    champ(o, "n", s.nom);
    champ(o, "u", s.url);
    champ(o, "c", s.codec);
    champ(o, "k", (long)s.kbps);
    champ(o, "p", s.pays);
    if (genres) champ(o, "t", s.genres);
    champ(o, "f", (long)wr::est_favori(s.url));
    fin_objet(o);
}
static void api_webradio()
{
    String o;
    o.reserve(9000);
    o += '{';
    champ(o, "etat", (long)wr::etat());
    champ(o, "msg", wr::message());
    champ(o, "nom", wr::en_cours().nom);
    champ(o, "url", wr::actif() ? wr::en_cours().url : "");
    champ(o, "titre", wr::titre());
    champ(o, "format", wr::format());
    champ(o, "cours", (long)wr::recherche_en_cours());
    cle(o, "res");
    o += '[';
    for (int i = 0; i < wr::nb_resultats(); i++) { if (i) o += ','; station_json(o, wr::resultat(i), true); }
    o += "],";
    cle(o, "fav");
    o += '[';
    for (int i = 0; i < wr::nb_favoris(); i++) { if (i) o += ','; station_json(o, wr::favori(i), false); }
    o += "],";
    fin_objet(o);
    envoyer_json(o);
}

static void api_dec()
{
    ui::commande("decactif 1");                       // les decodeurs tournent tant que la page les lit
    String o;
    o.reserve(1600);
    o += '{';
    champ(o, "mode", (long)dec::choisi());
    champ(o, "texte", dec::texte());
    champ(o, "i1", dec::info1());
    champ(o, "i2", dec::info2());
    champ(o, "cale", (long)dec::cale());
    champ(o, "inv", (long)dec::inverse());
    champf(o, "ton", dec::tonalite_hz(), 0);
    champ(o, "lignes", (long)dec::fax_nb_lignes());
    if (dec::choisi() == dec::FT8) {                  // FT8 : etat de la periode et les 60 derniers messages
        champ(o, "avant", (long)f8::secondes_avant());
        champ(o, "der", (long)f8::derniers());
        champ(o, "ms", (long)f8::duree_ms());
        champ(o, "fv", (long)f8::version());
        champ(o, "bande", (long)ui::ft8_bande_web());
        cle(o, "ft8");
        o += '[';
        int n = f8::nb() < 60 ? f8::nb() : 60;
        for (int i = 0; i < n; i++) {
            const f8::Message &m = f8::message(i);
            o += '{';
            champ(o, "t", (long)m.periode);
            champ(o, "s", (long)m.snr);
            champ(o, "dt", (long)m.dt10);
            champ(o, "f", (long)m.hz);
            champ(o, "m", m.texte);
            champ(o, "p", m.pays ? m.pays : "");
            champ(o, "l", m.loc);
            champ(o, "km", (long)m.km);
            champ(o, "cq", (long)m.cq);
            fin_objet(o);
            o += ',';
        }
        if (o.endsWith(",")) o.remove(o.length() - 1);
        o += "],";
    }
    fin_objet(o);
    envoyer_json(o);
}

static void api_fax()
{
    int n = dec::fax_nb_lignes();
    if (n > 240) n = 240;
    size_t taille = 2 + (size_t)n * dec::FAX_L;
    uint8_t *b = (uint8_t *)malloc(taille);
    if (!b) { W->send(503, "text/plain", "memoire"); return; }
    b[0] = n & 255;
    b[1] = n >> 8;
    for (int i = 0; i < n; i++) memcpy(b + 2 + i * dec::FAX_L, dec::fax_ligne(n - 1 - i), dec::FAX_L);
    W->send_P(200, "application/octet-stream", (const char *)b, taille);
    free(b);
}

// ------------------------------------------------------------------ reglages et systeme
static void api_reglages()
{
    String o;
    o.reserve(1500);
    o += '{';
    champ(o, "version", KIWI_VERSION);
    champ(o, "wfv", (long)cfg::R.wf_vitesse);
    champ(o, "lum", (long)cfg::R.luminosite);
    champ(o, "pal", (long)cfg::R.palette);
    champ(o, "adpcm", (long)cfg::R.adpcm);
    champ(o, "tampon", (long)cfg::R.tampon_ms);
    champ(o, "ampli", (long)cfg::R.ampli_bas);
    champ(o, "lim", (long)cfg::R.limiteur);
    champ(o, "suivre", (long)ui::suit_page_web());
    { int g, a, nu, ct; ui::options2(&g, &a, &nu, &ct); champ(o, "graves", (long)g); champ(o, "aigus", (long)a); champ(o, "nuit", (long)nu); champ(o, "cwtxt", (long)ct); }
    { int sq, sam, rep, vox, muet; ui::options(&sq, &sam, &rep, &vox, &muet); champ(o, "sq", (long)sq); champ(o, "sam", (long)sam); champ(o, "reprise", (long)rep); champ(o, "vox", (long)vox); }
    champ(o, "etiq", (long)cfg::R.etiquettes);
    champ(o, "sd", (long)plat_sd());
    champ(o, "sd_etat", plat_sd_etat());
    { char e[96]; geo::etiquette(e, sizeof e); champ(o, "pos", e); champ(o, "pos_etat", geo::etat()); champ(o, "pos_mode", (long)geo::mode()); }
    champf(o, "lat", cfg::R.lat, 4);
    champf(o, "lon", cfg::R.lon, 4);
    champ(o, "url_liste", cfg::R.url_liste);
    champ(o, "wifi", (long)plat_wifi_etat());
    champ(o, "reseau", plat_wifi_nom());
    champ(o, "ip", plat_wifi_ip());
    champ(o, "rssi_wifi", (long)plat_wifi_rssi());
    champ(o, "debit", (long)kw::debit_octets());
    champ(o, "coupures", (long)aud::coupures());
    champf(o, "ips", ui::images_par_seconde(), 1);
    champ(o, "tas", (long)(ESP.getFreeHeap() / 1024));
    champ(o, "psram", (long)(ESP.getFreePsram() / 1024));
    champ(o, "demarrage", plat_raison_demarrage());
    champ(o, "noaa", (long)plat_noaa());
    { float f, k; quoi::solaire(&f, &k); champf(o, "flux", f, 0); champf(o, "kp", k, 1); }
    champ(o, "duree", (long)(millis() / 1000));
    champ(o, "limite", (long)kw::limite_minutes());
    fin_objet(o);
    envoyer_json(o);
}

// ------------------------------------------------------------------ traducteur (programme Python sur le PC)
// le son est pris ici (deja traite par le KiwiSDR) : pas de 2e connexion au recepteur
static void api_son()
{
    // tampon sur le tas (PSRAM), lu directement : pas de tableau de 32 Ko en memoire interne
    const int NMAX = 16384;                           // ~1,3 s a 12 kHz, ~0,34 s a 48 kHz (FM)
    uint32_t c = (uint32_t)strtoul(W->arg("c").c_str(), nullptr, 10);
    if (c == 0) c = aud::curseur_brut();              // premier appel : on part de maintenant
    uint8_t *b = (uint8_t *)malloc(8 + NMAX * 2);
    if (!b) { W->send(503, "text/plain", "memoire"); return; }
    int n = aud::lire_brut(c, (int16_t *)(b + 8), NMAX);
    size_t taille = 8 + (size_t)n * 2;
    float fs = aud::debit();
    memcpy(b, &c, 4);
    memcpy(b + 4, &fs, 4);
    W->sendHeader("Cache-Control", "no-store");
    W->send_P(200, "application/octet-stream", (const char *)b, taille);
    free(b);
}

static const int NTRAD = 40;
static String s_trad[NTRAD];
static uint32_t s_ntrad = 0;                          // total recu (numero de la ligne suivante)
static uint32_t s_t_trad = 0;

static void api_trad_ajouter()
{
    String t = W->arg("t");
    if (t.length() > 600) t = t.substring(0, 600);
    String o = "{\"n\":" + String(s_ntrad) + ",\"heure\":";
    js(o, W->arg("h").c_str());
    o += ",\"langue\":";
    js(o, W->arg("l").c_str());
    o += ",\"orig\":";
    js(o, W->arg("o").c_str());
    o += ",\"fr\":";
    js(o, t.c_str());
    o += ",\"conf\":" + String(W->arg("c").toInt()) + "}";
    s_trad[s_ntrad % NTRAD] = o;
    s_ntrad++;
    s_t_trad = millis();
    ok();
}

static void api_trad()
{
    uint32_t depuis = (uint32_t)W->arg("n").toInt();
    if (s_ntrad > NTRAD && depuis < s_ntrad - NTRAD) depuis = s_ntrad - NTRAD;
    String o = "{\"total\":" + String(s_ntrad) + ",\"actif\":" + String(s_t_trad && millis() - s_t_trad < 60000 ? 1 : 0) + ",\"lignes\":[";
    for (uint32_t i = depuis; i < s_ntrad; i++) {
        if (i != depuis) o += ',';
        o += s_trad[i % NTRAD];
    }
    o += "]}";
    envoyer_json(o);
}

// ------------------------------------------------------------------ installation
void portail_init(WebServer &s)
{
    W = &s;
    s.on("/", HTTP_GET, [] {
        W->sendHeader("Cache-Control", "no-store");
        W->send_P(200, "text/html; charset=utf-8", PAGE_WEB);
    });
    s.on("/api/etat", HTTP_GET, api_etat);
    s.on("/api/cascade", HTTP_GET, api_cascade);
    s.on("/api/recepteurs", HTTP_GET, api_recepteurs);
    s.on("/api/connecter", HTTP_POST, api_connecter);
    s.on("/api/favoris", HTTP_GET, api_favoris);
    s.on("/api/fm/etat", HTTP_GET, api_fm_etat);
    s.on("/api/fm/serveurs", HTTP_GET, api_fm_serveurs);
    s.on("/api/vhf", HTTP_GET, api_vhf);
    s.on("/api/proches", HTTP_GET, api_proches);
    s.on("/api/canaux", HTTP_GET, api_canaux);
    s.on("/api/quoi", HTTP_GET, api_quoi);
    s.on("/api/journal", HTTP_GET, api_journal);
    s.on("/api/journal.csv", HTTP_GET, api_journal);
    s.on("/api/sauvegarde", HTTP_GET, api_sauvegarde);
    s.on("/api/restaurer", HTTP_POST, api_restaurer);
    s.on("/api/vhf/liste", HTTP_POST, [] { plat_vhf_liste_telecharger(); ok(); });
    s.on("/api/fm/liste", HTTP_POST, [] { plat_fm_liste_telecharger(); ok(); });
    s.on("/api/fm/connecter", HTTP_POST, [] {
        String u = W->arg("u"), n = W->arg("nom");
        u.trim();
        if (!u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
        if (u.length() > 98) { W->send(400, "text/plain", "Adresse trop longue"); return; }
        if (!n.length()) { const fml::ServeurFM *s = fml::serveur_url(u.c_str()); n = s ? s->nom : u; }
        n.replace("\t", " ");
        ui::commande(("fmco " + u + "\t" + n.substring(0, 46)).c_str());
        ok();
    });
    s.on("/api/fm/deconnecter", HTTP_POST, [] { ui::commande("fmdeco"); ok(); });
    s.on("/api/stations", HTTP_GET, api_stations);
    s.on("/api/stations.csv", HTTP_GET, api_stations_csv);
    s.on("/api/station", HTTP_POST, api_station_ecouter);
    s.on("/api/stations/recharger", HTTP_POST, [] {
        int n = sta::charger();
        String o = "{";
        champ(o, "nb", (long)n);
        champ(o, "source", sta::source());
        fin_objet(o);
        envoyer_json(o);
    });
    s.on("/api/stations/defaut", HTTP_POST, [] {
        int n = sta::reinitialiser();
        String o = "{";
        champ(o, "nb", (long)n);
        champ(o, "source", sta::source());
        fin_objet(o);
        envoyer_json(o);
    });
    s.on("/api/eibi", HTTP_GET, api_eibi);
    s.on("/api/eibi", HTTP_POST, [] {
        if (!kw::eibi_chercher(W->arg("q").c_str())) { W->send(409, "text/plain", "Connecte-toi d'abord à un récepteur"); return; }
        ok();
    });
    s.on("/api/dec", HTTP_GET, api_dec);
    s.on("/api/webradio", HTTP_GET, api_webradio);
    s.on("/api/fax", HTTP_GET, api_fax);
    s.on("/api/reglages", HTTP_GET, api_reglages);
    s.on("/api/son", HTTP_GET, api_son);
    s.on("/api/trad", HTTP_POST, api_trad_ajouter);
    s.on("/api/trad", HTTP_GET, api_trad);
    s.on("/api/cmd", HTTP_POST, [] {
        String o = W->arg("o");
        if (o.length() && o.length() < 120) ui::commande(o.c_str());
        ok();
    });
    s.on("/api/perso/ajouter", HTTP_POST, [] {
        if (srv::nb_perso() >= srv::MAX_PERSO) { W->send(400, "text/plain", "12 recepteurs perso au maximum"); return; }
        if (!srv::ajouter_perso(W->arg("n").c_str(), W->arg("a").c_str(), W->arg("p").c_str())) {
            W->send(400, "text/plain", "adresse non comprise");
            return;
        }
        ok();
    });
    s.on("/api/perso/retirer", HTTP_POST, [] { srv::retirer_perso(W->arg("i").toInt()); ok(); });
    s.on("/api/position", HTTP_POST, [] {
        String a = W->arg("la"), b = W->arg("lo");
        a.replace(',', '.');
        b.replace(',', '.');
        float la = a.toFloat(), lo = b.toFloat();
        if (la < -90 || la > 90 || lo < -180 || lo > 180) { W->send(400, "text/plain", "position"); return; }
        geo::manuel(la, lo);                             // enregistre, et recalcule les distances des trois listes
        ok();
    });
    s.on("/api/liste_url", HTTP_POST, [] {
        snprintf(cfg::R.url_liste, sizeof cfg::R.url_liste, "%s", W->arg("u").c_str());
        cfg::marquer();
        plat_liste_telecharger();
        ok();
    });
    s.on("/api/wifi", HTTP_POST, [] {
        ok();
        delay(300);
        plat_wifi_enregistrer(W->arg("s").c_str(), W->arg("p").c_str());
    });
    // mise a jour du programme : le fichier kiwi_console.ino.bin envoye par la page (onglet Systeme)
    s.on("/api/maj", HTTP_POST, [] {
        bool bon = s_maj_err[0] == 0 && s_maj_recu > 0 && !Update.hasError();
        W->sendHeader("Connection", "close");
        W->send(bon ? 200 : 400, "text/plain; charset=utf-8", bon ? "ok" : (s_maj_err[0] ? s_maj_err : "Mise à jour refusée"));
        if (bon) { ui::message("Mise à jour faite : redémarrage"); delay(1200); ESP.restart(); }
    }, maj_morceau);
    s.on("/api/redemarrer", HTTP_POST, [] {
        ok();
        delay(300);
        ESP.restart();
    });
    s.onNotFound([] {                                 // portail captif : tout ramene a l'accueil
        W->sendHeader("Location", "/");
        W->send(302);
    });
}
