// The engine tests of Empreinte, Givre
// (SpaceEffects.cpp). Included by EngineTests.cpp inside its anonymous
// namespace (expect(), the allocation counter and the helpers before it are
// in scope); spacefxEffectTests() runs them all.

// One of these effects' nodes: its defaults, then values by binding suffix.
cydaw::engine::NativeDspNode spaceNode(const cydaw::engine::NativeDspNodeKind kind,
                                       const std::vector<std::pair<const char*, float>>& values)
{
    using namespace cydaw::engine;
    NativeDspNode node;
    node.id = 1;
    node.kind = kind;
    if (const auto* effect = effectForKind(kind))
        applyEffectDefaults(*effect, node.parameters);
    for (const auto& [suffix, value] : values)
        if (const auto* parameter = effectParameterForSuffix(suffix))
            setEffectValue(node.parameters, *parameter, value);
    return node;
}

void setSpaceValue(cydaw::engine::NativeDspNode& node, const char* suffix, const float value)
{
    if (const auto* parameter = cydaw::engine::effectParameterForSuffix(suffix))
        cydaw::engine::setEffectValue(node.parameters, *parameter, value);
}

std::unique_ptr<cydaw::engine::AudioProcessor> spaceProcessor(const cydaw::engine::NativeDspNode& node,
                                                              cydaw::engine::ImpulseResponseRef file = {})
{
    using namespace cydaw::engine;
    auto processor = makeEffectProcessor(node);
    if (processor == nullptr)
        return processor;
    if (file != nullptr)
        if (auto* effect = dynamic_cast<EffectProcessor*>(processor.get()))
            effect->setImpulse(std::move(file));
    processor->prepare({ 48'000.0, 256, 2 });
    return processor;
}

// Both channels of a stereo signal through, in blocks of 256.
struct SpaceStereo {
    std::vector<float> left;
    std::vector<float> right;
};
SpaceStereo spaceRun(cydaw::engine::AudioProcessor& processor, const std::vector<float>& left, const std::vector<float>& right,
                     const cydaw::engine::ProcessContext& context = {})
{
    using namespace cydaw::engine;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    SpaceStereo result;
    result.left.reserve(left.size());
    result.right.reserve(left.size());
    for (std::size_t at = 0; at < left.size(); at += 256) {
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(256, left.size() - at));
        const auto input = in.view(count);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            input.channel(0)[frame] = left[at + frame];
            input.channel(1)[frame] = right[at + frame];
        }
        const auto output = out.view(count);
        processor.process(input, output, context);
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            result.left.push_back(output.channel(0)[frame]);
            result.right.push_back(output.channel(1)[frame]);
        }
    }
    return result;
}

std::vector<float> spaceNoise(const std::size_t length, std::uint32_t seed, const double amplitude)
{
    std::vector<float> noise(length);
    for (auto& x : noise) {
        seed = seed * 1'664'525u + 1'013'904'223u;
        x = static_cast<float>((static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0) * amplitude);
    }
    return noise;
}

