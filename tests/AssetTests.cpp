// The asset manager's bookkeeping, without a GPU: deduplication, reference counting and
// unloading, placeholders for missing files, and hot reload (debug builds) of textures, atlases,
// fonts and sounds - all through a loader that makes GPU-less stand-ins.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <Emerald/Assets/Assets.h>
#include <Emerald/Core/Log.h>
#include <Emerald/Core/ThreadPool.h>
#include <Emerald/Math/Common.h>

#include "Test.h"

using namespace Emerald;
namespace fs = std::filesystem;

namespace {

std::optional<std::string> ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes a file and moves its modification time forward, so a quick rewrite still counts as a
// change even on file systems with coarse timestamps.
void WriteText(const fs::path& path, const std::string& text)
{
    const bool existed = fs::exists(path);
    const fs::file_time_type before = existed ? fs::last_write_time(path) : fs::file_time_type{};
    std::ofstream(path, std::ios::binary) << text;
    if (existed)
        fs::last_write_time(path, before + std::chrono::seconds(2));
}

// A mono 16-bit 48 kHz WAV of `frames` samples at `level` (same layout as AudioTests).
void WriteWav(const fs::path& path, u32 frames, i16 level)
{
    const bool existed = fs::exists(path);
    const fs::file_time_type before = existed ? fs::last_write_time(path) : fs::file_time_type{};
    const auto u32le = [](std::ofstream& f, u32 v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    };
    const auto u16le = [](std::ofstream& f, u16 v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    };
    const std::vector<i16> samples(frames, level);
    const u32 dataBytes = frames * 2;
    {
        std::ofstream f(path, std::ios::binary);
        f.write("RIFF", 4);
        u32le(f, 36 + dataBytes);
        f.write("WAVEfmt ", 8);
        u32le(f, 16);
        u16le(f, 1);         // PCM
        u16le(f, 1);         // mono
        u32le(f, 48000);     // sample rate
        u32le(f, 48000 * 2); // bytes per second
        u16le(f, 2);         // bytes per frame
        u16le(f, 16);        // bits per sample
        f.write("data", 4);
        u32le(f, dataBytes);
        f.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    }
    if (existed)
        fs::last_write_time(path, before + std::chrono::seconds(2));
}

// "Images" are text files holding their width ("32"): the stand-in texture gets that width.
// Anything else (or a missing file) fails to load, like a broken PNG would.
class FakeLoader final : public AssetLoader {
public:
    u32 Loads = 0; // successful and failed attempts

    std::optional<Texture> LoadTexture(const fs::path& file, const TextureOptions&) override
    {
        ++Loads;
        const std::optional<u32> width = ReadWidth(file);
        if (!width)
            return std::nullopt;
        return Texture::CreateWithoutGpu(*width, 16);
    }
    std::optional<TextureAtlas> LoadAtlas(const fs::path& image, const fs::path& json,
                                          const TextureOptions&) override
    {
        ++Loads;
        const std::optional<u32> width = ReadWidth(image);
        const std::optional<std::string> text = ReadText(json);
        if (!width || !text)
            return std::nullopt;
        std::optional<TextureAtlas::RegionMap> regions = TextureAtlas::ParseRegions(*text);
        if (!regions)
            return std::nullopt;
        return TextureAtlas::Create(Texture::CreateWithoutGpu(*width, 16), std::move(*regions),
                                    TextureAtlas::ParseAnimations(*text));
    }
    std::optional<Font> LoadFont(const fs::path& file, const FontOptions& options) override
    {
        ++Loads;
        return Font::Load(nullptr, file, options); // fonts work without a GPU device
    }
    std::optional<Sound> LoadSound(const fs::path& file) override
    {
        ++Loads;
        const std::optional<u32> frames = ReadWidth(file); // "sounds" hold their frame count
        if (!frames)
            return std::nullopt;
        return Sound(std::vector<f32>(*frames * 2, 0.5f));
    }
    std::optional<Tilemap> LoadTilemap(const fs::path& file, const TextureOptions& options) override
    {
        ++Loads;
        return Tilemap::Load(nullptr, file, options); // no device: textures without GPU
    }

