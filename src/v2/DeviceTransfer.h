#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace cydaw::v2::device_transfer {

// Keep the transitive closure of module classes actually used by this graph.
// Local copies take precedence over the receiving installation's library.
inline juce::var portableGraph(const juce::var& source)
{
    auto graph = source.clone();
    auto* root = graph.getDynamicObject();
    if (root == nullptr)
        return graph;
    std::map<juce::String, juce::var> available;
    if (const auto* modules = root->getProperty("modules").getArray())
        for (const auto& module : *modules)
            available[module.getProperty("key", {}).toString()] = module;
    std::set<juce::String> needed;
    juce::Array<juce::var> kept;
    const auto visit = [&](const auto& self, const juce::var& state) -> void {
        if (const auto* nodes = state.getProperty("nodes", {}).getArray())
            for (const auto& node : *nodes) {
                const auto key = node.getProperty("type", {}).toString();
                const auto found = available.find(key);
                if (found != available.end() && needed.insert(key).second) {
                    auto module = found->second;
                    module.getDynamicObject()->setProperty("inline", true);
                    kept.add(module);
                    self(self, module);
                }
            }
        const auto parent = state.getProperty("parentClassKey", {}).toString();
        if (const auto found = available.find(parent); found != available.end() && needed.insert(parent).second) {
            auto module = found->second;
            module.getDynamicObject()->setProperty("inline", true);
            kept.add(module);
            self(self, module);
        }
    };
    visit(visit, graph);
    root->setProperty("modules", kept);
    return graph;
}

// Binary banks / recordings are not silently claimed to be part of a graph.
// Tables, impulse data, interface code and models already stored in the JSON
// remain embedded. Ignore descriptive source text and C++ source code.
inline void externalFiles(const juce::var& state, juce::StringArray& files)
{
    if (const auto* object = state.getDynamicObject()) {
        for (const auto& property : object->getProperties()) {
            const auto key = property.name.toString();
            if (property.value.isString() && (key == "file" || key == "path" || key == "filePath" || key == "samplePath")) {
                const auto path = property.value.toString();
                if (path.isNotEmpty())
                    files.addIfNotAlreadyThere(path);
            } else if (property.value.isObject() || property.value.isArray()) {
                externalFiles(property.value, files);
            }
        }
    } else if (const auto* list = state.getArray()) {
        for (const auto& child : *list)
            externalFiles(child, files);
    }
}

} // namespace cydaw::v2::device_transfer
