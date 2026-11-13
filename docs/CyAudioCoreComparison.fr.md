# Comparaison JUCE / CyAudioCore — CyDaw Windows


Résultat mesuré : le remplacement est autonome et améliore plusieurs opérations de projet, mais le coût CPU en lecture est supérieur dans les charges testées. 36/37 scénarios fonctionnels réussis côté Core, contre 33/37 côté JUCE ; le CUE ASIO constitue l’échec commun.

Comparaison de deux états du produit, sur le même poste. Les chiffres ne permettent pas d’attribuer chaque écart à la seule bibliothèque : le port remplace aussi les sources, le stockage, les services et le worker.

## Versions et méthode

- JUCE : commit `cf4228d`, dernier état avant l’intégration DSP/audio. Copie figée, reconstruite ; aucun ancien exécutable intermédiaire réutilisé.
- CyAudioCore : commit `de66021`, migration complète. Les exécutables, leurs tailles et leurs SHA-256 sont enregistrés dans le [rapport JSON](reports/CyAudioCoreComparison.json).
- Windows x64, MSVC 2022, Release `/O2 /Ob2 /DNDEBUG`, LTO désactivé des deux côtés. Optimisation effective MaxSpeed vérifiée dans les deux applications et workers. Même CyUI et mêmes DLL FFmpeg. JUCE 8.0.15, ses sources inchangées.
- JUCE impose normalement `/Ox` via ses options recommandées : [la configuration QA différée](../tests/fixtures/juce-benchmark-optimization.cmake) la remplace par `/O2` pour les mesures contrôlées. Le premier jeu reste sous `output/juce-core-comparison/original-ox`. Le panel fonctionnel complet utilise les options de production initiales ; les mesures de performance sont rejouées après normalisation.
- Adaptation QA du profil de la référence : [préférences dans le profil isolé](../tests/fixtures/juce-benchmark-profile.patch). JUCE utilise les Known Folders Windows et ignore le simple remplacement d’APPDATA. Cette adaptation touche uniquement les chemins de préférences, aucun calcul audio.
- ASIO Fireface USB, 48 000 Hz, 1 024 échantillons, 0 entrée / 2 sorties ; réglages relus et vérifiés avant chaque session de performance.
- Trois processus neufs par version, ordre alterné JUCE/Core puis Core/JUCE puis JUCE/Core. Échauffement exclu ; même ordre de scénarios et mêmes fichiers synthétiques. Cache disque Windows conservé.
- CPU mesuré par GetProcessTimes pour l’application et ses descendants : **100 % = un cœur logique occupé**. Mémoire : maximum échantillonné de la mémoire privée engagée, somme hôte + workers ; ce n’est pas une mesure de RAM physique ni de VRAM.
- Mesures de lecture sur environ six secondes après stabilisation. Interface automatisée cachée 1 500 × 900 ; la latence API mesure le protocole de contrôle, pas les FPS d’une fenêtre visible.
- Médiane des trois sessions. Les minima/maxima et mesures individuelles restent dans le JSON. Les rendus/traitements sont répétés trois ou cinq fois par session, avec une première exécution d’échauffement écartée.

## CPU et mémoire

| Scénario | CPU JUCE | CPU Core | Écart CPU | Privé JUCE (MiB) | Privé Core (MiB) | Écart mémoire |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Session vide | 0,76 % | 1,03 % | +36,3 % | 296,6 | 249,8 | -15,8 % |
| 32 pistes audio | 9,11 % | 17,00 % | +86,7 % | 730,1 | 691,9 | -5,2 % |
| 16 pistes × compresseur/chorus/réverbération | 67,14 % | 145,01 % | +116,0 % | 597,7 | 645,4 | +8,0 % |
| 64 voix Ambre / 8 pistes | 20,31 % | 31,12 % | +53,2 % | 485,6 | 461,2 | -5,0 % |
| 8 pistes × même VST3 / 8 workers | 3,03 % | 15,06 % | +396,4 % | 510,0 | 552,1 | +8,3 % |
| Mix DJ, 4 platines + 8 VST3 au repos | 6,55 % | 18,62 % | +184,4 % | 611,2 | 674,8 | +10,4 % |
| Fonderie WAV + 8 VST3 au repos | 9,37 % | 18,97 % | +102,6 % | 669,0 | 741,2 | +10,8 % |
| Cinéma 720p24 + WAV + 8 VST3 au repos | 18,70 % | 27,81 % | +48,7 % | 783,3 | 866,8 | +10,7 % |

