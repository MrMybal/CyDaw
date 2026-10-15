// The engine tests of Harmonie, Justesse
// (PitchEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); pitchEffectTests() runs them all.

constexpr double kPitchTestRate = 48'000.0;

// One of the two, its defaults set, then `values` by binding suffix
// ("harmonie.shiftA"), prepared for that many channels.
std::unique_ptr<cydaw::engine::AudioProcessor> pitchTestEffect(const cydaw::engine::NativeDspNodeKind kind,
                                                               const std::vector<std::pair<std::string_view, float>>& values,
                                                               const std::uint32_t channels = 2, const double rate = kPitchTestRate)
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = kind;
    applyEffectDefaults(*effectForKind(kind), node.parameters);
    for (const auto& [suffix, value] : values) {
        const auto* parameter = effectParameterForSuffix(suffix);
        expect(parameter != nullptr, "every Harmonie and Justesse binding exists");
        if (parameter != nullptr)
            setEffectValue(node.parameters, *parameter, value);
    }
    auto processor = makeEffectProcessor(node);
    expect(processor != nullptr, "Harmonie and Justesse are made");
    if (processor != nullptr)
        processor->prepare({ rate, 512, channels });
    return processor;
}

// Runs a signal through (the same on every channel), 512 at a time; channel
// `listen` out. `afterBlock` hears where each block ended.
std::vector<float> pitchTestRun(cydaw::engine::AudioProcessor& processor, const std::vector<float>& signal, const std::uint32_t channels = 2,
                                const std::uint32_t listen = 0, const std::function<void(std::size_t)>& afterBlock = {})
{
    using namespace cydaw::engine;
    AudioBuffer in(channels, 512);
    AudioBuffer out(channels, 512);
    std::vector<float> heard;
    heard.reserve(signal.size());
    const ProcessContext context;
    for (std::size_t at = 0; at < signal.size(); at += 512) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(512, signal.size() - at));
        const auto input = in.view(count);
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            for (std::uint32_t frame = 0; frame < count; ++frame)
                input.channel(channel)[frame] = signal[at + frame];
        const auto output = out.view(count);
        processor.process(input, output, context);
        for (std::uint32_t frame = 0; frame < count; ++frame)
            heard.push_back(output.channel(listen)[frame]);
        if (afterBlock)
            afterBlock(at + count);
    }
    return heard;
}

std::vector<float> pitchTestSine(const double hz, const double seconds, const double amplitude = 0.5, const double rate = kPitchTestRate)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return x;
}

// A sawtooth made of its harmonics up to 8 kHz (no aliasing).
std::vector<float> pitchTestSaw(const double hz, const double seconds, const double amplitude = 0.4)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * kPitchTestRate));
    const auto harmonics = static_cast<int>(8'000.0 / hz);
    for (std::size_t i = 0; i < x.size(); ++i) {
        auto sum = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            sum += std::sin(2.0 * std::numbers::pi * hz * k * static_cast<double>(i) / kPitchTestRate) / k;
        x[i] = static_cast<float>(amplitude * 0.6 * sum);
    }
    return x;
}

// A vowel: a pulse train at `hz` through two formant resonators.
std::vector<float> pitchTestVowel(const double hz, const double seconds, const double first = 700.0, const double second = 1'200.0)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * kPitchTestRate));
    struct Resonator {
        double a1 {}, a2 {}, y1 {}, y2 {};
        Resonator(const double centre, const double bandwidth)
        {
            const auto r = std::exp(-std::numbers::pi * bandwidth / kPitchTestRate);
            a1 = 2.0 * r * std::cos(2.0 * std::numbers::pi * centre / kPitchTestRate);
            a2 = -r * r;
        }
        double operator()(const double in)
        {
            const auto y = in + a1 * y1 + a2 * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Resonator one(first, 90.0);
    Resonator two(second, 110.0);
    auto phase = 0.0;
    auto peak = 1.0e-9;
    std::vector<double> raw(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        phase += hz / kPitchTestRate;
        auto pulse = 0.0;
        if (phase >= 1.0) {
            phase -= 1.0;
            pulse = 1.0;
        }
        raw[i] = two(one(pulse));
        peak = std::max(peak, std::abs(raw[i]));
    }
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(0.4 * raw[i] / peak);
    return x;
}

std::vector<float> pitchTestNoise(const double seconds, const double amplitude = 0.3, std::uint32_t seed = 11u)
{
    std::vector<float> x(static_cast<std::size_t>(seconds * kPitchTestRate));
    for (auto& v : x) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        v = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * amplitude);
    }
    return x;
}

