#!/usr/bin/env bash
# Copy the release archives for one preset into --dest and check them.
# Windows zips keep the game's .itch.toml (the itch app's Play target) and must
# contain a DXIL shader: a shadercross without DXC would otherwise ship a
# Direct3D 12 build that cannot draw. Linux uses the same package target the
# game already has (CPack) or Emerald's linux-release archive; it does not
# invent a second layout.
set -euo pipefail

preset=""
dest=""
platform=""
require_itch=true
require_dxil=true

while [ $# -gt 0 ]; do
    case "$1" in
        --preset) preset=$2; shift 2 ;;
        --dest) dest=$2; shift 2 ;;
        --platform) platform=$2; shift 2 ;;
        --require-itch) require_itch=$2; shift 2 ;;
        --require-dxil) require_dxil=$2; shift 2 ;;
        *)
            echo "::error::unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ -z "$preset" ] || [ -z "$dest" ] || [ -z "$platform" ]; then
    echo "::error::--preset, --dest and --platform are required" >&2
    exit 2
fi

cache="build/${preset}/CMakeCache.txt"
if [ ! -f "$cache" ]; then
    echo "::error::No CMake cache at ${cache} (looked for CMAKE_PROJECT_VERSION)" >&2
    exit 1
fi
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$cache" | head -1)
if [ -z "$version" ]; then
    echo "::error::CMAKE_PROJECT_VERSION is empty in ${cache}. Set project(VERSION) in CMakeLists.txt." >&2
    exit 1
fi

shopt -s nullglob
archives=(
    "build/${preset}/"*.zip
    "build/${preset}/"*.tar.gz
    "build/${preset}/dist/"*.zip
    "build/${preset}/dist/"*.tar.gz
)
if [ "${#archives[@]}" -eq 0 ]; then
    echo "::error::No .zip or .tar.gz from preset ${preset}. The package target should write one under build/${preset} or build/${preset}/dist." >&2
    exit 1
fi

mkdir -p "$dest"
printf '%s\n' "$version" >"${dest}/VERSION"

for archive in "${archives[@]}"; do
    if [ ! -s "$archive" ]; then
        echo "::error::${archive} is empty" >&2
        exit 1
    fi
    echo "Package: $archive"
    if [ "$platform" = windows ]; then
        listing=$(cmake -E tar tf "$archive")
        if [ "$require_itch" = true ]; then
            if ! printf '%s\n' "$listing" | grep -q '\.itch\.toml'; then
                echo "::error::${archive} has no .itch.toml. The itch.io app's Play button reads that manifest; the game's install rules should put it at the root of the zip." >&2
                exit 1
            fi
        fi
        if [ "$require_dxil" = true ]; then
            if ! printf '%s\n' "$listing" | grep -q '\.dxil$'; then
                echo "::error::${archive} has no DXIL shaders. shadercross must be built with DXC; refusing a Windows package that cannot draw with Direct3D 12." >&2
                exit 1
            fi
        fi
    fi
    cp -p "$archive" "$dest/"
done

echo "Version: $version"
