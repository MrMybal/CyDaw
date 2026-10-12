# CyDAW

> **CyDAW 2** — une nouvelle interface compacte en Dear ImGui (Direct3D 11) sur le même moteur et
> les mêmes projets `.cydaw` : cible `CyDAW2`, voir [docs/V2_INTERFACE.md](docs/V2_INTERFACE.md).
> L'interface JUCE ci-dessous (v1) reste compilable.

CyDAW 0.77.0 est un DAW multiplateforme orienté performance, création musicale et
traitement audio détaillé. Son identité repose sur quatre surfaces qui partagent
le même moteur : arrangement/launcher/mixer, éditeur audio destructif et
non destructif, graphe nodal d'instruments/effets, et constructeur d'interfaces.

Le dépôt contient aujourd'hui un **MVP desktop audio utilisable**, ainsi que le
socle du futur moteur nodal :

La révision 0.77.0 transforme les Graphs en véritables assets réutilisables. Le
panneau de configuration sépare les détails de la sélection de l'identité du
Graph courant et expose son nom, sa catégorie hiérarchique, son auteur, ses tags,
sa description et sa version sémantique. Un Graph peut être forké sans écraser
son contrat d'origine, sauvegardé directement après chargement ou dupliqué depuis
le manager avec une nouvelle identité stable. Le manager édite ces métadonnées de
manière atomique, recherche aussi par auteur et organise les catégories imbriquées.
Le format `.cydawgraph` reste compatible avec les versions précédentes et porte
désormais ces métadonnées jusqu'au runtime et à l'export VST3.

Le ruban de périphériques de piste adopte maintenant une présentation compacte
orientée signal. Entrée, Graph interne, traitements natifs, chaque instance VST3,
mixage et sortie occupent des cartes distinctes dans leur ordre audio réel. Les
VST3 ne sont plus masqués derrière une liste : une carte affiche directement son
état, sa latence, ses incidents éventuels et donne accès à son interface par
double-clic. Les contrôles publiés d'un Graph forment de la même manière un vrai
périphérique interne, tandis qu'un emplacement `+` ouvre le navigateur au point
d'insertion. Le ruban garde des cartes de largeur fixe et défile horizontalement
au lieu de compresser les contrôles lorsqu'une chaîne devient longue. Les VST3
peuvent être réordonnés par glisser-déposer, par menu contextuel ou avec
`Alt+Gauche/Droite`; `Entrée` ouvre leur interface et `Suppr` les retire. Le ruban
reste aligné à gauche et laisse volontairement de la place pour prolonger la
chaîne, comme sur une surface de devices dédiée.

Le jalon 0.77 comprend aussi un panneau `MEMBRES` inspiré des éditeurs de type
MetaSound. Il sépare le contrat public du Graph de ses primitives internes et
réunit les entrées typées, les sorties typées et les paramètres publiés. Un clic
sélectionne le nœud associé et ouvre ses détails modifiables ; un double-clic le
cadre dans le canevas. Le clic droit permet de gérer l'exposition d'un paramètre
dans le périphérique et vers l'hôte/VST3, ou de retirer un port public. Le
filtre intégré retrouve ces membres par nom, type ou exposition. Le gestionnaire
de Graph sélectionne maintenant un asset dès son ouverture, conserve
ce choix après actualisation et affiche immédiatement son rôle, ses contrats et
son état de validation.

L'interface du Graph Editor renforce aussi la lecture des grands patchs : les
panneaux ouverts ont un état actif et une poignée de redimensionnement visible,
la barre d'outils sépare clairement navigation, historique et disposition, et
chaque carte porte un badge de signal (`MIDI`, `VAL`, `TR` ou nombre de canaux
audio). Lorsqu'un ou plusieurs nœuds sont sélectionnés, leurs câbles restent au
premier plan tandis que les liaisons étrangères sont atténuées. La barre d'état
résume les nœuds, connexions et éléments sélectionnés ; la mini-carte affiche les
mêmes compteurs et partage exactement sa zone de navigation avec le pointeur.
La palette permanente affiche maintenant son nombre de résultats, regroupe ses
éléments dans des cartes plus lisibles et suit le port typé sélectionné : les
nœuds incompatibles sont atténués et un filtre optionnel ne conserve que les
primitives ou Graphs réellement connectables, y compris selon le nombre exact
de canaux audio.

Les nœuds du Graph Editor utilisent désormais une véritable échelle d'édition
au lieu d'une présentation en miniature. Une primitive simple part de
224 × 164 pixels, les cartes multiports disposent d'une largeur supérieure et
les interfaces publiées peuvent s'agrandir automatiquement pour préserver des
knobs, sélecteurs, enveloppes, waveforms, spectres et vu-mètres lisibles. Titres,
badges de signal, ports typés et noms de sockets restent visibles plus longtemps
pendant le dézoom, tandis que les vrais contrôles demeurent interactifs jusqu'à
66 %. La fenêtre Graph détachée s'ouvre par défaut en 1440 × 860 et migre
automatiquement l'ancien format compact 980 × 360.

La disposition automatique du Graph est maintenant dimensionnelle : elle
mesure chaque carte et son interface publiée, aligne les colonnes selon le flux
du signal et réserve un espacement réel entre les branches. Un patch trop grand
peut s'étendre au-delà du viewport et reste accessible par déplacement et
mini-carte ; il n'est plus tassé jusqu'à rendre ses contrôles illisibles. Les
presets et sous-graphes d'usine utilisent automatiquement cette présentation à
leur ouverture. Le cadrage d'une sélection conserve aussi le seuil de zoom des
contrôles interactifs, et la grille distingue désormais clairement subdivisions
et lignes principales.

La révision 0.76.5 introduit les presets portables de paramètres de nœuds
`.cydawnode`. Une primitive peut sauvegarder puis restaurer ses paramètres DSP et
les valeurs de repli de ses entrées Value/Trigger sans modifier son identité, sa
position ni ses câbles typés. Ces presets sont accessibles depuis l'inspecteur,
le menu contextuel et le gestionnaire de Graphs ; celui-ci vérifie leur
compatibilité avec le nœud sélectionné avant application et rescane réellement
les bibliothèques utilisateur. Les contrôles intégrés au canevas disposent aussi
d'infobulles dynamiques indiquant valeur, plage, unité, valeur par défaut et
pilotage éventuel par un câble. Tous les boutons visibles du Graph Editor ont
désormais une aide contextuelle en anglais et en français.

La révision 0.76.4 renforce l'édition de Graphs complexes : les nœuds peuvent être
repliés sans perdre leurs ports typés ni leur position visuelle, la sélection peut
suivre récursivement le signal en amont, en aval ou sur toute une chaîne, et un
câble routé peut être redressé en une action. L'aperçu temps réel partage désormais
les mêmes opérateurs que le runtime natif ; Racine carrée, Plancher, Plafond et
Arrondi complètent ainsi Modulo, Puissance, Sinus et Cosinus dans la bibliothèque
Float, jusque dans le VST3 exporté.

