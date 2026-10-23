#include <cydaw/engine/MidiEffects.h>

#include <algorithm>
#include <cmath>

namespace cydaw::engine {

namespace {

using Event = MidiEffectEvent;
namespace m = midifx;

constexpr std::size_t kRoom = MidiEffectChain::capacity;
// A note of a channel: channel × 128 + note.
constexpr std::size_t kKeys = 16 * 128;

[[nodiscard]] constexpr std::size_t keyOf(const int channel, const int note) noexcept
{
    return static_cast<std::size_t>(std::clamp(channel, 0, 15)) * 128 + static_cast<std::size_t>(std::clamp(note, 0, 127));
}

[[nodiscard]] Event noteOn(const int channel, const int note, const int velocity) noexcept
{
    return { 0, static_cast<std::uint8_t>(0x90 | (channel & 0x0F)), static_cast<std::uint8_t>(std::clamp(note, 0, 127)),
             static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)) };
}

[[nodiscard]] Event noteOff(const int channel, const int note) noexcept
{
    return { 0, static_cast<std::uint8_t>(0x80 | (channel & 0x0F)), static_cast<std::uint8_t>(std::clamp(note, 0, 127)), 0 };
}

[[nodiscard]] int whole(const float value, const int lowest, const int highest) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), lowest, highest);
}

// The length of a note value (a sync value 1..12), in quarter notes.
[[nodiscard]] double quartersOf(const float sync, const double fallback) noexcept
{
    const auto* division = noteDivisionFor(sync);
    return division != nullptr ? division->beats : fallback;
}

// A block's events, in time order. Sorting must not allocate: insertion,
// which keeps the order of events at the same place.
struct Events {
    std::array<Event, kRoom> items {};
    std::size_t count {};
    void clear() noexcept { count = 0; }
    void add(const Event& event) noexcept
    {
        if (count < items.size())
            items[count++] = event;
    }
    void add(const Event& event, const std::int64_t at, const std::int64_t blockStart) noexcept
    {
        auto placed = event;
        placed.offset = static_cast<std::uint32_t>(std::max<std::int64_t>(0, at - blockStart));
        add(placed);
    }
    void sort() noexcept
    {
        for (std::size_t index = 1; index < count; ++index) {
            const auto item = items[index];
            auto place = index;
            while (place > 0 && items[place - 1].offset > item.offset) {
                items[place] = items[place - 1];
                --place;
            }
            items[place] = item;
        }
    }
};

// What an effect will send later, by the sample it is due at.
struct Pending {
    struct Item {
        std::int64_t at {};
        Event event {};
        std::uint32_t tag {};
    };
    std::array<Item, kRoom> items {};
    std::size_t count {};
    void clear() noexcept { count = 0; }
    void add(const std::int64_t at, const Event& event, const std::uint32_t tag = 0) noexcept
    {
        if (count < items.size())
            items[count++] = { at, event, tag };
    }
    // Those due before `end` into `out` (any overdue at the block's start).
    void release(const std::int64_t start, const std::int64_t end, Events& out) noexcept
    {
        // Earliest first, so a note's end and its next start keep their order.
        for (std::size_t index = 1; index < count; ++index) {
            const auto item = items[index];
            auto place = index;
            while (place > 0 && items[place - 1].at > item.at) {
                items[place] = items[place - 1];
                --place;
            }
            items[place] = item;
        }
        std::size_t kept = 0;
        for (std::size_t index = 0; index < count; ++index) {
            if (items[index].at < end)
                out.add(items[index].event, items[index].at, start);
            else
                items[kept++] = items[index];
        }
        count = kept;
    }
    // Takes out the note-ons of a tag still to come; true when one was.
    bool dropStarts(const std::uint32_t tag, const int channel, const int note) noexcept
    {
        auto dropped = false;
        std::size_t kept = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto& item = items[index];
            if (item.tag == tag && item.event.isNoteOn() && item.event.channel() == channel && item.event.data1 == note) {
                dropped = true;
                continue;
            }
            items[kept++] = items[index];
        }
        count = kept;
        return dropped;
    }
};

// The block: where it starts (in samples since the chain was prepared), how
// long, and where that is in quarter notes.
struct Block {
    std::int64_t start {};
    std::uint32_t frames {};
    double samplesPerQuarter { 24'000.0 };
    double firstQuarter {};
    bool playing {};

    [[nodiscard]] std::int64_t end() const noexcept { return start + frames; }
    [[nodiscard]] double quarterAt(const std::int64_t sample) const noexcept
    {
        return firstQuarter + static_cast<double>(sample - start) / samplesPerQuarter;
    }
    [[nodiscard]] std::int64_t sampleAt(const double quarter) const noexcept
    {
        return start + static_cast<std::int64_t>(std::ceil((quarter - firstQuarter) * samplesPerQuarter - 1.0e-6));
    }
};

// A random number generator for the audio thread.
struct Random {
    std::uint32_t state { 0x9e3779b9u };
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state & 0xffffffu) / static_cast<float>(0x1000000);
    }
};

// When a step of a grid falls: step k from `origin`, `length` quarters each,
// every second one late by the swing.
[[nodiscard]] double stepQuarter(const double origin, const double length, const std::int64_t step, const float swing) noexcept
{
    const auto late = (step % 2 != 0) ? static_cast<double>(swing) * length * 0.5 : 0.0;
    return origin + static_cast<double>(step) * length + late;
}

// The first step at or after `quarter` (a note played just after a step
// counts as on it).
[[nodiscard]] std::int64_t firstStepFrom(const double origin, const double length, const double quarter) noexcept
{
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil((quarter - origin) / length - 0.04)));
}

