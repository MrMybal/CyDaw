#pragma once

#include <array>
#include <cstddef>
#include <limits>

namespace cydaw::engine {

// What an instrument's voice plays at this moment, for an interface to show
// (Prisme, the synth family): each setting its own modulations move, where
// they put it, in the setting's own units (NaN: where its knob is); its LFOs
// (their value -1..1 and where they are in their cycle 0..1) and its
// envelopes (0..1). NaN where it has none.
struct VoiceLive {
    static constexpr std::size_t valueCount = 512;
    static constexpr std::size_t lfoCount = 4;
    static constexpr std::size_t envelopeCount = 4;
    std::array<float, valueCount> values {};
    std::array<float, lfoCount> lfos {};
    std::array<float, lfoCount> lfoPhases {};
    std::array<float, envelopeCount> envelopes {};

    void clear() noexcept
    {
        constexpr auto none = std::numeric_limits<float>::quiet_NaN();
        values.fill(none);
        lfos.fill(none);
        lfoPhases.fill(none);
        envelopes.fill(none);
    }
};

} // namespace cydaw::engine
