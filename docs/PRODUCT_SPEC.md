# Spécification produit CyDAW

## Principes

1. **Temps réel d'abord** : aucune allocation, attente de verrou, E/S disque ou
   appel UI sur le thread audio.
2. **Tout est routable** : audio, MIDI, modulation, automation et sidechains
   utilisent un graphe commun typé.
3. **Un crash de plugin n'est pas un crash du projet** : l'isolation hors
   processus est le comportement par défaut.
4. **Une commande, une recherche** : actions, menus, préférences, devices,
   presets et contenus sont indexés dans la recherche globale.
5. **Un même document, plusieurs vues** : arrangement, clip launcher, sample,
   mixage, graphe et performance ne dupliquent pas les données.

## Espaces de travail

### Composition

- arrangement multipiste, launcher de clips et scènes ;
- piano roll, drum editor, MPE, MIDI 2.0 et expressions par note ;
- automation courbe et sample-accurate ;
- comping, takes, groupes, retours, sends et sidechains ;
- tempo/signature map, groove, markers et vidéo ultérieurement.

### Audio et sample

- édition destructive dans une copie de travail et historique illimité ;
- édition non destructive par régions ;
- spectral view, restauration, batch processing et analyse ;
- warp/time-stretch, pitch, transient detection, slicing et crossfades ;
- comparaison A/B et rendu partiel, dans l'esprit d'un éditeur dédié.

### Modularité

- nœuds audio, MIDI, CV/contrôle, événements et données ;
- sous-graphes réutilisables, voix polyphoniques et feedback explicite ;
- instruments et effets créés dans le même environnement ;
- macros, modulation sample-accurate, oversampling par sous-graphe ;
- compilateur de graphe immutable et plan d'exécution parallèle.

L'expérience d'édition reprend la lisibilité d'un graphe Blueprint sans le
copier visuellement : grille infinie, zoom fluide, sélection encadrée, reroute
points, commentaires, alignement, recherche par glisser-déposer, sous-graphes et
navigation par fil d'Ariane. Les connexions sont identifiées à la fois par leur
couleur, leur forme et leur libellé afin de rester accessibles.

La visualisation sonore fait partie du graphe, pas d'une fenêtre séparée :

- mini-waveform sur les générateurs et sorties ;
- spectre, vectorscope, oscilloscope et phase scope intégrables comme nœuds ;
- valeur courante et historique court sur les câbles de modulation ;
- niveau RMS/peak et activité MIDI sur les ports ;
- flux animé optionnel sur les câbles, désactivable et compatible reduced motion ;
- gel de l'analyse, curseurs et comparaison entrée/sortie ;
- analyse GPU/UI alimentée par une file de télémétrie à perte autorisée, jamais
  par un accès direct au thread audio.

### Constructeur d'interfaces

- contrôles vectoriels, grilles, contraintes, anchors et responsive layouts ;
- data binding typé vers paramètres, meters et commandes ;
- composants réutilisables et états visuels ;
- accessibilité clavier/lecteur d'écran et mise à l'échelle HiDPI ;
- script UI isolé du DSP, sans exécution dans le callback audio.

### Layout général

- panneaux dockables, groupes d'onglets et splits imbriqués ;
- détachement dans plusieurs fenêtres/écrans ;
- workspaces sauvegardés par activité ;
- historique annuler/rétablir des changements de layout ;
- palette de commandes/recherche disponible dans chaque menu.

Le docking suit un arbre persistant comparable aux workspaces d'un grand IDE :
drop zones centrales/latérales, splits, tab stacks, fenêtres flottantes, retour
au dock d'origine et support multi-écran/DPI. Les layouts sont nommés, exportés,
versionnés et peuvent être liés à un projet ou rester globaux. Les éditeurs de
plugin, de module, de sample et d'interface utilisent exactement le même système.

### Browser

- index asynchrone dans SQLite, scan incrémental et déduplication par hash ;
- samples, projets, presets, plugins, devices et chaînes ;
- tags hiérarchiques, favoris, collections et recherche floue ;
- analyse hors ligne : BPM, tonalité, loudness, durée, canaux et empreinte ;
- preview synchronisée au tempo, audition dans le contexte et remplacement ;
- bibliothèques locales, NAS et volumes amovibles sans bloquer l'audio.

