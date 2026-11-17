#include <chrono>
#include <cstdlib>
#include <string_view>
#include <cydaw/audio/SampleBuffer.h>
#include <cydaw/integration/CyAudioCoreDeviceManager.h>
#include <map>
namespace cydaw::integration {
struct DeviceManager::Impl final : audio::DeviceCallback, midi::InputCallback {
  std::vector<std::unique_ptr<audio::DeviceType>> ownedTypes;
  base::Array<audio::DeviceType *> types;
  audio::DeviceType *type{};
  std::unique_ptr<audio::Device> device;
  AudioDeviceSetup setup;
  int wantedInputs{}, wantedOutputs = 2;
  base::CriticalSection callbackLock;
  std::vector<audio::DeviceCallback *> callbacks;
  audio::SampleBuffer<float> scratch;
  bool prepared{};
  double rate = 48000;
  std::atomic<double> load{};
  mutable base::CriticalSection midiLock;
  std::map<base::Text, std::unique_ptr<midi::Input>> inputs;
  std::vector<std::pair<base::Text, midi::InputCallback *>> midiCallbacks;
  explicit Impl(std::vector<std::unique_ptr<audio::DeviceType>> factories)
      : ownedTypes(std::move(factories)) {
    for (auto &t : ownedTypes)
      types.add(t.get());
    if (!types.isEmpty())
      type = types[0];
  }
  ~Impl() {
    inputs.clear();
    if (device)
      device->close();
  }
  void audioDeviceAboutToStart(audio::Device *d) override {
    base::ScopedLock lock(callbackLock);
    rate = d->getCurrentSampleRate();
    scratch.setSize(
        std::max(1, d->getActiveOutputChannels().countNumberOfSetBits()),
        std::max(1, d->getCurrentBufferSizeSamples()));
    prepared = true;
    for (auto *c : callbacks)
      c->audioDeviceAboutToStart(d);
  }
  void audioDeviceStopped() override {
    base::ScopedLock lock(callbackLock);
    if (prepared)
      for (auto *c : callbacks)
        c->audioDeviceStopped();
    prepared = false;
    load = 0;
  }
  void audioDeviceError(const base::Text &error) override {
    base::ScopedLock lock(callbackLock);
    for (auto *c : callbacks)
      c->audioDeviceError(error);
  }
  void audioDeviceIOCallbackWithContext(
      const float *const *in, int ins, float *const *out, int outs, int frames,
      const audio::DeviceCallbackContext &context) override {
    const auto began = std::chrono::steady_clock::now();
    base::ScopedLock lock(callbackLock);
    for (int ch = 0; ch < outs; ++ch)
      if (out[ch])
        std::fill_n(out[ch], frames, 0.f);
    // Each client owns its output. Input capture clients run before rendering,
    // and their cleared output must not erase another client's contribution.
    if (!prepared || outs > scratch.getNumChannels() ||
        frames > scratch.getNumSamples())
      return;
    for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
      scratch.clear();
      try {
        (*it)->audioDeviceIOCallbackWithContext(
            in, ins, scratch.getArrayOfWritePointers(), outs, frames, context);
      } catch (...) {
        scratch.clear();
      }
      for (int ch = 0; ch < outs; ++ch)
        if (out[ch])
          for (int n = 0; n < frames; ++n)
            out[ch][n] += scratch.getReadPointer(ch)[n];
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began)
            .count();
    if (frames > 0)
      load = load.load() * .95 + elapsed * rate / double(frames) * .05;
  }
  void handleIncomingMidiMessage(midi::Input *source,
                                 const midi::Message &message) override {
    base::ScopedLock lock(midiLock);
    for (const auto &[id, callback] : midiCallbacks)
      if (id.isEmpty() || id == source->getIdentifier())
        callback->handleIncomingMidiMessage(source, message);
  }
};
namespace {
std::vector<std::unique_ptr<audio::DeviceType>> platformTypes() {
  std::vector<std::unique_ptr<audio::DeviceType>> types;
  // Explicit test-only selection: the same Core callback path, clocked by its
  // null backend, with no enumeration or access to physical audio devices.
  const auto* virtualAudio = std::getenv("CYDAW_TEST_VIRTUAL_AUDIO");
  if (virtualAudio && std::string_view(virtualAudio) == "1") {
    types.push_back(createNativeCoreDeviceType("Virtual Audio (test only)",
                                               cyaudio::AudioBackend::nullDevice));
    return types;
  }
#if defined(_WIN32)
  types.push_back(createNativeCoreDeviceType("Windows Audio",
                                             cyaudio::AudioBackend::wasapi));
  types.push_back(createNativeCoreDeviceType("Windows Audio (Exclusive Mode)",
                                             cyaudio::AudioBackend::wasapi,
                                             cyaudio::AudioAccess::exclusive));
  types.push_back(createNativeCoreDeviceType("Windows Audio (Low Latency Mode)",
                                             cyaudio::AudioBackend::wasapi));
  types.push_back(createNativeCoreDeviceType(
      "DirectSound", cyaudio::AudioBackend::directSound));
#elif defined(__APPLE__)
  types.push_back(createNativeCoreDeviceType("CoreAudio",
                                             cyaudio::AudioBackend::coreAudio));
#else
  types.push_back(
      createNativeCoreDeviceType("ALSA", cyaudio::AudioBackend::alsa));
  types.push_back(createNativeCoreDeviceType(
      "PulseAudio", cyaudio::AudioBackend::pulseAudio));
#if CYDAW_WITH_JACK
  types.push_back(
      createNativeCoreDeviceType("JACK", cyaudio::AudioBackend::jack));
#endif
#endif
#if CYDAW_WITH_ASIO
  types.push_back(createNativeAsioDeviceType());
#endif
  return types;
}
} // namespace
DeviceManager::DeviceManager() : DeviceManager(platformTypes()) {}
DeviceManager::DeviceManager(
    std::vector<std::unique_ptr<audio::DeviceType>> types)
    : impl_(std::make_unique<Impl>(std::move(types))) {}
