# Architecture technique

> L'ancienne interface JUCE (`CyDAW.exe`, ses composants `*Component` d'écran, l'éditeur de graphe autonome
> et `cydaw_desktop_tests`) a été retirée le 19 octobre 2026 : l'application est CyDAW 2 (`src/v2`). Le
> moteur décrit ici (`src/app` : pistes, graphes, runtime natif, import, plug-ins isolés) est celui qu'elle
> utilise ; les passages sur les écrans et les tests de l'ancienne interface sont historiques.

## Séparation des responsabilités

```text
UI / commandes / scripts de contrôleur
                 |
          Project model + undo
                 |
       Graph compiler (control thread)
                 |
      immutable real-time execution plan
                 |
audio device <-> engine <-> shared-memory plugin workers
                        |-> native VST3 / optional licensed VST2
                         |-> Wine/yabridge Windows plugins
```

Le cœur ne dépend pas de JUCE. JUCE adapte les devices, fenêtres, fichiers et
formats de plugins. Cela évite de mélanger état de projet, API d'un framework et
contraintes du callback audio.

## Threading

- **Audio thread** : exécute un plan immutable, buffers préalloués, aucun lock.
- **Control thread** : édite/compile le graphe et publie une nouvelle génération.
- **UI thread** : vues, gestes, menus et layout ; aucune propriété DSP directe.
- **Disk workers** : streaming, waveforms, analyse et base du browser.
- **Audio edit worker** : analyse de pic, export WAV et rendus de sélection
  annulables, sans bloquer l'UI ni toucher au fichier source ; le commit d'un
  gain calculé ou d'une nouvelle source revient sur le thread UI après validation
  de l'identité de la piste et conserve un historique borné de 32 états par piste.
- **Master export writer** : un wrapper placé après le mixeur copie la sortie stéréo
  finale dans le FIFO préalloué de `ThreadedWriter`. Le callback ne crée ni fichier,
  ni encodeur et n'attend jamais le disque ; l'en-tête WAV, le flush et la destruction
  du writer restent sur le thread de contrôle/disque. L'export désactive temporairement
  la boucle d'arrangement, capture la durée de session plus deux secondes de queue
  d'effets, puis restaure l'état de transport.
- **Launcher read-ahead** : chaque piste possède deux readers indépendants et
  deux buffers bornés de 131 072 échantillons. Le contrôle prépare le deck de
  réserve, le thread disque le remplit et le callback ne publie la bascule qu'une
  fois ce préchargement visible par atomique ; aucun décodage fichier n'est fait
  sur le thread audio.
- **Pistes instrument MIDI** : chaque piste possède une séquence publiée
  immuable, un synthé et une branche gain/pan/latence séparés. Les clips sont
  aplatis en événements absolus lors d'une édition ou d'un changement de tempo ;
  le callback ne parcourt ensuite que ce snapshot. Le collecteur MIDI et le
  clavier sont partagés mais consommés uniquement par la piste armée.
- **Plugin supervisor** : processus, heartbeat, IPC, crash et restauration.
- **Plugin workers** : un processus par instance par défaut.

Les commandes UI -> audio passent par une file SPSC bornée. Les automations et
événements contiennent leur offset dans le bloc. Les meters audio -> UI utilisent
une file séparée où la perte d'une valeur est autorisée.

Le premier runtime implémenté fournit déjà buffers planaires préalloués, transport
en samples, file SPSC bornée, compilation topologique du plan audio, sommation de
branches et processeurs gain/oscillateur. La publication RCU d'un nouveau plan et
les événements intra-bloc restent l'étape temps réel suivante.

## Graphe

Le modèle accepte trois familles de ports : audio, MIDI et contrôle. Une édition
est validée puis compilée par tri topologique déterministe. Le feedback ne sera
autorisé qu'avec un nœud délai explicite. Le desktop produit déjà un plan audio
immutable avec sommation des branches, remplacement hors callback et état visuel
indépendant par piste. Dix topologies factory utilisent exactement ce chemin de
compilation et restent entièrement éditables après chargement. Les prochaines
extensions du compilateur ajouteront :

- ordre et niveaux de parallélisme ;
- affectation/réutilisation des buffers ;
- compensation de latence globale ;
- frontières d'oversampling ;
- batches d'événements sample-accurate ;
- proxy IPC pour intégrer les plugins isolés directement comme nœuds du DAG.

Chaque module utilisateur possède maintenant un document d’interface versionné,
séparé de sa topologie : paramètres publiés avec plage/unité/valeur, contrôles
typés, binding stable et géométrie normalisée. L’éditeur est une fenêtre native
indépendante. En mode performance les knobs et faders publient les paramètres de
la piste sélectionnée ; en mode édition les contrôles sont déplaçables et leur
menu permet de changer type, binding ou suppression. Waveform, spectre et mètres
consomment la même télémétrie bornée audio→UI que le graphe, sans calcul dans le
callback. Ce document accompagne le module composite partagé dans chaque projet.

## Isolation des plugins

Le protocole `PluginIsolation.h` est fixe, versionné et indépendant du format.
Audio et événements utilisent des slots de mémoire partagée préalloués. Le canal
de contrôle transporte chargement, état, paramètres, UI et diagnostics. Le
worker n'accède jamais au modèle de projet.

Le premier transport exécutable utilise un slot mémoire partagé à pipeline d'un
bloc. Le callback dépose le bloc courant sans attendre et consomme uniquement une
réponse prête à temps ; sinon il conserve le signal sec. Une boîte aux lettres de
contrôle séparée, jamais attendue par le callback audio, transporte les paramètres,
un état binaire borné à 4 Mio et les commandes d'affichage de l'éditeur. La fenêtre
du plug-in appartient au worker et sa boucle de messages reste donc dans le domaine de
crash isolé. Un test d'intégration construit un VST3 de référence, vérifie son
gain, ses paramètres, la restauration de son état et son éditeur dans le worker,
puis tue le processus pour contrôler le bypass de sécurité.

Le même protocole accepte VST2 lorsque le build reçoit explicitement un SDK
historique autorisé. Ce format est désactivé par défaut ; la configuration refuse
un chemin incomplet et ajoute un second test d'intégration seulement quand les
en-têtes requis sont présents.

Une seconde boîte aux lettres bornée transporte jusqu'à 64 changements de
paramètres automatisés par instance et par bloc. Le callback ne la publie que
lorsque le worker a terminé le bloc précédent, puis soumet audio et automation
dans la même transaction ordonnée. Il n'attend donc jamais une commande de
contrôle et aucune lecture/écriture concurrente du slot n'est permise.

Le mixeur de session calcule la latence maximale déclarée par les workers
(pipeline IPC inclus) et retarde chaque autre piste ainsi que le synthé interne
du complément nécessaire. Une piste chaîne jusqu'à huit workers ; sa latence est
la somme de leurs pipelines IPC et de leurs latences déclarées. Les lignes
stéréo sont préallouées, bornées à 131 072
échantillons et réinitialisées sans effacer de mémoire dans le callback lors des
seeks ou des changements de topologie. La future compilation du graphe étendra
ce calcul aux branches internes.

Le coût d'un worker par instance est accepté comme mode sûr. Un mode groupé par
binaire est disponible pour les plugins qui partagent de grandes banques, avec
un avertissement clair : ils partagent alors aussi leur domaine de crash.

Sous Linux, un wrapper yabridge est chargé par ce même worker natif isolé. Le
mode direct lance à la place un `CyDAWPluginWorker.exe` compatible par Wine ou
UMU/Proton. Les chemins POSIX sont projetés sur le lecteur Wine `Z:` et la même
ABI IPC est utilisée ; des `static_assert` verrouillent taille et offsets du
slot partagé entre MSVC, GCC et Clang. Le navigateur emploie exactement le même
lanceur pour le scan et la validation DSP, jamais le processus principal.
La sélection du runtime est centralisée et déterministe : runner personnalisé,
UMU explicitement demandé, sinon Wine. Le statut du navigateur est issu de ce
même plan. Le CI construit le worker Windows depuis le même commit, vérifie son
handshake puis l'installe dans l'artefact Linux ; un test shell rejoue aussi les
cas Wine, UMU, worker absent et protocole incompatible sans dépendre d'un plugin.

