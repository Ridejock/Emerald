// Hello, Emerald.
//
// A window, a clear color, and one line of text, through the public headers only.
// Escape quits. Closing the window quits too: Application ends the loop when the
// close request arrives. The built-in debug font draws letters as capitals, so
// the greeting below is shown as "HELLO, EMERALD!".

#include <string>
#include <string_view>

#include <Emerald/Emerald.h>

namespace {

constexpr std::string_view kHello = "Hello, Emerald!";
constexpr u32 kWindowWidth = 960;
constexpr u32 kWindowHeight = 540;

class Hello final : public Emerald::Application {
public:
    using Application::Application;

protected:
    void OnStart() override { GetInput().BindAction("Quit", {Emerald::Key::Escape}); }

    void OnUpdate(f32 /*deltaSeconds*/) override
    {
        if (GetInput().WasActionPressed("Quit")) {
            Quit();
        }
    }

    void OnRender2D(Emerald::Renderer2D& renderer) override
    {
        const Emerald::Vec2i window = GetWindowSize();
        const Emerald::Vec2 view(window);
        // About an eighth of the window, in whole pixels of the 5x7 debug font.
        const i32 scale = Emerald::Max(1, (window.y / 8) / Emerald::kDebugTextLineHeight);
        const Emerald::Vec2 text = Emerald::MeasureDebugText(kHello, scale);
        const Emerald::Vec2 topLeft = (view - text) * 0.5f;

        renderer.Begin(Emerald::Mat4::OrthoPixelSpace(view.x, view.y));
        Emerald::DrawDebugText(renderer, topLeft, kHello, {1.0f, 1.0f, 1.0f, 1.0f}, scale);
        renderer.End();
    }
};

} // namespace

int main(int argc, char** argv)
{
    Emerald::ApplicationSpec spec;
    spec.Window.Title = std::string(kHello);
    spec.Window.Width = kWindowWidth;
    spec.Window.Height = kWindowHeight;
    // The swapchain is cleared to this at the start of every frame.
    spec.ClearColor = {0.07f, 0.094f, 0.086f, 1.0f};
    spec.ShaderFormats = Emerald::ShaderFormatFromMask(EMERALD_SHADER_FORMATS);
    spec.Args = {argv, static_cast<usize>(argc)};
    return Hello{spec}.Run();
}
