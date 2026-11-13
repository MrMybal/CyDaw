# Comparaison directe de l’interface JUCE / CyAudioCore

**Avec les mêmes panneaux et un plafond commun à 30 Hz, le vrai test de 16 pistes × 3 effets mesure 64.62 % CPU pour JUCE et 62.58 % pour CyAudioCore.** Le dessin prend respectivement 3.54 ms et 3.06 ms par image. L’ancienne hausse CPU globale ne démontrait donc pas un dessin plus lent par image : elle mélangeait cadence hors écran, disposition différente et charge mal nommée. Les plages se recouvrent ; il ne s’agit pas d’un gain général certifié de l’audio SDK.

Campagne contrôlée du 14 novembre 2026. Ce rapport rectifie les interprétations CPU des campagnes précédentes, dont les preuves sont conservées.

## Ce qui était différent

1. **Cadence dans les sessions cachées.** JUCE utilise VBlank et, hors écran sans notification VBlank, son timer de secours à 30 Hz. La version native interroge l’horloge depuis la boucle Windows et vise le taux du moniteur même hors écran. Le même dessin est donc exécuté bien plus souvent. Ces FPS sont maintenant comptés dans les deux versions, plutôt que déduits du seul code.
2. **Disposition et onglets.** Le remplacement de APPDATA ne redirige pas `juce::File::getSpecialLocation(userApplicationDataDirectory)` : JUCE passe par le dossier spécial Windows, alors que l’adaptateur natif lit APPDATA. L’ancienne copie chargeait donc une disposition préexistante : Graph/Lanceur selon le passage, contre Périphériques/Arrangement dans l’autre. Les préférences PropertiesFile étaient isolées, mais pas cette lecture de disposition. `CYDAW_NO_RECOVERY` désactive sa sauvegarde ; ces campagnes n’ont pas modifié volontairement la disposition personnelle.
3. **Ciblage des effets dans le banc.** Toutes les pistes importées depuis `long.wav` s’appellent `long`. Les anciennes commandes ciblaient ce nom, résolu vers la première piste. Le cas ancien appelé `effects16` était **16 pistes audio dont la première porte 48 effets en série et les 15 autres sont sèches**, pas 16 chaînes indépendantes. La liste `devices` du statut déduplique les noms, ce qui masquait les répétitions. Huit VST3 étaient aussi réunis sur la première des huit pistes, avec huit workers réels.

Le nouveau cas `effects16` cible les index renvoyés lors des imports. Il vérifie explicitement les trois types d’effets sur chacune des 16 pistes. `effectsSerial48` reproduit séparément l’ancienne charge en série. L’ancienne attribution de 95 % de l’écart aux threads UI reste une localisation des anciens processus mesurés ; **elle ne prouve pas que le coût de dessin identique a augmenté de cette proportion**.

## Mesures répétées

ASIO Fireface USB, 48 kHz, 1024 frames, entrées désactivées, deux sorties silencieuses, fenêtre cachée 1500×900. Trois processus neufs par combinaison, ordre alterné, 2 s d’échauffement exclus et 6 s mesurées. **100 % CPU = un cœur logique.** Le CPU du tableau concerne le parent ; workers, threads, cycles bruts et plages restent dans le JSON.

Les deux copies QA lisent leur disposition depuis leur profil vide, puis affichent Arrangement et Périphériques. Une capture précède chaque mesure, hors intervalle ; les sondes confirment que Périphériques est dessiné. Le plafond 30 vise 30 Hz : le taux effectivement obtenu figure dans le tableau.

### Cadence d’origine des deux versions

| Charge | CPU JUCE % | CPU Core % | FPS JUCE / Core | Cycles UI par image JUCE / Core (millions) |
| --- | ---: | ---: | ---: | ---: |
| 8 VST3 chargés, arrêtés, première piste | 5.19 | 15.75 | 29.60 / 139.80 | 8.58 / 6.54 |
| 32 pistes audio sèches | 10.03 | 20.63 | 29.78 / 139.30 | 9.01 / 6.73 |
| 48 effets en série sur la première de 16 pistes | 111.99 | 153.05 | 29.48 / 95.91 | 98.68 / 48.13 |
| 16 pistes × 3 effets indépendants | 65.65 | 92.34 | 29.78 / 138.84 | 18.15 / 13.52 |
| 4 platines DJ, aucun worker | 3.36 | 4.93 | 29.79 / 139.34 | 2.77 / 3.00 |

### Même plafond de dessin, 30 Hz

