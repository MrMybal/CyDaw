// The engine tests of Utilitaire, Stéréoscope
// (UtilityEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); utilityEffectTests() runs them all.

// One of these effects, its defaults set, then `values` by binding suffix
// ("utilitaire.gain"), prepared at 48 kHz for `channels`.
std::unique_ptr<cydaw::engine::AudioProcessor> utilityEffect(const cydaw::engine::NativeDspNodeKind kind,
                                                             const std::vector<std::pair<std::string_view, float>>& values,
                                                             const std::uint32_t channels = 2)
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
        expect(parameter != nullptr, "the utility tests name real settings");
        if (parameter != nullptr)
            setEffectValue(node.parameters, *parameter, value);
    }
    auto processor = makeEffectProcessor(node);
    if (processor != nullptr)
        processor->prepare({ 48'000.0, 256, channels });
    return processor;
}

// A signal of any number of channels through, in blocks of 256; what came
// out, channel by channel. `allocations`: how many times processing asked
// for memory.
std::vector<std::vector<float>> utilityRun(cydaw::engine::AudioProcessor& processor, const std::vector<std::vector<float>>& channels,
                                           std::size_t* allocations = nullptr)
{
    using namespace cydaw::engine;
    const auto count = static_cast<std::uint32_t>(channels.size());
    const auto frames = channels.empty() ? std::size_t {} : channels.front().size();
    AudioBuffer in(count, 256);
    AudioBuffer out(count, 256);
    std::vector<std::vector<float>> result(count, std::vector<float>(frames, 0.0f));
    ProcessContext context;
    if (allocations != nullptr) {
        allocation_probe::count.store(0, std::memory_order_relaxed);
        allocation_probe::enabled.store(true, std::memory_order_relaxed);
    }
    for (std::size_t at = 0; at < frames; at += 256) {
        const auto block = static_cast<std::uint32_t>(std::min<std::size_t>(256, frames - at));
        const auto input = in.view(block);
        for (std::uint32_t channel = 0; channel < count; ++channel)
            for (std::uint32_t frame = 0; frame < block; ++frame)
                input.channel(channel)[frame] = channels[channel][at + frame];
        const auto output = out.view(block);
        processor.process(input, output, context);
        for (std::uint32_t channel = 0; channel < count; ++channel)
            for (std::uint32_t frame = 0; frame < block; ++frame)
                result[channel][at + frame] = output.channel(channel)[frame];
    }
    if (allocations != nullptr) {
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        *allocations = allocation_probe::count.load(std::memory_order_relaxed);
    }
    return result;
}

std::vector<float> utilitySine(const double hz, const double amplitude, const std::size_t frames, const double offset = 0.0)
{
    std::vector<float> wave(frames);
    for (std::size_t i = 0; i < frames; ++i)
        wave[i] = static_cast<float>(offset + amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48'000.0));
    return wave;
}

std::vector<float> utilityNoise(const std::size_t frames, std::uint32_t seed, const double amplitude)
{
    std::vector<float> noise(frames);
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * amplitude);
    }
    return noise;
}

std::vector<float> utilityScaled(const std::vector<float>& x, const float factor)
{
    std::vector<float> scaled(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        scaled[i] = x[i] * factor;
    return scaled;
}

double utilityRms(const std::vector<float>& x, const std::size_t from, const std::size_t to)
{
    auto sum = 0.0;
    for (auto i = from; i < std::min(to, x.size()); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, std::min(to, x.size()) - from)));
}

// The side (L - R) / 2 of a stereo pair.
std::vector<float> utilitySide(const std::vector<std::vector<float>>& sides)
{
    std::vector<float> side(sides[0].size());
    for (std::size_t i = 0; i < side.size(); ++i)
        side[i] = 0.5f * (sides[0][i] - sides[1][i]);
    return side;
}

double utilityPeak(const std::vector<std::vector<float>>& channels)
{
    auto peak = 0.0;
    for (const auto& channel : channels)
        for (const auto x : channel)
            peak = std::isfinite(x) ? std::max(peak, static_cast<double>(std::abs(x))) : 1.0e30;
    return peak;
}

