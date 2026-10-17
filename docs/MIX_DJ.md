# Mix DJ

Le mode **Mix DJ** prend toute la fenêtre, comme l'Atelier Plugin : on y mixe des morceaux comme sur des
platines, avec une table de mixage et une collection. On y passe par le bouton **Mix DJ** de la barre du
haut (ou F10, ou Affichage › Mix DJ) ; **Studio** ramène aux pistes. Le son du mix rejoint le master comme
celui des pistes : ses effets, son volume et son vumètre s'appliquent après lui. Les pistes du projet
peuvent jouer en même temps.

Les commandes portent les noms qu'elles ont sur le matériel DJ (CUE, SYNC, HI MID LOW, FILTER…), en
anglais quelle que soit la langue, comme sur n'importe quelle table.

## Les modules du Studio

Le bouton **Masquer** à droite des onglets du panneau du bas, **Ctrl+J** ou **Affichage › Panneau du bas** masque ou réaffiche
les outils du bas. La place libérée agrandit les platines et la collection. Les effets restent actifs ;
l’état d’affichage est mémorisé séparément pour Studio, Mix DJ et Fonderie.

Sous les platines et la collection, une zone redimensionnable reprend **Périphériques**, **Graphe**,
**Mixeur**, **Routage** et **Éditeur**. Cliquer l'en-tête A/B/C/D d'une tranche choisit sa chaîne, ou la
choisir dans le sélecteur de la zone inférieure. **+ Effet** ouvre le catalogue habituel du Studio :
plusieurs modules peuvent être ajoutés, réglés et réordonnés. **MASTER** sélectionne le master de
traitement du Mix DJ ; **+ Bus** ajoute un bus, câblé dans Routage. Le mixeur et les graphes utilisent
les mêmes vues et le même moteur que ceux du Studio, avec leur annulation.

Ces chaînes viennent après l'EQ, le filtre, le fader/crossfader et le FX de chaque platine, puis passent
par leurs bus et leur master de traitement. Le gain MASTER DJ et son limiteur s'appliquent ensuite.
L'enregistrement DJ inclut donc ces modules et le routage. La pré-écoute CUE reste prise avant le fader
et avant ces chaînes, comme la pré-écoute DJ existante. Le mix rejoint ensuite la sortie master commune.
L'onglet Éditeur permet d'ouvrir le son d'une platine dans la Fonderie.

La table de traitement est conservée dans `DJ processing.cydjmix` du dossier des mixages (Options ›
Emplacements). Elle se recharge au prochain usage du mode sans ajouter de pistes au projet Studio.
Pour les tests isolés, `CYDAW_DJ_PROCESSING_FILE` permet de choisir un autre fichier.


Les réglages des modules restent audibles pendant le geste. Leur sauvegarde attend la fin du
geste ; l’écriture JSON du traitement se fait en arrière-plan. Le changement de mode et la fermeture
conservent aussi le dernier état. Le lanceur d’exemple « Rejouer le mix » automatise les faders et
l’EQ ; utiliser « Préparer les platines » pour garder la main sur ces commandes.

## Les platines

Deux platines (A à gauche, B à droite) ou quatre (C sous A, D sous B : bouton **2 DECKS / 4 DECKS**).
Un morceau se charge en le glissant sur une platine (depuis la collection, le navigateur du studio ou
l'explorateur), par un double-clic dans la collection (la première platine libre) ou par le clic droit
d'un clip audio ou d'un son du navigateur (« Charger sur une platine »).

Le morceau est décodé en entier, puis **analysé** en tâche de fond :

- son **tempo** et sa **grille** (le premier temps, posé sur les kicks), cherchés entre 70 et 180 BPM ;
- sa **tonalité**, avec son numéro sur la roue des quintes (8A = la mineur, 8B = do majeur…) ;
- sa **sonie** (LUFS), qui règle son **GAIN** quand **AUTO GAIN** est allumé (chaque morceau aussi fort
  que les autres, ses crêtes gardées 1 dB sous le maximum) ;
- sa forme d'onde, colorée par ses graves (bleu), ses médiums (ambre) et ses aigus (blanc).

En haut de chaque platine : le titre, l'artiste, le tempo joué, la tonalité et le temps restant (un clic :
le temps écoulé). Puis l'**aperçu** du morceau entier (un clic ou un glisser : y aller) et la **grande
onde** centrée sur la lecture, avec les temps et les mesures de la grille.

- **Tenir et glisser la grande onde** : le morceau suit la main (scratch), dans les deux sens ; lâché,
  il reprend. La molette zoome (2 à 128 temps).
- **CUE** : en lecture, revient au point de cue et s'arrête ; à l'arrêt, pose le point de cue là (sur le
  temps le plus proche quand **QUANT** est allumé) et joue tant qu'il est tenu.
