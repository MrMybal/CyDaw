#include "detail/AsioApi.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cydaw/platform/AsioDriver.h>
#include <limits>
#include <numeric>

namespace cydaw::platform {
namespace {
using namespace cyaudio;
Status status(PaError e) noexcept {
  switch (e) {
  case paNoError:
    return Status::ok;
  case paInsufficientMemory:
    return Status::allocation_failed;
  case paInvalidChannelCount:
    return Status::channel_mismatch;
  case paInvalidSampleRate:
    return Status::invalid_spec;
  case paSampleFormatNotSupported:
    return Status::unsupported_format;
  case paHostApiNotFound:
  case paInvalidHostApi:
    return Status::unsupported_backend;
  case paInvalidDevice:
  case paDeviceUnavailable:
    return Status::device_unavailable;
  case paInvalidFlag:
  case paBadStreamPtr:
  case paBadBufferPtr:
    return Status::invalid_argument;
  case paTimedOut:
    return Status::timed_out;
  default:
    return Status::io_error;
  }
}
std::uint64_t now() noexcept {
  return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count());
}
AudioTimingValue estimatedTime(std::uint64_t entry, double current,
                               double when) noexcept {
  if (!std::isfinite(current) || !std::isfinite(when) || !current || !when)
    return {};
  const auto value = static_cast<long double>(entry) +
                     (static_cast<long double>(when) - current) * 1e9L;
  if (value < 0 || value > std::numeric_limits<std::uint64_t>::max())
    return {};
  return {std::uint64_t(value), AudioMeasurementSource::backendClockEstimate,
          true};
}
} // namespace
struct AsioDriver::Impl {
  detail::AsioApi api;
  bool initialized = false, planar = true;
  PaStream *stream = nullptr;
  AudioDeviceConfig config;
  AudioStreamDescription description;
  AudioDeviceCallback callback = nullptr;
  AudioDeviceContextCallback extended = nullptr;
  void *user = nullptr;
  AudioBuffer<float> input, output;
  std::vector<int> inputSelectors, outputSelectors;
  std::atomic<std::uint32_t> notifications{0};
  std::uint64_t frame = 0;
  explicit Impl(const detail::AsioApi &a) : api(a) {}
  ~Impl() {
    close();
    if (initialized)
      (void)api.terminate();
  }
  Status ensure() noexcept {
    if (initialized)
      return Status::ok;
    const auto r = status(api.initialize());
    initialized = r == Status::ok;
    return r;
  }
  Status enumerate(std::vector<AudioDeviceInfo> &out) noexcept {
    auto r = ensure();
    if (r != Status::ok)
      return r;
    const auto host = api.hostIndex(paASIO);
    if (host < 0)
      return status(host);
    const auto *h = api.hostInfo(host);
    if (!h || h->type != paASIO)
      return Status::unsupported_backend;
    try {
      std::vector<AudioDeviceInfo> next;
      for (int i = 0; i < h->deviceCount; ++i) {
        const auto index = api.deviceIndex(host, i);
        const auto *d = api.deviceInfo(index);
        if (!d || d->hostApi != host)
          continue;
        for (bool capture : {true, false}) {
          if ((capture ? d->maxInputChannels : d->maxOutputChannels) <= 0)
            continue;
          AudioDeviceInfo info;
          info.id = "asio:" + std::to_string(index);
          info.name = d->name ? d->name : "ASIO";
          info.input = capture;
          info.isDefault = index == (capture ? h->defaultInputDevice
                                             : h->defaultOutputDevice);
          info.backend = AudioBackend::asio;
          info.provider = AudioProvider::portAudio;
          // PortAudio indices are session-local; do not claim
          // hardware-persistent IDs.
          next.push_back(std::move(info));
        }
      }
      out = std::move(next);
      return Status::ok;
    } catch (...) {
      return Status::allocation_failed;
    }
  }
  PaDeviceIndex find(std::string_view id, bool capture) noexcept {
    const auto host = api.hostIndex(paASIO);
    if (host < 0)
      return paNoDevice;
    const auto *h = api.hostInfo(host);
    if (!h)
      return paNoDevice;
    if (id.empty())
      return capture ? h->defaultInputDevice : h->defaultOutputDevice;
    if (!id.starts_with("asio:"))
      return paNoDevice;
    int wanted = -1;
    const auto text = id.substr(5);
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), wanted);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      return paNoDevice;
    for (int i = 0; i < h->deviceCount; ++i)
      if (api.deviceIndex(host, i) == wanted)
        return wanted;
    return paNoDevice;
  }
  Status details(std::string_view id, AsioDeviceDetails &out) noexcept {
    auto r = ensure();
    if (r != Status::ok)
      return r;
    const auto index = find(id, false);
    const auto *d = api.deviceInfo(index);
    if (!d)
      return Status::device_unavailable;
    try {
      AsioDeviceDetails next;
      next.defaultSampleRate = d->defaultSampleRate;
      for (bool capture : {true, false}) {
        auto &names = capture ? next.inputNames : next.outputNames;
        const auto channels =
            capture ? d->maxInputChannels : d->maxOutputChannels;
        for (int ch = 0; ch < channels; ++ch) {
          const char *name = nullptr;
          const auto code = capture ? api.inputName(index, ch, &name)
                                    : api.outputName(index, ch, &name);
          names.push_back(code == paNoError && name
                              ? name
                              : (capture ? "Input " : "Output ") +
                                    std::to_string(ch + 1));
        }
      }
      long minimum = 0, maximum = 0, preferred = 0, step = 0;
      if (api.bufferSizes(index, &minimum, &maximum, &preferred, &step) ==
              paNoError &&
          minimum > 0 && maximum >= minimum && maximum <= 1048576) {
        next.preferredBufferFrames = int(preferred);
        if (step == -1) {
          for (long n = 1; n <= maximum; n *= 2)
            if (n >= minimum)
              next.bufferSizes.push_back(int(n));
        } else if (step > 0) {
          for (long n = minimum; n <= maximum && next.bufferSizes.size() < 512;
               n += step)
            next.bufferSizes.push_back(int(n));
        } else if (minimum == maximum)
          next.bufferSizes.push_back(int(minimum));
        if (preferred >= minimum && preferred <= maximum)
          next.bufferSizes.push_back(int(preferred));
        std::sort(next.bufferSizes.begin(), next.bufferSizes.end());
        next.bufferSizes.erase(
            std::unique(next.bufferSizes.begin(), next.bufferSizes.end()),
            next.bufferSizes.end());
      }
      PaStreamParameters in{}, output{};
      in.device = output.device = index;
      in.sampleFormat = output.sampleFormat = paFloat32 | paNonInterleaved;
      in.channelCount = std::min(2, d->maxInputChannels);
      output.channelCount = std::min(2, d->maxOutputChannels);
      for (double rate :
           {22050., 32000., 44100., 48000., 88200., 96000., 176400., 192000.,
            352800., 384000., d->defaultSampleRate})
        if (std::isfinite(rate) && rate > 0 &&
            api.supports(in.channelCount ? &in : nullptr,
                         output.channelCount ? &output : nullptr,
                         rate) == paFormatIsSupported)
          next.sampleRates.push_back(rate);
      std::sort(next.sampleRates.begin(), next.sampleRates.end());
      next.sampleRates.erase(
          std::unique(next.sampleRates.begin(), next.sampleRates.end()),
          next.sampleRates.end());
      out = std::move(next);
      return Status::ok;
    } catch (...) {
      return Status::allocation_failed;
    }
  }
  void close() noexcept {
    if (stream) {
      if (api.active(stream) > 0) {
        if (api.stop(stream) != paNoError)
          (void)api.abort(stream);
      }
      (void)api.close(stream);
      stream = nullptr;
    }
    callback = nullptr;
    extended = nullptr;
    user = nullptr;
  }
  static int render(const void *from, void *to, unsigned long frames,
                    const PaStreamCallbackTimeInfo *time,
                    PaStreamCallbackFlags flags, void *opaque) noexcept {
    auto &s = *static_cast<Impl *>(opaque);
    const auto entry = now();
    for (std::size_t first = 0; first < frames;) {
      const auto count =
          std::min<std::size_t>(s.config.maxCallbackFrames, frames - first);
      AudioBlock<float> in{s.input.block().channelData(),
                           s.config.inputChannels, count};
      AudioBlock<float> out{s.output.block().channelData(),
                            s.config.outputChannels, count};
      for (std::size_t ch = 0; ch < in.channels(); ++ch) {
        auto dest = in.channel(ch);
        if (!from)
          std::fill(dest.begin(), dest.end(), 0.f);
        else if (s.planar) {
          auto *p = static_cast<const float *const *>(from)[ch];
          if (p)
            std::copy_n(p + first, count, dest.data());
          else
            std::fill(dest.begin(), dest.end(), 0.f);
        } else
          for (std::size_t n = 0; n < count; ++n)
            dest[n] = static_cast<const float *>(
                from)[(first + n) * in.channels() + ch];
      }
      for (std::size_t ch = 0; ch < out.channels(); ++ch)
        std::fill(out.channel(ch).begin(), out.channel(ch).end(), 0.f);
      AudioCallbackContext context;
      const auto offset = std::uint64_t(static_cast<long double>(first) * 1e9L /
                                        s.config.sampleRate);
      context.clock = {entry + offset, s.frame, double(s.config.sampleRate)};
      context.generation = s.config.generation;
      context.clockValid = true;
      context.callbackTime = {
          entry, AudioMeasurementSource::callbackSteadyClock, true};
      context.nativeCallbackFrames = std::uint32_t(frames);
      context.nativeCallbackSizeKnown = true;
      if (time) {
        if (!(flags & paInputUnderflow))
          context.adcTime = estimatedTime(entry + offset, time->currentTime,
                                          time->inputBufferAdcTime);
        context.dacTime = estimatedTime(entry + offset, time->currentTime,
                                        time->outputBufferDacTime);
      }
      const auto r = s.extended   ? s.extended(s.user, in, out, context)
                     : s.callback ? s.callback(s.user, in, out, s.frame)
                                  : Status::not_prepared;
      if (to)
        for (std::size_t ch = 0; ch < out.channels(); ++ch) {
          auto source = out.channel(ch);
          if (r != Status::ok)
            std::fill(source.begin(), source.end(), 0.f);
          if (s.planar) {
            auto *p = static_cast<float *const *>(to)[ch];
            if (p)
              std::copy_n(source.data(), count, p + first);
          } else
            for (std::size_t n = 0; n < count; ++n)
              static_cast<float *>(to)[(first + n) * out.channels() + ch] =
                  source[n];
        }
      s.frame += count;
      first += count;
    }
    return paContinue;
  }
  Status open(const AudioDeviceConfig &c, AudioDeviceCallback legacy,
              AudioDeviceContextCallback cb, void *data) noexcept {
    close();
    auto r = ensure();
    if (r != Status::ok)
      return r;
    if ((!legacy && !cb) || !c.sampleRate || !c.periodFrames ||
        !c.maxCallbackFrames || c.maxCallbackFrames > 1048576 ||
        c.inputChannels > 256 || c.outputChannels > 256 ||
        (!c.inputChannels && !c.outputChannels))
      return Status::invalid_argument;
    if (!c.inputChannelMap.empty() || !c.outputChannelMap.empty())
      return Status::unsupported_backend;
    const auto inIndex = c.inputChannels ? find(c.inputId, true) : paNoDevice;
    const auto outIndex =
        c.outputChannels ? find(c.outputId, false) : paNoDevice;
    if ((c.inputChannels && inIndex < 0) || (c.outputChannels && outIndex < 0))
      return Status::device_unavailable;
    if (c.inputChannels && c.outputChannels && inIndex != outIndex)
      return Status::invalid_argument;
    try {
      config = c;
      description = {};
      frame = 0;
      notifications.store(0);
      auto select = [](const std::vector<unsigned> &active,
                       std::size_t channels, int maximum,
                       std::vector<int> &result) {
        if (!active.empty() && active.size() != channels)
          return false;
        result.resize(channels);
        for (std::size_t i = 0; i < channels; ++i) {
          const auto v = active.empty() ? unsigned(i) : active[i];
          if (v >= unsigned(maximum) ||
              std::find(result.begin(), result.begin() + i, int(v)) !=
                  result.begin() + i)
            return false;
          result[i] = int(v);
        }
        return true;
      };
      const auto *di = c.inputChannels ? api.deviceInfo(inIndex) : nullptr;
      const auto *d = c.outputChannels ? api.deviceInfo(outIndex) : nullptr;
      if ((c.inputChannels &&
           (!di || !select(c.activeInputChannels, c.inputChannels,
                           di->maxInputChannels, inputSelectors))) ||
          (c.outputChannels &&
           (!d || !select(c.activeOutputChannels, c.outputChannels,
                          d->maxOutputChannels, outputSelectors))))
        return Status::channel_mismatch;
      if (c.inputChannels) {
        r = input.resize(c.inputChannels, c.maxCallbackFrames);
        if (r != Status::ok)
          return r;
      } else
        input = {};
      if (c.outputChannels) {
        r = output.resize(c.outputChannels, c.maxCallbackFrames);
        if (r != Status::ok)
          return r;
      } else
        output = {};
      PaAsioStreamInfo ia{sizeof(PaAsioStreamInfo), paASIO, 1,
                          paAsioUseChannelSelectors, inputSelectors.data()};
      PaAsioStreamInfo oa{sizeof(PaAsioStreamInfo), paASIO, 1,
                          paAsioUseChannelSelectors, outputSelectors.data()};
      PaStreamParameters ip{inIndex, int(c.inputChannels),
                            paFloat32 | paNonInterleaved,
                            double(c.periodFrames) / c.sampleRate, &ia};
      PaStreamParameters op{outIndex, int(c.outputChannels),
                            paFloat32 | paNonInterleaved,
                            double(c.periodFrames) / c.sampleRate, &oa};
      auto code = api.open(&stream, c.inputChannels ? &ip : nullptr,
                           c.outputChannels ? &op : nullptr, c.sampleRate,
                           c.periodFrames, paNoFlag, render, this);
      planar = true;
      if (code == paSampleFormatNotSupported) {
        ip.sampleFormat = op.sampleFormat = paFloat32;
        planar = false;
        code = api.open(&stream, c.inputChannels ? &ip : nullptr,
                        c.outputChannels ? &op : nullptr, c.sampleRate,
                        c.periodFrames, paNoFlag, render, this);
      }
      if (code != paNoError) {
        stream = nullptr;
        return status(code);
      }
      callback = legacy;
      extended = cb;
      user = data;
      const auto *info = api.streamInfo(stream);
      if (!info || !std::isfinite(info->sampleRate) || info->sampleRate <= 0 ||
          info->sampleRate > UINT32_MAX) {
        close();
        return Status::io_error;
      }
      config.sampleRate = std::uint32_t(std::llround(info->sampleRate));
      description.backend = AudioBackend::asio;
      description.provider = AudioProvider::portAudio;
      description.access = AudioAccess::exclusive;
      description.sampleRate = config.sampleRate;
      description.generation = c.generation;
      description.input.channels = c.inputChannels;
      description.output.channels = c.outputChannels;
      description.input.nativeChannels = di ? di->maxInputChannels : 0;
      description.output.nativeChannels = d ? d->maxOutputChannels : 0;
      description.input.activeChannels.assign(inputSelectors.begin(),
                                              inputSelectors.end());
      description.output.activeChannels.assign(outputSelectors.begin(),
                                               outputSelectors.end());
      auto latency = [](double seconds) {
        return std::isfinite(seconds) && seconds >= 0 && seconds < 86400
                   ? AudioTimingValue{std::uint64_t(seconds * 1e9),
                                      AudioMeasurementSource::backendDeclared,
                                      true}
                   : AudioTimingValue{};
      };
      if (c.inputChannels)
        description.input.latency = latency(info->inputLatency);
      if (c.outputChannels)
        description.output.latency = latency(info->outputLatency);
      (void)api.onFinished(stream, [](void *p) {
        static_cast<Impl *>(p)->notifications.fetch_or(2u);
      });
      return Status::ok;
    } catch (...) {
      close();
      return Status::allocation_failed;
    }
  }
};
AsioDriver::AsioDriver() : AsioDriver(detail::AsioApi{}) {}
AsioDriver::AsioDriver(const detail::AsioApi &api)
    : impl_(std::make_unique<Impl>(api)) {}
