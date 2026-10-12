#include <cydaw/engine/Sampler.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

namespace {

// A zone's value at a fractional frame, both channels: four-point Hermite
// between the frames around it, silence outside the file. A frame not in
// memory comes from the lane's ring (a long recording read from the disk),
// silence when it is not there either.
void readFrame(const SampleData& sample, const SampleStream* stream, const double position, double& left,
               double& right) noexcept
{
    const auto base = static_cast<std::int64_t>(std::floor(position));
    const auto fraction = position - static_cast<double>(base);
    const auto channels = sample.channels;
    const auto frames = sample.frames;
    const auto heldStart = sample.heldStart();
    const auto heldEnd = sample.heldEnd();
    const auto* pcm = sample.pcm.data();
    const auto scale = static_cast<double>(sample.scale);
    constexpr auto ringScale = 1.0 / 32'767.0;
    std::array<double, 4> l {};
    std::array<double, 4> r {};
    for (int tap = 0; tap < 4; ++tap) {
        const auto frame = base - 1 + tap;
        if (frame < 0 || frame >= frames)
            continue;
        if (frame >= heldStart && frame < heldEnd) {
            const auto at = static_cast<std::size_t>(frame - heldStart) * static_cast<std::size_t>(channels);
            l[static_cast<std::size_t>(tap)] = pcm[at] * scale;
            r[static_cast<std::size_t>(tap)] = (channels > 1 ? pcm[at + 1] : pcm[at]) * scale;
        } else if (stream != nullptr && stream->has(frame)) {
            const auto* cell = stream->cell(frame);
            l[static_cast<std::size_t>(tap)] = cell[0] * ringScale;
            r[static_cast<std::size_t>(tap)] = cell[1] * ringScale;
        }
    }
    const auto hermite = [fraction](const std::array<double, 4>& y) {
        const auto c1 = 0.5 * (y[2] - y[0]);
        const auto c2 = y[0] - 2.5 * y[1] + 2.0 * y[2] - 0.5 * y[3];
        const auto c3 = 0.5 * (y[3] - y[0]) + 1.5 * (y[1] - y[2]);
        return ((c3 * fraction + c2) * fraction + c1) * fraction + y[1];
    };
    left = hermite(l);
    right = hermite(r);
}

[[nodiscard]] std::int64_t zoneEnd(const SamplerZone& zone, const SampleData& sample) noexcept
{
    return zone.end >= 0 ? std::min(zone.end, sample.frames) : sample.frames;
}

[[nodiscard]] bool playsInRound(const SamplerZone& zone, const std::uint32_t round) noexcept
{
    if (zone.sequenceLength <= 1)
        return true;
    return static_cast<int>(round % static_cast<std::uint32_t>(zone.sequenceLength)) + 1 == zone.sequencePosition;
}

// Whether a zone sounds for this kind of event, with `held` other keys down.
[[nodiscard]] bool triggers(const SamplerZone& zone, const SamplerTrigger event, const int held) noexcept
{
    switch (zone.trigger) {
    case SamplerTrigger::attack: return event == SamplerTrigger::attack;
    case SamplerTrigger::first: return event == SamplerTrigger::attack && held == 0;
    case SamplerTrigger::legato: return event == SamplerTrigger::attack && held > 0;
    case SamplerTrigger::release: return event == SamplerTrigger::release;
    }
    return false;
}

[[nodiscard]] bool movesWhilePlaying(const SamplerZone& zone) noexcept
{
    return std::ranges::any_of(zone.rules, [](const SamplerRule& rule) {
        return rule.kind == SamplerRuleKind::level || rule.kind == SamplerRuleKind::volume
               || rule.kind == SamplerRuleKind::fadeIn || rule.kind == SamplerRuleKind::fadeOut;
    });
}

// The level of the whole velocity range, in dB, full at the top.
[[nodiscard]] double velocityGainOf(const SamplerValues& values, const double velocity) noexcept
{
    return std::pow(10.0, static_cast<double>(samplerValue(values, SamplerSetting::velocityDb))
                              * (std::clamp(velocity, 0.0, 1.0) - 1.0) / 20.0);
}

} // namespace

// ---- Reading from the disk --------------------------------------------------------------

SampleStreams& SampleStreams::instance()
{
    // Never destroyed: a voice may give its rings back while the program ends.
    static auto* streams = new SampleStreams();
    return *streams;
}

