#pragma once

#include "Studio.h"
#include "UiState.h"
#include "Widgets.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Modulators on a device, as Bitwig has them: a column at the left of its
// card with its modulators (an LFO, steps, an envelope, a macro…), a "+"
// that opens them by category, and each one's arrow dragged onto any knob of
// the track (or clicked: armed, a knob's drag then sets its amount). The
// knobs show each modulation as a ring around them. A modulator is a control
// node of the track's graph (engine/Modulators.h) that names its card; a
// modulation is a control cable onto the knob's socket, its amount the ring.

namespace cydaw::v2 {

class ModulatorsView final {
public:
    ModulatorsView(Studio& studio, UiState& state);

    [[nodiscard]] static float width();
    // Before the track's cards are drawn: its modulators and modulations,
    // for the column and the knobs (host()). `trackKey` keys its cards in
    // UiState (its routing key and pad).
    void begin(const TrackRef& track, std::uint64_t trackKey, app::NodeGraphComponent& graph,
               const std::vector<app::NodeGraphComponent::EditorNode>& nodes);
    // After them: the browser of modulators, opened from a "+".
    void end();
    [[nodiscard]] ui::ModulationHost* host() noexcept { return &host_; }
    // A modulator kept in its card's column (out of the chain).
    [[nodiscard]] bool owned(std::uint64_t node) const;
    [[nodiscard]] std::size_t countFor(std::uint64_t owner) const;
    // Whether the card's column shows.
    [[nodiscard]] bool open(std::uint64_t owner) const;
    void setOpen(std::uint64_t owner, bool open);
    // The column of the card `owner`.
    void draw(std::uint64_t owner, ImVec2 at, ImVec2 size, ImU32 colour);
    // The card going: its modulators with it.
    void removeWith(std::uint64_t owner);
    // A modulator's name as the knobs show it ("LFO", "LFO 2").
    [[nodiscard]] std::string nameOf(std::uint64_t modulator) const;

private:
    struct Host final : ui::ModulationHost {
        ModulatorsView* view {};
        // While a modulator's own settings are drawn: their knobs turn, even
        // armed (a modulator's settings take no modulation but Math's A, B).
        bool quiet {};
        void modulationsOf(const std::string& binding, std::vector<ui::KnobModulation>& into) override;
        [[nodiscard]] std::uint64_t armed() const override;
        void dropped(const std::string& binding, std::uint64_t source) override;
        void setAmount(const std::string& binding, std::uint64_t source, float amount) override;
    };
    void drawSettings(const app::NodeGraphComponent::EditorNode& modulator, ImVec2 at, float width, ImU32 colour);
    void drawBrowser();
    void report(const juce::String& problem);
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> key(std::uint64_t owner) const { return { trackKey_, owner }; }

    Studio& studio_;
    UiState& state_;
    Host host_;
    app::NodeGraphComponent* graph_ {};
    std::uint64_t trackKey_ {};
    // This frame's: each modulator's card, its node, its name; each knob's modulations.
    std::map<std::uint64_t, std::uint64_t> owners_;
    std::map<std::uint64_t, app::NodeGraphComponent::EditorNode> modulators_;
    std::map<std::uint64_t, std::string> names_;
    std::map<std::uint64_t, ImU32> colours_;
    std::map<std::string, std::vector<ui::KnobModulation>> byBinding_;
    // The card the browser adds to, and whether to open it this frame.
    std::uint64_t browserOwner_ {};
    bool openBrowser_ {};
    std::array<char, 64> browserSearch_ {};
    // The arrow being dragged (a click on it, not a drag, arms it).
    bool arrowDragged_ {};
    // The last problem told, and when (a drag repeats it every frame).
    juce::String lastProblem_;
    double lastProblemTime_ {};
};

} // namespace cydaw::v2