// The mean power (dB) of a stretch, seconds at 48 kHz.
double spaceLevelDb(const std::vector<float>& x, const double from, const double to)
{
    const auto a = std::min(x.size(), static_cast<std::size_t>(from * 48'000.0));
    const auto b = std::min(x.size(), static_cast<std::size_t>(to * 48'000.0));
    auto sum = 0.0;
    for (auto i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(sum / static_cast<double>(std::max<std::size_t>(1, b - a)) + 1.0e-30);
}

bool spaceFinite(const std::vector<float>& x, const double bound)
{
    for (const auto sample : x)
        if (!std::isfinite(sample) || std::abs(sample) > bound)
            return false;
    return true;
}

std::array<float, cydaw::engine::spacefx::empreinteReadingCount> empreinteReadings(const cydaw::engine::AudioProcessor& processor)
{
    std::array<float, cydaw::engine::spacefx::empreinteReadingCount> values {};
    (void) processor.readings(values.data(), values.size());
    return values;
}

// ---- Empreinte ---------------------------------------------------------------------------------

void empreinteRingsInEverySpace()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    for (int space = 0; space < empreinteSpaceCount; ++space) {
        const auto& room = empreinteRooms[static_cast<std::size_t>(space)];
        const auto name = std::string("Empreinte ") + room.english;
        auto processor = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 1.0f },
                                                                                   { "empreinte.space", static_cast<float>(space) } }));
        expect(processor != nullptr && processor->latencySamples() == 0, "Empreinte is made, with no latency");
        if (processor == nullptr)
            return;
        const auto readings = empreinteReadings(*processor);
        const auto seconds = empreinteFullSecondsOf(room);
        expect(std::abs(readings[empreinteReadingSeconds] - seconds) < 0.01 && readings[empreinteReadingFile] == 0.0f
                   && readings[empreinteReadingBuilding] == 0.0f,
               name + ": its response is the space's length (" + std::to_string(readings[empreinteReadingSeconds]) + " s)");
        auto envelopeOk = true;
        for (int point = 0; point < empreinteEnvelopePoints; ++point) {
            const auto value = readings[static_cast<std::size_t>(empreinteReadingEnvelope + point)];
            envelopeOk = envelopeOk && std::isfinite(value) && value <= 0.01f && value >= empreinteEnvelopeFloorDb;
        }
        expect(envelopeOk, name + ": its envelope is read in dB under its loudest");
        std::vector<float> impulse(static_cast<std::size_t>((seconds + 0.5) * 48'000.0), 0.0f);
        impulse[0] = 1.0f;
        const auto out = spaceRun(*processor, impulse, impulse);
        expect(spaceFinite(out.left, 4.0) && spaceFinite(out.right, 4.0), name + ": its response stays bounded");
        const auto start = std::max(spaceLevelDb(out.left, 0.0, 0.25 * seconds), spaceLevelDb(out.right, 0.0, 0.25 * seconds));
        const auto middle = spaceLevelDb(out.left, 0.4 * seconds, 0.5 * seconds);
        const auto end = spaceLevelDb(out.left, 0.8 * seconds, seconds);
        expect(start > -60.0 && middle > start - 55.0 && end < start - 30.0,
               name + ": it rings, and decays over its length (" + std::to_string(start) + ", " + std::to_string(middle) + ", "
                   + std::to_string(end) + " dB)");
        auto after = 0.0f;
        for (auto i = static_cast<std::size_t>((seconds + 0.02) * 48'000.0); i < out.left.size(); ++i)
            after = std::max({ after, std::abs(out.left[i]), std::abs(out.right[i]) });
        expect(after < 1.0e-5f, name + ": nothing after its length");
        // Its ears hear differently.
        auto lr = 0.0;
        auto ll = 0.0;
        auto rr = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(seconds * 48'000.0); ++i) {
            lr += static_cast<double>(out.left[i]) * out.right[i];
            ll += static_cast<double>(out.left[i]) * out.left[i];
            rr += static_cast<double>(out.right[i]) * out.right[i];
        }
        const auto correlation = lr / std::sqrt(ll * rr + 1.0e-30);
        expect(std::abs(correlation) < 0.9, name + ": its two ears are decorrelated (" + std::to_string(correlation) + ")");
    }
    // At mix 100 %, noise keeps about its level.
    auto hall = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 1.0f } }));
    const auto noise = spaceNoise(48'000 * 4, 21u, 0.3);
    const auto wet = spaceRun(*hall, noise, noise);
    const auto change = spaceLevelDb(wet.left, 2.5, 4.0) - spaceLevelDb(noise, 2.5, 4.0);
    expect(change > -6.0 && change < 6.0, "Empreinte's wet sound sits near the sound's level (" + std::to_string(change) + " dB)");
    // At no mix, the sound as it was.
    auto dry = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 0.0f } }));
    expect(spaceRun(*dry, noise, noise).left == noise, "Empreinte at no mix leaves the sound");
}