## Hardware DSP

Une carte DSP peut exposer trois surfaces différentes :

1. flux audio/horloge/canaux via ASIO, CoreAudio ou ALSA ;
2. plugins dont le calcul est déporté par le runtime du constructeur ;
3. mixeur/routing/monitoring propriétaire.

Les deux premières fonctionnent via les APIs standards et l'hébergement de
plugins. La troisième nécessite un `HardwareControlAdapter` par fabricant et ne
sera activée que sur SDK public/licencié. Le DAW reste fonctionnel si cet
adaptateur est absent, en laissant le panneau de contrôle officiel accessible.

## Budget performance initial

- aucune XRUN sur une session de référence à 48 kHz / 64 samples ;
- p99 du callback < 70 % de sa deadline, p99.99 < 90 % ;
- publication d'un nouveau graphe sans interruption audio ;
- UI 60/120 Hz indépendante de la charge audio ;
- scan de plugin et analyse de bibliothèque toujours hors processus ;
- tests soak 8 h, débranchement/rebranchement et injection de crash worker.

Ces objectifs seront mesurés par bancs reproductibles ; « temps réel » ne sera
pas déduit de la seule moyenne CPU.

## Architecture UI nodale

Le canevas ne possède jamais le DSP. Il affiche une projection du modèle et
envoie des commandes transactionnelles. Les analyseurs audio publient des
frames décimées (peak/RMS, min-max waveform, bins FFT) dans des ring buffers
bornés. Une frame visuelle peut être abandonnée si l'UI est en retard ; un bloc
audio ne l'est jamais.

Le layout d'un nœud est décrit par un document versionné : ports, paramètres,
contrôles, visualizers, contraintes et bindings. Les composants natifs assurent
le rendu de base ; un futur moteur de skin/UI peut enrichir la vue sans avoir le
droit d'exécuter du code sur le callback audio.

Depuis le jalon 0.12, la projection UI exporte aussi un `NativeDspGraph` sans
dépendance graphique. Chaque piste le compile hors callback avec
`AudioRenderPlanBuilder` : ordre topologique déterministe, buffers de nœuds et
états d'effets préalloués, sommation des branches et sortie explicite. La
publication utilise une barrière callbacks-en-vol ; l'ancien plan est détruit sur
le thread de contrôle après le basculement et jamais depuis le thread audio.

Depuis 0.16, le workspace conserve un document nodal par piste audio et republie
uniquement le plan de la piste sélectionnée lorsqu'il est édité. La sélection
sauvegarde le document courant, restaure celui de la nouvelle piste puis affiche
son nom dans le bandeau du graphe. Les modules composites forment une bibliothèque
de projet commune, injectée dans chaque document sans rendre leurs connexions ou
positions solidaires. Le format `.cydaw` 20 sérialise ce document dans chaque
piste ; un ancien champ global est encore lu puis cloné lors de la migration.

Depuis 0.24, une piste audio publie un plan d'arrangement immutable contenant
jusqu'à 64 régions multi-sources. Chaque région ouvre son propre reader et son
buffer de lecture anticipée hors callback. Le thread de read-ahead convertit sa
fréquence d'échantillonnage vers celle de la session par interpolation linéaire ;
le callback ne voit que des samples déjà préparés. Il parcourt ce tableau borné, lit les
intersections dans un scratch stéréo préalloué et somme les chevauchements ; il
n'alloue pas et ne reconfigure jamais un fichier. Un déplacement ne change qu'un
start sample atomique. Les ajouts, trims, découpes et suppressions construisent un
nouveau plan puis l'échangent avec une barrière callbacks-en-vol ; l'ancien plan
est détruit sur le thread de contrôle. Le schéma projet 20 stocke le chemin de
source de chaque région et migre les schémas 19/18 ainsi que le triplet historique
`clipStart/sourceOffset/clipDuration`. Un rendu d'édition remplace uniquement la
source de la région ciblée et son historique undo/redo, sans modifier ses voisines.

Depuis 0.25, chaque région transporte aussi deux durées de fondu bornées à sa
durée. Elles sont quantifiées une seule fois en samples lors de la construction
du plan ; le callback ne suit qu'un chemin rapide lorsque les deux valeurs sont
nulles. Sinon, il multiplie directement les deux pointeurs du scratch préalloué
par une rampe linéaire avant la sommation. Les gestes de trim/fade restent locaux
à l'UI pendant le drag puis publient une seule transaction au `mouseUp`. La
commande crossfade choisit le chevauchement maximal, assigne des rampes de même
durée aux régions sortante/entrante et sérialise `fadeIn/fadeOut` dans le schéma 20.

Depuis 0.26, chaque piste conserve jusqu'à 32 snapshots bornés de sa liste de
régions sur le thread de contrôle. Déplacement, trim/fade, crossfade, ajout,
duplication, découpe et suppression enregistrent l'état antérieur seulement après
la publication réussie du nouveau plan ; une édition sans changement ne crée pas
d'entrée et toute nouvelle transaction efface le redo. Undo/redo reconstruit le
plan hors callback, restaure l'état précédent en cas d'échec et conserve la
position de transport dans la nouvelle longueur. L'aperçu d'un drag reste local
à l'UI et n'est validé qu'au `mouseUp`, donc un geste continu reste une seule
transaction. Cet historique de session n'est pas sérialisé dans le schéma projet
20 ; il est vidé après chargement et lorsqu'un rendu audio remplace une source,
afin qu'un snapshot d'arrangement ne puisse pas rétablir une ancienne source.

Depuis 0.27, le workspace porte une sélection ordonnée d'identifiants de régions
et une région primaire par piste active. Le Ctrl-clic ajoute/retire un membre,
Ctrl+A normalise la sélection sur toutes les régions vivantes, et le changement de
piste élimine les identifiants devenus invalides. Un déplacement de groupe calcule
un delta unique depuis la région primaire, le borne avec la région la plus à gauche,
puis republie le plan une seule fois ; duplication et suppression groupées suivent
la même transaction atomique et conservent l'espacement relatif. La grille est
dérivée du tempo de session et du choix 1/4–1/32, puis partagée par le rendu de la
timeline, le seek, les gestes de région et les commandes. Elle reste une préférence
de projet dans le schéma 20 ; `Alt` désactive seulement le snap du geste courant.

Depuis 0.28, le presse-papiers de l'arrangement conserve des copies de descriptions
de régions et non des buffers audio : fichier source, offset, durée, fondus, couleur
et position relative restent intacts. Le collage attribue de nouveaux identifiants,
reconstruit une seule fois le plan de la piste cible puis publie un unique snapshot
undo ; un échec d'ouverture de source restaure intégralement l'état précédent.

Depuis 0.29, l'arrangement sépare la durée complète du projet de la fenêtre
temporelle visible. La règle et toutes les formes d'onde reçoivent la même plage
`[début, fin]`; leurs conversions pixels/secondes, grilles, régions, fondus,
crossfades, playhead et dépôt de fichier restent donc alignés à tout niveau de
zoom. Le zoom est centré sur le pointeur, le panoramique est borné à la durée de
session et une barre horizontale reflète exactement la plage visible, sans déplacer
les en-têtes de pistes. Cette navigation est strictement une préoccupation UI et ne
modifie ni le transport temps réel ni le document de projet.

