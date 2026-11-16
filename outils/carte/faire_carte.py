#!/usr/bin/env python3
# faire_carte.py - fabrique carte_monde.h : la carte du monde (terre / mer) de l'ecran "Carte".
# Un point tous les 0,25 degre (1440 x 720), 1 bit par point : 129 600 octets dans le programme.
# Source : paquet Python global-land-mask (licence MIT ; donnees GLOBE de la NOAA, domaine public).
#   pip install global-land-mask numpy
#   python3 faire_carte.py > ../../firmware/kiwi_console/carte_monde.h
import numpy as np
from global_land_mask import globe

L, H = 1440, 720
lat = 90 - (np.arange(H) + 0.5) * 180.0 / H
lon = -180 + (np.arange(L) + 0.5) * 360.0 / L
LON, LAT = np.meshgrid(lon, lat)
terre = globe.is_land(LAT, LON)
bits = np.packbits(terre.astype(np.uint8), axis=1, bitorder='big')      # 90 octets par ligne
print("// carte_monde.h - carte du monde terre / mer, fabriquee par outils/carte/faire_carte.py (ne pas modifier a la main)")
print("// %d x %d points (0,25 degre), 1 bit par point, bit de poids fort a l'ouest ; ligne 0 au nord." % (L, H))
print("// Donnees : global-land-mask (MIT), d'apres le modele GLOBE de la NOAA (domaine public).")
print("#pragma once\n#include <stdint.h>\n")
print("const int CARTE_L = %d, CARTE_H = %d;" % (L, H))
print("static const uint8_t CARTE_TERRE[%d] = {" % bits.size)
plat = bits.flatten()
for i in range(0, plat.size, 45):
    print("    " + ",".join(str(int(v)) for v in plat[i:i + 45]) + ",")
print("};")
