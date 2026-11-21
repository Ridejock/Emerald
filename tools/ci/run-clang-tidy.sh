#!/usr/bin/env bash
# Run clang-tidy on Emerald's own translation units.
# src/Vendor, FetchContent dependencies and any third_party tree are not passed in.
#
#   tools/ci/run-clang-tidy.sh --build build/debug [--report-only] [--jobs 3]
#
# Exit 0 when there are no findings. With --report-only, exit 0 even when there
# are findings (the log still lists them). Without it, any warning fails the run.
set -euo pipefail

build=""
jobs="$(nproc 2>/dev/null || echo 2)"
report_only=0
out="clang-tidy.txt"

while [ $# -gt 0 ]; do
    case "$1" in
        --build) build=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --report-only) report_only=1; shift ;;
        --output) out=$2; shift 2 ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ -z "$build" ] || [ ! -f "${build}/compile_commands.json" ]; then
    echo "::error::need --build pointing at a directory with compile_commands.json"
    exit 1
fi

runner="$(command -v run-clang-tidy || command -v run-clang-tidy-18 || true)"
if [ -z "$runner" ]; then
    echo "::error::run-clang-tidy is not installed (apt install clang-tidy)"
    exit 1
fi

# Paths in compile_commands.json are absolute. Vendor and dependencies must not match.
file_re='/(src/(?!Vendor/)|tests/|sandbox/src/|bench/).+\.cpp$'

set +e
"$runner" -p "$build" -j "$jobs" -quiet "$file_re" > "$out" 2>&1
tidy_status=$?
set -e

warnings=$(grep -c ': warning:' "$out" || true)
errors=$(grep -c ': error:' "$out" || true)
echo "clang-tidy: ${warnings} warning(s), ${errors} error(s). Log: ${out}"
echo "Checked translation units matching ${file_re}"

# A broken .clang-tidy (unknown key, bad regex) prints "Error parsing" and must
# fail the job even in report-only mode. Those lines are not findings in the code.
if grep -q "Error parsing" "$out"; then
    echo "::error::clang-tidy could not read .clang-tidy"
    exit 1
fi

if [ "$tidy_status" -ne 0 ] && [ "$warnings" -eq 0 ] && [ "$errors" -eq 0 ]; then
    echo "::error::clang-tidy failed to run (exit ${tidy_status})"
    cat "$out"
    exit "$tidy_status"
fi

if [ "$report_only" -eq 1 ]; then
    if [ "$warnings" -ne 0 ] || [ "$errors" -ne 0 ]; then
        echo "::notice title=clang-tidy report-only::${warnings} warning(s), ${errors} error(s). The job stays green; see ${out}."
    fi
    exit 0
fi

if [ "$warnings" -ne 0 ] || [ "$errors" -ne 0 ]; then
    echo "::error::clang-tidy reported ${warnings} warning(s) and ${errors} error(s)"
    exit 1
fi
exit 0
