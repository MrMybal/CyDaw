#pragma once

#include "StudioTypes.h"

#include <cydaw/engine/VoiceLive.h>

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

// What the views of a device (Prisme's, the synths', Kick's, the presets
// menu, the effects after an instrument) ask of where the device lives: the
// session (Studio) in CyDAW, or the plug-in's own graph once exported. A
// track may be the plug-in's only one; what a plug-in has no use for (the
// session's automation lanes) answers nothing.

namespace cydaw::v2 {

class DeviceHost {
public:
    virtual ~DeviceHost() = default;

    // The graph a track (or a pad of its rack) plays.
    [[nodiscard]] virtual GraphSlot* graphFor(const TrackRef& track) = 0;
    [[nodiscard]] virtual const GraphSlot* graphFor(const TrackRef& track) const = 0;
    [[nodiscard]] virtual bool isValid(const TrackRef& track) const = 0;
    [[nodiscard]] virtual juce::String trackName(const TrackRef& track) const = 0;

    // A card's presets: what holds them, its settings as a preset keeps
    // them, a preset put back; the name of the last one each holder took.
    [[nodiscard]] virtual std::optional<PresetHolder> devicePresetHolder(const TrackRef& track, std::uint64_t nodeId) const = 0;
    [[nodiscard]] virtual juce::var devicePresetState(const TrackRef& track, std::uint64_t nodeId) const = 0;
    virtual bool applyDevicePreset(const TrackRef& track, std::uint64_t nodeId, const juce::var& state, juce::String& error) = 0;
    [[nodiscard]] virtual juce::String presetName(const juce::String& holderKey) const = 0;
    virtual void setPresetName(const juce::String& holderKey, const juce::String& name) = 0;

    // The automation lanes of a track's settings (a knob's right click).
    [[nodiscard]] virtual bool canAutomate(const TrackRef& track, const juce::String& binding) const = 0;
    // What names its knobs as controllers' targets ("param/<track>/"); empty
    // where they are none (a plug-in's window).
    [[nodiscard]] virtual std::string learnPrefix(const TrackRef&) const { return {}; }
    [[nodiscard]] virtual bool hasLane(const TrackRef& track, const juce::String& lane) const = 0;
    virtual void clearLane(const TrackRef& track, const juce::String& lane) = 0;
    [[nodiscard]] virtual std::vector<juce::String> automatedParameters(const TrackRef& track) const = 0;
    virtual void setShownLane(const TrackRef& track, const juce::String& lane) = 0;
    virtual void setAutomationVisible(const TrackRef& track, bool visible) = 0;
    [[nodiscard]] virtual std::optional<float> laneValueNow(const TrackRef& track, const juce::String& lane) const = 0;

    // What an instrument's newest voice plays now (its modulations, live).
    virtual bool voiceLive(const TrackRef& track, engine::VoiceLive& out) const = 0;

    // A sound file made the table of a Prisme card's oscillator; back to the
    // factory one.
    virtual bool importPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator, const juce::File& file,
                                  juce::String& error) = 0;
    virtual bool clearPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator) = 0;

    // A cabinet's or a place's (Empreinte) response read from a sound file;
    // taken off (the models or the built-in spaces then).
    virtual bool loadCabImpulse(const TrackRef& track, std::uint64_t nodeId, const juce::File& file, juce::String& error) = 0;
    virtual bool clearCabImpulse(const TrackRef& track, std::uint64_t nodeId) = 0;

    // A device of the catalogue added to a track's chain.
    virtual juce::Result insertDevice(const TrackRef& track, const juce::String& key) = 0;
};

} // namespace cydaw::v2
