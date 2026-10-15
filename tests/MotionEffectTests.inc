// The engine tests of Pompe, Décaleur, Anneau, Rotor
// (MotionEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); motionEffectTests() runs them all.

// One of the motion effects, its defaults set, then `values` by binding
// suffix ("pompe.depth").
std::unique_ptr<cydaw::engine::AudioProcessor> motionEffect(const cydaw::engine::NativeDspNodeKind kind,
                                                            const std::vector<std::pair<std::string_view, float>>& values,
                                                            cydaw::engine::NativeDspNode* kept = nullptr)
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = kind;
    const auto* effect = effectForKind(kind);
    if (effect == nullptr)
        return {};
    applyEffectDefaults(*effect, node.parameters);
    for (const auto& [suffix, value] : values)
        if (const auto* parameter = effectParameterForSuffix(suffix))
            setEffectValue(node.parameters, *parameter, value);
    auto processor = makeEffectProcessor(node);
    if (processor != nullptr)
        processor->prepare({ 48'000.0, 256, 2 });
    if (kept != nullptr)
        *kept = node;
    return processor;
}

// A stereo signal through, in blocks of 256, the transport as given (its
// position moved on block by block while it plays); the right side into
// `right`.
std::vector<float> motionRun(cydaw::engine::AudioProcessor& processor, const std::vector<float>& left, const std::vector<float>& rightIn,
                             std::vector<float>* right = nullptr, cydaw::engine::TransportSnapshot transport = {})
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    std::vector<float> result;
    result.reserve(left.size());
    if (right != nullptr)
        right->clear();
    for (std::size_t at = 0; at < left.size(); at += 256) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(256, left.size() - at));
        const auto input = in.view(count);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            input.channel(0)[frame] = left[at + frame];
            input.channel(1)[frame] = rightIn.empty() ? left[at + frame] : rightIn[at + frame];
        }
        ProcessContext context;
        context.transport = transport;
        if (transport.state != PlaybackState::stopped)
            context.transport.quarterNotePosition = transport.quarterNotePosition
                                                    + static_cast<double>(at) / 48'000.0 * transport.beatsPerMinute / 60.0;
        const auto output = out.view(count);
        processor.process(input, output, context);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            result.push_back(output.channel(0)[frame]);
            if (right != nullptr)
                right->push_back(output.channel(1)[frame]);
        }
    }
    return result;
}

std::vector<float> motionNoise(const std::size_t count, const float amplitude, std::uint32_t seed)
{
    std::vector<float> noise(count);
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * amplitude);
    }
    return noise;
}

bool motionBounded(const std::vector<float>& signal, const double limit)
{
    for (const auto x : signal)
        if (!std::isfinite(x) || std::abs(x) > limit)
            return false;
    return true;
}

// Nothing allocated in a block of process().
bool motionProcessesWithoutAllocating(cydaw::engine::AudioProcessor& processor)
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    const auto input = in.view(256);
    for (std::uint32_t frame = 0; frame < 256; ++frame)
        input.channel(0)[frame] = input.channel(1)[frame] = 0.1f * static_cast<float>(std::sin(0.05 * frame));
    ProcessContext context;
    context.transport.state = PlaybackState::playing;
    allocation_probe::count.store(0);
    allocation_probe::enabled.store(true);
    processor.process(input, out.view(256), context);
    allocation_probe::enabled.store(false);
    return allocation_probe::count.load() == 0;
}

float motionReading(const cydaw::engine::AudioProcessor& processor, const int index)
{
    std::array<float, 8> readings {};
    const auto count = processor.readings(readings.data(), readings.size());
    return index >= 0 && static_cast<std::size_t>(index) < count ? readings[static_cast<std::size_t>(index)] : -1.0e9f;
}

