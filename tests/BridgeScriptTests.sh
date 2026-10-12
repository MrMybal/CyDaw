#!/usr/bin/env bash
set -euo pipefail

bridge_script="${1:?bridge script path is required}"
temporary_root="$(mktemp -d)"
trap 'rm -rf -- "$temporary_root"' EXIT

worker="$temporary_root/CyDAWPluginWorker.exe"
runner="$temporary_root/custom-wine"
umu_runner="$temporary_root/umu-run"
bad_worker="$temporary_root/BadWorker.exe"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf '\''%s\n'\'' '\''{"worker":"CyDAWPluginWorker","protocol":4,"isolation":"per-instance","formats":["VST3"],"status":"ready"}'\''' \
    > "$worker"
printf '%s\n' '#!/usr/bin/env bash' 'exec "$@"' > "$runner"
printf '%s\n' '#!/usr/bin/env bash' 'exec "$@"' > "$umu_runner"
printf '%s\n' '#!/usr/bin/env bash' 'printf '\''%s\n'\'' '\''{"status":"wrong"}'\''' \
    > "$bad_worker"
chmod +x "$worker" "$runner" "$umu_runner" "$bad_worker"

custom_result="$(CYDAW_WINDOWS_VST_RUNNER="$runner" \
    "$bridge_script" self-test "$worker")"
case "$custom_result" in
    *'"protocol":4'*'Bridge handshake OK: protocol 4'*) ;;
    *) printf 'Custom Wine runner test failed: %s\n' "$custom_result" >&2; exit 1 ;;
esac

umu_result="$(PATH="$temporary_root:$PATH" CYDAW_WINDOWS_VST_USE_UMU=1 \
    "$bridge_script" self-test "$worker")"
case "$umu_result" in
    *"via $umu_runner"*) ;;
    *) printf 'UMU runner selection test failed: %s\n' "$umu_result" >&2; exit 1 ;;
esac

if CYDAW_WINDOWS_VST_RUNNER="$runner" \
    "$bridge_script" self-test "$bad_worker" >/dev/null 2>&1; then
    printf '%s\n' 'Invalid worker handshake was accepted.' >&2
    exit 1
fi

if CYDAW_WINDOWS_VST_RUNNER="$runner" \
    "$bridge_script" self-test "$temporary_root/missing.exe" >/dev/null 2>&1; then
    printf '%s\n' 'Missing worker was accepted.' >&2
    exit 1
fi

printf '%s\n' 'CyDAW Windows VST bridge script tests passed'
