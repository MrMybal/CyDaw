#include "DeviceSignals.h"

#include <imgui.h>

#include <algorithm>

namespace cydaw::v2 {

void DeviceSignals::update(const TrackRef& track, const GraphSlot& slot)
{
    updateTarget({ false, track }, slot);
}

void DeviceSignals::updateSimulation(const GraphSlot& slot)
{
    updateTarget({ true, {} }, slot);
}

void DeviceSignals::updateTarget(const Target& target, const GraphSlot& slot)
{
    // Once a frame, whichever view asks first.
    if (updatedFrame_ == ImGui::GetFrameCount() && tapped_ && *tapped_ == target && spansSlot_ == &slot)
        return;
    updatedFrame_ = ImGui::GetFrameCount();
    auto& engine = studio_.engine();
    // Where each device runs in the engine, again only when the graph changed.
    if (spansSlot_ != &slot || spansRevision_ != slot.revision()) {
        spans_ = slot.graph().editorNativeSpans();
        spansSlot_ = &slot;
        spansRevision_ = slot.revision();
    }
    const auto setTaps = [&engine](const Target& where, const std::vector<app::NativeDspRuntime::TapRequest>& taps) {
        if (where.simulation)
            engine.setSimulationTaps(taps);
        else if (where.track.isBus())
            engine.setVirtualBusTaps(where.track.bus, taps);
        else if (where.track.master)
            engine.setMasterNodeTaps(taps);
        else if (where.track.midi)
            engine.setMidiTrackNodeTaps(where.track.midiId, taps);
        else
            engine.setTrackNodeTaps(where.track.audioIndex, taps);
    };
    if (tapped_ && !(*tapped_ == target)) {
        if (tapped_->simulation || tapped_->track.isMaster() || studio_.isValid(tapped_->track)
            || (tapped_->track.isBus() && studio_.hardwareBus(tapped_->track.bus)))
            setTaps(*tapped_, {});
        taps_.clear();
        cards_.clear();
    }
    tapped_ = target;

    // The cards in signal order, as far as the engine keeps taps.
    auto nodes = slot.graph().editorNodes();
    std::stable_sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
    std::vector<std::uint64_t> shown;
    std::vector<app::NativeDspRuntime::TapRequest> wanted;
    for (const auto& node : nodes) {
        if (node.boundary || node.collapsed)
            continue;
        const auto span = spans_.find(node.id);
        if (span == spans_.end() || span->second.exits.empty())
            continue;
        if (wanted.size() + 2 > app::NativeDspRuntime::tapCount)
            break;
        if (!span->second.instrument && !span->second.entries.empty())
            wanted.push_back({ span->second.entries.front(), true });
        wanted.push_back({ span->second.exits.front(), false });
        shown.push_back(node.id);
    }
    const auto same = wanted.size() == taps_.size()
                      && std::equal(wanted.begin(), wanted.end(), taps_.begin(), [](const auto& a, const auto& b) {
                             return a.nodeId == b.nodeId && a.input == b.input;
                         });
    if (!same) {
        taps_ = wanted;
        setTaps(target, taps_);
    }

    const auto now = ImGui::GetTime();
    const auto read = [&engine, &target](const std::uint64_t nodeId, const bool input, engine::AudioAnalysisFrame& frame) {
        if (target.simulation)
            return engine.readSimulationTap(nodeId, input, frame);
        if (target.track.isBus())
            return engine.readVirtualBusTap(target.track.bus, nodeId, input, frame);
        if (target.track.master)
            return engine.readMasterNodeTap(nodeId, input, frame);
        return target.track.midi ? engine.readMidiTrackNodeTap(target.track.midiId, nodeId, input, frame)
                                 : engine.readTrackNodeTap(target.track.audioIndex, nodeId, input, frame);
    };
    for (const auto id : shown) {
        const auto& span = spans_.at(id);
        auto& card = cards_[id];
        auto fresh = false;
        if (!span.instrument && !span.entries.empty() && read(span.entries.front(), true, card.beforeFrame)) {
            card.hasBefore = true;
            if (card.beforeFrame.sequence != card.beforeSequence) {
                card.beforeSequence = card.beforeFrame.sequence;
                card.before.update(&card.beforeFrame, now);
                card.inputDb = recentPeakDb(card.beforeFrame);
                fresh = true;
            }
        }
        if (read(span.exits.front(), false, card.afterFrame)) {
            card.hasAfter = true;
            if (card.afterFrame.sequence != card.afterSequence) {
                card.afterSequence = card.afterFrame.sequence;
                card.after.update(&card.afterFrame, now);
                card.outputDb = recentPeakDb(card.afterFrame);
                fresh = true;
            }
        }
        if (fresh) {
            card.lastFresh = now;
        } else if (now - card.lastFresh > 0.2) {
            // Nothing new: the transport stopped. The displays fall quiet.
            card.before.update(nullptr, now);
            card.after.update(nullptr, now);
            card.inputDb = card.outputDb = -120.0f;
        }
        // Levels sixty times a second, for the dynamics' history.
        if (now - card.lastHistory >= 1.0 / 60.0 && now - card.lastFresh < 0.3) {
            card.lastHistory = now;
            const auto write = static_cast<std::size_t>(card.historyWrite);
            card.inputHistory[write] = card.inputDb;
            card.outputHistory[write] = card.outputDb;
            card.differenceHistory[write] = card.hasBefore && card.inputDb > -100.0f ? card.outputDb - card.inputDb : 0.0f;
            card.historyWrite = (card.historyWrite + 1) % historyLength;
        }
    }
}

const app::NodeGraphComponent::EditorNativeSpan* DeviceSignals::span(const std::uint64_t cardId) const
{
    const auto found = spans_.find(cardId);
    return found != spans_.end() ? &found->second : nullptr;
}

visual::Live DeviceSignals::live(const std::uint64_t cardId, Histories& histories) const
{
    visual::Live live;
    live.tempo = studio_.engine().tempo();
    const auto found = cards_.find(cardId);
    if (found == cards_.end())
        return live;
    const auto& card = found->second;
    live.running = ImGui::GetTime() - card.lastFresh < 0.3;
    live.before = card.hasBefore ? &card.before : nullptr;
    live.after = card.hasAfter ? &card.after : nullptr;
    live.beforeFrame = card.hasBefore ? &card.beforeFrame : nullptr;
    live.afterFrame = card.hasAfter ? &card.afterFrame : nullptr;
    live.inputDb = card.hasBefore ? card.inputDb : -120.0f;
    live.outputDb = card.outputDb;
    for (auto index = 0; index < historyLength; ++index) {
        const auto from = static_cast<std::size_t>((card.historyWrite + index) % historyLength);
        histories.inputs[static_cast<std::size_t>(index)] = card.hasBefore ? card.inputHistory[from] : -120.0f;
        histories.outputs[static_cast<std::size_t>(index)] = card.outputHistory[from];
        histories.differences[static_cast<std::size_t>(index)] = card.differenceHistory[from];
    }
    live.inputHistory = histories.inputs.data();
    live.outputHistory = histories.outputs.data();
    live.reductionHistory = histories.differences.data();
    live.historyCount = historyLength;
    return live;
}

} // namespace cydaw::v2
