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
UiInput Press(UiButton UiInput::*button)
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
    CHECK(in.Mouse == Vec2(50.0f, 60.0f));                     // the caller's (UI-space) position
    CHECK(!in.AcceptPrompt.empty() && !in.BackPrompt.empty()); // the bound keys, for PromptBar
    // Rebinding works like for any action, and the prompt follows the new key.
    input.RebindAction(kUiAccept, {Key::J});
    const UiInput rebound = ReadUiInput(input, {});
    CHECK(!rebound.Accept.Held);
    CHECK(rebound.AcceptPrompt == "J");
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

// Pop-ups close at once in these tests; the fade has its own test. No prompt glyphs, so no bar.
void InstantPopups(Ui& ui)
{
    ui.GetStyle().PopupOpenSeconds = 0.0f;
    ui.GetStyle().PopupCloseSeconds = 0.0f;
    ui.GetStyle().PopupMaxRows = 3;
    ui.GetStyle().PopupGap = 4.0f;
}

UiPopupOptions PopupBox(UiPopupAnchor anchor = UiPopupAnchor::Center)
{
    return {.Width = 180.0f, .Anchor = anchor, .Screen = {400.0f, 300.0f}, .Prompt = false};
}

const UiItem* FindKind(const Ui& ui, UiKind kind, u32 scope)
{
    for (const UiItem& item : ui.GetItems())
        if (item.Kind == kind && (scope == 0 || item.Scope == scope))
            return &item;
    return nullptr;
}

TEST(UiPopupFocusReturnsToTheOpener)
{
    Ui ui;
    InstantPopups(ui);
    bool shop = false;
    bool potion = false;
    const auto describe = [&] {
        ui.BeginPanel("SHOP", {.Position = {40.0f, 30.0f}, .Width = 200.0f});
        shop = ui.Button("Shop");
        ui.Button("Leave");
        ui.EndPanel();
        if (shop)
            ui.OpenPopup("wares");
        if (ui.BeginPopup("wares", PopupBox())) {
            potion = ui.Button("Potion"); // the press that opened the list must not select this
            ui.Button("Ether");
            ui.EndPopup();
        }
    };
    Frame(ui, describe);
    CHECK(ui.GetFocusedId() == ui.GetId("Shop"));
    Frame(ui, describe, Press(&UiInput::Accept));
    CHECK(shop && !potion);
    CHECK(ui.IsPopupOpen("wares") && ui.GetPopupCount() == 1u);
    CHECK(ui.GetFocusedId() == ui.GetId("Potion"));
    Frame(ui, describe, Press(&UiInput::Back));
    CHECK(!ui.IsPopupOpen("wares") && ui.WasPopupClosed("wares"));
    CHECK(ui.GetFocusedId() == ui.GetId("Shop"));
    CHECK(ui.GetPopupCount() == 0u);
}

TEST(UiPopupStackRestoresEachOpener)
{
    Ui ui;
    InstantPopups(ui);
    bool shop = false;
    bool potion = false;
    bool yes = false;
    const auto describe = [&] {
        shop = ui.Button("Shop");
        if (shop)
            ui.OpenPopup("wares");
        if (ui.BeginPopup("wares", PopupBox())) {
            potion = ui.Button("Potion");
            ui.Button("Ether");
            if (potion)
                ui.OpenPopup("confirm");
            ui.EndPopup();
        }
        if (ui.BeginPopup("confirm", PopupBox())) {
            ui.Label("Buy it?");
            yes = ui.Button("Yes");
            if (ui.Button("No"))
                ui.ClosePopup();
            ui.EndPopup();
        }
    };
    Frame(ui, describe, Press(&UiInput::Accept)); // Shop opens the list; focus lands on Potion
    CHECK(ui.GetPopupCount() == 1u && ui.GetFocusedId() == ui.GetId("Potion"));
    Frame(ui, describe, Press(&UiInput::Accept)); // Potion opens confirm; Yes is not also pressed
    CHECK(!yes && ui.GetPopupCount() == 2u);
    CHECK(ui.GetFocusedId() == ui.GetId("Yes"));
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("No"));
    Frame(ui, describe, Press(&UiInput::Accept)); // No closes confirm
    CHECK(!ui.IsPopupOpen("confirm") && ui.IsPopupOpen("wares"));
    CHECK(ui.GetFocusedId() == ui.GetId("Potion"));
    CHECK(ui.GetPopupCount() == 1u);
    Frame(ui, describe, Press(&UiInput::Back));
    CHECK(!ui.IsPopupOpen("wares") && ui.GetFocusedId() == ui.GetId("Shop"));
}

