// The engine tests of the MIDI effects (MidiEffects.cpp). Included by
// EngineTests.cpp inside its anonymous namespace.

namespace midi_effect_tests {

using cydaw::engine::MidiEffectEvent;

// A chain of one effect, its values set by suffix over its defaults.
cydaw::engine::MidiEffectsPlan planOf(const cydaw::engine::NativeDspNodeKind kind,
                                      const std::vector<std::pair<const char*, float>>& values = {})
{
    using namespace cydaw::engine;
    NativeDspNodeParameters parameters;
    const auto* effect = effectForKind(kind);
    applyEffectDefaults(*effect, parameters);
    for (const auto& [suffix, value] : values)
        if (const auto* parameter = effectParameterForSuffix(suffix))
            setEffectValue(parameters, *parameter, value);
    MidiEffectsPlan plan;
    plan.effects[0] = midiEffectSettingsOf(*effect, parameters);
    plan.count = 1;
    return plan;
}

MidiEffectEvent on(const std::uint32_t offset, const int note, const int velocity = 100)
{
    return { offset, 0x90, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity) };
}

MidiEffectEvent off(const std::uint32_t offset, const int note)
{
    return { offset, 0x80, static_cast<std::uint8_t>(note), 0 };
}

// What comes out, block by block, as (sample since the start, event).
struct Heard {
    std::int64_t at {};
    MidiEffectEvent event {};
};

