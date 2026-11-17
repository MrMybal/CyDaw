# CyDSP : instruments, effets et projets mixtes

Campagne locale du 14 novembre 2026, branche `CyDSP-CORE`, base `0556eb1`.
Les campagnes précédentes restent conservées. Cette passe étend la réutilisation
numérique de CyDSP ; elle ne change pas le pilote ASIO, le routage ni les formats
de projet. Aucun push ni workflow distant n’a été déclenché.

## Calculs effectivement délégués

| Ensemble | Réutilisation CyDSP et contrat conservé |
| --- | --- |
| FFT commune | `ComplexFft<float>` remplace la FFT applicative. Arrondi des tailles, aller non normalisé, retour normalisé et copie préparée conservés. Tous ses consommateurs bénéficient du changement, dont Givre, Empreinte, Velours et les tables de synthèse. |
| FFT de Prisme, restauration et recherche de patch | Plans `ComplexFft<double>` ou façade commune, préparés hors callback. Les plans des traitements hors ligne restent propres au thread de travail. |
| Ambre, Alliage, Argile, Dérive, Silex et composants analogiques des hybrides | `Oscillator<double>` pour l’évaluation des formes partagées. Phases, synchronisations, modulations et allocation des voix restent applicatives. Le PWM est la différence de deux scies SDK, équilibrée en continu ; polarité du triangle conservée. |
| Prisme | Sous-oscillateur scie/PWM et construction FFT des tables délégués. Banques, interpolation entre tables et modulations du synthé conservées. |
| Colosse, Mosaïque, Nébuleuse | FFT de fabrication des tables/textures et formes analogiques partagées. Le moteur hybride/granulaire et son ordonnanceur restent ceux du produit. |
| Saphir | Analyse des harmoniques par FFT double du SDK, remplaçant la DFT directe. Routage FM, choix des formes et enveloppes propres au synthé conservés. |
| Quartz | Fenêtre Kaiser du filtre demi-bande par `makeWindow`, sans changer ses 47 taps ni sa décimation. |
| Tirette | Historiques et interpolation Catmull–Rom SDK ; échantillons toujours arrondis en float avant stockage et calcul d’interpolation en double. |
| Bronze | Historiques, lecture Lagrange cubique de la boucle et taps linéaires SDK ; décalage avant/après écriture explicitement adapté. Résonateurs modaux et accord du modèle physique conservés. |
| Jade | Historiques Catmull–Rom et cinq filtres de préampli/tonalité par `Biquad<double>`, regroupés par blocs de contrôle de 16 frames. Tirages de bruit maintenant explicitement séquencés. |
| Filtres communs des synthés | Historique du peigne par `CircularHistory<double>`, notamment Opale, Onyx, Quartz, Saphir et les hybrides. Les filtres de caractère à rétroaction non linéaire restent spécifiques. |
| MixEffects, SuiteEffects et coupes d’Empreinte | Conception commune des coefficients SDK conservée. La tentative de récurrence par blocs mono d’une frame a été mesurée puis écartée ; la récurrence fusionnée reste applicative, voir les coûts ci-dessous. |
| Chorus, flanger et ping-pong | Historique multi-canal et interpolation SDK, avec conservation de l’ancienne quantification float de la position de lecture. |
| ReverbPro | Historiques fractionnaires et diffusion entière SDK, en plus du Hadamard, des conversions et des coefficients déjà délégués. La topologie FDN et ses réglages restent propres à CyDaw. |
| Amplis et effets suréchantillonnés | Un seul adaptateur de délai commun ; Kaiser SDK et produits scalaires SIMD pour les FIR polyphases. Filtre et latence de 16 frames conservés. |
| Alignement sinc et réponses impulsionnelles | Fenêtres Kaiser SDK. La convolution SIMD et le compresseur de base SDK de la passe précédente restent actifs. |
| Générateur audio simple | Sinus/triangle SDK. Les anciennes scies et impulsions brutes restent brutes ; les remplacer par une variante anti-aliasée modifierait leur contrat sonore. |
| Générateur de percussions, restauration et normalisation | Biquads SDK. La restauration traite des canaux complets ; la pondération K du loudness utilise des blocs de 1024, en conservant le calcul historique des fenêtres partielles et des pistes très courtes. |

