#include <cydaw/base/Properties.h>
#include <cydaw/base/File.h>
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
    cydaw::base::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".settings";
    options.folderName = "CyDAW";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);
    const auto savedLanguage = properties_.getUserSettings()->getValue("language", "en");
    languageCode_ = savedLanguage.isNotEmpty() ? cydaw::base::Text(savedLanguage) : cydaw::base::Text("en");
    current_ = loadLanguage(languageCode_);
    if (current_.empty()) {
        languageCode_ = "en";
        current_ = english_;
    }
}

namespace {
// A node's name: "node.<name>", and the graph's audio inputs and outputs.
bool isNodeName(const cydaw::base::Text& key)
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
bool isModuleText(const cydaw::base::Text& key)
{
    return isNodeName(key) || key.startsWith("effect.parameter.") || key.startsWith("node.parameter.")
           || key.startsWith("sampler.parameter.") || key.startsWith("model.parameter.") || key.startsWith("module.ui.")
           || key.startsWith("modulator.parameter.") || key.startsWith("container.parameter.");
}
} // namespace

cydaw::base::Text Localization::text(const cydaw::base::Text key) const
{
    const cydaw::base::Text keyText(key);
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

cydaw::base::Text Localization::englishText(const cydaw::base::Text key) const
{
    const cydaw::base::Text keyText(key);
    const juce::ScopedReadLock lock(mappingsLock_);
    if (const auto english = english_.find(keyText); english != english_.end() && english->second.isNotEmpty())
        return english->second;
    return keyText;
}

cydaw::base::Text Localization::languageCode() const
{
    const juce::ScopedReadLock lock(mappingsLock_);
    return languageCode_;
}

std::vector<Localization::Language> Localization::availableLanguages()
{
    std::vector<Language> languages { { "fr", cydaw::base::Text::fromUTF8("Fran\xc3\xa7" "ais") }, { "en", "English" } };
    const auto folder = cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile).getSiblingFile("i18n");
    for (const auto& file : folder.findChildFiles(cydaw::base::File::findFiles, false, "*.json")) {
        const auto code = file.getFileNameWithoutExtension();
        if (code == "en" || code == "fr" || code.isEmpty() || file.getSize() > 8 * 1024 * 1024)
            continue;
        const auto parsed = cydaw::base::JSON::parse(file);
        auto name = parsed.getProperty("language.name", cydaw::base::Value()).toString();
        languages.push_back({ code, name.isNotEmpty() ? name : cydaw::base::Text(code) });
    }
    return languages;
}

bool Localization::setLanguage(const cydaw::base::Text& languageCode)
{
    // English and French are built in; another language needs its file.
    if (languageCode != "en" && languageCode != "fr"
        && !cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile)
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
    properties_.getUserSettings()->setValue("language", cydaw::base::Value(languageCode_));
    properties_.saveIfNeeded();
    sendChangeMessage();
    return true;
}

Localization::Mappings Localization::loadLanguage(const cydaw::base::Text& languageCode) const
{
    const auto externalFile = cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile)
                                  .getSiblingFile("i18n")
                                  .getChildFile(languageCode + ".json");
    if (externalFile.existsAsFile()) {
        auto mappings = parseJson(externalFile.loadFileAsString());
        if (!mappings.empty())
            return mappings;
    }

    if (languageCode == "fr")
        return parseJson(cydaw::base::Text::fromUTF8(cydaw::translations::fr_json, cydaw::translations::fr_jsonSize));
    return parseJson(cydaw::base::Text::fromUTF8(cydaw::translations::en_json, cydaw::translations::en_jsonSize));
}

Localization::Mappings Localization::parseJson(const cydaw::base::Text& json)
{
    Mappings result;
    const auto parsed = cydaw::base::JSON::parse(json);
    if (const auto* object = parsed.getDynamicObject()) {
        const auto& properties = object->getProperties();
        result.reserve(static_cast<std::size_t>(properties.size()));
        for (int index = 0; index < properties.size(); ++index)
            result.insert_or_assign(properties.getName(index).toString(),
                                    properties.getValueAt(index).toString());
    }
    return result;
}

cydaw::base::Text tr(const cydaw::base::Text key)
{
    return Localization::instance().text(key);
}

cydaw::base::Text tr(const cydaw::base::Text key, const cydaw::base::Text token, const cydaw::base::Text& value)
{
    return tr(key).replace(cydaw::base::Text("{") + cydaw::base::Text(token) + "}", value);
}

} // namespace cydaw::app
