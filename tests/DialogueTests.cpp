// Branching dialogue (#23): decks from JSON, conditions and effects, walking, saving, and the
// dialogue box (typing and answers) without a GPU. The D3 tests port the behaviour of the
// samples that ship with Jari Komppa's DialogTree (examples/simple.txt, examples/test_flags.txt)
// and of its tag concepts (single-use answers, automatic cards, a tag store shared by all
// decks); the data is rewritten in Emerald's JSON format and no D3 code is used.

#include <filesystem>
#include <string>

#include <Emerald/Core/Log.h>
#include <Emerald/Dialogue/Dialogue.h>
#include <Emerald/Dialogue/DialogueBox.h>
#include <Emerald/Renderer/Font.h>
#include <Emerald/Save/Save.h>

#include "Test.h"

using namespace Emerald;

namespace {

DialogueDeck Parse(std::string_view json)
{
    std::optional<DialogueDeck> deck = ParseDialogue(json, "test");
    CHECK(deck.has_value());
    return deck ? std::move(*deck) : DialogueDeck{};
}

// The visible answers' texts, joined with '|'.
std::string Answers(const Dialogue& d)
{
    std::string out;
    for (usize i = 0; i < d.GetAnswerCount(); ++i)
        out += (i ? "|" : "") + d.GetAnswer(i).Text;
    return out;
}

bool Contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

// D3's examples/simple.txt: a captive in a dark room.
constexpr std::string_view kSimple = R"json({
  "id": "simple",
  "cards": [
    { "id": "start", "text": "You're captive in a dark room. There's a door, a window and a trapdoor.",
      "answers": [ { "text": "Try the window", "goto": "window" },
                   { "text": "Try the door", "goto": "door" },
                   { "text": "Try the trapdoor", "goto": "trapdoor" } ] },
    { "id": "window", "text": "Try as you might, the window is out of reach.",
      "answers": [ { "text": "Look around", "goto": "start" } ] },
    { "id": "door", "text": "The door is locked",
      "answers": [ { "text": "Try the window instead", "goto": "window" },
                   { "text": "Try the trapdoor instead", "goto": "trapdoor" } ] },
    { "id": "trapdoor", "text": "The trapdoor opens easily, but leads to darkness, and there's no ladder.",
      "answers": [ { "text": "Wait for a while", "goto": "trapdoor" },
                   { "text": "Look around", "goto": "start" },
                   { "text": "Jump down to the darkness", "goto": "end" } ] },
    { "id": "end", "text": "The way down is much longer than you expected." }
  ]
})json";

// D3's examples/test_flags.txt: every line marked ERROR must stay hidden, whatever is chosen.
constexpr std::string_view kFlags = R"json({
  "id": "flags",
  "cards": [
    { "id": "testroom", "if": "has:unused", "do": "set:headerset",
      "text": [
        "Flag test",
        { "if": "has:unused", "do": "set:bodyset" },
        { "if": "flags.testroom", "text": "testroom is set (ok)" },
        { "if": "!flags.testroom", "text": "testroom is not set (ERROR)" },
        { "if": "has:headerset", "text": "headerset is set (ERROR)" },
        { "if": "not:headerset", "text": "headerset is not set (ok)" },
        { "if": "has:bodyset", "text": "bodyset is set (ERROR)" },
        { "if": "not:bodyset", "text": "bodyset is not set (ok)" },
        { "if": "unused", "text": "unused is set (ERROR)" },
        { "if": "!unused", "text": "unused is not set (ok)" },
        { "if": "toggle", "text": "toggle is set" },
        { "if": "!toggle", "text": "toggle is not set" },
        { "if": "toggle !toggle", "text": "toggle is both set and not set (ERROR)" }
      ],
      "answers": [
        { "goto": "testroom", "do": "set:toggle", "text": "Set toggle" },
        { "goto": "testroom", "do": "clr:toggle", "text": "Clear toggle" },
        { "goto": "testroom", "do": "xor:toggle", "text": "Toggle toggle" },
        { "goto": "testroom", "if": "bodyset", "text": "Bodyset is set (ERROR)" },
        { "goto": "testroom", "if": "headerset", "text": "Headerset is set (ERROR)" },
        { "goto": "testroom", "if": "unused", "text": "Unused is set (ERROR)" },
        { "goto": "testroom", "if": "toggle", "text": "Toggle is set" },
        { "goto": "testroom", "if": "not:toggle", "text": "Toggle is not set" },
        { "goto": "testroom", "if": "flags.testroom", "text": "Testroom is set (ok)" },
        { "goto": "testroom", "if": "!flags.testroom", "text": "Testroom is not set (ERROR)" }
      ] }
  ]
})json";