Cette table distingue les **calculs délégués** des **algorithmes musicaux conservés**.
Utiliser CyDSP ne signifie pas remplacer un instrument par un autre synthé portant
un nom proche. Les effets MIDI, l’allocation des notes, les graphes, le séquençage,
les lois de modulation et les modèles physiques du produit restent des responsabilités
applicatives. Aucun pourcentage artificiel de « migration totale » n’est annoncé.

## Limites précises du SDK et code conservé

- `SynthParts.h::Envelope` est exponentielle, vise 1,3 à l’attaque et accepte des
  fractions de pas de contrôle, avec état édité directement par certains moteurs.
  `cydsp::Adsr` est linéaire. Un remplacement direct changerait attaques et durées.
- Les ladders, cascades saturées, voyelles et filtres « scream » ont leurs propres
  boucles, compensations et états. `TptFilter`/`LadderFilter` ne présentent pas le
  même contrat de coefficients/états ; une correspondance timbrale nécessite un
  composant paramétrable ou une extension du SDK, puis des tests dédiés.
- Le biquad applicatif conserve l’accès aux deux états pour les nettoyages ciblés
  de VoiceEffects, RhythmEffects et UtilityEffects. Le SDK expose `reset()` mais
  pas l’import/export ou le nettoyage individuel de ces états.
- Les biquads fusionnés des boucles ReverbPro/ampli et de l’égaliseur de base restent
  applicatifs. L’API publique du SDK ne propose que le traitement de blocs avec
  état privé ; l’adaptateur mono d’une frame ajoute validation et appels par sample.
  Les filtres à trois états des modèles d’amplis ne se réduisent pas directement
  à la même topologie de biquads sans changer les états/transitions.
- Les opérateurs FM, distorsions de phase, oscillateurs modaux, excitation de bruit,
  résonateurs mécaniques, interpolation de banques et mixages d’algorithmes restent
  spécifiques. Les composants génériques du SDK ne reproduisent pas automatiquement
  leurs réglages, accord, latence, modulation ni couleurs sonores.
- `Diffuser` ne fournit pas de changement de feedback préparé équivalent aux
  diffusions modulées du produit : les équations de rétroaction restent au niveau
  hôte, mais leurs historiques sont maintenant fournis par le SDK.

Extensions utiles pour CyDSP : évaluation de forme sans phase interne ni validation
répétée ; `processSample`/cascade préparée sans validation de layout à chaque sample ;
accès sûr aux états/flush sélectif ; accès préparé rapide aux historiques de capacité
puissance de deux ; enveloppes exponentielles à pas fractionnaire ; shelves définis
par Q résonant (le SDK définit une pente limitée à 1). Ces besoins concernent la
réutilisation fidèle et les coûts des adaptateurs, pas l’existence d’un backend audio.

Les dépôts voisins ont été consultés en lecture seule. Les versions compilées sont
les snapshots propres Core `1a68279dfa9bf228bfc6ed4c6c11dd4952c1a075`, DSP
`ea56508ad3e26d790e491140b7f18a95e4411d5d`, UI
`7a793e9281e78572818d85e9cf856d779a7792a1`. Les optimisations non committées observées
pendant la campagne dans le dépôt CyDSP voisin ne sont pas amalgamées à ces mesures.

## Vérifications numériques et protocole

`FullDspReuseTests.cpp` couvre les 52 effets audio du catalogue, trois configurations
pour chacun des 15 modèles de la famille, puis Prisme : **98 cas**. Les configurations
font varier les choix d’oscillateur/algorithme/filtre ; on teste aussi des blocs de
tailles variables (dont zéro pour les effets), changement de paramètres, retarget,
expression et relâchement. Les données de sortie et allocations de tous les cas
sont conservées. Il s’agit d’un ensemble étendu de scénarios, pas d’une preuve
exhaustive de toutes les combinaisons de paramètres.

