#pragma once

#include "cydaw/engine/TimeStretch.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// A sound file as the Foundry edits it. Its frames are pieces of blocks laid
// one after the other; a block never changes once made, an edit makes a new
// sound that shares the blocks it did not touch (a piece table). Cutting,
// pasting and undoing cost a list of pieces, not the file: only what a
// process rewrites is new. Each block keeps the lowest and highest value of
// every 256 frames, so a whole hour draws at once.
//
// The processes act on frames [start, end) of the channels a mask picks
// (bit 0 the left, bit 1 the right...), the others kept as they are.

namespace cydaw::engine::foundry {

inline constexpr int summaryFrames = 256;
inline constexpr int maximumChannels = 8;

struct Block {
    std::vector<std::vector<float>> channels;
    // Per channel, per summaryFrames frames: the lowest and the highest value.
    std::vector<std::vector<std::pair<float, float>>> summary;

    [[nodiscard]] std::int64_t frames() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels.front().size());
    }
    [[nodiscard]] static std::shared_ptr<const Block> make(std::vector<std::vector<float>> channels);
};

struct Piece {
    std::shared_ptr<const Block> block;
    std::int64_t offset {};
    std::int64_t length {};
};

class Sound {
public:
    Sound() = default;
    Sound(std::vector<std::vector<float>> channels, double rate);
    // Silence.
    Sound(int channels, std::int64_t frames, double rate);

    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] double rate() const noexcept { return rate_; }
    [[nodiscard]] std::int64_t frames() const noexcept { return frames_; }
    [[nodiscard]] double seconds() const noexcept { return rate_ > 0.0 ? static_cast<double>(frames_) / rate_ : 0.0; }
    [[nodiscard]] const std::vector<Piece>& pieces() const noexcept { return pieces_; }

    // Frames [start, start + count) of one channel; silence out of the sound.
    // Allocates nothing (an audio callback may call it).
    void read(int channel, std::int64_t start, std::int64_t count, float* out) const noexcept;
    // The lowest and the highest value of a channel over [start, end).
    [[nodiscard]] std::pair<float, float> range(int channel, std::int64_t start, std::int64_t end) const noexcept;
    // Frames [start, end) as plain channels.
    [[nodiscard]] std::vector<std::vector<float>> render(std::int64_t start, std::int64_t end) const;

    // Edits: a new sound each time.
    [[nodiscard]] Sound excerpt(std::int64_t start, std::int64_t end) const;
    [[nodiscard]] Sound removed(std::int64_t start, std::int64_t end) const;
    [[nodiscard]] Sound cropped(std::int64_t start, std::int64_t end) const;
    // Another sound at `at` (its channels made ours: a mono one on every
    // channel, more channels mixed down). Its rate is taken as ours.
    [[nodiscard]] Sound inserted(std::int64_t at, const Sound& other) const;
    // Frames [start, end) replaced by another sound (any length).
    [[nodiscard]] Sound replaced(std::int64_t start, std::int64_t end, const Sound& other) const;
    // The same with plain channels (as many as ours).
    [[nodiscard]] Sound replaced(std::int64_t start, std::int64_t end, std::vector<std::vector<float>> channels) const;
    // Another sound mixed in from `at` (at `gain`), lengthening this one if
    // it goes past its end.
    [[nodiscard]] Sound mixedIn(std::int64_t at, const Sound& other, float gain) const;
    // Channels changed: `count` channels, each from a mix of ours
    // (`matrix[out][in]`).
    [[nodiscard]] Sound remixed(int count, const std::vector<std::vector<float>>& matrix) const;
    // The same frames at another rate (the sound stays as long).
    [[nodiscard]] Sound withRate(double rate) const;

private:
    void rebuild();
    [[nodiscard]] std::size_t pieceAt(std::int64_t frame) const noexcept;
    [[nodiscard]] std::vector<Piece> piecesOf(std::int64_t start, std::int64_t end) const;

    std::vector<Piece> pieces_;
    std::vector<std::int64_t> starts_;
    int channels_ { 2 };
    double rate_ { 48'000.0 };
    std::int64_t frames_ {};
};

// ---- Processes ---------------------------------------------------------------------------------

[[nodiscard]] inline bool inMask(const unsigned mask, const int channel) noexcept { return (mask >> channel) & 1u; }

// Frames [start, end) of the channels in `mask` handed to `process` (the
// others untouched), as one new block.
[[nodiscard]] Sound processed(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask,
                              const std::function<void(std::vector<float>& channel, int index)>& process);

enum class FadeCurve : int { linear, fast, slow, smooth };
// 0..1 along a fade in at `t` (0..1).
[[nodiscard]] float fadeShape(FadeCurve curve, float t) noexcept;

[[nodiscard]] Sound gained(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask, float gainDb);
// To a peak (dBFS) or an RMS level (dB), the channels together.
[[nodiscard]] Sound normalised(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask, float targetDb,
                               bool rms);
[[nodiscard]] Sound faded(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask, bool in, FadeCurve curve);
[[nodiscard]] Sound inverted(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask);
[[nodiscard]] Sound reversed(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask);
[[nodiscard]] Sound silenced(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask);
// The mean of each channel taken off.
[[nodiscard]] Sound centred(const Sound& sound, std::int64_t start, std::int64_t end, unsigned mask);
// Frames [start, end) at another speed and pitch (the rest as it is).
[[nodiscard]] Sound stretched(const Sound& sound, std::int64_t start, std::int64_t end, double speed, double pitch,
                              StretchAlgorithm algorithm);

struct Statistics {
    std::int64_t frames {};
    std::array<float, maximumChannels> peak {};   // linear
    std::array<double, maximumChannels> rms {};   // linear
    std::array<double, maximumChannels> dc {};    // the mean
    std::int64_t clipped {};                      // frames at full scale or over
    double loudness { -70.0 };                    // LUFS
};
[[nodiscard]] Statistics statistics(const Sound& sound, std::int64_t start, std::int64_t end);

// The average spectrum of [start, end) (all channels), `bins` levels in dB
// on a logarithmic scale from 20 Hz to the Nyquist frequency.
[[nodiscard]] std::vector<float> spectrum(const Sound& sound, std::int64_t start, std::int64_t end, int bins);

} // namespace cydaw::engine::foundry
