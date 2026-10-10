# Assets, audio and saves

The asset manager, audio, and save files.

[Back to the README](../README.md)

- [Assets (Assets)](#assets-assets)
- [Audio](#audio)
- [Saves (Save/Save.h)](#saves-savesaveh)

## Assets (`Assets`)

The asset manager loads textures, atlases, fonts, sounds, tilemaps, dialogue decks and particle effects by path and hands out
`AssetHandle<T>`s. Application owns one (`GetAssets()`):

```cpp
// In OnStart (relative paths start at Paths::GetBasePath(), or at SetRoot's folder):
m_Ship = GetAssets().Load<Emerald::Texture>("assets/ship.png");
m_Hero = GetAssets().Load<Emerald::TextureAtlas>("assets/hero.json"); // + hero.png next to it
m_Font = GetAssets().Load<Emerald::Font>("assets/ui.ttf", {.Size = 16.0f});
m_Boom = GetAssets().Load<Emerald::Sound>("assets/boom.wav");
m_Level = GetAssets().Load<Emerald::Tilemap>("assets/level1.tmj"); // see Tilemaps

// Later: use the handle like a pointer.
r.DrawSprite(*m_Ship, position);
r.DrawString(*m_Font, "SCORE", {20, 20}, white);
GetAudio().Play(*m_Boom);
```

- **Loaded once.** Loading the same file again returns the same handle, and the file is not read
  again. The file counts as the same however its path is written (`"a/../ship.png"` =
  `"ship.png"`, and on Windows any letter case). The type and the options count too: the same
  TTF at two sizes is two fonts.
- **Reference counted.** Handles copy like `std::shared_ptr`. When the last handle to an asset
  is gone, the asset is unloaded at the start of the next frame, so a texture dropped mid-frame is
  still there when that frame is drawn. Handles must be released before the manager is destroyed;
  members of your Application are.
- **Missing or broken files never crash.** The error is logged and you get a placeholder that is
  easy to spot on screen:
  - textures: a magenta/black checkerboard;
  - atlases: every sprite and animation shows that checkerboard;
  - fonts: every character is a hollow box;
  - sounds: a tenth of a second of silence;
  - tilemaps: an empty map (no layers, nothing collides);
  - dialogue decks: a deck with no cards (starting it logs an error and does nothing);
  - particle effects: the default `ParticleEmitterConfig` (white points) with a burst of 0.
- **Hot reload (debug builds).** Edit a PNG, an atlas JSON, a WAV/MP3, a font, a Tiled map
  (or one of its tilesets), a dialogue deck or a particle effect while the game runs, and it updates within about half a second. Release builds compile this out.

How hot reload works:

1. Every 0.25 s, a thread pool task reads the modification time of every loaded file. Only the
   time is read, not the file.
2. Back on the main thread, `Assets::Update` (called by Application before each frame) compares
   the times. A file counts as changed once its time differs from when it was loaded and has stayed
   the same for one more check. That way a file the editor is still writing is not read half
   done.
3. The asset is loaded again on the main thread, since GPU uploads happen there. The new version
   then replaces the old one *in place*, inside the same object, so nothing pointing at it
   breaks:
   - handles keep working;
   - a `Sprite` keeps pointing at the atlas texture;
   - an `Animator` keeps pointing at its animation (if the reloaded animation has fewer frames,
     the animator wraps around).
4. If the new version does not load (say, a half-saved PNG), the old one stays and the next
   change tries again. A placeholder whose file shows up later is loaded the same way.

The manager only does the bookkeeping. An `AssetLoader` does the actual loading:
`GpuAssetLoader` for the real thing, or a GPU-less fake in tests (see `tests/AssetTests.cpp`).

In the debug sandbox, the assets come straight from `sandbox/assets/`:

- Recolor `sprites/hero.png` while it runs and the hero changes.
- `sprites/missing.png` doesn't exist, so the sandbox shows the checkerboard placeholder until
  you put a PNG there.
- The ImGui panel lists every asset with its reference count, reloads and whether it is a
  placeholder.

## Audio

`include/Emerald/Audio/`: load or generate sounds, then play them through `Application::GetAudio()`.

```cpp
std::optional<Emerald::Sound> boom = Emerald::LoadSound("assets/boom.mp3"); // .wav or .mp3
Emerald::Audio& audio = GetAudio();
if (boom)
    audio.Play(*boom, {.Volume = 0.8f, .Pan = -0.3f});          // fire and forget

Emerald::VoiceHandle engine = audio.Play(hum, {.Volume = 0.5f, .Loop = true});
audio.SetVolume(engine, 0.2f);   // ramped, no click
audio.Stop(engine, 80.0f);       // fade out over 80 ms (default 10 ms)
audio.IsPlaying(engine);         // false once it has faded out
audio.SetMasterVolume(0.7f);
audio.SetMuted(!audio.IsMuted());

audio.SetGroupVolume(Emerald::AudioGroup::Music, 0.8f);
audio.SetListener(camera.GetPosition());
audio.Play(sfx, {.Position = enemyPos, .MinDistance = 64.0f, .MaxDistance = 512.0f});
audio.PlayMusic("assets/audio/loop_a.ogg", 400.0f);
audio.CrossfadeMusic("assets/audio/loop_b.ogg", 800.0f);
```

- **Loading**: `LoadSound(path)` picks the loader by extension (case-insensitive): `.wav` →
  `LoadWav` (`SDL_LoadWAV`), `.mp3` → `LoadMp3` ([dr_mp3](https://github.com/mackron/dr_libs),
  compiled once in `src/Vendor/dr_mp3.cpp`). MP3s are decoded to f32 at the file's own rate and
  channel count, then everything is converted with `SDL_ConvertAudioSamples` to the **mix format**
  `kMixSpec` (f32, stereo, 48 kHz). Failures return `std::nullopt` and are logged.
  `LoadMp3FromMemory` decodes embedded data, `MakeSound(samples, channels, rate)` converts
  generated samples. A `Sound` shares its (immutable) samples, so copies are cheap and a playing
  sound stays valid even if the game drops its `Sound`.
- **Synth** (`Synth.h`): `Generate(Tone)` makes sine, square/pulse (`Duty`), triangle, saw or
  noise with an optional exponential pitch sweep (`StartHz` → `EndHz`; for noise the frequency sets
  how bright it is) and vibrato (`VibratoHz`, `VibratoDepth`: e.g. 5 Hz, ±20% for a UFO warble); shape it with `ApplyDecay`, `ApplyAdsr`, `LowPass` (one-pole) and `MixInto`,
  then `ToSound`. Asteroids generates all its sounds this way.
- **Mixer** (`Mixer.h`, owned by `Audio`): 32 voices. `Play` returns a `VoiceHandle` that remembers
  the voice's generation, so a handle to a sound that has ended (and whose voice was reused) is a
  safe no-op. `PlayOptions`: `Volume`, `Pan` (-1..1), `Pitch` (speed, linear interpolation),
  `Loop`, `FadeInMs` (2 ms), `Group` (`AudioGroup::Sfx` / `Music` / `Ui`), and optional `Position`
  / `MinDistance` / `MaxDistance` for spatial playback. Every gain change is ramped per sample
  (start, `Stop` fades, `SetVolume`, master volume, mute, group volume/mute), so nothing clicks.
  The master output goes through a soft limiter (unchanged up to 0.8, then eases towards 1.0), so
  ten explosions at once get louder without harsh clipping. When all voices are busy, the oldest
  non-looping sound is cut off.
- **Groups**: `SetGroupVolume` / `SetGroupMuted` on `Audio` (or `Mixer`) for Music, Sfx and Ui;
  each voice carries its group. Streaming music is mixed into the Music group gain.
- **Spatial** (`Spatial.h`): linear falloff between min/max distance and balance pan from relative
  X. `SetListener` (usually the camera) and `SetVoicePosition` update pans/volumes on the fly.
  Pure CPU math — covered by `AudioExtrasTests` with no device.
- **Music** (`Music.h`): `MusicStream` decodes `.mp3` (dr_mp3) or `.ogg` (stb_vorbis) a chunk at a
  time into a fixed ~0.25 s stereo ring (constant PCM memory); `MusicPlayer` holds two streams for
  Play / Crossfade / Stop. Prefer SDL3 core audio only (no SDL_mixer). Via `Audio`:
  `PlayMusic` / `CrossfadeMusic` / `StopMusic` / `SetMusicVolume`. The sandbox Options screen has
  Music group / track sliders and Play A / Crossfade B / Stop using `assets/audio/loop_*.ogg`.
- **Threads**: SDL calls the mixer from its audio thread with the audio stream locked; every
  `Audio` method locks the same stream (for microseconds), so the game thread and the audio thread
  never touch mixer state at the same time. The audio thread never allocates or frees: finished
  sounds are released by `Audio::Update()` on the main thread (the Application calls it every
  frame). Call `Audio` from the main thread only.
- Without an audio device the app runs normally and `Play` returns an invalid handle (a warning is
  logged). `SDL_AUDIO_DRIVER=disk` writes the output to a raw file instead (handy for testing).

## Saves (`Save/Save.h`)

Versioned save slots in the per-user folder: one small text file per slot, migrations from older
data versions, and atomic writes.

```cpp
// Data version 2 of the game; the folder is %APPDATA%\Ridejock\RockBlaster\saves on Windows.
Emerald::SaveSystem saves(Emerald::SaveSystem::DefaultFolder("Ridejock", "RockBlaster"), 2);
saves.AddMigration(1, [](Emerald::SaveData& d) { d.Rename("volume", "audio.master"); }); // v1 -> v2

Emerald::SaveData settings;
settings.SetFloat("audio.master", 0.8f);
settings.SetBool("video.fullscreen", true);
saves.Save("settings", settings);

if (Emerald::LoadResult loaded = saves.Load("settings"))
    volume = loaded.Data.GetFloat("audio.master", 1.0f); // the fallback if the key is missing
else if (loaded.Error != Emerald::SaveError::NotFound)
    EM_WARN("Settings: {}", loaded.Message);
```

- **Data**: `SaveData` maps keys (letters, digits, `. _ -`) to ints (`i32`), floats, bools and
  strings. Getters take a fallback, so a new key read from an old save just gets its default. A
  table is numbered keys: `scores.count`, `scores.0.name`, `scores.0.points`, ...
- **Slots**: any name of letters, digits, `_` and `-` (`settings`, `highscores`, `slot_1`), stored
  as `<folder>/<name>.sav`. `ListSlots()` returns every slot with `Exists`, `Valid`, `Version`,
  `SavedAt` (Unix seconds) and the `Summary` passed to `Save`; `GetInfo`, `Exists`, `Delete`.
- **Format**: text, keys sorted, one value per line (newlines and backslashes escaped):

  ```
  EMERALD-SAVE 1
  version = 1
  saved = 1791297947
  summary = second
  crc32 = eabaa043
  ---
  audio.master = 0.25
  player.name = Ervin
  video.fullscreen = true
  ```

  `EMERALD-SAVE 1` is the container format, `version` the game's data version. The CRC-32 covers
  everything after `---`, so truncated or damaged files are detected. Delete the `crc32` line to
  edit a save by hand.
- **Versions**: `AddMigration(n, f)` registers the step n -> n+1; `Load` runs the chain from the
  file's version up to the current one (`LoadResult::FileVersion` tells where it started). A save
  from a newer version fails with `SaveError::TooNew`, a gap in the chain with `NoMigration`; the
  data is never half-migrated. `Load` doesn't write the upgraded save back; save it when you like.
- **Atomic writes**: `Save` writes `<name>.sav.tmp`, flushes it to the disk (`SDL_FlushIO`:
  `FlushFileBuffers` / `fsync`), then renames it over `<name>.sav` (`SDL_RenamePath`:
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` on Windows, `rename()` elsewhere). If anything fails,
  `Save` returns false (logged) and the old file is untouched. The previous good save is kept as
  `<name>.sav.bak`, and `Load` falls back to it (`FromBackup`) when `<name>.sav` is damaged.
- `SaveSystem::Serialize` / `Parse` work on strings, for tests and tools.
- The sandbox keeps its Options screen (volumes, CRT, fullscreen, highlight color) in
  `settings.sav` under `%APPDATA%\Emerald\Sandbox\saves\` (Linux:
  `~/.local/share/Emerald/Sandbox/saves/`): saved when leaving Options, loaded at startup, except
  in `--frames` / `--replay` runs.
