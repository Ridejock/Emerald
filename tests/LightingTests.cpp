// Lighting math and the post-effect chain's order, without a GPU.

#include <memory>
#include <string>
#include <vector>

#include <Emerald/Renderer/Light.h>
#include <Emerald/Renderer/PostEffect.h>

#include "Test.h"

using namespace Emerald;
using namespace LightMath;

TEST(LightAttenuation)
{
    CHECK(Attenuation(0.0f, 100.0f, 1.0f) == 1.0f);
    CHECK(Attenuation(100.0f, 100.0f, 1.0f) == 0.0f);
    CHECK(Attenuation(200.0f, 100.0f, 1.0f) == 0.0f);
    CHECK(Attenuation(50.0f, 100.0f, 1.0f) == 0.5f);
    CHECK_NEAR_EPS(Attenuation(50.0f, 100.0f, 2.0f), 0.25f, 1e-5f); // falloff squares the fade
    CHECK(Attenuation(10.0f, 0.0f, 1.0f) == 0.0f);
}

TEST(LightSpotCone)
{
    CHECK(SpotFactor(1.0f, 0.9f, 0.7f) == 1.0f); // on axis
    CHECK(SpotFactor(0.5f, 0.9f, 0.7f) == 0.0f); // outside
    CHECK(SpotFactor(0.8f, 0.9f, 0.7f) > 0.0f);
    CHECK(SpotFactor(0.8f, 0.9f, 0.7f) < 1.0f);
    CHECK(SpotFactor(0.0f, 1.0f, -2.0f) == 1.0f); // point light: always on
}

TEST(LightShadePointAndSpot)
{
    const Vec3 up{0.0f, 0.0f, 1.0f};
    Light point{.Kind = LightKind::Point,
                .Position = {0.0f, 0.0f},
                .Z = 10.0f,
                .Color = {1.0f, 0.0f, 0.0f},
                .Intensity = 1.0f,
                .Radius = 100.0f};
    // Directly under the light: N·L is almost 1, attenuation almost 1.
    const Vec3 under = ShadeLight({0.0f, 0.0f}, up, point);
    CHECK(under.x >= 0.9f && under.y == 0.0f && under.z == 0.0f);
    // Far away: nothing.
    CHECK(Length(ShadeLight({500.0f, 0.0f}, up, point)) == 0.0f);

    Light spot = point;
    spot.Kind = LightKind::Spot;
    spot.Color = {0.0f, 1.0f, 0.0f};
    spot.Direction = {0.0f, 1.0f}; // aims +Y
    spot.InnerAngle = 0.2f;
    spot.OuterAngle = 0.5f;
    // On the beam (below the light along +Y): lit.
    const Vec3 onBeam = ShadeLight({0.0f, 40.0f}, up, spot);
    CHECK(onBeam.y > 0.0f);
    // Beside the light, outside the cone: dark.
    const Vec3 beside = ShadeLight({80.0f, 0.0f}, up, spot);
    CHECK(beside.y == 0.0f);

    // Ambient adds under every light.
    const Vec3 ambient{0.1f, 0.1f, 0.1f};
    const Light lights[] = {point};
    const Vec3 shaded = Shade({0.0f, 0.0f}, up, ambient, lights);
    CHECK(shaded.x > under.x && shaded.y == ambient.y);
}

TEST(LightingUniformsPack)
{
    Light a{.Position = {1.0f, 2.0f},
            .Z = 3.0f,
            .Color = {0.5f, 0.25f, 0.125f},
            .Intensity = 2.0f,
            .Radius = 50.0f,
            .Falloff = 1.5f};
    Light b = a;
    b.Kind = LightKind::Spot;
    b.Direction = {0.0f, 1.0f};
    b.InnerAngle = 0.0f;
    b.OuterAngle = 1.0f;
    const Light many[kMaxLights + 2]{}; // extras dropped
    const LightingUniforms u = LightingUniforms::Make({0.2f, 0.3f, 0.4f}, {&a, 1});
    CHECK(u.Ambient.x == 0.2f && u.Ambient.y == 0.3f && u.Ambient.z == 0.4f);
    CHECK(u.Count.x == 1.0f);
    CHECK(u.Lights[0].x == 1.0f && u.Lights[0].y == 2.0f && u.Lights[0].z == 3.0f &&
          u.Lights[0].w == 50.0f);
    CHECK(u.Lights[1].w == 2.0f);  // intensity
    CHECK(u.Lights[2].w == -2.0f); // point: SpotFactor always 1
    CHECK(u.Lights[3].x == 1.5f);
    const LightingUniforms spot = LightingUniforms::Make({}, {&b, 1});
    CHECK(spot.Lights[2].z == 1.0f); // cos(0)
    CHECK_NEAR_EPS(spot.Lights[2].w, std::cos(1.0f), 1e-5f);
    CHECK(LightingUniforms::Make({}, many).Count.x == static_cast<f32>(kMaxLights));
    CHECK(sizeof(LightingUniforms) % 16 == 0);
}

namespace {

// Records Apply calls; no GPU.
class FakeEffect final : public PostEffect {
public:
    explicit FakeEffect(std::string name, std::vector<std::string>* log)
        : m_Name(std::move(name)), m_Log(log)
    {
    }
    bool Init(SDL_GPUDevice*, SDL_GPUTextureFormat) override { return true; }
    void Shutdown() override {}
    void Apply(SDL_GPUCommandBuffer*, SDL_GPUTexture*, SDL_GPUTexture*, u32, u32, f32) override
    {
        m_Log->push_back(m_Name);
    }
    [[nodiscard]] std::string_view GetName() const override { return m_Name; }

private:
    std::string m_Name;
    std::vector<std::string>* m_Log;
};

} // namespace

TEST(PostChainOrder)
{
    PostChain chain;
    std::vector<std::string> log;
    CHECK(chain.IsEmpty());
    CHECK(chain.Add(std::make_unique<FakeEffect>("A", &log)) == 0u);
    CHECK(chain.Add(std::make_unique<FakeEffect>("B", &log)) == 1u);
    CHECK(chain.Add(std::make_unique<FakeEffect>("C", &log)) == 2u);
    CHECK(chain.GetCount() == 3u);
    CHECK(chain.Get(1)->GetName() == "B");
    CHECK(chain.Find("C") == chain.Get(2));
    chain.Move(2, 0); // C A B
    CHECK(chain.Get(0)->GetName() == "C");
    CHECK(chain.Get(1)->GetName() == "A");
    CHECK(chain.Get(2)->GetName() == "B");
    chain.Remove(1); // C B
    CHECK(chain.GetCount() == 2u);
    CHECK(chain.Find("A") == nullptr);
    // Apply with no GPU targets is a no-op (no scene texture yet); just check Clear.
    chain.Clear();
    CHECK(chain.IsEmpty());
}
