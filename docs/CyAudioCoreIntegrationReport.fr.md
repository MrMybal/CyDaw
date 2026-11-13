# Migration JUCE vers CyAudioCore : état et problèmes restants

Date : 13 novembre 2026. Branche `CyDSP-CORE`, reprise depuis `bac0c09`.
Architecture : **CyDaw → CyAudioCore → CyDSP numérique**.
CyAudioCore `bb26e93`, CyDSP `cc18dca` ; empreintes dans le
[snapshot](reports/CyAudioCoreSnapshot.json).

## Résultat de cette reprise

Le branchement sur l'ancien CyDSP monolithique a été retiré. CyAudioCore gère
maintenant sa dépendance CyDSP et fournit les services utilisés par les lecteurs
et encodeurs de CyDaw. Le moteur commun et les exports de plugins internes
accèdent aux processeurs numériques par cette dépendance transitive. Aucun
fichier des dépôts CyAudioCore ou CyDSP n'a été modifié.

**JUCE n'est pas encore retiré du desktop complet.** La migration de dépendance,
les codecs et les traitements numériques sont intégrés. Les gestionnaires de
périphériques/MIDI, les sources du desktop, ses objets de données et les interfaces
historiques de plugins restent à porter. Le test autonome de l'hôte CyAudioCore
ne signifie pas que les sessions du desktop emploient déjà son worker.

