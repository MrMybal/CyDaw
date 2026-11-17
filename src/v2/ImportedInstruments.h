#include <cydaw/base/Text.h>
#pragma once

#include "ProjectImport.h"


#include <string>
#include <utility>
#include <vector>

// The instrument of CyDAW's that stands for another application's own one in
// an imported project (its polysynth, its FM synth, its organ…), and its
// settings carried over where they are known: the other application's names
// and units turned into the model's keys and units, as near as they go.

namespace cydaw::v2::imported {

struct Equivalent {
    // The factory instrument to put on the track ("opale-instrument").
    cydaw::base::Text instrument;
    // Then, if it is not there yet (a model still to come), this one.
    cydaw::base::Text fallback;
    // Its synth card's settings, by the model's keys; a choice by its index.
    std::vector<std::pair<std::string, float>> settings;
};

// What stands for `device` (an instrument of `format`'s own: "bitwig",
// "ableton", "dawproject"); empty when it is a plug-in or not an instrument.
[[nodiscard]] std::optional<Equivalent> equivalentOf(const app::project_import::ImportedDevice& device,
                                                     const cydaw::base::Text& format);

} // namespace cydaw::v2::imported
