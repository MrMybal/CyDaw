#pragma once
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
namespace cydaw::v2 {
// Transport only: no audio device is opened or changed. Callbacks run on the message thread.
class TransportSync final {
public:
    struct State {
        bool playing {};
        double seconds {}, bpm { 120.0 };
        int numerator { 4 }, denominator { 4 };
        bool available { true };
    };
    struct Update {
        State state;
        bool play {}, position {}, tempo {};
    };
    using Read = std::function<State(int)>;
    using Apply = std::function<void(int, const Update&)>;
    TransportSync(juce::PropertiesFile&, juce::AudioDeviceManager&, bool saveSettings, Read, Apply);
    ~TransportSync();
    void draw(bool& visible);
    void drawButton(bool& visible);
    juce::var command(const juce::var&);
    [[nodiscard]] bool active() const;
    // Its traffic, for the lights: messages received and sent; what it
    // does, in a line (empty: off).
    [[nodiscard]] std::uint64_t receivedCount() const;
    [[nodiscard]] std::uint64_t sentCount() const;
    [[nodiscard]] juce::String statusText() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
