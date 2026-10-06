// The tweak registry: values, the JSON file and the ImGui panel. Only built with ImGui
// (EMERALD_USE_IMGUI); without it Tweak.h has the do-nothing versions.

#include "Emerald/Editor/Tweak.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <system_error>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "../Core/FloatText.h"
#include "EditorWindow.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

using Json = nlohmann::json;

bool SameValue(TweakType type, const TweakValue& a, const TweakValue& b)
{
    switch (type) {
    case TweakType::Float:
        return a.F[0] == b.F[0];
    case TweakType::Int:
        return a.I == b.I;
    case TweakType::Bool:
        return a.B == b.B;
    case TweakType::Color:
        return std::equal(std::begin(a.F), std::end(a.F), std::begin(b.F));
    }
    return true;
}

std::optional<std::filesystem::file_time_type> FileTime(const std::filesystem::path& file)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    if (error)
        return std::nullopt;
    return time;
}

} // namespace

TweakRegistry& GetTweaks()
{
    static TweakRegistry registry;
    return registry;
}

std::string TweakRegistry::Key(std::string_view category, std::string_view name)
{
    return std::string(category) + '\n' + std::string(name);
}

TweakValue TweakRegistry::Read(const Entry& entry)
{
    TweakValue v;
    switch (entry.Type) {
    case TweakType::Float:
        v.F[0] = *static_cast<const f32*>(entry.Value);
        break;
    case TweakType::Int:
        v.I = *static_cast<const i32*>(entry.Value);
        break;
    case TweakType::Bool:
        v.B = *static_cast<const bool*>(entry.Value);
        break;
    case TweakType::Color:
        std::copy_n(static_cast<const f32*>(entry.Value), 4, v.F); // TweakColor::Rgba
        break;
    }
    return v;
}

void TweakRegistry::Write(const Entry& entry, const TweakValue& v)
{
    switch (entry.Type) {
    case TweakType::Float:
        *static_cast<f32*>(entry.Value) = v.F[0];
        break;
    case TweakType::Int:
        *static_cast<i32*>(entry.Value) = v.I;
        break;
    case TweakType::Bool:
        *static_cast<bool*>(entry.Value) = v.B;
        break;
    case TweakType::Color:
        std::copy_n(v.F, 4, static_cast<f32*>(entry.Value));
        break;
    }
}

bool TweakRegistry::Apply(const Entry& entry, const Stored& stored)
{
    const bool fits = entry.Type == TweakType::Bool    ? stored.IsBool
                      : entry.Type == TweakType::Color ? stored.IsList
                                                       : stored.IsNumber;
    if (!fits) {
        EM_CORE_WARN("Tweaks: '{} / {}' has a value of the wrong kind in the file; ignored",
                     entry.Category, entry.Name);
        return false;
    }
    Write(entry, stored.Value);
    return true;
}

void TweakRegistry::Register(TweakType type, void* value, std::string_view category,
                             std::string_view name, TweakRange range)
{
    Entry entry{.Category = std::string(category),
                .Name = std::string(name),
                .Type = type,
                .Value = value,
                .Default = {},
                .Range = range};
    entry.Default = Read(entry);
    if (const auto it = m_Stored.find(Key(category, name)); it != m_Stored.end())
        Apply(entry, it->second);
    m_Entries.push_back(std::move(entry));
}

void TweakRegistry::Unregister(const void* value)
{
    const auto it = std::find_if(m_Entries.begin(), m_Entries.end(),
                                 [value](const Entry& e) { return e.Value == value; });
    if (it == m_Entries.end())
        return;
    // Remembered, so a scene's tweak made again later comes back with the tuned value.
    Stored& stored = m_Stored[Key(it->Category, it->Name)];
    TweakValue last = Read(*it);
    if (it->Type == TweakType::Int)
        last.F[0] = static_cast<f32>(last.I); // numbers are written from F[0]
    stored = {.Value = last,
              .IsNumber = it->Type == TweakType::Float || it->Type == TweakType::Int,
              .IsBool = it->Type == TweakType::Bool,
              .IsList = it->Type == TweakType::Color};
    m_Entries.erase(it);
}