TEST(UiPopupScrollsWhenTheListIsLongerThanTheWindow)
{
    Ui ui;
    InstantPopups(ui); // three visible rows, row 28 + gap 6
    constexpr i32 kRows = 8;
    // A panel on the screen under the list: layout has to scroll anyway (every item, panels
    // included, shares one local-y list).
    const auto describe = [&] {
        ui.BeginPanel("##shop", {.Position = {8.0f, 8.0f}, .Width = 80.0f});
        ui.Button("Shop");
        ui.EndPanel();
        if (ui.BeginPopup("list", PopupBox())) {
            for (i32 i = 0; i < kRows; ++i)
                ui.Button("Row " + std::to_string(i));
            ui.EndPopup();
        }
    };
    // Opened from outside the describe: the list is the whole pop-up.
    ui.OpenPopup("list");
    Frame(ui, describe);
    CHECK(ui.GetFocusedId() == ui.GetId("Row 0"));
    CHECK(ui.GetPopupScroll() == 0.0f);
    CHECK(Find(ui, ui.GetId("Row 0")).Shown);
    CHECK(!Find(ui, ui.GetId("Row 3")).Shown);
    const UiItem* thumbItem = nullptr;
    const UiItem* trackItem = nullptr;
    for (const UiItem& item : ui.GetItems()) {
        if (item.Kind != UiKind::Scrollbar)
            continue;
        if (item.Amount > 0.5f)
            thumbItem = &item;
        else
            trackItem = &item;
    }
    CHECK(thumbItem != nullptr && trackItem != nullptr);
    if (thumbItem && trackItem)
        CHECK(thumbItem->Rect.Size.y < trackItem->Rect.Size.y);

    Frame(ui, describe, Press(&UiInput::Down));
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("Row 2"));
    CHECK(ui.GetPopupScroll() == 0.0f); // still inside the three visible rows
    Frame(ui, describe, Press(&UiInput::Down));
    CHECK(ui.GetFocusedId() == ui.GetId("Row 3"));
    CHECK_NEAR(ui.GetPopupScroll(), 34.0f); // one row (28 + 6) so Row 3 sits in the window
    CHECK(!Find(ui, ui.GetId("Row 0")).Shown);
    CHECK(Find(ui, ui.GetId("Row 3")).Shown);

    // Row 3 is focused. Three Ups return to Row 0; one more wraps to the last row.
    Frame(ui, describe, Press(&UiInput::Up));
    Frame(ui, describe, Press(&UiInput::Up));
    Frame(ui, describe, Press(&UiInput::Up));
    CHECK(ui.GetFocusedId() == ui.GetId("Row 0"));
    CHECK(ui.GetPopupScroll() == 0.0f);
    Frame(ui, describe, Press(&UiInput::Up));
    CHECK(ui.GetFocusedId() == ui.GetId("Row 7"));
    CHECK(ui.GetPopupScroll() > 34.0f);
    CHECK(Find(ui, ui.GetId("Row 7")).Shown);
    CHECK(!Find(ui, ui.GetId("Row 0")).Shown);
}

