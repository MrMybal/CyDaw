#include <cydaw/engine/AudioBuffer.h>
#include <cyaudio/sources/AudioTransport.h>
#include <cydsp/analysis/LevelMeter.h>
#include <cydsp/processors/Dynamics.h>
#include <cydsp/processors/MultiBusGraph.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(cyaudio::Status s, const char* message) { require(s == cyaudio::Status::ok, message); }
void storage() {
    using namespace cydaw::engine;
    AudioBuffer input(2, 67), output(3, 71);
    auto a = input.view(67), b = output.view(71);
    for (unsigned ch = 0; ch < 2; ++ch)
        for (unsigned n = 0; n < 67; ++n) a.channel(ch)[n] = float(n + ch) / 128;
    copyAudio(b, a); addAudio(b, a);
    for (unsigned ch = 0; ch < 3; ++ch)
        for (unsigned n = 0; n < 71; ++n)
            require(b.channel(ch)[n] == (ch < 2 && n < 67 ? 2 * a.channel(ch)[n] : 0), "SIMD mix/padding");
    copyAudio(a, a); require(a.channel(1)[66] == 67.0f / 128, "in-place copy");
    addAudio(a, a); require(a.channel(1)[66] == 134.0f / 128, "in-place sum");
    auto copy = output; clearAudio(b);
    require(copy.view(71).channel(1)[66] == 134.0f / 128, "independent copy ownership");
}
void sourceGraph() {
    // The numerical API is available through CyAudioCore's transitive dependency.
    static_assert(std::is_same_v<cyaudio::AudioBuffer<float>, cydsp::AudioBuffer<float>>);
    static_assert(std::is_same_v<cyaudio::AudioBlock<float>, cydsp::AudioBlock<float>>);
    cyaudio::ProcessSpec spec {48000, 67, 3};
    cyaudio::AudioBuffer<float> samples, read, output;
    check(samples.resize(3, 101), "source storage"); check(read.resize(3, 67), "read storage");
    check(output.resize(2, 67), "output storage"); auto data = samples.block();
    for (std::size_t n = 0; n < 101; ++n) {
        data.channel(0)[n] = .5f; data.channel(1)[n] = -.25f; data.channel(2)[n] = 0;
    }
    cyaudio::MemoryAudioSource<float> source; check(source.prepare(data, 48000), "source prepare");
    cyaudio::AudioTransport<float> transport; check(transport.prepare(spec, source), "transport prepare");
    check(transport.enqueue({cyaudio::TransportAction::play}), "play command");
    cydsp::Dynamics<float> dynamics; auto stereo = spec; stereo.channels = 2;
    check(dynamics.prepare(stereo), "sidechain prepare");
    cydsp::DynamicsParameters parameters; parameters.thresholdDb = -20; parameters.kneeDb = 0;
    check(dynamics.setParameters(parameters), "dynamics parameters");
    const std::array<cydsp::DspBus, 2> ins {{{10, 2}, {20, 1}}};
    const std::array<cydsp::DspBus, 1> outs {{{30, 2}}};
    cydsp::DspNode<float> node; node.id = 1; node.context = &dynamics; node.inputs = ins; node.outputs = outs;
    node.process = [](void* p, std::span<const cydsp::AudioBlock<const float>> in,
                      std::span<cydsp::AudioBlock<float>> out) noexcept {
        return static_cast<cydsp::Dynamics<float>*>(p)->process(in[0], in[1], out[0]);
    };
    const std::array<cydsp::DspConnection, 3> connections {{
        {cydsp::graphInputNode, 10, 1, 10}, {cydsp::graphInputNode, 20, 1, 20},
        {1, 30, cydsp::graphOutputNode, 30}}};
    cydsp::MultiBusGraph<float> graph;
    check(graph.prepare(spec, ins, outs, {&node, 1}, connections), "typed graph prepare");
    cydsp::LevelMeter<float> meter; check(meter.prepare(stereo, 101), "meter prepare");
    for (std::size_t frames : {67, 34}) {
        auto block = read.block(frames); check(transport.process(block), "source transport");
        const auto* pointers = block.channelData();
        const std::array<cydsp::AudioBlock<const float>, 2> inputs {{{pointers, 2, frames}, {pointers + 2, 1, frames}}};
        std::array<cydsp::AudioBlock<float>, 1> outputs {output.block(frames)};
        check(graph.process(inputs, outputs), "source graph"); check(meter.process(outputs[0]), "graph meter");
        for (std::size_t n = 0; n < frames; ++n) {
            require(std::abs(outputs[0].channel(0)[n] - .5f) < 1e-6, "programme left");
            require(std::abs(outputs[0].channel(1)[n] + .25f) < 1e-6, "programme right");
        }
    }
    require(std::abs(meter.levels(0).rms - .5) < 1e-6, "uneven blocks RMS");
    require(std::abs(meter.stereoCorrelation() + 1) < 1e-6, "stereo correlation");
    check(transport.process(read.block(17)), "source end padding");
    for (auto value : read.block(17).channel(0)) require(value == 0, "source end silence");
    graph.reset(); meter.reset(); require(meter.levels(0).peak == 0, "retained reset");
}
}
int main() {
    try { storage(); sourceGraph(); std::cout << "CyAudioCore sources/CyDSP composition passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
