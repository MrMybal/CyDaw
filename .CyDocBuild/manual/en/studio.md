---
id: studio
title: Studio: arrange and record
description: Build a song with audio and MIDI tracks.
section: Manual / Compose and edit
order: 3
revision: 9e974e9569855dcc
---

## Overview {#overview}

Arrangement builds a song on a shared musical timeline. Audio tracks play recordings, MIDI tracks play instruments and buses collect other tracks. A device chain, fader, outputs and automation belong to a track, not the current clip. Launcher uses the same tracks.


~~~image
{"id": "figure-studio", "src": "assets/studio-en.png", "alt": "An original note phrase and an audio clip in Arrangement.", "title": "An original note phrase and an audio clip in Arrangement.", "controls": "minimal"}
~~~

*An original note phrase and an audio clip in Arrangement.*


## Add tracks and clips {#clips}
Use the plus/Track menu or drag files from Browser or the file manager. An audio file dropped on an audio lane becomes a clip at that time. A sound used on a MIDI track becomes a sampler source instead. Double-click an empty MIDI lane to create a note clip; double-click a clip to open Editor.

Drag a clip to move it in time or to a compatible track. Its edges trim it. Split at the playhead, duplicate with Ctrl+D and delete a selection with Delete. Use short fades at audio joins. Reverse reverses time; polarity inversion changes waveform sign and is a separate operation.

## Record {#recording}
1. Set the audio/MIDI input and destination track; choose an instrument for notes.
2. Arm the intended track and check monitoring quietly. Avoid monitoring an input twice through hardware and software.
3. Place transport, set a loop if needed and record. Stop and inspect the resulting clip.
4. Repeated audio passes can become takes. Choose desired sections in Takes and keep the original recordings with the project.

## Finish an arrangement {#song}
Set tempo and signature before aligning beat-based material. Use meaningful names and colours. Add automation where values must follow project time and route related tracks through a bus. Save editable versions before structural changes. Check the master and effect tails before rendering.

Continue with [MIDI](doc:midi), [audio](doc:audio), [Launcher](doc:launcher) and [export](doc:import-export).
