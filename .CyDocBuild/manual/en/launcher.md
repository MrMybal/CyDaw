---
id: launcher
title: Launcher: clips and scenes
description: Launch clips and scenes on your Studio tracks.
section: Manual / Compose and edit
parent: studio
order: 4
revision: ffd590db56d5aae1
---

## Overview {#overview}

Launcher is Studio's performance view. Columns are tracks, rows are scenes and slots hold clips. Devices and routing stay the same between Arrangement and Launcher. The indicator above the grid says which source currently drives project playback.


~~~image
{"id": "figure-launcher", "src": "assets/launcher-en.png", "alt": "Scenes, slots and their track mixer strips.", "title": "Scenes, slots and their track mixer strips.", "controls": "minimal"}
~~~

*Scenes, slots and their track mixer strips.*


## Fill the grid {#slots}
Drop a WAV or supported audio file directly into an audio-track slot. Double-click a MIDI slot to create/edit its notes. A sound dropped in empty space outside track columns creates a track playing it as a sampler; this differs from inserting a long recording into an existing audio slot. Aim at the correct destination.

Name clips and scenes by musical role. A clip opened in Editor retains its slot identity. Audio context menus offer Reverse and audio edits; MIDI clips contain notes rather than a waveform.

## Launch in time {#launching}
Set launch quantisation above the grid, then launch a clip or whole scene. The change waits for that musical boundary. Each track has stop; a scene starts its populated slots together; global stop ends Launcher playback. Inspect pending actions and quantisation before repeatedly clicking a launch that has not happened yet.

## Return to Arrangement {#arrangement}
Launching can replace the arranged source. Check the playback-source indicator and explicitly return to Arrangement playback to hear the timeline song. Clip edits change stored clip data; track faders and effects process whichever source currently feeds the track.

See [Studio](doc:studio), [MIDI](doc:midi) and [audio](doc:audio).