Depuis 0.30, une sélection de temps rectangulaire est partagée par la règle et
toutes les pistes visibles. Le workspace convertit les coordonnées écran du geste
en une plage temporelle et un intervalle de pistes, puis conserve les identifiants
des régions intersectées par piste. Les suppressions, coupes et collages ouvrent
un lot d'édition au niveau du contrôleur : le premier snapshot de chaque piste
touchée est capturé, validé comme un seul état de session, ou restauré intégralement
si une sous-opération échoue. Le presse-papiers groupe les descriptions de régions
par décalage de piste et mémorise une ancre temporelle globale ; le collage recrée
donc la géométrie horizontale et verticale du bloc sans copier de buffers audio.
La sélection de la piste cible passe par le même chemin que l'interface afin que
le graphe nodal affiché, son document sérialisé et le plan DSP de la piste restent
toujours cohérents.

Depuis 0.31, le contrôleur expose deux transactions de session supplémentaires.
L'édition de plage transforme directement les descriptions de régions : la découpe
produit jusqu'à trois segments en conservant les offsets source, le rognage garde
l'intersection non destructive, et le silence conserve uniquement les fragments
extérieurs. Les fondus ne traversent jamais une nouvelle frontière interne et un
échec de lecture ou la limite de régions restaure toutes les pistes du lot. Le
déplacement inter-pistes calcule d'abord les états finaux de toutes les sources et
destinations, valide leur capacité, puis remplace et reconstruit chaque plan une
seule fois. Les identifiants sont conservés lors d'un déplacement horizontal et
renouvelés lors d'un transfert vertical, tandis que l'historique capture l'ensemble
comme un seul snapshot de session. Aucun état intermédiaire supprimé/collé n'est
donc visible par le callback audio.

La boucle n'utilise plus la longueur propre de chaque piste. Le contrôleur publie
une longueur de transport et les mêmes bornes en secondes à tous les
`ClipAudioSource` et `MidiSynthEngine`. Chaque source convertit ces bornes dans son
domaine de samples, scinde le bloc à la frontière, rend le silence hors de son
contenu puis reprend au début commun sans allocation. Les notes MIDI en cours
reçoivent All Notes Off et sont rechassées au retour de boucle. Le schéma projet 20
stocke optionnellement `loopRangeExplicit`, `loopStart` et `loopEnd` ; leur absence
conserve la migration historique vers la durée complète du projet.

Depuis 0.20, chaque `NativeDspNode` transporte son propre bloc de paramètres
(Drive/Cutoff, fréquence/niveau des générateurs, temps-feedback-mix du délai,
room-mix de la réverbération, seuil-ratio du compresseur et tonalité de l’EQ).
Ces valeurs sont bornées à la restauration, sérialisées avec le nœud puis passées
au constructeur du processeur lors de la compilation hors callback. Un module
utilisateur est maintenant expansé récursivement : les identifiants internes sont
remappés par instance, les frontières entrée/sortie sont dérivées du DAG et les
connexions externes sont raccordées à ces frontières. Les modules imbriqués sont
donc rendus par leur topologie réelle et non par un processeur composite approché.
Une pile de dépendances empêche une définition cyclique de bloquer la compilation.

Le même jalon attache une interface versionnée à chaque nœud. Le double-clic sur
un effet, générateur, analyseur ou module factory ouvre le même constructeur que
les modules utilisateur. Les changements de valeur mettent à jour le document
nodal, déclenchent sa recompilation sur le thread de contrôle et restent visibles
dans les visualizers alimentés par la télémétrie SPSC.

Depuis 0.21, une édition de valeur suit un chemin distinct d’une édition de
topologie. `updateNativeDspParameters` vérifie l’identité ordonnée des nœuds, la
sortie et toutes les connexions ; au moindre changement structurel il refuse la
mise à jour et le workspace retombe sur la compilation complète. Sinon, il écrit
uniquement dans les paramètres atomiques des processeurs déjà possédés par le
plan. Le délai réserve à `prepare` une ligne circulaire de deux secondes et
modifie sa tête de lecture, son feedback et son mix sans redimensionner le buffer.
Les interfaces persistent d’abord leur état, puis publient ce document au moteur,
de sorte qu’un changement de knob ne déclenche plus la destruction d’un plan.
Le test temps réel alterne les valeurs des effets pendant 2 000 blocs sous une
sonde globale d’allocations et exige strictement zéro `operator new`.

Depuis 0.22, `MasterOutputAudioSource` enveloppe le mixeur remis à
`AudioSourcePlayer`. Le même bloc qui part vers la carte son est copié dans un FIFO
stéréo 24 bits lorsque l'export master est armé : synthés MIDI, automation,
compensation de latence, plans DSP natifs et réponses des workers de plug-ins sont
donc inclus sans chemin de rendu divergent. La prise du verrou de callback du
device ne sert qu'aux transitions start/finalize/cancel afin que le premier bloc
du WAV coïncide avec la position zéro. Un compteur atomique borne exactement la
durée, un drapeau signale la finalisation au timer UI et une annulation détruit le
writer avant de supprimer le fichier partiel.

Le jalon 0.13 rend la télémétrie visible. Après DSP natif, plugins isolés et
gain/automation, chaque piste calcule uniquement peak, RMS et 256 points de
waveform dans le callback, puis tente de publier un `AudioAnalysisFrame`
trivialement copiable dans une file SPSC de huit slots. Une frame visuelle est
abandonnée si l'interface est en retard. Le timer UI draine la piste sélectionnée
à 30 Hz, réalise la DFT fenêtrée et logarithmique hors du thread audio, puis peint
waveform/spectre dans le nœud Analyzer, niveaux stéréo dans le Master et activité
des câbles. Aucun verrou, heap, FFT ni appel graphique n'entre dans le callback.

## Docking et layouts

Le jalon desktop 0.11 implémente la première tranche de cette architecture : les
largeurs/hauteurs et la visibilité de l'inspecteur, du navigateur et du panneau
inférieur sont persistantes ; quatre presets et des layouts utilisateur nommés
peuvent être appliqués, sauvés et restaurés par projet. Les transitions disposent
d'un historique borné à 32 états. L'inspecteur, le navigateur, l'éditeur central,
le panneau inférieur et le graphe nodal peuvent être détachés simultanément ; leurs
bounds sont sérialisées puis contraintes à un écran disponible à la restauration.

Depuis 0.14, la navigation transversale passe aussi par une palette de commandes
éphémère. Elle est construite à l'ouverture avec les libellés de la langue active,
mais chaque entrée conserve des mots-clés anglais et français. Les actions sont
capturées par `SafePointer`, la palette se ferme avant leur exécution asynchrone et
ne peut donc pas appeler un workspace détruit. Le filtre reste local à l'UI et ne
touche ni au modèle de session ni au thread audio.

La cible complète reste la suivante :

Le workspace est un arbre de `split -> tab stack -> panel`. Une fenêtre flottante
possède simplement une autre racine. Un drag/drop construit une copie candidate,
la valide, puis remplace atomiquement le layout UI. La sérialisation conserve les
identités de panneaux, bounds, écran, DPI et état actif ; la restauration ramène
les fenêtres hors écran vers un écran disponible.

## Modules composables

`ModuleLibrary` impose des identifiants/slugs stables et compile le DAG des
dépendances. Une instance référence une version de module et ses overrides ; le
graphe interne n'est aplati qu'à la compilation DSP. Cela garde l'édition lisible
tout en permettant au runtime d'inliner, fusionner les buffers, éliminer les
nœuds inutilisés et paralléliser au-delà des frontières de modules.

## Éditeur audio détaillé 0.32

