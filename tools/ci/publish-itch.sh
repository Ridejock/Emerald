#!/usr/bin/env bash
# Push release archives to itch.io with butler, one channel per platform.
#
#   user/game:windows
#   user/game:linux
#
# The version is the CMake project VERSION, passed with --userversion.
# There is no web channel (docs/emscripten.md).
#
# Exits 0 without uploading when this is not a tag push, when itch-user is
# empty, or when BUTLER_API_KEY is unset, so a fork or a pull request stays
# green. A web archive, two archives aimed at the same channel, or a butler
# failure exits non-zero.
set -euo pipefail

BUTLER_VERSION=15.32.0
# butler-linux-amd64.zip from https://github.com/itchio/butler/releases/tag/v15.32.0
BUTLER_SHA256=5db7b053fc9b578b86ffaf3520a12b952eb95c9225171cc5720dfbc9fe4574b3

user=${ITCH_USER:-}
game=${ITCH_GAME:-}
routes_text=${ITCH_ROUTES:-}
version=""
archives_dir=""
force=false
dry_run=false
install_only=false
archives=()

usage() {
    echo "usage: publish-itch.sh [--user U] [--game SLUG] [--version V] [--archives-dir DIR]" >&2
    echo "         [--force] [--dry-run] [--install-only] [archive ...]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --user) user=$2; shift 2 ;;
        --game) game=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --archives-dir) archives_dir=$2; shift 2 ;;
        --force) force=true; shift ;;
        --dry-run) dry_run=true; shift ;;
        --install-only) install_only=true; shift ;;
        --help|-h) usage ;;
        --) shift; break ;;
        -*)
            echo "::error::unknown argument: $1" >&2
            exit 2
            ;;
        *) archives+=("$1"); shift ;;
    esac