void empreinteSwapsWhilePlaying()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    auto node = spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 0.5f }, { "empreinte.space", 0.0f } });
    auto processor = spaceProcessor(node);
    if (processor == nullptr)
        return;
    AudioBuffer in(2, 256);
    AudioBuffer out(2, 256);
    const auto noise = spaceNoise(256 * 4'000, 5u, 0.25);
    std::size_t block = 0;
    auto finite = true;
    const auto play = [&] {
        const auto input = in.view(256);
        for (std::uint32_t frame = 0; frame < 256; ++frame) {
            input.channel(0)[frame] = noise[(block * 256 + frame) % noise.size()];
            input.channel(1)[frame] = noise[(block * 256 + frame + 7'919) % noise.size()];
        }
        const auto output = out.view(256);
        processor->process(input, output, {});
        for (std::uint32_t frame = 0; frame < 256; ++frame)
            finite = finite && std::isfinite(output.channel(0)[frame]) && std::abs(output.channel(0)[frame]) < 8.0f
                     && std::isfinite(output.channel(1)[frame]) && std::abs(output.channel(1)[frame]) < 8.0f;
        ++block;
    };
    for (int i = 0; i < 40; ++i)
        play();
    // A change of space, length or direction while it plays: the new one
    // within about a second of blocks (the worker given time to run).
    const auto follows = [&](const char* suffix, const float value, const double seconds, const char* what) {
        setSpaceValue(node, suffix, value);
        (void) updateEffectProcessor(*processor, node);
        auto heard = -1;
        for (int i = 0; i < 250 && heard < 0; ++i) {
            play();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            const auto readings = empreinteReadings(*processor);
            if (std::abs(readings[empreinteReadingSeconds] - seconds) < 0.01 && readings[empreinteReadingBuilding] == 0.0f)
                heard = i;
        }
        expect(heard >= 0 && heard * 256 < 48'000,
               std::string("Empreinte swaps its response while playing: ") + what + " (" + std::to_string(heard) + " blocks)");
    };
    follows("empreinte.space", 4.0f, empreinteFullSecondsOf(empreinteRooms[4]), "a cathedral");
    follows("empreinte.length", 0.5f, std::round(empreinteFullSecondsOf(empreinteRooms[4]) * 48'000.0 * 0.5) / 48'000.0, "half of it");
    follows("empreinte.reverse", 1.0f, std::round(empreinteFullSecondsOf(empreinteRooms[4]) * 48'000.0 * 0.5) / 48'000.0, "backwards");
    follows("empreinte.space", 6.0f, std::round(empreinteFullSecondsOf(empreinteRooms[6]) * 48'000.0 * 0.5) / 48'000.0, "a spring");
    // Changes faster than it can follow: it ends on the last.
    for (int space = 0; space < empreinteSpaceCount; ++space) {
        setSpaceValue(node, "empreinte.space", static_cast<float>(space));
        (void) updateEffectProcessor(*processor, node);
        play();
    }
    setSpaceValue(node, "empreinte.length", 1.0f);
    setSpaceValue(node, "empreinte.reverse", 0.0f);
    follows("empreinte.space", 1.0f, empreinteFullSecondsOf(empreinteRooms[1]), "after a run of changes");
    expect(finite, "Empreinte stays finite and bounded through its swaps");
}

void empreintePlaysAFile()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    // A recorded response of 7000 taps (direct, head and body), stereo.
    auto recorded = std::make_shared<ImpulseResponse>();
    recorded->name = "test.wav";
    recorded->sampleRate = 48'000.0;
    recorded->channels = { spaceNoise(7'000, 77u, 1.0), spaceNoise(7'000, 78u, 1.0) };
    for (auto& channel : recorded->channels)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] *= static_cast<float>(std::exp(-static_cast<double>(i) / 1'500.0));
    const ImpulseResponseRef file = recorded;
    auto processor = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 1.0f } }), file);
    if (processor == nullptr)
        return;
    const auto* effect = dynamic_cast<const EffectProcessor*>(processor.get());
    const auto readings = empreinteReadings(*processor);
    expect(effect != nullptr && effect->impulse() == file.get(), "Empreinte keeps the response it was given");
    expect(readings[empreinteReadingFile] == 1.0f && std::abs(readings[empreinteReadingSeconds] - 7'000.0f / 48'000.0f) < 1.0e-4f,
           "Empreinte plays a file's response, and says so");
    // What it should play: the response at unit energy, its last tenth faded.
    auto taps = recorded->channels;
    auto energy = 0.0;
    for (const auto& channel : taps)
        for (const auto x : channel)
            energy += static_cast<double>(x) * x;
    const auto scale = static_cast<float>(1.0 / std::sqrt(energy / 2.0));
    for (auto& channel : taps) {
        for (auto& x : channel)
            x *= scale;
        for (std::size_t i = 0; i < 700; ++i)
            channel[7'000 - 700 + i] *= static_cast<float>(0.5 + 0.5 * std::cos(std::numbers::pi * (static_cast<double>(i) + 0.5) / 700.0));
    }
    const auto left = spaceNoise(12'000, 3u, 0.3);
    const auto right = spaceNoise(12'000, 4u, 0.3);
    const auto out = spaceRun(*processor, left, right);
    auto worst = 0.0;
    for (std::size_t n = 0; n < left.size(); ++n) {
        auto l = 0.0;
        auto r = 0.0;
        for (std::size_t k = 0; k < taps[0].size() && k <= n; ++k) {
            l += static_cast<double>(taps[0][k]) * left[n - k];
            r += static_cast<double>(taps[1][k]) * right[n - k];
        }
        worst = std::max({ worst, std::abs(l - out.left[n]), std::abs(r - out.right[n]) });
    }
    expect(worst < 2.0e-3, "Empreinte's convolution is the convolution, with no delay (" + std::to_string(worst) + ")");
    // A one-tap response and a predelay: the sound, that much later.
    auto click = std::make_shared<ImpulseResponse>();
    click->sampleRate = 48'000.0;
    click->channels = { std::vector<float>(64, 0.0f) };
    click->channels[0][0] = 1.0f;
    auto late = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 1.0f }, { "empreinte.predelay", 50.0f } }),
                               ImpulseResponseRef(click));
    const auto delayed = spaceRun(*late, left, left);
    auto off = 0.0;
    for (std::size_t n = 2'400; n < left.size(); ++n)
        off = std::max(off, static_cast<double>(std::abs(delayed.left[n] - left[n - 2'400])));
    expect(off < 1.0e-3, "Empreinte's predelay holds the wet sound back as set");
    // Its length and direction apply to a file's response too.
    auto backwards = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 1.0f }, { "empreinte.reverse", 1.0f },
                                                                             { "empreinte.length", 0.5f } }),
                                    file);
    const auto turned = empreinteReadings(*backwards);
    expect(std::abs(turned[empreinteReadingSeconds] - 3'500.0f / 48'000.0f) < 1.0e-4f
               && turned[empreinteReadingEnvelope] < turned[empreinteReadingEnvelope + empreinteEnvelopePoints - 20],
           "Empreinte cuts and turns a file's response round");
    // A file at another rate keeps its length in seconds.
    auto other = std::make_shared<ImpulseResponse>(*recorded);
    other->sampleRate = 44'100.0;
    auto converted = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, {}), ImpulseResponseRef(other));
    expect(std::abs(empreinteReadings(*converted)[empreinteReadingSeconds] - 7'000.0f / 44'100.0f) < 1.0e-3f,
           "Empreinte reads a file's response at its own rate");
}

