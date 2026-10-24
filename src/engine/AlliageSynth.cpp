// Alliage: a synth of the synth family made of two modules, each picked from
// a list. One oscillator of eight kinds (a swarm of seven detuned saws, a
// wavetable from the family's tables, a pulse whose width moves by itself, a
// pair of sines in frequency modulation, a swarm of detuned squares, a saw
// hard-synced to the note, organ-like harmonics, a band of noise tuned to the
// note), each read through the same two knobs whose meaning follows the kind
// (timbre, shape), with a spread across the stereo, a sub an octave under the
// oscillator and a noise; then one filter of seven kinds (a ladder, a low-, band- or
// high-pass, a comb, a vowel, a scream), driven, following the keys and its
// envelope. An envelope for the volume, one for the filter (and, as much as
// asked, for the timbre), a pitch envelope; one LFO to the pitch, the timbre,
// the cutoff and the volume. Quick to make very different sounds.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"
#include "SynthTables.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Alliage's settings after the shared voicing, in its table's order.
enum Alliage : int {
    oscType = voicingFieldCount,
    oscTable,
    oscMode,
    oscRatio,
    oscOctave,
    oscSemi,
    oscFine,
    oscTimbre,
    oscShape,
    oscEnv,
    oscSpread,
    oscLevel,
    subLevel,
    subWave,
    noiseLevel,
    noiseColour,
    fType,
    fCutoff,
    fResonance,
    fDrive,
    fEnv,
    fKey,
    fVelocity,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    fenvAttack,
    fenvDecay,
    fenvSustain,
    fenvRelease,
    pitchEnv,
    pitchDecay,
    lfoShape,
    lfoRate,
    lfoSync,
    lfoDelay,
    lfoPitch,
    lfoTimbre,
    lfoCutoff,
    lfoAmp,
    modWheel,
    alliageCount
};
static_assert(alliageCount == 49);

// The oscillator module's kinds.
enum OscKind : int { swarmOsc, wavetableOsc, pulseOsc, fmOsc, squaresOsc, syncOsc, additiveOsc, noiseOsc, oscKindCount };

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kKinds { { "Essaim", "Swarm" },         { "Table d'ondes", "Wavetable" }, { "Impulsion", "Pulse" },
                       { "FM sinus", "Sine FM" },     { "Super carré", "Supersquare" }, { "Synchro", "Sync" },
                       { "Additif", "Additive" },     { "Bande de bruit", "Noise Band" } };
const Choices kTables { { "Classique", "Classic" }, { "Harmonique", "Harmonic" }, { "Voix", "Voices" }, { "Métal", "Metal" },
                        { "Plié", "Folded" },        { "Numérique", "Digital" },  { "Dents", "Teeth" }, { "Synchro", "Sync" } };
const Choices kModes { { "Direct", "Plain" }, { "Courbe", "Bend" },   { "Miroir", "Mirror" },        { "Formant", "Formant" },
                       { "Synchro", "Sync" }, { "Paliers", "Steps" }, { "Asymétrie", "Asymmetry" }, { "Pli", "Fold" } };
// The modulator's frequency against the carrier's (the inharmonic ones for bells and metal).
const Choices kRatios { { "1/2", "1/2" }, { "1", "1" },     { "1,41", "1.41" }, { "1,5", "1.5" }, { "2", "2" },
                        { "2,76", "2.76" }, { "3", "3" },   { "3,5", "3.5" },   { "4", "4" },     { "5", "5" },
                        { "7", "7" },     { "9", "9" },     { "11", "11" },     { "14", "14" } };
