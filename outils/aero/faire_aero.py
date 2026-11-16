#!/usr/bin/env python3
"""Fabrique la liste des frequences d'aerodromes (bande aviation 108-137 MHz).

Source : OurAirports (domaine public), https://github.com/davidmegginson/ourairports-data
  python3 faire_aero.py airports.csv airport-frequencies.csv

Produit :
  ../../firmware/kiwi_console/aero_liste.h   Europe, integree au firmware
  ../../carte_sd/kiwi/aero.txt               monde entier (facultatif : a copier sur la carte SD)

Format texte (une ligne par aerodrome, puis une par frequence) :
  A<TAB>lat<TAB>lon<TAB>code<TAB>nom
  F<TAB>kHz<TAB>type
"""
import csv, os, sys

ICI = os.path.dirname(os.path.abspath(__file__))
TYPES = {  # types OurAirports -> libelle court en francais
    'TWR': 'Tour', 'APP': 'Approche', 'GND': 'Sol', 'ATIS': 'ATIS', 'AFIS': 'AFIS', 'INFO': 'Info',
    'CTAF': 'Auto-info', 'UNIC': 'Auto-info', 'A/A': 'Auto-info', 'A/G': 'Air-sol', 'DEP': 'Départ', 'ARR': 'Arrivée',
    'CLD': 'Clairance', 'DEL': 'Clairance', 'CNTR': 'Centre', 'ACC': 'Centre', 'RDR': 'Radar', 'AWOS': 'Météo',
    'ASOS': 'Météo', 'VOLMET': 'VOLMET', 'FSS': 'Info vol', 'RDO': 'Radio', 'RADIO': 'Radio', 'OPS': 'Opérations',
    'MISC': 'Divers', 'FIS': 'Info vol', 'TML': 'Approche', 'PAL': 'Radio', 'ATF': 'Auto-info', 'RCO': 'Radio',
}

def nettoyer(t, n):
    t = ' '.join((t or '').replace('\t', ' ').split())
    return t[:n]

def lire(fa, ff):
    ap = {}
    for r in csv.DictReader(open(fa, encoding='utf-8')):
        if r['type'] == 'closed':
            continue
        ap[r['id']] = r
    par_ap = {}
    for r in csv.DictReader(open(ff, encoding='utf-8')):
        a = ap.get(r['airport_ref'])
        if not a:
            continue
        try:
            f = float(r['frequency_mhz'])
        except ValueError:
            continue
        if not 108.0 <= f <= 137.0:
            continue
        t = r['type'].upper().strip()
        lib = TYPES.get(t, nettoyer(r['description'], 12) or t.title())
        par_ap.setdefault(a['id'], (a, []))[1].append((round(f * 1000, 3), lib))
    return par_ap

def texte(par_ap, filtre):
    lignes = []
    n = 0
    for a, fr in sorted(par_ap.values(), key=lambda x: x[0]['ident']):
        if not filtre(a):
            continue
        code = a['icao_code'] or a['gps_code'] or a['ident']
        lignes.append('A\t%.4f\t%.4f\t%s\t%s' % (float(a['latitude_deg']), float(a['longitude_deg']), nettoyer(code, 7), nettoyer(a['name'], 30)))
        vu = set()
        for khz, lib in sorted(fr):
            if (khz, lib) in vu:
                continue
            vu.add((khz, lib))
            lignes.append('F\t%g\t%s' % (khz, lib))
            n += 1
    return '\n'.join(lignes) + '\n', n

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    par_ap = lire(sys.argv[1], sys.argv[2])
    europe, ne = texte(par_ap, lambda a: a['continent'] == 'EU')
    monde, nm = texte(par_ap, lambda a: True)
    h = os.path.join(ICI, '..', '..', 'firmware', 'kiwi_console', 'aero_liste.h')
    with open(h, 'w', encoding='utf-8') as o:
        o.write('// aero_liste.h - frequences des aerodromes d\'Europe (bande aviation)\n')
        o.write('// Genere par outils/aero/faire_aero.py depuis OurAirports (domaine public). Ne pas modifier a la main.\n')
        o.write('#pragma once\n// (const : reste en memoire flash sur l ESP32)\n')
        o.write('static const char AERO_EUROPE[] = R"AERO(')
        o.write(europe)
        o.write(')AERO";\n')
    sd = os.path.join(ICI, '..', '..', 'carte_sd', 'kiwi')
    os.makedirs(sd, exist_ok=True)
    with open(os.path.join(sd, 'aero.txt'), 'w', encoding='utf-8') as o:
        o.write(monde)
    print('Europe : %d frequences, %d octets ; monde : %d frequences, %d octets' % (ne, len(europe.encode()), nm, len(monde.encode())))

if __name__ == '__main__':
    main()
