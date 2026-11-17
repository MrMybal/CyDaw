#include <array>
#include <cyaudio/io/AudioFile.h>
#include <cyaudio/io/AudioMetadata.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>
using namespace cydaw;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
namespace {
struct SinkState {
  std::atomic<bool> entered{false}, release{false};
  int mode = 0, finishes = 0;
  std::uint64_t confirmed = 0;
  std::array<std::array<float, 4096>, 2> samples{};
};
class TestSink final : public audio::AudioFormatWriter {
  SinkState& state_;
public:
  explicit TestSink(SinkState& s) : AudioFormatWriter(nullptr, {}, 48000, 2, 32), state_(s) {}
  bool write(const int** input, int frames) override {
    state_.entered.store(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state_.release.load() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    if (!state_.release.load() || state_.mode == 1) return false;
    if (state_.mode == 4) throw std::runtime_error("injected write failure");
    for (unsigned ch = 0; ch < 2; ++ch)
      std::copy_n(reinterpret_cast<const float*>(input[ch]), frames,
                  state_.samples[ch].begin() + state_.confirmed);
    state_.confirmed += std::uint64_t(frames);
    return true;
  }
  bool flush() override { return state_.mode != 2; }
  bool finish() override {
    ++state_.finishes;
    if (state_.mode == 5) throw std::runtime_error("injected finalisation failure");
    return state_.mode != 3 && flush();
  }
};
bool waitForSink(SinkState& state) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!state.entered.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  return state.entered.load();
}
void finalFailuresAreReported() {
  std::array<float, 1024> block{};
  block.fill(.125f);
  const float* channels[]{block.data(), block.data()};
  for (int mode = 0; mode < 6; ++mode) {
    SinkState state;
    state.mode = mode;
    audio::AudioFormatWriter::ThreadedWriter recorder(new TestSink(state), 4096);
    const bool accepted = recorder.write(channels, 1024);
    const bool entered = waitForSink(state);
    state.release.store(true);
    const auto result = recorder.finish();
    require(accepted && entered, "last block accepted before the injected worker failure");
    require(result.submittedFrames == 1024 && result.acceptedFrames == 1024 &&
                result.rejectedFrames == 0 && result.rejectedBlocks == 0,
            "no producer rejection hides the final sink outcome");
    require(result.writtenFrames == (mode == 1 || mode == 4 ? 0 : 1024), "confirmed write accounting");
    require(result.failed == (mode != 0) && result.complete() == (mode == 0),
            "write, flush, close and exception failures survive finalisation");
    require(recorder.finish().failed == result.failed && state.finishes == 1, "finalisation is idempotent");
    require(!recorder.write(channels, 1) && recorder.finish().submittedFrames == 1024,
            "closed producer rejects writes without changing the final result");
  }
}
void queueAccountingAndAbsentInputs() {
  SinkState state;
  audio::AudioFormatWriter::ThreadedWriter recorder(new TestSink(state), 4096);
  std::array<float, 1024> block{};
  const float* channels[]{block.data(), block.data()};
  block.fill(.125f);
  const bool first = recorder.write(channels, 1024);
  const bool entered = waitForSink(state);
  bool accepted = true;
  for (int part = 1; part < 4; ++part) {
    block.fill(float(part + 1) / 8);
    accepted = recorder.write(channels, 1024) && accepted;
  }
  const bool overflow = recorder.write(channels, 1024);
  state.release = true;
  auto result = recorder.finish();
  require(first && entered && accepted && !overflow, "deterministic bounded backpressure");
  require(!result.failed && !result.complete() && result.submittedFrames == 5120 &&
              result.acceptedFrames == 4096 && result.writtenFrames == 4096 &&
              result.rejectedFrames == 1024 && result.rejectedBlocks == 1,
          "drain distinguishes rejected and confirmed frames");
  for (unsigned ch = 0; ch < 2; ++ch)
    for (unsigned i = 0; i < 4096; ++i)
      require(state.samples[ch][i] == float(i / 1024 + 1) / 8, "queued samples retain order");
  SinkState absent;
  absent.release = true;
  audio::AudioFormatWriter::ThreadedWriter zeros(new TestSink(absent), 4096);
  require(zeros.write(nullptr, 17), "absent input accepted");
  result = zeros.finish();
  require(result.complete() && result.writtenFrames == 17, "short final block drained");
  for (unsigned ch = 0; ch < 2; ++ch)
    for (unsigned i = 0; i < 17; ++i)
      require(absent.samples[ch][i] == 0, "absent inputs produce silence");
}
struct IoFault { bool write = false, flush = false; };
class FaultOutput final : public base::OutputStream {
  IoFault& fault_;
  std::array<char, 65536> bytes_{};
  base::int64 position_ = 0;
public:
  explicit FaultOutput(IoFault& fault) : fault_(fault) {}
  bool write(const void* data, std::size_t n) override {
    if (fault_.write || n > bytes_.size() - std::size_t(position_)) return false;
    std::memcpy(bytes_.data() + position_, data, n); position_ += base::int64(n); return true;
  }
  base::int64 getPosition() override { return position_; }
  bool setPosition(base::int64 n) override {
    if (n < 0 || std::uint64_t(n) > bytes_.size()) return false;
    position_ = n; return true;
  }
  void flush() override { if (fault_.flush) throw std::runtime_error("injected stream flush failure"); }
};
void nativeCloseErrorsAreReported() {
  for (bool failFlush : {false, true}) {
    IoFault fault;
    std::unique_ptr<base::OutputStream> stream = std::make_unique<FaultOutput>(fault);
    integration::WaveFormat format;
    auto writer = format.createWriterFor(stream, audio::AudioFormatWriterOptions().withNumChannels(2).withBitsPerSample(24));
    require(bool(writer), "open native writer on seekable fault stream");
    std::array<float, 512> block{};
    const float* channels[]{block.data(), block.data()};
    require(writer->writeFromFloatArrays(channels, 2, 512), "native write succeeds before close failure");
    fault.write = !failFlush; fault.flush = failFlush;
    audio::AudioFormatWriter::ThreadedWriter recorder(writer.release(), 4096);
    require(recorder.finish().failed && recorder.finish().failed,
            "native final header/flush error reaches the threaded owner and remains sticky");
  }
}
void longExportIsExact() {
  const auto file = base::File::createTempFile(".wav");
  constexpr std::uint64_t frames = 48000 * 120;
  auto value = [](std::uint64_t i, std::size_t ch) { return float(int((i * 131 + ch * 977) % 524287) - 262143) / 1048576.f; };
  integration::WaveFormat format;
  std::unique_ptr<base::OutputStream> stream = file.createOutputStream();
  auto writer = format.createWriterFor(stream, audio::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
  require(bool(writer), "open long export");
  // Oversized fixture queue isolates data/drain correctness from worker scheduling.
  audio::AudioFormatWriter::ThreadedWriter recorder(writer.release(), int(frames));
  cyaudio::AudioBuffer<float> buffer;
  require(buffer.resize(2, 2048) == cyaudio::Status::ok, "prepare export buffer");
  constexpr std::array<std::size_t, 5> sizes{17, 480, 1024, 7, 2048};
  std::size_t blocks = 0;
  for (std::uint64_t first = 0; first < frames; ++blocks) {
    const auto n = std::min<std::uint64_t>(sizes[blocks % sizes.size()], frames - first);
    for (std::size_t ch = 0; ch < 2; ++ch)
      for (std::size_t i = 0; i < n; ++i) buffer.block().channel(ch)[i] = value(first + i, ch);
    const float* channels[]{buffer.block().channel(0).data(), buffer.block().channel(1).data()};
    require(recorder.write(channels, int(n)), "long export accepts every fixture block");
    first += n;
  }
  const auto result = recorder.finish();
  require(result.complete() && result.submittedFrames == frames && result.acceptedFrames == frames &&
              result.writtenFrames == frames && result.rejectedBlocks == 0, "long export final accounting");
  cyaudio::AudioFileReader reader;
  require(reader.open(std::filesystem::path(file.getFullPathName().toStdString()), 2048) == cyaudio::Status::ok &&
              reader.info()->frames == frames, "long export final header duration");
  for (std::uint64_t first = 0; first < frames;) {
    const auto n = std::size_t(std::min<std::uint64_t>(2048, frames - first));
    std::size_t got = 0;
    require(reader.read(first, buffer.block(n), got) == cyaudio::Status::ok && got == n, "read long export");
    for (std::size_t ch = 0; ch < 2; ++ch)
      for (std::size_t i = 0; i < n; ++i) require(buffer.block().channel(ch)[i] == value(first + i, ch), "long export raw sample equality");
    first += got;
  }
  reader.close();
  require(file.deleteFile(), "remove long export fixture");
}
void nativeFinishClosesTheFile() {
  const auto file = base::File::createTempFile(".wav");
  integration::WaveFormat format;
  std::unique_ptr<base::OutputStream> stream = file.createOutputStream();
  auto writer = format.createWriterFor(stream, audio::AudioFormatWriterOptions());
  require(bool(writer), "open native finish fixture");
  require(writer->finish() && writer->finish(), "native finish is idempotent");
  require(file.deleteFile(), "finish releases the physical file before writer destruction");
}
}
int main() {
  try {
    finalFailuresAreReported();
    queueAccountingAndAbsentInputs();
    nativeCloseErrorsAreReported();
    nativeFinishClosesTheFile();
    longExportIsExact();
    auto file = base::File::createTempFile(".wav");
    std::array<float, 512> left{}, right{};
    for (int i = 0; i < 512; ++i) {
      left[i] = float(i) / 1024;
      right[i] = -left[i];
    }
    const float *input[] = {left.data(), right.data()};
    integration::WaveFormat format;
    std::unique_ptr<base::OutputStream> stream = file.createOutputStream();
    auto options = audio::AudioFormatWriterOptions()
                       .withSampleRate(48000)
                       .withNumChannels(2)
                       .withBitsPerSample(24)
                       .withMetadata("NumCuePoints", "1")
                       .withMetadata("Cue0Identifier", "9")
                       .withMetadata("Cue0Offset", "123")
                       .withMetadata("NumCueLabels", "1")
                       .withMetadata("CueLabel0Identifier", "9")
                       .withMetadata("CueLabel0Text", "Impact");
    auto writer = format.createWriterFor(stream, options);
    require(bool(writer) && !stream, "native writer owns output");
    {
      audio::AudioFormatWriter::ThreadedWriter recorder(writer.release(), 8192);
      require(recorder.write(input, 512), "recorder accepts prepared block");
      require(recorder.finish().complete(), "recorder finalises the native file successfully");
    } // drains and finalises
    audio::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<audio::AudioFormatReader> reader(
        formats.createReaderFor(file));
    require(reader && reader->sampleRate == 48000 &&
                reader->lengthInSamples == 512 && reader->numChannels == 2,
            "native decoder header");
    std::array<float, 520> a{}, b{};
    float *output[] = {a.data(), b.data()};
    require(reader->read(output, 2, -4, 520),
            "bounded reader handles both ends");
    for (int i = 0; i < 4; ++i)
      require(a[i] == 0 && a[516 + i] == 0, "out-of-file samples are silent");
    for (int i = 0; i < 512; ++i)
      require(std::abs(a[i + 4] - left[i]) < 1e-6 &&
                  std::abs(b[i + 4] - right[i]) < 1e-6,
              "24-bit PCM stereo roundtrip");
    require(reader->metadataValues.getValue("CueLabel0Text") == "Impact" &&
                reader->metadataValues.getValue("Cue0Offset") == "123",
            "markers remain available to editor");
    reader.reset();
    require(file.deleteFile(), "remove audio fixture");
    std::cout << "Native CyAudioCore files, recorder, bounds and markers "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