std::vector<float> utilityReadings(const cydaw::engine::AudioProcessor& processor)
{
    std::vector<float> readings(1'024, 0.0f);
    readings.resize(processor.readings(readings.data(), readings.size()));
    return readings;
}

void utilitairePassesAtRest()
{
    using namespace cydaw::engine;
    const std::vector<std::vector<float>> noise { utilityNoise(48'000, 11u, 0.5), utilityNoise(48'000, 23u, 0.5) };
    auto plain = utilityEffect(NativeDspNodeKind::utilitaire, {});
    expect(plain != nullptr, "the utility is made");
    if (plain == nullptr)
        return;
    expect(utilityRun(*plain, noise) == noise, "the utility at its defaults passes the sound bit for bit");
    expect(plain->latencySamples() == 0 && plain->maximumLatencySamples() == 0, "the utility reports no latency");
    auto bassOff = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.monoBass", 20.0f } });
    expect(utilityRun(*bassOff, noise) == noise, "mono bass at 20 Hz leaves the sound bit for bit");
    // Every step working: nothing asked of memory while it plays.
    auto busy = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.monoBass", 150.0f }, { "utilitaire.width", 1.5f },
                                                               { "utilitaire.pan", 0.3f }, { "utilitaire.gain", -3.0f },
                                                               { "utilitaire.dcFilter", 1.0f }, { "utilitaire.delay", 3.37f },
                                                               { "utilitaire.invertLeft", 1.0f } });
    (void) utilityRun(*busy, noise);
    std::size_t allocations = 0;
    (void) utilityRun(*busy, noise, &allocations);
    expect(allocations == 0, "the utility plays without allocating");
}

void utilitaireSidesAndPolarity()
{
    using namespace cydaw::engine;
    const auto left = utilityNoise(24'000, 5u, 0.6);
    const auto right = utilitySine(330.0, 0.4, 24'000);
    const std::vector<std::vector<float>> stereo { left, right };
    auto flipLeft = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.invertLeft", 1.0f } });
    auto out = utilityRun(*flipLeft, stereo);
    expect(out[0] == utilityScaled(left, -1.0f) && out[1] == right, "inverting the left negates the left only");
    auto flipRight = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.invertRight", 1.0f } });
    out = utilityRun(*flipRight, stereo);
    expect(out[0] == left && out[1] == utilityScaled(right, -1.0f), "inverting the right negates the right only");
    auto swapped = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.channelMode", 3.0f } });
    out = utilityRun(*swapped, stereo);
    expect(out[0] == right && out[1] == left, "swapped sends each side to the other");
    auto leftOnly = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.channelMode", 1.0f } });
    out = utilityRun(*leftOnly, stereo);
    expect(out[0] == left && out[1] == left, "left only plays the left on both sides");
    auto rightOnly = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.channelMode", 2.0f } });
    out = utilityRun(*rightOnly, stereo);
    expect(out[0] == right && out[1] == right, "right only plays the right on both sides");
    auto mono = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.channelMode", 4.0f } });
    out = utilityRun(*mono, stereo);
    auto worst = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(out[0][i]) - 0.5 * (static_cast<double>(left[i]) + right[i])));
    expect(out[0] == out[1] && worst < 1.0e-7, "mono plays (L + R) / 2 on both sides");
}

