# Traitements

Le mode **Traitements** sert à fabriquer ses propres traitements, audio et MIDI, à les essayer en
écoutant **avant / après**, puis à les appliquer aux clips du projet. Il prend toute la fenêtre (bouton
**Traitements** de la barre du haut, ou Affichage › Traitements) ; **Studio** ramène aux pistes.

À gauche, la bibliothèque : **Audio** ou **MIDI**, les traitements **De CyDAW** puis **Les miens**.
**Nouveau**, **Dupliquer** et **Supprimer** (les miens seulement, envoyés à la corbeille). Un traitement
d'usine modifié s'enregistre dans les miens (**Enregistrer dans les miens**) ; **Enregistrer comme
nouveau** en fait toujours un autre.

## Traitements audio

Un traitement audio est une suite d'**étapes**, appliquées dans l'ordre ; chacune s'allume, s'éteint,
monte, descend ou s'enlève dans la liste. **+ Ajouter une étape** propose :

| Étape | Ce qu'elle fait |
|---|---|
| **Chaîne d'effets** | Les effets de CyDAW sur une piste à elle, comme une piste du studio : Périphériques, Graphe, Mixeur, Routage, Éditeur. Les potards cochés dans « Réglages montrés quand on applique le traitement » deviennent ses réglages. |
| **Code (JSFX)** | Un effet écrit en JSFX, le langage d'effets de REAPER (`@init`, `@slider`, `@block`, `@sample` avec `spl0`, `spl1`). Le code est vérifié pendant la frappe (l'erreur donne sa ligne) ; ses `slider1..` deviennent les réglages du traitement. |
| **Débruitage** | Le bruit de fond (souffle, ventilation) mesuré dans les moments les plus calmes du son, puis retiré partout : réduction, sensibilité, lissage. |
| **Anti-ronflement** | Le ronflement du secteur (50 ou 60 Hz, trouvé tout seul) et ses harmoniques retirés par des filtres très étroits. |
| **Anti-clics** | Les clics et craquements redessinés à partir du son qui les entoure. |
| **Passe-haut, Passe-bas** | Coupure et pente. |
| **Gain, Normaliser** | Plus fort ou moins fort ; la crête amenée au niveau choisi. |
| **Couper les silences** | Le silence du début et de la fin enlevé (seuil, marge gardée). |

### La fin du son

Une réverbe ou un écho rendent le son plus long que l'original. **Laisser sonner la fin** attend la fin
du son traité : elle est finie quand elle reste sous le **seuil de silence** (sous la crête du son)
pendant la **tenue du silence**, puis un **fondu final** la referme. Le son peut s'allonger jusqu'à la
**durée max de la fin**.

Protection contre un bruit continu : une fin qui ne retombe plus (un souffle, un bourdonnement que le
traitement fabrique sans fin) est coupée peu après le moment où elle a cessé de décroître, au lieu
d'aller jusqu'au maximum. L'essai l'indique (« fin coupée : elle ne retombait pas »). Une fin qui
décroît encore au maximum est coupée au maximum. Sans cette option, le son garde exactement sa durée.

### L'essai

