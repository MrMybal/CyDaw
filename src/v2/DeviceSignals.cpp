#include "DeviceSignals.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

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
    // Where each device runs in the engine, again only when the graph changed.
    if (spansSlot_ != &slot || spansRevision_ != slot.revision()) {
        spans_ = slot.graph().editorNativeSpans();
        spansSlot_ = &slot;
        spansRevision_ = slot.revision();
    }
    const auto setTaps = [this](const Target& where, const std::vector<app::NativeDspRuntime::TapRequest>& taps) {
        if (source_.setTaps)
            source_.setTaps(where.simulation, where.track, taps);
    };
    if (tapped_ && !(*tapped_ == target)) {
        if (tapped_->simulation || (source_.stillThere && source_.stillThere(tapped_->track)))
            setTaps(*tapped_, {});
        taps_.clear();
        cards_.clear();
    }
    tapped_ = target;
    takeLevels(target);

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
    const auto read = [this, &target](const std::uint64_t nodeId, const bool input, engine::AudioAnalysisFrame& frame) {
        return source_.readTap && source_.readTap(target.simulation, target.track, nodeId, input, frame);
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
        // What its processor shows of its work: a track's chain only.
        if (target.simulation || target.track.isBus() || !source_.readings)
            card.readings.clear();
        else
            source_.readings(target.track, span.exits.front(), card.readings);
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

void DeviceSignals::takeLevels(const Target& target)
{
    // A track's chain: the loudest each card's last nodes gave out since the
    // last frame, then a meter's fall (about 24 dB a second).
    if (target.simulation || target.track.isBus() || !source_.peaks) {
        levels_.clear();
        return;
    }
    std::vector<std::uint64_t> nodes;
    for (const auto& [card, span] : spans_)
        nodes.insert(nodes.end(), span.exits.begin(), span.exits.end());
    std::vector<float> left;
    std::vector<float> right;
    source_.peaks(target.track, nodes, left, right);
    const auto now = ImGui::GetTime();
    const auto fall = static_cast<float>(std::pow(10.0, -24.0 / 20.0 * std::clamp(now - levelsTaken_, 0.0, 0.5)));
    levelsTaken_ = now;
    std::map<std::uint64_t, Level> next;
    auto at = std::size_t {};
    for (const auto& [card, span] : spans_) {
        Level heard;
        for (std::size_t exit = 0; exit < span.exits.size() && at < left.size(); ++exit, ++at) {
            heard.left = std::max(heard.left, left[at]);
            heard.right = std::max(heard.right, right[at]);
        }
        auto shown = Level {};
        if (const auto before = levels_.find(card); before != levels_.end())
            shown = { before->second.left * fall, before->second.right * fall };
        next[card] = { std::max(shown.left, heard.left), std::max(shown.right, heard.right) };
    }
    levels_ = std::move(next);
}

std::optional<DeviceSignals::Level> DeviceSignals::outputLevel(const std::uint64_t cardId) const
{
    const auto found = levels_.find(cardId);
    return found != levels_.end() ? std::optional<Level>(found->second) : std::nullopt;
}

std::optional<DeviceSignals::Level> DeviceSignals::instrumentLevel() const
{
    for (const auto& [card, span] : spans_)
        if (span.instrument)
            return outputLevel(card);
    return std::nullopt;
}

const app::NodeGraphComponent::EditorNativeSpan* DeviceSignals::span(const std::uint64_t cardId) const
{
    const auto found = spans_.find(cardId);
    return found != spans_.end() ? &found->second : nullptr;
}

visual::Live DeviceSignals::live(const std::uint64_t cardId, Histories& histories) const
{
    visual::Live live;
    live.tempo = source_.tempo ? source_.tempo() : 120.0;
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
    live.readings = card.readings.empty() ? nullptr : card.readings.data();
    live.readingCount = static_cast<int>(card.readings.size());
    return live;
}

} // namespace cydaw::v2
