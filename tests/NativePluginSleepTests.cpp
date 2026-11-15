#include "NativeHostedPlugin.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cydaw;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture and matching SDK worker");
        plugins::NativeHostedPlugin p;
        p.setWorkerExecutable(base::File(argv[2]));
        base::Text error;
        require(p.load(base::File(argv[1]), {}, cyaudio::PluginFormat::clap, "CLAP", error), error.toRawUTF8());
        p.prepareToPlay(48000, 1024);
        audio::SampleBuffer<float> b(2, 1024);
        midi::EventBuffer midi;
        cyaudio::PluginTransport t;
        t.hasSeconds = t.hasSongFrame = t.hasTempo = true; t.tempo = 120;
        p.setTransport(&t);
        auto render = [&](float input) {
            for (int ch = 0; ch < 2; ++ch) std::fill_n(b.getWritePointer(ch), 1024, input);
            p.processBlock(b, midi);
            return b.getSample(0, 0);
        };
        for (int i = 0; i < 8; ++i) require(render(0) == 0, "quiet output");
        auto first = p.sleepInfo();
        require(first.sleeping && first.skippedBlocks > 0, "automatic quiet suspension");
        for (int i = 0; i < 8; ++i) render(0);
        auto second = p.sleepInfo();
        require(second.processedBlocks == first.processedBlocks && second.skippedBlocks == first.skippedBlocks + 8,
                "sleep skips worker processing");
        require(std::abs(render(.5f) - .25f) < 1e-7, "first active block wakes without lost audio");
        require(std::abs(render(0) - .0625f) < 1e-7 && std::abs(render(0) - .0625f) < 1e-7,
                "full finite tail preserved");
        for (int i = 0; i < 8; ++i) render(0);
        require(p.sleepInfo().sleeping, "sleep after tail");
        auto before = p.sleepInfo();
        t.seconds = 3; t.songFrame = 144000; render(0);
        require(p.sleepInfo().processedBlocks > before.processedBlocks, "stopped transport seek wakes");
        for (int i = 0; i < 8; ++i) render(0);
        before = p.sleepInfo(); p.getParameters()[0]->setValue(.75f); render(0);
        require(p.sleepInfo().processedBlocks > before.processedBlocks, "automation wakes");
        require(std::abs(render(.5f) - .375f) < 1e-7, "automation gain audible after wake");
        p.releaseResources(); p.setOffline(true); p.prepareToPlay(48000, 1024);
        for (int i = 0; i < 12; ++i) render(0);
        require(!p.sleepInfo().sleeping && p.sleepInfo().skippedBlocks == 0 && p.sleepInfo().processedBlocks == 12,
                "offline render never sleeps");
        p.releaseResources(); p.setOffline(false); p.prepareToPlay(48000, 1024);
        // The actual nonblocking one-quantum application bridge; pacing is test-only.
        auto realtime = [&](float input) {
            for (int ch = 0; ch < 2; ++ch) std::fill_n(b.getWritePointer(ch), 1024, input);
            p.processRealtime(b, 0, 1024, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return b.getSample(0, 0);
        };
        for (int i = 0; i < 12; ++i) realtime(0);
        require(p.sleepInfo().sleeping, "realtime bridge sleeps");
        realtime(.5f);
        require(std::abs(realtime(0) - .375f) < 1e-7, "realtime wake retains one-quantum audio");
        require(p.missedBlocks() == 0, "no missed tickets in paced functional test");
        std::cout << "Plugin sleep passed: idle processed=" << second.processedBlocks << " skipped=" << second.skippedBlocks
                  << "; wake, tail, seek, automation, offline and realtime verified\n";
        p.releaseResources(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
