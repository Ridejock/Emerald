#pragma once

#include <cassert>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Emerald/Assets/AssetLoader.h"
#include "Emerald/Core/Defines.h"

// Hot reload is for development: debug builds only (CMake defines this to 1 for the Debug
// configuration), and release builds leave the code out.
#ifndef EMERALD_HOT_RELOAD
#define EMERALD_HOT_RELOAD 0
#endif

namespace Emerald {

class Assets;
class ThreadPool;

enum class AssetType : u8 { Texture, Atlas, Font, Sound, Tilemap, Dialogue, Particles };
[[nodiscard]] const char* GetAssetTypeName(AssetType type);

// What each asset type is loaded with: Load<T>(path, options).
template <typename T> struct AssetTraits;
template <> struct AssetTraits<Texture> {
    static constexpr AssetType Type = AssetType::Texture;
    using Options = TextureOptions;
};
template <> struct AssetTraits<TextureAtlas> { // the image: the JSON's name with ".png"
    static constexpr AssetType Type = AssetType::Atlas;
    using Options = TextureOptions;
};
template <> struct AssetTraits<Font> {
    static constexpr AssetType Type = AssetType::Font;
    using Options = FontOptions;
};
template <> struct AssetTraits<Sound> {
    static constexpr AssetType Type = AssetType::Sound;
    struct Options {};
};
template <> struct AssetTraits<Tilemap> { // a Tiled .tmj; the options are for its images
    static constexpr AssetType Type = AssetType::Tilemap;
    using Options = TextureOptions;
};
template <> struct AssetTraits<DialogueDeck> { // a dialogue .json (Dialogue.h)
    static constexpr AssetType Type = AssetType::Dialogue;
    struct Options {};
};
template <> struct AssetTraits<ParticleEffect> { // a particle effect .json (ParticleEffect.h)
    static constexpr AssetType Type = AssetType::Particles;
    struct Options {};
};

using AssetId = u32;

// A counted reference to a loaded asset, like a std::shared_ptr: copies share the asset, and when
// the last handle is gone the asset is unloaded (at the next Assets::Update). Handles for the same
// file compare equal. The asset is read through the handle every time it is used
// (handle->GetSize(), *handle), so a hot reload is picked up without the game doing anything.
//
// Handles must be released before the Assets manager that made them is destroyed (members of
// your Application are; globals are not). An empty handle (default-constructed) has no asset.
template <typename T> class AssetHandle {
public:
    AssetHandle() = default;
    AssetHandle(const AssetHandle& other);
    AssetHandle& operator=(const AssetHandle& other);
    AssetHandle(AssetHandle&& other) noexcept;
    AssetHandle& operator=(AssetHandle&& other) noexcept;
    ~AssetHandle() { Reset(); }

    [[nodiscard]] const T& Get() const;
    [[nodiscard]] const T& operator*() const { return Get(); }
    [[nodiscard]] const T* operator->() const { return &Get(); }
    [[nodiscard]] explicit operator bool() const { return m_Assets != nullptr; }
    [[nodiscard]] AssetId GetId() const { return m_Id; }
    bool operator==(const AssetHandle& other) const = default;

    void Reset(); // let go of the asset (the handle becomes empty)

private:
    friend class Assets;
    // Takes over one reference that the manager already counted.
    AssetHandle(Assets* assets, AssetId id) : m_Assets(assets), m_Id(id) {}

    Assets* m_Assets = nullptr;
    AssetId m_Id = 0;
};

// One line of Assets::List(), e.g. for a debug panel.
struct AssetInfo {
    AssetId Id = 0;
    AssetType Type = AssetType::Texture;
    std::string Path; // as resolved (absolute, normalized)
    u32 RefCount = 0; // live handles
    u32 Reloads = 0;  // successful hot reloads
    bool Placeholder = false;
};

// The asset manager: loads textures, atlases, fonts, sounds, tilemaps, dialogue decks and particle
// effects by path, each file only once.
//
//   AssetHandle<Texture> ship = GetAssets().Load<Texture>("assets/ship.png");
//   r.DrawSprite(*ship, position);
//   AssetHandle<Font> font = GetAssets().Load<Font>("assets/ui.ttf", {.Size = 16.0f});
//   GetAudio().Play(*GetAssets().Load<Sound>("assets/boom.wav"));
//   AssetHandle<Tilemap> map = GetAssets().Load<Tilemap>("assets/level1.tmj");
//   AssetHandle<DialogueDeck> npc = GetAssets().Load<DialogueDeck>("assets/dialogue/npc.json");
//   AssetHandle<ParticleEffect> fx = GetAssets().Load<ParticleEffect>("assets/particles/fx.json");
//
// - Relative paths are relative to the root, by default Paths::GetBasePath() (the folder of the
//   executable, where the build copies the assets).
// - Loading the same file again (same type and options, however the path is spelled:
//   "a/../ship.png" = "ship.png") returns the same handle instead of loading it twice.
// - Reference counting: the asset stays loaded while any handle to it exists. Unused assets are
//   unloaded in Update, at the start of the next frame, so a texture dropped mid-frame is still
//   there when the frame is drawn.
// - A missing or broken file logs an error and gives a placeholder (see GpuAssetLoader) instead
//   of failing, so the game keeps running and the problem is visible on screen.
// - Hot reload (debug builds): every kPollInterval seconds the files' modification times are
//   checked on the thread pool. A file that changed, and then stayed unchanged for one more
//   check (so a half-written file is not read), is loaded again on the main thread (GPU uploads
//   happen there) and replaces the old object in place: handles, and pointers like a Sprite's
//   texture or an Animator's animation, stay valid. A placeholder whose file appears is loaded
//   the same way. If the new version fails to load, the old one stays. A tilemap is watched
//   through all its files (the .tmj, external .tsj tilesets and their images); its contents are
//   replaced as a whole, so look up its layers, objects and tilesets through the handle each time
//   rather than keeping pointers into it.
//
// Use it from the main thread only. Application owns one (GetAssets()) and calls Update.
class Assets {
public:
    static constexpr bool kHotReload = EMERALD_HOT_RELOAD != 0;
    static constexpr f32 kPollInterval = 0.25f; // seconds between file checks

