"""Compile the native runtime once, with per-instance immutable definitions.
The source DAW is untouched. Licensing of JUCE is preserved, not relabelled MIT.
"""
import json
from pathlib import Path
from catalogue import ROOT,WORK

def prepare():
    sdk=WORK/'RuntimeSources';sdk.mkdir(exist_ok=True)
    source=(ROOT/'src/graph_plugin/GraphRuntimePlugin.cpp').read_text(encoding='utf-8')
    source=source.replace('#include <CyDawGeneratedGraphConfig.h>','#include "RuntimeDefinition.h"')
    source=source.replace('constexpr auto hostParameterSlotCount = std::size_t { 32 };','constexpr auto hostParameterSlotCount = std::size_t { 512 };')
    start=source.index('class GraphRuntimeProcessor final')
    end=source.index('GraphRuntimeEditor::GraphRuntimeEditor')
    processor=source[start:end]
    processor=processor.replace('GraphRuntimeProcessor()','GraphRuntimeProcessor(std::shared_ptr<const RuntimeDefinition> definition)')
    processor=processor.replace('AudioProcessor(makeBusesProperties()),','AudioProcessor(makeBusesProperties(*definition)),\n          definition_(std::move(definition)),')
    processor=processor.replace('cydaw::generated_graph::','definition_->')
    processor=processor.replace('if constexpr (','if (')
    processor=processor.replace('static BusesProperties makeBusesProperties()','static BusesProperties makeBusesProperties(const RuntimeDefinition& definition)')
    a=processor.index('static BusesProperties makeBusesProperties(')
    b=processor.index('struct RuntimeProgram',a)
    processor=processor[:a]+processor[a:b].replace('definition_->','definition.')+processor[b:]
    processor=processor.replace('    mutable juce::CriticalSection stateLock_;','    std::shared_ptr<const RuntimeDefinition> definition_;\n    mutable juce::CriticalSection stateLock_;')
    processor=processor.replace('mapping->nodes.size() <= hostParameterSlotCount','mapping->nodes.size() <= realtimeNodes_.size()')
    processor=processor.replace('std::array<cydaw::engine::NativeDspNode,\n                       hostParameterSlotCount> nodes;','auto& nodes = realtimeNodes_;')
    processor=processor.replace('slot < hostParameterSlotCount;','slot < parameterCapacity_;')
    processor=processor.replace('    std::array<GraphHostParameter*, hostParameterSlotCount> hostParameters_ {};','    std::vector<cydaw::engine::NativeDspNode> realtimeNodes_ {128};\n    std::array<GraphHostParameter*, hostParameterSlotCount> hostParameters_ {};')
    source=source[:start]+processor+source[end:]
    # Fixed identities/parameters are still used; the full graph editor is
    # available for loading SFZ/maps/models and editing device presentations.
    source=source.replace('if constexpr (cydaw::generated_graph::exactWrapper)','if (false)')
    source=source.replace('deviceSurface_.setCompactMode(true);','deviceSurface_.setPerformanceMode(true);')
    source=source.replace('    juce::TextButton graphButton_;','''    juce::TextButton graphButton_;
    juce::TextButton parametersButton_;
    std::unique_ptr<juce::GenericAudioProcessorEditor> parametersView_;''')
    source=source.replace('    addAndMakeVisible(graphButton_);','''    addAndMakeVisible(graphButton_);
    parametersButton_.setButtonText("Parametres");
    addAndMakeVisible(parametersButton_);
    parametersButton_.onClick=[this] {
        graph_.setVisible(false); deviceSurface_.setVisible(false);
        if(!parametersView_) {
            parametersView_=std::make_unique<juce::GenericAudioProcessorEditor>(processor_);
            addChildComponent(*parametersView_);
        }
        parametersView_->setBounds(getLocalBounds().withTrimmedTop(38));
        parametersView_->setVisible(true);
    };''')
    source=source.replace('    graphButton_.setBounds(toolbar.removeFromLeft(118));','    graphButton_.setBounds(toolbar.removeFromLeft(118));\n    toolbar.removeFromLeft(4); parametersButton_.setBounds(toolbar.removeFromLeft(118));')
    source=source.replace('    deviceSurface_.setBounds(bounds);','    deviceSurface_.setBounds(bounds);\n    if(parametersView_) parametersView_->setBounds(bounds);')
    source=source.replace('    graphEditorVisible_ = showGraph;','    if(parametersView_) parametersView_->setVisible(false);\n    graphEditorVisible_ = showGraph;')
    source=source.replace('return new cydaw::graph_plugin::GraphRuntimeProcessor();','return nullptr;')
    source=source[:source.index('juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()')]+'''
juce::AudioProcessor* createCatalogueProcessor(const juce::File& file)
{
    auto definition = loadRuntimeDefinition(file);
    if (!definition) return nullptr;
    return new cydaw::graph_plugin::GraphRuntimeProcessor(std::move(definition));
}
'''
    (sdk/'CatalogueRuntime.cpp').write_text(source,encoding='utf-8')
    print('RUNTIME PREPARED',sdk)

if __name__=='__main__': prepare()
