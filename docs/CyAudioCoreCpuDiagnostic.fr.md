# Diagnostic CPU de CyDAW après migration CyAudioCore

> **Rectificatif du 14 novembre 2026 :** la [comparaison directe des interfaces](CyAudioCoreUiPair.fr.md) mesure les deux cadences et révèle des biais du banc précédent : disposition/onglets différents et ciblage des pistes par un nom dupliqué. Le cas historique `effects16` porte 48 effets en série sur la première de 16 pistes, et les 8 VST3 sont sur la première de 8 pistes. L’attribution 95 % décrit les anciens processus mesurés, sans démontrer une hausse du coût par image à affichage identique. Les chiffres et preuves ci-dessous restent conservés.

**Le principal surcoût localisé se trouve dans l’interface de CyDAW.** Pour 16 chaînes FX, la médiane ASIO du parent passe de 71.23 % à 151.51 % d’un cœur.

Sur le banc null et le même binaire instrumenté, limiter seulement le dessin à 30 Hz fait passer cette charge de 155.99 % à 80.34 % ; désactiver le dessin la ramène à 49.55 %. Ce contrôle conserve les appels audio et les plugins ; leurs états et fréquences sont enregistrés.

Ces chiffres globaux ne démontrent donc pas une régression des kernels CyDSP ou de CyAudioCore. Le choix scalaire de SampleBuffer est bien une autre piste mesurée, avec une portée distincte.

Campagne du 13 novembre 2026. Mesures exécutées ; aucun traitement audio de production modifié. Les dépendances en cours de travail ont été consultées en lecture seule. Les binaires et archives de la comparaison précédente ont été réutilisés avec leurs empreintes vérifiées.

## Résultats CPU

CPU mesuré par les API Windows : **100 % = un cœur logique**. Parent et workers séparés. Chaque tableau indique la médiane, puis la plage min–max pour le parent. Les répétitions, threads, états et réglages réels figurent dans le [rapport JSON](reports/CyAudioCoreCpuDiagnostic.json).

### null

| Version | Cas | Sessions | Parent % | Min–max % | Workers % |
| --- | --- | ---: | ---: | ---: | ---: |
| native-instrumented | audio32 | 3 | 18.59 | 18.46–23.85 | 0.00 |
| native-instrumented | dj4 | 3 | 16.58 | 14.35–17.06 | 0.00 |
| native-instrumented | effects16 | 3 | 155.99 | 151.96–158.93 | 0.00 |
| native-instrumented | idle | 3 | 0.19 | 0.00–4.25 | 0.00 |
| native-instrumented | idle8 | 3 | 14.94 | 13.00–18.42 | 0.19 |
| native-instrumented | playing8 | 3 | 13.38 | 12.78–21.18 | 0.19 |
| native-null | audio32 | 3 | 18.05 | 16.57–22.84 | 0.00 |
| native-null | dj4 | 3 | 15.52 | 14.73–20.38 | 0.00 |
| native-null | effects16 | 3 | 152.87 | 150.78–158.09 | 0.00 |
| native-null | idle | 3 | 0.58 | 0.19–0.97 | 0.00 |
| native-null | idle8 | 3 | 14.77 | 12.21–18.08 | 0.39 |
| native-null | playing8 | 3 | 13.57 | 13.33–22.29 | 0.39 |

### null-cap30

| Version | Cas | Sessions | Parent % | Min–max % | Workers % |
| --- | --- | ---: | ---: | ---: | ---: |
| native-instrumented | audio32 | 3 | 6.42 | 5.90–7.20 | 0.00 |
| native-instrumented | dj4 | 3 | 3.11 | 2.33–4.15 | 0.00 |
| native-instrumented | effects16 | 3 | 80.34 | 79.00–80.54 | 0.00 |
| native-instrumented | idle8 | 3 | 3.35 | 2.85–4.91 | 0.00 |

### null-off

| Version | Cas | Sessions | Parent % | Min–max % | Workers % |
| --- | --- | ---: | ---: | ---: | ---: |
| native-instrumented | audio32 | 3 | 3.36 | 2.33–4.40 | 0.00 |
| native-instrumented | dj4 | 3 | 0.00 | 0.00–0.26 | 0.00 |
| native-instrumented | effects16 | 3 | 49.55 | 49.41–50.14 | 0.00 |
| native-instrumented | idle8 | 3 | 0.00 | 0.00–0.26 | 0.00 |