| Charge | CPU JUCE % | CPU Core % | FPS JUCE / Core | Cycles UI par image JUCE / Core (millions) |
| --- | ---: | ---: | ---: | ---: |
| 8 VST3 chargés, arrêtés, première piste | 5.19 | 4.92 | 29.06 / 29.42 | 9.91 / 10.34 |
| 32 pistes audio sèches | 8.81 | 9.28 | 29.05 / 29.42 | 10.54 / 10.22 |
| 48 effets en série sur la première de 16 pistes | 117.60 | 88.27 | 29.06 / 29.46 | 104.90 / 56.67 |
| 16 pistes × 3 effets indépendants | 64.62 | 62.58 | 29.06 / 29.46 | 19.01 / 16.88 |
| 4 platines DJ, aucun worker | 2.57 | 2.84 | 29.23 / 29.42 | 4.20 / 4.58 |

## Coût par image, panneau identique et plafond commun

Les durées sont des temps écoulés instrumentés, qui peuvent inclure une préemption. Les cycles sont obtenus par QueryThreadCycleTime sur le thread concerné ; ils ne sont pas convertis avec une fréquence CPU supposée. Les coûts inclusifs imbriqués ne s’additionnent pas. Les cycles UI par image du tableau précédent incluent aussi le travail du thread hors dessin.

### 16 pistes × 3 effets indépendants

| Fonction | Appels/image JUCE / Core | Temps inclusif ms/image JUCE / Core | Cycles inclusifs/image (millions) JUCE / Core |
| --- | ---: | ---: | ---: |
| ImGuiHost::frame | 1.00 / 1.00 | 3.544 / 3.059 | 16.609 / 14.211 |
| AppUi::drawWindows | 1.00 / 1.00 | 3.191 / 2.668 | 14.965 / 12.490 |
| DevicesView::draw | 1.00 / 1.00 | 2.416 / 2.005 | 11.333 / 9.379 |
| DevicesView::drawCard | 3.00 / 3.00 | 0.938 / 0.769 | 4.399 / 3.600 |
| GraphDocument::editorNodes | 4.14 / 4.13 | 0.842 / 0.440 | 3.951 / 2.058 |
| inferSurface | 3.00 / 3.00 | 0.082 / 0.054 | 0.385 / 0.252 |
| UI::D3Ddraw | 1.00 / 1.00 | 0.067 / 0.071 | 0.316 / 0.332 |
| UI::Present | 1.00 / 1.00 | 0.069 / 0.070 | 0.326 / 0.327 |

### 48 effets en série sur la première de 16 pistes

| Fonction | Appels/image JUCE / Core | Temps inclusif ms/image JUCE / Core | Cycles inclusifs/image (millions) JUCE / Core |
| --- | ---: | ---: | ---: |
| ImGuiHost::frame | 1.00 / 1.00 | 21.690 / 11.579 | 101.699 / 54.061 |
| AppUi::drawWindows | 1.00 / 1.00 | 21.289 / 11.122 | 99.851 / 52.040 |
| DevicesView::draw | 1.00 / 1.00 | 15.567 / 8.529 | 73.021 / 39.908 |
| DevicesView::drawCard | 4.00 / 4.00 | 7.482 / 3.879 | 35.105 / 18.164 |
| GraphDocument::editorNodes | 5.13 / 5.14 | 16.180 / 7.395 | 75.899 / 34.591 |
| inferSurface | 4.00 / 4.00 | 0.115 / 0.070 | 0.540 / 0.328 |
| UI::D3Ddraw | 1.00 / 1.00 | 0.098 / 0.097 | 0.457 / 0.453 |
| UI::Present | 1.00 / 1.00 | 0.078 / 0.081 | 0.364 / 0.381 |

## Localisation dans les sources

- Ancien `ImGuiHost.cpp` archivé : constructeur/VBlank à 324, secours à 30 Hz à 331–338 ; `vblank` à478. Source figée `cf4228d5ed18897b786bb10f4844673593c21c5a`, avec le patch QA de préférences déjà documenté.
- Natif `src/v2/ImGuiHost.cpp:333` : poll ;372–373 : lecture du taux écran ;455–467 : limitation idle/active ;616 : Present(0,0). `src/v2/Main.cpp:1102` : boucle Windows, dispatch/poll puis attente de 1 ms.
- `src/v2/AppUi.cpp:292` : lecture de la disposition ; `src/base/File.cpp:436` : APPDATA. JUCE `juce_Files_windows.cpp:712` : résolution CSIDL_APPDATA.
- `src/v2/RemoteCommands.cpp:514` : résolution par nom, première correspondance ; `src/app/GraphDocument.cpp:5477` : déduplication des noms de périphériques.
- `src/v2/DevicesView.cpp:214` reconstruit les descriptions par `editorNodes()` ;661 : cartes ;683 : inférence de présentation. `src/app/GraphDocumentEditorAccess.cpp:56` reconstruit vecteurs/ports/paramètres. Le code d’inférence de `DeviceVisuals.cpp` est le même après remplacement de String par Text et retrait des deux includes ajoutés.

