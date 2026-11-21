#!/usr/bin/env bash
# Fetch the pinned SwiftShader Vulkan ICD (Apache-2.0) into $EMERALD_CACHE_DIR and print the
# ICD json path on stdout. CI only: nothing here is compiled into Emerald or copied into a
# package. Other messages go to stderr.
#
# The bits are SwiftShader as shipped by Chrome for Testing 152.0.7977.54
# (SwiftShader revision 5b0479bd2d15058aaa9eb490e364f920ff824a8c).
#   Linux:   those two files, unmodified, in the npm tarball below (smaller than the browser zip).
#   Windows: the same Chrome build's zip, which also contains vulkan-1.dll (the loader).
# Point the loader at the printed json with both VK_DRIVER_FILES and VK_ICD_FILENAMES, and leave
# VK_ADD_DRIVER_FILES empty so no other ICD (Mesa's lavapipe, for example) is added.
set -euo pipefail

LINUX_URL="https://registry.npmjs.org/@onirenaud/swiftshader-vulkan/-/swiftshader-vulkan-0.1.0.tgz"
LINUX_SHA256="466d85dbf961edc2384bb4927ba39809be16258318ca2a87df71195bcd90dda4"

WIN_URL="https://storage.googleapis.com/chrome-for-testing-public/152.0.7977.54/win64/chrome-win64.zip"
WIN_SHA256="91850065e6b80bba0c752e17a150fe1b9e39bba51ed705640c1273f565950dda"

PIN="chrome-152.0.7977.54"

case "$(uname -s)" in
    Linux) platform=linux ;;
    MINGW* | MSYS* | CYGWIN*) platform=windows ;;
    *)
        if [ "${OS:-}" = "Windows_NT" ]; then
            platform=windows
        else
            echo "setup-swiftshader: unsupported OS $(uname -s)" >&2
            exit 1
        fi
        ;;
esac

# Windows runners have a real `python`. `python3` there can be the Store stub, which does not run.
if [ "$platform" = windows ] && command -v python >/dev/null 2>&1; then
    PY=python
elif command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo "setup-swiftshader: python is required" >&2
    exit 1
fi

if [ "$platform" = linux ]; then
    sha="$LINUX_SHA256"
    url="$LINUX_URL"
    dest_name=linux-x64
else
    sha="$WIN_SHA256"
    url="$WIN_URL"
    dest_name=win64
fi

cache_root="${EMERALD_CACHE_DIR:-${HOME}/.cache/emerald}"
dest="$cache_root/swiftshader/$PIN/$dest_name"
stamp="$dest/source.sha256"
mkdir -p "$dest"

if [ "$(cat "$stamp" 2>/dev/null || true)" != "$sha" ]; then
    archive=$(mktemp)
    trap 'rm -f "$archive"' EXIT
    echo "setup-swiftshader: downloading $url" >&2
    curl -fsSL --retry 5 --retry-delay 2 -o "$archive" "$url"
    "$PY" - "$archive" "$sha" <<'PY'
import hashlib, pathlib, sys
path, expected = sys.argv[1], sys.argv[2]
got = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
if got != expected:
    sys.stderr.write(f"setup-swiftshader: sha256 mismatch\n  expected {expected}\n  got      {got}\n")
    sys.exit(1)
PY
    if [ "$platform" = linux ]; then
        tar -xzf "$archive" -C "$dest" --strip-components=2 \
            package/linux-x64/libvk_swiftshader.so \
            package/linux-x64/vk_swiftshader_icd.json
        test -s "$dest/libvk_swiftshader.so"
        test -s "$dest/vk_swiftshader_icd.json"
    else
        "$PY" - "$archive" "$dest" <<'PY'
import pathlib, sys, zipfile
archive, dest = sys.argv[1], pathlib.Path(sys.argv[2])
want = {"vk_swiftshader.dll", "vk_swiftshader_icd.json", "vulkan-1.dll"}
found = set()
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        base = info.filename.replace("\\", "/").rsplit("/", 1)[-1]
        if base in want:
            (dest / base).write_bytes(z.read(info))
            found.add(base)
missing = want - found
if missing:
    sys.stderr.write("setup-swiftshader: zip is missing " + ", ".join(sorted(missing)) + "\n")
    sys.exit(1)
PY
        test -s "$dest/vk_swiftshader.dll"
        test -s "$dest/vulkan-1.dll"
        test -s "$dest/vk_swiftshader_icd.json"
    fi
    printf '%s\n' "$sha" >"$stamp"
    rm -f "$archive"
    trap - EXIT
fi

icd="$dest/vk_swiftshader_icd.json"
if [ "$platform" = windows ] && command -v cygpath >/dev/null 2>&1; then
    icd=$(cygpath -m "$icd")
fi
echo "setup-swiftshader: $icd" >&2
printf '%s\n' "$icd"