### asio

| Version | Cas | Sessions | Parent % | Min–max % | Workers % |
| --- | --- | ---: | ---: | ---: | ---: |
| JUCE | audio32 | 3 | 8.50 | 8.00–9.75 | 0.00 |
| JUCE | dj4 | 3 | 6.45 | 5.43–7.01 | 0.00 |
| JUCE | effects16 | 3 | 71.23 | 69.11–71.24 | 0.00 |
| JUCE | idle8 | 3 | 2.59 | 1.55–2.85 | 0.00 |
| native | audio32 | 3 | 18.56 | 17.21–20.07 | 0.00 |
| native | dj4 | 3 | 16.81 | 16.76–18.86 | 0.00 |
| native | effects16 | 3 | 151.51 | 148.90–154.64 | 0.00 |
| native | idle8 | 3 | 14.76 | 14.73–18.34 | 1.03 |

## Effet observé de l’instrumentation

Comparaison null avec et sans scopes. Les intervalles se recouvrent et le poste reste soumis à son activité système : ce tableau donne l’écart observé, pas une calibration isolée de chaque scope. Les conclusions ASIO principales utilisent les exécutables non instrumentés. Les expériences cap30/off comparent le même binaire instrumenté.

| Cas | Natif sans scopes % | Avec scopes % | Écart en points |
| --- | ---: | ---: | ---: |
| idle8 | 14.77 | 14.94 | +0.17 |
| audio32 | 18.05 | 18.59 | +0.54 |
| effects16 | 152.87 | 155.99 | +3.13 |
| dj4 | 15.52 | 16.58 | +1.06 |

## Contrôle des cycles et attribution du surcoût FX

Dans le passage complémentaire avec identification des fenêtres cachées, **94.9 % de l’écart de cycles du parent** est localisé sur ses threads porteurs de fenêtres. Un passage par produit : c’est une confirmation de localisation, pas une nouvelle médiane de performance. Les cycles sont rapportés à la durée de mesure, sans conversion par une fréquence CPU supposée.

| Version | Parent Gcycles/s | Threads fenêtre Gcycles/s | Thread hors UI dominant Gcycles/s |
| --- | ---: | ---: | ---: |
| JUCE | 3.526 | 0.916 | 2.471 |
| Native | 7.456 | 4.645 | 2.509 |

## Localisation par fonction dans le binaire QA

Les compteurs mesurent le **temps écoulé** inclusif/exclusif sur chaque thread. Ils incluent donc une éventuelle désactivation du thread ou attente. Le tableau regroupe par nom les scopes exécutés dans chaque session (threads/surcharges précisés dans le JSON), puis prend les médianes entre sessions. Les durées ne sont pas des piles CPU échantillonnées, ne doivent pas être additionnées entre niveaux imbriqués et ne remplacent pas les compteurs CPU par thread. « Exclusif » exclut uniquement les autres scopes instrumentés, pas tous les appels enfants.

Un observateur hors callback exporte les compteurs toutes les 500 ms. Les fenêtres retenues sont à l’intérieur des mesures et leur durée réelle est conservée. Inscription des threads et compteurs à capacité fixe, atomiques lock-free vérifiés à la compilation, aucune allocation/verrou/attente/écriture de fichier ajoutée au callback.

### null