`AudioEditRequest` sépare le type de traitement de son niveau en dB. Le composant
d'édition publie la plage en temps source — jamais en temps arrangement — puis le
workspace lance le rendu dans son pool de jobs annulables. Le renderer parcourt
le fichier par blocs bornés et recopie toute la source dans un nouveau WAV 24 bits ;
seule l'intersection exacte avec la sélection est transformée. Normalisation et
suppression DC effectuent une première passe bornée pour mesurer respectivement le
pic commun ou la moyenne de chaque canal. Aucune de ces passes ne touche le callback
audio.

Le gain et la normalisation utilisent un facteur commun avec saturation de sécurité,
la suppression DC conserve une moyenne indépendante par canal, l'inversion de
polarité agit sample par sample et l'échange stéréo ne permute que la première paire
de canaux. Le renderer refuse explicitement cette dernière opération sur une source
mono. Une fois le fichier terminé, `commitAudioClipEdit` remplace atomiquement la
source de la seule région éditée et conserve l'ancienne référence dans un historique
borné à 32 états. Annulation, erreur, changement de source concurrent ou fermeture
du job suppriment toute sortie partielle.

Les contrôles utilisent le même dispatcher que la palette globale et les raccourcis,
ce qui évite plusieurs implémentations d'une même commande. Le test desktop vérifie
le dispatch exact de la sélection, l'exclusion de jobs concurrents, les valeurs de
sample des neuf opérations, l'absence de modification hors sélection, l'erreur mono
et l'undo/redo de la source de région.

## Enregistrement MIDI 0.33

Seule la piste instrument armée draine le `MidiMessageCollector`. Avant d'ajouter les
événements planifiés du clip au buffer de synthèse, elle copie chaque note-on/off du
contrôleur ou du clavier virtuel dans une file SPSC bornée. L'événement contient canal,
note, vélocité et temps de timeline calculé à partir de la position du bloc et de son
offset en samples. Le callback n'alloue pas, ne verrouille pas et abandonne explicitement
l'événement si la file est pleine ; un compteur atomique rend cette perte visible.

Le timer d'interface draine la file et modifie le modèle du clip sur le thread de
contrôle. Les notes actives sont indexées par les 16 canaux et 128 hauteurs afin que
deux prises de même hauteur sur des canaux distincts restent indépendantes. Le début et
la fin sont convertis relativement à `MidiClip::startBeats`, puis quantifiés selon la
grille choisie. Une note provisoire est créée au note-on et sa durée est allongée pendant
la tenue ; le note-off la ferme. La plage du clip armé fournit temporairement une durée
de transport même s'il ne contient encore aucune note, ce qui rend le premier
enregistrement d'un projet vide possible.

Une prise ouvre un `MidiEditState` avant toute mutation. En remplacement, les anciennes
notes sont supprimées à l'intérieur de cette transaction ; en overdub elles restent en
place. Stop publie l'état précédent dans un historique borné à 32 entrées, tandis que
Cancel le restaure sans créer d'entrée. Le même mécanisme regroupe tous les updates d'un
glisser de note en un seul geste. Le piano-roll, les raccourcis et la palette globale
partagent donc exactement les mêmes opérations undo/redo.

## Édition MIDI multipoint 0.34

Le piano-roll conserve une sélection d'identifiants stables, indépendante du tableau
de notes rafraîchi par le moteur. Un clic modifié ou un rectangle ajoute plusieurs
notes ; le déplacement, le redimensionnement par le bord droit et la lane de vélocité
reconstruisent chaque frame depuis le snapshot pris au début du geste. Cela empêche
l'accumulation d'erreurs lorsque le pointeur revient en arrière et borne le groupe à
la grille, à la plage de hauteurs visible et aux valeurs MIDI valides.

L'interface publie le résultat par `replaceMidiNotes`, une mutation atomique qui
normalise les valeurs, conserve les identifiants existants, attribue des identifiants
neufs aux notes collées, agrandit le clip par mesures de quatre temps si nécessaire
et ne republie pas un tableau identique. Le snapshot transactionnel inclut aussi la
longueur du clip : undo/redo restaure donc simultanément notes et plage. Le
presse-papiers, le clavier, le menu contextuel et la palette globale passent tous par
ce même chemin.

## Échange Standard MIDI File 0.35

L'import lit et valide entièrement le SMF avant de toucher au modèle. Le premier
événement tempo définit le tempo de session optionnel ; les timestamps PPQ sont
convertis directement en temps musicaux et le format SMPTE passe par sa fréquence
d'images et sa résolution. Chaque couple piste source/canal devient une piste
instrument indépendante, les noms de piste sont conservés et les note-on sont appariés
à leurs note-off. Nombre de notes, nombre de pistes et longueur musicale sont bornés
avant allocation dans la session.

Le commit ajoute toutes les pistes en un lot sur le thread de contrôle, attribue des
identifiants persistants, arme la première piste importée puis republie les snapshots
MIDI immuables. L'export du clip actif construit un SMF Type 0 à 960 PPQ avec nom de
piste/clip, tempo, signature 4/4, notes et fin de piste. Le test desktop génère un
fichier Type 1 multicanal, contrôle sa séparation en trois pistes, exporte un clip et
le réimporte pour vérifier hauteur, position, durée, vélocité et tempo.

## Navigateur multibibliothèque et audition 0.36

Le navigateur conserve une liste ordonnée d'emplacements et de chemins favoris dans
ses préférences globales. Chaque rescan copie les racines vers un worker basse priorité,
parcourt les formats audio pris en charge, déduplique les chemins lorsque des racines
sont imbriquées et n'ajoute une entrée qu'après validation de son en-tête. Durée,
fréquence d'échantillonnage, nombre de canaux, taille et date de modification sont lus
hors du thread audio. La recherche sépare la requête en termes et exige que chacun
apparaisse dans le nom, le format, le chemin, l'emplacement ou les métadonnées.

La préécoute ouvre un `AudioFormatReaderSource` sur le thread de contrôle et le confie
à un `AudioTransportSource` alimenté par le thread de lecture anticipée existant. Un
mixeur d'audition enveloppe ensuite séparément la sortie master du projet et cette
source de preview avant `AudioSourcePlayer`. La capture master reste volontairement
autour du seul mixeur de session : un sample écouté dans le browser est donc audible
sur la carte son, mais absent de l'export et du modèle de projet. Arrêt, remplacement
et destruction détachent la source avant de libérer son reader.

## Autosauvegarde et récupération 0.37

La construction du document projet est séparée de sa destination. Une sauvegarde
manuelle encode les ressources relativement au `.cydaw` choisi et demande un état
frais aux workers de plug-ins. Une autosauvegarde utilise au contraire des chemins
absolus et la dernière copie d'état déjà détenue par chaque superviseur : elle ne peut
donc ni casser les références en étant stockée dans le dossier applicatif, ni attendre
un processus de plug-in. Le snapshot reçoit un schéma de récupération, l'heure et le
chemin du projet original.

Le thread UI capture le modèle immutable à intervalles de 30 secondes lorsqu'il est
sale, puis un worker unique effectue l'encodage JSON. `TemporaryFile` écrit dans le
même dossier et ne remplace `last-session.cydaw-recovery` qu'après relecture et
validation du format, de la version projet et du schéma de récupération. Une écriture
invalide laisse ainsi la génération connue comme bonne intacte. La fermeture attend le
worker et produit synchroniquement un dernier snapshot si le projet reste modifié.

Au lancement, l'application — désormais mono-instance — valide le snapshot avant
d'afficher le choix Restaurer/Ignorer. Une restauration recharge le document avec ses
chemins absolus, réassocie le fichier original lorsqu'il existe et conserve l'état
sale jusqu'à une sauvegarde explicite. Cette sauvegarde repasse aux chemins relatifs et
supprime le snapshot. L'autotest applicatif crée une session audio+MIDI, vérifie ces
deux représentations, recharge réellement le workspace puis confirme le nettoyage.

## Prise audio intégrée 0.38

