#include "Emerald/UI/UI.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include "Emerald/Core/Log.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Input/InputBindings.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

namespace {

constexpr u32 kFnvBasis = 2166136261u;
constexpr u32 kFnvPrime = 16777619u;
// Parallel to Ui::m_Items: a scrolled row's local y is >= 0. Anything else is not in the list.
constexpr f32 kNoLocal = -1.0f;
constexpr usize kNoItem = static_cast<usize>(-1);

using Json = nlohmann::json;

// FNV-1a, continuing from `hash` (so ids hash the PushId scopes and then the label).
u32 Hash(std::string_view text, u32 hash)
{
    for (const char c : text)
        hash = (hash ^ static_cast<u8>(c)) * kFnvPrime;
    return hash;
}

// "Volume##music" shows "Volume".
std::string_view DisplayText(std::string_view label)
{
    return label.substr(0, label.find("##"));
}

UiButton ReadAction(const Input& input, std::string_view action)
{
    return {input.IsActionDown(action), input.WasActionPressed(action),
            input.WasActionReleased(action)};
}

Vec4 Mix(const Vec4& a, const Vec4& b, f32 t)
{
    return a + (b - a) * t;
}

Vec4 Fade(Vec4 color, f32 alpha)
{
    color.w *= alpha;
    return color;
}

// The glyph PromptBar shows: a connected pad's button when one is plugged in (so a Deck shows
// A / B), otherwise the first bound key, otherwise the mouse button.
std::string ActionPrompt(const Input& input, std::string_view action)
{
    const auto buttons = input.GetActionButtons(action);
    if (input.GetGamepads().GetCount() > 0 && !buttons.empty()) {
        const char* label = input.GetGamepads().GetButtonLabel(buttons.front());
        if (label && *label)
            return label;
    }
    const auto keys = input.GetActionKeys(action);
    if (!keys.empty())
        return GetKeyLabel(keys.front());
    const auto mouse = input.GetActionMouseButtons(action);
    if (!mouse.empty())
        return std::string(GetMouseButtonName(mouse.front()));
    return {};
}

void WarnStyle(std::string_view message)
{
    if (Log::Core())
        EM_CORE_WARN("UiStyle: {}", message);
}

// Applies the keys that are present. The first wrong type is remembered and nothing is copied
// out, so a bad file leaves the caller's style alone.
class StyleReader {
public:
    explicit StyleReader(const Json& root) : m_Root(root) {}
    std::string Error;

    void Number(const char* key, f32& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_number())
            Fail(key, "a number");
        else
            out = value->get<f32>();
    }

    void Whole(const char* key, i32& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_number() || value->get<f32>() < 0.0f)
            Fail(key, "a whole number >= 0");
        else
            out = static_cast<i32>(std::lround(value->get<f32>()));
    }

    void Color(const char* key, Vec4& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_array() || value->size() < 3 || value->size() > 4) {
            Fail(key, "[r, g, b] or [r, g, b, a]");
            return;
        }
        f32 rgba[4] = {out.x, out.y, out.z, out.w};
        for (usize i = 0; i < value->size(); ++i) {
            if (!(*value)[i].is_number()) {
                Fail(key, "a list of numbers");
                return;
            }
            rgba[i] = (*value)[i].get<f32>();
        }
        if (value->size() == 3)
            rgba[3] = 1.0f;
        out = {rgba[0], rgba[1], rgba[2], rgba[3]};
    }

    void Text(const char* key, std::string& out)
    {
        const Json* value = Find(key);
        if (!value)
            return;
        if (!value->is_string())
            Fail(key, "a string");
        else
            out = value->get<std::string>();
    }

private:
    const Json* Find(const char* key) const
    {
        const auto it = m_Root.find(key);
        return it == m_Root.end() ? nullptr : &(*it);
    }

    void Fail(const char* key, const char* expected)
    {
        if (Error.empty())
            Error = std::string(key) + " should be " + expected;
    }

    const Json& m_Root;
};

} // namespace

void BindDefaultUiActions(Input& input)
{
    input.BindAction(kUiUp, {Key::Up, Key::W});
    input.BindAction(kUiUp, {GamepadButton::DPadUp});
    input.BindAction(kUiDown, {Key::Down, Key::S});
    input.BindAction(kUiDown, {GamepadButton::DPadDown});
    input.BindAction(kUiLeft, {Key::Left, Key::A});
    input.BindAction(kUiLeft, {GamepadButton::DPadLeft});
    input.BindAction(kUiRight, {Key::Right, Key::D});
    input.BindAction(kUiRight, {GamepadButton::DPadRight});
    input.BindAction(kUiAccept, {Key::Enter, Key::Space});
    input.BindAction(kUiAccept, {GamepadButton::South});
    input.BindAction(kUiBack, {Key::Escape, Key::Backspace});
    input.BindAction(kUiBack, {GamepadButton::East});
    input.BindAxis(kUiMoveX, GamepadAxis::LeftX);
    input.BindAxis(kUiMoveY, GamepadAxis::LeftY); // +1 = down, like the screen
}

UiInput ReadUiInput(const Input& input, const Vec2& mouse)
{
    UiInput in;
    in.Up = ReadAction(input, kUiUp);
    in.Down = ReadAction(input, kUiDown);
    in.Left = ReadAction(input, kUiLeft);
    in.Right = ReadAction(input, kUiRight);
    in.Accept = ReadAction(input, kUiAccept);
    in.Back = ReadAction(input, kUiBack);
    // A stick pushed past half way holds the direction (Ui turns that into presses and repeats).
    in.Left.Held |= input.GetAxis(kUiMoveX) < -0.5f;
    in.Right.Held |= input.GetAxis(kUiMoveX) > 0.5f;
    in.Up.Held |= input.GetAxis(kUiMoveY) < -0.5f;
    in.Down.Held |= input.GetAxis(kUiMoveY) > 0.5f;
    in.Mouse = mouse;
    if (!input.IsBlocked()) { // the raw mouse is not blocked with the actions
        const Mouse& m = input.GetMouse();
        in.Click = {m.IsButtonDown(MouseButton::Left), m.WasButtonPressed(MouseButton::Left),
                    m.WasButtonReleased(MouseButton::Left)};
        in.MouseMoved = m.HasMoved();
    }
    in.AcceptPrompt = ActionPrompt(input, kUiAccept);
    in.BackPrompt = ActionPrompt(input, kUiBack);
    return in;
}

