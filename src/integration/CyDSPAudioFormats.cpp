#include <cydaw/integration/CyDSPAudioFormats.h>
#include <cydsp/io/AudioFile.h>
#include <cydsp/core/AudioBuffer.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <istream>
#include <ostream>
#include <limits>
#include <streambuf>

namespace cydaw::integration {
namespace {
constexpr std::size_t chunkFrames = 4096;

// Borrowed seekable stream. Reader's base owns it and destroys it last.
class InputBuffer final : public std::streambuf {
public:
    explicit InputBuffer(juce::InputStream& stream) : stream_(stream) {}
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
        const auto base = way == std::ios_base::beg ? juce::int64(0)
                        : way == std::ios_base::cur ? stream_.getPosition() : stream_.getTotalLength();
        if (base < 0 || (offset > 0 && base > std::numeric_limits<juce::int64>::max() - offset)
            || offset < -base) return pos_type(off_type(-1));
        const auto at = base + offset;
        return stream_.setPosition(at) ? pos_type(at) : pos_type(off_type(-1));
    }
    pos_type seekpos(pos_type at, std::ios_base::openmode mode) override {
        return seekoff(off_type(at), std::ios_base::beg, mode);
    }
    juce::InputStream& stream_;
};

unsigned bits(cydsp::AudioEncoding encoding) {
    switch (encoding) {
        case cydsp::AudioEncoding::pcm8: return 8;
        case cydsp::AudioEncoding::pcm16: return 16;
        case cydsp::AudioEncoding::pcm24: return 24;
        case cydsp::AudioEncoding::pcm32:
        case cydsp::AudioEncoding::float32: return 32;
        case cydsp::AudioEncoding::float64: return 64;
    }
    return 0;
}

class Reader final : public juce::AudioFormatReader {
public:
    explicit Reader(juce::InputStream* stream)
        : juce::AudioFormatReader(stream, "CyDSP"), bridge_(*stream),
          stream_(std::make_shared<std::istream>(&bridge_)) {}

    bool open() {
        // Preserve the established application's metadata vocabulary. This is
        // intentionally NOT a decoder fallback: only CyDSP reads sample data.
        juce::AudioFormatManager metadataFormats;
        metadataFormats.registerBasicFormats();
        input->setPosition(0);
        if (std::unique_ptr<juce::AudioFormatReader> metadata(metadataFormats.createReaderFor(
                std::unique_ptr<juce::InputStream>(new BorrowedInput(*input)))); metadata) {
            metadataValues = metadata->metadataValues;
        }
        input->setPosition(0);
        cydsp::Status status;
        if (auto* file = dynamic_cast<juce::FileInputStream*>(input)) {
#if JUCE_WINDOWS
            status = reader_.open(std::filesystem::path(file->getFile().getFullPathName().toWideCharPointer()), chunkFrames);
#else
            status = reader_.open(std::filesystem::path(file->getFile().getFullPathName().toStdString()), chunkFrames);
#endif
        } else {
            status = reader_.open(stream_, chunkFrames);
        }
        if (status != cydsp::Status::ok) return false;
        const auto& info = *reader_.info();
        if (info.frames > static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max())) return false;
        sampleRate = info.sampleRate;
        numChannels = static_cast<unsigned>(info.channels);
        lengthInSamples = static_cast<juce::int64>(info.frames);
        bitsPerSample = bits(info.encoding);
        metadataValues.set("cydsp.format", juce::String::fromUTF8(info.formatName.c_str()));
        usesFloatingPointData = true; // All sample reads cross the bridge as normalized float.
        return scratch_.resize(info.channels, chunkFrames) == cydsp::Status::ok;
    }

