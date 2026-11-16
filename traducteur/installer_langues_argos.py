# Télécharge les paquets Argos Translate « langue -> anglais » et « anglais -> français »
# (Argos passe par l'anglais). Utilisé seulement pour la traduction hors ligne.
import argostranslate.package as p

LANGUES = ["es", "pt", "de", "it", "nl", "ru", "uk", "pl", "ro", "tr", "ar", "fa", "hi", "zh", "ja", "ko", "id", "el", "he"]
p.update_package_index()
dispo = p.get_available_packages()
voulus = [(l, "en") for l in LANGUES] + [("en", "fr")]
for de, vers in voulus:
    paq = next((x for x in dispo if x.from_code == de and x.to_code == vers), None)
    if paq:
        print("Installation", de, "->", vers)
        p.install_from_path(paq.download())
print("Langues Argos installées.")