constexpr std::array<double, 14> kRatioValues { 0.5, 1.0, 1.41421356, 1.5, 2.0, 2.76, 3.0, 3.5, 4.0, 5.0, 7.0, 9.0, 11.0, 14.0 };
const Choices kSubWaves { { "Sinus", "Sine" }, { "Triangle", "Triangle" }, { "Carré", "Square" } };
const Choices kFilters { { "Ladder", "Ladder" },        { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" }, { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kLfoShapes { { "Sinus", "Sine" },  { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };

// The control step: envelopes, LFO, glide, pitches and coefficients once in so many samples.
constexpr int stepLength = 16;

// A swarm's seven voices: where each sits in its detune (-1..1, the middle
// one on the note) and across the stereo (not in the same order: wide and
// even on both sides).
constexpr int swarmCount = 7;
constexpr std::array<double, swarmCount> kSwarmDetune { -1.0, -0.5716, -0.1775, 0.0, 0.1810, 0.5650, 0.9767 };
constexpr std::array<double, swarmCount> kSwarmPan { -1.0, 0.66, -0.33, 0.0, 0.33, -0.66, 1.0 };

// The additive kind's harmonics, and the organ's registration among them
// (the footages a church or a tonewheel organ draws: 8', 4', 2 2/3', 2',
// 1 3/5', 1 1/3', 1').
constexpr int harmonicCount = 16;
constexpr std::array<double, harmonicCount> kOrgan { 1.0, 1.0, 0.8, 0.8, 0.5, 0.6, 0.0, 0.6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

// The noise band's bands: the note, its octave and its twelfth.
constexpr int bandCount = 3;

// How the swarm's voices are mixed for a timbre (their detune) and a shape.
struct SwarmMix {
    double cents {};
    double centre { 1.0 };
    double side {};
    double norm { 1.0 };
};

SwarmMix swarmMix(const int kind, const double timbre, const double shape) noexcept
{
    SwarmMix mix;
    // Never quite still: a swarm with no detune would be its phases' comb.
    mix.cents = 2.0 + 88.0 * std::pow(std::clamp(timbre, 0.0, 1.0), 1.6);
    if (kind == squaresOsc) {
        mix.centre = 1.0;
        mix.side = 0.8;
        // Squares are louder than saws.
        mix.norm = 0.6 / std::sqrt(1.0 + 6.0 * 0.64);
    } else {
        // The middle saw alone at no shape, the six around it rising as it turns.
        mix.centre = 1.0 - 0.55 * shape;
        mix.side = 1.28 * shape - 0.74 * shape * shape;
        mix.norm = 1.0 / std::sqrt(std::max(1.0e-6, mix.centre * mix.centre + 6.0 * mix.side * mix.side));
    }
    return mix;
}

// The additive kind's harmonics for a timbre (their tilt) and a shape (all of
// them, the odd ones, the organ's registration), as loud as a saw whatever
// they are; those near the top (an increment of the note) faded out. Returns
// how many are heard.
int harmonicsOf(const double timbre, const double shape, const double increment, std::array<double, harmonicCount>& amplitudes) noexcept
{
    const auto slope = 2.6 - 2.3 * std::clamp(timbre, 0.0, 1.0);
    auto power = 0.0;
    auto top = 0;
    for (int index = 0; index < harmonicCount; ++index) {
        const auto at = static_cast<std::size_t>(index);
        const auto n = static_cast<double>(index + 1);
        const auto odd = index % 2 == 0 ? 1.0 : 0.0;
        const auto weight = shape <= 0.5 ? 1.0 + (odd - 1.0) * shape * 2.0 : odd + (kOrgan[at] - odd) * (shape - 0.5) * 2.0;
        const auto amplitude = weight * std::pow(n, -slope);
        power += amplitude * amplitude;
        const auto room = std::clamp((0.45 - n * increment) / 0.05, 0.0, 1.0);
        amplitudes[at] = amplitude * room;
        if (amplitudes[at] > 0.0)
            top = index + 1;
    }
    const auto scale = 0.577 / std::sqrt(std::max(1.0e-9, 0.5 * power));
    for (auto& amplitude : amplitudes)
        amplitude *= scale;
    return top;
}

// The noise band's sharpness for a timbre: narrow (a whistle) to wide (a breath).
double bandDampingOf(const double timbre) noexcept
{
    const auto open = 1.0 - std::clamp(timbre, 0.0, 1.0);
    return 1.0 / (0.7 + 80.0 * open * open);
}

// The noise band's bands' levels for a shape: the note's, then its octave and twelfth rising.
std::array<double, bandCount> bandLevelsOf(const double shape) noexcept
{
    const auto second = 0.7 * shape;
    const auto third = 0.5 * shape;
    const auto norm = 1.0 / std::sqrt(1.0 + second * second + third * third);
    return { norm, second * norm, third * norm };
}

// A two-pole band-pass (zero-delay, trapezoidal) for the noise band.
struct Band {
    double a1 {};
    double a2 {};
    double a3 {};
    double gain {};
};

struct BandState {
    double ic1 {};
    double ic2 {};
};

// One band's coefficients at `hz`, its gain making white noise through it as
// loud as a saw whatever its width; silent past the top.
Band bandAt(const double hz, const double damping, const double level, const double rate) noexcept
{
    Band band;
    if (hz >= rate * 0.45 || level <= 0.0)
        return band;
    const auto at = std::max(10.0, hz);
    const auto g = std::tan(kPi * at / rate);
    band.a1 = 1.0 / (1.0 + g * (g + damping));
    band.a2 = g * band.a1;
    band.a3 = g * band.a2;
    band.gain = level * std::min(200.0, std::sqrt(damping * rate / (kPi * at)));
    return band;
}

double bandStep(const Band& band, BandState& state, const double x) noexcept
{
    const auto v3 = x - state.ic2;
    const auto v1 = band.a1 * state.ic1 + band.a2 * v3;
    const auto v2 = state.ic2 + band.a2 * state.ic1 + band.a3 * v3;
    state.ic1 = 2.0 * v1 - state.ic1;
    state.ic2 = 2.0 * v2 - state.ic2;
    return v1 * band.gain;
}

void advance(double& phase, const double increment) noexcept
{
    phase += increment;
    if (phase >= 1.0)
        phase -= 1.0;
}

// The sync kind's window: nothing at the restarts, whole halfway.
double windowGain(const double window, const double master) noexcept
{
    return 1.0 - window + window * 4.0 * master * (1.0 - master);
}

} // namespace

std::vector<Section> alliageSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },         { "OSCILLATEUR", "OSCILLATOR", 0xffe8c25au },
             { "MÉLANGE", "MIXER", 0xffd0d6e0u },       { "FILTRE", "FILTER", 0xffff6b6bu },
             { "ENVELOPPES", "ENVELOPES", 0xff7ad0ffu }, { "LFO", "LFO", 0xffb98cffu } };
}

