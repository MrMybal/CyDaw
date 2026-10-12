# CyDAW — build de test Windows

Lancez `CyDAW.exe`. Le dossier doit conserver `CyDAWPluginWorker.exe` et le
sous-dossier `i18n` à côté de l'application. `CyDAWAudioProbe.exe` génère un
diagnostic JSON des backends audio et des ports MIDI sans lancer le DAW.

Fonctions testables dans ce jalon :

- ouvrez `BUS` dans les vues principales. Créez un bus d'entrée, nommez-le
  `Voix`, choisissez Mono ou Stéréo puis assignez ses canaux A/B aux entrées
  physiques de la carte son. Activez `MONITORING` : son vu-mètre et le Master
  doivent suivre l'entrée sans armer ni lire un clip. Cliquez sur `ÉDITER LE
  GRAPHE` : le bus doit présenter uniquement `ENTRÉE AUDIO → SORTIE AUDIO`.
  Ajoutez ensuite explicitement un compresseur au graphe et comparez le niveau
  en direct ; le traitement doit être audible sans redémarrage audio ;
- dans `AFFECTATION DES PISTES`, choisissez une piste audio puis `Voix`. Armez-la
  et enregistrez : le fichier créé doit contenir uniquement le ou les canaux du
  bus, avec son gain et le graphe que vous avez construit. Créez aussi un bus de sortie, changez ses
  destinations physiques A/B et vérifiez que le Master sort sur ces canaux.
  Sauvegardez/rechargez le projet : noms, directions, canaux, DSP et affectations
  doivent revenir dans le format `.cydaw` version 36. `PORT SYSTÈME` est pour ce
  jalon une demande persistée ; il ne crée pas encore un pilote audio Windows ou
  une extension CoreAudio. La publication JACK/PipeWire reste le prochain raccord
  natif Linux ;

- `LANCEUR` et `ARRANGEMENT` sont maintenant deux modes de lecture exclusifs :
  entrer dans le Lanceur désactive l'Arrangement, et revenir dans l'Arrangement
  arrête les clips du Lanceur. Dans une case vide du Lanceur, faites un double-clic
  pour créer un clip vide de quatre mesures et ouvrir `CLIP AUDIO`. Ce clip est une
  timeline locale à une piste : déposez-y plusieurs samples, déplacez-les, coupez-les,
  étirez-les et ajoutez des ancres de warp. Les commandes locales Lecture, Stop et
  Retour utilisent une tête de lecture propre au clip. Vérifiez aussi Durée, Début,
  Fin, Début de boucle, Fin de boucle et l'activation de la boucle, puis sauvegardez
  et rechargez le projet ; le conteneur doit rester présent même lorsqu'il ne contient
  plus aucun événement. Le bouton `+ PISTE` propose désormais une piste Audio ou une
  piste Instrument/MIDI ;

- dans l'arrangement, les trois menus visibles `1/16`, `GRILLE` et `SÉLECTION`
  règlent respectivement la résolution, le comportement du magnétisme et l'outil.
  Vérifiez les modes grille absolue, relatif et événements, puis les outils
  sélection, coupe, étirement, loupe et main. Un clic avec la loupe zoome x2 ;
  son glissement vertical zoome continûment et son glissement horizontal déplace
  la vue. La main et le clic-molette doivent déplacer
  la timeline horizontalement et faire défiler les pistes verticalement. Les valeurs
  de début et de durée saisies dans l'inspecteur suivent la grille, tout comme
  l'étirement temporel. L'arrangement conserve une zone libre d'au moins deux
  mesures à droite du dernier clip. Un double-clic sur un clip audio de
  l'arrangement ou du lanceur doit ouvrir `CLIP AUDIO`, jamais `FICHIER AUDIO`.
  Cette timeline locale non destructive accepte plusieurs événements sur une seule
  piste : la bande supérieure déplace ou découpe au double-clic,
  les bords centraux retaillent, le corps ajoute une ancre de warp au double-clic
  et toute la bande inférieure `STRETCH` étire. Utilisez ses menus de snap visibles
  pour tester Libre, 1/4, 1/8, 1/16 et 1/32 avec Grille, Relatif et Événements.
  Glissez horizontalement la règle supérieure pour déplacer la vue et verticalement
  pour zoomer ; la molette zoome également et Maj+molette déplace. Utilisez
  `SOURCE` pour ouvrir séparément `FICHIER AUDIO`. `CLIP MIDI` garde son propre
  piano-roll, son snap local et les mêmes gestes de navigation sur sa règle ;
- dans l'inspecteur du clip, utilisez `AUTO` pour détecter le BPM source, corrigez
  la valeur manuellement si nécessaire, choisissez Repitch, Tonal, Rythme ou Texture,
  puis `SYNCHRO` pour ajuster la durée au BPM du projet. Repitch doit changer la
  hauteur ; les trois modes granulaires doivent conserver la hauteur avec des
  traitements adaptés au type de source. Ces réglages doivent revenir après
  sauvegarde/rechargement ;
- faites un clic droit sur l'en-tête d'une piste : renommez-la, changez sa couleur,
  puis commutez M/S/R. Nom et couleur doivent être identiques dans le lanceur et
  revenir après sauvegarde/rechargement du projet version 36 ;
- ouvrez `FEN`, détachez le navigateur sur un second écran et agrandissez-le : il
  doit passer automatiquement en trois colonnes (filtres, résultats, aperçu audio).
  Détachez aussi l'éditeur principal et le panneau de périphériques, sauvegardez
  une disposition nommée, modifiez les fenêtres, puis réappliquez-la ;
- une piste audio neuve doit contenir uniquement `Entrée audio → Sortie de piste`.
  Une piste MIDI neuve doit afficher `Entrée MIDI` et `Sortie Master`, sans synthé
  implicite et sans produire de son. Ajoutez explicitement `Poly Sine` depuis le
  navigateur : la chaîne polyphonique devient alors audible. Faites un clic droit
  dans le vide du graphe pour ouvrir directement le navigateur contextuel filtrable
  (`Maj+clic droit` conserve les actions générales du canevas) ; validez au clavier
  ou double-cliquez pour insérer au point visé. Vérifiez aussi que l'on peut glisser
  un nœud depuis le navigateur latéral
  jusqu'au canevas, avec un aperçu de sa position. Passez ensuite en mode `LANCEUR`
  et glissez un device natif sur la mini-chaîne d'une autre piste : sa zone doit
  s'entourer en violet et l'effet doit être ajouté à cette piste, pas à l'ancienne
  sélection. Faites le même test avec un VST du navigateur, puis avec un `.vst3`
  ou une `.dll` depuis l'Explorateur. Un clic droit sur `Poly Sine`
  permet toujours d'ouvrir son sous-graphe et son interface. Un ancien projet dont
  les pistes audio avaient encore exactement le graphe d'usine mixte doit être
  migré automatiquement, sans modifier les graphes personnalisés ;
- dans le navigateur, combinez une recherche multi-termes, un emplacement et les
  favoris, puis changez le menu de tri entre ordre de bibliothèque, nom, récent,
  durée et taille. Seuls les résultats visibles doivent se réordonner, le sample
  sélectionné doit le rester et le choix de tri doit revenir au prochain lancement.
  Fermez puis relancez avec une grosse bibliothèque : les entrées du cache doivent
  apparaître immédiatement pendant que le statut annonce l'actualisation de l'index ;
  sélectionnez plusieurs samples après chaque tri : le panneau de prévisualisation
  doit afficher le bon nom, les métadonnées et la forme d'onde sans bloquer la liste.
  Cliquez au quart puis aux trois quarts de cette waveform : l'audition doit repartir
  de la position choisie et la tête orange doit progresser jusqu'à la fin. Une
  recherche ne donnant aucun résultat doit vider proprement cette waveform. Activez
  `AUTO`, parcourez plusieurs samples puis réglez le niveau de -48 à +6 dB : chaque
  nouvelle sélection doit démarrer une seule fois et le Master ne doit pas changer.
  Les deux réglages doivent revenir après redémarrage. Faites un clic droit sur un
  sample : ouvrir/insérer, écouter, favori, copier le chemin et afficher dans
  l'Explorateur doivent être proposés. Le clic droit sur un device doit proposer
  son insertion, et celui d'un VST doit utiliser la validation isolée. Essayez les
  facettes Tous audio, Mono, Stéréo, ≤2 s, 2–16 s et >16 s : seuls les samples
  correspondants doivent rester, sans nouvelle indexation, et le filtre doit être
  restauré au prochain lancement. Choisissez ensuite la catégorie `Graphes` : dix
  chaînes d'usine doivent apparaître. Le double-clic charge la chaîne sur la piste
  sélectionnée ; en la glissant sur le canevas, toute la zone doit s'encadrer en
  orange et le preset doit remplacer le graphe après le dépôt. La mini-chaîne du
  lanceur ne doit pas accepter ce payload. Modifiez ensuite le graphe et utilisez
  `SAUVER PRESET/SAVE PRESET` : un fichier `.cydawgraph` doit être créé dans
  `Documents/CyDAW/Graphs`, apparaître immédiatement dans la catégorie Graphes et
  pouvoir être rechargé par double-clic ou glisser-déposer. Un fichier JSON altéré
  ou surdimensionné doit être refusé sans modifier le graphe déjà ouvert ;
- session audio multipiste avec formes d'onde ;
- navigation d'arrangement : placez le pointeur sur la règle ou une forme d'onde et
  utilisez la molette pour zoomer autour de ce point. Utilisez `Maj+molette` ou la
  barre horizontale inférieure pour défiler, `-` / `+` pour changer le zoom et le
  bouton central (facteur affiché) ou `Ctrl+0` pour revenir au projet entier. Les
  en-têtes de pistes doivent rester fixes et les régions alignées avec la règle ;
- lecture, seek, boucle, gain, panoramique, mute et solo ;
- position, offset source et durée de clip non destructifs ;
- arrangement multiclips : cliquez une région audio pour la sélectionner, glissez-la
  horizontalement, puis utilisez son clic droit pour la dupliquer, la découper au
  curseur ou la supprimer. `Ctrl+D`, `Ctrl+Shift+X` et `Suppr` donnent accès aux
  mêmes commandes, également recherchables avec `Ctrl+K`. Depuis l'Explorateur,
  déposez ensuite un second WAV/AIFF/FLAC/OGG/MP3 directement dans la forme d'onde
  d'une piste existante : une nouvelle région doit apparaître à la position visée,
  même si sa fréquence d'échantillonnage diffère de celle du premier fichier.
  Glissez ensuite les marqueurs verticaux gauche/droite pour retailler la région
  sans toucher au WAV et ses deux poignées rondes supérieures pour régler les
  fondus. Superposez deux régions puis choisissez `Fondu enchaîné` dans le clic
  droit : une zone `FONDU X` avec deux diagonales doit matérialiser le crossfade.
  Après chacun de ces gestes, `Ctrl+Z` doit restaurer toute l'opération en une
  fois ; `Ctrl+Shift+Z` ou `Ctrl+Y` doit la rétablir. Vérifiez aussi duplication,
  découpe et suppression : chacune doit consommer exactement un pas d'historique.
  Le menu `1/4`, `1/8`, `1/16`, `1/32` de la barre supérieure règle maintenant
  le magnétisme musical (`Off` le désactive) : déplacement, bords, fondus, découpe,
  dépôt et curseur doivent suivre les lignes correspondantes au tempo ; gardez
  `Alt` pendant un glisser pour contourner la grille. Faites `Ctrl-clic` sur
  plusieurs régions, ou `Ctrl+A` sur la piste courante : les contours doivent
  rester sélectionnés et déplacement, `Ctrl+D` puis `Suppr` doivent traiter tout
  le groupe en conservant ses écarts. Chaque opération groupée s'annule en un
  seul `Ctrl+Z`, et le choix de grille doit revenir après sauvegarde/rechargement.
  Testez ensuite `Ctrl+C`, placez la tête de lecture, puis `Ctrl+V` : toutes les
  régions sélectionnées doivent être collées sur la piste cible avec leurs écarts,
  sources et fondus. `Ctrl+X` les coupe après copie ; coupe et collage de groupe
  consomment chacun un seul pas d'historique. Le clic droit expose aussi
  Copier/Couper/Coller ici. Enfin, partez d'un espace vide et glissez horizontalement
  puis verticalement à travers plusieurs pistes : une plage turquoise doit suivre
  le geste dans les pistes et la règle, et toutes les régions croisées doivent être
  entourées. `Suppr` les enlève toutes avec un seul `Ctrl+Z`. Répétez avec
  le bouton `PLAGE/RANGE` dans l'en-tête : `Découper aux bornes` doit créer les
  fragments intérieur/extérieur, `Rogner` ne doit conserver que l'intersection,
  et `Rendre silencieuse` doit créer un trou sans modifier les WAV. Les raccourcis
  correspondants sont `Ctrl+Shift+X`, `Ctrl+Shift+T` et `Shift+Suppr`; chaque
  action multipiste doit s'annuler et se rétablir en une seule étape. Glissez enfin
  une région ou le bloc sélectionné verticalement sur une autre piste : fichiers,
  offsets et espacements doivent rester identiques, avec un seul `Ctrl+Z` pour
  restaurer toutes les pistes. Testez enfin `Ctrl+C`, choisissez la première
  piste cible et faites `Ctrl+V` : le bloc doit
  retrouver ses espacements temporels et verticaux, toujours avec un undo unique ;
- dans la règle supérieure, faites `Maj-glisser` pour créer une plage orange
  magnétique. La boucle doit s'activer automatiquement ; glissez ensuite ses deux
  poignées, puis lancez une session contenant des pistes de longueurs différentes :
  elles doivent toutes revenir ensemble à la borne gauche. Le clic droit d'une
  région propose de boucler toute la sélection, et `Ctrl+K` permet aussi de
  réinitialiser la plage sur le projet complet. Sauvegardez/rechargez pour vérifier
  les bornes et l'état d'activation ;
- mode `ÉDITEUR AUDIO` : glissez dans la forme d'onde pour sélectionner une
  plage, zoomez avec la molette ou les boutons, gardez la sélection comme clip,
  réinitialisez le clip ou exportez la plage en WAV 24 bits ;
- appliquez ensuite un fondu, un silence, une inversion temporelle, un gain de
  -24 à +24 dB, la normalisation réelle à -1 dB, la suppression d'offset DC,
  l'inversion de polarité ou l'échange stéréo. Le calcul reste en arrière-plan,
  conserve la source originale et peut être annulé/rétabli avec les boutons ou
  `Ctrl+Z`/`Ctrl+Y`. Vérifiez qu'une zone extérieure à la sélection reste
  inchangée. `Suppr` rend la sélection silencieuse ; `Ctrl+Shift+R/N/G/D`
  déclenche respectivement inversion, normalisation, gain et suppression DC.
  Toutes les opérations sont aussi accessibles par recherche avec `Ctrl+K` ;
- mode `LANCEUR` à huit scènes : une case vide ordinaire ne déclenche rien. Armez
  une piste avec son bouton rond rouge, puis cliquez le bouton rond apparu dans
  une case vide : son contour orange attend la prochaine frontière du BPM, puis
  l'entrée de la carte son est réellement enregistrée. Cliquez une seconde fois
  pour programmer l'arrêt sur la grille ; la prise devient ensuite un clip autonome
  dans cette case et reste disponible après sauvegarde/rechargement du projet.
  Sur un clip existant, seul le petit triangle lance la lecture ; le corps du clip
  sélectionne simplement la piste. Glissez un sample du navigateur latéral ou de
  l'Explorateur sur n'importe quelle case : une bordure turquoise doit suivre la
  destination et le sample doit devenir la source autonome de cette case. Un clic
  droit ouvre maintenant un menu : `Choisir un sample` filtre le navigateur et un
  double-clic insère dans la case ciblée, tandis que `Vider la case` ne s'affiche
  que si elle contient un clip. Glissez ensuite le corps d'un clip vers une autre
  piste ou scène : la destination doit s'entourer en orange et le clip doit être
  déplacé avec sa source et sa couleur. Recommencez en maintenant `Ctrl` : les deux
  cases doivent cette fois contenir le clip. Cliquez le numéro de scène pour
  lancer toute la ligne ; le
  menu en haut à gauche choisit une quantification de libre à une mesure et une
  bordure orange identifie les clips en attente de la prochaine frontière ;
  chaque case possède une source préchargée sur le thread disque, de sorte que
  les bascules quantifiées n'effectuent aucun accès fichier dans le callback ;
- enregistrement audio intégré : activez une entrée dans `AUDIO`, armez une piste
  avec son bouton `R`, placez la tête de lecture puis appuyez sur le bouton rouge
  ou sur `R`. La boucle doit être suspendue pendant la prise ; `STOP` ou un second
  appui doit insérer le WAV comme nouvelle région exactement à la position de
  départ sur la piste armée. `Ctrl+Z` retire cette région et `Ctrl+Y` la restaure.
  Désarmez toutes les pistes et recommencez dans un projet vide : une nouvelle
  piste doit être créée et armée automatiquement. Appuyez sur `Échap` pendant une
  autre prise pour l'annuler sans conserver de région ni de WAV partiel. Fermez et
  relancez CyDAW pour vérifier que le pilote et les canaux choisis sont restaurés ;
- prises alternatives : sélectionnez une région audio et utilisez la section
  `PRISES ALTERNATIVES/ALTERNATE TAKES` de l’inspecteur. `+ FICHIER` doit ajouter
  une autre source et afficher `T2/2` sur la région ; la liste doit changer
  immédiatement la waveform et le son rendu. `● PRISE` arme la piste, replace la
  tête au début exact de la région et enregistre directement une nouvelle
  alternative. `SUPPR.` retire la prise active mais refuse de supprimer la
  dernière. Vérifiez `Ctrl+Z`/`Ctrl+Y` après ajout, changement et suppression,
  puis sauvegardez/rechargez : ordre, prise active et chemins doivent être conservés ;
- bouton `EXPORTER/EXPORT MIX` ou `Ctrl+E` : choisissez un WAV, puis laissez la
  session se lire depuis zéro. Le bouton affiche la progression et le fichier
  stéréo 24 bits contient le master réellement entendu, y compris MIDI, DSP,
  automation, plug-ins et compensation de latence, plus deux secondes de queue
  d'effets. Cliquez `ANNULER/CANCEL` pour vérifier que le WAV partiel est supprimé ;
- mode `MIDI` multipiste : utilisez `+ PISTE` et `+ CLIP`, choisissez le contexte
  dans les deux listes du piano-roll, éditez les notes puis réglez gain,
  panoramique, mute et solo dans le même bandeau. Activez votre contrôleur dans
  `AUDIO`, choisissez `Libre`, `1/4`, `1/8`, `1/16` ou `1/32`, puis appuyez sur
  `R` ou sur le bouton d'enregistrement du transport : les notes doivent apparaître
  pendant la prise avec leur vélocité et le compteur doit progresser. `OVERDUB`
  conserve les notes existantes ; désactivé, il remplace le contenu du clip.
  Arrêtez, puis vérifiez que `Ctrl+Z` annule toute la prise en une fois et que
  `Ctrl+Y` la restaure. Répétez dans un projet sans audio ni notes, puis sur un clip
  décalé dans la timeline : le transport doit démarrer et les notes rester relatives
  au début de ce clip ;
- toujours en mode `MIDI`, faites `Ctrl`/`Maj`-clic sur plusieurs notes ou tracez un
  rectangle depuis une zone vide. Glissez le groupe, tirez le bord droit d'une note
  sélectionnée, puis glissez ses barres dans la lane `VÉLOCITÉ`. Vérifiez aussi les
  flèches, `Maj+gauche/droite`, `Alt+haut/bas`, `Ctrl+A/C/X/V/D` et `Suppr` ; chaque
  geste complet doit s'annuler en une seule fois. Les mêmes commandes doivent être
  trouvables en anglais ou en français avec `Ctrl+K` ;
- cliquez `IMPORTER/IMPORT` ou faites `Ctrl+Maj+I` pour charger un `.mid` multicanal :
  CyDAW doit créer une piste instrument par canal, reprendre le tempo initial et
  sélectionner la première piste importée. Le même fichier doit pouvoir être déposé
  sur la fenêtre. Cliquez ensuite `EXPORTER/EXPORT` ou faites `Ctrl+Maj+E`, ouvrez le
  fichier obtenu dans un autre séquenceur et vérifiez notes, vélocités et durées ; les
  deux actions doivent aussi apparaître dans la recherche `Ctrl+K` ;
- clavier virtuel et contrôleurs MIDI routés uniquement vers la piste instrument
  sélectionnée/armée, chaque piste conservant son propre synthé et ses clips ;
- sauvegarde/chargement des projets multipistes `.cydaw`, y compris toutes les
  pistes instrument, clips MIDI et réglages de mixage ;
- modifiez un projet puis attendez environ 30 secondes : la barre d'état doit confirmer
  l'autosauvegarde sans interrompre la lecture. Pour simuler un incident, fermez ensuite
  le processus depuis le gestionnaire des tâches avant une sauvegarde manuelle, puis
  relancez CyDAW. La boîte de récupération doit indiquer le projet et l'heure ;
  `RESTAURER/RESTORE` recharge audio, MIDI et layout avec un `*` jusqu'au prochain
  `SAUVER/SAVE`, qui supprime le snapshot. Recommencez avec `IGNORER/DISCARD` pour
  vérifier que l'ancienne session n'est plus proposée. Ouvrir un `.cydaw` depuis
  l'Explorateur pendant que CyDAW tourne doit le transmettre à la fenêtre existante ;
- interface d'arrangement inspirée des DAW modernes : inspecteur à gauche,
  timeline et règle musicale au centre, navigateur à droite ;
- bouton `SEARCH` et raccourci `Ctrl+K` (`Cmd+K` sur macOS) : saisissez un terme
  anglais ou français pour retrouver et exécuter les commandes de fichier,
  transport, éditeurs, devices/graphe, fenêtres, layouts, panneaux, réglages ou
  langue ; `↑`/`↓` sélectionnent, `Entrée` exécute et `Échap` ferme la palette ;
- menu `DISPOSITION/LAYOUT` avec quatre espaces d'usine (production, édition
  audio, conception modulaire et concentration), sauvegarde de layouts nommés et
  restauration avec le projet ; le bouton `▦` affiche/masque indépendamment
  l'inspecteur, le navigateur et le panneau inférieur ; sa section fenêtres permet
  aussi de détacher/ré-ancrer l'inspecteur, le navigateur, l'éditeur principal, le
  panneau devices/nœuds et le graphe. Déplacez et redimensionnez ces fenêtres, sauvez
  un layout puis rechargez-le pour vérifier la restauration multi-écrans ;
  les boutons `↶`/`↷` et `Ctrl+Alt+Z/Y` parcourent les 32 derniers changements
  de disposition sans interférer avec l'undo/redo de l'éditeur audio ;
- panneau inférieur commutable entre chaîne de périphériques et graphe nodal ;
- contrôles Drive, filtre passe-bas, Gain et Pan reliés au traitement audio réel ;
- navigateur multibibliothèque : ajoutez deux dossiers avec `+ DOSSIER/+ FOLDER`,
  vérifiez leur présence dans le filtre d'emplacement puis sélectionnez un sample.
  Sa ligne doit afficher format, durée, fréquence, canaux et taille ; `FAVORI/FAVORITE`
  le rend disponible dans la collection Favoris après redémarrage. Une recherche à
  plusieurs mots doit exiger tous les termes. `ÉCOUTER/PREVIEW` joue le fichier sur
  la sortie courante sans ajouter de piste et `STOP` l'arrête ; cette audition ne doit
  pas apparaître dans un export master. `RESCAN` actualise toutes les bibliothèques et
  le bouton `−` retire seulement l'emplacement sélectionné ;
- sonde et validation de plug-in lancées dans `CyDAWPluginWorker.exe`, hors du processus principal ;
- chaîne ordonnée de huit plug-ins maximum par piste, chaque instance utilisant son
  propre worker et un transport audio par mémoire partagée préallouée ;
- paramètres de plug-in énumérés et modifiables par le canal de contrôle isolé ;
- automation du gain de piste et de chaque paramètre automatisable, avec
  sélecteur de cible, courbes modifiables et publication temps réel non bloquante ;
- sauvegarde et restauration de l'état binaire du plug-in dans le projet `.cydaw` ;
- éditeur graphique du plug-in ouvert dans une fenêtre appartenant au worker, de sorte
  qu'un crash de son interface ne puisse pas arrêter le processus principal ;
- compensation automatique de la latence entre pistes et synthé interne, avec la
  valeur PDC courante affichée dans la barre d'état ;
- détection de crash du worker sans arrêt de CyDAW ;
- graphe nodal déplaçable, détachable et sauvegardé ;
- bibliothèque de 21 types de nœuds avec recherche intégrée ;
- sélecteur de dix presets factory sous la bibliothèque : chargez successivement
  `Canal propre`, `Ambiance parallèle` et `Atmosphère bruitée`. Le graphe entier
  doit être remplacé, le bandeau doit confirmer sa compilation et chaque piste
  doit conserver son propre preset après changement de sélection et rechargement
  du projet ;
- ports typés audio, MIDI et contrôle, connexions par glisser-déposer et refus
  des cycles implicites ;
- le routage audio du graphe est actif : déconnectez un nœud avec son menu puis
  reconstruisez la chaîne en glissant les ports. Ajoutez notamment un délai, une
  réverbération, un compresseur, un EQ ou un vocal strip entre `CLIPS AUDIO` et
  `SORTIE MASTER` ; déconnecter toute la route produit du silence. Un oscillateur
  ou un bruit relié au Master devient une source audio réelle ; le bandeau d'état
  du graphe confirme le nombre de nœuds DSP compilés ;
- chaque piste audio possède maintenant sa propre topologie et sa propre disposition
  nodale. Chargez deux samples, retirez une connexion sur la première piste puis
  sélectionnez la seconde : son graphe reste intact et le bandeau orange indique
  toujours la piste actuellement éditée. Les modules créés avec `Ctrl+G` restent
  disponibles dans la bibliothèque des deux pistes ; sauvegardez/rechargez le
  projet pour vérifier le format `.cydaw` 24, les sources et les fondus propres
  aux régions ;
- le graphe d'usine place maintenant `SPECTRE` sur la route audio : pendant la
  lecture, il superpose waveform et énergie fréquentielle en direct ; la sortie
  `MASTER` affiche peak/RMS pour les canaux L/R et les câbles audio réagissent au
  niveau de la piste sélectionnée. La DFT est calculée par l'interface, jamais
  dans le callback temps réel ;
- les contrôles et visualisations sont maintenant dessinés directement dans les
  nœuds à partir du même document d'interface que leurs éditeurs. Chargez le preset
  du synthé polyphonique : l'oscillateur doit afficher sa forme d'onde et l'enveloppe
  sa courbe ADSR ; les knobs intégrés doivent modifier le son sans ouvrir de fenêtre ;
- double-cliquez dans le vide du canevas pour ouvrir la présentation complète du
  graphe. Déplacez ou ajoutez un contrôle ; pendant la lecture d'une piste audio
  ou MIDI, waveform, spectre et vu-mètre doivent recevoir le signal réel. Fermez,
  sauvegardez/rechargez le projet et vérifiez que le layout complet est conservé ;
- dans cet éditeur, utilisez `GRAPH — FREE` pour déplacer/redimensionner librement
  les contrôles. Ouvrez `DEVICE PARAMS`, décochez quelques paramètres puis passez
  sur `DEVICE — AUTO` : la façade compacte doit être recomposée automatiquement
  avec uniquement les paramètres cochés ;
- revenez sur `PÉRIPHÉRIQUES` et utilisez le bouton `UI` placé sur la façade nodale.
  Il doit ouvrir la même interface complète qu’un double-clic sur le canevas, de
  la même manière que le bouton `UI` d’un VST ;
- dans le graphe, utilisez la molette pour zoomer autour du pointeur, maintenez le
  bouton central pour déplacer la vue et pressez `0` pour revenir à 100 %. Changez
  de piste puis revenez : chaque graphe doit rappeler son zoom et son décalage ;
- clic droit dans le vide du graphe, ouvrez `Add I/O preset`, puis ajoutez les
  paires Audio, MIDI, Valeur et Transport. Chaque choix doit créer deux vrais nœuds
  de frontière connectables. Les mêmes ports apparaissent dans le constructeur
  d'interface ; leur position détermine aussi le point d'arrivée visuel des câbles ;
- activez `FREE UI` dans la barre du graphe, puis déplacez ou redimensionnez un
  contrôle directement dans un nœud. Désactivez ce mode pour reconnecter les
  câbles normalement. Sauvegardez/rechargez et vérifiez la persistance des deux
  paramètres exposés, des ports publiés et de leur type ;
- sélection multiple avec `Ctrl-clic`, duplication avec `Ctrl+D` et création
  d'un module composite réutilisable avec `Ctrl+G` ;
- double-cliquez un nœud `DÉLAI`, `RÉVERBÉRATION`, `COMPRESSEUR`, `EQ`,
  `OSCILLATEUR` ou `BRUIT` : une fenêtre séparée expose ses paramètres DSP réels,
  ainsi que waveform et vu-mètre. Modifiez une valeur, sauvegardez/rechargez le
  projet puis rouvrez le nœud pour vérifier sa persistance ;
- pendant la lecture, déplacez rapidement Time/Feedback/Mix ou les paramètres
  d’un autre effet natif : le son doit suivre sans disparition momentanée du
  graphe. Le bandeau conserve le même nombre de nœuds DSP ; une modification de
  connexion continue en revanche à demander une compilation structurelle ;
- pour tester le constructeur d’interface, sélectionnez `DRIVE`, `LOW-PASS` et
  `SPECTRUM` avec `Ctrl-clic`, créez le module avec `Ctrl+G`, recherchez
  `Module utilisateur 1` dans la bibliothèque, ajoutez-le au graphe puis
  double-cliquez son instance. Une fenêtre séparée doit afficher les knobs Drive
  et Cutoff, waveform, spectre et vu-mètre. Activez `ÉDITER LE LAYOUT` pour les
  déplacer, utilisez `+ CONTRÔLE` ou le clic droit pour ajouter/réassigner, puis
  sauvegardez et rechargez le projet pour vérifier la persistance. Ce module
  exécute maintenant exactement les nœuds sélectionnés. Il peut lui-même être
  sélectionné avec un autre effet puis regroupé avec `Ctrl+G` : les paramètres
  du sous-module remontent dans l’interface du nouveau module et les deux niveaux
  sont aplatis dans le plan DSP ;
- interface anglaise et française, choisie dans la barre supérieure.

Pour tester un plug-in : sélectionnez une piste, utilisez `+ PLUGIN`, choisissez le
bundle puis double-cliquez son entrée dans le navigateur. La première analyse
indexe le plugin ; le double-clic le valide puis l'insère dans la piste. Dans la
chaîne de périphériques, le sélecteur choisit l'instance active ; les flèches la
réordonnent, `UI` affiche son éditeur isolé et `×` la décharge. Dans le mode
`AUTOMATION`, choisissez ensuite `Piste / Gain` ou un paramètre de plug-in dans
la liste supérieure, puis cliquez dans la courbe pour ajouter des points.

Limites connues de ce jalon : l'historique des rendus audio est limité à la
session courante ; la
source résultante est bien sauvegardée avec le projet. L'automation de plug-in est calculée une
fois par bloc (pas encore sample-accurate). L'état binaire est limité à 4 Mio
par instance, la chaîne à huit instances par piste et la ligne de délai PDC à
131 072 échantillons stéréo.
VST2 est désactivé dans les paquets de test standards. Il peut être compilé avec
un SDK historique déjà obtenu légalement ; CyDAW n'inclut ni ne télécharge ce SDK
et refuse une configuration incomplète. Consultez `docs/VST2_BUILD.md`.
Deux variantes Windows sont produites : WASAPI et
ASIO+WASAPI. La variante ASIO a été compilée, a détecté `ASIO Fireface USB` et a
ouvert puis refermé ce pilote à 48 kHz ; le pilote a conservé sa taille matérielle
courante de 1024 échantillons. Sa redistribution publique demande une décision de
licence ; consultez `ASIO_SDK_LICENSE.txt`. JACK est détecté automatiquement mais
son SDK/runtime n'est pas installé sur la machine de validation. Les builds
Linux/Wine et macOS restent à exécuter sur leurs plateformes natives.

Le pipeline natif Linux construit désormais le worker Windows
depuis le même commit, vérifie son handshake, l'installe dans `windows-bridge/`
et teste les décisions Wine/UMU ainsi que les erreurs de protocole. La voie est
aussi couverte par les tests de classification, de chemins invités et d'ABI,
mais son exécution audio complète demande encore une machine Linux native.

Ce paquet est un build de développement interne. Ne le redistribuez pas avant
d'avoir fixé la licence de CyDAW/JUCE et produit les mentions légales définitives.
