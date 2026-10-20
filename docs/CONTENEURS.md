# Conteneurs

Un conteneur est un périphérique qui tient des chaînes de périphériques et les remélange. On l'ajoute avec le **+** de la bande des périphériques, catégorie **Conteneurs**, comme n'importe quel effet. Il y en a douze : neuf d'effets et trois d'instruments.

## Les neuf conteneurs d'effets

| Conteneur | Ses chaînes | Ce que chacune entend | Réglages propres |
|---|---|---|---|
| **Chain** | une | tout le son | — |
| **FX Layer** | 1 à 8 couches | tout le son ; les couches s'additionnent | volume, pan, solo et muet par couche |
| **FX Selector** | 1 à 8 chaînes | tout le son ; une seule chaîne est entendue | la chaîne jouée, la durée du fondu, un volume par chaîne |
| **Mid-Side Split** | Mid, Side | le milieu de la stéréo, ou ses côtés ; puis réunis | gain du Mid, gain du Side |
| **Stereo Split** | Left, Right | la gauche, ou la droite | gain de chaque côté |
| **Multiband FX-2** | Low, High | une bande (Linkwitz-Riley 24 dB/oct) ; les bandes se recomposent | fréquence de coupure, niveau, solo et muet par bande |
| **Multiband FX-3** | Low, Mid, High | une des trois bandes | deux fréquences de coupure, niveau, solo et muet |
| **XY FX** | A, B, C, D, puis **Post FX** | tout le son ; le mélange suit la place du point sur le pavé ; la chaîne Post FX traite ce mélange, avant Mix | X et Y |
| **Replacer** | une | un son joué à chaque transitoire de l'entrée : grosse caisse, note, bruit, clic, ou un **instrument** de CyDAW | seuil, sensibilité, maintien, son, vélocité ; hauteur et déclin des sons intégrés ; note et durée pour un instrument |

