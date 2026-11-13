#include <cydaw/base/Properties.h>
#include <cydaw/base/Value.h>
#include <cydaw/document/ModuleInterface.h>
#include <cydaw/document/NativeGraphState.h>
#include <cydaw/document/ProjectRecovery.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool pass, const char *text) {
  if (!pass)
    throw std::runtime_error(text);
}
int main() {
  try {
    Text words = Text::fromUTF8("Fran\xc3\xa7"
                                "ais \xf0\x9f\x8e\xac");
    require(words.length() == 10, "UTF-8 codepoints");
    require(words.substring(4, 5) == Text::fromUTF8("\xc3\xa7"),
            "UTF-8 slicing");
    require(Text(words.toWideString().c_str()) == words, "UTF-16/32 roundtrip");
    auto tokens = TextList::fromTokens("one \"two three\" four", true);
    require(tokens.size() == 3 && tokens[1].unquoted() == "two three",
            "quoted launch arguments");
    Value original;
    require(JSON::parse("{\"id\":9223372036854775807,\"name\":\"é🎬\","
                        "\"values\":[true,null,-1,0.125]}",
                        original)
                .wasOk(),
            "JSON fixture");
    require(int64(original.getProperty("id")) == INT64_MAX,
            "64-bit identity preserved");
    auto clone = original.clone();
    clone.getDynamicObject()->setProperty("name", "changed");
    require(original.getProperty("name").toString() !=
                clone.getProperty("name").toString(),
            "deep document clone");
    auto serialized = JSON::toString(original, true);
    require(JSON::parse(serialized).getProperty("name").toString() ==
                original.getProperty("name").toString(),
            "Unicode serialization");
    Value unchanged = original;
    require(JSON::parse("{broken", unchanged).failed() &&
                unchanged.getDynamicObject() == original.getDynamicObject(),
            "invalid JSON retains destination");
    cydaw::engine::NativeDspGraph graph;
    graph.outputNodeId = 3;
    graph.nodes.push_back({1, cydaw::engine::NativeDspNodeKind::input, {}});
    graph.nodes.push_back({2, cydaw::engine::NativeDspNodeKind::gain, {}});
    graph.nodes.back().parameters.gainDb = -6;
    graph.nodes.push_back({3, cydaw::engine::NativeDspNodeKind::output, {}});
    graph.connections = {{1, 2, 0, 0}, {2, 3, 0, 0}};
    auto state = cydaw::document::serialiseNativeDspGraphState(graph);
    Text error;
    auto restored = cydaw::document::restoreNativeDspGraphState(
        JSON::parse(JSON::toString(state, true)), error);
    require(bool(restored) && error.isEmpty() && restored->nodes.size() == 3 &&
                restored->nodes[1].parameters.gainDb == -6,
            "actual DSP graph document roundtrip without JUCE");
    state.getDynamicObject()->setProperty(
        "outputNode", 99); // A malformed graph must be rejected independently
                           // of JSON validity.
    auto invalid = state.clone();
    invalid.getDynamicObject()->setProperty("nodes", Array<Value>{});
    require(!cydaw::document::restoreNativeDspGraphState(invalid, error),
            "invalid graph rejected");
    cydaw::app::ModuleInterfaceState surface;
    surface.designed = true;
    surface.windowWidth = 960;
    surface.windowHeight = 540;
    cydaw::app::ModulePublishedParameter knob;
    knob.key = "gain.db";
    knob.label = "Gain";
    knob.minimum = -24;
    knob.maximum = 24;
    knob.value = -6;
    knob.defaultValue = 0;
    surface.parameters.push_back(knob);
    auto surfaceJson = cydaw::app::serialiseModuleInterface(surface);
    auto restoredSurface = cydaw::app::restoreModuleInterface(
        JSON::parse(JSON::toString(surfaceJson)));
    require(restoredSurface.designed && restoredSurface.windowWidth == 960 &&
                restoredSurface.parameters.size() == 1 &&
                restoredSurface.parameters[0].value == -6,
            "actual plugin interface document without JUCE");
    Text appended="clip-";appended+=12;appended+=char32_t(0xe9);require(appended==Text::fromUTF8("clip-12\xc3\xa9"),"numeric and Unicode append remain distinct");
  auto longLines=Text::repeatedString("example\n",10000);require(longLines.replace("\n","\r\n").getNumBytesAsUTF8()==90000,"large multiline text replacement");
  auto file = File::createTempFile(".cydaw-test");
    auto *projectObject = new Object;
    Value project(projectObject);
    projectObject->setProperty("format", "CyDAW Project");
    projectObject->setProperty("version", 36);
    projectObject->setProperty("name", words);
    projectObject->setProperty("id", int64(INT64_MAX));
    auto recovery = cydaw::document::ProjectRecovery::makeRecoveryDocument(
        project, file, Time(1234567890123));
    require(cydaw::document::ProjectRecovery::writeDocumentAtomically(file,
                                                                      recovery)
                .wasOk(),
            "atomic project save without JUCE");
    Value loaded;
    require(
        cydaw::document::ProjectRecovery::readDocument(file, loaded).wasOk(),
        "actual project load without JUCE");
    require(loaded.getProperty("name").toString() == words &&
                int64(loaded.getProperty("id")) == INT64_MAX,
            "project text and identities preserved");
    require(cydaw::document::ProjectRecovery::recoveryTimestamp(loaded)
                    .toMilliseconds() == 1234567890123,
            "recovery timestamp");
    MemoryBlock block("hello", 5), decoded;
    require(decoded.fromBase64Encoding(block.toBase64Encoding()) &&
                decoded == block,
            "binary state base64");
    require(!decoded.fromBase64Encoding("@@@=") && decoded == block,
            "invalid binary leaves prior state intact");
    auto settings = file.withFileExtension(".settings");
    require(settings.replaceWithText(
                "<PROPERTIES><VALUE name=\"title\" val=\"Fran&#231;ais &amp; "
                "clips\"/><VALUE name=\"device\"><DEVICESETUP "
                "rate=\"48000\"/></VALUE></PROPERTIES>"),
            "legacy settings fixture");
    {
      PropertiesFile preferences(settings, {});
      require(preferences.getValue("title") == Text::fromUTF8("Fran\xc3\xa7"
                                                              "ais & clips"),
              "legacy XML preferences and entities");
      require(XmlDocument::parse(preferences.getValue("device"))
                      ->getIntAttribute("rate") == 48000,
              "nested legacy device preferences");
      preferences.setValue("language", "fr");
      preferences.setValue("Language", "English");
      require(preferences.getValue("language") == "fr",
              "preference keys retain their case-sensitive identity");
      preferences.setValue("enabled", true);
      require(preferences.saveIfNeeded(), "atomic preferences save");
    }
    {
      PropertiesFile preferences(settings, {});
      require(preferences.getValue("language") == "fr" &&
                  preferences.getBoolValue("enabled"),
              "preferences reload");
    }
    require(settings.deleteFile(), "remove private settings");
    auto xml =
        XmlDocument::parse("<track><clip name='a'/><clip name='b'/></track>");
    require(xml && xml->getChildWithTagNameIterator("clip").size() == 2 &&
                xml->getChildElement(1)->getStringAttribute("name") == "b",
            "project XML traversal");
    require(!XmlDocument::parse("<broken>"), "invalid XML rejected");
    auto folder=File::createTempFile("-files");require(folder.createDirectory().wasOk(),"file mask fixture");require(folder.getChildFile("one.wav").create()&&folder.getChildFile("two.FLAC").create()&&folder.getChildFile("ignore.txt").create(),"file mask inputs");require(folder.findChildFiles(File::findFiles,false,"*.wav;*.flac").size()==2,"multiple extension filter retains MIDI stems");require(folder.deleteRecursively(),"remove file mask fixture");
  require(file.deleteFile(), "remove private test project");
    std::cout << "CyDaw original text/document/DSP state passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