    Texture MakePlaceholderTexture() override { return Texture::CreateWithoutGpu(64, 64); }
    TextureAtlas MakePlaceholderAtlas() override
    {
        return TextureAtlas::CreatePlaceholder(Texture::CreateWithoutGpu(64, 64));
    }
    Font MakePlaceholderFont(const FontOptions& options) override
    {
        return Font::CreatePlaceholder(nullptr, options);
    }

private:
    static std::optional<u32> ReadWidth(const fs::path& file)
    {
        const std::optional<std::string> text = ReadText(file);
        if (!text || text->empty() || (*text)[0] < '0' || (*text)[0] > '9')
            return std::nullopt;
        return static_cast<u32>(std::stoul(*text));
    }
};

// A fresh folder for one test, with an Assets manager rooted in it.
struct Fixture {
    fs::path Root;
    FakeLoader* Loader = nullptr; // owned by Manager
    std::unique_ptr<Assets> Manager;

    explicit Fixture(const char* name, ThreadPool* pool = nullptr)
        : Root(fs::temp_directory_path() / "emerald_asset_tests" / name)
    {
        Log::Init({}); // console only: the manager logs loads, placeholders and reloads
        fs::remove_all(Root);
        fs::create_directories(Root / "sub");
        auto loader = std::make_unique<FakeLoader>();
        Loader = loader.get();
        Manager = std::make_unique<Assets>(std::move(loader), Root, pool);
    }
    ~Fixture()
    {
        Manager.reset();
        fs::remove_all(Root);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

u32 RefCount(const Assets& assets, AssetId id)
{
    const std::optional<AssetInfo> info = assets.GetInfo(id);
    return info ? info->RefCount : 0;
}

} // namespace

TEST(AssetsDeduplicate)
{
    Fixture f("dedupe");
    WriteText(f.Root / "ship.png", "32");
    Assets& assets = *f.Manager;

    AssetHandle<Texture> a = assets.Load<Texture>("ship.png");
    AssetHandle<Texture> b = assets.Load<Texture>("ship.png");
    AssetHandle<Texture> c = assets.Load<Texture>("sub/../ship.png");   // the same file
    AssetHandle<Texture> d = assets.Load<Texture>(f.Root / "ship.png"); // absolute
    CHECK(a == b && a == c && a == d);
    CHECK(f.Loader->Loads == 1 && assets.GetCount() == 1);
    CHECK(RefCount(assets, a.GetId()) == 4);
    CHECK(a->GetWidth() == 32 && &a.Get() == &d.Get());

    // Other options or another type make another asset.
    AssetHandle<Texture> linear =
        assets.Load<Texture>("ship.png", {.Filter = TextureFilter::Linear});
    AssetHandle<Sound> sound = assets.Load<Sound>("ship.png");
    CHECK(linear != a && assets.GetCount() == 3 && f.Loader->Loads == 3);
    CHECK(sound->GetFrameCount() == 32);
}

TEST(AssetsReferenceCounting)
{
    Fixture f("lifetime");
    WriteText(f.Root / "a.png", "8");
    Assets& assets = *f.Manager;

    AssetHandle<Texture> a = assets.Load<Texture>("a.png");
    const AssetId id = a.GetId();
    {
        AssetHandle<Texture> copy = a; // copies count
        CHECK(RefCount(assets, id) == 2);
        AssetHandle<Texture> moved = std::move(copy); // moves don't
        CHECK(RefCount(assets, id) == 2 && !copy);
        AssetHandle<Texture> assigned;
        assigned = moved;
        CHECK(RefCount(assets, id) == 3);
        const AssetHandle<Texture>& self = assigned;
        assigned = self; // self-assignment changes nothing
        CHECK(RefCount(assets, id) == 3);
    }
    CHECK(RefCount(assets, id) == 1);

    // The last handle gone: still there until the next Update (the frame may still draw it).
    a.Reset();
    CHECK(!a && assets.GetCount() == 1 && RefCount(assets, id) == 0);
    // Loaded again before that: the same asset comes back, without loading the file again.
    AssetHandle<Texture> again = assets.Load<Texture>("a.png");
    CHECK(again.GetId() == id && f.Loader->Loads == 1);
    again.Reset();
    assets.Update(0.0f);
    CHECK(assets.GetCount() == 0 && !assets.GetInfo(id));
    // After unloading, a new load reads the file again.
    AssetHandle<Texture> fresh = assets.Load<Texture>("a.png");
    CHECK(fresh.GetId() != id && f.Loader->Loads == 2 && fresh->GetWidth() == 8);
    // Assets still in use survive Update.
    assets.Update(0.0f);
    CHECK(assets.GetCount() == 1);
    const std::vector<AssetInfo> list = assets.List();
    CHECK(list.size() == 1 && list[0].RefCount == 1 && list[0].Type == AssetType::Texture &&
          !list[0].Placeholder && list[0].Path == (f.Root / "a.png").lexically_normal().string());
}

TEST(AssetsMissingFilesGivePlaceholders)
{
    Fixture f("missing");
    Assets& assets = *f.Manager;

    AssetHandle<Texture> texture = assets.Load<Texture>("nope.png");
    CHECK(texture && texture->GetWidth() == 64 && texture->GetHeight() == 64);
    CHECK(assets.GetInfo(texture.GetId())->Placeholder);
    // Asking again does not retry (it is the same asset); a hot reload will once it exists.
    AssetHandle<Texture> again = assets.Load<Texture>("nope.png");
    CHECK(again == texture && f.Loader->Loads == 1);

    // An atlas placeholder shows its texture for every sprite and animation.
    AssetHandle<TextureAtlas> atlas = assets.Load<TextureAtlas>("nope.json");
    CHECK(atlas->IsPlaceholder() && atlas->Get("ship").Source == &atlas->GetTexture());
    CHECK(atlas->GetAnimation("walk").Frames.size() == 1);

    // A font placeholder draws boxes and still measures text.
    AssetHandle<Font> font = assets.Load<Font>("nope.ttf", {.Size = 20.0f});
    CHECK(font->FindGlyph('A') != nullptr && font->MeasureText("AB").x > 20.0f);

    // A broken file is treated like a missing one. A silent sound plays fine.
    WriteText(f.Root / "broken.wav", "not a sound");
    AssetHandle<Sound> sound = assets.Load<Sound>("broken.wav");
    CHECK(sound->GetFrameCount() > 0 && sound->GetSamples()[0] == 0.0f);
    CHECK(assets.GetInfo(sound.GetId())->Placeholder);
}

TEST(AssetsHotReloadInPlace)
{
    if (!Assets::kHotReload) // compiled out of release builds
        return;
    Fixture f("reload");
    WriteText(f.Root / "ship.png", "32");
    Assets& assets = *f.Manager;
    AssetHandle<Texture> ship = assets.Load<Texture>("ship.png");
    const Texture* address = &ship.Get();

    CHECK(assets.CheckForChanges() == 0); // nothing changed
    WriteText(f.Root / "ship.png", "48");
    // The first check sees the change, the next one (file unchanged since) reloads.
    CHECK(assets.CheckForChanges() == 0 && ship->GetWidth() == 32);
    CHECK(assets.CheckForChanges() == 1);
    CHECK(ship->GetWidth() == 48 && &ship.Get() == address); // same object, new contents
    CHECK(assets.GetInfo(ship.GetId())->Reloads == 1);
    CHECK(assets.CheckForChanges() == 0); // and only once

    // A broken new version is not loaded: the old one stays.
    WriteText(f.Root / "ship.png", "garbage");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 0 && ship->GetWidth() == 48);
    CHECK(assets.GetInfo(ship.GetId())->Reloads == 1);

