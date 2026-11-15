// A scheduling probe only: the frozen JUCE objects and the application adapter
// receive identical MIDI; a dummy voice records when notifications are heard.
#include <iostream>
#include <vector>
#ifdef CYDAW_PROBE_JUCE
#include <juce_audio_basics/juce_audio_basics.h>
using Allocator = juce::Synthesiser;
using Voice = juce::SynthesiserVoice;
using Sound = juce::SynthesiserSound;
using Buffer = juce::AudioBuffer<float>;
using Midi = juce::MidiBuffer;
using Message = juce::MidiMessage;
#else
#include <cydaw/audio/VoiceAllocator.h>
#include <cydaw/midi/EventBuffer.h>
#include <cydaw/midi/Message.h>
using Allocator = cydaw::audio::VoiceAllocator;
using Voice = cydaw::audio::AllocatedVoice;
using Sound = cydaw::audio::VoiceSound;
using Buffer = cydaw::audio::SampleBuffer<float>;
using Midi = cydaw::midi::EventBuffer;
using Message = cydaw::midi::Message;
#endif
struct Hit { int note, frame; };
struct ProbeSound : Sound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};
struct ProbeVoice : Voice {
    int &cursor;
    std::vector<Hit> &hits;
    ProbeVoice(int &c, std::vector<Hit> &h) : cursor(c), hits(h) {}
    bool canPlaySound(Sound *) override { return true; }
    void startNote(int note, float, Sound *, int) override { hits.push_back({note, cursor}); }
    void stopNote(float, bool) override { clearCurrentNote(); }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(Buffer &, int first, int count) override { cursor = first + count; }
};
void run(const char *name, bool future) {
    int cursor = 0;
    std::vector<Hit> hits;
    Allocator synth;
    synth.addVoice(new ProbeVoice(cursor, hits));
    synth.addSound(new ProbeSound);
    synth.setCurrentPlaybackSampleRate(48000);
    synth.setNoteStealingEnabled(true);
    Midi midi;
    midi.addEvent(Message::noteOn(1, 30, 0.7f), 0);
    midi.addEvent(Message::noteOn(1, 60, 0.7f), 10);
    midi.addEvent(Message::noteOn(1, 64, 0.7f), 22);
    midi.addEvent(Message::noteOn(1, 65, 0.7f), 60);
    if (future) midi.addEvent(Message::noteOn(1, 67, 0.7f), 200);
    Buffer output(2, 256);
    output.clear();
    synth.renderNextBlock(output, midi, 0, 128);
    if (future) synth.renderNextBlock(output, midi, 128, 128);
    std::cout << "{\"case\":\"" << name << "\",\"events\":[";
    bool first = true;
    for (const auto &hit : hits) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"note\":" << hit.note << ",\"heardFrame\":" << hit.frame << '}';
    }
    std::cout << "]}\n";
}
int main() {
    run("near-events", false);
    run("caller-passes-future-events", true);
}
