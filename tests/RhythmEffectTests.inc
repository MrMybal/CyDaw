// The engine tests of Grains, Saccade, Boucleur
// (RhythmEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); rhythmEffectTests() runs them all.

// One of the three effects, its defaults set then `values` (binding
// suffixes), run block by block with its values changed between blocks and
// the transport played or not.
struct RhythmRig {
    cydaw::engine::NativeDspNode node;
    std::unique_ptr<cydaw::engine::AudioProcessor> processor;
    cydaw::engine::ProcessContext context;
    cydaw::engine::AudioBuffer in { 2, 512 };
    cydaw::engine::AudioBuffer out { 2, 512 };
    // Samples processed, and the transport advanced with them when playing.
    std::size_t done {};
    bool playing {};
    double startBeat {};

    RhythmRig(const cydaw::engine::NativeDspNodeKind kind, const std::vector<std::pair<std::string_view, float>>& values)
    {
        using namespace cydaw::engine;
        node.id = 1;
        node.kind = kind;
        if (const auto* effect = effectForKind(kind))
            applyEffectDefaults(*effect, node.parameters);
        for (const auto& [suffix, value] : values)
            if (const auto* parameter = effectParameterForSuffix(suffix))
                setEffectValue(node.parameters, *parameter, value);
        processor = makeEffectProcessor(node);
        if (processor != nullptr)
            processor->prepare({ 48'000.0, 512, 2 });
    }

    void set(const std::string_view suffix, const float value)
    {
        using namespace cydaw::engine;
        if (const auto* parameter = effectParameterForSuffix(suffix))
            setEffectValue(node.parameters, *parameter, value);
        if (processor != nullptr)
            updateEffectProcessor(*processor, node);
    }

    // Runs `left` (and `right`, or `left` again) through in blocks of 480;
    // the outputs appended.
    void run(const std::vector<float>& left, std::vector<float>& outLeft, std::vector<float>* outRight = nullptr,
             const std::vector<float>* right = nullptr, const std::uint32_t block = 480)
    {
        using namespace cydaw::engine;
        if (processor == nullptr)
            return;
        for (std::size_t at = 0; at < left.size(); at += block) {
            const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(block, left.size() - at));
            const auto input = in.view(count);
            for (std::uint32_t frame = 0; frame < count; ++frame) {
                input.channel(0)[frame] = left[at + frame];
                input.channel(1)[frame] = right != nullptr ? (*right)[at + frame] : left[at + frame];
            }
            context.transport.state = playing ? PlaybackState::playing : PlaybackState::stopped;
            context.transport.beatsPerMinute = 120.0;
            context.transport.quarterNotePosition = startBeat + static_cast<double>(done) / 24'000.0;
            const auto output = out.view(count);
            processor->process(input, output, context);
            for (std::uint32_t frame = 0; frame < count; ++frame) {
                outLeft.push_back(output.channel(0)[frame]);
                if (outRight != nullptr)
                    outRight->push_back(output.channel(1)[frame]);
            }
            done += count;
        }
    }

    std::vector<float> runAll(const std::vector<float>& signal)
    {
        std::vector<float> result;
        result.reserve(signal.size());
        run(signal, result);
        return result;
    }

    [[nodiscard]] std::vector<float> readings() const
    {
        std::vector<float> values(1'024, 0.0f);
        values.resize(processor != nullptr ? processor->readings(values.data(), values.size()) : 0);
        return values;
    }
};

std::vector<float> rhythmNoise(const std::size_t count, const double amplitude, std::uint32_t seed = 11u)
{
    std::vector<float> noise(count);
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * amplitude);
    }
    return noise;
}

