// Tests for the immediate-mode UI's logic without a GPU: ids, layout, focus navigation (with
// wrapping), widget values from keys and the mouse, key repeat, and the actions it reads. Draw is
// only smoke-tested (it records sprites; nothing is uploaded).

#include <cmath>
#include <functional>
#include <string>

#include <Emerald/Input/Input.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/UI/UI.h>

#include "Test.h"

using namespace Emerald;

namespace {

constexpr f32 kDt = 1.0f / 60.0f;

// One frame of `describe` with `input`.
void Frame(Ui& ui, const std::function<void()>& describe, const UiInput& input = {})
{
    ui.Begin(input, kDt);
    describe();
    ui.End();
}

// A key press (down this frame) in one direction or on Accept.
UiInput Press(UiButton UiInput::* button)
{
    UiInput input;
    (input.*button) = {.Held = true, .Pressed = true};
    return input;
}

// A plain vertical menu: three buttons.
struct Menu {
    Ui& ui;
    bool Play = false, Options = false, Quit = false;
    void operator()()
    {
        ui.BeginPanel("MENU", {.Position = {100.0f, 50.0f}, .Width = 200.0f});
        Play = ui.Button("Play");
        Options = ui.Button("Options");
        Quit = ui.Button("Quit");
        ui.EndPanel();
    }
};

// The item with this id (or a default one).
UiItem Find(const Ui& ui, u32 id)
{
    for (const UiItem& item : ui.GetItems())
        if (item.Id == id)
            return item;
    return {};
}

} // namespace

TEST(UiIdsHashLabelsAndScopes)
{
    Ui ui;
    CHECK(ui.GetId("Volume##music") != ui.GetId("Volume##sfx"));
    CHECK(ui.GetId("Play") == ui.GetId("Play"));
    CHECK(ui.GetId("Play") != 0);
    const u32 outside = ui.GetId("Delete");
    ui.PushId("slot 1");
    const u32 inside = ui.GetId("Delete");
    ui.PopId();
    CHECK(inside != outside);
    CHECK(ui.GetId("Delete") == outside);

    // "##" hides the rest of the label.
    Frame(ui, [&] { ui.Button("Volume##music"); });
    CHECK(ui.GetItems()[0].Text == "Volume");
}

TEST(UiPanelLayoutIsWholeUnits)
{
    Ui ui; // RowHeight 28, Spacing 6, Padding 12
    const auto describe = [&] {
        ui.BeginPanel("TITLE", {.Position = {100.3f, 50.6f}, .Width = 201.0f});
        ui.Label("Hello");
        ui.Columns(2);
        ui.Button("A");
        ui.Button("B");
        ui.EndPanel();
    };
    Frame(ui, describe);
    const auto& items = ui.GetItems();
    CHECK(items.size() == 4u);
    const UiRect panel = items[0].Rect;
    CHECK(panel.Position == Vec2(100.0f, 51.0f));
    // Padding, title row, label row, button row, padding (no spacing after the last row).
    CHECK_NEAR(panel.Size.y, 12.0f + (28.0f + 6.0f) * 2.0f + 28.0f + 12.0f);
    CHECK(items[1].Rect.Position == Vec2(112.0f, 51.0f + 12.0f + 34.0f));
    // Two columns of the 177 inner units, 6 apart, tiling exactly.
    const UiRect a = items[2].Rect;
    const UiRect b = items[3].Rect;
    CHECK(a.Position.y == b.Position.y);
    CHECK(a.Position.x == 112.0f);
    CHECK(b.Position.x + b.Size.x == 112.0f + 177.0f);
    CHECK(a.Position.x + a.Size.x + 6.0f == b.Position.x);
    for (const UiItem& item : items)
        CHECK(item.Rect.Position.x == std::floor(item.Rect.Position.x) &&
              item.Rect.Size.x == std::floor(item.Rect.Size.x));
}

