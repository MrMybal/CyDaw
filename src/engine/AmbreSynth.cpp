// Ambre: an analog polysynth of the synth family. Three oscillators (saw,
// pulse with its width, triangle, sine; the second synced to the first, the
// third modulating the first's frequency), noise, an analog drift, up to four
// unison voices spread across the stereo; a high-pass, then one of four
// filters of character (a ladder, a cascade, a state-variable multimode, a
// biting one that saturates its own resonance); two envelopes that curve as
// capacitors do; two LFOs.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cydaw::engine::synth::detail {

namespace {

// Ambre's settings after the shared voicing, in its table's order.
enum Ambre : int {
    osc1Wave = voicingFieldCount,
    osc1Octave,
    osc1Pw,
    osc1Level,
    osc2Wave,
    osc2Octave,
    osc2Semi,
    osc2Fine,
    osc2Pw,
    osc2Level,
    osc2Sync,
    osc3Wave,
    osc3Octave,
    osc3Semi,
    osc3Fine,
    osc3Level,
    fmAmount,
    noiseLevel,
    driftAmount,
    unisonVoices,
    unisonDetune,
    unisonSpread,
    hpfCutoff,
    filterModel,
    filterMode,
    filterCutoff,
    filterResonance,
    filterDrive,
    filterEnv,
    filterKey,
    filterVelocity,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    fenvAttack,
    fenvDecay,
    fenvSustain,
    fenvRelease,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo1Delay,
    lfo1Pitch,
    lfo1Pwm,
    lfo2Shape,
    lfo2Rate,
    lfo2Sync,
    lfo2Cutoff,
    lfo2Amp,
    modWheelVibrato,
    ambreCount
};

const std::vector<std::pair<std::string, std::string>> kWaves { { "Dent de scie", "Saw" }, { "Impulsion", "Pulse" },
                                                                 { "Triangle", "Triangle" }, { "Sinus", "Sine" } };
const std::vector<std::pair<std::string, std::string>> kLfoShapes { { "Sinus", "Sine" }, { "Triangle", "Triangle" },
                                                                     { "Dent de scie", "Saw" }, { "Carré", "Square" },
                                                                     { "Aléatoire", "Random" } };

} // namespace

std::vector<Section> ambreSections()
{
    return { { "VOIX", "VOICES", 0xffb0b8c8u },     { "OSCILLATEURS", "OSCILLATORS", 0xffffb04au },
             { "FILTRE", "FILTER", 0xffff7a52u },   { "ENVELOPPES", "ENVELOPES", 0xff7ad0ffu },
             { "LFO", "LFO", 0xffa58cffu } };
}

