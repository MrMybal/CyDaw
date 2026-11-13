#include "UiState.h"
#include <cmath>
#include <cydaw/base/File.h>
#include <cydaw/integration/AudioFiles.h>

namespace cydaw::v2 {

WaveformCache::WaveformCache(cydaw::audio::AudioFormatManager &formats)
    : formats_(formats) {
  for (auto &worker : workers_)
    worker = std::thread([this] { run(); });
}

WaveformCache::~WaveformCache() {
  {
    std::lock_guard lock(jobMutex_);
    stopping_ = true;
    jobs_.clear();
  }
  jobReady_.notify_all();
  for (auto &worker : workers_)
    if (worker.joinable())
      worker.join();
}

WaveformPeaks *WaveformCache::get(const cydaw::base::File &file) {
  const auto key = file.getFullPathName();
  // The disk asked whether the file is there once a second at most: asked
  // for every clip drawn, at every frame, it was a good part of a frame
  // (more with a virus scanner looking at each request).
  const auto now = cydaw::base::Time::getMillisecondCounterHiRes();
  auto &present = present_[key];
  if (present.first == 0.0 || now - present.first > 1'000.0)
    present = {now, file.existsAsFile()};
  if (!present.second)
    return nullptr;
  const auto found = thumbnails_.find(key);
  if (found != thumbnails_.end())
    return found->second.get();
  // 128 samples per peak: fine enough for a close zoom, light to hold.
  auto thumbnail = std::make_shared<WaveformPeaks>();
  auto *raw = thumbnail.get();
  thumbnails_.emplace(key, thumbnail);
  {
    std::lock_guard lock(jobMutex_);
    jobs_.push_back({file, std::move(thumbnail)});
  }
  jobReady_.notify_one();
  return raw;
}

bool WaveformCache::anyLoading() const {
  for (const auto &[path, thumbnail] : thumbnails_)
    if (!thumbnail->isFullyLoaded())
      return true;
  return false;
}

void WaveformCache::clear() {
  {
    std::lock_guard lock(jobMutex_);
    jobs_.clear();
  }
  thumbnails_.clear();
  present_.clear();
}

} // namespace cydaw::v2

namespace cydaw::v2 {
void WaveformPeaks::getApproximateMinMax(double start, double end, int channel,
                                         float &low, float &high) const {
  low = high = 0;
  auto d = data.load(std::memory_order_acquire);
  if (!d || channel < 0 || channel >= d->channels || !std::isfinite(start) ||
      !std::isfinite(end))
    return;
  auto &peaks = d->peaks[channel];
  auto first =
      std::size_t(std::clamp(std::floor(std::max(0., start) * d->rate / 128),
                             0., double(peaks.size())));
  auto last =
      std::size_t(std::clamp(std::ceil(std::max(start, end) * d->rate / 128),
                             0., double(peaks.size())));
  for (auto i = first; i < last; ++i) {
    low = std::min(low, peaks[i].first);
    high = std::max(high, peaks[i].second);
  }
}
void WaveformCache::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(jobMutex_);
      jobReady_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_)
        return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    audio::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<audio::AudioFormatReader> reader(
        formats.createReaderFor(job.file));
    if (reader && reader->sampleRate > 0 && reader->numChannels > 0 &&
        reader->numChannels <= 256 && reader->lengthInSamples >= 0) {
      auto data = std::make_shared<WaveformPeaks::Data>();
      data->rate = reader->sampleRate;
      data->channels = int(reader->numChannels);
      auto blocks = std::size_t((reader->lengthInSamples + 127) / 128);
      if (blocks <=
          128 * 1024 * 1024 /
              (reader->numChannels * sizeof(std::pair<float, float>))) {
        data->peaks.resize(reader->numChannels);
        for (auto &ch : data->peaks)
          ch.reserve(blocks);
        std::vector<std::vector<float>> scratch(reader->numChannels,
                                                std::vector<float>(16384));
        std::vector<float *> pointers;
        for (auto &ch : scratch)
          pointers.push_back(ch.data());
        bool valid = true;
        for (base::int64 first = 0; first < reader->lengthInSamples;
             first += 16384) {
          {
            std::lock_guard lock(jobMutex_);
            if (stopping_)
              return;
          }
          if (job.target.use_count() == 1) {
            valid = false;
            break;
          }
          auto n = int(
              std::min<base::int64>(16384, reader->lengthInSamples - first));
          if (!reader->read(pointers.data(), int(pointers.size()), first, n)) {
            valid = false;
            break;
          }
          for (int ch = 0; ch < data->channels; ++ch)
            for (int offset = 0; offset < n; offset += 128) {
              float low = 0, high = 0;
              for (int i = offset; i < std::min(n, offset + 128); ++i) {
                auto value = scratch[ch][i];
                if (std::isfinite(value)) {
                  low = std::min(low, value);
                  high = std::max(high, value);
                }
              }
              data->peaks[ch].emplace_back(low, high);
            }
        }
        if (valid)
          job.target->data.store(std::move(data), std::memory_order_release);
      }
    }
    job.target->loaded.store(true, std::memory_order_release);
  }
}
} // namespace cydaw::v2
