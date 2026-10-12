#pragma once

#include <juce_core/juce_core.h>

namespace cydaw::app {

enum class AudioEditOperation {
    fadeIn,
    fadeOut,
    silence,
    reverse,
    amplify,
    normalisePeak,
    removeDcOffset,
    invertPolarity,
    swapStereo,
    // These change the length: the selection taken out, the selection kept
    // alone, silence as long as the selection put in at its start, another
    // stretch of sound (paste) put in there.
    cut,
    crop,
    insertSilence,
    paste
};

[[nodiscard]] inline bool changesLength(const AudioEditOperation operation) noexcept
{
    return operation == AudioEditOperation::cut || operation == AudioEditOperation::crop
           || operation == AudioEditOperation::insertSilence || operation == AudioEditOperation::paste;
}

// A fade's shape: straight, equal power (a sine quarter), slow start
// (exponential, the ear's "smooth" fade in), fast start (logarithmic).
enum class FadeCurve { linear, equalPower, slowStart, fastStart };

struct AudioEditRequest final {
    AudioEditOperation operation { AudioEditOperation::silence };
    float levelDb {};
    FadeCurve curve { FadeCurve::linear };
    // paste: what is put in, a stretch of a file (this one or another).
    juce::File pasteFile;
    double pasteStartSeconds {};
    double pasteDurationSeconds {};
};

} // namespace cydaw::app
