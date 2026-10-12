#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Electronic drums from recipes: what a generated sound bank holds. A recipe
// names a kind (kick, snare, clap, hat, cymbal, tom, rim, cowbell, shaker,
// tambourine, conga, zap, sweep, impact) and gives some of its values by
// name; the others take the kind's defaults. A kick is Kick's own engine
// (KickSynth.h, its keys). The same recipe gives the same sound, sample for
// sample: noise comes from the recipe's seed.

namespace cydaw::engine::drums {

struct Recipe {
    std::string kind;
    std::vector<std::pair<std::string, double>> values;
    std::uint32_t seed { 1 };
    // Its length; 0: the kind's own.
    double seconds {};
    // After it is brought to -1 dBFS at its peak.
    double gainDb {};
};

// Mono, at the rate given; empty for a kind not known.
[[nodiscard]] std::vector<float> render(const Recipe& recipe, double sampleRate);

// The kinds a recipe can name.
[[nodiscard]] const std::vector<std::string>& kinds();

} // namespace cydaw::engine::drums
