# Audio, MIDI and first launch

Extract the whole Windows package before launching CyDAW 2.exe. Keep DLLs, fonts, documentation and the worker beside it. The Alpha uses Windows Audio/WASAPI; an optional backend appears only in a build that includes it. A moving meter does not prove that the selected hardware channels reach the speakers.

![Check the entire output path.](../assets/signal-flow-en.svg)

## Device, rate and buffer {#device}
In Options > Audio settings choose the driver type, device, sample rate and buffer. Enable the output pair connected to the monitors. On multichannel hardware, outputs 1/2 may differ from the pair selected in its hardware mixer. Play a known signal quietly and check track, master, selected output and hardware monitor in that order.

A smaller buffer reduces monitoring latency but raises scheduling pressure. Increase it if playback crackles or overload counters rise. Keep the device rate compatible with the session and other applications using a shared driver. Installing an ASIO driver alone does not add that backend to the distributed WASAPI build.

## MIDI and monitoring {#midi}
Enable the intended MIDI input, select an instrument track and play. An audio track needs a source or recording input; a MIDI clip needs an instrument to turn notes into audio. Arm the intended track and verify input and monitoring before recording. Ctrl+K switches the computer keyboard's note-playing function; octave and velocity are set in the transport.

## Storage and portability {#storage}
Options > Locations lists caches, devices, presets, banks, exports and mode-specific sessions. Use this panel to change managed folders. Large banks can live on a data drive; build caches and source samples serve different purposes. Keep a saved project and all audio it references when moving work.

If setup looks correct but nothing is audible, follow [troubleshooting](troubleshooting.md#silence). Continue with [the workspace](workspace.md) and [recording](studio.md#recording).
