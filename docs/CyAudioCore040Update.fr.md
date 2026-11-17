# CyAudioCore / CyDSP 0.4 : intégration et validation du CUE ASIO

Validation locale du 17 novembre 2026, branche `CyDSP-CORE`, base applicative `b2e4efb`. Le correctif CUE/export était déjà présent dans cette branche ; aucun cherry-pick ni modification des dépôts frères n’a été effectué.

## Versions intégrées

- CyAudioCore : `8716ce67b0c3467017369cdc7745e8ff8ef147f2`.
- CyDSP : `c1d9a52fce7ac90515b0e264545903fdad2901b4`, version committée **0.4.0**.
- CyUI inchangé : `7a793e9281e78572818d85e9cf856d779a7792a1`.
- Correctif applicatif : `b2e4efb121b206eaf04cb04177f36bcd765f9750`.

Des archives des commits SDK sont extraites sous `output/cyaudio-update-2026-11-17/sdk`. Le travail CyDSP non committé, dont sa version locale 0.4.3 et les expériences HRTF, est explicitement exclu. Les empreintes de tous les fichiers des snapshots sont identiques avant/après validation.

Le configure confirme `CyAudioCore dependency: CyDSP 0.4.0`. L’application, son worker et les tests Release ont été reconstruits ensemble. Les pins de `.github/actions/audio-dependencies/action.yml` sont actualisés pour les prochains builds autorisés ; aucun push ni lancement GitHub Actions n’a été effectué.

Exécutable actualisé : [CyDaw](../build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW%202.exe).

## Correctif CUE : résultat mesuré

L’ancien `DeckPlayer` pouvait continuer à déplacer sa position pendant la transition de vitesse vers zéro après le jump au point CUE. Le commit applicatif introduit `stopAt` : il conserve la dernière sortie pour son court fondu vers le silence, tout en maintenant la position au repère. Il s’agit du transport de la platine, pas du routage casque.

- Test natif `cydaw_dj_cue_tests` : tailles de bloc 64, 480, 1 024, 2 048 et 4 097, avec/sans keylock ; CUE maintenu, relâché, arrêt depuis la lecture et stabilité de position. Tous les cas passent avec une tolérance de 10⁻⁸ s.
- Application réelle : **ASIO Fireface USB, 48 kHz / 1 024 frames**, zéro entrée audio/MIDI, sorties physiques silencieuses, profil isolé et fenêtre masquée.
- **20 essais** : dix sans keylock, dix avec ; chaque essai vérifie le retour après relâchement puis le retour depuis la lecture, soit **40 retours contrôlés**. Dérive maximale mesurée au relâchement : **0 seconde** ; la position reste ensuite stable.
- Le test historique `tests/v2_dj_check.py` passe intégralement, sans changement de son assertion de 10 ms. Son adaptation d’exécution isole le profil et masque les fenêtres ; les paramètres ASIO effectifs sont enregistrés. Collection, analyse, sync, boucles, hot cues, mixage et enregistrement passent également.

Le point CUE du bilan précédent est donc revalidé sur ce build Windows ASIO. Les autres pilotes, matériels et plateformes ne sont pas couverts par cette conclusion.

## Export et erreurs de finalisation

Le commit `b2e4efb` conserve les erreurs de write/flush/close et le bilan de frames jusqu’à la fin du thread écrivain ; `finishMasterExport` vérifie la complétude après drainage. Ces garanties ne démontrent pas l’absence de saturation sur toute machine ou tout projet.

Les tests natifs vérifient notamment : saturation de file et comptabilité, erreurs finales injectées, clôture effective/idempotente et WAV de 120 secondes relu échantillon par échantillon. La file du test de WAV natif est volontairement surdimensionnée : il teste l’intégrité et le drainage, pas les performances d’un disque lent.

Un contrôle complémentaire utilise le véritable export master/callback ASIO et la file normale de production : signal stéréo synthétique de **120 secondes**, suivi de la traîne normale de **2 secondes**.