// The pitch of a stretch (seconds), as the tracker hears it: the median of
// its answers; 0 when it hears none.
double pitchTestHz(const std::vector<float>& x, const double from, const double to, const double rate = kPitchTestRate)
{
    cydaw::engine::PitchTracker tracker;
    tracker.prepare(rate, 40.0, 2'000.0);
    std::vector<double> heard;
    const auto start = static_cast<std::size_t>(from * rate);
    const auto end = std::min(x.size(), static_cast<std::size_t>(to * rate));
    for (auto i = start >= 30'000 ? start - 30'000 : 0; i < end; ++i)
        if (tracker.push(x[i]) && i >= start && tracker.frequency() > 0.0f)
            heard.push_back(tracker.frequency());
    if (heard.empty())
        return 0.0;
    std::sort(heard.begin(), heard.end());
    return heard[heard.size() / 2];
}

double pitchTestCents(const double hz, const double reference)
{
    return hz > 0.0 && reference > 0.0 ? 1'200.0 * std::log2(hz / reference) : 1.0e9;
}

// The level (linear) of one frequency in a stretch, Hann-windowed.
double pitchTestLevelAt(const std::vector<float>& x, const std::size_t from, const std::size_t count, const double hz)
{
    auto re = 0.0;
    auto im = 0.0;
    auto windowSum = 0.0;
    for (std::size_t n = 0; n < count && from + n < x.size(); ++n) {
        const auto w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(count));
        const auto phase = 2.0 * std::numbers::pi * hz * static_cast<double>(n) / kPitchTestRate;
        re += w * x[from + n] * std::cos(phase);
        im -= w * x[from + n] * std::sin(phase);
        windowSum += w;
    }
    return 2.0 * std::hypot(re, im) / std::max(1.0e-9, windowSum);
}

