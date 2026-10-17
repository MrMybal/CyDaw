// Containers (Containers.h): a head, chains between it and its end, the end
// mixing them as the head's settings say. Each is checked on a tone through
// the whole runtime, as a track plays it. Included by CoreTests.cpp.

namespace container_tests {

using namespace cydaw::engine;
namespace container = cydaw::engine::container;

NativeDspNode headNode(const std::uint64_t id, const NativeDspNodeKind kind,
                       const std::vector<std::pair<const char*, float>>& settings = {})
{
    NativeDspNode node { id, kind };
    if (const auto* effect = effectForKind(kind)) {
        applyEffectDefaults(*effect, node.parameters);
        for (const auto& [key, value] : settings)
            if (const auto* parameter = effectParameter(*effect, key))
                setEffectValue(node.parameters, *parameter, value);
    }
    return node;
}

NativeDspNode gainNode(const std::uint64_t id, const float db)
{
    NativeDspNode node { id, NativeDspNodeKind::gain };
    node.parameters.gainDb = db;
    return node;
}

// input 1 -> head 2 -> branches (a gain each, ids 10 + k, by `branchDb`) ->
// end 3 -> output 4.
NativeDspGraph containerGraph(const NativeDspNode& head, const std::vector<float>& branchDb)
{
    NativeDspGraph graph;
    graph.nodes = { { 1, NativeDspNodeKind::input }, head, { 3, NativeDspNodeKind::containerEnd }, { 4, NativeDspNodeKind::output } };
    graph.connections = { { 1, 2, 0, container::headInputPort },
                          { 2, 3, container::headDryPort, container::endDryPort },
                          { 3, 4, container::endOutputPort, 0 } };
    for (std::size_t branch = 0; branch < branchDb.size(); ++branch) {
        const auto id = 10 + branch;
        graph.nodes.push_back(gainNode(id, branchDb[branch]));
        graph.connections.push_back({ 2, id, container::branchPort(static_cast<int>(branch)), 0 });
        graph.connections.push_back({ id, 3, 0, container::branchPort(static_cast<int>(branch)) });
    }
    graph.outputNodeId = 4;
    return graph;
}

struct Heard {
    double left {};
    double right {};
};

// A tone of `hz` (left at `leftLevel`, right at `rightLevel`) through the
// graph for half a second; the RMS of the last quarter, each side.
Heard play(cydaw::native::NativeDspRuntime& runtime, const double hz, const float leftLevel = 0.5f,
           const float rightLevel = 0.5f)
{
    constexpr double rate = 48'000.0;
    constexpr int block = 256;
    std::array<float, block> left {};
    std::array<float, block> right {};
    float* pointers[] { left.data(), right.data() };
    cydaw::native::PlanarBuffer buffer { pointers, 2, block };
    auto phase = 0.0;
    double sumLeft {}, sumRight {};
    std::size_t counted {};
    constexpr int blocks = static_cast<int>(rate * 0.5) / block;
    for (int index = 0; index < blocks; ++index) {
        for (int frame = 0; frame < block; ++frame) {
            const auto value = static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * hz / rate;
            left[static_cast<std::size_t>(frame)] = value * leftLevel;
            right[static_cast<std::size_t>(frame)] = value * rightLevel;
        }
        (void) runtime.process(buffer, 0, block);
        if (index >= blocks * 3 / 4) {
            for (int frame = 0; frame < block; ++frame) {
                sumLeft += static_cast<double>(left[static_cast<std::size_t>(frame)]) * left[static_cast<std::size_t>(frame)];
                sumRight += static_cast<double>(right[static_cast<std::size_t>(frame)]) * right[static_cast<std::size_t>(frame)];
            }
            counted += block;
        }
    }
    return { std::sqrt(sumLeft / static_cast<double>(counted)), std::sqrt(sumRight / static_cast<double>(counted)) };
}

double db(const double level)
{
    return 20.0 * std::log10(std::max(level, 1.0e-9));
}

} // namespace container_tests