    // A placeholder is replaced once its file appears.
    AssetHandle<Sound> sound = assets.Load<Sound>("late.wav");
    CHECK(assets.GetInfo(sound.GetId())->Placeholder);
    WriteText(f.Root / "late.wav", "100");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 1);
    CHECK(!assets.GetInfo(sound.GetId())->Placeholder && sound->GetFrameCount() == 100);
}

// Dialogue decks through the manager: loaded once, hot reloaded in place, and a Dialogue that is
// talking picks up the new text (or restarts if its card was removed).
TEST(AssetsDialogueDecks)
{
    Fixture f("dialogue");
    WriteText(f.Root / "npc.json", R"({ "id": "npc", "cards": [
        { "id": "hi", "text": "Hello.", "next": "bye" }, { "id": "bye", "text": "Bye." } ] })");
    Assets& assets = *f.Manager;
    AssetHandle<DialogueDeck> deck = assets.Load<DialogueDeck>("npc.json");
    CHECK(!assets.GetInfo(deck.GetId())->Placeholder && deck->Cards.size() == 2);
    CHECK(assets.Load<DialogueDeck>("sub/../npc.json") == deck);

    // A missing file gives an empty deck: starting it fails (logged) instead of crashing.
    AssetHandle<DialogueDeck> missing = assets.Load<DialogueDeck>("missing.json");
    DialogueFlags flags;
    Dialogue none(*missing, flags);
    CHECK(assets.GetInfo(missing.GetId())->Placeholder && !none.Start());

    if (!Assets::kHotReload)
        return;
    Dialogue talk(*deck, flags);
    CHECK(talk.Start() && talk.Advance() && talk.GetText() == "Bye.");
    WriteText(f.Root / "npc.json", R"({ "id": "npc", "cards": [
        { "id": "hi", "text": "Hello.", "next": "bye" }, { "id": "bye", "text": "See you." } ] })");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 1);
    talk.Refresh();
    CHECK(talk.GetCardId() == "bye" && talk.GetText() == "See you.");

    // A broken edit keeps the last good deck.
    WriteText(f.Root / "npc.json", R"({ "id": "npc", "cards": [ { "id": "hi", "if": "?" } ] })");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 0 && deck->Cards.size() == 2);

    // The current card removed: the talk starts over.
    WriteText(f.Root / "npc.json",
              R"({ "id": "npc", "cards": [ { "id": "hi", "text": "Hey." } ] })");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 1);
    talk.Refresh();
    CHECK(talk.GetCardId() == "hi" && talk.GetText() == "Hey.");
}

