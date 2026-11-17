# CyAudioCore : un worker natif par plugin

Campagne du 15 novembre 2026, branche `CyDSP-CORE`, base `1f187e6`.
Cette correction suit le [rapport de mise à jour 0.3.2](CyAudioCore032Update.fr.md),
qui avait constaté deux processus enfants par plugin. Aucun push ni workflow
GitHub n’est lancé. CyAudioCore, CyDSP, CyUI, CyCode et les banques personnelles
ne sont pas modifiés.

## Changement d’architecture

Sur le chemin natif, CyDaw appelle maintenant directement le fournisseur isolé
public de CyAudioCore. Un seul `CyDAWPluginWorker --cydsp-worker` contient le
plugin. Le worker applicatif `--realtime-plugin` ne s’intercale plus entre le
parent et ce worker SDK. Cela concerne VST3, CLAP, LV2 et JSFX, y compris les
sélecteurs `.cyplugin` utilisés pour choisir un plugin dans un module.
Les bibliothèques étrangères ne sont pas chargées dans le processus CyDaw.

Le parent réutilise `NativeHostedPlugin`, ses paramètres, ses presets LV2,
son codec d’états historiques et sa fenêtre native/générique. L’exécutable du
worker est fourni explicitement, sans accès aux chargeurs privés du SDK.
Le graphe de production reste sans JUCE.
Le worker Windows utilise le sous-système GUI avec son entrée `main` conservée :
les probes gardent leurs sorties redirigées, et Windows ne crée plus de
`conhost.exe` supplémentaire pour chaque plugin.


L’adaptateur préparé utilise `sameCycle` avec une soumission et une récupération
non bloquantes par quantum. Des callbacks partiels alimentent le même quantum
préparé ; les données entrantes sont copiées avant de remplacer le buffer par
la sortie retardée. Les événements d’automation conservent leur offset dans le
quantum. La latence déclarée vaut un quantum préparé plus la latence intrinsèque
du plugin. Le premier quantum est silencieux. Une réponse en retard est
abandonnée ; elle ne peut pas réapparaître à un autre emplacement temporel.
Une instance arrêtée ou déclarée défaillante rend le signal sec au bypass.

L’audio ne fait ni attente ni allocation C++ : ses buffers et événements sont
préparés. Les opérations d’état, preset et changement de ports prennent la main
sur le propriétaire audio depuis le thread de contrôle ; elles ne bloquent pas
le callback. Pendant cette transition, la chaîne active produit du silence.
Les changements de paramètres utilisent des valeurs atomiques ; les retours
du quantum précédent ne doivent pas écraser une valeur hôte encore en attente.

Les RPC SDK pompent des messages Windows. Un garde de déférence empêche ces
messages de relancer les tâches de CyDaw ou une image ImGui au milieu d’une
opération SDK. La boucle de tâches évite aussi une vidange récursive. Les
fenêtres étrangères continuent à recevoir leurs messages. Le contrôle SDK du
parent est limité à 10 Hz sans éditeur visible, 50 Hz avec éditeur ; le worker
continue de pomper lui-même son interface. Les textes de paramètres sont mis en
cache jusqu’au changement de valeur ou à la reconstruction des métadonnées.
Les fenêtres génériques masquées ne rafraîchissent plus leurs valeurs.

Les commandes de fenêtres SDK utilisent un canal qui peut remettre à zéro le
ticket `sameCycle` en attente. Un handoff couvre donc ouverture, affichage,
focus, resize et fermeture ; les demandes de resize reçues dans un pump gardent
le handoff jusqu’à la fin du RPC extérieur. Le callback ne peut pas utiliser
un ticket annulé. Ce handoff réamorce le pipeline et peut produire une courte
interruption lors d’une commande de fenêtre ; aucune continuité sans coupure
pendant un redimensionnement n’est revendiquée.