Les [commandes reproductibles](CyAudioCoreIntegration.md) et
[l'acceptation](reports/CyAudioCoreAcceptance.json) accompagnent ce rapport.

## Modifications et vérifications

| Élément | Intégration et preuve |
| --- | --- |
| Dépendances | Un seul point d'entrée CMake CyAudioCore, qui ajoute CyDSP. Cibles de services `CyAudioCore::*`, calculs sous `cydsp`. Aucun ancien header `cydsp/io`, `midi` ou `plugins` dans le code actif. |
| Audio fichiers | Tous les appels au lecteur commun passent par `registerCyAudioCoreReaders`. WAV et exports AIFF/FLAC/Vorbis utilisent les nouveaux services. Tests de seek/EOS, blocs de 4096, propriété des flux, quantification et relecture indépendante. |
| Métadonnées | Marqueurs/noms UTF-8, notes, régions, boucles et RIFF INFO conservés. Conventions exclusives/inclusives converties. Payload opaque invalide refusé ; champs BWF non convertis gardent le writer historique explicitement. |
| DSP des plugins internes | Buffers alignés, FFT convolution/stretch et analyse chroma conservés. Copie/remplissage/somme raccordés à VectorOps avec sélection automatique SIMD avant rendu ; aucune allocation ni détection CPU dans ces boucles. Blocs impairs, padding, canaux absents et copie en place vérifiés. |
| Extension numérique | Source mémoire + transport CyAudioCore → MultiBusGraph CyDSP → Dynamics avec sidechain externe → LevelMeter, test sans JUCE. Blocs 67/34 frames, fin de source et reset vérifiés. Aucun remplacement arbitraire des algorithmes musicaux existants. |
| Services | Identités persistantes et ambiguïtés audio/MIDI, callback virtuel et horloge/génération, proxy PCM borné/seek exact et refus d'écrasement testés sans matériel. |
| Hôtes | Fixtures originales CLAP/LV2/JSFX/VST3 : scan, création, activation, traitement, états et worker séparé. JSFX slider7 avec changement à la frame32. |
| Distribution | Notices CyAudioCore ET CyDSP copiées dans les sorties et prévues dans l'installation runtime. Packager renforcé ; checkouts CI épinglés, pas de push ni de publication. |

## Anciens blocages VST3 : résolus

CYDSP-010 (contrôleur sans état optionnel) et CYDSP-011 (lecture partielle non vide)
sont corrigés dans CyAudioCore. Les deux plugins originaux `CyDAW Test Gain.vst3`
et `CyDAW Graph Runtime.vst3` passent maintenant les sauvegardes/restaurations,
en direct et dans le worker, sans appliquer notre ancien patch. Ils ont aussi été
recompilés avec les bibliothèques actuelles et revérifiés. Cela ne démontre pas
la compatibilité avec tous les plugins tiers.

L'[ancien rapport](reports/CyDSPIntegrationReport.monolith.fr.md), son patch et ses
résultats sont conservés pour l'historique ; leurs erreurs ne sont plus présentées
comme des défauts actuels de la bibliothèque.

## Points à traiter pour retirer JUCE complètement

### AUDIO-001 — ASIO : capacité à compléter dans CyAudioCore

`AudioBackend::asio` est présent dans l'API mais cela ne fournit pas un backend.
La recette actuelle miniaudio/PortAudio n'active pas ASIO. L'option PortAudio
expose WASAPI/WinMM (et DirectSound explicite) sous Windows ; elle ne rétablit pas
ASIO. Le preset ASIO du desktop s'appuie toujours sur JUCE.

Avant de supprimer ce chemin : fournir un backend ou driver externe CyAudioCore
avec sélection explicite, SDK/licence choisis, noms et identités stables, canaux,
fréquence/taille réellement négociées, timestamps/latences connus ou inconnus et
gestion des changements de périphérique. Ne pas substituer WASAPI à un réglage
ASIO enregistré. Validation : ouverture/arrêt/reconnexion et entrée/sortie réelle
sur la carte de l'utilisateur, après les tests virtuels ; non effectuée ici.

### PLUGIN-001 — Programmes et unités : contrat public incomplet

L'API publique `cyaudio/plugins/Plugin.h` permet les paramètres, événements,
transport, états, ports et éditeurs. Elle n'expose pas encore un contrat complet
pour les programmes/unités VST3. CyDaw demande déjà les programmes et le choix de
programme à son worker ; son CLAP actuel reconnaît aussi un paramètre Preset.

À compléter dans CyAudioCore, ou à adapter explicitement dans CyDaw avant la
bascule : liste/identifiants de programmes, programme courant/changement, liens
aux unités et effets sur paramètres/état. Tester deux programmes distincts avec
rappel et automation. Ne pas supprimer la fonction des anciens projets.

### APP-001 — États et automation : adaptation dans CyDaw

L'état VST3 historique enveloppé par JUCE n'est pas `CyDSP.VST3State1`.
Le JSFX historique de CyDaw commence par `0x43594a53`/version1 ; CyAudioCore
conserve `CyJSFX`/version1. Le test confirme que le second n'accepte pas le premier.
Écrire des conversions bornées et versionnées, avec ancien lecteur et tests de
sliders clairsemés/`@serialize`. Mapper les anciens index d'automation aux IDs
stables ; les plages CLAP natives et VST3 normalisées ne sont pas interchangeables.
Les signatures amont ne doivent pas être renommées avec la bibliothèque.

### APP-002 — Worker et fenêtres : adaptation dans CyDaw

Le protocole CyDaw v6 (scan, états, programmes, fenêtres et pont Wine) reste dans
`src/plugin_worker`, `IsolatedPluginHost` et `WindowsPluginBridge`. Le worker
CyAudioCore représente une instance, avec un bloc de latence, des réponses tardives
silencieuses et une API différente. Il n'est pas interchangeable avec v6.
Migrer commandes, collecte des événements, pertes de connexion et compensation
de latence. Tester parent natif, resize, DPI, focus, clavier et fermeture sur
chaque plateforme. Le support Windows des éditeurs existe côté bibliothèque ;
le test audio/états exécuté ici ne valide pas ces interactions graphiques.
AU/AAX ne sont pas des hôtes livrés par l'API actuelle ; préserver les chemins
utilisés par l'application sur leurs plateformes avant retrait d'un ancien hôte.

### APP-003 — Périphériques, MIDI et sources : adaptation dans CyDaw

`AudioTrackController` possède encore `juce::AudioDeviceManager`,
`AudioSourcePlayer`, les sources/synthés historiques et les callbacks du recorder.
`TransportSync` utilise aussi ce gestionnaire pour ses ports MIDI.
Porter préférences/identités, consentement de reconnexion, snapshots d'horloge,
collecteur MIDI, gestion des doublons et notifications. Les contrats publics
CyAudioCore sont présents ; la majorité de ce point est un travail application.
Les réglages des cartes existantes n'ont pas été modifiés pendant cette reprise.

Les sources de playback/read-ahead et enregistrement existent sous `cyaudio`.
Migrer leur propriété, invalidation, préchargement et file de commandes en gardant
les comportements d'édition, de boucles et de tempo. Les modes beats/texture de
CyDaw ne doivent pas changer de son lors du remplacement : raccorder un provider
aux algorithmes existants si nécessaire. Conserver l'arrêt/join hors callback.

### APP-004 — Modèle, OS et interface : responsabilité application

Les données de projet et utilitaires JUCE (String/var/ValueTree/XML, fichiers,
processus et threading), la couche hôte/export VST3 historique et certaines
fenêtres restent utilisées. CyAudioCore n'est pas un toolkit GUI ni une API XML
générale. Les remplacer avec les abstractions de CyDaw/CyUI et des migrations
versionnées ; aucun besoin de recopier l'ensemble de JUCE dans CyAudioCore.
L'audit lexical conserve 235 fichiers/18137 références JUCE, commentaires et
cibles historiques compris : c'est un inventaire, pas une mesure du code exécuté.

## Limites de validation

Windows x64/MSVC Release uniquement. Aucune carte son/MIDI physique, aucun son
émis ni micro ouvert, aucun plugin tiers installé n'a été chargé. Les tests
utilisent des fixtures originales, un backend virtuel et des profils isolés.
Pas de validation matérielle ASIO/JACK, de macOS/Linux ni d'éditeur natif tiers.
Le fournisseur PortAudio est disponible en option mais n'a pas remplacé le
backend du desktop dans cette reprise. Les nouvelles familles DSP sont accessibles
au code ; leur exposition complète dans les nœuds et l'UI n'est pas revendiquée.

Les futurs jobs GitHub doivent pouvoir lire les deux dépôts aux révisions épinglées.
Pour des dépôts privés, fournir `CYDAW_DEPENDENCIES_TOKEN` en secret avec accès de
lecture ; aucun token n'a été demandé, imprimé, créé ou modifié ici. Les workflows
sont préparés localement et n'ont pas été exécutés sur GitHub.
