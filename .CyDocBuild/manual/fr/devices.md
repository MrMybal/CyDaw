---
id: devices
title: Périphériques, presets et panneaux natifs
description: Ajouter instruments et effets et gérer leurs presets.
section: Manuel / Instruments et effets
order: 9
sourceRevision: 9821d0d814f9d701
---

## Vue d’ensemble {#overview}

Périphériques est la façade lisible du graphe de traitement d'une piste. Instruments et effets natifs ont des cartes compactes et, selon le module, un grand éditeur. Un périphérique de graphe peut avoir sa carte et sa fenêtre conçues séparément. Les presets gardent le son ; le projet garde aussi placement, routage et automation.


~~~image
{"id": "figure-synth", "src": "assets/synth-fr.png", "alt": "La fenêtre native regroupe contrôles, ondes, filtre et enveloppes.", "title": "La fenêtre native regroupe contrôles, ondes, filtre et enveloppes.", "controls": "minimal"}
~~~

*La fenêtre native regroupe contrôles, ondes, filtre et enveloppes.*


## Ajouter et ordonner {#chain}
Choisir la piste puis le plus au bout de sa bande. La bibliothèque propose catégories, genres, fabricants et recherche. Double-cliquer un résultat l'insère. Un instrument appartient à une piste MIDI ; un effet traite son audio entrant. Glisser les cartes pour réordonner les chaînes compatibles. Bypasser compare sans supprimer. Enlever un instrument ou un conteneur de routage peut toucher plusieurs cartes : enregistrer avant.

## Presets et valeurs {#presets}
Le bouton de liste/preset charge les réglages d'usine ou personnels. Enregistrer son preset après édition puis vérifier sa réouverture. Un preset de chaîne garde plusieurs périphériques ; un programme de l'Atelier devient un programme du plugin exporté. Ce ne sont pas des formats de projet interchangeables. La liste des valeurs expose aussi les paramètres absents de la carte et se recherche. Millisecondes, secondes, hertz, décibels et pourcentages ont des plages distinctes.

## Grands panneaux et transfert {#panels}
Agrandir la carte ou double-cliquer les titres compatibles ouvre le grand éditeur. Les courbes interactives permettent de dessiner filtre/enveloppe avant réglage numérique. Le crayon d'un périphérique de graphe l'ouvre dans l'Atelier. Transférer le périphérique en .cydawdevice et copier ses médias externes. Les plugins natifs contiennent interface dessinée et DSP, pas l'éditeur de graphe.

Continuer avec les [synthés](doc:synths), les [effets](doc:studio-effects), les [conteneurs](doc:containers) et [l'Atelier](doc:workshop).