// Where a harmonic sound's spectral envelope peaks between `low` and `high`
// Hz: its harmonics' levels (each found within a few Hz of k * f0), the
// loudest one and a parabola through it and its neighbours (in dB).
double pitchTestFormant(const std::vector<float>& x, const std::size_t from, const std::size_t count, const double f0, const double low,
                        const double high)
{
    std::vector<double> levels;
    const auto harmonics = static_cast<int>(2'000.0 / f0);
    for (int k = 1; k <= harmonics; ++k) {
        auto best = 0.0;
        for (auto shift = -4.0; shift <= 4.0; shift += 1.0)
            best = std::max(best, pitchTestLevelAt(x, from, count, f0 * k + shift));
        levels.push_back(20.0 * std::log10(best + 1.0e-12));
    }
    auto loudest = -1;
    for (int k = 1; k <= harmonics; ++k)
        if (f0 * k >= low && f0 * k <= high && (loudest < 0 || levels[static_cast<std::size_t>(k - 1)] > levels[static_cast<std::size_t>(loudest - 1)]))
            loudest = k;
    if (loudest < 2 || loudest >= harmonics)
        return f0 * std::max(1, loudest);
    const auto a = levels[static_cast<std::size_t>(loudest - 2)];
    const auto b = levels[static_cast<std::size_t>(loudest - 1)];
    const auto c = levels[static_cast<std::size_t>(loudest)];
    const auto bend = a - 2.0 * b + c;
    const auto offset = bend < -1.0e-9 ? std::clamp(0.5 * (a - c) / bend, -0.5, 0.5) : 0.0;
    return f0 * (loudest + offset);
}

double pitchTestPeak(const std::vector<float>& x)
{
    auto peak = 0.0;
    for (const auto v : x)
        peak = std::isfinite(v) ? std::max(peak, static_cast<double>(std::abs(v))) : 1.0e30;
    return peak;
}

// The level (dB) of the difference between `x` and `reference` delayed by
// `lag`, against the reference's, over a stretch.
double pitchTestResidualDb(const std::vector<float>& x, const std::vector<float>& reference, const std::size_t lag, const std::size_t from,
                           const std::size_t to)
{
    auto error = 0.0;
    auto energy = 0.0;
    for (auto i = from; i < to && i < x.size(); ++i) {
        const auto r = i >= lag ? static_cast<double>(reference[i - lag]) : 0.0;
        error += (x[i] - r) * (x[i] - r);
        energy += r * r;
    }
    return 10.0 * std::log10((error + 1.0e-30) / (energy + 1.0e-30));
}

// ---- Harmonie -----------------------------------------------------------------------------------

void harmonieShiftsInTune()
{
    using namespace cydaw::engine;
    const auto sine = pitchTestSine(440.0, 1.6);
    // The smooth and percussive modes on a sine: an octave up, an octave down.
    for (const auto mode : { 0.0f, 2.0f }) {
        for (const auto& [shift, wanted] : { std::pair { 12.0f, 880.0 }, std::pair { -12.0f, 220.0 }, std::pair { 7.0f, 659.255 } }) {
            auto harmonie = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", mode }, { "harmonie.shiftA", shift } });
            const auto hz = pitchTestHz(pitchTestRun(*harmonie, sine), 0.4, 1.6);
            expect(std::abs(hz / wanted - 1.0) < 0.01, "Harmonie's grains shift a sine to its interval");
        }
    }
    // The voice mode on a sound with harmonics: the new pitch, its period.
    const auto saw = pitchTestSaw(220.0, 1.6);
    for (const auto& [shift, wanted] : { std::pair { 12.0f, 440.0 }, std::pair { -12.0f, 110.0 }, std::pair { 5.0f, 293.665 } }) {
        auto harmonie = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 1.0f }, { "harmonie.shiftA", shift } });
        const auto hz = pitchTestHz(pitchTestRun(*harmonie, saw), 0.4, 1.6);
        expect(std::abs(hz / wanted - 1.0) < 0.01, "Harmonie's voice mode shifts a voice to its interval");
    }
    // Its readings: the pitch it follows, the voices' ratios.
    auto harmonie = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 1.0f }, { "harmonie.shiftA", 12.0f },
                                                                  { "harmonie.shiftB", -5.0f }, { "harmonie.fineB", 50.0f } });
    static_cast<void>(pitchTestRun(*harmonie, saw));
    std::array<float, pitchfx::harmonieReadingCount> readings {};
    expect(harmonie->readings(readings.data(), readings.size()) == readings.size(), "Harmonie shows its readings");
    expect(std::abs(readings[pitchfx::harmonieReadHz] / 220.0f - 1.0f) < 0.01f && std::abs(readings[pitchfx::harmonieReadRatioA] - 2.0f) < 1.0e-4f
               && std::abs(readings[pitchfx::harmonieReadRatioB] - static_cast<float>(std::pow(2.0, -4.5 / 12.0))) < 1.0e-4f,
           "Harmonie's readings are the pitch it follows and its voices' ratios");
    auto smooth = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f } });
    static_cast<void>(pitchTestRun(*smooth, saw));
    static_cast<void>(smooth->readings(readings.data(), readings.size()));
    expect(readings[pitchfx::harmonieReadHz] == 0.0f && std::abs(readings[pitchfx::harmonieReadRatioA] - 2.0f) < 1.0e-4f,
           "Harmonie follows no pitch outside its voice mode");
}

void harmonieVoicesAsSet()
{
    using namespace cydaw::engine;
    const auto sine = pitchTestSine(440.0, 1.2);
    const auto fifth = 440.0 * std::pow(2.0, 7.0 / 12.0);
    // Voice B at no level: only voice A is heard.
    auto alone = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.shiftA", 12.0f },
                                                               { "harmonie.shiftB", 7.0f }, { "harmonie.levelB", 0.0f } });
    const auto aloneOut = pitchTestRun(*alone, sine);
    const auto octave = pitchTestLevelAt(aloneOut, 24'000, 16'384, 880.0);
    expect(octave > 0.1 && pitchTestLevelAt(aloneOut, 24'000, 16'384, fifth) < octave * 0.01, "Harmonie's voice B at no level is silent");
    // Both up: both heard.
    auto both = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.shiftA", 12.0f },
                                                              { "harmonie.shiftB", 7.0f }, { "harmonie.levelB", 1.0f } });
    const auto bothOut = pitchTestRun(*both, sine);
    expect(pitchTestLevelAt(bothOut, 24'000, 16'384, fifth) > pitchTestLevelAt(bothOut, 24'000, 16'384, 880.0) * 0.3,
           "Harmonie's voice B sounds at its interval");
    // Panned apart: A on the left, B on the right.
    auto panned = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.shiftA", 12.0f },
                                                                { "harmonie.shiftB", 7.0f }, { "harmonie.levelB", 1.0f },
                                                                { "harmonie.panA", -1.0f }, { "harmonie.panB", 1.0f } });
    const auto left = pitchTestRun(*panned, sine, 2, 0);
    panned->reset();
    const auto right = pitchTestRun(*panned, sine, 2, 1);
    expect(pitchTestLevelAt(left, 24'000, 16'384, fifth) < 0.01 * pitchTestLevelAt(right, 24'000, 16'384, fifth)
               && pitchTestLevelAt(right, 24'000, 16'384, 880.0) < 0.01 * pitchTestLevelAt(left, 24'000, 16'384, 880.0),
           "Harmonie pans its voices");
    // The level near the sound's at its defaults (one voice an octave up).
    auto plain = pitchTestEffect(NativeDspNodeKind::harmonie, {});
    const auto plainOut = pitchTestRun(*plain, pitchTestVowel(150.0, 1.2));
    const auto vowel = pitchTestVowel(150.0, 1.2);
    auto inEnergy = 0.0;
    auto outEnergy = 0.0;
    for (std::size_t i = 24'000; i < 48'000; ++i) {
        inEnergy += static_cast<double>(vowel[i]) * vowel[i];
        outEnergy += static_cast<double>(plainOut[i]) * plainOut[i];
    }
    const auto levelDb = 10.0 * std::log10(outEnergy / inEnergy);
    expect(levelDb > -4.0 && levelDb < 3.0, "Harmonie's level stays near the sound's");
}

