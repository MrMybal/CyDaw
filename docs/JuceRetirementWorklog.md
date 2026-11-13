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
