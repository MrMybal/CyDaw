#include <cydaw/base/TextMap.h>
#include <cydaw/base/Text.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include <cyaudio/io/AudioFile.h>
#include <cyaudio/io/AudioMetadata.h>
#include <cyaudio/core/DspTypes.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <istream>
#include <ostream>
#include <limits>
#include <streambuf>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace cydaw::integration {
namespace {
constexpr std::size_t chunkFrames = 4096;

// Borrowed seekable stream. Reader's base owns it and destroys it last.
class InputBuffer final : public std::streambuf {
public:
    explicit InputBuffer(cydaw::base::InputStream& stream) : stream_(stream) {}
private:
    std::streamsize xsgetn(char* dest, std::streamsize count) override {
        std::streamsize done = 0;
        while (done < count) {
            const auto n = static_cast<int>(std::min<std::streamsize>(count - done, 1 << 20));
            const auto got = stream_.read(dest + done, n);
            if (got <= 0) break;
            done += got;
        }
        return done;
    }
    int_type underflow() override {
        const auto at = stream_.getPosition();
        char c {};
        if (stream_.read(&c, 1) != 1 || !stream_.setPosition(at)) return traits_type::eof();
        return traits_type::to_int_type(c);
    }
    int_type uflow() override {
        char c {};
        return stream_.read(&c, 1) == 1 ? traits_type::to_int_type(c) : traits_type::eof();
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode) override {
        const auto base = way == std::ios_base::beg ? cydaw::base::int64(0)
                        : way == std::ios_base::cur ? stream_.getPosition() : stream_.getTotalLength();
        if (base < 0 || (offset > 0 && base > std::numeric_limits<cydaw::base::int64>::max() - offset)
            || offset < -base) return pos_type(off_type(-1));
        const auto at = base + offset;
        return stream_.setPosition(at) ? pos_type(at) : pos_type(off_type(-1));
    }
    pos_type seekpos(pos_type at, std::ios_base::openmode mode) override {
        return seekoff(off_type(at), std::ios_base::beg, mode);
    }
    cydaw::base::InputStream& stream_;
};

// The persisted application vocabulary remains stable while the implementation
// uses CyAudioCore's public versioned contract. Unknown legacy keys must take the
// explicit compatibility path; never silently discard metadata during export.
template<class Map>
bool writeMetadata(const Map& values,
                   cyaudio::AudioFileInfo& info) {
    if (values.empty()) return true;
    cydaw::base::TextMap v;
    for (const auto& [key, value] : values) v.set(key, value);
    cyaudio::AudioMetadata m;
    if (const auto opaque = v["cydsp.metadata"]; opaque.isNotEmpty()) {
        if (cyaudio::parseAudioMetadata(opaque.toStdString(), m) != cyaudio::Status::ok) return false;
    }
    std::set<std::string> used {"cydsp.metadata", "cydsp.format", "MetaDataSource"};
    const auto text = [&](const cydaw::base::Text& key, const cydaw::base::Text& fallback = {}) {
        used.insert(key.toStdString());
        return v.getValue(key, fallback);
    };
    const auto number = [&](const cydaw::base::Text& key, cydaw::base::int64 fallback = 0) -> std::uint64_t {
        const auto value = text(key, cydaw::base::Text(fallback)).getLargeIntValue();
        if (value < 0 || value > cydaw::base::int64(UINT32_MAX)) throw std::invalid_argument("metadata integer");
        return static_cast<std::uint64_t>(value);
    };
    const auto count = [&](const char* key) {
        const auto n = number(key);
        if (n > 65536) throw std::invalid_argument("metadata count");
        return static_cast<unsigned>(n);
    };
    try {
        if (v.containsKey("NumCuePoints")) {
            m.markers.clear();
            for (unsigned i = 0, n = count("NumCuePoints"); i < n; ++i) {
                const auto k = "Cue" + cydaw::base::Text(i);
                cyaudio::AudioMarker marker;
                marker.id = static_cast<std::uint32_t>(number(k + "Identifier"));
                marker.frame = number(k + "Offset");
                marker.position = static_cast<std::uint32_t>(number(k + "Order", i));
                m.markers.push_back(std::move(marker));
            }
        }
        for (const auto* kind : {"Label", "Note"}) {
            const auto key = "NumCue" + cydaw::base::Text(kind) + "s";
            if (!v.containsKey(key)) continue;
            for (auto& marker : m.markers)
                (cydaw::base::Text(kind) == "Label" ? marker.name : marker.note).clear();
            for (unsigned i = 0, n = count(key.toRawUTF8()); i < n; ++i) {
                const auto k = "Cue" + cydaw::base::Text(kind) + cydaw::base::Text(i);
                const auto id = number(k + "Identifier");
                const auto value = text(k + "Text").toStdString();
                const auto at = std::find_if(m.markers.begin(), m.markers.end(), [id](const auto& x) { return x.id == id; });
                if (at == m.markers.end()) return false; // Orphan data needs the legacy path.
                (cydaw::base::Text(kind) == "Label" ? at->name : at->note) = value;
            }
        }
        if (v.containsKey("NumCueRegions")) {
            m.regions.clear();
            for (unsigned i = 0, n = count("NumCueRegions"); i < n; ++i) {
                const auto k = "CueRegion" + cydaw::base::Text(i);
                cyaudio::AudioRegion region;
                region.markerId = static_cast<std::uint32_t>(number(k + "Identifier"));
                region.lengthFrames = number(k + "SampleLength");
                region.purpose = static_cast<std::uint32_t>(number(k + "Purpose"));
                for (const auto* field : {"Country", "Language", "Dialect", "CodePage"}) {
                    const auto value = number(k + field, cydaw::base::Text(field) == "CodePage" ? 65001 : 0);
                    if (value > UINT16_MAX) return false;
                    if (cydaw::base::Text(field) == "Country") region.country = static_cast<std::uint16_t>(value);
                    else if (cydaw::base::Text(field) == "Language") region.language = static_cast<std::uint16_t>(value);
                    else if (cydaw::base::Text(field) == "Dialect") region.dialect = static_cast<std::uint16_t>(value);
                    else region.codePage = static_cast<std::uint16_t>(value);
                }
                region.text = text(k + "Text").toStdString();
                m.regions.push_back(std::move(region));
            }
        }
        if (v.containsKey("NumSampleLoops")) {
            m.loops.clear();
            for (unsigned i = 0, n = count("NumSampleLoops"); i < n; ++i) {
                const auto k = "Loop" + cydaw::base::Text(i);
                cyaudio::AudioSamplerLoop loop;
                loop.id = static_cast<std::uint32_t>(number(k + "Identifier"));
                const auto mode = number(k + "Type");
                if (mode > 2) return false;
                loop.mode = static_cast<cyaudio::AudioLoopMode>(mode);
                loop.startFrame = number(k + "Start");
                loop.endFrame = number(k + "End") + 1; // Legacy WAV endpoint is inclusive.
                loop.fractionalOffset = static_cast<std::uint32_t>(number(k + "Fraction"));
                loop.playCount = static_cast<std::uint32_t>(number(k + "PlayCount"));
                m.loops.push_back(loop);
            }
        }
        m.unityNote = static_cast<std::uint32_t>(number("MidiUnityNote", m.unityNote));
        m.pitchFraction = static_cast<std::uint32_t>(number("MidiPitchFraction", m.pitchFraction));
        for (const auto& [key, value] : values) {
            if (used.contains(key.toStdString())) continue;
            // Four-character RIFF INFO names accepted by CyAudioCore's public tags API.
            if (key.length() != 4 || !key.startsWithChar('I')) return false;
            m.tags.push_back({key.toStdString(), value.toStdString()});
        }
        return cyaudio::setAudioMetadata(info, m) == cyaudio::Status::ok;
    } catch (const std::invalid_argument&) { return false; }
}

void readTypedMetadata(const cyaudio::AudioFileInfo& info, cydaw::base::TextMap& values) {
    cyaudio::AudioMetadata m;
    if (cyaudio::getAudioMetadata(info, m) != cyaudio::Status::ok || m.container != "WAV") return;
    std::string serialized;
    if (cyaudio::serializeAudioMetadata(m, serialized) == cyaudio::Status::ok)
        values.set("cydsp.metadata", cydaw::base::Text::fromUTF8(serialized.c_str()));
    const auto set = [&](const cydaw::base::Text& key, std::uint64_t n) { values.set(key, cydaw::base::Text(n)); };
    set("NumCuePoints", m.markers.size());
    unsigned labels = 0, notes = 0;
    for (std::size_t i = 0; i < m.markers.size(); ++i) {
        const auto& marker = m.markers[i];
        const auto k = "Cue" + cydaw::base::Text(i);
        set(k + "Identifier", marker.id); set(k + "Offset", marker.frame); set(k + "Order", marker.position);
        if (!marker.name.empty()) {
            const auto label = "CueLabel" + cydaw::base::Text(labels++);
            set(label + "Identifier", marker.id); values.set(label + "Text", cydaw::base::Text::fromUTF8(marker.name.c_str()));
        }
        if (!marker.note.empty()) {
            const auto note = "CueNote" + cydaw::base::Text(notes++);
            set(note + "Identifier", marker.id); values.set(note + "Text", cydaw::base::Text::fromUTF8(marker.note.c_str()));
        }
    }
    set("NumCueLabels", labels); set("NumCueNotes", notes); set("NumCueRegions", m.regions.size());
    for (std::size_t i = 0; i < m.regions.size(); ++i) {
        const auto& r = m.regions[i]; const auto k = "CueRegion" + cydaw::base::Text(i);
        set(k + "Identifier", r.markerId); set(k + "SampleLength", r.lengthFrames); set(k + "Purpose", r.purpose);
        set(k + "Country", r.country); set(k + "Language", r.language); set(k + "Dialect", r.dialect); set(k + "CodePage", r.codePage);
        values.set(k + "Text", cydaw::base::Text::fromUTF8(r.text.c_str()));
    }
    // An instrument-only WAV may have an inst base note outside the typed smpl
    // contract. Preserve the supplemental parser's value in that case.
    if (values.containsKey("NumSampleLoops") || !m.loops.empty() || !values.containsKey("MidiUnityNote")) {
        set("MidiUnityNote", m.unityNote); set("MidiPitchFraction", m.pitchFraction);
    }
    set("NumSampleLoops", m.loops.size());
    for (std::size_t i = 0; i < m.loops.size(); ++i) {
        const auto& loop = m.loops[i]; const auto k = "Loop" + cydaw::base::Text(i);
        set(k + "Identifier", loop.id); set(k + "Type", static_cast<unsigned>(loop.mode));
        set(k + "Start", loop.startFrame); set(k + "End", loop.endFrame - 1);
        set(k + "Fraction", loop.fractionalOffset); set(k + "PlayCount", loop.playCount);
    }
    for (const auto& tag : m.tags) values.set(cydaw::base::Text::fromUTF8(tag.key.c_str()), cydaw::base::Text::fromUTF8(tag.value.c_str()));
}

unsigned bits(cyaudio::AudioEncoding encoding) {
    switch (encoding) {
        case cyaudio::AudioEncoding::pcm8: return 8;
        case cyaudio::AudioEncoding::pcm16: return 16;
        case cyaudio::AudioEncoding::pcm24: return 24;
        case cyaudio::AudioEncoding::pcm32:
        case cyaudio::AudioEncoding::float32: return 32;
        case cyaudio::AudioEncoding::float64: return 64;
    }
    return 0;
}

class Reader final : public cydaw::audio::AudioFormatReader {
public:
    explicit Reader(cydaw::base::InputStream* stream)
        : cydaw::audio::AudioFormatReader(stream, "CyAudioCore"), bridge_(*stream),
          stream_(std::make_shared<std::istream>(&bridge_)) {}

