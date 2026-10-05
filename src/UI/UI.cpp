#include "Emerald/UI/UI.h"

#include <cmath>

#include "Emerald/Input/Input.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

namespace {

constexpr u32 kFnvBasis = 2166136261u;
constexpr u32 kFnvPrime = 16777619u;

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
    return in;
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
    m_Items.clear();
    m_IdStack.clear();
    m_InPanel = false;
    m_Area = {{0.0f, 0.0f}, {300.0f, 0.0f}}; // widgets outside a panel
    m_CursorY = 0.0f;
    m_Columns = 1;
    m_Column = 0;
}

void Ui::End()
{
    // Keep the focus on a widget that exists (the first one, at the start or when it went away).
    bool found = false;
    const UiItem* first = nullptr;
    for (const UiItem& item : m_Items) {
        if (item.Kind == UiKind::Panel || item.Kind == UiKind::Label)
            continue;
        found |= item.Id == m_Focused;
        if (!first)
            first = &item;
    }
    if (!found)
        m_Focused = first ? first->Id : 0;
    // Directions no widget used move the focus.
    for (u8 d = 0; d < 4; ++d)
        if (m_Nav[d] && found)
            MoveFocus(static_cast<Dir>(d));
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
        if (item.Kind == UiKind::Panel || item.Kind == UiKind::Label)
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
        if (item.Kind == UiKind::Panel || item.Kind == UiKind::Label || &item == current)
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
    UiItem& panel = m_Items.emplace_back();
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
}

UiRect Ui::NextRect()
{
    const f32 gap = m_Style->Spacing;
    const f32 width =
        (m_Area.Size.x - gap * static_cast<f32>(m_Columns - 1)) / static_cast<f32>(m_Columns);
    const f32 x = m_Area.Position.x + (width + gap) * static_cast<f32>(m_Column);
    // Whole units: the left edge and the right edge are rounded, so columns tile exactly.
    const UiRect rect{{std::round(x), std::round(m_CursorY)},
                      {std::round(x + width) - std::round(x), std::round(m_Style->RowHeight)}};
    if (++m_Column >= m_Columns) {
        m_Column = 0;
        m_Columns = 1;
        m_CursorY += m_Style->RowHeight + gap;
    }
    return rect;
}

// --- Widgets ---

UiItem& Ui::Add(UiKind kind, std::string_view label, bool focusable)
{
    UiItem& item = m_Items.emplace_back();
    item.Kind = kind;
    item.Id = GetId(label);
    item.Text = DisplayText(label);
    item.Rect = NextRect();
    if (focusable) {
        const bool over = item.Rect.Contains(m_Input.Mouse);
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
    state.Focus = Clamp(state.Focus + (target > state.Focus ? step : -step), 0.0f, 1.0f);
    item.Focus = state.Focus;
    return item;
}

bool Ui::Activated(const UiItem& item) const
{
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
    if (m_Focused == item.Id && (Nav(kLeft) || Nav(kRight))) {
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
    if (m_Focused == item.Id) {
        const f32 move = (Nav(kRight) ? step : 0.0f) - (Nav(kLeft) ? step : 0.0f);
        if (move != 0.0f) {
            // Snapped to whole steps from `min`, so repeated presses do not drift (0.30000001).
            *value = min + std::round((*value + move - min) / step) * step;
            UseNav(kLeft);
            UseNav(kRight);
        }
    }
    // Dragging: the value follows the cursor over the track (the right part of the row).
    if (m_Pressed == item.Id && m_Input.Click.Held) {
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
    if (m_Focused == item.Id) {
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
        const UiRect& rc = item.Rect;
        const Vec2 pos = rc.Position;
        const f32 right = pos.x + rc.Size.x;
        const Vec4 color = Mix(s.Text, s.TextFocused, item.Focus);
        const f32 inset = s.Spacing + 3.0f; // text from the row's edges (clear of the focus bar)
        // The value part of a row (toggle box, slider track, choice), right of the label.
        const f32 valueX = pos.x + rc.Size.x * 0.55f;
        const f32 valueW = rc.Size.x * 0.45f - s.Spacing;
        if (item.Kind != UiKind::Panel && item.Kind != UiKind::Label && item.Focus > 0.0f) {
            Vec4 glow = s.Highlight;
            glow.w *= 0.3f * item.Focus;
            r.FillRect(pos, rc.Size, glow);
            r.FillRect(pos, {3.0f, rc.Size.y}, s.Highlight * Vec4(1.0f, 1.0f, 1.0f, item.Focus));
        }
        switch (item.Kind) {
        case UiKind::Panel:
            if (s.PanelSprite) {
                DrawNineSlice(r, *s.PanelSprite, rc, s.PanelSliceBorder, s.PanelSliceScale);
            } else {
                r.FillRect(pos, rc.Size, s.PanelFill);
                r.DrawRect(pos, rc.Size, s.PanelBorder);
            }
            if (!item.Text.empty())
                text(item.Text, {pos + Vec2(0.0f, s.Padding), {rc.Size.x, s.RowHeight}},
                     pos.x + rc.Size.x * 0.5f, s.Title, TextAlign::Center);
            break;
        case UiKind::Label: {
            const f32 x = item.Align == TextAlign::Left     ? pos.x + inset
                          : item.Align == TextAlign::Center ? pos.x + rc.Size.x * 0.5f
                                                            : right - inset;
            text(item.Text, rc, x, s.Text, item.Align);
            break;
        }
        case UiKind::Button:
            if (item.Focus < 1.0f)
                r.FillRect(pos, rc.Size, s.Widget * Vec4(1.0f, 1.0f, 1.0f, 1.0f - item.Focus));
            text(item.Text, rc, pos.x + rc.Size.x * 0.5f, item.Held ? s.Highlight : color,
                 TextAlign::Center);
            break;
        case UiKind::Toggle: {
            text(item.Text, rc, pos.x + inset, color, TextAlign::Left);
            const f32 box = std::round(rc.Size.y * 0.5f);
            const Vec2 at{right - s.Spacing - box, std::round(pos.y + (rc.Size.y - box) * 0.5f)};
            r.DrawRect(at, Vec2(box), color);
            if (item.Amount > 0.0f)
                r.FillRect(at + Vec2(3.0f), Vec2(box - 6.0f), s.Highlight);
            break;
        }
        case UiKind::Slider: {
            text(item.Text, rc, pos.x + inset, color, TextAlign::Left);
            const f32 y = std::round(pos.y + rc.Size.y * 0.5f) - 2.0f;
            const f32 filled = std::round(valueW * item.Amount);
            r.FillRect({valueX, y}, {valueW, 4.0f}, s.Widget);
            r.FillRect({valueX, y}, {filled, 4.0f}, s.Highlight);
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
