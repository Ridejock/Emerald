#!/usr/bin/env bash
# Checks the itch publish script and the workflow matrix. With --butler, also
# downloads the pinned butler and dry-runs a push (nothing is uploaded).
# SC2030/2031: each case sets BUTLER_API_KEY inside its own subshell on purpose.
# shellcheck disable=SC2030,SC2031
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
with_butler=false
if [ "${1:-}" = "--butler" ]; then
    with_butler=true
fi

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

matrix() {
    "$root/game-matrix.sh" "$@"
}

out=$(matrix --platforms windows,linux --presets debug,release --package-preset release --cmake-target package)
printf '%s\n' "$out" | grep -q '^has-test=true$' || fail "debug should be tested when release is the package preset"
if printf '%s\n' "$out" | grep '^test-matrix=' | grep -q '"preset":"release"'; then
    fail "release is packaged, so it should not also be a test-matrix row"
fi
package_json=$(printf '%s\n' "$out" | sed -n 's/^package-matrix=//p')
[ "$(printf '%s' "$package_json" | jq -r '.include[] | select(.platform=="windows") | .cmake_args')" = "" ] \
    || fail "windows cmake args should stay empty"
printf '%s' "$package_json" | jq -e '.include[] | select(.platform=="linux") | .cmake_args | contains("EMERALD_SHADER_FORMATS=SPIRV")' >/dev/null \
    || fail "linux should request SPIR-V only"
[ "$(printf '%s' "$package_json" | jq -r '.include[] | select(.platform=="linux") | .runs_on')" = "ubuntu-24.04" ] \
    || fail "linux runner should be ubuntu-24.04"
[ "$(printf '%s' "$package_json" | jq -r '.include[] | select(.platform=="windows") | .target')" = "package" ] \
    || fail "cmake target should be package"

out=$(matrix --platforms windows --presets release --package-preset release --cmake-target package)
printf '%s\n' "$out" | grep -q '^has-test=false$' || fail "package preset alone should skip the test job"

if matrix --platforms html5 --presets release >/dev/null 2>"$tmpdir/web.err"; then
    fail "html5 platform should be rejected"
fi
grep -q 'WebGPU' "$tmpdir/web.err" || fail "web rejection should point at the SDL GPU blocker"

if matrix --platforms macos --presets release >/dev/null 2>"$tmpdir/mac.err"; then
    fail "macos is not built by this workflow"
fi

ref='Ridejock/Emerald/.github/workflows/game.yml@refs/heads/cursor/reusable-game-ci-441d'
[ "${ref%%/.github/*}" = "Ridejock/Emerald" ] || fail "workflow ref did not yield the repo"

# --- packages ---------------------------------------------------------------
pkg=$tmpdir/pkg
mkdir -p "$pkg/build/release/dist" "$tmpdir/stage/shaders"
printf 'float4 main() : SV_Target0 { return 1; }\n' >"$tmpdir/stage/shaders/Renderer2D.frag.dxil"
printf '[[actions]]\nname = "play"\npath = "RockBlaster.exe"\n' >"$tmpdir/stage/.itch.toml"
(
    cd "$tmpdir/stage"
    cmake -E tar cf "$pkg/build/release/RockBlaster-1.2.1-windows-x64.zip" --format=zip .itch.toml shaders/Renderer2D.frag.dxil
)
printf 'CMAKE_PROJECT_VERSION:STATIC=1.2.1\n' >"$pkg/build/release/CMakeCache.txt"
mkdir -p "$pkg/out"
(cd "$pkg" && "$root/collect-packages.sh" --preset release --dest "$pkg/out" --platform windows)
[ "$(cat "$pkg/out/VERSION")" = "1.2.1" ] || fail "version file"
[ -f "$pkg/out/RockBlaster-1.2.1-windows-x64.zip" ] || fail "windows zip was not copied"

