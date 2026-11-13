# Active JUCE retirement work

The user explicitly requires the COMPLETE migration. Do not stop after another
partial integration report. Target: the production desktop and plugin worker
configure/build/run without JUCE includes or libraries, preserving the actual
application, editing, modes, MIDI, plugin formats and document functionality.

Branch: CyDSP-CORE. Starting checkpoint: 58f8c87. CyAudioCore and CyDSP sibling
repositories are read-only and unchanged. Use their public APIs. Commit meaningful
validated checkpoints locally; do not push without a new request.

## Active implementation

- Added original application foundation in include/cydaw/base and src/base:
  UTF-8 Text, containers, Result, Identifier, document Value/Object/JSON,
  native/std filesystem/streams/memory/time. Nlohmann JSON 3.11.3 is imported
  unmodified with its MIT notice and provenance.
- Real NativeDspGraphState and ProjectRecovery implementations now compile in
  the common no-JUCE core. New tests exercise the actual graph and project
  serialization, atomic save/read/recovery, Unicode and integer identities.
- Converted Value/Object/JSON, Result and Identifier use in application sources.
  The desktop build is being repaired incrementally, not yet validated at runtime.
- Generic document/text conversion templates currently permit old boundaries;
  remove those boundaries during the complete port. Their existence does NOT
  constitute completion. The same applies to src/app/ProjectRecovery.h and
  NativeDspGraphState.h adapters retaining file/text types.

## Continue work

1. Finish the current document-port desktop build; inspect only unique errors
   from output/cyaudio-document-port-build.log. Last iteration had two errors:
   Array::addArray needed its std::vector base insert (fixed), and a const char*
   name erroneously got .toRawUTF8() in RemoteCommands.cpp (fixed).
   Identifier was then ported across sources; rebuild after the running build.
2. Build/run cydaw_base_tests in build/cyaudio-core and the actual application
   session checks before considering this foundation checkpoint validated.
3. Port the remaining text/file/concurrency/platform APIs, separate the hidden
   legacy graph component's data model from GUI, and replace the native window/
   event loop with the application's Win32/CyUI implementation.
4. Replace remaining source/transport/recorder, MIDI/voice bookkeeping and
   plugin-host boundaries using CyAudioCore/CyDSP and application-owned code.
   Preserve behavior; no fake no-op features and no rename of JUCE source.
5. Make a strict no-JUCE production build and run the complete relevant tests.

Important discovery: generated native VST3 exports already use NativePlugin.cmake
and the official MIT SDK with NO JUCE, just like CLAP/LV2. The historical
CyDAWGraphRuntime JUCE plugin is a different target/fixture. Do not repeat the
previous report's overbroad claim that all VST3 exports still need JUCE.
NativeVisuals.cmake already ports shared drawing to PortableString.h. Reuse the
existing original native editor/platform implementations where appropriate.

Tooling: cmake/ctest from Visual Studio 2022 Community; see integration guide.
File writes/builds/Git mutations generally need require_escalated on this host.
Do not spawn agents unless authorized by user/instructions. No applicable local
AGENTS.md was found. All changes since 58f8c87 currently belong to this work.

## Validated document checkpoint

- No-JUCE base/real graph/project recovery test passed.
- Compatibility tests caught and fixed preservation of empty tokens and the
  previous numeric-display behavior. JSON Unicode must be supplied explicitly
  as UTF-8 to the old runtime oracle; its const-char constructor uses ANSI.
- The complete unchanged production format scenario passed with VST3/CLAP/LV2,
  JSFX reload/tempo/error preservation, project save/reload and export generation:
  output/cyaudio-documents-runtime/a2d885587576416a96f60f072a315ae5/checks.json.
- One earlier run failed the live meter after invalid JSFX compilation; a debug
  run and the final unchanged scenario passed. Do not weaken the assertion.
- Source File/Value/Result/Identifier ports compile in the desktop. Generic
  text/document boundary adapters still exist and the application STILL links
  JUCE. Continue with actual text/file/window/source/MIDI/plugin-host removal.

