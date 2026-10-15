// Jade: a modelled electric piano of the synth family. A key throws a felt
// hammer (softer or harder, and harder still as the key is struck harder)
// at one of three vibrating parts, each read by its own pickup:
// - a thin tine coupled to a tone bar, read by an electromagnetic pickup: a
//   bell on the attack (the tine's second mode, near seven times the note),
//   a warm sustain (the tone bar keeps the fundamental alive), a bark when
//   struck hard as the tine swings close to the pickup's pole;
// - a steel reed read by an electrostatic pickup: nasal, rich in odd
//   harmonics as the reed leaves the plates' slot, growling when struck hard;
// - a short string struck and stopped by a pad, read by a magnetic pickup
//   along it: bright, percussive, funky, the pickup's place a comb.
// The tine and the reed are banks of modes (decaying sines) whose ratios and
// shapes come from a beam clamped at one end: where the hammer lands and
// where the pickup listens weigh them. The string is a delay line with its
// losses, struck at a place and read at another. Each pickup turns the motion
// into a voltage through its own curve (the flux through a coil and how fast
// it changes; the charge between plates): the closer, the more it bends.
// Then the hammer's thump and the damper's click, the pickup's resonance, a
// preamp that saturates, a tone stack (bass, body, treble), a tremolo that
// can pan the keys from side to side, and a light chorus. Tremolo and chorus
// turn with the song's clock, so that a chord moves as one.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Jade's settings after the shared voicing, in its table's order.
enum Jade : int {
    mechanismChoice = voicingFieldCount,
    tuneStretch,
    tuneDetune,
    hammerHardness,
    hammerVelocity,
    hammerPosition,
    velocityCurve,
    ampVelocity,
    tineDecay,
    tineTracking,
    tineDamper,
    tineBar,
    tineHarmonics,
    pickupProximity,
    pickupSymmetry,
    pickupPosition,
    pickupTone,
    noiseThump,
    noiseRelease,
    toneBass,
    toneBody,
    toneTreble,
    driveAmount,
    driveOutput,
    stereoSpread,
    tremoloShape,
    tremoloRate,
    tremoloSync,
    tremoloDepth,
    tremoloStereo,
    chorusRate,
    chorusDepth,
    chorusMix,
    jadeCount
};
static_assert(jadeCount == 39);

// The three mechanisms.
constexpr int tineMechanism = 0;
constexpr int reedMechanism = 1;
constexpr int stringMechanism = 2;
constexpr int mechanismCount = 3;

// ln(1000): a decay's sixty decibels.
constexpr double kT60 = 6.907755278982137;
// The lowest note the string's line holds, and the chorus's longest delay (s).
constexpr double kLowestString = 20.0;
constexpr double kChorusLongest = 0.014;
// One voice's level at the output, its pickup's voltage brought near -1..1.
constexpr double kOutput = 0.45;

// What each mechanism is made of.
struct Character {
    double decay;       // its sustain against the decay knob
    double fastShare;   // the quick part of its fundamental: its decay as a share of the slow part's
    double second;      // its second mode's ratio to the fundamental (middle C)
    double third;       // its third's
    double secondDecay; // their decays as shares of the fundamental's
    double thirdDecay;
    double upper;       // how loud its upper modes ring, harmonics at their middle
    double reach;       // how far into its pickup's curve it swings, the pickup closest
    double rest;        // where it rests on that curve, the symmetry at its middle
    double spread;      // how far the symmetry moves it: rest × (2 (1 - symmetry))^spread
    double hammer;      // its hammer's brightness against the others'
    double gain;        // its loudness against the others'
    double tilt;        // its level up the keyboard (octaves of gain an octave)
    double lowCut;      // Hz: what its preamp lets through below
    double coilQ;       // its pickup's resonance
    double colourHz;    // its preamp's colour: a bell
    double colourDb;
    double colourQ;
    double clickHz;     // the damper's click
    double clickLevel;
};

constexpr std::array<Character, mechanismCount> kCharacters { {
    // Tines: a long, warm sustain; the bell near seven times the note; the
    // tine off the pole's axis (the second harmonic, a bark when hard).
    { 1.0, 0.22, 7.0, 19.6, 0.06, 0.015, 1.0, 3.3, 1.2, 0.4, 1.0, 1.0, 1.0 / 6.0, 25.0, 1.1, 1'000.0, 0.0, 0.7, 1'100.0, 1.0 },
    // Reeds: shorter, the reed's own modes (its tip weighted); nearly
    // centred in its slot (odd harmonics, a growl when hard), nasal.
    { 0.7, 0.3, 5.9, 15.9, 0.05, 0.015, 2.0, 6.5, 0.15, 2.0, 1.3, 0.85, -1.0 / 6.0, 45.0, 0.75, 1'050.0, 3.0, 0.9, 1'600.0, 0.9 },
    // Struck strings: shorter, bright, a snap as the pad stops them.
    { 0.6, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 1.0, 0.5, 0.6, 2.0, 1.0 / 12.0, 40.0, 1.5, 2'300.0, 4.0, 1.4, 2'400.0, 1.8 },
} };

const std::vector<std::pair<std::string, std::string>> kShapes { { "Sinus", "Sine" }, { "Triangle", "Triangle" },
                                                                  { "Dent de scie", "Saw" }, { "Carré", "Square" },
                                                                  { "Aléatoire", "Random" } };

// A beam clamped at one end: the shape of its mode `k` (0..2) at `place` (0
// at the clamp, 1 at the free end, where it is ±1).
constexpr std::array<double, 3> kBeta { 1.8751040687, 4.6940911330, 7.8547574382 };
constexpr std::array<double, 3> kSigma { 0.7340955137, 1.0184664400, 0.9992244896 };

double cantilever(const std::size_t k, const double place) noexcept
{
    const auto b = kBeta[k] * std::clamp(place, 0.0, 1.0);
    return 0.5 * (std::cosh(b) - std::cos(b) - kSigma[k] * (std::sinh(b) - std::sin(b)));
}

// The electromagnetic pickup: the flux through its coil as the steel's tip
// passes its pole (a bell, 1 / (1 + u²)); its voltage is how fast it changes.
double flux(const double u) noexcept
{
    return 1.0 / (1.0 + u * u);
}

// The electrostatic pickup: the charge as the reed enters and leaves the
// slot between its plates, flattening at both ends.
double charge(const double u) noexcept
{
    const auto u2 = u * u;
    return u / std::sqrt(std::sqrt(1.0 + u2 * u2));
}

