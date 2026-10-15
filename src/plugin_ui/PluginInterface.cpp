#include "PluginInterface.h"

#include "DeviceBody.h"
#include "DeviceHost.h"
#include "DeviceOps.h"
#include "DeviceSignals.h"
#include "ImGuiHost.h"
#include "InterfaceEditor.h"
#include "KickView.h"
#include "Localization.h"
#include "PresetMenu.h"
#include "PrismView.h"
#include "SynthView.h"
#include "Theme.h"
#include "UiState.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <map>

namespace cydaw::plugin_ui {

using namespace cydaw::v2;
using ui::dp;
using ui::L;

namespace {

// The plug-in's graph as the device views see a track's: its only track.
class PluginDeviceHost final : public DeviceHost {
public:
    PluginDeviceHost(juce::String name, const bool instrument, PluginSound& sound, std::function<void(bool)> changed)
        : name_(std::move(name)), sound_(sound), changed_(std::move(changed)),
          slot_(GraphSlot::Owner::plugin, [this](GraphSlot&, const bool structural) { publish(structural); })
    {
        track_.midi = instrument;
        track_.midiId = instrument ? 1 : 0;
    }

    [[nodiscard]] const TrackRef& track() const noexcept { return track_; }
    [[nodiscard]] GraphSlot& slot() noexcept { return slot_; }
    [[nodiscard]] const GraphSlot& slot() const noexcept { return slot_; }
    void publish(const bool structural) const
    {
        if (changed_)
            changed_(structural);
    }

    GraphSlot* graphFor(const TrackRef& track) override { return isValid(track) ? &slot_ : nullptr; }
    const GraphSlot* graphFor(const TrackRef& track) const override { return isValid(track) ? &slot_ : nullptr; }
    bool isValid(const TrackRef& track) const override { return track.owner() == track_; }
    juce::String trackName(const TrackRef&) const override { return name_; }

    std::optional<PresetHolder> devicePresetHolder(const TrackRef& track, const std::uint64_t nodeId) const override
    {
        if (!isValid(track))
            return std::nullopt;
        for (const auto& node : slot_.graph().editorNodes())
            if (node.id == nodeId)
                return PresetHolder { "device", node.typeKey, "plugin/device/" + juce::String(static_cast<juce::int64>(nodeId)) };
        return std::nullopt;
    }
    juce::var devicePresetState(const TrackRef& track, const std::uint64_t nodeId) const override
    {
        return isValid(track) ? device_ops::presetState(slot_.graph(), nodeId) : juce::var();
    }
    bool applyDevicePreset(const TrackRef& track, const std::uint64_t nodeId, const juce::var& state, juce::String& error) override
    {
        if (!isValid(track) || !device_ops::applyPreset(slot_.graph(), nodeId, state, error, {}))
            return false;
        publish(true);
        return true;
    }
    juce::String presetName(const juce::String& holderKey) const override
    {
        const auto found = presetNames_.find(holderKey);
        return found != presetNames_.end() ? found->second : juce::String();
    }
    void setPresetName(const juce::String& holderKey, const juce::String& name) override { presetNames_[holderKey] = name; }

    // The host automates the plug-in's parameters: no lanes of its own.
    bool canAutomate(const TrackRef&, const juce::String&) const override { return false; }
    bool hasLane(const TrackRef&, const juce::String&) const override { return false; }
    void clearLane(const TrackRef&, const juce::String&) override {}
    std::vector<juce::String> automatedParameters(const TrackRef&) const override { return {}; }
    void setShownLane(const TrackRef&, const juce::String&) override {}
    void setAutomationVisible(const TrackRef&, bool) override {}
    std::optional<float> laneValueNow(const TrackRef&, const juce::String&) const override { return std::nullopt; }

    bool voiceLive(const TrackRef& track, engine::VoiceLive& out) const override
    {
        return isValid(track) && sound_.voiceLive && sound_.voiceLive(out);
    }

