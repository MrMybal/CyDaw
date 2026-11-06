---
id: synths
title: Synthesizer family and sound design
description: Choose a synthesis engine and shape a playable sound.
section: Manual / Instruments and effects
parent: devices
order: 10
revision: 752f553c7d7cfb3b
---

## Overview {#overview}

Start with one oscillator and a short phrase, then add complexity. Most family instruments share voice mode, voice count, glide, bend and master level. The same model is available as a track instrument and a graph node; the surrounding note/voice wiring determines how it plays.


~~~image
{"id": "figure-midi-voice", "src": "assets/midi-voice-en.svg", "alt": "From notes to per-voice sound and summed audio.", "title": "From notes to per-voice sound and summed audio.", "controls": "minimal"}
~~~

*From notes to per-voice sound and summed audio.*


## Choose a synthesis engine {#models}
| Instrument | Main engine and useful starting point |
| --- | --- |
| Ambre | Three-oscillator analogue polysynth; mix one oscillator before adding detune. |
| Colosse | Modern two-wavetable synth, dual filters, sequence and modulation; scan one table first. |
| Mosaïque | Two hybrid engines: analogue, wavetable, harmonic or modal; compare engines at matched levels. |
| Nébuleuse | Atmospheric layered synthesis/grains, Harmonia and Orb movement; start with one layer. |
| Opale | Everyday polysynth: two oscillators, sub, sync/unison, creative mixer and filter. |
| Quartz | Four-operator free-matrix FM; hear a carrier, then raise one modulator. |
| Saphir | Four-operator FM with eleven algorithms; choose routing before changing operator ratios. |
| Tirette | Nine-drawbar organ with percussion, vibrato/chorus, drive and rotary cabinet. |
| Onyx | Phase-distortion oscillators and resonant waveforms; sweep shape slowly. |
| Alliage | Modular choice of oscillator/filter engines; establish a simple signal path first. |
| Argile | Doubled oscillators, subs, filters and amplifiers; compare series/parallel filtering. |
| Dérive | Simple analogue voices with controlled drift and a loopable envelope. |
| Bronze | Mallet/noise excites two resonant objects; tune the objects before increasing excitation. |
| Jade | Modelled electric piano with pickup/preamp, tremolo and chorus; velocity matters. |
| Silex | Synthesized drum voices and character settings; closed/open hats can choke each other. |

## Voices, tone and envelopes {#voice}
Poly plays overlapping notes; Mono uses one note voice; Legato changes retrigger behaviour. More voices and long releases increase simultaneous work. Glide time and Always determine when transitions slide. Bend range affects how incoming pitch bend is interpreted. Set master level after adjusting unison or oscillator count.

Attack controls onset, Decay moves to Sustain, and Release controls the tail after note-off. A filter-envelope amount changes tone through that envelope; a negative amount moves in the opposite direction. Cutoff and resonance interact: audition resonant peaks at moderate level.

## Modulation and effects {#design}
Use synced LFOs for rhythmic motion and free rates for independent motion. Compare one destination at a time before assigning several macros. Built-in instrument FX follow the generated voice sound; track effects follow the instrument as a whole. Save a preset with a descriptive name and test soft/hard notes, low/high pitches and chords.

See [Prisme](doc:prisme), [modulators](doc:modulators) and the [native parameter reference](doc:reference-instruments).