TEST(UiPopupCancelClosesWithoutUsingTheScreen)
{
    Ui ui;
    InstantPopups(ui);
    bool shop = false;
    bool left = false;
    const auto describe = [&] {
        ui.BeginPanel("##shop", {.Position = {80.0f, 40.0f}, .Width = 160.0f});
        shop = ui.Button("Shop");
        ui.EndPanel();
        if (shop)
            ui.OpenPopup("wares");
        if (ui.BeginPopup("wares", PopupBox())) {
            ui.Button("Potion");
            ui.EndPopup();
        }
        if (ui.WasBackPressed())
            left = true;
    };
    Frame(ui, describe, Press(&UiInput::Accept));
    CHECK(ui.IsPopupOpen("wares") && !left);
    Frame(ui, describe, Press(&UiInput::Back));
    CHECK(!ui.IsPopupOpen("wares") && ui.WasPopupClosed("wares"));
    CHECK(!left); // the screen's Back runs only once the pop-up is gone
    CHECK(!shop);
    CHECK(ui.GetFocusedId() == ui.GetId("Shop"));

    Frame(ui, describe, Press(&UiInput::Accept));
    UiInput outside;
    outside.Mouse = {2.0f, 2.0f}; // the shop panel sits at (80, 40); this misses both
    outside.Click = {.Held = true, .Pressed = true};
    shop = false;
    Frame(ui, describe, outside);
    CHECK(!ui.IsPopupOpen("wares") && !shop);

    // Let the centered box settle (height comes from the previous frame) before pressing it.
    Frame(ui, describe, Press(&UiInput::Accept));
    Frame(ui, describe);
    UiRect box;
    bool found = false;
    for (const UiItem& item : ui.GetItems())
        if (item.Kind == UiKind::Panel && item.Scope != 0) {
            box = item.Rect;
            found = true;
        }
    CHECK(found);
    UiInput inside = outside;
    inside.Mouse = box.GetCenter();
    Frame(ui, describe, inside); // press on the box: it stays open
    CHECK(ui.IsPopupOpen("wares"));
}

TEST(UiPopupBackDuringTheFadeDoesNotLeaveTheScreen)
{
    Ui ui;
    ui.GetStyle().PopupOpenSeconds = 0.0f;
    ui.GetStyle().PopupCloseSeconds = 0.2f;
    bool left = false;
    const auto describe = [&] {
        if (ui.Button("Shop"))
            ui.OpenPopup("wares");
        if (ui.BeginPopup("wares", PopupBox())) {
            ui.Button("Potion");
            ui.EndPopup();
        }
        if (ui.WasBackPressed())
            left = true;
    };
    Frame(ui, describe, Press(&UiInput::Accept));
    CHECK(ui.IsPopupOpen("wares") && !left);
    Frame(ui, describe, Press(&UiInput::Back)); // starts the fade; this press is consumed
    CHECK(ui.IsPopupOpen("wares") && !ui.WasPopupClosed("wares") && !left);
    Frame(ui, describe, Press(&UiInput::Back)); // a second Back while it is still fading
    CHECK(ui.IsPopupOpen("wares") && !left);
    int frames = 0;
    while (ui.IsPopupOpen("wares") && frames < 30) {
        Frame(ui, describe);
        ++frames;
    }
    CHECK(frames > 1); // it faded, rather than snapping shut on the second Back
    CHECK(!ui.IsPopupOpen("wares") && ui.WasPopupClosed("wares") && !left);
    CHECK(ui.GetFocusedId() == ui.GetId("Shop"));
    Frame(ui, describe, Press(&UiInput::Back));
    CHECK(left); // the screen hears Back only once the pop-up is gone
}

TEST(UiPopupAnchorsUnderTheOpenerOrInTheCenter)
{
    Ui ui;
    InstantPopups(ui);
    const auto describe = [&] {
        ui.BeginPanel("##shop", {.Position = {40.0f, 50.0f}, .Width = 200.0f});
        if (ui.Button("Shop"))
            ui.OpenPopup("##list");
        ui.EndPanel();
        if (ui.BeginPopup("##list", PopupBox(UiPopupAnchor::Opener))) {
            ui.Button("One");
            ui.EndPopup();
        }
    };
    Frame(ui, describe, Press(&UiInput::Accept));
    const UiItem* popup = nullptr;
    for (const UiItem& item : ui.GetItems())
        if (item.Kind == UiKind::Panel && item.Scope != 0)
            popup = &item;
    CHECK(popup != nullptr);
    // Shop button: panel (40, 50), padding 12, so the row is (52, 62) and 28 tall. Gap is 4.
    if (popup)
        CHECK(popup->Rect.Position == Vec2(52.0f, 94.0f));

    Ui centered;
    InstantPopups(centered);
    const auto describeCenter = [&] {
        if (centered.BeginPopup("##list", PopupBox())) {
            centered.Button("One");
            centered.EndPopup();
        }
    };
    centered.OpenPopup("##list");
    Frame(centered, describeCenter); // height not known yet
    Frame(centered, describeCenter);
    const UiItem* box = nullptr;
    for (const UiItem& item : centered.GetItems())
        if (item.Kind == UiKind::Panel)
            box = &item;
    CHECK(box != nullptr);
    // 180 wide, padding 12 + one row 28 + padding 12 = 52 tall, centered in 400 x 300.
    if (box) {
        CHECK(box->Rect.Position == Vec2(110.0f, 124.0f));
        CHECK(FindKind(centered, UiKind::Backdrop, box->Scope) != nullptr);
    }
}

