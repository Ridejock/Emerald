#pragma once

// Tweak variables: values registered from code that show up in the Tweaks panel (ImGui builds),
// where they can be changed while the game runs and saved to a JSON file. For tuning game feel
// (jump height, coyote time, colors) without recompiling:
//
//   // Next to the code that uses them, at namespace scope (like constants), or as members of
//   // the scene or object that uses them (the panel shows them while it exists):
//   Emerald::Tweak<f32> g_JumpVelocity{"Platformer", "Jump velocity", 310.0f, {50.0f, 900.0f}};
//   Emerald::Tweak<bool> g_ShowTrail{"Platformer", "Show trail", true};
//   Emerald::Tweak<Emerald::Vec4> g_Sky{"Platformer", "Sky", {0.36f, 0.62f, 0.86f, 1.0f}};
//
//   tunables.JumpVelocity = g_JumpVelocity; // reads like the value
//
//   // At startup: load the saved values (the panel saves and reloads them).
//   Emerald::GetTweaks().SetFile("assets/tweaks.json");
//   // In OnImGui:
//   Emerald::GetTweaks().ShowPanel();
//
// Types: f32, i32 (sliders when a range is given, drag fields otherwise), bool and Vec4 (a color).
// The file groups the values by category:
//
//   { "Platformer": { "Jump velocity": 330, "Show trail": true, "Sky": [0.4, 0.6, 0.9, 1] } }
//
// Without ImGui (EMERALD_WITH_IMGUI = 0) a Tweak is just its default value: no registry, no file,
// nothing to pay. Values tuned in the panel only exist in ImGui builds, so copy them back into the
// code when they are right (the panel marks values that differ from the code's default).

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

// Slider limits; both 0 means "no limits" (a drag field).
struct TweakRange {
    f32 Min = 0.0f;
    f32 Max = 0.0f;
};

template <typename T>
inline constexpr bool kIsTweakType = std::is_same_v<T, f32> || std::is_same_v<T, i32> ||
                                     std::is_same_v<T, bool> || std::is_same_v<T, Vec4>;

// How a Tweak keeps its value: a color as four plain floats, because Vec4's 16-byte alignment
// would pad the structs that tweaks live in (MSVC warning C4324).
struct TweakColor {
    f32 Rgba[4]{};
};
template <typename T>
using TweakStorage = std::conditional_t<std::is_same_v<T, Vec4>, TweakColor, T>;

template <typename T> constexpr TweakStorage<T> ToTweakStorage(const T& value)
{
    if constexpr (std::is_same_v<T, Vec4>)
        return {{value.x, value.y, value.z, value.w}};
    else
        return value;
}
template <typename T> constexpr T FromTweakStorage(const TweakStorage<T>& stored)
{
    if constexpr (std::is_same_v<T, Vec4>)
        return {stored.Rgba[0], stored.Rgba[1], stored.Rgba[2], stored.Rgba[3]};
    else
        return stored;
}

#if EMERALD_WITH_IMGUI

enum class TweakType : u8 { Float, Int, Bool, Color };

// A value as the registry keeps it: plain fields (no Vec4, whose alignment would pad this).
struct TweakValue {
    f32 F[4]{}; // Float: F[0]; Color: all four
    i32 I = 0;
    bool B = false;
    bool operator==(const TweakValue&) const = default;
};

// The registry behind the Tweak variables (one per program: GetTweaks()). Its JSON and file
// functions don't need ImGui to be running, so they are unit-tested.
class TweakRegistry {
public:
    struct Entry {
        std::string Category;
        std::string Name;
        TweakType Type = TweakType::Float;
        void* Value = nullptr; // the Tweak's value (f32, i32, bool or TweakColor)
        TweakValue Default;
        TweakRange Range;
    };

    // Called by Tweak's constructor / destructor. A value loaded earlier for the same category
    // and name is applied at once; a removed tweak's value is remembered for its next instance.
    void Register(TweakType type, void* value, std::string_view category, std::string_view name,
                  TweakRange range);
    void Unregister(const void* value);

