#include "SoundModelFile.h"

#include "SamplerMapFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace cydaw::app {

namespace {

constexpr int silentDb10 = -1'200;
constexpr int noiseFloorDb10 = -2'000;

// `count` rows of `columns` dB x 10 values: a list of lists, base64 of
// little-endian int16 rows (a compact zone's), or "z:" + base64 of the zlib
// stream of those rows each written as its difference from the one before
// (modulo 2^16).
bool readRows(const juce::var& rows, const int count, const int columns, std::vector<float>& db10)
{
    // No larger than a model can be (a length field is not trusted further).
    if (count < 0 || columns < 0 || static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(columns) > (1ull << 26))
        return false;
    db10.assign(static_cast<std::size_t>(count) * static_cast<std::size_t>(columns), 0.0f);
    if (columns == 0)
        return true;
    if (rows.isString()) {
        const auto text = rows.toString();
        const auto deltas = text.startsWith("z:");
        juce::MemoryOutputStream bytes;
        if (!juce::Base64::convertFromBase64(bytes, deltas ? text.substring(2) : text))
            return false;
        juce::MemoryOutputStream raw;
        if (deltas) {
            juce::MemoryInputStream input(bytes.getData(), bytes.getDataSize(), false);
            juce::GZIPDecompressorInputStream unzip(&input, false, juce::GZIPDecompressorInputStream::zlibFormat);
            // Unpacked no further than the rows can take (a small stream of
            // zeros may unpack to gigabytes).
            (void) raw.writeFromInputStream(unzip, static_cast<juce::int64>(db10.size() * sizeof(std::int16_t)) + 1);
        }
        const auto& source = deltas ? raw : bytes;
        if (source.getDataSize() != db10.size() * sizeof(std::int16_t))
            return false;
        const auto* data = static_cast<const unsigned char*>(source.getData());
        std::vector<std::uint16_t> running(static_cast<std::size_t>(columns), 0);
        for (std::size_t index = 0; index < db10.size(); ++index) {
            auto value = static_cast<std::uint16_t>(data[2 * index] | (data[2 * index + 1] << 8));
            if (deltas) {
                auto& sum = running[index % static_cast<std::size_t>(columns)];
                sum = static_cast<std::uint16_t>(sum + value);
                value = sum;
            }
            db10[index] = static_cast<float>(static_cast<std::int16_t>(value));
        }
        return true;
    }
    const auto* list = rows.getArray();
    if (list == nullptr || list->size() != count)
        return false;
    for (int frame = 0; frame < count; ++frame) {
        const auto* row = (*list)[frame].getArray();
        if (row == nullptr || row->size() != columns)
            return false;
        for (int column = 0; column < columns; ++column)
            db10[static_cast<std::size_t>(frame) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)]
                = static_cast<float>(static_cast<int>((*row)[column]));
    }
    return true;
}

// A rows x columns table of dB x 10 integers, to linear values: amplitudes
// (dB / 20) or powers (dB / 10). The rows stay as they are (a compact zone's
// at its key frames): the voice reads between them.
bool readTable(const juce::var& rows, const int count, const int columns, const bool power, std::vector<float>& out)
{
    if (!readRows(rows, count, columns, out))
        return false;
    for (auto& cell : out) {
        if (power)
            cell = cell <= static_cast<float>(noiseFloorDb10) ? 0.0f : static_cast<float>(std::pow(10.0, cell / 100.0));
        else
            cell = cell <= static_cast<float>(silentDb10) ? 0.0f : static_cast<float>(std::pow(10.0, cell / 200.0));
    }
    return true;
}

std::vector<float> readFloats(const juce::var& value)
{
    std::vector<float> out;
    if (const auto* list = value.getArray()) {
        out.reserve(static_cast<std::size_t>(list->size()));
        for (const auto& item : *list)
            out.push_back(static_cast<float>(static_cast<double>(item)));
    }
    return out;
}