void pompeDucksOnTheBeat()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::motion;
    constexpr auto rate = 48'000.0;
    const std::vector<float> level(static_cast<std::size_t>(4.0 * rate), 0.5f);
    TransportSnapshot playing;
    playing.state = PlaybackState::playing;
    playing.beatsPerMinute = 120.0;
    const auto gainDb = [&](const std::vector<float>& out, const double seconds) {
        const auto at = std::min(out.size() - 1, static_cast<std::size_t>(seconds * rate));
        return 20.0 * std::log10(std::max(1.0e-9, std::abs(static_cast<double>(out[at])) / 0.5));
    };
    // 120 bpm, a quarter note a cycle, full depth: down on every beat, back
    // up before the next.
    auto pump = motionEffect(NativeDspNodeKind::pompe, { { "pompe.sync", 9.0f }, { "pompe.depth", 1.0f }, { "pompe.shape", 0.0f } });
    expect(pump != nullptr && pump->latencySamples() == 0, "Pompe is made, without latency");
    std::vector<float> right;
    const auto out = motionRun(*pump, level, {}, &right, playing);
    auto down = true;
    auto up = true;
    for (int beat = 0; beat < 7; ++beat) {
        const auto start = beat * 0.5;
        down = down && gainDb(out, start + 0.01) < -30.0 && gainDb(right, start + 0.01) < -30.0;
        up = up && gainDb(out, start + 0.495) > -0.5;
    }
    expect(down, "Pompe is down more than 30 dB at each beat's start");
    expect(up, "Pompe is back near unity just before the next beat");
    expect(motionBounded(out, 0.5 + 1.0e-6), "Pompe never raises the sound");
    // Its readings: the gain and the place in the cycle.
    const auto phase = motionReading(*pump, pompeReadingPhase);
    const auto gain = motionReading(*pump, pompeReadingGain);
    expect(phase >= 0.0f && phase < 1.0f && std::abs(gain - out.back() / 0.5f) < 1.0e-3f, "Pompe shows its gain and phase");
    // The stereo offset delays the right side's curve.
    auto offset = motionEffect(NativeDspNodeKind::pompe, { { "pompe.sync", 9.0f }, { "pompe.depth", 1.0f }, { "pompe.stereoOffset", 0.5f } });
    std::vector<float> offsetRight;
    const auto offsetLeft = motionRun(*offset, level, {}, &offsetRight, playing);
    expect(gainDb(offsetLeft, 1.01) < -30.0 && gainDb(offsetRight, 1.01) > -1.0 && gainDb(offsetRight, 1.26) < -30.0,
           "Pompe's stereo offset ducks the right side later");
    // No depth: the sound as it was.
    auto none = motionEffect(NativeDspNodeKind::pompe, { { "pompe.depth", 0.0f } });
    const auto flat = motionRun(*none, level, {}, nullptr, playing);
    auto unity = true;
    for (const auto x : flat)
        unity = unity && std::abs(x - 0.5f) < 1.0e-6f;
    expect(unity, "Pompe at no depth leaves the sound");
    // Free: two cycles a second at a rate of 2 Hz with the transport stopped.
    auto free = motionEffect(NativeDspNodeKind::pompe, { { "pompe.sync", 0.0f }, { "pompe.rate", 2.0f }, { "pompe.depth", 1.0f } });
    const auto freeOut = motionRun(*free, level, {});
    expect(gainDb(freeOut, 1.01) < -30.0 && gainDb(freeOut, 1.26) > -6.0 && gainDb(freeOut, 1.51) < -30.0, "Pompe runs free at its rate");
    expect(motionProcessesWithoutAllocating(*pump), "Pompe allocates nothing as it plays");
}

void pompeFollowsItsKey()
{
    using namespace cydaw::engine;
    constexpr auto rate = 48'000.0;
    auto pump = motionEffect(NativeDspNodeKind::pompe, { { "pompe.trigger", 1.0f }, { "pompe.sync", 9.0f }, { "pompe.depth", 1.0f },
                                                       { "pompe.threshold", -24.0f } });
    auto* buses = dynamic_cast<MultiBusAudioProcessor*>(pump.get());
    expect(buses != nullptr, "Pompe takes a sidechain");
    if (buses == nullptr)
        return;
    // A held level on the main input; two short bursts on the key, at 0.3 s and 1.1 s.
    const auto total = static_cast<std::size_t>(2.0 * rate);
    std::vector<float> key(total, 0.0f);
    for (const auto start : { 0.3, 1.1 })
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.03 * rate); ++i)
            key[static_cast<std::size_t>(start * rate) + i] = 0.8f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 80.0 * i / rate));
    AudioBuffer main(2, 256);
    AudioBuffer side(2, 256);
    AudioBuffer out(2, 256);
    std::vector<float> gains;
    for (std::size_t at = 0; at < total; at += 256) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(256, total - at));
        const auto mainView = main.view(count);
        const auto sideView = side.view(count);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            mainView.channel(0)[frame] = mainView.channel(1)[frame] = 0.5f;
            sideView.channel(0)[frame] = sideView.channel(1)[frame] = key[at + frame];
        }
        const std::array<AudioInputBusView, 2> inputs { { { 0, mainView }, { effectSidechainPortId, sideView } } };
        const auto output = out.view(count);
        buses->processAudioBuses(inputs, output, ProcessContext {});
        for (std::uint32_t frame = 0; frame < count; ++frame)
            gains.push_back(output.channel(0)[frame] / 0.5f);
    }
    const auto db = [&](const double seconds) {
        return 20.0 * std::log10(std::max(1.0e-9, static_cast<double>(gains[static_cast<std::size_t>(seconds * rate)])));
    };
    expect(db(0.25) > -0.1, "Pompe keyed waits at unity before a hit");
    expect(db(0.312) < -30.0 && db(1.112) < -30.0, "each hit on the key restarts Pompe's cycle");
    expect(db(0.79) > -1.0 && db(1.0) > -0.1 && db(1.6) > -0.1, "after its one cycle Pompe stays at the top");
}

