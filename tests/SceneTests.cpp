// The scene stack without a GPU: deferred push / pop / replace / clear with their hooks in order,
// requests made from inside hooks, DrawBelow / UpdateBelow, transitions (timing, queueing, the
// cover drawn on top, custom looks) and input blocked for scenes without focus.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Emerald/Core/Log.h>
#include <Emerald/Input/Gamepads.h>
#include <Emerald/Input/Input.h>
#include <Emerald/Input/Keyboard.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Scene/SceneStack.h>

#include "Test.h"

using namespace Emerald;

namespace {

using Log_ = std::vector<std::string>;

// Writes "<name>:<hook>" for every hook into a shared log; OnUpdate can run a test's lambda.
class Probe final : public Scene {
public:
    Probe(std::string name, Log_& log, bool drawBelow = false, bool updateBelow = false)
        : Scene(std::move(name)), m_Log(log)
    {
        DrawBelow = drawBelow;
        UpdateBelow = updateBelow;
    }
    ~Probe() override { Add("destroyed"); }

    void OnEnter() override { Add("enter"); }
    void OnExit() override { Add("exit"); }
    void OnPause() override { Add("pause"); }
    void OnResume() override { Add("resume"); }
    void OnFixedUpdate(f32) override { Add("fixed"); }
    void OnUpdate(f32) override
    {
        Add("update");
        if (Action)
            Action(*this);
    }
    void OnRender2D(Renderer2D&) override { Add("draw"); }

    std::function<void(Probe&)> Action; // run from OnUpdate

private:
    void Add(const char* hook) { m_Log.push_back(GetName() + ":" + hook); }
    Log_& m_Log;
};

std::unique_ptr<Probe> Make(const char* name, Log_& log, bool drawBelow = false,
                            bool updateBelow = false)
{
    return std::make_unique<Probe>(name, log, drawBelow, updateBelow);
}

bool Same(const Log_& log, const Log_& expected)
{
    if (log == expected)
        return true;
    std::string got;
    for (const std::string& s : log)
        got += s + " ";
    std::printf("    log was: %s\n", got.c_str());
    return false;
}

} // namespace

TEST(ScenesChangesWaitForUpdate)
{
    Log::Init({});
    Log_ log;
    SceneStack stack;
    stack.Push(Make("A", log));
    CHECK(stack.IsEmpty() && stack.GetPendingCount() == 1 && log.empty()); // only queued
    stack.Update(0.016f);
    CHECK(stack.GetCount() == 1 && stack.GetTop()->GetName() == "A");
    CHECK(Same(log, {"A:enter"})); // A did not update yet: it arrived at the end of the frame
    CHECK(stack.GetTop()->GetStack() == &stack && stack.GetTop()->HasFocus());
}

TEST(ScenesPushPopReplaceClearHooks)
{
    Log_ log;
    SceneStack stack;
    stack.Push(Make("A", log));
    stack.Push(Make("B", log)); // two requests in one frame: applied in order
    stack.Update(0.0f);
    CHECK(Same(log, {"A:enter", "A:pause", "B:enter"}));
    CHECK(stack.GetCount() == 2 && stack.GetScene(0).GetName() == "A");

    log.clear();
    stack.Pop();
    stack.Update(0.0f);
    CHECK(Same(log, {"B:update", "B:exit", "B:destroyed", "A:resume"}));

    log.clear();
    stack.Push(Make("P", log));
    stack.Replace(Make("C", log)); // P leaves, C enters; A stays paused under C
    stack.Update(0.0f);
    CHECK(Same(log, {"A:update", "A:pause", "P:enter", "P:exit", "P:destroyed", "C:enter"}));
    CHECK(stack.GetCount() == 2 && stack.GetTop()->GetName() == "C");

    log.clear();
    stack.Clear();
    stack.Update(0.0f);
    CHECK(Same(log, {"C:update", "C:exit", "C:destroyed", "A:exit", "A:destroyed"}));
    CHECK(stack.IsEmpty());

    // ReplaceAll: everything leaves top-down, then the new scene enters (back to the title).
    stack.Push(Make("Game", log));
    stack.Push(Make("Pause", log));
    stack.Update(0.0f);
    log.clear();
    stack.ReplaceAll(Make("Title", log));
    stack.Update(0.0f);
    CHECK(Same(log, {"Pause:update", "Pause:exit", "Pause:destroyed", "Game:exit", "Game:destroyed",
                     "Title:enter"}));
    CHECK(stack.GetCount() == 1 && stack.GetTop()->GetName() == "Title");
    stack.Clear();
    stack.Update(0.0f);

    // Popping an empty stack is logged and ignored; null scenes are refused.
    stack.Pop();
    stack.Push(nullptr);
    stack.Update(0.0f);
    CHECK(stack.IsEmpty() && stack.GetPendingCount() == 0);
}

