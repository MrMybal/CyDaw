# Cadence d’affichage réglable et coût CPU

Le rendu CyDAW utilise désormais **60 FPS par défaut**, avec **30 / 60 / 120 FPS** dans **Options → Fréquence d’affichage**. Le choix s’applique immédiatement et est conservé après redémarrage. Une valeur de préférence invalide revient à 60. La cadence active ne dépend plus du taux du moniteur ; au repos, elle descend à 12 FPS. Ces plafonds concernent l’interface et ses fenêtres détachées, indépendamment de la fréquence audio et du buffer.

## Corrections

- `ImGuiHost` pilote les échéances de rendu depuis la boucle Windows. Les fractions de période sont conservées : le polling à 1 ms ne rabaisse plus mécaniquement 120 FPS à environ 111. Après un blocage, les images manquées ne sont pas rattrapées en rafale.
- Les réveils liés aux interactions raccourcissent l’attente du repos sans réinitialiser le plafond à chaque événement. Les captures et entrées scriptées peuvent toujours demander explicitement une image immédiate.
- Le navigateur et la grande bibliothèque ne demandent plus `wake(6)` à chaque dessin d’un simple message vide/en chargement. Une sonde de diagnostic avait confirmé environ 60 appels `wake(6)` par seconde dans une session vide, sans animation audio : cela empêchait réellement le repos. Le rafraîchissement à 12 FPS permet toujours de constater la fin du chargement en arrière-plan.
- Le manuel local anglais/français décrit ce réglage, avec l’ancre commune `display-frame-rate`.

Aucun traitement audio ni dépôt CyAudioCore, CyDSP ou CyUI n’a été modifié.

## FPS réellement mesurés

Compteur autour de `ImGuiHost::frame` uniquement dans une copie QA. Les mesures naturelles excluent les captures et les clics scriptés. Échauffement de 2 s, puis 4 s mesurées, processus neuf pour chaque cas.

| Cas | FPS mesurés |
| --- | ---: |
| Profil neuf, défaut 60 | 60,21 |
| Choix 30 | 29,96 |
| Choix 120 | 120,15 |
| Nouveau processus après sauvegarde de 120 | 120,10 |
| Préférence invalide 999 → défaut 60 | 59,94 |
| Session vide au repos | 11,99 |

Les clics réels dans les menus anglais et français, les trois sauvegardes immédiates et le rechargement après redémarrage sont vérifiés. Les cinq modes Studio, Mix DJ, Fonderie, Cinéma et Atelier ont été ouverts et capturés ; ce contrôle d’ouverture ne constitue pas un test exhaustif de chaque fonction. Les processus se sont fermés normalement.

## Comparaison CPU avant / après

**Exécutables sans instrumentation**, original natif contre correction au défaut 60. Trois processus neufs par version, ordre alterné, même disposition Arrangement + Périphériques, mêmes charges synthétiques déterministes. ASIO Fireface USB, 48 kHz / 1024 frames, zéro entrée, deux sorties silencieuses. Échauffement de 2 s et 5 s de mesure par charge. Aucun build ni autre benchmark lancé par cette campagne pendant les mesures. **100 % CPU représente un cœur logique.**

| Charge | CPU parent avant, médiane % | CPU après, médiane % | Réduction relative |
| --- | ---: | ---: | ---: |
| 32 pistes audio sèches | 21,11 | 15,14 | 28,3 % |
| 16 pistes × compresseur + chorus + réverbération | 95,85 | 70,20 | 26,8 % |

Plages observées : 18,77–22,04 → 12,40–15,79 pour les pistes sèches ; 82,90–98,05 → 67,88–70,33 pour les effets. Les médianes des cycles parent passent respectivement de 1,647 à 0,992 et de 4,914 à 3,634 milliards de cycles/s. Les effets sont insérés par index numérique et vérifiés sur chacune des 16 pistes.

Le taux original proche de 139 FPS avait été mesuré dans la campagne précédente (`CyAudioCoreUiPair.fr.md`) ; les exécutables de ce tableau CPU ne contiennent pas de compteur FPS. Le gain ci-dessus porte sur ces projets et cette machine, sans démontrer une accélération du DSP lui-même. Les bruts conservent les temps CPU par thread, cycles, charge système, commandes et état final. Les fluctuations de la machine restent visibles dans les plages.

## Build et reproductibilité

Base du dépôt : `c136b4e`. Exécutable corrigé :

`output/cyaudio-frame-rate/2026-11-14-fixed-idle/production/CyDAW 2.exe`

Il s’agit d’une **recompilation Release /O2 des quatre fichiers UI modifiés et d’une réédition de liens avec les objets audio d’origine**, pas d’un build CMake complet à neuf. Cela préserve exactement les bibliothèques audio de référence alors que les SDK voisins sont en cours de modification. Les includes audio sont figés sur CyAudioCore `bb26e93604d43de2069694b0791bc0bcf5a53ea6` et CyDSP `fda6bd30c5b622795cc08e8bd2750188a6c5a0c0`. Le test compilé de taille/alignement de `ImGuiHost` donne 416/8 avant et après. MSVC 14.40.33807, /MD, /fp:precise, sans LTO. L’exécutable original n’a pas été écrasé.

Depuis la racine, avec les artefacts de référence locaux présents, utiliser un nouveau dossier de sortie :

```powershell
$py = 'output/cyaudio-qa-python/Scripts/python.exe'
& $py tests/cyaudio_frame_rate_build.py --output output/cyaudio-frame-rate/new-campaign
& $py tests/v2_frame_rate_check.py --build output/cyaudio-frame-rate/new-campaign --output output/cyaudio-frame-rate/new-campaign/acceptance
& $py tests/cyaudio_frame_rate_performance.py --fixed 'output/cyaudio-frame-rate/new-campaign/production/CyDAW 2.exe' --output output/cyaudio-frame-rate/new-campaign/performance --rounds 3
& $py .CyDocBuild/tools/build.py --offline
& $py .CyDocBuild/tools/install.py Documentation output/cyaudio-frame-rate/new-campaign/production/Documentation
```

Le helper local nécessite le build natif de référence, ses tlogs et les headers SDK figés de la campagne précédente. Le build CMake normal compile directement les sources corrigées et enregistre `cydaw_frame_pacing_tests` lorsque `CYDAW_BUILD_TESTS` est activé.

## Vérifications et limites

- Test C++ compilé et exécuté : 12/30/60/120 FPS sur 10 s simulées, polling régulier et irrégulier, reprise après un blocage sans rafale.
- Acceptation UI, persistance, FPS actifs et repos : réussis. Tous les périphériques MIDI sont restés désactivés ; aucun flux d’entrée physique n’a été ouvert.
- Six processus de performance sans instrumentation et douze fenêtres de mesure : terminés sans arrêt forcé ; empreintes des exécutables contrôlées avant/après.
- Empreintes des sources compilées inchangées après la campagne. Export CyDoc hors ligne régénéré et validé : 90 pages, anglais et français.
- Pas de nouveau build complet de tous les SDK ni de répétition de toute la suite audio ; pas de validation Linux/macOS ou de mesure de latence analogique. Les observations antérieures de composante continue après Cinéma et d’échec CUE ASIO ne sont pas corrigées ni masquées ici.

Les preuves détaillées restent dans `output/cyaudio-frame-rate/2026-11-14-fixed-idle/` ; un résumé distribuable est conservé dans `docs/reports/CyAudioCoreFrameRate.json`. Les essais initiaux qui ont révélé les réveils permanents restent dans `output/cyaudio-frame-rate/2026-11-14/`.