std::vector<Parameter> ambreParameters()
{
    Table table;
    addVoicing(table, 16, 0.7f);
    table.section = 1;
    table.choice("osc1.wave", "Osc 1 · forme", "Osc 1 · wave", kWaves, 0);
    table.whole("osc1.octave", "Osc 1 · octave", "Osc 1 · octave", {}, -2, 2, 0);
    table.add("osc1.pw", "Osc 1 · largeur", "Osc 1 · width", {}, 0.05f, 0.95f, 0.5f);
    table.add("osc1.level", "Osc 1 · niveau", "Osc 1 · level", {}, 0.0f, 1.0f, 0.8f);
    table.choice("osc2.wave", "Osc 2 · forme", "Osc 2 · wave", kWaves, 0);
    table.whole("osc2.octave", "Osc 2 · octave", "Osc 2 · octave", {}, -2, 2, 0);
    table.whole("osc2.semi", "Osc 2 · demi-tons", "Osc 2 · semitones", "st", -12, 12, 0);
    table.add("osc2.fine", "Osc 2 · fin", "Osc 2 · fine", "ct", -50.0f, 50.0f, 7.0f);
    table.add("osc2.pw", "Osc 2 · largeur", "Osc 2 · width", {}, 0.05f, 0.95f, 0.5f);
    table.add("osc2.level", "Osc 2 · niveau", "Osc 2 · level", {}, 0.0f, 1.0f, 0.6f);
    table.toggle("osc2.sync", "Osc 2 · synchro", "Osc 2 · sync", false);
    table.choice("osc3.wave", "Osc 3 · forme", "Osc 3 · wave", kWaves, 1);
    table.whole("osc3.octave", "Osc 3 · octave", "Osc 3 · octave", {}, -2, 2, -1);
    table.whole("osc3.semi", "Osc 3 · demi-tons", "Osc 3 · semitones", "st", -12, 12, 0);
    table.add("osc3.fine", "Osc 3 · fin", "Osc 3 · fine", "ct", -50.0f, 50.0f, -5.0f);
    table.add("osc3.level", "Osc 3 · niveau", "Osc 3 · level", {}, 0.0f, 1.0f, 0.0f);
    table.add("fm.amount", "FM (osc 3 → osc 1)", "FM (osc 3 → osc 1)", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.level", "Bruit", "Noise", {}, 0.0f, 1.0f, 0.0f);
    table.add("drift", "Dérive", "Drift", {}, 0.0f, 1.0f, 0.3f);
    table.whole("unison.voices", "Unisson", "Unison", {}, 1, 4, 1);
    table.add("unison.detune", "Unisson · désaccord", "Unison · detune", "ct", 0.0f, 50.0f, 12.0f);
    table.add("unison.spread", "Unisson · largeur", "Unison · spread", {}, 0.0f, 1.0f, 0.7f);
    table.section = 2;
    table.add("hpf", "Passe-haut", "High-pass", "Hz", 20.0f, 2'000.0f, 20.0f, Kind::continuous, true);
    table.choice("filter.model", "Modèle", "Model",
                 { { "Ladder", "Ladder" }, { "Cascade", "Cascade" }, { "Multimode", "Multimode" }, { "Mordant", "Biting" } }, 0);
    table.choice("filter.mode", "Mode", "Mode", { { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" }, { "Passe-haut", "High-pass" } },
                 0);
    table.add("filter.cutoff", "Fréquence", "Cutoff", "Hz", 20.0f, 20'000.0f, 1'800.0f, Kind::continuous, true);
    table.add("filter.resonance", "Résonance", "Resonance", {}, 0.0f, 1.0f, 0.25f);
    table.add("filter.drive", "Saturation", "Drive", {}, 0.0f, 1.0f, 0.2f);
    table.add("filter.env", "Enveloppe", "Envelope", {}, -1.0f, 1.0f, 0.45f);
    table.add("filter.key", "Suivi du clavier", "Key tracking", {}, 0.0f, 1.0f, 0.5f);
    table.add("filter.velocity", "Vélocité", "Velocity", {}, 0.0f, 1.0f, 0.3f);
    table.section = 3;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.5f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.75f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.35f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    table.add("fenv.attack", "Filtre · attaque", "Filter · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Filtre · déclin", "Filter · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("fenv.sustain", "Filtre · maintien", "Filter · sustain", {}, 0.0f, 1.0f, 0.25f);
    table.add("fenv.release", "Filtre · relâchement", "Filter · release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous, true);
    table.section = 4;
    table.choice("lfo1.shape", "LFO 1 · forme", "LFO 1 · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO 1 · vitesse", "LFO 1 · rate", "Hz", 0.05f, 20.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO 1 · synchro", "LFO 1 · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO 1 · retard", "LFO 1 · delay", "s", 0.0f, 3.0f, 0.25f);
    table.add("lfo1.pitch", "LFO 1 → hauteur", "LFO 1 → pitch", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.pwm", "LFO 1 → largeur", "LFO 1 → width", {}, 0.0f, 1.0f, 0.0f);
    table.choice("lfo2.shape", "LFO 2 · forme", "LFO 2 · shape", kLfoShapes, 1);
    table.add("lfo2.rate", "LFO 2 · vitesse", "LFO 2 · rate", "Hz", 0.05f, 20.0f, 0.4f, Kind::continuous, true);
    table.whole("lfo2.sync", "LFO 2 · synchro", "LFO 2 · sync", {}, 0, 12, 0);
    table.add("lfo2.cutoff", "LFO 2 → filtre", "LFO 2 → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo2.amp", "LFO 2 → volume", "LFO 2 → volume", {}, 0.0f, 1.0f, 0.0f);
    table.add("mod.wheel", "Molette → vibrato", "Wheel → vibrato", {}, 0.0f, 1.0f, 0.5f);
    return table.parameters;
}

namespace {

using namespace parts;

class AmbreEngine final : public Engine {
public:
    void prepare(const double sampleRate) override { rate_ = std::max(1.0, sampleRate); }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
        filterEnvelope_.level = 0.0;
        amp_.start();
        filterEnvelope_.start();
        for (auto& filter : filters_)
            filter.reset();
        hpfState_ = {};
        elapsed_ = 0.0;
        lfoPhases_ = { 0.0, 0.0 };
        lfoHeld_ = { random() , random() };
        for (auto& sub : subs_) {
            // Free-running oscillators: each note starts where they were.
            for (auto& phase : sub.phase)
                phase = 0.5 * (random() + 1.0);
            sub.drift = random() * 0.5;
            sub.driftTarget = random();
        }
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
        const auto choice = [&values](const int index, const int count) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, count - 1);
        };
        constexpr int step = 16;
        // What holds for the whole block.
        const auto waves = std::array<int, 3> { choice(osc1Wave, 4), choice(osc2Wave, 4), choice(osc3Wave, 4) };
        const auto levels = std::array<double, 3> { v(osc1Level), v(osc2Level), v(osc3Level) };
        const auto offsets = std::array<double, 3> { 12.0 * std::round(v(osc1Octave)),
                                                     12.0 * std::round(v(osc2Octave)) + std::round(v(osc2Semi)) + v(osc2Fine) / 100.0,
                                                     12.0 * std::round(v(osc3Octave)) + std::round(v(osc3Semi)) + v(osc3Fine) / 100.0 };
        const auto sync = v(osc2Sync) >= 0.5;
        const auto fm = v(fmAmount);
        const auto noise = v(noiseLevel);
        const auto drift = v(driftAmount);
        const auto voices = std::clamp(static_cast<int>(std::lround(v(unisonVoices))), 1, 4);
        const auto detune = v(unisonDetune);
        const auto spread = v(unisonSpread);
        const auto model = choice(filterModel, 4);
        const auto mode = choice(filterMode, 3);
        const auto resonance = std::clamp(v(filterResonance), 0.0, 1.0);
        const auto drive = 1.0 + 4.0 * v(filterDrive);
        const auto driveBack = 1.0 / (1.0 + 1.5 * v(filterDrive));
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto velocityFilter = 1.0 - v(filterVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        // Coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto filterAttackStep = coefficient(v(fenvAttack), 1.0 / 1.466);
        const auto filterDecayStep = coefficient(v(fenvDecay), 1.0 / 4.6);
        const auto filterReleaseStep = coefficient(v(fenvRelease), 1.0 / 4.6);
        const auto lfoRate = [&](const int rateIndex, const int syncIndex) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(syncIndex)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(rateIndex);
        };
        const std::array<double, 2> lfoRates { lfoRate(lfo1Rate, lfo1Sync), lfoRate(lfo2Rate, lfo2Sync) };
        const std::array<int, 2> lfoShapes { choice(lfo1Shape, 5), choice(lfo2Shape, 5) };
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto vibratoDepth = v(lfo1Pitch) + static_cast<double>(context.modWheel) * v(modWheelVibrato);
        const auto hpf = v(hpfCutoff);
        const auto hpfCoefficient = hpf > 20.5 ? std::exp(-2.0 * kPi * std::min(hpf, rate_ * 0.45) / rate_) : 0.0;

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            // The slow things, once a step: envelopes, LFOs, glide, drift, the pitches and the filter.
            const auto ampLevel = amp_.step(ampAttackStep, ampDecayStep, v(ampSustain), ampReleaseStep);
            const auto filterLevel = filterEnvelope_.step(filterAttackStep, filterDecayStep, v(fenvSustain), filterReleaseStep);
            if (!active())
                return false;
            elapsed_ += static_cast<double>(length) / rate_;
            std::array<double, 2> lfo {};
            for (std::size_t index = 0; index < 2; ++index) {
                lfoPhases_[index] += lfoRates[index] * static_cast<double>(length) / rate_;
                if (lfoPhases_[index] >= 1.0) {
                    lfoPhases_[index] -= std::floor(lfoPhases_[index]);
                    lfoHeld_[index] = random();
                }
                lfo[index] = lfoWave(lfoShapes[index], lfoPhases_[index], lfoHeld_[index]);
            }
            const auto lfoFade = v(lfo1Delay) > 0.0 ? std::min(1.0, elapsed_ / v(lfo1Delay)) : 1.0;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend + lfo[0] * lfoFade * vibratoDepth;
            const auto width = std::clamp(0.5 + lfo[0] * lfoFade * v(lfo1Pwm) * 0.4, 0.05, 0.95);
            const std::array<double, 3> widths { std::clamp(v(osc1Pw) + (width - 0.5), 0.05, 0.95),
                                                 std::clamp(v(osc2Pw) + (width - 0.5), 0.05, 0.95), 0.5 };
            for (int sub = 0; sub < voices; ++sub) {
                auto& unison = subs_[static_cast<std::size_t>(sub)];
                unison.drift += (unison.driftTarget - unison.drift) * 0.02;
                if (random() > 0.985)
                    unison.driftTarget = random();
                const auto place = voices == 1 ? 0.0 : 2.0 * sub / (voices - 1) - 1.0;
                const auto cents = place * detune + unison.drift * drift * 8.0;
                for (std::size_t osc = 0; osc < 3; ++osc) {
                    const auto hz = 440.0 * std::pow(2.0, (basePitch + offsets[osc] + cents / 100.0 - 69.0) / 12.0);
                    unison.increment[osc] = std::clamp(hz / rate_, 0.0, 0.45);
                }
                const auto pan = place * spread;
                unison.left = std::cos((pan + 1.0) * kPi * 0.25);
                unison.right = std::sin((pan + 1.0) * kPi * 0.25);
            }
            // The cutoff: its knob, the envelope (6 octaves), LFO 2 (3), the key, the velocity.
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(filterCutoff))) + v(filterEnv) * filterLevel * 72.0 * velocityFilter
                                   + lfo[1] * v(lfo2Cutoff) * 36.0 + v(filterKey) * (target_ - 60.0);
            const auto cutoff = std::clamp(std::pow(2.0, semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto g = std::tan(kPi * cutoff / rate_);
            const auto tremolo = 1.0 - v(lfo2Amp) * 0.5 * (1.0 - lfo[1]);
            const auto level = ampLevel * velocityLevel * master * tremolo * expressionGain * 0.5 / std::sqrt(static_cast<double>(voices));
            // The level eased across the step: no steps heard on a quick attack.
            const auto from = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                const auto gain = from + (level - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                auto sumLeft = 0.0;
                auto sumRight = 0.0;
                for (int sub = 0; sub < voices; ++sub) {
                    auto& unison = subs_[static_cast<std::size_t>(sub)];
                    // Oscillator 3 first: it moves oscillator 1.
                    const auto three = oscillator(waves[2], unison.phase[2], unison.increment[2], widths[2]);
                    const auto oneIncrement = std::clamp(unison.increment[0] * (1.0 + fm * 2.0 * three), 0.0, 0.45);
                    const auto one = oscillator(waves[0], unison.phase[0], oneIncrement, widths[0]);
                    const auto two = oscillator(waves[1], unison.phase[1], unison.increment[1], widths[1]);
                    const auto mixed = one * levels[0] + two * levels[1] + three * levels[2];
                    sumLeft += mixed * unison.left;
                    sumRight += mixed * unison.right;
                    unison.phase[0] += oneIncrement;
                    const auto wrapped = unison.phase[0] >= 1.0;
                    if (wrapped)
                        unison.phase[0] -= 1.0;
                    unison.phase[1] += unison.increment[1];
                    if (sync && wrapped)
                        unison.phase[1] = unison.phase[0] * (unison.increment[1] / std::max(1.0e-9, oneIncrement));
                    unison.phase[1] -= std::floor(unison.phase[1]);
                    unison.phase[2] += unison.increment[2];
                    unison.phase[2] -= std::floor(unison.phase[2]);
                }
                if (noise > 0.0) {
                    const auto hiss = random() * noise * 0.7;
                    sumLeft += hiss;
                    sumRight += hiss;
                }
                std::array<double, 2> channel { sumLeft, sumRight };
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = channel[side];
                    if (hpfCoefficient > 0.0) {
                        hpfState_[side] = x + hpfCoefficient * (hpfState_[side] - x);
                        x -= hpfState_[side];
                    }
                    x = std::tanh(x * drive) * driveBack;
                    auto& filter = filters_[side];
                    switch (model) {
                    case 0: x = filter.ladder(x, g, resonance); break;
                    case 1: x = filter.cascade(x, g, resonance); break;
                    case 2: x = filter.variable(x, g, resonance, mode, false); break;
                    default: x = filter.variable(x, g, resonance, mode, true); break;
                    }
                    channel[side] = x * gain;
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
            }
        }
        return active();
    }

private:
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

    struct Unison {
        std::array<double, 3> phase {};
        std::array<double, 3> increment {};
        double drift {};
        double driftTarget {};
        double left { 0.7071 };
        double right { 0.7071 };
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    Envelope amp_;
    Envelope filterEnvelope_;
    std::array<Filter, 2> filters_ {};
    std::array<double, 2> hpfState_ {};
    std::array<double, 2> lfoPhases_ {};
    std::array<double, 2> lfoHeld_ {};
    std::array<Unison, 4> subs_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    // Each voice its own draw: their drifts and phases apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeAmbre()
{
    return std::make_unique<AmbreEngine>();
}

} // namespace cydaw::engine::synth::detail
