#include "Localization.h"
#include <CyDAWTranslations.h>
#include <cstdlib>
#include <cydaw/base/File.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void environment(const char *key, const base::Text &value) {
#if defined(_WIN32)
  _putenv_s(key, value.toRawUTF8());
#else
  if (value.isEmpty())
    unsetenv(key);
  else
    setenv(key, value.toRawUTF8(), 1);
#endif
}
int main() {
  auto profile = base::File::createTempFile("-localization");
  auto original = base::SystemStats::getEnvironmentVariable("APPDATA");
  auto originalConfig =
      base::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME");
  try {
    require(profile.createDirectory().wasOk(), "isolated settings directory");
    environment("APPDATA", profile.getFullPathName());
    environment("XDG_CONFIG_HOME", profile.getFullPathName());
    for (auto pair : {std::pair{"en", std::pair{translations::en_json,
                                                translations::en_jsonSize}},
                      std::pair{"fr", std::pair{translations::fr_json,
                                                translations::fr_jsonSize}}}) {
      auto source = base::File(base::Text(CYDAW_TRANSLATIONS_SOURCE))
                        .getChildFile(base::Text(pair.first) + ".json");
      base::MemoryBlock bytes;
      require(source.loadFileAsData(bytes), "resource source available");
      require(bytes.getSize() == std::size_t(pair.second.second) &&
                  std::memcmp(bytes.getData(), pair.second.first,
                              bytes.getSize()) == 0,
              "embedded translation bytes are exact");
      require(base::JSON::parse(
                  base::Text::fromUTF8(pair.second.first, pair.second.second))
                  .isObject(),
              "embedded translation parses without GUI");
    }
    auto &language = app::Localization::instance();
    require(language.setLanguage("en") && language.languageCode() == "en",
            "English selection");
    require(language.text("translation.missing.key") ==
                "translation.missing.key",
            "stable missing-key fallback");
    require(language.setLanguage("fr") && language.languageCode() == "fr",
            "French selection");
    require(!language.setLanguage("unsupported-language"),
            "unsupported locale is rejected");
    require(app::Localization::availableLanguages().size() >= 2,
            "built-in language inventory");
    environment("APPDATA", original);
    environment("XDG_CONFIG_HOME", originalConfig);
    require(profile.deleteRecursively(),
            "remove isolated localization settings");
    std::cout << "Exact embedded resources and native English/French selection "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    environment("APPDATA", original);
    environment("XDG_CONFIG_HOME", originalConfig);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