void containersSplitAndMixAsSet()
{
    using namespace container_tests;
    using cydaw::native::NativeDspRuntime;
    const auto steady = 0.5 / std::sqrt(2.0);
    const auto near = [](const double value, const double expected, const double toleranceDb = 0.3) {
        return std::abs(db(value) - db(expected)) <= toleranceDb;
    };
    const auto compiled = [](NativeDspRuntime& runtime, const NativeDspGraph& graph) {
        runtime.prepare(48'000.0, 256, 2);
        return runtime.setGraph(graph).wasOk();
    };
    {
        // Two layers at -6 dB summed: the tone as it was; one muted: half;
        // the other soloed: the same half; mix 0: the dry tone alone.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::fxLayer), { -6.0206f, -6.0206f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a layer container compiles, its branches tapped");
        expect(near(play(runtime, 440.0).left, steady), "containers: two layers sum");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxLayer, { { "mute2", 1.0f } });
        expect(runtime.updateParameters(graph).wasOk(), "containers: a layer's mute is a live value");
        expect(near(play(runtime, 440.0).left, steady * 0.5), "containers: a muted layer is out of the sum");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxLayer, { { "solo1", 1.0f }, { "volume1", -6.0206f } });
        (void) runtime.updateParameters(graph);
        expect(near(play(runtime, 440.0).left, steady * 0.25), "containers: a soloed layer alone, by its volume");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxLayer, { { "mix", 0.0f }, { "volume1", -60.0f }, { "volume2", -60.0f } });
        (void) runtime.updateParameters(graph);
        expect(near(play(runtime, 440.0).left, steady), "containers: mix 0 gives the dry sound");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxLayer, { { "wet-gain", 6.0206f } });
        (void) runtime.updateParameters(graph);
        expect(near(play(runtime, 440.0).left, steady * 2.0), "containers: the wet gain lifts the chains");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxLayer, { { "pan1", 1.0f }, { "pan2", 1.0f } });
        (void) runtime.updateParameters(graph);
        const auto panned = play(runtime, 440.0);
        expect(panned.left < steady * 0.01 && near(panned.right, steady), "containers: a layer's pan");
    }
    {
        // Mid-side: a tone on the left only; the side muted leaves the mid
        // on both sides; the mid muted leaves the side, opposite on each.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::midSideSplit), { 0.0f, -120.0f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a mid-side split compiles");
        const auto mid = play(runtime, 440.0, 0.5f, 0.0f);
        expect(near(mid.left, steady * 0.5) && near(mid.right, steady * 0.5), "containers: the mid alone is the same on both sides");
        graph = containerGraph(headNode(2, NativeDspNodeKind::midSideSplit), { 0.0f, 0.0f });
        NativeDspRuntime whole;
        (void) compiled(whole, graph);
        const auto both = play(whole, 440.0, 0.5f, 0.0f);
        expect(near(both.left, steady) && both.right < steady * 0.01, "containers: mid and side give the stereo back");
    }
    {
        // Stereo split: the left chain silent leaves the right side alone.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::stereoSplit), { -120.0f, 0.0f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a stereo split compiles");
        const auto heard = play(runtime, 440.0, 0.5f, 0.25f);
        expect(heard.left < steady * 0.01 && near(heard.right, steady * 0.5), "containers: each side has its chain");
    }
    {
        // Two bands, both passing: the tone as it was (an allpass); the high
        // band muted: a low tone passes, a high one is gone.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::multiband2, { { "crossover", 1'000.0f } }), { 0.0f, 0.0f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a two-band container compiles");
        expect(near(play(runtime, 1'000.0).left, steady, 0.2) && near(play(runtime, 150.0).left, steady, 0.2)
                   && near(play(runtime, 6'000.0).left, steady, 0.2),
               "containers: two bands sum back to the sound (at, under and over the crossover)");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::multiband2, { { "crossover", 1'000.0f }, { "mute2", 1.0f } });
        (void) runtime.updateParameters(graph);
        expect(near(play(runtime, 100.0).left, steady, 0.3) && db(play(runtime, 8'000.0).left) < db(steady) - 30.0,
               "containers: a band muted takes its frequencies away");
        // Switched off (its end gives what its head received): all of it.
        graph.bypasses = { { 3, 2 } };
        (void) runtime.updateParameters(graph);
        (void) play(runtime, 8'000.0);
        expect(near(play(runtime, 8'000.0).left, steady, 0.3), "containers: switched off, the sound passes whole");
        graph.bypasses.clear();
        auto three = containerGraph(headNode(2, NativeDspNodeKind::multiband3), { 0.0f, 0.0f, 0.0f });
        NativeDspRuntime bands;
        expect(compiled(bands, three), "containers: a three-band container compiles");
        expect(near(play(bands, 250.0).left, steady, 0.3) && near(play(bands, 2'500.0).left, steady, 0.3)
                   && near(play(bands, 800.0).left, steady, 0.3),
               "containers: three bands sum back to the sound");
    }
    {
        // XY: at the top left, A alone.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::xyFx, { { "x", 0.0f }, { "y", 1.0f } }),
                                    { -6.0206f, -120.0f, -120.0f, -120.0f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: an XY container compiles");
        expect(near(play(runtime, 440.0).left, steady * 0.5), "containers: XY at a corner is that corner's chain");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::xyFx, { { "x", 1.0f }, { "y", 1.0f } });
        (void) runtime.updateParameters(graph);
        expect(play(runtime, 440.0).left < steady * 0.01, "containers: XY at another corner, its (silent) chain");
    }
    {
        // A selector: the second chain chosen is heard, the first not.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::fxSelector, { { "selected", 1.0f } }), { -120.0f, -6.0206f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a selector compiles");
        expect(near(play(runtime, 440.0).left, steady * 0.5), "containers: a selector plays the chain chosen");
        graph.nodes[1] = headNode(2, NativeDspNodeKind::fxSelector, { { "selected", 0.0f } });
        (void) runtime.updateParameters(graph);
        expect(play(runtime, 440.0).left < steady * 0.01, "containers: chosen again, the other");
    }
    {
        // A replacer: clicks every tenth of a second become kicks.
        auto graph = containerGraph(headNode(2, NativeDspNodeKind::replacer, { { "threshold", -30.0f } }), { 0.0f });
        NativeDspRuntime runtime;
        expect(compiled(runtime, graph), "containers: a replacer compiles");
        constexpr int block = 256;
        std::array<float, block> left {};
        std::array<float, block> right {};
        float* pointers[] { left.data(), right.data() };
        cydaw::native::PlanarBuffer buffer { pointers, 2, block };
        auto heard = 0.0;
        for (int index = 0; index < 190; ++index) {
            left.fill(0.0f);
            right.fill(0.0f);
            // A click at the start of every 19th block (about 0.1 s).
            if (index % 19 == 0) {
                left[0] = 0.9f;
                right[0] = 0.9f;
            }
            (void) runtime.process(buffer, 0, block);
            for (const auto value : left)
                heard = std::max(heard, static_cast<double>(std::abs(value)));
        }
        std::array<float, 2> readings {};
        const auto read = runtime.nodeReadings(2, readings.data(), readings.size());
        expect(read == 2 && readings[0] >= 8.0f && readings[0] <= 11.0f,
               "containers: the replacer finds each transient (" + std::to_string(readings[0]) + ")");
        expect(heard > 0.3, "containers: and plays its sound in their place");
    }
}
