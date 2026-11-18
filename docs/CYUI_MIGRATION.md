# CyDAW 2 — modular CyUI integration

The published fallback CyUI pin is `7a793e9281e78572818d85e9cf856d779a7792a1` (1.2.0),
from https://github.com/MrMybal/CyUI, under its [MIT licence](https://github.com/MrMybal/CyUI/blob/7a793e9281e78572818d85e9cf856d779a7792a1/LICENSE).
The complete licence and upstream notices are shipped in `Licenses/CyUI`, so
reading them does not require access to the source repository.
Integration uses FetchContent;
there is no copied CyUI implementation in the application. `src/v2/CyUI.cmake`
selects modules before adding the library and supplies the host's `cydaw_imgui`
target. No second ImGui library, demo, gallery, browser/CEF or Unreal adapter is
built. The existing Windows ImGui/D3D11 host and its context lifecycle remain.

## Selected modules and ownership

| Module | CyDAW use |
| --- | --- |
| Core | Common palettes, all five themes, DPI/density metrics, fonts, icons, numeric fields, toolbar buttons, tooltips, clipped/empty-state text, notifications and locale |
| Audio | Knobs, faders, peak/RMS/hold meters throughout the studio, graph cards, device windows and native plugin UI |
| AudioViews | Prepared before/after spectra with gradient fills, oscilloscopes, chronological spectrograms and persistent stereo density; no audio decoding or FFT runs in CyUI |
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

## Offline user manual

The user manual now uses CyDoc's local browser reader. CyUI DocumentViews is no
longer selected or linked. Images remains available for Cinema. Native exported
plugins retain their smaller explicit module closure. See [CyDoc integration](../.CyDocBuild/README.md).


## AudioViews update — 2026-11-14

The pinned update adds spectral fills and persistent spatial displays. CyDaw now
uses these in its existing analyzers; it does not add fictitious measurements or
change the audio processors:

- Before/after spectrum: the processed spectrum uses a translucent gradient area
  down to the existing dB floor; the input remains a line for comparison.
- Spectrometer: CyUI renders the host's 256-bin, 240-row history in vertical time,
  with newest data at the top, a logarithmic frequency axis, historical-cell
  tooltips and a legend in the large view. CyDaw repacks the ring only when the
  analysis history revision changes. Drawing is capped at 16,384 cells. The
  existing spectrum, peak hold, floor, tilt, smoothing and history controls stay
  available; double-click still clears held peaks.
- Stereoscope: the goniometer uses CyUI's persistent density view. Fresh analysis
  revisions feed points once, even for repeated identical audio samples; drawing
  the same revision again does not increase its energy. Small/large panels use
  bounded 64×64/96×96 grids with separate state per ImGui context and panel.
  Channel projection preserves CyDaw's existing R-L horizontal orientation.
  Persistence, wheel zoom, double-click reset, polar mode and correlation history
  remain available. The polar mode keeps its specialized host renderer.

FFT, measurements, prepared audio taps and processor state remain in CyDaw /
CyAudioCore / CyDSP. The update adds no work or allocations to the audio callback
and does not enable another CyUI module. Spatial frequency maps, LUFS and true
peak displays are not added without the corresponding host measurements.

The renderer-independent CyUI test also covers chronological history wrap,
vertical spectrogram rendering, gradient spectrum rendering, bounded stereo
storage, independent states and suppression of duplicate input revisions.


Validation of this update (Windows x64, MSVC 19.40, Release):

- Seven targeted CTest checks pass: CyUI integration, frame pacing, no-JUCE
  production graph, Cinema layout, routing, mixing and creative effects.
- Silent isolated sessions rendered compact and expanded analyzers with English
  and French application navigation. Captures were visually inspected, including
  the spectrogram legend and the preserved polar view. Device interiors retain
  the application's existing English-only module policy.
- A real wheel gesture changes stereo zoom from 1 to 1.10517; double-click
  restores 1. The two-analyzer chain's offline level difference is below
  0.000001 dB. ASIO output-only QA sessions use 48 kHz / 1024 frames with speaker
  output silenced; no capture channels are enabled.
