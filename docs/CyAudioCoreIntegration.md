# CyAudioCore integration — CyDSP-CORE

CyDaw integrates **CyAudioCore**, which manages **CyDSP** internally as its
numerical dependency. Service APIs are `cyaudio`; numerical processors remain
`cydsp`. The application does not add a second independent audio framework or
copy either library into its tree.

Validated source revisions:

- CyAudioCore: `bb26e93604d43de2069694b0791bc0bcf5a53ea6`.
- Its numerical dependency CyDSP: `cc18dca043925b75adea481c1c8545a125607e1f`.

The [snapshot](reports/CyAudioCoreSnapshot.json) records source hashes, including
private/vendored compilation inputs. Both checkouts were clean. The previous
monolithic CyDSP reports are archived; their VST3 blockers are fixed upstream.
See the [current French report](CyAudioCoreIntegrationReport.fr.md) for the precise
boundary: **the complete desktop still contains JUCE**.

## Dependency ownership

`cmake/CyAudioCore.cmake` supplies `CYAUDIOCORE_CYDSP_SOURCE_DIR` and adds only
CyAudioCore. CyAudioCore adds/links its numerical dependency. The DAW's shared
engine links `CyAudioCore::Core`; file, device and plugin adapters link the
corresponding CyAudioCore targets. Native internal plugin exports consume the
same engine and transitive numerical headers without JUCE. Static archives only
pull referenced objects; numerical consumers do not link the plugin-host SDKs.

Default source directories are sibling `../CyAudioCore` and `../CyDSP`. Override
`CYDAW_CYAUDIOCORE_SOURCE_DIR` and `CYDAW_CYDSP_SOURCE_DIR` as needed. Environment
variables of those names supply defaults for clean CI configurations. A missing
or obsolete public API fails configuration explicitly. Upstream tests are opt-in
and disabled here; the integration has its own acceptance tests. Do not enable
global fast-math/AVX flags: CyDSP owns its per-kernel ISA dispatch.

`CYAUDIOCORE_WITH_PORTAUDIO=ON` enables CyAudioCore's optional provider. This does
not switch the desktop's device manager to PortAudio. The current desktop still
uses its established JUCE device/MIDI integration; no saved audio setting is
changed by this migration.

## Production paths

- Default audio readers and WAV/AIFF/FLAC/Vorbis encoders use CyAudioCore. The
  application boundary is `CyAudioCoreAudioFormats`, including Fonderie/Cinema.
- Versioned WAV markers, labels, notes, regions, sampler loops and RIFF INFO tags
  retain the application's metadata vocabulary. The `cydsp.format` and
  `cydsp.metadata` keys intentionally remain stable. Extra historical metadata
  retains the explicit legacy parser/writer where conversion is not implemented.
- Aligned engine storage, convolution/stretch FFTs and chroma analysis use CyDSP.
  Engine copy/fill/summing now also use prepared VectorOps dispatch. These same
  routines ship in internal VST3/CLAP/LV2 processors.
- Existing musical processors and project parameters retain their algorithms.
  The new multibus graph, external sidechain dynamics and meters are available
  through the numerical dependency and validated in composition with CyAudioCore
  sources. Availability does not imply every new processor has a DAW UI node.
- CyAudioCore's host and separate worker are tested independently of JUCE. They
  do not silently replace CyDaw's current v6 IPC protocol or state envelopes.
- Build outputs and runtime/native-plugin packages carry both libraries' original
  licenses, manifests and third-party notices. The packager checks their presence.

## Build and tests (Windows x64 / VS2022)

```powershell
cmake --preset cyaudio-core
cmake --build --preset cyaudio-core --parallel 8
ctest --preset cyaudio-core

cmake --preset cyaudio-desktop -DCYDAW_JUCE_SOURCE_DIR=build/desktop/_deps/juce-src -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyImgui_Interface
cmake --build --preset cyaudio-desktop --target CyDAW2 CyDAWPluginWorker CyDAWGraphRuntime_VST3 CyDAWTestGain_VST3 cydaw_cyaudio_integration_tests cydaw_cyaudio_services_tests cydaw_plugin_ipc_tests --parallel 6
ctest --test-dir build/cyaudio-desktop -C Release -R '^cydaw_(cyaudio_integration_tests|cyaudio_services_tests|plugin_ipc_tests)$|^cydaw2_(dj|foundry|processing|editing|sample_search|mixing|project_import)_check$' --output-on-failure
```

Adjust local JUCE/CyUI paths, or use the existing pinned fetch setup. Former
`cydsp-core` / `cydsp-desktop` presets remain compatible entry points but now use
the split libraries. Test target names use `cyaudio`.

```powershell
python tools/audit_cydsp_integration.py --output output/cyaudio-inventory.json
build/cyaudio-core/Release/cydaw_cyaudio_services_tests.exe --plugin vst3 "path/to/our-plugin.vst3"
build/cyaudio-core/Release/cydaw_cyaudio_services_tests.exe --plugin clap "path/to/our-plugin.clap"
build/cyaudio-core/Release/cydaw_cyaudio_services_tests.exe --plugin lv2 "path/to/our-plugin.lv2"
```

The service executable handles the intentionally unchanged upstream
`--cydsp-worker` command. Persistent signatures such as `CyDSP.VST3State1` and
`CyJSFX` are unchanged upstream; renaming a project/namespace must not rewrite
saved binary identifiers. The original sparse-slider JSFX fixture also checks
exact event offsets and state round trips.

## CI and distribution

The native/alpha workflows check out the two tested source revisions using the
local `audio-dependencies` action. For private dependencies the repository needs
`CYDAW_DEPENDENCIES_TOKEN` with read access to both repositories; otherwise the
job token can read public dependencies only. No new workflow run or publication
is performed by this local integration. No credentials persist in dependency
checkouts; `.dependencies` is ignored. Documentation-only jobs do not need these
sources. `cmake --install --component CyDAWRuntime` also installs both notices.

[Acceptance evidence](reports/CyAudioCoreAcceptance.json) distinguishes execution
from inspection. Hardware/ASIO, Linux/macOS, installed third-party plugins and
native editor interoperability still require their own validation.
