# Rapport de migration JUCE → CyDSP

Date : 12 novembre 2026. Branche : `CyDSP-CORE`, créée depuis `cf4228d`.

**Le retrait complet de JUCE est bloqué.** La bibliothèque fournit maintenant de
nombreuses briques audio utilisables, effectivement intégrées ici. Elle ne fournit
pas encore toutes les fonctions utilisées par l'application. Son propre inventaire
`docs/Coverage.md` indique que GUI, standards de plugins et API générales JSON/XML
sont différés. Ces absences ne sont pas présentées comme des bugs d'algorithme.

Aucun fichier de CyDSP n'a été modifié. Les adaptations et diagnostics sont dans
CyDaw afin de permettre une mise à jour indépendante de la bibliothèque.

## État du code et provenance

- CyDaw de départ : `cf4228d5ed18897b786bb10f4844673593c21c5a`.
- CyDSP HEAD : `ad813d133a2f54e143db8a3392f2e1768cfdb5ba`.
- Les modules DAW examinés sont dans le **working tree non committé**, pas tous dans
  ce commit. Le détail et le hash des sources sont dans
  [CyDSPSnapshot.json](reports/CyDSPSnapshot.json).
- L'inventaire lexical initial après migration compte 235 fichiers avec des
  références JUCE et 18 083 occurrences dans `src`/`include`. Il inclut des anciens
  chemins et des commentaires : ce n'est ni un pourcentage de migration, ni un
  décompte de bugs. Le script fourni permet de régénérer les emplacements exacts.
- Les builds et paquets conservent les notices nécessaires. Le retrait d'une
  dépendance ne change pas automatiquement la licence du code CyDaw conservé.

## Changements réellement réalisés

1. Dépendance CMake explicite à CyDSP, sélectionnable par chemin et sans fallback DSP.
2. Stockage aligné CyDSP pour les buffers du moteur, avec contrats CyDaw conservés.
3. FFT CyDSP pour la convolution, le stretching existant et l'analyse chromatique.
4. Suppression du module `juce_dsp` dans les includes, le PCH et les liens.
5. Décodage d'échantillons par CyDSP dans les lecteurs par défaut de tous les modes.
6. Encodage WAV sans métadonnées par CyDSP dans les enregistrements/rendus/exports.
7. Conservation **explicite** du parseur de métadonnées et du writer WAV annoté
   existants pour éviter une perte de marqueurs/boucles pendant cette migration.
8. Presets, tests d'intégration, probe amont et inventaire reproductible.

## Défaut reproduit dans CyDSP

### CYDSP-001 — P1 — Perte des libellés WAV LIST/adtl après réécriture

**Constat expérimental.** Un WAV indépendant contient un cue ID 7 à l'échantillon 93
et un libellé « Door impact ». CyDSP lit le cue, mais restitue le LIST sous la forme
`{"type":"LIST","data":"bGFibBAAAAAHAAAARG9vciBpbXBhY3QA"}`. Le type de liste
`adtl` n'est plus représenté. La réécriture avec `AudioFileWriter` et ces métadonnées
ne permet plus au lecteur indépendant de retrouver le libellé.

Le probe retourne `wav_cue_label_roundtrip=false` et le code de sortie 2.
Source à corriger : `third_party/choc/choc/audio/choc_AudioFileFormat_WAV.h`,
`readMetadata_LIST`, `getMetadataSize_LIST`, `writeMetadata_LIST`, puis l'API
`AudioFileInfo::metadataJson` de CyDSP. Le calcul de taille, l'écriture du sous-type
et le padding doivent être cohérents, pour LIST/adtl comme LIST/INFO.

**Impact :** perte des noms de marqueurs/régions de Fonderie/Cinéma lors d'une
migration naïve des exports ; impossibilité de retirer le parseur historique.

**Correction attendue :** conserver le sous-type et tous les chunks labl/note/ltxt,
soit dans un schéma public typé/versionné, soit dans une représentation brute qui
préserve exactement les octets. Vérifier les bornes et l'alignement RIFF.

**Acceptation :** écrire avec un producteur indépendant, lire/écrire avec CyDSP,
relire indépendamment ; vérifier IDs, positions, noms UTF-8, longueurs de régions,
plusieurs LIST et chunk impair. Tester aussi le sens CyDSP → lecteur externe.
Ne pas valider uniquement un aller-retour entre deux implémentations partageant
le même défaut. Le probe reproductible est dans
[CyDSPIntegrationTests.cpp](../tests/CyDSPIntegrationTests.cpp).

## Contrats audio à compléter ou préciser

### CYDSP-002 — P1 — Capacités détaillées des périphériques absentes

`AudioDeviceInfo` expose ID, nom, sens et défaut, mais pas le nombre/noms des canaux,
les fréquences possibles, tailles de buffers, masques actifs ni configurations duplex.
`AudioDevice` expose le sample rate, pas les latences entrée/sortie ou la taille
réellement négociée. Les écrans et le routage utilisent ces informations dans
[AppUi.cpp](../src/v2/AppUi.cpp) et
[AudioTrackController.cpp](../src/app/AudioTrackController.cpp).