Un écart négatif indique une consommation inférieure. Les scénarios se succèdent dans le même processus, dans le même ordre : la mémoire inclut les caches conservés des étapes précédentes. Les huit VST3 restent chargés pendant DJ/Fonderie/Cinéma, même à l’arrêt : ces lignes décrivent un workflow avec projet conservé, pas des modes isolés. Le diagnostic séparé retire aussi les workers.

## Temps et réactivité

| Opération | JUCE (ms) | Core (ms) | Écart |
| --- | ---: | ---: | ---: |
| Démarrage → API prête | 2069,16 | 2019,77 | -2,4 % |
| Ouvrir projet 32 pistes | 1638,17 | 753,06 | -54,0 % |
| Charger 8 instances VST3 | 834,25 | 804,37 | -3,6 % |
| Exporter 3 s + 2 s de traîne, temps réel | 6041,74 | 5048,26 | -16,4 % |
| Synthèse hors ligne : accord Ambre, 2 s + 1 s de relâchement | 37,62 | 38,23 | +1,6 % |
| Restaurer/compiler/rendre chaîne FX, 3 s | 88,91 | 90,46 | +1,7 % |
| Sauvegarder projet 32 pistes | 182,21 | 133,89 | -26,5 % |
| Fermeture du processus | 578,18 | 484,84 | -16,1 % |
| Commande seek Cinéma + état | 0,98 | 2,56 | +160,1 % |
| Traitement Remove Hum, 3 s | 15,57 | 17,69 | +13,6 % |
| Traitement Tape Echo, 3 s | 83,76 | 88,65 | +5,8 % |
| Traitement Room Reverb, 3 s | 514,14 | 444,94 | -13,5 % |

| Latence API P95 par scénario | JUCE (ms) | Core (ms) |
| --- | ---: | ---: |
| Session vide | 9,66 | 15,77 |
| 32 pistes audio | 15,93 | 22,02 |
| 16 pistes × compresseur/chorus/réverbération | 22,19 | 22,07 |
| 64 voix Ambre / 8 pistes | 15,58 | 22,89 |
| 8 pistes × même VST3 / 8 workers | 1,42 | 2,80 |
| Mix DJ, 4 platines + 8 VST3 au repos | 1,49 | 3,51 |
| Fonderie WAV + 8 VST3 au repos | 2,51 | 11,82 |
| Cinéma 720p24 + WAV + 8 VST3 au repos | 2,45 | 17,78 |

Le P95 affiché est la médiane des P95 de chaque session. Le temps d’export temps réel comprend la durée jouée ; ce n’est pas un débit de rendu hors ligne.

## Même panel fonctionnel

37 scénarios identiques, mêmes scripts et assertions ; seul l’exécutable change. Les durées brutes des scripts incluent Python, attentes et exports temps réel : elles ne sont pas présentées comme un gain DSP.

