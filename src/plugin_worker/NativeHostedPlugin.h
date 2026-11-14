#pragma once
#include <atomic>
#include <cyaudio/plugins/Plugin.h>
#include <cydaw/audio/SampleBuffer.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Value.h>
#include <cydaw/midi/EventBuffer.h>
#include <limits>
namespace cydaw::plugins {
struct HostedMetadata {
  base::Text name, manufacturerName, version, category, pluginFormatName,
      descriptiveName, fileOrIdentifier;
  int numInputChannels{}, numOutputChannels{};
  bool isInstrument{};
  base::Text createIdentifierString() const {
    return pluginFormatName + ":" + descriptiveName;
  }
};
class NativeHostedPlugin;
class NativeParameter {
  friend class NativeHostedPlugin;
  NativeHostedPlugin *owner{};

public:
  cyaudio::PluginParameter info;
  std::atomic<double> value{};
  double sent = std::numeric_limits<double>::quiet_NaN();
  float getValue() const {
    return info.maximum > info.minimum ? float((value.load() - info.minimum) /
                                               (info.maximum - info.minimum))
                                       : 0;
  }
  void setValue(float v) {
    if (std::isfinite(v))
      value = info.minimum +
              std::clamp(double(v), 0., 1.) * (info.maximum - info.minimum);
  }
  void setValueNotifyingHost(float v) { setValue(v); }
  base::Text getName(int count) const {
    return base::Text(info.name).substring(0, count);
  }
  base::Text getLabel() const { return base::Text(info.units); }
  bool isAutomatable() const { return info.automatable && !info.readOnly; }
  bool isDiscrete() const { return info.stepped; }
  bool isBoolean() const {
    return info.stepped && info.maximum - info.minimum == 1;
  }
  base::Text getCurrentValueAsText() const;
};
class NativePluginEditor {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit NativePluginEditor(NativeHostedPlugin &);
  ~NativePluginEditor();
  int getWidth() const;
  int getHeight() const;
  bool nativeInterface() const;
  void setVisible(bool);
  bool isVisible() const;
  void toFront(bool);
  void resize(unsigned, unsigned);
  void closeRequested();
  void refreshParameters();
};
class NativeHostedPlugin {
  friend class NativeParameter;
  friend class NativePluginEditor;
  std::unique_ptr<cyaudio::PluginProvider> provider_;
  cyaudio::PluginInstance plugin_;
  std::vector<std::unique_ptr<NativeParameter>> controls_;
  base::Array<NativeParameter *> parameters_;
  std::vector<cyaudio::AudioBuffer<float>> inputStorage_, outputStorage_;
  std::vector<cyaudio::AudioBlock<const float>> inputs_;
  std::vector<cyaudio::AudioBlock<float>> outputs_;
  cyaudio::PluginEventBuffer events_, result_;
  cyaudio::PluginPrepareOptions options_;
  bool prepared_{}, offline_{}, audioPending_{};
  std::uint64_t audioTicket_{};
  int latency_{}, preset_ = -1;
  const cyaudio::PluginTransport *transport_{};
  NativePluginEditor *editor_{};
  void refreshDescription();
  struct Program {
    base::Text name;
    std::vector<std::uint8_t> state;
  };
  std::vector<Program> programs_;
  int currentProgram_{};
  void loadLv2Programs(const base::File &);
  void syncValues();
  void flushValues();

public:
  HostedMetadata description;
  NativeHostedPlugin();
  ~NativeHostedPlugin();
  bool load(const base::File &, const base::Text &id, cyaudio::PluginFormat,
            const base::Text &format, base::Text &error);
  const base::Text &getName() const { return description.name; }
  void fillInPluginDescription(HostedMetadata &d) const { d = description; }
  const base::Array<NativeParameter *> &getParameters() const {
    return parameters_;
  }
  int getTotalNumInputChannels() const { return description.numInputChannels; }
  int getTotalNumOutputChannels() const {
    return description.numOutputChannels;
  }
  int getLatencySamples() const { return latency_; }
  void setOffline(bool value) { offline_ = value; }
  void setTransport(const cyaudio::PluginTransport *t) { transport_ = t; }
  void prepareToPlay(double, int);
  void releaseResources();
  // Worker event loop: submit once, then poll collect while servicing UI.
  // These operations never wait. The device callback lives in the parent.
  void submitBlock(audio::SampleBuffer<float> &, midi::EventBuffer &);
  cyaudio::Status collectBlock(audio::SampleBuffer<float> &,
                               midi::EventBuffer &);
  // Synchronous convenience for offline validation only, not the realtime loop.
  void processBlock(audio::SampleBuffer<float> &, midi::EventBuffer &);
  void getStateInformation(base::MemoryBlock &);
  void setStateInformation(const void *, int);
  bool hasEditor() const;
  std::unique_ptr<NativePluginEditor> createEditor();
  void pump();
  int getNumPrograms() const;
  int getCurrentProgram() const;
  base::Text getProgramName(int) const;
  void setCurrentProgram(int);
};
base::Text nativeScriptDiagnostics(const base::File &);
class NativePluginFormat {
  cyaudio::PluginFormat format_;
  base::Text name_;

public:
  base::Text lastError;
  NativePluginFormat(cyaudio::PluginFormat format, base::Text name)
      : format_(format), name_(std::move(name)) {}
  void findAllTypesForFile(std::vector<std::unique_ptr<HostedMetadata>> &,
                           const base::Text &);
  std::unique_ptr<NativeHostedPlugin>
  createInstanceFromDescription(const HostedMetadata &, double, int,
                                base::Text &);
};
} // namespace cydaw::plugins