mkdir -p "$tmpdir/nomanifest/shaders"
cp "$tmpdir/stage/shaders/Renderer2D.frag.dxil" "$tmpdir/nomanifest/shaders/"
(
    cd "$tmpdir/nomanifest"
    cmake -E tar cf "$pkg/build/release/RockBlaster-1.2.1-windows-x64.zip" --format=zip shaders/Renderer2D.frag.dxil
)
if (cd "$pkg" && "$root/collect-packages.sh" --preset release --dest "$pkg/out" --platform windows) >"$tmpdir/nomanifest.out" 2>&1; then
    fail "a windows zip without .itch.toml should fail"
fi
grep -q 'itch.toml' "$tmpdir/nomanifest.out" || fail "missing manifest error should name .itch.toml"

mkdir -p "$tmpdir/linux/Sandbox-linux-x64"
echo sandbox >"$tmpdir/linux/Sandbox-linux-x64/Sandbox"
(
    cd "$tmpdir/linux"
    cmake -E tar czf "$pkg/build/release/dist/Sandbox-linux-x64.tar.gz" Sandbox-linux-x64
)
rm -f "$pkg/build/release/"*.zip
mkdir -p "$pkg/out-linux"
(cd "$pkg" && "$root/collect-packages.sh" --preset release --dest "$pkg/out-linux" --platform linux)
[ -f "$pkg/out-linux/Sandbox-linux-x64.tar.gz" ] || fail "linux archive was not copied"

# --- publish ----------------------------------------------------------------
mkdir -p "$tmpdir/zips"
vector=$tmpdir/zips/RockBlaster-1.2.1-windows-x64.zip
rogue=$tmpdir/zips/RockBlasterRogue-1.2.1-windows-x64.zip
linux_arc=$tmpdir/zips/Sandbox-linux-x64.tar.gz
(
    cd "$tmpdir/stage"
    cmake -E tar cf "$vector" --format=zip .itch.toml shaders/Renderer2D.frag.dxil
    cmake -E tar cf "$rogue" --format=zip .itch.toml shaders/Renderer2D.frag.dxil
)
cp "$pkg/out-linux/Sandbox-linux-x64.tar.gz" "$linux_arc"
echo placeholder >"$tmpdir/zips/game-html5.zip"

fake=$tmpdir/butler
cat >"$fake" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"${BUTLER_LOG:?}"
if [[ "$*" == *"--dry-run"* ]]; then
    echo "• Dry run, listing files we would push..."
    echo "✓ Would push 1 B"
fi
exit 0
EOF
chmod +x "$fake"

# Inherit BUTLER_API_KEY / ITCH_ROUTES from the caller; clear the rest so
# tests do not leak into each other.
run_pub() {
    env -u ITCH_USER -u ITCH_GAME \
        BUTLER="$fake" BUTLER_LOG="$tmpdir/butler.log" \
        "$@"
}

: >"$tmpdir/butler.log"
skip=$(
    unset BUTLER_API_KEY ITCH_ROUTES
    run_pub "$root/publish-itch.sh" --force --user demo --game sandbox --version 0.1.0 "$vector"
)
printf '%s\n' "$skip" | grep -q 'BUTLER_API_KEY' || fail "missing key should say it skipped: $skip"
[ ! -s "$tmpdir/butler.log" ] || fail "missing key must not invoke butler"

: >"$tmpdir/butler.log"
skip=$(
    unset BUTLER_API_KEY ITCH_ROUTES
    GITHUB_REF=refs/heads/main run_pub \
        "$root/publish-itch.sh" --user demo --game sandbox --version 0.1.0 "$vector"
)
printf '%s\n' "$skip" | grep -q 'not a tag' || fail "a branch push should skip: $skip"
[ ! -s "$tmpdir/butler.log" ] || fail "a branch push must not invoke butler"

: >"$tmpdir/butler.log"
skip=$(
    unset BUTLER_API_KEY ITCH_ROUTES
    run_pub "$root/publish-itch.sh" --force --game sandbox --version 0.1.0 "$vector"
)
printf '%s\n' "$skip" | grep -q 'itch-user' || fail "empty itch user should skip: $skip"
[ ! -s "$tmpdir/butler.log" ] || fail "empty itch user must not invoke butler"