- **▶** : lecture / pause.
- **SYNC** : la platine suit le tempo de la platine maître, temps calés (la petite barre sous l'onde
  montre l'écart de phase ; verte quand c'est calé). **MASTER** : cette platine donne le tempo ; sinon
  c'est automatique (celle qui joue et mène).
- **KEY** : la tonalité gardée quand le tempo change (étirement par grains, sans effet de « vinyle
  accéléré ») ; éteint, la hauteur suit la vitesse.
- **⏮ / ⏭** et leur nombre : sauts de 1/4 à 64 temps.
- **1 à 8** : les hot cues. Un clic saute au repère (ou le pose s'il est vide) ; Maj+clic ou clic droit
  l'efface. Quantifiés, les sauts gardent le temps où l'on était.
- **Boucles** : 1/4 à 32 temps depuis le temps où l'on est ; encore le même bouton : la boucle s'arrête.
  **/2** et **x2** la raccourcissent ou l'allongent, **‹ ›** la déplacent d'un temps.
- **FX** : un effet par platine, après son fader (un écho continue une fois le fader fermé) : Echo,
  Reverb, Flanger, Phaser, Filter LFO, Gater, Stutter, Bitcrusher, réglés en temps sur le morceau, avec
  **D/W** (sec/effet) et deux réglages.
- **Le fader de tempo**, à droite : plus lent en haut, plus rapide en bas, comme sur une platine
  (étendue : 4, 8, 16 ou 50 %, en cliquant dessus ; double-clic : 0 %). **+ / −** poussent la platine
  tant qu'ils sont tenus, pour caler à l'oreille.

## La table de mixage

Une tranche par platine (C A | B D en quatre platines) :

- **GAIN** (−24 à +12 dB), puis **HI**, **MID**, **LOW** : un égaliseur « isolateur » à trois bandes
  (coupures à 250 Hz et 3 kHz). Au milieu, 0 dB ; tout à gauche, la bande disparaît. **K** la coupe d'un
  coup (kill).
- **FILTER** : à gauche un passe-bas, à droite un passe-haut, au milieu rien.
- **CUE** : la tranche dans le casque (voir SPLIT), avant son fader.
- Le **fader** et le vumètre de la tranche ; **L / - / R** : son côté du crossfader (« - » : le fader seul).

En bas : le **crossfader** (double-clic : le milieu), **MASTER** (le volume du mix), **CURVE** (la courbe
du crossfader : douce à gauche, coupante à droite pour le scratch), **CUE MIX** (le casque : la pré-écoute
seule, le mix seul, ou entre les deux), **SPLIT** (casque séparé sur une sortie stéréo : le mix en mono à
gauche, la pré-écoute à droite, pour un câble en Y), **AUTO GAIN** et **LIMIT** (le limiteur du master :
le mix ne dépasse jamais −0,3 dBFS ; il s'allume en ambre quand il travaille).

Dans la barre du haut : le tempo de la platine maître, **●** pour **enregistrer le mix** (un WAV 24 bits
dans le dossier « Mixes DJ », Options › Emplacements), **QUANT** et le nombre de platines.

## La collection

En bas de la fenêtre (la ligne au-dessus se tire pour lui donner plus ou moins de place) :

- à gauche, **Tous les morceaux**, **Joués cette session** et les **dossiers** de musique (+ pour en
  ajouter ; le dossier Musique de Windows au premier lancement ; clic droit : le retirer) ;
- à droite, les morceaux : titre, artiste, tempo, tonalité, durée, nombre de lectures. Un clic sur un
  en-tête trie ; la recherche cherche dans les titres, les artistes et les noms de fichiers. Un clic
  droit : charger sur une platine, analyser, ouvrir dans la Fonderie, afficher dans l'explorateur.
- **Tout analyser** analyse en tâche de fond les morceaux qui ne le sont pas encore.

La collection est celle du logiciel, pas d'un projet : `%APPDATA%/CyDAW/DjCollection.json`. Elle garde
pour chaque morceau son tempo, sa grille, sa tonalité, sa sonie, son point de cue, ses hot cues et ses
lectures ; un morceau rechargé retrouve ses repères. Les formats lus : WAV, AIFF, FLAC, Ogg Vorbis, MP3 et
WMA (par Windows), jusqu'à 45 minutes par morceau.

## Pour les scripts

Le port de contrôle (et le serveur MCP) ont la commande `dj` (`{"action": …}` : load, play, cue, sync,
tempo, hotcue, loop, beatjump, channel, mixer, record, folders, analyse, tracks… ; `status` donne chaque
platine, ses niveaux et la collection) et `app_mode` (`studio`, `dj`, `foundry`, `workshop`). Le test
`cydaw2_dj_check` (silencieux) les parcourt.
