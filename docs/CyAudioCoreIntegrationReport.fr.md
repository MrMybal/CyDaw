# Remplacement de JUCE par CyAudioCore

État historique ; les mises à jour et substitutions numériques du 14 novembre sont
décrites dans [la nouvelle campagne](CyDSPNumericalReuse.fr.md).

Date : 13 novembre 2026. Branche `CyDSP-CORE`.
Architecture : **CyDaw → CyAudioCore → CyDSP numérique**, interface CyUI/ImGui.
Révisions : CyAudioCore `bb26e93604d43de2069694b0791bc0bcf5a53ea6`,
CyDSP `fda6bd30c5b622795cc08e8bd2750188a6c5a0c0`.
Les deux dépôts de bibliothèque restent inchangés par cette intégration.
La révision CyDSP précédente `cc18dca043925b75adea481c1c8545a125607e1f` diffère seulement
par sa documentation et ses outils de rapport : les sources compilées ont le même
SHA-256. Le [snapshot final](reports/JuceRetirementSourceSnapshot.json) conserve
les révisions, les périmètres de hachage et les empreintes vérifiées.

## Résultat final

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
- **Environnement Unicode** : `getenv` du CRT dégradait aussi les variables de
  chemin Windows accentuées. Leur lecture utilise maintenant UTF-16. Le scénario
  complet des quatre formats passe depuis un dossier accentué.
- **Transport vide** : une session sans pistes doit suivre l’horloge master ;
  le suivi JACK start/stop/locate fonctionne aussi dans ce cas.
- **ASIO** : initialisation COM avant la découverte et rejet d’un quantum non
  annoncé par le pilote. La Fireface annonce actuellement 1 024 échantillons ;
  accepter artificiellement 64 provoquait un découpage PortAudio en rafale.
  Le test positif à 1 024 et le rejet explicite à 64 sont vérifiés.
- **Répertoire de samples** : parcours paresseux au lieu de matérialiser toute
  l’arborescence avant le premier résultat, permettant l’annulation rapide.
- **Éditeur de plugin** : les changements d’état, métadonnées et demandes de
  reconfiguration rafraîchissent aussi les paramètres des contrôles génériques.
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
| P2 | Classification explicite des plugins | `PluginDescriptor` ne donne pas de type instrument/effet/MIDI ni de catégories ; l’heuristique ports audio/notes est insuffisante pour les hybrides. Ajouter ces champs publics |

Ces adaptations n'utilisent pas JUCE. Elles ne justifient plus de conserver
l'ancien framework dans le produit.

## Validation terminée

Les résultats structurés et chemins des preuves sont conservés dans
[JuceRetirementAcceptance.json](reports/JuceRetirementAcceptance.json).

| Vérification | Résultat |
| --- | --- |
| Production Windows Release, téléchargement et fixtures JUCE désactivés | Application, worker et Audio Probe compilés dans un répertoire neuf |
| Dépendances MSBuild de l’application et du worker | 41 projets inspectés ; aucun include ni lien JUCE |
| Suite CTest native complète | 119 cas enregistrés, 0 échec ; Batterie ignoré dans le profil isolé sans banque, puis exécuté séparément avec succès |
| Banque réelle en mode strict et Batterie | 2/2 réussis avec `D:/CyDAW/Banks` en lecture |
| Contrats finaux ASIO, manager, ports, plateforme et absence JUCE | 5/5 réussis après les derniers correctifs |
| Application réelle : JSFX/CLAP/LV2/VST3 | Scripts/rechargement/erreurs/tempo, presets, état, sauvegarde/relecture et génération des trois projets d’export réussis, y compris dossier accentué |
| Même scénario sur Fireface ASIO, 48 kHz / 1 024 échantillons | Réussi, entrées désactivées et sortie silencieuse |
| Exports neufs LV2 et CLAP | Audio, état, paramètres et interface native réussis |
| Export neuf VST3 | Audio, paramètres, ouverture/fermeture/réouverture de l’éditeur réussis |
| Validateur officiel VST3 | 47 réussis, 0 échec |
| Worker IPC natif face à un VST3 externe | Audio, crash/bypass, état, paramètres, boîte aux lettres et éditeur réussis |
| Paramètres épinglés et modulation VST3 | Sauvegarde/relecture réussie |
| Cinq fenêtres d’instruments/effets | Captures, tailles et fermeture réussies |
| Packaging sans checkout JUCE | 8 tests réussis ; archive Alpha locale produite et inspectée |
| Sécurité du commit | Scan Gitleaks du diff indexé effectué avant commit |

Les tests IPC et des cinq fenêtres utilisent aussi un plugin de comparaison
historique construit avec JUCE : cela vérifie qu’un plugin tiers peut employer
ce framework sans l’imposer à l’hôte. Les exports neufs VST3/CLAP/LV2 ont été
compilés et contrôlés séparément sans JUCE.

Les profils de test isolent `APPDATA`, `LOCALAPPDATA` et `Locations.json` ; les
réglages de travail ne sont pas remplacés. Les images du mode Cinéma, sa bande
de vignettes, ses clips audio et sa fenêtre de prévisualisation ont aussi été
inspectés. Les vérifications audio automatiques mesurent les buffers/mètres et
rendent les sorties silencieuses : ce n’est pas une validation d’écoute humaine.

La validation exige Python 3.12+ pour lire les WAV PCM extensibles émis par Core.
La configuration et l’Action Alpha utilisent cette version avec les dépendances
épinglées dans `tests/requirements-native.txt`.

## Limites d'acceptation matérielle et de plateforme

La cible validée est Windows x64. Cela ne constitue pas une validation du
desktop Linux/macOS, dont la fenêtre/dialogues/éditeurs natifs demandent un
portage spécifique. Les exports de plugins et le moteur portable sont distincts.

La fabrique de périphériques a été exercée réellement en Fireface ASIO et en
WASAPI. Le probe ASIO a reçu 15 callbacks / 15 360 frames sur 300 ms au quantum
annoncé de 1 024 ; sa latence backend rapportée est de 1 069 échantillons. Cela ne mesure pas la latence analogique et ne remplace pas des essais
microphone, MIDI physique, hotplug/reconnexion ou l'écoute des sorties. Les
plugins tiers installés demandent encore leur propre matrice d'interopérabilité.
VST2 n'est pas annoncé : Core ne fournit pas ce provider. La compatibilité avec
d'anciens projets VST3 n'était pas exigée, mais un lecteur d'état borné conserve
les cas historiques pris en charge.

## Reproduire

Voir [les commandes de build](CyAudioCoreIntegration.md). La voie de production
ne doit pas activer `CYDAW_BUILD_LEGACY_JUCE_TESTS`. Aucune publication ni push
n'est déclenché par cette intégration locale.

## Comparaison de performances après migration

Le [rapport de comparaison contrôlée](CyAudioCoreComparison.fr.md) complète cette acceptation. Il constate un CPU plus élevé en lecture, un chargement initial de bibliothèque plus lent et deux problèmes communs à l’ancienne version : CUE au quantum ASIO de 1 024 et composante continue après Cinéma. La suppression de la dépendance JUCE est terminée ; elle ne constitue pas une preuve d’équivalence des performances. Aucun code audio n’a été modifié pendant cette comparaison.