TEST(AssetsHotReloadAtlasKeepsPointers)
{
    if (!Assets::kHotReload)
        return;
    Fixture f("atlas");
    WriteText(f.Root / "hero.png", "64");
    WriteText(f.Root / "hero.json", R"({ "a": { "x": 0, "y": 0, "w": 16, "h": 16 },
        "b": { "x": 16, "y": 0, "w": 16, "h": 16 },
        "c": { "x": 32, "y": 0, "w": 16, "h": 16 },
        "animations": { "walk": ["a", "b", "c"] } })");
    Assets& assets = *f.Manager;
    AssetHandle<TextureAtlas> atlas = assets.Load<TextureAtlas>("hero.json");
    CHECK(atlas->Contains("c") && !atlas->IsPlaceholder());

    // What a game keeps between frames: a sprite and an animator on the third frame.
    const Sprite sprite = atlas->Get("a");
    Animator animator;
    animator.Play(atlas->GetAnimation("walk"));
    animator.Update(0.25f);
    CHECK(animator.GetFrameIndex() == 2);

    // Now the walk has only two frames, and the image (the atlas's second file) changes too.
    WriteText(f.Root / "hero.json", R"({ "a": { "x": 0, "y": 0, "w": 8, "h": 8 },
        "b": { "x": 8, "y": 0, "w": 8, "h": 8 },
        "animations": { "walk": ["a", "b"] } })");
    WriteText(f.Root / "hero.png", "128");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 1); // one atlas, though both files changed
    CHECK(atlas->GetTexture().GetWidth() == 128 && !atlas->Contains("c"));
    CHECK(sprite.Source == &atlas->GetTexture());                   // still our texture
    CHECK(animator.GetAnimation() == &atlas->GetAnimation("walk")); // still our animation
    CHECK(atlas->GetAnimation("walk").Frames.size() == 2);
    CHECK(atlas->GetAnimation("walk").Frames[1].Image.Source == &atlas->GetTexture());
    // The animator was past the new last frame: it carries on safely.
    CHECK(animator.GetSprite().Source == &atlas->GetTexture());
    animator.Update(0.05f);
    CHECK(animator.GetFrameIndex() < 2);
}

