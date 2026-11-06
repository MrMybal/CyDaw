---
id: welcome
title: Bienvenue et premier projet
description: Produire un premier son et choisir un espace de travail.
section: Manuel / Bien démarrer
order: 0
sourceRevision: c929e4beac79abaf
---

## Vue d’ensemble {#overview}

CyDAW réunit studio musical, table DJ, éditeur de sons multipiste, espace son à l'image, éditeur de traitements et atelier de plugins. Ce manuel hors ligne décrit l'interface V2 distribuée. Sa langue initiale est l'anglais ; choisir Français en haut retrouve les mêmes sujets en gardant la section courante. Cela ne change pas la langue de l'application.


~~~image
{"id": "figure-startup", "src": "assets/startup-fr.svg", "alt": "Du choix de la carte son au projet enregistré et au mix exporté.", "title": "Du choix de la carte son au projet enregistré et au mix exporté.", "controls": "minimal"}
~~~

*Du choix de la carte son au projet enregistré et au mix exporté.*


## Lire, rechercher et naviguer {#reading}
Ouvrir Aide > Manuel utilisateur ou Ctrl+F1. CyDAW ouvre le lecteur CyDoc installé comme fichier local dans le navigateur par défaut. Conserver le dossier Documentation complet à côté de l'exécutable ; aucun serveur, CDN ou accès Internet n'est nécessaire.

Utiliser l’arborescence des sujets, le plan de page et la recherche globale en haut (Ctrl+K). Elle couvre texte, code et symboles ; des filtres de contenu/langage et une tolérance aux fautes sont proposés. Les accents français sont facultatifs. Le sélecteur de langue conserve la page et les identifiants communs de section. Le manuel complet est disponible en anglais et en français.

Précédent/Suivant du navigateur et les outils CyDoc conservent navigation et positions. Mon espace garde favoris, pages récentes et notes privées localement ; Reprendre ma lecture retrouve une position enregistrée. Exporter ces données pour les sauvegarder. La politique de stockage du navigateur peut limiter leur persistance. L’aide contextuelle vise la page ou section demandée ; Reprendre ma lecture dans Mon espace retrouve une lecture enregistrée.

Les images ont des commandes épurées visibles au survol ou au focus : agrandir dans la même fenêtre de documentation, zoomer avec molette/boutons, glisser pour déplacer et double-cliquer pour recentrer. Échap réduit une image agrandie. Les commandes tactiles restent visibles et permettent déplacement/pincement. Employer le zoom du navigateur et le menu de thème pour le confort de lecture. Ces touches appartiennent au navigateur ; projet et transport du DAW restent séparés.

## Produire un premier son {#first-project}
1. Dans Options > Réglages audio choisir la carte et les canaux reliés aux enceintes. Commencer avec un tampon modéré et baisser le master avant l'écoute.
2. Dans Studio créer une piste instrument MIDI. Opale est un bon premier synthé.
3. Double-cliquer dans cette piste MIDI vide pour créer un clip. Ouvrir son éditeur, dessiner une phrase et appuyer sur Espace.
4. Sélectionner la piste et utiliser le plus au bout de Périphériques pour ajouter un effet. Enregistrer avec Ctrl+S.
5. Exporter un mixage pour obtenir un fichier audio. Enregistrer le projet et rendre l'audio sont deux opérations différentes.

## Choisir son mode {#modes}
| Mode | Utilisation |
| --- | --- |
| Studio | Arranger des pistes ou lancer clips et scènes. |
| Mix DJ | Mélanger des morceaux sur deux ou quatre platines. |
| Fonderie | Couper, réparer et assembler des fichiers son. |
| Cinéma | Placer les sons à l'image, avec une vidéo facultative. |
| Traitements | Créer et essayer des opérations audio ou sur les notes. |
| Atelier Plugin | Construire des graphes et interfaces VST3, CLAP ou LV2. |

Chaque mode possède son contexte. Vérifier le mode actif avant Enregistrer, Annuler ou le transport. Les modules natifs gardent des noms anglais même dans l'interface française ; les deux langues du manuel décrivent les mêmes réglages.

Continuer avec les [réglages audio](doc:setup), [l'espace de travail](doc:workspace) ou le [Studio](doc:studio).