    bool open() {
        input->setPosition(0);
        cyaudio::Status status;
        status = reader_.open(stream_, chunkFrames);
        if (status != cyaudio::Status::ok) return false;
        const auto& info = *reader_.info();
        if (info.frames > static_cast<std::uint64_t>(std::numeric_limits<cydaw::base::int64>::max())) return false;
        sampleRate = info.sampleRate;
        numChannels = static_cast<unsigned>(info.channels);
        lengthInSamples = static_cast<cydaw::base::int64>(info.frames);
        bitsPerSample = bits(info.encoding);
        readTypedMetadata(info, metadataValues);
        metadataValues.set("cydsp.format", cydaw::base::Text::fromUTF8(info.formatName.c_str()));
        usesFloatingPointData = true; // All sample reads cross the bridge as normalized float.
        return scratch_.resize(info.channels, chunkFrames) == cyaudio::Status::ok;
    }

    bool readSamples(int* const* dest, int destChannels, int offset, cydaw::base::int64 first, int count) override {
        if (!dest || count < 0 || offset < 0) return false;
        int done = 0;
        if (first < 0) {
            const auto silence = first <= -static_cast<cydaw::base::int64>(count) ? count : static_cast<int>(-first);
            for (int ch = 0; ch < destChannels; ++ch)
                if (dest[ch]) std::fill_n(reinterpret_cast<float*>(dest[ch]) + offset, silence, 0.0f);
            done += silence;
        }
        while (done < count) {
            const auto n = std::min(chunkFrames, static_cast<std::size_t>(count - done));
            auto block = scratch_.block(n);
            for(std::size_t ch=0;ch<block.channels();++ch)std::fill_n(block.channel(ch).data(),n,0.f);
            std::size_t read = 0;
            const auto status = reader_.read(static_cast<std::uint64_t>(first) + (done ? done : 0), block, read);
            if (status != cyaudio::Status::ok && status != cyaudio::Status::end_of_stream) return false;
            for (int ch = 0; ch < destChannels; ++ch) {
                if (!dest[ch]) continue;
                auto* out = reinterpret_cast<float*>(dest[ch]) + offset + done;
                if (ch < static_cast<int>(numChannels)) std::copy_n(block.channel(static_cast<std::size_t>(ch)).data(), n, out);
                else std::fill_n(out, n, 0.0f);
            }
            done += static_cast<int>(n);
        }
        return true;
    }
private:
    class BorrowedInput final : public cydaw::base::InputStream {
    public:
        explicit BorrowedInput(cydaw::base::InputStream& s) : stream(s) {}
        cydaw::base::int64 getTotalLength() override { return stream.getTotalLength(); }
        int read(void* d, int n) override { return stream.read(d, n); }
        bool isExhausted() override { return stream.isExhausted(); }
        cydaw::base::int64 getPosition() override { return stream.getPosition(); }
        bool setPosition(cydaw::base::int64 p) override { return stream.setPosition(p); }
    private: cydaw::base::InputStream& stream;
    };
    InputBuffer bridge_;
    std::shared_ptr<std::istream> stream_;
    cyaudio::AudioFileReader reader_;
    cyaudio::AudioBuffer<float> scratch_;
};

class OutputBuffer final : public std::streambuf {
public:
    explicit OutputBuffer(cydaw::base::OutputStream& stream) : stream_(stream), length_(stream.getPosition()) {}
    bool failed() const noexcept { return failed_; }
private:
    std::streamsize xsputn(const char* data, std::streamsize n) override {
        if (n < 0 || !stream_.write(data, static_cast<std::size_t>(n))) { failed_ = true; return 0; }
        length_ = std::max(length_, stream_.getPosition());
        return n;
    }
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        const auto value = traits_type::to_char_type(c);
        return xsputn(&value, 1) == 1 ? c : traits_type::eof();
    }
    int sync() override {
        stream_.flush();
        if (auto* file = dynamic_cast<cydaw::base::FileOutputStream*>(&stream_); file && file->getStatus().failed()) failed_ = true;
        return failed_ ? -1 : 0;
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode) override {
        const auto base = way == std::ios_base::beg ? cydaw::base::int64(0)
                        : way == std::ios_base::cur ? stream_.getPosition() : length_;
        if (base < 0 || (offset > 0 && base > std::numeric_limits<cydaw::base::int64>::max() - offset)
            || offset < -base) { failed_ = true; return pos_type(off_type(-1)); }
        const auto at = base + offset;
        if (!stream_.setPosition(at)) { failed_ = true; return pos_type(off_type(-1)); }
        return pos_type(at);
    }
    pos_type seekpos(pos_type at, std::ios_base::openmode mode) override {
        return seekoff(off_type(at), std::ios_base::beg, mode);
    }
    cydaw::base::OutputStream& stream_;
    cydaw::base::int64 length_ = 0;
    bool failed_ = false;
};

