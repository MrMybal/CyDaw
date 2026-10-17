#pragma once

#include <cstdint>

// A sound's loudness as ITU-R BS.1770 measures it: K-weighted (a high shelf
// then a high-pass), mean squares over 400 ms blocks overlapping by 75 %,
// blocks under -70 LUFS left out, then those more than 10 LU under the rest.
// Side channels (5th and 6th of six) weigh 1.41, the LFE nothing; up to two
// channels, every channel weighs 1.

namespace cydaw::engine {

// Integrated loudness in LUFS; -70 or less for silence (and shorter than a
// block: what there is, as one block).
[[nodiscard]] double integratedLoudness(const float* const* channels, int channelCount, std::int64_t frames,
                                        double sampleRate);

} // namespace cydaw::engine
