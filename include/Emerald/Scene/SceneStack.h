#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec3.h"
#include "Emerald/Tween/Easing.h"
#include "Emerald/Tween/Tween.h"

union SDL_Event;
struct SDL_GPURenderPass;

namespace Emerald {

class Input;
class Renderer2D;
class SceneStack;

// One screen of the game: title, gameplay, pause menu, game over... Derive from it and override
// the hooks you need; they mirror Application's. Scenes live on a SceneStack, usually the
// Application's (GetScenes()), and only the top one gets input.
//
//   class PauseScene final : public Scene {
//   public:
//       explicit PauseScene(Application& app) : Scene("Pause"), m_App(app) { DrawBelow = true; }
//       void OnUpdate(f32) override
//       {
//           if (m_App.GetInput().WasActionPressed("Pause"))
//               GetStack()->Pop(Transition::Fade(0.2f));
//       }
//       ...
//   };
class Scene {
public:
    explicit Scene(std::string name) : m_Name(std::move(name)) {}
    virtual ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Set these in the constructor. With DrawBelow the scene under this one is drawn first (an
    // overlay such as a pause menu); with UpdateBelow it keeps updating too (without input).
    bool DrawBelow = false;
    bool UpdateBelow = false;

    // --- Hooks, called by the stack ---
    virtual void OnEnter() {}  // now on the stack (pushed, or put in by Replace)
    virtual void OnExit() {}   // leaving the stack (popped, replaced, cleared); destroyed next
    virtual void OnPause() {}  // another scene was pushed on top of this one
    virtual void OnResume() {} // the scene on top was popped: this one is the top again
    virtual void OnEvent(const SDL_Event& /*event*/) {} // the focused scene only
    virtual void OnFixedUpdate(f32 /*dt*/) {}
    virtual void OnUpdate(f32 /*dt*/) {}
    virtual void OnRender2D(Renderer2D& /*r*/) {} // Begin / Draw* / End, as in the app
    virtual void OnRender(SDL_GPURenderPass* /*renderPass*/) {} // raw draw calls, below the 2D
    virtual void OnImGui() {} // only with ImGui (EMERALD_WITH_IMGUI); every scene on the stack

    [[nodiscard]] const std::string& GetName() const { return m_Name; }
    // The stack the scene is on (null before it is pushed and after it left).
    [[nodiscard]] SceneStack* GetStack() const { return m_Stack; }
    // The top scene while no transition runs: the one whose input is live.
    [[nodiscard]] bool HasFocus() const;

private:
    friend class SceneStack;
    std::string m_Name;
    SceneStack* m_Stack = nullptr;
};

// How a stack change looks. The screen is covered over Duration seconds, the change happens
// while it is fully covered, then it is uncovered over Duration seconds again. Input is blocked
// for the whole time. Duration 0 (the default) changes the stack with no transition.
struct Transition {
    f32 Duration = 0.0f;          // each half: covering, then uncovering
    Vec3 Color{0.0f, 0.0f, 0.0f}; // the fade's color
    Easing Curve = Easing::QuadInOut;
    // A custom look: draws the cover at `amount` (0 = clear, 1 = covered, already eased) over the
    // whole screen, inside a pixel-space Renderer2D batch. Empty: a fade to Color.
    std::function<void(Renderer2D& r, Vec2 viewSize, f32 amount)> Draw;

