// The engine tests of Résonateurs, Corps, Peigne
// (ResonanceEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); resonanceEffectTests() runs them all.

// One of the three, its defaults set, then `values` by binding suffix.
std::unique_ptr<cydaw::engine::AudioProcessor> resonanceEffect(const cydaw::engine::NativeDspNodeKind kind,
                                                               const std::vector<std::pair<std::string_view, float>>& values)
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = kind;
    const auto* effect = effectForKind(kind);
    if (effect == nullptr)
        return {};
    applyEffectDefaults(*effect, node.parameters);
    for (const auto& [suffix, value] : values) {
        const auto* parameter = effectParameterForSuffix(suffix);
        expect(parameter != nullptr, "a Resonance test names its values by their binding suffix");
        if (parameter != nullptr)
            setEffectValue(node.parameters, *parameter, value);
    }
    auto processor = makeEffectProcessor(node);
    if (processor != nullptr)
        processor->prepare({ 48'000.0, 256, 2 });
    return processor;
}

// Runs a signal through (left = right) in blocks of 256; the right side into `right`.
std::vector<float> resonanceRun(cydaw::engine::AudioProcessor& processor, const std::vector<float>& signal,
                                std::vector<float>* right = nullptr, const cydaw::engine::ProcessContext& context = {})
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    std::vector<float> left;
    left.reserve(signal.size());
    for (std::size_t at = 0; at < signal.size(); at += 256) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(256, signal.size() - at));
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

std::vector<float> resonanceNoise(const std::size_t frames, const float level, std::uint32_t seed = 11u)
{
    std::vector<float> noise(frames);
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * level);
    }
    return noise;
}