void harmonieLinesUpWithItsLatency()
{
    using namespace cydaw::engine;
    const auto noise = pitchTestNoise(0.5);
    // The dry sound alone: the sound itself, exactly as late as reported.
    auto dry = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.dry", 1.0f }, { "harmonie.levelA", 0.0f }, { "harmonie.levelB", 0.0f } });
    const auto latency = static_cast<std::size_t>(dry->latencySamples());
    expect(latency == 1'920 && dry->maximumLatencySamples() == dry->latencySamples(), "Harmonie reports its 40 ms");
    const auto dryOut = pitchTestRun(*dry, noise);
    auto exact = true;
    for (std::size_t i = 0; i < dryOut.size(); ++i)
        exact = exact && dryOut[i] == (i >= latency ? noise[i - latency] : 0.0f);
    expect(exact, "Harmonie's dry sound is the sound delayed by exactly its latency");
    // A voice at the unison, in each mode: the sound back, as late.
    for (const auto mode : { 0.0f, 2.0f }) {
        auto unison = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", mode }, { "harmonie.shiftA", 0.0f } });
        expect(pitchTestResidualDb(pitchTestRun(*unison, noise), noise, latency, 0, noise.size()) < -80.0,
               "Harmonie's grains at the unison give the sound back, as late as reported");
    }
    const auto vowel = pitchTestVowel(130.0, 1.2);
    auto voiced = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 1.0f }, { "harmonie.shiftA", 0.0f } });
    expect(pitchTestResidualDb(pitchTestRun(*voiced, vowel), vowel, latency, 24'000, vowel.size()) < -30.0,
           "Harmonie's voice mode at the unison gives the voice back, as late as reported");
    // Several channels: the third passed through as late; a mono track works.
    auto wide = pitchTestEffect(NativeDspNodeKind::harmonie, {}, 4);
    const auto third = pitchTestRun(*wide, noise, 4, 2);
    auto passed = true;
    for (std::size_t i = latency; i < third.size(); ++i)
        passed = passed && third[i] == noise[i - latency];
    expect(passed, "Harmonie passes the channels above two through, as late");
    auto mono = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.levelB", 1.0f } }, 1);
    const auto monoOut = pitchTestRun(*mono, pitchTestSine(440.0, 0.5), 1, 0);
    const auto monoPeak = pitchTestPeak(monoOut);
    expect(monoPeak > 0.1 && monoPeak < 2.0, "Harmonie plays on a mono track");
}

