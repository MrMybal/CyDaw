---
id: automation
title: Automation et courbes
description: Dessiner et modifier les variations des réglages de piste et de périphérique.
section: Manuel / Composer et éditer
parent: studio
order: 7
sourceRevision: 49d8f12b179aaebe
---

## Vue d’ensemble {#overview}

L'automation inscrit les changements dans la timeline ; la modulation produit des contrôles variables autour d'une base. Valeur enregistrée, courbe et quantité de modulation participent différemment au résultat joué. Un indicateur mobile peut donc différer du bouton.


~~~image
{"id": "figure-automation", "src": "assets/automation-fr.svg", "alt": "Base, automation temporelle et quantités rejoignent le paramètre destination.", "title": "Base, automation temporelle et quantités rejoignent le paramètre destination.", "controls": "minimal"}
~~~

*Base, automation temporelle et quantités rejoignent le paramètre destination.*


## Créer les lignes {#lanes}
Utiliser le menu contextuel d'un réglage ou les commandes A de piste pour choisir la destination. Ajouter les points où la valeur doit être définie. Encadrer un changement pour préserver les autres passages. Volume, paramètres d'effets et master ont des liaisons distinctes : nommer les lignes. Studio, Fonderie et Cinéma ont leurs propres temps.

## Dessiner les courbes {#curves}
Sélectionner avant déplacement, copie ou suppression. Choisir l'interpolation : palier pour commuter, linéaire pour une rampe, lisse/Bézier/B-spline si proposé. Tangentes et pliage modifient le chemin entre les points. Vérifier unités et échelles : fréquence logarithmique, temps et gain avec leurs plages. Annuler suit l'éditeur propriétaire du geste.

## Combiner la modulation {#combined}
Partir d'une base utile, ajouter un modulateur et régler sa quantité à l'écoute. Une flèche armée édite la profondeur, pas la base. Une quantité négative inverse le sens. Contrôler les extrêmes de résonance, feedback et gain. Les paramètres/modulateurs exportables sont conservés ; l'automation de l'hôte fournit alors ses valeurs au temps du plugin.

Voir les [modulateurs](doc:modulators), le [mixage](doc:mixing) et [l'exposition](doc:graphs#exposure).
