// The DJ mix's sound (decks, channel, effects, analysis) and the Foundry's
// edited sound. Included by EngineTests.cpp inside its namespace.

std::vector<std::vector<float>> djTone(const double hz, const double seconds, const double rate, const float amplitude = 0.5f)
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    std::vector<std::vector<float>> channels(2, std::vector<float>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        const auto value = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
        channels[0][i] = value;
        channels[1][i] = value;
    }
    return channels;
}

void djDeckPlaysAtItsTempo()
{
    using namespace cydaw::engine::dj;
    TrackAudio track;
    track.sampleRate = 44'100.0;
    track.channels = djTone(440.0, 6.0, 44'100.0);
    for (const auto keylock : { false, true }) {
        DeckPlayer deck;
        deck.prepare(48'000.0, 512);
        deck.setTrack(&track);
        std::vector<float> left(48'000), right(48'000);
        for (int at = 0; at < 48'000; at += 512)
            deck.render(left.data() + at, right.data() + at, std::min(512, 48'000 - at), 1.08, keylock);
        std::vector<float> middle(left.begin() + 8'000, left.begin() + 44'000);
        const auto hz = strongestFrequency(middle, 48'000.0, 380.0, 520.0);
        const auto expected = keylock ? 440.0 : 440.0 * 1.08;
        if (std::abs(hz - expected) >= 2.0)
            std::cout << "deck keylock " << keylock << ": " << hz << " Hz\n";
        expect(std::abs(hz - expected) < 2.0, keylock ? "a deck with its key locked keeps the pitch" : "a deck read faster goes up");
        expect(std::abs(deck.position() / 44'100.0 - 1.08) < 0.02, "a deck goes at its tempo");
        const auto level = 20.0 * std::log10(rmsOf(left, 8'000, 44'000));
        expect(std::abs(level - 20.0 * std::log10(0.5 / std::sqrt(2.0))) < 1.5, "a deck keeps the level");
    }
    // Looped, it stays inside its loop; backwards, it goes back.
    {
        DeckPlayer deck;
        deck.prepare(48'000.0, 512);
        deck.setTrack(&track);
        deck.setLoop(44'100.0, 44'100.0 + 22'050.0);
        deck.jump(44'100.0);
        std::vector<float> left(512), right(512);
        for (int block = 0; block < 200; ++block) {
            deck.render(left.data(), right.data(), 512, 1.0, block % 2 == 0);
            expect(deck.position() >= 44'000.0 && deck.position() < 44'100.0 + 22'050.0 + 600.0, "a loop holds the deck");
        }
        deck.clearLoop();
        const auto before = deck.position();
        for (int block = 0; block < 20; ++block)
            deck.render(left.data(), right.data(), 512, -1.0, false);
        expect(deck.position() < before - 8'000.0, "a deck plays backwards");
    }
}

void djChannelKillsItsBands()
{
    using namespace cydaw::engine::dj;
    const auto through = [](const double hz, const StripSettings& settings) {
        ChannelStrip strip;
        strip.prepare(48'000.0);
        auto channels = djTone(hz, 1.0, 48'000.0, 0.5f);
        for (int at = 0; at < 48'000; at += 512) {
            const auto n = std::min(512, 48'000 - at);
            strip.process(channels[0].data() + at, channels[1].data() + at, n, settings);
        }
        return 20.0 * std::log10(std::max(1.0e-12, rmsOf(channels[0], 24'000, 48'000)) / (0.5 / std::sqrt(2.0)));
    };
    StripSettings flat;
    for (const auto hz : { 60.0, 1'000.0, 8'000.0 })
        expect(std::abs(through(hz, flat)) < 0.2, "a flat channel leaves the level");
    auto noLow = flat;
    noLow.low = eqKnobGain(0.0f);
    expect(through(60.0, noLow) < -40.0, "the bass kill takes the bass out");
    expect(std::abs(through(8'000.0, noLow)) < 0.5, "the bass kill leaves the highs");
    auto noMid = flat;
    noMid.mid = 0.0f;
    expect(through(1'000.0, noMid) < -30.0, "the mid kill takes the mids out");
    auto noHigh = flat;
    noHigh.high = 0.0f;
    expect(through(12'000.0, noHigh) < -40.0, "the high kill takes the highs out");
    auto lowPassed = flat;
    lowPassed.filter = -1.0f;
    expect(through(5'000.0, lowPassed) < -40.0, "the filter turned left is a low-pass");
    auto highPassed = flat;
    highPassed.filter = 1.0f;
    expect(through(100.0, highPassed) < -40.0, "the filter turned right is a high-pass");
    expect(std::abs(eqKnobGain(0.5f) - 1.0f) < 1.0e-4f && eqKnobGain(0.0f) == 0.0f, "an EQ knob's middle is 0 dB");
    const auto [left, right] = crossfaderGains(0.5f, 0.0f);
    expect(std::abs(left * left + right * right - 1.0f) < 1.0e-3f, "the smooth crossfader keeps the power");
}

void djEffectsPlayInTime()
{
    using namespace cydaw::engine::dj;
    DeckEffect echo;
    echo.prepare(48'000.0);
    std::vector<float> left(24'000, 0.0f), right(24'000, 0.0f);
    left[2'400] = right[2'400] = 1.0f;
    EffectSettings settings;
    settings.kind = EffectKind::echo;
    settings.on = true;
    settings.mix = 0.5f;
    settings.first = 3.0f / 7.0f; // a quarter of a beat
    settings.second = 0.0f;
    for (int at = 0; at < 24'000; at += 512)
        echo.process(left.data() + at, right.data() + at, std::min(512, 24'000 - at), settings, 0.5, 0.0);
    expect(std::abs(beatsOfKnob(3.0f / 7.0f, false) - 0.25) < 1.0e-9, "an echo knob's steps");
    float heard = 0.0f;
    for (int i = 8'380; i < 8'500; ++i)
        heard = std::max(heard, std::abs(left[static_cast<std::size_t>(i)]));
    float before = 0.0f;
    for (int i = 3'000; i < 8'300; ++i)
        before = std::max(before, std::abs(left[static_cast<std::size_t>(i)]));
    expect(heard > 0.1f && before < 0.02f, "an echo comes a quarter of a beat later");
    // Every effect stays bounded on a loud tone.
    for (int kind = 0; kind < effectKindCount; ++kind) {
        DeckEffect effect;
        effect.prepare(48'000.0);
        auto channels = djTone(220.0, 1.0, 48'000.0, 0.9f);
        EffectSettings any;
        any.kind = static_cast<EffectKind>(kind);
        any.on = true;
        any.mix = 0.7f;
        any.first = 0.6f;
        any.second = 0.8f;
        auto worst = 0.0f;
        for (int at = 0; at < 48'000; at += 512) {
            const auto n = std::min(512, 48'000 - at);
            effect.process(channels[0].data() + at, channels[1].data() + at, n, any, 0.47, at / 48'000.0 / 0.47);
        }
        for (const auto value : channels[0])
            worst = std::max(worst, std::abs(value));
        expect(std::isfinite(worst) && worst < 6.0f && worst > 0.01f, effectInfo(any.kind).name);
    }
}

void djAnalysisFindsTempoAndKey()
{
    using namespace cydaw::engine::dj;
    TrackAudio track;
    track.sampleRate = 44'100.0;
    const auto rate = track.sampleRate;
    const auto frames = static_cast<std::size_t>(40.0 * rate);
    track.channels.assign(2, std::vector<float>(frames, 0.0f));
    const auto bpm = 128.0;
    const auto beat = 60.0 / bpm;
    const auto offset = 0.3;
    std::uint32_t seed = 12345u;
    for (auto at = offset; at < 40.0; at += beat) {
        // A kick on the beat, a hat between.
        const auto start = static_cast<std::size_t>(at * rate);
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.15 * rate) && start + i < frames; ++i) {
            const auto t = static_cast<double>(i) / rate;
            const auto value = 0.8f * static_cast<float>(std::exp(-t * 25.0) * std::sin(2.0 * std::numbers::pi * 55.0 * t));
            track.channels[0][start + i] += value;
            track.channels[1][start + i] += value;
        }
        const auto hat = static_cast<std::size_t>((at + beat * 0.5) * rate);
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.03 * rate) && hat + i < frames; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const auto noise = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.3f;
            const auto value = noise * static_cast<float>(std::exp(-static_cast<double>(i) / rate * 120.0));
            track.channels[0][hat + i] += value;
            track.channels[1][hat + i] += value;
        }
    }
    // A minor held under it.
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        float chord = 0.0f;
        for (const auto hz : { 220.0, 261.63, 329.63, 440.0 })
            chord += 0.04f * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t));
        track.channels[0][i] += chord;
        track.channels[1][i] += chord;
    }
    const auto found = analyse(track);
    if (std::abs(found.bpm - bpm) >= 0.05)
        std::cout << "analysis tempo: " << found.bpm << " BPM\n";
    expect(std::abs(found.bpm - bpm) < 0.05, "the analysis finds the tempo");
    auto off = std::fmod(found.firstBeat - offset, beat);
    if (off > beat * 0.5)
        off -= beat;
    if (off < -beat * 0.5)
        off += beat;
    if (std::abs(off) >= 0.015)
        std::cout << "analysis grid: " << found.firstBeat << " s (" << off * 1'000.0 << " ms off)\n";
    expect(std::abs(off) < 0.015, "the analysis lays its grid on the beats");
    if (found.key != 21)
        std::cout << "analysis key: " << keyName(found.key) << '\n';
    expect(found.key == 21, "the analysis finds A minor");
    expect(std::string_view(camelotName(21)) == "8A" && std::string_view(camelotName(0)) == "8B", "the wheel's names");
    expect(found.loudness > -40.0 && found.loudness < 0.0, "the analysis measures the loudness");
    expect(found.wave.size() == (frames + 127) / 128, "the waveform has a column per 128 frames");
    // Silence has no tempo.
    TrackAudio quiet;
    quiet.channels.assign(2, std::vector<float>(static_cast<std::size_t>(20.0 * rate), 0.0f));
    expect(analyse(quiet).bpm == 0.0, "silence has no tempo");
}

void loudnessOfASine()
{
    auto channels = djTone(1'000.0, 5.0, 48'000.0, 0.1f);
    const float* pointers[2] { channels[0].data(), channels[1].data() };
    const auto lufs = cydaw::engine::integratedLoudness(pointers, 2, static_cast<std::int64_t>(channels[0].size()), 48'000.0);
    if (std::abs(lufs + 20.0) >= 0.3)
        std::cout << "loudness: " << lufs << " LUFS\n";
    expect(std::abs(lufs + 20.0) < 0.3, "a -20 dBFS sine on both sides is -20 LUFS");
}

void foundrySoundEdits()
{
    using namespace cydaw::engine::foundry;
    std::vector<std::vector<float>> ramp(2, std::vector<float>(1'000));
    for (std::size_t i = 0; i < 1'000; ++i) {
        ramp[0][i] = static_cast<float>(i) / 1'000.0f;
        ramp[1][i] = -static_cast<float>(i) / 1'000.0f;
    }
    const Sound sound(ramp, 48'000.0);
    expect(sound.frames() == 1'000 && sound.channels() == 2, "a sound's size");
    const auto valueAt = [](const Sound& s, const int channel, const std::int64_t frame) {
        float value = 0.0f;
        s.read(channel, frame, 1, &value);
        return value;
    };
    const auto cut = sound.removed(100, 200);
    expect(cut.frames() == 900 && std::abs(valueAt(cut, 0, 100) - 0.2f) < 1.0e-6f, "a cut closes the gap");
    const auto piece = sound.excerpt(100, 200);
    const auto pasted = cut.inserted(100, piece);
    bool same = pasted.frames() == 1'000;
    for (std::int64_t i = 0; i < 1'000 && same; i += 7)
        same = valueAt(pasted, 0, i) == valueAt(sound, 0, i) && valueAt(pasted, 1, i) == valueAt(sound, 1, i);
    expect(same, "pasting back what was cut gives the sound back");
    const auto louder = gained(sound, 0, 1'000, 0b01, 6.0206f);
    expect(std::abs(valueAt(louder, 0, 500) - 1.0f) < 1.0e-3f && std::abs(valueAt(louder, 1, 500) + 0.5f) < 1.0e-6f,
           "a gain on the left only");
    const auto backwards = reversed(sound, 0, 1'000, 0b11);
    expect(std::abs(valueAt(backwards, 0, 0) - 0.999f) < 1.0e-6f, "reversed");
    const auto fadedIn = faded(sound, 500, 1'000, 0b11, true, FadeCurve::linear);
    expect(valueAt(fadedIn, 0, 500) == 0.0f && std::abs(valueAt(fadedIn, 0, 999) - 0.999f) < 1.0e-6f, "a fade in");
    const auto normal = normalised(sound, 0, 1'000, 0b11, -1.0f, false);
    const auto peak = statistics(normal, 0, 1'000).peak[0];
    expect(std::abs(20.0f * std::log10(peak) + 1.0f) < 0.01f, "normalised to -1 dBFS");
    const auto mixed = sound.mixedIn(900, sound.excerpt(0, 200), 1.0f);
    expect(mixed.frames() == 1'100 && std::abs(valueAt(mixed, 0, 950) - (0.95f + 0.05f)) < 1.0e-5f, "mixed in past the end");
    // The summaries agree with the frames.
    std::uint32_t seed = 99u;
    std::vector<std::vector<float>> noise(1, std::vector<float>(50'000));
    for (auto& value : noise[0]) {
        seed = seed * 1664525u + 1013904223u;
        value = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
    }
    const auto loud = Sound(noise, 48'000.0).removed(1'000, 1'333).inserted(20'000, Sound(noise, 48'000.0).excerpt(7, 3'000));
    auto agree = true;
    for (int round = 0; round < 50 && agree; ++round) {
        seed = seed * 1664525u + 1013904223u;
        const auto a = static_cast<std::int64_t>(seed % 40'000u);
        const auto b = a + 1 + static_cast<std::int64_t>((seed >> 16) % 9'000u);
        const auto [low, high] = loud.range(0, a, b);
        const auto frames = loud.render(a, b);
        const auto [minimum, maximum] = std::minmax_element(frames[0].begin(), frames[0].end());
        agree = low == *minimum && high == *maximum;
    }
    expect(agree, "a sound's ranges match its frames");
    expect(sound.withRate(96'000.0).frames() == 2'000, "a new rate keeps the length in time");
    const Sound tone(djTone(440.0, 2.0, 48'000.0), 48'000.0);
    const auto faster = stretched(tone, 0, tone.frames(), 2.0, 1.0, cydaw::engine::StretchAlgorithm::tonal);
    expect(std::abs(faster.frames() - 48'000) <= 1, "stretched twice as fast: half as long");
    const auto stats = statistics(tone, 0, tone.frames());
    expect(std::abs(stats.rms[0] - 0.5 / std::sqrt(2.0)) < 1.0e-3 && std::abs(stats.dc[0]) < 1.0e-3, "a sound's statistics");
    const auto bins = spectrum(tone, 0, tone.frames(), 120);
    const auto strongest = std::distance(bins.begin(), std::max_element(bins.begin(), bins.end()));
    const auto hz = 20.0 * std::pow(24'000.0 / 20.0, (static_cast<double>(strongest) + 0.5) / 120.0);
    expect(hz > 400.0 && hz < 480.0 && bins[static_cast<std::size_t>(strongest)] > -12.0f, "a sound's spectrum peaks at its tone");
}

void foundrySessionEdits()
{
    using namespace cydaw::engine::foundry;
    const auto ramp = [](const int frames, const float scale) {
        std::vector<std::vector<float>> channels(1, std::vector<float>(static_cast<std::size_t>(frames)));
        for (int i = 0; i < frames; ++i)
            channels[0][static_cast<std::size_t>(i)] = scale * static_cast<float>(i) / static_cast<float>(frames);
        return Sound(channels, 48'000.0);
    };
    Session session;
    session = withClip(session, 0, 0, ramp(1'000, 1.0f), "a", "");
    session = withClip(session, 0, 2'000, ramp(1'000, 0.5f), "b", "");
    session = withClip(session, 1, 500, ramp(1'000, 0.25f), "c", "");
    expect(session.tracks.size() == 2 && session.frames() == 3'000, "a session's tracks and length");
    const auto at = [&](const Session& s, const std::int64_t frame) {
        float left = 0.0f;
        float right = 0.0f;
        std::vector<float> scratch(2);
        cydaw::engine::foundry::mix(s, frame, 1, &left, &right, scratch.data());
        return left;
    };
    expect(std::abs(at(session, 600) - (0.6f + 0.025f)) < 1.0e-5f, "the tracks mix");
    auto muted = session;
    muted.tracks[1].mute = true;
    expect(std::abs(at(muted, 600) - 0.6f) < 1.0e-5f, "a muted track is not heard");
    auto solo = session;
    solo.tracks[1].solo = true;
    expect(std::abs(at(solo, 600) - 0.025f) < 1.0e-5f, "a solo track alone");
    // Cut on the first track only: what follows comes back.
    const TrackSet first { true, false };
    const auto cut = removed(session, 500, 1'500, first);
    expect(cut.tracks[0].clips.size() == 2 && cut.tracks[0].clips[1].start == 1'000 && cut.tracks[1].clips[0].start == 500,
           "a cut on one track closes its gap only");
    // Inside one clip: still one clip.
    const auto inside = removed(session, 200, 300, first);
    expect(inside.tracks[0].clips.size() == 2 && inside.tracks[0].clips[0].length == 900 && inside.tracks[0].clips[1].start == 1'900,
           "a cut inside a clip leaves it one clip");
    const auto quiet = withSilence(session, 500, 250, first);
    expect(quiet.tracks[0].clips.size() == 2 && quiet.tracks[0].clips[0].length == 1'250 && quiet.tracks[0].clips[1].start == 2'250
               && std::abs(at(quiet, 600) - 0.025f) < 1.0e-5f && std::abs(at(quiet, 800) - (0.55f + 0.075f)) < 1.0e-5f,
           "silence inserted inside a clip");
    const auto piece = excerpt(session, 500, 1'500, first);
    const auto back = pasted(cut, 500, piece, 0, false);
    auto same = true;
    for (std::int64_t f = 0; f < 3'000 && same; f += 37)
        same = std::abs(at(back, f) - at(session, f)) < 1.0e-6f;
    expect(same, "pasting back what was cut gives the session back");
    // Split, then a process on the second half grows it: what follows moves.
    const auto halves = split(session, 400, first);
    expect(halves.tracks[0].clips.size() == 3 && halves.tracks[0].clips[1].start == 400, "a clip split in two");
    const auto longer = processed(halves, 400, 1'000, first, [](const Sound& sound, std::int64_t from, std::int64_t to, std::int64_t) {
        return sound.inserted(to, Sound(sound.channels(), 100, sound.rate()));
    });
    expect(longer.tracks[0].clips[1].length == 700 && longer.tracks[0].clips[2].start == 2'100, "a longer clip moves what follows");
    const auto gained = processed(session, 0, 3'000, TrackSet { true, true }, [](const Sound& sound, std::int64_t from, std::int64_t to, std::int64_t) {
        return cydaw::engine::foundry::gained(sound, from, to, 0xffu, 6.0206f);
    });
    expect(std::abs(at(gained, 600) - 2.0f * (0.6f + 0.025f)) < 1.0e-3f, "a process over every track");
    const auto whole = render(session, 0, session.frames());
    expect(whole.frames() == 3'000 && whole.channels() == 2, "a session rendered whole");
    const auto kept = cropped(session, 500, 1'500);
    expect(kept.frames() == 1'000 && std::abs(at(kept, 100) - at(session, 600)) < 1.0e-6f, "cropped to a stretch");
}
