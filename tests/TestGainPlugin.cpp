#include <juce_audio_utils/juce_audio_utils.h>

#include <cstring>

class CyDawTestGainProcessor final : public juce::AudioProcessor {
public:
    CyDawTestGainProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        gain_ = new juce::AudioParameterFloat(
            juce::ParameterID { "gain", 1 }, "Gain",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f,
            juce::AudioParameterFloatAttributes().withLabel("x"));
        addParameter(gain_);
    }

    const juce::String getName() const override { return "CyDAW Test Gain"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
               && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
                   || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain(gain_->get());
    }
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain(static_cast<double>(gain_->get()));
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor(*this);
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override
    {
        const auto value = gain_->get();
        destination.replaceAll(&value, sizeof(value));
    }
    void setStateInformation(const void* data, const int size) override
    {
        if (data == nullptr || size != static_cast<int>(sizeof(float)))
            return;
        float value {};
        std::memcpy(&value, data, sizeof(value));
        *gain_ = juce::jlimit(0.0f, 1.0f, value);
    }

private:
    juce::AudioParameterFloat* gain_ {};
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CyDawTestGainProcessor();
}
