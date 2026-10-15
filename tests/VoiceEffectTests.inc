// The engine tests of Vocodeur, Dé-Esseur, Accordeur
// (VoiceEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); voiceEffectTests() runs them all.

// One of the three, its defaults set, then `values` by binding suffix
// ("vocoder.carrier"), prepared at 48 kHz.
std::unique_ptr<cydaw::engine::AudioProcessor> voiceEffect(const cydaw::engine::NativeDspNodeKind kind,
                                                           const std::vector<std::pair<std::string_view, float>>& values,
                                                           const std::uint32_t channels = 2, const double rate = 48'000.0)
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
        expect(parameter != nullptr, "a voice effect's parameter is known by its suffix");
        if (parameter != nullptr)
            setEffectValue(node.parameters, *parameter, value);
    }
    auto processor = makeEffectProcessor(node);
    if (processor != nullptr)
        processor->prepare({ rate, 256, channels });
    return processor;
}

// Runs a sound through in blocks of 256: `left` (and `right`, else the same)
// in, the left channel out (`rightOut` the right). With a `key`, the
// sidechain bus gets it (left = right).
std::vector<float> voiceRun(cydaw::engine::AudioProcessor& processor, const std::vector<float>& left,
                            const std::vector<float>* right = nullptr, std::vector<float>* rightOut = nullptr,
                            const std::vector<float>* key = nullptr, const std::uint32_t channels = 2)
{
    using namespace cydaw::engine;
    AudioBuffer in(channels, 256);
    AudioBuffer side(2, 256);
    AudioBuffer out(channels, 256);
    std::vector<float> result;
    result.reserve(left.size());
    if (rightOut != nullptr)
        rightOut->reserve(left.size());
    ProcessContext context;
    auto* buses = dynamic_cast<MultiBusAudioProcessor*>(&processor);
    for (std::size_t at = 0; at < left.size(); at += 256) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(256, left.size() - at));
        const auto input = in.view(count);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            input.channel(0)[frame] = left[at + frame];
            if (channels > 1)
                input.channel(1)[frame] = right != nullptr ? (*right)[at + frame] : left[at + frame];
        }
        const auto output = out.view(count);
        if (key != nullptr && buses != nullptr) {
            const auto keyed = side.view(count);
            for (std::uint32_t frame = 0; frame < count; ++frame)
                keyed.channel(0)[frame] = keyed.channel(1)[frame] = at + frame < key->size() ? (*key)[at + frame] : 0.0f;
            const std::array<AudioInputBusView, 2> views { { { 0, input }, { effectSidechainPortId, keyed } } };
            buses->processAudioBuses(views, output, context);
        } else {
            processor.process(input, output, context);
        }
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            result.push_back(output.channel(0)[frame]);
            if (rightOut != nullptr && channels > 1)
                rightOut->push_back(output.channel(1)[frame]);
        }
    }
    return result;
}

