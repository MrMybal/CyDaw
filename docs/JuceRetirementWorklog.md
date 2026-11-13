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