La révision 0.76.3 poursuit la transformation du Graph Editor en environnement
d'édition modulaire. Quatre dispositions nommées (`AUTHORING`, `FOCUS CANVAS`,
`INSPECT SELECTION` et `BROWSE LIBRARY`) réorganisent les panneaux redimensionnables,
une disposition personnelle peut être sauvegardée, et le panneau droit sépare
désormais clairement la configuration du Graph de sa palette. `F` cadre la
sélection, `Tab` ouvre la recherche de nœuds, `Entrée` descend dans le sous-graphe
sélectionné et `Alt+Gauche` remonte dans sa hiérarchie. Les sélections multiples,
le rectangle de sélection et le copier-coller conservent leur topologie et leurs
valeurs de ports. Enfin, Modulo, Puissance, Sinus et Cosinus rejoignent la
bibliothèque des primitives Float : ils sont compilés, sérialisés et exécutés par
le runtime temps réel, y compris dans le VST3 exporté. Le canevas accepte aussi
des coordonnées étendues au-delà de la fenêtre, avec une mini-carte interactive
persistante pour naviguer dans les grands Graphs. Les câbles peuvent maintenant
être tirés depuis une sortie ou une entrée ; le navigateur contextuel filtre les
nœuds compatibles, les connecte dans le bon sens et remplace proprement une
liaison d'entrée existante. Les flèches déplacent enfin la sélection avec précision
(`Maj` pour un pas de grille). Des cadres de commentaire colorés peuvent entourer
et déplacer une section complète du Graph. Neuf repères de vue persistants
(`Ctrl/Cmd+1…9`, `Ctrl/Cmd+Maj+1…9` pour enregistrer) accélèrent la navigation,
et les câbles acceptent des points de routage déplaçables, sérialisés mais
strictement exclus du plan DSP.

La révision 0.76.2 ajoute un historique transactionnel au Graph Editor : les
ajouts, suppressions, connexions, déplacements et paramètres disposent de
boutons Annuler/Rétablir et des raccourcis standard, tandis que les gestes
continus sont regroupés et que le zoom ne pollue pas l'historique. Le câblage
typé prévisualise désormais les destinations compatibles, les incompatibilités
de format et les cycles. Une commande d'organisation topologique aligne les
nœuds des entrées vers les sorties et corrige automatiquement les chevauchements
créés lors de l'insertion d'un device. Le gestionnaire de Graph offre aussi une
hiérarchie portée/type/catégorie et une recherche active dans les fichiers.

La révision 0.76.1 rapproche l'éditeur Graph d'une organisation MetaSound :
panneaux `Détails` et `Palette/Configuration` masquables et redimensionnables,
barre locale de zoom/cadrage/validation, descriptions éditables utilisées comme
tooltips et contrôles simplifiés lorsque le canevas est fortement dézoomé. Les
ports Audio Mono, Stéréo et 3 à 8 canaux sont maintenant des types de contrat
distincts jusque dans le cœur indépendant de l'interface. Enfin, le nouveau
planificateur `Graph -> VST3` valide l'identité du binaire, les bus exacts, les
paramètres publiés et l'empreinte du programme avant génération d'un wrapper.
Un premier `CyDAW Graph Runtime.vst3` charge déjà le même canvas et le même
moteur DSP que CyDAW : son graphe est éditable dans le plug-in, traité sans
allocation dans le callback et restauré avec le projet du DAW hôte. Les
changements de paramètres utilisent les valeurs atomiques du plan existant,
sans recompilation de la topologie, sur des bus de 1 à 8 canaux.
L'architecture de l'éditeur autonome et de l'export est décrite dans
[`docs/GRAPH_EDITOR_VST_ARCHITECTURE.md`](docs/GRAPH_EDITOR_VST_ARCHITECTURE.md).

Le jalon 0.76 clarifie le modèle d'édition de Graph. Les entrées et sorties du
Graph sont désormais des ports de frontière spécialisés, sans façade de device,
et sont distinctes des primitives DSP et des sous-graphes réutilisables. Les
contrats audio portent un format explicite de mono à huit canaux et refusent les
liaisons incompatibles. Chaque nœud peut recevoir un alias local, tandis qu'un
sous-graphe s'ouvre au double-clic ou depuis son menu contextuel et peut être
verrouillé en lecture seule. Le LFO d'usine est lui-même un sous-graphe éditable.
L'inspecteur de gauche décrit la sélection ; le panneau droit expose le nom, la
catégorie, l'identité et les contrats du Graph ou du nœud édité. Enfin, le
gestionnaire de Graph propose une arborescence par catégories et un explorateur
des fichiers `.cydawgraph` de `Documents/CyDAW/Graphs`. Son panneau de détails
dessine maintenant un aperçu miniature de la topologie : nœuds, direction du
flux et couleurs des contrats Audio, MIDI, Valeur et Transport restent lisibles
avant même d'ouvrir ou d'appliquer l'asset.

Le jalon 0.75 complète le socle de primitives de Graph avec des traitements
audio exécutables (gain, passe-haut, écrêteur, largeur stéréo, suppression DC et
somme audio), des opérateurs Value/Float et logiques, des transformations MIDI
(transposition, filtre de notes et vélocité), ainsi que des visualisations
waveform, spectre, niveau, valeur et événements MIDI. Les paramètres audio sont
modifiables à chaud et les transformations MIDI sont compilées dans le plan du
synthé polyphonique. Le registre typé conserve les contrats multiports destinés
à la prochaine évolution visuelle de l'éditeur.

Le jalon 0.74 rend la création de Graph directement testable dans le contexte
d'une piste. Le bouton `+ GRAPH` ou le menu contextuel du canevas crée un effet
audio, un instrument polyphonique, un effet MIDI ou un modulateur comme module
local à la piste, l'insère dans le chemin typé lorsqu'une connexion compatible
existe, puis ouvre son sous-graphe. Les modifications de structure republient un
plan DSP hors du callback audio ; les changements de paramètres utilisent la
mise à jour légère du plan actif. Le clip du lanceur ou de l'arrangement de la
piste reste donc la source d'écoute réelle pendant l'édition. Les Graph locaux
sont persistés avec leur piste sans contaminer les autres pistes. La recherche
de nœuds possède aussi un filtre de catégorie et un tri catégorie/nom inspirés
du navigateur Blueprint.

Le jalon 0.68 transforme les clips audio du lanceur en timelines locales éditables
et distingue explicitement `CLIP MIDI`, `CLIP AUDIO` et `FICHIER AUDIO`.
Un double-clic sur un clip audio de l'arrangement ou du lanceur ouvre désormais
sa timeline locale non destructive, et non l'éditeur du fichier source. Cette
timeline accepte plusieurs événements audio, leur déplacement, trim, découpe,
stretch et leurs ancres de warp, avec au moins trois lignes visibles. La bande
supérieure déplace l'événement et se découpe au double-clic, les bords centraux
retaillent, le corps crée une ancre de warp au double-clic et toute la bande
inférieure étire. Les éditeurs d'arrangement, de clip audio, de clip MIDI et de
fichier audio partagent aussi la navigation par la règle supérieure : glissement
horizontal pour déplacer la vue, vertical ou molette pour zoomer. Les snaps du
clip audio et du clip MIDI sont locaux et visibles dans leur barre d'outils.
`FICHIER AUDIO` reste l'éditeur distinct de la source, ouvert explicitement depuis
l'événement sélectionné. Les automations
de piste sont désormais repliées par défaut et s'ouvrent avec le bouton `A`.
Le Master reste ancré en bas et les pistes restent réordonnables et réglables en
hauteur.

La révision 0.68.1 corrige deux blocages du Launcher : les pistes MIDI ajoutées
avec `+ PISTE` restent désormais visibles dans le Launcher et le double-clic sur
une case MIDI vide crée bien un clip, même lorsque la piste est armée. La lecture
des timelines audio placées sur une piste vide conserve aussi la durée du clip au
lieu d'être tronquée à la source silencieuse interne. Les clips MIDI sont lus en
temps local et isolés des autres clips de la piste pendant leur préécoute.

