#pragma once

#include <cydaw/engine/VoiceLive.h>
#include <cydaw/engine/Sampler.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Prisme: a wavetable synthesiser in the manner of the big software synths —
// three wavetable oscillators (frames morphed by position, warped, reshaped in
// their harmonics, up to sixteen unison voices each), a sub oscillator, a
// noise source, a multimode filter, four envelopes, four LFOs (stereo if
// asked), sixteen modulation slots and four macros, in poly, mono or legato
// with glide.
//
// Its settings are one flat array of numbers (PrismValues), each described by
// prismParameter(): a key ("a.position", "env2.decay", "m5.amount"), names,
// a range and how it is shown. The array is what a card stores, what a plan
// carries and what a host automates. The wavetables are separate: built off
// the audio thread, shared by pointer.
//
// Nothing here knows JUCE; a host gives a voice the values, the tables, the
// note and the buffers to add into.

namespace cydaw::engine::prism {

// ---- Settings ---------------------------------------------------------------------------

// The fields of an oscillator (A, then B, the same).
enum Osc : int {
    oscOn,
    oscTable,
    oscPosition,
    oscWarp,
    oscWarpAmount,
    oscUnison,
    oscDetune,
    oscBlend,
    oscWidth,
    oscOctave,
    oscSemi,
    oscFine,
    oscCoarse,
    oscPhase,
    oscRandom,
    oscLevel,
    oscPan,
    oscToFilter,
    oscFieldCount
};
enum Sub : int { subOn, subShape, subOctave, subLevel, subPan, subDirect, subFieldCount };
enum Noise : int { noiseOn, noiseType, noiseTone, noisePitch, noiseLevel, noisePan, noiseToFilter, noiseFieldCount };
enum Filter : int {
    filterOn,
    filterType,
    filterCutoff,
    filterResonance,
    filterDrive,
    filterVariation,
    filterMix,
    filterKeytrack,
    filterFieldCount
};
enum Env : int { envAttack, envHold, envDecay, envSustain, envRelease, envAttackCurve, envDecayCurve, envReleaseCurve, envFieldCount };
enum Lfo : int { lfoShape, lfoMode, lfoSync, lfoRate, lfoDivision, lfoRise, lfoDelay, lfoPhase, lfoSmooth, lfoUnipolar, lfoFieldCount };
enum Voicing : int { voiceMode, voiceCount, voiceGlide, voiceGlideAlways, voiceBendRange, voiceVelocity, voiceMaster, voiceFieldCount };
enum Slot : int { slotSource, slotDestination, slotAmount, slotFieldCount };

inline constexpr int oscillatorCount = 3;
// A and B lie first; C came later, after everything else.
inline constexpr int firstOscillators = 2;
inline constexpr int envelopeCount = 4;
inline constexpr int lfoCount = 4;
inline constexpr int macroCount = 4;
inline constexpr int slotCount = 16;

inline constexpr int subBase = firstOscillators * oscFieldCount;
inline constexpr int noiseBase = subBase + subFieldCount;
inline constexpr int filterBase = noiseBase + noiseFieldCount;
inline constexpr int envBase(const int env) noexcept { return filterBase + filterFieldCount + env * envFieldCount; }
inline constexpr int lfoBase(const int lfo) noexcept { return envBase(envelopeCount) + lfo * lfoFieldCount; }
inline constexpr int voiceBase = lfoBase(lfoCount);
inline constexpr int macroBase = voiceBase + voiceFieldCount;
inline constexpr int slotBase(const int slot) noexcept { return macroBase + macroCount + slot * slotFieldCount; }
// A drawn LFO's shape: its value at 32 places of one cycle (-1..1), read
// between them. After everything else, so a file written before them reads
// the rest at its places.
inline constexpr int drawPoints = 32;
inline constexpr int drawBase(const int lfo) noexcept { return slotBase(slotCount) + lfo * drawPoints; }
// Added after the drawn shapes, for the same reason: oscillator C, then each
// oscillator's spectral morph (its mode, its amount), then each LFO's stereo
// offset (how far on the right side runs, up to half a cycle).
inline constexpr int oscCBase = drawBase(lfoCount);
enum Spectral : int { spectralMode, spectralAmount, spectralFieldCount };
inline constexpr int spectralBase(const int osc) noexcept { return oscCBase + oscFieldCount + osc * spectralFieldCount; }
inline constexpr int lfoStereo(const int lfo) noexcept { return spectralBase(oscillatorCount) + lfo; }
inline constexpr int parameterCount = lfoStereo(lfoCount);
inline constexpr int oscBase(const int osc) noexcept { return osc < firstOscillators ? osc * oscFieldCount : oscCBase; }

using PrismValues = std::array<float, parameterCount>;

enum class Kind : std::uint8_t { continuous, integer, toggle, choice };
enum class Group : std::uint8_t { oscA, oscB, sub, noise, filter, env, lfo, voicing, macro, matrix, oscC };

struct Parameter {
    std::string key;     // "a.position"
    std::string french;  // "Position"
    std::string english;
    std::string unit;    // "Hz", "s", "ct", "st", "%", "dB"…
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    Kind kind { Kind::continuous };
    Group group { Group::oscA };
    int instance {};     // which oscillator, envelope, LFO, macro or slot
    bool logarithmic {};
    bool modulatable {};
    // A choice's options, French then English for each.
    std::vector<std::pair<std::string, std::string>> choices;
};

[[nodiscard]] const Parameter& parameter(int index);
// -1 when no parameter has that key.
[[nodiscard]] int parameterIndex(const std::string& key);
[[nodiscard]] PrismValues defaultValues();
// 0..1 across the range (logarithmic where the parameter is), and back.
[[nodiscard]] float normalise(int index, float value);
[[nodiscard]] float denormalise(int index, float normalised);
[[nodiscard]] float clampValue(int index, float value);
// The value as a person reads it ("440 Hz", "1/8", "Sinus").
[[nodiscard]] std::string displayValue(int index, float value, bool french);

// What a modulation slot reads.
enum class Source : std::uint8_t {
    none,
    env1, env2, env3, env4,
    lfo1, lfo2, lfo3, lfo4,
    velocity,
    note,
    modWheel,
    aftertouch,
    pitchBend,
    macro1, macro2, macro3, macro4,
    random,
    count
};
// The parameters a slot can move: their indices, in the order a slot's
// destination counts them (destination 0 is "none").
[[nodiscard]] const std::vector<int>& destinations();

// The oscillators' warps.
enum class Warp : std::uint8_t {
    off, sync, bendPlus, bendMinus, bendBoth, pwm, asymPlus, asymMinus, mirror, flip, quantise, fm, am, rm, count
};
// The spectral morphs: a wavetable reshaped in its harmonics, by an amount
// that can be modulated (the table read again elsewhere, or at another band
// limit): its highs taken away or kept alone, its odd or even harmonics alone,
// a comb through them, an octave above blended in, its formant moved up.
enum class SpectralMode : std::uint8_t { off, lowPass, highPass, odd, even, comb, octave, formant, count };
enum class FilterType : std::uint8_t {
    lowpass12, lowpass24, ladder, highpass12, highpass24, bandpass12, bandpass24, notch, peak, combPlus, combMinus, formant, count
};
enum class LfoShape : std::uint8_t { sine, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, exponential, drawn, count };
// A drawn shape at a place of its cycle (0..1): between its two points.
[[nodiscard]] inline double drawnLfo(const float* points, const double phase) noexcept
{
    const auto x = (phase - static_cast<double>(static_cast<long long>(phase))) * drawPoints;
    const auto first = static_cast<int>(x) % drawPoints;
    const auto second = (first + 1) % drawPoints;
    const auto t = x - static_cast<double>(static_cast<int>(x));
    return static_cast<double>(points[first]) + (static_cast<double>(points[second]) - static_cast<double>(points[first])) * t;
}
enum class LfoMode : std::uint8_t { trigger, envelope, free, count };
enum class VoiceMode : std::uint8_t { poly, mono, legato, count };

// The divisions a synced LFO runs at, in beats.
[[nodiscard]] double divisionBeats(int division);

// ---- Wavetables -------------------------------------------------------------------------

// Frames of 2048 samples, each kept at ten band limits (all its harmonics,
// then half of them, and so on) so a note reads the one that cannot alias.
struct Wavetable {
    static constexpr int frameLength = 2'048;
    static constexpr int levels = 10;
    std::string name;
    int frameCount {};
    std::vector<float> data; // frame, then level, then sample