bool LoadUiStyle(UiStyle& style, std::string_view json)
{
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const Json::exception& error) {
        WarnStyle(error.what());
        return false;
    }
    if (!root.is_object()) {
        WarnStyle("expected an object");
        return false;
    }
    UiStyle next = style;
    StyleReader reader(root);
    reader.Number("rowHeight", next.RowHeight);
    reader.Number("spacing", next.Spacing);
    reader.Number("padding", next.Padding);
    reader.Number("textScale", next.TextScale);
    reader.Number("focusSpeed", next.FocusSpeed);
    reader.Number("repeatDelay", next.RepeatDelay);
    reader.Number("repeatRate", next.RepeatRate);
    reader.Color("text", next.Text);
    reader.Color("textFocused", next.TextFocused);
    reader.Color("title", next.Title);
    reader.Color("panelFill", next.PanelFill);
    reader.Color("panelBorder", next.PanelBorder);
    reader.Color("widget", next.Widget);
    reader.Color("highlight", next.Highlight);
    reader.Whole("maxVisibleRows", next.PopupMaxRows);
    reader.Number("openSeconds", next.PopupOpenSeconds);
    reader.Number("closeSeconds", next.PopupCloseSeconds);
    reader.Number("popupGap", next.PopupGap);
    reader.Number("scrollbar", next.PopupScrollbar);
    reader.Color("backdrop", next.PopupBackdrop);
    reader.Color("scroll", next.PopupScroll);
    reader.Color("scrollTrack", next.PopupScrollTrack);
    reader.Text("promptConfirm", next.PromptConfirm);
    reader.Text("promptCancel", next.PromptCancel);
    if (!reader.Error.empty()) {
        WarnStyle(reader.Error);
        return false;
    }
    style = std::move(next);
    return true;
}

bool LoadUiStyleFile(UiStyle& style, const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        WarnStyle("could not read " + path.string());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LoadUiStyle(style, text);
}

// --- Frame ---

void Ui::Begin(const UiInput& input, f32 dt)
{
    // A press, or a hold that just started (sticks only report holds), fires at once; a hold
    // fires again after RepeatDelay, then every RepeatRate.
    const UiButton* dirs[4] = {&input.Up, &input.Down, &input.Left, &input.Right};
    for (usize d = 0; d < 4; ++d) {
        m_Nav[d] = dirs[d]->Pressed || (dirs[d]->Held && !m_Held[d]);
        m_Held[d] = dirs[d]->Held;
        if (m_Nav[d]) {
            m_Repeat[d] = m_Style->RepeatDelay;
        } else if (dirs[d]->Held) {
            m_Repeat[d] -= dt;
            if (m_Repeat[d] <= 0.0f) {
                m_Nav[d] = true;
                m_Repeat[d] = Max(m_Repeat[d] + m_Style->RepeatRate, 0.0f);
            }
        }
    }
    m_Input = input;
    m_Dt = dt;
    ++m_Frame;
    m_BackConsumed = false;
    m_Closed.clear();
    // Back or a click outside closes a pop-up that was already open. Doing it here, before the
    // screen is described, means WasBackPressed is false for the rest of the frame and the click
    // is not also a press on whatever sits underneath.
    for (Popup& popup : m_Popups) {
        popup.Begun = false;
        popup.Track = kNoItem;
        popup.Thumb = kNoItem;
    }
    ApplyPopupInput();
    FinishClosing();
    m_Items.clear();
    m_LocalY.clear();
    m_IdStack.clear();
    m_InPanel = false;
    m_InPopup = false;
    m_PopupScope = 0;
    m_WantPrompt = false;
    m_Area = {{0.0f, 0.0f}, {300.0f, 0.0f}}; // widgets outside a panel
    m_CursorY = 0.0f;
    m_Columns = 1;
    m_Column = 0;
}

void Ui::End()
{
    // Opened and never described: close it, or it would hold the focus with nothing on screen.
    for (Popup& popup : m_Popups) {
        if (!popup.Begun && !popup.Closing) {
            popup.Closing = true;
            popup.CloseAge = 0.0f;
        }
    }
    ApplyPopupInput();
    for (Popup& popup : m_Popups) {
        popup.Age += m_Dt;
        if (popup.Closing)
            popup.CloseAge += m_Dt;
    }
    FinishClosing();
    // Keep the focus on a widget that exists in the active scope (the top pop-up, or the screen
    // when none is open): the first one, at the start or when the focused one went away.
    bool found = false;
    const UiItem* first = nullptr;
    for (const UiItem& item : m_Items) {
        if (!IsNavigable(item.Kind) || !InActiveScope(item))
            continue;
        found |= item.Id == m_Focused;
        if (!first)
            first = &item;
    }
    if (!found)
        m_Focused = first ? first->Id : 0;
    // Directions no widget used move the focus. A pop-up that is fading out keeps its focus
    // where it is; the screen underneath gets it back when the close finishes.
    const bool navigating = m_Popups.empty() || !m_Popups.back().Closing;
    for (u8 d = 0; d < 4; ++d)
        if (m_Nav[d] && found && navigating)
            MoveFocus(static_cast<Dir>(d));
    LayoutPopups();
    for (Popup& popup : m_Popups)
        if (popup.EatClick && !m_Input.Click.Held)
            popup.EatClick = false;
    // Forget widgets that were not described this frame.
    std::erase_if(m_States, [&](const auto& entry) { return entry.second.Frame != m_Frame; });
    if (!m_Input.Click.Held)
        m_Pressed = 0;
}

