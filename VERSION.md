# CyDaw — Version notes

[Back to README](README.md) · [Français](VERSION.fr.md)

**Published application: 0.78.0 · Alpha 1**<br>
**Stage: ALPHA · Windows x64**<br>
**Release tag: `v2-0.78.0-alpha.1` · 3 November 2026**

CyDaw is the public product name. The release tag, build target `CyDAW2` and `CyDAW 2.exe` retain internal version naming.

## Published Alpha 1

The [official release](https://github.com/MrMybal/CyDaw/releases/tag/v2-0.78.0-alpha.1) was built from [c159b7d](https://github.com/MrMybal/CyDaw/commit/c159b7d08ef61bcf64b6df97b3bd8f27e7fe2712). Its notes describe:

- Studio, DJ mix, Foundry, Cinema, Processing and Plugin workshop.
- Native instruments/effects, isolated VST3/CLAP/LV2/JSFX hosting and video codecs.
- Modular CyUI controls, themes, native source editors, Cinema preview and master property drafts.
- Windows Audio / WASAPI; this package does not enable ASIO or VST2.
- Separately downloaded factory banks; local examples and unfinished sound models may be absent.

[Download the application ZIP](https://github.com/MrMybal/CyDaw/releases/download/v2-0.78.0-alpha.1/CyDAW2-0.78.0-Windows-x64-Alpha.zip) · [SHA-256 checksums](https://github.com/MrMybal/CyDaw/releases/download/v2-0.78.0-alpha.1/SHA256SUMS.txt). Keep the complete extracted directory, DLLs and notices.

## Current source updates after that release

The current tree still declares application version **0.78.0** in [CMakeLists.txt](CMakeLists.txt). These subsequent updates do not imply a rebuilt Alpha 1 binary:

- Offline documentation migrated to the pinned CyDoc 0.1.0 reader, with current English/French guides and public API references.
- An illustrated bilingual product website, a separate HTML manual, compatibility redirects and automated Pages deployment.
- Public branding uses **CyDaw** without the internal suffix; README/version information now has complete English/French editions.

See [commits since Alpha 1](https://github.com/MrMybal/CyDaw/compare/v2-0.78.0-alpha.1...main), the [online manual](https://mrmybal.github.io/CyDaw/manual/Documentation.html#/en/welcome) and [release history](https://github.com/MrMybal/CyDaw/releases). The download above remains the published Alpha 1 package.

## Known issues

The published release notes do not provide an itemized confirmed-bug list. Check the [issue tracker](https://github.com/MrMybal/CyDaw/issues) for reports or [open a report](https://github.com/MrMybal/CyDaw/issues/new). The boundaries below are documented limitations, not invented bug tickets or a claim that there are no bugs.

## Documented limitations

- The published application is Windows x64 only; source presets and Linux plugin-export targets are not Linux/macOS application downloads.
- External VST3/CLAP/LV2/JSFX hosting currently uses audio tracks. It does not provide external instruments on MIDI tracks or effects on hardware buses/master. CLAP thread-pool/per-note-modulation extensions are not advertised.
- Native plugin compilation needs the source tree and appropriate tools. Linux exports need X11/OpenGL and verification in a Linux host; macOS export is deferred. Some nodes/containers, including Drum Machine pads, are excluded; run the workshop export checker.
- Editable `.cydawdevice` transfers list external media without copying whole banks. Imported projects vary in fidelity; compare against a reference render.
- Audio/video codecs depend on supplied shared libraries. Sound banks and optional search models are separate downloads. Sound models and local examples may be absent.
- Transport protocols require compatible peers and do not carry audio or make different devices sample-locked.
- The online/current-source manual can differ from the older package’s embedded help. A README or website update does not create a new application release.

## References

- [Plugin formats and export limits](docs/PLUGIN_FORMATS.md) · [Transport](docs/TRANSPORT_SYNC.md) · [FFmpeg](third_party/FFMPEG.md).
- [Application license](LICENSE) · [Factory credits](content/factory/LICENCE.txt).
