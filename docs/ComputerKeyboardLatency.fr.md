# Latence du clavier de l’ordinateur — référence avant pont MIDI direct

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

## Suite demandée

Remplacer le polling musical de l’interface par un service de capture des périphériques et de conversion en MIDI, indépendant des frames, commun aux sources natives et aux extensions `.cyctl`. La référence ci-dessus décrit le comportement avant cette correction.
