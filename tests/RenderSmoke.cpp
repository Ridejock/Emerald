// One headless GPU frame through the engine's normal path: Window, Renderer::Init (Vulkan),
// Renderer2D quads, RequestScreenshot. The pixels are compared with tests/data/render_smoke.png.
//
// Exit 0: matched (or the reference was updated).
// Exit 77: no Vulkan device. CTest's SKIP_RETURN_CODE turns that into a skip.
// Exit 1: a device was required, the device is not SwiftShader when CI says it must be, or the
//         image does not match.
//
// EMERALD_REQUIRE_GPU=1           missing device is a failure (CI).
// EMERALD_REQUIRE_SWIFTSHADER=1   the device name must contain "SwiftShader" (CI). The loader is
//                                 pointed at that ICD with VK_DRIVER_FILES and VK_ICD_FILENAMES;
//                                 this check is what fails the job if Mesa was selected instead.
// EMERALD_UPDATE_RENDER_REFERENCE=1
//                                 write the frame over the reference. Refused unless the device
//                                 name contains "SwiftShader", so another driver cannot replace it.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#include <Emerald/Assets/Image.h>
#include <Emerald/Core/Log.h>
#include <Emerald/Core/Window.h>
#include <Emerald/Math/Mat4.h>
#include <Emerald/Renderer/Renderer.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Renderer/Texture.h>

using namespace Emerald;

