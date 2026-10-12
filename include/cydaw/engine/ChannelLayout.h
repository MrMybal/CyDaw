#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace cydaw::engine {

// A track's audio format: how many channels it carries, and which speaker
// each one feeds. One layout per count, in the order of a WAV file's
// channels (what a multichannel recording or export holds):
//
//   1 mono   M
//   2 stereo L R
//   3 LCR    L R C
//   4 quad   L R Ls Rs
//   5 5.0    L R C Ls Rs
//   6 5.1    L R C LFE Ls Rs
//   7 7.0    L R C Lb Rb Ls Rs
//   8 7.1    L R C LFE Lb Rb Ls Rs
//
// Between two formats the sound converts: a speaker the other has is kept as
// it is, one it lacks goes to its nearest neighbours (a centre to left and
// right at -3 dB, a surround to its side at -3 dB), the LFE alone is dropped
// in a format without one. Mono spreads to left and right at full level (as
// a mono file always played on a stereo track), and anything folds to mono
// as half its left plus half its right.
inline constexpr int maximumTrackChannels = 8;

enum class SpeakerRole : unsigned char {
    mono,
    left,
    right,
    centre,
    lfe,
    surroundLeft,
    surroundRight,
    backLeft,
    backRight,
};

[[nodiscard]] constexpr int clampTrackChannels(const int channels) noexcept
{
    return std::clamp(channels, 1, maximumTrackChannels);
}

[[nodiscard]] constexpr SpeakerRole speakerRole(const int channels, const int channel) noexcept
{
    using R = SpeakerRole;
    constexpr std::array<std::array<R, 8>, 8> roles { {
        { R::mono },
        { R::left, R::right },
        { R::left, R::right, R::centre },
        { R::left, R::right, R::surroundLeft, R::surroundRight },
        { R::left, R::right, R::centre, R::surroundLeft, R::surroundRight },
        { R::left, R::right, R::centre, R::lfe, R::surroundLeft, R::surroundRight },
        { R::left, R::right, R::centre, R::backLeft, R::backRight, R::surroundLeft, R::surroundRight },
        { R::left, R::right, R::centre, R::lfe, R::backLeft, R::backRight, R::surroundLeft, R::surroundRight },
    } };
    const auto count = clampTrackChannels(channels);
    if (channel < 0 || channel >= count)
        return R::mono;
    return roles[static_cast<std::size_t>(count - 1)][static_cast<std::size_t>(channel)];
}

// -1 a speaker on the left, +1 on the right, 0 in the middle (centre, LFE,
// mono): where a balance knob turns it down.
[[nodiscard]] constexpr int speakerSide(const SpeakerRole role) noexcept
{
    switch (role) {
    case SpeakerRole::left:
    case SpeakerRole::surroundLeft:
    case SpeakerRole::backLeft:
        return -1;
    case SpeakerRole::right:
    case SpeakerRole::surroundRight:
    case SpeakerRole::backRight:
        return 1;
    default:
        return 0;
    }
}

// "mono", "stereo", "lcr", "quad", "5.0", "5.1", "7.0", "7.1".
[[nodiscard]] constexpr std::string_view layoutKey(const int channels) noexcept
{
    constexpr std::array<std::string_view, 8> keys { "mono", "stereo", "lcr", "quad", "5.0", "5.1", "7.0", "7.1" };
    return keys[static_cast<std::size_t>(clampTrackChannels(channels) - 1)];
}

// A layout's key (or its channel count written out: "4"), 0 for none.
[[nodiscard]] constexpr int layoutChannels(const std::string_view key) noexcept
{
    for (auto channels = 1; channels <= maximumTrackChannels; ++channels)
        if (layoutKey(channels) == key)
            return channels;
    if (key.size() == 1 && key[0] >= '1' && key[0] <= '8')
        return key[0] - '0';
    return 0;
}

// "L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "M".
[[nodiscard]] constexpr std::string_view speakerShortName(const SpeakerRole role) noexcept
{
    switch (role) {
    case SpeakerRole::mono: return "M";
    case SpeakerRole::left: return "L";
    case SpeakerRole::right: return "R";
    case SpeakerRole::centre: return "C";
    case SpeakerRole::lfe: return "LFE";
    case SpeakerRole::surroundLeft: return "Ls";
    case SpeakerRole::surroundRight: return "Rs";
    case SpeakerRole::backLeft: return "Lb";
    case SpeakerRole::backRight: return "Rb";
    }
    return "?";
}

// Gains from each channel of one format into each of another's:
// out[to] = sum over from of gain(to, from) * in[from].
struct ChannelMatrix {
    int from { 2 };
    int to { 2 };
    std::array<float, maximumTrackChannels * maximumTrackChannels> gains {};

