# Édition audio, prises, étirement

Un clip audio se travaille dans le panneau **Éditeur** (double-clic sur le clip, ou son clic droit). Son
éditeur a quatre faces : **Échantillon**, **Prises**, **Étirement** et **Événements**. En haut, toujours :
son début, sa durée, son décalage dans la source et ses fondus, et ▶ pour l'écouter comme la piste le
joue (étiré, transposé, avec ses prises).

## Échantillon : couper, fondus, normaliser… avec l'historique

La source du clip s'affiche en grand, chaque canal à part. La partie que le clip joue est encadrée.

- **Sélectionner** : glisser sur l'onde. Maj+glisser étend la sélection. Un double-clic sélectionne ce
  que le clip joue. Un simple clic pose le curseur (là où **Coller** insère).
- **Zoomer** : Ctrl+molette autour du pointeur, la molette fait défiler ; la bande au-dessus se saisit
  (déplacer, zoomer ; double-clic : tout voir).

Les éditions :

| Bouton | Effet | Raccourci |
|---|---|---|
| Couper | retire la sélection du fichier (et la copie) | Ctrl+X |
| Copier | copie la sélection | Ctrl+C |
| Coller | insère le passage copié au curseur | Ctrl+V |
| Silence | rend la sélection muette | Suppr |
| Rogner | ne garde que la sélection | |
| Insérer un silence | insère un silence aussi long que la sélection | |
| Fondu entrant / sortant | sur la sélection, avec sa forme : linéaire, puissance égale, lent (exponentiel), rapide | |
| Normaliser | le pic au niveau choisi (−1 dB par défaut) | |
| Gain | monte ou baisse de ce qui est choisi | |
| Inverser | à l'envers | |
| Phase | inverse la polarité | |
| Continu | retire la composante continue | |
| G ↔ D | échange les canaux | |

Sans sélection, Normaliser, Gain, Inverser, Phase, Continu et G ↔ D s'appliquent à ce que le clip joue.
**Écouter** joue la sélection (ou le clip) telle qu'elle est dans le fichier.

Une édition **n'abîme jamais l'original** : elle écrit un nouveau fichier (24 bits) dans le dossier
`edits` à côté du projet (`Documents/CyDAW/Edits` tant qu'il n'est pas enregistré), et le clip le joue.
Couper, coller, insérer gardent le clip calé : il continue de jouer la même musique autour de l'édition,
plus court ou plus long. Aux raccords, un fondu de 1,5 ms évite les clics.

**L'historique** est à droite : *0. Original*, puis chaque édition. Un clic rejoue une version ; une
édition faite après remplace les suivantes. Ctrl+Z annule une édition (ou un retour à une version). Le
projet garde l'historique.

