# Le sampler et les instruments d'usine

CyDAW joue des notes enregistrées de deux façons :

- le **sampler** les rejoue telles qu'elles sont, chacune sur ses touches et ses vélocités (comme le Sampler
  d'Ableton ou de Bitwig, Kontakt, les lecteurs SFZ) ;
- le **modèle de son** (docs/SOUND_MATCHING.md) les analyse en harmoniques et bruit et les resynthétise.

Les **instruments d'usine** (90) existent sous les deux formes, sauf les batteries (échantillons seulement) :

| Famille | Instruments |
|---|---|
| Pianos | à queue Steinway B et Kawai, droits Yamaha et Knight, honky-tonk, feutré, Osiris (una corda), Scarypiano |
| Pianos électriques | piano FM et clavisynth (Yamaha TX81Z) |
| Clavecins | flamand I et II, français, italien, anglais, anglais jeu de luth |
| Harpes | de concert, celtique |
| Cordes | violon (archet, pizzicato), violoncelle solo (archet, pizzicato), contrebasse (archet VSCO, Otto Rubner) |
| Cordes d'orchestre | violons, altos, violoncelles en section (archet, pizzicato) |
| Cuivres | cor, trompette, trombone ténor, tuba, War Tuba |
| Bois | flûte, hautbois, clarinette, basson, flûtes à bec alto et ténor, ocarina, saxophones alto, ténor et baryton |
| Orgues | orgue à tuyaux (grand jeu, jeu doux), orgue Renaissance |
| Percussions mélodiques | marimba, vibraphone, xylophone, glockenspiel, cloches tubulaires, carillon à main, steel drum |
| Guitares | Epiphone, archtop (électrique, acoustique), Gretsch, Hofner, banjo |
| Basses | Jazz Bass, « Dark Black », « Baby Blue », Killer, Swagbass, Bass VI, Big Little, contrebasses jazz, feutrée et électrique |
| Instruments traditionnels | harmonica, đàn tranh, psaltérion à archet, erhu, cithara barbarica, cithare hongroise, talharpa |
| Batteries et percussions | jazz (Virtuosity), Big Rusty, Swirly, Unruly (baguettes, balais), seventies, bobobo, percussions corporelles |
| Synthés échantillonnés, voix | Caveman Cosmonaut, Cowsynth, String Cyborgs ; voix chantée « a » |

