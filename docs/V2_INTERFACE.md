# L'interface de CyDAW (référence)

L'interface de CyDAW, en Dear ImGui sur Direct3D 11 : ce document en décrit chaque panneau et les
commandes du port de contrôle, pour qui développe ou pilote le logiciel.

- Exécutable : `CyDAW2_artefacts/Release/CyDAW 2.exe` dans le dossier de compilation (cible CMake
  `CyDAW2`).
- Projets : fichiers `.cydaw`.
- Réglages : la carte son dans `%APPDATA%\CyDAW\CyDAW.layout.settings`, la disposition des panneaux
  dans `%APPDATA%\CyDAW\CyDAW2.layout.ini`.

## Disposition

Par défaut : l'arrangement et le lanceur au centre, puis Périphériques, Graphe, Mixeur, Routage et
Éditeur en dessous ; l'inspecteur à gauche, le navigateur à droite (la disposition a une version : un nouveau
défaut s'applique une fois aux dispositions enregistrées). Le **lanceur porte son mixeur**, comme
Bitwig : sous chaque colonne de piste, sa tranche (nom et petit potard de pan en haut, grand fader et
grand vu-mètre, M/S/R), et à droite une colonne **Master** fixe (arrêter tout, lancer chaque scène, la
tranche master avec son vu-mètre et un bouton M qui coupe tout sans perdre le niveau,
`AudioTrackController::setMasterMuted`). Les noms des pistes restent en haut, la rangée de stops et les
tranches en bas : seules les **scènes** défilent entre eux (molette sur les scènes, ou leur barre entre
la grille et la colonne Master, qui suit) ; ailleurs la molette (et Maj+molette partout) fait défiler les
pistes de côté.

**La piste Master** a ses périphériques : un clic sur son nom (tranche du lanceur ou du mixeur, en-tête
de sa colonne) la choisit, et Périphériques, Graphe et l'Inspecteur la montrent comme une piste. Les
pistes mixées passent par son graphe, puis par son fader (`MasterOutputAudioSource::setChainGraph`,
avant le gain ; l'export du mix l'inclut). Un limiteur y est de base (nouveau projet, et projets d'avant
qui n'en ont pas) ; « + » ajoute des effets (pas d'instrument, pas de plugin VST3 pour l'instant). Le
graphe est enregistré dans le projet (`masterGraph`). Côté modèle : `TrackRef::master`
(`Studio::masterTrack()`), que `isValid` refuse pour que rien de propre aux pistes du moteur ne s'y
applique ; son graphe, son nom, son mix et son vu-mètre répondent.

**Routage entre pistes** : chaque piste a ses sorties — le master de base, puis autant qu'on veut —
vers le master, un bus ou n'importe quelle autre piste, chacune à son niveau (un envoi à -8 dB vers une
réverbération partagée…). Le son routé rejoint celui de la piste de destination avant ses
périphériques (après les voix pour un instrument). Une **piste bus** (Piste › Nouvelle piste bus, le
bouton Bus de l'arrangement, ou le menu d'une piste) est une piste sans clips où d'autres se routent :
ses effets, son fader, ses propres sorties. Une boucle est refusée ; en solo, le bus qu'une piste
alimente reste audible avec elle. Réglages : l'Inspecteur (section Routage : ce que la piste reçoit, sa
sortie, ses envois, « + Sortie »), le bouton « → destination » sous le fader de chaque tranche (lanceur,
mixeur), et le menu d'une piste (Sortie ›, Sorties et envois…, Envoyer vers un nouveau bus). Moteur :
`TrackMixer` remplace le `MixerAudioSource` des pistes (ordre émetteurs avant destinations,
`RoutedInputReceiver` pour injecter le son reçu), `AudioTrackController::setTrackRouting` ; Studio :
`TrackOutput`, `routingKey`/`trackForKey` (stables quand les pistes bougent), `setTrackOutputs`,
`addBusTrack` ; projet : `outputs` et `bus` par piste.

