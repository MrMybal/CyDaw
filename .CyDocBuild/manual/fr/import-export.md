---
id: import-export
title: Enregistrer, importer et rendre
description: Enregistrer les projets, importer des contenus et rendre l’audio.
section: Manuel / Créer et connecter
order: 32
sourceRevision: 6081dbde8a76a015
---

## Vue d’ensemble {#overview}

Enregistrer garde la structure éditable. Exporter rend l'audio ou fabrique un appareil/plugin distribué. Importer traduit et peut perdre les états non gérés. Garder projets et médias d'origine jusqu'à validation du résultat.


~~~image
{"id": "figure-startup", "src": "assets/startup-fr.svg", "alt": "Enregistrer l'éditable avant rendu ou génération d'un autre format.", "title": "Enregistrer l'éditable avant rendu ou génération d'un autre format.", "controls": "minimal"}
~~~

*Enregistrer l'éditable avant rendu ou génération d'un autre format.*


## Session et médias {#save}
Nommer les projets Studio et garder sources/edits. Fonderie/Cinéma ont documents et traitements séparés ; l'enregistrement DJ est une performance rendue. .cydawdevice garde un graphe, pas le morceau complet. Vérifier les médias manquants à la réouverture avant de supprimer une source. Copier le seul exécutable ou JSON du projet n'est pas une sauvegarde des médias.

## Importer d'autres projets {#import}
Fichier > Importer ouvre une nouvelle session et rapporte les pertes. Lecteurs : Live .als, Bitwig .bwproject, Reaper .rpp, FL Studio .flp, Ardour, LMMS, Zrythm, DAWproject et AAF ; MIDI avec audio voisin sert de repli. Studio One .song est approximatif : préférer l'interchange compatible. Les instruments natifs peuvent devenir des équivalents CyDAW sans timbre garanti ; les plugins externes des cartes manquantes. Automation, envois, dossiers et effets varient : lire le rapport et comparer au rendu de référence.

## Rendre et vérifier {#render}
Choisir mix/pistes séparées, plage, destination, fréquence, format/profondeur proposés. Prévoir une traîne pour écho/réverbe. Le limiteur master ne répare pas une source écrêtée. Réimporter ou lire ailleurs et vérifier début, fin, silences, canaux et sonie. Garder l'éditable pour les révisions.

Le plugin demande bundle/ressources ; le graphe transféré un schéma compatible et ses médias. Compiler pour une cible ne valide pas son exécution. Voir [formats](doc:plugin-formats), [Fonderie](doc:foundry#saving), [Cinéma](doc:cinema#delivery) et [licences](doc:licenses).
