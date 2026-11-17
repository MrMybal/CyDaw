# CyAudioCore / CyDSP 0.3.2 : intégration et vérifications

Campagne locale du 15 novembre 2026, branche `CyDSP-CORE`, base `1e4695d`. Aucun push ni workflow distant. Les dépôts voisins restent en lecture seule.

Mise à jour ultérieure : le [rapport du passage à un worker natif unique](CyAudioCoreSingleWorker.fr.md) remplace le constat de double IPC ci-dessous pour le chemin natif Windows. Ce rapport conserve les résultats de sa révision d’origine.

## Résultat validé

Application, worker et tests reconstruits ; **123/123 tests passent**. Les quatre formats de plugins et les cinq fenêtres natives testées fonctionnent. Le corpus DSP de 98 cas est identique bit à bit au précédent moteur Core. **Aucun nouveau comparatif de performances ASIO n’est retenu** : des compilations et une session Unreal extérieures ont perturbé les fenêtres ; les données brutes sont conservées, sans présenter leur différence comme une régression ou un gain du SDK.

## Versions et changements

- CyAudioCore `4e5710999acbd88ab3d1ce87f4101ec73abe54b6`.
- CyDSP `0d67c6a60749ef241a87bc3592fa63c61998a5e2` (0.3.2).
- CyUI reste au snapshot `7a793e9` pour isoler la mise à jour audio.
- Snapshots Git propres dans `output/cyaudio-032/2026-11-15/sdk`, archives et SHA-256 enregistrés. Reconstruction complète dans `build/cyaudio-032` : aucun mélange d’anciens objets avec les nouveaux headers/layout MatrixConvolver.
- Pins de dépendances et garde CMake actualisés. Aucun algorithme musical ou réglage de qualité remplacé pour améliorer les chiffres.

CyAudioCore impose désormais un chemin de worker et l’isolation des fournisseurs. CyDaw fournit le chemin absolu du même exécutable avec une nouvelle entrée `--cydsp-worker`. Le pont IPC existant utilise `sameCycle` : soumission d’un ticket, récupération non bloquante dans la boucle événementielle, pas de réenvoi pendant la requête. Les commandes de contrôle et reconfigurations attendent la consommation du ticket. Le callback périphérique parent reste non bloquant. La commodité synchrone est réservée aux probes/tests hors ligne, avec délai borné.

**Deux processus enfants par plugin restent présents** : pont CyDaw puis worker SDK. Le SDK n’ajoute pas un second bloc fixe de pipeline ; le pipeline extérieur garde son bloc déclaré. Le plafond SDK est de 64 canaux par direction ; cela n’étend pas les huit canaux de l’IPC applicatif. La suppression du pont extérieur et sa migration vers un ordonnanceur dans le parent restent à réaliser ; aucun gain CPU de cette nouvelle architecture n’est affirmé.

## Contrôle numérique et coûts hors ligne

**98/98 cas bit-identiques**, 7,771,392 valeurs PCM et zéro allocation C++ observée sur le thread du corpus. Les 52 effets audio et les 16 instruments sont exercés, avec trois configurations par modèle de famille, tailles de blocs variables, expression, retarget et relâchement.

Cinq processus neufs par version, ordre alterné, échauffement exclu. Médianes en µs/1024 ; instruments à une voix. Avant = corpus final de la campagne complète Core précédente ; après = même intégration numérique reconstruite avec les SDK actuels. Cette mesure est distincte du pilote et de l’interface. Les cinq valeurs et plages de chaque cas figurent dans le JSON.

| Cas | Avant | Après | Variation |
| --- | ---: | ---: | ---: |
| fx:chorus | 51.04 | 51.21 | +0.3 % |
| fx:reverb-pro | 755.00 | 753.28 | -0.2 % |
| fx:pression | 87.46 | 88.42 | +1.1 % |
| fx:fournaise | 210.01 | 212.04 | +1.0 % |
| fx:velours | 104.97 | 102.13 | -2.7 % |
| fx:halo | 401.51 | 402.44 | +0.2 % |
| fx:empreinte | 106.02 | 102.43 | -3.4 % |
| fx:givre | 152.40 | 153.64 | +0.8 % |
| synth:ambre:0 | 75.00 | 75.29 | +0.4 % |
| synth:derive:2 | 276.86 | 277.14 | +0.1 % |
| synth:bronze:2 | 88.78 | 89.28 | +0.6 % |
| synth:jade:0 | 29.26 | 29.37 | +0.4 % |

