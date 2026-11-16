---
id: controllers
title: Contrôleurs MIDI, OSC et matériels
description: Apprendre les commandes matérielles et configurer leur retour.
section: Manuel / Créer et connecter
order: 28
sourceRevision: be7cf38d3e9d0314
---

## Vue d’ensemble {#overview}

CTRL ou Options > Contrôleurs MIDI/OSC ouvre les appareils. Un périphérique identifie le matériel ; ses règles transforment les contrôles vers les cibles CyDAW. Ce n'est pas simplement activer les notes d'une piste. L'apprentissage part du matériel ou d'un contrôle logiciel encadré.


~~~image
{"id": "figure-automation", "src": "assets/automation-fr.svg", "alt": "Une règle matérielle change un paramètre, aussi accessible à automation et modulation.", "title": "Une règle matérielle change un paramètre, aussi accessible à automation et modulation.", "controls": "minimal"}
~~~

*Une règle matérielle change un paramètre, aussi accessible à automation et modulation.*


## Créer et apprendre {#learn}
Ajouter un appareil puis bouger un contrôle pour identifier l'entrée, ou la choisir. MIDI, clavier, plugin manette et points OSC sont distincts. Pour OSC créer le point UDP et ses hôte/ports de retour : ce sont des contrôles, pas un flux audio.

Activer Apprendre dans les règles et bouger les contrôles. Nommer la règle, choisir potard/fader/encodeur/bouton/bascule/pad/axe. Notes permet à l'entrée MIDI de jouer aussi l'instrument sélectionné. Retour choisit LED, fader motorisé, état de tablette ou retour manette géré.

## Transformer et assigner {#rules}
Plage d'entrée, inversion, courbe, décodage relatif, bascule, seuil, zone morte, pas et plage de sortie s'appliquent dans l'ordre ; le retour inverse ce qui est géré. Choisir le bon codage d'encodeur. Ajouter une ou plusieurs cibles par recherche ou Clic cible. Régler plage et reprise pour rattraper la valeur sans saut.

Adresses MIDI : cc/canal/numéro, note/canal/note, pb/canal, at/canal, pc/canal/programme. Le clavier utilise key/Nom ; OSC son adresse reçue. Le moniteur distingue absence d'événements et mauvaise liaison.

## MIDI direct et macros {#direct-midi}

Dans une règle, utiliser **+ Destination MIDI directe**, choisir CC, Note, Pitch bend ou Pression, puis régler canal et numéro. Les messages entrent dans le MIDI live de CyDAW ; sélectionner/armer la piste instrument et régler son entrée live comme d'habitude. Cela n'installe pas de port MIDI virtuel pour les autres logiciels.

Une macro est une règle avec plusieurs destinations. Par exemple, relier le stick gauche d'une manette au pitch bend et au CC 74, ainsi qu'à un paramètre de piste. Chaque destination possède sa propre plage de sortie ; les transformations précédentes sont communes. Clavier, MIDI, OSC et extensions d'application utilisent les mêmes règles. Ajouter **Courbe dessinée** aux modifications, déplacer ses 17 points ou double-cliquer pour retrouver une droite. La réponse est interpolée entre les points. Une courbe non monotone possède plusieurs inverses possibles ; le retour utilise son premier segment le plus proche.

Sous Windows, l'application de bureau capture clavier et souris avec un service Raw Input dédié quand CyDAW a le focus. Les touches musicales et les destinations MIDI assignées n'attendent pas une frame d'interface. La saisie de texte et les raccourcis Ctrl/Alt/Windows inhibent les contrôles clavier. Perte du focus, retrait d'un appareil, changement d'assignation et fermeture relâchent les notes tenues. Les claviers et souris sont actuellement regroupés dans une source par famille. Les autres plateformes conservent le repli clavier par l'UI ; les plugins exportés n'enregistrent pas cette capture de bureau.

Adresses souris : `button/left`, `button/right`, `button/middle`, `button/x1`, `button/x2`, position normalisée sur l'écran `axis/x`, `axis/y`, et molette relative `wheel`. Utiliser **Relatif** avec le codage complément à deux pour la molette. L'extension d'application XInput fournit chaque manette Xbox compatible connectée : sticks, gâchettes et boutons. Les autres matériels peuvent fournir leurs adresses via l'ABI publique d'extensions contrôleurs `.cyctl` ; les protocoles HID arbitraires ne sont pas décodés automatiquement.

La conversion MIDI s'exécute sur le thread d'entrée/fournisseur. Les commandes qui modifient le logiciel passent par le thread d'application et peuvent attendre s'il est occupé. Le callback audio n'exécute ni configuration de contrôleur ni commande d'interface. Définitions des contrôleurs, courbes et assignations MIDI globales sont sauvegardées dans les réglages de l'application ; les assignations DAW propres à un projet restent dans ce projet.

## Portée et sauvegarde {#scope}
Ctrl+M ou le menu de CTRL active l'apprentissage dans l'interface : cliquer un contrôle encadré puis bouger le matériel. Échap annule. Les cibles de piste/appareil suivent le projet ; transport, DJ, master et piste sélectionnée peuvent être globaux. Changer entrée/retour emporte les règles. Dupliquer pour un appareil similaire puis identifier sa nouvelle entrée.

Voir [MIDI](doc:setup#midi), [modulateurs](doc:modulators) et [synchronisation](doc:sync).
