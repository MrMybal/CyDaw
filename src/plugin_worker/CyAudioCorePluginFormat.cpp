#include "CyAudioCorePluginFormat.h"
#include "HostedInstance.h"
#include <cstring>
#include <cyaudio/plugins/Plugin.h>
#include <filesystem>
#include <limits>
#if JUCE_WINDOWS
#include <juce_gui_extra/juce_gui_extra.h>
#include <windows.h>
#undef min
#undef max
#endif
namespace cydaw::plugins {
namespace {
using namespace cyaudio;
std::filesystem::path nativePath(const juce::String &text) {
#if JUCE_WINDOWS
  return std::filesystem::path(text.toWideCharPointer());
#else
  return std::filesystem::u8path(text.toStdString());
#endif
}
class CoreInstance;
class CoreEditor final : public juce::AudioProcessorEditor {
  CoreInstance &owner;
#if JUCE_WINDOWS
  juce::HWNDComponent surface;
  HWND window = nullptr;
#endif
public:
  explicit CoreEditor(CoreInstance &);
  ~CoreEditor() override;
  void resized() override;
};
class CoreInstance final : public HostedInstance, private juce::Timer {
public:
  PluginInstance plugin;
  std::unique_ptr<PluginProvider> provider;
  struct Control {
    PluginParameter info;
    HostedControl *value = nullptr;
    double sent = std::numeric_limits<double>::quiet_NaN();
  };
  std::vector<Control> controls;
  std::vector<AudioBuffer<float>> inputStorage, outputStorage;
  std::vector<AudioBlock<const float>> inputs;
  std::vector<AudioBlock<float>> outputs;
  PluginEventBuffer events, result;
  PluginPrepareOptions options;
  bool prepared = false;
  std::uint32_t requests = 0;
  CoreEditor *editor = nullptr;
  static constexpr std::size_t maximumEvents = 4096;
  bool load(const juce::File &file, const juce::String &id,
            juce::String &error) {
    auto r = createPluginProvider(PluginFormat::vst3, provider);
    PluginHostCallbacks host;
    host.context = this;
    host.resizeEditor = [](void *p, PluginEditorSize size) noexcept {
      auto &s = *static_cast<CoreInstance *>(p);
      if (!s.editor || !size.width || !size.height || size.width > 16384 ||
          size.height > 16384)
        return false;
      s.editor->setSize(int(size.width), int(size.height));
      return true;
    };
    host.edited = [](void *p, const PluginEvent &e) noexcept {
      auto &s = *static_cast<CoreInstance *>(p);
      for (auto &c : s.controls)
        if (c.info.id == e.id && e.kind == PluginEventKind::parameterValue) {
          c.value->value.store(e.value);
          c.sent = e.value;
          break;
        }
    };
    if (r == Status::ok)
      r = provider->create(nativePath(file.getFullPathName()), id.toStdString(),
                           host, plugin);
    if (r != Status::ok) {
      error = "CyAudioCore VST3: status " + juce::String(int(r));
      return false;
    }
    const auto &d = *plugin.description();
    description.name = juce::String::fromUTF8(d.plugin.name.c_str());
    description.descriptiveName = juce::String::fromUTF8(d.plugin.id.c_str());
    description.manufacturerName =
        juce::String::fromUTF8(d.plugin.vendor.c_str());
    description.version = juce::String::fromUTF8(d.plugin.version.c_str());
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = file.getFullPathName();
    description.uniqueId = description.descriptiveName.hashCode();
    unsigned in = 0, out = 0;
    for (const auto &p : d.inputs)
      in += p.channels;
    for (const auto &p : d.outputs)
      out += p.channels;
    if (in > 256 || out > 256) {
      error = "CyAudioCore VST3: channel count exceeds application capacity";
      return false;
    }
    description.isInstrument = in == 0 && !d.noteInputs.empty();
    description.category = description.isInstrument ? "Instrument" : "Effect";
    addPorts(int(in), int(out));
    for (const auto &p : d.parameters) {
      auto control = std::make_unique<HostedControl>(
          juce::String(p.id), juce::String::fromUTF8(p.name.c_str()), p.minimum,
          p.maximum, p.defaultValue);
      control->unit = juce::String::fromUTF8(p.units.c_str());
      control->automate = p.automatable && !p.readOnly;
      if (p.stepped && !p.normalized)
        control->steps = int(std::min(1000000., p.maximum - p.minimum + 1));
      control->display = [this, id = p.id](double value) {
        std::string text;
        return plugin.parameterText(id, value, text) == Status::ok
                   ? juce::String::fromUTF8(text.c_str())
                   : juce::String(value, 3);
      };
      double value = p.defaultValue;
      if (plugin.parameterValue(p.id, value) == Status::ok)
        control->value = value;
      controls.push_back(
          {p, control.get(), std::numeric_limits<double>::quiet_NaN()});
      addHostedParameter(std::move(control));
    }
    (void)events.prepare(maximumEvents, 65536);
    (void)result.prepare(maximumEvents, 65536);
    startTimerHz(60);
    return true;
  }
  ~CoreInstance() override {
    stopTimer();
    releaseResources();
  }
  void syncValues() {
    for (auto &c : controls) {
      if (const auto *d = plugin.description())
        for (const auto &p : d->parameters)
          if (p.id == c.info.id) {
            c.info = p;
            c.value->name = juce::String::fromUTF8(p.name.c_str());
            c.value->unit = juce::String::fromUTF8(p.units.c_str());
            c.value->automate = p.automatable && !p.readOnly;
            break;
          }
      double value = c.info.defaultValue;
      if (plugin.parameterValue(c.info.id, value) == Status::ok)
        c.value->value = value;
      c.sent = std::numeric_limits<double>::quiet_NaN();
    }
  }
  void prepareToPlay(double rate, int block) override {
    releaseResources();
    if (block <= 0)
      return;
    setRateAndBufferSizeDetails(rate, block);
    options.sampleRate = rate;
    options.maximumFrames = unsigned(block);
    options.maximumEvents = maximumEvents;
    options.offline = isNonRealtime();
    const auto &d = *plugin.description();
    inputStorage.resize(d.inputs.size());
    outputStorage.resize(d.outputs.size());
    inputs.resize(d.inputs.size());
    outputs.resize(d.outputs.size());
    for (std::size_t i = 0; i < inputStorage.size(); ++i)
      if (inputStorage[i].resize(d.inputs[i].channels, block) != Status::ok)
        return;
    for (std::size_t i = 0; i < outputStorage.size(); ++i)
      if (outputStorage[i].resize(d.outputs[i].channels, block) != Status::ok)
        return;
    if (plugin.activate(options) != Status::ok)
      return;
    if (plugin.start() != Status::ok) {
      (void)plugin.deactivate();
      return;
    }
    prepared = true;
    setLatencySamples(int(plugin.timing().latencyFrames));
    for (auto &c : controls)
      c.sent = std::numeric_limits<double>::quiet_NaN();
  }
  void releaseResources() override {
    if (plugin.lifecycle() == PluginLifecycle::processing)
      (void)plugin.stop();
    if (plugin.lifecycle() == PluginLifecycle::active)
      (void)plugin.deactivate();
    prepared = false;
  }
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midi) override {
    const auto frames = unsigned(buffer.getNumSamples());
    if (!prepared || frames > options.maximumFrames) {
      buffer.clear();
      midi.clear();
      return;
    }
    unsigned channel = 0;
    for (std::size_t bus = 0; bus < inputStorage.size(); ++bus) {
      auto b = inputStorage[bus].block(frames);
      for (std::size_t ch = 0; ch < b.channels(); ++ch, ++channel) {
        if (channel < unsigned(buffer.getNumChannels()))
          std::copy_n(buffer.getReadPointer(int(channel)), frames,
                      b.channel(ch).data());
        else
          std::fill(b.channel(ch).begin(), b.channel(ch).end(), 0.f);
      }
      inputs[bus] = b;
    }
    for (std::size_t bus = 0; bus < outputs.size(); ++bus)
      outputs[bus] = outputStorage[bus].block(frames);
    events.reset();
    for (auto &c : controls) {
      const auto value = c.value->value.load();
      if (value != c.sent && !c.info.readOnly) {
        PluginEvent e;
        e.id = c.info.id;
        e.value = value;
        (void)events.add(e);
        c.sent = value;
      }
    }
    for (const auto metadata : midi) {
      PluginEvent e;
      e.kind = metadata.numBytes > 3 ? PluginEventKind::sysex
                                     : PluginEventKind::midi1;
      e.port = 0;
      e.frame = unsigned(
          std::clamp(metadata.samplePosition, 0, std::max(0, int(frames) - 1)));
      e.bytes = {metadata.data, std::size_t(metadata.numBytes)};
      (void)events.add(e);
    }
    PluginTransport transport;
    if (auto *head = getPlayHead())
      if (auto pos = head->getPosition()) {
        transport.playing = pos->getIsPlaying();
        transport.recording = pos->getIsRecording();
        transport.looping = pos->getIsLooping();
        if (auto value = pos->getBpm()) {
          transport.tempo = *value;
          transport.hasTempo = true;
        }
        if (auto value = pos->getTimeInSamples()) {
          transport.songFrame = *value;
          transport.hasSongFrame = true;
        }
        if (auto value = pos->getTimeInSeconds()) {
          transport.seconds = *value;
          transport.hasSeconds = true;
        }
        if (auto value = pos->getPpqPosition()) {
          transport.beats = *value;
          transport.hasBeats = true;
        }
        if (auto value = pos->getTimeSignature()) {
          transport.numerator = std::uint16_t(value->numerator);
          transport.denominator = std::uint16_t(value->denominator);
          transport.hasSignature = true;
        }
        if (auto value = pos->getLoopPoints()) {
          transport.loopStartBeats = value->ppqStart;
          transport.loopEndBeats = value->ppqEnd;
        }
      }
    PluginProcess<float> p{inputs,  outputs, frames,
                           &events, &result, &transport};
    const auto status = plugin.process(p);
    buffer.clear();
    midi.clear();
    if (status != Status::ok)
      return;
    channel = 0;
    for (const auto &bus : outputs)
      for (std::size_t ch = 0; ch < bus.channels(); ++ch, ++channel)
        if (channel < unsigned(buffer.getNumChannels()))
          std::copy_n(bus.channel(ch).data(), frames,
                      buffer.getWritePointer(int(channel)));
    for (std::size_t i = 0; i < result.size(); ++i)
      if (auto *e = result.event(i)) {
        if (e->kind == PluginEventKind::noteOn && e->key >= 0 && e->key <= 127)
          midi.addEvent(juce::MidiMessage::noteOn(
                            std::clamp(int(e->channel) + 1, 1, 16), int(e->key),
                            float(std::clamp(e->value, 0., 1.))),
                        int(e->frame));
        if ((e->kind == PluginEventKind::noteOff ||
             e->kind == PluginEventKind::noteChoke) &&
            e->key >= 0 && e->key <= 127)
          midi.addEvent(
              juce::MidiMessage::noteOff(std::clamp(int(e->channel) + 1, 1, 16),
                                         int(e->key)),
              int(e->frame));
        if (e->kind == PluginEventKind::midi1 ||
            e->kind == PluginEventKind::sysex)
          midi.addEvent(e->bytes.data(), int(e->bytes.size()), int(e->frame));
        if (e->kind == PluginEventKind::parameterValue)
          for (auto &c : controls)
            if (c.info.id == e->id) {
              c.value->value.store(e->value);
              c.sent = e->value;
              break;
            }
      }
  }
  bool hasEditor() const override {
    return plugin.description() && plugin.description()->editor;
  }
  juce::AudioProcessorEditor *createEditor() override {
#if JUCE_WINDOWS
    if (hasEditor())
      try {
        return new CoreEditor(*this);
      } catch (...) {
        return HostedInstance::createEditor();
      }
#endif
    return HostedInstance::createEditor();
  }
  double getTailLengthSeconds() const override {
    const auto t = plugin.timing();
    return t.infiniteTail ? 3600.
                          : t.tailFrames / std::max(1., getSampleRate());
  }
  void getStateInformation(juce::MemoryBlock &block) override {
    const bool resume = prepared;
    releaseResources();
    std::vector<std::uint8_t> state;
    const auto r = plugin.saveState(state);
    block.reset();
    if (r == Status::ok)
      block.append(state.data(), state.size());
    if (resume)
      prepareToPlay(options.sampleRate, int(options.maximumFrames));
  }
  void setStateInformation(const void *data, int size) override {
    if (!data || size <= 0)
      return;
    const bool resume = prepared;
    std::span<const std::uint8_t> state{static_cast<const std::uint8_t *>(data),
                                        std::size_t(size)};
    std::vector<std::uint8_t> wrapped;
    // CyDaw's current graph editing commands use the JUCE XML transport
    // envelope. Translate this application boundary into the Core format.
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(data, size))
      if (auto *component = xml->getChildByName("IComponent")) {
        juce::MemoryBlock audio, controller;
        if (!audio.fromBase64Encoding(component->getAllSubText()) ||
            audio.getSize() > 64 * 1024 * 1024)
          return;
        if (auto *c = xml->getChildByName("IController"))
          if (!controller.fromBase64Encoding(c->getAllSubText()))
            return;
        if (controller.getSize() > 64 * 1024 * 1024 - audio.getSize())
          return;
        wrapped.resize(24);
        std::memcpy(wrapped.data(), "CyDSP.VST3State1", 16);
        for (unsigned i = 0; i < 4; ++i) {
          wrapped[16 + i] = std::uint8_t(audio.getSize() >> (i * 8));
          wrapped[20 + i] = std::uint8_t(controller.getSize() >> (i * 8));
        }
        const auto append = [&](const juce::MemoryBlock &b) {
          const auto *p = static_cast<const std::uint8_t *>(b.getData());
          if (b.getSize())
            wrapped.insert(wrapped.end(), p, p + b.getSize());
        };
        append(audio);
        append(controller);
        state = wrapped;
      }
    // Internal graph tooling supplies its component JSON directly. Normal
    // saved projects use CyAudioCore's versioned VST3 envelope unchanged.
    if (state[0] == '{' &&
        description.manufacturerName.containsIgnoreCase("cydaw")) {
      wrapped.resize(24);
      std::memcpy(wrapped.data(), "CyDSP.VST3State1", 16);
      for (unsigned i = 0; i < 4; ++i)
        wrapped[16 + i] = std::uint8_t(unsigned(size) >> (i * 8));
      wrapped.insert(wrapped.end(), state.begin(), state.end());
      state = wrapped;
    }
    releaseResources();
    if (plugin.loadState(state) == Status::ok) {
      (void)plugin.refresh();
      syncValues();
    }
    if (resume)
      prepareToPlay(options.sampleRate, int(options.maximumFrames));
  }
  void timerCallback() override {
    std::uint32_t flags = 0;
    if (plugin.pumpMainThread(flags) == Status::ok) {
      requests |= flags;
      setLatencySamples(int(plugin.timing().latencyFrames));
    }
  }
};
CoreEditor::CoreEditor(CoreInstance &p)
    : juce::AudioProcessorEditor(p), owner(p) {
#if JUCE_WINDOWS
  window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
                           GetDesktopWindow(), nullptr,
                           GetModuleHandleW(nullptr), nullptr);
  if (!window)
    throw std::runtime_error("VST3 parent window failed");
  surface.setHWND(window);
  addAndMakeVisible(surface);
  PluginWindow parent;
  parent.handle = reinterpret_cast<std::uintptr_t>(window);
  PluginEditorSize size{640, 420};
  if (p.plugin.openEditor(parent, size) != Status::ok) {
    surface.setHWND(nullptr);
    DestroyWindow(window);
    window = nullptr;
    throw std::runtime_error("CyAudioCore VST3 editor failed");
  }
  p.editor = this;
  setSize(int(size.width), int(size.height));
  (void)p.plugin.showEditor(true);
