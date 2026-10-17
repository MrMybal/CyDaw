# VST3, CLAP, LV2 et JSFX

## Utiliser un plugin

La bibliothèque **Effets et périphériques**, ouverte par le **+** de la chaîne, recherche maintenant VST3, CLAP, LV2 et JSFX. **Dossiers de plug-ins** permet de compléter les emplacements standard. La colonne Genre indique le format. Les dossiers `CLAP_PATH` et `LV2_PATH` sont aussi pris en compte.

Sur Windows : VST3 et CLAP dans `Common Files` et `%LOCALAPPDATA%/Programs/Common`, LV2 dans `%APPDATA%/LV2` et `Common Files/LV2`. Les scripts personnels sont dans `Documents/CyDAW/JSFX`; les scripts de `REAPER/Effects` et les exemples livrés à côté de l'exécutable sont également recherchés. Les scripts JSFX sans extension sont reconnus par leur en-tête `desc:`. Les bundles LV2 restent des dossiers `.lv2` complets.

Le scan des nouveaux formats s'effectue en arrière-plan, dans un processus isolé avec délai maximal et cache. Les différentes entrées d'un même bundle CLAP/LV2 apparaissent séparément. Les fichiers `.cyplugin` de `%APPDATA%/CyDAW/PluginDescriptors` (hors cache) conservent le chemin et l'identifiant de chaque entrée.

Le chargement réutilise la chaîne existante de plugins externes des **pistes audio** : paramètres, automation, programmes disponibles, fenêtre et état sauvegardé dans le projet. Comme pour l'hôte VST3 préexistant, les plugins externes ne constituent pas encore les instruments des pistes MIDI, ni les effets des bus matériels et du master. Les instruments **exportés** en CLAP/LV2 reçoivent en revanche leurs notes depuis un hôte compatible.

Chaque instance vit dans `CyDAWPluginWorker`, y compris les JSFX : une erreur de compilation est rapportée à l'application; un arrêt du processus est pris en charge par le mécanisme d'isolation existant. Le tempo, la position et l'état de lecture traversent le protocole partagé version 6. CLAP gère audio, notes/MIDI, paramètres, état, GUI Win32 et timers; les extensions facultatives de pool de threads et de modulation par note ne sont pas annoncées par l'hôte.

## Éditeur JSFX

**Affichage → Éditeur de scripts JSFX**, le bouton de la bibliothèque ou le bouton **JSFX** de sa carte ouvre le même éditeur. Déposer un `.jsfx` dans la fenêtre principale l'ouvre également.

- **Nouveau** crée un script de gain modifiable.
- **Ouvrir**, **Enregistrer**, **Enregistrer sous** travaillent sur le fichier source.
- **Compiler** vérifie le texte courant dans un processus séparé, sans modifier le fichier actif. Les erreurs contiennent section et numéro de ligne.
- **Compiler et appliquer** écrit uniquement une version compilée avec succès et recharge les instances correspondantes sur la piste audio sélectionnée. Leur ordre et leur état sont conservés.
- **Ajouter à la piste** compile, sauvegarde et insère le script, ou recharge sa carte existante.

Sauvegarder un nouveau script avant de l'insérer. Les changements non sauvegardés sont signalés avant Nouveau/Ouvrir et à la fermeture de l’application. Les imports sont résolus depuis le dossier d'origine du script, avec les racines Effects/Data de ysfx. Le moteur prend en charge les sections EEL2, les sliders, l'état `@serialize`, les fichiers audio et le dessin `@gfx`; la fenêtre du plugin affiche sliders et dessin personnalisé. L'éditeur de texte utilise une police monospace et une zone de diagnostics; ce n'est pas un débogueur EEL pas à pas.

Trois exemples MIT sont livrés : **CyDAW Gain**, **CyDAW Stereo Delay**, **CyDAW Tremolo**.

## Exporter un appareil

Dans **Atelier Plugin → Export**, cocher un ou plusieurs formats **VST3**, **CLAP**, **LV2**, puis les plateformes **Windows x64** et/ou **Linux x64**. **Compiler les cibles choisies** les construit successivement, avec un résultat par cible et un journal commun balisé par cible. **Créer seulement les projets** génère la même sélection sans compiler. Le périphérique conserve la même définition DSP, ses paramètres, ses préréglages et son interface Dear ImGui dessinée dans l'Atelier. Le code C++ des composants personnalisés est inclus comme pour VST3. Aucun éditeur de graphe ni JUCE n'est lié dans les binaires exportés.

Les sorties sont rangées dans `Plugin Exports/<appareil>/<plateforme>/<format>/` :

- VST3 : bundle `.vst3`;
- CLAP : fichier `.clap` et dossier de licences associé;
- LV2 : dossier `.lv2` contenant DLL Windows ou bibliothèque `.so` Linux, `manifest.ttl`, `plugin.ttl`, presets avec état complet et licences.