Le jalon 0.69 clarifie les quatre espaces de travail `LANCEUR`, `ARRANGEMENT`,
`CLIP` et `GRAPH EDIT`, ainsi que la portée `MODULE`/`PISTE` du panneau inférieur.
Les éditeurs de clips audio et MIDI intègrent une vraie ligne d'automation de
piste assignable sous leur contenu sans remplacer les Périphériques ou le Graphe.
L'automation du gain des pistes MIDI est appliquée par le moteur, éditable,
triée et sauvegardée dans le projet. Le piano-roll se déplace horizontalement et
verticalement, zoome sur les notes et permet de choisir entre l'édition de la
vélocité et de la durée. Enfin, l'inspecteur suit réellement le contexte MIDI et
l'éditeur d'interface des modules propose huit poignées de redimensionnement
autour du contrôle sélectionné.

Le jalon 0.70 fixe la structure générale de l'espace de travail : le Lanceur,
l'Arrangement, les éditeurs de clips audio/MIDI et `GRAPH EDIT` partagent
désormais la même grande zone centrale. Le Graphe peut donc s'ouvrir sur place,
revenir à l'éditeur précédent ou être détaché dans une fenêtre indépendante. Les
`DÉTAILS DE LA SÉLECTION` à gauche et le `NAVIGATEUR` à droite possèdent des
commandes directes pour se replier et libérer la largeur centrale. Le navigateur
ajoute également une rangée de raccourcis persistants pour Tout, Samples,
Instruments, Effets et Graphes, tandis que le panneau inférieur conserve sa
portée explicite `MODULE`/`PISTE`.

La révision 0.70.1 rend le panneau gauche réellement contextuel : cliquer une
piste affiche les informations et commandes de cette piste, tandis que cliquer
un clip audio, un clip du Lanceur ou un clip MIDI affiche ses propres données.
Les panneaux gauche et droit réservent désormais une gouttière à leur commande
de repli, même lorsqu'ils sont masqués. Le panneau inférieur peut également être
replié. Dans les éditeurs de clips audio/MIDI, l'automation contextuelle est
masquable et sa hauteur se règle en temps réel. Enfin, la grille fixe qui se
superposait à la grille musicale dans l'éditeur de clip a été supprimée.

La révision 0.70.2 place le panneau Périphériques/Graphe sur toute la largeur de
l'espace de travail et regroupe à gauche ses commandes de repli et de portée
`MODULE`/`PISTE`. La barre principale sépare maintenant les menus
`FICHIER`/`ÉDITION`/`AFFICHAGE`, les actions classiques et les vues du logiciel.
Dans le Lanceur, chaque piste possède un arrêt global et son en-tête se glisse
horizontalement pour la réorganiser. L'éditeur de clip affiche des poignées
distinctes pour Début/Fin et Début/Fin de boucle, conserve la grille musicale
derrière la waveform et redessine celle-ci selon les ancres de stretch. Enfin,
le bouton d'automation reste toujours récupérable et l'inspecteur distingue la
piste, le clip et le sample sélectionné à l'intérieur du clip.

Le jalon 0.71 unifie les coordonnées temporelles des éditeurs audio, MIDI et
d'automation. Les automations suivent maintenant exactement le zoom et la
lecture du clip ; un double-clic ajoute un point et chaque segment peut être
linéaire ou Bézier avec une courbure réglable. Les clips MIDI disposent des mêmes
bornes Début/Fin et Début/Fin de boucle que les clips audio. Dans un clip audio,
la sélection d'un sample expose son gain non destructif, son BPM source, son
algorithme de resampling/stretch, sa durée source et sa durée sur la timeline.
Les ancres restent attachées au temps musical pendant le zoom, la waveform est
recalculée selon le stretch et les poignées de boucle suivent la souris avant la
reconstruction du plan audio.

La révision 0.71.2 recharge immédiatement les contrôles éditables du sample
sélectionné, ajoute la saisie numérique du temps et de la valeur d'un point
d'automation, et augmente la résolution des formes d'onde. Le retaillage du bord
droit conserve maintenant la transformation source/temps définie par les ancres
de stretch, afin que la waveform ne glisse plus pendant un trim ou un zoom.

La révision 0.71.3 remplace le rendu segmenté des samples warpés par une
transformation canonique temps du clip → temps source, commune aux ancres et à
la waveform. Les transitoires restent ainsi sous la même ancre à tous les
niveaux de zoom. Le résultat est mis en cache par vue et par état de stretch
pour conserver une navigation fluide pendant la lecture.

La révision 0.71.4 ajoute un second cache de waveform à haute définition,
chargé en arrière-plan uniquement lorsque le zoom rend le cache d'aperçu trop
grossier. Dans l'éditeur de clip audio, les boutons Lecture/Pause et la barre
espace commandent maintenant le clip édité sans arrêter les clips déjà lancés
sur les autres pistes. Le sélecteur `REPRENDRE`/`AU DÉBUT` choisit si une
nouvelle lecture conserve la position d'arrêt ou repart de la borne Début.

La révision 0.71.5 rend les gestes de stretch, les paramètres de sample et les
points/courbes d'automation audibles pendant leur déplacement. Le preview audio
est reconstruit en place en conservant sa position, et le plan actif du Lanceur
ne revient plus au début lors d'une modification. La waveform reflète maintenant
en temps réel le gain ainsi que les fondus d'entrée/sortie. Enfin, la tête de
lecture de l'automation suit exclusivement le transport local du clip et reste
figée avec lui lorsqu'il est arrêté.

La révision 0.71.6 restaure la fluidité des gestes de stretch : le déplacement
reste une prévisualisation graphique légère et le plan audio exact n'est
reconstruit qu'au relâchement de la souris. Les ancres, bords et poignées suivent
donc le pointeur sans reconstruire les lecteurs audio ni recalculer toute la
waveform à chaque pixel. La saisie clavier reste appliquée immédiatement.

La révision 0.71.7 conserve cette fluidité tout en rétablissant la prévisualisation
visuelle du stretch. Pendant le geste, le cache de waveform existant est déformé
par segments entre les ancres : les transitoires suivent donc l'ancre ou le bord
en temps réel, sans relire le fichier ni reconstruire le plan audio.

La révision 0.71.8 applique enfin les fondus d'entrée et de sortie des samples
au rendu audio du Lanceur, y compris dans l'éditeur local du clip. L'enveloppe
linéaire est calculée par sample sur tous les canaux avant le gain et
l'automation ; un test de rendu contrôle maintenant les deux extrémités du son.

La révision 0.71.9 porte le transport local complet dans l'éditeur de clips MIDI.
Lecture/Pause, Arrêt, retour au début et barre espace pilotent désormais le clip
édité avec le choix `REPRENDRE`/`AU DÉBUT`, sans interrompre les clips déjà lancés
sur les autres pistes. Les bornes Début/Fin et Début/Fin de boucle commandent
directement le moteur MIDI ; la tête de lecture et l'automation du clip suivent
la même horloge locale, y compris pendant les boucles, les déplacements et la
fin naturelle de la lecture. Les éditions de notes et d'automation restent
audibles dans cette préécoute, puis la séquence d'arrangement est restaurée à
l'arrêt.