// A running grid kept on the beat: when the transport starts or stops (the
// bars' grid or the free clock), or its step changed, its next step is found
// again from now.
template <typename Grid>
void keepOnTheBeat(Grid& grid, const Block& block, const double length, const float swing) noexcept
{
    if (!grid.running) {
        grid.playing = block.playing;
        return;
    }
    const auto next = block.sampleAt(stepQuarter(grid.origin, length, grid.step, swing));
    const auto stepSamples = length * block.samplesPerQuarter;
    if (grid.playing != block.playing || next < block.start
        || static_cast<double>(next - block.start) > stepSamples * 1.5 + 1.0) {
        if (grid.playing != block.playing)
            grid.origin = block.playing ? 0.0 : block.quarterAt(block.start);
        grid.step = firstStepFrom(grid.origin, length, block.quarterAt(block.start));
        grid.playing = block.playing;
    }
}

// ---- The effects' states -------------------------------------------------------------------------

struct ArpState {
    struct Held {
        int note {};
        int velocity {};
        int channel {};
        std::uint32_t order {};
    };
    std::array<Held, 32> held {};
    int heldCount {};
    int keysDown {};
    std::uint32_t nextOrder {};
    bool running {};
    bool playing {};
    double origin {};
    std::int64_t step {};
    std::int64_t played {};
};

struct ChordState {
    struct Made {
        std::array<std::int8_t, 8> notes {};
        int count {};
        std::uint32_t tag {};
    };
    std::array<Made, kKeys> made {};
    std::uint32_t nextTag { 1 };
    bool downward {};
};

struct EchoState {
    struct Echo {
        int repeats {};
        int pitch {};
        std::int64_t spacing {};
        int velocity {};
    };
    std::array<Echo, kKeys> echoes {};
};

struct AleaState {
    std::array<std::int16_t, kKeys> note {};
    std::array<std::int32_t, kKeys> delay {};
    Random random;
};

struct LengthState {
    std::array<std::int64_t, kKeys> started {};
    // Legato: the note sounding (-1 none), and when it ends once released.
    int sounding { -1 };
    int soundingChannel {};
    std::int64_t legatoEnd { -1 };
};

struct SequencerState {
    std::array<int, 16> keys {};
    int keyCount {};
    bool running {};
    bool playing {};
    double origin {};
    std::int64_t step {};
    std::int64_t played {};
    int position {};
    int direction { 1 };
    Random random;
};

struct RepeatState {
    struct Repeating {
        int channel {};
        int note {};
        int velocity {};
        std::int64_t next {};
        int hits {};
        bool active {};
    };
    std::array<Repeating, 16> notes {};
};

struct Stage {
    Pending pending;
    ArpState arp;
    ChordState chord;
    std::array<std::int16_t, kKeys> mapped {};
    EchoState echo;
    AleaState alea;
    LengthState length;
    SequencerState sequencer;
    RepeatState repeat;
    Random random;

    void reset() noexcept
    {
        pending.clear();
        arp = {};
        for (auto& made : chord.made)
            made.count = 0;
        chord.downward = false;
        mapped.fill(-2);
        for (auto& made : echo.echoes)
            made.repeats = 0;
        alea.note.fill(-2);
        alea.delay.fill(0);
        length.started.fill(-1);
        length.sounding = -1;
        length.legatoEnd = -1;
        sequencer = {};
        sequencer.direction = 1;
        repeat = {};
    }
};

// ---- The arpeggiator ---------------------------------------------------------------------------

// The notes a step chooses among, in order, octaves included.
std::size_t arpSequence(const ArpState& state, const int mode, const int octaves, std::array<ArpState::Held, 160>& into) noexcept
{
    std::array<ArpState::Held, 32> sorted {};
    const auto count = static_cast<std::size_t>(state.heldCount);
    std::copy_n(state.held.begin(), count, sorted.begin());
    if (mode == 4) // as played
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count),
                  [](const auto& a, const auto& b) { return a.order < b.order; });
    else
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count),
                  [](const auto& a, const auto& b) { return a.note < b.note; });
    std::size_t made = 0;
    for (int octave = 0; octave < octaves; ++octave)
        for (std::size_t index = 0; index < count && made < 128; ++index) {
            auto item = sorted[index];
            item.note += 12 * octave;
            if (item.note <= 127)
                into[made++] = item;
        }
    if (made == 0)
        return 0;
    switch (mode) {
    case 1: // down
        std::reverse(into.begin(), into.begin() + static_cast<std::ptrdiff_t>(made));
        return made;
    case 2: // up then down, the ends not twice
    case 3: { // down then up
        if (mode == 3)
            std::reverse(into.begin(), into.begin() + static_cast<std::ptrdiff_t>(made));
        const auto turn = made;
        for (std::size_t index = turn >= 2 ? turn - 2 : 0; index >= 1 && made < into.size() && turn >= 3; --index)
            into[made++] = into[index];
        return made;
    }
    case 7: { // converge: the lowest, the highest, the next lowest…
        std::array<ArpState::Held, 160> ordered {};
        std::size_t low = 0;
        auto high = made - 1;
        std::size_t at = 0;
        while (low <= high && at < made) {
            ordered[at++] = into[low];
            if (high != low && at < made)
                ordered[at++] = into[high];
            ++low;
            if (high == 0)
                break;
            --high;
        }
        std::copy_n(ordered.begin(), at, into.begin());
        return at;
    }
    case 8: { // diverge: from the middle outward
        std::array<ArpState::Held, 160> ordered {};
        const auto middle = static_cast<std::ptrdiff_t>((made - 1) / 2);
        std::size_t at = 0;
        for (std::ptrdiff_t spread = 0; at < made; ++spread) {
            const auto below = middle - spread;
            const auto above = middle + spread + 1;
            if (below >= 0)
                ordered[at++] = into[static_cast<std::size_t>(below)];
            if (above < static_cast<std::ptrdiff_t>(made) && at < made)
                ordered[at++] = into[static_cast<std::size_t>(above)];
        }
        std::copy_n(ordered.begin(), at, into.begin());
        return at;
    }
    default: return made; // up, as played, random, chord
    }
}

