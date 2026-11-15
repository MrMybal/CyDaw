#include <cydaw/base/Utilities.h>
#include <cydaw/audio/SampleBuffer.h>
#include <cydaw/base/TextMap.h>
#include <cydaw/integration/AudioFiles.h>
#include <cydaw/base/File.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include "FoundryDocument.h"
#include "FoundryJson.h"

#include "Widgets.h"
#include "ModeProcessing.h"

#include <algorithm>
#include <map>
#include <new>

namespace cydaw::v2 {

using engine::foundry::Clip;
using engine::foundry::Track;

namespace {

// Hours of a sound in memory would not fit: this many frames at most.
constexpr std::int64_t largestFile = static_cast<std::int64_t>(48'000) * 60 * 60 * 3;

int intOf(const cydaw::base::TextMap& values, const cydaw::base::Text& key, const int fallback = 0)
{
    return values.containsKey(key) ? values[key].getIntValue() : fallback;
}

double number(const cydaw::base::Value& object, const char* key, const double fallback = 0.0)
{
    const auto value = object.getProperty(key, cydaw::base::Value());
    return value.isVoid() ? fallback : static_cast<double>(value);
}

std::string utf8(const cydaw::base::Text& text)
{
    return text.toStdString();
}

cydaw::base::Text fromUtf8(const std::string& text)
{
    return cydaw::base::Text::fromUTF8(text.c_str());
}

} // namespace

std::shared_ptr<const engine::foundry::Sound> FoundryDocument::readSound(const cydaw::base::File& file, cydaw::base::Text& error, int* bits,
                                                                         std::vector<FoundryMarker>* markers)
{
    cydaw::audio::AudioFormatManager formats;
    cydaw::integration::registerCyAudioCoreReaders(formats);
    std::unique_ptr<cydaw::audio::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        error = file.existsAsFile() ? ui::Ls("Format non lu : ", "Format not read: ") + file.getFileName()
                                    : ui::Ls("Fichier introuvable : ", "File not found: ") + file.getFullPathName();
        return nullptr;
    }
    const auto frames = reader->lengthInSamples;
    if (frames > largestFile) {
        error = ui::Ls("Fichier trop long pour être édité en mémoire", "File too long to be edited in memory");
        return nullptr;
    }
    // An uncompressed file holds what its header says: one claiming more
    // than its size (a broken or forged header) is not believed.
    if (const auto format = reader->metadataValues.getValue("cydsp.format", reader->getFormatName()); format.containsIgnoreCase("WAV") || format.containsIgnoreCase("AIFF")) {
        const auto bytes = static_cast<double>(frames) * std::max(1u, reader->numChannels) * std::max(1u, reader->bitsPerSample / 8u);
        if (bytes > static_cast<double>(file.getSize()) + 1'048'576.0) {
            error = ui::Ls("Fichier abîmé : ", "Damaged file: ") + file.getFileName();
            return nullptr;
        }
    }
    const auto channels = static_cast<int>(std::clamp<unsigned>(reader->numChannels, 1u, static_cast<unsigned>(engine::foundry::maximumChannels)));
    std::vector<std::vector<float>> data;
    try {
        data.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(0, frames))));
    } catch (const std::bad_alloc&) {
        error = ui::Ls("Pas assez de mémoire pour ", "Not enough memory for ") + file.getFileName();
        return nullptr;
    }
    constexpr int chunk = 1 << 16;
    cydaw::audio::SampleBuffer<float> buffer(static_cast<int>(std::max(1u, reader->numChannels)), chunk);
    for (std::int64_t at = 0; at < frames; at += chunk) {
        const auto n = static_cast<int>(std::min<std::int64_t>(chunk, frames - at));
        buffer.clear();
        if (!reader->read(&buffer, 0, n, at, true, true)) {
            error = ui::Ls("Lecture impossible : ", "Cannot read: ") + file.getFileName();
            return nullptr;
        }
        for (int c = 0; c < channels; ++c)
            std::copy_n(buffer.getReadPointer(c), n, data[static_cast<std::size_t>(c)].data() + at);
    }
    if (bits != nullptr) {
        *bits = reader->bitsPerSample >= 32 ? 32 : static_cast<int>(reader->bitsPerSample);
        if (*bits != 16 && *bits != 24 && *bits != 32)
            *bits = 24;
    }
    // A WAV's cue points: markers, regions with their lengths and names.
    if (markers != nullptr) {
        const auto& values = reader->metadataValues;
        const auto cues = intOf(values, "NumCuePoints");
        for (int i = 0; i < cues; ++i) {
            const auto prefix = "Cue" + cydaw::base::Text(i);
            const auto identifier = intOf(values, prefix + "Identifier");
            FoundryMarker marker;
            marker.start = intOf(values, prefix + "Offset");
            marker.end = marker.start;
            for (int l = 0; l < intOf(values, "NumCueLabels"); ++l)
                if (intOf(values, "CueLabel" + cydaw::base::Text(l) + "Identifier") == identifier)
                    marker.name = values["CueLabel" + cydaw::base::Text(l) + "Text"];
            for (int r = 0; r < intOf(values, "NumCueRegions"); ++r)
                if (intOf(values, "CueRegion" + cydaw::base::Text(r) + "Identifier") == identifier) {
                    marker.end = marker.start + intOf(values, "CueRegion" + cydaw::base::Text(r) + "SampleLength");
                    if (marker.name.isEmpty())
                        marker.name = values["CueRegion" + cydaw::base::Text(r) + "Text"];
                }
            if (marker.name.isEmpty())
                marker.name = (marker.region() ? ui::Ls("Région ", "Region ") : ui::Ls("Marqueur ", "Marker ")) + cydaw::base::Text(i + 1);
            markers->push_back(std::move(marker));
        }
    }
    return std::make_shared<const engine::foundry::Sound>(std::move(data), reader->sampleRate);
}

