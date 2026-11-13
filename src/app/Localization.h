#include <cydaw/base/Properties.h>
#include <cydaw/base/Text.h>
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

    [[nodiscard]] cydaw::base::Text text(cydaw::base::Text key) const;
    // The same in English, whatever the language (what a module shows).
    [[nodiscard]] cydaw::base::Text englishText(cydaw::base::Text key) const;
    [[nodiscard]] cydaw::base::Text languageCode() const;
    [[nodiscard]] bool setLanguage(const cydaw::base::Text& languageCode);
    // The languages there are: English and French built in, then every
    // i18n/<code>.json beside the program (its "language.name" names it).
    struct Language {
        cydaw::base::Text code;
        cydaw::base::Text name;
    };
    [[nodiscard]] static std::vector<Language> availableLanguages();

private:
    // Keyed by exact key: tr() is called thousands of times when a surface
    // of cards is rebuilt, and a linear, case-folding scan of every string
    // in the language (what StringPairArray does) was a visible part of
    // every change of view.
    using Mappings = std::unordered_map<cydaw::base::Text, cydaw::base::Text>;
    Localization();
    [[nodiscard]] Mappings loadLanguage(const cydaw::base::Text& languageCode) const;
    [[nodiscard]] static Mappings parseJson(const cydaw::base::Text& json);

    cydaw::base::Text languageCode_ { "en" };
    Mappings english_;
    Mappings current_;
    cydaw::base::ApplicationProperties properties_;
    mutable juce::ReadWriteLock mappingsLock_;
};

[[nodiscard]] cydaw::base::Text tr(cydaw::base::Text key);
[[nodiscard]] cydaw::base::Text tr(cydaw::base::Text key, cydaw::base::Text token, const cydaw::base::Text& value);

} // namespace cydaw::app