La révision 0.71.10 complète les bornes temporelles des clips MIDI. Les valeurs
`DURÉE`, `DÉBUT`, `FIN`, `DÉBUT BOUCLE` et `FIN BOUCLE` sont maintenant éditables
numériquement en temps, avec validation et normalisation immédiates. La règle du
piano-roll expose les mêmes bornes dans trois bandes colorées : durée totale en
orange, lecture en cyan et boucle en violet. Toutes les poignées suivent la grille
musicale, mettent leurs valeurs à jour pendant le glissement et conservent la vue
stable jusqu'au relâchement de la souris. La grille d'automation est ensuite
resynchronisée avec la nouvelle durée du clip.

Le jalon 0.72 ajoute le gestionnaire de bus audio virtuels à la zone centrale.
Un bus d'entrée mono ou stéréo associe des canaux physiques nommés de la carte son,
un gain, un mute et un monitoring direct ; chaque piste
audio choisit ensuite son bus d'entrée au lieu d'utiliser implicitement tous les
canaux du périphérique. Chaque bus possède un graphe nodal indépendant, vide
d'effet par défaut (`Entrée audio → Sortie audio`). Tout traitement est ajouté
explicitement dans ce graphe et l'enregistrement reçoit le signal ainsi routé.
Les bus de sortie associent de la même façon le mix Master à une ou deux sorties
physiques et traversent leur propre graphe libre. Les niveaux
des bus sont visibles pendant la lecture ou le monitoring.

La matrice, les affectations de pistes et les graphes sont enregistrés
dans le projet `.cydaw` version 36. Le plan publié au callback audio est immutable :
les modifications de l'interface remplacent un snapshot complet sans verrou ni
allocation sur le chemin temps réel. L'option de publication système mémorise
l'intention d'exposer un port. La publication native JACK/PipeWire sera reliée
directement sous Linux ; Windows et macOS nécessitent encore respectivement un
pilote audio virtuel et une extension CoreAudio dédiés pour rendre ces ports
visibles dans les autres logiciels.

- modèle de graphe audio/MIDI/contrôle avec compilation topologique déterministe ;
- détection et refus des cycles implicites ;
- plan de rendu audio immutable avec buffers préalloués et mixage de branches ;
- transport positionné en samples et commandes SPSC sans verrou ;
- premiers processeurs natifs : passthrough, gain lissé et oscillateur sinus ;
- contrat des backends ASIO, WASAPI, CoreAudio, ALSA, PipeWire et JACK ;
- protocole versionné pour isoler les plugins dans des processus supervisés ;
- pont Linux pour VST Windows : wrappers yabridge dans un worker natif isolé,
  ou worker Windows direct configurable via Wine/UMU-Proton ; le pipeline CI
  construit ce worker avec le même protocole puis l'injecte dans le paquet Linux ;
- politique de redémarrage/quarantaine après crash ;
- insertion VST3 temps réel hors processus et VST2 legacy optionnelle, via mémoire partagée préallouée,
  avec bypass automatique, détection de crash et latence déclarée d'un bloc ;
- canal de contrôle isolé pour l'énumération/réglage des paramètres,
  l'état binaire persistant et l'éditeur graphique ouvert dans le worker ;
- chaîne ordonnée de huit plug-ins maximum par piste, avec un processus isolé par
  instance, réordonnancement sans redémarrage et latence cumulée ;
- automation temporelle des paramètres de plug-in par boîte aux lettres temps réel
  non bloquante, éditable dans la même vue que l'automation de gain ;
- compensation automatique de la latence des pistes et du synthé interne,
  recalculée lorsque la latence déclarée d'un plugin change ;
- application JUCE native avec session multipiste, mixeur et formes d'onde ;
- lecture synchronisée, navigation, boucle, gain, panoramique, mute et solo ;
- arrangement audio multiclips : jusqu'à 64 régions indépendantes par piste,
  sélection, déplacement, duplication, découpe et suppression directement dans la
  timeline ; déposez un autre sample directement sur une piste existante pour créer
  une région à la position visée. Chaque région conserve son propre fichier, sample
  rate, offset source, durée, nom et couleur ; les fichiers 44,1/48/96 kHz peuvent
  cohabiter, les trous produisent du silence et les chevauchements sont sommés
  sample-accurate. Les bords se retaillent directement, deux poignées supérieures
  règlent les fondus non destructifs et le menu de région construit automatiquement
  un crossfade avec la meilleure région superposée ; la zone `XFADE` affiche les
  deux courbes au-dessus des waveforms ;
- historique transactionnel de l'arrangement, borné à 32 états de session : un
  déplacement, un trim, un geste de fondu, un crossfade, une duplication, une
  découpe ou une suppression correspond à un seul pas `Ctrl+Z`/`Ctrl+Shift+Z`
  (`Ctrl+Y` est également accepté). Une nouvelle édition invalide le redo et le
  chargement d'un projet démarre avec un historique propre. Les opérations touchant
  plusieurs pistes sont restaurées atomiquement par le même pas d'historique ;
- grille musicale synchronisée au tempo avec magnétisme désactivable en 1/4,
  1/8, 1/16 ou 1/32 et trois comportements visibles : grille absolue, déplacement
  relatif et accrochage aux bords des événements voisins. Déplacement, trim,
  fondus, découpe, dépôt de fichier, curseur de règle et saisie directe du Start
  ou des durées utilisent la même quantification ; `Alt`
  la contourne temporairement pendant un geste. `Ctrl-clic` construit une
  sélection multiple, `Ctrl+A` sélectionne toutes les régions de la piste, puis
  déplacement, duplication et suppression agissent sur le groupe en une seule
  transaction annulable tout en conservant les écarts temporels. Un glisser depuis
  l'espace vide trace désormais une plage rectangulaire à travers plusieurs pistes,
  sélectionne toutes les régions croisées et affiche sa portée dans les pistes et
  la règle ; `Suppr` traite alors tout le rectangle en une opération atomique. Le
  bouton bilingue `RANGE/PLAGE` découpe aux deux bornes, rogne les régions à la
  plage ou retire seulement son contenu pour créer un silence, sur toutes les
  pistes sélectionnées avec un unique pas d'historique ;
- déplacement horizontal et vertical de blocs : une région, une sélection de piste
  ou un rectangle multipiste peut être glissé sur une autre piste. Le moteur borne
  le déplacement global, conserve fichiers, offsets, fondus et écarts relatifs,
  reconstruit chaque piste touchée une seule fois et annule tout le transfert par
  un seul `Ctrl+Z` ;
- presse-papiers d'arrangement multi-régions et multipiste : `Ctrl+C/X/V` copie,
  coupe et colle à la tête de lecture en conservant fichiers source, offsets,
  durées, fondus, couleurs, espacements temporels et écarts verticaux entre pistes.
  Un collage ou une coupe du bloc complet forme une seule transaction undo/redo ;
  les mêmes actions sont dans le clic droit et la palette globale ;
- cinq outils d'arrangement visibles : sélection, coupe, étirement temporel,
  loupe bidirectionnelle et main. La loupe zoome verticalement tout en déplaçant
  horizontalement le cadrage ; la main et le clic-molette déplacent la timeline
  horizontalement et la liste des pistes verticalement. Le double-clic d'une
  région ouvre sa timeline de clip relative et multi-événements ; son bouton
  `SOURCE` ouvre séparément l'éditeur du fichier audio. Le clic droit de l'en-tête renomme,
  colore et configure réellement M/S/R ;