| Scénario | JUCE | CyAudioCore |
| --- | --- | --- |
| `cydaw_help_entrypoints` | OK | OK | 3,64 | 3,22 |
| `cydaw2_cinema_layout_check` | Échec | OK | 7,54 | 7,85 |
| `cydaw2_cinema_codecs_check` | OK | OK | 12,14 | 8,89 |
| `cydaw2_mcp_check` | OK | OK | 17,84 | 17,82 |
| `cydaw2_sound_model_check` | OK | OK | 10,68 | 10,13 |
| `cydaw2_sampler_check` | OK | OK | 9,41 | 8,14 |
| `cydaw2_launcher_check` | OK | OK | 16,93 | 16,35 |
| `cydaw2_routing_check` | OK | OK | 13,27 | 11,97 |
| `cydaw2_banks_check` | OK | OK | 3,33 | 3,77 |
| `cydaw2_electro_check` | OK | OK | 4,71 | 4,06 |
| `cydaw2_sample_search_check` | OK | OK | 18,74 | 32,76 |
| `cydaw2_amps_check` | OK | OK | 3,91 | 3,47 |
| `cydaw2_editing_check` | OK | OK | 20,07 | 18,11 |
| `cydaw2_mixing_check` | OK | OK | 46,22 | 42,97 |
| `cydaw2_mix_effects_check` | OK | OK | 3,44 | 3,18 |
| `cydaw2_synths_check` | OK | OK | 15,47 | 13,78 |
| `cydaw2_creative_check` | OK | OK | 28,73 | 28,12 |
| `cydaw2_dj_check` | Échec | Échec | 27,54 | 27,44 |
| `cydaw2_foundry_check` | OK | OK | 5,19 | 4,50 |
| `cydaw2_processing_check` | OK | OK | 4,36 | 4,15 |
| `cydaw2_controllers_check` | OK | OK | 4,78 | 4,61 |
| `cydaw2_value_list_check` | OK | OK | 3,34 | 2,84 |
| `cydaw2_modulators_check` | OK | OK | 4,84 | 4,59 |
| `cydaw2_containers_check` | OK | OK | 6,01 | 5,49 |
| `cydaw2_instrument_containers_check` | OK | OK | 46,82 | 41,04 |
| `cydaw2_project_import_check` | OK | OK | 7,07 | 5,27 |
| `cydaw2_batterie_check` | OK | OK | 13,32 | 12,18 |
| `cydaw2_sfz_live_check` | OK | OK | 5,02 | 4,59 |
| `cydaw2_tempo_audition_check` | OK | OK | 12,07 | 25,84 |
| `cydaw2_bypass_check` | OK | OK | 15,72 | 15,41 |
| `cydaw2_knob_glide_check` | OK | OK | 19,20 | 18,96 |
| `cydaw2_voice_live_check` | OK | OK | 5,62 | 5,51 |
| `cydaw2_presets_check` | OK | OK | 4,32 | 4,17 |
| `cydaw2_transport_sync_check` | OK | OK | 8,83 | 7,19 |
| `cydaw2_transport_jack_check` | Échec | OK | 7,72 | 3,09 |
| `cydaw2_cinema_check` | OK | OK | 17,17 | 12,68 |
| `cydaw2_plugin_formats_check` | Échec | OK | 10,24 | 9,18 |

Échecs observés (assertions conservées) :

- JUCE, `cydaw2_cinema_layout_check` : Traceback (most recent call last):; AssertionError ; journal `output\juce-core-comparison\functional\JUCE\cydaw2_cinema_layout_check\test.log`.
- JUCE, `cydaw2_dj_check` : FAIL the cue button: held it plays, let go it is back on the cue point (8.22, cue 8.21) ; journal `output\juce-core-comparison\functional\JUCE\cydaw2_dj_check\test.log`.
- CyAudioCore, `cydaw2_dj_check` : FAIL the cue button: held it plays, let go it is back on the cue point (8.22, cue 8.21) ; journal `output\juce-core-comparison\functional\CyAudioCore\cydaw2_dj_check\test.log`.
- JUCE, `cydaw2_transport_jack_check` : Traceback (most recent call last):; AssertionError: timed out ; journal `output\juce-core-comparison\functional\JUCE\cydaw2_transport_jack_check\test.log`.
- JUCE, `cydaw2_plugin_formats_check` : Traceback (most recent call last):; AssertionError: (-12.2, -12.2, -12.2) ; journal `output\juce-core-comparison\functional\JUCE\cydaw2_plugin_formats_check\test.log`.

Les fonctionnalités optionnelles d’IA sémantique exigeant des modèles externes conservent les exclusions annoncées par leur test. Les banques sampler/Batterie installées sont lues en mode strict ; aucun téléchargement ni modification de la banque personnelle.

## Résultat audio

Comparaison des échantillons PCM décodés, sans exiger des en-têtes WAV identiques. Signaux d’entrée synthétiques identiques, stereo 48 kHz ; sorties conservées dans le dossier de preuves.

| Fichier / session | Dimensions identiques | Erreur maximale | RMS de l’erreur (dBFS) | Corrélation | PCM identique |
| --- | --- | ---: | ---: | ---: | --- |
| studio.wav / 1 | Oui | 0,060532928 | -26,9 | 0,903408645 | Non |
| Remove-Hum.wav / 1 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Tape-Echo.wav / 1 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Room-Reverb.wav / 1 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| studio.wav / 2 | Oui | 0,140336990 | -19,5 | -0,032637556 | Non |
| Remove-Hum.wav / 2 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Tape-Echo.wav / 2 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Room-Reverb.wav / 2 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| studio.wav / 3 | Oui | 0,055979252 | -26,4 | 0,980590328 | Non |
| Remove-Hum.wav / 3 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Tape-Echo.wav / 3 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |
| Room-Reverb.wav / 3 | Oui | 0,000000238 | -141,7 | 1,000000000 | Non |

