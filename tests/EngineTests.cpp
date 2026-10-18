#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/AudioRenderPlan.h>
#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/ChannelLayout.h>
#include <cydaw/engine/NativeDspCompiler.h>
#include <cydaw/engine/NativeDspParameters.h>
#include <cydaw/engine/PolyVoice.h>
#include <cydaw/engine/PolyVoiceFilter.h>
#include <cydaw/engine/PolyVoiceOscillator.h>
#include <cydaw/engine/SoundModel.h>
#include <cydaw/engine/VoiceAssetsFile.h>
#include <cydaw/control/Control.h>
#include <cydaw/engine/PrismSynth.h>
#include <cydaw/engine/EffectProcessors.h>
#include <cydaw/engine/ProEffectsDesign.h>
#include <cydaw/engine/KickSynth.h>
#include <cydaw/engine/DrumSynth.h>
#include <cydaw/engine/AmpDesign.h>
#include <cydaw/engine/HaloDesign.h>
#include <cydaw/engine/MidiEffects.h>
#include <cydaw/engine/PitchTracker.h>
#include <cydaw/engine/VoiceDesign.h>
#include <cydaw/engine/PitchDesign.h>
#include <cydaw/engine/SpaceDesign.h>
#include <cydaw/engine/RhythmDesign.h>
#include <cydaw/engine/MotionDesign.h>
#include <cydaw/engine/ResonanceDesign.h>
#include <cydaw/engine/UtilityDesign.h>
#include <cydaw/engine/MixDesign.h>
#include <cydaw/engine/DrumKit.h>
#include <cydaw/engine/SynthFamily.h>
#include <cydaw/engine/Convolver.h>
#include <cydaw/engine/TimeStretch.h>
#include <cydaw/engine/DjMix.h>
#include <cydaw/engine/FoundrySound.h>
#include <cydaw/engine/FoundrySession.h>
#include <cydaw/engine/Loudness.h>
#include <algorithm>
#include <numbers>

#include <tuple>
#include <vector>
#include <cydaw/graph/PrimitiveRegistry.h>
#include <cydaw/engine/RealtimeEngine.h>
#include <cydaw/engine/Transport.h>
#include <cydaw/realtime/RtPublisher.h>
#include <cydaw/realtime/SpscRing.h>

#include <array>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <thread>
#include <vector>

namespace allocation_probe {
std::atomic<bool> enabled {};
std::atomic<std::size_t> count {};
}

void* operator new(const std::size_t size)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
        allocation_probe::count.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc {};
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool approximatelyEqual(const float left, const float right, const float tolerance = 1.0e-5f)
{
    return std::abs(left - right) <= tolerance;
}

void spscRingIsBoundedAndFifo()
{
    cydaw::realtime::SpscRing<int, 4> queue;
    expect(queue.tryPush(10), "queue accepts first value");
    expect(queue.tryPush(20), "queue accepts second value");
    expect(queue.tryPush(30), "queue accepts third value");
    expect(queue.tryPush(40), "queue uses its full declared capacity");
    expect(!queue.tryPush(50), "queue rejects overflow without blocking");

    int value = 0;
    expect(queue.tryPop(value) && value == 10, "queue preserves FIFO order");
    expect(queue.tryPop(value) && value == 20, "queue pops second value");
    expect(queue.tryPush(50), "queue wraps after space is released");
    expect(queue.tryPop(value) && value == 30, "queue pops third value after wrap");
    expect(queue.tryPop(value) && value == 40, "queue pops fourth value after wrap");
    expect(queue.tryPop(value) && value == 50, "queue pops wrapped value");
    expect(!queue.tryPop(value), "empty queue returns immediately");
}