void arpeggiate(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block) noexcept
{
    auto& state = stage.arp;
    const auto& v = settings.values;
    const auto length = quartersOf(v[m::arpegeRate], 0.25);
    const auto mode = whole(v[m::arpegeMode], 0, 8);
    const auto octaves = whole(v[m::arpegeOctaves], 1, 4);
    const auto latch = v[m::arpegeLatch] >= 0.5f;
    const auto retrigger = v[m::arpegeRetrigger] >= 0.5f;
    const auto gate = std::clamp(v[m::arpegeGate], 0.05f, 2.0f);
    const auto swing = std::clamp(v[m::arpegeSwing], 0.0f, 0.75f);
    stage.pending.release(block.start, block.end(), out);
    // Latch let go of with no key down: the latched notes stop.
    if (!latch && state.keysDown <= 0 && state.heldCount > 0) {
        state.heldCount = 0;
        state.running = false;
    }
    const auto begin = [&](const std::int64_t at) {
        const auto quarter = block.quarterAt(at);
        state.origin = block.playing ? 0.0 : quarter;
        state.step = firstStepFrom(state.origin, length, quarter);
        state.played = 0;
        state.running = true;
        state.playing = block.playing;
    };
    keepOnTheBeat(state, block, length, swing);
    const auto stepAt = [&]() { return block.sampleAt(stepQuarter(state.origin, length, state.step, swing)); };
    std::size_t next = 0;
    while (true) {
        const auto inputAt = next < in.count ? block.start + in.items[next].offset : block.end();
        if (state.running && state.heldCount > 0) {
            const auto at = std::max(block.start, stepAt());
            if (at < block.end() && at < inputAt) {
                std::array<ArpState::Held, 160> sequence {};
                const auto count = arpSequence(state, mode, octaves, sequence);
                if (count > 0) {
                    const auto stepSamples = static_cast<std::int64_t>(length * block.samplesPerQuarter);
                    const auto off = at + std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(stepSamples) * gate));
                    const auto velocityOf = [&](const int played) {
                        const auto velocityMode = whole(v[m::arpegeVelocityMode], 0, 2);
                        if (velocityMode == 1)
                            return whole(v[m::arpegeVelocity], 1, 127);
                        return velocityMode == 2 ? (state.played % 4 == 0 ? std::min(127, played + 24) : std::max(1, played - 12)) : played;
                    };
                    if (mode == 6) {
                        // The whole chord, an octave further each step.
                        const auto octave = static_cast<int>(state.played % octaves);
                        for (int index = 0; index < state.heldCount; ++index) {
                            const auto& held = state.held[static_cast<std::size_t>(index)];
                            const auto note = held.note + 12 * octave;
                            if (note > 127)
                                continue;
                            out.add(noteOn(held.channel, note, velocityOf(held.velocity)), at, block.start);
                            stage.pending.add(off, noteOff(held.channel, note));
                        }
                    } else {
                        const auto pick = mode == 5 ? static_cast<std::size_t>(stage.random.next() * static_cast<float>(count)) % count
                                                    : static_cast<std::size_t>(state.played % static_cast<std::int64_t>(count));
                        const auto& chosen = sequence[pick];
                        out.add(noteOn(chosen.channel, chosen.note, velocityOf(chosen.velocity)), at, block.start);
                        stage.pending.add(off, noteOff(chosen.channel, chosen.note));
                    }
                }
                ++state.played;
                ++state.step;
                continue;
            }
        }
        if (next >= in.count)
            break;
        const auto& event = in.items[next++];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            state.heldCount = 0;
            state.keysDown = 0;
            state.running = false;
            stage.pending.clear();
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            if (latch && state.keysDown <= 0)
                state.heldCount = 0;
            ++state.keysDown;
            const auto wasEmpty = state.heldCount == 0;
            auto found = false;
            for (int index = 0; index < state.heldCount; ++index)
                found |= state.held[static_cast<std::size_t>(index)].note == event.data1;
            if (!found && state.heldCount < static_cast<int>(state.held.size()))
                state.held[static_cast<std::size_t>(state.heldCount++)] = { event.data1, event.data2, event.channel(), state.nextOrder++ };
            if (wasEmpty || !state.running)
                begin(at);
            else if (retrigger)
                state.played = 0;
            continue;
        }
        if (event.isNoteOff()) {
            state.keysDown = std::max(0, state.keysDown - 1);
            if (!latch) {
                auto kept = 0;
                for (int index = 0; index < state.heldCount; ++index)
                    if (state.held[static_cast<std::size_t>(index)].note != event.data1)
                        state.held[static_cast<std::size_t>(kept++)] = state.held[static_cast<std::size_t>(index)];
                state.heldCount = kept;
                if (kept == 0)
                    state.running = false;
            }
            continue;
        }
        out.add(event);
    }
}

// ---- Chords --------------------------------------------------------------------------------------

