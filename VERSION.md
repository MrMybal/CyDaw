# CyDaw — Version notes

[Back to README](README.md) · [Français](VERSION.fr.md)

**0.79.0 · Alpha 1 · Windows x64**<br>
**Release tag: `v2-0.79.0-alpha.1` · 17 November 2026**

CyDaw is the public product name. Build targets, the executable `CyDAW 2.exe` and versioned release tags retain internal naming.

## Native audio engine

This Alpha replaces the JUCE integration with **CyAudioCore**, using its **CyDSP** dependency for native DSP. The desktop, isolated plugin worker, audio/MIDI devices, file codecs and native VST3/CLAP/LV2 exports build without JUCE. Obsolete JUCE adapters, editors and authoring tools have been removed; historical comparison reports remain available.

- WASAPI and ASIO through the application-side PortAudio/GPL SDK integration.
- Isolated VST3, CLAP, LV2 and JSFX hosting, including plugin sleep support.
- Direct controller/MIDI bridge for the computer keyboard and programmable controllers.
- Named device controls, clearer library filtering, Master/bus automation in Arrangement and wider MIDI editor zoom.
- Configurable interface frame rate and integrated Windows title controls.
- Fixed ASIO CUE returns, exact-length WAV recording and silence after DJ pause across Studio/Cinema transitions.
- Illustrated English/French CyDoc help, available offline in the complete archive.

[Download Windows Alpha](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.1/CyDAW2-0.79.0-Windows-x64-Alpha.zip) · [Release status](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.1) · [SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.1/SHA256SUMS.txt)

Extract the complete ZIP and keep its DLLs, documentation, fonts and notices beside the executable. Factory sample banks are downloaded separately. Personal test projects and unfinished sound models are excluded.

## Validation and remaining limits

See the [native release report](docs/NativeRelease.fr.md), [comparison results](docs/CyAudioCoreComparison.fr.md), [CUE/export update](docs/CyAudioCore040Update.fr.md) and [cross-mode audio fix](docs/CinemaDcFix.fr.md).

The migration does not promise bit-identical rendering of every old project. The historical project 08 comparison still has an approximately **0.49 dB** chain-level difference, and a first-block reload difference remains. These findings are preserved. Linux/macOS application builds are not part of this release; macOS plugin export remains deferred. Native plugin compilation requires the source tree and suitable C++ tools. Plugin compatibility and project imports must be checked with the user's own material.

The previous JUCE main is preserved by `archive/juce-main-2026-11-17`. Older packages remain in the [release history](https://github.com/MrMybal/CyDaw/releases).
