// An exported graph instrument, played as a host plays it.
//
//     cydaw_graph_runtime_host <plugin.vst3> <state.json> [note]
//
// Loads the compiled CyDAW Graph Runtime plug-in (the code every exported
// device is built from), hands it a runtime state (the one an export embeds)
// through setStateInformation, as a host restoring a session does, then plays
// one note through processBlock, offline. Prints one JSON line:
// {"loaded": true, "rms": ..., "peak": ..., "name": ...}. Nothing reaches a
// sound device.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <iostream>

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 3) {
        std::cout << R"({"error": "usage: cydaw_graph_runtime_host <plugin.vst3> <state.json> [note]"})" << std::endl;
        return 2;
    }
    const juce::File bundle(juce::String::fromUTF8(argv[1]));
    const juce::File stateFile(juce::String::fromUTF8(argv[2]));
    const auto note = argc > 3 ? juce::String(argv[3]).getIntValue() : 69;
    const auto report = [](const juce::String& error) {
        std::cout << "{\"error\": " << juce::JSON::toString(error).toStdString() << "}" << std::endl;
        return 1;
    };

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, bundle.getFullPathName());
    if (types.isEmpty())
        return report("no plug-in in " + bundle.getFullPathName());
    constexpr auto rate = 48'000.0;
    constexpr auto block = 512;
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*types[0], rate, block, error);
    if (plugin == nullptr)
        return report("the plug-in did not load: " + error);

    const auto mode = argc > 4 ? juce::String(argv[4]) : juce::String("early");
    // Restored as a host restores it: before it is activated ("early"), or
    // after ("late"); "passthrough" gives it no state and a sine to pass.
    const auto state = stateFile.loadFileAsString();
    if (state.isEmpty() && mode != "passthrough")
        return report("no state in " + stateFile.getFullPathName());
    // A hosted VST3's state is its component's bytes in JUCE's container (as
    // getStateInformation gives it): the plug-in itself receives the bytes
    // through IComponent::setState, as from any host.
    juce::MemoryBlock wrapped;
    {
        const juce::MemoryBlock raw(state.toRawUTF8(), state.getNumBytesAsUTF8());
        juce::XmlElement container("VST3PluginState");
        container.createNewChildElement("IComponent")->addTextElement(raw.toBase64Encoding());
        juce::AudioProcessor::copyXmlToBinary(container, wrapped);
    }
    const auto restore = [&plugin, &wrapped] {
        plugin->setStateInformation(wrapped.getData(), static_cast<int>(wrapped.getSize()));
    };
    if (mode == "early")
        restore();
    // "set:<name>[|<other name>]=<0..1>" after the mode: a parameter the
    // host shows, by its name, moved as a host moves it (before the note).
    juce::Array<juce::var> moved;
    for (auto argument = 5; argument < argc; ++argument) {
        const auto text = juce::String::fromUTF8(argv[argument]);
        if (!text.startsWith("set:"))
            continue;
        const auto names = juce::StringArray::fromTokens(text.substring(4).upToLastOccurrenceOf("=", false, false), "|", "");
        const auto value = text.fromLastOccurrenceOf("=", false, false).getFloatValue();
        // "#<n>": the n-th parameter (a name with accents does not cross a
        // Windows command line intact).
        const auto byIndex = names.size() == 1 && names[0].startsWith("#") ? names[0].substring(1).getIntValue() : -1;
        auto index = 0;
        for (auto* parameter : plugin->getParameters())
            if (index++ == byIndex || names.contains(parameter->getName(128), true)) {
                parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
                moved.add(parameter->getName(128));
            }
    }

    // Stereo in and out, then one note for a second.
    auto layout = plugin->getBusesLayout();
    plugin->setPlayConfigDetails(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), rate, block);
    // Faster than real time, as a host's offline bounce.
    plugin->setNonRealtime(true);
    plugin->prepareToPlay(rate, block);
    if (mode == "late")
        restore();
    const auto channels = std::max(2, std::max(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buffer(channels, block);
    const auto total = static_cast<int>(rate);
    auto energy = 0.0;
    auto counted = 0;
    auto peak = 0.0f;
    for (auto start = 0; start < total; start += block) {
        buffer.clear();
        if (mode == "passthrough")
            for (auto index = 0; index < block; ++index)
                for (auto channel = 0; channel < channels; ++channel)
                    buffer.setSample(channel, index, 0.25f * std::sin(6.2831853f * 440.0f * static_cast<float>(start + index) / 48'000.0f));
        juce::MidiBuffer midi;
        if (start == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        plugin->processBlock(buffer, midi);
        for (auto index = 0; index < block; ++index) {
            const auto value = buffer.getSample(0, index);
            peak = std::max(peak, std::abs(value));
            if (start + index >= static_cast<int>(rate * 0.1)) {
                energy += static_cast<double>(value) * value;
                ++counted;
            }
        }
    }
    plugin->releaseResources();
    juce::ignoreUnused(layout);
    auto* result = new juce::DynamicObject();
    result->setProperty("loaded", true);
    result->setProperty("name", plugin->getName());
    result->setProperty("inputs", plugin->getTotalNumInputChannels());
    result->setProperty("outputs", plugin->getTotalNumOutputChannels());
    result->setProperty("acceptsMidi", plugin->acceptsMidi());
    result->setProperty("rms", counted > 0 ? std::sqrt(energy / counted) : 0.0);
    result->setProperty("peak", peak);
    // What a host lists, and what was moved.
    juce::Array<juce::var> names;
    for (auto* parameter : plugin->getParameters())
        if (parameter->getName(128).isNotEmpty())
            names.add(parameter->getName(128));
    result->setProperty("parameters", names);
    result->setProperty("moved", moved);
    std::cout << juce::JSON::toString(juce::var(result), true).toStdString() << std::endl;
    plugin.reset();
    return 0;
}