`DspAdapterTests.cpp` compare indépendamment les formes d’onde aux formules
historiques, la FFT à une DFT directe, son aller/retour, la récurrence des filtres,
les historiques multi-canaux, leur bouclage, copie indépendante et remise à zéro.

Les mesures hors ligne excluent les 28 premiers blocs d’effets et 16 premiers blocs
d’instruments. Cinq processus neufs par version, ordre alterné ; construction des
ressources, démarrage, copie du rendu et écriture des fichiers hors zone chronométrée.
Les instruments y sont mesurés **à une voix**. Les projets temps réel exercent en
plus l’allocation de plusieurs voix et le mixage complet. Aucune compilation ni autre
banc de test de cette campagne ne tourne pendant les mesures de performance.

## Coûts localisés et décisions d’intégration

La première implémentation déléguait aussi les récurrences des biquads communs
au processeur de blocs du SDK, avec un bloc mono d’une frame. Le banc complet a
mesuré Pression à 85,0 → 736,4 µs/1024 et Tranche à 66,2 → 530,5 µs/1024. Cette
adaptation a été **retirée** du chemin commun ; ses résultats sont conservés dans
`output/cydsp-complete/2026-11-14/numerical/` et la copie QA expérimentale dans
`realtime-final/`. Ce sont des mesures d’une tentative écartée, pas du code final.

Jade utilise maintenant les cinq filtres SDK par **blocs de contrôle de 16 frames** :
calcul de l’excitation/DC, traitement coil/colour, saturation, traitement du tone
stack, puis chorus et sortie. Chaque état garde l’ordre temporel de ses propres
échantillons ; les coefficients restent constants dans le pas. Il n’y a pas de
boucle de rétroaction externe entre ces étapes. Ce regroupement évite de valider
le layout d’un bloc pour chaque sample. Le même regroupement n’est pas directement
possible dans les boucles de rétroaction ReverbPro et dans tous les effets fusionnés.

Références applicatives à examiner pour les optimisations suivantes :

- `src/engine/SynthParts.h::PhaseWaveforms` appelle le générateur SDK à phase imposée.
  Chaque scie fait `setFrequency`, `reset` et `next` ; le PWM nécessite deux scies.
  Dans le SDK, cela répète validation, division de fréquence et normalisation de
  phase alors que l’hôte possède déjà un incrément et une phase bornés. Demander
  une évaluation préparée/stateless, sans changer les formules PolyBLEP.
- `src/engine/ProEffects.cpp::FractionalDelay/Allpass` et
  `include/cydaw/engine/DspHistory.h` utilisent l’historique générique du SDK.
  Par frame, ReverbPro exécute 44 lectures cubiques, 8 lectures entières et 28 pushes
  (décompte du code pour une réverbération stéréo, pas compteurs de profileur).
  Les lectures cubiques impliquent quatre échantillons et l’indexation générique
  de `CircularHistory::integer` utilise un modulo de capacité. Les anciens anneaux
  en puissance de deux utilisaient un masque. Une API de taps préparés/rapides ou
  un chemin spécialisé puissance de deux est une cible concrète pour CyDSP.
- `src/engine/DspParts.h::Oversampler` appelle `VectorOps::dot` pour des FIR courts
  (environ 18 coefficients par phase montante). Le calcul est bien délégué au SDK,
  mais les appels et les validations peuvent coûter davantage que la petite boucle
  fusionnée antérieure. Mesurer un noyau FIR polyphase préparé, conservant les taps
  et les 16 frames de latence, plutôt que changer de filtre pour gagner un benchmark.