    bool importPrismTable(const TrackRef& track, const std::uint64_t nodeId, const int oscillator, const juce::File& file,
                          juce::String& error) override
    {
        if (!isValid(track) || !device_ops::importPrismTable(slot_.graph(), nodeId, oscillator, file, error))
            return false;
        publish(true);
        return true;
    }
    bool clearPrismTable(const TrackRef& track, const std::uint64_t nodeId, const int oscillator) override
    {
        if (!isValid(track) || !device_ops::clearPrismTable(slot_.graph(), nodeId, oscillator))
            return false;
        publish(true);
        return true;
    }
    bool loadCabImpulse(const TrackRef& track, const std::uint64_t nodeId, const juce::File& file, juce::String& error) override
    {
        if (!isValid(track) || !device_ops::loadImpulse(slot_.graph(), nodeId, file, error))
            return false;
        publish(true);
        return true;
    }
    bool clearCabImpulse(const TrackRef& track, const std::uint64_t nodeId) override
    {
        juce::String error;
        if (!isValid(track) || !slot_.graph().nodeImpulseJson(nodeId).isObject() || !slot_.graph().setNodeImpulse(nodeId, {}, error))
            return false;
        publish(true);
        return true;
    }

    // An effect after the instrument (its effects tab): the instrument
    // itself is the plug-in's, never replaced.
    juce::Result insertDevice(const TrackRef& track, const juce::String& key) override
    {
        if (!isValid(track))
            return juce::Result::fail("no such track");
        auto& graph = slot_.graph();
        const auto catalogue = graph.deviceCatalogue();
        const auto device = std::find_if(catalogue.begin(), catalogue.end(), [&key](const auto& entry) { return entry.key == key; });
        if (device == catalogue.end())
            return juce::Result::fail("no device \"" + key + "\"");
        if (device->soundSource)
            return juce::Result::fail("\"" + device->name + "\" " + ui::Ls("est un instrument", "is an instrument"));
        const auto notes = device->input == "midi" && device->output == "midi";
        if (notes && !track_.midi)
            return juce::Result::fail("\"" + device->name + "\" " + ui::Ls("joue sur les notes", "works on notes"));
        auto applied = device->kind == "preset" ? graph.insertFactoryDevice(key)
                       : notes                  ? graph.insertNodeBeforeInstrument(key)
                       : device->input == "none" ? graph.insertNode(key)
                                                 : graph.insertNodeBeforeOutput(key);
        if (applied.failed() && device->kind != "preset")
            applied = graph.insertFactoryDevice(key);
        if (applied.wasOk())
            publish(true);
        return applied;
    }

private:
    juce::String name_;
    PluginSound& sound_;
    std::function<void(bool)> changed_;
    GraphSlot slot_;
    TrackRef track_;
    std::map<juce::String, juce::String> presetNames_;
};

DeviceSignals::Source signalsFrom(PluginSound& sound)
{
    DeviceSignals::Source source;
    source.setTaps = [&sound](bool, const TrackRef&, const DeviceSignals::Source::Taps& taps) {
        if (sound.setTaps)
            sound.setTaps(taps);
    };
    source.readTap = [&sound](bool, const TrackRef&, const std::uint64_t nodeId, const bool input, engine::AudioAnalysisFrame& frame) {
        return sound.readTap && sound.readTap(nodeId, input, frame);
    };
    source.readings = [&sound](const TrackRef&, const std::uint64_t nodeId, std::vector<float>& readings) {
        if (sound.readings)
            sound.readings(nodeId, readings);
        else
            readings.clear();
    };
    source.peaks = [&sound](const TrackRef&, const std::vector<std::uint64_t>& nodes, std::vector<float>& left, std::vector<float>& right) {
        if (sound.peaks) {
            sound.peaks(nodes, left, right);
        } else {
            left.assign(nodes.size(), 0.0f);
            right.assign(nodes.size(), 0.0f);
        }
    };
    source.tempo = [&sound] { return sound.tempo ? sound.tempo() : 120.0; };
    source.stillThere = [](const TrackRef&) { return true; };
    return source;
}

// Whether the person drew an interface for the device in the workshop (not
// the one made from its settings), with anything to play on it.
bool drawnInterface(const app::ModuleInterfaceState& interface)
{
    return (interface.designed || interface.windowWidth != 0)
           && std::any_of(interface.controls.begin(), interface.controls.end(), [](const auto& control) {
                  return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
              });
}

// A window filling the plug-in's, without decoration.
bool beginWhole(const char* id)
{
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    return ImGui::Begin(id, nullptr,
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse);
}

} // namespace

struct PluginInterface::Impl {
    Impl(PluginInterface& owner, const juce::String& productName, const bool instrument, PluginSound soundIn,
         std::function<void(bool)> changed)
        : sound(std::move(soundIn)),
          host(productName, instrument, sound, std::move(changed)),
          signals(signalsFrom(sound)),
          prism(host, state),
          synth(host, state),
          kick(host, state),
          presets(host, state),
          designed(host, state)
    {
        // CyDAW's language, its own windows filling the plug-in's.
        ui::setLanguage(app::Localization::instance().languageCode());
        ui::setEmbedded(true);
        surface.ownWindows = false;
        surface.onFrame = [this] { frame(); };
        // Tests: a frame written once the window has settled.
        snapshot = juce::SystemStats::getEnvironmentVariable("CYDAW_PLUGIN_SNAPSHOT", {});
        owner.addAndMakeVisible(surface);
    }