TEST(ScenesRequestsFromInsideHooks)
{
    Log_ log;
    SceneStack stack;
    stack.Push(Make("Game", log));
    stack.Update(0.0f);
    log.clear();

    // The game pushes a pause scene from its own update, the pause scene pops itself (and
    // replaces the game with the title) from its first update: nothing changes mid-hook.
    auto* game = static_cast<Probe*>(stack.GetTop());
    game->Action = [&](Probe& self) {
        self.GetStack()->Push(Make("Pause", log, true));
        self.Action = nullptr;
    };
    stack.Update(0.0f);
    CHECK(Same(log, {"Game:update", "Game:pause", "Pause:enter"}));

    log.clear();
    auto* pause = static_cast<Probe*>(stack.GetTop());
    pause->Action = [&](Probe& self) {
        CHECK(self.GetStack()->GetCount() == 2); // still there while its hook runs
        self.GetStack()->Pop();
        self.GetStack()->Replace(Make("Title", log));
    };
    stack.Update(0.0f); // Pause has UpdateBelow off: only it updates
    CHECK(Same(log, {"Pause:update", "Pause:exit", "Pause:destroyed", "Game:resume", "Game:exit",
                     "Game:destroyed", "Title:enter"}));
    CHECK(stack.GetCount() == 1 && stack.GetTop()->GetName() == "Title");
}

TEST(ScenesDrawBelowAndUpdateBelow)
{
    Log_ log;
    Renderer2D r; // CPU side only
    SceneStack stack;
    stack.Push(Make("World", log));
    stack.Push(Make("Hud", log, true, true));    // draws and updates the world under it
    stack.Push(Make("Pause", log, true, false)); // draws the others, freezes them
    stack.Update(0.0f);
    CHECK(stack.IsDrawn(0) && stack.IsDrawn(1) && stack.IsDrawn(2));
    CHECK(!stack.IsUpdated(0) && !stack.IsUpdated(1) && stack.IsUpdated(2));

    log.clear();
    stack.FixedUpdate(0.01f);
    stack.Update(0.01f);
    r.Clear();
    stack.Render2D(r, {640.0f, 360.0f});
    CHECK(Same(log, {"Pause:fixed", "Pause:update", "World:draw", "Hud:draw", "Pause:draw"}));

    // Without the pause: the HUD updates the world under it (bottom first).
    log.clear();
    stack.Pop();
    stack.Update(0.01f);
    log.clear();
    stack.Update(0.01f);
    CHECK(Same(log, {"World:update", "Hud:update"}));

    // A scene without DrawBelow hides everything under it.
    stack.Push(Make("Full", log));
    stack.Update(0.0f);
    CHECK(!stack.IsDrawn(1) && stack.IsDrawn(3) && !stack.IsUpdated(1));
}

TEST(ScenesFadeTransition)
{
    Log_ log;
    Renderer2D r;
    SceneStack stack;
    stack.Push(Make("A", log));
    stack.Update(0.0f);
    log.clear();

    // 0.5 s to cover, the change, 0.5 s to uncover.
    stack.Push(Make("B", log), Transition::Fade(0.5f, {1.0f, 0.0f, 0.0f}));
    stack.Update(0.0f); // the fade starts at the end of this frame
    CHECK(stack.IsTransitioning() && stack.GetCount() == 1 && stack.GetCover() == 0.0f);
    stack.Update(0.25f);
    CHECK(stack.GetCount() == 1 && stack.GetCover() > 0.3f && stack.GetCover() < 0.7f);
    CHECK(!stack.GetTop()->HasFocus()); // no focus while transitioning

    // The cover is drawn last, over the scenes, in the fade color.
    r.Clear();
    stack.Render2D(r, {640.0f, 360.0f});
    CHECK(r.GetSpriteCount() == 1 && r.GetBatches().size() == 1);
    const Renderer2D::SpriteVertex& v = r.GetSpriteVertices()[0];
    CHECK((v.Color & 0xffu) == 0xffu); // red (packed RGBA, R in the low byte)

    // Something asked for during the transition waits for it to end.
    stack.Pop();
    stack.Update(0.25f); // covered: B arrives
    CHECK(stack.GetCount() == 2 && NearlyEqual(stack.GetCover(), 1.0f));
    CHECK(Same(log, {"A:update", "A:update", "A:draw", "A:update", "A:pause", "B:enter"}));
    stack.Update(0.25f);
    CHECK(stack.IsTransitioning() && stack.GetCount() == 2 && stack.GetPendingCount() == 1);
    stack.Update(0.25f); // uncovered; then the queued Pop runs at once
    CHECK(!stack.IsTransitioning() && stack.GetCover() == 0.0f && stack.GetCount() == 1);
    CHECK(stack.GetTop()->GetName() == "A" && stack.GetTop()->HasFocus());
}