## Native text, files, preferences and audio-file checkpoint (current work)

- Source text and file/time APIs now use original base::Text/TextList/File/Time.
  Only explicit old external ABI overrides remain typed with JUCE until those
  interfaces are removed. Generic boundary conversion templates still exist.
- Added original XML tree and preference storage using pinned pugixml 1.15 MIT.
  Existing PROPERTIES XML, nested device XML, Unicode and case-sensitive keys
  are preserved. Malformed settings are not overwritten. Saves are atomic.
- Added original audio::AudioFormatReader/Writer/Manager and options. Real
  sample decoding/encoding runs through CyAudioCore; no JUCE codec fallback.
  Typed markers/regions/loops and opaque native preservation use Core's metadata.
  Unsupported metadata export fails explicitly rather than silently dropping it.
- Recording uses a preallocated bounded SPSC sample queue and a disk worker.
  Waveform thumbnails now use two background readers and immutable peak data;
  no JUCE AudioThumbnail. The preview uses the existing application segment
  source pending the source/transport port.
- Moved original plugin-interface data/serialization/validation out of graphical
  ModuleInterfaceEditor into document/ModuleInterface.h + ModuleInterfaceState.cpp.
  The actual implementation now compiles and tests in the common no-JUCE core.
- Base and native audio-file tests pass without JUCE, including graph/interface
  documents, old preferences, atomic saves, recording drain, actual PCM samples,
  bounds/silence and markers. The actual app format session passed:
  output/cyaudio-native-files-runtime/5e66d34482db485b85010e802771bfaf/checks.json.
- Initial regressions: Mix passed. Foundry correctly edited/exported audio but
  timed out saving session; text replacement copied the remaining string at
  each newline (quadratic). Fixed exact replacement with a linear byte scan.
  Project import passed Reaper/FL/Ardour/LMMS/Zrythm/AAF except MIDI-neighbour
  stems; native findChildFiles now supports semicolon-separated masks.
  Both unchanged regression scenarios are being rerun; inspect
  output/cyaudio-native-files-regression-fixes.log. No assertions were weakened.
- Native export build found a pre-existing notices path problem when called
  from an exported project: PROJECT_SOURCE_DIR named the outer export project.
  cydaw_copy_audio_notices now anchors to its own definition directory. Inspect
  output/cyaudio-native-text-export-build2.log for the rerun.
- Formatting reordered Windows includes; fixed windows.h before shell headers.

Next: finish these reruns and commit this validated checkpoint, then CONTINUE
complete retirement (do not end the user task with another partial report).
The desktop STILL links JUCE. Audio device/source/MIDI/plugin/window/platform
boundaries and the hidden graph model need actual removal.

Graph separation preparation: tree-sitter installed ONLY under ignored
output/migration-tools (MIT helper tooling, not an app dependency). Method maps
are in output/graph-model-functions.json, graph-model-roots.json and
 graph-model-reached.json. About 292 methods / 15k lines remain after excluding
old UI refresh paths. New V2 uses these original data/compile/edit methods but
never shows the old JUCE component. Separate those from UI, preserving actual
algorithms, notifications, validation and history. Do not create dummy GUI
compatibility classes. Existing modern GraphEditor already handles its own
navigation, drawing, selection and embedded device editors. The old model's
refreshTranslations should only apply translated type titles in the new model.
ModuleInterface data has already been separated; do not duplicate it again.
The maps are analysis artifacts, not final generated source, and may have old
line numbers after edits. Parse the current files when extracting.

Confirmed: both unchanged failing scenarios now PASS (Foundry 5.70s, project
import 7.26s), including session reload. Base/native audio-file tests PASS.
Native CLAP/LV2 exports rebuild after the notices fix; check their protocol/editor
runner results in output/cyaudio-native-text-export-tests.log before committing.