std::vector<Parameter> alliageParameters()
{
    Table table;
    addVoicing(table, 12, 0.7f);
    table.section = 1;
    table.choice("osc.type", "Oscillateur · type", "Oscillator · kind", kKinds, swarmOsc);
    table.choice("osc.table", "Oscillateur · table", "Oscillator · table", kTables, 0);
    table.choice("osc.mode", "Oscillateur · déformation", "Oscillator · warp mode", kModes, 1);
    table.choice("osc.ratio", "Oscillateur · rapport FM", "Oscillator · FM ratio", kRatios, 1);
    table.whole("osc.octave", "Oscillateur · octave", "Oscillator · octave", {}, -3, 3, 0);
    table.whole("osc.semi", "Oscillateur · demi-tons", "Oscillator · semitones", "st", -12, 12, 0);
    table.add("osc.fine", "Oscillateur · fin", "Oscillator · fine", "ct", -50.0f, 50.0f, 0.0f);
    // Read as the kind reads it: detune, position, width, index, sync, tilt, bandwidth.
    table.add("osc.timbre", "Oscillateur · timbre", "Oscillator · timbre", {}, 0.0f, 1.0f, 0.4f);
    // Mix, warp, PWM, feedback, width, window, registration, overtones.
    table.add("osc.shape", "Oscillateur · forme", "Oscillator · shape", {}, 0.0f, 1.0f, 0.5f);
    table.add("osc.env", "Enveloppe filtre → timbre", "Filter envelope → timbre", {}, -1.0f, 1.0f, 0.0f);
    table.add("osc.spread", "Oscillateur · largeur", "Oscillator · spread", {}, 0.0f, 1.0f, 0.6f);
    table.add("osc.level", "Oscillateur · niveau", "Oscillator · level", {}, 0.0f, 1.0f, 0.8f);
    table.section = 2;
    table.add("sub.level", "Sub · niveau", "Sub · level", {}, 0.0f, 1.0f, 0.0f);
    table.choice("sub.wave", "Sub · forme", "Sub · wave", kSubWaves, 0);
    table.add("noise.level", "Bruit · niveau", "Noise · level", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.colour", "Bruit · couleur", "Noise · colour", {}, 0.0f, 1.0f, 0.5f);
    table.section = 3;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilters, MultiFilter::ladder);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 2'200.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.2f);
    table.add("f.drive", "Filtre · saturation", "Filter · drive", {}, 0.0f, 1.0f, 0.15f);
    table.add("f.env", "Filtre · enveloppe", "Filter · envelope", {}, -1.0f, 1.0f, 0.3f);
    table.add("f.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.5f);
    table.add("f.velocity", "Filtre · vélocité", "Filter · velocity", {}, 0.0f, 1.0f, 0.3f);
    table.section = 4;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.8f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.35f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    table.add("fenv.attack", "Filtre · attaque", "Filter · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Filtre · déclin", "Filter · decay", "s", 0.005f, 10.0f, 0.5f, Kind::continuous, true);
    table.add("fenv.sustain", "Filtre · maintien", "Filter · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.release", "Filtre · relâchement", "Filter · release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous, true);
    table.add("pitch.env", "Hauteur · enveloppe", "Pitch · envelope", "st", -24.0f, 24.0f, 0.0f);
    table.add("pitch.decay", "Hauteur · déclin", "Pitch · decay", "s", 0.005f, 5.0f, 0.08f, Kind::continuous, true);
    table.section = 5;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 20.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO · retard", "LFO · delay", "s", 0.0f, 3.0f, 0.0f);
    table.add("lfo1.pitch", "LFO → hauteur", "LFO → pitch", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.timbre", "LFO → timbre", "LFO → timbre", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.cutoff", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.amp", "LFO → volume", "LFO → volume", {}, 0.0f, 1.0f, 0.0f);
    table.add("mod.wheel", "Molette → vibrato", "Wheel → vibrato", {}, 0.0f, 1.0f, 0.5f);
    return table.parameters;
}

namespace {

// One voice of the oscillator module: a swarm's seven, a pair's two (the
// other kinds), and whatever the kind keeps.
struct Sub {
    double phase {};      // the saw, the table, the pulse, the carrier, the synced saw, the harmonics
    double phase2 {};     // the modulator, the sync's master, the pulse width's own LFO
    double increment {};
    double increment2 {}; // the modulator's, the synced saw's
    double previous {};   // the modulator's last two outputs (its feedback)
    double before {};
    double held {};       // the synced saw's sample awaiting its correction
    double width { 0.5 };
    double gain { 1.0 };
    double left { 0.7071 };
    double right { 0.7071 };
    int level {};         // the table's level for its pitch
};

// The pair of sines: the modulator fed back on itself (two samples averaged:
// no hunting), then moving the carrier's phase.
double fmSample(Sub& sub, const double index, const double feedback) noexcept
{
    const auto modulator = std::sin(2.0 * kPi * sub.phase2 + feedback * 0.5 * (sub.previous + sub.before));
    sub.before = sub.previous;
    sub.previous = modulator;
    const auto x = std::sin(2.0 * kPi * sub.phase + index * modulator);
    advance(sub.phase, sub.increment);
    advance(sub.phase2, sub.increment2);
    return x;
}

// A saw restarted at each turn of its master (the note), its jumps smoothed
// by a polyBLEP a sample late; the window fades it to nothing at the restarts.
double syncedSample(Sub& sub, const double window) noexcept
{
    auto fixHeld = 0.0;
    auto fixNow = 0.0;
    // A jump of `height`, `since` samples before this one (0..1).
    const auto jump = [&fixHeld, &fixNow](const double height, const double since) {
        fixHeld += 0.5 * height * since * since;
        fixNow -= 0.5 * height * (1.0 - since) * (1.0 - since);
    };
    const auto slaveIncrement = std::max(1.0e-9, sub.increment2);
    auto master = sub.phase2 + sub.increment;
    auto slave = sub.phase + slaveIncrement;
    if (master >= 1.0) {
        master -= 1.0;
        const auto since = std::clamp(master / std::max(1.0e-9, sub.increment), 0.0, 1.0);
        auto reached = sub.phase + (1.0 - since) * slaveIncrement;
        if (reached >= 1.0) {
            // Its own turn came first.
            const auto ownSince = std::clamp(1.0 - (1.0 - sub.phase) / slaveIncrement, 0.0, 1.0);
            jump(-2.0 * windowGain(window, std::min(1.0, sub.phase2 + (1.0 - ownSince) * sub.increment)), ownSince);
            reached -= 1.0;
        }
        // The restart: from where it was to the bottom (the window is nothing there).
        jump((1.0 - window) * (-1.0 - (2.0 * reached - 1.0)), since);
        slave = since * slaveIncrement;
    } else if (slave >= 1.0) {
        slave -= 1.0;
        jump(-2.0 * windowGain(window, master), std::clamp(slave / slaveIncrement, 0.0, 1.0));
    }
    sub.phase = slave;
    sub.phase2 = master;
    const auto out = sub.held + fixHeld;
    sub.held = (2.0 * slave - 1.0) * windowGain(window, master) + fixNow;
    return out;
}

// Harmonics locked to the note: each sine from the two below it.
double additiveSample(Sub& sub, const std::array<double, harmonicCount>& amplitudes, const int top) noexcept
{
    const auto angle = 2.0 * kPi * sub.phase;
    const auto twice = 2.0 * std::cos(angle);
    auto lower = 0.0;
    auto current = std::sin(angle);
    auto x = 0.0;
    for (int n = 0; n < top; ++n) {
        x += amplitudes[static_cast<std::size_t>(n)] * current;
        const auto upper = twice * current - lower;
        lower = current;
        current = upper;
    }
    advance(sub.phase, sub.increment);
    return x;
}

class AlliageEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& side : filters_)
            side.prepare(rate_);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amp_, &filterEnvelope_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& side : filters_)
            side.clear();
        bands_ = {};
        noiseLow_ = {};
        dcIn_ = {};
        dcOut_ = {};
        elapsed_ = 0.0;
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
        pitchLevel_ = 1.0;
        subPhase_ = 0.0;
        const auto kind = std::clamp(static_cast<int>(std::lround(values[oscType])), 0, oscKindCount - 1);
        for (auto& sub : subs_) {
            // Free-running shapes start anywhere; a modulated or a synced
            // pair from the top (the same attack each note).
            sub = Sub {};
            sub.phase = kind == fmOsc || kind == syncOsc ? 0.0 : 0.5 * (random() + 1.0);
            // The pulse's width moves from anywhere: a chord's notes apart.
            sub.phase2 = kind == pulseOsc ? 0.5 * (random() + 1.0) : 0.0;
        }
        // A pair begins as one, and opens as it detunes.
        if (kind != swarmOsc && kind != squaresOsc)
            subs_[1] = subs_[0];
        paired_ = false;
        previousLevel_ = 0.0;
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (amp_.stage == Envelope::Stage::release) {
            amp_.start();
            filterEnvelope_.start();
        }
    }

    void release() noexcept override
    {
        amp_.release();
        filterEnvelope_.release();
    }

    void kill() noexcept override
    {
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
        filterEnvelope_.stage = Envelope::Stage::idle;
    }

    [[nodiscard]] bool active() const noexcept override { return amp_.stage != Envelope::Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept override { return amp_.stage == Envelope::Stage::release; }
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
        // What holds for the whole block.
        const auto kind = choice(oscType, oscKindCount);
        module_.set = choice(oscTable, tables::sets);
        module_.mode = choice(oscMode, tables::modes);
        const auto ratio = kRatioValues[static_cast<std::size_t>(choice(oscRatio, static_cast<int>(kRatioValues.size())))];
        const auto octave = 12.0 * std::round(v(oscOctave));
        const auto offset = octave + std::round(v(oscSemi)) + v(oscFine) / 100.0;
        const auto shape = std::clamp(v(oscShape), 0.0, 1.0);
        const auto spread = std::clamp(v(oscSpread), 0.0, 1.0);
        const auto oscGain = v(oscLevel);
        const auto subGain = v(subLevel) * 0.8 * 0.7071;
        const auto subKind = choice(subWave, 3);
        const auto noiseGain = v(noiseLevel) * 0.7;
        // The noise's colour: darkened below the middle, thinned above it.
        const auto colour = std::clamp(v(noiseColour), 0.0, 1.0);
        const auto noiseCut = colour < 0.5 ? 200.0 * std::pow(2.0, colour * 14.0) : 1'500.0;
        const auto noiseCoefficient = 1.0 - std::exp(-2.0 * kPi * std::min(noiseCut, rate_ * 0.45) / rate_);
        const auto noiseHigh = colour > 0.5 ? 2.0 * (colour - 0.5) : 0.0;
        const auto noiseMakeUp = colour < 0.5 ? std::min(4.0, std::sqrt((2.0 - noiseCoefficient) / std::max(1.0e-3, noiseCoefficient)))
                                              : 1.0 + 0.5 * noiseHigh;
        const auto filterKind = choice(fType, MultiFilter::kindCount);
        const auto resonance = std::clamp(v(fResonance), 0.0, 1.0);
        const auto drive = 1.0 + 4.0 * v(fDrive);
        const auto driveBack = 1.0 / (1.0 + 1.5 * v(fDrive));
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto velocityFilter = 1.0 - v(fVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        // Coefficients for a step of `stepLength` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(stepLength) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto filterAttackStep = coefficient(v(fenvAttack), 1.0 / 1.466);
        const auto filterDecayStep = coefficient(v(fenvDecay), 1.0 / 4.6);
        const auto filterReleaseStep = coefficient(v(fenvRelease), 1.0 / 4.6);
        const auto pitchStep = coefficient(v(pitchDecay), 1.0 / 4.6);
        const auto pitchAmount = v(pitchEnv);
        const auto lfoHz = [&]() {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto lfoKind = choice(lfoShape, 5);
        const auto lfoDelaySeconds = v(lfoDelay);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        // Its depth on a square law (fine vibratos, wide sirens), the wheel adding up to half a semitone.
        const auto vibratoDepth = 12.0 * v(lfoPitch) * v(lfoPitch) + static_cast<double>(context.modWheel) * v(modWheel) * 0.5;
        const auto dcCoefficient = std::exp(-2.0 * kPi * 6.0 / rate_);
        // The pair (all kinds but the swarms and the noise): the second voice
        // fades in over the first twelfth of the spread.
        const auto pairKind = kind != swarmOsc && kind != squaresOsc && kind != noiseOsc;
        const auto pairWeight = pairKind ? std::min(1.0, spread * 12.0) * 0.5 : 0.0;
        const auto pairGain = 1.0 / std::sqrt((1.0 - pairWeight) * (1.0 - pairWeight) + pairWeight * pairWeight);
        const auto pairCount = pairWeight > 0.0 ? 2 : 1;

        for (int done = 0; done < count; done += stepLength) {
            const auto length = std::min(stepLength, count - done);
            const auto part = static_cast<double>(length) / static_cast<double>(stepLength);
            // The slow things, once a step: envelopes, the LFO, the glide, the pitches and the filter.
            const auto ampLevel = amp_.step(ampAttackStep, ampDecayStep, v(ampSustain), ampReleaseStep, part);
            const auto filterLevel = filterEnvelope_.step(filterAttackStep, filterDecayStep, v(fenvSustain), filterReleaseStep, part);
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            lfoPhase_ += lfoHz * seconds;
            if (lfoPhase_ >= 1.0) {
                lfoPhase_ -= std::floor(lfoPhase_);
                lfoHeld_ = random();
            }
            const auto fade = lfoDelaySeconds > 0.0 ? std::min(1.0, elapsed_ / lfoDelaySeconds) : 1.0;
            const auto lfo = lfoWave(lfoKind, lfoPhase_, lfoHeld_) * fade;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto pitchShift = pitchAmount * pitchLevel_;
            pitchLevel_ *= pitchStep;
            const auto basePitch = pitch_ + expressionPitch + bend + lfo * vibratoDepth + pitchShift;
            const auto increment = 440.0 * std::pow(2.0, (basePitch + offset - 69.0) / 12.0) / rate_;
            // The sub: an octave under the oscillator's octave (never on its fundamental).
            subIncrement_ = std::min(0.45, 440.0 * std::pow(2.0, (basePitch + octave - 12.0 - 69.0) / 12.0) / rate_);
            // The timbre: its knob, the filter envelope, the LFO.
            module_.timbre = std::clamp(v(oscTimbre) + v(oscEnv) * filterLevel + lfo * v(lfoTimbre) * 0.5, 0.0, 1.0);
            module_.shape = shape;
            prepareOscillator(kind, increment, spread, ratio, pairCount, pairWeight, pairGain, seconds);
            // The cutoff: its knob, the envelope (6 octaves, as the velocity allows), the LFO (3), the key.
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(fCutoff))) + v(fEnv) * filterLevel * 72.0 * velocityFilter
                                   + lfo * v(lfoCutoff) * 36.0 + v(fKey) * (pitch_ - 60.0);
            const auto cutoff = std::clamp(std::pow(2.0, semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(filterKind, cutoff, resonance, rate_, filters_[0].combSize());
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(0, ampLevel);
            showEnvelope(1, filterLevel);
            show(oscTimbre, module_.timbre, v(oscTimbre));
            show(fCutoff, cutoff, v(fCutoff));
            const auto tremolo = 1.0 - v(lfoAmp) * 0.5 * (1.0 - lfo);
            const auto level = ampLevel * velocityLevel * master * tremolo * expressionGain * 0.6;
            // The level eased across the step: no steps heard on a quick attack.
            const auto from = previousLevel_;
            previousLevel_ = level;
            // The oscillator module's samples for the step.
            std::array<double, stepLength> oscLeft {};
            std::array<double, stepLength> oscRight {};
            switch (kind) {
            case swarmOsc:
            case squaresOsc: swarmInto(kind, length, oscLeft, oscRight); break;
            case noiseOsc: bandInto(length, oscLeft, oscRight); break;
            default: pairInto(kind, pairCount, length, oscLeft, oscRight); break;
            }
            for (int i = 0; i < length; ++i) {
                const auto at = static_cast<std::size_t>(i);
                const auto gain = from + (level - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                std::array<double, 2> channel { oscLeft[at] * oscGain, oscRight[at] * oscGain };
                if (subGain > 0.0) {
                    const auto low = subSample(subKind) * subGain;
                    channel[0] += low;
                    channel[1] += low;
                }
                advance(subPhase_, subIncrement_);
                if (noiseGain > 0.0)
                    for (std::size_t side = 0; side < 2; ++side) {
                        const auto white = random();
                        auto& low = noiseLow_[side];
                        low += (white - low) * noiseCoefficient;
                        channel[side] += (colour < 0.5 ? low : white - noiseHigh * low) * noiseMakeUp * noiseGain;
                    }
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = std::tanh(channel[side] * drive) * driveBack;
                    x = filters_[side].process(filterKind, coefficients, x);
                    // No offset left by a synced saw or a driven filter.
                    const auto blocked = x - dcIn_[side] + dcCoefficient * dcOut_[side];
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    channel[side] = blocked * gain;
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
            }
        }
        return active();
    }

private:
    // What the oscillator module plays with for a step.
    struct Module {
        int set {};
        int mode {};
        double timbre {};
        double shape {};
        double norm { 1.0 };
        double width { 0.5 };
        double index {};
        double feedback {};
        double window {};
        double stereo {};
        int top {};
        std::array<double, harmonicCount> harmonics {};
        std::array<Band, bandCount> bands {};
    };

    // The oscillator module's voices for a step: their pitches, places and
    // gains, and what their kind needs.
    void prepareOscillator(const int kind, const double increment, const double spread, const double ratio, const int pairCount,
                           const double pairWeight, const double pairGain, const double seconds) noexcept
    {
        const auto timbre = module_.timbre;
        const auto shape = module_.shape;
        const auto place = [](Sub& sub, const double pan) {
            sub.left = std::cos((pan + 1.0) * kPi * 0.25);
            sub.right = std::sin((pan + 1.0) * kPi * 0.25);
        };
        switch (kind) {
        case swarmOsc:
        case squaresOsc: {
            const auto mix = swarmMix(kind, timbre, shape);
            module_.norm = mix.norm;
            module_.width = kind == squaresOsc ? 0.5 - 0.42 * shape : 0.5;
            for (std::size_t at = 0; at < subs_.size(); ++at) {
                auto& sub = subs_[at];
                sub.increment = std::min(0.45, increment * std::pow(2.0, kSwarmDetune[at] * mix.cents / 1'200.0));
                sub.gain = at == 3 ? mix.centre : mix.side;
                place(sub, kSwarmPan[at] * spread);
            }
            paired_ = false;
            break;
        }
        case noiseOsc: {
            const auto damping = bandDampingOf(timbre);
            const auto levels = bandLevelsOf(shape);
            const auto hz = increment * rate_;
            for (std::size_t band = 0; band < static_cast<std::size_t>(bandCount); ++band)
                module_.bands[band] = bandAt(hz * static_cast<double>(band + 1), damping, levels[band], rate_);
            module_.stereo = spread;
            paired_ = false;
            break;
        }
        default: {
            // The second voice opening: it starts as the first.
            if (pairCount == 2 && !paired_)
                subs_[1] = subs_[0];
            paired_ = pairCount == 2;
            auto highest = 0.0;
            for (int at = 0; at < pairCount; ++at) {
                auto& sub = subs_[static_cast<std::size_t>(at)];
                const auto side = pairCount == 1 ? 0.0 : at == 0 ? -1.0 : 1.0;
                // Up to twelve cents each way, as wide.
                sub.increment = std::min(0.45, increment * std::pow(2.0, side * spread * 12.0 / 1'200.0));
                highest = std::max(highest, sub.increment);
                sub.gain = (pairCount == 1 ? 1.0 : at == 0 ? 1.0 - pairWeight : pairWeight) * pairGain;
                place(sub, side * spread);
                switch (kind) {
                case wavetableOsc: sub.level = tables::levelFor(sub.increment, tables::warpStretch(module_.mode, shape)); break;
                case pulseOsc:
                    // Its width: the timbre, and its own slow triangle as deep as the shape.
                    sub.phase2 += 0.75 * seconds;
                    sub.phase2 -= std::floor(sub.phase2);
                    sub.width = std::clamp(0.5 - 0.44 * timbre + 0.4 * shape * lfoWave(1, sub.phase2, 0.0), 0.04, 0.96);
                    break;
                case fmOsc: sub.increment2 = std::min(0.45, sub.increment * ratio); break;
                case syncOsc: sub.increment2 = std::min(0.45, sub.increment * std::pow(2.0, 3.0 * timbre)); break;
                default: break;
                }
            }
            if (kind == fmOsc) {
                // Its index (radians), kept so that its sidebands stay under the top.
                const auto room = std::max(0.0, (0.45 / std::max(1.0e-9, highest) - 1.0) / ratio - 1.0);
                module_.index = std::min(10.0 * timbre * timbre, room);
                module_.feedback = 1.5 * shape;
            }
            if (kind == syncOsc)
                module_.window = shape;
            if (kind == additiveOsc)
                module_.top = harmonicsOf(timbre, shape, highest, module_.harmonics);
            break;
        }
        }
    }

    // One sample of a pair's voice, of its kind.
    double next(const int kind, Sub& sub) noexcept
    {
        switch (kind) {
        case wavetableOsc: {
            const auto x = tables::sample(module_.set, module_.timbre, module_.mode, module_.shape, sub.level, sub.phase);
            advance(sub.phase, sub.increment);
            return x;
        }
        case pulseOsc: {
            const auto x = oscillator(1, sub.phase, sub.increment, sub.width) * 0.65;
            advance(sub.phase, sub.increment);
            return x;
        }
        case fmOsc: return fmSample(sub, module_.index, module_.feedback) * 0.85;
        case syncOsc: return syncedSample(sub, module_.window);
        case additiveOsc: return additiveSample(sub, module_.harmonics, module_.top);
        default: return 0.0;
        }
    }

    void swarmInto(const int kind, const int length, std::array<double, stepLength>& outLeft, std::array<double, stepLength>& outRight) noexcept
    {
        const auto wave = kind == squaresOsc ? 1 : 0;
        for (int i = 0; i < length; ++i) {
            auto sumLeft = 0.0;
            auto sumRight = 0.0;
            for (auto& sub : subs_) {
                const auto x = oscillator(wave, sub.phase, sub.increment, module_.width) * sub.gain;
                sumLeft += x * sub.left;
                sumRight += x * sub.right;
                advance(sub.phase, sub.increment);
            }
            outLeft[static_cast<std::size_t>(i)] = sumLeft * module_.norm;
            outRight[static_cast<std::size_t>(i)] = sumRight * module_.norm;
        }
    }

    void pairInto(const int kind, const int pairCount, const int length, std::array<double, stepLength>& outLeft,
                  std::array<double, stepLength>& outRight) noexcept
    {
        for (int i = 0; i < length; ++i) {
            auto sumLeft = 0.0;
            auto sumRight = 0.0;
            for (int at = 0; at < pairCount; ++at) {
                auto& sub = subs_[static_cast<std::size_t>(at)];
                const auto x = next(kind, sub) * sub.gain;
                sumLeft += x * sub.left;
                sumRight += x * sub.right;
            }
            outLeft[static_cast<std::size_t>(i)] = sumLeft;
            outRight[static_cast<std::size_t>(i)] = sumRight;
        }
    }

    // The noise band: white noise through bands at the note (and its
    // overtones), its sides apart as wide as the spread.
    void bandInto(const int length, std::array<double, stepLength>& outLeft, std::array<double, stepLength>& outRight) noexcept
    {
        const auto angle = module_.stereo * kPi * 0.5;
        const auto same = std::cos(angle);
        const auto apart = std::sin(angle);
        for (int i = 0; i < length; ++i) {
            const auto a = random();
            const auto b = random();
            const std::array<double, 2> in { a, a * same + b * apart };
            std::array<double, 2> out {};
            for (std::size_t side = 0; side < 2; ++side)
                for (std::size_t band = 0; band < static_cast<std::size_t>(bandCount); ++band)
                    if (module_.bands[band].gain > 0.0)
                        out[side] += bandStep(module_.bands[band], bands_[side][band], in[side]);
            outLeft[static_cast<std::size_t>(i)] = out[0] * 0.7071;
            outRight[static_cast<std::size_t>(i)] = out[1] * 0.7071;
        }
    }

    [[nodiscard]] double subSample(const int wave) const noexcept
    {
        switch (wave) {
        case 1: return oscillator(2, subPhase_, subIncrement_, 0.5);
        case 2: return oscillator(1, subPhase_, subIncrement_, 0.5) * 0.7;
        default: return std::sin(2.0 * kPi * subPhase_);
        }
    }

    // The pace a glide from here to the note takes to last its time.
    void aimGlide(const Values& values) noexcept
    {
        const auto glideSeconds = static_cast<double>(values[voicingGlide]);
        glidePerSample_ = glideSeconds > 0.0 ? std::abs(target_ - pitch_) / (glideSeconds * rate_) : 0.0;
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
    double elapsed_ {};
    double pitchLevel_ {};
    Envelope amp_;
    Envelope filterEnvelope_;
    std::array<MultiFilter, 2> filters_ {};
    std::array<std::array<BandState, bandCount>, 2> bands_ {};
    std::array<double, 2> noiseLow_ {};
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    std::array<Sub, swarmCount> subs_ {};
    bool paired_ {};
    double subPhase_ {};
    double subIncrement_ {};
    Module module_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    // Each voice its own draw: their phases and noises apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeAlliage()
{
    return std::make_unique<AlliageEngine>();
}

std::vector<float> alliagePicture(const Values& values, const std::string_view part, const int points)
{
    // The oscillator module over two periods of its note, as it plays (its
    // kind, its timbre and its shape; a swarm's voices at one moment, a
    // noise band as it rings on 220 Hz).
    if (part != "osc" || points < 2)
        return {};
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto kind = std::clamp(static_cast<int>(std::lround(v(oscType))), 0, oscKindCount - 1);
    const auto timbre = std::clamp(v(oscTimbre), 0.0, 1.0);
    const auto shape = std::clamp(v(oscShape), 0.0, 1.0);
    std::vector<float> result(static_cast<std::size_t>(points));
    const auto phaseAt = [points](const int at) { return 2.0 * static_cast<double>(at) / static_cast<double>(points - 1); };
    // A wave worked out finely, read across the picture.
    const auto resample = [&result, points](const std::vector<double>& wave, const double span) {
        for (int at = 0; at < points; ++at) {
            const auto position = std::clamp(span * static_cast<double>(at) / static_cast<double>(points - 1), 0.0,
                                             static_cast<double>(wave.size()) - 1.0);
            const auto lower = std::min(static_cast<std::size_t>(position), wave.size() - 1);
            const auto upper = std::min(lower + 1, wave.size() - 1);
            const auto mix = position - static_cast<double>(lower);
            result[static_cast<std::size_t>(at)] = static_cast<float>(wave[lower] + (wave[upper] - wave[lower]) * mix);
        }
    };
    switch (kind) {
    case swarmOsc:
    case squaresOsc: {
        static constexpr std::array<double, swarmCount> starts { 0.13, 0.37, 0.71, 0.0, 0.52, 0.89, 0.26 };
        const auto mix = swarmMix(kind, timbre, shape);
        const auto width = kind == squaresOsc ? 0.5 - 0.42 * shape : 0.5;
        std::array<double, swarmCount> ratios {};
        for (std::size_t voice = 0; voice < ratios.size(); ++voice)
            ratios[voice] = std::pow(2.0, kSwarmDetune[voice] * mix.cents / 1'200.0);
        const auto scale = kind == squaresOsc ? mix.norm / 0.6 : mix.norm;
        for (int at = 0; at < points; ++at) {
            auto x = 0.0;
            for (std::size_t voice = 0; voice < ratios.size(); ++voice) {
                auto phase = starts[voice] + phaseAt(at) * ratios[voice];
                phase -= std::floor(phase);
                const auto wave = kind == squaresOsc ? (phase < width ? 1.0 : -1.0) - (2.0 * width - 1.0) : 2.0 * phase - 1.0;
                x += wave * (voice == 3 ? mix.centre : mix.side);
            }
            result[static_cast<std::size_t>(at)] = static_cast<float>(x * scale);
        }
        break;
    }
    case wavetableOsc: {
        const auto set = std::clamp(static_cast<int>(std::lround(v(oscTable))), 0, tables::sets - 1);
        const auto mode = std::clamp(static_cast<int>(std::lround(v(oscMode))), 0, tables::modes - 1);
        for (int at = 0; at < points; ++at)
            result[static_cast<std::size_t>(at)] = static_cast<float>(tables::sample(set, timbre, mode, shape, 2, phaseAt(at)));
        break;
    }
    case pulseOsc: {
        const auto width = std::clamp(0.5 - 0.44 * timbre, 0.04, 0.96);
        for (int at = 0; at < points; ++at) {
            const auto phase = phaseAt(at) - std::floor(phaseAt(at));
            result[static_cast<std::size_t>(at)] = static_cast<float>((phase < width ? 1.0 : -1.0) - (2.0 * width - 1.0));
        }
        break;
    }
    case fmOsc: {
        // Worked out sample by sample (the feedback), a period first for it to settle.
        constexpr int perPeriod = 256;
        const auto ratio = kRatioValues[static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(v(oscRatio))), 0, static_cast<int>(kRatioValues.size()) - 1))];
        Sub sub;
        sub.increment = 1.0 / perPeriod;
        sub.increment2 = std::min(0.45, sub.increment * ratio);
        std::vector<double> wave(static_cast<std::size_t>(2 * perPeriod + 1));
        for (int at = -perPeriod; at <= 2 * perPeriod; ++at) {
            const auto x = fmSample(sub, 10.0 * timbre * timbre, 1.5 * shape);
            if (at >= 0)
                wave[static_cast<std::size_t>(at)] = x;
        }
        resample(wave, 2.0 * perPeriod);
        break;
    }
    case syncOsc: {
        const auto ratio = std::pow(2.0, 3.0 * timbre);
        for (int at = 0; at < points; ++at) {
            const auto master = phaseAt(at) - std::floor(phaseAt(at));
            auto slave = master * ratio;
            slave -= std::floor(slave);
            result[static_cast<std::size_t>(at)] = static_cast<float>((2.0 * slave - 1.0) * windowGain(shape, master));
        }
        break;
    }
    case additiveOsc: {
        std::array<double, harmonicCount> amplitudes {};
        const auto top = harmonicsOf(timbre, shape, 0.0, amplitudes);
        for (int at = 0; at < points; ++at) {
            auto x = 0.0;
            for (int n = 0; n < top; ++n)
                x += amplitudes[static_cast<std::size_t>(n)] * std::sin(2.0 * kPi * static_cast<double>(n + 1) * phaseAt(at));
            result[static_cast<std::size_t>(at)] = static_cast<float>(x);
        }
        break;
    }
    default: {
        // The band on 220 Hz at 48 kHz, after it has rung in, its noise always the same draw.
        constexpr double rate = 48'000.0;
        constexpr double hz = 220.0;
        const auto damping = bandDampingOf(timbre);
        const auto levels = bandLevelsOf(shape);
        std::array<Band, bandCount> bands {};
        for (std::size_t band = 0; band < bands.size(); ++band)
            bands[band] = bandAt(hz * static_cast<double>(band + 1), damping, levels[band], rate);
        std::array<BandState, bandCount> states {};
        std::uint32_t seed = 0x2545f491u;
        const auto span = 2.0 * rate / hz;
        std::vector<double> wave(static_cast<std::size_t>(std::ceil(span)) + 2);
        for (int at = -4'000; at < static_cast<int>(wave.size()); ++at) {
            seed = seed * 1'664'525u + 1'013'904'223u;
            const auto white = static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
            auto x = 0.0;
            for (std::size_t band = 0; band < bands.size(); ++band)
                if (bands[band].gain > 0.0)
                    x += bandStep(bands[band], states[band], white);
            if (at >= 0)
                wave[static_cast<std::size_t>(at)] = x;
        }
        resample(wave, span);
        break;
    }
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
