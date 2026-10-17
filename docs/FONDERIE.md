# Fonderie

La **Fonderie** est l'éditeur de sons de CyDAW, **multipiste** : plusieurs pistes, plusieurs sons sur
chacune, comme dans un éditeur audio dédié. Elle prend toute la fenêtre (bouton **Fonderie** de la barre
du haut, F11 ou Affichage › Fonderie) ; **Studio** ramène aux pistes du projet, les sessions de la
Fonderie restent ouvertes.

## Périphériques, graphe, mixeur et routage

La zone inférieure reprend les vues du Studio : **Périphériques**, **Graphe**, **Mixeur**, **Routage** et
**Éditeur**. La séparation horizontale se déplace pour donner plus de place au montage ou aux outils.
Le bouton **Masquer** à droite des onglets du panneau du bas, **Ctrl+J** ou **Affichage › Panneau du bas** masque ou réaffiche
la zone inférieure sans interrompre le traitement audio. Une barre **Afficher** reste accessible pour le rouvrir. Son état est mémorisé pour ce mode.
**Traitements** et **Marqueurs** occupent un panneau distinct en bas à droite ; **Détails**,
**Analyse** et **Historique** restent dans le panneau à droite du montage.
Cliquer une piste du montage choisit sa chaîne ; le sélecteur en bas permet aussi de choisir un bus ou
le **MASTER** de cette session. **+ Effet** ouvre le même catalogue de modules que le Studio ; **+ Bus**
crée une tranche de bus. Le mixeur reprend les faders, panoramiques, mute/solo et le master ; le routage
permet les sorties vers des bus, les envois avant/après fader et les entrées supplémentaires des graphes.
Une boucle de routage est refusée.

Les clips alimentent leur chaîne avant le fader, puis leurs sorties/bus, puis le master de la session.
Les effets et les graphes restent réglables pendant la lecture. Ils sont inclus dans l'export du mixage
et dans l'envoi au Studio ; **Traîne export (s)** ajoute le temps choisi pour les queues de réverbération
ou d'écho (0 par défaut). La lecture laisse jusqu'à 3 secondes de traîne en fin de région ; Arrêter
coupe la lecture avec un fondu court. L'onglet Éditeur règle le gain, la position et les fondus du clip
choisi par son titre. Les raccourcis d'édition suivent le montage ou le graphe qui a le focus.

Les pistes, bus, connexions, effets et le master appartiennent à cette session Fonderie ; les pistes du
projet Studio sont conservées. Dès qu'une chaîne est personnalisée, enregistrer conserve un
**`.cyfoundry`**, et exporter crée le fichier audio traité. Le format de session v2 conserve aussi les
identifiants des pistes et la table de traitement ; les sessions v1 restent lisibles.

## Ouvrir, ajouter

- **Ouvrir** (Ctrl+O) : un fichier son devient une session à lui seul (une piste, un clip) ; une session
  `.cyfoundry` s'ouvre telle qu'enregistrée. Chaque session a son onglet (un point quand elle n'est pas
  enregistrée) ; **+** en crée une vide.
