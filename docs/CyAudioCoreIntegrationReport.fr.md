# Intégration CyAudioCore : résultats et travail restant

Date : 13 novembre 2026. Branche `CyDSP-CORE`.
Architecture : **CyDaw → CyAudioCore → CyDSP numérique**.
CyAudioCore `bb26e93`, CyDSP `cc18dca` ; empreintes dans le
[snapshot](reports/CyAudioCoreSnapshot.json). Aucun de ces deux dépôts n'a été modifié.

## Résultat réel

CyAudioCore est maintenant employé par le desktop pour les périphériques audio
natifs et par son véritable worker VST3. Les codecs, buffers, FFT et opérations
numériques déjà migrés restent utilisés par les différents modes et plugins
internes. Ce travail dépasse les seuls tests autonomes des bibliothèques.

**Le desktop complet nécessite encore JUCE.** Il reste une couche ABI temporaire,
les sources et une partie du modèle de données, le MIDI et l'hôte LV2. Les exports
natifs CLAP/LV2 et le moteur commun peuvent déjà être construits sans JUCE.
Ce résultat valide une partie substantielle de la substitution ; il ne démontre
pas encore une application complète sans cette dépendance.

La compatibilité avec d'anciens projets VST3 n'est pas exigée pour cette reprise,
conformément à la demande. Le support VST3 actuel fonctionne : les anciennes
réserves sur ce format ne sont pas une raison de suspendre l'intégration.

## Chemins intégrés et essais

| Élément | Résultat vérifié |
| --- | --- |
| Fichiers | Lecteurs et encodeurs CyAudioCore, seek/EOS, WAV/AIFF/FLAC/Vorbis, métadonnées et propriété des flux. Les champs non convertis gardent leur chemin explicite. |
| DSP interne | Buffers alignés, FFT convolution/stretch/chroma et VectorOps préparées, y compris blocs impairs, padding, canaux absents et copie en place. Source/transport Core → graphe CyDSP → dynamique avec sidechain → mesure testés sans JUCE. |
| Périphériques desktop | Adaptateur vers `AudioDeviceContext`/`AudioDeviceManager` publics. Les pilotes ASIO/WASAPI/DirectSound JUCE sont désactivés dans les cibles migrées. Test du vrai thread de callback avec le périphérique nul Core : ouverture, deux démarrages/arrêts, horloge, fermeture, rejet d'un canal invalide. |
| ASIO | Extension appartenant à CyDaw, SDK Steinberg sous l'option GPL v3 et backend ASIO PortAudio. Interface externe Core ; pas d'ajout GPL dans les dépôts MIT. Formats planaires/entrelacés, sélecteurs non consécutifs, sortie seule, clocks, échec de démarrage, stop/abort et absence d'allocation/libération dans le callback vérifiés par injection d'API. |
| Matériel ASIO | Fireface USB : ouverture à 48 kHz, blocs clients de 64, démarrage/arrêt silencieux de 300 ms ; 224 callbacks / 14336 frames, aucune entrée activée. La latence de sortie déclarée par le backend reste 1069 échantillons ; ce n'est pas une mesure analogique ni la preuve d'une période matérielle de 64. |
| VST3 en production | Scan, création, ports, paramètres, audio, MIDI, transport, état, éditeur Windows via Core. L'hôte VST3 JUCE est compilé hors du worker. Test Gain et Graph Runtime originaux : processing/état/éditeur ; noms/unités/automation du graphe effectifs. Le protocole IPC v6 de CyDaw est conservé. |
| Session complète | Script de contrôle de l'application : gain JSFX −6 dB, tempo 60/120, recompilation, diagnostics de syntaxe et maintien de la version valide, CLAP/LV2/VST3, sauvegarde/relecture du projet, génération des exports VST3/CLAP/LV2. Aucun ancien projet requis. |
| Distribution | Licences/manifests CyAudioCore et CyDSP, SDK GPL, helpers PortAudio et provenance ASIO copiés et installables. Le packager refuse une notice ASIO incomplète quand l'extension est distribuée. |