void pompeDrawsAndStaysBounded()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::motion;
    constexpr auto rate = 48'000.0;
    const std::vector<float> level(static_cast<std::size_t>(1.5 * rate), 0.5f);
    TransportSnapshot playing;
    playing.state = PlaybackState::playing;
    playing.beatsPerMinute = 120.0;
    // A drawn ramp, no hold, no bend, no smoothing: the gain is the place
    // in the beat.
    std::vector<std::pair<std::string_view, float>> drawn { { "pompe.shape", 7.0f },   { "pompe.depth", 1.0f },     { "pompe.hold", 0.0f },
                                                            { "pompe.curve", 0.0f },   { "pompe.smoothing", 0.0f }, { "pompe.sync", 9.0f } };
    static constexpr std::array<const char*, 16> points { "pompe.point1",  "pompe.point2",  "pompe.point3",  "pompe.point4",
                                                          "pompe.point5",  "pompe.point6",  "pompe.point7",  "pompe.point8",
                                                          "pompe.point9",  "pompe.point10", "pompe.point11", "pompe.point12",
                                                          "pompe.point13", "pompe.point14", "pompe.point15", "pompe.point16" };
    for (std::size_t i = 0; i < points.size(); ++i)
        drawn.emplace_back(points[i], i == 7 ? 0.2f : static_cast<float>(i) / 15.0f);
    auto pump = motionEffect(NativeDspNodeKind::pompe, drawn);
    const auto out = motionRun(*pump, level, {}, nullptr, playing);
    const auto at = [&](const double place) { return out[static_cast<std::size_t>((0.5 + place * 0.5) * rate)] / 0.5f; };
    expect(std::abs(at(0.25) - 0.25f) < 0.01f && std::abs(at(0.8) - 0.8f) < 0.01f && std::abs(at(7.0 / 15.0) - 0.2f) < 0.01f,
           "Pompe's drawn shape follows its points");
    // The display's curve is the processor's.
    std::array<float, pompeParameterCount> values {};
    const auto* effect = effectForKind(NativeDspNodeKind::pompe);
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = effect->parameters[i].defaultValue;
    values[pompeShape] = 7.0f;
    values[pompeDepth] = 1.0f;
    values[pompeHold] = 0.0f;
    values[pompeCurve] = 0.0f;
    for (std::size_t i = 0; i < 16; ++i)
        values[static_cast<std::size_t>(pompePoint1) + i] = i == 7 ? 0.2f : static_cast<float>(i) / 15.0f;
    expect(std::abs(pompeGainAt(values.data(), 0.25) - at(0.25)) < 0.01, "Pompe's display draws what it plays");
    // Every shape at its extremes: finite, never above the sound.
    const auto noise = motionNoise(static_cast<std::size_t>(0.6 * rate), 0.5f, 31u);
    auto bounded = true;
    for (int shape = 0; shape < pompeShapeCount; ++shape)
        for (const auto extreme : { 0.0f, 1.0f }) {
            auto edge = motionEffect(NativeDspNodeKind::pompe,
                                     { { "pompe.shape", static_cast<float>(shape) },
                                       { "pompe.depth", 1.0f },
                                       { "pompe.curve", extreme > 0.5f ? 1.0f : -1.0f },
                                       { "pompe.hold", 0.5f * extreme },
                                       { "pompe.phase", extreme },
                                       { "pompe.stereoOffset", 0.5f * extreme },
                                       { "pompe.rate", extreme > 0.5f ? 20.0f : 0.1f },
                                       { "pompe.sync", extreme > 0.5f ? 1.0f : 0.0f },
                                       { "pompe.smoothing", 20.0f * extreme },
                                       { "pompe.trigger", extreme },
                                       { "pompe.threshold", -60.0f * extreme } });
            std::vector<float> right;
            const auto left = motionRun(*edge, noise, {}, &right, playing);
            bounded = bounded && motionBounded(left, 0.5 + 1.0e-5) && motionBounded(right, 0.5 + 1.0e-5);
        }
    expect(bounded, "every Pompe at its extremes stays finite and under the sound");
}