void harmonieVoiceKeepsFormants()
{
    using namespace cydaw::engine;
    const auto vowel = pitchTestVowel(140.0, 1.6);
    const auto inputFormant = pitchTestFormant(vowel, 24'000, 32'768, 140.0, 400.0, 1'000.0);
    auto voice = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 1.0f }, { "harmonie.shiftA", 5.0f } });
    const auto voiceOut = pitchTestRun(*voice, vowel);
    const auto voiceHz = pitchTestHz(voiceOut, 0.5, 1.5);
    const auto wantedHz = 140.0 * std::pow(2.0, 5.0 / 12.0);
    expect(std::abs(voiceHz / wantedHz - 1.0) < 0.01, "Harmonie's voice mode moves the vowel's pitch");
    const auto voiceFormant = pitchTestFormant(voiceOut, 24'000, 32'768, voiceHz, 400.0, 1'000.0);
    expect(std::abs(voiceFormant / inputFormant - 1.0) < 0.15, "Harmonie's voice mode keeps the vowel's formant");
    // The smooth mode moves the formant with the pitch.
    auto smooth = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.shiftA", 5.0f } });
    const auto smoothOut = pitchTestRun(*smooth, vowel);
    const auto smoothFormant = pitchTestFormant(smoothOut, 24'000, 32'768, pitchTestHz(smoothOut, 0.5, 1.5), 400.0, 1'300.0);
    expect(smoothFormant / inputFormant > 1.18, "Harmonie's smooth mode moves the formant with the pitch");
}

void harmonieStaysFinite()
{
    using namespace cydaw::engine;
    std::vector<float> signal = pitchTestSaw(180.0, 0.4);
    const auto noise = pitchTestNoise(0.3, 0.8);
    signal.insert(signal.end(), noise.begin(), noise.end());
    signal.insert(signal.end(), 9'600, 0.0f);
    const auto sweep = pitchTestSine(70.0, 0.3, 0.9);
    signal.insert(signal.end(), sweep.begin(), sweep.end());
    auto bounded = true;
    for (const auto shift : { -24.0f, 24.0f })
        for (const auto mode : { 0.0f, 1.0f, 2.0f })
            for (const auto window : { 10.0f, 120.0f })
                for (const auto delay : { 0.0f, 500.0f }) {
                    auto harmonie = pitchTestEffect(NativeDspNodeKind::harmonie,
                                                    { { "harmonie.shiftA", shift }, { "harmonie.shiftB", -shift }, { "harmonie.fineA", 100.0f },
                                                      { "harmonie.fineB", -100.0f }, { "harmonie.levelB", 1.0f }, { "harmonie.dry", 1.0f },
                                                      { "harmonie.mode", mode }, { "harmonie.window", window }, { "harmonie.delay", delay },
                                                      { "harmonie.feedback", 0.9f }, { "harmonie.panA", -1.0f }, { "harmonie.panB", 1.0f } });
                    const auto peak = pitchTestPeak(pitchTestRun(*harmonie, signal));
                    bounded = bounded && peak < 30.0;
                }
    auto loud = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.output", 24.0f }, { "harmonie.delay", 1.0f },
                                                              { "harmonie.feedback", 0.9f }, { "harmonie.dry", 1.0f } });
    bounded = bounded && pitchTestPeak(pitchTestRun(*loud, signal)) < 500.0;
    expect(bounded, "Harmonie stays finite and bounded at every extreme");
    // Nothing allocated while it plays.
    auto playing = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 1.0f }, { "harmonie.levelB", 1.0f } });
    AudioBuffer in(2, 512);
    AudioBuffer out(2, 512);
    const ProcessContext context;
    allocation_probe::count.store(0);
    allocation_probe::enabled.store(true);
    for (std::size_t at = 0; at + 512 <= signal.size(); at += 512) {
        const auto input = in.view(512);
        for (std::uint32_t frame = 0; frame < 512; ++frame)
            input.channel(0)[frame] = input.channel(1)[frame] = signal[at + frame];
        playing->process(input, out.view(512), context);
    }
    allocation_probe::enabled.store(false);
    expect(allocation_probe::count.load() == 0, "Harmonie allocates nothing while it plays");
}

// ---- Justesse -----------------------------------------------------------------------------------

void justesseChoosesItsNotes()
{
    using namespace cydaw::engine::pitchfx;
    const auto& major = justesseScales[1];
    const auto& chromatic = justesseScales[0];
    expect(justesseNearestOf(60.4, 0, major) == 60 && justesseNearestOf(60.9, 0, major) == 60 && justesseNearestOf(61.6, 0, major) == 62
               && justesseNearestOf(69.9, 0, major) == 69 && justesseNearestOf(70.4, 0, major) == 71 && justesseNearestOf(59.56, 2, major) == 59,
           "Justesse finds the nearest note of its scale");
    expect(justesseTargetOf(60.55, 60, 0, chromatic) == 60 && justesseTargetOf(60.65, 60, 0, chromatic) == 61
               && justesseTargetOf(60.45, 61, 0, chromatic) == 61 && justesseTargetOf(60.3, 61, 0, chromatic) == 60
               && justesseTargetOf(64.2, 60, 0, chromatic) == 64,
           "Justesse holds its target until the pitch has gone 60 % of the way");
    expect(justesseAllowsOf(66, 0, justesseScales[10]) && !justesseAllowsOf(65, 0, justesseScales[10]) && justesseAllowsOf(66, 0, justesseScales[7])
               && justesseAllowsOf(61, 0, justesseScales[9]),
           "Justesse's modes hold their notes");
    expect(std::abs(justesseSlownessOf(0.1, 1.0) - 1.0) < 1.0e-9 && std::abs(justesseSlownessOf(2.0, 1.0) - 5.0) < 1.0e-9
               && std::abs(justesseSlownessOf(2.0, 0.0) - 1.0) < 1.0e-9,
           "Justesse's humanize slows held notes up to five times");
}

