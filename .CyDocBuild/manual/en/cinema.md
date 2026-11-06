---
id: cinema
title: Cinema: sound to picture
description: Place editable sounds against video frames and bus containers.
section: Manual / Dedicated modes
order: 22
revision: 52e81bb062439e59
---

## Overview {#overview}

Cinema uses arrangement-style bus rows with time containers inside them. A container is an audio or MIDI sub-track holding ordinary clips. Its sub-track feeds the bus, then the Cinema master. Containers may sit side by side in time; they are not separate full-window timelines. Video is optional when an external transport supplies timing.


~~~image
{"id": "figure-cinema", "src": "assets/cinema-en.png", "alt": "The shared timeline, frame strip, bus containers, monitor and track tools.", "title": "The shared timeline, frame strip, bus containers, monitor and track tools.", "controls": "minimal"}
~~~

*The shared timeline, frame strip, bus containers, monitor and track tools.*


~~~image
{"id": "figure-cinema-bus", "src": "assets/cinema-bus-en.svg", "alt": "Sub-track audio reaches its bus and then the Cinema master.", "title": "Sub-track audio reaches its bus and then the Cinema master.", "controls": "minimal"}
~~~

*Sub-track audio reaches its bus and then the Cinema master.*


## Create buses and sub-tracks {#containers}
Create/name a bus row, then double-click its empty timeline or use its add control to create a time container. Choose audio or MIDI, place it and adjust its duration. Its title moves the whole sub-track; clips inside move independently and use compatible track types. Select a bus and open the extra Sub-tracks tab below: it lists all its containers, including offscreen ones. Select or Frame/double-click an entry to focus it; FX/A opens its effects/automation. A container can be reassigned to another bus, changing its main output.

## Edit sound and notes {#clips}
Drop a sound inside an audio container. Double-click empty audio space to choose a file for that sub-track; right-click offers import or an empty audio clip. In MIDI space, double-click creates a note clip and opens its editor; choose an instrument. Clip context menus offer Details, Reverse, split at cursor, duplicate and delete. Drag edges to trim, or move/copy between compatible containers. Details adjusts offset, gain and fades. Undo/Redo follows the active editing context.

## Video, thumbnails and frames {#video}
Load a video from the monitor, menu or drop. The monitor can be docked elsewhere, tabbed, resized or detached. Video settings define timeline start, source in, duration and frame rate. No video removes the reference but keeps imported audio clips. The frame strip follows the same scale as sound. At wide zoom it samples thumbnails; close zoom shows individual frames. Click/drag it to scrub the monitor. Choose frame grid, use previous/next frame or arrow keys and check timecode rate before fine alignment.

If video has audio, extraction creates a soundtrack bus and a full editable audio clip. Cut/move/treat it like WAV; moving it does not edit the video file. Explicit import soundtrack creates another extraction; reopening a saved session retains existing edits rather than duplicating the import.

## Transport, effects and delivery {#delivery}
Use the upper double band for pointer-anchored pan/zoom, the ruler for the cursor and the native split/duplicate/delete/undo tools. Manual navigation can suspend playback-follow. Devices, routing, buses and master automation work in the Cinema session. Use SYNC to follow a compatible external editor without local video; a chosen protocol must actually be supported at both ends. Link shares musical phase, not arbitrary film position.

Export renders audio, not a newly encoded edited video. Keep the Cinema session and source video separately, choose an effect tail and verify sync against the target picture. See [synchronisation](doc:sync), [routing](doc:mixing) and [export](doc:import-export).
