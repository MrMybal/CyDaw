#pragma once
#include <cydaw/document/NativeGraphState.h>
#include <juce_core/juce_core.h>
namespace cydaw::app {
using document::serialiseNativeDspGraphState;
using document::writeEqBands;
using document::readEqBands;
using document::restoreNativeDspGraphState;
inline std::optional<engine::NativeDspGraph> restoreNativeDspGraphState(const base::Value& state,juce::String& error){base::Text message;auto graph=document::restoreNativeDspGraphState(state,message);error=juce::String::fromUTF8(message.toRawUTF8());return graph;}
}
