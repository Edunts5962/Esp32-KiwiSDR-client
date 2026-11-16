// page_web.h - page unique du portail web (HTML + CSS + JavaScript)
// Dix onglets : Radio, Récepteurs, Stations, Quoi écouter, FM, Favoris, Décodeurs, Réglages, Système, Traduction.
// Elle ne fait que lire l'etat de la carte et lui envoyer des ordres (/api/...).
#pragma once
#include <pgmspace.h>

static const char PAGE_WEB[] PROGMEM = R"PAGE(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Client KiwiSDR</title>
<style>
:root{--f:#0B1220;--p:#121A2B;--l:#22304A;--b:#1A2438;--t:#E6EDF7;--t2:#8FA3BF;--c:#38BDF8;--bl:#0EA5E9;--a:#F59E0B;--v:#22C55E;--r:#EF4444}
*{box-sizing:border-box}body{margin:0;background:var(--f);color:var(--t);font:15px system-ui,sans-serif}
header{display:flex;align-items:center;gap:10px;padding:8px 12px;background:var(--p);border-bottom:1px solid var(--l);position:sticky;top:0;z-index:5;flex-wrap:wrap}
header b{font-size:16px}.ver{color:var(--c);font-size:13px}
nav{display:flex;gap:4px;padding:6px 8px;background:var(--p);border-bottom:1px solid var(--l);overflow-x:auto;position:sticky;top:41px;z-index:4}
nav button{flex:0 0 auto}
main{max-width:980px;margin:auto;padding:10px}
section{display:none}section.on{display:block}
.c{background:var(--p);border:1px solid var(--l);border-radius:10px;padding:12px;margin:8px 0}
button,.btn{background:var(--b);color:var(--t);border:1px solid var(--l);border-radius:8px;padding:9px 14px;font-size:15px;font-weight:600;cursor:pointer}
button:active{background:#33415c;border-color:var(--c)}
button.on{background:var(--bl);color:#06121F;border-color:var(--bl)}
button.pr{background:var(--bl);color:#06121F;border:0}button.dg{background:var(--r);color:#fff;border:0}
button.big{font-size:30px;padding:6px 0;width:90px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.g{flex:1}
.m{color:var(--t2);font-size:13px}.dot{width:10px;height:10px;border-radius:5px;display:inline-block;background:var(--t2)}
.chip{font-size:12px;font-weight:700;padding:1px 6px;border-radius:5px;background:#14532D;color:#86EFAC}.chip.c2{background:#1E3A5F;color:#7DD3FC}
@keyframes clig{50%{opacity:.3}}.clig{animation:clig .6s steps(1) infinite}
#jrnl{position:absolute;inset:0;background:#0B1220;padding:10px 12px;font-size:14px;line-height:1.7;overflow:hidden}
#jrnl .ok{color:#22C55E}#jrnl .ko{color:#EF4444}#jrnl .en{color:#F59E0B}#jrnl .tko{color:#F59E0B}
#freq{font:700 46px ui-monospace,Consolas,monospace;color:var(--a);cursor:pointer;letter-spacing:-1px}
#mode{font-weight:700;padding:3px 10px;border-radius:6px;background:#1E3A5F;color:#7DD3FC}
.sm{display:flex;gap:2px}.sm i{width:9px;height:16px;border-radius:2px;background:var(--l)}
#wfbox{position:relative;width:100%;background:#000;border-radius:8px;overflow:hidden;cursor:crosshair}
#spec,#wf{display:block;width:100%;image-rendering:pixelated}#spec{height:70px;background:#0d1526}#wf{height:260px}
#mk{position:absolute;top:0;bottom:0;width:2px;background:var(--a);pointer-events:none}
#pb{position:absolute;top:0;bottom:0;background:rgba(245,158,11,.18);pointer-events:none}
.st{display:inline-block;margin:3px;padding:3px 8px;border-radius:6px;background:#2a2410;color:#ffe6a0;font-size:13px;cursor:pointer}
input,select{background:var(--f);color:var(--t);border:1px solid var(--l);border-radius:8px;padding:8px;font-size:15px}
input[type=range]{width:100%;padding:0}
label{display:block;color:var(--t2);font-size:13px;margin:8px 0 3px}
.sb{padding:5px 9px;font-size:13px;white-space:nowrap}.fq{min-width:74px;text-align:right}.fq b{color:var(--a)}
.li{display:flex;align-items:center;gap:10px;padding:9px;border-bottom:1px solid var(--l);cursor:pointer}.li:hover{background:#18233a}
.li .g b{display:block}
pre{background:#050A14;color:#4ADE80;border-radius:8px;padding:10px;min-height:140px;white-space:pre-wrap;font-size:16px;margin:0}
table{width:100%;border-collapse:collapse}td{padding:6px;border-bottom:1px solid var(--l)}td:last-child{text-align:right}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#1e2940;border:1px solid var(--c);padding:10px 16px;border-radius:10px;display:none;z-index:9}
</style></head><body>
<header><span id="dot" class="dot"></span><b id="rx">Aucun récepteur</b><span id="chips"></span><span class="g"></span><span class="m" id="res"></span><span class="ver" id="ver"></span></header>
<nav id="nav"></nav>
<main>
<section id="t0">
 <div class="c">
  <div class="row"><span id="freq" title="Cliquer pour saisir">—</span><span class="m" id="unite">kHz</span><span id="mode">—</span><span class="m" id="bande"></span>
   <span class="g"></span><button id="fav" title="Favori">☆</button>
   <div><div class="sm" id="sm"></div><div class="m" id="dbm" style="text-align:right"></div></div></div>
  <div class="row" style="margin-top:8px"><button class="big" onclick="cmd('moins')">−</button><button class="big" onclick="cmd('plus')">+</button><button class="big" id="scb" title="Scan vers le bas" onclick="cmd('scan -1')">◀◀</button><button class="big" id="sch" title="Scan vers le haut" onclick="cmd('scan 1')">▶▶</button>
   <div class="g"><div class="m">Pas</div><div class="row" id="pas"></div></div></div>
  <div class="row" style="margin-top:6px"><button onclick="cmd('meilleur');toast('Recherche du meilleur récepteur : suis l\'écran de la carte')">Meilleur récepteur pour cette fréquence</button><span class="m">ondes courtes : compare le récepteur écouté à 4 KiwiSDR libres (30 s environ) et garde le meilleur</span></div>
  <div class="row" style="margin-top:6px"><span class="m">Sensibilité du scan</span><div class="row" id="scsens"></div><span class="g"></span><b id="scst" style="color:#F59E0B;cursor:pointer" title="Arrêter le scan" onclick="cmd('scan 0')"></b></div>
  <div class="row" style="margin-top:8px" id="modes"></div>
 </div>
 <div class="c" id="profbox" style="display:none"><b>Bande du récepteur</b> <span class="m">(OpenWebRX : la bande change aussi pour les autres auditeurs)</span><div class="row" id="profs" style="margin-top:8px"></div></div>
 <div id="wfbox"><canvas id="spec" width="320" height="70"></canvas><canvas id="wf" width="320" height="120"></canvas><div id="pb"></div><div id="mk"></div><div id="jrnl" style="display:none"></div><div id="wfnon" style="display:none;position:absolute;left:8%;right:8%;top:45%;background:#121a2be8;border:1px solid #22304a;border-radius:8px;padding:8px;text-align:center;color:#F59E0B;font-weight:700"></div></div>
 <div class="row" style="margin-top:6px"><span class="m" id="span"></span><span class="g"></span><span class="m">Zoom</span><button onclick="cmd('zoom -1')">−</button><button onclick="cmd('zoom 1')">+</button>
  <button id="rec" onclick="cmd('rec '+(E.rec?0:1))">● REC</button></div>
 <div class="c" id="stbox"><div class="m">Stations à l'écran (noms envoyés par le KiwiSDR) — cliquer pour écouter</div><div id="st"></div></div>
 <div class="c"><b>Audio</b>
  <label>Volume <span id="volv"></span></label><input type="range" min="0" max="100" id="vol" onchange="cmd('vol '+this.value)">
  <label>AGC</label><div class="row" id="agc"></div>
  <label><span id="agsl">Seuil de l'AGC</span> <span id="agsv"></span></label><input type="range" id="ags" step="5" onchange="cmd((E.agc?'agcseuil ':'gainman ')+this.value)">
  <label>Réduction de bruit</label><div class="row" id="nr"></div>
  <div class="row" style="margin-top:10px"><button id="notch" onclick="cmd('notch '+(E.notch?0:1))">Anti-sifflement</button><button id="lim" onclick="cmd('lim '+(E.lim?0:1))">Limiteur</button></div>
  <label>Anti-parasites (KiwiSDR)</label><div class="row" id="nbs"></div>
  <label>Gain du son KiwiSDR (si le son est trop faible)</label><div class="row" id="gain"></div>
  <label>Bande passante <span id="bpv"></span></label><input type="range" id="bp" step="100" onchange="cmd('reg bp '+this.value)"><div class="row" style="margin-top:6px"><button onclick="cmd('bporig 0');toast('Bande passante d\'origine pour ce mode')">Origine (ce mode)</button><button onclick="if(confirm('Remettre la bande passante d\'origine de tous les modes ?')){cmd('bporig 1');toast('Bandes passantes d\'origine')}">Origine (tous les modes)</button></div>
 </div>
 <div class="c" id="bandesbox"><b>Bandes</b><div class="row" id="bandes" style="margin-top:8px"></div></div>
</section>
<section id="t1">
 <div class="c"><div class="row" id="rtype"></div></div>
 <div id="rprox" style="display:none">
 <div class="c"><b>Ma position</b> : <span id="ppos">…</span> <button onclick="cmd('geoip');toast('Recherche de la position…');setTimeout(prox,2500)">Localiser</button>
  <div class="m">Les 5 récepteurs les plus proches de chaque type. La position se règle dans l'onglet Réglages (par Internet, par nom de ville ou à la main).</div></div>
 <div class="c"><b>Ondes courtes (KiwiSDR, place libre)</b><div id="pk"></div></div>
 <div class="c"><b>VHF / UHF (OpenWebRX)</b><div id="pv"></div></div>
 <div class="c"><b>Radio FM (FM-DX)</b><div id="pf"></div></div>
 </div>
 <div id="rvhf" style="display:none">
 <div class="c"><div class="row"><input id="vq" class="g" type="text" placeholder="🔍 Rechercher : nom, ville, adresse" oninput="clearTimeout(RQ);RQ=setTimeout(vhf,300)"><button onclick="$('vq').value='';vhf()">✕</button><button onclick="post('/api/vhf/liste',{}).then(()=>setTimeout(vhf,4000))">↻ Actualiser</button></div>
 <div class="m" id="vetat"></div><p class="m">Récepteurs OpenWebRX qui captent au-dessus de 30 MHz (liste de receiverbook.de), triés par distance. Chaque récepteur propose ses bandes : choisis-la ensuite dans l'onglet Radio.</p></div>
 <div class="c" id="vliste"></div>
 <div class="c"><b>Écouter une adresse OpenWebRX</b><div class="row"><input id="vadr" class="g" placeholder="https://sdr.exemple.org/ ou hote:8073"><button class="pr" onclick="cmd('owrx '+v('vadr'));go(0)">Écouter</button></div></div>
 </div>
 <div id="rkiwi">
 <div class="c"><div class="row"><input id="rq" class="g" type="text" placeholder="🔍 Rechercher : nom, ville, pays, indicatif (ex. Vermont, K1VL)" oninput="clearTimeout(RQ);RQ=setTimeout(rec,300)"><button id="rqx" onclick="$('rq').value='';rec()">✕</button></div>
 <div class="row" style="margin-top:8px"><div class="row g" id="regions"></div><button onclick="cmd('liste');setTimeout(rec,1500)">↻ Actualiser</button></div><div class="m" id="letat"></div></div>
 <div class="c" id="liste"></div>
 <div class="c"><b>Écouter une adresse</b><div class="row"><input id="adr" class="g" placeholder="monkiwi.ddns.net:8073"><input id="adm" placeholder="mot de passe"><button class="pr" onclick="post('/api/connecter',{a:v('adr'),mdp:v('adm')}).then(()=>go(0))">Écouter</button></div></div>
 <div class="c"><b>Ajouter un récepteur perso</b><div class="row"><input id="pn" placeholder="Nom"><input id="pa" class="g" placeholder="adresse:port"><input id="pp" placeholder="mot de passe"><button onclick="post('/api/perso/ajouter',{n:v('pn'),a:v('pa'),p:v('pp')}).then(()=>{toast('Ajouté');R=0;rec()})">Ajouter</button></div></div>
 </div>
</section>
<section id="t7">
 <div class="c"><div class="row" id="stype"></div></div>
 <div id="svhf" style="display:none">
 <div class="c"><div class="row"><input id="cq" class="g" type="text" placeholder="🔍 Aérodrome, canal, fréquence (ex. Nantes, ATIS, canal 16)" oninput="clearTimeout(SQ);SQ=setTimeout(can,250)"><button onclick="$('cq').value='';can()">✕</button></div>
  <div class="row" style="margin-top:8px"><div class="row g" id="ccat"></div><button class="pr" id="cscan" onclick="cmd('scanliste '+[15,1,2,4,8][CC]);setTimeout(can,400)">▶ Scanner</button></div>
  <label class="m" style="display:block;margin-top:6px"><input type="checkbox" id="cici" checked onchange="CI=this.checked;can()"> Seulement les canaux que ce récepteur capte</label><div class="m" id="cetat"></div>
  <p class="m">Fréquences communes (détresse, marine, radioamateurs, PMR446) et aérodromes à moins de 200 km du récepteur, du plus proche au plus lointain. Point vert : dans la bande en cours. Point bleu : dans une autre bande du récepteur (la carte change de bande toute seule). Point gris : ce récepteur ne le capte pas. Le scan passe en revue les canaux de la bande en cours et s'arrête sur celui qui parle. Écoute tolérée : ne rien enregistrer ni divulguer.</p></div>
 <div class="c" id="cliste"></div>
 </div>
 <div id="shf">
 <div class="c"><div class="row"><input id="sq" class="g" type="text" placeholder="🔍 Station, pays, langue ou fréquence (ex. WINS, BBC, RFI, 9790)" oninput="clearTimeout(SQ);SQ=setTimeout(sta,250)"><button onclick="$('sq').value='';sta()">✕</button></div>
  <div class="row g" id="slg" style="margin-top:8px"></div><div class="m" id="setat"></div></div>
 <div class="c" id="sliste"></div>
 <div class="c"><b>Recherche EiBi sur le récepteur connecté</b><div class="m">Toutes les émissions à l'antenne en ce moment (ondes courtes surtout), d'après la base EiBi du récepteur. Quelques secondes.</div>
  <div class="row"><input id="eq" class="g" type="text" placeholder="ex. BBC, France, China, Romania" onkeydown="if(event.key=='Enter')eibi()"><button class="pr" onclick="eibi()">Chercher</button></div>
  <div class="m" id="eetat"></div><div id="eliste"></div></div>
 <div class="c"><b>Ma liste de stations</b><div class="m" id="ssrc"></div>
  <div class="row"><button onclick="location.href='/api/stations.csv'">⭳ Télécharger (CSV)</button><button onclick="post('/api/stations/recharger',{}).then(r=>r.json()).then(d=>{toast(d.nb+' stations ('+d.source+')');sta()})">↻ Recharger depuis la SD</button><button onclick="if(confirm('Remplacer le fichier de la carte SD par la liste d\'origine ?'))post('/api/stations/defaut',{}).then(r=>r.json()).then(d=>{toast(d.nb+' stations');sta()})">Liste d'origine</button></div>
  <div class="m">Pour ajouter tes stations : modifie <b>/kiwi/stations.csv</b> sur la carte SD (une ligne par station : nom;kHz;mode;langue;pays;latitude;longitude;infos), remets la carte, puis « Recharger ».</div></div>
 </div>
</section>
<section id="tq">
 <div class="c"><div class="row"><div class="row g" id="qcat"></div><button class="pr" id="qmes" onclick="cmd('quoimes '+QC);setTimeout(quoi,500)">Mesurer</button></div>
  <div class="m" id="qctx" style="margin-top:6px"></div>
  <p class="m">Ce qu'il y a de plus intéressant à écouter maintenant. Sans mesure, c'est une estimation : elle part de l'heure, de la saison et de la position du récepteur (les bandes basses portent loin la nuit, les bandes hautes le jour). Les indices du jour (flux solaire et Kp de la NOAA), quand la carte a pu les lire, sont affichés et pris en compte. « Mesurer » compte les signaux réellement présents : sur un KiwiSDR, le waterfall visite chaque bande pendant 10 à 20 secondes (le son continue) ; sur un récepteur OpenWebRX, seule la bande en cours est mesurée. Touche une ligne pour l'écouter : si le récepteur en cours ne la capte pas, la carte se connecte au récepteur adapté le plus proche.</p></div>
 <div class="c" id="qliste"></div>
</section>
<section id="t8">
 <div class="c" id="wrc"><div class="row"><span id="wrdot" class="dot"></span><b class="g">Webradios</b><span class="m" id="wrmsg"></span><button id="wrstop" onclick="cmd('wrstop');setTimeout(wrE,300)">■ Arrêter</button></div>
  <div id="wrjoue" style="margin:6px 0"></div>
  <div class="row"><input id="wrn" class="g" placeholder="🔍 Nom de la radio (ex. FIP, France Inter, BBC)" onkeydown="if(event.key=='Enter')wrC()"><input id="wrp" style="width:60px" placeholder="Pays" title="Code du pays : FR, BE, CH, CA…" onkeydown="if(event.key=='Enter')wrC()"><input id="wrg" style="width:110px" placeholder="Genre (jazz…)" onkeydown="if(event.key=='Enter')wrC()"><button class="pr" onclick="wrC()">Chercher</button></div>
  <div class="row" style="margin-top:6px" id="wrr"></div>
  <div id="wrl" style="margin-top:6px"></div>
  <div class="m">Annuaire libre radio-browser.info · formats MP3 et AAC (pas HLS) · ★ = favori, aussi sur l'écran de la carte (Menu › Radio FM › Webradios).</div></div>
 <div class="c">
  <div class="row"><span id="fmdot" class="dot"></span><b class="g" id="fmsrv">Aucun serveur FM</b><span class="m" id="fmmsg"></span></div>
  <div class="row" style="align-items:baseline;margin-top:6px"><span id="fmf" style="font-size:44px;font-weight:800;color:var(--a)">––,–</span><span>MHz</span><span class="chip" id="fmst" style="display:none">STÉRÉO</span><span class="g"></span><span id="fmps" style="font-size:26px;font-weight:800;letter-spacing:2px"></span></div>
  <div id="fmrt" style="min-height:20px"></div><div class="m" id="fmtx"></div>
  <div class="row" style="margin:6px 0"><span class="m">Signal</span><div class="g" style="height:8px;background:var(--l);border-radius:4px"><div id="fmsig" style="height:8px;width:0;background:#22C55E;border-radius:4px"></div></div><span class="m" id="fmsigv" style="min-width:60px"></span></div>
  <div class="row"><button onclick="fmPas(-1)">−1</button><button onclick="fmPas(-0.1)">−0,1</button><input id="fmin" style="width:80px" placeholder="98,5" onkeydown="if(event.key=='Enter')fmAller()"><button class="pr" onclick="fmAller()">Aller</button><button onclick="fmPas(0.1)">+0,1</button><button onclick="fmPas(1)">+1</button></div>
 <div class="row" style="margin-top:8px"><button class="big" id="fsb" title="Autoscan vers le bas" onclick="cmd('fmscan -1');setTimeout(fmE,300)">◀◀</button><button class="big" id="fsh" title="Autoscan vers le haut" onclick="cmd('fmscan 1');setTimeout(fmE,300)">▶▶</button><b id="fsst" class="g" style="color:#F59E0B;cursor:pointer" title="Arrêter le scan" onclick="cmd('fmscan 0');setTimeout(fmE,300)"></b></div>
 <div class="m">Autoscan : re-cliquer sur le même bouton = plus vite (vitesses 1 à 4), le texte orange = arrêt. Le tuner est partagé : les autres auditeurs entendent aussi le scan.</div>
 <div id="fstbox" style="display:none;margin-top:8px"><div class="row"><b class="g">Stations trouvées</b><button onclick="cmd('fmtefface');setTimeout(fmE,300)">Effacer</button></div><div id="fst"></div></div>
  <div class="row" style="margin-top:8px"><span class="m">Volume</span><input type="range" id="fmvol" min="0" max="100" class="g" onchange="cmd('vol '+this.value)"><button onclick="post('/api/fm/deconnecter',{}).then(fmE)">■ Arrêter</button></div>
  <div class="m" id="fmaud"></div></div>
 <div class="c"><div class="row"><input id="fq" class="g" type="text" placeholder="🔍 Serveur FM : ville, pays, nom (ex. Paris, Bordeaux, France)" oninput="clearTimeout(FQ);FQ=setTimeout(fmS,300)"><button onclick="$('fq').value='';fmS()">✕</button><button title="Mettre à jour la liste" onclick="post('/api/fm/liste',{}).then(()=>setTimeout(fmS,1500))">↻</button></div><div class="m" id="fmletat"></div></div>
 <div class="c" id="fmliste"></div>
 <div class="c"><b>Écouter une adresse</b><div class="row"><input id="fmu" class="g" placeholder="https://monserveur.fmtuner.org"><button class="pr" onclick="fmCo(v('fmu'),'')">Écouter</button></div>
  <div class="m">Ces tuners FM sont partagés : changer la fréquence la change pour tous les auditeurs. Sur un serveur verrouillé 🔒, on peut seulement écouter.</div></div>
</section>
<section id="t2">
 <div class="c"><div class="row"><b class="g">Mes favoris</b><button onclick="cmd('scanfav');toast('Scan des favoris');setTimeout(()=>go(0),300)">▶ Scanner</button><button class="pr" onclick="cmd('favadd');setTimeout(favs,600)">★ Ajouter la station actuelle</button></div><div id="favs"></div></div>
 <div class="c"><b>Préréglages · stations fixes</b><div id="pres"></div></div>
 <div class="c"><div class="row"><b class="g">Journal d'écoute</b><a class="btn" href="/api/journal.csv"><button>Télécharger (CSV)</button></a><button class="dg" onclick="if(confirm('Vider le journal ?')){cmd('journalvide');setTimeout(jrn,500)}">Vider</button></div>
  <div class="m" style="margin:6px 0">Toute fréquence écoutée au moins 30 secondes est notée. Les 60 dernières sont ici ; avec une carte SD, tout l'historique est dans <b>/kiwi/journal.csv</b> (s'ouvre dans un tableur). <span id="jsd"></span></div>
  <div id="jrn"></div></div>
</section>
<section id="t3">
 <div class="c"><div class="row" id="dm"></div>
  <div class="row" style="margin:8px 0"><button id="i1" onclick="cmd('decvit')"></button><button id="i2" onclick="cmd('decshift')"></button><span id="cale" class="m"></span></div>
  <div id="ft8z" style="display:none"><div class="row" id="ft8b"></div><div class="m" id="ft8e" style="margin:6px 0"></div>
   <div style="max-height:420px;overflow:auto"><table id="ft8t" style="width:100%;font-size:13px;border-collapse:collapse"></table></div></div>
  <pre id="dtxt"></pre><canvas id="fax" width="320" height="240" style="display:none;width:100%;background:#000;border-radius:8px"></canvas>
  <div class="row" style="margin-top:8px"><button onclick="cmd('decclr')">Effacer / Nouvelle</button><button onclick="cmd('deccal')">Calage</button><button id="inv" onclick="cmd('decinv')">Inverser</button><button class="dg" onclick="cmd('decsave')">Sauver SD</button></div>
  <div class="m" style="margin-top:6px">CW : mode CW · RTTY : USB ou LSB · FAX : USB, 1,9 kHz sous la fréquence annoncée · FT8 : USB, sur les fréquences FT8 (décodage toutes les 15 s, heure Internet)</div></div>
</section>
<section id="t4">
 <div class="c"><b>Écoute</b>
  <label>Squelch (silence quand personne ne parle)</label><div class="row" id="sqs"></div>
  <label>AM synchrone (KiwiSDR) — une seule bande évite un brouilleur collé d'un côté</label><div class="row" id="sams"></div>
  <div class="row" style="margin-top:8px"><button id="orep" onclick="cmd('reprise '+(S.reprise?0:1));setTimeout(regl,300)">Reprise automatique</button><button id="ovox" onclick="cmd('vox '+(S.vox?0:1));setTimeout(regl,300)">Enregistrer à la voix</button><button id="onoaa" onclick="cmd('noaa '+(S.noaa?1:0));setTimeout(regl,300)">Propagation du jour (NOAA)</button><button id="ocw" onclick="cmd('cwtxt '+(S.cwtxt?0:1));setTimeout(regl,300)">Texte morse sur l'écran Radio</button></div>
  <div class="m" id="noaae" style="margin-top:6px"></div>
  <label>Graves <span id="gv"></span></label><input type="range" id="gr" min="-8" max="8" step="2" oninput="$('gv').textContent=this.value+' dB'" onchange="cmd('graves '+this.value)">
  <label>Aigus <span id="av"></span></label><input type="range" id="ar" min="-8" max="8" step="2" oninput="$('av').textContent=this.value+' dB'" onchange="cmd('aigus '+this.value)">
  <label>Mode nuit (écran atténué, heure et fréquence en grand, après un moment sans toucher)</label><div class="row" id="nus"></div>
  <div class="m" style="margin-top:6px">Squelch : la carte regarde le waterfall à l'endroit du filtre et coupe le haut-parleur quand rien ne dépasse le bruit (Bas : s'ouvre facilement ; Haut : signaux nets seulement). AM synchrone : moins de distorsion quand une station d'ondes courtes s'évanouit. Reprise automatique : si la connexion tombe, la carte se reconnecte, puis passe sur le KiwiSDR libre le plus proche. Enregistrer à la voix : le fichier WAV ne contient que les passages où quelqu'un parle, avec un journal .txt (heure, fréquence, durée).</div></div>
 <div class="c"><b>Écran et waterfall</b>
  <label>Luminosité <span id="lumv"></span></label><input type="range" min="5" max="100" id="lum" onchange="cmd('reg lum '+this.value)">
  <label>Vitesse du waterfall</label><div class="row" id="wfv"></div>
  <label>Palette</label><div class="row" id="pal"></div>
  <label>Noms des stations sur le waterfall</label><div class="row" id="etiq"></div>
  <div class="row" style="margin-top:12px"><button onclick="if(confirm('Lancer l\'étalonnage du tactile sur l\'écran de la carte ?'))cmd('etalon')">Étalonner l'écran tactile</button></div></div>
 <div class="c"><b>Son</b>
  <label>Réserve audio <span id="tamv"></span> ms</label><input type="range" min="150" max="1500" step="50" id="tampon" onchange="cmd('reg tampon '+this.value)">
  <div class="row" style="margin-top:10px"><button id="adpcm" onclick="cmd('reg adpcm '+(S.adpcm?0:1))">Audio compressé</button><button id="ampli" onclick="cmd('reg ampli '+(S.ampli?0:1))">Ampli actif bas</button><button id="lim2" onclick="cmd('lim '+(S.lim?0:1))">Limiteur</button></div></div>
 <div class="c"><b>Écran de la carte</b><div class="row"><button id="suivre" onclick="cmd('suivre '+(S.suivre?0:1))">L'écran de la carte suit cette page</button></div><div class="m">Quand c'est actif, changer d'onglet ici change aussi l'écran de la carte (Radio, Récepteurs, FM, Favoris…).</div></div>
 <div class="c"><b>Carte SD</b><div class="row"><span id="sde" class="g"></span><button class="dg" id="sdf" onclick="if(confirm('Formater la carte SD en FAT32 ? Tout son contenu sera effacé.'))cmd('sdformat')">Formater</button></div></div>
</section>
<section id="t5">
 <div class="c"><table id="sys"></table><div class="row" style="margin-top:10px"><button class="dg" onclick="if(confirm('Redémarrer la carte ?'))post('/api/redemarrer',{})">Redémarrer la carte</button></div></div>
 <div class="c"><b>Ma position</b> <span class="m">(tri des récepteurs par distance)</span>
  <div style="margin:6px 0"><b id="posl">…</b></div>
  <div class="row"><button class="pr" onclick="cmd('geoip');toast('Recherche de la position…');setTimeout(regl,2500)">Trouver par Internet</button><input id="pville" class="g" placeholder="ou une ville : Lyon, Brest…" onkeydown="if(event.key=='Enter')pvil()"><button onclick="pvil()">Chercher</button></div>
  <div class="m" style="margin:6px 0">Par Internet : la ville de ta connexion, parfois 20 à 50 km à côté (fausse avec un VPN ou une box 4G). Une ville choisie ou une position saisie reste prioritaire. Ou saisis la latitude et la longitude :</div><div class="row"><input id="la" placeholder="latitude"><input id="lo" placeholder="longitude"><button onclick="post('/api/position',{la:v('la'),lo:v('lo')}).then(()=>toast('Position enregistrée'))">Enregistrer</button></div></div>
 <div class="c"><b>Mise à jour du programme</b>
  <div class="m" style="margin:6px 0">Choisis le fichier <b>kiwi_console.ino.bin</b> du dossier <b>binaires</b> (pas « merged », pas « bootloader »), puis Envoyer. Compte une minute environ. La carte redémarre toute seule sur la nouvelle version ; si l'envoi échoue, l'ancienne reste en place. Réglages, favoris et WiFi sont conservés.</div>
  <div class="row"><input type="file" id="majf" accept=".bin" class="g"><button class="pr" id="majb" onclick="maj()">Envoyer</button></div>
  <div class="m" id="maje"></div></div>
 <div class="c"><b>Sauvegarde des réglages et des favoris</b>
  <div class="m" style="margin:6px 0">Le fichier contient les réglages, les favoris, les récepteurs personnels, la position et les mémoires de bandes. Il ne contient pas le WiFi ni son mot de passe. Restaurer remplace les réglages actuels, puis la carte redémarre.</div>
  <div class="row"><a href="/api/sauvegarde"><button class="pr">Télécharger la sauvegarde</button></a><input type="file" id="resf" accept=".json" class="g"><button onclick="restaurer()">Restaurer</button></div>
  <div class="m" id="rese"></div></div>
 <div class="c"><b>WiFi de la maison</b><div class="row"><input id="ws" placeholder="nom du réseau"><input id="wp" type="password" placeholder="mot de passe"><button onclick="if(confirm('Enregistrer et redémarrer ?'))post('/api/wifi',{s:v('ws'),p:v('wp')})">Enregistrer</button></div></div>
 <div class="c"><b>Liste publique des KiwiSDR</b><div class="row"><input id="lu" class="g"><button onclick="post('/api/liste_url',{u:v('lu')}).then(()=>toast('Téléchargement lancé'))">Enregistrer</button></div></div>
</section>
<section id="t6">
 <div class="c"><div class="row"><b class="g">Traduction en français (bêta)</b><span id="tetat" class="m"></span></div>
  <p class="m">Le traducteur tourne sur ton PC (programme Python). Il écoute la radio que la carte reçoit, reconnaît la langue parlée et affiche la traduction en français dans sa fenêtre et ici.</p>
  <div class="row"><button class="pr" onclick="lancer()">▶ Lancer le traducteur</button><button onclick="arreter()">■ Arrêter</button><button onclick="$('tl').innerHTML='';">Effacer</button></div>
  <p class="m">Première utilisation : installe le traducteur sur le PC avec <b>installer.bat</b> (dossier traducteur du projet). Le navigateur demande ensuite d'ouvrir « KiwiTraducteur » : coche « toujours autoriser ».</p></div>
 <div class="c" id="tl"></div>
</section>
</main><div id="toast"></div>
<script>
const MODES=['AM','LSB','USB','CW','FM','WFM'],PAS=[10,100,500,1000,5000,9000,10000,12500,25000,100000],ONG=['Radio','Récepteurs','Stations','Quoi écouter','FM','Favoris','Décodeurs','Réglages','Système','Traduction'],P=n=>ONG.indexOf(n);
const BANDES=[['160 m',1850,1],['80 m',3700,1],['40 m',7150,1],['30 m',10120,3],['20 m',14230,2],['17 m',18130,2],['15 m',21300,2],['10 m',28500,2],['GO',162,0],['PO',1008,0],['49 m',6070,0],['31 m',9650,0]];
let E={},S={},T=0,R=1,RT=0,PK='',ST=0,CC=0,CI=1,FMK='',wfN=0,cfP=0,spP=0;
const $=i=>document.getElementById(i),v=i=>$(i).value;
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2500)}
function post(u,d){return fetch(u,{method:'POST',body:new URLSearchParams(d)}).then(r=>{if(!r.ok)r.text().then(toast);return r})}
function cmd(o){post('/api/cmd',{o}).then(()=>setTimeout(etat,120))}
function hz(h){return h<1000?h+' Hz':(h%1000?(h/1000).toFixed(h%100?2:1).replace('.',','):h/1000)+' kHz'}
function fmhz(f){const m=f/1000,d=m>=1000?3:4;let [e,x]=m.toFixed(d).split('.');return e.replace(/\B(?=(\d{3})+(?!\d))/g,' ')+','+x}
function fkhz(f){let [e,d]=f.toFixed(2).split('.');return e.replace(/\B(?=(\d{3})+(?!\d))/g,' ')+','+d}
function seg(id,noms,sel,fn){const e=$(id);if(e.childElementCount!=noms.length){e.innerHTML='';noms.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;b.onclick=()=>fn(i);e.appendChild(b)})}[...e.children].forEach((b,i)=>b.classList.toggle('on',i==sel))}
function rtype(){seg('rtype',['Près de moi','KiwiSDR · ondes courtes','OpenWebRX · VHF/UHF'],RT+1,i=>{RT=i-1;rec()});$('rkiwi').style.display=RT==0?'':'none';$('rvhf').style.display=RT==1?'':'none';$('rprox').style.display=RT<0?'':'none'}
function pli(L,nom,sous,km,coul,clic){const e=document.createElement('div');e.className='li';e.innerHTML='<span class="dot" style="background:'+coul+'"></span><div class="g" style="min-width:0"><b></b><span class="m"></span></div><b style="white-space:nowrap"></b>';
 e.querySelector('.g b').textContent=nom;e.querySelector('.m').textContent=sous;e.lastChild.textContent=km+' km';e.onclick=clic;L.appendChild(e)}
function prox(){fetch('/api/proches').then(r=>r.json()).then(d=>{$('ppos').textContent=d.pos_etat||d.pos;
 const vide=(L,n)=>{L.innerHTML=n?'':'<div class="m">'+(d.cours?'Chargement de la liste…':'Liste vide pour le moment.')+'</div>'};
 vide($('pk'),d.kiwi.length);d.kiwi.forEach(s=>pli($('pk'),s.nom,s.lieu+' · '+s.u+'/'+s.um+' auditeurs',s.km,'#22C55E',()=>{post('/api/connecter',{h:s.hote,p:s.port,nom:s.nom});toast('Connexion à '+s.nom);go(0)}));
 vide($('pv'),d.vhf.length);d.vhf.forEach(s=>pli($('pv'),s.nom,['','VHF','UHF','VHF · UHF'][s.b&3],s.km,'#38BDF8',()=>{cmd('owrx '+s.url+'\t'+s.nom);toast('Connexion à '+s.nom);go(0)}));
 vide($('pf'),d.fm.length);d.fm.forEach(s=>pli($('pf'),s.nom,s.ville+(s.pays?', '+s.pays:''),s.km,'#F59E0B',()=>{post('/api/fm/connecter',{u:s.url,nom:s.nom});toast('Connexion à '+s.nom);go(P('FM'))}));
 if(d.cours)setTimeout(()=>{if(T==P('Récepteurs')&&RT<0)prox()},3000)})}
function vhf(){const q=$('vq').value.trim();fetch('/api/vhf'+(q?'?q='+encodeURIComponent(q):'')).then(r=>r.json()).then(d=>{if(q!=$('vq').value.trim())return;
 $('vetat').textContent=d.etat+(d.cours?' (chargement…)':'')+(q?' · '+d.total+' trouvé'+(d.total>1?'s':''):'');
 $('vliste').innerHTML=d.liste.length?'':'<div class="m">'+(d.cours?'Chargement de la liste…':q?'Aucun récepteur ne correspond.':'Liste vide : touche Actualiser.')+'</div>';
 d.liste.forEach(s=>{const e=document.createElement('div');e.className='li';
  e.innerHTML='<span class="dot" style="background:#38BDF8"></span><div class="g"><b></b><span class="m"></span></div>';
  e.querySelector('b').textContent=s.nom;e.querySelector('.m').textContent=['','VHF','UHF','VHF · UHF'][s.b&3]+(s.km<65000?' · '+s.km+' km':'')+' · '+s.url;
  e.onclick=()=>{cmd('owrx '+s.url+'\t'+s.nom);toast('Connexion à '+s.nom);go(0)};$('vliste').appendChild(e)});
 if(d.cours)setTimeout(()=>{if(T==P('Récepteurs')&&RT==1)vhf()},3000)})}
function go(i){T=i;[...document.querySelectorAll('section')].forEach((s,k)=>s.classList.toggle('on',k==i));[...$('nav').children].forEach((b,k)=>b.classList.toggle('on',k==i));if(i==P('Récepteurs'))rec();if(i==P('Stations'))sta();if(i==P('Quoi écouter'))quoi();if(i==P('FM')){fmS();fmE();wrE()}if(i==P('Favoris'))favs();if(i==P('Réglages')||i==P('Système'))regl()}
ONG.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;b.onclick=()=>{go(i);post('/api/cmd',{o:'ecran '+n})};$('nav').appendChild(b)});
for(let i=0;i<12;i++)$('sm').appendChild(document.createElement('i'));
BANDES.forEach(b=>{const e=document.createElement('button');e.innerHTML=b[0]+'<br><span class="m">'+fkhz(b[1]).replace(',00','')+'</span>';e.onclick=()=>{cmd('m '+b[2]);setTimeout(()=>cmd('f '+b[1]),80)};$('bandes').appendChild(e)});
$('freq').onclick=()=>{const mz=E.proto==1;const s=prompt(mz?'Fréquence en MHz':'Fréquence en kHz',mz?E.f/1000:E.f);if(s){let f=parseFloat(s.replace(',','.').replace(/\s/g,''));if(mz)f*=1000;if(f>=0&&f<=6000000)cmd('f '+f.toFixed(3))}};
$('fav').onclick=()=>{cmd('favadd');toast('Ajouté aux favoris')};
function etat(){fetch('/api/etat').then(r=>r.json()).then(e=>{E=e;
 const col=['#8FA3BF','#F59E0B','#F59E0B','#22C55E','#EF4444','#EF4444','#EF4444','#EF4444'][e.etat];$('dot').style.background=col;
 $('rx').textContent=e.nom||'Aucun récepteur';$('ver').textContent=e.version;
 $('res').textContent=e.etat==3?'':e.message;
 if(e.fm){$('rx').textContent='📻 '+e.fm_nom+(e.fm_ps?' · '+e.fm_ps:'');$('res').textContent=e.fm_mhz.toFixed(1)+' MHz · '+e.fm_msg;$('dot').style.background=['#8FA3BF','#F59E0B','#22C55E','#EF4444'][e.fm_etat]}
 const cx=(e.etat==1||e.etat==2)&&!e.fm;$('freq').classList.toggle('clig',cx);$('dot').classList.toggle('clig',cx);
 $('jrnl').style.display=e.jv&&!e.fm?'':'none';if(e.jv&&!e.fm)$('jrnl').innerHTML=(e.journal||[]).map(l=>'<div><span class="'+['en','ok','ko'][l.e]+'">'+['⟳','✓','✗'][l.e]+'</span> <span class="'+(l.e==2?'tko':'')+'">'+l.t.replace(/</g,'&lt;')+'</span></div>').join('');
 $('wfnon').style.display=e.wf_non&&!e.jv?'':'none';if(e.wf_non)$('wfnon').innerHTML='Pas de waterfall sur le canal '+(e.canal+1)+'<br><span style="color:#8FA3BF;font-weight:500;font-size:13px">Ce récepteur réserve le waterfall aux canaux 1 à '+e.wfch+' et il y a trop d\'auditeurs : le son marche, pas l\'image. Essaie un récepteur moins chargé.</span>';
 $('chips').innerHTML=e.fm?'':(e.nr||e.notch?'<span class="chip">NR</span> ':'')+(e.nb?'<span class="chip">NB</span> ':'')+(e.agc?'<span class="chip c2">AGC</span>':'');
 const mz=e.f>=30000;$('freq').textContent=mz?fmhz(e.f):fkhz(e.f);$('unite').textContent=mz?'MHz':'kHz';$('mode').textContent=MODES[e.mode];
 $('bandesbox').style.display=e.proto==1?'none':'';$('profbox').style.display=e.proto==1?'':'none';
 if(e.proto==1){const pk=(e.profils||[]).map(p=>p.id).join('/');if(pk!=PK){PK=pk;$('profs').innerHTML='';(e.profils||[]).forEach(p=>{const b=document.createElement('button');b.textContent=p.n;b.onclick=()=>{cmd('profil '+p.id);toast('Bande '+p.n)};$('profs').appendChild(b)})}
  [...$('profs').children].forEach((b,i)=>b.classList.toggle('on',i==e.profil));if(!(e.profils||[]).length)$('profs').innerHTML='<span class="m">Attente de la liste des bandes…</span>',PK=''}
 seg('pas',PAS.map(hz),e.pas,i=>cmd('pas '+i));seg('modes',e.proto==1?MODES:MODES.slice(0,5),e.mode,i=>cmd('m '+i));
 seg('agc',['Off','Lent','Moyen','Rapide'],e.agc,i=>cmd('agc '+i));seg('nr',['Off','Douce','Forte'],e.nr,i=>cmd('nr '+i));
 $('notch').classList.toggle('on',!!e.notch);seg('nbs',['Arrêt','Faible','Moyen','Fort'],e.nb|0,i=>cmd('nb '+i));{const g=$('ags');if(e.agc){g.min=-130;g.max=-60}else{g.min=0;g.max=120}if(document.activeElement!=g)g.value=e.agc?e.agc_seuil:e.gain_man;$('agsl').textContent=e.agc?"Seuil de l'AGC":'Gain fixe (AGC arrêtée)';$('agsv').textContent=(e.agc?e.agc_seuil:e.gain_man)+' dB'}$('lim').classList.toggle('on',!!e.lim);seg('gain',['Normal','+6 dB','+12 dB'],e.gain|0,i=>cmd('gain '+i));
 seg('scsens',['Faible','Moyen','Fort'],e.scan_sens|0,i=>cmd('scansens '+i));$('scb').classList.toggle('on',e.scan==-1);$('sch').classList.toggle('on',e.scan==1);
 $('scst').style.color=e.scan_ec?'#22C55E':'#F59E0B';$('scst').textContent=!e.scan?'':e.scan_ec?'Station trouvée · reprise dans '+e.scan_reste+' s':'Recherche '+(e.scan>0?'▲':'▼')+' · vitesse '+e.scan_v+'/4… (re-cliquer = plus vite, ici = arrêt)';
 if(document.activeElement!=$('vol'))$('vol').value=e.volume;$('volv').textContent=e.volume;
 const bp=$('bp');bp.min=e.bp_min;bp.max=e.bp_max;if(document.activeElement!=bp)bp.value=e.bp;$('bpv').textContent=hz(e.bp);
 const s=e.etat==3?Math.max(0,Math.min(9,Math.floor((e.rssi+127)/6)))+(e.rssi>-73?Math.min(3,Math.floor((e.rssi+73)/10)):0):0;
 [...$('sm').children].forEach((x,i)=>x.style.background=i<s?(i<7?'#22C55E':i<9?'#FACC15':'#EF4444'):'#22304A');
 $('dbm').textContent=e.etat==3?(e.rssi>-73?'S9+'+Math.round(e.rssi+73):'S'+Math.max(0,Math.min(9,Math.floor((e.rssi+127)/6))))+' · '+Math.round(e.rssi)+' dBm'+(e.sb>=0?' · S/B '+e.sb+' dB':''):'';
 $('rec').classList.toggle('dg',!!e.rec);$('rec').textContent=e.rec?'■ '+Math.floor(e.rec_s/60)+':'+String(e.rec_s%60).padStart(2,'0'):'● REC';
 $('span').textContent=(e.span>=10?Math.round(e.span):e.span.toFixed(1))+' kHz de large';
 $('stbox').style.display=e.stations.length?'':'none';
 $('st').innerHTML=e.stations.sort((a,b)=>a.f-b.f).map((s,i)=>'<span class="st" data-i="'+i+'">'+fkhz(s.f)+' · '+s.n.replace(/</g,'&lt;')+'</span>').join('');
 [...$('st').children].forEach(x=>x.onclick=()=>{const s=e.stations[x.dataset.i];cmd('m '+s.m);setTimeout(()=>cmd('f '+s.f),80)});
 repere()}).catch(()=>{$('res').textContent='carte injoignable'})}
function repere(){if(!E.span)return;const w=$('wfbox').clientWidth,k=w/E.span,x=w/2+(E.f-E.cf)*k;$('mk').style.left=x+'px';
 const lo={0:-E.bp/2,1:-(300+E.bp),2:300,3:500-E.bp/2,4:-E.bp/2,5:-E.bp/2}[E.mode]/1000,hi=lo+E.bp/1000;$('pb').style.left=(x+lo*k)+'px';$('pb').style.width=Math.max(2,(hi-lo)*k)+'px'}
$('wfbox').onclick=ev=>{if(!E.span)return;const r=$('wfbox').getBoundingClientRect();const f=E.cf+((ev.clientX-r.left)/r.width-0.5)*E.span;const p=PAS[E.pas]/1000;cmd('f '+(Math.round(f/p)*p).toFixed(3))};
const W=$('wf').getContext('2d'),SP=$('spec').getContext('2d');
function cascade(){if(T!=0)return;const n=(Math.abs(E.cf-cfP)>0.001||Math.abs(E.span-spP)>0.001)?0:wfN;fetch('/api/cascade?n='+n).then(r=>r.arrayBuffer()).then(b=>{const d=new DataView(b);
 const c=d.getUint32(0,true),k=d.getUint16(4,true);cfP=d.getFloat32(6,true);spP=d.getFloat32(10,true);wfN=c;
 SP.fillStyle='#0d1526';SP.fillRect(0,0,320,70);SP.fillStyle='#38BDF8';let fl=255;for(let x=0;x<320;x++)fl=Math.min(fl,d.getUint8(14+x));
 for(let x=0;x<320;x++){const h=Math.max(1,Math.min(70,(d.getUint8(14+x)-fl+4)*70/60));SP.fillRect(x,70-h,1,h)}
 if(!k)return;if(n==0)W.clearRect(0,0,320,120);else W.drawImage($('wf'),0,0,320,120-k,0,k,320,120-k);
 const im=W.createImageData(320,k);for(let r=0;r<k;r++){const src=334+r*640,dy=k-1-r;for(let x=0;x<320;x++){const p=d.getUint16(src+x*2,true),o=(dy*320+x)*4;
  im.data[o]=(p>>11)<<3;im.data[o+1]=((p>>5)&63)<<2;im.data[o+2]=(p&31)<<3;im.data[o+3]=255}}W.putImageData(im,0,0)}).catch(()=>{})}
let RQ=0;
function rec(){rtype();if(RT<0){prox();return}if(RT){vhf();return}const q=$('rq').value.trim();fetch('/api/recepteurs?r='+R+(q?'&q='+encodeURIComponent(q):'')).then(r=>r.json()).then(d=>{if(q!=$('rq').value.trim())return;
 seg('regions',d.regions,d.recherche?-1:R,i=>{R=i;$('rq').value='';rec()});
 $('letat').textContent=d.recherche?(d.total+' récepteur'+(d.total>1?'s':'')+' trouvé'+(d.total>1?'s':'')+' dans toutes les régions'+(d.total>d.liste.length?' (les '+d.liste.length+' plus proches affichés)':'')):d.etat+(d.cours?' (chargement…)':'');
 $('liste').innerHTML=d.liste.length?'':'<div class="m">'+(d.recherche?'Aucun récepteur ne correspond à « '+q.replace(/</g,'&lt;')+' ».':'Liste vide : touche Actualiser.')+'</div>';
 d.liste.forEach(s=>{const e=document.createElement('div');e.className='li';const plein=s.umax&&s.u>=s.umax;
  e.innerHTML='<span class="dot" style="background:'+(s.perso?'#38BDF8':plein?'#EF4444':s.umax&&s.u+1>=s.umax?'#FACC15':'#22C55E')+'"></span><div class="g"><b></b><span class="m"></span></div><span>'+(s.perso?'':plein?'plein':s.u+'/'+s.umax)+'</span>';
  e.querySelector('b').textContent=s.nom;e.querySelector('.m').textContent=s.perso?s.hote+':'+s.port:s.lieu+(s.km<65000?' · '+s.km+' km':'');
  e.onclick=()=>{if(plein&&!s.perso){toast('Ce récepteur est plein');return}const d2={h:s.hote,p:s.port,nom:s.nom};if(s.perso)d2.ip=s.ip;post('/api/connecter',d2).then(()=>{toast('Connexion à '+s.nom);go(0)})};
  if(s.perso){const x=document.createElement('button');x.textContent='Retirer';x.onclick=ev=>{ev.stopPropagation();if(confirm('Retirer '+s.nom+' ?'))post('/api/perso/retirer',{i:s.ip}).then(rec)};e.appendChild(x)}
  $('liste').appendChild(e)})})}
let SQ=0,SL=0,ET=0;const LGF=['','en','fr','zh','autres','--'],LGN=['Toutes','Anglais','Français','Chinois','Autres','Signaux'];
let QC=0;
function quoi(){if(T!=P('Quoi écouter'))return;fetch('/api/quoi?c='+QC).then(r=>r.json()).then(d=>{
 seg('qcat',['Ondes courtes','VHF','UHF','Tout'],QC,i=>{QC=i;quoi()});
 $('qmes').textContent=d.total?'■ Arrêter ('+d.fait+' sur '+d.total+')':'Mesurer';
 $('qctx').textContent=d.ctx+(d.actif?'':' · choisis un récepteur (onglet Récepteurs) pour écouter');
 const L=$('qliste');L.innerHTML=d.liste.length?'':'<div class="m">Aucune suggestion.</div>';
 d.liste.forEach(q=>{const e=document.createElement('div');e.className='li';const net=q.in||!d.actif;if(!net)e.style.opacity=.5;
  const coul=['#22C55E','#F59E0B','#38BDF8','#38BDF8','#38BDF8','#22C55E','#F59E0B'][q.g]||'#38BDF8',cb=q.niv>=4?'#22C55E':q.niv>=2?'#F59E0B':'#8FA3BF';
  let bars='';for(let k=0;k<5;k++)bars+='<i style="display:inline-block;width:6px;height:9px;margin-left:2px;border-radius:2px;background:'+(k<q.niv?cb:'#22304A')+'"></i>';
  e.innerHTML='<span class="dot" style="background:'+(net?coul:'#8FA3BF')+'"></span><div class="g" style="min-width:0"><b></b><span class="m"></span></div><div style="text-align:right;white-space:nowrap">'+(net?bars:'')+'<br><span class="m"></span></div>';
  const m=e.querySelectorAll('.m');e.querySelector('.g b').textContent=q.n;m[0].textContent=q.d;m[1].textContent=q.e;if(net&&q.niv>=4)m[1].style.color='#22C55E';
  e.onclick=()=>{const fm=q.g==6&&!q.in;
   cmd('quoi '+QC+' '+q.a+' '+q.b);toast(q.n+(fm?'':!q.in?' : récepteur le plus proche…':q.in==1?' (changement de bande)':''));setTimeout(()=>go(fm?P('FM'):0),300)};L.appendChild(e)})})}
function stype(){seg('stype',['Ondes courtes','Canaux VHF/UHF'],ST,i=>{ST=i;sta()});$('shf').style.display=ST?'none':'';$('svhf').style.display=ST?'':'none'}
function can(){const q=$('cq').value.trim();fetch('/api/canaux?c='+[15,1,2,4,8][CC]+'&e='+(CI?1:0)+(q?'&q='+encodeURIComponent(q):'')).then(r=>r.json()).then(d=>{if(q!=$('cq').value.trim())return;
 seg('ccat',['Tous','Aviation','Marine','Radioamateurs','PMR446'],CC,i=>{CC=i;can()});
 $('cscan').textContent=d.scan?'■ Arrêter le scan':'▶ Scanner';$('cscan').onclick=()=>{cmd(d.scan?'scan 0':'scanliste '+[15,1,2,4,8][CC]);setTimeout(can,400)};
 $('cetat').textContent=(d.scan?'Scan : '+d.scan+' · ':'')+d.total+' canaux · aérodromes : '+d.source+(d.owrx?'':' · connecte-toi à un récepteur VHF/UHF (onglet Récepteurs) pour écouter');
 const L=$('cliste');L.innerHTML=d.liste.length?'':'<div class="m">Aucun canal ne correspond.</div>';
 d.liste.forEach(c=>{const e=document.createElement('div');e.className='li';
  e.innerHTML='<span class="dot" style="background:'+(c.in==2?'#22C55E':c.in==1?'#38BDF8':'#8FA3BF')+'"></span><div class="fq"><b>'+(c.f/1000).toFixed(3).replace('.',',')+'</b><br><span class="chip c2">'+MODES[c.m]+'</span></div><div class="g" style="min-width:0"><b></b><span class="m"></span></div>';
  e.querySelector('.g b').textContent=c.n;e.querySelector('.g .m').textContent=c.t+(c.km>=0?' · '+c.km+' km':'');
  e.onclick=()=>{cmd('vhf '+c.f+' '+c.m);toast(c.n+(c.in==2?'':c.in==1?' (changement de bande)':' : ce récepteur ne le capte pas'));setTimeout(()=>go(0),300)};L.appendChild(e)});
 if(d.scan&&T==P('Stations')&&ST)setTimeout(can,2000)})}
function sta(){stype();if(ST){can();return}const q=$('sq').value.trim();fetch('/api/stations?q='+encodeURIComponent(q)+'&l='+LGF[SL]).then(r=>r.json()).then(d=>{if(q!=$('sq').value.trim())return;
 seg('slg',LGN,SL,i=>{SL=i;sta()});
 $('setat').textContent=d.total+' station'+(d.total>1?'s':'')+(d.connecte?'':' · pas de récepteur connecté : « Ici » réglera la fréquence pour la prochaine connexion');
 $('ssrc').textContent=d.nb+' stations · source : '+d.source;
 const L=$('sliste');L.innerHTML=d.liste.length?'':'<div class="m">Aucune station ne correspond. Essaie la recherche EiBi ci-dessous.</div>';
 d.liste.forEach(s=>{const e=document.createElement('div');e.className='li';e.style.cursor='default';
  e.innerHTML='<div class="fq"><b>'+fkhz(s.f)+'</b><br><span class="chip c2">'+MODES[s.m]+'</span></div><div class="g" style="min-width:0"><b></b><span class="m"></span></div><div style="display:flex;flex-direction:column;gap:4px"></div>';
  e.querySelector('.g b').textContent=s.nom+(s.lg&&s.lg!='--'?' · '+s.lg.toUpperCase():'');e.querySelector('.g .m').textContent=s.pays+(s.info?' · '+s.info:'');
  const bt=e.lastChild,b1=document.createElement('button');b1.className='sb';b1.textContent='▶ Ici';b1.title='Sur le récepteur actuel';b1.onclick=()=>ecouter(s,'ici');bt.appendChild(b1);
  if(s.pos){const b2=document.createElement('button');b2.className='sb';b2.textContent='📍 Proche';b2.title='Sur le récepteur libre le plus proche de l\'émetteur';b2.onclick=()=>ecouter(s,'proche');bt.appendChild(b2)}
  L.appendChild(e)})})}
function ecouter(s,ou){post('/api/station',{i:s.i,ou}).then(r=>r.ok?r.json():null).then(d=>{if(!d)return;toast(ou=='proche'?s.nom+' sur '+d.recepteur+' ('+d.km+' km de l\'émetteur)':s.nom+' · '+fkhz(s.f)+' kHz');go(0)})}
function eibi(){const q=$('eq').value.trim();if(!q)return;post('/api/eibi',{q}).then(r=>{if(r.ok){$('eliste').innerHTML='';clearInterval(ET);ET=setInterval(eibi_suivi,700)}})}
function eibi_suivi(){fetch('/api/eibi').then(r=>r.json()).then(d=>{
 $('eetat').textContent=d.etat=='cours'?'Recherche… '+d.avance+' % · '+d.liste.length+' trouvée(s)':d.etat=='erreur'?d.erreur:d.liste.length+' émission(s) à l\'antenne trouvée(s)';
 if(d.etat!='cours')clearInterval(ET);
 const L=$('eliste');L.innerHTML='';d.liste.forEach(s=>{const e=document.createElement('div');e.className='li';
  e.innerHTML='<div class="fq"><b>'+fkhz(s.f)+'</b><br><span class="chip c2">'+MODES[s.m]+'</span></div><div class="g" style="min-width:0"><b></b><span class="m"></span></div>';
  e.querySelector('.g b').textContent=s.nom;e.querySelector('.g .m').textContent=s.note;
  e.onclick=()=>{cmd('station '+s.f+' '+s.m);toast(s.nom+' · '+fkhz(s.f)+' kHz');go(0)};L.appendChild(e)})})}
let FQ=0,FM={};
function fmS(){const q=$('fq').value.trim();fetch('/api/fm/serveurs?q='+encodeURIComponent(q)).then(r=>r.json()).then(d=>{if(q!=$('fq').value.trim())return;
 $('fmletat').textContent=(d.cours?'Chargement de la liste… ':'')+d.etat+(q?' · '+d.total+' trouvé(s)':'');
 const L=$('fmliste');L.innerHTML=d.liste.length?'':'<div class="m">'+(d.cours?'Liste en cours de chargement…':'Aucun serveur.')+'</div>';
 d.liste.forEach(s=>{const e=document.createElement('div');e.className='li';
  e.innerHTML='<span class="dot" style="background:'+['#64748B','#22C55E','#F59E0B'][s.etat]+'"></span><div class="g" style="min-width:0"><b></b><span class="m"></span></div><span class="m" style="text-align:right"></span>';
  e.querySelector('.g b').textContent=(s.etat==2?'🔒 ':'')+s.nom;e.querySelector('.g .m').textContent=(s.ville?s.ville+', ':'')+s.pays+(s.km<65000?' · '+s.km+' km':'');
  e.lastChild.textContent=(s.etat==0?'injoignable':s.kbps?s.kbps+' k':'');
  e.onclick=()=>{if(s.etat==0&&!confirm(s.nom+' semble injoignable. Essayer quand même ?'))return;fmCo(s.url,s.nom)};L.appendChild(e)});
 if(d.cours)setTimeout(fmS,2500)})}
function fmCo(u,n){if(!u)return;post('/api/fm/connecter',{u,nom:n}).then(r=>{if(r.ok){toast('Connexion à '+(n||u));window.scrollTo(0,0);setTimeout(fmE,300)}})}
function fmE(){if(T!=P('FM'))return;fetch('/api/fm/etat').then(r=>r.json()).then(f=>{FM=f;
 $('fmdot').style.background=f.actif?['#8FA3BF','#F59E0B','#22C55E','#EF4444'][f.etat]:'#8FA3BF';
 $('fmsrv').textContent=f.actif?f.serveur:'Aucun serveur FM : choisis-en un dans la liste';$('fmmsg').textContent=f.actif?f.message:'';
 $('fmf').textContent=f.actif&&f.mhz>0?f.mhz.toFixed(Math.round(f.mhz*100)%10?2:1).replace('.',','):'––,–';
 $('fmst').style.display=f.actif&&f.st?'':'none';$('fmps').textContent=f.actif?f.ps:'';$('fmrt').textContent=f.actif?f.rt:'';
 $('fmtx').textContent=f.actif&&f.tx?'Émetteur : '+f.tx+(f.tx_pays?' ('+f.tx_pays+')':'')+(f.tx_km?' · '+f.tx_km+' km':'')+(f.tx_erp?' · '+String(f.tx_erp).replace('.',',')+' kW':''):'';
 const sg=f.actif?Math.max(0,Math.min(100,f.sig*100/80)):0;$('fmsig').style.width=sg+'%';$('fmsig').style.background=f.sig>45?'#22C55E':f.sig>25?'#FACC15':'#EF4444';
 $('fmsigv').textContent=f.actif?f.sig.toFixed(0)+' dBf':'';
 if(document.activeElement!=$('fmvol'))$('fmvol').value=f.volume;
 $('fmaud').textContent=f.actif?(f.hz?'Son MP3 '+f.kbps+' kbit/s · '+(f.hz/1000)+' kHz · réserve '+f.reserve+' ms':'')+(f.users?' · '+f.users+' auditeur'+(f.users>1?'s':''):''):'';
 $('fsb').classList.toggle('on',f.scan==-1);$('fsh').classList.toggle('on',f.scan==1);
 $('fsst').style.color=f.scan_reste?'#22C55E':'#F59E0B';$('fsst').textContent=!f.scan?'':f.scan_reste?'Station trouvée'+(f.ps?' : '+f.ps:'')+' · reprise dans '+f.scan_reste+' s':'Recherche '+(f.scan>0?'▲':'▼')+' · vitesse '+f.scan_v+'/4…';
 const tr=f.trouvees||[];$('fstbox').style.display=tr.length?'':'none';const k=tr.map(x=>x.f+x.ps).join();if(k!=FMK){FMK=k;$('fst').innerHTML='';tr.forEach(x=>{const e=document.createElement('div');e.className='li';
  e.innerHTML='<div class="fq"><b>'+x.f.toFixed(1).replace('.',',')+'</b></div><div class="g"><b></b><span class="m">'+x.s+' dBf</span></div>';e.querySelector('.g b').textContent=x.ps||'(sans RDS)';
  e.onclick=()=>{cmd('fmtrouve '+x.f);setTimeout(fmE,300)};$('fst').appendChild(e)})}})}
function fmPas(d){if(!FM.actif||!(FM.mhz>0))return;cmd('fmf '+(FM.mhz+d).toFixed(2));setTimeout(fmE,250)}
function fmAller(){let x=parseFloat(v('fmin').replace(',','.'));if(!(x>0))return;if(x>1000)x/=1000;cmd('fmf '+x.toFixed(2));$('fmin').value='';setTimeout(fmE,250)}
function jrn(){fetch('/api/journal').then(r=>r.json()).then(d=>{$('jsd').textContent=d.sd?'':'Pas de carte SD : le journal est perdu au redémarrage.';const L=$('jrn');L.innerHTML='';
 const li=(e,c)=>{const x=document.createElement('div');x.className='li';x.innerHTML='<div class="g" style="min-width:0"><b></b><span class="m"></span></div><div class="m" style="text-align:right;white-space:nowrap"></div>';
  x.querySelector('b').textContent=(e.f>=30000?(e.f/1000).toFixed(3).replace('.',',')+' MHz':e.f.toFixed(1).replace('.',',')+' kHz')+' '+e.m+(e.q?' · '+e.q:'');
  const m=x.querySelectorAll('.m');m[0].textContent=e.rx;m[1].innerHTML=(c?'<b style="color:#22C55E">en cours</b>':e.h.slice(0,5))+'<br>'+(e.s>=60?Math.floor(e.s/60)+' min':e.s+' s')+' · '+e.r+' dBm';
  x.onclick=()=>{cmd('f '+e.f);toast('Fréquence réglée')};L.appendChild(x)};
 if(d.cours&&d.cours.s>=30)li(d.cours,1);d.liste.forEach(e=>li(e,0));if(!L.children.length)L.innerHTML='<div class="m">Rien encore : écoute une fréquence au moins 30 secondes.</div>'})}
function favs(){jrn();fetch('/api/favoris').then(r=>r.json()).then(d=>{const L=$('favs');L.innerHTML=d.fav.length?'':'<div class="m">Aucun favori : touche ★ sur la radio.</div>';
 d.fav.forEach((f,i)=>{const e=document.createElement('div');e.className='li';e.innerHTML='<b style="color:var(--a);font-size:20px">'+fkhz(f.f)+'</b><span class="chip c2">'+MODES[f.m]+'</span><div class="g"><b></b><span class="m"></span></div>';
  e.querySelector('.g b').textContent=f.nom||'(sans nom)';e.querySelector('.g .m').textContent=f.hote;e.onclick=()=>{cmd('favgo '+i);go(0)};
  const r=document.createElement('button');r.textContent='Renommer';r.onclick=ev=>{ev.stopPropagation();const n=prompt('Nom du favori',f.nom);if(n!==null){cmd('favnom '+i+' '+n.slice(0,30));setTimeout(favs,500)}};
  const s=document.createElement('button');s.textContent='Supprimer';s.className='dg';s.onclick=ev=>{ev.stopPropagation();if(confirm('Supprimer ce favori ?')){cmd('favdel '+i);setTimeout(favs,500)}};
  e.appendChild(r);e.appendChild(s);L.appendChild(e)});
 $('pres').innerHTML='';d.presets.forEach((p,i)=>{const e=document.createElement('div');e.className='li';e.innerHTML='<b style="color:var(--a)">'+fkhz(p.f)+'</b><span class="m">'+MODES[p.m]+'</span><div class="g"></div>';e.querySelector('.g').textContent=p.nom;e.onclick=()=>{cmd('preset '+i);go(0)};$('pres').appendChild(e)})})}
function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
let WRV='',WRF=0;
function wrC(){cmd('wrcherche '+[v('wrn').trim(),v('wrp').trim().toUpperCase(),v('wrg').trim()].join('|'));WRF=1;WRV='';$('wrl').innerHTML='<div class="m">Recherche…</div>';setTimeout(wrE,1500)}
function wrE(){if(T!=P('FM'))return;fetch('/api/webradio').then(r=>r.json()).then(d=>{
 const a=d.etat>0;$('wrdot').style.background=d.etat==2?'#22C55E':(d.etat==3?'#EF4444':(a?'#F59E0B':''));$('wrstop').style.display=a?'':'none';
 $('wrmsg').textContent=a?(d.msg||''):'';$('wrjoue').innerHTML=a?'<b>'+esc(d.nom)+'</b> <span class="m">'+esc(d.format||'')+'</span><div style="color:#22D3EE">'+esc(d.titre||'')+'</div>':'';
 seg('wrr',['★ Favoris','Résultats'+(d.cours?' …':' ('+d.res.length+')')],WRF,i=>{WRF=i;WRV='';wrE()});$('wrr').children[1].textContent='Résultats'+(d.cours?' …':' ('+d.res.length+')');
 const L=WRF?d.res:d.fav,k=JSON.stringify(L)+d.url;if(k==WRV)return;WRV=k;
 let h='';L.forEach((s,i)=>{const on=a&&s.u==d.url;h+='<div class="row" style="padding:4px 0;border-top:1px solid #1E293B'+(on?';color:#22C55E':'')+'"><button onclick="cmd(\'wrjoue '+(WRF?'r ':'f ')+i+'\');setTimeout(wrE,500)">▶</button><span class="g"><b>'+esc(s.n)+'</b> <span class="m">'+esc(s.c)+(s.k?' '+s.k+' k':'')+(s.p?' · '+s.p:'')+(s.t?' · '+esc(s.t):'')+'</span></span><button title="Favori" onclick="cmd(\'wrfav '+(WRF?'r ':'f ')+i+'\');WRV=\'\';setTimeout(wrE,300)">'+(s.f?'★':'☆')+'</button></div>'});
 $('wrl').innerHTML=h||'<div class="m">'+(WRF?(d.cours?'Recherche…':'Aucune station (cherche un nom, un pays ou un genre)'):'Pas encore de favori')+'</div>'})}
function decod(){if(T!=P('Décodeurs'))return;fetch('/api/dec').then(r=>r.json()).then(d=>{seg('dm',['CW','RTTY','FAX','FT8'],d.mode,i=>cmd('dec '+i));const f8=d.mode==3;$('ft8z').style.display=f8?'':'none';$('i1').style.display=$('i2').style.display=$('cale').style.display=f8?'none':'';
 if(f8){seg('ft8b',['160 m','80 m','60 m','40 m','30 m','20 m','17 m','15 m','12 m','10 m','6 m'],d.bande,i=>cmd('ft8bande '+i));
  $('ft8e').textContent=(d.avant<0?'Heure inconnue (attente de l\'heure Internet)':'Décodage dans '+d.avant+' s')+(d.fv?' · dernière période : '+d.der+' message(s) en '+(d.ms/1000).toFixed(1)+' s':'')+(d.bande<0?' · choisis une bande':'');
  if(decod.fv!==d.fv||!$('ft8t').rows.length){decod.fv=d.fv;let h='<tr class="m"><td>UTC</td><td>dB</td><td>Hz</td><td>Message</td><td>Pays</td><td>km</td></tr>';
   for(const m of d.ft8||[]){const t=new Date(m.t*1000).toISOString().substr(11,8);h+='<tr style="border-top:1px solid #1E293B'+(m.cq?';color:#22C55E':'')+'"><td>'+t+'</td><td>'+m.s+'</td><td>'+m.f+'</td><td><b>'+esc(m.m)+'</b></td><td>'+esc(m.p)+'</td><td>'+(m.km||'')+'</td></tr>'}
   $('ft8t').innerHTML=h}}
 $('i1').textContent=d.i1||'—';$('i2').textContent=d.i2||'—';$('cale').textContent=d.cale?'● Calé':'○ Cherche';$('cale').style.color=d.cale?'#22C55E':'';
 $('inv').style.display=d.mode==1?'':'none';$('inv').classList.toggle('on',!!d.inv);
 const fax=d.mode==2;$('dtxt').style.display=(fax||f8)?'none':'';$('fax').style.display=fax?'':'none';
 if(!fax){const p=$('dtxt');p.textContent=d.texte;p.scrollTop=p.scrollHeight}else fetch('/api/fax').then(r=>r.arrayBuffer()).then(b=>{const u=new Uint8Array(b),n=u[0]|u[1]<<8,c=$('fax');if(!n)return;c.height=n;const x=c.getContext('2d'),im=x.createImageData(320,n);
  for(let i=0;i<320*n;i++){const g=u[2+i];im.data[i*4]=im.data[i*4+1]=im.data[i*4+2]=g;im.data[i*4+3]=255}x.putImageData(im,0,0)})})}
function pvil(){const n=v('pville').trim();if(n.length<2)return;cmd('geoville '+n);toast('Recherche de '+n+'…');setTimeout(regl,2500)}
function restaurer(){const f=$('resf').files[0];if(!f){toast('Choisis d\'abord le fichier de sauvegarde');return}const l=new FileReader();l.onload=()=>{if(String(l.result).indexOf('sauvegarde-kiwi-es3c28p')<0){$('rese').textContent='Ce fichier n\'est pas une sauvegarde de la carte.';return}
  if(!confirm('Remplacer les réglages et les favoris actuels par ceux de '+f.name+' ?'))return;
  fetch('/api/restaurer',{method:'POST',headers:{'Content-Type':'text/plain'},body:l.result}).then(r=>r.text().then(t=>{$('rese').textContent=r.ok?'Restauré. La carte redémarre : la page se recharge dans 20 secondes.':'Échec : '+t;if(r.ok)setTimeout(()=>location.reload(),20000)})).catch(()=>{$('rese').textContent='Échec : la carte ne répond pas.'})};l.readAsText(f)}
function maj(){const f=$('majf').files[0];if(!f){toast('Choisis d\'abord le fichier');return}if(!/\.bin$/i.test(f.name)||/merged|bootloader|partitions/i.test(f.name)){toast('Mauvais fichier : il faut kiwi_console.ino.bin');return}
 if(!confirm('Installer '+f.name+' ('+Math.round(f.size/1024)+' Ko) ? Le son s\'arrête pendant la mise à jour.'))return;
 const x=new XMLHttpRequest(),d=new FormData();d.append('f',f,f.name);$('majb').disabled=true;
 x.upload.onprogress=e=>{if(e.lengthComputable)$('maje').textContent='Envoi : '+Math.round(e.loaded*100/e.total)+' %'};
 x.onload=()=>{$('majb').disabled=false;if(x.status==200){$('maje').textContent='Mise à jour faite. La carte redémarre : la page se recharge dans 20 secondes.';setTimeout(()=>location.reload(),20000)}else $('maje').textContent='Échec : '+x.responseText};
 x.onerror=()=>{$('majb').disabled=false;$('maje').textContent='Échec : la carte ne répond plus. Recharge la page et vérifie la version.'};
 x.open('POST','/api/maj');x.send(d)}
function regl(){fetch('/api/reglages').then(r=>r.json()).then(s=>{S=s;seg('sqs',['Arrêt','Bas','Moyen','Haut'],s.sq|0,i=>{cmd('sq '+i);setTimeout(regl,300)});seg('sams',['Arrêt','2 bandes','Bande haute','Bande basse'],s.sam|0,i=>{cmd('sam '+i);setTimeout(regl,300)});$('orep').classList.toggle('on',!!s.reprise);$('ovox').classList.toggle('on',!!s.vox);seg('nus',['Arrêt','1 min','5 min','15 min'],s.nuit|0,i=>{cmd('nuit '+i);setTimeout(regl,300)});if(document.activeElement!=$('gr')){$('gr').value=s.graves|0;$('gv').textContent=(s.graves|0)+' dB'}if(document.activeElement!=$('ar')){$('ar').value=s.aigus|0;$('av').textContent=(s.aigus|0)+' dB'}$('onoaa').classList.toggle('on',!s.noaa);$('ocw').classList.toggle('on',!!s.cwtxt);$('noaae').textContent='Propagation : '+(s.noaa==3?'lecture coupée automatiquement (la carte avait redémarré pendant la lecture). Le bouton la remet.':s.noaa==2?'lecture coupée.':s.flux>=0||s.kp>=0?'flux solaire '+(s.flux>=0?s.flux:'?')+' · Kp '+(s.kp>=0?s.kp:'?'):'pas encore lue (premier essai 90 secondes après le démarrage).');$('posl').textContent=s.pos+(s.pos_etat?' · '+s.pos_etat:'');$('ver').textContent=s.version;
 if(document.activeElement!=$('lum'))$('lum').value=s.lum;$('lumv').textContent=s.lum+' %';
 seg('wfv',['1','2','3','4'],s.wfv-1,i=>cmd('reg wfv '+(i+1)));seg('pal',['Kiwi','Vert','Gris','Ambre'],s.pal,i=>cmd('reg pal '+i));
 seg('etiq',['Non','EiBi (en ondes)','Communauté','Propriétaire'],s.etiq,i=>cmd('reg etiq '+i));
 if(document.activeElement!=$('tampon'))$('tampon').value=s.tampon;$('tamv').textContent=s.tampon;
 $('adpcm').classList.toggle('on',!!s.adpcm);$('ampli').classList.toggle('on',!!s.ampli);$('lim2').classList.toggle('on',!!s.lim);$('suivre').classList.toggle('on',!!s.suivre);
 $('sde').textContent=(s.sd?'✓ ':'⚠ ')+s.sd_etat;$('sdf').style.display=s.sd?'none':'';
 const W2=['Hors ligne','Connexion…','Connecté','Point d\'accès'];
 $('sys').innerHTML=[['Version',s.version],['WiFi',W2[s.wifi]+' · '+s.reseau],['Adresse','http://'+s.ip+'/'],['Signal WiFi',s.rssi_wifi+' dBm'],
  ['Flux KiwiSDR',(s.debit/1000).toFixed(1).replace('.',',')+' ko/s'],['Coupures du son',s.coupures],['Écran',s.ips.toFixed(0)+' images/s'],
  ['Mémoire libre',s.tas+' Ko interne · '+s.psram+' Ko PSRAM'],['Dernier démarrage',s.demarrage||''],['En marche depuis',Math.floor(s.duree/3600)+' h '+Math.floor(s.duree/60)%60+' min'],
  ['Limite de session',s.limite?s.limite+' min':'aucune annoncée']].map(r=>'<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>').join('');
 if(document.activeElement!=$('la')){$('la').value=s.lat;$('lo').value=s.lon;$('lu').value=s.url_liste}})}
let TN=0;
function lancer(){location.href='kiwitrad://demarrer?carte='+encodeURIComponent(location.host);toast('Ouverture du traducteur sur le PC…')}
function arreter(){location.href='kiwitrad://arreter?carte='+encodeURIComponent(location.host)}
function trad(){if(T!=P('Traduction'))return;fetch('/api/trad?n='+TN).then(r=>r.json()).then(d=>{$('tetat').textContent=d.actif?'● traducteur actif':'○ traducteur arrêté';$('tetat').style.color=d.actif?'#22C55E':'';
 if(d.total<TN)TN=0;d.lignes.forEach(l=>{const e=document.createElement('div');e.className='li';e.style.cursor='default';e.style.opacity=l.conf<50?0.55:1;
  e.innerHTML='<span class="m" style="min-width:70px"></span><div class="g"><b></b><span class="m"></span></div>';
  e.children[0].textContent=l.heure+' · '+l.langue;e.querySelector('b').textContent=l.fr;e.querySelector('.g .m').textContent=l.orig;$('tl').prepend(e)});TN=d.total})}
setInterval(trad,1500);
go(0);etat();setInterval(etat,700);setInterval(cascade,400);setInterval(decod,1000);setInterval(()=>{if(T==P('Réglages')||T==P('Système'))regl()},3000);setInterval(fmE,800);setInterval(wrE,2000);setInterval(quoi,2000);addEventListener('resize',repere);
</script></body></html>)PAGE";
