# CyAudioCore integration — CyDSP-CORE

CyDaw's production Windows desktop and isolated plugin worker configure and
build without JUCE. CyAudioCore owns the CyDSP numerical dependency. The DAW
uses `cyaudio` service APIs and `cydsp` processors, including internal/exported
instruments and effects. Neither sibling repository is modified by this port.

Validated dependency revisions: CyAudioCore `bb26e93604d43de2069694b0791bc0bcf5a53ea6`,
CyDSP `fda6bd30c5b622795cc08e8bd2750188a6c5a0c0`. This CyDSP revision changes only docs/report tooling
from the previous pin; the compiled-source hash is identical. The
[final source snapshot](reports/JuceRetirementSourceSnapshot.json) records clean
sibling trees and their hashes. The earlier source snapshot remains
at [CyAudioCoreSnapshot.json](reports/CyAudioCoreSnapshot.json). See the
[current French integration/issue report](CyAudioCoreIntegrationReport.fr.md).
Historical checkpoint reports in this repository describe their dated state;
they are not the current production dependency boundary.

## Production architecture

- The Win32 main window, message loop and D3D11 ImGui surface are native. CyUI
  supplies controls, audio views, nodes, source editing, images and inspector UI.
- The application owns documents, UTF-8 text, JSON/XML, files/streams, timers,
  threads, TCP/UDP/OSC, processes, clipboard, file dialogs, HTTP streams and ZIP
  readers. These are original OS/standard-library adapters, not JUCE source or
  a renamed JUCE namespace. nlohmann JSON, pugixml, miniz and stb keep their notices.
- DeviceManager uses Core audio/MIDI drivers; physical channel masks remain
  distinct from dense client channel indices. The callback publishes Core host
  clock anchors. Source transport uses Core; rendered-source paging and the
  stereo voice scheduler are application adapters around numerical/Core services.
- Audio readers/writers, samples and recording use Core. Metadata intentionally
  preserves existing `cydsp.format`/`cydsp.metadata` keys and project vocabulary.
- Aligned sample storage, vector operations, FFTs, resampling and numerical DSP
  use CyDSP through CyAudioCore. Existing musical processors retain their sound
  and parameter identities. New SDK algorithms are not automatically new UI nodes.
- The ordinary `CyDAWPluginWorker` executable directly hosts VST3, CLAP, LV2 and
  JSFX through Core. The parent uses native shared-memory/wake IPC. Per-instance
  isolation, automation, transport, state, Windows editors and generic controls
  remain. Historical project state has an independent bounded compatibility codec.
- LV2 preset discovery uses the Core-bundled Lilv runtime. Detailed JSFX syntax
  diagnostics use the same Core-bundled ysfx runtime. These workarounds should
  become public Core APIs; they do not add a second VM or host implementation.
- Generated native VST3/CLAP/LV2 exports use their official format SDKs and the
  shared engine/CyUI renderer. No production target loads JUCE during CMake.
- Video/filmstrip/audio extraction stays with the existing shared FFmpeg SDK.
  The local CyDoc reader, translations, media and build/export pipeline remain.

## Windows builds

Requirements are in the README. Native acceptance tests require Python 3.12+
for extensible PCM WAV support; install `tests/requirements-native.txt` for
Cinema image/codec and numerical companion checks. Default sibling locations are `../CyAudioCore`
and `../CyDSP`; override `CYDAW_CYAUDIOCORE_SOURCE_DIR` and
`CYDAW_CYDSP_SOURCE_DIR`. Corresponding environment variables supply clean-build
CI defaults. No implicit fetch of another audio-library revision occurs.

```powershell
cmake --preset cyaudio-desktop
cmake --build --preset cyaudio-desktop --parallel 6
ctest --preset cyaudio-desktop --output-on-failure
```

