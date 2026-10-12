# Batterie — la batterie acoustique de CyDAW

Une batterie acoustique jouée élément par élément, comme sur une console de batteur : six kits
enregistrés (des banques libres, CC0), chaque élément avec son niveau, son panoramique, son accord, sa
durée, muet ou solo ; des motifs (rock, funk, jazz…) qui deviennent des clips MIDI sur la piste.

**La créer** : Piste › Nouvelle Batterie (kits acoustiques) — une piste MIDI dont l'instrument est
Batterie, son premier kit chargé, sa fenêtre ouverte. Aussi dans le catalogue des instruments
(« Batterie (kits acoustiques) ») : posée sur une piste, elle charge aussi son premier kit.

## Les kits

| Kit | Banque | Ce qu'il a |
|---|---|---|
| **Rouillée** | Big Rusty Drums | un kit rock complet : grosse caisse, caisse claire, charleston, toms, crash, ride, china |
| **Jazz** | Virtuosity Drums | un kit jazz complet, toutes les touches General MIDI |
| **Tourbillon** | Swirly Drums | un kit pop, ses cymbales et quelques percussions |
| **Indocile (baguettes)** | Unruly Drums | un kit joué aux baguettes |
| **Indocile (balais)** | Unruly Drums | le même joué aux balais |
| **Années 70** | Gogodze PHU 2 | grosse caisse, caisse claire, charleston, toms ; sans cymbales |

Les échantillons sont ceux des banques que CyDAW télécharge (Options › Banques de sons, dans le dossier
choisi) ; un kit dont la banque manque se télécharge comme les autres instruments d'usine. Changer de
kit (la liste en haut de la fenêtre) charge sa carte d'échantillons sur la carte Batterie.

## Les éléments

Chaque note joue un **élément** selon sa touche (celles de General MIDI et les articulations des
kits) : grosse caisse (35, 36), caisse claire (37 à 40, 26), charleston (42, 44, 46 et les variantes
54, 58, 30, 32, 34), toms 1 à 4 (50 et 48, 47, 45, 43 et 41), crash (49, 55, 57), ride (51, 53, 59),
china (52), percussions (56, 60 à 81), autres.

Pour chacun : **niveau** (−40 à +6 dB), **panoramique**, **accord** (±12 demi-tons), **durée** (à fond :
comme enregistré ; en dessous, il s'éteint plus tôt), **muet**, **solo** (un élément en solo fait taire
les autres). Pour le kit : son accord, son **jeu humain** (la vélocité varie un peu à chaque coup), son
niveau. Tout s'automatise et se module comme les autres boutons de la piste (`node.<id>.batterie.snare.tune`).

## La fenêtre

- **KIT** : le kit dessiné (cymbales derrière, fûts devant ; un élément muet est pâle, un élément en solo
  porte un S) ; un clic choisit un élément, ses réglages s'affichent dessous avec les touches qui le
  jouent ; à droite, ceux du kit.
- **CONSOLE** : une tranche par élément et une pour le kit : fader de niveau, panoramique, accord, durée,
  muet et solo.
- **MOTIFS** : douze motifs (Rock, Pop, Funk, Disco, Hip-hop, Shuffle, Bossa nova, Jazz, Metal, Ballade,
  Reggae, House) et deux roulements (toms, caisse claire), dessinés note par note ; on choisit le nombre
  de mesures (1, 2, 4, 8) et le roulement qui finit, puis **Mettre sur la piste** : un clip MIDI à la
  mesure de la tête de lecture, qu'on modifie ensuite dans l'éditeur. Les notes vont sur les touches que
  le kit joue : un tom que le kit n'a pas est remplacé par un de ses toms.
- **MICROS** : les réglages que le kit enregistré a lui-même (ceux de son SFZ), une rangée par élément : le
  niveau de chaque micro (proche, ambiance, dessous), le panoramique, l'accord, l'étouffement, la position du
  charleston… (40 pour Rouillée, une dizaine pour Jazz). Ils s'ajoutent à la console ; un micro monté de zéro
  se charge à ce moment-là. Clic droit : automatiser ; le MIDI de la piste (ses contrôleurs) les bouge aussi.
- **EFFETS** : les effets de la piste après Batterie (compresseur, égaliseur, Pression, Fournaise,
  Tranche, transitoires, Patine, Halo, limiteur).

La carte, dans Périphériques, montre le kit en petit, les niveaux de la grosse caisse, de la caisse
claire, du charleston, des toms, du crash et de la ride, l'accord et le niveau du kit, et **Ouvrir
Batterie**.

**Préréglages d'usine** (13) : Rouillée brute, Rock sec, Metal serré, Toms profonds, Hip-hop poussiéreux,
Jazz feutré, Jazz lumineux, Tourbillon pop, Funk serré, Indocile ouverte, Balais intimes, Années 70,
Lo-fi grave. Un préréglage d'un autre kit charge ce kit. Faits par
`tools/factory_content/build_batterie_presets.py` (chaque son entendu hors ligne par le chemin du
plug-in exporté).

## Le moteur

- `include/cydaw/engine/DrumKit.h`, `src/engine/DrumKit.cpp` : les éléments, la touche → l'élément, les
  réglages (80 nombres, `drum::Values`, `neutralValues()` sans table), ce qu'un élément fait à une note
  (`shapingFor` : gain, panoramique, demi-tons, durée), les kits, les motifs (`grooves()`,
  `grooveHits()`, `keyInKit()`).
- Batterie est un sampler (`core.batterie`, carte `batterie`) : sa carte d'échantillons est celle du kit ;
  le plan porte `drumKit` et `drumValues` ; la voix (`PolyVoice`) applique à chaque coup son élément :
  muet (la note ne part pas), niveau et panoramique, accord, extinction.
- Un plug-in exporté joue Batterie (carte et réglages compris) ; ses réglages se lient par
  `batterie.<clé>`.

## Piloter

- `batterie {track, kit, init, set}` (outil MCP `batterie`) : le kit par son numéro ou son nom, le mixage
  par clé (`kick.level`, `snare.tune`, `hihat.decay`, `tom2.mute`, `ride.solo`, `kit.humanise`…) ; rend
  les valeurs, le kit, le nombre de zones, les kits et les motifs.
- `groove {track, name, bars, fill, startBeats}` (outil MCP `groove`) : un motif en clip MIDI.
- `show_dialog batterie` ouvre sa fenêtre (piste sélectionnée).

## Vérifié

- `cydaw_engine_tests` (`batterieMixesItsPieces`) : la touche → l'élément, le niveau, le panoramique,
  l'accord, la durée, muet et solo sur une voix de sampler.
- `cydaw2_sfz_live_check` : les micros du kit sur sa carte, les overheads de la grosse caisse montés de zéro.
- `cydaw2_batterie_check` (`tests/v2_batterie_check.py`, port 47846 ; passé sans les banques) : le premier
  kit chargé, chaque élément qui sonne, muet, le niveau au décibel près, un autre kit, un motif sur des
  touches du kit et le mixage exporté qui sonne à chaque temps, les préréglages (un autre kit chargé),
  la fenêtre, un projet rouvert.
