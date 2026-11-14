# Réutilisation numérique CyDSP dans CyDaw

Campagne du 14 novembre 2026, branche `CyDSP-CORE`, base applicative `8ebfc00`.
Cette campagne remplace des calculs numériques applicatifs ; elle ne se limite
pas aux services audio auparavant fournis par JUCE.

## Dépendances réellement reconstruites

- CyAudioCore : `1a68279dfa9bf228bfc6ed4c6c11dd4952c1a075`.
- CyDSP : `ea56508ad3e26d790e491140b7f18a95e4411d5d` (0.2.0), fourni au build par CyAudioCore.

Les sources propres des deux dépôts ont été archivées avec Git dans
`output/cydsp-numerical/2026-11-14/sdk/`. Le nouveau répertoire
`build/cydsp-numerical` reconstruit les bibliothèques, le desktop et son worker,
avec leurs nouveaux headers. Aucune réédition de lien contre les anciennes
archives numériques n'est utilisée pour ce build. Les deux dépôts voisins sont
restés en lecture seule. Les pins de l'action de dépendances sont actualisés ;
aucune action GitHub n'est déclenchée par cette campagne locale.

## Calculs délégués

| Chemin applicatif | Réutilisation effective |
| --- | --- |
| Compresseur de base (`core.compressor`) | `cydsp::Dynamics<float>` : détection, enveloppe, courbe de compression, application du gain et minimum du gain appliqué. |
| Convolution des réponses impulsionnelles | `VectorOps::dot` pour les 128 premiers taps sans latence, `complexMultiplyAccumulate` pour les partitions spectrales, avec le FFT CyDSP déjà utilisé. |
| Buffers audio applicatifs, possédés et empruntés | Table SIMD automatique partagée avec les buffers moteur, résolue avant le rendu ; leur ancienne table implicite restait scalaire. |
| Conception des filtres communs | Types normalisés et fonctions CyDSP pour cloches, coupes, notch et passe-bande. Cela concerne aussi ReverbPro et les effets utilisant `BiquadDesign.h`. |
| Égaliseur trois bandes | Conception des shelves et de la cloche par CyDSP. Le noyau récursif fusionné reste applicatif après comparaison de coût. |
| ReverbPro | Mélange orthogonal Hadamard des 16 lignes par `orthogonalMix`, conversions de gain et conception des coefficients communs. |

Le compresseur conserve ses paramètres publics, son attaque de 8 ms, son
relâchement de 90 ms, sa détection de crête liée, sa sidechain inaudible et son
indicateur de réduction maximale. L'adaptateur diffuse une sidechain mono, accepte
les canaux préparés directement, et réduit les autres configurations historiques
au maximum des crêtes. Les tables de pointeurs et les buffers de secours sont
préparés hors callback. Le nouveau getter `minimumAppliedGain()` évite de déduire
la réduction à partir du signal ou d'introduire un second détecteur.

La convolution conserve sa tête directe, ses partitions de 128 échantillons,
son ordonnancement et sa latence nulle. Les taps de tête sont inversés une fois
à la préparation pour employer un produit scalaire SIMD sur des données contiguës.
Les exports natifs reconstruits à partir du moteur partagent ces sources ; les
anciens plugins déjà exportés ne sont pas réécrits.

## Mesures et qualité audio

Les mesures sont **hors ligne**, sans périphérique, UI, workers ni I/O de fichier
dans la zone chronométrée. Elles mesurent le temps écoulé du traitement, en µs,
pas le CPU global de l'application ni des xruns ASIO. MSVC 19.40, Windows x64,
Release `/O2 /MD`, sans LTO, 48 kHz. CyDSP conserve ses options strictes et son
dispatch ISA ; aucun fast-math ni AVX global n'a été ajouté.

Les exécutables sont figés. Plusieurs processus neufs sont alternés ;
l'échauffement est exclu. Aucune compilation ou autre benchmark de cette campagne
ne tourne pendant les mesures. L'activité externe de la machine n'est pas
contrôlée : considérer les petites différences comme des résultats locaux, pas
comme une garantie de performance générale. Les bruts, les médianes et les
empreintes sont dans [le rapport JSON](reports/CyDSPNumericalReuse.json).

