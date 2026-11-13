# Remplacement de JUCE par CyAudioCore

Date : 13 novembre 2026. Branche `CyDSP-CORE`.
Architecture : **CyDaw → CyAudioCore → CyDSP numérique**, interface CyUI/ImGui.
Révisions : CyAudioCore `bb26e93604d43de2069694b0791bc0bcf5a53ea6`,
CyDSP `cc18dca043925b75adea481c1c8545a125607e1f`.
Les deux dépôts de bibliothèque restent inchangés.

## Résultat de compilation

Le desktop Windows complet et le worker de plugins compilent désormais sans
checkout, includes ni bibliothèques JUCE. Le répertoire neuf
`build/cyaudio-native-desktop` est configuré avec téléchargement et fixtures JUCE
désactivés. Le contrôle des 41 projets de dépendances générés confirme cette
séparation. Le moteur, les sources, les périphériques audio/MIDI et l'hébergement
de plugins utilisent effectivement les nouveaux chemins dans l'application.

Les anciens widgets et fixtures de comparaison restent dans les sources,
accessibles uniquement par option explicite ; ils ne sont pas des dépendances
cachées du produit. Les exports VST3/CLAP/LV2 natifs utilisent leurs SDK officiels.
L'exécutable garde son nom interne `CyDAW 2.exe` pour les scripts existants.

## Périmètre remplacé

| Domaine | Implémentation actuelle |
| --- | --- |
| Fenêtre et rendu | Win32, boucle native, D3D11, ImGui et CyUI |
| Documents et projets | Types applicatifs, nlohmann JSON, pugixml, sauvegarde atomique |
| Audio et MIDI | CyAudioCore ; ASIO applicatif via PortAudio, WASAPI/DirectSound, ports MIDI Core |
| Sources et instruments | Transport Core, buffers/vectorisation CyDSP, planification stéréo et cache applicatifs |
| Fichiers audio | Lecteurs, encodeurs et enregistrement Core, métadonnées historiques préservées |
| Plugins externes | Hôtes Core VST3, CLAP, LV2, JSFX dans le worker isolé |
| Paramètres et état | IPC natif, automatismes, transport, sauvegarde, éditeurs Win32 et contrôles génériques |
| Presets et scripts | Presets LV2 avec Lilv fourni par Core, diagnostics JSFX détaillés avec son ysfx |
| Synchronisation | MIDI/MTC/MMC, Link et JACK de transport ; OSC/UDP et horloge haute résolution natifs |
| Services | Fichiers/processus/sockets/threads/temporisations/presse-papiers/dialogues Win32, HTTP WinHTTP et ZIP en flux |
| Cinéma et aide | FFmpeg partagé, lecteur CyDoc local, langues et ressources conservés |
| Distribution | Presets/Actions/packaging natifs ; plus de checkout ou notice JUCE obligatoire |

## Problèmes rencontrés et correctifs applicatifs

- **Chemins sur deux disques** : `std::filesystem::relative` peut rendre un chemin
  vide sans erreur entre C: et D:. Cela faisait perdre les références de clips
  lors du rechargement. Le chemin absolu est conservé dans ce cas ; un test
  vérifie ce contrat et le roundtrip du projet rythmique passe à nouveau.
- **Arguments Unicode sous Windows** : les arguments étroits du CRT arrivent
  dans la page de codes locale. Le worker et les probes lisent désormais la
  véritable ligne de commande UTF-16 ; l'archive ZIP au nom accentué est testée.
- **Période WASAPI** : augmenter uniquement la capacité du callback ne suffisait
  pas. La FIFO nominale Core devait aussi être alignée sur la période négociée,
  sinon le worker recevait des blocs courts en rafale. Les deux sont alignées.
- **Libellé DirectSound** : le nom du périphérique principal est en ACP sur ce
  poste français. Le point d'entrée Windows normalise ce cas vers UTF-8.
- **Réglages de périphérique** : les véritables attributs XML historiques sont
  conservés ; la sélection physique n'est pas confondue avec les canaux denses.
- **État VST3** : ne pas enfiler inutilement des paramètres avant la sauvegarde
  Core, sinon celle-ci peut répondre `would_block`. Le codec historique est
  borné et distinct du base64 standard.
- **Tests** : le contrôle du cache attendait encore un ancien JSON alors que le
  cache binaire `CYSL` existe déjà. Le test attend l'écriture asynchrone du vrai
  format. Les banques optionnelles absentes sont annoncées comme telles ; les
  tests synthétiques restent obligatoires et le mode strict exige les samples.

## Évolutions souhaitables dans CyAudioCore/CyDSP

Aucun changement des bibliothèques privées n'a été fait pour contourner ces points.

| Priorité | Besoin observé | Adaptation actuelle / amélioration proposée |
| --- | --- | --- |
| P1 | Différencier période physique, quantum client et capacité maximale | Alignement dans le driver applicatif ; exposer/documenter séparément ces valeurs et leur relation à la FIFO |
| P2 | Garantir UTF-8 pour les noms DirectSound | Conversion ACP ciblée dans CyDaw ; normalisation à faire dans le provider |
| P2 | Découvrir et charger les presets LV2 via l'API publique | Lilv du même runtime Core ; ajouter catalogue/chargement de presets sans dépendre du layout de ses en-têtes privés |
| P2 | Retourner les diagnostics JSFX structurés | Même runtime ysfx pour source/ligne/message ; ajouter ces informations à l'API publique |
| P2 | Planifier des voix stéréo personnalisées | Ordonnanceur applicatif conservé ; le callback générique de synthèse Core est mono |
| P2 | Mettre en cache une source rendue arbitraire | Cache applicatif de pages ; la source de lecture anticipée Core attend un chemin de fichier |
| P2 | API de programmes générique entre formats | Adaptation LV2 et paramètre CLAP Preset ; documenter les capacités par format |

Ces adaptations n'utilisent pas JUCE. Elles ne justifient plus de conserver
l'ancien framework dans le produit.

## Validation

Build complet natif réussi : `output/cyaudio-strict-verified-build.log`.
Les tests autonomes couvrent les services réellement employés, le moteur,
les fichiers audio, les voix/MIDI, les périphériques, le transport et l'IPC.
La campagne finale des scénarios applicatifs et des exports est en cours ;
les résultats définitifs seront ajoutés après leur exécution.

Les profils de test sont isolés ; les tests ne doivent pas enregistrer des
réglages dans le profil de travail de l'utilisateur. Les tests de samples
installés accèdent à la banque existante en lecture uniquement.

## Limites d'acceptation matérielle et de plateforme

La cible validée est Windows x64. Cela ne constitue pas une validation du
desktop Linux/macOS, dont la fenêtre/dialogues/éditeurs natifs demandent un
portage spécifique. Les exports de plugins et le moteur portable sont distincts.

Un démarrage/arrêt silencieux réel Fireface ASIO et des callbacks WASAPI ont été
vérifiés. Cela ne mesure pas la latence analogique et ne remplace pas des essais
microphone, MIDI physique, hotplug/reconnexion ou l'écoute des sorties. Les
plugins tiers installés demandent encore leur propre matrice d'interopérabilité.
VST2 n'est pas annoncé : Core ne fournit pas ce provider. La compatibilité avec
d'anciens projets VST3 n'était pas exigée, mais un lecteur d'état borné conserve
les cas historiques pris en charge.

## Reproduire

Voir [les commandes de build](CyAudioCoreIntegration.md). La voie de production
ne doit pas activer `CYDAW_BUILD_LEGACY_JUCE_TESTS`. Aucune publication ni push
n'est déclenché par cette intégration locale.
