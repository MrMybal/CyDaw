#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace cydaw::v2 {
struct ManualFigure { juce::String caption; juce::File file; };
struct ManualSection {
    juce::String id, title, markdown;
    std::vector<ManualFigure> figures;
};
struct ManualTopic {
    juce::String id, title, group;
    std::vector<ManualSection> sections;
};
struct ManualHit {
    juce::String topic, section, title, excerpt;
    int score {};
};
// Application-owned content, identity and search. No renderer or engine state.
class ManualContent {
public:
    bool load(const juce::File& root);
    const std::vector<ManualTopic>& topics(const juce::String& language) const;
    const ManualTopic* topic(const juce::String& language, const juce::String& id) const;
    const ManualSection* section(const juce::String& language, const juce::String& topic, const juce::String& id) const;
    std::vector<ManualHit> search(const juce::String& language, const juce::String& query) const;
    // Only local manual topic IDs/anchors are navigable. External links are
    // returned separately by the reader and opened only on an explicit click.
    bool resolve(const juce::String& target, const juce::String& current,
                 juce::String& topic, juce::String& section) const;
    const juce::String& error() const { return error_; }
    const juce::File& root() const { return root_; }
    static juce::String normalise(juce::String);
private:
    juce::File root_;
    juce::String error_;
    std::array<std::vector<ManualTopic>, 2> topics_;
};
} // namespace cydaw::v2