// --- Focus navigation ---

void Ui::MoveFocus(Dir d)
{
    const UiItem* current = nullptr;
    f32 first = 1e30f; // the extent of all focusable widgets along the direction's axis
    f32 last = -1e30f;
    const bool vertical = d == kUp || d == kDown;
    for (const UiItem& item : m_Items) {
        if (!IsNavigable(item.Kind) || !InActiveScope(item))
            continue;
        if (item.Id == m_Focused)
            current = &item;
        const f32 start = vertical ? item.Rect.Position.y : item.Rect.Position.x;
        first = Min(first, start);
        last = Max(last, start + (vertical ? item.Rect.Size.y : item.Rect.Size.x));
    }
    if (!current)
        return;
    const f32 span = last - first + m_Style->Spacing;
    const f32 sign = (d == kDown || d == kRight) ? 1.0f : -1.0f;
    // Candidates are the widgets entirely ahead in that direction; the best is the nearest
    // (center to center), plus twice how far it is to the side (0 if the two overlap sideways,
    // e.g. a wide button above two columns), so the focus stays in its column / row. A sliver of
    // the sideways center distance breaks ties towards the most aligned one. With nothing ahead,
    // the widgets entirely behind count as one whole layout further on: the focus wraps around.
    // Widgets overlapping the current one along the direction (the same row for Left / Right,
    // the same column for Up / Down) are never candidates.
    const auto range = [](const UiRect& rc, bool x, f32& from, f32& to) {
        from = x ? rc.Position.x : rc.Position.y;
        to = from + (x ? rc.Size.x : rc.Size.y);
    };
    f32 curLo = 0.0f; // the current widget along the direction's axis...
    f32 curHi = 0.0f;
    f32 curFrom = 0.0f; // ...and sideways
    f32 curTo = 0.0f;
    range(current->Rect, !vertical, curLo, curHi);
    range(current->Rect, vertical, curFrom, curTo);
    const UiItem* best = nullptr;
    f32 bestScore = 1e30f;
    for (const UiItem& item : m_Items) {
        if (!IsNavigable(item.Kind) || !InActiveScope(item) || &item == current)
            continue;
        f32 lo = 0.0f;
        f32 hi = 0.0f;
        f32 from = 0.0f;
        f32 to = 0.0f;
        range(item.Rect, !vertical, lo, hi);
        range(item.Rect, vertical, from, to);
        const bool ahead = sign > 0.0f ? lo >= curHi - 0.5f : hi <= curLo + 0.5f;
        const bool behind = sign > 0.0f ? hi <= curLo + 0.5f : lo >= curHi - 0.5f;
        if (!ahead && !behind)
            continue;
        const Vec2 delta = item.Rect.GetCenter() - current->Rect.GetCenter();
        const f32 along = sign * (vertical ? delta.y : delta.x) + (ahead ? 0.0f : span);
        const f32 gap = Max(0.0f, Max(from - curTo, curFrom - to));
        const f32 center = std::fabs(vertical ? delta.x : delta.y);
        const f32 score = along + 2.0f * gap + 0.01f * center;
        if (score < bestScore) {
            bestScore = score;
            best = &item;
        }
    }
    if (best)
        m_Focused = best->Id;
}

// --- Layout ---

void Ui::BeginPanel(std::string_view title, const UiPanelOptions& options)
{
    // Same path as the widgets: a screen panel still takes a local-y slot. Skipping it made
    // m_LocalY shorter than m_Items, and LayoutPopups then left pop-ups unscrolled and unfaded.
    UiItem& panel = Emplace(kNoLocal);
    panel.Kind = UiKind::Panel;
    panel.Id = GetId(title);
    panel.Text = DisplayText(title);
    State& state = m_States[panel.Id];
    state.Frame = m_Frame;
    // Placed with last frame's height; EndPanel sets this frame's.
    const Vec2 size{options.Width, state.Height};
    panel.Rect.Position = options.Position - size * options.Pivot;
    panel.Rect.Position = {std::round(panel.Rect.Position.x), std::round(panel.Rect.Position.y)};
    panel.Rect.Size = {std::round(options.Width), state.Height};
    m_Panel = m_Items.size() - 1;
    m_InPanel = true;
    const f32 pad = m_Style->Padding;
    m_Area = {panel.Rect.Position + Vec2(pad), {panel.Rect.Size.x - 2.0f * pad, 0.0f}};
    m_CursorY = m_Area.Position.y;
    if (!panel.Text.empty()) // the title has a row of its own
        m_CursorY += m_Style->RowHeight + m_Style->Spacing;
    m_Columns = 1;
    m_Column = 0;
}

void Ui::EndPanel()
{
    if (!m_InPanel)
        return;
    if (m_Column > 0) // an unfinished Columns row
        m_CursorY += m_Style->RowHeight + m_Style->Spacing;
    UiItem& panel = m_Items[m_Panel];
    // The last row's spacing becomes the bottom padding.
    const f32 height = m_CursorY - m_Style->Spacing + m_Style->Padding - panel.Rect.Position.y;
    panel.Rect.Size.y = std::round(height);
    m_States[panel.Id].Height = panel.Rect.Size.y;
    m_InPanel = false;
    m_Columns = 1;
    m_Column = 0;
}

void Ui::Columns(u32 columns)
{
    m_Columns = Max(columns, 1u);
    m_Column = 0;
}

void Ui::Space(f32 height)
{
    m_CursorY += height;
    if (m_InPopup)
        m_ContentY += height;
}

