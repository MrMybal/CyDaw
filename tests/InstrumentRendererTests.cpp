#include "GraphPolySynthRenderer.h"
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
engine::PolySynthGraphPlan sine() {
  engine::PolySynthGraphPlan p;
  p.enabled = true;
  p.waveform = engine::PolyOscillatorWaveform::sine;
  p.oscillatorLevel = .15f;
  p.attackSeconds = .0001f;
  p.decaySeconds = .0001f;
  p.sustainLevel = 1;
  p.releaseSeconds = .005f;
  return p;
}
int crossings(const audio::SampleBuffer<float> &samples) {
  int count = 0;
  for (int i = 65; i < samples.getNumSamples(); ++i)
    if (samples.getSample(0, i - 1) <= 0 && samples.getSample(0, i) > 0)
      ++count;
  return count;
}
int main() {
  try {
    app::GraphPolySynthRenderer renderer;
    renderer.prepare(48000);
    renderer.setPlan(sine());
    midi::EventBuffer events;
    require(events.addEvent(cyaudio::midiNoteOn(1, 69, 100), 32),
            "Core note on event");
    events.addEvent(cyaudio::midiController(1, 64, 127), 64);
    events.addEvent(cyaudio::midiNoteOff(1, 69), 128);
    events.addEvent(cyaudio::midiController(1, 64, 0), 512);
    audio::SampleBuffer<float> samples(2, 2048);
    renderer.render(samples, events, 0, 2048);
    require(samples.getMagnitude(0, 32) == 0,
            "sample-accurate native instrument start");
    require(samples.getRMSLevel(0, 256, 128) > .01f,
            "sustain retains released key");
    require(samples.getRMSLevel(0, 1500, 512) < 1e-5f,
            "sustain release finishes voice tail");
    renderer.release();
    renderer.prepare(48000);
    renderer.setPlan(sine());
    events.clear();
    events.addEvent(cyaudio::midiNoteOn(1, 69, 127), 0);
    samples.clear();
    renderer.render(samples, events, 0, 2048);
    const int base = crossings(samples);
    require(base > 14 && base < 22, "native A4 oscillator frequency");
    renderer.release();
    renderer.prepare(48000);
    renderer.setPlan(sine());
    events.clear();
    events.addEvent(cyaudio::midiPitchBend(2, 12288), 0);
    events.addEvent(cyaudio::midiNoteOn(2, 69, 127), 0);
    samples.clear();
    renderer.render(samples, events, 0, 2048);
    const int bent = crossings(samples);
    require(bent >= base * 1.8 && bent <= base * 2.2,
            "channel expression bends the native voice by twelve semitones");
    require(events.rejectedEvents() == 0, "prepared MIDI events are retained");
    renderer.release();
    std::cout << "Actual native instrument timing, sustain, release, frequency "
                 "and expression passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
