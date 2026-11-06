## Include et point d’entrée {#signatures}
Il s’agit du point d’entrée exporté et des signatures des callbacks stockés dans `CyControlPlugin`. Renvoyer un descripteur stable ; CyDAW compare `abi_version` à `CYDAW_CONTROL_ABI_VERSION` avant de démarrer le service.

```c
#include <cydaw/control/ControlPluginABI.h>
const CyControlPlugin* cydaw_control_plugin(void);
int start(const CyControlHost* host);
void stop(void);
void poll(void);
int send(const char* device, const char* address, double value);
```

## Paramètres et résultats {#parameters}
- `host` fournit le contexte hôte et les callbacks de périphériques, messages et journal. Sa validité se limite à la durée du service actif.
- `start` renvoie une valeur non nulle en cas de réussite. Annoncer chaque périphérique avec `device_added(context, id, name)` et signaler son retrait avec `device_removed(context, id)`.
- `device` et `address` identifient le périphérique et sa commande, par exemple `gamepad-1` et `axis/leftX` ; ce ne sont pas des adresses de tampons audio ou MIDI.
- `value` est normalisée de 0 à 1. `send` renvoie 1 si la valeur a été envoyée ; ce callback peut être nul lorsque le retour matériel est absent.
- `poll` et `stop` ne renvoient rien. Après le retour de `stop`, aucun appel vers l’hôte ne doit rester en cours.

## Envoyer une valeur de commande {#example}
Ce court fragment s’exécute lorsque le service dispose encore d’un pointeur hôte valide. Les chaînes identifient un périphérique déjà annoncé par l’extension.

```c
host->message(host->context, "gamepad-1", "axis/leftX", 0.5);
```

Installer la bibliothèque partagée sous l’extension `.cyctl` dans `ControlPlugins` à côté de CyDAW ou dans `Documents/CyDAW/Control Plugins`. Cette interface ajoute des contrôleurs bruts et reste indépendante des formats de plugins audio. Voir la [configuration matérielle](doc:controllers).