#endif
}
CoreEditor::~CoreEditor() {
  (void)owner.plugin.closeEditor();
  owner.editor = nullptr;
#if JUCE_WINDOWS
  surface.setHWND(nullptr);
  if (window)
    DestroyWindow(window);
#endif
}
void CoreEditor::resized() {
#if JUCE_WINDOWS
  surface.setBounds(getLocalBounds());
  if (owner.editor == this) {
    PluginEditorSize size{unsigned(getWidth()), unsigned(getHeight())};
    (void)owner.plugin.resizeEditor(size);
  }
#endif
}
class CoreFormat final : public juce::AudioPluginFormat {
public:
  juce::String getName() const override { return "VST3"; }
  void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription> &result,
                           const juce::String &path) override {
    std::unique_ptr<PluginProvider> provider;
    if (createPluginProvider(PluginFormat::vst3, provider) != Status::ok)
      return;
    std::vector<PluginDescriptor> list;
    if (provider->scan(nativePath(path), list) != Status::ok)
      return;
    for (const auto &d : list) {
      auto plugin = std::make_unique<CoreInstance>();
      juce::String error;
      if (plugin->load(juce::File(path), juce::String::fromUTF8(d.id.c_str()),
                       error)) {
        auto description = std::make_unique<juce::PluginDescription>();
        plugin->fillInPluginDescription(*description);
        result.add(std::move(description));
      }
    }
  }
  bool fileMightContainThisPluginType(const juce::String &path) override {
    return juce::File(path).hasFileExtension("vst3");
  }
  juce::String
  getNameOfPluginFromIdentifier(const juce::String &path) override {
    return juce::File(path).getFileNameWithoutExtension();
  }
  bool pluginNeedsRescanning(const juce::PluginDescription &) override {
    return true;
  }
  bool doesPluginStillExist(const juce::PluginDescription &d) override {
    return juce::File(d.fileOrIdentifier).exists();
  }
  bool canScanForPlugins() const override { return true; }
  bool isTrivialToScan() const override { return false; }
  juce::StringArray searchPathsForPlugins(const juce::FileSearchPath &paths,
                                          bool recursive, bool) override {
    juce::StringArray list;
    for (int i = 0; i < paths.getNumPaths(); ++i)
      for (auto f : paths[i].findChildFiles(juce::File::findDirectories |
                                                juce::File::findFiles,
                                            recursive, "*.vst3"))
        list.add(f.getFullPathName());
    return list;
  }
  juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
  bool requiresUnblockedMessageThreadDuringCreation(
      const juce::PluginDescription &) const override {
    return false;
  }
  void createPluginInstance(const juce::PluginDescription &d, double, int,
                            PluginCreationCallback cb) override {
    juce::String error;
    auto plugin = std::make_unique<CoreInstance>();
    if (!plugin->load(juce::File(d.fileOrIdentifier), d.descriptiveName, error))
      plugin.reset();
    cb(std::move(plugin), error);
  }
};
} // namespace
std::unique_ptr<juce::AudioPluginFormat> cyAudioCoreVst3Format() {
  return std::make_unique<CoreFormat>();
}
} // namespace cydaw::plugins