TEST(ScenesFadeIntoAnEmptyStackAndCustomLook)
{
    Log_ log;
    Renderer2D r;
    SceneStack stack;
    std::vector<f32> amounts;
    Transition look;
    look.Duration = 0.2f;
    look.Curve = Easing::Linear;
    look.Draw = [&](Renderer2D&, Vec2 size, f32 amount) {
        CHECK(size == Vec2(320.0f, 200.0f));
        amounts.push_back(amount);
    };
    stack.Push(Make("Title", log), look);
    stack.Update(0.0f); // nothing to cover: the scene is there at once, fully covered
    CHECK(stack.GetCount() == 1 && stack.IsTransitioning() && stack.GetCover() == 1.0f);
    stack.Update(0.1f);
    r.Clear();
    stack.Render2D(r, {320.0f, 200.0f});
    CHECK(amounts.size() == 1 && NearlyEqual(amounts[0], 0.5f) && r.GetSpriteCount() == 0);
    stack.Update(0.1f);
    CHECK(!stack.IsTransitioning());
    stack.Render2D(r, {320.0f, 200.0f});
    CHECK(amounts.size() == 1); // no transition, no cover
}

TEST(ScenesBlockInputWithoutFocus)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    input.BindAction("Jump", {Key::Space});
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);

    SceneStack stack(&input);
    std::vector<std::string> sawJump;
    const auto watch = [&](Probe& self) {
        if (input.IsActionDown("Jump"))
            sawJump.push_back(self.GetName());
    };
    Log_ log;
    auto world = Make("World", log);
    world->Action = watch;
    auto hud = Make("Hud", log, true, true);
    hud->Action = watch;
    stack.Push(std::move(world));
    stack.Push(std::move(hud));
    stack.Update(0.0f);
    stack.Update(0.0f);
    CHECK(sawJump == std::vector<std::string>{"Hud"});       // the world updates, but blind
    CHECK(input.IsActionDown("Jump") && !input.IsBlocked()); // the app still sees it

    // During a transition nobody gets input.
    stack.Pop(Transition::Fade(0.1f));
    stack.Update(0.0f); // the HUD still has focus this frame; the fade starts after it
    sawJump.clear();
    stack.Update(0.05f);
    CHECK(sawJump.empty() && stack.IsTransitioning());
    stack.Update(0.1f); // covered: the HUD goes
    stack.Update(0.1f); // uncovered
    CHECK(sawJump.empty());
    stack.Update(0.0f);
    CHECK(!stack.IsTransitioning() && sawJump == std::vector<std::string>{"World"});

    // An app that blocked input itself keeps it blocked.
    sawJump.clear();
    input.SetBlocked(true);
    stack.Update(0.0f);
    CHECK(sawJump.empty() && input.IsBlocked() && !input.WasActionPressed("Jump"));
    CHECK(input.GetAxis("Nothing") == 0.0f);
}

TEST(ScenesExitAllOnDestruction)
{
    Log_ log;
    {
        SceneStack stack;
        stack.Push(Make("A", log));
        stack.Push(Make("B", log));
        stack.Update(0.0f);
        stack.Push(Make("Never", log)); // still queued: dropped first, never entered
        log.clear();
    }
    CHECK(Same(log, {"Never:destroyed", "B:exit", "B:destroyed", "A:exit", "A:destroyed"}));
}
