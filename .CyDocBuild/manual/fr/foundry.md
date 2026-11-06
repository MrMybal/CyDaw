---
id: foundry
title: Fonderie : montage sonore multipiste
description: Assembler, réparer et traiter des fichiers son.
section: Manuel / Modes dédiés
order: 21
sourceRevision: 84e56ff09e2ce8fe
---

## Vue d’ensemble {#overview}

La Fonderie est l'espace des fichiers son, accessible en haut ou par F11. Chaque document a son onglet, fréquence, pistes audio/MIDI, clips, traitement et master. Le Studio reste séparé. Enregistrer une session .cyfoundry pour un travail avec plusieurs clips, MIDI, routage ou effets ; exporter crée l'audio.


~~~image
{"id": "figure-foundry", "src": "assets/foundry-fr.png", "alt": "Fonderie avec montages superposés d'un son de test original.", "title": "Fonderie avec montages superposés d'un son de test original.", "controls": "minimal"}
~~~

*Fonderie avec montages superposés d'un son de test original.*


## Importer, sélectionner et naviguer {#editing}
Ouvrir un son comme document ou ajouter des sons. Déposer sur une piste insère au temps visé ; sous les pistes en crée une. + Piste choisit audio ou MIDI ; MIDI demande instrument et clips de notes. Double-cliquer les noms pour renommer ; utiliser M/S, niveau/pan et sélection.

L'aperçu cadre la session. La bande supérieure se glisse pour déplacer/zoomer ; double-clic cadre. La molette zoome l'onde, Maj+molette déplace, Ctrl+molette grossit les ondes ; la bande verticale règle hauteur/défilement. Un clic place le curseur ; glisser sélectionne une durée sur les pistes ; Maj étend. G/D/G+D limite les canaux édités.

## Clips et opérations de source {#clips}
Glisser le titre déplace le clip ou change de piste compatible ; les bords rognent et les coins supérieurs règlent les fondus. Clic droit : scinder, dupliquer, gain, Reverse, supprimer. Détails règle début, durée, décalage, gain et fondus. S scinde au curseur ; Ctrl+D duplique.

Couper/copier/coller et collage en mixage portent sur curseur/sélection ; dans un son ils peuvent réécrire ses morceaux en gardant un clip. Supprimer, rogner, insérer silence et crayon ont des rôles distincts. Annuler/Rétablir et Historique reviennent aux versions ; éditer depuis une ancienne remplace la suite. Marqueurs (M) et régions (Maj+M) nomment moments/plages.

## Traiter, router et automatiser {#processing}
Traitements/Marqueurs sont en bas à droite ; Détails/Analyse/Historique restent près du montage. Choisir traitement, portée et valeurs avant Appliquer. Normaliser change le pic, inverser change le temps, polarité change le signe. Le plus inférieur ouvre la bibliothèque complète du Studio. Chaînes/faders et bus vont au master de session. A automatise pistes et master ; MASTER ouvre ses effets.

## Sauver et exporter {#saving}
Ctrl+S garde le travail éditable ; un simple fichier peut s'enregistrer en son, une session personnalisée en .cyfoundry. L'export inclut routage et effets. La traîne conserve écho/réverbe après la source ; zéro peut les couper. Envoyer le mix au Studio crée une source rendue ; chaque piste garde des parties rendues séparées. Ce ne sont pas des liens vivants vers le document.

Voir [édition audio](doc:audio), [traitements](doc:processing), [Cinéma](doc:cinema) et [médias](doc:import-export).
