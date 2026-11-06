---
id: interface-design
title: Concevoir fenêtres et cartes module
description: Concevoir la fenêtre du plugin et sa carte de module compacte.
section: Manuel / Créer et connecter
parent: workshop
order: 26
sourceRevision: c8dee75dd9f8b555
---

## Vue d’ensemble {#overview}

Interface édite deux surfaces indépendantes : Grande fenêtre pour l'éditeur/plugin et Carte module pour la bande de CyDAW. Tailles, contrôles et couleurs sont enregistrés séparément. Une interface native lisible demande des liaisons explicites en plus de la disposition.


~~~image
{"id": "figure-synth", "src": "assets/synth-fr.png", "alt": "Un panneau natif montre les groupes de contrôles disponibles au designer.", "title": "Un panneau natif montre les groupes de contrôles disponibles au designer.", "controls": "minimal"}
~~~

*Un panneau natif montre les groupes de contrôles disponibles au designer.*


## Taille, calques et contrôles {#layout}
Choisir la surface puis largeur/hauteur en pixels, fond, accent, texte et grille. Les réglages de grande fenêtre restent accessibles avec un contrôle sélectionné. 1:1 montre la vraie taille ; Cadrer adapte l'aperçu. Zoom et bouton du milieu déplacent le canevas sans redimensionner le produit.

Ajouter potards, faders, texte, afficheurs et panneaux natifs compatibles. Calques sélectionne, masque et verrouille ; Ctrl/Maj étend, le rectangle sélectionne plusieurs éléments. Aligner/distribuer puis grouper. Régler page, groupe, nom, arrondi, couleurs et affichage de valeur. Prévoir place pour les longues traductions et valeurs extrêmes.

## Relier les panneaux natifs {#bindings}
Pour un instrument natif du graphe, Ajouter l'interface complète crée pages et sections reliées. Les contrôles déjà dessinés restent. La bibliothèque de sections fournit aussi des panneaux pour un graphe personnel sans cet instrument : affecter chaque liaison nécessaire. Sans liaison, le contrôle est désactivé. Les panneaux d'effets ont aussi leurs liaisons. Changer le dessin ne corrige pas une mauvaise destination sonore.

## JSON déclaratif et C++ {#code}
Code JSON édite toute la surface active. Recharger reprend le modèle ; Appliquer valide types, IDs, liaisons et limites. Sauver avant les changements de structure. Les IDs sont uniques et les liaisons doivent exister. Un JSON visuellement plausible peut viser le mauvais paramètre : tester en direct.

Un composant C++ ImGui ajoute dessin/interaction. Exemple fournit le modèle CustomInterfaceABI et ses callbacks get/set ; Compiler affiche les diagnostics. Respecter l'ABI actuelle ; le code accompagne les exports et demande un compilateur cible. Tester redimensionnement, désactivation, presets et automation hôte, pas seulement l'image initiale.

Voir [Atelier](doc:workshop), [paramètres](doc:graphs#exposure) et [export](doc:plugin-formats).