## Responsabilités et suites ciblées

La cadence et la résolution de disposition appartiennent à l’intégration **CyDAW**, et le ciblage par nom appartenait au **banc CyDAW**. Ce sont des différences démontrées. Le présent banc ne modifie ni les bibliothèques CyAudioCore/CyDSP ni les traitements audio et ne permet pas d’attribuer ses écarts globaux à un kernel SDK. Les mesures par image ci-dessus remplacent les suppositions sur les cartes.

Pour la suite : normaliser explicitement cadence et visibilité lors des comparaisons ; isoler tous les chemins de disposition ; utiliser des index/identifiants stables dans les charges synthétiques. Pour optimiser les grandes chaînes, mesurer un cache des descriptions de graphe invalidé aux éditions, plutôt que reconstruire toutes leurs métadonnées plusieurs fois par image. Cette campagne ne livre aucune de ces modifications en production. Le choix scalaire de SampleBuffer et les microbenchmarks du rapport précédent restent une question indépendante ; ils n’expliquent pas à eux seuls cet écart de cadence.

## Validité et limites

- Même instrumentation UI dans les deux copies ; aucun scope ajouté au callback audio. Les sondes utilisent temps écoulé et cycles du thread, avec export hors callback. Elles ajoutent un coût : ce sont des mesures de localisation, pas une nouvelle certification des performances de distribution.
- L’expérience à 30 Hz force ce rythme dans les deux copies en amont du gate idle. Le secours JUCE reçoit des ticks rapides pour éviter qu’un double plafonnement à 30 Hz ne perde des images à cause du jitter. Son coût de réveil est inclus dans le CPU parent du contrôle ; les scopes de dessin l’isolent. Le taux réel est mesuré, pas supposé exactement 30.
- Sessions cachées : la différence VBlank/secours est propre à cette condition. On ne peut pas en déduire la cadence d’une ancienne fenêtre visible sur écran ni un ralentissement audio intrinsèque en usage visible.
- Aucun benchmark ni compilation concurrent de notre campagne. Les autres applications du poste n’ont pas été arrêtées ; leur charge est enregistrée. Les petites différences ne sont pas des preuves d’un gain généralisé.
- Les compteurs CPU Windows sous-comptent certaines petites bouffées sur ce poste. Les cycles restent conservés ; zéroCPU déclaré ne signifie pas absence de travail. Pas de piles ETW : la limitation de privilèges documentée précédemment n’a pas été contournée.
- Les exécutables originaux, inputs de lien et sources sélectionnées sont vérifiés par empreinte. Toutes les sessions retenues se ferment sans arrêt forcé. Les SDK sont des archives figées ou des objets originaux, sans rebuild de leurs répertoires de travail.
- Les trois tentatives exploratoires sont conservées avec notes explicites dans les dossiers2026-11-13,2026-11-13-aligned et2026-11-13-final ; elles ne sont pas mélangées aux statistiques contrôlées. Les deux interruptions sont des fermetures QA demandées par l’investigateur, pas des plantages du produit.
- Les différences audio Cinéma→Studio (composante continue) et l’échec CUE ASIO1024 restent dans les preuves précédentes. Aucun résultat audio n’a été masqué ou assertion assouplie. Pas de nouvelle validation d’écoute, de latence physique, d’xruns, ni de plateforme Linux/macOS.

## Reproduire

Prérequis : les builds originaux /O2 et les archives SDK de la campagne précédente existent localement. Le script relie des copies QA depuis leurs objets, sans CMake ni reconstruction des SDK. Choisir un dossier neuf.

```powershell
$qaPython = "D:/_Project/CyDAW_Project/output/cyaudio-qa-python/Scripts/python.exe"
& $qaPython tests/cyaudio_ui_pair_build.py --output output/cyaudio-ui-pair/<nouvelle-campagne>
& $qaPython tests/cyaudio_ui_pair.py --output output/cyaudio-ui-pair/<nouvelle-campagne> --rounds 3 --seconds 6
& $qaPython tests/cyaudio_ui_pair_report.py --input output/cyaudio-ui-pair/<nouvelle-campagne>
```

Preuves brutes : `output/cyaudio-ui-pair/2026-11-14-controlled`. [Mesures, scopes, threads, commandes et empreintes](reports/CyAudioCoreUiPair.json). Aucun push, GitHub Actions ou changement d’antivirus.