- `include/cydaw/engine/DspSampleBiquad.h` illustre la différence entre adaptation
  par sample et traitement par blocs. Une API publique de noyau préparé avec état
  explicite permettrait de remplacer les biquads fusionnés restants sans leur
  ajouter le coût d’un traitement de bloc complet à chaque échantillon.

Ces localisations proviennent des changements isolés et des timings par processeur.
Elles ne constituent pas une attribution par piles CPU de chaque instruction :
aucun nouveau profil d’échantillonnage de piles n’a été collecté ici. Les anciens
profils détaillés restent accessibles dans les rapports CPU précédents.

## Mesures hors ligne retenues

Médianes de cinq processus neufs, µs/1024 frames, mêmes scènes et réglages.
« Avant » désigne le moteur Core numérique précédent (`4f040dd`, archives conservées),
« après » le moteur de cette campagne. Une valeur positive indique un surcoût.
Ce tableau ne mesure ni l’interface ni le pilote. Les intervalles et cinq valeurs
individuelles de chacun des 98 cas sont dans le JSON associé.

| Effet / configuration | Avant (µs) | Après (µs) | Variation |
| --- | ---: | ---: | ---: |
| chorus | 32.75 | 50.77 | +55.0 % |
| reverb-pro | 499.36 | 747.40 | +49.7 % |
| halo | 268.57 | 398.11 | +48.2 % |
| pression | 85.03 | 87.86 | +3.3 % |
| tranche | 53.26 | 55.49 | +4.2 % |
| amp-sim | 831.69 | 907.92 | +9.2 % |
| cab-sim | 94.68 | 152.28 | +60.8 % |
| fournaise | 128.25 | 212.72 | +65.9 % |
| rotor | 149.40 | 207.65 | +39.0 % |
| empreinte | 142.21 | 103.83 | -27.0 % |
| givre | 192.94 | 152.67 | -20.9 % |
| velours | 174.62 | 103.75 | -40.6 % |

Le bénéfice de la FFT commune apparaît dans les effets spectraux. Les historiques
génériques, l’évaluation des oscillateurs à phase imposée et certains très petits
FIR ont en revanche un surcoût : la bibliothèque est effectivement utilisée, sans
que cela garantisse un gain CPU dans ces topologies. Les coûts de ReverbPro, Halo,
Dérive variante 2 et Bronze variante 2 doivent être traités explicitement avant de
présenter cette branche comme une optimisation générale.

| Instrument, réglages par défaut | Avant (µs) | Après (µs) | Variation |
| --- | ---: | ---: | ---: |
| ambre | 54.39 | 75.02 | +37.9 % |
| colosse | 68.69 | 68.48 | -0.3 % |
| mosaique | 75.52 | 88.50 | +17.2 % |
| nebuleuse | 68.04 | 68.38 | +0.5 % |
| opale | 65.60 | 65.38 | -0.3 % |
| onyx | 47.76 | 47.72 | -0.1 % |
| tirette | 44.94 | 69.31 | +54.2 % |
| alliage | 66.90 | 97.35 | +45.5 % |
| quartz | 159.56 | 160.34 | +0.5 % |
| argile | 46.21 | 56.47 | +22.2 % |
| saphir | 67.79 | 67.43 | -0.5 % |
| derive | 61.37 | 77.83 | +26.8 % |
| bronze | 32.34 | 58.82 | +81.9 % |
| silex | 0.06 | 0.06 | +6.2 % |
| jade | 20.72 | 29.05 | +40.2 % |
| prisme | 113.45 | 115.29 | +1.6 % |

Les percussions peuvent être déjà silencieuses dans une partie de la fenêtre
chaude ; les fractions de microseconde correspondantes ne mesurent pas leur
attaque active. Le scénario physique temps réel réamorce les notes chaque seconde
et complète cette mesure. Les variantes 1 et 2, conservées dans le JSON, font
apparaître d’autres chemins que les seuls presets par défaut.

### Écarts audio conservés

