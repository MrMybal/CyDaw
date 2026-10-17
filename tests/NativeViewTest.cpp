// Exercises an exported plugin's real HWND/D3D11 editor with the VST3 SDK.
// The parent stays hidden. It never captures or operates the user's desktop.
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include <windows.h>
#include <combaseapi.h>
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc, char **argv)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    if (argc < 2)
        return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string error;
    auto module = VST3::Hosting::Module::create(argv[1], error);
    if (!module)
    {
        std::cerr << error;
        return 1;
    }
    HostApplication host;
    PluginContextFactory::instance().setPluginContext(static_cast<IHostApplication *>(&host));
    const auto factory = module->getFactory();
    bool tested = false;
    for (const auto &info : factory.classInfos())
        if (info.category() == kVstAudioEffectClass)
        {
            PlugProvider provider(factory, info, true);
            if (!provider.initialize())
                return 3;
            auto controller = provider.getControllerPtr();
            if (!controller)
                return 4;
            auto component = provider.getComponentPtr();
            FUnknownPtr<IAudioProcessor> audio(component);
            if (!audio)
                return 12;
            ProcessSetup setup{kRealtime, kSample32, 128, 48000};
            if (audio->setupProcessing(setup) != kResultOk)
                return 13;
            const auto inputs = component->getBusCount(kAudio, kInput),
                       outputs = component->getBusCount(kAudio, kOutput);
            if (outputs != 1)
                return 14;
            for (int b = 0; b < inputs; ++b)
                component->activateBus(kAudio, kInput, b, true);
            component->activateBus(kAudio, kOutput, 0, true);
            component->setActive(true);
            audio->setProcessing(true);
            std::array<float, 128> inLeft, inRight, outLeft, outRight;
            float *inChannels[]{inLeft.data(), inRight.data()}, *outChannels[]{outLeft.data(), outRight.data()};
            AudioBusBuffers inBus{}, outBus{};
            inBus.numChannels = outBus.numChannels = 2;
            inBus.channelBuffers32 = inChannels;
            outBus.channelBuffers32 = outChannels;
            EventList events;
            Event note{};
            note.type = Event::kNoteOnEvent;
            note.noteOn.pitch = 60;
            note.noteOn.velocity = .7f;
            note.noteOn.noteId = 1;
            events.addEvent(note);
            ProcessData data{};
            data.processMode = kRealtime;
            data.symbolicSampleSize = kSample32;
            data.numSamples = 128;
            data.numInputs = inputs;
            data.numOutputs = 1;
            data.inputs = inputs ? &inBus : nullptr;
            data.outputs = &outBus;
            data.inputEvents = inputs ? nullptr : &events;
            const auto energy = [&] {
                double total = 0;
                for (int block = 0; block < 30; ++block)
                {
                    for (int i = 0; i < 128; ++i)
                        inLeft[i] = inRight[i] = .25f * std::sin((block * 128 + i) * .13f);
                    if (audio->process(data) != kResultOk)
                        return -1.0;
                    events.clear();
                    for (float v : outLeft)
                    {
                        if (!std::isfinite(v))
                            return -1.0;
                        total += v * v;
                    }
                }
                return total;
            };
            ParameterInfo parameter;
            const auto parameterIndex=argc>2?std::atoi(argv[2]):0;
            if(parameterIndex<0 || parameterIndex>=controller->getParameterCount())return 17;
            controller->getParameterInfo(parameterIndex, parameter);
            // Optional hidden parameter ID: verify that programs also restore
            // internal settings absent from the host's parameter list.
            if (argc > 5) {
                ParameterInfo programParameter{};
                bool foundProgram = false;
                for (int i = 0; i < controller->getParameterCount(); ++i) {
                    controller->getParameterInfo(i, programParameter);
                    if (programParameter.flags & ParameterInfo::kIsProgramChange) { foundProgram = true; break; }
                }
                if (!foundProgram) return 19;
                const auto hiddenId = static_cast<ParamID>(std::strtoul(argv[5], nullptr, 10));
                controller->setParamNormalized(programParameter.id, 0);
                const auto first = controller->getParamNormalized(hiddenId);
                controller->setParamNormalized(programParameter.id, 1);
                const auto last = controller->getParamNormalized(hiddenId);
                if (!std::isfinite(first) || !std::isfinite(last) || std::abs(first - last) < 1e-6) return 20;
                std::cout << "Internal parameter preset recall passed" << std::endl;
            }
            controller->setParamNormalized(parameter.id, 0);
            const auto a = energy();
            controller->setParamNormalized(parameter.id, 1);
            const auto b = energy();
            if (a < 0 || b < 0 || a + b < 1e-12)
                return 15;
            if (inputs && std::abs(a - b) < 1e-5)
                return 16;
            audio->setProcessing(false);
            component->setActive(false);
            const auto parent = CreateWindowExW(0, L"STATIC", L"CyDAW isolated native view test", WS_OVERLAPPEDWINDOW,
                                                0, 0, 1200, 900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                IPtr<IPlugView> view(controller->createView(ViewType::kEditor), false);
                if (!view)
                    return 5;
                ViewRect size;
                view->getSize(&size);
                if (size.getWidth() < 240 || size.getHeight() < 160)
                    return 6;
                if(argc>4 && (size.getWidth()!=std::atoi(argv[3]) || size.getHeight()!=std::atoi(argv[4])))return 18;
                if (view->attached(parent, kPlatformTypeHWND) != kResultOk)
                    return 7;
                HWND child = FindWindowExW(parent, nullptr, L"CyDAWNativePlugin", nullptr);
                if (!child)
                    return 8;
                const auto until = GetTickCount64() + 350;
                int timerFrames = 0;
                while (GetTickCount64() < until)
                {
                    MSG message;
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (message.message == WM_TIMER && message.hwnd == child)
                            ++timerFrames;
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                    Sleep(3);
                }
                if (timerFrames < 2)
                    return 9;
                if (view->removed() != kResultOk || FindWindowExW(parent, nullptr, L"CyDAWNativePlugin", nullptr))
                    return 10;
            }
            DestroyWindow(parent);
            tested = true;
        }
    PluginContextFactory::instance().setPluginContext(nullptr);
    std::cout << "Native audio/parameter processing and editor attach/render/detach/reopen "
              << (tested ? "passed" : "failed") << std::endl;
    return tested ? 0 : 11;
}
