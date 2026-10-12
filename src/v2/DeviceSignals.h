#pragma once

#include "DeviceVisuals.h"
#include "NativeDspRuntime.h"
#include "SignalAnalysis.h"
#include "Studio.h"

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

    explicit DeviceSignals(Studio& studio) : studio_(studio) {}

    void update(const TrackRef& track, const GraphSlot& slot);
    // The same for the workshop's device as the simulator plays it.
    void updateSimulation(const GraphSlot& slot);
    // Where a card runs in the engine, when it does.
    [[nodiscard]] const app::NodeGraphComponent::EditorNativeSpan* span(std::uint64_t cardId) const;
    // A card's live data, its histories copied into `histories`.
    [[nodiscard]] visual::Live live(std::uint64_t cardId, Histories& histories) const;

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
    };

    Studio& studio_;
    std::map<std::uint64_t, Card> cards_;
    std::map<std::uint64_t, app::NodeGraphComponent::EditorNativeSpan> spans_;
    const GraphSlot* spansSlot_ {};
    std::uint64_t spansRevision_ {};
    std::optional<Target> tapped_;
    std::vector<app::NativeDspRuntime::TapRequest> taps_;
    int updatedFrame_ { -1 };
};

} // namespace cydaw::v2