Médianes finales sur cinq processus par variante, 1024 frames :

| Charge | Avant (µs) | Après (µs) | Variation |
| --- | ---: | ---: | ---: |
| Compresseur de base | 17.30 | 10.76 | -37.8 % |
| Convolution, IR 4096 taps | 76.57 | 44.57 | -41.8 % |
| ReverbPro | 513.20 | 534.32 | +4.1 % |
| EQ trois bandes | 8.04 | 8.01 | -0.5 % |

Ces accélérations concernent uniquement le compresseur et la convolution.
ReverbPro reste plus coûteuse avec le mélange SDK retenu ; aucun gain global
du moteur ou du processus complet n’est déduit de ces micro-mesures.

Les références sont distinguées : le témoin initial utilise les sources
applicatives `8ebfc00` **avec les nouveaux SDK** ; le témoin convolution précède
uniquement ses substitutions vectorielles ; le témoin compresseur précède
uniquement sa migration Dynamics. Les gains annoncés concernent ces noyaux,
ils ne constituent pas une nouvelle comparaison globale JUCE/CyAudioCore.

Le corpus couvre ReverbPro mono/stéréo/4 canaux à 44,1/48/96 kHz, des blocs de
1/17/64/256/511/1024 frames, le gel/dégel, le ducking, des modifications de
paramètres, les blocs vides et reset. Les comparaisons comportent 5 873 728
échantillons d'effets, 32 768 de convolution et 262 144 de compresseur avec
sidechain à trois canaux. Le FIR est aussi vérifié contre une convolution directe
indépendante en double précision. Le compresseur est comparé à son enveloppe
historique, y compris l'indicateur de réduction.

Erreurs maximales : effets `1.49011612e-08`, convolution `5.96046448e-08`, compresseur `4.76837158e-07` (amplitude normalisée).

Les différences attendues viennent de l'ordre des réductions SIMD et de la
précision des enveloppes. Elles restent sous les seuils testés : les valeurs
exactes sont conservées dans le JSON. Les appels audio et l'automation sont
contrôlés sans allocation `operator new`, y compris les allocations alignées.

## Essais écartés et besoins précis pour CyDSP

Les substitutions ci-dessous ont été compilées et exécutées ; elles ne sont pas
des hypothèses. Leurs coûts comprennent l'intégration complète dans l'effet.
Les différences ne constituent pas un profil exclusif de chaque fonction SDK.

1. **Filtres FDN échantillon par échantillon** : trois `BiquadBank<double>` de
   16 voix appelées avec une frame passent d'environ 509 à 784 µs/1024 frames
   pour ReverbPro, soit environ **+54 %**. La comparaison PCM reste valide.
   Le chemin conserve donc ses états récursifs applicatifs. API concernée :
   `CyDSP/src/processors/BiquadBank.cpp:89`, empaquetage à la ligne 121.
   Besoin : une banque préparée avec appel échantillon/voix ou vue déjà validée,
   coefficients et dispatch résolus avant rendu, sans réempaquetage ni validation
   de layout à chaque échantillon. Ne pas désactiver les contrôles des API publiques.
2. **Filtres de tonalité/sortie par blocs et EQ** : la variante ReverbPro passe
   d'environ 509 à 550 µs ; le trois-bandes d'environ 8 à 13,55 µs/1024 frames.
   Les passages supplémentaires et appels SDK n'offrent pas de gain sur ce montage.
   `BiquadCascade.cpp:63` parcourt les sections en passes séparées. Besoin :
   cascade fusionnée et chemins stéréo préparés. La conception des coefficients
   CyDSP reste utilisée dans la version retenue.
3. **Interpolation polynomiale générique** : l'essai isolé de
   `evaluatePolynomial<double>` à chaque lecture Hermite augmente ReverbPro
   d'environ 8–10 % sur ce build. La boucle de taille dynamique et son intégration
   sont à examiner (`include/cydsp/math/DspMath.h:20`). Une surcharge de taille
   statique adaptée aux cubiques pourrait convenir ; ce gain reste à mesurer.
   La formule cubique spécialisée est conservée.