void SampleStreams::reserve(const int count)
{
    if (count <= 0 || reserving_.exchange(true))
        return;
    auto streams = std::make_unique<SampleStream[]>(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
        streams[static_cast<std::size_t>(index)].ring.assign(static_cast<std::size_t>(SampleStream::capacity) * 2, 0);
    streams_ = std::move(streams);
    count_.store(count, std::memory_order_release);
}

SampleStream* SampleStreams::claim(const SampleDataRef& sample, const std::int64_t from, const int direction) noexcept
{
    const auto count = count_.load(std::memory_order_acquire);
    for (int index = 0; index < count; ++index) {
        auto& stream = streams_[static_cast<std::size_t>(index)];
        auto expected = static_cast<int>(SampleStream::free);
        if (!stream.state.compare_exchange_strong(expected, SampleStream::claiming, std::memory_order_acq_rel))
            continue;
        // The streamer dropped the last one's: only a reference taken here.
        stream.sample = sample;
        stream.from = from;
        stream.direction = direction < 0 ? -1 : 1;
        stream.reading.store(from, std::memory_order_relaxed);
        stream.low.store(from, std::memory_order_relaxed);
        stream.high.store(from, std::memory_order_relaxed);
        stream.state.store(SampleStream::wanted, std::memory_order_release);
        return &stream;
    }
    refused.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

void SampleStreams::letGo(SampleStream* const stream) noexcept
{
    if (stream != nullptr)
        stream->state.store(SampleStream::letGo, std::memory_order_release);
}

int SampleStreams::busy() const noexcept
{
    auto busy = 0;
    const auto count = count_.load(std::memory_order_acquire);
    for (int index = 0; index < count; ++index)
        busy += streams_[static_cast<std::size_t>(index)].state.load(std::memory_order_acquire) != SampleStream::free ? 1 : 0;
    return busy;
}

// ---- Controllers ---------------------------------------------------------------------

void SamplerPlaying::follow(const SamplerMap* map, const SamplerControllers& card, const float articulation) noexcept
{
    following = true;
    for (std::size_t number = 0; number < controllers.size(); ++number) {
        const auto wanted = card[number] >= 0.0f ? card[number]
                            : map != nullptr     ? map->controllerStart[number]
                                                 : defaultSamplerControllers()[number];
        // NaN (nothing followed yet) differs from every value.
        if (!(wanted == followed[number])) {
            controllers[number] = wanted;
            followed[number] = wanted;
        }
    }
    if (!(articulation == followedArticulation)) {
        keyswitch = -1;
        followedArticulation = articulation;
    }
}

void SamplerPlaying::controllerMoved(const int controller, const int value) noexcept
{
    if (controller == 121) {
        for (std::size_t number = 0; number < controllers.size(); ++number)
            if (followed[number] == followed[number])
                controllers[number] = followed[number];
        return;
    }
    if (controller >= 0 && controller < 128)
        controllers[static_cast<std::size_t>(controller)] = static_cast<float>(std::clamp(value, 0, 127));
}

float SamplerCues::controller(const SamplerMap& map, const int number) const noexcept
{
    const auto index = static_cast<std::size_t>(std::clamp(number, 0, 127));
    if (playing != nullptr && playing->following)
        return playing->controllers[index];
    if (card != nullptr && (*card)[index] >= 0.0f)
        return (*card)[index];
    return map.controllerStart[index];
}

int SamplerCues::keyswitch(const SamplerMap& map) const noexcept
{
    if (playing != nullptr && samplerKeyswitch(map, playing->keyswitch))
        return playing->keyswitch;
    if (const auto chosen = static_cast<int>(std::lround(articulation)); samplerKeyswitch(map, chosen))
        return chosen;
    return map.keyswitchStart;
}

float samplerCurve(const SamplerMap& map, const int curve, const float value) noexcept
{
    const auto at = std::clamp(value, 0.0f, 127.0f);
    if (curve < 0 || curve >= static_cast<int>(map.curves.size()))
        return at / 127.0f;
    const auto& table = map.curves[static_cast<std::size_t>(curve)];
    const auto low = static_cast<int>(std::floor(at));
    const auto high = std::min(127, low + 1);
    const auto t = at - static_cast<float>(low);
    return table[static_cast<std::size_t>(low)] + (table[static_cast<std::size_t>(high)] - table[static_cast<std::size_t>(low)]) * t;
}

bool samplerRangesHold(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept
{
    for (const auto& rule : zone.rules)
        if (rule.kind == SamplerRuleKind::range) {
            const auto value = cues.controller(map, rule.controller);
            if (value < rule.a || value > rule.b)
                return false;
        }
    return true;
}

bool samplerZoneReachable(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept
{
    if (zone.keyswitch >= 0 && zone.keyswitch != cues.keyswitch(map))
        return false;
    if (zone.rules.empty())
        return true;
    if (!samplerRangesHold(map, zone, cues))
        return false;
    auto levels = 1.0;
    auto decibels = 0.0;
    for (const auto& rule : zone.rules) {
        const auto value = samplerCurve(map, rule.curve, cues.controller(map, rule.controller));
        if (rule.kind == SamplerRuleKind::level)
            levels *= static_cast<double>(rule.a) / 100.0 * static_cast<double>(value);
        else if (rule.kind == SamplerRuleKind::volume)
            decibels += static_cast<double>(rule.a) * static_cast<double>(value);
    }
    return levels * std::pow(10.0, std::clamp(decibels, -144.0, 48.0) / 20.0) > 1.0e-4;
}

double samplerRulesGain(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept
{
    auto gain = 1.0;
    auto decibels = 0.0;
    for (const auto& rule : zone.rules) {
        const auto value = static_cast<double>(cues.controller(map, rule.controller));
        const auto low = static_cast<double>(rule.a);
        const auto high = static_cast<double>(rule.b);
        switch (rule.kind) {
        case SamplerRuleKind::level:
            gain *= static_cast<double>(rule.a) / 100.0 * static_cast<double>(samplerCurve(map, rule.curve, static_cast<float>(value)));
            break;
        case SamplerRuleKind::volume:
            decibels += static_cast<double>(rule.a) * static_cast<double>(samplerCurve(map, rule.curve, static_cast<float>(value)));
            break;
        case SamplerRuleKind::fadeIn:
            gain *= value <= low ? (high <= low && value >= high ? 1.0 : 0.0)
                    : value >= high ? 1.0
                                    : std::sqrt((value - low) / (high - low));
            break;
        case SamplerRuleKind::fadeOut:
            gain *= value >= high ? (high <= low && value <= low ? 1.0 : 0.0)
                    : value <= low ? 1.0
                                   : std::sqrt((high - value) / (high - low));
            break;
        case SamplerRuleKind::range:
        case SamplerRuleKind::pan:
        case SamplerRuleKind::tune:
        case SamplerRuleKind::offset: break;
        }
    }
    return std::max(0.0, gain) * std::pow(10.0, std::clamp(decibels, -144.0, 48.0) / 20.0);
}

// ---- Choosing ------------------------------------------------------------------------

SamplerZoneChoice chooseSamplerZones(const SamplerMap& map, const int midiNote, const double velocity,
                                     const bool crossfade, const std::uint32_t round, const SamplerCues& cues,
                                     const SamplerTrigger trigger) noexcept
{
    SamplerZoneChoice choice;
    const auto velocity127 = std::clamp(static_cast<int>(std::lround(velocity * 127.0)), 1, 127);
    const auto zoneCount = static_cast<int>(map.zones.size());
    const auto held = cues.heldKeys();
    const auto keyswitch = map.hasKeyswitches() ? cues.keyswitch(map) : -1;
    const auto holdsKey = [&](const SamplerZone& zone) {
        if (midiNote < zone.lowKey || midiNote > zone.highKey || !playsInRound(zone, round) || !triggers(zone, trigger, held))
            return false;
        if (zone.keyswitch >= 0 && zone.keyswitch != keyswitch)
            return false;
        return zone.rules.empty() || samplerRangesHold(map, zone, cues);
    };
    const auto add = [&choice](const int index, const double weight) {
        if (choice.count >= SamplerZoneChoice::capacity)
            return;
        choice.zones[static_cast<std::size_t>(choice.count)] = index;
        choice.weights[static_cast<std::size_t>(choice.count)] = weight;
        ++choice.count;
    };
    if (!crossfade) {
        for (int index = 0; index < zoneCount && choice.count < SamplerZoneChoice::capacity; ++index) {
            const auto& zone = map.zones[static_cast<std::size_t>(index)];
            if (velocity127 >= zone.lowVelocity && velocity127 <= zone.highVelocity && holdsKey(zone))
                add(index, 1.0);
        }
        return choice;
    }
    // The layers holding the key, by the middle of their velocity range: the
    // nearest below the velocity and the nearest above; every zone of each
    // (the microphones of one hit share their range).
    auto belowCentre = -1.0;
    auto aboveCentre = 1'000.0;
    const auto centreOf = [](const SamplerZone& zone) {
        return 0.5 * static_cast<double>(zone.lowVelocity + zone.highVelocity);
    };
    for (int index = 0; index < zoneCount; ++index) {
        const auto& zone = map.zones[static_cast<std::size_t>(index)];
        if (!holdsKey(zone))
            continue;
        const auto centre = centreOf(zone);
        if (centre <= velocity127 && centre > belowCentre)
            belowCentre = centre;
        if (centre >= velocity127 && centre < aboveCentre)
            aboveCentre = centre;
    }
    const auto hasBelow = belowCentre >= 0.0;
    const auto hasAbove = aboveCentre < 1'000.0;
    if (!hasBelow && !hasAbove)
        return choice;
    if (!hasBelow || !hasAbove || belowCentre == aboveCentre) {
        const auto only = hasBelow ? belowCentre : aboveCentre;
        for (int index = 0; index < zoneCount; ++index) {
            const auto& zone = map.zones[static_cast<std::size_t>(index)];
            if (centreOf(zone) == only && holdsKey(zone))
                add(index, 1.0);
        }
        return choice;
    }
    const auto t = std::clamp((static_cast<double>(velocity127) - belowCentre) / std::max(1e-6, aboveCentre - belowCentre),
                              0.0, 1.0);
    const auto belowWeight = std::cos(t * std::numbers::pi * 0.5);
    const auto aboveWeight = std::sin(t * std::numbers::pi * 0.5);
    for (int index = 0; index < zoneCount; ++index) {
        const auto& zone = map.zones[static_cast<std::size_t>(index)];
        const auto centre = centreOf(zone);
        if ((centre == belowCentre || centre == aboveCentre) && holdsKey(zone))
            add(index, centre == belowCentre ? belowWeight : aboveWeight);
    }
    return choice;
}

int samplerGroupOf(const SamplerMap& map, const int midiNote, const double velocity, const bool crossfade,
                   const SamplerCues& cues) noexcept
{
    const auto choice = chooseSamplerZones(map, midiNote, velocity, crossfade, map.round.load(std::memory_order_relaxed),
                                           cues);
    for (int i = 0; i < choice.count; ++i)
        if (const auto zone = choice.zones[static_cast<std::size_t>(i)]; zone >= 0 && zone < static_cast<int>(map.zones.size()))
            if (const auto group = map.zones[static_cast<std::size_t>(zone)].group; group != 0)
                return group;
    return 0;
}

// ---- Playing -------------------------------------------------------------------------

void SamplerVoice::letGoStreams() noexcept
{
    for (auto& lane : lanes_)
        if (lane.stream != nullptr) {
            SampleStreams::instance().letGo(lane.stream);
            lane.stream = nullptr;
        }
}

void SamplerVoice::finish(Lane& lane) noexcept
{
    lane.done = true;
    if (lane.stream != nullptr) {
        SampleStreams::instance().letGo(lane.stream);
        lane.stream = nullptr;
    }
}

bool SamplerVoice::framesThere(const Lane& lane, const SampleData& sample, const double position) noexcept
{
    const auto base = static_cast<std::int64_t>(std::floor(position));
    for (auto frame = base - 1; frame <= base + 2; ++frame)
        if (frame >= 0 && frame < sample.frames && !sample.holds(frame) && (lane.stream == nullptr || !lane.stream->has(frame)))
            return false;
    return true;
}

void SamplerVoice::follow(Lane& lane, const SamplerZone& zone) noexcept
{
    if (lane.stream != nullptr || lane.noStream || lane.done || zone.sample == nullptr || !zone.sample->streamed)
        return;
    const auto& sample = *zone.sample;
    // A loop the head holds needs nothing more while it loops.
    if (lane.looping && zone.loopEnd <= sample.heldEnd())
        return;
    // Asked for this far ahead of the end of what is in memory: 0.3 s of
    // output (more played higher), the streamer's head start.
    const auto lead = std::max(4'096.0, std::abs(lane.increment) * pitchRatio_ * rate_ * 0.3);
    const auto at = static_cast<std::int64_t>(std::floor(lane.position));
    std::int64_t from {};
    if (!lane.reversed) {
        if (lane.position + lead < static_cast<double>(sample.heldEnd()) || sample.heldEnd() >= zoneEnd(zone, sample))
            return;
        from = at - 2 >= sample.heldEnd() ? at - 2 : sample.heldEnd();
    } else {
        if (lane.position - lead > static_cast<double>(sample.heldStart()) || sample.heldStart() <= std::max<std::int64_t>(0, zone.start))
            return;
        from = at + 3 <= sample.heldStart() ? at + 3 : sample.heldStart();
    }
    lane.stream = SampleStreams::instance().claim(zone.sample, from, lane.reversed ? -1 : 1);
    lane.noStream = lane.stream == nullptr;
}

void SamplerVoice::reset() noexcept
{
    pitchRatio_ = 1.0;
    letGoStreams();
    for (auto& lane : lanes_)
        lane = {};
    laneCount_ = 0;
    active_ = false;
    letGoOnRelease_ = false;
    released_ = false;
    releasePending_ = false;
    played_ = 0.0;
    rulesCountdown_ = 0;
}

void SamplerVoice::place(Lane& lane, const SamplerZone& zone, const SampleData& sample, const double elapsed) const noexcept
{
    lane.increment = std::pow(2.0, lane.semitones / 12.0) * sample.sampleRate / rate_;
    const auto end = zoneEnd(zone, sample);
    const auto first = std::clamp(zone.start + lane.offset, std::int64_t {}, std::max<std::int64_t>(0, end - 1));
    const auto moved = elapsed * lane.increment;
    if (lane.tail) {
        // A release's sound: from its start, once through.
        lane.reversed = false;
        lane.looping = false;
        lane.position = static_cast<double>(first) + moved;
        lane.done = lane.position >= static_cast<double>(end);
        return;
    }
    const auto skipped = skippedMs_ * 0.001 * sample.sampleRate;
    lane.reversed = reversed_;
    if (reversed_) {
        lane.position = static_cast<double>(end - 1) - skipped - moved;
        lane.looping = false;
        lane.done = lane.position < static_cast<double>(first);
        return;
    }
    lane.position = static_cast<double>(first) + skipped + moved;
    lane.looping = loopMode_ > 0 && zone.loops() && zone.loopEnd <= end && !(letGoOnRelease_ && released_);
    if (lane.looping && lane.position >= static_cast<double>(zone.loopEnd)) {
        // Late into a loop: where its turns have brought it.
        const auto length = static_cast<double>(zone.loopEnd - zone.loopStart);
        lane.position = static_cast<double>(zone.loopStart)
                        + std::fmod(lane.position - static_cast<double>(zone.loopStart), length);
    }
    lane.done = lane.position >= static_cast<double>(end);
}

void SamplerVoice::addLanes(const SamplerMap& map, const SamplerZoneChoice& choice, const SamplerValues& values,
                            const SamplerCues& cues, const double gain, const bool tail) noexcept
{
    const auto value = [&values](const SamplerSetting setting) {
        return static_cast<double>(samplerValue(values, setting));
    };
    const auto keyTrack = std::clamp(value(SamplerSetting::keyTrack), 0.0, 1.0);
    for (int index = 0; index < choice.count && laneCount_ < laneCapacity; ++index) {
        const auto zoneIndex = choice.zones[static_cast<std::size_t>(index)];
        const auto& zone = map.zones[static_cast<std::size_t>(zoneIndex)];
        if (zone.sample == nullptr || zone.sample->failed.load(std::memory_order_acquire))
            continue;
        const auto& sample = *zone.sample;
        // What the controllers set at the note-on: its pan, its tuning, where
        // in the file it starts.
        auto panMoved = 0.0;
        auto centsMoved = 0.0;
        auto framesMoved = 0.0;
        for (const auto& rule : zone.rules) {
            if (rule.kind != SamplerRuleKind::pan && rule.kind != SamplerRuleKind::tune && rule.kind != SamplerRuleKind::offset)
                continue;
            const auto amount = static_cast<double>(rule.a)
                                * static_cast<double>(samplerCurve(map, rule.curve, cues.controller(map, rule.controller)));
            (rule.kind == SamplerRuleKind::pan ? panMoved : rule.kind == SamplerRuleKind::tune ? centsMoved : framesMoved) += amount;
        }
        auto& lane = lanes_[static_cast<std::size_t>(laneCount_)];
        lane = {};
        lane.zone = zoneIndex;
        lane.sample = &sample;
        lane.tail = tail;
        lane.semitones = keyTrack * (note_ - static_cast<double>(zone.rootNote)) + value(SamplerSetting::transpose)
                         + (value(SamplerSetting::fine) + static_cast<double>(zone.tuneCents) + centsMoved) / 100.0;
        auto level = std::pow(10.0, static_cast<double>(zone.gainDb) / 20.0) * gain
                     * choice.weights[static_cast<std::size_t>(index)];
        if (tail && zone.releaseDecayDb > 0.0f)
            level *= std::pow(10.0, -static_cast<double>(zone.releaseDecayDb) * (played_ / rate_) / 20.0);
        const auto pan = std::clamp(static_cast<double>(zone.pan) + panMoved, -1.0, 1.0);
        lane.gainLeft = level * std::min(1.0, 1.0 - pan);
        lane.gainRight = level * std::min(1.0, 1.0 + pan);
        lane.offset = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::llround(framesMoved)));
        lane.moves = movesWhilePlaying(zone);
        lane.rulesTarget = lane.moves ? samplerRulesGain(map, zone, cues) : 1.0;
        lane.rulesGain = lane.rulesTarget;
        if (sample.playable()) {
            place(lane, zone, sample, 0.0);
            follow(lane, zone);
        } else {
            // Still being decoded (or not yet asked for: now it is): it comes
            // in when it is there.
            zone.sample->request();
            lane.pending = true;
            lane.done = false;
        }
        if (!lane.done)
            ++laneCount_;
    }
}

void SamplerVoice::start(const SamplerMap& map, const SamplerValues& values, const double midiNote,
                         const double velocity, const double sampleRate, const SamplerCues& cues) noexcept
{
    reset();
    rate_ = std::max(1.0, sampleRate);
    note_ = midiNote;
    velocity_ = velocity;
    const auto value = [&values](const SamplerSetting setting) {
        return static_cast<double>(samplerValue(values, setting));
    };
    round_ = map.round.fetch_add(1, std::memory_order_relaxed);
    const auto choice = chooseSamplerZones(map, static_cast<int>(std::lround(midiNote)), velocity,
                                           value(SamplerSetting::velocityCrossfade) >= 0.5, round_, cues);
    loopMode_ = static_cast<int>(std::lround(value(SamplerSetting::loop)));
    letGoOnRelease_ = loopMode_ == 2;
    reversed_ = value(SamplerSetting::reverse) >= 0.5;
    skippedMs_ = value(SamplerSetting::start);
    // Controllers move a level over 5 ms, so a fader does not click.
    rulesEase_ = 1.0 - std::exp(-1.0 / (0.005 * rate_));
    addLanes(map, choice, values, cues, velocityGainOf(values, velocity), false);
    active_ = laneCount_ > 0;
}

void SamplerVoice::startRelease(const SamplerMap& map, const SamplerValues& values, const SamplerCues& cues) noexcept
{
    // Room for it: the lanes done so far go.
    auto kept = 0;
    for (int index = 0; index < laneCount_; ++index) {
        auto& lane = lanes_[static_cast<std::size_t>(index)];
        if (lane.done) {
            finish(lane);
            continue;
        }
        if (kept != index) {
            lanes_[static_cast<std::size_t>(kept)] = lane;
            lane.stream = nullptr;
            lane.done = true;
        }
        ++kept;
    }
    laneCount_ = kept;
    const auto choice = chooseSamplerZones(map, static_cast<int>(std::lround(note_)), velocity_,
                                           samplerValue(values, SamplerSetting::velocityCrossfade) >= 0.5f, round_, cues,
                                           SamplerTrigger::release);
    addLanes(map, choice, values, cues, velocityGainOf(values, velocity_), true);
}

int SamplerVoice::offBy(const SamplerMap& map) const noexcept
{
    if (!active_)
        return 0;
    for (int i = 0; i < laneCount_; ++i) {
        const auto& lane = lanes_[static_cast<std::size_t>(i)];
        if (lane.tail || lane.zone < 0 || lane.zone >= static_cast<int>(map.zones.size()))
            continue;
        if (const auto offBy = map.zones[static_cast<std::size_t>(lane.zone)].offBy; offBy != 0)
            return offBy;
    }
    return 0;
}

void SamplerVoice::release(const bool sounds) noexcept
{
    if (released_)
        return;
    released_ = true;
    if (letGoOnRelease_)
        for (int index = 0; index < laneCount_; ++index)
            if (!lanes_[static_cast<std::size_t>(index)].tail)
                lanes_[static_cast<std::size_t>(index)].looping = false;
    releasePending_ = sounds && active_;
}

bool SamplerVoice::tailing() const noexcept
{
    if (releasePending_)
        return true;
    for (int index = 0; index < laneCount_; ++index) {
        const auto& lane = lanes_[static_cast<std::size_t>(index)];
        if (lane.tail && !lane.done)
            return true;
    }
    return false;
}

void SamplerVoice::endNote() noexcept
{
    for (int index = 0; index < laneCount_; ++index)
        if (!lanes_[static_cast<std::size_t>(index)].tail)
            finish(lanes_[static_cast<std::size_t>(index)]);
}

void SamplerVoice::next(const SamplerMap* map, const SamplerValues& values, double& left, double& right) noexcept
{
    auto tailLeft = 0.0;
    auto tailRight = 0.0;
    next(map, values, SamplerCues {}, left, right, tailLeft, tailRight);
    left += tailLeft;
    right += tailRight;
}

void SamplerVoice::next(const SamplerMap* map, const SamplerValues& values, const SamplerCues& cues, double& left,
                        double& right, double& tailLeft, double& tailRight) noexcept
{
    left = 0.0;
    right = 0.0;
    tailLeft = 0.0;
    tailRight = 0.0;
    if (!active_)
        return;
    if (map == nullptr) {
        active_ = false;
        return;
    }
    if (releasePending_) {
        releasePending_ = false;
        if (map->hasReleaseZones)
            startRelease(*map, values, cues);
    }
    played_ += 1.0;
    // The controllers are read again every 64 samples.
    const auto refresh = --rulesCountdown_ <= 0;
    if (refresh)
        rulesCountdown_ = 64;
    auto playing = 0;
    for (int index = 0; index < laneCount_; ++index) {
        auto& lane = lanes_[static_cast<std::size_t>(index)];
        if (lane.done)
            continue;
        // An edit that replaced the map, or this zone's sample, ends the lane.
        if (lane.zone < 0 || static_cast<std::size_t>(lane.zone) >= map->zones.size()
            || map->zones[static_cast<std::size_t>(lane.zone)].sample.get() != lane.sample) {
            finish(lane);
            continue;
        }
        const auto& zone = map->zones[static_cast<std::size_t>(lane.zone)];
        const auto& sample = *zone.sample;
        if (lane.pending) {
            if (sample.failed.load(std::memory_order_acquire) || lane.waited > rate_ * 60.0) {
                finish(lane);
                continue;
            }
            if (!sample.playable()) {
                lane.waited += 1.0;
                ++playing;
                continue;
            }
            // There now: where the note would be, faded in.
            place(lane, zone, sample, lane.waited);
            lane.pending = false;
            lane.fadeIn = 0.0;
            if (lane.done)
                continue;
            follow(lane, zone);
        }
        // A long recording: its ring asked for ahead of the end of what is in
        // memory; frames not there yet wait (before the lane has sounded, or
        // when rendering offline) or are missed (the disk was too slow).
        if (sample.streamed) {
            if (refresh)
                follow(lane, zone);
            if (!framesThere(lane, sample, lane.position)) {
                follow(lane, zone);
                if (lane.stream == nullptr && !lane.noStream) {
                    // Past its head where follow() did not look (a start far
                    // in): a ring from where it reads.
                    const auto forward = !lane.reversed;
                    lane.stream = SampleStreams::instance().claim(
                        zone.sample, forward ? static_cast<std::int64_t>(std::floor(lane.position)) - 2
                                             : static_cast<std::int64_t>(std::floor(lane.position)) + 3,
                        forward ? 1 : -1);
                    lane.noStream = lane.stream == nullptr;
                }
                if (lane.stream != nullptr) {
                    lane.stream->reading.store(static_cast<std::int64_t>(std::floor(lane.position)), std::memory_order_release);
                    if (samplerMayWait)
                        for (auto tries = 0; tries < 20'000 && !framesThere(lane, sample, lane.position); ++tries)
                            std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                if (!framesThere(lane, sample, lane.position)) {
                    if (!lane.sounded && lane.streamWaited < rate_) {
                        lane.streamWaited += 1.0;
                        ++playing;
                        continue;
                    }
                    SampleStreams::instance().underruns.fetch_add(1, std::memory_order_relaxed);
                } else if (!lane.sounded && lane.streamWaited > 0.0) {
                    lane.fadeIn = 0.0;
                }
            }
            if (lane.stream != nullptr)
                lane.stream->reading.store(static_cast<std::int64_t>(std::floor(lane.position)), std::memory_order_release);
        }
        lane.sounded = true;
        const auto end = zoneEnd(zone, sample);
        auto l = 0.0;
        auto r = 0.0;
        readFrame(sample, lane.stream, lane.position, l, r);
        if (lane.looping) {
            // The last frames of the loop fade into those just before its
            // start, so the jump back lands where the sound already is.
            const auto length = static_cast<double>(zone.loopEnd - zone.loopStart);
            const auto fade = std::min({ static_cast<double>(zone.loopCrossfade), length * 0.5,
                                         static_cast<double>(zone.loopStart) });
            const auto fadeStart = static_cast<double>(zone.loopEnd) - fade;
            if (fade >= 1.0 && lane.position >= fadeStart) {
                auto earlierLeft = 0.0;
                auto earlierRight = 0.0;
                readFrame(sample, lane.stream, lane.position - length, earlierLeft, earlierRight);
                const auto t = std::clamp((lane.position - fadeStart) / fade, 0.0, 1.0);
                l = l * (1.0 - t) + earlierLeft * t;
                r = r * (1.0 - t) + earlierRight * t;
            }
        }
        if (lane.fadeIn < 1.0) {
            l *= lane.fadeIn;
            r *= lane.fadeIn;
            lane.fadeIn = std::min(1.0, lane.fadeIn + 1.0 / (0.005 * rate_));
        }
        if (zone.endFade > 0 && !lane.looping && !lane.reversed) {
            const auto remaining = static_cast<double>(end) - lane.position;
            if (const auto fade = static_cast<double>(zone.endFade); remaining < fade) {
                const auto gain = 0.5 - 0.5 * std::cos(std::numbers::pi * std::max(0.0, remaining) / fade);
                l *= gain;
                r *= gain;
            }
        }
        if (lane.moves) {
            if (refresh)
                lane.rulesTarget = samplerRulesGain(*map, zone, cues);
            lane.rulesGain += (lane.rulesTarget - lane.rulesGain) * rulesEase_;
            l *= lane.rulesGain;
            r *= lane.rulesGain;
        }
        if (lane.tail) {
            tailLeft += l * lane.gainLeft;
            tailRight += r * lane.gainRight;
        } else {
            left += l * lane.gainLeft;
            right += r * lane.gainRight;
        }
        if (lane.reversed) {
            lane.position -= lane.increment * pitchRatio_;
            if (lane.position < static_cast<double>(std::max<std::int64_t>(0, zone.start)))
                finish(lane);
        } else {
            lane.position += lane.increment * pitchRatio_;
            if (lane.looping && lane.position >= static_cast<double>(zone.loopEnd))
                lane.position -= static_cast<double>(zone.loopEnd - zone.loopStart);
            else if (lane.position >= static_cast<double>(end - 1))
                finish(lane);
        }
        if (!lane.done)
            ++playing;
    }
    const auto width = std::clamp(static_cast<double>(samplerValue(values, SamplerSetting::width)), 0.0, 2.0);
    if (width != 1.0) {
        const auto widen = [width](double& l, double& r) {
            const auto mid = 0.5 * (l + r);
            const auto side = 0.5 * (l - r) * width;
            l = mid + side;
            r = mid - side;
        };
        widen(left, right);
        widen(tailLeft, tailRight);
    }
    active_ = playing > 0;
}

} // namespace cydaw::engine
