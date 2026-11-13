#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
namespace cydaw::plugins {
std::unique_ptr<juce::AudioPluginFormat> cyAudioCoreVst3Format();
}
