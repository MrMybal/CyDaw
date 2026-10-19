# Cinéma : son à l’image

Le bouton **Cinéma** ouvre un espace indépendant du Studio, de Mix DJ et de la Fonderie. Le transport commun en haut pilote alors la session Cinéma active. Le mode peut fonctionner avec une vidéo de référence ou avec un logiciel externe synchronisé, sans vidéo locale.

## Bus et sous-pistes

Les bus sont les lignes de la vue **Arrangement** de Cinéma. Une seule règle temporelle, un zoom et un curseur sont communs à toutes les lignes. Le menu **+ → Bus** ajoute une ligne avec ses propres effets et son routage vers le master.

Une sous-piste est un **conteneur placé dans le temps**, comme un clip sur la ligne de son bus. Elle accueille des clips audio ou MIDI dans son cadre. Plusieurs conteneurs peuvent se suivre horizontalement sur un même bus ; ceux qui se chevauchent se répartissent sur plusieurs rangées à l’intérieur du bus. Ils partagent tous l’échelle temporelle du projet.

Double-cliquer dans une zone vide de la timeline d’un bus crée un conteneur audio à cet endroit. Le clic droit propose aussi un conteneur MIDI. Les choix **Sous-piste audio / Sous-piste instrument MIDI** du menu **+**, et le **+** du bus, créent un conteneur au curseur. Déposer un fichier son sur la ligne d’un bus crée directement une sous-piste avec son clip.

Dans le menu d’affichage (icône œil), **Afficher le contenu des sous-pistes** affiche les clips à l’intérieur des cadres. Déplacer un conteneur par sa barre de titre déplace ses clips et ses automations ensemble. Le déplacer sur une autre ligne change le bus de sortie. Ses bords de titre règlent le cadre ; ils restent autour des clips qu’il contient, qui conservent leurs propres outils de rognage.

Le routage initial est :

```text
Sous-piste audio / MIDI → effets de la sous-piste → bus et ses effets → master
```

Cliquer sur le nom d’un bus ouvre l’onglet supplémentaire **Sous-pistes** dans le panneau inférieur. Il liste tous ses conteneurs, même hors de la vue : nom, type, début, durée et nombre de clips. Un clic sélectionne une sous-piste ; **Cadrer** ou un double-clic sur son nom ajuste le zoom et le défilement pour la retrouver dans la timeline. **FX** ouvre ses effets, **A** son automation.

Le **FX** du bus ouvre sa chaîne. Le panneau inférieur conserve **Périphériques, Graphe, Mixeur, Routage, Éditeur**, avec la même bibliothèque que le Studio. Les bus créés dans ce mixeur ajoutent aussi des lignes dans Cinéma. Le routage reste modifiable dans son onglet.

Les boutons **M**, **S** et **A** des bus donnent accès à muet, solo et automation. Le clic droit du titre d’un conteneur donne ces commandes pour sa sous-piste, ses effets et son instrument. **A** est aussi disponible dans la liste Sous-pistes et sur le master ; les courbes contrôlent le volume ou les paramètres des modules. Le bouton **MASTER** sélectionne ses effets. Les panneaux se redimensionnent par leurs séparations et se déplacent par leurs onglets. Le panneau inférieur se masque avec **Masquer** ou **Ctrl+J** ; **Ctrl+J** ou l’icône de restauration dans la barre de montage le rouvre.

Le menu contextuel d’une sous-piste permet de la renommer, de la déplacer vers un autre bus ou de la supprimer. Déplacer une sous-piste vers un bus change aussi sa sortie principale vers ce bus. Retirer un bus garde ses sous-pistes et les renvoie directement au master.

## Disposition et outils de montage

La double bande au-dessus des pistes reprend la navigation des autres timelines : attraper la bande supérieure et glisser à gauche/droite pour déplacer la vue, vers le bas pour zoomer, vers le haut pour dézoomer. Le temps attrapé reste sous le pointeur, y compris lors d’un geste diagonal ; le double-clic cadre tout le contenu. La bande inférieure porte les timecodes et place le curseur. Naviguer pendant la lecture suspend le suivi automatique jusqu’à sa reprise par le bouton de suivi ou une nouvelle lecture.

Le mode reprend les icônes du Studio : zoom −/+, tout voir, suivi de lecture, ciseaux (scinder à la tête de lecture), dupliquer, supprimer, annuler et rétablir. Le zoom se règle aussi par **Ctrl+molette** autour du pointeur, ou **+/−** au clavier. **Maj+molette** navigue dans le temps ; la barre horizontale sous le montage sert au défilement.

Les panneaux **Montage**, **Vidéo**, **Périphériques de la piste** et **Inspecteur de clip** sont réorganisables. Glisser leur onglet pour les placer ailleurs, les regrouper ou les détacher ; tirer une séparation pour les redimensionner. L’icône œil de la barre de montage permet de réafficher la vidéo, masquer les vignettes, choisir le contenu des conteneurs ou réinitialiser la disposition. Cette disposition est indépendante de celle du Studio.

## Vidéo et bande son