: >"$tmpdir/butler.log"
if (
    unset ITCH_ROUTES
    export BUTLER_API_KEY=test
    run_pub "$root/publish-itch.sh" --force --user demo --game sandbox --version 1.2.1 \
        "$tmpdir/zips/game-html5.zip" >"$tmpdir/webpub.out" 2>&1
); then
    fail "html5 archive should be refused"
fi
grep -q 'WebGPU' "$tmpdir/webpub.out" || fail "web refusal should mention the blocker"
[ ! -s "$tmpdir/butler.log" ] || fail "web archive must not be pushed"

routes=$'RockBlasterRogue rock-blaster-rogue\nRockBlaster rock-blaster\n'
: >"$tmpdir/butler.log"
(
    unset ITCH_ROUTES
    export BUTLER_API_KEY=test
    export ITCH_ROUTES="$routes"
    run_pub \
        "$root/publish-itch.sh" --force --user ridejock --version 1.2.1 "$rogue" "$vector" \
        >"$tmpdir/push.out"
)
grep -q "push ${rogue} ridejock/rock-blaster-rogue:windows" "$tmpdir/butler.log" \
    || fail "rogue zip should use the longer slug. log: $(cat "$tmpdir/butler.log")"
grep -q "push ${vector} ridejock/rock-blaster:windows" "$tmpdir/butler.log" \
    || fail "vector zip should use rock-blaster. log: $(cat "$tmpdir/butler.log")"
grep -q -- '--userversion 1.2.1' "$tmpdir/butler.log" || fail "userversion was not passed"
if grep -q -- '--dry-run' "$tmpdir/butler.log"; then
    fail "a real push must not pass --dry-run"
fi

: >"$tmpdir/butler.log"
if (
    unset ITCH_ROUTES
    export BUTLER_API_KEY=test
    export ITCH_ROUTES="$routes"
    run_pub \
        "$root/publish-itch.sh" --force --user ridejock --version 1.2.1 "$linux_arc" >"$tmpdir/noslug.out" 2>&1
); then
    fail "an archive with no slug should fail"
fi
grep -q 'No itch game slug' "$tmpdir/noslug.out" || fail "missing slug error. out: $(cat "$tmpdir/noslug.out")"

: >"$tmpdir/butler.log"
(
    unset ITCH_ROUTES
    export BUTLER_API_KEY=test
    export ITCH_ROUTES="$routes"
    run_pub \
        "$root/publish-itch.sh" --dry-run --user ridejock --game sandbox --version 0.1.0 "$linux_arc" \
        >"$tmpdir/dry.out"
)
grep -q -- '--dry-run' "$tmpdir/butler.log" || fail "dry run should pass --dry-run"
grep -q 'Would push' "$tmpdir/dry.out" || fail "dry run should list files"
grep -q 'sandbox:linux' "$tmpdir/butler.log" || fail "linux channel. log: $(cat "$tmpdir/butler.log")"

: >"$tmpdir/butler.log"
if (
    unset ITCH_ROUTES
    export BUTLER_API_KEY=test
    run_pub \
        "$root/publish-itch.sh" --force --user ridejock --game same --version 1.0.0 "$vector" "$rogue" \
        >"$tmpdir/dup.out" 2>&1
); then
    fail "two windows zips for one game should fail"
fi
grep -q 'both publish' "$tmpdir/dup.out" || fail "duplicate channel error. out: $(cat "$tmpdir/dup.out")"

echo "publish-itch tests passed"

if [ "$with_butler" = true ]; then
    bin=$(env -u BUTLER "$root/publish-itch.sh" --install-only)
    [ -x "$bin" ] || fail "butler was not installed"
    fixture=$tmpdir/dry.zip
    (
        cd "$tmpdir/stage"
        cmake -E tar cf "$fixture" --format=zip .itch.toml shaders/Renderer2D.frag.dxil
    )
    dry=$("$bin" push "$fixture" demo/sandbox:linux --userversion 0.1.0 --dry-run 2>&1)
    printf '%s\n' "$dry"
    printf '%s\n' "$dry" | grep -q 'Would push' || fail "real butler dry run did not list files"
    echo "butler dry run passed"
fi