DeviceManager::~DeviceManager() = default;
audio::Device *DeviceManager::getCurrentAudioDevice() const {
  return impl_->device.get();
}
const base::Array<audio::DeviceType *> &
DeviceManager::getAvailableDeviceTypes() const {
  return impl_->types;
}
audio::DeviceType *DeviceManager::getCurrentDeviceTypeObject() const {
  return impl_->type;
}
base::Text DeviceManager::getCurrentAudioDeviceType() const {
  return impl_->type ? impl_->type->getTypeName() : base::Text{};
}
DeviceManager::AudioDeviceSetup DeviceManager::getAudioDeviceSetup() const {
  return impl_->setup;
}
base::CriticalSection &DeviceManager::getAudioCallbackLock() {
  return impl_->callbackLock;
}
double DeviceManager::getCpuUsage() const { return impl_->load.load(); }
void DeviceManager::closeAudioDevice() {
  if (impl_->device)
    impl_->device->close();
  impl_->device.reset();
}
void DeviceManager::restartLastAudioDevice() {
  (void)setAudioDeviceSetup(impl_->setup, false);
}
void DeviceManager::addAudioCallback(audio::DeviceCallback *c) {
  if (!c)
    return;
  base::ScopedLock lock(impl_->callbackLock);
  if (std::find(impl_->callbacks.begin(), impl_->callbacks.end(), c) !=
      impl_->callbacks.end())
    return;
  impl_->callbacks.push_back(c);
  if (impl_->prepared)
    c->audioDeviceAboutToStart(impl_->device.get());
}
void DeviceManager::removeAudioCallback(audio::DeviceCallback *c) {
  base::ScopedLock lock(impl_->callbackLock);
  auto i = std::find(impl_->callbacks.begin(), impl_->callbacks.end(), c);
  if (i == impl_->callbacks.end())
    return;
  impl_->callbacks.erase(i);
  if (impl_->prepared)
    c->audioDeviceStopped();
}
base::Text DeviceManager::setAudioDeviceSetup(const AudioDeviceSetup &requested,
                                              bool) {
  const auto desired = requested;
  closeAudioDevice();
  auto *type = impl_->type;
  if (!type)
    return "No native audio backend available";
  type->scanForDevices();
  auto setup = desired;
  if (!type->hasSeparateInputsAndOutputs() &&
      setup.outputDeviceName.isNotEmpty())
    setup.inputDeviceName = setup.outputDeviceName;
  if (setup.inputDeviceName.isEmpty() && setup.outputDeviceName.isEmpty()) {
    impl_->setup = setup;
    return {};
  }
  auto device = std::unique_ptr<audio::Device>(
      type->createDevice(setup.outputDeviceName, setup.inputDeviceName));
  if (!device)
    return "Audio device unavailable";
  if (setup.useDefaultInputChannels) {
    setup.inputChannels.clear();
    setup.inputChannels.setRange(
        0, std::min(impl_->wantedInputs, device->getInputChannelNames().size()),
        true);
  }
  if (setup.useDefaultOutputChannels) {
    setup.outputChannels.clear();
    setup.outputChannels.setRange(
        0,
        std::min(impl_->wantedOutputs, device->getOutputChannelNames().size()),
        true);
  }
  if (setup.inputDeviceName.isEmpty())
    setup.inputChannels.clear();
  if (setup.outputDeviceName.isEmpty())
    setup.outputChannels.clear();
  if (setup.inputChannels.getHighestBit() >=
          device->getInputChannelNames().size() ||
      setup.outputChannels.getHighestBit() >=
          device->getOutputChannelNames().size())
    return "Selected hardware channel is unavailable";
  if (setup.inputChannels.countNumberOfSetBits() > 64 ||
      setup.outputChannels.countNumberOfSetBits() > 64)
    return "At most 64 active client channels are supported";
  if (setup.sampleRate <= 0) {
    const auto rates = device->getAvailableSampleRates();
    setup.sampleRate = rates.contains(48000.) ? 48000. : rates.getFirst();
    if (setup.sampleRate <= 0)
      setup.sampleRate = 48000;
  }
  if (setup.bufferSize <= 0)
    setup.bufferSize = device->getDefaultBufferSize();
  auto error = device->open(setup.inputChannels, setup.outputChannels,
                            setup.sampleRate, setup.bufferSize);
  if (error.isNotEmpty())
    return error;
  setup.sampleRate = device->getCurrentSampleRate();
  setup.bufferSize = device->getCurrentBufferSizeSamples();
  impl_->setup = setup;
  impl_->device = std::move(device);
  impl_->device->start(impl_.get());
  if (!impl_->device->isPlaying()) {
    error = impl_->device->getLastError();
    closeAudioDevice();
    return error.isEmpty() ? base::Text("Audio stream did not start") : error;
  }
  return {};
}
void DeviceManager::setCurrentAudioDeviceType(const base::Text &name,
                                              bool chosen) {
  for (auto *t : impl_->types)
    if (t->getTypeName() == name) {
      closeAudioDevice();
      impl_->type = t;
      t->scanForDevices();
      AudioDeviceSetup setup;
      auto ins = t->getDeviceNames(true), outs = t->getDeviceNames(false);
      const int i = t->getDefaultDeviceIndex(true),
                o = t->getDefaultDeviceIndex(false);
      if (impl_->wantedInputs > 0 && i >= 0 && i < ins.size())
        setup.inputDeviceName = ins[i];
      if (impl_->wantedOutputs > 0 && o >= 0 && o < outs.size())
        setup.outputDeviceName = outs[o];
      (void)setAudioDeviceSetup(setup, chosen);
      break;
    }
}
base::Text DeviceManager::initialise(int ins, int outs,
                                     const base::XmlElement *xml,
                                     bool fallback) {
  impl_->wantedInputs = std::max(0, ins);
  impl_->wantedOutputs = std::max(0, outs);
  AudioDeviceSetup setup;
  if (xml) {
    for (auto *type : impl_->types)
      if (type->getTypeName() == xml->getStringAttribute("deviceType"))
        impl_->type = type;
    setup.inputDeviceName = xml->getStringAttribute(
        "audioInputDeviceName",
        xml->getStringAttribute("audioDeviceInName",
                                xml->getStringAttribute("audioDeviceName")));
    setup.outputDeviceName = xml->getStringAttribute(
        "audioOutputDeviceName",
        xml->getStringAttribute("audioDeviceOutName",
                                xml->getStringAttribute("audioDeviceName")));
    setup.sampleRate = xml->getDoubleAttribute("audioDeviceRate");
    setup.bufferSize = xml->getIntAttribute("audioDeviceBufferSize");
    setup.useDefaultInputChannels = !xml->hasAttribute("audioDeviceInChans");
    setup.useDefaultOutputChannels = !xml->hasAttribute("audioDeviceOutChans");
    setup.inputChannels.parseString(
        xml->getStringAttribute("audioDeviceInChans"), 2);
    setup.outputChannels.parseString(
        xml->getStringAttribute("audioDeviceOutChans"), 2);
    for (auto *midi : xml->getChildWithTagNameIterator("MIDIINPUT"))
      setMidiInputDeviceEnabled(midi->getStringAttribute("identifier"), true);
  }
  auto defaults = [&] {
    if (auto *type = impl_->type) {
      type->scanForDevices();
      const auto inputs = type->getDeviceNames(true),
                 outputs = type->getDeviceNames(false);
      const int i = type->getDefaultDeviceIndex(true),
                o = type->getDefaultDeviceIndex(false);
      if (impl_->wantedInputs > 0 && i >= 0 && i < inputs.size())
        setup.inputDeviceName = inputs[i];
      if (impl_->wantedOutputs > 0 && o >= 0 && o < outputs.size())
        setup.outputDeviceName = outputs[o];
    }
  };
  if (!xml)
    defaults();
  auto error = setAudioDeviceSetup(setup, false);
  if (error.isNotEmpty() && fallback) {
    setup = {};
    defaults();
    error = setAudioDeviceSetup(setup, false);
  }
  return error;
}
std::unique_ptr<base::XmlElement> DeviceManager::createStateXml() const {
  auto xml = std::make_unique<base::XmlElement>("DEVICESETUP");
  const auto &s = impl_->setup;
  xml->setAttribute("deviceType", getCurrentAudioDeviceType());
  xml->setAttribute("audioInputDeviceName", s.inputDeviceName);
  xml->setAttribute("audioOutputDeviceName", s.outputDeviceName);
  xml->setAttribute("audioDeviceRate", s.sampleRate);
  xml->setAttribute("audioDeviceBufferSize", s.bufferSize);
  xml->setAttribute("audioDeviceInChans", s.inputChannels.toString(2));
  xml->setAttribute("audioDeviceOutChans", s.outputChannels.toString(2));
  for (const auto &[id, input] : impl_->inputs) {
    auto *child = xml->createNewChildElement("MIDIINPUT");
    child->setAttribute("identifier", id);
    child->setAttribute("name", input->getName());
  }
  return xml;
}
void DeviceManager::setMidiInputDeviceEnabled(const base::Text &id,
                                              bool enabled) {
  if (enabled) {
    if (impl_->inputs.contains(id))
      return;
    auto input = midi::Input::openDevice(id, impl_.get());
    if (input) {
      auto *raw = input.get();
      impl_->inputs.emplace(id, std::move(input));
      raw->start();
    }
  } else {
    auto i = impl_->inputs.find(id);
    if (i != impl_->inputs.end()) {
      auto input = std::move(i->second);
      impl_->inputs.erase(i);
      input->stop();
    }
  }
}
bool DeviceManager::isMidiInputDeviceEnabled(const base::Text &id) const {
  return impl_->inputs.contains(id);
}
void DeviceManager::addMidiInputDeviceCallback(const base::Text &id,
                                               midi::InputCallback *callback) {
  if (!callback)
    return;
  base::ScopedLock lock(impl_->midiLock);
  auto entry = std::make_pair(id, callback);
  if (std::find(impl_->midiCallbacks.begin(), impl_->midiCallbacks.end(),
                entry) == impl_->midiCallbacks.end())
    impl_->midiCallbacks.push_back(entry);
}
void DeviceManager::removeMidiInputDeviceCallback(
    const base::Text &id, midi::InputCallback *callback) {
  base::ScopedLock lock(impl_->midiLock);
  std::erase(impl_->midiCallbacks, std::make_pair(id, callback));
}
} // namespace cydaw::integration
