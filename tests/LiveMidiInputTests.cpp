#include <cydaw/midi/LiveInput.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::midi;
void require(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
int main() {
  try {
    auto note = Message::noteOn(2, 60, .5f);
    require(note.isNoteOn() && note.getChannel() == 2 &&
                note.getNoteNumber() == 60 && note.getVelocity() == 64,
            "Core wire message builders");
    require(Message::noteOn(1, 60, 0).isNoteOff(),
            "zero velocity note-on releases a note");
    const std::uint8_t data[] = {1, 2, 3, 4, 5};
    auto sysex = Message::createSysExMessage(data, 5);
    auto copy = sysex;
    require(copy.isSysEx() && copy.getSysExDataSize() == 5 &&
                copy.getSysExData()[4] == 5,
            "long messages own their bytes");
    require(Message::getMidiNoteName(60, true, true, 3) == "C3",
            "note naming retains middle-C convention");
    MessageCollector collector;
    collector.reset(48000);
    EventBuffer events;
    auto message = Message::noteOn(1, 69, 100);
    message.setTimeStamp(10.001);
    collector.addMessageToQueue(message);
    auto future = Message::noteOff(1, 69);
    future.setTimeStamp(11.);
    collector.addMessageToQueue(future);
    {
      const cyaudio::AudioClockAnchor anchor{10000000000ull, 1000, 48000};
      AudioClockScope clock(anchor);
      collector.removeNextBlockOfMessages(events, 128);
    }
    require(events.getNumEvents() == 1 &&
                (*events.begin()).samplePosition == 48,
            "Core collector maps device timestamps to exact sample offsets");
    {
      const cyaudio::AudioClockAnchor anchor{11000000000ull, 48000, 48000};
      AudioClockScope clock(anchor);
      collector.removeNextBlockOfMessages(events, 128);
    }
    require(events.getNumEvents() == 1 &&
                (*events.begin()).getMessage().isNoteOff() &&
                (*events.begin()).samplePosition == 0,
            "Core collector retains future events");
    KeyboardState keyboard;
    keyboard.noteOn(3, 64, .8f);
    require(keyboard.isNoteOn(3, 64) &&
                keyboard.isNoteOnForChannels(1 << 2, 64) &&
                !keyboard.isNoteOn(2, 64),
            "native keyboard tracks channel and key");
    events.clear();
    {
      const cyaudio::AudioClockAnchor anchor{monotonicNanoseconds() + 1000000,
                                             0, 48000};
      AudioClockScope clock(anchor);
      keyboard.processNextMidiBuffer(events, 0, 512, true);
    }
    require(events.getNumEvents() == 1 &&
                (*events.begin()).getMessage().isNoteOn(),
            "native keyboard injects queued events through Core collector");
    keyboard.allNotesOff(0);
    require(!keyboard.isNoteOn(3, 64), "keyboard panic releases held keys");
    require(collector.rejectedMessages() == 0 && events.rejectedEvents() == 0,
            "native live MIDI retains prepared messages");
    std::cout << "Native MIDI messages, timestamps, future events and keyboard "
                 "state passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