`InputRecorder` reste un callback secondaire qui ne contribue que du silence à la
sortie du `AudioDeviceManager`. Il copie les entrées actives vers un
`ThreadedWriter` préalloué ; le callback ne fait ni accès disque, ni allocation, ni
mutation de l'arrangement. Le compteur de blocs refusés reste atomique et visible.

Le workspace capture séparément la piste armée, la position de départ, l'état de
boucle et l'état de lecture. L'armement audio est exclusif et persisté dans le projet
version 21. Pendant la prise, la boucle et la commande Play sont suspendues afin que
le WAV linéaire conserve une correspondance non ambiguë avec la timeline. À l'arrêt,
`commitRecordedTake` valide complètement l'en-tête WAV avant mutation, puis ajoute
une région à la position capturée dans l'historique transactionnel existant. Sans
piste cible, il crée une piste dont la première région commence directement à cette
position. Une annulation désarme uniquement le writer et supprime le fichier neuf.

L'état XML du périphérique JUCE est enregistré avec les préférences globales et
restauré au lancement. Sans état précédent, CyDAW demande deux entrées et deux
sorties ; si aucune entrée n'est disponible, il retente automatiquement en sortie
seule. Le test desktop vérifie insertion, durée, position, undo/redo, création de
piste et refus d'un WAV invalide ; l'autotest applicatif couvre en plus le round-trip
du projet v21 et l'armement restauré.

## Piles de prises audio 0.39

Chaque `AudioClip` conserve désormais une pile bornée de 32 `AudioTake`. Une prise
porte son nom, son fichier et sa longueur validée ; l’index actif est la seule source
présentée au plan d’arrangement. `rebuildArrangementPlan` résout cet index sur le
thread de contrôle, ouvre un nouveau reader et publie ensuite le plan immutable. Le
callback audio ne parcourt donc jamais la pile et ne réalise aucune ouverture de
fichier. Un badge `Tn/total` expose l’état actif directement dans l’arrangement.

Ajout, activation et suppression capturent l’état complet des régions avant mutation,
reconstruisent le plan de manière transactionnelle et réutilisent l’historique global
undo/redo. L’édition audio par sélection mémorise elle aussi le `AudioClip` complet :
son undo restaure simultanément la source rendue et la pile concernée. Le test desktop
rend réellement deux sinus de fréquences différentes, change la prise active et
vérifie par corrélation que le reader publié suit ce choix.

La commande d’enregistrement alternatif arme la piste sélectionnée et positionne le
transport au début exact de la région avant d’ouvrir le writer 24 bits existant. Au
stop, le WAV validé devient la nouvelle prise active et ajuste la plage source sans
créer une région superposée. Le projet v22 sérialise toutes les sources avec des
chemins relatifs en sauvegarde normale, absolus en récupération, ainsi que l’index
actif ; les projets v21 restent chargés avec leur source unique promue implicitement
en première prise.

## Synthèse polyphonique nodale 0.40

Le synthé interne n'est plus un bloc DSP fixe masqué derrière un nœud décoratif.
Chaque piste instrument conserve un graphe versionné composé de `MIDI Input`,
`Voice Allocator`, `Poly Oscillator`, `ADSR Envelope`, `Voice Filter` et `Master
Output`. Le thread de contrôle valide la topologie puis compile les paramètres en
un `PolySynthGraphPlan` immutable publié atomiquement aux 16 voix du synthétiseur.

Chaque voix exécute sa forme d'onde, son enveloppe et son filtre passe-bas sans
allocation ni verrou dans le callback audio. Les graphes et paramètres sont
indépendants par piste MIDI, changent avec la sélection de piste et sont persistés
dans le format projet v24. L'ancien nœud `poly-synth` est uniquement conservé comme
migration des projets antérieurs et produit un plan par défaut compatible.

## Présentation nodale unifiée 0.41

Chaque graphe possède désormais un `ModuleInterfaceState` racine en plus de
l'interface attachée à chacun de ses nœuds et modules composites. Ce document est
la source de vérité commune : la vue compacte intégrée au nœud, l'éditeur flottant,
une instance de sous-graphe et la façade affichée dans la chaîne de périphériques
consomment les mêmes paramètres, contrôles normalisés et bindings.

Un geste effectué sur n'importe laquelle de ces surfaces met d'abord à jour le
document nodal, puis utilise le chemin de publication de paramètres existant vers
le plan DSP immutable. Waveform, spectre et niveaux reçoivent quant à eux la même
`AudioAnalysisFrame` bornée après le synthé ou le DSP de piste. La présentation
racine est sérialisée dans le schéma de graphe 6 ; les graphes antérieurs la
reconstruisent automatiquement depuis leurs paramètres publiés.

## Interfaces modulaires et ports publiés 0.42

`ModuleInterfaceState` utilise maintenant le schéma 2 et conserve deux listes de
contrôles indépendantes. Le layout `graph` est un canevas libre destiné à l'édition
du module ; le layout `device` est une façade compacte contrainte au ratio de la
chaîne de périphériques. Une copie initiale peut être créée du premier vers le
second, mais les modifications suivantes restent indépendantes.

Les frontières d'un graphe ne sont plus implicites. Des nœuds publiés distincts
déclarent autant d'entrées et de sorties Audio, MIDI, Valeur/Control et Transport
que nécessaire. Ils participent à la validation typée des connexions et sont
exposés dans l'interface uniquement lorsqu'un contrôle `inputPort` ou `outputPort`
correspondant est présent. Des presets ajoutent les paires usuelles sans empêcher
leur déplacement, leur suppression ou leur recomposition.

Le schéma de graphe 7 sauvegarde cette topologie ainsi que les deux présentations.
La position des contrôles de port dans le layout libre sert aussi d'ancrage aux
câbles visibles. Le mode `FREE UI` autorise déplacement et redimensionnement
directement dans les nœuds, tandis que le mode normal conserve la priorité aux
gestes de connexion.

## Façade automatique et navigation du graphe 0.43

Le schéma 3 de `ModuleInterfaceState` ajoute `showInDevice` à chaque paramètre
publié. La présentation complète demeure un canevas libre, mais la liste
`deviceControls` devient un cache déterministe : elle est régénérée en grille à
partir des seuls paramètres exposés. Les ports et visualisations restent dans
l'interface complète et la façade compacte n'a donc plus besoin d'un second
layout édité manuellement.

La chaîne de périphériques utilise cette façade automatique et fournit un bouton
`UI` pour ouvrir l'éditeur complet du graphe interne, parallèle au bouton utilisé
pour l'éditeur isolé d'un VST. Le mode Graphe de la piste conserve parallèlement
la topologie typée Audio/MIDI/Valeur/Transport et les connexions manuelles.

Le schéma de graphe 8 persiste enfin `viewZoom` et le décalage de caméra. La
transformation est appliquée aux nœuds, ports, câbles, contrôles intégrés et à la
grille ; le zoom est centré sur le pointeur et le panoramique utilise le bouton
central sans modifier les positions logiques du graphe DSP.

## Tranches de piste et insertion nodale 0.44

Le lanceur partage maintenant chaque piste avec une tranche verticale compacte :
scènes, résumé de la chaîne, I/O, gain/pan, états M/S/R et analyse stéréo. Les
niveaux sont copiés depuis les files `AudioAnalysisFrame` déjà consommées par
l'interface ; aucun lecteur, verrou ou calcul graphique n'est ajouté au callback
temps réel. La sortie Master possède une tranche de lecture séparée alimentée par
la file d'analyse du bus final.

Le bouton `+` sélectionne d'abord la piste propriétaire puis ouvre le navigateur
dans une catégorie combinant instruments, effets et plugins. Pour un device nodal,
`insertFactoryDevice` repère la branche atteignable depuis l'entrée audio, remplace
la dernière connexion vers la sortie par deux connexions entourant le nouveau
nœud, resynchronise l'interface publiée et republie le plan DSP. Les VST suivent
toujours le chemin distinct du worker isolé par instance.

