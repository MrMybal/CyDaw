# Modulateurs

Chaque périphérique a ses modulateurs : de petites sources (un LFO, une enveloppe, des pas, une macro…) qui font bouger ses potards autour de leur valeur. Ils s'ajoutent en plus des câbles du graphe et de la modulation de piste, qui restent.

## Sur une carte

- Le bouton **modulateurs** de l'en-tête d'une carte (une petite courbe, à côté du bouton liste) ouvre une colonne à sa gauche. Il est allumé dès que la carte a des modulateurs.
- **+** ouvre les modulateurs par catégorie, avec une recherche :

| Catégorie | Modulateur | Ce qu'il donne |
|---|---|---|
| LFO | **LFO** | une forme qui tourne : sinus, triangle, dents de scie montante et descendante, carré, hasard, hasard lissé |
| LFO | **Random** | une valeur au hasard à chaque cycle (échantillonné-bloqué), avec une probabilité de changer |
| Sequence | **Steps** | jusqu'à 16 pas dessinés à la souris, un pas par valeur de note |
| Envelope | **ADSR** | une enveloppe que les notes de la piste ouvrent et ferment, sensible à la vélocité, legato possible |
| Note | **Keytrack** | la dernière note : sa hauteur autour d'une fondamentale, sa vélocité, ou si une touche est tenue |
| Note | **Expressions** | la molette de modulation, l'aftertouch, le pitch bend ou n'importe quel contrôleur |
| Interface | **Macro** | un potard qui en bouge plusieurs |
| Interface | **Button** | un interrupteur : tout ou rien |
| Modifier | **Math** | deux valeurs mélangées, ajoutées, multipliées, la plus basse, la plus haute, la différence, la moyenne |
| Audio | **Envelope Follower** | le volume du son de sa carte |

- Chaque modulateur a une ligne : sa couleur, son nom, sa valeur en ce moment, et sa **flèche**. Un clic sur le nom montre ses réglages sous la liste. Un clic droit : retirer ses modulations, ou le retirer.
- Les modulateurs synchronisés (**Sync**) suivent le tempo et, quand le transport joue, la position dans les mesures : 4 mesures, 2 mesures, 1 mesure, 1/2, 1/4, 1/8, 1/16, 1/32, les triolets et les pointées. **Free** : leur propre vitesse, en hertz. **Retrigger** les relance à chaque note.
- Comme tout ce qui est dans un module, la colonne est en anglais.

## Moduler un potard

- **Glisser la flèche** d'un modulateur sur n'importe quel potard de la piste : il le module d'un quart de sa plage.
- **Cliquer la flèche** l'arme : tirer un potard règle alors de combien ce modulateur le bouge, sans toucher sa valeur (vers le bas : dans l'autre sens ; double clic : plus du tout). Un nouveau clic sur la flèche, ou Échap, désarme.
- Autour du potard, un anneau de la couleur du modulateur montre jusqu'où il peut l'emmener ; le point violet, où il le met maintenant. Plusieurs modulateurs peuvent bouger le même potard.
- Au survol, une bulle dit qui le bouge et de combien : `LFO → Pan [0.300]`.
- La **liste des valeurs** de la carte montre aussi les modulations, sous la valeur de chaque réglage.
- Tous les potards se modulent : ceux des effets, et ceux des instruments (Opale et les autres synthés, Prisme, Kick, le sampler, Batterie, les modèles de son). Les entrées **A** et **B** du modulateur **Math** prennent d'autres modulateurs.

## Comment ça marche

- Un modulateur est un nœud de contrôle du graphe de la piste. Il connaît la carte à laquelle il appartient, et il est rangé dans sa colonne plutôt que dans la chaîne. Supprimer la carte supprime ses modulateurs.
- Une modulation est un câble de contrôle sur la prise du potard, en mode « moduler ». Sa quantité est l'anneau : tourner l'anneau change la quantité pendant que le son joue, sans recompiler le graphe (les queues de réverbe continuent).
- Le moteur évalue les modulateurs une fois par bloc, avant le son : modulation « de base », monophonique. Un potard d'instrument est bougé pour toutes les voix ; la modulation note par note reste celle des instruments eux-mêmes.
- Les modulateurs s'enregistrent avec le projet et partent dans les plug-ins exportés (VST3, CLAP, LV2), qui leur passent leurs propres notes.

## Port de contrôle et MCP

`modulator {track, action, …}` :
- `add {nodeId, type}` : `lfo`, `steps`, `random`, `adsr`, `keytrack`, `expressions`, `macro`, `button`, `math`, `follower`.
- `set {id, settings}` : un choix par numéro ou par nom, par exemple `{"sync": "1/8", "shape": "Triangle"}`.
- `modulate {id, binding | nodeId + parameter, amount}` : amount de -1 à 1 de la plage. Si la modulation existe déjà, c'est sa quantité qui change.
- `unmodulate`, `remove {id}`, `arm {id}` (0 : désarmer), `column {nodeId, open}`.
- `list {nodeId?}` : les modulateurs, leur valeur maintenant, et leurs modulations.

L'outil MCP `modulator` prend les mêmes champs. Le test `tests/v2_modulators_check.py` (ctest `cydaw2_modulators_check`) les vérifie de bout en bout, en silence.