Native runner in build/cyaudio-native currently selects LV2: its real audio,
state/parameters and editor protocol test PASSED (0.70s). Do not infer a fresh
CLAP build from this single configured target. The app session independently
hosted the existing CLAP/LV2/VST3 fixtures. Staged Gitleaks scan: 3.26MB, clean.

## Next services phase after e1fbd3f (uncommitted)

- Native UTF-8 translation embedding in cmake/Translations.cmake replaces
  juce_add_binary_data. Byte-array generation avoids MSVC literal limits.
  cydaw_localization + Localization have no JUCE lifecycle, locks or broadcaster.
  Existing app refreshes languages explicitly; no listeners used the old signal.
- Original base::Thread/ThreadPool/ThreadPoolJob/WaitableEvent and lock guards
  replace JUCE types across sources. Actual OS thread naming/priority, cooperative
  cancellation, restart, TLS job cancellation and task ownership are implemented.
  Recorder no longer creates unused JUCE TimeSliceThread writers; its own bounded
  native writer owns the disk thread. Old TimeSlice reader boundaries still exist.
- Actual desktop NativeDspRuntime buffer adapter is generic and compiles in Core,
  without a JUCE audio buffer include. A real gain-graph test processes samples.
- cydaw_assets now compiles actual SamplerMapFile, SoundModelFile,
  GraphPluginProjectGenerator and PolySynthState with NO JUCE.
  Sample buffers use CyDSP; background workers use native threads. Explicit
  SampleStore::shutdown is called after Studio dies. Historical GraphRuntime
  plugin has a per-module last-user lease to stop sample workers before unload.
- Original polyphonic serialization/restore and voice/replacer asset attachment
  moved out of the JUCE renderer into document/PolySynthState.h + PolySynthState.cpp.
  Do not re-extract ModuleInterface or PolySynthState; already done.
- Imported miniz 3.1.0 MIT unmodified, SHA256 provenance + generated static export
  header. base::InflateStream supports zlib/raw/gzip, bounded caller reads, seek,
  gzip headers/CRC/trailers; Base64 utility uses standard encoding. SoundModel
  now uses these functions and rejects failed compressed-table checksums.
- Standalone no-JUCE tests pass: thread restart/events/queue/cancellation;
  exact embedded en/fr resource bytes and language selection; deferred sample
  load/cache/original PCM; actual compressed spectral model and corrupt rejection;
  actual polyphonic state roundtrip; external Python zlib/gzip/raw fixtures with
  1/17/4096/16384-byte reads, rewind, corruption; real native gain graph.
  Tests and logs: output/cyaudio-native-*-test-build.log and
  output/cyaudio-native-plan-state-build.log.

Current build: output/cyaudio-native-services-desktop-build.log (session returned
by exec). Must verify this full desktop build, then unchanged app format scenario
and relevant regressions before committing services phase. Previous full desktop
build with native sample assets passed; newest PolySynthState split is pending.
Native miniz notice copied/installed/required by package test. Need final format,
Gitleaks staged scan and commit after validation. No push.

Remaining: complete GraphDocument separation (no new GraphDocument file yet),
platform window/event loop/dialogs/process/socket/image services; source/transport/
MIDI/voice/device/plugin-worker ABIs. The production desktop STILL links JUCE.
Do NOT end task with another partial report. Continue to strict no-JUCE build.

The native service checkpoint also extracts the actual polyphonic instrument plan state and asset attachment code into PolySynthState, compiled without JUCE. The unchanged desktop format scenario passed (output/cyaudio-services-runtime/132af7b4b21d48d3bf3467e97d6b5ce4), as did DJ, Foundry and project import (38.80 s total). Thread, localisation, compression and sample/model service tests passed. The production desktop still requires JUCE; graph document separation is the next step, not a claim of completed retirement.

