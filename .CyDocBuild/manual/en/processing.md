---
id: processing
title: Processings: audio and note scripts
description: Audition reusable audio and MIDI processing steps.
section: Manual / Dedicated modes
order: 23
revision: 2b0aa3259e24ab43
---

## Overview {#overview}

Processings designs reusable transformations, auditions Before/After and applies them to clips. Choose Audio or MIDI in the library, then a factory example or personal item. Editing a factory item saves a personal copy; Save as new creates another item. A processing is a recipe, not a full Studio project.


~~~image
{"id": "figure-processor", "src": "assets/processor-en.svg", "alt": "Choose source, order steps, audition, then commit an undoable result.", "title": "Choose source, order steps, audition, then commit an undoable result.", "controls": "minimal"}
~~~

*Choose source, order steps, audition, then commit an undoable result.*


## Audio steps {#audio}
Add ordered steps: native effect chain, JSFX code, noise reduction, hum removal, click repair, high/low-pass, gain, normalisation or silence trim. Enable/bypass, move and remove steps before judging the whole result. A chain step uses Studio-style Devices/Graph/Mixer/Routing tools. Check exposed settings that should appear when applying the recipe.

Choose a test sound or the selected arrangement clip. Before and After timelines share time; play either or switch at the same point. Changes process in the background. An error leaves the last valid result rather than making faulty code useful. Save result writes a rendered file; applying changes the intended clip.

## Tails and apply {#apply}
Let the end ring permits reverb/delay to outlast the input. Set silence threshold, hold, final fade and maximum tail. A steady non-decaying noise tail is stopped; a still-decaying tail can reach the maximum. Without tail extension, output duration follows the source. Audition the end, not only the attack.

Use a clip's context menu to apply a recipe with its exposed settings. Confirm the source/selection and result before Apply. The application is undoable as a coherent edit; it does not turn a destructive external file overwrite into unlimited recovery.

## MIDI note scripts {#midi}
Note scripts use EEL2 with @init, @note and @end. Fields include pitch, start, length, velocity, chance and keep; times are beats. Parameters use param1 through param16 declarations. Functions can query neighbouring notes, convert milliseconds to beats and add notes. Preview before applying so notes stay inside useful pitch/duration ranges.

~~~eel2
param1:90<1,100,1>Velocity (%)
@note
velocity *= param1 / 100;
~~~

This example scales each stored note's velocity without adding audio processing. Removing notes uses keep = 0. Compare with live MIDI effects, which transform playback events without rewriting the clip.

See [MIDI](doc:midi), [audio](doc:audio) and [JSFX hosting](doc:plugin-formats#jsfx).