UiRect Ui::NextRect()
{
    const f32 gap = m_Style->Spacing;
    const f32 width =
        (m_Area.Size.x - gap * static_cast<f32>(m_Columns - 1)) / static_cast<f32>(m_Columns);
    const f32 x = m_Area.Position.x + (width + gap) * static_cast<f32>(m_Column);
    // A pop-up row is placed in the list's own coordinates, then shifted by the scroll. The
    // screen's rows use the panel cursor. Whole units either way, so columns tile exactly.
    f32 y = m_CursorY;
    if (m_InPopup) {
        const Popup* popup = FindPopup(m_PopupScope);
        const f32 scroll = popup ? popup->Scroll : 0.0f;
        y = m_ContentTop + m_ContentY - scroll;
    }
    const UiRect rect{{std::round(x), std::round(y)},
                      {std::round(x + width) - std::round(x), std::round(m_Style->RowHeight)}};
    if (++m_Column >= m_Columns) {
        m_Column = 0;
        m_Columns = 1;
        const f32 step = m_Style->RowHeight + gap;
        m_CursorY += step;
        if (m_InPopup) {
            m_ContentY += step;
            ++m_ContentRows;
        }
    }
    return rect;
}

// --- Widgets ---

UiItem& Ui::Add(UiKind kind, std::string_view label, bool focusable)
{
    // Captured before NextRect advances the row.
    const f32 local = m_InPopup ? m_ContentY : kNoLocal;
    UiItem& item = Emplace(local);
    item.Kind = kind;
    item.Id = GetId(label);
    item.Text = DisplayText(label);
    item.Rect = NextRect();
    if (m_InPopup && local >= 0.0f) {
        // Last frame's window: a row scrolled out of it must not take the mouse. The first frame
        // has no window yet (LayoutPopups shows the top rows before Draw).
        const Popup* popup = FindPopup(m_PopupScope);
        item.Shown = popup && popup->ViewHeight > 0.0f &&
                     item.Rect.Position.y + item.Rect.Size.y > popup->ContentTop - 0.5f &&
                     item.Rect.Position.y < popup->ContentTop + popup->ViewHeight - 0.5f;
    }
    if (focusable && AcceptsInput()) {
        if (m_Focused == 0) // nothing focused yet (the first frame): the first widget is
            m_Focused = item.Id;
        const bool over = item.Shown && item.Rect.Contains(m_Input.Mouse);
        if (over && m_Input.MouseMoved)
            m_Focused = item.Id; // the mouse focuses what it moves over
        if (over && m_Input.Click.Pressed) {
            m_Pressed = item.Id;
            m_Focused = item.Id;
        }
        item.Held = (m_Pressed == item.Id && m_Input.Click.Held) ||
                    (m_Focused == item.Id && m_Input.Accept.Held);
    }
    State& state = m_States[item.Id];
    state.Frame = m_Frame;
    const f32 target = (focusable && m_Focused == item.Id) ? 1.0f : 0.0f;
    const f32 step = m_Style->FocusSpeed * m_Dt;
    // Towards the target, stopping there (stepping past it made a full highlight flicker).
    state.Focus =
        target > state.Focus ? Min(state.Focus + step, target) : Max(state.Focus - step, target);
    item.Focus = state.Focus;
    return item;
}

bool Ui::Activated(const UiItem& item) const
{
    // A pop-up owns the input: a widget on the screen under it (or on a pop-up under the top
    // one) does not activate, even if the click lands on its rect.
    if (!InActiveScope(item))
        return false;
    const bool accepted = m_Input.Accept.Pressed && m_Focused == item.Id;
    const bool clicked =
        m_Input.Click.Released && m_Pressed == item.Id && item.Rect.Contains(m_Input.Mouse);
    return accepted || clicked;
}

void Ui::Label(std::string_view text, TextAlign align)
{
    UiItem& item = Add(UiKind::Label, text, false);
    item.Align = align;
}

bool Ui::Button(std::string_view label)
{
    return Activated(Add(UiKind::Button, label, true));
}

bool Ui::Toggle(std::string_view label, bool* value)
{
    UiItem& item = Add(UiKind::Toggle, label, true);
    bool flip = Activated(item);
    if (AcceptsInput() && m_Focused == item.Id && (Nav(kLeft) || Nav(kRight))) {
        flip = true;
        UseNav(kLeft);
        UseNav(kRight);
    }
    if (flip)
        *value = !*value;
    item.Amount = *value ? 1.0f : 0.0f;
    return flip;
}

bool Ui::Slider(std::string_view label, f32* value, f32 min, f32 max, f32 step)
{
    UiItem& item = Add(UiKind::Slider, label, true);
    const f32 before = *value;
    // step <= 0 (or NaN) would divide by zero in the snap below. Dragging still works.
    if (AcceptsInput() && m_Focused == item.Id && std::isfinite(step) && step > 0.0f) {
        const f32 move = (Nav(kRight) ? step : 0.0f) - (Nav(kLeft) ? step : 0.0f);
        if (move != 0.0f) {
            // Snapped to whole steps from `min`, so repeated presses do not drift (0.30000001).
            *value = min + std::round((*value + move - min) / step) * step;
            UseNav(kLeft);
            UseNav(kRight);
        }
    }
    // Dragging: the value follows the cursor over the track (the right part of the row).
    if (AcceptsInput() && m_Pressed == item.Id && m_Input.Click.Held) {
        const f32 trackX = item.Rect.Position.x + item.Rect.Size.x * 0.55f;
        const f32 trackW = item.Rect.Size.x * 0.45f - m_Style->Spacing;
        *value = min + (max - min) * Clamp((m_Input.Mouse.x - trackX) / trackW, 0.0f, 1.0f);
    }
    *value = Clamp(*value, min, max);
    item.Amount = max > min ? (*value - min) / (max - min) : 0.0f;
    return *value != before;
}