Chaque format possède son cache de compilation. Un projet généré accepte `-DCYDAW_EXPORT_FORMAT=VST3|CLAP|LV2` et expose la cible `CyDAWGeneratedGraphPlugin_<format>`. Les noms de fonctions historiques `generateVst3Project` / `Vst3Build` restent compatibles avec leurs appels existants; ils prennent maintenant le format en argument.

La compilation locale depuis Windows cible **Windows x64** (Visual Studio 2022, Win32/D3D11) et **Linux x64** (Zig 0.15.2, glibc 2.36 minimum, X11/OpenGL 3). Les deux réutilisent les mêmes contrôles ImGui et composants C++ ; Linux utilise Pugl et la boucle d'événements fournie par l'hôte (CLAP timer-support, VST3 IRunLoop, LV2 idleInterface). Les hôtes Linux doivent fournir X11 et OpenGL ; cette version n'ajoute pas de backend Wayland natif. Un composant personnalisé doit lui-même être portable pour compiler sur Linux.

La première compilation Linux prépare automatiquement la chaîne dans le cache `plugin-toolchains/linux`, sans runner ni WSL. Il faut Python 3 et Ninja (fourni avec les outils CMake de Visual Studio). La préparation utilise `tools/setup_plugin_toolchains.py`, vérifie le SHA256 de Zig et les paquets du sysroot Debian. On peut aussi lancer ce script avec `--output <cache>/plugin-toolchains/linux`. `CYDAW_PLUGIN_TOOLCHAINS` permet de choisir une racine déjà préparée. Les métadonnées LV2 sont générées côté Windows à partir des valeurs exportées, sans exécuter le binaire Linux. Les binaires doivent ensuite être validés dans leurs hôtes cibles ; une compilation croisée réussie ne valide pas le fonctionnement audio ou graphique sous Linux.

**macOS est reporté** ; aucun SDK Apple n'est installé ou téléchargé. Les limites DSP existantes restent communes : une sortie audio, au plus quatre entrées, jusqu'à huit canaux, mêmes restrictions sur les nœuds exportables et les racks de pads. Changer de SDK ne change pas la licence du moteur CyDAW embarqué.

## Vérification

Les tests `tests/NativeFormatTest.cpp`, activés par `CYDAW_NATIVE_VALIDATOR=ON` dans un export CLAP/LV2, utilisent un parent Win32 caché : audio réel, état, paramètres et deux cycles d'attachement/destruction des fenêtres. `tests/NativeViewTest.cpp` et le validateur officiel vérifient VST3. Le worker offre aussi `--probe-plugin` et `--validate-plugin` pour les quatre formats. `tests/v2_plugin_formats_check.py` vérifie l'éditeur JSFX, les erreurs, le transport, la chaîne multiformat et la génération des projets depuis l'application.


## Transférer un graphe entre installations CyDAW

La barre de l'Atelier comporte **Exporter CyDAW…** et **Ouvrir CyDAW…**. Le panneau **Export** propose aussi **Exporter le graphe .cydawdevice…**, indépendamment des restrictions de compilation des plugins. Ce format JSON conserve le graphe éditable, les ports typés, catégories, paramètres exposés, dispositions, interfaces grande fenêtre/carte module, sources C++ et banques de presets du graphe et des modules imbriqués.

L'export incorpore les définitions des modules utilisés, y compris leurs dépendances transitives et classes parentes. Les classes non utilisées sont omises. À la réouverture, ces copies locales passent devant la bibliothèque de l'installation destinataire ; un enregistrement ultérieur dans **Mes périphériques** les conserve. L'export est un instantané : il ne change ni le fichier de travail ni l'état non enregistré de l'onglet. Utiliser une version de CyDAW compatible avec le schéma du graphe ; un schéma plus récent est refusé explicitement.

Les fichiers audio externes et banques référencées ne sont pas inclus : leur liste `externalFiles` figure dans le document et est affichée après l'export. Les tables/données déjà incorporées dans le graphe restent incorporées. Les composants C++ sont transférés en source ; leur compilation sur l'autre poste nécessite sa chaîne de développement.

API de contrôle : `workshop {action: "device_export", file: ".../Device.cydawdevice"}` ; ouverture avec `workshop {action: "open", file: "..."}`. La compilation par lot utilise `plugin_export_batch`, `formats`, `platforms`, `folder`, `build` et éventuellement `sourceRoot`. `vst3_status` expose les résultats dans `targets` ; les anciennes commandes à un format restent compatibles.

Tests : `tests/v2_device_transfer_check.py` transfère un périphérique entre deux instances isolées (interfaces, sources, presets imbriqués, audio et bibliothèque vierge). `tests/v2_plugin_cross_export_check.py` construit les six cibles depuis le logiciel, contrôle les entêtes PE/ELF x86_64, les métadonnées et les presets LV2. Les tests natifs Windows vérifient séparément audio, état et fenêtres ; l'exécution Linux doit être vérifiée sur un poste Linux.