// The notes of a chord on `root`, low to high (at most 8).
int chordNotes(const MidiEffectSettings& settings, const int root, std::array<int, 8>& notes) noexcept
{
    const auto& v = settings.values;
    const auto type = whole(v[m::accordsChord], 0, m::customChord);
    const auto scale = whole(v[m::accordsScale], 0, 11);
    const auto key = whole(v[m::accordsKey], 0, 11);
    auto count = 0;
    if (scale > 0) {
        // Stacked thirds of the scale from the root's nearest degree up.
        auto base = root;
        while (!m::inScale(base, key, scale) && base > 0)
            --base;
        const auto size = m::chordShapes[static_cast<std::size_t>(type)].diatonicCount;
        if (type == 10) {
            notes[0] = base;
            notes[1] = base + 12;
            count = 2;
        } else {
            auto note = base;
            notes[count++] = note;
            while (count < size) {
                // Two scale steps up: the next third of the scale.
                for (int stepsUp = 0; stepsUp < 2; ++stepsUp) {
                    ++note;
                    while (!m::inScale(note, key, scale) && note < base + 36)
                        ++note;
                }
                notes[count++] = note;
            }
        }
    } else if (type == m::customChord) {
        notes[count++] = root;
        for (int index = 0; index < 6; ++index) {
            const auto interval = whole(v[static_cast<std::size_t>(m::accordsInterval1 + index)], -24, 24);
            if (interval != 0)
                notes[count++] = root + interval;
        }
    } else {
        const auto& shape = m::chordShapes[static_cast<std::size_t>(type)];
        for (int index = 0; index < shape.count; ++index)
            notes[count++] = root + shape.intervals[static_cast<std::size_t>(index)];
    }
    std::sort(notes.begin(), notes.begin() + count);
    // Inversions: the lowest up an octave, or the highest down.
    const auto inversion = whole(v[m::accordsInversion], -3, 3);
    for (int turn = 0; turn < std::abs(inversion) && count > 1; ++turn) {
        if (inversion > 0)
            notes[0] += 12;
        else
            notes[static_cast<std::size_t>(count - 1)] -= 12;
        std::sort(notes.begin(), notes.begin() + count);
    }
    // Open voicing: every second note above the lowest goes up an octave.
    if (v[m::accordsSpread] >= 0.5f && count > 2) {
        for (int index = 1; index < count; index += 2)
            notes[static_cast<std::size_t>(index)] += 12;
        std::sort(notes.begin(), notes.begin() + count);
    }
    auto kept = 0;
    for (int index = 0; index < count; ++index)
        if (notes[static_cast<std::size_t>(index)] >= 0 && notes[static_cast<std::size_t>(index)] <= 127
            && (kept == 0 || notes[static_cast<std::size_t>(kept - 1)] != notes[static_cast<std::size_t>(index)]))
            notes[static_cast<std::size_t>(kept++)] = notes[static_cast<std::size_t>(index)];
    return kept;
}

void chords(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block, const double rate) noexcept
{
    auto& state = stage.chord;
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto strum = static_cast<std::int64_t>(std::clamp(v[m::accordsStrum], 0.0f, 200.0f) * 0.001 * rate);
    const auto added = std::clamp(v[m::accordsVelocity], 0.0f, 1.0f);
    const auto direction = whole(v[m::accordsStrumMode], 0, 2);
    for (std::size_t index = 0; index < in.count; ++index) {
        const auto& event = in.items[index];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            for (auto& made : state.made)
                made.count = 0;
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            auto& made = state.made[keyOf(event.channel(), event.data1)];
            std::array<int, 8> notes {};
            const auto count = chordNotes(settings, event.data1, notes);
            made.count = count;
            made.tag = state.nextTag++;
            const auto down = direction == 1 || (direction == 2 && state.downward);
            if (direction == 2)
                state.downward = !state.downward;
            for (int index2 = 0; index2 < count; ++index2) {
                const auto note = notes[static_cast<std::size_t>(index2)];
                made.notes[static_cast<std::size_t>(index2)] = static_cast<std::int8_t>(note);
                const auto order = down ? count - 1 - index2 : index2;
                const auto velocity = note == event.data1 ? static_cast<int>(event.data2)
                                                          : std::max(1, static_cast<int>(std::lround(event.data2 * added)));
                const auto start = at + strum * order;
                if (start <= at)
                    out.add(noteOn(event.channel(), note, velocity), at, block.start);
                else
                    stage.pending.add(start, noteOn(event.channel(), note, velocity), made.tag);
            }
            continue;
        }
        if (event.isNoteOff()) {
            auto& made = state.made[keyOf(event.channel(), event.data1)];
            for (int index2 = 0; index2 < made.count; ++index2) {
                const auto note = made.notes[static_cast<std::size_t>(index2)];
                // One still to strum: it never starts.
                if (!stage.pending.dropStarts(made.tag, event.channel(), note))
                    out.add(noteOff(event.channel(), note), at, block.start);
            }
            made.count = 0;
            continue;
        }
        out.add(event);
    }
}

// ---- Scale ---------------------------------------------------------------------------------------

// A note into the scale: the nearest of it (ties up), above, below; -1 when
// it is to be removed.
int intoScale(const int note, const int key, const int scale, const int mode) noexcept
{
    if (m::inScale(note, key, scale))
        return note;
    if (mode == 3)
        return -1;
    for (int distance = 1; distance < 12; ++distance) {
        const auto up = note + distance;
        const auto down = note - distance;
        if (mode != 2 && m::inScale(up, key, scale))
            return up;
        if (mode != 1 && m::inScale(down, key, scale))
            return down;
    }
    return note;
}

// Some degrees of the scale up or down.
int byDegrees(int note, const int degrees, const int key, const int scale) noexcept
{
    for (int step = 0; step < std::abs(degrees); ++step) {
        do
            note += degrees > 0 ? 1 : -1;
        while (!m::inScale(note, key, scale) && note > -12 && note < 140);
    }
    return note;
}

void scaled(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out) noexcept
{
    const auto& v = settings.values;
    const auto key = whole(v[m::gammeKey], 0, 11);
    const auto scale = whole(v[m::gammeScale], 0, 11);
    const auto mode = whole(v[m::gammeMode], 0, 3);
    const auto transpose = whole(v[m::gammeTranspose], -24, 24);
    const auto degrees = whole(v[m::gammeDegrees], -7, 7);
    for (std::size_t index = 0; index < in.count; ++index) {
        const auto& event = in.items[index];
        if (event.isAllNotesOff()) {
            stage.mapped.fill(-2);
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            auto note = intoScale(event.data1, key, scale, mode);
            if (note >= 0)
                note = byDegrees(note, degrees, key, scale) + transpose;
            const auto kept = note >= 0 && note <= 127;
            stage.mapped[keyOf(event.channel(), event.data1)] = static_cast<std::int16_t>(kept ? note : -1);
            if (kept) {
                auto moved = event;
                moved.data1 = static_cast<std::uint8_t>(note);
                out.add(moved);
            }
            continue;
        }
        if (event.isNoteOff()) {
            auto& mapped = stage.mapped[keyOf(event.channel(), event.data1)];
            if (mapped >= 0) {
                auto moved = event;
                moved.data1 = static_cast<std::uint8_t>(mapped);
                out.add(moved);
            } else if (mapped == -2) {
                out.add(event);
            }
            mapped = -2;
            continue;
        }
        out.add(event);
    }
}

