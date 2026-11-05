#include "ManualContent.h"
#include <iostream>
#include <stdexcept>
using namespace cydaw::v2;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "manual source directory required");
        ManualContent content;
        require(content.load(juce::File(juce::String::fromUTF8(argv[1]))), "load real manual");
        require(content.error().isEmpty(), "manual has no loading error");
        require(content.topics("en").size() == 40 && content.topics("fr").size() == 40, "40 chapters in both languages");
        int sections = 0;
        for (const auto& topic : content.topics("en")) {
            const auto* translated = content.topic("fr", topic.id);
            require(translated && translated->sections.size() == topic.sections.size(), "section parity");
            require(topic.sections.size() >= 3, "chapter has overview and instructions");
            for (size_t i = 0; i < topic.sections.size(); ++i) {
                const auto& section = topic.sections[i];
                require(translated->sections[i].id == section.id, "stable translated section");
                require(section.markdown.trim().isNotEmpty() && translated->sections[i].markdown.trim().isNotEmpty(), "nonempty prose");
                for (const auto& figure : section.figures) require(figure.file.existsAsFile(), "figure exists");
                ++sections;
            }
        }
        require(sections >= 300, "full reference included");
        require(!content.search("en", "sidechain").empty(), "English full text search");
        require(!content.search("en", "master routing").empty(), "multiword search");
        const auto accents = content.search("fr", juce::String::fromUTF8("réverbération"));
        const auto ascii = content.search("fr", "reverberation");
        require(!accents.empty() && accents.size() == ascii.size(), "accent insensitive French search");
        for (size_t i=0; i<ascii.size(); ++i)
            require(accents[i].topic == ascii[i].topic && accents[i].section == ascii[i].section, "accent search ordering");
        require(content.search("en", "zxqvunfindable").empty() && content.search("fr", "   ").empty(), "empty and no-match searches");
        juce::String topic, section;
        require(content.resolve("mixing.md#sidechain", "welcome", topic, section) && topic == "mixing" && section == "sidechain", "chapter anchor");
        require(content.resolve("#sidechain", "mixing", topic, section), "local anchor");
        require(content.resolve("manual://mixing#sidechain", "welcome", topic, section), "manual URI");
        for (const auto* bad : {"../../setup.md", "file:///setup.md", "https://example.com", "mixing#unknown", "unknown.md", "mixing.md#../sidechain"})
            require(!content.resolve(bad, "welcome", topic, section), "unsafe or missing navigation rejected");
        const auto temporary = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("cydaw-manual-model", "", false);
        require(temporary.createDirectory().wasOk(), "create private malformed fixture");
        struct Cleanup { juce::File file; ~Cleanup() { file.deleteRecursively(); } } cleanup {temporary};
        ManualContent invalid;
        require(!invalid.load(temporary) && invalid.topics("en").empty(), "missing manifest");
        temporary.getChildFile("manifest.json").replaceWithText("{bad");
        require(!invalid.load(temporary) && invalid.error().isNotEmpty(), "malformed manifest");
        temporary.getChildFile("manifest.json").replaceWithText(R"({"format":"CyDAW Manual","schema":1,"topics":[{"id":"../escape"}]})");
        require(!invalid.load(temporary), "unsafe chapter identity");
        temporary.getChildFile("manifest.json").replaceWithText(R"({"format":"CyDAW Manual","schema":1,"topics":[{"id":"valid"}]})");
        temporary.getChildFile("en").createDirectory();
        temporary.getChildFile("en/valid.md").replaceWithText("# Test\n\nSome text\n\n## Broken {#../bad}\n");
        require(!invalid.load(temporary) && invalid.topics("en").empty(), "partial load rolled back");
        std::cout << "Manual model passed: 40 bilingual topics, " << sections << " sections per language\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
