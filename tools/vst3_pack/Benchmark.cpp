// A real VST3 host: measures callbacks of the exported binaries, without GUI
// or an audio device. The timings are host-side wall times, not an Unreal test.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) {
        std::cerr << "usage: CyPackBenchmark <bundle.vst3> <effect|synth|kick> [block] [seconds]\n";
        return 2;
    }
    const auto fail = [](const juce::String& text) {
        std::cout << "{\"error\":" << juce::JSON::toString(text).toStdString() << "}" << std::endl;
        return 1;
    };
    const auto block = argc > 3 ? std::max(16, juce::String(argv[3]).getIntValue()) : 128;
    const auto seconds = argc > 4 ? std::max(1.0, juce::String(argv[4]).getDoubleValue()) : 8.0;
    constexpr double rate = 48000;
    const auto mode = juce::String(argv[2]);
    const auto effect = mode == "effect";
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    std::cerr << "scan" << std::endl;
    format.findAllTypesForFile(types, juce::String::fromUTF8(argv[1]));
    if (types.size() != 1)
        return fail("expected one VST3 class");
    juce::String error;
    const auto create = [&] {
        return format.createInstanceFromDescription(*types[0], rate, block, error);
    };
    const auto loadStart = Clock::now();
    std::cerr << "create" << std::endl;
    auto plugin = create();
    const auto loadMs = std::chrono::duration<double, std::milli>(Clock::now()-loadStart).count();
    if (!plugin)
        return fail(error);
    if (plugin->getTotalNumOutputChannels() != 2 || plugin->getTotalNumInputChannels() != (effect ? 2 : 0))
        return fail("unexpected bus layout");
    auto& params = plugin->getParameters();
    if (params.isEmpty())
        return fail("no published parameters");
    // Validate the saved state through a new hosted instance, not through
    // assumptions about the internal JSON or a renamed copy of one binary.
    const auto original = params[0]->getValue();
    std::cerr << "save state" << std::endl;
    params[0]->setValueNotifyingHost(.17f);
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    auto restored = create();
    std::cerr << "restore state" << std::endl;
    if (!restored || state.getSize() == 0)
        return fail("state creation failed");
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (std::abs(restored->getParameters()[0]->getValue()-.17f) > .002f)
        return fail("parameter did not survive state roundtrip");
    auto firstImpulseSample = -1;
    if (effect) {
        restored->getParameters()[0]->setValueNotifyingHost(original);
        restored->setPlayConfigDetails(2,2,rate,block);
        restored->prepareToPlay(rate,block);
        juce::AudioBuffer<float> probe(2,block);
        juce::MidiBuffer empty;
        for (int start=0;start<4800;start+=block) {
            probe.clear();
            if (start==0)
                for (int c=0;c<2;++c)
                    probe.setSample(c,0,.01f);
            restored->processBlock(probe,empty);
            if (firstImpulseSample<0)
                for (int i=0;i<block;++i)
                    if (std::abs(probe.getSample(0,i))>1e-7f) {
                        firstImpulseSample=start+i;
                        break;
                    }
        }
        restored->releaseResources();
    }
    restored.reset();
    std::cerr << "prepare" << std::endl;
    params[0]->setValueNotifyingHost(original);
    plugin->setNonRealtime(false);
    plugin->setPlayConfigDetails(effect ? 2 : 0, 2, rate, block);
    plugin->prepareToPlay(rate, block);
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    const auto count = static_cast<int>(seconds*rate/block);
    constexpr int warmup = 200;
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(count));
    auto sum = 0.0, sumEnergy = 0.0, deltaEnergy = 0.0;
    auto peak = 0.0f;
    std::uint32_t random = 0x5a91b2c3;
    bool finite = true;
    int nextKick = 0;
    for (int b=-warmup; b<count; ++b) {
        if (b==-warmup)
            std::cerr << "process" << std::endl;
        const auto sampleStart = (b+warmup)*block;
        midi.clear();
        buffer.clear();
        if (effect) {
            for (int i=0; i<block; ++i) {
                random = random*1664525u+1013904223u;
                const auto noise = (static_cast<float>(random >> 8)/16777216.0f-.5f)*.045f;
                const auto t = static_cast<double>(sampleStart+i)/rate;
                const auto pulse = (sampleStart+i)%24000 < 90 ? .09f : 0.0f;
                buffer.setSample(0,i,static_cast<float>(.12*std::sin(6.283185307*220*t)+.035*std::sin(6.283185307*3100*t))+noise+pulse);
                buffer.setSample(1,i,static_cast<float>(.1*std::sin(6.283185307*330*t)+.03*std::sin(6.283185307*4700*t))-noise+pulse);
            }
        } else if (mode == "kick") {
            while (nextKick < sampleStart+block) {
                midi.addEvent(juce::MidiMessage::noteOn(1,36,.8f), std::max(0,nextKick-sampleStart));
                nextKick += 4800;
            }
        } else if (b == -warmup) {
            for (const auto note : {48,55,60,64})
                midi.addEvent(juce::MidiMessage::noteOn(1,note,.7f),0);
        }
        // Update one host parameter periodically; keep the benchmark around
        // factory defaults instead of pushing a filter into a silent extreme.
        if (b >= 0 && b%20 == 0)
            params[0]->setValueNotifyingHost(juce::jlimit(0.0f,1.0f,original+(b%40 == 0 ? .01f : -.01f)));
        const auto before = effect ? buffer.getSample(0,0) : 0.0f;
        const auto begin = Clock::now();
        plugin->processBlock(buffer,midi);
        const auto us = std::chrono::duration<double,std::micro>(Clock::now()-begin).count();
        if (b >= 0) {
            times.push_back(us);
            sum += us;
            deltaEnergy += std::pow(buffer.getSample(0,0)-before,2);
            for (int c=0;c<2;++c)
                for (int i=0;i<block;++i) {
                    const auto v=buffer.getSample(c,i);
                    finite = finite && std::isfinite(v);
                    peak = std::max(peak,std::abs(v));
                    sumEnergy += static_cast<double>(v)*v;
                }
        }
    }
    const auto latency = plugin->getLatencySamples();
    std::cerr << "report" << std::endl;
    plugin->releaseResources();
    const auto rms = std::sqrt(sumEnergy/(count*block*2.0));
    if (!finite || rms<.000001 || peak>8)
        return fail("invalid, silent, or unstable audio");
    std::sort(times.begin(),times.end());
    const auto percentile = [&](double p) { return times[static_cast<std::size_t>(p*(times.size()-1))]; };
    const auto budget = block/rate*1e6;
    auto* result = new juce::DynamicObject;
    result->setProperty("name",plugin->getName());
    result->setProperty("classUid",types[0]->uniqueId);
    result->setProperty("version",types[0]->version);
    result->setProperty("parameters",params.size());
    result->setProperty("programs",plugin->getNumPrograms());
    result->setProperty("hasEditor",plugin->hasEditor());
    result->setProperty("stateRoundtrip",true);
    result->setProperty("stateBytes",static_cast<juce::int64>(state.getSize()));
    result->setProperty("sampleRate",rate);
    result->setProperty("blockSize",block);
    result->setProperty("workload",mode == "synth" ? "4 held voices" : mode == "kick" ? "10 hits/sec" : "stereo multitone + noise + transients");
    result->setProperty("blocks",count);
    result->setProperty("loadMs",loadMs);
    result->setProperty("meanUs",sum/count);
    result->setProperty("p95Us",percentile(.95));
    result->setProperty("p99Us",percentile(.99));
    result->setProperty("maxUs",times.back());
    result->setProperty("audioThreadPercent",sum/count/budget*100);
    result->setProperty("p99BudgetPercent",percentile(.99)/budget*100);
    result->setProperty("declaredLatencySamples",latency);
    result->setProperty("firstImpulseOutputSample",firstImpulseSample);
    result->setProperty("rms",rms);
    result->setProperty("peak",peak);
    result->setProperty("outputDifferenceRms",std::sqrt(deltaEnergy/count));
    result->setProperty("finite",finite);
    if (argc>5 && plugin->hasEditor()) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
        if (!editor || editor->getWidth()<240 || editor->getHeight()<160)
            return fail("editor construction failed");
        const auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
        juce::File file(juce::String::fromUTF8(argv[5]));
        auto stream=file.createOutputStream();
        if (!stream || !juce::PNGImageFormat().writeImageToStream(snapshot,*stream))
            return fail("editor snapshot failed");
        result->setProperty("editorSnapshot",file.getFullPathName());
    }
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX memory {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) {
        result->setProperty("hostPrivateMiB",static_cast<double>(memory.PrivateUsage)/1048576.0);
        result->setProperty("hostWorkingSetMiB",static_cast<double>(memory.WorkingSetSize)/1048576.0);
    }
#endif
    std::cout << juce::JSON::toString(juce::var(result),true).toStdString() << std::endl;
    plugin.reset();
    std::cerr << "done" << std::endl;
    return 0;
}
