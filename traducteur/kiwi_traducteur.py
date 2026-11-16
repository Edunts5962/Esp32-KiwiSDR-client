#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KiwiTraducteur - traduction en français de ce que reçoit le client KiwiSDR (carte ES3C28P)

Lancé depuis l'onglet « Traduction » de la page web de la carte (lien kiwitrad://),
ou à la main :  python kiwi_traducteur.py --carte 192.168.1.145

Fonctionnement :
  1. le son est lu sur la carte (http://<carte>/api/son), déjà traité par le KiwiSDR
     (réduction de bruit, filtre) : aucune deuxième connexion au récepteur ;
  2. Whisper (faster-whisper, sur le PC) détecte la langue et transcrit la parole,
     avec une détection de voix pour ignorer le bruit et le silence ;
  3. le texte est traduit en français (Google en ligne, ou Argos hors ligne) ;
  4. la traduction s'affiche dans cette fenêtre et dans l'onglet Traduction de la page web.

Deux modes :
  Continu       : le son est réanalysé toutes les secondes ; les mots apparaissent en gris
                  (provisoires), sont confirmés quand deux analyses successives sont d'accord,
                  et chaque phrase confirmée est traduite (2 à 4 s de décalage) ;
  Par morceaux  : le son est découpé en morceaux de N secondes (plus précis, plus de décalage).

Installation : installer.bat (même dossier), une seule fois.
Options :
  --carte IP          adresse de la carte (sinon : la dernière utilisée)
  --installer         enregistre le lien kiwitrad:// dans Windows (sans droits administrateur)
  --desinstaller      retire le lien kiwitrad://
  --test-son          affiche le niveau du son reçu de la carte pendant 10 s, puis quitte
"""

VERSION = "v0.2.0-BETA"

import argparse
import datetime
import glob
import json
import math
import os
import queue
import re
import socket
import sys
import threading
import time
import urllib.parse

PORT_INSTANCE = 47123          # une seule fenêtre : un 2e lancement lui transmet l'ordre
FS_WHISPER = 16000
DOSSIER_CONFIG = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "KiwiTraducteur")
FICHIER_CONFIG = os.path.join(DOSSIER_CONFIG, "config.json")

LANGUES = [("Automatique", None), ("Anglais", "en"), ("Espagnol", "es"), ("Portugais", "pt"), ("Allemand", "de"),
           ("Italien", "it"), ("Néerlandais", "nl"), ("Russe", "ru"), ("Ukrainien", "uk"), ("Polonais", "pl"),
           ("Roumain", "ro"), ("Turc", "tr"), ("Arabe", "ar"), ("Persan", "fa"), ("Hindi", "hi"),
           ("Chinois", "zh"), ("Japonais", "ja"), ("Coréen", "ko"), ("Vietnamien", "vi"), ("Indonésien", "id"),
           ("Grec", "el"), ("Hébreu", "he"), ("Swahili", "sw")]
MODELES = ["tiny", "base", "small", "medium", "large-v3"]
MOTEURS = ["Google (Internet)", "Argos (hors ligne)"]
MODES = ["Continu", "Par morceaux"]

PAS_CONTINU = 1.0          # s de son nouveau entre deux analyses (mode continu)
MAX_PHRASE = 12.0          # s : une phrase sans ponctuation est traduite au bout de ce temps
MAX_TAMPON = 25.0          # s : au-delà, le PC ne suit pas, on lâche le plus ancien

# phrases que Whisper « invente » sur du bruit ou du silence (filtrées)
HALLUCINATIONS = [
    "merci d'avoir regardé", "sous-titres réalisés", "sous-titrage", "amara.org", "thank you for watching",
    "thanks for watching", "subtitles by", "please subscribe", "продолжение следует", "субтитры",
    "редактор субтитров", "ご視聴ありがとうございました", "字幕", "untertitel", "gracias por ver",
    "obrigado por assistir", "grazie per la visione", "www.", ".com",
]


# ============================================================ réglages
def charger_config():
    try:
        with open(FICHIER_CONFIG, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def sauver_config(c):
    try:
        os.makedirs(DOSSIER_CONFIG, exist_ok=True)
        with open(FICHIER_CONFIG, "w", encoding="utf-8") as f:
            json.dump(c, f, ensure_ascii=False, indent=1)
    except Exception:
        pass


# ============================================================ lien kiwitrad:// (Windows)
def commande_lancement():
    if getattr(sys, "frozen", False):                     # exécutable autonome (PyInstaller)
        return '"%s" "%%1"' % sys.executable
    py = sys.executable
    pyw = os.path.join(os.path.dirname(py), "pythonw.exe")
    if os.path.exists(pyw):
        py = pyw                                          # pas de fenêtre noire de console
    return '"%s" "%s" "%%1"' % (py, os.path.abspath(__file__))


def installer_lien():
    import winreg
    base = r"Software\Classes\kiwitrad"
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, base) as k:
        winreg.SetValueEx(k, "", 0, winreg.REG_SZ, "URL:KiwiTraducteur")
        winreg.SetValueEx(k, "URL Protocol", 0, winreg.REG_SZ, "")
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, base + r"\shell\open\command") as k:
        winreg.SetValueEx(k, "", 0, winreg.REG_SZ, commande_lancement())
    print("Lien kiwitrad:// installé :", commande_lancement())


def desinstaller_lien():
    import winreg
    for sous in (r"\shell\open\command", r"\shell\open", r"\shell", ""):
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\kiwitrad" + sous)
        except OSError:
            pass
    print("Lien kiwitrad:// retiré.")


def lire_arguments():
    p = argparse.ArgumentParser(description="KiwiTraducteur " + VERSION)
    p.add_argument("lien", nargs="?", help="kiwitrad://demarrer?carte=IP (passé par le navigateur)")
    p.add_argument("--carte")
    p.add_argument("--installer", action="store_true")
    p.add_argument("--desinstaller", action="store_true")
    p.add_argument("--test-son", action="store_true")
    a = p.parse_args()
    ordre, carte = "demarrer", a.carte
    if a.lien and a.lien.lower().startswith("kiwitrad:"):
        u = urllib.parse.urlparse(a.lien)
        ordre = (u.netloc or u.path.strip("/")).lower() or "demarrer"
        q = urllib.parse.parse_qs(u.query)
        if q.get("carte"):
            carte = q["carte"][0]
    return a, ordre, carte


# ============================================================ une seule fenêtre
def transmettre_a_l_instance(ordre, carte):
    """True si une fenêtre tourne déjà : on lui passe l'ordre et on s'arrête."""
    try:
        with socket.create_connection(("127.0.0.1", PORT_INSTANCE), timeout=1) as s:
            s.sendall(("%s %s\n" % (ordre, carte or "")).encode("utf-8"))
        return True
    except OSError:
        return False


def serveur_instance(file_ordres):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", PORT_INSTANCE))
    srv.listen(4)

    def boucle():
        while True:
            try:
                c, _ = srv.accept()
                with c:
                    ligne = c.recv(512).decode("utf-8", "replace").strip()
                    if ligne:
                        file_ordres.put(("ordre", ligne))
            except OSError:
                time.sleep(0.5)

    threading.Thread(target=boucle, daemon=True).start()
    return srv


# ============================================================ GPU NVIDIA (bibliothèques installées par pip)
def preparer_cuda():
    """Ajoute les DLL CUDA installées par pip (nvidia-cublas-cu12, nvidia-cudnn-cu12) au chemin de Windows."""
    if os.name != "nt":
        return
    for site in sys.path:
        for d in glob.glob(os.path.join(site, "nvidia", "*", "bin")):
            try:
                os.add_dll_directory(d)
                os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")
            except Exception:
                pass


# ============================================================ son venant de la carte
class Ecoute(threading.Thread):
    """Lit le son brut sur la carte et le passe à 16 kHz pour Whisper."""

    def __init__(self, app):
        super().__init__(daemon=True)
        self.app = app
        self.actif = threading.Event()
        self.verrou = threading.Lock()
        self.tampon = []          # morceaux numpy float32 à 16 kHz
        self.niveau = 0.0

    def vider(self):
        with self.verrou:
            self.tampon = []

    def prendre(self):
        import numpy as np
        with self.verrou:
            if not self.tampon:
                return np.zeros(0, dtype=np.float32)
            a = np.concatenate(self.tampon)
            self.tampon = []
            return a

    def remettre_devant(self, a):
        with self.verrou:
            self.tampon.insert(0, a)

    def duree(self):
        with self.verrou:
            return sum(len(x) for x in self.tampon) / FS_WHISPER

    def run(self):
        import numpy as np
        import requests
        curseur = 0
        t_etat = 0
        sess = requests.Session()
        while True:
            if not self.actif.is_set():
                curseur = 0
                self.actif.wait()
            carte = self.app.carte
            try:
                if time.time() - t_etat > 2:
                    t_etat = time.time()
                    e = sess.get("http://%s/api/etat" % carte, timeout=4).json()
                    self.app.evenement("etat", e)
                r = sess.get("http://%s/api/son" % carte, params={"c": curseur}, timeout=5)
                b = r.content
                if len(b) >= 8:
                    curseur = int.from_bytes(b[0:4], "little")
                    fs = float(np.frombuffer(b[4:8], dtype="<f4")[0]) or 12000.0
                    x = np.frombuffer(b[8:], dtype="<i2").astype(np.float32) / 32768.0
                    if len(x):
                        n = int(round(len(x) * FS_WHISPER / fs))
                        y = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x).astype(np.float32)
                        self.niveau = float(np.sqrt(np.mean(y * y)))
                        with self.verrou:
                            self.tampon.append(y)
                            # au plus 60 s en attente (si le PC est trop lent, on lâche l'ancien)
                            while sum(len(t) for t in self.tampon) > 60 * FS_WHISPER:
                                self.tampon.pop(0)
                self.app.evenement("connexion", True)
            except Exception as ex:
                self.app.evenement("connexion", "Carte injoignable (%s) : %s" % (carte, ex.__class__.__name__))
                time.sleep(2)
            time.sleep(0.3)


# ============================================================ reconnaissance + traduction
class Traduction(threading.Thread):
    def __init__(self, app, ecoute):
        super().__init__(daemon=True)
        self.app = app
        self.ecoute = ecoute
        self.modele = None
        self.nom_modele = None
        self.appareil = None
        self.dernier = ""
        self.contexte = ""        # derniers mots (aide Whisper à rester dans la même langue)
        self.file_trad = queue.Queue()
        threading.Thread(target=self.boucle_traduction, daemon=True).start()
        self.raz()

    # ---------------------------------------------------- état du mode continu
    def raz(self):
        import numpy as np
        self.audio = np.zeros(0, dtype=np.float32)   # son pas encore « coupé »
        self.t_buf = 0.0           # temps (s) du début de self.audio
        self.t_commit = 0.0        # fin du dernier mot confirmé
        self.precedent = []        # hypothèse non confirmée de la passe précédente
        self.attente = []          # mots confirmés pas encore traduits
        self.confirmes = []        # derniers mots confirmés (pour éviter les doublons)
        self.nouveau = 0           # échantillons reçus depuis la dernière analyse
        self.langue_auto = None
        self.calcul = 0.0
        self.mode_prec = None

    # ---------------------------------------------------- modèle
    def charger(self, nom, forcer_cpu=False):
        from faster_whisper import WhisperModel
        if self.modele is not None and self.nom_modele == nom and not forcer_cpu:
            return
        self.app.evenement("statut", "Chargement du modèle « %s » (le 1er usage le télécharge, patience)…" % nom)
        self.modele = None
        if not forcer_cpu:
            try:
                preparer_cuda()
                self.modele = WhisperModel(nom, device="cuda", compute_type="float16")
                self.appareil = "carte graphique NVIDIA"
            except Exception:
                self.modele = None
        if self.modele is None:
            # tous les cœurs sauf un (CTranslate2 n'en prend que 4 par défaut)
            coeurs = max(1, (os.cpu_count() or 4) - 1)
            self.modele = WhisperModel(nom, device="cpu", compute_type="int8", cpu_threads=coeurs)
            self.appareil = "processeur (%d cœurs)" % coeurs
        self.nom_modele = nom
        self.app.evenement("statut", "Modèle « %s » prêt sur le %s" % (nom, self.appareil))

    def transcrire(self, a, **kw):
        """Appelle Whisper ; bascule sur le processeur si la carte graphique échoue."""
        try:
            segments, info = self.modele.transcribe(a, task="transcribe", condition_on_previous_text=False,
                                                    vad_filter=True,
                                                    vad_parameters={"min_silence_duration_ms": 500}, **kw)
            return [s for s in segments], info
        except Exception as ex:
            if not self.appareil.startswith("processeur") and re.search(r"cuda|cublas|cudnn|dll", str(ex), re.I):
                self.app.evenement("statut", "Carte graphique inutilisable, passage au processeur…")
                self.charger(self.app.nom_modele, forcer_cpu=True)
                return None, None
            raise

    # ---------------------------------------------------- découpe : on coupe dans un creux de parole
    @staticmethod
    def point_de_coupe(a):
        import numpy as np
        n = len(a)
        fen = int(0.25 * FS_WHISPER)
        debut = max(0, n - 3 * FS_WHISPER)
        meilleur, pos = None, n
        for i in range(debut, n - fen, fen // 2):
            e = float(np.mean(a[i:i + fen] ** 2))
            if meilleur is None or e < meilleur:
                meilleur, pos = e, i + fen // 2
        return pos

    # ---------------------------------------------------- traduction du texte (fil séparé)
    def traduire(self, texte, langue):
        if not texte or langue == "fr":
            return texte
        moteur = self.app.moteur
        if moteur.startswith("Argos"):
            try:
                import argostranslate.translate as at
                if langue != "en":
                    en = at.translate(texte, langue, "en")
                else:
                    en = texte
                return at.translate(en, "en", "fr")
            except Exception as ex:
                return "(traduction hors ligne indisponible : %s)" % ex.__class__.__name__
        try:
            from deep_translator import GoogleTranslator
            src = langue if langue else "auto"
            try:
                return GoogleTranslator(source=src, target="fr").translate(texte)
            except Exception:
                return GoogleTranslator(source="auto", target="fr").translate(texte)
        except Exception as ex:
            return "(traduction indisponible : %s)" % ex.__class__.__name__

    def boucle_traduction(self):
        while True:
            item = self.file_trad.get()
            item["fr"] = self.traduire(item["orig"], item["langue"]) or ""
            self.app.evenement("ligne", item)

    def publier(self, texte, langue, proba, conf):
        """Phrase reconnue : filtre, puis envoi au fil de traduction."""
        texte = texte.strip()
        if self.est_invente(texte):
            return
        self.dernier = texte
        self.contexte = texte[-200:]
        self.file_trad.put({
            "heure": datetime.datetime.now().strftime("%H:%M:%S"),
            "langue": langue or "?", "proba": proba, "orig": texte, "conf": conf, "calcul": self.calcul,
        })

    def est_invente(self, t):
        b = t.lower().strip()
        if len(b) < 2 or not re.search(r"\w", b):      # vide, ou seulement « … … »
            return True
        if any(h in b for h in HALLUCINATIONS):
            return True
        if b == self.dernier.lower().strip():
            return True
        mots = b.split()
        if len(mots) >= 6 and len(set(mots)) <= 2:     # « la la la la la la »
            return True
        return False

    # ---------------------------------------------------- boucle
    def run(self):
        while True:
            if not self.ecoute.actif.is_set():
                if self.mode_prec is not None:
                    self.vider_attente(None, 0)
                    self.raz()
                    self.app.evenement("provisoire", "")
                time.sleep(0.3)
                continue
            try:
                self.charger(self.app.nom_modele)
            except Exception as ex:
                self.app.evenement("erreur", "Impossible de charger Whisper : %s\nLance installer.bat." % ex)
                self.app.evenement("arret", None)
                time.sleep(1)
                continue
            mode = self.app.mode
            if mode != self.mode_prec:
                self.raz()
                self.mode_prec = mode
                self.app.evenement("provisoire", "")
            try:
                if mode == "Continu":
                    self.pas_continu()
                else:
                    self.pas_morceau()
            except Exception as ex:
                self.app.evenement("erreur", "Reconnaissance : %s" % ex)
                self.raz()
                self.mode_prec = mode
                time.sleep(1)

    # ---------------------------------------------------- mode « par morceaux » (v0.1)
    def pas_morceau(self):
        import numpy as np
        if self.ecoute.duree() < self.app.duree_morceau:
            time.sleep(0.3)
            return
        a = self.ecoute.prendre()
        coupe = self.point_de_coupe(a)
        if coupe < len(a):
            self.ecoute.remettre_devant(a[coupe:])       # la fin attend le morceau suivant
            a = a[:coupe]
        if float(np.sqrt(np.mean(a * a))) < 0.003:       # silence total
            return
        t0 = time.time()
        segs, info = self.transcrire(a, language=self.app.langue, beam_size=3,
                                     initial_prompt=self.contexte or None)
        if segs is None:
            return
        self.calcul = time.time() - t0
        if not segs:
            self.app.evenement("statut", "Pas de voix détectée (bruit ou musique)")
            return
        texte = " ".join(s.text.strip() for s in segs).strip()
        lp = sum(s.avg_logprob for s in segs) / len(segs)
        ns = sum(s.no_speech_prob for s in segs) / len(segs)
        conf = int(max(0, min(100, 100 * math.exp(lp) * (1 - ns))))
        if ns > 0.7:
            return
        self.publier(texte, info.language, int(100 * (info.language_probability or 0)), conf)

    # ---------------------------------------------------- mode continu
    @staticmethod
    def norm(m):
        return re.sub(r"[^\w]", "", m.lower())

    def vider_attente(self, langue, proba, jusqua=None):
        """Envoie à la traduction les mots confirmés (tous, ou jusqu'à l'indice donné)."""
        if not self.attente:
            return
        n = len(self.attente) if jusqua is None else jusqua
        phrase, self.attente = self.attente[:n], self.attente[n:]
        texte = "".join(m[2] for m in phrase)
        conf = int(100 * sum(m[3] for m in phrase) / len(phrase))
        self.publier(texte, langue or self.langue_auto or self.app.langue, proba, conf)
        # le son de cette phrase n'est plus utile : on le retire du tampon
        fin = phrase[-1][1]
        if fin > self.t_buf:
            coupe = int((fin - self.t_buf) * FS_WHISPER)
            self.audio = self.audio[coupe:]
            self.t_buf = fin

    def pas_continu(self):
        import numpy as np
        x = self.ecoute.prendre()
        if len(x):
            self.audio = np.concatenate([self.audio, x])
            self.nouveau += len(x)
        if self.nouveau < PAS_CONTINU * FS_WHISPER:
            time.sleep(0.1)
            return
        self.nouveau = 0
        duree = len(self.audio) / FS_WHISPER
        t_fin = self.t_buf + duree

        # PC trop lent : on garde les 15 dernières secondes
        if duree > MAX_TAMPON:
            self.vider_attente(None, 0)
            coupe = len(self.audio) - int(15 * FS_WHISPER)
            if coupe > 0:
                self.audio = self.audio[coupe:]
                self.t_buf += coupe / FS_WHISPER
                self.t_commit = max(self.t_commit, self.t_buf)
            self.precedent = []
            self.app.evenement("statut", "Le PC ne suit pas : choisis un modèle plus petit (base ou tiny)")

        if float(np.sqrt(np.mean(self.audio[-FS_WHISPER * 3:] ** 2))) < 0.003 and not self.attente:
            self.precedent = []        # silence total
            self.couper_sans_voix(t_fin)
            self.app.evenement("provisoire", "")
            return

        langue = self.app.langue or self.langue_auto
        t0 = time.time()
        segs, info = self.transcrire(self.audio, language=langue, beam_size=1, word_timestamps=True,
                                     initial_prompt=self.contexte or None)
        if segs is None:
            return
        self.calcul = time.time() - t0
        if not self.app.langue and self.langue_auto is None and (info.language_probability or 0) > 0.7:
            self.langue_auto = info.language
        langue = self.app.langue or self.langue_auto or info.language
        proba = int(100 * (info.language_probability or 0))

        mots = []
        for s in segs:
            if s.no_speech_prob > 0.7:
                continue
            for w in (s.words or []):
                mots.append((self.t_buf + w.start, self.t_buf + w.end, w.word, w.probability))
        # on ne garde que ce qui suit le dernier mot confirmé
        mots = [m for m in mots if m[0] >= self.t_commit - 0.1]
        # Whisper répète parfois les derniers mots confirmés (même instant) : on les enlève
        if mots and self.confirmes:
            for k in range(min(5, len(mots), len(self.confirmes)), 0, -1):
                if mots[k - 1][0] < self.t_commit and \
                        [self.norm(m[2]) for m in self.confirmes[-k:]] == [self.norm(m[2]) for m in mots[:k]]:
                    mots = mots[k:]
                    break

        # accord entre cette passe et la précédente : le début commun est confirmé
        n = 0
        while n < len(mots) and n < len(self.precedent) and self.norm(mots[n][2]) == self.norm(self.precedent[n][2]):
            n += 1
        valides = mots[:n]
        self.precedent = mots[n:]
        if valides:
            self.attente.extend(valides)
            self.confirmes = (self.confirmes + valides)[-10:]
            self.t_commit = valides[-1][1]

        # une phrase terminée (. ? !) part à la traduction
        fin_phrase = None
        for i, m in enumerate(self.attente):
            if m[2].strip()[-1:] in ".?!。？！":
                fin_phrase = i + 1
        if fin_phrase:
            self.vider_attente(langue, proba, fin_phrase)
        # phrase trop longue, ou plus personne ne parle : on traduit ce qu'on a
        if self.attente and (not mots or t_fin - self.attente[0][0] > MAX_PHRASE):
            if not mots:
                self.precedent = []
            self.vider_attente(langue, proba)
        if not mots and not self.attente:
            self.couper_sans_voix(t_fin)

        prov = "".join(m[2] for m in self.attente + self.precedent).strip()
        self.app.evenement("provisoire", prov)
        self.app.evenement("statut", "Continu · analyse en %.1f s%s" % (
            self.calcul, "  (lent : essaie un modèle plus petit)" if self.calcul > 2.5 else ""))

    def couper_sans_voix(self, t_fin):
        """Pas de parole : on ne garde que la dernière seconde de son."""
        if t_fin - self.t_buf > 4:
            coupe = len(self.audio) - FS_WHISPER
            self.audio = self.audio[coupe:]
            self.t_buf += coupe / FS_WHISPER
            self.t_commit = max(self.t_commit, self.t_buf)
            self.langue_auto = None if not self.app.langue else self.langue_auto


# ============================================================ fenêtre
class Appli:
    def __init__(self, carte, file_ordres):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        cfg = charger_config()
        self.carte = carte or cfg.get("carte", "")
        self.nom_modele = cfg.get("modele", "small")
        self.langue = cfg.get("langue")
        self.moteur = cfg.get("moteur", MOTEURS[0])
        self.duree_morceau = float(cfg.get("duree", 8))
        self.mode = cfg.get("mode", "Continu") if cfg.get("mode") in MODES else "Continu"
        self.file = file_ordres
        self.lignes = []
        self.etat_radio = {}

        r = self.racine = tk.Tk()
        r.title("KiwiTraducteur %s" % VERSION)
        r.geometry("1180x680")
        r.configure(bg="#0B1220")
        st = ttk.Style()
        try:
            st.theme_use("clam")
        except Exception:
            pass
        for w in ("TFrame", "TLabel", "TCheckbutton"):
            st.configure(w, background="#0B1220", foreground="#E6EDF7")
        st.configure("TButton", background="#1A2438", foreground="#E6EDF7", padding=6)
        st.map("TButton", background=[("active", "#0EA5E9")])
        st.configure("Go.TButton", background="#0EA5E9", foreground="#06121F")

        haut = ttk.Frame(r, padding=8)
        haut.pack(fill="x")
        ttk.Label(haut, text="Carte").pack(side="left")
        self.v_carte = tk.StringVar(value=self.carte)
        ttk.Entry(haut, textvariable=self.v_carte, width=18).pack(side="left", padx=6)
        self.b_go = ttk.Button(haut, text="▶ Démarrer", style="Go.TButton", command=self.demarrer)
        self.b_go.pack(side="left", padx=3)
        ttk.Button(haut, text="■ Arrêter", command=self.arreter).pack(side="left", padx=3)
        ttk.Button(haut, text="Effacer", command=self.effacer).pack(side="left", padx=3)
        ttk.Button(haut, text="Enregistrer…", command=self.enregistrer).pack(side="left", padx=3)
        self.l_radio = ttk.Label(haut, text="", foreground="#F59E0B")
        self.l_radio.pack(side="right")

        opt = ttk.Frame(r, padding=(8, 0, 8, 6))
        opt.pack(fill="x")
        ttk.Label(opt, text="Langue parlée").pack(side="left")
        self.v_langue = tk.StringVar(value=next((n for n, c in LANGUES if c == self.langue), "Automatique"))
        cb = ttk.Combobox(opt, textvariable=self.v_langue, values=[n for n, _ in LANGUES], width=13, state="readonly")
        cb.pack(side="left", padx=6)
        cb.bind("<<ComboboxSelected>>", lambda e: self.options())
        ttk.Label(opt, text="Modèle").pack(side="left", padx=(10, 0))
        self.v_modele = tk.StringVar(value=self.nom_modele)
        cb = ttk.Combobox(opt, textvariable=self.v_modele, values=MODELES, width=9, state="readonly")
        cb.pack(side="left", padx=6)
        cb.bind("<<ComboboxSelected>>", lambda e: self.options())
        ttk.Label(opt, text="Traduction").pack(side="left", padx=(10, 0))
        self.v_moteur = tk.StringVar(value=self.moteur)
        cb = ttk.Combobox(opt, textvariable=self.v_moteur, values=MOTEURS, width=17, state="readonly")
        cb.pack(side="left", padx=6)
        cb.bind("<<ComboboxSelected>>", lambda e: self.options())
        ttk.Label(opt, text="Mode").pack(side="left", padx=(10, 0))
        self.v_mode = tk.StringVar(value=self.mode)
        cb = ttk.Combobox(opt, textvariable=self.v_mode, values=MODES, width=12, state="readonly")
        cb.pack(side="left", padx=6)
        cb.bind("<<ComboboxSelected>>", lambda e: self.options())
        ttk.Label(opt, text="Morceaux (s)").pack(side="left", padx=(10, 0))
        self.v_duree = tk.IntVar(value=int(self.duree_morceau))
        sp = ttk.Spinbox(opt, from_=4, to=20, textvariable=self.v_duree, width=4, command=self.options)
        sp.pack(side="left", padx=6)
        self.v_orig = tk.BooleanVar(value=cfg.get("original", True))
        ttk.Checkbutton(opt, text="Texte original", variable=self.v_orig, command=self.options).pack(side="left", padx=10)

        cadre = ttk.Frame(r, padding=(8, 0, 8, 0))
        cadre.pack(fill="both", expand=True)
        self.texte = tk.Text(cadre, bg="#050A14", fg="#E6EDF7", insertbackground="#E6EDF7", wrap="word",
                             font=("Segoe UI", 13), relief="flat", padx=10, pady=8)
        barre = ttk.Scrollbar(cadre, command=self.texte.yview)
        self.texte.configure(yscrollcommand=barre.set)
        barre.pack(side="right", fill="y")
        self.texte.pack(fill="both", expand=True)
        self.texte.tag_configure("info", foreground="#8FA3BF", font=("Segoe UI", 9))
        self.texte.tag_configure("orig", foreground="#7C8BA3", font=("Segoe UI", 10, "italic"))
        self.texte.tag_configure("fr", foreground="#E6EDF7", font=("Segoe UI", 14))
        self.texte.tag_configure("douteux", foreground="#8FA3BF", font=("Segoe UI", 13, "italic"))
        self.texte.insert("end", "Clique sur Démarrer (ou sur « Lancer le traducteur » dans la page web de la carte).\n"
                                 "La traduction s'affiche ici, phrase par phrase, avec quelques secondes de décalage.\n"
                                 "En mode Continu, les mots en cours de reconnaissance apparaissent en bas, en gris.\n"
                                 "Les phrases en gris italique sont douteuses (signal faible ou bruité).\n\n", "info")
        self.texte.configure(state="disabled")

        self.l_prov = tk.Label(r, text="", bg="#0B1220", fg="#7C8BA3", font=("Segoe UI", 12, "italic"),
                               anchor="w", justify="left", wraplength=980, padx=16, pady=4)
        self.l_prov.pack(side="bottom", fill="x", before=cadre)
        r.bind("<Configure>", lambda e: self.l_prov.configure(wraplength=max(200, r.winfo_width() - 40))
               if e.widget is r else None)

        bas = ttk.Frame(r, padding=8)
        bas.pack(side="bottom", fill="x", before=self.l_prov)
        self.l_statut = ttk.Label(bas, text="Prêt", foreground="#8FA3BF")
        self.l_statut.pack(side="left")
        self.vu = tk.Canvas(bas, width=120, height=10, bg="#22304A", highlightthickness=0)
        self.vu.pack(side="right")
        ttk.Label(bas, text="Son reçu ").pack(side="right")

        self.ecoute = Ecoute(self)
        self.trad = Traduction(self, self.ecoute)
        self.ecoute.start()
        self.trad.start()
        r.protocol("WM_DELETE_WINDOW", self.quitter)
        r.after(100, self.boucle)

    # ---- évènements venant des autres fils (passent par la file)
    def evenement(self, genre, valeur):
        self.file.put((genre, valeur))

    def options(self):
        self.langue = dict(LANGUES).get(self.v_langue.get())
        self.nom_modele = self.v_modele.get()
        self.moteur = self.v_moteur.get()
        self.mode = self.v_mode.get()
        try:
            self.duree_morceau = float(self.v_duree.get())
        except Exception:
            pass
        sauver_config({"carte": self.carte, "modele": self.nom_modele, "langue": self.langue,
                       "moteur": self.moteur, "duree": self.duree_morceau, "original": self.v_orig.get(),
                       "mode": self.mode})

    def demarrer(self, carte=None):
        if carte:
            self.v_carte.set(carte)
        self.carte = self.v_carte.get().strip()
        if not self.carte:
            self.statut("Entre l'adresse de la carte (Menu › WiFi sur la carte), par ex. 192.168.1.145")
            return
        self.options()
        self.ecoute.vider()
        self.ecoute.actif.set()
        self.statut("Écoute de la carte %s…" % self.carte)
        self.devant()

    def arreter(self):
        self.ecoute.actif.clear()
        self.statut("Arrêté")

    def effacer(self):
        self.texte.configure(state="normal")
        self.texte.delete("1.0", "end")
        self.texte.configure(state="disabled")
        self.lignes = []

    def enregistrer(self):
        from tkinter import filedialog
        nom = filedialog.asksaveasfilename(defaultextension=".txt", initialfile="traduction_%s.txt" %
                                           datetime.datetime.now().strftime("%Y%m%d-%H%M"))
        if not nom:
            return
        with open(nom, "w", encoding="utf-8") as f:
            for l in self.lignes:
                f.write("[%s] (%s) %s\n    %s\n" % (l["heure"], l["langue"], l["orig"], l["fr"]))
        self.statut("Enregistré : %s" % nom)

    def devant(self):
        r = self.racine
        r.deiconify()
        r.lift()
        r.attributes("-topmost", True)
        r.after(400, lambda: r.attributes("-topmost", False))

    def statut(self, t):
        self.l_statut.configure(text=t)

    def ajouter(self, l):
        self.lignes.append(l)
        t = self.texte
        t.configure(state="normal")
        douteux = l["conf"] < 45
        t.insert("end", "%s · %s%s · confiance %d %%\n" % (l["heure"], l["langue"],
                 " (%d %%)" % l["proba"] if l["proba"] else "", l["conf"]), "info")
        if self.v_orig.get() and l["langue"] != "fr":
            t.insert("end", l["orig"] + "\n", "orig")
        t.insert("end", l["fr"] + "\n\n", "douteux" if douteux else "fr")
        t.see("end")
        t.configure(state="disabled")
        # copie dans l'onglet Traduction de la page web de la carte
        def envoyer():
            try:
                import requests
                requests.post("http://%s/api/trad" % self.carte, timeout=3, data={
                    "t": l["fr"][:600], "o": l["orig"][:300], "l": l["langue"], "h": l["heure"], "c": l["conf"]})
            except Exception:
                pass
        threading.Thread(target=envoyer, daemon=True).start()

    def boucle(self):
        try:
            while True:
                genre, v = self.file.get_nowait()
                if genre == "ligne":
                    self.ajouter(v)
                    if self.mode != "Continu":
                        self.statut("Écoute… (dernière phrase calculée en %.1f s)" % v["calcul"])
                elif genre == "provisoire":
                    self.l_prov.configure(text=v[-400:] if v else "")
                elif genre == "statut":
                    self.statut(v)
                elif genre == "erreur":
                    self.statut(v.splitlines()[0])
                    from tkinter import messagebox
                    messagebox.showerror("KiwiTraducteur", v)
                elif genre == "arret":
                    self.arreter()
                elif genre == "connexion":
                    if v is not True:
                        self.statut(v)
                elif genre == "etat":
                    self.etat_radio = v
                    modes = ["AM", "LSB", "USB", "CW", "FM"]
                    if v.get("etat") == 3:
                        self.l_radio.configure(text="%s · %.2f kHz %s" % (v.get("nom", ""), v.get("f", 0),
                                                                          modes[v.get("mode", 0) % 5]))
                    else:
                        self.l_radio.configure(text=v.get("message", ""))
                elif genre == "ordre":
                    mots = v.split(" ", 1)
                    if mots[0] == "arreter":
                        self.arreter()
                    else:
                        self.demarrer(mots[1].strip() if len(mots) > 1 and mots[1].strip() else None)
        except queue.Empty:
            pass
        n = min(1.0, self.ecoute.niveau * 8) if self.ecoute.actif.is_set() else 0
        self.vu.delete("all")
        self.vu.create_rectangle(0, 0, int(120 * n), 10, fill="#22C55E", width=0)
        self.racine.after(150, self.boucle)

    def quitter(self):
        self.options()
        self.racine.destroy()
        os._exit(0)


# ============================================================ test du son
def test_son(carte):
    import numpy as np
    import requests
    c = 0
    print("Son reçu de la carte %s (Ctrl+C pour arrêter) :" % carte)
    t0 = time.time()
    while time.time() - t0 < 10:
        b = requests.get("http://%s/api/son" % carte, params={"c": c}, timeout=5).content
        c = int.from_bytes(b[0:4], "little")
        fs = float(np.frombuffer(b[4:8], dtype="<f4")[0])
        x = np.frombuffer(b[8:], dtype="<i2").astype(np.float32) / 32768
        niv = float(np.sqrt(np.mean(x * x))) if len(x) else 0
        print("  %5d échantillons à %.0f Hz   niveau %s" % (len(x), fs, "#" * int(min(40, niv * 400))))
        time.sleep(0.5)


# ============================================================ départ
def main():
    a, ordre, carte = lire_arguments()
    if a.installer:
        installer_lien()
        return
    if a.desinstaller:
        desinstaller_lien()
        return
    if a.test_son:
        test_son(carte or charger_config().get("carte", "192.168.4.1"))
        return
    if transmettre_a_l_instance(ordre, carte):     # une fenêtre est déjà ouverte
        return
    file_ordres = queue.Queue()
    serveur_instance(file_ordres)
    app = Appli(carte, file_ordres)
    if ordre == "demarrer" and (carte or app.carte) and a.lien:
        app.racine.after(300, lambda: app.demarrer(carte))
    app.racine.mainloop()


if __name__ == "__main__":
    main()