void justesseCorrectsToTheScale()
{
    using namespace cydaw::engine;
    struct Case {
        double hz;
        float key;
        float scale;
        double wanted;
        const char* what;
    };
    for (const auto& item : { Case { 452.0, 0.0f, 1.0f, 440.0, "Justesse pulls 452 Hz to A4 in C major" },
                              Case { 470.0, 0.0f, 0.0f, 466.164, "Justesse pulls 470 Hz to A#4 in the chromatic scale" },
                              Case { 255.0, 2.0f, 1.0f, 246.942, "Justesse pulls 255 Hz to B3 in D major" } }) {
        auto justesse = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.key", item.key },
                                                                      { "justesse.scale", item.scale } });
        const auto out = pitchTestRun(*justesse, pitchTestSine(item.hz, 1.6));
        expect(std::abs(pitchTestCents(pitchTestHz(out, 0.4, 1.6), item.wanted)) < 5.0, item.what);
    }
    // No amount: the pitch as it was.
    auto none = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.amount", 0.0f } });
    expect(std::abs(pitchTestCents(pitchTestHz(pitchTestRun(*none, pitchTestSine(452.0, 1.2)), 0.4, 1.2), 452.0)) < 5.0,
           "Justesse at no amount leaves the pitch");
    // The level kept.
    auto level = pitchTestEffect(NativeDspNodeKind::justesse, {});
    const auto vowel = pitchTestVowel(152.0, 1.2);
    const auto levelOut = pitchTestRun(*level, vowel);
    auto inEnergy = 0.0;
    auto outEnergy = 0.0;
    for (std::size_t i = 24'000; i < 48'000; ++i) {
        inEnergy += static_cast<double>(vowel[i]) * vowel[i];
        outEnergy += static_cast<double>(levelOut[i]) * levelOut[i];
    }
    const auto levelDb = 10.0 * std::log10(outEnergy / inEnergy);
    expect(levelDb > -2.0 && levelDb < 2.0, "Justesse keeps the sound's level");
    // Transposed an octave up after the correction: twice the pitch.
    auto up = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.transpose", 12.0f } });
    expect(std::abs(pitchTestHz(pitchTestRun(*up, pitchTestSaw(220.0, 1.6)), 0.4, 1.6) / 440.0 - 1.0) < 0.01,
           "Justesse's transposition by an octave doubles the pitch");
}

void justesseTakesItsTime()
{
    using namespace cydaw::engine;
    const auto sine = pitchTestSine(452.0, 1.0);
    // How far the correction has gone 50 ms after the first target.
    const auto after = [&](const float speed) {
        auto justesse = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", speed }, { "justesse.key", 0.0f },
                                                                      { "justesse.scale", 1.0f } });
        std::array<float, pitchfx::justesseReadingCount> readings {};
        auto targetAt = std::size_t { 0 };
        auto cents = 0.0f;
        auto wanted = 0.0f;
        pitchTestRun(*justesse, sine, 2, 0, [&](const std::size_t end) {
            static_cast<void>(justesse->readings(readings.data(), readings.size()));
            if (targetAt == 0 && readings[pitchfx::justesseReadTarget] >= 0.0f) {
                targetAt = end;
                wanted = static_cast<float>((69.0 - pitchfx::pitchNoteOfHz(readings[pitchfx::justesseReadHz])) * 100.0);
            }
            if (targetAt > 0 && end >= targetAt + 2'400 && end < targetAt + 2'400 + 512)
                cents = readings[pitchfx::justesseReadCents];
        });
        return std::pair { cents, wanted };
    };
    const auto [slow, slowWanted] = after(400.0f);
    const auto [fast, fastWanted] = after(0.0f);
    expect(slowWanted < -40.0f && fastWanted < -40.0f, "Justesse aims 452 Hz at A4");
    expect(std::abs(fast - fastWanted) < 2.0f, "Justesse at the robot speed corrects at once");
    expect(slow < 0.0f && std::abs(slow) < 0.5f * std::abs(slowWanted), "Justesse at its slowest is still far from the note after 50 ms");
    // Its trails: the detected notes and the corrected ones.
    auto trails = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.scale", 1.0f } });
    static_cast<void>(pitchTestRun(*trails, sine));
    std::array<float, pitchfx::justesseReadingCount> readings {};
    expect(trails->readings(readings.data(), readings.size()) == readings.size(), "Justesse shows its readings");
    auto trailed = true;
    for (int index = 0; index < pitchfx::justesseTrailLength; ++index) {
        trailed = trailed && std::abs(readings[static_cast<std::size_t>(pitchfx::justesseReadDetectedTrail + index)] - 69.46f) < 0.05f;
        trailed = trailed && std::abs(readings[static_cast<std::size_t>(pitchfx::justesseReadCorrectedTrail + index)] - 69.0f) < 0.02f;
    }
    expect(trailed && readings[pitchfx::justesseReadTarget] == 69.0f && readings[pitchfx::justesseReadConfidence] > 0.8f,
           "Justesse's trails hold the notes heard and the notes corrected");
}