    // What the plug-in shows: an instrument's own window, the interface
    // drawn in the workshop, or its effects' large views.
    enum class Face { none, prism, synth, kick, drawn, effects };

    struct Shown {
        Face face { Face::none };
        std::uint64_t nodeId {};
    };

    [[nodiscard]] Shown faceOf() const
    {
        const auto& graph = host.slot().graph();
        if (drawnInterface(graph.graphInterfaceState()))
            return { Face::drawn, 0 };
        for (const auto& node : graph.editorNodes()) {
            if (node.typeKey == "prism-synth")
                return { Face::prism, node.id };
            if (SynthView::handles(node.typeKey))
                return { Face::synth, node.id };
            if (node.typeKey == "kick-synth")
                return { Face::kick, node.id };
        }
        return { Face::effects, 0 };
    }

    void frame()
    {
        // A plug-in's window is a module: English, whatever the language.
        const ui::ModuleText english;
        if (snapshot.isNotEmpty() && ++frames == 45)
            surface.captureNextFrame(juce::File(snapshot));
        auto& slot = host.slot();
        const auto& track = host.track();
        signals.update(track, slot);
        // At the display's rate while a sound is heard (its displays move),
        // the idle rate otherwise.
        auto heard = false;
        for (const auto& node : slot.graph().editorNodes())
            if (const auto level = signals.outputLevel(node.id); level && std::max(level->left, level->right) > 1.0e-4f) {
                heard = true;
                break;
            }
        surface.setAnimating(heard);
        const auto shown = faceOf();
        // An instrument's window opened once, then drawn filling the plug-in's.
        if (shown.face != opened.face || shown.nodeId != opened.nodeId) {
            if (shown.face == Face::prism)
                prism.openEditor(track, shown.nodeId);
            else if (shown.face == Face::synth)
                synth.openEditor(track, shown.nodeId);
            else if (shown.face == Face::kick)
                kick.openEditor(track, shown.nodeId);
            opened = shown;
        }
        switch (shown.face) {
        case Face::prism: prism.drawEditor(); break;
        case Face::synth: synth.drawEditor(); break;
        case Face::kick: kick.drawEditor(); break;
        case Face::drawn: drawDrawn(); break;
        default: drawEffects(); break;
        }
        // A message to the person (a preset that would not load, a file).
        if (state.status.isNotEmpty() && ImGui::GetTime() - state.statusTime < 6.0) {
            const auto* viewport = ImGui::GetMainViewport();
            auto* list = ImGui::GetForegroundDrawList();
            const auto text = state.status.toStdString();
            const auto size = ImGui::CalcTextSize(text.c_str());
            const ImVec2 at(viewport->WorkPos.x + dp(10.0f), viewport->WorkPos.y + viewport->WorkSize.y - size.y - dp(14.0f));
            list->AddRectFilled(ImVec2(at.x - dp(6.0f), at.y - dp(4.0f)), ImVec2(at.x + size.x + dp(6.0f), at.y + size.y + dp(4.0f)),
                                theme::palette().bg2, dp(4.0f));
            list->AddText(at, state.statusIsError ? theme::palette().warn : theme::palette().text, text.c_str());
        }
    }

    void drawDrawn()
    {
        auto& graph = host.slot().graph();
        if (beginWhole("##drawn")) {
            designed.drawPlayed("plugin", graph.graphInterfaceState(), ImGui::GetContentRegionAvail(),
                                [&graph](const juce::String& key, const double value) { (void) graph.setGraphInterfaceParameter(key, value); });
        }
        ImGui::End();
    }

