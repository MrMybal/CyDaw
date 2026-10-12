#include <cydaw/engine/DrumKit.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace cydaw::engine::drum {

namespace {

struct PieceName {
    const char* key;
    const char* french;
    const char* english;
};
constexpr std::array<PieceName, pieceCount> pieceNames { {
    { "kick", "Grosse caisse", "Kick" },
    { "snare", "Caisse claire", "Snare" },
    { "hihat", "Charleston", "Hi-hat" },
    { "tom1", "Tom 1", "Tom 1" },
    { "tom2", "Tom 2", "Tom 2" },
    { "tom3", "Tom 3", "Tom 3" },
    { "tom4", "Tom basse", "Floor tom" },
    { "crash", "Crash", "Crash" },
    { "ride", "Ride", "Ride" },
    { "china", "China", "China" },
    { "perc", "Percussions", "Percussion" },
    { "other", "Autres", "Other" },
} };

struct Catalogue {
    std::vector<Parameter> parameters;
    std::unordered_map<std::string, int> indices;
};

const Catalogue& catalogue()
{
    static const Catalogue built = [] {
        Catalogue result;
        result.parameters.resize(static_cast<std::size_t>(valueCount));
        const auto set = [&result](const int at, Parameter parameter) { result.parameters[static_cast<std::size_t>(at)] = std::move(parameter); };
        for (int piece = 0; piece < pieceCount; ++piece) {
            const auto& name = pieceNames[static_cast<std::size_t>(piece)];
            const auto key = std::string(name.key) + ".";
            const auto french = std::string(name.french) + " · ";
            const auto english = std::string(name.english) + " · ";
            const auto at = [piece](const Field field) { return piece * fieldCount + field; };
            set(at(fieldLevel), { key + "level", french + "niveau", english + "level", "dB", -40.0f, 6.0f, 0.0f, Kind::continuous, piece, {} });
            set(at(fieldPan), { key + "pan", french + "panoramique", english + "pan", {}, -1.0f, 1.0f, 0.0f, Kind::continuous, piece, {} });
            set(at(fieldTune), { key + "tune", french + "accord", english + "tune", "st", -12.0f, 12.0f, 0.0f, Kind::continuous, piece, {} });
            set(at(fieldDecay), { key + "decay", french + "durée", english + "decay", {}, 0.0f, 1.0f, 1.0f, Kind::continuous, piece, {} });
            set(at(fieldMute), { key + "mute", french + "muet", english + "mute", {}, 0.0f, 1.0f, 0.0f, Kind::toggle, piece, {} });
            set(at(fieldSolo), { key + "solo", french + "solo", english + "solo", {}, 0.0f, 1.0f, 0.0f, Kind::toggle, piece, {} });
        }
        std::vector<std::pair<std::string, std::string>> kitNames;
        for (const auto& kit : kits)
            kitNames.emplace_back(kit.french, kit.english);
        const auto last = static_cast<float>(kitNames.size() - 1);
        set(globalBase + globalKit, { "kit.kit", "Kit", "Kit", {}, 0.0f, last, 0.0f, Kind::choice, -1, kitNames });
        set(globalBase + globalTune, { "kit.tune", "Kit · accord", "Kit · tune", "st", -12.0f, 12.0f, 0.0f, Kind::continuous, -1, {} });
        set(globalBase + globalHumanise, { "kit.humanise", "Kit · jeu humain", "Kit · humanise", {}, 0.0f, 1.0f, 0.0f, Kind::continuous, -1, {} });
        set(globalBase + globalLevel, { "kit.level", "Kit · niveau", "Kit · level", "dB", -40.0f, 6.0f, 0.0f, Kind::continuous, -1, {} });
        for (int at = 0; at < valueCount; ++at)
            result.indices[result.parameters[static_cast<std::size_t>(at)].key] = at;
        return result;
    }();
    return built;
}

} // namespace

const std::vector<Parameter>& parameters()
{
    return catalogue().parameters;
}

const Parameter& parameter(const int index)
{
    const auto& list = catalogue().parameters;
    return list[static_cast<std::size_t>(std::clamp(index, 0, valueCount - 1))];
}

int parameterIndex(const std::string_view key)
{
    const auto& indices = catalogue().indices;
    const auto found = indices.find(std::string(key));
    return found == indices.end() ? -1 : found->second;
}

Values defaultValues()
{
    Values values {};
    for (int at = 0; at < valueCount; ++at)
        values[static_cast<std::size_t>(at)] = parameter(at).defaultValue;
    return values;
}

float clampValue(const int index, const float value)
{
    const auto& setting = parameter(index);
    if (!std::isfinite(value))
        return setting.defaultValue;
    auto clamped = std::clamp(value, setting.minimum, setting.maximum);
    if (setting.kind != Kind::continuous)
        clamped = std::round(clamped);
    return clamped;
}