// The stereo rows' frames: 0, step, 2 step... and the last.
std::vector<int> stereoFramesOf(const int frames, const int step)
{
    std::vector<int> out;
    for (auto frame = 0; frame < frames; frame += step)
        out.push_back(frame);
    if (out.empty() || out.back() != frames - 1)
        out.push_back(frames - 1);
    return out;
}

// The two channels of a zone, from its "stereo" object; false when it does
// not match the zone.
bool readStereo(const juce::var& stereo, cydaw::engine::SoundModelZone& zone)
{
    const auto step = static_cast<int>(stereo.getProperty("step", 0));
    if (step <= 0)
        return false;
    const auto frames = stereoFramesOf(zone.frames, step);
    const auto rows = static_cast<int>(frames.size());
    std::vector<float> balance;
    std::vector<float> noiseBalance;
    if (!readRows(stereo.getProperty("balance", {}), rows, zone.harmonicCount, balance)
        || !readRows(stereo.getProperty("noiseBalance", {}), rows, zone.bandCount, noiseBalance))
        return false;
    const auto phase = readFloats(stereo.getProperty("phase", {}));
    auto coherence = readFloats(stereo.getProperty("noiseCoherence", {}));
    if (static_cast<int>(phase.size()) != zone.harmonicCount || static_cast<int>(coherence.size()) != zone.bandCount)
        return false;
    // A partial of RMS a and balance b (r = 10^(b/20)): a sqrt(2 / (1 + r^2))
    // on the left, r times that on the right; a band of mean power p and
    // balance n (q = 10^(n/10)): 2p / (1 + q) and q times that.
    zone.partialLeft.resize(balance.size());
    zone.partialRight.resize(balance.size());
    for (std::size_t index = 0; index < balance.size(); ++index) {
        const auto r = std::pow(10.0, balance[index] / 200.0);
        const auto left = std::sqrt(2.0 / (1.0 + r * r));
        zone.partialLeft[index] = static_cast<float>(left);
        zone.partialRight[index] = static_cast<float>(left * r);
    }
    zone.noiseLeft.resize(noiseBalance.size());
    zone.noiseRight.resize(noiseBalance.size());
    for (std::size_t index = 0; index < noiseBalance.size(); ++index) {
        const auto q = std::pow(10.0, noiseBalance[index] / 100.0);
        const auto left = 2.0 / (1.0 + q);
        zone.noiseLeft[index] = static_cast<float>(left);
        zone.noiseRight[index] = static_cast<float>(left * q);
    }
    zone.phaseCos.resize(phase.size());
    zone.phaseSin.resize(phase.size());
    for (std::size_t index = 0; index < phase.size(); ++index) {
        zone.phaseCos[index] = std::cos(phase[index]);
        zone.phaseSin[index] = std::sin(phase[index]);
    }
    for (auto& value : coherence)
        value = std::clamp(value, 0.0f, 1.0f);
    zone.noiseCoherence = std::move(coherence);
    zone.stereoFrames = frames;
    return true;
}