// D3's tag concepts: an automatic first card picks "first time" or "again" by a tag, answers are
// single use or need a tag that another character's deck sets.
constexpr std::string_view kGatekeeper = R"json({
  "id": "gatekeeper",
  "speaker": "Gatekeeper",
  "cards": [
    { "id": "start", "answers": [ { "goto": "first", "if": "!met_gatekeeper" }, { "goto": "again" } ] },
    { "id": "first", "do": "set:met_gatekeeper", "text": "Halt. Who goes there?", "next": "menu" },
    { "id": "again", "text": "You again.", "next": "menu" },
    { "id": "menu",
      "answers": [ { "text": "Ask about the city", "goto": "city", "once": true },
                   { "text": "Ask about the murder", "goto": "murder", "if": "heard_murder" },
                   { "text": "Leave" } ] },
    { "id": "city", "text": "Old walls, older rules.", "next": "menu" },
    { "id": "murder", "text": "Keep your voice down.", "next": "menu" }
  ]
})json";

constexpr std::string_view kBarkeep = R"json({
  "id": "barkeep",
  "cards": [ { "id": "start", "text": "Did you hear? Someone was found by the gate.", "do": "set:heard_murder" } ]
})json";

} // namespace

TEST(DialogueTermsParse)
{
    Log::Init({}); // console only: broken decks and missing cards are logged
    DialogueConditions c;
    CHECK(ParseDialogueConditions("met !angry has:x not:y gold>=5 rep<0 stage==2 kills!=3", c));
    CHECK(c.size() == 8);
    CHECK(c[1].Kind == DialogueCondition::Op::Not && c[1].Name == "angry");
    CHECK(c[4].Kind == DialogueCondition::Op::GreaterEqual && c[4].Value == 5);
    CHECK(c[5].Kind == DialogueCondition::Op::Less && c[5].Value == 0);

    DialogueEffects e;
    CHECK(ParseDialogueEffects(
        "set:a clear:b clr:c toggle:d xor:e gold=-5 gold+=3 gold-=2 hp-1 quest.started", e));
    CHECK(e.size() == 10);
    CHECK(e[9].Kind == DialogueEffect::Op::Set && e[9].Name == "quest.started");
    CHECK(e[5].Kind == DialogueEffect::Op::Assign && e[5].Value == -5);
    CHECK(e[7].Kind == DialogueEffect::Op::Add && e[7].Value == -2);
    CHECK(e[8].Kind == DialogueEffect::Op::Add && e[8].Value == -1 && e[8].Name == "hp");

    DialogueFlags flags;
    ApplyEffects(e, flags);
    CHECK(flags.Has("a") && flags.Has("d") && !flags.Has("b") && flags.GetValue("gold") == -4);
    CHECK(flags.GetValue("hp") == -1 && flags.Has("quest.started"));
    DialogueConditions rich;
    CHECK(ParseDialogueConditions("a gold<0 gold>-6", rich) && CheckConditions(rich, flags));
    CHECK(!CheckConditions(c, flags));

    // Mistakes are reported, naming the term.
    std::string error;
    DialogueConditions bad;
    CHECK(!ParseDialogueConditions("ok gold=5", bad, &error) && Contains(error, "gold=5"));
    CHECK(!ParseDialogueConditions("bad-name", bad));
    DialogueEffects badEffects;
    CHECK(!ParseDialogueEffects("bogus:x", badEffects, &error) && Contains(error, "bogus:x"));
    CHECK(!ParseDialogueEffects("gold>=5", badEffects));
    CHECK(!ParseDialogueEffects("set:", badEffects));
}

