---
id: dj
title: DJ mix: decks and transitions
description: Mix two or four decks with sync, loops and cue monitoring.
section: Manual / Dedicated modes
order: 20
revision: 130ea3bc8a43a6c9
---

## Overview {#overview}

Enter DJ mix from the top switch or F10. It has its own collection, decks and processing graph; Studio tracks remain available and may also be playing. The deck MASTER chooses the tempo leader. The processing master, DJ MASTER level and application's common master are separate points in the signal path.


~~~image
{"id": "figure-dj", "src": "assets/dj-en.png", "alt": "Two decks with an original test phrase and the central mixer.", "title": "Two decks with an original test phrase and the central mixer.", "controls": "minimal"}
~~~

*Two decks with an original test phrase and the central mixer.*


~~~image
{"id": "figure-dj-flow", "src": "assets/dj-flow-en.svg", "alt": "CUE is tapped before the channel fader and processing chains.", "title": "CUE is tapped before the channel fader and processing chains.", "controls": "minimal"}
~~~

*CUE is tapped before the channel fader and processing chains.*


## Load, analyse and cue {#decks}
Drop a song on a deck from collection, Browser or the file manager; a collection double-click uses a free deck. Wait for decode/analysis. The overview seeks through the whole track; the larger waveform follows the playhead and supports scratch dragging and wheel zoom. Two/four-deck mode changes the visible layout. Analysis estimates BPM, first beat, key and loudness; verify the beat grid by ear before relying on sync.

CUE in playback returns to the cue and stops; while stopped it sets/auditions a cue. Hot cues 1–8 jump or set an empty cue; Shift-click/right-click clears one. QUANT aligns relevant actions to the grid. Loop buttons set beat lengths; /2 and x2 resize and arrows shift the loop. Beat-jump controls move by their chosen length.

## Tempo, KEY and SYNC {#tempo}
The tempo fader speeds or slows playback; choose its range and double-click for zero change. Bend +/- nudges temporarily. With KEY enabled, time stretching preserves pitch while speed changes. Disable KEY for vinyl-like speed/pitch change. This explains why tempo movement may not transpose the sound. SYNC follows the tempo leader and phase; check which deck is MASTER and correct half/double-time analysis or a wrong first beat first.

## Mix and monitor {#mixer}
Each strip has gain, isolator HI/MID/LOW with kill switches, a centre-neutral filter, CUE, fader and crossfader assignment L/-/R. A deck assigned '-' ignores the crossfader but still uses its fader. Set crossfader CURVE for blending or cuts. CUE MIX balances preview and master; SPLIT uses one stereo pair as mono mix/mono cue for suitable cabling. It does not invent another hardware output.

Deck FX follow the fader/crossfader so echoes may continue after closing it. The lower Devices/Graph/Mixer/Routing tools add the same native modules used in Studio to decks, buses and the DJ processing master. CUE is pre-fader/pre-processing. DJ MASTER and LIMIT follow this processing; the result reaches the common output master.

## Record and keep a set {#recording}
The top record control writes a 24-bit WAV in the configured mixes folder and includes DJ processing. Check free disk space and destination first. Collection/processing settings persist in their own locations; saving a Studio song is not a DJ performance recording. Stop recording and verify the resulting file. To return to manual fader/EQ control, avoid simultaneously replaying an automated demonstration of those controls.

See [mixing](doc:mixing), [sync](doc:sync) and [troubleshooting](doc:troubleshooting#silence).