Les [commandes](CyAudioCoreIntegration.md) et
[l'acceptation structurée](reports/CyAudioCoreAcceptance.json) donnent les preuves
et leurs limites. Les logs complets contenant des identifiants de matériel
restent locaux dans `output/`.

## Défauts corrigés dans CyDaw pendant l'intégration

1. **Réveil IPC Windows trop lent.** La boucle GUI temporisée retardait les blocs.
   Le worker est maintenant réveillé directement par un événement natif ; le
   callback ne l'attend pas. Les éditeurs continuent de recevoir leurs messages.
2. **Bloc tardif joué sans effet.** Un worker actif pouvait laisser passer le bloc
   courant brut, en violation de sa latence et du gain de l'effet. Il émet
   maintenant du silence sur dépassement ; le bypass de sécurité après arrêt ou
   crash reste explicite. Le premier bloc silencieux est couvert par test IPC.
3. **Tempo modifié sur le mauvais thread.** Les valeurs DSP sont locales au thread
   pour isoler les instances exportées. Le setter UI seul ne les publiait pas au
   rendu. Le master publie maintenant son tempo/état par des atomiques au début
   du callback. Le script dépendant du tempo passe dans l'application réelle.
4. **Deux runtimes EEL dans le worker.** L'ancien archive ysfx et le runtime Core
   exposaient les mêmes symboles VM. Le worker ne lie plus que le runtime Core ;
   l'adaptateur JSFX utilise ses headers publics identiques.

## Ce qui reste à porter dans l'application

### APP-001 — Retirer la couche ABI et les objets JUCE

Les adaptateurs `AudioIODevice`, `AudioPluginInstance` et les fenêtres de plugins
conservent encore des types JUCE, même quand le travail natif passe par Core.
`AudioSourcePlayer`, mixer/transport/read-ahead, synthés et recorder historiques
restent aussi utilisés. Les remplacer par les contrats sources/transport/record
Core est du travail CyDaw ; leur présence ne prouve pas un manque de la bibliothèque.
Conserver les tests d'édition, boucles, lancement, compensation et rendu lors des
remplacements, ainsi que l'arrêt/join hors callback.

Les données String/var/ValueTree/XML, fichiers/processus et certaines fenêtres
restent à porter vers les utilitaires de l'application et CyUI. CyAudioCore
n'est pas une bibliothèque générale d'interface ou de sérialisation de projets.
L'audit lexical n'est pas une mesure du code exécuté ni un pourcentage de migration.

### APP-002 — MIDI, CLAP/LV2/JSFX et exports VST3

L'énumération, les entrées/sorties MIDI, le collecteur et TransportSync utilisent
encore JUCE. Les contrats MIDI publics Core sont disponibles ; il faut raccorder
les identités, la collecte, les timestamps et les préférences côté application.

Le worker emploie encore l'hôte LV2 JUCE et les adaptateurs CLAP/JSFX existants.
Leurs scénarios de production passent, et les quatre hôtes Core passent séparément ;
la bascule de tous ces chemins n'est donc pas revendiquée. Préserver les diagnostics
JSFX et les presets LV2 lors du raccordement. Les exports VST3 historiques
conservent aussi leur couche JUCE, contrairement aux exports natifs CLAP/LV2.

### CORE-001 — Programmes/unités : capacité publique à compléter

L'API actuelle ne fournit pas de liste/choix générique de programmes/unités VST3
ni de presets LV2 équivalente à celle exposée par le desktop. Le nouvel adaptateur
VST3 propose donc le programme Default ; les états et paramètres sont pris en
charge. Cette limite ne bloque ni l'hébergement VST3 de base ni les essais demandés,
et n'est pas présentée comme une exigence sur les anciens projets.
Pour conserver toutes les commandes de presets lors de la bascule LV2, prévoir un
contrat public stable et ses tests de rappel/automation.

### APP-003 — États et fenêtres : terminer les contrats de bord

Les commandes actuelles d'édition de graphe utilisant l'enveloppe XML sont
converties en état Core borné. Les sauvegardes simples testées passent. Un état
Core peut retourner `would_block` tant qu'une édition UI attend le traitement ;
il reste à renforcer le protocole de snapshot/retry pour ces éditions et tester
le cas d'une sauvegarde immédiate pendant un geste, sans traiter un échec comme
un état vide réussi. C'est une intégration de lifecycle, pas une absence VST3.

DPI, focus, clavier, resize et fermeture demandent encore des essais interactifs
avec des plugins tiers. Le chemin d'éditeur intégré a été testé avec nos fixtures.
Le reconnect/hotplug natif n'est pas activé automatiquement dans l'adaptateur ;
la sélection de périphérique reste explicite. Aucun repli silencieux ASIO→WASAPI.

## Limites des essais

Windows x64/MSVC Release. Lecture de session testée avec les haut-parleurs coupés ;
sonde ASIO matérielle uniquement en sortie silencieuse. Pas de validation de
capture/micro, de latence analogique, de reconnexion matérielle, de JACK, de
macOS/Linux/ARM ou d'un parc de plugins tiers. Les API et adaptateurs sur les
autres plateformes ne sont pas déclarés validés par une compilation Windows.

Les dépendances CI sont épinglées. Les jobs privés nécessitent un secret de lecture
`CYDAW_DEPENDENCIES_TOKEN`. Aucun workflow distant, push ni publication n'a été lancé
pour cette reprise. Les anciens rapports monolithiques restent archivés ; leurs
anciens défauts VST3 corrigés ne sont pas réutilisés comme blocages actuels.