| Cas | Fonction | Appels mesurés médians | Appels/s médians | Temps écoulé exclusif % | Inclusif moyen µs/appel |
| --- | --- | ---: | ---: | ---: | ---: |
| audio32 | `DevicesView::draw` | 1043 | 138.9 | 9.740 | 706.62 |
| audio32 | `ArrangementView::draw` | 1043 | 138.9 | 4.860 | 353.32 |
| audio32 | `ReadAheadCache::fillOne` | 375461 | 49997.0 | 4.278 | 0.86 |
| audio32 | `AppUi::drawWindows` | 1043 | 138.9 | 1.970 | 1201.72 |
| audio32 | `AudioTransportSource::getNextAudioBlock` | 11616 | 1547.2 | 1.005 | 7.55 |
| audio32 | `clearAudio` | 89056 | 11862.2 | 0.863 | 0.73 |
| audio32 | `GainPanAudioSource::publishAnalysisFrame` | 11616 | 1547.2 | 0.836 | 5.40 |
| audio32 | `TrackMixer::getNextAudioBlock` | 352 | 46.9 | 0.835 | 1053.60 |
| dj4 | `DevicesView::draw` | 1043 | 138.9 | 10.488 | 761.70 |
| dj4 | `DjView::draw` | 1043 | 138.9 | 6.284 | 1231.27 |
| dj4 | `DjEngine::getNextAudioBlock` | 704 | 93.7 | 1.866 | 261.42 |
| dj4 | `UI::D3Ddraw` | 1043 | 138.9 | 0.824 | 59.32 |
| dj4 | `UI::Present` | 1043 | 138.9 | 0.735 | 52.94 |
| dj4 | `ImGuiHost::frame` | 1043 | 138.9 | 0.733 | 1501.61 |
| dj4 | `AppUi::frame` | 1043 | 138.9 | 0.622 | 1337.54 |
| dj4 | `AppUi::drawTransportBar` | 1044 | 139.0 | 0.608 | 43.80 |
| effects16 | `ReverbPro::process` | 5632 | 750.1 | 44.940 | 599.36 |
| effects16 | `DevicesView::draw` | 690 | 91.9 | 37.229 | 7784.24 |
| effects16 | `DevicesView::drawCard` | 2760 | 367.6 | 34.279 | 932.24 |
| effects16 | `AppUi::drawWindows` | 690 | 91.9 | 20.047 | 10322.78 |
| effects16 | `CompressorPro::process` | 5632 | 750.1 | 4.356 | 58.22 |
| effects16 | `ArrangementView::draw` | 690 | 91.9 | 3.107 | 345.51 |
| effects16 | `ReadAheadCache::fillOne` | 198533 | 26445.8 | 2.869 | 1.02 |
| effects16 | `ModulatedDelay::process` | 5632 | 750.1 | 2.712 | 36.27 |
| idle | `AppUi::drawWindows` | 1042 | 138.8 | 0.861 | 136.22 |
| idle | `AppUi::frame` | 1042 | 138.8 | 0.805 | 252.26 |
| idle | `ImGuiHost::frame` | 1042 | 138.8 | 0.712 | 369.91 |
| idle | `UI::Present` | 1042 | 138.8 | 0.614 | 44.54 |
| idle | `AppUi::drawTransportBar` | 1042 | 138.8 | 0.608 | 44.41 |
| idle | `ArrangementView::draw` | 1042 | 138.8 | 0.546 | 39.23 |
| idle | `DevicesView::draw` | 1042 | 138.8 | 0.466 | 34.13 |
| idle | `UI::D3Ddraw` | 1042 | 138.8 | 0.230 | 16.78 |
| idle8 | `DevicesView::draw` | 1044 | 139.0 | 9.888 | 735.05 |
| idle8 | `ArrangementView::draw` | 1045 | 139.2 | 4.204 | 309.72 |
| idle8 | `AppUi::drawWindows` | 1044 | 139.0 | 1.933 | 1189.05 |
| idle8 | `ImGuiHost::frame` | 1044 | 139.0 | 1.296 | 1480.47 |
| idle8 | `AppUi::frame` | 1044 | 139.0 | 0.778 | 1301.31 |
| idle8 | `ReadAheadCache::fillOne` | 91224 | 12147.3 | 0.740 | 0.61 |
| idle8 | `UI::Present` | 1044 | 139.0 | 0.634 | 46.81 |
| idle8 | `AppUi::drawTransportBar` | 1045 | 139.2 | 0.551 | 41.91 |
| playing8 | `DevicesView::draw` | 1043 | 138.9 | 9.542 | 692.30 |
| playing8 | `ArrangementView::draw` | 1043 | 138.9 | 4.233 | 305.67 |
| playing8 | `AppUi::drawWindows` | 1043 | 138.9 | 1.825 | 1129.36 |
| playing8 | `ImGuiHost::frame` | 1043 | 138.9 | 1.314 | 1395.27 |
| playing8 | `ReadAheadCache::fillOne` | 95542 | 12726.2 | 1.155 | 0.91 |
| playing8 | `AppUi::frame` | 1043 | 138.9 | 0.699 | 1228.62 |
| playing8 | `UI::Present` | 1043 | 138.9 | 0.557 | 40.11 |
| playing8 | `UI::D3Ddraw` | 1043 | 138.9 | 0.548 | 40.32 |