void empreinteIsCheapOnLongResponses()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    // Nine seconds of response, kept to eight.
    auto recorded = std::make_shared<ImpulseResponse>();
    recorded->sampleRate = 48'000.0;
    recorded->channels = { spaceNoise(48'000 * 9, 11u, 1.0), spaceNoise(48'000 * 9, 12u, 1.0) };
    for (auto& channel : recorded->channels)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] *= static_cast<float>(std::exp(-static_cast<double>(i) / 96'000.0));
    auto processor = spaceProcessor(spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 0.5f } }), ImpulseResponseRef(recorded));
    if (processor == nullptr)
        return;
    expect(std::abs(empreinteReadings(*processor)[empreinteReadingSeconds] - 8.0f) < 1.0e-3f, "Empreinte keeps eight seconds at most");
    const auto left = spaceNoise(48'000 * 10, 13u, 0.3);
    const auto right = spaceNoise(48'000 * 10, 14u, 0.3);
    const auto began = std::chrono::steady_clock::now();
    const auto out = spaceRun(*processor, left, right);
    const auto took = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    expect(took < 3.0, "Empreinte runs ten seconds through an eight-second response well under real time (" + std::to_string(took) + " s)");
    expect(spaceFinite(out.left, 20.0) && spaceFinite(out.right, 20.0), "Empreinte's long response stays finite");
    // Steady, it allocates nothing.
    AudioBuffer in(2, 256);
    AudioBuffer buffer(2, 256);
    for (std::uint32_t frame = 0; frame < 256; ++frame) {
        in.view(256).channel(0)[frame] = left[frame];
        in.view(256).channel(1)[frame] = right[frame];
    }
    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    for (int block = 0; block < 400; ++block)
        processor->process(in.view(256), buffer.view(256), {});
    allocation_probe::enabled.store(false, std::memory_order_release);
    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0, "Empreinte processes without allocating");
}