## Mixage Master et graphes de modulation 0.45

La tranche Master est ancrée au bord droit du lanceur, indépendamment du nombre de
pistes. Son fader non linéaire partage les graduations +12, 0, -12, -30 et -60 dB
avec les tranches de piste ; les deux barres de niveau occupent toute la hauteur
disponible et distinguent RMS, crête et dépassement. La saisie directe et le glisser
publient le même gain atomique dans `MasterOutputAudioSource`, avant l'analyse et la
capture : l'écoute, le vumètre et l'export voient donc exactement le même signal.

Le panneau inférieur possède désormais trois modes. `Devices` construit la façade
compacte depuis les paramètres publiés du graphe de piste, `Graph` expose sa
topologie Audio/MIDI/Value/Transport, et `Modulation` charge un second document
nodal par piste. Ce dernier filtre la bibliothèque sur les nœuds Value et crée par
défaut `LFO -> Published Value Output`. LFO, Macro et calcul Value utilisent le
même payload de paramètres, la même interface libre, le même zoom et le même format
de sous-graphe que les effets audio.

Chaque paramètre natif publié conserve une valeur de base, des bornes, une profondeur
bipolaire et un drapeau d'assignation. Le thread de contrôle évalue la sortie Value
à 30 Hz en temps de transport, construit les valeurs absolues bornées puis appelle
les setters atomiques des processeurs natifs ; aucune analyse de JSON, allocation
ou recherche de binding n'entre dans le callback audio. Une assignation désactivée
republie sa base, ce qui évite qu'un LFO arrêté laisse le paramètre à sa dernière
phase. Le projet v26 persiste graphe et assignations par piste, tandis que le schéma
de graphe 9 marque explicitement les documents Value et conserve leurs paramètres.

## Déclenchement musical du lanceur 0.48

Les cases du lanceur séparent désormais la sélection de l'action : le corps d'un
clip sélectionne sa piste, tandis que son petit triangle est l'unique zone de
lancement. Une case vide ne devient enregistrable qu'après armement de sa piste ;
elle affiche alors un déclencheur rond, cohérent avec le bouton d'armement de la
tranche. La capture réutilise la région source de la piste, crée le clip de scène
et le place immédiatement en attente.

Le contrôleur valide d'abord la présence du clip, démarre le transport global s'il
était arrêté, puis calcule une seule échéance depuis le tempo et la quantification
de lancement. Une scène partage cette échéance entre toutes ses pistes. L'interface
interroge les scènes en attente et actives à chaque rafraîchissement d'analyse : la
bordure orange bascule ainsi vers l'état actif sans reconstruire tout le lanceur.

## Enregistrement de scène et sources indépendantes 0.49

Une case du lanceur ne dépend plus du fichier principal de sa piste. Chaque
`LauncherClip` conserve son propre chemin source, son offset et sa durée. Le
`ClipAudioSource` prépare un plan de lecture par scène sur le thread de lecture
anticipée ; le callback ne reçoit qu'un indice de scène et bascule vers le plan déjà
prêt. Cette organisation accepte donc des fréquences d'échantillonnage différentes
entre arrangement et lanceur, sans ouverture de fichier ni allocation temps réel.

Le déclencheur rond d'une piste armée ouvre directement un WAV dans le dossier
`Recordings`. `InputRecorder` conserve deux compteurs atomiques exprimés en samples :
le premier ignore précisément le pré-roll jusqu'à la frontière de quantification,
le second borne le dernier bloc lorsque l'utilisateur demande l'arrêt. Le writer
reste sur son thread disque ; à la fin, l'interface ferme le writer, valide le WAV,
publie la source dans la case puis programme sa lecture sur la grille suivante.
Les états attente, enregistrement et arrêt en attente sont visibles dans la case et
la barre de statut. Le schéma projet 27 sauvegarde les chemins de ces sources de
façon relative et restaure automatiquement les anciens lanceurs liés à la piste.

## Insertion directe dans le lanceur 0.50

Le navigateur publie désormais un payload interne typé pour chaque sample. Le
lanceur est à la fois cible de ce payload et cible des fichiers audio natifs du
système ; il résout en continu la paire piste/scène sous le pointeur et ne montre
la surbrillance que lorsqu'une vraie case peut recevoir le fichier. Le décodage du
payload est centralisé et testé afin qu'un nœud de graphe ou une description vide
ne puisse jamais être interprété comme un chemin audio.

Une insertion valide lit seulement les métadonnées sur le thread d'interface,
puis appelle le même `setLauncherClip` que l'enregistrement. Le contrôleur prépare
donc à nouveau le plan de lecture indépendant sur son thread de lecture anticipée ;
aucun accès fichier n'est ajouté au callback. Le clic droit d'une case utilise le
même chemin d'insertion : il cible temporairement le navigateur sur les samples,
et le prochain double-clic remplit précisément la paire piste/scène capturée. La
suppression est une action de menu distincte, ce qui évite qu'un clic droit vide
immédiatement un clip.

## Déplacement des clips du lanceur 0.51

Le corps d'un clip devient une source de glisser-déposer distincte de son triangle
de lecture. Son payload encode uniquement la piste et la scène d'origine ; le
contrôleur relit ensuite le `LauncherClip` autoritaire et ne transporte jamais un
pointeur ou un état audio dans l'interface. La destination est résolue par le même
hit-test que l'insertion de samples et reçoit une bordure orange.

`transferLauncherClip` prépare d'abord la destination avec la source, l'offset, la
durée, la boucle, le nom et la couleur d'origine. En mode déplacement, la source
n'est supprimée qu'après cette préparation réussie ; une erreur de reader laisse
donc la case d'origine intacte. `Ctrl`/`Cmd` garde la source et réalise une copie.
Les deux opérations restent entièrement hors callback : chaque nouvelle case publie
son plan préchargé par le chemin normal du contrôleur.

## Dépôt ciblé de périphériques 0.52

Les lignes Instrument, Effect et Plugin du navigateur publient maintenant des
payloads distincts des samples et des clips. Pendant le geste, le lanceur ne teste
que les rectangles de mini-chaîne des pistes audio et affiche la cible en violet ;
les cases et le Master ne peuvent donc pas recevoir accidentellement un device.
Les fichiers système `.vst3`, `.dll` et `.so` suivent le même routage.

À la réception, le workspace sélectionne d'abord la piste encodée par la cible,
ce qui restaure son graphe indépendant. Un device natif est ensuite inséré dans ce
graphe et republie son DAG ; un plug-in est confié au chemin existant
`setTrackPlugin`, donc reste isolé dans son propre worker et expose normalement ses
automations. La piste précédemment sélectionnée n'influence jamais la destination.

## Tri non destructif du navigateur 0.53

Le navigateur conserve l'index dans son ordre stable d'origine et trie uniquement
le vecteur d'indices filtrés affiché par la `ListBox`. Les modes Nom, Récent, Durée
et Taille utilisent les métadonnées déjà lues par l'indexeur : aucun fichier n'est
rouvert lorsqu'on change de tri. Les égalités retombent sur le nom puis le chemin,
ce qui rend le résultat déterministe sur Windows, Linux et macOS.

La ligne sélectionnée est d'abord reconvertie vers son indice autoritaire, puis
retrouvée après recherche, filtre d'emplacement et tri. Favoris, cible d'insertion
et préécoute restent ainsi associés au même sample. Seul l'identifiant du mode de
tri est persisté dans les préférences utilisateur.

## Cache d'index des samples 0.54

Après un scan réussi, le thread d'indexation sérialise jusqu'à 50 000 métadonnées
dans un document JSON de schéma 1 placé à côté des préférences de bibliothèque.
Chemin, racine, nom, format, durée, fréquence, canaux, taille et date suffisent à
reconstruire les lignes sans rouvrir un seul fichier audio au lancement suivant.

