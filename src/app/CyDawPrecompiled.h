#pragma once

// Precompiled header for the desktop shell.
//
// Every translation unit in src/app pulls in juce_gui_extra and juce_audio_utils,
// which between them are by far the largest thing the compiler parses: measured
// on MSVC, a 1600-line source file and a 19500-line one took 38.3 s and 40.6 s
// respectively, so the header parse — not the code — was setting the build time.
// Precompiling the JUCE and standard-library surface once per target makes the
// per-file cost proportional to the file again, which is also what makes it
// worthwhile to split the large components into focused translation units.
//
// Only headers that are stable belong here. CyDAW's own headers are deliberately
// absent: putting them in would mean every edit invalidates the precompiled
// header and rebuilds the whole target.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