void empreinteStaysFiniteAtItsExtremes()
{
    using namespace cydaw::engine;
    const auto* effect = effectForKind(NativeDspNodeKind::empreinte);
    if (effect == nullptr)
        return;
    auto signal = spaceNoise(24'000, 9u, 0.5);
    for (std::size_t i = 0; i < 2'000; ++i)
        signal[i] = i % 400 < 4 ? 1.0f : 0.0f;
    for (const auto& parameter : effect->parameters)
        for (const auto end : { parameter.minimum, parameter.maximum }) {
            auto node = spaceNode(NativeDspNodeKind::empreinte, { { "empreinte.mix", 0.7f } });
            setEffectValue(node.parameters, parameter, end);
            auto processor = spaceProcessor(node);
            if (processor == nullptr)
                continue;
            const auto out = spaceRun(*processor, signal, signal);
            expect(spaceFinite(out.left, 200.0) && spaceFinite(out.right, 200.0),
                   "Empreinte stays finite with " + std::string(parameter.key) + " at " + std::to_string(end));
        }
}

// ---- Givre -------------------------------------------------------------------------------------------

void givreFreezesAndLetsGo()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    auto node = spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f } });
    auto processor = spaceProcessor(node);
    if (processor == nullptr)
        return;
    expect(processor->latencySamples() == static_cast<std::uint32_t>(givreLatency)
               && processor->maximumLatencySamples() == static_cast<std::uint32_t>(givreLatency),
           "Givre reports its frame as its latency");
    const auto tone = sine(440.0, 0.5, 1.0);
    auto out = spaceRun(*processor, tone, tone).left;
    const auto before = spaceLevelDb(out, 0.5, 1.0);
    // Frozen on the sine, which then stops: it goes on.
    setSpaceValue(node, "givre.freeze", 1.0f);
    (void) updateEffectProcessor(*processor, node);
    const auto more = sine(440.0, 0.5, 0.25);
    const auto held = spaceRun(*processor, more, more).left;
    out.insert(out.end(), held.begin(), held.end());
    const std::vector<float> silence(48'000 * 2, 0.0f);
    const auto frozen = spaceRun(*processor, silence, silence).left;
    out.insert(out.end(), frozen.begin(), frozen.end());
    std::array<float, givreReadingCount> readings {};
    (void) processor->readings(readings.data(), readings.size());
    auto lowest = 0.0;
    auto highest = -200.0;
    for (auto at = 1.35; at < 3.2; at += 0.25) {
        lowest = std::min(lowest, spaceLevelDb(out, at, at + 0.25) - before);
        highest = std::max(highest, spaceLevelDb(out, at, at + 0.25) - before);
    }
    expect(lowest > -6.0 && highest < 3.0, "Givre's freeze holds a sine that stopped (" + std::to_string(lowest) + " dB)");
    expect(toneLevelDb(frozen, 440.0, 48'000, 96'000) > toneLevelDb(frozen, 330.0, 48'000, 96'000) + 15.0,
           "Givre's freeze holds the sine's pitch");
    expect(readings[givreReadingFrozen] == 1.0f, "Givre says it is frozen");
    // Let go: it fades.
    setSpaceValue(node, "givre.freeze", 0.0f);
    (void) updateEffectProcessor(*processor, node);
    const auto released = spaceRun(*processor, silence, silence).left;
    expect(spaceLevelDb(released, 1.0, 2.0) < before - 60.0, "Givre lets the sound go when the freeze is off");
    (void) processor->readings(readings.data(), readings.size());
    expect(readings[givreReadingFrozen] == 0.0f, "Givre says it is no longer frozen");

    // Frozen before any sound (a preset): it waits for one and holds it.
    auto waiting = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f },
                                                                       { "givre.freeze", 1.0f } }));
    std::vector<float> late(48'000 * 3, 0.0f);
    const auto note = sine(440.0, 0.5, 0.5);
    std::copy(note.begin(), note.end(), late.begin() + 24'000);
    const auto caught = spaceRun(*waiting, late, late).left;
    const auto heldLevel = spaceLevelDb(caught, 1.5, 3.0);
    expect(heldLevel > before - 12.0 && heldLevel < before + 6.0,
           "Givre frozen before any sound catches the first one (" + std::to_string(heldLevel - before) + " dB)");

    // Synced to quarter notes while the transport plays: caught again on each.
    auto synced = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f },
                                                                      { "givre.freeze", 1.0f }, { "givre.sync", 9.0f } }));
    auto changing = sine(440.0, 0.5, 1.2);
    const auto higher = sine(660.0, 0.5, 1.8);
    changing.insert(changing.end(), higher.begin(), higher.end());
    ProcessContext context;
    context.transport.state = PlaybackState::playing;
    context.transport.beatsPerMinute = 120.0;
    AudioBuffer in(2, 256);
    AudioBuffer outBuffer(2, 256);
    std::vector<float> followed;
    for (std::size_t at = 0; at + 256 <= changing.size(); at += 256) {
        context.transport.quarterNotePosition = static_cast<double>(at) / 48'000.0 * 2.0;
        for (std::uint32_t frame = 0; frame < 256; ++frame) {
            in.view(256).channel(0)[frame] = changing[at + frame];
            in.view(256).channel(1)[frame] = changing[at + frame];
        }
        synced->process(in.view(256), outBuffer.view(256), context);
        followed.insert(followed.end(), outBuffer.view(256).channel(0), outBuffer.view(256).channel(0) + 256);
    }
    // The beat at 1.5 s caught the higher note; the one at 1.0 s the first.
    expect(toneLevelDb(followed, 440.0, 52'800, 70'000) > toneLevelDb(followed, 660.0, 52'800, 70'000) + 15.0
               && toneLevelDb(followed, 660.0, 76'800, 93'600) > toneLevelDb(followed, 440.0, 76'800, 93'600) + 15.0,
           "Givre's synced freeze catches the sound again on the beat");
}