### null-cap30

| Cas | Fonction | Appels mesurés médians | Appels/s médians | Temps écoulé exclusif % | Inclusif moyen µs/appel |
| --- | --- | ---: | ---: | ---: | ---: |
| audio32 | `ReadAheadCache::fillOne` | 275040 | 49814.4 | 4.218 | 0.85 |
| audio32 | `DevicesView::draw` | 163 | 29.4 | 2.476 | 842.16 |
| audio32 | `ArrangementView::draw` | 163 | 29.4 | 1.035 | 353.84 |
| audio32 | `GainPanAudioSource::publishAnalysisFrame` | 8514 | 1546.8 | 0.842 | 5.44 |
| audio32 | `AudioTransportSource::getNextAudioBlock` | 8515 | 1546.8 | 0.835 | 6.46 |
| audio32 | `TrackMixer::getNextAudioBlock` | 258 | 46.9 | 0.816 | 949.21 |
| audio32 | `clearAudio` | 65288 | 11858.7 | 0.683 | 0.58 |
| audio32 | `AppUi::drawWindows` | 163 | 29.4 | 0.550 | 1381.88 |
| dj4 | `DevicesView::draw` | 162 | 29.4 | 2.329 | 797.68 |
| dj4 | `DjEngine::getNextAudioBlock` | 516 | 93.7 | 1.925 | 260.53 |
| dj4 | `DjView::draw` | 162 | 29.4 | 1.183 | 1210.10 |
| dj4 | `ImGuiHost::frame` | 162 | 29.4 | 0.213 | 1541.19 |
| dj4 | `ReadAheadCache::fillOne` | 29096 | 5283.9 | 0.201 | 0.38 |
| dj4 | `AppUi::drawTransportBar` | 162 | 29.4 | 0.185 | 62.94 |
| dj4 | `UI::Present` | 162 | 29.4 | 0.185 | 62.93 |
| dj4 | `AppUi::frame` | 162 | 29.4 | 0.181 | 1354.24 |
| effects16 | `ReverbPro::process` | 4136 | 751.2 | 41.859 | 556.93 |
| effects16 | `DevicesView::draw` | 162 | 29.4 | 12.581 | 7943.93 |
| effects16 | `DevicesView::drawCard` | 648 | 117.7 | 10.692 | 908.44 |
| effects16 | `AppUi::drawWindows` | 162 | 29.4 | 6.036 | 10329.23 |
| effects16 | `CompressorPro::process` | 4135 | 751.0 | 4.053 | 54.05 |
| effects16 | `ModulatedDelay::process` | 4135 | 751.0 | 2.548 | 33.99 |
| effects16 | `ReadAheadCache::fillOne` | 144624 | 26268.7 | 2.053 | 0.78 |
| effects16 | `ArrangementView::draw` | 162 | 29.4 | 1.007 | 344.18 |
| idle8 | `DevicesView::draw` | 162 | 29.4 | 2.458 | 840.58 |
| idle8 | `ArrangementView::draw` | 162 | 29.4 | 0.906 | 308.39 |
| idle8 | `AppUi::drawWindows` | 162 | 29.4 | 0.475 | 1303.06 |
| idle8 | `ReadAheadCache::fillOne` | 66888 | 12150.1 | 0.371 | 0.31 |
| idle8 | `ImGuiHost::frame` | 162 | 29.4 | 0.327 | 1656.76 |
| idle8 | `TrackMixer::getNextAudioBlock` | 258 | 46.9 | 0.287 | 299.29 |
| idle8 | `clearAudio` | 21930 | 3981.0 | 0.254 | 0.64 |
| idle8 | `AppUi::frame` | 162 | 29.4 | 0.225 | 1459.65 |