// ---- Note echo -----------------------------------------------------------------------------------

void echoed(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block, const double rate) noexcept
{
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto sync = v[m::echoNotesSync];
    const auto spacing = noteDivisionFor(sync) != nullptr
                             ? static_cast<std::int64_t>(quartersOf(sync, 0.5) * block.samplesPerQuarter)
                             : static_cast<std::int64_t>(std::clamp(v[m::echoNotesTime], 10.0f, 2'000.0f) * 0.001 * rate);
    const auto repeats = whole(v[m::echoNotesRepeats], 1, 16);
    const auto pitch = whole(v[m::echoNotesPitch], -12, 12);
    const auto decay = std::clamp(v[m::echoNotesDecay], 0.0f, 1.0f);
    for (std::size_t index = 0; index < in.count; ++index) {
        const auto& event = in.items[index];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            out.add(event);
            auto& echo = stage.echo.echoes[keyOf(event.channel(), event.data1)];
            // The echoes started, counted: as many end with the note, whatever
            // the decay is by then.
            auto started = 0;
            auto velocity = static_cast<float>(event.data2);
            for (int repeat = 1; repeat <= repeats; ++repeat) {
                velocity *= 1.0f - decay;
                const auto note = event.data1 + pitch * repeat;
                if (velocity < 1.0f || note < 0 || note > 127)
                    break;
                stage.pending.add(at + spacing * repeat, noteOn(event.channel(), note, static_cast<int>(std::lround(velocity))));
                started = repeat;
            }
            echo = { started, pitch, std::max<std::int64_t>(1, spacing), event.data2 };
            continue;
        }
        if (event.isNoteOff()) {
            out.add(event);
            const auto& echo = stage.echo.echoes[keyOf(event.channel(), event.data1)];
            for (int repeat = 1; repeat <= echo.repeats; ++repeat) {
                const auto note = event.data1 + echo.pitch * repeat;
                if (note < 0 || note > 127)
                    break;
                stage.pending.add(at + echo.spacing * repeat, noteOff(event.channel(), note));
            }
            continue;
        }
        out.add(event);
    }
}

// ---- Randomness ----------------------------------------------------------------------------------

void randomised(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block, const double rate) noexcept
{
    auto& state = stage.alea;
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto timing = std::clamp(v[m::aleaTiming], 0.0f, 50.0f) * 0.001 * rate;
    const auto spread = std::clamp(v[m::aleaVelocity], 0.0f, 1.0f);
    const auto chance = std::clamp(v[m::aleaChance], 0.0f, 1.0f);
    const auto pitchChance = std::clamp(v[m::aleaPitchChance], 0.0f, 1.0f);
    const auto range = whole(v[m::aleaPitchRange], 0, 12);
    for (std::size_t index = 0; index < in.count; ++index) {
        const auto& event = in.items[index];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            state.note.fill(-2);
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            const auto key = keyOf(event.channel(), event.data1);
            if (state.random.next() >= chance) {
                state.note[key] = -1;
                continue;
            }
            auto note = static_cast<int>(event.data1);
            if (range > 0 && state.random.next() < pitchChance) {
                const auto shift = 1 + static_cast<int>(state.random.next() * static_cast<float>(range));
                note += state.random.next() < 0.5f ? -shift : shift;
                if (note < 0 || note > 127)
                    note = event.data1;
            }
            const auto velocity = static_cast<int>(std::lround(event.data2 + (state.random.next() * 2.0f - 1.0f) * spread * 64.0f));
            const auto delay = static_cast<std::int32_t>(state.random.next() * static_cast<float>(timing));
            state.note[key] = static_cast<std::int16_t>(note);
            state.delay[key] = delay;
            if (delay <= 0)
                out.add(noteOn(event.channel(), note, velocity), at, block.start);
            else
                stage.pending.add(at + delay, noteOn(event.channel(), note, velocity));
            continue;
        }
        if (event.isNoteOff()) {
            const auto key = keyOf(event.channel(), event.data1);
            const auto note = state.note[key];
            if (note == -2)
                out.add(event);
            else if (note >= 0) {
                if (state.delay[key] <= 0)
                    out.add(noteOff(event.channel(), note), at, block.start);
                else
                    stage.pending.add(at + state.delay[key], noteOff(event.channel(), note));
            }
            state.note[key] = -2;
            continue;
        }
        out.add(event);
    }
}

// ---- Note length ---------------------------------------------------------------------------------

