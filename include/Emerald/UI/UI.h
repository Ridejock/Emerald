#pragma once

#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Font.h"
#include "Emerald/Renderer/Sprite.h"

namespace Emerald {

class Input;
class Renderer2D;

// Immediate-mode game UI (menus, options screens) driven by mouse, keyboard and gamepad:
//
//   // Every frame (OnUpdate): describe the UI; widgets return true when they were used.
//   ui.Begin(ReadUiInput(input, mouse), dt);
//   ui.BeginPanel("OPTIONS", {.Position = size * 0.5f, .Width = 320.0f, .Pivot = {0.5f, 0.5f}});
//   ui.Slider("Music", &settings.Music, 0.0f, 1.0f);
//   ui.Toggle("Fullscreen", &settings.Fullscreen);
//   ui.Choice("Window", &settings.Scale, {"x1", "x2", "x3"});
//   if (ui.Button("Back") || ui.WasBackPressed()) Close();
//   ui.EndPanel();
//   ui.End();
//
//   // Every frame (OnRender2D, between Begin and End of a pixel-space projection):
//   ui.Draw(r, font);
//
// Widgets are identified by their label (hashed), so the per-widget state (focus highlight,
// slider drag) survives between frames. Two widgets with the same label need different ids:
// "Volume##music" shows "Volume" but hashes the whole string, and PushId / PopId scope labels
// (e.g. inside a loop). Describing and drawing are separate: the description builds a list of
// items with their rectangles (GetItems), which Draw renders and the tests check without a GPU.
//
// Focus: there is always one focused widget (the first one at the start). Up / Down / Left / Right
// move it to the nearest widget in that direction, by the widgets' rectangles, wrapping around at
// the edges. Sliders, toggles and choices use Left / Right to change their value instead, so in
// a vertical menu Left / Right adjust and Up / Down move. Accept presses the focused widget. The
// mouse focuses what it moves over and clicks it. Directions held down repeat (Style::Repeat*).
// Units are whatever the projection uses (e.g. virtual pixels); rectangles are whole units.

// One button's state this frame.
struct UiButton {
    bool Held = false;     // down now
    bool Pressed = false;  // went down since the last frame
    bool Released = false; // went up since the last frame
};

// What the UI reads each frame. ReadUiInput fills it from actions; tests fill it by hand.
struct UiInput {
    UiButton Up, Down, Left, Right, Accept, Back;
    UiButton Click; // the mouse button that clicks widgets
    Vec2 Mouse{};   // the cursor in UI units
    bool MouseMoved = false;
};

// The action names ReadUiInput uses. Bind them once (BindDefaultUiActions does, with the usual
// keys and pad buttons) and rebind them like any other action (Input::RebindAction).
inline constexpr std::string_view kUiUp = "UiUp";
inline constexpr std::string_view kUiDown = "UiDown";
inline constexpr std::string_view kUiLeft = "UiLeft";
inline constexpr std::string_view kUiRight = "UiRight";
inline constexpr std::string_view kUiAccept = "UiAccept";
inline constexpr std::string_view kUiBack = "UiBack";
// Optional axes (e.g. a stick) that also move the focus when pushed past half way.
inline constexpr std::string_view kUiMoveX = "UiMoveX";
inline constexpr std::string_view kUiMoveY = "UiMoveY";

// Arrows / WASD, Enter / Space, Escape / Backspace; d-pad and left stick, South / East.
void BindDefaultUiActions(Input& input);
// The UI actions now (nothing while the input is blocked) and the left mouse button. `mouse` is
// the cursor in UI units (e.g. Mouse::GetPosition() scaled to the virtual resolution).
[[nodiscard]] UiInput ReadUiInput(const Input& input, const Vec2& mouse);

// How the UI looks and repeats. Sizes are in UI units, durations in seconds. (Vec4 colors are
// 16-byte aligned, so the struct is padded: MSVC's C4324 is switched off for it, as for
// SpriteOptions.)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct UiStyle {
    f32 RowHeight = 28.0f; // every widget row
    f32 Spacing = 6.0f;    // between rows and between columns
    f32 Padding = 12.0f;   // inside panels
    f32 TextScale = 1.0f;  // the font's baked size times this
    Vec4 Text{0.85f, 0.9f, 0.9f, 1.0f};
    Vec4 TextFocused{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 Title{0.35f, 0.95f, 0.55f, 1.0f};
    Vec4 PanelFill{0.05f, 0.1f, 0.12f, 0.92f};
    Vec4 PanelBorder{0.18f, 0.8f, 0.44f, 1.0f};
    Vec4 Widget{0.12f, 0.2f, 0.22f, 1.0f};    // button and slider track background
    Vec4 Highlight{0.18f, 0.8f, 0.44f, 1.0f}; // the focused row and active parts
    // Optional 9-slice panel sprite (DrawNineSlice) instead of PanelFill / PanelBorder; its
    // corners are PanelSliceBorder texture pixels, drawn at PanelSliceScale.
    const Sprite* PanelSprite = nullptr;
    f32 PanelSliceBorder = 4.0f;
    f32 PanelSliceScale = 1.0f;
    f32 FocusSpeed = 12.0f; // how fast the highlight fades in and out (per second)
    f32 RepeatDelay = 0.4f; // a held direction repeats after this...
    f32 RepeatRate = 0.08f; // ...every this
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Where a panel goes. Position is the pivot's place; Pivot (0..1 of the panel's size) says which
// point of the panel that is: (0, 0) top-left, (0.5, 0.5) centered. The height follows the
// content (the previous frame's, so a centered panel does not jump).
struct UiPanelOptions {
    Vec2 Position{};
    f32 Width = 300.0f;
    Vec2 Pivot{};
};

// A rectangle in UI units.
struct UiRect {
    Vec2 Position{}; // top-left
    Vec2 Size{};
    [[nodiscard]] Vec2 GetCenter() const { return Position + Size * 0.5f; }
    [[nodiscard]] bool Contains(const Vec2& p) const
    {
        return p.x >= Position.x && p.y >= Position.y && p.x < Position.x + Size.x &&
               p.y < Position.y + Size.y;
    }
};

enum class UiKind : u8 { Panel, Label, Button, Toggle, Slider, Choice };

// One widget as described this frame: what Draw renders.
struct UiItem {
    UiKind Kind = UiKind::Label;
    u32 Id = 0;
    UiRect Rect;
    std::string Text;                  // the label (without its ##id part) or the panel's title
    std::string Value;                 // a choice's current option
    f32 Amount = 0.0f;                 // a slider's position (0..1) or a toggle's state (0 / 1)
    f32 Focus = 0.0f;                  // 0..1, eased towards 1 while focused
    bool Held = false;                 // being pressed or dragged
    TextAlign Align = TextAlign::Left; // labels
};

class Ui {
public:
    explicit Ui(const UiStyle& style = {}) : m_Style(std::make_unique<UiStyle>(style)) {}

