# Moteur audio JUCE / CyAudioCore : mesure séparée de l’interface

**Le correctif FPS précédent ne mesurait pas une accélération du moteur audio.** Cette campagne compare le rendu réel des deux moteurs avec le dessin désactivé, puis livre une correction du cache de lecture dans l’intégration CyDAW. Elle ne modifie ni CyAudioCore ni CyDSP.

## Résultat du moteur, avant le correctif de cache

Trois processus neufs par version, ordre JUCE/Core puis Core/JUCE puis JUCE/Core. Même sortie ASIO Fireface USB silencieuse, 48 kHz, 1024 frames, zéro entrée, deux sorties, profils isolés et MIDI désactivé. Les projets utilisent les mêmes WAV synthétiques et paramètres fixes. Chaque mesure exclut 2 s d’échauffement puis dure 4 s. Aucun build ni benchmark de cette campagne n’est exécuté simultanément. Le poste ne constitue pas un laboratoire au repos garanti ; la charge extérieure est conservée par fenêtre.

Le point commun `VirtualBusRouter::getNextAudioBlock` englobe le rendu applicatif complet. Les sondes confirment environ 46,8 appels/s, proche de 48000/1024. Le master interne est appelé deux fois par bloc en DJ à cause du traitement du mode ; il ne sert donc pas de dénominateur de callback. Huit plugins désignent ici **une instance par piste**, avec huit workers vérifiés par leur nom de processus. Les consoles `conhost.exe` de JUCE sont conservées séparément dans les bruts.

| Charge | Rendu JUCE, ms/bloc | Rendu Core, ms/bloc | Écart de durée | Cycles thread audio JUCE / Core, millions/bloc |
| --- | ---: | ---: | ---: | ---: |
| 32 pistes audio sèches | 0,984 | 1,114 | +13,2 % | 4,876 / 5,637 |
| 16 pistes × compresseur + chorus + réverbération | 11,375 | 11,738 | +3,2 % | 53,890 / 56,192 |
| 8 VST3, transport arrêté | 0,287 | 0,351 | +22,1 % | 1,584 / 1,951 |
| 8 VST3, lecture | 0,385 | 0,400 | +3,9 % | 2,058 / 2,212 |
| 4 platines DJ, aucun worker | 0,561 | 0,572 | +2,1 % | 2,873 / 3,024 |

Ce sont les **médianes des durées moyennes par bloc** de trois sessions. Elles comprennent les préemptions et les attentes éventuelles. Ce ne sont ni des P95 de callback ni des latences physiques. Les cycles proviennent du thread audio identifié par la sonde racine : ils incluent son travail hors du rendu applicatif. Ils ne sont pas convertis en temps à l’aide d’une fréquence CPU supposée.

À 1024/48000, le budget nominal d’un bloc est 21,333 ms. Le cas avec effets utilise donc environ 55 % de ce budget en durée moyenne de rendu Core. Cela ne garantit pas toutes les échéances individuelles. Le pont applicatif natif de périphérique mesure 11,763 ms dans ce cas, contre 11,738 ms pour le rendu : environ 25 µs autour du rendu, pas plusieurs millisecondes. Le coût interne de tous les backends JUCE/Core n’est pas décomposé ici.

**Il reste un surcoût audio mesuré.** L’écart n’est toutefois pas un doublement du moteur avec effets : les anciens chiffres CPU mélangeaient aussi la cadence UI et des topologies différentes. Les petites consommations parent/workers subissent la granularité de GetProcessTimes/GetThreadTimes ; les bruts conservent les cycles et les zéros ne signifient pas absence de travail.

## Où passe le temps côté Core ?

Les durées suivantes localisent les fonctions dans les copies instrumentées. Les appels imbriqués ne s’additionnent pas.

| Fonction / charge | Durée moyenne par appel | Appels/s |
| --- | ---: | ---: |
| Transport adapté, 32 pistes | 7,71 µs | 1544 |
| Analyse des niveaux de piste, 32 pistes | 5,33 µs | 1544 |
| Vérification/remplissage du cache, 32 pistes, thread de fond | 0,80 µs | 50897 |
| Réverbération applicative, 16 chaînes | 568,23 µs | 750 |
| Compresseur applicatif, 16 chaînes | 76,10 µs | 750 |
| Chorus / délai modulé applicatif, 16 chaînes | 34,50 µs | 750 |

