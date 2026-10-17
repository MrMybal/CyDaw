// Modulators (Modulators.h): each kind's output as the runtime evaluates it
// block by block, and a cable's amount turned while it plays. Included by
// CoreTests.cpp.

// A modulator node of `kind`, its catalogue defaults, then `settings` by key.
cydaw::engine::NativeDspNode modulatorNode(const std::uint64_t id, const cydaw::engine::NativeDspNodeKind kind,
                                           const std::vector<std::pair<const char*, float>>& settings)
{
    using namespace cydaw::engine;
    NativeDspNode node { id, kind };
    if (const auto* effect = effectForKind(kind)) {
        applyEffectDefaults(*effect, node.parameters);
        for (const auto& [key, value] : settings)
            if (const auto* parameter = effectParameter(*effect, key))
                setEffectValue(node.parameters, *parameter, value);
    }
    return node;
}

void modulatorsMoveAsTheyAreSet()
{
    using namespace cydaw::engine;
    using cydaw::native::NativeDspRuntime;
    constexpr double rate = 48'000.0;
    constexpr int block = 480; // 10 ms
    std::array<float, block> left {};
    std::array<float, block> right {};
    float* pointers[] { left.data(), right.data() };
    cydaw::native::PlanarBuffer buffer { pointers, 2, block };
    const auto base = [](std::vector<NativeDspNode> extra) {
        NativeDspGraph graph;
        graph.nodes = { { 1, NativeDspNodeKind::input }, { 2, NativeDspNodeKind::gain }, { 3, NativeDspNodeKind::output } };
        graph.nodes.insert(graph.nodes.end(), extra.begin(), extra.end());
        graph.connections = { { 1, 2 }, { 2, 3 } };
        graph.outputNodeId = 3;
        return graph;
    };
    // The value of node `id` after each of `blocks` blocks (the sound in: `level`).
    const auto run = [&](NativeDspRuntime& runtime, const std::uint64_t id, const int blocks, const float level = 0.0f,
                         const double timeline = std::numeric_limits<double>::quiet_NaN()) {
        std::vector<float> values;
        for (int index = 0; index < blocks; ++index) {
            left.fill(level);
            right.fill(level);
            (void) runtime.process(buffer, 0, block, timeline + index * (block / rate));
            float value {};
            runtime.controlValuesNow(&id, &value, 1);
            values.push_back(value);
        }
        return values;
    };
    const auto near = [](const float value, const float expected, const float tolerance = 1.0e-3f) {
        return std::abs(value - expected) <= tolerance;
    };

    {
        // A sine at 1 Hz: a quarter of a second in, its top; unipolar, from 0 to 1.
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        expect(runtime.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modLfo, { { "rate", 1.0f } }) })).wasOk(),
               "modulators: an LFO compiles as a control node");
        const auto values = run(runtime, 4, 26);
        expect(near(values[0], 0.0f) && near(values[25], 1.0f) && near(values[12], std::sin(0.24f * 3.14159265f)),
               "modulators: a free LFO's sine runs at its rate");
        NativeDspRuntime unipolar;
        unipolar.prepare(rate, block, 2);
        (void) unipolar.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modLfo, { { "rate", 1.0f }, { "bipolar", 0.0f } }) }));
        const auto halves = run(unipolar, 4, 26);
        expect(near(halves[0], 0.5f) && near(halves[25], 1.0f), "modulators: a unipolar LFO runs from 0 to 1");
    }
    {
        // On the tempo while the song plays: a quarter note at 120 BPM is
        // half a second; an eighth of a second into the song, a quarter cycle.
        const auto tempo = NativeDspRuntime::sessionTempo();
        const auto playing = NativeDspRuntime::sessionPlaying();
        NativeDspRuntime::setSessionTempo(120.0);
        NativeDspRuntime::setSessionPlaying(true);
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modLfo, { { "sync", 5.0f } }) }));
        const auto values = run(runtime, 4, 1, 0.0f, 0.125);
        const auto later = run(runtime, 4, 1, 0.0f, 0.375);
        expect(near(values[0], 1.0f) && near(later[0], -1.0f), "modulators: a synced LFO follows the song's beat");
        NativeDspRuntime::setSessionTempo(tempo);
        NativeDspRuntime::setSessionPlaying(playing);
    }
    {
        // Four steps, one a block (100 steps a second), round and round.
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modSteps,
                                                     { { "steps", 4.0f }, { "sync", 0.0f }, { "rate", 100.0f },
                                                       { "step1", 0.1f }, { "step2", 0.9f }, { "step3", 0.4f }, { "step4", 0.7f } }) }));
        const auto values = run(runtime, 4, 6);
        expect(near(values[0], 0.1f) && near(values[1], 0.9f) && near(values[3], 0.7f) && near(values[4], 0.1f)
                   && near(values[5], 0.9f),
               "modulators: steps play their values in turn");
    }
    {
        // An envelope opened by a note, held, then let go.
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modAdsr,
                                                     { { "attack", 0.05f }, { "decay", 0.05f }, { "sustain", 0.5f },
                                                       { "release", 0.1f }, { "velocity", 0.0f } }) }));
        (void) run(runtime, 4, 1);
        modulator::ControlNotes notes;
        notes.noteOns = 1;
        notes.held = 1;
        notes.note = 72.0f;
        notes.velocity = 1.0f;
        runtime.setControlNotes(notes);
        const auto held = run(runtime, 4, 12);
        notes.held = 0;
        runtime.setControlNotes(notes);
        const auto released = run(runtime, 4, 12);
        expect(near(held[0], 0.2f) && near(held[4], 1.0f) && near(held[9], 0.5f) && near(held[11], 0.5f),
               "modulators: an ADSR rises, falls to its sustain and holds there while the key is down");
        expect(near(released[0], 0.45f) && near(released[11], 0.0f), "modulators: let go, it falls over its release");
    }
    {
        // The note an octave over its root, on a range of four octaves: half way up.
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(base({ modulatorNode(4, NativeDspNodeKind::modNote, {}),
                                       modulatorNode(5, NativeDspNodeKind::modExpression, { { "smooth", 0.0f } }) }));
        modulator::ControlNotes notes;
        notes.noteOns = 1;
        notes.held = 1;
        notes.note = 72.0f;
        notes.modWheel = 0.25f;
        runtime.setControlNotes(notes);
        expect(near(run(runtime, 4, 1)[0], 0.5f), "modulators: keytrack gives the note around its root");
        expect(near(run(runtime, 5, 1)[0], 0.25f), "modulators: expressions follow the modulation wheel");
    }
    {
        // Two macros into a math modulator: added, then mixed half way.
        auto graph = base({ modulatorNode(4, NativeDspNodeKind::modMacro, { { "value", 0.2f } }),
                            modulatorNode(5, NativeDspNodeKind::modMacro, { { "value", 0.6f } }),
                            modulatorNode(6, NativeDspNodeKind::modMath, { { "mode", 1.0f } }) });
        graph.modulations = { { 4, 6, 3, 1 }, { 5, 6, 3, 2 } };
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        expect(runtime.setGraph(graph).wasOk(), "modulators: a math modulator takes two modulators in");
        const auto added = run(runtime, 6, 1)[0];
        graph.nodes.back() = modulatorNode(6, NativeDspNodeKind::modMath, { { "mode", 0.0f }, { "blend", 0.5f } });
        expect(runtime.updateParameters(graph).wasOk(), "modulators: its mode changes live");
        const auto mixed = run(runtime, 6, 1)[0];
        expect(near(added, 0.8f) && near(mixed, 0.4f), "modulators: math adds and mixes what it is given");
    }
    {
        // A follower on the gain card hears its sound.
        auto follower = modulatorNode(4, NativeDspNodeKind::modFollower, { { "attack", 1.0f } });
        follower.parameters.eqBands[modulator::ownerLowSlot] = modulator::ownerLowOf(2);
        follower.parameters.eqBands[modulator::ownerHighSlot] = modulator::ownerHighOf(2);
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(base({ follower }));
        const auto values = run(runtime, 4, 6, 0.5f);
        expect(near(values[5], 0.5f, 0.01f), "modulators: an envelope follower follows its card's level");
    }
    {
        // A macro moving the gain: its amount turned while it plays is no
        // new graph, and the sound follows.
        auto graph = base({ modulatorNode(4, NativeDspNodeKind::modMacro, { { "value", 1.0f } }) });
        graph.modulations = { { 4, 2, 3, 0, "gain-db", 1, 0.0f, -60.0f, 0.0f } };
        NativeDspRuntime runtime;
        runtime.prepare(rate, block, 2);
        (void) runtime.setGraph(graph);
        graph.nodes[1].parameters.gainDb = -30.0f;
        (void) runtime.updateParameters(graph);
        (void) run(runtime, 4, 30, 0.5f);
        const auto before = left.back();
        graph.modulations[0].amount = 0.5f;
        expect(runtime.updateParameters(graph).wasOk(), "modulators: an amount turned is a live update");
        (void) run(runtime, 4, 30, 0.5f);
        const auto after = left.back();
        expect(before < 0.02f && after > 0.4f, "modulators: the knob moves by the new amount");
    }
}
