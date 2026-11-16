@echo off
chcp 65001 >nul
echo === Installation de KiwiTraducteur ===
cd /d "%~dp0"
python --version || (echo Python introuvable : installe-le depuis python.org en cochant "Add python.exe to PATH". & pause & exit /b 1)
python -m pip install --upgrade pip
python -m pip install -r requirements.txt || (echo Echec de l installation des bibliotheques. & pause & exit /b 1)
echo.
set /p GPU="Carte graphique NVIDIA ? Installer l acceleration (environ 1 Go) [o/N] : "
if /i "%GPU%"=="o" python -m pip install -r requirements-gpu.txt
set /p HL="Traduction hors ligne Argos (environ 1 Go avec les langues) [o/N] : "
if /i "%HL%"=="o" (python -m pip install -r requirements-hors-ligne.txt && python installer_langues_argos.py)
python kiwi_traducteur.py --installer
echo.
echo Termine. Dans la page web de la carte : onglet Traduction, bouton "Lancer le traducteur".
echo Au premier lancement, Whisper telecharge son modele (quelques centaines de Mo).
pause