// Runs `blocks` blocks of 256 at 120 bpm; `input(block)` gives each block's
// events. Transport playing from 0 when `playing`.
template <typename Input>
std::vector<Heard> run(const cydaw::engine::MidiEffectsPlan& plan, const int blocks, Input&& input, const bool playing = false)
{
    cydaw::engine::MidiEffectChain chain;
    chain.prepare(48'000.0);
    std::vector<Heard> heard;
    std::array<MidiEffectEvent, 1'024> out {};
    for (int block = 0; block < blocks; ++block) {
        const auto events = input(block);
        cydaw::engine::MidiEffectClock clock;
        clock.sampleRate = 48'000.0;
        clock.beatsPerMinute = 120.0;
        clock.playing = playing;
        clock.quarterNotes = static_cast<double>(block) * 256.0 / 24'000.0;
        const auto count = chain.process(events.data(), events.size(), out.data(), out.size(), 256, clock, &plan);
        for (std::size_t index = 0; index < count; ++index)
            heard.push_back({ static_cast<std::int64_t>(block) * 256 + out[index].offset, out[index] });
    }
    return heard;
}

std::vector<int> notesOn(const std::vector<Heard>& heard)
{
    std::vector<int> notes;
    for (const auto& item : heard)
        if (item.event.isNoteOn())
            notes.push_back(item.event.data1);
    return notes;
}

bool balanced(const std::vector<Heard>& heard)
{
    std::array<int, 128> sounding {};
    for (const auto& item : heard) {
        if (item.event.isNoteOn())
            ++sounding[item.event.data1];
        else if (item.event.isNoteOff())
            sounding[item.event.data1] = 0;
    }
    return std::all_of(sounding.begin(), sounding.end(), [](const int count) { return count == 0; });
}

void chordsScaleAndEchoes()
{
    using cydaw::engine::NativeDspNodeKind;
    // A major chord on C4, let go: its three notes start and end.
    {
        const auto heard = run(planOf(NativeDspNodeKind::accords), 4, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(10, 60) } : block == 2 ? std::vector<MidiEffectEvent> { off(5, 60) }
                                                                                          : std::vector<MidiEffectEvent> {};
        });
        auto notes = notesOn(heard);
        std::sort(notes.begin(), notes.end());
        expect(notes == std::vector<int> { 60, 64, 67 }, "midi: a major chord on C4 plays C, E and G");
        expect(balanced(heard), "midi: the chord's notes all end with the key");
    }
    // A minor seventh in its first inversion.
    {
        const auto heard = run(planOf(NativeDspNodeKind::accords, { { "accords.chord", 7.0f }, { "accords.inversion", 1.0f } }), 2,
                               [](const int block) { return block == 0 ? std::vector<MidiEffectEvent> { on(0, 57) } : std::vector<MidiEffectEvent> {}; });
        auto notes = notesOn(heard);
        std::sort(notes.begin(), notes.end());
        expect(notes == std::vector<int> { 60, 64, 67, 69 }, "midi: A minor seventh, first inversion: C E G A");
    }
    // Strummed: each note a little later.
    {
        const auto heard = run(planOf(NativeDspNodeKind::accords, { { "accords.strum", 20.0f } }), 12,
                               [](const int block) { return block == 0 ? std::vector<MidiEffectEvent> { on(0, 60) } : std::vector<MidiEffectEvent> {}; });
        std::vector<std::int64_t> starts;
        for (const auto& item : heard)
            if (item.event.isNoteOn())
                starts.push_back(item.at);
        expect(starts.size() == 3 && starts[1] >= 950 && starts[1] <= 970 && starts[2] >= 1'910 && starts[2] <= 1'930,
               "midi: a chord strummed 20 ms apart");
    }
    // C major, nearest: C# goes up to D, F# to G; removing drops them.
    {
        const auto heard = run(planOf(NativeDspNodeKind::gamme), 2, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 61), on(1, 66), on(2, 64), off(100, 61), off(100, 66), off(100, 64) }
                              : std::vector<MidiEffectEvent> {};
        });
        expect(notesOn(heard) == std::vector<int> { 62, 67, 64 }, "midi: into C major, C# becomes D and F# G");
        expect(balanced(heard), "midi: a scaled note ends as its key does");
        const auto removed = run(planOf(NativeDspNodeKind::gamme, { { "gamme.mode", 3.0f } }), 2, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 61), on(1, 60), off(50, 61), off(50, 60) } : std::vector<MidiEffectEvent> {};
        });
        expect(notesOn(removed) == std::vector<int> { 60 } && balanced(removed), "midi: removing what is out of the scale");
        const auto degrees = run(planOf(NativeDspNodeKind::gamme, { { "gamme.degrees", 2.0f } }), 1,
                                 [](const int) { return std::vector<MidiEffectEvent> { on(0, 60) }; });
        expect(notesOn(degrees) == std::vector<int> { 64 }, "midi: two degrees of C major above C is E");
    }
    // Three echoes a quarter note apart (1/4 at 120 bpm: 24000 samples),
    // softer each time, an octave higher each.
    {
        const auto heard = run(planOf(NativeDspNodeKind::echoNotes, { { "echonotes.sync", 9.0f }, { "echonotes.repeats", 3.0f },
                                                                       { "echonotes.pitch", 12.0f }, { "echonotes.decay", 0.5f } }),
                               400, [](const int block) {
                                   return block == 0 ? std::vector<MidiEffectEvent> { on(0, 48, 120) } : block == 10 ? std::vector<MidiEffectEvent> { off(0, 48) }
                                                                                                                     : std::vector<MidiEffectEvent> {};
                               });
        std::vector<Heard> starts;
        for (const auto& item : heard)
            if (item.event.isNoteOn())
                starts.push_back(item);
        expect(starts.size() == 4 && starts[1].at == 24'000 && starts[2].at == 48'000 && starts[3].at == 72'000,
               "midi: note echoes a quarter note apart");
        expect(starts.size() == 4 && starts[3].event.data1 == 84 && starts[1].event.data2 == 60 && starts[3].event.data2 == 15,
               "midi: each echo an octave up and half as loud");
        expect(balanced(heard), "midi: every echo ends");
    }
}

