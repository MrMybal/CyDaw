# FFmpeg in CyDAW Cinema

CyDAW uses the FFmpeg libavformat, libavcodec, libavutil, libswscale and libswresample shared libraries for video previews, filmstrip frames and soundtrack extraction. FFmpeg is not linked into exported VST3, CLAP or LV2 plugins.

Pinned Windows x64 distribution: BtbN FFmpeg-Builds, autobuild-2026-10-16-13-04, n8.1.3-18-g46d899d7a0, LGPL shared build. This build uses LGPL-3.0-or-later (`--enable-version3`), without `--enable-gpl`, `--enable-nonfree`, libx264 or libx265. Native H.264/HEVC decoders remain available. The exact URL, binary-archive SHA-256, FFmpeg revision and source-archive SHA-256 are in ffmpeg-sdk.json / MANIFEST.json.

Run `python tools/setup_ffmpeg.py` to install the pinned SDK into the ignored third_party/ffmpeg cache, or pass a previously downloaded verified archive with `--archive`. Older daily upstream releases may expire: keep that archive when preparing a reproducible release. CMake also accepts CYDAW_FFMPEG_ROOT for a separately prepared shared SDK. These binaries are dynamically linked and can be replaced by ABI-compatible shared libraries.

The runtime distribution carries the LGPL license, incorporated GPLv3 text, FFmpeg source archive, provenance and complete configuration under Licenses/FFmpeg. Keep their notices and those of the optional dependencies included by the binary provider. A release distributor must also provide the corresponding sources and notices for the statically included external libraries in that provider's build; the FFmpeg-only source archive does not replace those dependency sources. The provider's build scripts list its dependency set: https://github.com/BtbN/FFmpeg-Builds/tree/master/scripts.d . For a public release, preserve that full source set or build a controlled minimal shared SDK from maintained source archives.

Upstream: https://ffmpeg.org/ . License and redistribution checklist: https://ffmpeg.org/legal.html . Binary provider: https://github.com/BtbN/FFmpeg-Builds .
