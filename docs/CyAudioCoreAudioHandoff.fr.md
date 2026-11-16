# CyDaw : bilan audio à transmettre à l’agent CyAudioCore

Établi le 16 novembre 2026 à partir des campagnes existantes. Aucune nouvelle mesure audio n’a été exécutée pour ce bilan. « Ouvert » signifie que la correction n’a pas été démontrée : les constats historiques doivent être rejoués sur le build actuel. Aucun ne prouve à lui seul un défaut de CyAudioCore.

## Versions

- CyDaw : `a429389de25d0147c2bdff20dff1d058e613768a`, branche `CyDSP-CORE`.
- Build actuel : `build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe`.
- CyAudioCore intégré : `f9b0c813c233e9d5d3cfdef6c8492bbe8f5548ea` ; CyDSP : `0d67c6a60749ef241a87bc3592fa63c61998a5e2` ; CyUI : `7a793e9281e78572818d85e9cf856d779a7792a1`.
- Témoin JUCE figé : `cf4228d`. Chaque campagne a son propre manifeste ; ne pas confondre les anciennes révisions Core et le build actuel.
- Campagnes audio citées : Windows x64, MSVC Release ; ASIO Fireface USB 48 kHz / 1 024 frames, zéro entrée, sorties silencieuses. MIDI désactivé pour les campagnes de projets.
- L’application et son worker actuels sont indépendants de JUCE. Les anciennes sources/builds restent pour comparaison.

## 1. CUE DJ : position au relâchement — défaut historique commun

Le même test échoue côté JUCE et Core en ASIO / 1 024 frames. Journal arrondi : position 8,22 s, repère 8,21 s. L’assertion exige simultanément l’arrêt, une distance au repère strictement inférieure à 10 ms et un repère proche de 8 s.

Sans changer l’assertion, les deux versions passent en WASAPI / 480 frames. À 48 kHz, les quanta valent 21,333 ms et 10 ms. Cela suggère d’examiner consommation des commandes et publication de position ; ce n’est pas une preuve contre le pilote ou le SDK.

**Il s’agit du bouton CUE de transport d’une platine, pas d’un défaut démontré du routage de préécoute casque.**

- Code : `src/app/DjEngine.cpp::cue` (491), commandes `cueDown` / `cueUp` (758 / 773).
- Test : `tests/v2_dj_check.py`, 244–250.
- Preuves : `docs/CyAudioCoreComparison.fr.md`, section CUE ; `docs/reports/CyAudioCoreComparison.json` ; `output/juce-core-comparison/functional/{JUCE,CyAudioCore}/cydaw2_dj_check/test.log`.
- À relever : commande reçue/consommée, position réelle avant/après jump, position publiée après le bloc, état lecture/arrêt ; plusieurs tailles de bloc, tolérance inchangée.
- Responsabilité à examiner d’abord : lecteur/ordonnanceur/publication **CyDaw**. Isoler une violation du contrat source/callback avant attribution à Core.

## 2. DC après Cinéma → Studio → nouveau projet — défaut historique commun

Campagne initiale : Core `de66021` contre JUCE `cf4228d`. Studio neuf : traîne silencieuse. Après lecture/arrêt Cinéma, retour Studio et nouveau projet : composante continue dans l’export, entrées physiques désactivées.

| Après transition, amplitude PCM normalisée | JUCE | Core historique |
| --- | ---: | ---: |
| Moyenne gauche | −0,0069364 | −0,0913291 |
| Moyenne droite | +0,0184723 | +0,0076092 |
| Pic absolu dans la traîne | 0,0184721 | 0,0913296 |

Studio neuf : moyennes de l’ordre de 2 à 5 × 10⁻⁷, pic de traîne nul dans les valeurs publiées. Retirer les moyennes **uniquement pour diagnostic** ramène le résidu entre versions vers −141 à −143 dBFS. Les exports originaux conservent le défaut.