void arpeggiatorAndSequencer()
{
    using cydaw::engine::NativeDspNodeKind;
    // Up, 1/16 at 120 bpm (6000 samples), a C major chord held for a beat
    // and a half: C E G C E G.
    {
        const auto heard = run(planOf(NativeDspNodeKind::arpege), 160, [](const int block) {
            if (block == 0)
                return std::vector<MidiEffectEvent> { on(0, 60), on(0, 64), on(0, 67) };
            if (block == 140)
                return std::vector<MidiEffectEvent> { off(160, 60), off(160, 64), off(160, 67) };
            return std::vector<MidiEffectEvent> {};
        });
        const auto notes = notesOn(heard);
        expect(notes.size() == 6 && notes == std::vector<int> { 60, 64, 67, 60, 64, 67 }, "midi: the arpeggiator plays the chord upward, a sixteenth each");
        std::vector<std::int64_t> starts;
        for (const auto& item : heard)
            if (item.event.isNoteOn())
                starts.push_back(item.at);
        expect(starts.size() == 6 && starts[1] - starts[0] == 6'000 && starts[5] - starts[4] == 6'000, "midi: arpeggio steps a sixteenth apart");
        expect(balanced(heard), "midi: the arpeggio's notes all end");
    }
    // Down over two octaves with the transport: on the grid.
    {
        const auto heard = run(planOf(NativeDspNodeKind::arpege, { { "arpege.mode", 1.0f }, { "arpege.octaves", 2.0f } }), 100,
                               [](const int block) {
                                   if (block == 3)
                                       return std::vector<MidiEffectEvent> { on(20, 60), on(20, 67) };
                                   return std::vector<MidiEffectEvent> {};
                               },
                               true);
        const auto notes = notesOn(heard);
        expect(notes.size() >= 4 && notes[0] == 79 && notes[1] == 72 && notes[2] == 67 && notes[3] == 60,
               "midi: arpeggio downward over two octaves");
        expect(!heard.empty() && heard.front().at == 6'000, "midi: with the transport, the arpeggio waits for the grid's next sixteenth");
    }
    // The sequencer with the transport: sixteen steps, its first notes C4,
    // C4, C5 (its default pattern), at each sixteenth.
    {
        const auto heard = run(planOf(NativeDspNodeKind::sequenceur, { { "sequenceur.mode", 1.0f } }), 80,
                               [](const int) { return std::vector<MidiEffectEvent> {}; }, true);
        const auto notes = notesOn(heard);
        expect(notes.size() >= 3 && notes[0] == 60 && notes[1] == 60 && notes[2] == 72, "midi: the sequencer plays its steps with the transport");
        // Held-key mode: transposed by the key, silent without one.
        const auto held = run(planOf(NativeDspNodeKind::sequenceur), 90, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 62) } : block == 60 ? std::vector<MidiEffectEvent> { off(0, 62) }
                                                                                       : std::vector<MidiEffectEvent> {};
        });
        const auto heldNotes = notesOn(held);
        expect(heldNotes.size() >= 3 && heldNotes[0] == 62 && heldNotes[2] == 74, "midi: a held key plays the sequence from itself");
        expect(balanced(held), "midi: the sequence stops with the key");
    }
}