TEST(DialogueD3SimpleWalk)
{
    const DialogueDeck deck = Parse(kSimple);
    DialogueFlags flags;
    Dialogue d(deck, flags);
    CHECK(d.Start() && d.GetCardId() == "start" && d.HasChoices());
    CHECK(Answers(d) == "Try the window|Try the door|Try the trapdoor");
    CHECK(d.Choose(1) && d.GetText() == "The door is locked");
    CHECK(d.Choose(1) && d.GetCardId() == "trapdoor");
    const u32 step = d.GetStep();
    CHECK(d.Choose(0) && d.GetCardId() == "trapdoor" && d.GetStep() == step + 1); // same card
    CHECK(!d.Choose(7) && d.GetCardId() == "trapdoor");                           // no such answer
    CHECK(!d.Advance()); // there are choices: Advance does nothing
    CHECK(d.Choose(2) && d.GetCardId() == "end" && d.GetAnswerCount() == 0 && !d.HasChoices());
    CHECK(Contains(d.GetText(), "much longer"));
    CHECK(d.Advance() && !d.IsActive()); // an end card closes
    CHECK(flags.Has("simple.start") && flags.Has("simple.door") && !flags.Has("simple.window"));
}

TEST(DialogueD3FlagSample)
{
    const DialogueDeck deck = Parse(kFlags);
    DialogueFlags flags;
    Dialogue d(deck, flags);
    CHECK(d.Start());
    const auto noErrors = [&] {
        CHECK(!Contains(d.GetText(), "ERROR"));
        CHECK(!Contains(Answers(d), "ERROR"));
    };
    noErrors();
    CHECK(Contains(d.GetText(), "testroom is set (ok)"));
    CHECK(Contains(d.GetText(), "toggle is not set") && Contains(Answers(d), "Toggle is not set"));
    CHECK(!flags.Has("headerset") && !flags.Has("bodyset")); // their guards failed

    // Set, toggle (off), toggle (on), clear: the text and answers follow every time.
    CHECK(d.Choose(0));
    noErrors();
    CHECK(Contains(d.GetText(), "toggle is set") && Contains(Answers(d), "Toggle is set"));
    CHECK(d.Choose(2));
    noErrors();
    CHECK(Contains(d.GetText(), "toggle is not set"));
    CHECK(d.Choose(2));
    noErrors();
    CHECK(flags.Has("toggle"));
    CHECK(d.Choose(1));
    noErrors();
    CHECK(!flags.Has("toggle") && Contains(d.GetText(), "toggle is not set"));
}

TEST(DialogueD3TagSample)
{
    const DialogueDeck gatekeeper = Parse(kGatekeeper);
    const DialogueDeck barkeep = Parse(kBarkeep);
    DialogueFlags flags; // shared by both characters
    Dialogue gate(gatekeeper, flags);

    // First talk: the automatic start card picks "first".
    CHECK(gate.Start() && gate.GetCardId() == "first" && gate.GetText() == "Halt. Who goes there?");
    CHECK(gate.GetCard()->Speaker == "Gatekeeper");          // the deck's speaker
    CHECK(!gate.HasChoices() && gate.GetAnswerCount() == 1); // "next": a continue
    CHECK(gate.Advance() && gate.GetCardId() == "menu" && gate.GetText().empty());
    CHECK(Answers(gate) == "Ask about the city|Leave"); // no murder yet

    // A single-use answer disappears once picked.
    CHECK(gate.Choose(0) && gate.GetText() == "Old walls, older rules.");
    CHECK(gate.Advance() && Answers(gate) == "Leave");
    CHECK(flags.Has("gatekeeper.menu.a1"));
    CHECK(gate.Choose(0) && !gate.IsActive());

    // Another character sets a tag that opens a new answer here.
    Dialogue bar(barkeep, flags);
    CHECK(bar.Start() && flags.Has("heard_murder"));
    CHECK(gate.Start() && gate.GetText() == "You again."); // met before: the other branch
    CHECK(gate.Advance() && Answers(gate) == "Ask about the murder|Leave");
    CHECK(gate.Choose(0) && gate.GetText() == "Keep your voice down.");
}