void decaleurShiftsEveryFrequency()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::motion;
    constexpr auto rate = 48'000.0;
    const auto tone = sine(1'000.0, 0.5, 1.5);
    const auto from = static_cast<std::size_t>(0.3 * rate);
    const auto to = static_cast<std::size_t>(1.3 * rate);
    // Up 100 Hz: 1100 Hz, the lower sideband and the original far under it.
    auto up = motionEffect(NativeDspNodeKind::decaleur, { { "decaleur.shift", 100.0f }, { "decaleur.mode", 0.0f } });
    expect(up != nullptr && up->latencySamples() == 0, "Décaleur is made, without latency");
    const auto upOut = motionRun(*up, tone, {});
    const auto at1100 = toneLevelDb(upOut, 1'100.0, from, to);
    expect(at1100 > -7.0 && at1100 < -5.0, "Décaleur keeps the level as it shifts");
    expect(toneLevelDb(upOut, 900.0, from, to) < at1100 - 30.0 && toneLevelDb(upOut, 1'000.0, from, to) < at1100 - 30.0,
           "Décaleur up 100 Hz: 1000 Hz becomes 1100 Hz, one sideband only");
    expect(std::abs(motionReading(*up, decaleurReadingShift) - 100.0f) < 1.0e-3f, "Décaleur shows its shift");
    // Down 300 Hz.
    auto down = motionEffect(NativeDspNodeKind::decaleur, { { "decaleur.shift", -300.0f } });
    const auto downOut = motionRun(*down, tone, {});
    const auto at700 = toneLevelDb(downOut, 700.0, from, to);
    expect(at700 > -7.0 && toneLevelDb(downOut, 1'300.0, from, to) < at700 - 30.0, "Décaleur down 300 Hz: 1000 Hz becomes 700 Hz");
    // Opposite sides: the left up, the right down.
    auto opposite = motionEffect(NativeDspNodeKind::decaleur, { { "decaleur.shift", 100.0f }, { "decaleur.mode", 2.0f } });
    std::vector<float> right;
    const auto left = motionRun(*opposite, tone, {}, &right);
    expect(toneLevelDb(left, 1'100.0, from, to) > toneLevelDb(left, 900.0, from, to) + 30.0
               && toneLevelDb(right, 900.0, from, to) > toneLevelDb(right, 1'100.0, from, to) + 30.0,
           "Décaleur's opposite sides: the left at 1100 Hz, the right at 900 Hz");
    // Both sidebands: 900 and 1100 Hz alike, no 1000 Hz.
    auto both = motionEffect(NativeDspNodeKind::decaleur, { { "decaleur.shift", 100.0f }, { "decaleur.mode", 1.0f } });
    const auto bothOut = motionRun(*both, tone, {});
    const auto a = toneLevelDb(bothOut, 900.0, from, to);
    const auto b = toneLevelDb(bothOut, 1'100.0, from, to);
    expect(std::abs(a - b) < 1.0 && toneLevelDb(bothOut, 1'000.0, from, to) < b - 30.0, "Décaleur's both sidebands: 900 and 1100 Hz");
    // The spread parts the sides: the left lower, the right higher.
    auto spread = motionEffect(NativeDspNodeKind::decaleur, { { "decaleur.shift", 100.0f }, { "decaleur.spread", 50.0f } });
    std::vector<float> spreadRight;
    const auto spreadLeft = motionRun(*spread, tone, {}, &spreadRight);
    expect(toneLevelDb(spreadLeft, 1'050.0, from, to) > -7.0 && toneLevelDb(spreadRight, 1'150.0, from, to) > -7.0,
           "Décaleur's spread: the left 50 Hz lower, the right 50 Hz higher");
    expect(motionProcessesWithoutAllocating(*up), "Décaleur allocates nothing as it plays");
    // Fed back at its most, at the extremes: finite, bounded.
    const auto noise = motionNoise(static_cast<std::size_t>(rate), 0.5f, 77u);
    auto bounded = true;
    for (int mode = 0; mode < decaleurModeCount; ++mode)
        for (const auto shift : { -2'000.0f, 0.0f, 2'000.0f })
            for (const auto delay : { 0.0f, 1'000.0f }) {
                auto edge = motionEffect(NativeDspNodeKind::decaleur,
                                         { { "decaleur.shift", shift },
                                           { "decaleur.mode", static_cast<float>(mode) },
                                           { "decaleur.feedback", 0.95f },
                                           { "decaleur.delay", delay },
                                           { "decaleur.spread", 200.0f },
                                           { "decaleur.lfoDepth", 1'000.0f },
                                           { "decaleur.lfoRate", 10.0f },
                                           { "decaleur.lfoShape", 3.0f } });
                std::vector<float> edgeRight;
                const auto edgeLeft = motionRun(*edge, noise, {}, &edgeRight);
                bounded = bounded && motionBounded(edgeLeft, 8.0) && motionBounded(edgeRight, 8.0);
            }
    expect(bounded, "Décaleur fed back at 0.95 stays finite and bounded");
}

void anneauRingsAndKeeps()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::motion;
    constexpr auto rate = 48'000.0;
    const auto tone = sine(1'000.0, 0.5, 1.2);
    const auto from = static_cast<std::size_t>(0.2 * rate);
    const auto to = static_cast<std::size_t>(1.2 * rate);
    // Ring, 220 Hz sine: 780 and 1220 Hz, no 1000 Hz.
    auto ring = motionEffect(NativeDspNodeKind::anneau, { { "anneau.frequency", 220.0f }, { "anneau.mix", 1.0f }, { "anneau.mode", 0.0f } });
    expect(ring != nullptr && ring->latencySamples() == 0, "Anneau is made, without latency");
    const auto ringOut = motionRun(*ring, tone, {});
    const auto low = toneLevelDb(ringOut, 780.0, from, to);
    const auto high = toneLevelDb(ringOut, 1'220.0, from, to);
    expect(low > -10.0 && high > -10.0 && std::abs(low - high) < 1.0, "Anneau ring: 1000 Hz times 220 Hz gives 780 and 1220 Hz");
    expect(toneLevelDb(ringOut, 1'000.0, from, to) < high - 30.0, "Anneau ring takes the original away");
    expect(std::abs(motionReading(*ring, anneauReadingLeftHz) - 220.0f) < 1.0e-3f, "Anneau shows its carrier's frequency");
    // Amplitude: the original kept, its sidebands under it.
    auto amplitude = motionEffect(NativeDspNodeKind::anneau, { { "anneau.frequency", 220.0f }, { "anneau.mix", 1.0f }, { "anneau.mode", 1.0f } });
    const auto amplitudeOut = motionRun(*amplitude, tone, {});
    const auto kept = toneLevelDb(amplitudeOut, 1'000.0, from, to);
    expect(kept > -13.0 && kept > toneLevelDb(amplitudeOut, 1'220.0, from, to) + 3.0, "Anneau's amplitude mode keeps 1000 Hz");
    // At its defaults (half mix) and fully wet, its level near the sound's.
    const auto noise = motionNoise(static_cast<std::size_t>(rate), 0.3f, 5u);
    const auto powerDb = [](const std::vector<float>& x) {
        auto sum = 0.0;
        for (std::size_t i = x.size() / 4; i < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(sum + 1.0e-30);
    };
    auto levelled = true;
    for (int shape = 0; shape < anneauShapeCount; ++shape)
        for (int mode = 0; mode < anneauModeCount; ++mode)
            for (const auto mix : { 0.5f, 1.0f }) {
                auto each = motionEffect(NativeDspNodeKind::anneau, { { "anneau.shape", static_cast<float>(shape) },
                                                                      { "anneau.mode", static_cast<float>(mode) },
                                                                      { "anneau.mix", mix } });
                const auto difference = powerDb(motionRun(*each, noise, {})) - powerDb(noise);
                levelled = levelled && std::abs(difference) < 3.0;
            }
    expect(levelled, "Anneau keeps about the sound's level, any carrier, any mode");
    // No mix: the sound untouched.
    auto dry = motionEffect(NativeDspNodeKind::anneau, { { "anneau.mix", 0.0f }, { "anneau.drive", 24.0f } });
    expect(motionRun(*dry, tone, {}) == tone, "Anneau at no mix leaves the sound");
    // The LFO in octaves: one octave up at its top.
    auto swept = motionEffect(NativeDspNodeKind::anneau, { { "anneau.frequency", 200.0f }, { "anneau.lfoDepth", 1.0f },
                                                          { "anneau.lfoShape", 3.0f }, { "anneau.lfoRate", 0.5f } });
    motionRun(*swept, sine(1'000.0, 0.5, 0.2), {});
    expect(std::abs(motionReading(*swept, anneauReadingLeftHz) - 400.0f) < 1.0f, "Anneau's LFO swings its carrier by octaves");
    // A square carrier band-limited: its 13th harmonic (40.3 kHz) hardly
    // folds back (to 7.7 kHz) under its first.
    auto square = motionEffect(NativeDspNodeKind::anneau, { { "anneau.frequency", 3'100.0f }, { "anneau.shape", 2.0f }, { "anneau.mix", 1.0f } });
    const auto squareOut = motionRun(*square, sine(40.0, 0.5, 1.0), {});
    expect(toneLevelDb(squareOut, 3'140.0, 4'800, 48'000) > toneLevelDb(squareOut, 7'740.0, 4'800, 48'000) + 35.0,
           "Anneau's square carrier is band-limited");
    expect(motionProcessesWithoutAllocating(*ring), "Anneau allocates nothing as it plays");
    // Every shape and mode at the extremes: finite and bounded.
    const auto loud = motionNoise(static_cast<std::size_t>(0.5 * rate), 0.7f, 9u);
    auto bounded = true;
    for (int shape = 0; shape < anneauShapeCount; ++shape)
        for (int mode = 0; mode < anneauModeCount; ++mode)
            for (const auto extreme : { 0.0f, 1.0f }) {
                auto edge = motionEffect(NativeDspNodeKind::anneau,
                                         { { "anneau.shape", static_cast<float>(shape) },
                                           { "anneau.mode", static_cast<float>(mode) },
                                           { "anneau.frequency", extreme > 0.5f ? 5'000.0f : 1.0f },
                                           { "anneau.lfoDepth", 2.0f },
                                           { "anneau.lfoRate", 10.0f },
                                           { "anneau.lfoShape", 3.0f },
                                           { "anneau.drive", 24.0f * extreme },
                                           { "anneau.lowCut", extreme > 0.5f ? 2'000.0f : 20.0f },
                                           { "anneau.stereo", extreme },
                                           { "anneau.sync", extreme > 0.5f ? 1.0f : 0.0f } });
                std::vector<float> right;
                const auto left = motionRun(*edge, loud, {}, &right);
                bounded = bounded && motionBounded(left, 4.0) && motionBounded(right, 4.0);
            }
    expect(bounded, "every Anneau at its extremes stays finite and bounded");
}

void rotorTurnsWithInertia()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::motion;
    constexpr auto rate = 48'000.0;
    // The envelope of a 2 kHz tone (the horn's) in 1 ms steps; how many
    // times it swings round its middle a second.
    const auto swingsPerSecond = [&](const std::vector<float>& out, const double fromSeconds, const double toSeconds) {
        std::vector<double> envelope;
        for (auto at = static_cast<std::size_t>(fromSeconds * rate); at + 48 <= static_cast<std::size_t>(toSeconds * rate); at += 48) {
            auto sum = 0.0;
            for (std::size_t i = 0; i < 48; ++i)
                sum += static_cast<double>(out[at + i]) * out[at + i];
            envelope.push_back(std::sqrt(sum / 48.0));
        }
        auto mean = 0.0;
        for (const auto e : envelope)
            mean += e;
        mean /= static_cast<double>(std::max<std::size_t>(1, envelope.size()));
        auto lowest = 1.0e9;
        auto highest = 0.0;
        for (const auto e : envelope) {
            lowest = std::min(lowest, e);
            highest = std::max(highest, e);
        }
        // Rising crossings of the middle, with a little hysteresis.
        auto crossings = 0;
        auto below = true;
        const auto margin = (highest - lowest) * 0.1;
        for (const auto e : envelope) {
            if (below && e > mean + margin) {
                ++crossings;
                below = false;
            } else if (!below && e < mean - margin) {
                below = true;
            }
        }
        return std::pair { static_cast<double>(crossings) / (toSeconds - fromSeconds), 20.0 * std::log10(highest / std::max(1.0e-9, lowest)) };
    };
    const auto tone = sine(2'000.0, 0.5, 3.0);
    // Fast, up to speed: the envelope swings at the horn's rate.
    auto fast = motionEffect(NativeDspNodeKind::rotor, { { "rotor.speed", 2.0f }, { "rotor.fast", 6.8f }, { "rotor.room", 0.0f } });
    expect(fast != nullptr && fast->latencySamples() == 48, "Rotor reports its flight to the microphones as latency");
    const auto fastOut = motionRun(*fast, tone, {});
    const auto [fastRate, fastDepth] = swingsPerSecond(fastOut, 0.5, 3.0);
    expect(fastRate > 6.8 * 0.85 && fastRate < 6.8 * 1.15 && fastDepth > 3.0, "Rotor fast swings a 2 kHz tone at the horn's rate");
    expect(std::abs(motionReading(*fast, rotorReadingHornHz) - 6.8f) < 1.0e-3f
               && std::abs(motionReading(*fast, rotorReadingDrumHz) - 6.8f * 0.85f) < 1.0e-3f,
           "Rotor shows its rotors' speeds");
    // Stopped: no swing at all.
    auto stopped = motionEffect(NativeDspNodeKind::rotor, { { "rotor.speed", 0.0f }, { "rotor.room", 0.0f } });
    const auto stoppedOut = motionRun(*stopped, tone, {});
    expect(swingsPerSecond(stoppedOut, 0.5, 3.0).second < 0.2, "Rotor stopped does not move the sound");
    // Slow, then fast: two thirds of the way after the acceleration's time
    // (its time constant), nearly there after three; the drum slower. Then
    // stopped: braked to a halt.
    NativeDspNode node;
    auto rotor = motionEffect(NativeDspNodeKind::rotor, { { "rotor.speed", 1.0f }, { "rotor.acceleration", 1.2f }, { "rotor.room", 0.0f } }, &node);
    const auto seconds = [](const double s) { return sine(2'000.0, 0.5, s); };
    motionRun(*rotor, seconds(0.5), {});
    expect(std::abs(motionReading(*rotor, rotorReadingHornHz) - 0.8f) < 1.0e-3f, "Rotor starts at its slow speed");
    setEffectValue(node.parameters, *effectParameterForSuffix("rotor.speed"), 2.0f);
    updateEffectProcessor(*rotor, node);
    motionRun(*rotor, seconds(1.2), {});
    const auto horn = (motionReading(*rotor, rotorReadingHornHz) - 0.8f) / 6.0f;
    const auto drum = (motionReading(*rotor, rotorReadingDrumHz) - 0.68f) / (6.8f * 0.85f - 0.68f);
    motionRun(*rotor, seconds(2.4), {});
    const auto later = (motionReading(*rotor, rotorReadingHornHz) - 0.8f) / 6.0f;
    expect(horn > 0.55f && horn < 0.72f && drum > 0.25f && drum < 0.42f && later > 0.93f,
           "Rotor gets from slow to fast with its acceleration as time constant, the drum slower");
    setEffectValue(node.parameters, *effectParameterForSuffix("rotor.speed"), 0.0f);
    updateEffectProcessor(*rotor, node);
    motionRun(*rotor, seconds(20.0), {});
    const auto halted = motionRun(*rotor, seconds(1.5), {});
    expect(motionReading(*rotor, rotorReadingHornHz) == 0.0f && motionReading(*rotor, rotorReadingDrumHz) == 0.0f
               && swingsPerSecond(halted, 0.0, 1.5).second < 0.2,
           "Rotor braked: no movement once it has stopped");
    // Its latency as heard: nothing before it, the sound right after it.
    auto still = motionEffect(NativeDspNodeKind::rotor, { { "rotor.speed", 0.0f }, { "rotor.doppler", 0.0f }, { "rotor.room", 0.0f },
                                                         { "rotor.drive", 0.0f } });
    std::vector<float> impulse(4'800, 0.0f);
    impulse[0] = 1.0f;
    const auto response = motionRun(*still, impulse, {});
    auto before = 0.0;
    for (std::size_t i = 0; i + 1 < 48; ++i)
        before = std::max(before, static_cast<double>(std::abs(response[i])));
    const auto loudest = static_cast<std::size_t>(std::distance(
        response.begin(), std::max_element(response.begin(), response.end(), [](float a, float b) { return std::abs(a) < std::abs(b); })));
    expect(before < 1.0e-6 && loudest >= 48 && loudest < 48 + 24, "Rotor's sound arrives at its reported latency");
    // The dry part lines up with the wet: at half mix on a low tone nothing cancels.
    auto half = motionEffect(NativeDspNodeKind::rotor, { { "rotor.speed", 0.0f }, { "rotor.mix", 0.5f }, { "rotor.room", 0.0f } });
    const auto lowTone = sine(100.0, 0.5, 1.0);
    const auto halfOut = motionRun(*half, lowTone, {});
    expect(toneLevelDb(halfOut, 100.0, 9'600, 48'000) > -8.0, "Rotor's dry and wet line up");
    // Its level near the sound's on noise.
    const auto noise = motionNoise(static_cast<std::size_t>(2.0 * rate), 0.3f, 11u);
    auto defaults = motionEffect(NativeDspNodeKind::rotor, {});
    const auto noiseOut = motionRun(*defaults, noise, {});
    auto inPower = 0.0;
    auto outPower = 0.0;
    for (std::size_t i = static_cast<std::size_t>(0.5 * rate); i < noise.size(); ++i) {
        inPower += static_cast<double>(noise[i]) * noise[i];
        outPower += static_cast<double>(noiseOut[i]) * noiseOut[i];
    }
    const auto levelDb = 10.0 * std::log10(outPower / inPower);
    expect(levelDb > -3.0 && levelDb < 3.0, "Rotor at its defaults keeps about the sound's level");
    expect(motionProcessesWithoutAllocating(*fast), "Rotor allocates nothing as it plays");
    // At the extremes: finite and bounded.
    auto bounded = true;
    for (int speed = 0; speed < rotorSpeedCount; ++speed)
        for (const auto extreme : { 0.0f, 1.0f }) {
            auto edge = motionEffect(NativeDspNodeKind::rotor,
                                     { { "rotor.speed", static_cast<float>(speed) },
                                       { "rotor.fast", 9.0f },
                                       { "rotor.slow", extreme > 0.5f ? 2.0f : 0.3f },
                                       { "rotor.acceleration", extreme > 0.5f ? 5.0f : 0.2f },
                                       { "rotor.balance", extreme > 0.5f ? 1.0f : -1.0f },
                                       { "rotor.doppler", 1.0f },
                                       { "rotor.drive", 24.0f },
                                       { "rotor.crossover", extreme > 0.5f ? 1'500.0f : 400.0f },
                                       { "rotor.width", extreme },
                                       { "rotor.room", 1.0f },
                                       { "rotor.output", 0.0f } });
            std::vector<float> right;
            const auto left = motionRun(*edge, noise, {}, &right);
            bounded = bounded && motionBounded(left, 4.0) && motionBounded(right, 4.0);
        }
    expect(bounded, "every Rotor at its extremes stays finite and bounded");
}

// Mono and more than two channels: the first worked, the others through.
void motionEffectsTakeAnyWidth()
{
    using namespace cydaw::engine;
    auto ok = true;
    for (const auto kind : { NativeDspNodeKind::pompe, NativeDspNodeKind::decaleur, NativeDspNodeKind::anneau, NativeDspNodeKind::rotor })
        for (const std::uint32_t channels : { 1u, 4u }) {
            NativeDspNode node;
            node.kind = kind;
            applyEffectDefaults(*effectForKind(kind), node.parameters);
            auto processor = makeEffectProcessor(node);
            processor->prepare({ 48'000.0, 128, channels });
            AudioBuffer in(channels, 128);
            AudioBuffer out(channels, 128);
            const auto input = in.view(128);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::uint32_t frame = 0; frame < 128; ++frame)
                    input.channel(channel)[frame] = 0.25f * static_cast<float>(std::sin(0.03 * (frame + 7 * channel)));
            const auto output = out.view(128);
            for (int block = 0; block < 20; ++block)
                processor->process(input, output, ProcessContext {});
            const auto latency = processor->latencySamples();
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::uint32_t frame = 0; frame < 128; ++frame) {
                    const auto x = output.channel(channel)[frame];
                    ok = ok && std::isfinite(x) && std::abs(x) < 2.0f;
                    // The channels past two: the sound itself (delayed by the latency).
                    if (channel >= 2 && frame >= latency)
                        ok = ok && std::abs(x - input.channel(channel)[frame - latency]) < 1.0e-6f;
                }
        }
    expect(ok, "the motion effects play mono and pass channels past two through");
}

void motionEffectTests()
{
    pompeDucksOnTheBeat();
    pompeFollowsItsKey();
    pompeDrawsAndStaysBounded();
    decaleurShiftsEveryFrequency();
    anneauRingsAndKeeps();
    rotorTurnsWithInertia();
    motionEffectsTakeAnyWidth();
}