Le piano droit **honky-tonk** est fait des notes du Yamaha (chaque note deux fois, désaccordées de 14 cents
l'une de l'autre et écartées en stéréo) et le **piano feutré** de celles du Kawai (les deux couches les plus
douces sur toute l'étendue des vélocités, le filtre à 2,6 kHz, un relâchement plus long).

## Dans CyDAW 2

- **Navigateur › Instruments** : les instruments d'usine par catégorie. Double-clic : une nouvelle piste qui
  joue les échantillons. Clic droit : « Modèle de son » pour la version analysée. Aussi dans **Piste ›
  Instruments d'usine**. Un instrument dont les échantillons ne sont pas encore là affiche à droite leur
  taille (icône de téléchargement) : le double-clic crée la piste et les télécharge, elle joue dès qu'ils
  sont arrivés (une barre de progression remplace la taille) ; clic droit : « Télécharger les
  échantillons ». Le modèle de son, lui, est fourni avec CyDAW et marche sans rien télécharger.
- **Options › Banques de sons…** (ou le bouton du navigateur) : chaque banque connue, son auteur, sa
  licence, sa taille, ce qui en est déjà là ; « Télécharger » (toute la banque) ou « Supprimer » (à la
  corbeille) ; sa provenance (dépôt, version, lien de la licence) ; le téléchargement en cours, sa
  vitesse, « Annuler ».
- **Options › Emplacements…** : où vont les banques, les téléchargements en cours, le cache, le
  compagnon Python, les modèles de son, Mes périphériques et les exports de plug-ins (voir plus bas).
- **Piste › Nouveau sampler (échantillons)…** : des fichiers audio (WAV, FLAC, AIFF, OGG, MP3) sur une nouvelle
  piste. La note de chaque fichier vient de son nom (`C4`, `A#2`, `Db3`, `A-1`, ou un numéro MIDI `_60_`),
  vérifiée par la hauteur entendue (les banques ne numérotent pas toutes les octaves pareil). Sans nom de
  note, c'est la hauteur entendue qui compte. Plusieurs fichiers sur une note deviennent des couches de
  vélocité (rangées par `v1`/`vl2`/`pp`/`mf`/`ff` dans le nom, sinon par leur niveau) ou des round robins
  (`rr1`, `rr2`). Un son sans hauteur (une batterie) prend une touche à lui, à partir de C1, et joue
  jusqu'au bout.
- **Piste › Ouvrir un instrument SFZ…** (ou glisser un `.sfz` dans la fenêtre).
- La **carte Sampler** (Périphériques) montre ses zones : les touches en travers, les vélocités en hauteur.
  Glissez-y des fichiers (du navigateur ou de l'explorateur) pour les ajouter. **Zones…** (ou double-clic)
  ouvre l'éditeur :
  - la carte des zones (un clic choisit) et leur tableau ;
  - pour la zone choisie : racine, touches, vélocités, accord (cents), gain, panoramique, round robin, et
    « Hauteur du son » (racine et accord d'après la hauteur entendue) ;
  - sa forme d'onde avec ses repères à glisser : début (vert), fin (rouge), boucle, et la boucle
    activée ou non, avec son fondu ;
  - Ajouter des échantillons, Ouvrir / Enregistrer en SFZ, Répartir les touches (chaque touche à la
    racine la plus proche), Retirer la zone.
  Chaque modification s'annule (Ctrl+Z).
- **Contrôles (N)** (en haut à droite de la carte, quand l'instrument en a) ouvre l'onglet **CONTRÔLES** de
  l'éditeur : un instrument venu d'un SFZ y montre ses **articulations** (keyswitches : « Sustain · C1 »,
  « Staccato · C#1 »… celle choisie joue les notes suivantes) et ses **contrôleurs nommés** en boutons
  rotatifs, une rangée par élément (« Charleston : position, proche, ambiance, panoramique, accord » ;
  « Micro : A, B, C » du piano Osiris ; pédales…). Double-clic : la valeur de départ de l'instrument ; clic
  droit : l'automatiser dans l'arrangement. En jouant, le MIDI de la piste les bouge aussi (molette,
  pédales, contrôleurs) et une touche de keyswitch change l'articulation : le dernier à bouger l'emporte.
  Batterie les montre dans son onglet **MICROS** (docs/BATTERIE.md).
- Les **réglages** de la carte : niveau, transposition, accord fin, début (ms), vélocité (dB sur toute
  l'étendue), fondu des couches (les deux couches de vélocité autour de la note mêlées à puissance égale),
  boucle (0 aucune ; 1 les boucles des zones, tenues jusqu'au bout ; 2 bouclé tant que la touche est
  enfoncée), suivi du clavier (0 : chaque touche joue la racine), à l'envers, largeur stéréo, jusqu'au bout
  (la note ignore la touche relâchée), articulation (choisie dans l'onglet des contrôles ; −1 : celle de
  l'instrument). L'enveloppe (ADSR) et le filtre de l'instrument mettent en forme par-dessus.

## Le moteur

`include/cydaw/engine/Sampler.h`, `src/engine/Sampler.cpp` : une zone par fichier (touches, vélocités,
racine, accord, gain, panoramique, début, fin, boucle et son fondu, round robin). Une note joue toutes les zones
qui la contiennent (couches, partages de clavier et sons superposés sont une seule chose) ; avec le fondu des
couches, les deux couches de vélocité autour d'elle. Lecture en stéréo, interpolation d'Hermite à 4 points, la
hauteur suivant la note ; la voix polyphonique (`PolyVoice`) filtre chaque canal. 32 voix par piste (comme
dans le plug-in exporté), la pédale de maintien (CC 64) tenue par le synthétiseur.

Un instrument SFZ joue **comme il bouge** :

- les **règles** d'une zone (`SamplerRule` : plage d'un contrôleur, niveau, volume, fondus d'entrée et de
  sortie, panoramique, accord, début ; chacune sur la courbe de la carte qu'elle suit) sont lues à chaque note
  (plages, panoramique, accord, début) et toutes les 64 trames pendant qu'elle joue (niveaux et fondus, lissés
  sur 5 ms : un fader de micro ne claque pas) ;
- les contrôleurs viennent de la **carte** (`sampler.ccN`, −1 : la valeur de départ de la carte
  d'échantillons) et du **MIDI de la piste** : le synthétiseur (`PlanSynthesiser`) les tient dans
  `SamplerPlaying` (partagé par les voix), suit la carte à chaque bloc (un contrôleur que la carte ou
  l'automation bouge reprend sa valeur, les autres restent où le MIDI les a mis ; CC 121 les remet tous) ;
- une touche de **keyswitch** (`sw_lokey`..`sw_hikey`, les `sw_last`) ne joue rien : elle choisit
  l'articulation des notes suivantes (sinon celle de la carte, sinon `sw_default`) ;
- **déclencheurs** : `first` (une touche seule), `legato` (jouée par-dessus une autre), `release` (une touche
  relâchée : la zone part à ce moment-là, une fois, hors de l'enveloppe de la note qui se ferme dessous,
  plus basse de `rt_decay` dB par seconde tenue ; une note coupée — exclusion, ligne monophonique — n'en joue
  pas).

Les échantillons sont décodés hors du fil audio (`SampleStore`, `src/app/SamplerMapFile.*`), une fois pour
toutes les zones et toutes les pistes qui les jouent, en 16 bits normalisés (le niveau gardé à part). Ceux des
zones que l'instrument n'atteint pas tel qu'il s'ouvre (un micro à zéro, une autre articulation, une plage de
contrôleur hors de sa valeur) **attendent** : un réglage de la carte qui les atteint les fait décoder
aussitôt (`SampleStore::want`), une note qui les demande aussi (elle entre, en fondu, quand ils sont là). Une
note jouée avant que son échantillon soit lu entre dès qu'il l'est ; le plug-in exporté attend ceux qu'il
atteint avant de se dire restauré.

**Lecture depuis le disque.** Un enregistrement long (plus de 2,5 s au-delà de sa tête) ne garde en mémoire
que sa **tête** : 0,6 s à partir du début de sa zone (et jusqu'au bout de sa boucle quand elle en a une). Une
voie qui joue au-delà prend un **anneau** (`SampleStream`, 16 384 trames, 320 pour toutes les pistes, réservés au
premier enregistrement long) dès qu'elle approche de la fin de la tête (0,3 s avant) ; le **lecteur**
(`SampleStore`, un fil à part) ouvre le fichier et le remplit devant elle (derrière elle à l'envers). Sans fil
ni verrou côté audio : la voie annonce où elle lit, le lecteur ne touche jamais ce qu'elle va lire. Si le
disque est en retard, la voie qui n'a pas encore sonné attend, celle qui joue passe la trame (comptée :
`streamUnderruns`). Un rendu plus rapide que le temps réel (la vérification hors ligne, un hôte qui rend un
plug-in exporté hors ligne) attend le disque. Gain mesuré : piano Steinway 263 → 7 Mo, Osiris 619 → 45 Mo,
kit Rouillée 685 → 181 Mo, le même son au dB près, chargement presque instantané ; 81 anneaux à la fois sur
des accords denses d'Osiris avec un groove de batterie en temps réel, sans trame manquée.
`CYDAW_SAMPLE_STREAMING=0` garde tout en mémoire. La forme d'onde de l'éditeur de zones est lue dans le
fichier pour un enregistrement long.

## Le format (`.cysampler`, « cydaw-sampler » v1)

Le JSON de la carte, enregistré dans le projet (les fichiers par leur chemin, pas leur son) :

```json
{ "format": "cydaw-sampler", "version": 1, "name": "Steinway B grand piano",
  "zones": [ { "file": "factory:Instruments/Steinway B Grand/D4_3.flac", "root": 62,
               "keys": [61, 63], "velocities": [43, 84], "tune": -3.5, "gain": 0, "pan": 0,
               "start": 0, "end": -1, "loop": [24000, 48000], "loopCrossfade": 3528,
               "sequence": [1, 1] } ],
  "settings": { "velocity-db": 30 } }
```

Ce qu'un SFZ garde de ce qui le fait bouger (`docs` du format dans `src/app/SamplerMapFile.h`) : dans une zone,
`trigger` (`release`, `first`, `legato`), `releaseDecay` (dB par seconde tenue), `keyswitch` (la touche qui
la choisit), `rules` (`{"cc": 70, "level": 100}`, `{"cc": 4, "range": [0, 63]}`, `{"cc": 1, "fadeIn": [0,
64]}`, `pan`, `tune`, `volume`, `offset`, et `curve` : une des courbes de la carte) ; dans la carte,
`controllers` (`{"70": 100}` : où ils commencent), `controls` (`[{"cc": 70, "label": "Kick mic"}]` : ceux que
la carte montre), `curves` (128 valeurs chacune), `keyswitches` (`{"keys": [24, 25], "start": 24, "labels":
{"24": "Sustain"}}`). Les cartes d'usine s'écrivent une zone par ligne.

`group` / `offBy` (facultatifs) : l'exclusion des batteries. Une note sur une zone du groupe *n* coupe en 6 ms
les voix qui jouent des zones dont `offBy` vaut *n* : le charley fermé coupe l'ouvert. Le SFZ les donne par
`group` et `off_by`.

`factory:` désigne le dossier des instruments d'usine (`Factory` à côté de CyDAW ; `content/factory` dans les
sources) ; `bank:` le dossier des banques téléchargées (`bank:vcsl/Chordophones/…/A0_vl2_rr1.wav` : la banque
puis le chemin dans son dépôt). Un plug-in exporté emporte ses échantillons (voir les limites). `start` / `end` :
la partie du fichier jouée (une note coupée dans un enregistrement plus long) ; `endFade` : les trames fondues
avant `end`. `settings` (facultatif) : les réglages de la carte pour lesquels la carte a été faite. Un fichier
`.cysampler` peut donner des chemins relatifs à son dossier.

Les cartes des versions où CyDAW livrait les notes en FLAC (`factory:Instruments/…/C4_2.flac`) jouent
maintenant les enregistrements des banques : `Factory/Instruments/legacy.json` dit pour chaque FLAC
l'enregistrement et la trame où il avait été coupé, et la zone est réécrite au chargement (début, fin et
boucle décalés d'autant).

SFZ lu : `sample`, `lokey`/`hikey`/`key`/`pitch_keycenter` (noms de notes, `c4` = 60), `lovel`/`hivel`,
`tune`, `transpose`, `volume`, `pan`, `offset`, `end`, `loop_mode` / `loop_start` / `loop_end` (ou la boucle
écrite dans le WAV, lue dans son en-tête), `seq_length` / `seq_position`, `group` / `off_by` (l'exclusion), `lorand` / `hirand` (joués à tour de
rôle), `default_path`, `note_offset`, `octave_offset`, `#define`, `#include`, l'héritage `<control>` /
`<global>` / `<master>` / `<group>` / `<region>`.

Ce que ses contrôleurs font est gardé en règles jouées en direct ; leurs valeurs de départ (`set_ccN`,
`set_hdccN`, sinon volume 100, panoramique 64, expression 127, les autres 0) et leurs noms (`label_ccN`) vont
avec la carte :

- les régions jouées : `locc`/`hicc`, `lohdcc`/`hihdcc` (le canal 1, le programme 0 et le pitch bend au centre
  restent fixes) ;
- leur niveau : fondus `xfin_locc`/`xfin_hicc` et `xfout_locc`/`xfout_hicc` (courbe en puissance),
  `amplitude_oncc` / `amplitude_cc` (un fader de micro : son pourcentage au sommet du contrôleur, rien à zéro),
  `volume_oncc`, `gain_oncc` / `gain_cc` ; `amplitude`, les volumes et amplitudes ARIA des niveaux
  (`group_volume`…, qui s'ajoutent) restent fixes ;
- leur panoramique, accord et début : `pan_oncc` / `pan_cc`, `tune_oncc` / `tune_cc` / `pitch_oncc`,
  `offset_oncc` / `offset_cc` ;
- les courbes : `<curve>` (`curve_index`, `vNNN`, droites entre les points) et celles de SFZ (1 à 6), que
  suivent `amplitude_curveccN`, `volume_curveccN`, `pan_curveccN`, `tune_curveccN`, `offset_curveccN` ;
- les keyswitches : `sw_last`, `sw_lokey`/`sw_hikey`, `sw_default`, `sw_label` ;
- les déclencheurs : `trigger=attack|first|legato|release|release_key`, `rt_decay`.

Les régions d'une touche tenue (`sw_down`, `sw_up`, `sw_previous`) sont laissées de côté. Les instruments
d'usine refaits ainsi sonnent comme avant à leur départ, sauf là où l'ancienne lecture figée se trompait :
les courbes de panoramique et de niveau ignorées (la caisse claire et le charleston de Big Rusty penchaient
à droite, la crash d'Unruly aussi) et `tune_cc` oublié (le kit « seventies » et les bobobo jouaient une
octave trop bas). Un échantillon absent là où le SFZ le dit est cherché sans le `$nom`
qu'aucun `#define` n'a donné, puis dans `../Samples` (une carte écrite pour être incluse par une autre).
Par le port de contrôle, `sampler_file` lit un SFZ ou une carte sans charger aucun échantillon, avec d'autres
valeurs de départ si on les donne (`controllers`).

## Les instruments d'usine : d'où ils viennent

Enregistrements **CC0** (domaine public : libres, redistribuables, sans mention obligatoire), de Versilian
Studios : [VCSL](https://github.com/sgossner/VCSL) et
[VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE). `Factory/LICENCE.txt` les crédite. Le
registre de toutes les banques, utilisées ou candidates, avec leur licence et leur provenance :
`docs/SOUND_SOURCES.md`.

**CyDAW ne livre pas les échantillons** : `Factory` (quelques Mo) contient les cartes des instruments, leurs
modèles de son et la liste des banques ; les enregistrements se téléchargent depuis le logiciel, du dépôt de
chaque banque, dans le dossier des banques.

1. `tools/factory_content/fetch_banks.py` télécharge les banques entières (CC0, Unlicense) dans le dossier des
   banques (`--dest`, sinon `CYDAW_BANKS_DIR`, sinon celui choisi dans CyDAW, sinon
   `%LOCALAPPDATA%/CyDAW/Banks`) : `<banque>/bank.json` (dépôt, branche, licence, fichiers).
2. `tools/factory_content/fetch_sources.py --dest <dossier des banques>` choisit les notes tenues des
   instruments (`SELECTION`) et écrit `manifest.json` (les fichiers déjà là ne sont pas retéléchargés).
3. `tools/factory_content/build_factory_content.py --tool <cydaw_sample_tool>` en fait les instruments
   sampler dans `content/factory` : chaque note mesurée par `cydaw_sample_tool` (où son son commence, où il
   s'est éteint), jouée de l'un à l'autre dans l'enregistrement de la banque (`bank:`, fondue sur 60 ms à la
   fin), sa hauteur mesurée (YIN) pour sa racine et son accord (chaque note au tempérament égal ; un piano
   seulement par sa moyenne, il garde son accord étiré), les couches réparties sur les vélocités, les sons
   tenus bouclés là où ils se répètent le mieux ; puis les instruments dérivés (`DERIVED` : honky-tonk,
   feutré), qui reprennent les fichiers d'un autre ; et `Instruments/legacy.json`. (`--samples flac` : les
   notes coupées en FLAC 16 bits à côté des cartes, comme avant.)
4. `tools/factory_content/build_sfz_instruments.py` fait les instruments des banques SFZ (Karoryfer,
   Virtuosity Drums, jSteelDrum…) : chaque programme choisi lu par CyDAW lui-même (une session muette,
   `sampler_file`), sa carte écrite avec ses fichiers en `bank:`, listée après les autres.
5. `tools/factory_content/build_factory_models.py` en fait les modèles de son (`content/factory/Sound Models`,
   toutes les couches de vélocité de chaque note ; pour un instrument SFZ, chacune de ses plages de vélocité,
   premier round robin, un seul micro, sans ses bruits), en stéréo, avec la tête enregistrée de chaque note,
   en format compact (80 modèles, 176 Mo). Les batteries n'en ont pas. `--tool` choisit le CyDAW qui convertit
   les FLAC en WAV pour le compagnon.
6. `tools/factory_content/build_bank_registry.py` écrit la liste des banques téléchargeables
   (`content/factory/banks.json`, `BankLists/<banque>.json`) : pour chaque banque à la licence confirmée,
   son dépôt et le commit lu (`git ls-remote` : un changement plus tard en amont ne change pas ce que
   CyDAW télécharge), son auteur, sa licence et chacun de ses fichiers avec sa taille.

`tools/package_v2_test.py` met `content/factory` dans le paquet sous le nom `Factory`.

## Le téléchargement des banques

`src/v2/SoundBanks.*` : le registre (`Factory/banks.json`, ou `CYDAW_BANKS_REGISTRY`), ce qui de chaque
banque est sur le disque (compté sur un fil à part), ce qu'il manque à une carte. Un téléchargement est une
tâche (un instrument, une banque) ; elles passent l'une après l'autre, quatre fichiers à la fois. Un fichier
vient de `raw.githubusercontent.com/<dépôt>/<commit>/<chemin>` (`media.githubusercontent.com` pour Git LFS ;
`baseUrl` + chemin pour une banque qui en donne une, un serveur de test), s'écrit dans le dossier des
téléchargements (`.part`), est vérifié à sa taille, puis rangé dans `<banques>/<id>/<chemin>` ; les fichiers
de licence et de présentation de la banque viennent avec. À la fin, les samplers qui jouent la banque
rechargent leurs échantillons.

Une banque **générée** (`"generator": "cydaw-drums"`, CyDAW Electro) ne se télécharge pas : chaque fichier
de sa liste porte sa recette en quatrième élément, et CyDAW écrit le WAV lui-même (16 bits, mono, à la
fréquence `rate` de la banque), de la taille exacte que la liste donne. Ses sons manquants se font seuls à
l'ajout d'un de ses kits ou à l'ouverture d'un projet qui les joue (`Studio::generateMissingSounds`). Voir
docs/KICK_ET_ELECTRO.md.

## Les emplacements

`src/v2/Locations.*`, réglés dans **Options › Emplacements…** et gardés dans
`%APPDATA%/CyDAW/Locations.json` (seulement ceux qui ont quitté leur défaut) :

| Emplacement | Par défaut |
|---|---|
| Banques de sons | `%LOCALAPPDATA%/CyDAW/Banks` (`CYDAW_BANKS_DIR` le change) |
| Téléchargements en cours | `%LOCALAPPDATA%/CyDAW/Downloads` |
| Cache (index des échantillons, compilations VST3) | `%LOCALAPPDATA%/CyDAW/Cache` |
| Compagnon Python | `%LOCALAPPDATA%/CyDAW/companion` |
| Modèles de son | `Documents/CyDAW/Sound Models` |
| Mes périphériques | `Documents/CyDAW/Devices` |
| Exports de plug-ins | `Documents/CyDAW/Plugin Exports` |
| Préréglages | `Documents/CyDAW/Presets` |

Changer un dossier y emporte (case cochée) ce que CyDAW avait mis dans l'ancien, et rien d'autre : les
banques (dossiers avec leur `bank.json`), l'index des échantillons, l'environnement du compagnon, les
`.cysound`, les `.cydawdevice`, les `.cypreset`. Sur le même disque, un renommage ; sinon une copie sur un fil à part, avec
sa progression, puis les originaux effacés une fois tout copié (annulé : les copies partent, les originaux
restent). Les compilations VST3 ne déménagent pas (CMake ne le permet pas) : elles se refont. Les sessions
automatisées lisent ces choix sans les changer, sauf avec un fichier à elles (`CYDAW_LOCATIONS_FILE`).

## Vérifié

- `cydaw_engine_tests` : `samplerPlaysItsZones` (zones, hauteur, couches, fondu, vélocité en dB, fin et
  boucle, round robin, stéréo jusqu'à la voix) ; `samplerPlaysAsItMoves` (un micro à son niveau, un autre
  entré par sa plage, un contrôleur bougé en jouant sans saut, le MIDI gardé jusqu'à ce que la carte bouge,
  CC 121, une courbe, les keyswitches, first/legato, le fondu des couches avec tous leurs micros, un
  relâchement joué hors de l'enveloppe et plus bas après une seconde tenue, aucun après une exclusion).
- `cydaw_engine_tests` : `samplerStreamsLongRecordings` (un enregistrement de 6 s avec 0,3 s en mémoire, un
  lecteur de test : le même son que l'enregistrement entier, à l'endroit et à l'envers, les anneaux rendus,
  aucune trame manquée).
- `cydaw2_sfz_live_check` (ctest, port 47847, silencieux) : un petit SFZ (deux micros, un relâchement, deux
  articulations) — ses contrôles et articulations sur la carte, les zones pas encore atteintes en attente, un
  micro réglé, un autre monté de zéro puis décodé, le MIDI qui coupe un micro en pleine note, l'articulation
  choisie par la carte ou jouée par sa touche, le relâchement à −6 dB après une seconde, un enregistrement de
  9 s lu depuis le disque (sa tête seule en mémoire, joué sans trou), un projet rouvert ; avec les banques, les
  overheads du kit de Batterie montés.
- `cydaw2_banks_check` (ctest, silencieux, hors ligne) : un serveur HTTP local tient lieu de GitHub ; une
  carte aux échantillons manquants (comptés avec leur taille), la banque téléchargée dans les dossiers du
  test (la licence avec, rien qui traîne dans les téléchargements), le sampler qui les joue alors, un fichier
  des banques gardé en `bank:`, une carte de l'époque FLAC réécrite sur l'enregistrement, le dossier des
  modèles déplacé avec ses modèles et sans le reste.
- `cydaw2_sampler_check` (ctest, silencieux) : des notes de test placées par leur nom, jouées sur la piste, dans
  un périphérique, annulées et rétablies, après réouverture du projet, après l'atelier, depuis Mes
  périphériques, depuis un SFZ, par le plug-in exporté et par le plug-in « CyDAW Graph Runtime » compilé, puis
  le projet natif généré (son plan, ses zones et chaque échantillon dans `Voice.cyvoice`) ; les
  instruments d'usine (tous les fichiers trouvés, un clavecin, le violon tenu 20 s grâce à sa boucle, les
  modèles de son).

## Limites

- Les têtes et les enregistrements courts sont en mémoire (16 bits) ; une batterie aux cymbales courtes reste
  surtout en mémoire (ses sons sont courts), ceux qu'aucun réglage n'atteint attendent. 320 anneaux pour toutes
  les pistes : au-delà, une voie de plus joue sa tête puis se tait. Un seul fil lit le disque.
- D'un SFZ, CyDAW ne joue pas encore les enveloppes par région (`ampeg_*` et leurs contrôleurs), les courbes de
  vélocité (`amp_velcurve_N`), les filtres et égaliseurs des régions, les LFO, `polyphony` ni `delay` ; les
  programmes « vélocité » des banques sont ceux choisis pour les instruments d'usine.
- Les instruments VCSL/VSCO (cartes faites à partir des enregistrements) n'ont ni bruits de relâchement
  (sautereau du clavecin, étouffoir du piano) ni résonance par sympathie ; ceux des banques SFZ ont ceux de
  leur SFZ.
- Le violon n'a ni legato ni changement d'archet (une note à la fois, par zone).
- L'export natif (VST3, CLAP, LV2) d'un sampler copie chaque échantillon décodé (16 bits) dans le fichier
  `Voice.cyvoice` du plug-in : il s'ouvre sur n'importe quelle machine, mais pèse le poids de l'audio non
  compressé, et le plug-in le charge en entier en mémoire à son activation (pas de lecture continue depuis le
  disque). Détails dans [PLUGIN_FORMATS.md](PLUGIN_FORMATS.md).
- Un enregistrement court est décodé en entier, même quand la zone n'en joue qu'une partie.
