# Les effets de studio

Quatre effets complets, dans l'esprit des grands plug-ins de studio (FabFilter Pro-R, Pro-C, Pro-L et
Volcano) : **Réverbe Pro**, **Compresseur Pro**, **Limiteur Pro** et **Filtre Pro**. Chacun a sa carte dans
Périphériques (l'affichage principal, quelques champs) et, avec le bouton ⤢ de sa carte, une **fenêtre
complète** : grand affichage interactif et tous les réglages. Tous ont des préréglages d'usine (le
bouton liste de la carte), s'automatisent (clic droit sur une molette › « Automatiser dans
l'arrangement »), se modulent par câble (les réglages principaux ont une prise) et passent dans les
Graph VST exportés.

Ils sont dans l'arbre des périphériques : Effets › Échos et réverbération (Réverbe Pro), Dynamique
(Compresseur Pro, Limiteur Pro), Filtres et EQ › Filtres (Filtre Pro).

## Réverbe Pro

Une réverbération à réseau de seize lignes à retard (matrice de Hadamard, sans perte), modulées
lentement, avec un filtre de décroissance par ligne : le temps de réverbération se règle **par bande**.

- **Affichage** : la courbe du temps de décroissance selon la fréquence (de 20 Hz à 20 kHz, en
  secondes). Trois poignées **G** (graves), **M** (médiums), **A** (aigus) : à gauche/droite leur
  fréquence, en haut/bas leur décroissance (un multiple de la décroissance principale) ; molette sur
  une poignée pour la changer, double-clic pour la remettre à x1. Glisser ailleurs ou molette : la
  décroissance principale. Les zones sombres aux bords sont les **coupe-bas / coupe-haut** (glisser
  leur bord). En haut à droite, l'**impulsion** telle que les réglages la dessinent : le pré-délai,
  les premières réflexions, la queue. Le spectre du son passe derrière.
- **Espace** (taille du réseau), **Décroissance** (secondes jusqu'à -60 dB), **Pré-délai** (ms, ou
  **Synchro** sur une valeur de note du tempo), **Distance** (moins de premières réflexions, une queue
  plus diffuse et un peu plus tardive), **Brillance**, **Caractère** (le mouvement des lignes : de net à
  chorusé), **Largeur**, **Mélange**.