std::string TweakRegistry::ToJson() const
{
    // Category -> name -> value text; registered tweaks win over remembered values.
    std::map<std::string, std::map<std::string, std::string>> groups;
    const auto text = [](const Stored& s) -> std::string {
        if (s.IsBool)
            return s.Value.B ? "true" : "false";
        if (s.IsList)
            return "[" + FloatToText(s.Value.F[0]) + ", " + FloatToText(s.Value.F[1]) + ", " +
                   FloatToText(s.Value.F[2]) + ", " + FloatToText(s.Value.F[3]) + "]";
        return FloatToText(s.Value.F[0]);
    };
    for (const auto& [key, stored] : m_Stored) {
        const usize split = key.find('\n');
        groups[key.substr(0, split)][key.substr(split + 1)] = text(stored);
    }
    for (const Entry& e : m_Entries) {
        const TweakValue v = Read(e);
        std::string& out = groups[e.Category][e.Name];
        switch (e.Type) {
        case TweakType::Float:
            out = FloatToText(v.F[0]);
            break;
        case TweakType::Int:
            out = std::to_string(v.I);
            break;
        case TweakType::Bool:
            out = v.B ? "true" : "false";
            break;
        case TweakType::Color:
            out = text({.Value = v, .IsList = true});
            break;
        }
    }
    // Hand-written for one value per line; Json(...).dump() only quotes the names.
    std::string json = "{\n";
    usize g = 0;
    for (const auto& [category, values] : groups) {
        json += "  " + Json(category).dump() + ": {\n";
        usize i = 0;
        for (const auto& [name, value] : values)
            json +=
                "    " + Json(name).dump() + ": " + value + (++i < values.size() ? ",\n" : "\n");
        json += ++g < groups.size() ? "  },\n" : "  }\n";
    }
    return json + "}\n";
}

bool TweakRegistry::FromJson(std::string_view json, std::string_view source)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        EM_CORE_ERROR("Tweaks {}: not a JSON object", source);
        return false;
    }
    usize count = 0;
    for (const auto& [category, values] : root.items()) {
        if (!values.is_object()) {
            EM_CORE_WARN("Tweaks {}: '{}' should be an object of values; skipped", source,
                         category);
            continue;
        }
        for (const auto& [name, value] : values.items()) {
            Stored s;
            if (value.is_boolean()) {
                s.IsBool = true;
                s.Value.B = value.get<bool>();
            } else if (value.is_number()) {
                s.IsNumber = true;
                s.Value.F[0] = value.get<f32>();
                s.Value.I = static_cast<i32>(std::lround(value.get<f64>()));
            } else if (value.is_array() && (value.size() == 3 || value.size() == 4) &&
                       std::all_of(value.begin(), value.end(),
                                   [](const Json& c) { return c.is_number(); })) {
                s.IsList = true;
                s.Value.F[3] = 1.0f; // [r, g, b] is opaque
                for (usize i = 0; i < value.size(); ++i)
                    s.Value.F[i] = value[i].get<f32>();
            } else {
                EM_CORE_WARN("Tweaks {}: '{} / {}' is not a number, bool or color; skipped", source,
                             category, name);
                continue;
            }
            const std::string key = Key(category, name);
            for (const Entry& e : m_Entries)
                if (e.Category == category && e.Name == name)
                    Apply(e, s);
            m_Stored[key] = s;
            ++count;
        }
    }
    EM_CORE_INFO("Tweaks: {} values from {}", count, source);
    return true;
}

bool TweakRegistry::Save(const std::filesystem::path& file)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << ToJson();
    out.close();
    if (!out) {
        EM_CORE_ERROR("Tweaks: can't write {}", file.string());
        return false;
    }
    if (file == m_File)
        m_FileTime = FileTime(file).value_or(std::filesystem::file_time_type{});
    EM_CORE_INFO("Tweaks: saved {}", file.string());
    return true;
}

bool TweakRegistry::Save()
{
    if (m_File.empty()) {
        EM_CORE_WARN("Tweaks: no file set (SetFile)");
        return false;
    }
    return Save(m_File);
}

