---
id: foundry
title: Foundry: multitrack sound editing
description: Assemble, repair and process sound files.
section: Manual / Dedicated modes
order: 21
revision: 84e56ff09e2ce8fe
---

## Overview {#overview}

Foundry is a dedicated sound-file workspace, entered from the top switch or F11. Each document has a tab, sample rate, audio/MIDI tracks, clips, processing and master. Studio tracks are preserved separately. Save an editable .cyfoundry session when your work includes multiple clips, MIDI, routing or effects; export creates audio.


~~~image
{"id": "figure-foundry", "src": "assets/foundry-en.png", "alt": "Foundry with several overlapping edits of an original test sound.", "title": "Foundry with several overlapping edits of an original test sound.", "controls": "minimal"}
~~~

*Foundry with several overlapping edits of an original test sound.*


## Import, select and navigate {#editing}
Open a sound as a document or add sounds to an existing one. Drop on a lane to insert at that time, or below lanes to create a track. + Track chooses audio or MIDI; MIDI tracks need an instrument and note clips. Rename tracks by double-clicking their names, use M/S, level/pan and selected-track controls.

The overview frames the session. Grab the upper ruler band to pan/zoom; double-click fits. Wheel zooms the waveform, Shift+wheel pans, Ctrl+wheel changes waveform height; the right vertical band adjusts track height/scroll. A click places the cursor; drag selects a time range across tracks; Shift extends. L/R/L+R can restrict channel edits.

## Clips and source operations {#clips}
Drag a clip title to move it or another compatible track; drag edges to trim and top corner handles for fades. Right-click for split, duplicate, gain, Reverse or removal. Details edits start, duration, source offset, gain and fades. S splits at cursor; Ctrl+D duplicates.

Cut/copy/paste and mixing paste operate at the cursor/selection; inside a sound they may rewrite its pieces while retaining one clip. Delete, trim, silence insertion and waveform pencil editing serve different jobs. Undo/Redo and History let you return to prior edits; editing after an older version replaces the later branch. Markers (M) and regions (Shift+M) label moments/ranges.

## Process, route and automate {#processing}
Treatments/Markers are in the separate lower-right panel; Details/Analysis/History stay beside the montage. Select treatment, scope and values before Apply. Normalise changes peak level, reverse changes time and polarity inversion changes sign. The lower Devices plus opens the full Studio library. Tracks pass through chains/faders and buses to the session master. A controls automation on tracks and master; selecting MASTER opens its effects.

## Save and export {#saving}
Ctrl+S keeps editable work; a simple opened file may be saved as sound, while customised sessions use .cyfoundry. Export mix includes routing and effects. Export tail seconds preserves delay/reverb beyond source end; default zero may cut it. Send mix to Studio creates one rendered source; send each track keeps separate rendered parts. They are not live links to the Foundry document.

See [audio editing](doc:audio), [processings](doc:processing), [Cinema](doc:cinema) and [media](doc:import-export).