void lengthened(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block, const double rate) noexcept
{
    auto& state = stage.length;
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto mode = whole(v[m::dureeMode], 0, 3);
    const auto fixed = static_cast<std::int64_t>(std::clamp(v[m::dureeLength], 10.0f, 4'000.0f) * 0.001 * rate);
    const auto synced = static_cast<std::int64_t>(quartersOf(v[m::dureeSync], 0.5) * block.samplesPerQuarter);
    const auto factor = std::clamp(v[m::dureeFactor], 1.0f, 4.0f);
    // Legato: the note sounding ends when its time past its release runs out.
    const auto endLegatoBefore = [&](const std::int64_t at) {
        if (state.sounding >= 0 && state.legatoEnd >= 0 && state.legatoEnd <= at) {
            out.add(noteOff(state.soundingChannel, state.sounding), state.legatoEnd, block.start);
            state.sounding = -1;
            state.legatoEnd = -1;
        }
    };
    for (std::size_t index = 0; index < in.count; ++index) {
        const auto& event = in.items[index];
        const auto at = block.start + event.offset;
        endLegatoBefore(at);
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            state.started.fill(-1);
            state.sounding = -1;
            state.legatoEnd = -1;
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            if (mode == 3) {
                // The same key struck again while its end is still to come:
                // that end now, before it starts again (or it would never end).
                if (state.sounding == event.data1 && state.soundingChannel == event.channel() && state.legatoEnd >= 0)
                    out.add(noteOff(state.soundingChannel, state.sounding), at, block.start);
                // The one before ends as this one starts (after it: one
                // voice slides into the next).
                out.add(event);
                if (state.sounding >= 0 && !(state.sounding == event.data1 && state.soundingChannel == event.channel()))
                    out.add(noteOff(state.soundingChannel, state.sounding), at, block.start);
                state.sounding = event.data1;
                state.soundingChannel = event.channel();
                state.legatoEnd = -1;
                continue;
            }
            out.add(event);
            if (mode == 0 || mode == 1)
                stage.pending.add(at + std::max<std::int64_t>(1, mode == 0 ? fixed : synced), noteOff(event.channel(), event.data1));
            else
                state.started[keyOf(event.channel(), event.data1)] = at;
            continue;
        }
        if (event.isNoteOff()) {
            if (mode == 2) {
                auto& started = state.started[keyOf(event.channel(), event.data1)];
                if (started >= 0) {
                    const auto held = at - started;
                    stage.pending.add(started + std::max<std::int64_t>(held, static_cast<std::int64_t>(static_cast<double>(held) * factor)),
                                      noteOff(event.channel(), event.data1));
                } else {
                    out.add(event);
                }
                started = -1;
            } else if (mode == 3 && state.sounding == event.data1 && state.soundingChannel == event.channel()) {
                state.legatoEnd = at + fixed;
            } else if (auto& started = state.started[keyOf(event.channel(), event.data1)]; started >= 0) {
                // Begun in "as played × factor" before the mode changed: ends now.
                out.add(event);
                started = -1;
            } else if (mode != 3 && state.sounding == event.data1 && state.soundingChannel == event.channel()) {
                // Begun in legato before the mode changed: ends now.
                out.add(event);
                state.sounding = -1;
                state.legatoEnd = -1;
            }
            continue;
        }
        out.add(event);
    }
    endLegatoBefore(block.end() - 1);
}

// ---- Step sequencer ------------------------------------------------------------------------------

void sequenced(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block) noexcept
{
    auto& state = stage.sequencer;
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto withTransport = whole(v[m::sequenceurMode], 0, 1) == 1;
    const auto steps = whole(v[m::sequenceurSteps], 1, 16);
    const auto length = quartersOf(v[m::sequenceurRate], 0.25);
    const auto direction = whole(v[m::sequenceurDirection], 0, 3);
    const auto swing = std::clamp(v[m::sequenceurSwing], 0.0f, 0.75f);
    const auto gate = std::clamp(v[m::sequenceurGate], 0.05f, 1.0f);
    const auto root = whole(v[m::sequenceurRoot], 24, 96);
    // With the transport, it runs while it plays, on the bars' grid.
    if (withTransport) {
        if (block.playing && !state.running) {
            state.origin = 0.0;
            state.step = firstStepFrom(0.0, length, block.quarterAt(block.start));
            state.running = true;
            state.playing = true;
        } else if (!block.playing) {
            state.running = false;
        }
    }
    keepOnTheBeat(state, block, length, swing);
    const auto stepAt = [&]() { return block.sampleAt(stepQuarter(state.origin, length, state.step, swing)); };
    std::size_t next = 0;
    while (true) {
        const auto inputAt = next < in.count ? block.start + in.items[next].offset : block.end();
        if (state.running) {
            const auto at = std::max(block.start, stepAt());
            if (at < block.end() && at < inputAt) {
                // Which step: with the transport, where the bars say.
                int position = 0;
                if (withTransport && direction == 0) {
                    position = static_cast<int>(state.step % steps);
                } else if (direction == 3) {
                    position = static_cast<int>(state.random.next() * static_cast<float>(steps)) % steps;
                } else if (direction == 2 && steps > 1) {
                    const auto cycle = static_cast<int>(state.played % (2 * (steps - 1)));
                    position = cycle < steps ? cycle : 2 * (steps - 1) - cycle;
                } else {
                    position = static_cast<int>(state.played % steps);
                    if (direction == 1)
                        position = steps - 1 - position;
                }
                const auto value = [&](const int what) { return v[static_cast<std::size_t>(m::sequencerStep(position, what))]; };
                if (value(0) >= 0.5f) {
                    const auto base = withTransport ? root + (state.keyCount > 0 ? state.keys[static_cast<std::size_t>(state.keyCount - 1)] - 60 : 0)
                                                    : (state.keyCount > 0 ? state.keys[static_cast<std::size_t>(state.keyCount - 1)] : root);
                    const auto note = base + whole(value(1), -24, 24);
                    if (note >= 0 && note <= 127) {
                        const auto stepSamples = length * block.samplesPerQuarter;
                        const auto off = at + std::max<std::int64_t>(1, static_cast<std::int64_t>(stepSamples * gate * std::clamp(value(3), 0.1f, 1.0f)));
                        out.add(noteOn(0, note, whole(value(2), 1, 127)), at, block.start);
                        stage.pending.add(off, noteOff(0, note));
                    }
                }
                ++state.played;
                ++state.step;
                continue;
            }
        }
        if (next >= in.count)
            break;
        const auto& event = in.items[next++];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            state.keyCount = 0;
            if (!withTransport)
                state.running = false;
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            if (state.keyCount < static_cast<int>(state.keys.size()))
                state.keys[static_cast<std::size_t>(state.keyCount++)] = event.data1;
            if (!withTransport && !state.running) {
                const auto quarter = block.quarterAt(at);
                state.origin = block.playing ? 0.0 : quarter;
                state.step = firstStepFrom(state.origin, length, quarter);
                state.played = 0;
                state.running = true;
                state.playing = block.playing;
            }
            continue;
        }
        if (event.isNoteOff()) {
            auto kept = 0;
            for (int index = 0; index < state.keyCount; ++index)
                if (state.keys[static_cast<std::size_t>(index)] != event.data1)
                    state.keys[static_cast<std::size_t>(kept++)] = state.keys[static_cast<std::size_t>(index)];
            state.keyCount = kept;
            if (kept == 0 && !withTransport)
                state.running = false;
            continue;
        }
        out.add(event);
    }
}