AsioDriver::~AsioDriver() = default;
cyaudio::AudioDeviceDriver AsioDriver::interface() noexcept {
  using namespace cyaudio;
  AudioDeviceDriver d;
  d.context = impl_.get();
  d.enumerate = [](void *p, std::vector<AudioDeviceInfo> &out) noexcept {
    return static_cast<Impl *>(p)->enumerate(out);
  };
  d.open = [](void *p, const AudioDeviceConfig &c, AudioDeviceCallback cb,
              void *data) noexcept {
    return static_cast<Impl *>(p)->open(c, cb, nullptr, data);
  };
  d.openWithContext = [](void *p, const AudioDeviceConfig &c,
                         AudioDeviceContextCallback cb, void *data) noexcept {
    return static_cast<Impl *>(p)->open(c, nullptr, cb, data);
  };
  d.start = [](void *p) noexcept {
    auto &s = *static_cast<Impl *>(p);
    if (!s.stream)
      return Status::not_prepared;
    auto r = status(s.api.start(s.stream));
    if (r == Status::ok)
      s.notifications.fetch_or(1u);
    return r;
  };
  d.close = [](void *p) noexcept { static_cast<Impl *>(p)->close(); };
  d.running = [](void *p) noexcept {
    auto &s = *static_cast<Impl *>(p);
    return s.stream && s.api.active(s.stream) > 0;
  };
  d.sampleRate = [](void *p) noexcept {
    auto &s = *static_cast<Impl *>(p);
    return s.stream ? s.config.sampleRate : 0u;
  };
  d.notifications = [](void *p) noexcept {
    return static_cast<Impl *>(p)->notifications.exchange(0);
  };
  d.description = [](void *p, AudioStreamDescription &out) noexcept {
    auto &s = *static_cast<Impl *>(p);
    if (!s.stream)
      return Status::not_prepared;
    try {
      out = s.description;
      return Status::ok;
    } catch (...) {
      return Status::allocation_failed;
    }
  };
  d.capabilities = [](void *p, std::string_view id, bool capture,
                      AudioDeviceCapabilities &out) noexcept {
    auto &s = *static_cast<Impl *>(p);
    AsioDeviceDetails details;
    auto r = s.details(id, details);
    if (r != Status::ok)
      return r;
    try {
      AudioDeviceCapabilities next;
      next.channelNames = capture ? details.inputNames : details.outputNames;
      next.channelNamesKnown = true;
      next.duplexKnown = true;
      next.duplex = !details.inputNames.empty() && !details.outputNames.empty();
      if (!details.bufferSizes.empty()) {
        next.bufferRangeKnown = true;
        next.minimumBufferFrames = details.bufferSizes.front();
        next.maximumBufferFrames = details.bufferSizes.back();
      }
      out = std::move(next);
      return Status::ok;
    } catch (...) {
      return Status::allocation_failed;
    }
  };
  return d;
}
cyaudio::Status AsioDriver::details(std::string_view id,
                                    AsioDeviceDetails &out) noexcept {
  return impl_->details(id, out);
}
cyaudio::Status AsioDriver::showControlPanel(std::string_view id,
                                             void *parent) noexcept {
  auto r = impl_->ensure();
  if (r != cyaudio::Status::ok)
    return r;
  if (impl_->stream)
    return cyaudio::Status::transition_in_progress;
  return status(impl_->api.panel(impl_->find(id, false), parent));
}
} // namespace cydaw::platform
