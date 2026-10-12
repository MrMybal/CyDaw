#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::modules {

using ModuleId = std::uint64_t;
using ParameterId = std::uint64_t;

enum class ModuleCategory : std::uint8_t { instrument, audioEffect, midiEffect, modulator, analyzer, utility, buildingBlock };
enum class InterfaceElementKind : std::uint8_t { knob, fader, toggle, selector, xyPad, envelope, keyboard, waveform, spectrum, levelMeter, text, group };

struct PublishedParameter {
    ParameterId id {};
    std::string key;
    std::string displayName;
    std::string unit;
    double minimum {};
    double maximum { 1.0 };
    double defaultValue {};
    std::uint64_t targetNodeId {};
    std::string targetParameterKey;
};

struct InterfaceElement {
    std::uint64_t id {};
    InterfaceElementKind kind { InterfaceElementKind::knob };
    std::string label;
    std::optional<ParameterId> parameterBinding;
    float x {};
    float y {};
    float width { 1.0f };
    float height { 1.0f };
};

struct ModuleDefinition {
    ModuleId id {};
    std::string slug;
    std::string displayName;
    ModuleCategory category { ModuleCategory::buildingBlock };
    std::uint32_t schemaVersion { 1 };
    std::vector<ModuleId> dependencies;
    std::vector<PublishedParameter> parameters;
    std::vector<InterfaceElement> interface;
};

enum class ModuleError : std::uint8_t { none, invalidId, duplicateId, duplicateSlug, missingDependency, cyclicDependency, invalidParameterRange, missingParameterBinding };

struct ModuleResult {
    ModuleError error { ModuleError::none };
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept { return error == ModuleError::none; }
};

struct CompiledModuleLibrary { std::vector<ModuleId> loadOrder; };

class ModuleLibrary final {
public:
    [[nodiscard]] ModuleResult add(ModuleDefinition module);
    [[nodiscard]] ModuleResult remove(ModuleId id);
    [[nodiscard]] ModuleResult validate() const;
    [[nodiscard]] std::optional<CompiledModuleLibrary> compile() const;
    [[nodiscard]] const ModuleDefinition* find(ModuleId id) const noexcept;
    [[nodiscard]] const ModuleDefinition* findBySlug(const std::string& slug) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<ModuleId, ModuleDefinition> modules_;
    std::unordered_map<std::string, ModuleId> slugs_;
};

} // namespace cydaw::modules