Le cache n'est accepté que si son schéma, ses limites numériques et l'ensemble
normalisé de ses racines correspondent aux emplacements actuels. Toute entrée
invalide rejette le snapshot complet au lieu d'afficher un index partiel ambigu.
L'interface charge le snapshot validé, puis lance toujours le scan asynchrone : les
fichiers ajoutés, modifiés ou supprimés convergent donc vers l'état réel, et seul le
worker remplace ensuite le cache. Retirer tous les emplacements supprime ce cache.

## Prévisualisation waveform du navigateur 0.55

La sélection d'un sample alimente un `AudioThumbnail` dédié, construit à partir
d'une source fichier indépendante de la préécoute. La lecture et la réduction de
la forme d'onde sont donc asynchrones ; le thread d'interface se limite à dessiner
les niveaux déjà mis en cache et à afficher les métadonnées de l'index.

La source du thumbnail est remplacée ou vidée à chaque changement de sélection,
filtre, tri ou rescan. L'identité reste celle de l'entrée autoritaire de l'index :
réordonner la liste ne peut pas associer la waveform au mauvais sample. Le cache
mémoire du thumbnail est borné à quatre sources et reste distinct du cache JSON de
la bibliothèque, qui ne contient jamais de données audio décodées.

## Audition positionnable du navigateur 0.56

Le contrat d'audition accepte désormais un offset en secondes. Le contrôleur ouvre
le reader hors callback, borne cet offset à la durée réelle du fichier, configure
le read-ahead puis démarre l'`AudioTransportSource`. La sortie continue de passer
par le mixeur d'audition indépendant du projet et de l'export Master.

Le navigateur convertit la coordonnée horizontale d'un clic dans la waveform en
temps source et redémarre l'audition à cette position. À 4 Hz, il lit uniquement la
position atomique du transport et redessine une tête de lecture orange ; aucune
lecture disque ni recherche dans l'index n'est effectuée par le timer graphique.

## Auto-préécoute et niveau d'audition 0.57

Le navigateur peut lancer automatiquement le sample dont l'identité autoritaire
vient de changer. Un tri ou un filtre qui conserve la même sélection ne redémarre
pas inutilement la lecture ; la fin naturelle du sample ne forme pas non plus une
boucle implicite. Le bouton d'écoute manuelle et le clic dans la waveform utilisent
le même chemin de démarrage.

Le gain d'audition est borné entre -48 et +6 dB puis converti en gain linéaire à
l'entrée de l'`AudioTransportSource`. Sa valeur atomique est mémorisée par le
contrôleur et réappliquée à chaque nouveau reader. Le mix Master, le projet et
l'export restent inchangés. Auto-préécoute et niveau sont enregistrés dans les
préférences de bibliothèque, séparément du projet `.cydaw`.

## Menu contextuel typé du navigateur 0.58

Le modèle de liste sélectionne d'abord la ligne visée, puis construit un menu à
partir de son type autoritaire. Un sample expose insertion ou ouverture, audition,
favori et actions de fichier ; un périphérique natif expose son insertion ; un
plug-in passe par la validation du worker isolé avant insertion. Les entrées sans
handler ou sans fichier réel sont désactivées au lieu d'échouer silencieusement.

L'activation est factorisée avec le double-clic : une case du lanceur ciblée reçoit
donc exactement le même sample, et un device conserve la piste déjà sélectionnée.
Le callback asynchrone du menu utilise un `SafePointer` vers le navigateur et une
copie immuable de la description d'item ; fermer la fenêtre pendant l'affichage du
menu ne laisse aucune référence pendante.

## Filtre de caractéristiques des samples 0.59

Le navigateur applique une facette supplémentaire après la catégorie et avant la
recherche textuelle. Les modes Tout, Mono, Stéréo, ≤2 s, 2–16 s et >16 s utilisent
exclusivement `channels` et `durationSeconds` déjà présents dans `SampleMetadata`.
Changer de facette reste donc une opération sur le vecteur d'indices filtrés : ni
reader, ni thumbnail, ni fichier du cache n'est rouvert.

Une facette autre que Tout masque les devices et plug-ins lorsque la catégorie
globale est active, pour que le résultat représente sans ambiguïté des samples.
La sélection autoritaire est conservée si elle reste visible, sinon waveform et
actions sont vidées. L'identifiant borné du filtre est persisté avec les autres
préférences de bibliothèque.

## Presets de graphe dans le navigateur global 0.60

Dix chaînes d'usine complètes sont maintenant des items `graphPreset` du navigateur
global : canaux propre/chaleureux, finition studio, voix, espaces parallèle et
rythmique, ainsi que deux générateurs. Elles utilisent les mêmes clés stables que le
catalogue interne de `NodeGraphComponent`, donc aucune seconde définition DSP n'est
dupliquée. Le double-clic et le menu contextuel passent par `insertFactoryDevice`,
qui reconnaît la clé du preset et applique sa topologie autoritaire.

Le drag-and-drop utilise `cydaw-graph-preset:` au lieu de `cydaw-node:`. Le lanceur
ignore ainsi un graphe complet, tandis que le canevas l'accepte et encadre toute sa
surface en orange avec un message de remplacement. À la réception, le preset est
validé avant que l'ancien graphe soit effacé ; son application republie ensuite le
DAG par le handler de layout normal. Un payload vide ou croisé avec celui d'un nœud
est rejeté par les tests de décodage typé.

## Presets de graphe utilisateur 0.61

Un graphe complet peut être exporté dans un document JSON versionné
`.cydawgraph`. L'enveloppe conserve un nom borné et l'état `NodeGraphComponent`
autoritaire ; la limite de 4 Mio empêche un preset de monopoliser le thread UI.
Le chargeur valide schéma, nombre de nœuds/modules/connexions, identifiants uniques,
types déclarés et extrémités des connexions avant toute mutation. Il garde ensuite
un snapshot du graphe courant et le restaure si la désérialisation n'applique pas
intégralement la topologie attendue.

`LibraryBrowserComponent` indexe `Documents/CyDAW/Graphs` sans récursion et expose
les documents valides comme `userGraphPreset`. Le double-clic appelle le même
chargeur transactionnel ; le drag-and-drop utilise le payload de fichier typé
`cydaw-graph-file:` afin de rester distinct des nœuds et presets d'usine. Le callback
de sauvegarde rafraîchit immédiatement cette collection sans rescanner les samples.

## Sous-graphes éditables et Poly Sine 0.46

Les modules composites possèdent maintenant une topologie interne persistante,
des types d’entrée/sortie explicites et une interface publiée. Le menu contextuel
d’un nœud sépare l’ouverture du graphe interne de l’éditeur de présentation ; un
double-clic sur un module ouvre directement sa composition. Les contrôles JUCE
intégrés relaient aussi le clic droit au canevas propriétaire, ce qui évite qu’un
potentiomètre masque le menu du module.

L’éditeur de sous-graphe utilise le même `NodeGraphComponent` que la piste : ajout,
suppression, câblage typé, zoom, panoramique, création d’une nouvelle composition
et édition des paramètres restent disponibles. Chaque changement est fusionné
dans la bibliothèque partagée puis recompilé dans toutes les instances sans
remplacer les définitions personnalisées par une copie d’usine périmée. Le schéma
de graphe 10 conserve les bornes Audio, MIDI, Value ou Transport de chaque module.

`Poly Sine` est désormais un module d’usine réellement composé de six nœuds :
entrée MIDI publiée, allocation de voix, oscillateur polyphonique, ADSR, filtre de
voix et sortie audio publiée. Le compilateur d’instrument aplatit récursivement les
modules imbriqués avant de valider le chemin MIDI→audio et de produire le plan de
rendu immutable. Les anciens graphes contenant le nœud Poly Synth monolithique
restent acceptés pour compatibilité.

