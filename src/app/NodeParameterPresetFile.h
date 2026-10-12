#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace cydaw::app {

inline constexpr auto nodeParameterPresetFileExtension = ".cydawnode";
inline constexpr auto nodeParameterPresetFileFormat =
    "CyDAW Node Parameter Preset";
inline constexpr int nodeParameterPresetFileSchema = 1;
inline constexpr juce::int64 maximumNodeParameterPresetBytes = 512 * 1024;

struct NodeParameterPresetDocument {
    juce::String name;
    juce::String nodeTypeKey;
    juce::String processorKey;
    juce::String categoryKey;
    juce::String description;
    juce::StringArray tags;
    juce::var parameters;
    juce::var portDefaults;
};

[[nodiscard]] inline juce::var makeNodeParameterPresetDocument(
    NodeParameterPresetDocument document)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("format", nodeParameterPresetFileFormat);
    root->setProperty("schema", nodeParameterPresetFileSchema);
    root->setProperty("name", document.name.trim().substring(0, 128));
    root->setProperty("nodeType", document.nodeTypeKey.trim().substring(0, 192));
    root->setProperty("processor", document.processorKey.trim().substring(0, 192));
    root->setProperty("category", document.categoryKey.trim().substring(0, 192));
    root->setProperty("description",
                      document.description.trim().substring(0, 2'048));
    juce::Array<juce::var> tags;
    for (const auto& tag : document.tags)
        if (tag.trim().isNotEmpty() && tags.size() < 64)
            tags.add(tag.trim().substring(0, 64));
    root->setProperty("tags", std::move(tags));
    root->setProperty("parameters", document.parameters);
    root->setProperty("portDefaults", document.portDefaults);
    return juce::var(root.get());
}

[[nodiscard]] inline std::optional<NodeParameterPresetDocument>
parseNodeParameterPresetDocument(const juce::var& value)
{
    const auto* root = value.getDynamicObject();
    if (root == nullptr
        || root->getProperty("format").toString()
               != nodeParameterPresetFileFormat
        || static_cast<int>(root->getProperty("schema")) < 1
        || static_cast<int>(root->getProperty("schema"))
               > nodeParameterPresetFileSchema)
        return std::nullopt;

    NodeParameterPresetDocument document;
    document.name = root->getProperty("name").toString().trim();
    document.nodeTypeKey = root->getProperty("nodeType").toString().trim();
    document.processorKey = root->getProperty("processor").toString().trim();
    document.categoryKey = root->getProperty("category").toString().trim();
    document.description = root->getProperty("description").toString();
    document.parameters = root->getProperty("parameters");
    document.portDefaults = root->getProperty("portDefaults");
    if (const auto* tags = root->getProperty("tags").getArray())
        for (const auto& tag : *tags)
            if (document.tags.size() < 64)
                document.tags.add(tag.toString().substring(0, 64));

    if (document.name.isEmpty() || document.name.length() > 128
        || document.nodeTypeKey.isEmpty()
        || document.nodeTypeKey.length() > 192
        || document.processorKey.length() > 192
        || document.categoryKey.length() > 192
        || document.description.length() > 2'048
        || document.parameters.getDynamicObject() == nullptr
        || document.portDefaults.getArray() == nullptr
        || document.portDefaults.getArray()->size() > 128)
        return std::nullopt;

    return document;
}

[[nodiscard]] inline juce::File userNodeParameterPresetDirectory()
{
    return juce::File::getSpecialLocation(
               juce::File::userDocumentsDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Node Presets");
}

} // namespace cydaw::app