namespace {

constexpr int kSkip = 77;
constexpr int kChannelTolerance = 2;
constexpr u32 kWidth = 96;
constexpr u32 kHeight = 64;

// Clear color, as bytes. The corners of the frame stay this color.
constexpr u8 kClearR = 18;
constexpr u8 kClearG = 32;
constexpr u8 kClearB = 51;

// 4x4 checker, nearest-filtered. 0 is the dark texel, 1 the light one.
constexpr u8 kChecker[4][4] = {
    {0, 1, 0, 1},
    {1, 0, 1, 0},
    {0, 1, 0, 1},
    {1, 0, 1, 0},
};
constexpr u8 kCheckerDark = 16;
constexpr u8 kCheckerLight = 240;

enum class SpriteTex : u8 { White, Checker };

// One axis-aligned quad. Positions are pixels, origin top-left, so edges land on pixel borders.
struct Quad {
    SpriteTex Texture;
    BlendMode Blend;
    f32 X, Y, W, H;
    u8 R, G, B, A;
};

// Opaque red, green at half alpha over it, an additive blue on top of both, then the checker
// (tinted, and a second copy faded). All of it sits inside the 96x64 frame.
constexpr Quad kQuads[] = {
    {SpriteTex::White, BlendMode::Alpha, 4.0f, 4.0f, 36.0f, 24.0f, 210, 48, 36, 255},
    {SpriteTex::White, BlendMode::Alpha, 16.0f, 12.0f, 32.0f, 22.0f, 32, 176, 72, 128},
    {SpriteTex::White, BlendMode::Additive, 24.0f, 18.0f, 28.0f, 18.0f, 24, 64, 220, 200},
    {SpriteTex::Checker, BlendMode::Alpha, 56.0f, 4.0f, 32.0f, 32.0f, 255, 214, 96, 255},
    {SpriteTex::Checker, BlendMode::Alpha, 64.0f, 20.0f, 24.0f, 24.0f, 255, 255, 255, 153},
};

const std::filesystem::path kReference{EMERALD_RENDER_SMOKE_REFERENCE};
constexpr const char* kActualName = "render-smoke-actual.png";

bool EnvFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle)
{
    if (needle.empty() || haystack.size() < needle.size())
        return false;
    const auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (usize i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (usize j = 0; j < needle.size(); ++j) {
            if (lower(static_cast<unsigned char>(haystack[i + j])) !=
                lower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

Vec4 ByteColor(u8 r, u8 g, u8 b, u8 a)
{
    return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

Image MakeChecker()
{
    Image image;
    image.Width = 4;
    image.Height = 4;
    image.Pixels.resize(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const u8 value = kChecker[y][x] ? kCheckerLight : kCheckerDark;
            u8* pixel = &image.Pixels[static_cast<usize>(y * 4 + x) * 4];
            pixel[0] = pixel[1] = pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return image;
}

void DrawScene(Renderer2D& renderer, const Texture& checker, u32 width, u32 height)
{
    renderer.Begin(Mat4::OrthoPixelSpace(static_cast<f32>(width), static_cast<f32>(height)));
    for (const Quad& quad : kQuads) {
        renderer.SetBlendMode(quad.Blend);
        const Vec4 tint = ByteColor(quad.R, quad.G, quad.B, quad.A);
        const Vec2 topLeft{quad.X, quad.Y};
        const Vec2 size{quad.W, quad.H};
        if (quad.Texture == SpriteTex::White)
            renderer.FillRect(topLeft, size, tint);
        else {
            renderer.DrawSprite(checker, topLeft,
                                {.Size = size, .Origin = {0.0f, 0.0f}, .Tint = tint});
        }
    }
    renderer.End();
}

// No Vulkan device: skip locally, fail when CI requires a device.
int NoDevice(const char* why)
{
    if (EnvFlag("EMERALD_REQUIRE_GPU") || EnvFlag("EMERALD_REQUIRE_SWIFTSHADER")) {
        std::printf("FAIL: %s (EMERALD_REQUIRE_GPU / EMERALD_REQUIRE_SWIFTSHADER is set)\n", why);
        return 1;
    }
    std::printf("SKIP: %s\n", why);
    return kSkip;
}

int Compare(const Image& got)
{
    if (got.Width != static_cast<i32>(kWidth) || got.Height != static_cast<i32>(kHeight)) {
        std::printf("FAIL: frame is %dx%d, the reference is %ux%u\n", got.Width, got.Height, kWidth,
                    kHeight);
        return 1;
    }

    const std::optional<Image> reference = Image::LoadFromFile(kReference);
    if (!reference) {
        std::printf("FAIL: could not read reference %s\n", kReference.string().c_str());
        std::printf("      Set EMERALD_UPDATE_RENDER_REFERENCE=1 with the SwiftShader ICD "
                    "selected to create it.\n");
        return 1;
    }
    if (reference->Width != got.Width || reference->Height != got.Height ||
        reference->Pixels.size() != got.Pixels.size()) {
        std::printf("FAIL: reference %s is %dx%d, frame is %dx%d\n", kReference.string().c_str(),
                    reference->Width, reference->Height, got.Width, got.Height);
        return 1;
    }

    int maxDelta = 0;
    int outside = 0;
    int firstX = 0;
    int firstY = 0;
    for (int y = 0; y < got.Height; ++y) {
        for (int x = 0; x < got.Width; ++x) {
            const usize offset = static_cast<usize>(y * got.Width + x) * 4;
            bool pixelOutside = false;
            for (int channel = 0; channel < 4; ++channel) {
                const int delta = std::abs(static_cast<int>(got.Pixels[offset + channel]) -
                                           static_cast<int>(reference->Pixels[offset + channel]));
                if (delta > maxDelta)
                    maxDelta = delta;
                if (delta > kChannelTolerance)
                    pixelOutside = true;
            }
            if (pixelOutside) {
                if (outside == 0) {
                    firstX = x;
                    firstY = y;
                }
                ++outside;
            }
        }
    }

    std::printf("compared %dx%d, max channel delta %d (tolerance %d), pixels outside %d\n",
                got.Width, got.Height, maxDelta, kChannelTolerance, outside);
    if (outside == 0)
        return 0;

    const usize offset = static_cast<usize>(firstY * got.Width + firstX) * 4;
    const u8* g = &got.Pixels[offset];
    const u8* r = &reference->Pixels[offset];
    std::printf("FAIL: first mismatch at %d,%d got %u %u %u %u reference %u %u %u %u\n", firstX,
                firstY, g[0], g[1], g[2], g[3], r[0], r[1], r[2], r[3]);
    return 1;
}

int Run()
{
    if (!InitVideo())
        return NoDevice("SDL video did not start");

    WindowSpec spec;
    spec.Title = "Emerald render smoke";
    spec.Width = kWidth;
    spec.Height = kHeight;
    spec.Resizable = false;
    spec.VSync = false;
    Window window(spec);
    if (!window.IsValid())
        return NoDevice("could not open a window");

    Renderer renderer;
    if (!renderer.Init(window.GetNativeWindow(), spec.VSync,
                       ShaderFormatFromMask(EMERALD_SHADER_FORMATS), "vulkan"))
        return NoDevice("could not create a Vulkan device");

    const std::string driver(renderer.GetDriverName());
    const std::string device(renderer.GetDeviceName());
    std::printf("GPU driver '%s', device '%s'\n", driver.c_str(), device.c_str());
    std::fflush(stdout);

    const bool swiftShader = ContainsIgnoreCase(device, "swiftshader");
    if (EnvFlag("EMERALD_REQUIRE_SWIFTSHADER") && !swiftShader) {
        std::printf("FAIL: CI requires SwiftShader, got driver '%s' device '%s'\n", driver.c_str(),
                    device.c_str());
        return 1;
    }
    if (driver != "vulkan")
        return NoDevice("GPU backend is not Vulkan");

    Renderer2D renderer2D;
    if (!renderer2D.Init(renderer.GetDevice(), renderer.GetSwapchainFormat())) {
        std::printf("FAIL: Renderer2D::Init failed (shaders missing, or the device rejected the "
                    "pipelines)\n");
        return 1;
    }
    const Image checkerImage = MakeChecker();
    std::optional<Texture> checker = Texture::Create(renderer.GetDevice(), checkerImage);
    if (!checker) {
        std::printf("FAIL: could not upload the checker texture\n");
        return 1;
    }

    const std::filesystem::path shot =
        std::filesystem::temp_directory_path() / "emerald-render-smoke.png";
    renderer.RequestScreenshot(shot);
    if (renderer.BeginFrame() != FrameStatus::Ready) {
        std::printf("FAIL: BeginFrame did not return a drawable swapchain\n");
        return 1;
    }
    const u32 width = renderer.GetFrameWidth();
    const u32 height = renderer.GetFrameHeight();
    DrawScene(renderer2D, *checker, width, height);
    renderer2D.Upload(renderer.GetCommandBuffer());
    const Color clear{kClearR / 255.0f, kClearG / 255.0f, kClearB / 255.0f, 1.0f};
    GpuRenderPass* pass = renderer.BeginRenderPass(clear);
    if (!pass) {
        std::printf("FAIL: BeginRenderPass failed\n");
        renderer.EndFrame();
        return 1;
    }
    renderer2D.Render(renderer.GetCommandBuffer(), pass, width, height);
    renderer.EndRenderPass();
    if (!renderer.EndFrame()) {
        std::printf("FAIL: EndFrame failed\n");
        return 1;
    }

    const std::optional<Image> got = Image::LoadFromFile(shot);
    if (!got) {
        std::printf("FAIL: screenshot was not written to %s\n", shot.string().c_str());
        return 1;
    }

    if (EnvFlag("EMERALD_UPDATE_RENDER_REFERENCE")) {
        if (!swiftShader) {
            std::printf("FAIL: refusing to update %s from device '%s' (need SwiftShader)\n",
                        kReference.string().c_str(), device.c_str());
            return 1;
        }
        std::error_code error;
        std::filesystem::create_directories(kReference.parent_path(), error);
        std::filesystem::copy_file(shot, kReference,
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            std::printf("FAIL: could not write %s: %s\n", kReference.string().c_str(),
                        error.message().c_str());
            return 1;
        }
        std::printf("updated reference %s from device '%s'\n", kReference.string().c_str(),
                    device.c_str());
        return 0;
    }

    const int compared = Compare(*got);
    if (compared != 0) {
        std::error_code error;
        std::filesystem::copy_file(shot, kActualName,
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (!error)
            std::printf("wrote %s\n", kActualName);
    }
    return compared;
}

} // namespace

int main()
{
    Log::Init({});
    const int code = Run();
    std::fflush(stdout);
    Log::Shutdown();
    return code;
}
