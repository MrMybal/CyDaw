#include <cydaw/base/File.h>
#include <cydaw/integration/CyAudioCoreDeviceManager.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <iostream>
#include <memory>

namespace {

cydaw::base::Value stringArrayToVar(const juce::StringArray& values)
{
    cydaw::base::Array<cydaw::base::Value> result;
    for (const auto& value : values)
        result.add(value);
    return result;
}

bool containsIgnoreCase(const juce::StringArray& values, const juce::String& expected)
{
    return std::any_of(values.begin(), values.end(), [&expected](const auto& value) {
        return value.containsIgnoreCase(expected);
    });
}

juce::String argumentValue(const juce::StringArray& arguments,
                           const juce::String& key,
                           const juce::String& fallback = {})
{
    const auto index = arguments.indexOf(key);
    return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : fallback;
}

juce::String matchingName(const juce::StringArray& values, const juce::String& requested)
{
    for (const auto& value : values)
        if (value.containsIgnoreCase(requested))
            return value;
    return {};
}

struct SilentCallback final : juce::AudioIODeviceCallback {
    std::atomic<unsigned> calls { 0 };
    std::atomic<unsigned long long> frames { 0 };
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const*, int,
        float* const* outputs, int channels, int count,
        const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch=0; ch<channels; ++ch)
            if (outputs[ch]) std::fill_n(outputs[ch], count, 0.0f);
        frames.fetch_add(static_cast<unsigned>(count), std::memory_order_relaxed);
        calls.fetch_add(1, std::memory_order_release);
    }
};

} // namespace