std::string displayValue(const int index, const float value, const bool french)
{
    const auto& setting = parameter(index);
    char text[64];
    switch (setting.kind) {
    case Kind::choice: {
        const auto at = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0,
                                                            std::max(0, static_cast<int>(setting.choices.size()) - 1)));
        return at < setting.choices.size() ? (french ? setting.choices[at].first : setting.choices[at].second) : std::string {};
    }
    case Kind::toggle: return value >= 0.5f ? (french ? "Oui" : "On") : (french ? "Non" : "Off");
    case Kind::continuous: break;
    }
    if (setting.unit == "dB")
        std::snprintf(text, sizeof(text), value <= setting.minimum + 0.01f ? "-inf dB" : "%+.1f dB", value);
    else if (setting.unit == "st")
        std::snprintf(text, sizeof(text), "%+.1f st", value);
    else if (setting.key.ends_with(".pan"))
        std::snprintf(text, sizeof(text), std::abs(value) < 0.005f ? (french ? "centre" : "centre") : value < 0.0f ? "%.0f G" : "%.0f D",
                      std::abs(value) * 100.0f);
    else if (setting.key.ends_with(".decay"))
        std::snprintf(text, sizeof(text), value >= 0.999f ? (french ? "naturelle" : "natural") : "%.0f %%", value * 100.0f);
    else
        std::snprintf(text, sizeof(text), "%.0f %%", value * 100.0f);
    return text;
}

std::string pieceName(const Piece piece, const bool french)
{
    const auto& name = pieceNames[static_cast<std::size_t>(std::clamp(static_cast<int>(piece), 0, pieceCount - 1))];
    return french ? name.french : name.english;
}

const char* pieceKey(const Piece piece)
{
    return pieceNames[static_cast<std::size_t>(std::clamp(static_cast<int>(piece), 0, pieceCount - 1))].key;
}

Shaping shapingFor(const Values& values, const Piece piece) noexcept
{
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto p = std::clamp(static_cast<int>(piece), 0, pieceCount - 1);
    Shaping shaping;
    auto soloed = false;
    for (int other = 0; other < pieceCount; ++other)
        soloed = soloed || v(other * fieldCount + fieldSolo) >= 0.5;
    const auto silent = v(p * fieldCount + fieldMute) >= 0.5 || (soloed && v(p * fieldCount + fieldSolo) < 0.5);
    const auto levelDb = v(p * fieldCount + fieldLevel) + v(globalBase + globalLevel);
    shaping.gain = silent || v(p * fieldCount + fieldLevel) <= -39.99 ? 0.0 : std::pow(10.0, levelDb / 20.0);
    shaping.pan = std::clamp(v(p * fieldCount + fieldPan), -1.0, 1.0);
    shaping.semitones = v(p * fieldCount + fieldTune) + v(globalBase + globalTune);
    // Shorter as the knob comes down: from half a second at three quarters
    // to a few milliseconds; at the top, as the sample rings.
    const auto decay = std::clamp(v(p * fieldCount + fieldDecay), 0.0, 1.0);
    shaping.decaySeconds = decay >= 0.999 ? 0.0 : 0.01 + 1.2 * decay * decay * decay;
    return shaping;
}

// ---- Grooves -----------------------------------------------------------------------------------

namespace {

// A line of a pattern: its key, one character a step ('X' accent, 'x' a
// hit, 'o' a ghost, '-' nothing), the bar cut in as many steps as it has.
struct Line {
    int note;
    const char* steps;
};

Groove makeGroove(std::string key, std::string french, std::string english, const bool fill, std::initializer_list<Line> lines)
{
    Groove groove { std::move(key), std::move(french), std::move(english), fill, {} };
    for (const auto& line : lines) {
        const std::string_view steps(line.steps);
        const auto length = static_cast<double>(steps.size());
        for (std::size_t at = 0; at < steps.size(); ++at) {
            const auto c = steps[at];
            if (c == '-')
                continue;
            const auto velocity = c == 'X' ? 1.0f : c == 'x' ? 0.78f : 0.35f;
            groove.hits.push_back({ 4.0 * static_cast<double>(at) / length, line.note, velocity });
        }
    }
    std::sort(groove.hits.begin(), groove.hits.end(), [](const Hit& a, const Hit& b) { return a.beat < b.beat; });
    return groove;
}

} // namespace