const char* writerName(cyaudio::AudioFileFormat format) {
    switch (format) {
        case cyaudio::AudioFileFormat::aiff: return "CyAudioCore AIFF";
        case cyaudio::AudioFileFormat::flac: return "CyAudioCore FLAC";
        case cyaudio::AudioFileFormat::oggVorbis: return "CyAudioCore Vorbis";
        default: return "CyAudioCore WAV";
    }
}
class Writer final : public cydaw::audio::AudioFormatWriter {
public:
    Writer(cydaw::base::OutputStream& stream, const cydaw::audio::AudioFormatWriterOptions& options,
           cyaudio::AudioFileFormat format = cyaudio::AudioFileFormat::wav, std::string quality = {})
        : cydaw::audio::AudioFormatWriter(nullptr, writerName(format), options.getSampleRate(),
              static_cast<unsigned>(options.getNumChannels()), static_cast<unsigned>(options.getBitsPerSample())),
          bridge_(stream), stream_(std::make_shared<std::ostream>(&bridge_)), pointers_(numChannels), zero_(chunkFrames, 0.0f), format_(format), quality_(std::move(quality)) {
        usesFloatingPointData = true; // JUCE source adapter passes normalized floats.
    }
    bool open(const cydaw::audio::AudioFormatWriterOptions& options) {
        if (options.getNumChannels() <= 0 || options.getNumChannels() > 256) return false;
        auto encoding = cyaudio::AudioEncoding::pcm16;
        switch (options.getBitsPerSample()) {
            case 8: encoding = cyaudio::AudioEncoding::pcm8; break;
            case 16: break;
            case 24: encoding = cyaudio::AudioEncoding::pcm24; break;
            case 32: encoding = options.getSampleFormat() == cydaw::audio::AudioFormatWriterOptions::SampleFormat::integral
                                   || (format_ != cyaudio::AudioFileFormat::wav && options.getSampleFormat() != cydaw::audio::AudioFormatWriterOptions::SampleFormat::floatingPoint)
                                   ? cyaudio::AudioEncoding::pcm32 : cyaudio::AudioEncoding::float32; break;
            default: return false;
        }
        cyaudio::AudioFileInfo info;
        info.sampleRate = options.getSampleRate(); info.channels = numChannels; info.encoding = encoding;
        if (format_ == cyaudio::AudioFileFormat::oggVorbis) info.encoding = cyaudio::AudioEncoding::float32;
        if (format_ == cyaudio::AudioFileFormat::wav) {
            info.formatName = "WAV";
            if (!writeMetadata(options.getMetadataValues(), info)) return false;
        } else if (!options.getMetadataValues().empty()) return false;
        return writer_.open(stream_, format_, info, quality_, chunkFrames) == cyaudio::Status::ok;
    }
    void takeStream(cydaw::base::OutputStream* stream) { output = stream; }
    ~Writer() override { (void) finish(); }
    bool finish() override {
        if (finished_) return finishResult_;
        finished_ = true;
        const auto status = writer_.close();
        stream_->flush();
        auto* file = dynamic_cast<cydaw::base::FileOutputStream*>(output);
        const bool streamOk = !file || file->close().wasOk();
        finishResult_ = status == cyaudio::Status::ok && *stream_ && !bridge_.failed() && streamOk;
        return finishResult_;
    }
    bool write(const int** samples, int frames) override {
        if (finished_ || !samples || frames < 0) return false;
        unsigned provided = 0;
        while (provided < numChannels && samples[provided]) ++provided;
        for (int offset = 0; offset < frames;) {
            const auto count = std::min(chunkFrames, static_cast<std::size_t>(frames - offset));
            for (unsigned ch = 0; ch < numChannels; ++ch)
                pointers_[ch] = ch < provided ? reinterpret_cast<const float*>(samples[ch]) + offset : zero_.data();
            if (writer_.write({pointers_.data(), numChannels, count}) != cyaudio::Status::ok || bridge_.failed()) return false;
            offset += static_cast<int>(count);
        }
        return true;
    }
    bool flush() override {
        if (finished_) return finishResult_;
        const auto status = writer_.flush();
        stream_->flush();
        return status == cyaudio::Status::ok && *stream_ && !bridge_.failed();
    }
private:
    OutputBuffer bridge_;
    std::shared_ptr<std::ostream> stream_;
    std::vector<const float*> pointers_;
    std::vector<float> zero_;
    cyaudio::AudioFileWriter writer_;
    cyaudio::AudioFileFormat format_;
    std::string quality_;
    bool finished_ = false, finishResult_ = false;
};

