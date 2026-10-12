#pragma once

#include <array>
#include <cstdint>

namespace cydaw::generated_graph {

struct ParameterConfig {
    std::uint32_t stableId {};
    const char* name {};
    const char* unit {};
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    bool automatable { true };
};

// The normal CyDAW Graph Runtime is an editable development plug-in. A
// generated wrapper supplies another header with the same contract before this
// directory in the include path, giving it an exact identity, parameter list,
// bus format and embedded initial Graph state.
inline constexpr bool exactWrapper = false;
inline constexpr char productName[] = "CyDAW Graph Runtime";
inline constexpr std::array<const char*, 1> embeddedStateJsonChunks {{ "" }};
inline constexpr int mainInputChannels = 2;
inline constexpr int mainOutputChannels = 2;
inline constexpr char mainInputName[] = "Input";
inline constexpr char mainOutputName[] = "Output";
inline constexpr bool acceptsMidi = true;
inline constexpr bool producesMidi = false;
inline constexpr std::array<ParameterConfig, 0> parameters {};

} // namespace cydaw::generated_graph
