# Platforms

Linux, Steam Deck and the differences from the Windows build: where files go, how fullscreen
works, and how the Deck's controller shows up. The browser build is a separate spike
([Emscripten](emscripten.md)).

[Back to the README](../README.md)

- [Linux package](#linux-package)
- [Per-user paths (XDG)](#per-user-paths-xdg)
- [Fullscreen](#fullscreen)
- [Steam Deck](#steam-deck)
- [Input](#input)

## Linux package

Build and archive layout: [Linux release package](building.md#linux-release-package). Short
version:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
```

The archive is `build/linux-release/dist/Sandbox-linux-x64.tar.gz`. SDL3 is linked into the
binary. RPATH is `$ORIGIN` and `$ORIGIN/lib`, so a library placed next to `Sandbox` or in `lib/`
is found without `LD_LIBRARY_PATH`. `Sandbox.sh` changes to the archive directory and execs the
binary; `Paths::GetBasePath()` is the executable's folder either way, so assets and shaders do
not depend on the working directory.

The Linux CI package job builds this with GCC, checks the RPATH and that `libSDL3` / `libstdc++`
/ `libgcc_s` are not dynamic dependencies, runs the extracted binary for a few frames on lavapipe,
and uploads the tar.gz.

## Per-user paths (XDG)

`Paths::GetPrefPath(org, app)` is the writable folder for saves and settings (see [Core](core.md#logging)).
On Linux, SDL follows the XDG Base Directory spec:

- `$XDG_DATA_HOME/<org>/<app>/` when `XDG_DATA_HOME` is set
- otherwise `~/.local/share/<org>/<app>/`

The directory is created if it is missing. The sandbox's options file is
`<that folder>/saves/settings.sav`. Logs stay next to the executable (`logs/Emerald.log`), not
under XDG: a relative `ApplicationSpec::LogFile` is resolved with `GetBasePath()`.

Windows uses `%APPDATA%\<org>\<app>\`. macOS uses `~/Library/Application Support/<org>/<app>/`.

## Fullscreen

`Window::SetFullscreen(true)` calls `SDL_SetWindowFullscreenMode(window, nullptr)` and then
`SDL_SetWindowFullscreen`. A null mode is borderless desktop fullscreen: the display mode is not
changed, and alt-tab does not have to restore a resolution.

That is the right behaviour on Wayland. Steam Deck Gaming Mode runs the game under gamescope,
which also wants a fullscreen window rather than an exclusive mode switch; gamescope scales the
window to the 1280×800 panel. `WindowSpec::Fullscreen` starts the window that way.
`IsSteamDeck()` is true when Steam has set `SteamDeck=1` (Gaming Mode and Desktop Mode). The
sandbox turns fullscreen on in that case. A saved options file can turn it off again on the next
launch (`video.fullscreen` in the settings save).

## Steam Deck

**Untested on real hardware.** Nothing in this tree has been run on a physical Steam Deck or on
SteamOS. The package is built and smoke-tested on Ubuntu 24.04 (software Vulkan under Xvfb). The
controller behaviour below is what SDL 3.4.16's gamepad API does with the Deck's USB id and
HIDAPI driver, checked by unit tests with a synthetic pad, not by holding the device.

To try the archive on a Deck once you have one:

1. Copy `Sandbox-linux-x64.tar.gz` to the Deck (Desktop Mode: `scp`, a USB drive, or Steam's file
   transfer) and extract it: `tar -xzf Sandbox-linux-x64.tar.gz`.
2. In Desktop Mode, open Steam → Games → Add a Non-Steam Game, and browse to `Sandbox.sh` inside
   the extracted folder. (Pointing Steam at `Sandbox` directly also works; the script only fixes
   the working directory.)
3. Return to Gaming Mode and launch the new entry. The sandbox starts borderless fullscreen
   because Steam sets `SteamDeck=1`.

The Deck panel is 1280×800. A windowed 1280×720 sandbox leaves a strip; fullscreen does not.

## Input

Default actions are positional (`GamepadButton::South`, `East`, the d-pad, the left stick), so
one set of bindings covers Xbox, PlayStation, Switch and the Deck. Prompts use
`GetButtonLabel`, which follows the pad that was used last.

SDL 3.4.16 has a HIDAPI driver for the built-in controller (vendor `0x28DE`, product `0x1205`,
name `"Steam Deck"`). Emerald turns `SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK` on before the gamepad
subsystem starts, next to the PS4, PS5 and Switch hints. That driver maps the printed buttons
onto SDL's standard layout:

| On the Deck | SDL button | Prompt |
|---|---|---|
| A, B, X, Y | South, East, West, North | A, B, X, Y |
| Bumpers | Left / Right shoulder | L1, R1 |
| Triggers | Left / Right trigger | L2, R2 |
| View, Menu | Back, Start | View, Menu |
| Rear L4, R4, L5, R5 | Left / Right paddle 1 and 2 | L4, R4, L5, R5 |

SDL's own type for this device is `SDL_GAMEPAD_TYPE_STANDARD`, whose face labels are already
A/B/X/Y (the same positions as the printing). `IsSteamDeckGamepad` recognises the name or the
USB id, and `GetButtonLabel` then uses the Deck wording for shoulders, triggers and paddles.
`BindDefaultUiActions` binds the d-pad and the left stick for focus, South for accept and East
for back, so a Deck menu moves with the d-pad or the stick and confirms with A.

### Steam Input

Steam Input sits in front of the raw controller whenever the game is launched by Steam, which
includes a non-Steam game in Gaming Mode. What SDL sees then is not the HIDAPI "Steam Deck"
device:

- Gaming Mode's default for a non-Steam shortcut is a gamepad template. SDL receives a virtual
  Xbox pad (often named like an Xbox 360 controller). Face buttons are still South = A, East = B,
  West = X, North = Y, so the default bindings and the on-screen A/B/X/Y prompts match the Deck.
  Shoulder prompts say LB/RB, which are the same bumpers. The rear paddles are handled by the
  Steam Input config, not as SDL paddle buttons, unless the template passes them through.
- Desktop Mode can apply a desktop configuration (keyboard and mouse) instead of a pad. If the
  shortcut does that, SDL sees no gamepad. In the shortcut's properties, set Steam Input to a
  gamepad template, or turn Steam Input off.
- Turning Steam Input off for the shortcut lets SDL's HIDAPI driver open the controller itself.
  Prompts switch to the Deck wording (L1/R1, L4–R5), and the rear paddles become
  `GamepadButton::LeftPaddle1` and the other paddle values. The Steam button and the Quick
  Access button (QAM) are usually kept by the Steam client either way.
- `SteamAPI_Init` is not required. Emerald only uses SDL's gamepad API. A game that also links
  the Steamworks SDK must call `SteamAPI_Init` before `SDL_Init`, which is SDL's rule for Steam
  virtual gamepads, not something Emerald does.

The same Steam Input note applies to other pads: a PlayStation or Switch pad can show up as an
Xbox pad while Steam Input owns it. Bindings by position still work; the prompt glyphs follow
whatever type SDL reports.
