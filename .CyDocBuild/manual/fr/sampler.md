---
id: sampler
title: Sampler, SFZ et instruments d’usine
description: Répartir les échantillons, couches de vélocité et instruments SFZ.
section: Manuel / Instruments et effets
parent: devices
order: 12
sourceRevision: 7516866469500268
---

## Vue d’ensemble {#overview}

Sampler répartit les enregistrements selon touches et vélocités. Une zone définit échantillon, note racine, tessiture et règles ; les couches peuvent se chevaucher. L'échantillonneur simple joue une seule source sur tout le clavier. Les pianos, cordes et vents d'usine utilisent ce moteur et des banques CC0 téléchargées séparément.


~~~image
{"id": "figure-sample-zones", "src": "assets/sample-zones-fr.svg", "alt": "Les fichiers deviennent des zones de touches/vélocités puis des voix jouables.", "title": "Les fichiers deviennent des zones de touches/vélocités puis des voix jouables.", "controls": "minimal"}
~~~

*Les fichiers deviennent des zones de touches/vélocités puis des voix jouables.*


## Construire la répartition {#zones}
Importer les sons ou un SFZ puis contrôler les zones. La note racine doit correspondre à la hauteur enregistrée. Définir limites de clavier et vélocités ; ne superposer des couches que volontairement. Tester grave, médium, aigu et frappes douces/fortes. Une touche non couverte reste muette ; le nom du fichier ne garantit pas sa hauteur réelle.

Régler début/fin, gain, pan, accord et enveloppe d'ampli. Les boucles prolongent un son court ; examiner frontières et fondus pour les clics. Lecture unique, tenue par la touche et zones de relâchement ont des rôles distincts. Le round robin alterne les sons ; les groupes d'exclusion coupent les sons incompatibles.

## SFZ et lecture disque {#sfz}
SFZ peut importer keyswitches, conditions de contrôleurs, courbes de vélocité, régions de relâchement et autres règles. Garder les sons référencés dans la structure relative attendue. Un keyswitch change l'articulation sans forcément jouer. Le streaming disque aide les longs sons ; l'instrument natif exporté emporte au contraire du PCM décodé et le charge en mémoire. Contrôler la taille avant un gros export.

## Autres instruments échantillonnés {#granular}
Nuée travaille vos sons en grains ; taille, densité, position et hauteur changent la texture. Batterie est le sampler de kits acoustiques avec contrôle des pièces/micros. Drum Machine et les couches d'instruments contiennent leurs instruments et effets ; ce ne sont pas des zones ordinaires.

Options > Banques de sons télécharge les médias manquants. Pour déplacer un projet/périphérique, copier ses sources ; l'export éditable liste les fichiers externes sans incorporer toutes les banques.

Voir la [bibliothèque](doc:library), les [batteries](doc:drums), les [conteneurs](doc:containers) et [l'export](doc:plugin-formats).
