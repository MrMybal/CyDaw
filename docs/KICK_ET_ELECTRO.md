# Kick et les batteries électroniques

Deux choses pour les musiques électroniques :

- **Kick**, un instrument qui ne fait que des grosses caisses ;
- **CyDAW Electro**, une banque de batteries électroniques (kicks, caisses claires, claps, charleys,
  cymbales, toms, percussions, effets) et dix kits qui la jouent.

Ni l'un ni l'autre ne vient d'un enregistrement. Kick synthétise chaque coup. CyDAW fabrique les sons
de la banque sur l'ordinateur de l'utilisateur, à partir de recettes livrées avec lui. Rien n'est
téléchargé, et aucun échantillon n'est rangé dans le dépôt. Les sons, les recettes et les kits sont
à CyDAW, sous licence CC0 (domaine public).

## Kick

**Piste › Nouveau Kick** crée une piste instrument dont la fenêtre s'ouvre aussitôt. Sur sa carte,
dans Périphériques, on trouve l'image du kick, une rangée de potards (départ, fin, chute, déclin,
clic, sub, drive, sortie) et le bouton « Ouvrir Kick ».

### Comment il sonne

Un coup de Kick se compose de plusieurs étages :

- **un corps** : une onde (sinus, triangle, ronde ou dure) dont la hauteur tombe d'un départ à une
  fin. La durée et la courbe de cette chute se règlent ;
- **son enveloppe** : attaque, maintien, déclin et courbe du déclin. La longueur coupe le son, avec
  un fondu de 5 ms ;
- **un clic** par-dessus : du bruit filtré, un clic ou un blip, avec son niveau, son déclin et son
  ton ;
- **un sub** : un sinus sur la hauteur de fin, ou une octave dessous, avec son propre déclin ;
- **une saturation** : douce, dure, repli ou lampe, avec son drive et un filtre de ton ;
- **le timbre** : coupe-bas, graves (plateau à 70 Hz), coupe-haut ;
- **les réglages globaux** : suivi de la note, sensibilité à la vélocité, sortie.