Ces résultats n’établissent pas un gain général de CyDaw. Les variations proches de zéro ne sont pas significatives à elles seules ; ce microbanc ne fournit pas d’intervalle de confiance. Les surcoûts historiques ReverbPro/Halo et oscillateurs ne sont pas corrigés par cette mise à jour.

## Vérifications fonctionnelles

- Build complet Release x64 et graphe de 41 projets de production sans includes ni bibliothèques JUCE.
- Suite native : 123/123 en 502,85 s, incluant Cinéma, Mix, Fonderie, instruments, effets, graphes, fichiers et services.
- Fournisseur sans chemin refusé sans remplacer le fournisseur précédent ; instances réellement isolées.
- JSFX : tempo, automation à offset exact, sauvegarde/restauration et bloc variable de 17 frames, sans bloc de pipeline SDK ajouté.
- Fournisseurs et workers VST3/CLAP/LV2 : chargement, audio, états et fermeture sur fixtures originales conservées. Les fixtures ne prouvent pas la compatibilité de tous les plugins tiers.
- IPC applicatif : audio traité, absence de fuite du signal sec avant la première réponse, automation, état, éditeur et fermeture ; sélection et restauration des presets LV2 également revérifiées.
- Cinq fenêtres VST3 : Prisme, Jade, Kick, ReverbPro et chaîne, dimensions natives, image réellement dessinée et fermeture autonome.
- Huit VST3 simultanés sur Fireface ASIO 48 kHz / 1024, zéro entrée, sorties silencieuses : seize workers observés, transport et signal actifs, latence déclarée d’un bloc extérieur, tous les workers fermés après remise à zéro ; compteurs de blocs manqués nuls sur ce court essai. C’est une vérification fonctionnelle sous charge extérieure, pas un benchmark de performance ou une preuve d’absence d’xruns.

Les premiers logs de chargement refusé sont conservés : la borne de canaux initialement configurée à 256 a été corrigée à 64 avant les contrôles finaux. L’assertion du test n’a pas été assouplie.

## Campagnes ASIO perturbées et conservées

Les mêmes scènes que la campagne précédente ont été lancées avec JUCE, Core précédent et Core actuel : quatre familles d’instruments, mélange audio/MIDI/effets/bus, effets variés, 32 pistes sèches, 16 compresseurs et 16 chaînes Pro. Les contrôles de transport, topologies et signal réussissent pour les scènes achevées. Le dessin UI est supprimé seulement dans les copies QA ; une sonde mesure la racine du rendu. 100 % CPU représente un cœur logique.

Les compteurs système ont révélé des pointes extérieures de 12 à 16 cœurs logiques. Un inventaire CPU en lecture seule a identifié plusieurs `cl.exe` et `UnrealEditor`, sans les interrompre. Le passage initial a été arrêté via `quit` sur notre seul port de contrôle. Une seconde tentative avec contrôles de calme et reprises a aussi été interrompue quand les compilations ont repris. **Aucune médiane comparative ASIO de cette campagne n’est publiée comme résultat validé.** Les fenêtres terminées, coûts par thread, compteurs système et empreintes restent disponibles pour audit.

Le script de reprise attend deux intervalles avec charge extérieure ≤ 150 % d’un cœur avant chaque tentative, et rejette une fenêtre dépassant 200 %. Il conserve les tentatives et échoue après trois refus. Ce sont des critères explicites de bruit résiduel, pas une preuve de machine totalement inactive. Le banc plugins/DJ à trois versions est préparé mais n’a pas été exécuté comme campagne de performance.

## Cibles restantes

