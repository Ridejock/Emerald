#pragma once

#include <filesystem>
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
// A pop-up (a dropdown or a confirm) opens over that screen and takes the focus until it closes:
//
//   if (ui.Button("Buy"))
//       ui.OpenPopup("wares");
//   if (ui.BeginPopup("wares", {.Width = 280.0f, .Anchor = UiPopupAnchor::Opener,
//                               .Screen = viewSize})) {
//       for (const std::string_view item : wares)
//           if (ui.Button(item)) ui.OpenPopup("confirm");
//       ui.EndPopup();
//   }
//   if (ui.BeginPopup("confirm", {.Width = 260.0f, .Screen = viewSize})) {
//       ui.Label("Buy it?");
//       if (ui.Button("Yes")) { Buy(); ui.ClosePopup(); }
//       if (ui.Button("No")) ui.ClosePopup();
//       ui.EndPopup();
//   }
//
// Back or a click outside closes the top one and the focus returns to whatever opened it. A list
// longer than UiStyle::PopupMaxRows scrolls, with a bar on the right. Sizes, colors and the open
// and close times are UiStyle (LoadUiStyle reads them from JSON). The line under the list is the
// same PromptBar a panel can draw: the glyphs come from the UiAccept / UiBack bindings.
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
    // What PromptBar prints for Accept and Back. ReadUiInput fills them from the bindings
    // (a pad button when a pad is connected, otherwise the first key). Tests set them directly.
    std::string AcceptPrompt;
    std::string BackPrompt;
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
// On a Steam Deck those are the d-pad, the left stick, A and B (Xbox positions, as printed).
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
    // Pop-ups. PopupMaxRows is how many list rows show before the rest scrolls. The open and
    // close times fade the pop-up; 0 snaps. PopupGap is the space between an anchored pop-up and
    // the widget that opened it. PopupScrollbar is the scroll indicator's width (0 hides it).
    i32 PopupMaxRows = 8;
    f32 PopupOpenSeconds = 0.12f;
    f32 PopupCloseSeconds = 0.10f;
    f32 PopupGap = 4.0f;
    f32 PopupScrollbar = 6.0f;
    Vec4 PopupBackdrop{0.0f, 0.02f, 0.03f, 0.55f};
    Vec4 PopupScroll{0.18f, 0.8f, 0.44f, 1.0f}; // the scroll indicator's thumb
    Vec4 PopupScrollTrack{1.0f, 1.0f, 1.0f, 0.16f};
    // PromptBar's words, after the glyph from the binding ("A  Confirm"). Empty hides that word.
    std::string PromptConfirm = "Confirm";
    std::string PromptCancel = "Back";
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

// Center puts the pop-up in the middle of Screen. Opener puts it under the widget that opened it
// (above, when there is no room below), and falls back to Center when that widget has no rect.
enum class UiPopupAnchor : u8 { Center, Opener };

// Screen is the UI space in units (the same size the projection uses). It places a centered
// pop-up, keeps an anchored one on screen, and sizes the dimmed backdrop. (0, 0) skips all three.
// MaxRows 0 uses UiStyle::PopupMaxRows. Prompt adds the prompt bar under the list.
struct UiPopupOptions {
    f32 Width = 280.0f;
    UiPopupAnchor Anchor = UiPopupAnchor::Center;
    Vec2 Screen{};
    bool Dim = true;
    bool Prompt = true;
    i32 MaxRows = 0;
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

enum class UiKind : u8 {
    Panel,
    Label,
    Button,
    Toggle,
    Slider,
    Choice,
    Backdrop,  // the dimmed full-screen rect behind a pop-up
    Scrollbar, // track (Amount 0) or thumb (Amount 1)
    Prompt     // the Accept / Back prompt bar
};

// One widget as described this frame: what Draw renders.
struct UiItem {
    UiKind Kind = UiKind::Label;
    u32 Id = 0;
    u32 Scope = 0; // 0 on the screen under the pop-ups; otherwise the pop-up's id
    UiRect Rect;
    std::string Text;                  // the label (without its ##id part) or the panel's title
    std::string Value;                 // a choice's current option
    f32 Amount = 0.0f;                 // a slider's position (0..1) or a toggle's state (0 / 1)
    f32 Focus = 0.0f;                  // 0..1, eased towards 1 while focused
    f32 Alpha = 1.0f;                  // pop-up open / close fade
    bool Held = false;                 // being pressed or dragged
    bool Shown = true;                 // false: scrolled out of a pop-up's window (not drawn)
    TextAlign Align = TextAlign::Left; // labels
};

// Reads the popup and prompt fields (and the shared look: rows, colors, text scale) from JSON
// text. Keys that are present replace `style`; keys that are absent stay as they are. False when
// the text is not a JSON object or a present value has the wrong type, and `style` is unchanged.
// A sprite (`PanelSprite`) is never read from the file.
[[nodiscard]] bool LoadUiStyle(UiStyle& style, std::string_view json);
// The same, from a file. False (and `style` unchanged) when the file cannot be read or the text
// is refused.
[[nodiscard]] bool LoadUiStyleFile(UiStyle& style, const std::filesystem::path& path);

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
    // One row, not focusable: the Accept and Back glyphs (UiInput) and PromptConfirm /
    // PromptCancel. Nothing is drawn when both glyphs and both words are empty. A pop-up draws
    // one of these under its list unless UiPopupOptions::Prompt is false.
    void PromptBar();

