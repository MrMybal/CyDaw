# Plugin format dependencies

Vendored sources, without nested Git repositories:

| Dependency | Upstream revision | License | Usage |
| --- | --- | --- | --- |
| Pugl | lv2/pugl `ba0d119ff5a1a6abe65efe40dd28022eafc5590c` | ISC | native Linux X11/OpenGL editor |
| CLAP | free-audio/clap `a47f6badb49d948fd009998f28309cdab78979c9` | MIT | worker host adapter and native CLAP export |
| LV2 | lv2/lv2 `3c57dae600a5ad8d05acd53ee3490f7d91cb7be6` | ISC; notices retained | native LV2 API headers and metadata |
| ysfx | jpcima/ysfx `8077347ccf4115567aed81400281dca57acbb0cc` | Apache-2.0 | JSFX compiler/runtime in worker only |
| ysfx dr_libs | mackron/dr_libs `f13cbcfd06afe7287f99b1bb5982cefdf3d6a974` | public domain or MIT-0 | JSFX audio file access |
| ysfx WDL | upstream snapshot distributed by ysfx | zlib-style, see `ysfx/thirdparty/WDL/LICENSE.txt` | EEL2 and LICE graphics |
| ysfx stb | upstream snapshot distributed by ysfx | public domain or MIT | LICE image readers |

LV2 hosting uses CyAudioCore's separately vendored Lilv/Serd/Sord/Sratom implementation and its retained notices. It does not use a JUCE SDK. Native CLAP/LV2 exports do not link ysfx. The local ysfx copy contains only library functionality: the optional upstream JUCE plugin wrapper and its build/download configuration have been removed. CyDaw's active JSFX runtime is supplied by CyAudioCore.

NASM enables EEL2 native code generation on MSVC x64. CMake finds `nasm` on PATH, accepts `-DNASM_PROGRAM=...`, and looks in `output/plugin-formats/tools/nasm-2.16.03` for this workspace's build. Without NASM it uses ysfx's portable interpreter. NASM is a build tool, not a plugin or DAW runtime dependency. The tested build uses NASM 2.16.03 from nasm.us. Exported native plugins do not require NASM.

No SDK choice changes the license of CyDAW DSP code or of imported JSFX scripts. Example JSFX files in `Examples/JSFX` are original MIT templates. Preserve each third-party script's license independently.

Local cross-compilation is provisioned by `tools/setup_plugin_toolchains.py`. Zig 0.15.2 is pinned by SHA256. The Debian bookworm x86_64 X11/GL sysroot is verified against package SHA256 entries from Debian's HTTPS index; the exact versions and checksums are recorded in its cache `ready.json`. Compiler archives and sysroots are cache data, not checked into the repository. X11/GL shared libraries remain system dependencies on Linux. The embedded libc++, libc++abi and libunwind notices are copied with exported Linux binaries; Pugl's COPYING is copied as well. macOS SDKs are not included.
