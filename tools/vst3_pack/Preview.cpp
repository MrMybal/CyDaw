// Render the exported presentation with the same JUCE component as the VST3.
// No desktop window or audio device is created.
#include "CyDawLookAndFeel.h"
#include "ModuleInterfaceEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc!=3)
        return 2;
    const juce::File input(juce::String::fromUTF8(argv[1]));
    const auto json=juce::JSON::parse(input.loadFileAsString());
    const auto state=cydaw::app::restoreModuleInterface(
        json.getProperty("graphEditorState",juce::var()).getProperty("presentation",juce::var()));
    const auto valid=cydaw::app::validateModuleInterface(state);
    if (valid.failed()) {
        std::cerr<<valid.getErrorMessage()<<std::endl;
        return 1;
    }
    cydaw::app::CyDawLookAndFeel style;
    cydaw::app::ModuleInterfaceEditorComponent panel(state);
    panel.setLookAndFeel(&style);
    panel.setPerformanceMode(true);
    panel.setSize(state.windowWidth!=0 ? state.windowWidth : 720,
                  state.windowHeight!=0 ? state.windowHeight : 420);
    panel.setVisible(true);
    const auto bitmap=panel.createComponentSnapshot(panel.getLocalBounds(),true,1.0f);
    auto stream=juce::File(juce::String::fromUTF8(argv[2])).createOutputStream();
    const auto ok=stream && juce::PNGImageFormat().writeImageToStream(bitmap,*stream);
    stream.reset();
    panel.setLookAndFeel(nullptr);
    std::cout<<"controls="<<state.controls.size()<<" parameters="<<state.parameters.size()<<std::endl;
    // This renderer never enters a desktop event loop. JUCE's font-service
    // shutdown can wait for that loop; finish the file and terminate this
    // short-lived rendering process explicitly. This is not a DSP test.
    std::_Exit(ok ? 0 : 1);
}
