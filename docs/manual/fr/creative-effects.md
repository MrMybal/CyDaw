# Effets créatifs, MIDI et utilitaires

Le traitement créatif change temps, hauteur, spectre ou espace. Ajouter une transformation à la fois en gardant une référence. Un effet audio demande de l'audio ; un effet MIDI demande notes/contrôles avant l'instrument.

![Séparer événements/contrôles et chemin audio audible.](../assets/midi-voice-fr.svg)

## Transformations audio {#audio}
Vocodeur impose le spectre d'un modulateur à une porteuse ; vérifier les deux signaux nécessaires. Harmonie, Justesse et Décaleur ont des fonctions différentes de hauteur/harmonisation/fréquence. Dé-esseur réduit les sifflantes ; Pompe anime le niveau. Empreinte utilise une impulsion, Givre tient le spectre, Grains granule, Saccade répète en rythme et Boucleur capture. Anneau module en anneau, Rotor fait tourner, Résonateurs/Corps soulignent les objets et Peigne filtre en peigne.

Chorus, Flanger et Phaser ont des mouvements de retard/phase distincts. Trémolo change le niveau ; Auto-pan déplace le panoramique. Bitcrusher réduit la résolution ; Gate, Limiteur et Transitoires règlent la dynamique. L'écho ping-pong alterne ses retours. Comparer la stéréo en mono et maîtriser le feedback.

## Traitement MIDI {#midi}

Arpège crée un motif depuis les notes tenues ; Accords harmonise ; Gamme contraint la hauteur. Écho de notes répète, Aléa varie, Durée modifie les longueurs, Séquenceur fournit un motif et Roulement répète vite. Les anciens nœuds transposition, filtre de notes et vélocité existent aussi. Observer entrée/sortie pour diagnostiquer notes hors plage, fins retardées et transpositions. Il faut un instrument derrière pour produire l'audio.

## Utilitaires et nœuds simples {#utilities}
Utilitaire règle polarité, gain et fonctions stéréo. Accordeur mesure la justesse ; Stéréoscope montre l'espace. Conversions mono/stéréo, suppression DC, filtres, gain/pan, largeur et Mixeur 4 construisent les chaînes simples. Spectre, oscilloscope, VU et moniteur MIDI observent sans améliorer automatiquement. LFO, macros, arithmétique, logique et valeurs du transport produisent le contrôle : relier des ports compatibles.

Voir les [graphes](graphs.md), les [modulateurs](modulators.md) et la [référence des nœuds](reference-nodes.md).