TEST(DialogueNumbersGateAnswers)
{
    const DialogueDeck deck = Parse(R"json({
      "id": "shop",
      "cards": [
        { "id": "counter", "do": "visits+=1",
          "text": [ "What will it be?", { "if": "visits>=3", "text": "Back so soon?" } ],
          "answers": [
            { "text": "Buy the sword (5 gold)", "goto": "counter", "if": "gold>=5 !has_sword",
              "do": "gold-=5 set:has_sword", "data": "give:sword" },
            { "text": "Leave" } ] }
      ]
    })json");
    DialogueFlags flags;
    flags.SetValue("gold", 7);
    Dialogue d(deck, flags);
    CHECK(d.Start() && Answers(d) == "Buy the sword (5 gold)|Leave");
    CHECK(d.GetAnswer(0).Data == "give:sword" && d.GetAnswer(9).Text.empty());
    CHECK(d.Choose(0) && flags.GetValue("gold") == 2 && flags.Has("has_sword"));
    CHECK(Answers(d) == "Leave"); // bought, and too poor anyway
    CHECK(!Contains(d.GetText(), "Back so soon?") && flags.GetValue("visits") == 2);
    CHECK(d.Start() && Contains(d.GetText(), "Back so soon?"));
}

TEST(DialogueMissingCardsAndBrokenFiles)
{
    // Broken links load (with warnings) and end the dialogue when used.
    const DialogueDeck deck = Parse(R"json({
      "id": "lost", "start": "hello",
      "cards": [ { "id": "hello", "text": "Hi.", "answers": [ { "text": "Go", "goto": "nowhere" } ] },
                 { "id": "auto", "next": "missing" } ]
    })json");
    DialogueFlags flags;
    Dialogue d(deck, flags);
    CHECK(d.Start() && d.Choose(0) && !d.IsActive());
    CHECK(!d.Start("no_such_card") && !d.IsActive());
    CHECK(!d.Start("auto") && !d.IsActive()); // an automatic card into a missing one
    Dialogue empty;
    CHECK(!empty.Start() && !empty.Choose(0) && !empty.Advance() && empty.GetCard() == nullptr);

    // An automatic loop is cut off.
    const DialogueDeck loop = Parse(R"json({ "id": "loop", "cards": [
      { "id": "a", "next": "b" }, { "id": "b", "next": "a" } ] })json");
    Dialogue looping(loop, flags);
    CHECK(!looping.Start() && !looping.IsActive());

    // Real mistakes refuse the file.
    CHECK(!ParseDialogue("{ not json", "test"));
    CHECK(!ParseDialogue(R"({ "id": "x", "cards": [] })", "test"));
    CHECK(!ParseDialogue(R"({ "cards": [ { "id": "a" } ] })", "test")); // no deck id
    CHECK(!ParseDialogue(R"({ "id": "x", "cards": [ { "id": "a" }, { "id": "a" } ] })", "test"));
    CHECK(!ParseDialogue(R"({ "id": "x", "cards": [ { "id": "bad id" } ] })", "test"));
    CHECK(!ParseDialogue(R"({ "id": "x", "cards": [ { "id": "a", "if": "x=1" } ] })", "test"));
    CHECK(!ParseDialogue(R"({ "id": "x", "cards": [ { "id": "a", "text": 5 } ] })", "test"));
    CHECK(!ParseDialogue(
        R"({ "id": "x", "cards": [ { "id": "a", "answers": [ { "once": 1 } ] } ] })", "test"));
}

