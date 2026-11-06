---
id: developer-api
title: Étendre et piloter CyDAW
description: Choisir le contrat public des composants, contrôleurs ou automatismes.
section: Développer / API publique
order: 100
sourceRevision: 50dad86ee80c0b28
---

## Choisir une interface {#overview}
Utiliser l’interface correspondant au travail à réaliser :

| Objectif | Contrat public |
| --- | --- |
| Dessiner un composant interactif de l’atelier | [ABI des composants visuels](doc:api-interface-abi) |
| Ajouter un contrôleur matériel ou réseau brut | [ABI des extensions contrôleurs](doc:api-controller-abi) |
| Implémenter un processeur audio du moteur | [Contrat du processeur audio](doc:api-audio-processor) |
| Piloter une session CyDAW depuis un client MCP | [Serveur MCP](doc:mcp) |

Ces contrats ont des durées de vie et des threads différents. Les callbacks UI ne traitent pas l’audio ; les extensions contrôleurs ne reçoivent pas de tampons audio. Employer les structures hôtes versionnées et les includes publics sans supposer la compatibilité binaire de compilations arbitraires.

## Garder des exemples courts {#examples}
Les pages API donnent signatures, paramètres, retours et courts fragments d’utilisation. Les modèles complets de composants se trouvent dans l’action Exemple de l’atelier. Enregistrer le périphérique avant d’essayer un composant et vérifier ses liaisons dans le [concepteur d’interface](doc:interface-design#code).
