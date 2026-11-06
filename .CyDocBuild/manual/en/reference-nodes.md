---
id: reference-nodes
title: MIDI, control and container reference
description: Look up MIDI, control and container node settings.
section: Manual / Reference
parent: graphs
order: 39
revision: 304dab179f130caa
---

## Overview {#overview}

Generated from the CyDAW 0.78.0 registry. Each section describes a native type, not a track preset or external plugin. Search to find an engine key, range or port.


~~~image
{"id": "figure-graph", "src": "assets/graph-en.png", "alt": "The graph connects typed ports and their parameters.", "title": "The graph connects typed ports and their parameters.", "controls": "minimal"}
~~~

*The graph connects typed ports and their parameters.*


Numbers are raw engine-schema values. Display units, logarithmic scales, choices and file/table controls belong to the module panel and cannot be inferred from these numbers. Defaults describe a fresh node, not necessarily the loaded preset. A declared port does not guarantee an exportable plugin boundary.

See the [glossary](doc:glossary#reference), [instruments](doc:synths) and [effects](doc:studio-effects) for listening workflows. For limits read [export](doc:plugin-formats#limits).

## CHORDS (accords) {#accords}

Category: MIDI primitives.

Turns a note into a chord: major, minor, fifth, sus, sevenths, ninths, octaves or six intervals of your own; inversion, open voicing, added notes' velocity, notes strummed up or down, or chords that follow a scale.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `velocity` | 0 | 0.850000024 | 1 |
| `strum` | 0 | 0 | 200 |
| `chord-type` | 0 | 0 | 11 |
| `interval-1` | -24 | 4 | 24 |
| `interval-2` | -24 | 7 | 24 |
| `interval-3` | -24 | 0 | 24 |
| `interval-4` | -24 | 0 | 24 |
| `interval-5` | -24 | 0 | 24 |
| `interval-6` | -24 | 0 | 24 |
| `inversion` | -3 | 0 | 3 |
| `spread` | 0 | 0 | 1 |
| `strum-mode` | 0 | 0 | 2 |
| `key` | 0 | 0 | 11 |
| `scale` | 0 | 0 | 11 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## RANDOM (alea) {#alea}

Category: MIDI primitives.

Makes the playing human or unpredictable: a little random delay, varying velocities, notes that do not always play, or that change pitch now and then.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `timing` | 0 | 0 | 50 |
| `velocity` | 0 | 0.150000006 | 1 |
| `chance` | 0 | 1 | 1 |
| `pitch-chance` | 0 | 0 | 1 |
| `pitch-range` | 0 | 0 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## ARPEGGIATOR (arpege) {#arpege}

Category: MIDI primitives.

Plays the held notes one after another in time: up, down, up and down, in the order played, at random, as a chord, converging or diverging; over one to four octaves, at the chosen step, with swing, gate, velocity as played, fixed or accented, latch.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `gate` | 0.0500000007 | 0.600000024 | 2 |
| `swing` | 0 | 0 | 0.75 |
| `velocity` | 1 | 100 | 127 |
| `rate-sync` | 1 | 3 | 12 |
| `mode` | 0 | 0 | 8 |
| `octaves` | 1 | 1 | 4 |
| `velocity-mode` | 0 | 0 | 2 |
| `latch` | 0 | 0 | 1 |
| `retrigger` | 0 | 1 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## CONTROL MATH (control-math) {#control-math}

Category: Value generators / primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `amount` | 0 | 1 | 1 |
| `offset` | -1 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## DRUM MACHINE (drum-rack) {#drum-rack}

Category: Containers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Output | audio | 2 |
| frequency / `frequency` | Input | value | 1 |
| level / `level` | Input | value | 1 |

## NOTE LENGTH (duree) {#duree}

Category: MIDI primitives.

Gives the notes the chosen length, whatever was played: a fixed time, a note value, a stretched length, or until the next note (legato).

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `length` | 10 | 250 | 4000 |
| `factor` | 1 | 1.5 | 4 |
| `mode` | 0 | 0 | 3 |
| `length-sync` | 1 | 6 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## NOTE ECHO (echo-notes) {#echo-notes}

Category: MIDI primitives.

Repeats each note like an echo, as notes: at the chosen time or note value, as many times as wanted, each echo softer and, if wanted, higher or lower.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `time` | 10 | 250 | 2000 |
| `decay` | 0 | 0.300000012 | 1 |
| `pitch` | -12 | 0 | 12 |
| `sync` | 0 | 6 | 12 |
| `repeats` | 1 | 3 | 16 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## ENVELOPE FOLLOWER (envelope-follower) {#envelope-follower}

Category: Visualization / Analyzers.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `attack` | 0.0001 | 0.01 | 1 |
| `release` | 0.001 | 0.1 | 5 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| envelope / `envelope` | Output | value | 1 |

## CHAIN (fx-chain) {#fx-chain}

Category: Containers.

A chain of devices in one box, blended with what came in by Mix.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |

## FX LAYER (fx-layer) {#fx-layer}

Category: Containers.

Chains side by side, each hearing the whole sound, summed: a volume, a pan, solo and mute each.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `chains` | 1 | 2 | 8 |
| `volume1` | -60 | 0 | 12 |
| `pan1` | -1 | 0 | 1 |
| `mute1` | 0 | 0 | 1 |
| `solo1` | 0 | 0 | 1 |
| `volume2` | -60 | 0 | 12 |
| `pan2` | -1 | 0 | 1 |
| `mute2` | 0 | 0 | 1 |
| `solo2` | 0 | 0 | 1 |
| `volume3` | -60 | 0 | 12 |
| `pan3` | -1 | 0 | 1 |
| `mute3` | 0 | 0 | 1 |
| `solo3` | 0 | 0 | 1 |
| `volume4` | -60 | 0 | 12 |
| `pan4` | -1 | 0 | 1 |
| `mute4` | 0 | 0 | 1 |
| `solo4` | 0 | 0 | 1 |
| `volume5` | -60 | 0 | 12 |
| `pan5` | -1 | 0 | 1 |
| `mute5` | 0 | 0 | 1 |
| `solo5` | 0 | 0 | 1 |
| `volume6` | -60 | 0 | 12 |
| `pan6` | -1 | 0 | 1 |
| `mute6` | 0 | 0 | 1 |
| `solo6` | 0 | 0 | 1 |
| `volume7` | -60 | 0 | 12 |
| `pan7` | -1 | 0 | 1 |
| `mute7` | 0 | 0 | 1 |
| `solo7` | 0 | 0 | 1 |
| `volume8` | -60 | 0 | 12 |
| `pan8` | -1 | 0 | 1 |
| `mute8` | 0 | 0 | 1 |
| `solo8` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| chains / `chains` | Input | value | 1 |
| volume1 / `volume1` | Input | value | 1 |
| pan1 / `pan1` | Input | value | 1 |
| mute1 / `mute1` | Input | value | 1 |
| solo1 / `solo1` | Input | value | 1 |
| volume2 / `volume2` | Input | value | 1 |
| pan2 / `pan2` | Input | value | 1 |
| mute2 / `mute2` | Input | value | 1 |
| solo2 / `solo2` | Input | value | 1 |
| volume3 / `volume3` | Input | value | 1 |
| pan3 / `pan3` | Input | value | 1 |
| mute3 / `mute3` | Input | value | 1 |
| solo3 / `solo3` | Input | value | 1 |
| volume4 / `volume4` | Input | value | 1 |
| pan4 / `pan4` | Input | value | 1 |
| mute4 / `mute4` | Input | value | 1 |
| solo4 / `solo4` | Input | value | 1 |
| volume5 / `volume5` | Input | value | 1 |
| pan5 / `pan5` | Input | value | 1 |
| mute5 / `mute5` | Input | value | 1 |
| solo5 / `solo5` | Input | value | 1 |
| volume6 / `volume6` | Input | value | 1 |
| pan6 / `pan6` | Input | value | 1 |
| mute6 / `mute6` | Input | value | 1 |
| solo6 / `solo6` | Input | value | 1 |
| volume7 / `volume7` | Input | value | 1 |
| pan7 / `pan7` | Input | value | 1 |
| mute7 / `mute7` | Input | value | 1 |
| solo7 / `solo7` | Input | value | 1 |
| volume8 / `volume8` | Input | value | 1 |
| pan8 / `pan8` | Input | value | 1 |
| mute8 / `mute8` | Input | value | 1 |
| solo8 / `solo8` | Input | value | 1 |

## FX SELECTOR (fx-selector) {#fx-selector}

Category: Containers.

Several chains, one heard at a time, faded from one to the other.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `chains` | 1 | 2 | 8 |
| `selected` | 0 | 0 | 7 |
| `fade` | 0 | 20 | 500 |
| `volume1` | -60 | 0 | 12 |
| `pan1` | -1 | 0 | 1 |
| `mute1` | 0 | 0 | 1 |
| `solo1` | 0 | 0 | 1 |
| `volume2` | -60 | 0 | 12 |
| `pan2` | -1 | 0 | 1 |
| `mute2` | 0 | 0 | 1 |
| `solo2` | 0 | 0 | 1 |
| `volume3` | -60 | 0 | 12 |
| `pan3` | -1 | 0 | 1 |
| `mute3` | 0 | 0 | 1 |
| `solo3` | 0 | 0 | 1 |
| `volume4` | -60 | 0 | 12 |
| `pan4` | -1 | 0 | 1 |
| `mute4` | 0 | 0 | 1 |
| `solo4` | 0 | 0 | 1 |
| `volume5` | -60 | 0 | 12 |
| `pan5` | -1 | 0 | 1 |
| `mute5` | 0 | 0 | 1 |
| `solo5` | 0 | 0 | 1 |
| `volume6` | -60 | 0 | 12 |
| `pan6` | -1 | 0 | 1 |
| `mute6` | 0 | 0 | 1 |
| `solo6` | 0 | 0 | 1 |
| `volume7` | -60 | 0 | 12 |
| `pan7` | -1 | 0 | 1 |
| `mute7` | 0 | 0 | 1 |
| `solo7` | 0 | 0 | 1 |
| `volume8` | -60 | 0 | 12 |
| `pan8` | -1 | 0 | 1 |
| `mute8` | 0 | 0 | 1 |
| `solo8` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| chains / `chains` | Input | value | 1 |
| selected / `selected` | Input | value | 1 |
| fade / `fade` | Input | value | 1 |
| volume1 / `volume1` | Input | value | 1 |
| pan1 / `pan1` | Input | value | 1 |
| mute1 / `mute1` | Input | value | 1 |
| solo1 / `solo1` | Input | value | 1 |
| volume2 / `volume2` | Input | value | 1 |
| pan2 / `pan2` | Input | value | 1 |
| mute2 / `mute2` | Input | value | 1 |
| solo2 / `solo2` | Input | value | 1 |
| volume3 / `volume3` | Input | value | 1 |
| pan3 / `pan3` | Input | value | 1 |
| mute3 / `mute3` | Input | value | 1 |
| solo3 / `solo3` | Input | value | 1 |
| volume4 / `volume4` | Input | value | 1 |
| pan4 / `pan4` | Input | value | 1 |
| mute4 / `mute4` | Input | value | 1 |
| solo4 / `solo4` | Input | value | 1 |
| volume5 / `volume5` | Input | value | 1 |
| pan5 / `pan5` | Input | value | 1 |
| mute5 / `mute5` | Input | value | 1 |
| solo5 / `solo5` | Input | value | 1 |
| volume6 / `volume6` | Input | value | 1 |
| pan6 / `pan6` | Input | value | 1 |
| mute6 / `mute6` | Input | value | 1 |
| solo6 / `solo6` | Input | value | 1 |
| volume7 / `volume7` | Input | value | 1 |
| pan7 / `pan7` | Input | value | 1 |
| mute7 / `mute7` | Input | value | 1 |
| solo7 / `solo7` | Input | value | 1 |
| volume8 / `volume8` | Input | value | 1 |
| pan8 / `pan8` | Input | value | 1 |
| mute8 / `mute8` | Input | value | 1 |
| solo8 / `solo8` | Input | value | 1 |

## SCALE (gamme) {#gamme}

Category: MIDI primitives.

Brings the notes played into a key and a scale: to the nearest, above, below, or by removing those outside it; transposition in semitones or in degrees of the scale.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `transpose` | -24 | 0 | 24 |
| `degrees` | -7 | 0 | 7 |
| `key` | 0 | 0 | 11 |
| `scale` | 0 | 1 | 11 |
| `mode` | 0 | 0 | 3 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## INSTRUMENT LAYER (instrument-layer) {#instrument-layer}

Category: Containers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Output | audio | 2 |
| frequency / `frequency` | Input | value | 1 |
| level / `level` | Input | value | 1 |

## INSTRUMENT SELECTOR (instrument-selector) {#instrument-selector}

Category: Containers.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mode` | 0 | 0 | 5 |
| `layer` | 1 | 1 | 16 |
| `controller` | 0 | 1 | 127 |
| `keyswitch` | 0 | 24 | 127 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-out / `audio-out` | Output | audio | 2 |
| frequency / `frequency` | Input | value | 1 |
| level / `level` | Input | value | 1 |

## LEVEL METER (level-meter) {#level-meter}

Category: Visualization / Analyzers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| level / `level` | Output | other | 1 |

## LFO OSCILLATOR (lfo-core) {#lfo-core}

Category: Value generators / primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.001 | 1 | 100 |
| `shape` | 0 | 0 | 4 |
| `depth` | 0 | 1 | 1 |
| `offset` | -1 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| reset / `reset` | Input | trigger | 1 |
| value / `value` | Output | value | 1 |

## LOGIC AND (logic-and) {#logic-and}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | trigger | 1 |
| b / `b` | Input | trigger | 1 |
| result / `result` | Output | trigger | 1 |

## EQUAL (logic-equal) {#logic-equal}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| result / `result` | Output | trigger | 1 |

## GREATER THAN (logic-greater) {#logic-greater}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| result / `result` | Output | trigger | 1 |

## LESS THAN (logic-less) {#logic-less}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| result / `result` | Output | trigger | 1 |

## LOGIC NOT (logic-not) {#logic-not}

Category: Boolean / Logic.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | trigger | 1 |
| result / `result` | Output | trigger | 1 |

## LOGIC OR (logic-or) {#logic-or}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | trigger | 1 |
| b / `b` | Input | trigger | 1 |
| result / `result` | Output | trigger | 1 |

## LOGIC XOR (logic-xor) {#logic-xor}

Category: Boolean / Logic.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | trigger | 1 |
| b / `b` | Input | trigger | 1 |
| result / `result` | Output | trigger | 1 |

## MACRO (macro) {#macro}

Category: Value generators / primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `value` | -1 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Output | value | 1 |

## MAP RANGE (map-range) {#map-range}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `input-min` | -1 | 0 | 1 |
| `input-max` | -1 | 1 | 1 |
| `output-min` | -1000 | 0 | 1000 |
| `output-max` | -1000 | 1 | 1000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| mapped / `mapped` | Output | value | 1 |

## MID-SIDE SPLIT (mid-side-split) {#mid-side-split}

Category: Containers.

The middle of the stereo and its sides, each through its own chain, put back together.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `mid-gain` | -36 | 0 | 12 |
| `side-gain` | -36 | 0 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| mid-gain / `mid-gain` | Input | value | 1 |
| side-gain / `side-gain` | Input | value | 1 |

## MIDI MONITOR (midi-monitor) {#midi-monitor}

Category: Visualization / Analyzers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |
| events / `events` | Output | other | 1 |

## MIDI TRANSPOSE (midi-transpose) {#midi-transpose}

Category: MIDI primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `semitones` | -48 | 0 | 48 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## ADSR (mod-adsr) {#mod-adsr}

Category: Value generators / primitives.

An envelope the track's notes open and close: attack, decay, sustain, release.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `attack` | 0.00100000005 | 0.00999999978 | 10 |
| `decay` | 0.00100000005 | 0.300000012 | 10 |
| `sustain` | 0 | 0.600000024 | 1 |
| `release` | 0.00100000005 | 0.400000006 | 10 |
| `velocity` | 0 | 0 | 1 |
| `legato` | 0 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## BUTTON (mod-button) {#mod-button}

Category: Value generators / primitives.

A switch: on, it moves its knobs by their whole amount; off, not at all.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `on` | 0 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## EXPRESSIONS (mod-expression) {#mod-expression}

Category: Value generators / primitives.

The modulation wheel, aftertouch, pitch bend or any controller of the track.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `source` | 0 | 0 | 3 |
| `controller` | 0 | 74 | 127 |
| `smooth` | 0 | 0.100000001 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## ENVELOPE FOLLOWER (mod-follower) {#mod-follower}

Category: Value generators / primitives.

How loud its device sounds, as a value: the louder, the more it moves its knobs.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `attack` | 1 | 10 | 500 |
| `release` | 5 | 150 | 2000 |
| `gain` | -24 | 0 | 24 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## LFO (mod-lfo) {#mod-lfo}

Category: Value generators / primitives.

A low-frequency oscillator: a shape that cycles, free or on the tempo, restarted by notes if asked.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.00999999978 | 1 | 40 |
| `sync` | 0 | 0 | 13 |
| `shape` | 0 | 0 | 6 |
| `phase` | 0 | 0 | 1 |
| `retrigger` | 0 | 0 | 1 |
| `bipolar` | 0 | 1 | 1 |
| `smooth` | 0 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## MACRO (mod-macro) {#mod-macro}

Category: Value generators / primitives.

One knob that moves many: turn it, or automate it.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `value` | 0 | 0.5 | 1 |
| `bipolar` | 0 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## MATH (mod-math) {#mod-math}

Category: Value generators / primitives.

Two values combined: mixed, added, multiplied, the lower, the higher… Plug modulators into A and B.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mode` | 0 | 0 | 6 |
| `blend` | 0 | 0.5 | 1 |
| `a` | -1 | 0 | 1 |
| `b` | -1 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## KEYTRACK (mod-note) {#mod-note}

Category: Value generators / primitives.

The last note played: its key around a root, its velocity, or whether a key is held.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `source` | 0 | 0 | 2 |
| `root` | 0 | 60 | 127 |
| `range` | 1 | 48 | 127 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## RANDOM (mod-random) {#mod-random}

Category: Value generators / primitives.

A new random value each cycle (sample and hold), smoothed if asked.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `rate` | 0.0500000007 | 4 | 40 |
| `sync` | 0 | 6 | 13 |
| `smooth` | 0 | 0 | 1 |
| `bipolar` | 0 | 1 | 1 |
| `chance` | 0 | 1 | 1 |
| `retrigger` | 0 | 0 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## STEPS (mod-steps) {#mod-steps}

Category: Value generators / primitives.

A step sequence of values, one step per note value of the tempo, or at its own rate.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `steps` | 1 | 8 | 16 |
| `sync` | 0 | 6 | 13 |
| `rate` | 0.0500000007 | 4 | 40 |
| `smooth` | 0 | 0 | 1 |
| `bipolar` | 0 | 0 | 1 |
| `retrigger` | 0 | 0 | 1 |
| `step1` | 0 | 1 | 1 |
| `step2` | 0 | 0.25 | 1 |
| `step3` | 0 | 0.75 | 1 |
| `step4` | 0 | 0.5 | 1 |
| `step5` | 0 | 1 | 1 |
| `step6` | 0 | 0 | 1 |
| `step7` | 0 | 0.600000024 | 1 |
| `step8` | 0 | 0.300000012 | 1 |
| `step9` | 0 | 0.899999976 | 1 |
| `step10` | 0 | 0.200000003 | 1 |
| `step11` | 0 | 0.699999988 | 1 |
| `step12` | 0 | 0.400000006 | 1 |
| `step13` | 0 | 0.800000012 | 1 |
| `step14` | 0 | 0.100000001 | 1 |
| `step15` | 0 | 0.5 | 1 |
| `step16` | 0 | 0.349999994 | 1 |
| `owner-low` | 0 | 0 | 16777215 |
| `owner-high` | 0 | 0 | 16777215 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| value / `value` | Output | value | 1 |

## MULTIBAND FX-2 (multiband-2) {#multiband-2}

Category: Containers.

Lows and highs, split at a crossover, each through its own chain, summed back.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `crossover` | 40 | 400 | 16000 |
| `volume1` | -60 | 0 | 12 |
| `mute1` | 0 | 0 | 1 |
| `solo1` | 0 | 0 | 1 |
| `volume2` | -60 | 0 | 12 |
| `mute2` | 0 | 0 | 1 |
| `solo2` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| crossover / `crossover` | Input | value | 1 |
| volume1 / `volume1` | Input | value | 1 |
| mute1 / `mute1` | Input | value | 1 |
| solo1 / `solo1` | Input | value | 1 |
| volume2 / `volume2` | Input | value | 1 |
| mute2 / `mute2` | Input | value | 1 |
| solo2 / `solo2` | Input | value | 1 |

## MULTIBAND FX-3 (multiband-3) {#multiband-3}

Category: Containers.

Lows, mids and highs, split at two crossovers, each through its own chain.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `crossover-low` | 40 | 250 | 16000 |
| `crossover-high` | 40 | 2500 | 16000 |
| `volume1` | -60 | 0 | 12 |
| `mute1` | 0 | 0 | 1 |
| `solo1` | 0 | 0 | 1 |
| `volume2` | -60 | 0 | 12 |
| `mute2` | 0 | 0 | 1 |
| `solo2` | 0 | 0 | 1 |
| `volume3` | -60 | 0 | 12 |
| `mute3` | 0 | 0 | 1 |
| `solo3` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| crossover-low / `crossover-low` | Input | value | 1 |
| crossover-high / `crossover-high` | Input | value | 1 |
| volume1 / `volume1` | Input | value | 1 |
| mute1 / `mute1` | Input | value | 1 |
| solo1 / `solo1` | Input | value | 1 |
| volume2 / `volume2` | Input | value | 1 |
| mute2 / `mute2` | Input | value | 1 |
| solo2 / `solo2` | Input | value | 1 |
| volume3 / `volume3` | Input | value | 1 |
| mute3 / `mute3` | Input | value | 1 |
| solo3 / `solo3` | Input | value | 1 |

## NOTE FILTER (note-filter) {#note-filter}

Category: MIDI primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `minimum-note` | 0 | 0 | 127 |
| `maximum-note` | 0 | 127 | 127 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## OSCILLOSCOPE (oscilloscope) {#oscilloscope}

Category: Visualization / Analyzers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| waveform / `waveform` | Output | other | 1 |

## REPLACER (replacer) {#replacer}

Category: Containers.

Each transient of the sound replaced by a sound of its own (a kick, a tone, noise, a click), through its chain.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `threshold` | -60 | -24 | 0 |
| `sensitivity` | 1 | 6 | 24 |
| `hold` | 10 | 60 | 500 |
| `generator` | 0 | 0 | 4 |
| `pitch` | 20 | 55 | 2000 |
| `decay` | 5 | 250 | 2000 |
| `velocity` | 0 | 0.5 | 1 |
| `note` | 0 | 36 | 127 |
| `length` | 10 | 120 | 2000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| threshold / `threshold` | Input | value | 1 |
| sensitivity / `sensitivity` | Input | value | 1 |
| hold / `hold` | Input | value | 1 |
| generator / `generator` | Input | value | 1 |
| pitch / `pitch` | Input | value | 1 |
| decay / `decay` | Input | value | 1 |
| velocity / `velocity` | Input | value | 1 |
| note / `note` | Input | value | 1 |
| length / `length` | Input | value | 1 |

## NOTE REPEAT (roulement) {#roulement}

Category: MIDI primitives.

Repeats a note while it is held, at a note value or a free rate: snare rolls, fast hi-hats; hit length, velocity rising or falling.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `rate` | 1 | 8 | 40 |
| `gate` | 0.100000001 | 0.5 | 1 |
| `ramp` | -1 | 0 | 1 |
| `sync` | 0 | 3 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## STEP SEQUENCER (sequenceur) {#sequenceur}

Category: MIDI primitives.

A sixteen-step sequencer: each step on or off, its note, velocity and length; played while a key is held (and transposed by it) or with the transport; step, swing, direction (forward, backward, ping-pong, random).

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `swing` | 0 | 0 | 0.75 |
| `gate` | 0.0500000007 | 0.75 | 1 |
| `mode` | 0 | 0 | 1 |
| `steps` | 1 | 16 | 16 |
| `rate-sync` | 1 | 3 | 12 |
| `direction-mode` | 0 | 0 | 3 |
| `root` | 24 | 60 | 96 |
| `step-1-on` | 0 | 1 | 1 |
| `step-1-note` | -24 | 0 | 24 |
| `step-1-velocity` | 1 | 120 | 127 |
| `step-1-length` | 0.100000001 | 1 | 1 |
| `step-2-on` | 0 | 1 | 1 |
| `step-2-note` | -24 | 0 | 24 |
| `step-2-velocity` | 1 | 96 | 127 |
| `step-2-length` | 0.100000001 | 1 | 1 |
| `step-3-on` | 0 | 1 | 1 |
| `step-3-note` | -24 | 12 | 24 |
| `step-3-velocity` | 1 | 96 | 127 |
| `step-3-length` | 0.100000001 | 1 | 1 |
| `step-4-on` | 0 | 1 | 1 |
| `step-4-note` | -24 | 0 | 24 |
| `step-4-velocity` | 1 | 96 | 127 |
| `step-4-length` | 0.100000001 | 1 | 1 |
| `step-5-on` | 0 | 1 | 1 |
| `step-5-note` | -24 | 7 | 24 |
| `step-5-velocity` | 1 | 120 | 127 |
| `step-5-length` | 0.100000001 | 1 | 1 |
| `step-6-on` | 0 | 1 | 1 |
| `step-6-note` | -24 | 0 | 24 |
| `step-6-velocity` | 1 | 96 | 127 |
| `step-6-length` | 0.100000001 | 1 | 1 |
| `step-7-on` | 0 | 1 | 1 |
| `step-7-note` | -24 | 12 | 24 |
| `step-7-velocity` | 1 | 96 | 127 |
| `step-7-length` | 0.100000001 | 1 | 1 |
| `step-8-on` | 0 | 1 | 1 |
| `step-8-note` | -24 | 3 | 24 |
| `step-8-velocity` | 1 | 96 | 127 |
| `step-8-length` | 0.100000001 | 1 | 1 |
| `step-9-on` | 0 | 1 | 1 |
| `step-9-note` | -24 | 0 | 24 |
| `step-9-velocity` | 1 | 120 | 127 |
| `step-9-length` | 0.100000001 | 1 | 1 |
| `step-10-on` | 0 | 1 | 1 |
| `step-10-note` | -24 | 0 | 24 |
| `step-10-velocity` | 1 | 96 | 127 |
| `step-10-length` | 0.100000001 | 1 | 1 |
| `step-11-on` | 0 | 1 | 1 |
| `step-11-note` | -24 | 12 | 24 |
| `step-11-velocity` | 1 | 96 | 127 |
| `step-11-length` | 0.100000001 | 1 | 1 |
| `step-12-on` | 0 | 1 | 1 |
| `step-12-note` | -24 | 0 | 24 |
| `step-12-velocity` | 1 | 96 | 127 |
| `step-12-length` | 0.100000001 | 1 | 1 |
| `step-13-on` | 0 | 1 | 1 |
| `step-13-note` | -24 | 10 | 24 |
| `step-13-velocity` | 1 | 120 | 127 |
| `step-13-length` | 0.100000001 | 1 | 1 |
| `step-14-on` | 0 | 1 | 1 |
| `step-14-note` | -24 | 7 | 24 |
| `step-14-velocity` | 1 | 96 | 127 |
| `step-14-length` | 0.100000001 | 1 | 1 |
| `step-15-on` | 0 | 1 | 1 |
| `step-15-note` | -24 | 12 | 24 |
| `step-15-velocity` | 1 | 96 | 127 |
| `step-15-length` | 0.100000001 | 1 | 1 |
| `step-16-on` | 0 | 1 | 1 |
| `step-16-note` | -24 | 5 | 24 |
| `step-16-velocity` | 1 | 96 | 127 |
| `step-16-length` | 0.100000001 | 1 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## SONOMETRE (sonometre) {#sonometre}

Category: Visualization / Analyzers.

Measures loudness (LUFS): momentary, short-term, integrated, loudness range, true peak, PLR, and their history, against a target (streaming, Apple Music, radio, TV, CD). The sound passes unchanged.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `target` | 0 | 0 | 6 |
| `history` | 10 | 60 | 600 |
| `true-peak` | -3 | -1 | 0 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| target / `target` | Input | value | 1 |
| history / `history` | Input | value | 1 |
| true-peak / `true-peak` | Input | value | 1 |

## SPECTROMETER (spectrometer) {#spectrometer}

Category: Visualization / Analyzers.

Analyses the sound without changing it: a large spectrum curve with its peaks held, and a scrolling waterfall (the spectrum over time).

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `floor` | -120 | -90 | -36 |
| `tilt` | 0 | 3 | 6 |
| `smoothing` | 0 | 0.5 | 1 |
| `hold` | 0 | 2 | 10 |
| `history` | 2 | 8 | 30 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| floor / `floor` | Input | value | 1 |
| tilt / `tilt` | Input | value | 1 |
| smoothing / `smoothing` | Input | value | 1 |
| hold / `hold` | Input | value | 1 |
| history / `history` | Input | value | 1 |

## SPECTRUM (spectrum) {#spectrum}

Category: Visualization / Analyzers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| spectrum / `spectrum` | Output | other | 1 |

## STEREO SPLIT (stereo-split) {#stereo-split}

Category: Containers.

The left and the right, each through its own chain.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `left-gain` | -36 | 0 | 12 |
| `right-gain` | -36 | 0 | 12 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| left-gain / `left-gain` | Input | value | 1 |
| right-gain / `right-gain` | Input | value | 1 |

## STEREOSCOPE (stereoscope) {#stereoscope}

Category: Visualization / Analyzers.

Shows the sound's stereo image: the left-right cloud (goniometer), the phase correlation over time and per band (lows, mids, highs), the balance and the width. The sound passes untouched.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `zoom` | 0.5 | 1 | 4 |
| `persistence` | 0 | 0.5 | 1 |
| `history` | 1 | 10 | 30 |
| `display-mode` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| zoom / `zoom` | Input | value | 1 |
| persistence / `persistence` | Input | value | 1 |
| history / `history` | Input | value | 1 |
| display-mode / `display-mode` | Input | value | 1 |

## TRANSPORT BEAT (transport-beat) {#transport-beat}

Category: Transport primitives.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| transport / `transport` | Input | transport | 1 |
| beat / `beat` | Output | value | 1 |

## TRANSPORT PLAYING (transport-playing) {#transport-playing}

Category: Transport primitives.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| transport / `transport` | Input | transport | 1 |
| playing / `playing` | Output | trigger | 1 |

## TRANSPORT (transport-reader) {#transport-reader}

Category: Transport primitives.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| transport / `transport` | Input | transport | 1 |
| tempo / `tempo` | Output | value | 1 |
| beat / `beat` | Output | value | 1 |
| playing / `playing` | Output | trigger | 1 |

## TRANSPORT TEMPO (transport-tempo) {#transport-tempo}

Category: Transport primitives.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| transport / `transport` | Input | transport | 1 |
| tempo / `tempo` | Output | value | 1 |

## TUNER (tuner) {#tuner}

Category: Visualization / Analyzers.

Shows the note played and how far it is from the true note, with a needle or a strobe; the reference A is adjustable. The sound passes untouched, or is muted while tuning.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `reference` | 415 | 440 | 466 |
| `mute` | 0 | 0 | 1 |
| `display-mode` | 0 | 0 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| audio-out / `audio-out` | Output | audio | 2 |
| reference / `reference` | Input | value | 1 |
| mute / `mute` | Input | value | 1 |
| display-mode / `display-mode` | Input | value | 1 |

## ABSOLUTE (value-abs) {#value-abs}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| absolute / `absolute` | Output | value | 1 |

## ADD (value-add) {#value-add}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| sum / `sum` | Output | value | 1 |

## CEIL (value-ceil) {#value-ceil}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## CLAMP (value-clamp) {#value-clamp}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `minimum` | -1000000 | -1 | 1000000 |
| `maximum` | -1000000 | 1 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| clamped / `clamped` | Output | value | 1 |

## CONSTANT (value-constant) {#value-constant}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `value` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Output | value | 1 |

## COSINE (value-cosine) {#value-cosine}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| radians / `radians` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## VALUE DISPLAY (value-display) {#value-display}

Category: Visualization / Analyzers.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value-in / `value-in` | Input | value | 1 |
| value-out / `value-out` | Output | value | 1 |
| analysis / `analysis` | Output | other | 1 |

## DIVIDE (value-divide) {#value-divide}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 1 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| numerator / `numerator` | Input | value | 1 |
| denominator / `denominator` | Input | value | 1 |
| quotient / `quotient` | Output | value | 1 |

## FLOOR (value-floor) {#value-floor}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## MAXIMUM (value-max) {#value-max}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| maximum / `maximum` | Output | value | 1 |

## MINIMUM (value-min) {#value-min}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| minimum / `minimum` | Output | value | 1 |

## MODULO (value-modulo) {#value-modulo}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 1 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| remainder / `remainder` | Output | value | 1 |

## MULTIPLY (value-multiply) {#value-multiply}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 1 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| product / `product` | Output | value | 1 |

## NEGATE (value-negate) {#value-negate}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| negated / `negated` | Output | value | 1 |

## POWER (value-power) {#value-power}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -32 | 1 | 32 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| base / `base` | Input | value | 1 |
| exponent / `exponent` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## ROUND (value-round) {#value-round}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## SINE (value-sine) {#value-sine}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| radians / `radians` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## SQUARE ROOT (value-square-root) {#value-square-root}

Category: Float / Math.

No numeric parameter declared by this schema.

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| value / `value` | Input | value | 1 |
| result / `result` | Output | value | 1 |

## SUBTRACT (value-subtract) {#value-subtract}

Category: Float / Math.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `operand` | -1000000 | 0 | 1000000 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| a / `a` | Input | value | 1 |
| b / `b` | Input | value | 1 |
| difference / `difference` | Output | value | 1 |

## VELOCITY MAP (velocity-map) {#velocity-map}

Category: MIDI primitives.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `scale` | 0 | 1 | 2 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| midi-in / `midi-in` | Input | midi | 1 |
| midi-out / `midi-out` | Output | midi | 1 |

## XY FX (xy-fx) {#xy-fx}

Category: Containers.

Four chains at the corners of a pad: where the point is, the blend you hear.

| Engine key | Minimum | Default | Maximum |
| --- | --- | --- | --- |
| `mix` | 0 | 1 | 1 |
| `wet-gain` | -36 | 0 | 12 |
| `x` | 0 | 0.5 | 1 |
| `y` | 0 | 0.5 | 1 |

| Port / key | Direction | Type | Channels |
| --- | --- | --- | --- |
| audio-in / `audio-in` | Input | audio | 2 |
| dry-out / `dry-out` | Output | audio | 2 |
| branch-1 / `branch-1` | Output | audio | 2 |
| branch-2 / `branch-2` | Output | audio | 2 |
| branch-3 / `branch-3` | Output | audio | 2 |
| branch-4 / `branch-4` | Output | audio | 2 |
| branch-5 / `branch-5` | Output | audio | 2 |
| branch-6 / `branch-6` | Output | audio | 2 |
| branch-7 / `branch-7` | Output | audio | 2 |
| branch-8 / `branch-8` | Output | audio | 2 |
| mix / `mix` | Input | value | 1 |
| wet-gain / `wet-gain` | Input | value | 1 |
| x / `x` | Input | value | 1 |
| y / `y` | Input | value | 1 |