**Compensation de latence à travers le routage** : la latence des plugins (et le bloc que l'hôte isolé
ajoute à chacun) est compensée sur tout le graphe de routage. Chaque piste reçoit à la latence la plus
tardive de ce qui y entre (son propre son attend d'autant), en ressort avec celle de ses plugins en
plus ; chaque câble est retardé pour arriver en même temps que le plus tardif à sa destination, et le
master attend le chemin le plus long (`status` › `latencyCompensationSamples`). Ce que les sorties d'une
piste après le fader ont en commun est son retard de sortie ; le reste, et un envoi PRE (pris avant ce
retard), ont une ligne de retard par câble, gardée tant que sa longueur ne change pas (bouger un niveau
ne la relance pas). Moteur : `AudioTrackController::rebuildRouting` (routage gardé par identité de piste,
refait quand une latence change), `AudioDelayLine`, `TrackMixer::Output::delay`, retard d'entrée de
`GainPanAudioSource` et des voix d'un instrument.

**Formats des pistes** : une piste audio n'est pas forcément stéréo. Son format est **mono, stéréo, LCR,
quad, 5.0, 5.1, 7.0 ou 7.1** (1 à 8 canaux, dans l'ordre d'un fichier WAV : L R C LFE puis arrière et
côtés, `include/cydaw/engine/ChannelLayout.h`). Une piste faite d'un fichier de plus de deux canaux prend
son format (un WAV 5.1 donne une piste 5.1) ; les autres sont stéréo jusqu'à ce qu'on change leur format
(Inspecteur › Format, ou la colonne Format du panneau Routage). Ses clips y sont convertis (un fichier
mono s'étale à gauche et à droite, un 5.1 se replie sur une piste stéréo), ses effets traitent chaque
canal, son pré-fader et ses retards de compensation aussi, et son son se convertit vers où il va : une
enceinte que la destination a est gardée, une qui lui manque va à ses voisines (centre et surrounds à
-3 dB), le LFE seul disparaît ; vers le mono, la moitié de chaque côté. Le panoramique d'une piste mono
la place entre gauche et droite ; celui d'une piste plus large baisse les enceintes du côté opposé. Le
master et les pistes instrument restent stéréo. La piste n'affiche que deux côtés (le côté gauche le plus
fort, le droit) ; ses détails (Inspecteur › Canaux, panneau Routage) montrent chaque canal sous le nom de
son enceinte. Moteur : `ChannelMatrix`/`channelConversion`, `TrackMixer` (chaque piste rendue dans son
format, `Entry::channels`, `Output::channels`), `GainPanAudioSource::setChannelCount`,
`ClipAudioSource` (decks à la largeur de la piste, `SegmentReaderSource` convertit les canaux du
fichier), `AudioTrackController::setTrackChannelCount` ; Studio : `trackChannels`, `setTrackChannels` ;
projet : `audioChannels` par piste ; port : `set_track` `format` (`mono`…`7.1`) ou `channels`.

**Entrées et sorties virtuelles** : celles du logiciel, pas d'un projet. Elles se créent dans Options ›
Réglages audio › « Entrées et sorties de la carte » (ou Options › Entrées et sorties de la carte son…) et
sont gardées dans `%APPDATA%\CyDAW\VirtualIO.json` avec leurs effets ; chaque projet les assigne à ses
pistes (il les note par id et par nom, et les retrouve par nom ailleurs ; il ne les remplace plus). Chacune a
**sa chaîne d'effets** : « N effets › » (ou Routage › « Modifier ses effets ») la montre dans Périphériques et
Graphe comme celle d'une piste — par exemple une entrée « Micro » avec un compresseur et un limiteur, pour
que le son arrive déjà traité. Une entrée tourne en continu (ses affichages bougent même sans
enregistrement) ; une piste l'enregistre **après ses effets** (par défaut) **ou avant** (brut), au choix
(Routage › Entrée). Une piste envoyée sur une sortie virtuelle passe **à travers ses effets ou après eux**.
Moteur : `VirtualBusRouter::inputBusBlock` (le bloc de chaque entrée avant et après ses effets, lu par
l'enregistreur juste après), `setTaps`/`readTap`, `DirectOutputSource::directOutput(…, beforeEffects)` ;
`AudioTrackController::setTrackInputAfterEffects`, `TrackOutputRoute::hardwareAfterEffects` ; Studio :
`TrackRef::ofBus` (un bus édité comme une piste : `master` vrai, `bus` son id), `virtualIoFile`,
`loadVirtualIo`, `flushVirtualIo` (une seconde après un changement, et en quittant ; les sessions
automatiques n'y touchent pas, sauf avec `CYDAW_VIRTUAL_IO_FILE`) ; projet : `inputBusName`,
`inputAfterEffects`, `hardwareName`, `hardwareAfterEffects` ; port : `set_track` `inputAfterEffects`,
`routing` `hardwareAfterEffects`, `set_view` `bus`, `list_buses` `effects`, les commandes de graphe sur
`"bus:<nom>"`. Les bus vont de 1 à 8 canaux, chacun sur le canal de la carte qu'on choisit (un canal fermé est en
orange). Un bus d'entrée est ce qu'une piste audio enregistre (Inspecteur › Entrée, ou Routage ›
Entrée) : un micro, une paire, une prise
quad ou 5.1 (l'enregistrement a ses canaux) ; « Écoute » l'entend replié en stéréo sur sa paire. Un bus de
sortie joue le master (« Master ») et ce que les pistes lui envoient directement : une piste choisit
« Carte › Sorties 3-6 » comme sortie (ou envoi), sans passer par le master ni son fader ; décoché, le bus
ne joue que ces pistes (sorties directes vers une console, un casque de retour). En haut, combien des
entrées et sorties de la carte sont ouvertes (les premières) : un bus compte parmi elles. Moteur :
`VirtualAudioBus` (`channels` jusqu'à 8, `moreChannels`, `carriesMaster`, `deviceChannel`),
`DirectOutputSource` (le mixeur des pistes y laisse le son des sorties directes, le routeur des bus le lit
dans le même bloc), `TrackOutputRoute::hardwareBus`, `deviceChannels`/`setDeviceChannelCounts` ; Studio :
`TrackOutput::hardware`, `updateHardwareBus`, `addHardwareBus`, `removeHardwareBus` (les sorties qui y
allaient s'en vont) ; projet : `hardware` par sortie, `moreChannels` et `carriesMaster` par bus ; port :
`routing` (`hardware` : un bus de sortie), `set_bus` (`format`, `deviceChannels`, `carriesMaster`),
`device_channels` (`inputs`, `outputs`). L'export du mix ne prend que le master.

**Panneau Routage (F8)** : la piste choisie, en grand, et seulement son entrée et sa sortie, côte à côte.
**Entrée** : pour une piste audio, ce qu'elle enregistre (un bus d'entrée de la carte) et ses canaux sur
la carte (« L ← 1 Entrée micro ») ; pour une piste instrument, les claviers MIDI ; pour un bus, ce qui y
est routé ; puis les pistes qui lui envoient du son (avec leur niveau) et ses autres entrées (sidechain).
**Sortie** : sa sortie et ses envois, chacun avec sa destination (master, une sortie de la carte, un bus,
une piste, son sidechain), son niveau et PRE, « + Envoi ». Pour le **master** : les pistes mixées en
entrée, et en sortie les sorties de la carte qui le jouent (à cocher). La carte son elle-même se règle
dans Options › Audio (`RoutingView.*`, `SoundCardView.*`, éléments partagés avec l'inspecteur dans
`RoutingUi.*`). `set_view` 8 ou 11 l'amène devant ; `show_dialog` `soundcard` ouvre les entrées et
sorties de la carte.

**Compensation de latence** : un plugin, un Compresseur ou Limiteur Pro qui anticipe, l'ampli
suréchantillonné retardent le son de leur piste ; chacun le déclare (`AudioProcessor::latencySamples`).
Dans le graphe d'une piste, les chemins parallèles plus rapides attendent le plus lent
(`AudioRenderPlan`, une ligne de retard par câble vers un nœud que plusieurs chemins atteignent) ; dans
le mixage, les autres pistes attendent la plus en retard, à travers les bus et les envois. L'export
saute ce retard et celui du master. Le port de contrôle donne, par piste, `latencySamples` (le sien) et
`compensationSamples` (l'attente que le mixage lui donne), et `latencyCompensationSamples` (le mixage).

**Entrées d'une piste et envois avant le fader** : une piste a son entrée principale (ses clips, son
instrument, ou pour un bus ce qui y est routé) et jusqu'à trois autres (Inspecteur › Routage ›
« + Entrée », nommées, retirables) : chacune est un nœud d'entrée audio de son graphe, à relier où elle
sert. Une sortie peut viser l'une d'elles (« Bus › Sidechain » dans la liste des destinations) : c'est le
sidechain. Le plus simple : le bouton maillons de la carte d'un Compresseur Pro ou d'une Porte, dans
Périphériques, choisit la piste que son détecteur écoute ; CyDAW ajoute l'entrée, la relie à la prise
« sidechain » de la carte et y route la piste, avant son fader (docs/EFFETS_STUDIO.md). À la main :
l'entrée Sidechain → la prise sidechain d'un compresseur, ou → un suiveur d'enveloppe → un câble de
contrôle en « moduler » (profondeur négative) sur le gain du bus. Chaque sortie a aussi **PRE** : prise avant le
fader de la piste (son niveau ne suit plus le fader ; muet et solo s'appliquent toujours). Moteur :
`NativeDspNode::inputBus` (0 la principale, puis 1, 2, 3), `AudioRenderPlan::process` avec des entrées en
plus, `NativeDspRuntime::setExtraInputs`, `RoutedInputReceiver::setRoutedInputs` (4 bus), le son avant
fader gardé par `GainPanAudioSource` quand un envoi PRE le demande ; les deux compilateurs (typé et
repli) numérotent les entrées dans l'ordre du graphe, et sur une piste instrument toutes ses entrées
audio viennent après l'instrument (`NodeGraphComponent::setInstrumentIsMainInput`). Studio :
`TrackOutput::input` (le nœud d'entrée visé, stable) et `preFader`, `trackInputs`, `addTrackInput`,
`removeTrackInput` ; projet : `input` et `preFader` par sortie. Correction au passage : changer un câble
de contrôle en « moduler » après sa création restait sans effet (le cache de compilation ignorait le
mode et la profondeur des câbles).

**Recréer un son (sound matching)** : Piste › Recréer un son (enregistrement → instrument)… prend une ou
plusieurs notes isolées (WAV, une zone chacune) ; le compagnon Python les analyse en harmoniques + bruit (+ attaque,
boucle, relâchement) en tâche de fond (barre d'état, Annuler) et une nouvelle piste instrument joue le modèle,
en DSP pur, à toutes les hauteurs et vélocités (entre deux notes enregistrées, les deux zones mêlées). La carte
**Modèle de son** montre ses partiels dans le temps et 14 réglages (harmoniques, bruit, attaque, brillance,
formants, inharmonicité, vibrato, vitesse, relâchement, hauteur du bruit et de l'attaque, vélocité) ; « Charger… »
y met un autre modèle, « Lisible » en fait un patch usuel (wavetable + ADSR + filtre) sur une nouvelle piste.

**Sampler et instruments d'usine** (docs/SAMPLER.md) : Navigateur › Instruments (et Piste › Instruments
d'usine) donne les instruments d'usine (pianos, clavecins, harpes, cordes solo et d'orchestre, cuivres, bois,
orgues, percussions mélodiques…), en échantillons (un double-clic) ou en modèle de son (clic droit). Leurs
échantillons ne sont pas livrés : un instrument qui les attend affiche leur taille, le double-clic crée la
piste et les télécharge (elle joue dès qu'ils sont là). **Options › Banques de sons…** liste les banques
(auteur, licence, taille, ce qui est là, provenance), les télécharge ou les supprime, et suit le
téléchargement en cours. **Options › Emplacements…** choisit où vont les banques, les téléchargements, le
cache, le compagnon, les modèles, Mes périphériques, les exports de plug-ins et les préréglages, le contenu déplacé avec
(`LocationsView.*`, `BanksView.*`, `Locations.*`, `SoundBanks.*`). Piste › Nouveau sampler… met des fichiers audio sur une nouvelle piste, chacun sur ses
touches (la note de son nom ou de sa hauteur, plusieurs par note en couches de vélocité ou round robins) ;
Piste › Ouvrir un instrument SFZ… lit un SFZ. La carte **Sampler** montre ses zones (touches × vélocités) et
prend les fichiers qu'on y glisse ; « Zones… » ouvre l'éditeur (carte et tableau des zones, racine, touches,
vélocités, accord, gain, panoramique, round robin, forme d'onde avec début, fin et boucle à glisser, SFZ
ouvert ou écrit). Les voix des instruments sont stéréo pour le sampler ; 32 voix par piste.
Piste › Charger un modèle de son… (ou un .cysound déposé sur la fenêtre) le rejoue. Les modèles vont dans
le dossier des modèles de son (Documents/CyDAW/Sound Models par défaut) et sont enregistrés dans le projet
avec la carte. Options › Compagnon Python : quel
Python, ce qu'il a (numpy, PyTorch, CUDA, la carte NVIDIA) et, sur un clic, son propre environnement avec numpy
puis PyTorch CPU ou CUDA (rien n'est téléchargé sans ce clic). Détails et étapes : `docs/SOUND_MATCHING.md`.

**Fenêtres des périphériques** : un périphérique fait d'un graphe qui a une interface dessinée (dans
l'atelier, onglet Interface) a un bouton œil sur sa carte (Périphériques), et l'instrument d'une piste
MIDI au pied de sa colonne : il ouvre cette interface dans une fenêtre à part, jouable (potards, faders,
interrupteurs, sélecteurs, pad XY ; double-clic : valeur par défaut), comme la fenêtre d'un plugin. Les
fenêtres se déplacent sur un autre écran ; plusieurs peuvent être ouvertes (`PluginWindows`,
`InterfaceEditor::drawPlayed`, `NodeGraphComponent::editorNodeInterface`). Quand le panneau
Périphériques est assez haut pour la montrer lisible (à 60 % de sa taille au moins), la carte affiche
cette interface, jouable, à la place de ses potards ; plus bas, elle garde ses potards et l'œil ouvre la
fenêtre.

**Catalogue « + »** (Périphériques) : sans recherche, un arbre — Mes périphériques, puis les graphes
d'usine par catégorie (Effets › Dynamique et canal, Délai et espace…, MIDI, Modulateurs, Utilitaires…),
les chaînes complètes et les nœuds ; une recherche donne la liste à plat. Dans l'arbre des nœuds (atelier,
recherche du graphe), « Graphes » vient juste après « Audio », rangé de même par catégorie, et « Entrées
et sorties » est replié (Entrées, Sorties). Clic droit sur une piste (en-tête de
l'arrangement ou du lanceur, bande du mixeur) : son menu (renommer, couleur, muet, solo, armer,
automation, ses périphériques, son graphe, l'atelier pour un instrument, monter, descendre, nouvelle
piste, supprimer) ; sur une case du lanceur : lancer, arrêter, ouvrir, renommer, couleur, dupliquer
dans la scène suivante, supprimer, ou un nouveau clip MIDI, et sur une case vide son stop
(`ContextMenus.*`).

Dans le Lanceur, déposer un fichier audio sur une case de piste audio crée ou remplace son clip.
Plusieurs fichiers occupent les scènes successives. Déposer un son dans la zone vide à droite des
pistes crée une piste MIDI dont l’instrument est un sampler chargé avec ce son. Le dépôt fonctionne
avec les fichiers du navigateur interne et ceux de l’Explorateur Windows. Le clic droit sur un clip
audio propose **Inverser le clip (Reverse)**, également disponible dans l’Arrangement et la Fonderie.

**Lanceur** (`LauncherView.*`, `Studio::launchScene`…) : comme dans Bitwig, la **colonne des scènes est à
gauche** (un bouton par rangée, « Tout » sous elles arrête tous les clips) et la **colonne Master à droite
a ses propres cases** : des **clips d'automation du master**, un par scène (`MasterClip`, `Studio::masterClips`).
Double-clic sur une case vide du Master : un clip (« Master n »), ouvert dans l'Éditeur ; il a un nom, une
longueur en temps, une boucle, et des courbes : le **volume du master** (`master.gain`, en dB) ou n'importe
quel réglage des effets du master (choisi dans la liste « Courbe », avec une recherche ; ● : la courbe a des
points). Dans l'éditeur : clic ajoute un point (et le tire), glisser le déplace entre ses voisins (aimanté à
la grille, Alt pour libérer), clic droit le retire. Le clip part avec sa scène, à la quantification du
lanceur, sur une horloge du master qui tourne pendant la lecture (`MasterOutputAudioSource::clock`) ; ses
courbes d'effets sont des couloirs d'automation « de clip » (`AutomationLane::clipStartSeconds`,
`clipLengthSeconds`, `clipLooping`) que le moteur lit à chaque bloc, sa courbe de volume fait bouger le
fader du master (remis où il était quand le clip s'arrête). Une scène lancée sans clip Master arrête celui
qui joue ; Stop puis lecture le relance depuis son début, comme les clips des pistes. Menu de la case :
lancer, éditer, dupliquer vers la scène suivante, supprimer. Enregistrés dans le projet (`masterClips`).
Port de contrôle : `master_clip {scene, action, name, lengthBeats, looping, lane, points}` (action `lanes` :
ce qu'une courbe peut automatiser), `launch_scene` répond aussi `masterScene`.
Un clic sélectionne une case (`UiState::selectedSlots`,
Ctrl/Maj pour plusieurs), le triangle d'un clip le lance (son carré l'arrête pendant qu'il joue), une rangée
de stops par piste sous les clips. Une case vide a un stop (actif par défaut) : une scène lancée arrête
les pistes dont la case est vide, sauf si ce stop est retiré (Ctrl+E, menu ; enregistré par piste dans le
projet, `launcherStopOff`). Lancer un clip audio ou une scène n'arrête plus les clips MIDI des autres pistes.
**Stop puis lecture** : les clips qui jouaient quand le transport s'arrête attendent (leur case s'encadre)
et repartent **depuis leur début** à la lecture suivante, tous ensemble ; lancer un clip ou une scène
pendant l'arrêt les fait partir avec lui ; « Tout arrêter » ou l'arrêt d'une piste les oublie
(`Studio::clipsAtStop_`, `AudioTrackController::restartLauncherClips`).
**Scènes** : jusqu'à 256 (`maximumLauncherScenes`) ; la grille montre celles qui servent et deux de plus
(huit au moins), les autres défilent (molette, barre). **Un clip MIDI du lanceur a sa case à lui**
(`MidiClip::launcherScene` ; -1 : clip de l'arrangement) : l'arrangement ne le joue ni ne le montre, et un
clip de l'arrangement n'apparaît plus dans le lanceur (comme dans Bitwig ; avant, la scène se déduisait
de la position, une scène tous les 16 temps). Enregistré dans le projet (`launcherScene` du clip). Port
de contrôle : `add_midi_clip {scene}` crée le clip dans la case, `set_midi_clip {scene}` l'y range (-1 :
le rend à l'arrangement). Un projet importé (Bitwig, Ableton, DAWproject) garde toutes ses lignes : un
projet Bitwig de 215 scènes arrive entier (431 clips, 143 836 notes), là où il en perdait la quasi-totalité.
Bitwig : les pistes **hybrides** (type 2 : notes et audio) sont lues comme des pistes d'instrument (une piste
« (audio) » à côté si elles ont aussi de l'audio), le Master est celui que nomme le groupe racine
(`master_track`), les pistes d'effets sont lues aussi ; une piste sans nom prend celui que Bitwig montre
(son instrument, sinon « Audio 14 », « Inst 1 »…). Les pistes audio restent avant les pistes MIDI.
Armement : choisir une piste n'arme plus rien (`setArmFollowsMidiInput(false)` ; CyDAW 1 garde l'ancien
comportement), plusieurs pistes MIDI s'arment ensemble ; le clavier joue la piste choisie, ou une piste
armée (la choisie si elle l'est) ; changer de piste ne lâche plus que les notes tenues au clavier (les notes
des clips continuent). Port de contrôle : `slot_stop` (`track`, `scene`, `stops`), `launch_scene` répond
la scène de chaque piste, `set_master_gain` prend `muted`.

Barre de transport en haut, panneaux ancrables (glisser un onglet pour le déplacer, le détacher ou
le regrouper ; Affichage > Réinitialiser la disposition). Un panneau tiré hors de la fenêtre, ou
Affichage > Détacher dans une fenêtre, devient une fenêtre à part, à placer sur un autre écran ;
ramené sur un emplacement d'ancrage, il se réancre. **Affichage > Dispositions** garde des
dispositions nommées (« Mixage », « Deux écrans »…) : Enregistrer la disposition… prend les panneaux,
leur place, l'onglet en avant et les fenêtres détachées ; un clic sur un nom la remet (avant l'image
suivante : Dear ImGui ne lit une disposition qu'entre deux images) ; Supprimer l'envoie à la
corbeille. Fichiers : `%APPDATA%\CyDAW\Layouts\<nom>.ini` (`AppUi::saveLayoutAs`, `loadLayout`) :

Les boutons **Masquer** à droite du titre de l’**Inspecteur** et du **Navigateur** replient chaque
panneau indépendamment. Une flèche orange reste sur le bord gauche ou droit pour le rouvrir ;
**Affichage > Inspecteur (gauche) / Navigateur (droite)** offre les mêmes commandes. Le centre
récupère la place libérée et les préférences sont conservées au redémarrage.

| Panneau | Contenu |
|---|---|
| Arrangement (F1) | Pistes compactes, règle, clips avec forme d'onde ou aperçu des notes, boucle, tête de lecture |
| Lanceur (F2) | Scènes × pistes, lancement quantifié, triangle/carré par clip, rangée de stops, stops des cases vides |
| Périphériques (F3) | La chaîne de la piste sélectionnée en une bande horizontale : pour chaque périphérique ses affichages (voir plus bas) puis les potards restants ; plugins VST3 avec leurs paramètres épinglés en potards, la valeur écrite par le plugin (« -6.0 dB »), ← → pour les réordonner, … : la fenêtre de tous leurs paramètres (potards ou liste, recherche, étoile pour épingler ; le projet garde les épingles) |
| Graphe (F4) | L'éditeur nodal de la piste sélectionnée : Son (ses effets) ou Modulation (son graphe de modulation : LFO, macros, calculs, logique, plages) |
| Mixeur (F5) | Tranches : pan, fader, vu-mètre, M/S/R, master |
| Routage (F8) | La piste choisie (ou le master) en grand : son entrée et sa sortie |
| Éditeur (F6) | Piano roll du clip MIDI ouvert ; pour un clip audio, son éditeur : Échantillon (sélection, couper, fondus, normaliser… et l'historique des versions), Prises (comping, mise à plat), Étirement (algorithme, transposition, tempo, repères), Événements (plusieurs sons dans le clip : déplacer, raccourcir, couper, gain, fondus) — docs/EDITION_AUDIO.md |
| Bibliothèque (F9) | La grande fenêtre (flottante, toute la fenêtre ⤢ ou à part ⧉) : sons, instruments, effets et périphériques (plug-ins VST3 compris), filtre multicolonne avec les comptes de chaque valeur, grand tableau triable, volet d'écoute ; ouverte aussi par le + de la chaîne de périphériques |
| Navigateur | La liste rapide de la bibliothèque de samples : vos dossiers et les banques de CyDAW, un arbre par famille › catégorie › instrument et par source, recherche par nom, et, en option, par description et par ressemblance (modèle CLAP, docs/BANQUE_DE_SONS.md) ; favoris, écoute, glisser vers l'arrangement. Instruments d'usine et catalogue de périphériques |
| Inspecteur | Piste (format, entrée, niveau de chaque canal, routage), clip ou projet sélectionné ; modulation de la piste (son LFO, « Éditer le graphe de modulation › », les paramètres modulés, ceux des plugins VST3 compris) |

## Atelier Plugin

Un mode de toute la fenêtre, à part du studio : le sélecteur **Studio | Atelier Plugin** en tête de la
barre de transport, F7, ou Affichage > Atelier Plugin. Il sert à fabriquer ses propres effets et
instruments avec un graphe, à les garder dans *Mes périphériques* (`Documents/CyDAW/Devices`, un
fichier `.cydawdevice` chacun, utilisable dans tous les projets) et à les exporter en VST3. Ni
pistes ni arrangement dans ce mode ; le transport reste, pour écouter.

- **Onglets** : plusieurs périphériques ouverts à la fois, un onglet chacun (sous la barre du haut ;
  « + » en ouvre un autre, la croix le ferme en demandant d'abord pour un périphérique ni enregistré ni
  sur une piste ; un point marque ce qui n'est pas enregistré). Tout le reste agit sur l'onglet devant.
  Ouvrir un périphérique déjà ouvert ramène son onglet (`Studio::workshopTabs`, `selectWorkshopTab`,
  `closeWorkshopTab`).
- **Simulateur** (sous le graphe) : le périphérique devant, seul, sans les pistes du studio. En mode
  Atelier, Lecture, Stop et Espace lancent la simulation et pas l'arrangement. Un effet reçoit une
  source de test dans ses entrées audio (sinus avec sa fréquence, accords, bruit blanc ou rose,
  batterie au tempo, clics sur les temps, fichier audio en boucle, choisi ou déposé), avec son niveau ;
  un instrument joue un motif en boucle au tempo (note tenue, accords, arpège, basse, gamme, à partir
  d'une note et d'une vélocité), le clavier à l'écran (clic, glisser) et un clavier MIDI (repris aux
  pistes pendant ce temps). Ses autres entrées audio (un sidechain), quand il en a, entendent au choix
  la même source, rien, ou un kick à chaque temps (« Autres entrées »). Sortie : volume et vu-mètre ;
  le résultat va au master. Les afficheurs des
  cartes (spectres, compresseur, égaliseur) et les potards modulés suivent la simulation. Moteur :
  `AudioTrackController::Simulation` (dans le mixeur du master ; un `MidiSynthEngine` pour un
  instrument, un `NativeDspRuntime` pour un effet), `Studio::setSimulator*`/`setSimulating`, publié à
  chaque changement du graphe. Toutes les entrées audio d'un effet reçoivent la même source.
- À gauche : **Nœuds**, un arbre à plusieurs niveaux (Audio › Effets › Modulation, MIDI, Contrôle ›
  Maths…, `NodeTree.*`, le même que la recherche Tab du graphe quand rien n'est tapé ; clic : la carte
  se pose dans un espace libre au milieu de la vue, glisser : où elle tombe) et **Mes périphériques**
  (double-clic : ouvrir ; clic droit : mettre sur la piste choisie, afficher le fichier, corbeille).
- **Graphe | Interface** (barre du haut) : l'interface est la fenêtre du plugin (`InterfaceEditor.*`).
  Contrôles (potard, fader, interrupteur, sélecteur, pad XY, afficheur, texte, cadre, courbes de
  filtre, de dynamique, d'égaliseur, enveloppe ADSR, forme d'onde, spectre, vumètre) glissés de la
  palette ou faits d'un réglage publié ; déplacés et dimensionnés sur une grille aimantée ; liés aux
  réglages publiés à droite ; « Disposition automatique » ; mode Jouer pour tourner les potards. Taille
  de la fenêtre en pixels et grille réglables. L'état est l'interface du graphe racine
  (`NodeGraphComponent::editorSetInterface`, `windowWidth`/`windowHeight` dans « presentation ») ; une
  interface modifiée ici est marquée dessinée (`designed`) et un VST3 exporté ouvre exactement cette
  disposition à cette taille (jouée par `InterfaceEditor::drawPlayed`).
- **Entrée et sortie audio** (onglet Infos) : mono, stéréo ou jusqu'à 8 canaux ; mono → stéréo et
  stéréo → mono passent par un convertisseur (nœuds `mono-to-stereo` avec panoramique, `stereo-to-mono`
  avec balance) posé avant la sortie. Un câble entre une prise mono et une stéréo en pose un tout seul ;
  un périphérique mono mis sur une piste stéréo y entre entre deux convertisseurs.
- Au milieu : le graphe du périphérique. Un effet va de « Entrée audio » à « Sortie audio » ; un
  instrument part de l'entrée MIDI. Le bouton **Entrées et sorties** de la barre du graphe (et la
  branche du même nom dans l'arbre des nœuds) en ajoute : entrée ou sortie audio mono, stéréo ou 3 à
  8 canaux (une seconde entrée pour un sidechain, plusieurs sons à mélanger), entrée et sortie MIDI,
  entrée et sortie de contrôle (une prise sur la carte du périphérique, où brancher un LFO de
  l'extérieur). Chacune devient une prise du périphérique sur sa carte, dehors. Une carte qui est elle-même un périphérique s'ouvre d'un double-clic
  (fil d'Ariane, Échap pour remonter) ; Ctrl+G réunit les cartes choisies en un périphérique (le son qui
  entre et sort, et aussi les notes : la section de voix d'un instrument devient un périphérique, avec son
  entrée MIDI, comme les claviers d'usine).
- À droite : **Infos** (nom, auteur, catégorie, description), **Paramètres** (aperçu des potards tels
  que le panneau Périphériques les montre ; pour chaque réglage : nom affiché, case Panneau, case
  VST3, valeur), **Préréglages** (les réglages du périphérique gardés sous un nom : « Ajouter » prend
  les réglages actuels, un clic en charge un, clic droit : remplacer, renommer, monter, descendre,
  supprimer ; enregistrés avec le périphérique, ils deviennent les **programmes du VST3 exporté**, la
  liste de préréglages de l'hôte), **VST3** (identité du plugin : nom, éditeur, codes de 4 caractères,
  version ; vérification ; « Compiler le VST3 »).
- **Tester sur** une piste : l'effet entre dans sa chaîne, l'instrument devient celui de la piste
  MIDI, et le périphérique y reste lié : chaque changement s'entend pendant la lecture. « aucune
  piste » le détache (l'atelier garde sa copie, la piste la sienne).
- Depuis le studio : le crayon d'un périphérique fait d'un graphe (Périphériques), celui au pied de la
  colonne du nom d'une piste MIDI (son instrument), ou le double-clic sur une telle carte dans le
  Graphe l'ouvrent dans l'atelier, lié à sa piste. « + » > *Mes périphériques* les remet sur n'importe
  quelle piste.
- Compiler : le projet généré (`Documents/CyDAW/Plugin Exports/<nom>/projet`) est compilé avec CMake
  et Visual Studio 2022 en tâche de fond, dans `%LOCALAPPDATA%\CyDAW\vst3` (gardé pour que les
  compilations suivantes soient plus rapides) ; le `.vst3` arrive dans
  `Documents/CyDAW/Plugin Exports/<nom>`. Il faut les sources de CyDAW (trouvées à côté de
  l'exécutable, ou à indiquer). Chaque nouveau périphérique reçoit son propre code plugin ; le nom du
  plugin suit celui du périphérique tant qu'on ne lui en donne pas un autre.
- Quitter avec un périphérique ni enregistré ni sur une piste demande d'abord quoi en faire.
- **La fenêtre du VST3 exporté** : la même interface que dans CyDAW (Dear ImGui), jamais le graphe.
  Prisme, les synthés de la famille et Kick ouvrent leur propre fenêtre en entier (leurs onglets, leurs
  préréglages, leurs effets après eux) ; des effets montrent chacun leur grande vue (un sélecteur en haut
  quand il y en a plusieurs) ; un périphérique dont l'interface a été dessinée dans l'onglet Interface
  ouvre sur elle. Les affichages suivent le son du plug-in (les taps de son processeur) ; le tempo est
  celui de l'hôte (effets et effets MIDI synchronisés). La fenêtre se redimensionne ; elle dessine à la
  cadence de l'écran quand un son passe, au ralenti sinon. Code : `src/plugin_ui/PluginInterface.*`
  (sur `ImGuiHost`, `DeviceBody`, `DeviceHost`, `DeviceSignals`), `GraphRuntimeEditor` dans
  `src/graph_plugin/GraphRuntimePlugin.cpp` ; les vues des périphériques passent par `DeviceHost`
  (le studio dans CyDAW, le graphe du plug-in une fois exporté). Test : `cydaw2_plugin_window_check`
  (`cydaw_graph_runtime_host … editor <image>` ouvre la fenêtre hors de l'écran et en écrit une image).

Code : `src/v2/WorkshopView.*` (le mode), `src/v2/Vst3Build.*` (la compilation),
`Studio::workshop*` (le document : graphe propre, lien à une piste, niveaux ouverts, bibliothèque),
`GraphEditor` en mode `workshop`.

## Mix DJ et Fonderie

La barre du haut a quatre modes : **Studio** (pistes, arrangement, lanceur et leurs panneaux), **Mix DJ**
(F10), **Fonderie** (F11) et **Atelier Plugin** (F7). Les trois derniers prennent toute la fenêtre ; les
panneaux du studio attendent tels quels. En Mix DJ et en Fonderie, la barre de transport devient la
leur (tempo maître et enregistrement du mix ; lecture, boucle et position du fichier), et les touches
Espace, Origine, L et R ne pilotent plus le studio. En Fonderie, Ctrl+S, Ctrl+O, Ctrl+N, Ctrl+Z et
Ctrl+Y travaillent sur ses sessions.

- [Mix DJ](MIX_DJ.md) : deux ou quatre platines, table de mixage, collection.
- [Fonderie](FONDERIE.md) : un éditeur de sons multipiste.

Leur son rejoint le mixeur du master comme celui des pistes (le simulateur de l'Atelier fait de même).
Le clic droit d'un clip audio ou d'un son du navigateur les ouvre directement.

## Ouvrir un projet

Ouvrir (Fichier, un modèle, un fichier déposé) ne fige plus l'interface : le fichier est lu et décodé sur
un fil à part, puis les pistes reviennent une à une entre les images. Pendant ce temps, la fenêtre montre
le logo, une barre, ce qui se charge (« Piste 77/170 : … »), le pourcentage, la taille et le temps ; le
reste attend. Port de contrôle : `open_project` avec `background: true` fait de même, et `status` dit où il en
est (`opening`) ; les autres commandes attendent l'ouverture. La ligne de commande ouvre tout de suite (ce qui
suit en a besoin). Code : `Studio::startOpeningProject / continueOpeningProject`, `project_io::DocumentLoad`
(les étapes), `AppUi::drawOpening`.

Le **logo** (`resources/branding`, depuis `output/imagegen/cydaw-logo-orange-fonce-3-points.png`, bord
nettoyé) est l'icône de l'exécutable, de sa fenêtre et de la barre des tâches (`ICON_BIG`/`ICON_SMALL` de
`juce_add_gui_app`), et s'affiche dans la barre de menus, À propos et l'ouverture d'un projet
(`ImGuiHost::texture`, `CyDAWBranding.h`).

## Signature rythmique

Le « 4/4 » de la barre de transport se change d'un clic : temps par mesure (1 à 32) et valeur du temps (2, 4, 8, 16), ou 3/4, 5/4, 6/8, 7/8, 12/8. Le tempo compte toujours des noires ; une mesure dure numérateur × 4 / dénominateur noires. La règle, la grille, le compteur (mesure.temps.double-croche), l'éditeur de clip, l'enregistrement et l'export MIDI la suivent ; elle est enregistrée dans le projet (`timeSignatureNumerator`, `timeSignatureDenominator` ; 4/4 pour les anciens). Port de contrôle : `set_tempo {bpm, numerator, denominator}`, `status` › `timeSignature`.

## Expressions par note et jeu legato

**Expressions** (comme dans Bitwig) : chaque note porte deux courbes dessinées sur elle, sa **hauteur**
(demi-tons, ±24) et son **volume** (dB, -48 à +12), d'un bout à l'autre de la note. Dans l'éditeur de
clip, les boutons **Notes / Hauteur / Volume** choisissent ce qu'on dessine : en Hauteur, la courbe passe
sur la note, un demi-ton par rangée (clic sur une note : un point ; glisser ; double-clic l'ôte ; Ctrl :
demi-tons entiers, Maj : fin) ; en Volume, les courbes sont dans la bande du bas. Clic droit (ou le bouton
des formes) : **formes** prêtes, ajoutées à ce que la note a déjà : scoop, glissé depuis le bas, vibrato
retardé ou large, chute, montée finale, souffle, decrescendo, accent, effacer. Le bouton **Humaniser**
déplace un peu la sélection (ou tout le clip) : placement (± ms), vélocité et longueur (± %), un tirage
nouveau à chaque clic (Ctrl+Z revient). En mode Notes, une courbe de hauteur reste visible en fin trait.

Moteur : une note qui a des courbes joue sur un canal MIDI à elle (2 à 16, attribués au plus libre),
où son pitch bend (14 bits, ±24 demi-tons) et son volume (CC 11 puis 43, 14 bits) la font bouger seule,
toutes les 64 échantillons quand ils changent ; le canal 1 reste celui du clavier (le pitch bend de
Prisme). Les voix (échantillonneur, modèle de son, oscillateurs, Prisme) suivent ces messages ; un plugin
VST3 instrument les reçoit tels quels (à la manière du MPE). Enregistré par note dans le projet (`pitch`,
`gain` : `[[position, valeur], …]`), gardé par les copies et duplications. Port de contrôle : `add_notes`,
`update_notes` et `list_notes` prennent et rendent `pitch` et `gain`.

**Jeu** (l'allocateur de voix de l'instrument, dans sa carte) : *Poly* (des accords), *Mono* (une note à la
fois : celle qui sonne s'efface en 30 ms, la suivante glisse depuis elle) ou *Legato* (une note à la fois,
qui glisse — et sans nouvelle attaque — seulement quand elle chevauche la précédente), et **Glide** (ms).
Les instruments d'usine qui ne jouent qu'une note à la fois (saxophones, flûtes, clarinette, hautbois,
basson, flûtes à bec, ocarina, cuivres, violon, violoncelle et contrebasse à l'archet, erhu, voix) sont en
Legato avec un glide court (`tools/factory_content/factory_voicing.py`, champ `voicing` du catalogue). Prisme
garde son propre réglage (GLOBAL).

## Édition audio

Le son d'un clip se travaille dans son éditeur : sélection et éditions destructives gardées dans un
historique de versions, prises multiples et comping (enregistrer sur un clip ajoute une prise ; en
boucle, une par tour), étirement par quatre algorithmes (vitesse, tonal à phases verrouillées, rythme
découpé aux attaques, texture), transposition sans changer la durée, repères d'étirement. Tout est dans
docs/EDITION_AUDIO.md (`src/v2/AudioEditorView.*`, moteur `TimeStretch.*`).

## Modulation d'une piste

Une piste audio a un graphe de modulation : par défaut un LFO dans sa sortie, dont la valeur bouge les
paramètres cochés dans l'Inspecteur (chacun de sa quantité), ceux de ses périphériques comme ceux de ses
plugins VST3 (épinglés). Le panneau Graphe › **Modulation** l'édite en nœuds : plusieurs LFO, macros,
calculs (addition, multiplication, min, max, puissance…), logique, plages (map range) ; la sortie
s'affiche dans la barre. L'Inspecteur règle le premier LFO directement. Le projet garde le graphe.
Port de contrôle : `track_modulation {track, lfo, assign}`, et les commandes de graphe avec
`"graph": "modulation"`.

## Effets de studio

Réverbe Pro, Compresseur Pro, Limiteur Pro et Filtre Pro (dans l'esprit de FabFilter Pro-R, Pro-C, Pro-L
et Volcano) : leur carte dans Périphériques, leur fenêtre complète (⤢), leurs préréglages d'usine. Tout est
dans docs/EFFETS_STUDIO.md (`src/v2/StudioEffectsView.*`, moteur `ProEffects.cpp`, calculs partagés
`ProEffectsDesign.h`).

## Prisme

Le synthé à tables d'ondes de CyDAW (Piste › Nouveau Prisme) : sa carte dans Périphériques, sa fenêtre (OSC, MATRICE, EFFETS, GLOBAL), la modulation par glisser-déposer. Tout est dans docs/PRISME.md (`src/v2/PrismView.*`, moteur `PrismSynth.*`, jeu `PlanSynthesiser.h`).

## Amplis et micros

Trois effets (catalogue › Effets › Amplis et micros) :

- **Ampli** : noise gate, pédale, préampli à lampes, tone stack passif calculé depuis ses composants,
  étage de puissance avec sag, présence et profondeur ; 8 modèles, suréchantillonné ;
- **Baffle et micro** : 8 enceintes, un ou deux micros placés à la souris sur le haut-parleur (position,
  distance, angle, type), leur mélange et leur phase, la pièce ;
- **Micro** : 12 modèles, des micros de studio au téléphone et au mégaphone, avec distance, angle,
  directivité et préampli.

Tout est dans docs/AMPLIS_ET_MICROS.md (`src/v2/StudioEffectsView.cpp`, moteurs `AmpDesign.*` et
`AmpEffects.cpp`). L'ampli a deux canaux, A et B (un modèle et des potards chacun, passage sans clic) ;
le baffle et le micro ont une pièce dont on règle la taille et la tonalité.

## Effets de mixage

Huit effets et deux suites natives inspirés d'outils de mixage connus :

- **Pression** (Dynamique) : compression vers le haut et vers le bas sur trois bandes, seuils tirés à la
  souris sur trois colonnes ;
- **Fournaise** (Effets › Distorsion) : saturation analogique en cinq caractères, niveau automatique ;
- **Écho Bande** (Effets › Échos et réverbération) : écho à bande à trois têtes en douze modes, ressort,
  pleurage, usure, synchro au tempo ;
- **Multibande Pro** (Dynamique) : quatre bandes, chacune compressée, expansée, relevée ou éclatée,
  coupures et seuils tirés sur le spectre ;
- **Velours** (Dynamique) : les résonances trouvées dans le spectre et calmées pendant qu'elles sonnent ;
- **Patine** (Effets › Couleur et usure) : bruit, ondulation, distorsion, usure numérique, petite pièce,
  bande magnétique ;
- **Sonomètre** (Analyse et affichage) : sonie intégrée, court terme, momentanée, LRA, crête vraie,
  historique, contre une cible ;
- **Tranche** (Effets › Suites) : gate, égaliseur, compresseur, transitoires, excitateur, largeur, et un
  assistant qui écoute la piste et propose l'égalisation et la compression ;
- **Finition** (Effets › Suites) : égaliseur, dynamique trois bandes, excitateur, image stéréo,
  maximiseur, et un assistant qui règle le gain pour atteindre la sonie visée ;
- **Halo** (Effets › Échos et réverbération) : réverbe de caractère, dix algorithmes (salles, plaque,
  pièce, chambre, espaces mouvants, sanctuaire, non linéaire) en trois époques.

Tout est dans docs/EFFETS_MIXAGE.md (`src/v2/MixEffectsPanes.inc`, moteurs `MixDesign.h` et
`MixEffects.cpp`).

## Effets créatifs, effets MIDI et utilitaires

Dix-neuf effets audio : **Vocodeur**, **Harmonie** (changeur de hauteur et harmoniseur), **Justesse**
(correction de justesse), **Dé-esseur**, **Pompe** (volume en rythme), **Empreinte** (réverbe à
convolution), **Givre** (gel et flou spectraux), **Grains** (écho granulaire), **Saccade** (répétitions
en rythme), **Boucleur**, **Décaleur** (décalage de fréquence), **Anneau** (modulation en anneau),
**Rotor** (cabine tournante), **Résonateurs**, **Corps**, **Peigne**, **Utilitaire** (polarité, mono,
largeur…), **Accordeur** et **Stéréoscope** (goniomètre, corrélation de phase). Huit effets MIDI, posés
avant l'instrument dans l'ordre où on les ajoute : **Arpège**, **Accords**, **Gamme**, **Écho de notes**,
**Aléa**, **Durée**, **Séquenceur** (seize pas) et **Roulement** (note repeat). Chacun a sa carte et sa
grande fenêtre ; tout est dans docs/EFFETS_CREATIFS.md (panneaux `src/v2/*Panes.inc`, moteurs
`src/engine/*Effects.cpp`, effets MIDI `MidiEffects.cpp` joués avant les voix par `MidiEffectsHost`).

Une étiquette trop longue pour la case de son bouton est écrite un peu plus petite (jusqu'aux deux tiers),
centrée sous le bouton sur toute la largeur de la case (`KnobStyle::labelWidth`).

## Kick et batteries électroniques

**Kick** (Piste › Nouveau Kick) est un instrument qui ne fait que des grosses caisses : un corps dont la
hauteur tombe, un clic, un sub, une saturation et un timbre. Sa fenêtre dessine le kick, avec des points
à glisser pour la hauteur et l'enveloppe ; 17 sons d'usine. **CyDAW Electro** est une banque de 93 sons
électroniques que CyDAW génère lui-même (rien à télécharger), avec 10 kits sous « Batteries
électroniques » (808, 909, techno, house, trap, lo-fi, hard dance, drum and bass, kicks, percussions et
effets). Tout est dans docs/KICK_ET_ELECTRO.md (`src/v2/KickView.*`, moteurs `KickSynth.*` et
`DrumSynth.*`, banque générée dans `SoundBanks.*`).

## Préréglages

Tout ce qui a des réglages a ses préréglages, et un bouton pour eux (l'icône de liste) :

**Préréglages d'usine des périphériques de base** : chacun des modules d'usine (Poly Keys, Poly Pad, Poly
Bass, Poly Pluck, Poly Lead, Poly Sine, Ambient Glass, Electric Organ, Percussive Bell, Stereo Finisher,
Warm Drive, Echo Space, Reverb Rack, Vocal Strip, Parallel Drive, Drum Punch, Wide Echo, Dark Room, Mono
Focus, Master Guard, Tremolo, Auto-Pan, Dynamic Filter, Arp Motion, Note Shaper, Octave Up, Bass Zone,
LFO…) et chacune des cartes simples (Compresseur, Limiteur, Porte, Transitoires, Drive, Écrêtage,
Bitcrusher, Délai, Réverbe, Ping-pong, Chorus, Flanger, Phaser, Trémolo, Passe-bas, Passe-haut, EQ, Bande
d'EQ, Filtre multimode, Filtre auto, EQ 3 bandes, Largeur stéréo, Table d'ondes, Corde, Bruit, LFO, Échelle
de vélocité, Transposition) a de 3 à 8 préréglages en anglais (`tools/factory_content/studio_presets/basics.py`,
écrits par `build_studio_presets.py` ; un module les range par les cartes de son intérieur,
« node.16002.eq.tone »).

- **une carte** de la chaîne d'une piste (dans son en-tête, Périphériques) : ses potards, et ce qui n'est
  pas un nombre — la carte d'un sampler, le modèle d'une carte Modèle de son, la table d'une wavetable.
  Charger un préréglage est une seule étape d'annulation ;
- **un plug-in hébergé** (dans son en-tête) : son état complet ; et en tête de la liste, **ses propres
  programmes** (ceux qu'il livre, comme un hôte les montre), un clic en choisit un ;
- **la chaîne d'une piste** (au pied de sa colonne de nom) : l'instrument et ses effets, tels quels ;
- **la fenêtre d'un périphérique** (un graphe ouvert en fenêtre) : une barre en haut, précédent, le nom
  du préréglage (la liste), suivant, enregistrer.

La liste : ceux de CyDAW, puis « les vôtres » ; précédent et suivant ; un nom et **Enregistrer** garde les
réglages actuels (le même nom les remplace) ; clic droit sur un préréglage : renommer, remplacer par les
réglages actuels, supprimer (à la corbeille), afficher dans le dossier. Le préréglage chargé en dernier est
rappelé sur son bouton.

Les fichiers : `<Préréglages>/<device|plugin|chain>/<type de carte, nom du plug-in ou rôle>/<nom>.cypreset`
(JSON `cydaw-preset` v1 : `kind`, `target`, `name`, `created`, `state`), le dossier choisi dans Options ›
Emplacements (Documents/CyDAW/Presets par défaut) ; ceux de CyDAW dans `Factory/Presets`, avec la même
forme. Les préréglages d'un périphérique de l'atelier, eux, sont dans le périphérique (onglet Préréglages,
plus haut) et suivent son export en VST3.

Le bouton **Préréglages** de la barre du graphe ouvre la même gestion dans le studio et l'atelier.
Dans un sous-graphe, il agit sur ce sous-graphe : ses presets restent indépendants de ceux du parent.
Un nom et **Ajouter** enregistrent les valeurs actuelles ; **Créer Init (valeurs par défaut)** garde
les valeurs par défaut déclarées des paramètres, sans changer le son actuel. Un clic charge le preset ;
le clic droit permet de remplacer, renommer, ordonner ou supprimer. Enregistrer ensuite le périphérique
conserve cette banque dans son `.cydawdevice`. Les presets ne changent ni les connexions ni l'interface.
L'export VST3 natif conserve également les valeurs des paramètres internes non publiés à l'hôte.


Code : `src/v2/Presets.*` (les fichiers), `src/v2/PresetMenu.*` (le bouton, la barre, la liste),
`Studio::*PresetHolder / *PresetState / apply*Preset / pluginPrograms`,
`NodeGraphComponent::editorPrograms` (les programmes d'un graphe), `GraphRuntimePlugin` (les programmes du
VST3 exporté), `IsolatedPluginHost::getPrograms / setProgram` (ceux d'un plug-in hébergé). Port de
contrôle : `presets` et `workshop` › `programs` ; MCP : `presets`. Vérifié par `cydaw2_presets_check`.

## Clavier de l'ordinateur et entrées MIDI

Comme dans Bitwig, **le clavier de l'ordinateur joue des notes** (activé par défaut ; Ctrl+K, bouton ⌨ de
la barre de transport, Options › Clavier de l'ordinateur) : la rangée du milieu donne les touches blanches,
celle du dessus les noires, **selon la place des touches** (AZERTY : Q S D F G H J K L M ù = Do…Fa ; QWERTY :
A S D F…), lue de la disposition Windows (`MapVirtualKeyEx`) ; les deux premières touches de la rangée du
bas changent d'octave (AZERTY W / X), les deux suivantes la vélocité (C / V). Il joue la piste instrument
choisie (ou les pistes armées), par l'état du clavier du moteur. Ses touches passent avant les raccourcis
d'une lettre (S, L…) tant qu'une piste instrument est choisie ; Ctrl et Alt gardent les leurs ; une saisie
de texte, le graphe et l'atelier le coupent (`ComputerKeyboard.*`). **Options › Entrées MIDI…** (ou clic
droit sur ⌨) : l'onglet « Entrées MIDI » des réglages audio, avec les claviers et contrôleurs MIDI branchés
(une case par appareil ; un appareil vu pour la première fois est allumé), le clavier de l'ordinateur (octave,
vélocité) et le dessin de ses touches. Port de contrôle : `computer_keyboard {enabled, octave, velocity}`
(`held` : les notes tenues), `midi_inputs {device, enabled}`.

## Thèmes

**Options › Thème** : *Gris moyen* (par défaut, proche de Bitwig), *Gris clair* (proche d'Ableton Live),
*Sombre (d'origine)*, et les thèmes à soi. **Thèmes et couleurs…** : la liste, et pour un thème à soi ses
29 couleurs par groupes (fonds, traits et grille, texte, accent et sélection, signaux et transport,
vu-mètres), qui changent l'interface pendant qu'on les règle ; « Créer à partir de celui-ci » copie
n'importe quel thème. Un thème à soi est un fichier JSON `.cytheme` (`"format": "CyDAW Theme"`, couleurs
`"#RRGGBB"` ou `"#RRGGBBAA"`) dans le dossier Thèmes des préréglages. Seules les couleurs changent
(`theme::setPalette`, `ThemesView.*`). Port de contrôle : `theme {key|name, create, from, colours}`.

## Importer un projet d'une autre application

Fichier › Importer, ou un .bwproject, .als, .dawproject, .song **glissé dans la fenêtre** : le projet ouvert
est d'abord proposé à l'enregistrement (comme pour en ouvrir un autre), puis l'import se fait **par étapes,
avec la barre de progression** de l'ouverture (« Importation de … » : lecture du projet sur un fil à part,
puis les pistes, puis leurs instruments et effets, quelques-unes par image ; `Studio::startImportingProject`,
`importStep`). Le rapport de ce qui n'a pas pu passer s'affiche à la fin. Port de contrôle :
`import_project {file, background: true}`.

**Plugins du projet importé** : chaque piste garde sa chaîne telle qu'elle était (`ImportedTrack::chain` :
nom, éditeur, format, plug-in ou périphérique de l'application, instrument, effet de notes, et les réglages
lisibles — ceux d'un périphérique Bitwig par leur identifiant, « F1FREQ », ceux d'un périphérique Live par leur
chemin XML). Un **plug-in** (VST, CLAP…) n'est plus remplacé par un instrument : sa place est gardée par une
carte **« Plugin manquant »** (ou « Instrument manquant ») comme dans Bitwig — un triangle d'avertissement, son
nom, son éditeur et son format (types `missing-plugin`, `missing-instrument` ; le son passe à travers un effet
manquant, un instrument manquant est muet). Choisir un instrument pour la piste (+, bibliothèque) prend la
place de l'instrument manquant, ses effets restent (`replaceInstrument`). `Studio::addMissingPlugin`.

**Instruments de l'application d'origine** : un instrument propre à Bitwig ou à Live devient celui de CyDAW
qui lui ressemble (`src/v2/ImportedInstruments.cpp`, `imported::equivalentOf`) :

| D'origine | Dans CyDAW |
|---|---|
| Bitwig Polysynth | Opale, **avec ses réglages** (oscillateurs, sub, synchro, unisson, mélange, filtre, enveloppes) |
| Bitwig FM-4 | Quartz |
| Bitwig Organ | Tirette |
| Bitwig Phase-4 | Onyx |
| Bitwig Polymer | Alliage |
| Bitwig E-Kick | Kick |
| Live Operator | Saphir |
| Live Analog | Argile |
| Live Drift | Dérive |
| Live Collision, Tension | Bronze |
| Live Wavetable | Prisme |
| Live Meld | Alliage |
| Simpler, Sampler (Bitwig) | l'échantillonneur simple |
| Sampler (Live) | le sampler |
| Drum Rack, Drum Machine, Impulse | Batterie |
| tout autre instrument natif | Opale |

Les réglages de Polysynth passent un à un (`Studio::setSynthSettings`) : octave en pieds, forme continue
(triangle → dent de scie → carré), sub et sa forme, synchro en demi-tons, voix et écart d'unisson, balance et
mode du mélange, saturation avant le filtre, fréquence du filtre (une note MIDI chez Bitwig), résonance,
profondeur d'enveloppe en demi-tons, suivi du clavier, les deux enveloppes en secondes. Pour les autres, seul
l'instrument est choisi pour l'instant (leurs réglages n'ont pas encore été relevés sur de vrais projets).

Une **nouvelle piste instrument** (menu Piste, bouton de l'arrangement) et les pistes d'un **fichier MIDI importé**
reçoivent aussi Opale (au lieu des Poly Keys, qui restent dans le catalogue et dans les modèles).

## Ajouter une piste

Le **+** après la dernière piste du lanceur, après la dernière tranche du mixeur, et en tête de la barre de
l'arrangement (« + Piste ») ouvre le menu des pistes (`src/v2/AddTrackMenu.cpp`) : piste instrument (Opale),
piste audio, piste bus, et « Piste instrument avec… » : chacun des treize synthés, Prisme, Kick, Batterie,
l'échantillonneur simple, le sampler. La piste faite est choisie. Le menu Piste garde aussi tout cela.

## Plusieurs fenêtres d'instrument

Options › **Plusieurs fenêtres d'instrument** (coché par défaut, gardé dans les réglages) : la fenêtre d'un
instrument (un synthé, Prisme, Kick, Batterie, le sampler) s'ouvre à côté de celles déjà ouvertes, une par
instrument, chacune un peu plus bas et à droite de la précédente, chacune avec sa place, sa taille et son
bouton pour sortir de la fenêtre du studio ; décoché, elle remplace la dernière. Chaque fenêtre du sampler
garde sa zone en main et son édition en cours. `EditorWindows.h` (une liste de fenêtres par sorte
d'instrument, un identifiant par instrument), `UiState::manyInstrumentWindows`.

## L'échantillonneur simple

Instruments › **Échantillonneur simple** (`simple-sampler-instrument`) : un seul son joué sur **tout le
clavier**, autour de sa note (celle de son nom, ou sa hauteur entendue ; do 4 pour un son sans hauteur, qui
est alors joué à toutes les hauteurs comme un échantillon). Sa carte montre **la forme d'onde** avec ses repères
à glisser (début en vert, fin en rouge, boucle), la case Boucle, la durée, la **racine** à glisser ou taper et
le nom du fichier ; « Remplacer… » ou un son glissé dessus (navigateur, explorateur) **remplace** le son (au
lieu d'ajouter une zone) ; « Éditer… » ouvre la fenêtre du sampler. Ses boutons sont ceux du sampler
(transposition, accord fin, début, vélocité, boucle, suivi du clavier, à l'envers, jusqu'au bout, largeur),
suivis de son enveloppe et de son filtre de voix. C'est une carte sampler avec le réglage caché `sampler.simple`
(`SamplerSetting::simple`) ; `app::simpleSamplerMap`, `SamplerView::drawSimplePane`. Port de contrôle :
`load_sampler {track, file: un son}` sur une piste qui en a un.

**Nuée** (Piste › Nouvelle piste › Nuée, `nuee-instrument`) est l'échantillonneur simple en mode
granulaire, suivi de Halo : le son chargé n'est plus joué d'un bout à l'autre mais lu en grains, à la
hauteur de la touche. Sa carte ajoute les réglages des grains : position dans le son, balayage (vitesse et
sens de la lecture, 0 : figé), taille (10 ms à 1 s), densité (1 à 120 grains par seconde), dispersion
autour de la position, hauteur aléatoire, largeur stéréo, chance à l'envers et forme des grains
(`SamplerSetting::grains` et suivants).

## Rack de pads

Piste › Nouvelle piste › **Rack de pads** (`rack-instrument`) : seize pads, quatre par quatre, le plus grave
(do 2, note 36) en bas à gauche. Chaque pad joue **n'importe quel instrument avec ses propres effets** quand
sa touche arrive ; le rack neuf contient un kit (grosse caisse Kick, caisse claire et clap Silex,
charlestons fermé et ouvert Silex dans le même groupe de coupure).

- Un clic choisit un pad : à droite, son nom, sa touche, « Ouvrir sa chaîne », son volume, son
  panoramique, muet, solo, son groupe de coupure (les pads d'un même groupe se coupent) et la note que son
  instrument reçoit (sa touche, ou une autre : do 4 pour un instrument accordé).
- Un double-clic (ou « Ouvrir sa chaîne ») montre dans Périphériques **la chaîne du pad** : son instrument
  et ses effets, qu'on modifie comme ceux d'une piste ; la flèche ◀ en haut de la colonne revient au rack.
- Clic droit : son instrument (Kick, Silex, les deux échantillonneurs, Nuée, Prisme ou l'un des synthés),
  charger un son, muet, solo, retirer. Un son
  glissé sur un pad (navigateur, explorateur) en fait un pad d'échantillonneur simple.
- Le pad s'allume quand il sonne. Les pads se jouent après les voix de la piste et avant ses effets ;
  chacun a ses douze voix et son propre graphe (`MidiSynthEngine::RackPad`).
- Projet : les pads sont enregistrés avec la piste (`rackPads`). Port de contrôle : `rack {track, action:
  list|add|instrument|mix|sample|remove|open|close, note, pad, …}` (`src/v2/RackView.*`, `Studio::addRackPad`
  et suivants).

## Changer d'instrument

Un instrument choisi pour une piste instrument **remplace son instrument** au lieu de créer une piste :
depuis le + de la chaîne (la bibliothèque s'ouvre pour cette piste), la bibliothèque (F9) ou le Navigateur
quand une piste instrument est choisie (« Nouvelle piste » reste au menu et dans le volet de la
bibliothèque). Seule la section de voix est échangée (allocateur, source, enveloppe, gain, filtre de voix,
et ce que le préréglage met après son filtre, comme le halo de Nébuleuse) : **les effets après lui, leur
automation et les câbles de modulation restent** (`NodeGraphComponent::replaceInstrument`,
`Studio::replaceInstrument`). Une piste qui portait le nom de son instrument prend celui du nouveau. Port de
contrôle : `insert_device` d'un instrument sur une piste instrument le remplace ; `add_factory_instrument
{key, track}` le met sur cette piste. La liste des instruments d'usine se lit en arrière-plan au démarrage
(26 Mo de cartes, `SoundBanks::preloadMaps`) : l'onglet Instruments s'ouvre sans attendre.

## Affichages des périphériques

Chaque périphérique montre ce qu'il fait, à partir des paramètres qu'il publie (donc aussi pour un
module fait de ces éléments) ; la bibliothèque est `src/v2/DeviceVisuals.*`, réutilisée par
l'Inspecteur pour le LFO de piste :

| Affichage | Contenu | Geste |
|---|---|---|
| Filtre (passe-bas, passe-haut) | Courbe de réponse sur le spectre avant (gris) et après (couleur) | Glisser le point : fréquence et résonance ; molette : résonance |
| Égaliseur | Courbe des bandes et du ton sur les spectres | Glisser une bande ; molette : Q |
| Compression | Courbe d'entrée/sortie, niveau d'entrée en direct, historique du niveau et de la réduction de gain | Glisser : seuil (horizontal), ratio (vertical) ; molette : ratio |
| Saturation, écrêtage | Courbe de transfert, forme d'onde avant et après | Glisser verticalement |
| Enveloppe | ADSR | Glisser les trois coins |
| Oscillateur, table d'ondes | Forme d'onde, tables en profondeur | Clic : forme suivante ; glisser : position |
| LFO | Forme, profondeur, décalage, position en direct | Glisser : vitesse et profondeur ; clic : forme |
| Écho, réverbération, stéréo | Échos dans le temps (valeur de note au tempo), décroissance, largeur | Glisser |
| Égaliseur Pro | Courbe de chaque bande et leur somme sur les spectres avant et après, poignées numérotées | Voir ci-dessous |
| Compresseur Pro | Niveaux d'entrée et de sortie et réduction de gain sur cinq secondes, vumètre de réduction, courbe de transfert avec le genou | Historique : glisser le seuil, molette pour le ratio ; courbe : seuil (côté), ratio (haut/bas), molette pour le genou |
| Autre périphérique | Spectres avant et après | — |

**Carte d'un périphérique sans vue à lui** (effets, dynamique, filtres, analyseurs, modules faits d'un
graphe : `DevicesView` `cardFace`) : chaque affichage en petit (toujours manipulable, ⤢ l'ouvre en grand),
avec **ses vrais boutons dessous** (un bouton avec son nom et sa valeur, un interrupteur, une liste :
`visual::compactControl`) — ses réglages principaux (`visual::cardSlots` : la bande en main d'un égaliseur,
les réglages principaux d'un effet studio) —, puis ses autres réglages en colonnes ; chaque partie aussi
étroite que ses boutons le permettent à la hauteur du volet. Le spectre du son est laissé à la fenêtre
quand la carte montre autre chose. L'oscilloscope dessine l'onde du son, le vumètre ses niveaux.

**Grande fenêtre d'un périphérique** (boutons œil et ⧉ de sa carte, ⤢ d'un affichage) : un périphérique
à **un seul** affichage l'ouvre directement en grand avec **tous ses réglages** en grands boutons (styles,
options, potards ; `visual::drawLargePane`). Pour un périphérique à plusieurs affichages (Poly Keys, Stereo
Finisher, Warm Drive…), l'onglet **TOUT** (par défaut) montre ses affichages en grand, sur autant de rangées
qu'il faut, avec leurs niveaux, et dessous **tous ses réglages en grands boutons**, groupés par affichage
puis « Autres réglages » (`visual::controlGroups`) ; un onglet par affichage le montre seul, en grand
(`visual::drawOverview`). Un module qui a une interface dessinée dans l'atelier la garde dans un
onglet **INTERFACE** de la même fenêtre (avant, l'œil ouvrait cette seule interface, générée et bien plus
pauvre que la carte) ; un module sans aucun affichage reconnu ouvre toujours la fenêtre de son interface.

Dans Périphériques, la molette fait défiler la chaîne de côté quand rien sous le pointeur ne la prend
(afficheurs et potards gardent la leur) ; ✕ retire un périphérique en reliant ce qui l'alimentait à ce
qu'il alimentait, de sorte que le son continue (une seule étape d'annulation).

Sous chaque affichage, ses boutons (glisser, Maj pour le réglage fin, double-clic pour la valeur par
défaut, clic sur la valeur pour la taper, clic droit pour l'automatiser). Le bouton ⤢ après le titre d'un affichage (dans Périphériques comme sur les cartes
du graphe) l'ouvre en grand dans une fenêtre « Périphérique » à lui. On peut en ouvrir plusieurs à la
fois, comme des fenêtres de VST. Elle s'ancre ou flotte comme les panneaux, et montre un onglet par
affichage quand le périphérique en a plusieurs.

Son en-tête porte les préréglages du périphérique et le bouton **fenêtre séparée** (⧉). Il sort la
fenêtre de celle du studio, dans une vraie fenêtre Windows que l'on pose sur un autre écran ; un second
clic la remet dans le studio.

La grande vue d'un effet sans fenêtre à lui (chorus, écho, filtres, trémolo, gate, saturation, réverbe,
compresseur…) se compose ainsi :

- **en haut**, son nom, puis ses valeurs écrites en grand ;
- **au milieu**, l'affichage, avec en dessous le spectre avant et après et, à côté, les niveaux d'entrée
  et de sortie ;
- **en bas**, une façade à sa couleur :
  - les choix sous forme de boutons nommés (mode du filtre, forme du trémolo), ou d'une liste quand ils
    sont nombreux (la synchro) ;
  - les interrupteurs ;
  - les potards, en grand ;
  - ses autres réglages dans une seconde section.

Les fenêtres de Prisme, du Kick, du sampler et des interfaces dessinées dans l'atelier ont le même
bouton ⧉ dans leur en-tête. Sur chaque carte de Périphériques, le bouton ⧉ de l'en-tête ouvre
directement la fenêtre du périphérique hors du studio. Par le port de contrôle et le MCP :
`set_view {device, pane, detached}`. Le spectre et les niveaux n'y viennent que pour la piste
sélectionnée. Le signal vient de prises que le moteur garde à l'entrée et à la sortie de chaque
périphérique affiché (`NativeDspRuntime::setTaps`, sans verrou ni allocation dans le thread audio).

## Liste des valeurs d'une carte

Le bouton liste de l'en-tête de chaque carte (instrument, effet, périphérique de l'atelier) ouvre une colonne à droite de la carte avec **toutes ses valeurs**, y compris celles que sa face ne montre pas (les contrôles d'un SFZ, les réglages d'un synthé) :
- chaque valeur a un petit potard, son nom et sa valeur brute avec son unité ; sous le pointeur, sa clé (`node.3.opale.osc1.level`) et sa valeur exacte ;
- **la dernière valeur touchée vient en haut**, surlignée, au-dessus des précédentes, puis un trait et les autres dans l'ordre de la carte. Une valeur est « touchée » quand elle change par un potard, où qu'il soit (carte, grande fenêtre, éditeur de graphe, interface dessinée) ou par un contrôleur ; cela fait le lien entre ce qu'on tourne et son nom ;
- la recherche garde les valeurs dont le nom ou la clé contient les mots tapés ;
- clic droit : le menu du paramètre (l'automatiser dans l'arrangement, sa valeur par défaut) ; en mode Apprendre (Ctrl+M), chaque potard de la liste s'assigne à un contrôleur.

La liste reste ouverte par carte pendant la session. Port de contrôle : `value_list {track, nodeId, open, filter}` l'ouvre ou la ferme et rend ses valeurs dans l'ordre affiché (`binding`, `label`, `value`, `unit`, `touched`). Test : `tests/v2_value_list_check.py` (ctest `cydaw2_value_list_check`).

## Modulateurs d'une carte

Le bouton **modulateurs** de l'en-tête d'une carte ouvre une colonne à sa gauche : **+** ajoute un LFO, des Steps, un Random, un ADSR, un Keytrack, des Expressions, une Macro, un Button, un Math ou un Envelope Follower. La flèche d'un modulateur se glisse sur n'importe quel potard de la piste. Cliquée, elle s'arme : tirer un potard règle alors la quantité (Échap désarme). Un anneau de la couleur du modulateur entoure chaque potard modulé, et la liste des valeurs montre les modulations. Détails : [MODULATEURS.md](MODULATEURS.md). Port de contrôle : `modulator {track, action, …}`. Test : `tests/v2_modulators_check.py` (ctest `cydaw2_modulators_check`).

## Allumer, éteindre, tourner sans claquer

**Changer l'ordre** : on prend un effet par son nom (en tête de sa carte) et on le lâche sur une autre
carte : sur sa moitié gauche, il passe avant ; sur sa moitié droite, après (sur le « + », en dernier).
Une ligne montre où il va. La chaîne est recâblée (ce qui l'alimentait alimente ce qui le suivait, puis
il s'insère à sa nouvelle place ; son entrée sidechain et les câbles de contrôle sur ses boutons
restent) et les cartes se rangent dans l'ordre du signal. L'instrument reste en tête. Un pas
d'annulation. Moteur : `NodeGraphComponent::editorMoveInChain` ; port de contrôle : `move_device`
(`track`, `nodeId`, `before` : la carte devant laquelle il va, 0 pour la fin).

**Vumètre de sortie** : chaque carte de Périphériques (instrument, effet, plug-in VST3, même repliée) a
un vumètre le long de son bord droit : ce qui sort de ce périphérique, gauche et droite, pour voir d'un
coup d'œil où le son s'arrête dans la chaîne. Un instrument montre ce que ses voix donnent. Moteur :
chaque nœud du plan garde sa crête depuis la dernière lecture (`AudioRenderPlan::takePeaks`), les
plug-ins la leur (`GainPanAudioSource::takePluginPeaks`) ; `DeviceSignals::outputLevel` la fait
retomber comme un vumètre (24 dB par seconde). La fenêtre d'un instrument s'ouvre par le bouton de
l'en-tête de sa carte (le bouton « Ouvrir … » de la carte faisait doublon).

**Bouton marche/arrêt** : chaque périphérique qui fait ou transporte du son a, en tête de sa carte
(Périphériques, même repliée), à gauche de son nom, un bouton allumé ou éteint. Éteint (bypass), un effet laisse passer
ce qui lui arrive, comme s'il n'était pas là, et sa carte se grise ; un instrument éteint se tait.
Le moteur le fait tourner quand même : rallumé, il repart sans trou (une réverbe a déjà sa queue). Le
passage se fait en fondu de 10 ms, et un effet qui a une latence (une anticipation, un
suréchantillonnage) la garde quand il est éteint : ce qui passe arrive au même moment, la
compensation du mix ne bouge pas. L'état est enregistré avec le projet et a son pas d'annulation.
Un plug-in VST3 qui a son propre interrupteur « Bypass » a le même bouton, qui le commande (le plug-in
passe le son lui-même, latence et réglages gardés). Moteur : `NativeDspGraph::bypasses` (la carte
éteinte : le nœud dont la sortie est remplacée, celui dont l'entrée passe ; 0 : le silence),
`AudioRenderPlan::setBypasses` (sans recompiler), `NodeGraphComponent::editorSetBypassed`.

**Tourner un bouton** : pendant qu'on le tourne, le pointeur disparaît et reste où on a cliqué (il ne
bute jamais sur un bord d'écran) ; il réapparaît au même endroit quand on lâche. Un réglage à crans
(nombre de voix, octave) cumule les petits mouvements jusqu'au cran suivant ; la molette avance d'un
cran. Un bouton grisé dit dans sa bulle ce qui le ferait agir (« agit quand Unisson vaut 2 ou plus »).
`ui::heldMovement`, `KnobStyle::steps` ; le pointeur piloté par le port de contrôle (tests) garde
l'ancien comportement.

**Boutons tournés sans claquer** : un réglage d'effet ou d'instrument change à chaque image quand on
tourne un bouton ; le moteur fait glisser ce qu'il entend vers la nouvelle valeur en quelques
millisecondes, au lieu d'un saut au début du bloc suivant. Pendant ce glissement, un effet traite son
bloc par morceaux de 32 échantillons (`AudioProcessor::valuesGliding` / `glideValues`, que
`EffectProcessor` fait pour tout le catalogue) ; un choix (type, mode, pente, synchro…), un
interrupteur, une anticipation ou un suréchantillonnage, eux, changent d'un coup. Un instrument fait
de même avec les valeurs de ses voix (`PlanGlide` dans `AudioTrackController.cpp`), et son volume et
son maintien suivent à l'échantillon près. Une modulation de piste n'est plus réinitialisée un instant
quand on touche un bouton de la même piste.

## Effets

Quatorze effets s'ajoutent au catalogue (Périphériques « + », recherche du graphe), chacun avec son
affichage (et un analyseur, le spectromètre, plus bas) :

| Effet | Paramètres |
|---|---|
| Chorus | vitesse, profondeur, retard, largeur, mixage |
| Flanger | vitesse, profondeur, retard, réinjection (±), mixage |
| Phaser | vitesse, profondeur, fréquence, réinjection, mixage |
| Trémolo / auto-pan | vitesse, profondeur, forme, étalement (0 trémolo, 1 auto-pan), synchro |
| Bitcrusher | bits, sous-échantillonnage, saturation, mixage |
| Filtre multimode | mode (passe-bas, passe-haut, passe-bande, réjecteur), coupure, résonance, saturation |
| Filtre auto | coupure, résonance, vitesse et profondeur du LFO (jusqu'à ±4 octaves), synchro |
| EQ 3 bandes | graves, médiums, aigus, fréquences de séparation basse et haute |
| Égaliseur Pro | 8 bandes (forme, fréquence, gain, Q, pente, active ; dynamique, plage, seuil, attaque, relâchement, détection) et sortie |
| Compresseur Pro | seuil, ratio, genou, attaque, relâchement, compensation, mélange, filtre du détecteur |
| Limiteur | gain, plafond, relâchement |
| Gate | seuil, attaque, relâchement, plage |
| Transitoires | attaque, maintien, sortie |
| Écho ping-pong | temps, réinjection, mixage, largeur, synchro |

Le **Spectromètre** (Périphériques « + » › Analyseurs › Audio, ou le nœud `spectrometer`) laisse passer
le son intact et l'analyse : une grande courbe de spectre (20 Hz – 20 kHz, 256 bandes, échelle en dB
jusqu'au plancher choisi, pente par octave autour de 1 kHz), ses crêtes maintenues puis relâchées, et
une **cascade** dessous (le spectre dans le temps, le plus récent en haut, du bleu nuit au jaune). Sous
le pointeur : fréquence, note et niveau ; double-clic : les crêtes repartent. Réglages : plancher,
pente / octave, lissage, maintien des crêtes (10 s : pour toujours), durée de la cascade. Moteur :
`NativeDspNodeKind::spectrometer` (copie de l'entrée), entrée du catalogue d'effets
(`spectrometer.floor`…) ; affichage : `Spectrometer` (`SignalAnalysis.*`), `PaneKind::spectrometer`
(`DeviceVisuals.cpp`).

L'**Égaliseur Pro**, à la manière de Pro-Q : huit bandes, chacune cloche, plateau bas ou haut,
coupe-bas ou coupe-haut (12, 24, 36 ou 48 dB/oct, le Q règle la résonance du coude), réjecteur,
passe-bande ou inclinaison. Sur la courbe : double-clic dans le vide ajoute une bande (coupe-bas tout
à gauche, coupe-haut tout à droite, cloche ailleurs), glisser une poignée règle fréquence et gain (pour
une coupe ou un réjecteur, la hauteur règle le Q), Maj pour le réglage fin, molette sur une poignée
pour le Q, double-clic sur une poignée la retire, clic droit pour la forme et la pente (ou, dans le
vide, ajouter une bande d'une forme donnée). L'info-bulle donne la note la plus proche (« A4 +12 ct »).
**Écouter une bande seule** : un casque s'affiche au-dessus de la bande choisie ; tant qu'on maintient le
clic dessus, on n'entend que ce que cette bande travaille (un passe-bande à sa fréquence et à son Q,
en fondu) ; Alt + clic maintenu sur une poignée fait de même, et la fenêtre en grand a un bouton casque
à côté d'« Active » (`eqpro.listen` : 0 aucune, n la bande n). **Pendant l'écoute**, glisser à gauche
ou à droite déplace la bande (on cherche la fréquence à l'oreille, Maj pour le fin) et la molette la
resserre ou l'élargit (son Q) ; l'info-bulle suit la fréquence, la note et le Q. Le double-clic suit le
réglage de Windows (sa durée et sa distance), plus tolérant que celui d'ImGui.

**Bandes dynamiques** (comme Pro-Q 3) : une cloche, un plateau ou une inclinaison peut devenir
dynamique (clic droit sur sa poignée › « Bande dynamique », ou le bouton « Dynamique » de la fenêtre en
grand). Son gain bouge alors avec le son : un détecteur écoute **sa bande** (un passe-bande à sa
fréquence et à son Q pour une cloche, un passe-bas sous un plateau bas, un passe-haut au-dessus d'un
plateau haut, tout le son pour une inclinaison) ou, au choix, la même bande du **sidechain** (choisi dans
l'en-tête de la carte, comme pour le Compresseur Pro ; sans sidechain, la bande elle-même). Au-dessus
du **seuil**, le gain part vers sa **plage** (trois quarts de dB par dB dépassé, genou doux de 6 dB) sans
jamais la dépasser : une plage négative creuse la bande quand elle est trop forte (sifflantes, boue,
résonances), une plage positive la fait ressortir quand elle frappe (le clic d'un kick). **Attaque** et
**relâchement** règlent la vitesse. Sur la courbe : la zone que la plage peut balayer est ombrée, un
**losange** marque où elle mène (glisser : la plage ; double-clic : la bande redevient statique), et
pendant la lecture la courbe et un trait épais montrent où le gain est **maintenant** (ce que le moteur
fait vraiment, lu sur le processeur). La fenêtre en grand a une seconde rangée : Dynamique, « Sa bande »
/ « Sidechain », Plage, Seuil, Attaque, Relâchement, un vumètre de ce que le détecteur entend avec le
trait du seuil, et **Seuil auto** (pendant la lecture : le seuil se place 6 dB sous les crêtes que la bande a
eues ces dernières secondes). Sur la carte, une bande dynamique choisie ajoute ses champs Plage et
Seuil. Valeurs : `eqpro.b<n>Dynamic`, `Range` (−30 à +30 dB), `Threshold`, `Attack`, `Release`,
`Detector` (0 sa bande, 1 sidechain), rangées après la sortie et l'écoute ; moteur `ParametricEq`
(`EffectProcessors.cpp`), lecture de ce qu'il fait : `AudioProcessor::readings` →
`AudioRenderPlan::readings` → `NativeDspRuntime::nodeReadings` → `DeviceSignals` (`Live::readings`).
31 préréglages d'usine, en anglais (« Softened Sibilance (De-esser) », « Bass Making Room for the Kick
(Sidechain) », « Backing Vocals Behind the Lead (Sidechain) », « Kick Click Forward »…).
En grand : pastilles des huit bandes (clic choisit, double-clic active ou coupe), formes dessinées,
pentes, potards fréquence / gain / Q avec leur valeur, sortie, échelle ±3 / ±6 / ±12 / ±30 dB,
lecture de la fréquence, de la note et du niveau sous le pointeur. Courbes et son viennent du même
calcul (`include/cydaw/engine/BiquadDesign.h`) : `--effects-test` vérifie que des sinus ressortent au
gain que la courbe dessine (écart mesuré : 0,003 dB, tolérance du test : 0,35 dB). Les valeurs des bandes vivent dans `eqBands` (48 valeurs
pour les bandes, puis 48 pour leur dynamique ; le tableau en a 96) du nœud ; un déplacement de
fréquence, gain ou Q glisse en quelques millisecondes pour ne pas claquer.
La modulation de piste ne propose que la fréquence, le gain et le Q des bandes actives.

Le **Compresseur Pro** calcule son gain à la manière des compresseurs de studio (genou progressif,
attaque et relâchement sur le gain, détection sur le côté le plus fort pour garder l'image stéréo) ;
son détecteur peut être passé au filtre passe-haut (au-dessus de 20 Hz) pour que les basses ne le
fassent pas pomper, et le mélange permet la compression parallèle. `--effects-test` vérifie qu'un
sinus 12 dB au-dessus du seuil ressort à 4:1 à la valeur de la courbe (écart mesuré 0,17 dB), que la
compensation s'ajoute et qu'à mélange nul le son est intact. La réduction affichée est mesurée sur
les prises d'entrée et de sortie, compensation retirée.

« Synchro » : Libre, ou une valeur de note (1/32 à 1/1, triolets et pointées) au tempo du projet ;
sur l'affichage de l'écho, glisser horizontalement passe d'une valeur à la suivante.

Un câble de contrôle (LFO, map…) branché sur un paramètre d'une voix de synthé (coupure ou résonance
du filtre de voix, position, niveau ou désaccord de la table d'ondes, amortissement de la corde)
module maintenant le son bloc par bloc ; avant, il était ignoré. Les potards et les courbes modulés
montrent leur position en direct en violet, dans le graphe comme dans Périphériques.

Ils sont décrits une seule fois dans `include/cydaw/engine/EffectCatalogue.h` (clé, processeur,
paramètres, plages, libellés, catégorie, couleur) et leur DSP est dans `src/engine/EffectProcessors.cpp` :
toutes les tables du moteur, du graphe, de l'éditeur v1, de l'export VST3 et de la modulation de piste
lisent ce catalogue. Leurs valeurs vivent dans les champs `effect1`…`effect8` des nœuds (ou dans
`eqBands` pour l'égaliseur), enregistrés dans les .cydaw ; seules celles d'un champ nommé ont une
entrée de modulation sur la carte. `--effects-test` (ctest `cydaw2_effects`) vérifie que chacun compile, que ses potards
atteignent le moteur et qu'il fait ce qu'il dit (le limiteur tient son plafond, l'EQ à plat est
transparent, la gate se ferme…).

## Gestes

- **Transport** : Espace lecture/pause, R enregistrer (piste armée ou sélectionnée), L boucle,
  Origine retour au début, Ctrl+Z / Ctrl+Y dans le panneau actif.
- **Arrangement** : clic sélectionne, glisser déplace (aimanté à la grille, Alt pour libérer), bords
  pour rogner, glisser vers une autre piste du même type pour changer de piste, S scinde à la tête de
  lecture, Ctrl+D duplique, Suppr supprime, rectangle de sélection dans le vide, double-clic sur une
  piste instrument vide crée un clip, double-clic sur un clip l'ouvre dans l'éditeur. Molette défile
  les pistes (sur les clips comme sur les en-têtes ; une barre verticale à droite), Maj+molette en
  horizontal, Ctrl+molette zoome autour du pointeur, bouton du milieu déplace la vue.
  À droite des pistes, la **bande des hauteurs** (comme celle du temps, mais debout) : glisser vers le
  haut et le bas fait défiler les pistes, vers la gauche les agrandit (les clips se voient en plus gros),
  vers la droite les réduit ; double-clic : taille normale.
  Règle en deux bandes : en haut, la **bande à saisir** (glisser à gauche et à droite se déplace dans le
  morceau, vers le bas zoome, vers le haut dézoome, autour du point saisi ; double-clic : tout voir) ;
  dessous, les temps : clic place la tête de lecture, glisser dans leur haut (ou Maj) trace la boucle.
  Se déplacer pendant la lecture met en pause le suivi de la tête (le bouton Suivre s'éteint) jusqu'à la
  lecture suivante ou un clic sur ce bouton.
  Fichiers audio déposés depuis l'Explorateur ou le Navigateur : sur une piste audio au temps visé,
  ou sur une nouvelle piste.
- **Graphe** : Tab, double-clic ou clic droit dans le vide ouvrent la recherche de nœud au pointeur
  (clavier : flèches + Entrée ; la description du nœud surligné s'affiche en bas). Chaque carte montre
  le même affichage que dans Périphériques (courbe de filtre sur le spectre, compresseur, enveloppe…),
  modifiable directement, puis ses potards. Les entrées de modulation non branchées se cachent ; elles
  apparaissent sur la carte sélectionnée et pendant qu'on tire un câble de contrôle (pointillés jaunes).
  Pendant la lecture, des points circulent sur les câbles audio. Glisser une carte libre sur un câble
  audio l'insère dans la chaîne (le câble visé s'éclaire). Le zoom ne descend pas sous 72 % au cadrage :
  un grand graphe commence à ses entrées et se parcourt avec la mini-carte. Tirer un câble depuis une sortie et le lâcher dans le vide ouvre la
  recherche filtrée sur les nœuds qui acceptent ce signal, et le nouveau nœud est câblé à
  l'arrivée. Attraper une entrée déjà reliée reprend son câble. Alt+clic coupe un câble, clic le
  sélectionne (Suppr). Potards directement sur les cartes.
  **Prises des boutons** : un périphérique fait d'un graphe (module, périphérique de l'atelier) a une
  entrée de contrôle par bouton qu'il montre (`knob.<clé>` ; cachée tant que rien ne la branche, comme
  les autres entrées de valeur). Un câble de contrôle sur un bouton a deux modes, au **clic droit sur
  le câble** : *Régler la valeur* (le bouton prend la valeur du câble) ou *Moduler autour de la
  valeur* (le bouton garde sa valeur, le câble le fait bouger autour, de « Profondeur » × sa course de
  chaque côté, négative pour inverser ; sur une échelle logarithmique pour une fréquence). Un LFO posé
  sur un bouton module à 25 % ; une valeur, une macro le règlent. Un badge au milieu du câble dit le
  mode (vague : moduler, = : régler) ; l'anneau du bouton montre où le câble le met. Le moteur part de
  la valeur du bouton à chaque bloc (`NativeDspRuntime::applyParameters`), dans les deux compilateurs
  (graphe typé et chaîne d'instrument) et pour les voix d'un synthé (`modulatePolyVoice`). F cadre, A range par flux du signal,
  Ctrl+D duplique, double-clic sur le titre renomme, clic droit sur une carte pour son menu.
  Molette zoome autour du pointeur ; glisser dans le vide (bouton gauche ou droit), le bouton du milieu
  ou Espace+glisser déplace la vue, Ctrl ou Maj+glisser trace un rectangle de sélection ; mini-carte
  quand tout ne tient pas à l'écran. Un graphe hérité de la v1 trop étalé est rangé à son premier
  affichage (Ctrl+Z le rétablit).
- **Automation** : bouton A dans l'en-tête de la piste (ou menu de la piste). Le nom du couloir, en
  haut à gauche, choisit ce qu'il montre : le volume, n'importe quel paramètre d'un périphérique de
  la chaîne, un paramètre des voix d'un synthé (filtre, table d'ondes, corde : le moteur des voix le
  lit à chaque bloc, avant les câbles) ou un paramètre d'un plugin VST3 de la piste (0 à 1 ; les
  paramètres déjà automatisés sont listés d'abord) ; dans Périphériques, un clic droit
  sur un potard ou un champ propose « Automatiser dans l'arrangement ». Clic dans le couloir ajoute un
  point, glisser le déplace (aimanté à la grille, Alt pour libérer), double-clic le supprime,
  Alt+glisser sur un segment le courbe, clic droit pour le menu (segment droit ou courbe, effacer,
  choisir le paramètre, masquer). Ctrl+Z / Ctrl+Y annulent aussi ces modifications. Le moteur lit les
  courbes des paramètres au début de chaque bloc du graphe de la piste, dans le fil audio
  (`NativeDspRuntime::setAutomation`) ; fréquences et durées glissent sur leur échelle logarithmique.
  Les courbes sont enregistrées dans le .cydaw (`parameterAutomation`, `shownAutomation` de chaque
  piste ; CyDAW 1 les ignore). Un réglage tourné à côté d'un paramètre modulé ou automatisé tient
  désormais (avant, chaque bloc lui rendait sa valeur de compilation).
- **Modulation** (Inspecteur, pistes audio) : un LFO (vitesse, profondeur, décalage, forme) déplace les
  paramètres publiés du graphe de la piste ; cocher un paramètre l'active, la glissière règle la
  quantité. Un potard modulé montre sa position réelle par un point violet dans Périphériques.
- **Piano roll** : la hauteur se lit d'un coup d'œil — chaque touche blanche porte son nom quand il y a la
  place (chaque Do en gras), une ligne marquée et le nom de l'octave à chaque Do au bord gauche des notes, la
  rangée sous le pointeur éclairée (et sa touche nommée, noire comprise), les touches des notes
  sélectionnées marquées, le nom dans chaque note, et celui de la note qu'on déplace à côté du pointeur. La
  molette fait défiler les notes même pendant une sélection au rectangle ou un déplacement, et tirer vers
  un bord fait défiler la vue (le rectangle garde son point de départ). La tête de lecture suit le clip là où il joue — dans le lanceur, depuis son début (un
  stop puis une lecture le font repartir du début, la tête aussi), dans l'arrangement, à la position du
  morceau. Double-clic ajoute ou supprime une note, glisser déplace (temps et hauteur),
  bord droit pour la durée, clic droit efface ce qu'il survole, flèches déplacent (Maj : octave),
  Q quantifie, Ctrl+D duplique, couloir de vélocité en bas, clavier cliquable qui joue
  l'instrument. Ctrl+molette zoom horizontal, Alt+molette hauteur des lignes. Comme partout où il y a
  une ligne de temps, la **bande à saisir** au-dessus des mesures (à gauche/droite : se déplacer ; vers
  le bas : zoomer ; vers le haut : dézoomer ; double-clic : tout le clip) et, à droite des notes, la
  bande des hauteurs (haut/bas : défiler ; gauche : rangées plus hautes ; double-clic : taille normale).
  Modes Hauteur / Volume : voir « Expressions par note ».
- **Clip audio** : la source entière d'abord, puis la même bande à saisir au-dessus de ses secondes
  (Ctrl+molette zoome au pointeur, molette déplace).
- **Potards, faders, champs** : glisser verticalement à trois vitesses — normale, **Ctrl lente**
  (réglage fin), **Maj rapide** (`ui::valueSpeed`) — molette, double-clic pour la valeur par défaut.
  **Un clic sur le nombre** écrit sous un potard, un fader, un champ (sans glisser) ouvre une saisie au
  clavier : Entrée la prend (« 1,5k » = 1500, la virgule vaut un point, une fraction se tape en %),
  Échap ou un clic ailleurs l'abandonne (`ui::valueEntry`, `visual::typedValue`) — dans Périphériques,
  les grandes fenêtres, les effets de studio, l'Égaliseur Pro, les contrôleurs du sampler, le mixeur
  (niveau, panoramique « C », « L 30 »).

## Architecture

```text
JUCEApplication (src/v2/Main.cpp)
  └─ MainWindow → ImGuiHost : fenêtre enfant Win32 + Direct3D 11, ImGui sur le thread des messages,
                              cadencé sur la synchro écran, ralenti au repos (src/v2/ImGuiHost.*)
       └─ AppUi : menus, transport, ancrage, panneaux (src/v2/AppUi.*, *View.*, GraphEditor.*)
Studio (src/v2/Studio.*) : la session sans interface
  ├─ AudioTrackController (moteur v1, inchangé sauf des accesseurs légers sans copie)
  ├─ un GraphSlot par piste : un NodeGraphComponent jamais affiché, utilisé comme modèle, catalogue
  │   et compilateur ; l'éditeur v2 passe par sa couche « editor* » (NodeGraphComponentEditorAccess.cpp)
  │   et chaque modification est compilée et publiée au moteur aussitôt
  ├─ SampleLibrary, import de projets (Ableton, Bitwig, Studio One, DAWproject)
  └─ ProjectIo.cpp : lecture et écriture des .cydaw (format v36 de la v1)
```

Thème, polices (Segoe UI, icônes Segoe MDL2 Assets, Consolas) et widgets compacts :
`src/v2/Theme.*`, `src/v2/Icons.h`, `src/v2/Widgets.*`. Dear ImGui 1.93 (branche docking) est
dans `third_party/imgui`.

## Langues

Options › Langue change l'interface et les textes du moteur (messages) ensemble. Les **noms des
nœuds restent en anglais** par défaut (Chorus, Low-pass, Voice Allocator…, comme dans les outils
nodaux) ; leurs descriptions et leurs réglages suivent la langue. Options › Langue › « Noms des nœuds
en anglais » les traduit à la demande (`Localization::setNodeNamesInEnglish`, réglage
`nodeNamesEnglish`) ; un changement renomme aussitôt les cartes déjà posées (`Studio::refreshTranslations`). Le
français et l'anglais sont intégrés ; une autre langue s'ajoute par deux fichiers à côté de
`CyDAW 2.exe` : `i18n/<code>.json` (copie de `resources/i18n/en.json` traduite, avec
`"language.name"`) et `i18n/v2/<code>.json` (copie de `resources/i18n/v2/template.json`, les textes de
l'interface par leur anglais). `python tools/i18n/extract_v2_strings.py [i18n/v2/<code>.json]`
régénère le modèle depuis `src/v2` et complète un fichier existant. Ce qui manque reste en anglais.

## Port de contrôle et MCP

CyDAW 2 ouvre le même port de contrôle que la v1 (127.0.0.1:47820, `--control-port=N`, `0` pour
aucun, jeton facultatif `CYDAW_CONTROL_TOKEN`) avec les mêmes 94 commandes, plus celles de l'atelier
(`src/v2/RemoteCommands.*`) : le serveur MCP `tools/mcp/cydaw_mcp_server.py` pilote donc la v2, qui
est maintenant son exécutable par défaut (`.mcp.json` pointe sur `CyDAW 2.exe`). Les sondes n'ouvrent
pas le port. Une session automatisée (`CYDAW_NO_RECOVERY=1`, posé par `cydaw_mcp_check.py`) ne laisse
ni copie de récupération ni disposition modifiée. Avec `CYDAW_SINGLE_VIEWPORT=1`, les fenêtres (éditeurs
de périphériques compris) restent dans la fenêtre principale, pour qu'un `snapshot` et `ui_input` les
atteignent (tests).

Fermer la fenêtre, Ctrl+Q, Nouveau, Ouvrir, un modèle ou un import demandent d'abord s'il faut
enregistrer les modifications (Enregistrer, Ne pas enregistrer, Annuler).

Langue : `set_language` (`code`, `nodeNamesInEnglish`) change l'interface et les textes du moteur ;
`status` donne `language`, `languages`, `translatedTexts`, `nodeNamesInEnglish`.

Routage : `add_bus_track` (`name`), `routing` (`track` ; `outputs` = liste de `destination`
— "master", un nom ou un index de piste —, `gainDb`, `input` — une autre entrée de la destination par
nom ou numéro, 2 la première après la principale — et `preFader` pour les remplacer ; la réponse donne
aussi `senders` et `inputs`) ; `add_track_input` (`track`, `name`), `remove_track_input` (`track`,
`input`) ; `status` donne `outputs` (et `bus`, `inputs`) pour chaque piste. `get_track_graph` avec
`native: true` ajoute le graphe compilé que le moteur exécute (nœuds, câbles, modulations).

Sound matching : `recreate_sound` (`files`, `name`, `refine`) puis `sound_matching` (l'avancement, `cancel`) ;
`load_sound_model` (`file`, `track`), `readable_patch` (`track` ou `file`), `render_sound_model` (un modèle
joué hors ligne dans un WAV : `model`, `out`, `note`, `velocity`, `seconds`, `held`, `settings`),
`companion_info` (`python`), `show_dialog` (`name` : audio, companion). `instrument_export_check` (`track`,
`note`, `seconds`, `stateOut`) : l'instrument tel qu'un plug-in exporté le reçoit (son état restauré, une note
jouée hors ligne ; un sampler attend ses échantillons, la réponse donne `sampler`, `samplerZones`,
`samplesReady`, `samplerValues`, `rmsRight` et `tailRms`, le niveau de la dernière demi-seconde ; `notes`
`[[note, vélocité, secondes, durée]]` joue plusieurs notes, relâchées après leur durée quand elle est donnée,
`controllers` `[[numéro, valeur, secondes]]` envoie des contrôleurs MIDI).

Sampler : `factory_instruments` (les instruments d'usine : `key`, `name`, `category`, `range`, `map`,
`model`), `add_factory_instrument` (`key`, `model` : ses échantillons ou son modèle de son), `new_sampler`
(`files`), `load_sampler` (`file` : SFZ ou .cysampler, `track`), `sampler_map` (`track`, `nodeId` ; avec
`map` les zones remplacées, `applySettings`), `sampler_status` (`track`, `wait` : les échantillons décodés,
introuvables, en attente — `deferred` : pas encore atteints par les réglages —, la mémoire ; lus depuis le
disque : `streamedZones`, `streamedHeadMegabytes`, et pour toutes les pistes `streamsBusy`, `streamUnderruns`,
`streamsRefused`),
`sampler_controls` (`track`, `nodeId`, `set` : `{"70": 100, "articulation": 25}`, une valeur négative : la
valeur de départ ; rend les contrôles nommés d'un SFZ avec leur valeur, ses articulations et celle choisie,
le nombre de zones de relâchement et de zones en attente). `show_dialog` `sampler-controls` ouvre l'onglet
des contrôles du sampler de la piste choisie, `batterie-mics` celui des micros de Batterie.

Master : `"track": "master"` désigne la piste master pour les commandes de graphe (`get_track_graph`,
`insert_device`, `list_parameters`, `set_parameter`…) ; un bus se nomme `"bus:<nom>"` (`"bus:"` seul :
celui de sortie). `device_window` (`track`, `nodeId`, 0 pour le graphe entier, `open`) ouvre la fenêtre
d'un périphérique ; `get_track_graph` marque `window` les cartes qui en ont une. `set_view` avec
`catalogue: true` ouvre le catalogue « + ».

Atelier : `workshop` `status` donne aussi `tabs`, `activeTab` et `simulator` (réglages, `playing`,
`levelDb` avant le master) ; `tab` / `close_tab` (`id`), `simulate` (`play`, `source`
none/sine/chords/noise/pink/drums/clicks/sample, `level`, `tone`, `sample`, `pattern`
none/held/chords/arpeggio/bass/scale, `root`, `velocity`, `output`), `note` (`note`, `on`).

Marche/arrêt : `set_node` avec `bypassed` (vrai : la carte est éteinte) ; `describe_node` le dit, et
`get_track_graph` donne `bypasses`, ce que le moteur reçoit (paires nœud remplacé, nœud dont l'entrée
passe ; 0 : le silence). `voice_live` (`track`) : ce que joue la voix la plus récente d'une piste
(Prisme, synthés de la famille) : `modulated` (les réglages que ses modulations déplacent, par clé),
`lfos`, `lfoPhases`, `envelopes` ; `playing` faux quand aucune ne joue. `launch_scene` avec
`query: true` lit seulement ce qui joue, et où (`position`, en secondes dans chaque clip).

Câbles : `connect` prend `mode` (`set` ou `modulate`) et `amount` (-1..1) pour un câble de contrôle
sur un bouton ; `get_track_graph` donne les ports et le mode de chaque câble. `describe_node` liste les
prises `knob.*` d'un périphérique fait d'un graphe. Dispositions : `layout` avec `action` = list, save,
load ou delete et `name`.

Atelier Plugin : `workshop` avec `action` = status, new (`instrument`, `name`, `inputChannels`,
`outputChannels`), channels, open (`name` de Mes
périphériques), open_track (`track`, `nodeId`, 0 = l'instrument d'une piste MIDI), link (`track`),
unlink, save (`asNew`), close, enter / leave, info (`name`, `description`, `author`, `category` ;
`parameter` avec `panel`, `vst3`, `label`), vst3_check, vst3_export (`folder`, `build`), vst3_status,
vst3_cancel. Les commandes de graphe (`get_track_graph`, `add_node`, `connect`, `set_node_parameter`…)
acceptent `"track": "workshop"`. `create_device` (`nodeIds`) réunit des cartes en un périphérique (son et notes),
`list_my_devices` et `insert_my_device` (`name`) servent la bibliothèque.

## Exemples et version de test

**14-pas-de-cinq** : une pièce de cool jazz originale en 5/4 (saxophone alto, piano, contrebasse,
balais), 160 BPM. Le saxophone est joué, pas tapé : ligne d'une note à la fois en legato (notes liées qui se
chevauchent d'un cheveu, glide de 35 ms ; quelques notes détachées), scoops en début de phrase, vibrato
qui vient tard sur les notes longues, chutes et fins qui s'éteignent, souffles, temps levés appuyés, notes
fantômes, un peu derrière le temps ; piano, basse et balais légèrement humanisés.

**15-calle-luna, 16-greve-de-l-ouest, 17-neons, 18-cycles-lents** : quatre pièces de piano seul,
originales, écrites dans les styles demandés (néo-classique au groove latin, intime et mélancolique en
3/4, sombre et pulsé, minimaliste en trois mouvements) — pas des transcriptions. Elles mettent à l'épreuve
la subtilité du jeu : pédale (changée avec l'harmonie, relevée un peu après), rubato (phrases qui
avancent et respirent, ritardandos, points d'orgue), mélodie devant l'accompagnement, accords égrenés
depuis la basse, nuances par phrase. Partition et interprétation : `tools/templates/piano_pieces.py` ;
`build_v2_examples.py --only pianos` (ou luna, greve, neons, cycles).

**12-tous-les-instruments** (Fichier › Modèles) : les 90 instruments d'usine, pour chacun
une piste de ses échantillons et, à côté, celle de son modèle de son (80 en ont un) ; dans l'arrangement,
quatre mesures par instrument, la même phrase sur les échantillons puis sur le modèle (une batterie joue
son rythme deux fois). Gros : 194 Mo (tous les modèles y sont), environ 9 Go d'échantillons en mémoire une
fois ouvert ; il s'ouvre en une dizaine de secondes, les échantillons arrivent ensuite. Fait par
`tools/templates/build_v2_examples.py --only all-instruments`.


`templates/07-egaliseur-pro`, `08-echos-et-mouvement`, `09-glitch-dynamique` et `10-compresseur-pro`
montrent les effets ; ils sont construits à partir des modèles 05, 03, 06 et 05 par `python tools/templates/build_v2_examples.py`
(port de contrôle, master à -60 dB, sans lecture). Comme tous les modèles, ils ont leurs ctests
`cydaw2_roundtrip_*` et `cydaw2_device_taps_*`. `python tools/package_v2_test.py` rassemble la version
de test dans `dist/CyDAW2-<version>-Windows-x64-test` (+ .zip), avec `docs/V2_TEST_BUILD.md` comme
LISEZ-MOI.

## Tests et sondes

Une session automatisée (`--silent`, et les sondes) a sa **sortie son coupée** : le master, les vumètres et
l'export (qui joue le projet en temps réel par le master) continuent, la carte son ne reçoit rien
(`setSpeakersSilenced`, `status` › `speakersSilenced`).

- `ctest -C Release -R cydaw2` : chaque modèle est chargé, enregistré, rechargé et comparé, et la
  modulation et les potards sont vérifiés dans le moteur (`--modulation-test=<cydaw>`), les prises
  des affichages et la compilation des chaînes de chaque modèle (`--display-test=<cydaw>`), sans
  fenêtre ni son ; `cydaw2_mcp_check` pilote la v2 par le serveur MCP (master à -60 dB) ;
  `cydaw2_routing_check` vérifie les formats de piste (fichier 5.1, mono, quad), les niveaux de chaque
  canal, un bus de sortie quad sans le master et une sortie directe, relus avec le projet (master et bus
  coupés) ; `cydaw2_banks_check` télécharge une banque d'un serveur local dans des dossiers à lui et
  déplace un emplacement (hors ligne, silencieux). Le ctest complet
  lance aussi les tests de la v1, qui ouvrent l'ancienne fenêtre : utiliser
  `ctest -C Release -R "cydaw2|cydaw_core_tests|cydaw_engine_tests"`.
- Sondes : `--demo`, `--snapshot=<png> --snapshot-delay=<ms> --quit`, `--view=<1..6>`,
  `--track=<n>`, `--dump-graphs=<txt>`, `--save-as=<cydaw>`, `--audio-settings`, `--silent`,
  `--roundtrip-test=<cydaw>`. Une sonde garde le master à -60 dB et ne touche pas aux réglages.
  Par le port de contrôle, `set_view` avec `"device": <id de carte>` (et `"pane"`) ouvre la fenêtre
  Périphérique ; `0` la ferme. `ui_input` joue des évènements de souris dans l'interface, un par
  image, en pixels de la fenêtre (ceux d'une capture), sans toucher au vrai pointeur :
  `{"type": "move"|"down"|"up"|"wheel"|"wait"|"release", "x", "y", "button", "dy", "frames", "shift"}`.
  `ui_spots {record, filter}` dit où ont été dessinés les potards (`knob:<binding>`) et les flèches
  des modulateurs (`modulator-arrow:<id>`), dans ces mêmes pixels, pour qu'un test y porte le
  pointeur ; `"record": false` arrête le relevé.
  `status` donne le niveau crête et RMS de chaque piste et du master (`meters`, `masterMeter`) ;
  `set_node` accepte `"collapsed"` ; `add_automation`, `list_automation` et `clear_automation`
  acceptent `"key"` (un paramètre de `list_parameters`, valeurs dans ses unités).
- Plantage : la pile d'appels symbolisée est écrite dans `%TEMP%\cydaw2-crash.txt`.
- Sauvegarde automatique toutes les 45 s tant qu'il reste des modifications, proposée au démarrage
  suivant après un plantage (`%APPDATA%\CyDAW\Recovery\last-session-v2.cydaw-recovery`).

## Porté depuis la v1

- Les graphes des bus de la carte son s'éditent en nœuds comme ceux d'une piste (Routage › Modifier ses
  effets, ou `set_view {mode: 7, bus}`), et l'atelier dessine librement l'interface d'un périphérique
  (Graphe | Interface). Il ne reste rien de la liste d'origine.

### Designer de l'Atelier Plugin (15 octobre 2026)

- Dans Graphe, **Détails** suit la carte sélectionnée : réglages, publication
  Panneau/VST3, prises Exposer, noms et catégories. **E/S** crée et supprime les
  entrées/sorties typées. Le menu Nouveau propose aussi un graphe vide.
- Dans Interface, choisir **Grande fenêtre** ou **Carte module**. Les tailles
  et les contrôles sont indépendants ; la carte apparaît dans Périphériques.
  **Réglages de la grande fenêtre** reste accessible même avec un contrôle
  sélectionné : formats, largeur/hauteur en pixels, fond, accent, texte et grille.
  Les couleurs de la fenêtre et de la carte sont indépendantes, sauvegardées et
  reprises à l'export ; un contrôle peut définir ses propres couleurs.
- Depuis une valeur du panneau Détails ou le clic droit sur un potard :
  **Create Input** crée une entrée typée du graphe et la relie au réglage ;
  **Create Variable** crée une constante locale nommée, modifiable et reliée.
  La variable conserve la valeur, l'unité et la plage du réglage. Les mêmes
  actions existent sur les prises d'entrée (variable pour les valeurs seulement).
  Une entrée déjà reliée doit être débranchée avant la promotion. Les entrées de
  valeur reliées directement à un seul paramètre publié, en mode valeur,
  deviennent ce paramètre dans le VST3 ; les prises restent présentes dans le
  module CyDAW. Une même entrée vers plusieurs paramètres demande encore un
  adaptateur VST3 et n'est pas exportée.
- **Panneaux natifs CyDAW** propose les sections des 15 synthés de la famille,
  de Prisme et de Kick. Sous un instrument présent dans le graphe,
  **Ajouter l'interface complète** crée ses pages et ses sections déjà reliées.
  Chaque panneau reste déplaçable, dimensionnable et personnalisable (titre,
  couleurs, page et liaisons). Un premier modèle remplace la disposition
  automatique ; les contrôles déjà dessinés à la main sont conservés.
- **Bibliothèque de sections** permet de prendre un panneau natif pour un
  graphe personnel, même sans cet instrument. Dans **Liaisons du panneau
  natif**, affecter chaque valeur à un paramètre du graphe ; **Sans liaison**
  désactive le contrôle. Les panneaux d'effets ont aussi leurs liaisons éditables.
- **Taille fenêtre**, dans la barre du canevas, règle directement largeur et
  hauteur en pixels. **1:1** montre la taille réelle ; **Cadrer** ajuste l'aperçu
  à la place disponible. Ces dimensions sont indépendantes de la carte module,
  sauvegardées et reprises par la grande fenêtre et le VST3 exporté.
- La colonne Calques gère la visibilité et la sélection multiple. Ctrl/Shift
  ajoute à la sélection ; un rectangle sélectionne plusieurs éléments.
  Alignement, distribution et groupes sont disponibles pour une sélection.
- Les propriétés ajoutent couleurs, fond, texte, taille de police, arrondi,
  page, groupe, verrouillage et valeur visible. Les pages se changent au-dessus
  du canevas. Le zoom et le déplacement au bouton du milieu facilitent le travail.
- **Code JSON** édite le descriptif complet de la disposition active. Appliquer
  refuse notamment les types inconnus, IDs dupliqués, liaisons manquantes et
  contrôles hors canevas. Recharger récupère le modèle actuel.
- **Composant C++ ImGui** crée un contrôle personnalisé. Exemple insère un
  composant utilisable ; Compiler affiche ses diagnostics. Le code utilise
  `CustomInterfaceABI.h` et les callbacks de paramètres `get`/`set`.
- Les panneaux natifs disponibles reprennent les afficheurs et réglages de
  leurs modules. Le mode lecture permet de les utiliser dans le designer.
- L'export de l'Atelier est désormais VST3 natif sans JUCE, avec l'interface
  conçue et les composants C++ compilés, sans l'éditeur de graphe. La limite
  historique de 32 paramètres a été retirée. La première compilation nécessite
  Visual Studio C++, CMake et le SDK VST3 ; les suivantes réutilisent le cache.

Version actuelle de l'export : Windows x64, une sortie audio et jusqu'à quatre
entrées. Synthèse et effets ; les lecteurs de samples et modèles enregistrés
sont exclus. Les paramètres de synthèse sans prise de modulation moteur restent
non exposables en câble. La licence du moteur embarqué conserve ses conditions.

## Formats CLAP, LV2 et scripts JSFX

L’onglet **Export** de l’Atelier propose maintenant VST3, CLAP et LV2. L’éditeur de scripts est accessible dans **Affichage → Éditeur de scripts JSFX**, dans la bibliothèque et sur les cartes JSFX. Voir [PLUGIN_FORMATS.md](PLUGIN_FORMATS.md) pour les chemins, le rechargement, les dépendances et les limites de l’hôte.


### Export natif et compilation multiplateforme

La barre de l'Atelier Plugin propose **Exporter CyDAW…** / **Ouvrir CyDAW…** pour partager un `.cydawdevice` éditable entre installations, sans compiler. L'export conserve interfaces, presets et définitions des modules utilisés. Les fichiers audio externes sont listés, à transférer séparément.

Dans **Export**, sélectionner plusieurs formats VST3/CLAP/LV2 et plateformes Windows x64/Linux x64 ; les résultats vont dans `<appareil>/<plateforme>/<format>`. Linux se compile localement depuis Windows avec Zig et un sysroot X11/OpenGL préparé au premier usage (Python 3 et Ninja requis), sans runner. macOS est reporté. Voir [PLUGIN_FORMATS.md](PLUGIN_FORMATS.md) pour la chaîne locale, les limites et les tests.

Contrôle : `workshop` `device_export` (`file`), `open` (`file`), `plugin_export_batch` (`formats`, `platforms`, `folder`, `build`, `sourceRoot`). `vst3_status` donne une entrée par cible dans `targets`, avec `platform`, `format`, `file`, `error`.
