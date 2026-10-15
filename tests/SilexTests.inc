// Silex, the drum machine: each key of the drum map a piece that plays out
// its envelope whatever the key does, the other keys silent; the kick on its
// tune once its pitch has fallen, the open hat longer than the closed one, a
// cut open hat gone in a few milliseconds, a soft hit quieter and darker,
// lo-fi held at a lower rate, nothing allocated as it plays, every setting
// at its ends still bounded.
void silexPlaysItsPieces()
{
    using namespace cydaw::engine;
    using synth::Model;
    constexpr auto rate = 48'000.0;
    const prism::Context context;
    const auto key = [](const std::string& name) { return static_cast<std::size_t>(synth::parameterIndex(Model::silex, name)); };
    expect(synth::modelForType("silex-synth") == Model::silex && synth::modelForPrefix("silex") == Model::silex
               && synth::parameterIndex(Model::silex, "voice.master") == synth::voicingMaster
               && synth::parameterIndex(Model::silex, "kick.tune") == synth::voicingFieldCount
               && synth::parameterIndex(Model::silex, "master.tone") == 72 && synth::parameterCount(Model::silex) == 73,
           "Silex is found by its type and prefix, its settings in their order");
    const auto defaults = synth::defaultValues(Model::silex);
    auto inRange = true;
    for (int index = 0; index < synth::parameterCount(Model::silex); ++index)
        inRange = inRange
                  && synth::clampValue(Model::silex, index, defaults[static_cast<std::size_t>(index)]) == defaults[static_cast<std::size_t>(index)];
    expect(inRange, "Silex's defaults are within their ranges");
    struct Played {
        std::vector<float> left;
        bool alive {};
    };
    // A note played for `seconds`, let go of at `releaseAt` and cut at
    // `killAt` (below 0: never): its left side, and whether it still plays.
    const auto play = [&](const synth::Values& values, const int note, const float velocity, const double seconds, const double releaseAt,
                          const double killAt) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(Model::silex, values, note, velocity, -1.0, rate);
        Played played;
        played.left.assign(static_cast<std::size_t>(seconds * rate), 0.0f);
        std::vector<float> right(played.left.size(), 0.0f);
        auto released = false;
        auto killed = false;
        for (std::size_t at = 0; at < played.left.size(); at += 64) {
            if (releaseAt >= 0.0 && !released && static_cast<double>(at) >= releaseAt * rate) {
                voice.release();
                released = true;
            }
            if (killAt >= 0.0 && !killed && static_cast<double>(at) >= killAt * rate) {
                voice.kill();
                killed = true;
            }
            const auto count = static_cast<int>(std::min<std::size_t>(64, played.left.size() - at));
            voice.render(values, context, played.left.data() + at, right.data() + at, count, rate);
        }
        played.alive = voice.active();
        return played;
    };
    const auto span = [](const std::vector<float>& samples, const double from, const double to) {
        return std::pair { std::min(samples.size(), static_cast<std::size_t>(from * rate)),
                           std::min(samples.size(), static_cast<std::size_t>(to * rate)) };
    };
    const auto peakOf = [&](const std::vector<float>& samples, const double from, const double to) {
        const auto [first, last] = span(samples, from, to);
        auto peak = 0.0;
        for (auto i = first; i < last; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(samples[i])));
        return peak;
    };
    const auto rmsDb = [&](const std::vector<float>& samples, const double from, const double to) {
        const auto [first, last] = span(samples, from, to);
        auto energy = 0.0;
        for (auto i = first; i < last; ++i)
            energy += static_cast<double>(samples[i]) * samples[i];
        return 10.0 * std::log10(std::max(1.0e-18, energy / static_cast<double>(std::max<std::size_t>(1, last - first))));
    };
    // How bright: the energy of its slope against its own (dB).
    const auto brightness = [&](const std::vector<float>& samples, const double from, const double to) {
        const auto [first, last] = span(samples, from, to);
        auto energy = 1.0e-18;
        auto slope = 1.0e-18;
        for (auto i = std::max<std::size_t>(first, 1); i < last; ++i) {
            energy += static_cast<double>(samples[i]) * samples[i];
            slope += static_cast<double>(samples[i] - samples[i - 1]) * (samples[i] - samples[i - 1]);
        }
        return 10.0 * std::log10(slope / energy);
    };
    const auto crossings = [&](const std::vector<float>& samples, const double from, const double to) {
        const auto [first, last] = span(samples, from, to);
        auto count = 0;
        for (auto i = std::max<std::size_t>(first, 1); i < last; ++i)
            count += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f) ? 1 : 0;
        return count;
    };
    // Every piece of the map sounds and ends by itself; let go of at once,
    // it plays out all the same; the keys outside the map are silent.
    auto digital = defaults;
    digital[key("kit.character")] = 1.0f;
    auto allSound = true;
    auto allEnd = true;
    for (const auto note : { 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 56, 57, 59, 70, 75 }) {
        const auto held = play(defaults, note, 1.0f, 5.0, -1.0, -1.0);
        allSound = allSound && peakOf(held.left, 0.0, 0.1) > 0.05 && peakOf(held.left, 0.0, 5.0) < 1.0;
        allEnd = allEnd && !held.alive && peakOf(held.left, 4.0, 5.0) == 0.0;
    }
    expect(allSound, "every Silex piece sounds, under full scale");
    expect(allEnd, "every Silex piece ends by itself");
    const auto kickHeld = play(digital, 36, 1.0f, 0.6, -1.0, -1.0);
    const auto kickLetGo = play(digital, 36, 1.0f, 0.6, 0.002, -1.0);
    const auto tomHeld = play(digital, 45, 1.0f, 0.6, -1.0, -1.0);
    const auto tomLetGo = play(digital, 45, 1.0f, 0.6, 0.002, -1.0);
    expect(std::abs(rmsDb(kickHeld.left, 0.05, 0.5) - rmsDb(kickLetGo.left, 0.05, 0.5)) < 0.2
               && std::abs(rmsDb(tomHeld.left, 0.05, 0.5) - rmsDb(tomLetGo.left, 0.05, 0.5)) < 0.2,
           "a Silex piece plays out whatever the key does");
    auto outsideSilent = true;
    for (const auto note : { 0, 34, 52, 60, 69, 76, 127 }) {
        const auto played = play(defaults, note, 1.0f, 0.2, -1.0, -1.0);
        outsideSilent = outsideSilent && !played.alive && peakOf(played.left, 0.0, 0.2) == 0.0;
    }
    expect(outsideSilent, "the keys outside Silex's map are silent");
    // The kick: its pitch falls to its tune (52 Hz); the kit's tuning moves it.
    expect(toneLevelDb(kickHeld.left, 52.0, 7'200, 16'800) > toneLevelDb(kickHeld.left, 104.0, 7'200, 16'800) + 12.0
               && crossings(kickHeld.left, 0.0, 0.03) > 2 * crossings(kickHeld.left, 0.2, 0.23),
           "Silex's kick falls to its tune");
    auto higher = digital;
    higher[key("kit.tune")] = 12.0f;
    const auto kickUp = play(higher, 36, 1.0f, 0.4, -1.0, -1.0);
    expect(toneLevelDb(kickUp.left, 104.0, 7'200, 16'800) > toneLevelDb(kickUp.left, 52.0, 7'200, 16'800) + 12.0,
           "Silex's kit tuning moves its pieces");
    // The toms apart: the highest a little more than an octave above the lowest.
    const auto lowTom = play(digital, 41, 1.0f, 0.4, -1.0, -1.0);
    const auto highTom = play(digital, 50, 1.0f, 0.4, -1.0, -1.0);
    expect(crossings(highTom.left, 0.1, 0.3) > 3 * crossings(lowTom.left, 0.1, 0.3) / 2, "Silex's toms are tuned apart");
    // The open hat rings on where the closed one is gone; cut, it goes at once.
    const auto closedHat = play(defaults, 42, 1.0f, 0.4, -1.0, -1.0);
    const auto openHat = play(defaults, 46, 1.0f, 0.4, -1.0, -1.0);
    expect(peakOf(closedHat.left, 0.15, 0.4) == 0.0 && peakOf(openHat.left, 0.15, 0.3) > 0.01,
           "Silex's open hat rings longer than its closed one");
    const auto cut = play(defaults, 46, 1.0f, 0.3, -1.0, 0.05);
    expect(!cut.alive && peakOf(cut.left, 0.06, 0.3) == 0.0 && peakOf(cut.left, 0.03, 0.05) > 0.01,
           "a cut Silex voice is gone in a few milliseconds");
    // Soft: quieter and darker.
    const auto loudSnare = play(digital, 38, 1.0f, 0.3, -1.0, -1.0);
    const auto softSnare = play(digital, 38, 0.3f, 0.3, -1.0, -1.0);
    expect(rmsDb(softSnare.left, 0.0, 0.1) < rmsDb(loudSnare.left, 0.0, 0.1) - 4.0
               && brightness(softSnare.left, 0.0, 0.1) < brightness(loudSnare.left, 0.0, 0.1) - 1.0,
           "a soft Silex hit is quieter and darker");
    // Lo-fi: held at a lower rate (most samples the same as the one before).
    auto lofi = defaults;
    lofi[key("kit.character")] = 2.0f;
    const auto crunchy = play(lofi, 36, 1.0f, 0.3, -1.0, -1.0);
    auto same = 0;
    for (std::size_t i = 481; i < 9'600; ++i)
        same += crunchy.left[i] == crunchy.left[i - 1] ? 1 : 0;
    expect(same > 9'119 * 6 / 10, "Silex's lo-fi character holds its samples");
    // Nothing allocated as a voice plays.
    {
        synth::Voice voice;
        voice.prepare(rate);
        std::vector<float> left(4'800, 0.0f);
        std::vector<float> right(4'800, 0.0f);
        allocation_probe::count.store(0);
        allocation_probe::enabled.store(true);
        for (const auto note : { 36, 38, 39, 46, 49, 51 }) {
            voice.start(Model::silex, defaults, note, 0.8f, -1.0, rate);
            voice.render(defaults, context, left.data(), right.data(), static_cast<int>(left.size()), rate);
        }
        allocation_probe::enabled.store(false);
        expect(allocation_probe::count.load() == 0, "a Silex voice allocates nothing as it plays");
    }
    // Every setting at its top (and every one at its bottom): finite, bounded.
    for (const auto top : { true, false }) {
        auto extreme = defaults;
        for (int index = 0; index < synth::parameterCount(Model::silex); ++index) {
            const auto& parameter = synth::parameter(Model::silex, index);
            if (index != synth::voicingMaster)
                extreme[static_cast<std::size_t>(index)] = synth::clampValue(Model::silex, index, top ? parameter.maximum : parameter.minimum);
        }
        auto finite = true;
        auto peak = 0.0;
        for (const auto note : { 36, 37, 38, 39, 41, 42, 44, 46, 49, 50, 51, 56, 70, 75 })
            for (const auto sample : play(extreme, note, 1.0f, 0.5, 0.3, -1.0).left) {
                finite = finite && std::isfinite(sample);
                peak = std::max(peak, static_cast<double>(std::abs(sample)));
            }
        expect(finite && peak < 16.0, top ? "Silex at every top stays finite and bounded" : "Silex at every bottom stays finite and bounded");
    }
    // Its pieces drawn; nothing for a part it has not.
    auto drawn = true;
    for (const auto* part : { "kick", "snare", "clap", "hat", "tom", "crash", "ride" }) {
        const auto picture = synth::picture(Model::silex, defaults, part, 200);
        auto peak = 0.0;
        for (const auto x : picture)
            peak = std::max(peak, static_cast<double>(std::abs(x)));
        drawn = drawn && picture.size() == 200 && peak > 0.5 && peak <= 1.0;
    }
    expect(drawn && synth::picture(Model::silex, defaults, "osc1", 200).empty(), "Silex draws its pieces");
}
