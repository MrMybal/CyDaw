// Render the actual exported runtime editor without an external host window.
#include "RuntimeDefinition.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdlib>
#include <iostream>

int main(int argc,char** argv)
{
    if(argc!=3) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File source {juce::String::fromUTF8(argv[1])};
    std::unique_ptr<juce::AudioProcessor> processor(createCatalogueProcessor(source));
    if(!processor) return 3;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
    if(!editor || editor->getWidth()<240 || editor->getHeight()<160) return 4;
    const auto bitmap=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
    const juce::File output {juce::String::fromUTF8(argv[2])};
    auto stream=output.createOutputStream();
    if(stream) { stream->setPosition(0); stream->truncate(); }
    const auto ok=stream && juce::PNGImageFormat().writeImageToStream(bitmap,*stream);
    stream.reset();
    std::cout<<processor->getName()<<" editor="<<editor->getWidth()<<"x"<<editor->getHeight()<<std::endl;
    // This short-lived console renderer has no desktop event loop to drain
    // Windows font-service callbacks during shutdown. All output is flushed.
    std::_Exit(ok ? 0 : 5);
}
