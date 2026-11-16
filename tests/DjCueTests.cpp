#include "DjEngine.h"
#include <cyaudio/io/AudioFile.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
struct Fixture {
    cydaw::base::File file = cydaw::base::File::createTempFile(".wav");
    ~Fixture() { (void)file.deleteFile(); }
};
}
int main() try {
    Fixture fixture;
    cyaudio::AudioBuffer<float> tone;
    require(tone.resize(2, 48000 * 12) == cyaudio::Status::ok, "allocate synthetic track");
    for (std::size_t i = 0; i < tone.block().frames(); ++i)
        for (std::size_t ch = 0; ch < 2; ++ch)
            tone.block().channel(ch)[i] = float(int((i * 31 + ch * 73) % 4096) - 2048) / 8192;
    cyaudio::AudioFileInfo info;
    info.sampleRate = 48000; info.channels = 2; info.encoding = cyaudio::AudioEncoding::float32;
    cyaudio::AudioFileWriter writer;
    require(writer.open(std::filesystem::path(fixture.file.getFullPathName().toStdString()), cyaudio::AudioFileFormat::wav, info,
                        {}, tone.block().frames()) == cyaudio::Status::ok, "open synthetic track");
    require(writer.write(cyaudio::AudioBlock<const float>(tone.block())) == cyaudio::Status::ok &&
                writer.close() == cyaudio::Status::ok, "write synthetic track");
    cydaw::app::DjEngine engine;
    engine.prepareToPlay(4097, 48000);
    engine.setQuantize(false);
    cydaw::audio::SampleBuffer<float> output;
    output.setSize(2, 4103);
    auto render = [&](int frames) {
        for (int ch = 0; ch < 2; ++ch)
            std::fill_n(output.getWritePointer(ch), 4103, 99.f);
        engine.getNextAudioBlock({&output, 3, frames});
        for (int ch = 0; ch < 2; ++ch) {
            require(output.getSample(ch, 2) == 99 && output.getSample(ch, frames + 3) == 99, "callback sub-block guards");
            for (int i = 0; i < frames; ++i)
                require(std::isfinite(output.getSample(ch, i + 3)), "finite DJ output");
        }
    };
    cydaw::app::DjEngine::Known known;
    known.hasGrid = true; known.bpm = 120; known.cuePoint = 8.21;
    engine.load(0, fixture.file, known);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        render(1024);
        const auto state = engine.deck(0);
        require(state.state != cydaw::app::DjEngine::LoadState::failed, "decode synthetic track");
        if (state.state == cydaw::app::DjEngine::LoadState::ready && std::abs(state.position - 8.21) < 1e-8) break;
        require(std::chrono::steady_clock::now() < deadline, "DJ load timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (bool keylock : {false, true}) for (int frames : {64, 480, 1024, 2048, 4097}) {
        engine.prepareToPlay(frames, 48000);
        engine.setKeylock(0, keylock);
        engine.seek(0, 8.21);
        engine.cue(0, true); render(frames); render(frames);
        require(engine.deck(0).previewing, "CUE press starts preview");
        engine.cue(0, false); render(frames);
        auto state = engine.deck(0);
        require(!state.playing && !state.previewing && std::abs(state.position - state.cuePoint) < 1e-8,
                "CUE release publishes the exact cue position");
        render(frames);
        require(std::abs(engine.deck(0).position - state.cuePoint) < 1e-8, "CUE hold does not drift");
        engine.setPlaying(0, true); render(frames); render(frames);
        require(engine.deck(0).playing, "play resumes");
        engine.cue(0, true); render(frames);
        state = engine.deck(0);
        require(!state.playing && !state.previewing && std::abs(state.position - state.cuePoint) < 1e-8,
                "CUE while playing stops exactly at the cue");
        engine.cue(0, false); render(frames);
        std::cout << "CUE frames=" << frames << " keylock=" << keylock << " drift=0\n";
    }
    std::cout << "Headless DJ command and published-position tests passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