**+ → Charger une vidéo…**, **Charger une vidéo…** dans le moniteur, ou le dépôt d’un fichier vidéo chargent la référence. Le moniteur suit la lecture et les déplacements du curseur. La section **Réglages vidéo** du moniteur contient les champs **Début dans le projet**, **Point d’entrée vidéo**, **Durée du plan** et **Images / seconde** pour régler le placement et le timecode. Les boutons image précédente/suivante et les flèches gauche/droite dans le montage permettent le déplacement image par image. Le choix **Grille → Image** active le magnétisme temporel des clips ; Alt le suspend pendant un geste.

Une bande de vignettes apparaît sous la règle temporelle, à la même échelle que les sons. À vue large, elle échantillonne les images ; à zoom rapproché, chaque image du film a sa vignette et son timecode. Cliquer ou glisser sur cette bande déplace le curseur et le moniteur à l’image correspondante. Les repères et la grille restent alignés sur les images. Les vignettes sont décodées dans un second décodeur en arrière-plan et conservées dans un cache limité à 128 textures par session ; les seeks de la bande ne déplacent pas le décodeur du moniteur.

Si la vidéo contient du son, CyDAW décode automatiquement la piste audio choisie par défaut par FFmpeg en arrière-plan et crée :

```text
Bus « Bande son vidéo »
└─ Sous-piste « Guide vidéo » : clip audio complet de la vidéo
```

Ce clip est un son normal, décodé en PCM stéréo 48 kHz, 16 bits puis adapté à la fréquence de la session. Il conserve toute la bande son, indépendamment des points d’entrée/sortie de la référence vidéo. Son placement initial correspond au début de la vidéo dans le projet. Il se coupe, se déplace, se duplique, se rogne et se traite comme un WAV importé. Le déplacer sur une autre piste le fait passer dans les effets et le routage de sa destination. Modifier ce clip ne modifie jamais le fichier vidéo. Une vidéo muette ne crée pas de bus audio vide.

Le bouton **Importer la bande son comme piste…** permet une nouvelle extraction explicite, dans un nouveau bus. La réouverture d’une session déjà sauvegardée ne réimporte pas le son : elle reprend les clips édités.

**Sans vidéo** retire la référence du moniteur et conserve les clips audio déjà importés.

## Clips et montage audio

- Déposer un son sur une sous-piste l’importe à cet endroit. En dehors des sous-pistes, une nouvelle sous-piste est créée dans le bus sélectionné.
- Glisser un clip à l’intérieur d’un conteneur le déplace dans le temps ou vers un autre conteneur de même type ; **Ctrl** au dépôt le copie. Tirer ses bords le rogne. Le cadre s’étend si nécessaire pour garder tous ses clips visibles. La barre de titre du cadre déplace toute la sous-piste.
- Le clic droit donne les détails, **Reverse**, **Scinder au curseur**, dupliquer et supprimer. **S** scinde au curseur ; **Ctrl+Z / Ctrl+Y** annulent et rétablissent les éditions de clips.
- Le panneau **Détails** règle début, durée, décalage dans le son, gain et fondus. Un double-clic dans une sous-piste MIDI vide crée un clip à cet endroit et ouvre l’éditeur de notes. Le clic droit dans une zone vide propose aussi **Créer un clip MIDI ici**, l’instrument, l’import et le collage. Dans une sous-piste audio, le double-clic ouvre le choix d’un son pour cette sous-piste précise ; le clic droit propose **Importer un son ici…** et **Créer un clip audio vide ici**. Ces créations s’annulent et se rétablissent. Le choix de fichier reste attaché à la sous-piste et au document d’origine si l’utilisateur change d’onglet entre-temps.
- La molette défile verticalement ; **Maj+molette** défile dans le temps et **Ctrl+molette** zoome. Une barre de défilement horizontale est sous la timeline. Les icônes de zoom et Tout voir règlent la vue commune.

Les sous-pistes audio et MIDI restent distinctes : un clip audio peut passer sur une autre piste audio, un clip MIDI sur une autre piste MIDI. Déposer un fichier son sur une piste MIDI utilise le sampler comme en Fonderie.

## Session et export

**Enregistrer / Ctrl+S** écrit une session **`.cycinema`** (version 5) : clips, notes, automations, bus, positions et durées des conteneurs, effets, routage, marqueurs et référence vidéo. Les sessions de la première version Cinéma restent lisibles ; leurs sous-pistes deviennent des conteneurs autour de leurs clips. Les sons modifiés ou extraits sont enregistrés dans le dossier de fichiers associé, à conserver avec la session. La vidéo reste référencée à son chemin d’origine ; conserver ce fichier pour retrouver l’image. Si elle manque, la session audio reste éditable et exportable.

L’icône **Exporter le mixage…** exporte le rendu audio, effets et automations compris. Cette version utilise un seul fichier vidéo de référence et ne produit pas de montage vidéo ni de fichier vidéo remuxé avec le mixage. Le décodage vidéo et l’extraction audio utilisent les bibliothèques FFmpeg intégrées (build partagé LGPLv3), sans dépendre des codecs Windows. Sont notamment pris en charge H.264, HEVC, VP9, AV1, ProRes, DNxHD/DNxHR et AAC, E-AC-3/AC-3, DTS, Opus, FLAC, PCM. Les conteneurs MP4, MKV, MOV, WebM, AVI, MXF et MPEG-TS sont acceptés. Le moniteur indique le décodeur et le codec détecté. La présence d’un DRM ou un flux endommagé peuvent toujours empêcher une lecture. Le moniteur décode en arrière-plan, avec une prévisualisation limitée à 960 × 540 ; les fichiers d’origine restent intacts.