void justesseLinesUpWithItsLatency()
{
    using namespace cydaw::engine;
    const auto noise = pitchTestNoise(0.5);
    auto dry = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.mix", 0.0f } });
    const auto latency = static_cast<std::size_t>(dry->latencySamples());
    expect(latency == 1'920 && dry->maximumLatencySamples() == dry->latencySamples(), "Justesse reports its 40 ms");
    const auto dryOut = pitchTestRun(*dry, noise);
    auto exact = true;
    for (std::size_t i = 0; i < dryOut.size(); ++i)
        exact = exact && dryOut[i] == (i >= latency ? noise[i - latency] : 0.0f);
    expect(exact, "Justesse's dry sound is the sound delayed by exactly its latency");
    // What has no pitch passes, as late.
    auto wet = pitchTestEffect(NativeDspNodeKind::justesse, {});
    expect(pitchTestResidualDb(pitchTestRun(*wet, noise), noise, latency, 0, noise.size()) < -80.0, "Justesse passes noise as late as reported");
    // A voice in tune, uncorrected: the voice back, as late.
    const auto vowel = pitchTestVowel(130.81, 1.2);
    auto tuned = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.amount", 0.0f } });
    expect(pitchTestResidualDb(pitchTestRun(*tuned, vowel), vowel, latency, 24'000, vowel.size()) < -30.0,
           "Justesse gives an uncorrected voice back, as late as reported");
    // Its mix lines the two up: half and half of the same sound is the sound.
    auto half = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.amount", 0.0f }, { "justesse.mix", 0.5f } });
    expect(pitchTestResidualDb(pitchTestRun(*half, vowel), vowel, latency, 24'000, vowel.size()) < -30.0,
           "Justesse's dry and corrected sounds line up");
    // A mono track; the channels above two passed through.
    auto mono = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f } }, 1);
    expect(std::abs(pitchTestCents(pitchTestHz(pitchTestRun(*mono, pitchTestSine(452.0, 1.2), 1, 0), 0.4, 1.2), 440.0)) < 5.0,
           "Justesse corrects a mono track");
    auto wide = pitchTestEffect(NativeDspNodeKind::justesse, {}, 3);
    const auto third = pitchTestRun(*wide, noise, 3, 2);
    auto passed = true;
    for (std::size_t i = latency; i < third.size(); ++i)
        passed = passed && third[i] == noise[i - latency];
    expect(passed, "Justesse passes the channels above two through, as late");
}

void justesseTransposesWhatItDoesNotFollow()
{
    using namespace cydaw::engine;
    // 45 Hz is under its lowest pitch: no note corrected (F1 or F#1 would be
    // 87.3 or 92.5 Hz an octave up), just the octave.
    auto justesse = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.transpose", 12.0f } });
    const auto hz = pitchTestHz(pitchTestRun(*justesse, pitchTestSine(45.0, 1.6, 0.6)), 0.4, 1.6);
    expect(std::abs(hz / 90.0 - 1.0) < 0.01, "Justesse only transposes what it does not follow");
}

