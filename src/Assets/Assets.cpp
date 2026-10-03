#include "Emerald/Assets/Assets.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

#include "Emerald/Core/Log.h"
#include "Emerald/Core/ThreadPool.h"

namespace Emerald {

namespace {

// What makes two loads "the same asset": the options that change the result go into the key,
// so the same font file at two sizes is two assets.
std::string OptionsKey(const TextureOptions& o)
{
    return "|filter=" + std::to_string(static_cast<i32>(o.Filter)) +
           "|wrap=" + std::to_string(static_cast<i32>(o.Wrap));
}

std::string OptionsKey(const FontOptions& o)
{
    std::string key = "|size=" + std::to_string(o.Size) +
                      "|oversample=" + std::to_string(o.Oversample) +
                      "|filter=" + std::to_string(static_cast<i32>(o.Filter)) + "|ranges=";
    for (const GlyphRange& r : o.Ranges)
        key += std::to_string(r.First) + "+" + std::to_string(r.Count) + ",";
    return key;
}

std::string OptionsKey(const AssetTraits<Sound>::Options&)
{
    return {};
}

// When the file was last written, or nullopt if it does not exist.
std::optional<std::filesystem::file_time_type> GetFileTime(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
    if (error)
        return std::nullopt;
    return time;
}

} // namespace

const char* GetAssetTypeName(AssetType type)
{
    switch (type) {
    case AssetType::Texture:
        return "texture";
    case AssetType::Atlas:
        return "atlas";
    case AssetType::Font:
        return "font";
    case AssetType::Sound:
        return "sound";
    case AssetType::Tilemap:
        return "tilemap";
    }
    return "?";
}

Assets::Assets(std::unique_ptr<AssetLoader> loader, std::filesystem::path root, ThreadPool* pool)
    : m_Loader(std::move(loader)), m_Root(std::move(root)), m_Pool(pool)
{
}

Assets::~Assets()
{
    if (m_Check.valid())
        m_Check.wait(); // the check only reads file times, but let it finish
    u32 alive = 0;
    for (const auto& [id, entry] : m_Entries)
        alive += entry.RefCount > 0 ? 1 : 0;
    if (alive > 0)
        EM_CORE_WARN("Assets: {} assets still have handles at shutdown; release handles before "
                     "the manager is destroyed",
                     alive);
}

std::filesystem::path Assets::Resolve(const std::filesystem::path& path) const
{
    // `/` keeps an absolute path as it is. lexically_normal removes "." and "a/..".
    return (m_Root / path).lexically_normal();
}

AssetId Assets::Acquire(AssetType type, const std::filesystem::path& path, const Options& options)
{
    const std::filesystem::path file = Resolve(path);
    std::string key = std::string(GetAssetTypeName(type)) + ":" + file.generic_string() +
                      std::visit([](const auto& o) { return OptionsKey(o); }, options);
#if defined(_WIN32)
    // Windows paths are not case-sensitive: "Ship.png" is the same file as "ship.png".
    std::transform(key.begin(), key.end(), key.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
#endif

    // Already loaded (or unused but not unloaded yet): one more reference.
    if (const auto it = m_ByKey.find(key); it != m_ByKey.end()) {
        ++m_Entries.at(it->second).RefCount;
        return it->second;
    }

    Entry entry;
    entry.Type = type;
    entry.Key = key;
    entry.Files = {file};
    if (type == AssetType::Atlas) // the image next to the JSON
        entry.Files.push_back(std::filesystem::path(file).replace_extension(".png"));
    entry.LoadOptions = options;
    entry.RefCount = 1;
    for (const std::filesystem::path& f : entry.Files)
        entry.LoadedTimes.push_back(kHotReload ? GetFileTime(f) : std::nullopt);
    entry.SeenTimes = entry.LoadedTimes;

    if (std::unique_ptr<Object> object = LoadObject(entry)) {
        entry.Value = std::move(object);
        TrackFiles(entry);
        EM_CORE_INFO("Assets: loaded {} {}", GetAssetTypeName(type), file.string());
    } else {
        EM_CORE_ERROR("Assets: could not load {} {}; using a placeholder", GetAssetTypeName(type),
                      file.string());
        entry.Value = std::make_unique<Object>(MakePlaceholder(entry));
        entry.Placeholder = true;
    }

    const AssetId id = m_NextId++;
    m_ByKey.emplace(std::move(key), id);
    m_Entries.emplace(id, std::move(entry));
    return id;
}

std::unique_ptr<Assets::Object> Assets::LoadObject(const Entry& entry) const
{
    const std::filesystem::path& file = entry.Files[0];
    switch (entry.Type) {
    case AssetType::Texture:
        if (auto texture = m_Loader->LoadTexture(file, std::get<TextureOptions>(entry.LoadOptions)))
            return std::make_unique<Object>(std::in_place_type<Texture>, std::move(*texture));
        break;
    case AssetType::Atlas:
        if (auto atlas = m_Loader->LoadAtlas(entry.Files[1], file,
                                             std::get<TextureOptions>(entry.LoadOptions)))
            return std::make_unique<Object>(std::in_place_type<TextureAtlas>, std::move(*atlas));
        break;
    case AssetType::Font:
        if (auto font = m_Loader->LoadFont(file, std::get<FontOptions>(entry.LoadOptions)))
            return std::make_unique<Object>(std::in_place_type<Font>, std::move(*font));
        break;
    case AssetType::Sound:
        if (auto sound = m_Loader->LoadSound(file))
            return std::make_unique<Object>(std::in_place_type<Sound>, std::move(*sound));
        break;
    case AssetType::Tilemap:
        if (auto map = m_Loader->LoadTilemap(file, std::get<TextureOptions>(entry.LoadOptions)))
            return std::make_unique<Object>(std::in_place_type<Tilemap>, std::move(*map));
        break;
    }
    return nullptr;
}

Assets::Object Assets::MakePlaceholder(const Entry& entry) const
{
    switch (entry.Type) {
    case AssetType::Texture:
        return m_Loader->MakePlaceholderTexture();
    case AssetType::Atlas:
        return m_Loader->MakePlaceholderAtlas();
    case AssetType::Font:
        return m_Loader->MakePlaceholderFont(std::get<FontOptions>(entry.LoadOptions));
    case AssetType::Tilemap:
        return m_Loader->MakePlaceholderTilemap();
    case AssetType::Sound:
        break;
    }
    return m_Loader->MakePlaceholderSound();
}

bool Assets::Reload(Entry& entry)
{
    std::unique_ptr<Object> fresh = LoadObject(entry);
    if (!fresh) {
        EM_CORE_WARN("Assets: reloading {} failed; keeping the current version",
                     entry.Files[0].string());
        return false;
    }
    // Replace the object's contents, not the object, so everything pointing at it stays valid.
    Object& current = *entry.Value;
    if (entry.Type == AssetType::Atlas)
        std::get<TextureAtlas>(current).ReplaceWith(std::move(std::get<TextureAtlas>(*fresh)));
    else
        current = std::move(*fresh); // same alternative: move-assigns the object in place
    entry.Placeholder = false;
    TrackFiles(entry);
    ++entry.Reloads;
    EM_CORE_INFO("Assets: reloaded {} {} (reload #{})", GetAssetTypeName(entry.Type),
                 entry.Files[0].string(), entry.Reloads);
    return true;
}

void Assets::TrackFiles(Entry& entry)
{
    if (entry.Type != AssetType::Tilemap)
        return;
    const std::vector<std::filesystem::path>& files = std::get<Tilemap>(*entry.Value).GetFiles();
    if (files == entry.Files)
        return;
    entry.Files = files;
    entry.LoadedTimes.clear();
    for (const std::filesystem::path& f : entry.Files)
        entry.LoadedTimes.push_back(kHotReload ? GetFileTime(f) : std::nullopt);
    entry.SeenTimes = entry.LoadedTimes;
}

void Assets::AddRef(AssetId id)
{
    ++m_Entries.at(id).RefCount;
}

void Assets::Release(AssetId id)
{
    Entry& entry = m_Entries.at(id);
    assert(entry.RefCount > 0);
    --entry.RefCount; // unloaded later, by UnloadUnused
}

void Assets::UnloadUnused()
{
    for (auto it = m_Entries.begin(); it != m_Entries.end();) {
        if (it->second.RefCount > 0) {
            ++it;
            continue;
        }
        EM_CORE_INFO("Assets: unloaded {} {}", GetAssetTypeName(it->second.Type),
                     it->second.Files[0].string());
        m_ByKey.erase(it->second.Key);
        it = m_Entries.erase(it);
    }
}

void Assets::Update([[maybe_unused]] f32 dt)
{
#if EMERALD_HOT_RELOAD
    // A check that was started earlier has finished: act on what it found.
    if (m_Check.valid() && m_Check.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        ApplyStamps(m_Check.get());
    // Time for the next one (one at a time).
    m_PollTimer += dt;
    if (!m_Check.valid() && m_PollTimer >= kPollInterval) {
        m_PollTimer = 0.0f;
        std::vector<FileStamp> files = FilesToCheck();
        if (m_Pool) {
            // The task gets its own copy of the paths, so it touches nothing of ours.
            m_Check = m_Pool->Submit([files = std::move(files)]() mutable {
                StampFiles(files);
                return std::move(files);
            });
        } else {
            StampFiles(files);
            ApplyStamps(files);
        }
    }
#endif
    UnloadUnused();
}

u32 Assets::CheckForChanges()
{
#if EMERALD_HOT_RELOAD
    std::vector<FileStamp> files = FilesToCheck();
    StampFiles(files);
    return ApplyStamps(files);
#else
    return 0;
#endif
}

std::vector<Assets::FileStamp> Assets::FilesToCheck() const
{
    std::vector<FileStamp> files;
    for (const auto& [id, entry] : m_Entries)
        for (usize i = 0; i < entry.Files.size(); ++i)
            files.push_back({.Id = id, .File = i, .Path = entry.Files[i], .Time = std::nullopt});
    return files;
}

void Assets::StampFiles(std::vector<FileStamp>& files)
{
    for (FileStamp& f : files)
        f.Time = GetFileTime(f.Path);
}

u32 Assets::ApplyStamps(const std::vector<FileStamp>& stamps)
{
    // Which assets have a file that changed since it was loaded and then stayed the same since
    // the previous check.
    std::vector<AssetId> ready;
    for (const FileStamp& stamp : stamps) {
        const auto it = m_Entries.find(stamp.Id);
        if (it == m_Entries.end())
            continue; // unloaded while the check ran
        Entry& entry = it->second;
        if (stamp.File >= entry.Files.size() || entry.Files[stamp.File] != stamp.Path)
            continue; // the asset's file list changed while the check ran (a tilemap)
        const bool settled = stamp.Time == entry.SeenTimes[stamp.File];
        entry.SeenTimes[stamp.File] = stamp.Time;
        if (settled && stamp.Time != entry.LoadedTimes[stamp.File] && stamp.Time)
            ready.push_back(stamp.Id);
    }
    // An atlas or tilemap can be listed more than once (several of its files changed).
    std::sort(ready.begin(), ready.end());
    ready.erase(std::unique(ready.begin(), ready.end()), ready.end());

    u32 reloaded = 0;
    for (const AssetId id : ready) {
        Entry& entry = m_Entries.at(id);
        // Whether or not it works, this version has been tried: wait for the next change.
        entry.LoadedTimes = entry.SeenTimes;
        reloaded += Reload(entry) ? 1 : 0;
    }
    return reloaded;
}

std::vector<AssetInfo> Assets::List() const
{
    std::vector<AssetInfo> list;
    for (const auto& [id, entry] : m_Entries)
        list.push_back(*GetInfo(id));
    std::sort(list.begin(), list.end(),
              [](const AssetInfo& a, const AssetInfo& b) { return a.Id < b.Id; });
    return list;
}

std::optional<AssetInfo> Assets::GetInfo(AssetId id) const
{
    const auto it = m_Entries.find(id);
    if (it == m_Entries.end())
        return std::nullopt;
    const Entry& e = it->second;
    return AssetInfo{.Id = id,
                     .Type = e.Type,
                     .Path = e.Files[0].string(),
                     .RefCount = e.RefCount,
                     .Reloads = e.Reloads,
                     .Placeholder = e.Placeholder};
}

} // namespace Emerald
