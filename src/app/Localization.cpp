#include "Localization.h"

#include <CyDAWTranslations.h>

namespace cydaw::app {

JUCE_IMPLEMENT_SINGLETON(Localization)

Localization& Localization::instance()
{
    return *getInstance();
}

Localization::~Localization()
{
    clearSingletonInstance();
}

Localization::Localization()
{
    english_ = loadLanguage("en");
    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".settings";
    options.folderName = "CyDAW";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);
    const auto savedLanguage = properties_.getUserSettings()->getValue("language", "en");
    languageCode_ = savedLanguage.isNotEmpty() ? savedLanguage : juce::String("en");
    current_ = loadLanguage(languageCode_);
    if (current_.empty()) {
        languageCode_ = "en";
        current_ = english_;
    }
}

namespace {
// A node's name: "node.<name>", and the graph's audio inputs and outputs.
bool isNodeName(const juce::String& key)
{
    if (!key.startsWith("node."))
        return false;
    if (key.startsWith("node.graphAudioInput.") || key.startsWith("node.graphAudioOutput."))
        return true;
    return key.indexOfChar(5, '.') < 0;
}

// What is inside a module, in English whatever the language: its name, its
// settings' names, its interface's controls (a module reads the same in a
// tutorial from anywhere; CyDAW's own interface is what gets translated).
bool isModuleText(const juce::String& key)
{
    return isNodeName(key) || key.startsWith("effect.parameter.") || key.startsWith("node.parameter.")
           || key.startsWith("sampler.parameter.") || key.startsWith("model.parameter.") || key.startsWith("module.ui.")
           || key.startsWith("modulator.parameter.") || key.startsWith("container.parameter.");
}
} // namespace

juce::String Localization::text(const juce::StringRef key) const
{
    const juce::String keyText(key.text);
    const juce::ScopedReadLock lock(mappingsLock_);
    if (languageCode_ != "en" && isModuleText(keyText))
        if (const auto english = english_.find(keyText); english != english_.end() && english->second.isNotEmpty())
            return english->second;
    if (const auto translated = current_.find(keyText);
        translated != current_.end() && translated->second.isNotEmpty())
        return translated->second;
    if (const auto fallback = english_.find(keyText);
        fallback != english_.end() && fallback->second.isNotEmpty())
        return fallback->second;
    return keyText;
}

juce::String Localization::languageCode() const
{
    const juce::ScopedReadLock lock(mappingsLock_);
    return languageCode_;
}

std::vector<Localization::Language> Localization::availableLanguages()
{
    std::vector<Language> languages { { "fr", juce::String::fromUTF8("Fran\xc3\xa7" "ais") }, { "en", "English" } };
    const auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("i18n");
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto code = file.getFileNameWithoutExtension();
        if (code == "en" || code == "fr" || code.isEmpty() || file.getSize() > 8 * 1024 * 1024)
            continue;
        const auto parsed = juce::JSON::parse(file);
        auto name = parsed.getProperty("language.name", juce::var()).toString();
        languages.push_back({ code, name.isNotEmpty() ? name : code });
    }
    return languages;
}

bool Localization::setLanguage(const juce::String& languageCode)
{
    // English and French are built in; another language needs its file.
    if (languageCode != "en" && languageCode != "fr"
        && !juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                .getSiblingFile("i18n")
                .getChildFile(languageCode + ".json")
                .existsAsFile())
        return false;
    auto mappings = loadLanguage(languageCode);
    if (mappings.empty())
        return false;
    {
        const juce::ScopedWriteLock lock(mappingsLock_);
        languageCode_ = languageCode;
        current_ = std::move(mappings);
    }
    properties_.getUserSettings()->setValue("language", languageCode_);
    properties_.saveIfNeeded();
    sendChangeMessage();
    return true;
}

Localization::Mappings Localization::loadLanguage(const juce::String& languageCode) const
{
    const auto externalFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getSiblingFile("i18n")
                                  .getChildFile(languageCode + ".json");
    if (externalFile.existsAsFile()) {
        auto mappings = parseJson(externalFile.loadFileAsString());
        if (!mappings.empty())
            return mappings;
    }

    if (languageCode == "fr")
        return parseJson(juce::String::fromUTF8(cydaw::translations::fr_json, cydaw::translations::fr_jsonSize));
    return parseJson(juce::String::fromUTF8(cydaw::translations::en_json, cydaw::translations::en_jsonSize));
}

Localization::Mappings Localization::parseJson(const juce::String& json)
{
    Mappings result;
    const auto parsed = juce::JSON::parse(json);
    if (const auto* object = parsed.getDynamicObject()) {
        const auto& properties = object->getProperties();
        result.reserve(static_cast<std::size_t>(properties.size()));
        for (int index = 0; index < properties.size(); ++index)
            result.insert_or_assign(properties.getName(index).toString(),
                                    properties.getValueAt(index).toString());
    }
    return result;
}

juce::String tr(const juce::StringRef key)
{
    return Localization::instance().text(key);
}

juce::String tr(const juce::StringRef key, const juce::StringRef token, const juce::String& value)
{
    return tr(key).replace(juce::String("{") + juce::String(token.text) + "}", value);
}

} // namespace cydaw::app
