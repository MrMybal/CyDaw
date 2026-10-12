#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Batterie: a drum kit played by the sampler (an acoustic kit's map), mixed
// piece by piece as a drummer's console would: each piece (kick, snare,
// hi-hat, four toms, crash, ride, china, percussion, the rest) its level, its
// pan, its tuning, how long it rings, muted or soloed. A note is a piece by
// its key (General MIDI's drum keys, and the kits' extra articulations).
//
// The settings are one flat array (Values) described by parameter(): a key
// ("snare.tune"), names, a range. A card stores them, a plan carries them, a
// sampler voice applies its piece's while it plays.

namespace cydaw::engine::drum {

enum class Piece : std::uint8_t { kick, snare, hihat, tom1, tom2, tom3, tom4, crash, ride, china, percussion, other, count };
inline constexpr int pieceCount = static_cast<int>(Piece::count);

// A piece's settings, in this order.
enum Field : int { fieldLevel, fieldPan, fieldTune, fieldDecay, fieldMute, fieldSolo, fieldCount };
// Then the kit's: which kit (the factory kits' list), its tuning, how much the
// velocity wanders (a drummer's hand), the whole kit's level.
inline constexpr int globalBase = pieceCount * fieldCount;
enum Global : int { globalKit, globalTune, globalHumanise, globalLevel, globalCount };
inline constexpr int valueCount = globalBase + globalCount;
using Values = std::array<float, valueCount>;

[[nodiscard]] constexpr int index(const Piece piece, const Field field) noexcept
{
    return static_cast<int>(piece) * fieldCount + field;
}

enum class Kind : std::uint8_t { continuous, toggle, choice };

struct Parameter {
    std::string key;     // "snare.tune", "kit.tune"
    std::string french;
    std::string english;
    std::string unit;
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    Kind kind { Kind::continuous };
    int piece { -1 };    // -1: the kit's
    std::vector<std::pair<std::string, std::string>> choices;
};

// The kits Batterie plays: the factory instruments whose maps they are.
struct KitInfo {
    const char* instrument; // the factory instrument's key ("drums-rusty")
    const char* french;
    const char* english;
};
inline constexpr std::array<KitInfo, 6> kits { {
    { "drums-rusty", "Rouillée", "Rusty" },
    { "drums-jazz", "Jazz", "Jazz" },
    { "drums-swirly", "Tourbillon", "Swirly" },
    { "drums-unruly-sticks", "Indocile (baguettes)", "Unruly (sticks)" },
    { "drums-unruly-brushes", "Indocile (balais)", "Unruly (brushes)" },
    // Kick, snare, hi-hat and toms; no cymbals.
    { "drums-seventies", "Années 70", "Seventies" },
} };

// The settings at rest: every piece at its level, centred, in tune, ringing
// as recorded; the first kit. The same as defaultValues(), without its table
// (nothing allocated: a card's parameters start from it).
[[nodiscard]] constexpr Values neutralValues() noexcept
{
    Values values {};
    for (int piece = 0; piece < pieceCount; ++piece)
        values[static_cast<std::size_t>(piece * fieldCount + fieldDecay)] = 1.0f;
    return values;
}

[[nodiscard]] const std::vector<Parameter>& parameters();
[[nodiscard]] const Parameter& parameter(int index);
// -1 when no setting has that key.
[[nodiscard]] int parameterIndex(std::string_view key);
[[nodiscard]] Values defaultValues();
[[nodiscard]] float clampValue(int index, float value);
[[nodiscard]] std::string displayValue(int index, float value, bool french);
[[nodiscard]] std::string pieceName(Piece piece, bool french);
// "kick", "snare"…: the start of its settings' keys.
[[nodiscard]] const char* pieceKey(Piece piece);

// The piece a key plays.
[[nodiscard]] constexpr Piece pieceForNote(const int note) noexcept
{
    switch (note) {
    case 35:
    case 36: return Piece::kick;
    case 37:
    case 38:
    case 39:
    case 40:
    case 26: return Piece::snare;
    case 42:
    case 44:
    case 46:
    case 54:
    case 58:
    case 30:
    case 32:
    case 34: return Piece::hihat;
    case 48:
    case 50: return Piece::tom1;
    case 47: return Piece::tom2;
    case 45: return Piece::tom3;
    case 41:
    case 43: return Piece::tom4;
    case 49:
    case 55:
    case 57: return Piece::crash;
    case 51:
    case 53:
    case 59: return Piece::ride;
    case 52: return Piece::china;
    default: break;
    }
    if (note == 56 || (note >= 60 && note <= 81))
        return Piece::percussion;
    return Piece::other;
}

// What a piece's settings do to a note of it.
struct Shaping {
    double gain { 1.0 };      // linear; 0 when muted (or another piece soloed)
    double pan {};            // -1..1, added to the sample's
    double semitones {};      // the piece's tuning and the kit's
    double decaySeconds {};   // 0: as it rings; else the time it fades in
};
[[nodiscard]] Shaping shapingFor(const Values& values, Piece piece) noexcept;

// ---- Grooves -------------------------------------------------------------------------------

// A hit of a pattern: when (beats from its start), which key, how hard.
struct Hit {
    double beat {};
    int note {};
    float velocity {};
};
// A bar a drummer plays (rock, funk, bossa…) or a fill to end on.
struct Groove {
    std::string key;     // "rock", "fill-toms"
    std::string french;
    std::string english;
    bool fill {};
    std::vector<Hit> hits; // one bar of 4/4
};
[[nodiscard]] const std::vector<Groove>& grooves();
// -1 when there is none of that key.
[[nodiscard]] int grooveIndex(std::string_view key);
// A groove played `bars` times (the last bar the fill, when one is given).
[[nodiscard]] std::vector<Hit> grooveHits(int groove, int bars, int fill);
// The key a kit plays for `note`: itself if the kit has it, else the nearest
// key of the same piece it has (a tom: any of its toms, in order); -1: none.
// `has` says which keys the kit plays.
[[nodiscard]] int keyInKit(int note, const std::array<bool, 128>& has) noexcept;

} // namespace cydaw::engine::drum
