---
id: reference-effects
title: Référence des paramètres d’effets
description: Consulter les plages, valeurs par défaut et ports des effets natifs.
section: Manuel / Référence
parent: studio-effects
order: 38
sourceRevision: 999c61348685a603
---

## Vue d’ensemble {#overview}

Référence générée depuis le registre CyDAW 0.78.0. Chaque section décrit un type natif, pas un preset de piste ni un plugin externe. Utiliser la recherche pour trouver une clé, une plage ou un port.


~~~image
{"id": "figure-graph", "src": "assets/graph-fr.png", "alt": "Le graphe relie les ports typés et leurs paramètres.", "title": "Le graphe relie les ports typés et leurs paramètres.", "controls": "minimal"}
~~~

*Le graphe relie les ports typés et leurs paramètres.*


Les nombres sont les valeurs brutes du schéma moteur. Les unités affichées, échelles logarithmiques, choix et réglages de fichiers/tableaux se lisent dans le panneau du module ; ils ne se déduisent pas de ces nombres. Les défauts décrivent un nœud neuf, pas forcément le preset chargé. Un port déclaré ne garantit pas une frontière de plugin exportable.

Voir le [glossaire](doc:glossary#reference), les [instruments](doc:synths) et les [effets](doc:studio-effects) pour la méthode d’écoute. Pour les limites lire [export](doc:plugin-formats#limits).

## AMP (amp-sim) {#amp-sim}

Catégorie : DSP / Effets.

Simulateur d'ampli : noise gate, pédale (boost, overdrive, fuzz), préampli à lampes, tone stack passif (basses, médiums, aigus), étage de puissance avec sag, présence et profondeur ; huit modèles, du clean américain au gain moderne et aux amplis basse. Mettez Baffle et micro après lui.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain` | 0 | 5 | 10 |
| `bass` | 0 | 5 | 10 |
| `middle` | 0 | 5 | 10 |
| `treble` | 0 | 5 | 10 |
| `presence` | 0 | 5 | 10 |
| `master` | 0 | 5 | 10 |
| `output` | -24 | 0 | 12 |
| `mix` | 0 | 1 | 1 |
| `model` | 0 | 1 | 7 |
| `bright` | 0 | 0 | 1 |
| `depth` | 0 | 5 | 10 |
| `sag` | 0 | 0.300000012 | 1 |
| `tight` | 20 | 20 | 400 |
| `gate` | -90 | -90 | -20 |
| `pedal` | 0 | 0 | 3 |
| `pedal-drive` | 0 | 0.5 | 1 |
| `pedal-tone` | 0 | 0.5 | 1 |
| `pedal-level` | -12 | 0 | 12 |
| `oversampling` | 0 | 2 | 2 |
| `channel` | 0 | 0 | 1 |
| `model-b` | 0 | 2 | 7 |
| `gain-b` | 0 | 7 | 10 |
| `bass-b` | 0 | 5 | 10 |
| `middle-b` | 0 | 6 | 10 |
| `treble-b` | 0 | 5.5 | 10 |
| `presence-b` | 0 | 6 | 10 |
| `master-b` | 0 | 5 | 10 |
| `bright-b` | 0 | 0 | 1 |
| `depth-b` | 0 | 5 | 10 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| bass / `bass` | Entrée | value | 1 |
| middle / `middle` | Entrée | value | 1 |
| treble / `treble` | Entrée | value | 1 |
| presence / `presence` | Entrée | value | 1 |
| master / `master` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| model / `model` | Entrée | value | 1 |
| bright / `bright` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| sag / `sag` | Entrée | value | 1 |
| tight / `tight` | Entrée | value | 1 |
| gate / `gate` | Entrée | value | 1 |
| pedal / `pedal` | Entrée | value | 1 |
| pedal-drive / `pedal-drive` | Entrée | value | 1 |
| pedal-tone / `pedal-tone` | Entrée | value | 1 |
| pedal-level / `pedal-level` | Entrée | value | 1 |
| oversampling / `oversampling` | Entrée | value | 1 |
| channel / `channel` | Entrée | value | 1 |
| model-b / `model-b` | Entrée | value | 1 |
| gain-b / `gain-b` | Entrée | value | 1 |
| bass-b / `bass-b` | Entrée | value | 1 |
| middle-b / `middle-b` | Entrée | value | 1 |
| treble-b / `treble-b` | Entrée | value | 1 |
| presence-b / `presence-b` | Entrée | value | 1 |
| master-b / `master-b` | Entrée | value | 1 |
| bright-b / `bright-b` | Entrée | value | 1 |
| depth-b / `depth-b` | Entrée | value | 1 |

## ANNEAU (anneau) {#anneau}

Catégorie : DSP / Effets.

Multiplie le son par une onde : cloches, voix de robot, trémolo très rapide. Fréquence, forme de l'onde, modulation en anneau ou d'amplitude, saturation, LFO (vitesse, profondeur en octaves, phase stéréo), coupe-bas à l'entrée.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 1 | 220 | 5000 |
| `drive` | 0 | 0 | 24 |
| `lfo-rate` | 0.00999999978 | 0.5 | 10 |
| `lfo-depth` | 0 | 0 | 2 |
| `mix` | 0 | 0.5 | 1 |
| `output` | -24 | 0 | 24 |
| `stereo` | 0 | 0 | 1 |
| `shape` | 0 | 0 | 3 |
| `mode` | 0 | 0 | 1 |
| `sync` | 0 | 0 | 12 |
| `low-cut` | 20 | 20 | 2000 |
| `lfo-shape` | 0 | 0 | 3 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| lfo-rate / `lfo-rate` | Entrée | value | 1 |
| lfo-depth / `lfo-depth` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| stereo / `stereo` | Entrée | value | 1 |
| shape / `shape` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| lfo-shape / `lfo-shape` | Entrée | value | 1 |

## AUDIO SUM (audio-sum) {#audio-sum}

Catégorie : DSP / Utilitaires audio.

Aucun paramètre numérique déclaré par ce schéma.

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## AUTO FILTER (auto-filter) {#auto-filter}

Catégorie : DSP / Filtres et EQ.

Un passe-bas résonant dont la coupure est balayée par un LFO : effet wah, wobble.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `cutoff` | 40 | 800 | 15000 |
| `resonance` | 0.300000012 | 2 | 10 |
| `rate` | 0.0500000007 | 1 | 16 |
| `depth` | 0 | 0.5 | 1 |
| `sync` | 0 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| cutoff / `cutoff` | Entrée | value | 1 |
| resonance / `resonance` | Entrée | value | 1 |
| rate / `rate` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |

## BITCRUSHER (bitcrusher) {#bitcrusher}

Catégorie : DSP / Effets.

Moins de bits et une fréquence d'échantillonnage réduite : son numérique rugueux, lo-fi.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `bits` | 1 | 8 | 16 |
| `downsample` | 1 | 4 | 32 |
| `drive` | 0 | 0 | 24 |
| `mix` | 0 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| bits / `bits` | Entrée | value | 1 |
| downsample / `downsample` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## LOOPER (boucleur) {#boucleur}

Catégorie : DSP / Effets.

Enregistre ce qui entre et le rejoue en boucle, puis superpose de nouvelles couches : enregistrer, jouer, superposer, arrêter, effacer ; longueur libre ou en mesures calées sur le tempo, départ sur la mesure, demi-vitesse ou double vitesse, à l'envers.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `feedback` | 0 | 1 | 1 |
| `level` | 0 | 1 | 1 |
| `dry` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `state` | 0 | 0 | 3 |
| `bars` | 0 | 0 | 5 |
| `quantize` | 0 | 1 | 1 |
| `speed-mode` | 0 | 1 | 2 |
| `reverse` | 0 | 0 | 1 |
| `clear` | 0 | 0 | 100 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| feedback / `feedback` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| dry / `dry` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| state / `state` | Entrée | value | 1 |
| bars / `bars` | Entrée | value | 1 |
| quantize / `quantize` | Entrée | value | 1 |
| speed-mode / `speed-mode` | Entrée | value | 1 |
| reverse / `reverse` | Entrée | value | 1 |
| clear / `clear` | Entrée | value | 1 |

## CABINET AND MIC (cab-sim) {#cab-sim}

Catégorie : DSP / Effets.

Une enceinte de guitare ou de basse prise par un ou deux micros : le baffle (1x12 ouvert à 8x10 basse), le type de chaque micro, sa place sur le haut-parleur (centre ou bord), sa distance et son angle, le mélange des deux, leur phase, la pièce.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `position-a` | 0 | 0.300000012 | 1 |
| `distance-a` | 1 | 3 | 100 |
| `position-b` | 0 | 0.600000024 | 1 |
| `distance-b` | 1 | 10 | 100 |
| `blend` | 0 | 0.5 | 1 |
| `room` | 0 | 0.100000001 | 1 |
| `low-cut` | 20 | 20 | 400 |
| `output` | -24 | 0 | 24 |
| `model` | 0 | 3 | 7 |
| `mic-a` | 0 | 0 | 6 |
| `angle-a` | 0 | 0 | 90 |
| `mic-b` | 0 | 2 | 6 |
| `angle-b` | 0 | 0 | 90 |
| `mic-b-on` | 0 | 0 | 1 |
| `polarity-b` | 0 | 0 | 1 |
| `align` | 0 | 1 | 1 |
| `high-cut` | 2000 | 20000 | 20000 |
| `resonance` | 0 | 0.5 | 1 |
| `stereo` | 0 | 0 | 1 |
| `mix` | 0 | 1 | 1 |
| `room-size` | 0 | 0.25 | 1 |
| `room-tone` | 1000 | 5000 | 12000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| position-a / `position-a` | Entrée | value | 1 |
| distance-a / `distance-a` | Entrée | value | 1 |
| position-b / `position-b` | Entrée | value | 1 |
| distance-b / `distance-b` | Entrée | value | 1 |
| blend / `blend` | Entrée | value | 1 |
| room / `room` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| model / `model` | Entrée | value | 1 |
| mic-a / `mic-a` | Entrée | value | 1 |
| angle-a / `angle-a` | Entrée | value | 1 |
| mic-b / `mic-b` | Entrée | value | 1 |
| angle-b / `angle-b` | Entrée | value | 1 |
| mic-b-on / `mic-b-on` | Entrée | value | 1 |
| polarity-b / `polarity-b` | Entrée | value | 1 |
| align / `align` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| resonance / `resonance` | Entrée | value | 1 |
| stereo / `stereo` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| room-size / `room-size` | Entrée | value | 1 |
| room-tone / `room-tone` | Entrée | value | 1 |

## CHORUS (chorus) {#chorus}

Catégorie : DSP / Effets.

Double le son avec des copies légèrement retardées et modulées : épaisseur et largeur stéréo.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.0500000007 | 0.600000024 | 8 |
| `depth` | 0 | 0.5 | 1 |
| `delay` | 2 | 14 | 40 |
| `width` | 0 | 0.800000012 | 1 |
| `mix` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| rate / `rate` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| delay / `delay` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## PRO COMPRESSOR (comp-pro) {#comp-pro}

Catégorie : DSP / Dynamique.

Compresseur de studio : huit styles (Clean, Classic, Opto, Voix, Mastering, Bus, Punch, Pompage), genou, plage, attaque, relâchement (automatique ou non), maintien, anticipation, détecteur filtré (passe-haut et passe-bas, à écouter), lien stéréo ou mid/side, gain de compensation automatique et mélange pour la compression parallèle.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold` | -60 | -18 | 0 |
| `ratio` | 1 | 4 | 20 |
| `knee` | 0 | 6 | 24 |
| `attack` | 0.0500000007 | 10 | 250 |
| `release` | 5 | 120 | 2500 |
| `makeup` | -12 | 0 | 24 |
| `mix` | 0 | 1 | 1 |
| `sidechain` | 20 | 20 | 500 |
| `style` | 0 | 0 | 7 |
| `range` | 0 | 60 | 60 |
| `hold` | 0 | 0 | 500 |
| `lookahead` | 0 | 0 | 20 |
| `auto-release` | 0 | 0 | 1 |
| `auto-gain` | 0 | 0 | 1 |
| `sidechain-low` | 500 | 20000 | 20000 |
| `link` | 0 | 1 | 1 |
| `channels` | 0 | 0 | 2 |
| `audition` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold / `threshold` | Entrée | value | 1 |
| ratio / `ratio` | Entrée | value | 1 |
| knee / `knee` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| makeup / `makeup` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| sidechain / `sidechain` | Entrée | value | 1 |
| style / `style` | Entrée | value | 1 |
| range / `range` | Entrée | value | 1 |
| hold / `hold` | Entrée | value | 1 |
| lookahead / `lookahead` | Entrée | value | 1 |
| auto-release / `auto-release` | Entrée | value | 1 |
| auto-gain / `auto-gain` | Entrée | value | 1 |
| sidechain-low / `sidechain-low` | Entrée | value | 1 |
| link / `link` | Entrée | value | 1 |
| channels / `channels` | Entrée | value | 1 |
| audition / `audition` | Entrée | value | 1 |
| sidechain-in / `sidechain-in` | Entrée | audio | 2 |

## COMPRESSOR (compressor) {#compressor}

Catégorie : DSP / Dynamique.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold` | -80 | -18 | 0 |
| `ratio` | 1 | 4 | 100 |
| `attack` | 0.0001 | 0.01 | 1 |
| `release` | 0.001 | 0.1 | 5 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| sidechain / `sidechain` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold / `threshold` | Entrée | value | 1 |
| ratio / `ratio` | Entrée | value | 1 |
| reduction / `reduction` | Sortie | value | 1 |

## CORPS (corps) {#corps}

Catégorie : DSP / Effets.

Fait résonner le son dans un objet : poutre, marimba, corde, membrane, plaque, tuyau ou tube. Accord, durée, matériau, inharmonicité, position d'écoute, largeur, coupe-bas à l'entrée.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `tune` | 40 | 220 | 2000 |
| `decay` | 0 | 0.5 | 1 |
| `material` | 0 | 0.5 | 1 |
| `inharmonic` | -1 | 0 | 1 |
| `position` | 0 | 0.300000012 | 1 |
| `mix` | 0 | 0.5 | 1 |
| `output` | -24 | 0 | 24 |
| `width` | 0 | 0.5 | 1 |
| `body-type` | 0 | 0 | 6 |
| `low-cut` | 20 | 80 | 2000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| tune / `tune` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| material / `material` | Entrée | value | 1 |
| inharmonic / `inharmonic` | Entrée | value | 1 |
| position / `position` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| body-type / `body-type` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |

## DC BLOCKER (dc-blocker) {#dc-blocker}

Catégorie : DSP / Filtres et EQ.

Aucun paramètre numérique déclaré par ce schéma.

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## DE-ESSER (de-esser) {#de-esser}

Catégorie : DSP / Dynamique.

Calme les « s » et les « ch » trop forts d'une voix, seulement quand ils passent le seuil : fréquence, seuil, plage, relâchement, réduction de la seule bande ou de tout le son, détection passe-haut ou en bande, et l'écoute de ce que le détecteur entend.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold` | -60 | -28 | 0 |
| `frequency` | 2000 | 6000 | 12000 |
| `range` | 0 | 10 | 24 |
| `release` | 10 | 60 | 300 |
| `output` | -24 | 0 | 24 |
| `mode` | 0 | 0 | 1 |
| `detector-type` | 0 | 0 | 1 |
| `listen` | 0 | 0 | 1 |
| `bandwidth` | 0.300000012 | 1 | 3 |
| `attack` | 0.100000001 | 1 | 20 |
| `link` | 0 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold / `threshold` | Entrée | value | 1 |
| frequency / `frequency` | Entrée | value | 1 |
| range / `range` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| detector-type / `detector-type` | Entrée | value | 1 |
| listen / `listen` | Entrée | value | 1 |
| bandwidth / `bandwidth` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| link / `link` | Entrée | value | 1 |

## DECALEUR (decaleur) {#decaleur}

Catégorie : DSP / Effets.

Décale toutes les fréquences du son du même nombre de hertz (et non d'un intervalle) : sons métalliques, battements, spirales sans fin avec la rétroaction. Décalage, réglage fin, écart stéréo, LFO, une bande, deux bandes ou les deux côtés opposés.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `shift` | -2000 | 100 | 2000 |
| `fine` | -50 | 0 | 50 |
| `spread` | 0 | 0 | 200 |
| `feedback` | 0 | 0 | 0.949999988 |
| `lfo-rate` | 0.00999999978 | 0.5 | 10 |
| `lfo-depth` | 0 | 0 | 1000 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `mode` | 0 | 0 | 2 |
| `sync` | 0 | 0 | 12 |
| `lfo-shape` | 0 | 0 | 3 |
| `delay` | 0 | 0 | 1000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| shift / `shift` | Entrée | value | 1 |
| fine / `fine` | Entrée | value | 1 |
| spread / `spread` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| lfo-rate / `lfo-rate` | Entrée | value | 1 |
| lfo-depth / `lfo-depth` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| lfo-shape / `lfo-shape` | Entrée | value | 1 |
| delay / `delay` | Entrée | value | 1 |

## DELAY (delay) {#delay}

Catégorie : DSP / Effets.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `time` | 1 | 250 | 10000 |
| `feedback` | 0 | 0.25 | 0.999 |
| `mix` | 0 | 0.25 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| time / `time` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## DRIVE (drive) {#drive}

Catégorie : DSP / Effets.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `drive-db` | 0 | 0 | 36 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| drive / `drive` | Entrée | value | 1 |

## ECHO BANDE (echo-bande) {#echo-bande}

Catégorie : DSP / Effets.

Un écho à bande : trois têtes de lecture combinées en douze modes, un ressort de réverbération, l'intensité qui peut s'emballer, le pleurage, l'usure de la bande, les basses et les aigus des échos, le temps libre ou calé sur le tempo.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `time` | 30 | 120 | 350 |
| `intensity` | 0 | 0.449999988 | 1.20000005 |
| `echo` | 0 | 0.699999988 | 1 |
| `reverb` | 0 | 0.300000012 | 1 |
| `bass` | -12 | 0 | 12 |
| `treble` | -12 | 0 | 12 |
| `wow` | 0 | 0.300000012 | 1 |
| `mix` | 0 | 0.5 | 1 |
| `mode` | 0 | 4 | 11 |
| `sync` | 0 | 0 | 12 |
| `drive` | 0 | 0 | 24 |
| `age` | 0 | 0.300000012 | 1 |
| `width` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| time / `time` | Entrée | value | 1 |
| intensity / `intensity` | Entrée | value | 1 |
| echo / `echo` | Entrée | value | 1 |
| reverb / `reverb` | Entrée | value | 1 |
| bass / `bass` | Entrée | value | 1 |
| treble / `treble` | Entrée | value | 1 |
| wow / `wow` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| age / `age` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |

## EMPREINTE (empreinte) {#empreinte}

Catégorie : DSP / Effets.

Une réverbération par convolution : la réponse d'un vrai lieu, chargée d'un fichier (jusqu'à huit secondes), ou l'un des dix espaces intégrés (pièce, studio, salles, cathédrale, plaque, ressort, chambre d'écho, tunnel, grotte). Longueur, à l'envers, pré-délai, niveaux du début et de la queue, coupe-bas, coupe-haut, largeur.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 0.300000012 | 1 |
| `predelay` | 0 | 0 | 250 |
| `low-cut` | 10 | 10 | 1000 |
| `high-cut` | 1000 | 20000 | 20000 |
| `width` | 0 | 1 | 1 |
| `early` | -24 | 0 | 6 |
| `tail` | -24 | 0 | 6 |
| `output` | -24 | 0 | 24 |
| `space-type` | 0 | 2 | 9 |
| `ir-length` | 0.100000001 | 1 | 1 |
| `reverse` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| mix / `mix` | Entrée | value | 1 |
| predelay / `predelay` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| early / `early` | Entrée | value | 1 |
| tail / `tail` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| space-type / `space-type` | Entrée | value | 1 |
| ir-length / `ir-length` | Entrée | value | 1 |
| reverse / `reverse` | Entrée | value | 1 |

## PARAMETRIC EQ (eq) {#eq}

Catégorie : DSP / Filtres et EQ.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `tone` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| tone / `tone` | Entrée | value | 1 |

## EQ BAND (eq-band) {#eq-band}

Catégorie : DSP / Filtres et EQ.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 1000 | 20000 |
| `gain` | -30 | 0 | 30 |
| `q` | 0.1 | 1 | 18 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| gain / `gain` | Entrée | value | 1 |
| q / `q` | Entrée | value | 1 |

## PRO EQ (eq-pro) {#eq-pro}

Catégorie : DSP / Filtres et EQ.

Égaliseur paramétrique 8 bandes : cloches, plateaux, coupe-bas et coupe-haut jusqu'à 48 dB/oct, réjecteur, passe-bande et inclinaison, avec spectre avant/après.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `b1-enabled` | 0 | 0 | 1 |
| `b1-type` | 0 | 0 | 7 |
| `b1-frequency` | 10 | 40 | 22000 |
| `b1-gain` | -30 | 0 | 30 |
| `b1-q` | 0.100000001 | 1 | 30 |
| `b1-slope` | 0 | 1 | 3 |
| `b2-enabled` | 0 | 0 | 1 |
| `b2-type` | 0 | 0 | 7 |
| `b2-frequency` | 10 | 120 | 22000 |
| `b2-gain` | -30 | 0 | 30 |
| `b2-q` | 0.100000001 | 1 | 30 |
| `b2-slope` | 0 | 1 | 3 |
| `b3-enabled` | 0 | 0 | 1 |
| `b3-type` | 0 | 0 | 7 |
| `b3-frequency` | 10 | 350 | 22000 |
| `b3-gain` | -30 | 0 | 30 |
| `b3-q` | 0.100000001 | 1 | 30 |
| `b3-slope` | 0 | 1 | 3 |
| `b4-enabled` | 0 | 0 | 1 |
| `b4-type` | 0 | 0 | 7 |
| `b4-frequency` | 10 | 900 | 22000 |
| `b4-gain` | -30 | 0 | 30 |
| `b4-q` | 0.100000001 | 1 | 30 |
| `b4-slope` | 0 | 1 | 3 |
| `b5-enabled` | 0 | 0 | 1 |
| `b5-type` | 0 | 0 | 7 |
| `b5-frequency` | 10 | 2200 | 22000 |
| `b5-gain` | -30 | 0 | 30 |
| `b5-q` | 0.100000001 | 1 | 30 |
| `b5-slope` | 0 | 1 | 3 |
| `b6-enabled` | 0 | 0 | 1 |
| `b6-type` | 0 | 0 | 7 |
| `b6-frequency` | 10 | 5000 | 22000 |
| `b6-gain` | -30 | 0 | 30 |
| `b6-q` | 0.100000001 | 1 | 30 |
| `b6-slope` | 0 | 1 | 3 |
| `b7-enabled` | 0 | 0 | 1 |
| `b7-type` | 0 | 0 | 7 |
| `b7-frequency` | 10 | 10000 | 22000 |
| `b7-gain` | -30 | 0 | 30 |
| `b7-q` | 0.100000001 | 1 | 30 |
| `b7-slope` | 0 | 1 | 3 |
| `b8-enabled` | 0 | 0 | 1 |
| `b8-type` | 0 | 0 | 7 |
| `b8-frequency` | 10 | 16000 | 22000 |
| `b8-gain` | -30 | 0 | 30 |
| `b8-q` | 0.100000001 | 1 | 30 |
| `b8-slope` | 0 | 1 | 3 |
| `output` | -24 | 0 | 24 |
| `listen` | 0 | 0 | 8 |
| `b1-dynamic` | 0 | 0 | 1 |
| `b1-range` | -30 | -6 | 30 |
| `b1-threshold` | -60 | -24 | 0 |
| `b1-attack` | 0.5 | 10 | 200 |
| `b1-release` | 10 | 120 | 2000 |
| `b1-detector` | 0 | 0 | 1 |
| `b2-dynamic` | 0 | 0 | 1 |
| `b2-range` | -30 | -6 | 30 |
| `b2-threshold` | -60 | -24 | 0 |
| `b2-attack` | 0.5 | 10 | 200 |
| `b2-release` | 10 | 120 | 2000 |
| `b2-detector` | 0 | 0 | 1 |
| `b3-dynamic` | 0 | 0 | 1 |
| `b3-range` | -30 | -6 | 30 |
| `b3-threshold` | -60 | -24 | 0 |
| `b3-attack` | 0.5 | 10 | 200 |
| `b3-release` | 10 | 120 | 2000 |
| `b3-detector` | 0 | 0 | 1 |
| `b4-dynamic` | 0 | 0 | 1 |
| `b4-range` | -30 | -6 | 30 |
| `b4-threshold` | -60 | -24 | 0 |
| `b4-attack` | 0.5 | 10 | 200 |
| `b4-release` | 10 | 120 | 2000 |
| `b4-detector` | 0 | 0 | 1 |
| `b5-dynamic` | 0 | 0 | 1 |
| `b5-range` | -30 | -6 | 30 |
| `b5-threshold` | -60 | -24 | 0 |
| `b5-attack` | 0.5 | 10 | 200 |
| `b5-release` | 10 | 120 | 2000 |
| `b5-detector` | 0 | 0 | 1 |
| `b6-dynamic` | 0 | 0 | 1 |
| `b6-range` | -30 | -6 | 30 |
| `b6-threshold` | -60 | -24 | 0 |
| `b6-attack` | 0.5 | 10 | 200 |
| `b6-release` | 10 | 120 | 2000 |
| `b6-detector` | 0 | 0 | 1 |
| `b7-dynamic` | 0 | 0 | 1 |
| `b7-range` | -30 | -6 | 30 |
| `b7-threshold` | -60 | -24 | 0 |
| `b7-attack` | 0.5 | 10 | 200 |
| `b7-release` | 10 | 120 | 2000 |
| `b7-detector` | 0 | 0 | 1 |
| `b8-dynamic` | 0 | 0 | 1 |
| `b8-range` | -30 | -6 | 30 |
| `b8-threshold` | -60 | -24 | 0 |
| `b8-attack` | 0.5 | 10 | 200 |
| `b8-release` | 10 | 120 | 2000 |
| `b8-detector` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| output / `output` | Entrée | value | 1 |
| listen / `listen` | Entrée | value | 1 |
| b1-enabled / `b1-enabled` | Entrée | value | 1 |
| b1-type / `b1-type` | Entrée | value | 1 |
| b1-frequency / `b1-frequency` | Entrée | value | 1 |
| b1-gain / `b1-gain` | Entrée | value | 1 |
| b1-q / `b1-q` | Entrée | value | 1 |
| b1-slope / `b1-slope` | Entrée | value | 1 |
| b2-enabled / `b2-enabled` | Entrée | value | 1 |
| b2-type / `b2-type` | Entrée | value | 1 |
| b2-frequency / `b2-frequency` | Entrée | value | 1 |
| b2-gain / `b2-gain` | Entrée | value | 1 |
| b2-q / `b2-q` | Entrée | value | 1 |
| b2-slope / `b2-slope` | Entrée | value | 1 |
| b3-enabled / `b3-enabled` | Entrée | value | 1 |
| b3-type / `b3-type` | Entrée | value | 1 |
| b3-frequency / `b3-frequency` | Entrée | value | 1 |
| b3-gain / `b3-gain` | Entrée | value | 1 |
| b3-q / `b3-q` | Entrée | value | 1 |
| b3-slope / `b3-slope` | Entrée | value | 1 |
| b4-enabled / `b4-enabled` | Entrée | value | 1 |
| b4-type / `b4-type` | Entrée | value | 1 |
| b4-frequency / `b4-frequency` | Entrée | value | 1 |
| b4-gain / `b4-gain` | Entrée | value | 1 |
| b4-q / `b4-q` | Entrée | value | 1 |
| b4-slope / `b4-slope` | Entrée | value | 1 |
| b5-enabled / `b5-enabled` | Entrée | value | 1 |
| b5-type / `b5-type` | Entrée | value | 1 |
| b5-frequency / `b5-frequency` | Entrée | value | 1 |
| b5-gain / `b5-gain` | Entrée | value | 1 |
| b5-q / `b5-q` | Entrée | value | 1 |
| b5-slope / `b5-slope` | Entrée | value | 1 |
| b6-enabled / `b6-enabled` | Entrée | value | 1 |
| b6-type / `b6-type` | Entrée | value | 1 |
| b6-frequency / `b6-frequency` | Entrée | value | 1 |
| b6-gain / `b6-gain` | Entrée | value | 1 |
| b6-q / `b6-q` | Entrée | value | 1 |
| b6-slope / `b6-slope` | Entrée | value | 1 |
| b7-enabled / `b7-enabled` | Entrée | value | 1 |
| b7-type / `b7-type` | Entrée | value | 1 |
| b7-frequency / `b7-frequency` | Entrée | value | 1 |
| b7-gain / `b7-gain` | Entrée | value | 1 |
| b7-q / `b7-q` | Entrée | value | 1 |
| b7-slope / `b7-slope` | Entrée | value | 1 |
| b8-enabled / `b8-enabled` | Entrée | value | 1 |
| b8-type / `b8-type` | Entrée | value | 1 |
| b8-frequency / `b8-frequency` | Entrée | value | 1 |
| b8-gain / `b8-gain` | Entrée | value | 1 |
| b8-q / `b8-q` | Entrée | value | 1 |
| b8-slope / `b8-slope` | Entrée | value | 1 |
| b1-dynamic / `b1-dynamic` | Entrée | value | 1 |
| b1-range / `b1-range` | Entrée | value | 1 |
| b1-threshold / `b1-threshold` | Entrée | value | 1 |
| b1-attack / `b1-attack` | Entrée | value | 1 |
| b1-release / `b1-release` | Entrée | value | 1 |
| b1-detector / `b1-detector` | Entrée | value | 1 |
| b2-dynamic / `b2-dynamic` | Entrée | value | 1 |
| b2-range / `b2-range` | Entrée | value | 1 |
| b2-threshold / `b2-threshold` | Entrée | value | 1 |
| b2-attack / `b2-attack` | Entrée | value | 1 |
| b2-release / `b2-release` | Entrée | value | 1 |
| b2-detector / `b2-detector` | Entrée | value | 1 |
| b3-dynamic / `b3-dynamic` | Entrée | value | 1 |
| b3-range / `b3-range` | Entrée | value | 1 |
| b3-threshold / `b3-threshold` | Entrée | value | 1 |
| b3-attack / `b3-attack` | Entrée | value | 1 |
| b3-release / `b3-release` | Entrée | value | 1 |
| b3-detector / `b3-detector` | Entrée | value | 1 |
| b4-dynamic / `b4-dynamic` | Entrée | value | 1 |
| b4-range / `b4-range` | Entrée | value | 1 |
| b4-threshold / `b4-threshold` | Entrée | value | 1 |
| b4-attack / `b4-attack` | Entrée | value | 1 |
| b4-release / `b4-release` | Entrée | value | 1 |
| b4-detector / `b4-detector` | Entrée | value | 1 |
| b5-dynamic / `b5-dynamic` | Entrée | value | 1 |
| b5-range / `b5-range` | Entrée | value | 1 |
| b5-threshold / `b5-threshold` | Entrée | value | 1 |
| b5-attack / `b5-attack` | Entrée | value | 1 |
| b5-release / `b5-release` | Entrée | value | 1 |
| b5-detector / `b5-detector` | Entrée | value | 1 |
| b6-dynamic / `b6-dynamic` | Entrée | value | 1 |
| b6-range / `b6-range` | Entrée | value | 1 |
| b6-threshold / `b6-threshold` | Entrée | value | 1 |
| b6-attack / `b6-attack` | Entrée | value | 1 |
| b6-release / `b6-release` | Entrée | value | 1 |
| b6-detector / `b6-detector` | Entrée | value | 1 |
| b7-dynamic / `b7-dynamic` | Entrée | value | 1 |
| b7-range / `b7-range` | Entrée | value | 1 |
| b7-threshold / `b7-threshold` | Entrée | value | 1 |
| b7-attack / `b7-attack` | Entrée | value | 1 |
| b7-release / `b7-release` | Entrée | value | 1 |
| b7-detector / `b7-detector` | Entrée | value | 1 |
| b8-dynamic / `b8-dynamic` | Entrée | value | 1 |
| b8-range / `b8-range` | Entrée | value | 1 |
| b8-threshold / `b8-threshold` | Entrée | value | 1 |
| b8-attack / `b8-attack` | Entrée | value | 1 |
| b8-release / `b8-release` | Entrée | value | 1 |
| b8-detector / `b8-detector` | Entrée | value | 1 |
| sidechain-in / `sidechain-in` | Entrée | audio | 2 |

## 3-BAND EQ (eq-three) {#eq-three}

Catégorie : DSP / Filtres et EQ.

Graves, médiums et aigus, avec les deux fréquences de séparation réglables.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `low` | -24 | 0 | 12 |
| `mid` | -24 | 0 | 12 |
| `high` | -24 | 0 | 12 |
| `low-frequency` | 50 | 250 | 800 |
| `high-frequency` | 1000 | 3500 | 12000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| low / `low` | Entrée | value | 1 |
| mid / `mid` | Entrée | value | 1 |
| high / `high` | Entrée | value | 1 |
| low-frequency / `low-frequency` | Entrée | value | 1 |
| high-frequency / `high-frequency` | Entrée | value | 1 |

## PRO FILTER (filter-pro) {#filter-pro}

Catégorie : DSP / Filtres et EQ.

Quatre filtres (passe-bas, passe-haut, passe-bande, réjecteur, pic, peigne ; 12 à 48 dB/oct, saturation, écart stéréo) en série, en parallèle ou par paires, modulés par deux LFO, un suiveur d'enveloppe et quatre modulations libres ; on glisse une source sur une coupure pour la moduler.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `f1-on` | 0 | 1 | 1 |
| `f1-type` | 0 | 0 | 5 |
| `f1-slope` | 0 | 1 | 2 |
| `f1-cutoff` | 20 | 2000 | 20000 |
| `f1-resonance` | 0 | 0.200000003 | 1 |
| `f1-drive` | 0 | 0 | 36 |
| `f1-spread` | -2 | 0 | 2 |
| `f2-on` | 0 | 0 | 1 |
| `f2-type` | 0 | 1 | 5 |
| `f2-slope` | 0 | 1 | 2 |
| `f2-cutoff` | 20 | 300 | 20000 |
| `f2-resonance` | 0 | 0.200000003 | 1 |
| `f2-drive` | 0 | 0 | 36 |
| `f2-spread` | -2 | 0 | 2 |
| `f3-on` | 0 | 0 | 1 |
| `f3-type` | 0 | 2 | 5 |
| `f3-slope` | 0 | 1 | 2 |
| `f3-cutoff` | 20 | 1000 | 20000 |
| `f3-resonance` | 0 | 0.300000012 | 1 |
| `f3-drive` | 0 | 0 | 36 |
| `f3-spread` | -2 | 0 | 2 |
| `f4-on` | 0 | 0 | 1 |
| `f4-type` | 0 | 3 | 5 |
| `f4-slope` | 0 | 1 | 2 |
| `f4-cutoff` | 20 | 4000 | 20000 |
| `f4-resonance` | 0 | 0.300000012 | 1 |
| `f4-drive` | 0 | 0 | 36 |
| `f4-spread` | -2 | 0 | 2 |
| `routing` | 0 | 0 | 2 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `lfo1-rate` | 0.00999999978 | 1 | 20 |
| `lfo1-shape` | 0 | 0 | 6 |
| `lfo1-sync` | 0 | 0 | 12 |
| `lfo1-stereo` | 0 | 0 | 0.5 |
| `lfo2-rate` | 0.00999999978 | 0.25 | 20 |
| `lfo2-shape` | 0 | 1 | 6 |
| `lfo2-sync` | 0 | 0 | 12 |
| `lfo2-stereo` | 0 | 0 | 0.5 |
| `follow-attack` | 0.100000001 | 10 | 500 |
| `follow-release` | 1 | 150 | 2000 |
| `follow-gain` | -24 | 0 | 24 |
| `m1-source` | 0 | 0 | 3 |
| `m1-destination` | 0 | 1 | 13 |
| `m1-amount` | -1 | 0 | 1 |
| `m2-source` | 0 | 0 | 3 |
| `m2-destination` | 0 | 0 | 13 |
| `m2-amount` | -1 | 0 | 1 |
| `m3-source` | 0 | 0 | 3 |
| `m3-destination` | 0 | 0 | 13 |
| `m3-amount` | -1 | 0 | 1 |
| `m4-source` | 0 | 0 | 3 |
| `m4-destination` | 0 | 0 | 13 |
| `m4-amount` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| f1-cutoff / `f1-cutoff` | Entrée | value | 1 |
| f1-resonance / `f1-resonance` | Entrée | value | 1 |
| f2-cutoff / `f2-cutoff` | Entrée | value | 1 |
| f2-resonance / `f2-resonance` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| lfo1-rate / `lfo1-rate` | Entrée | value | 1 |
| lfo2-rate / `lfo2-rate` | Entrée | value | 1 |
| f1-on / `f1-on` | Entrée | value | 1 |
| f1-type / `f1-type` | Entrée | value | 1 |
| f1-slope / `f1-slope` | Entrée | value | 1 |
| f1-drive / `f1-drive` | Entrée | value | 1 |
| f1-spread / `f1-spread` | Entrée | value | 1 |
| f2-on / `f2-on` | Entrée | value | 1 |
| f2-type / `f2-type` | Entrée | value | 1 |
| f2-slope / `f2-slope` | Entrée | value | 1 |
| f2-drive / `f2-drive` | Entrée | value | 1 |
| f2-spread / `f2-spread` | Entrée | value | 1 |
| f3-on / `f3-on` | Entrée | value | 1 |
| f3-type / `f3-type` | Entrée | value | 1 |
| f3-slope / `f3-slope` | Entrée | value | 1 |
| f3-cutoff / `f3-cutoff` | Entrée | value | 1 |
| f3-resonance / `f3-resonance` | Entrée | value | 1 |
| f3-drive / `f3-drive` | Entrée | value | 1 |
| f3-spread / `f3-spread` | Entrée | value | 1 |
| f4-on / `f4-on` | Entrée | value | 1 |
| f4-type / `f4-type` | Entrée | value | 1 |
| f4-slope / `f4-slope` | Entrée | value | 1 |
| f4-cutoff / `f4-cutoff` | Entrée | value | 1 |
| f4-resonance / `f4-resonance` | Entrée | value | 1 |
| f4-drive / `f4-drive` | Entrée | value | 1 |
| f4-spread / `f4-spread` | Entrée | value | 1 |
| routing / `routing` | Entrée | value | 1 |
| lfo1-shape / `lfo1-shape` | Entrée | value | 1 |
| lfo1-sync / `lfo1-sync` | Entrée | value | 1 |
| lfo1-stereo / `lfo1-stereo` | Entrée | value | 1 |
| lfo2-shape / `lfo2-shape` | Entrée | value | 1 |
| lfo2-sync / `lfo2-sync` | Entrée | value | 1 |
| lfo2-stereo / `lfo2-stereo` | Entrée | value | 1 |
| follow-attack / `follow-attack` | Entrée | value | 1 |
| follow-release / `follow-release` | Entrée | value | 1 |
| follow-gain / `follow-gain` | Entrée | value | 1 |
| m1-source / `m1-source` | Entrée | value | 1 |
| m1-destination / `m1-destination` | Entrée | value | 1 |
| m1-amount / `m1-amount` | Entrée | value | 1 |
| m2-source / `m2-source` | Entrée | value | 1 |
| m2-destination / `m2-destination` | Entrée | value | 1 |
| m2-amount / `m2-amount` | Entrée | value | 1 |
| m3-source / `m3-source` | Entrée | value | 1 |
| m3-destination / `m3-destination` | Entrée | value | 1 |
| m3-amount / `m3-amount` | Entrée | value | 1 |
| m4-source / `m4-source` | Entrée | value | 1 |
| m4-destination / `m4-destination` | Entrée | value | 1 |
| m4-amount / `m4-amount` | Entrée | value | 1 |

## FINITION (finition) {#finition}

Catégorie : DSP / Effets.

Le mastering en un module : égaliseur, dynamique sur trois bandes, excitateur et image stéréo par bande, maximiseur avec anticipation ; un assistant écoute le morceau et règle le gain pour atteindre la sonie visée (streaming, CD, radio…).

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain` | 0 | 0 | 18 |
| `ceiling` | -3 | -1 | 0 |
| `dynamics` | 0 | 0.300000012 | 1 |
| `excite` | 0 | 0.150000006 | 1 |
| `width` | 0 | 1 | 2 |
| `low` | -6 | 0 | 6 |
| `mid` | -6 | 0 | 6 |
| `high` | -6 | 0 | 6 |
| `eq-on` | 0 | 1 | 1 |
| `dynamics-on` | 0 | 1 | 1 |
| `exciter-on` | 0 | 1 | 1 |
| `imager-on` | 0 | 1 | 1 |
| `maximizer-on` | 0 | 1 | 1 |
| `cross-low` | 60 | 150 | 500 |
| `cross-high` | 1000 | 4000 | 10000 |
| `low-cut` | 10 | 10 | 100 |
| `low-hz` | 40 | 100 | 400 |
| `mid-hz` | 200 | 1500 | 8000 |
| `mid-q` | 0.300000012 | 0.699999988 | 4 |
| `high-hz` | 2000 | 10000 | 16000 |
| `threshold` | -36 | -20 | 0 |
| `ratio` | 1 | 2.5 | 6 |
| `attack` | 1 | 30 | 200 |
| `release` | 20 | 200 | 1000 |
| `low-drive` | 0 | 0.300000012 | 1 |
| `mid-drive` | 0 | 0.5 | 1 |
| `high-drive` | 0 | 0.800000012 | 1 |
| `low-width` | 0 | 0.800000012 | 2 |
| `mid-width` | 0 | 1 | 2 |
| `high-width` | 0 | 1.20000005 | 2 |
| `maximizer-release` | 10 | 80 | 500 |
| `target` | 0 | 0 | 6 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| ceiling / `ceiling` | Entrée | value | 1 |
| dynamics / `dynamics` | Entrée | value | 1 |
| excite / `excite` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| low / `low` | Entrée | value | 1 |
| mid / `mid` | Entrée | value | 1 |
| high / `high` | Entrée | value | 1 |
| eq-on / `eq-on` | Entrée | value | 1 |
| dynamics-on / `dynamics-on` | Entrée | value | 1 |
| exciter-on / `exciter-on` | Entrée | value | 1 |
| imager-on / `imager-on` | Entrée | value | 1 |
| maximizer-on / `maximizer-on` | Entrée | value | 1 |
| cross-low / `cross-low` | Entrée | value | 1 |
| cross-high / `cross-high` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| low-hz / `low-hz` | Entrée | value | 1 |
| mid-hz / `mid-hz` | Entrée | value | 1 |
| mid-q / `mid-q` | Entrée | value | 1 |
| high-hz / `high-hz` | Entrée | value | 1 |
| threshold / `threshold` | Entrée | value | 1 |
| ratio / `ratio` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| low-drive / `low-drive` | Entrée | value | 1 |
| mid-drive / `mid-drive` | Entrée | value | 1 |
| high-drive / `high-drive` | Entrée | value | 1 |
| low-width / `low-width` | Entrée | value | 1 |
| mid-width / `mid-width` | Entrée | value | 1 |
| high-width / `high-width` | Entrée | value | 1 |
| maximizer-release / `maximizer-release` | Entrée | value | 1 |
| target / `target` | Entrée | value | 1 |

## FLANGER (flanger) {#flanger}

Catégorie : DSP / Effets.

Un retard très court balayé et réinjecté : le souffle d'avion des années 70.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.0199999996 | 0.25 | 5 |
| `depth` | 0 | 0.699999988 | 1 |
| `delay` | 0.300000012 | 1.5 | 8 |
| `feedback` | -0.949999988 | 0.5 | 0.949999988 |
| `mix` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| rate / `rate` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| delay / `delay` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## FOURNAISE (fournaise) {#fournaise}

Catégorie : DSP / Effets.

Saturation analogique en cinq caractères (bande, console, classe A, lampe, pentode) : drive, tonalité, coupe-bas (avec sa bosse), coupe-haut, punition (+20 dB), niveau automatique, suréchantillonnage.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `drive` | 0 | 3 | 10 |
| `tone` | -1 | 0 | 1 |
| `low-cut` | 10 | 20 | 1000 |
| `high-cut` | 500 | 20000 | 20000 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 12 |
| `style` | 0 | 0 | 4 |
| `punish` | 0 | 0 | 1 |
| `auto` | 0 | 1 | 1 |
| `thump` | 0 | 0 | 1 |
| `steep` | 0 | 0 | 1 |
| `oversampling` | 0 | 1 | 2 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| drive / `drive` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| style / `style` | Entrée | value | 1 |
| punish / `punish` | Entrée | value | 1 |
| auto / `auto` | Entrée | value | 1 |
| thump / `thump` | Entrée | value | 1 |
| steep / `steep` | Entrée | value | 1 |
| oversampling / `oversampling` | Entrée | value | 1 |

## GAIN (gain) {#gain}

Catégorie : DSP / Utilitaires audio.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain-db` | -120 | 0 | 24 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## GAIN / PAN (gain-pan) {#gain-pan}

Catégorie : DSP / Utilitaires audio.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `pan` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| pan / `pan` | Entrée | value | 1 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## GATE (gate) {#gate}

Catégorie : DSP / Dynamique.

Coupe le son sous le seuil : nettoie le bruit de fond et raccourcit les queues.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold` | -80 | -40 | 0 |
| `attack` | 0.100000001 | 1 | 50 |
| `release` | 5 | 120 | 2000 |
| `range` | -80 | -80 | 0 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold / `threshold` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| range / `range` | Entrée | value | 1 |
| sidechain-in / `sidechain-in` | Entrée | audio | 2 |

## GIVRE (givre) {#givre}

Catégorie : DSP / Effets.

Fige le son dans son spectre et le fait durer, ou le rend flou dans le temps et dans les fréquences : gel, flou, étalement des fréquences, décalage spectral, dispersion des phases, couleur, gel relancé en rythme.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `blur` | 0 | 0.400000006 | 1 |
| `smear` | 0 | 0 | 1 |
| `shift` | -12 | 0 | 12 |
| `tone` | -1 | 0 | 1 |
| `scatter` | 0 | 0.200000003 | 1 |
| `mix` | 0 | 0.5 | 1 |
| `output` | -24 | 0 | 24 |
| `freeze` | 0 | 0 | 1 |
| `sync` | 0 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| blur / `blur` | Entrée | value | 1 |
| smear / `smear` | Entrée | value | 1 |
| shift / `shift` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| scatter / `scatter` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| freeze / `freeze` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |

## GRAINS (grains) {#grains}

Catégorie : DSP / Effets.

Un écho fait de grains : le son retardé découpé en petits morceaux rejoués à une autre hauteur, dispersés dans le temps et dans l'espace, parfois à l'envers. Temps (ou valeur de note), rétroaction, hauteur, hauteur aléatoire, dispersion, densité, taille des grains, gel du tampon.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `time` | 1 | 250 | 2000 |
| `feedback` | 0 | 0.349999994 | 0.949999988 |
| `pitch` | -24 | 0 | 24 |
| `spray` | 0 | 20 | 500 |
| `density` | 1 | 25 | 100 |
| `pitch-random` | 0 | 0 | 12 |
| `mix` | 0 | 0.400000006 | 1 |
| `output` | -24 | 0 | 24 |
| `sync` | 0 | 0 | 12 |
| `grain-size` | 10 | 90 | 500 |
| `stereo` | 0 | 0.600000024 | 1 |
| `reverse-chance` | 0 | 0 | 1 |
| `freeze` | 0 | 0 | 1 |
| `high-cut` | 1000 | 14000 | 20000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| time / `time` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| pitch / `pitch` | Entrée | value | 1 |
| spray / `spray` | Entrée | value | 1 |
| density / `density` | Entrée | value | 1 |
| pitch-random / `pitch-random` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| grain-size / `grain-size` | Entrée | value | 1 |
| stereo / `stereo` | Entrée | value | 1 |
| reverse-chance / `reverse-chance` | Entrée | value | 1 |
| freeze / `freeze` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |

## HALO (halo) {#halo}

Catégorie : DSP / Effets.

Une réverbération de caractère : dix algorithmes (salle de concert, salle brillante, plaque, pièce, chambre, espace aléatoire, espace chorus, ambiance, sanctuaire, non linéaire) en trois couleurs (années 70, années 80, maintenant) ; décroissance des graves à part, modulation, diffusion, pré-délai calé ou non sur le tempo.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 0.300000012 | 1 |
| `decay` | 0.200000003 | 2.5 | 20 |
| `predelay` | 0 | 15 | 250 |
| `size` | 0 | 0.600000024 | 1 |
| `high-cut` | 1000 | 12000 | 20000 |
| `low-cut` | 10 | 80 | 1000 |
| `mod-depth` | 0 | 0.400000006 | 1 |
| `mod-rate` | 0.0500000007 | 0.600000024 | 5 |
| `mode` | 0 | 0 | 9 |
| `colour` | 0 | 2 | 2 |
| `attack` | 0 | 0.5 | 1 |
| `early-diffusion` | 0 | 0.699999988 | 1 |
| `late-diffusion` | 0 | 0.699999988 | 1 |
| `bass-multiplier` | 0.25 | 1.20000005 | 4 |
| `bass-crossover` | 50 | 300 | 1000 |
| `damping` | 1000 | 7000 | 20000 |
| `sync` | 0 | 0 | 12 |
| `width` | 0 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| mix / `mix` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| predelay / `predelay` | Entrée | value | 1 |
| size / `size` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| mod-depth / `mod-depth` | Entrée | value | 1 |
| mod-rate / `mod-rate` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| colour / `colour` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| early-diffusion / `early-diffusion` | Entrée | value | 1 |
| late-diffusion / `late-diffusion` | Entrée | value | 1 |
| bass-multiplier / `bass-multiplier` | Entrée | value | 1 |
| bass-crossover / `bass-crossover` | Entrée | value | 1 |
| damping / `damping` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |

## HARD CLIP (hard-clip) {#hard-clip}

Catégorie : DSP / Effets.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold` | 0.001 | 0.9 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold / `threshold` | Entrée | value | 1 |

## HARMONIE (harmonie) {#harmonie}

Catégorie : DSP / Effets.

Change la hauteur d'un son et lui ajoute une seconde voix : deux voix, chacune son intervalle (deux octaves de part et d'autre), son réglage fin, son niveau et sa place ; le son direct, un écho, trois façons de faire (lisse, voix qui garde ses formants, percussif).

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `shift-a` | -24 | 12 | 24 |
| `fine-a` | -100 | 0 | 100 |
| `level-a` | 0 | 1 | 1 |
| `shift-b` | -24 | 7 | 24 |
| `level-b` | 0 | 0 | 1 |
| `dry` | 0 | 0 | 1 |
| `delay` | 0 | 0 | 500 |
| `output` | -24 | 0 | 24 |
| `window` | 10 | 40 | 120 |
| `mode` | 0 | 1 | 2 |
| `fine-b` | -100 | 0 | 100 |
| `pan-a` | -1 | 0 | 1 |
| `pan-b` | -1 | 0 | 1 |
| `feedback` | 0 | 0 | 0.899999976 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| shift-a / `shift-a` | Entrée | value | 1 |
| fine-a / `fine-a` | Entrée | value | 1 |
| level-a / `level-a` | Entrée | value | 1 |
| shift-b / `shift-b` | Entrée | value | 1 |
| level-b / `level-b` | Entrée | value | 1 |
| dry / `dry` | Entrée | value | 1 |
| delay / `delay` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| window / `window` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| fine-b / `fine-b` | Entrée | value | 1 |
| pan-a / `pan-a` | Entrée | value | 1 |
| pan-b / `pan-b` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |

## HIGH-PASS (highpass) {#highpass}

Catégorie : DSP / Filtres et EQ.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `cutoff` | 10 | 80 | 20000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| cutoff / `cutoff` | Entrée | value | 1 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## JUSTESSE (justesse) {#justesse}

Catégorie : DSP / Effets.

Ramène une voix sur les notes justes de la tonalité et de la gamme choisies, doucement ou d'un coup (l'effet robot à vitesse zéro) : vitesse de correction, quantité, humanisation qui laisse vivre les notes tenues, transposition, plage de voix, sensibilité.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `speed` | 0 | 20 | 400 |
| `amount` | 0 | 1 | 1 |
| `humanize` | 0 | 0 | 1 |
| `transpose` | -12 | 0 | 12 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `key` | 0 | 0 | 11 |
| `scale` | 0 | 0 | 11 |
| `low-pitch` | 50 | 70 | 400 |
| `high-pitch` | 300 | 1000 | 1600 |
| `sensitivity` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| speed / `speed` | Entrée | value | 1 |
| amount / `amount` | Entrée | value | 1 |
| humanize / `humanize` | Entrée | value | 1 |
| transpose / `transpose` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| key / `key` | Entrée | value | 1 |
| scale / `scale` | Entrée | value | 1 |
| low-pitch / `low-pitch` | Entrée | value | 1 |
| high-pitch / `high-pitch` | Entrée | value | 1 |
| sensitivity / `sensitivity` | Entrée | value | 1 |

## LIMITER (limiter) {#limiter}

Catégorie : DSP / Dynamique.

Monte le niveau et empêche les crêtes de dépasser le plafond.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain` | 0 | 0 | 24 |
| `ceiling` | -24 | -0.300000012 | 0 |
| `release` | 1 | 80 | 1000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| ceiling / `ceiling` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |

## PRO LIMITER (limiter-pro) {#limiter-pro}

Catégorie : DSP / Dynamique.

Limiteur de mastering : anticipation, huit styles, true peak (crêtes entre les échantillons), lien stéréo des transitoires et du relâchement, dither avec mise en forme du bruit, et la mesure de loudness (LUFS) à côté de l'historique de la réduction de gain.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain` | 0 | 0 | 30 |
| `output` | -30 | -1 | 0 |
| `lookahead` | 0 | 1 | 5 |
| `attack` | 0.100000001 | 5 | 100 |
| `release` | 1 | 100 | 1000 |
| `style` | 0 | 0 | 7 |
| `link-transients` | 0 | 0.75 | 1 |
| `link-release` | 0 | 1 | 1 |
| `true-peak` | 0 | 1 | 1 |
| `oversampling` | 0 | 1 | 3 |
| `dither` | 0 | 0 | 4 |
| `shaping` | 0 | 1 | 2 |
| `dc-filter` | 0 | 0 | 1 |
| `unity` | 0 | 0 | 1 |
| `target` | -30 | -14 | -5 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| lookahead / `lookahead` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| style / `style` | Entrée | value | 1 |
| link-transients / `link-transients` | Entrée | value | 1 |
| link-release / `link-release` | Entrée | value | 1 |
| true-peak / `true-peak` | Entrée | value | 1 |
| oversampling / `oversampling` | Entrée | value | 1 |
| dither / `dither` | Entrée | value | 1 |
| shaping / `shaping` | Entrée | value | 1 |
| dc-filter / `dc-filter` | Entrée | value | 1 |
| unity / `unity` | Entrée | value | 1 |
| target / `target` | Entrée | value | 1 |

## LOW-PASS (lowpass) {#lowpass}

Catégorie : DSP / Filtres et EQ.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `cutoff` | 20 | 1000 | 20000 |
| `resonance` | 0.1 | 0.707 | 20 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| cutoff / `cutoff` | Entrée | value | 1 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| resonance / `resonance` | Entrée | value | 1 |

## MICROPHONE (mic-sim) {#mic-sim}

Catégorie : DSP / Effets.

Le son comme un micro le prendrait : dynamique, statique, ruban, à lampe, broadcast, cravate, ou téléphone, mégaphone, radio ancienne, talkie-walkie ; sa distance (effet de proximité), son angle et sa directivité, son préampli poussé, la pièce.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `distance` | 1 | 15 | 200 |
| `angle` | 0 | 0 | 180 |
| `drive` | 0 | 0 | 30 |
| `low-cut` | 20 | 20 | 500 |
| `high-cut` | 1000 | 20000 | 20000 |
| `room` | 0 | 0 | 1 |
| `output` | -24 | 0 | 24 |
| `mix` | 0 | 1 | 1 |
| `model` | 0 | 3 | 11 |
| `pattern` | 0 | 0 | 4 |
| `preamp` | 0 | 0 | 2 |
| `noise` | -90 | -90 | -20 |
| `room-size` | 0 | 0.25 | 1 |
| `room-tone` | 1000 | 5000 | 12000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| distance / `distance` | Entrée | value | 1 |
| angle / `angle` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| room / `room` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| model / `model` | Entrée | value | 1 |
| pattern / `pattern` | Entrée | value | 1 |
| preamp / `preamp` | Entrée | value | 1 |
| noise / `noise` | Entrée | value | 1 |
| room-size / `room-size` | Entrée | value | 1 |
| room-tone / `room-tone` | Entrée | value | 1 |

## MIXER 4 (mixer-4) {#mixer-4}

Catégorie : DSP / Utilitaires audio.

Aucun paramètre numérique déclaré par ce schéma.

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| in-1 / `in-1` | Entrée | audio | 2 |
| in-2 / `in-2` | Entrée | audio | 2 |
| in-3 / `in-3` | Entrée | audio | 2 |
| in-4 / `in-4` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |

## MONO → STEREO (mono-to-stereo) {#mono-to-stereo}

Catégorie : DSP / Utilitaires audio.

Un signal mono sur les deux côtés, placé dans l'espace stéréo par le panoramique.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `pan` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 1 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| pan / `pan` | Entrée | value | 1 |

## MULTIBANDE PRO (multiband-pro) {#multiband-pro}

Catégorie : DSP / Dynamique.

Dynamique sur quatre bandes : chacune compressée, expansée (comme un gate), relevée ou éclatée, son seuil, son ratio, sa plage, ses temps, son genou, son gain ; les coupures et les seuils se tirent à la souris sur le spectre.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `threshold-1` | -60 | -24 | 0 |
| `threshold-2` | -60 | -24 | 0 |
| `threshold-3` | -60 | -24 | 0 |
| `threshold-4` | -60 | -24 | 0 |
| `input` | -24 | 0 | 24 |
| `output` | -24 | 0 | 24 |
| `amount` | 0 | 1 | 1 |
| `link` | 0 | 1 | 1 |
| `cross-1` | 20 | 120 | 1000 |
| `cross-2` | 200 | 1000 | 8000 |
| `cross-3` | 1000 | 6000 | 18000 |
| `band1-mode` | 0 | 0 | 3 |
| `band1-ratio` | 1 | 2.5 | 20 |
| `band1-range` | 0 | 12 | 48 |
| `band1-attack` | 0.100000001 | 10 | 300 |
| `band1-release` | 5 | 120 | 2000 |
| `band1-knee` | 0 | 6 | 24 |
| `band1-makeup` | -24 | 0 | 24 |
| `band1-on` | 0 | 1 | 1 |
| `band1-solo` | 0 | 0 | 1 |
| `band2-mode` | 0 | 0 | 3 |
| `band2-ratio` | 1 | 2.5 | 20 |
| `band2-range` | 0 | 12 | 48 |
| `band2-attack` | 0.100000001 | 10 | 300 |
| `band2-release` | 5 | 120 | 2000 |
| `band2-knee` | 0 | 6 | 24 |
| `band2-makeup` | -24 | 0 | 24 |
| `band2-on` | 0 | 1 | 1 |
| `band2-solo` | 0 | 0 | 1 |
| `band3-mode` | 0 | 0 | 3 |
| `band3-ratio` | 1 | 2.5 | 20 |
| `band3-range` | 0 | 12 | 48 |
| `band3-attack` | 0.100000001 | 10 | 300 |
| `band3-release` | 5 | 120 | 2000 |
| `band3-knee` | 0 | 6 | 24 |
| `band3-makeup` | -24 | 0 | 24 |
| `band3-on` | 0 | 1 | 1 |
| `band3-solo` | 0 | 0 | 1 |
| `band4-mode` | 0 | 0 | 3 |
| `band4-ratio` | 1 | 2.5 | 20 |
| `band4-range` | 0 | 12 | 48 |
| `band4-attack` | 0.100000001 | 10 | 300 |
| `band4-release` | 5 | 120 | 2000 |
| `band4-knee` | 0 | 6 | 24 |
| `band4-makeup` | -24 | 0 | 24 |
| `band4-on` | 0 | 1 | 1 |
| `band4-solo` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| threshold-1 / `threshold-1` | Entrée | value | 1 |
| threshold-2 / `threshold-2` | Entrée | value | 1 |
| threshold-3 / `threshold-3` | Entrée | value | 1 |
| threshold-4 / `threshold-4` | Entrée | value | 1 |
| input / `input` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| amount / `amount` | Entrée | value | 1 |
| link / `link` | Entrée | value | 1 |
| cross-1 / `cross-1` | Entrée | value | 1 |
| cross-2 / `cross-2` | Entrée | value | 1 |
| cross-3 / `cross-3` | Entrée | value | 1 |
| band1-mode / `band1-mode` | Entrée | value | 1 |
| band1-ratio / `band1-ratio` | Entrée | value | 1 |
| band1-range / `band1-range` | Entrée | value | 1 |
| band1-attack / `band1-attack` | Entrée | value | 1 |
| band1-release / `band1-release` | Entrée | value | 1 |
| band1-knee / `band1-knee` | Entrée | value | 1 |
| band1-makeup / `band1-makeup` | Entrée | value | 1 |
| band1-on / `band1-on` | Entrée | value | 1 |
| band1-solo / `band1-solo` | Entrée | value | 1 |
| band2-mode / `band2-mode` | Entrée | value | 1 |
| band2-ratio / `band2-ratio` | Entrée | value | 1 |
| band2-range / `band2-range` | Entrée | value | 1 |
| band2-attack / `band2-attack` | Entrée | value | 1 |
| band2-release / `band2-release` | Entrée | value | 1 |
| band2-knee / `band2-knee` | Entrée | value | 1 |
| band2-makeup / `band2-makeup` | Entrée | value | 1 |
| band2-on / `band2-on` | Entrée | value | 1 |
| band2-solo / `band2-solo` | Entrée | value | 1 |
| band3-mode / `band3-mode` | Entrée | value | 1 |
| band3-ratio / `band3-ratio` | Entrée | value | 1 |
| band3-range / `band3-range` | Entrée | value | 1 |
| band3-attack / `band3-attack` | Entrée | value | 1 |
| band3-release / `band3-release` | Entrée | value | 1 |
| band3-knee / `band3-knee` | Entrée | value | 1 |
| band3-makeup / `band3-makeup` | Entrée | value | 1 |
| band3-on / `band3-on` | Entrée | value | 1 |
| band3-solo / `band3-solo` | Entrée | value | 1 |
| band4-mode / `band4-mode` | Entrée | value | 1 |
| band4-ratio / `band4-ratio` | Entrée | value | 1 |
| band4-range / `band4-range` | Entrée | value | 1 |
| band4-attack / `band4-attack` | Entrée | value | 1 |
| band4-release / `band4-release` | Entrée | value | 1 |
| band4-knee / `band4-knee` | Entrée | value | 1 |
| band4-makeup / `band4-makeup` | Entrée | value | 1 |
| band4-on / `band4-on` | Entrée | value | 1 |
| band4-solo / `band4-solo` | Entrée | value | 1 |

## MULTIMODE FILTER (multimode-filter) {#multimode-filter}

Catégorie : DSP / Filtres et EQ.

Filtre résonant passe-bas, passe-haut, passe-bande ou réjecteur, avec saturation en entrée.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `mode` | 0 | 0 | 3 |
| `cutoff` | 20 | 1000 | 20000 |
| `resonance` | 0.100000001 | 0.707000017 | 20 |
| `drive` | 0 | 0 | 24 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| mode / `mode` | Entrée | value | 1 |
| cutoff / `cutoff` | Entrée | value | 1 |
| resonance / `resonance` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |

## PATINE (patine) {#patine}

Catégorie : DSP / Effets.

Le son vieilli : bruit (disque, bande, secteur), ondulation de la bande, distorsion, usure numérique, petite pièce, bande magnétique avec ses pertes ; une ampleur sur l'ensemble, la tonalité, la largeur et le mélange.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `noise` | 0 | 0.25 | 1 |
| `wobble` | 0 | 0.25 | 1 |
| `distort` | 0 | 0.200000003 | 1 |
| `digital` | 0 | 0 | 1 |
| `space` | 0 | 0.150000006 | 1 |
| `magnetic` | 0 | 0.200000003 | 1 |
| `magnitude` | 0 | 1 | 1 |
| `mix` | 0 | 1 | 1 |
| `noise-type` | 0 | 0 | 2 |
| `wobble-rate` | 0 | 0.300000012 | 1 |
| `distort-type` | 0 | 0 | 2 |
| `space-size` | 0 | 0.400000006 | 1 |
| `tone` | -1 | 0 | 1 |
| `width` | 0 | 1 | 1.5 |
| `output` | -24 | 0 | 12 |
| `noise-follow` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| noise / `noise` | Entrée | value | 1 |
| wobble / `wobble` | Entrée | value | 1 |
| distort / `distort` | Entrée | value | 1 |
| digital / `digital` | Entrée | value | 1 |
| space / `space` | Entrée | value | 1 |
| magnetic / `magnetic` | Entrée | value | 1 |
| magnitude / `magnitude` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| noise-type / `noise-type` | Entrée | value | 1 |
| wobble-rate / `wobble-rate` | Entrée | value | 1 |
| distort-type / `distort-type` | Entrée | value | 1 |
| space-size / `space-size` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| noise-follow / `noise-follow` | Entrée | value | 1 |

## COMB (peigne) {#peigne}

Catégorie : DSP / Effets.

Un filtre en peigne accordé : le son repassé dans un court retard, en boucle ou en avance, creuse ou fait sonner une série d'harmoniques. Fréquence, rétroaction (négative : harmoniques impaires), amortissement, écart stéréo, LFO.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 200 | 5000 |
| `feedback` | -0.980000019 | 0.75 | 0.980000019 |
| `damping` | 500 | 9000 | 20000 |
| `spread` | 0 | 0 | 1 |
| `lfo-depth` | 0 | 0 | 1 |
| `lfo-rate` | 0.00999999978 | 0.300000012 | 10 |
| `mix` | 0 | 0.5 | 1 |
| `output` | -24 | 0 | 24 |
| `mode` | 0 | 0 | 1 |
| `sync` | 0 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| damping / `damping` | Entrée | value | 1 |
| spread / `spread` | Entrée | value | 1 |
| lfo-depth / `lfo-depth` | Entrée | value | 1 |
| lfo-rate / `lfo-rate` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |

## PHASER (phaser) {#phaser}

Catégorie : DSP / Effets.

Six filtres déphaseurs balayés par un LFO : des creux qui tournent dans le spectre.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.0199999996 | 0.349999994 | 8 |
| `depth` | 0 | 0.800000012 | 1 |
| `frequency` | 100 | 700 | 4000 |
| `feedback` | 0 | 0.5 | 0.899999976 |
| `mix` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| rate / `rate` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| frequency / `frequency` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## PING-PONG DELAY (ping-pong) {#ping-pong}

Catégorie : DSP / Effets.

Des échos qui rebondissent de gauche à droite.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `time` | 10 | 375 | 2000 |
| `feedback` | 0 | 0.449999988 | 0.949999988 |
| `mix` | 0 | 0.349999994 | 1 |
| `width` | 0 | 1 | 1 |
| `sync` | 0 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| time / `time` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |

## POMPE (pompe) {#pompe}

Catégorie : DSP / Dynamique.

Fait pomper le volume en rythme sans compresseur : une courbe (huit formes ou dessinée en seize points) rejouée à chaque temps ou à chaque coup de la piste branchée sur son entrée sidechain. Profondeur, courbe, tenue en bas, phase, décalage stéréo, lissage.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `depth` | 0 | 0.800000012 | 1 |
| `rate` | 0.100000001 | 2 | 20 |
| `curve` | -1 | 0.400000006 | 1 |
| `hold` | 0 | 0.0500000007 | 0.5 |
| `phase` | 0 | 0 | 1 |
| `stereo-offset` | 0 | 0 | 0.5 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `shape` | 0 | 0 | 7 |
| `sync` | 0 | 9 | 12 |
| `trigger-source` | 0 | 0 | 1 |
| `threshold` | -60 | -24 | 0 |
| `smoothing` | 0 | 1.5 | 20 |
| `point-1` | 0 | 0 | 1 |
| `point-2` | 0 | 0.219999999 | 1 |
| `point-3` | 0 | 0.419999987 | 1 |
| `point-4` | 0 | 0.579999983 | 1 |
| `point-5` | 0 | 0.699999988 | 1 |
| `point-6` | 0 | 0.790000021 | 1 |
| `point-7` | 0 | 0.860000014 | 1 |
| `point-8` | 0 | 0.910000026 | 1 |
| `point-9` | 0 | 0.939999998 | 1 |
| `point-10` | 0 | 0.964999974 | 1 |
| `point-11` | 0 | 0.980000019 | 1 |
| `point-12` | 0 | 0.99000001 | 1 |
| `point-13` | 0 | 1 | 1 |
| `point-14` | 0 | 1 | 1 |
| `point-15` | 0 | 1 | 1 |
| `point-16` | 0 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| depth / `depth` | Entrée | value | 1 |
| rate / `rate` | Entrée | value | 1 |
| curve / `curve` | Entrée | value | 1 |
| hold / `hold` | Entrée | value | 1 |
| phase / `phase` | Entrée | value | 1 |
| stereo-offset / `stereo-offset` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| shape / `shape` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| trigger-source / `trigger-source` | Entrée | value | 1 |
| threshold / `threshold` | Entrée | value | 1 |
| smoothing / `smoothing` | Entrée | value | 1 |
| point-1 / `point-1` | Entrée | value | 1 |
| point-2 / `point-2` | Entrée | value | 1 |
| point-3 / `point-3` | Entrée | value | 1 |
| point-4 / `point-4` | Entrée | value | 1 |
| point-5 / `point-5` | Entrée | value | 1 |
| point-6 / `point-6` | Entrée | value | 1 |
| point-7 / `point-7` | Entrée | value | 1 |
| point-8 / `point-8` | Entrée | value | 1 |
| point-9 / `point-9` | Entrée | value | 1 |
| point-10 / `point-10` | Entrée | value | 1 |
| point-11 / `point-11` | Entrée | value | 1 |
| point-12 / `point-12` | Entrée | value | 1 |
| point-13 / `point-13` | Entrée | value | 1 |
| point-14 / `point-14` | Entrée | value | 1 |
| point-15 / `point-15` | Entrée | value | 1 |
| point-16 / `point-16` | Entrée | value | 1 |
| sidechain-in / `sidechain-in` | Entrée | audio | 2 |

## PRESSION (pression) {#pression}

Catégorie : DSP / Dynamique.

Compression vers le haut et vers le bas sur trois bandes : le fort est tenu, le faible relevé, tout à la fois. Profondeur, temps, quantités vers le haut et vers le bas, gain de chaque bande ; seuils et ratios réglables en tirant sur les barres.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `depth` | 0 | 1 | 1 |
| `time` | 0 | 0.5 | 1 |
| `upward` | 0 | 1 | 1 |
| `downward` | 0 | 1 | 1 |
| `low-gain` | -24 | 0 | 24 |
| `mid-gain` | -24 | 0 | 24 |
| `high-gain` | -24 | 0 | 24 |
| `output` | -24 | 0 | 24 |
| `input` | -24 | 0 | 24 |
| `cross-low` | 40 | 88 | 400 |
| `cross-high` | 800 | 2500 | 10000 |
| `low-down-threshold` | -60 | -33.7999992 | 0 |
| `low-down-ratio` | 1 | 66.6999969 | 100 |
| `low-up-threshold` | -80 | -41.7999992 | 0 |
| `low-up-ratio` | 1 | 4.17000008 | 10 |
| `low-attack` | 0.100000001 | 47.7999992 | 300 |
| `low-release` | 5 | 282 | 2000 |
| `mid-down-threshold` | -60 | -30.2000008 | 0 |
| `mid-down-ratio` | 1 | 66.6999969 | 100 |
| `mid-up-threshold` | -80 | -41.7999992 | 0 |
| `mid-up-ratio` | 1 | 4.17000008 | 10 |
| `mid-attack` | 0.100000001 | 22.3999996 | 300 |
| `mid-release` | 5 | 282 | 2000 |
| `high-down-threshold` | -60 | -35.5 | 0 |
| `high-down-ratio` | 1 | 66.6999969 | 100 |
| `high-up-threshold` | -80 | -40.7999992 | 0 |
| `high-up-ratio` | 1 | 4.17000008 | 10 |
| `high-attack` | 0.100000001 | 13.5 | 300 |
| `high-release` | 5 | 132 | 2000 |
| `solo` | 0 | 0 | 3 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| depth / `depth` | Entrée | value | 1 |
| time / `time` | Entrée | value | 1 |
| upward / `upward` | Entrée | value | 1 |
| downward / `downward` | Entrée | value | 1 |
| low-gain / `low-gain` | Entrée | value | 1 |
| mid-gain / `mid-gain` | Entrée | value | 1 |
| high-gain / `high-gain` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| input / `input` | Entrée | value | 1 |
| cross-low / `cross-low` | Entrée | value | 1 |
| cross-high / `cross-high` | Entrée | value | 1 |
| low-down-threshold / `low-down-threshold` | Entrée | value | 1 |
| low-down-ratio / `low-down-ratio` | Entrée | value | 1 |
| low-up-threshold / `low-up-threshold` | Entrée | value | 1 |
| low-up-ratio / `low-up-ratio` | Entrée | value | 1 |
| low-attack / `low-attack` | Entrée | value | 1 |
| low-release / `low-release` | Entrée | value | 1 |
| mid-down-threshold / `mid-down-threshold` | Entrée | value | 1 |
| mid-down-ratio / `mid-down-ratio` | Entrée | value | 1 |
| mid-up-threshold / `mid-up-threshold` | Entrée | value | 1 |
| mid-up-ratio / `mid-up-ratio` | Entrée | value | 1 |
| mid-attack / `mid-attack` | Entrée | value | 1 |
| mid-release / `mid-release` | Entrée | value | 1 |
| high-down-threshold / `high-down-threshold` | Entrée | value | 1 |
| high-down-ratio / `high-down-ratio` | Entrée | value | 1 |
| high-up-threshold / `high-up-threshold` | Entrée | value | 1 |
| high-up-ratio / `high-up-ratio` | Entrée | value | 1 |
| high-attack / `high-attack` | Entrée | value | 1 |
| high-release / `high-release` | Entrée | value | 1 |
| solo / `solo` | Entrée | value | 1 |

## RESONATORS (resonateurs) {#resonateurs}

Catégorie : DSP / Effets.

Cinq résonateurs accordés que le son fait chanter : une note de base et quatre intervalles (un accord), chacun son niveau ; durée, couleur, largeur, cordes, pincé ou métal.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `note` | 24 | 48 | 96 |
| `decay` | 0 | 0.699999988 | 1 |
| `colour` | 0 | 0.5 | 1 |
| `width` | 0 | 0.600000024 | 1 |
| `mix` | 0 | 0.5 | 1 |
| `output` | -24 | 0 | 24 |
| `tone` | -1 | 0 | 1 |
| `mode` | 0 | 0 | 2 |
| `interval-2` | -24 | 7 | 24 |
| `interval-3` | -24 | 12 | 24 |
| `interval-4` | -24 | 16 | 24 |
| `interval-5` | -24 | 19 | 24 |
| `gain-1` | 0 | 1 | 1 |
| `gain-2` | 0 | 0.800000012 | 1 |
| `gain-3` | 0 | 0.699999988 | 1 |
| `gain-4` | 0 | 0.600000024 | 1 |
| `gain-5` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| note / `note` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| colour / `colour` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| interval-2 / `interval-2` | Entrée | value | 1 |
| interval-3 / `interval-3` | Entrée | value | 1 |
| interval-4 / `interval-4` | Entrée | value | 1 |
| interval-5 / `interval-5` | Entrée | value | 1 |
| gain-1 / `gain-1` | Entrée | value | 1 |
| gain-2 / `gain-2` | Entrée | value | 1 |
| gain-3 / `gain-3` | Entrée | value | 1 |
| gain-4 / `gain-4` | Entrée | value | 1 |
| gain-5 / `gain-5` | Entrée | value | 1 |

## REVERB (reverb) {#reverb}

Catégorie : DSP / Effets.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `room-size` | 0 | 0.5 | 0.95 |
| `mix` | 0 | 0.25 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| room-size / `room-size` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |

## PRO REVERB (reverb-pro) {#reverb-pro}

Catégorie : DSP / Effets.

Réverbération de studio : un réseau de seize délais, l'espace, la décroissance réglée à part dans les graves, les médiums et les aigus (la courbe se tire à la souris), le pré-délai calé ou non sur le tempo, la distance, la brillance, le caractère, les premières réflexions, les coupes après, le ducking et le gel.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `space` | 0 | 0.5 | 1 |
| `decay` | 0.100000001 | 2.20000005 | 20 |
| `predelay` | 0 | 12 | 500 |
| `distance` | 0 | 0.349999994 | 1 |
| `brightness` | -1 | 0 | 1 |
| `character` | 0 | 0.349999994 | 1 |
| `width` | 0 | 1 | 2 |
| `mix` | 0 | 0.300000012 | 1 |
| `low-decay` | 0.25 | 1.29999995 | 4 |
| `low-frequency` | 50 | 220 | 1000 |
| `mid-decay` | 0.25 | 1 | 4 |
| `mid-frequency` | 200 | 1200 | 8000 |
| `high-decay` | 0.100000001 | 0.550000012 | 2 |
| `high-frequency` | 1000 | 4500 | 16000 |
| `low-cut` | 10 | 20 | 1000 |
| `high-cut` | 1000 | 20000 | 20000 |
| `ducking` | 0 | 0 | 30 |
| `duck-release` | 20 | 300 | 2000 |
| `freeze` | 0 | 0 | 1 |
| `sync` | 0 | 0 | 12 |
| `diffusion` | 0 | 0.75 | 1 |
| `output` | -24 | 0 | 12 |
| `early` | -24 | 0 | 6 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| space / `space` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| predelay / `predelay` | Entrée | value | 1 |
| distance / `distance` | Entrée | value | 1 |
| brightness / `brightness` | Entrée | value | 1 |
| character / `character` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| low-decay / `low-decay` | Entrée | value | 1 |
| low-frequency / `low-frequency` | Entrée | value | 1 |
| mid-decay / `mid-decay` | Entrée | value | 1 |
| mid-frequency / `mid-frequency` | Entrée | value | 1 |
| high-decay / `high-decay` | Entrée | value | 1 |
| high-frequency / `high-frequency` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| ducking / `ducking` | Entrée | value | 1 |
| duck-release / `duck-release` | Entrée | value | 1 |
| freeze / `freeze` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |
| diffusion / `diffusion` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| early / `early` | Entrée | value | 1 |

## ROTOR (rotor) {#rotor}

Catégorie : DSP / Effets.

Une cabine à haut-parleurs tournants : le pavillon des aigus et le tambour des graves tournent, lentement ou vite, et accélèrent chacun à son rythme. Vitesses, accélération, équilibre pavillon / tambour, effet Doppler, saturation du préampli, séparation, écart des micros, pièce.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `slow` | 0.300000012 | 0.800000012 | 2 |
| `fast` | 4 | 6.80000019 | 9 |
| `acceleration` | 0.200000003 | 1.20000005 | 5 |
| `balance` | -1 | 0 | 1 |
| `doppler` | 0 | 0.5 | 1 |
| `drive` | 0 | 3 | 24 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `speed-mode` | 0 | 1 | 2 |
| `crossover` | 400 | 800 | 1500 |
| `width` | 0 | 0.800000012 | 1 |
| `room` | 0 | 0.25 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| slow / `slow` | Entrée | value | 1 |
| fast / `fast` | Entrée | value | 1 |
| acceleration / `acceleration` | Entrée | value | 1 |
| balance / `balance` | Entrée | value | 1 |
| doppler / `doppler` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| speed-mode / `speed-mode` | Entrée | value | 1 |
| crossover / `crossover` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| room / `room` | Entrée | value | 1 |

## SACCADE (saccade) {#saccade}

Catégorie : DSP / Effets.

Bégaie en rythme : à un moment choisi de chaque mesure, un morceau du son se répète sur une grille (1/32 à 1/4), plus ou moins souvent, de plus en plus bas ou de plus en plus doux, parfois à l'envers ; mêlé au son, à sa place ou seul. Un bouton répète tout de suite.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `chance` | 0 | 1 | 1 |
| `variation` | 0 | 0 | 1 |
| `decay` | 0 | 0 | 1 |
| `pitch-decay` | 0 | 0 | 1 |
| `filter` | 200 | 20000 | 20000 |
| `mix` | 0 | 1 | 1 |
| `output` | -24 | 0 | 24 |
| `interval-sync` | 1 | 12 | 12 |
| `offset` | 0 | 8 | 15 |
| `gate-length` | 1 | 4 | 16 |
| `grid-sync` | 1 | 3 | 12 |
| `mode` | 0 | 1 | 2 |
| `reverse-chance` | 0 | 0 | 1 |
| `repeat` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| chance / `chance` | Entrée | value | 1 |
| variation / `variation` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| pitch-decay / `pitch-decay` | Entrée | value | 1 |
| filter / `filter` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| interval-sync / `interval-sync` | Entrée | value | 1 |
| offset / `offset` | Entrée | value | 1 |
| gate-length / `gate-length` | Entrée | value | 1 |
| grid-sync / `grid-sync` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| reverse-chance / `reverse-chance` | Entrée | value | 1 |
| repeat / `repeat` | Entrée | value | 1 |

## STEREO → MONO (stereo-to-mono) {#stereo-to-mono}

Catégorie : DSP / Utilitaires audio.

Les deux côtés réunis en un : la moyenne, ou penchée vers un côté par la balance.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `balance` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 1 |
| balance / `balance` | Entrée | value | 1 |

## STEREO WIDTH (stereo-width) {#stereo-width}

Catégorie : DSP / Utilitaires audio.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `width` | 0 | 1 | 2 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| width / `width` | Entrée | value | 1 |

## TRANCHE (tranche) {#tranche}

Catégorie : DSP / Effets.

La tranche d'une piste en un module : gate, égaliseur (coupe-bas, graves, deux cloches, aigus, coupe-haut), compresseur, transitoires, excitateur, largeur ; un assistant écoute la piste et propose l'égalisation et la compression.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gate-threshold` | -80 | -60 | 0 |
| `comp-threshold` | -60 | -18 | 0 |
| `attack` | -1 | 0 | 1 |
| `sustain` | -1 | 0 | 1 |
| `excite` | 0 | 0.200000003 | 1 |
| `width` | 0 | 1 | 2 |
| `output` | -24 | 0 | 24 |
| `mix` | 0 | 1 | 1 |
| `gate-on` | 0 | 0 | 1 |
| `eq-on` | 0 | 1 | 1 |
| `comp-on` | 0 | 1 | 1 |
| `transient-on` | 0 | 0 | 1 |
| `exciter-on` | 0 | 0 | 1 |
| `gate-range` | 0 | 40 | 80 |
| `gate-attack` | 0.100000001 | 1 | 50 |
| `gate-release` | 5 | 100 | 1000 |
| `low-cut` | 10 | 10 | 500 |
| `low-shelf-hz` | 40 | 120 | 500 |
| `low-shelf-db` | -12 | 0 | 12 |
| `bell1-hz` | 80 | 400 | 8000 |
| `bell1-db` | -12 | 0 | 12 |
| `bell1-q` | 0.300000012 | 1 | 8 |
| `bell2-hz` | 300 | 3000 | 16000 |
| `bell2-db` | -12 | 0 | 12 |
| `bell2-q` | 0.300000012 | 1 | 8 |
| `high-shelf-hz` | 2000 | 8000 | 20000 |
| `high-shelf-db` | -12 | 0 | 12 |
| `high-cut` | 2000 | 20000 | 20000 |
| `comp-ratio` | 1 | 3 | 20 |
| `comp-attack` | 0.100000001 | 10 | 200 |
| `comp-release` | 10 | 120 | 2000 |
| `comp-knee` | 0 | 6 | 24 |
| `comp-makeup` | -12 | 0 | 24 |
| `exciter-hz` | 1000 | 3000 | 12000 |
| `exciter-style` | 0 | 0 | 2 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gate-threshold / `gate-threshold` | Entrée | value | 1 |
| comp-threshold / `comp-threshold` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| sustain / `sustain` | Entrée | value | 1 |
| excite / `excite` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| gate-on / `gate-on` | Entrée | value | 1 |
| eq-on / `eq-on` | Entrée | value | 1 |
| comp-on / `comp-on` | Entrée | value | 1 |
| transient-on / `transient-on` | Entrée | value | 1 |
| exciter-on / `exciter-on` | Entrée | value | 1 |
| gate-range / `gate-range` | Entrée | value | 1 |
| gate-attack / `gate-attack` | Entrée | value | 1 |
| gate-release / `gate-release` | Entrée | value | 1 |
| low-cut / `low-cut` | Entrée | value | 1 |
| low-shelf-hz / `low-shelf-hz` | Entrée | value | 1 |
| low-shelf-db / `low-shelf-db` | Entrée | value | 1 |
| bell1-hz / `bell1-hz` | Entrée | value | 1 |
| bell1-db / `bell1-db` | Entrée | value | 1 |
| bell1-q / `bell1-q` | Entrée | value | 1 |
| bell2-hz / `bell2-hz` | Entrée | value | 1 |
| bell2-db / `bell2-db` | Entrée | value | 1 |
| bell2-q / `bell2-q` | Entrée | value | 1 |
| high-shelf-hz / `high-shelf-hz` | Entrée | value | 1 |
| high-shelf-db / `high-shelf-db` | Entrée | value | 1 |
| high-cut / `high-cut` | Entrée | value | 1 |
| comp-ratio / `comp-ratio` | Entrée | value | 1 |
| comp-attack / `comp-attack` | Entrée | value | 1 |
| comp-release / `comp-release` | Entrée | value | 1 |
| comp-knee / `comp-knee` | Entrée | value | 1 |
| comp-makeup / `comp-makeup` | Entrée | value | 1 |
| exciter-hz / `exciter-hz` | Entrée | value | 1 |
| exciter-style / `exciter-style` | Entrée | value | 1 |

## TRANSIENT SHAPER (transient-shaper) {#transient-shaper}

Catégorie : DSP / Dynamique.

Plus ou moins d'attaque et de maintien : donne du punch ou adoucit une percussion.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `attack` | -1 | 0 | 1 |
| `sustain` | -1 | 0 | 1 |
| `output` | -12 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| attack / `attack` | Entrée | value | 1 |
| sustain / `sustain` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |

## TREMOLO / AUTO-PAN (tremolo-pan) {#tremolo-pan}

Catégorie : DSP / Effets.

Le volume ondule au rythme d'un LFO ; avec l'étalement, gauche et droite s'opposent (auto-pan).

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.100000001 | 4 | 20 |
| `depth` | 0 | 0.600000024 | 1 |
| `shape` | 0 | 0 | 3 |
| `spread` | 0 | 0 | 1 |
| `sync` | 0 | 0 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| rate / `rate` | Entrée | value | 1 |
| depth / `depth` | Entrée | value | 1 |
| shape / `shape` | Entrée | value | 1 |
| spread / `spread` | Entrée | value | 1 |
| sync / `sync` | Entrée | value | 1 |

## UTILITY (utilitaire) {#utilitaire}

Catégorie : DSP / Utilitaires audio.

Les petits réglages d'une piste au même endroit : gain, panoramique, largeur stéréo, graves en mono, polarité de chaque côté, canaux (stéréo, gauche, droite, inversés, mono), filtre de courant continu, retard pour aligner, silence.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `gain` | -36 | 0 | 36 |
| `pan` | -1 | 0 | 1 |
| `width` | 0 | 1 | 2 |
| `mono-bass` | 20 | 20 | 500 |
| `invert-left` | 0 | 0 | 1 |
| `invert-right` | 0 | 0 | 1 |
| `channel-mode` | 0 | 0 | 4 |
| `dc-filter` | 0 | 0 | 1 |
| `mute` | 0 | 0 | 1 |
| `delay` | 0 | 0 | 20 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| gain / `gain` | Entrée | value | 1 |
| pan / `pan` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| mono-bass / `mono-bass` | Entrée | value | 1 |
| invert-left / `invert-left` | Entrée | value | 1 |
| invert-right / `invert-right` | Entrée | value | 1 |
| channel-mode / `channel-mode` | Entrée | value | 1 |
| dc-filter / `dc-filter` | Entrée | value | 1 |
| mute / `mute` | Entrée | value | 1 |
| delay / `delay` | Entrée | value | 1 |

## VELOURS (velours) {#velours}

Catégorie : DSP / Dynamique.

Trouve les résonances qui dépassent dans le spectre et les calme, seulement quand elles sonnent : profondeur, netteté, sélectivité, attaque, relâchement, zone de travail, mode doux ou dur, et l'écoute de ce qui est retiré.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `depth` | 0 | 6 | 24 |
| `sharpness` | 0 | 0.5 | 1 |
| `selectivity` | 0 | 0.5 | 1 |
| `attack` | 1 | 5 | 200 |
| `release` | 5 | 80 | 500 |
| `focus-low` | 20 | 200 | 20000 |
| `focus-high` | 20 | 12000 | 20000 |
| `mix` | 0 | 1 | 1 |
| `mode` | 0 | 0 | 1 |
| `delta` | 0 | 0 | 1 |
| `output` | -12 | 0 | 12 |
| `link` | 0 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| depth / `depth` | Entrée | value | 1 |
| sharpness / `sharpness` | Entrée | value | 1 |
| selectivity / `selectivity` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| focus-low / `focus-low` | Entrée | value | 1 |
| focus-high / `focus-high` | Entrée | value | 1 |
| mix / `mix` | Entrée | value | 1 |
| mode / `mode` | Entrée | value | 1 |
| delta / `delta` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| link / `link` | Entrée | value | 1 |

## VOCODER (vocoder) {#vocoder}

Catégorie : DSP / Effets.

Fait parler un son avec la voix qui entre : la voix découpée en bandes donne son relief à un porteur — bruit, dents de scie sur une note, dents de scie qui suivent la hauteur de la voix, ou le son d'une autre piste branché sur son entrée sidechain. Bandes, plage, formants, attaque, relâchement, sifflantes, étalement stéréo.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `formant` | -12 | 0 | 12 |
| `attack` | 0.5 | 4 | 100 |
| `release` | 5 | 60 | 1000 |
| `pitch` | 24 | 48 | 96 |
| `unvoiced` | 0 | 0.300000012 | 1 |
| `tone` | -1 | 0 | 1 |
| `output` | -24 | 0 | 24 |
| `carrier-source` | 0 | 2 | 3 |
| `bands` | 4 | 20 | 40 |
| `low-band` | 50 | 100 | 1000 |
| `high-band` | 2000 | 10000 | 18000 |
| `bandwidth` | 0.25 | 1 | 2 |
| `band-gate` | -90 | -70 | -20 |
| `stereo` | 0 | 0.5 | 1 |
| `detune` | 0 | 0.200000003 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| mix / `mix` | Entrée | value | 1 |
| formant / `formant` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| pitch / `pitch` | Entrée | value | 1 |
| unvoiced / `unvoiced` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |
| carrier-source / `carrier-source` | Entrée | value | 1 |
| bands / `bands` | Entrée | value | 1 |
| low-band / `low-band` | Entrée | value | 1 |
| high-band / `high-band` | Entrée | value | 1 |
| bandwidth / `bandwidth` | Entrée | value | 1 |
| band-gate / `band-gate` | Entrée | value | 1 |
| stereo / `stereo` | Entrée | value | 1 |
| detune / `detune` | Entrée | value | 1 |
| sidechain-in / `sidechain-in` | Entrée | audio | 2 |
