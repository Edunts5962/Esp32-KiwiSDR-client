// stations_liste.h - liste de stations integree au firmware (saison B26, hiver 2026/27)
//
// Copiee sur la carte SD dans /kiwi/stations.csv au premier demarrage : tu peux
// ensuite modifier ce fichier (une station par ligne), puis Stations > Recharger.
//
// Format : nom;kHz;mode;langue;pays;latitude;longitude;infos
//   mode   : AM, LSB, USB, CW ou FM
//   langue : en, fr, zh, es, ja... ; "--" pour les signaux sans parole (horaires, météo)
//   lat/lon de l'emetteur (degres decimaux) : sert a trouver le recepteur le plus proche
//   infos  : horaires en UTC (ondes courtes) ou description
// Sources : guides BDXC B26 (novembre 2026), sites des stations.
#pragma once

static const char STATIONS_INTEGREES[] = R"csv(# Stations KiwiSDR - nom;kHz;mode;langue;pays;lat;lon;infos
# --- Informations en continu, ondes moyennes (Amérique du Nord) ---
1010 WINS New York;1010;AM;en;USA;40.80;-74.11;Infos en continu 24 h/24
KYW Newsradio Philadelphie;1060;AM;en;USA;40.10;-75.25;Infos en continu 24 h/24
WBZ NewsRadio Boston;1030;AM;en;USA;42.28;-70.88;Infos et débats
WBBM Newsradio Chicago;780;AM;en;USA;41.98;-88.03;Infos en continu 24 h/24
WWJ Newsradio Detroit;950;AM;en;USA;42.20;-83.35;Infos en continu 24 h/24
KCBS San Francisco;740;AM;en;USA;38.13;-122.30;Infos en continu 24 h/24
KNX News Los Angeles;1070;AM;en;USA;33.86;-118.35;Infos en continu 24 h/24
KRLD Dallas;1080;AM;en;USA;32.85;-96.95;Infos et débats
WLW Cincinnati;700;AM;en;USA;39.35;-84.33;Infos et débats (grande portée la nuit)
WGN Chicago;720;AM;en;USA;42.00;-88.00;Débats et infos (grande portée la nuit)
# --- BBC World Service (anglais) ---
BBC World Service;639;AM;en;Chypre;34.62;32.95;6h-6h30, 14h30-15h, 15h30-16h UTC
BBC World Service;720;AM;en;Chypre;34.62;32.95;6h-6h30, 14h30-15h, 15h30-16h UTC
BBC World Service;9410;AM;en;Ascension;-7.95;-14.38;5h-7h UTC (Afrique de l'Ouest)
BBC World Service;9915;AM;en;Ascension;-7.95;-14.38;5h-7h UTC (Afrique centrale)
BBC World Service;12095;AM;en;Oman;21.92;59.62;15h-17h UTC lun-sam (Afrique de l'Est)
BBC World Service;15400;AM;en;Ascension;-7.95;-14.38;18h-19h UTC (Afrique de l'Ouest)
BBC World Service;15420;AM;en;Ascension;-7.95;-14.38;18h-19h UTC (Afrique centrale)
BBC World Service;17780;AM;en;Ascension;-7.95;-14.38;17h-18h UTC lun-ven
BBC World Service;21565;AM;en;Madagascar;-18.70;47.50;12h-14h UTC (Asie du Sud-Est)
BBC World Service;11645;AM;en;Madagascar;-18.70;47.50;22h-0h UTC (Asie du Sud-Est)
# --- Autres stations en anglais ---
WRMI Radio Miami International;9395;AM;en;USA;27.45;-80.93;24 h/24 (Amérique du Nord)
WRMI Radio Miami International;7730;AM;en;USA;27.45;-80.93;24 h/24 (est de l'Amérique du Nord)
WRMI Radio Miami International;15770;AM;en;USA;27.45;-80.93;24 h/24 (vers l'Europe)
WWCR Nashville;4840;AM;en;USA;36.21;-86.89;1h-13h UTC
WWCR Nashville;13845;AM;en;USA;36.21;-86.89;13h-1h UTC
Deutsche Welle (via WRMI);7780;AM;en;USA;27.45;-80.93;7h-8h et 11h-12h UTC lun-sam
Radio Romania International;15460;AM;en;Roumanie;44.75;26.08;12h-13h UTC
Radio Romania International;7220;AM;en;Roumanie;44.75;26.08;4h-5h et 23h-0h UTC
Radio Romania International;7375;AM;en;Roumanie;44.75;26.08;21h30-22h UTC
CGTN Radio (Chine) anglais;9730;AM;en;Chine;39.90;116.40;11h-12h UTC
CGTN Radio (Chine) anglais;5955;AM;en;Chine;39.90;116.40;12h-13h UTC
CGTN Radio (Chine) anglais;9560;AM;en;Chine;39.90;116.40;0h-1h UTC
NHK World Radio Japan;11920;AM;en;Japon;36.17;139.83;14h-14h29 UTC lun-ven
Radio Vatican anglais;9705;AM;en;Vatican;42.05;12.33;20h-20h30 UTC
Radio Vatican anglais;15565;AM;en;Vatican;42.05;12.33;16h30-17h UTC
RNZ Pacific;15720;AM;en;Nouvelle-Zélande;-38.83;176.42;6h-6h30 UTC lun-ven
# --- Français ---
RFI;9790;AM;fr;France;46.95;1.90;4h-5h, 19h-20h, 21h-22h UTC
RFI;11700;AM;fr;France;46.95;1.90;4h-5h, 6h-7h, 19h-20h UTC
RFI;13740;AM;fr;France;46.95;1.90;6h-7h, 19h-20h, 22h-23h UTC
RFI;15300;AM;fr;France;46.95;1.90;8h-9h, 19h-20h UTC
RFI;17660;AM;fr;France;46.95;1.90;12h-13h UTC
RFI;21580;AM;fr;France;46.95;1.90;8h-9h, 12h-13h UTC
Radio Roumanie Internationale;9720;AM;fr;Roumanie;44.75;26.08;17h-18h UTC
Radio Roumanie Internationale;7375;AM;fr;Roumanie;44.75;26.08;21h-21h30 UTC
Radio Roumanie Internationale;17800;AM;fr;Roumanie;44.75;26.08;11h-12h UTC
CGTN Radio (Chine) français;6115;AM;fr;Chine;39.90;116.40;20h30-22h30 UTC
CGTN Radio (Chine) français;7350;AM;fr;Chine;39.40;76.00;16h-17h UTC
Radio Vatican français;9705;AM;fr;Vatican;42.05;12.33;20h30-21h UTC
Radio Vatican français;15565;AM;fr;Vatican;42.05;12.33;17h-17h30 UTC
Médi 1 (Radio Méditerranée Int.);171;AM;fr;Maroc;35.03;-2.92;Grandes ondes, français et arabe
# --- Chinois ---
CNR-1 La Voix de la Chine;6000;AM;zh;Chine;39.90;116.40;20h25-23h30 et 11h-18h05 UTC
CNR-1 La Voix de la Chine;7290;AM;zh;Chine;39.90;116.40;20h25-1h et 10h-18h05 UTC
CNR-1 La Voix de la Chine;9830;AM;zh;Chine;39.90;116.40;20h25-1h et 7h30-18h05 UTC
CNR-1 La Voix de la Chine;11760;AM;zh;Chine;38.04;114.51;0h-12h UTC
CNR-1 La Voix de la Chine;17565;AM;zh;Chine;39.90;116.40;1h-7h30 UTC
CGTN Radio (Chine) chinois;11900;AM;zh;Chine;39.90;116.40;0h-1h UTC
CGTN Radio (Chine) chinois;7390;AM;zh;Chine;39.90;116.40;12h-13h UTC
CGTN Radio (Chine) chinois;5975;AM;zh;Chine;39.90;116.40;22h-23h UTC
# --- Autres langues ---
NHK Radio Japon (japonais);9750;AM;ja;Japon;36.17;139.83;10h-13h50 UTC
NHK Radio Japon (japonais);11640;AM;ja;Japon;36.17;139.83;0h-1h UTC
Radio Exterior de España;9690;AM;es;Espagne;39.95;-3.43;19h-3h UTC lun-ven, 15h-0h sam-dim
Radio Exterior de España;12030;AM;es;Espagne;39.95;-3.43;16h-0h UTC
Radio Exterior de España;17775;AM;es;Espagne;39.95;-3.43;16h-0h UTC
Polskie Radio Jedynka;225;AM;pl;Pologne;53.02;18.26;Grandes ondes
Antena Satelor;153;AM;ro;Roumanie;45.76;25.65;Grandes ondes
Chaîne 1 Algérie;153;AM;ar;Algérie;31.62;-2.22;Grandes ondes, arabe
# --- Signaux horaires ---
WWV signal horaire;2500;AM;--;USA;40.68;-105.04;24 h/24, annonce en anglais
WWV signal horaire;5000;AM;--;USA;40.68;-105.04;24 h/24
WWV signal horaire;10000;AM;--;USA;40.68;-105.04;24 h/24
WWV signal horaire;15000;AM;--;USA;40.68;-105.04;24 h/24
WWV signal horaire;20000;AM;--;USA;40.68;-105.04;24 h/24
WWVH signal horaire;5000;AM;--;USA (Hawaï);21.99;-159.76;24 h/24 (même fréquence que WWV)
WWVH signal horaire;10000;AM;--;USA (Hawaï);21.99;-159.76;24 h/24
BPM signal horaire;10000;AM;--;Chine;34.95;109.55;24 h/24
DCF77 horloge;77.5;CW;--;Allemagne;50.02;9.01;Ondes longues, horloges radio-pilotées
MSF horloge;60;CW;--;Royaume-Uni;54.91;-3.28;Ondes longues
JJY horloge;40;CW;--;Japon;37.37;140.85;Ondes longues
# --- Météo et radioamateurs ---
Shannon Volmet (météo aviation);5505;USB;en;Irlande;52.75;-8.93;24 h/24, anglais
New York Volmet (météo aviation);6604;USB;en;USA;40.80;-72.90;24 h/24, anglais
DWD météo marine RTTY;10100.8;USB;--;Allemagne;53.67;9.80;Décodeur RTTY
DWD carte météo FAX;7878.1;USB;--;Allemagne;53.67;9.80;Décodeur FAX
FT8 radioamateurs 40 m;7074;USB;--;Monde;;;Décodeur FT8 (sur PC)
FT8 radioamateurs 20 m;14074;USB;--;Monde;;;Décodeur FT8 (sur PC)
)csv";