TEST(UiCenteredPanelUsesLastFramesHeight)
{
    Ui ui;
    const auto describe = [&] {
        ui.BeginPanel("##p",
                      {.Position = {200.0f, 200.0f}, .Width = 100.0f, .Pivot = {0.5f, 0.5f}});
        ui.Button("One");
        ui.Button("Two");
        ui.EndPanel();
    };
    Frame(ui, describe); // the height is not known yet: placed as if 0 high
    Frame(ui, describe);
    const UiRect panel = ui.GetItems()[0].Rect;
    CHECK_NEAR_EPS(panel.GetCenter().y, 200.0f, 0.5f);
    CHECK(panel.Position.x == 150.0f);
}

TEST(UiFocusStartsOnFirstWidgetAndWraps)
{
    Ui ui;
    Menu menu{ui};
    Frame(ui, std::ref(menu));
    CHECK(ui.GetFocusedId() == ui.GetId("Play")); // labels and panels are skipped
    Frame(ui, std::ref(menu), Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("Options"));
    Frame(ui, std::ref(menu), Press(&UiInput::Down));
    Frame(ui, std::ref(menu), Press(&UiInput::Down)); // past the last one: back to the top
    CHECK(ui.GetFocusedId() == ui.GetId("Play"));
    Frame(ui, std::ref(menu), Press(&UiInput::Up)); // and from the top to the bottom
    CHECK(ui.GetFocusedId() == ui.GetId("Quit"));
    // Left / Right have nowhere to go in one column.
    Frame(ui, std::ref(menu), Press(&UiInput::Left));
    CHECK(ui.GetFocusedId() == ui.GetId("Quit"));
}

TEST(UiFirstFrameAccept)
{
    // The first widget has the focus from the very first frame, so Accept pressed as a menu
    // opens picks it (instead of being lost while nothing is focused yet).
    Ui ui;
    Menu menu{ui};
    Frame(ui, std::ref(menu), Press(&UiInput::Accept));
    CHECK(menu.Play && !menu.Options);
    CHECK(Find(ui, ui.GetId("Play")).Focus > 0.0f);
}

TEST(UiFocusMovesByRectangles)
{
    // Wide
    // [L] [R]
    // Back
    Ui ui;
    const auto describe = [&] {
        ui.BeginPanel("##grid", {.Width = 200.0f});
        ui.Button("Wide");
        ui.Columns(2);
        ui.Button("L");
        ui.Button("R");
        ui.Button("Back");
        ui.EndPanel();
    };
    Frame(ui, describe);
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("L")); // equally near: the first
    Frame(ui, describe, Press(&UiInput::Right));
    CHECK(ui.GetFocusedId() == ui.GetId("R"));
    Frame(ui, describe, Press(&UiInput::Right)); // wraps within the row
    CHECK(ui.GetFocusedId() == ui.GetId("L"));
    Frame(ui, describe, Press(&UiInput::Left));
    CHECK(ui.GetFocusedId() == ui.GetId("R"));
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("Back"));
    Frame(ui, describe, Press(&UiInput::Up)); // the nearer of L and R... both: the first
    CHECK(ui.GetFocusedId() == ui.GetId("L"));
}

TEST(UiButtonActivatesOncePerPress)
{
    Ui ui;
    Menu menu{ui};
    Frame(ui, std::ref(menu));
    Frame(ui, std::ref(menu), Press(&UiInput::Accept));
    CHECK(menu.Play && !menu.Options && !menu.Quit);
    UiInput held;
    held.Accept.Held = true; // still down: no second activation
    Frame(ui, std::ref(menu), held);
    CHECK(!menu.Play);
    CHECK(Find(ui, ui.GetId("Play")).Held);
}