Sous l'éditeur, l'**Essai** traite un son pendant qu'on règle : un fichier déposé sur le mode,
**Choisir un son…**, ou **Prendre le clip sélectionné** (un clip audio de l'arrangement). Chaque
changement relance le traitement (dans un fil à part, l'interface reste fluide). Deux lignes de temps à
la même échelle montrent le son **Avant** et **Après** ; un trait marque la fin de l'original sur le son
traité. Cliquer une ligne l'écoute depuis là. **Avant**, **Après**, **Comparer avant / après** (passe
de l'un à l'autre au même endroit pendant la lecture), **Arrêter**. **Enregistrer le résultat…** écrit
le son traité en WAV 24 bits.

## Traitements MIDI

Un traitement MIDI est un **script de notes** en EEL2 (le langage des JSFX), vérifié pendant la frappe.

- Sections : `@init` (une fois), `@note` (chaque note, dans l'ordre de leur début), `@end` (une fois à
  la fin).
- Réglages : des lignes `paramN:défaut<min,max,pas>Nom` en tête (`param1` à `param16`).
- Par note : `pitch` (0..127), `start` et `length` (en temps depuis le début du clip), `velocity` et
  `chance` (0..1), `index` ; `keep = 0` retire la note.
- Autour : `count`, `tempo`, `clip_length`, `prev_start`, `next_start`, `prev_pitch`, `next_pitch`.
- Fonctions : `random()` (0..1), `gauss()` (moyenne 0, écart 1), `ms_to_beats(ms)`, `note_start(i)`,
  `note_pitch(i)`, `note_length(i)`, `note_velocity(i)`, `add_note(pitch, start, length, velocity)`.

Exemple (un humaniseur) :

```
param1:12<0,60,1>Timing (ms)
param2:10<0,50,1>Velocity (%)

@note
start += gauss() * ms_to_beats(param1) / 2;
velocity *= 1 + gauss() * param2 / 200;
```

L'essai se fait sur un clip MIDI du projet (le clip sélectionné, sinon le premier), joué par sa piste :
les notes **Avant** et **Après** sur deux bandes, les mêmes boutons d'écoute (les notes traitées
remplacent celles du clip pendant l'écoute de « après », puis reviennent). **Autre tirage** refait les
choix au hasard du script. **Appliquer au clip** garde le résultat (Ctrl+Z l'annule).

## Appliquer à un clip

- Clic droit d'un clip de l'arrangement : **Appliquer un traitement…** (audio) ou **Appliquer un
  traitement MIDI…**.
- Clic droit d'un clip MIDI du lanceur : **Appliquer un traitement MIDI…**.
- Éditeur MIDI : bouton **Traitement…** ; éditeur audio : **Appliquer un traitement (Traitements)…**.

La fenêtre qui s'ouvre propose le traitement (liste des traitements de ce genre), ses réglages, la fin
du son quand une étape peut sonner au-delà (chaîne, code), et les deux lignes de temps avant / après à
écouter. **Modifier dans Traitements…** ouvre le traitement dans le mode. **Appliquer** :

- audio : le son traité devient une nouvelle **version** du clip (son historique dans l'éditeur audio,
  l'original reste) ; le clip s'allonge de la fin qui sonne, ou se raccourcit quand des silences sont
  coupés ;
- MIDI : les notes du clip remplacées (leurs expressions gardées).

Les deux s'annulent en un seul pas. Les clips audio du lanceur ne se traitent pas encore (passer par
l'arrangement) ; un son de plus de 30 minutes est refusé.

## Traitements d'usine

| Audio | |
|---|---|
| Remove Noise | Le débruitage seul. |
| Remove Hum | L'anti-ronflement seul. |
| Remove Clicks | L'anti-clics seul. |
| Voice Clean-up | Passe-haut 80 Hz, anti-ronflement, anti-clics, débruitage, normalisation à -1 dB. |
| Vinyl Restoration | Anti-clics, anti-ronflement, débruitage doux, passe-bas à 15 kHz. |
| Normalize | La crête à -1 dBFS. |
| Trim Silence | Les silences du début et de la fin coupés. |
| Tape Echo | Un écho ping-pong en JSFX, ses répétitions laissées sonner. |
| Small Room | Une petite pièce en JSFX (d'après Freeverb), sa queue laissée sonner. |
| Room Reverb | La réverbe de studio de CyDAW dans une chaîne d'effets, ses temps de déclin (global, graves, médiums, aigus) et son mélange montrés. |

| MIDI | |
|---|---|
| Humanize | Petits décalages au hasard du placement, de la vélocité et de la longueur. |
| Swing | Les contretemps de la grille (1/8 ou 1/16) retardés. |
| Velocity Variation | Les vélocités variées, les temps un peu accentués. |
| Strum | Les notes d'un accord jouées l'une après l'autre (vers le haut ou le bas). |
| Legato | Chaque note tenue jusqu'à la suivante. |
| Staccato | Chaque note raccourcie. |
| Random Octaves | Des notes déplacées d'une octave, au hasard. |
| Note Echo | Chaque note répétée après elle, de plus en plus douce. |

## Fichiers

Un traitement est un fichier **`.cyprocess`** (JSON, format `cydaw-processing`) : son genre, ses étapes
et leurs réglages (la chaîne d'effets enregistrée en entier, le code JSFX en texte), sa fin du son, ou
son script de notes. Les miens sont dans l'emplacement **Traitements** (Options › Emplacements) ; ceux
de CyDAW dans `Factory/Processings`.

## Port de contrôle et MCP

La commande `processing` (outil MCP du même nom) :

- sans argument : la liste (`kind` : `audio` ou `midi`), chacun avec ses réglages (`id`, `label`,
  `min`, `max`, `value`) ;
- `render` (nom), `file`, `out`, `values` (`{id: valeur}`), `tail` (`allow`, `thresholdDb`,
  `maxSeconds`, `holdSeconds`) : un fichier son traité dans un autre ; la réponse donne
  `framesBefore`, `framesAfter`, `tailSeconds`, `endless` et les remarques (`notes`) ;
- `apply` (nom), `track`, `clip` (son id), `values`, `tail`, `seed` : sur un clip, en un pas
  d'annulation (`undo` avec `scope` `arrangement` pour l'audio, `midi` pour les notes) ;
- `open` (`track`, `clip`) : la fenêtre d'application ;
- `check` (le code), `language` (`jsfx` ou `notes`) : vérifié, avec ses réglages ;
- `save` (un traitement en JSON), `delete` (nom, les miens seulement), `writeFactory` (dossier) ;
- `show`, `select` (nom), `testFile` (un son), `state` : la fenêtre du mode.

Test : `ctest -C Release -R cydaw2_processing_check` (silencieux, session cachée).