### null-off

| Cas | Fonction | Appels mesurés médians | Appels/s médians | Temps écoulé exclusif % | Inclusif moyen µs/appel |
| --- | --- | ---: | ---: | ---: | ---: |
| audio32 | `ReadAheadCache::fillOne` | 274944 | 49925.0 | 2.861 | 0.57 |
| audio32 | `AudioTransportSource::getNextAudioBlock` | 8514 | 1546.0 | 0.985 | 7.39 |
| audio32 | `clearAudio` | 65274 | 11852.6 | 0.873 | 0.74 |
| audio32 | `TrackMixer::getNextAudioBlock` | 258 | 46.8 | 0.821 | 1031.97 |
| audio32 | `GainPanAudioSource::publishAnalysisFrame` | 8514 | 1546.0 | 0.820 | 5.30 |
| audio32 | `AudioRenderPlan::process` | 9288 | 1686.5 | 0.542 | 8.62 |
| audio32 | `GainPanAudioSource::getNextAudioBlock` | 8514 | 1546.0 | 0.425 | 25.60 |
| audio32 | `EventLoop::dispatchPending` | 5728 | 1040.1 | 0.189 | 3.85 |
| dj4 | `DjEngine::getNextAudioBlock` | 516 | 93.7 | 1.969 | 269.31 |
| dj4 | `clearAudio` | 17544 | 3185.9 | 0.187 | 0.59 |
| dj4 | `ReadAheadCache::fillOne` | 29384 | 5334.4 | 0.186 | 0.35 |
| dj4 | `TrackMixer::getNextAudioBlock` | 516 | 93.7 | 0.180 | 330.61 |
| dj4 | `AudioRenderPlan::process` | 2322 | 421.7 | 0.131 | 9.23 |
| dj4 | `GainPanAudioSource::publishAnalysisFrame` | 1290 | 234.3 | 0.119 | 5.07 |
| dj4 | `EventLoop::dispatchPending` | 5720 | 1038.9 | 0.115 | 1.48 |
| dj4 | `GainPanAudioSource::getNextAudioBlock` | 1290 | 234.3 | 0.087 | 16.83 |
| effects16 | `ReverbPro::process` | 4138 | 750.9 | 41.578 | 554.63 |
| effects16 | `CompressorPro::process` | 4138 | 751.1 | 4.009 | 53.44 |
| effects16 | `ModulatedDelay::process` | 4138 | 751.1 | 2.537 | 33.91 |
| effects16 | `ReadAheadCache::fillOne` | 145632 | 26366.8 | 1.756 | 0.67 |
| effects16 | `clearAudio` | 94310 | 17112.0 | 0.859 | 0.50 |
| effects16 | `AudioRenderPlan::process` | 5161 | 937.3 | 0.768 | 532.38 |
| effects16 | `AudioTransportSource::getNextAudioBlock` | 4386 | 796.6 | 0.458 | 6.74 |
| effects16 | `TrackMixer::getNextAudioBlock` | 258 | 46.9 | 0.414 | 11006.67 |
| idle8 | `ReadAheadCache::fillOne` | 73032 | 12232.0 | 0.372 | 0.31 |
| idle8 | `TrackMixer::getNextAudioBlock` | 281 | 46.8 | 0.291 | 304.64 |
| idle8 | `clearAudio` | 23885 | 3977.5 | 0.254 | 0.64 |
| idle8 | `GainPanAudioSource::publishAnalysisFrame` | 2529 | 421.1 | 0.225 | 5.35 |
| idle8 | `AudioRenderPlan::process` | 3372 | 561.5 | 0.192 | 8.65 |
| idle8 | `GainPanAudioSource::getNextAudioBlock` | 2529 | 421.1 | 0.170 | 25.62 |
| idle8 | `AudioTransportSource::getNextAudioBlock` | 2529 | 421.1 | 0.159 | 3.79 |
| idle8 | `EventLoop::dispatchPending` | 6215 | 1035.3 | 0.143 | 2.76 |

## Contrôles complémentaires

