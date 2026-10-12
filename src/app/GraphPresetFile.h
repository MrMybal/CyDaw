#pragma once

#include <cydaw/graph/GraphDefinition.h>

#include <juce_core/juce_core.h>

#include <optional>

namespace cydaw::app {

inline constexpr auto graphPresetFileExtension = ".cydawgraph";
inline constexpr auto graphPresetFileFormat = "CyDAW Graph Preset";
inline constexpr int graphPresetFileSchema = 3;
inline constexpr int nodeGraphStateSchema = 29;
inline constexpr juce::int64 maximumGraphPresetBytes = 4 * 1024 * 1024;

struct GraphPresetMetadata {
    juce::String definitionId;
    cydaw::graph::SemanticVersion version;
    cydaw::graph::GraphRole role { cydaw::graph::GraphRole::generic };
    cydaw::graph::GraphScope scope { cydaw::graph::GraphScope::user };
    juce::StringArray tags;
    juce::String category;
    juce::String description;
    juce::String author;
};

struct GraphPresetDocument {
    juce::String name;
    juce::var graph;
    GraphPresetMetadata metadata;
};

[[nodiscard]] inline cydaw::graph::GraphRole graphRoleFromString(
    const juce::String& value)
{
    using Role = cydaw::graph::GraphRole;
    if (value == "audio-effect") return Role::audioEffect;
    if (value == "instrument") return Role::instrument;
    if (value == "midi-effect") return Role::midiEffect;
    if (value == "modulator") return Role::modulator;
    if (value == "analyzer") return Role::analyzer;
    if (value == "utility") return Role::utility;
    return Role::generic;
}

[[nodiscard]] inline cydaw::graph::GraphScope graphScopeFromString(
    const juce::String& value)
{
    using Scope = cydaw::graph::GraphScope;
    if (value == "factory") return Scope::factory;
    if (value == "project") return Scope::project;
    if (value == "inline-track") return Scope::inlineTrack;
    if (value == "inline-bus") return Scope::inlineBus;
    return Scope::user;
}

[[nodiscard]] inline std::optional<cydaw::graph::SemanticVersion>
parseGraphVersion(const juce::String& value)
{
    const auto parts = juce::StringArray::fromTokens(value, ".", "");
    if (parts.size() != 3)
        return std::nullopt;
    for (const auto& part : parts)
        if (part.isEmpty() || part.containsOnly("0123456789") == false)
            return std::nullopt;
    return cydaw::graph::SemanticVersion {
        static_cast<std::uint16_t>(juce::jlimit(0, 65535,
                                                parts[0].getIntValue())),
        static_cast<std::uint16_t>(juce::jlimit(0, 65535,
                                                parts[1].getIntValue())),
        static_cast<std::uint16_t>(juce::jlimit(0, 65535,
                                                parts[2].getIntValue()))
    };
}

[[nodiscard]] inline juce::var makeGraphPresetDocument(
    const juce::String& name, const juce::var& graph,
    GraphPresetMetadata metadata = {})
{
    const auto safeName = name.trim().substring(0, 128);
    if (metadata.definitionId.isEmpty()) {
        auto slug = juce::File::createLegalFileName(safeName)
                        .toLowerCase().replaceCharacter(' ', '-');
        while (slug.contains("--"))
            slug = slug.replace("--", "-");
        metadata.definitionId = "user." + slug.trimCharactersAtStart("-")
            .trimCharactersAtEnd("-");
    }
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("format", graphPresetFileFormat);
    root->setProperty("schema", graphPresetFileSchema);
    root->setProperty("name", safeName);
    root->setProperty("definitionId", metadata.definitionId.substring(0, 192));
    root->setProperty("version", juce::String(metadata.version.toString()));
    root->setProperty("role", cydaw::graph::toString(metadata.role));
    root->setProperty("scope", cydaw::graph::toString(metadata.scope));
    root->setProperty("category", metadata.category.trim().substring(0, 192));
    root->setProperty("description",
                      metadata.description.trim().substring(0, 2'048));
    root->setProperty("author", metadata.author.trim().substring(0, 128));
    juce::Array<juce::var> tags;
    for (const auto& tag : metadata.tags)
        if (tag.trim().isNotEmpty() && tags.size() < 64)
            tags.add(tag.trim().substring(0, 64));
    root->setProperty("tags", tags);
    root->setProperty("graph", graph);
    return juce::var(root.get());
}

[[nodiscard]] inline std::optional<GraphPresetDocument>
parseGraphPresetDocument(const juce::var& value)
{
    const auto* root = value.getDynamicObject();
    if (root == nullptr
        || root->getProperty("format").toString() != graphPresetFileFormat)
        return std::nullopt;
    const auto schema = static_cast<int>(root->getProperty("schema"));
    if (schema < 1 || schema > graphPresetFileSchema)
        return std::nullopt;
    const auto name = root->getProperty("name").toString().trim();
    const auto graph = root->getProperty("graph");
    const auto* graphObject = graph.getDynamicObject();
    if (name.isEmpty() || name.length() > 128 || graphObject == nullptr)
        return std::nullopt;
    const auto graphSchema = static_cast<int>(
        graphObject->getProperty("schema"));
    if (graphSchema < 1 || graphSchema > nodeGraphStateSchema)
        return std::nullopt;
    GraphPresetMetadata metadata;
    if (schema >= 2) {
        metadata.definitionId = root->getProperty("definitionId").toString();
        const auto version = parseGraphVersion(
            root->getProperty("version").toString());
        if (metadata.definitionId.isEmpty() || metadata.definitionId.length() > 192
            || !version.has_value())
            return std::nullopt;
        metadata.version = *version;
        metadata.role = graphRoleFromString(
            root->getProperty("role").toString());
        metadata.scope = graphScopeFromString(
            root->getProperty("scope").toString());
        if (const auto* tags = root->getProperty("tags").getArray())
            for (const auto& tag : *tags)
                if (metadata.tags.size() < 64)
                    metadata.tags.add(tag.toString().substring(0, 64));
        if (schema >= 3) {
            metadata.category = root->getProperty("category").toString()
                                    .trim().substring(0, 192);
            metadata.description = root->getProperty("description").toString()
                                       .trim().substring(0, 2'048);
            metadata.author = root->getProperty("author").toString()
                                  .trim().substring(0, 128);
        } else {
            metadata.description = graphObject
                ->getProperty("graphDescription").toString()
                .trim().substring(0, 2'048);
        }
    } else {
        auto slug = name.toLowerCase().replaceCharacter(' ', '-');
        metadata.definitionId = "legacy." + slug;
        metadata.scope = cydaw::graph::GraphScope::user;
    }
    return GraphPresetDocument { name, graph, std::move(metadata) };
}

[[nodiscard]] inline juce::File userGraphPresetDirectory()
{
    return juce::File::getSpecialLocation(
               juce::File::userDocumentsDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Graphs");
}

} // namespace cydaw::app