    bool readSamples(int* const* dest, int destChannels, int offset, juce::int64 first, int count) override {
        if (!dest || count < 0 || offset < 0) return false;
        int done = 0;
        if (first < 0) {
            const auto silence = first <= -static_cast<juce::int64>(count) ? count : static_cast<int>(-first);
            for (int ch = 0; ch < destChannels; ++ch)
                if (dest[ch]) std::fill_n(reinterpret_cast<float*>(dest[ch]) + offset, silence, 0.0f);
            done += silence;
        }
        while (done < count) {
            const auto n = std::min(chunkFrames, static_cast<std::size_t>(count - done));
            auto block = scratch_.block(n);
            std::size_t read = 0;
            const auto status = reader_.read(static_cast<std::uint64_t>(first) + (done ? done : 0), block, read);
            if (status != cydsp::Status::ok && status != cydsp::Status::end_of_stream) return false;
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
    class BorrowedInput final : public juce::InputStream {
    public:
        explicit BorrowedInput(juce::InputStream& s) : stream(s) {}
        juce::int64 getTotalLength() override { return stream.getTotalLength(); }
        int read(void* d, int n) override { return stream.read(d, n); }
        bool isExhausted() override { return stream.isExhausted(); }
        juce::int64 getPosition() override { return stream.getPosition(); }
        bool setPosition(juce::int64 p) override { return stream.setPosition(p); }
    private: juce::InputStream& stream;
    };
    InputBuffer bridge_;
    std::shared_ptr<std::istream> stream_;
    cydsp::AudioFileReader reader_;
    cydsp::AudioBuffer<float> scratch_;
};

class OutputBuffer final : public std::streambuf {
public:
    explicit OutputBuffer(juce::OutputStream& stream) : stream_(stream), length_(stream.getPosition()) {}
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
        if (auto* file = dynamic_cast<juce::FileOutputStream*>(&stream_); file && file->getStatus().failed()) failed_ = true;
        return failed_ ? -1 : 0;
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode) override {
        const auto base = way == std::ios_base::beg ? juce::int64(0)
                        : way == std::ios_base::cur ? stream_.getPosition() : length_;
        if (base < 0 || (offset > 0 && base > std::numeric_limits<juce::int64>::max() - offset)
            || offset < -base) { failed_ = true; return pos_type(off_type(-1)); }
        const auto at = base + offset;
        if (!stream_.setPosition(at)) { failed_ = true; return pos_type(off_type(-1)); }
        return pos_type(at);
    }
    pos_type seekpos(pos_type at, std::ios_base::openmode mode) override {
        return seekoff(off_type(at), std::ios_base::beg, mode);
    }
    juce::OutputStream& stream_;
    juce::int64 length_ = 0;
    bool failed_ = false;
};

class Writer final : public juce::AudioFormatWriter {
public:
    Writer(juce::OutputStream& stream, const juce::AudioFormatWriterOptions& options)
        : juce::AudioFormatWriter(nullptr, "CyDSP WAV", options.getSampleRate(),
              static_cast<unsigned>(options.getNumChannels()), static_cast<unsigned>(options.getBitsPerSample())),
          bridge_(stream), stream_(std::make_shared<std::ostream>(&bridge_)), pointers_(numChannels), zero_(chunkFrames, 0.0f) {
        usesFloatingPointData = true; // JUCE source adapter passes normalized floats.
    }
    bool open(const juce::AudioFormatWriterOptions& options) {
        if (options.getNumChannels() <= 0 || options.getNumChannels() > 256) return false;
        auto encoding = cydsp::AudioEncoding::pcm16;
        switch (options.getBitsPerSample()) {
            case 8: encoding = cydsp::AudioEncoding::pcm8; break;
            case 16: break;
            case 24: encoding = cydsp::AudioEncoding::pcm24; break;
            case 32: encoding = options.getSampleFormat() == juce::AudioFormatWriterOptions::SampleFormat::integral
                                   ? cydsp::AudioEncoding::pcm32 : cydsp::AudioEncoding::float32; break;
            default: return false;
        }
        cydsp::AudioFileInfo info;
        info.sampleRate = options.getSampleRate(); info.channels = numChannels; info.encoding = encoding;
        return writer_.open(stream_, cydsp::AudioFileFormat::wav, info, {}, chunkFrames) == cydsp::Status::ok;
    }
    void takeStream(juce::OutputStream* stream) { output = stream; }
    ~Writer() override { (void) writer_.close(); }
    bool write(const int** samples, int frames) override {
        if (!samples || frames < 0) return false;
        unsigned provided = 0;
        while (provided < numChannels && samples[provided]) ++provided;
        for (int offset = 0; offset < frames;) {
            const auto count = std::min(chunkFrames, static_cast<std::size_t>(frames - offset));
            for (unsigned ch = 0; ch < numChannels; ++ch)
                pointers_[ch] = ch < provided ? reinterpret_cast<const float*>(samples[ch]) + offset : zero_.data();
            if (writer_.write({pointers_.data(), numChannels, count}) != cydsp::Status::ok || bridge_.failed()) return false;
            offset += static_cast<int>(count);
        }
        return true;
    }
    bool flush() override {
        const auto status = writer_.flush();
        stream_->flush();
        return status == cydsp::Status::ok && *stream_ && !bridge_.failed();
    }
private:
    OutputBuffer bridge_;
    std::shared_ptr<std::ostream> stream_;
    std::vector<const float*> pointers_;
    std::vector<float> zero_;
    cydsp::AudioFileWriter writer_;
};

class ReadFormat final : public juce::AudioFormat {
public:
    ReadFormat() : juce::AudioFormat("CyDSP", ".wav .bwf .aif .aiff .aifc .caf .flac .ogg .mp3 .m4a .aac .wma") {}
    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {8, 16, 24, 32, 64}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    juce::AudioFormatReader* createReaderFor(juce::InputStream* source, bool deleteOnFail) override {
        if (!source) return nullptr;
        auto next = std::make_unique<Reader>(source);
        if (next->open()) return next.release();
        if (!deleteOnFail) next->input = nullptr;
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(std::unique_ptr<juce::OutputStream>&,
            const juce::AudioFormatWriterOptions&) override { return {}; }
};
} // namespace
void registerCyDSPReaders(juce::AudioFormatManager& manager) {
    manager.clearFormats();
    manager.registerFormat(new ReadFormat, true);
}
std::unique_ptr<juce::AudioFormatWriter> WaveFormat::createWriterFor(
        std::unique_ptr<juce::OutputStream>& stream, const juce::AudioFormatWriterOptions& options) {
    if (!options.getMetadataValues().empty())
        return juce::WavAudioFormat::createWriterFor(stream, options);
    if (!stream || options.getNumChannels() <= 0 || options.getNumChannels() > 256
        || !std::isfinite(options.getSampleRate()) || options.getSampleRate() <= 0)
        return {};
    auto next = std::make_unique<Writer>(*stream, options);
    if (!next->open(options)) return {};
    next->takeStream(stream.release());
    return next;
}
} // namespace cydaw::integration
