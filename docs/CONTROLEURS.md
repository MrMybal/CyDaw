# Contrôleurs MIDI / OSC

**Options → Contrôleurs MIDI / OSC…**, ou le bouton **CTRL** de la barre d'outils, ouvre la fenêtre. Un clic droit sur **CTRL**, ou **Ctrl+M**, passe en mode **Apprendre**.

Tout passe par trois étages, dans les deux sens :

1. **Appareils RAW** : ce qui parle à CyDAW, tel quel.
   - Les ports MIDI d'entrée et de sortie.
   - Les points OSC, sur cette machine ou sur le réseau.
   - Les appareils qu'apportent les plug-ins d'application, par exemple une manette.
2. **Appareils virtuels**, faits à partir des RAW. Chaque contrôle virtuel a trois choses :
   - une source, c'est-à-dire un appareil RAW et une de ses adresses ;
   - une chaîne de modifications ;
   - un retour, l'endroit où sa valeur repart.
3. **Assignations** : un contrôle virtuel relié à une cible de CyDAW. CyDAW n'utilise que le côté virtuel.

## Appareils RAW

- **Entrées MIDI** : la case ouvre ou ferme le port. C'est le même réglage que l'onglet *Entrées MIDI* des réglages audio.
  - **Notes** allumé : les notes et contrôleurs de l'appareil jouent aussi l'instrument sélectionné, sauf ceux qu'une assignation prend (un CC assigné ne bouge plus aussi le synthé).
  - **Notes** éteint : l'appareil ne sert qu'à piloter.
- **Sorties MIDI** : ouvertes seulement au premier retour à envoyer. Un port déjà pris par un autre logiciel est signalé.
- **OSC** : **+ Point OSC** écoute un port UDP, sur cette machine et sur le réseau local.
  - **Réponse (hôte, port)** : l'endroit où CyDAW renvoie les valeurs, par exemple une tablette.
  - **Adresses /cydaw/… directes** : `/cydaw/<cible> valeur` bouge la cible sans contrôle virtuel, par exemple `/cydaw/transport/play`, `/cydaw/track/selected/volume 0.8` ou `/cydaw/dj/A/high 0.5`. Une cible ainsi touchée est renvoyée à l'adresse quand elle bouge ailleurs.
  - Un message OSC sans nombre vaut un appui puis un relâchement.
- **Plug-ins d'application** : les fichiers `.cyctl` placés dans `ControlPlugins`, à côté de CyDAW, ou dans `Documents/CyDAW/Control Plugins`. Voir plus bas.