- **CyDAW / intégration :** le cache parcourt jusqu’à 64 pages par fenêtre préparée, même lorsque toutes sont valides et que la page courante n’a pas changé. Les répétitions de CAS sur les pages se situent hors du callback, mais consomment du temps et peuvent créer du trafic entre cœurs.
- **CyDAW / DSP applicatif :** `ProEffects.cpp`, `EffectProcessors.cpp` et `AudioRenderPlan.cpp` sont identiques à la référence JUCE après normalisation BOM/fins de ligne. La réverbération constitue le principal coût de cette charge. Elle appartient aux algorithmes de CyDAW, pas automatiquement au processeur public homonyme de CyDSP.
- **CyDAW / buffers :** `SampleBuffer` conserve le backend VectorOps scalaire par défaut. La campagne isolée Core (`CyAudioCore/docs/CorePathProfiling.fr.md`) estime environ 0,155 point de cœur sur le RMS de 32 buffers ; cela ne démontre pas une explication de tout le résidu observé ici. Cette sélection reste à optimiser en préparant explicitement le backend hors audio.
- **CyAudioCore :** le temps du transport adapté comprend à la fois les appels Core et le travail applicatif de buffers/contrôle. La mesure ne permet pas de lui attribuer tout ce coût. La bibliothèque demeure en lecture seule ; aucune modification spéculative de ses kernels n’a été appliquée.

## Correction livrée : fenêtre de cache déjà prête

`src/integration/BufferedAudioSource.cpp` mémorise désormais le début de la dernière fenêtre entièrement prête. Tant que cette page courante reste la même, le thread de lecture évite le nouveau parcours et les CAS de toutes les pages. Un changement de page provoque à nouveau le remplissage ; `setLooping` invalide explicitement ce raccourci. Une page momentanément occupée par le lecteur empêche de déclarer la fenêtre complète.

Le marqueur appartient au scheduler sous le verrou de durée de vie déjà existant. Aucun verrou, allocation ou attente n’est ajouté au callback. Le délai de réveil de 1 ms reste identique. Les traitements DSP et le contenu des pages restent les mêmes.

### Mesures sans instrumentation audio, avant/après cache

Trois paires de processus natifs, ordre alterné, dessin désactivé des deux côtés, mêmes 32 pistes puis 16 chaînes ; 2 s d’échauffement et 5 s mesurées. Seul l’objet du cache change. Les exécutables et leurs empreintes restent distincts.

Le premier candidat mesure **0,485 → 0,399 milliard de cycles parent/s (−17,8 %)** sur les pistes sèches ; CPU médian 4,97 → 4,65 %. Les effets ne montrent pas de gain CPU établi : 53,26 → 53,81 %, plages recouvrantes. Les bruts de ce candidat restent dans `output/cyaudio-cache-window/2026-11-14/`.

Une seconde campagne complète porte directement sur le build final recompilé depuis les sources intégrées :

| Charge | Cycles parent avant, milliards/s | Après | Variation | CPU parent médian avant / après, un cœur |
| --- | ---: | ---: | ---: | ---: |
| 32 pistes sèches | 0,532 | 0,451 | −15,2 % | 5,25 / 6,21 % |
| 16 chaînes d’effets | 2,692 | 2,811 | +4,4 % | 53,23 / 54,51 % |

L’activité extérieure du poste varie fortement dans cette seconde campagne : jusqu’à environ 16,7 cœurs équivalents occupés hors du processus mesuré, contre 0,6 dans une autre fenêtre. Les plages des pistes sèches sont 0,496–0,624 → 0,399–0,579 milliard de cycles/s et 3,71–5,59 → 4,61–7,76 % CPU. Les résultats par paire ne vont pas tous dans le même sens. Les valeurs brutes ne sont pas supprimées.

