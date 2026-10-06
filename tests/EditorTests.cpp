// Particle effect files, Tweak variables and the entity inspector: the parts of the editor tools
// that don't need a window or a GPU. The registry and inspector only exist in ImGui builds; the
// other builds check that their stand-ins are just the default values.

#include "Test.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <Emerald/Core/Log.h>
#include <Emerald/Editor/EntityInspector.h>
#include <Emerald/Editor/Tweak.h>
#include <Emerald/Entity/Components.h>
#include <Emerald/Particles/ParticleEffect.h>

using namespace Emerald;
namespace fs = std::filesystem;

namespace {

fs::path TempFolder(const char* name)
{
    const fs::path dir = fs::temp_directory_path() / "emerald_editor_tests" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void WriteText(const fs::path& file, const std::string& text)
{
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

// Every field away from its default, with values that aren't round in binary.
ParticleEffect MakeEveryField()
{
    const ParticleEmitterConfig config{.StartColor = {0.9f, 0.7f, 0.3f, 1.0f},
                                       .EndColor = {0.8f, 0.1f, 0.05f, 0.25f},
                                       .Shape = EmitterShape::Line,
                                       .Radius = 4.5f,
                                       .LineHalfExtent = {12.25f, -3.0f},
                                       .Speed = {35.5f, 210.0f},
                                       .Angle = {-0.7f, 1.3f},
                                       .Lifetime = {0.15f, 0.8f},
                                       .Drag = 1.75f,
                                       .Gravity = {-5.0f, 140.0f},
                                       .StartSize = 3.3f,
                                       .EndSize = 0.6f,
                                       .Rate = 42.0f};
    return ParticleEffect(config, 37, BlendMode::Alpha);
}

void CheckSame(const ParticleEffect& a, const ParticleEffect& b)
{
    const ParticleEmitterConfig& x = a.GetConfig();
    const ParticleEmitterConfig& y = b.GetConfig();
    CHECK(x.StartColor == y.StartColor && x.EndColor == y.EndColor);
    CHECK(x.Shape == y.Shape && x.Radius == y.Radius && x.LineHalfExtent == y.LineHalfExtent);
    CHECK(x.Speed.Min == y.Speed.Min && x.Speed.Max == y.Speed.Max);
    // Angles are written in degrees (rounded to 0.001), so they come back nearly equal.
    CHECK_NEAR_EPS(x.Angle.Min, y.Angle.Min, 1e-4f);
    CHECK_NEAR_EPS(x.Angle.Max, y.Angle.Max, 1e-4f);
    CHECK(x.Lifetime.Min == y.Lifetime.Min && x.Lifetime.Max == y.Lifetime.Max);
    CHECK(x.Drag == y.Drag && x.Gravity == y.Gravity);
    CHECK(x.StartSize == y.StartSize && x.EndSize == y.EndSize && x.Rate == y.Rate);
    CHECK(a.Burst == b.Burst && a.Blend == b.Blend);
}

} // namespace

TEST(ParticleEffectJsonRoundTrip)
{
    Log::Init({}); // broken files are logged
    const ParticleEffect effect = MakeEveryField();
    const std::string json = ParticleEffectToJson(effect);
    const std::optional<ParticleEffect> parsed = ParseParticleEffect(json);
    CHECK(parsed.has_value());
    if (!parsed)
        return;
    CheckSame(effect, *parsed);
    CHECK(ParticleEffectToJson(*parsed) == json); // stable: saving again changes nothing
    CHECK(json.find("\"shape\": \"line\"") != std::string::npos);
    CHECK(json.find("\"blend\": \"alpha\"") != std::string::npos);
    CHECK(json.find("\"speed\": [35.5, 210]") != std::string::npos);

    // The defaults survive too (360 degrees back to TwoPi).
    const std::optional<ParticleEffect> defaults =
        ParseParticleEffect(ParticleEffectToJson(ParticleEffect{}));
    CHECK(defaults.has_value());
    if (defaults)
        CheckSame(ParticleEffect{}, *defaults);
}

TEST(ParticleEffectParseRules)
{
    // Every key is optional: missing ones keep the config's defaults. Unknown keys are ignored.
    std::optional<ParticleEffect> e = ParseParticleEffect(
        R"({ "shape": "circle", "radius": 6, "angle": [0, 90], "burst": 12, "colour": 1 })");
    CHECK(e.has_value());
    if (e) {
        CHECK(e->GetConfig().Shape == EmitterShape::Circle && e->GetConfig().Radius == 6.0f);
        CHECK_NEAR(e->GetConfig().Angle.Max, HalfPi);
        CHECK(e->Burst == 12 && e->Blend == BlendMode::Additive);
        CHECK(e->GetConfig().StartSize == ParticleEmitterConfig{}.StartSize);
    }
    // A color may leave out alpha (opaque).
    e = ParseParticleEffect(R"({ "startColor": [0.5, 0.25, 1] })");
    CHECK(e && e->GetConfig().StartColor == Vec4(0.5f, 0.25f, 1.0f, 1.0f));

    // Broken files are rejected (and logged) rather than half applied.
    CHECK(!ParseParticleEffect("{ not json"));
    CHECK(!ParseParticleEffect("[1, 2]"));
    CHECK(!ParseParticleEffect(R"({ "shape": "square" })"));
    CHECK(!ParseParticleEffect(R"({ "radius": "big" })"));
    CHECK(!ParseParticleEffect(R"({ "speed": [1] })"));
    CHECK(!ParseParticleEffect(R"({ "burst": -3 })"));
    CHECK(!ParseParticleEffect(R"({ "blend": true })"));

    // Each parse gets a new revision, so editors notice a reload.
    const u32 first = ParseParticleEffect("{}")->Revision;
    CHECK(ParseParticleEffect("{}")->Revision != first);
}

TEST(ParticleEffectFilesAndCopies)
{
    const fs::path dir = TempFolder("particles");
    const ParticleEffect effect = MakeEveryField();
    CHECK(SaveParticleEffect(dir / "fx.json", effect));
    const std::optional<ParticleEffect> loaded = LoadParticleEffect(dir / "fx.json");
    CHECK(loaded.has_value());
    if (loaded)
        CheckSame(effect, *loaded);
    CHECK(!LoadParticleEffect(dir / "missing.json"));
    WriteText(dir / "broken.json", R"({ "radius": "big" })");
    CHECK(!LoadParticleEffect(dir / "broken.json"));

    // Copies are deep, and assignment keeps the config's address (hot reload relies on it).
    ParticleEffect a;
    const ParticleEmitterConfig* before = &a.GetConfig();
    ParticleEffect b = effect;
    b.GetConfig().Rate = 7.0f;
    CHECK(effect.GetConfig().Rate == 42.0f);
    a = b;
    CHECK(&a.GetConfig() == before && a.GetConfig().Rate == 7.0f && a.Burst == 37);
}

// A color tweak is four plain floats (no 16-byte alignment to pad the structs it sits in), and a
// tweak without ImGui is just its value.
static_assert(alignof(Tweak<Vec4>) == alignof(f32) && sizeof(Tweak<Vec4>) == 4 * sizeof(f32));
static_assert(sizeof(Tweak<bool>) == sizeof(bool));

#if EMERALD_WITH_IMGUI

TEST(TweaksRegisterAndJsonRoundTrip)
{
    TweakRegistry& tweaks = GetTweaks();
    tweaks.Clear();
    {
        Tweak<f32> jump{"Player", "Jump velocity", 310.0f, {50.0f, 900.0f}};
        Tweak<i32> lives{"Player", "Lives", 3, {1, 9}};
        Tweak<bool> trail{"Effects", "Show trail", true};
        Tweak<Vec4> sky{"Effects", "Sky", {0.25f, 0.5f, 0.75f, 1.0f}};
        CHECK(tweaks.GetEntries().size() == 4);
        CHECK(jump == 310.0f && lives == 3 && trail.Get() && sky.Get().y == 0.5f);

        jump.Set(333.5f);
        lives.Set(5);
        trail.Set(false);
        sky.Set({0.1f, 0.2f, 0.3f, 0.4f});
        const std::string json = tweaks.ToJson();
        // Grouped by category, sorted, one value per line.
        CHECK(json.find("\"Effects\": {") < json.find("\"Player\": {"));
        CHECK(json.find("\"Jump velocity\": 333.5") != std::string::npos);
        CHECK(json.find("\"Lives\": 5") != std::string::npos);
        CHECK(json.find("\"Show trail\": false") != std::string::npos);
        CHECK(json.find("\"Sky\": [0.1, 0.2, 0.3, 0.4]") != std::string::npos);

        tweaks.ResetAll();
        CHECK(jump == 310.0f && lives == 3 && trail.Get() && sky.Get().w == 1.0f);
        CHECK(tweaks.FromJson(json));
        CHECK(jump == 333.5f && lives == 5 && !trail.Get());
        CHECK(sky.Get() == Vec4(0.1f, 0.2f, 0.3f, 0.4f));
        CHECK(tweaks.ToJson() == json);

        // Wrong kinds are skipped (logged), the rest still applies; a 3-number color is opaque.
        CHECK(tweaks.FromJson(R"({ "Player": { "Jump velocity": true, "Lives": 7 },
                                   "Effects": { "Sky": [1, 0, 0] } })"));
        CHECK(jump == 333.5f && lives == 7 && sky.Get() == Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        CHECK(!tweaks.FromJson("[]") && !tweaks.FromJson("{ broken"));
    }
    CHECK(tweaks.GetEntries().empty()); // destroyed tweaks unregister
}

TEST(TweaksRememberValuesAndFiles)
{
    TweakRegistry& tweaks = GetTweaks();
    tweaks.Clear();
    const fs::path dir = TempFolder("tweaks");

    // Values loaded before a tweak exists are applied when it registers; unknown ones are kept.
    WriteText(dir / "tweaks.json",
              R"({ "Platformer": { "Gravity": 1500, "Coyote time": 0.12 }, "Old": { "X": 1 } })");
    CHECK(tweaks.Load(dir / "tweaks.json"));
    {
        Tweak<f32> gravity{"Platformer", "Gravity", 1100.0f};
        CHECK(gravity == 1500.0f);
        gravity.Set(1600.0f);
    }
    // The value outlives the tweak (a scene that is left and entered again keeps it).
    {
        Tweak<f32> gravity{"Platformer", "Gravity", 1100.0f};
        Tweak<f32> coyote{"Platformer", "Coyote time", 0.08f};
        CHECK(gravity == 1600.0f && coyote == 0.12f);
        CHECK(tweaks.Save(dir / "saved.json"));
    }
    std::ifstream in(dir / "saved.json");
    const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(saved.find("\"Gravity\": 1600") != std::string::npos);
    CHECK(saved.find("\"Old\": {") != std::string::npos); // kept for a tweak not registered now

    // SetFile loads at once, and Save() writes back to it.
    tweaks.Clear();
    tweaks.SetFile(dir / "saved.json");
    {
        Tweak<i32> count{"Old", "X", 0};
        CHECK(count == 1);
        count.Set(4);
        CHECK(tweaks.Save());
    }
    tweaks.Clear();
    CHECK(tweaks.Load(dir / "saved.json"));
    Tweak<i32> count{"Old", "X", 0};
    CHECK(count == 4);
    CHECK(!tweaks.Load(dir / "missing.json"));
    tweaks.SetFile({});
    tweaks.Clear();
}

namespace {
struct Health {
    i32 Points = 10;
};
struct Frozen {};
} // namespace

TEST(EntityInspectorRegistration)
{
    World world;
    EntityInspector inspector;
    inspector.Add<Health>("Health", [](Health& h) { h.Points = 42; }); // stands in for widgets
    inspector.AddTag<Frozen>("Frozen");

    Entity a = world.Spawn();
    a.Add<Transform>(Transform{.Position = {10.0f, 0.0f}});
    a.Add<Health>();
    a.Add<Frozen>();
    Entity b = world.Spawn();
    b.Add<Transform>(Transform{.Position = {100.0f, 0.0f}});
    b.Add<Velocity>();

    const std::vector<std::string_view> names = inspector.GetComponentNames(a);
    CHECK(names.size() == 3 && names[0] == "Transform" && names[1] == "Health" &&
          names[2] == "Frozen");
    CHECK(inspector.GetComponentNames(b).size() == 2);
    CHECK(inspector.Inspect(a, "Health") && a.Get<Health>().Points == 42);
    CHECK(inspector.Inspect(a, "Frozen"));   // a tag: nothing to draw, still fine
    CHECK(!inspector.Inspect(b, "Health"));  // b has none
    CHECK(!inspector.Inspect(a, "Unknown")); // not registered

    // Picking: the nearest Transform within the radius.
    CHECK(inspector.SelectAt(world, {95.0f, 4.0f}) == b && inspector.GetSelected() == b);
    CHECK(!inspector.SelectAt(world, {50.0f, 0.0f}, 8.0f) && !inspector.GetSelected());
    inspector.Select(a);
    world.Destroy(a); // a destroyed selection reads as none
    CHECK(!inspector.GetSelected());
}

#else

TEST(EditorStubsWithoutImGui)
{
    // A Tweak is its default value; the registry and inspector do nothing.
    static constexpr Tweak<f32> kJump{"Player", "Jump velocity", 310.0f, {50.0f, 900.0f}};
    static_assert(kJump.Get() == 310.0f);
    Tweak<bool> trail{"Effects", "Show trail", true};
    CHECK(trail.Get());
    CHECK(!GetTweaks().Save() && !GetTweaks().Load("tweaks.json"));

    World world;
    Entity e = world.Spawn();
    e.Add<Transform>();
    EntityInspector inspector;
    inspector.Add<Transform>("Transform", [](Transform& t) { t.Position = {1.0f, 1.0f}; });
    CHECK(inspector.GetComponentNames(e).empty() && !inspector.Inspect(e, "Transform"));
    CHECK(!inspector.SelectAt(world, {0.0f, 0.0f}));
}

#endif
