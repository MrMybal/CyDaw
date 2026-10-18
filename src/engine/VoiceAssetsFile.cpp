#include <cydaw/engine/VoiceAssetsFile.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <new>
#include <ostream>
#include <stdexcept>

namespace cydaw::engine {
namespace {

static_assert(std::endian::native == std::endian::little, "voice assets are written little-endian");

constexpr std::uint32_t magic = 0x41565943; // "CYVA"
// 2: a model's zones carry their free ratios, their onset and their string.
constexpr std::uint32_t version = 2;
constexpr std::uint32_t samplerPart = 1;
constexpr std::uint32_t modelPart = 2;

class Writer final {
public:
    explicit Writer(std::ostream& out) : out_(out) {}

    template <typename T>
    void value(const T v)
    {
        out_.write(reinterpret_cast<const char*>(&v), sizeof v);
    }
    void text(const std::string& s)
    {
        value(static_cast<std::uint32_t>(s.size()));
        out_.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    template <typename T>
    void list(const std::vector<T>& v)
    {
        value(static_cast<std::uint64_t>(v.size()));
        if (!v.empty())
            out_.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
    }
    [[nodiscard]] bool good() const { return out_.good(); }

private:
    std::ostream& out_;
};

// Reads what Writer wrote; a count larger than what is left of the data
// fails rather than allocating it.
class Reader final {
public:
    explicit Reader(std::istream& in) : in_(in)
    {
        const auto at = in_.tellg();
        if (at >= 0 && in_.seekg(0, std::ios::end)) {
            end_ = in_.tellg();
            in_.seekg(at);
        }
        in_.clear();
    }

