#include "Emerald/Input/InputRecording.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "Emerald/Core/Log.h"
#include "Emerald/Input/Input.h"

namespace Emerald {

namespace {

constexpr std::string_view kMagic = "emerald-input";
constexpr i32 kFormatVersion = 1;

// The words of a line, split at spaces.
std::vector<std::string_view> Words(std::string_view line)
{
    std::vector<std::string_view> words;
    while (!line.empty()) {
        const usize start = line.find_first_not_of(' ');
        if (start == std::string_view::npos)
            break;
        const usize end = line.find(' ', start);
        words.push_back(line.substr(start, end - start));
        line = end == std::string_view::npos ? std::string_view() : line.substr(end);
    }
    return words;
}

bool ParseU64(std::string_view text, u64& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

// Floats as text: 9 significant digits read back as exactly the same f32.
bool ParseFloat(std::string_view text, f64& value)
{
    const std::string copy(text); // strtod wants a terminated string
    char* end = nullptr;
    value = std::strtod(copy.c_str(), &end);
    return !copy.empty() && end == copy.c_str() + copy.size();
}

void WriteSample(std::string& out, const InputSample& sample)
{
    // The action flags as one digit each ("-" for none), then the axes.
    out += ' ';
    for (const u8 flags : sample.Actions)
        out += static_cast<char>('0' + flags);
    if (sample.Actions.empty())
        out += '-';
    for (const f32 axis : sample.Axes) {
        char number[32];
        std::snprintf(number, sizeof(number), " %.9g", static_cast<f64>(axis));
        out += number;
    }
    out += '\n';
}

// Reads a sample from words[first...]: the flags word, then one word per axis.
bool ReadSample(const std::vector<std::string_view>& words, usize first, const InputNames& names,
                InputSample& sample)
{
    if (words.size() != first + 1 + names.Axes.size())
        return false;
    const std::string_view flags = words[first];
    if (names.Actions.empty() ? flags != "-" : flags.size() != names.Actions.size())
        return false;
    for (const char c : names.Actions.empty() ? std::string_view() : flags) {
        if (c < '0' || c > '7')
            return false;
        sample.Actions.push_back(static_cast<u8>(c - '0'));
    }
    for (usize i = 0; i < names.Axes.size(); ++i) {
        f64 value = 0.0;
        if (!ParseFloat(words[first + 1 + i], value))
            return false;
        sample.Axes.push_back(static_cast<f32>(value));
    }
    return true;
}

} // namespace

std::string InputRecording::ToText() const
{
    std::string out = std::string(kMagic) + " " + std::to_string(kFormatVersion) + "\n";
    out += "engine " + (EngineVersion.empty() ? std::string("unknown") : EngineVersion) + "\n";
    out += "seed " + std::to_string(Seed) + "\n";
    char rate[32];
    std::snprintf(rate, sizeof(rate), "%.17g", FixedRate);
    out += std::string("fixed-rate ") + rate + "\n";
    out += "actions";
    for (const std::string& name : Names.Actions)
        out += " " + name;
    out += "\naxes";
    for (const std::string& name : Names.Axes)
        out += " " + name;
    out += "\n";
    for (const Frame& frame : Frames) {
        out += "frame " + std::to_string(frame.Nanoseconds);
        WriteSample(out, frame.Update);
        for (const InputSample& step : frame.Steps) {
            out += "step";
            WriteSample(out, step);
        }
    }
    return out;
}

std::optional<InputRecording> InputRecording::FromText(std::string_view text, std::string* error)
{
    InputRecording recording;
    usize lineNumber = 0;
    const auto fail = [&](const std::string& why) -> std::optional<InputRecording> {
        if (error)
            *error = "line " + std::to_string(lineNumber) + ": " + why;
        return std::nullopt;
    };

    // The header lines in this order, then frames and steps.
    constexpr std::string_view kHeader[] = {kMagic,       "engine",  "seed",
                                            "fixed-rate", "actions", "axes"};
    while (!text.empty()) {
        const usize end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1); // a file saved with Windows line ends
        ++lineNumber;
        const std::vector<std::string_view> words = Words(line);
        if (words.empty())
            continue;

        const usize headerIndex = lineNumber - 1;
        if (headerIndex < std::size(kHeader)) {
            if (words[0] != kHeader[headerIndex])
                return fail("expected '" + std::string(kHeader[headerIndex]) + "'");
            u64 number = 0;
            f64 rate = 0.0;
            switch (headerIndex) {
            case 0:
                if (words.size() != 2 || !ParseU64(words[1], number) || number != kFormatVersion)
                    return fail("not an Emerald input recording of version 1");
                break;
            case 1:
                recording.EngineVersion = words.size() > 1 ? std::string(words[1]) : "";
                break;
            case 2:
                if (words.size() != 2 || !ParseU64(words[1], recording.Seed))
                    return fail("bad seed");
                break;
            case 3:
                if (words.size() != 2 || !ParseFloat(words[1], rate) || rate <= 0.0)
                    return fail("bad fixed rate");
                recording.FixedRate = rate;
                break;
            default: // actions, axes
                for (usize i = 1; i < words.size(); ++i)
                    (headerIndex == 4 ? recording.Names.Actions : recording.Names.Axes)
                        .emplace_back(words[i]);
                break;
            }
            continue;
        }

        InputSample sample;
        if (words[0] == "frame") {
            Frame frame;
            if (words.size() < 2 || !ParseU64(words[1], frame.Nanoseconds) ||
                !ReadSample(words, 2, recording.Names, frame.Update))
                return fail("bad frame");
            recording.Frames.push_back(std::move(frame));
        } else if (words[0] == "step") {
            if (recording.Frames.empty() || !ReadSample(words, 1, recording.Names, sample))
                return fail("bad step");
            recording.Frames.back().Steps.push_back(std::move(sample));
        } else {
            return fail("unknown line '" + std::string(words[0]) + "'");
        }
    }
    if (lineNumber < std::size(kHeader))
        return fail("the header is incomplete");
    return recording;
}

bool InputRecording::Save(const std::filesystem::path& path) const
{
    for (const auto* list : {&Names.Actions, &Names.Axes}) {
        for (const std::string& name : *list) {
            if (name.empty() || name.find_first_of(" \t\r\n") != std::string::npos) {
                EM_CORE_ERROR("Input recording: '{}' cannot be saved (names need no spaces)", name);
                return false;
            }
        }
    }
    std::ofstream file(path, std::ios::binary);
    file << ToText();
    if (!file) {
        EM_CORE_ERROR("Input recording: cannot write {}", path.string());
        return false;
    }
    EM_CORE_INFO("Input recording: {} frames saved to {}", Frames.size(), path.string());
    return true;
}

std::optional<InputRecording> InputRecording::Load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        EM_CORE_ERROR("Input recording: cannot open {}", path.string());
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::string error;
    std::optional<InputRecording> recording = FromText(text.str(), &error);
    if (!recording)
        EM_CORE_ERROR("Input recording {}: {}", path.string(), error);
    return recording;
}

