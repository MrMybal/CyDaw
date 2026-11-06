---
id: cinema
title: Cinéma : travailler le son à l’image
description: Caler des sons éditables sur les images vidéo et les conteneurs de bus.
section: Manuel / Modes dédiés
order: 22
sourceRevision: 52e81bb062439e59
---

## Vue d’ensemble {#overview}

Cinéma présente des lignes de bus comme l'arrangement, avec des conteneurs temporels dedans. Un conteneur est une sous-piste audio ou MIDI contenant des clips ordinaires. Sa sous-piste va au bus puis au master Cinéma. Les conteneurs peuvent se suivre horizontalement ; ce ne sont pas des timelines plein écran séparées. La vidéo est facultative si un transport externe fournit le temps.


~~~image
{"id": "figure-cinema", "src": "assets/cinema-fr.png", "alt": "Timeline commune, vignettes, conteneurs, moniteur et outils de piste.", "title": "Timeline commune, vignettes, conteneurs, moniteur et outils de piste.", "controls": "minimal"}
~~~

*Timeline commune, vignettes, conteneurs, moniteur et outils de piste.*


~~~image
{"id": "figure-cinema-bus", "src": "assets/cinema-bus-fr.svg", "alt": "Le son des sous-pistes passe au bus puis au master Cinéma.", "title": "Le son des sous-pistes passe au bus puis au master Cinéma.", "controls": "minimal"}
~~~

*Le son des sous-pistes passe au bus puis au master Cinéma.*


## Créer bus et sous-pistes {#containers}
Créer/nommer le bus puis double-cliquer sa timeline vide ou utiliser son ajout pour créer un cadre temporel. Choisir audio/MIDI, placement et durée. Son titre déplace toute la sous-piste ; ses clips bougent indépendamment, selon les types compatibles. Sélectionner un bus et l'onglet Sous-pistes en bas : il liste tous les cadres, même hors écran. Sélectionner ou Cadrer/double-cliquer pour les retrouver ; FX/A ouvre effets/automation. Réaffecter un cadre à un bus change aussi sa sortie principale.

## Éditer sons et notes {#clips}
Déposer un son dans un cadre audio. Double-cliquer un espace audio vide choisit un fichier pour cette sous-piste ; le clic droit importe ou crée un clip vide. En MIDI, double-cliquer crée un clip de notes et son éditeur ; choisir l'instrument. Les menus de clips proposent Détails, Reverse, scission au curseur, duplication et suppression. Rogner les bords ou déplacer/copier entre cadres compatibles. Détails règle décalage, gain et fondus. Annuler/Rétablir suit le contexte actif.

## Vidéo, vignettes et images {#video}
Charger la vidéo depuis moniteur, menu ou dépôt. Le moniteur se déplace, se regroupe, se redimensionne ou se détache. Les réglages définissent début dans le projet, point d'entrée, durée et cadence. Sans vidéo retire la référence mais garde les sons importés. La bande suit la même échelle que les sons : vignettes espacées en vue large, chaque image en zoom rapproché. Cliquer/glisser fait défiler le moniteur. Choisir la grille image, précédent/suivant ou les flèches et vérifier la cadence avant le calage fin.

Si la vidéo contient du son, l'extraction crée un bus de bande son et un clip complet éditable. Le couper/déplacer/traiter comme un WAV ne modifie pas la vidéo. Importer la bande son explicitement crée une autre extraction ; rouvrir la session garde les éditions sans dupliquer l'import.

## Transport, effets et livraison {#delivery}
La double bande déplace/zoome autour du pointeur ; la règle place le curseur ; les outils natifs scindent/dupliquent/suppriment/annulent. Naviguer manuellement peut suspendre le suivi. Effets, routage, bus et automation master appartiennent à la session Cinéma. SYNC permet un éditeur externe sans vidéo locale si les deux côtés gèrent le même protocole. Link partage la phase musicale, pas une position de film arbitraire.

L'export rend l'audio, pas une nouvelle vidéo montée. Garder session et vidéo séparément, choisir la traîne et vérifier avec l'image finale. Voir [synchronisation](doc:sync), [routage](doc:mixing) et [export](doc:import-export).