TEST(AssetsHotReloadFonts)
{
    if (!Assets::kHotReload)
        return;
    Fixture f("font");
    Assets& assets = *f.Manager;
    // Missing at first (boxes), then the real font file is copied in.
    AssetHandle<Font> font = assets.Load<Font>("ui.ttf", {.Size = 16.0f});
    const Font* address = &font.Get();
    CHECK(font->IsPixelFont() && assets.GetInfo(font.GetId())->Placeholder);
    fs::copy_file(EMERALD_TEST_FONT, f.Root / "ui.ttf");
    assets.CheckForChanges();
    CHECK(assets.CheckForChanges() == 1);
    CHECK(!assets.GetInfo(font.GetId())->Placeholder && &font.Get() == address);
    CHECK(font->GetKerning('A', 'V') <= 0.0f && font->FindGlyph('~') != nullptr);
}

// The way Application uses it: Update every frame, file checks on the thread pool.
TEST(AssetsHotReloadThroughUpdate)
{
    if (!Assets::kHotReload)
        return;
    ThreadPool pool(2);
    Fixture f("update", &pool);
    WriteText(f.Root / "ship.png", "32");
    Assets& assets = *f.Manager;
    AssetHandle<Texture> ship = assets.Load<Texture>("ship.png");
    WriteText(f.Root / "ship.png", "40");

    // About 60 frames a second until it shows up; it should take well under a second.
    const auto start = std::chrono::steady_clock::now();
    f32 seconds = 0.0f;
    while (ship->GetWidth() != 40 && seconds < 3.0f) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        assets.Update(0.016f);
        seconds = std::chrono::duration<f32>(std::chrono::steady_clock::now() - start).count();
    }
    CHECK(ship->GetWidth() == 40);
    CHECK(seconds < 1.0f);
}

// The real loader without a GPU device: sounds and fonts load for real, textures fall back to
// the GPU-less checkerboard.
TEST(GpuAssetLoaderWithoutDevice)
{
    Log::Init({});
    const fs::path root = fs::temp_directory_path() / "emerald_asset_tests" / "gpu_loader";
    fs::remove_all(root);
    fs::create_directories(root);
    WriteWav(root / "beep.wav", 4800, 8000);
    {
        Assets assets(std::make_unique<GpuAssetLoader>(nullptr), root);
        AssetHandle<Sound> beep = assets.Load<Sound>("beep.wav");
        CHECK(!assets.GetInfo(beep.GetId())->Placeholder);
        CHECK(NearlyEqual(beep->GetDurationSeconds(), 0.1f, 1e-3f) && beep->GetSamples()[0] > 0.2f);

        AssetHandle<Font> font = assets.Load<Font>(EMERALD_TEST_FONT, {.Size = 8.0f});
        CHECK(!assets.GetInfo(font.GetId())->Placeholder && font->GetSize() == 8.0f);

        AssetHandle<Texture> missing = assets.Load<Texture>("missing.png");
        CHECK(missing->GetWidth() == 64 && !missing->HasGpuTexture());
        const Image checker = MakeCheckerImage();
        CHECK(checker.Pixels[0] == 255 && checker.Pixels[1] == 0 && checker.Pixels[2] == 255);
        CHECK(checker.Pixels[8 * 4] == 0); // the next square is black

        if (Assets::kHotReload) { // a real WAV, rewritten twice as long and quieter
            WriteWav(root / "beep.wav", 9600, 2000);
            assets.CheckForChanges();
            CHECK(assets.CheckForChanges() == 1);
            CHECK(NearlyEqual(beep->GetDurationSeconds(), 0.2f, 1e-3f) &&
                  beep->GetSamples()[0] < 0.1f);
        }
    }
    fs::remove_all(root);
}
