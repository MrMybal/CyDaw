#include "platform/detail/AsioApi.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cydaw/platform/AsioDriver.h>
#include <iostream>
#include <malloc.h>
#include <new>
#include <stdexcept>
namespace audit {
thread_local bool active = false;
thread_local unsigned allocations = 0;
} // namespace audit
void *operator new(std::size_t n) {
  if (audit::active)
    ++audit::allocations;
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept {
  if (audit::active)
    ++audit::allocations;
  std::free(p);
}
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
void *operator new[](std::size_t n, std::align_val_t a) {
  if (audit::active)
    ++audit::allocations;
  if (auto *p = _aligned_malloc(n, std::size_t(a)))
    return p;
  throw std::bad_alloc();
}
void operator delete[](void *p, std::align_val_t) noexcept {
  if (audit::active)
    ++audit::allocations;
  _aligned_free(p);
}
namespace {
using namespace cyaudio;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
void check(Status s, const char *message) { require(s == Status::ok, message); }
struct Fixture {
  static inline Fixture *current = nullptr;
  unsigned init = 0, terminated = 0, opens = 0, closed = 0, aborted = 0,
           panels = 0;
  bool running = false, interleaved = false, failStart = false,
       failStop = false, failOpen = false;
  PaStreamCallback *callback = nullptr;
  void *user = nullptr;
  std::vector<int> inputSelectors, outputSelectors;
  PaHostApiInfo host{1, paASIO, "ASIO fixture", 2, 10, 10};
  PaDeviceInfo device{2,    "Fixture interface", 1, 4, 4, .001, .002, .01, .02,
                      48000};
  PaStreamInfo stream{1, .001, .002, 48000};
  cydaw::platform::detail::AsioApi api() {
    current = this;
    cydaw::platform::detail::AsioApi a;
    a.initialize = []() -> int {
      ++current->init;
      return paNoError;
    };
    a.terminate = []() -> int {
      ++current->terminated;
      return paNoError;
    };
    a.hostIndex = [](PaHostApiTypeId type) -> int {
      return type == paASIO ? 1 : paHostApiNotFound;
    };
    a.hostInfo = [](PaHostApiIndex index) -> const PaHostApiInfo * {
      return index == 1 ? &current->host : nullptr;
    };
    a.deviceIndex = [](PaHostApiIndex host, int index) -> int {
      return host == 1 && index < 2 ? 10 + index : paInvalidDevice;
    };
    a.deviceInfo = [](PaDeviceIndex index) -> const PaDeviceInfo * {
      return index == 10 || index == 11 ? &current->device : nullptr;
    };
    a.supports = [](const PaStreamParameters *, const PaStreamParameters *,
                    double rate) -> int {
      return rate == 48000 || rate == 96000 ? paFormatIsSupported
                                            : paInvalidSampleRate;
    };
    a.bufferSizes = [](PaDeviceIndex, long *lo, long *hi, long *preferred,
                       long *step) -> int {
      *lo = 32;
      *hi = 512;
      *preferred = 64;
      *step = -1;
      return paNoError;
    };
    a.inputName = a.outputName = [](PaDeviceIndex, int channel,
                                    const char **name) -> int {
      static const char *names[] = {"One", "Two", "Three", "Four"};
      if (channel < 0 || channel >= 4)
        return paInvalidChannelCount;
      *name = names[channel];
      return paNoError;
    };
    a.panel = [](PaDeviceIndex, void *) -> int {
      ++current->panels;
      return paNoError;
    };
    a.open = [](PaStream **stream, const PaStreamParameters *in,
                const PaStreamParameters *out, double, unsigned long,
                PaStreamFlags, PaStreamCallback *callback, void *user) -> int {
      auto &f = *current;
      ++f.opens;
      if (f.failOpen)
        return paUnanticipatedHostError;
      if (f.interleaved && ((in ? in : out)->sampleFormat & paNonInterleaved))
        return paSampleFormatNotSupported;
      f.inputSelectors.clear();
      f.outputSelectors.clear();
      for (bool capture : {true, false})
        if (const auto *p = capture ? in : out) {
          if (std::abs(p->suggestedLatency - 64. / 48000) > 1e-12)
            return paInvalidFlag;
          const auto *info = static_cast<const PaAsioStreamInfo *>(
              p->hostApiSpecificStreamInfo);
          if (!info || info->hostApiType != paASIO ||
              info->flags != paAsioUseChannelSelectors)
            return paInvalidFlag;
          auto &v = capture ? f.inputSelectors : f.outputSelectors;
          v.assign(info->channelSelectors,
                   info->channelSelectors + p->channelCount);
        }
      f.callback = callback;
      f.user = user;
      *stream = reinterpret_cast<PaStream *>(&f);
      return paNoError;
    };
    a.start = [](PaStream *) -> int {
      if (current->failStart)
        return paUnanticipatedHostError;
      current->running = true;
      return paNoError;
    };
    a.stop = [](PaStream *) -> int {
      if (current->failStop)
        return paUnanticipatedHostError;
      current->running = false;
      return paNoError;
    };
    a.abort = [](PaStream *) -> int {
      ++current->aborted;
      current->running = false;
      return paNoError;
    };
    a.close = [](PaStream *) -> int {
      ++current->closed;
      current->callback = nullptr;
      current->user = nullptr;
      return paNoError;
    };
    a.active = [](PaStream *) -> int { return current->running ? 1 : 0; };
    a.streamInfo = [](PaStream *) -> const PaStreamInfo * {
      return &current->stream;
    };
    a.onFinished = [](PaStream *, PaStreamFinishedCallback *) -> int {
      return paNoError;
    };
    return a;
  }
};
struct Received {
  unsigned calls = 0;
  std::uint64_t next = 0;
  bool valid = true, fail = false;
};
Status render(void *p, AudioBlock<const float> in, AudioBlock<float> out,
              const AudioCallbackContext &c) noexcept {
  auto &r = *static_cast<Received *>(p);
  r.valid &= c.clockValid && c.clock.firstFrame == r.next &&
             c.nativeCallbackFrames == 131 && out.frames() <= 64;
  r.valid &= c.callbackTime.valid && c.generation > 0;
  r.next += out.frames();
  ++r.calls;
  for (std::size_t ch = 0; ch < out.channels(); ++ch)
    for (std::size_t n = 0; n < out.frames(); ++n)
      out.channel(ch)[n] =
          in.channels() ? in.channel(ch % in.channels())[n] * .5f : .25f;
  return r.fail ? Status::io_error : Status::ok;
}
void exercise(bool interleaved) {
  Fixture f;
  f.interleaved = interleaved;
  {
    cydaw::platform::AsioDriver driver(f.api());
    AudioDeviceManager manager;
    check(manager.prepare(driver.interface()), "external driver prepare");
    std::vector<AudioDeviceInfo> devices;
    check(manager.enumerate(devices), "ASIO enumeration");
    require(devices.size() == 4 && devices[0].backend == AudioBackend::asio &&
                !devices[0].persistentIdentity,
            "session identities, ASIO provider");
    cydaw::platform::AsioDeviceDetails details;
    check(driver.details("asio:10", details), "driver details");
    require(details.bufferSizes == std::vector<int>({32, 64, 128, 256, 512}) &&
                details.sampleRates == std::vector<double>({48000, 96000}),
            "native buffer/rate queries");
    AudioDeviceConfig c;
    c.inputId = c.outputId = "asio:10";
    c.inputChannels = 2;
    c.outputChannels = 2;
    c.activeInputChannels = {2, 0};
    c.activeOutputChannels = {1, 3};
    c.maxCallbackFrames = 64;
    c.periodFrames = 64;
    AudioReconnectOptions options;
    options.automatic = false;
    Received received;
    check(manager.openWithContext(c, render, &received, options),
          "ASIO manager start");
    require(manager.running(), "running");
    require(f.inputSelectors == std::vector<int>({2, 0}) &&
                f.outputSelectors == std::vector<int>({1, 3}),
            "nonconsecutive native channel routing");
    AudioStreamDescription stream;
    check(manager.description(stream), "negotiated description");
    require(stream.backend == AudioBackend::asio &&
                stream.access == AudioAccess::exclusive &&
                stream.sampleRate == 48000 && stream.output.latency.valid,
            "negotiated ASIO properties");
    require(driver.showControlPanel("asio:10", nullptr) ==
                Status::transition_in_progress,
            "control panel cannot race live stream");
    std::array<float, 262> input{}, output{};
    for (unsigned n = 0; n < 262; ++n)
      input[n] = float(n) / 512;
    std::array<float *, 2> ip{input.data(), input.data() + 131},
        op{output.data(), output.data() + 131};
    PaStreamCallbackTimeInfo time{99.999, 100, 100.002};
    audit::allocations = 0;
    audit::active = true;
    f.callback(interleaved ? static_cast<void *>(input.data()) : ip.data(),
               interleaved ? static_cast<void *>(output.data()) : op.data(),
               131, &time, 0, f.user);
    audit::active = false;
    require(audit::allocations == 0,
            "prepared callback must not allocate or free");
    require(received.valid && received.calls == 3 && received.next == 131,
            "chunked callback and clock generation");
    for (unsigned n = 0; n < 262; ++n)
      require(output[n] == input[n] * .5f, "ASIO sample layout");
    received.fail = true;
    f.callback(interleaved ? static_cast<void *>(input.data()) : ip.data(),
               interleaved ? static_cast<void *>(output.data()) : op.data(),
               131, &time, 0, f.user);
    for (auto value : output)
      require(value == 0, "processor failure must silence outputs");
    f.failStop = true;
    manager.close();
    require(f.aborted == 1 && !f.running && !f.callback,
            "stop error aborts and joins before close");
    check(driver.showControlPanel("asio:10", nullptr),
          "ASIO native control panel");
    require(f.panels == 1, "panel routed");
    c.activeOutputChannels = {1, 1};
    require(manager.openWithContext(c, render, &received, options) ==
                Status::channel_mismatch,
            "duplicate output selectors rejected");
    manager.close();
    c.activeOutputChannels = {1, 3};
    c.inputId = "asio:11";
    require(manager.openWithContext(c, render, &received, options) ==
                Status::invalid_argument,
            "ASIO duplex cannot span different drivers");
    manager.close();
    c.inputChannels = 0;
    c.inputId.clear();
    c.activeInputChannels.clear();
    received = {};
    check(manager.openWithContext(c, render, &received, options),
          "output-only ASIO");
    manager.close();
    f.failStart = true;
    require(manager.openWithContext(c, render, &received, options) !=
                    Status::ok &&
                !manager.running() && !f.callback,
            "failed start closes stream");
    manager.close();
  }
  require(f.init == 1 && f.terminated == 1,
          "balanced PortAudio runtime lifetime");
}
} // namespace
int main() {
  try {
    exercise(false);
    exercise(true);
    std::cout << "ASIO/CyAudioCore deterministic integration passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