Sur **7 771 392 échantillons**, 82 des 98 cas sont bit à bit identiques.
Le compteur final est propre au thread appelant (`thread_local`), afin de ne pas
interférer avec les workers asynchrones. Les 98 rendus PCM sont identiques à ceux
du premier corpus de cette campagne après cette correction du compteur.
Aucune allocation `new` n’est observée dans les régions de traitement contrôlées,
et les 98 sorties restent finies et bornées. Le compteur ne prétend pas intercepter
un appel direct à `malloc` de toute bibliothèque tierce ni profiler ses threads.

Hors les deux cas bruités de Jade, l’écart maximal est celui de Givre :
`2,2415e-5`, RMS `7,3245e-7` (−122,70 dBFS). Le calcul spectral a changé de FFT ;
ce sont des différences numériques, pas une sortie déclarée bit-identique.

Jade 0/1 conserve des écarts maximaux de 0,01261 / 0,01398, RMS environ −65,9 / −65,7
dBFS. L’ancienne expression mélangeait trois appels à `random()` dont l’ordre
relatif était laissé au compilateur. Cette édition définit explicitement cet
ordre. Le contrôle Jade 2 sans ces bruits reste **bit à bit identique**, y compris
avec les filtres SDK regroupés en blocs. Les cas bruités restent dans les résultats ;
ils ne sont ni retirés, ni renommés en égalité numérique.

## Projets complets : JUCE, Core précédent, Core de cette campagne

**81 fenêtres mesurées**, neuf charges, trois processus neufs pour chacun des trois
états, ordre cyclique alterné. Deux secondes d’échauffement exclues, cinq secondes
par fenêtre. Même ASIO Fireface USB, 48 kHz / 1024 frames, zéro entrée, deux sorties
silencieuses, MIDI matériel désactivé. Le dessin est supprimé dans les trois copies
QA ; les commandes et services du logiciel restent actifs.

Les topologies enregistrées ont été normalisées et comparées entre les 81 fenêtres :
mêmes instruments, notes, clips, effets et sorties. La piste MIDI vide initiale est
retirée dans chaque version. Les percussions sont réamorcées chaque seconde ; les
autres notes restent tenues. Les groupes « 16 » programment quatre notes par
instrument ; les modes monophoniques et chokes de Silex peuvent réduire le nombre
réellement actif. Les phases aléatoires des voix ne sont pas forcées par le contrôleur
temps réel ; la comparaison PCM déterministe séparée ne repose pas sur ces sessions.

Le projet mixte contient huit fichiers audio synthétiques et huit instruments,
compresseur + chorus sur chaque piste, quatre pistes audio et les huit instruments
vers un bus à −6 dB portant ReverbPro, quatre autres pistes audio vers le master.
Le groupe d’effets variés couvre deux exemplaires de Givre, Empreinte, Velours,
Amp Sim, Écho bande, Chorus, Tranche et Multiband Pro.

Médianes des durées moyennes du rendu complet par bloc, en millisecondes :

| Charge | JUCE | Core avant | Core après | Après / avant |
| --- | ---: | ---: | ---: | ---: |
| 4 synthés analogiques, 16 notes | 1.126 | 1.128 | 1.445 | +28.1 % |
| 4 synthés tables/hybrides, 16 notes | 1.827 | 1.814 | 2.102 | +15.9 % |
| 4 synthés numériques, 16 notes | 1.587 | 1.584 | 1.585 | +0.1 % |
| 4 instruments physiques/percussifs | 1.513 | 1.519 | 2.114 | +39.1 % |
| 8 audio + 8 instruments, effets et bus | 4.024 | 4.078 | 5.036 | +23.5 % |
| 16 pistes, effets variés | 4.462 | 4.443 | 4.351 | -2.1 % |
| 32 pistes audio sèches | 0.868 | 0.844 | 0.854 | +1.2 % |
| 16 compresseurs de base | 0.770 | 0.714 | 0.705 | -1.3 % |
| 16 × Comp Pro + Chorus + ReverbPro | 10.985 | 11.460 | 15.827 | +38.1 % |