TEST(UiPopupPromptBarUsesTheBindingGlyphs)
{
    Ui ui;
    InstantPopups(ui);
    UiInput input;
    input.AcceptPrompt = "A";
    input.BackPrompt = "B";
    const auto describe = [&] {
        if (ui.BeginPopup("list", {.Width = 200.0f, .Screen = {400.0f, 300.0f}})) {
            ui.Button("One");
            ui.EndPopup();
        }
    };
    ui.OpenPopup("list");
    Frame(ui, describe, input);
    const UiItem* prompt = FindKind(ui, UiKind::Prompt, 0);
    CHECK(prompt != nullptr);
    CHECK(prompt->Text == "A  Confirm      B  Back");
}

TEST(UiPopupFadesWithTheStyleTimes)
{
    Ui ui;
    ui.GetStyle().PopupOpenSeconds = 1.0f;
    ui.GetStyle().PopupCloseSeconds = 0.0f;
    ui.OpenPopup("list");
    const auto describe = [&] {
        ui.BeginPanel("##screen", {.Position = {10.0f, 10.0f}, .Width = 120.0f});
        ui.Button("Shop");
        ui.EndPanel();
        if (ui.BeginPopup("list", PopupBox())) {
            ui.Button("One");
            ui.EndPopup();
        }
    };
    ui.Begin({}, 0.25f);
    describe();
    ui.End();
    const UiItem* panel = nullptr;
    for (const UiItem& item : ui.GetItems())
        if (item.Kind == UiKind::Panel)
            panel = &item;
    CHECK(panel != nullptr);
    if (panel)
        CHECK(panel->Alpha == 0.25f);                // a quarter of PopupOpenSeconds
    CHECK(Find(ui, ui.GetId("Shop")).Alpha == 1.0f); // the screen under it does not fade
    ui.Begin({}, 1.0f);
    describe();
    ui.End();
    for (const UiItem& item : ui.GetItems())
        if (item.Kind == UiKind::Panel)
            panel = &item;
    if (panel)
        CHECK(panel->Alpha == 1.0f);
}

TEST(UiStyleLoadsFromJsonAndRejectsABadFile)
{
    UiStyle style;
    CHECK(style.PopupMaxRows == 8);
    CHECK(!LoadUiStyle(style, "{"));
    CHECK(style.PopupMaxRows == 8); // a broken file changes nothing
    const char* json = R"({
        "maxVisibleRows": 5,
        "openSeconds": 0.2,
        "closeSeconds": 0,
        "rowHeight": 32,
        "promptConfirm": "Choose",
        "backdrop": [0, 0, 0, 0.4],
        "text": [1, 1, 1]
    })";
    CHECK(LoadUiStyle(style, json));
    CHECK(style.PopupMaxRows == 5);
    CHECK_NEAR(style.PopupOpenSeconds, 0.2f);
    CHECK(style.PopupCloseSeconds == 0.0f);
    CHECK(style.RowHeight == 32.0f);
    CHECK(style.PromptConfirm == "Choose");
    CHECK(style.PromptCancel == "Back"); // absent key: the default stays
    CHECK(style.Spacing == 6.0f);
    CHECK_NEAR(style.PopupBackdrop.w, 0.4f);
    CHECK(style.Text.w == 1.0f); // three components: opaque
    CHECK(!LoadUiStyle(style, R"({"maxVisibleRows": "lots"})"));
    CHECK(style.PopupMaxRows == 5);
    CHECK(!LoadUiStyleFile(style, "no/such/popup.json"));
    CHECK(style.PopupMaxRows == 5);
}