void transportUsesSamplePositions()
{
    cydaw::engine::TransportClock transport;
    transport.prepare(48'000.0);
    expect(transport.setTempo(120.0), "valid tempo is accepted");
    transport.play();
    transport.advance(24'000);
    auto snapshot = transport.snapshot();
    expect(snapshot.samplePosition == 24'000, "transport advances in integer samples");
    expect(std::abs(snapshot.quarterNotePosition - 1.0) < 1.0e-12, "24000 samples at 120 BPM is one quarter note");

    expect(transport.setTempo(60.0), "tempo can change while playing");
    expect(std::abs(transport.snapshot().quarterNotePosition - 1.0) < 1.0e-12,
           "tempo change does not reinterpret the elapsed timeline");
    transport.advance(48'000);
    expect(std::abs(transport.snapshot().quarterNotePosition - 2.0) < 1.0e-12,
           "transport integrates the new tempo from the change onward");

    transport.stop();
    transport.advance(256);
    expect(transport.snapshot().samplePosition == 72'000, "stopped transport does not advance");
    transport.seek(-10);
    expect(transport.snapshot().samplePosition == 0, "negative seek is clamped to project start");
    expect(!transport.setTempo(0.0), "invalid tempo is rejected");
}

void renderPlanProcessesBranchesAndMixes()
{
    using namespace cydaw::engine;
    const ProcessSpec spec { 48'000.0, 64, 2 };
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<PassthroughProcessor>(), true) == RenderPlanError::none, "add external input node");
    expect(builder.addNode(2, std::make_unique<GainProcessor>(0.25f)) == RenderPlanError::none, "add first branch");
    expect(builder.addNode(3, std::make_unique<GainProcessor>(0.75f)) == RenderPlanError::none, "add second branch");
    expect(builder.addNode(4, std::make_unique<PassthroughProcessor>()) == RenderPlanError::none, "add mix output");
    expect(builder.connect(1, 2) == RenderPlanError::none, "connect first branch input");
    expect(builder.connect(1, 3) == RenderPlanError::none, "connect second branch input");
    expect(builder.connect(2, 4) == RenderPlanError::none, "connect first branch output");
    expect(builder.connect(3, 4) == RenderPlanError::none, "connect second branch output");
    auto compiled = builder.compile(spec, 4);
    expect(static_cast<bool>(compiled), "branching render plan compiles");

    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            input.view(64).channel(channel)[frame] = static_cast<float>(frame + 1) / 64.0f;

    compiled.plan->process(input.view(64), output.view(64), {});
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            expect(approximatelyEqual(output.view(64).channel(channel)[frame], input.view(64).channel(channel)[frame]),
                   "parallel gains sum back to unity");
}

// A processor that is late by a fixed number of samples, and says so.
class LateProcessor final : public cydaw::engine::AudioProcessor {
public:
    explicit LateProcessor(const std::uint32_t latency) : latency_(latency) {}
    void prepare(const cydaw::engine::ProcessSpec& spec) override
    {
        ring_.assign(static_cast<std::size_t>(spec.channelCount) * (latency_ + 1), 0.0f);
        channels_ = spec.channelCount;
    }
    void reset() noexcept override { std::fill(ring_.begin(), ring_.end(), 0.0f); }
    void process(const cydaw::engine::AudioBlockView input, const cydaw::engine::AudioBlockView output,
                 const cydaw::engine::ProcessContext&) noexcept override
    {
        const auto size = latency_ + 1;
        for (std::uint32_t channel = 0; channel < std::min(channels_, output.channelCount); ++channel)
            for (std::uint32_t frame = 0; frame < output.frameCount; ++frame) {
                auto* ring = ring_.data() + static_cast<std::size_t>(channel) * size;
                const auto at = (write_ + frame) % size;
                ring[at] = input.channel(channel)[frame];
                output.channel(channel)[frame] = ring[(at + 1) % size];
            }
        write_ = (write_ + output.frameCount) % size;
    }
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return latency_; }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return latency_; }

private:
    std::uint32_t latency_ {};
    std::uint32_t channels_ { 2 };
    std::uint32_t write_ {};
    std::vector<float> ring_;
};

void renderPlanAlignsParallelLatencies()
{
    // A dry path and a late one into the same node: the dry one waits, so an
    // impulse comes out once, doubled, as late as the late path.
    using namespace cydaw::engine;
    const ProcessSpec spec { 48'000.0, 64, 2 };
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<PassthroughProcessor>(), true) == RenderPlanError::none, "latency: input");
    expect(builder.addNode(2, std::make_unique<LateProcessor>(37)) == RenderPlanError::none, "latency: late branch");
    expect(builder.addNode(3, std::make_unique<PassthroughProcessor>()) == RenderPlanError::none, "latency: dry branch");
    expect(builder.addNode(4, std::make_unique<PassthroughProcessor>()) == RenderPlanError::none, "latency: mix");
    expect(builder.addNode(5, std::make_unique<LateProcessor>(11)) == RenderPlanError::none, "latency: after the mix");
    expect(builder.connect(1, 2) == RenderPlanError::none && builder.connect(1, 3) == RenderPlanError::none
               && builder.connect(2, 4) == RenderPlanError::none && builder.connect(3, 4) == RenderPlanError::none
               && builder.connect(4, 5) == RenderPlanError::none,
           "latency: connections");
    auto compiled = builder.compile(spec, 5);
    expect(static_cast<bool>(compiled), "latency: the plan compiles");
    if (!compiled)
        return;
    expect(compiled.plan->latencySamples() == 48, "latency: the slowest path is the graph's latency (37 + 11)");

    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);
    std::vector<float> heard;
    for (int block = 0; block < 4; ++block) {
        input.clear(64);
        if (block == 0)
            for (std::uint32_t channel = 0; channel < 2; ++channel)
                input.view(64).channel(channel)[10] = 1.0f;
        allocation_probe::count.store(0);
        allocation_probe::enabled.store(true);
        compiled.plan->process(input.view(64), output.view(64), {});
        allocation_probe::enabled.store(false);
        expect(allocation_probe::count.load() == 0, "latency: aligning paths allocates nothing");
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            heard.push_back(output.view(64).channel(0)[frame]);
    }
    for (std::size_t frame = 0; frame < heard.size(); ++frame)
        expect(approximatelyEqual(heard[frame], frame == 10 + 48 ? 2.0f : 0.0f),
               "latency: the impulse comes out once, both paths together, 48 samples late");

    // The studio effects that look ahead say by how much.
    const auto lateBy = [](const NativeDspNodeKind kind, const char* key, const float value) {
        NativeDspNode node;
        node.id = 1;
        node.kind = kind;
        if (const auto* descriptor = effectForKind(kind))
            applyEffectDefaults(*descriptor, node.parameters);
        if (const auto* parameter = effectParameterForSuffix(key))
            setEffectValue(node.parameters, *parameter, value);
        auto processor = makeEffectProcessor(node);
        if (processor == nullptr)
            return -1;
        processor->prepare({ 48'000.0, 64, 2 });
        return static_cast<int>(processor->latencySamples());
    };
    expect(lateBy(NativeDspNodeKind::compressorPro, "comppro.lookahead", 5.0f) == 240,
           "latency: a 5 ms look-ahead compressor is 240 samples late at 48 kHz");
    expect(lateBy(NativeDspNodeKind::compressorPro, "comppro.lookahead", 0.0f) == 0,
           "latency: no look-ahead, no latency");
    expect(lateBy(NativeDspNodeKind::limiterPro, "limpro.lookahead", 1.0f) == 52,
           "latency: the limiter's 1 ms look-ahead and its four samples of interpolation");
    expect(lateBy(NativeDspNodeKind::ampSim, "amp.oversampling", 2.0f) == 16, "latency: the oversampled amp");
    expect(lateBy(NativeDspNodeKind::ampSim, "amp.oversampling", 0.0f) == 0, "latency: the amp at its own rate");
}

void dynamicsListenToTheirSidechain()
{
    // A steady sound through a compressor (and a gate) whose sidechain is
    // wired to a second input: silent there, nothing happens; loud there,
    // the compressor pulls the sound down and the gate opens.
    using namespace cydaw::engine;
    const auto run = [](const NativeDspNodeKind kind, const std::vector<std::pair<const char*, float>>& settings,
                        const float keyLevel) {
        NativeDspGraph graph;
        NativeDspNode main;
        main.id = 1;
        main.kind = NativeDspNodeKind::input;
        NativeDspNode key;
        key.id = 2;
        key.kind = NativeDspNodeKind::input;
        key.inputBus = 1;
        NativeDspNode effect;
        effect.id = 3;
        effect.kind = kind;
        if (const auto* descriptor = effectForKind(kind))
            applyEffectDefaults(*descriptor, effect.parameters);
        for (const auto& [name, value] : settings)
            if (const auto* parameter = effectParameterForSuffix(name))
                setEffectValue(effect.parameters, *parameter, value);
        NativeDspNode out;
        out.id = 4;
        out.kind = NativeDspNodeKind::output;
        graph.nodes = { main, key, effect, out };
        graph.connections = { { 1, 3, 0, 1 }, { 2, 3, 0, effectSidechainPortId }, { 3, 4, 0, 0 } };
        graph.outputNodeId = 4;
        auto compiled = compileNativeDspGraph(graph, { 48'000.0, 256, 2 });
        if (!compiled)
            return -1.0;
        AudioBuffer input(2, 256);
        AudioBuffer side(2, 256);
        AudioBuffer output(2, 256);
        auto phase = 0.0;
        auto sum = 0.0;
        auto count = 0;
        for (int block = 0; block < 40; ++block) {
            for (std::uint32_t frame = 0; frame < 256; ++frame) {
                const auto sample = static_cast<float>(0.25 * std::sin(phase));
                const auto kick = static_cast<float>(keyLevel * std::sin(phase * 0.3));
                phase += 2.0 * std::numbers::pi * 220.0 / 48'000.0;
                for (std::uint32_t channel = 0; channel < 2; ++channel) {
                    input.view(256).channel(channel)[frame] = sample;
                    side.view(256).channel(channel)[frame] = kick;
                }
            }
            const auto extra = side.view(256);
            compiled.plan->process(input.view(256), &extra, 1, output.view(256), {});
            if (block >= 30)
                for (std::uint32_t frame = 0; frame < 256; ++frame) {
                    const auto value = output.view(256).channel(0)[frame];
                    sum += static_cast<double>(value) * value;
                    ++count;
                }
        }
        return std::sqrt(sum / std::max(1, count));
    };
    const auto steady = 0.25 / std::sqrt(2.0);
    const std::vector<std::pair<const char*, float>> compressor { { "comppro.threshold", -40.0f }, { "comppro.ratio", 20.0f },
                                                                  { "comppro.knee", 0.0f }, { "comppro.attack", 0.05f } };
    const auto quiet = run(NativeDspNodeKind::compressorPro, compressor, 0.0f);
    const auto ducked = run(NativeDspNodeKind::compressorPro, compressor, 0.8f);
    expect(quiet > 0.0 && std::abs(quiet - steady) < steady * 0.05,
           "sidechain: a compressor whose key is silent leaves a sound it would compress alone");
    expect(ducked > 0.0 && ducked < steady * 0.1, "sidechain: a loud key pulls the compressor's sound down by 20 dB and more");
    // Every effect of the catalogue is a valid primitive (a port key used
    // twice makes one invalid, and its cards lose every socket), and those
    // that listen to a sidechain have its audio input.
    for (const auto& effect : effectCatalogue) {
        const auto* descriptor = cydaw::graph::factoryPrimitiveRegistry().find(std::string(effect.processorKey));
        expect(descriptor != nullptr, "sidechain: every catalogue effect is registered as a primitive");
        if (descriptor == nullptr || !effect.sidechain)
            continue;
        const auto found = std::ranges::find_if(descriptor->ports, [](const auto& port) {
            return port.id == effectSidechainPortId && port.type == cydaw::graph::PortType::audio
                   && port.direction == cydaw::graph::PortDirection::input;
        });
        expect(found != descriptor->ports.end(), "sidechain: the effect's primitive has the sidechain input");
    }
    const std::vector<std::pair<const char*, float>> gate { { "gate.threshold", -20.0f }, { "gate.range", -80.0f } };
    const auto closed = run(NativeDspNodeKind::gate, gate, 0.0f);
    const auto opened = run(NativeDspNodeKind::gate, gate, 0.8f);
    expect(closed >= 0.0 && closed < steady * 0.01, "sidechain: a gate whose key is silent stays shut on a loud sound");
    expect(std::abs(opened - steady) < steady * 0.05, "sidechain: a loud key opens the gate");
}

void equaliserBandsMoveWithTheSound()
{
    // A dynamic bell at 1 kHz (range -12 dB over -30 dB): a loud tone there
    // is cut by its range, a quiet one passes; a range upward lifts the loud
    // tone; a static band does nothing of it. What the processor shows is
    // what it did. Keyed on the sidechain, the band follows the key, not the
    // sound.
    using namespace cydaw::engine;
    const auto settingsFor = [](const float range, const bool dynamic, const float detector, const float threshold) {
        NativeDspNodeParameters parameters;
        if (const auto* descriptor = effectForKind(NativeDspNodeKind::parametricEq))
            applyEffectDefaults(*descriptor, parameters);
        const auto set = [&parameters](const EffectParameter* parameter, const float value) {
            if (parameter != nullptr)
                setEffectValue(parameters, *parameter, value);
        };
        set(effectParameterForSuffix("eqpro.b1Enabled"), 1.0f);
        set(effectParameterForSuffix("eqpro.b1Type"), 0.0f);
        set(effectParameterForSuffix("eqpro.b1Frequency"), 1'000.0f);
        set(effectParameterForSuffix("eqpro.b1Gain"), 0.0f);
        set(effectParameterForSuffix("eqpro.b1Q"), 1.0f);
        set(effectParameterForSuffix("eqpro.b1Dynamic"), dynamic ? 1.0f : 0.0f);
        set(effectParameterForSuffix("eqpro.b1Range"), range);
        set(effectParameterForSuffix("eqpro.b1Threshold"), threshold);
        set(effectParameterForSuffix("eqpro.b1Attack"), 1.0f);
        set(effectParameterForSuffix("eqpro.b1Release"), 50.0f);
        set(effectParameterForSuffix("eqpro.b1Detector"), detector);
        return parameters;
    };
    // The tone's level out over its level in (dB), once settled, and what
    // the processor shows of band 1: its gain moved, its detector's level.
    struct Heard {
        double changeDb {};
        float movedDb {};
        float levelDb {};
    };
    const auto play = [](const NativeDspNodeParameters& parameters, const double amplitude) {
        NativeDspNode node;
        node.id = 1;
        node.kind = NativeDspNodeKind::parametricEq;
        node.parameters = parameters;
        auto processor = makeEffectProcessor(node);
        Heard heard;
        if (processor == nullptr)
            return heard;
        processor->prepare({ 48'000.0, 256, 2 });
        AudioBuffer input(2, 256);
        AudioBuffer output(2, 256);
        auto phase = 0.0;
        auto in = 0.0;
        auto out = 0.0;
        for (int block = 0; block < 60; ++block) {
            for (std::uint32_t frame = 0; frame < 256; ++frame) {
                const auto sample = static_cast<float>(amplitude * std::sin(phase));
                phase += 2.0 * std::numbers::pi * 1'000.0 / 48'000.0;
                input.view(256).channel(0)[frame] = sample;
                input.view(256).channel(1)[frame] = sample;
            }
            processor->process(input.view(256), output.view(256), {});
            if (block >= 40)
                for (std::uint32_t frame = 0; frame < 256; ++frame) {
                    in += static_cast<double>(input.view(256).channel(0)[frame]) * input.view(256).channel(0)[frame];
                    out += static_cast<double>(output.view(256).channel(0)[frame]) * output.view(256).channel(0)[frame];
                }
        }
        heard.changeDb = 10.0 * std::log10(std::max(out, 1.0e-20) / std::max(in, 1.0e-20));
        std::array<float, eqProReadingCount> shown {};
        if (processor->readings(shown.data(), shown.size()) == shown.size()) {
            heard.movedDb = shown[0];
            heard.levelDb = shown[eqProBands];
        }
        return heard;
    };
    const auto loud = play(settingsFor(-12.0f, true, 0.0f, -30.0f), 0.5);
    expect(std::abs(loud.changeDb + 12.0) < 1.0, "dynamic EQ: a loud tone in a dynamic band is cut by its range");
    expect(std::abs(loud.movedDb + 12.0f) < 0.5f, "dynamic EQ: the processor shows the band's gain moved by its range");
    expect(std::abs(loud.levelDb + 6.0f) < 1.5f, "dynamic EQ: the detector hears the tone's level in its band");
    const auto quiet = play(settingsFor(-12.0f, true, 0.0f, -30.0f), 0.01);
    expect(std::abs(quiet.changeDb) < 0.2, "dynamic EQ: a quiet tone, under the threshold, passes");
    expect(std::abs(quiet.movedDb) < 0.05f, "dynamic EQ: under the threshold, nothing moved");
    const auto lifted = play(settingsFor(6.0f, true, 0.0f, -30.0f), 0.5);
    expect(std::abs(lifted.changeDb - 6.0) < 1.0, "dynamic EQ: a range upward lifts a loud tone");
    const auto still = play(settingsFor(-12.0f, false, 0.0f, -30.0f), 0.5);
    expect(std::abs(still.changeDb) < 0.2, "dynamic EQ: a static band leaves the tone it would cut");
    expect(still.movedDb == 0.0f && still.levelDb <= -119.0f, "dynamic EQ: a static band shows nothing moving");

    // Keyed: the sound alone stays under the threshold, the key over it.
    const auto keyed = [&settingsFor](const float detector, const float keyLevel) {
        NativeDspGraph graph;
        NativeDspNode main;
        main.id = 1;
        main.kind = NativeDspNodeKind::input;
        NativeDspNode key;
        key.id = 2;
        key.kind = NativeDspNodeKind::input;
        key.inputBus = 1;
        NativeDspNode effect;
        effect.id = 3;
        effect.kind = NativeDspNodeKind::parametricEq;
        effect.parameters = settingsFor(-12.0f, true, detector, -10.0f);
        NativeDspNode out;
        out.id = 4;
        out.kind = NativeDspNodeKind::output;
        graph.nodes = { main, key, effect, out };
        graph.connections = { { 1, 3, 0, 1 }, { 2, 3, 0, effectSidechainPortId }, { 3, 4, 0, 0 } };
        graph.outputNodeId = 4;
        auto compiled = compileNativeDspGraph(graph, { 48'000.0, 256, 2 });
        if (!compiled)
            return 99.0;
        AudioBuffer input(2, 256);
        AudioBuffer side(2, 256);
        AudioBuffer output(2, 256);
        auto phase = 0.0;
        auto in = 0.0;
        auto out2 = 0.0;
        for (int block = 0; block < 60; ++block) {
            for (std::uint32_t frame = 0; frame < 256; ++frame) {
                const auto tone = std::sin(phase);
                phase += 2.0 * std::numbers::pi * 1'000.0 / 48'000.0;
                for (std::uint32_t channel = 0; channel < 2; ++channel) {
                    input.view(256).channel(channel)[frame] = static_cast<float>(0.1 * tone);
                    side.view(256).channel(channel)[frame] = static_cast<float>(keyLevel * tone);
                }
            }
            const auto extra = side.view(256);
            compiled.plan->process(input.view(256), &extra, 1, output.view(256), {});
            if (block >= 40)
                for (std::uint32_t frame = 0; frame < 256; ++frame) {
                    in += static_cast<double>(input.view(256).channel(0)[frame]) * input.view(256).channel(0)[frame];
                    out2 += static_cast<double>(output.view(256).channel(0)[frame]) * output.view(256).channel(0)[frame];
                }
        }
        return 10.0 * std::log10(std::max(out2, 1.0e-20) / std::max(in, 1.0e-20));
    };
    // A key at -2 dB: 8 dB over -10, three quarters of it, 6 dB down.
    const auto ducked = keyed(1.0f, 0.8f);
    expect(std::abs(ducked + 6.0) < 1.0, "dynamic EQ: a band keyed on the sidechain dips when the key is loud");
    expect(std::abs(keyed(1.0f, 0.0f)) < 0.2, "dynamic EQ: keyed, a silent key leaves the sound");
    expect(std::abs(keyed(0.0f, 0.8f)) < 0.2, "dynamic EQ: a band hearing itself ignores the key");
}

void knobsGlideAndCardsSwitchOff()
{
    // A knob turned at once is heard as a slope, not a jump; a card switched
    // off lets its input through, crossfaded and as late as it was; an
    // instrument switched off falls silent.
    using namespace cydaw::engine;
    const auto graphWith = [](const NativeDspNodeKind kind, const std::vector<std::pair<const char*, float>>& settings) {
        NativeDspGraph graph;
        NativeDspNode in;
        in.id = 1;
        in.kind = NativeDspNodeKind::input;
        NativeDspNode effect;
        effect.id = 2;
        effect.kind = kind;
        if (const auto* descriptor = effectForKind(kind))
            applyEffectDefaults(*descriptor, effect.parameters);
        for (const auto& [name, value] : settings)
            if (const auto* parameter = effectParameterForSuffix(name))
                setEffectValue(effect.parameters, *parameter, value);
        NativeDspNode out;
        out.id = 3;
        out.kind = NativeDspNodeKind::output;
        graph.nodes = { in, effect, out };
        graph.connections = { { 1, 2, 0, 0 }, { 2, 3, 0, 0 } };
        graph.outputNodeId = 3;
        return graph;
    };
    constexpr std::uint32_t block = 512;
    AudioBuffer input(2, block);
    AudioBuffer output(2, block);
    auto phase = 0.0;
    std::vector<float> heard;
    const auto run = [&](AudioRenderPlan& plan, const int blocks) {
        for (int index = 0; index < blocks; ++index) {
            for (std::uint32_t frame = 0; frame < block; ++frame) {
                const auto sample = static_cast<float>(0.25 * std::sin(phase));
                phase += 2.0 * std::numbers::pi * 6'000.0 / 48'000.0;
                input.view(block).channel(0)[frame] = sample;
                input.view(block).channel(1)[frame] = sample;
            }
            plan.process(input.view(block), output.view(block), {});
            for (std::uint32_t frame = 0; frame < block; ++frame)
                heard.push_back(output.view(block).channel(0)[frame]);
        }
    };
    // The loudest of each stretch of 32 samples, in dB, from `from` on.
    const auto pieces = [&heard](const std::size_t from) {
        std::vector<double> levels;
        for (auto at = from; at + 32 <= heard.size(); at += 32) {
            auto peak = 0.0f;
            for (std::size_t frame = at; frame < at + 32; ++frame)
                peak = std::max(peak, std::abs(heard[frame]));
            levels.push_back(20.0 * std::log10(std::max(1.0e-9f, peak)));
        }
        return levels;
    };

    // ---- A knob turned at once: its three gains from 0 to -24 dB.
    AudioProcessor* equaliser {};
    auto graph = graphWith(NativeDspNodeKind::threeBandEq, {});
    auto compiled = compileNativeDspGraph(graph, { 48'000.0, block, 2 }, [&equaliser](const NativeDspNode& node, AudioProcessor* processor) {
        if (node.id == 2)
            equaliser = processor;
    });
    expect(static_cast<bool>(compiled) && equaliser != nullptr, "glide: a three-band equaliser compiles");
    if (!compiled || equaliser == nullptr)
        return;
    run(*compiled.plan, 10);
    const auto before = heard.size();
    auto turned = graph.nodes[1];
    for (const auto* key : { "eq3.low", "eq3.mid", "eq3.high" })
        setEffectValue(turned.parameters, *effectParameterForSuffix(key), -24.0f);
    expect(updateEffectProcessor(*equaliser, turned), "glide: the equaliser takes its new values");
    run(*compiled.plan, 6);
    const auto levels = pieces(before);
    auto steepest = 0.0;
    for (std::size_t index = 1; index < levels.size(); ++index)
        steepest = std::max(steepest, std::abs(levels[index] - levels[index - 1]));
    expect(levels.front() > -15.0, "glide: the first samples after the change still sound as before");
    expect(steepest < 4.5, "glide: 24 dB at once is heard in steps of a few dB, 32 samples apart (" + std::to_string(steepest) + " dB)");
    const auto lowered = levels.back();
    expect(lowered < -33.0 && lowered > -42.0, "glide: it lands where the knob went (" + std::to_string(lowered) + " dB)");

    // ---- Switched off: the sound at its full level again, crossfaded;
    // switched on: back down.
    const BypassedNode off[] { { 2, 2 } };
    auto start = heard.size();
    compiled.plan->setBypasses(off);
    run(*compiled.plan, 4);
    auto through = pieces(start);
    steepest = 0.0;
    for (std::size_t index = 1; index < through.size(); ++index)
        steepest = std::max(steepest, std::abs(through[index] - through[index - 1]));
    expect(std::abs(through.back() + 12.0) < 0.5, "bypass: switched off, the input goes through (" + std::to_string(through.back()) + " dB)");
    expect(through.front() < lowered + 8.0 && steepest < 9.0, "bypass: the switch is crossfaded, not a jump");
    start = heard.size();
    compiled.plan->setBypasses({});
    run(*compiled.plan, 4);
    expect(std::abs(pieces(start).back() - lowered) < 1.0, "bypass: switched on again, it plays as before");
    // An instrument switched off: silence.
    const BypassedNode silenced[] { { 2, 0 } };
    compiled.plan->setBypasses(silenced);
    start = heard.size();
    run(*compiled.plan, 3);
    expect(pieces(start).back() < -100.0, "bypass: switched off with nothing to pass, it falls silent");
    // The chain's input itself (where an instrument's voices come in).
    const BypassedNode voice[] { { 1, 0 } };
    compiled.plan->setBypasses(voice);
    start = heard.size();
    run(*compiled.plan, 3);
    expect(pieces(start).back() < -100.0, "bypass: the chain's input switched off, nothing comes in (" + std::to_string(pieces(start).back()) + " dB)");

    // ---- A look-ahead switched off: what passes is as late as it was.
    AudioProcessor* compressor {};
    auto late = graphWith(NativeDspNodeKind::compressorPro, { { "comppro.lookahead", 10.0f }, { "comppro.threshold", 0.0f },
                                                              { "comppro.ratio", 1.0f } });
    auto withLookAhead = compileNativeDspGraph(late, { 48'000.0, block, 2 }, [&compressor](const NativeDspNode& node, AudioProcessor* processor) {
        if (node.id == 2)
            compressor = processor;
    });
    expect(static_cast<bool>(withLookAhead) && compressor != nullptr && compressor->latencySamples() == 480,
           "bypass: a compressor looking 10 ms ahead is 480 samples late");
    if (!withLookAhead || compressor == nullptr)
        return;
    heard.clear();
    phase = 0.0;
    run(*withLookAhead.plan, 8);
    const auto playing = heard;
    heard.clear();
    phase = 0.0;
    withLookAhead.plan->reset();
    run(*withLookAhead.plan, 4);
    withLookAhead.plan->setBypasses(off);
    run(*withLookAhead.plan, 4);
    auto worst = 0.0f;
    for (std::size_t frame = 6 * block; frame < heard.size(); ++frame)
        worst = std::max(worst, std::abs(heard[frame] - playing[frame]));
    expect(worst < 0.01f, "bypass: switched off, what passes keeps the look-ahead's delay (" + std::to_string(worst) + ")");
}

void renderPlanRejectsCycles()
{
    using namespace cydaw::engine;
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<PassthroughProcessor>()) == RenderPlanError::none, "add cycle node one");
    expect(builder.addNode(2, std::make_unique<PassthroughProcessor>()) == RenderPlanError::none, "add cycle node two");
    expect(builder.connect(1, 2) == RenderPlanError::none, "connect cycle forward edge");
    expect(builder.connect(2, 1) == RenderPlanError::none, "builder records cycle back edge");
    const auto compiled = builder.compile({ 48'000.0, 64, 2 }, 2);
    expect(compiled.error == RenderPlanError::cycleDetected, "render compiler rejects implicit feedback");
}

void sharedNativeGraphCompilerBuildsTheHostIndependentProgram()
{
    using namespace cydaw::engine;
    NativeDspNodeParameters gainParameters;
    gainParameters.gainDb = -6.0206f;
    NativeDspGraph graph;
    graph.nodes = {
        { 1, NativeDspNodeKind::input, {} },
        { 2, NativeDspNodeKind::gain, gainParameters },
        { 3, NativeDspNodeKind::output, {} }
    };
    graph.connections = { { 1, 2 }, { 2, 3 } };
    graph.outputNodeId = 3;
    auto observed = std::size_t {};
    AudioProcessor* liveGain {};
    auto compiled = compileNativeDspGraph(
        graph, { 48'000.0, 64, 2 },
        [&observed, &liveGain](const NativeDspNode& node,
                              AudioProcessor* processor) {
            if (processor != nullptr)
                ++observed;
            if (node.kind == NativeDspNodeKind::gain)
                liveGain = processor;
        });
    expect(static_cast<bool>(compiled) && observed == graph.nodes.size(),
           "shared native compiler creates every processor for host adapters");

    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            input.view(64).channel(channel)[frame] = 0.8f;
    compiled.plan->process(input.view(64), output.view(64), {});
    expect(std::abs(output.view(64).channel(0)[63] - 0.4f) < 0.002f,
           "shared native compiler renders the same Graph program outside CyDAW UI");

    gainParameters.gainDb = -12.0412f;
    expect(liveGain != nullptr
               && updateNativeDspProcessorParameters(
                   *liveGain,
                   { 2, NativeDspNodeKind::gain, gainParameters }),
           "shared native compiler exposes realtime-safe parameter updates");
    compiled.plan->process(input.view(64), output.view(64), {});
    expect(std::abs(output.view(64).channel(0)[63] - 0.2f) < 0.004f,
           "live Graph parameter changes do not rebuild the render topology");
}

void realtimeEngineAppliesCommandsAtBlockBoundary()
{
    using namespace cydaw::engine;
    const ProcessSpec spec { 48'000.0, 64, 2 };
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<PassthroughProcessor>(), true) == RenderPlanError::none, "add engine passthrough");
    auto compiled = builder.compile(spec, 1);
    expect(static_cast<bool>(compiled), "engine plan compiles");

    RealtimeEngine engine(spec);
    expect(engine.installPlan(std::move(compiled.plan)), "engine accepts matching plan");
    expect(engine.postCommand(AudioCommand::tempo(90.0)), "tempo command is queued");
    expect(engine.postCommand(AudioCommand::play()), "play command is queued");

    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            input.view(64).channel(channel)[frame] = 0.125f;
    engine.process(input.view(64), output.view(64));

    const auto snapshot = engine.transportSnapshot();
    expect(snapshot.state == PlaybackState::playing, "play command is applied before rendering");
    expect(snapshot.beatsPerMinute == 90.0, "tempo command reaches transport");
    expect(snapshot.samplePosition == 64, "transport advances by rendered frames");
    expect(approximatelyEqual(output.view(64).channel(0)[12], 0.125f), "installed plan renders audio");
    expect(engine.queuedCommandCountApprox() == 0, "audio thread drains pending commands");
}

void oscillatorIsDeterministicAfterReset()
{
    using namespace cydaw::engine;
    SineOscillatorProcessor oscillator(1'000.0f);
    oscillator.prepare({ 48'000.0, 16, 1 });
    AudioBuffer silentInput(1, 16);
    AudioBuffer first(1, 16);
    AudioBuffer second(1, 16);
    oscillator.process(silentInput.view(16), first.view(16), {});
    oscillator.reset();
    oscillator.process(silentInput.view(16), second.view(16), {});
    for (std::uint32_t frame = 0; frame < 16; ++frame)
        expect(approximatelyEqual(first.view(16).channel(0)[frame], second.view(16).channel(0)[frame]),
               "oscillator reset restores deterministic phase");

    oscillator.setWaveform(3.0f);
    oscillator.setLevel(0.25f);
    oscillator.reset();
    oscillator.process(silentInput.view(16), first.view(16), {});
    expect(approximatelyEqual(first.view(16).channel(0)[0], 0.25f),
           "typed oscillator waveform and level affect rendered audio");
}

void steadyStateRenderingDoesNotAllocate()
{
    using namespace cydaw::engine;
    const ProcessSpec spec { 48'000.0, 64, 2 };
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<GainProcessor>(0.5f), true) == RenderPlanError::none,
           "add allocation-test gain");
    auto compiled = builder.compile(spec, 1);
    expect(static_cast<bool>(compiled), "allocation-test plan compiles");
    RealtimeEngine engine(spec);
    expect(engine.installPlan(std::move(compiled.plan)), "allocation-test plan installs");
    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);

    // Warm the exact path once before probing global allocations.
    engine.process(input.view(64), output.view(64));
    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    for (int block = 0; block < 10'000; ++block)
        engine.process(input.view(64), output.view(64));
    allocation_probe::enabled.store(false, std::memory_order_release);

    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0,
           "steady-state audio rendering performs no operator-new allocation");
}

void nativeEffectGraphDoesNotAllocate()
{
    using namespace cydaw::engine;
    const ProcessSpec spec { 48'000.0, 128, 2 };
    AudioRenderPlanBuilder builder;
    expect(builder.addNode(1, std::make_unique<PassthroughProcessor>(), true)
               == RenderPlanError::none, "add native graph input");
    expect(builder.addNode(2, std::make_unique<DriveProcessor>(8.0f))
               == RenderPlanError::none, "add native drive");
    expect(builder.addNode(3, std::make_unique<OnePoleLowPassProcessor>(8'000.0f))
               == RenderPlanError::none, "add native filter");
    auto delay = std::make_unique<DelayProcessor>();
    auto* delayParameters = delay.get();
    expect(builder.addNode(4, std::move(delay))
               == RenderPlanError::none, "add native delay");
    auto reverb = std::make_unique<ReverbProcessor>();
    auto* reverbParameters = reverb.get();
    expect(builder.addNode(5, std::move(reverb))
               == RenderPlanError::none, "add native reverb");
    auto compressor = std::make_unique<CompressorProcessor>();
    auto* compressorParameters = compressor.get();
    expect(builder.addNode(6, std::move(compressor))
               == RenderPlanError::none, "add native compressor");
    auto tone = std::make_unique<ToneProcessor>();
    auto* toneParameters = tone.get();
    expect(builder.addNode(7, std::move(tone))
               == RenderPlanError::none, "add native tone processor");
    expect(builder.addNode(8, std::make_unique<VocalStripProcessor>())
               == RenderPlanError::none, "add native vocal strip");
    expect(builder.addNode(9, std::make_unique<PassthroughProcessor>())
               == RenderPlanError::none, "add native graph output");
    for (NodeId node = 1; node < 9; ++node)
        expect(builder.connect(node, node + 1) == RenderPlanError::none,
               "connect native effect graph");
    auto compiled = builder.compile(spec, 9);
    expect(static_cast<bool>(compiled), "native effect graph compiles");

    AudioBuffer input(2, 128);
    AudioBuffer output(2, 128);
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 128; ++frame)
            input.view(128).channel(channel)[frame]
                = 0.2f * std::sin(static_cast<float>(frame) * 0.13f);
    compiled.plan->process(input.view(128), output.view(128), {});

    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    for (int block = 0; block < 2'000; ++block) {
        const auto alternate = (block & 1) != 0;
        delayParameters->setDelayMilliseconds(alternate ? 180.0f : 420.0f);
        delayParameters->setFeedback(alternate ? 0.25f : 0.58f);
        delayParameters->setMix(alternate ? 0.2f : 0.45f);
        reverbParameters->setRoomSize(alternate ? 0.52f : 0.84f);
        reverbParameters->setMix(alternate ? 0.18f : 0.38f);
        compressorParameters->setThresholdDb(alternate ? -12.0f : -24.0f);
        compressorParameters->setRatio(alternate ? 2.0f : 7.0f);
        toneParameters->setAmount(alternate ? -0.2f : 0.35f);
        compiled.plan->process(input.view(128), output.view(128), {});
    }
    allocation_probe::enabled.store(false, std::memory_order_release);
    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0,
           "native effect DAG and live parameter updates perform no operator-new allocation");
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < 128; ++frame)
            expect(std::isfinite(output.view(128).channel(channel)[frame]),
                   "native effect DAG output remains finite");
}

void fundamentalAudioPrimitivesProcessDeterministically()
{
    using namespace cydaw::engine;
    constexpr std::uint32_t frames = 128;
    const ProcessSpec spec { 48'000.0, frames, 2 };
    AudioBuffer input(2, frames);
    AudioBuffer temporary(2, frames);
    AudioBuffer output(2, frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        input.view(frames).channel(0)[frame] = 0.75f + 0.4f * std::sin(frame * 0.17f);
        input.view(frames).channel(1)[frame] = 0.75f - 0.4f * std::sin(frame * 0.17f);
    }

    OnePoleHighPassProcessor highpass(80.0f);
    highpass.prepare(spec);
    highpass.process(input.view(frames), temporary.view(frames), {});
    expect(std::abs(temporary.view(frames).channel(0)[frames - 1]) < 0.9f,
           "high-pass primitive rejects steady DC content");

    HardClipProcessor clipper(0.25f);
    clipper.prepare(spec);
    clipper.process(input.view(frames), output.view(frames), {});
    auto peak = 0.0f;
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            peak = std::max(peak, std::abs(output.view(frames).channel(channel)[frame]));
    expect(peak <= 0.25001f, "hard-clip primitive enforces its live threshold");

    StereoWidthProcessor width(0.0f);
    width.prepare(spec);
    width.process(input.view(frames), output.view(frames), {});
    expect(approximatelyEqual(output.view(frames).channel(0)[42],
                              output.view(frames).channel(1)[42]),
           "zero stereo width folds side information to mono");

    PanProcessor pan(1.0f);
    pan.prepare(spec);
    pan.process(input.view(frames), output.view(frames), {});
    expect(approximatelyEqual(output.view(frames).channel(0)[42], 0.0f)
               && approximatelyEqual(output.view(frames).channel(1)[42],
                                     input.view(frames).channel(1)[42]),
           "native pan processor applies a live stereo balance");

    OnePoleLowPassProcessor dampedFilter(1'000.0f, 0.5f);
    OnePoleLowPassProcessor resonantFilter(1'000.0f, 8.0f);
    AudioBuffer dampedOutput(2, frames);
    AudioBuffer resonantOutput(2, frames);
    dampedFilter.prepare(spec);
    resonantFilter.prepare(spec);
    double phase {};
    for (int block = 0; block < 40; ++block) {
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto sample = 0.05f * std::sin(static_cast<float>(phase));
            phase += 2.0 * std::acos(-1.0) * 1'000.0 / spec.sampleRate;
            for (std::uint32_t channel = 0; channel < 2; ++channel)
                input.view(frames).channel(channel)[frame] = sample;
        }
        dampedFilter.process(input.view(frames), dampedOutput.view(frames), {});
        resonantFilter.process(input.view(frames), resonantOutput.view(frames), {});
    }
    auto dampedMagnitude = 0.0f;
    auto resonantMagnitude = 0.0f;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        dampedMagnitude = std::max(
            dampedMagnitude,
            std::abs(dampedOutput.view(frames).channel(0)[frame]));
        resonantMagnitude = std::max(
            resonantMagnitude,
            std::abs(resonantOutput.view(frames).channel(0)[frame]));
    }
    expect(resonantMagnitude > dampedMagnitude * 2.0f
               && resonantFilter.resonance() == 8.0f,
           "low-pass resonance is audible and remains a live atomic parameter");

    DcBlockerProcessor blocker;
    blocker.prepare(spec);
    for (std::uint32_t channel = 0; channel < 2; ++channel)
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            input.view(frames).channel(channel)[frame] = 0.75f;
    for (int block = 0; block < 60; ++block)
        blocker.process(input.view(frames), output.view(frames), {});
    expect(std::abs(output.view(frames).channel(0)[frames - 1]) < 0.1f,
           "DC blocker removes a constant offset without allocations");
}

// A snapshot that is expensive to release. Freeing one of these on the audio
// thread is exactly the stall RtPublisher exists to prevent, so the tests below
// count every destruction and assert where it happened.
struct TrackedSnapshot {
    explicit TrackedSnapshot(const int identity)
        : id(identity), payload(256, identity)
    {
    }

    ~TrackedSnapshot()
    {
        destructionCount.fetch_add(1, std::memory_order_relaxed);
        if (audioThread.load(std::memory_order_relaxed) == std::this_thread::get_id())
            destroyedOnAudioThread.fetch_add(1, std::memory_order_relaxed);
    }

    int id {};
    std::vector<int> payload;

    static inline std::atomic<std::size_t> destructionCount {};
    static inline std::atomic<std::size_t> destroyedOnAudioThread {};
    static inline std::atomic<std::thread::id> audioThread {};
};

void realtimePublisherReaderIsLockFreeAndAllocationFree()
{
    using cydaw::realtime::RtPublisher;

    // The whole premise of the class: the reader side must be lock-free where
    // std::atomic<std::shared_ptr<T>> is not.
    expect(!std::atomic<std::shared_ptr<TrackedSnapshot>>::is_always_lock_free,
           "std::atomic<std::shared_ptr> is not guaranteed lock-free, which is why RtPublisher exists");
    expect(std::atomic<const TrackedSnapshot*>::is_always_lock_free,
           "RtPublisher reads through an always-lock-free atomic pointer");

    RtPublisher<TrackedSnapshot> publisher;
    publisher.store(std::make_shared<const TrackedSnapshot>(1));

    auto observed = 0;
    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    for (int block = 0; block < 512; ++block) {
        const auto snapshot = publisher.read();
        if (snapshot != nullptr)
            observed += snapshot->id;
    }
    allocation_probe::enabled.store(false, std::memory_order_release);

    expect(observed == 512, "the reader observes the published snapshot on every block");
    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0,
           "reading a published snapshot never allocates on the audio thread");
}

void realtimePublisherRetiresSnapshotsInsteadOfFreeingThemInTheCallback()
{
    using cydaw::realtime::RtPublisher;

    TrackedSnapshot::destructionCount.store(0, std::memory_order_relaxed);
    TrackedSnapshot::destroyedOnAudioThread.store(0, std::memory_order_relaxed);
    TrackedSnapshot::audioThread.store(std::thread::id {}, std::memory_order_relaxed);

    RtPublisher<TrackedSnapshot> publisher;
    publisher.store(std::make_shared<const TrackedSnapshot>(1));

    {
        // Simulates a callback that has already borrowed the old snapshot when a
        // control thread swaps in a replacement.
        const auto borrowed = publisher.read();
        expect(borrowed != nullptr && borrowed->id == 1, "the callback borrowed the first snapshot");
        publisher.store(std::make_shared<const TrackedSnapshot>(2));
        expect(borrowed->id == 1, "the borrowed snapshot stays valid for the rest of the block");
        expect(publisher.retiredCountApprox() == 1,
               "the replaced snapshot is retired while the callback still holds it");
        expect(TrackedSnapshot::destructionCount.load(std::memory_order_relaxed) == 0,
               "no snapshot is destroyed while a callback still references one");
    }

    // The next publication is what proves the callback has moved on.
    publisher.store(std::make_shared<const TrackedSnapshot>(3));
    expect(publisher.retiredCountApprox() <= 1,
           "quiescent snapshots are released instead of accumulating");
    expect(TrackedSnapshot::destructionCount.load(std::memory_order_relaxed) >= 1,
           "the retired snapshot is eventually destroyed once the callback has moved on");

    const auto final = publisher.read();
    expect(final != nullptr && final->id == 3, "the reader observes the newest publication");
}

void realtimePublisherSurvivesConcurrentSwapping()
{
    using cydaw::realtime::RtPublisher;

    RtPublisher<TrackedSnapshot> publisher;
    publisher.store(std::make_shared<const TrackedSnapshot>(0));

    std::atomic<bool> registered { false };
    std::atomic<bool> running { true };
    std::atomic<std::size_t> torn {};
    std::atomic<std::size_t> blocks {};

    // Stands in for the audio callback: borrow, read every byte of the payload,
    // check it is internally consistent, release.
    std::thread audio([&publisher, &registered, &running, &torn, &blocks] {
        TrackedSnapshot::audioThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
        TrackedSnapshot::destroyedOnAudioThread.store(0, std::memory_order_relaxed);
        registered.store(true, std::memory_order_release);
        while (running.load(std::memory_order_relaxed)) {
            const auto snapshot = publisher.read();
            if (snapshot == nullptr)
                continue;
            const auto identity = snapshot->id;
            for (const auto value : snapshot->payload)
                if (value != identity)
                    torn.fetch_add(1, std::memory_order_relaxed);
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Only start swapping once the reader has claimed the audio-thread identity,
    // otherwise control-thread destructions would be misattributed to it.
    while (!registered.load(std::memory_order_acquire))
        std::this_thread::yield();

    for (int revision = 1; revision <= 2'000; ++revision)
        publisher.store(std::make_shared<const TrackedSnapshot>(revision));

    running.store(false, std::memory_order_relaxed);
    audio.join();

    expect(blocks.load(std::memory_order_relaxed) > 0, "the simulated callback ran");
    expect(torn.load(std::memory_order_relaxed) == 0,
           "a borrowed snapshot is never mutated or freed underneath the callback");
    expect(TrackedSnapshot::destroyedOnAudioThread.load(std::memory_order_relaxed) == 0,
           "2000 concurrent graph swaps free nothing on the audio thread");
    expect(publisher.retiredCountApprox() < 64,
           "retired snapshots are reclaimed instead of leaking for the session");
}


// A primitive that declares a value input promises that a cable into it drives
// something. Three primitives were added declaring eleven such ports between
// them and driving none: the registry knew the ports, and the two maps that
// turn a port into a field knew nothing of the kinds. A cable into an EQ
// band's frequency did not fail quietly either — it failed the whole graph
// compile.
//
// The sweep is deliberately strict: a declared value input must name a
// parameter, and that parameter must resolve to a field or an in-bounds
// extended payload slot. Skipping the ones
// that name nothing would leave open the very hole this is here to close.
void everyDeclaredValueInputDrivesAParameterField()
{
    const auto registry = cydaw::graph::createFactoryPrimitiveRegistry();
    auto checked = 0;
    for (const auto* descriptor : registry.all()) {
        const auto kind = cydaw::engine::nativeDspKindForProcessor(
            descriptor->key);
        if (!kind.has_value())
            continue;
        for (const auto& port : descriptor->ports) {
            if (port.type != cydaw::graph::PortType::value
                || port.direction != cydaw::graph::PortDirection::input)
                continue;
            const auto parameterKey
                = cydaw::engine::nativeDspParameterKeyForPort(descriptor->key,
                                                              port.key);
            if (parameterKey.empty()) {
                expect(false, descriptor->key + " declares a value input '"
                                  + port.key + "' that names no parameter");
                continue;
            }
            ++checked;
            const auto slot = cydaw::engine::nativeDspParameterSlot(*kind, parameterKey);
            const auto validSlot = slot >= 0 && static_cast<std::size_t>(slot)
                < cydaw::engine::NativeDspNodeParameters{}.eqBands.size();
            expect(cydaw::engine::nativeDspParameterField(*kind, parameterKey)
                       != nullptr || validSlot,
                   descriptor->key + " port '" + port.key
                       + "' names a parameter with no field or valid payload slot");
        }
    }
    expect(checked >= 20, "the registry sweep actually examined its ports");
}

const std::array<std::pair<cydaw::engine::PolyOscillatorWaveform,
                           std::string_view>, 6> voiceModes {
    std::pair { cydaw::engine::PolyOscillatorWaveform::sine, "sine" },
    std::pair { cydaw::engine::PolyOscillatorWaveform::triangle, "triangle" },
    std::pair { cydaw::engine::PolyOscillatorWaveform::saw, "saw" },
    std::pair { cydaw::engine::PolyOscillatorWaveform::square, "square" },
    std::pair { cydaw::engine::PolyOscillatorWaveform::wavetable, "wavetable" },
    std::pair { cydaw::engine::PolyOscillatorWaveform::string, "string" }
};

// Two modes were appended to the waveform enum and one of the two renderers
// never grew a case for them, so its switch fell through and the note was
// silence. Every mode has to make a sound.
void everyVoiceWaveformProducesSound()
{
    const auto rate = 48000.0;
    for (const auto& mode : voiceModes) {
        cydaw::engine::PolyVoiceOscillator source;
        source.prepare(rate);
        cydaw::engine::PolySynthGraphPlan plan;
        plan.waveform = mode.first;
        source.startNote(plan, 220.0, rate);
        auto energy = 0.0;
        for (auto sample = 0; sample < 9600; ++sample) {
            const auto value = source.nextSample(plan, 220.0, rate);
            energy += value * value;
        }
        expect(energy > 1.0e-3,
               std::string("voice waveform '") + std::string(mode.second)
                   + "' renders sound rather than silence");
    }
}

// The voice-filter card draws a two-pole low-pass with the resonance as
// its Q. The track's voice ran a one-pole without resonance: the knob did
// nothing there, and a patch previewed resonant played flat on its track.
// The one filter both now run has to be the response drawn.
void voiceFilterResonatesAsDrawn()
{
    const auto rate = 48000.0;
    const auto pi = std::acos(-1.0);
    const auto gainDb = [rate, pi](const double resonance, const double hz) {
        cydaw::engine::PolyVoiceFilter filter;
        filter.reset();
        filter.setCoefficients(1000.0, resonance, rate);
        auto energy = 0.0;
        auto counted = 0;
        for (auto sample = 0; sample < 48000; ++sample) {
            const auto value = filter.process(
                std::sin(2.0 * pi * hz * static_cast<double>(sample) / rate));
            if (sample < 24000)
                continue;
            energy += value * value;
            ++counted;
        }
        return 20.0 * std::log10(std::sqrt(energy / counted) / std::sqrt(0.5));
    };
    expect(std::abs(gainDb(4.0, 1000.0) - 12.04) < 0.5,
           "at its cutoff a voice filter with a Q of four gains twelve decibels, as its card draws");
    expect(std::abs(gainDb(0.707, 250.0)) < 0.5,
           "two octaves under the cutoff the voice filter passes the note as it is");
    expect(gainDb(0.707, 4000.0) < -22.0,
           "two octaves over the cutoff the voice filter is twenty-four decibels down");
}

// The one voice both renderers play: its note is at the pitch it was
// given, its attack takes the time the envelope card says, it holds the
// sustain level, the release ends it, and its filter resonates as the
// filter card draws.
void polyVoiceShapesTheNoteAsItsCardsSay()
{
    const auto rate = 48000.0;
    cydaw::engine::PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.waveform = cydaw::engine::PolyOscillatorWaveform::sine;
    plan.oscillatorLevel = 0.5f;
    plan.attackSeconds = 0.1f;
    plan.decaySeconds = 0.05f;
    plan.sustainLevel = 0.5f;
    plan.releaseSeconds = 0.1f;
    plan.filterCutoffHz = 20000.0f;
    plan.filterResonance = 0.707f;
    plan.velocityScale = 1.0f;

    std::vector<float> mono(480, 0.0f);
    float* channels[1] = { mono.data() };
    const auto renderBlock = [&](cydaw::engine::PolyVoice& voice) {
        std::fill(mono.begin(), mono.end(), 0.0f);
        const auto alive = voice.render(plan, channels, 1, 0, 480, rate);
        auto peak = 0.0f;
        auto crossings = 0;
        for (std::size_t index = 0; index < mono.size(); ++index) {
            peak = std::max(peak, std::abs(mono[index]));
            if (index > 0 && (mono[index - 1] < 0.0f) != (mono[index] < 0.0f))
                ++crossings;
        }
        return std::tuple { alive, peak, crossings };
    };

    cydaw::engine::PolyVoice voice;
    voice.prepare(rate);
    expect(voice.startNote(plan, 69, 1.0f, rate), "a voice takes the note it is given");
    const auto [firstAlive, firstPeak, firstCrossings] = renderBlock(voice);
    // Ten milliseconds into a hundred-millisecond attack: a tenth of the way.
    expect(firstAlive && firstPeak > 0.02f && firstPeak < 0.12f,
           "ten milliseconds into the attack the note is a tenth of the way up");
    auto lastAttackPeak = 0.0f;
    for (auto block = 1; block < 10; ++block)
        lastAttackPeak = std::get<1>(renderBlock(voice));
    expect(lastAttackPeak > 0.40f,
           "at the end of the attack the note has reached its full level");
    auto sustainPeak = 0.0f;
    auto sustainCrossings = 0;
    for (auto block = 0; block < 30; ++block) {
        const auto [alive, peak, crossings] = renderBlock(voice);
        sustainPeak = peak;
        sustainCrossings += crossings;
    }
    // Half the level at sustain, and 440 Hz crosses zero 880 times a second:
    // 264 in three hundred milliseconds.
    expect(std::abs(sustainPeak - 0.25f) < 0.04f,
           "held, the note sits at the sustain level its card says");
    expect(std::abs(sustainCrossings - 264) <= 4,
           "the note is at the pitch it was given");
    voice.stopNote(true);
    auto releaseAlive = true;
    auto releaseBlocks = 0;
    while (releaseAlive && releaseBlocks < 200) {
        releaseAlive = std::get<0>(renderBlock(voice));
        ++releaseBlocks;
    }
    // A tenth of a second to fall to a ten-thousandth: ten blocks, give or
    // take one.
    expect(!releaseAlive && releaseBlocks >= 9 && releaseBlocks <= 12,
           "the release ends the note in the time its card says");
    expect(!voice.isActive(), "a released voice is free again");

    // The filter: the same note with the cutoff on it and a Q of four is
    // about four times as loud as with no resonance.
    const auto sustainLevelWith = [&](const float resonance) {
        plan.filterCutoffHz = 440.0f;
        plan.filterResonance = resonance;
        cydaw::engine::PolyVoice filtered;
        filtered.prepare(rate);
        (void) filtered.startNote(plan, 69, 1.0f, rate);
        auto peak = 0.0f;
        for (auto block = 0; block < 40; ++block)
            peak = std::get<1>(renderBlock(filtered));
        return peak;
    };
    const auto flat = sustainLevelWith(0.707f);
    const auto resonant = sustainLevelWith(4.0f);
    expect(flat > 0.05f && resonant > flat * 4.0f,
           "with the cutoff on the note, a Q of four makes it four times as loud, as the filter card draws");
}

// The three unison voices used to start a third of a cycle apart, which is
// exactly the spacing at which three copies of one wave cancel. With the
// detune at zero they never drifted out of it and the note nearly vanished.
void wavetableUnisonKeepsItsLevelWithoutDetune()
{
    const auto rate = 48000.0;
    const auto renderRms = [rate](const float detuneCents) {
        cydaw::engine::PolyVoiceOscillator source;
        source.prepare(rate);
        cydaw::engine::PolySynthGraphPlan plan;
        plan.waveform = cydaw::engine::PolyOscillatorWaveform::wavetable;
        plan.wavetableDetuneCents = detuneCents;
        plan.wavetablePosition = 0.0f;
        source.startNote(plan, 220.0, rate);
        auto energy = 0.0;
        for (auto sample = 0; sample < 4800; ++sample) {
            const auto value = source.nextSample(plan, 220.0, rate);
            energy += value * value;
        }
        return std::sqrt(energy / 4800.0);
    };
    const auto spread = renderRms(8.0f);
    const auto coherent = renderRms(0.0f);
    expect(spread > 0.2, "a detuned wavetable voice has a usable level");
    expect(coherent > spread * 0.7,
           "a wavetable voice with no detune does not cancel itself");
}

// The loop filter's own delay is part of what tunes a string, and the period
// is not a whole number of samples. Ignoring either flattened the note, by as
// much as three semitones once the damping knob was turned up.
void stringVoiceRingsAtTheNoteItWasGiven()
{
    const auto rate = 48000.0;
    const auto measure = [rate](const float damping) {
        cydaw::engine::PolyVoiceOscillator source;
        source.prepare(rate);
        cydaw::engine::PolySynthGraphPlan plan;
        plan.waveform = cydaw::engine::PolyOscillatorWaveform::string;
        plan.stringDamping = damping;
        plan.stringDecaySeconds = 8.0f;
        source.startNote(plan, 220.0, rate);
        std::vector<double> ring;
        ring.reserve(12000);
        for (auto sample = 0; sample < 16800; ++sample) {
            const auto value = source.nextSample(plan, 220.0, rate);
            if (sample >= 4800)
                ring.push_back(value);
        }
        auto bestLag = 0;
        auto bestScore = -1.0e30;
        for (auto lag = 120; lag <= 320; ++lag) {
            auto score = 0.0;
            const auto step = static_cast<std::size_t>(lag);
            for (std::size_t index = 0; index + step < ring.size(); ++index)
                score += ring[index] * ring[index + step];
            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
            }
        }
        return rate / static_cast<double>(bestLag);
    };
    const std::array<float, 3> dampings { 0.1f, 0.5f, 0.9f };
    for (const auto damping : dampings) {
        const auto heard = measure(damping);
        expect(std::abs(heard - 220.0) < 220.0 * 0.04,
               "a string voice rings within four percent of its note");
    }
}

// A recorded note made playable: its partials at the note asked for and at
// their level, held by its loop while the key is down, gone after its
// release; between two recorded pitches, both zones blended.
cydaw::engine::SoundModelZone twoPartialZone(const int rootNote, const float f0, const float first, const float second)
{
    cydaw::engine::SoundModelZone zone;
    zone.rootNote = rootNote;
    zone.f0 = f0;
    zone.velocity = 0.8f;
    zone.frameSeconds = 0.01f;
    zone.frames = 100;
    zone.harmonicCount = 2;
    zone.harmonics.assign(200, 0.0f);
    for (std::size_t frame = 0; frame < 100; ++frame) {
        zone.harmonics[frame * 2] = first;
        zone.harmonics[frame * 2 + 1] = second;
    }
    zone.pitchCents.assign(100, 0.0f);
    zone.loopStart = 20;
    zone.loopEnd = 60;
    zone.release = 80;
    return zone;
}

void soundModelVoicePlaysItsPartials()
{
    const auto rate = 48000.0;
    cydaw::engine::SoundModel model;
    model.zones.push_back(twoPartialZone(57, 220.0f, 0.5f, 0.25f));
    const auto values = cydaw::engine::defaultSoundModelValues();
    cydaw::engine::SoundModelVoice voice;
    voice.prepare(rate);
    // A4: the zone was recorded at A3, an octave down.
    voice.start(model, values, 69.0, 0.8, rate);
    std::vector<double> held;
    held.reserve(96'000);
    for (auto sample = 0; sample < 96'000; ++sample)
        held.push_back(voice.nextSample(&model, values, rate));
    expect(voice.active(), "a sound model holds its loop while the key is down");
    auto energy = 0.0;
    for (std::size_t index = 24'000; index < 72'000; ++index)
        energy += held[index] * held[index];
    const auto rms = std::sqrt(energy / 48'000.0);
    const auto expected = std::sqrt(0.5 * 0.5 / 2.0 + 0.25 * 0.25 / 2.0);
    expect(std::abs(rms - expected) < expected * 0.02, "a sound model plays its partials at their level");
    auto bestLag = 0;
    auto bestScore = -1.0e30;
    for (auto lag = 90; lag <= 130; ++lag) {
        auto score = 0.0;
        for (std::size_t index = 24'000; index < 48'000; ++index)
            score += held[index] * held[index + static_cast<std::size_t>(lag)];
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    expect(std::abs(bestLag - 109) <= 1, "a sound model plays at the note asked for, not the one recorded");
    voice.release();
    auto after = 0;
    while (voice.active() && after < 48'000) {
        (void) voice.nextSample(&model, values, rate);
        ++after;
    }
    expect(!voice.active() && after < 14'400, "a sound model ends after its recorded release");

    // A second zone an octave up, twice as loud: halfway, the level between.
    model.zones.push_back(twoPartialZone(69, 440.0f, 1.0f, 0.5f));
    const auto choice = cydaw::engine::chooseSoundModelZones(model, 63.0, 0.8);
    expect(choice.count == 2 && std::abs(choice.weights[0] - 0.5) < 1.0e-6,
           "a note between two recorded pitches blends both zones");
    cydaw::engine::SoundModelVoice between;
    between.prepare(rate);
    between.start(model, values, 63.0, 0.8, rate);
    energy = 0.0;
    for (auto sample = 0; sample < 48'000; ++sample) {
        const auto value = between.nextSample(&model, values, rate);
        if (sample >= 24'000)
            energy += value * value;
    }
    const auto blended = std::sqrt(energy / 24'000.0);
    expect(std::abs(blended - 1.5 * expected) < expected * 0.03, "two zones blend their partials by weight");
}

// The two channels of a model, its recorded head and its velocity layers.
// Prisme: in tune, its unison spread in stereo, its filter heard, its note
// ended by its envelope, its glide, its modulation, its tables clean.
void prismSynthPlays()
{
    namespace prism = cydaw::engine::prism;
    constexpr double rate = 48'000.0;
    const auto render = [](prism::PrismValues values, const int note, const int frames, std::vector<float>& left,
                           std::vector<float>& right, const double glideFrom = -1.0, const int releaseAt = -1) {
        prism::Voice voice;
        voice.prepare(rate);
        const auto tables = prism::resolveTables(values);
        voice.start(values, note, 1.0f, glideFrom, rate);
        left.assign(static_cast<std::size_t>(frames), 0.0f);
        right.assign(static_cast<std::size_t>(frames), 0.0f);
        prism::Context context;
        auto done = 0;
        auto alive = true;
        while (done < frames && alive) {
            const auto count = std::min(256, frames - done);
            if (releaseAt >= 0 && done >= releaseAt && !voice.releasing())
                voice.release();
            alive = voice.render(values, *tables, context, left.data() + done, right.data() + done, count, rate);
            context.clockSeconds += count / rate;
            done += count;
        }
        return voice.active();
    };
    const auto rms = [](const std::vector<float>& samples, const std::size_t from, const std::size_t to) {
        auto sum = 0.0;
        for (auto i = from; i < to && i < samples.size(); ++i)
            sum += static_cast<double>(samples[i]) * samples[i];
        return std::sqrt(sum / std::max<std::size_t>(1, std::min(to, samples.size()) - from));
    };
    const auto crossings = [](const std::vector<float>& samples, const std::size_t from, const std::size_t to) {
        auto count = 0;
        for (auto i = from + 1; i < to; ++i)
            if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
                ++count;
        return count;
    };
    const auto at = [](const std::string& key) { return static_cast<std::size_t>(prism::parameterIndex(key)); };

    expect(prism::parameterCount == static_cast<int>(prism::defaultValues().size()), "prism: one value per parameter");
    expect(prism::parameterIndex("a.position") == prism::oscBase(0) + prism::oscPosition, "prism: keys find their parameters");
    expect(prism::parameterIndex("m16.amount") == prism::drawBase(0) - 1, "prism: the last slot comes just before the drawn LFOs' points");
    for (int index = 0; index < prism::parameterCount; ++index) {
        const auto& parameter = prism::parameter(index);
        const auto back = prism::denormalise(index, prism::normalise(index, parameter.defaultValue));
        expect(std::abs(back - parameter.defaultValue) < 1.0e-3f * std::max(1.0f, std::abs(parameter.maximum)),
               "prism: a default survives normalising (" + parameter.key + ")");
    }

    // A sine (Basic Shapes, position 0) at A4: 440 crossings a second.
    auto values = prism::defaultValues();
    values[at("a.random")] = 0.0f;
    std::vector<float> left;
    std::vector<float> right;
    (void) render(values, 69, 48'000, left, right);
    const auto cycles = crossings(left, 4'800, 48'000);
    expect(std::abs(cycles - 396) <= 2, "prism: A4 plays at 440 Hz (" + std::to_string(cycles) + " in 0.9 s)");
    expect(rms(left, 4'800, 48'000) > 0.05, "prism: it is heard");
    expect(rms(left, 4'800, 48'000) - rms(right, 4'800, 48'000) < 1.0e-6, "prism: one voice sits in the middle");

    // Seven voices of unison, spread: left and right differ.
    values[at("a.unison")] = 7.0f;
    values[at("a.detune")] = 0.4f;
    values[at("a.width")] = 1.0f;
    values[at("a.table")] = 0.0f;
    values[at("a.position")] = 0.67f; // the saw
    (void) render(values, 57, 24'000, left, right);
    auto difference = 0.0;
    for (std::size_t i = 2'400; i < left.size(); ++i)
        difference += std::abs(static_cast<double>(left[i]) - right[i]);
    expect(difference / static_cast<double>(left.size()) > 0.01, "prism: unison spreads in stereo");

    // The filter: a low pass at 300 Hz takes most of a saw's level away.
    values[at("a.unison")] = 1.0f;
    (void) render(values, 57, 24'000, left, right);
    const auto open = rms(left, 2'400, 24'000);
    values[at("filter.on")] = 1.0f;
    values[at("filter.type")] = 1.0f;
    values[at("filter.cutoff")] = 100.0f;
    values[at("filter.resonance")] = 0.0f;
    (void) render(values, 57, 24'000, left, right);
    const auto closed = rms(left, 2'400, 24'000);
    expect(closed < open * 0.7 && closed > 0.0, "prism: the low pass below a saw takes its level");
    // Every filter type stays finite and makes sound.
    for (int type = 0; type < static_cast<int>(prism::FilterType::count); ++type) {
        values[at("filter.type")] = static_cast<float>(type);
        values[at("filter.resonance")] = 0.9f;
        values[at("filter.drive")] = 0.5f;
        values[at("filter.cutoff")] = 1'000.0f;
        (void) render(values, 45, 9'600, left, right);
        auto finite = true;
        for (const auto sample : left)
            finite = finite && std::isfinite(sample) && std::abs(sample) < 8.0f;
        expect(finite && rms(left, 0, left.size()) > 1.0e-4, "prism: filter type " + std::to_string(type) + " plays, finite");
    }
    values[at("filter.on")] = 0.0f;

    // The release ends the voice.
    values[at("env1.release")] = 0.05f;
    const auto stillOn = render(values, 60, 24'000, left, right, -1.0, 4'800);
    expect(!stillOn, "prism: the release ends the note");
    expect(rms(left, 12'000, 24'000) < 1.0e-6, "prism: silent after its release");

    // A glide from an octave down: the pitch climbs.
    values[at("voice.glide")] = 0.2f;
    {
        prism::Voice voice;
        voice.prepare(rate);
        const auto tables = prism::resolveTables(values);
        voice.start(values, 72, 1.0f, 60.0, rate);
        std::vector<float> l(4'800);
        std::vector<float> r(4'800);
        prism::Context context;
        (void) voice.render(values, *tables, context, l.data(), r.data(), 4'800, rate);
        expect(voice.currentPitch() > 64.0 && voice.currentPitch() < 72.0, "prism: half way up its glide");
    }
    values[at("voice.glide")] = 0.0f;

    // An LFO on the position: the modulated position moves away from the knob.
    values[at("m1.source")] = static_cast<float>(prism::Source::lfo1);
    auto destination = 0;
    for (std::size_t i = 1; i < prism::destinations().size(); ++i)
        if (prism::destinations()[i] == prism::parameterIndex("a.position"))
            destination = static_cast<int>(i);
    values[at("m1.destination")] = static_cast<float>(destination);
    values[at("m1.amount")] = 0.5f;
    values[at("lfo1.sync")] = 0.0f;
    values[at("lfo1.rate")] = 5.0f;
    {
        prism::Voice voice;
        voice.prepare(rate);
        const auto tables = prism::resolveTables(values);
        voice.start(values, 60, 1.0f, -1.0, rate);
        std::vector<float> l(9'600);
        std::vector<float> r(9'600);
        prism::Context context;
        (void) voice.render(values, *tables, context, l.data(), r.data(), 2'400, rate);
        const auto moved = voice.modulated(prism::parameterIndex("a.position"));
        expect(destination > 0 && moved > 0.67f + 0.05f, "prism: an LFO moves the position (" + std::to_string(moved) + ")");
    }
    // A drawn LFO: drawn high all through, it pushes the position up; drawn
    // low, down; drawn as a ramp, it reads between its points.
    {
        const auto drawnAt = [&](const float level) {
            auto drawn = values;
            drawn[at("lfo1.shape")] = static_cast<float>(prism::LfoShape::drawn);
            drawn[at("lfo1.unipolar")] = 0.0f;
            for (int point = 0; point < prism::drawPoints; ++point)
                drawn[static_cast<std::size_t>(prism::drawBase(0) + point)] = level;
            prism::Voice voice;
            voice.prepare(rate);
            const auto tables = prism::resolveTables(drawn);
            voice.start(drawn, 60, 1.0f, -1.0, rate);
            std::vector<float> l(2'400);
            std::vector<float> r(2'400);
            prism::Context context;
            (void) voice.render(drawn, *tables, context, l.data(), r.data(), 2'400, rate);
            return voice.modulated(prism::parameterIndex("a.position"));
        };
        const auto up = drawnAt(1.0f);
        const auto down = drawnAt(-1.0f);
        expect(up > 0.67f + 0.2f && down < 0.67f - 0.2f,
               "prism: a drawn LFO moves the position as drawn (" + std::to_string(up) + ", " + std::to_string(down) + ")");
        std::array<float, prism::drawPoints> ramp {};
        for (int point = 0; point < prism::drawPoints; ++point)
            ramp[static_cast<std::size_t>(point)] = static_cast<float>(point) / prism::drawPoints;
        expect(std::abs(prism::drawnLfo(ramp.data(), 0.5 + 0.5 / prism::drawPoints) - (16.5 / prism::drawPoints)) < 1.0e-6,
               "prism: a drawn LFO reads between its points");
        expect(std::abs(prism::drawnLfo(ramp.data(), 1.0 - 0.25 / prism::drawPoints) - 0.25 * ramp.back()) < 1.0e-5,
               "prism: a drawn LFO goes from its last point back to its first");
        expect(prism::parameterIndex("lfo4.draw32") == prism::oscCBase - 1,
               "prism: the drawn points come after the rest, before what came later, their keys kept");
    }

    // Every factory table: frames, finite, peak about 1, band limits quieter at the top.
    for (int table = 0; table < prism::factoryTableCount(); ++table) {
        const auto built = prism::factoryTable(table);
        auto finite = built != nullptr && built->frameCount > 0;
        auto peak = 0.0f;
        for (int frame = 0; finite && frame < built->frameCount; ++frame)
            for (int i = 0; i < prism::Wavetable::frameLength; ++i) {
                const auto sample = built->frame(frame, 0)[i];
                finite = finite && std::isfinite(sample);
                peak = std::max(peak, std::abs(sample));
            }
        expect(finite && peak > 0.9f && peak < 1.1f, "prism: table " + prism::factoryTableName(table) + " is clean");
    }
    // A high note reads a narrow band: no alias above.
    values = prism::defaultValues();
    values[at("a.position")] = 0.67f;
    (void) render(values, 120, 9'600, left, right);
    auto finite = true;
    for (const auto sample : left)
        finite = finite && std::isfinite(sample);
    expect(finite, "prism: a high saw stays finite");
}

void soundModelPlaysStereoHeadAndLayers()
{
    const auto rate = 48'000.0;
    const auto values = cydaw::engine::defaultSoundModelValues();
    const auto twoPi = 2.0 * std::numbers::pi;

    // One partial at 440 Hz, its RMS 0.5; the right 6 dB under the left and a
    // quarter turn later.
    auto zone = twoPartialZone(69, 440.0f, 0.5f, 0.0f);
    zone.stereoFrames = { 0, 99 };
    const auto r = 0.5;
    const auto left = std::sqrt(2.0 / (1.0 + r * r));
    zone.partialLeft = { static_cast<float>(left), 0.0f, static_cast<float>(left), 0.0f };
    zone.partialRight = { static_cast<float>(left * r), 0.0f, static_cast<float>(left * r), 0.0f };
    zone.phaseCos = { 0.0f, 1.0f };
    zone.phaseSin = { 1.0f, 0.0f };
    cydaw::engine::SoundModel stereo;
    stereo.zones.push_back(zone);
    cydaw::engine::SoundModelVoice voice;
    voice.prepare(rate);
    voice.start(stereo, values, 69.0, 0.8, rate);
    std::vector<double> lefts;
    std::vector<double> rights;
    for (auto sample = 0; sample < 48'000; ++sample) {
        auto l = 0.0;
        auto rr = 0.0;
        voice.nextFrame(&stereo, values, rate, l, rr);
        lefts.push_back(l);
        rights.push_back(rr);
    }
    auto ll = 0.0;
    auto rrSum = 0.0;
    auto lr = 0.0;
    for (std::size_t index = 12'000; index < 36'000; ++index) {
        ll += lefts[index] * lefts[index];
        rrSum += rights[index] * rights[index];
        lr += lefts[index] * rights[index];
    }
    expect(std::abs(std::sqrt(rrSum / ll) - 0.5) < 0.01 && std::abs(std::sqrt(ll / 24'000.0) - left * 0.5 / std::sqrt(2.0)) < 0.01
               && std::abs(lr / std::sqrt(ll * rrSum)) < 0.05,
           "a stereo model plays each partial's share and phase in each channel");

    // A head: the recording's first 100 ms, a sine the partial continues; the
    // model takes it over in phase, so the crossfade leaves the wave whole.
    auto headed = twoPartialZone(69, 440.0f, 0.5f, 0.0f);
    const auto start = 0.7;
    headed.headLeft.resize(4'800);
    for (std::size_t index = 0; index < headed.headLeft.size(); ++index)
        headed.headLeft[index] = static_cast<float>(0.5 * std::sin(twoPi * 440.0 * static_cast<double>(index) / rate + start));
    headed.headFade = 960;
    headed.headPhase = { static_cast<float>(std::fmod(twoPi * 440.0 * 3'840.0 / rate + start, twoPi)), 0.0f };
    cydaw::engine::SoundModel withHead;
    withHead.zones.push_back(headed);
    cydaw::engine::SoundModelVoice headVoice;
    headVoice.prepare(rate);
    headVoice.start(withHead, values, 69.0, 0.8, rate);
    auto worst = 0.0;
    for (auto sample = 0; sample < 9'600; ++sample) {
        auto l = 0.0;
        auto rr = 0.0;
        headVoice.nextFrame(&withHead, values, rate, l, rr);
        const auto expected = 0.5 * std::sin(twoPi * 440.0 * sample / rate + start);
        if (sample > 64)
            worst = std::max(worst, std::abs(l - expected));
    }
    expect(worst < 0.02, "a model's head gives way to its partials in phase, the wave unbroken (worst "
                             + std::to_string(worst) + ")");

    // Velocity layers either side of the velocity, pitches either side of the note.
    cydaw::engine::SoundModel layered;
    for (const auto root : { 60, 72 })
        for (const auto velocity : { 0.3f, 0.9f }) {
            auto layer = twoPartialZone(root, static_cast<float>(440.0 * std::pow(2.0, (root - 69) / 12.0)), 0.5f, 0.25f);
            layer.velocity = velocity;
            layered.zones.push_back(layer);
        }
    const auto choice = cydaw::engine::chooseSoundModelZones(layered, 63.0, 0.6);
    auto sum = 0.0;
    for (auto index = 0; index < choice.count; ++index)
        sum += choice.weights[static_cast<std::size_t>(index)];
    expect(choice.count == 4 && std::abs(sum - 1.0) < 1.0e-6 && std::abs(choice.weights[0] - 0.375) < 1.0e-6,
           "a note between pitches and velocities blends the four zones around it");
    const auto top = cydaw::engine::chooseSoundModelZones(layered, 60.0, 1.0);
    expect(top.count == 1 && layered.zones[static_cast<std::size_t>(top.zones[0])].velocity == 0.9f,
           "a velocity above the loudest layer plays that layer alone");
}

// A stereo sample of a sine: the left at `level`, the right at `level` x
// `right`, decoded as the store leaves it.
cydaw::engine::SampleDataRef sineSample(const double frequency, const double seconds, const double level,
                                        const double right, const double rate = 48'000.0)
{
    auto data = std::make_shared<cydaw::engine::SampleData>();
    data->channels = 2;
    data->sampleRate = rate;
    data->frames = static_cast<std::int64_t>(seconds * rate);
    data->scale = static_cast<float>(level / 32'767.0);
    data->pcm.resize(static_cast<std::size_t>(data->frames) * 2);
    for (std::int64_t frame = 0; frame < data->frames; ++frame) {
        const auto value = std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / rate);
        data->pcm[static_cast<std::size_t>(frame) * 2] = static_cast<std::int16_t>(std::lround(32'767.0 * value));
        data->pcm[static_cast<std::size_t>(frame) * 2 + 1]
            = static_cast<std::int16_t>(std::lround(32'767.0 * right * value));
    }
    data->ready.store(true);
    return data;
}

struct StereoTake {
    std::vector<double> left;
    std::vector<double> right;
    [[nodiscard]] double rms(const std::vector<double>& side, const std::size_t from, const std::size_t to) const
    {
        auto energy = 0.0;
        for (auto index = from; index < to && index < side.size(); ++index)
            energy += side[index] * side[index];
        return std::sqrt(energy / static_cast<double>(std::max<std::size_t>(1, to - from)));
    }
};

StereoTake playSampler(const cydaw::engine::SamplerMap& map, const cydaw::engine::SamplerValues& values,
                       const double note, const double velocity, const int samples, int* playedFor = nullptr)
{
    cydaw::engine::SamplerVoice voice;
    voice.start(map, values, note, velocity, 48'000.0);
    StereoTake take;
    for (auto sample = 0; sample < samples; ++sample) {
        auto left = 0.0;
        auto right = 0.0;
        voice.next(&map, values, left, right);
        take.left.push_back(left);
        take.right.push_back(right);
        if (playedFor != nullptr && voice.active())
            *playedFor = sample + 1;
    }
    return take;
}

int strongestLag(const std::vector<double>& signal, const int from, const int to)
{
    auto bestLag = from;
    auto bestScore = -1.0e30;
    for (auto lag = from; lag <= to; ++lag) {
        auto score = 0.0;
        for (std::size_t index = 4'800; index < 14'400; ++index)
            score += signal[index] * signal[index + static_cast<std::size_t>(lag)];
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    return bestLag;
}

// Track formats: each channel's speaker, and the conversions between two
// formats (kept speakers, a centre and surrounds folded at -3 dB, mono
// spread at full level, anything to mono as half of each side).
void channelLayoutsConvert()
{
    using namespace cydaw::engine;
    expect(layoutKey(1) == "mono" && layoutKey(2) == "stereo" && layoutKey(4) == "quad" && layoutKey(6) == "5.1"
               && layoutKey(8) == "7.1",
           "every channel count names its layout");
    expect(layoutChannels("5.1") == 6 && layoutChannels("quad") == 4 && layoutChannels("3") == 3
               && layoutChannels("nope") == 0,
           "a layout's key gives its channel count back");
    expect(speakerRole(6, 3) == SpeakerRole::lfe && speakerRole(8, 6) == SpeakerRole::surroundLeft
               && speakerRole(4, 2) == SpeakerRole::surroundLeft,
           "channels follow the WAV order");

    expect(channelConversion(4, 4).identity() && channelConversion(1, 1).identity(),
           "a format into itself is left alone");
    const auto monoToStereo = channelConversion(1, 2);
    expect(monoToStereo.gain(0, 0) == 1.0f && monoToStereo.gain(1, 0) == 1.0f,
           "mono spreads to left and right at full level");
    const auto stereoToMono = channelConversion(2, 1);
    expect(approximatelyEqual(stereoToMono.gain(0, 0), 0.5f) && approximatelyEqual(stereoToMono.gain(0, 1), 0.5f),
           "stereo folds to mono as half of each side");
    const auto fiveOneToStereo = channelConversion(6, 2);
    expect(fiveOneToStereo.gain(0, 0) == 1.0f && approximatelyEqual(fiveOneToStereo.gain(0, 2), 0.70710678f)
               && approximatelyEqual(fiveOneToStereo.gain(1, 2), 0.70710678f) && fiveOneToStereo.gain(0, 3) == 0.0f
               && fiveOneToStereo.gain(1, 3) == 0.0f && approximatelyEqual(fiveOneToStereo.gain(0, 4), 0.70710678f)
               && fiveOneToStereo.gain(1, 4) == 0.0f && approximatelyEqual(fiveOneToStereo.gain(1, 5), 0.70710678f),
           "5.1 folds to stereo: centre and surrounds at -3 dB, no LFE");
    const auto stereoToQuad = channelConversion(2, 4);
    expect(stereoToQuad.gain(0, 0) == 1.0f && stereoToQuad.gain(1, 1) == 1.0f && stereoToQuad.gain(2, 0) == 0.0f
               && stereoToQuad.gain(3, 1) == 0.0f,
           "stereo into quad keeps to the front");
    const auto sevenToFive = channelConversion(8, 6);
    expect(sevenToFive.gain(3, 3) == 1.0f && sevenToFive.gain(4, 4) == 1.0f && sevenToFive.gain(4, 6) == 1.0f
               && sevenToFive.gain(5, 7) == 1.0f,
           "7.1 into 5.1: back and side surrounds meet in the surrounds");

    // A quad block folded to stereo, panned hard left: the right side is gone.
    std::array<float, 4> l { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 4> r { 0.5f, 0.5f, 0.5f, 0.5f };
    std::array<float, 4> ls { 0.25f, 0.25f, 0.25f, 0.25f };
    std::array<float, 4> rs { 0.25f, 0.25f, 0.25f, 0.25f };
    std::array<float, 4> left {};
    std::array<float, 4> right {};
    const std::array<const float*, 4> in { l.data(), r.data(), ls.data(), rs.data() };
    const std::array<float*, 2> out { left.data(), right.data() };
    convertChannels(channelConversion(4, 2), in.data(), out.data(), 4, 2.0f, false, -1.0f);
    expect(approximatelyEqual(left[3], 2.0f * (1.0f + 0.25f * 0.70710678f)) && right[0] == 0.0f,
           "a conversion scales, pans and folds in one pass");
}

// The sampler: the zones a note falls in, played at the note's pitch, in
// stereo, layered by velocity, looped, in turn.
void samplerPlaysItsZones()
{
    using cydaw::engine::SamplerSetting;
    auto values = cydaw::engine::defaultSamplerValues();
    values[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 0.0f;
    cydaw::engine::SamplerMap map;
    // A3 recorded twice: soft (0.25) under velocity 64, loud (0.5) above;
    // the right channel at half the left.
    cydaw::engine::SamplerZone soft;
    soft.sample = sineSample(220.0, 2.0, 0.25, 0.5);
    soft.rootNote = 57;
    soft.highVelocity = 63;
    auto loud = soft;
    loud.sample = sineSample(220.0, 2.0, 0.5, 0.5);
    loud.lowVelocity = 64;
    loud.highVelocity = 127;
    map.zones = { soft, loud };

    // A4, softly: the soft zone an octave up (a period of 109 samples).
    const auto quiet = playSampler(map, values, 69.0, 0.3, 24'000);
    const auto sine = 1.0 / std::sqrt(2.0);
    expect(std::abs(quiet.rms(quiet.left, 4'800, 19'200) - 0.25 * sine) < 0.25 * sine * 0.02,
           "a sampler plays the zone of the velocity, at its level");
    expect(std::abs(strongestLag(quiet.left, 90, 130) - 109) <= 1, "a sampler plays at the note asked for");
    expect(std::abs(quiet.rms(quiet.right, 4'800, 19'200) / quiet.rms(quiet.left, 4'800, 19'200) - 0.5) < 0.01,
           "a sampler keeps the sample's two channels");
    const auto strong = playSampler(map, values, 69.0, 0.9, 24'000);
    expect(std::abs(strong.rms(strong.left, 4'800, 19'200) - 0.5 * sine) < 0.5 * sine * 0.02,
           "a harder note plays the louder layer");

    // The layers' crossfade: halfway between their centres, both at equal power.
    auto blended = values;
    blended[static_cast<std::size_t>(SamplerSetting::velocityCrossfade)] = 1.0f;
    const auto choice = cydaw::engine::chooseSamplerZones(map, 69, 64.0 / 127.0, true, 0);
    expect(choice.count == 2 && std::abs(choice.weights[0] * choice.weights[0] + choice.weights[1] * choice.weights[1] - 1.0) < 1e-9,
           "between two velocity layers both play, at equal power");

    // The velocity's level: a third of the range down, a third of the dB.
    auto sensitive = values;
    sensitive[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 24.0f;
    const auto lower = playSampler(map, sensitive, 69.0, 0.9, 24'000);
    expect(std::abs(20.0 * std::log10(lower.rms(lower.left, 4'800, 19'200) / strong.rms(strong.left, 4'800, 19'200))
                    + 2.4) < 0.05,
           "the velocity sets the level over its range in dB");

    // Without a loop the note ends with its sample (an octave up: twice as
    // fast); with one it holds while the key is down.
    auto played = 0;
    (void) playSampler(map, values, 69.0, 0.3, 96'000, &played);
    expect(std::abs(played - 48'000) < 16, "a sample without a loop ends where it ends");
    auto looped = map.zones;
    looped[0].loopStart = 24'000;
    looped[0].loopEnd = 48'000;
    looped[0].loopCrossfade = 480;
    cydaw::engine::SamplerMap loopMap;
    loopMap.zones = looped;
    played = 0;
    const auto held = playSampler(loopMap, values, 57.0, 0.3, 288'000, &played);
    expect(played == 288'000 && held.rms(held.left, 240'000, 288'000) > 0.2 * sine,
           "a looped sample holds while the key is down");
    // A note cut from a longer recording (end before the file's): faded out
    // over its last frames, whole until then; not without endFade.
    auto cut = map.zones;
    cut[0].end = 48'000;
    cut[0].endFade = 4'800;
    cydaw::engine::SamplerMap cutMap;
    cutMap.zones = cut;
    played = 0;
    const auto faded = playSampler(cutMap, values, 57.0, 0.3, 60'000, &played);
    cut[0].endFade = 0;
    cutMap.zones = cut;
    const auto abrupt = playSampler(cutMap, values, 57.0, 0.3, 60'000);
    const auto whole = faded.rms(faded.left, 30'000, 40'000);
    expect(std::abs(played - 48'000) < 16 && faded.rms(faded.left, 41'000, 43'000) > 0.95 * whole
               && faded.rms(faded.left, 47'700, 47'990) < 0.05 * whole
               && abrupt.rms(abrupt.left, 47'700, 47'990) > 0.9 * whole,
           "a zone's end fade takes it to silence over its last frames");

    // Round robin: two zones taking turns, one note each.
    cydaw::engine::SamplerMap robin;
    auto first = soft;
    first.highVelocity = 127;
    first.sequenceLength = 2;
    first.sequencePosition = 1;
    auto second = first;
    second.sample = sineSample(220.0, 2.0, 0.5, 0.5);
    second.sequencePosition = 2;
    robin.zones = { first, second };
    const auto one = playSampler(robin, values, 57.0, 0.5, 9'600);
    const auto two = playSampler(robin, values, 57.0, 0.5, 9'600);
    const auto three = playSampler(robin, values, 57.0, 0.5, 9'600);
    expect(one.rms(one.left, 0, 9'600) < 0.6 * two.rms(two.left, 0, 9'600)
               && std::abs(three.rms(three.left, 0, 9'600) - one.rms(one.left, 0, 9'600)) < 1e-6,
           "round-robin zones take turns");

    // A note played before its sample is decoded comes in once it is, where
    // the note would be by then (here half a second in), faded in.
    {
        cydaw::engine::SamplerMap late;
        auto zone = soft;
        zone.highVelocity = 127;
        zone.sample = std::make_shared<cydaw::engine::SampleData>();
        late.zones = { zone };
        cydaw::engine::SamplerVoice voice;
        voice.start(late, values, 57.0, 0.5, 48'000.0);
        auto silent = 0.0;
        for (auto sample = 0; sample < 24'000; ++sample) {
            auto left = 0.0;
            auto right = 0.0;
            voice.next(&late, values, left, right);
            silent = std::max(silent, std::abs(left));
        }
        const auto waiting = voice.active();
        // Decoded now: a sine of 220 Hz, level 0.25, two seconds.
        const auto decoded = sineSample(220.0, 2.0, 0.25, 0.5);
        auto& data = *late.zones[0].sample;
        data.channels = decoded->channels;
        data.frames = decoded->frames;
        data.sampleRate = decoded->sampleRate;
        data.scale = decoded->scale;
        data.pcm = decoded->pcm;
        data.ready.store(true);
        std::vector<double> after;
        for (auto sample = 0; sample < 72'000; ++sample) {
            auto left = 0.0;
            auto right = 0.0;
            voice.next(&late, values, left, right);
            after.push_back(left);
        }
        auto energy = 0.0;
        for (std::size_t index = 4'800; index < 19'200; ++index)
            energy += after[index] * after[index];
        auto ended = 0;
        for (auto index = static_cast<int>(after.size()) - 1; index >= 0 && std::abs(after[static_cast<std::size_t>(index)]) < 1e-9; --index)
            ended = index;
        expect(waiting && silent == 0.0 && std::abs(std::sqrt(energy / 14'400.0) - 0.25 * sine) < 0.25 * sine * 0.03
                   && std::abs(ended - 72'000) < 50 && std::abs(after[0]) < 0.01,
               "a note waits for its sample, then comes in where it would be, faded in");
    }

    // Through the polyphonic voice: the two channels stay two.
    cydaw::engine::PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.waveform = cydaw::engine::PolyOscillatorWaveform::sampler;
    auto shared = std::make_shared<cydaw::engine::SamplerMap>();
    shared->zones = map.zones;
    plan.sampler = shared;
    plan.samplerValues = values;
    plan.oscillatorLevel = 1.0f;
    plan.attackSeconds = 0.001f;
    plan.sustainLevel = 1.0f;
    plan.filterCutoffHz = 20'000.0f;
    cydaw::engine::PolyVoice voice;
    voice.prepare(48'000.0);
    voice.startNote(plan, 69, 0.3f, 48'000.0);
    std::vector<float> left(24'000, 0.0f);
    std::vector<float> right(24'000, 0.0f);
    float* channels[] = { left.data(), right.data() };
    (void) voice.render(plan, channels, 2, 0, 24'000, 48'000.0);
    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    for (std::size_t index = 4'800; index < 19'200; ++index) {
        leftEnergy += static_cast<double>(left[index]) * left[index];
        rightEnergy += static_cast<double>(right[index]) * right[index];
    }
    expect(std::abs(std::sqrt(rightEnergy / leftEnergy) - 0.5) < 0.02, "a sampler voice renders in stereo");
}

// An SFZ played as it moves: its controllers level and choose zones while
// they play, its keyswitches choose an articulation, its first and legato
// zones follow the keys held, a key let go sounds its release.
void samplerPlaysAsItMoves()
{
    using namespace cydaw::engine;
    auto values = defaultSamplerValues();
    values[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 0.0f;
    const auto sine = 1.0 / std::sqrt(2.0);
    const auto take = [&values](const SamplerMap& map, const SamplerCues& cues, const int samples,
                                const std::function<void(int)>& during = {}) {
        SamplerVoice voice;
        voice.start(map, values, 57.0, 0.8, 48'000.0, cues);
        std::vector<double> left;
        for (auto sample = 0; sample < samples; ++sample) {
            if (during)
                during(sample);
            auto l = 0.0;
            auto r = 0.0;
            auto tl = 0.0;
            auto tr = 0.0;
            voice.next(&map, values, cues, l, r, tl, tr);
            left.push_back(l + tl);
        }
        return left;
    };
    const auto rms = [](const std::vector<double>& signal, const std::size_t from, const std::size_t to) {
        auto energy = 0.0;
        for (auto index = from; index < to && index < signal.size(); ++index)
            energy += signal[index] * signal[index];
        return std::sqrt(energy / static_cast<double>(std::max<std::size_t>(1, to - from)));
    };

    // Two microphones of one hit: the close one levelled by CC 70, the room
    // one there only with CC 71 up.
    SamplerMap mics;
    SamplerZone close;
    close.sample = sineSample(220.0, 2.0, 0.5, 1.0);
    close.rootNote = 57;
    close.rules = { SamplerRule { SamplerRuleKind::level, 70, -1, 100.0f, 0.0f } };
    auto room = close;
    room.sample = sineSample(220.0, 2.0, 0.25, 1.0);
    room.rules = { SamplerRule { SamplerRuleKind::range, 71, -1, 1.0f, 127.0f } };
    mics.zones = { close, room };
    mics.controllerStart[70] = 127.0f;
    auto card = unsetSamplerControllers();
    const SamplerCues opening { &card, nullptr, -1.0f };
    const auto alone = take(mics, opening, 9'600);
    expect(std::abs(rms(alone, 2'400, 9'600) - 0.5 * sine) < 0.5 * sine * 0.02,
           "an SFZ opens with its controllers where it sets them (the room microphone off)");
    card[70] = 63.5f;
    const auto half = take(mics, opening, 9'600);
    expect(std::abs(rms(half, 2'400, 9'600) - 0.25 * sine) < 0.25 * sine * 0.02, "a controller levels the zones it reads");
    card[71] = 127.0f;
    expect(chooseSamplerZones(mics, 57, 0.8, false, 0, opening).count == 2, "a controller's range lets a zone in");
    const auto both = take(mics, opening, 9'600);
    expect(std::abs(rms(both, 2'400, 9'600) - 0.5 * sine) < 0.5 * sine * 0.02, "both microphones sound together");

    // The track's MIDI moves it while it plays: the close microphone pulled
    // down over a few milliseconds, without a jump.
    SamplerPlaying playing;
    card = unsetSamplerControllers();
    playing.follow(&mics, card, -1.0f);
    const SamplerCues live { &card, &playing, -1.0f };
    const auto pulled = take(mics, live, 14'400, [&playing](const int sample) {
        if (sample == 4'800)
            playing.controllerMoved(70, 0);
    });
    auto largestStep = 0.0;
    for (std::size_t index = 4'790; index < 6'000; ++index)
        largestStep = std::max(largestStep, std::abs(pulled[index] - pulled[index - 1]));
    expect(rms(pulled, 2'400, 4'800) > 0.3 && rms(pulled, 9'600, 14'400) < 1e-3 && largestStep < 0.05,
           "a controller moved while a note plays takes its level down smoothly");
    playing.controllerMoved(70, 20);
    playing.follow(&mics, card, -1.0f);
    const auto keptMidi = playing.controllers[70] == 20.0f;
    card[70] = 100.0f;
    playing.follow(&mics, card, -1.0f);
    const auto tookCard = playing.controllers[70] == 100.0f;
    playing.controllerMoved(70, 5);
    playing.controllerMoved(121, 0);
    expect(keptMidi && tookCard && playing.controllers[70] == 100.0f,
           "MIDI keeps a controller until the card moves it; a reset puts it back where the card has it");

    // A curve bends the way there: a bipolar one centred at 63.
    SamplerMap curved;
    curved.zones = { close };
    std::array<float, 128> bipolar {};
    for (std::size_t at = 0; at < 128; ++at)
        bipolar[at] = at <= 63 ? -1.0f + static_cast<float>(at) / 63.0f : static_cast<float>(at - 63) / 64.0f;
    curved.curves = { bipolar };
    expect(std::abs(samplerCurve(curved, 0, 63.0f)) < 1e-6f && samplerCurve(curved, 0, 0.0f) == -1.0f
               && samplerCurve(curved, -1, 127.0f) == 1.0f,
           "a rule's curve is the map's, else straight");

    // Keyswitches: two articulations; the instrument's own first, then the
    // last keyswitch pressed, or the card's.
    SamplerMap articulated;
    auto sustained = close;
    sustained.rules.clear();
    sustained.keyswitch = 24;
    auto staccato = sustained;
    staccato.keyswitch = 25;
    articulated.zones = { sustained, staccato };
    articulated.keyswitchKeys[24] = true;
    articulated.keyswitchKeys[25] = true;
    articulated.keyswitchStart = 24;
    SamplerPlaying switching;
    const auto chosen = [&articulated](const SamplerCues& cues) {
        const auto choice = chooseSamplerZones(articulated, 57, 0.8, false, 0, cues);
        return choice.count == 1 ? choice.zones[0] : -1;
    };
    const auto first = chosen({ nullptr, &switching, -1.0f });
    switching.keyswitch = 25;
    const auto pressed = chosen({ nullptr, &switching, -1.0f });
    switching.keyswitch = -1;
    const auto carded = chosen({ nullptr, &switching, 25.0f });
    expect(first == 0 && pressed == 1 && carded == 1 && samplerKeyswitch(articulated, 25) && !samplerKeyswitch(articulated, 57),
           "a keyswitch chooses the articulation the next notes play");

    // First and legato zones: alone, or played over another key.
    SamplerMap phrased;
    auto alonePlayed = close;
    alonePlayed.rules.clear();
    alonePlayed.trigger = SamplerTrigger::first;
    auto overPlayed = alonePlayed;
    overPlayed.trigger = SamplerTrigger::legato;
    phrased.zones = { alonePlayed, overPlayed };
    SamplerPlaying hands;
    const auto lone = chooseSamplerZones(phrased, 57, 0.8, false, 0, { nullptr, &hands, -1.0f });
    hands.heldKeys = 1;
    const auto joined = chooseSamplerZones(phrased, 57, 0.8, false, 0, { nullptr, &hands, -1.0f });
    expect(lone.count == 1 && lone.zones[0] == 0 && joined.count == 1 && joined.zones[0] == 1,
           "a first zone plays a key alone, a legato zone one played over another");

    // The velocity crossfade takes every microphone of each layer.
    SamplerMap layered;
    auto softClose = close;
    softClose.rules.clear();
    softClose.highVelocity = 63;
    auto softRoom = softClose;
    auto loudClose = softClose;
    loudClose.lowVelocity = 64;
    loudClose.highVelocity = 127;
    auto loudRoom = loudClose;
    layered.zones = { softClose, softRoom, loudClose, loudRoom };
    expect(chooseSamplerZones(layered, 57, 64.0 / 127.0, true, 0).count == 4,
           "between two layers, every microphone of both plays");

    // A key let go sounds its release: once through, not shaped by the
    // note's envelope, lower the longer the key was held.
    auto released = std::make_shared<SamplerMap>();
    auto body = close;
    body.rules.clear();
    auto letGo = body;
    letGo.sample = sineSample(440.0, 0.5, 0.25, 1.0);
    letGo.trigger = SamplerTrigger::release;
    letGo.releaseDecayDb = 6.0f;
    released->zones = { body, letGo };
    released->hasReleaseZones = true;
    PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.waveform = PolyOscillatorWaveform::sampler;
    plan.sampler = released;
    plan.samplerValues = values;
    plan.oscillatorLevel = 1.0f;
    plan.attackSeconds = 0.001f;
    plan.sustainLevel = 1.0f;
    plan.releaseSeconds = 0.05f;
    plan.filterCutoffHz = 20'000.0f;
    const auto played = [&plan](const bool chokedInstead, bool* stillThere, bool* endedAfter) {
        PolyVoice voice;
        voice.prepare(48'000.0);
        voice.startNote(plan, 57, 0.8f, 48'000.0);
        std::vector<float> left(96'000, 0.0f);
        std::vector<float> right(96'000, 0.0f);
        float* channels[] = { left.data(), right.data() };
        (void) voice.render(plan, channels, 2, 0, 48'000, 48'000.0);
        if (chokedInstead)
            voice.fadeOut(0.006);
        else
            voice.stopNote(true);
        for (auto at = 48'000; at < 96'000; at += 480) {
            (void) voice.render(plan, channels, 2, at, 480, 48'000.0);
            if (at == 48'000 + 14'400 && stillThere != nullptr)
                *stillThere = voice.isActive();
        }
        if (endedAfter != nullptr)
            *endedAfter = !voice.isActive();
        return left;
    };
    auto stillThere = false;
    auto endedAfter = false;
    const auto letGoTake = played(false, &stillThere, &endedAfter);
    std::vector<double> before(letGoTake.begin(), letGoTake.begin() + 48'000);
    std::vector<double> after(letGoTake.begin() + 48'000, letGoTake.end());
    // Held a second: 6 dB lower.
    const auto expected = 0.25 * sine * std::pow(10.0, -6.0 / 20.0);
    expect(std::abs(rms(before, 4'800, 48'000) - 0.5 * sine) < 0.5 * sine * 0.02,
           "a release zone does not sound while the key is down");
    expect(std::abs(rms(after, 4'800, 14'400) - expected) < expected * 0.05 && stillThere && endedAfter,
           "a key let go plays its release zone through, lower the longer it was held");
    const auto chokedTake = played(true, nullptr, nullptr);
    std::vector<double> cutOff(chokedTake.begin() + 48'000, chokedTake.end());
    expect(rms(cutOff, 4'800, 14'400) < 1e-4, "a note cut (a choke) sounds no release");
}

// A long recording read from the disk while it plays: its head in memory, the
// rest from a ring a streamer fills (here a thread of the test reading the
// whole recording, as the host's reads the file). It sounds as the whole
// one does, forwards and backwards, and gives its rings back.
void samplerStreamsLongRecordings()
{
    using namespace cydaw::engine;
    auto values = defaultSamplerValues();
    values[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 0.0f;
    // Six seconds of a sine at half scale: whole, and with 0.3 s in memory.
    const auto whole = sineSample(220.0, 6.0, 0.5, 1.0);
    auto head = std::make_shared<SampleData>();
    head->channels = 2;
    head->sampleRate = whole->sampleRate;
    head->frames = whole->frames;
    head->scale = whole->scale;
    head->streamed = true;
    head->heldFirst = 0;
    head->heldFrames = 14'400;
    head->pcm.assign(whole->pcm.begin(), whole->pcm.begin() + 14'400 * 2);
    head->ready.store(true);
    SampleStreams::instance().reserve(32);

    // The stand-in streamer: every ring, ahead of (or behind) where it is read.
    std::atomic<bool> stop { false };
    std::thread streamer([&stop, &whole] {
        auto& pool = SampleStreams::instance();
        while (!stop.load()) {
            for (int index = 0; index < pool.size(); ++index) {
                auto& stream = pool.at(index);
                auto state = stream.state.load();
                if (state == SampleStream::wanted) {
                    auto expected = static_cast<int>(SampleStream::wanted);
                    state = stream.state.compare_exchange_strong(expected, SampleStream::filling) ? static_cast<int>(SampleStream::filling)
                                                                                                    : expected;
                }
                if (state == SampleStream::filling) {
                    const auto read = stream.reading.load();
                    if (stream.direction > 0) {
                        const auto first = stream.high.load();
                        const auto limit = std::min<std::int64_t>(whole->frames, read + SampleStream::capacity - SampleStream::margin);
                        for (auto frame = first; frame < limit; ++frame) {
                            auto* cell = stream.cell(frame);
                            const auto value = static_cast<double>(whole->pcm[static_cast<std::size_t>(frame) * 2]) * whole->scale;
                            cell[0] = cell[1] = static_cast<std::int16_t>(std::lround(value * 32'767.0));
                        }
                        if (limit > first) {
                            stream.low.store(std::max(stream.low.load(), limit - SampleStream::capacity));
                            stream.high.store(limit);
                        }
                    } else {
                        const auto first = std::min(stream.low.load(), whole->frames);
                        const auto limit = std::max<std::int64_t>(0, read - SampleStream::capacity + SampleStream::margin);
                        for (auto frame = limit; frame < first; ++frame) {
                            auto* cell = stream.cell(frame);
                            const auto value = static_cast<double>(whole->pcm[static_cast<std::size_t>(frame) * 2]) * whole->scale;
                            cell[0] = cell[1] = static_cast<std::int16_t>(std::lround(value * 32'767.0));
                        }
                        if (first > limit) {
                            stream.high.store(std::min(stream.high.load(), limit + SampleStream::capacity));
                            stream.low.store(limit);
                        }
                    }
                }
                if (state == SampleStream::letGo) {
                    stream.sample.reset();
                    auto expected = static_cast<int>(SampleStream::letGo);
                    (void) stream.state.compare_exchange_strong(expected, SampleStream::free);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    const auto play = [&values](const SampleDataRef& sample, const bool reversed) {
        SamplerMap map;
        SamplerZone zone;
        zone.sample = sample;
        zone.rootNote = 57;
        map.zones = { zone };
        auto played = values;
        played[static_cast<std::size_t>(SamplerSetting::reverse)] = reversed ? 1.0f : 0.0f;
        SamplerVoice voice;
        voice.start(map, played, 57.0, 1.0, 48'000.0);
        std::vector<double> left;
        // Offline, faster than real time: it waits for the streamer.
        const auto waited = samplerMayWait;
        samplerMayWait = true;
        for (auto sample = 0; sample < 288'000; ++sample) {
            auto l = 0.0;
            auto r = 0.0;
            voice.next(&map, played, l, r);
            left.push_back(l);
        }
        samplerMayWait = waited;
        return left;
    };
    const auto rms = [](const std::vector<double>& signal, const std::size_t from, const std::size_t to) {
        auto energy = 0.0;
        for (auto index = from; index < to; ++index)
            energy += signal[index] * signal[index];
        return std::sqrt(energy / static_cast<double>(to - from));
    };
    const auto underrunsBefore = SampleStreams::instance().underruns.load();
    const auto reference = play(whole, false);
    const auto streamed = play(head, false);
    auto largest = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index)
        largest = std::max(largest, std::abs(reference[index] - streamed[index]));
    expect(largest < 1.0e-3 && std::abs(rms(streamed, 200'000, 280'000) - 0.5 / std::sqrt(2.0)) < 0.01,
           "a long recording read from the disk plays as the whole one does (largest difference "
               + std::to_string(largest) + ")");
    const auto backwards = play(head, true);
    expect(std::abs(rms(backwards, 2'400, 280'000) - 0.5 / std::sqrt(2.0)) < 0.01,
           "and backwards, its end streamed first");
    // The voices are gone: the streamer frees their rings.
    for (auto tries = 0; tries < 1'000 && SampleStreams::instance().busy() > 0; ++tries)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop.store(true);
    streamer.join();
    const auto missed = SampleStreams::instance().underruns.load() - underrunsBefore;
    expect(SampleStreams::instance().busy() == 0 && missed == 0,
           "its rings given back (" + std::to_string(SampleStreams::instance().busy()) + " busy), no frame missed ("
               + std::to_string(missed) + ")");
}

// A voice must never ask for memory while it is sounding.
void polyVoiceDoesNotAllocateWhileSounding()
{
    const auto rate = 48000.0;
    for (const auto& mode : voiceModes) {
        cydaw::engine::PolyVoiceOscillator source;
        source.prepare(rate);
        cydaw::engine::PolySynthGraphPlan plan;
        plan.waveform = mode.first;
        source.startNote(plan, 220.0, rate);
        allocation_probe::count.store(0, std::memory_order_relaxed);
        allocation_probe::enabled.store(true, std::memory_order_relaxed);
        for (auto sample = 0; sample < 4800; ++sample)
            (void) source.nextSample(plan, 220.0, rate);
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        expect(allocation_probe::count.load(std::memory_order_relaxed) == 0,
               std::string("voice waveform '") + std::string(mode.second)
                   + "' renders without allocating");
    }
}

// ---- The studio effects (ProEffectsDesign.h) ----------------------------------------------------

// One of the catalogue's effects, its defaults set, then `values` (by the
// parameter's key).
std::unique_ptr<cydaw::engine::AudioProcessor> studioEffect(
    const cydaw::engine::NativeDspNodeKind kind, const std::vector<std::pair<std::string_view, float>>& values,
    const double rate = 48'000.0)
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = kind;
    const auto* effect = effectForKind(kind);
    if (effect == nullptr)
        return {};
    applyEffectDefaults(*effect, node.parameters);
    for (const auto& [key, value] : values)
        if (const auto* parameter = effectParameter(*effect, key))
            setEffectValue(node.parameters, *parameter, value);
    auto processor = makeEffectProcessor(node);
    if (processor != nullptr)
        processor->prepare({ rate, 512, 2 });
    return processor;
}

// Runs a stereo signal (left = right) through, block by block.
std::vector<float> runStereo(cydaw::engine::AudioProcessor& processor, const std::vector<float>& signal,
                             std::vector<float>* right = nullptr)
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 512);
    AudioBuffer out(2, 512);
    std::vector<float> left;
    left.reserve(signal.size());
    cydaw::engine::ProcessContext context;
    for (std::size_t at = 0; at < signal.size(); at += 512) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(512, signal.size() - at));
        const auto input = in.view(count);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            input.channel(0)[frame] = signal[at + frame];
            input.channel(1)[frame] = signal[at + frame];
        }
        const auto output = out.view(count);
        processor.process(input, output, context);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            left.push_back(output.channel(0)[frame]);
            if (right != nullptr)
                right->push_back(output.channel(1)[frame]);
        }
    }
    return left;
}

void limiterProHoldsItsCeiling()
{
    using namespace cydaw::engine;
    // A loud chord of sines, pushed 18 dB into a -1 dB ceiling: no sample, and
    // no point between samples, over it.
    std::vector<float> signal(48'000);
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const auto t = static_cast<double>(i) / 48'000.0;
        signal[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 997.0 * t)
                                       + 0.4 * std::sin(2.0 * std::numbers::pi * 11'025.5 * t + 0.3)
                                       + 0.3 * std::sin(2.0 * std::numbers::pi * 63.0 * t));
    }
    for (int style = 0; style < pro::limiterStyleCount; ++style) {
        auto limiter = studioEffect(NativeDspNodeKind::limiterPro, { { "gain", 18.0f }, { "output", -1.0f }, { "style", static_cast<float>(style) } });
        expect(limiter != nullptr, "the studio limiter is made");
        if (limiter == nullptr)
            return;
        const auto out = runStereo(*limiter, signal);
        const auto ceiling = std::pow(10.0, -1.0 / 20.0);
        auto samplePeak = 0.0;
        auto truePeak = 0.0;
        for (std::size_t i = 4'800; i + 4 < out.size(); ++i) {
            samplePeak = std::max(samplePeak, static_cast<double>(std::abs(out[i])));
            // Between samples, by a four-times windowed-sinc reconstruction.
            for (int phase = 1; phase < 4; ++phase) {
                auto value = 0.0;
                for (int tap = -4; tap < 4; ++tap) {
                    const auto x = static_cast<double>(tap) - static_cast<double>(phase) / 4.0;
                    const auto sinc = std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                    const auto window = 0.5 + 0.5 * std::cos(std::numbers::pi * x / 4.5);
                    value += static_cast<double>(out[i + static_cast<std::size_t>(tap + 4) - 3]) * sinc * window;
                }
                truePeak = std::max(truePeak, std::abs(value));
            }
        }
        expect(samplePeak <= ceiling + 1.0e-6, "the studio limiter keeps every sample under its ceiling");
        expect(truePeak <= ceiling * std::pow(10.0, 0.3 / 20.0), "the studio limiter keeps the true peak within 0.3 dB of its ceiling");
        // It limits, it does not mute: the level stays close to the ceiling.
        auto sum = 0.0;
        for (std::size_t i = 24'000; i < out.size(); ++i)
            sum += static_cast<double>(out[i]) * out[i];
        expect(std::sqrt(sum / 24'000.0) > ceiling * 0.25, "the studio limiter keeps the music loud");
    }
    // Dithered to 16 bits: on the 16-bit grid.
    auto dithered = studioEffect(NativeDspNodeKind::limiterPro, { { "dither", 4.0f }, { "output", -1.0f } });
    const auto out = runStereo(*dithered, signal);
    auto offGrid = 0;
    for (std::size_t i = 1'000; i < out.size(); ++i) {
        const auto steps = static_cast<double>(out[i]) * 32'768.0;
        offGrid += std::abs(steps - std::round(steps)) > 1.0e-3 ? 1 : 0;
    }
    expect(offGrid == 0, "the studio limiter's dither lands on the 16-bit steps");
}

void compressorProKeepsItsSoundAndRange()
{
    using namespace cydaw::engine;
    std::vector<float> signal(48'000);
    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = static_cast<float>(0.7 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / 48'000.0));
    // Clean, the defaults: the gain computer's own reduction, settled.
    auto compressor = studioEffect(NativeDspNodeKind::compressorPro, { { "threshold", -20.0f }, { "ratio", 4.0f }, { "knee", 0.0f },
                                                                              { "attack", 0.05f }, { "release", 2000.0f } });
    expect(compressor != nullptr, "the studio compressor is made");
    if (compressor == nullptr)
        return;
    const auto out = runStereo(*compressor, signal);
    auto inPeak = 0.0;
    auto outPeak = 0.0;
    for (std::size_t i = 40'000; i < out.size(); ++i) {
        inPeak = std::max(inPeak, static_cast<double>(std::abs(signal[i])));
        outPeak = std::max(outPeak, static_cast<double>(std::abs(out[i])));
    }
    const auto expected = pro::compressorGainDb(20.0 * std::log10(0.7), -20.0, 4.0, 0.0);
    const auto measured = 20.0 * std::log10(outPeak / inPeak);
    if (std::abs(measured - expected) >= 1.0)
        std::cerr << "compressor: measured " << measured << " dB, curve " << expected << " dB\n";
    expect(std::abs(measured - expected) < 1.0, "the studio compressor reduces as its curve says");
    // A range of 3 dB: never more.
    auto limited = studioEffect(NativeDspNodeKind::compressorPro,
                                { { "threshold", -40.0f }, { "ratio", 20.0f }, { "knee", 0.0f }, { "range", 3.0f } });
    const auto ranged = runStereo(*limited, signal);
    auto rangedPeak = 0.0;
    for (std::size_t i = 40'000; i < ranged.size(); ++i)
        rangedPeak = std::max(rangedPeak, static_cast<double>(std::abs(ranged[i])));
    expect(20.0 * std::log10(rangedPeak / inPeak) > -3.3, "the studio compressor's range bounds its reduction");
    // Every style runs and stays finite.
    for (int style = 0; style < pro::compressorStyleCount; ++style) {
        auto styled = studioEffect(NativeDspNodeKind::compressorPro,
                                   { { "style", static_cast<float>(style) }, { "lookahead", 5.0f }, { "channels", 1.0f } });
        const auto result = runStereo(*styled, signal);
        expect(std::all_of(result.begin(), result.end(), [](const float x) { return std::isfinite(x) && std::abs(x) < 4.0f; }),
               "every studio compressor style stays finite");
    }
}

void reverbProDecaysAsSet()
{
    using namespace cydaw::engine;
    // An impulse into a fully wet reverb of 1.5 s: the tail falls by 60 dB
    // in about that time (energy measured in 50 ms windows).
    for (const auto decay : { 0.8f, 1.5f, 3.0f }) {
        auto reverb = studioEffect(NativeDspNodeKind::reverbPro, { { "decay", decay }, { "mix", 1.0f }, { "predelay", 0.0f },
                                                                  { "low-decay", 1.0f }, { "mid-decay", 1.0f }, { "high-decay", 1.0f },
                                                                  { "early", -24.0f } });
        expect(reverb != nullptr, "the studio reverb is made");
        if (reverb == nullptr)
            return;
        std::vector<float> signal(static_cast<std::size_t>(48'000.0 * (decay * 1.6 + 0.5)));
        signal[0] = 1.0f;
        std::vector<float> right;
        const auto out = runStereo(*reverb, signal, &right);
        const auto window = static_cast<std::size_t>(2'400);
        const auto energyDb = [&](const std::size_t from) {
            auto sum = 0.0;
            for (std::size_t i = from; i < from + window && i < out.size(); ++i)
                sum += static_cast<double>(out[i]) * out[i] + static_cast<double>(right[i]) * right[i];
            return 10.0 * std::log10(sum + 1.0e-30);
        };
        // The slope between 0.2 s and 0.2 + decay/2: half a decay is -30 dB.
        const auto start = static_cast<std::size_t>(0.2 * 48'000.0);
        const auto later = start + static_cast<std::size_t>(decay * 0.5 * 48'000.0);
        const auto fall = energyDb(start) - energyDb(later);
        expect(fall > 22.0 && fall < 38.0, "the studio reverb's tail falls 30 dB in half its decay");
        auto peak = 0.0;
        for (const auto x : out)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        expect(peak < 1.5 && std::isfinite(peak), "the studio reverb stays bounded");
    }
    // The decay curve the interface draws: longer below, shorter above, as set.
    const auto low = pro::reverbDecayAt(80.0, 2.0, 2.0, 250.0, 1.0, 1'200.0, 0.5, 4'000.0);
    const auto middle = pro::reverbDecayAt(1'200.0, 2.0, 2.0, 250.0, 1.0, 1'200.0, 0.5, 4'000.0);
    const auto high = pro::reverbDecayAt(12'000.0, 2.0, 2.0, 250.0, 1.0, 1'200.0, 0.5, 4'000.0);
    expect(low > 3.2 && low < 4.5 && middle > 1.7 && middle < 2.4 && high > 0.8 && high < 1.25,
           "the studio reverb's decay over frequency follows its three multipliers");
}

void filterProMatchesItsCurve()
{
    using namespace cydaw::engine;
    // A low-pass at 1 kHz, 24 dB, then a high-pass and a peak: sines through
    // it measure what filterMagnitudeDb draws.
    struct Case {
        float type;
        float slope;
        float resonance;
    };
    for (const auto& item : { Case { 0.0f, 1.0f, 0.2f }, Case { 1.0f, 2.0f, 0.4f }, Case { 4.0f, 0.0f, 0.5f } }) {
        auto filter = studioEffect(NativeDspNodeKind::filterPro, { { "f1-type", item.type }, { "f1-slope", item.slope },
                                                                  { "f1-cutoff", 1'000.0f }, { "f1-resonance", item.resonance } });
        expect(filter != nullptr, "the studio filter is made");
        if (filter == nullptr)
            return;
        for (const auto hz : { 250.0, 1'000.0, 3'000.0 }) {
            filter->reset();
            std::vector<float> signal(24'000);
            for (std::size_t i = 0; i < signal.size(); ++i)
                signal[i] = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48'000.0));
            const auto out = runStereo(*filter, signal);
            auto inPeak = 0.0;
            auto outPeak = 0.0;
            for (std::size_t i = 12'000; i < out.size(); ++i) {
                inPeak = std::max(inPeak, static_cast<double>(std::abs(signal[i])));
                outPeak = std::max(outPeak, static_cast<double>(std::abs(out[i])));
            }
            const auto measured = 20.0 * std::log10(outPeak / inPeak);
            const auto drawn = pro::filterMagnitudeDb(static_cast<int>(item.type), static_cast<int>(item.slope), 1'000.0,
                                                      item.resonance, hz);
            if (drawn > -60.0 && std::abs(measured - drawn) >= 1.0)
                std::cerr << "filter type " << item.type << " slope " << item.slope << " at " << hz << " Hz: measured " << measured
                          << " dB, drawn " << drawn << " dB\n";
            expect(drawn > -60.0 ? std::abs(measured - drawn) < 1.0 : measured < -55.0, "the studio filter sounds as its curve is drawn");
        }
    }
    // An LFO on the cutoff moves the sound; nothing blows up at full
    // resonance, drive and modulation.
    auto wild = studioEffect(NativeDspNodeKind::filterPro,
                             { { "f1-resonance", 1.0f }, { "f1-drive", 24.0f }, { "f2-on", 1.0f }, { "f2-type", 5.0f },
                               { "f2-resonance", 1.0f }, { "lfo1-rate", 7.0f }, { "m1-source", 1.0f }, { "m1-destination", 9.0f },
                               { "m1-amount", 1.0f }, { "m2-source", 3.0f }, { "m2-destination", 2.0f }, { "m2-amount", 1.0f } });
    std::vector<float> noise(48'000);
    std::uint32_t random = 12'345;
    for (auto& x : noise) {
        random = random * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>(static_cast<double>(random >> 8) / 16'777'216.0 - 0.5);
    }
    const auto out = runStereo(*wild, noise);
    expect(std::all_of(out.begin(), out.end(), [](const float x) { return std::isfinite(x) && std::abs(x) < 8.0f; }),
           "the studio filter stays finite under full resonance and modulation");
}

// ---- Kick and the electronic drums ---------------------------------------------------------------

// The frequency of a stretch of a signal, by its zero crossings.
double crossingHz(const std::vector<float>& signal, const std::size_t from, const std::size_t to, const double rate)
{
    auto crossings = 0;
    auto first = static_cast<std::size_t>(0);
    auto last = static_cast<std::size_t>(0);
    for (auto i = from + 1; i < std::min(to, signal.size()); ++i)
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f) {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / static_cast<double>(last - first) : 0.0;
}

void kickPlaysItsPitchFall()
{
    using namespace cydaw::engine;
    auto values = kick::defaultValues();
    values[kick::clickType] = 0.0f;
    values[kick::pitchStart] = 400.0f;
    values[kick::pitchEnd] = 50.0f;
    values[kick::pitchTime] = 80.0f;
    values[kick::hold] = 400.0f;
    values[kick::decay] = 400.0f;
    const auto out = kick::renderKick(values, 48'000.0);
    expect(!out.empty(), "Kick renders");
    // Early it is high, late it is at its end pitch.
    const auto early = crossingHz(out, 0, 480, 48'000.0);
    const auto late = crossingHz(out, 9'600, 19'200, 48'000.0);
    expect(early > 200.0, "Kick starts high");
    expect(std::abs(late - 50.0) < 2.0, "Kick settles on its end pitch");
    // An octave up, the note tunes it.
    const auto up = kick::renderKick(values, 48'000.0, kick::referenceNote + 12);
    expect(std::abs(crossingHz(up, 9'600, 19'200, 48'000.0) - 100.0) < 3.0, "Kick follows the note");
    // Its curves are what the voice plays: the amplitude ends with it.
    expect(kick::amplitudeAt(values, 0.2) > 0.99 && kick::amplitudeAt(values, 0.9) == 0.0, "Kick's envelope holds then ends");
    expect(std::all_of(out.begin(), out.end(), [](const float x) { return std::isfinite(x) && std::abs(x) < 4.0f; }),
           "Kick stays finite");
}

void drumRecipesRender()
{
    using namespace cydaw::engine;
    for (const auto& kind : drums::kinds()) {
        drums::Recipe recipe;
        recipe.kind = kind;
        recipe.seed = 42;
        const auto first = drums::render(recipe, 48'000.0);
        const auto again = drums::render(recipe, 48'000.0);
        expect(!first.empty(), "every drum kind renders");
        expect(first == again, "a drum recipe gives the same sound twice");
        auto peak = 0.0f;
        auto finite = true;
        for (const auto x : first) {
            peak = std::max(peak, std::abs(x));
            finite = finite && std::isfinite(x);
        }
        expect(finite, "a drum sound stays finite");
        expect(std::abs(20.0 * std::log10(std::max(1.0e-9f, peak)) + 1.0) < 0.1, "a drum sound peaks at -1 dBFS");
    }
    // Values a recipe gives change the sound; a different seed changes the noise.
    drums::Recipe hat { "hat", { { "decay", 400.0 } }, 1, 0.5, 0.0 };
    drums::Recipe closed { "hat", { { "decay", 40.0 } }, 1, 0.5, 0.0 };
    const auto open = drums::render(hat, 48'000.0);
    const auto shut = drums::render(closed, 48'000.0);
    auto openTail = 0.0;
    auto shutTail = 0.0;
    for (std::size_t i = 12'000; i < open.size(); ++i) {
        openTail += std::abs(open[i]);
        shutTail += std::abs(shut[i]);
    }
    expect(openTail > shutTail * 10.0, "an open hat rings longer than a closed one");
}

// Kick as an instrument's voice: a note starts it, it plays out past the
// note-off, the next one in mono cuts it, the note tunes it.
void kickVoicePlaysOut()
{
    using namespace cydaw::engine;
    constexpr auto rate = 48'000.0;
    PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.waveform = PolyOscillatorWaveform::kick;
    plan.oscillatorLevel = 1.0f;
    plan.kickValues[kick::clickType] = 0.0f;
    const auto play = [&](const int note, const bool letGo, const double cutAt) {
        PolyVoice voice;
        voice.prepare(rate);
        std::vector<float> left(static_cast<std::size_t>(rate));
        std::vector<float> right(left.size());
        expect(voice.startNote(plan, note, 1.0f, rate), "a kick note starts");
        for (std::size_t at = 0; at < left.size(); at += 480) {
            if (letGo && at == 480)
                voice.stopNote(true);
            if (cutAt > 0.0 && at == static_cast<std::size_t>(cutAt * rate))
                voice.fadeOut(0.03);
            float* channels[] { left.data() + at, right.data() + at };
            if (!voice.render(plan, channels, 2, 0, 480, rate))
                break;
        }
        return left;
    };
    const auto rms = [](const std::vector<float>& x, const double from, const double to) {
        auto sum = 0.0;
        const auto a = static_cast<std::size_t>(from * rate);
        const auto b = static_cast<std::size_t>(to * rate);
        for (auto i = a; i < b; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(b - a));
    };
    const auto held = play(kick::referenceNote, false, 0.0);
    const auto released = play(kick::referenceNote, true, 0.0);
    expect(rms(held, 0.05, 0.25) > 0.05, "the kick voice sounds");
    expect(released == held, "a kick plays out whatever the key does");
    expect(rms(held, 0.6, 1.0) < 1.0e-6, "the kick voice ends by itself");
    const auto cut = play(kick::referenceNote, false, 0.1);
    expect(rms(cut, 0.12, 0.3) < 1.0e-6 && rms(cut, 0.0, 0.1) > 0.05, "the next kick cuts the last one quickly");
    const auto up = play(kick::referenceNote + 12, false, 0.0);
    const auto settled = crossingHz(held, 9'600, 14'400, rate);
    expect(std::abs(crossingHz(up, 9'600, 14'400, rate) - 2.0 * settled) < 4.0, "the note tunes the kick voice");
    // The registry knows the card, every setting a parameter of it.
    const auto registry = cydaw::graph::createFactoryPrimitiveRegistry();
    const auto* primitive = registry.find("core.kick-synth");
    expect(primitive != nullptr && primitive->parameters.size() == static_cast<std::size_t>(kick::parameterCount) + 2,
           "Kick is a primitive with its settings");
}

// ---- Amp, cabinet and microphone simulators --------------------------------------------------------

// The level (dB) of a frequency in a stretch of a signal (Goertzel).
double toneLevelDb(const std::vector<float>& signal, const double hz, const std::size_t from, const std::size_t to,
                   const double rate = 48'000.0)
{
    const auto omega = 2.0 * std::numbers::pi * hz / rate;
    const auto coefficient = 2.0 * std::cos(omega);
    double previous {};
    double before {};
    const auto last = std::min(to, signal.size());
    for (auto i = from; i < last; ++i) {
        const auto now = static_cast<double>(signal[i]) + coefficient * previous - before;
        before = previous;
        previous = now;
    }
    const auto power = previous * previous + before * before - coefficient * previous * before;
    const auto amplitude = 2.0 * std::sqrt(std::max(0.0, power)) / static_cast<double>(std::max<std::size_t>(1, last - from));
    return 20.0 * std::log10(std::max(amplitude, 1.0e-12));
}

std::vector<float> sine(const double hz, const double amplitude, const double seconds, const double rate = 48'000.0)
{
    std::vector<float> signal(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return signal;
}

void ampToneStackFollowsItsKnobs()
{
    using namespace cydaw::engine;
    const auto& fender = amp::ampModel(0);
    const auto db = [](const amp::ThirdOrder& filter, const double hz) {
        return 20.0 * std::log10(std::abs(amp::response(filter, hz, 96'000.0)));
    };
    // At noon the American clean's mids are scooped.
    const auto noon = amp::toneStack(fender.stack, 5.0, 5.0, 5.0, 96'000.0);
    expect(db(noon, 400.0) < db(noon, 80.0) - 4.0 && db(noon, 400.0) < db(noon, 5'000.0) - 4.0, "the clean tone stack scoops the mids");
    // Each knob raises its own part.
    const auto bass = amp::toneStack(fender.stack, 10.0, 5.0, 5.0, 96'000.0);
    const auto middle = amp::toneStack(fender.stack, 5.0, 10.0, 5.0, 96'000.0);
    const auto treble = amp::toneStack(fender.stack, 5.0, 5.0, 10.0, 96'000.0);
    expect(db(bass, 60.0) > db(noon, 60.0) + 3.0, "the bass knob raises the lows");
    expect(db(middle, 500.0) > db(noon, 500.0) + 2.0, "the middle knob raises the mids");
    expect(db(treble, 3'000.0) > db(noon, 3'000.0) + 3.0, "the treble knob raises the highs");
    // Every model at every corner stays stable: an impulse dies away.
    for (int model = 0; model < amp::ampModelCount; ++model)
        for (const auto corner : { 0.0, 10.0 }) {
            const auto filter = amp::toneStack(amp::ampModel(model).stack, corner, 10.0 - corner, corner, 96'000.0);
            std::array<double, 4> x {};
            std::array<double, 4> y {};
            auto tail = 0.0;
            for (int n = 0; n < 96'000; ++n) {
                std::rotate(x.rbegin(), x.rbegin() + 1, x.rend());
                std::rotate(y.rbegin(), y.rbegin() + 1, y.rend());
                x[0] = n == 0 ? 1.0 : 0.0;
                y[0] = filter.b[0] * x[0] + filter.b[1] * x[1] + filter.b[2] * x[2] + filter.b[3] * x[3] - filter.a[1] * y[1]
                       - filter.a[2] * y[2] - filter.a[3] * y[3];
                if (n > 86'000)
                    tail = std::max(tail, std::abs(y[0]));
            }
            expect(tail < 1.0e-6, "every tone stack is stable");
        }
}

void ampDistortsAsTurned()
{
    using namespace cydaw::engine;
    const auto signal = sine(220.0, 0.3, 1.0);
    const auto harmonics = [&](const std::vector<float>& out) {
        // The third harmonic against the fundamental, past the start.
        return toneLevelDb(out, 660.0, 9'600, 48'000) - toneLevelDb(out, 220.0, 9'600, 48'000);
    };
    auto clean = studioEffect(NativeDspNodeKind::ampSim, { { "model", 0.0f }, { "gain", 0.0f } });
    auto driven = studioEffect(NativeDspNodeKind::ampSim, { { "model", 3.0f }, { "gain", 10.0f } });
    expect(clean != nullptr && driven != nullptr, "the amp is made");
    if (clean == nullptr || driven == nullptr)
        return;
    const auto cleanOut = runStereo(*clean, signal);
    const auto drivenOut = runStereo(*driven, signal);
    expect(harmonics(drivenOut) > harmonics(cleanOut) + 15.0, "the gain turned up distorts");
    auto peak = 0.0;
    for (const auto x : drivenOut)
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    expect(std::isfinite(peak) && peak < 2.0 && peak > 0.05, "the amp stays bounded");
    // Oversampled, a high note's harmonics fold back far less below it.
    const auto high = sine(4'100.0, 0.3, 1.0);
    auto plain = studioEffect(NativeDspNodeKind::ampSim, { { "model", 3.0f }, { "gain", 10.0f }, { "oversampling", 0.0f } });
    auto fourTimes = studioEffect(NativeDspNodeKind::ampSim, { { "model", 3.0f }, { "gain", 10.0f }, { "oversampling", 2.0f } });
    const auto alias = [&](const std::vector<float>& out) {
        // The 11th harmonic (45.1 kHz) folds to 2.9 kHz.
        return toneLevelDb(out, 2'900.0, 9'600, 48'000) - toneLevelDb(out, 4'100.0, 9'600, 48'000);
    };
    expect(alias(runStereo(*fourTimes, high)) < alias(runStereo(*plain, high)) - 10.0, "oversampling keeps the aliases down");
    // The mix with the dry sound lines up (no comb): half wet, half dry of
    // a clean amp at unity stays near the sum.
    auto gate = studioEffect(NativeDspNodeKind::ampSim, { { "gate", -40.0f } });
    std::vector<float> quiet(48'000, 0.0f);
    for (std::size_t i = 0; i < quiet.size(); ++i)
        quiet[i] = static_cast<float>(0.001 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / 48'000.0));
    const auto gated = runStereo(*gate, quiet);
    auto rest = 0.0;
    for (std::size_t i = 24'000; i < gated.size(); ++i)
        rest = std::max(rest, static_cast<double>(std::abs(gated[i])));
    expect(rest < 1.0e-4, "the gate closes under its threshold");
}

void cabinetMatchesItsCurve()
{
    using namespace cydaw::engine;
    // Microphone A alone, aligned, no room: sines measure what the curve draws.
    std::array<float, amp::cabParameterCount> values {};
    const auto* effect = effectForKind(NativeDspNodeKind::cabSim);
    expect(effect != nullptr, "the cabinet is in the catalogue");
    if (effect == nullptr)
        return;
    for (std::size_t i = 0; i < effect->parameters.size(); ++i)
        values[i] = effect->parameters[i].defaultValue;
    values[amp::cabRoom] = 0.0f;
    auto cabinet = studioEffect(NativeDspNodeKind::cabSim, { { "room", 0.0f } });
    for (const auto hz : { 100.0, 1'000.0, 3'000.0, 8'000.0 }) {
        const auto out = runStereo(*cabinet, sine(hz, 0.1, 0.5));
        const auto measured = toneLevelDb(out, hz, 12'000, 24'000) - 20.0 * std::log10(0.1);
        const auto drawn = amp::cabResponseDb(values, 2, hz, 48'000.0);
        expect(std::abs(measured - drawn) < 1.0, "the cabinet sounds as its curve");
    }
    // A cabinet keeps the fizz out: 8 kHz far under 2 kHz.
    expect(amp::cabResponseDb(values, 2, 8'000.0, 48'000.0) < amp::cabResponseDb(values, 2, 2'000.0, 48'000.0) - 15.0,
           "the cabinet cuts the highs");
    // At the edge of the speaker darker than at its centre.
    auto edge = values;
    edge[amp::cabPositionA] = 1.0f;
    auto centre = values;
    centre[amp::cabPositionA] = 0.0f;
    expect(amp::cabResponseDb(edge, 0, 4'000.0, 48'000.0) < amp::cabResponseDb(centre, 0, 4'000.0, 48'000.0) - 3.0,
           "a microphone at the edge is darker");
    // Two microphones unaligned at different distances comb; aligned they do not.
    auto pair = values;
    pair[amp::cabMicBOn] = 1.0f;
    pair[amp::cabMicB] = pair[amp::cabMicA];
    pair[amp::cabPositionB] = pair[amp::cabPositionA];
    pair[amp::cabDistanceA] = 1.0f;
    pair[amp::cabDistanceB] = 20.0f;
    pair[amp::cabAlign] = 0.0f;
    auto deepest = 0.0;
    auto alignedDeepest = 0.0;
    for (auto hz = 200.0; hz < 4'000.0; hz *= 1.02) {
        deepest = std::min(deepest, amp::cabResponseDb(pair, 2, hz, 48'000.0) - amp::cabResponseDb(pair, 0, hz, 48'000.0));
        auto aligned = pair;
        aligned[amp::cabAlign] = 1.0f;
        alignedDeepest = std::min(alignedDeepest,
                                  amp::cabResponseDb(aligned, 2, hz, 48'000.0) - amp::cabResponseDb(aligned, 0, hz, 48'000.0));
    }
    // Aligned, only their proximity bass differs (1 cm against 20): no notch.
    expect(deepest < -15.0 && alignedDeepest > -3.0, "two microphones out of time comb, aligned they do not");
}

void microphoneTakesItsModel()
{
    using namespace cydaw::engine;
    std::array<float, amp::micParameterCount> values {};
    const auto* effect = effectForKind(NativeDspNodeKind::micSim);
    expect(effect != nullptr, "the microphone is in the catalogue");
    if (effect == nullptr)
        return;
    for (std::size_t i = 0; i < effect->parameters.size(); ++i)
        values[i] = effect->parameters[i].defaultValue;
    // Close, a directional microphone gains bass.
    values[amp::micModelValue] = 0.0f;
    auto close = values;
    close[amp::micDistance] = 1.0f;
    auto far = values;
    far[amp::micDistance] = 80.0f;
    expect(amp::micResponseDb(close, 100.0, 48'000.0) > amp::micResponseDb(far, 100.0, 48'000.0) + 5.0, "close up, proximity bass");
    // An omni pattern does not.
    close[amp::micPattern] = 1.0f;
    far[amp::micPattern] = 1.0f;
    expect(std::abs(amp::micResponseDb(close, 100.0, 48'000.0) - amp::micResponseDb(far, 100.0, 48'000.0)) < 0.5,
           "an omni has no proximity effect");
    // Behind a cardioid, little; a figure of eight hears its back.
    auto behind = values;
    behind[amp::micAngle] = 180.0f;
    behind[amp::micPattern] = 2.0f;
    expect(amp::micResponseDb(behind, 1'000.0, 48'000.0) < amp::micResponseDb(values, 1'000.0, 48'000.0) - 20.0,
           "a cardioid rejects what is behind it");
    // The telephone keeps a narrow band.
    auto phone = values;
    phone[amp::micModelValue] = 8.0f;
    expect(amp::micResponseDb(phone, 100.0, 48'000.0) < amp::micResponseDb(phone, 1'000.0, 48'000.0) - 20.0
               && amp::micResponseDb(phone, 8'000.0, 48'000.0) < amp::micResponseDb(phone, 1'000.0, 48'000.0) - 20.0,
           "the telephone keeps its narrow band");
    // Every model plays, bounded, without the drive making it explode.
    for (int model = 0; model < amp::micModelCount; ++model) {
        auto mic = studioEffect(NativeDspNodeKind::micSim, { { "model", static_cast<float>(model) }, { "drive", 24.0f }, { "room", 0.5f } });
        const auto out = runStereo(*mic, sine(440.0, 0.5, 0.3));
        auto peak = 0.0;
        for (const auto x : out)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        expect(std::isfinite(peak) && peak < 3.0 && peak > 1.0e-3, "every microphone plays, bounded");
    }
}

void ampSwitchesChannels()
{
    using namespace cydaw::engine;
    const auto signal = sine(220.0, 0.3, 1.0);
    const auto harmonics = [&](const std::vector<float>& out) {
        return toneLevelDb(out, 660.0, 9'600, 48'000) - toneLevelDb(out, 220.0, 9'600, 48'000);
    };
    // A clean on A, B the modern lead with A's other knobs.
    std::vector<std::pair<std::string_view, float>> setting { { "model", 0.0f },    { "gain", 0.0f },       { "model-b", 3.0f },
                                                              { "gain-b", 10.0f },  { "middle-b", 5.0f },   { "treble-b", 5.0f },
                                                              { "presence-b", 5.0f } };
    auto onA = studioEffect(NativeDspNodeKind::ampSim, setting);
    setting.emplace_back("channel", 1.0f);
    auto onB = studioEffect(NativeDspNodeKind::ampSim, setting);
    auto lead = studioEffect(NativeDspNodeKind::ampSim, { { "model", 3.0f }, { "gain", 10.0f } });
    expect(onA != nullptr && onB != nullptr && lead != nullptr, "the amp with two channels is made");
    if (onA == nullptr || onB == nullptr || lead == nullptr)
        return;
    const auto a = runStereo(*onA, signal);
    const auto b = runStereo(*onB, signal);
    expect(harmonics(b) > harmonics(a) + 15.0, "channel B plays its own amp");
    auto apart = 0.0;
    const auto alone = runStereo(*lead, signal);
    for (std::size_t i = 0; i < b.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(b[i] - alone[i])));
    expect(apart < 1.0e-5, "channel B is a whole amp, as A set the same");
    // Switched while it plays: no click, the other channel there 20 ms on.
    const auto* effect = effectForKind(NativeDspNodeKind::ampSim);
    NativeDspNode node;
    node.id = 1;
    node.kind = NativeDspNodeKind::ampSim;
    applyEffectDefaults(*effect, node.parameters);
    for (const auto& [key, value] : setting)
        if (key != "channel")
            setEffectValue(node.parameters, *effectParameter(*effect, key), value);
    auto amp = makeEffectProcessor(node);
    amp->prepare({ 48'000.0, 512, 2 });
    const std::vector<float> first(signal.begin(), signal.begin() + 24'064);
    const std::vector<float> second(signal.begin() + 24'064, signal.end());
    auto switched = runStereo(*amp, first);
    setEffectValue(node.parameters, *effectParameter(*effect, "channel"), 1.0f);
    expect(updateEffectProcessor(*amp, node), "the amp takes its channel while it plays");
    const auto after = runStereo(*amp, second);
    switched.insert(switched.end(), after.begin(), after.end());
    // The steepest step (second difference) about the switch against the
    // steepest of either channel playing on its own.
    const auto steepest = [](const std::vector<float>& x, const std::size_t from, const std::size_t to) {
        auto most = 0.0;
        for (auto i = std::max<std::size_t>(from, 2); i < std::min(to, x.size()); ++i)
            most = std::max(most, std::abs(static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
        return most;
    };
    const auto steady = std::max(steepest(a, 12'000, 48'000), steepest(b, 12'000, 48'000));
    expect(steepest(switched, 23'500, 26'500) < steady * 1.2, "switching channels does not click");
    auto difference = 0.0;
    auto level = 0.0;
    for (std::size_t i = 38'000; i < 48'000; ++i) {
        difference += (static_cast<double>(switched[i]) - b[i]) * (static_cast<double>(switched[i]) - b[i]);
        level += static_cast<double>(b[i]) * b[i];
    }
    expect(difference < level * 0.01, "after the switch, channel B alone");
}

// ---- Mixing effects: Pression, Fournaise, Écho Bande ---------------------------------------------

void pressionSplitsFlatAndPresses()
{
    using namespace cydaw::engine;
    // At no depth, the three bands sum back to the sound: flat.
    auto flat = studioEffect(NativeDspNodeKind::pression, { { "depth", 0.0f } });
    expect(flat != nullptr, "Pression is made");
    if (flat == nullptr)
        return;
    for (const auto hz : { 60.0, 88.0, 500.0, 2'500.0, 9'000.0 }) {
        auto fresh = studioEffect(NativeDspNodeKind::pression, { { "depth", 0.0f } });
        const auto out = runStereo(*fresh, sine(hz, 0.25, 0.5));
        const auto gain = toneLevelDb(out, hz, 12'000, 24'000) - 20.0 * std::log10(0.25);
        expect(std::abs(gain) < 0.2, "Pression's bands sum flat");
    }
    // At full depth, the quiet is raised and the loud held down: 40 dB apart
    // in, much less out.
    const auto level = [](const double amplitude) {
        auto pression = studioEffect(NativeDspNodeKind::pression, {});
        const auto out = runStereo(*pression, sine(1'000.0, amplitude, 1.5));
        return toneLevelDb(out, 1'000.0, 48'000, 72'000);
    };
    const auto quiet = level(0.005);
    const auto loud = level(0.5);
    expect(quiet > 20.0 * std::log10(0.005) + 15.0, "Pression raises the quiet");
    expect(loud < 20.0 * std::log10(0.5), "Pression holds the loud down");
    expect(loud - quiet < 20.0, "Pression brings the loud and the quiet together");
    // The design: a band's gain at its reference level is undone by its makeup.
    const mix::PressionBand band { -30.0, 10.0, -45.0, 3.0 };
    expect(std::abs(mix::pressionOutputDb(band, mix::pressionReferenceDb, 1.0, 1.0, 1.0, 0.0) - mix::pressionReferenceDb) < 1.0e-9,
           "Pression leaves its reference level where it is");
}

void fournaiseSaturatesInCharacter()
{
    using namespace cydaw::engine;
    const auto signal = sine(200.0, 0.3, 1.0);
    const auto harmonic = [](const std::vector<float>& out, const double which) {
        return toneLevelDb(out, 200.0 * which, 24'000, 48'000) - toneLevelDb(out, 200.0, 24'000, 48'000);
    };
    auto clean = studioEffect(NativeDspNodeKind::fournaise, { { "drive", 0.0f } });
    expect(clean != nullptr, "Fournaise is made");
    if (clean == nullptr)
        return;
    const auto cleanOut = runStereo(*clean, signal);
    for (int style = 0; style < mix::fournaiseStyleCount; ++style) {
        auto hot = studioEffect(NativeDspNodeKind::fournaise, { { "drive", 8.0f }, { "style", static_cast<float>(style) } });
        const auto out = runStereo(*hot, signal);
        expect(harmonic(out, 3.0) > harmonic(cleanOut, 3.0) + 20.0 || harmonic(out, 2.0) > harmonic(cleanOut, 2.0) + 20.0,
               "every character of Fournaise saturates");
        // Its automatic level brings it back near the sound's.
        expect(std::abs(toneLevelDb(out, 200.0, 24'000, 48'000) - toneLevelDb(cleanOut, 200.0, 24'000, 48'000)) < 6.0,
               "Fournaise's automatic level keeps the level");
        auto peak = 0.0;
        for (const auto x : out)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        expect(std::isfinite(peak) && peak < 2.0, "Fournaise stays bounded");
    }
    // Tape is symmetric: odd harmonics; the valve is not: even ones.
    auto tape = studioEffect(NativeDspNodeKind::fournaise, { { "drive", 8.0f }, { "style", 0.0f } });
    auto valve = studioEffect(NativeDspNodeKind::fournaise, { { "drive", 8.0f }, { "style", 3.0f } });
    const auto tapeOut = runStereo(*tape, signal);
    const auto valveOut = runStereo(*valve, signal);
    expect(harmonic(tapeOut, 2.0) < harmonic(tapeOut, 3.0) - 20.0, "tape saturates in odd harmonics");
    expect(harmonic(valveOut, 2.0) > harmonic(tapeOut, 2.0) + 20.0, "the valve adds even harmonics");
}

void echoBandeRepeatsOnItsHeads()
{
    using namespace cydaw::engine;
    std::vector<float> impulse(48'000 * 2, 0.0f);
    impulse[0] = 1.0f;
    // The loudest moment of the wet sound after a time (seconds).
    const auto firstEcho = [](const std::vector<float>& out, const double from) {
        auto best = 0.0;
        std::size_t at = 0;
        for (auto i = static_cast<std::size_t>(from * 48'000.0); i < out.size(); ++i)
            if (std::abs(out[i]) > best + 1.0e-6) {
                best = std::abs(out[i]);
                at = i;
            }
        return std::make_pair(static_cast<double>(at) / 48'000.0, best);
    };
    const auto run = [&](const std::vector<std::pair<std::string_view, float>>& values) {
        std::vector<std::pair<std::string_view, float>> all { { "mix", 1.0f }, { "wow", 0.0f }, { "age", 0.0f }, { "reverb", 0.0f } };
        all.insert(all.end(), values.begin(), values.end());
        auto echo = studioEffect(NativeDspNodeKind::echoBande, all);
        return runStereo(*echo, impulse);
    };
    // Head 1 alone, 100 ms: the echo at 100 ms, the next at 200 ms, quieter.
    const auto one = run({ { "mode", 0.0f }, { "time", 100.0f }, { "intensity", 0.5f } });
    std::vector<float> early(one.begin(), one.begin() + 7'200);
    const auto [firstAt, firstLevel] = firstEcho(early, 0.05);
    expect(std::abs(firstAt - 0.1) < 0.002, "the first head echoes at its time");
    std::vector<float> later(one.begin() + 7'200, one.begin() + 12'000);
    const auto [secondAt, secondLevel] = firstEcho(later, 0.0);
    expect(std::abs(secondAt + 0.15 - 0.2) < 0.003 && secondLevel < firstLevel, "the echo repeats, quieter");
    // Head 3 alone: almost three times as late.
    const auto three = run({ { "mode", 2.0f }, { "time", 100.0f }, { "intensity", 0.0f } });
    expect(std::abs(firstEcho(three, 0.05).first - 0.292) < 0.003, "the third head plays later");
    // Synced to a quarter note at 120 BPM: half a second.
    const auto synced = run({ { "mode", 0.0f }, { "sync", 9.0f }, { "intensity", 0.0f } });
    expect(std::abs(firstEcho(synced, 0.05).first - 0.5) < 0.003, "the echo follows the tempo");
    // The spring alone rings and dies away.
    const auto spring = run({ { "mode", 11.0f }, { "reverb", 1.0f } });
    auto ringing = 0.0;
    auto rest = 0.0;
    for (std::size_t i = 2'400; i < 24'000; ++i)
        ringing += static_cast<double>(spring[i]) * spring[i];
    for (std::size_t i = 72'000; i < spring.size(); ++i)
        rest += static_cast<double>(spring[i]) * spring[i];
    expect(ringing > 1.0e-4 && rest < ringing * 1.0e-3, "the spring rings and dies away");
    // Run away (all heads, intensity past 1): held by the tape.
    auto runaway = studioEffect(NativeDspNodeKind::echoBande, { { "mode", 10.0f }, { "intensity", 1.2f }, { "time", 60.0f } });
    const auto wild = runStereo(*runaway, sine(300.0, 0.5, 2.0));
    auto peak = 0.0;
    for (const auto x : wild)
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    expect(std::isfinite(peak) && peak < 4.0, "a runaway echo stays bounded");
}

void multibandSplitsAndActs()
{
    using namespace cydaw::engine;
    // No amount: four bands that sum flat.
    for (const auto hz : { 60.0, 120.0, 400.0, 1'000.0, 3'000.0, 6'000.0, 12'000.0 }) {
        auto flat = studioEffect(NativeDspNodeKind::multibandPro, { { "amount", 0.0f } });
        const auto out = runStereo(*flat, sine(hz, 0.25, 0.5));
        expect(std::abs(toneLevelDb(out, hz, 12'000, 24'000) - 20.0 * std::log10(0.25)) < 0.2, "Multibande Pro's bands sum flat");
    }
    // The second band compressed hard, the third off: 400 Hz held down, 3 kHz left.
    const std::vector<std::pair<std::string_view, float>> pressed { { "threshold-2", -40.0f }, { "band2-ratio", 10.0f },
                                                                    { "band2-range", 24.0f }, { "band3-on", 0.0f } };
    auto held = studioEffect(NativeDspNodeKind::multibandPro, pressed);
    auto left = studioEffect(NativeDspNodeKind::multibandPro, pressed);
    expect(toneLevelDb(runStereo(*held, sine(400.0, 0.3, 0.5)), 400.0, 12'000, 24'000) < 20.0 * std::log10(0.3) - 6.0,
           "a band compressed holds its band down");
    expect(std::abs(toneLevelDb(runStereo(*left, sine(3'000.0, 0.3, 0.5)), 3'000.0, 12'000, 24'000) - 20.0 * std::log10(0.3)) < 0.3,
           "a band off leaves its band");
    // Expanded (a gate): a quiet 400 Hz pushed down by its range.
    auto gate = studioEffect(NativeDspNodeKind::multibandPro, { { "band2-mode", 1.0f }, { "threshold-2", -20.0f } });
    expect(toneLevelDb(runStereo(*gate, sine(400.0, 0.01, 0.5)), 400.0, 12'000, 24'000) < 20.0 * std::log10(0.01) - 10.0,
           "a band expanded pushes the quiet down");
    // The top band alone: 400 Hz gone.
    auto solo = studioEffect(NativeDspNodeKind::multibandPro, { { "band4-solo", 1.0f }, { "amount", 0.0f } });
    expect(toneLevelDb(runStereo(*solo, sine(400.0, 0.3, 0.5)), 400.0, 12'000, 24'000) < 20.0 * std::log10(0.3) - 30.0,
           "a band heard alone leaves the others out");
}

void veloursCalmsResonances()
{
    using namespace cydaw::engine;
    constexpr auto lag = mix::veloursLatency;
    // Nothing taken: the sound back, as it was, later by its frames.
    auto clear = studioEffect(NativeDspNodeKind::velours, { { "depth", 0.0f } });
    expect(clear != nullptr && clear->latencySamples() == static_cast<std::uint32_t>(lag), "Velours says its latency");
    if (clear == nullptr)
        return;
    std::vector<float> noise(48'000);
    std::uint32_t seed = 12'345u;
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * 0.02);
    }
    const auto back = runStereo(*clear, noise);
    auto apart = 0.0;
    for (std::size_t i = 8'000; i < noise.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(back[i] - noise[i - lag])));
    expect(apart < 1.0e-4, "Velours gives the sound back whole when it takes nothing");
    // A resonance standing out of the noise is calmed; the noise around it stays.
    auto ringing = noise;
    for (std::size_t i = 0; i < ringing.size(); ++i)
        ringing[i] += static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * 2'000.0 * static_cast<double>(i) / 48'000.0));
    auto calm = studioEffect(NativeDspNodeKind::velours, { { "depth", 12.0f } });
    const auto calmed = runStereo(*calm, ringing);
    const auto before = toneLevelDb(ringing, 2'000.0, 12'000, 36'000);
    const auto after = toneLevelDb(calmed, 2'000.0, 12'000 + lag, 36'000 + lag);
    expect(after < before - 4.0, "Velours calms a resonance");
    expect(std::abs(toneLevelDb(calmed, 7'000.0, 12'000 + lag, 36'000 + lag) - toneLevelDb(ringing, 7'000.0, 12'000, 36'000)) < 3.0,
           "Velours leaves the noise around it");
}

void patineAgesTheSound()
{
    using namespace cydaw::engine;
    // Every module at nothing: the sound as it was, later by the wobble's line.
    auto none = studioEffect(NativeDspNodeKind::patine, { { "noise", 0.0f }, { "wobble", 0.0f }, { "distort", 0.0f },
                                                          { "digital", 0.0f }, { "space", 0.0f }, { "magnetic", 0.0f } });
    const auto signal = sine(440.0, 0.3, 0.5);
    const auto lag = static_cast<std::size_t>(none->latencySamples());
    const auto same = runStereo(*none, signal);
    auto apart = 0.0;
    for (std::size_t i = lag; i < signal.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(same[i] - signal[i - lag])));
    expect(lag == 240 && apart < 1.0e-4, "Patine at nothing leaves the sound");
    // Its noise plays under silence.
    auto hiss = studioEffect(NativeDspNodeKind::patine, { { "noise", 1.0f }, { "noise-type", 1.0f } });
    const auto quiet = runStereo(*hiss, std::vector<float>(48'000, 0.0f));
    auto energy = 0.0;
    for (const auto x : quiet)
        energy += static_cast<double>(x) * x;
    expect(10.0 * std::log10(energy / static_cast<double>(quiet.size()) + 1.0e-20) > -60.0, "Patine's noise plays");
    // Everything at once: changed, bounded.
    for (int type = 0; type < 3; ++type) {
        auto worn = studioEffect(NativeDspNodeKind::patine, { { "noise", 1.0f }, { "wobble", 1.0f }, { "distort", 1.0f }, { "digital", 1.0f },
                                                              { "space", 1.0f }, { "magnetic", 1.0f }, { "noise-type", static_cast<float>(type) },
                                                              { "distort-type", static_cast<float>(type) } });
        const auto out = runStereo(*worn, signal);
        auto peak = 0.0;
        auto difference = 0.0;
        for (std::size_t i = lag; i < out.size(); ++i) {
            peak = std::max(peak, static_cast<double>(std::abs(out[i])));
            difference = std::max(difference, static_cast<double>(std::abs(out[i] - signal[i - lag])));
        }
        expect(std::isfinite(peak) && peak < 2.0 && difference > 0.05, "Patine wears the sound, bounded");
    }
}

void sonometrePassesUntouched()
{
    using namespace cydaw::engine;
    auto meter = studioEffect(NativeDspNodeKind::sonometre, {});
    const auto signal = sine(1'000.0, 0.5, 0.2);
    const auto out = runStereo(*meter, signal);
    expect(out == signal, "the Sonometre leaves the sound as it is");
}

void trancheShapesAChannel()
{
    using namespace cydaw::engine;
    const auto signal = sine(1'000.0, 0.5, 0.5);
    // Its stages flat or off: the sound as it was.
    auto plain = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f } });
    expect(plain != nullptr, "Tranche is made");
    if (plain == nullptr)
        return;
    const auto same = runStereo(*plain, signal);
    auto apart = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(same[i] - signal[i])));
    expect(apart < 1.0e-4, "Tranche flat leaves the sound");
    // The compressor: 12 dB over a threshold at 3:1 comes 8 dB down.
    auto pressed = studioEffect(NativeDspNodeKind::tranche, { { "comp-threshold", -18.0f }, { "comp-ratio", 3.0f }, { "comp-knee", 0.0f } });
    const auto reduction = 20.0 * std::log10(0.5) - toneLevelDb(runStereo(*pressed, signal), 1'000.0, 12'000, 24'000);
    expect(reduction > 6.5 && reduction < 9.0, "Tranche's compressor takes what its ratio says");
    // The gate shuts a quiet sound by its range.
    auto gate = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f }, { "gate-on", 1.0f }, { "gate-threshold", -40.0f } });
    expect(toneLevelDb(runStereo(*gate, sine(1'000.0, 0.001, 0.5)), 1'000.0, 12'000, 24'000) < 20.0 * std::log10(0.001) - 30.0,
           "Tranche's gate shuts the quiet");
    // A bell of the equaliser raises its frequency.
    auto bell = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f }, { "bell1-hz", 1'000.0f }, { "bell1-db", 6.0f } });
    expect(std::abs(toneLevelDb(runStereo(*bell, sine(1'000.0, 0.1, 0.5)), 1'000.0, 12'000, 24'000) - 20.0 * std::log10(0.1) - 6.0) < 0.3,
           "Tranche's bell raises its frequency");
    // Transients: more attack makes the start louder, less sustain the tail quieter.
    std::vector<float> hit(48'000, 0.0f);
    for (std::size_t i = 4'800; i < hit.size(); ++i) {
        const auto t = static_cast<double>(i - 4'800) / 48'000.0;
        hit[i] = static_cast<float>(0.5 * std::exp(-t * 8.0) * std::sin(2.0 * std::numbers::pi * 200.0 * t));
    }
    const auto rms = [](const std::vector<float>& x, const std::size_t from, const std::size_t to) {
        auto sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(to - from));
    };
    auto flat = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f }, { "transient-on", 1.0f } });
    auto punchy = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f }, { "transient-on", 1.0f }, { "attack", 1.0f } });
    auto short_ = studioEffect(NativeDspNodeKind::tranche, { { "comp-on", 0.0f }, { "transient-on", 1.0f }, { "sustain", -1.0f } });
    const auto plainHit = runStereo(*flat, hit);
    expect(rms(runStereo(*punchy, hit), 4'800, 5'500) > rms(plainHit, 4'800, 5'500) * 1.3, "more attack, a louder start");
    expect(rms(runStereo(*short_, hit), 14'400, 28'800) < rms(plainHit, 14'400, 28'800) * 0.8, "less sustain, a quieter tail");
}

void finitionMastersWithinItsCeiling()
{
    using namespace cydaw::engine;
    // Every stage off: the sound as it was, later by the maximizer's look.
    auto off = studioEffect(NativeDspNodeKind::finition, { { "eq-on", 0.0f }, { "dynamics-on", 0.0f }, { "exciter-on", 0.0f },
                                                          { "imager-on", 0.0f }, { "maximizer-on", 0.0f } });
    expect(off != nullptr, "Finition is made");
    if (off == nullptr)
        return;
    const auto lag = static_cast<std::size_t>(off->latencySamples());
    const auto signal = sine(440.0, 0.3, 0.3);
    const auto same = runStereo(*off, signal);
    auto apart = 0.0;
    for (std::size_t i = lag; i < signal.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(same[i] - signal[i - lag])));
    expect(lag == 72 && apart < 1.0e-5, "Finition with nothing on leaves the sound, later by its look ahead");
    // Its bands with nothing done to them sum flat.
    for (const auto hz : { 80.0, 150.0, 1'000.0, 4'000.0, 10'000.0 }) {
        auto bands = studioEffect(NativeDspNodeKind::finition, { { "eq-on", 0.0f }, { "dynamics", 0.0f }, { "exciter-on", 0.0f },
                                                                { "low-width", 1.0f }, { "high-width", 1.0f }, { "maximizer-on", 0.0f } });
        const auto out = runStereo(*bands, sine(hz, 0.2, 0.5));
        expect(std::abs(toneLevelDb(out, hz, 12'000, 24'000) - 20.0 * std::log10(0.2)) < 0.2, "Finition's bands sum flat");
    }
    // Pushed 12 dB into a ceiling of -1 dB: nothing past it.
    std::vector<float> noise(48'000);
    std::uint32_t seed = 99u;
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * 0.5);
    }
    auto loud = studioEffect(NativeDspNodeKind::finition, { { "gain", 12.0f }, { "ceiling", -1.0f } });
    const auto out = runStereo(*loud, noise);
    auto peak = 0.0;
    for (const auto x : out)
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    expect(peak <= std::pow(10.0, -1.0 / 20.0) + 1.0e-6, "Finition's maximizer keeps its ceiling");
}

void haloRingsInCharacter()
{
    using namespace cydaw::engine;
    std::vector<float> impulse(48'000 * 4, 0.0f);
    impulse[0] = 1.0f;
    const auto energyDb = [](const std::vector<float>& x, const double from, const double to) {
        auto sum = 0.0;
        const auto a = static_cast<std::size_t>(from * 48'000.0);
        const auto b = std::min(x.size(), static_cast<std::size_t>(to * 48'000.0));
        for (auto i = a; i < b; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(sum / static_cast<double>(std::max<std::size_t>(1, b - a)) + 1.0e-30);
    };
    // Every algorithm and colour rings, then dies away, bounded.
    for (int mode = 0; mode < halo::haloModeCount; ++mode)
        for (int colour = 0; colour < halo::haloColourCount; ++colour) {
            auto reverb = studioEffect(NativeDspNodeKind::halo, { { "mix", 1.0f }, { "decay", 1.5f }, { "predelay", 0.0f },
                                                                  { "mode", static_cast<float>(mode) }, { "colour", static_cast<float>(colour) } });
            const auto out = runStereo(*reverb, impulse);
            auto peak = 0.0;
            for (const auto x : out)
                peak = std::max(peak, static_cast<double>(std::abs(x)));
            const auto ringing = energyDb(out, 0.02, 0.3);
            const auto later = energyDb(out, 3.2, 3.9);
            expect(std::isfinite(peak) && peak < 2.0, "every Halo stays bounded");
            expect(ringing > -90.0 && later < ringing - 40.0, "every Halo rings and dies away");
        }
    // Its decay as set: a second later, about 30 dB down at 2 s.
    auto hall = studioEffect(NativeDspNodeKind::halo, { { "mix", 1.0f }, { "decay", 2.0f }, { "predelay", 0.0f }, { "mode", 0.0f },
                                                        { "bass-multiplier", 1.0f }, { "damping", 20'000.0f } });
    const auto hallOut = runStereo(*hall, impulse);
    const auto fall = energyDb(hallOut, 0.4, 0.6) - energyDb(hallOut, 1.4, 1.6);
    expect(fall > 20.0 && fall < 42.0, "Halo decays as its decay says");
    // Gated: dense for its time, then nothing.
    auto gated = studioEffect(NativeDspNodeKind::halo, { { "mix", 1.0f }, { "decay", 0.5f }, { "predelay", 0.0f }, { "mode", 9.0f } });
    const auto gatedOut = runStereo(*gated, impulse);
    expect(energyDb(gatedOut, 0.6, 1.0) < energyDb(gatedOut, 0.05, 0.25) - 50.0, "the nonlinear Halo shuts after its time");
    // On noise its level stays near the sound's; at no mix, the sound as it was.
    std::vector<float> noise(48'000);
    std::uint32_t seed = 7u;
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * 0.3);
    }
    auto wet = studioEffect(NativeDspNodeKind::halo, { { "mix", 1.0f } });
    const auto wetOut = runStereo(*wet, noise);
    const auto level = energyDb(wetOut, 0.5, 1.0) - energyDb(noise, 0.5, 1.0);
    expect(level > -15.0 && level < 6.0, "Halo's tail sits near the sound's level");
    auto dry = studioEffect(NativeDspNodeKind::halo, { { "mix", 0.0f } });
    expect(runStereo(*dry, noise) == noise, "Halo at no mix leaves the sound");
}

void ambrePlaysInTune()
{
    using namespace cydaw::engine;
    using synth::Model;
    // Its table: the shared voicing first, its own after, in order.
    expect(synth::parameterCount(Model::ambre) == 58 && synth::parameterIndex(Model::ambre, "osc1.wave") == synth::voicingFieldCount
               && synth::parameterIndex(Model::ambre, "mod.wheel") == 57 && synth::parameterIndex(Model::ambre, "voice.master") == synth::voicingMaster,
           "Ambre's settings are in their order");
    expect(synth::modelForType("ambre-synth") == Model::ambre && synth::modelForPrefix("ambre") == Model::ambre
               && synth::modelForProcessor("core.ambre-synth") == Model::ambre && synth::modelOf(1.0f) == Model::ambre,
           "Ambre is found by its type, prefix and primitive");
    constexpr auto rate = 48'000.0;
    const prism::Context context;
    const auto play = [&](synth::Values values, const int note, const double seconds, const double releaseAt, std::vector<float>* right = nullptr) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::ambre, values, note, 1.0f, -1.0, rate);
        std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> other(left.size(), 0.0f);
        for (std::size_t at = 0; at < left.size(); at += 256) {
            if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
            voice.render(values, context, left.data() + at, other.data() + at, count, rate);
        }
        if (right != nullptr)
            *right = other;
        return left;
    };
    auto values = synth::defaultValues(Model::ambre);
    const auto a4 = play(values, 69, 1.0, -1.0);
    const auto at440 = toneLevelDb(a4, 440.0, 9'600, 48'000);
    expect(at440 > -40.0 && at440 > toneLevelDb(a4, 415.0, 9'600, 48'000) + 10.0 && at440 > toneLevelDb(a4, 466.0, 9'600, 48'000) + 10.0,
           "Ambre plays A4 at 440 Hz");
    // Released, it ends.
    const auto released = play(values, 60, 2.5, 0.5);
    auto tail = 0.0;
    for (std::size_t i = static_cast<std::size_t>(2.2 * rate); i < released.size(); ++i)
        tail = std::max(tail, static_cast<double>(std::abs(released[i])));
    expect(tail < 1.0e-4, "Ambre's note ends after its release");
    // Every filter, its resonance up: bounded, sounding.
    for (int model = 0; model < 4; ++model)
        for (int mode = 0; mode < 3; ++mode) {
            auto resonant = values;
            resonant[static_cast<std::size_t>(synth::parameterIndex(Model::ambre, "filter.model"))] = static_cast<float>(model);
            resonant[static_cast<std::size_t>(synth::parameterIndex(Model::ambre, "filter.mode"))] = static_cast<float>(mode);
            resonant[static_cast<std::size_t>(synth::parameterIndex(Model::ambre, "filter.resonance"))] = 1.0f;
            resonant[static_cast<std::size_t>(synth::parameterIndex(Model::ambre, "filter.drive"))] = 1.0f;
            const auto out = play(resonant, 48, 0.6, -1.0);
            auto peak = 0.0;
            for (const auto x : out)
                peak = std::max(peak, static_cast<double>(std::abs(x)));
            expect(std::isfinite(peak) && peak < 4.0 && peak > 1.0e-3, "every Ambre filter stays bounded");
        }
    // Four unison voices spread: the two sides differ.
    auto wide = values;
    wide[static_cast<std::size_t>(synth::parameterIndex(Model::ambre, "unison.voices"))] = 4.0f;
    std::vector<float> right;
    const auto left = play(wide, 57, 0.5, -1.0, &right);
    auto apart = 0.0;
    for (std::size_t i = 4'800; i < left.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(left[i] - right[i])));
    expect(apart > 0.01, "Ambre's unison spreads across the stereo");
    // Legato with glide: the pitch slides to the new note.
    auto gliding = values;
    gliding[static_cast<std::size_t>(synth::voicingGlide)] = 0.2f;
    synth::Voice voice;
    voice.prepare(rate);
    voice.start(Model::ambre, gliding, 60, 1.0f, -1.0, rate);
    std::vector<float> l(256), r(256);
    voice.render(gliding, context, l.data(), r.data(), 256, rate);
    voice.retarget(gliding, 72, 1.0f);
    voice.render(gliding, context, l.data(), r.data(), 256, rate);
    const auto halfway = voice.currentPitch();
    for (int block = 0; block < 100; ++block)
        voice.render(gliding, context, l.data(), r.data(), 256, rate);
    expect(halfway > 60.0 && halfway < 72.0 && std::abs(voice.currentPitch() - 72.0) < 1.0e-6, "Ambre glides from note to note");
}

void colosseSoundsEveryWay()
{
    using namespace cydaw::engine;
    using synth::Model;
    synth::warmUp(Model::colosse);
    expect(synth::parameterCount(Model::colosse) == 101 && synth::parameterIndex(Model::colosse, "osc1.table") == synth::voicingFieldCount
               && synth::parameterIndex(Model::colosse, "mod6.amount") == 100 && synth::modelForType("colosse-synth") == Model::colosse,
           "Colosse's settings are in their order");
    constexpr auto rate = 48'000.0;
    prism::Context context;
    const auto key = [](const char* name) { return static_cast<std::size_t>(synth::parameterIndex(Model::colosse, name)); };
    const auto play = [&](const synth::Values& values, const int note, const double seconds, const double releaseAt, std::vector<float>* right = nullptr) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::colosse, values, note, 1.0f, -1.0, rate);
        std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> other(left.size(), 0.0f);
        for (std::size_t at = 0; at < left.size(); at += 256) {
            if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
            voice.render(values, context, left.data() + at, other.data() + at, count, rate);
        }
        if (right != nullptr)
            *right = other;
        return left;
    };
    const auto peakOf = [](const std::vector<float>& samples) {
        auto peak = 0.0;
        for (const auto x : samples)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        return peak;
    };
    auto values = synth::defaultValues(Model::colosse);
    // A sine alone, the filter open: A4 at 440 Hz.
    auto pure = values;
    pure[key("osc1.position")] = 0.0f;
    pure[key("osc2.level")] = 0.0f;
    pure[key("f1.cutoff")] = 20'000.0f;
    pure[key("mod1.amount")] = 0.0f;
    const auto a4 = play(pure, 69, 1.0, -1.0);
    const auto at440 = toneLevelDb(a4, 440.0, 9'600, 48'000);
    expect(at440 > -40.0 && at440 > toneLevelDb(a4, 415.0, 9'600, 48'000) + 10.0 && at440 > toneLevelDb(a4, 880.0, 9'600, 48'000) + 20.0,
           "Colosse plays A4 at 440 Hz");
    // Released, it ends.
    const auto released = play(values, 60, 2.0, 0.4);
    auto tail = 0.0;
    for (std::size_t i = static_cast<std::size_t>(1.7 * rate); i < released.size(); ++i)
        tail = std::max(tail, static_cast<double>(std::abs(released[i])));
    expect(tail < 1.0e-4, "Colosse's note ends after its release");
    // Every table read every way, high and low: finite, sounding.
    auto allFinite = true;
    auto allSounding = true;
    for (int table = 0; table < 8; ++table)
        for (int mode = 0; mode < 8; ++mode) {
            auto bent = values;
            bent[key("osc1.table")] = static_cast<float>(table);
            bent[key("osc1.mode")] = static_cast<float>(mode);
            bent[key("osc1.warp")] = 0.7f;
            bent[key("osc1.position")] = static_cast<float>(mode) / 7.0f;
            bent[key("f1.cutoff")] = 20'000.0f;
            for (const auto note : { 28, 96 }) {
                const auto out = play(bent, note, 0.15, -1.0);
                const auto peak = peakOf(out);
                allFinite = allFinite && std::isfinite(peak) && peak < 4.0;
                allSounding = allSounding && peak > 1.0e-3;
            }
        }
    expect(allFinite && allSounding, "every Colosse table sounds, bounded, read every way");
    // Every filter at full resonance, after every drive: bounded.
    auto bounded = true;
    for (int type = 0; type < 7; ++type)
        for (int drive = 0; drive < 5; ++drive) {
            auto loud = values;
            loud[key("f1.type")] = static_cast<float>(type);
            loud[key("f1.resonance")] = 1.0f;
            loud[key("f2.type")] = static_cast<float>((type + 2) % 7);
            loud[key("f2.resonance")] = 1.0f;
            loud[key("filter.blend")] = 0.5f;
            loud[key("filter.routing")] = static_cast<float>(drive % 2);
            loud[key("drive.type")] = static_cast<float>(drive);
            loud[key("drive.amount")] = 1.0f;
            const auto out = play(loud, 45, 0.4, -1.0);
            const auto peak = peakOf(out);
            bounded = bounded && std::isfinite(peak) && peak < 4.0 && peak > 1.0e-3;
        }
    expect(bounded, "every Colosse filter and drive stays bounded");
    // Eight unison voices spread across the stereo.
    auto wide = values;
    wide[key("unison.voices")] = 8.0f;
    std::vector<float> right;
    const auto left = play(wide, 57, 0.4, -1.0, &right);
    auto apart = 0.0;
    for (std::size_t i = 4'800; i < left.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(left[i] - right[i])));
    expect(apart > 0.01, "Colosse's unison spreads across the stereo");
    // The sequence on the volume: loud steps and silent ones, at 1/16 of 120 BPM.
    auto stepped = pure;
    for (int step = 0; step < 16; ++step)
        stepped[key("perf.step1") + static_cast<std::size_t>(step)] = step % 2 == 0 ? 0.0f : -1.0f;
    stepped[key("perf.smooth")] = 0.0f;
    stepped[key("mod2.source")] = 5.0f;
    stepped[key("mod2.dest")] = 15.0f;
    stepped[key("mod2.amount")] = 1.0f;
    const auto pulsing = play(stepped, 69, 1.0, -1.0);
    const auto windowPeak = [&pulsing](const double from, const double to) {
        auto peak = 0.0;
        for (auto i = static_cast<std::size_t>(from * rate); i < static_cast<std::size_t>(to * rate); ++i)
            peak = std::max(peak, static_cast<double>(std::abs(pulsing[i])));
        return peak;
    };
    // A sixteenth: 0.125 s. Step 1 sounds, step 2 is silent.
    expect(windowPeak(0.02, 0.11) > 0.01 && windowPeak(0.15, 0.24) < windowPeak(0.02, 0.11) * 0.05,
           "Colosse's sequence steps the volume in time");
    // Its oscillators drawn as they play.
    const auto drawn = synth::picture(Model::colosse, values, "osc1", 200);
    auto drawnPeak = 0.0;
    for (const auto x : drawn)
        drawnPeak = std::max(drawnPeak, static_cast<double>(std::abs(x)));
    expect(drawn.size() == 200 && drawnPeak > 0.3 && synth::picture(Model::ambre, values, "osc1", 200).empty(),
           "Colosse draws its oscillators");
}

void mosaiqueEnginesPlayInTune()
{
    using namespace cydaw::engine;
    using synth::Model;
    synth::warmUp(Model::mosaique);
    expect(synth::parameterCount(Model::mosaique) == 141 && synth::parameterIndex(Model::mosaique, "a.type") == synth::voicingFieldCount
               && synth::parameterIndex(Model::mosaique, "mod8.amount") == 140 && synth::modelForType("mosaique-synth") == Model::mosaique,
           "Mosaïque's settings are in their order");
    constexpr auto rate = 48'000.0;
    prism::Context context;
    const auto key = [](const std::string& name) { return static_cast<std::size_t>(synth::parameterIndex(Model::mosaique, name)); };
    const auto play = [&](const synth::Values& values, const int note, const double seconds, const double releaseAt) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::mosaique, values, note, 1.0f, -1.0, rate);
        std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> right(left.size(), 0.0f);
        for (std::size_t at = 0; at < left.size(); at += 256) {
            if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
            voice.render(values, context, left.data() + at, right.data() + at, count, rate);
        }
        return left;
    };
    const auto peakOf = [](const std::vector<float>& samples, const std::size_t from = 0, std::size_t to = 0) {
        auto peak = 0.0;
        to = to == 0 ? samples.size() : std::min(to, samples.size());
        for (auto i = from; i < to; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(samples[i])));
        return peak;
    };
    // Engine A alone, each kind set to sound its note's fundamental: A4 at 440 Hz.
    auto base = synth::defaultValues(Model::mosaique);
    base[key("b.level")] = 0.0f;
    base[key("f1.cutoff")] = 20'000.0f;
    base[key("mod1.amount")] = 0.0f;
    auto inTune = true;
    for (int kind = 0; kind < 4; ++kind) {
        auto values = base;
        values[key("a.type")] = static_cast<float>(kind);
        values[key("a.wave")] = 0.0f;           // analog: a sine
        values[key("a.table")] = 0.0f;          // wavetable: Classic, its sine
        values[key("a.position")] = 0.0f;
        values[key("a.partials")] = 4.0f;       // harmonic: four partials, the first loudest
        values[key("a.bright")] = -1.0f;
        values[key("a.material")] = 3.0f;       // modal: a string
        values[key("a.tone")] = 0.2f;
        values[key("a.decay")] = 3.0f;
        const auto a4 = play(values, 69, 0.6, -1.0);
        const auto at440 = toneLevelDb(a4, 440.0, 4'800, 28'800);
        const auto ok = at440 > -50.0 && at440 > toneLevelDb(a4, 415.0, 4'800, 28'800) + 10.0 && at440 > toneLevelDb(a4, 466.0, 4'800, 28'800) + 10.0;
        if (!ok)
            std::cerr << "  Mosaique kind " << kind << " at 440 Hz: " << at440 << " dB\n";
        inTune = inTune && ok;
    }
    expect(inTune, "every Mosaïque engine plays A4 at 440 Hz");
    // Every kind on both engines, every filter, the routings: bounded and sounding.
    auto bounded = true;
    for (int kind = 0; kind < 4; ++kind)
        for (int type = 0; type < 7; ++type) {
            auto values = synth::defaultValues(Model::mosaique);
            values[key("a.type")] = static_cast<float>(kind);
            values[key("b.type")] = static_cast<float>((kind + 1 + type) % 4);
            values[key("a.exciter")] = static_cast<float>(type % 3);
            values[key("b.exciter")] = static_cast<float>((type + 1) % 3);
            values[key("a.send")] = 0.5f;
            values[key("b.send")] = 1.0f;
            values[key("f1.type")] = static_cast<float>(type);
            values[key("f1.resonance")] = 1.0f;
            values[key("f2.type")] = static_cast<float>((type + 3) % 7);
            values[key("f2.resonance")] = 1.0f;
            values[key("filter.routing")] = static_cast<float>(type % 2);
            values[key("unison.voices")] = 4.0f;
            values[key("noise.level")] = 0.3f;
            for (const auto note : { 30, 90 }) {
                const auto out = play(values, note, 0.25, -1.0);
                const auto peak = peakOf(out);
                const auto ok = std::isfinite(peak) && peak < 4.0 && peak > 1.0e-4;
                if (!ok)
                    std::cerr << "  Mosaique kind " << kind << " filter " << type << " note " << note << ": peak " << peak << "\n";
                bounded = bounded && ok;
            }
        }
    expect(bounded, "every Mosaïque engine and filter stays bounded");
    // Released, it ends; a modal ring held dies of itself.
    const auto released = play(synth::defaultValues(Model::mosaique), 60, 2.0, 0.4);
    expect(peakOf(released, static_cast<std::size_t>(1.7 * rate)) < 1.0e-4, "Mosaïque's note ends after its release");
    auto struck = base;
    struck[key("a.type")] = 3.0f;
    struck[key("a.decay")] = 0.3f;
    struck[key("amp.sustain")] = 1.0f;
    const auto ring = play(struck, 57, 2.0, -1.0);
    expect(peakOf(ring, 0, 4'800) > 0.01 && peakOf(ring, static_cast<std::size_t>(1.5 * rate)) < peakOf(ring, 0, 4'800) * 0.01,
           "a struck mode rings and dies");
    // The function on the volume: its first point silent, the next loud, one cycle a second.
    auto shaped = base;
    for (int point = 0; point < 16; ++point)
        shaped[key("fn.point" + std::to_string(point + 1))] = point % 2 == 0 ? 0.0f : 1.0f;
    shaped[key("fn.points")] = 2.0f;
    shaped[key("fn.sync")] = 0.0f;
    shaped[key("fn.rate")] = 1.0f;
    shaped[key("fn.smooth")] = 0.0f;
    shaped[key("mod7.source")] = 5.0f;
    shaped[key("mod7.dest")] = 16.0f;
    shaped[key("mod7.amount")] = -1.0f;
    const auto function = play(shaped, 69, 1.0, -1.0);
    // Value 0 then 1 over the second: the volume full then silent.
    expect(peakOf(function, 4'800, 19'200) > 0.01 && peakOf(function, 28'800, 43'200) < peakOf(function, 4'800, 19'200) * 0.05,
           "Mosaïque's drawn function moves the volume in time");
    // A macro turned reaches what it is routed to (macro 2 opens the filters).
    auto closed = base;
    closed[key("f1.cutoff")] = 100.0f;
    closed[key("mod4.amount")] = 1.0f;
    const auto dark = play(closed, 60, 0.5, -1.0);
    closed[key("macro2")] = 1.0f;
    const auto opened = play(closed, 60, 0.5, -1.0);
    expect(peakOf(opened, 4'800) > 3.0 * peakOf(dark, 4'800), "a Mosaïque macro opens the filter");
    // Its engines drawn: a shape over two periods, a mode's ring.
    auto drawnValues = synth::defaultValues(Model::mosaique);
    const auto drawnA = synth::picture(Model::mosaique, drawnValues, "a", 120);
    drawnValues[key("b.type")] = 3.0f;
    const auto drawnB = synth::picture(Model::mosaique, drawnValues, "b", 120);
    auto peakA = 0.0;
    for (const auto x : drawnA)
        peakA = std::max(peakA, static_cast<double>(std::abs(x)));
    expect(drawnA.size() == 120 && drawnB.size() == 120 && peakA > 0.3 && std::abs(drawnB[1]) > std::abs(drawnB[119]),
           "Mosaïque draws its engines");
}

// The models added after Nébuleuse, each the same way: found by its names,
// its defaults within its ranges, A4 at 440 Hz, a released note that ends,
// every setting at its top still bounded.
void laterSynthModelsPlay()
{
    using namespace cydaw::engine;
    using synth::Model;
    constexpr auto rate = 48'000.0;
    const prism::Context context;
    for (const auto model : { Model::opale, Model::onyx, Model::tirette, Model::alliage, Model::quartz, Model::argile, Model::saphir,
                              Model::derive, Model::bronze, Model::jade }) {
        synth::warmUp(model);
        const auto& info = synth::info(model);
        const auto name = std::string(info.prefix);
        expect(synth::modelForType(info.typeKey) == model && synth::modelForPrefix(info.prefix) == model
                   && synth::parameterCount(model) > synth::voicingFieldCount
                   && synth::parameterIndex(model, "voice.master") == synth::voicingMaster,
               (name + " is found by its type and prefix, the voicing first").c_str());
        const auto defaults = synth::defaultValues(model);
        auto inRange = true;
        for (int index = 0; index < synth::parameterCount(model); ++index)
            inRange = inRange && synth::clampValue(model, index, defaults[static_cast<std::size_t>(index)]) == defaults[static_cast<std::size_t>(index)];
        expect(inRange, (name + "'s defaults are within its ranges").c_str());
        const auto play = [&](const synth::Values& values, const int note, const double seconds, const double releaseAt) {
            synth::Voice voice;
            voice.prepare(rate);
            voice.start(model, values, note, 1.0f, -1.0, rate);
            std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
            std::vector<float> right(left.size(), 0.0f);
            for (std::size_t at = 0; at < left.size(); at += 256) {
                if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                    voice.release();
                const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
                voice.render(values, context, left.data() + at, right.data() + at, count, rate);
            }
            return left;
        };
        const auto a4 = play(defaults, 69, 1.0, -1.0);
        const auto at440 = toneLevelDb(a4, 440.0, 9'600, 48'000);
        expect(at440 > -50.0 && at440 > toneLevelDb(a4, 415.0, 9'600, 48'000) + 6.0 && at440 > toneLevelDb(a4, 466.0, 9'600, 48'000) + 6.0,
               (name + " plays A4 at 440 Hz").c_str());
        const auto released = play(defaults, 60, 10.0, 1.0);
        auto tail = 0.0;
        for (std::size_t i = static_cast<std::size_t>(9.5 * rate); i < released.size(); ++i)
            tail = std::max(tail, static_cast<double>(std::abs(released[i])));
        expect(tail < 1.0e-3, (name + "'s note ends after its release").c_str());
        // Every setting at its top (and every one at its bottom): finite, bounded.
        for (const auto top : { true, false }) {
            auto extreme = defaults;
            for (int index = 0; index < synth::parameterCount(model); ++index) {
                const auto& parameter = synth::parameter(model, index);
                if (index == synth::voicingMaster)
                    continue;
                extreme[static_cast<std::size_t>(index)] = synth::clampValue(model, index, top ? parameter.maximum : parameter.minimum);
            }
            auto finite = true;
            auto peak = 0.0;
            for (const auto note : { 24, 60, 108 })
                for (const auto sample : play(extreme, note, 0.5, 0.3)) {
                    finite = finite && std::isfinite(sample);
                    peak = std::max(peak, static_cast<double>(std::abs(sample)));
                }
            expect(finite && peak < 16.0, (name + (top ? " at every top" : " at every bottom") + " stays finite and bounded").c_str());
        }
    }
}

// Jade: its table in order, found by its names; each mechanism plays A4 at
// 440 Hz; a harder blow is louder and brighter; the damper stops a released
// note; a held one sustains then dies away; the auto-pan moves the sides
// apart; every mechanism with every setting at an end stays finite and
// bounded.
void jadePlaysInTune()
{
    using namespace cydaw::engine;
    using synth::Model;
    const auto key = [](const char* name) { return static_cast<std::size_t>(synth::parameterIndex(Model::jade, name)); };
    expect(synth::parameterCount(Model::jade) == 39 && synth::parameterIndex(Model::jade, "model") == synth::voicingFieldCount
               && synth::parameterIndex(Model::jade, "chorus.mix") == 38 && synth::modelForType("jade-synth") == Model::jade
               && synth::modelForPrefix("jade") == Model::jade && synth::modelForProcessor("core.jade-synth") == Model::jade,
           "Jade's settings are in their order, found by its names");
    constexpr auto rate = 48'000.0;
    const prism::Context context;
    const auto play = [&](const synth::Values& values, const int note, const float velocity, const double seconds, const double releaseAt,
                          std::vector<float>* right = nullptr) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::jade, values, note, velocity, -1.0, rate);
        std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> other(left.size(), 0.0f);
        for (std::size_t at = 0; at < left.size(); at += 256) {
            if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
            voice.render(values, context, left.data() + at, other.data() + at, count, rate);
        }
        if (right != nullptr)
            *right = other;
        return left;
    };
    const auto peakOf = [](const std::vector<float>& samples, const double from, const double to) {
        auto peak = 0.0;
        const auto last = std::min(samples.size(), static_cast<std::size_t>(to * 48'000.0));
        for (auto i = static_cast<std::size_t>(from * 48'000.0); i < last; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(samples[i])));
        return peak;
    };
    // How bright: the slope's level against the level, over the attack.
    const auto brightness = [](const std::vector<float>& samples) {
        auto level = 1.0e-12;
        auto slope = 0.0;
        for (std::size_t i = 1; i < std::min<std::size_t>(samples.size(), 7'200); ++i) {
            level += static_cast<double>(samples[i]) * samples[i];
            const auto change = static_cast<double>(samples[i]) - samples[i - 1];
            slope += change * change;
        }
        return std::sqrt(slope / level);
    };
    const auto defaults = synth::defaultValues(Model::jade);
    for (int mechanism = 0; mechanism < 3; ++mechanism) {
        auto values = defaults;
        values[key("model")] = static_cast<float>(mechanism);
        values[key("tune.detune")] = 0.0f;
        // In tune: the strongest frequency near A4 within a few cents of 440 Hz.
        const auto a4 = play(values, 69, 0.8f, 1.0, -1.0);
        auto best = 0.0;
        auto bestDb = -400.0;
        for (auto hz = 430.0; hz <= 450.0; hz += 0.1)
            if (const auto db = toneLevelDb(a4, hz, 9'600, 48'000); db > bestDb) {
                bestDb = db;
                best = hz;
            }
        expect(bestDb > -40.0 && std::abs(best - 440.0) < 1.0 && bestDb > toneLevelDb(a4, 415.0, 9'600, 48'000) + 12.0,
               "every Jade mechanism plays A4 at 440 Hz");
        // Struck harder, louder and brighter (the blow shorter, the pickup's curve reached further).
        const auto soft = play(values, 60, 0.25f, 0.2, -1.0);
        const auto hard = play(values, 60, 1.0f, 0.2, -1.0);
        expect(brightness(hard) > brightness(soft) * 1.3 && peakOf(hard, 0.0, 0.2) > peakOf(soft, 0.0, 0.2) * 1.5,
               "a harder blow makes Jade louder and brighter");
        // Let go of, the damper stops it; held, it sustains and dies away by itself.
        const auto released = play(values, 60, 0.8f, 1.2, 0.4);
        expect(peakOf(released, 0.9, 1.2) < 1.0e-3 * peakOf(released, 0.0, 0.4), "Jade's damper stops a released note");
        const auto held = play(values, 60, 0.8f, 4.0, -1.0);
        expect(peakOf(held, 3.5, 4.0) < 0.3 * peakOf(held, 0.0, 0.5) && peakOf(held, 3.5, 4.0) > 1.0e-5,
               "a held Jade note sustains, then dies away");
        // One voice near -1..1 at its master.
        expect(peakOf(hard, 0.0, 0.2) > 0.1 && peakOf(hard, 0.0, 0.2) < 1.5, "a Jade voice plays near full scale");
        // Every setting at an end (each mechanism kept): finite, bounded.
        for (const auto top : { true, false }) {
            auto extreme = values;
            for (int index = 0; index < synth::parameterCount(Model::jade); ++index) {
                const auto& parameter = synth::parameter(Model::jade, index);
                if (index == synth::voicingMaster || parameter.key == "model")
                    continue;
                extreme[static_cast<std::size_t>(index)] = synth::clampValue(Model::jade, index, top ? parameter.maximum : parameter.minimum);
            }
            auto finite = true;
            auto peak = 0.0;
            for (const auto note : { 21, 60, 108, 127 })
                for (const auto velocity : { 0.05f, 1.0f })
                    for (const auto sample : play(extreme, note, velocity, 0.4, 0.25)) {
                        finite = finite && std::isfinite(sample);
                        peak = std::max(peak, static_cast<double>(std::abs(sample)));
                    }
            expect(finite && peak < 8.0, "every Jade mechanism stays finite and bounded at its ends");
        }
    }
    // The auto-pan: the tremolo swings the note from one side to the other.
    auto panned = defaults;
    panned[key("tremolo.depth")] = 1.0f;
    panned[key("tremolo.stereo")] = 1.0f;
    panned[key("tremolo.rate")] = 4.0f;
    std::vector<float> right;
    const auto left = play(panned, 60, 0.8f, 1.0, -1.0, &right);
    auto apart = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
        apart = std::max(apart, static_cast<double>(std::abs(left[i] - right[i])));
    expect(apart > 0.2 * peakOf(left, 0.0, 1.0), "Jade's auto-pan moves the note across");
    // Rung out while still held, then let go of: the voice ends.
    {
        auto brief = defaults;
        brief[key("tine.decay")] = 0.3f;
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::jade, brief, 96, 0.5f, -1.0, rate);
        std::vector<float> scratchLeft(256);
        std::vector<float> scratchRight(256);
        for (int block = 0; block < static_cast<int>(3.0 * rate / 256.0); ++block)
            voice.render(brief, context, scratchLeft.data(), scratchRight.data(), 256, rate);
        const auto stillHeld = voice.active() && !voice.releasing();
        voice.release();
        voice.render(brief, context, scratchLeft.data(), scratchRight.data(), 256, rate);
        expect(stillHeld && !voice.active(), "a Jade note rung out ends once let go of");
    }
    // Its pictures: the pickup's wave and its curve; nothing for another part.
    expect(synth::picture(Model::jade, defaults, "pickup", 64).size() == 64 && synth::picture(Model::jade, defaults, "curve", 64).size() == 64
               && synth::picture(Model::jade, defaults, "osc1", 64).empty(),
           "Jade draws its pickup");
}


void nebuleuseLayersSound()
{
    using namespace cydaw::engine;
    using synth::Model;
    synth::warmUp(Model::nebuleuse);
    expect(synth::parameterCount(Model::nebuleuse) == 98 && synth::parameterIndex(Model::nebuleuse, "a.source") == synth::voicingFieldCount
               && synth::parameterIndex(Model::nebuleuse, "mod8.amount") == 97 && synth::modelForType("nebuleuse-synth") == Model::nebuleuse,
           "Nébuleuse's settings are in their order");
    constexpr auto rate = 48'000.0;
    prism::Context context;
    const auto key = [](const std::string& name) { return static_cast<std::size_t>(synth::parameterIndex(Model::nebuleuse, name)); };
    const auto play = [&](const synth::Values& values, const int note, const double seconds, const double releaseAt) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::nebuleuse, values, note, 1.0f, -1.0, rate);
        std::vector<float> left(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> right(left.size(), 0.0f);
        for (std::size_t at = 0; at < left.size(); at += 256) {
            if (releaseAt >= 0.0 && static_cast<double>(at) >= releaseAt * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, left.size() - at));
            voice.render(values, context, left.data() + at, right.data() + at, count, rate);
        }
        return left;
    };
    const auto peakOf = [](const std::vector<float>& samples, const std::size_t from = 0, std::size_t to = 0) {
        auto peak = 0.0;
        to = to == 0 ? samples.size() : std::min(to, samples.size());
        for (auto i = from; i < to; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(samples[i])));
        return peak;
    };
    // Still: no orb moving, no filter closing, layer B silent, a quick attack.
    auto still = synth::defaultValues(Model::nebuleuse);
    still[key("orb.motion")] = 0.0f;
    still[key("b.level")] = 0.0f;
    still[key("f.cutoff")] = 20'000.0f;
    still[key("amp.attack")] = 0.005f;
    for (int slot = 1; slot <= 8; ++slot)
        still[key("mod" + std::to_string(slot) + ".amount")] = 0.0f;
    // A synthesis layer on a sine: A4 at 440 Hz; with the fifth, E5 too.
    auto sine = still;
    sine[key("a.source")] = 0.0f;
    sine[key("a.position")] = 0.0f;
    sine[key("a.drift")] = 0.0f;
    const auto a4 = play(sine, 69, 1.0, -1.0);
    const auto at440 = toneLevelDb(a4, 440.0, 9'600, 48'000);
    expect(at440 > -45.0 && at440 > toneLevelDb(a4, 415.0, 9'600, 48'000) + 10.0 && toneLevelDb(a4, 659.3, 9'600, 48'000) < at440 - 30.0,
           "a Nébuleuse synthesis layer plays A4 at 440 Hz");
    sine[key("a.harmonia")] = 2.0f;
    sine[key("a.harmoniaLevel")] = 1.0f;
    const auto fifth = play(sine, 69, 1.0, -1.0);
    expect(toneLevelDb(fifth, 659.3, 9'600, 48'000) > toneLevelDb(fifth, 440.0, 9'600, 48'000) - 6.0, "its harmonia adds the fifth");
    // Every texture as grains: bounded, sounding; the pitched ones at the note.
    auto bounded = true;
    for (int texture = 0; texture < 7; ++texture) {
        auto grains = still;
        grains[key("a.texture")] = static_cast<float>(texture);
        grains[key("a.scatter")] = 0.0f;
        grains[key("a.spray")] = 0.1f;
        for (const auto note : { 36, 84 }) {
            const auto out = play(grains, note, 0.8, -1.0);
            const auto peak = peakOf(out, 4'800);
            const auto ok = std::isfinite(peak) && peak < 4.0 && peak > 1.0e-3;
            if (!ok)
                std::cerr << "  Nebuleuse texture " << texture << " note " << note << ": peak " << peak << "\n";
            bounded = bounded && ok;
        }
    }
    expect(bounded, "every Nébuleuse texture sounds as grains, bounded");
    auto pitched = true;
    for (const auto texture : { 0, 3, 6 }) {
        auto grains = still;
        grains[key("a.texture")] = static_cast<float>(texture);
        grains[key("a.scatter")] = 0.0f;
        grains[key("a.grain")] = 0.2f;
        const auto out = play(grains, 69, 1.2, -1.0);
        const auto at = toneLevelDb(out, 440.0, 9'600, 57'600);
        const auto ok = at > toneLevelDb(out, 392.0, 9'600, 57'600) + 6.0 && at > toneLevelDb(out, 494.0, 9'600, 57'600) + 6.0;
        if (!ok)
            std::cerr << "  Nebuleuse texture " << texture << " at 440 Hz: " << at << " dB\n";
        pitched = pitched && ok;
    }
    expect(pitched, "a pitched texture's grains play the note");
    // Released, it ends.
    auto quick = still;
    quick[key("amp.release")] = 0.3f;
    const auto released = play(quick, 60, 2.0, 0.5);
    expect(peakOf(released, static_cast<std::size_t>(1.6 * rate)) < 1.0e-4, "Nébuleuse's note ends after its release");
    // The orb moves what it is routed to: up, the filter opens.
    auto orb = still;
    orb[key("f.cutoff")] = 150.0f;
    orb[key("mod2.amount")] = 1.0f;
    orb[key("orb.y")] = -1.0f;
    const auto low = play(orb, 60, 0.6, -1.0);
    orb[key("orb.y")] = 1.0f;
    const auto high = play(orb, 60, 0.6, -1.0);
    expect(peakOf(high, 9'600) > 3.0 * peakOf(low, 9'600), "Nébuleuse's orb opens the filter");
    // Its layers drawn: a wave, a texture's outline.
    auto drawnValues = synth::defaultValues(Model::nebuleuse);
    const auto texture = synth::picture(Model::nebuleuse, drawnValues, "a", 100);
    const auto wave = synth::picture(Model::nebuleuse, drawnValues, "b", 100);
    auto texturePeak = 0.0;
    auto negative = false;
    for (const auto x : texture) {
        texturePeak = std::max(texturePeak, static_cast<double>(x));
        negative = negative || x < 0.0f;
    }
    auto waveLow = 0.0;
    for (const auto x : wave)
        waveLow = std::min(waveLow, static_cast<double>(x));
    expect(texture.size() == 100 && texturePeak > 0.3 && !negative && waveLow < -0.3, "Nébuleuse draws its layers");
}

void prismGoesFurther()
{
    namespace prism = cydaw::engine::prism;
    constexpr double rate = 48'000.0;
    const auto render = [](const prism::PrismValues& values, const int note, const int frames, std::vector<float>& left,
                           std::vector<float>& right) {
        prism::Voice voice;
        voice.prepare(rate);
        const auto tables = prism::resolveTables(values);
        voice.start(values, note, 1.0f, -1.0, rate);
        left.assign(static_cast<std::size_t>(frames), 0.0f);
        right.assign(static_cast<std::size_t>(frames), 0.0f);
        prism::Context context;
        for (auto done = 0; done < frames; done += 256) {
            const auto count = std::min(256, frames - done);
            voice.render(values, *tables, context, left.data() + done, right.data() + done, count, rate);
            context.clockSeconds += count / rate;
        }
    };
    const auto at = [](const std::string& key) { return static_cast<std::size_t>(prism::parameterIndex(key)); };
    const auto rms = [](const std::vector<float>& samples) {
        auto sum = 0.0;
        for (auto i = samples.size() / 5; i < samples.size(); ++i)
            sum += static_cast<double>(samples[i]) * samples[i];
        return std::sqrt(sum / static_cast<double>(samples.size() - samples.size() / 5));
    };
    // How bright: the change from sample to sample against the whole.
    const auto brightness = [&rms](const std::vector<float>& samples) {
        std::vector<float> steps(samples.size(), 0.0f);
        for (std::size_t i = 1; i < samples.size(); ++i)
            steps[i] = samples[i] - samples[i - 1];
        return rms(steps) / std::max(1.0e-9, rms(samples));
    };

    // The layout: C and the rest after everything, the old slots' targets kept.
    expect(prism::parameterIndex("c.position") == prism::oscCBase + prism::oscPosition
               && prism::parameterIndex("a.spectral") == prism::spectralBase(0) + prism::spectralMode
               && prism::parameterIndex("lfo4.stereo") == prism::parameterCount - 1
               && prism::parameterIndex("m16.amount") == prism::drawBase(0) - 1,
           "prism: oscillator C, the spectral morphs and the stereo offsets come after everything else");
    const auto& targets = prism::destinations();
    // The LFOs' rates close the old list (the last of them, listed twice).
    const auto fromEnd = std::find(targets.rbegin(), targets.rend(), prism::lfoBase(prism::lfoCount - 1) + prism::lfoRate);
    const auto lastRate = fromEnd == targets.rend() ? targets.end() : std::prev(fromEnd.base());
    expect(lastRate != targets.end() && lastRate + 1 != targets.end() && *(lastRate + 1) >= prism::oscCBase
               && std::find(targets.begin(), targets.end(), prism::spectralBase(2) + prism::spectralAmount) != targets.end(),
           "prism: the new targets follow the old ones");

    // A saw on A alone, the filter open.
    auto saw = prism::defaultValues();
    saw[at("a.table")] = 0.0f;
    saw[at("a.position")] = 0.667f;
    saw[at("a.unison")] = 1.0f;
    saw[at("filter.on")] = 0.0f;
    std::vector<float> left;
    std::vector<float> right;
    render(saw, 69, 24'000, left, right);
    const auto plainBrightness = brightness(left);
    const auto plainSecond = toneLevelDb(left, 880.0, 4'800, 24'000);
    const auto plainFirst = toneLevelDb(left, 440.0, 4'800, 24'000);

    // C alone, a sine: A4 at 440 Hz.
    auto sine = saw;
    sine[at("a.on")] = 0.0f;
    sine[at("c.on")] = 1.0f;
    sine[at("c.table")] = 0.0f;
    sine[at("c.position")] = 0.0f;
    sine[at("c.unison")] = 1.0f;
    render(sine, 69, 24'000, left, right);
    const auto c440 = toneLevelDb(left, 440.0, 4'800, 24'000);
    expect(c440 > -40.0 && c440 > toneLevelDb(left, 880.0, 4'800, 24'000) + 30.0, "prism: oscillator C plays A4 at 440 Hz");
    // C's FM reads A.
    auto fm = sine;
    fm[at("a.on")] = 1.0f;
    fm[at("a.level")] = 0.0f;
    fm[at("c.warp")] = 11.0f;
    fm[at("c.warpAmount")] = 0.8f;
    std::vector<float> fmLeft;
    render(fm, 69, 24'000, fmLeft, right);
    expect(brightness(fmLeft) > brightness(left) * 1.5, "prism: oscillator C's FM reads oscillator A");

    // The spectral morphs: a low pass darkens, a high pass brightens, odd takes the even harmonics away.
    auto darker = saw;
    darker[at("a.spectral")] = 1.0f;
    darker[at("a.spectralAmount")] = 0.6f;
    render(darker, 69, 24'000, left, right);
    expect(brightness(left) < plainBrightness * 0.5, "prism: the spectral low pass takes the highs away");
    auto brighter = saw;
    brighter[at("a.spectral")] = 2.0f;
    brighter[at("a.spectralAmount")] = 0.4f;
    render(brighter, 69, 24'000, left, right);
    expect(brightness(left) > plainBrightness * 1.3, "prism: the spectral high pass keeps the highs");
    auto odd = saw;
    odd[at("a.spectral")] = 3.0f;
    odd[at("a.spectralAmount")] = 1.0f;
    render(odd, 69, 24'000, left, right);
    expect(plainSecond > plainFirst - 12.0 && toneLevelDb(left, 880.0, 4'800, 24'000) < toneLevelDb(left, 440.0, 4'800, 24'000) - 30.0,
           "prism: the spectral odd morph takes the even harmonics away");
    auto bounded = true;
    for (int mode = 1; mode < static_cast<int>(prism::SpectralMode::count); ++mode)
        for (const auto amount : { 0.0f, 0.5f, 1.0f })
            for (const auto note : { 30, 100 }) {
                auto morphed = saw;
                morphed[at("a.spectral")] = static_cast<float>(mode);
                morphed[at("a.spectralAmount")] = amount;
                render(morphed, note, 9'600, left, right);
                auto peak = 0.0;
                for (const auto x : left)
                    peak = std::max(peak, static_cast<double>(std::abs(x)));
                bounded = bounded && std::isfinite(peak) && peak < 4.0 && (peak > 1.0e-3 || (mode == 2 && amount == 0.0f));
            }
    expect(bounded, "prism: every spectral morph stays bounded");

    // A stereo LFO on the filter: the sides apart; without it, together.
    auto swept = saw;
    swept[at("filter.on")] = 1.0f;
    swept[at("filter.cutoff")] = 800.0f;
    swept[at("lfo1.sync")] = 0.0f;
    swept[at("lfo1.rate")] = 3.0f;
    swept[at("lfo1.shape")] = 0.0f;
    swept[at("m1.source")] = static_cast<float>(prism::Source::lfo1);
    const auto& list = prism::destinations();
    const auto cutoffTarget = std::find(list.begin(), list.end(), prism::filterBase + prism::filterCutoff);
    swept[at("m1.destination")] = static_cast<float>(cutoffTarget - list.begin());
    swept[at("m1.amount")] = 0.5f;
    render(swept, 57, 48'000, left, right);
    std::vector<float> apart(left.size());
    for (std::size_t i = 0; i < left.size(); ++i)
        apart[i] = left[i] - right[i];
    const auto together = rms(apart);
    swept[at("lfo1.stereo")] = 1.0f;
    render(swept, 57, 48'000, left, right);
    for (std::size_t i = 0; i < left.size(); ++i)
        apart[i] = left[i] - right[i];
    expect(together < 1.0e-6 && rms(apart) > 0.05 * rms(left), "prism: a stereo LFO moves the filter differently on each side");
}

void batterieMixesItsPieces()
{
    namespace drum = cydaw::engine::drum;
    using drum::Piece;
    expect(drum::pieceForNote(36) == Piece::kick && drum::pieceForNote(38) == Piece::snare && drum::pieceForNote(42) == Piece::hihat
               && drum::pieceForNote(46) == Piece::hihat && drum::pieceForNote(50) == Piece::tom1 && drum::pieceForNote(41) == Piece::tom4
               && drum::pieceForNote(49) == Piece::crash && drum::pieceForNote(51) == Piece::ride && drum::pieceForNote(52) == Piece::china
               && drum::pieceForNote(64) == Piece::percussion && drum::pieceForNote(24) == Piece::other,
           "batterie: each key is a piece of the kit");
    expect(drum::parameterIndex("snare.tune") == drum::index(Piece::snare, drum::fieldTune)
               && drum::parameterIndex("kit.level") == drum::globalBase + drum::globalLevel
               && static_cast<int>(drum::parameters().size()) == drum::valueCount,
           "batterie: its settings by their keys");

    // A sampler map whose every key plays A3 (a sine), a drum kit's way: no key tracking.
    cydaw::engine::SamplerZone zone;
    zone.sample = sineSample(220.0, 2.0, 0.5, 0.5);
    zone.rootNote = 57;
    auto shared = std::make_shared<cydaw::engine::SamplerMap>();
    shared->zones = { zone };
    cydaw::engine::PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.waveform = cydaw::engine::PolyOscillatorWaveform::sampler;
    plan.sampler = shared;
    plan.samplerValues = cydaw::engine::defaultSamplerValues();
    plan.samplerValues[static_cast<std::size_t>(cydaw::engine::SamplerSetting::velocityDb)] = 0.0f;
    plan.samplerValues[static_cast<std::size_t>(cydaw::engine::SamplerSetting::keyTrack)] = 0.0f;
    plan.oscillatorLevel = 1.0f;
    plan.attackSeconds = 0.001f;
    plan.sustainLevel = 1.0f;
    plan.filterCutoffHz = 20'000.0f;
    plan.drumKit = true;
    plan.drumValues = drum::defaultValues();
    struct Heard {
        bool started {};
        double left {};
        double right {};
        double late {};
        int lag {};
        int shortLag {};
    };
    const auto hit = [&plan](const int note) {
        cydaw::engine::PolyVoice voice;
        voice.prepare(48'000.0);
        Heard heard;
        heard.started = voice.startNote(plan, note, 1.0f, 48'000.0);
        std::vector<float> left(48'000, 0.0f);
        std::vector<float> right(48'000, 0.0f);
        float* channels[] = { left.data(), right.data() };
        if (heard.started)
            for (int at = 0; at < 48'000; at += 480) {
                float* block[] = { left.data() + at, right.data() + at };
                if (!voice.render(plan, block, 2, 0, 480, 48'000.0))
                    break;
            }
        (void) channels;
        auto energyLeft = 0.0;
        auto energyRight = 0.0;
        auto energyLate = 0.0;
        for (std::size_t index = 2'400; index < 7'200; ++index) {
            energyLeft += static_cast<double>(left[index]) * left[index];
            energyRight += static_cast<double>(right[index]) * right[index];
        }
        for (std::size_t index = 36'000; index < 48'000; ++index)
            energyLate += static_cast<double>(left[index]) * left[index];
        heard.left = std::sqrt(energyLeft / 4'800.0);
        heard.right = std::sqrt(energyRight / 4'800.0);
        heard.late = std::sqrt(energyLate / 12'000.0);
        // A period: around 218 samples at A3, 109 an octave up.
        const std::vector<double> held(left.begin(), left.end());
        heard.lag = strongestLag(held, 160, 300);
        heard.shortLag = strongestLag(held, 80, 150);
        return heard;
    };
    const auto natural = hit(38);
    expect(natural.started && natural.left > 0.1 && std::abs(natural.lag - 218) <= 2, "batterie: a snare plays as recorded");
    auto mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::snare, drum::fieldLevel))] = -6.0f;
    plan.drumValues = mixed;
    const auto lower = hit(38);
    expect(std::abs(20.0 * std::log10(lower.left / natural.left) + 6.0) < 0.2, "batterie: a piece's level");
    mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::snare, drum::fieldPan))] = 1.0f;
    plan.drumValues = mixed;
    const auto right = hit(38);
    expect(right.left < 0.01 * natural.left && right.right > 0.9 * natural.right, "batterie: a piece's pan");
    mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::snare, drum::fieldTune))] = 12.0f;
    plan.drumValues = mixed;
    const auto higher = hit(38);
    expect(std::abs(higher.shortLag - 109) <= 2 && std::abs(higher.lag - 218) <= 2, "batterie: a piece's tuning");
    mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::snare, drum::fieldDecay))] = 0.3f;
    plan.drumValues = mixed;
    const auto shorter = hit(38);
    expect(natural.late > 0.1 && shorter.late < 1.0e-4 && shorter.left > 0.01, "batterie: a piece rings as long as it is told");
    mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::snare, drum::fieldMute))] = 1.0f;
    plan.drumValues = mixed;
    expect(!hit(38).started && hit(36).started, "batterie: a muted piece is silent, the others play");
    mixed = drum::defaultValues();
    mixed[static_cast<std::size_t>(drum::index(Piece::kick, drum::fieldSolo))] = 1.0f;
    plan.drumValues = mixed;
    expect(!hit(38).started && hit(36).started, "batterie: a soloed piece plays alone");
}

void roomTakesItsSize()
{
    using namespace cydaw::engine;
    // An impulse through a far microphone in its room: a larger room's
    // reflections come later.
    std::vector<float> impulse(9'600, 0.0f);
    impulse[0] = 1.0f;
    const auto tail = [&](const float size) {
        auto mic = studioEffect(NativeDspNodeKind::micSim, { { "model", 4.0f }, { "distance", 100.0f }, { "room", 1.0f }, { "room-size", size } });
        const auto out = runStereo(*mic, impulse);
        auto energy = 0.0;
        for (std::size_t i = 2'400; i < out.size(); ++i) // past 50 ms
            energy += static_cast<double>(out[i]) * out[i];
        return energy;
    };
    expect(tail(1.0f) > tail(0.0f) * 10.0, "a larger room answers later");
    auto cabinet = studioEffect(NativeDspNodeKind::cabSim, { { "room", 1.0f }, { "room-size", 1.0f }, { "room-tone", 1'500.0f } });
    const auto out = runStereo(*cabinet, sine(440.0, 0.3, 0.3));
    auto peak = 0.0;
    for (const auto x : out)
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    expect(std::isfinite(peak) && peak > 1.0e-3 && peak < 3.0, "the cabinet's large dark room plays, bounded");
}

} // namespace


namespace {
using namespace cydaw::engine;

// ---- Time stretch ----------------------------------------------------------------------------

// A sound held in memory, read as a file is: silence around it.
struct VectorSource final : StretchSource {
    std::vector<std::vector<float>> channels;
    void read(const std::int64_t start, const int count, float* const* out, const int channelCount) override
    {
        for (int channel = 0; channel < channelCount; ++channel) {
            const auto& from = channels[static_cast<std::size_t>(std::min<int>(channel, static_cast<int>(channels.size()) - 1))];
            for (int i = 0; i < count; ++i) {
                const auto at = start + i;
                out[channel][i] = at >= 0 && at < static_cast<std::int64_t>(from.size()) ? from[static_cast<std::size_t>(at)] : 0.0f;
            }
        }
    }
};

VectorSource toneSource(const double hz, const double seconds, const double rate = 48'000.0, const int channels = 2)
{
    VectorSource source;
    const auto count = static_cast<std::size_t>(seconds * rate);
    source.channels.assign(static_cast<std::size_t>(channels), std::vector<float>(count));
    for (std::size_t i = 0; i < count; ++i)
        for (int channel = 0; channel < channels; ++channel)
            source.channels[static_cast<std::size_t>(channel)][i]
                = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate + channel * 0.7));
    return source;
}

// The stretcher's output, `count` frames from `from`, in blocks of 512 as a
// player asks.
std::vector<std::vector<float>> stretched(TimeStretcher& stretcher, StretchSource& source, const std::int64_t from, const int count)
{
    const auto channels = stretcher.settings().channels;
    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(count)));
    for (auto done = 0; done < count; done += 512) {
        const auto block = std::min(512, count - done);
        std::array<float*, 8> pointers {};
        for (int channel = 0; channel < channels; ++channel)
            pointers[static_cast<std::size_t>(channel)] = out[static_cast<std::size_t>(channel)].data() + done;
        stretcher.render(source, from + done, block, pointers.data());
    }
    return out;
}

// The strongest frequency of a signal, to a tenth of a hertz (a scan of
// Goertzel powers around a guess, refined).
double strongestFrequency(const std::vector<float>& signal, const double rate, const double low, const double high)
{
    const auto power = [&](const double hz) {
        const auto coefficient = 2.0 * std::cos(2.0 * std::numbers::pi * hz / rate);
        auto previous = 0.0;
        auto before = 0.0;
        for (const auto sample : signal) {
            const auto now = sample + coefficient * previous - before;
            before = previous;
            previous = now;
        }
        return previous * previous + before * before - coefficient * previous * before;
    };
    auto best = low;
    auto bestPower = -1.0;
    for (auto hz = low; hz <= high; hz += 1.0)
        if (const auto value = power(hz); value > bestPower) {
            bestPower = value;
            best = hz;
        }
    for (auto hz = best - 1.0; hz <= best + 1.0; hz += 0.1)
        if (const auto value = power(hz); value > bestPower) {
            bestPower = value;
            best = hz;
        }
    return best;
}

double rmsOf(const std::vector<float>& signal, const std::size_t from, const std::size_t to)
{
    auto sum = 0.0;
    for (auto i = from; i < to && i < signal.size(); ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, std::min(to, signal.size()) - from)));
}

void timeStretchKeepsPitchAndLength()
{
    auto source = toneSource(440.0, 4.0);
    for (const auto algorithm : { StretchAlgorithm::tonal, StretchAlgorithm::beats }) {
        for (const auto speed : { 0.5, 1.5 }) {
            TimeStretcher stretcher;
            stretcher.prepare({ algorithm, 48'000.0, 48'000.0, 4'800.0, speed, 1.0, 2 });
            const auto out = stretched(stretcher, source, 0, 48'000);
            std::vector<float> middle(out[0].begin() + 8'000, out[0].begin() + 40'000);
            const auto hz = strongestFrequency(middle, 48'000.0, 380.0, 500.0);
            if (std::abs(hz - 440.0) >= 1.5)
                std::cout << "stretch " << static_cast<int>(algorithm) << " at " << speed << ": " << hz << " Hz\n";
            expect(std::abs(hz - 440.0) < 1.5, "a stretched tone keeps its pitch");
            // Its level holds (0.5 peak: -9 dB RMS).
            const auto level = 20.0 * std::log10(rmsOf(out[0], 8'000, 40'000));
            expect(std::abs(level - 20.0 * std::log10(0.5 / std::sqrt(2.0))) < 1.5, "a stretched tone keeps its level");
        }
    }
    // Transposed an octave, the length kept.
    {
        TimeStretcher stretcher;
        stretcher.prepare({ StretchAlgorithm::tonal, 48'000.0, 48'000.0, 0.0, 1.0, 2.0, 2 });
        const auto out = stretched(stretcher, source, 0, 48'000);
        std::vector<float> middle(out[0].begin() + 8'000, out[0].begin() + 40'000);
        expect(std::abs(strongestFrequency(middle, 48'000.0, 800.0, 960.0) - 880.0) < 2.0, "a transposed tone is an octave up");
        expect(rmsOf(out[0], 8'000, 40'000) > 0.25, "a transposed tone keeps its level");
    }
    // Repitch: twice as fast, an octave up.
    {
        TimeStretcher stretcher;
        stretcher.prepare({ StretchAlgorithm::repitch, 48'000.0, 48'000.0, 0.0, 2.0, 1.0, 2 });
        const auto out = stretched(stretcher, source, 0, 24'000);
        std::vector<float> middle(out[0].begin() + 2'000, out[0].begin() + 22'000);
        expect(std::abs(strongestFrequency(middle, 48'000.0, 800.0, 960.0) - 880.0) < 2.0, "repitch follows the speed");
    }
    // A file at 44.1 kHz played at 48 kHz: its pitch, read through the sinc.
    {
        auto slow = toneSource(1'000.0, 2.0, 44'100.0);
        TimeStretcher stretcher;
        stretcher.prepare({ StretchAlgorithm::repitch, 44'100.0, 48'000.0, 0.0, 1.0, 1.0, 2 });
        const auto out = stretched(stretcher, slow, 0, 48'000);
        std::vector<float> middle(out[0].begin() + 4'000, out[0].begin() + 44'000);
        expect(std::abs(strongestFrequency(middle, 48'000.0, 950.0, 1'050.0) - 1'000.0) < 1.0, "another rate keeps the pitch");
    }
    // Unstretched at the same rate: the file itself, sample for sample.
    {
        TimeStretcher stretcher;
        stretcher.prepare({ StretchAlgorithm::tonal, 48'000.0, 48'000.0, 1'000.0, 1.0, 1.0, 2 });
        const auto out = stretched(stretcher, source, 0, 4'096);
        auto same = true;
        for (std::size_t i = 0; i < 4'096; ++i)
            same = same && out[1][i] == source.channels[1][i + 1'000];
        expect(same, "an unstretched clip is the file as it is");
    }
    // A jump (a seek) plays on at once.
    {
        TimeStretcher stretcher;
        stretcher.prepare({ StretchAlgorithm::tonal, 48'000.0, 48'000.0, 0.0, 0.75, 1.0, 2 });
        (void) stretched(stretcher, source, 0, 2'048);
        const auto after = stretched(stretcher, source, 60'000, 4'096);
        expect(rmsOf(after[0], 0, 4'096) > 0.28, "after a jump the sound plays on at once");
    }
}

void timeStretchFollowsPitchCurve()
{
    // Half a second at its pitch, then an octave up: the tone follows, its
    // time kept.
    auto curve = std::make_shared<std::vector<float>>();
    for (auto knot = 0; knot < 600; ++knot)
        curve->push_back(knot < 100 ? 0.0f : 12.0f);
    auto source = toneSource(440.0, 4.0);
    for (const auto algorithm : { StretchAlgorithm::tonal, StretchAlgorithm::complex }) {
        StretchSettings settings { algorithm, 48'000.0, 48'000.0, 0.0, 1.0, 1.0, 2 };
        settings.pitchCurve = curve;
        TimeStretcher stretcher;
        stretcher.prepare(settings);
        const auto out = stretched(stretcher, source, 0, 96'000);
        std::vector<float> before(out[0].begin() + 4'000, out[0].begin() + 20'000);
        std::vector<float> after(out[0].begin() + 40'000, out[0].begin() + 90'000);
        const auto low = strongestFrequency(before, 48'000.0, 380.0, 500.0);
        const auto high = strongestFrequency(after, 48'000.0, 800.0, 960.0);
        if (std::abs(low - 440.0) >= 2.0 || std::abs(high - 880.0) >= 3.0)
            std::cout << "pitch curve " << static_cast<int>(algorithm) << ": " << low << " then " << high << " Hz\n";
        expect(std::abs(low - 440.0) < 2.0, "a pitch curve starts at the sound's pitch");
        expect(std::abs(high - 880.0) < 3.0, "a pitch curve then takes it an octave up");
        expect(rmsOf(out[0], 40'000, 90'000) > 0.25, "a sound under a pitch curve keeps its level");
    }
    // Clicks keep their places in time while the pitch moves.
    VectorSource clicks;
    clicks.channels.assign(1, std::vector<float>(48'000 * 3));
    for (std::size_t at = 2'400; at < clicks.channels[0].size(); at += 12'000)
        for (std::size_t i = 0; i < 1'200 && at + i < clicks.channels[0].size(); ++i)
            clicks.channels[0][at + i] = static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 120.0)
                                                            * std::sin(2.0 * std::numbers::pi * 3'000.0 * i / 48'000.0));
    StretchSettings settings { StretchAlgorithm::beats, 48'000.0, 48'000.0, 0.0, 1.0, 1.0, 1 };
    settings.pitchCurve = curve;
    TimeStretcher stretcher;
    stretcher.prepare(settings);
    const auto out = stretched(stretcher, clicks, 0, 120'000);
    auto weakest = 1.0f;
    for (std::size_t click = 1; click < 9; ++click) {
        const auto expected = 2'400 + click * 12'000;
        auto peak = 0.0f;
        for (auto at = expected - 600; at < expected + 600; ++at)
            peak = std::max(peak, std::abs(out[0][at]));
        weakest = std::min(weakest, peak);
    }
    expect(weakest > 0.5f, "under a pitch curve the clicks stay where they were");
}

void timeStretchKeepsAttacks()
{
    // Clicks every quarter second, with a short decay: stretched twice as
    // long, "beats" keeps their peaks sharper than "tonal".
    VectorSource clicks;
    clicks.channels.assign(1, std::vector<float>(48'000 * 3));
    for (std::size_t at = 2'400; at < clicks.channels[0].size(); at += 12'000)
        for (std::size_t i = 0; i < 1'200 && at + i < clicks.channels[0].size(); ++i)
            clicks.channels[0][at + i] = static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 120.0)
                                                            * std::sin(2.0 * std::numbers::pi * 3'000.0 * i / 48'000.0));
    // Each click's peak, stretched twice as long: where it falls (twice as
    // late), and how much of the click is left there.
    const auto peaks = [&](const StretchAlgorithm algorithm) {
        TimeStretcher stretcher;
        stretcher.prepare({ algorithm, 48'000.0, 48'000.0, 0.0, 0.5, 1.0, 1 });
        const auto out = stretched(stretcher, clicks, 0, 120'000);
        auto weakest = 1.0f;
        for (std::size_t click = 0; click < 4; ++click) {
            const auto expected = (2'400 + click * 12'000) * 2;
            auto peak = 0.0f;
            for (auto at = expected - 600; at < expected + 600; ++at)
                peak = std::max(peak, std::abs(out[0][at]));
            weakest = std::min(weakest, peak);
        }
        return weakest;
    };
    const auto beats = peaks(StretchAlgorithm::beats);
    const auto tonal = peaks(StretchAlgorithm::tonal);
    expect(beats > 0.7f, "beats keeps every attack where it falls, whole");
    expect(beats > tonal * 1.2f, "beats keeps the attacks sharper than tonal");
    // Texture: ten times as long, still sounding.
    VectorSource noise;
    noise.channels.assign(2, std::vector<float>(48'000));
    std::uint32_t seed = 12'345;
    for (auto& channel : noise.channels)
        for (auto& sample : channel) {
            seed = seed * 1'664'525u + 1'013'904'223u;
            sample = (static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f) * 0.5f;
        }
    TimeStretcher texture;
    texture.prepare({ StretchAlgorithm::texture, 48'000.0, 48'000.0, 0.0, 0.1, 1.0, 2 });
    const auto out = stretched(texture, noise, 0, 96'000);
    const auto inLevel = rmsOf(noise.channels[0], 0, 48'000);
    const auto outLevel = rmsOf(out[0], 24'000, 96'000);
    expect(std::abs(20.0 * std::log10(outLevel / inLevel)) < 3.0, "texture smears a sound and keeps its level");
}


void stretchedLoopKeepsItsBeats()
{
    // A loop of four beats at 100 BPM (2.4 s): a click on each beat. At
    // 120 BPM it lasts two seconds exactly, its clicks on the new beats, and
    // played round its end comes back to its start without a gap.
    std::vector<std::vector<float>> loop(2, std::vector<float>(115'200, 0.0f));
    for (int beat = 0; beat < 4; ++beat)
        for (std::size_t i = 0; i < 1'200; ++i)
            for (auto& channel : loop)
                channel[static_cast<std::size_t>(beat) * 28'800 + i]
                    = static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 120.0)
                                         * std::sin(2.0 * std::numbers::pi * 3'000.0 * static_cast<double>(i) / 48'000.0));
    for (const auto algorithm : { StretchAlgorithm::beats, StretchAlgorithm::repitch }) {
        const auto out = renderStretchedLoop(loop, 48'000.0, 100.0, 4, 120.0, 48'000.0, algorithm);
        expect(out.size() == 2 && out[0].size() == 96'000, "a stretched loop lasts its beats at the new tempo");
        if (out.empty() || out[0].size() != 96'000)
            continue;
        auto weakest = 1.0f;
        auto between = 0.0f;
        for (std::size_t beat = 0; beat < 4; ++beat) {
            auto peak = 0.0f;
            for (auto at = beat * 24'000; at < beat * 24'000 + 600; ++at)
                peak = std::max(peak, std::abs(out[0][at]));
            weakest = std::min(weakest, peak);
            for (auto at = beat * 24'000 + 6'000; at < beat * 24'000 + 18'000; ++at)
                between = std::max(between, std::abs(out[0][at]));
        }
        expect(weakest > 0.5f && between < 0.05f,
               std::string(algorithm == StretchAlgorithm::beats ? "beats" : "repitch")
                   + ": each click on its beat at the new tempo, silence between");
    }
    // A held tone keeps its pitch (tonal), and its end runs into its start.
    std::vector<std::vector<float>> tone(1, std::vector<float>(115'200));
    for (std::size_t i = 0; i < tone[0].size(); ++i)
        tone[0][i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 250.0 * static_cast<double>(i) / 48'000.0));
    const auto held = renderStretchedLoop(tone, 48'000.0, 100.0, 4, 120.0, 48'000.0, StretchAlgorithm::tonal);
    if (held.empty() || held[0].size() != 96'000) {
        expect(false, "a held loop renders");
        return;
    }
    auto crossings = 0;
    for (std::size_t i = 24'000; i < 72'000; ++i)
        crossings += (held[0][i - 1] < 0.0f) != (held[0][i] < 0.0f) ? 1 : 0;
    const auto frequency = static_cast<double>(crossings) / 2.0;
    auto seam = 0.0;
    for (std::size_t i = 0; i < 480; ++i)
        seam = std::max(seam, static_cast<double>(std::abs(held[0][i])) + std::abs(held[0][95'999 - i]));
    expect(std::abs(frequency - 250.0) < 3.0 && seam > 0.5,
           "a held loop keeps its pitch, and sounds through its seam (" + std::to_string(frequency) + " Hz)");
}


// ---- Convolution -----------------------------------------------------------------------------

void convolverMatchesDirectConvolution()
{
    // A response of 1000 taps (head and many blocks), noise through it: the
    // same as the convolution done the long way, sample for sample.
    std::uint32_t seed = 777;
    const auto random = [&seed] {
        seed = seed * 1'664'525u + 1'013'904'223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f;
    };
    std::vector<float> response(1'000);
    for (std::size_t i = 0; i < response.size(); ++i)
        response[i] = random() * static_cast<float>(std::exp(-static_cast<double>(i) / 300.0));
    std::vector<float> input(6'000);
    for (auto& sample : input)
        sample = random();
    Convolver convolver;
    convolver.prepare(response);
    auto worst = 0.0;
    for (std::size_t n = 0; n < input.size(); ++n) {
        const auto fast = convolver.process(input[n]);
        auto slow = 0.0;
        for (std::size_t k = 0; k < response.size() && k <= n; ++k)
            slow += static_cast<double>(response[k]) * input[n - k];
        worst = std::max(worst, std::abs(slow - fast));
    }
    expect(worst < 1.0e-4, "the convolver is the convolution, with no delay");
    // Read at another rate: the same length in seconds, scaled to unit power.
    ImpulseResponse recorded;
    recorded.sampleRate = 44'100.0;
    recorded.channels = { std::vector<float>(4'410, 0.0f) };
    recorded.channels[0][0] = 1.0f;
    const auto read = impulseAtRate(recorded, 48'000.0);
    expect(read.size() == 1 && read[0].size() == 4'800, "a response read at another rate keeps its length");
    auto energy = 0.0;
    for (const auto sample : read[0])
        energy += static_cast<double>(sample) * sample;
    expect(std::abs(energy - 1.0) < 1.0e-3, "a response is scaled to unit power");
}

// The effects added on 2026-10-14.
#include "VoiceEffectTests.inc"
#include "PitchEffectTests.inc"
#include "SpaceEffectTests.inc"
#include "RhythmEffectTests.inc"
#include "MotionEffectTests.inc"
#include "ResonanceEffectTests.inc"
#include "UtilityEffectTests.inc"
#include "MidiEffectTests.inc"
#include "PitchTrackerTests.inc"
#include "GrainTests.inc"
#include "SilexTests.inc"
#include "DjFoundryTests.inc"
#include "VoiceAssetsTests.inc"
#include "ControlTests.inc"

} // namespace

int main()
{
    spscRingIsBoundedAndFifo();
    transportUsesSamplePositions();
    renderPlanProcessesBranchesAndMixes();
    renderPlanAlignsParallelLatencies();
    dynamicsListenToTheirSidechain();
    equaliserBandsMoveWithTheSound();
    knobsGlideAndCardsSwitchOff();
    renderPlanRejectsCycles();
    sharedNativeGraphCompilerBuildsTheHostIndependentProgram();
    realtimeEngineAppliesCommandsAtBlockBoundary();
    oscillatorIsDeterministicAfterReset();
    steadyStateRenderingDoesNotAllocate();
    nativeEffectGraphDoesNotAllocate();
    fundamentalAudioPrimitivesProcessDeterministically();
    realtimePublisherReaderIsLockFreeAndAllocationFree();
    realtimePublisherRetiresSnapshotsInsteadOfFreeingThemInTheCallback();
    realtimePublisherSurvivesConcurrentSwapping();
    everyDeclaredValueInputDrivesAParameterField();
    everyVoiceWaveformProducesSound();
    wavetableUnisonKeepsItsLevelWithoutDetune();
    stringVoiceRingsAtTheNoteItWasGiven();
    polyVoiceDoesNotAllocateWhileSounding();
    voiceFilterResonatesAsDrawn();
    polyVoiceShapesTheNoteAsItsCardsSay();
    soundModelVoicePlaysItsPartials();
    soundModelPlaysStereoHeadAndLayers();
    prismSynthPlays();
    samplerPlaysItsZones();
    samplerPlaysAsItMoves();
    samplerStreamsLongRecordings();
    channelLayoutsConvert();
    limiterProHoldsItsCeiling();
    compressorProKeepsItsSoundAndRange();
    reverbProDecaysAsSet();
    filterProMatchesItsCurve();
    kickPlaysItsPitchFall();
    drumRecipesRender();
    kickVoicePlaysOut();
    ampToneStackFollowsItsKnobs();
    ampDistortsAsTurned();
    cabinetMatchesItsCurve();
    microphoneTakesItsModel();
    ampSwitchesChannels();
    roomTakesItsSize();
    pressionSplitsFlatAndPresses();
    fournaiseSaturatesInCharacter();
    echoBandeRepeatsOnItsHeads();
    multibandSplitsAndActs();
    veloursCalmsResonances();
    patineAgesTheSound();
    sonometrePassesUntouched();
    trancheShapesAChannel();
    finitionMastersWithinItsCeiling();
    haloRingsInCharacter();
    ambrePlaysInTune();
    colosseSoundsEveryWay();
    mosaiqueEnginesPlayInTune();
    nebuleuseLayersSound();
    laterSynthModelsPlay();
    jadePlaysInTune();
    prismGoesFurther();
    batterieMixesItsPieces();
    timeStretchKeepsPitchAndLength();
    timeStretchFollowsPitchCurve();
    timeStretchKeepsAttacks();
    stretchedLoopKeepsItsBeats();
    convolverMatchesDirectConvolution();
    voiceEffectTests();
    pitchEffectTests();
    spacefxEffectTests();
    rhythmEffectTests();
    motionEffectTests();
    resonanceEffectTests();
    utilityEffectTests();
    midiEffectTests();
    pitchTrackerReadsInTune();
    samplerPlaysGrains();
    silexPlaysItsPieces();
    voiceAssetsTravelWithTheirSound();
    controlMidiAddressesGoBothWays();
    controlModifiersShapeAndGiveBack();
    controlRouterMovesTargetsAndFeedsBack();
    djDeckPlaysAtItsTempo();
    djChannelKillsItsBands();
    djEffectsPlayInTime();
    djAnalysisFindsTempoAndKey();
    loudnessOfASine();
    foundrySoundEdits();
    foundrySessionEdits();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All CyDAW engine tests passed\n";
    return 0;
}