Les niveaux et statistiques des synthèses et chaînes d’effets sont également conservés dans les mesures brutes. Une différence PCM n’est pas, à elle seule, une appréciation de qualité sonore.

## Diagnostics et priorités

Ces diagnostics complémentaires ont été exécutés avec les options de production initiales (JUCE /Ox, Core /O2). Ils localisent les symptômes ; les tableaux de performance principaux utilisent bien /O2 des deux côtés. Les valeurs CPU isolées ci-dessous sont exploratoires, une session par version.

### P1 — coût du chemin temps réel et du processus principal

Les rendus hors ligne de synthèse et chaîne FX sont proches, alors que les chemins en lecture divergent fortement. Cela appelle un profilage du chemin live, de ses buffers, contrôles et de l’IPC parent ; ces mesures ne démontrent pas que la bibliothèque numérique seule est responsable.

| Diagnostic CPU, 100 % = 1 cœur logique | JUCE | Core |
| --- | ---: | ---: |
| Session vide | 0,39 % | 0,78 % |
| 8 VST3 chargés, transport arrêté | 1,54 % | 14,20 % |
| 8 VST3 en lecture | 1,92 % | 17,71 % |
| 4 platines DJ après suppression des 8 plugins | 6,93 % | 16,83 % |

Dans le diagnostic Core avec huit plugins au repos, le processus principal représente 13,43 % de cœur, contre 0,77 % pour l’ensemble des workers. Le surcoût ne se situe donc pas principalement dans leurs calculs DSP. Le surcoût du Mix persiste aussi sans workers chargés.

### P1 commun — composante continue après Cinéma

Une session Studio neuve produit une traîne silencieuse. Après lecture/arrêt du même clip dans Cinéma puis retour à Studio et nouveau projet, une composante continue demeure dans le fichier exporté, dans les deux versions. L’entrée physique est désactivée ; le défaut est reproductible dans cette transition et n’est pas propre au port CyAudioCore.

| Export diagnostique | Moyennes JUCE gauche/droite | Moyennes Core gauche/droite | Pic dans la traîne JUCE / Core |
| --- | --- | --- | --- |
| Studio neuf | 0,0000004, 0,0000002 | 0,0000005, 0,0000003 | 0,0000000 / 0,0000000 |
| Après lecture/arrêt Cinéma | -0,0069364, 0,0184723 | -0,0913291, 0,0076092 | 0,0184721 / 0,0913296 |

Les fichiers du workflow complet conservent volontairement ce défaut dans la comparaison PCM brute : ils ne sont pas déclarés identiques. Pour diagnostic seulement, retirer la moyenne par canal ramène le résidu entre versions à quelques unités de quantification 24 bits. Cela n’efface pas le défaut de l’export original.

| Export Studio normalisé / session | Écart maximal après retrait diagnostique des moyennes | RMS du résidu (dBFS) |
| --- | ---: | ---: |
| 1 | 0,000000202 | -143,1 |
| 2 | 0,000000257 | -140,8 |
| 3 | 0,000000247 | -142,0 |

Les traitements hors ligne Remove Hum, Tape Echo et Room Reverb gardent les mêmes dimensions et ne diffèrent que d’environ deux unités de quantification 24 bits. Il faut corriger le comportement audio après changement de mode avant de considérer le workflow d’export comme propre.

### P2 — chargement initial de la bibliothèque de samples

Deux ralentissements du panel ont été répétés deux fois, dont une exécution profilée par version. Les médianes ci-dessous incluent la première exécution du panel ; les attentes font partie des scripts, ce ne sont pas des durées de calcul DSP.

| Scénario | Médiane JUCE (s) | Médiane Core (s) |
| --- | ---: | ---: |
| `cydaw2_sample_search_check` | 18,75 | 32,84 |
| `cydaw2_tempo_audition_check` | 12,15 | 25,84 |

