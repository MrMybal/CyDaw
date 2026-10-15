#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <CyDawGeneratedGraphConfig.h>
#include <memory>
#include <vector>

struct RuntimeDefinition {
    bool exactWrapper {true};
    juce::String productName;
    int mainInputChannels {2}, mainOutputChannels {2};
    const char* mainInputName {"Input"};
    const char* mainOutputName {"Output"};
    bool acceptsMidi {}, producesMidi {};
    bool midiOnly {};
    std::string deviceKey;
    std::vector<cydaw::generated_graph::ParameterConfig> parameters;
    std::vector<std::string> names, units;
    std::string state;
    std::vector<const char*> embeddedStateJsonChunks;
};

inline std::shared_ptr<const RuntimeDefinition> loadRuntimeDefinition(const juce::File& file)
{
    const auto value=juce::JSON::parse(file.loadFileAsString());
    if(!value.isObject()) return {};
    auto d=std::make_shared<RuntimeDefinition>();
    d->productName=value.getProperty("name",file.getFileNameWithoutExtension());
    d->mainInputChannels=static_cast<int>(value.getProperty("inputs",2));
    d->mainOutputChannels=static_cast<int>(value.getProperty("outputs",2));
    d->acceptsMidi=static_cast<bool>(value.getProperty("acceptsMidi",false));
    d->producesMidi=static_cast<bool>(value.getProperty("producesMidi",false));
    d->midiOnly=static_cast<bool>(value.getProperty("midiOnly",false));
    d->deviceKey=value.getProperty("deviceKey",{}).toString().toStdString();
    auto state=value.getProperty("state",juce::var());
    auto root=file.getParentDirectory();
    for(int depth=0;depth<12 && !root.getChildFile(".cydaw-vst-root").existsAsFile();++depth)
        root=root.getParentDirectory();
    if(root.getChildFile(".cydaw-vst-root").existsAsFile()) {
        state.getDynamicObject()->setProperty("factoryFolder",root.getChildFile("Donnees/Factory").getFullPathName());
        state.getDynamicObject()->setProperty("banksFolder",root.getChildFile("Donnees/Banks").getFullPathName());
    }
    if(const auto* parameters=state.getProperty("publishedParameters",{}).getArray()) {
        d->names.reserve(parameters->size()); d->units.reserve(parameters->size());
        d->parameters.reserve(parameters->size());
        for(const auto& p:*parameters) {
            d->names.push_back(p.getProperty("name",{}).toString().toStdString());
            d->units.push_back(p.getProperty("unit",{}).toString().toStdString());
            d->parameters.push_back({static_cast<std::uint32_t>(static_cast<juce::int64>(p.getProperty("id",0))),
                d->names.back().c_str(),d->units.back().c_str(),
                static_cast<float>(p.getProperty("minimum",0)),static_cast<float>(p.getProperty("maximum",1)),
                static_cast<float>(p.getProperty("default",0)),static_cast<bool>(p.getProperty("automatable",true))});
        }
    }
    d->state=juce::JSON::toString(state,false).toStdString();
    d->embeddedStateJsonChunks.push_back(d->state.c_str());
    return d;
}

juce::AudioProcessor* createCatalogueProcessor(const juce::File& file);