- Preuves : `docs/CyAudioCoreComparison.fr.md`, « P1 commun — composante continue après Cinéma » ; JSON associé et WAV sous `output/juce-core-comparison/`.
- À sonder : buffers avant/après mixage/master ; détachement et arrêt des sources Cinéma ; fermeture vidéo/audio ; création de projet ; état des effets/délais ; moyenne et pic à chaque frontière.
- Points d’entrée applicatifs : `src/app/AudioTrackController.cpp`, `MasterOutputAudioSource`, `startMasterExport`, cycle de vie des sources Cinéma. Ce sont des pistes, pas des causes confirmées.
- Priorité élevée pour la fiabilité des exports. Ne pas masquer le défaut par un filtre DC global.

## 3. Projet Échos et mouvement — écart de chaîne encore inexpliqué

Campagne du 15 novembre : 100 captures sur 25 cas, deux répétitions par moteur, via le véritable callback/master. WAV finis, non globalement silencieux, aucune valeur à la limite numérique. Seulement 10/25 cas restent sous −110 dBFS de résidu dans les deux paires : le panel ne prouve pas l’identité générale des moteurs.

`08-echos-et-mouvement` complet : écart de niveau maximal **0,63502723 dB**, résidu **−30,44 dBFS** dans les deux paires.

Contrôle distinct sur 16 temps : ajouter 125 ms d’attente avant export change le rendu avec modulation des deux côtés. Profondeurs chorus/trémolo nulles : écart interversions persistant de **0,487614 dB**, résidu **−32,15 dBFS**.

Contre-épreuves : 55 paires de noyaux d’effets sans pilote/UI, dont 37 identiques float32 ; sur les autres, erreur absolue maximale **3,725290298 × 10⁻⁷**, résidu maximal **−142,51 dBFS**. Les états natif/voix comparés à 10⁻⁶ ne diffèrent pas ; une note isolée donne environ 0,055895399302244 RMS des deux côtés.

Ces noyaux ne reproduisent pas l’écart de chaîne. Vérifier d’abord fréquence préparée, horloges/transport, phases/états de modulation, nombre/tailles de blocs exécutés avant export, entrées effectives et publication des paramètres. Cela oriente vers le contexte sans innocenter universellement le DSP.

- Code : `src/engine/EffectProcessors.cpp::ModulatedDelay::process`, `Tremolo::process`, `src/app/AudioTrackController.cpp::startMasterExport` et `renderVoices`, `src/native_plugin/NativeRuntime.cpp`.
- Preuves : `docs/CyAudioCoreProjectAudio.fr.md`, `docs/reports/CyAudioCoreProjectAudio.json` ; `output/cyaudio-project-audio/2026-11-15/accepted`, contrôles voisins `phase`, `effects`, `runtime-state`.
- SDK Core de cette campagne : `4e5710999acbd88ab3d1ce87f4101ec73abe54b6`, antérieur à la veille actuelle.

## 4. Autres différences à interpréter avant correction

- **Samples prêts** : Pas de cinq conserve 0,00886377 dB d’écart et −46,94 dBFS de résidu ; Néons, 0,04268303 dB et −49,94 dBFS. Deux paires reproduites, répétitions identiques par moteur après attente explicite ; `streamUnderruns` et `streamsRefused` samples nuls avant/après. Ces compteurs ne mesurent pas les xruns ASIO.
- **MIDI** : pour les frames demandées 0, 10, 22, 60, JUCE notifie 0, 10, 10, 60 ; l’adaptateur actuel, 0, 10, 22, 60. Différence de contrat démontrée : subdivision minimale JUCE de 32 frames, contre positions exactes actuelles. Les 12 frames valent 0,25 ms. Ne pas réintroduire d’imprécision pour égaler JUCE. Références : `include/cydaw/audio/VoiceAllocator.h::renderNextBlock`, appels `renderVoices`.
- **Prisme** : les deux versions mélangent l’adresse de la voix à la graine aléatoire. Les répétitions divergent déjà vers −16/−17 dBFS. Le résidu interversions n’est donc pas une preuve directe de dégradation numérique ; une graine QA stable serait nécessaire.