- **Décroissance par bande** : graves x / fréquence, médiums x / fréquence, aigus x / fréquence.
- **Après** : coupe-bas, coupe-haut, **Diffusion**, **Réflexions** (niveau des premières réflexions),
  **Ducking** (la réverbération s'efface quand le son direct joue) et son retour, **Sortie**, **Gel**
  (la queue tient sans fin, rien de nouveau n'y entre).
- Préréglages : 29 (noms en anglais), par exemple Lead Vocal Plate, Snare Room Slam, Piano Concert Hall, Shoegaze Wash, Cinematic Trailer Hall.

## Compresseur Pro

Le compresseur de studio de CyDAW, enrichi (un projet qui l'utilisait déjà sonne pareil : ses nouveaux
réglages prennent les valeurs d'avant).

- **Affichage** : les niveaux qui défilent (entrée en gris, sortie en couleur), la réduction de gain
  en rouge, le seuil à glisser ; la courbe de transfert avec son genou (le style et la plage y sont).
- **Styles** : Clean (net, à anticipation possible), Classic (rétroaction), Opto (relâchement qui
  dépend du signal), Voix, Mastering (très doux), Bus (cohésion), Punch (laisse passer l'attaque),
  Pompage (relâchement marqué).
- **Seuil, Ratio, Genou, Plage** (la réduction maximale), **Attaque, Relâchement, Maintien,
  Anticipation** (look-ahead, jusqu'à 20 ms), **Compensation, Mélange** (compression parallèle),
  **Lien stéréo**, **Détecteur PH / PB** (filtrer ce qu'entend le détecteur).
- Options : **Stéréo / Mid / Side** (ne compresser que le centre ou les côtés), **Relâchement auto**,
  **Gain auto**, **Écoute** (entendre le détecteur, après ses filtres).
- Préréglages : 31 (noms en anglais), par exemple Pop Lead Vocal Tight Control, Snappy Kick, Explosive Parallel Drums, Synth Bass Ducking Under the Kick (Sidechain), Mid-Side Master (Mid Only).
- **Sidechain** : le bouton maillons de sa carte (dans Périphériques) choisit la piste que son détecteur
  écoute à la place du son qu'il compresse : un kick qui fait pomper une basse, une voix qui baisse la
  musique. CyDAW ajoute à la piste une entrée « Sidechain », la relie à la prise sidechain du
  compresseur et y route la piste choisie, avant son fader (le kick peut être baissé, voire mis à
  -60 dB dans le mixage, il fait toujours pomper ; coupé, il ne fait plus rien). « Son propre » revient
  au son de la piste. Le projet garde ce choix. Dans le graphe, la carte a une prise d'entrée
  « sidechain » qui se relie à la main à n'importe quelle entrée de la piste. La Porte a la même :
  un kick qui ouvre un accord tenu. Port de contrôle : `sidechain {track, nodeId?, source | clear}`.

## Limiteur Pro

Un limiteur de mastering à anticipation : la gain descend en rampe avant le pic (minimum puis moyenne
glissants), ce qui ne laisse rien passer au-dessus du plafond, **true peak** compris (les crêtes entre
les échantillons, que la conversion fera entendre).

- **Affichage** : les niveaux qui défilent (ce qui entre, gain compris, en gris ; ce qui sort en
  couleur), la réduction en rouge depuis le haut, le **plafond** en pointillés à glisser (molette,
  double-clic : valeur par défaut). À droite, les **mesures** : crête, true peak, loudness
  momentanée (M, 400 ms), à court terme (S, 3 s) et intégrée (I, avec les deux portes de la norme
  BS.1770), avec la **cible** (un trait) ; « Remettre la mesure à zéro » recommence l'intégrée. La
  mesure est faite par l'interface sur le son de la carte (pondération K, une approximation
  honnête : le mix est lu en mono, les deux canaux comptés).
- **Styles** : Transparent, Percutant, Dynamique, Polyvalent, Agressif, Moderne, Sûr, Bus (comment la
  gain revient, un étage lent qui suit le niveau avant les pics, un écrêtage doux pour Agressif).
- **Gain** (ce qui pousse dans le limiteur), **Plafond**, **Anticipation** (0 à 5 ms), **Attaque**
  (l'étage lent), **Relâchement**, **Lien transitoires / Lien relâchement** (la stéréo), **Cible**
  (LUFS, pour les mesures).
- Options : **True peak**, **Suréchantillonnage** de la détection (1x à 8x), **Dither** (24, 20, 18,
  16 bits) et sa **mise en forme du bruit** (basique, optimisée, pondérée), **Filtre DC**, **Gain
  unité** (pour comparer au même niveau).
- Pour viser un loudness (−14 LUFS pour le streaming) : régler la cible, puis monter le **Gain**
  jusqu'à ce que la mesure **I** l'atteigne sur tout le morceau.
- Préréglages : 29 (noms en anglais), par exemple Pop Streaming Master (-14 LUFS), Loud Club Master (-8 LUFS), Film and TV EBU R128 (-23 LUFS), 24-bit Export with Dither, Vinyl Pre-Master.

## Filtre Pro

Quatre filtres modulés, à la manière de Volcano.

- **Affichage** : la courbe de chaque filtre (sa couleur) et leur somme en blanc, telle que le routage
  et le mélange la font ; le spectre derrière. Chaque filtre a sa **poignée** : à gauche/droite la
  coupure, en haut/bas la résonance (molette : résonance) ; **clic droit** : son type, sa pente, le
  couper ; **double-clic** dans le vide : le filtre suivant s'allume là. Sous une poignée, la **portée
  de sa modulation** et un point où elle est maintenant (les courbes bougent avec les LFO).
- **Chaque filtre** : Actif, **Type** (passe-bas, passe-haut, passe-bande, réjecteur, pic, peigne),
  **Pente** (12, 24, 48 dB/oct), **Coupure**, **Résonance**, **Saturation**, **Écart stéréo** (la gauche et
  la droite décalées, en octaves).
- **Routage** : série, parallèle, ou par paires (1 puis 2, à côté de 3 puis 4). **Mélange**,
  **Sortie**.
- **Modulation** : deux **LFO** (sept formes : sinus, triangle, scies, carré, aléatoire tenu,
  aléatoire lissé ; vitesse libre ou **synchronisée** sur le tempo et calée sur la position du
  morceau ; **phase stéréo**), un **suiveur d'enveloppe** (attaque, relâchement, gain) et quatre
  **modulations** (source, cible, quantité). **Glisser** l'étiquette d'un LFO ou du suiveur sur une
  molette (coupure, résonance, saturation, mélange, sortie) ou sur une poignée : une modulation libre
  la prend.
- Préréglages : 29 (noms en anglais), par exemple Wobble Bass Triplet Growl, Funk Guitar Envelope Filter, Talking Bass Formants, Old Radio Tuning Drift, EDM Bar-Long High-Pass Rise.

## Exemple

**19-effets-studio** (Fichier › Modèles) : « Pas de cinq » à travers les quatre effets, chacun depuis
un préréglage — une plaque et le compresseur « Gentle Vocal » sur le saxophone, une grande salle sur le
piano, « Steady Bass » sur la contrebasse, les balais qui tournent dans le Filtre Pro, et le Limiteur
Pro « Streaming -14 LUFS » sur le master (plafond -1 dBTP, mesuré sur l'export).

## Pour les curieux

- Moteur : `src/engine/ProEffects.cpp`; ce que l'interface dessine et ce que le moteur calcule sont
  les mêmes fonctions (`include/cydaw/engine/ProEffectsDesign.h` : réponses des filtres, décroissance
  de la réverbération, styles et courbes). Interface : `src/v2/StudioEffectsView.cpp`.
- Les réglages principaux sont dans les champs à prise (modulables par câble) ; les autres dans les
  emplacements du nœud (comme les bandes de l'Égaliseur Pro), automatisables et exportés dans les
  Graph VST.
- L'**anticipation** (compresseur, limiteur) retarde le son de sa durée, et l'ampli suréchantillonné
  de 16 échantillons : chaque effet le déclare, le graphe fait attendre ses chemins parallèles plus
  rapides (un mélange sec / traité reste en phase), et le mixage fait attendre les autres pistes
  autant que la plus en retard (compensation de latence, comme pour les plugins). L'export saute ce
  retard et celui du master : le fichier commence avec le premier échantillon du morceau.
- Tests : `cydaw_engine_tests` (plafond et true peak du limiteur dans ses huit styles, dither sur la
  grille 16 bits, réduction et plage du compresseur, décroissance mesurée de la réverbération, réponse
  mesurée du filtre contre sa courbe), `cydaw2_effects` (chaque réglage atteint le moteur, survit à
  une sauvegarde, le son reste fini ; le Limiteur Pro tient son plafond).
- Préréglages : `tools/factory_content/build_studio_presets.py` (écrits par CyDAW lui-même, session
  muette).