For an existing local CyUI checkout add
`-DFETCHCONTENT_SOURCE_DIR_CYUI=D:/_Project/CyImgui_Interface` at configure.
`CYDAW_ENABLE_ASIO=ON` enables CyDaw's application-side GPL ASIO driver using
PortAudio. Its provenance, original notices and corresponding sources stay
outside the MIT siblings and travel with builds/packages. `OFF` gives an
SDK-free WASAPI/DirectSound build. Never enable global fast-math/AVX flags;
CyDSP owns its per-kernel ISA dispatch.

`CYDAW_BUILD_LEGACY_JUCE_TESTS=ON` explicitly opts into historical comparison
fixtures. That option is OFF by default and those fixtures are not production
runtime dependencies. `CYDAW_FETCH_JUCE` is OFF in native presets. A fresh
Windows build's generated dependency graph is checked by
`tests/no_juce_build_check.py` for actual includes and libraries.

## Validation and distribution

Final Windows acceptance passed: the 119-case native suite has no failures;
its optional Batterie skip was separately exercised with the installed bank.
The strict sampler/Batterie checks, actual Fireface ASIO four-format application
session, native CLAP/LV2/VST3 exports, 47 official VST3 validator tests, five plugin
windows and eight packaging tests passed. See
[the acceptance manifest](reports/JuceRetirementAcceptance.json) for evidence
paths and limits. No JUCE includes or libraries occur in the generated 41-project
production dependency graph.

```powershell
python -m pip install -r tests/requirements-native.txt
python tests/no_juce_build_check.py build/cyaudio-native-desktop
# Native Audio Probe is in Release, not in a JUCE artefacts directory:
& 'build/cyaudio-native-desktop/Release/CyDAWAudioProbe.exe' --open-type ASIO --open-device Fireface --sample-rate 48000 --block-size 1024 --output-only --run-silent-ms 300
python tools/package_v2_test.py --build-dir build/cyaudio-native-desktop/CyDAW2_artefacts/Release --output output/cyaudio-native-package --alpha
```

Choose a block size actually advertised by the ASIO driver/control panel;
1,024 is the currently installed Fireface setting, not a universal requirement.


Run automated desktop scenarios with isolated `APPDATA`/`LOCALAPPDATA`, recovery
disabled, and silent output. Some optional factory-instrument tests need installed
banks; `CYDAW_BANKS_DIR` can point at an existing read-only sample bank for those
instrument-only checks. Do not run bank-installation tests against a personal
bank directory. Tests synthesize their other audio assets locally.

The native/alpha workflows check out tested dependencies using
`.github/actions/audio-dependencies`; private access requires
`CYDAW_DEPENDENCIES_TOKEN`. Local integration does not push or publish anything.
Packaging no longer requires a JUCE checkout/notice. It still verifies offline
CyDoc resources, FFmpeg runtimes/corresponding sources and all native dependency
notices. An explicitly supplied historical `--juce-source` remains optional.

The application executable remains `CyDAW 2.exe` internally for existing scripts;
its user-facing product name is CyDaw. Runtime files stay beside the executable.
Linux/macOS desktop UI, physical latency, microphone recording, MIDI hardware
hotplug and installed third-party plugin interoperability require separate
platform/device acceptance. A compiled Windows desktop is not a claim that those
hardware combinations have all been exercised.

## Paired JUCE comparison

The [controlled Windows comparison](CyAudioCoreComparison.fr.md) measures the actual old JUCE desktop against the native product. It identifies higher live CPU use, delayed sample-library readiness and shared pre-existing CUE/Cinema-export issues. Dependency retirement is complete; performance equivalence is not established. No audio implementation was changed during that comparison.

## CPU attribution after the comparison

The [application CPU diagnostic](CyAudioCoreCpuDiagnostic.fr.md) separates the UI thread, live audio, worker IPC and vector backends. It includes controlled null-driver UI experiments and repeated ASIO measurements. The global CPU regression must not be attributed to SDK DSP kernels without this thread/function breakdown. Production audio code and sibling dependencies are unchanged by this campaign.