// ---------------------------------------------------------------------------
// InputSession
// ---------------------------------------------------------------------------

void InputSession::StartRecording(Input& input, std::string engineVersion, u64 seed, f64 fixedRate)
{
    Stop();
    m_Mode = Mode::Record;
    m_Input = &input;
    m_Recording = {};
    m_Recording.EngineVersion = std::move(engineVersion);
    m_Recording.Seed = seed;
    m_Recording.FixedRate = fixedRate;
    m_Recording.Names = input.GetNames();
}

bool InputSession::StartReplay(Input& input, InputRecording recording, f64 fixedRate)
{
    Stop();
    if (recording.FixedRate != fixedRate) {
        EM_CORE_ERROR("Replay: recorded at {} fixed steps per second, the game runs {}",
                      recording.FixedRate, fixedRate);
        return false;
    }
    m_Mode = Mode::Replay;
    m_Input = &input;
    m_Recording = std::move(recording);
    m_Frame = 0;
    m_Step = 0;
    m_Started = false;
    m_Diverged = false;
    m_Empty.Actions.assign(m_Recording.Names.Actions.size(), 0);
    m_Empty.Axes.assign(m_Recording.Names.Axes.size(), 0.0f);
    return true;
}

void InputSession::Stop()
{
    if (m_Input)
        m_Input->SetReplay(nullptr, nullptr);
    m_Mode = Mode::Off;
    m_Input = nullptr;
}

bool InputSession::BeginFrame(u64& elapsedNs)
{
    if (m_Mode == Mode::Record) {
        m_Recording.Frames.push_back({.Nanoseconds = elapsedNs, .Update = {}, .Steps = {}});
        return true;
    }
    if (m_Mode != Mode::Replay)
        return true;
    if (m_Started) {
        CheckStepCount();
        ++m_Frame;
    }
    m_Started = true;
    m_Step = 0;
    if (m_Frame >= m_Recording.Frames.size()) {
        m_Input->SetReplay(nullptr, nullptr);
        return false;
    }
    elapsedNs = m_Recording.Frames[m_Frame].Nanoseconds;
    return true;
}

void InputSession::BeginStep()
{
    if (m_Mode == Mode::Record) {
        m_Recording.Frames.back().Steps.push_back(m_Input->Capture(m_Recording.Names));
    } else if (m_Mode == Mode::Replay && m_Frame < m_Recording.Frames.size()) {
        const std::vector<InputSample>& steps = m_Recording.Frames[m_Frame].Steps;
        m_Input->SetReplay(m_Step < steps.size() ? &steps[m_Step] : &m_Empty, &m_Recording.Names);
        ++m_Step;
    }
}

void InputSession::BeginUpdate()
{
    if (m_Mode == Mode::Record)
        m_Recording.Frames.back().Update = m_Input->Capture(m_Recording.Names);
    else if (m_Mode == Mode::Replay && m_Frame < m_Recording.Frames.size())
        m_Input->SetReplay(&m_Recording.Frames[m_Frame].Update, &m_Recording.Names);
}

void InputSession::CheckStepCount()
{
    if (m_Diverged || m_Step == m_Recording.Frames[m_Frame].Steps.size())
        return;
    m_Diverged = true;
    EM_CORE_WARN("Replay: frame {} ran {} fixed steps, the recording has {}; it is no longer "
                 "exact (another MaxFixedStepsPerFrame?)",
                 m_Frame, m_Step, m_Recording.Frames[m_Frame].Steps.size());
}

} // namespace Emerald
