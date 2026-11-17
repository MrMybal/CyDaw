#include <cydaw/base/File.h>
#pragma once

#include <cydaw/base/Utilities.h>

#include <optional>

namespace cydaw::app {

inline constexpr auto nodeParameterPresetFileExtension = ".cydawnode";
inline constexpr auto nodeParameterPresetFileFormat =
    "CyDAW Node Parameter Preset";
inline constexpr int nodeParameterPresetFileSchema = 1;
inline constexpr std::int64_t maximumNodeParameterPresetBytes = 512 * 1024;

struct NodeParameterPresetDocument {
    cydaw::base::Text name;
    cydaw::base::Text nodeTypeKey;
    cydaw::base::Text processorKey;
    cydaw::base::Text categoryKey;
    cydaw::base::Text description;
    cydaw::base::TextList tags;
    cydaw::base::Value parameters;
    cydaw::base::Value portDefaults;
};

[[nodiscard]] inline cydaw::base::Value makeNodeParameterPresetDocument(
    NodeParameterPresetDocument document)
{
    auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
    root->setProperty("format", nodeParameterPresetFileFormat);
    root->setProperty("schema", nodeParameterPresetFileSchema);
    root->setProperty("name", document.name.trim().substring(0, 128));
    root->setProperty("nodeType", document.nodeTypeKey.trim().substring(0, 192));
    root->setProperty("processor", document.processorKey.trim().substring(0, 192));
    root->setProperty("category", document.categoryKey.trim().substring(0, 192));
    root->setProperty("description",
                      document.description.trim().substring(0, 2'048));
    cydaw::base::Array<cydaw::base::Value> tags;
    for (const auto& tag : document.tags)
        if (tag.trim().isNotEmpty() && tags.size() < 64)
            tags.add(tag.trim().substring(0, 64));
    root->setProperty("tags", std::move(tags));
    root->setProperty("parameters", document.parameters);
    root->setProperty("portDefaults", document.portDefaults);
    return cydaw::base::Value(root.get());
}

[[nodiscard]] inline std::optional<NodeParameterPresetDocument>
parseNodeParameterPresetDocument(const cydaw::base::Value& value)
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

[[nodiscard]] inline cydaw::base::File userNodeParameterPresetDirectory()
{
    return cydaw::base::File::getSpecialLocation(
               cydaw::base::File::userDocumentsDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Node Presets");
}

} // namespace cydaw::app
