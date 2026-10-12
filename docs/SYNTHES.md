# La famille de synthés

Les synthés natifs de CyDAW qui partagent un même cadre : **Ambre** (synthé analogique polyphonique),
**Colosse** (grand synthé moderne à tables d'ondes), **Mosaïque** (synthé hybride) et **Nébuleuse** (synthé
d'atmosphères, granulaire). Chacun a sa carte dans
Périphériques (une image, une rangée de boutons, « Ouvrir ») et sa grande fenêtre (ses panneaux, ses
préréglages, ses effets). Prisme (tables d'ondes) et Kick restent à part.

| Synthé | Où le trouver | Ce qu'il est |
|---|---|---|
| **Ambre** (`ambre-synth`, instrument `ambre-instrument`) | Piste › Nouveau Ambre ; Instruments | trois oscillateurs, quatre filtres de caractère, deux enveloppes, deux LFO |
| **Colosse** (`colosse-synth`, instrument `colosse-instrument`) | Piste › Nouveau Colosse ; Instruments | deux oscillateurs à tables d'ondes déformables, deux filtres de sept sortes, une séquence, six modulations libres |
| **Mosaïque** (`mosaique-synth`, instrument `mosaique-instrument`) | Piste › Nouveau Mosaïque ; Instruments | deux moteurs hybrides (analogique, tables, harmonique, modal), deux filtres, fonction dessinée, aléatoire Turing, quatre macros, huit modulations |
| **Nébuleuse** (`nebuleuse-synth`, instrument `nebuleuse-instrument`) | Piste › Nouveau Nébuleuse ; Instruments | deux couches (synthèse ou grains de sept textures), harmonia, l'orbe, huit modulations ; Halo derrière |

## Le cadre commun

Un modèle, c'est trois choses :

- **sa table de réglages** : une liste ordonnée (clé, noms français et anglais, unité, plage, valeur de
  départ, sorte : continu, entier, interrupteur ou choix, section). Les six premiers réglages sont les
  mêmes pour tous : `voice.mode` (Poly, Mono, Legato), `voice.count`, `voice.glide`,
  `voice.glideAlways`, `voice.bend`, `voice.master` ;
- **son moteur** : ce que joue une voix (`synth::Voice` le choisit d'après le modèle) ;
- **sa mise en page** (`src/v2/SynthLayouts.cpp`) : ses panneaux en rangées, l'image de chacun (onde,
  courbe du filtre, enveloppe, LFO), ses boutons et leurs noms courts ; la carte : son image et sa
  rangée de boutons.

Les tables d'ondes (Colosse, Mosaïque, Nébuleuse) et les textures (Nébuleuse) sont fabriquées à la
première carte qui en a besoin, hors du fil audio (`synth::warmUp`), pas au démarrage.

Tout le reste est commun : la carte garde le modèle et ses réglages (`synthModel`, `synthValues` dans
le projet, par clé, seulement ce qui diffère des valeurs de départ), le plan d'instrument les porte, la
voix joue tout le son (comme Prisme : polyphonie, mono, legato avec glissando), chaque réglage est un
bouton de la piste (`node.<id>.ambre.filter.cutoff`), donc automatisable et modulable ; un plug-in
exporté joue la même chose.

Code : `include/cydaw/engine/SynthFamily.h` (le cadre), `src/engine/SynthFamily.cpp` (le catalogue, la
voix), `src/engine/SynthEngines.h` (ce qu'un modèle fournit), `src/engine/AmbreSynth.cpp` (Ambre),
`src/v2/SynthView.cpp` (la carte et la fenêtre de tous les modèles).

## Ambre

Un synthé analogique à l'ancienne :

- **trois oscillateurs** (dent de scie, impulsion à largeur réglable, triangle, sinus ; sans
  repliement) : le deuxième peut être synchronisé sur le premier, le troisième module la fréquence du
  premier (FM) ; un bruit ; une **dérive** analogique (chaque voix un peu fausse, un peu différente) ;
- **l'unisson** : jusqu'à quatre voix par note, désaccordées et ouvertes en stéréo ;
- **un passe-haut**, puis **un filtre parmi quatre** : *Ladder* (quatre pôles en boucle, résolu sans
  retard, son entrée saturée), *Cascade* (quatre pôles, la résonance retardée et saturée : plus rond,
  chante plus tôt), *Multimode* (deux pôles à variables d'état : passe-bas, passe-bande, passe-haut),
  *Mordant* (le même dont la résonance sature et mord). Saturation avant le filtre, enveloppe (± six
  octaves), suivi du clavier, vélocité ;
- **deux enveloppes** (volume, filtre) qui montent et descendent comme un condensateur ;
- **deux LFO** (sinus, triangle, dent de scie, carré, aléatoire ; libres ou calés sur le tempo) : le
  premier vers la hauteur (vibrato, avec un retard et la molette) et la largeur d'impulsion, le second
  vers le filtre et le volume.

La fenêtre (Ouvrir Ambre, ou le bouton ⤢ de la carte pour une fenêtre séparée) :

- **VOIX**, **OSC 1**, **OSC 2**, **OSC 3** ;
- **MÉLANGE** : l'onde des trois oscillateurs dessinée (avec la synchro et la FM), le bruit, la dérive,
  l'unisson ;
- **FILTRE** : sa courbe calculée d'après le modèle, le mode, la fréquence, la résonance et le passe-haut ;
  glisser sur la courbe règle la fréquence (gauche, droite) et la résonance (haut, bas), la molette la
  résonance, un double-clic les remet ;
- **ENV. VOLUME**, **ENV. FILTRE** : chaque enveloppe dessinée, ses trois points à glisser (attaque ;
  déclin et maintien ; relâchement) ;
- **LFO 1**, **LFO 2** : leur forme et leur vitesse (ou leur valeur de note).

Un bouton grisé ne sert pas dans l'état actuel (la largeur quand l'onde n'est pas une impulsion, le mode
pour le Ladder et la Cascade qui sont des passe-bas, la vitesse d'un LFO calé sur le tempo). Clic droit
sur un bouton : valeur de départ, automation dans l'arrangement. L'onglet **EFFETS** ajoute après Ambre
les effets de CyDAW (chorus, Fournaise, Patine, Écho Bande, Halo…).

**Préréglages d'usine** (22) : Init, Basse Moog, Basse acide, Basse ronde, Basse pulsée, Sub sinus,
Lead mono, Lead synchro, Lead mordant, Nappe chaude, Nappe cordes, Nappe balayée, Cuivres, Pincé, Arpège
résonant, Clavier poly, Orgue analogique, Cloche FM, Vent, Unisson large, Wobble, Dérive vintage. Ils
sont faits par `tools/factory_content/build_synth_presets.py` : chaque son réglé par le port de
contrôle, mesuré hors ligne, son volume ajusté pour qu'un do 4 tenu ait la même sonie.

## Colosse

Un grand synthé moderne :

- **deux oscillateurs à tables d'ondes**. Huit familles de tables (Classique : sinus → triangle →
  dent de scie → carré → impulsion ; Harmonique : de plus en plus d'harmoniques ; Voix : des voyelles ;
  Métal : des grappes d'harmoniques hautes ; Plié : un sinus de plus en plus replié ; Numérique : une
  rampe en paliers ; Dents : une dent de scie creusée en peigne ; Synchro : une synchro qui monte),
  chacune huit images que la **position** parcourt en fondu. Chaque table est faite une fois, sans
  repliement, à onze niveaux d'harmoniques : la voix lit celui que sa hauteur permet. Huit **modes**
  tordent la lecture (Direct, Courbe, Miroir, Formant, Synchro, Paliers, Asymétrie, Pli), réglés par
  la **déformation** ;
- **un modulateur** (sinus à 1/2, 1, 2, 3, 4, 5, 7 ou 8 fois la note) qui module la phase de chaque
  oscillateur, un **sub** (sinus une octave sous la note), un **bruit** (blanc, rose, crépitement,
  souffle), **l'unisson** jusqu'à huit voix ouvertes en stéréo ;
- **deux filtres**, chacun de sept sortes : Ladder, passe-bas, passe-bande, passe-haut, **Peigne**
  (accordé sur sa fréquence ; avec le suivi du clavier à fond, il joue la note), **Voyelle** (trois
  formants : la fréquence va de A à U), **Hurlement** (une bande poussée dans sa propre résonance). En
  **série** ou en **parallèle**, le **mélange** dosant le second ;
- **une saturation** après les filtres : douce, dure, pliée, réductrice (moins de bits, moins
  d'échantillons), redresseuse ;
- **trois enveloppes** (volume, 2 et 3), **deux LFO**, une **séquence** de seize pas (dessinée à la
  souris, calée sur le tempo ou libre, lissée ou non) ;
- **six modulations** : chacune une source (enveloppe 2 ou 3, LFO 1 ou 2, séquence, vélocité, molette,
  clavier, aléatoire par note, pression) vers une destination (hauteur, position ou déformation d'un
  oscillateur, mélange des oscillateurs, modulation de phase, fréquence d'un filtre ou des deux,
  résonance, mélange des filtres, saturation, bruit, volume, panoramique, désaccord), avec sa quantité
  (positive ou négative). Par défaut, l'enveloppe 2 ouvre les filtres.

La fenêtre a deux pages : **SYNTHÉ** (les oscillateurs dessinés tels que le moteur les joue, leur
table et leur position ; les sources ; les deux filtres et leur courbe, deux poignées 1 et 2 à
glisser ; la saturation ; les trois enveloppes) et **MODULATION** (la séquence à dessiner, les LFO, les
six modulations).

**Préréglages d'usine** (22) : Init, Basse colosse, Basse réducteur, Grondement, Wobble numérique,
Lead tranchant, Lead métal, Supersaw colosse, Nappe mouvante, Nappe voyelles, Pincé harmonique, Cloche
de phase, Séquence filtrée, Arpège à pas, Corde peigne, Hurlement, Sub profond, Cuivres modernes,
Texture numérique, Chœur formant, Redressé, Basse pliée.

## Mosaïque

Un synthé hybride : **deux moteurs** (A et B), chacun d'une des quatre sortes, la fenêtre ne montrant
que les réglages de la sorte choisie :

- **Analogique** : une forme qui passe du sinus au triangle, à la dent de scie, au carré (sans
  repliement), la largeur d'impulsion, une synchro dure (0 à 24 demi-tons) ;
- **Tables** : les tables de la famille (les mêmes que Colosse), leur position, leur mode de
  déformation ;
- **Harmonique** : jusqu'à 32 partiels joués un à un (additif) : leur nombre, leur **brillance** (la
  pente du spectre), **pairs et impairs** (que les impairs, comme une clarinette, ou que les pairs),
  leur **étirement** (les partiels écartés comme ceux d'un piano, jusqu'à la cloche) ;
- **Modal** : un objet qui résonne, huit modes accordés sur la note : sa **matière** (bois d'un
  marimba, métal, verre, corde, peau), son **excitateur** (un maillet, un souffle de bruit, un archet
  tenu tant que la touche l'est), sa **résonance** (combien de temps il sonne), son **timbre** (les
  modes aigus plus ou moins présents, le maillet plus ou moins dur), son **point de frappe**.

Chaque moteur a son octave, ses demi-tons, son réglage fin, son niveau, son panoramique et son
**envoi** : ce qu'il donne au filtre 2 plutôt qu'au filtre 1. Un **bruit** (blanc, rose, crépitement,
souffle) avec son envoi, **l'unisson** jusqu'à quatre voix. **Deux filtres** des sept sortes de la
famille, **en parallèle** (chaque moteur dans le sien, comme il l'envoie) ou **en série** ; une
**saturation**.

Les modulations : trois **enveloppes**, deux **LFO**, une **fonction** dessinée (jusqu'à seize points,
en boucle ou une fois, calée sur le tempo ou libre, lissée d'un point à l'autre : la courbe suivie est
tracée sur les points), une suite **Turing** (un registre à décalage qui garde sa mélodie et la change
un peu, plus ou moins souvent), **quatre macros**, et **huit modulations** (la source, la destination,
la quantité). Le « timbre 1 » et le « timbre 2 » d'un moteur dépendent de sa sorte : forme et largeur en
analogique, position et déformation en tables, brillance et pairs/impairs en harmonique, timbre et
point de frappe en modal. Prêt à jouer : l'enveloppe 2 ouvre les filtres, la macro 1 change le timbre
des deux moteurs, la macro 2 ouvre les filtres, la macro 3 élargit l'unisson, la macro 4 sature.

La fenêtre a deux pages : **MOTEURS** (les deux moteurs, dessinés tels qu'ils jouent ; les voix, les
sources, les filtres et leur courbe selon les envois ; le volume ; les macros) et **MODULATION** (les
enveloppes 2 et 3, les LFO, la fonction, l'aléatoire, les huit modulations).

**Préréglages d'usine** (22) : Init, Marimba, Vibraphone, Cloches de verre, Corde frappée, Tambour de
peau, Archet de verre, Violoncelle d'archet, Orgue harmonique, Nappe additive, Spectre étiré,
Analogique gras, Synchro hurlante, Tables mouvantes, Turing arpège, Aléatoire mélodique, Fonction
pulsée, Fonction filtrée, Pincé hybride, Basse hybride, Voix du vent, Macros en morphose.

## Nébuleuse

Un synthé d'atmosphères, en **deux couches** (A et B). Chacune est :

- **Synthèse** : une table d'ondes de la famille et sa position ;
- **Granulaire** : des grains pris dans une des **sept textures** que Nébuleuse fabrique elle-même
  (un **chœur** dont la voyelle passe de A à O, du **verre** qui scintille, du **vent**, des
  **cordes**, des **cloches**, de la **pluie**, un **drone** en modulation de fréquence), chacune de
  trois secondes. La **lecture** dit où les grains sont pris, le **défilement** la fait avancer seule (ou
  reculer), la **taille** et la **densité** des grains, leur **dispersion** autour de la lecture, leur
  **écart de hauteur**, la part joués **à l'envers**. Les textures accordées (chœur, cordes, cloches,
  verre, drone) jouent la note ; elles sont fabriquées une fois, à la première carte Nébuleuse, hors du
  fil audio.

Chaque couche a son **harmonia** (un accord joué autour de la note : octave, quinte, octave et
quinte, majeur, mineur, sus 4, septième, neuvième, grave, large ; son niveau), son **timbre** (plus
sombre ou plus brillant), son **filtre** passe-bas et sa résonance, sa **dérive** (la hauteur qui
flotte un peu), son octave, son niveau, son panoramique. Après les couches, un **filtre** des sept
sortes de la famille.

**L'orbe** : un point dans un cercle (X et Y), qu'on glisse à la souris ; son **mouvement** le fait
tourner tout seul autour de sa place, à sa **vitesse** (sur l'horloge du morceau : toutes les voix
ensemble). X et Y sont des sources de modulation. Prêt à jouer, l'orbe fait défiler la couche A,
change le timbre de la couche B, ouvre le filtre et l'harmonia ; le LFO 1 fait respirer les grains.
Les autres sources : l'enveloppe 2, deux LFO, la vélocité, la molette, le clavier, l'aléatoire, la
pression, une **dérive** lente ; **huit modulations** vers la hauteur, le niveau, la lecture, la
position, la taille et la densité des grains, le timbre de chaque couche, le filtre, la résonance, le
volume, le panoramique, l'harmonia.

L'instrument d'usine est livré avec **Halo** derrière (mélange 28 %, 4,5 s, grand espace) : une carte de
plus dans Périphériques, qu'on règle ou qu'on retire.

La fenêtre a deux pages : **COUCHES** (les deux couches : une table dessinée, ou la texture entière avec
la lecture et la longueur d'un grain ; les réglages de la source choisie seulement ; les voix ; le
filtre et sa courbe ; l'orbe ; le volume) et **MODULATION** (l'enveloppe 2, les LFO, les huit
modulations). La carte montre l'orbe.

**Préréglages d'usine** (22) : Init, Chœur céleste, Nappe de verre, Vent du nord, Cordes lointaines,
Carillon, Pluie d'été, Drone sombre, Drone lumineux, Grains inversés, Nuage d'accords, Basse nébuleuse,
Pad orbital, Texture balayée, Souffle vocal, Cristaux, Mer de cloches, Lead aérien, Ombre, Aube,
Granulaire pur, Fantôme.

## Piloter

La commande `synth` (outil MCP `synth`) : `{"cmd": "synth", "track": "Ambre", "set": {"filter.cutoff":
800, "filter.model": "Cascade"}, "notes": [48, 60, 64], "seconds": 1}` règle les réglages par clé (un choix
par son nom) et rend toutes les valeurs et le son mesuré en mémoire, sans le jouer : `peakDb`, `rmsDb`
(note tenue), `sideDb` (largeur stéréo), `endDb` (après le relâchement). `init: true` remet tout par
défaut ; `model` choisit le synthé quand la piste en a plusieurs. `show_dialog` avec `ambre` ouvre sa
fenêtre (piste sélectionnée).

## Tests

- `cydaw_engine_tests` (`ambrePlaysInTune`) : l'ordre de la table, le la 4 à 440 Hz, la fin après le
  relâchement, les quatre filtres bornés à pleine résonance, l'unisson en stéréo, le glissando legato ;
  (`colosseSoundsEveryWay`) : le la 4, la fin, chaque table lue de chaque façon en grave et en aigu,
  chaque filtre et chaque saturation bornés, l'unisson, la séquence qui découpe le volume en double
  croches, les oscillateurs dessinés ; (`mosaiqueEnginesPlayInTune`) : chaque sorte de moteur juste au la 4,
  chaque moteur et chaque filtre bornés en grave et en aigu, la fin, un mode frappé qui sonne puis meurt,
  la fonction qui découpe le volume, une macro qui ouvre le filtre, les moteurs dessinés ;
  (`nebuleuseLayersSound`) : une couche de synthèse juste au la 4, son harmonia qui ajoute la quinte,
  chaque texture en grains bornée en grave et en aigu, les textures accordées qui jouent la note, la
  fin, l'orbe qui ouvre le filtre, les couches dessinées ;
- `cydaw2_synths_check` (`tests/v2_synths_check.py`, port 47845) : les réglages par clé, le son mesuré,
  les boutons de la piste, les préréglages, le mixage exporté à 440 Hz, le plug-in exporté, un projet
  rouvert.
