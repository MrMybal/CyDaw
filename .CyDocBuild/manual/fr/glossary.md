---
id: glossary
title: Glossaire et unités
description: Comprendre les unités audio et le vocabulaire de CyDAW.
section: Manuel / Référence
order: 35
sourceRevision: c0159b7901fa88ac
---

## Vue d’ensemble {#overview}

Ces mots décrivent des couches distinctes d'une session. Lire unité et contexte avant de copier une valeur : index brut, pourcentage et valeur musicale affichée peuvent représenter différemment un même choix.


~~~image
{"id": "figure-midi-voice", "src": "assets/midi-voice-fr.svg", "alt": "Événements et contrôles diffèrent de l'audio qu'ils façonnent.", "title": "Événements et contrôles diffèrent de l'audio qu'ils façonnent.", "controls": "minimal"}
~~~

*Événements et contrôles diffèrent de l'audio qu'ils façonnent.*


## Signal et structure {#structure}
| Terme | Sens |
| --- | --- |
| Audio | Échantillons sonores lus dans les canaux de sortie. |
| MIDI | Événements de notes/contrôles, pas du son enregistré. |
| Voix | État d'une note active de l'instrument. |
| Clip | Référence temporisée vers audio, événements ou notes. |
| Piste | Lecture des clips, instrument/effets et routage. |
| Bus | Route qui reçoit d'autres pistes. |
| Conteneur | Branches d'effets, couches d'instruments ou sous-pistes temporelles Cinéma selon le contexte. |
| Master | Point de mix/traitement final de session ; MASTER tempo DJ est un autre rôle. |
| Sidechain | Source de détection/contrôle, souvent pour la dynamique. |
| Envoi/retour | Chemin supplémentaire et sa destination, souvent parallèle. |
| Preset/programme | Réglages mémorisés d'appareil, chaîne ou plugin. |
| Définition/instance | Graphe réutilisable ou ses valeurs d'une utilisation. |
| Sec/mouillé | Contribution non traitée/traitée. |

## Temps, niveaux et mesures {#units}
| Unité | Lecture |
| --- | --- |
| Hz / kHz | Cycles par seconde / milliers de cycles. |
| ms / s | Millisecondes / secondes ; 1000 ms = 1 s. |
| BPM | Noires par minute dans le contexte du tempo. |
| Temps/mesure | Temps musical dépendant de tempo/signature. |
| Échantillon/frame | Position discrète audio ou vidéo selon contexte. |
| fps | Images vidéo par seconde, pas fréquence audio. |
| dB | Gain relatif logarithmique. |
| dBFS | Niveau numérique relatif au maximum. |
| RMS / crête | Niveau lié à l'énergie / magnitude instantanée maximale. |
| True peak | Estimation des crêtes entre échantillons. |
| LUFS | Sonie avec pondération/portes selon la mesure. |
| Q | Résonance ou largeur de bande de filtre/EQ. |
| % | Fraction affichée ; le moteur peut normaliser autrement. |

## Lire la référence générée {#reference}
Les tableaux gardent clés moteur et minimum/défaut/maximum du registre en exécution. Ce sont des nombres de schéma, pas la promesse d'une même unité ou échelle d'affichage. Les choix demandent leurs libellés UI ; un contrôle logarithmique n'est pas une fraction linéaire de fréquence. Les ports donnent sens, type et canaux déclarés. Leur nombre ne garantit pas les frontières exportables.

Voir [instruments](doc:reference-instruments), [effets](doc:reference-effects) et [nœuds/contrôles](doc:reference-nodes).