std::unique_ptr<FoundryDocument> FoundryDocument::open(const cydaw::base::File& file, cydaw::base::Text& error)
{
    if (file.hasFileExtension("cyfoundry;cycinema"))
        return openSession(file, error);
    int bits = 24;
    std::vector<FoundryMarker> markers;
    const auto sound = readSound(file, error, &bits, &markers);
    if (sound == nullptr)
        return nullptr;
    auto document = std::make_unique<FoundryDocument>();
    Session session;
    session.rate = sound->rate();
    Track track;
    track.id = session.newId();
    track.name = utf8(file.getFileNameWithoutExtension());
    Clip clip;
    clip.id = session.newId();
    clip.sound = sound;
    clip.length = sound->frames();
    clip.name = utf8(file.getFileNameWithoutExtension());
    clip.file = utf8(file.getFullPathName());
    track.clips.push_back(std::move(clip));
    session.tracks.push_back(std::move(track));
    document->session_ = std::make_shared<const Session>(std::move(session));
    document->file_ = file;
    document->name_ = file.getFileName();
    document->bits_ = bits;
    document->markers_ = std::move(markers);
    return document;
}

std::unique_ptr<FoundryDocument> FoundryDocument::create(const double rate, const cydaw::base::Text& name)
{
    auto document = std::make_unique<FoundryDocument>();
    Session session;
    session.rate = rate > 0.0 ? rate : 48'000.0;
    session = engine::foundry::withTrack(session, utf8(ui::Ls("Piste 1", "Track 1")));
    document->session_ = std::make_shared<const Session>(std::move(session));
    document->name_ = name;
    return document;
}

void FoundryDocument::makeTemplateCopy(const cydaw::base::Text& name)
{
    name_ = name;
    file_ = {};
    sessionFile_ = {};
    dirty_ = true;
    ++revision_;
}

cydaw::base::Text FoundryDocument::name() const
{
    if (sessionFile_ != cydaw::base::File())
        return sessionFile_.getFileName();
    return file_ != cydaw::base::File() ? file_.getFileName() : name_;
}