// Its slope (where the reed rests: the small swings' gain).
double chargeSlope(const double u) noexcept
{
    const auto u2 = u * u;
    return 1.0 / std::pow(1.0 + u2 * u2, 1.25);
}

double fraction(const double x) noexcept
{
    return x - std::floor(x);
}

// A number in -1..1 that a whole number always gives: a key's own small
// mistuning, the same every time it sounds.
double scatter(const long long seed) noexcept
{
    auto n = static_cast<std::uint32_t>(seed) * 0x9e3779b1u;
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<double>(n) / 4'294'967'295.0 * 2.0 - 1.0;
}

double hzOf(const double pitch) noexcept
{
    return 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0);
}

int mechanismOf(const Values& values) noexcept
{
    return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(mechanismChoice)])), 0, mechanismCount - 1);
}

// The stretch (cents, sharper up the keyboard, flatter down it, A4 where it
// is) and each key's own small error.
double tuningCents(const Values& values, const double pitch, const double key) noexcept
{
    const auto from = (pitch - 69.0) / 36.0;
    const auto stretch = std::clamp(static_cast<double>(values[static_cast<std::size_t>(tuneStretch)]), 0.0, 100.0);
    const auto detune = std::clamp(static_cast<double>(values[static_cast<std::size_t>(tuneDetune)]), 0.0, 100.0);
    return stretch * (from < 0.0 ? -1.0 : 1.0) * std::pow(std::abs(from), 1.6) + detune * scatter(std::llround(key));
}

// How far into its pickup's curve the motion reaches (the pickup closer; the
// low keys swing wider) and where it rests on it (the pickup less symmetric).
double reachOf(const Values& values, const int mechanism, const double key) noexcept
{
    const auto proximity = std::clamp(static_cast<double>(values[static_cast<std::size_t>(pickupProximity)]), 0.0, 1.0);
    return kCharacters[static_cast<std::size_t>(mechanism)].reach * (0.1 + 0.9 * std::pow(proximity, 1.3))
           * std::clamp(std::pow(2.0, -(key - 60.0) / 30.0), 0.4, 1.6);
}

double offsetOf(const Values& values, const int mechanism) noexcept
{
    const auto symmetry = std::clamp(static_cast<double>(values[static_cast<std::size_t>(pickupSymmetry)]), 0.0, 1.0);
    const auto& character = kCharacters[static_cast<std::size_t>(mechanism)];
    return character.rest * std::pow(2.0 * (1.0 - symmetry), character.spread);
}

// The velocity as the hammer feels it (its curve).
double touchOf(const Values& values, const double velocity) noexcept
{
    const auto curve = std::clamp(static_cast<double>(values[static_cast<std::size_t>(velocityCurve)]), -1.0, 1.0);
    return std::pow(std::clamp(velocity, 0.0, 1.0), std::pow(2.0, -1.4 * curve));
}

} // namespace

std::vector<Section> jadeSections()
{
    return { { "VOIX", "VOICES", 0xffb0b8c8u },        { "MÉCANIQUE", "MECHANISM", 0xff5fc89au },
             { "MARTEAU", "HAMMER", 0xffffb04au },     { "LAME / ANCHE / CORDE", "TINE / REED / STRING", 0xffd9c58cu },
             { "MICRO", "PICKUP", 0xffff7a52u },       { "BRUITS", "NOISES", 0xff9aa4b8u },
             { "TONALITÉ", "TONE", 0xffff5f6du },      { "TRÉMOLO", "TREMOLO", 0xffa58cffu },
             { "CHORUS", "CHORUS", 0xff4fd1c5u } };
}

