#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Kick: an instrument for kick drums alone. A body (a sine, or a rounder or
// harder wave) whose pitch falls from a start to an end over its own time
// and curve, under an amplitude envelope (attack, hold, decay and its
// curve); a click on top (noise, a click or a blip, its tone and decay); a
// sub (a sine at the end pitch or an octave under, with its own decay);
// then a drive (soft, hard, fold or tube), and tone (low cut, low boost,
// high cut). The note can tune it (C2 plays it as set).
//
// Its settings are one flat array of numbers (KickValues), each described
// by parameter(); what the voice plays and what the interface draws come
// from the same functions (pitchAt, amplitudeAt, renderKick). Nothing here
// knows JUCE.

namespace cydaw::engine::kick {

enum Value : int {
    bodyShape,
    bodyPhase,
    pitchStart,
    pitchEnd,
    pitchTime,
    pitchCurve,
    bodyLevel,
    attack,
    hold,
    decay,
    decayCurve,
    length,
    clickType,
    clickLevel,
    clickDecay,
    clickTone,
    subLevel,
    subDecay,
    subOctave,
    driveType,
    drive,
    driveTone,
    lowCut,
    highCut,
    lowBoost,
    keyTrack,
    velocity,
    output,
    parameterCount
};

using KickValues = std::array<float, parameterCount>;

enum class Group : std::uint8_t { body, amplitude, click, sub, drive, tone, global };

struct Parameter {
    std::string key;     // "pitchStart"
    std::string french;
    std::string english;
    std::string unit;    // "Hz", "ms", "dB", "%", "°"…
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    Group group { Group::body };
    bool logarithmic {};
    // A choice's options, French then English for each (empty: a number).
    std::vector<std::pair<std::string, std::string>> choices;
};

[[nodiscard]] const Parameter& parameter(int index);
// -1 when no parameter has that key.
[[nodiscard]] int parameterIndex(const std::string& key);
[[nodiscard]] KickValues defaultValues();
[[nodiscard]] float clampValue(int index, float value);
// The value as a person reads it ("52 Hz", "Sinus").
[[nodiscard]] std::string displayValue(int index, float value, bool french);

// The note that plays the kick as set; others tune it by their distance.
inline constexpr int referenceNote = 36;

// What the kick does in time: the body's pitch (Hz) and its amplitude
// (0..1) `seconds` after the hit, for a tuning ratio (1 at the reference
// note); how long it lasts.
[[nodiscard]] double pitchAt(const KickValues& values, double seconds, double tuning = 1.0) noexcept;
[[nodiscard]] double amplitudeAt(const KickValues& values, double seconds) noexcept;
[[nodiscard]] double lengthSeconds(const KickValues& values) noexcept;

class Voice final {
public:
    Voice();
    ~Voice();
    Voice(Voice&&) noexcept;
    Voice& operator=(Voice&&) noexcept;

    void start(const KickValues& values, int note, float velocity, double sampleRate) noexcept;
    // A kick plays out; a note let go changes nothing. Kill: silence now.
    void kill() noexcept;
    [[nodiscard]] bool active() const noexcept;
    // Adds `count` stereo frames; false once it has played out.
    bool render(const KickValues& values, float* left, float* right, int count, double sampleRate) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// A whole kick, mono, as the voice plays it (the interface's picture, a
// bank's file).
[[nodiscard]] std::vector<float> renderKick(const KickValues& values, double sampleRate, int note = referenceNote,
                                            float velocity = 1.0f, double maximumSeconds = 6.0);

} // namespace cydaw::engine::kick
