## Include et callbacks {#signatures}
Le modèle de composant de l’atelier fournit `CyInterfaceContext`. Ses deux callbacks ont les signatures suivantes ; ce sont des pointeurs de fonction du contexte, pas des fonctions globales à implémenter dans l’hôte.

```cpp
#include "CustomInterfaceABI.h"
double (*get)(void* user, const char* key);
void (*set)(void* user, const char* key, double value);
```

## Paramètres et résultats {#parameters}
`user` est le pointeur opaque de l’hôte et doit être transmis sans modification à chaque callback. `key` provient de la liaison `primary`, `secondary`, `tertiary` ou `quaternary` du contexte. Un `get` valide renvoie la valeur actuelle du paramètre ; `set` la modifie sans renvoyer de valeur. `minimum` et `maximum` décrivent la plage du réglage principal.

`imgui` est le contexte ImGui de l’hôte. `abiVersion` et `imguiVersion` décrivent le contrat binaire pris en charge. Les composants doivent correspondre au modèle fourni et au compilateur cible ; des compilations Dear ImGui arbitraires ne sont pas compatibles au niveau binaire.

## Lire et modifier une valeur liée {#example}
Avec un pointeur de contexte valide nommé `context`, ce fragment lit puis écrit une liaison principale existante. Il illustre l’emploi des callbacks, sans constituer un composant chargeable complet.

```cpp
const double value = context->get(context->user, context->primary);
context->set(context->user, context->primary, value);
```

Créer et compiler le composant complet depuis **Interface > Exemple** dans le [concepteur d’interface](doc:interface-design#code). Le traitement audio relève du [contrat processeur](doc:api-audio-processor), pas des callbacks UI.
