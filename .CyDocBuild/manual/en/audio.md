---
id: audio
title: Audio, takes and time stretching
description: Cut recordings, edit fades and stretch clips.
section: Manual / Compose and edit
parent: studio
order: 6
revision: 745766c4844489d0
---

## Overview {#overview}

Double-click an audio clip for Editor's Sample, Takes, Stretch and Events faces. Start, duration, source offset and fades describe what the clip plays; changing its source is another operation. Preview auditions the clip with its actual stretch and take choices.


~~~image
{"id": "figure-timeline", "src": "assets/timeline-en.svg", "alt": "Clip boundaries, fades and navigation have separate roles.", "title": "Clip boundaries, fades and navigation have separate roles.", "controls": "minimal"}
~~~

*Clip boundaries, fades and navigation have separate roles.*


## Source edits and history {#source}
Drag the waveform to select; Shift extends selection. Cut, copy, paste, trim, silence, gain, normalise, reverse, polarity, DC removal and channel swap act on the selected or played region as supported. Reverse changes time; polarity changes sign. Source editing creates an edited file and preserves the original. History lists prior versions; editing after an older version replaces the later history branch. Keep project and edits folder together.

Cut/trim/insert/paste are restricted with stretch markers or comped takes; first choose the intended take or edit separate events. Preserve a saved version before flattening complex edits.

## Takes and comping {#takes}
Recording over a clip can add an aligned take; loop recording makes repeated passes. In Takes audition them, then choose their best time ranges. Cuts stay aligned to the clip. Listen for clicks, phase changes and background-noise jumps at boundaries. A take added from a file must be aligned to the original performance.

## Stretch and events {#stretch}
Stretch anchors map source moments to project times. Moving an anchor changes when its source moment plays. Choose a mode suitable for percussive or sustained material and compare at final tempo. Transposition changes pitch; stretching changes duration and may preserve pitch. Events editing splits a clip into timed source regions: edit those rather than assuming one continuous file.

See [Foundry](doc:foundry), [processing](doc:processing) and [media/export](doc:import-export).