| Mesure | Résultat |
| --- | ---: |
| Frames attendues et écrites | 5 856 000 / 5 856 000 |
| Fréquence | 48 000 Hz |
| Durée du fichier | 122 secondes |
| Erreur PCM maximale par rapport au signal source | 0 |
| Pic dans la traîne | 0 |
| Valeurs PCM finies | Oui |

Aucune perte de frames n’est observée dans ce cas. Le signal source est PCM16 et le fichier master PCM24 ; la comparaison porte sur leurs valeurs décodées, sans filtrage, normalisation ni réalignement. Cela ne clôt pas universellement le signalement historique d’export musical long.

**Limite d’intégration relevée par lecture du code :** `Studio::tick` ignore encore la valeur retournée par `finishMasterExport`. Les tests d’erreurs de sink valident le retour au niveau natif/appelant ; cette campagne n’affirme pas que l’interface affiche ces erreurs. Cette dernière frontière reste à traiter et tester.

## Autres vérifications

- **36/36 tests natifs** réussis : trois ciblés moteur/CUE/fichiers, puis 33 autres tests incluant DSP, instruments, buffers, sources, MIDI, fichiers, services, plugins et graphe de production sans JUCE. Les tests sont fonctionnels ; leurs durées ne sont pas présentées comme un benchmark CPU.
- Huit plugins CLAP : huit workers, chaque plugin restant à deux blocs traités en veille ; **37 blocs supplémentaires évités par instance** pendant l’observation. Réveil sur automation, lecture, retour en veille, sauvegarde/rechargement et fermeture des enfants passent. Ce sont des compteurs, pas un pourcentage de gain CPU.
- Binaires et DLL stables pendant l’acceptation ASIO ; session arrêtée normalement, sans terminaison forcée.

## Preuves et reproduction

[Résumé JSON, résultats et empreintes](reports/CyAudioCore040Update.json).

Dossier local : `output/cyaudio-update-2026-11-17/`. Conserve `sdk-manifest.json`, `sdk-stability.json`, `configure.log`, `build.log`, les deux rapports CTest XML/log, `asio/report.json`, `original-dj.log`, `original-dj-audio.json` et `sleep/report.json`. Les anciens relevés ne sont pas écrasés.

```powershell
cmake -S . -B build/cyaudio-sleep -DCYDAW_CYAUDIOCORE_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-update-2026-11-17/sdk/CyAudioCore -DCYDAW_CYDSP_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-update-2026-11-17/sdk/CyDSP
cmake --build build/cyaudio-sleep --config Release --parallel 4
ctest --test-dir build/cyaudio-sleep -C Release -R '^(cydaw_.*_tests|cydaw_no_juce_production_graph)$' --output-on-failure
output/cyaudio-qa-python/Scripts/python.exe tests/cyaudio_update_acceptance.py --exe 'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe' --output output/cyaudio-update-new/asio --seconds 120
output/cyaudio-qa-python/Scripts/python.exe tests/cyaudio_plugin_sleep_check.py --exe 'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe' --plugin build/cyaudio-sleep/Release/cydaw_plugin_sleep_fixture.clap --mode asio --output output/cyaudio-update-new/sleep
```

Les deux dernières commandes ouvrent la sortie ASIO silencieuse et supposent son autorisation dans la session concernée. Employer des dossiers de sortie neufs. Le script CUE/export utilise exclusivement des données synthétiques ; aucun média musical privé n’est ajouté au dépôt ou à la distribution.

## Points non revalidés

La DC après transition Cinéma, les résidus du projet Échos et mouvement, les différences de samples/instruments et le premier bloc après rechargement du [bilan précédent](CyAudioCoreAudioHandoff.fr.md) restent hors de cette campagne. Pas de nouvelle comparaison CPU JUCE/Core, mesure physique de latence, écoute humaine, matrice exhaustive de plugins tiers ou validation Linux/macOS. Les nouveaux outils spatiaux du SDK ne sont pas ajoutés à l’interface par cette mise à jour de dépendances.