Le SDK VST3 met les changements du contrôleur en attente pour le processeur.
Une sauvegarde sans callback audio doit donc consommer les changements avant
de lire l’état : le thread de contrôle peut traiter un quantum silencieux,
avec une attente bornée, puis réinitialise le pipeline. Ce traitement est hors
callback ; il peut faire avancer l’état DSP interne d’un quantum lors d’une
sauvegarde contenant des changements non consommés. Une API publique SDK
permettant de vider ces changements sans rendre d’audio serait préférable.

## Périmètre conservé

Le pont à fichier partagé reste utilisé par le fixture passthrough et par la
compatibilité Linux Wine/UMU vers un plugin Windows. Cette dernière traverse
deux systèmes de mémoire partagée différents : elle conserve l’ancien chemin
imbriqué. **Le passage à un worker unique n’est pas revendiqué pour Wine/UMU.**
Cette campagne valide Windows x64 ; Linux/macOS et tous les plugins tiers
restent à tester.

Les SDK restent aux révisions de la campagne précédente :

- CyAudioCore `4e5710999acbd88ab3d1ce87f4101ec73abe54b6` ;
- CyDSP `0d67c6a60749ef241a87bc3592fa63c61998a5e2`, 0.3.2 ;
- CyUI `7a793e9281e78572818d85e9cf856d779a7792a1`.

Les anciens rapports sont préservés avec une note indiquant cette correction
ultérieure. La composante continue Cinéma et l’échec historique CUE ASIO ne
sont ni corrigés ni masqués par ce travail.

## Vérifications et métriques

Les résultats définitifs et les empreintes sont enregistrés dans
[le manifeste de campagne](reports/CyAudioCoreSingleWorker.json).
Les logs complets restent dans `output/cyaudio-single-worker/2026-11-15`.

**124/124 tests réussissent** au passage complet final (670,62 s), sans
modification de l’assertion de synchronisation ayant échoué au premier passage.
Le scénario de liste de valeurs qui déconnectait l’application repasse après
la protection de réentrée. La première suite (122/124), les premiers essais
applicatifs et les logs de correction sont conservés.

Dans le vrai CyDaw :

| Scène | Workers directs | Autres descendants | Latence du pipeline | Blocs manqués observés |
| --- | ---: | ---: | ---: | ---: |
| Huit VST3 Gain | 8 | 0 | 1 024 frames | 0 |
| Projet rechargé | 8 | 0 | 1 024 frames | — |
| VST3 + CLAP + LV2 + JSFX, éditeurs ouverts | 4 | 0 | 1 024 frames | 0 |

Les fenêtres de lecture comportent douze relevés espacés de 200 ms. Le paramètre
Gain sauvegardé à 0,37 est retrouvé après réouverture. Un worker est terminé
volontairement : les trois autres restent actifs, puis la remise à zéro ferme
les descendants et l’application quitte normalement en 585 ms environ.
Ces chiffres sont des observations fonctionnelles courtes, sans intervalle de
confiance ni garantie d’absence d’xruns. À 48 kHz, le seul quantum applicatif
représente 21,33 ms ; ce chiffre n’est pas la latence physique totale.

Le test client séparé avec le vrai VST3 passe également. Sa vérification finale
d’attente de fin de processus accorde jusqu’à deux secondes à la terminaison
forcée Windows, qui est asynchrone ; elle exige toujours zéro worker vivant.
Deux essais initiaux comparaient la présence immédiate dans Toolhelp avant la
fin de cette terminaison. Le compteur distingue maintenant les processus
vivants des objets de processus déjà signalés. Aucun test audio, synchronisation,
CUE ou composante continue Cinéma n’est assoupli.


Le test du client est exécuté avec un vrai VST3 Gain et avec un script JSFX
créé uniquement pour le test. Il contrôle :

- un enfant SDK direct, aucun worker imbriqué, puis zéro enfant après fermeture ;
- audio traité, automation, états et paramètre édité avant le premier callback ;
- 2 048 frames par canal sous forme de fragments de 1, 7, 17, 39, 47, 32 et
  63 frames, avec une latence exacte de 64 frames et des sentinelles de buffer ;
