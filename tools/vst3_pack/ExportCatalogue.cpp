// Offline compiler of the actual factory graphs. No audio device or window.
#include "NodeGraphComponent.h"
#include "GraphPolySynthRenderer.h"
#include "NativeDspGraphState.h"
#include "SamplerMapFile.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdlib>
#include <iostream>

int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3) return 2;
    const juce::File list(juce::String::fromUTF8(argv[1]));
    const juce::File output(juce::String::fromUTF8(argv[2]));
    const auto jobs=juce::JSON::parse(list.loadFileAsString());
    auto* array=jobs.getArray();
    if(!array) return 2;
    output.createDirectory();
    for(const auto& job:*array) {
        const auto id=job.getProperty("id",{}).toString();
        const auto file=output.getChildFile(id+".json");
        if(file.existsAsFile()) continue;
        auto* root=new juce::DynamicObject;
        root->setProperty("id",id);
        cydaw::app::NodeGraphComponent graph;
        const auto instrument=static_cast<bool>(job.getProperty("instrument",false));
        const auto kind=job.getProperty("kind",{}).toString();
        const auto key=job.getProperty("key",{}).toString();
        const auto midi=job.getProperty("input",{}).toString()=="midi" && job.getProperty("output",{}).toString()=="midi";
        if(instrument || midi) graph.initialiseEmptyMidiTrackGraph();
        juce::Result inserted=juce::Result::ok();
        if(midi) {
            inserted=graph.insertFactoryDevice("poly-graph-synth");
            if(inserted.wasOk()) inserted=graph.insertNodeBeforeInstrument(key);
        } else if(kind=="preset" || (kind=="module" && instrument))
            inserted=graph.insertFactoryDevice(key);
        else if(job.getProperty("input",{}).toString()=="audio")
            inserted=graph.insertNodeBeforeOutput(key);
        else {
            const auto node=graph.addNodeForRemote(key,{.4f,.4f});
            if(node==0) inserted=juce::Result::fail("Unknown source node");
            else {
                (void)graph.disconnectNodesForRemote(1,2);
                if(!graph.connectNodesForRemote(node,2,0,0))
                    inserted=juce::Result::fail("The source could not reach the audio output");
            }
        }
        if(job.hasProperty("map")) {
            const juce::File map(job.getProperty("map",{}).toString());
            const auto json=juce::JSON::parse(map.loadFileAsString());
            for(const auto& n:graph.editorNodes())
                if(n.typeKey=="sampler") {
                    juce::String error;
                    if(!graph.setNodeSampler(n.id,json,error)) inserted=juce::Result::fail(error);
                    break;
                }
        }
        if(inserted.failed()) root->setProperty("error",inserted.getErrorMessage());
        root->setProperty("editor",graph.serialise());
        const auto compiled=graph.runtimeGraphExport();
        if(compiled.compilationResult.failed())
            root->setProperty("error",compiled.compilationResult.getErrorMessage());
        root->setProperty("native",cydaw::app::serialiseNativeDspGraphState(compiled.nativeGraph));
        root->setProperty("polySynth",cydaw::app::serialisePolySynthGraphPlan(compiled.instrumentPlan));
        juce::Array<juce::var> parameters;
        for(const auto& p:compiled.publishedParameters) {
            auto* d=new juce::DynamicObject;
            d->setProperty("id",static_cast<juce::int64>(p.id));
            d->setProperty("key",juce::String(p.key));
            d->setProperty("name",juce::String(p.displayName));
            d->setProperty("unit",juce::String(p.unit));
            d->setProperty("minimum",p.minimum); d->setProperty("maximum",p.maximum);
            d->setProperty("default",p.defaultValue);
            d->setProperty("targetNodeId",static_cast<juce::int64>(p.targetNodeId));
            d->setProperty("targetParameter",juce::String(p.targetParameterKey));
            d->setProperty("automatable",p.automatable); d->setProperty("modulatable",p.modulatable);
            d->setProperty("visibleInDevice",p.visibleInDevice); d->setProperty("exportedToHost",p.exportedToHost);
            parameters.add(juce::var(d));
        }
        root->setProperty("publishedParameters",parameters);
        file.replaceWithText(juce::JSON::toString(juce::var(root),false));
        std::cout<<id<<" parameters="<<parameters.size()<<" "<<compiled.compilationResult.getErrorMessage()<<std::endl;
    }
    std::cout<<"COMPILATION COMPLETE"<<std::endl;
    std::_Exit(0);
}
