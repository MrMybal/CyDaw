#include "NativeProcessor.h"
#include "NativeResources.h"
#include "ParameterBindings.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
namespace cydaw::native
{
namespace
{
// A bundle's voice data is read once per process, whatever instances play it.
std::shared_ptr<const engine::VoiceAssets> sharedVoiceAssets(const std::filesystem::path &file)
{
    if (file.empty())
        return {};
    static std::mutex lock;
    static std::map<std::filesystem::path, std::weak_ptr<const engine::VoiceAssets>> read;
    const std::scoped_lock guard(lock);
    if (auto known = read[file].lock())
        return known;
    std::ifstream in(file, std::ios::binary);
    std::string error;
    auto assets = engine::readVoiceAssets(in, error);
    if (assets.empty())
        return {};
    auto shared = std::make_shared<const engine::VoiceAssets>(std::move(assets));
    read[file] = shared;
    return shared;
}
} // namespace
struct NativeProcessor::Target
{
    int node{-1};
    std::optional<NativeParameterField> field;
    InstrumentParameterField instrument{InstrumentParameterField::none};
};
NativeProcessor::NativeProcessor()
    : definition_(makeDefinition()), graph_(makeAudioGraph()), instrument_(makeInstrumentPlan())
{
    values_ = std::make_unique<std::atomic<double>[]>(definition_.parameters.size());
    for (std::size_t i = 0; i < definition_.parameters.size(); ++i)
    {
        const auto &p = definition_.parameters[i];
        values_[i].store(p.initial);
        Target t;
        t.field = fieldForBinding(p.key);
        if (!t.field)
            t.field = fieldForBinding(p.binding);
        t.instrument = instrumentFieldForBinding(p.key);
        if (t.instrument == InstrumentParameterField::none)
            t.instrument = instrumentFieldForBinding(p.binding);
        for (std::size_t n = 0; n < graph_.nodes.size(); ++n)
            if (graph_.nodes[n].id == p.node)
                t.node = static_cast<int>(n);
        targets_.push_back(t);
    }
}
NativeProcessor::~NativeProcessor() = default;
void NativeProcessor::applyProgram(int index)
{
    if (index < 0 || index >= static_cast<int>(definition_.programs.size()))
        return;
    program_.store(index, std::memory_order_relaxed);
    const auto &preset = definition_.programs[index];
    const auto complete = preset.allValues.size() == definition_.parameters.size();
    const auto &values = complete ? preset.allValues : preset.values;
    std::size_t n = 0;
    for (std::size_t i = 0; i < definition_.parameters.size(); ++i)
    {
        const auto &p = definition_.parameters[i];
        if (!complete && !p.host)
            continue;
        if (n < values.size())
            values_[i].store(p.minimum + std::clamp(values[n], 0.0, 1.0) * (p.maximum - p.minimum),
                             std::memory_order_relaxed);
        ++n;
    }
}
int NativeProcessor::parameterIndex(uint32_t id) const
{
    for (std::size_t i = 0; i < definition_.parameters.size(); ++i)
        if (definition_.parameters[i].id == id)
            return static_cast<int>(i);
    return -1;
}
double NativeProcessor::value(const char *key) const
{
    for (std::size_t i = 0; i < definition_.parameters.size(); ++i)
        if (definition_.parameters[i].key == key)
            return values_[i].load(std::memory_order_relaxed);
    return 0;
}
void NativeProcessor::setParameter(uint32_t id, double v)
{
    if (!std::isfinite(v))
        return;
    int i = parameterIndex(id);
    if (i < 0)
        return;
    const auto &p = definition_.parameters[i];
    values_[i].store(std::clamp(v, p.minimum, p.maximum), std::memory_order_relaxed);
}
double NativeProcessor::parameterValue(uint32_t id) const
{
    int i = parameterIndex(id);
    return i >= 0 ? values_[i].load(std::memory_order_relaxed) : 0;
}
void NativeProcessor::edit(const char *key, double v)
{
    for (const auto &p : definition_.parameters)
        if (p.key == key)
        {
            setParameter(p.id, v);
            if (p.host && edited)
                edited(p.id, parameterValue(p.id));
            return;
        }
}
void NativeProcessor::applyValues()
{
    for (std::size_t i = 0; i < targets_.size(); ++i)
    {
        const auto v = static_cast<float>(values_[i].load(std::memory_order_relaxed));
        const auto &t = targets_[i];
        if (t.node >= 0 && t.field)
            applyParameter(graph_.nodes[t.node].parameters, *t.field, v);
        if (instrument_.enabled && t.instrument != InstrumentParameterField::none)
            applyInstrumentParameter(instrument_, t.instrument, v);
    }
    for (const auto &node : graph_.nodes)
        (void)runtime_.updateNodeParametersRealtime(node);
}
bool NativeProcessor::prepare(double rate, int maximumBlock)
{
    if (!std::isfinite(rate) || rate < 1 || maximumBlock < 1 || maximumBlock > 1048576)
        return false;
    rate_ = rate;
    maximumBlock_ = maximumBlock;
    int channels = 2;
    for (const auto &b : definition_.buses)
        if (!b.input)
            channels = b.channels;
    if (channels < 1 || channels > 8)
        return false;
    preparedChannels_ = channels;
    scratch_.assign(static_cast<std::size_t>(channels * maximumBlock_), 0);
    for (int c = 0; c < channels; ++c)
        pointers_[c] = scratch_.data() + c * maximumBlock_;
    loadVoice();
    runtime_.prepare(rate_, maximumBlock_, channels);
    const auto result = runtime_.setGraph(graph_);
    if (result.failed())
        return false;
    voices_.clear();
    notes_.clear();
    channels_.clear();
    ages_.clear();
    const auto count = instrument_.enabled ? std::clamp(instrument_.voiceCount, 1, 64) : 0;
    for (int i = 0; i < count; ++i)
    {
        auto voice = std::make_unique<engine::PolyVoice>();
        voice->prepare(rate_);
        voice->setPrismContext(&prismContext_);
        voices_.push_back(std::move(voice));
        notes_.push_back(-1);
        channels_.push_back(0);
        ages_.push_back(0);
    }
    std::vector<NativeDspRuntime::TapRequest> taps{{graph_.outputNodeId, false}};
    for (const auto &c : definition_.controls)
        if (c.kind == "native-panel" && !c.extra.empty())
        {
            for (const auto &p : definition_.parameters)
                if (p.key == c.extra[0])
                {
                    if (std::none_of(taps.begin(), taps.end(),
                                     [&](const auto &t) { return t.nodeId == p.node; }))
                    {
                        if (taps.size() + 2 <= NativeDspRuntime::tapCount)
                        {
                            taps.push_back({p.node, true});
                            taps.push_back({p.node, false});
                        }
                    }
                    break;
                }
        }
    runtime_.setTaps(taps);
    midiEffects_.prepare(rate_);
    applyValues();
    reset();
    return true;
}
void NativeProcessor::loadVoice()
{
    // Once, before the first block: a sampler's samples, a sound model.
    if (voiceTried_)
        return;
    voiceTried_ = true;
    if (!definition_.voiceAssets.empty())
        voice_ = sharedVoiceAssets(findResource(definition_.voiceAssets));
    if (voice_ != nullptr)
    {
        if (voice_->sampler != nullptr)
            instrument_.sampler = voice_->sampler;
        if (voice_->soundModel != nullptr)
            instrument_.soundModel = voice_->soundModel;
    }
    // Each Replacer's instrument: its own samples, onto its plan.
    for (const auto &replacer : definition_.replacerVoices)
    {
        auto assets = sharedVoiceAssets(findResource(replacer.assets));
        if (assets == nullptr)
            continue;
        for (auto &node : graph_.nodes)
            if (node.id == replacer.node && node.instrument != nullptr)
            {
                auto plan = std::make_shared<engine::PolySynthGraphPlan>(*node.instrument);
                if (assets->sampler != nullptr)
                    plan->sampler = assets->sampler;
                if (assets->soundModel != nullptr)
                    plan->soundModel = assets->soundModel;
                node.instrument = std::move(plan);
            }
        replacerVoices_.push_back(std::move(assets));
    }
}
void NativeProcessor::reset()
{
    runtime_.resetRealtime();
    for (auto &v : voices_)
        v->clear();
    std::fill(notes_.begin(), notes_.end(), -1);
    midiEffects_.reset();
    std::fill(std::begin(bends_), std::end(bends_), 0);
    std::memset(sustain_, 0, sizeof(sustain_));
    std::memset(deferred_, 0, sizeof(deferred_));
    down_.fill(false);
    downCount_ = 0;
    controlNotes_.held = 0;
}
void NativeProcessor::midi(const engine::MidiEffectEvent &e)
{
    const auto channel = e.channel(), note = static_cast<int>(e.data1);
    const auto type = e.status & 0xf0;
    if (e.isNoteOn())
    {
        noteOn(channel, note, e.data2 / 127.0f);
        ++controlNotes_.noteOns;
        controlNotes_.note = static_cast<float>(note);
        controlNotes_.velocity = e.data2 / 127.0f;
    }
    else if (e.isNoteOff())
    {
        if (note >= 0 && note < 128 && down_[note])
        {
            down_[note] = false;
            --downCount_;
        }
        if (sustain_[channel])
            deferred_[note][channel] = true;
        else
            for (std::size_t i = 0; i < voices_.size(); ++i)
                if (notes_[i] == note && channels_[i] == channel)
                    voices_[i]->stopNote(true);
    }
    else if (e.isAllNotesOff())
    {
        down_.fill(false);
        downCount_ = 0;
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (channels_[i] == channel)
                voices_[i]->stopNote(e.data1 != 120);
    }
    else if (type == 0xb0)
    {
        // A sampler's SFZ reads every controller, but a note's own
        // expression (11 and 43 on the channels after the first).
        if (channel == 0 || (e.data1 != 11 && e.data1 != 43))
            prismContext_.sampler.controllerMoved(e.data1, e.data2);
        controlNotes_.controllers[e.data1 & 127] = e.data2 / 127.0f;
        if (e.data1 == 1)
            controlNotes_.modWheel = e.data2 / 127.0f;
        if (e.data1 == 64)
        {
            sustain_[channel] = e.data2 >= 64;
            if (!sustain_[channel])
                for (std::size_t i = 0; i < voices_.size(); ++i)
                    if (channels_[i] == channel && notes_[i] >= 0 && deferred_[notes_[i]][channel])
                    {
                        voices_[i]->stopNote(true);
                        deferred_[notes_[i]][channel] = false;
                    }
        }
        else if (e.data1 == 1)
            prismContext_.modWheel = e.data2 / 127.0f;
    }
    else if (type == 0xe0)
    {
        bends_[channel] = ((e.data1 | (e.data2 << 7)) - 8192) / 8192.0f * 2;
        controlNotes_.pitchBend = bends_[channel] * 0.5f;
        for (std::size_t i = 0; i < voices_.size(); ++i)
            if (channels_[i] == channel)
                voices_[i]->setExpression(bends_[channel], 0);
    }
    else if (type == 0xd0)
    {
        prismContext_.aftertouch = e.data1 / 127.0f;
        controlNotes_.aftertouch = e.data1 / 127.0f;
    }
    controlNotes_.held = downCount_;
}
void NativeProcessor::noteOn(const int channel, const int note, const float velocity)
{
    const auto transposed = std::clamp(note + instrument_.transposeSemitones, 0, 127);
    const auto *sampler =
        instrument_.waveform == engine::PolyOscillatorWaveform::sampler ? instrument_.sampler.get() : nullptr;
    // A keyswitch: it plays nothing, the next notes play its articulation.
    if (sampler != nullptr && engine::samplerKeyswitch(*sampler, transposed))
    {
        prismContext_.sampler.keyswitch = transposed;
        return;
    }
    // The keys held as it starts (a sampler's first and legato zones).
    prismContext_.sampler.heldKeys = downCount_;
    if (note >= 0 && note < 128 && !down_[note])
    {
        down_[note] = true;
        ++downCount_;
    }
    // A closed hat cuts the open one: the voices its group chokes go first.
    auto group = 0;
    if (sampler != nullptr)
    {
        const auto crossfade =
            engine::samplerValue(instrument_.samplerValues, engine::SamplerSetting::velocityCrossfade) >= 0.5f;
        const engine::SamplerCues cues{&instrument_.samplerControllers, &prismContext_.sampler,
                                       engine::samplerValue(instrument_.samplerValues, engine::SamplerSetting::articulation)};
        group = engine::samplerGroupOf(*sampler, transposed,
                                       std::clamp(static_cast<double>(velocity) * instrument_.velocityScale, 0.0, 1.0),
                                       crossfade, cues);
    }
    else if (instrument_.waveform == engine::PolyOscillatorWaveform::family &&
             instrument_.synthModel == engine::synth::Model::silex && transposed != engine::synth::silexOpenHat)
        group = engine::synth::silexChokeGroupOf(transposed);
    if (group != 0)
        for (auto &voice : voices_)
            if (voice->isActive() && voice->chokedBy(instrument_) == group)
                voice->fadeOut(0.006);
    {
        std::size_t chosen = 0;
        bool found = false;
        if (instrument_.voiceMode != 0)
        {
            chosen = 0;
            found = true;
        }
        for (std::size_t i = 0; !found && i < voices_.size(); ++i)
            if (!voices_[i]->isActive())
            {
                chosen = i;
                found = true;
            }
        if (!found)
            for (std::size_t i = 1; i < voices_.size(); ++i)
                if (ages_[i] < ages_[chosen])
                    chosen = i;
        if (voices_.empty())
            return;
        const auto previous = notes_[chosen];
        const auto legato = instrument_.voiceMode == 2 && voices_[chosen]->isActive();
        voices_[chosen]->startNote(instrument_, note, velocity, rate_,
                                   instrument_.glideSeconds > 0 ? previous : -1, legato);
        voices_[chosen]->setExpression(bends_[channel], 0);
        notes_[chosen] = note;
        channels_[chosen] = channel;
        ages_[chosen] = ++age_;
        deferred_[note][channel] = false;
    }
}
void NativeProcessor::renderVoices(float *const *channels, int count, int start, int frames)
{
    if (frames <= 0)
        return;
    auto plan = instrument_;
    for (const auto &route : definition_.voiceModulations)
    {
        float value{};
        runtime_.controlValuesNow(&route.controlNodeId, &value, 1);
        if (!std::isfinite(value))
            continue;
        if (route.depth != 0.0f)
            engine::modulatePolyVoice(plan, route.target, value, route.depth, route.index, route.minimum,
                                      route.maximum, route.logarithmic);
        else
            engine::applyPolyVoiceModulation(plan, route.target, value, route.index);
    }
    for (auto &v : voices_)
        if (v->isActive())
            v->render(plan, channels, count, start, frames, rate_);
}
bool NativeProcessor::process(NativeBlock &data)
{
    applyValues();
    const int nc = data.channels, frames = data.frames;
    if (frames == 0)
        return true;
    if (frames < 0 || frames > maximumBlock_ || nc < 1 || nc > preparedChannels_ || !data.output)
        return false;
    auto *const *output = pointers_.data();
    for (int c = 0; c < nc; ++c)
    {
        if (!instrument_.enabled && !data.inputs.empty() && data.inputs[0].count > 0 &&
            data.inputs[0].channels && data.inputs[0].channels[std::min(c, data.inputs[0].count - 1)])
            std::copy_n(data.inputs[0].channels[std::min(c, data.inputs[0].count - 1)], frames, output[c]);
        else
            std::fill_n(output[c], frames, 0.f);
    }
    auto incoming = std::min(data.midi.size(), midiIn_.size());
    std::copy_n(data.midi.begin(), incoming, midiIn_.begin());
    std::sort(midiIn_.begin(), midiIn_.begin() + incoming,
              [](auto a, auto b) { return a.offset < b.offset; });
    auto clock = data.clock;
    clock.sampleRate = rate_;
    prismContext_.clockSeconds = data.seconds;
    prismContext_.tempo = clock.beatsPerMinute;
    NativeDspRuntime::setSessionTempo(clock.beatsPerMinute);
    NativeDspRuntime::setSessionPlaying(clock.playing);
    // A sampler's controllers and articulation follow its card, once a block.
    if (instrument_.enabled && instrument_.waveform == engine::PolyOscillatorWaveform::sampler)
        prismContext_.sampler.follow(instrument_.sampler.get(), instrument_.samplerControllers,
                                     engine::samplerValue(instrument_.samplerValues, engine::SamplerSetting::articulation));
    const auto count = midiEffects_.process(midiIn_.data(), incoming, midiOut_.data(), midiOut_.size(),
                                            frames, clock, instrument_.midiEffects.get());
    int position = 0;
    data.midiCount = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto &e = midiOut_[i];
        int at = std::clamp<int>(e.offset, position, frames);
        if (instrument_.enabled)
        {
            renderVoices(output, nc, position, at - position);
            midi(e);
        }
        position = at;
        if (definition_.producesMidi && data.midiOutput && data.midiCount < data.midiCapacity)
            data.midiOutput[data.midiCount++] = e;
    }
    if (instrument_.enabled)
        renderVoices(output, nc, position, frames - position);
    std::array<PlanarBuffer, NativeDspRuntime::maximumExtraInputs> extra;
    std::array<const PlanarBuffer *, NativeDspRuntime::maximumExtraInputs> ptrs{};
    for (size_t i = 1; i < data.inputs.size() && i <= extra.size(); ++i)
        if (data.inputs[i].channels)
        {
            extra[i - 1] = {data.inputs[i].channels, data.inputs[i].count, frames};
            ptrs[i - 1] = &extra[i - 1];
        }
    runtime_.setExtraInputs(ptrs.data(), ptrs.size());
    runtime_.setControlNotes(controlNotes_);
    PlanarBuffer buffer{output, nc, frames};
    if (!runtime_.process(buffer, 0, frames, data.seconds))
        for (int c = 0; c < nc; ++c)
            std::fill_n(output[c], frames, 0.f);
    for (int c = 0; c < nc; ++c)
        std::copy_n(output[c], frames, data.output[c]);
    prismContext_.clockSeconds += frames / rate_;
    return true;
}
std::vector<uint8_t> NativeProcessor::save() const
{
    std::vector<uint8_t> bytes;
    auto write = [&](auto v)
    {
        auto *p = reinterpret_cast<const uint8_t *>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(v));
    };
    write(uint32_t(0x43594e31));
    write(uint32_t(1));
    write(uint32_t(definition_.parameters.size() + 1));
    for (size_t i = 0; i < definition_.parameters.size(); ++i)
    {
        write(definition_.parameters[i].id);
        write(values_[i].load());
    }
    write(programId);
    write(double(program_.load()));
    return bytes;
}
bool NativeProcessor::load(std::span<const uint8_t> bytes)
{
    if (bytes.size() < 12)
        return false;
    size_t offset = 0;
    auto read = [&](auto &v)
    {
        if (offset + sizeof(v) > bytes.size())
            return false;
        std::memcpy(&v, bytes.data() + offset, sizeof(v));
        offset += sizeof(v);
        return true;
    };
    uint32_t magic, version, count;
    read(magic);
    read(version);
    read(count);
    if (magic != 0x43594e31 || version != 1 || count > 65536 || bytes.size() != 12 + size_t(count) * 12)
        return false;
    std::vector<std::pair<uint32_t, double>> pending;
    for (uint32_t n = 0; n < count; ++n)
    {
        uint32_t id;
        double v;
        read(id);
        read(v);
        if (!std::isfinite(v))
            return false;
        pending.emplace_back(id, v);
    }
    for (auto [id, v] : pending)
        if (id == programId)
            program_ =
                int(std::clamp(v, 0.0, double(std::max<int>(0, int(definition_.programs.size()) - 1))));
        else
            setParameter(id, v);
    return true;
}
} // namespace cydaw::native
