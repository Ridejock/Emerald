#!/usr/bin/env bash
# Turn the reusable workflow's inputs into the test and package matrices.
# Prints GitHub Actions output (key=value) and, when GITHUB_OUTPUT is set,
# appends the same lines there. A web/html5/emscripten platform is an error:
# SDL GPU has no WebGPU backend (docs/emscripten.md).
set -euo pipefail

platforms_csv=""
presets_csv=""
package_preset="release"
linux_cmake_args="-DEMERALD_SHADER_FORMATS=SPIRV"
cmake_args=""
cmake_target="package"

while [ $# -gt 0 ]; do
    case "$1" in
        --platforms) platforms_csv=$2; shift 2 ;;
        --presets) presets_csv=$2; shift 2 ;;
        --package-preset) package_preset=$2; shift 2 ;;
        --linux-cmake-args) linux_cmake_args=$2; shift 2 ;;
        --cmake-args) cmake_args=$2; shift 2 ;;
        --cmake-target) cmake_target=$2; shift 2 ;;
        *)
            echo "::error::unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ -z "$platforms_csv" ] || [ -z "$presets_csv" ]; then
    echo "::error::platforms and presets are required" >&2
    exit 2
fi
if [ -z "$package_preset" ] || [ -z "$cmake_target" ]; then
    echo "::error::package-preset and cmake-target must not be empty" >&2
    exit 2
fi

# Here-string, not a nested process substitution: this function is itself run
# from mapfile's process substitution, and a second one drops every line but
# the first under pipefail.
split_csv() {
    local raw="$1" item
    local -a items=()
    raw=${raw//$'\n'/,}
    IFS=',' read -ra items <<<"$raw"
    for item in "${items[@]}"; do
        item="${item#"${item%%[![:space:]]*}"}"
        item="${item%"${item##*[![:space:]]}"}"
        if [ -n "$item" ]; then
            printf '%s\n' "$item"
        fi
    done
}

runs_on_for() {
    case "$1" in
        windows) printf '%s\n' "windows-latest" ;;
        linux) printf '%s\n' "ubuntu-24.04" ;;
        *)
            echo "::error::Unknown platform '$1' (use windows or linux)" >&2
            return 1
            ;;
    esac
}

reject_web() {
    local lower
    lower=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
    case "$lower" in
        web|emscripten|html5|wasm|webgpu)
            echo "::error::The '$1' platform is not supported. SDL 3.4.16 GPU has no WebGPU backend (docs/emscripten.md)." >&2
            return 1
            ;;
    esac
}

mapfile -t platforms < <(split_csv "$platforms_csv")
mapfile -t presets < <(split_csv "$presets_csv")
if [ "${#platforms[@]}" -eq 0 ] || [ "${#presets[@]}" -eq 0 ]; then
    echo "::error::platforms and presets must list at least one entry" >&2
    exit 2
fi

linux_full="$cmake_args"
if [ -n "$linux_cmake_args" ]; then
    linux_full="${linux_full:+$linux_full }$linux_cmake_args"
fi

test_items='[]'
package_items='[]'
for platform in "${platforms[@]}"; do
    reject_web "$platform"
    runs=$(runs_on_for "$platform")
    if [ "$platform" = linux ]; then
        args=$linux_full
    else
        args=$cmake_args
    fi
    for preset in "${presets[@]}"; do
        if [ "$preset" = "$package_preset" ]; then
            continue
        fi
        test_items=$(jq -c --arg platform "$platform" --arg preset "$preset" \
            --arg runs_on "$runs" --arg cmake_args "$args" \
            '. + [{platform:$platform,preset:$preset,runs_on:$runs_on,cmake_args:$cmake_args}]' \
            <<<"$test_items")
    done
    package_items=$(jq -c --arg platform "$platform" --arg preset "$package_preset" \
        --arg runs_on "$runs" --arg cmake_args "$args" --arg target "$cmake_target" \
        '. + [{platform:$platform,preset:$preset,runs_on:$runs_on,cmake_args:$cmake_args,target:$target}]' \
        <<<"$package_items")
done

if [ "$test_items" = "[]" ]; then
    has_test=false
    # A skipped job still wants a legal matrix if Actions expands it.
    test_items='[{"platform":"linux","preset":"debug","runs_on":"ubuntu-24.04","cmake_args":""}]'
else
    has_test=true
fi

test_matrix=$(jq -cn --argjson include "$test_items" '{include:$include}')
package_matrix=$(jq -cn --argjson include "$package_items" '{include:$include}')

write_out() {
    printf '%s=%s\n' "$1" "$2"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        printf '%s=%s\n' "$1" "$2" >>"$GITHUB_OUTPUT"
    fi
}

write_out has-test "$has_test"
write_out test-matrix "$test_matrix"
write_out package-matrix "$package_matrix"