- Local evidence (configuration/build logs, JUnit, images, source/binary SHA-256,
  dependency revisions) is in `output/cyui-audioviews/2026-11-14`.

The targeted tests can be rerun after building the desktop and test targets:

```text
ctest --test-dir build/cyui-audioviews -C Release --output-on-failure -R "^(cydaw_cyui_tests|cydaw_frame_pacing_tests|cydaw_no_juce_production_graph|cydaw2_(routing|creative|mixing|cinema_layout)_check)$"
```

Run application checks in an isolated QA profile, as for the existing native
suite. This update does not establish a CPU performance improvement or validate
Linux/macOS execution; the bounded draw budgets are implementation limits, not
benchmark results. The earlier DSP comparison evidence remains unchanged.

## Local envelope API integration (2026-11-18)

The new curve APIs are currently uncommitted in `D:/_Project/CyImgui_Interface`.
The local validation build explicitly overrides the FetchContent source directory;
it does not modify, commit or publish the dependency checkout. The fallback pin
above does not contain these APIs. `CyUI.cmake` now rejects an outdated SDK with
a message identifying the override, rather than compiling a second local fill.
Update that published pin only after an upstream commit containing the APIs exists.

Prisme and SynthView call `cyui::drawCurveFill` directly with their existing
screen points, palette/source colors and opacity. Their outlines are still drawn
once by the host. The uncommitted `ui::fillUnderCurve` helper has been removed.
The original sampled interpolation, stage widths, nonlinear time mapping, dp
sizes, drag handles, knob bindings and live-level overlay are unchanged. No audio
engine, envelope parameter or DSP source is modified.

The read-only `cyui::envelope` widget is exercised with host-provided points and
AHDSR stages in the integration tests. It is not forced into these two custom
canvases: SynthView owns editable handles and stage widths, while Prisme uses its
specific padding and source color. The current component exposes only categorical
colors and fixed inset/phase-label geometry. Using it there would change required
colors or layout. Standard displays can use it; a direct color/padding option
would allow a future Prisme migration without recoloring vertices or changing the
shared palette temporarily.

Validation covers zero sustain, zero hold, zero stage durations, repeated points,
NaN/Inf gaps, baseline crossings, nonlinear host curves, and 75/100/150/200 percent
DPI geometry. Coverage checks inspect actual triangles instead of requiring the
former helper's exact index count. They also ensure filling reserves no item and
does not move the host cursor or replace existing interaction targets.

Prisme is captured in a separate hidden 1800x1100 process, with a private temporary
profile and only CyAudioCore's virtual audio backend. Both ENV 1 and the initially
selected ENV 2 receive the test values. Captures cover zero sustain, nonlinear
curves, zero durations and the live indicator during playback. Host parameters
are checked again after drawing. The user's open session is left untouched.

Reproduce with the previously configured native build and its frozen audio SDKs:

```powershell
cmake -S . -B build/cyaudio-sleep `
  -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyImgui_Interface `
  -DCYDAW_DESKTOP_RUNTIME_ROOT=D:/_Project/CyDAW_Project/build/cyaudio-sleep/CyDAW2-envelope_artefacts
cmake --build build/cyaudio-sleep --config Release --target CyDAW2 cydaw_cyui_tests --parallel 2
ctest --test-dir build/cyaudio-sleep -C Release --output-on-failure `
  -R '^cydaw_(cyui_tests|envelope_views|no_juce_production_graph|native_headless)$'
```

Source hashes before/after, compiler logs and JUnit results are retained under
`output/cyui-envelopes-2026-11-18`. Captures and the isolated-process results are
under `build/cyaudio-sleep/envelope-views-check`. The new renderer calls use only
portable CyUI/ImGui APIs; the visual test is attached to the existing Windows
desktop test block. Linux/macOS builds are not claimed as executed on this host.

[Measured integration evidence and immutable source hashes](reports/CyUIEnvelopes.json).
