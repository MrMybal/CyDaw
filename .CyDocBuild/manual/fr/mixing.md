---
id: mixing
title: Mixeur, bus et sidechains
description: Router les pistes à travers les bus, envois et sidechains.
section: Manuel / Composer et éditer
parent: studio
order: 8
sourceRevision: 2624a3d201a0b0c4
---

## Vue d’ensemble {#overview}

Un bus reçoit des pistes via ses effets et son fader. La sortie principale dirige le signal d'une piste ; un envoi ajoute un chemin parallèle. Garder le trajet complet visible pour contrôler niveaux ou absence de son.


~~~image
{"id": "figure-routing", "src": "assets/routing-fr.svg", "alt": "Sources, bus de groupe et retour parallèle rejoignent le master.", "title": "Sources, bus de groupe et retour parallèle rejoignent le master.", "controls": "minimal"}
~~~

*Sources, bus de groupe et retour parallèle rejoignent le master.*



~~~image
{"id": "figure-mixer", "src": "assets/mixer-fr.png", "alt": "Le mixeur Studio partage le routage des pistes, bus et master.", "title": "Le mixeur Studio partage le routage des pistes, bus et master.", "controls": "minimal"}
~~~

*Le mixeur Studio partage le routage des pistes, bus et master.*


## Pistes, bus et canaux {#buses}
Créer et nommer un bus, puis le choisir comme sortie des pistes liées. Y mettre le traitement commun. Dans Routage vérifier entrées/sorties supplémentaires, format de canaux et niveau d'envoi. Sources mono, effets stéréo et destinations multicanal demandent des affectations compatibles. CyDAW refuse les boucles : corriger le trajet au lieu de forcer un cycle.

## Envois et sidechain {#sidechain}
L'envoi pré-fader est pris avant le fader ; le post-fader le suit. Décider si baisser la source doit aussi baisser son retour. La sidechain alimente le détecteur, pas une autre entrée audible. Le bouton maillons des dynamiques compatibles choisit sa source. Un kick peut piloter le compresseur de basse avec son fader baissé puisque l'envoi est pré-fader ; le couper est différent. Écouter le détecteur si proposé et régler seuil, attaque et relâchement.

## Master et mesures {#master}
Contrôler les niveaux partout. Un plafond de limiteur évite les pics excessifs mais n'annule pas une saturation déjà imprimée dans la source ou un étage précédent. Crête, RMS et sonie mesurent des choses différentes. Remettre l'intégrée à zéro avant une passe complète ; comparer à sonie d'écoute proche.

Fonderie, Cinéma et traitement DJ ont leurs contextes avant la sortie commune. Voir les [effets](doc:studio-effects), la [Fonderie](doc:foundry) et [l'export](doc:import-export).