bool Ui::Choice(std::string_view label, i32* index, std::span<const std::string_view> options)
{
    UiItem& item = Add(UiKind::Choice, label, true);
    const i32 count = static_cast<i32>(options.size());
    if (count == 0)
        return false;
    const i32 before = *index;
    i32 move = 0;
    if (AcceptsInput() && m_Focused == item.Id) {
        move = (Nav(kRight) ? 1 : 0) - (Nav(kLeft) ? 1 : 0);
        UseNav(kLeft);
        UseNav(kRight);
        if (m_Input.Accept.Pressed)
            move = 1;
    }
    if (Activated(item) && m_Input.Click.Released) // a click on the left half goes back
        move = m_Input.Mouse.x < item.Rect.Position.x + item.Rect.Size.x * 0.775f ? -1 : 1;
    *index = ((Clamp(*index, 0, count - 1) + move) % count + count) % count;
    item.Value = options[static_cast<usize>(*index)];
    return *index != before;
}

// --- Ids ---

u32 Ui::GetId(std::string_view label) const
{
    const u32 id = Hash(label, m_IdStack.empty() ? kFnvBasis : m_IdStack.back());
    return id == 0 ? 1 : id; // 0 means "no widget"
}

void Ui::PushId(std::string_view id)
{
    m_IdStack.push_back(GetId(id));
}

void Ui::PopId()
{
    if (!m_IdStack.empty())
        m_IdStack.pop_back();
}

// --- Pop-ups ---

bool Ui::IsNavigable(UiKind kind)
{
    switch (kind) {
    case UiKind::Button:
    case UiKind::Toggle:
    case UiKind::Slider:
    case UiKind::Choice:
        return true;
    default:
        return false;
    }
}

u32 Ui::MakePopupId(std::string_view id) const
{
    const u32 base = m_IdStack.empty() ? kFnvBasis : m_IdStack.back();
    const u32 hash = Hash(id, Hash("popup/", base));
    return hash == 0 ? 1 : hash;
}

Ui::Popup* Ui::FindPopup(u32 id)
{
    for (Popup& popup : m_Popups)
        if (popup.Id == id)
            return &popup;
    return nullptr;
}

const Ui::Popup* Ui::FindPopup(u32 id) const
{
    for (const Popup& popup : m_Popups)
        if (popup.Id == id)
            return &popup;
    return nullptr;
}

bool Ui::AcceptsInput() const
{
    if (!m_Popups.empty() && m_Popups.back().Closing)
        return false;
    if (m_Popups.empty())
        return m_PopupScope == 0;
    return m_PopupScope == m_Popups.back().Id;
}

bool Ui::InActiveScope(const UiItem& item) const
{
    if (m_Popups.empty())
        return item.Scope == 0;
    return item.Scope == m_Popups.back().Id;
}

UiItem& Ui::Emplace(f32 localY)
{
    m_LocalY.push_back(localY);
    UiItem& item = m_Items.emplace_back();
    item.Scope = m_PopupScope;
    return item;
}

std::string Ui::PromptText() const
{
    std::string text;
    // No glyph (nothing bound, or no pad and no key): that side stays off, so a test that never
    // sets prompts does not grow a bar of bare words.
    const auto add = [&](const std::string& glyph, const std::string& word) {
        if (glyph.empty())
            return;
        if (!text.empty())
            text += "      ";
        text += glyph;
        if (!word.empty())
            text += "  ";
        text += word;
    };
    add(m_Input.AcceptPrompt, m_Style->PromptConfirm);
    add(m_Input.BackPrompt, m_Style->PromptCancel);
    return text;
}

f32 Ui::PopupAlpha(const Popup& popup) const
{
    const f32 open = m_Style->PopupOpenSeconds;
    f32 alpha = open > 0.0f ? Clamp(popup.Age / open, 0.0f, 1.0f) : 1.0f;
    if (popup.Closing) {
        const f32 close = m_Style->PopupCloseSeconds;
        alpha *= close > 0.0f ? Clamp(1.0f - popup.CloseAge / close, 0.0f, 1.0f) : 0.0f;
    }
    return alpha;
}

void Ui::ApplyPopupInput()
{
    if (m_Popups.empty())
        return;
    Popup& top = m_Popups.back();
    // One press closes one pop-up. A second Back while the top one is still fading must not
    // close the one under it or leave the screen (WasBackPressed stays false).
    if (top.Closing) {
        if (m_Input.Back.Pressed)
            m_BackConsumed = true;
        return;
    }
    if (m_Input.Back.Pressed && !m_BackConsumed) {
        top.Closing = true;
        top.CloseAge = 0.0f;
        m_BackConsumed = true;
        return;
    }
    // Pressed, not released: a click that starts outside closes. The press that opened the
    // pop-up is eaten, and a release never closes on its own.
    if (m_Input.Click.Pressed && !top.EatClick && !top.Panel.Contains(m_Input.Mouse)) {
        top.Closing = true;
        top.CloseAge = 0.0f;
        m_Input.Click = {};
        m_Pressed = 0;
    }
}

void Ui::FinishClosing()
{
    while (!m_Popups.empty()) {
        const Popup& top = m_Popups.back();
        if (!top.Closing || top.CloseAge < m_Style->PopupCloseSeconds)
            break;
        m_Closed.push_back(top.Id);
        const u32 opener = top.Opener;
        m_Popups.pop_back();
        m_Focused = opener;
    }
}

