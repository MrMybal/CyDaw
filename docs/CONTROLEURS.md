# Contrôleurs MIDI / OSC

**Options → Contrôleurs et entrées…**, ou le bouton **CTRL** de la barre d'outils, ouvre la fenêtre. Un clic droit sur **CTRL**, ou **Ctrl+M**, passe en mode **Apprendre**.

La fenêtre est faite autour des **périphériques**. Un périphérique, c'est un appareil que CyDAW écoute : votre clavier MIDI, une manette, une tablette OSC, le clavier de l'ordinateur. On le crée, on lui donne son matériel, puis on apprend ses règles en bougeant ses contrôles un par un.

Quatre onglets :
- **Périphériques** : vos périphériques et leurs règles (l'essentiel) ;
- **Matériel détecté** : ce que CyDAW voit tel quel ;
- **Toutes les assignations** : tout ce que les règles pilotent, d'un coup d'œil ;
- **Clavier musical** : jeu de notes, octave, vélocité, aperçu des touches et création d’un contrôleur clavier personnalisé.

## Créer un périphérique

1. **+ Nouveau périphérique**. CyDAW attend aussitôt que vous bougiez quelque chose sur l'appareil (« Bougez-le… »). Le premier potard, bouton, touche ou stick bougé désigne le matériel. Le périphérique prend son nom, que l'on peut changer en haut. On peut aussi choisir le matériel dans la liste **Entrée**. **Échap** annule l'attente.
2. **1 · Matériel** :
   - **Entrée** : ce que le périphérique écoute. La liste est rangée par familles : MIDI, cet ordinateur (le clavier), les plug-ins d'application (les manettes), OSC. Une manette XInput n'apparaît que branchée. **+ Nouveau point OSC (réseau)…** ouvre un port UDP pour une tablette ; ses réglages (port d'écoute, hôte et port de réponse, adresses `/cydaw/…`) s'affichent sous l'entrée.
   - **Détecter** : reprend l'attente du point 1.
   - **Notes** (entrée MIDI) : allumé, les notes et contrôleurs de l'appareil jouent aussi l'instrument sélectionné, sauf ceux qu'une règle pilote. Éteint, l'appareil ne sert qu'à piloter. C'est un réglage du port, partagé par les périphériques qui l'utilisent.
   - **Retour** : où les valeurs repartent (LED, faders motorisés, écran d'une tablette, vibration d'une manette). Par défaut, la sortie MIDI du même nom, le même point OSC ou la même manette.
   Changer l'entrée ou le retour emmène les règles avec : elles écoutent la nouvelle entrée et répondent au nouveau retour.
3. **2 · Règles** : **Apprendre**, puis bougez les contrôles de l'appareil un par un. Chaque nouvelle adresse devient une règle, avec le type qui lui va (une touche : un pad ; un bouton de manette ou une touche du clavier : un bouton ; un stick ou le pitch bend : un axe ; sinon un potard). Le relâchement d'une touche n'apprend rien, et un autre appareil n'est pas écouté. **Échap** ou **Apprendre** termine. **+ Règle** ajoute une règle vide, à apprendre ou à écrire.
4. Choisissez une règle dans la liste pour la régler à droite (voir plus bas), et dire **ce qu'elle pilote**.

Le moniteur, en bas, montre ce qui arrive de ce périphérique et ce qui y repart (**Tout le matériel** pour tout voir).

Clic droit sur un périphérique de la liste : **Dupliquer** ou **Supprimer**. Une copie garde les règles et ce qu'elles pilotent, mais attend son propre matériel : pratique pour un deuxième appareil du même modèle.

## Une règle

Une règle, c'est un contrôle de l'appareil, ses modifications, et ce qu'il pilote dans CyDAW.

- **Nom** et **type** : Potard, Fader, Encodeur, Bouton, Interrupteur, Pad ou Axe. Le type pose la chaîne de modifications qui lui convient : « Relatif » pour un encodeur, « Seuil » pour un bouton, « Bascule » pour un interrupteur, « Zone morte » pour un axe.
- **Contrôle** : son adresse sur l'entrée du périphérique. **Apprendre** prend l'adresse du prochain contrôle bougé.
- **Retour** : coché, sa valeur repart vers l'appareil quand la cible bouge autrement, à l'adresse indiquée.
- **Avancé** : une autre entrée, ou un autre retour, que ceux du périphérique (rare).

Les adresses s'écrivent ainsi, avec des valeurs de 0 à 1 :
- MIDI : `cc/<canal 1..16>/<numéro>`, `note/<canal>/<note>` (sa vélocité ; 0 au relâchement), `pb/<canal>` (pitch bend, 0,5 au milieu), `at/<canal>` (aftertouch), `pat/<canal>/<note>` (aftertouch d'une note), `pc/<canal>/<programme>` ;
- clavier de l'ordinateur : `key/<touche>` (`key/F2`, `key/Space`, `key/A`) ;
- manette : `button/a`…, `axis/leftX`…, `trigger/left`… ;
- OSC : l'adresse reçue ; un deuxième nombre se lit à `<adresse>#1`.

Les **modifications** s'appliquent dans l'ordre, de l'appareil vers CyDAW, puis à l'envers pour le retour :

| Modification | Effet | Réglages |
| --- | --- | --- |
| Plage d'entrée | seule la partie [a, b] de la course compte | a, b |
| Inverser | le haut devient le bas | |
| Courbe | négative : lente au début ; positive : rapide | a (-1..1) |
| Courbe dessinée | réponse interpolée entre 17 points déplaçables ; double-clic : droite | points |
| Relatif (encodeur) | chaque cran avance ou recule depuis la valeur actuelle | codage : complément à deux, décalé de 64, ou bit de signe ; taille d'un cran |
| Bascule | un appui allume, le suivant éteint | |
| Seuil (bouton) | 1 au-dessus du seuil, 0 en dessous | a |
| Zone morte | le milieu d'un stick reste immobile | a |
| Pas | la valeur se pose sur n crans | a |
| Plage de sortie | le contrôle ne couvre que [a, b] | a, b |

Un encodeur repart toujours de la valeur actuelle de sa cible. Aucun retour n'est renvoyé au contrôle qui vient de bouger la cible.

### Ce qu'elle pilote

- **+ Cliquer la cible dans CyDAW** : tout ce qui peut être piloté s'encadre ; cliquez ce que la règle doit bouger. **Échap** annule.
- Ou cherchez une cible par son nom (volume, cutoff, transport…).

Une règle peut piloter plusieurs cibles, et une cible peut être pilotée par plusieurs règles (un fader et un encodeur sur le même volume, par exemple). Chaque cible a :
- une **plage**, la partie de sa course que la règle couvre ;
- une **reprise** : la cible ne bouge qu'une fois le contrôle arrivé à sa valeur, sans saut.

Les potards suivent la même échelle que la souris (logarithmique pour une fréquence ou un temps).

Les assignations aux pistes et à leurs cartes sont **gardées avec le projet** ; elles suivent les pistes à la réouverture. Celles du transport, du Mix DJ, du master et de la piste sélectionnée valent **pour tous les projets**, avec les périphériques.

## Apprendre depuis l'interface

L'autre sens : **Apprendre** (en haut de la fenêtre, clic droit sur **CTRL** ou **Ctrl+M**) couvre toute l'interface d'un calque. Tout ce qu'un contrôleur peut bouger est encadré, y compris dans les fenêtres détachées :
- le transport (lecture, stop, enregistrement, boucle, tempo) ;
- le mixeur, l'inspecteur et les en-têtes de pistes (volume, panoramique, muet, solo, armer) ;
- le master ;
- les potards et les cases de valeur des cartes et des instruments ;
- le Mix DJ (gain, égaliseur, kills, filtre, faders, pré-écoute, crossfader, master, lecture, CUE, SYNC).

On clique une commande, puis on bouge un contrôle :
- la règle va dans le périphérique dont c'est l'entrée ; s'il n'y en a pas, CyDAW en crée un, au nom de l'appareil ;
- en apprenant, le nouveau contrôle remplace l'ancien sur la cible ;
- un clic droit sur une commande retire son assignation ;
- **Échap** abandonne la commande choisie ; un second **Échap**, ou **Terminer**, sort du mode.

## Matériel détecté

Ce que CyDAW voit tel quel :
- **Entrées MIDI** : la case ouvre ou ferme le port (le même réglage que l’onglet *Matériel détecté* de cette fenêtre), et **Notes** comme plus haut ;
- **Sorties MIDI** : ouvertes seulement au premier retour à envoyer ; un port déjà pris par un autre logiciel est signalé ;
- **Cet ordinateur** : le clavier ;
- **Plug-ins d'application** : leurs appareils, et les plug-ins chargés ;
- **OSC** : **+ Point OSC** écoute un port UDP, sur cette machine et sur le réseau local. **Adresses /cydaw/… directes** : `/cydaw/<cible> valeur` bouge la cible sans règle, par exemple `/cydaw/transport/play`, `/cydaw/track/selected/volume 0.8` ou `/cydaw/dj/A/high 0.5`. Une cible ainsi touchée est renvoyée à l'adresse quand elle bouge ailleurs. Un message OSC sans nombre vaut un appui puis un relâchement.

À côté de chaque appareil, les périphériques qui l'utilisent, et **Nouveau périphérique avec celui-ci**. Le moniteur de l'onglet montre tout le matériel.

## Le clavier de l'ordinateur

C'est un appareil comme les autres. Les touches de ses règles ne font plus leur raccourci ni leur note, sauf pendant une saisie de texte. Ctrl et Alt gardent les raccourcis, et Échap n'est jamais pris (il arrête un apprentissage). Toutes les touches ne sont écoutées que pendant qu'on apprend pour lui.

## Moniteur

Le moniteur affiche ce qui arrive (`>`) et ce qui repart (`<`) : l'appareil, l'adresse (et sa lecture : `CC 74 · ch. 1`, `Touche F2`) et la valeur (0..127 pour le MIDI). **Pause** fige la liste, **Effacer** la vide.

## Plug-ins d'application

Ce ne sont pas des plug-ins audio : ils apportent des appareils RAW que CyDAW ne connaît pas lui-même (manette, boîtier HID, protocole réseau). Un plug-in est une bibliothèque partagée renommée `.cyctl`, placée dans `ControlPlugins`, à côté de CyDAW, ou dans `Documents/CyDAW/Control Plugins`. Elle exporte une fonction C, `cydaw_control_plugin()`, décrite dans `include/cydaw/control/ControlPluginABI.h`.
- CyDAW appelle `start()`, puis `poll()` environ cent fois par seconde sur un fil à lui, `send()` sur ce même fil pour le retour, et `stop()` avant de décharger le plug-in.
- Le plug-in annonce ses appareils, branchés et débranchés, et leurs messages : une adresse à lui et une valeur de 0 à 1.

L'exemple livré, **Gamepads (XInput)** (`tools/control_plugins/gamepad`), lit les manettes sous Windows :
- `button/a`…`button/right` ;
- `axis/leftX`, `axis/leftY`, `axis/rightX`, `axis/rightY` (0,5 au milieu) ;
- `trigger/left` et `trigger/right` ;
- en retour, `rumble/left` et `rumble/right` font vibrer la manette.

Le plug-in tourne dans le processus de CyDAW : un plug-in défaillant peut le faire tomber.

## Port de contrôle et MCP

Commande `controllers` :
- `status`, `scan` ;
- `add_osc`, `set_raw`, `remove_raw` ;
- `inject {device, address, value}` (comme un message reçu ; `keyboard:computer` pour le clavier) ;
- `add_virtual {name, input, output}`, `set_virtual {device, name, input, output}`, `duplicate_virtual`, `remove_virtual` ;
- `learn_hardware {device, on}`, `learn_controls {device, on}` ;
- `add_control`, `set_control`, `remove_control` ;
- `assign`, `unassign`, `learn {on, target}` ;
- `targets {filter}`, `value`, `apply`, `feedback`, `monitor` ;
- `show {tab devices|hardware|assignments, device, control, center}` (`center` ramène la fenêtre sur la fenêtre principale).

Test : `tests/v2_controllers_check.py` (ctest `cydaw2_controllers_check`, silencieux). Il passe par de vrais sockets UDP sur 127.0.0.1 et couvre :
- l'aller et le retour OSC, à travers une plage ;
- un encodeur, un bouton, la reprise ;
- les adresses `/cydaw/…` ;
- l'apprentissage depuis l'interface ;
- un périphérique : son matériel trouvé en bougeant un contrôle, ses règles apprises, son entrée changée, sa copie ;
- une touche du clavier de l'ordinateur ;
- un potard de carte et l'égaliseur d'une platine ;
- le projet enregistré, un nouveau projet, puis le projet rouvert ;
- le chargement du plug-in manette.

## Limites

- Pas encore de MIDI 14 bits (CC MSB/LSB, NRPN), de SysEx ni de profils prêts pour des surfaces du commerce (protocoles de surfaces de mixage, LED des contrôleurs DJ).
- Les notes d'un appareil vont toutes à l'instrument sélectionné : pas encore de routage par appareil ou par canal vers une piste choisie.
- Pas de sortie MIDI des pistes vers un instrument externe.
- Les manettes passent par XInput (Xbox et compatibles) ; les autres manettes demandent un plug-in d'application.
- Un périphérique ne s'exporte pas encore en fichier pour être partagé.
- Les plug-ins des pistes (VST3, CLAP, LV2, JSFX) et les pads d'un rack ne sont pas encore des cibles.

## MIDI direct et macros

Dans une règle, utiliser **+ Destination MIDI directe**, choisir CC, Note, Pitch bend ou Pression, puis régler canal et numéro. Les messages entrent dans le MIDI live de CyDAW ; sélectionner/armer la piste instrument et régler son entrée live comme d'habitude. Cela n'installe pas de port MIDI virtuel pour les autres logiciels.

Une macro est une règle avec plusieurs destinations. Par exemple, relier le stick gauche d'une manette au pitch bend et au CC 74, ainsi qu'à un paramètre de piste. Chaque destination possède sa propre plage de sortie ; les transformations précédentes sont communes. Clavier, MIDI, OSC et extensions d'application utilisent les mêmes règles. Ajouter **Courbe dessinée** aux modifications, déplacer ses 17 points ou double-cliquer pour retrouver une droite. La réponse est interpolée entre les points. Une courbe non monotone possède plusieurs inverses possibles ; le retour utilise son premier segment le plus proche.

Sous Windows, l'application de bureau capture clavier et souris avec un service Raw Input dédié quand CyDAW a le focus. Les touches musicales et les destinations MIDI assignées n'attendent pas une frame d'interface. La saisie de texte et les raccourcis Ctrl/Alt/Windows inhibent les contrôles clavier. Perte du focus, retrait d'un appareil, changement d'assignation et fermeture relâchent les notes tenues. Les claviers et souris sont actuellement regroupés dans une source par famille. Les autres plateformes conservent le repli clavier par l'UI ; les plugins exportés n'enregistrent pas cette capture de bureau.

Adresses souris : `button/left`, `button/right`, `button/middle`, `button/x1`, `button/x2`, position normalisée sur l'écran `axis/x`, `axis/y`, et molette relative `wheel`. Utiliser **Relatif** avec le codage complément à deux pour la molette. L'extension d'application XInput fournit chaque manette Xbox compatible connectée : sticks, gâchettes et boutons. Les autres matériels peuvent fournir leurs adresses via l'ABI publique d'extensions contrôleurs `.cyctl` ; les protocoles HID arbitraires ne sont pas décodés automatiquement.

La conversion MIDI s'exécute sur le thread d'entrée/fournisseur. Les commandes qui modifient le logiciel passent par le thread d'application et peuvent attendre s'il est occupé. Le callback audio n'exécute ni configuration de contrôleur ni commande d'interface. Définitions des contrôleurs, courbes et assignations MIDI globales sont sauvegardées dans les réglages de l'application ; les assignations DAW propres à un projet restent dans ce projet.

Les réglages audio ne conservent plus de page MIDI séparée. Leur bouton **Contrôleurs et entrées…** ouvre cette fenêtre ; le clic droit sur l’icône clavier du transport ouvre directement **Clavier musical**. Les anciennes commandes d’automatisation `midi_inputs` et `computer_keyboard` restent compatibles et partagent les mêmes réglages.