- navigation horizontale d'arrangement synchronisée : molette pour zoomer sous le
  pointeur, `Maj+molette` et barre inférieure pour défiler, boutons `-`, facteur/TOUT
  et `+`, raccourcis `Ctrl+-`, `Ctrl+0`, `Ctrl++`, en-têtes de pistes fixes et suivi
  automatique du curseur pendant la lecture ;
- plage de boucle musicale commune à toute la session audio/MIDI : `Maj-glisser`
  dans la règle la crée sur la grille, ses deux poignées orange la retaillent et
  le menu de région peut l'ajuster à la sélection. Les pistes plus courtes avancent
  silencieusement jusqu'à la frontière commune puis toutes repartent au même
  sample ; activation, bornes et préférence explicite sont restaurées par le projet ;
- éditeur audio détaillé avec forme d'onde zoomable, sélection temporelle,
  trim/reset non destructifs et export de la sélection vers un nouveau WAV 24 bits ;
  tous les calculs s'exécutent sur le worker audio, restent annulables et ne
  bloquent pas l'interface ;
- traitements audio sample-accurate par sélection : fondus d'entrée/sortie,
  silence, inversion temporelle, gain réglable de -24 à +24 dB, vraie
  normalisation rendue à -1 dB, suppression de l'offset continu par canal,
  inversion de polarité et échange gauche/droite. Chaque opération crée une
  nouvelle source sans écraser l'original, préserve les samples hors sélection
  et dispose d'un historique undo/redo de 32 états ; toutes sont également
  recherchables dans la palette globale ;
- prise audio intégrée à l'arrangement : chaque piste expose un armement exclusif
  `R`, l'enregistrement mémorise la position exacte du transport, désactive
  temporairement la boucle et écrit le WAV 24 bits sur un thread disque distinct
  du callback. À l'arrêt, la prise devient une région éditable sur la piste armée
  avec un seul pas d'undo ; sans piste cible, CyDAW crée et arme automatiquement
  une nouvelle piste. `Échap` annule proprement la prise et supprime uniquement son
  nouveau fichier. Le périphérique et ses canaux actifs sont restaurés au prochain
  lancement, avec deux entrées demandées par défaut puis repli sûr en sortie seule ;
- prises alternatives par région pour préparer le comping : chaque région audio
  conserve jusqu’à 32 sources alignées et affiche `Tn/total` directement dans
  l’arrangement. L’inspecteur permet d’importer, d’enregistrer, d’activer ou de
  supprimer une prise sans pouvoir perdre la dernière source. Enregistrer une
  alternative arme la piste et place le transport au début exact de la région ;
  le changement actif modifie réellement le reader et la waveform. Ajout,
  sélection et suppression sont chacun annulables en une transaction, et toutes
  les alternatives utilisent des chemins de projet portables ;
- export du mix master stéréo en WAV 24 bits depuis la sortie exacte du moteur :
  pistes audio et MIDI, DSP nodal, plug-ins isolés, automation et compensation de
  latence sont capturés en temps réel, suivis de deux secondes de queue d'effets ;
  l'écriture utilise un FIFO préalloué sur un thread disque, affiche sa progression,
  détecte les blocs perdus et supprime proprement le fichier partiel si elle est annulée ;
- lanceur de clips à huit scènes : le triangle lance un clip, tandis que les cases
  vides n'exposent un bouton rond d'enregistrement que sur les pistes armées. Le
  premier clic attend la prochaine frontière BPM, enregistre réellement l'entrée
  audio et le second programme l'arrêt sur la grille. La prise devient une source
  autonome préchargée dans la case ; lancements individuels et scènes partagent la
  quantification libre, 1/4 temps, 1/2 temps, 1 temps ou 1 mesure. Les samples du
  navigateur et de l'Explorateur se déposent directement sur n'importe quelle case,
  avec surbrillance de la destination ; le clic droit ouvre un choix de sample
  filtré ou permet de vider explicitement la case. Le corps d'un clip se glisse
  aussi entre pistes et scènes pour le déplacer ; maintenir `Ctrl` (`Cmd` sur macOS)
  en crée une copie complète ;
- mode piste vertical inspiré du lanceur Bitwig : colonnes étroites et stables,
  scènes et clips en haut, mini-chaîne nodale/VST avec bouton `+`, entrées/sorties,
  panoramique, fader, M/S/R et vumètre stéréo temps réel par piste. Une bande Master
  dédiée, toujours placée à droite, affiche la sortie générale, ses niveaux et un
  vrai gain Master sauvegardé. Les faders utilisent une échelle audio graduée,
  restent réglables à la souris et acceptent une saisie numérique directe en dB ;
- streamer double-deck par piste : deux readers et deux buffers de lecture
  anticipée alternent aux frontières sample-accurate ; le callback ne réalise
  plus d'accès fichier lors d'un lancement et attend que le deck soit préchargé ;
- sauvegarde/chargement de projets multipistes `.cydaw` version 35, incluant
  noms et couleurs de pistes, outil d'arrangement et mode de magnétisme, ainsi que
  le time-stretch non destructif, ses marqueurs de warp et ses métadonnées source/timeline,
  le fichier et les fondus propres à chaque région audio, avec migration automatique
  des projets version 31, 26, 21, 20, 19, 18 ou à clip unique,
  les pistes/clips MIDI, les scènes, l'ordre, l'état et les automations de chaque
  instance de plug-in, la disposition du workspace et la migration des anciens projets
  à séquence MIDI unique ;
- autosauvegarde de récupération toutes les 30 secondes lorsqu'un projet est modifié,
  ainsi qu'à la fermeture si nécessaire. Le snapshot conserve des chemins absolus,
  l'état de plug-in déjà mis en cache et toutes les données audio/MIDI/layout sans
  interrompre le transport ; son JSON est encodé sur un worker puis remplace
  atomiquement la génération précédente. Au prochain lancement, une boîte bilingue
  permet de restaurer ou d'ignorer la session ; une sauvegarde manuelle confirmée
  supprime la récupération. CyDAW utilise une instance applicative unique et transmet
  les projets ouverts depuis l'Explorateur à la fenêtre existante ;
- choix des périphériques audio et MIDI, avec WASAPI opérationnel sous Windows ;
- variante ASIO compilable sous Windows et sonde JSON audio/MIDI permettant de
  vérifier l’énumération et l’ouverture temporaire d’un pilote professionnel ;
- synthé polyphonique `Poly Sine` réellement construit comme module composé : son
  sous-graphe éditable contient entrée MIDI publiée, allocateur 16 voix, oscillateur
  sinus/triangle/dent de scie/carré, enveloppe ADSR, filtre par voix et sortie audio
  publiée. Le compilateur aplatit les compositions imbriquées avant le rendu ; chaque
  piste instrument reste modifiable en direct et sauvegardée dans le projet ;
- pistes instrument MIDI indépendantes avec clips multiples, synthé, transport,
  gain, panoramique, mute/solo et armement exclusif du clavier/contrôleur ; le
  piano-roll permet de sélectionner ou créer pistes et clips sans quitter l'éditeur ;
- enregistrement MIDI temps réel depuis le clavier virtuel ou un contrôleur vers
  le clip sélectionné : les événements horodatés sample-accurate passent du callback
  par une file SPSC bornée sans verrou ni allocation. Le piano-roll affiche les notes
  pendant la prise, compte les pertes éventuelles, propose overdub ou remplacement et
  quantifie librement, en 1/4, 1/8, 1/16 ou 1/32. Une prise complète forme un seul pas
  d'historique `Ctrl+Z`/`Ctrl+Y`, comme un glisser de note ; l'enregistrement fonctionne
  aussi dans un projet MIDI vide et respecte le décalage du clip sur la timeline ;