// The mean power (dB) between two times (s).
double resonanceEnergyDb(const std::vector<float>& x, const double from, const double to)
{
    auto sum = 0.0;
    const auto a = std::min(x.size(), static_cast<std::size_t>(from * 48'000.0));
    const auto b = std::min(x.size(), static_cast<std::size_t>(to * 48'000.0));
    for (auto i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(sum / static_cast<double>(std::max<std::size_t>(1, b - a)) + 1.0e-30);
}

// The power (dB) at a frequency between two times, averaged over Hann
// windows of `size` samples overlapping by half.
double resonanceToneDb(const std::vector<float>& x, const double hz, const double from, const double to, const std::size_t size = 8'192)
{
    const auto a = std::min(x.size(), static_cast<std::size_t>(from * 48'000.0));
    const auto b = std::min(x.size(), static_cast<std::size_t>(to * 48'000.0));
    const auto w = 2.0 * std::numbers::pi * hz / 48'000.0;
    auto total = 0.0;
    auto segments = 0;
    for (auto start = a; start + size <= b; start += size / 2) {
        auto re = 0.0;
        auto im = 0.0;
        auto windows = 0.0;
        for (std::size_t n = 0; n < size; ++n) {
            const auto window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(size - 1));
            const auto v = static_cast<double>(x[start + n]) * window;
            re += v * std::cos(w * static_cast<double>(n));
            im -= v * std::sin(w * static_cast<double>(n));
            windows += window;
        }
        total += (re * re + im * im) / (windows * windows);
        ++segments;
    }
    return 10.0 * std::log10(total / static_cast<double>(std::max(1, segments)) + 1.0e-30);
}

double resonancePeak(const std::vector<float>& x)
{
    auto peak = 0.0;
    for (const auto v : x)
        peak = std::isfinite(v) ? std::max(peak, static_cast<double>(std::abs(v))) : 1.0e9;
    return peak;
}

// A block processed allocates nothing; no latency is claimed.
void resonanceProcessesInPlace(cydaw::engine::AudioProcessor& processor, const char* what)
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    const auto input = in.view(256);
    for (std::uint32_t frame = 0; frame < 256; ++frame) {
        input.channel(0)[frame] = std::sin(static_cast<float>(frame) * 0.05f) * 0.5f;
        input.channel(1)[frame] = input.channel(0)[frame];
    }
    ProcessContext context;
    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    processor.process(input, out.view(256), context);
    allocation_probe::enabled.store(false, std::memory_order_release);
    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0, std::string(what) + " processes without allocating");
    expect(processor.latencySamples() == 0 && processor.maximumLatencySamples() == 0, std::string(what) + " adds no latency");
}

void resonateursRingAtTheirNotes()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    // A burst of noise, then nothing: the strings ring on, at the root (A2,
    // 110 Hz) and at its intervals (a fifth, an octave, a tenth, a twelfth).
    auto burst = resonanceNoise(48'000 * 3, 0.5f);
    std::fill(burst.begin() + 2'400, burst.end(), 0.0f);
    auto strings = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f },
                                                     { "resonateurs.decay", 0.8f }, { "resonateurs.width", 0.0f } });
    expect(strings != nullptr, "Résonateurs is made");
    if (strings == nullptr)
        return;
    resonanceProcessesInPlace(*strings, "Résonateurs");
    strings->reset();
    const auto rung = resonanceRun(*strings, burst);
    for (const auto note : { 45.0, 52.0, 57.0, 61.0, 64.0 }) {
        const auto hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        const auto at = resonanceToneDb(rung, hz, 0.2, 1.5, 32'768);
        const auto below = resonanceToneDb(rung, hz * std::pow(2.0, -1.0 / 12.0), 0.2, 1.5, 32'768);
        const auto above = resonanceToneDb(rung, hz * std::pow(2.0, 1.0 / 12.0), 0.2, 1.5, 32'768);
        expect(at > below + 20.0 && at > above + 20.0, "Résonateurs rings at the root and at each interval");
    }
    expect(resonanceToneDb(rung, 137.0, 0.2, 1.5) < resonanceToneDb(rung, 110.0, 0.2, 1.5) - 30.0,
           "Résonateurs does not ring between its notes");
    // Its readings: every voice rang.
    std::array<float, 8> readings {};
    const auto read = strings->readings(readings.data(), readings.size());
    expect(read == static_cast<std::size_t>(resonance::resonateursReadingCount), "Résonateurs shows its five voices");

    // Decay 1 still rings after 2 s; decay 0 dies at once.
    auto longest = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 1.0f } });
    const auto longOut = resonanceRun(*longest, burst);
    expect(resonanceEnergyDb(longOut, 2.0, 2.5) > resonanceEnergyDb(longOut, 0.06, 0.3) - 30.0, "Résonateurs at decay 1 still rings after 2 s");
    auto shortest = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 0.0f } });
    const auto shortOut = resonanceRun(*shortest, burst);
    expect(resonanceEnergyDb(shortOut, 0.6, 1.0) < resonanceEnergyDb(shortOut, 0.0, 0.05) - 80.0, "Résonateurs at decay 0 dies fast");
    // Plucked rings shorter than strings at the same decay.
    auto plucked = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 0.6f },
                                                     { "resonateurs.mode", 1.0f } });
    auto bowed = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 0.6f } });
    const auto pluckedOut = resonanceRun(*plucked, burst);
    const auto bowedOut = resonanceRun(*bowed, burst);
    expect(resonanceEnergyDb(pluckedOut, 1.0, 1.5) - resonanceEnergyDb(pluckedOut, 0.06, 0.3)
               < resonanceEnergyDb(bowedOut, 1.0, 1.5) - resonanceEnergyDb(bowedOut, 0.06, 0.3) - 10.0,
           "Résonateurs' plucked strings ring shorter");
    // Metal: its partials stretched off the harmonic series (the octave of
    // the root is no longer where a string puts it).
    auto metal = resonanceEffect(K::resonateurs, { { "resonateurs.note", 45.0f }, { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 0.8f },
                                                   { "resonateurs.mode", 2.0f }, { "resonateurs.gain2", 0.0f }, { "resonateurs.gain3", 0.0f },
                                                   { "resonateurs.gain4", 0.0f }, { "resonateurs.gain5", 0.0f }, { "resonateurs.width", 0.0f } });
    const auto metalOut = resonanceRun(*metal, burst);
    const auto rootDb = resonanceToneDb(metalOut, 110.0, 0.2, 1.5, 32'768);
    expect(rootDb > resonanceToneDb(metalOut, 104.0, 0.2, 1.5, 32'768) + 20.0, "Résonateurs' metal keeps its root in tune");
    expect(resonanceToneDb(metalOut, 330.0, 0.2, 1.5, 32'768) < rootDb - 25.0, "Résonateurs' metal is inharmonic");

    // Loud, every voice full, the longest decay, a sine on the root: bounded.
    std::vector<float> loud(48'000 * 2);
    for (std::size_t i = 0; i < loud.size(); ++i)
        loud[i] = static_cast<float>(4.0 * std::sin(2.0 * std::numbers::pi * 110.0 * static_cast<double>(i) / 48'000.0));
    const auto noise = resonanceNoise(loud.size(), 4.0f, 5u);
    for (std::size_t i = 0; i < loud.size(); ++i)
        loud[i] += noise[i];
    for (int mode = 0; mode < 3; ++mode) {
        const std::array<std::array<float, 6>, 4> corners { { { 1.0f, 1.0f, 96.0f, 24.0f, 1.0f, 1.0f },
                                                               { 1.0f, 0.0f, 24.0f, -24.0f, -1.0f, 0.0f },
                                                               { 0.0f, 1.0f, 24.0f, 24.0f, 1.0f, 1.0f },
                                                               { 1.0f, 0.5f, 45.0f, 0.0f, 0.0f, 0.5f } } };
        for (const auto& corner : corners) {
            auto effect = resonanceEffect(K::resonateurs, { { "resonateurs.mode", static_cast<float>(mode) }, { "resonateurs.mix", 1.0f },
                                                            { "resonateurs.decay", corner[0] }, { "resonateurs.colour", corner[1] },
                                                            { "resonateurs.note", corner[2] }, { "resonateurs.interval2", corner[3] },
                                                            { "resonateurs.interval3", corner[3] }, { "resonateurs.interval4", -corner[3] },
                                                            { "resonateurs.interval5", corner[3] * 0.5f }, { "resonateurs.tone", corner[4] },
                                                            { "resonateurs.width", corner[5] }, { "resonateurs.gain1", 1.0f },
                                                            { "resonateurs.gain2", 1.0f }, { "resonateurs.gain3", 1.0f },
                                                            { "resonateurs.gain4", 1.0f }, { "resonateurs.gain5", 1.0f } });
            std::vector<float> right;
            const auto out = resonanceRun(*effect, loud, &right);
            expect(resonancePeak(out) <= 1.0 && resonancePeak(right) <= 1.0, "Résonateurs stays finite and bounded at its extremes");
        }
    }
    // At defaults on noise the level stays near the sound's; at no mix, the
    // sound as it was, nothing delayed.
    auto wet = resonanceEffect(K::resonateurs, { { "resonateurs.mix", 1.0f } });
    const auto quiet = resonanceNoise(48'000 * 2, 0.25f, 3u);
    const auto wetOut = resonanceRun(*wet, quiet);
    const auto level = resonanceEnergyDb(wetOut, 0.5, 2.0) - resonanceEnergyDb(quiet, 0.5, 2.0);
    expect(level > -12.0 && level < 3.0, "Résonateurs' level stays near the sound's");
    auto dry = resonanceEffect(K::resonateurs, { { "resonateurs.mix", 0.0f } });
    expect(resonanceRun(*dry, quiet) == quiet, "Résonateurs at no mix leaves the sound");
}

void corpsRingsAsItsObject()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    std::vector<float> impulse(48'000 * 2, 0.0f);
    impulse[0] = 1.0f;
    // A string at 220 Hz: its harmonics, nothing between them.
    auto string = resonanceEffect(K::corps, { { "corps.type", 2.0f }, { "corps.tune", 220.0f }, { "corps.mix", 1.0f }, { "corps.decay", 0.6f },
                                              { "corps.width", 0.0f } });
    expect(string != nullptr, "Corps is made");
    if (string == nullptr)
        return;
    resonanceProcessesInPlace(*string, "Corps");
    string->reset();
    const auto rung = resonanceRun(*string, impulse);
    const auto between = std::max(resonanceToneDb(rung, 330.0, 0.0, 1.5), resonanceToneDb(rung, 550.0, 0.0, 1.5));
    for (const auto hz : { 220.0, 440.0, 660.0 })
        expect(resonanceToneDb(rung, hz, 0.0, 1.5) > between + 20.0, "Corps' string rings at its harmonics");
    std::array<float, 32> readings {};
    const auto read = string->readings(readings.data(), readings.size());
    expect(read == static_cast<std::size_t>(resonance::corpsReadingCount) && readings[0] > 0.0f, "Corps shows its modes ringing");
    // A tube closed at one end: its odd harmonics only.
    auto tube = resonanceEffect(K::corps, { { "corps.type", 6.0f }, { "corps.tune", 220.0f }, { "corps.mix", 1.0f }, { "corps.decay", 0.6f },
                                            { "corps.width", 0.0f } });
    const auto blown = resonanceRun(*tube, impulse);
    const auto octave = resonanceToneDb(blown, 440.0, 0.0, 1.5);
    expect(resonanceToneDb(blown, 220.0, 0.0, 1.5) > octave + 20.0 && resonanceToneDb(blown, 660.0, 0.0, 1.5) > octave + 20.0,
           "Corps' tube rings at its odd harmonics only");
    // The membrane's first overtone where a drum's is (1.59 times its note).
    auto skin = resonanceEffect(K::corps, { { "corps.type", 3.0f }, { "corps.tune", 200.0f }, { "corps.mix", 1.0f }, { "corps.decay", 0.6f },
                                            { "corps.position", 0.5f }, { "corps.width", 0.0f } });
    const auto struck = resonanceRun(*skin, impulse);
    expect(resonanceToneDb(struck, 318.66, 0.0, 1.5) > resonanceToneDb(struck, 400.0, 0.0, 1.5) + 20.0,
           "Corps' membrane rings at its Bessel ratios");
    // Decay 1 rings much longer than 0.2.
    auto longest = resonanceEffect(K::corps, { { "corps.decay", 1.0f }, { "corps.mix", 1.0f } });
    auto shorter = resonanceEffect(K::corps, { { "corps.decay", 0.2f }, { "corps.mix", 1.0f } });
    const auto longOut = resonanceRun(*longest, impulse);
    const auto shortOut = resonanceRun(*shorter, impulse);
    expect(resonanceEnergyDb(longOut, 1.0, 1.5) - resonanceEnergyDb(longOut, 0.0, 0.1)
               > resonanceEnergyDb(shortOut, 1.0, 1.5) - resonanceEnergyDb(shortOut, 0.0, 0.1) + 30.0,
           "Corps at decay 1 rings longer than at 0.2");
    // Metal keeps its upper modes ringing; wood lets them go.
    auto metal = resonanceEffect(K::corps, { { "corps.type", 0.0f }, { "corps.material", 1.0f }, { "corps.mix", 1.0f }, { "corps.width", 0.0f } });
    auto wood = resonanceEffect(K::corps, { { "corps.type", 0.0f }, { "corps.material", 0.0f }, { "corps.mix", 1.0f }, { "corps.width", 0.0f } });
    const auto metalOut = resonanceRun(*metal, impulse);
    const auto woodOut = resonanceRun(*wood, impulse);
    const auto upper = 220.0 * 5.404;
    expect(resonanceToneDb(metalOut, upper, 0.2, 1.5) - resonanceToneDb(metalOut, 220.0, 0.2, 1.5)
               > resonanceToneDb(woodOut, upper, 0.2, 1.5) - resonanceToneDb(woodOut, 220.0, 0.2, 1.5) + 10.0,
           "Corps' metal keeps its upper modes longer than wood");
    // Every object at its extremes, loud: bounded.
    const auto loud = resonanceNoise(48'000, 4.0f, 9u);
    for (int type = 0; type < resonance::corpsTypeCount; ++type)
        for (const auto extreme : { 0.0f, 1.0f }) {
            auto effect = resonanceEffect(K::corps, { { "corps.type", static_cast<float>(type) }, { "corps.mix", 1.0f },
                                                      { "corps.tune", extreme > 0.5f ? 2'000.0f : 40.0f }, { "corps.decay", 1.0f },
                                                      { "corps.material", extreme }, { "corps.inharmonic", extreme * 2.0f - 1.0f },
                                                      { "corps.position", extreme }, { "corps.width", 1.0f - extreme },
                                                      { "corps.lowCut", extreme > 0.5f ? 2'000.0f : 20.0f } });
            std::vector<float> right;
            const auto out = resonanceRun(*effect, loud, &right);
            expect(resonancePeak(out) <= 1.0 && resonancePeak(right) <= 1.0, "Corps stays finite and bounded at its extremes");
        }
    // At defaults on noise near the sound's level; at no mix, the sound.
    auto wet = resonanceEffect(K::corps, { { "corps.mix", 1.0f } });
    const auto quiet = resonanceNoise(48'000 * 2, 0.25f, 3u);
    const auto level = resonanceEnergyDb(resonanceRun(*wet, quiet), 0.5, 2.0) - resonanceEnergyDb(quiet, 0.5, 2.0);
    expect(level > -12.0 && level < 3.0, "Corps' level stays near the sound's");
    auto dry = resonanceEffect(K::corps, { { "corps.mix", 0.0f } });
    expect(resonanceRun(*dry, quiet) == quiet, "Corps at no mix leaves the sound");
}

void peigneCombsInTune()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const auto noise = resonanceNoise(48'000 * 3, 0.25f, 21u);
    // Feedback 0.9 at 200 Hz: peaks at 200, 400, 600, troughs between.
    auto comb = resonanceEffect(K::peigne, { { "peigne.frequency", 200.0f }, { "peigne.feedback", 0.9f }, { "peigne.mix", 1.0f } });
    expect(comb != nullptr, "Peigne is made");
    if (comb == nullptr)
        return;
    resonanceProcessesInPlace(*comb, "Peigne");
    comb->reset();
    const auto combed = resonanceRun(*comb, noise);
    const auto trough = std::max(resonanceToneDb(combed, 300.0, 0.3, 3.0), resonanceToneDb(combed, 500.0, 0.3, 3.0));
    for (const auto hz : { 200.0, 400.0, 600.0 })
        expect(resonanceToneDb(combed, hz, 0.3, 3.0) > trough + 15.0, "Peigne's feedback peaks at the frequency and its multiples");
    // Negative: the odd half-multiples, an octave down.
    auto negative = resonanceEffect(K::peigne, { { "peigne.frequency", 200.0f }, { "peigne.feedback", -0.9f }, { "peigne.mix", 1.0f } });
    const auto low = resonanceRun(*negative, noise);
    const auto evens = std::max(resonanceToneDb(low, 200.0, 0.3, 3.0), resonanceToneDb(low, 400.0, 0.3, 3.0));
    for (const auto hz : { 100.0, 300.0, 500.0 })
        expect(resonanceToneDb(low, hz, 0.3, 3.0) > evens + 15.0, "Peigne's negative feedback peaks at the odd half-multiples");
    // Feedforward: notches at the odd half-multiples.
    auto forward = resonanceEffect(K::peigne, { { "peigne.frequency", 200.0f }, { "peigne.feedback", 0.9f }, { "peigne.mix", 1.0f },
                                                { "peigne.mode", 1.0f } });
    const auto notched = resonanceRun(*forward, noise);
    const auto kept = std::min(resonanceToneDb(notched, 200.0, 0.3, 3.0), resonanceToneDb(notched, 400.0, 0.3, 3.0));
    for (const auto hz : { 100.0, 300.0, 500.0 })
        expect(resonanceToneDb(notched, hz, 0.3, 3.0) < kept - 15.0, "Peigne's feedforward notches between its multiples");
    // The spread: the right channel's comb higher than the left's.
    auto spread = resonanceEffect(K::peigne, { { "peigne.frequency", 400.0f }, { "peigne.feedback", 0.95f }, { "peigne.mix", 1.0f },
                                               { "peigne.spread", 1.0f } });
    std::vector<float> spreadRight;
    const auto spreadLeft = resonanceRun(*spread, noise, &spreadRight);
    expect(resonanceToneDb(spreadLeft, 400.0 / 1.03, 0.3, 3.0) > resonanceToneDb(spreadRight, 400.0 / 1.03, 0.3, 3.0) + 6.0
               && resonanceToneDb(spreadRight, 400.0 * 1.03, 0.3, 3.0) > resonanceToneDb(spreadLeft, 400.0 * 1.03, 0.3, 3.0) + 6.0,
           "Peigne's spread tunes its sides apart");
    // Synced to the song: a quarter note into a 1/4 cycle, the LFO at its
    // top (an octave up at full depth).
    auto synced = resonanceEffect(K::peigne, { { "peigne.frequency", 200.0f }, { "peigne.lfoDepth", 1.0f }, { "peigne.sync", 9.0f } });
    ProcessContext playing;
    playing.transport.state = PlaybackState::playing;
    playing.transport.quarterNotePosition = 0.25;
    playing.transport.beatsPerMinute = 120.0;
    resonanceRun(*synced, std::vector<float>(16, 0.1f), nullptr, playing);
    std::array<float, 4> readings {};
    const auto read = synced->readings(readings.data(), readings.size());
    expect(read == static_cast<std::size_t>(resonance::peigneReadingCount) && readings[0] > 390.0f && readings[0] < 401.0f,
           "Peigne's synced LFO follows the song");
    // Its drawn response is what it plays: peaks at the multiples.
    std::array<float, resonance::peigneParameterCount> values {};
    for (std::size_t index = 0; index < values.size(); ++index)
        values[index] = effectForKind(K::peigne)->parameters[index].defaultValue;
    values[resonance::peigneFeedback] = 0.9f;
    expect(resonance::peigneResponseDb(values.data(), 400.0, 48'000.0) > resonance::peigneResponseDb(values.data(), 300.0, 48'000.0) + 20.0,
           "Peigne's drawn response peaks where it rings");
    // ±0.98, every extreme, loud: bounded.
    const auto loud = resonanceNoise(48'000, 4.0f, 13u);
    for (int mode = 0; mode < 2; ++mode)
        for (const auto feedback : { -0.98f, 0.98f })
            for (const auto frequency : { 20.0f, 5'000.0f }) {
                auto effect = resonanceEffect(K::peigne, { { "peigne.mode", static_cast<float>(mode) }, { "peigne.feedback", feedback },
                                                           { "peigne.frequency", frequency }, { "peigne.mix", 1.0f },
                                                           { "peigne.damping", frequency > 100.0f ? 20'000.0f : 500.0f },
                                                           { "peigne.spread", 1.0f }, { "peigne.lfoDepth", 1.0f }, { "peigne.lfoRate", 10.0f } });
                std::vector<float> right;
                const auto out = resonanceRun(*effect, loud, &right);
                expect(resonancePeak(out) <= 1.0 && resonancePeak(right) <= 1.0, "Peigne stays finite and bounded at its extremes");
            }
    // At defaults on noise near the sound's level; at no mix, the sound.
    auto wet = resonanceEffect(K::peigne, { { "peigne.mix", 1.0f } });
    const auto level = resonanceEnergyDb(resonanceRun(*wet, noise), 0.5, 3.0) - resonanceEnergyDb(noise, 0.5, 3.0);
    expect(level > -6.0 && level < 3.0, "Peigne's level stays near the sound's");
    auto dry = resonanceEffect(K::peigne, { { "peigne.mix", 0.0f } });
    expect(resonanceRun(*dry, noise) == noise, "Peigne at no mix leaves the sound");
}

// On a sound like music's (nearly pink noise), each at its defaults, all
// wet: about as loud as it came in.
void resonanceKeepsTheLevel()
{
    using namespace cydaw::engine;
    const auto white = resonanceNoise(48'000 * 3, 0.25f, 5u);
    std::vector<float> pink(white.size());
    std::array<double, 3> poles {};
    for (std::size_t i = 0; i < white.size(); ++i) {
        const auto w = static_cast<double>(white[i]);
        poles[0] = 0.99765 * poles[0] + w * 0.0990460;
        poles[1] = 0.96300 * poles[1] + w * 0.2965164;
        poles[2] = 0.57000 * poles[2] + w * 1.0526913;
        pink[i] = static_cast<float>((poles[0] + poles[1] + poles[2] + w * 0.1848) * 0.25);
    }
    const std::array<std::pair<NativeDspNodeKind, std::string_view>, 3> effects { { { NativeDspNodeKind::resonateurs, "resonateurs.mix" },
                                                                                    { NativeDspNodeKind::corps, "corps.mix" },
                                                                                    { NativeDspNodeKind::peigne, "peigne.mix" } } };
    for (const auto& [kind, mix] : effects) {
        auto effect = resonanceEffect(kind, { { mix, 1.0f } });
        const auto level = resonanceEnergyDb(resonanceRun(*effect, pink), 0.5, 3.0) - resonanceEnergyDb(pink, 0.5, 3.0);
        expect(level > -6.0 && level < 3.0, "a Resonance effect keeps music's level at its defaults");
    }
}

// A mono or a 4-channel bus: the first channels worked, the rest passed through.
void resonanceTakesAnyWidth()
{
    using namespace cydaw::engine;
    for (const auto kind : { NativeDspNodeKind::resonateurs, NativeDspNodeKind::corps, NativeDspNodeKind::peigne })
        for (const std::uint32_t channels : { 1u, 4u }) {
            NativeDspNode node;
            node.id = 1;
            node.kind = kind;
            applyEffectDefaults(*effectForKind(kind), node.parameters);
            auto processor = makeEffectProcessor(node);
            if (processor == nullptr) {
                expect(false, "a Resonance effect is made");
                continue;
            }
            processor->prepare({ 48'000.0, 256, channels });
            AudioBuffer in(channels, 256);
            AudioBuffer out(channels, 256);
            const auto noise = resonanceNoise(256, 0.5f, 17u);
            auto finite = true;
            auto passed = true;
            ProcessContext context;
            for (int block = 0; block < 40; ++block) {
                const auto input = in.view(256);
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    for (std::uint32_t frame = 0; frame < 256; ++frame)
                        input.channel(channel)[frame] = noise[frame] * (channel == 3 ? 0.5f : 1.0f);
                const auto output = out.view(256);
                processor->process(input, output, context);
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    for (std::uint32_t frame = 0; frame < 256; ++frame) {
                        finite = finite && std::isfinite(output.channel(channel)[frame]);
                        if (channel >= 2)
                            passed = passed && output.channel(channel)[frame] == input.channel(channel)[frame];
                    }
            }
            expect(finite, "a Resonance effect works on one channel and on four");
            expect(passed, "a Resonance effect passes the channels above two through");
        }
}

// After a sound and its tail, silence comes out exactly (the states let go
// before they sink into denormals), and the next sound wakes it.
void resonanceRestsInSilence()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const std::array<std::pair<K, std::vector<std::pair<std::string_view, float>>>, 3> effects { {
        { K::resonateurs, { { "resonateurs.mix", 1.0f }, { "resonateurs.decay", 0.2f } } },
        { K::corps, { { "corps.mix", 1.0f }, { "corps.decay", 0.2f } } },
        { K::peigne, { { "peigne.mix", 1.0f }, { "peigne.feedback", 0.6f } } },
    } };
    auto signal = resonanceNoise(48'000 * 4, 0.3f, 23u);
    std::fill(signal.begin() + 4'800, signal.begin() + 48'000 * 3, 0.0f);
    for (const auto& [kind, values] : effects) {
        auto effect = resonanceEffect(kind, values);
        const auto out = resonanceRun(*effect, signal);
        auto silent = true;
        for (std::size_t i = 48'000 * 2 + 24'000; i < 48'000 * 3; ++i)
            silent = silent && out[i] == 0.0f;
        expect(silent, "a Resonance effect falls to exact silence after its tail");
        expect(resonanceEnergyDb(out, 3.05, 3.5) > -40.0, "a Resonance effect wakes when sound comes back");
    }
}

void resonanceEffectTests()
{
    resonateursRingAtTheirNotes();
    corpsRingsAsItsObject();
    peigneCombsInTune();
    resonanceKeepsTheLevel();
    resonanceRestsInSilence();
    resonanceTakesAnyWidth();
}
