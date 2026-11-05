# Mixer, buses and sidechains

A bus collects tracks through its own devices and fader. A main output sends the track's main signal somewhere; a send adds a parallel route. Keep the full path visible when checking levels or missing audio.

![Sources feed a group bus and parallel return, then the master.](../assets/routing-en.svg)

![The Studio mixer shares track, bus and master routing.](../assets/mixer-en.png)

## Tracks, buses and channels {#buses}
Create and name a bus, then set it as the main output of related tracks. Put common processing on that bus. In Routing inspect extra inputs/outputs, channel format and send level. Mono sources, stereo effects and multichannel destinations need compatible assignments. CyDAW rejects routing loops: correct the route rather than forcing a cycle.

## Sends and sidechain {#sidechain}
Pre-fader sends are taken before the fader; post-fader sends follow it. Decide whether lowering the source should also lower its return. A compressor sidechain feeds the detector, not an additional audible input. On supported dynamics devices, the chain-link selector chooses the source. A kick can drive bass compression with its fader lowered because the feed is pre-fader; muting it differs. Listen to the detector when offered and adjust threshold, attack and release.

## Master and measurement {#master}
Control levels throughout the chain. A limiter ceiling prevents excessive output peaks but cannot undo clipping already printed into a source or earlier stage. Peak, RMS and loudness measure different things. Reset integrated loudness before a full pass; compare references at similar listening loudness.

Foundry, Cinema and DJ processing have their own treatment contexts before the common output. See [effects](studio-effects.md), [Foundry](foundry.md) and [export](import-export.md).
