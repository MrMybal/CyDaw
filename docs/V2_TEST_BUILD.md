# CyDAW 2 — version de test

## Lancer

Double-cliquez sur `CyDAW 2.exe`. Laissez à côté de lui `CyDAWPluginWorker.exe` et les dossiers
`fonts`, `i18n` et `templates`. Au premier lancement, choisissez la carte son dans
**Options > Réglages audio…** (ASIO ou Windows Audio).

Les exemples s'ouvrent par **Fichier > Modèles**. Un modèle s'ouvre « sans titre » : enregistrer
demande un emplacement et ne modifie jamais l'exemple. Espace lance et arrête la lecture.

## Les exemples

| Modèle | Ce qu'il montre | À regarder |
|---|---|---|
| 07-egaliseur-pro | Batterie et basse, chaque piste réglée par l'**Égaliseur Pro** ; le plateau aigu des charleys est **automatisé** (il descend puis remonte) | Sélectionnez Kick, Snare, Hats ou Basse : la courbe, les poignées et le spectre avant/après dans Périphériques ; sous Hats, le couloir d'automation |
| 08-echos-et-mouvement | Arpège : auto-pan à la noire, écho ping-pong à la croche pointée, chorus ; la réinjection de l'écho est automatisée | Changez le tempo : l'écho et le pan suivent ; « Synchro » propose les valeurs de note |
| 09-glitch-dynamique | Rythme glitch : transitoires sur le kick, gate sur la caisse claire, bitcrusher sur les charleys, filtre auto à la double croche et limiteur sur Glitch, phaser sur la nappe | Les afficheurs pendant la lecture : réduction de gain, ouverture de la gate, balayage du filtre |
| 10-compresseur-pro | La rythmique au **Compresseur Pro** : kick dont l'attaque passe, caisse claire en compression parallèle (le mélange monte au fil de la boucle), charleys nivelés, basse tenue (détecteur filtré à 80 Hz) | Sélectionnez Snare : niveaux qui défilent, réduction de gain en rouge, courbe ; ouvrez-le en grand (⤢) |
| 11-echantillons-et-modeles | **Échantillons contre modèles de son**, côte à côte : piano à queue, harpe, violon et clavecin ont chacun deux pistes voisines, « (échantillons) » puis « (modèle) », réglées au même niveau. La même phrase (les accords du Canon de Pachelbel) passe sur les échantillons pendant 4 mesures, puis aussitôt sur le modèle | Écoutez du début : chaque instrument, échantillons puis modèle. Dans le lanceur, les deux versions sont dans deux scènes voisines ; Solo sur l'une des deux pistes pour comparer |
| 12-tous-les-instruments | **Tous les instruments d'usine** (90) : pour chacun, la piste de ses échantillons et à côté celle de son **modèle de son** (80 en ont un, en stéréo). Quatre mesures par instrument : la même phrase sur les échantillons, puis sur le modèle (une batterie joue son rythme deux fois) | Gros projet (194 Mo, ~9 Go d'échantillons en mémoire) : il s'ouvre en une dizaine de secondes, les échantillons arrivent ensuite. Lancez la lecture au début ou sur un instrument ; Solo sur l'une des deux pistes |
| 14-pas-de-cinq | Un morceau original de jazz cool **en 5/4** (3+2), 160 BPM, swing : saxophone alto, piano, contrebasse et batterie aux balais (instruments d'usine). Intro, thème, pont, solos de sax et de piano, balais seuls, thème final. Le sax est **joué** : une note à la fois en legato avec un glide court, scoops, vibrato tardif, chutes, souffles | La règle compte des mesures de 5 temps. Ouvrez le clip du sax : boutons **Hauteur** et **Volume** de l'éditeur pour voir les courbes dessinées sur les notes ; sur la carte « Allocateur de voix », le jeu Legato et le Glide |
| 19-effets-studio | « Pas de cinq » à travers les quatre **effets de studio** (Réverbe Pro, Compresseur Pro, Limiteur Pro, Filtre Pro), chacun depuis un préréglage d'usine | Sur une carte, le bouton ⤢ ouvre la fenêtre complète de l'effet ; sur le master, le Limiteur Pro et ses mesures de loudness (LUFS) |
| 15-calle-luna | Piano seul, pièce originale néo-classique sur un groove latin (3+3+2), si mineur | Pédale, rubato, mélodie en avant, accords égrenés, crescendo jusqu'au sommet puis retour pianissimo |
| 16-greve-de-l-ouest | Piano seul, pièce originale intime et mélancolique en 3/4, accords brisés | Le jeu très doux, les fins de section qui respirent |
| 17-neons | Piano seul, pièce originale sombre : octaves pulsées sous une ligne lente qui monte jusqu'aux accords pleins | La pulsation régulière et la montée des nuances |
| 18-cycles-lents | Piano seul, pièce originale minimaliste en trois mouvements (figure brisée et chant ; triolets contre croches ; choral) | Jeu égal, nuances en paliers, pédale changée à chaque accord |
| 22-amplis-et-micros | Un morceau rock (la mineur, 110 BPM) joué par des instruments d'usine passés dans les **simulateurs d'ampli, de baffle et de micro** : guitare clean, rythmique crunch, solo, basse dans son ampli, voix à la radio puis au téléphone | Sélectionnez une guitare : sur ses cartes Ampli et Baffle et micro, le bouton ⤢ ouvre leur fenêtre ; glissez les micros sur le haut-parleur |
| 23-instruments-et-effets-natifs | Un morceau original (ré mineur, 100 BPM) joué par les **instruments de CyDAW** et passé dans ses **effets** : nappe Nébuleuse usée par Patine, cloches Mosaïque dans Écho Bande et Halo, Batterie pressée par Pression et Fournaise, basse Colosse, accords Ambre dans Tranche et Velours, lead Prisme, Kick sous la batterie ; sur le master, Multibande Pro, Finition et Sonomètre | Sélectionnez une piste : ses instruments et effets sont dans Périphériques (le bouton « Ouvrir » ouvre la grande fenêtre d'un synthé) ; le master montre la chaîne de finition |
| 24-catalogue-instruments-et-effets | **Tout CyDAW sans clips** : chaque instrument sur sa piste (les quinze synthés dont Silex et Jade, Prisme, Kick, Batterie, le rack de pads, l'échantillonneur simple, Nuée, le sampler, un modèle de son, Poly Keys), chacun avec un son d'usine et un ou deux effets ; entre eux, tous les effets (Réverbe Pro, Écho Bande, Égaliseur Pro, Fournaise, Halo, Filtre Pro, Compresseur Pro, amplis et baffles, Velours, Limiteur Pro, Pression, Multibande Pro, Tranche, Finition, Patine, les effets créatifs du Vocodeur au Peigne, l'Utilitaire, l'Accordeur, et les huit effets MIDI avant leurs instruments) ; sur le master, le spectromètre, le sonomètre et le stéréoscope | Cliquez une piste : son instrument et ses effets sont dans Périphériques ; jouez-la au clavier de l'ordinateur (Ctrl+K) |
| 21-kick-et-batteries-electro | Un morceau techno (la mineur, 126 BPM) : **Kick** accordé sur A1, le kit **909** et les percussions et effets de **CyDAW Electro**, basse roulante et nappe de Prisme ; groove, basse, break avec riser, drop | Ouvrez la fenêtre du Kick (bouton de l'en-tête de sa carte) : glissez les points jaunes et bleus. Les sons des kits se génèrent à l'ouverture (Options › Banques de sons, ligne CyDAW Electro) |
| 13-prisme | **Prisme**, le synthé à tables d'ondes : nappe, pluck, basse Reese, lead supersaw, cloches, puis basse growl et batterie, chacun un son d'usine | Sélectionnez une piste : ouvrez Prisme par le bouton de l'en-tête de sa carte ; essayez les préréglages, glissez un LFO sur un potard |
| 04-ambiance | Un LFO branché sur le filtre de chaque voix du synthé | Le point violet sur le potard modulé, la courbe du filtre qui bouge |
| 01 à 06 | Les modèles d'origine (synthé et filtre, basse et nappes, arpège, ambiance, rythmique, glitch) | Base pour vos essais |

## À essayer

**Atelier Plugin** (bouton « Atelier Plugin » en haut à gauche, ou F7) : toute la fenêtre sert à fabriquer
un effet ou un instrument avec un graphe, puis à l'exporter en VST3.
- « Effet stéréo », « Effet mono » ou « Instrument » (le bouton + propose aussi mono → stéréo et
  multicanal) ; à gauche, l'arbre des **Nœuds** : un clic pose la carte au milieu, un glisser la pose
  où elle tombe ; reliez « Entrée audio » → vos cartes → « Sortie audio » (un câble posé sur une carte
  trouve sa prise ; sur une entrée déjà reliée, il remplace l'ancien ; mono vers stéréo : un
  convertisseur se pose tout seul) ;
- **Interface** (en haut, à côté de Graphe) : la fenêtre du plugin. Glissez des potards, faders,
  courbes… depuis la palette, ou un réglage publié ; déplacez, dimensionnez (le coin), liez-les à droite ;
  « Disposition automatique » pour commencer ; ▷ pour tourner les potards. Le VST3 exporté ouvre cette
  fenêtre telle quelle ;
- le bouton **Entrées et sorties** de la barre du graphe (ou la branche du même nom dans l'arbre)
  ajoute une entrée ou une sortie : audio mono, stéréo ou jusqu'à 8 canaux (une seconde entrée pour un
  sidechain), MIDI, ou de contrôle (une prise sur la carte du périphérique pour y brancher un LFO) ;
- **Tester sur** : choisissez une piste, lancez la lecture (Espace) : chaque changement s'entend ;
- à droite, **Paramètres** : quels réglages le périphérique montre (Panneau) et publie au VST3 ;
- l'icône disquette l'enregistre dans **Mes périphériques** : dans le studio, « + » d'une piste >
  Mes périphériques le remet sur n'importe quelle piste, dans tous les projets ;
- onglet **VST3** > « Compiler le VST3 » (il faut Visual Studio 2022 et les sources de CyDAW ; la
  première fois prend quelques minutes) : le plugin arrive dans `Documents\CyDAW\Plugin Exports` ;
  copiez-le dans `C:\Program Files\Common Files\VST3` pour d'autres logiciels ;
- dans le studio, Ctrl+G dans le Graphe réunit des cartes choisies en un périphérique ; le crayon
  d'un tel périphérique (panneau Périphériques) l'ouvre dans l'atelier.
- **Plusieurs onglets** : chaque périphérique ouvert a son onglet sous la barre du haut ; « + » en
  ouvre un autre, la croix le ferme ;
- **Simulateur** (sous le graphe) : Espace ou Lecture fait jouer le périphérique seul, sans les pistes.
  Pour un effet, choisissez la source (sinus, accords, bruits, batterie, clics, ou un fichier audio) ;
  pour un instrument, un motif (accords, arpège, basse…) et jouez sur le clavier à l'écran ou un
  clavier MIDI. Un périphérique à plusieurs entrées audio : « Autres entrées » (même source, silence,
  kick à chaque temps) pour tester son sidechain. Les afficheurs des cartes montrent le son qui passe ;
- « Studio » (ou F7) revient aux pistes.

**Égaliseur Pro** (Périphériques, « + », tapez « égaliseur ») :
- double-clic dans le vide : ajoute une bande (coupe-bas tout à gauche, coupe-haut tout à droite,
  cloche ailleurs) ; double-clic sur une poignée : la retire ;
- glisser une poignée : fréquence et gain (pour une coupe ou un réjecteur, la hauteur règle le Q) ;
  Maj pour le réglage fin ; molette sur une poignée : Q ;
- clic droit sur une poignée : forme et pente (12 à 48 dB/oct) ; dans le vide : ajouter une bande
  d'une forme donnée ;
- sous l'affichage, les champs de la bande choisie (forme, fréquence, gain ou pente, Q, sortie) ;
- écouter une bande seule : maintenir le casque au-dessus d'elle (ou Alt + clic maintenu) ; pendant
  l'écoute, glisser la déplace et la molette la resserre ;
- bande dynamique : clic droit sur une cloche › « Bande dynamique » ; un losange sous la poignée règle
  la plage, et en lecture la courbe bouge avec le son. En grand : seuil, attaque, relâchement,
  « Sa bande » ou « Sidechain », « Seuil auto ». Essayez le préréglage « Softened Sibilance
  (De-esser) » sur une voix.

**Compresseur Pro** (« + », tapez « compresseur ») : seuil, ratio, genou, attaque, relâchement,
compensation, mélange (compression parallèle) et filtre du détecteur.
- l'historique montre le niveau d'entrée (gris), de sortie (couleur) et la réduction de gain (rouge,
  depuis le haut) sur cinq secondes ; glisser verticalement déplace le seuil (ligne pointillée), la
  molette change le ratio ;
- sur la courbe de transfert : glisser de côté pour le seuil, de haut en bas pour le ratio, molette
  pour le genou ; le point blanc est le niveau qui entre ;
- le vumètre rouge donne la réduction de gain du moment.

**Automation des périphériques** : n'importe quel réglage d'un périphérique de la chaîne (fréquence
d'une bande, seuil, mélange, coupure…), le filtre et la table d'ondes des voix d'un synthé, et les
paramètres des plugins VST3 peuvent suivre une courbe dans l'arrangement.
- clic droit sur un potard ou un champ dans Périphériques : « Automatiser dans l'arrangement » ;
- ou bouton A d'une piste, puis clic sur le nom du couloir (en haut à gauche) : Volume, les paramètres
  déjà automatisés, chaque périphérique et ses réglages, puis chaque plugin et ses paramètres ;
- clic droit sur un potard épinglé d'un plugin : la même chose ;
- dans le couloir : clic pour ajouter un point, glisser pour le déplacer (Alt : sans la grille),
  double-clic pour le retirer, Alt+glisser sur un segment pour le courber, clic droit pour le menu ;
  Ctrl+Z annule ;
- les fréquences et les durées glissent sur leur échelle logarithmique, comme le potard : une ligne
  droite dans le couloir est ce qu'on entend ; dans Périphériques, le point violet sur le potard et la
  courbe violette sur l'afficheur montrent la valeur du moment.

**Ouvrir en grand** : le bouton ⤢ après le titre de chaque affichage (Périphériques ou carte du
graphe) ouvre la fenêtre « Périphérique ». Pour l'égaliseur : pastilles des 8 bandes (clic : choisir,
double-clic : activer/couper), formes dessinées, pentes, potards fréquence / gain / Q, sortie,
échelle ±3 à ±30 dB, note et niveau sous le pointeur. La fenêtre peut sortir de la fenêtre principale
(glissez-la sur un autre écran).

**Les autres effets** (« + » dans Périphériques) : Compresseur Pro, chorus, flanger, phaser, trémolo / auto-pan,
bitcrusher, filtre multimode, filtre auto, EQ 3 bandes, limiteur, gate, transitoires, écho ping-pong.
Chacun a son afficheur, modifiable à la souris.

**Périphériques** : la molette fait défiler la chaîne de côté (sauf au-dessus d'un afficheur ou d'un
potard, qui gardent la leur) ; ✕ retire un périphérique et recolle la chaîne ; ‹ le replie.

**Graphe** : Tab, double-clic ou clic droit dans le vide pour chercher un nœud, glisser une carte sur
un câble pour l'insérer, potards et afficheurs directement sur les cartes ; glisser dans le vide
déplace la vue (Ctrl ou Maj + glisser : sélection par rectangle).
- **LFO sur un bouton** : un périphérique fait d'un graphe a une prise par bouton visible (elles
  apparaissent quand on tire un câble de contrôle, ou sur la carte sélectionnée). Tirez un LFO dessus :
  il **module** le bouton autour de sa valeur (25 % de sa course) ; tournez le bouton, la modulation
  suit. **Clic droit sur le câble** : *Régler la valeur* (le câble impose la valeur) ou *Moduler
  autour de la valeur*, et la **Profondeur** (négative : inversée). Le badge au milieu du câble dit le
  mode (vague : moduler, = : régler).

**Lanceur** : un clic sur le nom d'une piste la choisit (Périphériques et Graphe la montrent),
un double-clic amène ses périphériques. Un clic sur un clip ou une case la **sélectionne** (Ctrl, Maj :
en ajouter) sans rien lancer ; le **triangle** d'un clip le lance, et devient un **carré** qui l'arrête
pendant qu'il joue. Sous les clips, une rangée **■** arrête chaque piste (« ■ Tout » dans la colonne
Master). Une **case vide** porte un petit carré : un clic arrête la piste, et lancer la scène de cette
rangée arrête la piste ; **Ctrl+E** (ou clic droit › « Stop dans la case ») le retire des cases vides
sélectionnées : la piste continue alors de jouer quand cette scène est lancée. Choisir une piste
**n'arme plus** rien : armez à la main (R), plusieurs pistes à la fois ; le clavier joue la piste choisie,
ou les armées quand il y en a. Chaque colonne a sa tranche de mixage en dessous, et la
colonne **Master** est à droite (lancer une scène, volume master, M pour tout couper).

**Clic droit** : sur une piste (arrangement, lanceur, mixeur) son menu ; sur une case du lanceur
celui du clip ou de la case vide.

**Routage et bus** : Piste › Nouvelle piste bus (ou le bouton Bus de l'arrangement). Sous le fader de
chaque tranche, « → Master » : un clic règle où va la piste (le master, un bus, une autre piste) et ses
envois (+ Sortie, chacun à son niveau). Aussi dans l'Inspecteur (section Routage) et le menu d'une
piste (Sortie ›, Envoyer vers un nouveau bus). Mettez une réverbération sur un bus et envoyez-y
plusieurs pistes. **PRE** sur un envoi : pris avant le fader. **Sidechain** : dans l'Inspecteur d'une
piste, « + Entrée » lui ajoute une entrée (un nœud de son graphe, à relier où elle sert, par exemple un
suiveur d'enveloppe qui module un gain) ; les autres pistes s'y routent en choisissant
« Piste › Entrée » comme destination.

**Recréer un son** : il faut Python (3.10 ou plus récent) avec numpy — Options › Compagnon Python dit ce qui
est là et prépare son environnement d'un clic (numpy ; PyTorch CPU ou CUDA en plus, pour l'affinage). Puis
Piste › Recréer un son… : choisissez une note isolée (WAV), ou plusieurs (une par hauteur ou vélocité). L'analyse
prend quelques secondes ; la piste instrument qui en sort joue au clavier. Sur sa carte Modèle de son, tournez
Formants, Brillance, Vitesse ; « Lisible » en fait un patch wavetable modifiable.

**Instruments d'usine** : Navigateur › Instruments (ou Piste › Instruments d'usine) — pianos, clavecins,
harpes, cordes solo et d'orchestre, cuivres, bois, orgues, percussions mélodiques… enregistrés (CC0). Leurs
échantillons ne sont pas dans le paquet : un instrument affiche la taille à télécharger, et le double-clic
crée la piste et les télécharge (elle joue dès qu'ils sont là). Options › Banques de sons… montre chaque
banque (auteur, licence, provenance) et la télécharge en entier ; Options › Emplacements… choisit où elles
vont (plusieurs Go). Clic droit › Modèle de son : les mêmes notes analysées, sans rien télécharger. Le
premier accord peut rester muet le temps que les échantillons se chargent (quelques secondes pour un
piano).

**Sampler** : Piste › Nouveau sampler… avec vos propres fichiers (la note vient du nom, `C4`, `A#2`… ou de
la hauteur), ou un instrument SFZ (Piste › Ouvrir un instrument SFZ…, ou glissé dans la fenêtre). Sur la
carte Sampler, glissez d'autres fichiers ; « Zones… » ouvre l'éditeur (touches, vélocités, accord, boucle à
glisser sur la forme d'onde).

**Préréglages** : l'icône de liste sur l'en-tête d'une carte, d'un plug-in ou au pied de la colonne d'une
piste (sa chaîne entière) ; une barre en haut de la fenêtre d'un périphérique. Tapez un nom et
« Enregistrer » ; un clic en recharge un (Ctrl+Z l'annule) ; précédent et suivant ; clic droit pour renommer,
remplacer ou supprimer. Un plug-in VST montre aussi ses propres programmes en tête de la liste. Dans
l'atelier, l'onglet **Préréglages** garde les réglages du périphérique sous des noms : ils deviennent les
programmes du VST3 exporté (la liste de préréglages de votre hôte). Le dossier se choisit dans Options ›
Emplacements.

**Prisme** : Piste › Nouveau Prisme (synthé à tables d'ondes). Trois oscillateurs à tables d'ondes avec unisson, déformations et **spectre** (la table remodelée dans ses harmoniques : passe-bas, passe-haut, impairs, pairs, peigne, octave, formant), sub, bruit, filtre multimode, 4 enveloppes, 4 LFO (stéréo si on veut), matrice de 16 modulations, macros, mono/legato/glissando, onglet Effets ; glissez une source (ENV, LFO, macro) sur un potard pour le moduler. 30 sons d'usine dans ses préréglages. Voir docs/PRISME.md.

**Synthés natifs** (Piste › Nouveau…, chacun sa carte, sa grande fenêtre et 22 sons d'usine ; voir docs/SYNTHES.md) : **Ambre** (synthé analogique : trois oscillateurs, quatre filtres de caractère ; glissez la courbe du filtre et les points des enveloppes), **Colosse** (grand synthé moderne : deux oscillateurs à tables d'ondes déformables, deux filtres de sept sortes, une séquence de 16 pas à dessiner, six modulations libres), **Mosaïque** (synthé hybride : deux moteurs analogique, tables, harmonique ou modal — un maillet, un souffle ou un archet sur du bois, du métal, du verre, une corde, une peau —, une fonction dessinée, de l'aléatoire, quatre macros), **Nébuleuse** (synthé d'atmosphères : deux couches de synthèse ou de grains pris dans sept textures, des accords, l'**orbe** qui fait tout bouger ; livré avec Halo derrière).

**Batterie** : Piste › Nouvelle Batterie (kits acoustiques). Six kits libres (rock, jazz, pop, baguettes, balais, années 70 ; leurs banques se téléchargent dans Options › Banques de sons si elles manquent), un kit dessiné où l'on clique un élément, une console (niveau, panoramique, accord, durée, muet, solo pour chaque élément), et des motifs (rock, funk, bossa, jazz, metal…) à poser sur la piste en clips MIDI. 13 sons d'usine. Voir docs/BATTERIE.md.

**Fenêtres séparées** : le bouton ⧉ dans l'en-tête d'une carte (Périphériques) ouvre la fenêtre du périphérique hors du studio, dans une vraie fenêtre Windows, comme un VST : grande vue d'un effet, Prisme, Kick, sampler, interface dessinée. Plusieurs peuvent être ouvertes à la fois ; le même bouton dans la fenêtre la remet dans le studio. La grande vue des effets natifs a ses préréglages, ses valeurs en grand, le spectre avant/après, les niveaux, et une façade de gros potards avec les choix en boutons.

**Amplis et micros** : dans le catalogue des périphériques, Effets › Amplis et micros. **Ampli** (8 modèles, du clean américain au métal et aux amplis basse ; pédale, noise gate, tone stack de vrai ampli), **Baffle et micro** (8 enceintes, un ou deux micros que l'on glisse sur le haut-parleur, leur distance et leur angle), **Micro** (micros de studio, téléphone, mégaphone, radio ancienne, talkie-walkie). Des préréglages d'usine pour chacun. L'ampli a deux canaux (**Canal A | Canal B**, un modèle et des potards chacun, passage sans clic, automatisable) ; le baffle et le micro règlent la **taille** et le **ton** de leur pièce. Voir docs/AMPLIS_ET_MICROS.md.

**Effets de mixage** : **Pression** (Dynamique : compression vers le haut et vers le bas sur trois bandes ; tirez les lignes rouge et bleue de chaque colonne), **Fournaise** (Effets › Distorsion : saturation en cinq caractères, bande, console, classe A, lampe, pentode), **Écho Bande** (Effets › Échos et réverbération : écho à bande à trois têtes, douze modes, ressort, pleurage, usure), **Multibande Pro** (Dynamique : quatre bandes ; tirez les coupures de côté et les seuils de haut en bas), **Velours** (Dynamique : calme les résonances ; tirez les bords de sa zone), **Patine** (Effets › Couleur et usure : bruit de disque ou de bande, ondulation, distorsion, numérique, pièce, bande magnétique), **Sonomètre** (Analyse et affichage : LUFS contre une cible, à mettre en dernier sur le Master). Deux suites : **Tranche** (la tranche d'une piste : gate, égaliseur, compresseur, transitoires, excitateur ; ouvrez sa grande fenêtre, lancez la lecture et cliquez « Assistant : écouter ») et **Finition** (le mastering : égaliseur, dynamique trois bandes, excitateur, image stéréo, maximiseur ; son assistant règle le gain pour atteindre la cible choisie). **Halo** (Effets › Échos et réverbération) : une réverbe de caractère, dix algorithmes en trois époques (années 70, années 80, maintenant). Des préréglages d'usine pour chacun. Voir docs/EFFETS_MIXAGE.md.

**Kick** : Piste › Nouveau Kick (synthé de grosse caisse). Un corps dont la hauteur tombe, un clic, un sub, une saturation et un timbre ; la fenêtre dessine le kick, glissez ses points (hauteur en jaune, enveloppe en bleu, longueur en blanc). « Fin 55 Hz · A1 » donne la note pour l'accorder. 17 sons d'usine (808, 909, techno, trap, hardstyle…). Voir docs/KICK_ET_ELECTRO.md.

**Batteries électroniques** : dans le navigateur, onglet Instruments, « Batteries électroniques » : 10 kits (808, 909, techno, house, trap, lo-fi, hard dance, drum and bass, collection de kicks, percussions et effets). Leurs 93 sons ne se téléchargent pas : CyDAW les fabrique (Options › Banques de sons › CyDAW Electro › Générer, ou un double-clic sur un kit) ; ils arrivent aussi dans le navigateur de samples.

**Bibliothèque en grand (F9)** : Affichage › Bibliothèque, le bouton ⤢ du navigateur, ou le + de la chaîne de périphériques (qui l'ouvre sur les effets et ajoute à la piste ce qu'on choisit). Trois onglets, Sons, Instruments, Effets et périphériques (plug-ins VST3 compris), chacun avec un **filtre multicolonne** : cliquez une valeur, Ctrl+clic pour en ajouter, Maj+clic pour une plage ; les nombres disent combien chaque choix donnerait. Grand tableau trié en cliquant les en-têtes, volet d'écoute à droite (flèches : son suivant, Entrée : sur la piste). ⤢ : toute la fenêtre ; ⧉ : une fenêtre à part. Voir docs/BANQUE_DE_SONS.md.

**Banque de sons** : Navigateur › Samples montre aussi les sons des banques de CyDAW (51 872 sur une machine qui les a toutes, analysés en une minute la première fois), rangés dans un arbre famille › catégorie › instrument, puis par banque ; un clic sur une branche filtre. Clic droit sur un son : Sons similaires, Catégorie, Montrer dans l'explorateur. L'ampoule à droite de la recherche : la **recherche par description**, optionnelle — Options › Recherche sémantique des sons installe le moteur Python et un modèle CLAP (licence Apache-2.0 affichée, téléchargement de 615 Mo confirmé, ou « Depuis un dossier… » pour reprendre celui de CyTools AudioLibrary), puis indexe vos sons (environ 10 minutes pour 50 000 sons sur une carte NVIDIA). Cherchez « kick sombre », « nappe aérienne », « vinyl crackle ». Voir docs/BANQUE_DE_SONS.md.

**Se déplacer** : dans l'arrangement, la bande du haut de la règle se saisit (glisser de côté pour se
déplacer, vers le bas pour zoomer) ; une barre verticale fait défiler les pistes ; se déplacer pendant
la lecture ne vous ramène plus à la tête de lecture (le bouton Suivre la reprend). Dans le lanceur, seules
les scènes défilent : les noms et les tranches restent en place.

**Ouvrir un gros projet** (12-tous-les-instruments) : une barre montre l'avancée piste par piste, la
fenêtre reste vivante.

**Master** : cliquez sur son nom (en bas à droite du lanceur, ou dans le mixeur) : ses effets
s'affichent dans Périphériques et Graphe. Un limiteur y est de base ; « + » en ajoute d'autres.

**Fenêtre d'un périphérique** : sur la carte d'un périphérique fait d'un graphe (Périphériques), l'œil
ouvre son interface dessinée dans une fenêtre à part, comme un plugin (aussi au pied de la colonne d'une
piste MIDI pour son instrument).

**Catalogue « + »** : rangé en arbre (Mes périphériques, Effets › …, MIDI, Modulateurs, chaînes
complètes, nœuds) ; tapez pour chercher à plat.

**Langues** : Options › Langue change toute l'interface. Les noms des nœuds restent en anglais
(Options › Langue › « Noms des nœuds en anglais » pour les traduire). D'autres langues s'ajoutent par
fichier (voir docs/V2_INTERFACE.md, « Langues »).

**Fenêtres** : un onglet tiré hors de la fenêtre principale (ou Affichage > Détacher dans une
fenêtre) devient une fenêtre à part, à poser sur un autre écran. **Affichage > Dispositions** :
« Enregistrer la disposition… » garde la disposition du moment sous un nom (panneaux, onglets,
fenêtres détachées) ; un clic sur son nom la remet.

**Édition audio** (docs/EDITION_AUDIO.md) : double-cliquez un clip audio. *Échantillon* : glissez pour
sélectionner, puis Couper, Coller, Silence, Rogner, Fondus (quatre formes), Normaliser, Gain,
Inverser… ; l'historique à droite rejoue une version, Ctrl+Z annule. *Prises* : enregistrez sur un clip
(ou en boucle) puis glissez sur une prise pour garder ce passage-là ; « Mettre à plat » en fait un
seul fichier. *Étirement* : Tonal, Rythme ou Texture, la transposition, « Caler sur le tempo du
projet », les repères. *Événements* : plusieurs sons dans un clip (« + Son… » ou un son glissé du
navigateur), à déplacer, raccourcir par leurs bords, couper (S), dupliquer (Ctrl+D).

**Modulation** : Graphe › Modulation édite le graphe qui module la piste (LFO, macros, calculs) ; les
paramètres modulés se cochent dans l'Inspecteur, ceux des plugins VST3 compris.

**Plugins VST3** : les potards de la carte montrent la valeur du plugin ; … ouvre tous ses paramètres
en potards ; ← → réordonnent la chaîne.

**Prisme** : le bouton dossier d'un oscillateur importe une table d'ondes (.wav de Serum ou Vital) ou
n'importe quel son. **Baffle et micro** : IR… charge une réponse impulsionnelle. **Kick** : onglet
EFFETS.

**Mixage** : les effets qui anticipent (Compresseur et Limiteur Pro, l'ampli suréchantillonné)
retardent leur piste ; les autres l'attendent, et l'export commence à l'heure. Le bouton maillons d'un
Compresseur Pro ou d'une Porte (sur sa carte, dans Périphériques) choisit la piste que son détecteur
écoute (un kick qui fait pomper une basse). Les réglages de Prisme, de Kick, du sampler et ceux à
l'intérieur d'un module s'automatisent dans l'arrangement comme les autres.

## Limites connues

- Dans le graphe, seule la sortie de l'Égaliseur Pro a une entrée de modulation ; les bandes se
  modulent par le LFO de la piste (Inspecteur) ou s'automatisent dans l'arrangement.
- Compiler un VST3 demande Visual Studio 2022 et les sources de CyDAW : sans elles, l'atelier crée
  seulement le projet.
- Version non signée : Windows SmartScreen peut demander une confirmation au premier lancement.

## Signaler un problème

Notez le modèle ouvert et ce que vous avez fait, et joignez une capture d'écran. Après un plantage,
la pile d'appels est dans `%TEMP%\cydaw2-crash.txt` ; la session est proposée au relancement.
