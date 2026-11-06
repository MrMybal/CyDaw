---
id: studio
title: Studio : arranger et enregistrer
description: Composer un morceau avec des pistes audio et MIDI.
section: Manuel / Composer et éditer
order: 3
sourceRevision: 9e974e9569855dcc
---

## Vue d’ensemble {#overview}

Arrangement construit un morceau sur une timeline musicale commune. Les pistes audio jouent des enregistrements, les pistes MIDI des instruments et les bus reçoivent d'autres pistes. Chaîne, fader, sorties et automation appartiennent à la piste, pas au clip courant. Le Lanceur utilise les mêmes pistes.


~~~image
{"id": "figure-studio", "src": "assets/studio-fr.png", "alt": "Une phrase de notes originale et un clip audio dans l'Arrangement.", "title": "Une phrase de notes originale et un clip audio dans l'Arrangement.", "controls": "minimal"}
~~~

*Une phrase de notes originale et un clip audio dans l'Arrangement.*


## Ajouter pistes et clips {#clips}
Utiliser le plus/menu Piste ou glisser les fichiers du Navigateur/explorateur. Un son déposé sur une piste audio devient un clip à cet instant. Utilisé sur une piste MIDI, il devient plutôt une source de sampler. Double-cliquer une piste MIDI vide crée un clip ; double-cliquer un clip ouvre l'Éditeur.

Glisser un clip le déplace dans le temps ou sur une piste compatible. Ses bords le rognent. Scinder au curseur, dupliquer avec Ctrl+D et retirer avec Suppr. Ajouter des fondus aux raccords audio. Reverse inverse le temps ; la polarité change le signe de l'onde, une autre opération.

## Enregistrer {#recording}
1. Régler entrée audio/MIDI et piste destination ; choisir l'instrument des notes.
2. Armer la piste et contrôler l'écoute à faible niveau. Éviter une double écoute matériel/logiciel.
3. Placer le transport, définir une boucle si besoin et enregistrer. Arrêter et examiner le clip.
4. Plusieurs passages audio peuvent devenir des prises. Choisir leurs régions dans Prises et garder les originaux avec le projet.

## Terminer l'arrangement {#song}
Fixer tempo et signature avant le calage musical. Donner noms et couleurs utiles. Ajouter l'automation pour les changements temporels et router les pistes liées dans un bus. Garder des versions éditables avant les changements de structure. Contrôler master et queues d'effets avant le rendu.

Continuer avec [MIDI](doc:midi), [audio](doc:audio), [Lanceur](doc:launcher) et [export](doc:import-export).