// The recording's first moments, from a zone's "head" object.
bool readHead(const juce::var& head, cydaw::engine::SoundModelZone& zone)
{
    const auto channels = juce::jlimit(1, 2, static_cast<int>(head.getProperty("channels", 1)));
    const auto length = static_cast<int>(head.getProperty("length", 0));
    const auto gain = static_cast<float>(static_cast<double>(head.getProperty("gain", 0.0)));
    juce::MemoryOutputStream bytes;
    if (length <= 0 || length > 10 * 192'000
        || !juce::Base64::convertFromBase64(bytes, head.getProperty("int16", {}).toString())
        || bytes.getDataSize() != static_cast<std::size_t>(length) * static_cast<std::size_t>(channels) * sizeof(std::int16_t))
        return false;
    const auto* data = static_cast<const unsigned char*>(bytes.getData());
    zone.headLeft.resize(static_cast<std::size_t>(length));
    if (channels == 2)
        zone.headRight.resize(static_cast<std::size_t>(length));
    for (auto frame = 0; frame < length; ++frame)
        for (auto channel = 0; channel < channels; ++channel) {
            const auto at = static_cast<std::size_t>(frame * channels + channel);
            const auto value = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[2 * at] | (data[2 * at + 1] << 8)));
            (channel == 0 ? zone.headLeft : zone.headRight)[static_cast<std::size_t>(frame)] = gain * static_cast<float>(value);
        }
    zone.headRate = static_cast<float>(static_cast<double>(head.getProperty("rate", 48'000)));
    zone.headFade = juce::jlimit(1, length / 2, static_cast<int>(head.getProperty("fade", 0)));
    zone.headPhase = readFloats(head.getProperty("phase", {}));
    return zone.headRate > 0.0f;
}

} // namespace

cydaw::engine::SoundModelRef soundModelFromVar(const juce::var& value, juce::String& error)
{
    if (value.getProperty("format", {}).toString() != "cydaw-sound-model") {
        error = "not a cydaw-sound-model";
        return {};
    }
    if (static_cast<int>(value.getProperty("version", 0)) > 1) {
        error = "a newer sound model version (" + value.getProperty("version", 0).toString() + ")";
        return {};
    }
    auto model = std::make_shared<cydaw::engine::SoundModel>();
    model->name = value.getProperty("name", {}).toString().toStdString();
    const auto* zones = value.getProperty("zones", {}).getArray();
    if (zones == nullptr || zones->isEmpty()) {
        error = "the sound model has no zone";
        return {};
    }
    for (const auto& item : *zones) {
        cydaw::engine::SoundModelZone zone;
        zone.rootNote = juce::jlimit(0, 127, static_cast<int>(item.getProperty("rootNote", 60)));
        zone.f0 = static_cast<float>(static_cast<double>(item.getProperty("f0", 261.63)));
        zone.velocity = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(item.getProperty("velocity", 0.8))));
        zone.gain = static_cast<float>(static_cast<double>(item.getProperty("gain", 1.0)));
        zone.frameSeconds = juce::jlimit(0.001f, 1.0f, static_cast<float>(static_cast<double>(item.getProperty("frameSeconds", 0.01))));
        zone.frames = juce::jlimit(0, 1'000'000, static_cast<int>(item.getProperty("frames", 0)));
        zone.inharmonicity = juce::jlimit(0.0f, 0.05f, static_cast<float>(static_cast<double>(item.getProperty("inharmonicity", 0.0))));
        // Partials that are no harmonics: their ratios to f0 (positive).
        zone.ratios = readFloats(item.getProperty("ratios", {}));
        zone.onset = juce::jlimit(0.0f, 10.0f, static_cast<float>(static_cast<double>(item.getProperty("onset", 0.0))));
        // A plucked string in place of its partials.
        if (const auto string = item.getProperty("string", {}); string.isObject()) {
            auto& model = zone.string;
            model.gain = juce::jlimit(0.0f, 0.999999f, static_cast<float>(static_cast<double>(string.getProperty("gain", 0.99))));
            model.pole = juce::jlimit(0.0f, 0.99f, static_cast<float>(static_cast<double>(string.getProperty("pole", 0.0))));
            model.zero = juce::jlimit(-0.99f, model.pole, static_cast<float>(static_cast<double>(string.getProperty("zero", 0.0))));
            model.order = static_cast<int>(string.getProperty("order", 1)) >= 2 ? 2 : 1;
            model.dispersion = juce::jlimit(-0.95f, 0.0f, static_cast<float>(static_cast<double>(string.getProperty("dispersion", 0.0))));
            model.stages = juce::jlimit(0, cydaw::engine::PluckedString::maximumStages, static_cast<int>(string.getProperty("stages", 0)));
            model.start = juce::jlimit(0, std::max(0, zone.frames - 1), static_cast<int>(string.getProperty("start", 0)));
            if (const auto* dips = string.getProperty("dips", {}).getArray())
                for (const auto& dip : *dips)
                    if (static_cast<int>(model.dips.size()) < cydaw::engine::PluckedString::maximumDips)
                        model.dips.push_back({ static_cast<float>(static_cast<double>(dip.getProperty("hz", 1000.0))),
                                               static_cast<float>(static_cast<double>(dip.getProperty("q", 1.0))),
                                               juce::jlimit(-6.0f, 0.0f, static_cast<float>(static_cast<double>(dip.getProperty("db", 0.0)))) });
            for (const auto value : readFloats(string.getProperty("excitation", {})))
                model.excitationDb.push_back(value / 10.0f);
            model.bodyEdges = readFloats(string.getProperty("bodyEdges", {}));
            for (const auto value : readFloats(string.getProperty("body", {})))
                model.bodyDb.push_back(value / 10.0f);
            if (model.bodyEdges.size() != model.bodyDb.size() + 1 || model.bodyDb.empty())
                model.excitationDb.clear();
        }
        if (std::any_of(zone.ratios.begin(), zone.ratios.end(), [](const float ratio) { return !(ratio > 0.0f && ratio < 1.0e4f); }))
            zone.ratios.clear();
        // A compact zone's key frames: ascending, from 0 to the last frame.
        if (const auto* times = item.getProperty("frameTimes", {}).getArray()) {
            for (const auto& time : *times)
                zone.keyFrames.push_back(static_cast<int>(time));
            const auto& keys = zone.keyFrames;
            const auto ascending = std::adjacent_find(keys.begin(), keys.end(), std::greater_equal<int>()) == keys.end();
            if (keys.empty() || keys.front() != 0 || keys.back() != zone.frames - 1 || !ascending) {
                error = "a zone's key frames are not in order";
                return {};
            }
        }
        const auto rows = zone.rows();
        const auto harmonics = item.getProperty("harmonics", {});
        zone.harmonicCount = juce::jlimit(0, 512, static_cast<int>(harmonics.getProperty("count", 0)));
        if (zone.frames <= 0 || !readTable(harmonics.getProperty("db10", {}), rows, zone.harmonicCount, false, zone.harmonics)) {
            error = "a zone's partials do not match its frame count";
            return {};
        }
        const auto noise = item.getProperty("noise", {});
        zone.noiseEdges = readFloats(noise.getProperty("edges", {}));
        zone.bandCount = std::max(0, static_cast<int>(zone.noiseEdges.size()) - 1);
        if (!readTable(noise.getProperty("db10", {}), rows, zone.bandCount, true, zone.noise)) {
            error = "a zone's noise does not match its frame count";
            return {};
        }
        zone.pitchCents = readFloats(item.getProperty("pitchCents", {}));
        if (static_cast<int>(zone.pitchCents.size()) != rows)
            zone.pitchCents.assign(static_cast<std::size_t>(rows), 0.0f);
        if (const auto attack = item.getProperty("attack", {}); attack.isObject()) {
            juce::MemoryOutputStream bytes;
            if (juce::Base64::convertFromBase64(bytes, attack.getProperty("float32", {}).toString())) {
                const auto count = bytes.getDataSize() / sizeof(float);
                zone.attack.resize(count);
                std::memcpy(zone.attack.data(), bytes.getData(), count * sizeof(float));
            }
            zone.attackRate = static_cast<float>(static_cast<double>(attack.getProperty("rate", 48'000)));
            zone.attackGain = static_cast<float>(static_cast<double>(attack.getProperty("gain", 1.0)));
        }
        if (const auto loop = item.getProperty("loop", {}); loop.isObject()) {
            zone.loopStart = juce::jlimit(0, zone.frames - 1, static_cast<int>(loop.getProperty("start", -1)));
            zone.loopEnd = juce::jlimit(0, zone.frames - 1, static_cast<int>(loop.getProperty("end", -1)));
            if (zone.loopEnd <= zone.loopStart)
                zone.loopStart = zone.loopEnd = -1;
        }
        if (const auto release = item.getProperty("release", {}); !release.isVoid() && !release.isUndefined())
            zone.release = juce::jlimit(0, zone.frames - 1, static_cast<int>(release));
        // The two channels, and the recording's first moments: a zone that
        // cannot read them plays without (in mono, with no head).
        if (const auto stereo = item.getProperty("stereo", {}); stereo.isObject() && !readStereo(stereo, zone)) {
            zone.stereoFrames.clear();
            zone.partialLeft.clear();
            zone.partialRight.clear();
            zone.noiseLeft.clear();
            zone.noiseRight.clear();
        }
        if (const auto head = item.getProperty("head", {}); head.isObject() && !readHead(head, zone)) {
            zone.headLeft.clear();
            zone.headRight.clear();
        }
        model->zones.push_back(std::move(zone));
    }
    return model;
}

namespace {

// The factory models read, by their stored name ("factory:…"), while
// something holds them (a card, a document being read).
struct FactoryModel {
    juce::var json;
    juce::Time modified;
    juce::int64 size {};
};
std::mutex& factoryModelsLock()
{
    static std::mutex lock;
    return lock;
}
std::map<juce::String, FactoryModel>& factoryModels()
{
    static std::map<juce::String, FactoryModel> models;
    return models;
}
const juce::Identifier soundModelKey("soundModel");
const juce::Identifier soundModelFileKey("soundModelFile");

juce::var shallowCopy(const juce::DynamicObject& object)
{
    auto copy = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : object.getProperties())
        copy->setProperty(property.name, property.value);
    return juce::var(copy.get());
}

// The value with its factory models named; `changed` when anything was.
juce::var referenced(const juce::var& value, bool& changed)
{
    if (const auto* array = value.getArray()) {
        std::optional<juce::Array<juce::var>> copy;
        for (int i = 0; i < array->size(); ++i) {
            const auto& item = array->getReference(i);
            if (!item.isObject() && !item.isArray())
                continue;
            auto itemChanged = false;
            auto next = referenced(item, itemChanged);
            if (!itemChanged)
                continue;
            if (!copy)
                copy = *array;
            copy->getReference(i) = std::move(next);
        }
        if (!copy)
            return value;
        changed = true;
        return juce::var(*copy);
    }
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return value;
    juce::var copy;
    for (const auto& property : object->getProperties()) {
        if (property.name == soundModelKey) {
            // A model holds no other: not looked into. A factory model, or the
            // stand-in of one not found, is written as its name.
            auto stored = factoryModelReference(property.value);
            if (stored.isEmpty() && isMissingFactoryModel(property.value))
                stored = property.value.getProperty(soundModelFileKey, {}).toString();
            if (stored.isNotEmpty()) {
                if (copy.isVoid())
                    copy = shallowCopy(*object);
                copy.getDynamicObject()->removeProperty(soundModelKey);
                copy.getDynamicObject()->setProperty(soundModelFileKey, stored);
            }
            continue;
        }
        if (!property.value.isObject() && !property.value.isArray())
            continue;
        auto itemChanged = false;
        auto next = referenced(property.value, itemChanged);
        if (!itemChanged)
            continue;
        if (copy.isVoid())
            copy = shallowCopy(*object);
        copy.getDynamicObject()->setProperty(property.name, next);
    }
    if (copy.isVoid())
        return value;
    changed = true;
    return copy;
}

void expanded(const juce::var& value, juce::StringArray& missing)
{
    if (const auto* array = value.getArray()) {
        for (const auto& item : *array)
            if (item.isObject() || item.isArray())
                expanded(item, missing);
        return;
    }
    auto* object = value.getDynamicObject();
    if (object == nullptr)
        return;
    if (const auto stored = object->getProperty(soundModelFileKey).toString();
        stored.startsWith("factory:") && !object->getProperty(soundModelKey).isObject()) {
        juce::var json;
        juce::String error;
        if (loadSoundModelFile(resolveSamplePath(stored), json, error) != nullptr) {
            object->setProperty(soundModelKey, json);
            object->removeProperty(soundModelFileKey);
        } else {
            // Not here (another computer, a model not installed): its name kept
            // on the card, so the next save still names it.
            auto standIn = juce::DynamicObject::Ptr(new juce::DynamicObject());
            standIn->setProperty(soundModelFileKey, stored);
            object->setProperty(soundModelKey, juce::var(standIn.get()));
            object->removeProperty(soundModelFileKey);
            missing.addIfNotAlreadyThere(stored);
        }
    }
    for (const auto& property : object->getProperties())
        if (property.name != soundModelKey && (property.value.isObject() || property.value.isArray()))
            expanded(property.value, missing);
}

} // namespace

cydaw::engine::SoundModelRef loadSoundModelFile(const juce::File& file, juce::var& json, juce::String& error)
{
    if (!file.existsAsFile()) {
        error = "no file " + file.getFullPathName();
        return {};
    }
    if (file.getSize() > 256 * 1024 * 1024) {
        error = "the sound model is too large";
        return {};
    }
    // A factory model: read once, the same JSON for every card.
    const auto stored = storedSamplePath(file);
    const auto factory = stored.startsWith("factory:");
    const auto modified = file.getLastModificationTime();
    const auto size = file.getSize();
    if (factory) {
        const std::scoped_lock lock(factoryModelsLock());
        if (const auto found = factoryModels().find(stored);
            found != factoryModels().end() && found->second.modified == modified && found->second.size == size) {
            json = found->second.json;
            return soundModelFromVar(json, error);
        }
    }
    json = juce::JSON::parse(file.loadFileAsString());
    if (!json.isObject()) {
        error = "not JSON: " + file.getFileName();
        return {};
    }
    auto model = soundModelFromVar(json, error);
    if (model != nullptr && factory) {
        const std::scoped_lock lock(factoryModelsLock());
        auto& models = factoryModels();
        // The ones nothing holds any more let go.
        for (auto it = models.begin(); it != models.end();)
            it = it->second.json.getDynamicObject() != nullptr && it->second.json.getDynamicObject()->getReferenceCount() <= 1
                     ? models.erase(it)
                     : std::next(it);
        models[stored] = { json, modified, size };
    }
    return model;
}

juce::String factoryModelReference(const juce::var& json)
{
    const auto* object = json.getDynamicObject();
    if (object == nullptr)
        return {};
    const std::scoped_lock lock(factoryModelsLock());
    for (const auto& [stored, model] : factoryModels())
        if (model.json.getDynamicObject() == object)
            return stored;
    return {};
}

juce::var withFactoryModelReferences(const juce::var& document)
{
    auto changed = false;
    return referenced(document, changed);
}

void expandFactoryModelReferences(juce::var& document, juce::StringArray& missing)
{
    expanded(document, missing);
}

bool isMissingFactoryModel(const juce::var& model)
{
    const auto* object = model.getDynamicObject();
    return object != nullptr && object->getProperties().size() == 1
           && object->getProperty(soundModelFileKey).toString().startsWith("factory:");
}

VoiceAssets voiceAssetsFromGraphState(const juce::var& state)
{
    VoiceAssets assets;
    // The devices' classes, by key: a card of that type is one of them.
    std::map<juce::String, const juce::Array<juce::var>*> classes;
    if (const auto* modules = state.getProperty("modules", {}).getArray())
        for (const auto& module : *modules)
            if (const auto* nodes = module.getProperty("nodes", {}).getArray())
                classes[module.getProperty("key", {}).toString()] = nodes;
    auto modelFound = false;
    auto samplerFound = false;
    auto tableFound = false;
    auto prismFound = false;
    std::set<juce::String> visiting;
    std::function<void(const juce::Array<juce::var>&)> walk = [&](const juce::Array<juce::var>& nodes) {
        for (const auto& node : nodes) {
            if (modelFound && samplerFound && tableFound && prismFound)
                return;
            const auto type = node.getProperty("type", {}).toString();
            if (type == "prism-synth") {
                if (!prismFound) {
                    prismFound = true;
                    assets.prismTables = prismTablesFromVar(node.getProperty("prismTables", {}));
                }
            } else if (type == "sampler" || type == "batterie") {
                if (!samplerFound) {
                    samplerFound = true;
                    juce::String error;
                    if (const auto map = upgradedSamplerMap(node.getProperty("sampler", {})); map.isObject())
                        assets.sampler = samplerMapFromVar(map, error);
                }
            } else if (type == "sound-model") {
                if (!modelFound) {
                    modelFound = true;
                    juce::String error;
                    if (const auto model = node.getProperty("soundModel", {}); model.isObject())
                        assets.soundModel = soundModelFromVar(model, error);
                }
            } else if (type == "wavetable") {
                if (!tableFound) {
                    tableFound = true;
                    using Shape = cydaw::engine::WavetableShape;
                    if (const auto* harmonics = node.getProperty("wavetable", {}).getArray();
                        harmonics != nullptr && harmonics->size() == static_cast<int>(Shape::frameCount * Shape::harmonicCount)) {
                        Shape shape;
                        auto index = 0;
                        for (auto& frame : shape.harmonics)
                            for (auto& amplitude : frame)
                                amplitude = static_cast<float>(static_cast<double>((*harmonics)[index++]));
                        assets.wavetable = cydaw::engine::buildWavetableBank(shape);
                    }
                }
            } else if (const auto found = classes.find(type); found != classes.end() && visiting.insert(type).second) {
                walk(*found->second);
            }
        }
    };
    if (const auto* nodes = state.getProperty("nodes", {}).getArray())
        walk(*nodes);
    return assets;
}

namespace {
const char* prismOscillatorKey(const int oscillator)
{
    return oscillator == 0 ? "a" : oscillator == 1 ? "b" : "c";
}
} // namespace

juce::var prismTableVar(const juce::String& name, const juce::String& source, const int frameLength,
                        const std::vector<std::vector<float>>& frames)
{
    juce::MemoryOutputStream pcm;
    auto count = 0;
    for (const auto& frame : frames) {
        if (static_cast<int>(frame.size()) != frameLength)
            continue;
        for (const auto sample : frame)
            pcm.writeShort(static_cast<short>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32'767.0f)));
        ++count;
    }
    auto table = std::make_unique<juce::DynamicObject>();
    table->setProperty("name", name);
    table->setProperty("source", source);
    table->setProperty("frameLength", frameLength);
    table->setProperty("frames", count);
    table->setProperty("pcm", juce::Base64::toBase64(pcm.getData(), pcm.getDataSize()));
    return juce::var(table.release());
}

cydaw::engine::prism::TablesRef prismTablesFromVar(const juce::var& value)
{
    if (!value.isObject())
        return {};
    auto tables = std::make_shared<cydaw::engine::prism::Tables>();
    auto any = false;
    for (int oscillator = 0; oscillator < cydaw::engine::prism::oscillatorCount; ++oscillator) {
        const auto table = value.getProperty(prismOscillatorKey(oscillator), {});
        if (!table.isObject())
            continue;
        const auto frameLength = static_cast<int>(table.getProperty("frameLength", 0));
        const auto count = static_cast<int>(table.getProperty("frames", 0));
        if (frameLength < 16 || frameLength > 16'384 || count < 1 || count > 1'024)
            continue;
        juce::MemoryOutputStream bytes;
        if (!juce::Base64::convertFromBase64(bytes, table.getProperty("pcm", {}).toString())
            || bytes.getDataSize() < static_cast<std::size_t>(frameLength) * static_cast<std::size_t>(count) * 2)
            continue;
        juce::MemoryInputStream input(bytes.getData(), bytes.getDataSize(), false);
        std::vector<std::vector<float>> frames(static_cast<std::size_t>(count), std::vector<float>(static_cast<std::size_t>(frameLength)));
        for (auto& frame : frames)
            for (auto& sample : frame)
                sample = static_cast<float>(input.readShort()) / 32'767.0f;
        if (auto built = cydaw::engine::prism::buildWavetable(table.getProperty("name", {}).toString().toStdString(), frames)) {
            tables->oscillator[static_cast<std::size_t>(oscillator)] = std::move(built);
            any = true;
        }
    }
    return any ? cydaw::engine::prism::TablesRef(std::move(tables)) : cydaw::engine::prism::TablesRef {};
}

juce::var withPrismTable(const juce::var& tables, const int oscillator, const juce::var& table)
{
    auto copy = std::make_unique<juce::DynamicObject>();
    for (int other = 0; other < cydaw::engine::prism::oscillatorCount; ++other) {
        const auto* key = prismOscillatorKey(other);
        const auto kept = other == oscillator ? table : tables.getProperty(key, {});
        if (kept.isObject())
            copy->setProperty(key, kept);
    }
    if (copy->getProperties().isEmpty())
        return {};
    return juce::var(copy.release());
}

juce::String prismTableName(const juce::var& tables, const int oscillator)
{
    return tables.getProperty(prismOscillatorKey(oscillator), {}).getProperty("name", {}).toString();
}

juce::var impulseVar(const cydaw::engine::ImpulseResponse& response, const juce::String& source)
{
    juce::MemoryOutputStream pcm;
    for (const auto& channel : response.channels)
        for (const auto sample : channel)
            pcm.writeFloat(sample);
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("name", juce::String(response.name));
    value->setProperty("source", source);
    value->setProperty("sampleRate", response.sampleRate);
    value->setProperty("channels", static_cast<int>(response.channels.size()));
    value->setProperty("frames", static_cast<juce::int64>(response.length()));
    value->setProperty("pcm", juce::Base64::toBase64(pcm.getData(), pcm.getDataSize()));
    return juce::var(value.release());
}

cydaw::engine::ImpulseResponseRef impulseFromVar(const juce::var& value)
{
    if (!value.isObject())
        return {};
    const auto channels = static_cast<int>(value.getProperty("channels", 0));
    const auto frames = static_cast<juce::int64>(value.getProperty("frames", 0));
    const auto rate = static_cast<double>(value.getProperty("sampleRate", 0.0));
    if (channels < 1 || channels > 2 || frames < 1 || frames > static_cast<juce::int64>(cydaw::engine::maximumImpulseSamples) * 4
        || rate < 8'000.0 || rate > 384'000.0)
        return {};
    juce::MemoryOutputStream bytes;
    if (!juce::Base64::convertFromBase64(bytes, value.getProperty("pcm", {}).toString())
        || static_cast<juce::int64>(bytes.getDataSize()) < frames * channels * 4)
        return {};
    juce::MemoryInputStream input(bytes.getData(), bytes.getDataSize(), false);
    auto response = std::make_shared<cydaw::engine::ImpulseResponse>();
    response->name = value.getProperty("name", {}).toString().toStdString();
    response->sampleRate = rate;
    response->channels.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    for (auto& channel : response->channels)
        for (auto& sample : channel) {
            sample = input.readFloat();
            if (!std::isfinite(sample))
                sample = 0.0f;
        }
    return response;
}

std::map<std::uint64_t, cydaw::engine::ImpulseResponseRef> impulsesFromGraphState(const juce::var& state)
{
    std::map<std::uint64_t, cydaw::engine::ImpulseResponseRef> found;
    if (const auto* nodes = state.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            if (auto response = impulseFromVar(node.getProperty("impulse", {})))
                found[static_cast<std::uint64_t>(static_cast<juce::int64>(node.getProperty("id", 0)))] = std::move(response);
    return found;
}

void attachImpulses(cydaw::engine::NativeDspGraph& graph, const juce::var& editorState)
{
    const auto responses = impulsesFromGraphState(editorState);
    if (responses.empty())
        return;
    for (auto& node : graph.nodes)
        if (const auto found = responses.find(node.id); found != responses.end())
            node.impulse = found->second;
}

juce::String describeSoundModel(const cydaw::engine::SoundModel& model)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::StringArray notes;
    auto partials = 0;
    for (const auto& zone : model.zones) {
        notes.addIfNotAlreadyThere(juce::String(names[zone.rootNote % 12]) + juce::String(zone.rootNote / 12 - 1));
        partials = std::max(partials, zone.harmonicCount);
    }
    return juce::String(model.name) + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(static_cast<int>(model.zones.size()))
           + (model.zones.size() > 1 ? " zones" : " zone") + juce::String::fromUTF8(" \xc2\xb7 ") + notes.joinIntoString(", ")
           + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(partials) + " partials";
}

} // namespace cydaw::app
