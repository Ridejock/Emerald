// CrtEffect's CPU side: afterglow decay, the composite uniforms and the bloom tap spacing.
// (The passes themselves need a GPU; the sandbox and games show them.)

#include <cmath>

#include <Emerald/Renderer/CrtEffect.h>

#include "Test.h"

using Emerald::CrtEffect;
using Emerald::CrtParams;

TEST(CrtAfterglowDecay)
{
    CHECK(CrtEffect::AfterglowDecay(0.0f, 1.0f / 60.0f) == 0.0f); // off: no trail
    // One half-life halves the image, two quarter it.
    CHECK_NEAR_EPS(CrtEffect::AfterglowDecay(0.05f, 0.05f), 0.5f, 1e-5f);
    CHECK_NEAR_EPS(CrtEffect::AfterglowDecay(0.05f, 0.1f), 0.25f, 1e-5f);
    // Frame-rate independent: two 120 Hz frames fade as much as one 60 Hz frame.
    const f32 fast = CrtEffect::AfterglowDecay(0.035f, 1.0f / 120.0f);
    CHECK_NEAR_EPS(fast * fast, CrtEffect::AfterglowDecay(0.035f, 1.0f / 60.0f), 1e-5f);
    // dt is clamped: a zero-length frame still fades a little, a long hitch not to nothing.
    CHECK(CrtEffect::AfterglowDecay(0.035f, 0.0f) < 1.0f);
    CHECK(CrtEffect::AfterglowDecay(0.035f, 10.0f) == CrtEffect::AfterglowDecay(0.035f, 0.25f));
}

TEST(CrtUniforms)
{
    const CrtParams defaults;
    // A vector monitor has no scanlines or mask: both are off by default.
    CHECK(defaults.Scanlines == 0.0f && defaults.Mask == 0.0f);

    CrtParams params;
    params.ChromaticOffset = 2.0f;
    params.Scanlines = 3.0f; // clamped to 0..1
    params.Mask = -1.0f;
    const Emerald::CrtUniforms u = CrtEffect::MakeUniforms(params, 1920, 1080);
    CHECK(u.Size[0] == 1920.0f && u.Size[1] == 1080.0f);
    // 2 px at 1080p, as UV per axis.
    CHECK_NEAR_EPS(u.Chroma[0], 2.0f / 1920.0f, 1e-7f);
    CHECK_NEAR_EPS(u.Chroma[1], 2.0f / 1080.0f, 1e-7f);
    CHECK(u.Scanlines == 1.0f && u.Mask == 0.0f);
    CHECK(u.Curvature == params.Curvature && u.Bloom == params.Bloom);
    // The fringe scales with the resolution: 1 px at 540p for the same setting.
    const Emerald::CrtUniforms half = CrtEffect::MakeUniforms(params, 960, 540);
    CHECK_NEAR_EPS(half.Chroma[1] * 540.0f, 1.0f, 1e-5f);
    // Laid out like the shader's cbuffer: 12 floats, a multiple of 16 bytes.
    CHECK(sizeof(Emerald::CrtUniforms) == 48);
    // A zero-sized frame does not divide by zero.
    CHECK(std::isfinite(CrtEffect::MakeUniforms(params, 0, 0).Chroma[0]));
}

TEST(CrtBloomSpread)
{
    CrtParams params;
    const f32 nearStep = CrtEffect::BlurStepPixels(params, 720, false);
    CHECK(nearStep > 0.0f);
    CHECK_NEAR_EPS(CrtEffect::BlurStepPixels(params, 720, true), 3.0f * nearStep, 1e-4f);
    // Relative to the screen height, and proportional to BloomRadius.
    CHECK_NEAR_EPS(CrtEffect::BlurStepPixels(params, 1440, false), 2.0f * nearStep, 1e-4f);
    params.BloomRadius = 2.0f;
    CHECK_NEAR_EPS(CrtEffect::BlurStepPixels(params, 720, false), 2.0f * nearStep, 1e-4f);
}