C2 (note 36) joue le kick tel qu'il est réglé. Avec le suivi de la note, les autres notes le jouent
plus haut ou plus bas. Un coup va toujours jusqu'au bout, même si la touche est relâchée, et le
coup suivant le coupe en quelques millisecondes (l'allocateur de voix est en Mono).

### Sa fenêtre

Le haut de la fenêtre montre le kick tel que la voix le joue : son onde, sa hauteur (en jaune, sur
une échelle de 20 Hz à 10 kHz) et son enveloppe (en bleu). Les points se glissent :

- en jaune : le départ (haut, bas) ; la fin, qui règle la hauteur (haut, bas) et le temps de chute
  (gauche, droite) ; le milieu de la chute, qui règle sa courbe ;
- en bleu : la fin du maintien, la fin du déclin, et le milieu du déclin, qui règle sa courbe ;
- en blanc : la longueur.

En haut à droite s'affichent la hauteur de fin avec sa note (par exemple « Fin 55 Hz · A1 »), pour
accorder le kick sur la tonalité du morceau, ainsi que la durée et la crête. En dessous, les sections
CORPS, AMPLITUDE, CLIC, SUB, SATURATION, TIMBRE et GLOBAL ont chacune leurs potards. Un double-clic
remet un potard à sa valeur de départ, et « Initialiser » remet tout.

L'onglet **EFFETS** ajoute à la piste, après Kick, ce qu'une tranche de batterie a : distorsion,
transitoires, compresseur, égaliseur, filtre, réducteur, ampli, délai, réverbe, limiteur ; chacun montre
ses potards, et la croix le retire. Ce sont les périphériques de la piste (Périphériques les montre
aussi). Le même onglet sert à Prisme (`src/v2/InstrumentEffects.*`).

Chaque potard s'automatise : clic droit › « Automatiser dans l'arrangement ». La courbe joue sur
chaque coup, dès son départ (la hauteur, la chute, le clic d'un kick suivent le morceau).

### Ses préréglages

Kick a 17 sons d'usine : Init, 808 Long, 808 Short, 909 Punch, House Classic, Deep House, Techno
Hard, Techno Rumble, Hardstyle, Gabber, Trap Boom, Drum & Bass, Minimal Blip, Big Room, Sub Kick,
Lo-Fi Dusty et Acoustic Felt. Chacun a sa crête à -1 dBFS. Ils se fabriquent avec
`tools/factory_content/build_kick_presets.py`, en session silencieuse, et vont dans
`content/factory/Presets/device/kick-synth`.

### Dans le code

- Le moteur est dans `include/cydaw/engine/KickSynth.h` et `src/engine/KickSynth.cpp`. Les réglages
  forment un tableau plat (`KickValues`). `pitchAt`, `amplitudeAt` et `renderKick` servent à la fois
  à la voix et à l'image de la fenêtre.
- Le type de carte est `kick-synth` (primitive `core.kick-synth`, forme `PolyOscillatorWaveform::kick`).
  Les réglages sont publiés sous `kick.<clé>`, enregistrés par clé dans `kickValues`, et exportés
  dans le VST3 (`GraphRuntimePlugin`, plage de champs `kickFirst..kickLast`).
- `PolyVoice` délègue la voix à `kick::Voice`. Elle rend par blocs de 256 échantillons, puis applique
  le niveau de la carte, le gain de la note et le fondu de coupure.
- L'instrument préréglé `kick-instrument` comprend une entrée MIDI, un allocateur Mono, Kick, une
  enveloppe et un filtre de voix (laissés ouverts), un gain et le master.
- L'interface est dans `src/v2/KickView.*`.
- La commande `kick {track, nodeId?, init, set}`, dans le port de contrôle et dans le MCP, règle
  Kick par clé. Elle renvoie aussi le kick mesuré sans être joué (`peakDb`, `rmsDb`, `lengthMs`,
  `endHz`).

## CyDAW Electro

### La banque

Dans **Options › Banques de sons**, la ligne « CyDAW Electro » propose **Générer** au lieu de
Télécharger. On peut aussi double-cliquer sur un kit dans le navigateur, ou ouvrir un projet qui
l'utilise : les sons manquants se fabriquent tout seuls, sans rien demander, en un instant. Ils
arrivent dans le dossier des banques, sous `cydaw-electro/` (Kicks, Snares, Claps, Hats, Cymbals,
Toms, Percussion, FX), avec `LICENCE.txt` et `README.txt`. Ce dossier s'ajoute aussi au navigateur
de samples : on peut glisser les sons partout.

La banque compte 93 sons (4,9 Mo), en WAV 16 bits mono à 48 kHz, avec une crête à -1 dBFS :

- 22 kicks (ceux de Kick, et six autres) ;
- 10 caisses claires et 7 claps ;
- 12 charleys et 6 cymbales ;
- 8 toms ;
- 17 percussions (rims, claves, wood block, cowbells, congas, bongo, shakers, maracas, tambourins) ;
- 11 effets (zaps, laser, risers, downlifter, impacts, sub drop).

### Les kits

Ils sont rangés dans le navigateur, onglet Instruments, sous **Batteries électroniques**. Leurs charleys
forment un groupe d'exclusion : le fermé et la pédale coupent l'ouvert. Ils suivent la disposition General
MIDI : C2 kick, C#2 rim, D2 caisse claire, D#2 clap, F#2 charley fermé,
G#2 pédale, A#2 charley ouvert, C#3 crash, D#3 ride. Les effets sont à partir de C6.

- Boîte à rythmes 808 et Boîte à rythmes 909 ;
- les kits techno, house, trap, lo-fi, hard dance et drum and bass ;
- la Collection de kicks (les 22 kicks, un par touche à partir de C2) ;
- Percussions et effets électro.

### Comment c'est fait

`banks.json` désigne la banque par `"generator": "cydaw-drums"`. Sa liste,
`BankLists/cydaw-electro.json`, donne pour chaque fichier sa taille exacte (44 octets d'en-tête,
plus 2 par échantillon) et sa recette en quatrième élément :

- pour un son : `{"kind", "values", "seed", "seconds"}`, à jouer par `DrumSynth.h`, ou par Kick pour
  un kick ;
- pour la licence et le lisez-moi : `{"text"}`.

`SoundBanks::generate` rend chaque recette, écrit le WAV dans le dossier des téléchargements, vérifie
sa taille, puis le déplace dans la banque. La même recette donne le même fichier, octet pour octet.

Tout se reconstruit avec `tools/factory_content/build_electro_banks.py`. Le script écrit la liste,
l'entrée de `banks.json`, les cartes `Instruments/Electro/<kit>/<kit>.cysampler`, le catalogue et la
`LICENCE.txt` d'usine. `build_bank_registry.py` garde les banques générées quand il réécrit le
registre.

## Exemple

`templates/21-kick-et-batteries-electro.cydaw` est un morceau techno en la mineur, à 126 BPM. Il
dure 32 mesures en quatre parties :

- le groove (mesures 1-8) ;
- la basse roulante de Prisme (9-16) ;
- un break avec la nappe, un downlifter et un riser (17-24) ;
- le drop (25-32).

Le kick vient de Kick (préréglage Techno Hard), accordé pour finir sur A1 (55 Hz). La batterie vient
du kit 909, et les percussions et les effets du kit dédié. Il se reconstruit avec
`tools/templates/build_v2_examples.py --only electro`.

## Vérifié

- Les tests moteur `kickPlaysItsPitchFall`, `drumRecipesRender` et `kickVoicePlaysOut` vérifient
  que la chute de hauteur arrive où il faut, que la note accorde le kick, que la voix joue jusqu'au
  bout malgré le relâchement, que le coup suivant la coupe, et que chaque recette se rejoue à
  l'identique avec une crête à -1 dBFS.
- `cydaw2_electro_check` (`tests/v2_electro_check.py`) tourne en silence, dans des dossiers à lui.
  Il vérifie :
  - la génération de la banque : tailles, format, crêtes, licence, aucun reste dans les
    téléchargements, et un fichier refait identique ;
  - les kits ;
  - un son manquant refait à l'ajout d'un kit ;
  - Kick : réglages par clé, rendu en plug-in exporté, préréglages d'usine, projet enregistré puis
    rouvert.

## Limites

- Les sons de la banque sont mono.