std::vector<float> rhythmSine(const double hz, const double amplitude, const std::size_t count)
{
    std::vector<float> signal(count);
    for (std::size_t i = 0; i < count; ++i)
        signal[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48'000.0));
    return signal;
}

std::vector<float> rhythmJoined(std::vector<float> first, const std::vector<float>& second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

// A tone's level (dB) as the power of its short stretches averaged: grains
// of random phases add up there as they are heard.
double rhythmToneDb(const std::vector<float>& signal, const double hz, const std::size_t from, const std::size_t to)
{
    auto power = 0.0;
    auto stretches = 0;
    for (auto at = from; at + 4'800 <= std::min(to, signal.size()); at += 4'800) {
        const auto db = toneLevelDb(signal, hz, at, at + 4'800);
        power += std::pow(10.0, db / 10.0);
        ++stretches;
    }
    return 10.0 * std::log10(power / std::max(1, stretches) + 1.0e-30);
}

double rhythmPeak(const std::vector<float>& signal, const std::size_t from = 0, const std::size_t to = static_cast<std::size_t>(-1))
{
    auto peak = 0.0;
    for (auto i = from; i < std::min(to, signal.size()); ++i) {
        if (!std::isfinite(signal[i]))
            return 1.0e300;
        peak = std::max(peak, static_cast<double>(std::abs(signal[i])));
    }
    return peak;
}

double rhythmCorrelation(const std::vector<float>& a, const std::size_t fromA, const std::vector<float>& b, const std::size_t fromB,
                         const std::size_t count)
{
    auto ab = 0.0;
    auto aa = 0.0;
    auto bb = 0.0;
    for (std::size_t i = 0; i < count && fromA + i < a.size() && fromB + i < b.size(); ++i) {
        ab += static_cast<double>(a[fromA + i]) * b[fromB + i];
        aa += static_cast<double>(a[fromA + i]) * a[fromA + i];
        bb += static_cast<double>(b[fromB + i]) * b[fromB + i];
    }
    return ab / std::sqrt(aa * bb + 1.0e-30);
}

void grainsScattersEchoes()
{
    using namespace cydaw::engine;
    using Kind = NativeDspNodeKind;
    // An impulse comes back at the time: every grain reads it from there.
    {
        RhythmRig rig(Kind::grains, { { "grains.time", 250.0f }, { "grains.spray", 0.0f }, { "grains.feedback", 0.0f }, { "grains.mix", 1.0f },
                                      { "grains.stereo", 0.0f }, { "grains.highCut", 20'000.0f } });
        std::vector<float> impulse(48'000, 0.0f);
        impulse[0] = 1.0f;
        const auto out = rig.runAll(impulse);
        std::size_t loudest = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs(out[i]) > std::abs(out[loudest]))
                loudest = i;
        const auto around = std::max(rhythmPeak(out, 0, 11'980), rhythmPeak(out, 12'040));
        expect(loudest >= 11'995 && loudest <= 12'010 && std::abs(out[loudest]) > 0.3, "Grains brings an impulse back at its time");
        expect(around < 0.02, "Grains' grains all read from the time when nothing scatters them");
    }
    // An octave up: a 300 Hz sine's echo is at 600 Hz.
    {
        RhythmRig rig(Kind::grains, { { "grains.pitch", 12.0f }, { "grains.feedback", 0.0f }, { "grains.mix", 1.0f } });
        const auto out = rig.runAll(rhythmSine(300.0, 0.5, 96'000));
        const auto octave = rhythmToneDb(out, 600.0, 24'000, 96'000);
        const auto unison = rhythmToneDb(out, 300.0, 24'000, 96'000);
        expect(octave > -30.0 && octave > unison + 15.0, "Grains pitched +12 plays a 300 Hz sine at 600 Hz");
    }
    // Frozen, the buffer keeps what it held: a new sound does not get in.
    for (const auto frozen : { true, false }) {
        RhythmRig rig(Kind::grains, { { "grains.feedback", 0.0f }, { "grains.mix", 1.0f } });
        std::vector<float> out;
        rig.run(rhythmSine(300.0, 0.5, 6 * 48'000), out);
        rig.set("grains.freeze", frozen ? 1.0f : 0.0f);
        rig.run(rhythmSine(440.0, 0.5, 3 * 48'000), out);
        const auto held = rhythmToneDb(out, 300.0, static_cast<std::size_t>(6.5 * 48'000), 9 * 48'000);
        const auto fresh = rhythmToneDb(out, 440.0, static_cast<std::size_t>(6.5 * 48'000), 9 * 48'000);
        if (frozen)
            expect(held > -30.0 && held > fresh + 15.0, "Grains frozen holds its buffer");
        else
            expect(fresh > -30.0 && fresh > held + 15.0, "Grains unfrozen takes the new sound");
    }
    // Feedback at its top: it rings on and stays bounded.
    {
        RhythmRig rig(Kind::grains, { { "grains.feedback", 0.95f }, { "grains.mix", 1.0f }, { "grains.spray", 0.0f } });
        const auto out = rig.runAll(rhythmJoined(rhythmNoise(24'000, 0.5), std::vector<float>(8 * 48'000, 0.0f)));
        expect(rhythmPeak(out) < 3.0, "Grains at feedback 0.95 stays bounded");
        expect(rhythmPeak(out, 3 * 48'000, 4 * 48'000) > 1.0e-3, "Grains' feedback keeps the echoes coming");
        RhythmRig loud(Kind::grains, { { "grains.feedback", 0.95f }, { "grains.mix", 1.0f }, { "grains.density", 100.0f },
                                       { "grains.size", 500.0f }, { "grains.spray", 0.0f } });
        expect(rhythmPeak(loud.runAll(rhythmNoise(5 * 48'000, 0.7))) < 3.0, "Grains fed all along at feedback 0.95 stays bounded");
    }
    // Dense and long: the pool full, still sound.
    {
        RhythmRig rig(Kind::grains, { { "grains.density", 100.0f }, { "grains.size", 500.0f }, { "grains.mix", 1.0f } });
        const auto out = rig.runAll(rhythmNoise(3 * 48'000, 0.5));
        const auto peak = rhythmPeak(out);
        expect(std::isfinite(peak) && peak < 3.0 && peak > 0.05, "Grains at density 100 and size 500 stays finite");
        const auto shown = rig.readings();
        expect(shown.size() == static_cast<std::size_t>(rhythm::grainsReadingCount) && shown[0] >= 1.0f && shown[0] <= 32.0f,
               "Grains shows its sounding grains");
    }
    // Every setting at its ends, mono and stereo: finite, bounded.
    for (const auto top : { false, true })
        for (const auto freeze : { 0.0f, 1.0f }) {
            RhythmRig rig(Kind::grains, { { "grains.time", top ? 2'000.0f : 1.0f }, { "grains.feedback", top ? 0.95f : 0.0f },
                                          { "grains.pitch", top ? 24.0f : -24.0f }, { "grains.spray", top ? 500.0f : 0.0f },
                                          { "grains.density", top ? 100.0f : 1.0f }, { "grains.pitchRandom", top ? 12.0f : 0.0f },
                                          { "grains.mix", top ? 1.0f : 0.0f }, { "grains.output", top ? 24.0f : -24.0f },
                                          { "grains.sync", top ? 12.0f : 0.0f }, { "grains.size", top ? 500.0f : 10.0f },
                                          { "grains.stereo", top ? 1.0f : 0.0f }, { "grains.reverseChance", top ? 1.0f : 0.0f },
                                          { "grains.highCut", top ? 20'000.0f : 1'000.0f } });
            std::vector<float> out;
            rig.run(rhythmNoise(48'000, 0.5), out);
            rig.set("grains.freeze", freeze);
            rig.run(rhythmNoise(2 * 48'000, 0.5, 3u), out);
            const auto peak = rhythmPeak(out);
            expect(std::isfinite(peak) && peak < (top ? 500.0 : 4.0), "Grains at its extremes stays finite");
        }
    {
        RhythmRig rig(Kind::grains, { { "grains.reverseChance", 0.5f }, { "grains.pitchRandom", 7.0f }, { "grains.mix", 1.0f } });
        rig.processor->prepare({ 48'000.0, 512, 1 });
        cydaw::engine::AudioBuffer mono(1, 512);
        cydaw::engine::AudioBuffer monoOut(1, 512);
        const auto noise = rhythmNoise(512, 0.5);
        auto peak = 0.0;
        for (int block = 0; block < 200; ++block) {
            auto view = mono.view(512);
            std::copy(noise.begin(), noise.end(), view.channel(0));
            auto outView = monoOut.view(512);
            rig.processor->process(view, outView, rig.context);
            for (std::uint32_t i = 0; i < 512; ++i)
                peak = std::max(peak, std::isfinite(outView.channel(0)[i]) ? static_cast<double>(std::abs(outView.channel(0)[i])) : 1.0e9);
        }
        expect(peak < 4.0 && peak > 0.01, "Grains plays in mono");
    }
    // At no mix, the sound as it was; no latency.
    {
        RhythmRig rig(Kind::grains, { { "grains.mix", 0.0f } });
        const auto noise = rhythmNoise(48'000, 0.3);
        expect(rig.runAll(noise) == noise && rig.processor->latencySamples() == 0, "Grains at no mix leaves the sound");
    }
}

void saccadeRepeatsOnTheBeat()
{
    using namespace cydaw::engine;
    using Kind = NativeDspNodeKind;
    constexpr std::size_t bar = 96'000;
    constexpr std::size_t slice = 6'000;
    constexpr std::size_t edge = 104;
    // Inserted, at the bar's start, a sixteenth repeated for a beat: the
    // output plays the bar's first sixteenth over and over, then the sound.
    {
        RhythmRig rig(Kind::saccade, { { "saccade.offset", 0.0f }, { "saccade.gate", 4.0f }, { "saccade.grid", 3.0f }, { "saccade.chance", 1.0f },
                                       { "saccade.mode", 1.0f }, { "saccade.interval", 12.0f } });
        rig.playing = true;
        const auto noise = rhythmNoise(3 * bar, 0.5);
        const auto out = rig.runAll(noise);
        auto repeated = 0.0;
        auto after = 0.0;
        for (std::size_t start = 0; start < 3 * bar; start += bar) {
            for (std::size_t n = 0; n < 4 * slice; ++n) {
                if (n >= slice && n % slice < edge)
                    continue;
                repeated = std::max(repeated, static_cast<double>(std::abs(out[start + n] - noise[start + n % slice])));
            }
            for (auto n = 4 * slice + edge; n < bar; ++n)
                after = std::max(after, static_cast<double>(std::abs(out[start + n] - noise[start + n])));
        }
        expect(repeated < 1.0e-5, "Saccade repeats the bar's first sixteenth through its first beat");
        expect(after < 1.0e-6, "Saccade gives the sound back after its gate");
    }
    // No chance, no repeat.
    {
        RhythmRig rig(Kind::saccade, { { "saccade.chance", 0.0f } });
        rig.playing = true;
        const auto noise = rhythmNoise(2 * bar, 0.5);
        expect(rig.runAll(noise) == noise, "Saccade at no chance leaves the sound");
    }
    // Stopped, the manual repeat: a sixteenth from the moment it is held.
    {
        RhythmRig rig(Kind::saccade, { { "saccade.mode", 1.0f }, { "saccade.grid", 3.0f } });
        const auto noise = rhythmNoise(3 * 24'000, 0.5);
        std::vector<float> out;
        rig.run(std::vector<float>(noise.begin(), noise.begin() + 24'000), out);
        rig.set("saccade.repeat", 1.0f);
        rig.run(std::vector<float>(noise.begin() + 24'000, noise.begin() + 48'000), out);
        const auto shown = rig.readings();
        rig.set("saccade.repeat", 0.0f);
        rig.run(std::vector<float>(noise.begin() + 48'000, noise.end()), out);
        auto before = 0.0;
        auto held = 0.0;
        auto released = 0.0;
        for (std::size_t n = 0; n < 24'000; ++n)
            before = std::max(before, static_cast<double>(std::abs(out[n] - noise[n])));
        for (std::size_t n = 0; n < 24'000; ++n)
            if (n < slice || n % slice >= edge)
                held = std::max(held, static_cast<double>(std::abs(out[24'000 + n] - noise[24'000 + n % slice])));
        for (auto n = 48'000 + edge; n < 72'000; ++n)
            released = std::max(released, static_cast<double>(std::abs(out[n] - noise[n])));
        expect(before < 1.0e-6 && held < 1.0e-5 && released < 1.0e-6, "Saccade's manual repeat works with the transport stopped");
        expect(shown.size() == static_cast<std::size_t>(rhythm::saccadeReadingCount) && shown[rhythm::saccadeReadingRepeating] == 1.0f
                   && std::abs(shown[rhythm::saccadeReadingGrid] - 0.25f) < 1.0e-6 && shown[rhythm::saccadeReadingRepeats] >= 3.0f,
               "Saccade shows its repeating");
    }
    // Mixed, the repeats come over the sound; alone, the sound is silent.
    {
        RhythmRig mixed(Kind::saccade, { { "saccade.offset", 0.0f }, { "saccade.mode", 0.0f }, { "saccade.grid", 3.0f } });
        RhythmRig alone(Kind::saccade, { { "saccade.offset", 0.0f }, { "saccade.mode", 2.0f }, { "saccade.grid", 3.0f } });
        mixed.playing = alone.playing = true;
        const auto noise = rhythmNoise(bar, 0.5);
        const auto both = mixed.runAll(noise);
        const auto only = alone.runAll(noise);
        auto mixError = 0.0;
        auto onlyError = 0.0;
        for (auto n = slice + edge; n < 2 * slice; ++n) {
            mixError = std::max(mixError, static_cast<double>(std::abs(both[n] - noise[n] - noise[n - slice])));
            onlyError = std::max(onlyError, static_cast<double>(std::abs(only[n] - noise[n - slice])));
        }
        expect(mixError < 1.0e-5 && onlyError < 1.0e-5 && rhythmPeak(only, 0, slice) < 1.0e-9,
               "Saccade mixes its repeats over the sound, or plays them alone");
    }
    // Every setting at its ends: finite, bounded.
    for (const auto top : { false, true })
        for (int mode = 0; mode < 3; ++mode) {
            RhythmRig rig(Kind::saccade, { { "saccade.chance", top ? 1.0f : 0.5f }, { "saccade.variation", top ? 1.0f : 0.0f },
                                           { "saccade.decay", top ? 1.0f : 0.0f }, { "saccade.pitchDecay", top ? 1.0f : 0.0f },
                                           { "saccade.filter", top ? 20'000.0f : 200.0f }, { "saccade.mix", top ? 1.0f : 0.0f },
                                           { "saccade.output", top ? 24.0f : -24.0f }, { "saccade.interval", top ? 12.0f : 1.0f },
                                           { "saccade.offset", top ? 15.0f : 0.0f }, { "saccade.gate", top ? 16.0f : 1.0f },
                                           { "saccade.grid", top ? 12.0f : 1.0f }, { "saccade.mode", static_cast<float>(mode) },
                                           { "saccade.reverseChance", top ? 1.0f : 0.0f } });
            rig.playing = true;
            std::vector<float> out;
            rig.run(rhythmNoise(3 * bar, 0.5), out);
            rig.set("saccade.repeat", 1.0f);
            rig.run(rhythmNoise(6 * 48'000, 0.5, 5u), out);
            const auto peak = rhythmPeak(out);
            expect(std::isfinite(peak) && peak < (top ? 64.0 : 4.0), "Saccade at its extremes stays finite");
        }
    {
        RhythmRig rig(Kind::saccade, { { "saccade.variation", 1.0f }, { "saccade.reverseChance", 0.5f }, { "saccade.pitchDecay", 0.5f },
                                       { "saccade.interval", 9.0f }, { "saccade.offset", 2.0f }, { "saccade.gate", 2.0f } });
        rig.playing = true;
        const auto noise = rhythmNoise(2 * bar, 0.5);
        const auto out = rig.runAll(noise);
        expect(rhythmPeak(out) < 2.0 && out != noise, "Saccade varies, reverses and drops its repeats, bounded");
    }
}

void boucleurLoopsWhatItHears()
{
    using namespace cydaw::engine;
    using Kind = NativeDspNodeKind;
    constexpr std::size_t second = 48'000;
    const std::vector<float> silence(second, 0.0f);
    const auto a = rhythmSine(440.0, 0.4, second);
    const auto b = rhythmSine(660.0, 0.4, second);
    // Each take starts from a stop seen once, so the state changes.
    const auto fresh = [&](const std::vector<std::pair<std::string_view, float>>& values) {
        auto rig = std::make_unique<RhythmRig>(Kind::boucleur, values);
        std::vector<float> ignored;
        rig->run(std::vector<float>(480, 0.0f), ignored);
        return rig;
    };
    // A state set from the start is not a command: only a change is.
    {
        RhythmRig idle(Kind::boucleur, { { "boucleur.state", 1.0f } });
        std::vector<float> out;
        idle.run(a, out);
        expect(idle.readings()[rhythm::boucleurReadingState] == 0.0f, "Boucleur acts on changes of its state, not on its level");
    }
    // Recorded, then played: the next second repeats it.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f } });
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(a, out);
        rig->set("boucleur.state", 2.0f);
        rig->run(silence, out);
        const auto shown = rig->readings();
        expect(rhythmCorrelation(out, second, a, 0, second) > 0.95, "Boucleur plays back what it recorded");
        expect(shown.size() == static_cast<std::size_t>(rhythm::boucleurReadingCount) && shown[rhythm::boucleurReadingState] == 2.0f
                   && std::abs(shown[rhythm::boucleurReadingLength] - 1.0f) < 1.0e-3 && shown[rhythm::boucleurReadingLayers] == 1.0f
                   && shown[rhythm::boucleurReadingOutline + 64] > 0.3f,
               "Boucleur shows its state, length, layers and outline");
        // The record button pressed while playing: a new take.
        rig->set("boucleur.state", 1.0f);
        rig->run(std::vector<float>(480, 0.0f), out);
        expect(rig->readings()[rhythm::boucleurReadingState] == 1.0f, "Boucleur records anew from playing");
    }
    // Overdubbed: both layers play.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f } });
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(a, out);
        rig->set("boucleur.state", 3.0f);
        rig->run(b, out);
        rig->set("boucleur.state", 2.0f);
        rig->run(silence, out);
        const auto from = 2 * second + 2'400;
        const auto lower = toneLevelDb(out, 440.0, from, from + 40'000);
        const auto upper = toneLevelDb(out, 660.0, from, from + 40'000);
        expect(std::abs(lower - 20.0 * std::log10(0.4)) < 1.0 && std::abs(upper - 20.0 * std::log10(0.4)) < 1.0,
               "Boucleur's overdub adds a layer");
        expect(rig->readings()[rhythm::boucleurReadingLayers] >= 2.0f, "Boucleur counts its layers");
    }
    // Overdub at feedback 0.5: one pass halves the old layer.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f } });
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(a, out);
        rig->set("boucleur.feedback", 0.5f);
        rig->set("boucleur.state", 3.0f);
        rig->run(silence, out);
        rig->set("boucleur.state", 2.0f);
        rig->run(silence, out);
        const auto from = 2 * second + 2'400;
        const auto kept = toneLevelDb(out, 440.0, from, from + 40'000) - 20.0 * std::log10(0.4);
        expect(std::abs(kept + 6.02) < 0.5, "Boucleur's feedback 0.5 halves the old layer after one pass");
    }
    // Cleared: nothing left.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f } });
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(a, out);
        rig->set("boucleur.state", 2.0f);
        rig->run(std::vector<float>(second / 2, 0.0f), out);
        rig->set("boucleur.clear", 1.0f);
        rig->run(silence, out);
        expect(rhythmPeak(out, second + second / 2 + 960) < 1.0e-9 && rig->readings()[rhythm::boucleurReadingLength] == 0.0f
                   && rig->readings()[rhythm::boucleurReadingState] == 0.0f,
               "Boucleur's clear empties the loop");
    }
    // A bar at 120 bpm: two seconds, then it plays by itself.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f }, { "boucleur.bars", 1.0f } });
        std::vector<float> out;
        const auto take = rhythmNoise(3 * second, 0.4);
        rig->set("boucleur.state", 1.0f);
        rig->run(take, out);
        const auto shown = rig->readings();
        expect(shown[rhythm::boucleurReadingState] == 2.0f && std::abs(shown[rhythm::boucleurReadingLength] - 2.0f) < 1.0e-3,
               "Boucleur records a bar of 120 bpm in two seconds and plays it");
        expect(rhythmCorrelation(out, 2 * second + 960, take, 960, second - 960) > 0.95, "Boucleur's bar loop repeats its take");
    }
    // Quantised, with the transport playing: it waits for the bar line.
    {
        auto rig = fresh({ { "boucleur.quantize", 1.0f } });
        rig->playing = true;
        rig->startBeat = 1.0;
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(std::vector<float>(480, 0.0f), out);
        const auto waiting = rig->readings()[rhythm::boucleurReadingState];
        // The bar line: beat 4, 72 000 samples after beat 1 (the rig counts from its first block).
        rig->run(std::vector<float>(72'000 - 960, 0.0f), out);
        const auto before = rig->readings()[rhythm::boucleurReadingState];
        rig->run(std::vector<float>(960, 0.0f), out);
        const auto after = rig->readings()[rhythm::boucleurReadingState];
        expect(waiting == static_cast<float>(rhythm::boucleurWaiting) && before == waiting && after == 1.0f,
               "Boucleur quantised starts recording on the next bar line");
    }
    // At 2×, the loop lasts half as long.
    {
        auto rig = fresh({ { "boucleur.dry", 0.0f } });
        std::vector<float> out;
        rig->set("boucleur.state", 1.0f);
        rig->run(rhythmJoined(rhythmSine(440.0, 0.4, second / 2), std::vector<float>(second / 2, 0.0f)), out);
        rig->set("boucleur.speed", 2.0f);
        rig->set("boucleur.state", 2.0f);
        rig->run(silence, out);
        const auto start = second;
        const auto quarter = second / 4;
        const auto sounding = rhythmPeak(out, start + 1'000, start + quarter - 1'000);
        const auto quiet = rhythmPeak(out, start + quarter + 1'000, start + 2 * quarter - 1'000);
        const auto again = rhythmPeak(out, start + 2 * quarter + 1'000, start + 3 * quarter - 1'000);
        expect(sounding > 0.3 && quiet < 1.0e-3 && again > 0.3, "Boucleur at 2× plays its loop in half the time");
        expect(toneLevelDb(out, 880.0, start + 1'000, start + quarter - 1'000) > toneLevelDb(out, 440.0, start + 1'000, start + quarter - 1'000) + 20.0,
               "Boucleur at 2× plays an octave up");
    }
    // Every setting at its ends, every state in turn: finite, bounded.
    for (const auto top : { false, true }) {
        auto rig = fresh({ { "boucleur.feedback", top ? 1.0f : 0.0f }, { "boucleur.level", top ? 1.0f : 0.0f },
                           { "boucleur.dry", top ? 1.0f : 0.0f }, { "boucleur.speed", top ? 2.0f : 0.0f },
                           { "boucleur.reverse", top ? 1.0f : 0.0f }, { "boucleur.bars", top ? 5.0f : 0.0f } });
        std::vector<float> out;
        for (const auto state : { 1.0f, 3.0f, 2.0f, 3.0f, 0.0f, 3.0f, 2.0f, 1.0f, 3.0f }) {
            rig->set("boucleur.state", state);
            rig->run(rhythmNoise(second, 0.9, static_cast<std::uint32_t>(state * 7.0f) + 1u), out);
        }
        for (int pass = 0; pass < 8; ++pass)
            rig->run(rhythmNoise(second, 0.9, 99u + static_cast<std::uint32_t>(pass)), out);
        const auto peak = rhythmPeak(out);
        expect(std::isfinite(peak) && peak < 4.0, "Boucleur at its extremes stays finite and bounded");
        RhythmRig loud(Kind::boucleur, { { "boucleur.output", 24.0f } });
        std::vector<float> loudOut;
        loud.run(rhythmNoise(second, 0.5), loudOut);
        expect(std::isfinite(rhythmPeak(loudOut)) && loud.processor->latencySamples() == 0, "Boucleur turned up stays finite");
    }
}

void rhythmEffectsDoNotAllocate()
{
    using namespace cydaw::engine;
    using Kind = NativeDspNodeKind;
    for (const auto kind : { Kind::grains, Kind::saccade, Kind::boucleur }) {
        RhythmRig rig(kind, {});
        rig.playing = true;
        std::vector<float> out;
        out.reserve(48'000);
        const auto noise = rhythmNoise(24'000, 0.5);
        rig.run(noise, out);
        rig.set(kind == Kind::boucleur ? "boucleur.state" : kind == Kind::saccade ? "saccade.repeat" : "grains.freeze", 1.0f);
        allocation_probe::count.store(0, std::memory_order_relaxed);
        allocation_probe::enabled.store(true, std::memory_order_relaxed);
        rig.run(noise, out);
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        expect(allocation_probe::count.load(std::memory_order_relaxed) == 0, "the rhythm effects play without allocating");
    }
}

void rhythmEffectTests()
{
    grainsScattersEchoes();
    saccadeRepeatsOnTheBeat();
    boucleurLoopsWhatItHears();
    rhythmEffectsDoNotAllocate();
}