## Synchroniser un logiciel externe

Dans **SYNC**, choisir la cible **Cinéma** puis le protocole pris en charge par l’autre logiciel. La cible désigne l’onglet actif de Cinéma et reste indépendante des transports Studio et Fonderie. Voir [Synchronisation du transport](TRANSPORT_SYNC.md).

Pour le son à l’image, utiliser un protocole de position absolue tel que **MTC + MMC** et régler la même cadence et la même origine de timecode des deux côtés. Link partage tempo et phase musicale, pas le timecode absolu d’un film. Un port MIDI virtuel ou réseau se configure séparément dans le système.

DaVinci Resolve 19 documente la poursuite MTC dans les commandes de contrôle à distance Fairlight : lorsque cette fonction est disponible dans l’édition utilisée, CyDAW peut être configuré en émission MTC/MMC sur le port qui alimente Resolve. Cette configuration n’a pas été validée ici avec Resolve. CyDAW ne reçoit pas le LTC audio dans cette version. Référence : [guide officiel Resolve 19](https://documents.blackmagicdesign.com/au/SupportNotes/DaVinci_Resolve_19_New_Features_Guide.pdf?_v=1712905211000).

## Contrôle et validation

La commande locale `cinema` reprend les actions de `foundry` pour les clips, notes, traitements, automations, effets, lecture, sauvegarde et export. Les actions supplémentaires sont :

- `group`, avec `op: add|rename|select|remove`, `id`, `name` : les conteneurs sont des bus.
- `add_subtrack`, avec `groupId`, `type: audio|midi`, `name`, et facultativement `at`, `length`, `seconds: true` ; `assign`, avec `trackId`, `groupId`.
- `container`, avec `id`, `op: focus|select|move|resize|rename|delete`. Move/resize acceptent `at`, `length` et `seconds: true`. Focus cadre la sous-piste et ouvre sa liste ; select la sélectionne sans changer la vue.
- `video`, avec `file`, `start`, `sourceIn`, `duration`, `fps`, ou `remove: true`. Un nouveau chargement déclenche l’extraction audio automatique.
- `video_audio` relance explicitement l’extraction complète.
- `view`, avec `start`, `seconds`, `snap`, `length` en secondes, `contentsVisible` pour l’édition imbriquée, `filmstrip`, `zoom` (facteur), `fit: true` et `resetLayout: true`.

Le statut expose `cinema`, `picture`, le décodeur, les IDs de bus et `parentId`. Les pistes exposent `containerStart` et `containerLength` en frames. Les rectangles `panes` sont donnés dans les coordonnées clientes de la fenêtre, avec `timelineX` et `timelineWidth` pour l’échelle commune ; `buses` et `subtrackFocusButtons` décrivent les cibles des tests d’interface. `app_mode` accepte `cinema` et `transport_sync.target` accepte `cinema`.

`tests/v2_cinema_check.py` s’exécute uniquement contre une instance isolée `--silent`, avec Pillow et un exécutable FFmpeg pour fabriquer la mire de test :

```text
python tests/v2_cinema_check.py --port 48072 --output output/cinema-tests --ffmpeg <ffmpeg.exe>
```

Il vérifie les rendus réels des bus et de leurs effets, muet, automations, instruments MIDI, création de conteneurs par double-clic, déplacement avec leur audio, annulation et routage restauré, liste et bouton Cadrer à la souris, compatibilité des anciennes sessions, rognage des clips, image H.264 au changement de seconde, orientation, extraction automatique complète, découpage et déplacement de la bande son, sauvegarde/réouverture, vidéo manquante, vidéo muette et réception MTC/MMC sur Cinéma. Les contrôles Fonderie/Mix DJ existants restent applicables au moteur partagé.

Le test `tests/v2_cinema_layout_check.py --exe <CyDAW 2.exe> --output output/cinema-layout-ui` lance sa propre instance silencieuse. Il vérifie les vignettes consécutives, le scrubbing et le moniteur, le zoom ancré à la souris, les boutons natifs de zoom et de découpe avec undo/redo, le déplacement du panneau vidéo à la souris, le masquage/restauration des outils et les diagnostics de disposition ImGui.

`tests/v2_cinema_codecs_check.py --exe <CyDAW 2.exe> --output output/cinema-codecs` vérifie six familles vidéo, l’audio E-AC-3 5.1 converti en stéréo, les seeks avant/arrière, les vignettes et le rendu audio extrait. Il contrôle aussi la création réelle de clips dans les sous-pistes à la souris. La préparation du SDK est documentée dans `third_party/FFMPEG.md` ; les DLL et leurs informations de licence sont copiées à côté de l’exécutable par CMake.