void Ui::LayoutPopups()
{
    if (m_LocalY.size() != m_Items.size())
        return;
    if (!m_Popups.empty() && !m_Popups.back().Closing) {
        Popup& top = m_Popups.back();
        f32 local = kNoLocal;
        for (usize i = 0; i < m_Items.size(); ++i) {
            if (m_Items[i].Id == m_Focused && m_Items[i].Scope == top.Id && m_LocalY[i] >= 0.0f)
                local = m_LocalY[i];
        }
        if (local >= 0.0f) {
            const f32 row = m_Style->RowHeight;
            if (local < top.Scroll)
                top.Scroll = local;
            else if (local + row > top.Scroll + top.ViewHeight)
                top.Scroll = local + row - top.ViewHeight;
            top.Scroll = Clamp(top.Scroll, 0.0f, Max(0.0f, top.ContentHeight - top.ViewHeight));
        }
    }
    for (Popup& popup : m_Popups) {
        const f32 alpha = PopupAlpha(popup);
        const f32 row = m_Style->RowHeight;
        for (usize i = 0; i < m_Items.size(); ++i) {
            UiItem& item = m_Items[i];
            if (item.Scope != popup.Id)
                continue;
            item.Alpha = alpha;
            if (m_LocalY[i] < 0.0f)
                continue;
            const f32 y = popup.ContentTop + m_LocalY[i] - popup.Scroll;
            item.Rect.Position.y = std::round(y);
            item.Shown =
                y + row > popup.ContentTop - 0.5f && y < popup.ContentTop + popup.ViewHeight - 0.5f;
        }
        if (popup.Thumb >= m_Items.size() || popup.ContentHeight <= popup.ViewHeight)
            continue;
        const f32 view = popup.ViewHeight;
        f32 thumb = view * view / popup.ContentHeight;
        thumb = Clamp(thumb, m_Style->PopupScrollbar, view);
        const f32 travel = Max(0.0f, view - thumb);
        const f32 span = Max(popup.ContentHeight - view, 0.0001f);
        UiItem& thumbItem = m_Items[popup.Thumb];
        thumbItem.Rect.Position.y = std::round(popup.ContentTop + travel * (popup.Scroll / span));
        thumbItem.Rect.Size.y = Max(std::round(thumb), 1.0f);
        thumbItem.Shown = true;
    }
}

void Ui::OpenPopup(std::string_view id)
{
    const u32 pid = MakePopupId(id);
    if (FindPopup(pid))
        return;
    Popup popup;
    popup.Id = pid;
    popup.Opener = m_Focused;
    for (const UiItem& item : m_Items) {
        if (item.Id == m_Focused && IsNavigable(item.Kind)) {
            popup.OpenerRect = item.Rect;
            popup.HasOpener = true;
            break;
        }
    }
    // The press that opened the list must not also activate its first row, and the click must
    // not count as a click outside it.
    popup.EatClick = m_Input.Click.Pressed || m_Input.Click.Held;
    if (m_Input.Accept.Pressed) {
        m_Input.Accept.Pressed = false;
        m_Input.Accept.Held = false;
    }
    m_Popups.push_back(popup);
    m_Focused = 0;
}

bool Ui::BeginPopup(std::string_view id, const UiPopupOptions& options)
{
    if (m_InPopup) {
        if (Log::Core())
            EM_CORE_WARN("Ui: BeginPopup('{}') while another pop-up is still open; call EndPopup "
                         "first",
                         id);
        return false;
    }
    Popup* popup = FindPopup(MakePopupId(id));
    if (!popup)
        return false;
    popup->Begun = true;
    popup->Anchor = options.Anchor;
    popup->Screen = options.Screen;
    popup->Dim = options.Dim;
    popup->MaxRows = options.MaxRows > 0 ? options.MaxRows : m_Style->PopupMaxRows;
    popup->Track = kNoItem;
    popup->Thumb = kNoItem;
    m_WantPrompt = options.Prompt;

    const Vec2 screen = options.Screen;
    const Vec2 size{options.Width, popup->LastHeight};
    const bool center = options.Anchor == UiPopupAnchor::Center || !popup->HasOpener;
    Vec2 pos = screen * 0.5f - size * 0.5f;
    if (!center) {
        const UiRect& opener = popup->OpenerRect;
        const f32 gap = m_Style->PopupGap;
        pos = {opener.Position.x, opener.Position.y + opener.Size.y + gap};
        // Below the opener, or above it when the box would leave the screen.
        if (screen.y > 0.0f && pos.y + size.y > screen.y &&
            opener.Position.y - gap - size.y >= 0.0f)
            pos.y = opener.Position.y - gap - size.y;
    }
    if (screen.x > 0.0f)
        pos.x = Clamp(pos.x, 0.0f, Max(0.0f, screen.x - size.x));
    if (screen.y > 0.0f)
        pos.y = Clamp(pos.y, 0.0f, Max(0.0f, screen.y - size.y));
    pos = {std::round(pos.x), std::round(pos.y)};

    m_Saved = {m_InPanel, m_Panel, m_Area, m_CursorY, m_Columns, m_Column};
    m_InPopup = true;
    m_PopupScope = popup->Id;
    m_InPanel = false;
    m_Columns = 1;
    m_Column = 0;
    m_ContentY = 0.0f;
    m_ContentRows = 0;

    if (options.Dim && screen.x > 0.0f && screen.y > 0.0f) {
        UiItem& backdrop = Emplace(kNoLocal);
        backdrop.Kind = UiKind::Backdrop;
        backdrop.Rect = {{0.0f, 0.0f}, screen};
    }
    UiItem& panel = Emplace(kNoLocal);
    panel.Kind = UiKind::Panel;
    panel.Id = popup->Id;
    panel.Text = std::string(DisplayText(id));
    panel.Rect = {pos, {std::round(options.Width), std::round(popup->LastHeight)}};
    m_Panel = m_Items.size() - 1;

    const f32 pad = m_Style->Padding;
    m_ContentTop = pos.y + pad;
    if (!panel.Text.empty())
        m_ContentTop += m_Style->RowHeight + m_Style->Spacing;
    popup->ContentTop = m_ContentTop;
    m_Area = {Vec2(pos.x + pad, m_ContentTop), {panel.Rect.Size.x - 2.0f * pad, 0.0f}};
    m_CursorY = m_ContentTop;
    return true;
}

