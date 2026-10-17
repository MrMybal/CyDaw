# zstd in CyDAW

CyDAW decompresses Zrythm projects (`project.zpj`, JSON compressed with Zstandard) with the official zstd library's decoder.

Version: zstd 1.5.6, from the release archive `zstd-1.5.6.tar.gz` (https://github.com/facebook/zstd/releases/tag/v1.5.6), SHA-256 checked against the release's `zstd-1.5.6.tar.gz.sha256`.

Only the decoder is kept, in `third_party/zstd`: `lib/zstd.h`, `lib/zstd_errors.h`, the headers of `lib/common`, the sources `lib/common/{debug,entropy_common,error_private,fse_decompress,xxhash,zstd_common}.c` and `lib/decompress/*.c` / `*.h`. No file was changed. It builds as the static library `cydaw_zstd` (CMakeLists.txt) with `ZSTD_DISABLE_ASM=1` and `ZSTD_LEGACY_SUPPORT=0`.

License: BSD (the `LICENSE` file beside it; zstd is also offered under GPLv2, CyDAW uses it under the BSD license). Keep `third_party/zstd/LICENSE` in any distribution.
