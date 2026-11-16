---
id: workspace
title: Panels and timeline navigation
description: Find panels, navigate time and adjust zoom.
section: Manual / Start here
parent: welcome
order: 2
revision: 0e4d5feb7323a1eb
---

## Overview {#overview}

The top mode switch changes the main task; it does not convert session formats. Studio's Arrangement and Launcher are two views of the project's tracks. Selecting a track connects Inspector and the lower Devices, Graph, Mixer, Routing and Editor tools to that selection.


~~~image
{"id": "figure-studio", "src": "assets/studio-en.png", "alt": "Studio with audio and MIDI clips and surrounding panels.", "title": "Studio with audio and MIDI clips and surrounding panels.", "controls": "minimal"}
~~~

*Studio with audio and MIDI clips and surrounding panels.*


## Main window {#window}
On Windows, the main desktop window uses an integrated dark title bar. Drag the empty area around the project title to move it; double-click there to maximize or restore it. The buttons at the top right minimize, maximize/restore and close the window. Drag its edges or corners to resize it. Menus stay clickable. Closing a modified project still asks whether to save. Exported plugin editors keep their host application's frame.

## Arrange panels {#panels}
Drag supported panel tabs to rearrange or detach them; drag separators to resize. Hide beside the bottom tabs, Ctrl+J or View > Bottom panel frees vertical space. Inspector and Browser have their own hide and restore controls. Hiding a panel does not remove its devices or stop processing. Relevant layouts and lower-panel visibility are remembered per mode.

## Navigate the timeline {#timeline}
Grab the upper band above the timeline. Drag sideways to pan, down to zoom in and up to zoom out. The grabbed time stays under the pointer. Double-click to frame content. The lower ruler places the playhead. Follow playback scrolls with transport; manual navigation can suspend following. Arrangement/Cinema also offer pointer-anchored Ctrl+wheel zoom and Shift+wheel time scrolling. Foundry's waveform gestures are described in its chapter.

## Editing focus {#focus}
Click the editor you mean to change before Undo, Delete, Split or Duplicate. Graph Undo, note Undo and waveform history address different operations. Text fields and source editors retain keyboard input. Native knobs support dragging, wheel adjustment, reset and numeric entry; verify value and unit. An armed modulation arrow changes modulation depth instead of the knob's base value.

See [shortcuts](doc:shortcuts), [automation](doc:automation) and [graphs](doc:graphs).
