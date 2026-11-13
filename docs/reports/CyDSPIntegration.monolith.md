# CyDSP integration — CyDSP-CORE

This branch starts at `cf4228d5ed18897b786bb10f4844673593c21c5a`.
It integrates the expanded CyDSP working tree described by its `docs/DawIntegration.md`.
**It does not yet remove JUCE from the complete desktop application.**
See the [detailed French issue report](CyDSPIntegrationReport.monolith.fr.md) before attempting
that last step. No CyDSP source was changed in its repository or vendored into production targets.

## Dependency and reproducibility

`CYDAW_CYDSP_SOURCE_DIR` is a CMake cache path, defaulting to the sibling `../CyDSP`.
The checkout must contain the expanded core, Files, Devices, Sources, Stretch
and Plugins modules. Configuration fails clearly instead of falling back to JUCE's DSP.
CyDSP's own tests/benchmarks are disabled; CyDaw's integration tests are separate.
Do not enable global AVX/fast-math flags. Upstream ISA and strict FP options remain
local to CyDSP's kernels.

The inspected and compiled CyDSP revision is
`ea2361af1452b9770c8355cc346060bd3772b40b`, with a clean working tree.
The [source snapshot](CyDSPSnapshot.json) records the source digest and scope.
The build still uses an explicit local checkout; it does not fetch or publish the
private dependency. No CyDSP source is modified by the production configuration.

## Implemented

- The core audio buffers use CyDSP's 64-byte-aligned planar storage. Deep copies,
  moves, zero-sized buffers, prepared limits and CyDaw's padded channel stride are
  preserved by the application adapter.
- Convolution and the existing stretching algorithms use prepared CyDSP FFTs.
  Their existing algorithms, inverse scaling, phase treatment and latency policies
  remain; no automatic replacement of the application's musical algorithms occurs.
- Sample-library chroma analysis uses CyDSP real FFT and coherent-gain-normalized
  Hann windows. `juce_dsp` is removed from source includes, PCH and link targets.
- Every application default reader manager is registered with the CyDSP decoder
  bridge (Studio, DJ, Foundry, Cinema, sampler, processing and import helpers).
  Reads are normalized float, chunked to the prepared maximum, with seek/EOS and
  stream ownership checks. Source bit depth and original format remain available.
- WAV exports/recordings use CyDSP's writer, including integer 8/16/24/32
  and the existing automatic float32 choice. Public typed metadata supports the
  application's cue labels/notes/regions, sampler loops and RIFF INFO tags.
  Exclusive typed loop endpoints are converted to inclusive WAV endpoints.
- Fonderie's AIFF, FLAC and Vorbis exports use CyDSP encoders. Independent decoding
  verifies frames, sample rate, channel routing and sample error. Unsupported
  parameters retain stream ownership.
- A JUCE-free service acceptance target exercises persistent/ambiguous audio and
  MIDI selectors, null-backend callback clocks/diagnostics, bounded exact PCM
  proxies, and a sparse-slider JSFX hosted directly and in a separate process.
  Original local CLAP/LV2 exports are also checked through this host/worker API.
- CyDSP and upstream notices are copied beside desktop binaries and into native
  VST3/CLAP/LV2 license directories.

## Explicit retained boundaries

`src/integration/CyDSPAudioFormats.cpp` is an application-side transition adapter,
not a JUCE API emulation added to CyDSP. JUCE's reader/source interfaces remain.
The legacy parser still supplies non-typed fields and other containers. CyDSP's
public WAV metadata fills the stable application vocabulary and carries a versioned
opaque preservation payload. Unsupported legacy WAV metadata (e.g. BWF fields)
explicitly retains the original writer; an opaque payload that cannot be interpreted
is rejected rather than discarded. No audio decoder fallback is registered.

Desktop device/MIDI services, source scheduling, GUI, serialization, networking and
hosted plugin wrappers still require application migration. In particular the
existing project-state envelopes, plugin program API and IPC wire format differ
from the new CyDSP services. Two VST3 state incompatibilities were reproduced and
isolated; see the French report and its tested patch. That patch is **not** applied
to the dependency or production targets. Service acceptance does not mean the
desktop has already switched its device manager/worker protocol.

The JUCE-free core/native-export path remains distinct from the transitional desktop.

## Build and check (Windows x64)

With VS2022 C++ tools, CMake, the expanded CyDSP checkout and the existing CyUI/JUCE
sources available:

```powershell
cmake --preset cydsp-core -DCYDAW_CYDSP_SOURCE_DIR=../CyDSP
cmake --build --preset cydsp-core
ctest --preset cydsp-core

cmake --preset cydsp-desktop -DCYDAW_CYDSP_SOURCE_DIR=../CyDSP
cmake --build --preset cydsp-desktop --target CyDAW2 CyDAWTestGain_VST3 cydaw_plugin_ipc_tests cydaw_cydsp_integration_tests cydaw_cydsp_services_tests
ctest --preset cydsp-desktop -R "^cydaw_(cydsp_integration_tests|cydsp_services_tests|plugin_ipc_tests)$"
ctest --preset cydsp-desktop -R "^cydaw2_(dj|foundry|processing|editing|sample_search|mixing|project_import)_check$"
```

An existing local JUCE checkout can be selected with `CYDAW_JUCE_SOURCE_DIR` and a
local CyUI checkout with `FETCHCONTENT_SOURCE_DIR_CYUI`. Normal pinned dependency
rules otherwise remain in effect. Standalone core tests do not configure JUCE.

## Upstream diagnostics and inventory

```powershell
& '.\build\cydsp-desktop\cydaw_cydsp_integration_tests_artefacts\Release\cydaw_cydsp_integration_tests.exe' --upstream-probes "$((Get-Location).Path)/output/cydsp-probes"
python tools/audit_cydsp_integration.py --cydsp-source ../CyDSP --output output/cydsp-inventory.json
```

The metadata probe now exits `0`: cue labels and typed LIST/adtl survive the
roundtrip. The optional plugin probe is deliberately separate from passing adapter
regressions and leaves real failures visible (no CTest WILL_FAIL):

```powershell
# Supply only a deliberately selected original fixture/export, never a whole
# installed third-party catalogue. Each probe scans/loads executable plugin code.
& .\build\cydsp-core\Release\cydaw_cydsp_services_tests.exe --plugin clap "D:/absolute/path/OurPlugin.clap"
& .\build\cydsp-core\Release\cydaw_cydsp_services_tests.exe --plugin lv2 "D:/absolute/path/OurPlugin.lv2"
& .\build\cydsp-core\Release\cydaw_cydsp_services_tests.exe --plugin vst3 "D:/absolute/path/OurPlugin.vst3"
```

The unmodified CyDSP VST3 probe fails on `CyDAW Test Gain.vst3` during save-state
(status12). Correcting the optional controller-state case exposes a second
short-read restore failure. The combined report patch passes the same save/load
and worker checks in a separate validation executable, while production libraries
remain untouched. CLAP/LV2/JSFX acceptance passes on our original fixtures.
The inventory is lexical (legacy code/comments included), not a migration percentage.

## Acceptance for complete JUCE removal

Require a full desktop/worker build with no JUCE headers, modules, CMake helpers or
symbols; equivalent project/preset/marker roundtrips; physical audio/MIDI acceptance;
all relevant application regressions; and native plugin export/host validation.
Changing namespace names, returning fabricated device capabilities or dropping
features is not an acceptable substitute. No new release is claimed by this branch.