Ajouter des structures originales CyDSP pour capacités et configuration négociée,
avec distinction disponible/inconnue et latence mesurée/déclarée. Ne pas retourner
une liste de fréquences/canaux inventée. Acceptation : device simulé qui négocie
une autre configuration, puis tests sur la carte réelle sans écraser ses réglages.

### CYDSP-003 — P1/P2 — Sélection explicite des backends et mode exclusif

`AudioDeviceContext::prepare(bool)` laisse le backend natif en sélection automatique.
`AudioDeviceConfig` n'exprime pas le choix WASAPI partagé/exclusif ; ASIO n'est pas
fourni. CyDaw possède actuellement des choix de pilotes et configurations de canaux.

Prévoir une politique explicite de backend et d'accès partagé/exclusif, retourner
`unsupported_backend` plutôt que changer silencieusement de mode. ASIO concerne
les configurations qui le demandent, pas le build WASAPI de validation.

### CYDSP-004 — P1 — Identifiants audio non persistants entre contextes

`AudioDeviceContext::enumerate` construit des clés locales à partir de l'ordre de
ses enregistrements ; le header précise que les IDs appartiennent à un contexte.
Les paramètres de CyDaw sont restaurés après redémarrage. Réutiliser ces clés sans
résolution peut sélectionner un autre périphérique.

Ajouter un identifiant sérialisable par backend, ou un descripteur de sélection et
un résolveur documenté. Exposer les ambiguïtés ; ne pas choisir arbitrairement entre
deux appareils de même nom. Tester réordonnancement, retrait et redémarrage.

### CYDSP-005 — P2 — Temps et diagnostics du callback audio

Le callback reçoit un numéro de frame, mais pas l'ancre monotone associée, le temps
ADC/DAC, les xruns ou le coût du callback. Le collecteur MIDI demande une ancre dans
le même domaine steady_clock. On peut fournir une approximation dans l'application,
mais elle ne doit pas être présentée comme une mesure du driver.

Proposer un contexte audio avec champs valides optionnels et compteurs explicites.
Documenter qui possède l'horloge et la génération après reconnexion. Les délais
matériels ne doivent pas être confondus avec les délais des processeurs DSP.

### CYDSP-006 — P1 — Métadonnées publiques et conversion de vocabulaire

Même après CYDSP-001, `metadataJson` expose un schéma CHOC privé alors que l'application
consomme des marqueurs, régions, loops sampler et tags avec des clés établies.
L'absence de conversion n'est pas un défaut du décodeur audio, mais empêche un
remplacement transparent. Les usages se trouvent dans `FoundryDocument.cpp`,
`SamplerMapFile.cpp` et `DjEngine.cpp`.

Stabiliser/versionner le contrat public ou fournir des accesseurs originaux pour
ces données, en préservant les chunks inconnus. Tester cue/régions et smpl loops,
tags titre/artiste et absence de métadonnées. Conserver les conventions de fin de
boucle inclusive/exclusive de chaque conteneur.

### CYDSP-007 — P2 — Lecture aléatoire des codecs système

Limite déjà documentée dans DawIntegration : le fallback Windows natif rejoue le
décodage depuis le début lors d'un seek non séquentiel. Cela rend le coût O(position)
pour les fichiers longs. Le MP3 portable reste distinct de ce fallback.

Prévoir cache PCM/proxy d'édition ou seeking indexé avec exactitude annoncée.
Mesurer un seek en fin de long fichier AAC/WMA et la mémoire utilisée. Ne pas
interpréter une compilation du codec comme une validation de la fluidité d'édition.

### CYDSP-008 — P2 — Parité des politiques de stretching/source

`ReadAheadOptions` borne le ratio à 0,25..4 et n'exprime pas les modes musicaux
repitch/tonal/beats/texture/complex de CyDaw ni toutes ses courbes de pitch. Le module
TimeStretch générique est utile, mais ses résultats ne sont pas ceux de chaque mode.

Cette branche conserve les algorithmes CyDaw et remplace leurs FFT. Avant de changer
les algorithmes ou sources, définir une politique d'adaptation explicite, les plages
acceptées, le seek/prime, les tails et les références sonores. Ne pas supprimer les
modes ou borner silencieusement leurs paramètres.

### CYDSP-009 — P2 — MIDI : identité, capacité et publication des ports

Les briques MIDI1/UMP et le collecteur sont présents. Restent à adapter les sélections
persistantes, plusieurs entrées simultanées, SysEx au-delà de 4096 octets du backend
MIDI1, threads producteurs, horloges, reconnection et relâchement des notes tenues.
DawIntegration documente déjà les limites d'identité MIDI1 et de ports virtuels WinMM.

Ce sont des contrats/politiques à préciser, pas l'affirmation que MIDI2 ne fonctionne
pas. Tester plusieurs ports et doublons, débordements signalés, note-on avant
reconnexion/note-off après reconnexion et messages système fragmentés.

## Travail d'application hors du périmètre livré par CyDSP