int main(const int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
        arguments.add(juce::String::fromUTF8(argv[index]));

    cydaw::integration::DeviceManager manager;
    cydaw::base::Array<cydaw::base::Value> deviceTypes;
    juce::StringArray availableTypeNames;
    juce::StringArray allDeviceNames;
    for (auto* type : manager.getAvailableDeviceTypes()) {
        type->scanForDevices();
        const auto inputs = type->getDeviceNames(true);
        const auto outputs = type->getDeviceNames(false);
        availableTypeNames.add(type->getTypeName());
        allDeviceNames.addArray(inputs);
        allDeviceNames.addArray(outputs);

        auto description = cydaw::base::Object::Ptr(new cydaw::base::Object());
        description->setProperty("name", type->getTypeName());
        description->setProperty("inputs", stringArrayToVar(inputs));
        description->setProperty("outputs", stringArrayToVar(outputs));
        deviceTypes.add(cydaw::base::Value(description.get()));
    }

    cydaw::base::Array<cydaw::base::Value> midiInputs;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
        value->setProperty("name", device.name);
        value->setProperty("identifier", device.identifier);
        midiInputs.add(cydaw::base::Value(value.get()));
    }
    cydaw::base::Array<cydaw::base::Value> midiOutputs;
    for (const auto& device : juce::MidiOutput::getAvailableDevices()) {
        auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
        value->setProperty("name", device.name);
        value->setProperty("identifier", device.identifier);
        midiOutputs.add(cydaw::base::Value(value.get()));
    }

    auto report = cydaw::base::Object::Ptr(new cydaw::base::Object());
    report->setProperty("application", "CyDAW Audio Probe");
    report->setProperty("asioCompiled", CYDAW_WITH_ASIO != 0);
    report->setProperty("jackCompiled", JUCE_JACK != 0);
    report->setProperty("audioDeviceTypes", cydaw::base::Value(deviceTypes));
    report->setProperty("midiInputs", cydaw::base::Value(midiInputs));
    report->setProperty("midiOutputs", cydaw::base::Value(midiOutputs));
    auto exitCode = 0;
    if (arguments.contains("--self-test")) {
#if JUCE_WINDOWS
        if (!containsIgnoreCase(availableTypeNames, "Windows Audio"))
            exitCode = 10;
#endif
#if CYDAW_WITH_ASIO
        if (!containsIgnoreCase(availableTypeNames, "ASIO"))
            exitCode = 11;
#endif
#if JUCE_JACK
        if (!containsIgnoreCase(availableTypeNames, "JACK"))
            exitCode = 12;
#endif
    }

    const auto requiredTypeIndex = arguments.indexOf("--require-type");
    if (requiredTypeIndex >= 0
        && (requiredTypeIndex + 1 >= arguments.size()
            || !containsIgnoreCase(availableTypeNames, arguments[requiredTypeIndex + 1])))
        exitCode = 20;

    const auto requiredDeviceIndex = arguments.indexOf("--require-device");
    if (requiredDeviceIndex >= 0
        && (requiredDeviceIndex + 1 >= arguments.size()
            || !containsIgnoreCase(allDeviceNames, arguments[requiredDeviceIndex + 1])))
        exitCode = 21;

    const auto openType = argumentValue(arguments, "--open-type");
    if (openType.isNotEmpty()) {
        const auto requestedDevice = argumentValue(arguments, "--open-device");
        auto openResult = cydaw::base::Object::Ptr(new cydaw::base::Object());
        openResult->setProperty("requestedType", openType);
        openResult->setProperty("requestedDevice", requestedDevice);
        openResult->setProperty("requestedSampleRate",
                                argumentValue(arguments, "--sample-rate", "48000").getDoubleValue());
        openResult->setProperty("requestedBlockSize",
                                argumentValue(arguments, "--block-size", "64").getIntValue());

        juce::AudioIODeviceType* selectedType = nullptr;
        for (auto* type : manager.getAvailableDeviceTypes())
            if (type->getTypeName().containsIgnoreCase(openType)) {
                selectedType = type;
                break;
            }

        if (selectedType == nullptr) {
            openResult->setProperty("opened", false);
            openResult->setProperty("error", "Requested audio type was not found");
            exitCode = 30;
        } else {
            const auto inputs = selectedType->getDeviceNames(true);
            const auto outputs = selectedType->getDeviceNames(false);
            const auto inputName = matchingName(inputs, requestedDevice);
            const auto outputName = matchingName(outputs, requestedDevice);
            auto device = std::unique_ptr<juce::AudioIODevice>(
                selectedType->createDevice(outputName, inputName));
            if (device == nullptr) {
                openResult->setProperty("opened", false);
                openResult->setProperty("error", "Requested audio device could not be created");
                exitCode = 31;
            } else {
                juce::BigInteger inputChannels;
                juce::BigInteger outputChannels;
                for (int channel = 0; !arguments.contains("--output-only") && channel < std::min(2, device->getInputChannelNames().size()); ++channel)
                    inputChannels.setBit(channel);
                for (int channel = 0; channel < std::min(2, device->getOutputChannelNames().size()); ++channel)
                    outputChannels.setBit(channel);
                const auto error = device->open(
                    inputChannels, outputChannels,
                    argumentValue(arguments, "--sample-rate", "48000").getDoubleValue(),
                    argumentValue(arguments, "--block-size", "64").getIntValue());
                openResult->setProperty("opened", error.isEmpty());
                openResult->setProperty("error", error);
                openResult->setProperty("deviceName", device->getName());
                openResult->setProperty("sampleRate", device->getCurrentSampleRate());
                openResult->setProperty("blockSize", device->getCurrentBufferSizeSamples());
                openResult->setProperty("inputLatencySamples", device->getInputLatencyInSamples());
                openResult->setProperty("outputLatencySamples", device->getOutputLatencyInSamples());
                openResult->setProperty("inputChannels", stringArrayToVar(device->getInputChannelNames()));
                openResult->setProperty("outputChannels", stringArrayToVar(device->getOutputChannelNames()));
                const auto silentMs = juce::jlimit(0, 5000,
                    argumentValue(arguments, "--run-silent-ms", "0").getIntValue());
                if (error.isEmpty() && silentMs > 0) {
                    SilentCallback callback;
                    device->start(&callback);
                    juce::Thread::sleep(silentMs);
                    const bool running = device->isPlaying();
                    device->stop();
                    openResult->setProperty("silentRunMs", silentMs);
                    openResult->setProperty("callbacks", static_cast<int>(callback.calls.load()));
                    openResult->setProperty("frames", static_cast<juce::int64>(callback.frames.load()));
                    openResult->setProperty("streamStarted", running && callback.calls.load() > 0);
                    openResult->setProperty("inputEnabled", !inputChannels.isZero());
                    if (!running || callback.calls.load() == 0) exitCode = 33;
                }
                device->close();
                if (error.isNotEmpty())
                    exitCode = 32;
            }
        }
        report->setProperty("openTest", cydaw::base::Value(openResult.get()));
    }

    report->setProperty("exitCode", exitCode);
    std::cout << cydaw::base::JSON::toString(cydaw::base::Value(report.get()), true).toStdString() << '\n';

    return exitCode;
}
