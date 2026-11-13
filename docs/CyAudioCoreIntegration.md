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

The desktop now opens native audio devices through CyAudioCore. A temporary
`juce::AudioIODevice` adapter preserves the application's callback/preferences
boundary; it does not call JUCE's native audio drivers. Windows ASIO/WASAPI/
DirectSound JUCE backends are disabled in these targets. MIDI and the historical
source graph still use JUCE and remain separate migration work.

Windows desktop builds enable `CYDAW_ENABLE_ASIO` by default. CyDaw extends the
CyAudioCore PortAudio build with its own GPL-v3 ASIO SDK integration and supplies
an external public `cyaudio::AudioDeviceDriver`; neither MIT repository is edited.
`CYDAW_ENABLE_ASIO=OFF` keeps an SDK-free build. Standalone numerical/native
exports default to desktop OFF and do not acquire this ASIO dependency.
Provenance, exact source hashes, original per-file notices and the full GPL text
are under `third_party/asio-*` and copied with the application.

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
- The production v6 worker now uses CyAudioCore for VST3 scan/creation, ports,
  processing, parameters, transport, state and native Windows editors. Its JUCE
  VST3 host is compiled out. JUCE plugin-instance/editor types remain temporary
  application ABI adapters. Current graph commands' XML payloads are translated
  at that boundary; historical VST3 projects are not an acceptance requirement.
- The production CLAP/JSFX adapters and LV2 JUCE host remain in place. A single
  Core-provided ysfx/EEL runtime is linked into the worker, avoiding duplicate VM
  symbols. Core's independent hosts for all four formats also pass their tests.
- Windows worker IPC uses a non-waiting wake signal from the callback rather
  than coarse GUI polling. An active worker missing its pipeline deadline emits
  silence; a stopped/crashed worker retains the explicit safety bypass.
- Session tempo/play state are published on the actual render thread, retaining
  per-instance TLS isolation in numerical plugins.
- Build outputs and runtime/native-plugin packages carry both libraries' original
  licenses, manifests and third-party notices. The packager checks their presence.

## Build and tests (Windows x64 / VS2022)

```powershell
cmake --preset cyaudio-core
cmake --build --preset cyaudio-core --parallel 8
ctest --preset cyaudio-core

cmake --preset cyaudio-desktop -DCYDAW_JUCE_SOURCE_DIR=build/desktop/_deps/juce-src -DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyImgui_Interface
cmake --build --preset cyaudio-desktop --target CyDAW2 CyDAWPluginWorker CyDAWGraphRuntime_VST3 CyDAWTestGain_VST3 cydaw_cyaudio_integration_tests cydaw_cyaudio_services_tests cydaw_asio_tests cydaw_cyaudio_device_tests cydaw_plugin_ipc_tests --parallel 6
ctest --test-dir build/cyaudio-desktop -C Release -R '^cydaw_(cyaudio_integration_tests|cyaudio_services_tests|asio_tests|cyaudio_device_tests|plugin_ipc_tests)$|^cydaw2_(dj|foundry|processing|editing|sample_search|mixing|project_import)_check$' --output-on-failure
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
from inspection. A real Fireface ASIO output-only silent start/stop was verified;
this does not measure analog latency or validate recording/hotplug. Linux/macOS,
installed third-party plugins and native editor interoperability still require
platform/device-specific validation.

Additional application acceptance (isolated profile, speakers silenced):

```powershell
python tests/v2_plugin_formats_check.py --exe "build/cyaudio-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe" --output output/cyaudio-formats --port 47965 --vst3 "build/cyaudio-desktop/CyDAWGraphRuntime_artefacts/Release/VST3/CyDAW Graph Runtime.vst3"
build/cyaudio-desktop/CyDAWAudioProbe_artefacts/Release/CyDAWAudioProbe.exe --open-type ASIO --open-device Fireface --sample-rate 48000 --block-size 64 --output-only --run-silent-ms 300
```

Use `--clap`/`--lv2` with built original fixtures for their production-host checks.
The hardware probe is opt-in; substitute an installed device name. Its default
mode only enumerates. `--run-silent-ms` starts the selected stream, writes zeros
and stops it; `--output-only` leaves all inputs disabled.