Le profilage localise l’écart dans les attentes initiales de disponibilité de la bibliothèque : 62 évaluations contre 18 de la condition `loaded` dans la recherche, et 94 contre 28 de la condition de scan dans l’audition. Le cumul des requêtes reste inférieur à une seconde pour la recherche et proche de trois secondes pour l’audition. La fermeture reste inférieure à une seconde : elle n’explique pas les quelque 13–14 secondes supplémentaires. Examiner le réveil et la disponibilité asynchrones de la bibliothèque dans l’intégration.

### CUE commun et améliorations fonctionnelles

- Le même test DJ, sans changer son assertion de 10 ms, réussit sur les deux versions avec WASAPI 48 kHz / 480 échantillons, après avoir échoué en ASIO / 1 024. Les réglages effectifs du test complémentaire sont conservés. Le comportement/tolérance doit être examiné par rapport au quantum audio ; ce n’est pas une régression propre à Core.
- JACK sur session vide, reprise du suivi Cinéma et transmission du tempo JSFX passent avec Core et échouent sur la référence. Le scénario des formats s’arrête au test de tempo JSFX côté JUCE : ce seul échec ne signifie pas que ses autres formats sont absents.
- Les huit relectures critiques après normalisation /O2 sont conservées dans le JSON ; elles permettent de vérifier les résultats des quatre scénarios concernés avec les options alignées.

### Taille des exécutables

| Binaire | JUCE (MiB) | Core (MiB) |
| --- | ---: | ---: |
| Application | 21,06 | 19,29 |

La baisse de taille de l’application et de mémoire initiale ne compense pas automatiquement la hausse du CPU dans le workflow chargé. Les mesures et fichiers restent consultables pour préparer une optimisation séparée, sans avoir modifié le code audio pendant ce benchmark.

## Limites

- Trois sessions par produit, sur un seul poste. Variabilité système, gestion d’énergie et caches non neutralisés par un protocole de laboratoire ; les petits écarts doivent être interprétés avec les plages min/max.
- Sorties silencieuses : comparaison des buffers, fichiers et états, pas validation d’écoute. Pas de mesure analogique de latence, de durée maximale des callbacks, de xruns du pilote, de GPU ou de comportement fenêtre visible.
- Les `latencyMs` bruts ne constituent pas une mesure aller-retour : JUCE additionne encore une latence d’entrée même sans entrée active, tandis que Core rapporte zéro pour celle désactivée. Ne pas interpréter ces nombres comme un gain de latence physique.
- Le temps d’ouverture mesure la fin de l’état API `opening`, pas le préchargement de toutes les banques de sons. Le scénario de bibliothèque, mesuré séparément, reste plus lent côté Core.
- Résultats Windows uniquement. Le VST3 est le même binaire de test dans les huit workers ; ce n’est pas une matrice exhaustive de plugins tiers.

## Reproduire

Construire la référence figée `cf4228d` en appliquant le patch de profil QA, puis utiliser les deux exécutables Release et le même panel CTest. Les SDK JUCE/CyUI/FFmpeg, le build natif et les fixtures de plugins doivent déjà être disponibles ; adapter leurs chemins et celui des banques au poste. Le préparateur conserve sa configuration dans `output/juce-core-comparison/build-commands.json`. Les mesures et métadonnées de cette campagne sont conservées sur ce poste et synthétisées dans le JSON versionné.

```powershell
python tests/cyaudio_prepare_comparison.py
python tests/cyaudio_comparison.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison --panel output/juce-core-comparison/ctest-panel.json --banks D:/CyDAW/Banks --mode functional
python tests/cyaudio_comparison.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison/normalized-o2 --banks D:/CyDAW/Banks --mode performance
python tests/cyaudio_comparison_diagnostics.py --juce "build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe" --cyaudiocore "build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/juce-core-comparison --panel output/juce-core-comparison/ctest-panel.json --banks D:/CyDAW/Banks
python tests/cyaudio_comparison_report.py --input output/juce-core-comparison --performance output/juce-core-comparison/normalized-o2 --output docs/CyAudioCoreComparison.fr.md
```

Profils, journaux, fichiers audio et mesures brutes restent sous `output/juce-core-comparison`. Aucune publication ni push effectué.
