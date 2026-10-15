#pragma once

#include "NodeGraphComponent.h"

#include <cstdint>
#include <functional>
#include <memory>

// What the views of the devices share with the session without needing it
// whole: a track as the interface names it, the graph a track plays, a
// preset's holder. A plug-in exported from CyDAW draws the same views over
// its own graph (DeviceHost.h).

namespace cydaw::v2 {

// One track as the interface lists it. Audio tracks come first, then MIDI
// tracks, in the engine's own order.
struct TrackRef {
    bool midi {};
    std::size_t audioIndex {};   // audio tracks: the engine index
    std::uint64_t midiId {};     // MIDI tracks: the engine id
    // The master track: its devices and its fader, not one of the engine's
    // tracks (isValid() says no; its graph, name and mix answer).
    bool master {};
    // A virtual input or output of the software (its bus id): its effects
    // edited as a track's devices are. `master` is set too, so whatever is
    // not one of the engine's tracks leaves it alone as it does the master;
    // what is the master's own (its graph, name, mix, meter) asks for this
    // first.
    std::uint64_t bus {};
    // A pad of an instrument track's rack (its own instrument and effects):
    // its index; -1 for the track itself.
    int pad { -1 };

    [[nodiscard]] static TrackRef ofBus(const std::uint64_t id) noexcept
    {
        TrackRef track;
        track.master = true;
        track.bus = id;
        track.audioIndex = static_cast<std::size_t>(-1);
        return track;
    }
    [[nodiscard]] bool isMaster() const noexcept { return master && bus == 0; }
    [[nodiscard]] bool isBus() const noexcept { return bus != 0; }

    [[nodiscard]] bool operator==(const TrackRef& other) const noexcept
    {
        if (master || other.master)
            return master == other.master && bus == other.bus;
        return midi == other.midi && pad == other.pad && (midi ? midiId == other.midiId : audioIndex == other.audioIndex);
    }
    // The track a pad belongs to (itself for a track).
    [[nodiscard]] TrackRef owner() const noexcept
    {
        auto track = *this;
        track.pad = -1;
        return track;
    }
};

class GraphSlot final {
public:
    // module: the inside of a device made of a graph, open for editing;
    // workshop: a device the workshop holds itself; modulation: a track's
    // value graph (what moves its parameters), never compiled for sound;
    // plugin: the graph of a plug-in exported from CyDAW, in its host.
    enum class Owner { audioTrack, midiTrack, bus, module, workshop, master, modulation, plugin };
    GraphSlot(Owner owner, std::function<void(GraphSlot&, bool structural)> changed);
    ~GraphSlot();

    [[nodiscard]] app::NodeGraphComponent& graph() noexcept { return *graph_; }
    [[nodiscard]] const app::NodeGraphComponent& graph() const noexcept { return *graph_; }
    [[nodiscard]] Owner owner() const noexcept { return owner_; }
    // Bumped by every change, so a view can tell when to refresh caches.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // A change made around the editor's notifications (the control port's).
    void touch() noexcept { ++revision_; }
    // Restoring does not notify: the caller publishes once afterwards.
    void restore(const juce::var& stateWithLibrary);
    [[nodiscard]] juce::var serialise() const;
    [[nodiscard]] juce::String lastCompileError() const { return lastError_; }
    void setLastCompileError(juce::String error) { lastError_ = std::move(error); }

private:
    Owner owner_;
    std::unique_ptr<app::NodeGraphComponent> graph_;
    std::uint64_t revision_ { 1 };
    bool restoring_ {};
    juce::String lastError_;
};

// What has settings, as a preset holds it and puts it back:
// a card of a track's chain (its knobs by key, a sampler's map, a model
// card's model, a wavetable's table; one undo step), a hosted plug-in
// (its whole state), a track's whole chain (an instrument and its effects).
struct PresetHolder {
    juce::String kind;   // "device", "plugin", "chain"
    juce::String target; // the card's type, the plug-in's name, the chain's role
    juce::String key;    // which one it is in the session (its last preset's name)
};

} // namespace cydaw::v2
