# CyAudioCore : veille automatique des plugins dans CyDaw

Validation du 15 novembre 2026, branche `CyDSP-CORE`, base applicative `cadcd58`.

## Révisions et livraison locale

La version précédente utilisait CyAudioCore `4e5710999acbd88ab3d1ce87f4101ec73abe54b6`.
L’application et son worker sont maintenant reconstruits ensemble avec
`f9b0c813c233e9d5d3cfdef6c8492bbe8f5548ea`, qui ajoute la veille automatique.
CyDSP reste à `0d67c6a60749ef241a87bc3592fa63c61998a5e2` et CyUI à
`7a793e9281e78572818d85e9cf856d779a7792a1` pour isoler cette intégration.

Les sources audio proviennent d’archives Git propres dans
`output/cyaudio-sleep/2026-11-15/sdk`. Les modifications locales de documentation
présentes dans le dépôt frère CyDSP sont exclues. Aucun dépôt frère n’a été
modifié. Les empreintes des deux arbres SDK sont identiques avant/après les tests.
Le manifeste conserve les SHA des archives et les modifications exclues.

Exécutable livré : `build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe`,
avec son `CyDAWPluginWorker.exe` et ses ressources à côté. Les anciens builds et
leurs preuves sont conservés. Les modèles Studio, Fonderie, Cinéma et DJ sont
copiés dans le nouveau build. Aucun push ni workflow GitHub n’est lancé ; le pin
source de l’action de dépendances est mis à jour pour un prochain build autorisé.

## Comportement

`NativeHostedPlugin` utilise explicitement `PluginSleepMode::automatic`, le
maintien au silence de 2048 frames et le seuil exact zéro du SDK. Aucun mode
heuristique `whenQuiet` n’est imposé. CLAP suit son statut natif ; VST3 suspend
seulement les effets admissibles déclarant notamment une latence et une queue
finies, sans ports d’événements ni catégorie générateur. LV2/JSFX restent actifs
par défaut. Le rendu hors ligne ne dort pas.

Le pont audio continue ses soumissions/récupérations non bloquantes, avec un
quantum de retard déclaré. Le SDK évite les copies IPC et réveils audio du worker
endormi ; le processus, son contrôle, son état et sa surveillance restent actifs.
Aucune attente ni journalisation n’est ajoutée au callback applicatif.

La commande locale `status` expose maintenant pour chaque plugin audio :
`sleepAvailable`, `sleeping`, `processedBlocks`, `skippedBlocks`. Le premier indique
la disponibilité du diagnostic, pas l’éligibilité à la veille. Les compteurs
32 bits bouclent et sont remis à zéro à l’activation/démarrage explicite. Leur
lecture ne réveille pas le worker. L’ancien pont Wine n’a pas ce diagnostic.

## Vérifications réalisées

Trois tests natifs passent, sans périphérique :

- `cydaw_native_plugin_sleep_tests` : fixture CLAP originale, 2 blocs traités puis
  14 évités sur 16 blocs silencieux ; première entrée active et gain exacts au
  réveil, queue de 2048 frames conservée, réveil sur seek arrêté et automation,
  12/12 blocs traités hors ligne et zéro évité, réveil dans le pont temps réel
  avec le quantum de retard attendu et aucun ticket manqué dans ce test cadencé.
- `cydaw_native_hosted_plugin_tests` : tempo, tailles variables, état et JSFX.
- `cydaw_native_plugin_ipc_tests` : fragments, état, automation, éditeur, crash,
  nettoyage et contrat d’absence d’allocation du callback déjà existant.

Dans l’application Release, profil isolé, sorties ASIO Fireface USB silencieuses
à 48 kHz/1024 frames, aucune entrée audio et aucune entrée MIDI activée :

- Huit instances CLAP : huit workers SDK enfants directs, toutes en veille.
- Après stabilisation, chacune reste à **2 blocs traités** ; chacune évite
  **36 blocs supplémentaires** pendant une observation d’environ 0,75 seconde.
- Automation, reprise audio, retour en veille après arrêt, sauvegarde/rechargement
  et restauration du gain 0,75 passent. Aucun enfant ne reste après reset ;
  arrêt normal du processus en 430 ms, sans terminaison forcée.
- Acceptation existante : huit VST3, les quatre formats VST3/CLAP/LV2/JSFX,
  paramètres/états, éditeurs, crash d’un worker et survie des trois autres passent.
  Arrêt normal en 454 ms. Cette acceptation n’établit pas que ses fixtures VST3
  sont éligibles à la veille automatique.
- Graphe de dépendances : 41 projets de production, aucun include/bibliothèque JUCE.
- Empreintes exécutables/fixtures inchangées pendant chaque acceptation.

Les preuves brutes sont dans `output/cyaudio-sleep/2026-11-15` :
`sdk-manifest.json`, `source-hashes-before.json`, `source-hashes-after.json`,
`configure.log`, `build.log`, `native-tests.log`, `application/report.json`,
`four-formats/acceptance.json`, `summary.json` et les journaux des commandes.

## Reproduction

Utiliser les chemins SDK du manifeste au configure, en gardant le même CyUI :

```powershell
cmake -S . -B build/cyaudio-sleep -G "Visual Studio 17 2022" -A x64 -DCYDAW_BUILD_DESKTOP=ON -DCYDAW_BUILD_V2=ON -DCYDAW_BUILD_TESTS=ON -DCYDAW_FETCH_JUCE=OFF -DCYDAW_ENABLE_ASIO=ON -DCYDAW_ENABLE_LTO=OFF -DCYDAW_CYAUDIOCORE_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-sleep/2026-11-15/sdk/CyAudioCore -DCYDAW_CYDSP_SOURCE_DIR=D:/_Project/CyDAW_Project/output/cyaudio-sleep/2026-11-15/sdk/CyDSP -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyDAW_Project/output/cyui-audioviews/2026-11-14/CyUI -DCYUI_IMGUI_DIR=D:/_Project/CyImgui_Interface/third_party/imgui
cmake --build build/cyaudio-sleep --config Release --target CyDAW2 cydaw_native_plugin_sleep_tests cydaw_native_hosted_plugin_tests cydaw_native_plugin_ipc_tests --parallel 4
ctest --test-dir build/cyaudio-sleep -C Release -R 'cydaw_native_(plugin_sleep|hosted_plugin|plugin_ipc)_tests' --output-on-failure
python tests/cyaudio_plugin_sleep_check.py --exe 'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe' --plugin build/cyaudio-sleep/Release/cydaw_plugin_sleep_fixture.clap --mode asio --output output/cyaudio-sleep/recheck
```

L’option `--mode asio` réutilise uniquement le matériel silencieux autorisé pour
cette campagne. `--mode null` demande un binaire QA disposant du pilote factice ;
ce n’est pas une mesure du périphérique ASIO.

## Limites

Les compteurs prouvent les traitements évités, **pas un pourcentage de gain CPU**.
Aucun nouveau benchmark comparatif de performance n’a été fait. La fixture CLAP
contrôlée et l’acceptation quatre formats ne constituent pas une validation de
tous les plugins tiers. Les matériels/MIDI externes, Wine, Linux/macOS et les
réveils propres à chaque éditeur tiers demandent une acceptation séparée.
Les écarts audio déjà documentés, la composante continue après Cinéma, l’échec
CUE ASIO et les pertes de blocs du long export Studio ne sont pas corrigés ni
masqués par cette campagne.
