#include "Emerald/Dialogue/Dialogue.h"

#include <charconv>

#include "Emerald/Core/Log.h"
#include "Emerald/Save/Save.h"

namespace Emerald {

namespace {

bool IsNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.';
}

bool ParseI32(std::string_view text, i32& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && error == std::errc() && end == text.data() + text.size();
}

// Calls fn(term) for each space-separated term; stops at the first false.
template <typename Fn> bool ForEachTerm(std::string_view text, Fn&& fn)
{
    while (!text.empty()) {
        const usize start = text.find_first_not_of(' ');
        if (start == std::string_view::npos)
            break;
        const usize end = text.find(' ', start);
        if (!fn(text.substr(start, end - start)))
            return false;
        text = end == std::string_view::npos ? std::string_view() : text.substr(end);
    }
    return true;
}

// "name<op>number" -> name, op, number. The name runs up to the first non-name character.
bool SplitComparison(std::string_view term, std::string_view& name, std::string_view& op,
                     i32& value)
{
    usize opStart = 0;
    while (opStart < term.size() && IsNameChar(term[opStart]))
        ++opStart;
    usize valueStart = opStart;
    while (valueStart < term.size() &&
           std::string_view("=!<>+-").find(term[valueStart]) != std::string_view::npos)
        ++valueStart;
    // "gold-=5" is "-=" then 5, but "gold=-5" is "=" then -5: give a trailing '-' back.
    if (valueStart > opStart + 1 && term[valueStart - 1] == '-' && valueStart < term.size())
        --valueStart;
    name = term.substr(0, opStart);
    op = term.substr(opStart, valueStart - opStart);
    return opStart > 0 && !op.empty() && ParseI32(term.substr(valueStart), value);
}

void SetError(std::string* error, std::string_view term)
{
    if (error)
        *error = "bad term '" + std::string(term) + "'";
}

const DialogueAnswer kNoAnswer{};

} // namespace

// ---------------------------------------------------------------------------------------------
// DialogueFlags
// ---------------------------------------------------------------------------------------------

bool DialogueFlags::IsValidName(std::string_view name)
{
    if (name.empty())
        return false;
    for (const char c : name) {
        if (!IsNameChar(c))
            return false;
    }
    return true;
}

bool DialogueFlags::Has(std::string_view flag) const
{
    return m_Flags.find(flag) != m_Flags.end();
}

void DialogueFlags::Set(std::string_view flag)
{
    if (IsValidName(flag))
        m_Flags.emplace(flag);
}

void DialogueFlags::Clear(std::string_view flag)
{
    if (const auto it = m_Flags.find(flag); it != m_Flags.end())
        m_Flags.erase(it);
}

void DialogueFlags::Toggle(std::string_view flag)
{
    if (Has(flag))
        Clear(flag);
    else
        Set(flag);
}

i32 DialogueFlags::GetValue(std::string_view name) const
{
    const auto it = m_Values.find(name);
    return it != m_Values.end() ? it->second : 0;
}

void DialogueFlags::SetValue(std::string_view name, i32 value)
{
    if (!IsValidName(name))
        return;
    const auto it = m_Values.find(name);
    if (value == 0) { // 0 is the default: no need to store (or save) it
        if (it != m_Values.end())
            m_Values.erase(it);
    } else if (it != m_Values.end()) {
        it->second = value;
    } else {
        m_Values.emplace(std::string(name), value);
    }
}

void DialogueFlags::AddValue(std::string_view name, i32 amount)
{
    SetValue(name, GetValue(name) + amount);
}

void DialogueFlags::Reset()
{
    m_Flags.clear();
    m_Values.clear();
}

void DialogueFlags::Save(SaveData& data, std::string_view prefix) const
{
    std::string flags;
    for (const std::string& flag : m_Flags)
        flags += (flags.empty() ? "" : " ") + flag;
    std::string values;
    for (const auto& [name, value] : m_Values)
        values += (values.empty() ? "" : " ") + name + "=" + std::to_string(value);
    data.SetString(std::string(prefix) + ".flags", flags);
    data.SetString(std::string(prefix) + ".values", values);
}

