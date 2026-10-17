#pragma once
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PolySynthGraph.h>
#include "CustomInterfaceABI.h"
#include <string>
#include <vector>
#include <cstdint>
namespace cydaw::native
{
struct ParameterDefinition
{
    std::uint32_t id{};
    std::string key, name, unit, binding;
    std::uint64_t node{};
    double minimum{}, maximum{1}, initial{};
    bool host{true};
};
struct ControlDefinition
{
    std::uint64_t id{};
    std::string kind, label, primary, secondary, tertiary, quaternary, page;
    std::vector<std::string> extra;
    std::string instrumentType,instrumentPanel;
    bool instrumentCompact{};
    std::vector<std::string> instrumentKeys;
    float x{}, y{}, width{.2f}, height{.3f};
    std::uint32_t accent{0xfff28a2e}, background{0xff2e2e2e}, text{0xffdadada};
    float fontScale{1}, rounding{4};
    bool hidden{}, showValue{}, large{};
    int pane{}, variant{};
    std::vector<int> indices;
    void (*custom)(const CyInterfaceContext *){};
};
struct ProgramDefinition
{
    std::string name;
    std::vector<double> values;
    std::vector<double> allValues;
};
struct BusDefinition
{
    std::string name;
    int channels{2};
    bool input{};
};
struct Definition
{
    std::string name, vendor, version, category;
    std::vector<ParameterDefinition> parameters;
    std::vector<ControlDefinition> controls;
    std::vector<ProgramDefinition> programs;
    std::vector<BusDefinition> buses;
    std::vector<engine::PolyVoiceModulation> voiceModulations;
    int width{1000}, height{700};
    std::uint32_t background{0xff252525}, accent{0xfff28a2e}, text{0xffdadada};
    bool acceptsMidi{}, producesMidi{}, synth{};
};
Definition makeDefinition();
engine::NativeDspGraph makeAudioGraph();
engine::PolySynthGraphPlan makeInstrumentPlan();
} // namespace cydaw::native
