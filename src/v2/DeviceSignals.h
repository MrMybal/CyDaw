#pragma once

#include "DeviceVisuals.h"
#include "NativeDspRuntime.h"
#include "SignalAnalysis.h"
#include "StudioTypes.h"

#include <functional>

#include <array>
#include <map>
#include <optional>
#include <vector>

// The sound of the selected track's devices, for the displays of both the
// device panel and the graph: which engine nodes to tap, their spectra and
// frames, and their levels over the last seconds. Updated once a frame,
// whichever view asks first.

namespace cydaw::v2 {

class DeviceSignals final {
public:
    // Five seconds of levels, sixty a second.
    static constexpr int historyLength = 300;
    using History = std::array<float, historyLength>;
    // What live() fills, oldest first; it must outlive the Live it returns.
    struct Histories {
        History inputs {};
        History outputs {};
        History differences {};
    };

    // Where the taps are read: a track's chain in the engine (or the
    // simulator's), or a plug-in's own graph. `simulation`: the workshop's
    // simulator rather than `track`.
    struct Source {
        using Taps = std::vector<app::NativeDspRuntime::TapRequest>;
        std::function<void(bool simulation, const TrackRef& track, const Taps& taps)> setTaps;
        std::function<bool(bool simulation, const TrackRef& track, std::uint64_t nodeId, bool input, engine::AudioAnalysisFrame& frame)>
            readTap;
        // What a node's processor shows of its work (a track's chain only).
        std::function<void(const TrackRef& track, std::uint64_t nodeId, std::vector<float>& readings)> readings;
        // The loudest each node gave out since the last time (a track's chain only).
        std::function<void(const TrackRef& track, const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                           std::vector<float>& right)>
            peaks;
        std::function<double()> tempo;
        // Whether taps left on a track are still to be taken off it (it is still there).
        std::function<bool(const TrackRef& track)> stillThere;
    };
    explicit DeviceSignals(Source source) : source_(std::move(source)) {}

    void update(const TrackRef& track, const GraphSlot& slot);
    // The same for the workshop's device as the simulator plays it.
    void updateSimulation(const GraphSlot& slot);
    // Where a card runs in the engine, when it does.
    [[nodiscard]] const app::NodeGraphComponent::EditorNativeSpan* span(std::uint64_t cardId) const;
    // A card's live data, its histories copied into `histories`.
    [[nodiscard]] visual::Live live(std::uint64_t cardId, Histories& histories) const;
    // How loud a card's sound comes out now (left, right; linear), falling
    // back as a meter's does; none for a card the engine does not run.
    struct Level {
        float left {};
        float right {};
    };
    [[nodiscard]] std::optional<Level> outputLevel(std::uint64_t cardId) const;
    // The instrument's (where its voices enter the effects), for its cards.
    [[nodiscard]] std::optional<Level> instrumentLevel() const;

private:
    // Where the taps are: a track's chain, or the simulator.
    struct Target {
        bool simulation {};
        TrackRef track;
        [[nodiscard]] bool operator==(const Target& other) const noexcept
        {
            return simulation == other.simulation && (simulation || track == other.track);
        }
    };
    void updateTarget(const Target& target, const GraphSlot& slot);
    struct Card {
        Card()
        {
            inputHistory.fill(-120.0f);
            outputHistory.fill(-120.0f);
        }
        Spectrum before;
        Spectrum after;
        engine::AudioAnalysisFrame beforeFrame;
        engine::AudioAnalysisFrame afterFrame;
        std::uint64_t beforeSequence {};
        std::uint64_t afterSequence {};
        bool hasBefore {};
        bool hasAfter {};
        double lastFresh { -1.0e9 };
        double lastHistory {};
        float inputDb { -120.0f };
        float outputDb { -120.0f };
        History inputHistory {};
        History outputHistory {};
        History differenceHistory {};
        int historyWrite {};
        // What its processor shows of its work (AudioProcessor::readings).
        std::vector<float> readings;
    };

    Source source_;
    std::map<std::uint64_t, Card> cards_;
    std::map<std::uint64_t, app::NodeGraphComponent::EditorNativeSpan> spans_;
    const GraphSlot* spansSlot_ {};
    std::uint64_t spansRevision_ {};
    std::optional<Target> tapped_;
    std::vector<app::NativeDspRuntime::TapRequest> taps_;
    int updatedFrame_ { -1 };
    // Every card's output level (all of them, folded too: the taps are for
    // the displays, these for the meters), and when they were last taken.
    std::map<std::uint64_t, Level> levels_;
    double levelsTaken_ {};
    void takeLevels(const Target& target);
};

} // namespace cydaw::v2
