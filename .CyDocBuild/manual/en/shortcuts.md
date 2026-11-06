---
id: shortcuts
title: Keyboard and mouse reference
description: Look up keyboard shortcuts and mouse gestures.
section: Manual / Reference
order: 34
revision: d24730438920027c
---

## Overview {#overview}

Commands act on the focused editor and active mode. Text/source fields keep typing. The CyDoc reader opens in the browser and uses its own search/navigation keys. Hardware controller assignments can add other mappings; check them if a key behaves differently.


~~~image
{"id": "figure-timeline", "src": "assets/timeline-en.svg", "alt": "The two-axis navigator keeps its grabbed time under the pointer.", "title": "The two-axis navigator keeps its grabbed time under the pointer.", "controls": "minimal"}
~~~

*The two-axis navigator keeps its grabbed time under the pointer.*


## Global and workspace {#global}
| Key | Action |
| --- | --- |
| Ctrl+F1 | Open the user manual. |
| Ctrl+Shift+F1 | Help for the active mode/editor. |
| Ctrl+N / Ctrl+O / Ctrl+S | New/open/save in the active session context. |
| Ctrl+Shift+S | Save as. |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo in the relevant editor. |
| Ctrl+J | Hide/restore bottom panel. |
| F1 / F2 | Studio Arrangement / Launcher. |
| F3 / F4 / F5 / F6 | Devices / Graph / Mixer / Editor. |
| F7 / F8 / F9 | Plugin workshop / Routing / Library. |
| F10 / F11 | DJ mix / Foundry. |
| Ctrl+K | Toggle computer-keyboard notes. |
| Ctrl+M | Controller Learn overlay. |
| ? | Open the CyDoc shortcut reference. |

Cinema reuses function keys for its own lower tools; use its visible labels. CyDoc Ctrl+K opens local search. Escape closes search or reduces an enlarged image; browser Back/Forward follows visited pages.

## Transport and editing {#editing}
| Gesture | Context and result |
| --- | --- |
| Space | Play/pause the active workspace; workshop uses its simulator. |
| Home / R / L | Studio rewind / record / loop when its transport owns keys. |
| S / Ctrl+D / Delete | Split at cursor / duplicate / remove selection where offered. |
| Ctrl+X / C / V | Cut / copy / paste in the focused editor. |
| Ctrl+Shift+V / Ctrl+T | Foundry mix-paste / trim selection. |
| M / Shift+M | Foundry marker / selection region. |
| Q / Shift+arrows | Piano-roll quantise / octave moves. |
| Double-click / right-click note | Add/remove / erase note. |

## Mouse and graph {#mouse}
Drag the upper time band sideways to pan, down/up to zoom; double-click fits. Ctrl+wheel zooms Arrangement/Cinema around the pointer; Shift+wheel moves in time. Foundry wheel zooms waveform and Ctrl+wheel changes waveform height. Piano-roll Alt+wheel changes row height.

Graph Tab/double-click searches nodes, F frames and A arranges; Alt-click cuts a cable. Ctrl+G groups selected nodes. Native controls use vertical drag, Ctrl for fine and Shift for coarse movement, wheel for steps and double-click for reset. Numeric entry is available on supported value fields. Escape exits armed modulation/learning states.

See [workspace](doc:workspace), [graphs](doc:graphs) and [controllers](doc:controllers).