- **Ajouter des sons** (bouton à côté d'Ouvrir) : chacun sur une nouvelle piste, au curseur.
- **Glisser des sons** (navigateur du studio, collection du Mix DJ, explorateur) : sur une piste, ils
  deviennent un clip à l'endroit où on les lâche ; sous les pistes, ils en créent une nouvelle.
- Depuis le studio : le clic droit d'un clip audio (« Ouvrir son fichier dans la Fonderie ») ou d'un son
  du navigateur.

Quitter CyDAW avec des sessions non enregistrées demande d'abord.

## Les pistes et les clips

À gauche de chaque piste : son nom (double-clic pour le changer), **M** (muet), **S** (solo), son volume
et son panoramique, × pour la retirer ; un clic dessus la choisit (Ctrl : une de plus). **+ Piste** en
ajoute une.

Un clip se travaille à la souris :

- sa **barre de titre** le déplace dans le temps, ou vers une autre piste ;
- ses **bords** le rognent (le son est gardé, on peut revenir en arrière) ;
- les **petits carrés** de ses coins du haut règlent ses fondus d'entrée et de sortie ;
- son **clic droit** : sélectionner le clip, le scinder au curseur, le dupliquer, son gain, retirer ses
  fondus, le supprimer.

## L'affichage

- En haut, l'**aperçu** de toute la session ; le cadre montre ce qui est affiché (un clic : y aller).
- La **règle** des temps, avec sa bande à saisir (glisser de côté : se déplacer ; vers le bas : zoomer ;
  vers le haut : dézoomer ; double-clic : tout voir) ; un clic dans les temps pose le curseur.
- La **molette** zoome autour du pointeur, jusqu'aux échantillons (des points quand on est tout près) ;
  Maj+molette fait défiler ; Ctrl+molette grossit les ondes ; la bande verticale à droite règle la hauteur
  des pistes et les fait défiler.

## Sélectionner et éditer

- Un **clic** pose le curseur et choisit la piste ; **glisser** sélectionne un moment sur les pistes
  traversées ; Maj+clic étend ; double-clic sur un clip : le clip ; ailleurs : tout.
- Glisser dans le haut d'une piste ne prend que la **gauche**, dans le bas que la **droite** (ou les
  boutons **G**, **D**, **G+D**).