- édition MIDI multipoint inspirée des workflows modernes : `Ctrl`/`Maj`-clic ou
  rectangle dans la grille pour construire la sélection, déplacement chromatique et
  temporel du groupe, redimensionnement par le bord droit et lane de vélocité. Les
  flèches déplacent, `Maj+gauche/droite` change la durée et `Alt+haut/bas` ajuste la
  vélocité ; `Ctrl+A/C/X/V/D`, `Suppr`, le clic droit et la palette `Ctrl+K` partagent
  les mêmes commandes. Chaque geste ou opération de groupe forme un unique pas
  d'historique et les collages reçoivent des identifiants persistants neufs ;
- échange de fichiers MIDI standard : import Type 0/1/2 par bouton, glisser-déposer,
  `Ctrl+Maj+I` ou recherche globale. Le tempo initial est détecté, chaque canal source
  devient une piste instrument nommée et les ticks PPQ ou SMPTE sont convertis en
  positions musicales. Le clip sélectionné s'exporte à 960 PPQ avec tempo, signature,
  nom, hauteurs, durées et vélocités via `Ctrl+Maj+E` ; les fichiers sont validés avant
  toute mutation et des limites explicites protègent la session des imports abusifs ;
- interface sombre et modulaire inspirée des workflows de Bitwig, avec inspecteur,
  arrangement, navigateur et chaîne de périphériques redimensionnables ;
- navigateur de contenu multibibliothèque : plusieurs emplacements persistants et
  amovibles, collection de favoris, catégories et recherche multi-termes commune aux
  noms, chemins et métadonnées. L'indexation asynchrone déduplique les dossiers
  imbriqués, ignore les fichiers invalides et affiche format, durée, fréquence,
  canaux et taille. La préécoute utilise un mixeur d'audition dédié au périphérique
  courant sans créer de piste et sans contaminer l'export master. Le résultat filtré
  se trie par ordre de bibliothèque, nom, date récente, durée ou taille ; ce choix
  persiste entre les lancements sans modifier l'index ni perdre la sélection. Un
  cache JSON versionné affiche immédiatement jusqu'à 50 000 entrées au démarrage,
  puis l'indexeur le vérifie et le remplace sur son thread ; un cache corrompu ou
  associé à d'autres emplacements est ignoré en sécurité. Le sample sélectionné
  dispose d'une prévisualisation de forme d'onde asynchrone avec son nom, sa durée,
  ses canaux et sa fréquence, sans décodage audio dans le thread d'interface. Un
  clic dans la waveform démarre l'audition à cette position et une tête orange
  suit en direct la position réellement lue par le moteur. Un mode `AUTO` lance la
  préécoute au changement de sélection et son gain indépendant (-48 à +6 dB) reste
  réglable sans modifier le Master ; les deux préférences sont persistantes. Le
  clic droit adapte ses actions à l'élément : ouvrir/insérer, écouter, gérer le
  favori, copier/révéler un fichier ou valider un plug-in isolé. Un filtre compact
  limite les samples à tout audio, mono, stéréo, ≤2 s, 2–16 s ou >16 s en utilisant
  seulement les métadonnées de l'index ; sa sélection persiste au redémarrage. Une
  catégorie Graphes expose dix chaînes nodales d'usine complètes, chargeables par
  double-clic ou déposables sur le canevas avec un payload distinct des nœuds. Les
  graphes édités se sauvegardent aussi comme presets utilisateur `.cydawgraph` dans
  `Documents/CyDAW/Graphs` ; ils sont indexés automatiquement dans la même catégorie,
  rechargeables par double-clic ou glisser-déposer, et validés transactionnellement
  avant de remplacer le graphe actif. Une fenêtre de navigateur suffisamment large
  passe automatiquement en vue à trois colonnes : facettes et bibliothèques,
  résultats, puis forme d'onde et contrôles de préécoute ;
- insertion depuis le mode piste : le bouton `+` ouvre le navigateur filtré sur les
  périphériques ; un double-clic sur un effet nodal d'usine l'insère réellement dans
  la chaîne audio avant la sortie et recompile le DAG, tandis que les VST restent
  isolés dans leurs processus supervisés. Un device natif ou un VST du navigateur
  peut aussi être glissé sur la mini-chaîne d'une piste précise, qui s'entoure en
  violet ; les bundles `.vst3` et bibliothèques `.dll`/`.so` peuvent être déposés
  au même endroit depuis l'Explorateur ;
- trois vues cohérentes par piste : la chaîne de périphériques est le mode compact
  par défaut, le Graphe expose la topologie interne des mêmes modules et Modulation
  ouvre un graphe `Value` séparé. LFO, Macro et calcul de contrôle sont des nœuds
  paramétrables et réutilisables reliés à une sortie publiée ; chaque paramètre natif
  exposé peut recevoir une assignation active avec profondeur bipolaire. Graphe,
  valeurs de base et assignations multiples sont indépendants par piste et sauvés
  avec le projet ;
- quatre dispositions d'usine, menu de visibilité des panneaux, layouts utilisateur
  nommés persistants, restauration par projet et historique annuler/rétablir sur
  32 changements ; l'inspecteur, le navigateur, l'éditeur principal, le panneau
  devices/nœuds et le graphe peuvent vivre dans cinq fenêtres indépendantes ; leurs
  positions multi-écrans et dimensions sont sauvées avec chaque layout ;
- palette de commandes globale bilingue (`Ctrl+K`, `Cmd+K` sur macOS) accessible
  aussi par le bouton `SEARCH` : recherche instantanée dans fichiers/projet,
  transport, cinq éditeurs, tous les traitements audio détaillés, devices, graphe
  nodal, fenêtres, layouts, panneaux, réglages audio/MIDI et langues ; les termes anglais et français fonctionnent
  quelle que soit la langue active ;
- règle musicale synchronisée au tempo et clips colorés par piste ;
- chaîne Drive / filtre passe-bas / Gain-Pan reliée au DSP de chaque piste ;
- éditeur nodal avec bibliothèque recherchable, ports audio/MIDI/contrôle,
  connexions validées et modules composites persistants. Le clic droit fonctionne
  aussi au-dessus des contrôles intégrés et sépare `Ouvrir le graphe interne` de
  l’édition de l’interface ; le double-clic entre directement dans la composition ;
- constructeur d’interface pour les nœuds natifs et modules utilisateur : un
  double-clic ouvre une fenêtre séparée avec knobs/faders paramétriques, waveform,
  spectre, vu-mètre et texte ; délai, réverbération, compresseur, EQ, oscillateur,
  bruit, Drive et Cutoff publient désormais leurs paramètres réels. Les contrôles
  restent réassignables par clic droit et déplaçables en mode édition ; disposition,
  bindings et valeurs utilisent un schéma versionné sauvegardé dans le projet ;
- présentation nodale unifiée : le même document d'interface versionné dessine les
  contrôles et visualisations directement dans un nœud, sur un sous-graphe réutilisé,
  dans une fenêtre détachée et dans la chaîne de périphériques. Un double-clic sur
  le canevas ouvre l'éditeur de la façade complète du graphe ; knobs, faders,
  waveform, spectre, enveloppe et vu-mètre suivent les paramètres et la télémétrie
  réels de la piste audio ou du synthé MIDI sélectionné ;