    // --- Pop-ups: open over the screen, own the focus, stack, scroll ---
    // Opens `id` on top and remembers the focused widget (the focus returns there when this
    // pop-up closes). The Accept press that opened it is consumed, so the new list's first row
    // is not activated on the same frame. Call BeginPopup later in the same frame to describe it.
    // An id that is already open is left as it is. Ids follow PushId, like widget ids, and the
    // part before "##" is the title ("Wares##shop" shows "Wares").
    void OpenPopup(std::string_view id);
    // True while `id` is open (including while it fades out): describe its rows, then EndPopup.
    // False when it is closed. Only the top pop-up takes input; the ones under it still draw.
    // Describing a pop-up inside another is refused (call EndPopup first).
    bool BeginPopup(std::string_view id, const UiPopupOptions& options = {});
    void EndPopup();
    // Closes the top pop-up (it fades out over PopupCloseSeconds, or leaves at once when that is
    // 0). Back and a click outside do this too.
    void ClosePopup();
    [[nodiscard]] bool IsPopupOpen(std::string_view id) const;
    [[nodiscard]] u32 GetPopupCount() const { return static_cast<u32>(m_Popups.size()); }
    // How far the top pop-up's list is scrolled, in UI units (0 at the top). 0 when none is open.
    [[nodiscard]] f32 GetPopupScroll() const;
    // True after `id` finished closing, until the next Begin.
    [[nodiscard]] bool WasPopupClosed(std::string_view id) const;

    // --- Ids ---
    void PushId(std::string_view id);
    void PopId();
    // The id a widget with this label gets here (inside the current PushId scopes).
    [[nodiscard]] u32 GetId(std::string_view label) const;

    // --- Focus and results ---
    [[nodiscard]] u32 GetFocusedId() const { return m_Focused; }
    void SetFocus(u32 id) { m_Focused = id; } // e.g. GetId("Play") when a menu opens
    // Back was pressed this frame and no pop-up took it. A pop-up closes itself on Back, and keeps
    // taking Back while it fades out, so the screen underneath does not also leave.
    [[nodiscard]] bool WasBackPressed() const { return m_Input.Back.Pressed && !m_BackConsumed; }

    // --- Drawing ---
    void Draw(Renderer2D& r, const Font& font) const;
    [[nodiscard]] const std::vector<UiItem>& GetItems() const { return m_Items; }
    [[nodiscard]] UiStyle& GetStyle() { return *m_Style; }
    [[nodiscard]] const UiStyle& GetStyle() const { return *m_Style; }

private:
    struct State {
        f32 Focus = 0.0f;  // the highlight's fade
        f32 Height = 0.0f; // panels: the content's height last frame
        u64 Frame = 0;     // last frame it was seen (unseen ones are dropped)
    };
    enum Dir : u8 { kUp, kDown, kLeft, kRight };