    [[nodiscard]] constexpr float gain(const int destination, const int source) const noexcept
    {
        return gains[static_cast<std::size_t>(destination * maximumTrackChannels + source)];
    }
    constexpr void set(const int destination, const int source, const float value) noexcept
    {
        gains[static_cast<std::size_t>(destination * maximumTrackChannels + source)] = value;
    }
    [[nodiscard]] constexpr bool identity() const noexcept
    {
        if (from != to)
            return false;
        for (auto destination = 0; destination < to; ++destination)
            for (auto source = 0; source < from; ++source)
                if (gain(destination, source) != (destination == source ? 1.0f : 0.0f))
                    return false;
        return true;
    }
};

namespace detail {
inline constexpr float minus3Db = 0.70710678f;

[[nodiscard]] constexpr int channelOf(const int channels, const SpeakerRole role) noexcept
{
    for (auto channel = 0; channel < clampTrackChannels(channels); ++channel)
        if (speakerRole(channels, channel) == role)
            return channel;
    return -1;
}

// One source speaker into a format of two channels or more.
constexpr void spread(ChannelMatrix& matrix, const int source, const SpeakerRole role) noexcept
{
    using R = SpeakerRole;
    const auto to = matrix.to;
    const auto add = [&matrix, to, source](const R target, const float value) {
        if (const auto channel = channelOf(to, target); channel >= 0) {
            matrix.set(channel, source, matrix.gain(channel, source) + value);
            return true;
        }
        return false;
    };
    if (role == R::mono) {
        (void) add(R::left, 1.0f);
        (void) add(R::right, 1.0f);
        return;
    }
    if (add(role, 1.0f))
        return;
    switch (role) {
    case R::centre:
        (void) add(R::left, minus3Db);
        (void) add(R::right, minus3Db);
        return;
    case R::lfe:
        return;
    case R::surroundLeft:
        if (!add(R::backLeft, 1.0f))
            (void) add(R::left, minus3Db);
        return;
    case R::surroundRight:
        if (!add(R::backRight, 1.0f))
            (void) add(R::right, minus3Db);
        return;
    case R::backLeft:
        if (!add(R::surroundLeft, 1.0f))
            (void) add(R::left, minus3Db);
        return;
    case R::backRight:
        if (!add(R::surroundRight, 1.0f))
            (void) add(R::right, minus3Db);
        return;
    default:
        return;
    }
}
} // namespace detail

[[nodiscard]] constexpr ChannelMatrix channelConversion(const int fromChannels, const int toChannels) noexcept
{
    ChannelMatrix matrix;
    matrix.from = clampTrackChannels(fromChannels);
    matrix.to = clampTrackChannels(toChannels);
    if (matrix.from == matrix.to) {
        for (auto channel = 0; channel < matrix.to; ++channel)
            matrix.set(channel, channel, 1.0f);
        return matrix;
    }
    if (matrix.to == 1) {
        // Folded to stereo first, then half of each side.
        ChannelMatrix stereo;
        stereo.from = matrix.from;
        stereo.to = 2;
        for (auto source = 0; source < matrix.from; ++source)
            detail::spread(stereo, source, speakerRole(matrix.from, source));
        for (auto source = 0; source < matrix.from; ++source)
            matrix.set(0, source, 0.5f * (stereo.gain(0, source) + stereo.gain(1, source)));
        return matrix;
    }
    for (auto source = 0; source < matrix.from; ++source)
        detail::spread(matrix, source, speakerRole(matrix.from, source));
    return matrix;
}

// What a balance knob (-1 left .. +1 right) leaves of a speaker: the far side
// turned down, the middle untouched.
[[nodiscard]] inline float balanceGain(const SpeakerRole role, const float pan) noexcept
{
    const auto side = speakerSide(role);
    if (side < 0)
        return pan <= 0.0f ? 1.0f : 1.0f - std::min(1.0f, pan);
    if (side > 0)
        return pan >= 0.0f ? 1.0f : 1.0f + std::max(-1.0f, pan);
    return 1.0f;
}

// `numSamples` of `in` (the matrix's `from` channels) added to, or written
// over, `out` (its `to` channels), each scaled by `gain`, and the
// destination's speakers by the balance `pan` (0 for none).
inline void convertChannels(const ChannelMatrix& matrix, const float* const* in, float* const* out,
                            const int numSamples, const float gain = 1.0f, const bool add = true,
                            const float pan = 0.0f) noexcept
{
    for (auto destination = 0; destination < matrix.to; ++destination) {
        float* target = out[destination];
        if (target == nullptr)
            continue;
        if (!add)
            std::fill(target, target + numSamples, 0.0f);
        const auto side = pan != 0.0f ? balanceGain(speakerRole(matrix.to, destination), pan) : 1.0f;
        for (auto source = 0; source < matrix.from; ++source) {
            const auto weight = matrix.gain(destination, source) * gain * side;
            if (weight == 0.0f || in[source] == nullptr)
                continue;
            const float* from = in[source];
            for (auto sample = 0; sample < numSamples; ++sample)
                target[sample] += from[sample] * weight;
        }
    }
}

} // namespace cydaw::engine
