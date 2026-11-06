---
id: devices
title: Devices, presets and native panels
description: Add instruments and effects and manage their presets.
section: Manual / Instruments and effects
order: 9
revision: 9821d0d814f9d701
---

## Overview {#overview}

Devices is the readable front of a track's processing graph. Native instruments and effects have compact cards and, where supported, a full-size editor. A graph device can have a separately designed card and full window. Presets store sound settings; the project also stores placement, routing and automation.


~~~image
{"id": "figure-synth", "src": "assets/synth-en.png", "alt": "The native instrument window exposes grouped controls, waves, filter and envelope displays.", "title": "The native instrument window exposes grouped controls, waves, filter and envelope displays.", "controls": "minimal"}
~~~

*The native instrument window exposes grouped controls, waves, filter and envelope displays.*


## Add and order devices {#chain}
Select the destination track, then press the plus at the end of its device strip. The library offers categories, device kinds, manufacturers and search. Double-click a result to insert it. Instruments belong on MIDI tracks; effects process the audio they receive. Drag cards to reorder supported chains. Bypass for comparison without removing the device. Removing an instrument or a routing container can change more than one visible card, so save first.

## Presets and values {#presets}
Use the card's preset/list control to load factory or personal settings. Save a personal preset after editing, then test that reopening it restores the intended sound. A track-chain preset stores multiple devices, while a device program in the workshop becomes an exported plugin program. They are not interchangeable project formats. The value list exposes parameters beyond the compact card and supports searching. Numeric units matter: milliseconds, seconds, hertz, decibels and percentages are not equivalent ranges.

## Full panels and portable devices {#panels}
Expand the card or double-click supported panel titles for the large editor. Drag the visual filter/envelope controls when offered, then fine-tune numeric values. Graph devices reopen in Plugin workshop through their pencil. Transfer editable devices with .cydawdevice, and copy referenced external media separately. Native plugin exports package the designed interface and DSP, not the graph editor.

Continue with [synths](doc:synths), [studio effects](doc:studio-effects), [containers](doc:containers) and [workshop](doc:workshop).