TEST(DialogueStateRoundTripsThroughSaves)
{
    const DialogueDeck deck = Parse(kGatekeeper);
    DialogueFlags flags;
    flags.SetValue("gold", 12);
    flags.SetValue("rep", -3);
    Dialogue d(deck, flags);
    CHECK(d.Start() && d.Advance() && d.Choose(0)); // at "city", the city answer used up

    SaveData data;
    flags.Save(data, "npc");
    d.Save(data, "npc.gate");
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "emerald_dialogue_tests";
    std::filesystem::remove_all(folder);
    SaveSystem saves(folder, 1);
    CHECK(saves.Save("slot_1", data));

    const LoadResult loaded = saves.Load("slot_1");
    CHECK(loaded.Data.GetString("npc.gate.card") == "city");
    DialogueFlags restoredFlags;
    restoredFlags.Load(loaded.Data, "npc");
    CHECK(restoredFlags.GetFlags() == flags.GetFlags() &&
          restoredFlags.GetValues() == flags.GetValues());
    Dialogue restored(deck, restoredFlags);
    CHECK(restored.Restore(loaded.Data, "npc.gate"));
    CHECK(restored.GetCardId() == "city" && restored.GetText() == d.GetText());
    CHECK(restored.Advance() && Answers(restored) == "Leave"); // still used up
    CHECK(restoredFlags.GetValue("gold") == 12 && restoredFlags.GetValue("rep") == -3);

    // Restore doesn't run the card's effects again.
    const DialogueDeck counter = Parse(R"json({ "id": "c", "cards": [
      { "id": "a", "do": "visits+=1", "text": "Hi.", "answers": [ { "text": "Bye" } ] } ] })json");
    DialogueFlags counts;
    Dialogue first(counter, counts);
    CHECK(first.Start() && counts.GetValue("visits") == 1);
    SaveData counterSave;
    first.Save(counterSave, "talk");
    Dialogue again(counter, counts);
    CHECK(again.Restore(counterSave, "talk") && counts.GetValue("visits") == 1);

    // A save for another deck, a missing card, or no conversation: not resumed.
    Dialogue other(deck, flags);
    CHECK(!other.Restore(counterSave, "talk"));
    SaveData gone;
    gone.SetString("talk.deck", "gatekeeper");
    gone.SetString("talk.card", "removed");
    CHECK(!other.Restore(gone, "talk") && !other.IsActive());
    first.Stop();
    first.Save(counterSave, "talk");
    CHECK(!again.Restore(counterSave, "talk"));
}

TEST(DialogueRefreshAfterReload)
{
    // What the asset manager does on a hot reload: the same DialogueDeck object, new contents.
    DialogueDeck deck = Parse(kSimple);
    DialogueFlags flags;
    Dialogue d(deck, flags);
    CHECK(d.Start() && d.Choose(1) && d.GetCardId() == "door");
    const u32 step = d.GetStep();

    std::string edited(kSimple);
    edited.replace(edited.find("The door is locked"), 18, "The door is bolted");
    deck = Parse(edited);
    CHECK(d.GetAnswer(0).Text == "Try the window instead"); // safe even before Refresh
    d.Refresh();
    CHECK(d.GetCardId() == "door" && d.GetText() == "The door is bolted" && d.GetStep() > step);

    // The current card was removed: back to the start.
    deck = Parse(R"json({ "id": "simple", "cards": [ { "id": "start", "text": "Again." } ] })json");
    d.Refresh();
    CHECK(d.GetCardId() == "start" && d.GetText() == "Again.");
}

