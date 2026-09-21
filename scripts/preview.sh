#!/usr/bin/env bash
set -euo pipefail
renderer="${OBSERVATORY_RENDERER:-gas-giant-screensaver}"
"$renderer" "$@" &
child=$!
cleanup() {
    kill "$child" 2>/dev/null || true
    wait "$child" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT TERM
while kill -0 "$child" 2>/dev/null; do
    if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
        if monitors="$(hyprctl monitors -j 2>/dev/null)"; then
            if ! jq -e 'length > 0 and all(.[]; .dpmsStatus == true)' <<<"$monitors" >/dev/null; then
                exit 0
            fi
        fi
    fi
    sleep 1
done
status=0
wait "$child" || status=$?
trap - EXIT
cleanup
exit "$status"
