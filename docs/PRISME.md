# Prisme — le synthé à tables d'ondes de CyDAW

Un instrument dans l'esprit des grands synthés à tables d'ondes : trois oscillateurs à tables d'ondes (chacun
avec sa déformation et son **spectre**), un sub, un bruit, un filtre multimode, quatre enveloppes, quatre LFO
(stéréo si on veut), une matrice de seize modulations, quatre macros, et le jeu en poly, mono ou legato avec
glissando. Tout est du DSP C++ maison (pas de plug-in tiers).

**Le créer** : Piste › Nouveau Prisme (synthé à tables d'ondes) — une piste MIDI dont l'instrument est Prisme,
sa fenêtre ouverte. Aussi dans le catalogue « + » (Instruments › Prisme). Le modèle **13-prisme** le fait
entendre (nappe, pluck, basse Reese, lead supersaw, cloches, basse growl et une batterie d'usine).

## L'interface

Sur la carte (Périphériques) : la table de l'oscillateur A, les potards qui comptent le plus (positions A et B,
fréquence et résonance du filtre, attaque et relâchement, macros, volume) ; sa fenêtre s'ouvre par
le bouton de l'en-tête de la carte.

La fenêtre, quatre onglets :

- **OSC** :
  - **OSC A / OSC B / OSC C** : la table (liste, ◀ ▶), son affichage en 3D (les images empilées, celle qui joue
    devant), WT POS, la déformation (Sync, Bend +/−/±, PWM, Asym +/−, Miroir, Inverser, Quantifier, FM/AM/RM par
    un autre oscillateur : A lit B, B et C lisent A) et sa quantité, le **spectre** et sa quantité, Phase, Rand,
    l'unisson (1 à 16 voix) avec Désaccord, Mélange et Largeur stéréo, Pan, Niveau, Octave, Demi-tons, Fin,
    Hauteur, F (vers le filtre). Quand la fenêtre est assez large, les trois oscillateurs sont côte à côte ;
    sinon B et C se partagent leur place, deux onglets au-dessus (un point sur celui qui joue).
    - **Le spectre** remodèle la table dans ses harmoniques, d'une quantité qu'on peut moduler (une enveloppe,
      un LFO, une macro) : **passe-bas** (les aigus retirés, de moins en moins d'harmoniques), **passe-haut**
      (les aigus seuls), **impairs** (les harmoniques paires retirées : creux comme une clarinette), **pairs**
      (les impaires retirées : une octave plus haut), **peigne** (des creux qui se promènent dans le spectre,
      comme un phaser), **octave** (l'octave au-dessus mêlée), **formant** (la forme lue plus vite dans une
      fenêtre : sa résonance monte). C'est la même table relue autrement (un autre point du cycle, une autre
      largeur de bande), sans calcul de spectre pendant le jeu.
  - **SUB** : forme (sinus, triangle, dent de scie, carré, impulsions 25 % et 10 %), octave, niveau, pan,
    sortie directe.
  - **BRUIT** : blanc, rose, brun, numérique, craquements, souffle ; couleur, vitesse, niveau, pan, vers le
    filtre.
  - **FILTRE** : passe-bas 12/24, Ladder, passe-haut 12/24, passe-bande 12/24, coupe-bande, cloche, peigne +/−,
    formants (A-E-I-O-U par Variation) ; Fréquence, Résonance, Drive, Variation, Mélange, Suivi du clavier ;
    les boutons A B C N S disent qui passe par lui ; sa courbe de réponse.
  - **ENV 1 à 4, LFO 1 à 4** : l'enveloppe dessinée (attaque, maintien, déclin, soutien, relâchement et leurs
    courbes ; l'env 1 fait le volume), le LFO dessiné (8 formes et « Dessinée », redéclenché / enveloppe /
    libre, BPM avec division ou Hz, montée, retard, phase, lissage, 0..1, **stéréo** : la droite en avance sur la
    gauche jusqu'à une demi-période, ce qu'il fait au filtre et au volume diffère d'un côté à l'autre — un
    filtre qui tourne dans l'espace, un trémolo qui passe d'un haut-parleur à l'autre).
    - **Forme dessinée** : choisie dans la liste des formes, son cycle s'affiche avec ses 32 points. On
      glisse sur la courbe pour les dessiner : un geste rapide ne laisse pas de trou, les points entre deux
      positions de la souris suivent une droite. Entre deux points, le LFO lit une droite. Clic droit : des
      formes toutes faites (sinus, triangle, rampes, carré, escalier de 8 marches, pompe façon sidechain,
      aléatoire) et des retouches (lisser, inverser haut et bas, à l'envers, remettre à zéro). Les points
      sont rangés dans la carte (`lfo1.draw1` à `lfo4.draw32`) : le projet, un préréglage et un VST3 exporté
      les gardent.
  - **MACROS** 1 à 4 et les autres sources (vélocité, note, molette, aftertouch, aléatoire).
- **MATRICE** : seize lignes source → quantité (±100 %) → destination (par section).
- **EFFETS** : les effets de la piste après Prisme (distorsion, réducteur, flanger, phaser, chorus, délai,
  compresseur, égaliseur, filtre, réverbe, limiteur) : un clic en ajoute un, ses potards sont là, la croix le
  retire. Ce sont les périphériques de CyDAW, aussi dans Périphériques.
- **GLOBAL** : poly / mono / legato, nombre de voix, glissando (et « toujours glisser »), plage du pitch bend,
  vélocité → niveau, volume ; Initialiser le son.

**Moduler** : glisser une source (ENV 2, LFO 1, M1…) sur un potard, ou clic droit › Moduler par. Un anneau
violet autour du potard montre jusqu'où il va ; clic droit pour régler la quantité ou retirer. Double-clic :
valeur par défaut. Les préréglages (barre du haut de la fenêtre, bouton de la carte) : 65 sons d'usine (noms en anglais), par exemple Breathing Reese, Neuro Growler, Trance Supersaw, Pumping Future Bass Chords, Trance Gate, Tension Riser, et les vôtres. Un préréglage garde le son entier : le troisième oscillateur, les spectres, la stéréo
des LFO et leurs points dessinés compris ; ce qu'il ne fixe pas reprend sa valeur par défaut.

## Le moteur

`include/cydaw/engine/PrismSynth.h`, `src/engine/PrismSynth.cpp` :

- Les réglages : un tableau de nombres (`PrismValues`), chacun décrit par `prism::parameter()` — clé
  (`a.position`, `env2.decay`, `m5.amount`), noms français et anglais, plage, défaut, sorte (continu, entier,
  bascule, choix), échelle (logarithmique pour fréquences et vitesses, resserrée vers zéro pour les temps).
- Ajoutés après coup, rangés après tout le reste (les anciens réglages gardent leur place et leurs clés) :
  l'oscillateur C (`c.*`), le spectre de chaque oscillateur (`a.spectral`, `a.spectralAmount`…), la stéréo de
  chaque LFO (`lfo1.stereo`…) ; leurs destinations de modulation après les anciennes.
- Les tables : 2048 échantillons par image, chacune gardée à 10 largeurs de bande (FFT), la note lit celle qui
  ne peut pas replier ; 12 tables d'usine générées (Basic Shapes, Analog PWM, Harmonic Sweep, Formant Vowels,
  Sync Sweep, Digital Steps, Drawbar Organ, FM Bells, Growl, Spectral Noise, Square Build, Overtones).
- La voix : unisson jusqu'à 16 par oscillateur (désaccord symétrique, panoramique étalé, mélange centre/côtés),
  déformations de phase et de sortie, FM/AM/RM entre oscillateurs, sub en polyBLEP, bruits, filtres à variable
  d'état (TPT), Ladder 4 pôles saturé, peigne, formants ; enveloppes AHDSR courbées, LFO, matrice appliquée
  par bloc de 32 échantillons ; glissando linéaire en demi-tons ; ~1 % d'un cœur par voix pour un supersaw
  de 14 voix d'unisson.
- Le jeu (`src/app/PlanSynthesiser.h`) : un `juce::Synthesiser` qui sait la limite de voix (la plus ancienne
  part, relâchée d'abord), le mono et le legato (une voix, les notes liées glissent sans relancer), le pitch
  bend, la molette (CC 1), l'aftertouch, le tempo et l'horloge des LFO libres. Les pistes MIDI et le VST3
  exporté l'utilisent.
- Dans le graphe : la carte `prism-synth` (`core.prism-synth`), le préréglage `prism-instrument` (l'allocateur,
  Prisme, et l'enveloppe, le gain et le filtre de voix laissés ouverts — Périphériques les cache), le plan des
  voix (`PolyOscillatorWaveform::prism`, `prismValues`, `prismTables`).
- Export VST3 : Prisme joue dans le plug-in ; 16 réglages sont proposés comme paramètres de l'hôte (macros,
  positions, filtre, attaque/relâchement, glissando, volume…), les autres se cochent dans l'atelier
  (Paramètres, 32 au plus).

## Port de contrôle et MCP

- `add_midi_track {name, instrument: "prism-instrument"}` : une piste Prisme.
- `prism {track, init, set: {"a.position": 0.5, "m1.source": "lfo1", "m1.destination": "filter.cutoff",
  "filter.type": "Low pass 24"}}` : lit et règle par clé ; rend toutes les valeurs, les sources, les
  destinations et les tables.
- `show_dialog {name: "prism"}` : la fenêtre de la piste choisie.
- Les préréglages par `presets {track, nodeId}` comme toute carte.

Les sons d'usine sont faits par `tools/factory_content/build_prism_presets.py` (chacun réglé, écouté hors ligne,
enregistré dans `content/factory/Presets/device/prism-synth`).

## Automation

Chacun de ses réglages s'automatise dans l'arrangement : clic droit sur un potard de sa carte ›
« Automatiser dans l'arrangement », ou le menu des couloirs de la piste. La courbe joue sur toutes
les voix, dès le début de chaque note (ce qu'une note lit une seule fois, comme son enveloppe, part de
la valeur du moment). Il en va de même pour Kick, le sampler et le modèle de son. Port de contrôle :
`add_automation {track, key: "node.<id>.prism.voice.master", points}` (`list_parameters` donne les
clés).

## Tables importées

Le bouton dossier à côté de la table d'un oscillateur (onglet OSC) en charge une d'un fichier :

- une table d'ondes de Serum ou de Vital (`.wav`) : ses cycles de 2048 échantillons (ou la longueur que
  dit son bloc « clm »), jusqu'à 256 images ;
