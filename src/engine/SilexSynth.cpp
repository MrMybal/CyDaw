// Silex: a drum machine of the synth family whose pieces are all synthesised,
// as the analog drum boxes made theirs; each key of the keyboard is a piece
// (the general MIDI drum map), notes outside it are silent. A kick (a sine
// whose pitch falls fast, a click, a drive), a snare (two tuned modes and a
// filtered noise for its wires), a clap (noise bursts in quick succession,
// then their tail), hi-hats (six detuned square oscillators, metallic, through
// a band-pass and a high-pass, with noise; closed, pedal and open), three
// toms (a sine or a triangle whose pitch drops, a stick), a crash and a ride
// (the metallic oscillators and noise in their bands, a long decay; the
// ride's bell), a rimshot, a cowbell (two square oscillators through a
// band-pass), claves (a short resonant ping) and maracas (a burst of
// high-passed noise). Each piece has its own settings; the kit has a
// character (analog: warm and round, each hit a little different; digital:
// bright and sharp; lo-fi: crunchy, its rate and its bits reduced), a drive,
// a tone, a tuning and a length for all. A piece plays out its envelope
// whatever the key does; the velocity makes it louder and brighter.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Silex's settings after the shared voicing, in its table's order.
enum Silex : int {
    kickTune = voicingFieldCount,
    kickSweep,
    kickTime,
    kickDecay,
    kickClick,
    kickDrive,
    kickLevel,
    kickPan,
    snareTune,
    snareTone,
    snareSnappy,
    snareColour,
    snareDecay,
    snareLevel,
    snarePan,
    clapSpread,
    clapTone,
    clapDecay,
    clapLevel,
    clapPan,
    hatTune,
    hatMetal,
    hatTone,
    hatClosed,
    hatOpen,
    hatLevel,
    hatPan,
    tomTune,
    tomDrop,
    tomDecay,
    tomTone,
    tomLevel,
    tomPan,
    tomSpread,
    crashTune,
    crashTone,
    crashDecay,
    crashLevel,
    crashPan,
    rideTune,
    rideTone,
    rideBell,
    rideDecay,
    rideLevel,
    ridePan,
    rimTune,
    rimDecay,
    rimLevel,
    rimPan,
    cowbellTune,
    cowbellDecay,
    cowbellLevel,
    cowbellPan,
    claveTune,
    claveDecay,
    claveLevel,
    clavePan,
    shakerTone,
    shakerDecay,
    shakerLevel,
    shakerPan,
    kitCharacter,
    kitTune,
    kitDecay,
    kitVelocity,
    masterDrive,
    masterTone,
    silexCount
};

const std::vector<std::pair<std::string, std::string>> kCharacterNames { { "Analogique", "Analog" },
                                                                          { "Numérique", "Digital" },
                                                                          { "Lo-fi", "Lo-fi" } };

} // namespace

std::vector<Section> silexSections()
{
    return { { "VOIX", "VOICES", 0xffb0b8c8u },         { "GROSSE CAISSE", "KICK", 0xffff7a52u },
             { "CAISSE CLAIRE", "SNARE", 0xffffb04au }, { "CLAP", "CLAP", 0xffffd86au },
             { "CHARLESTONS", "HI-HATS", 0xff7ad0ffu },  { "TOMS", "TOMS", 0xffd08c4cu },
             { "CYMBALES", "CYMBALS", 0xff8ce0c8u },     { "PERCUSSIONS", "PERCUSSION", 0xffa58cffu },
             { "GÉNÉRAL", "GLOBAL", 0xffc8ccd8u } };
}