void utilitaireMonoBassAndWidth()
{
    using namespace cydaw::engine;
    constexpr std::size_t frames = 48'000;
    const auto steady = static_cast<std::size_t>(24'000);
    // Width 0: one sound in the middle; width 2: the side doubled.
    const std::vector<std::vector<float>> stereo { utilityNoise(frames, 3u, 0.5), utilityNoise(frames, 4u, 0.5) };
    auto narrow = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.width", 0.0f } });
    auto out = utilityRun(*narrow, stereo);
    expect(out[0] == out[1], "width 0 makes both sides the same");
    const auto tone = utilitySine(440.0, 0.5, frames);
    auto wide = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.width", 2.0f } });
    out = utilityRun(*wide, { tone, std::vector<float>(frames, 0.0f) });
    auto off = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
        off = std::max({ off, std::abs(out[0][i] - 1.5 * tone[i]), std::abs(out[1][i] + 0.5 * tone[i]) });
    expect(off < 1.0e-6, "width 2 doubles the side");
    // Mono bass at 200 Hz: a 50 Hz side goes, a 2 kHz side stays, a 50 Hz middle stays.
    const auto sideOnly = [&](const double hz) {
        const auto wave = utilitySine(hz, 0.5, frames);
        return std::vector<std::vector<float>> { wave, utilityScaled(wave, -1.0f) };
    };
    auto bass = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.monoBass", 200.0f } });
    auto low = sideOnly(50.0);
    out = utilityRun(*bass, low);
    const auto lowKept = utilityRms(utilitySide(out), steady, frames) / utilityRms(utilitySide(low), steady, frames);
    expect(lowKept < 0.03, "mono bass takes a 50 Hz side away (" + std::to_string(lowKept) + " left)");
    bass->reset();
    auto high = sideOnly(2'000.0);
    out = utilityRun(*bass, high);
    const auto highKept = utilityRms(utilitySide(out), steady, frames) / utilityRms(utilitySide(high), steady, frames);
    expect(highKept > 0.95 && highKept < 1.05, "mono bass leaves a 2 kHz side (" + std::to_string(highKept) + ")");
    bass->reset();
    const auto middle = utilitySine(50.0, 0.5, frames);
    out = utilityRun(*bass, { middle, middle });
    const auto middleKept = utilityRms(out[0], steady, frames) / utilityRms(middle, steady, frames);
    auto apart = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
        apart = std::max(apart, static_cast<double>(std::abs(out[0][i] - out[1][i])));
    expect(middleKept > 0.98 && middleKept < 1.02 && apart < 1.0e-6, "mono bass keeps the middle whole");
}

void utilitaireLevelDcDelayMute()
{
    using namespace cydaw::engine;
    constexpr std::size_t frames = 48'000;
    const auto left = utilityNoise(frames, 7u, 0.4);
    const auto right = utilityNoise(frames, 9u, 0.4);
    const std::vector<std::vector<float>> stereo { left, right };
    // Gain.
    auto louder = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.gain", 6.0f } });
    auto out = utilityRun(*louder, stereo);
    const auto factor = std::pow(10.0, 6.0 / 20.0);
    auto off = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
        off = std::max(off, std::abs(out[0][i] - factor * left[i]));
    expect(off < 1.0e-5, "the gain sets the level");
    // Balance: the side it goes to keeps its level, the other one fades.
    auto toLeft = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.pan", -1.0f } });
    out = utilityRun(*toLeft, stereo);
    expect(out[0] == left && utilityPeak({ out[1] }) == 0.0, "pan fully left keeps the left and silences the right");
    auto halfRight = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.pan", 0.5f } });
    out = utilityRun(*halfRight, stereo);
    off = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
        off = std::max({ off, std::abs(out[0][i] - std::sqrt(0.5) * left[i]), static_cast<double>(std::abs(out[1][i] - right[i])) });
    expect(off < 1.0e-6, "pan half right takes the left 3 dB down");
    // DC filter: an offset goes, the tone stays.
    const auto offset = utilitySine(1'000.0, 0.1, frames * 2, 0.5);
    auto dc = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.dcFilter", 1.0f } });
    out = utilityRun(*dc, { offset, offset });
    auto mean = 0.0;
    for (std::size_t i = frames; i < frames * 2; ++i)
        mean += out[0][i];
    mean /= static_cast<double>(frames);
    const auto toneLevel = utilityRms(out[0], frames, frames * 2) / (0.1 / std::sqrt(2.0));
    expect(std::abs(mean) < 1.0e-3 && toneLevel > 0.98 && toneLevel < 1.02, "the DC filter takes an offset away and keeps the tone");
    // Delay: 10 ms is 480 samples at 48 kHz, to the sample.
    auto later = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.delay", 10.0f } });
    out = utilityRun(*later, stereo);
    auto aligned = true;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto expected = i < 480 ? 0.0f : left[i - 480];
        const auto expectedRight = i < 480 ? 0.0f : right[i - 480];
        aligned = aligned && out[0][i] == expected && out[1][i] == expectedRight;
    }
    expect(aligned, "a 10 ms delay moves the sound 480 samples");
    expect(later->latencySamples() == 0, "the delay is a setting, not a latency");
    // Between two samples the highs stay (480.5 samples).
    for (const auto hz : { 1'000.0, 15'000.0 }) {
        const auto wave = utilitySine(hz, 0.5, frames);
        auto between = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.delay", static_cast<float>(480.5 / 48.0) } });
        out = utilityRun(*between, { wave, wave });
        const auto kept = utilityRms(out[0], 4'800, frames) / utilityRms(wave, 4'800, frames);
        expect(kept > 0.94 && kept < 1.03,
               "a delay between two samples keeps " + std::to_string(static_cast<int>(hz)) + " Hz (" + std::to_string(kept) + ")");
    }
    // Mute.
    auto muted = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.mute", 1.0f } });
    out = utilityRun(*muted, stereo);
    expect(utilityPeak(out) == 0.0, "mute gives silence");
    // Its readings: each side's peak.
    auto meter = utilityEffect(NativeDspNodeKind::utilitaire, {});
    (void) utilityRun(*meter, { utilitySine(500.0, 0.5, 9'600), std::vector<float>(9'600, 0.0f) });
    const auto readings = utilityReadings(*meter);
    expect(readings.size() == 2 && readings[0] > 0.45f && readings[0] <= 0.5f && readings[1] == 0.0f,
           "the utility reads each side's peak");
}

void utilitaireStaysBoundedAndSingleChannel()
{
    using namespace cydaw::engine;
    const std::vector<std::vector<float>> loud { utilityNoise(9'600, 31u, 1.0), utilityNoise(9'600, 37u, 1.0) };
    auto bounded = true;
    for (int mode = 0; mode < cydaw::engine::utility::utilitaireChannelModeCount; ++mode)
        for (int corner = 0; corner < 16; ++corner) {
            auto processor = utilityEffect(
                NativeDspNodeKind::utilitaire,
                { { "utilitaire.channelMode", static_cast<float>(mode) }, { "utilitaire.gain", corner & 1 ? 36.0f : -36.0f },
                  { "utilitaire.pan", corner & 2 ? 1.0f : -1.0f }, { "utilitaire.width", corner & 4 ? 2.0f : 0.0f },
                  { "utilitaire.monoBass", corner & 8 ? 500.0f : 20.0f }, { "utilitaire.invertLeft", static_cast<float>(corner % 3 == 0) },
                  { "utilitaire.invertRight", static_cast<float>(corner % 5 == 0) }, { "utilitaire.dcFilter", static_cast<float>(mode % 2) },
                  { "utilitaire.delay", corner % 2 == 0 ? 20.0f : 0.37f } });
            const auto peak = utilityPeak(utilityRun(*processor, loud));
            bounded = bounded && peak < 300.0;
        }
    expect(bounded, "the utility stays finite and bounded at its extremes");
    // One channel: a centred sound.
    const auto wave = utilityNoise(9'600, 41u, 0.5);
    auto plain = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.width", 0.0f } }, 1);
    expect(utilityRun(*plain, { wave }).front() == wave, "on one channel, width changes nothing");
    auto flipped = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.invertRight", 1.0f } }, 1);
    expect(utilityRun(*flipped, { wave }).front() == utilityScaled(wave, -1.0f), "on one channel, either polarity switch turns it over");
    auto panned = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.pan", 1.0f } }, 1);
    expect(utilityRun(*panned, { wave }).front() == utilityScaled(wave, 0.5f), "on one channel, a full pan halves it (its mono sum)");
    // Four channels: the two past the pair take the gain.
    auto surround = utilityEffect(NativeDspNodeKind::utilitaire, { { "utilitaire.gain", -6.0f }, { "utilitaire.channelMode", 3.0f } }, 4);
    const auto out = utilityRun(*surround, { wave, wave, wave, wave });
    const auto expected = std::pow(10.0, -6.0 / 20.0);
    auto off = 0.0;
    for (std::size_t i = 0; i < wave.size(); ++i)
        off = std::max(off, std::abs(out[3][i] - expected * wave[i]));
    expect(off < 1.0e-6, "channels past the pair take the gain");
}

// Settings changed while it plays: each switch fades its step in or out, so
// no sample jumps far from the one before.
void utilitaireSwitchesWithoutClicks()
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = NativeDspNodeKind::utilitaire;
    const auto* effect = effectForKind(node.kind);
    if (effect == nullptr)
        return;
    applyEffectDefaults(*effect, node.parameters);
    auto processor = makeEffectProcessor(node);
    processor->prepare({ 48'000.0, 256, 2 });
    const std::vector<std::pair<std::string_view, float>> changes {
        { "utilitaire.invertLeft", 1.0f }, { "utilitaire.channelMode", 3.0f }, { "utilitaire.monoBass", 300.0f },
        { "utilitaire.dcFilter", 1.0f },   { "utilitaire.width", 2.0f },       { "utilitaire.pan", -0.7f },
        { "utilitaire.mute", 1.0f },       { "utilitaire.mute", 0.0f },        { "utilitaire.channelMode", 4.0f },
        { "utilitaire.invertLeft", 0.0f }, { "utilitaire.monoBass", 20.0f },   { "utilitaire.dcFilter", 0.0f },
    };
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    ProcessContext context;
    std::array<double, 2> previous {};
    auto jump = 0.0;
    auto finite = true;
    std::size_t next = 0;
    for (int block = 0; block < 420; ++block) {
        if (block % 30 == 15 && next < changes.size()) {
            setEffectValue(node.parameters, *effectParameterForSuffix(changes[next].first), changes[next].second);
            expect(updateEffectProcessor(*processor, node), "the utility takes new settings");
            ++next;
        }
        const auto input = in.view(256);
        for (std::uint32_t frame = 0; frame < 256; ++frame) {
            const auto phase = 2.0 * std::numbers::pi * 100.0 * static_cast<double>(block * 256 + static_cast<int>(frame)) / 48'000.0;
            input.channel(0)[frame] = static_cast<float>(0.5 * std::sin(phase) + 0.2);
            input.channel(1)[frame] = static_cast<float>(0.5 * std::cos(phase) - 0.1);
        }
        const auto output = out.view(256);
        processor->process(input, output, context);
        for (std::uint32_t channel = 0; channel < 2; ++channel)
            for (std::uint32_t frame = 0; frame < 256; ++frame) {
                const auto x = static_cast<double>(output.channel(channel)[frame]);
                finite = finite && std::isfinite(x);
                if (block > 0 || frame > 0)
                    jump = std::max(jump, std::abs(x - previous[channel]));
                previous[channel] = x;
            }
    }
    expect(finite && next == changes.size() && jump < 0.03,
           "the utility's switches fade without clicks (largest step " + std::to_string(jump) + ")");
}

void stereoscopeReadsTheImage()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::utility;
    constexpr std::size_t frames = 96'000;
    const auto a = utilityNoise(frames, 51u, 0.5);
    const auto b = utilityNoise(frames, 77u, 0.5);
    const auto read = [](const std::vector<std::vector<float>>& sound, std::vector<std::vector<float>>* out = nullptr) {
        auto scope = utilityEffect(NativeDspNodeKind::stereoscope, {}, static_cast<std::uint32_t>(sound.size()));
        auto result = utilityRun(*scope, sound);
        if (out != nullptr)
            *out = result;
        return utilityReadings(*scope);
    };
    std::vector<std::vector<float>> out;
    auto readings = read({ a, b }, &out);
    expect(out == std::vector<std::vector<float>> { a, b }, "the stereoscope passes the sound bit for bit");
    expect(readings.size() == static_cast<std::size_t>(stereoscopeReadingCount), "the stereoscope gives all its readings");
    expect(std::abs(readings[stereoscopeReadingCorrelation]) < 0.15f && std::abs(readings[stereoscopeReadingCorrelationAverage]) < 0.15f,
           "independent noises read as uncorrelated");
    expect(readings[stereoscopeReadingWidth] > 0.8f && readings[stereoscopeReadingWidth] < 1.25f, "independent noises read as wide as tall");
    readings = read({ a, a });
    expect(readings[stereoscopeReadingCorrelation] > 0.999f && readings[stereoscopeReadingCorrelationAverage] > 0.999f
               && readings[stereoscopeReadingWidth] < 0.001f && std::abs(readings[stereoscopeReadingBalance]) < 0.01f,
           "L = R reads correlation 1, no width, centred");
    readings = read({ a, utilityScaled(a, -1.0f) });
    expect(readings[stereoscopeReadingCorrelation] < -0.999f && readings[stereoscopeReadingCorrelationAverage] < -0.999f
               && readings[stereoscopeReadingWidth] > 9.0f,
           "L = -R reads correlation -1 and all side");
    readings = read({ a, utilityScaled(a, 2.0f) });
    expect(readings[stereoscopeReadingBalance] > 5.5f && readings[stereoscopeReadingBalance] < 6.5f, "a louder right reads a positive balance");
    readings = read({ utilityScaled(a, 2.0f), a });
    expect(readings[stereoscopeReadingBalance] < -5.5f, "a louder left reads a negative balance");
    // A mono low under an anti-phase high: the bands tell them apart.
    const auto low = utilitySine(100.0, 0.4, frames);
    const auto high = utilitySine(8'000.0, 0.3, frames);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        left[i] = low[i] + high[i];
        right[i] = low[i] - high[i];
    }
    readings = read({ left, right });
    expect(readings[stereoscopeReadingLowBand] > 0.9f && readings[stereoscopeReadingHighBand] < -0.9f,
           "the band correlations tell a mono low from an anti-phase high");
    // The last pairs: a sine and its opposite, a fixed step apart.
    const auto tone = utilitySine(1'000.0, 0.8, frames);
    readings = read({ tone, utilityScaled(tone, -1.0f) });
    auto opposite = true;
    auto loud = 0.0f;
    auto recurrence = 0.0;
    const auto step = 2.0 * std::numbers::pi * 1'000.0 * stereoscopePairStrideOf(48'000.0) / 48'000.0;
    for (int pair = 0; pair < stereoscopePairCount; ++pair) {
        const auto l = readings[static_cast<std::size_t>(stereoscopeReadingPairs + 2 * pair)];
        const auto r = readings[static_cast<std::size_t>(stereoscopeReadingPairs + 2 * pair + 1)];
        opposite = opposite && l == -r;
        loud = std::max(loud, std::abs(l));
        if (pair > 0 && pair + 1 < stereoscopePairCount) {
            const auto before = readings[static_cast<std::size_t>(stereoscopeReadingPairs + 2 * (pair - 1))];
            const auto after = readings[static_cast<std::size_t>(stereoscopeReadingPairs + 2 * (pair + 1))];
            recurrence = std::max(recurrence, std::abs(before + after - 2.0 * std::cos(step) * l));
        }
    }
    expect(opposite && loud > 0.7f && recurrence < 1.0e-4, "the stereoscope keeps the last pairs in order, evenly spread");
    // One channel: a centred sound.
    readings = read({ a });
    expect(readings[stereoscopeReadingCorrelation] > 0.999f, "one channel reads as correlated");
    // No latency, no memory asked for while it plays.
    auto scope = utilityEffect(NativeDspNodeKind::stereoscope, { { "stereoscope.zoom", 4.0f }, { "stereoscope.displayMode", 1.0f } });
    (void) utilityRun(*scope, { a, b });
    std::size_t allocations = 0;
    (void) utilityRun(*scope, { a, b }, &allocations);
    expect(allocations == 0 && scope->latencySamples() == 0, "the stereoscope plays without allocating or delaying");
}

void utilityEffectTests()
{
    utilitairePassesAtRest();
    utilitaireSidesAndPolarity();
    utilitaireMonoBassAndWidth();
    utilitaireLevelDcDelayMute();
    utilitaireStaysBoundedAndSingleChannel();
    utilitaireSwitchesWithoutClicks();
    stereoscopeReadsTheImage();
}