**Le raccourci du cache supprime un travail redondant et les médianes de cycles des pistes sèches diminuent dans les deux campagnes. Le gain CPU global du build final n’est pas établi dans ces conditions variables.** Il ne faut pas présenter ce correctif comme une accélération générale du DSP, ni considérer les pourcentages de cycles comme un gain universel. Une campagne sur poste réellement au repos reste nécessaire pour borner précisément ce gain. Les mesures de rendu JUCE/Core du premier tableau sont conservées séparément.

## Vérifications et provenance

- Régressions C++ exécutées cinq fois sans périphérique : transport, pause/reprise, sous-plages, sommation, fenêtre de cache entière, changement de boucle sur la même fenêtre, silence de fin de fichier, seeks éloignés et retour sur pages évincées, franchissement de frontière et zéro underrun sur les lectures préparées. Les valeurs PCM sont comparées exactement.
- Build final de production lancé en lecture ASIO silencieuse avec interface : transport et niveaux actifs, capture conservée, MIDI désactivé et fermeture normale. L’aide embarquée validée est installée à côté.
- Toutes les sessions retenues se terminent sans arrêt forcé. Les empreintes des exécutables et sources compilées sont conservées ; le changement de source de production intervient après la comparaison JUCE/Core ; le build final est ensuite recompilé depuis les sources intégrées, vérifié et mesuré séparément.
- Compilation /O2, /MD, /fp:precise, sans LTO. Rééditions de liens locales avec les objets d’origine et headers figés CyAudioCore `bb26e93604d43de2069694b0791bc0bcf5a53ea6` / CyDSP `fda6bd30c5b622795cc08e8bd2750188a6c5a0c0`. Aucun build du checkout mouvant des SDK ; aucun rebuild CMake complet revendiqué.
- Les deux tentatives préliminaires restent conservées et exclues des statistiques : l’une comptait les consoles comme workers, l’autre supposait un unique passage master en DJ. Ce sont des assertions du banc, sans plantage de l’application.
- Les défauts antérieurs Cinéma → Studio / composante continue et CUE ASIO ne sont pas corrigés ni masqués par cette modification. Pas de validation d’écoute, de P95 de callback, de comptage d’xruns matériels ou de plateforme Linux/macOS.

Base applicative avant cache : `bc7c300`. Exécutable livré, comprenant aussi le réglage 30/60/120 FPS :

`output/cyaudio-cache-window/2026-11-14-final/production/CyDAW 2.exe`

Les bruts complets restent dans `output/cyaudio-engine-pair/2026-11-14-root/`, `output/cyaudio-cache-window/2026-11-14/` et `output/cyaudio-cache-window/2026-11-14-final/`. Le résumé distribuable est `docs/reports/CyAudioCoreEnginePair.json`.

## Reproduire localement

Ces helpers nécessitent les objets Release, tlogs, sources JUCE et SDK figés des campagnes locales. Les copies de référence déjà construites conservent la version précédant la correction du cache. Une nouvelle génération depuis la branche courante mesure le nouveau cache : conserver le manifeste avec la révision souhaitée.

```powershell
$py = 'output/cyaudio-qa-python/Scripts/python.exe'
& $py tests/cyaudio_engine_pair_build.py --output output/cyaudio-engine-pair/new-base
& $py tests/cyaudio_engine_pair_root.py --base output/cyaudio-engine-pair/new-base --output output/cyaudio-engine-pair/new-root
& $py tests/cyaudio_engine_pair.py --output output/cyaudio-engine-pair/new-root --tag engine --rounds 3 --seconds 4
& $py tests/cyaudio_cache_build.py --output output/cyaudio-cache-window/new
& $py tests/cyaudio_cache_performance.py --baseline 'output/cyaudio-engine-pair/2026-11-14/native/control/CyDAW 2.exe' --fixed 'output/cyaudio-cache-window/new/control/CyDAW 2.exe' --output output/cyaudio-cache-window/new/performance --rounds 3
```

Les FPS de la campagne précédente sont documentés séparément dans `CyAudioCoreFrameRate.fr.md` ; leurs gains CPU ne sont pas réutilisés ici comme résultat moteur.