Le groupe numérique reste pratiquement inchangé. Les groupes analogique, tables et
physique montrent les surcoûts des nouveaux chemins par sample ; le mix augmente de
23,5 %. La chaîne Pro augmente de 38,1 % face au Core précédent et 44,1 % face à JUCE.
Les effets variés sont proches/légèrement plus rapides : les gains spectraux
compensent certains coûts ajoutés. Les pistes sèches restent proches ; les petits
écarts doivent être lus avec leur dispersion dans les résultats JSON.

Le budget nominal du bloc est 21,333 ms : les 15,827 ms des chaînes Pro consomment
74,2 % de ce budget en moyenne. Ce n’est **pas** une mesure des xruns ou une garantie
sur chaque échéance. La sonde mesure une durée écoulée inclusive, avec possibles
préemptions, et pas la latence physique de sortie.

| Charge | CPU parent JUCE / avant / après (% d’un cœur) | Mémoire privée JUCE / avant / après (MiB) |
| --- | ---: | ---: |
| 4 synthés analogiques, 16 notes | 5.29 / 5.59 / 7.16 | 359.1 / 302.0 / 325.9 |
| 4 synthés tables/hybrides, 16 notes | 9.03 / 9.33 / 10.85 | 357.3 / 317.6 / 341.7 |
| 4 synthés numériques, 16 notes | 7.48 / 8.33 / 6.85 | 353.8 / 315.3 / 339.5 |
| 4 instruments physiques/percussifs | 6.82 / 7.45 / 11.20 | 352.2 / 313.7 / 338.5 |
| 8 audio + 8 instruments, effets et bus | 20.44 / 20.05 / 24.79 | 566.9 / 537.5 / 581.2 |
| 16 pistes, effets variés | 20.99 / 21.44 / 20.63 | 514.1 / 484.7 / 489.9 |
| 32 pistes audio sèches | 4.34 / 4.93 / 4.02 | 699.3 / 678.3 / 683.6 |
| 16 compresseurs de base | 3.72 / 4.35 / 4.04 | 488.0 / 459.4 / 464.4 |
| 16 × Comp Pro + Chorus + ReverbPro | 51.66 / 54.08 / 75.26 | 514.5 / 487.8 / 508.3 |

**100 % CPU représente un cœur logique.** La mémoire est un instantané en fin de
fenêtre, pas un maximum ; les banques/tables réchauffées précédemment peuvent rester
en cache. Les historiques en double préservent l’ancien calcul mais augmentent leur
mémoire. Le CPU parent comprend ordonnanceur et tâches de fond ; cycles du thread
audio et intervalles min/max sont dans le JSON. Ces charges n’ouvrent aucun worker
de plugin ; les anciennes campagnes VST3 et DJ restent séparées.

Cadence observée : 46,75–47,08 blocs/s, cohérente avec 48 kHz / 1024. Les 1403
entrées sources/objets/bibliothèques/snapshots contrôlées sont restées stables.
Les neuf processus se ferment normalement. La charge externe varie de 0,34 à 2,13
cœurs équivalents ; aucune compilation ni autre benchmark de cette campagne n’est
concurrent, mais les autres applications ne sont pas arrêtées et aucun résultat
n’est retiré du bilan.

## Validation et limites restantes

- Build complet Release de l’application, du worker et des tests dans `build/cydsp-complete`.
- Premier passage intégral : 121/123, 474,76 s. L’oracle du nouveau test de triangle
  avait le signe du LFO au lieu du signe historique de l’oscillateur ; il a été
  corrigé à partir de l’ancienne formule, sans modifier le DSP pour satisfaire le test.
- Le suivi Cinéma a échoué une première fois puis repassé sans changer l’assertion.
  Ce passage ne prouve pas l’élimination d’une éventuelle intermittence.