## Matériel, pilotes et interopérabilité

| Système | Backends prioritaires | Inter-app |
|---|---|---|
| Windows | ASIO, WASAPI exclusif/partagé | JACK optionnel |
| macOS | CoreAudio | JACK optionnel, IAC MIDI |
| Linux | PipeWire, JACK, ALSA direct | PipeWire/JACK |

- sélection du device, fréquence, buffer, clock source et canaux ;
- hot-plug sûr, reprise après perte de device et configuration par projet ;
- MIDI 1.0/2.0, MPE, clock, MMC, controllers et scripts de mapping ;
- Ableton Link envisagé pour tempo/phase, en plus du routage JACK ;
- interfaces DSP : audio via le pilote ; accélération via plugins du fabricant ;
  contrôle propriétaire via adaptateurs SDK quand le fabricant en fournit un.

## Plugins

Formats : VST3 en priorité, CLAP et Audio Unit (macOS), puis LV2 (Linux). VST2
reste optionnel et ne peut être construit qu'avec un SDK/droit historique valide.

Chaque instance s'exécute par défaut dans un worker séparé. Le moteur échange les
blocs par mémoire partagée préallouée et les commandes par IPC. Le superviseur :

- détecte timeout, crash et boucle de crash ;
- remplace temporairement l'instance par un bypass compensé ;
- conserve le dernier état sérialisé connu ;
- relance le worker et restaure l'état ;
- permet un groupement explicite pour les plugins qui partagent des ressources.

Sous Linux, un adaptateur Wine/yabridge permet les VST Windows 32/64 bits. Les
préfixes Wine sont versionnés et séparés par fournisseur ou compatibilité. Proton
n'est pas la dépendance principale : il peut rester une expérimentation car il
est optimisé et distribué comme couche Steam Play pour les jeux.

## Modules, contenu factory et presets

Un `.cymodule` contient un graphe, ses dépendances, ports publiés, paramètres
publiés, interface, presets, métadonnées et ressources. Un module peut instancier
un autre module : une reverb construite une fois devient un nœud compact dans un
instrument. L'éditeur permet d'ouvrir ce nœud, le remplacer, le dériver ou le
mettre à jour sans copier silencieusement son contenu.

L'interface publiée est conçue dans une vue dédiée : knob, fader, switch, XY,
enveloppe, clavier, waveform, spectrum, meter, texte et groupes responsives.
Chaque contrôle se lie à un paramètre publié, une macro ou une commande. La vue
peut être compacte dans la device chain, complète dans le panneau principal, ou
détachée comme une fenêtre de plugin.

Objectif de contenu pour une version 1 solide (à produire et valider par étapes) :

- 250+ nœuds natifs documentés ;
- 15+ instruments factory (sampler, drum, wavetable, FM, granular, physical…) ;
- 50+ effets/modules factory ;
- 1 000+ presets musicaux et chaînes ;
- packs de samples séparés de l'application pour garder l'OS minimal ;
- tests audio de référence, version de schéma et migration pour chaque module.

Le contenu factory sert aussi d'exemple éditable : l'utilisateur peut inspecter
comment chaque instrument est construit, enregistrer une variante et publier sa
propre bibliothèque sans modifier l'original signé.

## Feuille de route

1. **Foundation** : moteur audio, MIDI, transport, graphe, persistance, tests RT.
2. **Vertical slice** : enregistrer, éditer, jouer et exporter une piste audio.
3. **Plugins** : scanner séparé, VST3, worker natif, compensation de latence.
4. **Composition** : clips, launcher, piano roll, automation, mixeur.
5. **Modulaire** : devices natifs, graphe, modulation et UI builder.
6. **Audio avancé** : éditeur sample/spectral, batch, warp et restauration.
7. **Interop** : JACK/PipeWire, Link, bridge Wine, surfaces de contrôle.
8. **CyDAW OS** : image reproductible, tuning et validation matérielle.
9. **Release** : crash reporting opt-in, accessibilité, packaging et signature.