bool TweakRegistry::Load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        EM_CORE_WARN("Tweaks: can't open {}", file.string());
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (file == m_File)
        m_FileTime = FileTime(file).value_or(std::filesystem::file_time_type{});
    return FromJson(text, file.string());
}

void TweakRegistry::SetFile(std::filesystem::path file)
{
    m_File = std::move(file);
    m_FileTime = {};
    if (FileTime(m_File))
        Load(m_File);
}

void TweakRegistry::ResetAll()
{
    for (const Entry& e : m_Entries)
        Write(e, e.Default);
}

void TweakRegistry::Clear()
{
    m_Stored.clear();
}

void TweakRegistry::CheckFile(f32 dt)
{
    m_CheckTimer += dt;
    if (m_File.empty() || m_CheckTimer < 0.5f)
        return;
    m_CheckTimer = 0.0f;
    const auto time = FileTime(m_File);
    if (time && *time != m_FileTime && Load(m_File))
        m_Status = "Reloaded (the file changed)";
}

void TweakRegistry::ShowPanel(bool* open)
{
    CheckFile(ImGui::GetIO().DeltaTime);
    if (m_Entries.empty())
        return; // nothing to tune right now (e.g. a title screen)
    PlaceNextEditorWindow(ImVec2(370.0f, 420.0f), 20.0f);
    if (!ImGui::Begin("Tweaks", open)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Save") && Save())
        m_Status = "Saved";
    ImGui::SameLine();
    if (ImGui::Button("Load") && !m_File.empty() && Load(m_File))
        m_Status = "Loaded";
    ImGui::SameLine();
    if (ImGui::Button("Reset all")) {
        ResetAll();
        m_Status = "Defaults from the code";
    }
    ImGui::TextDisabled("%s", m_File.empty() ? "(no file)" : m_File.filename().string().c_str());
    if (!m_Status.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(m_Status.c_str());
    }
    char filter[64] = {};
    m_Filter.copy(filter, sizeof(filter) - 1);
    if (ImGui::InputTextWithHint("##filter", "filter", filter, sizeof(filter)))
        m_Filter = filter;
    ImGui::Separator();

    // Room for the names right of the widgets (ImGui's default leaves them a third).
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 13.0f);
    // Categories in name order, tweaks in registration order within each.
    std::vector<std::string_view> categories;
    for (const Entry& e : m_Entries)
        if (std::find(categories.begin(), categories.end(), e.Category) == categories.end())
            categories.push_back(e.Category);
    std::sort(categories.begin(), categories.end());
    for (const std::string_view category : categories) {
        const std::string header(category);
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const Entry& e : m_Entries) {
            if (e.Category != category ||
                (!m_Filter.empty() && e.Name.find(m_Filter) == std::string::npos))
                continue;
            ImGui::PushID(e.Value);
            const bool ranged = e.Range.Max > e.Range.Min;
            const char* label = e.Name.c_str();
            switch (e.Type) {
            case TweakType::Float: {
                f32& v = *static_cast<f32*>(e.Value);
                if (ranged)
                    ImGui::SliderFloat(label, &v, e.Range.Min, e.Range.Max, "%.3f");
                else
                    ImGui::DragFloat(label, &v, 0.01f + std::abs(v) * 0.005f, 0.0f, 0.0f, "%.3f");
                break;
            }
            case TweakType::Int: {
                i32& v = *static_cast<i32*>(e.Value);
                if (ranged)
                    ImGui::SliderInt(label, &v, static_cast<i32>(e.Range.Min),
                                     static_cast<i32>(e.Range.Max));
                else
                    ImGui::DragInt(label, &v);
                break;
            }
            case TweakType::Bool:
                ImGui::Checkbox(label, static_cast<bool*>(e.Value));
                break;
            case TweakType::Color:
                ImGui::ColorEdit4(label, static_cast<f32*>(e.Value));
                break;
            }
            // Changed from the code's default: a reset button (and a reminder to copy it back).
            if (!SameValue(e.Type, Read(e), e.Default)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("reset"))
                    Write(e, e.Default);
                else if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Changed from the default in the code");
            }
            ImGui::PopID();
        }
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

} // namespace Emerald
