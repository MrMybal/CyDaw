#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <unordered_map>
#include <vector>

namespace cydaw::app {

class Localization final : public juce::ChangeBroadcaster {
public:
    static Localization& instance();

    [[nodiscard]] juce::String text(juce::StringRef key) const;
    [[nodiscard]] juce::String languageCode() const;
    [[nodiscard]] bool setLanguage(const juce::String& languageCode);
    // The languages there are: English and French built in, then every
    // i18n/<code>.json beside the program (its "language.name" names it).
    struct Language {
        juce::String code;
        juce::String name;
    };
    [[nodiscard]] static std::vector<Language> availableLanguages();
    // Node names ("node.<name>", the graph's inputs and outputs) stay in
    // English unless asked otherwise: the names of a node graph, as every
    // modular tool writes them. Their descriptions and settings follow the
    // language.
    [[nodiscard]] bool nodeNamesInEnglish() const;
    void setNodeNamesInEnglish(bool english);

private:
    // Keyed by exact key: tr() is called thousands of times when a surface
    // of cards is rebuilt, and a linear, case-folding scan of every string
    // in the language (what StringPairArray does) was a visible part of
    // every change of view.
    using Mappings = std::unordered_map<juce::String, juce::String>;
    Localization();
    [[nodiscard]] Mappings loadLanguage(const juce::String& languageCode) const;
    [[nodiscard]] static Mappings parseJson(const juce::String& json);

    juce::String languageCode_ { "en" };
    bool nodeNamesInEnglish_ { true };
    Mappings english_;
    Mappings current_;
    juce::ApplicationProperties properties_;
    mutable juce::ReadWriteLock mappingsLock_;
};

[[nodiscard]] juce::String tr(juce::StringRef key);
[[nodiscard]] juce::String tr(juce::StringRef key, juce::StringRef token, const juce::String& value);

} // namespace cydaw::app
