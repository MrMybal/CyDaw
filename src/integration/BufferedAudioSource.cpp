#include <chrono>
#include <cydaw/audio/Sources.h>
#include <limits>
namespace cydaw::audio {
struct ReadAheadScheduler::Registry {
  std::mutex mutex;
  std::vector<std::shared_ptr<Task>> tasks;
};
ReadAheadScheduler::ReadAheadScheduler(base::Text name)
    : base::Thread(std::move(name)), registry_(std::make_unique<Registry>()) {}
ReadAheadScheduler::~ReadAheadScheduler() { stopThread(-1); }
void ReadAheadScheduler::add(const std::shared_ptr<Task> &task) {
  {
    std::lock_guard guard(registry_->mutex);
    registry_->tasks.push_back(task);
  }
  if (!isThreadRunning())
    startThread();
  notify();
}
void ReadAheadScheduler::remove(const std::shared_ptr<Task> &task) {
  if (!task)
    return;
  {
    std::lock_guard guard(task->lifetime);
    task->active = false;
  }
  std::lock_guard guard(registry_->mutex);
  std::erase(registry_->tasks, task);
}
void ReadAheadScheduler::run() {
  while (!threadShouldExit()) {
    std::vector<std::shared_ptr<Task>> tasks;
    {
      std::lock_guard guard(registry_->mutex);
      tasks = registry_->tasks;
    }
    bool work = false;
    for (const auto &task : tasks) {
      if (threadShouldExit())
        break;
      std::lock_guard guard(task->lifetime);
      if (task->active)
        work = task->fillOne() || work;
    }
    if (!work)
      wait(1);
  }
}
struct BufferingAudioSource::Native {
  struct Page {
    SampleBuffer<float> samples;
    std::atomic<int> access{};
    std::int64_t first = std::numeric_limits<std::int64_t>::min();
  };
  struct Cache final : ReadAheadScheduler::Task {
    PositionableAudioSource *source{};
    std::vector<std::unique_ptr<Page>> pages;
    int frames{}, channels{};
    std::atomic<std::int64_t> position{};
    std::atomic<std::uint64_t> misses{};
    bool fillOne() override {
      const auto wanted =
          std::max<std::int64_t>(0, position.load(std::memory_order_acquire));
      const auto first = wanted / frames * frames;
      for (std::size_t ahead = 0; ahead < pages.size(); ++ahead) {
        const auto target = first + std::int64_t(ahead) * frames;
        auto &page = *pages[std::size_t(target / frames) % pages.size()];
        int free = 0;
        if (!page.access.compare_exchange_strong(free, -1,
                                                 std::memory_order_acquire))
          continue;
        if (page.first == target) {
          page.access.store(0, std::memory_order_release);
          continue;
        }
        source->setNextReadPosition(target);
        source->getNextAudioBlock({&page.samples, 0, frames});
        page.first = target;
        page.access.store(0, std::memory_order_release);
        return true;
      }
      return false;
    }
    bool ready(std::int64_t at, int count) const {
      while (count > 0) {
        const auto first = at / frames * frames;
        auto &page = *pages[std::size_t(first / frames) % pages.size()];
        int free = 0;
        if (!page.access.compare_exchange_strong(free, 1,
                                                 std::memory_order_acquire))
          return false;
        const bool okay = page.first == first;
        page.access.store(0, std::memory_order_release);
        if (!okay)
          return false;
        const int n = std::min(count, frames - int(at - first));
        count -= n;
        at += n;
      }
      return true;
    }
  };
  PositionableAudioSource *source{};
  std::unique_ptr<PositionableAudioSource> owner;
  ReadAheadScheduler *scheduler{};
  int ahead{}, channels{};
  bool prefill{};
  std::shared_ptr<Cache> cache;
  std::int64_t initial{};
};
BufferingAudioSource::BufferingAudioSource(PositionableAudioSource *source,
                                           ReadAheadScheduler &scheduler,
                                           bool own, int ahead, int channels,
                                           bool prefill)
    : native_(std::make_unique<Native>()) {
  auto &n = *native_;
  n.source = source;
  if (own)
    n.owner.reset(source);
  n.scheduler = &scheduler;
  n.ahead = std::max(1024, ahead);
  n.channels = std::clamp(channels, 1, 64);
  n.prefill = prefill;
}
BufferingAudioSource::~BufferingAudioSource() { releaseResources(); }
void BufferingAudioSource::prepareToPlay(int maximum, double rate) {
  releaseResources();
  auto &n = *native_;
  if (!n.source)
    return;
  n.source->prepareToPlay(maximum, rate);
  auto cache = std::make_shared<Native::Cache>();
  cache->source = n.source;
  cache->position = n.initial;
  cache->frames = std::max(1024, maximum * 2);
  cache->channels = n.channels;
  const int pageCount =
      std::max(4, (n.ahead + cache->frames - 1) / cache->frames);
  for (int page = 0; page < pageCount; ++page) {
    auto value = std::make_unique<Native::Page>();
    value->samples.setSize(n.channels, cache->frames);
    cache->pages.push_back(std::move(value));
  }
  n.cache = cache;
  n.scheduler->add(cache);
  if (n.prefill)
    waitForReady(std::max(1, maximum));
}
void BufferingAudioSource::releaseResources() {
  auto &n = *native_;
  if (n.cache) {
    n.initial = n.cache->position.load();
    n.scheduler->remove(n.cache);
    n.cache.reset();
    n.source->releaseResources();
  }
}
void BufferingAudioSource::setNextReadPosition(std::int64_t at) {
  auto &n = *native_;
  if (n.cache)
    n.cache->position.store(std::max<std::int64_t>(0, at),
                            std::memory_order_release);
  else
    n.initial = std::max<std::int64_t>(0, at);
}
std::int64_t BufferingAudioSource::getNextReadPosition() const {
  auto &n = *native_;
  auto at = n.cache ? n.cache->position.load() : n.initial;
  const auto length = getTotalLength();
  return isLooping() && length > 0 ? at % length : at;
}
std::int64_t BufferingAudioSource::getTotalLength() const {
  return native_->source ? native_->source->getTotalLength() : 0;
}
bool BufferingAudioSource::isLooping() const {
  return native_->source && native_->source->isLooping();
}
void BufferingAudioSource::setLooping(bool loop) {
  auto &n = *native_;
  if (!n.source)
    return;
  n.source->setLooping(loop);
  if (n.cache) {
    std::lock_guard guard(n.cache->lifetime);
    for (const auto &page : n.cache->pages) {
      int free = 0;
      if (page->access.compare_exchange_strong(free, -1,
                                               std::memory_order_acquire)) {
        page->first = std::numeric_limits<std::int64_t>::min();
        page->access.store(0, std::memory_order_release);
      }
    }
  }
}
void BufferingAudioSource::getNextAudioBlock(
    const AudioSourceChannelInfo &block) {
  block.clearActiveBufferRegion();
  auto &n = *native_;
  auto *cache = n.cache.get();
  if (!cache || !block.buffer || block.numSamples <= 0)
    return;
  auto at = cache->position.load(std::memory_order_acquire);
  int written = 0;
  bool missed = false;
  while (written < block.numSamples) {
    const auto first = at / cache->frames * cache->frames;
    auto &page =
        *cache->pages[std::size_t(first / cache->frames) % cache->pages.size()];
    const int offset = int(at - first),
              count =
                  std::min(block.numSamples - written, cache->frames - offset);
    int free = 0;
    if (page.access.compare_exchange_strong(free, 1,
                                            std::memory_order_acquire)) {
      if (page.first == first) {
        for (int ch = 0;
             ch < std::min(block.buffer->getNumChannels(), cache->channels);
             ++ch)
          block.buffer->copyFrom(ch, block.startSample + written, page.samples,
                                 ch, offset, count);
      } else
        missed = true;
      page.access.store(0, std::memory_order_release);
    } else
      missed = true;
    written += count;
    at += count;
  }
  cache->position.store(at, std::memory_order_release);
  if (missed)
    cache->misses.fetch_add(1, std::memory_order_relaxed);
}
bool BufferingAudioSource::waitForReady(int frames, int timeout) const {
  auto &n = *native_;
  if (!n.cache)
    return false;
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(std::max(0, timeout));
  do {
    if (n.cache->ready(n.cache->position.load(std::memory_order_acquire),
                       frames))
      return true;
    base::Thread::sleep(1);
  } while (std::chrono::steady_clock::now() < end);
  return false;
}
std::uint64_t BufferingAudioSource::underruns() const noexcept {
  return native_->cache ? native_->cache->misses.load(std::memory_order_relaxed)
                        : 0;
}
} // namespace cydaw::audio