done
while [ $# -gt 0 ]; do
    archives+=("$1")
    shift
done

install_butler() {
    if [ -n "${BUTLER:-}" ]; then
        return 0
    fi
    local root="${RUNNER_TEMP:-/tmp}/emerald-butler-${BUTLER_VERSION}"
    local bin="${root}/butler"
    if [ ! -x "$bin" ]; then
        local tmp url
        tmp=$(mktemp -d)
        url="https://github.com/itchio/butler/releases/download/v${BUTLER_VERSION}/butler-linux-amd64.zip"
        curl -sSL --fail -o "${tmp}/butler.zip" "$url"
        echo "${BUTLER_SHA256}  ${tmp}/butler.zip" | sha256sum -c - >&2
        unzip -q "${tmp}/butler.zip" -d "$tmp"
        mkdir -p "$root"
        mv "${tmp}/linux-amd64/butler" "$bin"
        chmod +x "$bin"
        rm -rf "$tmp"
    fi
    BUTLER=$bin
}

if [ "$install_only" = true ]; then
    install_butler
    printf '%s\n' "$BUTLER"
    exit 0
fi

# Not a tag, and the caller did not ask to try: nothing to do.
ref=${GITHUB_REF:-}
if [ "$force" != true ] && [ "$dry_run" != true ]; then
    case "$ref" in
        refs/tags/*) ;;
        *)
            echo "::notice::Skipping itch.io publish: ${ref:-this ref} is not a tag."
            exit 0
            ;;
    esac
fi

if [ -n "$archives_dir" ]; then
    while IFS= read -r path; do
        archives+=("$path")
    done < <(find "$archives_dir" -type f \( -name '*.zip' -o -name '*.tar.gz' -o -name '*.tgz' \) | sort)
fi

if [ "${#archives[@]}" -eq 0 ]; then
    echo "::error::No release archives to publish." >&2
    exit 1
fi

channel_of() {
    local base lower
    base=$(basename "$1")
    lower=$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')
    case "$lower" in
        *emscripten*|*html5*|*wasm*)
            echo "::error::Refusing to publish '${base}'. There is no web channel: SDL 3.4.16 GPU has no WebGPU backend (docs/emscripten.md)." >&2
            return 1
            ;;
        *windows*|*win32*|*win64*) printf '%s\n' windows ;;
        *linux*) printf '%s\n' linux ;;
        *macos*|*osx*|*darwin*) printf '%s\n' osx ;;
        *)
            echo "::error::Cannot tell an itch channel from '${base}'. Name the archive with windows or linux." >&2
            return 1
            ;;
    esac
}

slug_for() {
    local base="$1" best="" best_len=0 line sub slug len
    if [ -n "$routes_text" ]; then
        while IFS= read -r line || [ -n "$line" ]; do
            line="${line#"${line%%[![:space:]]*}"}"
            line="${line%"${line##*[![:space:]]}"}"
            case "$line" in
                ''|\#*) continue ;;
            esac
            case "$line" in
                *" "*) ;;
                *)
                    echo "::error::Bad itch-games line '${line}'. Expected '<archive-substring> <slug>'." >&2
                    return 1
                    ;;
            esac
            sub=${line%% *}
            slug=${line#* }
            slug=${slug%% *}
            case "$base" in
                *"$sub"*)
                    len=${#sub}
                    if [ "$len" -gt "$best_len" ]; then
                        best=$slug
                        best_len=$len
                    fi
                    ;;
            esac
        done <<<"$routes_text"
    fi
    if [ -n "$best" ]; then
        printf '%s\n' "$best"
    elif [ -n "$game" ]; then
        printf '%s\n' "$game"
    else
        echo "::error::No itch game slug for '${base}'. Set itch-game, or itch-games with a matching line." >&2
        return 1
    fi
}

for archive in "${archives[@]}"; do
    if [ ! -f "$archive" ]; then
        echo "::error::Archive not found: ${archive}" >&2
        exit 1
    fi
    # Reject a web build even when the push itself will be skipped.
    channel_of "$archive" >/dev/null
done

if [ -z "$user" ]; then
    echo "::notice::Skipping itch.io publish: itch-user is empty. Set the itch-user input when the game page exists."
    exit 0
fi

declare -A seen=()
targets=()
resolved=()
for archive in "${archives[@]}"; do
    channel=$(channel_of "$archive")
    slug=$(slug_for "$(basename "$archive")")
    target="${user}/${slug}:${channel}"
    if [ -n "${seen[$target]:-}" ]; then
        echo "::error::${archive} and ${seen[$target]} both publish to ${target}. Split them across itch games (itch-games) so one channel is not overwritten." >&2
        exit 1
    fi
    seen[$target]=$archive
    targets+=("$target")
    resolved+=("$archive")
done

if [ -z "${BUTLER_API_KEY:-}" ]; then
    echo "::notice::Skipping itch.io publish: BUTLER_API_KEY is not set. Add the itch.io API key as an Actions secret to publish on a tag. Forks and pull requests stay green without it."
    exit 0
fi

if [ -z "$version" ]; then
    echo "::error::No version to pass to butler --userversion. The package step should have read CMAKE_PROJECT_VERSION." >&2
    exit 1
fi

install_butler

i=0
while [ "$i" -lt "${#resolved[@]}" ]; do
    archive=${resolved[$i]}
    target=${targets[$i]}
    cmd=("$BUTLER" push "$archive" "$target" --userversion "$version" --fix-permissions --assume-yes)
    if [ "$dry_run" = true ]; then
        cmd+=(--dry-run)
    fi
    echo "butler push $(basename "$archive") ${target} --userversion ${version}$([ "$dry_run" = true ] && printf ' %s' --dry-run)"
    set +e
    output=$("${cmd[@]}" 2>&1)
    status=$?
    set -e
    printf '%s\n' "$output"
    if [ "$status" -ne 0 ] || printf '%s\n' "$output" | grep -q "No credentials"; then
        echo "::error::butler failed for ${target}" >&2
        exit 1
    fi
    if [ "$dry_run" = true ] && ! printf '%s\n' "$output" | grep -q "Would push"; then
        echo "::error::butler dry run did not list files for ${target}" >&2
        exit 1
    fi
    i=$((i + 1))
done
