#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <unordered_map>
#include <vector>

namespace cydaw::app {

// One for the program, made on first use and deleted when JUCE shuts down
// (not when the module is unloaded: a plug-in's would then be destroyed
// after JUCE, under the loader's lock, and its host would hang).
class Localization final : public juce::ChangeBroadcaster, private juce::DeletedAtShutdown {
public:
    static Localization& instance();
    ~Localization() override;
    JUCE_DECLARE_SINGLETON(Localization, false)

    [[nodiscard]] juce::String text(juce::StringRef key) const;
    // The same in English, whatever the language (what a module shows).
    [[nodiscard]] juce::String englishText(juce::StringRef key) const;
    [[nodiscard]] juce::String languageCode() const;
    [[nodiscard]] bool setLanguage(const juce::String& languageCode);
    // The languages there are: English and French built in, then every
    // i18n/<code>.json beside the program (its "language.name" names it).
    struct Language {
        juce::String code;
        juce::String name;
    };
    [[nodiscard]] static std::vector<Language> availableLanguages();

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
    Mappings english_;
    Mappings current_;
    juce::ApplicationProperties properties_;
    mutable juce::ReadWriteLock mappingsLock_;
};

[[nodiscard]] juce::String tr(juce::StringRef key);
[[nodiscard]] juce::String tr(juce::StringRef key, juce::StringRef token, const juce::String& value);

} // namespace cydaw::app