| Édition | Raccourci |
|---|---|
| Couper, copier, coller (au curseur, ou à la place de la sélection) | Ctrl+X, Ctrl+C, Ctrl+V |
| Coller en mixant (par-dessus, sans décaler la suite) | Ctrl+Maj+V |
| Supprimer la sélection (sur un seul canal : la rendre muette) ; sans sélection : le clip choisi | Suppr |
| Rogner : ne garder que la sélection | Ctrl+T |
| Scinder les clips au curseur | S |
| Dupliquer le clip choisi | Ctrl+D |
| Tout sélectionner | Ctrl+A |
| Annuler, rétablir (sans limite ; l'onglet **Historique** les liste) | Ctrl+Z, Ctrl+Y |
| Lire depuis le curseur, ou la sélection ; en boucle avec L | Espace |
| Marqueur au curseur, région de la sélection | M, Maj+M |

Couper, coller ou insérer du silence **à l'intérieur d'un son** change ce son : il reste un seul clip
(un fichier ouvert seul reste un fichier). Sur plusieurs pistes, chacune se referme sur ce qui lui a été
pris. Le presse-papiers garde les pistes copiées et passe d'une session à l'autre.

Le **crayon** (barre d'outils) redessine les échantillons d'un clip quand on est zoomé jusqu'à eux.

Rien n'est recopié en entier : un son est fait de morceaux partagés, et seul ce qu'un traitement réécrit
est nouveau. Annuler ne coûte presque rien, même sur de longs enregistrements.

## Les traitements

Dans l'onglet **Traitements** à droite : on choisit, on règle, **Appliquer**. Ils agissent sur la
sélection des pistes choisies (ou sur ces pistes en entier sans sélection), clip par clip.

- **Volume** (−48 à +24 dB), **Normaliser** (la crête, ou le niveau RMS, mesurés sur toute la sélection).
- **Fondu entrant / sortant** sur toute la sélection, quels que soient les clips qu'elle traverse :
  linéaire, rapide, lent, doux (en S).
- **Inverser la polarité**, **Renverser**, **Retirer le décalage (DC)**, **Rendre silencieux**,
  **Insérer du silence** (au curseur).
- **Étirer et transposer** : la vitesse (25 à 400 %) et la transposition, avec l'algorithme : Bande (la
  hauteur suit), Tonal, Rythmique, Texture. Ce qui suit sur la piste se décale.
- **Fréquence d'échantillonnage** : toute la session convertie (8 à 192 kHz).
- **Canaux** : mono (mélangés), stéréo (doublé), échanger gauche et droite, garder un côté.
- **Effets d'une piste** : les effets d'une piste audio du projet (ou du master), tels que réglés dans son
  panneau Périphériques, rendus dans chaque clip, leur traîne gardée dans le clip. Tous les effets de
  CyDAW servent ainsi à traiter des sons.

## Mesurer

L'onglet **Analyse** mesure le mixage de la sélection (ou de tout) sur les pistes choisies, dès qu'elle
ne bouge plus : crête, RMS et décalage DC de chaque côté, sonie intégrée (LUFS), échantillons à pleine
échelle, et le spectre moyen.

## Enregistrer, exporter

- **Ctrl+S** : un fichier ouvert seul (une piste, un clip entier, sans fondu ni gain) est réécrit tel
  quel ; sinon la **session** est enregistrée (`.cyfoundry`, avec un dossier « … files » à côté pour les
  sons modifiés ; les sons intacts restent là où ils sont). Ctrl+Maj+S : sous un autre nom.
- **Exporter le mixage** : la sélection ou tout, en **WAV** (16, 24 bits ou 32 bits flottants),
  **FLAC** ou **AIFF** (16 ou 24 bits), **Ogg Vorbis**.
- **Envoyer le mixage au studio**, ou **chaque piste**, sur de nouvelles pistes audio du projet.

Un fichier est écrit à côté puis mis à sa place : s'il est utilisé (par un clip du projet), rien n'est
abîmé et un message le dit. Dans un WAV, les **marqueurs et les régions** sont enregistrés avec le son
et retrouvés à l'ouverture. Les nouveaux fichiers vont par défaut dans le dossier « Fonderie »
(Options › Emplacements).

Les fichiers lus : WAV, AIFF, FLAC, Ogg, MP3 et WMA (par Windows), jusqu'à 3 heures.

## Pour les scripts

La commande `foundry` du port de contrôle et du serveur MCP (`{"action": …}` : open, new, import,
add_track, track, clip, select, edit, process, undo, save, save_session, export, play, marker, stats,
send_to_studio…) et `app_mode`. Le test `cydaw2_foundry_check` (silencieux) les parcourt.


## Pistes MIDI, détails et automation

Le bouton **+ Piste** (ou le + de la barre d’outils) propose **Audio** et **MIDI / instrument**.
Un double-clic dans une zone vide d’une piste MIDI crée un clip ; un double-clic sur ce clip ouvre
son piano-roll dans **Éditeur**. La grille et l’octave se règlent au-dessus ; double-clic pour ajouter
une note, glisser pour la déplacer, tirer son bord droit pour sa durée, clic droit pour la supprimer.
La vélocité de la note choisie se règle dans **Détails**. Le tempo sert à la grille MIDI ; la Fonderie
conserve ses positions en secondes.

Déposer un fichier audio sur une piste MIDI charge un échantillonneur simple et crée un clip de note.
**Instrument…** ouvre la bibliothèque pour remplacer cet instrument par un synthé ou un autre instrument.

L’onglet **Détails**, à droite, affiche le clip sélectionné : nom, début, durée, décalage dans la source,
gain et fondus audio. **Inverser le clip (Reverse)** lit son contenu à l’envers ; cette action figure
aussi dans les menus contextuels de la Fonderie, de l’Arrangement et du Lanceur. Les fichiers sources
restent intacts.

Les bus créés avec **+ Bus** apparaissent aussi dans la zone des pistes et disposent de leurs courbes.
Le **master** reste visible au bas de la zone des pistes. Le bouton **A** d’une piste affiche sa courbe.
La liste de la courbe choisit le volume ou un paramètre automatisable du graphe. Double-clic dans la
courbe pour créer un point ; glisser un point pour le déplacer ; clic droit sur le point pour le
supprimer. Plusieurs courbes sont conservées par piste ; la liste choisit celle qui est affichée.
Les points sont interpolés linéairement. Le volume suit les points échantillon par échantillon, les
paramètres des modules au rythme des blocs audio. Notes et courbes sont incluses dans la lecture,
l’export et les sessions `.cyfoundry` (version 3, lecture des anciennes versions conservée).

Le **+** des périphériques et **+ Effet**, dans la Fonderie comme dans Mix DJ, ouvrent la même grande
**Bibliothèque** que le Studio, avec recherche et filtres par catégorie, genre, rôle et fabricant.

Le port de contrôle accepte aussi `midi_clip`, `notes`, `automation`, `reverse_clip`, et
`add_track` avec `type: "midi"`. `notes` utilise des positions et durées en échantillons ; `automation`
prend `track` (−1 pour le master) et `lanes: [{binding, points: [{frame, value}]}]`.
