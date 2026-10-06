// Particle effect files. nlohmann::json stays in here; the header has the plain class.

#include "Emerald/Particles/ParticleEffect.h"

#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "../Core/FloatText.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

// A new field in the config must be added to the reader, the writer, the editor and the test.
static_assert(sizeof(ParticleEmitterConfig) == 96, "ParticleEmitterConfig changed: update "
                                                   "ParticleEffect.cpp and ParticleEditor.cpp");

namespace {

using Json = nlohmann::json;

std::atomic<u32> s_Revision{0};

// Degrees for the file, rounded so the default 2 pi reads "360", not "360.00003".
f32 FileDegrees(f32 radians)
{
    return std::round(ToDegrees(radians) * 1000.0f) / 1000.0f;
}

// Reads the members it is asked for and remembers the first problem.
class Reader {
public:
    explicit Reader(const Json& root) : m_Root(root) {}
    std::string Error;

    void Number(const char* key, f32& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_number())
            return Fail(key, "a number");
        out = value->get<f32>();
    }

    void Count(const char* key, u32& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_number_unsigned())
            return Fail(key, "a whole number >= 0");
        out = value->get<u32>();
    }

    // A list of `min`..`max` numbers into `out`; returns how many were read.
    usize Numbers(const char* key, f32* out, usize min, usize max, const char* expected)
    {
        const Json* value = Find(key);
        if (!value)
            return 0;
        if (!value->is_array() || value->size() < min || value->size() > max) {
            Fail(key, expected);
            return 0;
        }
        for (usize i = 0; i < value->size(); ++i) {
            if (!(*value)[i].is_number()) {
                Fail(key, "a list of numbers");
                return 0;
            }
            out[i] = (*value)[i].get<f32>();
        }
        return value->size();
    }

    void Vector(const char* key, Vec2& out)
    {
        f32 pair[2] = {out.x, out.y};
        Numbers(key, pair, 2, 2, "[x, y]");
        out = {pair[0], pair[1]};
    }
    void Range(const char* key, FloatRange& out)
    {
        f32 pair[2] = {out.Min, out.Max};
        Numbers(key, pair, 2, 2, "[min, max]");
        out = {pair[0], pair[1]};
    }
    void Color(const char* key, Vec4& out)
    {
        f32 rgba[4] = {out.x, out.y, out.z, out.w};
        if (Numbers(key, rgba, 3, 4, "[r, g, b] or [r, g, b, a]") == 3)
            rgba[3] = 1.0f; // no alpha: opaque
        out = {rgba[0], rgba[1], rgba[2], rgba[3]};
    }

    // One of `names` (lowercase), its index into `out`.
    template <typename E, usize N>
    void Choice(const char* key, E& out, const char* const (&names)[N])
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (value->is_string()) {
            const std::string text = value->get<std::string>();
            for (usize i = 0; i < N; ++i) {
                if (text == names[i]) {
                    out = static_cast<E>(i);
                    return;
                }
            }
        }
        std::string choices;
        for (usize i = 0; i < N; ++i)
            choices += (i > 0 ? " / " : "") + std::string("\"") + names[i] + "\"";
        Fail(key, choices.c_str());
    }

private:
    const Json* Find(const char* key) const
    {
        const auto it = m_Root.find(key);
        return it == m_Root.end() ? nullptr : &*it;
    }

    void Fail(const char* key, const char* expected)
    {
        if (Error.empty())
            Error = std::string("'") + key + "' must be " + expected;
    }

    const Json& m_Root;
};

constexpr const char* kShapes[] = {"point", "circle", "line"};
constexpr const char* kBlends[] = {"alpha", "additive"};

std::string Pair(f32 a, f32 b)
{
    return "[" + FloatToText(a) + ", " + FloatToText(b) + "]";
}

} // namespace

std::optional<ParticleEffect> ParseParticleEffect(std::string_view json, std::string_view source)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        EM_CORE_ERROR("Particle effect {}: not a JSON object", source);
        return std::nullopt;
    }
    ParticleEffect effect;
    ParticleEmitterConfig& c = effect.GetConfig();
    Reader read(root);
    read.Color("startColor", c.StartColor);
    read.Color("endColor", c.EndColor);
    read.Choice("shape", c.Shape, kShapes);
    read.Number("radius", c.Radius);
    read.Vector("lineHalfExtent", c.LineHalfExtent);
    read.Range("speed", c.Speed);
    if (root.contains("angle")) { // degrees in the file
        FloatRange angle;
        read.Range("angle", angle);
        c.Angle = {ToRadians(angle.Min), ToRadians(angle.Max)};
    }
    read.Range("lifetime", c.Lifetime);
    read.Number("drag", c.Drag);
    read.Vector("gravity", c.Gravity);
    read.Number("startSize", c.StartSize);
    read.Number("endSize", c.EndSize);
    read.Number("rate", c.Rate);
    read.Count("burst", effect.Burst);
    read.Choice("blend", effect.Blend, kBlends);
    if (!read.Error.empty()) {
        EM_CORE_ERROR("Particle effect {}: {}", source, read.Error);
        return std::nullopt;
    }
    effect.Revision = ++s_Revision;
    return effect;
}

std::optional<ParticleEffect> LoadParticleEffect(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        EM_CORE_ERROR("Particle effect: can't open {}", file.string());
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return ParseParticleEffect(text, file.filename().string());
}

std::string ParticleEffectToJson(const ParticleEffect& effect)
{
    const ParticleEmitterConfig& c = effect.GetConfig();
    const auto color = [](const Vec4& v) {
        return "[" + FloatToText(v.x) + ", " + FloatToText(v.y) + ", " + FloatToText(v.z) + ", " +
               FloatToText(v.w) + "]";
    };
    std::string out = "{\n";
    const auto line = [&out](const char* key, const std::string& value, bool last = false) {
        out += std::string("  \"") + key + "\": " + value + (last ? "\n" : ",\n");
    };
    line("startColor", color(c.StartColor));
    line("endColor", color(c.EndColor));
    line("shape", std::string("\"") + kShapes[static_cast<usize>(c.Shape)] + "\"");
    line("radius", FloatToText(c.Radius));
    line("lineHalfExtent", Pair(c.LineHalfExtent.x, c.LineHalfExtent.y));
    line("speed", Pair(c.Speed.Min, c.Speed.Max));
    line("angle", Pair(FileDegrees(c.Angle.Min), FileDegrees(c.Angle.Max)));
    line("lifetime", Pair(c.Lifetime.Min, c.Lifetime.Max));
    line("drag", FloatToText(c.Drag));
    line("gravity", Pair(c.Gravity.x, c.Gravity.y));
    line("startSize", FloatToText(c.StartSize));
    line("endSize", FloatToText(c.EndSize));
    line("rate", FloatToText(c.Rate));
    line("burst", std::to_string(effect.Burst));
    line("blend", effect.Blend == BlendMode::Additive ? "\"additive\"" : "\"alpha\"", true);
    return out + "}\n";
}

bool SaveParticleEffect(const std::filesystem::path& file, const ParticleEffect& effect)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << ParticleEffectToJson(effect);
    out.close();
    if (!out) {
        EM_CORE_ERROR("Particle effect: can't write {}", file.string());
        return false;
    }
    EM_CORE_INFO("Particle effect: saved {}", file.string());
    return true;
}

} // namespace Emerald