const std::vector<Groove>& grooves()
{
    static const std::vector<Groove> built {
        makeGroove("rock", "Rock", "Rock", false,
                   { { 42, "x-x-x-x-x-x-x-x-" }, { 38, "----X-------X---" }, { 36, "X-------X-x-----" } }),
        makeGroove("pop", "Pop", "Pop", false,
                   { { 42, "xoxoxoxoxoxoxoxo" }, { 38, "----X-------X---" }, { 36, "X-----x-X-------" } }),
        makeGroove("funk", "Funk", "Funk", false,
                   { { 42, "xoxoxoxoxoxoxo--" }, { 46, "--------------x-" }, { 38, "----X--o-o--X--o" }, { 36, "X--x--x---x-----" } }),
        makeGroove("disco", "Disco", "Disco", false,
                   { { 36, "X---X---X---X---" }, { 46, "--x---x---x---x-" }, { 42, "x---x---x---x---" }, { 38, "----X-------X---" } }),
        makeGroove("hiphop", "Hip-hop", "Hip-hop", false,
                   { { 42, "x-x-x-x-x-x-x-x-" }, { 38, "----X-------X---" }, { 36, "X-----X---X--x--" } }),
        makeGroove("shuffle", "Shuffle", "Shuffle", false,
                   { { 42, "x-xx-xx-xx-x" }, { 38, "---X-----X--" }, { 36, "X-----X-----" } }),
        makeGroove("bossa", "Bossa nova", "Bossa nova", false,
                   { { 42, "o-o-o-o-o-o-o-o-" }, { 37, "x--x--x---x--x--" }, { 36, "X--xX--xX--xX--x" } }),
        makeGroove("jazz", "Jazz", "Jazz", false,
                   { { 51, "X--X-xX--X-x" }, { 44, "---x-----x--" }, { 36, "o-----o-----" } }),
        makeGroove("metal", "Metal", "Metal", false,
                   { { 36, "xxxxxxxxxxxxxxxx" }, { 38, "----X-------X---" }, { 49, "X-------X-------" } }),
        makeGroove("ballade", "Ballade", "Ballad", false,
                   { { 42, "o-o-o-o-o-o-o-o-" }, { 38, "--------X-------" }, { 36, "X-------x-x-----" } }),
        makeGroove("reggae", "Reggae", "Reggae", false,
                   { { 42, "x-x-x-x-x-x-x-x-" }, { 37, "--------X-------" }, { 36, "--------X-------" } }),
        makeGroove("house", "House", "House", false,
                   { { 36, "X---X---X---X---" }, { 46, "--x---x---x---x-" }, { 39, "----X-------X---" } }),
        makeGroove("fill-toms", "Roulement de toms", "Tom fill", true,
                   { { 50, "Xxxx------------" }, { 48, "----Xxxx--------" }, { 45, "--------Xxxx----" }, { 41, "------------XxXX" },
                     { 36, "X---------------" } }),
        makeGroove("fill-snare", "Roulement de caisse claire", "Snare fill", true,
                   { { 38, "x-x-x-x-oxoxXxXX" }, { 36, "X-------X-------" } }),
    };
    return built;
}

int grooveIndex(const std::string_view key)
{
    const auto& list = grooves();
    for (std::size_t at = 0; at < list.size(); ++at)
        if (list[at].key == key)
            return static_cast<int>(at);
    return -1;
}

int keyInKit(const int note, const std::array<bool, 128>& has) noexcept
{
    if (note < 0 || note > 127)
        return -1;
    if (has[static_cast<std::size_t>(note)])
        return note;
    const auto piece = pieceForNote(note);
    auto best = -1;
    for (int key = 0; key < 128; ++key)
        if (has[static_cast<std::size_t>(key)] && pieceForNote(key) == piece && (best < 0 || std::abs(key - note) < std::abs(best - note)))
            best = key;
    if (best >= 0)
        return best;
    // A tom the kit lacks: its toms, high to low, as the groove's go.
    const auto tom = [](const Piece p) { return p == Piece::tom1 || p == Piece::tom2 || p == Piece::tom3 || p == Piece::tom4; };
    if (!tom(piece))
        return -1;
    std::vector<int> toms;
    for (int key = 127; key >= 0; --key)
        if (has[static_cast<std::size_t>(key)] && tom(pieceForNote(key)))
            toms.push_back(key);
    if (toms.empty())
        return -1;
    const auto rank = static_cast<std::size_t>(static_cast<int>(piece) - static_cast<int>(Piece::tom1));
    return toms[std::min(rank * toms.size() / 4, toms.size() - 1)];
}

std::vector<Hit> grooveHits(const int groove, const int bars, const int fill)
{
    std::vector<Hit> hits;
    const auto& list = grooves();
    if (groove < 0 || groove >= static_cast<int>(list.size()))
        return hits;
    const auto count = std::max(1, bars);
    for (int bar = 0; bar < count; ++bar) {
        const auto ending = bar == count - 1 && fill >= 0 && fill < static_cast<int>(list.size()) && count > 1;
        const auto& played = list[static_cast<std::size_t>(ending ? fill : groove)];
        for (const auto& hit : played.hits)
            hits.push_back({ hit.beat + 4.0 * static_cast<double>(bar), hit.note, hit.velocity });
    }
    return hits;
}

} // namespace cydaw::engine::drum
