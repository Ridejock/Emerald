#include <charconv>
#include <cstdlib>
#include <string_view>

#include <SDL3/SDL.h>

#include <Emerald/Emerald.h>

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

namespace {

#if EMERALD_WITH_ENTT
struct Position {
    float X = 0.0f, Y = 0.0f;
};
struct Velocity {
    float X = 0.0f, Y = 0.0f;
};
#endif

class Sandbox final : public Emerald::Application {
public:
    using Application::Application;

protected:
    void OnStart() override
    {
        EM_INFO("Sandbox started - press Escape or close the window to quit");
#if EMERALD_WITH_ENTT
        auto& registry = GetRegistry();
        const auto entity = registry.create();
        registry.emplace<Position>(entity, 100.0f, 100.0f);
        registry.emplace<Velocity>(entity, 120.0f, 80.0f);
        EM_INFO("EnTT enabled: created entity {}", static_cast<std::uint32_t>(entity));
#endif
    }

    void OnEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            Quit();
    }

    void OnUpdate(float dt) override
    {
        m_Time += dt;
#if EMERALD_WITH_ENTT
        int w = 0, h = 0;
        SDL_GetWindowSize(GetWindow().GetNativeWindow(), &w, &h);
        GetRegistry().view<Position, Velocity>().each([&](Position& p, Velocity& v) {
            p.X += v.X * dt;
            p.Y += v.Y * dt;
            if (p.X < 0.0f || p.X > static_cast<float>(w - 50))
                v.X = -v.X;
            if (p.Y < 0.0f || p.Y > static_cast<float>(h - 50))
                v.Y = -v.Y;
        });
#endif
    }

    void OnRender() override
    {
        SDL_Renderer* renderer = GetWindow().GetRenderer();
        SDL_SetRenderDrawColor(renderer, 46, 204, 113, 255); // emerald green
#if EMERALD_WITH_ENTT
        GetRegistry().view<Position>().each([&](const Position& p) {
            const SDL_FRect rect{p.X, p.Y, 50.0f, 50.0f};
            SDL_RenderFillRect(renderer, &rect);
        });
#else
        const SDL_FRect rect{100.0f, 100.0f, 50.0f, 50.0f};
        SDL_RenderFillRect(renderer, &rect);
#endif
    }

    void OnImGui() override
    {
#if EMERALD_WITH_IMGUI
        ImGui::Begin("Emerald");
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(GetFrameCount()));
        ImGui::Text("Time:  %.2f s", static_cast<double>(m_Time));
        ImGui::End();
#endif
    }

    void OnShutdown() override { EM_INFO("Sandbox shutting down after {:.2f}s", m_Time); }

private:
    float m_Time = 0.0f;
};

std::uint64_t ParseFrames(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--frames") {
            std::uint64_t value = 0;
            std::string_view arg(argv[i + 1]);
            std::from_chars(arg.data(), arg.data() + arg.size(), value);
            return value;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    Emerald::ApplicationSpec spec;
    spec.Window.Title = "Emerald Sandbox";
    spec.MaxFrames = ParseFrames(argc, argv);

    Sandbox app(spec);
    return app.Run();
}