    // `pool` runs the file checks; without one they run on the calling thread.
    explicit Assets(std::unique_ptr<AssetLoader> loader, std::filesystem::path root,
                    ThreadPool* pool = nullptr);
    ~Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    template <typename T>
    [[nodiscard]] AssetHandle<T> Load(const std::filesystem::path& path,
                                      const typename AssetTraits<T>::Options& options = {});

    // Once per frame: hot reload (debug builds) and unloading assets nobody uses any more.
    void Update(f32 dt);
    // Unloads every asset without handles now (Update does this too).
    void UnloadUnused();
    // One hot reload check, right here on the calling thread; returns how many assets were
    // reloaded. Always 0 without hot reload. (Update does this on its own; tests call it.)
    u32 CheckForChanges();

    [[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& path) const;
    [[nodiscard]] const std::filesystem::path& GetRoot() const { return m_Root; }
    // Where later relative paths start (assets already loaded keep their files).
    void SetRoot(std::filesystem::path root) { m_Root = std::move(root); }
    [[nodiscard]] usize GetCount() const { return m_Entries.size(); }
    [[nodiscard]] std::vector<AssetInfo> List() const; // sorted by id (load order)
    [[nodiscard]] std::optional<AssetInfo> GetInfo(AssetId id) const;

private:
    template <typename U> friend class AssetHandle;

    using Object =
        std::variant<Texture, TextureAtlas, Font, Sound, Tilemap, DialogueDeck, ParticleEffect>;
    using Options =
        std::variant<TextureOptions, FontOptions, AssetTraits<Sound>::Options,
                     AssetTraits<DialogueDeck>::Options, AssetTraits<ParticleEffect>::Options>;
    using FileTime = std::optional<std::filesystem::file_time_type>; // nullopt: no such file

    struct Entry {
        AssetType Type = AssetType::Texture;
        std::string Key;                          // what deduplicates: type, path and options
        std::vector<std::filesystem::path> Files; // what it is loaded from (an atlas has two)
        Options LoadOptions;
        std::unique_ptr<Object> Value; // on the heap: its address never changes
        u32 RefCount = 0;
        u32 Reloads = 0;
        bool Placeholder = false;
        std::vector<FileTime> LoadedTimes; // per file: when it was loaded
        std::vector<FileTime> SeenTimes;   // per file: at the last check
    };
    // A file to check, and what the check found.
    struct FileStamp {
        AssetId Id = 0;
        usize File = 0;
        std::filesystem::path Path;
        FileTime Time;
    };

    // Finds or loads an asset and counts one more reference to it.
    AssetId Acquire(AssetType type, const std::filesystem::path& path, const Options& options);
    // Builds the object, or a placeholder if loading fails (placeholder = true).
    [[nodiscard]] std::unique_ptr<Object> LoadObject(const Entry& entry) const; // null on failure
    [[nodiscard]] Object MakePlaceholder(const Entry& entry) const;
    bool Reload(Entry& entry);
    // A tilemap's files are only known once it has loaded: watch those from now on.
    static void TrackFiles(Entry& entry);
    void AddRef(AssetId id);
    void Release(AssetId id);
    template <typename T> [[nodiscard]] const T& Get(AssetId id) const
    {
        return std::get<T>(*m_Entries.at(id).Value);
    }

    [[nodiscard]] std::vector<FileStamp> FilesToCheck() const;
    static void StampFiles(std::vector<FileStamp>& files); // reads the modification times
    u32 ApplyStamps(const std::vector<FileStamp>& stamps); // reloads what changed

    std::unique_ptr<AssetLoader> m_Loader;
    std::filesystem::path m_Root;
    ThreadPool* m_Pool = nullptr;
    std::unordered_map<AssetId, Entry> m_Entries;
    std::unordered_map<std::string, AssetId> m_ByKey;
    AssetId m_NextId = 1;
    f32 m_PollTimer = 0.0f;
    std::future<std::vector<FileStamp>> m_Check; // a file check running on the pool
};

// --- Templates ---------------------------------------------------------------------------------

template <typename T>
AssetHandle<T> Assets::Load(const std::filesystem::path& path,
                            const typename AssetTraits<T>::Options& options)
{
    return AssetHandle<T>(this, Acquire(AssetTraits<T>::Type, path, Options(options)));
}

template <typename T> const T& AssetHandle<T>::Get() const
{
    assert(m_Assets && "AssetHandle::Get on an empty handle");
    return m_Assets->template Get<T>(m_Id);
}

template <typename T> void AssetHandle<T>::Reset()
{
    if (m_Assets)
        m_Assets->Release(m_Id);
    m_Assets = nullptr;
    m_Id = 0;
}

template <typename T>
AssetHandle<T>::AssetHandle(const AssetHandle& other) : m_Assets(other.m_Assets), m_Id(other.m_Id)
{
    if (m_Assets)
        m_Assets->AddRef(m_Id);
}

template <typename T> AssetHandle<T>& AssetHandle<T>::operator=(const AssetHandle& other)
{
    if (this != &other) {
        if (other.m_Assets)
            other.m_Assets->AddRef(other.m_Id); // first, in case both share the asset
        Reset();
        m_Assets = other.m_Assets;
        m_Id = other.m_Id;
    }
    return *this;
}

template <typename T>
AssetHandle<T>::AssetHandle(AssetHandle&& other) noexcept
    : m_Assets(other.m_Assets), m_Id(other.m_Id)
{
    other.m_Assets = nullptr;
    other.m_Id = 0;
}

template <typename T> AssetHandle<T>& AssetHandle<T>::operator=(AssetHandle&& other) noexcept
{
    if (this != &other) {
        Reset();
        m_Assets = other.m_Assets;
        m_Id = other.m_Id;
        other.m_Assets = nullptr;
        other.m_Id = 0;
    }
    return *this;
}

} // namespace Emerald