Les adresses MIDI s'écrivent ainsi, avec des valeurs de 0 à 1 :
- `cc/<canal 1..16>/<numéro>` ;
- `note/<canal>/<note>` (sa vélocité ; 0 au relâchement) ;
- `pb/<canal>` (le pitch bend, 0,5 au milieu) ;
- `at/<canal>` (aftertouch) et `pat/<canal>/<note>` (aftertouch d'une note) ;
- `pc/<canal>/<programme>`.

Les adresses OSC sont celles reçues. Un deuxième nombre se lit à `<adresse>#1`.

## Appareils virtuels

Un appareil virtuel se crée tout seul quand on apprend. On peut aussi le créer avec **+**, ou avec **Appareil virtuel depuis celui-ci** sur un appareil RAW sélectionné.

Chaque contrôle a un **type** : Potard, Fader, Encodeur, Bouton, Interrupteur, Pad ou Axe. Le type pose la chaîne de modifications qui lui convient :
- un encodeur prend « Relatif » ;
- un bouton prend « Seuil » ;
- un interrupteur prend « Bascule » ;
- un axe prend « Zone morte ».

**Apprendre**, à côté de la source, prend l'adresse du prochain message reçu.

Les **modifications** s'appliquent dans l'ordre, du RAW vers le virtuel, puis à l'envers pour le retour :

| Modification | Effet | Réglages |
| --- | --- | --- |
| Plage d'entrée | seule la partie [a, b] de la course compte | a, b |
| Inverser | le haut devient le bas | |
| Courbe | négative : lente au début ; positive : rapide | a (-1..1) |
| Relatif (encodeur) | chaque cran avance ou recule depuis la valeur actuelle | codage : complément à deux, décalé de 64, ou bit de signe ; taille d'un cran |
| Bascule | un appui allume, le suivant éteint | |
| Seuil (bouton) | 1 au-dessus du seuil, 0 en dessous | a |
| Zone morte | le milieu d'un stick reste immobile | a |
| Pas | la valeur se pose sur n crans | a |
| Plage de sortie | le contrôle ne couvre que [a, b] | a, b |

Le **retour** renvoie la valeur au contrôle (LED, fader motorisé, écran d'une tablette) quand la cible bouge par un autre chemin : la souris, l'automation, un autre contrôleur. Par défaut, il part vers la sortie MIDI du même nom, le même point OSC ou le même appareil du plug-in, à la même adresse.

Un encodeur repart toujours de la valeur actuelle de sa cible. Aucun retour n'est renvoyé au contrôle qui vient de bouger la cible.

## Assignations et apprentissage

**Apprendre** couvre toute l'interface d'un calque. Tout ce qu'un contrôleur peut bouger est encadré, y compris dans les fenêtres détachées :
- le transport (lecture, stop, enregistrement, boucle, tempo) ;
- le mixeur, l'inspecteur et les en-têtes de pistes (volume, panoramique, muet, solo, armer) ;
- le master ;
- les potards et les cases de valeur des cartes et des instruments ;
- le Mix DJ (gain, égaliseur, kills, filtre, faders, pré-écoute, crossfader, master, lecture, CUE, SYNC).

On clique une commande, puis on bouge un contrôle :
- si ce contrôle n'existe pas encore côté virtuel, CyDAW le crée, avec son retour ;
- un clic droit sur une commande retire son assignation ;
- **Échap** abandonne la commande choisie ; un second **Échap**, ou **Terminer**, sort du mode.

En apprenant, le nouveau contrôle remplace l'ancien sur la cible. À la main (colonne de droite, recherche d'une cible), on peut en ajouter un second : un fader et un encodeur sur le même volume, par exemple.

Chaque assignation a :
- une **plage**, la partie de la course de la cible qu'elle couvre ;
- une **reprise** : la cible ne bouge qu'une fois le contrôle arrivé à sa valeur, sans saut.

Les potards suivent la même échelle que la souris (logarithmique pour une fréquence ou un temps).

Les assignations aux pistes et à leurs cartes sont **gardées avec le projet** ; elles suivent les pistes à la réouverture. Celles du transport, du Mix DJ, du master et de la piste sélectionnée valent **pour tous les projets**, avec les appareils et les appareils virtuels.

## Moniteur

Le moniteur affiche ce qui arrive (`>`) et ce qui repart (`<`) : l'appareil, l'adresse et la valeur (0..127 pour le MIDI). **Pause** fige la liste, **Effacer** la vide.

## Plug-ins d'application

Ce ne sont pas des plug-ins audio : ils apportent des appareils RAW que CyDAW ne connaît pas lui-même (manette, boîtier HID, protocole réseau). Un plug-in est une bibliothèque partagée renommée `.cyctl`. Elle exporte une fonction C, `cydaw_control_plugin()`, décrite dans `include/cydaw/control/ControlPluginABI.h`.
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
- `inject {device, address, value}` (comme un message reçu) ;
- `add_virtual`, `add_control`, `set_control`, `remove_control`, `remove_virtual` ;
- `assign`, `unassign`, `learn {on, target}` ;
- `targets {filter}`, `value`, `apply`, `feedback`, `monitor`, `show`.

Test : `tests/v2_controllers_check.py` (ctest `cydaw2_controllers_check`, silencieux). Il passe par de vrais sockets UDP sur 127.0.0.1 et couvre :
- l'aller et le retour OSC, à travers une plage ;
- un encodeur, un bouton, la reprise ;
- les adresses `/cydaw/…` ;
- l'apprentissage ;
- un potard de carte et l'égaliseur d'une platine ;
- le projet enregistré, un nouveau projet, puis le projet rouvert ;
- le chargement du plug-in manette.

## Limites

- Pas encore de MIDI 14 bits (CC MSB/LSB, NRPN), de SysEx ni de profils prêts pour des surfaces du commerce (protocoles de surfaces de mixage, LED des contrôleurs DJ).
- Les notes d'un appareil vont toutes à l'instrument sélectionné : pas encore de routage par appareil ou par canal vers une piste choisie.
- Pas de sortie MIDI des pistes vers un instrument externe.
- Le clavier de l'ordinateur reste un clavier de notes ; il n'est pas un appareil RAW pilotable.
- Les plug-ins des pistes (VST3, CLAP, LV2, JSFX) et les pads d'un rack ne sont pas encore des cibles.