    [[nodiscard]] const float* frame(const int index, const int level) const noexcept
    {
        return data.data() + (static_cast<std::size_t>(index) * levels + static_cast<std::size_t>(level)) * frameLength;
    }
};
using WavetableRef = std::shared_ptr<const Wavetable>;

// From one cycle per frame (any length; resampled to 2048, normalised).
[[nodiscard]] WavetableRef buildWavetable(const std::string& name, const std::vector<std::vector<float>>& frames);
// The tables Prisme comes with, by index (built on first use, kept).
[[nodiscard]] int factoryTableCount();
[[nodiscard]] std::string factoryTableName(int index);
[[nodiscard]] WavetableRef factoryTable(int index);

// What a patch plays its oscillators with (null: the factory table its
// "table" setting names).
struct Tables {
    std::array<WavetableRef, oscillatorCount> oscillator;
};
using TablesRef = std::shared_ptr<const Tables>;
// The tables a patch names, the factory ones built if they were not yet.
[[nodiscard]] TablesRef resolveTables(const PrismValues& values, const TablesRef& own = {});

// ---- Playing ------------------------------------------------------------------------

// What every voice of a track shares in a block: tempo, the free LFOs'
// clock, the controllers; what a sampler's SFZ reads of the track's MIDI.
struct Context {
    double tempo { 120.0 };
    double clockSeconds {};
    float modWheel {};
    float aftertouch {};
    float pitchBend {}; // -1..1
    SamplerPlaying sampler;
};

class Voice final {
public:
    Voice();
    ~Voice();
    Voice(Voice&&) noexcept;
    Voice& operator=(Voice&&) noexcept;

    // Off the audio thread: the comb filter's delay for this rate.
    void prepare(double sampleRate);
    // A note. glideFrom: the note it slides from (below 0: none).
    void start(const PrismValues& values, int note, float velocity, double glideFrom, double sampleRate) noexcept;
    // Legato: the same voice slides to another note without starting again.
    void retarget(const PrismValues& values, int note, float velocity) noexcept;
    void release() noexcept;
    void kill() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool releasing() const noexcept;
    [[nodiscard]] int note() const noexcept;
    // Where its pitch is now (a glide in progress), in semitones.
    [[nodiscard]] double currentPitch() const noexcept;
    // Adds `count` stereo frames; false once silent.
    bool render(const PrismValues& values, const Tables& tables, const Context& context, float* left, float* right,
                int count, double sampleRate) noexcept;
    // A note's expression: its pitch moved (semitones) and its gain (linear).
    void setExpression(double pitchSemitones, double gain) noexcept;
    // The modulated value of a parameter (0..1) as the voice last played it,
    // for an interface to show where a modulated knob is.
    [[nodiscard]] float modulated(int index) const noexcept;
    // What it plays now: its modulated settings, LFOs and envelopes.
    void live(VoiceLive& out) const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cydaw::engine::prism