- un fichier dont la longueur est un multiple de 2048, 1024, 512 ou 256 : des cycles de cette taille ;
- un son court (4096 échantillons au plus) : un seul cycle ;
- n'importe quel autre son : 64 cycles pris tout du long à sa hauteur (trouvée par autocorrélation),
  chacun commençant où l'onde remonte par zéro (2048 échantillons sans hauteur nette).

Chaque image est limitée en bande comme les tables d'usine. La table est rangée dans la carte (16 bits),
si bien que le projet, un préréglage et un VST3 exporté la gardent. Son nom remplace la liste ; ✕ (ou une
des flèches) revient aux tables de Prisme. Port de contrôle : `prism {importTable: {osc, file}}`,
`clearTable`.

## Vérifié

- `cydaw_engine_tests` (`prismSynthPlays`) : justesse (440 Hz), unisson stéréo, filtre, chaque type de filtre
  fini et sonore, fin de note, glissando, LFO sur la position, tables d'usine propres, LFO dessiné (lu entre
  ses points, bouclé sur son cycle) ; (`prismGoesFurther`) : l'oscillateur C juste, sa FM qui lit A, le spectre
  passe-bas qui assombrit, le passe-haut qui éclaircit, « impairs » qui retire les harmoniques paires, chaque
  spectre borné en grave et en aigu, un LFO stéréo qui sépare la gauche et la droite ; les nouveaux réglages
  rangés après les anciens (un projet, un préréglage ou un plug-in d'avant se relisent pareil).
- Le rendu par le chemin d'export (`instrument_export_check`) de chaque son d'usine ; `cydaw2_roundtrip_13-prisme`
  et `cydaw2_device_taps_13-prisme`.

## Limites

- Les effets de l'onglet EFFETS s'ajoutent dans l'ordre des clics (ils se réordonnent dans Périphériques).

## Les modulations à l'écran

Pendant qu'une note joue, Prisme montre ce que fait sa voix la plus récente : sur chaque bouton
modulé, un point là où la matrice le met (en plus de l'arc de sa plage), la courbe du filtre tracée
une seconde fois en violet là où elle se trouve, un point qui court sur chaque LFO et une ligne au
niveau de chaque enveloppe (`prism::Voice::live`, `VoiceLive`, lus par `PrismView`).