// ---- Note repeat ---------------------------------------------------------------------------------

void repeated(Stage& stage, const MidiEffectSettings& settings, const Events& in, Events& out, const Block& block, const double rate) noexcept
{
    auto& state = stage.repeat;
    const auto& v = settings.values;
    stage.pending.release(block.start, block.end(), out);
    const auto sync = v[m::roulementSync];
    const auto interval = std::max<std::int64_t>(
        16, noteDivisionFor(sync) != nullptr ? static_cast<std::int64_t>(quartersOf(sync, 0.25) * block.samplesPerQuarter)
                                             : static_cast<std::int64_t>(rate / std::clamp(v[m::roulementRate], 1.0f, 40.0f)));
    const auto gate = std::clamp(v[m::roulementGate], 0.1f, 1.0f);
    const auto ramp = std::clamp(v[m::roulementRamp], -1.0f, 1.0f);
    const auto hit = [&](RepeatState::Repeating& note, const std::int64_t at) {
        const auto velocity = static_cast<int>(std::lround(note.velocity * std::clamp(1.0f + ramp * std::min(1.0f, static_cast<float>(note.hits) / 8.0f),
                                                                                       0.1f, 1.6f)));
        out.add(noteOn(note.channel, note.note, velocity), at, block.start);
        stage.pending.add(at + std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(interval) * gate)),
                          noteOff(note.channel, note.note));
        ++note.hits;
        note.next = at + interval;
    };
    std::size_t next = 0;
    while (true) {
        const auto inputAt = next < in.count ? block.start + in.items[next].offset : block.end();
        // The earliest repeat due before the next input.
        RepeatState::Repeating* due = nullptr;
        for (auto& note : state.notes)
            if (note.active && note.next < block.end() && note.next < inputAt && (due == nullptr || note.next < due->next))
                due = &note;
        if (due != nullptr) {
            hit(*due, std::max(block.start, due->next));
            continue;
        }
        if (next >= in.count)
            break;
        const auto& event = in.items[next++];
        const auto at = block.start + event.offset;
        if (event.isAllNotesOff()) {
            stage.pending.clear();
            for (auto& note : state.notes)
                note.active = false;
            out.add(event);
            continue;
        }
        if (event.isNoteOn()) {
            for (auto& note : state.notes)
                if (!note.active) {
                    note = { event.channel(), event.data1, event.data2, at, 0, true };
                    hit(note, at);
                    break;
                }
            continue;
        }
        if (event.isNoteOff()) {
            for (auto& note : state.notes)
                if (note.active && note.note == event.data1 && note.channel == event.channel())
                    note.active = false;
            continue;
        }
        out.add(event);
    }
}

} // namespace

// ---- The chain ---------------------------------------------------------------------------------

