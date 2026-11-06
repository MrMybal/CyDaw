---
id: licenses
title: Versions, licences et documentation
description: Retrouver les licences de l’application, des plugins et des banques sonores.
section: Manuel / Référence
order: 36
sourceRevision: 2a59486cab791db0
---

## Vue d’ensemble {#overview}

Ce manuel accompagne l'interface Windows CyDAW V2 et décrit les capacités Alpha actuelles sans promettre toutes les fonctions de chaque norme de plugin. Texte, schémas et captures sont locaux. Aucune connexion de navigateur n'est nécessaire à leur lecture.


~~~image
{"id": "figure-graph-export", "src": "assets/graph-export-fr.svg", "alt": "Produit et cibles d'export gardent leurs frontières de dépendances.", "title": "Produit et cibles d'export gardent leurs frontières de dépendances.", "controls": "minimal"}
~~~

*Produit et cibles d'export gardent leurs frontières de dépendances.*


## Application et dépendances {#licences}
LICENSE distribué avec CyDAW est GNU AGPL version 3. JUCE garde ses conditions ; Dear ImGui et CyUI fournissent MIT. Le paquet contient licence CyUI et notices TextEditor/Lexilla/Scintilla dans Licenses/CyUI, avec les autres notices. FFmpeg est la version partagée LGPL épinglée, avec configuration et sources correspondantes dans son dossier. Lire les textes livrés pour la version utilisée.

SDK des formats, composants, dépendances de plugins, banques et échantillons gardent leurs conditions. Le choix du format d'export ne change pas la licence du code inclus. Les médias d'un instrument ont leurs conditions séparées de l'exécutable.

## Sources et fichiers hors ligne {#sources}
Les [sources CyDAW](https://github.com/MrMybal/CyDaw) et [CyUI](https://github.com/MrMybal/CyUI) identifient les projets. La [licence MIT CyUI épinglée](https://github.com/MrMybal/CyUI/blob/8367401fe45fc22bfed5e227cb3de343177f676b/LICENSE) correspond à l'intégration. Un dépôt privé demande éventuellement l'accès ; les notices installées restent lisibles hors ligne.

Le manuel installé s’ouvre avec `Documentation/Documentation.html` ; ses ressources et notices de dépendances se trouvent dans `Documentation/DocData`. Garder ce dossier entier lors du déplacement de l’application.

## Signaler un problème {#maintenance}
Si un geste diffère de ce guide ou si une image est dépassée, indiquer la version de l’application et le nom de la page/section. Les commandes de lien du lecteur copient un lien précis vers une page ou une image. Les favoris et notes personnels restent dans le stockage local du navigateur ; Mon espace permet de les exporter lors d’un changement de machine.

Revenir à [l'accueil](doc:welcome), consulter [dépannage](doc:troubleshooting) ou [limites plugin](doc:plugin-formats#limits).

Le lecteur CyDoc est sous licence MIT ; son texte et les notices de génération sont livrés dans Documentation/DocData/Licenses. Voir le [dépôt CyDoc](https://github.com/MrMybal/CyDoc).