    // The file: SetFile loads it (if it exists), Save() writes it, and ShowPanel reloads it when
    // it changes on disk. Values for tweaks that aren't registered (yet) are kept and written back.
    void SetFile(std::filesystem::path file);
    [[nodiscard]] const std::filesystem::path& GetFile() const { return m_File; }
    bool Save();                                  // to GetFile(); false (logged) if it failed
    bool Save(const std::filesystem::path& file); // logs
    bool Load(const std::filesystem::path& file); // logs; false if missing or broken

    [[nodiscard]] std::string ToJson() const;
    bool FromJson(std::string_view json, std::string_view source = "tweaks"); // false if broken

    void ResetAll(); // every registered tweak back to its default
    void Clear();    // forget remembered values (registered tweaks keep theirs)

    // The Tweaks window: one section per category, Save / Load / Reset all. Hidden while no
    // tweak is registered.
    void ShowPanel(bool* open = nullptr);

    [[nodiscard]] const std::vector<Entry>& GetEntries() const { return m_Entries; }
    [[nodiscard]] static TweakValue Read(const Entry& entry);
    static void Write(const Entry& entry, const TweakValue& value);

private:
    struct Stored {
        TweakValue Value;
        bool IsNumber = false; // what the file had: a number, a bool or a list
        bool IsBool = false;
        bool IsList = false;
    };
    [[nodiscard]] static std::string Key(std::string_view category, std::string_view name);
    // Copies a remembered value into the tweak if the kinds fit (a list for a color, ...).
    static bool Apply(const Entry& entry, const Stored& stored);
    void CheckFile(f32 dt); // the panel's hot reload

    std::vector<Entry> m_Entries;
    std::map<std::string, Stored, std::less<>> m_Stored; // "category\nname" -> value
    std::filesystem::path m_File;
    std::filesystem::file_time_type m_FileTime{};
    f32 m_CheckTimer = 0.0f;
    std::string m_Status;
    std::string m_Filter;
};

[[nodiscard]] TweakRegistry& GetTweaks();

template <typename T> class Tweak {
    static_assert(kIsTweakType<T>, "Tweak<T>: T is f32, i32, bool or Vec4 (a color)");

public:
    Tweak(std::string_view category, std::string_view name, T value, TweakRange range = {})
        : m_Value(ToTweakStorage(value))
    {
        GetTweaks().Register(TypeOf(), &m_Value, category, name, range);
    }
    ~Tweak() { GetTweaks().Unregister(&m_Value); }
    // Registered by address: not copyable or movable.
    Tweak(const Tweak&) = delete;
    Tweak& operator=(const Tweak&) = delete;

    [[nodiscard]] T Get() const { return FromTweakStorage<T>(m_Value); }
    operator T() const { return Get(); }
    void Set(const T& value) { m_Value = ToTweakStorage(value); }

private:
    static constexpr TweakType TypeOf()
    {
        if constexpr (std::is_same_v<T, f32>)
            return TweakType::Float;
        else if constexpr (std::is_same_v<T, i32>)
            return TweakType::Int;
        else if constexpr (std::is_same_v<T, bool>)
            return TweakType::Bool;
        else
            return TweakType::Color;
    }

    TweakStorage<T> m_Value;
};

#else // without ImGui: the default value and nothing else

class TweakRegistry {
public:
    void SetFile(const std::filesystem::path&) {}
    bool Save() { return false; }
    bool Save(const std::filesystem::path&) { return false; }
    bool Load(const std::filesystem::path&) { return false; }
    void ResetAll() {}
    void ShowPanel(bool* = nullptr) {}
};

[[nodiscard]] inline TweakRegistry& GetTweaks()
{
    static TweakRegistry registry; // empty: nothing to construct or destroy
    return registry;
}

template <typename T> class Tweak {
    static_assert(kIsTweakType<T>, "Tweak<T>: T is f32, i32, bool or Vec4 (a color)");

public:
    constexpr Tweak(std::string_view, std::string_view, T value, TweakRange = {})
        : m_Value(ToTweakStorage(value))
    {
    }
    Tweak(const Tweak&) = delete;
    Tweak& operator=(const Tweak&) = delete;

    [[nodiscard]] constexpr T Get() const { return FromTweakStorage<T>(m_Value); }
    constexpr operator T() const { return Get(); }
    constexpr void Set(const T& value) { m_Value = ToTweakStorage(value); }

private:
    TweakStorage<T> m_Value;
};

#endif

} // namespace Emerald
