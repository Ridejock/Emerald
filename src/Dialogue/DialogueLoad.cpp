// Reading dialogue decks from JSON. nlohmann::json stays in here: the public header only has
// the plain structs.

#include <atomic>
#include <fstream>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

#include "Emerald/Core/Log.h"
#include "Emerald/Dialogue/Dialogue.h"

namespace Emerald {

namespace {

using Json = nlohmann::json;

bool IsValidId(std::string_view id)
{
    if (id.empty())
        return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
    }
    return true;
}

// Walks the JSON and remembers the first problem (with where it was), so the parse code can
// read straight through and check once at the end.
class Reader {
public:
    std::string Error;
    std::string Where; // "card 'gate'", for the message

    bool Ok() const { return Error.empty(); }

    void Fail(const std::string& message)
    {
        if (Error.empty())
            Error = Where.empty() ? message : Where + ": " + message;
    }

    // An optional string member: "" if missing, an error if it is something else.
    std::string String(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end() || it->is_null())
            return {};
        if (!it->is_string()) {
            Fail(std::string("'") + key + "' must be a string");
            return {};
        }
        return it->get<std::string>();
    }

    bool Bool(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end())
            return false;
        if (!it->is_boolean()) {
            Fail(std::string("'") + key + "' must be true or false");
            return false;
        }
        return it->get<bool>();
    }

    DialogueConditions Conditions(const Json& object)
    {
        DialogueConditions out;
        std::string why;
        if (!ParseDialogueConditions(String(object, "if"), out, &why))
            Fail("'if': " + why);
        return out;
    }

    DialogueEffects Effects(const Json& object)
    {
        DialogueEffects out;
        std::string why;
        if (!ParseDialogueEffects(String(object, "do"), out, &why))
            Fail("'do': " + why);
        return out;
    }
};

// "text": "One line." or ["A line.", {"text": "Only sometimes.", "if": "flag", "do": "..."}]
std::vector<DialogueLine> ReadLines(Reader& reader, const Json& card)
{
    std::vector<DialogueLine> lines;
    const auto it = card.find("text");
    if (it == card.end() || it->is_null())
        return lines;
    if (it->is_string()) {
        lines.push_back({.Text = it->get<std::string>(), .If = {}, .Do = {}});
        return lines;
    }
    if (!it->is_array()) {
        reader.Fail("'text' must be a string or a list");
        return lines;
    }
    for (const Json& entry : *it) {
        if (entry.is_string()) {
            lines.push_back({.Text = entry.get<std::string>(), .If = {}, .Do = {}});
        } else if (entry.is_object()) {
            DialogueLine line;
            line.Text = reader.String(entry, "text");
            line.If = reader.Conditions(entry);
            line.Do = reader.Effects(entry);
            lines.push_back(std::move(line));
        } else {
            reader.Fail("'text' entries must be strings or objects");
        }
    }
    return lines;
}

std::vector<DialogueAnswer> ReadAnswers(Reader& reader, const Json& card)
{
    std::vector<DialogueAnswer> answers;
    const auto it = card.find("answers");
    if (it != card.end()) {
        if (!it->is_array()) {
            reader.Fail("'answers' must be a list");
            return answers;
        }
        for (const Json& entry : *it) {
            if (!entry.is_object()) {
                reader.Fail("answers must be objects");
                continue;
            }
            DialogueAnswer answer;
            answer.Text = reader.String(entry, "text");
            answer.Goto = reader.String(entry, "goto");
            answer.If = reader.Conditions(entry);
            answer.Do = reader.Effects(entry);
            answer.Once = reader.Bool(entry, "once");
            answer.Data = reader.String(entry, "data");
            answers.push_back(std::move(answer));
        }
    }
    // "next": "card" is short for one answer without text: the line continues there.
    if (const std::string next = reader.String(card, "next"); !next.empty())
        answers.push_back(
            {.Text = {}, .Goto = next, .If = {}, .Do = {}, .Once = false, .Data = {}});
    return answers;
}

std::atomic<u32> s_Revision{0};

} // namespace

std::optional<DialogueDeck> ParseDialogue(std::string_view json, std::string_view source)
{
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        EM_CORE_ERROR("Dialogue {}: not a JSON object", source);
        return std::nullopt;
    }

    Reader reader;
    DialogueDeck deck;
    deck.Id = reader.String(root, "id");
    deck.Start = reader.String(root, "start");
    deck.Data = reader.String(root, "data");
    const std::string speaker = reader.String(root, "speaker");
    const std::string portrait = reader.String(root, "portrait");
    if (!IsValidId(deck.Id))
        reader.Fail("the deck needs an 'id' (letters, digits and _)");

    const auto cards = root.find("cards");
    if (cards == root.end() || !cards->is_array() || cards->empty()) {
        EM_CORE_ERROR("Dialogue {}: 'cards' must be a list with at least one card", source);
        return std::nullopt;
    }
    std::set<std::string, std::less<>> ids;
    for (const Json& entry : *cards) {
        if (!entry.is_object()) {
            reader.Fail("cards must be objects");
            break;
        }
        DialogueCard card;
        card.Id = reader.String(entry, "id");
        reader.Where = "card '" + card.Id + "'";
        if (!IsValidId(card.Id))
            reader.Fail("card ids are letters, digits and _");
        else if (!ids.insert(card.Id).second)
            reader.Fail("there is another card with this id");
        card.Speaker = reader.String(entry, "speaker");
        card.Portrait = reader.String(entry, "portrait");
        card.Data = reader.String(entry, "data");
        if (card.Speaker.empty())
            card.Speaker = speaker;
        if (card.Portrait.empty())
            card.Portrait = portrait;
        card.If = reader.Conditions(entry);
        card.Do = reader.Effects(entry);
        card.Lines = ReadLines(reader, entry);
        card.Answers = ReadAnswers(reader, entry);
        deck.Cards.push_back(std::move(card));
        if (!reader.Ok())
            break;
    }
    if (!reader.Ok()) {
        EM_CORE_ERROR("Dialogue {}: {}", source, reader.Error);
        return std::nullopt;
    }

    // Broken links are only warnings: the rest of the deck still works, and choosing such an
    // answer ends the dialogue (logged again then).
    if (deck.Start.empty())
        deck.Start = deck.Cards.front().Id;
    if (!deck.Find(deck.Start))
        EM_CORE_WARN("Dialogue {}: the start card '{}' doesn't exist", source, deck.Start);
    for (const DialogueCard& card : deck.Cards) {
        for (const DialogueAnswer& answer : card.Answers) {
            if (!answer.Goto.empty() && !deck.Find(answer.Goto))
                EM_CORE_WARN("Dialogue {}: card '{}' has an answer to missing card '{}'", source,
                             card.Id, answer.Goto);
        }
    }
    deck.Revision = ++s_Revision;
    return deck;
}

std::optional<DialogueDeck> LoadDialogue(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        EM_CORE_ERROR("Dialogue: can't open {}", file.string());
        return std::nullopt;
    }
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseDialogue(json, file.filename().string());
}

} // namespace Emerald