    // --- A frame: Begin, panels and widgets, End; then Draw (any time before the next Begin) ---
    void Begin(const UiInput& input, f32 dt);
    void End();

    // --- Layout: widgets stack downwards inside a panel, one row each ---
    void BeginPanel(std::string_view title, const UiPanelOptions& options);
    void EndPanel();
    // The next `columns` widgets share one row, side by side with equal widths.
    void Columns(u32 columns);
    void Space(f32 height); // an empty gap

    // --- Widgets ---
    void Label(std::string_view text, TextAlign align = TextAlign::Left); // not focusable
    // True on the frame it is clicked or Accept is pressed on it.
    bool Button(std::string_view label);
    // Flips *value on click / Accept / Left / Right; true when it changed.
    bool Toggle(std::string_view label, bool* value);
    // *value in [min, max]: Left / Right move it by `step`, the mouse drags it; true when it
    // changed.
    bool Slider(std::string_view label, f32* value, f32 min, f32 max, f32 step = 0.1f);
    // *index into `options`: Left / Right / Accept / click step through them, wrapping; true when
    // it changed.
    bool Choice(std::string_view label, i32* index, std::span<const std::string_view> options);
    bool Choice(std::string_view label, i32* index, std::initializer_list<std::string_view> options)
    {
        return Choice(label, index, std::span(options.begin(), options.size()));
    }

    // --- Ids ---
    void PushId(std::string_view id);
    void PopId();
    // The id a widget with this label gets here (inside the current PushId scopes).
    [[nodiscard]] u32 GetId(std::string_view label) const;

    // --- Focus and results ---
    [[nodiscard]] u32 GetFocusedId() const { return m_Focused; }
    void SetFocus(u32 id) { m_Focused = id; } // e.g. GetId("Play") when a menu opens
    // Back was pressed this frame (the game decides what it means: close, go up a level).
    [[nodiscard]] bool WasBackPressed() const { return m_Input.Back.Pressed; }

    // --- Drawing ---
    void Draw(Renderer2D& r, const Font& font) const;
    [[nodiscard]] const std::vector<UiItem>& GetItems() const { return m_Items; }
    [[nodiscard]] UiStyle& GetStyle() { return *m_Style; }

private:
    struct State {
        f32 Focus = 0.0f;  // the highlight's fade
        f32 Height = 0.0f; // panels: the content's height last frame
        u64 Frame = 0;     // last frame it was seen (unseen ones are dropped)
    };
    enum Dir : u8 { kUp, kDown, kLeft, kRight };

    // Adds a widget row; returns its item (with Focus and the hover / click state updated).
    UiItem& Add(UiKind kind, std::string_view label, bool focusable);
    [[nodiscard]] UiRect NextRect();
    // This frame's directions (presses and repeats) and whether a widget used them.
    [[nodiscard]] bool Nav(Dir d) const { return m_Nav[d]; }
    void UseNav(Dir d) { m_Nav[d] = false; }
    // Clicked: pressed and released over the item (or Accept while focused).
    [[nodiscard]] bool Activated(const UiItem& item) const;
    void MoveFocus(Dir d);

    // On the heap: its Vec4s are 16-byte aligned, which would pad Ui and every class holding
    // one (MSVC C4324).
    std::unique_ptr<UiStyle> m_Style;
    UiInput m_Input;
    f32 m_Dt = 0.0f;
    u64 m_Frame = 0;
    bool m_Nav[4] = {};
    bool m_Held[4] = {};  // each direction last frame
    f32 m_Repeat[4] = {}; // seconds until a held direction repeats
    std::vector<UiItem> m_Items;
    std::vector<u32> m_IdStack;
    std::unordered_map<u32, State> m_States;
    u32 m_Focused = 0;
    u32 m_Pressed = 0; // the item the click went down on (0 = none)
    // The current panel: its item, the next row's top and the columns left in a Columns row.
    usize m_Panel = 0;
    bool m_InPanel = false;
    UiRect m_Area;
    f32 m_CursorY = 0.0f;
    u32 m_Columns = 1;
    u32 m_Column = 0;
};

// A sprite stretched over `rect` with its corners kept at their size (`border` texture pixels,
// drawn `scale` times larger) and its edges and middle stretched: panel and button frames.
void DrawNineSlice(Renderer2D& r, const Sprite& sprite, const UiRect& rect, f32 border,
                   f32 scale = 1.0f, const Vec4& tint = Vec4(1.0f));

} // namespace Emerald
