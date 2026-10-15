# Releasing a game

How a game repository builds, tests and packages on every push, and publishes a tag to itch.io. The engine's own Windows and Linux workflows stay as they are; a game calls the reusable workflow instead of copying them.

[Back to the README](../README.md)

- [Caller](#caller)
- [Inputs](#inputs)
- [Secrets](#secrets)
- [Channels and version](#channels-and-version)
- [What gets packaged](#what-gets-packaged)
- [Warnings](#warnings)
- [ROCK BLASTER](#rock-blaster)

## Caller

`.github/workflows/game.yml` is a [`workflow_call`](https://docs.github.com/en/actions/sharing-automations/reusing-workflows) workflow. The smallest caller, saved as `.github/workflows/ci.yml` in the game repo:

```yaml
name: CI

on:
  push:
    branches: [main]
    tags: ["v*"]
  pull_request:
  workflow_dispatch:

permissions:
  contents: read

jobs:
  game:
    uses: Ridejock/Emerald/.github/workflows/game.yml@main
    with:
      itch-user: your-user
      itch-game: your-game
      cmake-target: package
      presets: debug,release
      platforms: windows,linux
    secrets:
      BUTLER_API_KEY: ${{ secrets.BUTLER_API_KEY }}
```

Push to `main` or a pull request: configure, build and test each preset, then build `cmake-target` and upload the archives. Push a tag `v1.2.0`: the same, then butler. The workflow runs on every tag the caller triggers; limit that with `tags:` above.

A caller may set its own concurrency group, including `group: ${{ github.workflow }}-${{ github.ref }}`. The reusable workflow uses `emerald-game-${{ github.workflow }}-${{ github.ref }}`. Inside a called workflow `github.workflow` is the caller's workflow name, so an unprefixed group is the same string the caller already holds and GitHub deadlocks the run. The prefix keeps cancel-in-progress without that collision. [docs/examples/game-ci.yml](examples/game-ci.yml) sets the caller's group.

`tools/ci` (the matrix, Linux packages, archive checks, butler) is checked out from the workflow file's own repository at `job.workflow_sha`, not from the game. A caller does not copy those scripts. `GITHUB_WORKFLOW_REF` is the caller, so the workflow does not use it to find Emerald.

`cmake-target` is the target the game already uses to write its archives. For a CPack game that is `package` (`cmake --build --preset release --target package`). For the engine sandbox's Linux archive it is `emerald_package` with `package-preset: linux-release` (the `linux-release` preset and `EMERALD_LINUX_PACKAGE`, the same path as the Linux CI package job). The workflow does not assemble a second zip.

There is no Emscripten or web channel. `platforms: html5` (or `web`, `emscripten`, `wasm`) fails the run. SDL 3.4.16's GPU API has no WebGPU backend ([Emscripten](emscripten.md)).

## Inputs

| Input | Default | Role |
|---|---|---|
| `itch-user` | empty | itch.io account. Empty skips publishing |
| `itch-game` | empty | Slug used for an archive that matches no `itch-games` line |
| `itch-games` | empty | One `<archive-substring> <slug>` per line. Longest match wins |
| `cmake-target` | `package` | Target that writes the `.zip` / `.tar.gz` |
| `presets` | `debug,release` | Presets to configure, build and test |
| `platforms` | `windows,linux` | `windows` and/or `linux` |
| `package-preset` | `release` | Preset whose build produces the archives. Tested in the package job, not again in the test matrix |
| `linux-cmake-args` | `-DEMERALD_SHADER_FORMATS=SPIRV` | Linux only. DXIL crashes the GCC 13 shader compiler; Windows still builds DXIL |
| `cmake-args` | empty | Extra `-D` flags, space-separated, every platform |
| `require-itch-manifest` | `true` | A Windows archive must contain `.itch.toml` |
| `require-dxil` | `true` | A Windows archive must contain a `.dxil` shader |
| `dry-run` | `false` | Run the publish job off a tag and pass `butler push --dry-run`. Still skipped when the API key is unset |

Presets use `build/<preset>` (the `binaryDir` in Emerald's and the games' presets).

## Secrets

| Secret | Required | Role |
|---|---|---|
| `BUTLER_API_KEY` | no | itch.io API key ([itch.io/user/settings/api-keys](https://itch.io/user/settings/api-keys)). Repository secret, Settings → Secrets and variables → Actions. Never commit it |

On a tag, the publish job runs and then skips (exit 0, with a notice) when `BUTLER_API_KEY` is unset, when `itch-user` is empty, or when the caller did not trigger on a tag. A fork and a pull request from a fork do not have the secret; they stay green. `.github/workflows/itch-publish.yml` runs that skip on every engine pull request, and a `butler push --dry-run` that lists files and does not upload.

A wrong key, a missing archive, two archives aimed at the same `user/game:channel`, or a web build fails the job. butler is the pinned Linux build [v15.32.0](https://github.com/itchio/butler/releases/tag/v15.32.0).

## Channels and version

One channel per platform. The channel comes from the archive name (`RockBlaster-1.2.1-windows-x64.zip` → `windows`, `Sandbox-linux-x64.tar.gz` → `linux`).

| Platform | Channel | butler target |
|---|---|---|
| Windows | `windows` | `itch-user/slug:windows` |
| Linux | `linux` | `itch-user/slug:linux` |

macOS is not built here. There is no `html5` channel.

The version is `CMAKE_PROJECT_VERSION`, which CMake sets from the top-level `project(... VERSION ...)`. The package job reads it from the CMake cache and the publish job passes it as `butler push <archive> <target> --userversion <version>`. Windows and Linux must report the same version. Bump the version in `CMakeLists.txt` before tagging.

Two games in one repo (two zips, two itch pages) use `itch-games`. `RockBlaster` is a prefix of `RockBlasterRogue`, so the longer substring is the one that matches the roguelike zip:

```yaml
with:
  itch-user: your-user
  itch-games: |
    RockBlasterRogue rock-blaster-rogue
    RockBlaster rock-blaster
```

That publishes `RockBlasterRogue-1.2.1-windows-x64.zip` to `your-user/rock-blaster-rogue:windows` and `RockBlaster-1.2.1-windows-x64.zip` to `your-user/rock-blaster:windows`. The same pair on Linux uses the `linux` channel. A ready-to-paste caller for that layout is [docs/examples/game-ci.yml](examples/game-ci.yml).

## What gets packaged

The workflow uploads whatever the package target writes:

- A game's CPack zip, next to the build tree: `build/<preset>/*-windows-x64.zip` and, on Linux, `*-linux-x64.zip`. The game's install rules are unchanged, so a Windows zip that already contains `.itch.toml` at its root still does. The check fails the job if that manifest or a `.dxil` shader is missing (`require-itch-manifest` / `require-dxil`).
- The engine sandbox's Linux archive: preset `linux-release`, target `emerald_package`, file `build/linux-release/dist/Sandbox-linux-x64.tar.gz`. Same preset and option as [the Linux release package](building.md#linux-release-package). Set `platforms: linux`, `package-preset: linux-release`, `cmake-target: emerald_package`.

Linux configures with GCC and `EMERALD_SHADER_FORMATS=SPIRV`, matching the engine's Linux package job. Windows keeps the default shader formats, so the zip has DXIL for Direct3D 12. shadercross is still the only shader compiler.

The engine's `.github/workflows/linux.yml` package job is still what uploads `Sandbox-linux-x64.tar.gz` for this repository. Game CI does not replace it.

## Warnings

Every configure in the reusable workflow passes `-DEMERALD_WARNINGS_AS_ERRORS=ON`. There is no input to turn that off.

`emerald_set_warnings(<target>)` is what applies `/WX` or `-Werror`. Call it on each game target. The engine already calls it on its own targets. The option is a cache variable, so FetchContent sees the `-D` flag from the game's cmake command line. Flags are private to the target that was passed in. Dependencies do not call the function, so they are unaffected. Do not add a second warnings function in the game.

An engine pin from before this (the function only added `/W4` or `-Wall -Wextra -Wpedantic`) ignores the flag. Bump the pin and call `emerald_set_warnings`.

The engine workflows do the same for every preset in `CMakePresets.json`: Windows `debug`, `debug-asan`, `debug-full`, `release`, `profile`; Linux `debug`, `release`, `debug-ubsan`, `debug-tsan`, `profile` (GCC and Clang) and the `linux-release` package.

## ROCK BLASTER

ROCK BLASTER and ROCK BLASTER ROGUE are two games in [Ridejock/Asteroids](https://github.com/Ridejock/Asteroids) (`GAME_TITLE` / `PIXEL_TITLE` in that repo's `CMakeLists.txt`). There is no separate Rock Blaster or Rock Blaster Rogue repository. Each zip already carries `.itch.toml` (a Play action for `RockBlaster.exe` or `RockBlasterRogue.exe`), built by `cmake/Packaging.cmake` there. The itch pages are [rock-blaster](https://ridejock.itch.io/rock-blaster) and [rock-blaster-rogue](https://ridejock.itch.io/rock-blaster-rogue).

[docs/examples/game-ci.yml](examples/game-ci.yml) is that caller: Windows and Linux, target `package`, user `ridejock`, the two slugs, the caller's own concurrency group, and the existing GitHub Release attachment as a second job. It does not vendor `tools/ci`. Each game target calls `emerald_set_warnings` so the warnings-as-errors flag applies; an older pin needs a bump, not a local copy of the function. Set the repository secret `BUTLER_API_KEY` before a tag should upload.