void givrePassesAndShifts()
{
    using namespace cydaw::engine;
    using namespace cydaw::engine::spacefx;
    // No blur, no shift: the sound, a frame late.
    auto still = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f }, { "givre.shift", 0.0f } }));
    if (still == nullptr)
        return;
    const auto noise = spaceNoise(48'000, 31u, 0.3);
    const auto out = spaceRun(*still, noise, noise).left;
    auto xy = 0.0;
    auto xx = 0.0;
    auto yy = 0.0;
    for (std::size_t n = 4'096; n + givreLatency < noise.size(); ++n) {
        const auto x = static_cast<double>(noise[n]);
        const auto y = static_cast<double>(out[n + givreLatency]);
        xy += x * y;
        xx += x * x;
        yy += y * y;
    }
    const auto correlation = xy / std::sqrt(xx * yy + 1.0e-30);
    expect(correlation > 0.95 && std::abs(10.0 * std::log10(yy / xx)) < 1.0,
           "Givre at rest is the sound delayed by its frame (" + std::to_string(correlation) + ")");
    // Up an octave: 500 Hz becomes 1 kHz.
    auto up = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f }, { "givre.shift", 12.0f } }));
    const auto tone = sine(500.0, 0.5, 1.5);
    const auto shifted = spaceRun(*up, tone, tone).left;
    const auto octave = toneLevelDb(shifted, 1'000.0, 24'000, 72'000);
    expect(octave > -16.0 && octave > toneLevelDb(shifted, 500.0, 24'000, 72'000) + 20.0,
           "Givre's shift moves 500 Hz to 1 kHz (" + std::to_string(octave) + " dB)");
    std::array<float, givreReadingCount> readings {};
    (void) up->readings(readings.data(), readings.size());
    auto loudest = 0;
    for (int band = 1; band < givreBands; ++band)
        if (readings[static_cast<std::size_t>(givreReadingSpectrum + band)] > readings[static_cast<std::size_t>(givreReadingSpectrum + loudest)])
            loudest = band;
    expect(std::abs(givreBandHzOf(loudest) / 1'000.0 - 1.0) < 0.12 && readings[static_cast<std::size_t>(givreReadingSpectrum + loudest)] > -20.0f,
           "Givre shows its output's spectrum (" + std::to_string(givreBandHzOf(loudest)) + " Hz)");
    // Blurred, a burst lingers; unblurred, it stops with the sound.
    auto burst = spaceNoise(48'000 * 2, 41u, 0.3);
    std::fill(burst.begin() + 9'600, burst.end(), 0.0f);
    auto blurred = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.8f } }));
    auto sharp = spaceProcessor(spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 1.0f }, { "givre.blur", 0.0f } }));
    const auto smeared = spaceRun(*blurred, burst, burst).left;
    const auto clean = spaceRun(*sharp, burst, burst).left;
    const auto heard = spaceLevelDb(smeared, 0.05, 0.25);
    expect(spaceLevelDb(smeared, 0.6, 0.9) > heard - 40.0 && spaceLevelDb(clean, 0.6, 0.9) < heard - 80.0,
           "Givre's blur lets a burst linger");
}

