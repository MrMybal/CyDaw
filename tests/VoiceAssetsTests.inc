// An exported instrument's voice data (.cyvoice): written, read back, played
// the same. Included by EngineTests.cpp inside its namespace.

void voiceAssetsTravelWithTheirSound()
{
    using cydaw::engine::SamplerSetting;
    auto values = cydaw::engine::defaultSamplerValues();
    values[static_cast<std::size_t>(SamplerSetting::velocityDb)] = 0.0f;
    // Two layers of A3 (one sample shared by two zones), a loop, a rule on a
    // controller with a curve, a keyswitch and a release zone.
    cydaw::engine::SamplerMap map;
    map.name = "Voice check";
    cydaw::engine::SamplerZone soft;
    soft.sample = sineSample(220.0, 1.0, 0.25, 0.5);
    soft.sample->path = "C:/Somewhere/Private/A3 soft.wav";
    soft.rootNote = 57;
    soft.highVelocity = 63;
    soft.loopStart = 24'000;
    soft.loopEnd = 40'000;
    soft.loopCrossfade = 240;
    soft.rules.push_back({ cydaw::engine::SamplerRuleKind::level, 70, 0, 80.0f, 0.0f });
    auto loud = soft;
    loud.sample = sineSample(220.0, 1.0, 0.5, 0.5);
    loud.lowVelocity = 64;
    loud.highVelocity = 127;
    loud.rules.clear();
    auto again = soft;
    again.lowKey = again.highKey = 90;
    again.trigger = cydaw::engine::SamplerTrigger::release;
    map.zones = { soft, loud, again };
    map.curves.push_back({});
    for (std::size_t index = 0; index < 128; ++index)
        map.curves[0][index] = static_cast<float>(index) / 127.0f;
    map.controllerStart[70] = 90.0f;
    map.controls.push_back({ 70, "Close mic" });
    map.keyswitchKeys[24] = true;
    map.keyswitchStart = 24;
    map.keyswitchLabels.emplace_back(24, "Sustain");
    map.hasReleaseZones = true;

    cydaw::engine::SoundModel model;
    model.name = "Two partials";
    model.zones.push_back(twoPartialZone(57, 220.0f, 0.5f, 0.25f));

    std::stringstream file(std::ios::in | std::ios::out | std::ios::binary);
    std::string error;
    auto asked = 0;
    const auto whole = [&asked](const cydaw::engine::SampleDataRef& sample) {
        ++asked;
        return sample;
    };
    expect(cydaw::engine::writeVoiceAssets(file, &map, whole, &model, error), "voice data is written (" + error + ")");
    expect(asked == 2, "each sample is written once, whatever zones share it");
    const auto written = file.str();
    expect(written.find("Private") == std::string::npos && written.find("A3 soft.wav") != std::string::npos,
           "voice data keeps a sample's file name, not where it was on the disk");

    file.seekg(0);
    const auto read = cydaw::engine::readVoiceAssets(file, error);
    expect(read.sampler != nullptr && read.soundModel != nullptr, "voice data is read back (" + error + ")");
    if (read.sampler == nullptr || read.soundModel == nullptr)
        return;
    const auto& back = *read.sampler;
    expect(back.name == "Voice check" && back.zones.size() == 3 && back.zones[0].sample == back.zones[2].sample
               && back.zones[0].sample != back.zones[1].sample && back.zones[0].sample->playable(),
           "the zones come back sharing their samples, ready to play");
    expect(back.zones[0].rules == map.zones[0].rules && back.zones[2].trigger == cydaw::engine::SamplerTrigger::release
               && back.zones[0].loopEnd == 40'000 && back.zones[0].loopCrossfade == 240,
           "a zone's rules, trigger and loop come back");
    expect(back.curves == map.curves && back.controllerStart[70] == 90.0f && back.controls.size() == 1
               && back.controls[0].label == "Close mic" && back.keyswitchKeys[24] && back.keyswitchStart == 24
               && back.keyswitchLabels == map.keyswitchLabels && back.hasReleaseZones,
           "the map's curves, controllers and keyswitches come back");
    for (const auto velocity : { 0.3, 0.9 }) {
        const auto before = playSampler(map, values, 69.0, velocity, 60'000);
        const auto after = playSampler(back, values, 69.0, velocity, 60'000);
        expect(before.left == after.left && before.right == after.right,
               "the map read back plays exactly as the one written");
    }

    const auto& zone = read.soundModel->zones.at(0);
    expect(read.soundModel->name == "Two partials" && zone.harmonics == model.zones[0].harmonics
               && zone.pitchCents == model.zones[0].pitchCents,
           "the sound model's tables come back");
    const auto modelValues = cydaw::engine::defaultSoundModelValues();
    cydaw::engine::SoundModelVoice first;
    cydaw::engine::SoundModelVoice second;
    first.prepare(48'000.0);
    second.prepare(48'000.0);
    first.start(model, modelValues, 64.0, 0.8, 48'000.0);
    second.start(*read.soundModel, modelValues, 64.0, 0.8, 48'000.0);
    auto same = true;
    for (auto sample = 0; sample < 24'000; ++sample)
        same = same && first.nextSample(&model, modelValues, 48'000.0) == second.nextSample(read.soundModel.get(), modelValues, 48'000.0);
    expect(same, "the sound model read back plays exactly as the one written");

    // A bell's free ratios, where a note starts, a plucked string: all kept.
    cydaw::engine::SoundModel plucked;
    plucked.name = "Bell and string";
    auto bell = twoPartialZone(57, 220.0f, 0.5f, 0.25f);
    bell.ratios = { 1.0f, 2.756f };
    bell.onset = 0.012f;
    auto string = twoPartialZone(64, 329.63f, 0.5f, 0.25f);
    string.velocity = 1.0f;
    string.string.gain = 0.996f;
    string.string.pole = 0.2f;
    string.string.zero = 0.05f;
    string.string.dispersion = -0.1f;
    string.string.stages = 2;
    string.string.dips = { { 1'500.0f, 2.0f, -3.0f } };
    string.string.excitationDb = { 0.0f, -6.0f, -12.0f, -18.0f };
    string.string.bodyEdges = { 100.0f, 1'000.0f, 10'000.0f };
    string.string.bodyDb = { 0.0f, -3.0f };
    plucked.zones = { bell, string };
    std::stringstream pluckedFile(std::ios::in | std::ios::out | std::ios::binary);
    expect(cydaw::engine::writeVoiceAssets(pluckedFile, nullptr, whole, &plucked, error), "a plucked model is written (" + error + ")");
    pluckedFile.seekg(0);
    const auto pluckedBack = cydaw::engine::readVoiceAssets(pluckedFile, error).soundModel;
    expect(pluckedBack != nullptr && pluckedBack->zones.size() == 2, "a plucked model is read back (" + error + ")");
    if (pluckedBack != nullptr && pluckedBack->zones.size() == 2) {
        const auto& backBell = pluckedBack->zones[0];
        const auto& backString = pluckedBack->zones[1].string;
        expect(backBell.ratios == bell.ratios && backBell.onset == bell.onset && !backBell.string.present(),
               "a zone's free ratios and onset come back");
        expect(backString.present() && backString.gain == 0.996f && backString.pole == 0.2f && backString.zero == 0.05f
                   && backString.dispersion == -0.1f && backString.stages == 2 && backString.dips.size() == 1
                   && backString.dips[0].hz == 1'500.0f && backString.excitationDb == string.string.excitationDb
                   && backString.bodyEdges == string.string.bodyEdges && backString.bodyDb == string.string.bodyDb,
               "a zone's string comes back");
        for (const auto note : { 57.0, 64.0 }) {
            cydaw::engine::SoundModelVoice written;
            cydaw::engine::SoundModelVoice back;
            written.prepare(48'000.0);
            back.prepare(48'000.0);
            written.start(plucked, modelValues, note, 0.9, 48'000.0);
            back.start(*pluckedBack, modelValues, note, 0.9, 48'000.0);
            auto alike = true;
            auto energy = 0.0;
            for (auto sample = 0; sample < 24'000; ++sample) {
                const auto value = written.nextSample(&plucked, modelValues, 48'000.0);
                alike = alike && value == back.nextSample(pluckedBack.get(), modelValues, 48'000.0);
                energy += static_cast<double>(value) * value;
            }
            expect(alike && energy > 1.0, "a bell or a string read back plays exactly as written (energy " + std::to_string(energy) + ")");
        }
    }

    // Cut short, or another file: refused, nothing half made.
    std::stringstream cut(written.substr(0, written.size() / 2), std::ios::in | std::ios::binary);
    expect(cydaw::engine::readVoiceAssets(cut, error).empty() && !error.empty(), "voice data cut short is refused");
    std::stringstream other(std::string("RIFF0000WAVEfmt "), std::ios::in | std::ios::binary);
    expect(cydaw::engine::readVoiceAssets(other, error).empty(), "a file that is not voice data is refused");
    // A sample that cannot be had fails the writing.
    std::stringstream refused(std::ios::out | std::ios::binary);
    expect(!cydaw::engine::writeVoiceAssets(refused, &map, [](const auto&) { return cydaw::engine::SampleDataRef {}; }, nullptr, error)
               && error.find("A3 soft.wav") != std::string::npos,
           "a sample that cannot be read stops the export, by name");
}
