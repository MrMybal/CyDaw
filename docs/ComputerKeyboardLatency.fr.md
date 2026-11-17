# Latence du clavier de l’ordinateur — mesure et pont MIDI direct

Campagne locale du 16 novembre 2026, source initiale `475f2fb` avec sonde opt-in. L’application personnelle n’était pas ouverte ; ses réglages enregistrés ont été reproduits dans un profil isolé, sans modification de l’original.

## Conditions et mesure

Windows Audio (Low Latency Mode), sortie RME Fireface UCX, 48 000 Hz, bloc demandé et effectif 480 frames (10 ms), 60 FPS par défaut, transport arrêté. Instrument Opale natif, projet synthétique sans notes préprogrammées, compensation de mixage nulle. Sortie silencieuse et aucune entrée audio/MIDI activée.

24 touches Windows après trois essais d’échauffement. Événements WM_KEYDOWN/WM_KEYUP envoyés à la seule fenêtre de test, sur son bureau d’automatisation caché ; aucune activation du bureau personnel. Le script ne force pas le rendu des frames. Le seuil de premier signal est 10⁻⁵ ; le silence avant chaque note est vérifié. L’horloge monotone est commune aux étapes ; les lectures du port de contrôle arrivent après la mesure et ne définissent pas sa durée.

| Étape interne | Médiane | P95 | Maximum |
|---|---:|---:|---:|
| Émission du message → traitement Windows | 0,047 ms | 0,100 ms | 0,659 ms |
| Traitement Windows → note mise en file MIDI | **17,143 ms** | **17,664 ms** | **17,694 ms** |
| File MIDI → collecte dans le callback audio | 5,371 ms | 9,090 ms | 9,358 ms |
| Collecte → bloc contenant le premier signal prêt | 0,118 ms | 0,171 ms | 0,187 ms |
| Message émis → bloc contenant le signal prêt | **21,908 ms** | **26,848 ms** | **26,936 ms** |

Les moyennes/médianes des étapes ne doivent pas être additionnées pour reconstruire un percentile du total. Le premier signal se trouve ensuite à une position dans le bloc, enregistrée dans les résultats bruts.

## Cause démontrée

`ComputerKeyboard::update()` lit les touches ImGui dans `AppUi::frame()`. `ImGuiHost::wake()` attend encore le prochain créneau de frame. Cette attente impose environ une frame complète au jeu au clavier depuis une interface au repos, alors que le calcul audio du bloc est bien plus court. Le MIDI matériel passe déjà par un autre chemin : le collecteur du moteur.

## Limites

Ce sont des temps de disponibilité **internes**, pas une mesure acoustique. Le commutateur physique, son polling USB, la file de sortie Windows, le convertisseur et les enceintes ne sont pas mesurés. Le backend expose une latence de sortie inconnue ; son adaptateur historique renvoie alors 0 au lieu d’une valeur mesurée. Le champ `outputLatencyKnown` permet de distinguer cette absence d’information. Ni 0 ms de latence physique, ni une latence totale jusqu’aux enceintes ne sont donc démontrés.

Un projet plus chargé et les attaques particulières des instruments peuvent ajouter du retard. La campagne ne mesure pas les mêmes phénomènes qu’un test physique aller-retour. Les dépendances CyAudioCore/CyDSP/CyUI sont restées en lecture seule.

## Reproduction

La sonde est inactive par défaut. Le script l’active seulement dans le processus de test, copie les paramètres audio enregistrés en supprimant les entrées et vérifie l’empreinte des préférences avant/après :

```powershell
output/cyaudio-qa-python/Scripts/python.exe tests/key_latency_check.py --output output/key-latency-new --fps 60 --samples 24
```

Cette commande utilise silencieusement la sortie audio configurée : l’exécuter seulement avec l’autorisation de ce périphérique. Résultats de référence ignorés par Git : `output/key-latency-2026-11-16/stopped-60/report.json`. Binaire de référence et son empreinte conservés dans `output/key-latency-2026-11-16/before/`.

## Correction et contrôleurs programmables

Le service Windows `NativeControllerInput` possède sa propre fenêtre Raw Input et son thread de messages. Il filtre le focus, les raccourcis et les relâchements ; il ne supprime pas les événements clavier/souris habituels de Windows. Le clavier musical ne lit plus ImGui pour produire les notes lorsque ce service est actif. Les états de contexte (saisie de texte, instrument sélectionné, activation) restent publiés par l'interface ; les événements musicaux n'attendent plus son rafraîchissement.

`DirectMidiBridge` réutilise le routeur programmable existant sur les threads d'entrée MIDI/OSC/natifs/extensions. Un contrôle peut alimenter plusieurs destinations MIDI (CC, note, pitch bend, pression) et commandes DAW avec des plages distinctes. L'éditeur ajoute une courbe à 17 points interpolés, sauvegardée avec la définition. Les commandes DAW et le moniteur restent dispatchés sur le thread d'application ; une UI bloquée peut donc retarder ces commandes, sans retenir le MIDI direct. Ni allocation supplémentaire ni mutex de contrôleur ne sont introduits dans le callback audio : ces opérations restent sur les producteurs d'événements.

