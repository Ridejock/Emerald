#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

class SaveData;

// Branching dialogue, modeled on Jari Komppa's DialogTree (D3) and written fresh: a conversation
// is a deck of cards; each card has text (lines that can depend on flags) and answers that jump
// to other cards. Conditions read flags and numbers; effects set, clear and change them. This
// header is only the logic (no drawing): DialogueBox.h shows a Dialogue on screen.
//
//   DialogueFlags flags;                                 // one store for the whole game
//   AssetHandle<DialogueDeck> guard = GetAssets().Load<DialogueDeck>("assets/dialogue/guard.json");
//   Dialogue talk(*guard, flags);
//   talk.Start();
//   talk.GetText();                                      // what the card says
//   for (usize i = 0; i < talk.GetAnswerCount(); ++i)    // the answers you may pick now
//       talk.GetAnswer(i).Text;
//   talk.Choose(1);                                      // effects, then the next card
//
// See README.md ("Dialogue") for the JSON format.

// ---------------------------------------------------------------------------------------------
// Flags and numbers
// ---------------------------------------------------------------------------------------------

// What conditions read and effects write: flags (on or off) and integer values (0 until set).
// One store is shared by every dialogue and by the game (quest stages, kill counts, gold), so
// talking to one character can change what another one says. Names are letters, digits, '_' and
// '.' (e.g. "quest.wolves.started").
class DialogueFlags {
public:
    [[nodiscard]] bool Has(std::string_view flag) const;
    void Set(std::string_view flag);
    void Clear(std::string_view flag);
    void Toggle(std::string_view flag);

    [[nodiscard]] i32 GetValue(std::string_view name) const;
    void SetValue(std::string_view name, i32 value);
    void AddValue(std::string_view name, i32 amount);

    void Reset(); // every flag off, every value 0
    [[nodiscard]] const std::set<std::string, std::less<>>& GetFlags() const { return m_Flags; }
    [[nodiscard]] const std::map<std::string, i32, std::less<>>& GetValues() const
    {
        return m_Values;
    }

    // Through the save system: "<prefix>.flags" = "a b c", "<prefix>.values" = "gold=5 rep=-2".
    void Save(SaveData& data, std::string_view prefix) const;
    void Load(const SaveData& data, std::string_view prefix); // replaces everything

    [[nodiscard]] static bool IsValidName(std::string_view name);

private:
    std::set<std::string, std::less<>> m_Flags;
    std::map<std::string, i32, std::less<>> m_Values; // only values that aren't 0
};

// One term of a condition: "met_guard", "!met_guard", "gold>=5".
struct DialogueCondition {
    enum class Op : u8 { Has, Not, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
    Op Kind = Op::Has;
    std::string Name;
    i32 Value = 0; // the comparisons' right side
};

// One effect: "met_guard" (or "set:met_guard"), "clear:x", "toggle:x", "gold=5", "gold+=5",
// "gold-=5".
struct DialogueEffect {
    enum class Op : u8 { Set, Clear, Toggle, Assign, Add };
    Op Kind = Op::Set;
    std::string Name;
    i32 Value = 0; // Assign / Add ("-=" is Add with a negative value)
};

using DialogueConditions = std::vector<DialogueCondition>; // all must hold
using DialogueEffects = std::vector<DialogueEffect>;       // applied in order

// Space-separated terms, as in the JSON files. Conditions: "flag", "!flag" (also D3's
// "has:flag" / "not:flag"), "name==3", "!=", "<", "<=", ">", ">=". Effects: "flag" or
// "set:flag", "clear:flag" ("clr:"), "toggle:flag" ("xor:"), "name=3", "name+=3", "name-=3".
// Return false
// (with `error` naming the term) on a term they don't understand.
bool ParseDialogueConditions(std::string_view text, DialogueConditions& out,
                             std::string* error = nullptr);
bool ParseDialogueEffects(std::string_view text, DialogueEffects& out,
                          std::string* error = nullptr);
[[nodiscard]] bool CheckConditions(const DialogueConditions& conditions,
                                   const DialogueFlags& flags);
void ApplyEffects(const DialogueEffects& effects, DialogueFlags& flags);

// ---------------------------------------------------------------------------------------------
// Decks
// ---------------------------------------------------------------------------------------------

// A piece of a card's text, shown if its condition holds; its effects run when it is shown.
struct DialogueLine {
    std::string Text;
    DialogueConditions If;
    DialogueEffects Do;
};

struct DialogueAnswer {
    std::string Text;      // empty: a "continue" answer (or an automatic one, see Dialogue)
    std::string Goto;      // the next card; empty ends the dialogue
    DialogueConditions If; // only offered while this holds
    DialogueEffects Do;    // when picked
    bool Once = false;     // offered until picked once (D3's single-use answers)
    std::string Data;      // for the game: "open_shop", "start_fight", ...
};

struct DialogueCard {
    std::string Id;
    std::string Speaker;  // a name to show (the deck's if the card has none)
    std::string Portrait; // e.g. a sprite name in the game's atlas (the deck's if none)
    std::vector<DialogueLine> Lines;
    DialogueConditions If; // guards Do (the card is shown either way, as in D3)
    DialogueEffects Do;    // when the card is entered
    std::vector<DialogueAnswer> Answers;
    std::string Data; // for the game
};

// One conversation. Card and deck ids are letters, digits and '_'.
struct DialogueDeck {
    std::string Id;
    std::string Start; // the first card (the first in the file if the file doesn't say)
    std::string Data;  // for the game
    std::vector<DialogueCard> Cards;
    // A new number every time a deck is parsed, so a Dialogue notices a hot reload.
    u32 Revision = 0;