TEST(UiValueWidgetsUseLeftAndRight)
{
    Ui ui;
    bool fullscreen = false;
    f32 volume = 0.5f;
    i32 mode = 0;
    const auto describe = [&] {
        ui.BeginPanel("##o", {.Width = 300.0f});
        ui.Toggle("Fullscreen", &fullscreen);
        ui.Slider("Volume", &volume, 0.0f, 1.0f, 0.1f);
        ui.Choice("Mode", &mode, {"Windowed", "Fullscreen", "Borderless"});
        ui.EndPanel();
    };
    Frame(ui, describe);
    Frame(ui, describe, Press(&UiInput::Right)); // the toggle flips instead of moving the focus
    CHECK(fullscreen);
    CHECK(ui.GetFocusedId() == ui.GetId("Fullscreen"));
    Frame(ui, describe, Press(&UiInput::Accept));
    CHECK(!fullscreen);

    Frame(ui, describe, Press(&UiInput::Down));
    for (i32 i = 0; i < 3; ++i)
        Frame(ui, describe, Press(&UiInput::Right));
    CHECK(volume == 0.8f); // snapped to steps: exactly 0.8f, not 0.80000007
    for (i32 i = 0; i < 5; ++i)
        Frame(ui, describe, Press(&UiInput::Right));
    CHECK(volume == 1.0f); // clamped
    CHECK_NEAR(Find(ui, ui.GetId("Volume")).Amount, 1.0f);

    Frame(ui, describe, Press(&UiInput::Down));
    Frame(ui, describe, Press(&UiInput::Left)); // wraps to the last option
    CHECK(mode == 2);
    CHECK(Find(ui, ui.GetId("Mode")).Value == "Borderless");
    Frame(ui, describe, Press(&UiInput::Accept));
    CHECK(mode == 0);
}

TEST(UiHeldDirectionRepeats)
{
    Ui ui; // RepeatDelay 0.4, RepeatRate 0.08
    f32 value = 0.0f;
    const auto describe = [&] { ui.Slider("V", &value, 0.0f, 100.0f, 1.0f); };
    Frame(ui, describe);
    Frame(ui, describe, Press(&UiInput::Right));
    CHECK(value == 1.0f);
    UiInput held;
    held.Right.Held = true;
    // One second held at 60 fps: the delay, then a step every 0.08 s (7 of them in 0.6 s).
    for (i32 i = 0; i < 60; ++i)
        Frame(ui, describe, held);
    CHECK(value >= 7.0f && value <= 9.0f);
    // A stick (held only, no press event) fires at once.
    const f32 before = value;
    Frame(ui, describe); // released
    Frame(ui, describe, held);
    CHECK(value == before + 1.0f);
}

TEST(UiMouseHoverAndClick)
{
    Ui ui;
    Menu menu{ui};
    Frame(ui, std::ref(menu));
    const UiRect quit = Find(ui, ui.GetId("Quit")).Rect;
    UiInput in;
    in.Mouse = quit.GetCenter();
    in.MouseMoved = true;
    Frame(ui, std::ref(menu), in);
    CHECK(ui.GetFocusedId() == ui.GetId("Quit")); // moving over it focuses it
    in.MouseMoved = false;
    in.Click = {.Held = true, .Pressed = true};
    Frame(ui, std::ref(menu), in);
    CHECK(!menu.Quit); // a click is press and release
    in.Click = {.Released = true};
    Frame(ui, std::ref(menu), in);
    CHECK(menu.Quit);

    // Pressed on Quit but released elsewhere: nothing.
    in.Click = {.Held = true, .Pressed = true};
    Frame(ui, std::ref(menu), in);
    in.Mouse = {0.0f, 0.0f};
    in.Click = {.Released = true};
    Frame(ui, std::ref(menu), in);
    CHECK(!menu.Quit && !menu.Play && !menu.Options);
}

TEST(UiMouseDragsSlider)
{
    Ui ui;
    f32 value = 0.0f;
    const auto describe = [&] {
        ui.BeginPanel("##s", {.Width = 224.0f}); // 200 inner units: track from x 122 to 206
        ui.Slider("Music", &value, 0.0f, 10.0f);
        ui.EndPanel();
    };
    Frame(ui, describe);
    const UiRect row = Find(ui, ui.GetId("Music")).Rect;
    const f32 trackX = row.Position.x + row.Size.x * 0.55f;
    const f32 trackW = row.Size.x * 0.45f - ui.GetStyle().Spacing;
    UiInput in;
    in.Mouse = {trackX + trackW * 0.25f, row.GetCenter().y};
    in.Click = {.Held = true, .Pressed = true};
    Frame(ui, describe, in);
    CHECK_NEAR_EPS(value, 2.5f, 0.01f);
    in.Click = {.Held = true};
    in.Mouse.x = trackX + trackW * 2.0f; // dragged past the end, even outside the row
    Frame(ui, describe, in);
    CHECK(value == 10.0f);
}