4. **Hadamard 16 lignes** : le mélange SDK est retenu, avec un surcoût local
   d'environ 4 % pour ReverbPro entier. Ce choix centralise la matrice et sa
   validation ; il n'est pas présenté comme une accélération. Examiner une
   spécialisation 16 lignes : `include/cydsp/processors/Diffusion.h:18` utilise
   un espace temporaire de 64 doubles et une API générique.
5. **Shelves résonants** : la conception applicative prend un Q jusqu'à 40,
   alors que `designShelfBiquad` prend une pente limitée à 1
   (`src/processors/BiquadDesign.cpp:92`). Convertir et borner ce paramètre
   changerait certaines réponses. Besoin d'une conception publique fondée sur Q.
6. **Délais et diffusion ReverbPro** : lecture Hermite arbitraire, retours
   récursifs, gains de diffusion automatisables et stockage float/calcul double
   restent applicatifs. `DelayLine`/`MultiTapDelay`/`Diffuser` n'exposent pas le
   même contrat complet. Leurs réverbes génériques ne reproduisent pas la topologie
   et les paramètres de ReverbPro. Aucun remplacement intégral par une autre
   réverbération n'est revendiqué.

Responsabilités : **CyDaw** pour les adaptateurs, le choix du SIMD et les boucles
encore locales ; **CyDSP** pour les noyaux et extensions ci-dessus ; **CyAudioCore**
pour les services/transport, reconstruits avec leur optimisation de suppression
d'effacements redondants. Cette campagne ne chiffre pas isolément le bénéfice de
cette optimisation de transport. Les synthèses spécifiques, détecteurs/styles Pro,
Halo, structures musicales et ordonnanceurs ne deviennent pas automatiquement des
algorithmes SDK ; leurs composants peuvent être migrés après comparaison équivalente.

## Reproduire et vérifier

```powershell
cmake --preset cyaudio-desktop -B build/cydsp-numerical `
  -DCYDAW_CYAUDIOCORE_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cydsp-numerical/2026-11-14/sdk/CyAudioCore `
  -DCYDAW_CYDSP_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cydsp-numerical/2026-11-14/sdk/CyDSP `
  -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyImgui_Interface
cmake --build build/cydsp-numerical --config Release --parallel 6
ctest --test-dir build/cydsp-numerical -C Release --output-on-failure
& build/cydsp-numerical/Release/cydaw_dsp_reuse_tests.exe --dump output/new.f32
python tests/measure_dsp_reuse.py `
  --variant before=output/cydsp-numerical/2026-11-14/compressor-before.exe `
  --variant after=build/cydsp-numerical/Release/cydaw_dsp_reuse_tests.exe `
  --rounds 5 --output output/new-dsp-bench.json
```

Les dépendances et Python sont décrits dans [l'intégration](CyAudioCoreIntegration.md).
Pour reconstruire un témoin sans les binaires locaux, utiliser un checkout isolé
`8ebfc00`, les mêmes SDK figés, y ajouter le seul harness `DspReuseTests.cpp` et sa
cible de test, puis construire en Release avec les mêmes options. Les variantes
expérimentales non retenues gardent leurs binaires et logs dans le dossier de
campagne ; elles ne sont pas des modes du produit.

Les conclusions restent limitées à Windows x64 et aux charges indiquées.
Pas de nouvelle certification Linux/macOS, de mesure de latence physique ou
validation d'écoute. La composante continue Cinéma → Studio et l'échec CUE ASIO
relevés dans les campagnes précédentes restent ouverts et ne sont pas masqués.
Les **121 tests natifs passent**, y compris Studio/Mix/Cinéma, services/hosts,
transport, routage et graphes. Le contrôle du graphe de build ne trouve pas de
dépendance JUCE. Le test numérique a aussi été recompilé et relancé après ajout
du garde d’allocations alignées. La suite complète a pris 474 secondes.
La compilation et le chargement de nouveaux exports VST3/CLAP/LV2 n’ont pas
été rejoués dans cette campagne.

Les résultats d’acceptation du 13 novembre restent historiques ; les contrôles
réalisés sur ce build sont recensés dans le JSON de cette campagne.