TEST(DialogueWrapText)
{
    const auto measure = [](std::string_view s) { return static_cast<f32>(s.size()) * 10.0f; };
    const std::vector<std::string> lines = WrapText("the quick brown fox\njumps", 100.0f, measure);
    CHECK(lines.size() == 3);
    if (lines.size() == 3)
        CHECK(lines[0] == "the quick" && lines[1] == "brown fox" && lines[2] == "jumps");
    const std::vector<std::string> longWord = WrapText("a extraordinarily b", 50.0f, measure);
    CHECK(longWord.size() == 3 && longWord[1] == "extraordinarily");
    CHECK(WrapText("", 50.0f, measure).size() == 1);
}

TEST(DialogueBoxTypesAndAnswers)
{
    const std::optional<Font> font = Font::Load(nullptr, EMERALD_TEST_FONT, {.Size = 8.0f});
    CHECK(font.has_value());
    if (!font)
        return;
    const DialogueDeck deck = Parse(kSimple);
    DialogueFlags flags;
    Dialogue d(deck, flags);
    DialogueBox box;
    const DialogueBoxOptions options{
        .Position = {320.0f, 360.0f}, .Width = 600.0f, .CharsPerSecond = 100.0f};
    const auto frame = [&](UiInput input, f32 dt = 1.0f / 60.0f) {
        box.Update(d, input, *font, dt, options);
    };
    const auto buttons = [&] {
        std::string out;
        for (const UiItem& item : box.GetUi().GetItems())
            if (item.Kind == UiKind::Button)
                out += (out.empty() ? "" : "|") + item.Text;
        return out;
    };
    UiInput accept;
    accept.Accept = {.Held = true, .Pressed = true, .Released = false};
    UiInput down;
    down.Down = {.Held = true, .Pressed = true, .Released = false};

    frame({});
    CHECK(!box.IsOpen()); // not started
    CHECK(d.Start());
    frame({}, 0.1f); // types ~10 of 71 characters
    CHECK(box.IsOpen() && box.IsTyping());
    CHECK(!box.GetShownText().empty() && box.GetShownText().size() < d.GetText().size());
    CHECK(buttons().empty()); // no answers while typing

    frame(accept); // finishes the line at once...
    CHECK(!box.IsTyping() && box.GetShownText() == d.GetText());
    CHECK(d.GetCardId() == "start"); // ...without picking an answer
    frame(accept);                   // the answers show a frame after the text is complete,
    CHECK(buttons().empty());        // so pressing again right away picks nothing either
    CHECK(d.GetCardId() == "start");
    frame({});
    CHECK(buttons() == "Try the window|Try the door|Try the trapdoor");
    frame(down); // keyboard / d-pad: the focus moves to the door
    frame(accept);
    CHECK(d.GetCardId() == "door");

    // The mouse: wait for the text, then click "Try the trapdoor instead".
    frame({}, 5.0f);
    frame({}); // the answers appear
    frame({}); // the bottom-anchored box has its new height (the UI sizes panels a frame late)
    UiRect target;
    for (const UiItem& item : box.GetUi().GetItems())
        if (item.Text == "Try the trapdoor instead")
            target = item.Rect;
    UiInput press;
    press.Mouse = target.GetCenter();
    press.MouseMoved = true;
    press.Click = {.Held = true, .Pressed = true, .Released = false};
    frame(press);
    UiInput release;
    release.Mouse = target.GetCenter();
    release.Click = {.Held = false, .Pressed = false, .Released = true};
    frame(release);
    CHECK(d.GetCardId() == "trapdoor");

    // An end card offers Close, which ends the dialogue and closes the box.
    CHECK(d.Choose(2) && d.GetCardId() == "end");
    frame({}, 5.0f);
    frame({});
    CHECK(buttons() == "Close");
    frame(accept);
    CHECK(!d.IsActive());
    frame({});
    CHECK(!box.IsOpen());
}