std::vector<Parameter> silexParameters()
{
    Table table;
    addVoicing(table, 16, 0.7f);
    table.section = 1;
    table.add("kick.tune", "Grosse caisse · accord", "Kick · tune", "Hz", 30.0f, 120.0f, 52.0f, Kind::continuous, true);
    table.add("kick.sweep", "Grosse caisse · chute de hauteur", "Kick · pitch drop", "st", 0.0f, 48.0f, 24.0f);
    table.add("kick.time", "Grosse caisse · temps de chute", "Kick · drop time", "s", 0.005f, 0.5f, 0.035f, Kind::continuous, true);
    table.add("kick.decay", "Grosse caisse · déclin", "Kick · decay", "s", 0.05f, 4.0f, 0.5f, Kind::continuous, true);
    table.add("kick.click", "Grosse caisse · clic", "Kick · click", {}, 0.0f, 1.0f, 0.35f);
    table.add("kick.drive", "Grosse caisse · saturation", "Kick · drive", {}, 0.0f, 1.0f, 0.15f);
    table.add("kick.level", "Grosse caisse · niveau", "Kick · level", {}, 0.0f, 1.0f, 0.9f);
    table.add("kick.pan", "Grosse caisse · panoramique", "Kick · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 2;
    table.add("snare.tune", "Caisse claire · accord", "Snare · tune", "Hz", 100.0f, 400.0f, 185.0f, Kind::continuous, true);
    table.add("snare.tone", "Caisse claire · ton (corps ↔ bruit)", "Snare · tone (body ↔ noise)", {}, 0.0f, 1.0f, 0.5f);
    table.add("snare.snappy", "Caisse claire · timbre", "Snare · snappy", {}, 0.0f, 1.0f, 0.6f);
    table.add("snare.colour", "Caisse claire · couleur du bruit", "Snare · noise colour", {}, 0.0f, 1.0f, 0.5f);
    table.add("snare.decay", "Caisse claire · déclin", "Snare · decay", "s", 0.05f, 1.5f, 0.25f, Kind::continuous, true);
    table.add("snare.level", "Caisse claire · niveau", "Snare · level", {}, 0.0f, 1.0f, 0.75f);
    table.add("snare.pan", "Caisse claire · panoramique", "Snare · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 3;
    table.add("clap.spread", "Clap · écart", "Clap · spread", "s", 0.003f, 0.03f, 0.011f, Kind::continuous, true);
    table.add("clap.tone", "Clap · ton", "Clap · tone", "Hz", 500.0f, 5'000.0f, 1'150.0f, Kind::continuous, true);
    table.add("clap.decay", "Clap · déclin", "Clap · decay", "s", 0.05f, 2.0f, 0.3f, Kind::continuous, true);
    table.add("clap.level", "Clap · niveau", "Clap · level", {}, 0.0f, 1.0f, 0.7f);
    table.add("clap.pan", "Clap · panoramique", "Clap · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 4;
    table.add("hat.tune", "Charleston · accord", "Hi-hat · tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("hat.metal", "Charleston · métal", "Hi-hat · metal", {}, 0.0f, 1.0f, 0.65f);
    table.add("hat.tone", "Charleston · ton", "Hi-hat · tone", {}, 0.0f, 1.0f, 0.5f);
    table.add("hat.closed", "Charleston fermé · déclin", "Closed hat · decay", "s", 0.01f, 0.5f, 0.07f, Kind::continuous, true);
    table.add("hat.open", "Charleston ouvert · déclin", "Open hat · decay", "s", 0.08f, 3.0f, 0.5f, Kind::continuous, true);
    table.add("hat.level", "Charleston · niveau", "Hi-hat · level", {}, 0.0f, 1.0f, 0.55f);
    table.add("hat.pan", "Charleston · panoramique", "Hi-hat · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 5;
    table.add("tom.tune", "Toms · accord (grave)", "Toms · tune (low)", "Hz", 50.0f, 300.0f, 100.0f, Kind::continuous, true);
    table.add("tom.drop", "Toms · chute de hauteur", "Toms · pitch drop", "st", 0.0f, 24.0f, 5.0f);
    table.add("tom.decay", "Toms · déclin", "Toms · decay", "s", 0.08f, 3.0f, 0.55f, Kind::continuous, true);
    table.add("tom.tone", "Toms · ton", "Toms · tone", {}, 0.0f, 1.0f, 0.3f);
    table.add("tom.level", "Toms · niveau", "Toms · level", {}, 0.0f, 1.0f, 0.7f);
    table.add("tom.pan", "Toms · panoramique", "Toms · pan", {}, -1.0f, 1.0f, 0.0f);
    table.add("tom.spread", "Toms · écart stéréo", "Toms · stereo spread", {}, 0.0f, 1.0f, 0.5f);
    table.section = 6;
    table.add("crash.tune", "Crash · accord", "Crash · tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("crash.tone", "Crash · ton", "Crash · tone", {}, 0.0f, 1.0f, 0.5f);
    table.add("crash.decay", "Crash · déclin", "Crash · decay", "s", 0.3f, 8.0f, 2.0f, Kind::continuous, true);
    table.add("crash.level", "Crash · niveau", "Crash · level", {}, 0.0f, 1.0f, 0.5f);
    table.add("crash.pan", "Crash · panoramique", "Crash · pan", {}, -1.0f, 1.0f, 0.0f);
    table.add("ride.tune", "Ride · accord", "Ride · tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("ride.tone", "Ride · ton", "Ride · tone", {}, 0.0f, 1.0f, 0.5f);
    table.add("ride.bell", "Ride · cloche", "Ride · bell", {}, 0.0f, 1.0f, 0.35f);
    table.add("ride.decay", "Ride · déclin", "Ride · decay", "s", 0.3f, 8.0f, 2.5f, Kind::continuous, true);
    table.add("ride.level", "Ride · niveau", "Ride · level", {}, 0.0f, 1.0f, 0.45f);
    table.add("ride.pan", "Ride · panoramique", "Ride · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 7;
    table.add("rim.tune", "Rimshot · accord", "Rimshot · tune", "Hz", 200.0f, 1'200.0f, 480.0f, Kind::continuous, true);
    table.add("rim.decay", "Rimshot · déclin", "Rimshot · decay", "s", 0.01f, 0.3f, 0.04f, Kind::continuous, true);
    table.add("rim.level", "Rimshot · niveau", "Rimshot · level", {}, 0.0f, 1.0f, 0.6f);
    table.add("rim.pan", "Rimshot · panoramique", "Rimshot · pan", {}, -1.0f, 1.0f, 0.0f);
    table.add("cowbell.tune", "Cloche · accord", "Cowbell · tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("cowbell.decay", "Cloche · déclin", "Cowbell · decay", "s", 0.05f, 1.5f, 0.3f, Kind::continuous, true);
    table.add("cowbell.level", "Cloche · niveau", "Cowbell · level", {}, 0.0f, 1.0f, 0.5f);
    table.add("cowbell.pan", "Cloche · panoramique", "Cowbell · pan", {}, -1.0f, 1.0f, 0.0f);
    table.add("clave.tune", "Claves · accord", "Claves · tune", "Hz", 1'000.0f, 4'000.0f, 2'500.0f, Kind::continuous, true);
    table.add("clave.decay", "Claves · déclin", "Claves · decay", "s", 0.01f, 0.4f, 0.06f, Kind::continuous, true);
    table.add("clave.level", "Claves · niveau", "Claves · level", {}, 0.0f, 1.0f, 0.55f);
    table.add("clave.pan", "Claves · panoramique", "Claves · pan", {}, -1.0f, 1.0f, 0.0f);
    table.add("shaker.tone", "Maracas · ton", "Maracas · tone", {}, 0.0f, 1.0f, 0.5f);
    table.add("shaker.decay", "Maracas · déclin", "Maracas · decay", "s", 0.02f, 0.5f, 0.07f, Kind::continuous, true);
    table.add("shaker.level", "Maracas · niveau", "Maracas · level", {}, 0.0f, 1.0f, 0.5f);
    table.add("shaker.pan", "Maracas · panoramique", "Maracas · pan", {}, -1.0f, 1.0f, 0.0f);
    table.section = 8;
    table.choice("kit.character", "Caractère", "Character", kCharacterNames, 0);
    table.add("kit.tune", "Kit · accord", "Kit · tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("kit.decay", "Kit · longueur", "Kit · length", "×", 0.25f, 4.0f, 1.0f, Kind::continuous, true);
    table.add("kit.velocity", "Vélocité", "Velocity", {}, 0.0f, 1.0f, 0.6f);
    table.add("master.drive", "Saturation générale", "Master drive", {}, 0.0f, 1.0f, 0.0f);
    table.add("master.tone", "Ton général", "Master tone", {}, -1.0f, 1.0f, 0.0f);
    return table.parameters;
}

namespace {

// The pieces a key plays.
enum class Piece : std::uint8_t { none, kick, rim, snare, clap, closedHat, pedalHat, openHat, tom, crash, ride, cowbell, clave, shaker };

struct Struck {
    Piece piece { Piece::none };
    int tom {};   // 0 low, 1 mid, 2 high
    int place {}; // the six tom notes from the lowest
};

Struck pieceFor(const int note) noexcept
{
    switch (note) {
    case 35:
    case 36: return { Piece::kick, 0, 0 };
    case 37: return { Piece::rim, 0, 0 };
    case 38:
    case 40: return { Piece::snare, 0, 0 };
    case 39: return { Piece::clap, 0, 0 };
    case 42: return { Piece::closedHat, 0, 0 };
    case 44: return { Piece::pedalHat, 0, 0 };
    case 46: return { Piece::openHat, 0, 0 };
    case 41: return { Piece::tom, 0, 0 };
    case 43: return { Piece::tom, 0, 1 };
    case 45: return { Piece::tom, 1, 2 };
    case 47: return { Piece::tom, 1, 3 };
    case 48: return { Piece::tom, 2, 4 };
    case 50: return { Piece::tom, 2, 5 };
    case 49:
    case 57: return { Piece::crash, 0, 0 };
    case 51:
    case 59: return { Piece::ride, 0, 0 };
    case 56: return { Piece::cowbell, 0, 0 };
    case 70: return { Piece::shaker, 0, 0 };
    case 75: return { Piece::clave, 0, 0 };
    default: return {};
    }
}

// Each tom note from the low one (semitones): three toms, the second note
// of each a minor third above the first. And where it sits across the
// stereo, as a drummer hears them (the high ones on the left).
constexpr std::array<double, 6> kTomSteps { 0.0, 3.0, 5.0, 8.0, 10.0, 13.0 };
constexpr std::array<double, 6> kTomPlaces { 1.0, 0.6, 0.2, -0.2, -0.6, -1.0 };
// The six square oscillators of the metals (Hz), apart as the analog boxes had them.
constexpr std::array<double, 6> kMetal { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
// The ride's bell: three inharmonic partials.
constexpr std::array<double, 3> kBell { 760.0, 1'145.0, 1'710.0 };
// ln(1000): a decay of so many seconds falls 60 dB in them.
constexpr double kT60 = 6.907755278982137;
// What is left when a piece is over (-80 dB).
constexpr double kSilent = 1.0e-4;

// How a character makes the kit.
struct Character {
    double variance; // how much each hit differs (an analog drift)
    double bright;   // the noises and metals moved (octaves)
    double click;    // the attacks
    double round;    // a soft saturation, as analog circuits round off
    double warmHz;   // a gentle low-pass (0: none)
    double holdHz;   // the rate it is held at (0: not reduced)
    double steps;    // the levels it keeps either side of nothing (0: not reduced)
    double drive;    // a drive of its own
};

constexpr std::array<Character, 3> kCharacters { {
    { 1.0, -0.15, 1.0, 0.6, 12'500.0, 0.0, 0.0, 0.0 },
    { 0.0, 0.35, 1.45, 0.0, 0.0, 0.0, 0.0, 0.0 },
    { 0.45, -0.3, 0.85, 0.4, 7'000.0, 11'025.0, 40.0, 0.3 },
} };

int characterOf(const Values& values) noexcept
{
    return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(kitCharacter)])), 0, 2);
}

// Per sample, the factor that takes a level 60 dB down in `seconds`.
double fall(const double seconds, const double rate) noexcept
{
    return std::exp(-kT60 / (std::max(1.0e-4, seconds) * rate));
}

// Per sample, the factor of a time constant of `seconds`.
double fallBy(const double seconds, const double rate) noexcept
{
    return std::exp(-1.0 / (std::max(1.0e-5, seconds) * rate));
}

// A state-variable filter's g for `hz`, kept below Nyquist.
double gainOf(const double hz, const double rate) noexcept
{
    return std::tan(kPi * std::clamp(hz, 20.0, 0.45 * rate) / rate);
}

// A one-pole low-pass's coefficient for `hz`.
double onePole(const double hz, const double rate) noexcept
{
    return 1.0 - std::exp(-2.0 * kPi * std::clamp(hz, 5.0, 0.45 * rate) / rate);
}

// A state-variable filter (two poles, solved without delay): its low, band
// and high outputs; k its damping (2: none, 1/Q).
struct Svf {
    struct Out {
        double low;
        double band;
        double high;
    };
    double ic1 {};
    double ic2 {};
    void reset() noexcept { ic1 = ic2 = 0.0; }
    Out run(const double x, const double g, const double k) noexcept
    {
        const auto a1 = 1.0 / (1.0 + g * (g + k));
        const auto v1 = a1 * ic1 + g * a1 * (x - ic2);
        const auto v2 = ic2 + g * v1;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
};

// What holds for a block whatever the piece.
struct Shared {
    double ratio { 1.0 };  // the tunings moved (the kit's, the bend, the note's expression)
    double length { 1.0 }; // the decays stretched
    double bright {};      // the noises and metals moved (octaves): the character, the velocity
    double click { 1.0 };  // the attacks
};

class SilexEngine final : public Engine {
public:
    void prepare(const double sampleRate) override { rate_ = std::max(1.0, sampleRate); }

    void start(const Values& values, const int note, const float velocity, const double /*glideFrom*/, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        hit(values, note, velocity);
    }

    // Played legato: the next piece is struck all the same.
    void retarget(const Values& values, const int note, const float velocity) noexcept override { hit(values, note, velocity); }

    // A drum plays out its envelope whatever the key does.
    void release() noexcept override
    {
        if (playing_)
            released_ = true;
    }

    // Cut (a closed hat choking the open one, a voice taken): a fade of a few
    // milliseconds, no click; the voice ends with it.
    void kill() noexcept override
    {
        if (playing_ && fadeStep_ <= 0.0)
            fadeStep_ = 1.0 / (0.004 * rate_);
    }

    [[nodiscard]] bool active() const noexcept override { return playing_; }
    [[nodiscard]] bool releasing() const noexcept override { return playing_ && released_; }
    [[nodiscard]] double currentPitch() const noexcept override { return playing_ ? static_cast<double>(note_) : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!playing_)
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto& character = kCharacters[static_cast<std::size_t>(characterOf(values))];
        // What holds for the whole block.
        const auto sensitivity = std::clamp(v(kitVelocity), 0.0, 1.0);
        const auto touch = 1.0 - sensitivity * (1.0 - velocity_);
        Shared shared;
        shared.ratio = std::exp2((std::clamp(v(kitTune), -24.0, 24.0) + static_cast<double>(context.pitchBend) * std::round(v(voicingBend))
                                  + expressionPitch)
                                 / 12.0)
                       * jitter_;
        shared.length = std::clamp(v(kitDecay), 0.25, 4.0);
        shared.bright = character.bright - 1.6 * (1.0 - touch);
        shared.click = character.click * (0.35 + 0.65 * touch);
        const auto loud = (1.0 - sensitivity * (1.0 - std::pow(velocity_, 1.5))) * levelJitter_;
        double level {};
        double pan {};
        switch (piece_) {
        case Piece::kick:
            level = v(kickLevel);
            pan = v(kickPan);
            break;
        case Piece::rim:
            level = v(rimLevel);
            pan = v(rimPan);
            break;
        case Piece::snare:
            level = v(snareLevel);
            pan = v(snarePan);
            break;
        case Piece::clap:
            level = v(clapLevel);
            pan = v(clapPan);
            break;
        case Piece::closedHat:
        case Piece::pedalHat:
        case Piece::openHat:
            level = v(hatLevel);
            pan = v(hatPan);
            break;
        case Piece::tom:
            level = v(tomLevel);
            pan = v(tomPan) + 0.8 * std::clamp(v(tomSpread), 0.0, 1.0) * kTomPlaces[static_cast<std::size_t>(place_)];
            break;
        case Piece::crash:
            level = v(crashLevel);
            pan = v(crashPan);
            break;
        case Piece::ride:
            level = v(rideLevel);
            pan = v(ridePan);
            break;
        case Piece::cowbell:
            level = v(cowbellLevel);
            pan = v(cowbellPan);
            break;
        case Piece::clave:
            level = v(claveLevel);
            pan = v(clavePan);
            break;
        case Piece::shaker:
            level = v(shakerLevel);
            pan = v(shakerPan);
            break;
        case Piece::none: break;
        }
        pan = std::clamp(pan, -1.0, 1.0);
        // Balanced: the middle at full level on both sides.
        const auto panLeft = std::min(1.0, 1.0 - pan);
        const auto panRight = std::min(1.0, 1.0 + pan);
        const auto target = std::clamp(level, 0.0, 1.0) * loud * v(voicingMaster) * expressionGain;
        // The output: the character's roundness, the drive, the tone, the character's filter and its reduced rate and bits.
        const auto rounding = character.round;
        const auto drive = std::clamp(v(masterDrive) + character.drive, 0.0, 1.0);
        const auto driveGain = 1.0 + 6.0 * drive;
        const auto driveBack = 1.0 / std::tanh(driveGain);
        const auto tone = std::clamp(v(masterTone), -1.0, 1.0);
        const auto tilted = std::abs(tone) > 1.0e-4;
        const auto tiltCoefficient = onePole(650.0, rate_);
        const auto highGain = std::exp2(1.3 * tone);
        const auto lowGain = std::exp2(-0.35 * tone);
        const auto warmCoefficient = character.warmHz > 0.0 && character.warmHz < 0.45 * rate_ ? onePole(character.warmHz, rate_) : 0.0;
        const auto holdStep = character.holdHz > 0.0 ? std::min(1.0, character.holdHz / rate_) : 0.0;
        const auto steps = character.steps;

        for (int done = 0; done < count; done += chunk) {
            const auto length = std::min(chunk, count - done);
            std::array<double, chunk> mono {};
            auto alive = false;
            switch (piece_) {
            case Piece::kick: alive = kick(values, shared, mono.data(), length); break;
            case Piece::rim: alive = rim(values, shared, mono.data(), length); break;
            case Piece::snare: alive = snare(values, shared, mono.data(), length); break;
            case Piece::clap: alive = clap(values, shared, mono.data(), length); break;
            case Piece::closedHat:
            case Piece::pedalHat:
            case Piece::openHat: alive = hat(values, shared, mono.data(), length); break;
            case Piece::tom: alive = tom(values, shared, mono.data(), length); break;
            case Piece::crash: alive = crash(values, shared, mono.data(), length); break;
            case Piece::ride: alive = ride(values, shared, mono.data(), length); break;
            case Piece::cowbell: alive = cowbell(values, shared, mono.data(), length); break;
            case Piece::clave: alive = clave(values, shared, mono.data(), length); break;
            case Piece::shaker: alive = shaker(values, shared, mono.data(), length); break;
            case Piece::none: break;
            }
            // The level eased across the chunk (a knob turned); a hit starts at its own.
            const auto from = previousGain_ < 0.0 ? target : previousGain_;
            previousGain_ = target;
            for (int i = 0; i < length; ++i) {
                auto x = mono[static_cast<std::size_t>(i)];
                if (rounding > 0.0)
                    x += rounding * (std::tanh(x) - x);
                if (drive > 0.0)
                    x += drive * (std::tanh(driveGain * x) * driveBack - x);
                if (tilted) {
                    tiltLow_ += tiltCoefficient * (x - tiltLow_);
                    x = tiltLow_ * lowGain + (x - tiltLow_) * highGain;
                }
                if (warmCoefficient > 0.0) {
                    warm_ += warmCoefficient * (x - warm_);
                    x = warm_;
                }
                if (holdStep > 0.0) {
                    // Held at a lower rate, its levels few: crunchy.
                    holdPhase_ += holdStep;
                    if (holdPhase_ >= 1.0) {
                        holdPhase_ -= 1.0;
                        held_ = steps > 0.0 ? std::round(std::clamp(x, -2.0, 2.0) * steps) / steps : x;
                    }
                    x = held_;
                }
                auto gain = from + (target - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                if (fadeStep_ > 0.0) {
                    fade_ = std::max(0.0, fade_ - fadeStep_);
                    gain *= fade_;
                }
                left[done + i] += static_cast<float>(x * gain * panLeft);
                right[done + i] += static_cast<float>(x * gain * panRight);
            }
            showEnvelope(0, std::min(1.0, envA_ + envB_));
            if (!alive || (fadeStep_ > 0.0 && fade_ <= 0.0)) {
                playing_ = false;
                return false;
            }
        }
        return playing_;
    }

private:
    static constexpr int chunk = 64;

    // A piece struck: its oscillators and envelopes from the start.
    void hit(const Values& values, const int note, const float velocity) noexcept
    {
        const auto struck = pieceFor(note);
        note_ = note;
        piece_ = struck.piece;
        tom_ = struck.tom;
        place_ = struck.place;
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        playing_ = piece_ != Piece::none;
        released_ = false;
        fade_ = 1.0;
        fadeStep_ = 0.0;
        previousGain_ = -1.0;
        const auto variance = kCharacters[static_cast<std::size_t>(characterOf(values))].variance;
        // Analog: each hit a little apart (its pitch, its level, its metals).
        jitter_ = 1.0 + 0.006 * variance * random();
        levelJitter_ = 1.0 + 0.06 * variance * random();
        for (auto& detune : detune_)
            detune = 1.0 + 0.004 * variance * random();
        // The metals free-running where analog (a hit never twice the same);
        // where digital, always from the same places (apart: never all at once).
        for (std::size_t k = 0; k < metal_.size(); ++k)
            metal_[k] = variance > 0.0 ? 0.5 * (random() + 1.0) : 0.13 + 0.37 * static_cast<double>(k) - std::floor(0.13 + 0.37 * static_cast<double>(k));
        phaseA_ = phaseB_ = phaseC_ = 0.0;
        envA_ = envB_ = envC_ = envD_ = 1.0;
        pitchEnv_ = 1.0;
        low_ = 0.0;
        for (auto& filter : filters_)
            filter.reset();
        tiltLow_ = warm_ = held_ = 0.0;
        holdPhase_ = 1.0;
        elapsed_ = 0.0;
        burst_ = 0;
        switch (piece_) {
        case Piece::clap: {
            // Three quick bursts, the fourth opening the tail; their gaps a little uneven.
            envA_ = envB_ = 0.0;
            const auto spread = std::clamp(static_cast<double>(values[static_cast<std::size_t>(clapSpread)]), 0.002, 0.05) * rate_;
            burstAt_[0] = 0.0;
            for (std::size_t at = 1; at < burstAt_.size(); ++at)
                burstAt_[at] = burstAt_[at - 1] + spread * (1.0 + (0.08 + 0.17 * variance) * random());
            break;
        }
        case Piece::pedalHat:
        case Piece::shaker: envD_ = 0.0; break;
        default: break;
        }
    }

    // The kick: a sine whose pitch falls from `sweep` semitones above its
    // tune, a click (a noise and a short high tone), a drive.
    bool kick(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto hz = std::clamp(v(kickTune), 20.0, 400.0) * shared.ratio;
        const auto sweep = std::clamp(v(kickSweep), 0.0, 48.0);
        const auto pitchFall = fallBy(std::max(0.002, v(kickTime)), rate_);
        const auto bodyFall = fall(v(kickDecay) * shared.length, rate_);
        const auto click = std::clamp(v(kickClick), 0.0, 1.0) * shared.click;
        const auto noiseFall = fallBy(0.0012, rate_);
        const auto tickFall = fallBy(0.0008, rate_);
        const auto tickStep = std::min(0.45, 2'600.0 * std::exp2(0.5 * shared.bright) / rate_);
        const auto highPass = onePole(1'800.0 * std::exp2(shared.bright), rate_);
        const auto drive = std::clamp(v(kickDrive), 0.0, 1.0);
        const auto driveGain = 1.0 + 7.0 * drive;
        const auto driveBack = 1.0 / std::tanh(driveGain);
        for (int i = 0; i < length; ++i) {
            const auto increment = std::min(0.45, hz * std::exp2(sweep * pitchEnv_ / 12.0) / rate_);
            pitchEnv_ *= pitchFall;
            auto x = std::sin(2.0 * kPi * phaseA_) * envA_;
            phaseA_ += increment;
            phaseA_ -= std::floor(phaseA_);
            envA_ *= bodyFall;
            if (envB_ > 1.0e-5) {
                const auto white = random();
                low_ += highPass * (white - low_);
                x += click * (0.45 * (white - low_) * envB_ + 0.7 * std::sin(2.0 * kPi * phaseB_) * envC_);
                phaseB_ += tickStep;
                phaseB_ -= std::floor(phaseB_);
                envB_ *= noiseFall;
                envC_ *= tickFall;
            }
            if (drive > 0.0)
                x += drive * (std::tanh(driveGain * x) * driveBack - x);
            out[i] = x * 0.95;
        }
        return envA_ > kSilent || envB_ > kSilent;
    }

    // The snare: two tuned modes (the drum's body, a little punch as it is
    // struck) and a noise through its colour for the wires; the tone sets the
    // balance, the snappy how long and how much the wires rattle.
    bool snare(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto hz = std::clamp(v(snareTune), 50.0, 1'000.0) * shared.ratio;
        const auto tone = std::clamp(v(snareTone), 0.0, 1.0);
        const auto snappy = std::clamp(v(snareSnappy), 0.0, 1.0);
        const auto colour = std::clamp(v(snareColour), 0.0, 1.0);
        const auto seconds = std::max(0.01, v(snareDecay) * shared.length);
        const auto lowFall = fall(seconds * 0.9, rate_);
        const auto highFall = fall(seconds * 0.55, rate_);
        const auto noiseFall = fall(seconds * (0.55 + 1.3 * snappy), rate_);
        const auto punchFall = fallBy(0.01, rate_);
        const auto lowPassHz = std::min(0.45 * rate_, 3'500.0 * std::exp2(2.2 * colour + shared.bright));
        const auto highPassHz = 450.0 * std::exp2(1.6 * colour + 0.5 * shared.bright);
        const auto g = gainOf(lowPassHz, rate_);
        const auto highPass = onePole(highPassHz, rate_);
        // About as loud whatever the colour: a narrower band given back its level.
        const auto band = std::max(200.0, lowPassHz - highPassHz);
        const auto norm = std::clamp(0.5 / std::sqrt(band / (0.5 * rate_)), 0.8, 3.0);
        const auto bodyGain = std::cos(tone * kPi * 0.5) * 0.7;
        const auto noiseGain = std::sin(tone * kPi * 0.5) * (0.55 + 0.6 * snappy) * norm;
        const auto step1 = hz / rate_;
        const auto step2 = hz * 1.71 / rate_;
        for (int i = 0; i < length; ++i) {
            const auto punch = 1.0 + 0.3 * envD_;
            envD_ *= punchFall;
            const auto body = std::sin(2.0 * kPi * phaseA_) * envA_ + 0.55 * std::sin(2.0 * kPi * phaseB_) * envB_;
            phaseA_ += std::min(0.45, step1 * punch);
            phaseA_ -= std::floor(phaseA_);
            phaseB_ += std::min(0.45, step2 * punch);
            phaseB_ -= std::floor(phaseB_);
            envA_ *= lowFall;
            envB_ *= highFall;
            const auto lowPassed = filters_[0].run(random(), g, 1.2).low;
            low_ += highPass * (lowPassed - low_);
            const auto wires = (lowPassed - low_) * envC_;
            envC_ *= noiseFall;
            out[i] = bodyGain * body + noiseGain * wires;
        }
        return envA_ > kSilent || envC_ > kSilent;
    }

    // The clap: a band of noise in three quick bursts, the fourth opening
    // its tail.
    bool clap(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto spread = std::clamp(v(clapSpread), 0.002, 0.05);
        const auto hz = std::min(0.45 * rate_, std::clamp(v(clapTone), 200.0, 8'000.0) * std::exp2(0.5 * shared.bright));
        constexpr double damping = 0.6;
        const auto g = gainOf(hz, rate_);
        const auto burstFall = fallBy(spread * 0.32 + 0.001, rate_);
        const auto tailFall = fall(std::max(0.02, v(clapDecay) * shared.length), rate_);
        // A narrow band of noise given back its level.
        const auto norm = std::min(14.0, 0.3 / (0.577 * std::sqrt(kPi * hz * damping / rate_)));
        for (int i = 0; i < length; ++i) {
            if (burst_ < static_cast<int>(burstAt_.size()) && elapsed_ >= burstAt_[static_cast<std::size_t>(burst_)]) {
                envB_ = 1.0;
                if (burst_ + 1 == static_cast<int>(burstAt_.size()))
                    envA_ = 1.0;
                ++burst_;
            }
            const auto band = filters_[0].run(random(), g, damping).band * damping;
            out[i] = band * norm * (envB_ + 0.7 * envA_);
            envB_ *= burstFall;
            envA_ *= tailFall;
            elapsed_ += 1.0;
        }
        return burst_ < static_cast<int>(burstAt_.size()) || envA_ > kSilent || envB_ > kSilent;
    }

    // The six metal squares at `ratio`, summed (about 1 in level).
    double metals(const std::array<double, 6>& increments) noexcept
    {
        auto sum = 0.0;
        for (std::size_t k = 0; k < metal_.size(); ++k) {
            sum += oscillator(1, metal_[k], increments[k], 0.5);
            metal_[k] += increments[k];
            metal_[k] -= std::floor(metal_[k]);
        }
        return sum * 0.41;
    }

    void metalIncrements(std::array<double, 6>& increments, const double ratio) const noexcept
    {
        for (std::size_t k = 0; k < increments.size(); ++k)
            increments[k] = std::clamp(kMetal[k] * ratio * detune_[k] / rate_, 0.0, 0.45);
    }

    // The hi-hats: the metal squares and a noise (as metallic as `metal`
    // says) through a band-pass then a high-pass; closed, pedal (a softer
    // onset, a little darker, longer) and open.
    bool hat(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto metal = std::clamp(v(hatMetal), 0.0, 1.0);
        const auto tone = std::clamp(v(hatTone), 0.0, 1.0);
        const auto pedal = piece_ == Piece::pedalHat;
        const auto open = piece_ == Piece::openHat;
        const auto centre = 7'800.0 * std::exp2(1.4 * (tone - 0.5) + 0.6 * shared.bright) * (pedal ? 0.8 : 1.0);
        const auto gBand = gainOf(centre, rate_);
        const auto gHigh = gainOf(centre * 0.7, rate_);
        const auto seconds = (open ? v(hatOpen) : v(hatClosed) * (pedal ? 1.5 : 1.0)) * shared.length;
        const auto decayFall = fall(std::max(0.004, seconds), rate_);
        const auto rise = pedal ? 1.0 / (0.004 * rate_) : 1.0;
        std::array<double, 6> increments {};
        metalIncrements(increments, std::exp2(std::clamp(v(hatTune), -24.0, 24.0) / 12.0) * shared.ratio);
        const auto squares = metal;
        const auto hiss = 0.15 + 0.85 * (1.0 - metal);
        const auto norm = pedal ? 2.6 : open ? 2.7 : 3.2;
        for (int i = 0; i < length; ++i) {
            const auto x = metals(increments) * squares + random() * hiss;
            const auto band = filters_[0].run(x, gBand, 0.9).band * 0.9;
            const auto high = filters_[1].run(band, gHigh, 1.3).high;
            envD_ = std::min(1.0, envD_ + rise);
            out[i] = high * envA_ * envD_ * norm;
            envA_ *= decayFall;
        }
        return envA_ > kSilent;
    }

    // The toms: a sine (or a triangle, as the tone says) whose pitch drops,
    // and the stick.
    bool tom(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto hz = std::clamp(v(tomTune), 30.0, 600.0) * std::exp2(kTomSteps[static_cast<std::size_t>(place_)] / 12.0) * shared.ratio;
        const auto drop = std::clamp(v(tomDrop), 0.0, 36.0);
        const auto tone = std::clamp(v(tomTone), 0.0, 1.0);
        const auto seconds = std::max(0.02, v(tomDecay) * shared.length * (1.0 - 0.12 * static_cast<double>(tom_)));
        const auto bodyFall = fall(seconds, rate_);
        const auto pitchFall = fallBy(std::max(0.008, 0.13 * seconds), rate_);
        const auto stickFall = fallBy(0.003, rate_);
        const auto gStick = gainOf(2'800.0 * std::exp2(shared.bright), rate_);
        const auto stick = (0.12 + 0.45 * tone) * shared.click;
        for (int i = 0; i < length; ++i) {
            const auto increment = std::min(0.45, hz * std::exp2(drop * pitchEnv_ / 12.0) / rate_);
            pitchEnv_ *= pitchFall;
            const auto sine = std::sin(2.0 * kPi * phaseA_);
            // The triangle in step with the sine (rising through nothing at the start).
            auto shifted = phaseA_ + 0.75;
            shifted -= std::floor(shifted);
            const auto triangle = 4.0 * std::abs(shifted - 0.5) - 1.0;
            phaseA_ += increment;
            phaseA_ -= std::floor(phaseA_);
            auto x = ((1.0 - tone) * sine + tone * triangle) * envA_;
            envA_ *= bodyFall;
            if (envB_ > 1.0e-5) {
                x += stick * 2.0 * filters_[0].run(random(), gStick, 1.0).band * envB_;
                envB_ *= stickFall;
            }
            out[i] = x * 0.85;
        }
        return envA_ > kSilent;
    }

    // The crash: the metal squares in two bands (the high one long, the low
    // one fading first), noise splashing at the start, a high-pass.
    bool crash(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto tone = std::clamp(v(crashTone), 0.0, 1.0);
        const auto seconds = std::max(0.05, v(crashDecay) * shared.length);
        const auto highFall = fall(seconds, rate_);
        const auto lowFall = fall(seconds * 0.35, rate_);
        const auto splashFall = fallBy(0.03, rate_);
        const auto gHigh = gainOf(7'500.0 * std::exp2(1.2 * (tone - 0.5) + 0.5 * shared.bright), rate_);
        const auto gLow = gainOf(3'300.0 * std::exp2(tone - 0.5 + 0.3 * shared.bright), rate_);
        const auto gClean = gainOf(1'600.0, rate_);
        std::array<double, 6> increments {};
        metalIncrements(increments, 1.47 * std::exp2(std::clamp(v(crashTune), -24.0, 24.0) / 12.0) * shared.ratio);
        for (int i = 0; i < length; ++i) {
            const auto metal = metals(increments);
            const auto white = random();
            const auto high = filters_[0].run(metal + white * (0.3 + 0.6 * envC_), gHigh, 1.0).band;
            const auto low = filters_[1].run(metal, gLow, 1.4).band * 1.4;
            const auto x = filters_[2].run(high * envA_ + 0.55 * low * envB_, gClean, 1.4).high;
            envA_ *= highFall;
            envB_ *= lowFall;
            envC_ *= splashFall;
            out[i] = x * 0.9;
        }
        return envA_ > kSilent;
    }

    // The ride: the metal squares through a narrow band (its ping), a bell
    // of three inharmonic partials, the stick.
    bool ride(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto tone = std::clamp(v(rideTone), 0.0, 1.0);
        const auto bell = std::clamp(v(rideBell), 0.0, 1.0);
        const auto seconds = std::max(0.05, v(rideDecay) * shared.length);
        const auto pingFall = fall(seconds, rate_);
        const auto bellFall = fall(seconds * 0.5, rate_);
        const auto stickFall = fallBy(0.002, rate_);
        const auto tune = std::exp2(std::clamp(v(rideTune), -24.0, 24.0) / 12.0) * shared.ratio;
        const auto gPing = gainOf(5'800.0 * std::exp2(1.2 * (tone - 0.5) + 0.5 * shared.bright), rate_);
        const auto gStick = gainOf(6'000.0 * std::exp2(shared.bright), rate_);
        const auto gClean = gainOf(450.0, rate_);
        std::array<double, 6> increments {};
        metalIncrements(increments, 1.9 * tune);
        const std::array<double, 3> bellSteps { std::min(0.45, kBell[0] * tune / rate_), std::min(0.45, kBell[1] * tune / rate_),
                                                std::min(0.45, kBell[2] * tune / rate_) };
        for (int i = 0; i < length; ++i) {
            const auto metal = metals(increments);
            const auto white = random();
            const auto ping = filters_[0].run(metal + 0.12 * white, gPing, 0.45).band * 0.45;
            const auto partials = std::sin(2.0 * kPi * phaseA_) + 0.6 * std::sin(2.0 * kPi * phaseB_) + 0.35 * std::sin(2.0 * kPi * phaseC_);
            phaseA_ += bellSteps[0];
            phaseA_ -= std::floor(phaseA_);
            phaseB_ += bellSteps[1];
            phaseB_ -= std::floor(phaseB_);
            phaseC_ += bellSteps[2];
            phaseC_ -= std::floor(phaseC_);
            const auto stick = filters_[1].run(white, gStick, 1.0).high * envC_ * shared.click;
            const auto x = filters_[2].run(ping * envA_ * 1.5 + partials * bell * 0.3 * envB_ + 0.35 * stick, gClean, 1.4).high;
            envA_ *= pingFall;
            envB_ *= bellFall;
            envC_ *= stickFall;
            out[i] = x * 0.95;
        }
        return envA_ > kSilent || (bell > 0.0 && envB_ > kSilent);
    }

    // The rimshot: two short resonances (about 1 : 3.5) and a tick, driven
    // hard, the lows taken out.
    bool rim(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto hz = std::clamp(v(rimTune), 100.0, 3'000.0) * shared.ratio;
        const auto seconds = std::max(0.004, v(rimDecay) * shared.length);
        const auto lowFall = fall(seconds, rate_);
        const auto highFall = fall(seconds * 0.55, rate_);
        const auto tickFall = fallBy(0.0006, rate_);
        const auto highPass = onePole(260.0, rate_);
        const auto step1 = std::min(0.45, hz / rate_);
        const auto step2 = std::min(0.45, hz * 3.47 / rate_);
        constexpr double drive = 2.4;
        const auto driveBack = 1.0 / std::tanh(drive);
        for (int i = 0; i < length; ++i) {
            auto x = std::sin(2.0 * kPi * phaseA_) * envA_ + 0.75 * std::sin(2.0 * kPi * phaseB_) * envB_
                     + 0.5 * shared.click * random() * envC_;
            phaseA_ += step1;
            phaseA_ -= std::floor(phaseA_);
            phaseB_ += step2;
            phaseB_ -= std::floor(phaseB_);
            envA_ *= lowFall;
            envB_ *= highFall;
            envC_ *= tickFall;
            x = std::tanh(drive * x) * driveBack;
            low_ += highPass * (x - low_);
            out[i] = (x - low_) * 0.8;
        }
        return envA_ > kSilent;
    }

    // The cowbell: two square oscillators (about 540 and 800 Hz) through a
    // band-pass; struck hard, then ringing.
    bool cowbell(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto tune = std::exp2(std::clamp(v(cowbellTune), -24.0, 24.0) / 12.0) * shared.ratio;
        const auto step1 = std::min(0.45, 540.0 * tune / rate_);
        const auto step2 = std::min(0.45, 800.0 * tune / rate_);
        const auto g = gainOf(1'100.0 * tune * std::exp2(0.3 * shared.bright), rate_);
        const auto strikeFall = fallBy(0.006, rate_);
        const auto ringFall = fall(std::max(0.01, v(cowbellDecay) * shared.length), rate_);
        for (int i = 0; i < length; ++i) {
            const auto squares = 0.5 * (oscillator(1, phaseA_, step1, 0.5) + oscillator(1, phaseB_, step2, 0.5));
            phaseA_ += step1;
            phaseA_ -= std::floor(phaseA_);
            phaseB_ += step2;
            phaseB_ -= std::floor(phaseB_);
            const auto band = filters_[0].run(squares, g, 1.1).band * 1.1;
            out[i] = (band + 0.15 * squares) * (0.6 * envA_ + 0.4 * envB_) * 1.1;
            envA_ *= strikeFall;
            envB_ *= ringFall;
        }
        return envB_ > kSilent;
    }

    // The claves: a short resonant ping, a faint overtone.
    bool clave(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto hz = std::clamp(v(claveTune), 300.0, 8'000.0) * shared.ratio;
        const auto seconds = std::max(0.005, v(claveDecay) * shared.length);
        const auto mainFall = fall(seconds, rate_);
        const auto overFall = fall(seconds * 0.35, rate_);
        const auto step1 = std::min(0.45, hz / rate_);
        const auto step2 = std::min(0.45, hz * 2.71 / rate_);
        for (int i = 0; i < length; ++i) {
            out[i] = (std::sin(2.0 * kPi * phaseA_) * envA_ + 0.22 * shared.click * std::sin(2.0 * kPi * phaseB_) * envB_) * 0.8;
            phaseA_ += step1;
            phaseA_ -= std::floor(phaseA_);
            phaseB_ += step2;
            phaseB_ -= std::floor(phaseB_);
            envA_ *= mainFall;
            envB_ *= overFall;
        }
        return envA_ > kSilent;
    }

    // The maracas: a burst of high-passed noise, rising quickly then falling.
    bool shaker(const Values& values, const Shared& shared, double* out, const int length) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto tone = std::clamp(v(shakerTone), 0.0, 1.0);
        const auto g = gainOf(5'200.0 * std::exp2(1.6 * (tone - 0.5) + 0.6 * shared.bright), rate_);
        const auto seconds = std::max(0.01, v(shakerDecay) * shared.length);
        const auto rise = 1.0 / (std::clamp(0.15 * seconds, 0.002, 0.025) * rate_);
        const auto decayFall = fall(seconds, rate_);
        for (int i = 0; i < length; ++i) {
            const auto high = filters_[0].run(random(), g, 1.0).high;
            out[i] = high * envA_ * envD_ * 0.75;
            if (envD_ < 1.0)
                envD_ = std::min(1.0, envD_ + rise);
            else
                envA_ *= decayFall;
        }
        return envA_ > kSilent;
    }

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    double rate_ { 48'000.0 };
    Piece piece_ { Piece::none };
    int note_ {};
    int tom_ {};
    int place_ {};
    double velocity_ { 1.0 };
    bool playing_ {};
    bool released_ {};
    double fade_ { 1.0 };
    double fadeStep_ {};
    double previousGain_ { -1.0 };
    double jitter_ { 1.0 };
    double levelJitter_ { 1.0 };
    std::array<double, 6> detune_ { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    std::array<double, 6> metal_ {};
    double phaseA_ {};
    double phaseB_ {};
    double phaseC_ {};
    double envA_ {};
    double envB_ {};
    double envC_ {};
    double envD_ {};
    double pitchEnv_ {};
    double low_ {};
    std::array<Svf, 3> filters_ {};
    double elapsed_ {};
    int burst_ {};
    std::array<double, 4> burstAt_ {};
    double tiltLow_ {};
    double warm_ {};
    double held_ {};
    double holdPhase_ { 1.0 };
    // Each voice its own draw: their noises apart.
    std::uint32_t seed_ { nextSeed() };
};

// A noise that is the same at the same place: a picture holds still.
double hashNoise(const std::uint32_t at) noexcept
{
    auto x = at * 0x9e3779b9u + 0x7f4a7c15u;
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return static_cast<double>(x >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
}

} // namespace

std::unique_ptr<Engine> makeSilex()
{
    return std::make_unique<SilexEngine>();
}

std::vector<float> silexPicture(const Values& values, const std::string_view part, const int points)
{
    // A piece struck once, from its start to well into its decay: "kick",
    // "snare", "clap", "hat" (the open one), "tom" (the low one), "crash" and
    // "ride", as their settings shape them (their noises drawn still).
    if (points < 2)
        return {};
    const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
    const auto length = std::clamp(v(kitDecay), 0.25, 4.0);
    const auto tune = std::exp2(std::clamp(v(kitTune), -24.0, 24.0) / 12.0);
    const auto last = static_cast<double>(points - 1);
    std::vector<double> wave(static_cast<std::size_t>(points), 0.0);
    const auto falling = [](const double t, const double seconds) { return std::exp(-kT60 * t / std::max(1.0e-4, seconds)); };
    // A sine whose pitch falls from `sweep` semitones above `hz` (time
    // constant `tau`), its phase summed finely between the points.
    const auto sweeping = [&](const double hz, const double sweep, const double tau, const double window, auto&& shape) {
        constexpr int fine = 16;
        const auto dt = window / last / static_cast<double>(fine);
        auto phase = 0.0;
        for (int at = 0; at < points; ++at) {
            const auto t = window * static_cast<double>(at) / last;
            wave[static_cast<std::size_t>(at)] = shape(t, phase, at);
            for (int step = 0; step < fine; ++step) {
                const auto moment = t + dt * static_cast<double>(step);
                phase += hz * std::exp2(sweep * std::exp(-moment / tau) / 12.0) * dt;
            }
            phase -= std::floor(phase);
        }
    };
    if (part == "kick") {
        const auto seconds = std::max(0.02, v(kickDecay) * length);
        const auto window = std::clamp(0.4 * seconds, 0.05, 0.6);
        const auto click = std::clamp(v(kickClick), 0.0, 1.0);
        const auto drive = std::clamp(v(kickDrive), 0.0, 1.0);
        const auto driveGain = 1.0 + 7.0 * drive;
        sweeping(std::clamp(v(kickTune), 20.0, 400.0) * tune, std::clamp(v(kickSweep), 0.0, 48.0), std::max(0.002, v(kickTime)), window,
                 [&](const double t, const double phase, const int at) {
                     auto x = std::sin(2.0 * kPi * phase) * falling(t, seconds)
                              + click * 0.45 * hashNoise(static_cast<std::uint32_t>(at)) * std::exp(-t / 0.0012);
                     if (drive > 0.0)
                         x += drive * (std::tanh(driveGain * x) / std::tanh(driveGain) - x);
                     return x;
                 });
    } else if (part == "tom") {
        const auto seconds = std::max(0.02, v(tomDecay) * length);
        const auto window = std::clamp(0.5 * seconds, 0.05, 0.6);
        const auto tone = std::clamp(v(tomTone), 0.0, 1.0);
        sweeping(std::clamp(v(tomTune), 30.0, 600.0) * tune, std::clamp(v(tomDrop), 0.0, 36.0), std::max(0.008, 0.13 * seconds), window,
                 [&](const double t, const double phase, const int at) {
                     auto shifted = phase + 0.75;
                     shifted -= std::floor(shifted);
                     const auto body = (1.0 - tone) * std::sin(2.0 * kPi * phase) + tone * (4.0 * std::abs(shifted - 0.5) - 1.0);
                     return body * falling(t, seconds)
                            + (0.12 + 0.45 * tone) * hashNoise(static_cast<std::uint32_t>(at)) * std::exp(-t / 0.003);
                 });
    } else if (part == "snare") {
        const auto seconds = std::max(0.01, v(snareDecay) * length);
        const auto window = std::clamp(0.6 * seconds, 0.04, 0.4);
        const auto tone = std::clamp(v(snareTone), 0.0, 1.0);
        const auto snappy = std::clamp(v(snareSnappy), 0.0, 1.0);
        const auto hz = std::clamp(v(snareTune), 50.0, 1'000.0) * tune;
        for (int at = 0; at < points; ++at) {
            const auto t = window * static_cast<double>(at) / last;
            const auto body = std::sin(2.0 * kPi * hz * t) * falling(t, seconds * 0.9)
                              + 0.55 * std::sin(2.0 * kPi * hz * 1.71 * t) * falling(t, seconds * 0.55);
            const auto wires = hashNoise(static_cast<std::uint32_t>(at)) * falling(t, seconds * (0.55 + 1.3 * snappy));
            wave[static_cast<std::size_t>(at)]
                = std::cos(tone * kPi * 0.5) * 0.7 * body + std::sin(tone * kPi * 0.5) * (0.55 + 0.6 * snappy) * 0.8 * wires;
        }
    } else if (part == "clap") {
        const auto spread = std::clamp(v(clapSpread), 0.002, 0.05);
        const auto seconds = std::max(0.02, v(clapDecay) * length);
        const auto window = std::clamp(3.0 * spread + 0.5 * seconds, 0.03, 0.6);
        for (int at = 0; at < points; ++at) {
            const auto t = window * static_cast<double>(at) / last;
            const auto burst = std::min(3.0, std::floor(t / spread));
            const auto since = t - burst * spread;
            const auto bursts = std::exp(-since / (spread * 0.32 + 0.001));
            const auto tail = t >= 3.0 * spread ? 0.7 * falling(t - 3.0 * spread, seconds) : 0.0;
            wave[static_cast<std::size_t>(at)] = hashNoise(static_cast<std::uint32_t>(at)) * (bursts + tail);
        }
    } else if (part == "hat" || part == "crash" || part == "ride") {
        // Metals: their squares (drawn coarse) and their noise, under their decay.
        const auto hat = part == "hat";
        const auto crash = part == "crash";
        const auto seconds = std::max(0.01, (hat ? v(hatOpen) : crash ? v(crashDecay) : v(rideDecay)) * length);
        const auto window = std::clamp(0.6 * seconds, 0.03, hat ? 0.6 : 1.5);
        const auto metal = hat ? std::clamp(v(hatMetal), 0.0, 1.0) : crash ? 0.6 : 0.85;
        const auto ratio = std::exp2((hat ? v(hatTune) : crash ? v(crashTune) : v(rideTune)) / 12.0) * tune * (hat ? 1.0 : crash ? 1.47 : 1.9);
        for (int at = 0; at < points; ++at) {
            const auto t = window * static_cast<double>(at) / last;
            auto squares = 0.0;
            for (const auto hz : kMetal)
                squares += std::sin(2.0 * kPi * hz * ratio * t) >= 0.0 ? 1.0 : -1.0;
            const auto x = metal * squares / 6.0 + (1.0 - metal) * hashNoise(static_cast<std::uint32_t>(at));
            wave[static_cast<std::size_t>(at)] = x * falling(t, seconds) * (crash ? 1.0 + 0.6 * std::exp(-t / 0.03) : 1.0);
        }
    } else {
        return {};
    }
    auto peak = 1.0e-9;
    for (const auto sample : wave)
        peak = std::max(peak, std::abs(sample));
    std::vector<float> result(static_cast<std::size_t>(points));
    for (std::size_t at = 0; at < result.size(); ++at)
        result[at] = static_cast<float>(wave[at] * 0.9 / peak);
    return result;
}

} // namespace cydaw::engine::synth::detail