## Bus audio virtuels 0.72

`VirtualBusRouter` est placé entre le mixeur de session et l'`AudioSourcePlayer`,
et reçoit aussi les entrées brutes du `AudioDeviceManager`. Son modèle distingue
les bus d'entrée et de sortie, chacun limité actuellement à mono ou stéréo. Une
entrée référence deux indices de canaux physiques ; une sortie référence deux
indices de destination. Le nom présenté par le pilote est conservé dans l'UI,
mais l'indice reste la donnée persistée afin de garder un plan de rendu compact.

Chaque bus porte uniquement son routage, un gain, un mute et un graphe nodal
indépendant. Ce graphe ne contient aucun effet imposé : son état initial relie
directement l'entrée audio à la sortie audio. Compresseur, EQ et autres traitements
sont des nœuds ajoutés volontairement par l'utilisateur. Un bus d'entrée peut
aussi être monitoré directement. L'enregistreur reçoit une
copie du bus sélectionné par la piste armée et écrit le signal routé et traité,
jamais l'ensemble implicite des entrées de la carte son. Le Master traverse les
bus de sortie et peut être envoyé vers plusieurs paires physiques. Les vu-mètres
sont des pics atomiques publiés vers l'interface à 20 Hz.

L'interface modifie un vecteur sous verrou uniquement sur le thread de contrôle,
puis publie un `shared_ptr<const vector<VirtualAudioBus>>` atomique. Le callback
charge ce snapshot immutable et travaille dans deux buffers préalloués ; aucun
verrou, fichier, composant graphique ni reconstruction de graphe ne se trouve sur
le chemin audio. Le projet version 36 persiste la matrice complète, le graphe de
chaque bus et l'identifiant du bus d'entrée de chaque piste. Le chargement normalise les plages, supprime les
identifiants invalides ou dupliqués et garantit toujours au moins une entrée et
une sortie.

Ces bus sont internes à CyDAW. `exposeToSystem` est une intention persistée, pas
un faux périphérique : JACK/PipeWire pourra publier les ports directement sous
Linux, tandis que Windows demande un pilote audio virtuel signé et macOS une
extension CoreAudio. Cette séparation permet de tester dès maintenant le routage
et le DSP sans prétendre qu'un autre logiciel peut déjà ouvrir ces ports.

## Instances Graph locales et édition live 0.74

Un Graph créé depuis une piste est enregistré comme module composite
`inlineTrack` dans le document de cette piste. Son identifiant UUID évite toute
collision avec les assets factory ou utilisateur. La fusion de bibliothèque
conserve les modules `inline` du document cible et ne partage que les autres
définitions ; changer de piste restaure ainsi l'instance et sa définition locale
exactes.

L'éditeur enfant d'un module publie deux classes de mutations. Une mutation de
structure remplace la définition du module, réinfère ses ports, revalide le DAG
et prépare un nouveau plan immutable. Une mutation de paramètres conserve la
topologie et appelle le chemin de mise à jour atomique des processeurs actifs,
avec recompilation de secours si la forme du graphe a réellement changé. Pour
les instruments, la définition imbriquée est aplatie puis transformée en
`PolySynthGraphPlan`. Pour l'audio, le DAG est appliqué au traitement de la piste
qui reçoit déjà la sortie de ses clips du lanceur ou de l'arrangement.

La création automatique ne cache aucun traitement : les effets audio et MIDI
commencent par un simple passage entrée→sortie, le modulateur publie une Value et
le modèle instrument expose explicitement ses six primitives. La recherche de
nœuds utilise le même registre de primitives/modules que la compilation, trié et
filtrable par catégorie ; une palette persistante pourra donc réutiliser ce
catalogue sans créer un second système de types.

## Contrats de frontière et gestionnaire de Graph 0.76

`NodeGraphComponent` sépare maintenant les ports publics d'une définition, les
primitives atomiques et les instances de sous-graphe. Les ports d'entrée/sortie
ne possèdent ni présentation de device ni graphe enfant. Leurs contrats Audio
conservent un nombre de canaux fixe de 1 à 8 et la validation refuse une liaison
directe entre formats incompatibles. Cette information, l'alias local d'une
instance et l'état de verrouillage d'un sous-graphe sont persistés par le schéma
d'état 13 et propagés lors de l'édition d'un module imbriqué.

L'éditeur de Graph réserve le panneau gauche à la sélection courante et le
panneau droit à la configuration de la définition éditée ainsi qu'aux
métadonnées stables du nœud. L'alias est donc éditable sans réécrire le nom de
type. Le menu contextuel donne accès à la navigation dans un sous-graphe, à son
verrouillage, au format des ports Audio et aux opérations structurelles. Le LFO
d'usine utilise le même chemin : il est une définition composée d'une primitive
LFO et d'une sortie Value, pas une primitive monolithique cachée.

`GraphManagerComponent` présente le catalogue partagé sous deux projections.
L'arbre classe presets d'usine, fichiers utilisateur et sous-graphes par
catégorie et rôle ; l'explorateur parcourt récursivement
`Documents/CyDAW/Graphs` avec un filtre `.cydawgraph`. Recherche, détails,
double-clic et menu contextuel appellent les mêmes callbacks de chargement ou
d'ouverture, ce qui évite une seconde logique de bibliothèque.

Chaque asset transporte aussi une projection légère de sa topologie destinée au
gestionnaire. Elle ne contient aucun processeur ni état audio : uniquement les
identifiants, positions, libellés, couleurs et extrémités des connexions. Les
presets d'usine et classes réutilisables la construisent depuis leurs structures
en mémoire ; les fichiers utilisateur la reconstruisent depuis le document
`.cydawgraph`. Une miniature commune peut ainsi présenter le flux typé sans
instancier un second `NodeGraphComponent` ni compiler de DSP dans l'interface.

## Interface Graph éditable et télémétrie multicanal 0.78

`ModuleInterfaceState` utilise désormais le schéma 13. Les migrations conservent
les anciens layouts, tandis que les nouveaux contrôles ajoutent interrupteurs,
sélecteurs, pavés XY à deux bindings, affichages de valeur liés, affichage d'enveloppe et panneaux de groupe aux potentiomètres, faders,
waveforms, spectres, textes et vumètres existants. Les ports Audio et les
vumètres portent le même contrat explicite de 1 à 8 canaux que le Graph DSP.

L'éditeur d'interface sépare un canevas normalisé d'un inspecteur permanent.
Une sélection expose son type, son binding, son libellé et sa géométrie exacte ;
le glisser, les huit poignées, les nudges clavier et les champs numériques
modifient tous la même source de vérité. La grille portable est configurable,
et le menu d'ajout est structuré en contrôles de paramètres, visualiseurs,
éléments de layout et frontières typées. La façade `device` reste générée depuis
les paramètres publiés afin d'éviter une seconde définition divergente.

Le pavé XY publie ses axes X/Y comme deux identifiants de paramètres stables.
Sa valeur, ses gestes et son double-clic de remise à zéro sont synchronisés entre
l'interface libre, la représentation intégrée au nœud et le contrat exportable
VST3. L'historique du layout groupe chaque déplacement ou réglage continu dans
une seule opération annulable afin de rester fluide.

L'enveloppe n'est plus un dessin décoratif : le contrat d'interface transporte
quatre identifiants stables A/D/S/R, exige qu'ils soient distincts, et propage
les gestes interactifs au DSP, aux instances de Graph et à l'export VST3.

`AudioAnalysisFrame` transporte des crêtes et niveaux RMS bornés pour huit
canaux. Le même snapshot SPSC alimente la fenêtre d'interface, la représentation
intégrée au nœud et la chaîne de périphériques sans accès concurrent aux buffers
du callback audio.