void Ui::EndPopup()
{
    if (!m_InPopup)
        return;
    Popup* popup = FindPopup(m_PopupScope);
    if (!popup) {
        m_InPopup = false;
        m_PopupScope = 0;
        return;
    }
    if (m_Column > 0) { // a Columns row that never filled
        m_ContentY += m_Style->RowHeight + m_Style->Spacing;
        ++m_ContentRows;
        m_Column = 0;
        m_Columns = 1;
    }
    const f32 gap = m_Style->Spacing;
    const f32 row = m_Style->RowHeight;
    popup->Rows = m_ContentRows;
    popup->ContentHeight = m_ContentRows > 0 ? Max(0.0f, m_ContentY - gap) : 0.0f;
    const i32 visible = Min(m_ContentRows, Max(popup->MaxRows, 1));
    popup->ViewHeight = visible > 0 ? visible * row + static_cast<f32>(visible - 1) * gap : 0.0f;
    popup->Scroll = Clamp(popup->Scroll, 0.0f, Max(0.0f, popup->ContentHeight - popup->ViewHeight));

    f32 bottom = m_ContentTop + popup->ViewHeight;
    if (m_WantPrompt && !PromptText().empty()) {
        if (popup->ViewHeight > 0.0f)
            bottom += gap;
        UiItem& prompt = Emplace(kNoLocal);
        prompt.Kind = UiKind::Prompt;
        prompt.Text = PromptText();
        prompt.Align = TextAlign::Center;
        prompt.Rect = {{std::round(m_Area.Position.x), std::round(bottom)},
                       {std::round(m_Area.Size.x), std::round(row)}};
        bottom += row;
    }
    bottom += m_Style->Padding;
    m_Items[m_Panel].Rect.Size.y = std::round(bottom - m_Items[m_Panel].Rect.Position.y);
    popup->Panel = m_Items[m_Panel].Rect;
    popup->LastHeight = popup->Panel.Size.y;

    const bool overflow = popup->ContentHeight > popup->ViewHeight + 0.5f &&
                          m_Style->PopupScrollbar > 0.0f && popup->ViewHeight > 0.0f;
    if (overflow) {
        const f32 bar = m_Style->PopupScrollbar;
        const f32 inset = bar + gap;
        for (usize i = 0; i < m_Items.size(); ++i) {
            if (m_Items[i].Scope == popup->Id && m_LocalY[i] >= 0.0f)
                m_Items[i].Rect.Size.x = Max(0.0f, m_Items[i].Rect.Size.x - inset);
        }
        const f32 x = popup->Panel.Position.x + popup->Panel.Size.x - m_Style->Padding - bar;
        UiItem& track = Emplace(kNoLocal);
        track.Kind = UiKind::Scrollbar;
        track.Amount = 0.0f;
        track.Rect = {{std::round(x), std::round(popup->ContentTop)},
                      {std::round(bar), std::round(popup->ViewHeight)}};
        popup->Track = m_Items.size() - 1;
        UiItem& thumb = Emplace(kNoLocal);
        thumb.Kind = UiKind::Scrollbar;
        thumb.Amount = 1.0f;
        thumb.Rect = track.Rect;
        popup->Thumb = m_Items.size() - 1;
    }

    m_InPanel = m_Saved.InPanel;
    m_Panel = m_Saved.Panel;
    m_Area = m_Saved.Area;
    m_CursorY = m_Saved.CursorY;
    m_Columns = m_Saved.Columns;
    m_Column = m_Saved.Column;
    m_InPopup = false;
    m_PopupScope = 0;
    m_WantPrompt = false;
}

void Ui::ClosePopup()
{
    if (m_Popups.empty() || m_Popups.back().Closing)
        return;
    m_Popups.back().Closing = true;
    m_Popups.back().CloseAge = 0.0f;
}

bool Ui::IsPopupOpen(std::string_view id) const
{
    return FindPopup(MakePopupId(id)) != nullptr;
}

f32 Ui::GetPopupScroll() const
{
    return m_Popups.empty() ? 0.0f : m_Popups.back().Scroll;
}

bool Ui::WasPopupClosed(std::string_view id) const
{
    const u32 pid = MakePopupId(id);
    for (const u32 closed : m_Closed)
        if (closed == pid)
            return true;
    return false;
}

void Ui::PromptBar()
{
    if (m_InPopup) {
        m_WantPrompt = true; // EndPopup places a single bar under the list
        return;
    }
    const std::string text = PromptText();
    if (text.empty())
        return;
    UiItem& item = Add(UiKind::Prompt, "##prompt", false);
    item.Text = text;
    item.Align = TextAlign::Center;
}

// --- Drawing ---