bool FoundryDocument::plainFile() const noexcept
{
    if(session_->cinema)return false;
    const auto& session = *session_;
    if (!session.masterAutomation.empty() || std::any_of(session.tracks.begin(),session.tracks.end(),[](const auto& t){return t.midi || !t.automation.empty();}) || processing_.getDynamicObject() != nullptr || session.tracks.size() != 1 || session.tracks[0].clips.size() != 1)
        return false;
    const auto& track = session.tracks[0];
    const auto& clip = track.clips[0];
    return clip.start == 0 && clip.offset == 0 && clip.sound != nullptr && clip.length == clip.sound->frames() && clip.fadeIn == 0
           && clip.fadeOut == 0 && clip.gainDb == 0.0f && track.gainDb == 0.0f && track.pan == 0.0f;
}

void FoundryDocument::changed()
{
    dirty_ = true;
    ++revision_;
}

void FoundryDocument::apply(Session next, const cydaw::base::Text& label)
{
    apply(std::move(next), markers_, label);
}

void FoundryDocument::apply(Session next, std::vector<FoundryMarker> markers, const cydaw::base::Text& label)
{
    undo_.push_back({ session_, markers_, label });
    redo_.clear();
    session_ = std::make_shared<const Session>(std::move(next));
    markers_ = std::move(markers);
    changed();
}

void FoundryDocument::replace(Session next)
{
    session_ = std::make_shared<const Session>(std::move(next));
    changed();
}

void FoundryDocument::setMarkers(std::vector<FoundryMarker> markers, const cydaw::base::Text& label)
{
    undo_.push_back({ session_, markers_, label });
    redo_.clear();
    markers_ = std::move(markers);
    changed();
}

cydaw::base::Text FoundryDocument::undoLabel() const
{
    return undo_.empty() ? cydaw::base::Text() : undo_.back().label;
}

cydaw::base::Text FoundryDocument::redoLabel() const
{
    return redo_.empty() ? cydaw::base::Text() : redo_.back().label;
}

bool FoundryDocument::undo()
{
    if (undo_.empty())
        return false;
    auto step = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({ session_, markers_, step.label });
    session_ = std::move(step.session);
    markers_ = std::move(step.markers);
    changed();
    return true;
}

bool FoundryDocument::redo()
{
    if (redo_.empty())
        return false;
    auto step = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({ session_, markers_, step.label });
    session_ = std::move(step.session);
    markers_ = std::move(step.markers);
    changed();
    return true;
}

cydaw::base::TextList FoundryDocument::history() const
{
    cydaw::base::TextList labels;
    for (const auto& step : undo_)
        labels.add(step.label);
    return labels;
}

std::vector<FoundryMarker> FoundryDocument::shiftedMarkers(const std::int64_t at, const std::int64_t removed,
                                                           const std::int64_t inserted) const
{
    std::vector<FoundryMarker> result;
    const auto cut = at + removed;
    const auto move = [&](const std::int64_t frame) {
        if (frame < at)
            return frame;
        if (frame < cut)
            return at;
        return frame - removed + inserted;
    };
    for (auto marker : markers_) {
        if (!marker.region() && marker.start >= at && marker.start < cut && removed > 0)
            continue;
        const auto start = move(marker.start);
        const auto end = marker.region() ? move(marker.end) : start;
        if (marker.region() && end <= start)
            continue;
        marker.start = start;
        marker.end = end;
        result.push_back(std::move(marker));
    }
    return result;
}

