---
id: reference-instruments
title: Référence instruments et générateurs
description: Consulter les paramètres et ports typés des instruments natifs.
section: Manuel / Référence
parent: synths
order: 37
sourceRevision: 2452167cfe1ec554
---

## Vue d’ensemble {#overview}

Référence générée depuis le registre CyDAW 0.78.0. Chaque section décrit un type natif, pas un preset de piste ni un plugin externe. Utiliser la recherche pour trouver une clé, une plage ou un port.


~~~image
{"id": "figure-graph", "src": "assets/graph-fr.png", "alt": "Le graphe relie les ports typés et leurs paramètres.", "title": "Le graphe relie les ports typés et leurs paramètres.", "controls": "minimal"}
~~~

*Le graphe relie les ports typés et leurs paramètres.*


Les nombres sont les valeurs brutes du schéma moteur. Les unités affichées, échelles logarithmiques, choix et réglages de fichiers/tableaux se lisent dans le panneau du module ; ils ne se déduisent pas de ces nombres. Les défauts décrivent un nœud neuf, pas forcément le preset chargé. Un port déclaré ne garantit pas une frontière de plugin exportable.

Voir le [glossaire](doc:glossary#reference), les [instruments](doc:synths) et les [effets](doc:studio-effects) pour la méthode d’écoute. Pour les limites lire [export](doc:plugin-formats#limits).

## ALLIAGE (alliage-synth) {#alliage-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 12 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `osc.type` | 0 | 0 | 7 |
| `osc.table` | 0 | 0 | 7 |
| `osc.mode` | 0 | 1 | 7 |
| `osc.ratio` | 0 | 1 | 13 |
| `osc.octave` | -3 | 0 | 3 |
| `osc.semi` | -12 | 0 | 12 |
| `osc.fine` | -50 | 0 | 50 |
| `osc.timbre` | 0 | 0.400000006 | 1 |
| `osc.shape` | 0 | 0.5 | 1 |
| `osc.env` | -1 | 0 | 1 |
| `osc.spread` | 0 | 0.600000024 | 1 |
| `osc.level` | 0 | 0.800000012 | 1 |
| `sub.level` | 0 | 0 | 1 |
| `sub.wave` | 0 | 0 | 2 |
| `noise.level` | 0 | 0 | 1 |
| `noise.colour` | 0 | 0.5 | 1 |
| `f.type` | 0 | 0 | 6 |
| `f.cutoff` | 20 | 2200 | 20000 |
| `f.resonance` | 0 | 0.200000003 | 1 |
| `f.drive` | 0 | 0.150000006 | 1 |
| `f.env` | -1 | 0.300000012 | 1 |
| `f.key` | 0 | 0.5 | 1 |
| `f.velocity` | 0 | 0.300000012 | 1 |
| `amp.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp.decay` | 0.00499999989 | 0.600000024 | 10 |
| `amp.sustain` | 0 | 0.800000012 | 1 |
| `amp.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `fenv.decay` | 0.00499999989 | 0.5 | 10 |
| `fenv.sustain` | 0 | 0.300000012 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 12 |
| `pitch.env` | -24 | 0 | 24 |
| `pitch.decay` | 0.00499999989 | 0.0799999982 | 5 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0 | 3 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.timbre` | 0 | 0 | 1 |
| `lfo1.cutoff` | 0 | 0 | 1 |
| `lfo1.amp` | 0 | 0 | 1 |
| `mod.wheel` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| osc.type / `osc.type` | Entrée | value | 1 |
| osc.table / `osc.table` | Entrée | value | 1 |
| osc.mode / `osc.mode` | Entrée | value | 1 |
| osc.ratio / `osc.ratio` | Entrée | value | 1 |
| osc.octave / `osc.octave` | Entrée | value | 1 |
| osc.semi / `osc.semi` | Entrée | value | 1 |
| osc.fine / `osc.fine` | Entrée | value | 1 |
| osc.timbre / `osc.timbre` | Entrée | value | 1 |
| osc.shape / `osc.shape` | Entrée | value | 1 |
| osc.env / `osc.env` | Entrée | value | 1 |
| osc.spread / `osc.spread` | Entrée | value | 1 |
| osc.level / `osc.level` | Entrée | value | 1 |
| sub.level / `sub.level` | Entrée | value | 1 |
| sub.wave / `sub.wave` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| noise.colour / `noise.colour` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| f.drive / `f.drive` | Entrée | value | 1 |
| f.env / `f.env` | Entrée | value | 1 |
| f.key / `f.key` | Entrée | value | 1 |
| f.velocity / `f.velocity` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| pitch.env / `pitch.env` | Entrée | value | 1 |
| pitch.decay / `pitch.decay` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.timbre / `lfo1.timbre` | Entrée | value | 1 |
| lfo1.cutoff / `lfo1.cutoff` | Entrée | value | 1 |
| lfo1.amp / `lfo1.amp` | Entrée | value | 1 |
| mod.wheel / `mod.wheel` | Entrée | value | 1 |

## AMBRE (ambre-synth) {#ambre-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `osc1.wave` | 0 | 0 | 3 |
| `osc1.octave` | -2 | 0 | 2 |
| `osc1.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `osc1.level` | 0 | 0.800000012 | 1 |
| `osc2.wave` | 0 | 0 | 3 |
| `osc2.octave` | -2 | 0 | 2 |
| `osc2.semi` | -12 | 0 | 12 |
| `osc2.fine` | -50 | 7 | 50 |
| `osc2.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `osc2.level` | 0 | 0.600000024 | 1 |
| `osc2.sync` | 0 | 0 | 1 |
| `osc3.wave` | 0 | 1 | 3 |
| `osc3.octave` | -2 | -1 | 2 |
| `osc3.semi` | -12 | 0 | 12 |
| `osc3.fine` | -50 | -5 | 50 |
| `osc3.level` | 0 | 0 | 1 |
| `fm.amount` | 0 | 0 | 1 |
| `noise.level` | 0 | 0 | 1 |
| `drift` | 0 | 0.300000012 | 1 |
| `unison.voices` | 1 | 1 | 4 |
| `unison.detune` | 0 | 12 | 50 |
| `unison.spread` | 0 | 0.699999988 | 1 |
| `hpf` | 20 | 20 | 2000 |
| `filter.model` | 0 | 0 | 3 |
| `filter.mode` | 0 | 0 | 2 |
| `filter.cutoff` | 20 | 1800 | 20000 |
| `filter.resonance` | 0 | 0.25 | 1 |
| `filter.drive` | 0 | 0.200000003 | 1 |
| `filter.env` | -1 | 0.449999988 | 1 |
| `filter.key` | 0 | 0.5 | 1 |
| `filter.velocity` | 0 | 0.300000012 | 1 |
| `amp.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp.decay` | 0.00499999989 | 0.5 | 10 |
| `amp.sustain` | 0 | 0.75 | 1 |
| `amp.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `fenv.decay` | 0.00499999989 | 0.600000024 | 10 |
| `fenv.sustain` | 0 | 0.25 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 12 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0.25 | 3 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.pwm` | 0 | 0 | 1 |
| `lfo2.shape` | 0 | 1 | 4 |
| `lfo2.rate` | 0.0500000007 | 0.400000006 | 20 |
| `lfo2.sync` | 0 | 0 | 12 |
| `lfo2.cutoff` | 0 | 0 | 1 |
| `lfo2.amp` | 0 | 0 | 1 |
| `mod.wheel` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| osc1.wave / `osc1.wave` | Entrée | value | 1 |
| osc1.octave / `osc1.octave` | Entrée | value | 1 |
| osc1.pw / `osc1.pw` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc2.wave / `osc2.wave` | Entrée | value | 1 |
| osc2.octave / `osc2.octave` | Entrée | value | 1 |
| osc2.semi / `osc2.semi` | Entrée | value | 1 |
| osc2.fine / `osc2.fine` | Entrée | value | 1 |
| osc2.pw / `osc2.pw` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| osc2.sync / `osc2.sync` | Entrée | value | 1 |
| osc3.wave / `osc3.wave` | Entrée | value | 1 |
| osc3.octave / `osc3.octave` | Entrée | value | 1 |
| osc3.semi / `osc3.semi` | Entrée | value | 1 |
| osc3.fine / `osc3.fine` | Entrée | value | 1 |
| osc3.level / `osc3.level` | Entrée | value | 1 |
| fm.amount / `fm.amount` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| drift / `drift` | Entrée | value | 1 |
| unison.voices / `unison.voices` | Entrée | value | 1 |
| unison.detune / `unison.detune` | Entrée | value | 1 |
| unison.spread / `unison.spread` | Entrée | value | 1 |
| hpf / `hpf` | Entrée | value | 1 |
| filter.model / `filter.model` | Entrée | value | 1 |
| filter.mode / `filter.mode` | Entrée | value | 1 |
| filter.cutoff / `filter.cutoff` | Entrée | value | 1 |
| filter.resonance / `filter.resonance` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.pwm / `lfo1.pwm` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| lfo2.cutoff / `lfo2.cutoff` | Entrée | value | 1 |
| lfo2.amp / `lfo2.amp` | Entrée | value | 1 |
| mod.wheel / `mod.wheel` | Entrée | value | 1 |

## ARGILE (argile-synth) {#argile-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `unison.voices` | 1 | 1 | 4 |
| `unison.detune` | 0 | 12 | 50 |
| `unison.spread` | 0 | 0.699999988 | 1 |
| `drift` | 0 | 0.25 | 1 |
| `osc1.wave` | 0 | 1 | 3 |
| `osc1.octave` | -3 | 0 | 3 |
| `osc1.semi` | -12 | 0 | 12 |
| `osc1.fine` | -50 | 0 | 50 |
| `osc1.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `osc1.sub` | 0 | 0 | 1 |
| `osc1.level` | 0 | 0.800000012 | 1 |
| `osc1.route` | 0 | 0 | 1 |
| `osc2.wave` | 0 | 1 | 3 |
| `osc2.octave` | -3 | 0 | 3 |
| `osc2.semi` | -12 | 0 | 12 |
| `osc2.fine` | -50 | 7 | 50 |
| `osc2.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `osc2.sub` | 0 | 0 | 1 |
| `osc2.level` | 0 | 0.5 | 1 |
| `osc2.route` | 0 | 0 | 1 |
| `osc2.sync` | 0 | 0 | 1 |
| `osc2.env` | -1 | 0 | 1 |
| `noise.level` | 0 | 0 | 1 |
| `noise.colour` | 0 | 0.5 | 1 |
| `noise.route` | 0 | 0 | 1 |
| `filter1.type` | 0 | 1 | 7 |
| `filter1.cutoff` | 20 | 1800 | 20000 |
| `filter1.resonance` | 0 | 0.200000003 | 1 |
| `filter1.drive` | 0 | 0.150000006 | 1 |
| `filter1.key` | 0 | 0.5 | 1 |
| `filter1.env` | -1 | 0.349999994 | 1 |
| `filter1.vowel` | 0 | 0 | 1 |
| `fenv1.attack` | 0.00100000005 | 0.00300000003 | 10 |
| `fenv1.decay` | 0.00499999989 | 0.600000024 | 10 |
| `fenv1.sustain` | 0 | 0.300000012 | 1 |
| `fenv1.release` | 0.00499999989 | 0.400000006 | 12 |
| `filter2.type` | 0 | 0 | 7 |
| `filter2.cutoff` | 20 | 2500 | 20000 |
| `filter2.resonance` | 0 | 0.200000003 | 1 |
| `filter2.drive` | 0 | 0.150000006 | 1 |
| `filter2.key` | 0 | 0.5 | 1 |
| `filter2.env` | -1 | 0.300000012 | 1 |
| `filter2.vowel` | 0 | 0 | 1 |
| `fenv2.attack` | 0.00100000005 | 0.00300000003 | 10 |
| `fenv2.decay` | 0.00499999989 | 0.600000024 | 10 |
| `fenv2.sustain` | 0 | 0.300000012 | 1 |
| `fenv2.release` | 0.00499999989 | 0.400000006 | 12 |
| `filter.serial` | 0 | 0 | 1 |
| `filter.velocity` | 0 | 0.300000012 | 1 |
| `amp1.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp1.decay` | 0.00499999989 | 0.600000024 | 10 |
| `amp1.sustain` | 0 | 0.800000012 | 1 |
| `amp1.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp1.level` | 0 | 0.800000012 | 1 |
| `amp1.pan` | -1 | 0 | 1 |
| `amp1.velocity` | 0 | 0.5 | 1 |
| `amp2.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp2.decay` | 0.00499999989 | 0.600000024 | 10 |
| `amp2.sustain` | 0 | 0.800000012 | 1 |
| `amp2.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp2.level` | 0 | 0.800000012 | 1 |
| `amp2.pan` | -1 | 0 | 1 |
| `amp2.velocity` | 0 | 0.5 | 1 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0199999996 | 5 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0.300000012 | 4 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.pw` | 0 | 0 | 1 |
| `lfo1.cutoff1` | -1 | 0 | 1 |
| `lfo1.cutoff2` | -1 | 0 | 1 |
| `lfo1.pan` | 0 | 0 | 1 |
| `lfo1.amp` | 0 | 0 | 1 |
| `lfo2.shape` | 0 | 1 | 4 |
| `lfo2.rate` | 0.0199999996 | 0.5 | 30 |
| `lfo2.sync` | 0 | 0 | 12 |
| `lfo2.delay` | 0 | 0 | 4 |
| `lfo2.pitch` | 0 | 0 | 1 |
| `lfo2.pw` | 0 | 0 | 1 |
| `lfo2.cutoff1` | -1 | 0 | 1 |
| `lfo2.cutoff2` | -1 | 0 | 1 |
| `lfo2.pan` | 0 | 0 | 1 |
| `lfo2.amp` | 0 | 0 | 1 |
| `mod.wheel` | 0 | 0.5 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| unison.voices / `unison.voices` | Entrée | value | 1 |
| unison.detune / `unison.detune` | Entrée | value | 1 |
| unison.spread / `unison.spread` | Entrée | value | 1 |
| drift / `drift` | Entrée | value | 1 |
| osc1.wave / `osc1.wave` | Entrée | value | 1 |
| osc1.octave / `osc1.octave` | Entrée | value | 1 |
| osc1.semi / `osc1.semi` | Entrée | value | 1 |
| osc1.fine / `osc1.fine` | Entrée | value | 1 |
| osc1.pw / `osc1.pw` | Entrée | value | 1 |
| osc1.sub / `osc1.sub` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc1.route / `osc1.route` | Entrée | value | 1 |
| osc2.wave / `osc2.wave` | Entrée | value | 1 |
| osc2.octave / `osc2.octave` | Entrée | value | 1 |
| osc2.semi / `osc2.semi` | Entrée | value | 1 |
| osc2.fine / `osc2.fine` | Entrée | value | 1 |
| osc2.pw / `osc2.pw` | Entrée | value | 1 |
| osc2.sub / `osc2.sub` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| osc2.route / `osc2.route` | Entrée | value | 1 |
| osc2.sync / `osc2.sync` | Entrée | value | 1 |
| osc2.env / `osc2.env` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| noise.colour / `noise.colour` | Entrée | value | 1 |
| noise.route / `noise.route` | Entrée | value | 1 |
| filter1.type / `filter1.type` | Entrée | value | 1 |
| filter1.cutoff / `filter1.cutoff` | Entrée | value | 1 |
| filter1.resonance / `filter1.resonance` | Entrée | value | 1 |
| filter1.drive / `filter1.drive` | Entrée | value | 1 |
| filter1.key / `filter1.key` | Entrée | value | 1 |
| filter1.env / `filter1.env` | Entrée | value | 1 |
| filter1.vowel / `filter1.vowel` | Entrée | value | 1 |
| fenv1.attack / `fenv1.attack` | Entrée | value | 1 |
| fenv1.decay / `fenv1.decay` | Entrée | value | 1 |
| fenv1.sustain / `fenv1.sustain` | Entrée | value | 1 |
| fenv1.release / `fenv1.release` | Entrée | value | 1 |
| filter2.type / `filter2.type` | Entrée | value | 1 |
| filter2.cutoff / `filter2.cutoff` | Entrée | value | 1 |
| filter2.resonance / `filter2.resonance` | Entrée | value | 1 |
| filter2.drive / `filter2.drive` | Entrée | value | 1 |
| filter2.key / `filter2.key` | Entrée | value | 1 |
| filter2.env / `filter2.env` | Entrée | value | 1 |
| filter2.vowel / `filter2.vowel` | Entrée | value | 1 |
| fenv2.attack / `fenv2.attack` | Entrée | value | 1 |
| fenv2.decay / `fenv2.decay` | Entrée | value | 1 |
| fenv2.sustain / `fenv2.sustain` | Entrée | value | 1 |
| fenv2.release / `fenv2.release` | Entrée | value | 1 |
| filter.serial / `filter.serial` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| amp1.attack / `amp1.attack` | Entrée | value | 1 |
| amp1.decay / `amp1.decay` | Entrée | value | 1 |
| amp1.sustain / `amp1.sustain` | Entrée | value | 1 |
| amp1.release / `amp1.release` | Entrée | value | 1 |
| amp1.level / `amp1.level` | Entrée | value | 1 |
| amp1.pan / `amp1.pan` | Entrée | value | 1 |
| amp1.velocity / `amp1.velocity` | Entrée | value | 1 |
| amp2.attack / `amp2.attack` | Entrée | value | 1 |
| amp2.decay / `amp2.decay` | Entrée | value | 1 |
| amp2.sustain / `amp2.sustain` | Entrée | value | 1 |
| amp2.release / `amp2.release` | Entrée | value | 1 |
| amp2.level / `amp2.level` | Entrée | value | 1 |
| amp2.pan / `amp2.pan` | Entrée | value | 1 |
| amp2.velocity / `amp2.velocity` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.pw / `lfo1.pw` | Entrée | value | 1 |
| lfo1.cutoff1 / `lfo1.cutoff1` | Entrée | value | 1 |
| lfo1.cutoff2 / `lfo1.cutoff2` | Entrée | value | 1 |
| lfo1.pan / `lfo1.pan` | Entrée | value | 1 |
| lfo1.amp / `lfo1.amp` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| lfo2.delay / `lfo2.delay` | Entrée | value | 1 |
| lfo2.pitch / `lfo2.pitch` | Entrée | value | 1 |
| lfo2.pw / `lfo2.pw` | Entrée | value | 1 |
| lfo2.cutoff1 / `lfo2.cutoff1` | Entrée | value | 1 |
| lfo2.cutoff2 / `lfo2.cutoff2` | Entrée | value | 1 |
| lfo2.pan / `lfo2.pan` | Entrée | value | 1 |
| lfo2.amp / `lfo2.amp` | Entrée | value | 1 |
| mod.wheel / `mod.wheel` | Entrée | value | 1 |

## BATTERIE (batterie) {#batterie}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `transpose` | -48 | 0 | 48 |
| `fine` | -100 | 0 | 100 |
| `start` | 0 | 0 | 2000 |
| `velocity-db` | 0 | 24 | 48 |
| `velocity-crossfade` | 0 | 0 | 1 |
| `loop` | 0 | 1 | 2 |
| `key-track` | 0 | 1 | 1 |
| `reverse` | 0 | 0 | 1 |
| `width` | 0 | 1 | 2 |
| `one-shot` | 0 | 0 | 1 |
| `articulation` | -1 | -1 | 127 |
| `simple` | 0 | 0 | 1 |
| `grains` | 0 | 0 | 1 |
| `grain-position` | 0 | 0.300000012 | 1 |
| `grain-scan` | -2 | 0 | 2 |
| `grain-size` | 10 | 120 | 1000 |
| `grain-density` | 1 | 24 | 120 |
| `grain-spray` | 0 | 0.0500000007 | 1 |
| `grain-pitch` | 0 | 0 | 12 |
| `grain-stereo` | 0 | 0.5 | 1 |
| `grain-reverse` | 0 | 0 | 1 |
| `grain-shape` | 0 | 0.200000003 | 1 |
| `kick.level` | -40 | 0 | 6 |
| `kick.pan` | -1 | 0 | 1 |
| `kick.tune` | -12 | 0 | 12 |
| `kick.decay` | 0 | 1 | 1 |
| `kick.mute` | 0 | 0 | 1 |
| `kick.solo` | 0 | 0 | 1 |
| `snare.level` | -40 | 0 | 6 |
| `snare.pan` | -1 | 0 | 1 |
| `snare.tune` | -12 | 0 | 12 |
| `snare.decay` | 0 | 1 | 1 |
| `snare.mute` | 0 | 0 | 1 |
| `snare.solo` | 0 | 0 | 1 |
| `hihat.level` | -40 | 0 | 6 |
| `hihat.pan` | -1 | 0 | 1 |
| `hihat.tune` | -12 | 0 | 12 |
| `hihat.decay` | 0 | 1 | 1 |
| `hihat.mute` | 0 | 0 | 1 |
| `hihat.solo` | 0 | 0 | 1 |
| `tom1.level` | -40 | 0 | 6 |
| `tom1.pan` | -1 | 0 | 1 |
| `tom1.tune` | -12 | 0 | 12 |
| `tom1.decay` | 0 | 1 | 1 |
| `tom1.mute` | 0 | 0 | 1 |
| `tom1.solo` | 0 | 0 | 1 |
| `tom2.level` | -40 | 0 | 6 |
| `tom2.pan` | -1 | 0 | 1 |
| `tom2.tune` | -12 | 0 | 12 |
| `tom2.decay` | 0 | 1 | 1 |
| `tom2.mute` | 0 | 0 | 1 |
| `tom2.solo` | 0 | 0 | 1 |
| `tom3.level` | -40 | 0 | 6 |
| `tom3.pan` | -1 | 0 | 1 |
| `tom3.tune` | -12 | 0 | 12 |
| `tom3.decay` | 0 | 1 | 1 |
| `tom3.mute` | 0 | 0 | 1 |
| `tom3.solo` | 0 | 0 | 1 |
| `tom4.level` | -40 | 0 | 6 |
| `tom4.pan` | -1 | 0 | 1 |
| `tom4.tune` | -12 | 0 | 12 |
| `tom4.decay` | 0 | 1 | 1 |
| `tom4.mute` | 0 | 0 | 1 |
| `tom4.solo` | 0 | 0 | 1 |
| `crash.level` | -40 | 0 | 6 |
| `crash.pan` | -1 | 0 | 1 |
| `crash.tune` | -12 | 0 | 12 |
| `crash.decay` | 0 | 1 | 1 |
| `crash.mute` | 0 | 0 | 1 |
| `crash.solo` | 0 | 0 | 1 |
| `ride.level` | -40 | 0 | 6 |
| `ride.pan` | -1 | 0 | 1 |
| `ride.tune` | -12 | 0 | 12 |
| `ride.decay` | 0 | 1 | 1 |
| `ride.mute` | 0 | 0 | 1 |
| `ride.solo` | 0 | 0 | 1 |
| `china.level` | -40 | 0 | 6 |
| `china.pan` | -1 | 0 | 1 |
| `china.tune` | -12 | 0 | 12 |
| `china.decay` | 0 | 1 | 1 |
| `china.mute` | 0 | 0 | 1 |
| `china.solo` | 0 | 0 | 1 |
| `perc.level` | -40 | 0 | 6 |
| `perc.pan` | -1 | 0 | 1 |
| `perc.tune` | -12 | 0 | 12 |
| `perc.decay` | 0 | 1 | 1 |
| `perc.mute` | 0 | 0 | 1 |
| `perc.solo` | 0 | 0 | 1 |
| `other.level` | -40 | 0 | 6 |
| `other.pan` | -1 | 0 | 1 |
| `other.tune` | -12 | 0 | 12 |
| `other.decay` | 0 | 1 | 1 |
| `other.mute` | 0 | 0 | 1 |
| `other.solo` | 0 | 0 | 1 |
| `kit.kit` | 0 | 0 | 5 |
| `kit.tune` | -12 | 0 | 12 |
| `kit.humanise` | 0 | 0 | 1 |
| `kit.level` | -40 | 0 | 6 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| transpose / `transpose` | Entrée | value | 1 |
| fine / `fine` | Entrée | value | 1 |
| start / `start` | Entrée | value | 1 |
| velocity-db / `velocity-db` | Entrée | value | 1 |
| velocity-crossfade / `velocity-crossfade` | Entrée | value | 1 |
| loop / `loop` | Entrée | value | 1 |
| key-track / `key-track` | Entrée | value | 1 |
| reverse / `reverse` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| one-shot / `one-shot` | Entrée | value | 1 |
| articulation / `articulation` | Entrée | value | 1 |
| simple / `simple` | Entrée | value | 1 |
| grains / `grains` | Entrée | value | 1 |
| grain-position / `grain-position` | Entrée | value | 1 |
| grain-scan / `grain-scan` | Entrée | value | 1 |
| grain-size / `grain-size` | Entrée | value | 1 |
| grain-density / `grain-density` | Entrée | value | 1 |
| grain-spray / `grain-spray` | Entrée | value | 1 |
| grain-pitch / `grain-pitch` | Entrée | value | 1 |
| grain-stereo / `grain-stereo` | Entrée | value | 1 |
| grain-reverse / `grain-reverse` | Entrée | value | 1 |
| grain-shape / `grain-shape` | Entrée | value | 1 |
| kick.level / `kick.level` | Entrée | value | 1 |
| kick.pan / `kick.pan` | Entrée | value | 1 |
| kick.tune / `kick.tune` | Entrée | value | 1 |
| kick.decay / `kick.decay` | Entrée | value | 1 |
| kick.mute / `kick.mute` | Entrée | value | 1 |
| kick.solo / `kick.solo` | Entrée | value | 1 |
| snare.level / `snare.level` | Entrée | value | 1 |
| snare.pan / `snare.pan` | Entrée | value | 1 |
| snare.tune / `snare.tune` | Entrée | value | 1 |
| snare.decay / `snare.decay` | Entrée | value | 1 |
| snare.mute / `snare.mute` | Entrée | value | 1 |
| snare.solo / `snare.solo` | Entrée | value | 1 |
| hihat.level / `hihat.level` | Entrée | value | 1 |
| hihat.pan / `hihat.pan` | Entrée | value | 1 |
| hihat.tune / `hihat.tune` | Entrée | value | 1 |
| hihat.decay / `hihat.decay` | Entrée | value | 1 |
| hihat.mute / `hihat.mute` | Entrée | value | 1 |
| hihat.solo / `hihat.solo` | Entrée | value | 1 |
| tom1.level / `tom1.level` | Entrée | value | 1 |
| tom1.pan / `tom1.pan` | Entrée | value | 1 |
| tom1.tune / `tom1.tune` | Entrée | value | 1 |
| tom1.decay / `tom1.decay` | Entrée | value | 1 |
| tom1.mute / `tom1.mute` | Entrée | value | 1 |
| tom1.solo / `tom1.solo` | Entrée | value | 1 |
| tom2.level / `tom2.level` | Entrée | value | 1 |
| tom2.pan / `tom2.pan` | Entrée | value | 1 |
| tom2.tune / `tom2.tune` | Entrée | value | 1 |
| tom2.decay / `tom2.decay` | Entrée | value | 1 |
| tom2.mute / `tom2.mute` | Entrée | value | 1 |
| tom2.solo / `tom2.solo` | Entrée | value | 1 |
| tom3.level / `tom3.level` | Entrée | value | 1 |
| tom3.pan / `tom3.pan` | Entrée | value | 1 |
| tom3.tune / `tom3.tune` | Entrée | value | 1 |
| tom3.decay / `tom3.decay` | Entrée | value | 1 |
| tom3.mute / `tom3.mute` | Entrée | value | 1 |
| tom3.solo / `tom3.solo` | Entrée | value | 1 |
| tom4.level / `tom4.level` | Entrée | value | 1 |
| tom4.pan / `tom4.pan` | Entrée | value | 1 |
| tom4.tune / `tom4.tune` | Entrée | value | 1 |
| tom4.decay / `tom4.decay` | Entrée | value | 1 |
| tom4.mute / `tom4.mute` | Entrée | value | 1 |
| tom4.solo / `tom4.solo` | Entrée | value | 1 |
| crash.level / `crash.level` | Entrée | value | 1 |
| crash.pan / `crash.pan` | Entrée | value | 1 |
| crash.tune / `crash.tune` | Entrée | value | 1 |
| crash.decay / `crash.decay` | Entrée | value | 1 |
| crash.mute / `crash.mute` | Entrée | value | 1 |
| crash.solo / `crash.solo` | Entrée | value | 1 |
| ride.level / `ride.level` | Entrée | value | 1 |
| ride.pan / `ride.pan` | Entrée | value | 1 |
| ride.tune / `ride.tune` | Entrée | value | 1 |
| ride.decay / `ride.decay` | Entrée | value | 1 |
| ride.mute / `ride.mute` | Entrée | value | 1 |
| ride.solo / `ride.solo` | Entrée | value | 1 |
| china.level / `china.level` | Entrée | value | 1 |
| china.pan / `china.pan` | Entrée | value | 1 |
| china.tune / `china.tune` | Entrée | value | 1 |
| china.decay / `china.decay` | Entrée | value | 1 |
| china.mute / `china.mute` | Entrée | value | 1 |
| china.solo / `china.solo` | Entrée | value | 1 |
| perc.level / `perc.level` | Entrée | value | 1 |
| perc.pan / `perc.pan` | Entrée | value | 1 |
| perc.tune / `perc.tune` | Entrée | value | 1 |
| perc.decay / `perc.decay` | Entrée | value | 1 |
| perc.mute / `perc.mute` | Entrée | value | 1 |
| perc.solo / `perc.solo` | Entrée | value | 1 |
| other.level / `other.level` | Entrée | value | 1 |
| other.pan / `other.pan` | Entrée | value | 1 |
| other.tune / `other.tune` | Entrée | value | 1 |
| other.decay / `other.decay` | Entrée | value | 1 |
| other.mute / `other.mute` | Entrée | value | 1 |
| other.solo / `other.solo` | Entrée | value | 1 |
| kit.kit / `kit.kit` | Entrée | value | 1 |
| kit.tune / `kit.tune` | Entrée | value | 1 |
| kit.humanise / `kit.humanise` | Entrée | value | 1 |
| kit.level / `kit.level` | Entrée | value | 1 |

## BRONZE (bronze-synth) {#bronze-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 12 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `mallet.volume` | 0 | 1 | 1 |
| `mallet.stiffness` | 0 | 0.449999988 | 1 |
| `mallet.noise` | 0 | 0.100000001 | 1 |
| `mallet.colour` | 0 | 0.5 | 1 |
| `noise.volume` | 0 | 0 | 1 |
| `noise.filter` | 0 | 0 | 2 |
| `noise.cutoff` | 20 | 3000 | 20000 |
| `noise.resonance` | 0 | 0.200000003 | 1 |
| `noise.key` | 0 | 0 | 1 |
| `noise.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `noise.decay` | 0.00499999989 | 0.150000006 | 10 |
| `noise.sustain` | 0 | 0 | 1 |
| `noise.release` | 0.00499999989 | 0.100000001 | 10 |
| `r1.type` | 0 | 1 | 6 |
| `r1.quality` | 4 | 16 | 32 |
| `r1.tune` | -48 | 0 | 48 |
| `r1.fine` | -50 | 0 | 50 |
| `r1.decay` | 0.0199999996 | 1.20000005 | 20 |
| `r1.material` | -1 | 0.400000006 | 1 |
| `r1.bright` | 0 | 0.550000012 | 1 |
| `r1.inharm` | -1 | 0 | 1 |
| `r1.hit` | 0 | 0.200000003 | 1 |
| `r1.left` | 0 | 0.150000006 | 1 |
| `r1.right` | 0 | 0.449999988 | 1 |
| `r2.type` | 0 | 6 | 6 |
| `r2.quality` | 4 | 16 | 32 |
| `r2.tune` | -48 | 0 | 48 |
| `r2.fine` | -50 | 0 | 50 |
| `r2.decay` | 0.0199999996 | 0.600000024 | 20 |
| `r2.material` | -1 | 0.300000012 | 1 |
| `r2.bright` | 0 | 0.449999988 | 1 |
| `r2.inharm` | -1 | 0 | 1 |
| `r2.hit` | 0 | 0.150000006 | 1 |
| `r2.left` | 0 | 0.200000003 | 1 |
| `r2.right` | 0 | 0.550000012 | 1 |
| `structure` | 0 | 0 | 1 |
| `balance` | 0 | 0.349999994 | 1 |
| `release` | 0.00999999978 | 1 | 10 |
| `velocity.volume` | 0 | 0.600000024 | 1 |
| `velocity.stiffness` | 0 | 0.5 | 1 |
| `output.gain` | -24 | 0 | 12 |
| `output.limiter` | 0 | 1 | 1 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 4 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.tune` | -12 | 0 | 12 |
| `lfo1.decay` | -1 | 0 | 1 |
| `lfo1.hit` | -1 | 0 | 1 |
| `lfo1.volume` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| mallet.volume / `mallet.volume` | Entrée | value | 1 |
| mallet.stiffness / `mallet.stiffness` | Entrée | value | 1 |
| mallet.noise / `mallet.noise` | Entrée | value | 1 |
| mallet.colour / `mallet.colour` | Entrée | value | 1 |
| noise.volume / `noise.volume` | Entrée | value | 1 |
| noise.filter / `noise.filter` | Entrée | value | 1 |
| noise.cutoff / `noise.cutoff` | Entrée | value | 1 |
| noise.resonance / `noise.resonance` | Entrée | value | 1 |
| noise.key / `noise.key` | Entrée | value | 1 |
| noise.attack / `noise.attack` | Entrée | value | 1 |
| noise.decay / `noise.decay` | Entrée | value | 1 |
| noise.sustain / `noise.sustain` | Entrée | value | 1 |
| noise.release / `noise.release` | Entrée | value | 1 |
| r1.type / `r1.type` | Entrée | value | 1 |
| r1.quality / `r1.quality` | Entrée | value | 1 |
| r1.tune / `r1.tune` | Entrée | value | 1 |
| r1.fine / `r1.fine` | Entrée | value | 1 |
| r1.decay / `r1.decay` | Entrée | value | 1 |
| r1.material / `r1.material` | Entrée | value | 1 |
| r1.bright / `r1.bright` | Entrée | value | 1 |
| r1.inharm / `r1.inharm` | Entrée | value | 1 |
| r1.hit / `r1.hit` | Entrée | value | 1 |
| r1.left / `r1.left` | Entrée | value | 1 |
| r1.right / `r1.right` | Entrée | value | 1 |
| r2.type / `r2.type` | Entrée | value | 1 |
| r2.quality / `r2.quality` | Entrée | value | 1 |
| r2.tune / `r2.tune` | Entrée | value | 1 |
| r2.fine / `r2.fine` | Entrée | value | 1 |
| r2.decay / `r2.decay` | Entrée | value | 1 |
| r2.material / `r2.material` | Entrée | value | 1 |
| r2.bright / `r2.bright` | Entrée | value | 1 |
| r2.inharm / `r2.inharm` | Entrée | value | 1 |
| r2.hit / `r2.hit` | Entrée | value | 1 |
| r2.left / `r2.left` | Entrée | value | 1 |
| r2.right / `r2.right` | Entrée | value | 1 |
| structure / `structure` | Entrée | value | 1 |
| balance / `balance` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| velocity.volume / `velocity.volume` | Entrée | value | 1 |
| velocity.stiffness / `velocity.stiffness` | Entrée | value | 1 |
| output.gain / `output.gain` | Entrée | value | 1 |
| output.limiter / `output.limiter` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.tune / `lfo1.tune` | Entrée | value | 1 |
| lfo1.decay / `lfo1.decay` | Entrée | value | 1 |
| lfo1.hit / `lfo1.hit` | Entrée | value | 1 |
| lfo1.volume / `lfo1.volume` | Entrée | value | 1 |

## COLOSSE (colosse-synth) {#colosse-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `osc1.table` | 0 | 0 | 7 |
| `osc1.position` | 0 | 0.569999993 | 1 |
| `osc1.mode` | 0 | 0 | 7 |
| `osc1.warp` | 0 | 0 | 1 |
| `osc1.octave` | -3 | 0 | 3 |
| `osc1.semi` | -12 | 0 | 12 |
| `osc1.fine` | -50 | 0 | 50 |
| `osc1.level` | 0 | 0.800000012 | 1 |
| `osc2.table` | 0 | 1 | 7 |
| `osc2.position` | 0 | 0.449999988 | 1 |
| `osc2.mode` | 0 | 0 | 7 |
| `osc2.warp` | 0 | 0 | 1 |
| `osc2.octave` | -3 | 0 | 3 |
| `osc2.semi` | -12 | 0 | 12 |
| `osc2.fine` | -50 | 7 | 50 |
| `osc2.level` | 0 | 0.449999988 | 1 |
| `pm.ratio` | 0 | 1 | 7 |
| `pm.osc1` | 0 | 0 | 1 |
| `pm.osc2` | 0 | 0 | 1 |
| `sub.level` | 0 | 0 | 1 |
| `noise.colour` | 0 | 0 | 3 |
| `noise.level` | 0 | 0 | 1 |
| `unison.voices` | 1 | 1 | 8 |
| `unison.detune` | 0 | 15 | 50 |
| `unison.spread` | 0 | 0.800000012 | 1 |
| `f1.type` | 0 | 0 | 6 |
| `f1.cutoff` | 20 | 2200 | 20000 |
| `f1.resonance` | 0 | 0.200000003 | 1 |
| `f2.type` | 0 | 2 | 6 |
| `f2.cutoff` | 20 | 1200 | 20000 |
| `f2.resonance` | 0 | 0.300000012 | 1 |
| `filter.routing` | 0 | 0 | 1 |
| `filter.blend` | 0 | 0 | 1 |
| `filter.key` | 0 | 0.400000006 | 1 |
| `filter.velocity` | 0 | 0.200000003 | 1 |
| `drive.type` | 0 | 0 | 4 |
| `drive.amount` | 0 | 0 | 1 |
| `drive.mix` | 0 | 1 | 1 |
| `amp.attack` | 0.00100000005 | 0.00300000003 | 10 |
| `amp.decay` | 0.00499999989 | 0.600000024 | 10 |
| `amp.sustain` | 0 | 0.800000012 | 1 |
| `amp.release` | 0.00499999989 | 0.300000012 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `env2.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `env2.decay` | 0.00499999989 | 0.5 | 10 |
| `env2.sustain` | 0 | 0.200000003 | 1 |
| `env2.release` | 0.00499999989 | 0.400000006 | 12 |
| `env3.attack` | 0.00100000005 | 0.400000006 | 10 |
| `env3.decay` | 0.00499999989 | 1.5 | 10 |
| `env3.sustain` | 0 | 0.5 | 1 |
| `env3.release` | 0.00499999989 | 1 | 12 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 4 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo2.shape` | 0 | 1 | 4 |
| `lfo2.rate` | 0.0500000007 | 0.5 | 30 |
| `lfo2.sync` | 0 | 0 | 12 |
| `perf.steps` | 1 | 8 | 16 |
| `perf.sync` | 0 | 3 | 12 |
| `perf.rate` | 0.100000001 | 4 | 30 |
| `perf.smooth` | 0 | 0.100000001 | 1 |
| `perf.step1` | -1 | 1 | 1 |
| `perf.step2` | -1 | 0 | 1 |
| `perf.step3` | -1 | 0.5 | 1 |
| `perf.step4` | -1 | 0 | 1 |
| `perf.step5` | -1 | 0.75 | 1 |
| `perf.step6` | -1 | 0 | 1 |
| `perf.step7` | -1 | 0.25 | 1 |
| `perf.step8` | -1 | 0 | 1 |
| `perf.step9` | -1 | 1 | 1 |
| `perf.step10` | -1 | 0 | 1 |
| `perf.step11` | -1 | 0.5 | 1 |
| `perf.step12` | -1 | 0 | 1 |
| `perf.step13` | -1 | 0.75 | 1 |
| `perf.step14` | -1 | 0 | 1 |
| `perf.step15` | -1 | 0.25 | 1 |
| `perf.step16` | -1 | -0.5 | 1 |
| `mod1.source` | 0 | 1 | 10 |
| `mod1.dest` | 0 | 10 | 17 |
| `mod1.amount` | -1 | 0.349999994 | 1 |
| `mod2.source` | 0 | 0 | 10 |
| `mod2.dest` | 0 | 0 | 17 |
| `mod2.amount` | -1 | 0 | 1 |
| `mod3.source` | 0 | 0 | 10 |
| `mod3.dest` | 0 | 0 | 17 |
| `mod3.amount` | -1 | 0 | 1 |
| `mod4.source` | 0 | 0 | 10 |
| `mod4.dest` | 0 | 0 | 17 |
| `mod4.amount` | -1 | 0 | 1 |
| `mod5.source` | 0 | 0 | 10 |
| `mod5.dest` | 0 | 0 | 17 |
| `mod5.amount` | -1 | 0 | 1 |
| `mod6.source` | 0 | 0 | 10 |
| `mod6.dest` | 0 | 0 | 17 |
| `mod6.amount` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| osc1.table / `osc1.table` | Entrée | value | 1 |
| osc1.position / `osc1.position` | Entrée | value | 1 |
| osc1.mode / `osc1.mode` | Entrée | value | 1 |
| osc1.warp / `osc1.warp` | Entrée | value | 1 |
| osc1.octave / `osc1.octave` | Entrée | value | 1 |
| osc1.semi / `osc1.semi` | Entrée | value | 1 |
| osc1.fine / `osc1.fine` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc2.table / `osc2.table` | Entrée | value | 1 |
| osc2.position / `osc2.position` | Entrée | value | 1 |
| osc2.mode / `osc2.mode` | Entrée | value | 1 |
| osc2.warp / `osc2.warp` | Entrée | value | 1 |
| osc2.octave / `osc2.octave` | Entrée | value | 1 |
| osc2.semi / `osc2.semi` | Entrée | value | 1 |
| osc2.fine / `osc2.fine` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| pm.ratio / `pm.ratio` | Entrée | value | 1 |
| pm.osc1 / `pm.osc1` | Entrée | value | 1 |
| pm.osc2 / `pm.osc2` | Entrée | value | 1 |
| sub.level / `sub.level` | Entrée | value | 1 |
| noise.colour / `noise.colour` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| unison.voices / `unison.voices` | Entrée | value | 1 |
| unison.detune / `unison.detune` | Entrée | value | 1 |
| unison.spread / `unison.spread` | Entrée | value | 1 |
| f1.type / `f1.type` | Entrée | value | 1 |
| f1.cutoff / `f1.cutoff` | Entrée | value | 1 |
| f1.resonance / `f1.resonance` | Entrée | value | 1 |
| f2.type / `f2.type` | Entrée | value | 1 |
| f2.cutoff / `f2.cutoff` | Entrée | value | 1 |
| f2.resonance / `f2.resonance` | Entrée | value | 1 |
| filter.routing / `filter.routing` | Entrée | value | 1 |
| filter.blend / `filter.blend` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| drive.type / `drive.type` | Entrée | value | 1 |
| drive.amount / `drive.amount` | Entrée | value | 1 |
| drive.mix / `drive.mix` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| env2.attack / `env2.attack` | Entrée | value | 1 |
| env2.decay / `env2.decay` | Entrée | value | 1 |
| env2.sustain / `env2.sustain` | Entrée | value | 1 |
| env2.release / `env2.release` | Entrée | value | 1 |
| env3.attack / `env3.attack` | Entrée | value | 1 |
| env3.decay / `env3.decay` | Entrée | value | 1 |
| env3.sustain / `env3.sustain` | Entrée | value | 1 |
| env3.release / `env3.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| perf.steps / `perf.steps` | Entrée | value | 1 |
| perf.sync / `perf.sync` | Entrée | value | 1 |
| perf.rate / `perf.rate` | Entrée | value | 1 |
| perf.smooth / `perf.smooth` | Entrée | value | 1 |
| perf.step1 / `perf.step1` | Entrée | value | 1 |
| perf.step2 / `perf.step2` | Entrée | value | 1 |
| perf.step3 / `perf.step3` | Entrée | value | 1 |
| perf.step4 / `perf.step4` | Entrée | value | 1 |
| perf.step5 / `perf.step5` | Entrée | value | 1 |
| perf.step6 / `perf.step6` | Entrée | value | 1 |
| perf.step7 / `perf.step7` | Entrée | value | 1 |
| perf.step8 / `perf.step8` | Entrée | value | 1 |
| perf.step9 / `perf.step9` | Entrée | value | 1 |
| perf.step10 / `perf.step10` | Entrée | value | 1 |
| perf.step11 / `perf.step11` | Entrée | value | 1 |
| perf.step12 / `perf.step12` | Entrée | value | 1 |
| perf.step13 / `perf.step13` | Entrée | value | 1 |
| perf.step14 / `perf.step14` | Entrée | value | 1 |
| perf.step15 / `perf.step15` | Entrée | value | 1 |
| perf.step16 / `perf.step16` | Entrée | value | 1 |
| mod1.source / `mod1.source` | Entrée | value | 1 |
| mod1.dest / `mod1.dest` | Entrée | value | 1 |
| mod1.amount / `mod1.amount` | Entrée | value | 1 |
| mod2.source / `mod2.source` | Entrée | value | 1 |
| mod2.dest / `mod2.dest` | Entrée | value | 1 |
| mod2.amount / `mod2.amount` | Entrée | value | 1 |
| mod3.source / `mod3.source` | Entrée | value | 1 |
| mod3.dest / `mod3.dest` | Entrée | value | 1 |
| mod3.amount / `mod3.amount` | Entrée | value | 1 |
| mod4.source / `mod4.source` | Entrée | value | 1 |
| mod4.dest / `mod4.dest` | Entrée | value | 1 |
| mod4.amount / `mod4.amount` | Entrée | value | 1 |
| mod5.source / `mod5.source` | Entrée | value | 1 |
| mod5.dest / `mod5.dest` | Entrée | value | 1 |
| mod5.amount / `mod5.amount` | Entrée | value | 1 |
| mod6.source / `mod6.source` | Entrée | value | 1 |
| mod6.dest / `mod6.dest` | Entrée | value | 1 |
| mod6.amount / `mod6.amount` | Entrée | value | 1 |

## DÉRIVE (derive-synth) {#derive-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 12 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `unison.mode` | 0 | 1 | 2 |
| `unison.thickness` | 0 | 0.300000012 | 1 |
| `osc1.wave` | 0 | 4 | 6 |
| `osc1.shape` | 0 | 0 | 1 |
| `osc1.octave` | -2 | 0 | 2 |
| `osc2.wave` | 0 | 3 | 4 |
| `osc2.octave` | -2 | 0 | 2 |
| `osc2.semi` | -12 | 0 | 12 |
| `osc2.detune` | -50 | 6 | 50 |
| `osc1.level` | 0 | 0.800000012 | 1 |
| `osc2.level` | 0 | 0.5 | 1 |
| `noise.level` | 0 | 0 | 1 |
| `drift` | 0 | 0.349999994 | 1 |
| `osc1.filter` | 0 | 1 | 1 |
| `osc2.filter` | 0 | 1 | 1 |
| `f.type` | 0 | 0 | 1 |
| `f.cutoff` | 20 | 1400 | 20000 |
| `f.resonance` | 0 | 0.150000006 | 1 |
| `filter.hpf` | 20 | 20 | 2000 |
| `filter.key` | 0 | 0.5 | 1 |
| `filter.env` | -1 | 0.300000012 | 1 |
| `filter.lfo` | 0 | 0 | 1 |
| `amp.attack` | 0.00100000005 | 0.00499999989 | 10 |
| `amp.decay` | 0.00499999989 | 0.800000012 | 10 |
| `amp.sustain` | 0 | 0.75 | 1 |
| `amp.release` | 0.00499999989 | 0.449999988 | 12 |
| `amp.velocity` | 0 | 0.400000006 | 1 |
| `env2.attack` | 0.00100000005 | 0.00300000003 | 10 |
| `env2.decay` | 0.00499999989 | 0.600000024 | 10 |
| `env2.sustain` | 0 | 0.300000012 | 1 |
| `env2.release` | 0.00499999989 | 0.5 | 12 |
| `env2.cycle` | 0 | 0 | 1 |
| `env2.rate` | 0.0500000007 | 2 | 30 |
| `env2.sync` | 0 | 0 | 12 |
| `env2.tilt` | -1 | 0 | 1 |
| `env2.hold` | 0 | 0 | 1 |
| `lfo1.shape` | 0 | 0 | 6 |
| `lfo1.rate` | 0.0199999996 | 5 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.retrigger` | 0 | 0 | 1 |
| `lfo1.amount` | 0 | 0 | 1 |
| `mod1.source` | 0 | 1 | 5 |
| `mod1.dest` | 0 | 0 | 8 |
| `mod1.amount` | -1 | 0.100000001 | 1 |
| `mod2.source` | 0 | 2 | 5 |
| `mod2.dest` | 0 | 4 | 8 |
| `mod2.amount` | -1 | 0.150000006 | 1 |
| `mod3.source` | 0 | 3 | 5 |
| `mod3.dest` | 0 | 8 | 8 |
| `mod3.amount` | -1 | 1 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| unison.mode / `unison.mode` | Entrée | value | 1 |
| unison.thickness / `unison.thickness` | Entrée | value | 1 |
| osc1.wave / `osc1.wave` | Entrée | value | 1 |
| osc1.shape / `osc1.shape` | Entrée | value | 1 |
| osc1.octave / `osc1.octave` | Entrée | value | 1 |
| osc2.wave / `osc2.wave` | Entrée | value | 1 |
| osc2.octave / `osc2.octave` | Entrée | value | 1 |
| osc2.semi / `osc2.semi` | Entrée | value | 1 |
| osc2.detune / `osc2.detune` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| drift / `drift` | Entrée | value | 1 |
| osc1.filter / `osc1.filter` | Entrée | value | 1 |
| osc2.filter / `osc2.filter` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.hpf / `filter.hpf` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.lfo / `filter.lfo` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| env2.attack / `env2.attack` | Entrée | value | 1 |
| env2.decay / `env2.decay` | Entrée | value | 1 |
| env2.sustain / `env2.sustain` | Entrée | value | 1 |
| env2.release / `env2.release` | Entrée | value | 1 |
| env2.cycle / `env2.cycle` | Entrée | value | 1 |
| env2.rate / `env2.rate` | Entrée | value | 1 |
| env2.sync / `env2.sync` | Entrée | value | 1 |
| env2.tilt / `env2.tilt` | Entrée | value | 1 |
| env2.hold / `env2.hold` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.retrigger / `lfo1.retrigger` | Entrée | value | 1 |
| lfo1.amount / `lfo1.amount` | Entrée | value | 1 |
| mod1.source / `mod1.source` | Entrée | value | 1 |
| mod1.dest / `mod1.dest` | Entrée | value | 1 |
| mod1.amount / `mod1.amount` | Entrée | value | 1 |
| mod2.source / `mod2.source` | Entrée | value | 1 |
| mod2.dest / `mod2.dest` | Entrée | value | 1 |
| mod2.amount / `mod2.amount` | Entrée | value | 1 |
| mod3.source / `mod3.source` | Entrée | value | 1 |
| mod3.dest / `mod3.dest` | Entrée | value | 1 |
| mod3.amount / `mod3.amount` | Entrée | value | 1 |

## ADSR ENVELOPE (envelope) {#envelope}

Catégorie : DSP / Instruments.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `attack` | 0.1 | 10 | 30000 |
| `decay` | 0.1 | 200 | 30000 |
| `sustain` | 0 | 0.8 | 1 |
| `release` | 0.1 | 500 | 60000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| gate / `gate` | Entrée | trigger | 1 |
| envelope / `envelope` | Sortie | value | 1 |

## JADE (jade-synth) {#jade-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.5 | 1 |
| `model` | 0 | 0 | 2 |
| `tune.stretch` | 0 | 3 | 30 |
| `tune.detune` | 0 | 1.5 | 20 |
| `hammer.hardness` | 0 | 0.449999988 | 1 |
| `hammer.velocity` | 0 | 0.600000024 | 1 |
| `hammer.position` | 0 | 0.300000012 | 1 |
| `velocity.curve` | -1 | 0 | 1 |
| `amp.velocity` | 0 | 0.699999988 | 1 |
| `tine.decay` | 0.300000012 | 6 | 30 |
| `tine.tracking` | 0 | 0.550000012 | 1 |
| `tine.damper` | 0.00999999978 | 0.100000001 | 3 |
| `tine.bar` | 0 | 0.550000012 | 1 |
| `tine.harmonics` | 0 | 0.5 | 1 |
| `pickup.proximity` | 0 | 0.400000006 | 1 |
| `pickup.symmetry` | 0 | 0.5 | 1 |
| `pickup.position` | 0 | 0.5 | 1 |
| `pickup.tone` | 1000 | 7000 | 16000 |
| `noise.thump` | 0 | 0.300000012 | 1 |
| `noise.release` | 0 | 0.300000012 | 1 |
| `tone.bass` | -12 | 0 | 12 |
| `tone.body` | -12 | 0 | 12 |
| `tone.treble` | -12 | 0 | 12 |
| `drive.amount` | 0 | 0.100000001 | 1 |
| `drive.output` | -18 | 0 | 6 |
| `stereo.spread` | 0 | 0.25 | 1 |
| `tremolo.shape` | 0 | 0 | 4 |
| `tremolo.rate` | 0.100000001 | 4.5 | 15 |
| `tremolo.sync` | 0 | 0 | 12 |
| `tremolo.depth` | 0 | 0 | 1 |
| `tremolo.stereo` | 0 | 1 | 1 |
| `chorus.rate` | 0.0500000007 | 0.699999988 | 5 |
| `chorus.depth` | 0 | 0.5 | 1 |
| `chorus.mix` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| model / `model` | Entrée | value | 1 |
| tune.stretch / `tune.stretch` | Entrée | value | 1 |
| tune.detune / `tune.detune` | Entrée | value | 1 |
| hammer.hardness / `hammer.hardness` | Entrée | value | 1 |
| hammer.velocity / `hammer.velocity` | Entrée | value | 1 |
| hammer.position / `hammer.position` | Entrée | value | 1 |
| velocity.curve / `velocity.curve` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| tine.decay / `tine.decay` | Entrée | value | 1 |
| tine.tracking / `tine.tracking` | Entrée | value | 1 |
| tine.damper / `tine.damper` | Entrée | value | 1 |
| tine.bar / `tine.bar` | Entrée | value | 1 |
| tine.harmonics / `tine.harmonics` | Entrée | value | 1 |
| pickup.proximity / `pickup.proximity` | Entrée | value | 1 |
| pickup.symmetry / `pickup.symmetry` | Entrée | value | 1 |
| pickup.position / `pickup.position` | Entrée | value | 1 |
| pickup.tone / `pickup.tone` | Entrée | value | 1 |
| noise.thump / `noise.thump` | Entrée | value | 1 |
| noise.release / `noise.release` | Entrée | value | 1 |
| tone.bass / `tone.bass` | Entrée | value | 1 |
| tone.body / `tone.body` | Entrée | value | 1 |
| tone.treble / `tone.treble` | Entrée | value | 1 |
| drive.amount / `drive.amount` | Entrée | value | 1 |
| drive.output / `drive.output` | Entrée | value | 1 |
| stereo.spread / `stereo.spread` | Entrée | value | 1 |
| tremolo.shape / `tremolo.shape` | Entrée | value | 1 |
| tremolo.rate / `tremolo.rate` | Entrée | value | 1 |
| tremolo.sync / `tremolo.sync` | Entrée | value | 1 |
| tremolo.depth / `tremolo.depth` | Entrée | value | 1 |
| tremolo.stereo / `tremolo.stereo` | Entrée | value | 1 |
| chorus.rate / `chorus.rate` | Entrée | value | 1 |
| chorus.depth / `chorus.depth` | Entrée | value | 1 |
| chorus.mix / `chorus.mix` | Entrée | value | 1 |

## KICK (kick-synth) {#kick-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `bodyShape` | 0 | 0 | 3 |
| `bodyPhase` | 0 | 0 | 180 |
| `pitchStart` | 40 | 320 | 8000 |
| `pitchEnd` | 20 | 52 | 400 |
| `pitchTime` | 2 | 110 | 1000 |
| `pitchCurve` | 0.5 | 4 | 16 |
| `bodyLevel` | -48 | 0 | 6 |
| `attack` | 0 | 0.400000006 | 30 |
| `hold` | 0 | 30 | 1000 |
| `decay` | 10 | 420 | 4000 |
| `decayCurve` | 0.300000012 | 1.79999995 | 8 |
| `length` | 50 | 1500 | 6000 |
| `clickType` | 0 | 1 | 3 |
| `clickLevel` | -48 | -10 | 6 |
| `clickDecay` | 0.5 | 6 | 80 |
| `clickTone` | 500 | 4500 | 16000 |
| `subLevel` | -60 | -60 | 6 |
| `subDecay` | 20 | 600 | 6000 |
| `subOctave` | 0 | 0 | 1 |
| `driveType` | 0 | 0 | 3 |
| `drive` | 0 | 0 | 36 |
| `driveTone` | 500 | 20000 | 20000 |
| `lowCut` | 10 | 15 | 200 |
| `highCut` | 1000 | 20000 | 20000 |
| `lowBoost` | 0 | 0 | 12 |
| `keyTrack` | 0 | 1 | 1 |
| `velocity` | 0 | 0.5 | 1 |
| `output` | -24 | -3 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| bodyShape / `bodyShape` | Entrée | value | 1 |
| bodyPhase / `bodyPhase` | Entrée | value | 1 |
| pitchStart / `pitchStart` | Entrée | value | 1 |
| pitchEnd / `pitchEnd` | Entrée | value | 1 |
| pitchTime / `pitchTime` | Entrée | value | 1 |
| pitchCurve / `pitchCurve` | Entrée | value | 1 |
| bodyLevel / `bodyLevel` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| hold / `hold` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |
| decayCurve / `decayCurve` | Entrée | value | 1 |
| length / `length` | Entrée | value | 1 |
| clickType / `clickType` | Entrée | value | 1 |
| clickLevel / `clickLevel` | Entrée | value | 1 |
| clickDecay / `clickDecay` | Entrée | value | 1 |
| clickTone / `clickTone` | Entrée | value | 1 |
| subLevel / `subLevel` | Entrée | value | 1 |
| subDecay / `subDecay` | Entrée | value | 1 |
| subOctave / `subOctave` | Entrée | value | 1 |
| driveType / `driveType` | Entrée | value | 1 |
| drive / `drive` | Entrée | value | 1 |
| driveTone / `driveTone` | Entrée | value | 1 |
| lowCut / `lowCut` | Entrée | value | 1 |
| highCut / `highCut` | Entrée | value | 1 |
| lowBoost / `lowBoost` | Entrée | value | 1 |
| keyTrack / `keyTrack` | Entrée | value | 1 |
| velocity / `velocity` | Entrée | value | 1 |
| output / `output` | Entrée | value | 1 |

## MOSAÏQUE (mosaique-synth) {#mosaique-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 12 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `a.type` | 0 | 0 | 3 |
| `a.octave` | -3 | 0 | 3 |
| `a.semi` | -12 | 0 | 12 |
| `a.fine` | -50 | 0 | 50 |
| `a.level` | 0 | 0.800000012 | 1 |
| `a.pan` | -1 | 0 | 1 |
| `a.send` | 0 | 0 | 1 |
| `a.wave` | 0 | 0.670000017 | 1 |
| `a.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `a.sync` | 0 | 0 | 24 |
| `a.table` | 0 | 1 | 7 |
| `a.position` | 0 | 0.400000006 | 1 |
| `a.mode` | 0 | 0 | 7 |
| `a.warp` | 0 | 0 | 1 |
| `a.partials` | 1 | 16 | 32 |
| `a.bright` | -1 | 0 | 1 |
| `a.parity` | -1 | 0 | 1 |
| `a.stretch` | 0 | 0 | 1 |
| `a.material` | 0 | 0 | 4 |
| `a.exciter` | 0 | 0 | 2 |
| `a.decay` | 0.0500000007 | 1.20000005 | 10 |
| `a.tone` | 0 | 0.5 | 1 |
| `a.strike` | 0 | 0.300000012 | 1 |
| `b.type` | 0 | 1 | 3 |
| `b.octave` | -3 | 0 | 3 |
| `b.semi` | -12 | 0 | 12 |
| `b.fine` | -50 | 6 | 50 |
| `b.level` | 0 | 0.449999988 | 1 |
| `b.pan` | -1 | 0 | 1 |
| `b.send` | 0 | 0 | 1 |
| `b.wave` | 0 | 0.670000017 | 1 |
| `b.pw` | 0.0500000007 | 0.5 | 0.949999988 |
| `b.sync` | 0 | 0 | 24 |
| `b.table` | 0 | 1 | 7 |
| `b.position` | 0 | 0.400000006 | 1 |
| `b.mode` | 0 | 0 | 7 |
| `b.warp` | 0 | 0 | 1 |
| `b.partials` | 1 | 16 | 32 |
| `b.bright` | -1 | 0 | 1 |
| `b.parity` | -1 | 0 | 1 |
| `b.stretch` | 0 | 0 | 1 |
| `b.material` | 0 | 0 | 4 |
| `b.exciter` | 0 | 0 | 2 |
| `b.decay` | 0.0500000007 | 1.20000005 | 10 |
| `b.tone` | 0 | 0.5 | 1 |
| `b.strike` | 0 | 0.300000012 | 1 |
| `noise.colour` | 0 | 0 | 3 |
| `noise.level` | 0 | 0 | 1 |
| `noise.send` | 0 | 0 | 1 |
| `unison.voices` | 1 | 1 | 4 |
| `unison.detune` | 0 | 12 | 50 |
| `unison.spread` | 0 | 0.800000012 | 1 |
| `f1.type` | 0 | 0 | 6 |
| `f1.cutoff` | 20 | 2400 | 20000 |
| `f1.resonance` | 0 | 0.200000003 | 1 |
| `f2.type` | 0 | 1 | 6 |
| `f2.cutoff` | 20 | 5000 | 20000 |
| `f2.resonance` | 0 | 0.100000001 | 1 |
| `filter.routing` | 0 | 1 | 1 |
| `filter.key` | 0 | 0.400000006 | 1 |
| `filter.velocity` | 0 | 0.200000003 | 1 |
| `drive.type` | 0 | 0 | 4 |
| `drive.amount` | 0 | 0 | 1 |
| `amp.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp.decay` | 0.00499999989 | 0.800000012 | 10 |
| `amp.sustain` | 0 | 0.800000012 | 1 |
| `amp.release` | 0.00499999989 | 0.400000006 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `env2.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `env2.decay` | 0.00499999989 | 0.600000024 | 10 |
| `env2.sustain` | 0 | 0.200000003 | 1 |
| `env2.release` | 0.00499999989 | 0.400000006 | 12 |
| `env3.attack` | 0.00100000005 | 0.600000024 | 10 |
| `env3.decay` | 0.00499999989 | 2 | 10 |
| `env3.sustain` | 0 | 0.5 | 1 |
| `env3.release` | 0.00499999989 | 1.20000005 | 12 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 3 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo2.shape` | 0 | 1 | 4 |
| `lfo2.rate` | 0.0500000007 | 0.300000012 | 30 |
| `lfo2.sync` | 0 | 0 | 12 |
| `fn.points` | 2 | 8 | 16 |
| `fn.sync` | 0 | 12 | 12 |
| `fn.rate` | 0.0500000007 | 0.5 | 20 |
| `fn.smooth` | 0 | 1 | 1 |
| `fn.loop` | 0 | 1 | 1 |
| `fn.point1` | 0 | 0 | 1 |
| `fn.point2` | 0 | 0.800000012 | 1 |
| `fn.point3` | 0 | 1 | 1 |
| `fn.point4` | 0 | 0.600000024 | 1 |
| `fn.point5` | 0 | 0.400000006 | 1 |
| `fn.point6` | 0 | 0.300000012 | 1 |
| `fn.point7` | 0 | 0.150000006 | 1 |
| `fn.point8` | 0 | 0 | 1 |
| `fn.point9` | 0 | 0 | 1 |
| `fn.point10` | 0 | 0.5 | 1 |
| `fn.point11` | 0 | 1 | 1 |
| `fn.point12` | 0 | 0.5 | 1 |
| `fn.point13` | 0 | 0 | 1 |
| `fn.point14` | 0 | 0.5 | 1 |
| `fn.point15` | 0 | 1 | 1 |
| `fn.point16` | 0 | 0.5 | 1 |
| `turing.sync` | 0 | 3 | 12 |
| `turing.rate` | 0.100000001 | 4 | 30 |
| `turing.change` | 0 | 0.200000003 | 1 |
| `turing.length` | 2 | 8 | 16 |
| `macro1` | 0 | 0 | 1 |
| `macro2` | 0 | 0 | 1 |
| `macro3` | 0 | 0 | 1 |
| `macro4` | 0 | 0 | 1 |
| `mod1.source` | 0 | 1 | 15 |
| `mod1.dest` | 0 | 12 | 18 |
| `mod1.amount` | -1 | 0.300000012 | 1 |
| `mod2.source` | 0 | 12 | 15 |
| `mod2.dest` | 0 | 4 | 18 |
| `mod2.amount` | -1 | 0.5 | 1 |
| `mod3.source` | 0 | 12 | 15 |
| `mod3.dest` | 0 | 6 | 18 |
| `mod3.amount` | -1 | 0.5 | 1 |
| `mod4.source` | 0 | 13 | 15 |
| `mod4.dest` | 0 | 12 | 18 |
| `mod4.amount` | -1 | 0.5 | 1 |
| `mod5.source` | 0 | 14 | 15 |
| `mod5.dest` | 0 | 18 | 18 |
| `mod5.amount` | -1 | 1 | 1 |
| `mod6.source` | 0 | 15 | 15 |
| `mod6.dest` | 0 | 14 | 18 |
| `mod6.amount` | -1 | 0.800000012 | 1 |
| `mod7.source` | 0 | 0 | 15 |
| `mod7.dest` | 0 | 0 | 18 |
| `mod7.amount` | -1 | 0 | 1 |
| `mod8.source` | 0 | 0 | 15 |
| `mod8.dest` | 0 | 0 | 18 |
| `mod8.amount` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| a.type / `a.type` | Entrée | value | 1 |
| a.octave / `a.octave` | Entrée | value | 1 |
| a.semi / `a.semi` | Entrée | value | 1 |
| a.fine / `a.fine` | Entrée | value | 1 |
| a.level / `a.level` | Entrée | value | 1 |
| a.pan / `a.pan` | Entrée | value | 1 |
| a.send / `a.send` | Entrée | value | 1 |
| a.wave / `a.wave` | Entrée | value | 1 |
| a.pw / `a.pw` | Entrée | value | 1 |
| a.sync / `a.sync` | Entrée | value | 1 |
| a.table / `a.table` | Entrée | value | 1 |
| a.position / `a.position` | Entrée | value | 1 |
| a.mode / `a.mode` | Entrée | value | 1 |
| a.warp / `a.warp` | Entrée | value | 1 |
| a.partials / `a.partials` | Entrée | value | 1 |
| a.bright / `a.bright` | Entrée | value | 1 |
| a.parity / `a.parity` | Entrée | value | 1 |
| a.stretch / `a.stretch` | Entrée | value | 1 |
| a.material / `a.material` | Entrée | value | 1 |
| a.exciter / `a.exciter` | Entrée | value | 1 |
| a.decay / `a.decay` | Entrée | value | 1 |
| a.tone / `a.tone` | Entrée | value | 1 |
| a.strike / `a.strike` | Entrée | value | 1 |
| b.type / `b.type` | Entrée | value | 1 |
| b.octave / `b.octave` | Entrée | value | 1 |
| b.semi / `b.semi` | Entrée | value | 1 |
| b.fine / `b.fine` | Entrée | value | 1 |
| b.level / `b.level` | Entrée | value | 1 |
| b.pan / `b.pan` | Entrée | value | 1 |
| b.send / `b.send` | Entrée | value | 1 |
| b.wave / `b.wave` | Entrée | value | 1 |
| b.pw / `b.pw` | Entrée | value | 1 |
| b.sync / `b.sync` | Entrée | value | 1 |
| b.table / `b.table` | Entrée | value | 1 |
| b.position / `b.position` | Entrée | value | 1 |
| b.mode / `b.mode` | Entrée | value | 1 |
| b.warp / `b.warp` | Entrée | value | 1 |
| b.partials / `b.partials` | Entrée | value | 1 |
| b.bright / `b.bright` | Entrée | value | 1 |
| b.parity / `b.parity` | Entrée | value | 1 |
| b.stretch / `b.stretch` | Entrée | value | 1 |
| b.material / `b.material` | Entrée | value | 1 |
| b.exciter / `b.exciter` | Entrée | value | 1 |
| b.decay / `b.decay` | Entrée | value | 1 |
| b.tone / `b.tone` | Entrée | value | 1 |
| b.strike / `b.strike` | Entrée | value | 1 |
| noise.colour / `noise.colour` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| noise.send / `noise.send` | Entrée | value | 1 |
| unison.voices / `unison.voices` | Entrée | value | 1 |
| unison.detune / `unison.detune` | Entrée | value | 1 |
| unison.spread / `unison.spread` | Entrée | value | 1 |
| f1.type / `f1.type` | Entrée | value | 1 |
| f1.cutoff / `f1.cutoff` | Entrée | value | 1 |
| f1.resonance / `f1.resonance` | Entrée | value | 1 |
| f2.type / `f2.type` | Entrée | value | 1 |
| f2.cutoff / `f2.cutoff` | Entrée | value | 1 |
| f2.resonance / `f2.resonance` | Entrée | value | 1 |
| filter.routing / `filter.routing` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| drive.type / `drive.type` | Entrée | value | 1 |
| drive.amount / `drive.amount` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| env2.attack / `env2.attack` | Entrée | value | 1 |
| env2.decay / `env2.decay` | Entrée | value | 1 |
| env2.sustain / `env2.sustain` | Entrée | value | 1 |
| env2.release / `env2.release` | Entrée | value | 1 |
| env3.attack / `env3.attack` | Entrée | value | 1 |
| env3.decay / `env3.decay` | Entrée | value | 1 |
| env3.sustain / `env3.sustain` | Entrée | value | 1 |
| env3.release / `env3.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| fn.points / `fn.points` | Entrée | value | 1 |
| fn.sync / `fn.sync` | Entrée | value | 1 |
| fn.rate / `fn.rate` | Entrée | value | 1 |
| fn.smooth / `fn.smooth` | Entrée | value | 1 |
| fn.loop / `fn.loop` | Entrée | value | 1 |
| fn.point1 / `fn.point1` | Entrée | value | 1 |
| fn.point2 / `fn.point2` | Entrée | value | 1 |
| fn.point3 / `fn.point3` | Entrée | value | 1 |
| fn.point4 / `fn.point4` | Entrée | value | 1 |
| fn.point5 / `fn.point5` | Entrée | value | 1 |
| fn.point6 / `fn.point6` | Entrée | value | 1 |
| fn.point7 / `fn.point7` | Entrée | value | 1 |
| fn.point8 / `fn.point8` | Entrée | value | 1 |
| fn.point9 / `fn.point9` | Entrée | value | 1 |
| fn.point10 / `fn.point10` | Entrée | value | 1 |
| fn.point11 / `fn.point11` | Entrée | value | 1 |
| fn.point12 / `fn.point12` | Entrée | value | 1 |
| fn.point13 / `fn.point13` | Entrée | value | 1 |
| fn.point14 / `fn.point14` | Entrée | value | 1 |
| fn.point15 / `fn.point15` | Entrée | value | 1 |
| fn.point16 / `fn.point16` | Entrée | value | 1 |
| turing.sync / `turing.sync` | Entrée | value | 1 |
| turing.rate / `turing.rate` | Entrée | value | 1 |
| turing.change / `turing.change` | Entrée | value | 1 |
| turing.length / `turing.length` | Entrée | value | 1 |
| macro1 / `macro1` | Entrée | value | 1 |
| macro2 / `macro2` | Entrée | value | 1 |
| macro3 / `macro3` | Entrée | value | 1 |
| macro4 / `macro4` | Entrée | value | 1 |
| mod1.source / `mod1.source` | Entrée | value | 1 |
| mod1.dest / `mod1.dest` | Entrée | value | 1 |
| mod1.amount / `mod1.amount` | Entrée | value | 1 |
| mod2.source / `mod2.source` | Entrée | value | 1 |
| mod2.dest / `mod2.dest` | Entrée | value | 1 |
| mod2.amount / `mod2.amount` | Entrée | value | 1 |
| mod3.source / `mod3.source` | Entrée | value | 1 |
| mod3.dest / `mod3.dest` | Entrée | value | 1 |
| mod3.amount / `mod3.amount` | Entrée | value | 1 |
| mod4.source / `mod4.source` | Entrée | value | 1 |
| mod4.dest / `mod4.dest` | Entrée | value | 1 |
| mod4.amount / `mod4.amount` | Entrée | value | 1 |
| mod5.source / `mod5.source` | Entrée | value | 1 |
| mod5.dest / `mod5.dest` | Entrée | value | 1 |
| mod5.amount / `mod5.amount` | Entrée | value | 1 |
| mod6.source / `mod6.source` | Entrée | value | 1 |
| mod6.dest / `mod6.dest` | Entrée | value | 1 |
| mod6.amount / `mod6.amount` | Entrée | value | 1 |
| mod7.source / `mod7.source` | Entrée | value | 1 |
| mod7.dest / `mod7.dest` | Entrée | value | 1 |
| mod7.amount / `mod7.amount` | Entrée | value | 1 |
| mod8.source / `mod8.source` | Entrée | value | 1 |
| mod8.dest / `mod8.dest` | Entrée | value | 1 |
| mod8.amount / `mod8.amount` | Entrée | value | 1 |

## NÉBULEUSE (nebuleuse-synth) {#nebuleuse-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 12 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `a.source` | 0 | 1 | 1 |
| `a.level` | 0 | 0.800000012 | 1 |
| `a.pan` | -1 | 0 | 1 |
| `a.octave` | -3 | 0 | 3 |
| `a.semi` | -12 | 0 | 12 |
| `a.fine` | -50 | 0 | 50 |
| `a.table` | 0 | 0 | 7 |
| `a.position` | 0 | 0.300000012 | 1 |
| `a.texture` | 0 | 0 | 6 |
| `a.scan` | 0 | 0.300000012 | 1 |
| `a.travel` | -1 | 0.100000001 | 1 |
| `a.grain` | 0.00999999978 | 0.119999997 | 0.5 |
| `a.density` | 2 | 18 | 80 |
| `a.spray` | 0 | 0.200000003 | 1 |
| `a.scatter` | 0 | 0.0500000007 | 1 |
| `a.reverse` | 0 | 0 | 1 |
| `a.harmonia` | 0 | 0 | 10 |
| `a.harmoniaLevel` | 0 | 0.5 | 1 |
| `a.timbre` | -1 | 0 | 1 |
| `a.cutoff` | 20 | 12000 | 20000 |
| `a.resonance` | 0 | 0.100000001 | 1 |
| `a.drift` | 0 | 0.200000003 | 1 |
| `b.source` | 0 | 0 | 1 |
| `b.level` | 0 | 0.349999994 | 1 |
| `b.pan` | -1 | 0 | 1 |
| `b.octave` | -3 | 0 | 3 |
| `b.semi` | -12 | 0 | 12 |
| `b.fine` | -50 | 5 | 50 |
| `b.table` | 0 | 0 | 7 |
| `b.position` | 0 | 0.300000012 | 1 |
| `b.texture` | 0 | 6 | 6 |
| `b.scan` | 0 | 0.300000012 | 1 |
| `b.travel` | -1 | 0.100000001 | 1 |
| `b.grain` | 0.00999999978 | 0.119999997 | 0.5 |
| `b.density` | 2 | 18 | 80 |
| `b.spray` | 0 | 0.200000003 | 1 |
| `b.scatter` | 0 | 0.0500000007 | 1 |
| `b.reverse` | 0 | 0 | 1 |
| `b.harmonia` | 0 | 0 | 10 |
| `b.harmoniaLevel` | 0 | 0.5 | 1 |
| `b.timbre` | -1 | 0 | 1 |
| `b.cutoff` | 20 | 12000 | 20000 |
| `b.resonance` | 0 | 0.100000001 | 1 |
| `b.drift` | 0 | 0.200000003 | 1 |
| `f.type` | 0 | 1 | 6 |
| `f.cutoff` | 20 | 8000 | 20000 |
| `f.resonance` | 0 | 0.100000001 | 1 |
| `filter.key` | 0 | 0.300000012 | 1 |
| `filter.velocity` | 0 | 0.200000003 | 1 |
| `amp.attack` | 0.00100000005 | 0.25 | 10 |
| `amp.decay` | 0.00499999989 | 1.5 | 10 |
| `amp.sustain` | 0 | 0.850000024 | 1 |
| `amp.release` | 0.00499999989 | 1.79999995 | 12 |
| `amp.velocity` | 0 | 0.400000006 | 1 |
| `env2.attack` | 0.00100000005 | 1 | 10 |
| `env2.decay` | 0.00499999989 | 3 | 10 |
| `env2.sustain` | 0 | 0.5 | 1 |
| `env2.release` | 0.00499999989 | 2 | 12 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0199999996 | 0.300000012 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo2.shape` | 0 | 1 | 4 |
| `lfo2.rate` | 0.0199999996 | 0.0799999982 | 20 |
| `lfo2.sync` | 0 | 0 | 12 |
| `orb.x` | -1 | 0 | 1 |
| `orb.y` | -1 | 0 | 1 |
| `orb.motion` | 0 | 0.300000012 | 1 |
| `orb.rate` | 0.00999999978 | 0.0700000003 | 4 |
| `mod1.source` | 0 | 4 | 11 |
| `mod1.dest` | 0 | 4 | 18 |
| `mod1.amount` | -1 | 0.300000012 | 1 |
| `mod2.source` | 0 | 5 | 11 |
| `mod2.dest` | 0 | 14 | 18 |
| `mod2.amount` | -1 | 0.300000012 | 1 |
| `mod3.source` | 0 | 4 | 11 |
| `mod3.dest` | 0 | 13 | 18 |
| `mod3.amount` | -1 | 0.5 | 1 |
| `mod4.source` | 0 | 5 | 11 |
| `mod4.dest` | 0 | 18 | 18 |
| `mod4.amount` | -1 | 0.400000006 | 1 |
| `mod5.source` | 0 | 2 | 11 |
| `mod5.dest` | 0 | 8 | 18 |
| `mod5.amount` | -1 | 0.150000006 | 1 |
| `mod6.source` | 0 | 0 | 11 |
| `mod6.dest` | 0 | 0 | 18 |
| `mod6.amount` | -1 | 0 | 1 |
| `mod7.source` | 0 | 0 | 11 |
| `mod7.dest` | 0 | 0 | 18 |
| `mod7.amount` | -1 | 0 | 1 |
| `mod8.source` | 0 | 0 | 11 |
| `mod8.dest` | 0 | 0 | 18 |
| `mod8.amount` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| a.source / `a.source` | Entrée | value | 1 |
| a.level / `a.level` | Entrée | value | 1 |
| a.pan / `a.pan` | Entrée | value | 1 |
| a.octave / `a.octave` | Entrée | value | 1 |
| a.semi / `a.semi` | Entrée | value | 1 |
| a.fine / `a.fine` | Entrée | value | 1 |
| a.table / `a.table` | Entrée | value | 1 |
| a.position / `a.position` | Entrée | value | 1 |
| a.texture / `a.texture` | Entrée | value | 1 |
| a.scan / `a.scan` | Entrée | value | 1 |
| a.travel / `a.travel` | Entrée | value | 1 |
| a.grain / `a.grain` | Entrée | value | 1 |
| a.density / `a.density` | Entrée | value | 1 |
| a.spray / `a.spray` | Entrée | value | 1 |
| a.scatter / `a.scatter` | Entrée | value | 1 |
| a.reverse / `a.reverse` | Entrée | value | 1 |
| a.harmonia / `a.harmonia` | Entrée | value | 1 |
| a.harmoniaLevel / `a.harmoniaLevel` | Entrée | value | 1 |
| a.timbre / `a.timbre` | Entrée | value | 1 |
| a.cutoff / `a.cutoff` | Entrée | value | 1 |
| a.resonance / `a.resonance` | Entrée | value | 1 |
| a.drift / `a.drift` | Entrée | value | 1 |
| b.source / `b.source` | Entrée | value | 1 |
| b.level / `b.level` | Entrée | value | 1 |
| b.pan / `b.pan` | Entrée | value | 1 |
| b.octave / `b.octave` | Entrée | value | 1 |
| b.semi / `b.semi` | Entrée | value | 1 |
| b.fine / `b.fine` | Entrée | value | 1 |
| b.table / `b.table` | Entrée | value | 1 |
| b.position / `b.position` | Entrée | value | 1 |
| b.texture / `b.texture` | Entrée | value | 1 |
| b.scan / `b.scan` | Entrée | value | 1 |
| b.travel / `b.travel` | Entrée | value | 1 |
| b.grain / `b.grain` | Entrée | value | 1 |
| b.density / `b.density` | Entrée | value | 1 |
| b.spray / `b.spray` | Entrée | value | 1 |
| b.scatter / `b.scatter` | Entrée | value | 1 |
| b.reverse / `b.reverse` | Entrée | value | 1 |
| b.harmonia / `b.harmonia` | Entrée | value | 1 |
| b.harmoniaLevel / `b.harmoniaLevel` | Entrée | value | 1 |
| b.timbre / `b.timbre` | Entrée | value | 1 |
| b.cutoff / `b.cutoff` | Entrée | value | 1 |
| b.resonance / `b.resonance` | Entrée | value | 1 |
| b.drift / `b.drift` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| env2.attack / `env2.attack` | Entrée | value | 1 |
| env2.decay / `env2.decay` | Entrée | value | 1 |
| env2.sustain / `env2.sustain` | Entrée | value | 1 |
| env2.release / `env2.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| orb.x / `orb.x` | Entrée | value | 1 |
| orb.y / `orb.y` | Entrée | value | 1 |
| orb.motion / `orb.motion` | Entrée | value | 1 |
| orb.rate / `orb.rate` | Entrée | value | 1 |
| mod1.source / `mod1.source` | Entrée | value | 1 |
| mod1.dest / `mod1.dest` | Entrée | value | 1 |
| mod1.amount / `mod1.amount` | Entrée | value | 1 |
| mod2.source / `mod2.source` | Entrée | value | 1 |
| mod2.dest / `mod2.dest` | Entrée | value | 1 |
| mod2.amount / `mod2.amount` | Entrée | value | 1 |
| mod3.source / `mod3.source` | Entrée | value | 1 |
| mod3.dest / `mod3.dest` | Entrée | value | 1 |
| mod3.amount / `mod3.amount` | Entrée | value | 1 |
| mod4.source / `mod4.source` | Entrée | value | 1 |
| mod4.dest / `mod4.dest` | Entrée | value | 1 |
| mod4.amount / `mod4.amount` | Entrée | value | 1 |
| mod5.source / `mod5.source` | Entrée | value | 1 |
| mod5.dest / `mod5.dest` | Entrée | value | 1 |
| mod5.amount / `mod5.amount` | Entrée | value | 1 |
| mod6.source / `mod6.source` | Entrée | value | 1 |
| mod6.dest / `mod6.dest` | Entrée | value | 1 |
| mod6.amount / `mod6.amount` | Entrée | value | 1 |
| mod7.source / `mod7.source` | Entrée | value | 1 |
| mod7.dest / `mod7.dest` | Entrée | value | 1 |
| mod7.amount / `mod7.amount` | Entrée | value | 1 |
| mod8.source / `mod8.source` | Entrée | value | 1 |
| mod8.dest / `mod8.dest` | Entrée | value | 1 |
| mod8.amount / `mod8.amount` | Entrée | value | 1 |

## NOISE (noise) {#noise}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `colour` | 0 | 0 | 2 |
| `level` | 0 | 0.25 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio / `audio` | Sortie | audio | 2 |
| level / `level` | Entrée | value | 1 |

## ONYX (onyx-synth) {#onyx-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.600000024 | 1 |
| `osc1.mode` | 0 | 0 | 7 |
| `osc1.amount` | 0 | 0.25 | 1 |
| `osc1.level` | 0 | 0.699999988 | 1 |
| `osc1.ratio` | 1 | 1 | 16 |
| `osc1.fine` | -50 | 0 | 50 |
| `osc1.fixed` | 0 | 0 | 1 |
| `osc1.hz` | 1 | 220 | 10000 |
| `osc1.phase` | 0 | 0 | 360 |
| `osc1.env.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `osc1.env.decay` | 0.00499999989 | 0.899999976 | 10 |
| `osc1.env.sustain` | 0 | 0.300000012 | 1 |
| `osc1.env.release` | 0.00499999989 | 0.5 | 12 |
| `osc1.env.depth` | -1 | 0.349999994 | 1 |
| `osc1.env.level` | 0 | 0 | 1 |
| `osc2.mode` | 0 | 0 | 7 |
| `osc2.amount` | 0 | 0.200000003 | 1 |
| `osc2.level` | 0 | 0.449999988 | 1 |
| `osc2.ratio` | 1 | 1 | 16 |
| `osc2.fine` | -50 | 7 | 50 |
| `osc2.fixed` | 0 | 0 | 1 |
| `osc2.hz` | 1 | 220 | 10000 |
| `osc2.phase` | 0 | 0 | 360 |
| `osc2.env.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `osc2.env.decay` | 0.00499999989 | 0.899999976 | 10 |
| `osc2.env.sustain` | 0 | 0.300000012 | 1 |
| `osc2.env.release` | 0.00499999989 | 0.5 | 12 |
| `osc2.env.depth` | -1 | 0.25 | 1 |
| `osc2.env.level` | 0 | 0 | 1 |
| `osc3.mode` | 0 | 5 | 7 |
| `osc3.amount` | 0 | 0.400000006 | 1 |
| `osc3.level` | 0 | 0 | 1 |
| `osc3.ratio` | 1 | 2 | 16 |
| `osc3.fine` | -50 | 0 | 50 |
| `osc3.fixed` | 0 | 0 | 1 |
| `osc3.hz` | 1 | 220 | 10000 |
| `osc3.phase` | 0 | 0 | 360 |
| `osc3.env.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `osc3.env.decay` | 0.00499999989 | 0.899999976 | 10 |
| `osc3.env.sustain` | 0 | 0.300000012 | 1 |
| `osc3.env.release` | 0.00499999989 | 0.5 | 12 |
| `osc3.env.depth` | -1 | 0 | 1 |
| `osc3.env.level` | 0 | 0 | 1 |
| `osc4.mode` | 0 | 3 | 7 |
| `osc4.amount` | 0 | 0.300000012 | 1 |
| `osc4.level` | 0 | 0 | 1 |
| `osc4.ratio` | 1 | 1 | 16 |
| `osc4.fine` | -50 | -7 | 50 |
| `osc4.fixed` | 0 | 0 | 1 |
| `osc4.hz` | 1 | 220 | 10000 |
| `osc4.phase` | 0 | 0 | 360 |
| `osc4.env.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `osc4.env.decay` | 0.00499999989 | 0.899999976 | 10 |
| `osc4.env.sustain` | 0 | 0.300000012 | 1 |
| `osc4.env.release` | 0.00499999989 | 0.5 | 12 |
| `osc4.env.depth` | -1 | 0 | 1 |
| `osc4.env.level` | 0 | 0 | 1 |
| `octave` | -3 | 0 | 3 |
| `phase.random` | 0 | 0 | 1 |
| `unison.voices` | 1 | 1 | 4 |
| `unison.detune` | 0 | 12 | 50 |
| `unison.spread` | 0 | 0.699999988 | 1 |
| `pm.11` | 0 | 0 | 1 |
| `pm.12` | 0 | 0 | 1 |
| `pm.13` | 0 | 0 | 1 |
| `pm.14` | 0 | 0 | 1 |
| `pm.21` | 0 | 0 | 1 |
| `pm.22` | 0 | 0 | 1 |
| `pm.23` | 0 | 0 | 1 |
| `pm.24` | 0 | 0 | 1 |
| `pm.31` | 0 | 0 | 1 |
| `pm.32` | 0 | 0 | 1 |
| `pm.33` | 0 | 0 | 1 |
| `pm.34` | 0 | 0 | 1 |
| `pm.41` | 0 | 0 | 1 |
| `pm.42` | 0 | 0 | 1 |
| `pm.43` | 0 | 0 | 1 |
| `pm.44` | 0 | 0 | 1 |
| `f.type` | 0 | 1 | 6 |
| `f.cutoff` | 20 | 7000 | 20000 |
| `f.resonance` | 0 | 0.100000001 | 1 |
| `filter.env` | -1 | 0.100000001 | 1 |
| `filter.key` | 0 | 0.300000012 | 1 |
| `filter.drive` | 0 | 0 | 1 |
| `amp.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp.decay` | 0.00499999989 | 0.800000012 | 10 |
| `amp.sustain` | 0 | 0.800000012 | 1 |
| `amp.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `distortion.velocity` | 0 | 0.300000012 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `fenv.decay` | 0.00499999989 | 0.600000024 | 10 |
| `fenv.sustain` | 0 | 0.300000012 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 12 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0.300000012 | 3 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.distortion` | 0 | 0 | 1 |
| `lfo1.cutoff` | 0 | 0 | 1 |
| `mod.wheel` | 0 | 0.400000006 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| osc1.mode / `osc1.mode` | Entrée | value | 1 |
| osc1.amount / `osc1.amount` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc1.ratio / `osc1.ratio` | Entrée | value | 1 |
| osc1.fine / `osc1.fine` | Entrée | value | 1 |
| osc1.fixed / `osc1.fixed` | Entrée | value | 1 |
| osc1.hz / `osc1.hz` | Entrée | value | 1 |
| osc1.phase / `osc1.phase` | Entrée | value | 1 |
| osc1.env.attack / `osc1.env.attack` | Entrée | value | 1 |
| osc1.env.decay / `osc1.env.decay` | Entrée | value | 1 |
| osc1.env.sustain / `osc1.env.sustain` | Entrée | value | 1 |
| osc1.env.release / `osc1.env.release` | Entrée | value | 1 |
| osc1.env.depth / `osc1.env.depth` | Entrée | value | 1 |
| osc1.env.level / `osc1.env.level` | Entrée | value | 1 |
| osc2.mode / `osc2.mode` | Entrée | value | 1 |
| osc2.amount / `osc2.amount` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| osc2.ratio / `osc2.ratio` | Entrée | value | 1 |
| osc2.fine / `osc2.fine` | Entrée | value | 1 |
| osc2.fixed / `osc2.fixed` | Entrée | value | 1 |
| osc2.hz / `osc2.hz` | Entrée | value | 1 |
| osc2.phase / `osc2.phase` | Entrée | value | 1 |
| osc2.env.attack / `osc2.env.attack` | Entrée | value | 1 |
| osc2.env.decay / `osc2.env.decay` | Entrée | value | 1 |
| osc2.env.sustain / `osc2.env.sustain` | Entrée | value | 1 |
| osc2.env.release / `osc2.env.release` | Entrée | value | 1 |
| osc2.env.depth / `osc2.env.depth` | Entrée | value | 1 |
| osc2.env.level / `osc2.env.level` | Entrée | value | 1 |
| osc3.mode / `osc3.mode` | Entrée | value | 1 |
| osc3.amount / `osc3.amount` | Entrée | value | 1 |
| osc3.level / `osc3.level` | Entrée | value | 1 |
| osc3.ratio / `osc3.ratio` | Entrée | value | 1 |
| osc3.fine / `osc3.fine` | Entrée | value | 1 |
| osc3.fixed / `osc3.fixed` | Entrée | value | 1 |
| osc3.hz / `osc3.hz` | Entrée | value | 1 |
| osc3.phase / `osc3.phase` | Entrée | value | 1 |
| osc3.env.attack / `osc3.env.attack` | Entrée | value | 1 |
| osc3.env.decay / `osc3.env.decay` | Entrée | value | 1 |
| osc3.env.sustain / `osc3.env.sustain` | Entrée | value | 1 |
| osc3.env.release / `osc3.env.release` | Entrée | value | 1 |
| osc3.env.depth / `osc3.env.depth` | Entrée | value | 1 |
| osc3.env.level / `osc3.env.level` | Entrée | value | 1 |
| osc4.mode / `osc4.mode` | Entrée | value | 1 |
| osc4.amount / `osc4.amount` | Entrée | value | 1 |
| osc4.level / `osc4.level` | Entrée | value | 1 |
| osc4.ratio / `osc4.ratio` | Entrée | value | 1 |
| osc4.fine / `osc4.fine` | Entrée | value | 1 |
| osc4.fixed / `osc4.fixed` | Entrée | value | 1 |
| osc4.hz / `osc4.hz` | Entrée | value | 1 |
| osc4.phase / `osc4.phase` | Entrée | value | 1 |
| osc4.env.attack / `osc4.env.attack` | Entrée | value | 1 |
| osc4.env.decay / `osc4.env.decay` | Entrée | value | 1 |
| osc4.env.sustain / `osc4.env.sustain` | Entrée | value | 1 |
| osc4.env.release / `osc4.env.release` | Entrée | value | 1 |
| osc4.env.depth / `osc4.env.depth` | Entrée | value | 1 |
| osc4.env.level / `osc4.env.level` | Entrée | value | 1 |
| octave / `octave` | Entrée | value | 1 |
| phase.random / `phase.random` | Entrée | value | 1 |
| unison.voices / `unison.voices` | Entrée | value | 1 |
| unison.detune / `unison.detune` | Entrée | value | 1 |
| unison.spread / `unison.spread` | Entrée | value | 1 |
| pm.11 / `pm.11` | Entrée | value | 1 |
| pm.12 / `pm.12` | Entrée | value | 1 |
| pm.13 / `pm.13` | Entrée | value | 1 |
| pm.14 / `pm.14` | Entrée | value | 1 |
| pm.21 / `pm.21` | Entrée | value | 1 |
| pm.22 / `pm.22` | Entrée | value | 1 |
| pm.23 / `pm.23` | Entrée | value | 1 |
| pm.24 / `pm.24` | Entrée | value | 1 |
| pm.31 / `pm.31` | Entrée | value | 1 |
| pm.32 / `pm.32` | Entrée | value | 1 |
| pm.33 / `pm.33` | Entrée | value | 1 |
| pm.34 / `pm.34` | Entrée | value | 1 |
| pm.41 / `pm.41` | Entrée | value | 1 |
| pm.42 / `pm.42` | Entrée | value | 1 |
| pm.43 / `pm.43` | Entrée | value | 1 |
| pm.44 / `pm.44` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| distortion.velocity / `distortion.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.distortion / `lfo1.distortion` | Entrée | value | 1 |
| lfo1.cutoff / `lfo1.cutoff` | Entrée | value | 1 |
| mod.wheel / `mod.wheel` | Entrée | value | 1 |

## OPALE (opale-synth) {#opale-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `osc1.octave` | 0 | 2 | 4 |
| `osc1.pitch` | -24 | 0 | 24 |
| `osc1.fine` | -50 | 0 | 50 |
| `osc1.shape` | 0 | 0.5 | 1 |
| `osc1.level` | 0 | 1 | 1 |
| `osc1.sub` | 0 | 0 | 1 |
| `osc1.subshape` | 0 | 0 | 1 |
| `osc1.hardsync` | 0 | 0 | 36 |
| `osc1.unison` | 1 | 1 | 7 |
| `osc1.detune` | 0 | 20 | 100 |
| `osc1.spread` | 0 | 0.699999988 | 1 |
| `osc1.retrigger` | 0 | 0 | 1 |
| `osc2.octave` | 0 | 2 | 4 |
| `osc2.pitch` | -24 | 0 | 24 |
| `osc2.fine` | -50 | 7 | 50 |
| `osc2.shape` | 0 | 0.5 | 1 |
| `osc2.level` | 0 | 1 | 1 |
| `osc2.sub` | 0 | 0 | 1 |
| `osc2.subshape` | 0 | 0 | 1 |
| `osc2.hardsync` | 0 | 0 | 36 |
| `osc2.unison` | 1 | 1 | 7 |
| `osc2.detune` | 0 | 20 | 100 |
| `osc2.spread` | 0 | 0.699999988 | 1 |
| `osc2.retrigger` | 0 | 0 | 1 |
| `mix.mode` | 0 | 0 | 5 |
| `mix.balance` | 0 | 0.5 | 1 |
| `noise.level` | 0 | 0 | 1 |
| `noise.colour` | -1 | 0 | 1 |
| `mix.drive` | 0 | 0 | 1 |
| `mix.feedback` | 0 | 0 | 1 |
| `drift` | 0 | 0.200000003 | 1 |
| `pitchenv.amount` | -24 | 0 | 24 |
| `pitchenv.target` | 0 | 0 | 2 |
| `f.type` | 0 | 0 | 6 |
| `f.cutoff` | 20 | 2200 | 20000 |
| `f.resonance` | 0 | 0.200000003 | 1 |
| `filter.env` | -1 | 0.300000012 | 1 |
| `filter.key` | 0 | 0.5 | 1 |
| `filter.velocity` | 0 | 0.300000012 | 1 |
| `filter.drive` | 0 | 0 | 1 |
| `amp.attack` | 0.00100000005 | 0.00400000019 | 10 |
| `amp.decay` | 0.00499999989 | 0.600000024 | 10 |
| `amp.sustain` | 0 | 0.800000012 | 1 |
| `amp.release` | 0.00499999989 | 0.349999994 | 12 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `fenv.decay` | 0.00499999989 | 0.600000024 | 10 |
| `fenv.sustain` | 0 | 0.300000012 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 12 |
| `lfo1.shape` | 0 | 1 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0 | 3 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.morph` | 0 | 0 | 1 |
| `lfo1.balance` | 0 | 0 | 1 |
| `lfo1.cutoff` | 0 | 0 | 1 |
| `lfo1.amp` | 0 | 0 | 1 |
| `mod.vibrato` | 0 | 0.400000006 | 1 |
| `mod.cutoff` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| osc1.octave / `osc1.octave` | Entrée | value | 1 |
| osc1.pitch / `osc1.pitch` | Entrée | value | 1 |
| osc1.fine / `osc1.fine` | Entrée | value | 1 |
| osc1.shape / `osc1.shape` | Entrée | value | 1 |
| osc1.level / `osc1.level` | Entrée | value | 1 |
| osc1.sub / `osc1.sub` | Entrée | value | 1 |
| osc1.subshape / `osc1.subshape` | Entrée | value | 1 |
| osc1.hardsync / `osc1.hardsync` | Entrée | value | 1 |
| osc1.unison / `osc1.unison` | Entrée | value | 1 |
| osc1.detune / `osc1.detune` | Entrée | value | 1 |
| osc1.spread / `osc1.spread` | Entrée | value | 1 |
| osc1.retrigger / `osc1.retrigger` | Entrée | value | 1 |
| osc2.octave / `osc2.octave` | Entrée | value | 1 |
| osc2.pitch / `osc2.pitch` | Entrée | value | 1 |
| osc2.fine / `osc2.fine` | Entrée | value | 1 |
| osc2.shape / `osc2.shape` | Entrée | value | 1 |
| osc2.level / `osc2.level` | Entrée | value | 1 |
| osc2.sub / `osc2.sub` | Entrée | value | 1 |
| osc2.subshape / `osc2.subshape` | Entrée | value | 1 |
| osc2.hardsync / `osc2.hardsync` | Entrée | value | 1 |
| osc2.unison / `osc2.unison` | Entrée | value | 1 |
| osc2.detune / `osc2.detune` | Entrée | value | 1 |
| osc2.spread / `osc2.spread` | Entrée | value | 1 |
| osc2.retrigger / `osc2.retrigger` | Entrée | value | 1 |
| mix.mode / `mix.mode` | Entrée | value | 1 |
| mix.balance / `mix.balance` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| noise.colour / `noise.colour` | Entrée | value | 1 |
| mix.drive / `mix.drive` | Entrée | value | 1 |
| mix.feedback / `mix.feedback` | Entrée | value | 1 |
| drift / `drift` | Entrée | value | 1 |
| pitchenv.amount / `pitchenv.amount` | Entrée | value | 1 |
| pitchenv.target / `pitchenv.target` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.morph / `lfo1.morph` | Entrée | value | 1 |
| lfo1.balance / `lfo1.balance` | Entrée | value | 1 |
| lfo1.cutoff / `lfo1.cutoff` | Entrée | value | 1 |
| lfo1.amp / `lfo1.amp` | Entrée | value | 1 |
| mod.vibrato / `mod.vibrato` | Entrée | value | 1 |
| mod.cutoff / `mod.cutoff` | Entrée | value | 1 |

## OSCILLATOR (oscillator) {#oscillator}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 20000 |
| `waveform` | 0 | 0 | 4 |
| `level` | 0 | 0.8 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio / `audio` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |

## POLY OSCILLATOR (poly-oscillator) {#poly-oscillator}

Catégorie : DSP / Instruments.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `waveform` | 0 | 0 | 4 |
| `detune` | -48 | 0 | 48 |
| `level` | 0 | 0.8 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| pitch / `pitch` | Entrée | value | 1 |
| reset / `reset` | Entrée | trigger | 1 |
| audio / `audio` | Sortie | audio | 2 |

## LEGACY POLY SYNTH (poly-synth) {#poly-synth}

Catégorie : DSP / Instruments.

Aucun paramètre numérique déclaré par ce schéma.

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| Input / `input` | Entrée | midi | 1 |
| Output / `output` | Sortie | audio | 2 |

## PRISME (prism-synth) {#prism-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `a.on` | 0 | 1 | 1 |
| `a.table` | 0 | 0 | 11 |
| `a.position` | 0 | 0 | 1 |
| `a.warp` | 0 | 0 | 13 |
| `a.warpAmount` | 0 | 0 | 1 |
| `a.unison` | 1 | 1 | 16 |
| `a.detune` | 0 | 0.25 | 1 |
| `a.blend` | 0 | 0.75 | 1 |
| `a.width` | 0 | 1 | 1 |
| `a.octave` | -4 | 0 | 4 |
| `a.semi` | -12 | 0 | 12 |
| `a.fine` | -100 | 0 | 100 |
| `a.coarse` | -36 | 0 | 36 |
| `a.phase` | 0 | 0 | 1 |
| `a.random` | 0 | 1 | 1 |
| `a.level` | 0 | 0.75 | 1 |
| `a.pan` | -1 | 0 | 1 |
| `a.toFilter` | 0 | 1 | 1 |
| `b.on` | 0 | 0 | 1 |
| `b.table` | 0 | 2 | 11 |
| `b.position` | 0 | 0 | 1 |
| `b.warp` | 0 | 0 | 13 |
| `b.warpAmount` | 0 | 0 | 1 |
| `b.unison` | 1 | 1 | 16 |
| `b.detune` | 0 | 0.25 | 1 |
| `b.blend` | 0 | 0.75 | 1 |
| `b.width` | 0 | 1 | 1 |
| `b.octave` | -4 | 0 | 4 |
| `b.semi` | -12 | 0 | 12 |
| `b.fine` | -100 | 0 | 100 |
| `b.coarse` | -36 | 0 | 36 |
| `b.phase` | 0 | 0 | 1 |
| `b.random` | 0 | 1 | 1 |
| `b.level` | 0 | 0.75 | 1 |
| `b.pan` | -1 | 0 | 1 |
| `b.toFilter` | 0 | 1 | 1 |
| `sub.on` | 0 | 0 | 1 |
| `sub.shape` | 0 | 0 | 5 |
| `sub.octave` | -3 | -1 | 2 |
| `sub.level` | 0 | 0.75 | 1 |
| `sub.pan` | -1 | 0 | 1 |
| `sub.direct` | 0 | 0 | 1 |
| `noise.on` | 0 | 0 | 1 |
| `noise.type` | 0 | 0 | 5 |
| `noise.tone` | 0 | 0.5 | 1 |
| `noise.pitch` | 0 | 0.5 | 1 |
| `noise.level` | 0 | 0.400000006 | 1 |
| `noise.pan` | -1 | 0 | 1 |
| `noise.toFilter` | 0 | 1 | 1 |
| `filter.on` | 0 | 0 | 1 |
| `filter.type` | 0 | 1 | 11 |
| `filter.cutoff` | 20 | 1200 | 20000 |
| `filter.resonance` | 0 | 0.150000006 | 1 |
| `filter.drive` | 0 | 0 | 1 |
| `filter.variation` | 0 | 0.5 | 1 |
| `filter.mix` | 0 | 1 | 1 |
| `filter.keytrack` | 0 | 0 | 1 |
| `env1.attack` | 0 | 0.00100000005 | 20 |
| `env1.hold` | 0 | 0 | 10 |
| `env1.decay` | 0 | 1 | 20 |
| `env1.sustain` | 0 | 1 | 1 |
| `env1.release` | 0 | 0.0299999993 | 20 |
| `env1.attackCurve` | -1 | 0 | 1 |
| `env1.decayCurve` | -1 | 0.600000024 | 1 |
| `env1.releaseCurve` | -1 | 0.600000024 | 1 |
| `env2.attack` | 0 | 0.00999999978 | 20 |
| `env2.hold` | 0 | 0 | 10 |
| `env2.decay` | 0 | 0.600000024 | 20 |
| `env2.sustain` | 0 | 0 | 1 |
| `env2.release` | 0 | 0.300000012 | 20 |
| `env2.attackCurve` | -1 | 0 | 1 |
| `env2.decayCurve` | -1 | 0.600000024 | 1 |
| `env2.releaseCurve` | -1 | 0.600000024 | 1 |
| `env3.attack` | 0 | 0.00999999978 | 20 |
| `env3.hold` | 0 | 0 | 10 |
| `env3.decay` | 0 | 0.600000024 | 20 |
| `env3.sustain` | 0 | 0 | 1 |
| `env3.release` | 0 | 0.300000012 | 20 |
| `env3.attackCurve` | -1 | 0 | 1 |
| `env3.decayCurve` | -1 | 0.600000024 | 1 |
| `env3.releaseCurve` | -1 | 0.600000024 | 1 |
| `env4.attack` | 0 | 0.00999999978 | 20 |
| `env4.hold` | 0 | 0 | 10 |
| `env4.decay` | 0 | 0.600000024 | 20 |
| `env4.sustain` | 0 | 0 | 1 |
| `env4.release` | 0 | 0.300000012 | 20 |
| `env4.attackCurve` | -1 | 0 | 1 |
| `env4.decayCurve` | -1 | 0.600000024 | 1 |
| `env4.releaseCurve` | -1 | 0.600000024 | 1 |
| `lfo1.shape` | 0 | 1 | 8 |
| `lfo1.mode` | 0 | 0 | 2 |
| `lfo1.sync` | 0 | 1 | 1 |
| `lfo1.rate` | 0.00999999978 | 2 | 50 |
| `lfo1.division` | 0 | 7 | 16 |
| `lfo1.rise` | 0 | 0 | 10 |
| `lfo1.delay` | 0 | 0 | 10 |
| `lfo1.phase` | 0 | 0 | 1 |
| `lfo1.smooth` | 0 | 0 | 1 |
| `lfo1.unipolar` | 0 | 1 | 1 |
| `lfo2.shape` | 0 | 1 | 8 |
| `lfo2.mode` | 0 | 0 | 2 |
| `lfo2.sync` | 0 | 1 | 1 |
| `lfo2.rate` | 0.00999999978 | 2 | 50 |
| `lfo2.division` | 0 | 7 | 16 |
| `lfo2.rise` | 0 | 0 | 10 |
| `lfo2.delay` | 0 | 0 | 10 |
| `lfo2.phase` | 0 | 0 | 1 |
| `lfo2.smooth` | 0 | 0 | 1 |
| `lfo2.unipolar` | 0 | 1 | 1 |
| `lfo3.shape` | 0 | 1 | 8 |
| `lfo3.mode` | 0 | 0 | 2 |
| `lfo3.sync` | 0 | 1 | 1 |
| `lfo3.rate` | 0.00999999978 | 2 | 50 |
| `lfo3.division` | 0 | 7 | 16 |
| `lfo3.rise` | 0 | 0 | 10 |
| `lfo3.delay` | 0 | 0 | 10 |
| `lfo3.phase` | 0 | 0 | 1 |
| `lfo3.smooth` | 0 | 0 | 1 |
| `lfo3.unipolar` | 0 | 1 | 1 |
| `lfo4.shape` | 0 | 1 | 8 |
| `lfo4.mode` | 0 | 0 | 2 |
| `lfo4.sync` | 0 | 1 | 1 |
| `lfo4.rate` | 0.00999999978 | 2 | 50 |
| `lfo4.division` | 0 | 7 | 16 |
| `lfo4.rise` | 0 | 0 | 10 |
| `lfo4.delay` | 0 | 0 | 10 |
| `lfo4.phase` | 0 | 0 | 1 |
| `lfo4.smooth` | 0 | 0 | 1 |
| `lfo4.unipolar` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.voices` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 5 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bendRange` | 0 | 2 | 24 |
| `voice.velocity` | 0 | 0.699999988 | 1 |
| `voice.master` | 0 | 0.75 | 1 |
| `macro1` | 0 | 0 | 1 |
| `macro2` | 0 | 0 | 1 |
| `macro3` | 0 | 0 | 1 |
| `macro4` | 0 | 0 | 1 |
| `m1.source` | 0 | 0 | 18 |
| `m1.destination` | 0 | 0 | 54 |
| `m1.amount` | -1 | 0 | 1 |
| `m2.source` | 0 | 0 | 18 |
| `m2.destination` | 0 | 0 | 54 |
| `m2.amount` | -1 | 0 | 1 |
| `m3.source` | 0 | 0 | 18 |
| `m3.destination` | 0 | 0 | 54 |
| `m3.amount` | -1 | 0 | 1 |
| `m4.source` | 0 | 0 | 18 |
| `m4.destination` | 0 | 0 | 54 |
| `m4.amount` | -1 | 0 | 1 |
| `m5.source` | 0 | 0 | 18 |
| `m5.destination` | 0 | 0 | 54 |
| `m5.amount` | -1 | 0 | 1 |
| `m6.source` | 0 | 0 | 18 |
| `m6.destination` | 0 | 0 | 54 |
| `m6.amount` | -1 | 0 | 1 |
| `m7.source` | 0 | 0 | 18 |
| `m7.destination` | 0 | 0 | 54 |
| `m7.amount` | -1 | 0 | 1 |
| `m8.source` | 0 | 0 | 18 |
| `m8.destination` | 0 | 0 | 54 |
| `m8.amount` | -1 | 0 | 1 |
| `m9.source` | 0 | 0 | 18 |
| `m9.destination` | 0 | 0 | 54 |
| `m9.amount` | -1 | 0 | 1 |
| `m10.source` | 0 | 0 | 18 |
| `m10.destination` | 0 | 0 | 54 |
| `m10.amount` | -1 | 0 | 1 |
| `m11.source` | 0 | 0 | 18 |
| `m11.destination` | 0 | 0 | 54 |
| `m11.amount` | -1 | 0 | 1 |
| `m12.source` | 0 | 0 | 18 |
| `m12.destination` | 0 | 0 | 54 |
| `m12.amount` | -1 | 0 | 1 |
| `m13.source` | 0 | 0 | 18 |
| `m13.destination` | 0 | 0 | 54 |
| `m13.amount` | -1 | 0 | 1 |
| `m14.source` | 0 | 0 | 18 |
| `m14.destination` | 0 | 0 | 54 |
| `m14.amount` | -1 | 0 | 1 |
| `m15.source` | 0 | 0 | 18 |
| `m15.destination` | 0 | 0 | 54 |
| `m15.amount` | -1 | 0 | 1 |
| `m16.source` | 0 | 0 | 18 |
| `m16.destination` | 0 | 0 | 54 |
| `m16.amount` | -1 | 0 | 1 |
| `lfo1.draw1` | -1 | 0 | 1 |
| `lfo1.draw2` | -1 | 0.195090324 | 1 |
| `lfo1.draw3` | -1 | 0.382683426 | 1 |
| `lfo1.draw4` | -1 | 0.555570245 | 1 |
| `lfo1.draw5` | -1 | 0.707106769 | 1 |
| `lfo1.draw6` | -1 | 0.831469595 | 1 |
| `lfo1.draw7` | -1 | 0.923879504 | 1 |
| `lfo1.draw8` | -1 | 0.980785251 | 1 |
| `lfo1.draw9` | -1 | 1 | 1 |
| `lfo1.draw10` | -1 | 0.980785251 | 1 |
| `lfo1.draw11` | -1 | 0.923879504 | 1 |
| `lfo1.draw12` | -1 | 0.831469595 | 1 |
| `lfo1.draw13` | -1 | 0.707106769 | 1 |
| `lfo1.draw14` | -1 | 0.555570245 | 1 |
| `lfo1.draw15` | -1 | 0.382683426 | 1 |
| `lfo1.draw16` | -1 | 0.195090324 | 1 |
| `lfo1.draw17` | -1 | 1.22464685e-16 | 1 |
| `lfo1.draw18` | -1 | -0.195090324 | 1 |
| `lfo1.draw19` | -1 | -0.382683426 | 1 |
| `lfo1.draw20` | -1 | -0.555570245 | 1 |
| `lfo1.draw21` | -1 | -0.707106769 | 1 |
| `lfo1.draw22` | -1 | -0.831469595 | 1 |
| `lfo1.draw23` | -1 | -0.923879504 | 1 |
| `lfo1.draw24` | -1 | -0.980785251 | 1 |
| `lfo1.draw25` | -1 | -1 | 1 |
| `lfo1.draw26` | -1 | -0.980785251 | 1 |
| `lfo1.draw27` | -1 | -0.923879504 | 1 |
| `lfo1.draw28` | -1 | -0.831469595 | 1 |
| `lfo1.draw29` | -1 | -0.707106769 | 1 |
| `lfo1.draw30` | -1 | -0.555570245 | 1 |
| `lfo1.draw31` | -1 | -0.382683426 | 1 |
| `lfo1.draw32` | -1 | -0.195090324 | 1 |
| `lfo2.draw1` | -1 | 0 | 1 |
| `lfo2.draw2` | -1 | 0.195090324 | 1 |
| `lfo2.draw3` | -1 | 0.382683426 | 1 |
| `lfo2.draw4` | -1 | 0.555570245 | 1 |
| `lfo2.draw5` | -1 | 0.707106769 | 1 |
| `lfo2.draw6` | -1 | 0.831469595 | 1 |
| `lfo2.draw7` | -1 | 0.923879504 | 1 |
| `lfo2.draw8` | -1 | 0.980785251 | 1 |
| `lfo2.draw9` | -1 | 1 | 1 |
| `lfo2.draw10` | -1 | 0.980785251 | 1 |
| `lfo2.draw11` | -1 | 0.923879504 | 1 |
| `lfo2.draw12` | -1 | 0.831469595 | 1 |
| `lfo2.draw13` | -1 | 0.707106769 | 1 |
| `lfo2.draw14` | -1 | 0.555570245 | 1 |
| `lfo2.draw15` | -1 | 0.382683426 | 1 |
| `lfo2.draw16` | -1 | 0.195090324 | 1 |
| `lfo2.draw17` | -1 | 1.22464685e-16 | 1 |
| `lfo2.draw18` | -1 | -0.195090324 | 1 |
| `lfo2.draw19` | -1 | -0.382683426 | 1 |
| `lfo2.draw20` | -1 | -0.555570245 | 1 |
| `lfo2.draw21` | -1 | -0.707106769 | 1 |
| `lfo2.draw22` | -1 | -0.831469595 | 1 |
| `lfo2.draw23` | -1 | -0.923879504 | 1 |
| `lfo2.draw24` | -1 | -0.980785251 | 1 |
| `lfo2.draw25` | -1 | -1 | 1 |
| `lfo2.draw26` | -1 | -0.980785251 | 1 |
| `lfo2.draw27` | -1 | -0.923879504 | 1 |
| `lfo2.draw28` | -1 | -0.831469595 | 1 |
| `lfo2.draw29` | -1 | -0.707106769 | 1 |
| `lfo2.draw30` | -1 | -0.555570245 | 1 |
| `lfo2.draw31` | -1 | -0.382683426 | 1 |
| `lfo2.draw32` | -1 | -0.195090324 | 1 |
| `lfo3.draw1` | -1 | 0 | 1 |
| `lfo3.draw2` | -1 | 0.195090324 | 1 |
| `lfo3.draw3` | -1 | 0.382683426 | 1 |
| `lfo3.draw4` | -1 | 0.555570245 | 1 |
| `lfo3.draw5` | -1 | 0.707106769 | 1 |
| `lfo3.draw6` | -1 | 0.831469595 | 1 |
| `lfo3.draw7` | -1 | 0.923879504 | 1 |
| `lfo3.draw8` | -1 | 0.980785251 | 1 |
| `lfo3.draw9` | -1 | 1 | 1 |
| `lfo3.draw10` | -1 | 0.980785251 | 1 |
| `lfo3.draw11` | -1 | 0.923879504 | 1 |
| `lfo3.draw12` | -1 | 0.831469595 | 1 |
| `lfo3.draw13` | -1 | 0.707106769 | 1 |
| `lfo3.draw14` | -1 | 0.555570245 | 1 |
| `lfo3.draw15` | -1 | 0.382683426 | 1 |
| `lfo3.draw16` | -1 | 0.195090324 | 1 |
| `lfo3.draw17` | -1 | 1.22464685e-16 | 1 |
| `lfo3.draw18` | -1 | -0.195090324 | 1 |
| `lfo3.draw19` | -1 | -0.382683426 | 1 |
| `lfo3.draw20` | -1 | -0.555570245 | 1 |
| `lfo3.draw21` | -1 | -0.707106769 | 1 |
| `lfo3.draw22` | -1 | -0.831469595 | 1 |
| `lfo3.draw23` | -1 | -0.923879504 | 1 |
| `lfo3.draw24` | -1 | -0.980785251 | 1 |
| `lfo3.draw25` | -1 | -1 | 1 |
| `lfo3.draw26` | -1 | -0.980785251 | 1 |
| `lfo3.draw27` | -1 | -0.923879504 | 1 |
| `lfo3.draw28` | -1 | -0.831469595 | 1 |
| `lfo3.draw29` | -1 | -0.707106769 | 1 |
| `lfo3.draw30` | -1 | -0.555570245 | 1 |
| `lfo3.draw31` | -1 | -0.382683426 | 1 |
| `lfo3.draw32` | -1 | -0.195090324 | 1 |
| `lfo4.draw1` | -1 | 0 | 1 |
| `lfo4.draw2` | -1 | 0.195090324 | 1 |
| `lfo4.draw3` | -1 | 0.382683426 | 1 |
| `lfo4.draw4` | -1 | 0.555570245 | 1 |
| `lfo4.draw5` | -1 | 0.707106769 | 1 |
| `lfo4.draw6` | -1 | 0.831469595 | 1 |
| `lfo4.draw7` | -1 | 0.923879504 | 1 |
| `lfo4.draw8` | -1 | 0.980785251 | 1 |
| `lfo4.draw9` | -1 | 1 | 1 |
| `lfo4.draw10` | -1 | 0.980785251 | 1 |
| `lfo4.draw11` | -1 | 0.923879504 | 1 |
| `lfo4.draw12` | -1 | 0.831469595 | 1 |
| `lfo4.draw13` | -1 | 0.707106769 | 1 |
| `lfo4.draw14` | -1 | 0.555570245 | 1 |
| `lfo4.draw15` | -1 | 0.382683426 | 1 |
| `lfo4.draw16` | -1 | 0.195090324 | 1 |
| `lfo4.draw17` | -1 | 1.22464685e-16 | 1 |
| `lfo4.draw18` | -1 | -0.195090324 | 1 |
| `lfo4.draw19` | -1 | -0.382683426 | 1 |
| `lfo4.draw20` | -1 | -0.555570245 | 1 |
| `lfo4.draw21` | -1 | -0.707106769 | 1 |
| `lfo4.draw22` | -1 | -0.831469595 | 1 |
| `lfo4.draw23` | -1 | -0.923879504 | 1 |
| `lfo4.draw24` | -1 | -0.980785251 | 1 |
| `lfo4.draw25` | -1 | -1 | 1 |
| `lfo4.draw26` | -1 | -0.980785251 | 1 |
| `lfo4.draw27` | -1 | -0.923879504 | 1 |
| `lfo4.draw28` | -1 | -0.831469595 | 1 |
| `lfo4.draw29` | -1 | -0.707106769 | 1 |
| `lfo4.draw30` | -1 | -0.555570245 | 1 |
| `lfo4.draw31` | -1 | -0.382683426 | 1 |
| `lfo4.draw32` | -1 | -0.195090324 | 1 |
| `c.on` | 0 | 0 | 1 |
| `c.table` | 0 | 4 | 11 |
| `c.position` | 0 | 0 | 1 |
| `c.warp` | 0 | 0 | 13 |
| `c.warpAmount` | 0 | 0 | 1 |
| `c.unison` | 1 | 1 | 16 |
| `c.detune` | 0 | 0.25 | 1 |
| `c.blend` | 0 | 0.75 | 1 |
| `c.width` | 0 | 1 | 1 |
| `c.octave` | -4 | 0 | 4 |
| `c.semi` | -12 | 0 | 12 |
| `c.fine` | -100 | 0 | 100 |
| `c.coarse` | -36 | 0 | 36 |
| `c.phase` | 0 | 0 | 1 |
| `c.random` | 0 | 1 | 1 |
| `c.level` | 0 | 0.75 | 1 |
| `c.pan` | -1 | 0 | 1 |
| `c.toFilter` | 0 | 1 | 1 |
| `a.spectral` | 0 | 0 | 7 |
| `a.spectralAmount` | 0 | 0.5 | 1 |
| `b.spectral` | 0 | 0 | 7 |
| `b.spectralAmount` | 0 | 0.5 | 1 |
| `c.spectral` | 0 | 0 | 7 |
| `c.spectralAmount` | 0 | 0.5 | 1 |
| `lfo1.stereo` | 0 | 0 | 1 |
| `lfo2.stereo` | 0 | 0 | 1 |
| `lfo3.stereo` | 0 | 0 | 1 |
| `lfo4.stereo` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| a.on / `a.on` | Entrée | value | 1 |
| a.table / `a.table` | Entrée | value | 1 |
| a.position / `a.position` | Entrée | value | 1 |
| a.warp / `a.warp` | Entrée | value | 1 |
| a.warpAmount / `a.warpAmount` | Entrée | value | 1 |
| a.unison / `a.unison` | Entrée | value | 1 |
| a.detune / `a.detune` | Entrée | value | 1 |
| a.blend / `a.blend` | Entrée | value | 1 |
| a.width / `a.width` | Entrée | value | 1 |
| a.octave / `a.octave` | Entrée | value | 1 |
| a.semi / `a.semi` | Entrée | value | 1 |
| a.fine / `a.fine` | Entrée | value | 1 |
| a.coarse / `a.coarse` | Entrée | value | 1 |
| a.phase / `a.phase` | Entrée | value | 1 |
| a.random / `a.random` | Entrée | value | 1 |
| a.level / `a.level` | Entrée | value | 1 |
| a.pan / `a.pan` | Entrée | value | 1 |
| a.toFilter / `a.toFilter` | Entrée | value | 1 |
| b.on / `b.on` | Entrée | value | 1 |
| b.table / `b.table` | Entrée | value | 1 |
| b.position / `b.position` | Entrée | value | 1 |
| b.warp / `b.warp` | Entrée | value | 1 |
| b.warpAmount / `b.warpAmount` | Entrée | value | 1 |
| b.unison / `b.unison` | Entrée | value | 1 |
| b.detune / `b.detune` | Entrée | value | 1 |
| b.blend / `b.blend` | Entrée | value | 1 |
| b.width / `b.width` | Entrée | value | 1 |
| b.octave / `b.octave` | Entrée | value | 1 |
| b.semi / `b.semi` | Entrée | value | 1 |
| b.fine / `b.fine` | Entrée | value | 1 |
| b.coarse / `b.coarse` | Entrée | value | 1 |
| b.phase / `b.phase` | Entrée | value | 1 |
| b.random / `b.random` | Entrée | value | 1 |
| b.level / `b.level` | Entrée | value | 1 |
| b.pan / `b.pan` | Entrée | value | 1 |
| b.toFilter / `b.toFilter` | Entrée | value | 1 |
| sub.on / `sub.on` | Entrée | value | 1 |
| sub.shape / `sub.shape` | Entrée | value | 1 |
| sub.octave / `sub.octave` | Entrée | value | 1 |
| sub.level / `sub.level` | Entrée | value | 1 |
| sub.pan / `sub.pan` | Entrée | value | 1 |
| sub.direct / `sub.direct` | Entrée | value | 1 |
| noise.on / `noise.on` | Entrée | value | 1 |
| noise.type / `noise.type` | Entrée | value | 1 |
| noise.tone / `noise.tone` | Entrée | value | 1 |
| noise.pitch / `noise.pitch` | Entrée | value | 1 |
| noise.level / `noise.level` | Entrée | value | 1 |
| noise.pan / `noise.pan` | Entrée | value | 1 |
| noise.toFilter / `noise.toFilter` | Entrée | value | 1 |
| filter.on / `filter.on` | Entrée | value | 1 |
| filter.type / `filter.type` | Entrée | value | 1 |
| filter.cutoff / `filter.cutoff` | Entrée | value | 1 |
| filter.resonance / `filter.resonance` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| filter.variation / `filter.variation` | Entrée | value | 1 |
| filter.mix / `filter.mix` | Entrée | value | 1 |
| filter.keytrack / `filter.keytrack` | Entrée | value | 1 |
| env1.attack / `env1.attack` | Entrée | value | 1 |
| env1.hold / `env1.hold` | Entrée | value | 1 |
| env1.decay / `env1.decay` | Entrée | value | 1 |
| env1.sustain / `env1.sustain` | Entrée | value | 1 |
| env1.release / `env1.release` | Entrée | value | 1 |
| env1.attackCurve / `env1.attackCurve` | Entrée | value | 1 |
| env1.decayCurve / `env1.decayCurve` | Entrée | value | 1 |
| env1.releaseCurve / `env1.releaseCurve` | Entrée | value | 1 |
| env2.attack / `env2.attack` | Entrée | value | 1 |
| env2.hold / `env2.hold` | Entrée | value | 1 |
| env2.decay / `env2.decay` | Entrée | value | 1 |
| env2.sustain / `env2.sustain` | Entrée | value | 1 |
| env2.release / `env2.release` | Entrée | value | 1 |
| env2.attackCurve / `env2.attackCurve` | Entrée | value | 1 |
| env2.decayCurve / `env2.decayCurve` | Entrée | value | 1 |
| env2.releaseCurve / `env2.releaseCurve` | Entrée | value | 1 |
| env3.attack / `env3.attack` | Entrée | value | 1 |
| env3.hold / `env3.hold` | Entrée | value | 1 |
| env3.decay / `env3.decay` | Entrée | value | 1 |
| env3.sustain / `env3.sustain` | Entrée | value | 1 |
| env3.release / `env3.release` | Entrée | value | 1 |
| env3.attackCurve / `env3.attackCurve` | Entrée | value | 1 |
| env3.decayCurve / `env3.decayCurve` | Entrée | value | 1 |
| env3.releaseCurve / `env3.releaseCurve` | Entrée | value | 1 |
| env4.attack / `env4.attack` | Entrée | value | 1 |
| env4.hold / `env4.hold` | Entrée | value | 1 |
| env4.decay / `env4.decay` | Entrée | value | 1 |
| env4.sustain / `env4.sustain` | Entrée | value | 1 |
| env4.release / `env4.release` | Entrée | value | 1 |
| env4.attackCurve / `env4.attackCurve` | Entrée | value | 1 |
| env4.decayCurve / `env4.decayCurve` | Entrée | value | 1 |
| env4.releaseCurve / `env4.releaseCurve` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.mode / `lfo1.mode` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.division / `lfo1.division` | Entrée | value | 1 |
| lfo1.rise / `lfo1.rise` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.phase / `lfo1.phase` | Entrée | value | 1 |
| lfo1.smooth / `lfo1.smooth` | Entrée | value | 1 |
| lfo1.unipolar / `lfo1.unipolar` | Entrée | value | 1 |
| lfo2.shape / `lfo2.shape` | Entrée | value | 1 |
| lfo2.mode / `lfo2.mode` | Entrée | value | 1 |
| lfo2.sync / `lfo2.sync` | Entrée | value | 1 |
| lfo2.rate / `lfo2.rate` | Entrée | value | 1 |
| lfo2.division / `lfo2.division` | Entrée | value | 1 |
| lfo2.rise / `lfo2.rise` | Entrée | value | 1 |
| lfo2.delay / `lfo2.delay` | Entrée | value | 1 |
| lfo2.phase / `lfo2.phase` | Entrée | value | 1 |
| lfo2.smooth / `lfo2.smooth` | Entrée | value | 1 |
| lfo2.unipolar / `lfo2.unipolar` | Entrée | value | 1 |
| lfo3.shape / `lfo3.shape` | Entrée | value | 1 |
| lfo3.mode / `lfo3.mode` | Entrée | value | 1 |
| lfo3.sync / `lfo3.sync` | Entrée | value | 1 |
| lfo3.rate / `lfo3.rate` | Entrée | value | 1 |
| lfo3.division / `lfo3.division` | Entrée | value | 1 |
| lfo3.rise / `lfo3.rise` | Entrée | value | 1 |
| lfo3.delay / `lfo3.delay` | Entrée | value | 1 |
| lfo3.phase / `lfo3.phase` | Entrée | value | 1 |
| lfo3.smooth / `lfo3.smooth` | Entrée | value | 1 |
| lfo3.unipolar / `lfo3.unipolar` | Entrée | value | 1 |
| lfo4.shape / `lfo4.shape` | Entrée | value | 1 |
| lfo4.mode / `lfo4.mode` | Entrée | value | 1 |
| lfo4.sync / `lfo4.sync` | Entrée | value | 1 |
| lfo4.rate / `lfo4.rate` | Entrée | value | 1 |
| lfo4.division / `lfo4.division` | Entrée | value | 1 |
| lfo4.rise / `lfo4.rise` | Entrée | value | 1 |
| lfo4.delay / `lfo4.delay` | Entrée | value | 1 |
| lfo4.phase / `lfo4.phase` | Entrée | value | 1 |
| lfo4.smooth / `lfo4.smooth` | Entrée | value | 1 |
| lfo4.unipolar / `lfo4.unipolar` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.voices / `voice.voices` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bendRange / `voice.bendRange` | Entrée | value | 1 |
| voice.velocity / `voice.velocity` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| macro1 / `macro1` | Entrée | value | 1 |
| macro2 / `macro2` | Entrée | value | 1 |
| macro3 / `macro3` | Entrée | value | 1 |
| macro4 / `macro4` | Entrée | value | 1 |
| m1.source / `m1.source` | Entrée | value | 1 |
| m1.destination / `m1.destination` | Entrée | value | 1 |
| m1.amount / `m1.amount` | Entrée | value | 1 |
| m2.source / `m2.source` | Entrée | value | 1 |
| m2.destination / `m2.destination` | Entrée | value | 1 |
| m2.amount / `m2.amount` | Entrée | value | 1 |
| m3.source / `m3.source` | Entrée | value | 1 |
| m3.destination / `m3.destination` | Entrée | value | 1 |
| m3.amount / `m3.amount` | Entrée | value | 1 |
| m4.source / `m4.source` | Entrée | value | 1 |
| m4.destination / `m4.destination` | Entrée | value | 1 |
| m4.amount / `m4.amount` | Entrée | value | 1 |
| m5.source / `m5.source` | Entrée | value | 1 |
| m5.destination / `m5.destination` | Entrée | value | 1 |
| m5.amount / `m5.amount` | Entrée | value | 1 |
| m6.source / `m6.source` | Entrée | value | 1 |
| m6.destination / `m6.destination` | Entrée | value | 1 |
| m6.amount / `m6.amount` | Entrée | value | 1 |
| m7.source / `m7.source` | Entrée | value | 1 |
| m7.destination / `m7.destination` | Entrée | value | 1 |
| m7.amount / `m7.amount` | Entrée | value | 1 |
| m8.source / `m8.source` | Entrée | value | 1 |
| m8.destination / `m8.destination` | Entrée | value | 1 |
| m8.amount / `m8.amount` | Entrée | value | 1 |
| m9.source / `m9.source` | Entrée | value | 1 |
| m9.destination / `m9.destination` | Entrée | value | 1 |
| m9.amount / `m9.amount` | Entrée | value | 1 |
| m10.source / `m10.source` | Entrée | value | 1 |
| m10.destination / `m10.destination` | Entrée | value | 1 |
| m10.amount / `m10.amount` | Entrée | value | 1 |
| m11.source / `m11.source` | Entrée | value | 1 |
| m11.destination / `m11.destination` | Entrée | value | 1 |
| m11.amount / `m11.amount` | Entrée | value | 1 |
| m12.source / `m12.source` | Entrée | value | 1 |
| m12.destination / `m12.destination` | Entrée | value | 1 |
| m12.amount / `m12.amount` | Entrée | value | 1 |
| m13.source / `m13.source` | Entrée | value | 1 |
| m13.destination / `m13.destination` | Entrée | value | 1 |
| m13.amount / `m13.amount` | Entrée | value | 1 |
| m14.source / `m14.source` | Entrée | value | 1 |
| m14.destination / `m14.destination` | Entrée | value | 1 |
| m14.amount / `m14.amount` | Entrée | value | 1 |
| m15.source / `m15.source` | Entrée | value | 1 |
| m15.destination / `m15.destination` | Entrée | value | 1 |
| m15.amount / `m15.amount` | Entrée | value | 1 |
| m16.source / `m16.source` | Entrée | value | 1 |
| m16.destination / `m16.destination` | Entrée | value | 1 |
| m16.amount / `m16.amount` | Entrée | value | 1 |
| lfo1.draw1 / `lfo1.draw1` | Entrée | value | 1 |
| lfo1.draw2 / `lfo1.draw2` | Entrée | value | 1 |
| lfo1.draw3 / `lfo1.draw3` | Entrée | value | 1 |
| lfo1.draw4 / `lfo1.draw4` | Entrée | value | 1 |
| lfo1.draw5 / `lfo1.draw5` | Entrée | value | 1 |
| lfo1.draw6 / `lfo1.draw6` | Entrée | value | 1 |
| lfo1.draw7 / `lfo1.draw7` | Entrée | value | 1 |
| lfo1.draw8 / `lfo1.draw8` | Entrée | value | 1 |
| lfo1.draw9 / `lfo1.draw9` | Entrée | value | 1 |
| lfo1.draw10 / `lfo1.draw10` | Entrée | value | 1 |
| lfo1.draw11 / `lfo1.draw11` | Entrée | value | 1 |
| lfo1.draw12 / `lfo1.draw12` | Entrée | value | 1 |
| lfo1.draw13 / `lfo1.draw13` | Entrée | value | 1 |
| lfo1.draw14 / `lfo1.draw14` | Entrée | value | 1 |
| lfo1.draw15 / `lfo1.draw15` | Entrée | value | 1 |
| lfo1.draw16 / `lfo1.draw16` | Entrée | value | 1 |
| lfo1.draw17 / `lfo1.draw17` | Entrée | value | 1 |
| lfo1.draw18 / `lfo1.draw18` | Entrée | value | 1 |
| lfo1.draw19 / `lfo1.draw19` | Entrée | value | 1 |
| lfo1.draw20 / `lfo1.draw20` | Entrée | value | 1 |
| lfo1.draw21 / `lfo1.draw21` | Entrée | value | 1 |
| lfo1.draw22 / `lfo1.draw22` | Entrée | value | 1 |
| lfo1.draw23 / `lfo1.draw23` | Entrée | value | 1 |
| lfo1.draw24 / `lfo1.draw24` | Entrée | value | 1 |
| lfo1.draw25 / `lfo1.draw25` | Entrée | value | 1 |
| lfo1.draw26 / `lfo1.draw26` | Entrée | value | 1 |
| lfo1.draw27 / `lfo1.draw27` | Entrée | value | 1 |
| lfo1.draw28 / `lfo1.draw28` | Entrée | value | 1 |
| lfo1.draw29 / `lfo1.draw29` | Entrée | value | 1 |
| lfo1.draw30 / `lfo1.draw30` | Entrée | value | 1 |
| lfo1.draw31 / `lfo1.draw31` | Entrée | value | 1 |
| lfo1.draw32 / `lfo1.draw32` | Entrée | value | 1 |
| lfo2.draw1 / `lfo2.draw1` | Entrée | value | 1 |
| lfo2.draw2 / `lfo2.draw2` | Entrée | value | 1 |
| lfo2.draw3 / `lfo2.draw3` | Entrée | value | 1 |
| lfo2.draw4 / `lfo2.draw4` | Entrée | value | 1 |
| lfo2.draw5 / `lfo2.draw5` | Entrée | value | 1 |
| lfo2.draw6 / `lfo2.draw6` | Entrée | value | 1 |
| lfo2.draw7 / `lfo2.draw7` | Entrée | value | 1 |
| lfo2.draw8 / `lfo2.draw8` | Entrée | value | 1 |
| lfo2.draw9 / `lfo2.draw9` | Entrée | value | 1 |
| lfo2.draw10 / `lfo2.draw10` | Entrée | value | 1 |
| lfo2.draw11 / `lfo2.draw11` | Entrée | value | 1 |
| lfo2.draw12 / `lfo2.draw12` | Entrée | value | 1 |
| lfo2.draw13 / `lfo2.draw13` | Entrée | value | 1 |
| lfo2.draw14 / `lfo2.draw14` | Entrée | value | 1 |
| lfo2.draw15 / `lfo2.draw15` | Entrée | value | 1 |
| lfo2.draw16 / `lfo2.draw16` | Entrée | value | 1 |
| lfo2.draw17 / `lfo2.draw17` | Entrée | value | 1 |
| lfo2.draw18 / `lfo2.draw18` | Entrée | value | 1 |
| lfo2.draw19 / `lfo2.draw19` | Entrée | value | 1 |
| lfo2.draw20 / `lfo2.draw20` | Entrée | value | 1 |
| lfo2.draw21 / `lfo2.draw21` | Entrée | value | 1 |
| lfo2.draw22 / `lfo2.draw22` | Entrée | value | 1 |
| lfo2.draw23 / `lfo2.draw23` | Entrée | value | 1 |
| lfo2.draw24 / `lfo2.draw24` | Entrée | value | 1 |
| lfo2.draw25 / `lfo2.draw25` | Entrée | value | 1 |
| lfo2.draw26 / `lfo2.draw26` | Entrée | value | 1 |
| lfo2.draw27 / `lfo2.draw27` | Entrée | value | 1 |
| lfo2.draw28 / `lfo2.draw28` | Entrée | value | 1 |
| lfo2.draw29 / `lfo2.draw29` | Entrée | value | 1 |
| lfo2.draw30 / `lfo2.draw30` | Entrée | value | 1 |
| lfo2.draw31 / `lfo2.draw31` | Entrée | value | 1 |
| lfo2.draw32 / `lfo2.draw32` | Entrée | value | 1 |
| lfo3.draw1 / `lfo3.draw1` | Entrée | value | 1 |
| lfo3.draw2 / `lfo3.draw2` | Entrée | value | 1 |
| lfo3.draw3 / `lfo3.draw3` | Entrée | value | 1 |
| lfo3.draw4 / `lfo3.draw4` | Entrée | value | 1 |
| lfo3.draw5 / `lfo3.draw5` | Entrée | value | 1 |
| lfo3.draw6 / `lfo3.draw6` | Entrée | value | 1 |
| lfo3.draw7 / `lfo3.draw7` | Entrée | value | 1 |
| lfo3.draw8 / `lfo3.draw8` | Entrée | value | 1 |
| lfo3.draw9 / `lfo3.draw9` | Entrée | value | 1 |
| lfo3.draw10 / `lfo3.draw10` | Entrée | value | 1 |
| lfo3.draw11 / `lfo3.draw11` | Entrée | value | 1 |
| lfo3.draw12 / `lfo3.draw12` | Entrée | value | 1 |
| lfo3.draw13 / `lfo3.draw13` | Entrée | value | 1 |
| lfo3.draw14 / `lfo3.draw14` | Entrée | value | 1 |
| lfo3.draw15 / `lfo3.draw15` | Entrée | value | 1 |
| lfo3.draw16 / `lfo3.draw16` | Entrée | value | 1 |
| lfo3.draw17 / `lfo3.draw17` | Entrée | value | 1 |
| lfo3.draw18 / `lfo3.draw18` | Entrée | value | 1 |
| lfo3.draw19 / `lfo3.draw19` | Entrée | value | 1 |
| lfo3.draw20 / `lfo3.draw20` | Entrée | value | 1 |
| lfo3.draw21 / `lfo3.draw21` | Entrée | value | 1 |
| lfo3.draw22 / `lfo3.draw22` | Entrée | value | 1 |
| lfo3.draw23 / `lfo3.draw23` | Entrée | value | 1 |
| lfo3.draw24 / `lfo3.draw24` | Entrée | value | 1 |
| lfo3.draw25 / `lfo3.draw25` | Entrée | value | 1 |
| lfo3.draw26 / `lfo3.draw26` | Entrée | value | 1 |
| lfo3.draw27 / `lfo3.draw27` | Entrée | value | 1 |
| lfo3.draw28 / `lfo3.draw28` | Entrée | value | 1 |
| lfo3.draw29 / `lfo3.draw29` | Entrée | value | 1 |
| lfo3.draw30 / `lfo3.draw30` | Entrée | value | 1 |
| lfo3.draw31 / `lfo3.draw31` | Entrée | value | 1 |
| lfo3.draw32 / `lfo3.draw32` | Entrée | value | 1 |
| lfo4.draw1 / `lfo4.draw1` | Entrée | value | 1 |
| lfo4.draw2 / `lfo4.draw2` | Entrée | value | 1 |
| lfo4.draw3 / `lfo4.draw3` | Entrée | value | 1 |
| lfo4.draw4 / `lfo4.draw4` | Entrée | value | 1 |
| lfo4.draw5 / `lfo4.draw5` | Entrée | value | 1 |
| lfo4.draw6 / `lfo4.draw6` | Entrée | value | 1 |
| lfo4.draw7 / `lfo4.draw7` | Entrée | value | 1 |
| lfo4.draw8 / `lfo4.draw8` | Entrée | value | 1 |
| lfo4.draw9 / `lfo4.draw9` | Entrée | value | 1 |
| lfo4.draw10 / `lfo4.draw10` | Entrée | value | 1 |
| lfo4.draw11 / `lfo4.draw11` | Entrée | value | 1 |
| lfo4.draw12 / `lfo4.draw12` | Entrée | value | 1 |
| lfo4.draw13 / `lfo4.draw13` | Entrée | value | 1 |
| lfo4.draw14 / `lfo4.draw14` | Entrée | value | 1 |
| lfo4.draw15 / `lfo4.draw15` | Entrée | value | 1 |
| lfo4.draw16 / `lfo4.draw16` | Entrée | value | 1 |
| lfo4.draw17 / `lfo4.draw17` | Entrée | value | 1 |
| lfo4.draw18 / `lfo4.draw18` | Entrée | value | 1 |
| lfo4.draw19 / `lfo4.draw19` | Entrée | value | 1 |
| lfo4.draw20 / `lfo4.draw20` | Entrée | value | 1 |
| lfo4.draw21 / `lfo4.draw21` | Entrée | value | 1 |
| lfo4.draw22 / `lfo4.draw22` | Entrée | value | 1 |
| lfo4.draw23 / `lfo4.draw23` | Entrée | value | 1 |
| lfo4.draw24 / `lfo4.draw24` | Entrée | value | 1 |
| lfo4.draw25 / `lfo4.draw25` | Entrée | value | 1 |
| lfo4.draw26 / `lfo4.draw26` | Entrée | value | 1 |
| lfo4.draw27 / `lfo4.draw27` | Entrée | value | 1 |
| lfo4.draw28 / `lfo4.draw28` | Entrée | value | 1 |
| lfo4.draw29 / `lfo4.draw29` | Entrée | value | 1 |
| lfo4.draw30 / `lfo4.draw30` | Entrée | value | 1 |
| lfo4.draw31 / `lfo4.draw31` | Entrée | value | 1 |
| lfo4.draw32 / `lfo4.draw32` | Entrée | value | 1 |
| c.on / `c.on` | Entrée | value | 1 |
| c.table / `c.table` | Entrée | value | 1 |
| c.position / `c.position` | Entrée | value | 1 |
| c.warp / `c.warp` | Entrée | value | 1 |
| c.warpAmount / `c.warpAmount` | Entrée | value | 1 |
| c.unison / `c.unison` | Entrée | value | 1 |
| c.detune / `c.detune` | Entrée | value | 1 |
| c.blend / `c.blend` | Entrée | value | 1 |
| c.width / `c.width` | Entrée | value | 1 |
| c.octave / `c.octave` | Entrée | value | 1 |
| c.semi / `c.semi` | Entrée | value | 1 |
| c.fine / `c.fine` | Entrée | value | 1 |
| c.coarse / `c.coarse` | Entrée | value | 1 |
| c.phase / `c.phase` | Entrée | value | 1 |
| c.random / `c.random` | Entrée | value | 1 |
| c.level / `c.level` | Entrée | value | 1 |
| c.pan / `c.pan` | Entrée | value | 1 |
| c.toFilter / `c.toFilter` | Entrée | value | 1 |
| a.spectral / `a.spectral` | Entrée | value | 1 |
| a.spectralAmount / `a.spectralAmount` | Entrée | value | 1 |
| b.spectral / `b.spectral` | Entrée | value | 1 |
| b.spectralAmount / `b.spectralAmount` | Entrée | value | 1 |
| c.spectral / `c.spectral` | Entrée | value | 1 |
| c.spectralAmount / `c.spectralAmount` | Entrée | value | 1 |
| lfo1.stereo / `lfo1.stereo` | Entrée | value | 1 |
| lfo2.stereo / `lfo2.stereo` | Entrée | value | 1 |
| lfo3.stereo / `lfo3.stereo` | Entrée | value | 1 |
| lfo4.stereo / `lfo4.stereo` | Entrée | value | 1 |

## QUARTZ (quartz-synth) {#quartz-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.600000024 | 1 |
| `op1.ratio` | 0.125 | 1 | 16 |
| `op1.fixed` | 0 | 0 | 1 |
| `op1.hz` | 0.5 | 110 | 12000 |
| `op1.fine` | -100 | 0 | 100 |
| `op1.level` | 0 | 0.800000012 | 1 |
| `op1.depth` | 0 | 1 | 1 |
| `op1.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `op1.decay` | 0.00499999989 | 1.5 | 20 |
| `op1.sustain` | 0 | 0.699999988 | 1 |
| `op1.release` | 0.00499999989 | 0.400000006 | 20 |
| `m.11` | 0 | 0 | 1 |
| `m.12` | 0 | 0 | 1 |
| `m.13` | 0 | 0 | 1 |
| `m.14` | 0 | 0 | 1 |
| `op2.ratio` | 0.125 | 1 | 16 |
| `op2.fixed` | 0 | 0 | 1 |
| `op2.hz` | 0.5 | 440 | 12000 |
| `op2.fine` | -100 | 0 | 100 |
| `op2.level` | 0 | 0 | 1 |
| `op2.depth` | 0 | 1 | 1 |
| `op2.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `op2.decay` | 0.00499999989 | 1.20000005 | 20 |
| `op2.sustain` | 0 | 0.449999988 | 1 |
| `op2.release` | 0.00499999989 | 0.400000006 | 20 |
| `m.21` | 0 | 0.349999994 | 1 |
| `m.22` | 0 | 0 | 1 |
| `m.23` | 0 | 0 | 1 |
| `m.24` | 0 | 0 | 1 |
| `op3.ratio` | 0.125 | 2 | 16 |
| `op3.fixed` | 0 | 0 | 1 |
| `op3.hz` | 0.5 | 1200 | 12000 |
| `op3.fine` | -100 | 0 | 100 |
| `op3.level` | 0 | 0 | 1 |
| `op3.depth` | 0 | 1 | 1 |
| `op3.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `op3.decay` | 0.00499999989 | 1 | 20 |
| `op3.sustain` | 0 | 0.5 | 1 |
| `op3.release` | 0.00499999989 | 0.400000006 | 20 |
| `m.31` | 0 | 0 | 1 |
| `m.32` | 0 | 0 | 1 |
| `m.33` | 0 | 0 | 1 |
| `m.34` | 0 | 0 | 1 |
| `op4.ratio` | 0.125 | 3 | 16 |
| `op4.fixed` | 0 | 0 | 1 |
| `op4.hz` | 0.5 | 3200 | 12000 |
| `op4.fine` | -100 | 0 | 100 |
| `op4.level` | 0 | 0 | 1 |
| `op4.depth` | 0 | 1 | 1 |
| `op4.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `op4.decay` | 0.00499999989 | 0.800000012 | 20 |
| `op4.sustain` | 0 | 0.5 | 1 |
| `op4.release` | 0.00499999989 | 0.400000006 | 20 |
| `m.41` | 0 | 0 | 1 |
| `m.42` | 0 | 0 | 1 |
| `m.43` | 0 | 0 | 1 |
| `m.44` | 0 | 0 | 1 |
| `m.n1` | 0 | 0 | 1 |
| `m.n2` | 0 | 0 | 1 |
| `m.n3` | 0 | 0 | 1 |
| `m.n4` | 0 | 0 | 1 |
| `noise.tone` | 0 | 0.699999988 | 1 |
| `ratio.snap` | 0 | 1 | 1 |
| `velocity.depth` | 0 | 0.5 | 1 |
| `key.depth` | 0 | 0.300000012 | 1 |
| `key.time` | 0 | 0 | 1 |
| `f.type` | 0 | 1 | 6 |
| `f.cutoff` | 20 | 9000 | 20000 |
| `f.resonance` | 0 | 0.100000001 | 1 |
| `filter.drive` | 0 | 0 | 1 |
| `filter.env` | -1 | 0 | 1 |
| `filter.key` | 0 | 0.300000012 | 1 |
| `filter.velocity` | 0 | 0.200000003 | 1 |
| `amp.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `amp.decay` | 0.00499999989 | 1 | 20 |
| `amp.sustain` | 0 | 1 | 1 |
| `amp.release` | 0.00499999989 | 0.349999994 | 20 |
| `amp.velocity` | 0 | 0.5 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 10 |
| `fenv.decay` | 0.00499999989 | 0.600000024 | 20 |
| `fenv.sustain` | 0 | 0.300000012 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 20 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 20 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0.200000003 | 3 |
| `lfo1.pitch` | 0 | 0 | 1 |
| `lfo1.depth` | 0 | 0 | 1 |
| `lfo1.cutoff` | 0 | 0 | 1 |
| `lfo1.amp` | 0 | 0 | 1 |
| `wheel.vibrato` | 0 | 0.400000006 | 1 |
| `wheel.depth` | 0 | 0.300000012 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| op1.ratio / `op1.ratio` | Entrée | value | 1 |
| op1.fixed / `op1.fixed` | Entrée | value | 1 |
| op1.hz / `op1.hz` | Entrée | value | 1 |
| op1.fine / `op1.fine` | Entrée | value | 1 |
| op1.level / `op1.level` | Entrée | value | 1 |
| op1.depth / `op1.depth` | Entrée | value | 1 |
| op1.attack / `op1.attack` | Entrée | value | 1 |
| op1.decay / `op1.decay` | Entrée | value | 1 |
| op1.sustain / `op1.sustain` | Entrée | value | 1 |
| op1.release / `op1.release` | Entrée | value | 1 |
| m.11 / `m.11` | Entrée | value | 1 |
| m.12 / `m.12` | Entrée | value | 1 |
| m.13 / `m.13` | Entrée | value | 1 |
| m.14 / `m.14` | Entrée | value | 1 |
| op2.ratio / `op2.ratio` | Entrée | value | 1 |
| op2.fixed / `op2.fixed` | Entrée | value | 1 |
| op2.hz / `op2.hz` | Entrée | value | 1 |
| op2.fine / `op2.fine` | Entrée | value | 1 |
| op2.level / `op2.level` | Entrée | value | 1 |
| op2.depth / `op2.depth` | Entrée | value | 1 |
| op2.attack / `op2.attack` | Entrée | value | 1 |
| op2.decay / `op2.decay` | Entrée | value | 1 |
| op2.sustain / `op2.sustain` | Entrée | value | 1 |
| op2.release / `op2.release` | Entrée | value | 1 |
| m.21 / `m.21` | Entrée | value | 1 |
| m.22 / `m.22` | Entrée | value | 1 |
| m.23 / `m.23` | Entrée | value | 1 |
| m.24 / `m.24` | Entrée | value | 1 |
| op3.ratio / `op3.ratio` | Entrée | value | 1 |
| op3.fixed / `op3.fixed` | Entrée | value | 1 |
| op3.hz / `op3.hz` | Entrée | value | 1 |
| op3.fine / `op3.fine` | Entrée | value | 1 |
| op3.level / `op3.level` | Entrée | value | 1 |
| op3.depth / `op3.depth` | Entrée | value | 1 |
| op3.attack / `op3.attack` | Entrée | value | 1 |
| op3.decay / `op3.decay` | Entrée | value | 1 |
| op3.sustain / `op3.sustain` | Entrée | value | 1 |
| op3.release / `op3.release` | Entrée | value | 1 |
| m.31 / `m.31` | Entrée | value | 1 |
| m.32 / `m.32` | Entrée | value | 1 |
| m.33 / `m.33` | Entrée | value | 1 |
| m.34 / `m.34` | Entrée | value | 1 |
| op4.ratio / `op4.ratio` | Entrée | value | 1 |
| op4.fixed / `op4.fixed` | Entrée | value | 1 |
| op4.hz / `op4.hz` | Entrée | value | 1 |
| op4.fine / `op4.fine` | Entrée | value | 1 |
| op4.level / `op4.level` | Entrée | value | 1 |
| op4.depth / `op4.depth` | Entrée | value | 1 |
| op4.attack / `op4.attack` | Entrée | value | 1 |
| op4.decay / `op4.decay` | Entrée | value | 1 |
| op4.sustain / `op4.sustain` | Entrée | value | 1 |
| op4.release / `op4.release` | Entrée | value | 1 |
| m.41 / `m.41` | Entrée | value | 1 |
| m.42 / `m.42` | Entrée | value | 1 |
| m.43 / `m.43` | Entrée | value | 1 |
| m.44 / `m.44` | Entrée | value | 1 |
| m.n1 / `m.n1` | Entrée | value | 1 |
| m.n2 / `m.n2` | Entrée | value | 1 |
| m.n3 / `m.n3` | Entrée | value | 1 |
| m.n4 / `m.n4` | Entrée | value | 1 |
| noise.tone / `noise.tone` | Entrée | value | 1 |
| ratio.snap / `ratio.snap` | Entrée | value | 1 |
| velocity.depth / `velocity.depth` | Entrée | value | 1 |
| key.depth / `key.depth` | Entrée | value | 1 |
| key.time / `key.time` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.decay / `amp.decay` | Entrée | value | 1 |
| amp.sustain / `amp.sustain` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.depth / `lfo1.depth` | Entrée | value | 1 |
| lfo1.cutoff / `lfo1.cutoff` | Entrée | value | 1 |
| lfo1.amp / `lfo1.amp` | Entrée | value | 1 |
| wheel.vibrato / `wheel.vibrato` | Entrée | value | 1 |
| wheel.depth / `wheel.depth` | Entrée | value | 1 |

## SAMPLER (sampler) {#sampler}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `transpose` | -48 | 0 | 48 |
| `fine` | -100 | 0 | 100 |
| `start` | 0 | 0 | 2000 |
| `velocity-db` | 0 | 24 | 48 |
| `velocity-crossfade` | 0 | 0 | 1 |
| `loop` | 0 | 1 | 2 |
| `key-track` | 0 | 1 | 1 |
| `reverse` | 0 | 0 | 1 |
| `width` | 0 | 1 | 2 |
| `one-shot` | 0 | 0 | 1 |
| `articulation` | -1 | -1 | 127 |
| `simple` | 0 | 0 | 1 |
| `grains` | 0 | 0 | 1 |
| `grain-position` | 0 | 0.300000012 | 1 |
| `grain-scan` | -2 | 0 | 2 |
| `grain-size` | 10 | 120 | 1000 |
| `grain-density` | 1 | 24 | 120 |
| `grain-spray` | 0 | 0.0500000007 | 1 |
| `grain-pitch` | 0 | 0 | 12 |
| `grain-stereo` | 0 | 0.5 | 1 |
| `grain-reverse` | 0 | 0 | 1 |
| `grain-shape` | 0 | 0.200000003 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| transpose / `transpose` | Entrée | value | 1 |
| fine / `fine` | Entrée | value | 1 |
| start / `start` | Entrée | value | 1 |
| velocity-db / `velocity-db` | Entrée | value | 1 |
| velocity-crossfade / `velocity-crossfade` | Entrée | value | 1 |
| loop / `loop` | Entrée | value | 1 |
| key-track / `key-track` | Entrée | value | 1 |
| reverse / `reverse` | Entrée | value | 1 |
| width / `width` | Entrée | value | 1 |
| one-shot / `one-shot` | Entrée | value | 1 |
| articulation / `articulation` | Entrée | value | 1 |
| simple / `simple` | Entrée | value | 1 |
| grains / `grains` | Entrée | value | 1 |
| grain-position / `grain-position` | Entrée | value | 1 |
| grain-scan / `grain-scan` | Entrée | value | 1 |
| grain-size / `grain-size` | Entrée | value | 1 |
| grain-density / `grain-density` | Entrée | value | 1 |
| grain-spray / `grain-spray` | Entrée | value | 1 |
| grain-pitch / `grain-pitch` | Entrée | value | 1 |
| grain-stereo / `grain-stereo` | Entrée | value | 1 |
| grain-reverse / `grain-reverse` | Entrée | value | 1 |
| grain-shape / `grain-shape` | Entrée | value | 1 |

## SAPHIR (saphir-synth) {#saphir-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.649999976 | 1 |
| `algorithm` | 0 | 0 | 10 |
| `feedback` | 0 | 0 | 1 |
| `a.wave` | 0 | 0 | 11 |
| `a.coarse` | 0 | 1 | 48 |
| `a.fine` | 0 | 0 | 1000 |
| `a.detune` | -50 | 0 | 50 |
| `a.fixed` | 0 | 0 | 1 |
| `a.freq` | 1 | 440 | 10000 |
| `a.level` | -48 | 0 | 0 |
| `a.velocity` | 0 | 0.400000006 | 1 |
| `a.keyScale` | -1 | 0 | 1 |
| `a.lfo` | 0 | 0 | 1 |
| `a.attack` | 0.00100000005 | 0.00200000009 | 20 |
| `a.decay` | 0.00499999989 | 2.5 | 20 |
| `a.sustain` | 0 | 0.550000012 | 1 |
| `a.release` | 0.00499999989 | 0.5 | 20 |
| `a.loop` | 0 | 0 | 1 |
| `b.wave` | 0 | 0 | 11 |
| `b.coarse` | 0 | 1 | 48 |
| `b.fine` | 0 | 0 | 1000 |
| `b.detune` | -50 | 0 | 50 |
| `b.fixed` | 0 | 0 | 1 |
| `b.freq` | 1 | 440 | 10000 |
| `b.level` | -48 | -14 | 0 |
| `b.velocity` | 0 | 0.699999988 | 1 |
| `b.keyScale` | -1 | 0 | 1 |
| `b.lfo` | 0 | 0 | 1 |
| `b.attack` | 0.00100000005 | 0.00200000009 | 20 |
| `b.decay` | 0.00499999989 | 1.20000005 | 20 |
| `b.sustain` | 0 | 0.25 | 1 |
| `b.release` | 0.00499999989 | 0.5 | 20 |
| `b.loop` | 0 | 0 | 1 |
| `c.wave` | 0 | 0 | 11 |
| `c.coarse` | 0 | 3 | 48 |
| `c.fine` | 0 | 0 | 1000 |
| `c.detune` | -50 | 0 | 50 |
| `c.fixed` | 0 | 0 | 1 |
| `c.freq` | 1 | 440 | 10000 |
| `c.level` | -48 | -48 | 0 |
| `c.velocity` | 0 | 0.5 | 1 |
| `c.keyScale` | -1 | 0 | 1 |
| `c.lfo` | 0 | 0 | 1 |
| `c.attack` | 0.00100000005 | 0.00200000009 | 20 |
| `c.decay` | 0.00499999989 | 1 | 20 |
| `c.sustain` | 0 | 0.5 | 1 |
| `c.release` | 0.00499999989 | 0.400000006 | 20 |
| `c.loop` | 0 | 0 | 1 |
| `d.wave` | 0 | 0 | 11 |
| `d.coarse` | 0 | 1 | 48 |
| `d.fine` | 0 | 0 | 1000 |
| `d.detune` | -50 | 0 | 50 |
| `d.fixed` | 0 | 0 | 1 |
| `d.freq` | 1 | 440 | 10000 |
| `d.level` | -48 | -48 | 0 |
| `d.velocity` | 0 | 0.5 | 1 |
| `d.keyScale` | -1 | 0 | 1 |
| `d.lfo` | 0 | 0 | 1 |
| `d.attack` | 0.00100000005 | 0.00200000009 | 20 |
| `d.decay` | 0.00499999989 | 1 | 20 |
| `d.sustain` | 0 | 0.5 | 1 |
| `d.release` | 0.00499999989 | 0.400000006 | 20 |
| `d.loop` | 0 | 0 | 1 |
| `f.type` | 0 | 1 | 6 |
| `f.cutoff` | 20 | 10000 | 20000 |
| `f.resonance` | 0 | 0.100000001 | 1 |
| `filter.drive` | 0 | 0 | 1 |
| `filter.env` | -1 | 0 | 1 |
| `filter.key` | 0 | 0 | 1 |
| `filter.velocity` | 0 | 0 | 1 |
| `fenv.attack` | 0.00100000005 | 0.00200000009 | 20 |
| `fenv.decay` | 0.00499999989 | 0.600000024 | 20 |
| `fenv.sustain` | 0 | 0.300000012 | 1 |
| `fenv.release` | 0.00499999989 | 0.400000006 | 20 |
| `penv.amount` | -48 | 0 | 48 |
| `penv.attack` | 0.00100000005 | 0.00100000005 | 10 |
| `penv.decay` | 0.00499999989 | 0.150000006 | 10 |
| `lfo1.shape` | 0 | 0 | 4 |
| `lfo1.rate` | 0.0500000007 | 5 | 30 |
| `lfo1.sync` | 0 | 0 | 12 |
| `lfo1.delay` | 0 | 0 | 5 |
| `lfo1.amount` | 0 | 1 | 1 |
| `lfo1.wheel` | 0 | 0 | 1 |
| `lfo1.pitch` | 0 | 0 | 2 |
| `lfo1.cutoff` | 0 | 0 | 1 |
| `tone` | -1 | 0 | 1 |
| `spread` | 0 | 0.200000003 | 1 |
| `transpose` | -24 | 0 | 24 |
| `time` | 0.25 | 1 | 4 |
| `time.key` | 0 | 0.25 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| algorithm / `algorithm` | Entrée | value | 1 |
| feedback / `feedback` | Entrée | value | 1 |
| a.wave / `a.wave` | Entrée | value | 1 |
| a.coarse / `a.coarse` | Entrée | value | 1 |
| a.fine / `a.fine` | Entrée | value | 1 |
| a.detune / `a.detune` | Entrée | value | 1 |
| a.fixed / `a.fixed` | Entrée | value | 1 |
| a.freq / `a.freq` | Entrée | value | 1 |
| a.level / `a.level` | Entrée | value | 1 |
| a.velocity / `a.velocity` | Entrée | value | 1 |
| a.keyScale / `a.keyScale` | Entrée | value | 1 |
| a.lfo / `a.lfo` | Entrée | value | 1 |
| a.attack / `a.attack` | Entrée | value | 1 |
| a.decay / `a.decay` | Entrée | value | 1 |
| a.sustain / `a.sustain` | Entrée | value | 1 |
| a.release / `a.release` | Entrée | value | 1 |
| a.loop / `a.loop` | Entrée | value | 1 |
| b.wave / `b.wave` | Entrée | value | 1 |
| b.coarse / `b.coarse` | Entrée | value | 1 |
| b.fine / `b.fine` | Entrée | value | 1 |
| b.detune / `b.detune` | Entrée | value | 1 |
| b.fixed / `b.fixed` | Entrée | value | 1 |
| b.freq / `b.freq` | Entrée | value | 1 |
| b.level / `b.level` | Entrée | value | 1 |
| b.velocity / `b.velocity` | Entrée | value | 1 |
| b.keyScale / `b.keyScale` | Entrée | value | 1 |
| b.lfo / `b.lfo` | Entrée | value | 1 |
| b.attack / `b.attack` | Entrée | value | 1 |
| b.decay / `b.decay` | Entrée | value | 1 |
| b.sustain / `b.sustain` | Entrée | value | 1 |
| b.release / `b.release` | Entrée | value | 1 |
| b.loop / `b.loop` | Entrée | value | 1 |
| c.wave / `c.wave` | Entrée | value | 1 |
| c.coarse / `c.coarse` | Entrée | value | 1 |
| c.fine / `c.fine` | Entrée | value | 1 |
| c.detune / `c.detune` | Entrée | value | 1 |
| c.fixed / `c.fixed` | Entrée | value | 1 |
| c.freq / `c.freq` | Entrée | value | 1 |
| c.level / `c.level` | Entrée | value | 1 |
| c.velocity / `c.velocity` | Entrée | value | 1 |
| c.keyScale / `c.keyScale` | Entrée | value | 1 |
| c.lfo / `c.lfo` | Entrée | value | 1 |
| c.attack / `c.attack` | Entrée | value | 1 |
| c.decay / `c.decay` | Entrée | value | 1 |
| c.sustain / `c.sustain` | Entrée | value | 1 |
| c.release / `c.release` | Entrée | value | 1 |
| c.loop / `c.loop` | Entrée | value | 1 |
| d.wave / `d.wave` | Entrée | value | 1 |
| d.coarse / `d.coarse` | Entrée | value | 1 |
| d.fine / `d.fine` | Entrée | value | 1 |
| d.detune / `d.detune` | Entrée | value | 1 |
| d.fixed / `d.fixed` | Entrée | value | 1 |
| d.freq / `d.freq` | Entrée | value | 1 |
| d.level / `d.level` | Entrée | value | 1 |
| d.velocity / `d.velocity` | Entrée | value | 1 |
| d.keyScale / `d.keyScale` | Entrée | value | 1 |
| d.lfo / `d.lfo` | Entrée | value | 1 |
| d.attack / `d.attack` | Entrée | value | 1 |
| d.decay / `d.decay` | Entrée | value | 1 |
| d.sustain / `d.sustain` | Entrée | value | 1 |
| d.release / `d.release` | Entrée | value | 1 |
| d.loop / `d.loop` | Entrée | value | 1 |
| f.type / `f.type` | Entrée | value | 1 |
| f.cutoff / `f.cutoff` | Entrée | value | 1 |
| f.resonance / `f.resonance` | Entrée | value | 1 |
| filter.drive / `filter.drive` | Entrée | value | 1 |
| filter.env / `filter.env` | Entrée | value | 1 |
| filter.key / `filter.key` | Entrée | value | 1 |
| filter.velocity / `filter.velocity` | Entrée | value | 1 |
| fenv.attack / `fenv.attack` | Entrée | value | 1 |
| fenv.decay / `fenv.decay` | Entrée | value | 1 |
| fenv.sustain / `fenv.sustain` | Entrée | value | 1 |
| fenv.release / `fenv.release` | Entrée | value | 1 |
| penv.amount / `penv.amount` | Entrée | value | 1 |
| penv.attack / `penv.attack` | Entrée | value | 1 |
| penv.decay / `penv.decay` | Entrée | value | 1 |
| lfo1.shape / `lfo1.shape` | Entrée | value | 1 |
| lfo1.rate / `lfo1.rate` | Entrée | value | 1 |
| lfo1.sync / `lfo1.sync` | Entrée | value | 1 |
| lfo1.delay / `lfo1.delay` | Entrée | value | 1 |
| lfo1.amount / `lfo1.amount` | Entrée | value | 1 |
| lfo1.wheel / `lfo1.wheel` | Entrée | value | 1 |
| lfo1.pitch / `lfo1.pitch` | Entrée | value | 1 |
| lfo1.cutoff / `lfo1.cutoff` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| spread / `spread` | Entrée | value | 1 |
| transpose / `transpose` | Entrée | value | 1 |
| time / `time` | Entrée | value | 1 |
| time.key / `time.key` | Entrée | value | 1 |

## SILEX (silex-synth) {#silex-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.699999988 | 1 |
| `kick.tune` | 30 | 52 | 120 |
| `kick.sweep` | 0 | 24 | 48 |
| `kick.time` | 0.00499999989 | 0.0350000001 | 0.5 |
| `kick.decay` | 0.0500000007 | 0.5 | 4 |
| `kick.click` | 0 | 0.349999994 | 1 |
| `kick.drive` | 0 | 0.150000006 | 1 |
| `kick.level` | 0 | 0.899999976 | 1 |
| `kick.pan` | -1 | 0 | 1 |
| `snare.tune` | 100 | 185 | 400 |
| `snare.tone` | 0 | 0.5 | 1 |
| `snare.snappy` | 0 | 0.600000024 | 1 |
| `snare.colour` | 0 | 0.5 | 1 |
| `snare.decay` | 0.0500000007 | 0.25 | 1.5 |
| `snare.level` | 0 | 0.75 | 1 |
| `snare.pan` | -1 | 0 | 1 |
| `clap.spread` | 0.00300000003 | 0.0109999999 | 0.0299999993 |
| `clap.tone` | 500 | 1150 | 5000 |
| `clap.decay` | 0.0500000007 | 0.300000012 | 2 |
| `clap.level` | 0 | 0.699999988 | 1 |
| `clap.pan` | -1 | 0 | 1 |
| `hat.tune` | -12 | 0 | 12 |
| `hat.metal` | 0 | 0.649999976 | 1 |
| `hat.tone` | 0 | 0.5 | 1 |
| `hat.closed` | 0.00999999978 | 0.0700000003 | 0.5 |
| `hat.open` | 0.0799999982 | 0.5 | 3 |
| `hat.level` | 0 | 0.550000012 | 1 |
| `hat.pan` | -1 | 0 | 1 |
| `tom.tune` | 50 | 100 | 300 |
| `tom.drop` | 0 | 5 | 24 |
| `tom.decay` | 0.0799999982 | 0.550000012 | 3 |
| `tom.tone` | 0 | 0.300000012 | 1 |
| `tom.level` | 0 | 0.699999988 | 1 |
| `tom.pan` | -1 | 0 | 1 |
| `tom.spread` | 0 | 0.5 | 1 |
| `crash.tune` | -12 | 0 | 12 |
| `crash.tone` | 0 | 0.5 | 1 |
| `crash.decay` | 0.300000012 | 2 | 8 |
| `crash.level` | 0 | 0.5 | 1 |
| `crash.pan` | -1 | 0 | 1 |
| `ride.tune` | -12 | 0 | 12 |
| `ride.tone` | 0 | 0.5 | 1 |
| `ride.bell` | 0 | 0.349999994 | 1 |
| `ride.decay` | 0.300000012 | 2.5 | 8 |
| `ride.level` | 0 | 0.449999988 | 1 |
| `ride.pan` | -1 | 0 | 1 |
| `rim.tune` | 200 | 480 | 1200 |
| `rim.decay` | 0.00999999978 | 0.0399999991 | 0.300000012 |
| `rim.level` | 0 | 0.600000024 | 1 |
| `rim.pan` | -1 | 0 | 1 |
| `cowbell.tune` | -12 | 0 | 12 |
| `cowbell.decay` | 0.0500000007 | 0.300000012 | 1.5 |
| `cowbell.level` | 0 | 0.5 | 1 |
| `cowbell.pan` | -1 | 0 | 1 |
| `clave.tune` | 1000 | 2500 | 4000 |
| `clave.decay` | 0.00999999978 | 0.0599999987 | 0.400000006 |
| `clave.level` | 0 | 0.550000012 | 1 |
| `clave.pan` | -1 | 0 | 1 |
| `shaker.tone` | 0 | 0.5 | 1 |
| `shaker.decay` | 0.0199999996 | 0.0700000003 | 0.5 |
| `shaker.level` | 0 | 0.5 | 1 |
| `shaker.pan` | -1 | 0 | 1 |
| `kit.character` | 0 | 0 | 2 |
| `kit.tune` | -12 | 0 | 12 |
| `kit.decay` | 0.25 | 1 | 4 |
| `kit.velocity` | 0 | 0.600000024 | 1 |
| `master.drive` | 0 | 0 | 1 |
| `master.tone` | -1 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| kick.tune / `kick.tune` | Entrée | value | 1 |
| kick.sweep / `kick.sweep` | Entrée | value | 1 |
| kick.time / `kick.time` | Entrée | value | 1 |
| kick.decay / `kick.decay` | Entrée | value | 1 |
| kick.click / `kick.click` | Entrée | value | 1 |
| kick.drive / `kick.drive` | Entrée | value | 1 |
| kick.level / `kick.level` | Entrée | value | 1 |
| kick.pan / `kick.pan` | Entrée | value | 1 |
| snare.tune / `snare.tune` | Entrée | value | 1 |
| snare.tone / `snare.tone` | Entrée | value | 1 |
| snare.snappy / `snare.snappy` | Entrée | value | 1 |
| snare.colour / `snare.colour` | Entrée | value | 1 |
| snare.decay / `snare.decay` | Entrée | value | 1 |
| snare.level / `snare.level` | Entrée | value | 1 |
| snare.pan / `snare.pan` | Entrée | value | 1 |
| clap.spread / `clap.spread` | Entrée | value | 1 |
| clap.tone / `clap.tone` | Entrée | value | 1 |
| clap.decay / `clap.decay` | Entrée | value | 1 |
| clap.level / `clap.level` | Entrée | value | 1 |
| clap.pan / `clap.pan` | Entrée | value | 1 |
| hat.tune / `hat.tune` | Entrée | value | 1 |
| hat.metal / `hat.metal` | Entrée | value | 1 |
| hat.tone / `hat.tone` | Entrée | value | 1 |
| hat.closed / `hat.closed` | Entrée | value | 1 |
| hat.open / `hat.open` | Entrée | value | 1 |
| hat.level / `hat.level` | Entrée | value | 1 |
| hat.pan / `hat.pan` | Entrée | value | 1 |
| tom.tune / `tom.tune` | Entrée | value | 1 |
| tom.drop / `tom.drop` | Entrée | value | 1 |
| tom.decay / `tom.decay` | Entrée | value | 1 |
| tom.tone / `tom.tone` | Entrée | value | 1 |
| tom.level / `tom.level` | Entrée | value | 1 |
| tom.pan / `tom.pan` | Entrée | value | 1 |
| tom.spread / `tom.spread` | Entrée | value | 1 |
| crash.tune / `crash.tune` | Entrée | value | 1 |
| crash.tone / `crash.tone` | Entrée | value | 1 |
| crash.decay / `crash.decay` | Entrée | value | 1 |
| crash.level / `crash.level` | Entrée | value | 1 |
| crash.pan / `crash.pan` | Entrée | value | 1 |
| ride.tune / `ride.tune` | Entrée | value | 1 |
| ride.tone / `ride.tone` | Entrée | value | 1 |
| ride.bell / `ride.bell` | Entrée | value | 1 |
| ride.decay / `ride.decay` | Entrée | value | 1 |
| ride.level / `ride.level` | Entrée | value | 1 |
| ride.pan / `ride.pan` | Entrée | value | 1 |
| rim.tune / `rim.tune` | Entrée | value | 1 |
| rim.decay / `rim.decay` | Entrée | value | 1 |
| rim.level / `rim.level` | Entrée | value | 1 |
| rim.pan / `rim.pan` | Entrée | value | 1 |
| cowbell.tune / `cowbell.tune` | Entrée | value | 1 |
| cowbell.decay / `cowbell.decay` | Entrée | value | 1 |
| cowbell.level / `cowbell.level` | Entrée | value | 1 |
| cowbell.pan / `cowbell.pan` | Entrée | value | 1 |
| clave.tune / `clave.tune` | Entrée | value | 1 |
| clave.decay / `clave.decay` | Entrée | value | 1 |
| clave.level / `clave.level` | Entrée | value | 1 |
| clave.pan / `clave.pan` | Entrée | value | 1 |
| shaker.tone / `shaker.tone` | Entrée | value | 1 |
| shaker.decay / `shaker.decay` | Entrée | value | 1 |
| shaker.level / `shaker.level` | Entrée | value | 1 |
| shaker.pan / `shaker.pan` | Entrée | value | 1 |
| kit.character / `kit.character` | Entrée | value | 1 |
| kit.tune / `kit.tune` | Entrée | value | 1 |
| kit.decay / `kit.decay` | Entrée | value | 1 |
| kit.velocity / `kit.velocity` | Entrée | value | 1 |
| master.drive / `master.drive` | Entrée | value | 1 |
| master.tone / `master.tone` | Entrée | value | 1 |

## SOUND MODEL (sound-model) {#sound-model}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `harmonics` | 0 | 1 | 2 |
| `noise` | 0 | 1 | 2 |
| `attack` | 0 | 1 | 2 |
| `brightness` | -24 | 0 | 24 |
| `formant` | 0 | 0 | 1 |
| `inharmonicity` | 0 | 1 | 4 |
| `pitch-curve` | 0 | 1 | 2 |
| `speed` | 0.25 | 1 | 4 |
| `release` | -1 | -1 | 10 |
| `noise-pitch` | 0 | 0 | 1 |
| `attack-pitch` | 0 | 1 | 1 |
| `velocity-db` | 0 | 24 | 48 |
| `velocity-brightness` | -12 | 3 | 12 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| harmonics / `harmonics` | Entrée | value | 1 |
| noise / `noise` | Entrée | value | 1 |
| attack / `attack` | Entrée | value | 1 |
| brightness / `brightness` | Entrée | value | 1 |
| formant / `formant` | Entrée | value | 1 |
| inharmonicity / `inharmonicity` | Entrée | value | 1 |
| pitch-curve / `pitch-curve` | Entrée | value | 1 |
| speed / `speed` | Entrée | value | 1 |
| release / `release` | Entrée | value | 1 |
| noise-pitch / `noise-pitch` | Entrée | value | 1 |
| attack-pitch / `attack-pitch` | Entrée | value | 1 |
| velocity-db / `velocity-db` | Entrée | value | 1 |
| velocity-brightness / `velocity-brightness` | Entrée | value | 1 |

## STRING (string) {#string}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 4000 |
| `damping` | 0 | 0.35 | 1 |
| `decay` | 0.05 | 4 | 20 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Entrée | audio | 2 |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| damping / `damping` | Entrée | value | 1 |
| decay / `decay` | Entrée | value | 1 |

## TIRETTE (tirette-synth) {#tirette-synth}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `level` | 0 | 1 | 1 |
| `voice.mode` | 0 | 0 | 2 |
| `voice.count` | 1 | 16 | 32 |
| `voice.glide` | 0 | 0 | 2 |
| `voice.glideAlways` | 0 | 0 | 1 |
| `voice.bend` | 0 | 2 | 24 |
| `voice.master` | 0 | 0.600000024 | 1 |
| `bar1` | 0 | 8 | 8 |
| `bar2` | 0 | 8 | 8 |
| `bar3` | 0 | 8 | 8 |
| `bar4` | 0 | 0 | 8 |
| `bar5` | 0 | 0 | 8 |
| `bar6` | 0 | 0 | 8 |
| `bar7` | 0 | 0 | 8 |
| `bar8` | 0 | 0 | 8 |
| `bar9` | 0 | 0 | 8 |
| `organ.type` | 0 | 0 | 2 |
| `leakage` | 0 | 0.200000003 | 1 |
| `click` | 0 | 0.400000006 | 1 |
| `perc.on` | 0 | 0 | 1 |
| `perc.harmonic` | 0 | 1 | 1 |
| `perc.decay` | 0 | 0 | 1 |
| `perc.volume` | 0 | 0 | 1 |
| `vibrato.mode` | 0 | 6 | 6 |
| `vibrato.rate` | 4 | 6.9000001 | 9 |
| `drive.amount` | 0 | 0.150000006 | 1 |
| `tone` | 0 | 0.75 | 1 |
| `drive.output` | -18 | 0 | 6 |
| `rotary.mode` | 0 | 1 | 2 |
| `rotary.wheel` | 0 | 1 | 1 |
| `rotary.accel` | 0.100000001 | 0.800000012 | 10 |
| `rotary.balance` | 0 | 0.5 | 1 |
| `rotary.depth` | 0 | 0.600000024 | 1 |
| `rotary.width` | 0 | 0.800000012 | 1 |
| `rotary.mix` | 0 | 1 | 1 |
| `amp.attack` | 0.00100000005 | 0.00100000005 | 5 |
| `amp.release` | 0.00499999989 | 0.0149999997 | 8 |
| `amp.velocity` | 0 | 0 | 1 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| voice.mode / `voice.mode` | Entrée | value | 1 |
| voice.count / `voice.count` | Entrée | value | 1 |
| voice.glide / `voice.glide` | Entrée | value | 1 |
| voice.glideAlways / `voice.glideAlways` | Entrée | value | 1 |
| voice.bend / `voice.bend` | Entrée | value | 1 |
| voice.master / `voice.master` | Entrée | value | 1 |
| bar1 / `bar1` | Entrée | value | 1 |
| bar2 / `bar2` | Entrée | value | 1 |
| bar3 / `bar3` | Entrée | value | 1 |
| bar4 / `bar4` | Entrée | value | 1 |
| bar5 / `bar5` | Entrée | value | 1 |
| bar6 / `bar6` | Entrée | value | 1 |
| bar7 / `bar7` | Entrée | value | 1 |
| bar8 / `bar8` | Entrée | value | 1 |
| bar9 / `bar9` | Entrée | value | 1 |
| organ.type / `organ.type` | Entrée | value | 1 |
| leakage / `leakage` | Entrée | value | 1 |
| click / `click` | Entrée | value | 1 |
| perc.on / `perc.on` | Entrée | value | 1 |
| perc.harmonic / `perc.harmonic` | Entrée | value | 1 |
| perc.decay / `perc.decay` | Entrée | value | 1 |
| perc.volume / `perc.volume` | Entrée | value | 1 |
| vibrato.mode / `vibrato.mode` | Entrée | value | 1 |
| vibrato.rate / `vibrato.rate` | Entrée | value | 1 |
| drive.amount / `drive.amount` | Entrée | value | 1 |
| tone / `tone` | Entrée | value | 1 |
| drive.output / `drive.output` | Entrée | value | 1 |
| rotary.mode / `rotary.mode` | Entrée | value | 1 |
| rotary.wheel / `rotary.wheel` | Entrée | value | 1 |
| rotary.accel / `rotary.accel` | Entrée | value | 1 |
| rotary.balance / `rotary.balance` | Entrée | value | 1 |
| rotary.depth / `rotary.depth` | Entrée | value | 1 |
| rotary.width / `rotary.width` | Entrée | value | 1 |
| rotary.mix / `rotary.mix` | Entrée | value | 1 |
| amp.attack / `amp.attack` | Entrée | value | 1 |
| amp.release / `amp.release` | Entrée | value | 1 |
| amp.velocity / `amp.velocity` | Entrée | value | 1 |

## VOICE ALLOCATOR (voice-allocator) {#voice-allocator}

Catégorie : DSP / Instruments.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `voices` | 1 | 16 | 64 |
| `mode` | 0 | 0 | 2 |
| `glide` | 0 | 0 | 2000 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| midi / `midi` | Entrée | midi | 1 |
| pitch / `pitch` | Sortie | value | 1 |
| gate / `gate` | Sortie | trigger | 1 |
| velocity / `velocity` | Sortie | value | 1 |

## VOICE FILTER (voice-filter) {#voice-filter}

Catégorie : DSP / Instruments.

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

## WAVETABLE (wavetable) {#wavetable}

Catégorie : DSP / Oscillateurs et générateurs.

| Clé moteur | Minimum | Défaut | Maximum |
| --- | --- | --- | --- |
| `frequency` | 20 | 220 | 8000 |
| `position` | 0 | 0.35 | 1 |
| `level` | 0 | 0.2 | 1 |
| `detune` | 0 | 8 | 50 |

| Port / clé | Sens | Type | Canaux |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Sortie | audio | 2 |
| frequency / `frequency` | Entrée | value | 1 |
| position / `position` | Entrée | value | 1 |
| level / `level` | Entrée | value | 1 |
| detune / `detune` | Entrée | value | 1 |