void DialogueFlags::Load(const SaveData& data, std::string_view prefix)
{
    Reset();
    ForEachTerm(data.GetString(std::string(prefix) + ".flags"), [&](std::string_view flag) {
        Set(flag);
        return true;
    });
    ForEachTerm(data.GetString(std::string(prefix) + ".values"), [&](std::string_view term) {
        std::string_view name, op;
        i32 value = 0;
        if (SplitComparison(term, name, op, value) && op == "=")
            SetValue(name, value);
        else
            EM_CORE_WARN("DialogueFlags: skipped saved value '{}'", term);
        return true;
    });
}

// ---------------------------------------------------------------------------------------------
// Conditions and effects
// ---------------------------------------------------------------------------------------------

bool ParseDialogueConditions(std::string_view text, DialogueConditions& out, std::string* error)
{
    using Op = DialogueCondition::Op;
    return ForEachTerm(text, [&](std::string_view term) {
        DialogueCondition c;
        if (term.starts_with("has:") || term.starts_with("not:") || term.starts_with("!")) {
            c.Kind = term.starts_with("has:") ? Op::Has : Op::Not;
            c.Name = term.substr(term.starts_with("!") ? 1 : 4);
        } else if (DialogueFlags::IsValidName(term)) {
            c.Kind = Op::Has;
            c.Name = term;
        } else {
            std::string_view name, op;
            if (!SplitComparison(term, name, op, c.Value)) {
                SetError(error, term);
                return false;
            }
            c.Name = name;
            if (op == "==")
                c.Kind = Op::Equal;
            else if (op == "!=")
                c.Kind = Op::NotEqual;
            else if (op == "<")
                c.Kind = Op::Less;
            else if (op == "<=")
                c.Kind = Op::LessEqual;
            else if (op == ">")
                c.Kind = Op::Greater;
            else if (op == ">=")
                c.Kind = Op::GreaterEqual;
            else {
                SetError(error, term); // e.g. "gold=5": an effect, not a condition
                return false;
            }
        }
        if (!DialogueFlags::IsValidName(c.Name)) {
            SetError(error, term);
            return false;
        }
        out.push_back(std::move(c));
        return true;
    });
}

bool ParseDialogueEffects(std::string_view text, DialogueEffects& out, std::string* error)
{
    using Op = DialogueEffect::Op;
    return ForEachTerm(text, [&](std::string_view term) {
        DialogueEffect e;
        const usize colon = term.find(':');
        if (colon != std::string_view::npos) {
            const std::string_view verb = term.substr(0, colon);
            if (verb == "set")
                e.Kind = Op::Set;
            else if (verb == "clear" || verb == "clr")
                e.Kind = Op::Clear;
            else if (verb == "toggle" || verb == "xor")
                e.Kind = Op::Toggle;
            else {
                SetError(error, term);
                return false;
            }
            e.Name = term.substr(colon + 1);
        } else if (DialogueFlags::IsValidName(term)) {
            e.Name = term; // a bare name sets the flag
        } else {
            std::string_view name, op;
            if (!SplitComparison(term, name, op, e.Value)) {
                SetError(error, term);
                return false;
            }
            e.Name = name;
            if (op == "=") {
                e.Kind = Op::Assign;
            } else if (op == "+=" || op == "+") { // D3 also writes "hp+1"
                e.Kind = Op::Add;
            } else if (op == "-=" || op == "-") {
                e.Kind = Op::Add;
                e.Value = -e.Value;
            } else {
                SetError(error, term);
                return false;
            }
        }
        if (!DialogueFlags::IsValidName(e.Name)) {
            SetError(error, term);
            return false;
        }
        out.push_back(std::move(e));
        return true;
    });
}

