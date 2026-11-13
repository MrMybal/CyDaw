#include "NativeHostedPlugin.h"
#include <cstring>
#include <cydaw/base/LegacyPluginState.h>
#include <filesystem>
#include <stdexcept>
namespace cydaw::plugins {
using namespace cyaudio;
namespace {
std::filesystem::path pathFor(const base::File &file) {
  return std::filesystem::u8path(file.getFullPathName().toStdString());
}
base::Text failure(Status status) {
  return "CyAudioCore plugin: status " + base::Text(int(status));
}
} // namespace
base::Text NativeParameter::getCurrentValueAsText() const {
  std::string text;
  return owner->plugin_.parameterText(info.id, value.load(), text) == Status::ok
             ? base::Text(text)
             : base::Text(value.load(), 3);
}
NativeHostedPlugin::NativeHostedPlugin() {
  if (events_.prepare(4096, 256 * 1024) != Status::ok ||
      result_.prepare(4096, 256 * 1024) != Status::ok)
    throw std::bad_alloc();
}
NativeHostedPlugin::~NativeHostedPlugin() { releaseResources(); }
bool NativeHostedPlugin::load(const base::File &file, const base::Text &id,
                              PluginFormat format, const base::Text &formatName,
                              base::Text &error) {
  auto status = createPluginProvider(format, provider_);
  PluginHostCallbacks host;
  host.context = this;
  host.resizeEditor = [](void *context, PluginEditorSize size) noexcept {
    auto &p = *static_cast<NativeHostedPlugin *>(context);
    if (!p.editor_ || !size.width || !size.height || size.width > 16384 ||
        size.height > 16384)
      return false;
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
  if (status == Status::ok)
    status = provider_->create(pathFor(file), id.toStdString(), host, plugin_);
  if (status != Status::ok) {
    error = format == PluginFormat::jsfx ? nativeScriptDiagnostics(file)
                                         : base::Text{};
    if (error.isEmpty())
      error = failure(status);
    return false;
  }
  description.pluginFormatName=formatName;
  description.fileOrIdentifier=file.getFullPathName();
  try { refreshDescription(); } catch(const std::exception& e){error=e.what();return false;}
  if(format==PluginFormat::lv2)loadLv2Programs(file);
  return true;
}
void NativeHostedPlugin::refreshDescription(){
  const auto &d=*plugin_.description();
  description.name = base::Text(d.plugin.name);
  description.descriptiveName = base::Text(d.plugin.id);
  description.manufacturerName = base::Text(d.plugin.vendor);
  description.version = base::Text(d.plugin.version);
  description.numInputChannels=description.numOutputChannels=0;
  parameters_.clear();controls_.clear();preset_=-1;
  for (const auto &p : d.inputs)
    description.numInputChannels += int(p.channels);
  for (const auto &p : d.outputs)
    description.numOutputChannels += int(p.channels);
  if (description.numInputChannels > 256 ||
      description.numOutputChannels > 256) {
    throw std::runtime_error("Plugin exceeds 256 channels");
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
  }
}
void NativeHostedPlugin::prepareToPlay(double rate, int frames) {
  releaseResources();
  if (frames <= 0 || !std::isfinite(rate) || rate <= 0)
    throw std::invalid_argument("Invalid plugin audio configuration");
  options_ = {};
  options_.sampleRate = rate;
  options_.maximumFrames = unsigned(frames);
  options_.maximumEvents = 4096;
  options_.offline = offline_;
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
  latency_ = int(plugin_.timing().latencyFrames);
  for (auto *p : parameters_)
    p->sent = std::numeric_limits<double>::quiet_NaN();
}
void NativeHostedPlugin::releaseResources() {
  if (plugin_.lifecycle() == PluginLifecycle::processing)
    (void)plugin_.stop();
  if (plugin_.lifecycle() == PluginLifecycle::active)
    (void)plugin_.deactivate();
  prepared_ = false;
}
void NativeHostedPlugin::processBlock(audio::SampleBuffer<float> &buffer,
                                      midi::EventBuffer &midi) {
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
  const auto status = plugin_.process(process);
  buffer.clear();
  midi.clear();
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
  channel = 0;
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
}
void NativeHostedPlugin::getStateInformation(base::MemoryBlock &block) {
  const bool resume = prepared_;
  releaseResources();
  std::vector<std::uint8_t> state;
  const auto status = plugin_.saveState(state);
  block.reset();
  if (status == Status::ok)
    block.append(state.data(), state.size());
  if (resume)
    prepareToPlay(options_.sampleRate, int(options_.maximumFrames));
  if (status != Status::ok)
    throw std::runtime_error(failure(status).toStdString());
}
void NativeHostedPlugin::setStateInformation(const void *data, int size) {
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
    syncValues();
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
  // The worker serializes this main-thread handoff with audio processing.
  // Core reports requests; the host must perform the deactivate/refresh/restart.
  std::uint32_t flags{};
  if(plugin_.pumpMainThread(flags)!=Status::ok)return;
  if(flags&(pluginRestart|pluginParametersChanged|pluginPortsChanged)){
    const bool resume=prepared_;const auto options=options_;
    releaseResources();
    const auto status=plugin_.refresh();
    if(status!=Status::ok)throw std::runtime_error(failure(status).toStdString());
    refreshDescription();
    if(editor_)editor_->refreshParameters();
    if(resume)prepareToPlay(options.sampleRate,int(options.maximumFrames));
  }
  latency_=int(plugin_.timing().latencyFrames);
  if(editor_){
    if(flags&pluginEditorClosed)editor_->closeRequested();
    else if(flags&pluginEditorShow)editor_->setVisible(true);
    else if(flags&pluginEditorHide)editor_->setVisible(false);
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
  if (createPluginProvider(format_, provider) != Status::ok)
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
