#pragma once

#include <cydaw/graph/GraphDefinition.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::graph {

enum class PrimitiveCategory : std::uint8_t {
    io,
    generator,
    voice,
    envelope,
    filter,
    dynamics,
    delay,
    mixer,
    midi,
    modulation,
    math,
    transport,
    analysis,
    utility
};

struct PrimitiveDescriptor {
    std::string key;
    std::string displayName;
    PrimitiveCategory category { PrimitiveCategory::utility };
    std::vector<GraphPort> ports;
    std::vector<NodeParameter> parameters;
    bool stateful {};
    bool supportsPolyphony {};
    bool breaksFeedbackCycle {};
    // Audio sockets remain exact on every GraphNode instance, but primitives
    // with this capability may be instantiated as Mono, Stereo or 3-8 ch.
    // This avoids a generic/untyped audio cable while allowing one DSP class
    // to be reused for each explicit channel topology.
    bool supportsVariableAudioChannels {};
};

class PrimitiveRegistry final {
public:
    [[nodiscard]] ValidationResult install(PrimitiveDescriptor descriptor,
                                           bool replaceExisting = false);
    [[nodiscard]] const PrimitiveDescriptor* find(
        const std::string& key) const noexcept;
    [[nodiscard]] std::vector<const PrimitiveDescriptor*> all() const;
    [[nodiscard]] std::vector<const PrimitiveDescriptor*> category(
        PrimitiveCategory category) const;
    [[nodiscard]] ValidationResult validateNode(
        const GraphNode& node) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, PrimitiveDescriptor> descriptors_;
};

[[nodiscard]] PrimitiveRegistry createFactoryPrimitiveRegistry();

// The same registry, built once and shared. It is immutable after
// construction, and it is consulted on every graph compilation and every
// validation pass: building a fresh copy each time cost two milliseconds of
// every track selection.
[[nodiscard]] const PrimitiveRegistry& factoryPrimitiveRegistry();
[[nodiscard]] const char* toString(PrimitiveCategory category) noexcept;

} // namespace cydaw::graph
