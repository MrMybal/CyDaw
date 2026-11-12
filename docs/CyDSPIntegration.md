# CyDSP integration — CyDSP-CORE

This branch starts at `cf4228d5ed18897b786bb10f4844673593c21c5a`.
It integrates the expanded CyDSP working tree described by its `docs/DawIntegration.md`.
**It does not yet remove JUCE from the complete desktop application.**
See the [detailed French issue report](CyDSPIntegrationReport.fr.md) before attempting
that last step. No CyDSP source was changed or copied into this repository.

## Dependency and reproducibility

`CYDAW_CYDSP_SOURCE_DIR` is a CMake cache path, defaulting to the sibling `../CyDSP`.
The checkout must contain the expanded core, Files, Devices, Sources and Stretch
modules. Configuration fails clearly instead of falling back to JUCE's DSP.
CyDSP's own tests/benchmarks are disabled; CyDaw's integration tests are separate.
Do not enable global AVX/fast-math flags. Upstream ISA and strict FP options remain
local to CyDSP's kernels.

The inspected CyDSP HEAD is `ad813d133a2f54e143db8a3392f2e1768cfdb5ba`, but the
required additions were **uncommitted/untracked** at integration time. That commit
alone cannot reproduce this build. The [source snapshot](reports/CyDSPSnapshot.json)
records the working-tree source hash and scope. Commit/publish the expanded library
before pinning it for CI/distribution; this branch neither changes workflows nor
publishes the private working tree.

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
- Untagged WAV exports/recordings use CyDSP's writer, including integer 8/16/24/32
  and the existing automatic float32 choice. Channel mapping and finalization are
  independently checked. Unsupported parameters retain stream ownership.
- CyDSP and upstream notices are copied beside desktop binaries and into native
  VST3/CLAP/LV2 license directories.

## Explicit retained boundaries

`src/integration/CyDSPAudioFormats.cpp` is an application-side transition adapter,
not a JUCE API emulation added to CyDSP. JUCE's reader/source interfaces remain.
It deliberately uses the old parser **only for metadata** so markers/tags/loops are
not lost. CyDSP is the only decoder registered for sample reads; decoder failures
are reported, not retried with another audio backend. WAV exports with metadata
explicitly retain the old writer until the reproduced LIST/adtl bug is fixed.
AIFF/FLAC/Ogg writers, desktop device/MIDI services, source scheduling, GUI,
serialization, networking and hosted plugin wrappers still require subsequent work.

The CyDSP Devices/Sources/Stretch targets are available to integration acceptance
checks; their presence does not mean the desktop has switched every service to them.
The JUCE-free core/native-export path is distinct from the transitional desktop path.

## Build and check (Windows x64)

With VS2022 C++ tools, CMake, the expanded CyDSP checkout and the existing CyUI/JUCE
sources available:

```powershell
cmake --preset cydsp-core -DCYDAW_CYDSP_SOURCE_DIR=../CyDSP
cmake --build --preset cydsp-core
ctest --preset cydsp-core

cmake --preset cydsp-desktop -DCYDAW_CYDSP_SOURCE_DIR=../CyDSP
cmake --build --preset cydsp-desktop --target CyDAW2 CyDAWTestGain_VST3 cydaw_cydsp_integration_tests
ctest --preset cydsp-desktop -R "^cydaw_cydsp_integration_tests$"
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

The upstream probe is intentionally separate from adapter regressions. Exit `2`
currently identifies a failed metadata roundtrip; it must not be presented as a
passing test or hidden with CTest WILL_FAIL. The inventory is a lexical scan,
including retained legacy code/comments, not a count of reachable bugs.

## Acceptance for complete JUCE removal

Require a full desktop/worker build with no JUCE headers, modules, CMake helpers or
symbols; equivalent project/preset/marker roundtrips; physical audio/MIDI acceptance;
all relevant application regressions; and native plugin export/host validation.
Changing namespace names, returning fabricated device capabilities or dropping
features is not an acceptable substitute. No new release is claimed by this branch.
