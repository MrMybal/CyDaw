---
id: interface-design
title: Design full windows and module cards
description: Design the full plugin window and the compact module card.
section: Manual / Build and connect
parent: workshop
order: 26
revision: c8dee75dd9f8b555
---

## Overview {#overview}

The Interface tab edits two independent surfaces: Full window for the large device/exported plugin and Module card for the in-CyDAW device strip. Window size, card size, controls and colours are saved separately. A clean native-looking interface needs explicit bindings as well as visual layout.


~~~image
{"id": "figure-synth", "src": "assets/synth-en.png", "alt": "A native instrument panel illustrates the grouped controls available to the designer.", "title": "A native instrument panel illustrates the grouped controls available to the designer.", "controls": "minimal"}
~~~

*A native instrument panel illustrates the grouped controls available to the designer.*


## Size, layers and controls {#layout}
Choose the active surface, then set width/height in pixels, background, accent, text and grid. The Full-window settings remain available with a control selected. 1:1 shows actual size; Frame fits the canvas to the available area. Middle-button pan and zoom navigate the design without resizing the product.

Add knobs, faders, text, displays and supported native panels. Use Layers to select, hide or lock items; Ctrl/Shift extends selection and a rectangle selects several. Align/distribute and group related controls. Set page, group, label, rounding, colours and value visibility. Keep enough space for long translated labels and extreme parameter values.

## Bind native panels {#bindings}
For a native instrument in the graph, Add complete interface creates its pages and sections already connected. Existing manually drawn controls remain. The section library also supplies native panels for personal graphs without that instrument: assign every required binding explicitly. Unbound controls are disabled. Effect panels also have editable bindings. Changing visual layout does not repair a wrong parameter destination.

## Declarative JSON and C++ {#code}
Code JSON edits the complete active surface. Reload gets the current model; Apply validates types, IDs, bindings and canvas bounds before accepting it. Keep a saved version before structural edits. IDs must be unique and bindings must exist. A visually plausible JSON file can still target the wrong sound control, so test live changes.

A C++ ImGui component supplies new custom drawing/interaction. Use Example for the supported CustomInterfaceABI template and its parameter get/set callbacks, then Compile and read diagnostics. Use only the current ABI; component source is included with exports and needs a working target compiler. Test resizing, disabled/unbound states, presets and host automation, not only the default picture.


See [workshop](doc:workshop), [graph parameters](doc:graphs#exposure) and [export](doc:plugin-formats).