    // One open pop-up. The list under it keeps its scroll and the widget to return to.
    struct Popup {
        u32 Id = 0;
        u32 Opener = 0;
        UiRect OpenerRect{};
        bool HasOpener = false;
        f32 Scroll = 0.0f;
        f32 Age = 0.0f;
        f32 CloseAge = 0.0f;
        f32 LastHeight = 0.0f;
        f32 ContentTop = 0.0f;
        f32 ContentHeight = 0.0f;
        f32 ViewHeight = 0.0f;
        i32 Rows = 0;
        i32 MaxRows = 8;
        bool Begun = false;
        bool Closing = false;
        bool EatClick = false; // the click that opened it must not also close it
        bool Dim = true;
        UiPopupAnchor Anchor = UiPopupAnchor::Center;
        Vec2 Screen{};
        UiRect Panel{};
        usize Track = static_cast<usize>(-1); // this frame's scrollbar items, or npos
        usize Thumb = static_cast<usize>(-1);
    };

    // The panel layout a pop-up interrupts, restored by EndPopup.
    struct Layout {
        bool InPanel = false;
        usize Panel = 0;
        UiRect Area{};
        f32 CursorY = 0.0f;
        u32 Columns = 1;
        u32 Column = 0;
    };

    // Adds a widget row; returns its item (with Focus and the hover / click state updated).
    UiItem& Add(UiKind kind, std::string_view label, bool focusable);
    // Appends an item. `localY` is its place in a pop-up list (negative: not a scrolled row).
    UiItem& Emplace(f32 localY);
    [[nodiscard]] UiRect NextRect();
    // This frame's directions (presses and repeats) and whether a widget used them.
    [[nodiscard]] bool Nav(Dir d) const { return m_Nav[d]; }
    void UseNav(Dir d) { m_Nav[d] = false; }
    // Clicked: pressed and released over the item (or Accept while focused).
    [[nodiscard]] bool Activated(const UiItem& item) const;
    void MoveFocus(Dir d);
    [[nodiscard]] u32 MakePopupId(std::string_view id) const;
    [[nodiscard]] Popup* FindPopup(u32 id);
    [[nodiscard]] const Popup* FindPopup(u32 id) const;
    // The widget being described may take focus and clicks (only the top pop-up, or the screen
    // when none is open).
    [[nodiscard]] bool AcceptsInput() const;
    [[nodiscard]] bool InActiveScope(const UiItem& item) const;
    [[nodiscard]] static bool IsNavigable(UiKind kind);
    // Back or a click outside the top pop-up starts its close. A pop-up nobody described closes
    // too, so a forgotten one cannot hold the focus.
    void ApplyPopupInput();
    // Drops pop-ups whose close has finished and gives the focus back to their opener.
    void FinishClosing();
    // Scrolls the top list so the focus is visible, then places every pop-up's rows in its window.
    void LayoutPopups();
    [[nodiscard]] std::string PromptText() const;
    [[nodiscard]] f32 PopupAlpha(const Popup& popup) const;

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
    std::vector<f32> m_LocalY; // parallel to m_Items; < 0 when the row does not scroll
    std::vector<u32> m_IdStack;
    std::unordered_map<u32, State> m_States;
    std::vector<Popup> m_Popups;
    std::vector<u32> m_Closed; // pop-ups that finished closing since the last Begin
    u32 m_Focused = 0;
    u32 m_Pressed = 0; // the item the click went down on (0 = none)
    bool m_BackConsumed = false;
    // The current panel: its item, the next row's top and the columns left in a Columns row.
    usize m_Panel = 0;
    bool m_InPanel = false;
    UiRect m_Area;
    f32 m_CursorY = 0.0f;
    u32 m_Columns = 1;
    u32 m_Column = 0;
    // The pop-up being described (0 is the screen). m_ContentY is the next list row's local y.
    u32 m_PopupScope = 0;
    bool m_InPopup = false;
    bool m_WantPrompt = false;
    f32 m_ContentTop = 0.0f;
    f32 m_ContentY = 0.0f;
    i32 m_ContentRows = 0;
    Layout m_Saved{};
};

// A sprite stretched over `rect` with its corners kept at their size (`border` texture pixels,
// drawn `scale` times larger) and its edges and middle stretched: panel and button frames.
void DrawNineSlice(Renderer2D& r, const Sprite& sprite, const UiRect& rect, f32 border,
                   f32 scale = 1.0f, const Vec4& tint = Vec4(1.0f));

} // namespace Emerald