std::vector<Parameter> jadeParameters()
{
    Table table;
    addVoicing(table, 16, 0.5f);
    table.section = 1;
    table.choice("model", "Mécanique", "Mechanism", { { "Lames", "Tines" }, { "Anches", "Reeds" }, { "Cordes frappées", "Struck strings" } },
                 tineMechanism);
    table.add("tune.stretch", "Accord étiré", "Stretch tuning", "ct", 0.0f, 30.0f, 3.0f);
    table.add("tune.detune", "Désaccord des touches", "Key detune", "ct", 0.0f, 20.0f, 1.5f);
    table.section = 2;
    table.add("hammer.hardness", "Marteau · dureté", "Hammer · hardness", {}, 0.0f, 1.0f, 0.45f);
    table.add("hammer.velocity", "Vélocité → dureté", "Velocity → hardness", {}, 0.0f, 1.0f, 0.6f);
    table.add("hammer.position", "Marteau · point de frappe", "Hammer · strike point", {}, 0.0f, 1.0f, 0.3f);
    table.add("velocity.curve", "Courbe de vélocité", "Velocity curve", {}, -1.0f, 1.0f, 0.0f);
    table.add("amp.velocity", "Vélocité → volume", "Velocity → volume", {}, 0.0f, 1.0f, 0.7f);
    table.section = 3;
    table.add("tine.decay", "Déclin", "Decay", "s", 0.3f, 30.0f, 6.0f, Kind::continuous, true);
    table.add("tine.tracking", "Déclin · suivi du clavier", "Decay · key tracking", {}, 0.0f, 1.0f, 0.55f);
    table.add("tine.damper", "Étouffoir", "Damper", "s", 0.01f, 3.0f, 0.1f, Kind::continuous, true);
    table.add("tine.bar", "Barre de ton / corps", "Tone bar / body", {}, 0.0f, 1.0f, 0.55f);
    table.add("tine.harmonics", "Harmoniques", "Harmonics", {}, 0.0f, 1.0f, 0.5f);
    table.section = 4;
    table.add("pickup.proximity", "Micro · proximité", "Pickup · proximity", {}, 0.0f, 1.0f, 0.4f);
    table.add("pickup.symmetry", "Micro · symétrie", "Pickup · symmetry", {}, 0.0f, 1.0f, 0.5f);
    table.add("pickup.position", "Micro · position", "Pickup · position", {}, 0.0f, 1.0f, 0.5f);
    table.add("pickup.tone", "Micro · résonance", "Pickup · resonance", "Hz", 1'000.0f, 16'000.0f, 7'000.0f, Kind::continuous, true);
    table.section = 5;
    table.add("noise.thump", "Choc du marteau", "Hammer thump", {}, 0.0f, 1.0f, 0.3f);
    table.add("noise.release", "Clic de relâche", "Release click", {}, 0.0f, 1.0f, 0.3f);
    table.section = 6;
    table.add("tone.bass", "Graves", "Bass", "dB", -12.0f, 12.0f, 0.0f);
    table.add("tone.body", "Corps (médiums)", "Body (mids)", "dB", -12.0f, 12.0f, 0.0f);
    table.add("tone.treble", "Aigus", "Treble", "dB", -12.0f, 12.0f, 0.0f);
    table.add("drive.amount", "Préampli · saturation", "Preamp · drive", {}, 0.0f, 1.0f, 0.1f);
    table.add("drive.output", "Sortie", "Output", "dB", -18.0f, 6.0f, 0.0f);
    table.add("stereo.spread", "Largeur du clavier", "Key spread", {}, 0.0f, 1.0f, 0.25f);
    table.section = 7;
    table.choice("tremolo.shape", "Trémolo · forme", "Tremolo · shape", kShapes, 0);
    table.add("tremolo.rate", "Trémolo · vitesse", "Tremolo · rate", "Hz", 0.1f, 15.0f, 4.5f, Kind::continuous, true);
    table.whole("tremolo.sync", "Trémolo · synchro", "Tremolo · sync", {}, 0, 12, 0);
    table.add("tremolo.depth", "Trémolo · profondeur", "Tremolo · depth", {}, 0.0f, 1.0f, 0.0f);
    table.add("tremolo.stereo", "Trémolo · stéréo (panoramique)", "Tremolo · stereo (auto-pan)", {}, 0.0f, 1.0f, 1.0f);
    table.section = 8;
    table.add("chorus.rate", "Chorus · vitesse", "Chorus · rate", "Hz", 0.05f, 5.0f, 0.7f, Kind::continuous, true);
    table.add("chorus.depth", "Chorus · profondeur", "Chorus · depth", {}, 0.0f, 1.0f, 0.5f);
    table.add("chorus.mix", "Chorus · mélange", "Chorus · mix", {}, 0.0f, 1.0f, 0.0f);
    return table.parameters;
}

namespace {

using namespace parts;

// A delay line sized off the audio thread, read between its samples (cubic
// Hermite).
class Line {
public:
    void prepare(const double samples)
    {
        std::size_t size = 64;
        while (static_cast<double>(size) < samples + 8.0)
            size *= 2;
        buffer_.assign(size, 0.0f);
        mask_ = static_cast<int>(size) - 1;
        write_ = 0;
    }
    [[nodiscard]] bool ready() const noexcept { return !buffer_.empty(); }
    // The longest delay it reads.
    [[nodiscard]] double longest() const noexcept { return static_cast<double>(std::max(2, mask_ - 4)); }
    void clear() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    void push(const double x) noexcept
    {
        if (buffer_.empty())
            return;
        write_ = (write_ + 1) & mask_;
        buffer_[static_cast<std::size_t>(write_)] = static_cast<float>(x);
    }
    // What was pushed `samples` ago (0: the last one; at least two).
    [[nodiscard]] double read(double samples) const noexcept
    {
        if (buffer_.empty())
            return 0.0;
        samples = std::clamp(samples, 2.0, longest());
        const auto position = static_cast<double>(write_) - samples;
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto at = static_cast<int>(whole);
        const auto sample = [this](const int index) { return static_cast<double>(buffer_[static_cast<std::size_t>(index & mask_)]); };
        const auto before = sample(at - 1);
        const auto x0 = sample(at);
        const auto x1 = sample(at + 1);
        const auto x2 = sample(at + 2);
        const auto c1 = 0.5 * (x1 - before);
        const auto c2 = before - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const auto c3 = 0.5 * (x2 - before) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    std::vector<float> buffer_;
    int mask_ {};
    int write_ {};
};

// One biquad section and its state.
struct Biquad {
    BiquadCoefficients c;
    double z1 {};
    double z2 {};
    void reset() noexcept { z1 = z2 = 0.0; }
    double run(const double x) noexcept
    {
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// A band-pass of two poles (state variable), its gain one at its centre:
// the noises' colours.
struct BandPass {
    double ic1 {};
    double ic2 {};
    double a1 {};
    double a2 {};
    double a3 {};
    double k { 1.0 };

    void reset() noexcept { ic1 = ic2 = 0.0; }
    void tune(const double hz, const double q, const double rate) noexcept
    {
        const auto g = std::tan(kPi * std::clamp(hz, 20.0, rate * 0.45) / rate);
        k = 1.0 / std::max(0.1, q);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    double run(const double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v1 * k;
    }
};

class JadeEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        string_.prepare(rate_ / kLowestString + 8.0);
        chorus_.prepare(kChorusLongest * rate_ + 8.0);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        mechanism_ = mechanismOf(values);
        // Everything at rest.
        re_.fill(0.0);
        im_.fill(0.0);
        if (mechanism_ == stringMechanism)
            string_.clear();
        chorus_.clear();
        loss_ = 0.0;
        push_ = 0.0;
        stringLevel_ = 0.0;
        dcLow_ = 0.0;
        driveLow_ = 0.0;
        coil_.reset();
        colour_.reset();
        bass_.reset();
        body_.reset();
        treble_.reset();
        thumpFilter_.reset();
        knockFilter_.reset();
        clickFilter_.reset();
        thump_ = knock_ = click_ = 0.0;
        tunedPitch_ = std::numeric_limits<double>::quiet_NaN();
        stage_ = Stage::held;
        quiet_ = false;
        silentSteps_ = 0;
        fresh_ = true;
        previousGain_ = 0.0;
        // The pickup's curve where it is set, the motion at rest on it.
        reach_ = reachOf(values, mechanism_, target_);
        offset_ = offsetOf(values, mechanism_);
        lastFlux_ = flux(offset_);
        strike(values);
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        // Legato: the ringing part slides to the note; struck again only
        // once let go of (or rung out).
        if (stage_ == Stage::released || quiet_) {
            if (mechanismOf(values) != mechanism_) {
                // Another mechanism: the old one silenced, the pickup's curve its own.
                mechanism_ = mechanismOf(values);
                re_.fill(0.0);
                im_.fill(0.0);
                string_.clear();
                loss_ = 0.0;
                stringLevel_ = 0.0;
                tunedPitch_ = std::numeric_limits<double>::quiet_NaN();
                reach_ = reachOf(values, mechanism_, target_);
                offset_ = offsetOf(values, mechanism_);
                lastFlux_ = flux(offset_);
                resetChain();
            }
            stage_ = Stage::held;
            quiet_ = false;
            silentSteps_ = 0;
            strike(values);
        }
    }

    void release() noexcept override
    {
        if (stage_ != Stage::held)
            return;
        stage_ = Stage::released;
        if (quiet_)
            return;
        // The damper lands: a click, louder as the part still swings, and a
        // softer thud.
        const auto swinging = std::min(1.0, amplitude_ * 3.0);
        const auto amount = releaseAmount_ * kCharacters[static_cast<std::size_t>(mechanism_)].clickLevel * level_;
        click_ += amount * 1.0 * (0.3 + 0.7 * swinging);
        thump_ += amount * 0.5 * (0.2 + 0.8 * swinging);
        clickFilter_.tune(kCharacters[static_cast<std::size_t>(mechanism_)].clickHz, 1.4, rate_);
    }

    void kill() noexcept override
    {
        stage_ = Stage::idle;
        quiet_ = false;
    }

    [[nodiscard]] bool active() const noexcept override { return stage_ != Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept override { return stage_ == Stage::released; }
    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        const auto& character = kCharacters[static_cast<std::size_t>(mechanism_)];
        const auto strings = mechanism_ == stringMechanism;
        const auto released = stage_ == Stage::released;
        releaseAmount_ = std::clamp(v(noiseRelease), 0.0, 1.0);
        show(hammerHardness, hardness_, v(hammerHardness));

        // The tremolo and the chorus: their rates, their phases where the
        // song's clock puts them on a voice's first block.
        const auto tremoloHz = [&]() {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(tremoloSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return std::clamp(v(tremoloRate), 0.01, 40.0);
        }();
        const auto chorusHz = std::clamp(v(chorusRate), 0.01, 20.0);
        if (fresh_) {
            fresh_ = false;
            const auto clock = std::max(0.0, context.clockSeconds);
            const auto turns = tremoloHz * clock;
            tremoloCycle_ = static_cast<long long>(std::floor(turns));
            tremoloPhase_ = turns - std::floor(turns);
            chorusPhase_ = fraction(chorusHz * clock);
            tremoloSmooth_ = lfoWave(choice(tremoloShape, 5), tremoloPhase_, scatter(tremoloCycle_));
        }
        // Held but rung out: nothing to play until struck again; let go of, it ends.
        if (quiet_ && stage_ == Stage::released) {
            stage_ = Stage::idle;
            quiet_ = false;
            return false;
        }
        if (quiet_) {
            const auto seconds = static_cast<double>(count) / rate_;
            const auto turns = tremoloPhase_ + tremoloHz * seconds;
            tremoloCycle_ += static_cast<long long>(std::floor(turns));
            tremoloPhase_ = fraction(turns);
            chorusPhase_ = fraction(chorusPhase_ + chorusHz * seconds);
            showEnvelope(0, 0.0);
            return true;
        }

        // What holds for the whole block. The decays: the knob's at middle C,
        // shorter up the keyboard as far as the tracking says.
        const auto tracking = std::clamp(v(tineTracking), 0.0, 1.0);
        const auto sustain = std::clamp(v(tineDecay), 0.05, 60.0) * character.decay * std::pow(2.0, -(target_ - 60.0) / 12.0 * tracking);
        const auto damper = std::clamp(v(tineDamper), 0.005, 10.0);
        const auto bar = std::clamp(v(tineBar), 0.0, 1.0);
        const auto harmonics = std::clamp(v(tineHarmonics), 0.0, 1.0);
        if (!strings) {
            // The fundamental's quick and slow parts (the tone bar, the body
            // the reed is screwed to), then the upper modes.
            std::array<double, modeCount> seconds { sustain * character.fastShare, sustain,
                                                    std::clamp(sustain * character.secondDecay, 0.02, 2.0),
                                                    std::clamp(sustain * character.thirdDecay, 0.01, 0.5) };
            if (released) {
                seconds[0] = std::min(seconds[0], damper);
                seconds[1] = std::min(seconds[1], damper);
                seconds[2] = std::min(seconds[2], damper * 0.5);
                seconds[3] = std::min(seconds[3], damper * 0.3);
            }
            for (std::size_t k = 0; k < modeCount; ++k)
                radius_[k] = std::exp(-kT60 / (std::max(0.002, seconds[k]) * rate_));
            // Where the pickup listens weighs each mode (the second's node
            // near three quarters of the way to the tip); how much of each the
            // tone bar and the harmonics let through.
            const auto listen = 0.7 + 0.3 * std::clamp(v(pickupPosition), 0.0, 1.0);
            const auto first = cantilever(0, listen);
            const auto upper = character.upper * 2.0 * harmonics;
            output_[0] = 0.15 + 0.85 * (1.0 - bar);
            output_[1] = 0.15 + 0.85 * bar;
            output_[2] = upper * cantilever(1, listen) / first;
            output_[3] = upper * 0.6 * cantilever(2, listen) / first;
        }
        // The string: how long its fundamental rings (the body balance
        // lengthens it) and how long its high harmonics do (the harmonics
        // say; the pad stops them sooner).
        const auto stringSeconds = std::max(0.005, released ? damper : sustain * (0.3 + 1.4 * bar));
        const auto highSeconds = std::max(0.002, stringSeconds * (released ? 0.5 : 0.04 + 0.6 * harmonics * harmonics));
        const auto listenGap = 0.04 + 0.42 * std::clamp(v(pickupPosition), 0.0, 1.0);
        // The pickup's curve, eased toward its knobs.
        const auto reachAim = reachOf(values, mechanism_, target_);
        const auto offsetAim = offsetOf(values, mechanism_);
        const auto curveFollow = 1.0 - std::exp(-static_cast<double>(step) / (0.04 * rate_));
        // The preamp: its low cut, the pickup's resonance, its colour, the
        // drive (a tube's curve, leaning to one side as it is pushed), the
        // tone stack.
        const auto dcFollow = 1.0 - std::exp(-2.0 * kPi * character.lowCut / rate_);
        coil_.c = designEqBand(EqBandType::highCut, std::clamp(v(pickupTone), 200.0, 30'000.0), 0.0, character.coilQ, 0, 0, rate_);
        const auto colourOn = std::abs(character.colourDb) > 0.01;
        if (colourOn)
            colour_.c = designEqBand(EqBandType::bell, character.colourHz, character.colourDb, character.colourQ, 0, 0, rate_);
        const auto drive = std::clamp(v(driveAmount), 0.0, 1.0);
        const auto driveOn = drive > 1.0e-4;
        const auto driveGain = 0.05 + 14.0 * std::pow(drive, 1.6);
        const auto bias = 0.35 * drive;
        const auto biasTanh = std::tanh(bias);
        const auto driveNorm = 0.5 / std::max(1.0e-9, std::tanh(0.5 * driveGain + bias) - biasTanh);
        const auto driveFollow = 1.0 - std::exp(-2.0 * kPi * 15.0 / rate_);
        const auto bassDb = std::clamp(v(toneBass), -24.0, 24.0);
        const auto bodyDb = std::clamp(v(toneBody), -24.0, 24.0);
        const auto trebleDb = std::clamp(v(toneTreble), -24.0, 24.0);
        const auto bassOn = std::abs(bassDb) > 0.01;
        const auto bodyOn = std::abs(bodyDb) > 0.01;
        const auto trebleOn = std::abs(trebleDb) > 0.01;
        if (bassOn)
            bass_.c = designEqBand(EqBandType::lowShelf, 150.0, bassDb, 0.7, 0, 0, rate_);
        if (bodyOn)
            body_.c = designEqBand(EqBandType::bell, 650.0, bodyDb, 0.8, 0, 0, rate_);
        if (trebleOn)
            treble_.c = designEqBand(EqBandType::highShelf, 3'500.0, trebleDb, 0.7, 0, 0, rate_);
        // The level: the velocity's (the motion already follows it), the
        // mechanism's, the keyboard evened out (a tine's treble lifted a
        // little, a reed's bass); then the output, the master, the expression.
        const auto pre = level_ / std::max(0.05, swing_) * character.gain * std::pow(2.0, (target_ - 60.0) / 12.0 * character.tilt);
        const auto gainAim = kOutput * v(voicingMaster) * std::pow(10.0, std::clamp(v(driveOutput), -60.0, 24.0) / 20.0)
                             * std::max(0.0, expressionGain);
        // The tremolo (and its pan) and the keys across the stereo.
        const auto shape = choice(tremoloShape, 5);
        const auto depth = std::clamp(v(tremoloDepth), 0.0, 1.0);
        const auto panning = std::clamp(v(tremoloStereo), 0.0, 1.0);
        const auto tremoloFollow = 1.0 - std::exp(-static_cast<double>(step) / (0.003 * rate_));
        const auto keyPan = std::clamp(v(stereoSpread), 0.0, 1.0) * std::clamp((target_ - 60.0) / 36.0, -1.0, 1.0);
        // The chorus: a line swept on two taps a quarter turn apart.
        const auto chorusAmount = std::clamp(v(chorusMix), 0.0, 1.0);
        const auto chorusOn = chorusAmount > 1.0e-4 && chorus_.ready();
        const auto chorusCentre = 0.007 * rate_;
        const auto chorusSwing = std::clamp(v(chorusDepth), 0.0, 1.0) * 0.0035 * rate_;
        const auto dry = 1.0 - 0.5 * chorusAmount;
        const auto wet = 0.5 * chorusAmount;
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));

        std::array<double, step> motion {};
        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto inverse = 1.0 / static_cast<double>(length);
            const auto seconds = static_cast<double>(length) / rate_;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto played = pitch_ + expressionPitch + bend + tuningCents(values, pitch_, target_) / 100.0;
            const auto hz = std::clamp(hzOf(played), 1.0, rate_ * 0.3);
            // The pickup's curve this step (eased from where it was).
            const auto reachFrom = reach_;
            const auto offsetFrom = offset_;
            reach_ += (reachAim - reach_) * curveFollow;
            offset_ += (offsetAim - offset_) * curveFollow;

            // The motion at the pickup.
            auto stepPeak = 0.0;
            if (!strings) {
                if (played != tunedPitch_ || rate_ != tunedRate_) {
                    tuneModes(played, hz);
                    tunedPitch_ = played;
                    tunedRate_ = rate_;
                }
                std::array<double, modeCount> turnRe {};
                std::array<double, modeCount> turnIm {};
                for (std::size_t k = 0; k < modeCount; ++k) {
                    turnRe[k] = radius_[k] * cos_[k];
                    turnIm[k] = radius_[k] * sin_[k];
                }
                for (int i = 0; i < length; ++i) {
                    const auto force = hammer();
                    auto x = 0.0;
                    for (std::size_t k = 0; k < modeCount; ++k) {
                        const auto re = re_[k] * turnRe[k] - im_[k] * turnIm[k] + input_[k] * force;
                        const auto im = re_[k] * turnIm[k] + im_[k] * turnRe[k];
                        re_[k] = re;
                        im_[k] = im;
                        x += output_[k] * im;
                    }
                    motion[static_cast<std::size_t>(i)] = x;
                }
                auto heard = 0.0;
                for (std::size_t k = 0; k < modeCount; ++k)
                    heard += std::abs(output_[k]) * std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]);
                stepPeak = heard;
            } else if (string_.ready()) {
                // A taut string: higher as it swings wide, settling.
                tension_ *= std::exp(-seconds / 0.07);
                const auto stretched = std::min(hz * (1.0 + tensionDepth_ * tension_), rate_ * 0.3);
                const auto period = rate_ / stretched;
                // Its losses each period: all of it as its fundamental's
                // decay says, and a low-pass for what the high ones lose more.
                const auto reference = std::clamp(std::max(3'000.0, 4.0 * stretched), 0.0, rate_ * 0.45);
                const auto keep = std::pow(10.0, -3.0 / stretched * std::max(0.0, 1.0 / highSeconds - 1.0 / stringSeconds));
                const auto loss = lossFor(keep, 2.0 * kPi * reference / rate_);
                // The fundamental's own loss (what the low-pass takes from it given back).
                const auto w = 2.0 * kPi * stretched / rate_;
                const auto passed = (1.0 - loss) / std::sqrt(std::max(1.0e-12, 1.0 - 2.0 * loss * std::cos(w) + loss * loss));
                const auto gain = std::min(std::pow(10.0, -3.0 / (stringSeconds * stretched)) / std::max(1.0e-6, passed), 0.99999);
                // The loop as long as one period, the loss filter's delay taken from it.
                const auto filterDelay = std::atan2(loss * std::sin(w), 1.0 - loss * std::cos(w)) / w;
                const auto loop = std::clamp(period - filterDelay - 1.0, 2.0, string_.longest());
                const auto gap = std::max(2.0, listenGap * period);
                for (int i = 0; i < length; ++i) {
                    // The hammer's blow, struck at its place: the string pushed between.
                    if (contactAt_ < pulseLength_) {
                        push_ += hammer();
                        if (contactAt_ >= pulseLength_)
                            push_ = 0.0;
                    }
                    const auto back = string_.read(loop);
                    loss_ = back + loss * (loss_ - back);
                    auto y = gain * loss_ + push_ * excite_;
                    // Never kept running away.
                    if (std::abs(y) > 8.0)
                        y = 8.0 * std::tanh(y * 0.125);
                    string_.push(y);
                    const auto x = 0.5 * (y - string_.read(gap));
                    motion[static_cast<std::size_t>(i)] = x;
                    stepPeak = std::max(stepPeak, std::abs(y));
                }
                stringLevel_ = std::max(stepPeak, stringLevel_ * std::exp(-static_cast<double>(length) / (2.0 * period)));
                stepPeak = stringLevel_;
                if (!std::isfinite(stepPeak)) {
                    string_.clear();
                    loss_ = 0.0;
                    stringLevel_ = 0.0;
                    stepPeak = 0.0;
                }
            }
            amplitude_ = stepPeak;

            // The pickup turns the motion into a voltage: the flux's change
            // (tines, strings) or the charge (reeds), as loud whatever how
            // far into its curve it swings.
            const auto derivative = 1.0 / std::max(1.0e-6, 2.0 * std::sin(kPi * std::min(hz, rate_ * 0.3) / rate_));
            const auto normFrom = 1.0 / std::pow(std::max(0.01, reachFrom), 0.75);
            const auto normTo = 1.0 / std::pow(std::max(0.01, reach_), 0.75);
            if (mechanism_ == reedMechanism) {
                const auto restFrom = charge(offsetFrom);
                const auto restTo = charge(offset_);
                // The charge flattens at both ends: a reed swung far in is as
                // loud, not quieter, so its curve's reach weighs less.
                const auto slopeFrom = 1.0 / (std::sqrt(std::max(0.01, reachFrom)) * chargeSlope(offsetFrom));
                const auto slopeTo = 1.0 / (std::sqrt(std::max(0.01, reach_)) * chargeSlope(offset_));
                for (int i = 0; i < length; ++i) {
                    const auto t = static_cast<double>(i + 1) * inverse;
                    const auto u = (reachFrom + (reach_ - reachFrom) * t) * motion[static_cast<std::size_t>(i)] + offsetFrom
                                   + (offset_ - offsetFrom) * t;
                    motion[static_cast<std::size_t>(i)]
                        = (charge(u) - (restFrom + (restTo - restFrom) * t)) * (slopeFrom + (slopeTo - slopeFrom) * t);
                }
            } else {
                const auto scaleFrom = derivative * normFrom / 0.65;
                const auto scaleTo = derivative * normTo / 0.65;
                for (int i = 0; i < length; ++i) {
                    const auto t = static_cast<double>(i + 1) * inverse;
                    const auto u = (reachFrom + (reach_ - reachFrom) * t) * motion[static_cast<std::size_t>(i)] + offsetFrom
                                   + (offset_ - offsetFrom) * t;
                    const auto now = flux(u);
                    motion[static_cast<std::size_t>(i)] = (now - lastFlux_) * (scaleFrom + (scaleTo - scaleFrom) * t);
                    lastFlux_ = now;
                }
            }

            // The tremolo, the pan and the chorus's sweep this step.
            const auto turns = tremoloPhase_ + tremoloHz * seconds;
            if (turns >= 1.0)
                tremoloCycle_ += static_cast<long long>(std::floor(turns));
            tremoloPhase_ = fraction(turns);
            const auto lfo = lfoWave(shape, tremoloPhase_, scatter(tremoloCycle_));
            tremoloSmooth_ += (lfo - tremoloSmooth_) * tremoloFollow;
            const auto throb = 1.0 - depth * (1.0 - panning) * 0.5 * (1.0 - tremoloSmooth_);
            const auto pan = std::clamp(keyPan + depth * panning * tremoloSmooth_, -1.0, 1.0);
            const auto gainTo = gainAim * throb;
            const auto leftTo = gainTo * std::cos((pan + 1.0) * kPi * 0.25) * std::sqrt(2.0);
            const auto rightTo = gainTo * std::sin((pan + 1.0) * kPi * 0.25) * std::sqrt(2.0);
            if (previousGain_ <= 0.0) {
                previousLeft_ = 0.0;
                previousRight_ = 0.0;
            }
            const auto leftFrom = previousLeft_;
            const auto rightFrom = previousRight_;
            previousLeft_ = leftTo;
            previousRight_ = rightTo;
            previousGain_ = gainTo;
            chorusPhase_ = fraction(chorusPhase_ + chorusHz * seconds);
            const auto sweepTo = std::sin(2.0 * kPi * chorusPhase_);
            const auto sweepOtherTo = std::cos(2.0 * kPi * chorusPhase_);
            const auto sweepFrom = chorusSweep_;
            const auto sweepOtherFrom = chorusSweepOther_;
            chorusSweep_ = sweepTo;
            chorusSweepOther_ = sweepOtherTo;
            showLfo(0, tremoloSmooth_ * depth, tremoloPhase_);
            showLfo(1, chorusOn ? sweepTo : 0.0, chorusPhase_);
            showEnvelope(0, std::min(1.0, amplitude_ * pre));

            const auto noisy = thump_ > 1.0e-7 || knock_ > 1.0e-7 || click_ > 1.0e-7;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) * inverse;
                auto y = motion[static_cast<std::size_t>(i)] * pre;
                if (noisy) {
                    y += thumpFilter_.run(random()) * thump_ + knockFilter_.run(random()) * knock_ + clickFilter_.run(random()) * click_;
                    thump_ *= thumpFall_;
                    knock_ *= knockFall_;
                    click_ *= clickFall_;
                }
                dcLow_ += dcFollow * (y - dcLow_);
                y = coil_.run(y - dcLow_);
                if (colourOn)
                    y = colour_.run(y);
                if (driveOn) {
                    y = (std::tanh(driveGain * y + bias) - biasTanh) * driveNorm;
                    driveLow_ += driveFollow * (y - driveLow_);
                    y -= driveLow_;
                }
                if (bassOn)
                    y = bass_.run(y);
                if (bodyOn)
                    y = body_.run(y);
                if (trebleOn)
                    y = treble_.run(y);
                chorus_.push(y);
                auto outLeft = y;
                auto outRight = y;
                if (chorusOn) {
                    const auto sweep = sweepFrom + (sweepTo - sweepFrom) * t;
                    const auto other = sweepOtherFrom + (sweepOtherTo - sweepOtherFrom) * t;
                    outLeft = y * dry + chorus_.read(chorusCentre + chorusSwing * sweep) * wet;
                    outRight = y * dry + chorus_.read(chorusCentre + chorusSwing * other) * wet;
                }
                left[done + i] += static_cast<float>(outLeft * (leftFrom + (leftTo - leftFrom) * t));
                right[done + i] += static_cast<float>(outRight * (rightFrom + (rightTo - rightFrom) * t));
            }
            if (!std::isfinite(dcLow_) || !std::isfinite(driveLow_)) {
                resetChain();
            }

            // Rung out: once let go of, the voice ends; held, it rests until struck again.
            const auto striking = contactAt_ < pulseLength_;
            const auto heard = amplitude_ * pre;
            silentSteps_ = !striking && !noisy && heard < 2.0e-6 ? silentSteps_ + 1 : 0;
            if (silentSteps_ >= 8) {
                if (released) {
                    stage_ = Stage::idle;
                    return false;
                }
                quiet_ = true;
                re_.fill(0.0);
                im_.fill(0.0);
                if (strings)
                    string_.clear();
                loss_ = 0.0;
                stringLevel_ = 0.0;
                resetChain();
                return true;
            }
        }
        return active();
    }

private:
    enum class Stage : std::uint8_t { idle, held, released };
    static constexpr std::size_t modeCount = 4;

    // The hammer lands: how hard (the velocity through its curve), how
    // bright its blow (harder, brighter; brighter up the keyboard), how much
    // of each mode it rings (where it lands on the beam) or where on the
    // string it lands, its thump.
    void strike(const Values& values) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        touch_ = touchOf(values, velocity_);
        hardness_ = std::clamp(v(hammerHardness) + std::clamp(v(hammerVelocity), 0.0, 1.0) * (touch_ - 0.6) * 1.2, 0.0, 1.0);
        // The level as a piano's: much quieter as it is played softly.
        level_ = std::pow(std::max(touch_, 1.0e-3), 2.0 * std::clamp(v(ampVelocity), 0.0, 1.0));
        swing_ = 0.1 + 0.9 * touch_;
        const auto hz = std::clamp(hzOf(pitch_ + tuningCents(values, pitch_, target_) / 100.0), 1.0, rate_ * 0.3);
        const auto& character = kCharacters[static_cast<std::size_t>(mechanism_)];
        // The blow: two poles (a felt pressed then let go), as bright as the
        // hammer is hard, brighter up the keyboard, never darker than the note.
        const auto bright = std::clamp(150.0 * std::pow(2.0, 5.0 * hardness_) * std::pow(2.0, (target_ - 60.0) / 24.0) * character.hammer,
                                       std::min(std::max(hz, 60.0), rate_ * 0.4), rate_ * 0.4);
        const auto samples = rate_ / (2.0 * kPi * bright);
        pulsePole_ = std::exp(-1.0 / samples);
        const auto w0 = 2.0 * kPi * hz / rate_;
        // Its response at the fundamental one: the touch alone says how far it swings.
        pulseScale_ = 1.0 - 2.0 * pulsePole_ * std::cos(w0) + pulsePole_ * pulsePole_;
        pulseOne_ = 0.0;
        pulseTwo_ = 0.0;
        contactAt_ = 0;
        const auto place = std::clamp(v(hammerPosition), 0.0, 1.0);
        if (mechanism_ == stringMechanism) {
            // Struck at its place: the part between it and the end pushed
            // (a comb: the harmonics with a node there left out).
            const auto period = rate_ / hz;
            strikeGap_ = std::max(1, static_cast<int>(std::lround((0.08 + 0.4 * place) * period)));
            excite_ = swing_;
            push_ = 0.0;
            tension_ = 1.0;
            tensionDepth_ = 0.004 * touch_ * touch_;
        } else {
            // Each mode as much as the beam moves where the hammer lands,
            // its swing smaller as it is higher (as fast for the same blow).
            strikeGap_ = -1;
            const auto hit = 0.06 + 0.5 * place;
            const auto first = cantilever(0, hit);
            input_[0] = swing_;
            input_[1] = swing_;
            input_[2] = swing_ * cantilever(1, hit) / first / character.second;
            input_[3] = swing_ * cantilever(2, hit) / first / character.third;
        }
        // The thump (a felt on metal, lower as it is softer) and the knock of
        // a hard hammer.
        const auto thump = std::clamp(v(noiseThump), 0.0, 1.0);
        thump_ += thump * 3.3 * level_ * (0.3 + 0.7 * touch_);
        knock_ += thump * 1.6 * level_ * hardness_ * hardness_;
        thumpFilter_.tune((130.0 + 260.0 * hardness_) * std::pow(2.0, (target_ - 60.0) / 48.0), 1.2, rate_);
        knockFilter_.tune(2'600.0, 1.0, rate_);
        thumpFall_ = std::exp(-1.0 / (0.008 * rate_));
        knockFall_ = std::exp(-1.0 / (0.0015 * rate_));
        clickFall_ = std::exp(-1.0 / (0.006 * rate_));
        // Until the blow has died away (and, on a string, its second edge).
        pulseLength_ = std::max(0, strikeGap_) + static_cast<int>(14.0 * samples) + 4;
    }

    // The hammer's force this sample: a blow through two poles (on a
    // string, a second one the other way where it lands' other edge is).
    double hammer() noexcept
    {
        if (contactAt_ >= pulseLength_)
            return 0.0;
        const auto blow = (contactAt_ == 0 ? 1.0 : 0.0) - (contactAt_ == strikeGap_ ? 1.0 : 0.0);
        pulseOne_ = pulsePole_ * pulseOne_ + blow;
        pulseTwo_ = pulsePole_ * pulseTwo_ + pulseOne_;
        ++contactAt_;
        return pulseTwo_ * pulseScale_;
    }

    // A one-pole low-pass (DC one) that keeps `keep` of what passes at `w`.
    static double lossFor(const double keep, const double w) noexcept
    {
        const auto g2 = std::clamp(keep, 0.0, 0.999999) * std::clamp(keep, 0.0, 0.999999);
        const auto c = std::cos(w);
        const auto b = 1.0 - g2 * c;
        const auto a = 1.0 - g2;
        const auto root = std::sqrt(std::max(0.0, b * b - a * a));
        return std::clamp((b - root) / std::max(1.0e-12, a), 0.0, 0.95);
    }

    // The modes' frequencies for this pitch: the fundamental and its twin a
    // hair apart (the tone bar's beat), the upper modes stretched with the key.
    void tuneModes(const double pitch, const double hz) noexcept
    {
        const auto& character = kCharacters[static_cast<std::size_t>(mechanism_)];
        const auto key = std::llround(target_);
        const auto twin = std::pow(2.0, (0.5 + 1.0 * std::abs(scatter(key * 7 + 3))) / 1'200.0);
        const auto spread = std::clamp(1.0 + 0.04 * (60.0 - pitch) / 12.0, 0.85, 1.15) * (1.0 + 0.01 * scatter(key * 13 + 5));
        const std::array<double, modeCount> ratios { 1.0, twin, character.second * spread, character.third * spread };
        for (std::size_t k = 0; k < modeCount; ++k) {
            const auto modeHz = hz * ratios[k];
            if (modeHz >= rate_ * 0.45) {
                cos_[k] = 1.0;
                sin_[k] = 0.0;
                re_[k] = 0.0;
                im_[k] = 0.0;
                continue;
            }
            const auto w = 2.0 * kPi * modeHz / rate_;
            cos_[k] = std::cos(w);
            sin_[k] = std::sin(w);
        }
    }

    void resetChain() noexcept
    {
        dcLow_ = 0.0;
        driveLow_ = 0.0;
        coil_.reset();
        colour_.reset();
        bass_.reset();
        body_.reset();
        treble_.reset();
        thumpFilter_.reset();
        knockFilter_.reset();
        clickFilter_.reset();
        thump_ = knock_ = click_ = 0.0;
    }

    // The pace a glide from here to the note takes to last its time.
    void aimGlide(const Values& values) noexcept
    {
        const auto seconds = static_cast<double>(values[voicingGlide]);
        glidePerSample_ = seconds > 0.0 ? std::abs(target_ - pitch_) / (seconds * rate_) : 0.0;
    }

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double glidePerSample_ {};
    int mechanism_ {};
    Stage stage_ { Stage::idle };
    bool quiet_ {};
    bool fresh_ { true };
    int silentSteps_ {};
    // The blow.
    double touch_ { 1.0 };
    double hardness_ {};
    double level_ { 1.0 };
    double swing_ { 1.0 };
    int pulseLength_ {};
    int contactAt_ {};
    int strikeGap_ { -1 };
    double pulsePole_ {};
    double pulseScale_ { 1.0 };
    double pulseOne_ {};
    double pulseTwo_ {};
    // The modes: their complex states, turns, inputs and outputs.
    std::array<double, modeCount> re_ {};
    std::array<double, modeCount> im_ {};
    std::array<double, modeCount> cos_ {};
    std::array<double, modeCount> sin_ {};
    std::array<double, modeCount> radius_ {};
    std::array<double, modeCount> input_ {};
    std::array<double, modeCount> output_ {};
    double tunedPitch_ {};
    double tunedRate_ {};
    // The string.
    Line string_;
    double loss_ {};
    double push_ {};
    double excite_ {};
    double tension_ {};
    double tensionDepth_ {};
    double stringLevel_ {};
    double amplitude_ {};
    // The pickup.
    double reach_ { 1.0 };
    double offset_ {};
    double lastFlux_ { 1.0 };
    // The noises.
    BandPass thumpFilter_;
    BandPass knockFilter_;
    BandPass clickFilter_;
    double thump_ {};
    double knock_ {};
    double click_ {};
    double thumpFall_ {};
    double knockFall_ {};
    double clickFall_ {};
    double releaseAmount_ {};
    // The preamp and the tone.
    double dcLow_ {};
    double driveLow_ {};
    Biquad coil_;
    Biquad colour_;
    Biquad bass_;
    Biquad body_;
    Biquad treble_;
    // The tremolo, the chorus, the output.
    long long tremoloCycle_ {};
    double tremoloPhase_ {};
    double tremoloSmooth_ {};
    double chorusPhase_ {};
    double chorusSweep_ {};
    double chorusSweepOther_ { 1.0 };
    Line chorus_;
    double previousGain_ {};
    double previousLeft_ {};
    double previousRight_ {};
    // Each voice its own draw: their noises apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeJade()
{
    return std::make_unique<JadeEngine>();
}

std::vector<float> jadePicture(const Values& values, const std::string_view part, const int points)
{
    // "pickup": two periods of the note held at a firm touch, after its bell,
    // as the pickup turns it into a voltage; "curve": the pickup's curve, the
    // motion from one side to the other.
    const auto wave = part == "pickup";
    if ((!wave && part != "curve") || points < 2)
        return {};
    const auto mechanism = mechanismOf(values);
    const auto reach = reachOf(values, mechanism, 60.0);
    const auto offset = offsetOf(values, mechanism);
    const auto curve = [&](const double x) {
        const auto u = reach * x + offset;
        return mechanism == reedMechanism ? charge(u) - charge(offset) : flux(u) - flux(offset);
    };
    std::vector<double> drawn(static_cast<std::size_t>(points), 0.0);
    if (!wave) {
        for (int at = 0; at < points; ++at)
            drawn[static_cast<std::size_t>(at)] = curve(-1.0 + 2.0 * static_cast<double>(at) / static_cast<double>(points - 1));
    } else {
        const auto swing = 0.1 + 0.9 * touchOf(values, 0.8);
        // The string's harmonics as its strike and its pickup comb them and
        // its losses leave them; the tine's or reed's fundamental alone.
        const auto harmonics = std::clamp(static_cast<double>(values[static_cast<std::size_t>(tineHarmonics)]), 0.0, 1.0);
        const auto struck = 0.08 + 0.4 * std::clamp(static_cast<double>(values[static_cast<std::size_t>(hammerPosition)]), 0.0, 1.0);
        const auto heard = 0.04 + 0.42 * std::clamp(static_cast<double>(values[static_cast<std::size_t>(pickupPosition)]), 0.0, 1.0);
        const auto bright = 3.0 + 20.0 * harmonics;
        const auto motionAt = [&](const double phase) {
            if (mechanism != stringMechanism)
                return swing * std::sin(2.0 * parts::kPi * phase);
            auto sum = 0.0;
            for (int n = 1; n <= 24; ++n) {
                const auto h = static_cast<double>(n);
                const auto over = h / bright;
                sum += std::sin(parts::kPi * h * struck) * std::sin(parts::kPi * h * heard) / h / (1.0 + over * over) * std::sin(2.0 * parts::kPi * h * phase);
            }
            return swing * sum * 1.6;
        };
        const auto dt = 2.0 / static_cast<double>(points - 1);
        for (int at = 0; at < points; ++at) {
            const auto phase = dt * static_cast<double>(at);
            if (mechanism == reedMechanism)
                drawn[static_cast<std::size_t>(at)] = curve(motionAt(phase));
            else
                drawn[static_cast<std::size_t>(at)] = curve(motionAt(phase + 0.5 * dt)) - curve(motionAt(phase - 0.5 * dt));
        }
    }
    auto peak = 1.0e-9;
    for (const auto sample : drawn)
        peak = std::max(peak, std::abs(sample));
    std::vector<float> result(drawn.size());
    for (std::size_t at = 0; at < result.size(); ++at)
        result[at] = static_cast<float>(drawn[at] * 0.95 / peak);
    return result;
}

} // namespace cydaw::engine::synth::detail
