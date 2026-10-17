#include "JsfxView.h"
#include "ModeProcessing.h"
#include "DeviceEditorView.h"
#include "DeviceSignals.h"
#include "DevicesView.h"
#include "GraphEditor.h"
#include "ImGuiHost.h"
#include "LibraryWindow.h"
#include "MixerView.h"
#include "PluginWindows.h"
#include "RoutingView.h"
#include "SamplerMapFile.h"
#include "Theme.h"
#include "TrackModulation.h"
#include "Widgets.h"
#include "BottomPanelControls.h"
#include <bit>
#include <cmath>
#include <set>

namespace cydaw::v2
{
using ui::dp;
using ui::L;
namespace
{
juce::var object()
{
    return juce::var(new juce::DynamicObject());
}
void put(juce::var &v, const char *k, const juce::var &x)
{
    v.getDynamicObject()->setProperty(k, x);
}
double num(const juce::var &v, const char *k, double fallback = 0)
{
    return static_cast<double>(v.getProperty(k, fallback));
}
} // namespace
struct ModeProcessing::Input final : juce::AudioSource
{
    std::uint64_t id{};
    const float *left{};
    const float *right{};
    int count{};
    std::array<float, 8192> rawL{}, rawR{};
    std::array<float, 16384> scratch{};
    std::array<float, 128> blockL{}, blockR{};
    void prepareToPlay(int, double) override
    {
    }
    void releaseResources() override
    {
    }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo &out) override
    {
        out.clearActiveBufferRegion();
        if (!left || !out.buffer)
            return;
        const int n = std::min(count, out.numSamples);
        out.buffer->copyFrom(0, out.startSample, left, n);
        if (out.buffer->getNumChannels() > 1)
            out.buffer->copyFrom(1, out.startSample, right ? right : left, n);
    }
};
ModeProcessing::ModeProcessing(ImGuiHost *host) : host_(host)
{
    static std::atomic<std::uint64_t> nextWindowNamespace{1};
    state_.deviceWindowNamespace = "-mode-" + std::to_string(nextWindowNamespace.fetch_add(1));
    state_.inlineDeviceCatalogue = false;
    output_.setSize(2, 32768);
    studio_.engine().prepareModeMix(rate_, block_);
    state_.selectedTrack = Studio::masterTrack();
    if (!host_)
        return;
    DeviceSignals::Source source;
    source.setTaps = [this](bool, const TrackRef &t, const auto &taps) {
        if (t.master)
            studio_.engine().setMasterNodeTaps(taps);
        else if (t.midi)
            studio_.engine().setMidiTrackNodeTaps(t.midiId, taps);
        else
            studio_.engine().setTrackNodeTaps(t.audioIndex, taps);
    };
    source.readTap = [this](bool, const TrackRef &t, std::uint64_t n, bool input, auto &frame) {
        return t.master ? studio_.engine().readMasterNodeTap(n, input, frame)
               : t.midi ? studio_.engine().readMidiTrackNodeTap(t.midiId, n, input, frame)
                        : studio_.engine().readTrackNodeTap(t.audioIndex, n, input, frame);
    };
    source.readings = [this](const TrackRef &t, std::uint64_t n, auto &v) {
        if (t.master)
            studio_.engine().masterNodeReadings(n, v);
        else if (!t.midi)
            studio_.engine().trackNodeReadings(t.audioIndex, n, v);
    };
    source.peaks = [this](const TrackRef &t, const auto &n, auto &l, auto &r) {
        if (t.master)
            studio_.engine().masterNodePeaks(n, l, r);
        else if (!t.midi)
            studio_.engine().trackNodePeaks(t.audioIndex, n, l, r);
    };
    source.tempo = [this] { return studio_.engine().tempo(); };
    source.stillThere = [this](const TrackRef &t) { return t.master || studio_.isValid(t); };
    signals_ = std::make_unique<DeviceSignals>(std::move(source));
    devices_ = std::make_unique<DevicesView>(studio_, state_, *signals_);
    graph_ = std::make_unique<GraphEditor>(studio_, state_, *host_, *signals_);
    mixer_ = std::make_unique<MixerView>(studio_, state_);
    routing_ = std::make_unique<RoutingView>(studio_, state_);
    editors_ = std::make_unique<DeviceEditorView>(studio_, state_, *signals_);
    windows_ = std::make_unique<PluginWindows>(studio_, state_);
}
ModeProcessing::~ModeProcessing()
{
    studio_.clearSession();
}
void ModeProcessing::prepare(double rate, int block)
{
    if (std::abs(rate - rate_) < .1 && block == block_)
        return;
    rate_ = rate;
    block_ = block;
    studio_.engine().prepareModeMix(rate, block);
}
std::optional<TrackRef> ModeProcessing::inputTrack(std::uint64_t id) const
{
    const auto it = trackKeys_.find(id);
    return it == trackKeys_.end() ? std::nullopt : studio_.trackForKey(it->second);
}
void ModeProcessing::ensureInput(std::uint64_t id, const juce::String &name, const bool midi, const bool bus)
{
    if (auto track = inputTrack(id))
    {
        if (studio_.trackName(*track) != name)
            studio_.setTrackName(*track, name);
        return;
    }
    const auto track = bus ? studio_.addBusTrack(name) : midi ? studio_.addMidiTrack(name, "synth-keys") : studio_.addAudioTrack();
    if (!track)
        return;
    studio_.setTrackName(*track, name);
    trackKeys_[id] = studio_.routingKey(*track);
    if (bus)
        studio_.engine().setModeTrackSource(track->audioIndex, nullptr);
    if (!midi && !bus)
    {
        auto old = std::find_if(inputs_.begin(), inputs_.end(), [id](const auto &p) { return p->id == id; });
        if (old == inputs_.end())
        {
            auto input = std::make_unique<Input>();
            input->id = id;
            inputs_.push_back(std::move(input));
            old = std::prev(inputs_.end());
        }
        studio_.engine().setModeTrackSource(track->audioIndex, old->get());
        auto list = std::make_shared<std::vector<Input *>>();
        for (auto &p : inputs_)
            list->push_back(p.get());
        publishedInputs_.store(std::move(list));
    }
    if (state_.selectedTrack == Studio::masterTrack())
        state_.selectedTrack = *track;
}
void ModeProcessing::syncFoundry(const engine::foundry::Session &session)
{
    if (std::abs(studio_.engine().tempo() - session.tempo) > 0.00001)
        studio_.engine().setTempo(session.tempo);
    std::set<std::uint64_t> present;
    for (const auto &t : session.tracks)
    {
        present.insert(t.id);
        ensureInput(t.id, juce::String::fromUTF8(t.name.c_str()), t.midi, t.bus);
        if (auto track = inputTrack(t.id))
        {
            auto mix = studio_.trackMix(*track);
            mix.gainDb = t.gainDb;
            mix.pan = t.pan;
            mix.muted = t.mute;
            mix.solo = t.solo;
            studio_.setTrackMix(*track, mix);
            std::uint64_t signature = 1469598103934665603ull;
            const auto hash = [&](std::uint64_t v) { signature = (signature ^ v) * 1099511628211ull; };
            hash(std::bit_cast<std::uint64_t>(session.rate));
            hash(std::bit_cast<std::uint64_t>(session.tempo));
            if (t.midi)
                for (const auto &c : t.clips)
                {
                    hash(c.id);
                    hash(c.start);
                    hash(c.offset);
                    hash(c.length);
                    for (const auto &n : c.notes)
                    {
                        hash(n.id);
                        hash(n.start);
                        hash(n.length);
                        hash(n.key);
                        hash(std::bit_cast<std::uint32_t>(n.velocity));
                    }
                }
            if (t.midi && midiSignatures_[t.id] != signature)
            {
                midiSignatures_[t.id] = signature;
                auto &engine = studio_.engine();
                const auto clips = engine.midiClipsView(track->midiId);
                std::vector<std::uint64_t> old;
                if (clips)
                    for (const auto &c : *clips)
                        old.push_back(c.id);
                for (auto id : old)
                    (void)engine.removeMidiClip(track->midiId, id);
                const auto beatsPerFrame = engine.tempo() / 60.0 / session.rate;
                for (const auto &c : t.clips)
                {
                    const auto id = engine.addMidiClip(track->midiId, juce::String::fromUTF8(c.name.c_str()), c.start * beatsPerFrame,
                                                       c.length * beatsPerFrame, false);
                    std::vector<app::AudioTrackController::MidiNote> notes;
                    for (const auto &note : c.notes)
                    {
                        const auto a = std::max<std::int64_t>(c.offset, note.start);
                        const auto b = std::min(c.offset + c.length, note.start + note.length);
                        if (b > a)
                            notes.push_back({note.id, note.key, (a - c.offset) * beatsPerFrame, (b - a) * beatsPerFrame, note.velocity});
                    }
                    (void)engine.replaceMidiNotes(track->midiId, id, std::move(notes));
                }
            }
            auto old = studio_.automatedParameters(*track);
            for (const auto &binding : old)
                studio_.setLanePoints(*track, binding, {});
            std::vector<app::GainAutomationPoint> gain;
            for (const auto &lane : t.automation)
            {
                if (lane.binding.empty())
                {
                    for (const auto &point : lane.points)
                        gain.push_back({point.frame / session.rate, point.value, 0, 0});
                }
                else
                {
                    std::vector<Studio::AutomationPoint> points;
                    for (const auto &point : lane.points)
                        points.push_back({0, point.frame / session.rate, point.value});
                    studio_.setLanePoints(*track, juce::String::fromUTF8(lane.binding.c_str()), points);
                }
            }
            studio_.engine().setModeGainAutomation(track->midi, track->midi ? track->midiId : track->audioIndex, std::move(gain));
        }
    }
    auto masterGain = std::make_shared<MasterGain>();
    masterGain->rate = session.rate;
    for (const auto &lane : session.masterAutomation)
        if (lane.binding.empty())
            masterGain->lane = lane;
    masterGainAutomation_.store(std::move(masterGain));
    std::vector<app::NativeDspRuntime::AutomationLane> master;
    for (const auto &lane : session.masterAutomation)
        if (const auto target = modulation::nativeTargetFor(juce::String::fromUTF8(lane.binding.c_str())))
        {
            app::NativeDspRuntime::AutomationLane out;
            out.nodeId = target->nodeId;
            out.field = target->field;
            out.slot = target->slot;
            for (const auto &point : lane.points)
                out.points.push_back({point.frame / session.rate, std::clamp(point.value, target->minimum, target->maximum), 0, 0});
            master.push_back(std::move(out));
        }
    studio_.engine().setMasterNativeAutomation(std::move(master));
    for (auto it = trackKeys_.begin(); it != trackKeys_.end();)
    {
        if (!present.contains(it->first))
        {
            if (auto track = studio_.trackForKey(it->second))
                studio_.removeTrack(*track);
            it = trackKeys_.erase(it);
        }
        else
            ++it;
    }
    if (state_.selectedTrack && !state_.selectedTrack->master && !studio_.isValid(*state_.selectedTrack))
        state_.selectedTrack = Studio::masterTrack();
}
bool ModeProcessing::copyMixTo(engine::foundry::Session &session)
{
    bool changed = false;
    for (auto &t : session.tracks)
        if (auto track = inputTrack(t.id))
        {
            const auto mix = studio_.trackMix(*track);
            const auto name = studio_.trackName(*track).toStdString();
            if (t.gainDb != mix.gainDb || t.pan != mix.pan || t.mute != mix.muted || t.solo != mix.solo || t.name != name)
                changed = true;
            t.gainDb = mix.gainDb;
            t.pan = mix.pan;
            t.mute = mix.muted;
            t.solo = mix.solo;
            t.name = name;
        }
    // Buses made in the shared mixer also belong to the Foundry timeline.
    for (const auto &track : studio_.tracks())
        if (track.midi || studio_.isBusTrack(track))
        {
            const auto key = studio_.routingKey(track);
            if (std::any_of(trackKeys_.begin(), trackKeys_.end(), [key](const auto &p) { return p.second == key; }))
                continue;
            engine::foundry::Track bus;
            bus.id = session.newId();
            bus.midi = track.midi;
            bus.bus = studio_.isBusTrack(track);
            bus.name = studio_.trackName(track).toStdString();
            const auto m = studio_.trackMix(track);
            bus.gainDb = m.gainDb;
            bus.pan = m.pan;
            bus.mute = m.muted;
            bus.solo = m.solo;
            trackKeys_[bus.id] = key;
            session.tracks.push_back(std::move(bus));
            if (!track.midi)
                studio_.engine().setModeTrackSource(track.audioIndex, nullptr);
            changed = true;
        }
    return changed;
}
bool ModeProcessing::routeInput(std::uint64_t id,std::uint64_t destinationId)
{
    const auto from=inputTrack(id);if(!from)return false;
    const auto to=destinationId?inputTrack(destinationId):std::optional<TrackRef>(Studio::masterTrack());
    if(!to)return false;
    TrackOutput out;out.destination=studio_.routingKey(*to);
    return studio_.setTrackOutputs(*from,{out}).wasOk();
}
void ModeProcessing::selectInput(std::uint64_t id)
{
    if (!id)
    {
        selectMaster();
        return;
    }
    if (auto track = inputTrack(id))
        state_.selectTrack(*track);
}
void ModeProcessing::selectMaster()
{
    state_.selectTrack(Studio::masterTrack());
}
void ModeProcessing::chooseInstrument(std::uint64_t id)
{
    selectInput(id);
    openLibrary();
    state_.libraryTab = 1;
}
juce::Result ModeProcessing::loadSample(std::uint64_t id, const juce::File &file)
{
    const auto track = inputTrack(id);
    if (!track || !track->midi)
        return juce::Result::fail("No MIDI track");
    if (const auto result = studio_.replaceInstrument(*track, "simple-sampler-instrument"); result.failed())
        return result;
    juce::String error;
    if (!studio_.loadSamplerFile(file, track, error))
        return juce::Result::fail(error);
    return juce::Result::ok();
}
std::vector<ModeProcessing::Parameter> ModeProcessing::parameters(std::uint64_t id) const
{
    std::vector<Parameter> result{{"", ui::Ls("Volume", "Volume").toStdString(), -60, 12, 0}};
    const auto track = id ? inputTrack(id) : std::optional<TrackRef>(Studio::masterTrack());
    if (track)
        if (const auto *slot = studio_.graphFor(*track))
            for (const auto &node : slot->graph().editorNodes())
                for (const auto &p : node.parameters)
                    if (id ? studio_.canAutomate(*track, p.binding) : modulation::nativeTargetFor(p.binding).has_value())
                        result.push_back({p.binding.toStdString(), (node.title + " / " + p.label).toStdString(),
                                          static_cast<float>(p.minimum), static_cast<float>(p.maximum), static_cast<float>(p.value)});
    return result;
}
void ModeProcessing::process(const app::ModeInput *inputs, int count, float *left, float *right, int frames)
{
    const auto sources = publishedInputs_.read();
    if (sources)
        for (auto *source : *sources)
        {
            source->left = nullptr;
            source->right = nullptr;
            source->count = frames;
            for (int i = 0; i < count; ++i)
                if (inputs[i].id == source->id)
                {
                    source->left = inputs[i].left;
                    source->right = inputs[i].right;
                    break;
                }
        }
    if (frames > output_.getNumSamples())
    {
        std::fill_n(left, frames, 0);
        std::fill_n(right, frames, 0);
        return;
    }
    const auto at = timeline_;
    if (timelineActive_)
        studio_.engine().setModeTimeline(timeline_, frames, insideFoundry_ || count > 0);
    studio_.engine().renderModeMix({&output_, 0, frames});
    if (timelineActive_)
        timeline_ += frames / rate_;
    std::copy_n(output_.getReadPointer(0), frames, left);
    std::copy_n(output_.getReadPointer(1), frames, right);
    if (timelineActive_)
    {
        const auto gain = masterGainAutomation_.read();
        if (gain && !gain->lane.points.empty())
            for (int i = 0; i < frames; ++i)
            {
                const auto level =
                    juce::Decibels::decibelsToGain(engine::foundry::automationValue(gain->lane, (at + i / rate_) * gain->rate));
                left[i] *= level;
                right[i] *= level;
            }
    }
}
void ModeProcessing::processFoundry(const engine::foundry::Session &session, double at, double step, float *left, float *right, int frames)
{
    // A separate borrow from process(): never nest RtPublisher read guards.
    for (int offset = 0; offset < frames;)
    {
        const auto n = std::min(128, frames - offset);
        std::array<app::ModeInput, 256> blocks{};
        int count = 0;
        {
            const auto sources = publishedInputs_.read();
            if (sources)
                for (auto *source : *sources)
                {
                    if (count == static_cast<int>(blocks.size()))
                        break;
                    const auto track = session.trackIndex(source->id);
                    if (track < 0)
                        continue;
                    const auto first = static_cast<std::int64_t>(std::floor(at)) - 1;
                    const auto size = static_cast<int>(std::ceil(step * n)) + 4;
                    if (size > static_cast<int>(source->rawL.size()))
                        continue;
                    engine::foundry::mixTrackRaw(session.tracks[track], first, size, source->rawL.data(), source->rawR.data(),
                                                 source->scratch.data());
                    for (int i = 0; i < n; ++i)
                    {
                        const auto pos = at + i * step - first;
                        const auto k = static_cast<int>(pos);
                        const auto f = static_cast<float>(pos - k);
                        const auto sample = [&](const auto &a) {
                            const auto c1 = .5f * (a[k + 1] - a[k - 1]);
                            const auto c2 = a[k - 1] - 2.5f * a[k] + 2 * a[k + 1] - .5f * a[k + 2];
                            const auto c3 = .5f * (a[k + 2] - a[k - 1]) + 1.5f * (a[k] - a[k + 1]);
                            return ((c3 * f + c2) * f + c1) * f + a[k];
                        };
                        source->blockL[i] = sample(source->rawL);
                        source->blockR[i] = sample(source->rawR);
                    }
                    blocks[count++] = {source->id, source->blockL.data(), source->blockR.data()};
                }
        }
        timelineActive_ = true;
        timeline_ = at / session.rate;
        insideFoundry_ = true;
        process(blocks.data(), count, left + offset, right + offset, n);
        insideFoundry_ = false;
        at += n * step;
        offset += n;
    }
}
void ModeProcessing::drawTargets()
{
    auto selected = state_.selectedTrack.value_or(Studio::masterTrack());
    ImGui::SetNextItemWidth(dp(210));
    if (ImGui::BeginCombo("##processingTarget", studio_.trackName(selected).toRawUTF8()))
    {
        for (const auto &track : studio_.tracks())
            if (ImGui::Selectable(studio_.trackName(track).toRawUTF8(), track == selected))
                state_.selectTrack(track);
        if (ImGui::Selectable("MASTER", selected.master))
            state_.selectTrack(Studio::masterTrack());
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("MASTER"))
        state_.selectTrack(Studio::masterTrack());
    ImGui::SameLine();
    if (ImGui::Button(L("+ Bus", "+ Bus")))
        if (auto bus = studio_.addBusTrack())
            state_.selectTrack(*bus);
    ImGui::SameLine();
    if (ImGui::Button(L("+ Effet", "+ Effect")))
    {
        panel_ = 0;
        selectPanel_ = true;
        openLibrary();
    }
    ImGui::SameLine();
    auto gain = studio_.engine().masterGainDb();
    ImGui::SetNextItemWidth(dp(130));
    if (ImGui::SliderFloat("Master##mode", &gain, -60, 12, "%.1f dB"))
        studio_.engine().setMasterGain(gain);
}
bool ModeProcessing::undo(const bool redo)
{
    if (!focused_ || panel_ >= 4 || !state_.selectedTrack)
        return false;
    if (auto *slot = studio_.graphFor(*state_.selectedTrack))
    {
        if (redo)
            (void)slot->graph().editorRedo();
        else
            (void)slot->graph().editorUndo();
    }
    return true; // The focused tools own this shortcut, even with no remaining history.
}