void pitchEffectsAtOtherRates()
{
    using namespace cydaw::engine;
    for (const auto rate : { 44'100.0, 96'000.0 }) {
        auto justesse = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f }, { "justesse.scale", 1.0f } }, 2, rate);
        expect(justesse->latencySamples() == static_cast<std::uint32_t>(std::lround(0.04 * rate)), "Justesse's latency is 40 ms at every rate");
        const auto tuned = pitchTestHz(pitchTestRun(*justesse, pitchTestSine(452.0, 1.6, 0.5, rate)), 0.4, 1.6, rate);
        expect(std::abs(pitchTestCents(tuned, 440.0)) < 5.0, "Justesse corrects at 44.1 and 96 kHz");
        auto harmonie = pitchTestEffect(NativeDspNodeKind::harmonie, { { "harmonie.mode", 0.0f }, { "harmonie.shiftA", 7.0f } }, 2, rate);
        const auto fifth = pitchTestHz(pitchTestRun(*harmonie, pitchTestSine(440.0, 1.6, 0.5, rate)), 0.4, 1.6, rate);
        expect(std::abs(fifth / (440.0 * std::pow(2.0, 7.0 / 12.0)) - 1.0) < 0.01, "Harmonie shifts at 44.1 and 96 kHz");
    }
}

void justesseStaysFinite()
{
    using namespace cydaw::engine;
    // A gliding tone, noise, silence, a low hum, a high whistle.
    std::vector<float> signal(static_cast<std::size_t>(0.5 * kPitchTestRate));
    auto phase = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const auto hz = 100.0 * std::pow(8.0, static_cast<double>(i) / static_cast<double>(signal.size()));
        phase += hz / kPitchTestRate;
        signal[i] = static_cast<float>(0.7 * std::sin(2.0 * std::numbers::pi * phase));
    }
    for (const auto& part : { pitchTestNoise(0.2, 0.9), std::vector<float>(4'800, 0.0f), pitchTestSine(52.0, 0.3, 0.9), pitchTestSine(1'500.0, 0.2, 0.9) })
        signal.insert(signal.end(), part.begin(), part.end());
    auto bounded = true;
    for (const auto speed : { 0.0f, 400.0f })
        for (const auto transpose : { -12.0f, 12.0f })
            for (const auto scale : { 0.0f, 7.0f, 11.0f })
                for (const auto pitch : { 0.0f, 1.0f }) {
                    auto justesse = pitchTestEffect(NativeDspNodeKind::justesse,
                                                    { { "justesse.speed", speed }, { "justesse.transpose", transpose }, { "justesse.scale", scale },
                                                      { "justesse.key", 11.0f * pitch }, { "justesse.humanize", pitch },
                                                      { "justesse.sensitivity", pitch }, { "justesse.lowPitch", pitch > 0.5f ? 400.0f : 50.0f },
                                                      { "justesse.highPitch", pitch > 0.5f ? 300.0f : 1'600.0f }, { "justesse.mix", 1.0f } });
                    bounded = bounded && pitchTestPeak(pitchTestRun(*justesse, signal)) < 4.0;
                }
    auto loud = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.output", 24.0f }, { "justesse.mix", 0.5f } });
    bounded = bounded && pitchTestPeak(pitchTestRun(*loud, signal)) < 40.0;
    expect(bounded, "Justesse stays finite and bounded at every extreme");
    auto playing = pitchTestEffect(NativeDspNodeKind::justesse, { { "justesse.speed", 0.0f } });
    AudioBuffer in(2, 512);
    AudioBuffer out(2, 512);
    const ProcessContext context;
    allocation_probe::count.store(0);
    allocation_probe::enabled.store(true);
    for (std::size_t at = 0; at + 512 <= signal.size(); at += 512) {
        const auto input = in.view(512);
        for (std::uint32_t frame = 0; frame < 512; ++frame)
            input.channel(0)[frame] = input.channel(1)[frame] = signal[at + frame];
        playing->process(input, out.view(512), context);
    }
    allocation_probe::enabled.store(false);
    expect(allocation_probe::count.load() == 0, "Justesse allocates nothing while it plays");
}

void pitchEffectTests()
{
    harmonieShiftsInTune();
    harmonieVoicesAsSet();
    harmonieLinesUpWithItsLatency();
    harmonieVoiceKeepsFormants();
    harmonieStaysFinite();
    justesseChoosesItsNotes();
    justesseCorrectsToTheScale();
    justesseTakesItsTime();
    justesseLinesUpWithItsLatency();
    justesseTransposesWhatItDoesNotFollow();
    justesseStaysFinite();
    pitchEffectsAtOtherRates();
}