- zéro allocation C++ ordinaire ou alignée sur le thread du callback préparé ;
- automation JSFX à l’offset 17 au milieu du quantum ;
- éditeur, crash injecté dans le seul enfant possédé, bypass sec et fermeture.

Les pauses entre fragments du faux appelant laissent au worker le temps de
répondre. Ce test vérifie les PCM et les positions ; il ne mesure pas la latence
physique ASIO ou les xruns. La garde d’allocations ne surveille pas les
allocations internes des processus plugins ni tous les appels système C.

Le contrôle de l’application utilise ensuite huit VST3 simultanés, une
sauvegarde/relecture du projet et quatre formats simultanés. Le pilote autorisé
est Fireface ASIO, 48 kHz, 1 024 frames, zéro entrée, sorties silencieuses,
entrées MIDI désactivées et profil isolé. Le compteur de processus est mesuré
sur les descendants du seul processus de test. Les demandes d’éditeurs et un
crash d’un worker sont également exercés.

Aucun nouveau pourcentage de gain CPU n’est déduit de ces contrôles
fonctionnels. La suppression d’un processus/IPC est vérifiable ; son gain de
performance nécessite une nouvelle comparaison équitable et au repos.

## Points précis à remonter à CyAudioCore

- `src/plugins/PluginWorker.cpp::State::rpc` remet `pending` à zéro pour les
  commandes autres que `remote`, notamment les commandes de fenêtre.
  Une commande GUI indépendante du ticket audio éviterait le handoff et la
  réinitialisation du pipeline effectués ici par CyDaw.
- `src/plugins/IsolatedPlugin.cpp::IsolatedBackend::pumpMainThread` fait un
  RPC `Op::pump` après le poll local. Une API publique de poll/notifications
  sans RPC systématique réduirait le travail de contrôle du parent ; aucune
  mesure CPU n’attribue ici un pourcentage précis à cet appel.
- `src/plugins/Vst3Plugin.cpp::flush/saveState` : la file `uiToAudio_` explique
  le `would_block` observé si une sauvegarde suit un changement non consommé.
  Une API de capture d’état intégrant ce cas sans quantum de rendu éviterait
  l’avancement d’état DSP décrit plus haut.

Ces points portent sur le contrat de contrôle CyAudioCore et l’adaptation
CyDaw. Aucune régression numérique de CyDSP n’est déduite de cette campagne.

## Reproduction

Réutiliser la configuration figée de `build/cyaudio-032` décrite dans le rapport
0.3.2, ou configurer le preset natif avec les mêmes snapshots SDK.

```powershell
cmake --build build/cyaudio-032 --config Release --parallel 4
ctest --test-dir build/cyaudio-032 -C Release --output-on-failure -j 1
python tests/no_juce_build_check.py build/cyaudio-032
& build/cyaudio-032/Release/cydaw_native_plugin_ipc_tests.exe `
  'D:/_Project/CyDAW_Project/build/cyaudio-032/CyDAWPluginWorker_artefacts/Release/CyDAWPluginWorker.exe' `
  'D:/_Project/CyDAW_Project/build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3' --silent
python tests/cyaudio_single_worker_check.py --mode asio `
  --exe 'build/cyaudio-032/CyDAW2_artefacts/Release/CyDAW 2.exe' `
  --vst3 '<bundle Gain VST3>' --clap '<fixture Gain.clap>' --lv2 '<fixture Gain.lv2>' `
  --output 'output/cyaudio-single-worker/nouvelle-campagne/application'
```

Le script refuse d’écraser un précédent résultat. Les fixtures tierces/originales
sont fournies explicitement et leurs empreintes sont enregistrées. Le mode
`null` exige un binaire QA configuré pour le fournisseur factice ; il ne prétend
pas simuler les résultats du périphérique physique. Le banc comparatif
`cyaudio_sdk_plugin_pair.py` attend huit workers natifs par défaut ;
`--current-workers 16` conserve la reproduction de l’ancienne architecture.
