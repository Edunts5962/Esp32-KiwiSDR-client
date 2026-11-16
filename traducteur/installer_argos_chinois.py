# Argos Translate : installe chinois -> anglais et anglais -> français
# (Argos passe par l'anglais pour aller du chinois au français).
# Lancer avant : python -m pip install argostranslate
import argostranslate.package as p

VOULUS = [("zh", "en"), ("en", "fr")]

p.update_package_index()
dispo = p.get_available_packages()
deja = {(x.from_code, x.to_code) for x in p.get_installed_packages()}
for de, vers in VOULUS:
    if (de, vers) in deja:
        print("Déjà installé :", de, "->", vers)
        continue
    paq = next((x for x in dispo if x.from_code == de and x.to_code == vers), None)
    if not paq:
        print("Paquet introuvable :", de, "->", vers)
        continue
    print("Installation", de, "->", vers, "...")
    p.install_from_path(paq.download())
print("Terminé : chinois -> français prêt (via l'anglais).")
