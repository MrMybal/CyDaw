#include "HelpDocumentation.h"
#include <iostream>
#include <stdexcept>
using namespace cydaw::v2;
static void need(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
int main(int argc, char** argv) {
    try {
        need(argc==2,"help export directory required");
        HelpDocumentation help{juce::File(juce::String::fromUTF8(argv[1]))};
        need(help.load(),"load shipped CyDoc catalog");
        auto link=help.link("cinema","video","fr");
        need(link.startsWith("file:///") && link.endsWith("#/fr/cinema/video"),"canonical file route");
        need(help.follow("manual:mixing.md#sidechain").endsWith("#/en/mixing/sidechain"),"old manual link");
        need(help.follow("doc:mixing#sidechain").endsWith("#/en/mixing/sidechain"),"CyDoc link");
        need(help.link("atelier-vst").endsWith("#/en/workshop"),"renamed workshop alias");
        juce::StringPairArray media;media.set("media","figure-cinema");media.set("zoom","2");media.set("x","12.5");
        need(help.link("cinema",{},"en",media).endsWith("?media=figure-cinema&x=12.5&zoom=2"),"precise media state");
        for (const auto* invalid : {"https://example.com","file:///Windows/win.ini","../../secret.md","doc://external/page","missing"})
            need(help.follow(invalid).isEmpty(),"reject unknown/external help target");
        need(help.link("cinema","unknown").isEmpty(),"unknown section");
        need(help.link("cinema",{},"de").isEmpty(),"unsupported locale");
        const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile(juce::String::fromUTF8("CyDAW help é #%"),"",false);
        folder.createDirectory();
        struct Cleanup {juce::File file;~Cleanup(){file.deleteRecursively();}} cleanup{folder};
        juce::File(juce::String::fromUTF8(argv[1])).getChildFile("routes.json").copyFileTo(folder.getChildFile("routes.json"));
        folder.getChildFile("index.html").replaceWithText("fixture");
        HelpDocumentation spaced{folder};auto encoded=spaced.link("welcome");
        need(encoded.contains("%20") && encoded.contains("%23") && encoded.contains("%25") && encoded.contains("%C3%A9"),"platform file URL encoding");
        HelpDocumentation absent{folder.getChildFile("absent")};need(!absent.load() && absent.error().isNotEmpty(),"missing resources are reported");
        std::cout<<"CyDoc native route/alias/media/encoding checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
