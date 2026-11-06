---
id: automation
title: Automation and curves
description: Draw and edit changes to track and device settings.
section: Manual / Compose and edit
parent: studio
order: 7
revision: 49d8f12b179aaebe
---

## Overview {#overview}

Automation stores changes against the timeline; modulation produces changing controls around a base value. Saved knob value, automation lane and modulation amount are different parts of the played result. A moving indicator can therefore differ from the knob position.


~~~image
{"id": "figure-automation", "src": "assets/automation-en.svg", "alt": "Base value, timeline automation and modulator amounts meet at the destination.", "title": "Base value, timeline automation and modulator amounts meet at the destination.", "controls": "minimal"}
~~~

*Base value, timeline automation and modulator amounts meet at the destination.*


## Create lanes {#lanes}
Use a parameter's context menu to automate it, or a track's A controls to choose its destination. Add points where the value must be defined. Put endpoints around a change to preserve other passages. Track volume, device controls and master targets have different bindings: name lanes clearly. Studio, Foundry and Cinema use their own time contexts.

## Shape curves {#curves}
Select points before moving, copying or deleting. Choose interpolation: step for switches, linear for ramps, smooth/Bezier/B-spline where offered. Tangents and segment bends change the path between points. Check units and scales: frequency often needs logarithmic perception, time and gain their own ranges. Undo follows the editor owning the gesture.

## Combine modulation {#combined}
Start with a useful base value, add one modulator and adjust its amount by listening. An armed arrow edits depth instead of base. A negative amount reverses direction. Check resonance, feedback and gain endpoints before using extreme motion. Graph parameters/modulators supported by native export are preserved; host automation then supplies values in plugin time.

See [modulators](doc:modulators), [mixing](doc:mixing) and [publication](doc:graphs#exposure).