- Contrôle final après adaptation des filtres : 8/8 en 66,71 s, incluant les trois
  tests DSP, moteur, Cinéma, Mix Effects, Synths et Creative. Un passage ciblé précédent
  a également revérifié Fonderie, traitements, amplis, percussions et conteneurs
  d’instruments (9/9, 100,07 s). Les logs initiaux restent conservés.
- Corpus final : 98 cas, cinq répétitions par version, zéro allocation observée,
  7,77 millions d’échantillons comparés. L’instrumentation reste dans les bancs et
  copies QA ; elle n’est pas activée par défaut en production.
- Pas de validation macOS/Linux, pas d’écoute humaine ou de tous les presets,
  pas de mesure P95/xruns, pas de nouvelle campagne sur de très longs fichiers.
  La restauration hors ligne conserve un tampon double proportionnel au canal.
- La composante continue après Cinéma et l’échec CUE ASIO des campagnes précédentes
  restent ouverts. Le test de layout Cinéma ne valide pas ces chemins audio.

## Reproduire et retrouver les preuves

Les sources des scénarios sont versionnées ; les PCM, exécutables, traces par thread,
commandes et logs restent dans `output/cydsp-complete/2026-11-14/` (corpus final : `numerical-thread-local/`). Les chiffres de
chaque répétition, topologies représentatives, empreintes et paramètres sont aussi
dans [CyDSPCompleteReuse.json](reports/CyDSPCompleteReuse.json).
Voir les rapports [numérique précédent](CyDSPNumericalReuse.fr.md),
[comparatif précédent](CyAudioCoreCurrentPair.fr.md) et
[diagnostic CPU](CyAudioCoreCpuDiagnostic.fr.md).

MSVC 19.40 x64 Release, `/O2 /Ob2 /MD`, SDK `/fp:strict`, SIMD automatique, sans LTO.
Les SDK utilisés sont les snapshots propres décrits plus haut. Le cache CMake et
les `.tlog` conservent les options effectives. Les builds et témoins précédents
sont des prérequis des commandes de comparaison ; ne pas les reconstruire avec
les nouveaux headers à la place de leurs sources historiques.

```powershell
cmake --preset cyaudio-desktop -B build/cydsp-complete `
  -DCYDAW_CYAUDIOCORE_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cydsp-numerical/2026-11-14/sdk/CyAudioCore `
  -DCYDAW_CYDSP_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cydsp-numerical/2026-11-14/sdk/CyDSP `
  -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyDAW_Project/output/cyui-audioviews/2026-11-14/CyUI
cmake --build build/cydsp-complete --config Release --parallel 10
ctest --test-dir build/cydsp-complete -C Release --output-on-failure
python tests/build_previous_dsp_corpus.py --build build/cydsp-numerical --output output/cydsp-complete/reproduce-before
python tests/cydsp_complete_evidence.py --output output/cydsp-complete/reproduce-numerical `
  --before output/cydsp-complete/reproduce-before/corpus.exe `
  --after build/cydsp-complete/Release/cydaw_full_dsp_reuse_tests.exe --rounds 5
python tests/cyaudio_current_pair_build.py --output output/cydsp-complete/reproduce-rt --current-build build/cydsp-complete
python tests/cydsp_complete_provenance.py --manifest output/cydsp-complete/reproduce-rt/build-manifest.json `
  --previous output/cyaudio-current-pair/2026-11-14/build-manifest.json
# Exécution réelle output-only ASIO, après arrêt des autres builds/tests.
python tests/cyaudio_complete_pair.py --output output/cydsp-complete/reproduce-rt --rounds 3 --seconds 5
```

Les dossiers de sortie temps réel doivent être neufs : le banc refuse un résultat
existant pour préserver les preuves. Les copies QA ajoutent seulement la sonde de
racine et la suppression du dessin. Sorties silencieuses et entrées fermées.
Le build testé est `build/cydsp-complete/CyDAW2_artefacts/Release/CyDAW 2.exe`.