    void drawEffects()
    {
        auto& graph = host.slot().graph();
        const auto& track = host.track();
        // The devices in signal order, those with something to show large.
        auto nodes = graph.editorNodes();
        std::stable_sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
        std::vector<std::pair<app::NodeGraphComponent::EditorNode, DeviceBody>> devices;
        for (const auto& node : nodes) {
            if (node.boundary)
                continue;
            auto body = deviceBodyOf(graph, node, signals.span(node.id));
            if (!body.empty())
                devices.emplace_back(node, std::move(body));
        }
        if (beginWhole("##effects")) {
            if (devices.empty()) {
                ui::emptyState(L("Rien à régler", "Nothing to set"), L("Ce périphérique n'a pas de réglages.", "This device has no settings."));
            } else {
                auto chosen = std::find_if(devices.begin(), devices.end(), [this](const auto& device) { return device.first.id == chosenDevice; });
                if (chosen == devices.end())
                    chosen = devices.begin();
                chosenDevice = chosen->first.id;
                // Several devices: chosen at the top, one at a time.
                if (devices.size() > 1) {
                    for (const auto& device : devices) {
                        ImGui::PushID(static_cast<int>(device.first.id));
                        auto current = device.first.id == chosenDevice;
                        const auto colour = IM_COL32(device.first.colour.getRed(), device.first.colour.getGreen(), device.first.colour.getBlue(), 255);
                        if (ui::toggle(device.first.title.toRawUTF8(), &current, colour) && device.first.id != chosenDevice) {
                            chosenDevice = device.first.id;
                            pane = -1;
                        }
                        ImGui::PopID();
                        ImGui::SameLine(0.0f, dp(4.0f));
                    }
                    ImGui::NewLine();
                    ImGui::Separator();
                }
                auto& [node, body] = *chosen;
                // Its name, as large as an instrument's window has it.
                const auto colour = IM_COL32(node.colour.getRed(), node.colour.getGreen(), node.colour.getBlue(), 255);
                ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
                ImGui::TextColored(theme::vec(colour), "%s", node.title.toUpperCase().toRawUTF8());
                ImGui::PopFont();
                ImGui::SameLine(0.0f, dp(16.0f));
                std::map<juce::String, float> modulated = graph.editorModulatedParameters();
                DeviceSignals::Histories histories;
                auto live = signals.live(node.id, histories);
                live.modulated = &modulated;
                if (const auto control = graph.editorControlValue(node.id)) {
                    live.hasControl = true;
                    live.control = *control;
                }
                ImGui::PushID(static_cast<int>(node.id));
                drawDeviceBody(body, pane, host, state, presets, designed, track, graph, node, live, nullptr);
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    PluginSound sound;
    PluginDeviceHost host;
    UiState state;
    DeviceSignals signals;
    PrismView prism;
    SynthView synth;
    KickView kick;
    PresetMenu presets;
    InterfaceEditor designed;
    ImGuiHost surface;
    Shown opened;
    juce::String snapshot;
    int frames {};
    std::uint64_t chosenDevice {};
    int pane { -1 };
};

PluginInterface::PluginInterface(const juce::String& productName, const bool instrument, PluginSound sound,
                                 std::function<void(bool)> changed)
    : impl_(std::make_unique<Impl>(*this, productName, instrument, std::move(sound), std::move(changed)))
{
    setOpaque(true);
}

PluginInterface::~PluginInterface() = default;

app::NodeGraphComponent& PluginInterface::graph()
{
    return impl_->host.slot().graph();
}

void PluginInterface::restore(const juce::var& editorState)
{
    impl_->host.slot().restore(editorState);
}

juce::Point<int> PluginInterface::preferredSize() const
{
    const auto& graph = impl_->host.slot().graph();
    const auto interface = graph.graphInterfaceState();
    switch (impl_->faceOf().face) {
    case Impl::Face::drawn:
        return { interface.windowWidth != 0 ? static_cast<int>(interface.windowWidth) : 720,
                 interface.windowHeight != 0 ? static_cast<int>(interface.windowHeight) : 420 };
    case Impl::Face::prism: return { 1'240, 800 };
    case Impl::Face::synth: return { 1'180, 780 };
    case Impl::Face::kick: return { 1'080, 700 };
    default: return { 1'100, 700 };
    }
}

void PluginInterface::resized()
{
    impl_->surface.setBounds(getLocalBounds());
}

void PluginInterface::captureNextFrame(const juce::File& target, std::function<void(bool)> done)
{
    impl_->surface.captureNextFrame(target, std::move(done));
}

} // namespace cydaw::plugin_ui
