#include "NativeHostedPlugin.h"
#include <cydaw/base/LegacyPluginState.h>
#include <cydaw/base/Utilities.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *reason) {
  if (!okay)
    throw std::runtime_error(reason);
}
int main(int argc, char **argv) {
  try {
    base::MemoryBlock decoded;
    require(base::decodeLegacyStateBytes("3.AIzP", decoded) &&
                decoded.getSize() == 3 &&
                std::memcmp(decoded.getData(), "ABC", 3) == 0,
            "historical six-bit state encoding decodes");
    require(!base::decodeLegacyStateBytes("999999999.A", decoded),
            "oversized legacy state rejected");

    const auto script =
        base::File::getSpecialLocation(base::File::tempDirectory)
            .getNonexistentChildFile("cydaw-native-tempo", ".jsfx");
    struct Cleanup {
      base::File file;
      ~Cleanup() { file.deleteFile(); }
    } cleanup{script};
    require(
        script.replaceWithText("desc:Native tempo "
                               "test\nslider1:-6<-60,12,.1>Gain\n@block\ngain="
                               "tempo/120;\n@sample\nspl0*=gain;spl1*=gain;\n"),
        "test script writes");
    plugins::NativeHostedPlugin plugin;
    base::Text error;
    require(
        plugin.load(script, "jsfx", cyaudio::PluginFormat::jsfx, "JSFX", error),
        error.toRawUTF8());
    cyaudio::PluginTransport time;
    time.hasTempo = true;
    time.tempo = 60;
    plugin.setTransport(&time);
    plugin.prepareToPlay(48000, 480);
    audio::SampleBuffer<float> buffer(2, 480);
    midi::EventBuffer events;
    auto render = [&] {
      for (int ch = 0; ch < 2; ++ch)
        std::fill_n(buffer.getWritePointer(ch), 480, .25f);
      plugin.processBlock(buffer, events);
      return buffer.getMagnitude(0, 0, 480);
    };
    require(std::abs(render() - .125f) < 1e-6f, "JSFX 60 BPM gain");
    time.tempo = 120;
    require(std::abs(render() - .25f) < 1e-6f,
            "JSFX tempo returns to 120 without attenuation");
    base::MemoryBlock state;
    plugin.getStateInformation(state);
    require(state.getSize() > 0, "native script state saves");
    plugin.setStateInformation(state.getData(), int(state.getSize()));
    require(std::abs(render() - .25f) < 1e-6f,
            "script continues after state roundtrip");
    plugin.releaseResources();
    if (argc > 1) {
      plugins::NativeHostedPlugin lv2;
      require(lv2.load(base::File(argv[1]), "urn:cydaw:0x43594457:0x47505253",
                       cyaudio::PluginFormat::lv2, "LV2", error),
              error.toRawUTF8());
      require(lv2.getNumPrograms() >= 2, "LV2 presets discovered");
      base::MemoryBlock a, b;
      lv2.getStateInformation(a);
      lv2.setCurrentProgram(1);
      require(lv2.getCurrentProgram() == 1, "LV2 selection tracked");
      lv2.getStateInformation(b);
      require(a != b, "LV2 preset changes native state");
    }
    std::cout << "Native plugin tempo, state and preset contracts passed "
                 "without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