class ReadFormat final : public cydaw::audio::AudioFormat {
public:
    ReadFormat() : cydaw::audio::AudioFormat("CyAudioCore", ".wav .bwf .aif .aiff .aifc .caf .flac .ogg .mp3 .m4a .aac .wma") {}
    cydaw::base::Array<int> getPossibleSampleRates() override { return {}; }
    cydaw::base::Array<int> getPossibleBitDepths() override { return {8, 16, 24, 32, 64}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    cydaw::audio::AudioFormatReader* createReaderFor(cydaw::base::InputStream* source, bool deleteOnFail) override {
        if (!source) return nullptr;
        auto next = std::make_unique<Reader>(source);
        if (next->open()) return next.release();
        if (!deleteOnFail) next->input = nullptr;
        return nullptr;
    }
    std::unique_ptr<cydaw::audio::AudioFormatWriter> createWriterFor(std::unique_ptr<cydaw::base::OutputStream>&,
            const cydaw::audio::AudioFormatWriterOptions&) override { return {}; }
};
} // namespace
void registerCyAudioCoreReaders(cydaw::audio::AudioFormatManager& manager) {
    manager.clearFormats();
    manager.registerFormat(new ReadFormat, true);
}
std::unique_ptr<cydaw::audio::AudioFormatWriter> WaveFormat::createWriterFor(
        std::unique_ptr<cydaw::base::OutputStream>& stream, const cydaw::audio::AudioFormatWriterOptions& options) {
    cyaudio::AudioFileInfo metadataCheck;
    metadataCheck.formatName = "WAV";
    if (!writeMetadata(options.getMetadataValues(), metadataCheck)) {
        return {}; // Unsupported metadata must fail visibly, never be discarded.
    }
    if (!stream || options.getNumChannels() <= 0 || options.getNumChannels() > 256
        || !std::isfinite(options.getSampleRate()) || options.getSampleRate() <= 0)
        return {};
    auto next = std::make_unique<Writer>(*stream, options);
    if (!next->open(options)) return {};
    next->takeStream(stream.release());
    return next;
}
FileFormat::FileFormat(cyaudio::AudioFileFormat format)
    : cydaw::audio::AudioFormat(writerName(format), format == cyaudio::AudioFileFormat::aiff ? ".aif .aiff" : format == cyaudio::AudioFileFormat::flac ? ".flac" : ".ogg"), format_(format) {}