### APP-001 — P1 — Fenêtres et services plateforme

`ImGuiHost` hérite encore de `juce::Component`, `main.cpp` utilise la coque JUCE.
ImGui/CyUI dessinent l'interface mais ne remplacent pas tous ses événements, fenêtres,
DPI, timers, menus, presse-papiers, images, dialogues et cycle de vie.
Décider si ces services vivent dans CyUI ou une couche plateforme dédiée.
Ce n'est pas une fonction manquante de filtrage/convolution à ajouter à CyDSP.

### APP-002 — P1 — Hôte de plugins et IPC

`HostedInstance` hérite de `juce::AudioPluginInstance` ; CLAP/LV2/JSFX utilisent ce
contrat et des fenêtres JUCE. Prévoir une interface d'instance indépendante : ports,
paramètres/gestures, événements horodatés, état, latence/tails, callbacks UI et IPC.
Le format CLAP n'isole pas automatiquement les crashes : conserver le worker.
Les exports natifs déjà sans JUCE sont un chemin séparé, utilisable avec CyDSP.

### APP-003 — P1 — Sérialisation et utilitaires

String, var/DynamicObject, JSON, XML, File, Result, collections, sockets/processus,
archives et tâches sont encore utilisés largement. CyDSP ne fournit pas d'API
publique générale JSON/XML. Migrer le modèle de données et les services avec tests
de compatibilité des anciens projets/presets. Renommer `juce::` en `cydsp::` ne
retire pas la dépendance. La documentation, le réseau et l'IPC ne doivent pas être
supprimés pour obtenir artificiellement un build sans JUCE.

### APP-004 — P2 — Graphe et contrat des sources

Le graphe CyDSP n'est pas un remplacement direct du graphe typé CyDaw : multibus,
MIDI/contrôle, sidechain et compensation de latence doivent rester définis dans le
moteur de l'application. Les AudioSource/Transport de JUCE et leurs workers sont
encore présents dans le desktop. Les codecs des readers sont migrés, leurs types
et leur ordonnanceur ne le sont pas. Les interfaces CyDSP disponibles permettent
une migration ultérieure de ces adaptateurs avec validation des seeks, loops,
underruns et arrêt/drain de l'enregistrement.

### BUILD-001 — P1 — Dépendance non publiable à partir de HEAD seul

Les ajouts CyDSP nécessaires ne sont pas tous committés. Les copier dans CyDaw ou
pointer un fetch sur `ad813d1` ne fournirait pas les sources examinées. Il faut
committer les changements CyDSP dans son propre dépôt, puis fixer une révision
accessible et garder les notices. Cette branche utilise explicitement le checkout
local et ne prétend pas être reproductible depuis les deux HEAD distants seuls.

### VALIDATION-001 — P2 — Couverture physique et autres plateformes

Les tests documentés de CyDSP ne prouvent pas une session duplex avec la carte audio
réelle, la latence physique, le hotplug et l'interopérabilité MIDI2 réelle. Cette
migration vérifie Windows x64 et des tests silencieux ; elle ne valide pas macOS,
Linux desktop, Windows ARM64 ou SVE. Les sources Apple codec sont elles-mêmes en
attente de compilation/runtime selon le guide. Conserver cette distinction.

## Vérifications et reproduction

Les commandes sont dans [CyDSPIntegration.md](CyDSPIntegration.md).
Les logs et inventaires détaillés sont sous `output/cydsp-*` et restent locaux.

- Moteur indépendant de JUCE : compilation Release et 3 suites CTest passées.
- Adaptateur application : test de lecture/seek/EOS, ownership, buffers, MIDI et
  exports WAV 8/16/24/32 relus indépendamment passé.
- Application CyDAW2 + worker + Graph Runtime VST3 : compilation Release passée.
- Scénarios sample search, mixage, DJ, Fonderie, traitements et import : passés.
- Le premier scénario editing a échoué sur un plugin de test non construit et une
  assertion limitée au français ; plugin construit, assertion corrigée pour les
  deux traductions. La relance du scénario editing est passée (23,46 s).
- Probe WAV CyDSP pur : **échec reproduit attendu pour CYDSP-001, code 2** ; il reste
  séparé des tests d'adaptation. L'échec n'est pas masqué comme un succès.
- Export CLAP natif avec le cœur CyDSP : compilation Release et test CTest `cydaw_native_clap` passés (0,81 s).

## Ordre proposé pour la suite

1. Publier un commit complet de CyDSP et corriger le roundtrip LIST/adtl.
2. Stabiliser métadonnées et capacités/identités/horloges des périphériques.
3. Migrer les adaptateurs sources, enregistrement et MIDI sans modifier leur sémantique.
4. Séparer les services plateforme et l'interface d'hébergement de plugins de JUCE.
5. Retirer les derniers modèles/utilitaires JUCE, puis compiler l'application entière
   avec une vérification interdisant headers, cibles et symboles JUCE.
6. Accepter les chemins audio physiques et les autres plateformes sur leurs hôtes.

Il n'y a pas de nouvelle release ni de push automatique de cette branche.