void lengthsRepeatsAndRandom()
{
    using cydaw::engine::NativeDspNodeKind;
    // A fixed length of 100 ms whatever was held.
    {
        const auto heard = run(planOf(NativeDspNodeKind::duree, { { "duree.length", 100.0f } }), 60, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 60) } : block == 50 ? std::vector<MidiEffectEvent> { off(0, 60) }
                                                                                       : std::vector<MidiEffectEvent> {};
        });
        std::int64_t end = -1;
        for (const auto& item : heard)
            if (item.event.isNoteOff())
                end = item.at;
        expect(end == 4'800, "midi: a note made 100 ms long");
    }
    // A note repeated every sixteenth while held, quieter and quieter.
    {
        const auto heard = run(planOf(NativeDspNodeKind::roulement, { { "roulement.ramp", -1.0f } }), 120, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 38, 120) } : block == 100 ? std::vector<MidiEffectEvent> { off(0, 38) }
                                                                                             : std::vector<MidiEffectEvent> {};
        });
        std::vector<Heard> starts;
        for (const auto& item : heard)
            if (item.event.isNoteOn())
                starts.push_back(item);
        expect(starts.size() == 5 && starts[1].at == 6'000 && starts[4].at == 24'000, "midi: a held note repeats every sixteenth");
        expect(starts.size() == 5 && starts[4].event.data2 < starts[0].event.data2, "midi: the repeats get softer");
        expect(balanced(heard), "midi: the repeats stop with the key");
    }
    // Nothing plays at chance 0; its ends are not sent either.
    {
        const auto heard = run(planOf(NativeDspNodeKind::alea, { { "alea.chance", 0.0f } }), 2, [](const int block) {
            return block == 0 ? std::vector<MidiEffectEvent> { on(0, 60), off(100, 60) } : std::vector<MidiEffectEvent> {};
        });
        expect(heard.empty(), "midi: at chance 0 no note plays");
        // Random timing delays a note and its end alike.
        const auto late = run(planOf(NativeDspNodeKind::alea, { { "alea.timing", 50.0f }, { "alea.velocity", 0.0f } }), 40,
                              [](const int block) {
                                  return block == 0 ? std::vector<MidiEffectEvent> { on(0, 60) } : block == 20 ? std::vector<MidiEffectEvent> { off(0, 60) }
                                                                                                             : std::vector<MidiEffectEvent> {};
                              });
        expect(late.size() == 2 && late[0].at >= 0 && late[0].at <= 2'400 && late[1].at - late[0].at == 20 * 256,
               "midi: a random delay moves the note and its end together");
    }
    // A chain removed while notes sound: they are let go.
    {
        cydaw::engine::MidiEffectChain chain;
        chain.prepare(48'000.0);
        const auto plan = planOf(NativeDspNodeKind::accords);
        std::array<MidiEffectEvent, 64> out {};
        const auto first = on(0, 60);
        cydaw::engine::MidiEffectClock clock;
        (void) chain.process(&first, 1, out.data(), out.size(), 256, clock, &plan);
        const auto count = chain.process(nullptr, 0, out.data(), out.size(), 256, clock, nullptr);
        auto offs = 0;
        for (std::size_t index = 0; index < count; ++index)
            offs += out[index].isNoteOff() ? 1 : 0;
        expect(offs == 3, "midi: a chain taken away lets its notes go");
    }
    // Nothing allocated while it runs.
    {
        cydaw::engine::MidiEffectChain chain;
        chain.prepare(48'000.0);
        cydaw::engine::MidiEffectsPlan plan;
        for (const auto kind : { cydaw::engine::NativeDspNodeKind::accords, cydaw::engine::NativeDspNodeKind::arpege,
                                 cydaw::engine::NativeDspNodeKind::echoNotes, cydaw::engine::NativeDspNodeKind::alea })
            plan.effects[plan.count++] = planOf(kind).effects[0];
        std::array<MidiEffectEvent, 1'024> out {};
        cydaw::engine::MidiEffectClock clock;
        const std::array<MidiEffectEvent, 3> keys { on(0, 60), on(4, 64), on(8, 67) };
        (void) chain.process(keys.data(), keys.size(), out.data(), out.size(), 256, clock, &plan);
        allocation_probe::count = 0;
        allocation_probe::enabled = true;
        for (int block = 0; block < 200; ++block)
            (void) chain.process(nullptr, 0, out.data(), out.size(), 256, clock, &plan);
        allocation_probe::enabled = false;
        expect(allocation_probe::count == 0, "midi: the effects allocate nothing while they play");
    }
}

} // namespace midi_effect_tests

void midiEffectTests()
{
    midi_effect_tests::chordsScaleAndEchoes();
    midi_effect_tests::arpeggiatorAndSequencer();
    midi_effect_tests::lengthsRepeatsAndRandom();
}