Les notes partagées ont des propriétaires ; répétitions de touches, relâchements avec plage minimale non nulle, changement de configuration, désactivation, perte du focus et retrait d'un fournisseur ne doivent pas laisser de note tenue. Le clavier et la souris Windows sont actuellement des sources agrégées ; les manettes XInput utilisent l'extension `.cyctl` existante et ses identifiants par appareil. Les protocoles HID supplémentaires nécessitent un fournisseur. Les autres OS gardent le repli clavier existant ; les instances plugin exportées n'enregistrent pas la capture Raw Input du bureau.

## Mesures après correction

Mêmes sortie, fréquence, bloc, instrument, transport arrêté et silence que la référence. Chaque campagne comporte 24 essais retenus et trois échauffements ; chaque processus est neuf et les campagnes sont séquentielles. L'événement **synthétique** est cette fois programmé sur le thread du service d'entrée : le point de départ est son entrée dans le traitement, et non le commutateur physique ni l'arrivée effective d'un paquet WM_INPUT. Le délai volontaire de programmation (20 ou 150 ms) est exclu. Ce test démontre l'indépendance vis-à-vis de l'UI, pas le délai du pilote clavier/USB.

| Chemin | Entrée → file MIDI médiane | Entrée → bloc prêt médiane | Entrée → bloc prêt P95 |
|---|---:|---:|---:|
| Clavier natif, 30 FPS | 0.014 ms | 4.238 ms | 9.395 ms |
| Clavier natif, 60 FPS | 0.022 ms | 4.018 ms | 9.193 ms |
| Clavier natif, 120 FPS | 0.015 ms | 4.235 ms | 8.264 ms |
| Règle clavier → note MIDI, 60 FPS | 0.019 ms | 4.849 ms | 9.130 ms |

La référence avant correction perdait **17,143 ms** entre traitement du message Windows et mise en file MIDI, à 60 FPS. Ce délai de frame tombe ici à environ **0,02 ms** depuis l'entrée du service. L'attente résiduelle avant le prochain bloc de 10 ms demeure et varie selon la phase d'arrivée. Les médianes de blocs entre campagnes ne doivent pas être interprétées comme des différences de vitesse du synthé.

Les six derniers essais de chacune de ces quatre campagnes occupent volontairement le dispatcher UI pendant **350 ms**. L'entrée est émise par le service pendant cette occupation ; les blocs sont prêts au plus tard **10,08 ms** après l'entrée. Le MIDI direct fonctionne donc sans tour de boucle UI. Les relevés de confirmation sur le binaire avec les sécurités de relâchement ajoutées sont conservés dans `native-final` et `mapped-final` (12 essais chacun). La vérification finale `native-verified`, après correction de la présence souris et de la détection MIDI, mesure **0,014 ms** jusqu’à la file et **4,905 ms** jusqu’au bloc prêt en médiane (12 essais, P95 **8,433 ms** pour le bloc).

Les événements programmés sont uniquement disponibles avec `CYDAW_KEY_LATENCY_PROBE=1`, comme la sonde initiale. Aucune génération de touche OS ni activation du bureau personnel n'est nécessaire. Le mode normal n'autorise ni cette injection de service, ni le blocage artificiel du dispatcher. Aucun test ne démontre encore la latence physique d'une manette Xbox ou d'un clavier USB.

## Vérifications et reproduction de la correction

```powershell
# Profils isolés, sortie silencieuse courante, aucune entrée :
output/cyaudio-qa-python/Scripts/python.exe tests/key_latency_check.py --output output/key-latency-direct --mode native --fps 60 --samples 24 --stall-ms 350
output/cyaudio-qa-python/Scripts/python.exe tests/key_latency_check.py --output output/key-latency-mapped --mode mapped --fps 60 --samples 24 --stall-ms 350
output/cyaudio-qa-python/Scripts/python.exe tests/controller_bridge_acceptance.py --output output/controller-bridge-check
# Tests sans périphérique :
ctest --test-dir build/cyaudio-sleep -C Release -R "cydaw_(direct_midi|engine)_tests" --output-on-failure
```

Le mode `--mode os` du script initial sert au binaire de référence avant correction : les messages WM_KEYDOWN synthétiques ne sont pas des paquets Raw Input et ne doivent pas être utilisés pour prétendre tester le nouveau chemin natif.

Les tests Core contrôlent octets MIDI, plages, courbe, deadzone, macro, déduplication, notes partagées, invalides, désactivation et producteur indépendant d'une boucle UI. Le test applicatif reprend la suite OSC/DAW existante, vérifie une macro MIDI + volume master, le geste souris de modification de courbe, la copie de la définition, sa sauvegarde et sa restauration après redémarrage. Les paramètres personnels ont une empreinte identique avant/après. Les tests physiques de focus, saisie et débranchement restent à confirmer avec le matériel ; leurs chemins de libération ont été inspectés, les notes du pont étant couvertes par les tests Core.

Données brutes et empreintes : `output/key-latency-2026-11-16/direct-bridge-campaign.json`, dossiers `native-{30,60,120}`, `mapped-60`, `native-final`, `mapped-final` et `controller-acceptance-verified`. Les assertions audio antérieures (DC Cinéma, CUE ASIO et divergences de premiers blocs) restent distinctes : cette campagne ne les corrige ni ne les masque.