    [[nodiscard]] const DialogueCard* Find(std::string_view id) const;
};

// Reads a deck from JSON. Returns nothing (and logs why, naming `source`) for broken JSON,
// duplicate or invalid ids and terms it doesn't understand. Answers that go to missing cards
// are logged as warnings but load: choosing one ends the dialogue.
[[nodiscard]] std::optional<DialogueDeck> ParseDialogue(std::string_view json,
                                                        std::string_view source = "dialogue");
[[nodiscard]] std::optional<DialogueDeck> LoadDialogue(const std::filesystem::path& file);

// ---------------------------------------------------------------------------------------------
// Walking a deck
// ---------------------------------------------------------------------------------------------

// Walks one deck with a flag store. Entering a card sets the flag "<deck>.<card>" (so later text
// can check "was I there?"), runs the card's effects, then works out its text and answers:
//
// - Answers whose condition fails, or Once answers already picked, are hidden.
// - A card with text but no visible answers ends the conversation: Advance() closes it.
// - A card whose only visible answers have no text is a "continue": Advance() takes the first.
// - A card without text whose visible answers have no text either jumps on through the first
//   one at once (D3's automatic cards: e.g. "first time" vs "hello again" picked by flags).
//   A card without text but with choices is a menu that doesn't repeat its question.
//
// The deck and the flags must outlive the Dialogue. Missing cards are logged and end the
// dialogue; nothing crashes.
class Dialogue {
public:
    // Stops automatic cards that jump in a circle.
    static constexpr u32 kMaxAutoSteps = 64;

    Dialogue() = default;
    Dialogue(const DialogueDeck& deck, DialogueFlags& flags) : m_Deck(&deck), m_Flags(&flags) {}

    // Enters `card` (the deck's start if empty). False if there is no such card (logged).
    bool Start(std::string_view card = {});
    void Stop();
    [[nodiscard]] bool IsActive() const { return !m_CardId.empty(); }

    // The current card (null when not active) and what it shows now. The card is looked up by
    // id every time, so it is never a stale pointer, even right after a hot reload.
    [[nodiscard]] const DialogueCard* GetCard() const;
    [[nodiscard]] const std::string& GetCardId() const { return m_CardId; }
    [[nodiscard]] const std::string& GetText() const { return m_Text; } // lines joined by '\n'
    [[nodiscard]] usize GetAnswerCount() const { return m_Answers.size(); }
    // A visible answer (an empty one if `visible` is out of range).
    [[nodiscard]] const DialogueAnswer& GetAnswer(usize visible) const;
    // True if the answers need picking (some have text); false for continue / end cards.
    [[nodiscard]] bool HasChoices() const;

    // Picks a visible answer: its effects, then its card. False if out of range or not active.
    bool Choose(usize visible);
    // Continue (the first answer) on a card without choices, or close an end card.
    bool Advance();

    // Goes up by one for every card entered: a display restarts its typing when it changes.
    [[nodiscard]] u32 GetStep() const { return m_Step; }
    [[nodiscard]] const DialogueDeck* GetDeck() const { return m_Deck; }

    // Picks up a hot-reloaded deck (cheap; call it every frame, DialogueBox does). The card is
    // shown again with the new text and answers; if it was removed, the dialogue restarts at
    // the start card (logged).
    void Refresh();

    // Through the save system: "<prefix>.deck" and "<prefix>.card" (empty when not active).
    // Save the flags too (DialogueFlags::Save). Restore re-enters the card without running its
    // effects again (the flags already have them); false if the deck differs or the card is gone.
    void Save(SaveData& data, std::string_view prefix) const;
    bool Restore(const SaveData& data, std::string_view prefix);

private:
    // Enters `id`; with `runEffects` false (Restore, Refresh) only the text and answers are
    // worked out again.
    bool Enter(std::string_view id, bool runEffects);
    void BuildView(bool runEffects);
    void Pick(const DialogueCard& card, usize answer); // an answer's effects (not its goto)
    void Finish();

    const DialogueDeck* m_Deck = nullptr;
    DialogueFlags* m_Flags = nullptr;
    std::string m_CardId; // empty when not active
    u32 m_Revision = 0;   // the deck's Revision when the view was built
    u32 m_Step = 0;
    std::string m_Text;
    std::vector<usize> m_Answers; // visible answers: indices into the card's Answers
};

} // namespace Emerald