Un passage ASIO instrumenté relie les scopes à leurs threads réels. Un second passage interroge les fenêtres sur le bureau caché CyDAW-automation (EnumDesktopWindows/GetWindowThreadProcessId) et relève les cycles des deux produits. Les listes UI vides du premier essai signifient que son énumération du seul bureau courant ne voyait pas les fenêtres cachées ; les scopes restent exploitables. Ce passage unique sert à localiser ; les chiffres de référence restent ceux des trois processus non instrumentés. Le contrôle null-cycle-check relève aussi les cycles CPU bruts : aucune conversion des cycles en secondes ou pourcentage CPU n’est faite.

| Contrôle / cas | Thread fenêtre UI | CPU parent % | CPU UI % | Cycles parent |
| --- | --- | ---: | ---: | ---: |
| asio-instrumented-check / native-instrumented / idle8 | [] | 17.10 | non identifié | 6334191880 |
| asio-instrumented-check / native-instrumented / audio32 | [] | 19.14 | non identifié | 8369720859 |
| asio-instrumented-check / native-instrumented / effects16 | [] | 151.68 | non identifié | 44768760590 |
| asio-instrumented-check / native-instrumented / dj4 | [] | 18.91 | non identifié | 6723459369 |
| asio-cycle-pair / JUCE / idle8 | [65664, 78532] | 3.37 | 1.30 | 1550816888 |
| asio-cycle-pair / JUCE / audio32 | [65664, 78532] | 8.29 | 3.37 | 3423950190 |
| asio-cycle-pair / JUCE / effects16 | [65664, 78532] | 73.96 | 19.84 | 21381602897 |
| asio-cycle-pair / JUCE / dj4 | [65664, 78532] | 3.11 | 1.55 | 2546783616 |
| asio-cycle-pair / native / idle8 | [74020] | 17.87 | 15.02 | 6805801770 |
| asio-cycle-pair / native / audio32 | [74020] | 20.17 | 13.45 | 10133074046 |
| asio-cycle-pair / native / effects16 | [74020] | 153.69 | 97.08 | 45478529617 |
| asio-cycle-pair / native / dj4 | [74020] | 19.66 | 9.57 | 6366513556 |
| null-cycle-recheck / native-instrumented / idle8 | [65184] | 0.00 | 0.00 | 1174824078 |
| null-cycle-recheck / native-instrumented / dj4 | [65184] | 0.00 | 0.00 | 1524859258 |

Le contrôle initial `null-cycle-check` a échoué à son assertion de lecture : le fichier DJ de 12 s était terminé après 2 s d’échauffement et 12 s de mesure. Il est exclu des comparaisons, conservé intégralement dans le JSON et rejoué sous `null-cycle-recheck` sur 6 s, sans modifier l’assertion.

## Opérations vectorielles — banc sans périphérique

Trois processus neufs, 300 000 appels par opération après 1 000 appels d’échauffement ; float, mono, 1 024 échantillons. Même archive CyDSP que le produit de référence. `VectorOps` construit par défaut : scalar (1). Sélection automatique disponible : AVX-512 (5). Le compilateur peut déjà vectoriser certains kernels scalaires ; les noms désignent les tables choisies par CyDSP.

| Opération | Scalar ns/appel | Automatique ns/appel | Rapport scalar/auto |
| --- | ---: | ---: | ---: |
| fill | 36.74 | 29.49 | 1.25× |
| scale | 55.19 | 34.06 | 1.62× |
| addScaled | 56.51 | 38.52 | 1.47× |
| dot | 569.97 | 40.19 | 14.18× |

La sélection scalaire est confirmée dans `include/cydaw/audio/SampleBuffer.h:16` et le constructeur de `VectorOps` de la révision figée. En revanche `include/cydaw/audio/Math.h` et `src/engine/AudioBuffer.cpp` sélectionnent automatic. Le facteur du microbenchmark ne s’applique pas au CPU total. Aucun backend de production n’a été changé.

## Interprétation et responsabilités

