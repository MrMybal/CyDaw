#include <cydaw/base/File.h>
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
namespace cydaw::plugins
{
std::unique_ptr<juce::AudioPluginFormat> extraPluginFormat(const cydaw::base::File &);
cydaw::base::Value validateJsfxGraphics(juce::AudioPluginInstance &);
bool isExtraFormat(const cydaw::base::File &);
std::unique_ptr<juce::AudioPluginInstance> createExtraPlugin(const cydaw::base::File &, double, int, cydaw::base::Text &,
                                                             const cydaw::base::Text &id = {});
void scanExtraPlugin(const cydaw::base::File &, juce::OwnedArray<juce::PluginDescription> &, cydaw::base::Text &);
} // namespace cydaw::plugins