The actual V2 GraphSlot now owns GraphDocument, an application model extracted from the original catalogue, persistence, graph editing and compilation algorithms. It is compiled as cydaw_graph_document without JUCE. Its native tests exercise catalogue/factory data, editing notifications, JSON restore, undo/redo, alias changes, layout and native effect/instrument compilation. The model owns no window or legacy controls; V2 ticks deferred wavetable work, validation and parameter notifications, preserving the 180 ms history coalescing window. The old NodeGraphComponent remains only in the historical JUCE graph-plugin fixture and legacy shell, pending removal from the final production configuration.

Desktop logos, PNG snapshots and FFmpeg video frames now use the app-owned straight RGBA Image service. Decode/PNG encoding reuse the already-vendored stb implementation (MIT/public-domain, with a separate distribution notice); scaling handles alpha before interpolation. GPU upload consumes contiguous RGBA directly rather than converting every pixel through a JUCE colour object. Native PNG/channel/alpha/resize/error tests pass. The desktop build with these changes succeeded. This is a checkpoint of the complete retirement, not completion of the production no-JUCE build.

The next native service phase routes Standard MIDI File parsing and serialization through CyAudioCore::Core, including the live engine import/export, MIDI/stem imports and Ardour regions. Independent SMF fixtures test running status, tempo/signature/name metadata, repeated-note pairing, zero-velocity note-off, roundtrip and rejection without destroying a previous document.

The application now owns EventLoop dispatch, periodic timers, async coalescing and weak callback lifetimes. The current JUCE shell temporarily drains that queue from its TickTimer; the native shell must take over that dispatch when it is replaced. The shared historical plugin UI ticks GraphSlot pending work as well. Tests cover dispatch ordering, cancellation, timer stop/destruction and weak-owner expiry.

Windows file selection uses IFileDialog (filters, multi-select, directories, overwrite prompts); cancelling or destroying a queued dialog prevents stale callbacks. This native dialog implementation is Windows-only at this stage. Other platform UI integration is not claimed validated. Windows process creation restricts inherited handles and preserves argument quoting; process/mapping/dynamic-library services are original OS implementations. Child argument, exit-code, timeout/kill, shared mapped-view and symbol tests pass. Native TCP sockets also replace the remote-control and Python-service JUCE sockets. The actual remote-control server compiles/tests without JUCE and enforces loopback binding, tokens, UI-thread dispatch and HTTP rejection.

Native platform checkpoint validation: all four standalone tests passed in 0.41 s (output/cyaudio-native-platform-tests.log). The desktop built with the native process, mapping, socket, timer and SMF services. Unchanged DJ, Foundry and project-import scenarios passed in 39.98 s. The actual JSFX/CLAP/LV2/VST3/save/reopen scenario passed at output/cyaudio-native-platform-runtime/e9b6059ad2c04f12b57c8b1907785a01. Native file-dialog cancellation is tested; interactive user selection has not yet been automated. Audio source/voice/MIDI-device ABIs, the production plugin-worker instance API and the main window shell still need removal of their remaining JUCE interfaces; continue to the complete production no-JUCE build.

Audio-source foundation (not yet switched into the production engine): SampleBuffer uses CyDSP aligned ownership and VectorOps, with allocation-free borrowed channel views, deep copies, bounded reuse, gain/ramp and meter operations. Native tests pass. AudioTransportSource delegates timeline/loop processing to CyAudioCore::AudioTransport; the callback alone produces its bounded command queue, while controls publish desired state. Lifecycle, subrange/chunk rendering, immediate seek visibility, pause/resume and gap-free looping tests pass without JUCE.

CyAudioCore ReadAheadSource currently prepares filesystem paths rather than an arbitrary application-rendered AudioSource. CyDaw clips render custom stretch/pitch curves through SegmentReaderSource, so an application ReadAheadScheduler/BufferingAudioSource caches those rendered pages on a worker. The callback takes page access with a single CAS attempt and does not lock, allocate or read files; a missing page is silent and counted. Native prefill/seek/end-of-file/release tests pass. The actual AudioTrackController and device SourcePlayer still use the legacy source ABI and must be switched next. No claim of production integration is made for this foundation until that switch and application scenarios pass.