bool CheckConditions(const DialogueConditions& conditions, const DialogueFlags& flags)
{
    using Op = DialogueCondition::Op;
    for (const DialogueCondition& c : conditions) {
        const i32 v = flags.GetValue(c.Name);
        bool ok = false;
        switch (c.Kind) {
        case Op::Has:
            ok = flags.Has(c.Name);
            break;
        case Op::Not:
            ok = !flags.Has(c.Name);
            break;
        case Op::Equal:
            ok = v == c.Value;
            break;
        case Op::NotEqual:
            ok = v != c.Value;
            break;
        case Op::Less:
            ok = v < c.Value;
            break;
        case Op::LessEqual:
            ok = v <= c.Value;
            break;
        case Op::Greater:
            ok = v > c.Value;
            break;
        case Op::GreaterEqual:
            ok = v >= c.Value;
            break;
        }
        if (!ok)
            return false;
    }
    return true;
}

void ApplyEffects(const DialogueEffects& effects, DialogueFlags& flags)
{
    using Op = DialogueEffect::Op;
    for (const DialogueEffect& e : effects) {
        switch (e.Kind) {
        case Op::Set:
            flags.Set(e.Name);
            break;
        case Op::Clear:
            flags.Clear(e.Name);
            break;
        case Op::Toggle:
            flags.Toggle(e.Name);
            break;
        case Op::Assign:
            flags.SetValue(e.Name, e.Value);
            break;
        case Op::Add:
            flags.AddValue(e.Name, e.Value);
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// DialogueDeck
// ---------------------------------------------------------------------------------------------

const DialogueCard* DialogueDeck::Find(std::string_view id) const
{
    for (const DialogueCard& card : Cards) {
        if (card.Id == id)
            return &card;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Dialogue
// ---------------------------------------------------------------------------------------------

const DialogueCard* Dialogue::GetCard() const
{
    return m_Deck && !m_CardId.empty() ? m_Deck->Find(m_CardId) : nullptr;
}

const DialogueAnswer& Dialogue::GetAnswer(usize visible) const
{
    const DialogueCard* card = GetCard();
    if (!card || visible >= m_Answers.size() || m_Answers[visible] >= card->Answers.size())
        return kNoAnswer; // also covers a reload the game hasn't Refresh()ed yet
    return card->Answers[m_Answers[visible]];
}

bool Dialogue::HasChoices() const
{
    for (usize i = 0; i < m_Answers.size(); ++i) {
        if (!GetAnswer(i).Text.empty())
            return true;
    }
    return false;
}

bool Dialogue::Start(std::string_view card)
{
    if (!m_Deck || !m_Flags) {
        EM_CORE_ERROR("Dialogue: Start without a deck");
        return false;
    }
    return Enter(card.empty() ? std::string_view(m_Deck->Start) : card, true);
}

void Dialogue::Stop()
{
    Finish();
}

void Dialogue::Finish()
{
    m_CardId.clear();
    m_Text.clear();
    m_Answers.clear();
}

bool Dialogue::Choose(usize visible)
{
    const DialogueCard* card = GetCard();
    if (!card || visible >= m_Answers.size() || m_Revision != m_Deck->Revision)
        return false;
    const usize index = m_Answers[visible];
    Pick(*card, index);
    const std::string target = card->Answers[index].Goto; // a copy: Enter changes m_CardId
    if (target.empty()) {
        Finish();
        return true;
    }
    Enter(target, true);
    return true;
}

bool Dialogue::Advance()
{
    if (!IsActive() || HasChoices())
        return false;
    if (m_Answers.empty()) { // an end card
        Finish();
        return true;
    }
    return Choose(0);
}

void Dialogue::Pick(const DialogueCard& card, usize answer)
{
    const DialogueAnswer& a = card.Answers[answer];
    ApplyEffects(a.Do, *m_Flags);
    // Like D3's autogenerated tags: "<deck>.<card>.a1" for the first answer.
    if (a.Once)
        m_Flags->Set(m_Deck->Id + "." + card.Id + ".a" + std::to_string(answer + 1));
}

bool Dialogue::Enter(std::string_view id, bool runEffects)
{
    std::string next(id);
    for (u32 hop = 0; hop < kMaxAutoSteps; ++hop) {
        const DialogueCard* card = m_Deck->Find(next);
        if (!card) {
            EM_CORE_ERROR("Dialogue '{}': no card '{}'; the dialogue ends", m_Deck->Id, next);
            Finish();
            return false;
        }
        m_CardId = card->Id;
        m_Revision = m_Deck->Revision;
        ++m_Step;
        if (runEffects) {
            m_Flags->Set(m_Deck->Id + "." + card->Id); // "visited"
            if (CheckConditions(card->If, *m_Flags))
                ApplyEffects(card->Do, *m_Flags);
        }
        BuildView(runEffects);

        // No text and no choices: an automatic card, which goes on through its first answer.
        // (No text but choices is a menu: "Anything else?" without repeating the question.)
        if (!m_Text.empty() || m_Answers.empty() || HasChoices() || !runEffects)
            return true;
        const usize index = m_Answers.front();
        Pick(*card, index);
        next = card->Answers[index].Goto;
        if (next.empty()) {
            Finish();
            return true;
        }
    }
    EM_CORE_ERROR("Dialogue '{}': more than {} automatic cards in a row (a loop?); the dialogue "
                  "ends",
                  m_Deck->Id, kMaxAutoSteps);
    Finish();
    return false;
}

void Dialogue::BuildView(bool runEffects)
{
    m_Text.clear();
    m_Answers.clear();
    const DialogueCard* card = GetCard();
    if (!card)
        return;
    for (const DialogueLine& line : card->Lines) {
        if (!CheckConditions(line.If, *m_Flags))
            continue;
        if (runEffects)
            ApplyEffects(line.Do, *m_Flags);
        if (!line.Text.empty())
            m_Text += (m_Text.empty() ? "" : "\n") + line.Text;
    }
    for (usize i = 0; i < card->Answers.size(); ++i) {
        const DialogueAnswer& a = card->Answers[i];
        const bool used =
            a.Once && m_Flags->Has(m_Deck->Id + "." + card->Id + ".a" + std::to_string(i + 1));
        if (!used && CheckConditions(a.If, *m_Flags))
            m_Answers.push_back(i);
    }
}

void Dialogue::Refresh()
{
    if (!m_Deck || m_Revision == m_Deck->Revision)
        return;
    m_Revision = m_Deck->Revision;
    if (!IsActive())
        return;
    if (GetCard()) {
        BuildView(false); // same card, new text and answers
        ++m_Step;         // so a display types it again
        return;
    }
    EM_CORE_WARN("Dialogue '{}': card '{}' is gone after a reload; restarting at '{}'", m_Deck->Id,
                 m_CardId, m_Deck->Start);
    Finish();
    Enter(m_Deck->Start, true);
}

void Dialogue::Save(SaveData& data, std::string_view prefix) const
{
    data.SetString(std::string(prefix) + ".deck", m_Deck ? std::string_view(m_Deck->Id) : "");
    data.SetString(std::string(prefix) + ".card", m_CardId);
}

bool Dialogue::Restore(const SaveData& data, std::string_view prefix)
{
    Finish();
    if (!m_Deck || !m_Flags)
        return false;
    const std::string deck = data.GetString(std::string(prefix) + ".deck");
    const std::string card = data.GetString(std::string(prefix) + ".card");
    if (card.empty())
        return false; // saved outside a conversation
    if (deck != m_Deck->Id) {
        EM_CORE_WARN("Dialogue: the save is for deck '{}', not '{}'", deck, m_Deck->Id);
        return false;
    }
    if (!m_Deck->Find(card)) {
        EM_CORE_WARN("Dialogue '{}': saved card '{}' no longer exists", deck, card);
        return false;
    }
    return Enter(card, false);
}

} // namespace Emerald
