#pragma once
#include <cydaw/audio/Devices.h>
#include <cydaw/base/Xml.h>
#include <cydaw/midi/Devices.h>
namespace cydaw::integration {
class DeviceManager final {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  struct AudioDeviceSetup {
    base::Text inputDeviceName, outputDeviceName;
    double sampleRate{};
    int bufferSize{};
    audio::ChannelSet inputChannels, outputChannels;
    bool useDefaultInputChannels = true, useDefaultOutputChannels = true;
  };
  DeviceManager();
  ~DeviceManager();
  DeviceManager(const DeviceManager &) = delete;
  // Injected factories support a real Core null backend and deterministic
  // tests.
  explicit DeviceManager(std::vector<std::unique_ptr<audio::DeviceType>> types);
  base::Text initialise(int inputs, int outputs, const base::XmlElement *,
                        bool fallback);
  audio::Device *getCurrentAudioDevice() const;
  AudioDeviceSetup getAudioDeviceSetup() const;
  void getAudioDeviceSetup(AudioDeviceSetup &out) const {
    out = getAudioDeviceSetup();
  }
  base::Text setAudioDeviceSetup(const AudioDeviceSetup &, bool);
  const base::Array<audio::DeviceType *> &getAvailableDeviceTypes() const;
  audio::DeviceType *getCurrentDeviceTypeObject() const;
  base::Text getCurrentAudioDeviceType() const;
  void setCurrentAudioDeviceType(const base::Text &, bool);
  base::CriticalSection &getAudioCallbackLock();
  void addAudioCallback(audio::DeviceCallback *);
  void removeAudioCallback(audio::DeviceCallback *);
  void closeAudioDevice();
  void restartLastAudioDevice();
  std::unique_ptr<base::XmlElement> createStateXml() const;
  double getCpuUsage() const;
  void setMidiInputDeviceEnabled(const base::Text &, bool);
  bool isMidiInputDeviceEnabled(const base::Text &) const;
  void addMidiInputDeviceCallback(const base::Text &, midi::InputCallback *);
  void removeMidiInputDeviceCallback(const base::Text &, midi::InputCallback *);
};
} // namespace cydaw::integration