- interface complète `GRAPH — FREE` par graphe : contrôles, ports et visualisations
  sont librement positionnables et redimensionnables. Le graphe accepte zoom à la
  molette, panoramique par glisser-molette et mémorise sa transformation par piste ;
- façade `DEVICE — AUTO` générée uniquement depuis les paramètres marqués
  `Show in Devices`. Le menu `DEVICE PARAMS` contrôle cette exposition et le même
  bouton `UI` que pour un VST ouvre l’interface complète du module interne ;
- entrées/sorties publiées réelles et multiples pour Audio, MIDI, Valeur et
  Transport. Les presets d'I/O créent les nœuds de frontière correspondants, leurs
  connexions internes restent éditables et les ports sont affichés dans l’interface
  complète du module ;
- panneau `MEMBRES` pour éditer le contrat public sans casser ses identifiants :
  nom et description, ordre des ports/paramètres, visibilité Device/VST3 et
  changement strict de type Audio 1–8, MIDI, Valeur ou Transport. Les connexions
  compatibles sont conservées et les autres sont déconnectées explicitement ;
- versions sémantiques indépendantes pour les classes de Graph réutilisables :
  chaque instance compile la version de sa classe et conserve séparément ses
  overrides de paramètres et d'interface ;
- variantes de classes Graph depuis le menu contextuel : l'original reste
  intact, la nouvelle classe garde sa filiation et seule l'instance
  sélectionnée bascule vers la variante modifiable. Le Gestionnaire de Graph
  permet aussi de créer une variante depuis la bibliothèque sans remplacer
  l'instance courante ;
- bibliothèque de trente-deux classes Graph d'usine, toutes inspectables et
  réutilisables : neuf synthés polyphoniques, chaînes de canal et de batterie,
  saturation série/parallèle, espaces, modulation audio, filtre dynamique,
  finition et protection master, outils MIDI, LFO, suiveur audio et suite
  d'analyse. Quatre exemples complets (`Poly Keys`, `Ambient Glass`, `Electric
  Organ` et `Percussive Bell`) regroupent allocation de voix, oscillateur, ADSR,
  traitement stéréo et visualisation dans des sous-graphes directement jouables.
  Le Gestionnaire les range par catégorie, rôle, tags ou auteur, affiche le rôle
  sur chaque ligne et conserve ces métadonnées dans le projet ;
- compilation réelle du DAG audio visuel vers un plan DSP immutable par piste :
  les branches sont mixées, une connexion retirée modifie le son et le plan est
  remplacé hors callback sans allocation ni destruction sur le thread audio ;
- topologie et disposition nodales indépendantes pour chaque piste audio : changer
  de piste rappelle son propre graphe, tandis que les modules composites créés
  avec `Ctrl+G` restent une bibliothèque partagée par le projet ; les projets
  version 15 à graphe global sont migrés vers une copie indépendante par piste ;
- les modules utilisateur ne sont plus remplacés par une approximation DSP : leur
  véritable DAG interne est aplati à la compilation, avec paramètres par nœud et
  connexions conservés. Les modules peuvent contenir d’autres modules ; leurs
  paramètres publiés remontent dans l’interface du parent et le rendu reste un
  plan immutable sans allocation sur le callback audio ;
- les gestes sur les paramètres natifs ne recompilent plus ce plan : si les
  identifiants, types, connexions et sortie restent identiques, le contrôleur met
  à jour directement les atomiques des processeurs actifs. Delay préalloue sa
  mémoire maximale à `prepare`, puis Time/Feedback/Mix, Reverb, Compressor, EQ,
  oscillateur et bruit changent sans allocation ni échange de plan dans le callback ;
- trente-huit graphes d'usine chargeables indépendamment sur chaque piste. Neuf
  sont des instruments polyphoniques (lead, nappe, basse, pincé, clavier, orgue,
  cloches, atmosphère, pincé arpégé) et le reste couvre les classiques d'un DAW :
  canaux clean/warm/studio, vocal strip, delays slapback et dub, réverbérations
  plaque et pièce, chorus et filtre automatique modulés, colle de bus batterie,
  dé-esseur, téléphone, punch gaté et chaîne de mastering. Chaque graphe est un
  vrai DAG natif éditable, sauvegardé avec la piste et recompilé hors du callback
  audio. Deux primitives couvrent ce qu'une palette soustractive ne sait pas
  feindre : `core.wavetable` (huit trames d'un cycle construites par synthèse
  additive, morphing sans aliasing, trois voix désaccordées) et `core.string`
  (guide d'ondes — ligne à retard d'une période, filtre de boucle, gain de retour
  déduit du temps de déclin). Les deux sont
  jouables au clavier ; `include/cydaw/graph/SoundSources.h` est la seule
  description de ce qui fait une source sonore, lue par les deux compilateurs
  d'instrument et les deux inférences de rôle : la voix polyphonique connaît deux modes de plus en
  regard de ses quatre formes d'onde. Le catalogue compte 50 graphes, dont sept devices bâtis sur la bande
  d'égalisation paramétrique `core.peaking-eq` et la sortie de réduction de gain
  du compresseur : EQ de précision, compresseur, limiteur, saturateur multibande,
  écho à bande, réverbération de chambre, plus deux instruments.
  Charger un preset cale la vue sur la chaîne entière. Le rôle d'un graphe
  (instrument, effet, générique) est déduit d'une seule description de ses
  frontières, lue par l'inférence, la bibliothèque et le gestionnaire de modules.
  `CyDAWGraphEditor --language=fr` ouvre l'éditeur dans une autre langue.
  La bibliothèque les liste tous — les instruments et générateurs sous
  INS, les traitements sous FX, l'ensemble sous GRF. `CyDAWGraphEditor --preset=N` en charge un (par index de catalogue)
en ligne de commande, et `CyDAWGraphEditor --verify-presets` les charge tous et vérifie qu'ils
  compilent — et, pour les instruments, qu'ils produisent bien un plan de voix ;
- presets de graphes utilisateur portables `.cydawgraph` : le document versionné
  contient le DAG, les modules composites, les paramètres et l'interface publiée.
  L'import refuse les fichiers surdimensionnés, les types inconnus, les identifiants
  dupliqués et les connexions invalides, puis restaure l'ancien graphe si la mutation
  ne peut pas être appliquée intégralement ;
- processeurs nodaux natifs opérationnels et paramétrables par nœud : saturation, passe-bas, oscillateur,
  bruit, délai, réverbération, compresseur, égalisation tonale, rack de réverbération
  et vocal strip ; les valeurs sont sérialisées avec chaque graphe de piste ;
- télémétrie audio post-DSP/post-plugin et post-gain publiée par piste dans une
  file SPSC bornée : le graphe affiche en direct une waveform, un spectre
  logarithmique, les niveaux peak/RMS stéréo du Master et l'activité des câbles,
  tandis que la transformée spectrale reste entièrement hors du callback audio ;
- interface traduisible, anglais et français inclus ;
- tests du cœur sans dépendance externe.

## Construction

Prérequis : CMake 3.24+, compilateur C++20 et Ninja ou un IDE natif.

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Pour la coque desktop, fournir un checkout JUCE 8.0.15 :

```text
cmake -S . -B build/desktop -DCYDAW_BUILD_DESKTOP=ON \
  -DCYDAW_JUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build/desktop --config RelWithDebInfo