Détails dans le rapport projets/JSON précité. La contribution exacte de ces mécanismes aux WAV complets reste à isoler.

## 5. Premier bloc différent après rechargement

Campagne `output/usability-2026-11-15/acceptance-4/report.json`, build `cyaudio-sleep` : différence brute maximale **0,014268159866333008**, frames **480 à 1 023**. Dès la frame 1 024, PCM identique.

La validation du reste du fichier ne constitue pas une validation de l’export entier. Cause non établie ; pas de contre-épreuve JUCE sur ce scénario.

- Preuves : `docs/UsabilityCorrections.fr.md` ; `tests/usability_acceptance.py`, lignes 55–56 ; JSON précité.
- À contrôler : alignement capture/transport, `skipSamples`, compensation de latence, automation restaurée, initialisation des effets/buffers.
- Frontière : `MasterOutputAudioSource::startCapture`, `AudioTrackController::startMasterExport`. Intégration CyDaw à examiner avant attribution au SDK.

## 6. Export long : pertes de blocs historiques à reproduire

Le rapport `docs/CyAudioCorePluginSleep.fr.md` conserve explicitement ce problème. Sa résolution n’est pas démontrée. Le relevé brut chiffré n’a pas été retrouvé avec assez de certitude pour l’annexer : ce bilan ne réaffirme pas de nombre de blocs perdus.

Chemin actuel : `MasterOutputAudioSource::startCapture` crée un `AudioFormatWriter::ThreadedWriter` avec capacité `sampleRate * 4`. `stopMasterExport` contrôle `droppedBlockCount()` et renvoie une erreur si des blocs sont perdus. Voir `src/app/AudioTrackController.cpp`, autour de 1820, puis 16521–16535 et `droppedMasterExportBlocks`.

Reproduire avec plusieurs minutes de signal synthétique déterministe. Relever durée prévue/réelle, frames soumises/acceptées/écrites, remplissage FIFO, retours de write et erreurs/temps disque. Distinguer saturation de capture, retard du writer et callback manqué. Aucun lien causal démontré avec ASIO ou le writer Core.

## Travail demandé à l’agent CyAudioCore

1. Relire les rapports et JSON. Reproduire sur le SDK intégré, puis séparément sur le dernier SDK, avec empreintes exactes et répertoires neufs. Ne pas présenter les observations historiques comme des mesures du build actuel.
2. Commencer sans matériel pour les contrats buffers, reset, transport, writer et positions. Les essais physiques doivent respecter les autorisations de la session où ils sont lancés.
3. Pour toute attribution Core/CyDSP, fournir une sonde minimale. Si seul CyDaw reproduit le défaut, préciser la frontière et le contrat à vérifier côté application.
4. Instrumentation opt-in : aucune allocation, attente, verrou ni journalisation dans le callback préparé. Recueillir les mesures hors callback ; conserver WAV et assertions brutes.
5. Livrer par point : reproduction ou non, versions/commandes, valeurs avant/après, cause démontrée ou hypothèse, couche responsable et test de non-régression. Ne pas filtrer, normaliser, réaligner ni élargir les tolérances pour présenter une égalité artificielle.

Les commandes de reproduction sont dans `docs/CyAudioCoreComparison.fr.md`, `docs/CyAudioCoreProjectAudio.fr.md` et `docs/UsabilityCorrections.fr.md`. Utiliser des sorties de campagne neuves ; les scripts, profils isolés, manifests et preuves antérieurs restent intacts.

La dernière intégration de veille valide ses propres scénarios plugins, pas les six points ci-dessus. Ce bilan n’est pas un nouveau benchmark CPU, une écoute humaine, une mesure physique de latence ou une validation Linux/macOS. Les médias musicaux privés ne sont pas joints et ne doivent pas être committés : reproduire avec des fixtures synthétiques.