- **CyDAW, interface et cadence** : le thread identifié par `ImGuiHost::frame` concentre le surcoût des huit VST3 à l’arrêt. Les scopes distinguent `DevicesView::draw`, les cartes, l’arrangement et les appels D3D/Present. Les expériences cap30/off modifient seulement le dessin dans une copie QA ; elles ne sont pas une optimisation livrée. Comparer leurs valeurs aux sessions instrumentées normales, puis vérifier le thread audio.
- **CyDAW, calcul des effets** : `ReverbPro::process`, `CompressorPro::process` et `ModulatedDelay::process` localisent le coût audio de la charge 16 FX. `src/engine/ProEffects.cpp`, `EffectProcessors.cpp` et `AudioRenderPlan.cpp` sont identiques à la référence JUCE après normalisation BOM/fins de ligne. Ce sont des algorithmes applicatifs ; leur coût ne doit pas être attribué automatiquement aux processeurs numériques homonymes de CyDSP.
- **CyDAW, graphes et buffers** : `AudioRenderPlan::process` et `nodeDispatch` séparent l’ordonnancement des nœuds ; `clearAudio`, `copyAudio`, `addAudio`, transport et analyse des niveaux ont leurs compteurs. Les durées résiduelles du runtime incluent les appels non instrumentés ; elles ne sont pas déclarées coûts propres au SDK.
- **IPC** : huit workers et leur consommation sont contrôlés. Les compteurs du parent `IsolatedPluginHost::processBlock`, `pollWorkerHealth`, `refreshParameters`, `getState` et `sendCommand` restent consultables dans le JSON, y compris lorsqu’ils ne figurent pas parmi les huit fonctions les plus coûteuses.
- **CyDSP / sélection par l’hôte** : scalar contre automatic est une piste démontrée pour certaines opérations. Le premier correctif candidat est le choix du backend dans l’adaptateur CyDAW, préparé hors callback. Une modification des kernels du SDK n’est pas justifiée par ce seul résultat.
- **CyAudioCore** : les mesures isolent les frontières applicatives du transport et du callback mais ne décomposent pas tous les appels internes de la bibliothèque. Aucun défaut de kernel/transport Core n’est déclaré démontré par simple soustraction du CPU global. Croiser avec les mesures isolées de l’autre chat.

## Corrections ciblées à évaluer après cette campagne

1. Dans `src/v2/ImGuiHost.cpp` (poll/vblank) et `src/v2/Main.cpp` (boucle native), adapter la cadence au contexte visible/caché et aux besoins d’animation. Rechercher aussi ce qui maintient les réveils à haute fréquence au repos. Valider ensuite l’interactivité en fenêtre visible : les présents tests utilisent une fenêtre cachée.
2. Dans `src/v2/DevicesView.cpp` (`draw`, `drawCard`) et ses appels à `GraphDocument::editorNodes`, mettre en cache les descriptions stables et limiter les reconstructions par frame. Les scopes localisent ces ensembles, pas encore chaque allocation interne. Faire une campagne plus fine avant de réécrire un composant.
3. Optimiser séparément la réverbération applicative `src/engine/ProEffects.cpp` après profilage fin des lignes de délai/interpolations et en préservant le résultat audio. Ne pas remplacer cet algorithme par un autre sous prétexte qu’un SDK fournit une réverbération.
4. Mesurer le budget cumulé des opérations SampleBuffer et le gain d’une sélection automatique préparée avant lecture. La conversion backend est une expérience ultérieure ; contrôler les différences numériques et ne pas activer globalement fast-math/AVX.
5. Examiner le polling du cache applicatif dans `src/integration/BufferedAudioSource.cpp` : `fillOne` est appelé fréquemment même lorsque les pages sont prêtes. Les nombres d’appels sont mesurés ; le gain d’une autre politique de réveil ne l’est pas encore.

## Méthode, provenance et limites