```

`CYDAW_FETCH_JUCE=ON` peut aussi récupérer automatiquement la version épinglée.

Pendant le développement, préférer le preset `desktop-fast`, identique au preset
`desktop` mais sans optimisation à l'édition de liens :

```text
cmake --preset desktop-fast
cmake --build --preset desktop-fast
```

Le LTO domine entièrement la boucle édition-compilation : sur ce dépôt, modifier
un fichier de 88 lignes coûtait 33,7 s, dont environ 33 s d'édition de liens et
une fraction de seconde de compilation. Le désactiver ramène cette boucle à
1,6 s, et la modification du plus gros fichier de l'interface de 40,6 s à 10,8 s.
Les deux réglages sont pilotés par `CYDAW_ENABLE_LTO` (défaut `ON`, à laisser
activé pour tout binaire distribué) et `CYDAW_ENABLE_PCH` (défaut `ON`, qui
précompile la surface JUCE partagée par les sources de `src/app`).

Chaque test de la suite tourne dans son propre bac à sable : profil utilisateur,
dossier temporaire et délai maximal lui sont propres
(`cydaw_configure_test` dans `CMakeLists.txt`). Aucun test ne lit donc ce qu'un
autre — ou l'application en cours d'exécution — a laissé derrière lui, et le
journal d'un self-test qui échoue n'est plus écrasé par les tests suivants :
il reste dans `<build>/test-sandbox/<test>/temp/`.

Un banc de performance permanent mesure les surfaces interactives :

```text
CyDAW.exe --perf-self-test
```

Il construit une session réaliste (huit pistes audio, 1 920 points d'automation,
400 notes), mesure la disposition, le coût de peinture de chaque surface et celui
d'un tick de transport, puis écrit son rapport dans `cydaw-perf.txt` et dans le
journal des self-tests. Il rapporte le **passage le plus rapide**, jamais une
moyenne : mesuré coup sur coup, un même binaire annonçait entre 5 et 26 ms selon
ce que faisait la machine, et seul le passage sans interférence est comparable
d'une build à l'autre.

## Vue mixer

Le workspace compte une neuvième vue, `MIXER` : une tranche par canal — les
pistes audio puis les pistes MIDI, dans l'ordre du launcher — et une tranche
master épinglée. Chaque tranche montre le nom et la couleur de la piste, le
résumé de sa chaîne de périphériques (double-clic pour ouvrir son graphe), le
panoramique, un fader avec vu-mètre stéréo et lecture en dB, puis mute / solo /
arm, avec une ligne I/O (entrée et sortie ; un clic sur l'entrée d'une piste
audio choisit son bus d'entrée virtuel). Le fader se déplace relativement au
point de saisie (Maj affine, double-clic remet à 0 dB, la molette ajuste par
0,5 dB, un clic sur la valeur la saisit au clavier). Glisser l'en-tête d'une
tranche la réordonne, le clic droit ouvre le menu de la piste.
`Ctrl/Cmd+9` ouvre le mixer : les neuf vues du workspace répondent à
`Ctrl/Cmd+1…9`, dans l'ordre du sélecteur de vue. Le mixer et le launcher passent par les mêmes gestionnaires du
workspace (`applyChannelMix`, `applyChannelMute`, …), et leurs faders sont
dessinés par le même en-tête, `src/app/MixerStripDrawing.h`.

Une capture de l'interface se prend sans intervention :

```text
CyDAW.exe --ui-snapshot=capture.png --view=9 projet.cydaw
```

`--view=N` choisit la vue capturée (1 arrangement, 2 clip MIDI, 3 automation,
4 launcher, 5 fichier audio, 6 clip audio, 7 graphe, 8 bus, 9 mixer) ; sans lui,
la capture montre la vue par défaut. `--browser-category=N` sélectionne d'abord
un filtre de la bibliothèque (1 tout, 2 samples, 3 instruments, 4 effets,
5 plug-ins, 6 devices, 7 graphes). `--window-size=LxH` et `--language=xx`
capturent une largeur et une langue précises — la barre d'outils décide ce
qu'elle affiche en mesurant ses libellés, donc les deux comptent.

## Chrome

`CyDawLookAndFeel` porte la palette partagée (`cydaw::app::theme`) : le
turquoise signale « actif », l'ambre « sélectionné », le rouge n'est que
l'enregistrement. Trois traitements distinguent les contrôles — une entrée de
menu est plate, un onglet ne montre en relief que celui qui est actif
(`CyDawLookAndFeel::setFlat`), une action garde son cadre. La barre d'outils
groupe ses commandes derrière des filets, pose le transport et l'horloge dans
des puits encastrés, et choisit ce qu'elle affiche en mesurant les libellés :
la navigation passe avant les raccourcis de fichier, qui sont tous dans le menu
Fichier et sur `Ctrl+O/S/L/E`.

La version Windows construite localement se trouve dans
`build/desktop/CyDAW_artefacts/Release/CyDAW.exe`. Elle permet actuellement de
choisir le périphérique audio/MIDI, ajouter ou glisser plusieurs fichiers audio,
lire la session et se déplacer dans les formes d'onde, régler gain/pan/mute/solo
par piste, boucler, enregistrer une entrée en WAV et sauver/charger un projet
`.cydaw`.

L'interface utilise des clés de traduction. L'anglais et le français sont
embarqués et également copiés dans le dossier `i18n` à côté de l'exécutable ; ces
JSON externes peuvent être traduits ou corrigés sans modifier le code.

ASIO est volontairement désactivé par défaut : activez
`CYDAW_ENABLE_ASIO=ON` uniquement après avoir choisi une licence compatible
(GPLv3 pour un produit GPL, ou accord propriétaire pour un produit fermé).

VST2 est lui aussi désactivé par défaut et aucun SDK historique n'est inclus ou
téléchargé. Une personne disposant déjà d'un SDK VST2 obtenu légalement peut
compiler le support optionnel avec `CYDAW_ENABLE_VST2=ON` et
`CYDAW_VST2_SDK_PATH=...`. La procédure et les limites sont décrites dans
[docs/VST2_BUILD.md](docs/VST2_BUILD.md).

## Direction technique

La spécification se trouve dans [docs/PRODUCT_SPEC.md](docs/PRODUCT_SPEC.md),
l'architecture dans [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), et le plan de
la distribution dédiée dans [os/README.md](os/README.md). Les presets de build
par plateforme sont détaillés dans [docs/BUILD_MATRIX.md](docs/BUILD_MATRIX.md),
et le pont VST Windows/Linux dans
[docs/LINUX_WINDOWS_VST.md](docs/LINUX_WINDOWS_VST.md).

Les priorités immédiates sont : exécuter le paquet Linux enrichi sur une machine
native avec JACK, Wine/yabridge et UMU, valider macOS/CoreAudio, puis étendre la
compatibilité de formats et la bibliothèque native.

## Licence

CyDAW est distribué sous la licence publique générale GNU Affero, version 3
(AGPL-3.0) : voir [LICENSE](LICENSE). Maintenance : Cyberalien.

Il s'appuie sur JUCE 8 (AGPLv3, récupéré à la compilation) et Dear ImGui (MIT,
[third_party/imgui/LICENSE.txt](third_party/imgui/LICENSE.txt)). Les banques de
sons que le logiciel télécharge gardent leurs propres licences (CC0, Unlicense),
créditées dans [content/factory/LICENCE.txt](content/factory/LICENCE.txt) et
[docs/SOUND_SOURCES.md](docs/SOUND_SOURCES.md).