    template <typename T>
    T value()
    {
        T v {};
        bytes(&v, sizeof v);
        return v;
    }
    std::string text()
    {
        const auto size = value<std::uint32_t>();
        if (!ok_ || size > (1u << 20) || !fits(size)) {
            ok_ = false;
            return {};
        }
        std::string s(size, '\0');
        bytes(s.data(), size);
        return s;
    }
    template <typename T>
    std::vector<T> list()
    {
        const auto count = value<std::uint64_t>();
        if (!ok_ || count > (std::uint64_t { 1 } << 40) / sizeof(T) || !fits(count * sizeof(T))) {
            ok_ = false;
            return {};
        }
        std::vector<T> v(static_cast<std::size_t>(count));
        bytes(v.data(), static_cast<std::size_t>(count * sizeof(T)));
        return v;
    }
    void fail() noexcept { ok_ = false; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    void bytes(void* to, const std::size_t size)
    {
        if (!ok_ || size == 0)
            return;
        in_.read(static_cast<char*>(to), static_cast<std::streamsize>(size));
        ok_ = in_.gcount() == static_cast<std::streamsize>(size);
    }
    // Whether `size` bytes are still there (a stream whose end is unknown: yes).
    [[nodiscard]] bool fits(const std::uint64_t size)
    {
        if (end_ < 0)
            return true;
        const auto at = in_.tellg();
        return at >= 0 && size <= static_cast<std::uint64_t>(end_ - at);
    }

    std::istream& in_;
    std::streamoff end_ { -1 };
    bool ok_ { true };
};

std::string fileNameOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool wholeAndSound(const SampleData& data)
{
    return data.channels >= 1 && data.channels <= 2 && data.frames > 0 && !data.streamed
           && data.pcm.size() == static_cast<std::size_t>(data.frames) * static_cast<std::size_t>(data.channels);
}

// Each sample is had whole, written and let go before the next: a large
// instrument is never all in memory at once.
bool writeSampler(Writer& w, const SamplerMap& map, const WholeSample& whole, std::string& error)
{
    std::vector<SampleDataRef> order;
    std::map<const SampleData*, int> indexOf;
    for (const auto& zone : map.zones)
        if (zone.sample != nullptr && !indexOf.contains(zone.sample.get())) {
            indexOf[zone.sample.get()] = static_cast<int>(order.size());
            order.push_back(zone.sample);
        }
    w.text(map.name);
    w.value(static_cast<std::uint32_t>(order.size()));
    for (const auto& playing : order) {
        const auto sample = whole ? whole(playing) : playing;
        if (sample == nullptr || !wholeAndSound(*sample)) {
            error = "a sample cannot be read: " + fileNameOf(playing->path);
            return false;
        }
        w.text(fileNameOf(playing->path));
        w.value(static_cast<std::int32_t>(sample->channels));
        w.value(static_cast<std::int64_t>(sample->frames));
        w.value(sample->sampleRate);
        w.value(sample->scale);
        w.value(static_cast<std::int64_t>(sample->fileLoopStart));
        w.value(static_cast<std::int64_t>(sample->fileLoopEnd));
        w.list(sample->pcm);
        if (!w.good()) {
            error = "the voice data cannot be written";
            return false;
        }
    }
    w.value(static_cast<std::uint32_t>(map.zones.size()));
    for (const auto& zone : map.zones) {
        const auto found = zone.sample != nullptr ? indexOf.find(zone.sample.get()) : indexOf.end();
        w.value(static_cast<std::int32_t>(found != indexOf.end() ? found->second : -1));
        for (const auto v : { zone.rootNote, zone.lowKey, zone.highKey, zone.lowVelocity, zone.highVelocity })
            w.value(static_cast<std::int32_t>(v));
        for (const auto v : { zone.tuneCents, zone.gainDb, zone.pan })
            w.value(v);
        for (const auto v : { zone.start, zone.end, zone.loopStart, zone.loopEnd })
            w.value(static_cast<std::int64_t>(v));
        for (const auto v : { zone.loopCrossfade, zone.endFade, zone.sequenceLength, zone.sequencePosition, zone.group,
                              zone.offBy, zone.keyswitch })
            w.value(static_cast<std::int32_t>(v));
        w.value(static_cast<std::uint8_t>(zone.trigger));
        w.value(zone.releaseDecayDb);
        w.value(static_cast<std::uint32_t>(zone.rules.size()));
        for (const auto& rule : zone.rules) {
            w.value(static_cast<std::uint8_t>(rule.kind));
            w.value(rule.controller);
            w.value(rule.curve);
            w.value(rule.a);
            w.value(rule.b);
        }
    }
    for (const auto v : map.controllerStart)
        w.value(v);
    w.value(static_cast<std::uint32_t>(map.controls.size()));
    for (const auto& control : map.controls) {
        w.value(static_cast<std::int32_t>(control.controller));
        w.text(control.label);
    }
    w.value(static_cast<std::uint32_t>(map.curves.size()));
    for (const auto& curve : map.curves)
        for (const auto v : curve)
            w.value(v);
    for (const auto held : map.keyswitchKeys)
        w.value(static_cast<std::uint8_t>(held ? 1 : 0));
    w.value(static_cast<std::int32_t>(map.keyswitchStart));
    w.value(static_cast<std::uint32_t>(map.keyswitchLabels.size()));
    for (const auto& [key, label] : map.keyswitchLabels) {
        w.value(static_cast<std::int32_t>(key));
        w.text(label);
    }
    w.value(static_cast<std::uint8_t>(map.hasReleaseZones ? 1 : 0));
    return true;
}

void writeModel(Writer& w, const SoundModel& model)
{
    w.text(model.name);
    w.value(static_cast<std::uint32_t>(model.zones.size()));
    for (const auto& zone : model.zones) {
        w.value(static_cast<std::int32_t>(zone.rootNote));
        for (const auto v : { zone.f0, zone.velocity, zone.gain, zone.frameSeconds })
            w.value(v);
        w.value(static_cast<std::int32_t>(zone.frames));
        w.value(zone.inharmonicity);
        w.list(zone.keyFrames);
        w.value(static_cast<std::int32_t>(zone.harmonicCount));
        w.list(zone.harmonics);
        w.value(static_cast<std::int32_t>(zone.bandCount));
        for (const auto* table : { &zone.noiseEdges, &zone.noise, &zone.pitchCents })
            w.list(*table);
        w.list(zone.stereoFrames);
        for (const auto* table : { &zone.partialLeft, &zone.partialRight, &zone.phaseCos, &zone.phaseSin, &zone.noiseLeft,
                                   &zone.noiseRight, &zone.noiseCoherence, &zone.headLeft, &zone.headRight })
            w.list(*table);
        w.value(zone.headRate);
        w.value(static_cast<std::int32_t>(zone.headFade));
        w.list(zone.headPhase);
        w.list(zone.attack);
        w.value(zone.attackRate);
        w.value(zone.attackGain);
        for (const auto v : { zone.loopStart, zone.loopEnd, zone.release })
            w.value(static_cast<std::int32_t>(v));
        // Version 2: partials that are no harmonics, where the note starts,
        // a plucked string in place of the partials.
        w.list(zone.ratios);
        w.value(zone.onset);
        const auto& string = zone.string;
        w.value(static_cast<std::uint8_t>(string.present() ? 1 : 0));
        if (string.present()) {
            for (const auto v : { string.gain, string.pole, string.zero, string.dispersion })
                w.value(v);
            for (const auto v : { string.order, string.stages, string.start })
                w.value(static_cast<std::int32_t>(v));
            std::vector<float> dips;
            for (const auto& dip : string.dips)
                dips.insert(dips.end(), { dip.hz, dip.q, dip.db });
            w.list(dips);
            w.list(string.excitationDb);
            w.list(string.bodyEdges);
            w.list(string.bodyDb);
        }
    }
}

bool finite(const float v) noexcept { return std::isfinite(v); }

SamplerMapRef readSampler(Reader& r, std::string& error)
{
    auto map = std::make_shared<SamplerMap>();
    map->name = r.text();
    const auto sampleCount = r.value<std::uint32_t>();
    if (!r.ok() || sampleCount > 1'000'000) {
        error = "the sampler's samples are unreadable";
        return {};
    }
    std::vector<SampleDataRef> samples;
    samples.reserve(sampleCount);
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        auto data = std::make_shared<SampleData>();
        data->path = r.text();
        data->channels = r.value<std::int32_t>();
        data->frames = r.value<std::int64_t>();
        data->sampleRate = r.value<double>();
        data->scale = r.value<float>();
        data->fileLoopStart = r.value<std::int64_t>();
        data->fileLoopEnd = r.value<std::int64_t>();
        data->pcm = r.list<std::int16_t>();
        if (!r.ok() || !wholeAndSound(*data) || !std::isfinite(data->sampleRate) || data->sampleRate <= 0.0
            || !finite(data->scale)) {
            error = "sample " + std::to_string(index + 1) + " is unreadable";
            return {};
        }
        if (data->fileLoopStart < -1 || data->fileLoopStart >= data->frames || data->fileLoopEnd < -1
            || data->fileLoopEnd > data->frames)
            data->fileLoopStart = data->fileLoopEnd = -1;
        data->heldFrames = data->frames;
        data->ready.store(true, std::memory_order_release);
        samples.push_back(std::move(data));
    }
    const auto zoneCount = r.value<std::uint32_t>();
    if (!r.ok() || zoneCount > 1'000'000) {
        error = "the sampler's zones are unreadable";
        return {};
    }
    map->zones.reserve(zoneCount);
    for (std::uint32_t index = 0; index < zoneCount; ++index) {
        SamplerZone zone;
        const auto sample = r.value<std::int32_t>();
        const auto key = [&r] { return std::clamp(static_cast<int>(r.value<std::int32_t>()), 0, 127); };
        zone.rootNote = key();
        zone.lowKey = key();
        zone.highKey = key();
        zone.lowVelocity = key();
        zone.highVelocity = key();
        zone.tuneCents = r.value<float>();
        zone.gainDb = r.value<float>();
        zone.pan = std::clamp(r.value<float>(), -1.0f, 1.0f);
        zone.start = r.value<std::int64_t>();
        zone.end = r.value<std::int64_t>();
        zone.loopStart = r.value<std::int64_t>();
        zone.loopEnd = r.value<std::int64_t>();
        zone.loopCrossfade = std::max(0, static_cast<int>(r.value<std::int32_t>()));
        zone.endFade = std::max(0, static_cast<int>(r.value<std::int32_t>()));
        zone.sequenceLength = std::max(1, static_cast<int>(r.value<std::int32_t>()));
        zone.sequencePosition = std::clamp(static_cast<int>(r.value<std::int32_t>()), 1, zone.sequenceLength);
        zone.group = r.value<std::int32_t>();
        zone.offBy = r.value<std::int32_t>();
        zone.keyswitch = std::clamp(static_cast<int>(r.value<std::int32_t>()), -1, 127);
        const auto trigger = r.value<std::uint8_t>();
        zone.trigger = static_cast<SamplerTrigger>(std::min<int>(trigger, static_cast<int>(SamplerTrigger::legato)));
        zone.releaseDecayDb = r.value<float>();
        const auto ruleCount = r.value<std::uint32_t>();
        if (!r.ok() || ruleCount > 4'096 || sample < -1 || sample >= static_cast<std::int32_t>(samples.size())
            || !finite(zone.tuneCents) || !finite(zone.gainDb) || !finite(zone.pan) || !finite(zone.releaseDecayDb)) {
            error = "zone " + std::to_string(index + 1) + " is unreadable";
            return {};
        }
        for (std::uint32_t rule = 0; rule < ruleCount; ++rule) {
            SamplerRule read;
            const auto kind = r.value<std::uint8_t>();
            read.kind = static_cast<SamplerRuleKind>(std::min<int>(kind, static_cast<int>(SamplerRuleKind::offset)));
            read.controller = static_cast<std::uint8_t>(std::min<int>(r.value<std::uint8_t>(), 127));
            read.curve = r.value<std::int16_t>();
            read.a = r.value<float>();
            read.b = r.value<float>();
            if (!finite(read.a) || !finite(read.b))
                r.fail();
            zone.rules.push_back(read);
        }
        if (sample >= 0) {
            zone.sample = samples[static_cast<std::size_t>(sample)];
            const auto frames = zone.sample->frames;
            zone.start = std::clamp<std::int64_t>(zone.start, 0, frames - 1);
            if (zone.end >= 0)
                zone.end = std::clamp<std::int64_t>(zone.end, zone.start, frames);
            if (zone.loopStart < 0 || zone.loopEnd > frames || zone.loopEnd <= zone.loopStart + 1)
                zone.loopStart = zone.loopEnd = -1;
        }
        map->zones.push_back(std::move(zone));
    }
    for (auto& v : map->controllerStart)
        v = std::clamp(r.value<float>(), 0.0f, 127.0f);
    const auto controlCount = r.value<std::uint32_t>();
    if (!r.ok() || controlCount > 128) {
        error = "the sampler's controls are unreadable";
        return {};
    }
    for (std::uint32_t index = 0; index < controlCount; ++index) {
        SamplerMap::Control control;
        control.controller = std::clamp(static_cast<int>(r.value<std::int32_t>()), 0, 127);
        control.label = r.text();
        map->controls.push_back(std::move(control));
    }
    const auto curveCount = r.value<std::uint32_t>();
    if (!r.ok() || curveCount > 4'096) {
        error = "the sampler's curves are unreadable";
        return {};
    }
    map->curves.resize(curveCount);
    for (auto& curve : map->curves)
        for (auto& v : curve) {
            v = r.value<float>();
            if (!finite(v))
                r.fail();
        }
    for (auto& held : map->keyswitchKeys)
        held = r.value<std::uint8_t>() != 0;
    map->keyswitchStart = std::clamp(static_cast<int>(r.value<std::int32_t>()), -1, 127);
    const auto labelCount = r.value<std::uint32_t>();
    if (!r.ok() || labelCount > 128) {
        error = "the sampler's keyswitches are unreadable";
        return {};
    }
    for (std::uint32_t index = 0; index < labelCount; ++index) {
        const auto key = std::clamp(static_cast<int>(r.value<std::int32_t>()), 0, 127);
        map->keyswitchLabels.emplace_back(key, r.text());
    }
    map->hasReleaseZones = r.value<std::uint8_t>() != 0;
    // A rule's curve that the map does not have: straight.
    for (auto& zone : map->zones)
        for (auto& rule : zone.rules)
            if (rule.curve < -1 || rule.curve >= static_cast<int>(map->curves.size()))
                rule.curve = -1;
    if (!r.ok()) {
        error = "the sampler is cut short";
        return {};
    }
    return map;
}

// The same checks as a .cysound read by the application: the tables a voice
// reads match the frames they are for.
bool modelZoneHolds(SoundModelZone& zone)
{
    if (zone.frames <= 0 || zone.frames > 1'000'000 || zone.harmonicCount < 0 || zone.harmonicCount > 512)
        return false;
    const auto& keys = zone.keyFrames;
    if (!keys.empty()
        && (keys.front() != 0 || keys.back() != zone.frames - 1
            || std::adjacent_find(keys.begin(), keys.end(), std::greater_equal<int>()) != keys.end()))
        return false;
    const auto rows = static_cast<std::size_t>(zone.rows());
    zone.bandCount = std::max(0, static_cast<int>(zone.noiseEdges.size()) - 1);
    const auto harmonics = static_cast<std::size_t>(zone.harmonicCount), bands = static_cast<std::size_t>(zone.bandCount);
    if (zone.harmonics.size() != rows * harmonics || zone.noise.size() != rows * bands || zone.pitchCents.size() != rows)
        return false;
    const auto stereoRows = zone.stereoFrames.size();
    const auto& at = zone.stereoFrames;
    const auto stereoHolds = std::adjacent_find(at.begin(), at.end(), std::greater_equal<int>()) == at.end()
                             && zone.partialLeft.size() == stereoRows * harmonics
                             && zone.partialRight.size() == stereoRows * harmonics
                             && zone.noiseLeft.size() == stereoRows * bands && zone.noiseRight.size() == stereoRows * bands
                             && zone.phaseCos.size() == harmonics && zone.phaseSin.size() == harmonics
                             && zone.noiseCoherence.size() == bands;
    if (stereoRows > 0 && !stereoHolds)
        return false;
    if (stereoRows == 0) {
        zone.partialLeft.clear();
        zone.partialRight.clear();
        zone.noiseLeft.clear();
        zone.noiseRight.clear();
    }
    if (!zone.headRight.empty() && zone.headRight.size() != zone.headLeft.size())
        return false;
    if (!zone.headLeft.empty())
        zone.headFade = std::clamp(zone.headFade, 1, std::max(1, zone.headLength() / 2));
    if (!(zone.headRate > 0.0f) || !(zone.attackRate > 0.0f) || !std::isfinite(zone.f0) || !(zone.frameSeconds > 0.0f))
        return false;
    zone.rootNote = std::clamp(zone.rootNote, 0, 127);
    zone.loopStart = std::clamp(zone.loopStart, -1, zone.frames - 1);
    zone.loopEnd = std::clamp(zone.loopEnd, -1, zone.frames - 1);
    if (zone.loopEnd <= zone.loopStart)
        zone.loopStart = zone.loopEnd = -1;
    zone.release = std::clamp(zone.release, -1, zone.frames - 1);
    if (std::any_of(zone.ratios.begin(), zone.ratios.end(), [](const float ratio) { return !(ratio > 0.0f && ratio < 1.0e4f); }))
        zone.ratios.clear();
    zone.onset = std::isfinite(zone.onset) ? std::clamp(zone.onset, 0.0f, 10.0f) : 0.0f;
    // The string's checks are the .cysound reader's (SoundModelFile.cpp).
    auto& string = zone.string;
    if (string.present()) {
        const auto finiteAll = [](const std::vector<float>& values) { return std::all_of(values.begin(), values.end(), finite); };
        if (!finite(string.gain) || !finite(string.pole) || !finite(string.zero) || !finite(string.dispersion)
            || !finiteAll(string.excitationDb) || !finiteAll(string.bodyEdges) || !finiteAll(string.bodyDb)
            || string.bodyEdges.size() != string.bodyDb.size() + 1 || string.bodyDb.empty())
            return false;
        string.gain = std::clamp(string.gain, 0.0f, 0.999999f);
        string.pole = std::clamp(string.pole, 0.0f, 0.99f);
        string.zero = std::clamp(string.zero, -0.99f, string.pole);
        string.order = string.order >= 2 ? 2 : 1;
        string.dispersion = std::clamp(string.dispersion, -0.95f, 0.0f);
        string.stages = std::clamp(string.stages, 0, PluckedString::maximumStages);
        string.start = std::clamp(string.start, 0, std::max(0, zone.frames - 1));
        if (static_cast<int>(string.dips.size()) > PluckedString::maximumDips)
            string.dips.resize(static_cast<std::size_t>(PluckedString::maximumDips));
        for (auto& dip : string.dips) {
            if (!finite(dip.hz) || !finite(dip.q) || !finite(dip.db))
                return false;
            dip.db = std::clamp(dip.db, -6.0f, 0.0f);
        }
    }
    return true;
}

SoundModelRef readModel(Reader& r, const std::uint32_t readVersion, std::string& error)
{
    auto model = std::make_shared<SoundModel>();
    model->name = r.text();
    const auto zoneCount = r.value<std::uint32_t>();
    if (!r.ok() || zoneCount == 0 || zoneCount > 4'096) {
        error = "the sound model's zones are unreadable";
        return {};
    }
    for (std::uint32_t index = 0; index < zoneCount; ++index) {
        SoundModelZone zone;
        zone.rootNote = r.value<std::int32_t>();
        zone.f0 = r.value<float>();
        zone.velocity = std::clamp(r.value<float>(), 0.0f, 1.0f);
        zone.gain = r.value<float>();
        zone.frameSeconds = r.value<float>();
        zone.frames = r.value<std::int32_t>();
        zone.inharmonicity = r.value<float>();
        zone.keyFrames = r.list<int>();
        zone.harmonicCount = r.value<std::int32_t>();
        zone.harmonics = r.list<float>();
        zone.bandCount = r.value<std::int32_t>();
        zone.noiseEdges = r.list<float>();
        zone.noise = r.list<float>();
        zone.pitchCents = r.list<float>();
        zone.stereoFrames = r.list<int>();
        for (auto* table : { &zone.partialLeft, &zone.partialRight, &zone.phaseCos, &zone.phaseSin, &zone.noiseLeft,
                             &zone.noiseRight, &zone.noiseCoherence, &zone.headLeft, &zone.headRight })
            *table = r.list<float>();
        zone.headRate = r.value<float>();
        zone.headFade = r.value<std::int32_t>();
        zone.headPhase = r.list<float>();
        zone.attack = r.list<float>();
        zone.attackRate = r.value<float>();
        zone.attackGain = r.value<float>();
        zone.loopStart = r.value<std::int32_t>();
        zone.loopEnd = r.value<std::int32_t>();
        zone.release = r.value<std::int32_t>();
        if (readVersion >= 2) {
            zone.ratios = r.list<float>();
            zone.onset = r.value<float>();
            if (r.value<std::uint8_t>() != 0) {
                auto& string = zone.string;
                string.gain = r.value<float>();
                string.pole = r.value<float>();
                string.zero = r.value<float>();
                string.dispersion = r.value<float>();
                string.order = r.value<std::int32_t>();
                string.stages = r.value<std::int32_t>();
                string.start = r.value<std::int32_t>();
                const auto dips = r.list<float>();
                for (std::size_t at = 0; at + 2 < dips.size(); at += 3)
                    string.dips.push_back({ dips[at], dips[at + 1], dips[at + 2] });
                string.excitationDb = r.list<float>();
                string.bodyEdges = r.list<float>();
                string.bodyDb = r.list<float>();
                if (!string.present()) {
                    error = "sound model zone " + std::to_string(index + 1) + "'s string is unreadable";
                    return {};
                }
            }
        }
        if (!r.ok() || !modelZoneHolds(zone)) {
            error = "sound model zone " + std::to_string(index + 1) + " is unreadable";
            return {};
        }
        model->zones.push_back(std::move(zone));
    }
    return model;
}

} // namespace

bool writeVoiceAssets(std::ostream& out, const SamplerMap* sampler, const WholeSample& whole, const SoundModel* soundModel,
                      std::string& error)
{
    Writer w(out);
    w.value(magic);
    w.value(version);
    w.value((sampler != nullptr ? samplerPart : 0u) | (soundModel != nullptr ? modelPart : 0u));
    // Every sample once, whole, in the order the zones meet them.
    if (sampler != nullptr && !writeSampler(w, *sampler, whole, error))
        return false;
    if (soundModel != nullptr)
        writeModel(w, *soundModel);
    if (!w.good()) {
        error = "the voice data cannot be written";
        return false;
    }
    return true;
}

VoiceAssets readVoiceAssets(std::istream& in, std::string& error)
{
    try {
        Reader r(in);
        const auto readMagic = r.value<std::uint32_t>();
        const auto readVersion = r.value<std::uint32_t>();
        const auto parts = r.value<std::uint32_t>();
        if (!r.ok() || readMagic != magic) {
            error = "not CyDAW voice data";
            return {};
        }
        if (readVersion < 1 || readVersion > version) {
            error = "voice data of another version (" + std::to_string(readVersion) + ")";
            return {};
        }
        VoiceAssets assets;
        if ((parts & samplerPart) != 0 && (assets.sampler = readSampler(r, error)) == nullptr)
            return {};
        if ((parts & modelPart) != 0 && (assets.soundModel = readModel(r, readVersion, error)) == nullptr)
            return {};
        return assets;
    } catch (const std::bad_alloc&) {
        error = "the voice data is too large to read";
    } catch (const std::length_error&) {
        error = "the voice data is too large to read";
    }
    return {};
}

} // namespace cydaw::engine
