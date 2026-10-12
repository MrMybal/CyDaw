#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' \
        'Usage:' \
        '  cydaw-windows-vst-bridge.sh status' \
        '  cydaw-windows-vst-bridge.sh register <windows-plugin-directory>' \
        '  cydaw-windows-vst-bridge.sh sync' \
        '  cydaw-windows-vst-bridge.sh self-test [CyDAWPluginWorker.exe]'
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bundled_worker="$script_dir/../windows-bridge/CyDAWPluginWorker.exe"

print_tool() {
    local label="$1"
    local command_name="$2"
    if command -v "$command_name" >/dev/null 2>&1; then
        printf '%-18s %s\n' "$label" "$(command -v "$command_name")"
    else
        printf '%-18s %s\n' "$label" 'not found'
    fi
}

is_truthy() {
    case "${1:-}" in
        1|true|TRUE|yes|YES|on|ON) return 0 ;;
        *) return 1 ;;
    esac
}

resolve_runner() {
    local runner
    if [ -n "${CYDAW_WINDOWS_VST_RUNNER:-}" ]; then
        runner="$(command -v "$CYDAW_WINDOWS_VST_RUNNER" 2>/dev/null || true)"
        if [ -z "$runner" ]; then
            printf 'Configured runner was not found: %s\n' "$CYDAW_WINDOWS_VST_RUNNER" >&2
            return 1
        fi
        printf '%s\n' "$runner"
        return 0
    fi
    if is_truthy "${CYDAW_WINDOWS_VST_USE_UMU:-}"; then
        runner="$(command -v umu-run 2>/dev/null || true)"
        if [ -z "$runner" ]; then
            printf '%s\n' 'UMU/Proton was requested, but umu-run was not found.' >&2
            return 1
        fi
        printf '%s\n' "$runner"
        return 0
    fi
    runner="$(command -v wine64 2>/dev/null || command -v wine 2>/dev/null || true)"
    if [ -z "$runner" ]; then
        printf '%s\n' 'No Wine runner was found.' >&2
        return 1
    fi
    printf '%s\n' "$runner"
}

resolve_worker() {
    local requested="${1:-${CYDAW_WINDOWS_VST_WORKER:-$bundled_worker}}"
    if [ ! -f "$requested" ]; then
        printf 'Windows worker was not found: %s\n' "$requested" >&2
        return 1
    fi
    printf '%s\n' "$requested"
}

status() {
    local worker runner
    worker="${CYDAW_WINDOWS_VST_WORKER:-$bundled_worker}"
    printf '%s\n' 'CyDAW Windows VST bridge status'
    print_tool 'yabridgectl' 'yabridgectl'
    print_tool 'wine64' 'wine64'
    print_tool 'wine' 'wine'
    print_tool 'umu-run' 'umu-run'
    printf '%-18s %s\n' 'Windows worker' "$worker"
    printf '%-18s %s\n' 'Worker present' "$([ -f "$worker" ] && printf yes || printf no)"
    printf '%-18s %s\n' 'Custom runner' "${CYDAW_WINDOWS_VST_RUNNER:-automatic}"
    printf '%-18s %s\n' 'Wine prefix' "${WINEPREFIX:-automatic/default}"
    printf '%-18s %s\n' 'Proton path' "${PROTONPATH:-not configured}"
    if runner="$(resolve_runner 2>/dev/null)"; then
        printf '%-18s %s\n' 'Selected runner' "$runner"
    else
        printf '%-18s %s\n' 'Selected runner' 'not available'
    fi
    if command -v yabridgectl >/dev/null 2>&1; then
        yabridgectl status || true
    fi
}

self_test() {
    if [ "$#" -gt 1 ]; then
        usage
        exit 2
    fi
    local worker runner output compact expected_protocol
    worker="$(resolve_worker "${1:-}")" || exit 5
    runner="$(resolve_runner)" || exit 6
    expected_protocol="${CYDAW_EXPECTED_WORKER_PROTOCOL:-4}"
    if ! output="$("$runner" "$worker" --self-test)"; then
        printf '%s\n' 'The Windows worker failed its handshake.' >&2
        exit 7
    fi
    compact="$(printf '%s' "$output" | tr -d '[:space:]')"
    case "$compact" in
        *\"worker\":\"CyDAWPluginWorker\"*\"protocol\":$expected_protocol*\"isolation\":\"per-instance\"*\"status\":\"ready\"*) ;;
        *)
            printf 'Unexpected worker handshake: %s\n' "$output" >&2
            exit 8
            ;;
    esac
    printf '%s\n' "$output"
    printf 'Bridge handshake OK: protocol %s via %s\n' "$expected_protocol" "$runner"
}

register_directory() {
    if [ "$#" -ne 1 ]; then
        usage
        exit 2
    fi
    if ! command -v yabridgectl >/dev/null 2>&1; then
        printf '%s\n' 'yabridgectl is required for the recommended bridge mode.' >&2
        exit 3
    fi
    if [ ! -d "$1" ]; then
        printf 'Plugin directory does not exist: %s\n' "$1" >&2
        exit 4
    fi
    yabridgectl add "$1"
    yabridgectl sync
}

case "${1:-}" in
    status)
        status
        ;;
    register)
        shift
        register_directory "$@"
        ;;
    sync)
        command -v yabridgectl >/dev/null 2>&1 || {
            printf '%s\n' 'yabridgectl was not found.' >&2
            exit 3
        }
        yabridgectl sync
        ;;
    self-test)
        shift
        self_test "$@"
        ;;
    *)
        usage
        exit 2
        ;;
esac