- Fenêtre cachée et sortie silencieuse, 1 500 × 900. Profils, banques vides, collection DJ et fichiers synthétiques isolés sous le dossier de campagne ; aucune banque personnelle modifiée.
- Phase null : provider Core null, explicitement activé dans les seules copies QA. L’application réelle, ses sources et son IPC restent traversés. Ce n’est pas une mesure ASIO ; le provider et son ordonnanceur sont différents.
- Phase asio : les exécutables JUCE/Core d’origine gardent exactement leurs SHA-256 précédents. Fireface à 48 kHz / 1 024 frames, entrées désactivées, sortie silencieuse ; paramètres effectifs conservés. Aucun binaire de production recompilé ou remplacé.
- Les versions instrumentées recompilent une liste explicite de sources applicatives avec /O2 /Ob2 /MD /fp:precise /Zi puis réutilisent les archives et objets précédents. Aucun build ne dépend de la branche mouvante des SDK. Le manifeste contient chaque commande, scope et empreinte d’entrée.
- Plusieurs sessions neuves, ordre alterné entre binaires JUCE/Core et entre natif/instrumenté pour la phase null. Les contrôles UI cap30 puis off sont exécutés en séries successives de trois processus et ne sont pas intercalés : cette limite interdit de traiter leurs petits écarts comme des gains précis. Deux secondes d’échauffement par cas exclues. Les scénarios s’enchaînent dans chaque session ; les 32 pistes et 16 FX sont mesurés avant de charger les platines DJ. Après suppression, l’absence des huit workers est vérifiée.
- Aucun build/benchmark de cette campagne n’est exécuté en parallèle des mesures. Le poste n’est toutefois pas un laboratoire au repos garanti : l’activité système extérieure, conservée par fenêtre, peut être importante. Les grandes localisations sont exploitables ; les petits écarts ne permettent pas une conclusion précise.
- Les compteurs GetThreadTimes ont une granularité limitée : des threads exécutant des rafales courtes peuvent afficher zéro ou presque malgré des cycles bruts non nuls. Les zéros du contrôle sans dessin ne signifient donc pas coût nul ; ne pas en déduire un gain de 100 %. Les threads disparus pendant la fenêtre ne sont pas additionnés comme s’ils avaient été observés intégralement.
- ETW a échoué : xperf 0x3ec, WPR 0x80070514 (privilèges manquants). Aucune pile CPU n’a donc été collectée. Les scopes offrent des comptes et durées instrumentés, avec les limites explicitées ci-dessus. Aucun réglage de sécurité/antivirus changé.
- Les expériences de cadence coupent aussi le travail métier déclenché par le dessin ; elles démontrent le poids de ce chemin, pas le coût exclusif de la carte graphique.
- Les défauts précédents Cinéma/DC et CUE ASIO sont conservés dans [le rapport initial](CyAudioCoreComparison.fr.md) et ses preuves. Pas de correction, normalisation cachée ou assouplissement d’assertion dans cette campagne. Pas de nouvelle validation d’écoute, d’xruns ou de latence physique.

## Reproduire

Prérequis : objets/archives du build natif et référence JUCE /O2 déjà construits comme dans la comparaison initiale ; Python 3.12 et tests/requirements-native.txt. Le build QA réutilise ces artefacts locaux. Il ne constitue pas un remplacement du build de distribution.

```powershell
$qaPython = "D:/_Project/CyDAW_Project/output/cyaudio-qa-python/Scripts/python.exe"
& $qaPython tests/cyaudio_cpu_build.py --output output/cyaudio-cpu-diagnostic/2026-11-13
& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --rounds 3 --seconds 8
& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --ui cap30 --variants native-instrumented --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4
& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode null --ui off --variants native-instrumented --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4
& $qaPython tests/cyaudio_cpu_diagnostic.py --output output/cyaudio-cpu-diagnostic/2026-11-13 --mode asio --variants JUCE native --rounds 3 --seconds 6 --cases idle8 audio32 effects16 dj4
& $qaPython tests/cyaudio_cpu_micro.py --exe output/cyaudio-cpu-diagnostic/2026-11-13/micro/buffers.exe --output output/cyaudio-cpu-diagnostic/2026-11-13/micro/final-results.json
& $qaPython tests/cyaudio_cpu_verify.py --output output/cyaudio-cpu-diagnostic/2026-11-13
& $qaPython tests/cyaudio_cpu_report.py --input output/cyaudio-cpu-diagnostic/2026-11-13
```

Choisir un nouvel identifiant de campagne pour toute nouvelle version afin de préserver ces preuves. Résultats bruts : `output/cyaudio-cpu-diagnostic/2026-11-13`. Aucun push ni GitHub Actions déclenché.
