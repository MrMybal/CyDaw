#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <iostream>
#include <memory>

namespace {

juce::var stringArrayToVar(const juce::StringArray& values)
{
    juce::Array<juce::var> result;
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

} // namespace

int main(const int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
        arguments.add(juce::String::fromUTF8(argv[index]));

    juce::AudioDeviceManager manager;
    juce::Array<juce::var> deviceTypes;
    juce::StringArray availableTypeNames;
    juce::StringArray allDeviceNames;
    for (auto* type : manager.getAvailableDeviceTypes()) {
        type->scanForDevices();
        const auto inputs = type->getDeviceNames(true);
        const auto outputs = type->getDeviceNames(false);
        availableTypeNames.add(type->getTypeName());
        allDeviceNames.addArray(inputs);
        allDeviceNames.addArray(outputs);

        auto description = juce::DynamicObject::Ptr(new juce::DynamicObject());
        description->setProperty("name", type->getTypeName());
        description->setProperty("inputs", stringArrayToVar(inputs));
        description->setProperty("outputs", stringArrayToVar(outputs));
        deviceTypes.add(juce::var(description.get()));
    }

    juce::Array<juce::var> midiInputs;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("name", device.name);
        value->setProperty("identifier", device.identifier);
        midiInputs.add(juce::var(value.get()));
    }
    juce::Array<juce::var> midiOutputs;
    for (const auto& device : juce::MidiOutput::getAvailableDevices()) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("name", device.name);
        value->setProperty("identifier", device.identifier);
        midiOutputs.add(juce::var(value.get()));
    }

    auto report = juce::DynamicObject::Ptr(new juce::DynamicObject());
    report->setProperty("application", "CyDAW Audio Probe");
    report->setProperty("asioCompiled", JUCE_ASIO != 0);
    report->setProperty("jackCompiled", JUCE_JACK != 0);
    report->setProperty("audioDeviceTypes", juce::var(deviceTypes));
    report->setProperty("midiInputs", juce::var(midiInputs));
    report->setProperty("midiOutputs", juce::var(midiOutputs));
    auto exitCode = 0;
    if (arguments.contains("--self-test")) {
#if JUCE_WINDOWS
        if (!containsIgnoreCase(availableTypeNames, "Windows Audio"))
            exitCode = 10;
#endif
#if JUCE_ASIO
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
        auto openResult = juce::DynamicObject::Ptr(new juce::DynamicObject());
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
                for (int channel = 0; channel < std::min(2, device->getInputChannelNames().size()); ++channel)
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
                device->close();
                if (error.isNotEmpty())
                    exitCode = 32;
            }
        }
        report->setProperty("openTest", juce::var(openResult.get()));
    }

    report->setProperty("exitCode", exitCode);
    std::cout << juce::JSON::toString(juce::var(report.get()), true).toStdString() << '\n';

    return exitCode;
}