void Ui::Draw(Renderer2D& r, const Font& font) const
{
    const UiStyle& s = *m_Style;
    const f32 scale = s.TextScale;
    const f32 textHeight = (font.GetAscent() - font.GetDescent()) * scale;
    // Text centered vertically in a row, on a whole unit.
    const auto text = [&](std::string_view str, const UiRect& rect, f32 x, const Vec4& color,
                          TextAlign align) {
        const f32 y = std::round(rect.Position.y + (rect.Size.y - textHeight) * 0.5f);
        r.DrawString(font, str, {std::round(x), y}, color, scale, align);
    };
    for (const UiItem& item : m_Items) {
        if (!item.Shown || item.Alpha <= 0.0f)
            continue;
        const UiRect& rc = item.Rect;
        const Vec2 pos = rc.Position;
        const f32 right = pos.x + rc.Size.x;
        const f32 alpha = item.Alpha;
        const Vec4 color = Fade(Mix(s.Text, s.TextFocused, item.Focus), alpha);
        const f32 inset = s.Spacing + 3.0f; // text from the row's edges (clear of the focus bar)
        // The value part of a row (toggle box, slider track, choice), right of the label.
        const f32 valueX = pos.x + rc.Size.x * 0.55f;
        const f32 valueW = rc.Size.x * 0.45f - s.Spacing;
        if (IsNavigable(item.Kind) && item.Focus > 0.0f) {
            Vec4 glow = s.Highlight;
            glow.w *= 0.3f * item.Focus * alpha;
            r.FillRect(pos, rc.Size, glow);
            r.FillRect(pos, {3.0f, rc.Size.y}, Fade(s.Highlight, item.Focus * alpha));
        }
        switch (item.Kind) {
        case UiKind::Panel:
            if (s.PanelSprite) {
                DrawNineSlice(r, *s.PanelSprite, rc, s.PanelSliceBorder, s.PanelSliceScale,
                              Vec4(1.0f, 1.0f, 1.0f, alpha));
            } else {
                r.FillRect(pos, rc.Size, Fade(s.PanelFill, alpha));
                r.DrawRect(pos, rc.Size, Fade(s.PanelBorder, alpha));
            }
            if (!item.Text.empty())
                text(item.Text, {pos + Vec2(0.0f, s.Padding), {rc.Size.x, s.RowHeight}},
                     pos.x + rc.Size.x * 0.5f, Fade(s.Title, alpha), TextAlign::Center);
            break;
        case UiKind::Backdrop:
            r.FillRect(pos, rc.Size, Fade(s.PopupBackdrop, alpha));
            break;
        case UiKind::Scrollbar:
            r.FillRect(pos, rc.Size,
                       Fade(item.Amount > 0.5f ? s.PopupScroll : s.PopupScrollTrack, alpha));
            break;
        case UiKind::Prompt:
            text(item.Text, rc, pos.x + rc.Size.x * 0.5f, Fade(s.Text, alpha), TextAlign::Center);
            break;
        case UiKind::Label: {
            const f32 x = item.Align == TextAlign::Left     ? pos.x + inset
                          : item.Align == TextAlign::Center ? pos.x + rc.Size.x * 0.5f
                                                            : right - inset;
            text(item.Text, rc, x, Fade(s.Text, alpha), item.Align);
            break;
        }
        case UiKind::Button:
            if (item.Focus < 1.0f)
                r.FillRect(pos, rc.Size, Fade(s.Widget, (1.0f - item.Focus) * alpha));
            text(item.Text, rc, pos.x + rc.Size.x * 0.5f,
                 item.Held ? Fade(s.Highlight, alpha) : color, TextAlign::Center);
            break;
        case UiKind::Toggle: {
            text(item.Text, rc, pos.x + inset, color, TextAlign::Left);
            const f32 box = std::round(rc.Size.y * 0.5f);
            const Vec2 at{right - s.Spacing - box, std::round(pos.y + (rc.Size.y - box) * 0.5f)};
            r.DrawRect(at, Vec2(box), color);
            if (item.Amount > 0.0f)
                r.FillRect(at + Vec2(3.0f), Vec2(box - 6.0f), Fade(s.Highlight, alpha));
            break;
        }
        case UiKind::Slider: {
            text(item.Text, rc, pos.x + inset, color, TextAlign::Left);
            const f32 y = std::round(pos.y + rc.Size.y * 0.5f) - 2.0f;
            const f32 filled = std::round(valueW * item.Amount);
            r.FillRect({valueX, y}, {valueW, 4.0f}, Fade(s.Widget, alpha));
            r.FillRect({valueX, y}, {filled, 4.0f}, Fade(s.Highlight, alpha));
            const f32 knob = std::round(rc.Size.y * 0.6f);
            r.FillRect({valueX + filled - 3.0f, std::round(pos.y + (rc.Size.y - knob) * 0.5f)},
                       {6.0f, knob}, color);
            break;
        }
        case UiKind::Choice:
            text(item.Text, rc, pos.x + inset, color, TextAlign::Left);
            text("<", rc, valueX, color, TextAlign::Left);
            text(item.Value, rc, valueX + valueW * 0.5f, color, TextAlign::Center);
            text(">", rc, valueX + valueW, color, TextAlign::Right);
            break;
        }
    }
}

void DrawNineSlice(Renderer2D& r, const Sprite& sprite, const UiRect& rect, f32 border, f32 scale,
                   const Vec4& tint)
{
    // Three columns and rows in the source (corner, middle, corner) and in the destination.
    const Vec2 src = sprite.Region.Size;
    const f32 edge = border * scale;
    const f32 srcX[4] = {0.0f, border, src.x - border, src.x};
    const f32 srcY[4] = {0.0f, border, src.y - border, src.y};
    const f32 dstX[4] = {0.0f, edge, rect.Size.x - edge, rect.Size.x};
    const f32 dstY[4] = {0.0f, edge, rect.Size.y - edge, rect.Size.y};
    for (usize row = 0; row < 3; ++row) {
        for (usize col = 0; col < 3; ++col) {
            const Vec2 size{dstX[col + 1] - dstX[col], dstY[row + 1] - dstY[row]};
            if (size.x <= 0.0f || size.y <= 0.0f)
                continue;
            const Sprite piece = sprite.Crop(
                {srcX[col], srcY[row]}, {srcX[col + 1] - srcX[col], srcY[row + 1] - srcY[row]});
            r.DrawSprite(piece, rect.Position + Vec2(dstX[col], dstY[row]),
                         {.Size = size, .Origin = {0.0f, 0.0f}, .Tint = tint});
        }
    }
}

} // namespace Emerald