void ModeProcessing::draw(const std::function<void()> &editor, const bool visible, const std::function<void()> &collapse, const std::function<void()> &subtracks)
{
    if (!devices_)
        return;
    focused_ = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (std::exchange(state_.openDeviceCatalogue, false))
    {
        openLibrary();
    }
    studio_.tick();
    if (visible)
    {
        drawTargets();
        if (state_.statusIsError && state_.status.isNotEmpty())
            ImGui::TextColored(theme::vec(theme::palette().record), "%s", state_.status.toRawUTF8());
        const char *labels[] = {L("Périphériques", "Devices"), L("Graphe", "Graph"), L("Mixeur", "Mixer"), L("Routage", "Routing"),
                                L("Éditeur", "Editor"), L("Sous-pistes", "Subtracks")};
        if (state_.bringToFront == Panel::devices)
        {
            panel_ = 0;
            selectPanel_ = true;
            state_.bringToFront = Panel::none;
        }
        if (state_.bringToFront == Panel::graph)
        {
            panel_ = 1;
            selectPanel_ = true;
            state_.bringToFront = Panel::none;
        }

        if (requestedEditorFrames_ > 0)
        {
            --requestedEditorFrames_;
            panel_ = 4;
            selectPanel_ = true;
        }
        if(panel_==5&&!subtracks)panel_=0;
        const auto requestedPanel = panel_;
        if (ImGui::BeginTabBar("##modeTools"))
        {
            const auto hide = collapse && ui::bottomPanelHideTab();
            for (int i = 0; i < (subtracks ? 6 : 5); ++i)
                if (ImGui::BeginTabItem(labels[i], nullptr, selectPanel_ && requestedPanel == i ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    panel_ = i;
                    ImGui::BeginChild("##modeToolBody");
                    if (i == 0)
                        devices_->draw();
                    else if (i == 1)
                        graph_->draw();
                    else if (i == 2)
                        mixer_->draw();
                    else if (i == 3)
                        routing_->draw();
                    else if (i == 5 && subtracks)
                        subtracks();
                    else if (editor)
                        editor();
                    else
                        ImGui::TextDisabled("%s", L("Choisissez un son à éditer.", "Choose a sound to edit."));
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
            selectPanel_ = false;
            if (hide)
                collapse();
        }
    }
    editors_->draw();
    windows_->draw();
    if (state_.showLibrary)
    {
        if (!library_)
        {
            catalog_ = std::make_unique<SampleCatalog>(studio_.samples());
            library_ = std::make_unique<LibraryWindow>(studio_, state_, *host_, *catalog_);
        }
        library_->draw();
    }
    if(state_.showJsfx||jsfx_){if(!jsfx_)jsfx_=std::make_unique<JsfxView>(studio_,state_);jsfx_->draw();}
}
void ModeProcessing::openLibrary()
{
    state_.showLibrary = true;
    state_.libraryTab = 2;
    state_.libraryTarget = state_.selectedTrack;
    state_.libraryPick = true;
    state_.libraryFocus = true;
}

juce::var ModeProcessing::serialise() const
{
    std::set<std::uint64_t> live;
    const auto snapshot = [&](std::uint64_t key, const GraphSlot& slot) -> juce::var {
        live.insert(key);
        auto& cached = graphSnapshots_[key];
        if (cached.source != &slot || cached.revision != slot.revision()) {
            cached.state = slot.serialise();
            cached.source = &slot;
            cached.revision = slot.revision();
        }
        return cached.state;
    };
    auto state = object();
    put(state, "version", 1);
    juce::Array<juce::var> tracks;
    for (const auto &track : studio_.tracks())
    {
        auto t = object();
        const auto key = studio_.routingKey(track);
        put(t, "key", static_cast<juce::int64>(key));
        for (const auto &[id, k] : trackKeys_)
            if (k == key)
                put(t, "inputId", static_cast<juce::int64>(id));
        put(t, "name", studio_.trackName(track));
        put(t, "bus", studio_.isBusTrack(track));
        put(t, "midi", track.midi);
        if (const auto *slot = studio_.graphFor(track))
            put(t, "graph", snapshot(key, *slot));
        const auto mix = studio_.trackMix(track);
        put(t, "gain", mix.gainDb);
        put(t, "pan", mix.pan);
        put(t, "mute", mix.muted);
        put(t, "solo", mix.solo);
        juce::Array<juce::var> outputs;
        for (const auto &o : studio_.trackOutputs(track))
        {
            auto v = object();
            put(v, "destination", static_cast<juce::int64>(o.destination));
            put(v, "gain", o.gainDb);
            put(v, "input", static_cast<juce::int64>(o.input));
            put(v, "pre", o.preFader);
            outputs.add(v);
        }
        put(t, "outputs", outputs);
        tracks.add(t);
    }
    put(state, "tracks", tracks);
    put(state, "master", snapshot(0, *studio_.graphFor(Studio::masterTrack())));
    std::erase_if(graphSnapshots_, [&](const auto& item) { return !live.contains(item.first); });
    put(state, "masterGain", studio_.engine().masterGainDb());
    put(state, "masterMute", studio_.engine().isMasterMuted());
    return state;
}
void ModeProcessing::restore(const juce::var &state)
{
    if (!state.getDynamicObject())
        return;
    graphSnapshots_.clear();
    studio_.clearSession();
    trackKeys_.clear();
    midiSignatures_.clear();
    std::map<std::uint64_t, TrackRef> mapping;
    const auto *tracks = state.getProperty("tracks", {}).getArray();
    if (tracks)
        for (const auto &t : *tracks)
        {
            const auto id = static_cast<std::uint64_t>(num(t, "inputId"));
            std::optional<TrackRef> track;
            if (id)
            {
                ensureInput(id, t.getProperty("name", {}).toString(), static_cast<bool>(t.getProperty("midi", false)),
                            static_cast<bool>(t.getProperty("bus", false)));
                track = inputTrack(id);
            }
            else
                track = studio_.addBusTrack(t.getProperty("name", {}).toString());
            if (!track)
                continue;
            mapping[static_cast<std::uint64_t>(num(t, "key"))] = *track;
            if (auto *slot = studio_.graphFor(*track))
            {
                slot->restore(t.getProperty("graph", {}));
                (void)studio_.publishGraph(*track, true);
            }
            Studio::Mix mix;
            mix.gainDb = static_cast<float>(num(t, "gain"));
            mix.pan = static_cast<float>(num(t, "pan"));
            mix.muted = static_cast<bool>(t.getProperty("mute", false));
            mix.solo = static_cast<bool>(t.getProperty("solo", false));
            studio_.setTrackMix(*track, mix);
        }
    if (tracks)
        for (const auto &t : *tracks)
            if (auto it = mapping.find(static_cast<std::uint64_t>(num(t, "key"))); it != mapping.end())
            {
                std::vector<TrackOutput> outputs;
                if (const auto *list = t.getProperty("outputs", {}).getArray())
                    for (const auto &o : *list)
                    {
                        const auto key = static_cast<std::uint64_t>(num(o, "destination"));
                        if (key && !mapping.contains(key))
                            continue;
                        TrackOutput next;
                        next.destination = key ? studio_.routingKey(mapping.at(key)) : 0;
                        next.gainDb = static_cast<float>(num(o, "gain"));
                        next.input = static_cast<std::uint64_t>(num(o, "input"));
                        next.preFader = static_cast<bool>(o.getProperty("pre", false));
                        outputs.push_back(next);
                    }
                (void)studio_.setTrackOutputs(it->second, std::move(outputs));
            }
    if (auto master = state.getProperty("master", {}); master.getDynamicObject())
    {
        auto *slot = studio_.graphFor(Studio::masterTrack());
        slot->restore(master);
        (void)studio_.publishGraph(Studio::masterTrack(), true);
    }
    studio_.engine().setMasterGain(static_cast<float>(num(state, "masterGain")));
    studio_.engine().setMasterMuted(static_cast<bool>(state.getProperty("masterMute", false)));
}
std::uint64_t ModeProcessing::revision() const
{
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const auto &t : studio_.tracks())
    {
        mix(studio_.routingKey(t));
        mix(studio_.trackName(t).hashCode64());
        if (const auto *g = studio_.graphFor(t))
            mix(g->revision());
        const auto m = studio_.trackMix(t);
        mix(static_cast<std::uint64_t>(std::llround(m.gainDb * 10000)));
        mix(static_cast<std::uint64_t>(std::llround(m.pan * 10000)));
        mix(m.muted);
        mix(m.solo);
        for (const auto &o : studio_.trackOutputs(t))
        {
            mix(o.destination);
            mix(o.input);
            mix(static_cast<std::uint64_t>(std::llround(o.gainDb * 10000)));
            mix(o.preFader);
        }
    }
    mix(studio_.graphFor(Studio::masterTrack())->revision());
    mix(static_cast<std::uint64_t>(std::llround(studio_.engine().masterGainDb() * 10000)));
    mix(studio_.engine().isMasterMuted());
    return h;
}
juce::var ModeProcessing::command(const juce::var &request)
{
    const auto action = request.getProperty("op", "status").toString();
    juce::String error;
    auto track = Studio::masterTrack();
    const auto id = static_cast<std::uint64_t>(num(request, "inputId"));
    const auto key = static_cast<std::uint64_t>(num(request, "trackKey"));
    if (id)
    {
        const auto t = inputTrack(id);
        if (!t)
            error = "unknown input";
        else
            track = *t;
    }
    else if (key)
    {
        const auto t = studio_.trackForKey(key);
        if (!t)
            error = "unknown track";
        else
            track = *t;
    }
    if (error.isEmpty())
    {
        if (action == "instrument")
            error = studio_.replaceInstrument(track, request.getProperty("key", {}).toString()).getErrorMessage();
        else if (action == "add_effect")
            error = studio_.insertDevice(track, request.getProperty("key", {}).toString()).getErrorMessage();
        else if (action == "parameter")
        {
            if (!studio_.graphFor(track)->graph().editorSetParameter(request.getProperty("binding", {}).toString(), num(request, "value")))
                error = "unknown parameter";
        }
        else if (action == "bus")
        {
            if (auto bus = studio_.addBusTrack(request.getProperty("name", "Bus").toString()))
                state_.selectTrack(*bus);
            else
                error = "cannot create bus";
        }
        else if (action == "route")
        {
            TrackOutput out;
            out.destination = static_cast<std::uint64_t>(num(request, "destination"));
            out.gainDb = static_cast<float>(num(request, "gain"));
            out.input = static_cast<std::uint64_t>(num(request, "input"));
            out.preFader = static_cast<bool>(request.getProperty("pre", false));
            error = studio_.setTrackOutputs(track, {out}).getErrorMessage();
        }
        else if (action == "mix")
        {
            auto m = studio_.trackMix(track);
            m.gainDb = static_cast<float>(num(request, "gain", m.gainDb));
            m.pan = static_cast<float>(num(request, "pan", m.pan));
            m.muted = static_cast<bool>(request.getProperty("mute", m.muted));
            m.solo = static_cast<bool>(request.getProperty("solo", m.solo));
            studio_.setTrackMix(track, m);
        }
        else if (action == "select")
            state_.selectTrack(track);
        else if (action != "status")
            error = "unknown processing operation";
    }
    auto result = object();
    put(result, "ok", error.isEmpty());
    if (error.isNotEmpty())
    {
        put(result, "error", error);
        return result;
    }
    auto state = serialise();
    put(result, "result", state);
    return result;
}
engine::foundry::Sound ModeProcessing::render(const engine::foundry::Session &session, const juce::var &state, std::int64_t start,
                                              std::int64_t end, double tail)
{
    ModeProcessing processor;
    processor.restore(state);
    processor.syncFoundry(session);
    for (const auto &track : processor.studio_.tracks())
        if (track.midi)
        {
            const auto plan = processor.studio_.engine().midiTrackInstrumentGraph(track.midiId);
            if (plan.sampler)
                (void)app::SampleStore::instance().waitUntilReady(*plan.sampler, 5000);
        }
    processor.prepare(session.rate, 128);
    start = std::max<std::int64_t>(0, start);
    end = end > start ? std::min(end, session.frames()) : session.frames();
    const auto length = std::max<std::int64_t>(0, end - start) + static_cast<std::int64_t>(std::clamp(tail, 0.0, 30.0) * session.rate);
    std::vector<std::vector<float>> data(2, std::vector<float>(static_cast<std::size_t>(length)));
    // Processing an excerpt starts with fresh effect state, just like pressing
    // play there.
    for (std::int64_t i = 0; i < length; i += 128)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(128, length - i));
        const auto before = static_cast<int>(std::clamp<std::int64_t>(end - start - i, 0, n));
        if (before)
            processor.processFoundry(session, static_cast<double>(start + i), 1, data[0].data() + i, data[1].data() + i, before);
        if (before < n)
            processor.process(nullptr, 0, data[0].data() + i + before, data[1].data() + i + before, n - before);
    }
    return {std::move(data), session.rate};
}
} // namespace cydaw::v2