- **CyDSP / oscillateurs** : `src/engine/SynthParts.h::PhaseWaveforms` appelle encore setFrequency/reset/next pour des phases externes. La 0.3.2 ne fournit pas l’évaluation stateless préparée demandée.
- **CyDSP / historiques** : `include/cydaw/engine/DspHistory.h` et `src/engine/ProEffects.cpp` restent sur l’historique générique, sans taps préparés à capacité puissance de deux. Coût ReverbPro/Halo toujours présent.
- **CyDSP / filtres et FIR courts** : pas d’API publique biquad par sample avec état explicite, flush sélectif ou FIR polyphase conservant les taps/16 frames historiques. Les besoins du [rapport précédent](CyDSPCompleteReuse.fr.md) restent applicables ; aucun retour à l’adaptateur mono d’une frame mesuré huit fois plus lent.
- **CyDaw / IPC** : retirer à terme le pont externe devenu redondant, avec intégration de l’ordonnanceur, UI, états, automation, transport, bus et compensation. Le contrat `sameCycle` ne réalise pas cet ordonnanceur à la place de l’application.
- Les optimisations ADAA, DynamicEqualizer et MatrixConvolver concernent leurs consommateurs SDK. Les topologies musicales spécifiques de CyDaw ne deviennent pas automatiquement ces processeurs. Les FFT et primitives déjà déléguées sont bien reconstruites.

Ces localisations reposent sur le code et les microbancs ; aucune nouvelle capture de piles CPU n’est annoncée. Pas de nouvelle validation Linux/macOS, écoute humaine ou tous presets/plugins. La composante continue Cinéma et l’échec CUE ASIO historiques restent ouverts, sans assertion masquée.

## Reproduction et preuves

MSVC 19.40 x64 Release, `/O2 /Ob2 /MD`, SDK `/fp:strict`, SIMD automatique, sans LTO. Les commandes et options effectives sont enregistrées dans le manifeste, cache CMake et `.tlog`. Les témoins précédents ne doivent pas être reconstruits avec de nouveaux headers.

```powershell
cmake --preset cyaudio-desktop -B build/cyaudio-032 `
  -DCYDAW_CYAUDIOCORE_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-032/2026-11-15/sdk/CyAudioCore `
  -DCYDAW_CYDSP_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-032/2026-11-15/sdk/CyDSP `
  -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyDAW_Project/output/cyui-audioviews/2026-11-14/CyUI `
  -DCYUI_IMGUI_DIR=D:/_Project/CyImgui_Interface/third_party/imgui -DCYDAW_ENABLE_ASIO=ON
cmake --build build/cyaudio-032 --config Release --parallel 6
ctest --test-dir build/cyaudio-032 -C Release --output-on-failure
python tests/cydsp_complete_evidence.py --output <nouveau>/numerical --before output/cydsp-complete/2026-11-14/accepted-artifacts/corpus-thread-local.exe --after build/cyaudio-032/Release/cydaw_full_dsp_reuse_tests.exe --rounds 5
python tests/cyaudio_current_pair_build.py --output <nouveau>/realtime --current-build build/cyaudio-032
python tests/cydsp_complete_provenance.py --manifest <nouveau>/realtime/build-manifest.json --previous output/cydsp-complete/2026-11-14/realtime-accepted/build-manifest.json --current-build build/cyaudio-032 --sdk-manifest output/cyaudio-032/2026-11-15/sdk-manifest.json
python tests/cyaudio_sdk_quiet_pair.py --output <nouveau>/realtime --rounds 3 --seconds 5
# Copier le manifeste vers un nouveau dossier plugins avant ce banc :
python tests/cyaudio_sdk_plugin_pair.py --output <nouveau>/plugins --rounds 3 --seconds 5
python tests/cyaudio_sdk_update_report.py --campaign output/cyaudio-032/2026-11-15
```

Les commandes ASIO supposent la même autorisation matérielle et un pilote Fireface configuré à 1024 frames. Ne pas mesurer pendant une compilation, une autre campagne ou une charge CPU importante.

[Données, empreintes, répétitions et traces non retenues](reports/CyAudioCore032Update.json). Les PCM, binaires, topologies et logs complets restent dans `output/cyaudio-032/2026-11-15/`.