void givreStaysFiniteAtItsExtremes()
{
    using namespace cydaw::engine;
    const auto* effect = effectForKind(NativeDspNodeKind::givre);
    if (effect == nullptr)
        return;
    auto signal = spaceNoise(24'000, 19u, 0.5);
    const auto tone = sine(220.0, 0.8, 0.25);
    std::copy(tone.begin(), tone.end(), signal.begin() + 12'000);
    for (const auto& parameter : effect->parameters)
        for (const auto end : { parameter.minimum, parameter.maximum }) {
            auto node = spaceNode(NativeDspNodeKind::givre, { { "givre.mix", 0.8f } });
            setEffectValue(node.parameters, parameter, end);
            auto processor = spaceProcessor(node);
            if (processor == nullptr)
                continue;
            ProcessContext context;
            context.transport.state = PlaybackState::playing;
            context.transport.beatsPerMinute = 180.0;
            const auto out = spaceRun(*processor, signal, signal, context);
            expect(spaceFinite(out.left, 100.0) && spaceFinite(out.right, 100.0),
                   "Givre stays finite with " + std::string(parameter.key) + " at " + std::to_string(end));
        }
    // Everything at once, frozen and synced to a playing transport.
    auto node = spaceNode(NativeDspNodeKind::givre, { { "givre.blur", 1.0f }, { "givre.smear", 1.0f }, { "givre.shift", -12.0f },
                                                     { "givre.tone", 1.0f }, { "givre.scatter", 1.0f }, { "givre.freeze", 1.0f },
                                                     { "givre.sync", 1.0f }, { "givre.output", 24.0f } });
    auto processor = spaceProcessor(node);
    ProcessContext context;
    context.transport.state = PlaybackState::playing;
    context.transport.beatsPerMinute = 200.0;
    const auto out = spaceRun(*processor, signal, signal, context);
    expect(spaceFinite(out.left, 400.0), "Givre stays finite with everything turned up");
    // Steady, it allocates nothing.
    AudioBuffer in(2, 256);
    AudioBuffer buffer(2, 256);
    for (std::uint32_t frame = 0; frame < 256; ++frame) {
        in.view(256).channel(0)[frame] = signal[frame];
        in.view(256).channel(1)[frame] = signal[frame + 300];
    }
    allocation_probe::count.store(0, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    for (int block = 0; block < 200; ++block)
        processor->process(in.view(256), buffer.view(256), context);
    allocation_probe::enabled.store(false, std::memory_order_release);
    expect(allocation_probe::count.load(std::memory_order_relaxed) == 0, "Givre processes without allocating");
}

void spacefxEffectTests()
{
    empreinteRingsInEverySpace();
    empreinteSwapsWhilePlaying();
    empreintePlaysAFile();
    empreinteIsCheapOnLongResponses();
    empreinteStaysFiniteAtItsExtremes();
    givreFreezesAndLetsGo();
    givrePassesAndShifts();
    givreStaysFiniteAtItsExtremes();
}
