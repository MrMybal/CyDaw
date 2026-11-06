---
id: containers
title: Effect and instrument containers
description: Arrange device chains in layers, splits and selectors.
section: Manual / Instruments and effects
parent: devices
order: 19
revision: 272deb6392baaf46
---

## Overview {#overview}

A container holds branches of devices and mixes or selects them. Add it from the normal Devices plus, then open a branch to edit its own chain. A track bus routes between tracks; a device container routes inside one device. Cinema time containers are another concept.


~~~image
{"id": "figure-routing", "src": "assets/routing-en.svg", "alt": "Parallel branches and their common output need explicit balance.", "title": "Parallel branches and their common output need explicit balance.", "controls": "minimal"}
~~~

*Parallel branches and their common output need explicit balance.*


## Effect containers {#effects}
| Container | Branch behaviour |
| --- | --- |
| Chain | One serial chain. |
| FX Layer | One to eight parallel layers summed together. |
| FX Selector | One of up to eight chains, with switching fade. |
| Mid-Side Split | Mid and Side processed separately then recombined. |
| Stereo Split | Left and Right paths. |
| Multiband FX-2 / FX-3 | Two or three crossover-separated bands. |
| XY FX | A/B/C/D blend controlled by an XY pad, followed by Post FX. |
| Replacer | Transients trigger a built-in sound or selected instrument. |

Mix blends the dry input with the branches; Wet Gain changes branch output. Check branch level, mute/solo and crossover settings. A Replacer threshold/hold determines triggers; it is not a continuous sampler input.

## Instrument containers {#instruments}
Drum Machine assigns an instrument/effects to each mapped key. Instrument Layer plays up to sixteen layers within key/velocity limits. Instrument Selector sends a new note to one layer; that note finishes in its original layer. Selection can be manual, round robin, random, keyswitch, CC or program change. Each layer has its own instrument and chain. Instrument containers cannot nest other instrument containers; changing container type resets its default structure.

## Edit and transfer {#editing}
Name branches, add devices at the intended depth and use the breadcrumb/back controls to leave a branch. Compare the full recombined sound; soloing a band or Mid/Side path does not represent the final mix. Save the complete container preset when its sound depends on several branches. Instrument containers are currently excluded from native graph-plugin export because their voices are owned by track engines.

See [graphs](doc:graphs), [drums](doc:drums) and [plugin limits](doc:plugin-formats#limits).
