# Conteneurs

Un conteneur est un périphérique qui tient des chaînes de périphériques et les remélange, comme dans Bitwig. On l'ajoute avec le **+** de la bande des périphériques, catégorie **Conteneurs**, comme n'importe quel effet.

## Les neuf conteneurs

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
| **Replacer** | une | un son joué à chaque transitoire de l'entrée : grosse caisse, note, bruit ou clic | seuil, sensibilité, maintien, son, hauteur, déclin, vélocité |

Tous ont **Mix** (le son d'entrée mêlé à celui des chaînes) et **Wet Gain** (le gain des chaînes).

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
- `list` : chaque conteneur, ses branches et leurs cartes.

Les réglages passent par `set_node_parameter`, par exemple `fxLayer.mute2`, `multiband3.crossoverLow` ou `xyFx.x`. L'outil MCP `container` prend les mêmes champs.

Tests :
- `tests/ContainerTests.inc`, dans les tests du cœur : le son de chaque conteneur ;
- `tests/v2_containers_check.py` (ctest `cydaw2_containers_check`, en silence) : l'édition, le glisser-déposer, l'imbrication, la sauvegarde.
