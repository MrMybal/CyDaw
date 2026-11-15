#include "NativeHostedPlugin.h"
#include <chrono>
#include <cstring>
#include <cydaw/base/DispatchPause.h>
#include <cydaw/base/LegacyPluginState.h>
#include <filesystem>
#include <stdexcept>
#include <thread>
namespace cydaw::plugins {
using namespace cyaudio;
namespace {
std::filesystem::path pathFor(const base::File &file) {
  return std::filesystem::u8path(file.getFullPathName().toStdString());
}
PluginIsolationOptions isolationOptions(const base::File &worker = {}) {
  PluginIsolationOptions options;
  options.workerExecutable = pathFor(
      worker == base::File{}
          ? base::File::getSpecialLocation(base::File::currentExecutableFile)
          : worker);
  options.scheduling = PluginScheduling::sameCycle;
  options.maximumChannelsPerDirection = 64;
  options.maximumPayloadBytes = 256 * 1024;
  return options;
}
base::Text failure(Status status) {
  return "CyAudioCore plugin: status " + base::Text(int(status));
}
} // namespace
base::Text NativeParameter::getCurrentValueAsText() const {
  base::ScopedEventDispatchPause pause;
  const auto current = value.load();
  if (current == textValue_)
    return text_;
  std::string text;
  if (owner->plugin_.parameterText(info.id, current, text) == Status::ok) {
    textValue_ = current;
    text_ = base::Text(text);
    return text_;
  }
  return base::Text(current, 3);
}
NativeHostedPlugin::NativeHostedPlugin() {
  if (events_.prepare(4096, 256 * 1024) != Status::ok ||
      result_.prepare(4096, 256 * 1024) != Status::ok)
    throw std::bad_alloc();
}
NativeHostedPlugin::~NativeHostedPlugin() { releaseResources(); }
void NativeHostedPlugin::beginControl() {
  if (controlDepth_++ == 0 && handoff_)
    handoff_(handoffContext_, true);
}
void NativeHostedPlugin::endControl() {
  if (--controlDepth_ == 0 && handoff_)
    handoff_(handoffContext_, false);
}
void NativeHostedPlugin::resetRealtimePipeline() noexcept {
  if (audioPending_) {
    (void)plugin_.discard(audioTicket_);
    audioPending_ = false;
  }
  streamFill_ = 0;
  streamReady_ = false;
}

bool NativeHostedPlugin::load(const base::File &file, const base::Text &id,
                              PluginFormat format, const base::Text &formatName,
                              base::Text &error) {
  base::ScopedEventDispatchPause pause;
  auto status = createPluginProvider(format, provider_,
                                     isolationOptions(workerExecutable_));
  PluginHostCallbacks host;
  host.context = this;
  host.resizeEditor = [](void *context, PluginEditorSize size) noexcept {
    auto &p = *static_cast<NativeHostedPlugin *>(context);
    if (!p.editor_ || !size.width || !size.height || size.width > 16384 ||
        size.height > 16384)
      return false;
    // Core completes its resize RPC after the host callback returns. Keep the
    // callback owner parked until the outer main-thread pump has finished too.
    if (p.polling_ && !p.deferredEditorControl_) {
      p.beginControl();
      p.deferredEditorControl_ = true;
    }
    p.editor_->resize(size.width, size.height);
    return true;
  };
  host.edited = [](void *context, const PluginEvent &event) noexcept {
    auto &p = *static_cast<NativeHostedPlugin *>(context);
    if (event.kind == PluginEventKind::parameterValue)
      for (auto *c : p.parameters_)
        if (c->info.id == event.id) {
          c->value = event.value;
          c->sent = event.value;
          break;
        }
  };
  auto selectedId = id.toStdString();
  if (status == Status::ok && selectedId.empty()) {
    std::vector<PluginDescriptor> descriptors;
    status = provider_->scan(pathFor(file), descriptors);
    if (status == Status::ok && descriptors.empty())
      status = Status::unsupported_format;
    if (status == Status::ok)
      selectedId = descriptors.front().id;
  }
  if (status == Status::ok)
    status = provider_->create(pathFor(file), selectedId, host, plugin_);
  if (status != Status::ok) {
    error = format == PluginFormat::jsfx ? nativeScriptDiagnostics(file)
                                         : base::Text{};
    if (error.isEmpty())
      error = failure(status);
    return false;
  }
  description.pluginFormatName = formatName;
  description.fileOrIdentifier = file.getFullPathName();
  try {
    refreshDescription();
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
  if (format == PluginFormat::lv2)
    loadLv2Programs(file);
  return true;
}
void NativeHostedPlugin::refreshDescription() {
  const auto &d = *plugin_.description();
  description.name = base::Text(d.plugin.name);
  description.descriptiveName = base::Text(d.plugin.id);
  description.manufacturerName = base::Text(d.plugin.vendor);
  description.version = base::Text(d.plugin.version);
  description.numInputChannels = description.numOutputChannels = 0;
  parameters_.clear();
  controls_.clear();
  preset_ = -1;
  for (const auto &p : d.inputs)
    description.numInputChannels += int(p.channels);
  for (const auto &p : d.outputs)
    description.numOutputChannels += int(p.channels);
  if (description.numInputChannels > 64 || description.numOutputChannels > 64) {
    throw std::runtime_error(
        "Plugin exceeds the SDK isolation limit of 64 channels per direction");
  }
  description.isInstrument =
      description.numInputChannels == 0 && !d.noteInputs.empty();
  description.category = description.isInstrument ? "Instrument" : "Effect";
  for (const auto &info : d.parameters) {
    auto p = std::make_unique<NativeParameter>();
    p->owner = this;
    p->info = info;
    p->value = info.defaultValue;
    double value;
    if (plugin_.parameterValue(info.id, value) == Status::ok)
      p->value = value;
    p->sent = p->value.load();
    if (preset_ < 0 && info.stepped &&
        base::Text(info.name).equalsIgnoreCase("Preset") &&
        info.maximum - info.minimum < 2048)
      preset_ = int(parameters_.size());
    parameters_.add(p.get());
    controls_.push_back(std::move(p));
  }
}

void NativeHostedPlugin::syncValues() {
  for (auto *p : parameters_) {
    if (const auto *d = plugin_.description())
      for (const auto &info : d->parameters)
        if (info.id == p->info.id) {
          p->info = info;
          break;
        }
    double value = p->info.defaultValue;
    if (plugin_.parameterValue(p->info.id, value) == Status::ok)
      p->value = value;
    p->sent = std::numeric_limits<double>::quiet_NaN();
    p->textValue_ = std::numeric_limits<double>::quiet_NaN();
  }
}
void NativeHostedPlugin::prepareToPlay(double rate, int frames) {
  base::ScopedEventDispatchPause pause;
  releaseResources();
  if (frames <= 0 || !std::isfinite(rate) || rate <= 0)
    throw std::invalid_argument("Invalid plugin audio configuration");
  options_ = {};
  options_.sampleRate = rate;
  options_.maximumFrames = unsigned(frames);
  options_.maximumEvents = 4096;
  options_.offline = offline_;
  // Let each format's native contract decide whether suspension is safe.
  // In particular, do not force unknown-tail effects or instruments to sleep.
  options_.sleep.mode = PluginSleepMode::automatic;
  const auto &d = *plugin_.description();
  inputStorage_.resize(d.inputs.size());
  outputStorage_.resize(d.outputs.size());
  inputs_.resize(d.inputs.size());
  outputs_.resize(d.outputs.size());
  for (std::size_t i = 0; i < inputStorage_.size(); ++i)
    if (inputStorage_[i].resize(d.inputs[i].channels, frames) != Status::ok)
      throw std::bad_alloc();
  for (std::size_t i = 0; i < outputStorage_.size(); ++i)
    if (outputStorage_[i].resize(d.outputs[i].channels, frames) != Status::ok)
      throw std::bad_alloc();
  auto status = plugin_.activate(options_);
  if (status == Status::ok)
    status = plugin_.start();
  if (status != Status::ok) {
    releaseResources();
    throw std::runtime_error(failure(status).toStdString());
  }
  prepared_ = true;
  streamFill_ = 0;
  streamReady_ = false;
  streamFailure_ = int(Status::ok);
  for (std::size_t i = 0; i < inputs_.size(); ++i)
    inputs_[i] = inputStorage_[i].block(unsigned(frames));
  for (std::size_t i = 0; i < outputs_.size(); ++i) {
    outputs_[i] = outputStorage_[i].block(unsigned(frames));
    for (std::size_t ch = 0; ch < outputs_[i].channels(); ++ch)
      std::fill(outputs_[i].channel(ch).begin(), outputs_[i].channel(ch).end(),
                0.f);
  }
  latency_ = int(plugin_.timing().latencyFrames);
}
void NativeHostedPlugin::releaseResources() {
  base::ScopedEventDispatchPause pause;
  if (audioPending_) {
    (void)plugin_.discard(audioTicket_);
    audioPending_ = false;
  }
  if (plugin_.lifecycle() == PluginLifecycle::processing)
    (void)plugin_.stop();
  if (plugin_.lifecycle() == PluginLifecycle::active)
    (void)plugin_.deactivate();
  prepared_ = false;
}
void NativeHostedPlugin::submitBlock(audio::SampleBuffer<float> &buffer,
                                     midi::EventBuffer &midi) {
  if (audioPending_)
    throw std::runtime_error("Plugin already has an outstanding audio block");
  const auto frames = unsigned(buffer.getNumSamples());
  if (!prepared_ || frames > options_.maximumFrames) {
    buffer.clear();
    midi.clear();
    return;
  }
  unsigned channel = 0;
  for (std::size_t bus = 0; bus < inputs_.size(); ++bus) {
    auto b = inputStorage_[bus].block(frames);
    for (std::size_t ch = 0; ch < b.channels(); ++ch, ++channel) {
      if (channel < unsigned(buffer.getNumChannels()))
        std::copy_n(buffer.getReadPointer(int(channel)), frames,
                    b.channel(ch).data());
      else
        std::fill(b.channel(ch).begin(), b.channel(ch).end(), 0.f);
    }
    inputs_[bus] = b;
  }
  for (std::size_t i = 0; i < outputs_.size(); ++i)
    outputs_[i] = outputStorage_[i].block(frames);
  events_.reset();
  for (auto *p : parameters_) {
    const double value = p->value.load();
    if (value != p->sent && !p->info.readOnly) {
      PluginEvent event;
      event.id = p->info.id;
      event.value = value;
      if (events_.add(event) == Status::ok)
        p->sent = value;
    }
  }
  for (const auto m : midi) {
    PluginEvent event;
    event.kind =
        m.numBytes > 3 ? PluginEventKind::sysex : PluginEventKind::midi1;
    event.port = 0;
    event.frame =
        unsigned(std::clamp(m.samplePosition, 0, std::max(0, int(frames) - 1)));
    event.bytes = {m.data, std::size_t(m.numBytes)};
    (void)events_.add(event);
  }
  PluginProcess<float> process{inputs_,  outputs_, frames,
                               &events_, &result_, transport_};
  const auto status = plugin_.submit(process, audioTicket_);
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
  audioPending_ = true;
}
cyaudio::Status
NativeHostedPlugin::collectBlock(audio::SampleBuffer<float> &buffer,
                                 midi::EventBuffer &midi) {
  if (!audioPending_)
    return Status::invalid_argument;
  const auto frames = unsigned(buffer.getNumSamples());
  PluginProcess<float> process{inputs_,  outputs_, frames,
                               &events_, &result_, transport_};
  const auto status = plugin_.collect(process, audioTicket_);
  if (status == Status::would_block)
    return status;
  audioPending_ = false;
  buffer.clear();
  midi.clear();
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
  unsigned channel = 0;
  for (const auto &bus : outputs_)
    for (std::size_t ch = 0; ch < bus.channels(); ++ch, ++channel)
      if (channel < unsigned(buffer.getNumChannels()))
        std::copy_n(bus.channel(ch).data(), frames,
                    buffer.getWritePointer(int(channel)));
  for (std::size_t i = 0; i < result_.size(); ++i)
    if (const auto *e = result_.event(i)) {
      if (e->kind == PluginEventKind::midi1 ||
          e->kind == PluginEventKind::sysex)
        midi.addEvent(e->bytes.data(), int(e->bytes.size()), int(e->frame));
      else if (e->kind == PluginEventKind::noteOn && e->key >= 0 &&
               e->key < 128)
        midi.addEvent(
            midi::Message::noteOn(std::clamp(int(e->channel) + 1, 1, 16),
                                  e->key, float(e->value)),
            int(e->frame));
      else if ((e->kind == PluginEventKind::noteOff ||
                e->kind == PluginEventKind::noteChoke) &&
               e->key >= 0 && e->key < 128)
        midi.addEvent(midi::Message::noteOff(
                          std::clamp(int(e->channel) + 1, 1, 16), e->key),
                      int(e->frame));
      else if (e->kind == PluginEventKind::parameterValue)
        for (auto *p : parameters_)
          if (p->info.id == e->id) {
            p->value = e->value;
            p->sent = e->value;
            break;
          }
    }
  return Status::ok;
}
void NativeHostedPlugin::processBlock(audio::SampleBuffer<float> &buffer,
                                      midi::EventBuffer &midi) {
  submitBlock(buffer, midi);
  if (!audioPending_)
    return;
  // Probe/test owner only. Realtime uses submitBlock/collectBlock in its event
  // loop.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (collectBlock(buffer, midi) == Status::would_block) {
    if (std::chrono::steady_clock::now() >= deadline) {
      (void)plugin_.discard(audioTicket_);
      audioPending_ = false;
      throw std::runtime_error("Plugin validation audio timeout");
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}
bool NativeHostedPlugin::processRealtime(audio::SampleBuffer<float> &buffer,
                                         int start, int count,
                                         const PluginTransport &time) noexcept {
  if (start < 0 || count <= 0 || start > buffer.getNumSamples() - count)
    return false;
  if (!prepared_) {
    buffer.clear(start, count);
    streamFailure_ = int(Status::not_prepared);
    return false;
  }
  const auto quantum = options_.maximumFrames;
  bool allReady = true;
  int consumed = 0;
  while (consumed < count) {
    if (streamFill_ == 0) {
      streamReady_ = false;
      if (audioPending_) {
        PluginProcess<float> process{inputs_,  outputs_, quantum,
                                     &events_, &result_, &streamTransport_};
        const auto status = plugin_.collect(process, audioTicket_);
        if (status == Status::would_block) {
          // A late reply must never be played at a later timeline position.
          (void)plugin_.discard(audioTicket_);
          streamMissed_.fetch_add(1, std::memory_order_relaxed);
        } else if (status != Status::ok) {
          streamFailure_.store(int(status), std::memory_order_relaxed);
        } else {
          streamReady_ = true;
          for (std::size_t i = 0; i < result_.size(); ++i)
            if (const auto *e = result_.event(i);
                e && e->kind == PluginEventKind::parameterValue)
              for (auto *p : parameters_)
                if (p->info.id == e->id) {
                  // Do not overwrite a more recent host automation value with
                  // a response belonging to the preceding quantum.
                  double expected = p->sent.load();
                  if (p->value.compare_exchange_strong(expected, e->value))
                    p->sent = e->value;
                  break;
                }
        }
        audioPending_ = false;
      }
      events_.reset();
      streamTransport_ = time;
      streamTransport_.seconds += consumed / options_.sampleRate;
      streamTransport_.songFrame += consumed;
      streamTransport_.beats +=
          consumed / options_.sampleRate * time.tempo / 60.;
    }
    const unsigned frames =
        std::min(quantum - streamFill_, unsigned(count - consumed));
    for (auto *p : parameters_) {
      const double value = p->value.load(std::memory_order_relaxed);
      if (!p->info.readOnly &&
          value != p->sent.load(std::memory_order_relaxed)) {
        PluginEvent event;
        event.id = p->info.id;
        event.value = value;
        event.frame = streamFill_;
        if (events_.add(event) == Status::ok)
          p->sent = value;
      }
    }
    unsigned channel = 0;
    for (auto &storage : inputStorage_) {
      auto bus = storage.block(quantum);
      for (std::size_t ch = 0; ch < bus.channels(); ++ch, ++channel) {
        auto *destination = bus.channel(ch).data() + streamFill_;
        if (channel < unsigned(buffer.getNumChannels()))
          std::copy_n(buffer.getReadPointer(int(channel), start + consumed),
                      frames, destination);
        else
          std::fill_n(destination, frames, 0.f);
      }
    }
    // Input has been copied before in-place replacement by delayed output.
    channel = 0;
    for (const auto &bus : outputs_)
      for (std::size_t ch = 0; ch < bus.channels(); ++ch, ++channel)
        if (channel < unsigned(buffer.getNumChannels())) {
          auto *destination =
              buffer.getWritePointer(int(channel), start + consumed);
          if (streamReady_)
            std::copy_n(bus.channel(ch).data() + streamFill_, frames,
                        destination);
          else
            std::fill_n(destination, frames, 0.f);
        }
    for (; channel < unsigned(buffer.getNumChannels()); ++channel)
      std::fill_n(buffer.getWritePointer(int(channel), start + consumed),
                  frames, 0.f);
    allReady = allReady && streamReady_;
    streamFill_ += frames;
    consumed += int(frames);
    if (streamFill_ == quantum) {
      PluginProcess<float> process{inputs_,  outputs_, quantum,
                                   &events_, &result_, &streamTransport_};
      const auto status = plugin_.submit(process, audioTicket_);
      audioPending_ = status == Status::ok;
      if (!audioPending_) {
        streamMissed_.fetch_add(1, std::memory_order_relaxed);
        if (status != Status::would_block)
          streamFailure_.store(int(status), std::memory_order_relaxed);
        for (auto *p : parameters_)
          p->sent = std::numeric_limits<double>::quiet_NaN();
      }
      streamFill_ = 0;
    }
  }
  return allReady;
}
void NativeHostedPlugin::getStateInformation(base::MemoryBlock &block) {
  base::ScopedEventDispatchPause pause;
  const bool resume = prepared_;
  const auto options = options_;
  const auto drainEdits = [&] {
    if (audioPending_) {
      (void)plugin_.discard(audioTicket_);
      audioPending_ = false;
    }
    // VST3 flush queues processor changes for its next block. Drain on this
    // quiesced control owner, never wait in the device callback.
    audio::SampleBuffer<float> silence(
        std::max({2, getTotalNumInputChannels(), getTotalNumOutputChannels()}),
        int(options.maximumFrames));
    silence.clear();
    midi::EventBuffer midi;
    processBlock(silence, midi);
  };
  if (resume &&
      std::any_of(parameters_.begin(), parameters_.end(), [](auto *p) {
        return !p->info.readOnly && p->value.load() != p->sent.load();
      }))
    drainEdits();
  releaseResources();
  std::vector<std::uint8_t> state;
  auto status = plugin_.saveState(state);
  if (resume && status == Status::would_block) {
    // An editor may have queued a change directly inside the isolated plugin.
    prepareToPlay(options.sampleRate, int(options.maximumFrames));
    drainEdits();
    releaseResources();
    status = plugin_.saveState(state);
  }
  block.reset();
  if (status == Status::ok)
    block.append(state.data(), state.size());
  if (resume)
    prepareToPlay(options.sampleRate, int(options.maximumFrames));
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
}
void NativeHostedPlugin::setStateInformation(const void *data, int size) {
  base::ScopedEventDispatchPause pause;
  if (!data || size <= 0 || size > 64 * 1024 * 1024)
    return;
  const bool resume = prepared_;
  releaseResources();
  std::span<const std::uint8_t> state{static_cast<const std::uint8_t *>(data),
                                      std::size_t(size)};
  std::vector<std::uint8_t> wrapped;
  auto wrap = [&](const void *a, std::size_t n, const void *b, std::size_t m) {
    wrapped.assign(24, 0);
    std::memcpy(wrapped.data(), "CyDSP.VST3State1", 16);
    for (unsigned i = 0; i < 4; ++i) {
      wrapped[16 + i] = std::uint8_t(n >> (8 * i));
      wrapped[20 + i] = std::uint8_t(m >> (8 * i));
    }
    const auto *p = static_cast<const std::uint8_t *>(a);
    if (n)
      wrapped.insert(wrapped.end(), p, p + n);
    p = static_cast<const std::uint8_t *>(b);
    if (m)
      wrapped.insert(wrapped.end(), p, p + m);
    state = wrapped;
  };
  if (description.pluginFormatName == "VST3") {
    if (auto xml = base::legacyXmlFromBinary(data, size))
      if (auto *component = xml->getChildByName("IComponent")) {
        base::MemoryBlock audio, controller;
        if (!base::decodeLegacyStateBytes(component->getAllSubText(), audio))
          return;
        if (auto *c = xml->getChildByName("IController"))
          if (!base::decodeLegacyStateBytes(c->getAllSubText(), controller))
            return;
        wrap(audio.getData(), audio.getSize(), controller.getData(),
             controller.getSize());
      }
    if (state[0] == '{' &&
        description.manufacturerName.containsIgnoreCase("cydaw"))
      wrap(state.data(), state.size(), nullptr, 0);
  }
  const auto status = plugin_.loadState(state);
  if (status == Status::ok) {
    (void)plugin_.refresh();
    refreshDescription();
    if (editor_)
      editor_->refreshParameters();
  }
  if (resume)
    prepareToPlay(options_.sampleRate, int(options_.maximumFrames));
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
}
void NativeHostedPlugin::flushValues() {
  events_.reset();
  for (auto *p : parameters_)
    if (!p->info.readOnly) {
      PluginEvent e;
      e.id = p->info.id;
      e.value = p->value.load();
      (void)events_.add(e);
    }
  if (events_.size())
    (void)plugin_.flush(events_, result_);
  for (auto *p : parameters_)
    p->sent = std::numeric_limits<double>::quiet_NaN();
}
bool NativeHostedPlugin::hasEditor() const {
  return plugin_.description() &&
         (plugin_.description()->editor || !parameters_.isEmpty());
}
std::unique_ptr<NativePluginEditor> NativeHostedPlugin::createEditor() {
  return hasEditor() ? std::make_unique<NativePluginEditor>(*this) : nullptr;
}
void NativeHostedPlugin::pump() {
  // Defer reconfiguration until the outstanding ticket has been consumed.
  if (audioPending_)
    return;
  // The worker serializes this main-thread handoff with audio processing.
  // Core reports requests; the host must perform the
  // deactivate/refresh/restart.
  std::uint32_t flags{};
  if (pollRequests(flags) != Status::ok)
    return;
  applyRequests(flags);
}
Status NativeHostedPlugin::pollRequests(std::uint32_t &flags) noexcept {
  base::ScopedEventDispatchPause pause;
  polling_ = true;
  const auto status = plugin_.pumpMainThread(flags);
  polling_ = false;
  if (deferredEditorControl_) {
    deferredEditorControl_ = false;
    endControl();
  }
  return status;
}
void NativeHostedPlugin::applyRequests(std::uint32_t flags) {
  if (flags & (pluginRestart | pluginParametersChanged | pluginPortsChanged)) {
    const bool resume = prepared_;
    const auto options = options_;
    releaseResources();
    const auto status = plugin_.refresh();
    if (status != Status::ok)
      throw std::runtime_error(failure(status).toStdString());
    refreshDescription();
    if (editor_)
      editor_->refreshParameters();
    if (resume)
      prepareToPlay(options.sampleRate, int(options.maximumFrames));
  }
  latency_ = int(plugin_.timing().latencyFrames);
  if (editor_) {
    if (flags & pluginEditorClosed)
      editor_->closeRequested();
    else if (flags & pluginEditorShow)
      editor_->setVisible(true);
    else if (flags & pluginEditorHide)
      editor_->setVisible(false);
  }
}

int NativeHostedPlugin::getNumPrograms() const {
  if (!programs_.empty())
    return int(programs_.size());
  if (preset_ < 0)
    return 1;
  const auto &p = parameters_[preset_]->info;
  return std::max(1, int(p.maximum - p.minimum) + 1);
}
int NativeHostedPlugin::getCurrentProgram() const {
  if (!programs_.empty())
    return currentProgram_;
  if (preset_ < 0)
    return 0;
  return std::clamp(int(std::round(parameters_[preset_]->value.load() -
                                   parameters_[preset_]->info.minimum)),
                    0, getNumPrograms() - 1);
}
base::Text NativeHostedPlugin::getProgramName(int i) const {
  base::ScopedEventDispatchPause pause;
  if (!programs_.empty())
    return i >= 0 && i < int(programs_.size()) ? programs_[i].name
                                               : base::Text{};
  if (preset_ < 0)
    return "Default";
  if (i < 0 || i >= getNumPrograms())
    return {};
  std::string text;
  const auto *p = parameters_[preset_];
  return const_cast<PluginInstance &>(plugin_).parameterText(
             p->info.id, p->info.minimum + i, text) == Status::ok
             ? base::Text(text)
             : base::Text(i + 1);
}
void NativeHostedPlugin::setCurrentProgram(int i) {
  base::ScopedEventDispatchPause pause;
  if (!programs_.empty()) {
    if (i >= 0 && i < int(programs_.size())) {
      const auto &state = programs_[i].state;
      setStateInformation(state.data(), int(state.size()));
      currentProgram_ = i;
    }
    return;
  }
  if (preset_ < 0 || i < 0 || i >= getNumPrograms())
    return;
  parameters_[preset_]->value = parameters_[preset_]->info.minimum + i;
  const bool resume = prepared_;
  releaseResources();
  flushValues();
  syncValues();
  if (resume)
    prepareToPlay(options_.sampleRate, int(options_.maximumFrames));
}
void NativePluginFormat::findAllTypesForFile(
    std::vector<std::unique_ptr<HostedMetadata>> &result,
    const base::Text &path) {
  lastError.clear();
  if (format_ == PluginFormat::jsfx) {
    NativeHostedPlugin instance;
    if (instance.load(base::File(path), "jsfx", format_, name_, lastError))
      result.push_back(std::make_unique<HostedMetadata>(instance.description));
    return;
  }
  std::unique_ptr<PluginProvider> provider;
  if (createPluginProvider(format_, provider, isolationOptions()) != Status::ok)
    return;
  std::vector<PluginDescriptor> list;
  if (provider->scan(pathFor(base::File(path)), list) != Status::ok)
    return;
  for (const auto &d : list) {
    NativeHostedPlugin instance;
    base::Text error;
    if (instance.load(base::File(path), base::Text(d.id), format_, name_,
                      error))
      result.push_back(std::make_unique<HostedMetadata>(instance.description));
  }
}
std::unique_ptr<NativeHostedPlugin>
NativePluginFormat::createInstanceFromDescription(const HostedMetadata &d,
                                                  double, int,
                                                  base::Text &error) {
  auto plugin = std::make_unique<NativeHostedPlugin>();
  if (!plugin->load(base::File(d.fileOrIdentifier), d.descriptiveName, format_,
                    name_, error))
    return {};
  return plugin;
}
} // namespace cydaw::plugins