Couper, rogner, insérer et coller ne s'appliquent pas à un clip qui a des repères d'étirement ou qui joue
plusieurs prises (choisissez d'abord une prise) ; un clip fait d'événements s'édite événement par
événement.

## Prises : enregistrer plusieurs fois, garder les meilleurs passages

Un clip peut avoir plusieurs prises : elles se superposent sur son temps, et le clip joue, passage par
passage, celle qu'on a choisie (le *comping*).

- **Enregistrer par-dessus un clip** ajoute une prise à ce clip (case « Enregistrer sur un clip ajoute
  une prise », cochée par défaut), calée là où elle a été jouée.
- **En boucle**, chaque tour devient une prise d'un même clip sur la boucle ; la dernière joue.
- **+ Prise depuis un fichier…** en ajoute une, calée sur le début du clip.

La face **Prises** les empile sous la ligne « CE QUI JOUE », chacune de sa couleur :

- **glisser sur une prise** : ce passage-là vient d'elle (les autres prises le cèdent) ;
- **un clic sur son nom** : elle joue en entier ;
- son **clic droit** : la retirer.

Les passages se rejoignent par un fondu enchaîné de 10 ms. Dans l'arrangement, le clip montre ses
passages et « 2 prises* » (l'astérisque : un comping). Son clic droit › **Prise jouée** en choisit une.

**Mettre à plat** réunit les passages choisis dans un seul fichier, tels qu'ils jouent (chacun de sa
prise, les mêmes fondus enchaînés de 10 ms, le silence là où aucun passage ne joue), sur toute la
longueur de la source : le clip peut encore être allongé ou raccourci ensuite. Le fichier est écrit dans
`edits` comme les autres éditions ; le clip le joue seul, comme une prise unique, ce qui permet ensuite
les éditions d'Échantillon (couper, coller…). L'étirement et la transposition du clip restent. La mise à
plat entre dans l'historique, et Ctrl+Z rend les prises et le comping.

## Étirement : quatre algorithmes, transposition, repères

| Algorithme | Pour | Comment |
|---|---|---|
| **Vitesse** | effets de bande | lu plus vite ou plus lentement : la hauteur suit |
| **Tonal** | voix, accords, nappes | vocodeur de phase, phases verrouillées sur leur pic, canaux qui gardent leur image stéréo |
| **Rythme** | batteries, boucles | coupé aux attaques, chaque tranche jouée telle quelle à sa nouvelle place ; un son tenu est raccordé là où son onde continue (sans saut de hauteur) |
| **Texture** | étirer 10× et plus | fenêtres très longues, phases tirées au hasard : un son étalé en nappe, à niveau constant |

- **Transposition** (demi-tons et centièmes, ±24) sans changer la durée, pour Tonal, Rythme et Texture.
- **Tempo du son** : à régler, ou **Détecter**. **Caler sur le tempo du projet** étire le clip pour qu'il
  joue au tempo du projet. **Durée** en % de l'original.
- **Repères** : double-clic sur l'onde pour en poser un, sa poignée se glisse pour caler ce moment
  ailleurs, clic droit pour le retirer. Les temps du projet sont tracés dessous.

Non étiré, non transposé et à la fréquence de la carte son, un fichier est lu tel quel, échantillon pour
échantillon. Sinon, la conversion passe par un sinc fenêtré (16 passages à zéro).

## Événements : plusieurs sons dans un clip

Un clip peut contenir plusieurs sons, chacun à sa place sur le temps du clip (un *événement*) : une
boucle et ses variations, une voix et ses doublures, des bruitages. La face **Événements** les montre sur
le temps du clip, chacun avec son onde ; deux qui se chevauchent prennent deux lignes.

- **+ Son…** en ajoute un au curseur, ou **glissez un son du navigateur** sur la vue : il arrive là où on
  le lâche. Un clip d'un seul son en devient un d'événements (son propre son en est le premier).
- **Cliquer** un événement le choisit ; **le glisser** le déplace ; **ses bords** le raccourcissent ou
  l'allongent, le son restant en place. Le glisser est aimanté aux bords des autres et au curseur (Maj :
  libre). Un clic ailleurs pose le curseur.
- **Couper au curseur** (S), **Dupliquer** (Ctrl+D, la copie juste après), **Supprimer** (Suppr).
- L'événement choisi : son **gain** (−60 à +24 dB), ses **fondus**, son **algorithme** d'étirement, sa
  **transposition** (sa durée gardée ; pas en Vitesse), et **Normaliser**, **Inverser**, **Phase** (un
  nouveau fichier, sa longueur gardée), **Écouter**.

Chaque geste fait une étape d'annulation. Les faces Échantillon, Prises et Étirement renvoient aux
événements pour un clip qui en est fait. Dans l'arrangement, un tel clip montre les ondes de ses
événements, un trait où chacun commence et « 3 sons ».

## Dans l'arrangement

