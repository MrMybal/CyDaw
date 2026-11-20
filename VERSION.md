# CyDaw — Version notes

[Back to README](README.md) · [Français](VERSION.fr.md)

**0.79.0 · Alpha 4 · Windows x64**<br>
**Release tag `v2-0.79.0-alpha.4`**

CyDaw is the public product name. Build targets, the executable `CyDAW 2.exe` and versioned release tags retain internal naming.

## Alpha 4 · Local MCP access profiles

- CyMCPSDK 0.7.2 adds four local MCP access profiles. **Free** is the default when no profile was selected. After enabling the server, the loopback address is enough to connect and edit a session.
- **Local controlled** checks session permissions, **Token only** requires a bearer token and **Protected** requires both the token and permission to edit. Saved selections carry over.
- Use **Help → AI assistant → Enable local MCP**, then **Copy address**. Argument validation still applies. The bilingual offline guide documents the profiles and command-line options.

[Download Alpha 4](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.4/CyDAW2-0.79.0-Windows-x64-Alpha.zip) · [Release](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.4) · [SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.4/SHA256SUMS.txt)

## Alpha 3 · AI assistant and automation

- Open **Help → AI assistant** to connect to a supported API or local model service. The chat, `cydawctl` and the local MCP endpoint share 50 tools.
- Control graphs, Cinema, DJ Mix, Foundry and Plugin Workshop alongside Studio. Session changes require **Allow session edits**. Import and export tools use the selected media and export folder.
- The portable package includes the CLI, the CyMCPSDK notices and the updated English/French offline guide. [Assistant and MCP guide](https://mrmybal.github.io/CyDaw/manual/Documentation.html#/en/mcp).

[Download Alpha 3](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.3/CyDAW2-0.79.0-Windows-x64-Alpha.zip) · [Release](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.3) · [SHA-256](https://github.com/MrMybal/CyDaw/releases/download/v2-0.79.0-alpha.3/SHA256SUMS.txt)

## Alpha 2 · Instruments and session delivery

Play VST3 and CLAP instruments from MIDI and route their additional outputs to the mixer. The update also introduced tempo mapping and session rendering. [Alpha 2](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.2).

## Alpha 1 · Native audio engine

This Alpha replaces the JUCE integration with **CyAudioCore**, using its **CyDSP** dependency for native DSP. The desktop, isolated plugin worker, audio/MIDI devices, file codecs and native VST3/CLAP/LV2 exports build without JUCE. Obsolete JUCE adapters, editors and authoring tools have been removed; historical comparison reports remain available.

- WASAPI and ASIO through the application-side PortAudio/GPL SDK integration.
- Isolated VST3, CLAP, LV2 and JSFX hosting, including plugin sleep support.
- Direct controller/MIDI bridge for the computer keyboard and programmable controllers.
- Named device controls, clearer library filtering, Master/bus automation in Arrangement and wider MIDI editor zoom.
- Configurable interface frame rate and integrated Windows title controls.
- Fixed ASIO CUE returns, exact-length WAV recording and silence after DJ pause across Studio/Cinema transitions.
- Illustrated English/French CyDoc help, available offline in the complete archive.

[Alpha 1](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.79.0-alpha.1).

Extract the complete ZIP and keep its DLLs, documentation, fonts and notices beside the executable. Factory sample banks are downloaded separately. Personal test projects and unfinished sound models are excluded.

## Validation and remaining limits

See the [native release report](docs/NativeRelease.fr.md), [comparison results](docs/CyAudioCoreComparison.fr.md), [CUE/export update](docs/CyAudioCore040Update.fr.md) and [cross-mode audio fix](docs/CinemaDcFix.fr.md).

The migration does not promise bit-identical rendering of every old project. The historical project 08 comparison still has an approximately **0.49 dB** chain-level difference, and a first-block reload difference remains. These findings are preserved. Linux/macOS application builds are not part of this release; macOS plugin export remains deferred. Native plugin compilation requires the source tree and suitable C++ tools. Plugin compatibility and project imports must be checked with the user's own material.

The previous JUCE main is preserved by `archive/juce-main-2026-11-17`. Older packages remain in the [release history](https://github.com/MrMybal/CyDaw/releases).
