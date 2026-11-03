# CyDAW 2 — modular CyUI integration

CyUI is pinned to `8367401fe45fc22bfed5e227cb3de343177f676b` (1.2.0),
from https://github.com/MrMybal/CyUI, under its [MIT licence](https://github.com/MrMybal/CyUI/blob/8367401fe45fc22bfed5e227cb3de343177f676b/LICENSE).
The complete licence and upstream notices are shipped in `Licenses/CyUI`, so
reading them does not require access to the source repository.
Integration uses FetchContent, like JUCE;
there is no copied CyUI implementation in the application. `src/v2/CyUI.cmake`
selects modules before adding the library and supplies the host's `cydaw_imgui`
target. No second ImGui library, demo, gallery, browser/CEF or Unreal adapter is
built. The existing Windows ImGui/D3D11 host and its context lifecycle remain.

## Selected modules and ownership

| Module | CyDAW use |
| --- | --- |
| Core | Common palettes, all five themes, DPI/density metrics, fonts, icons, numeric fields, toolbar buttons, tooltips, clipped/empty-state text, notifications and locale |
| Audio | Knobs, faders, peak/RMS/hold meters throughout the studio, graph cards, device windows and native plugin UI |
| AudioViews | Prepared before/after spectra and oscilloscopes; no audio decoding or FFT runs in CyUI |
| Canvas / Plots | Double-precision graph transforms/grid and anchored timeline-band navigation; linear/log axes for automation |
| Nodes | Deterministic, cycle-tolerant graph arrangement; positions commit once through the existing graph history |
| Code / CodeEditor | Native JSFX/EEL2, processing, JSON interface and C++ component source editors; syntax, keyboard editing and editor Undo |
| Images | Cinema reference-video preview, fit/pan/zoom; decoding and texture lifetime stay in CyDAW |
| Inspector | Master gain/mute property drafts, live preview, commit, Escape/hidden-panel cancellation and external revision invalidation |

Canvas, Plots and Code are enabled by the selected modules' dependency closure.
Consumers link explicit `CyUI::<Module>` targets. There is no `CyUI::CyUI`
umbrella or `CYUI_MODULES=All` in production.

The application retains device bindings, MIDI learn, modulation rings/drop
routing, infinite-pointer Windows dragging, engine taps, FFT, effect response
calculation, waveform caches, transport/time units, validation, files, compilation,
project persistence and Undo/Redo. Source editors synchronize application-owned
buffers on edits and load/rebase only when the host document changes. Native
editor history does not replace graph or project history. JSFX syntax is an
application language registration in CyUI's shared registry; the ysfx compiler
remains the authoritative validator.

`ThemeRuntime.cpp` is shared by the DAW and JUCE-free native exports. Theme-file
JSON and filesystem discovery remain in `Theme.cpp`; legacy `.cytheme` keys,
including `dark`, and audio/MIDI/modulation signal roles remain supported.
Common colors come from CyUI; track colors use its categorical palette.
Each ImGui context owns its common palette, fonts, density and numeric-entry
state; CyDAW's extra signal roles are released on context destruction too.

Native VST3/CLAP/LV2 projects select only Core, Audio and AudioViews (and their
Canvas/Plots closure). They do not compile CodeEditor, Lexilla, Inspector, Nodes,
Images, the gallery, JUCE or the authoring interface. Their existing format SDK,
DSP engine and platform surface remain. CyUI/TextEditor/Lexilla/Scintilla notices
are included for the targets that use them, beside the application's notices.

## Remaining host extensions / upstream gaps

This migration deliberately keeps required capabilities that the pinned CyUI
model editors do not yet represent. It does not substitute a reduced editor:

- **Timeline:** the shared Timeline model is flat. CyDAW needs interactive nested
  time containers/sub-tracks on buses, loop/stretch/fade handles, comping, clip
  cross-editor drag/drop and MIDI editing. Arrangement, launcher, Cinema and
  Foundry therefore retain these domain editors. Their navigation bands use the
  shared double-precision Canvas transform. Timeline is not linked as an unused
  dependency.
- **Curves:** CyUI supports linear, step, smooth and Bezier points. CyDAW also
  needs B-splines, segment bend gestures, independently linked tangents, multi-key
  transforms and clipboard conversion across time units/value scales. Its full
  curve interaction layer and engine interpolation remain; axis mapping uses
  Plots. Curves is not selected simply to replace it with an incomplete UI.
- **Graph cards:** the generic Nodes editor has uniform card dimensions and no
  exposed socket/variable editor, heterogeneous embedded instrument layouts,
  routing containers or CyDAW's comment/port-promotion gestures. The application
  retains these card/connection interactions, using shared Canvas navigation and
  Nodes layout rather than copying CyUI's node implementation.
- **Time entry:** CyUI's numeric popup accepts decimal/comma/k values, but has no
  host unit-parser hook. A small context-owned CyDAW popup preserves `250 ms` in a
  seconds field and the inverse. All ordinary numeric entries use Core.
- **ImGui compatibility:** the pinned CyUI still calls the pre-1.92.8 draw-list
  overloads. ImGui 1.93's compatibility overloads are enabled consistently in the
  host, CyUI and native exports; the shared dependency itself is not patched.

No Python, Web, CEF or Unreal integration is used by this application.

## Build and verification

The normal desktop presets fetch the pinned dependency automatically. To use an
existing checkout for an offline developer build, set CMake's standard
`FETCHCONTENT_SOURCE_DIR_CYUI` cache override to that checkout (it must match the
pinned revision). No workstation path is stored in project files.

```text
cmake --preset desktop-windows-wasapi -DCYDAW_BUILD_V2=ON
cmake --build --preset desktop-windows-wasapi
ctest --preset desktop-windows-wasapi --output-on-failure
```

`cydaw_cyui_tests` is a renderer-independent integration test: it exercises
context/theme/DPI isolation, five themes in two densities, a disabled knob,
actual knob wheel edits and scroll ownership, source focus/typing/Undo and exact
UTF-8/tab/CRLF round trips. Existing workshop blueprint/designer integration tests
exercise typed ports, variables, presets, save/reopen, declared interfaces and C++
components against an isolated silent instance. UI captures and native export
load/render checks complement these tests. Tests must not run against a person's
open working session.

Validation for this migration uses Windows x64/MSVC. Linux/macOS UI execution and
cross-export builds have not been validated here; their existing source/backend
boundaries are preserved. A JSFX tempo propagation assertion in
`v2_plugin_formats_check.py` also fails identically on the unmodified 0.77.0 Alpha
(`dry = slow = normal = -12.2 dB`); it is an existing engine/worker issue, not a
passing migration check.

Verified on this revision:

- Clean Windows x64 configuration and Release build; six targeted CTest checks
  pass (UI integration, core, engine, transport timecode, worker and graph runtime).
- Workshop blueprint/designer checks, including C++ component compile/load,
  presets, typed ports and device save/reopen, pass.
- Cinema real UI checks pass: filmstrip, frame scrub, pointer-anchored zoom,
  double grab band, toolbar and panel restoration, with no ImGui layout errors.
  The test accepts an explicit `--ffmpeg` executable and `--encoder` for local
  verification without installing a separate encoder package.
- `v2_master_inspector_check.py` checks live drag preview, Escape rollback,
  hidden-inspector rollback and external value replacement in its own silent
  process (`--exe`, `--output`, optional `--port`).
- Fresh native VST3, CLAP and LV2 exports compile and pass their hidden native
  audio/parameter/state/editor load/render checks.