    [[nodiscard]] static Transition Fade(f32 seconds, Vec3 color = {0.0f, 0.0f, 0.0f})
    {
        Transition fade;
        fade.Duration = seconds;
        fade.Color = color;
        return fade;
    }
};

// A stack of scenes: the top one is the current screen, the ones below wait (or, with the top's
// DrawBelow / UpdateBelow, keep drawing / updating under it).
//
//   GetScenes().Push(std::make_unique<TitleScene>(*this));          // in OnStart
//   GetScenes().Replace(std::make_unique<GameScene>(*this), Transition::Fade(0.4f));
//   GetScenes().Push(std::make_unique<PauseScene>(*this));          // over the game
//   GetScenes().Pop();                                              // back to the game
//
// Changes are requests: they are queued and applied at the end of Update, never while a scene's
// hook is running, so a scene can pop itself from its own OnUpdate. With a transition, the
// queue waits until it is over. Hooks run bottom to top. The top scene gets input; the others,
// and every scene during a transition, see the Input as blocked (Input::SetBlocked).
class SceneStack {
public:
    // `input` is blocked for the scenes that should not see it; null (tests) blocks nothing.
    explicit SceneStack(Input* input = nullptr) : m_Input(input) {}
    ~SceneStack() { ExitAll(); }
    SceneStack(const SceneStack&) = delete;
    SceneStack& operator=(const SceneStack&) = delete;

    // --- Requests (applied at the end of Update) ---
    void Push(std::unique_ptr<Scene> scene, Transition transition = {});
    void Pop(Transition transition = {});
    void Replace(std::unique_ptr<Scene> scene, Transition transition = {}); // pop + push
    void Clear(Transition transition = {}); // every scene leaves (top first)
    // Every scene leaves (top first), then `scene` enters: e.g. game over -> back to the title.
    void ReplaceAll(std::unique_ptr<Scene> scene, Transition transition = {});
    // Right now: every scene leaves, pending requests and the transition are dropped. For
    // shutdown (Application does it before releasing its resources).
    void ExitAll();

    // --- The loop (Application calls these after its own hooks) ---
    void OnEvent(const SDL_Event& event);
    void FixedUpdate(f32 dt);
    // The scenes, then the transition, then the queued changes.
    void Update(f32 dt);
    void Render(SDL_GPURenderPass* pass);
    // The drawn scenes bottom to top, then the transition's cover on top of them.
    void Render2D(Renderer2D& r, Vec2 viewSize);
    void ImGui();

    // --- State ---
    [[nodiscard]] bool IsEmpty() const { return m_Scenes.empty(); }
    [[nodiscard]] usize GetCount() const { return m_Scenes.size(); }
    [[nodiscard]] Scene* GetTop() const
    {
        return m_Scenes.empty() ? nullptr : m_Scenes.back().get();
    }
    [[nodiscard]] Scene& GetScene(usize index) const { return *m_Scenes[index]; } // 0 = bottom
    // Whether a scene is drawn / updated this frame (the top always is; below it, as long as
    // every scene above has DrawBelow / UpdateBelow).
    [[nodiscard]] bool IsDrawn(usize index) const { return index >= GetFirst(true); }
    [[nodiscard]] bool IsUpdated(usize index) const { return index >= GetFirst(false); }
    [[nodiscard]] bool IsTransitioning() const { return m_Phase != Phase::Idle; }
    [[nodiscard]] f32 GetCover() const { return m_Cover; } // 0 = clear .. 1 = covered
    [[nodiscard]] usize GetPendingCount() const { return m_Pending.size(); }

private:
    enum class Kind : u8 { Push, Pop, Replace, Clear, ReplaceAll };
    struct Request {
        Kind What = Kind::Push;
        std::unique_ptr<Scene> NewScene; // Push, Replace and ReplaceAll
        Transition Look;
    };
    enum class Phase : u8 { Idle, Covering, Uncovering };

    void Apply(Request& request);
    void ApplyPending();
    [[nodiscard]] usize GetFirst(bool drawn) const; // lowest drawn (or updated) scene
    // Calls fn(scene) for scenes first..top, with the Input blocked for those without focus.
    template <typename Fn> void ForEach(usize first, Fn&& fn);

    Input* m_Input = nullptr;
    std::vector<std::unique_ptr<Scene>> m_Scenes; // bottom first
    std::deque<Request> m_Pending;
    Request m_Current; // the change the running transition is for
    Phase m_Phase = Phase::Idle;
    f32 m_Cover = 0.0f;
    bool m_TweenDone = false; // the current half of the transition has finished
    Tweens m_Tweens;          // drives m_Cover
};

} // namespace Emerald