TEST(UiFocusFollowsDisappearingWidgets)
{
    Ui ui;
    bool showExtra = true;
    const auto describe = [&] {
        ui.Button("A");
        if (showExtra)
            ui.Button("Extra");
    };
    Frame(ui, describe);
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("Extra"));
    showExtra = false;
    Frame(ui, describe);
    CHECK(ui.GetFocusedId() == ui.GetId("A"));
    ui.SetFocus(ui.GetId("A"));
}

TEST(UiFocusHighlightEases)
{
    Ui ui; // FocusSpeed 12 per second
    Menu menu{ui};
    Frame(ui, std::ref(menu));
    Frame(ui, std::ref(menu));
    const f32 early = Find(ui, ui.GetId("Play")).Focus;
    CHECK(early > 0.0f && early < 0.5f);
    for (i32 i = 0; i < 10; ++i)
        Frame(ui, std::ref(menu));
    CHECK(Find(ui, ui.GetId("Play")).Focus == 1.0f);
    CHECK(Find(ui, ui.GetId("Quit")).Focus == 0.0f);
    Frame(ui, std::ref(menu)); // and it stays there (no flicker round the full value)
    CHECK(Find(ui, ui.GetId("Play")).Focus == 1.0f);
}

TEST(UiReadsActions)
{
    Keyboard keyboard;
    Gamepads gamepads;
    Mouse mouse;
    Input input(keyboard, gamepads, &mouse);
    BindDefaultUiActions(input);
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_DOWN);
    keyboard.OnKeyDown(SDL_SCANCODE_RETURN);
    mouse.BeginFrame();
    mouse.OnMotion({5.0f, 6.0f});
    mouse.OnButton(MouseButton::Left, true);
    UiInput in = ReadUiInput(input, {50.0f, 60.0f});
    CHECK(in.Down.Held && in.Down.Pressed && !in.Up.Held);
    CHECK(in.Accept.Pressed && !in.Back.Pressed);
    CHECK(in.Click.Pressed && in.MouseMoved);
    CHECK(in.Mouse == Vec2(50.0f, 60.0f)); // the caller's (UI-space) position
    // Rebinding works like for any action.
    input.RebindAction(kUiAccept, {Key::J});
    CHECK(!ReadUiInput(input, {}).Accept.Held);
    // Blocked (e.g. during a scene transition): nothing, the mouse included.
    input.SetBlocked(true);
    in = ReadUiInput(input, {});
    CHECK(!in.Down.Held && !in.Click.Held && !in.MouseMoved);
}

TEST(UiDrawRecordsSprites)
{
    Ui ui;
    bool on = true;
    f32 v = 0.5f;
    i32 c = 0;
    Frame(ui, [&] {
        ui.BeginPanel("TITLE", {.Width = 200.0f});
        ui.Label("Label", TextAlign::Center);
        ui.Button("Button");
        ui.Toggle("Toggle", &on);
        ui.Slider("Slider", &v, 0.0f, 1.0f);
        ui.Choice("Choice", &c, {"One", "Two"});
        ui.EndPanel();
    });
    const Font font = Font::CreatePlaceholder(nullptr, {.Size = 8.0f});
    Renderer2D r;
    r.Begin(Mat4::OrthoPixelSpace(320.0f, 180.0f));
    ui.Draw(r, font);
    r.End();
    CHECK(r.GetSpriteCount() > 20u);

    // A 9-slice panel: nine pieces, corners at their size.
    const Texture texture = Texture::CreateWithoutGpu(12, 12);
    const Sprite frame = Sprite::FromTexture(texture);
    Renderer2D nine;
    nine.Begin(Mat4::OrthoPixelSpace(320.0f, 180.0f));
    DrawNineSlice(nine, frame, {{10.0f, 10.0f}, {100.0f, 50.0f}}, 4.0f, 2.0f);
    nine.End();
    CHECK(nine.GetSpriteCount() == 9u);
}