struct MidiEffectChain::State {
    double sampleRate { 48'000.0 };
    std::int64_t now {};
    double freeQuarters {};
    std::array<Stage, midiEffectMost> stages {};
    std::array<NativeDspNodeKind, midiEffectMost> kinds {};
    std::size_t kindCount {};
    Events a;
    Events b;
    // How many times each note is on at the chain's output: a note-off goes
    // out only when the last of them ends.
    std::array<std::uint8_t, kKeys> sounding {};

    void resetStages() noexcept
    {
        for (auto& stage : stages)
            stage.reset();
    }
};

MidiEffectChain::MidiEffectChain() : state_(std::make_unique<State>())
{
    state_->resetStages();
}

MidiEffectChain::~MidiEffectChain() = default;

void MidiEffectChain::prepare(const double sampleRate)
{
    state_->sampleRate = std::max(1'000.0, sampleRate);
    reset();
}

void MidiEffectChain::reset() noexcept
{
    state_->now = 0;
    state_->freeQuarters = 0.0;
    state_->resetStages();
    state_->sounding.fill(0);
    state_->kindCount = 0;
}

std::size_t MidiEffectChain::process(const MidiEffectEvent* in, const std::size_t count, MidiEffectEvent* out, const std::size_t room,
                                     const std::uint32_t frames, const MidiEffectClock& clock, const MidiEffectsPlan* plan) noexcept
{
    auto& s = *state_;
    std::size_t written = 0;
    const auto send = [&](const Event& event) {
        if (written < room)
            out[written++] = event;
    };
    Block block;
    block.start = s.now;
    block.frames = frames;
    block.samplesPerQuarter = s.sampleRate * 60.0 / std::clamp(clock.beatsPerMinute, 20.0, 400.0);
    block.playing = clock.playing;
    block.firstQuarter = clock.playing ? clock.quarterNotes : s.freeQuarters;
    s.now += frames;
    s.freeQuarters = block.firstQuarter + static_cast<double>(frames) / block.samplesPerQuarter;

    const auto effects = plan != nullptr ? std::min(plan->count, midiEffectMost) : std::size_t {};
    // Another chain (a card added, removed, moved): what it sounds stops,
    // and the new one starts clean.
    auto same = effects == s.kindCount;
    for (std::size_t index = 0; same && index < effects; ++index)
        same = s.kinds[index] == plan->effects[index].kind;
    if (!same) {
        for (std::size_t key = 0; key < kKeys; ++key)
            if (s.sounding[key] > 0)
                send(noteOff(static_cast<int>(key / 128), static_cast<int>(key % 128)));
        s.sounding.fill(0);
        s.resetStages();
        s.kindCount = effects;
        for (std::size_t index = 0; index < effects; ++index)
            s.kinds[index] = plan->effects[index].kind;
    }
    if (effects == 0) {
        // Passed through, but counted: a note held while the first effect is
        // put in is then let go with the others when the chain changes.
        for (std::size_t index = 0; index < count; ++index) {
            const auto& event = in[index];
            if (event.isAllNotesOff()) {
                s.sounding.fill(0);
            } else if (event.isNoteOn()) {
                auto& sounding = s.sounding[keyOf(event.channel(), event.data1)];
                sounding = static_cast<std::uint8_t>(std::min(255, sounding + 1));
            } else if (event.isNoteOff()) {
                if (auto& sounding = s.sounding[keyOf(event.channel(), event.data1)]; sounding > 0)
                    --sounding;
            }
            send(event);
        }
        return written;
    }
    s.a.clear();
    for (std::size_t index = 0; index < count; ++index)
        s.a.add(in[index]);
    for (std::size_t index = 0; index < effects; ++index) {
        auto& stage = s.stages[index];
        const auto& settings = plan->effects[index];
        s.b.clear();
        switch (settings.kind) {
        case NativeDspNodeKind::arpege: arpeggiate(stage, settings, s.a, s.b, block); break;
        case NativeDspNodeKind::accords: chords(stage, settings, s.a, s.b, block, s.sampleRate); break;
        case NativeDspNodeKind::gamme: scaled(stage, settings, s.a, s.b); break;
        case NativeDspNodeKind::echoNotes: echoed(stage, settings, s.a, s.b, block, s.sampleRate); break;
        case NativeDspNodeKind::alea: randomised(stage, settings, s.a, s.b, block, s.sampleRate); break;
        case NativeDspNodeKind::duree: lengthened(stage, settings, s.a, s.b, block, s.sampleRate); break;
        case NativeDspNodeKind::sequenceur: sequenced(stage, settings, s.a, s.b, block); break;
        case NativeDspNodeKind::roulement: repeated(stage, settings, s.a, s.b, block, s.sampleRate); break;
        default:
            for (std::size_t item = 0; item < s.a.count; ++item)
                s.b.add(s.a.items[item]);
            break;
        }
        s.b.sort();
        for (std::size_t item = 0; item < s.b.count; ++item)
            s.b.items[item].offset = std::min(s.b.items[item].offset, frames > 0 ? frames - 1 : 0u);
        std::swap(s.a, s.b);
    }
    // Overlapping notes of one pitch: the voice is started again each time,
    // let go only by the last end.
    for (std::size_t index = 0; index < s.a.count; ++index) {
        const auto& event = s.a.items[index];
        if (event.isAllNotesOff()) {
            s.sounding.fill(0);
            send(event);
        } else if (event.isNoteOn()) {
            auto& sounding = s.sounding[keyOf(event.channel(), event.data1)];
            sounding = static_cast<std::uint8_t>(std::min(255, sounding + 1));
            send(event);
        } else if (event.isNoteOff()) {
            auto& sounding = s.sounding[keyOf(event.channel(), event.data1)];
            if (sounding == 0)
                continue;
            if (--sounding == 0)
                send(event);
        } else {
            send(event);
        }
    }
    return written;
}

MidiEffectSettings midiEffectSettingsOf(const EffectDescriptor& effect, const NativeDspNodeParameters& parameters) noexcept
{
    MidiEffectSettings settings;
    settings.kind = effect.kind;
    for (std::size_t index = 0; index < effect.parameters.size() && index < settings.values.size(); ++index) {
        const auto& parameter = effect.parameters[index];
        settings.values[index] = std::clamp(effectValue(parameters, parameter), parameter.minimum, parameter.maximum);
    }
    return settings;
}

MidiEffectSettings legacyArpeggiatorSettings(const double stepBeats, const double octaves) noexcept
{
    MidiEffectSettings settings;
    settings.kind = NativeDspNodeKind::arpege;
    if (const auto* effect = effectForKind(NativeDspNodeKind::arpege))
        for (std::size_t index = 0; index < effect->parameters.size() && index < settings.values.size(); ++index)
            settings.values[index] = effect->parameters[index].defaultValue;
    // The note value nearest its step.
    auto best = 0;
    for (std::size_t index = 0; index < noteDivisions.size(); ++index)
        if (best == 0 || std::abs(std::log2(noteDivisions[index].beats / std::max(1.0e-3, stepBeats)))
                             < std::abs(std::log2(noteDivisions[static_cast<std::size_t>(best - 1)].beats / std::max(1.0e-3, stepBeats))))
            best = static_cast<int>(index) + 1;
    settings.values[m::arpegeRate] = static_cast<float>(best);
    settings.values[m::arpegeOctaves] = static_cast<float>(std::clamp(std::lround(octaves), 1l, 4l));
    return settings;
}

int midiChordNotes(const MidiEffectSettings& settings, const int root, std::array<int, 8>& notes) noexcept
{
    return chordNotes(settings, root, notes);
}

int midiArpeggioNotes(const MidiEffectSettings& settings, const int* held, const int count, std::array<int, 160>& notes) noexcept
{
    ArpState state;
    for (int index = 0; index < count && index < static_cast<int>(state.held.size()); ++index)
        state.held[static_cast<std::size_t>(state.heldCount++)] = { held[index], 100, 0, static_cast<std::uint32_t>(index) };
    std::array<ArpState::Held, 160> sequence {};
    const auto made = arpSequence(state, whole(settings.values[m::arpegeMode], 0, 8), whole(settings.values[m::arpegeOctaves], 1, 4), sequence);
    for (std::size_t index = 0; index < made; ++index)
        notes[index] = sequence[index].note;
    return static_cast<int>(made);
}

int midiScaleNote(const int note, const int key, const int scale, const int mode, const int degrees, const int transpose) noexcept
{
    const auto into = intoScale(note, key, scale, mode);
    return into < 0 ? -1 : byDegrees(into, degrees, key, scale) + transpose;
}

bool isMidiEffectType(const std::string_view typeKey) noexcept
{
    if (typeKey == "midi-arp")
        return true;
    const auto* effect = effectForType(typeKey);
    return effect != nullptr && effect->midi;
}

} // namespace cydaw::engine