cydaw::base::Result FoundryDocument::writeSound(const Sound& sound, const cydaw::base::File& file, int bits, const std::vector<FoundryMarker>& markers)
{
    std::unique_ptr<cydaw::audio::AudioFormat> format;
    if (file.hasFileExtension("flac"))
        format = std::make_unique<cydaw::integration::FlacFormat>();
    else if (file.hasFileExtension("aif;aiff"))
        format = std::make_unique<cydaw::integration::AiffFormat>();
    else if (file.hasFileExtension("ogg"))
        format = std::make_unique<cydaw::integration::OggFormat>();
    else
        format = std::make_unique<cydaw::integration::WaveFormat>();
    const auto wav = dynamic_cast<cydaw::integration::WaveFormat*>(format.get()) != nullptr;
    if (!wav && bits == 32)
        bits = 24;
    if (file.hasFileExtension("ogg"))
        bits = 16;
    auto options = cydaw::audio::AudioFormatWriterOptions().withSampleRate(sound.rate()).withNumChannels(sound.channels()).withBitsPerSample(bits);
    if (wav && bits == 32)
        options = options.withSampleFormat(cydaw::audio::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    if (file.hasFileExtension("ogg"))
        options = options.withQualityOptionIndex(8);
    if (wav && !markers.empty()) {
        auto labels = 0;
        auto regions = 0;
        options = options.withMetadata("NumCuePoints", cydaw::base::Text(static_cast<int>(markers.size())));
        for (std::size_t i = 0; i < markers.size(); ++i) {
            const auto& marker = markers[i];
            const auto identifier = cydaw::base::Text(static_cast<int>(i) + 1);
            const auto prefix = "Cue" + cydaw::base::Text(static_cast<int>(i));
            options = options.withMetadata(prefix + "Identifier", identifier)
                          .withMetadata(prefix + "Offset", cydaw::base::Text(marker.start))
                          .withMetadata(prefix + "Order", cydaw::base::Text(static_cast<int>(i)));
            if (marker.region()) {
                const auto region = "CueRegion" + cydaw::base::Text(regions++);
                options = options.withMetadata(region + "Identifier", identifier)
                              .withMetadata(region + "SampleLength", cydaw::base::Text(marker.end - marker.start))
                              .withMetadata(region + "Purpose", cydaw::base::Text(0x206e6772))
                              .withMetadata(region + "Text", marker.name);
            } else {
                const auto label = "CueLabel" + cydaw::base::Text(labels++);
                options = options.withMetadata(label + "Identifier", identifier).withMetadata(label + "Text", marker.name);
            }
        }
        options = options.withMetadata("NumCueLabels", cydaw::base::Text(labels)).withMetadata("NumCueRegions", cydaw::base::Text(regions));
    }
    // Written beside it, then put in its place: a file in use stays whole.
    const auto temporary = file.getSiblingFile("." + file.getFileNameWithoutExtension() + ".writing" + file.getFileExtension());
    {
        temporary.deleteFile();
        auto stream = temporary.createOutputStream();
        if (stream == nullptr)
            return cydaw::base::Result::fail(ui::Ls("Fichier impossible à écrire : ", "Cannot write the file: ") + file.getFullPathName());
        std::unique_ptr<cydaw::base::OutputStream> output { std::move(stream) };
        auto writer = format->createWriterFor(output, options);
        if (writer == nullptr) {
            temporary.deleteFile();
            return cydaw::base::Result::fail(ui::Ls("Ce format n'accepte pas ce son (canaux, fréquence ou bits)",
                                             "This format does not take this sound (channels, rate or bits)"));
        }
        constexpr std::int64_t chunk = 1 << 16;
        std::vector<std::vector<float>> data(static_cast<std::size_t>(sound.channels()), std::vector<float>(chunk));
        std::vector<const float*> pointers(static_cast<std::size_t>(sound.channels()));
        for (std::int64_t at = 0; at < sound.frames(); at += chunk) {
            const auto n = std::min(chunk, sound.frames() - at);
            for (int c = 0; c < sound.channels(); ++c) {
                sound.read(c, at, n, data[static_cast<std::size_t>(c)].data());
                pointers[static_cast<std::size_t>(c)] = data[static_cast<std::size_t>(c)].data();
            }
            if (!writer->writeFromFloatArrays(pointers.data(), sound.channels(), static_cast<int>(n))) {
                writer.reset();
                temporary.deleteFile();
                return cydaw::base::Result::fail(ui::Ls("Écriture interrompue", "Writing stopped"));
            }
        }
    }
    if (!temporary.moveFileTo(file)) {
        temporary.deleteFile();
        return cydaw::base::Result::fail(ui::Ls("Le fichier est utilisé (par le projet ?) : enregistrez sous un autre nom",
                                         "The file is in use (by the project?): save it under another name"));
    }
    return cydaw::base::Result::ok();
}

cydaw::base::Result FoundryDocument::save(const cydaw::base::File& file, const int bits)
{
    if (!plainFile())
        return cydaw::base::Result::fail(ui::Ls("Plusieurs clips : enregistrez la session, ou exportez le mixage",
                                         "Several clips: save the session, or export the mix"));
    const auto& clip = session_->tracks[0].clips[0];
    if (const auto result = writeSound(*clip.sound, file, bits, markers_); result.failed())
        return result;
    // The clip is that file now, untouched.
    auto next = *session_;
    next.tracks[0].clips[0].file = utf8(file.getFullPathName());
    next.tracks[0].clips[0].edited = false;
    session_ = std::make_shared<const Session>(std::move(next));
    file_ = file;
    bits_ = bits;
    dirty_ = false;
    ++revision_;
    return cydaw::base::Result::ok();
}

cydaw::base::Result FoundryDocument::exportMix(const cydaw::base::File& file, const int bits, std::int64_t start, std::int64_t end) const
{
    const auto& session = *session_;
    if (end <= start) {
        start = 0;
        end = session.frames();
    }
    if (end <= start)
        return cydaw::base::Result::fail(ui::Ls("Rien à exporter", "Nothing to export"));
    std::vector<FoundryMarker> inside;
    for (auto marker : markers_)
        if (marker.start >= start && marker.start < end) {
            marker.end = marker.region() ? std::min(marker.end, end) - start : marker.start - start;
            marker.start -= start;
            inside.push_back(std::move(marker));
        }
    return writeSound((processing_.getDynamicObject() || !session.masterAutomation.empty() || std::any_of(session.tracks.begin(),session.tracks.end(),[](const auto& t){return t.midi || !t.automation.empty();})) ? ModeProcessing::render(session,processing_,start,end,renderTail_) : engine::foundry::render(session, start, end), file, bits, inside);
}

cydaw::base::Result FoundryDocument::saveSession(const cydaw::base::File& file)
{
    const auto& session = *session_;
    const auto folder = file.getSiblingFile(file.getFileNameWithoutExtension() + " files");
    // The sounds edited (each once), the others where they are.
    std::map<const Sound*, cydaw::base::Text> written;
    auto* root = new cydaw::base::Object();
    root->setProperty("format", session.cinema ? "CyDAW Cinema session" : "CyDAW Foundry session");
    auto* picture = new cydaw::base::Object();
    picture->setProperty("file",fromUtf8(session.picture.file));
    picture->setProperty("start",session.picture.start);picture->setProperty("sourceIn",session.picture.sourceIn);
    picture->setProperty("duration",session.picture.duration);picture->setProperty("fps",session.picture.fps);
    root->setProperty("picture",cydaw::base::Value(picture));
    root->setProperty("version", session.cinema ? 5 : 3);
    root->setProperty("processing",processing_);
    root->setProperty("renderTail",renderTail_);
    root->setProperty("rate", session.rate);
    root->setProperty("tempo",session.tempo);
    root->setProperty("span",session.span);
    root->setProperty("masterAutomation",foundry_json::automation(session.masterAutomation));
    cydaw::base::Array<cydaw::base::Value> tracks;
    for (const auto& track : session.tracks) {
        auto* entry = new cydaw::base::Object();
        entry->setProperty("id",static_cast<std::int64_t>(track.id));
        entry->setProperty("name", fromUtf8(track.name));
        entry->setProperty("gainDb", track.gainDb);
        entry->setProperty("pan", track.pan);
        entry->setProperty("mute", track.mute);
        entry->setProperty("solo", track.solo);
        entry->setProperty("midi",track.midi);
        entry->setProperty("bus",track.bus);
        entry->setProperty("virtualGroup",track.virtualGroup);
        entry->setProperty("parentId",static_cast<std::int64_t>(track.parentId));
        entry->setProperty("containerStart",track.containerStart);entry->setProperty("containerLength",track.containerLength);
        entry->setProperty("automation",foundry_json::automation(track.automation));
        cydaw::base::Array<cydaw::base::Value> clips;
        for (const auto& clip : track.clips) {
            auto* item = new cydaw::base::Object();
            item->setProperty("id",static_cast<std::int64_t>(clip.id));
            item->setProperty("notes",foundry_json::notes(clip.notes));
            item->setProperty("name", fromUtf8(clip.name));
            item->setProperty("start", clip.start);
            item->setProperty("offset", clip.offset);
            item->setProperty("length", clip.length);
            item->setProperty("fadeIn", clip.fadeIn);
            item->setProperty("fadeOut", clip.fadeOut);
            item->setProperty("gainDb", clip.gainDb);
            if (!clip.file.empty())
                item->setProperty("file", fromUtf8(clip.file));
            const auto source = cydaw::base::File(fromUtf8(clip.file));
            const auto inPlace = !clip.edited && !clip.file.empty() && source.existsAsFile() && clip.sound != nullptr
                                 && std::abs(clip.sound->rate() - session.rate) < 0.5;
            if (!inPlace && clip.sound != nullptr) {
                auto& name = written[clip.sound.get()];
                if (name.isEmpty()) {
                    if (!folder.createDirectory())
                        return cydaw::base::Result::fail(ui::Ls("Dossier impossible à créer : ", "Cannot make the folder: ") + folder.getFullPathName());
                    name = folder.getFileName() + "/clip-" + cydaw::base::Text(static_cast<std::int64_t>(clip.id)) + ".wav";
                    if (const auto result = writeSound(*clip.sound, file.getSiblingFile(name), 32); result.failed())
                        return result;
                }
                item->setProperty("sound", name);
            }
            clips.add(cydaw::base::Value(item));
        }
        entry->setProperty("clips", clips);
        tracks.add(cydaw::base::Value(entry));
    }
    root->setProperty("tracks", tracks);
    cydaw::base::Array<cydaw::base::Value> markers;
    for (const auto& marker : markers_) {
        auto* item = new cydaw::base::Object();
        item->setProperty("start", marker.start);
        item->setProperty("end", marker.end);
        item->setProperty("name", marker.name);
        markers.add(cydaw::base::Value(item));
    }
    root->setProperty("markers", markers);
    if (!file.replaceWithText(cydaw::base::JSON::toString(cydaw::base::Value(root))))
        return cydaw::base::Result::fail(ui::Ls("Fichier impossible à écrire : ", "Cannot write the file: ") + file.getFullPathName());
    sessionFile_ = file;
    file_ = {};
    dirty_ = false;
    ++revision_;
    return cydaw::base::Result::ok();
}

std::unique_ptr<FoundryDocument> FoundryDocument::openSession(const cydaw::base::File& file, cydaw::base::Text& error)
{
    const auto document = cydaw::base::JSON::parse(file);
    const auto format=document.getProperty("format",cydaw::base::Value()).toString();
    if (format != "CyDAW Foundry session" && format != "CyDAW Cinema session") {
        error = ui::Ls("Ce n'est pas une session de la Fonderie : ", "Not a Foundry session: ") + file.getFileName();
        return nullptr;
    }
    Session session;
    session.cinema=format=="CyDAW Cinema session";
    const auto picture=document.getProperty("picture",{});
    session.picture.file=picture.getProperty("file",{}).toString().toStdString();
    if (!session.picture.file.empty())
        session.picture.file = file.getSiblingFile(fromUtf8(session.picture.file)).getFullPathName().toStdString();
    session.picture.start=std::max(0.0,number(picture,"start"));
    session.picture.sourceIn=std::max(0.0,number(picture,"sourceIn"));
    session.picture.duration=std::max(0.0,number(picture,"duration"));
    session.picture.fps=std::clamp(number(picture,"fps",25),1.0,240.0);
    session.rate = number(document, "rate", 48'000.0);
    session.tempo=std::clamp(number(document,"tempo",120),20.0,400.0);
    session.span=static_cast<std::int64_t>(document.getProperty("span",0));
    session.masterAutomation=foundry_json::automation(document.getProperty("masterAutomation",{}));
    std::map<cydaw::base::Text, std::shared_ptr<const Sound>> read;
    if (const auto* tracks = document.getProperty("tracks", cydaw::base::Value()).getArray())
        for (const auto& entry : *tracks) {
            Track track;
            track.id = static_cast<std::uint64_t>(number(entry,"id",0));
            if(!track.id)track.id=session.newId();
            session.nextId=std::max(session.nextId,track.id+1);
            track.name = utf8(entry.getProperty("name", cydaw::base::Value()).toString());
            track.gainDb = static_cast<float>(number(entry, "gainDb"));
            track.pan = static_cast<float>(number(entry, "pan"));
            track.mute = static_cast<bool>(entry.getProperty("mute", false));
            track.solo = static_cast<bool>(entry.getProperty("solo", false));
            track.midi=static_cast<bool>(entry.getProperty("midi",false));
            track.bus=static_cast<bool>(entry.getProperty("bus",false));
            track.virtualGroup=track.bus&&static_cast<bool>(entry.getProperty("virtualGroup",false));
            track.parentId=static_cast<std::uint64_t>(number(entry,"parentId",0));
            track.containerStart=static_cast<std::int64_t>(std::max(0.0,number(entry,"containerStart",0)));
            track.containerLength=static_cast<std::int64_t>(std::max(0.0,number(entry,"containerLength",0)));
            track.automation=foundry_json::automation(entry.getProperty("automation",{}));
            if (const auto* clips = entry.getProperty("clips", cydaw::base::Value()).getArray())
                for (const auto& item : *clips) {
                    Clip clip;
                    clip.id = static_cast<std::uint64_t>(number(item,"id"));
                    if(!clip.id)clip.id=session.newId();
                    session.nextId=std::max(session.nextId,clip.id+1);
                    clip.notes=foundry_json::notes(item.getProperty("notes",{}),session);
                    clip.name = utf8(item.getProperty("name", cydaw::base::Value()).toString());
                    clip.start = static_cast<std::int64_t>(number(item, "start"));
                    clip.offset = static_cast<std::int64_t>(number(item, "offset"));
                    clip.length = static_cast<std::int64_t>(number(item, "length"));
                    clip.fadeIn = static_cast<std::int64_t>(number(item, "fadeIn"));
                    clip.fadeOut = static_cast<std::int64_t>(number(item, "fadeOut"));
                    clip.gainDb = static_cast<float>(number(item, "gainDb"));
                    clip.file = utf8(item.getProperty("file", cydaw::base::Value()).toString());
                    if(track.midi) {track.clips.push_back(std::move(clip));continue;}
                    const auto own = item.getProperty("sound", cydaw::base::Value()).toString();
                    clip.edited = own.isNotEmpty();
                    const auto source = file.getSiblingFile(own.isNotEmpty() ? own : fromUtf8(clip.file));
                    clip.file = utf8(source.getFullPathName());
                    auto& sound = read[source.getFullPathName()];
                    if (sound == nullptr) {
                        cydaw::base::Text problem;
                        sound = readSound(source, problem);
                        if (sound != nullptr && std::abs(sound->rate() - session.rate) > 0.5)
                            sound = std::make_shared<const Sound>(sound->withRate(session.rate));
                    }
                    if (sound == nullptr) {
                        // Missing: silence as long, so the session still opens.
                        sound = std::make_shared<const Sound>(2, clip.offset + clip.length, session.rate);
                        error = ui::Ls("Son introuvable : ", "Sound not found: ") + source.getFullPathName();
                    }
                    clip.sound = sound;
                    track.clips.push_back(std::move(clip));
                }
            session.tracks.push_back(std::move(track));
        }
    auto opened = std::make_unique<FoundryDocument>();
    opened->session_ = std::make_shared<const Session>(std::move(session));
    opened->processing_=document.getProperty("processing",{});
    opened->renderTail_=static_cast<float>(std::clamp(number(document,"renderTail",0),0.0,30.0));
    opened->sessionFile_ = file;
    opened->name_ = file.getFileName();
    if (const auto* markers = document.getProperty("markers", cydaw::base::Value()).getArray())
        for (const auto& item : *markers)
            opened->markers_.push_back({ static_cast<std::int64_t>(number(item, "start")), static_cast<std::int64_t>(number(item, "end")),
                                         item.getProperty("name", cydaw::base::Value()).toString() });
    return opened;
}

} // namespace cydaw::v2