double voiceRmsDb(const std::vector<float>& signal, const double from, const double to)
{
    const auto a = std::min(signal.size(), static_cast<std::size_t>(from * 48'000.0));
    const auto b = std::min(signal.size(), static_cast<std::size_t>(to * 48'000.0));
    auto sum = 0.0;
    for (auto i = a; i < b; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return 10.0 * std::log10(sum / static_cast<double>(std::max<std::size_t>(1, b - a)) + 1.0e-30);
}

double voicePeak(const std::vector<float>& signal)
{
    auto peak = 0.0;
    for (const auto x : signal) {
        if (!std::isfinite(x))
            return 1.0e30;
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    }
    return peak;
}

// The fundamental of a stretch (Hz, 80..1000): where it best matches itself
// one period later, between samples by a parabola.
double voiceFundamental(const std::vector<float>& signal, const std::size_t from, const std::size_t to)
{
    const auto correlation = [&](const int lag) {
        auto sum = 0.0;
        auto energy = 0.0;
        for (auto i = from; i + static_cast<std::size_t>(lag) < to; ++i) {
            sum += static_cast<double>(signal[i]) * signal[i + static_cast<std::size_t>(lag)];
            energy += static_cast<double>(signal[i]) * signal[i];
        }
        return sum / std::max(1.0e-12, energy);
    };
    // The first peak nearly as high as the highest (not a multiple of it).
    std::vector<double> values(602, 0.0);
    auto highest = -2.0;
    for (int lag = 47; lag <= 601; ++lag) {
        values[static_cast<std::size_t>(lag)] = correlation(lag);
        highest = std::max(highest, values[static_cast<std::size_t>(lag)]);
    }
    auto best = 48;
    for (int lag = 48; lag <= 600; ++lag) {
        const auto v = values[static_cast<std::size_t>(lag)];
        if (v >= 0.85 * highest && v >= values[static_cast<std::size_t>(lag - 1)] && v >= values[static_cast<std::size_t>(lag + 1)]) {
            best = lag;
            break;
        }
    }
    const auto a = values[static_cast<std::size_t>(best - 1)];
    const auto b = values[static_cast<std::size_t>(best)];
    const auto c = values[static_cast<std::size_t>(best + 1)];
    const auto bend = a - 2.0 * b + c;
    const auto exact = best + (bend < 0.0 ? 0.5 * (a - c) / bend : 0.0);
    return 48'000.0 / exact;
}

std::vector<float> voiceNoise(const double seconds, const double amplitude, std::uint32_t seed)
{
    std::vector<float> signal(static_cast<std::size_t>(seconds * 48'000.0));
    for (auto& x : signal) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 23) - 1.0) * amplitude);
    }
    return signal;
}

// Something like speech: a buzz at 140 Hz (its pitch wandering) through three
// formants that move, in syllables, with a hissed consonant now and then.
std::vector<float> voiceSpeechLike(const double seconds)
{
    using namespace cydaw::engine;
    const auto count = static_cast<std::size_t>(seconds * 48'000.0);
    std::vector<float> signal(count, 0.0f);
    struct Section {
        BiquadCoefficients c;
        double z1 {}, z2 {};
        double run(const double x)
        {
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    std::array<Section, 3> formants {};
    std::array<Section, 2> hiss {};
    for (int stage = 0; stage < 2; ++stage)
        hiss[static_cast<std::size_t>(stage)].c = designEqBand(EqBandType::lowCut, 4'500.0, 0.0, 0.7071, 1, stage, 48'000.0);
    std::uint32_t seed = 12'345u;
    auto phase = 0.0;
    constexpr std::array<std::array<double, 3>, 4> vowels { { { 730.0, 1'090.0, 2'440.0 }, { 270.0, 2'290.0, 3'010.0 },
                                                               { 530.0, 1'840.0, 2'480.0 }, { 300.0, 870.0, 2'240.0 } } };
    for (std::size_t i = 0; i < count; ++i) {
        const auto t = static_cast<double>(i) / 48'000.0;
        const auto syllable = static_cast<std::size_t>(t / 0.3);
        const auto inSyllable = std::fmod(t, 0.3);
        if (i % 4'800 == 0 || i == 0) {
            const auto& vowel = vowels[syllable % vowels.size()];
            for (std::size_t f = 0; f < 3; ++f)
                formants[f].c = designEqBand(EqBandType::bandPass, vowel[f], 0.0, 6.0, 0, 0, 48'000.0);
        }
        const auto hz = 140.0 * (1.0 + 0.03 * std::sin(2.0 * std::numbers::pi * 0.7 * t));
        phase += hz / 48'000.0;
        const auto pulse = phase >= 1.0 ? 1.0 : 0.0;
        if (phase >= 1.0)
            phase -= 1.0;
        const auto source = pulse * 6.0;
        const auto voiced = formants[0].run(source) + 0.6 * formants[1].run(source) + 0.3 * formants[2].run(source);
        seed = seed * 1'664'525u + 1'013'904'223u;
        const auto white = static_cast<double>(seed >> 8) / static_cast<double>(1u << 23) - 1.0;
        const auto hissed = hiss[1].run(hiss[0].run(white));
        // A vowel for 220 ms, then 80 ms of hiss or of silence.
        const auto envelope = inSyllable < 0.22 ? std::sin(std::numbers::pi * inSyllable / 0.22) : 0.0;
        const auto consonant = inSyllable >= 0.22 && syllable % 2 == 0 ? 0.25 * std::sin(std::numbers::pi * (inSyllable - 0.22) / 0.08) : 0.0;
        signal[i] = static_cast<float>(voiced * envelope + hissed * consonant);
    }
    return signal;
}

// ---- Vocodeur ----

void vocoderSpeaksThroughItsCarrier()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const auto silence = std::vector<float>(48'000, 0.0f);
    const auto key = sine(330.0, 0.3, 1.0);
    // Nothing said, nothing heard, whatever the carrier.
    for (int carrier = 0; carrier < voice::vocoderCarrierCount; ++carrier) {
        auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", static_cast<float>(carrier) } });
        expect(vocoder != nullptr, "the Vocoder is made");
        if (vocoder == nullptr)
            return;
        const auto out = voiceRun(*vocoder, silence, nullptr, nullptr, carrier == voice::vocoderExternalCarrier ? &key : nullptr);
        expect(voicePeak(out) < 1.0e-4, "a silent voice gives a silent Vocoder (carrier " + std::to_string(carrier) + ")");
    }
    // Its output follows the voice's envelope: on, off, on.
    {
        auto noise = voiceNoise(1.5, 0.3, 77u);
        for (std::size_t i = 24'000; i < 48'000; ++i)
            noise[i] = 0.0f;
        auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", 1.0f }, { "vocoder.release", 20.0f } });
        const auto out = voiceRun(*vocoder, noise);
        const auto on = voiceRmsDb(out, 0.2, 0.45);
        const auto off = voiceRmsDb(out, 0.7, 0.95);
        const auto again = voiceRmsDb(out, 1.2, 1.45);
        expect(on > -30.0 && again > -30.0 && off < on - 50.0, "the Vocoder's output follows a gated voice (on " + std::to_string(on)
                                                                     + " dB, off " + std::to_string(off) + " dB)");
    }
    // Following the voice: a 220 Hz voice makes a 220 Hz carrier.
    {
        auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", 2.0f }, { "vocoder.pitch", 48.0f } });
        const auto out = voiceRun(*vocoder, sine(220.0, 0.3, 1.5));
        const auto at220 = toneLevelDb(out, 220.0, 48'000, 72'000);
        auto strongest = true;
        for (const auto hz : { 130.81, 165.0, 196.0, 247.0, 261.6, 330.0, 440.0 })
            strongest = strongest && toneLevelDb(out, hz, 48'000, 72'000) < at220 - 12.0;
        const auto heard = static_cast<float>(voiceFundamental(out, 48'000, 72'000));
        float readings[voice::vocoderReadingCount] {};
        const auto shown = vocoder->readings(readings, voice::vocoderReadingCount);
        expect(strongest && std::abs(heard - 220.0f) < 6.0f,
               "the Vocoder following the voice sings at its pitch (" + std::to_string(heard) + " Hz)");
        expect(shown == static_cast<std::size_t>(voice::vocoderReadingEnvelopes + 20) && std::abs(readings[0] - 220.0f) < 4.0f
                   && readings[1] == 20.0f,
               "the Vocoder shows its carrier's pitch and its bands");
    }
    // The saw on a note: the note's fundamental, whatever the voice's pitch.
    {
        auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", 1.0f }, { "vocoder.pitch", 57.0f }, { "vocoder.detune", 0.0f } });
        const auto out = voiceRun(*vocoder, voiceNoise(1.0, 0.3, 5u));
        expect(toneLevelDb(out, 220.0, 24'000, 48'000) > toneLevelDb(out, 233.08, 24'000, 48'000) + 15.0,
               "the Vocoder's saw plays its note");
    }
    // External: the sidechain is the carrier; without it, noise.
    {
        const auto voiceSound = voiceNoise(1.0, 0.3, 9u);
        const auto tone = sine(600.0, 0.3, 1.0);
        auto keyed = voiceEffect(K::vocoder, { { "vocoder.carrier", 3.0f } });
        const auto withKey = voiceRun(*keyed, voiceSound, nullptr, nullptr, &tone);
        auto unkeyed = voiceEffect(K::vocoder, { { "vocoder.carrier", 3.0f } });
        const auto withoutKey = voiceRun(*unkeyed, voiceSound);
        const auto keyedTone = toneLevelDb(withKey, 600.0, 24'000, 48'000);
        expect(keyedTone > toneLevelDb(withoutKey, 600.0, 24'000, 48'000) + 15.0 && keyedTone > toneLevelDb(withKey, 900.0, 24'000, 48'000) + 15.0,
               "the external carrier is the sidechain's sound");
        expect(voiceRmsDb(withoutKey, 0.3, 1.0) > -40.0, "with nothing wired, the external carrier falls back to noise");
    }
    // A voice comes out near its own level, whatever the carrier.
    {
        const auto speech = voiceSpeechLike(3.0);
        const auto in = voiceRmsDb(speech, 0.3, 3.0);
        for (int carrier = 0; carrier < 3; ++carrier) {
            auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", static_cast<float>(carrier) } });
            std::vector<float> right;
            const auto left = voiceRun(*vocoder, speech, nullptr, &right);
            const auto level = 10.0 * std::log10(0.5 * (std::pow(10.0, voiceRmsDb(left, 0.3, 3.0) / 10.0)
                                                        + std::pow(10.0, voiceRmsDb(right, 0.3, 3.0) / 10.0)))
                               - in;
            expect(std::abs(level) < 3.0, "the Vocoder keeps a voice's level (carrier " + std::to_string(carrier) + ": "
                                              + std::to_string(level) + " dB)");
        }
    }
    // No mix: the voice as it was; mono works.
    {
        const auto speech = voiceSpeechLike(0.5);
        auto dry = voiceEffect(K::vocoder, { { "vocoder.mix", 0.0f } });
        expect(voiceRun(*dry, speech) == speech, "the Vocoder at no mix leaves the voice");
        auto mono = voiceEffect(K::vocoder, {}, 1);
        const auto out = voiceRun(*mono, speech, nullptr, nullptr, nullptr, 1);
        expect(voicePeak(out) < 4.0 && voiceRmsDb(out, 0.1, 0.5) > -40.0, "the Vocoder works in mono");
    }
    // Every setting at its ends: finite, bounded; band count changes on the fly.
    {
        const auto speech = voiceSpeechLike(0.6);
        auto worst = 0.0;
        for (int carrier = 0; carrier < voice::vocoderCarrierCount; ++carrier)
            for (int corner = 0; corner < 8; ++corner) {
                const auto a = (corner & 1) != 0;
                const auto b = (corner & 2) != 0;
                const auto c = (corner & 4) != 0;
                auto vocoder = voiceEffect(K::vocoder, { { "vocoder.carrier", static_cast<float>(carrier) },
                                                         { "vocoder.bands", a ? 40.0f : 4.0f },
                                                         { "vocoder.lowBand", b ? 1'000.0f : 50.0f },
                                                         { "vocoder.highBand", c ? 18'000.0f : 2'000.0f },
                                                         { "vocoder.bandwidth", a == b ? 0.25f : 2.0f },
                                                         { "vocoder.formant", c ? 12.0f : -12.0f },
                                                         { "vocoder.attack", b ? 100.0f : 0.5f },
                                                         { "vocoder.release", a ? 1'000.0f : 5.0f },
                                                         { "vocoder.unvoiced", 1.0f },
                                                         { "vocoder.tone", a ? 1.0f : -1.0f },
                                                         { "vocoder.output", 24.0f },
                                                         { "vocoder.gate", c ? -20.0f : -90.0f },
                                                         { "vocoder.stereo", b ? 1.0f : 0.0f },
                                                         { "vocoder.detune", c ? 1.0f : 0.0f },
                                                         { "vocoder.pitch", a ? 96.0f : 24.0f } });
                const auto loudKey = voiceNoise(0.6, 0.9, 3u);
                worst = std::max(worst, voicePeak(voiceRun(*vocoder, speech, nullptr, nullptr, &loudKey)));
            }
        expect(worst < 64.0, "every Vocoder setting stays finite and bounded (" + std::to_string(worst) + ")");
        expect(voiceEffect(K::vocoder, {})->latencySamples() == 0, "the Vocoder has no latency");
    }
}

// ---- Dé-Esseur ----

void deEsserTakesTheHissDown()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const auto amplitude = std::pow(10.0, -10.0 / 20.0);
    // Bursts of 6 kHz: 150 ms on, 150 ms off.
    auto bursts = sine(6'000.0, amplitude, 1.2);
    for (std::size_t i = 0; i < bursts.size(); ++i)
        if ((i / 7'200) % 2 == 1)
            bursts[i] = 0.0f;
    const auto burstDb = [](const std::vector<float>& x) {
        // The middle of the third burst (its attack past).
        return toneLevelDb(x, 6'000.0, 30'000, 35'000);
    };
    const auto inDb = burstDb(bursts);
    for (int mode = 0; mode < voice::deEsserModeCount; ++mode) {
        auto deEsser = voiceEffect(K::deEsser, { { "deesser.mode", static_cast<float>(mode) } });
        expect(deEsser != nullptr, "the De-esser is made");
        if (deEsser == nullptr)
            return;
        const auto taken = inDb - burstDb(voiceRun(*deEsser, bursts));
        expect(taken > 8.0 && taken < 11.0, "the De-esser takes 6 kHz bursts down by its range (mode " + std::to_string(mode) + ": "
                                                + std::to_string(taken) + " dB)");
        float readings[voice::deEsserReadingCount] {};
        (void) voiceRun(*deEsser, std::vector<float>(bursts.begin(), bursts.begin() + 33'000));
        expect(deEsser->readings(readings, voice::deEsserReadingCount) == 2 && std::abs(readings[0] - 10.0f) < 0.5f && readings[1] > -20.0f,
               "the De-esser shows how much it takes and what it hears");
        // A 200 Hz tone as loud is left as it was.
        auto low = voiceEffect(K::deEsser, { { "deesser.mode", static_cast<float>(mode) } });
        const auto tone = sine(200.0, amplitude, 1.0);
        const auto change = toneLevelDb(voiceRun(*low, tone), 200.0, 24'000, 48'000) - toneLevelDb(tone, 200.0, 24'000, 48'000);
        expect(std::abs(change) < 0.5, "the De-esser leaves a low tone (" + std::to_string(change) + " dB)");
    }
    // The band only: in a mix of 200 Hz and 6 kHz, the 200 Hz stays.
    {
        auto mix = sine(200.0, amplitude, 1.0);
        const auto high = sine(6'000.0, amplitude, 1.0);
        for (std::size_t i = 0; i < mix.size(); ++i)
            mix[i] += high[i];
        auto deEsser = voiceEffect(K::deEsser, { { "deesser.mode", 0.0f } });
        const auto out = voiceRun(*deEsser, mix);
        const auto lowChange = toneLevelDb(out, 200.0, 24'000, 48'000) - toneLevelDb(mix, 200.0, 24'000, 48'000);
        const auto highChange = toneLevelDb(out, 6'000.0, 24'000, 48'000) - toneLevelDb(mix, 6'000.0, 24'000, 48'000);
        expect(std::abs(lowChange) < 0.5 && highChange < -7.0, "the De-esser's band only keeps the lows and takes the hiss ("
                                                                    + std::to_string(lowChange) + ", " + std::to_string(highChange) + " dB)");
        // Listening: mostly the band.
        auto listen = voiceEffect(K::deEsser, { { "deesser.listen", 1.0f } });
        const auto heard = voiceRun(*listen, mix);
        expect(toneLevelDb(heard, 6'000.0, 24'000, 48'000) > toneLevelDb(heard, 200.0, 24'000, 48'000) + 40.0,
               "the De-esser's listen plays what it hears");
        // The band detector hears around its frequency, not an octave and a half below.
        auto band = voiceEffect(K::deEsser, { { "deesser.detectorType", 1.0f }, { "deesser.mode", 1.0f }, { "deesser.bandwidth", 1.0f } });
        const auto twoKilo = sine(2'000.0, amplitude, 1.0);
        const auto bandChange = toneLevelDb(voiceRun(*band, twoKilo), 2'000.0, 24'000, 48'000) - toneLevelDb(twoKilo, 2'000.0, 24'000, 48'000);
        expect(bandChange > -2.0, "the De-esser's band detector lets a tone outside its band be");
    }
    // Linked: a hiss on the left takes the right down too; unlinked, not.
    {
        const auto hiss = sine(7'000.0, amplitude, 1.0);
        const auto voiceTone = sine(300.0, 0.1, 1.0);
        for (const auto link : { 0.0f, 1.0f }) {
            auto deEsser = voiceEffect(K::deEsser, { { "deesser.mode", 1.0f }, { "deesser.link", link } });
            std::vector<float> right;
            (void) voiceRun(*deEsser, hiss, &voiceTone, &right);
            const auto change = toneLevelDb(right, 300.0, 24'000, 48'000) - toneLevelDb(voiceTone, 300.0, 24'000, 48'000);
            expect(link > 0.5 ? change < -8.0 : std::abs(change) < 0.5, "the De-esser's link takes both sides or one ("
                                                                             + std::to_string(change) + " dB)");
        }
    }
    // Every setting at its ends: finite, bounded; mono works.
    {
        const auto noise = voiceNoise(0.5, 0.9, 21u);
        auto worst = 0.0;
        for (int corner = 0; corner < 64; ++corner) {
            const auto bit = [corner](const int n) { return (corner >> n) & 1; };
            auto deEsser = voiceEffect(K::deEsser, { { "deesser.threshold", bit(0) ? 0.0f : -60.0f },
                                                     { "deesser.frequency", bit(1) ? 12'000.0f : 2'000.0f },
                                                     { "deesser.range", bit(2) ? 24.0f : 0.0f },
                                                     { "deesser.release", bit(3) ? 300.0f : 10.0f },
                                                     { "deesser.output", 24.0f },
                                                     { "deesser.mode", static_cast<float>(bit(4)) },
                                                     { "deesser.detectorType", static_cast<float>(bit(5)) },
                                                     { "deesser.listen", static_cast<float>(bit(0) & bit(3)) },
                                                     { "deesser.bandwidth", bit(1) ? 3.0f : 0.3f },
                                                     { "deesser.attack", bit(2) ? 20.0f : 0.1f },
                                                     { "deesser.link", static_cast<float>(bit(3)) } });
            worst = std::max(worst, voicePeak(voiceRun(*deEsser, noise)));
        }
        expect(worst < 40.0, "every De-esser setting stays finite and bounded (" + std::to_string(worst) + ")");
        auto mono = voiceEffect(K::deEsser, {}, 1);
        const auto out = voiceRun(*mono, bursts, nullptr, nullptr, nullptr, 1);
        expect(inDb - burstDb(out) > 7.0, "the De-esser works in mono");
        expect(voiceEffect(K::deEsser, {})->latencySamples() == 0, "the De-esser has no latency");
    }
}

// ---- Accordeur ----

void tunerFindsTheNote()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const auto measure = [](const std::vector<float>& sound, const float reference) {
        auto tuner = voiceEffect(K::tuner, { { "tuner.reference", reference } });
        std::array<float, voice::tunerReadingCount> readings {};
        if (tuner == nullptr)
            return readings;
        (void) voiceRun(*tuner, sound);
        (void) tuner->readings(readings.data(), readings.size());
        return readings;
    };
    struct Case {
        double hz;
        int note;
        const char* name;
    };
    for (const auto& [hz, note, name] : { Case { 440.0, 69, "A4" }, Case { 82.41, 40, "E2" }, Case { 1'046.5, 84, "C6" },
                                          Case { 110.0, 45, "A2" }, Case { 329.63, 64, "E4" } }) {
        const auto readings = measure(sine(hz, 0.3, 0.6), 440.0f);
        const auto expected = 100.0 * (69.0 + 12.0 * std::log2(hz / 440.0) - note);
        expect(static_cast<int>(readings[voice::tunerReadingNote]) == note
                   && std::abs(readings[voice::tunerReadingCents] - expected) < (note == 69 ? 2.0 : 3.0)
                   && std::abs(readings[voice::tunerReadingHz] - hz) < hz * 0.002,
               std::string("the Tuner finds ") + name + " (" + std::to_string(readings[voice::tunerReadingHz]) + " Hz, note "
                   + std::to_string(readings[voice::tunerReadingNote]) + ", " + std::to_string(readings[voice::tunerReadingCents])
                   + " cents)");
    }
    // A sound rich in harmonics: its fundamental, not an octave off.
    {
        std::vector<float> saw(28'800);
        for (std::size_t i = 0; i < saw.size(); ++i)
            saw[i] = static_cast<float>(0.4 * (std::fmod(110.0 * static_cast<double>(i) / 48'000.0, 1.0) * 2.0 - 1.0));
        const auto readings = measure(saw, 440.0f);
        expect(static_cast<int>(readings[voice::tunerReadingNote]) == 45 && std::abs(readings[voice::tunerReadingCents]) < 3.0f,
               "the Tuner finds a saw's fundamental");
    }
    // Slightly sharp, and against another reference.
    {
        const auto sharp = measure(sine(440.0 * std::pow(2.0, 12.0 / 1'200.0), 0.3, 0.6), 440.0f);
        expect(std::abs(sharp[voice::tunerReadingCents] - 12.0f) < 2.0f, "the Tuner reads 12 cents sharp");
        const auto other = measure(sine(440.0, 0.3, 0.6), 432.0f);
        const auto expected = static_cast<float>(100.0 * 12.0 * std::log2(440.0 / 432.0));
        expect(static_cast<int>(other[voice::tunerReadingNote]) == 69 && std::abs(other[voice::tunerReadingCents] - expected) < 2.0f,
               "the Tuner's reference moves the cents (" + std::to_string(other[voice::tunerReadingCents]) + ")");
    }
    // Silence: no pitch, even after a note.
    {
        auto sound = sine(440.0, 0.3, 0.5);
        sound.resize(sound.size() + 48'000, 0.0f);
        const auto readings = measure(sound, 440.0f);
        expect(readings[voice::tunerReadingHz] == 0.0f && readings[voice::tunerReadingLevel] < -40.0f,
               "the Tuner shows no pitch in silence");
        const auto loud = measure(sine(440.0, 0.5, 0.5), 440.0f);
        expect(std::abs(loud[voice::tunerReadingLevel] - static_cast<float>(20.0 * std::log10(0.5 / std::sqrt(2.0)))) < 1.0f
                   && loud[voice::tunerReadingConfidence] > 0.8f,
               "the Tuner shows the level and how sure it is");
    }
    // The sound passes untouched; muted, nothing passes but it still listens.
    {
        const auto left = voiceNoise(0.3, 0.5, 31u);
        const auto right = sine(440.0, 0.4, 0.3);
        auto tuner = voiceEffect(K::tuner, {});
        std::vector<float> rightOut;
        expect(voiceRun(*tuner, left, &right, &rightOut) == left && rightOut == right, "the Tuner passes the sound untouched");
        auto muted = voiceEffect(K::tuner, { { "tuner.mute", 1.0f } });
        expect(voicePeak(voiceRun(*muted, right)) == 0.0, "the muted Tuner lets nothing through");
        std::array<float, voice::tunerReadingCount> readings {};
        (void) muted->readings(readings.data(), readings.size());
        expect(static_cast<int>(readings[voice::tunerReadingNote]) == 69, "the muted Tuner still listens");
        auto mono = voiceEffect(K::tuner, {}, 1);
        expect(voiceRun(*mono, right, nullptr, nullptr, nullptr, 1) == right, "the Tuner works in mono");
        expect(tuner->latencySamples() == 0, "the Tuner has no latency");
    }
}

// At other rates, each stays finite and does its work.
void voiceEffectsAtOtherRates()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    for (const auto rate : { 44'100.0, 96'000.0 }) {
        const auto voiceSound = sine(220.0, 0.3, 0.8, rate);
        auto vocoder = voiceEffect(K::vocoder, { { "vocoder.highBand", 18'000.0f }, { "vocoder.formant", 12.0f } }, 2, rate);
        const auto vocoded = voiceRun(*vocoder, voiceSound);
        expect(voicePeak(vocoded) < 4.0 && toneLevelDb(vocoded, 220.0, voiceSound.size() / 2, voiceSound.size(), rate) > -40.0,
               "the Vocoder works at " + std::to_string(static_cast<int>(rate)) + " Hz");
        auto deEsser = voiceEffect(K::deEsser, { { "deesser.frequency", 12'000.0f } }, 2, rate);
        expect(voicePeak(voiceRun(*deEsser, voiceSound)) < 1.0, "the De-esser works at another rate");
        auto tuner = voiceEffect(K::tuner, {}, 2, rate);
        (void) voiceRun(*tuner, voiceSound);
        std::array<float, voice::tunerReadingCount> readings {};
        (void) tuner->readings(readings.data(), readings.size());
        expect(static_cast<int>(readings[voice::tunerReadingNote]) == 57 && std::abs(readings[voice::tunerReadingCents]) < 3.0f,
               "the Tuner finds A3 at " + std::to_string(static_cast<int>(rate)) + " Hz (" + std::to_string(readings[voice::tunerReadingCents])
                   + " cents)");
    }
    // With nothing to take, the De-esser leaves the sound as it was.
    const auto speech = voiceSpeechLike(0.5);
    for (int mode = 0; mode < voice::deEsserModeCount; ++mode) {
        auto deEsser = voiceEffect(K::deEsser, { { "deesser.range", 0.0f }, { "deesser.mode", static_cast<float>(mode) } });
        const auto out = voiceRun(*deEsser, speech);
        auto apart = 0.0;
        for (std::size_t i = 0; i < out.size(); ++i)
            apart = std::max(apart, static_cast<double>(std::abs(out[i] - speech[i])));
        expect(apart < 1.0e-5, "the De-esser with no range leaves the sound, without delay");
    }
}

// None of them asks for memory while it plays.
void voiceEffectsDoNotAllocate()
{
    using namespace cydaw::engine;
    using K = NativeDspNodeKind;
    const auto speech = voiceSpeechLike(0.3);
    for (const auto kind : { K::vocoder, K::deEsser, K::tuner }) {
        auto processor = voiceEffect(kind, {});
        if (processor == nullptr)
            continue;
        AudioBuffer in(2, 256);
        AudioBuffer out(2, 256);
        ProcessContext context;
        allocation_probe::count.store(0, std::memory_order_relaxed);
        allocation_probe::enabled.store(true, std::memory_order_relaxed);
        for (std::size_t at = 0; at + 256 <= speech.size(); at += 256) {
            const auto input = in.view(256);
            for (std::uint32_t frame = 0; frame < 256; ++frame)
                input.channel(0)[frame] = input.channel(1)[frame] = speech[at + frame];
            processor->process(input, out.view(256), context);
        }
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        expect(allocation_probe::count.load(std::memory_order_relaxed) == 0, "a voice effect plays without allocating");
    }
}

void voiceEffectTests()
{
    vocoderSpeaksThroughItsCarrier();
    deEsserTakesTheHissDown();
    tunerFindsTheNote();
    voiceEffectsAtOtherRates();
    voiceEffectsDoNotAllocate();
}
