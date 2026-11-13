#include "NativeHostedPlugin.h"
#include <cydaw/audio/Denormals.h>
#include <cydaw/base/EventLoop.h>
#include <cydaw/base/File.h>
#include <cydaw/base/LegacyPluginState.h>
#include <cydaw/base/Process.h>
#include <cydaw/base/Utilities.h>
#include <cydaw/plugins/PluginIsolation.h>
#include <cydaw/plugins/WorkerWake.h>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on
#endif

#if defined(_WIN32)
extern "C" __declspec(dllimport) void *__stdcall OpenProcess(unsigned long, int,
                                                             unsigned long);
extern "C" __declspec(dllimport) unsigned long __stdcall WaitForSingleObject(
    void *, unsigned long);
extern "C" __declspec(dllimport) int __stdcall CloseHandle(void *);
extern "C"
    __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
extern "C"
    __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int);
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif
#else
#include <cerrno>
#include <signal.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

namespace {

// The CyDAW that started this worker. Gone (a crash, a kill), and the worker
// ends too rather than keep its plug-in loaded for ever.
class ParentWatch final {
public:
  explicit ParentWatch(const std::int64_t pid) : pid_(pid) {
#if defined(_WIN32)
    if (pid_ > 0)
      handle_ = OpenProcess(0x00100000ul /* SYNCHRONIZE */, 0,
                            static_cast<unsigned long>(pid_));
#endif
  }
  ~ParentWatch() {
#if defined(_WIN32)
    if (handle_ != nullptr)
      (void)CloseHandle(handle_);
#endif
  }
  ParentWatch(const ParentWatch &) = delete;
  ParentWatch &operator=(const ParentWatch &) = delete;
  // Looked at twice a second at most.
  [[nodiscard]] bool gone() noexcept {
    const auto now = cydaw::base::Time::getMillisecondCounter();
    if (pid_ <= 0 || now - checked_ < 500)
      return false;
    checked_ = now;
#if defined(_WIN32)
    return handle_ != nullptr &&
           WaitForSingleObject(handle_, 0) == 0 /* WAIT_OBJECT_0 */;
#else
    return kill(static_cast<pid_t>(pid_), 0) != 0 && errno == ESRCH;
#endif
  }

private:
  std::int64_t pid_{};
  std::uint32_t checked_{};
#if defined(_WIN32)
  void *handle_{};
#endif
};
// What runRealtimeWorker returns when CyDAW went away.
constexpr int parentGone = 18;

cydaw::base::Value
pluginDescriptionToJson(const cydaw::plugins::HostedMetadata &description) {
  auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
  value->setProperty("name", description.name);
  value->setProperty("manufacturer", description.manufacturerName);
  value->setProperty("version", description.version);
  value->setProperty("category", description.category);
  value->setProperty("format", description.pluginFormatName);
  value->setProperty("identifier", description.createIdentifierString());
  value->setProperty("file", description.fileOrIdentifier);
  value->setProperty("pluginId", description.descriptiveName);
  value->setProperty("inputs", description.numInputChannels);
  value->setProperty("outputs", description.numOutputChannels);
  value->setProperty("instrument", description.isInstrument);
  return cydaw::base::Value(value.get());
}

int runSelfTest() {
  const cydaw::plugins::AudioBlockHeader header;
  if (header.magic != cydaw::plugins::workerProtocolMagic ||
      header.protocolVersion != cydaw::plugins::workerProtocolVersion)
    return 1;

  std::cout << "{\"worker\":\"CyDAWPluginWorker\",\"protocol\":"
            << cydaw::plugins::workerProtocolVersion
            << ",\"isolation\":\"per-instance\",\"formats\":[\"VST3\""
            << ",\"CLAP\",\"LV2\",\"JSFX\"],\"status\":\"ready\"}\n";
  return 0;
}

struct PluginFormatSelection final {
  std::unique_ptr<cydaw::plugins::NativePluginFormat> format;
  cydaw::base::Text name;
  cydaw::base::Text error;
};

PluginFormatSelection formatForPlugin(const cydaw::base::File &pluginFile) {
  if (pluginFile.hasFileExtension("cyplugin")) {
    const auto data = cydaw::base::JSON::parse(pluginFile);
    const auto path = data.getProperty("file", {}).toString();
    if (!cydaw::base::File::isAbsolutePath(path))
      return {{}, {}, "Invalid plugin descriptor"};
    const cydaw::base::File target(path);
    if (target.hasFileExtension("cyplugin"))
      return {{}, {}, "Nested descriptors are unsupported"};
    return formatForPlugin(target);
  }
  const auto make = [](cyaudio::PluginFormat format, const char *name) {
    return PluginFormatSelection{
        std::make_unique<cydaw::plugins::NativePluginFormat>(format, name),
        name,
        {}};
  };
  if (pluginFile.hasFileExtension("vst3"))
    return make(cyaudio::PluginFormat::vst3, "VST3");
  if (pluginFile.hasFileExtension("clap"))
    return make(cyaudio::PluginFormat::clap, "CLAP");
  if (pluginFile.hasFileExtension("lv2"))
    return make(cyaudio::PluginFormat::lv2, "LV2");
  if (pluginFile.hasFileExtension("jsfx") ||
      pluginFile.getFileExtension().isEmpty())
    return make(cyaudio::PluginFormat::jsfx, "JSFX");
  if (pluginFile.hasFileExtension("dll;so;vst"))
    return {{},
            "VST2",
            "This worker does not provide VST2 hosting. Supported formats are "
            "VST3, CLAP, LV2 and JSFX."};
  return {{}, {}, "Unsupported plugin file. Expected VST3, CLAP, LV2 or JSFX."};
}
void findPluginTypes(
    PluginFormatSelection &selection, const cydaw::base::File &requested,
    std::vector<std::unique_ptr<cydaw::plugins::HostedMetadata>> &result) {
  auto file = requested;
  cydaw::base::Text id;
  if (file.hasFileExtension("cyplugin")) {
    const auto data = cydaw::base::JSON::parse(file);
    id = data.getProperty("id", {}).toString();
    file = cydaw::base::File(data.getProperty("file", {}).toString());
  }
  selection.format->findAllTypesForFile(result, file.getFullPathName());
  selection.error = selection.format->lastError;
  if (id.isNotEmpty())
    std::erase_if(result, [&](const auto &d) {
      return d->descriptiveName != id && d->fileOrIdentifier != id;
    });
}

cydaw::base::Value
graphStateFromPlugin(cydaw::plugins::NativeHostedPlugin &processor) {
  cydaw::base::MemoryBlock state;
  processor.getStateInformation(state);
  if (state.isEmpty())
    return {};
  if (state.getSize() >= 24 &&
      std::memcmp(state.getData(), "CyDSP.VST3State1", 16) == 0) {
    const auto *p = static_cast<const unsigned char *>(state.getData());
    const auto size = std::uint32_t(p[16]) | (std::uint32_t(p[17]) << 8) |
                      (std::uint32_t(p[18]) << 16) |
                      (std::uint32_t(p[19]) << 24);
    if (size <= state.getSize() - 24)
      return cydaw::base::JSON::parse(cydaw::base::Text::fromUTF8(
          reinterpret_cast<const char *>(p + 24), int(size)));
    return {};
  }
  if (auto xml = cydaw::base::legacyXmlFromBinary(
          state.getData(), static_cast<int>(state.getSize()))) {
    const auto *component = xml->getChildByName("IComponent");
    if (component == nullptr)
      return {};
    cydaw::base::MemoryBlock decoded;
    if (!cydaw::base::decodeLegacyStateBytes(component->getAllSubText(),
                                             decoded))
      return {};
    return cydaw::base::JSON::parse(cydaw::base::Text::fromUTF8(
        static_cast<const char *>(decoded.getData()),
        static_cast<int>(decoded.getSize())));
  }
  return cydaw::base::JSON::parse(
      cydaw::base::Text::fromUTF8(static_cast<const char *>(state.getData()),
                                  static_cast<int>(state.getSize())));
}

int probePlugin(const cydaw::base::File &pluginFile,
                const cydaw::base::File &outputFile,
                const bool validateProcessing) {
  cydaw::base::Array<cydaw::base::Value> results;
  auto selection = formatForPlugin(pluginFile);
  if (selection.format == nullptr) {
    auto failure = cydaw::base::Object::Ptr(new cydaw::base::Object());
    failure->setProperty("format", selection.name);
    failure->setProperty("file", pluginFile.getFullPathName());
    failure->setProperty("error", selection.error);
    failure->setProperty("processingOk", false);
    results.add(cydaw::base::Value(failure.get()));
  }
  std::vector<std::unique_ptr<cydaw::plugins::HostedMetadata>> descriptions;
  if (selection.format != nullptr) {
    findPluginTypes(selection, pluginFile, descriptions);
  }
  if (selection.format != nullptr && descriptions.empty()) {
    auto failure = cydaw::base::Object::Ptr(new cydaw::base::Object());
    failure->setProperty("format", selection.name);
    failure->setProperty("file", pluginFile.getFullPathName());
    failure->setProperty("error",
                         selection.error.isNotEmpty()
                             ? selection.error
                             : "No " + selection.name +
                                   " type was found in the selected file");
    failure->setProperty("processingOk", false);
    results.add(cydaw::base::Value(failure.get()));
  }
  for (const auto &description : descriptions) {
    auto result = pluginDescriptionToJson(*description);
    if (validateProcessing) {
      auto *object = result.getDynamicObject();
      cydaw::base::Text error;
      try {
        auto instance = selection.format->createInstanceFromDescription(
            *description, 48'000.0, 512, error);
        if (instance != nullptr) {
          instance->setOffline(true);
          instance->prepareToPlay(48'000.0, 512);

          const auto editableGraphRuntime =
              description->name.containsIgnoreCase("CyDAW Graph Runtime");
          auto initialGraphState = graphStateFromPlugin(*instance);
          const auto *initialGraphRoot = initialGraphState.getDynamicObject();
          const auto graphPlugin =
              initialGraphRoot != nullptr &&
              static_cast<int>(initialGraphRoot->getProperty("schema")) == 2 &&
              initialGraphRoot->getProperty("native").getDynamicObject() !=
                  nullptr;
          if (editableGraphRuntime) {
            auto native = cydaw::base::Object::Ptr(new cydaw::base::Object());
            native->setProperty("schema", 1);
            native->setProperty("outputNodeId", 3);
            cydaw::base::Array<cydaw::base::Value> nodes;
            const auto addNode = [&nodes](const int id, const int kind,
                                          const double gainDb) {
              auto node = cydaw::base::Object::Ptr(new cydaw::base::Object());
              node->setProperty("id", id);
              node->setProperty("kind", kind);
              auto parameters =
                  cydaw::base::Object::Ptr(new cydaw::base::Object());
              parameters->setProperty("gainDb", gainDb);
              node->setProperty("parameters",
                                cydaw::base::Value(parameters.get()));
              nodes.add(cydaw::base::Value(node.get()));
            };
            addNode(1, 1, 0.0); // input
            addNode(2, 4, 0.0); // gain
            addNode(3, 2, 0.0); // output
            native->setProperty("nodes", nodes);
            cydaw::base::Array<cydaw::base::Value> connections;
            for (const auto [source, destination] :
                 {std::pair{1, 2}, std::pair{2, 3}}) {
              auto connection =
                  cydaw::base::Object::Ptr(new cydaw::base::Object());
              connection->setProperty("source", source);
              connection->setProperty("destination", destination);
              connections.add(cydaw::base::Value(connection.get()));
            }
            native->setProperty("connections", connections);

            auto published =
                cydaw::base::Object::Ptr(new cydaw::base::Object());
            published->setProperty("id", 0x1020304);
            published->setProperty("key", "node.2.gain.db");
            published->setProperty("name", "Host Gain");
            published->setProperty("unit", "dB");
            published->setProperty("minimum", -24.0);
            published->setProperty("maximum", 24.0);
            published->setProperty("default", 0.0);
            published->setProperty("targetNodeId", 2);
            published->setProperty("targetParameter", "node.2.gain.db");
            published->setProperty("automatable", true);
            published->setProperty("modulatable", true);
            published->setProperty("visibleInDevice", true);
            published->setProperty("exportedToHost", true);
            cydaw::base::Array<cydaw::base::Value> publishedParameters;
            publishedParameters.add(cydaw::base::Value(published.get()));
            cydaw::base::Array<cydaw::base::Value> hostValues;
            hostValues.add(0.25); // -12 dB

            auto state = cydaw::base::Object::Ptr(new cydaw::base::Object());
            state->setProperty("schema", 2);
            state->setProperty("native", cydaw::base::Value(native.get()));
            state->setProperty("publishedParameters", publishedParameters);
            state->setProperty("hostValues", hostValues);
            const auto stateJson = cydaw::base::JSON::toString(
                cydaw::base::Value(state.get()), false);
            cydaw::base::XmlElement vstState("VST3PluginState");
            cydaw::base::MemoryBlock componentState(
                stateJson.toRawUTF8(), stateJson.getNumBytesAsUTF8());
            vstState.createNewChildElement("IComponent")
                ->addTextElement(componentState.toBase64Encoding());
            cydaw::base::MemoryBlock wrappedState;
            cydaw::base::copyLegacyXmlToBinary(vstState, wrappedState);
            instance->setStateInformation(
                wrappedState.getData(),
                static_cast<int>(wrappedState.getSize()));
          }
          object->setProperty("inputs", instance->getTotalNumInputChannels());
          object->setProperty("outputs", instance->getTotalNumOutputChannels());
          const auto channels =
              std::max({1, instance->getTotalNumInputChannels(),
                        instance->getTotalNumOutputChannels()});
          cydaw::audio::SampleBuffer<float> buffer(channels, 512);
          buffer.clear();
          for (int channel = 0; channel < instance->getTotalNumInputChannels();
               ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
              buffer.setSample(
                  channel, sample,
                  static_cast<float>(
                      0.05 *
                      std::sin(cydaw::base::MathConstants<double>::twoPi *
                               220.0 * sample / 48'000.0)));

          cydaw::midi::EventBuffer midi;
          if (description->isInstrument) {
            midi.addEvent(cydaw::midi::Message::noteOn(1, 60, 0.8f), 0);
            midi.addEvent(cydaw::midi::Message::noteOff(1, 60), 480);
          }
          {
            cydaw::audio::ScopedNoDenormals noDenormals;
            instance->processBlock(buffer, midi);
          }

          auto peak = 0.0f;
          auto finite = true;
          for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
              const auto value = buffer.getSample(channel, sample);
              finite = finite && std::isfinite(value);
              peak = std::max(peak, std::abs(value));
            }
          if (graphPlugin) {
            const auto *nativeRoot =
                initialGraphRoot->getProperty("native").getDynamicObject();
            const auto *nativeNodes =
                nativeRoot != nullptr
                    ? nativeRoot->getProperty("nodes").getArray()
                    : nullptr;
            object->setProperty("graphNativeNodeCount",
                                nativeNodes != nullptr ? nativeNodes->size()
                                                       : 0);
            const auto &parameters = instance->getParameters();
            cydaw::plugins::NativeParameter *graphGain = nullptr;
            cydaw::plugins::NativeParameter *graphPostEffect = nullptr;
            cydaw::base::Text expectedName;
            cydaw::base::Text expectedUnit;
            cydaw::base::Text expectedPostEffectName;
            auto expectedAutomatable = true;
            auto publishedCount = 0;
            if (editableGraphRuntime) {
              expectedName = "Host Gain";
              expectedUnit = "dB";
              publishedCount = 1;
            } else if (const auto *published =
                           initialGraphRoot->getProperty("publishedParameters")
                               .getArray()) {
              publishedCount = published->size();
              for (const auto &encoded : *published) {
                const auto *value = encoded.getDynamicObject();
                if (value == nullptr)
                  continue;
                const auto target =
                    value->getProperty("targetParameter").toString();
                if (description->isInstrument &&
                    target.containsIgnoreCase("drive.db"))
                  expectedPostEffectName =
                      value->getProperty("name").toString();
                const auto testableAudioParameter =
                    target.containsIgnoreCase("gain.db") ||
                    (description->isInstrument &&
                     target.containsIgnoreCase("polyOscillator.level"));
                if (!testableAudioParameter)
                  continue;
                expectedName = value->getProperty("name").toString();
                expectedUnit = value->getProperty("unit").toString();
                expectedAutomatable =
                    !value->hasProperty("automatable") ||
                    static_cast<bool>(value->getProperty("automatable"));
              }
            }
            cydaw::base::Array<cydaw::base::Value> parameterNames;
            for (auto *parameter : parameters) {
              if (parameter == nullptr)
                continue;
              const auto name = parameter->getName(128);
              parameterNames.add(name);
              if (expectedName.isNotEmpty() && name == expectedName)
                graphGain = parameter;
              if (expectedPostEffectName.isNotEmpty() &&
                  name == expectedPostEffectName)
                graphPostEffect = parameter;
            }
            const auto parameterContractOk =
                publishedCount == 0
                    ? true
                    : parameters.size() >= publishedCount &&
                          (expectedName.isEmpty() ||
                           (graphGain != nullptr &&
                            graphGain->getLabel() == expectedUnit &&
                            graphGain->isAutomatable() == expectedAutomatable));
            if (graphGain != nullptr)
              graphGain->setValueNotifyingHost(
                  description->isInstrument ? 0.20f : 0.25f);
            auto initialPeak = peak;
            for (auto block = 0; block < 12; ++block) {
              buffer.clear();
              for (int channel = 0;
                   channel < instance->getTotalNumInputChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                  buffer.setSample(
                      channel, sample,
                      static_cast<float>(
                          0.05 *
                          std::sin(cydaw::base::MathConstants<double>::twoPi *
                                   220.0 * sample / 48'000.0)));
              cydaw::midi::EventBuffer warmupMidi;
              if (description->isInstrument && block == 0) {
                warmupMidi.addEvent(cydaw::midi::Message::allNotesOff(1), 0);
                warmupMidi.addEvent(cydaw::midi::Message::noteOn(1, 64, 0.8f),
                                    1);
              }
              instance->processBlock(buffer, warmupMidi);
              if (block == 11) {
                initialPeak = 0.0f;
                for (int channel = 0; channel < buffer.getNumChannels();
                     ++channel)
                  initialPeak = std::max(
                      initialPeak,
                      buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
              }
            }
            if (graphGain != nullptr)
              graphGain->setValueNotifyingHost(
                  description->isInstrument ? 0.80f : 0.75f);
            auto automatedPeak = 0.0f;
            for (auto block = 0; block < 16; ++block) {
              buffer.clear();
              for (int channel = 0;
                   channel < instance->getTotalNumInputChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                  buffer.setSample(
                      channel, sample,
                      static_cast<float>(
                          0.05 *
                          std::sin(cydaw::base::MathConstants<double>::twoPi *
                                   220.0 * sample / 48'000.0)));
              cydaw::midi::EventBuffer automationMidi;
              instance->processBlock(buffer, automationMidi);
              if (block == 15)
                for (int channel = 0; channel < buffer.getNumChannels();
                     ++channel)
                  automatedPeak = std::max(
                      automatedPeak,
                      buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
            }
            auto postEffectDryPeak = 0.0f;
            auto postEffectWetPeak = 0.0f;
            if (graphPostEffect != nullptr) {
              graphPostEffect->setValueNotifyingHost(0.0f);
              for (auto block = 0; block < 4; ++block) {
                buffer.clear();
                cydaw::midi::EventBuffer effectMidi;
                instance->processBlock(buffer, effectMidi);
                if (block == 3)
                  for (auto channel = 0; channel < buffer.getNumChannels();
                       ++channel)
                    postEffectDryPeak =
                        std::max(postEffectDryPeak,
                                 buffer.getMagnitude(channel, 0,
                                                     buffer.getNumSamples()));
              }
              graphPostEffect->setValueNotifyingHost(1.0f);
              for (auto block = 0; block < 4; ++block) {
                buffer.clear();
                cydaw::midi::EventBuffer effectMidi;
                instance->processBlock(buffer, effectMidi);
                if (block == 3)
                  for (auto channel = 0; channel < buffer.getNumChannels();
                       ++channel)
                    postEffectWetPeak =
                        std::max(postEffectWetPeak,
                                 buffer.getMagnitude(channel, 0,
                                                     buffer.getNumSamples()));
              }
              object->setProperty("graphPostEffectProcessingOk",
                                  postEffectDryPeak > 0.0f &&
                                      postEffectWetPeak >
                                          postEffectDryPeak * 1.5f);
              object->setProperty("graphPostEffectDryPeak", postEffectDryPeak);
              object->setProperty("graphPostEffectWetPeak", postEffectWetPeak);
            }
            object->setProperty("parameterCount",
                                static_cast<int>(parameters.size()));
            object->setProperty("parameterNames", parameterNames);
            object->setProperty("graphParameterLabel",
                                graphGain != nullptr
                                    ? cydaw::base::Text(graphGain->getLabel())
                                    : cydaw::base::Text());
            object->setProperty("graphParameterAutomatable",
                                graphGain != nullptr &&
                                    graphGain->isAutomatable());
            object->setProperty("graphParameterContractOk",
                                parameterContractOk);
            object->setProperty("graphStateDetected", true);
            object->setProperty("graphPublishedParameterCount", publishedCount);
            object->setProperty(
                "graphAutomationProcessingOk",
                graphGain == nullptr ||
                    (parameterContractOk && initialPeak > 0.0f &&
                     automatedPeak >
                         initialPeak *
                             (description->isInstrument ? 2.5f : 3.0f)));
            object->setProperty("graphInitialPeak", initialPeak);
            object->setProperty("graphAutomatedPeak", automatedPeak);
            if (description->isInstrument)
              object->setProperty("graphInstrumentAudioOk",
                                  finite && peak > 0.00001f);
          }
          cydaw::base::MemoryBlock state;
          instance->getStateInformation(state);
          instance->setStateInformation(state.getData(),
                                        static_cast<int>(state.getSize()));
          auto editor = std::unique_ptr<cydaw::plugins::NativePluginEditor>(
              instance->hasEditor() ? instance->createEditor().release()
                                    : nullptr);
          const auto editorOk = !instance->hasEditor() ||
                                (editor != nullptr && editor->getWidth() > 0 &&
                                 editor->getHeight() > 0);
          object->setProperty("processingOk", finite);
          object->setProperty("stateRoundTripOk", state.getSize() > 0);
          object->setProperty("editorOk", editorOk);
          if (description->pluginFormatName == "JSFX") {
            const bool graphics =
                instance->description.pluginFormatName == "JSFX" && editor &&
                editor->nativeInterface();
            object->setProperty("gfxPresent", graphics);
            if (graphics) {
              for (int frame = 0; frame < 3; ++frame) {
                instance->pump();
                cydaw::plugins::WorkerWake dispatch;
                dispatch.waitAndDispatch();
              }
              object->setProperty("gfxOk", true);
              object->setProperty("gfxWidth", editor->getWidth());
              object->setProperty("gfxHeight", editor->getHeight());
            }
          }
          object->setProperty("latencySamples", instance->getLatencySamples());
          object->setProperty("stateBytes",
                              static_cast<std::int64_t>(state.getSize()));
          object->setProperty("testPeak", peak);
          instance->releaseResources();
        } else {
          object->setProperty("processingOk", false);
          object->setProperty("error", error);
        }
      } catch (const std::exception &exception) {
        object->setProperty("processingOk", false);
        object->setProperty("error", exception.what());
      } catch (...) {
        object->setProperty("processingOk", false);
        object->setProperty("error", "Unknown plugin exception");
      }
    }
    results.add(std::move(result));
  }

  const auto json = cydaw::base::JSON::toString(results, true);
  if (outputFile != cydaw::base::File{}) {
    if (!outputFile.replaceWithText(json))
      return 3;
  } else {
    std::cout << json << '\n';
  }
  if (validateProcessing) {
    for (const auto &result : results) {
      const auto *object = result.getDynamicObject();
      if (object == nullptr ||
          !static_cast<bool>(object->getProperty("processingOk")))
        return 4;
      if (static_cast<bool>(object->getProperty("graphStateDetected")) &&
          (!static_cast<bool>(
               object->getProperty("graphParameterContractOk")) ||
           !static_cast<bool>(
               object->getProperty("graphAutomationProcessingOk")) ||
           (object->hasProperty("graphInstrumentAudioOk") &&
            !static_cast<bool>(
                object->getProperty("graphInstrumentAudioOk"))) ||
           (object->hasProperty("graphPostEffectProcessingOk") &&
            !static_cast<bool>(
                object->getProperty("graphPostEffectProcessingOk")))))
        return 5;
    }
  }
  return 0;
}

cydaw::base::Text argumentValue(const cydaw::base::TextList &arguments,
                                const cydaw::base::Text &key,
                                const cydaw::base::Text &fallback = {}) {
  const auto index = arguments.indexOf(key);
  return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1]
                                                    : fallback;
}

void setWorkerStatus(cydaw::plugins::SharedPluginAudio &shared,
                     const cydaw::plugins::SharedPluginState state,
                     const cydaw::base::Text &message) {
  const auto utf8 = message.toRawUTF8();
  const auto length = std::min<std::size_t>(std::strlen(utf8),
                                            sizeof(shared.statusMessage) - 1);
  std::memcpy(shared.statusMessage, utf8, length);
  shared.statusMessage[length] = '\0';
  cydaw::plugins::sharedAtomic(shared.state)
      .store(static_cast<std::uint32_t>(state), std::memory_order_release);
}

template <std::size_t Size>
void copyFixedText(char (&destination)[Size], const cydaw::base::Text &text) {
  const auto utf8 = text.toRawUTF8();
  const auto length = std::min<std::size_t>(std::strlen(utf8), Size - 1);
  std::memcpy(destination, utf8, length);
  destination[length] = '\0';
}

struct WorkerPlayHead {
  cyaudio::PluginTransport time;
  void update(const cydaw::plugins::SharedPluginAudio &shared, double rate) {
    time.seconds = shared.transportSeconds;
    time.songFrame = std::int64_t(time.seconds * rate);
    time.beats = shared.transportQuarterNotes;
    time.tempo = shared.transportTempo;
    time.playing = shared.transportPlaying != 0;
    time.numerator = std::uint16_t(shared.transportNumerator);
    time.denominator = std::uint16_t(shared.transportDenominator);
    time.hasSeconds = time.hasSongFrame = time.hasBeats = time.hasTempo =
        time.hasSignature = true;
  }
};

int runRealtimeWorker(const cydaw::base::File &pluginFile,
                      const cydaw::base::File &sharedFile,
                      const double sampleRate, const int requestedBlockSize,
                      const int requestedChannels, const bool passthrough,
                      const std::int64_t parentPid) {
  // Prepared for the largest block the host can send, whatever its device
  // uses now: a buffer made larger later is still played.
  const auto maximumBlockSize =
      static_cast<int>(cydaw::plugins::maximumSharedAudioFrames);
  cydaw::base::ignoreUnused(requestedBlockSize);
  ParentWatch parent(parentPid);
  // Wake the audio worker independently of coarse GUI dispatch timeouts.
#if defined(_WIN32)
  struct FineTimer {
    FineTimer() { (void)timeBeginPeriod(1); }
    ~FineTimer() { (void)timeEndPeriod(1); }
  } fineTimer;
#endif
#if defined(_WIN32)
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
  if (!sharedFile.existsAsFile())
    return 10;
  cydaw::base::MemoryMappedFile mapping(
      sharedFile, cydaw::base::MemoryMappedFile::readWrite, false);
  auto *shared =
      static_cast<cydaw::plugins::SharedPluginAudio *>(mapping.getData());
  if (shared == nullptr ||
      mapping.getSize() < sizeof(cydaw::plugins::SharedPluginAudio) ||
      shared->magic != cydaw::plugins::workerProtocolMagic ||
      shared->protocolVersion != cydaw::plugins::workerProtocolVersion)
    return 11;

  cydaw::plugins::WorkerWake wake;
  wake.open(("Local\\CyDAW.PluginAudio." + sharedFile.getFileName())
                .toWideCharPointer());
  try {
    WorkerPlayHead playHead;
    std::unique_ptr<cydaw::plugins::NativeHostedPlugin> instance;
    cydaw::base::Text pluginFormatName;
    auto processingChannels = cydaw::base::limit(
        1, static_cast<int>(cydaw::plugins::maximumSharedAudioChannels),
        requestedChannels);
    if (!passthrough) {
      auto selection = formatForPlugin(pluginFile);
      pluginFormatName = selection.name;
      if (selection.format == nullptr) {
        setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                        selection.error);
        return 12;
      }
      std::vector<std::unique_ptr<cydaw::plugins::HostedMetadata>> descriptions;
      findPluginTypes(selection, pluginFile, descriptions);
      if (descriptions.empty()) {
        setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                        "No " + pluginFormatName +
                            " type was found in the selected file");
        return 13;
      }
      cydaw::base::Text error;
      instance = selection.format->createInstanceFromDescription(
          *descriptions[0], sampleRate, maximumBlockSize, error);
      if (instance == nullptr) {
        setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                        error.isNotEmpty() ? cydaw::base::Text(error)
                                           : "Cannot instantiate " +
                                                 pluginFormatName + " plugin");
        return 14;
      }
      instance->setTransport(&playHead.time);
      instance->setOffline(false);
      instance->prepareToPlay(sampleRate, maximumBlockSize);
      processingChannels = cydaw::base::limit(
          1, static_cast<int>(cydaw::plugins::maximumSharedAudioChannels),
          std::max({processingChannels, instance->getTotalNumInputChannels(),
                    instance->getTotalNumOutputChannels()}));
      cydaw::plugins::sharedAtomic(shared->latencySamples)
          .store(static_cast<std::uint32_t>(
                     std::max(0, instance->getLatencySamples())),
                 std::memory_order_release);
    }

    cydaw::audio::SampleBuffer<float> buffer(processingChannels,
                                             maximumBlockSize);
    cydaw::midi::EventBuffer midi;
    midi.ensureSize(256 * 1024);
    std::unique_ptr<cydaw::plugins::NativePluginEditor> editorWindow;
    setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::ready,
                    passthrough ? "IPC passthrough ready"
                                : pluginFormatName + " ready");
    auto request = cydaw::plugins::sharedAtomic(shared->requestSequence);
    auto response = cydaw::plugins::sharedAtomic(shared->responseSequence);
    auto heartbeat = cydaw::plugins::sharedAtomic(shared->heartbeat);
    auto commandRequest = cydaw::plugins::sharedAtomic(shared->commandSequence);
    auto commandResponse =
        cydaw::plugins::sharedAtomic(shared->commandResponseSequence);
    auto automationRequest =
        cydaw::plugins::sharedAtomic(shared->automationSequence);
    std::uint64_t appliedAutomationSequence{};
    auto shutdown = cydaw::plugins::sharedAtomic(shared->shutdownRequested);

    while (shutdown.load(std::memory_order_acquire) == 0) {
      if (instance)
        instance->pump();
      cydaw::base::EventLoop::dispatchPending();
      if (parent.gone()) {
        editorWindow.reset();
        if (instance != nullptr)
          instance->releaseResources();
        return parentGone;
      }
      auto didWork = false;
      const auto requested = request.load(std::memory_order_acquire);
      if (requested != response.load(std::memory_order_relaxed)) {
        const auto frames = static_cast<int>(shared->frameCount);
        const auto inputs = cydaw::base::limit(
            0, processingChannels, static_cast<int>(shared->inputChannels));
        const auto outputs = cydaw::base::limit(
            0, processingChannels, static_cast<int>(shared->outputChannels));
        if (frames <= 0 || frames > maximumBlockSize ||
            frames >
                static_cast<int>(cydaw::plugins::maximumSharedAudioFrames)) {
          setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                          "Invalid audio block received from host");
          return 15;
        }

        buffer.clear();
        for (int channel = 0; channel < inputs; ++channel)
          for (int frame = 0; frame < frames; ++frame)
            buffer.setSample(channel, frame,
                             shared->input[cydaw::plugins::sharedAudioIndex(
                                 static_cast<std::uint32_t>(channel),
                                 static_cast<std::uint32_t>(frame))]);
        midi.clear();
        if (instance != nullptr) {
          const auto automationSequence =
              automationRequest.load(std::memory_order_acquire);
          if (automationSequence != appliedAutomationSequence) {
            const auto &parameters = instance->getParameters();
            const auto count =
                std::min(shared->automationCount,
                         cydaw::plugins::maximumSharedAutomationParameters);
            for (std::uint32_t automationIndex = 0; automationIndex < count;
                 ++automationIndex) {
              const auto &automation = shared->automation[automationIndex];
              if (automation.index <
                  static_cast<std::uint32_t>(parameters.size()))
                parameters[static_cast<int>(automation.index)]->setValue(
                    cydaw::base::limit(0.0f, 1.0f, automation.value));
            }
            appliedAutomationSequence = automationSequence;
          }
          cydaw::audio::ScopedNoDenormals noDenormals;
          playHead.update(*shared, sampleRate);
          cydaw::audio::SampleBuffer<float> block(
              buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
              frames);
          instance->processBlock(block, midi);
          cydaw::plugins::sharedAtomic(shared->latencySamples)
              .store(static_cast<std::uint32_t>(
                         std::max(0, instance->getLatencySamples())),
                     std::memory_order_release);
        }
        for (int channel = 0; channel < outputs; ++channel)
          for (int frame = 0; frame < frames; ++frame)
            shared->output[cydaw::plugins::sharedAudioIndex(
                static_cast<std::uint32_t>(channel),
                static_cast<std::uint32_t>(frame))] =
                buffer.getSample(channel, frame);
        shared->outputFrameCount = static_cast<std::uint32_t>(frames);
        heartbeat.fetch_add(1, std::memory_order_relaxed);
        response.store(requested, std::memory_order_release);
        didWork = true;
      }

      const auto commandSequence =
          commandRequest.load(std::memory_order_acquire);
      if (commandSequence != commandResponse.load(std::memory_order_relaxed)) {
        shared->commandResult = 0;
        shared->commandStatus[0] = '\0';
        try {
          const auto command =
              static_cast<cydaw::plugins::SharedPluginCommand>(shared->command);
          if (command ==
              cydaw::plugins::SharedPluginCommand::refreshParameters) {
            shared->parameterCount = 0;
            if (instance != nullptr) {
              const auto &parameters = instance->getParameters();
              const auto count =
                  std::min<int>(parameters.size(),
                                cydaw::plugins::maximumSharedPluginParameters);
              shared->parameterCount = static_cast<std::uint32_t>(count);
              for (int index = 0; index < count; ++index) {
                const auto *parameter = parameters[index];
                auto &destination = shared->parameters[index];
                destination.index = static_cast<std::uint32_t>(index);
                destination.flags =
                    (parameter->isAutomatable()
                         ? cydaw::plugins::parameterAutomatable
                         : 0u) |
                    (parameter->isDiscrete() ? cydaw::plugins::parameterDiscrete
                                             : 0u) |
                    (parameter->isBoolean() ? cydaw::plugins::parameterBoolean
                                            : 0u);
                destination.value = parameter->getValue();
                copyFixedText(destination.name, parameter->getName(95));
                copyFixedText(destination.label, parameter->getLabel());
                copyFixedText(destination.text,
                              parameter->getCurrentValueAsText());
              }
            }
          } else if (command ==
                     cydaw::plugins::SharedPluginCommand::setParameter) {
            if (instance == nullptr ||
                shared->commandParameterIndex >=
                    static_cast<std::uint32_t>(
                        instance->getParameters().size())) {
              shared->commandResult = 1;
              copyFixedText(shared->commandStatus, "Unknown plugin parameter");
            } else {
              instance
                  ->getParameters()[static_cast<int>(
                      shared->commandParameterIndex)]
                  ->setValueNotifyingHost(cydaw::base::limit(
                      0.0f, 1.0f, shared->commandParameterValue));
            }
          } else if (command == cydaw::plugins::SharedPluginCommand::getState) {
            cydaw::base::MemoryBlock state;
            if (instance != nullptr)
              instance->getStateInformation(state);
            if (state.getSize() >
                cydaw::plugins::maximumSharedPluginStateBytes) {
              shared->commandResult = 1;
              copyFixedText(shared->commandStatus,
                            "Plugin state exceeds 4 MB limit");
            } else {
              shared->stateBytes = static_cast<std::uint32_t>(state.getSize());
              if (!state.isEmpty())
                std::memcpy(shared->stateData, state.getData(),
                            state.getSize());
            }
          } else if (command == cydaw::plugins::SharedPluginCommand::setState) {
            if (shared->stateBytes >
                cydaw::plugins::maximumSharedPluginStateBytes) {
              shared->commandResult = 1;
              copyFixedText(shared->commandStatus, "Invalid plugin state size");
            } else if (instance != nullptr) {
              instance->setStateInformation(
                  shared->stateData, static_cast<int>(shared->stateBytes));
            }
          } else if (command ==
                     cydaw::plugins::SharedPluginCommand::showEditor) {
            if (instance == nullptr || !instance->hasEditor()) {
              shared->commandResult = 1;
              copyFixedText(shared->commandStatus,
                            "This plugin has no graphical editor");
            } else {
              if (editorWindow == nullptr) {
                auto editor =
                    std::unique_ptr<cydaw::plugins::NativePluginEditor>(
                        instance->createEditor().release());
                if (editor != nullptr)
                  editorWindow = std::move(editor);
              }
              if (editorWindow == nullptr) {
                shared->commandResult = 1;
                copyFixedText(shared->commandStatus,
                              "Plugin editor could not be created");
              } else {
                editorWindow->setVisible(true);
                editorWindow->toFront(true);
              }
            }
          } else if (command ==
                     cydaw::plugins::SharedPluginCommand::hideEditor) {
            if (editorWindow != nullptr)
              editorWindow->setVisible(false);
          } else if (command ==
                     cydaw::plugins::SharedPluginCommand::getPrograms) {
            cydaw::base::Text names;
            auto count = 0;
            if (instance != nullptr) {
              count = std::clamp(instance->getNumPrograms(), 0, 4096);
              for (int index = 0; index < count; ++index)
                names << (index > 0 ? "\n" : "")
                      << instance->getProgramName(index).replaceCharacters(
                             "\r\n", "  ");
              shared->commandParameterIndex = static_cast<std::uint32_t>(
                  std::max(0, instance->getCurrentProgram()));
            }
            const auto bytes = std::min<std::size_t>(
                names.getNumBytesAsUTF8(),
                cydaw::plugins::maximumSharedPluginStateBytes);
            if (bytes > 0)
              std::memcpy(shared->stateData, names.toRawUTF8(), bytes);
            shared->stateBytes = static_cast<std::uint32_t>(bytes);
            shared->commandParameterValue = static_cast<float>(count);
          } else if (command ==
                     cydaw::plugins::SharedPluginCommand::setProgram) {
            if (instance == nullptr ||
                shared->commandParameterIndex >=
                    static_cast<std::uint32_t>(
                        std::max(0, instance->getNumPrograms()))) {
              shared->commandResult = 1;
              copyFixedText(shared->commandStatus, "Unknown plugin program");
            } else {
              instance->setCurrentProgram(
                  static_cast<int>(shared->commandParameterIndex));
            }
          }
        } catch (const std::exception &error) {
          shared->commandResult = 1;
          copyFixedText(shared->commandStatus, cydaw::base::Text(error.what()));
        } catch (...) {
          shared->commandResult = 1;
          copyFixedText(shared->commandStatus, "Plugin command failed");
        }
        commandResponse.store(commandSequence, std::memory_order_release);
        didWork = true;
      }

#if defined(_WIN32)
      if ((editorWindow != nullptr && editorWindow->isVisible()) || !didWork)
        wake.waitAndDispatch();
#else
      if ((editorWindow != nullptr && editorWindow->isVisible()) || !didWork)
        cydaw::base::Thread::sleep(1);
#endif
    }
    editorWindow.reset();
    if (instance != nullptr)
      instance->releaseResources();
    setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::stopping,
                    "Worker stopped");
    return 0;
  } catch (const std::exception &exception) {
    setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                    cydaw::base::Text("Plugin exception: ") + exception.what());
    return 16;
  } catch (...) {
    setWorkerStatus(*shared, cydaw::plugins::SharedPluginState::error,
                    "Unknown plugin exception");
    return 17;
  }
}

} // namespace

int main(const int argc, char *argv[]) {
#if defined(_WIN32)
  struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() {
      if (SUCCEEDED(result))
        CoUninitialize();
    }
  } com;
#endif
  cydaw::base::TextList arguments;
  for (int index = 1; index < argc; ++index)
    arguments.add(cydaw::base::Text::fromUTF8(argv[index]));

  if (arguments.contains("--self-test") ||
      arguments.contains("--worker-handshake"))
    return runSelfTest();

  auto realtimeIndex = arguments.indexOf("--realtime-plugin");
  if (realtimeIndex < 0)
    realtimeIndex = arguments.indexOf("--realtime-vst3");
  const auto passthrough = arguments.contains("--realtime-passthrough");
  if ((realtimeIndex >= 0 && realtimeIndex + 1 < arguments.size()) ||
      passthrough) {
    const auto pluginFile =
        passthrough ? cydaw::base::File{}
                    : cydaw::base::File(arguments[realtimeIndex + 1]);
    const auto sharedFile =
        cydaw::base::File(argumentValue(arguments, "--shared-file"));
    const auto sampleRate =
        argumentValue(arguments, "--sample-rate", "48000").getDoubleValue();
    const auto maximumBlockSize =
        argumentValue(arguments, "--block-size", "512").getIntValue();
    const auto channels =
        argumentValue(arguments, "--channels", "2").getIntValue();
    const auto parentPid =
        argumentValue(arguments, "--parent-pid", "0").getLargeIntValue();
    const auto result =
        runRealtimeWorker(pluginFile, sharedFile, sampleRate, maximumBlockSize,
                          channels, passthrough, parentPid);
    // Nobody left to remove its shared file.
    if (result == parentGone)
      (void)sharedFile.deleteFile();
    return result;
  }

  auto probeIndex = arguments.indexOf("--probe-plugin");
  if (probeIndex < 0)
    probeIndex = arguments.indexOf("--probe-vst3");
  if (probeIndex >= 0 && probeIndex + 1 < arguments.size()) {
    cydaw::base::File outputFile;
    const auto outputIndex = arguments.indexOf("--output");
    if (outputIndex >= 0 && outputIndex + 1 < arguments.size())
      outputFile = cydaw::base::File(arguments[outputIndex + 1]);
    return probePlugin(cydaw::base::File(arguments[probeIndex + 1]), outputFile,
                       false);
  }

  auto validateIndex = arguments.indexOf("--validate-plugin");
  if (validateIndex < 0)
    validateIndex = arguments.indexOf("--validate-vst3");
  if (validateIndex >= 0 && validateIndex + 1 < arguments.size()) {
    cydaw::base::File outputFile;
    const auto outputIndex = arguments.indexOf("--output");
    if (outputIndex >= 0 && outputIndex + 1 < arguments.size())
      outputFile = cydaw::base::File(arguments[outputIndex + 1]);
    return probePlugin(cydaw::base::File(arguments[validateIndex + 1]),
                       outputFile, true);
  }

  std::cerr
      << "Usage: CyDAWPluginWorker --self-test | --probe-plugin <plugin> | "
         "--validate-plugin <plugin> [--output file.json] | "
         "--realtime-plugin <plugin> --shared-file <path>\n";
  return 2;
}