- Les poignées en haut du clip (à la fin du fondu entrant, au début du fondu sortant) se glissent.
- Les badges : nombre de prises, transposition, durée en % quand il est étiré (le nombre de sons pour
  un clip d'événements).
- L'onde d'un clip étiré suit chacun de ses repères (de petits triangles les marquent), comme la lecture.
  Les événements d'un clip suivent les leurs.
- Clic droit : Éditer le son, Prises et comping, Étirement et transposition, Prise jouée, Fondu enchaîné
  avec le voisin, Caler sur le tempo du projet, Normaliser, Inverser.

## Port de contrôle et MCP

- `edit_audio {track, clipId, operation, start, length, level, curve}` : `fade-in`, `fade-out`,
  `silence`, `reverse`, `gain`, `normalize`, `dc`, `polarity`, `swap`, `cut`, `crop`, `insert-silence`,
  `paste` ; `copy_audio {track, clipId, start, length}`.
- `audio_clip_history {track, clipId, version?}`.
- `audio_takes {track, clipId, action: list | add | activate | remove | comp | flatten, file, shift,
  take, comp}`.
- `audio_events {track, clipId, action: list | add | move | trim | split | remove | duplicate | set |
  edit, eventId, file, start, end, at, gainDb, fadeIn, fadeOut, pitch, stretchMode, operation, level}` : les
  événements d'un clip, en secondes depuis son début.
- `set_audio_clip` prend `pitch`, `markers` (`[{source, timeline}]`, des repères d'étirement) et
  `clearMarkers` ; `list_audio_clips` donne `pitch`, `takes`, `activeTake`, `compPassages`, `versions`,
  `version`, `stretchMode`, `source`.
- `set_view {mode: 2, track, clipId, face: sample | takes | stretch | events}` montre un clip dans son
  éditeur.

## Dans le code

- `include/cydaw/engine/TimeStretch.h`, `src/engine/TimeStretch.cpp` : l'étirement (vocodeur de phase,
  tranches aux attaques, texture, sinc), sans JUCE.
- `src/app/AudioTrackController.*` : `SegmentReaderSource` lit par `TimeStretcher` ; `AudioClip` a
  `pitchSemitones`, `comp`, `versions`/`version`, `AudioTake::shiftSeconds` ; `rebuildArrangementPlan`
  joue les passages de chaque prise ; `renderAudioSplice`, `commitAudioClipSourceEdit`,
  `setAudioClipVersion`, `setAudioClipComp`, `addAudioTakes`, `setAudioClipPitch` ; la mise à plat :
  `renderAudioClipComp`, `commitAudioClipCompFlatten` ; le temps d'un clip dans sa source :
  `clipSourceTime`, `eventSourceTime` (ce que l'arrangement et l'éditeur dessinent).
- `src/app/AudioEdit.h` : les opérations et les formes de fondu.
- `src/v2/AudioEditorView.*` : l'éditeur ; `src/v2/Studio.cpp` : `editAudioClip`, `copyAudio`,
  `keepRecordedAudio` (prises à l'enregistrement), `flattenAudioClipComp`, `editAudioEvent`.

## Vérifié

- `cydaw_engine_tests` : un son étiré ×0,5 et ×1,5 garde sa hauteur (440 Hz à 1,5 Hz près) et son
  niveau, en Tonal et en Rythme ; transposé d'une octave, 880 Hz ; Vitesse suit la vitesse ; un fichier à
  44,1 kHz lu à 48 kHz garde sa hauteur ; non étiré, copié à l'identique ; un saut rejoue aussitôt ;
  Rythme garde chaque attaque entière à sa place, mieux que Tonal ; Texture ×10 garde le niveau.
- `cydaw2_editing_check` : normaliser (−1,00 dBFS), fondu lent, inverser, couper (le clip et le fichier
  une seconde plus courts), coller, insérer un silence, rogner, l'historique, revenir à l'original,
  Ctrl+Z ; deux prises, un comping, la transposition et l'historique gardés par le projet ; un clip
  étiré, transposé et en comping sonne à la lecture ; la mise à plat (440 Hz puis 660 Hz, aucun saut
  au raccord, le silence après le dernier passage, l'historique, Ctrl+Z) ; les événements (ajout, bords,
  déplacement, découpe, gain et fondu, suppression, normalisation, duplication, transposition), puis
  exportés : chacun sonne là où il a été mis, silence entre eux, la copie une octave plus haut.

## Limites

- L'enregistrement de prises n'est pas testé automatiquement (il faut une entrée audio).
- Un clip fait d'événements ne se met pas à plat en un fichier.
- La face Événements ne pose pas de repères d'étirement dans un événement (ceux qu'il a, venus d'un
  projet, jouent et se dessinent).