Tous ont **Mix** (le son d'entrée mêlé à celui des chaînes) et **Wet Gain** (le gain des chaînes).

## Les trois conteneurs d'instruments

Sur une piste instrument, ils sont **l'instrument** de la piste. Chacune de leurs chaînes (un pad, une couche) est un instrument avec ses propres effets. Ces chaînes sont jouées par la piste, puis passent dans les effets de la piste.

| Conteneur | Ses chaînes | Ce que chacune joue | Réglages |
|---|---|---|---|
| **Drum Machine** | un pad par touche, sur les 128 touches | les notes de sa touche (ou une autre note, pour un instrument accordé) | volume, pan, muet, solo et groupe de coupure par pad |
| **Instrument Layer** | 1 à 16 couches | toutes les notes, dans ses touches et ses vélocités | volume, pan, muet, solo, touches et vélocités par couche |
| **Instrument Selector** | 1 à 16 couches | une seule couche reçoit chaque note ; une note finit là où elle a commencé | le mode de choix, la couche, le CC, la première touche de keyswitch |

Le mode de choix de l'Instrument Selector :
- **Manual** : la couche allumée ; un clic sur un voyant en choisit une autre ;
- **Round Robin** : chaque note sur la couche suivante ;
- **Random** : une autre couche que la dernière, au hasard ;
- **Keyswitch** : les touches à partir de *Keyswitch* (note 24, do 1, par défaut) choisissent la couche et ne jouent rien ;
- **CC** : la valeur d'un contrôleur répartit les couches de 0 à 127 ;
- **Program Change** : le numéro de programme choisit la couche.

Ces réglages sont ceux de la carte : on peut les assigner à un contrôleur et ils sont dans la liste des valeurs.

- **Drum Machine** : la grille montre seize touches. La molette, ou la bande à droite de la grille (les 128 touches, les pads marqués, les seize visibles encadrés), montre les autres. C'est l'ancien rack de pads : les projets qui en ont un l'ouvrent tel quel.
- **Instrument Layer** et **Instrument Selector** : la carte montre les couches en liste, avec pour chacune muet, solo, volume, un indicateur de niveau et l'ouverture de sa chaîne. Les réglages de la couche choisie sont à droite : son instrument, sa chaîne, pan, volume, touches et vélocités (Layer seulement). **+ Layer** en ajoute une. Un double-clic sur un nom le renomme. Le clic droit propose : changer d'instrument, ouvrir la chaîne, renommer, retirer.
- Un pad ou une couche peut jouer un instrument de CyDAW (Kick, Silex, Prisme, les synthés, Batterie, les échantillonneurs) ou un **instrument d'usine** échantillonné, rangé par catégorie dans *Factory Instruments*. Les échantillons d'un instrument d'usine qui ne sont pas encore là sont téléchargés au moment du choix. Les noms sont en anglais, comme dans tout module.
- Un pad ou une couche ne tient pas de conteneur d'instruments. Changer l'instrument d'une piste pour un autre conteneur repart de ses pads ou couches par défaut ; les garder demande de rester sur le même conteneur.
- Les conteneurs d'instruments ne partent pas dans les plugins exportés (leurs chaînes sont des instruments de la piste).

## Noms des branches

Une branche se renomme par un double-clic sur son nom (dans la liste du conteneur, ou en haut du cadre de sa chaîne), ou par son clic droit › *Rename…*. Un nom vide redonne le nom habituel (*Layer 2*, *Mid*, *Low*, *A*…). Le nom est gardé sur la carte du conteneur, suit sa branche quand une branche avant elle est retirée, et se sauvegarde avec le projet. Les couches d'un Instrument Layer ou Selector se renomment de la même façon.

## Presets de conteneurs

Le preset de la carte d'un conteneur garde **tout ce qu'il contient** :
- pour un conteneur d'effets : ses réglages, chaque branche avec ses périphériques et leurs réglages, leurs modulateurs, les noms des branches ;
- pour un conteneur d'instruments : ses pads ou couches, chacun avec son instrument, ses effets, son mixage, ses touches et vélocités.

Charger un preset remplace le contenu du conteneur. La carte garde sa place et ce qui l'alimente. C'est une seule étape d'annulation. Le preset de toute la chaîne d'une piste garde aussi les pads ou couches de son conteneur d'instruments.

Presets d'usine (43, `tools/factory_content/build_container_presets.py`, données dans `studio_presets/containers.py`) :
- Chain : Warm Tape Chain, Lo-Fi Chain, Vocal Polish Chain, Drum Bus Chain ;
- FX Layer : Parallel Saturation, Parallel Crush, Wide Doubler, Shimmer Send ;
- FX Selector : Three Colours, Delay Choices, Filter Moves ;
- Mid-Side Split : Wide Reverb Sides, Solid Mid, Air on the Sides ;
- Stereo Split : Split Tape Echoes, Split Detune, Left Right Motion ;
- Multiband FX-2 : Saturated Highs, Tight Lows, Air Shimmer ;
- Multiband FX-3 : Three Band Glue, Warm Lows Bright Tops, Mid Focus ;
- XY FX : Four Spaces, Texture Pad, Drive Corners ;
- Replacer : Kick Reinforce, Sub Thump, Snare Crack, Click Attack ;
- Drum Machine (huit pads, un preset de Kick et un de Silex) : Techno Kit, House Kit, Trap Kit, Lo-fi Kit, Electro Kit, Minimal Kit ;
- Instrument Layer : Piano and Pad, Bass and Keys Split, Velocity Layers, Organ and Strings ;
- Instrument Selector : Synth Choices, Keyswitch Keys, Round Robin Kicks, Mod Wheel Pads.

Chaque périphérique d'une branche y est réglé par l'un de ses propres presets d'usine. Le générateur fabrique les presets par le port de contrôle, dans une instance silencieuse.

## Dans la bande des périphériques

- La carte du conteneur montre son panneau :
  - ses branches (couches, chaînes, bandes) ;
  - les réglages de chaque branche ;
  - le pavé XY ou les fréquences de coupure à tirer ;
  - **Mix** et **Wet Gain**.
- Les cartes de la branche choisie suivent la carte du conteneur, dans son cadre. Un clic sur le nom d'une branche (ou sur une bande, un coin du pavé, **Post FX** sous le pavé du XY FX) montre sa chaîne.
- Le **+** au bout de la branche ajoute un périphérique. On peut aussi y déposer une carte tirée par son nom, d'où qu'elle vienne. Tirée sur une carte de la branche, elle se place avant ou après elle.
- **+ Layer** ou **+ Chain** ajoute une branche. Un clic droit sur une branche la retire, avec ses périphériques.
- Un conteneur peut en tenir d'autres.
- **Replacer avec un instrument** : choisir *Instrument* dans *Sound*, puis un instrument dans la liste (Kick, Opale et les autres synthés, Prisme, Batterie…). À chaque transitoire, il joue la note réglée (36 par défaut, la grosse caisse d'une Batterie), aussi fort que le coup, aussi longtemps que *Length*. **Open** montre ses cartes dans la bande, comme un pad de rack, pour régler l'instrument ; la flèche de la colonne revient à la piste. Ses effets sont la chaîne du Replacer, dans son cadre. L'instrument est gardé dans la carte du Replacer (son graphe et son plan compilé) et se sauvegarde avec le projet. Il part dans les plugins exportés (VST3, CLAP, LV2) : son plan dans le code généré, ses échantillons (une Batterie, un sampler) dans un fichier de voix à lui, `Voice1.cyvoice`, `Voice2.cyvoice`…, à côté de celui de l'instrument du plugin.
- Supprimer la carte d'un conteneur le supprime avec tout ce qu'il tient. L'éteindre (bypass) laisse passer le son sans lui.
- Ses réglages sont des réglages comme les autres : automatisables, modulables (les modulateurs se glissent dessus), dans la liste des valeurs.

## Comment ça marche

- Dans le graphe de la piste, un conteneur est fait de trois choses :
  - sa **tête**, la carte, qui porte tous ses réglages ;
  - les cartes de ses branches ;
  - sa **fin** (un nœud *Container End*), qui les mélange. Le XY FX en a deux : la première mélange les coins, la chaîne Post FX la suit, la dernière y mêle le son sec par Mix.
- La tête passe le son d'entrée à la fin, qui le garde pour **Mix**. Elle découpe aussi la part de chaque branche (le milieu, une bande…).
- Les périphériques d'une branche sont des nœuds ordinaires. Ils gardent donc tout ce qu'a une carte : automation, modulateurs, liste des valeurs, compensation de latence (les branches sont réalignées), export en plugin.
- Le graphe montre ces nœuds tels qu'ils sont ; la bande des périphériques les range dans leur conteneur.

## Port de contrôle et MCP

`container {track, action, …}` :
- `insert {type, before}`, avec les types `fx-chain`, `fx-layer`, `fx-selector`, `mid-side-split`, `stereo-split`, `multiband-2`, `multiband-3`, `xy-fx` et `replacer`.
- `add_device {head, branch, key, before}` : ajoute un périphérique dans une branche. Pour le XY FX, la branche 4 est le Post FX (`postBranch` dans `list`).
- `move {nodeId, head, branch, before}`. Avec `head` à 0, le périphérique va dans la chaîne principale.
- `add_branch {head}`, `remove_branch {head, branch}`, `remove {head}`.
- `show {head, branch}` : choisit la branche montrée dans la bande.
- `rename {head, branch, name}` : nomme une branche (vide : son nom habituel).
- `instrument {head, key}` donne un instrument à un Replacer : une clé du catalogue, ou `factory:<clé>` pour un instrument d'usine. `open_instrument {head, open}` ouvre ou ferme sa chaîne dans la bande.
- `list` : chaque conteneur, ses branches, leurs cartes et les noms donnés (`names`).

`instrument_choices` : ce que peut jouer un pad, une couche ou un Replacer (`key`, `name` en anglais, `category` pour un instrument d'usine, `missing` quand ses échantillons sont encore à télécharger).

`rack {track, action, …}` pour les conteneurs d'instruments :
- `list` donne `kind` (Drum Machine, Instrument Layer, Instrument Selector), `padsKind`, `selected` (la couche que joue un Selector) et chaque pad ou couche : touche, nom, mixage, `keyLow`, `keyHigh`, `velocityLow`, `velocityHigh`, ses cartes ;
- `add {note, instrument}` ajoute un pad (sur sa touche) ou une couche (à la fin) ;
- `instrument`, `mix`, `sample`, `remove`, `open` et `close` agissent sur un pad ou une couche, désigné par `pad` (sa place) ou par `note`.

`presets` prend `pad` pour la chaîne d'un pad ou d'une couche. Les réglages d'un Selector passent par `set_node_parameter` : `selector.mode`, `selector.layer`, `selector.controller`, `selector.keyswitch`. `value_list {track, plugin, open}` ouvre la liste des valeurs d'une carte de plugin.

Les réglages passent par `set_node_parameter`, par exemple `fxLayer.mute2`, `multiband3.crossoverLow` ou `xyFx.x`. L'outil MCP `container` prend les mêmes champs.

Tests :
- `tests/ContainerTests.inc`, dans les tests du cœur : le son de chaque conteneur ;
- `tests/v2_containers_check.py` (ctest `cydaw2_containers_check`, en silence) : l'édition, le glisser-déposer, l'imbrication, la sauvegarde ;
- `tests/v2_instrument_containers_check.py` (ctest `cydaw2_instrument_containers_check`, en silence, mixages exportés hors ligne) : couches, touches, vélocités, les modes du Selector, les pads sur toutes les touches, les instruments proposés, les noms de branches, les presets (et ceux d'usine), la liste des valeurs d'un plugin, la sauvegarde.

`effect_chain_check` prend `"live": true` (le graphe tel que la piste le joue, plutôt que tel qu'un plugin exporté le restaure) et `"signal": "clicks"` (un clic tous les quarts de seconde).