cydaw::base::Array<int> FileFormat::getPossibleSampleRates() { return {}; }
cydaw::base::Array<int> FileFormat::getPossibleBitDepths() {
    cydaw::base::Array<int> result;
    for (const auto& capabilities : cyaudio::audioFileFormats()) if (capabilities.format == format_)
        for (const auto encoding : capabilities.writableEncodings) result.addIfNotAlreadyThere(static_cast<int>(bits(encoding)));
    return result;
}
cydaw::base::TextList FileFormat::getQualityOptions() {
    cydaw::base::TextList result;
    for (const auto& capabilities : cyaudio::audioFileFormats()) if (capabilities.format == format_)
        for (const auto& quality : capabilities.qualityLevels) result.add(cydaw::base::Text::fromUTF8(quality.c_str()));
    return result;
}
cydaw::audio::AudioFormatReader* FileFormat::createReaderFor(cydaw::base::InputStream* stream, bool deleteOnFail) {
    if (!stream) return nullptr;
    auto next = std::make_unique<Reader>(stream);
    if (next->open()) return next.release();
    if (!deleteOnFail) next->input = nullptr;
    return nullptr;
}
std::unique_ptr<cydaw::audio::AudioFormatWriter> FileFormat::createWriterFor(std::unique_ptr<cydaw::base::OutputStream>& stream,
        const cydaw::audio::AudioFormatWriterOptions& options) {
    if (!stream || options.getNumChannels() <= 0 || options.getNumChannels() > 256
        || !std::isfinite(options.getSampleRate()) || options.getSampleRate() <= 0) return {};
    const auto qualities = getQualityOptions();
    const auto index = options.getQualityOptionIndex();
    if (index < 0 || (!qualities.isEmpty() && index >= qualities.size())) return {};
    const auto quality = qualities.isEmpty() ? std::string() : qualities[index].toStdString();
    auto next = std::make_unique<Writer>(*stream, options, format_, quality);
    if (!next->open(options)) return {};
    next->takeStream(stream.release());
    return next;
}
} // namespace cydaw::integration

namespace cydaw::audio {void AudioFormatManager::registerBasicFormats(){cydaw::integration::registerCyAudioCoreReaders(*this);}
base::Text AudioFormatManager::getWildcardForAllFormats()const{base::TextList extensions;for(auto& f:formats_)for(auto& e:f->getFileExtensions())if(e.isNotEmpty())extensions.addIfNotAlreadyThere("*."+e);return extensions.joinIntoString(";");}}
namespace cydaw::integration {audio::AudioFormatReader* WaveFormat::createReaderFor(base::InputStream* input,bool deleteOnFail){FileFormat reader(cyaudio::AudioFileFormat::wav);return reader.createReaderFor(input,deleteOnFail);}}
