// The sampler read as grains (Nuée). Included by EngineTests.cpp inside its
// anonymous namespace, after the sampler's own tests and their helpers.

void samplerPlaysGrains()
{
    using cydaw::engine::SamplerSetting;
    auto values = cydaw::engine::defaultSamplerValues();
    values[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 0.0f;
    values[static_cast<std::size_t>(SamplerSetting::grains)] = 1.0f;
    values[static_cast<std::size_t>(SamplerSetting::loop)] = 0.0f;
    cydaw::engine::SamplerMap map;
    cydaw::engine::SamplerZone zone;
    zone.sample = sineSample(220.0, 1.0, 0.5, 1.0);
    zone.rootNote = 57;
    map.zones = { zone };
    // A note held far past the sample's length: grains go on.
    const auto held = playSampler(map, values, 57.0, 0.9, 96'000);
    const auto early = held.rms(held.left, 9'600, 24'000);
    const auto late = held.rms(held.left, 72'000, 96'000);
    expect(early > 0.05 && late > 0.05, "grains: a held note goes on past the end of its sample");
    expect(std::abs(strongestLag(held.left, 180, 260) - 218) <= 2, "grains: at the note's pitch, the sample's own");
    auto peak = 0.0;
    for (const auto sample : held.left)
        peak = std::max(peak, std::abs(static_cast<double>(sample)));
    expect(peak < 1.5, "grains: their sum stays near one sample's level");
    // An octave up: grains at twice the pitch.
    const auto up = playSampler(map, values, 69.0, 0.9, 48'000);
    expect(std::abs(strongestLag(up.left, 90, 130) - 109) <= 2, "grains: a note an octave up plays them an octave up");
    // Dense, long, random and backwards: still bounded and finite.
    auto wild = values;
    wild[static_cast<std::size_t>(SamplerSetting::grainDensity)] = 120.0f;
    wild[static_cast<std::size_t>(SamplerSetting::grainSize)] = 1'000.0f;
    wild[static_cast<std::size_t>(SamplerSetting::grainPitch)] = 12.0f;
    wild[static_cast<std::size_t>(SamplerSetting::grainReverse)] = 1.0f;
    wild[static_cast<std::size_t>(SamplerSetting::grainSpray)] = 1.0f;
    wild[static_cast<std::size_t>(SamplerSetting::grainScan)] = -2.0f;
    const auto dense = playSampler(map, wild, 60.0, 1.0, 48'000);
    auto finite = true;
    auto densePeak = 0.0;
    for (const auto sample : dense.left) {
        finite = finite && std::isfinite(sample);
        densePeak = std::max(densePeak, std::abs(static_cast<double>(sample)));
    }
    expect(finite && densePeak < 2.0 && densePeak > 0.01, "grains: dense, long, random and backwards, they stay bounded");
}
