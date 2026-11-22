#!/usr/bin/env bash
# Run Sandbox for a few frames on one GPU backend (vulkan or metal).
#
# A runner with no display or no usable device must not fail the job: the
# script prints SKIP and exits 0. A shader that will not load, a crash, or a
# hang after the device was created still fails. --frames is required so
# Application does not block on an error dialog.
#
#   tools/ci/gpu-smoke.sh --bin build/debug/bin/Sandbox --gpu metal
set -euo pipefail

bin=""
gpu=""
frames=30
screenshot=""
log="gpu-smoke.log"
timeout_s=180

while [ $# -gt 0 ]; do
    case "$1" in
        --bin) bin=$2; shift 2 ;;
        --gpu) gpu=$2; shift 2 ;;
        --frames) frames=$2; shift 2 ;;
        --screenshot) screenshot=$2; shift 2 ;;
        --log) log=$2; shift 2 ;;
        --timeout) timeout_s=$2; shift 2 ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ -z "$bin" ] || [ -z "$gpu" ]; then
    echo "usage: gpu-smoke.sh --bin <sandbox> --gpu vulkan|metal" >&2
    exit 2
fi
if [ ! -x "$bin" ]; then
    echo "::error::sandbox executable not found: ${bin}"
    exit 1
fi
if [ -z "$screenshot" ]; then
    screenshot="${gpu}-smoke.png"
fi

rm -f "$screenshot" "$log"

set +e
python3 - "$bin" "$gpu" "$frames" "$screenshot" "$log" "$timeout_s" << 'PY'
import subprocess
import sys

bin_path, gpu, frames, shot, log_path, timeout_s = sys.argv[1:]
with open(log_path, "w", encoding="utf-8", errors="replace") as handle:
    try:
        result = subprocess.run(
            [bin_path, "--gpu", gpu, "--frames", frames, "--screenshot", shot],
            stdout=handle,
            stderr=subprocess.STDOUT,
            timeout=int(timeout_s),
            check=False,
        )
        sys.exit(result.returncode)
    except subprocess.TimeoutExpired:
        handle.write(f"\n[gpu-smoke] timed out after {timeout_s}s\n")
        sys.exit(124)
PY
status=$?
set -e

echo "----- ${gpu} smoke (exit ${status}) -----"
cat "$log"
echo "----- end ${gpu} smoke -----"

if grep -E -q "SDL_CreateGPUShader failed|no usable variant of" "$log"; then
    echo "::error::${gpu} smoke: a shader failed to load. Metal needs the MSL files shadercross writes; Vulkan needs SPIR-V."
    exit 1
fi

device_created=0
if grep -q "GPU device created" "$log"; then
    device_created=1
fi

unavailable=0
if grep -E -q "Failed to create SDL GPU device|SDL_Init failed|SDL_CreateWindow failed|Cannot run: window/GPU initialization failed|No available video device" "$log"; then
    unavailable=1
fi
if [ "$status" -eq 124 ] && [ "$device_created" -eq 0 ]; then
    unavailable=1
fi

if [ "$status" -eq 0 ]; then
    if [ ! -s "$screenshot" ]; then
        echo "::error::${gpu} smoke exited 0 but did not write ${screenshot}"
        exit 1
    fi
    if ! grep -q "backend '${gpu}'" "$log"; then
        echo "::error::${gpu} smoke did not report backend '${gpu}'"
        exit 1
    fi
    echo "${gpu} smoke passed (${screenshot})"
    exit 0
fi

if [ "$unavailable" -eq 1 ]; then
    reason=$(grep -E "Failed to create SDL GPU device|SDL_Init failed|SDL_CreateWindow failed|Cannot run: window/GPU initialization failed|No available video device|timed out after" "$log" | head -4 | tr '\n' ' ' || true)
    echo "::notice title=${gpu} GPU smoke skipped::${reason}"
    echo "SKIP: ${gpu} GPU smoke did not run to completion. This runner has no usable ${gpu} device or display, so the job continues. ${reason}"
    exit 0
fi

echo "::error::${gpu} smoke failed (exit ${status})"
exit "$status"
