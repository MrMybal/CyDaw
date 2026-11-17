#pragma once
#include <atomic>
#include <condition_variable>
#include <cydaw/base/File.h>
#include <cydaw/base/TextMap.h>
#include <mutex>
#include <unordered_map>
namespace cydaw::audio {
using base::Array;
using base::int64;
using base::Text;
using base::TextList;
using base::TextMap;
class AudioFormatReader {
  Text formatName_;

public:
  base::InputStream *input = nullptr;
  double sampleRate = 0;
  unsigned numChannels = 0, bitsPerSample = 0;
  int64 lengthInSamples = 0;
  bool usesFloatingPointData = true;
  TextMap metadataValues;
  AudioFormatReader(base::InputStream *in, Text format)
      : formatName_(std::move(format)), input(in) {}
  virtual ~AudioFormatReader() { delete input; }
  const Text &getFormatName() const { return formatName_; }
  template <class Range>
  void readMaxLevels(int64 start, int64 count, Range *levels, int channels) {
    if (!levels || channels <= 0 || channels > 256)
      return;
    std::vector<std::vector<float>> scratch(channels, std::vector<float>(4096));
    std::vector<float *> pointers;
    for (auto &ch : scratch)
      pointers.push_back(ch.data());
    std::vector<float> low(channels, 0), high(channels, 0);
    for (int64 at = 0; at < count; at += 4096) {
      auto n = int(std::min<int64>(4096, count - at));
      if (!read(pointers.data(), channels, start + at, n))
        break;
      for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i) {
          low[ch] = std::min(low[ch], scratch[ch][i]);
          high[ch] = std::max(high[ch], scratch[ch][i]);
        }
    }
    for (int ch = 0; ch < channels; ++ch)
      levels[ch] = Range(low[ch], high[ch]);
  }
  virtual bool readSamples(int *const *, int channels, int destinationOffset,
                           int64 sourceStart, int frames) = 0;
  bool read(float *const *destination, int channels, int64 start, int count) {
    return readSamples(reinterpret_cast<int *const *>(destination), channels, 0,
                       start, count);
  }
  template <class Buffer>
    requires requires(Buffer *b) {
      b->getNumSamples();
      b->getNumChannels();
    }
  bool read(Buffer *destination, int offset, int count, int64 first,
            bool left = true, bool right = true) {
    if (!destination || offset < 0 || count < 0 ||
        offset + count > destination->getNumSamples())
      return false;
    std::array<float *, 256> pointers{};
    int n = std::min(256, destination->getNumChannels());
    for (int ch = 0; ch < n; ++ch)
      pointers[ch] = destination->getWritePointer(ch);
    bool ok = readSamples(reinterpret_cast<int *const *>(pointers.data()), n,
                          offset, first, count);
    if (!left && n > 0)
      std::fill_n(pointers[0] + offset, count, 0.f);
    if (!right && n > 1)
      std::fill_n(pointers[1] + offset, count, 0.f);
    return ok;
  }
};
class AudioFormatWriterOptions {
public:
  enum class SampleFormat { automatic, integral, floatingPoint };

private:
  double rate_ = 44100;
  int channels_ = 2, bits_ = 16, quality_ = 0;
  SampleFormat format_ = SampleFormat::automatic;
  std::unordered_map<Text, Text> metadata_;

public:
  auto withSampleRate(double value) const {
    auto copy = *this;
    copy.rate_ = value;
    return copy;
  }
  auto withNumChannels(int value) const {
    auto copy = *this;
    copy.channels_ = value;
    return copy;
  }
  auto withBitsPerSample(int value) const {
    auto copy = *this;
    copy.bits_ = value;
    return copy;
  }
  auto withQualityOptionIndex(int value) const {
    auto copy = *this;
    copy.quality_ = value;
    return copy;
  }
  auto withSampleFormat(SampleFormat value) const {
    auto copy = *this;
    copy.format_ = value;
    return copy;
  }
  auto withMetadata(Text key, Text value) const {
    auto copy = *this;
    copy.metadata_.insert_or_assign(std::move(key), std::move(value));
    return copy;
  }
  double getSampleRate() const { return rate_; }
  int getNumChannels() const { return channels_; }
  int getBitsPerSample() const { return bits_; }
  int getQualityOptionIndex() const { return quality_; }
  SampleFormat getSampleFormat() const { return format_; }
  const auto &getMetadataValues() const { return metadata_; }
};
class AudioFormatWriter {
public:
  class ThreadedWriter;
  base::OutputStream *output = nullptr;
  Text formatName;
  double sampleRate;
  unsigned numChannels, bitsPerSample;
  bool usesFloatingPointData = true;
  AudioFormatWriter(base::OutputStream *stream, Text format, double rate,
                    unsigned channels, unsigned bits)
      : output(stream), formatName(std::move(format)), sampleRate(rate),
        numChannels(channels), bitsPerSample(bits) {}
  virtual ~AudioFormatWriter() { delete output; }
  virtual bool write(const int **channels, int frames) = 0;
  virtual bool flush() { return true; }
  // Control/worker only. Finalise the underlying stream and report close errors.
  virtual bool finish() { return flush(); }
  bool writeFromFloatArrays(const float *const *channels, int n, int frames) {
    if (n < 0 || n > 256)
      return false;
    std::array<const int *, 257> pointers{};
    for (int i = 0; i < std::min(n, int(numChannels)); ++i)
      pointers[i] = reinterpret_cast<const int *>(channels[i]);
    return write(pointers.data(), frames);
  }
  template <class Buffer>
  bool writeFromAudioSampleBuffer(const Buffer &buffer, int start, int count) {
    if (start < 0 || count < 0 || start + count > buffer.getNumSamples())
      return false;
    std::array<const float *, 256> pointers{};
    int n = std::min(256, buffer.getNumChannels());
    for (int i = 0; i < n; ++i)
      pointers[i] = buffer.getReadPointer(i, start);
    return writeFromFloatArrays(pointers.data(), n, count);
  }
};
// Bounded SPSC recorder queue; allocation and file IO stay on the
// control/worker side.
class AudioFormatWriter::ThreadedWriter {
public:
  struct Result {
    std::uint64_t submittedFrames = 0, acceptedFrames = 0, writtenFrames = 0;
    std::uint64_t rejectedFrames = 0, rejectedBlocks = 0;
    bool failed = false;
    // writtenFrames counts frames confirmed by successful sink writes; a failed
    // write may have written a partial block and is never counted as confirmed.
    bool complete() const noexcept {
      return !failed && rejectedFrames == 0 && acceptedFrames == writtenFrames;
    }
  };

private:
  std::unique_ptr<AudioFormatWriter> writer_;
  std::vector<float> ring_, scratch_;
  std::vector<const float *> pointers_;
  std::size_t capacity_;
  std::atomic<uint64_t> written_{0}, read_{0};
  std::atomic<bool> stopping_{false}, failed_{false};
  std::atomic<std::uint64_t> submitted_{0}, rejected_{0}, rejectedBlocks_{0};
  std::uint64_t confirmedWritten_ = 0; // Worker-owned; read only after join.
  std::thread worker_;
  void run() {
    while (!stopping_.load(std::memory_order_acquire) ||
           read_.load() != written_.load()) {
      auto r = read_.load(std::memory_order_relaxed),
           w = written_.load(std::memory_order_acquire);
      auto n = std::min<std::size_t>(4096, w - r);
      if (!n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      for (unsigned ch = 0; ch < writer_->numChannels; ++ch) {
        auto *out = scratch_.data() + ch * 4096;
        for (std::size_t i = 0; i < n; ++i)
          out[i] = ring_[ch * capacity_ + (r + i) % capacity_];
        pointers_[ch] = out;
      }
      if (!failed_.load(std::memory_order_relaxed)) {
        try {
          if (writer_->writeFromFloatArrays(pointers_.data(),
                                           int(pointers_.size()), int(n)))
            confirmedWritten_ += n;
          else
            failed_.store(true, std::memory_order_release);
        } catch (...) {
          failed_.store(true, std::memory_order_release);
        }
      }
      read_.store(r + n, std::memory_order_release);
    }
    try {
      if (!writer_->finish())
        failed_.store(true, std::memory_order_release);
    } catch (...) {
      failed_.store(true, std::memory_order_release);
    }
  }

public:
  ThreadedWriter(AudioFormatWriter *writer, int samples)
      : writer_(writer),
        ring_(writer->numChannels * std::size_t(std::max(4096, samples))),
        scratch_(writer->numChannels * 4096), pointers_(writer->numChannels),
        capacity_(std::size_t(std::max(4096, samples))),
        worker_([this] { run(); }) {}
  ~ThreadedWriter() { (void)finish(); }
  // Single control owner, producer already quiescent. Never call in the audio
  // callback: drains, joins, closes and returns the final sticky sink outcome.
  Result finish() {
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable())
      worker_.join();
    return {submitted_.load(), written_.load(), confirmedWritten_,
            rejected_.load(), rejectedBlocks_.load(), failed_.load()};
  }
  bool write(const float *const *channels, int frames) {
    if (frames < 0 || stopping_.load(std::memory_order_acquire))
      return false;
    submitted_.fetch_add(std::uint64_t(frames), std::memory_order_relaxed);
    auto w = written_.load(std::memory_order_relaxed),
         r = read_.load(std::memory_order_acquire);
    if (failed_.load(std::memory_order_acquire) ||
        std::size_t(frames) > capacity_ - (w - r)) {
      rejected_.fetch_add(std::uint64_t(frames), std::memory_order_relaxed);
      if (frames > 0)
        rejectedBlocks_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    for (unsigned ch = 0; ch < writer_->numChannels; ++ch)
      for (int i = 0; i < frames; ++i)
        ring_[ch * capacity_ + (w + std::size_t(i)) % capacity_] =
            channels && channels[ch] ? channels[ch][i] : 0.f;
    written_.store(w + frames, std::memory_order_release);
    return true;
  }
};
class AudioFormat {
  Text name_, extensions_;

public:
  AudioFormat(Text name, Text extensions)
      : name_(std::move(name)), extensions_(std::move(extensions)) {}
  virtual ~AudioFormat() = default;
  const Text &getFormatName() const { return name_; }
  TextList getFileExtensions() const {
    return TextList::fromTokens(extensions_, " .;", {});
  }
  virtual Array<int> getPossibleSampleRates() { return {}; }
  virtual Array<int> getPossibleBitDepths() { return {}; }
  virtual TextList getQualityOptions() { return {}; }
  virtual bool canDoStereo() { return true; }
  virtual bool canDoMono() { return true; }
  virtual AudioFormatReader *createReaderFor(base::InputStream *, bool) = 0;
  virtual std::unique_ptr<AudioFormatWriter>
  createWriterFor(std::unique_ptr<base::OutputStream> &,
                  const AudioFormatWriterOptions &) = 0;
  AudioFormatWriter *createWriterFor(base::OutputStream *stream, double rate,
                                     unsigned channels, int bits,
                                     const TextMap &metadata, int quality) {
    std::unique_ptr<base::OutputStream> own(stream);
    auto options = AudioFormatWriterOptions()
                       .withSampleRate(rate)
                       .withNumChannels(int(channels))
                       .withBitsPerSample(bits)
                       .withQualityOptionIndex(quality);
    for (auto &[key, value] : metadata)
      options = options.withMetadata(key, value);
    auto writer = createWriterFor(own, options);
    if (!writer)
      own.release();
    return writer.release();
  }
};
class AudioFormatManager {
  std::vector<std::unique_ptr<AudioFormat>> formats_;

public:
  void clearFormats() { formats_.clear(); }
  void registerFormat(AudioFormat *format, bool = false) {
    formats_.emplace_back(format);
  }
  void registerBasicFormats();
  AudioFormatReader *createReaderFor(const base::File &file) {
    return createReaderFor(file.createInputStream()).release();
  }
  std::unique_ptr<AudioFormatReader>
  createReaderFor(std::unique_ptr<base::InputStream> stream) {
    if (!stream)
      return {};
    for (auto &format : formats_) {
      stream->setPosition(0);
      if (auto *reader = format->createReaderFor(stream.get(), false)) {
        stream.release();
        return std::unique_ptr<AudioFormatReader>(reader);
      }
    }
    return {};
  }
  Text getWildcardForAllFormats() const;
  int getNumKnownFormats() const { return int(formats_.size()); }
  AudioFormat *getKnownFormat(int i) const {
    return i >= 0 && i < int(formats_.size()) ? formats_[i].get() : nullptr;
  }
};
} // namespace cydaw::audio
