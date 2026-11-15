#include <cydaw/base/Utilities.h>
#include "IsolatedPluginHost.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <new>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <malloc.h>
#endif


namespace
{
thread_local bool probing{};
thread_local std::size_t allocations{};
} // namespace
void* operator new(std::size_t n)
{
    if (probing)
        ++allocations;
    if (auto* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment)
{
    if (probing)
        ++allocations;
    const auto a = static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    auto* p = _aligned_malloc(n ? n : a, a);
#else
    auto* p = std::aligned_alloc(a, ((std::max(n, a) + a - 1) / a) * a);
#endif
    if (p)
        return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

namespace {
bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

#if defined(_WIN32)
std::pair<int, int> workerCounts()
{
    std::vector<PROCESSENTRY32W> entries;
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {-1, -1};
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) entries.push_back(entry);
    CloseHandle(snapshot);
    std::vector<DWORD> tree{GetCurrentProcessId()};
    for (std::size_t i = 0; i < tree.size(); ++i)
        for (const auto& e : entries)
            if (e.th32ParentProcessID == tree[i]) tree.push_back(e.th32ProcessID);
    int direct = 0, total = 0;
    for (const auto& e : entries)
        if (_wcsicmp(e.szExeFile, L"CyDAWPluginWorker.exe") == 0 &&
            std::find(tree.begin() + 1, tree.end(), e.th32ProcessID) != tree.end()) {
            // Toolhelp may retain a signalled process briefly after termination.
            // Count live workers, not kernel objects awaiting their last handle.
            auto process = OpenProcess(SYNCHRONIZE, FALSE, e.th32ProcessID);
            if (!process) {
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                return {-1, -1};
            }
            const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
            if (!running) continue;
            ++total;
            if (e.th32ParentProcessID == GetCurrentProcessId()) ++direct;
        }
    return {direct, total};
}
#endif

int main(const int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "FAILED: worker or isolated test-plugin argument is missing\n";
        return 1;
    }
    const cydaw::base::File worker(cydaw::base::Text::fromUTF8(argv[1]));
    const auto fixture = cydaw::base::File::getSpecialLocation(cydaw::base::File::tempDirectory)
        .getNonexistentChildFile("cydaw-single-worker-gain", ".jsfx");
    struct Cleanup { cydaw::base::File file; ~Cleanup() { file.deleteFile(); } } cleanup{fixture};
    const bool jsfx = std::string_view(argv[2]) == "--jsfx-fixture";
    if (jsfx && !fixture.replaceWithText("desc:Single worker gain\nslider1:0.5<0,1,.01>Gain\n"
        "@sample\nspl0*=slider1;spl1*=slider1;\n")) return 1;
    const cydaw::base::File pluginBinary = jsfx ? fixture : cydaw::base::File(cydaw::base::Text::fromUTF8(argv[2]));
    auto pluginBundle = pluginBinary;
    if (pluginBinary.getParentDirectory().getFileName().containsIgnoreCase("x86_64"))
        pluginBundle = pluginBinary.getParentDirectory().getParentDirectory().getParentDirectory();
    else if (pluginBinary.getParentDirectory().getFileName() == "MacOS")
        pluginBundle = pluginBinary.getParentDirectory().getParentDirectory();
    auto passed = expect(worker.existsAsFile(), "plugin worker exists");

    cydaw::app::IsolatedPluginHost host;
    const auto started = host.startPassthroughForTest(worker, 48'000.0, 64, 2);
    passed = expect(started.wasOk(), "start isolated passthrough worker") && passed;
    passed = expect(host.isRunning(), "worker reports running") && passed;

    cydaw::audio::SampleBuffer<float> buffer(2, 64);
    auto received = false;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.25f);
        received = host.processBlock(buffer, 0, buffer.getNumSamples());
        if (!received)
            cydaw::base::Thread::sleep(2);
    }
    passed = expect(received, "receive processed block from worker") && passed;
    if (received) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                passed = expect(std::abs(buffer.getSample(channel, sample) - 0.25f) < 0.00001f,
                                "shared-memory passthrough preserves samples") && passed;
    }
    passed = expect(host.latencySamples() >= 64, "worker reports pipeline latency") && passed;

    host.terminateWorkerForTest();
    for (int attempt = 0; attempt < 100 && host.isRunning(); ++attempt) {
        cydaw::base::Thread::sleep(2);
        host.pollWorkerHealth();
    }
    passed = expect(!host.isRunning(), "worker crash activates bypass") && passed;
    passed = expect(host.pluginName().isNotEmpty(), "crashed plugin identity is retained") && passed;
    passed = expect(host.statusMessage().containsIgnoreCase("bypass"),
                    "crash status reports safety bypass") && passed;
    host.stop();
    buffer.clear();
    buffer.applyGain(0.5f);
    passed = expect(!host.processBlock(buffer, 0, buffer.getNumSamples()),
                    "stopped worker returns safety bypass") && passed;
    passed = expect(!host.isRunning(), "worker stops cleanly") && passed;

#if defined(_WIN32) && !JUCE_PLUGINHOST_VST
    const auto disabledVst2Plugin = cydaw::base::File::getSpecialLocation(cydaw::base::File::tempDirectory)
                                        .getNonexistentChildFile("cydaw-disabled-vst2", ".dll", false);
    {
        auto output = disabledVst2Plugin.createOutputStream();
        passed = expect(output != nullptr, "create temporary disabled VST2 fixture") && passed;
        if (output != nullptr) {
            constexpr char peSignature[] { 'M', 'Z', 0, 0 };
            passed = expect(output->write(peSignature, sizeof(peSignature)),
                            "write temporary disabled VST2 fixture") && passed;
            output->flush();
        }
    }
    cydaw::app::IsolatedPluginHost disabledVst2Host;
    const auto disabledVst2Result = disabledVst2Host.startPluginForTest(
        worker, disabledVst2Plugin, 48'000.0, 64, 2);
    passed = expect(disabledVst2Result.failed()
                        && disabledVst2Result.getErrorMessage().containsIgnoreCase("VST2 hosting"),
                    "worker rejects VST2 explicitly when the optional SDK is disabled") && passed;
    passed = expect(disabledVst2Plugin.deleteFile(),
                    "remove temporary disabled VST2 fixture") && passed;
#endif

    passed = expect(pluginBundle.exists(), "isolated test plugin exists") && passed;
    cydaw::app::IsolatedPluginHost vstHost;
    const auto vstStarted = vstHost.startPluginForTest(worker, pluginBundle, 48'000.0, 64, 2);
    if (vstStarted.failed())
        std::cerr << "Plugin worker error: " << vstStarted.getErrorMessage() << '\n'
                  << "Plugin file: " << pluginBundle.getFullPathName() << '\n';
    passed = expect(vstStarted.wasOk(), "load plugin in isolated worker") && passed;
#if defined(_WIN32)
    passed = expect(workerCounts() == std::pair{1, 1},
                    "one native plugin owns exactly one direct SDK child and no nested worker") && passed;
#endif
    passed = expect(vstHost.latencySamples() == 64, "one declared quantum, no nested SDK pipeline") && passed;
    std::vector<cydaw::app::IsolatedPluginHost::ParameterSnapshot> parameters;
    passed = expect(vstHost.refreshParameters(parameters).wasOk(),
                    "enumerate plugin parameters through control IPC") && passed;
    const auto findGain = [&parameters] {
        return std::find_if(parameters.begin(), parameters.end(), [](const auto& parameter) {
            return parameter.name == "Gain";
        });
    };
    auto gainParameter = findGain();
    passed = expect(gainParameter != parameters.end()
                        && std::abs(gainParameter->value - 0.5f) < 0.001f,
                    "plugin parameter metadata and value") && passed;
    cydaw::base::MemoryBlock initialState;
    passed = expect(vstHost.getState(initialState).wasOk() && !initialState.isEmpty(),
                    "retrieve plugin binary state") && passed;
    cydaw::base::MemoryBlock cachedState;
    passed = expect(vstHost.getCachedState(cachedState).wasOk()
                        && cachedState == initialState,
                    "reuse plugin state for non-blocking project autosave")
             && passed;
    const auto gainIndex = gainParameter != parameters.end() ? gainParameter->index : 0u;
    passed = expect(vstHost.setParameter(gainIndex, 0.25f).wasOk(),
                    "set plugin parameter through worker") && passed;
    cydaw::base::MemoryBlock editedState;
    passed = expect(vstHost.getState(editedState).wasOk() && !editedState.isEmpty(),
                    "save host parameter edit before any audio callback") && passed;
    passed = expect(vstHost.setState(editedState).wasOk(), "restore stopped parameter edit") && passed;
    parameters.clear();
    passed = expect(vstHost.refreshParameters(parameters).wasOk() && findGain() != parameters.end()
                        && std::abs(findGain()->value - .25f) < 1e-5f,
                    "saved stopped edit reaches the processor state") && passed;
    buffer.clear();
    for(int channel=0;channel<buffer.getNumChannels();++channel)
        for(int sample=0;sample<buffer.getNumSamples();++sample)buffer.setSample(channel,sample,.4f);
    const auto firstVstBlock = vstHost.processBlock(buffer,0,buffer.getNumSamples());
    passed = expect(!firstVstBlock && buffer.getMagnitude(0,0,buffer.getNumSamples())==0,
                    "an active worker never leaks dry audio before its first response") && passed;
    auto vstReceived = false;
    for (int attempt = 0; attempt < 100 && !vstReceived; ++attempt) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.4f);
        vstReceived = vstHost.processBlock(buffer, 0, buffer.getNumSamples());
        if (!vstReceived)
            cydaw::base::Thread::sleep(2);
    }
    passed = expect(vstReceived, "receive audio processed by plugin worker") && passed;
    if (vstReceived)
        passed = expect(std::abs(buffer.getSample(0, 12) - 0.1f) < 0.00001f,
                        "plugin processing changes audio outside host process") && passed;
    const cydaw::app::IsolatedPluginHost::RealtimeParameterValue realtimeGain {
        gainIndex, 0.75f
    };
    vstHost.publishRealtimeParameters(&realtimeGain, 1);
    auto automatedBlockReceived = false;
    for (int attempt = 0; attempt < 100 && !automatedBlockReceived; ++attempt) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.4f);
        if (vstHost.processBlock(buffer, 0, buffer.getNumSamples()))
            automatedBlockReceived = std::abs(buffer.getSample(0, 12) - 0.3f) < 0.00001f;
        if (!automatedBlockReceived)
            cydaw::base::Thread::sleep(2);
    }
    passed = expect(automatedBlockReceived,
                    "non-blocking realtime parameter mailbox reaches plugin worker") && passed;
    passed = expect(vstHost.setState(initialState).wasOk(),
                    "restore plugin binary state") && passed;
    parameters.clear();
    const auto refreshed = vstHost.refreshParameters(parameters);
    gainParameter = findGain();
    passed = expect(refreshed.wasOk() && gainParameter != parameters.end()
                        && std::abs(gainParameter->value - 0.5f) < 0.001f,
                    "restored plugin state updates parameter") && passed;
    // Leave a sameCycle ticket outstanding before opening the editor: SDK
    // window commands must not invalidate an audio ticket still owned by us.
    for (int ch = 0; ch < 2; ++ch) std::fill_n(buffer.getWritePointer(ch), 64, .4f);
    (void)vstHost.processBlock(buffer, 0, 64);
    passed = expect(vstHost.showEditor().wasOk(), "open plugin editor in worker process") && passed;
    passed = expect(vstHost.hideEditor().wasOk(), "hide plugin editor in worker process") && passed;
    for (int i = 0; i < 4; ++i) {
        for (int ch = 0; ch < 2; ++ch) std::fill_n(buffer.getWritePointer(ch), 64, .4f);
        (void)vstHost.processBlock(buffer, 0, 64);
        cydaw::base::Thread::sleep(3);
        vstHost.pollWorkerHealth();
    }
    passed = expect(vstHost.isRunning(), "editor control handoff preserves active SDK processing") && passed;
    // Timestamp oracle: several callback fragments form each SDK quantum.
    // The fake caller sleeps between fragments; this proves PCM/latency, not ASIO timing.
    passed = expect(vstHost.setState(initialState).wasOk(), "reset streaming pipeline") && passed;
    const auto missesBefore = vstHost.missedBlockCount();
    const int fragments[]{1, 7, 17, 39, 17, 47, 32, 32, 1, 63};
    cydaw::audio::SampleBuffer<float> partial(2, 80);
    std::size_t cursor = 0;
    bool exact = true;
    allocations = 0;
    for (int cycle = 0; cycle < 8; ++cycle)
        for (int frames : fragments) {
            for (int ch = 0; ch < 2; ++ch) {
                std::fill_n(partial.getWritePointer(ch), 80, -.9f);
                for (int i = 0; i < frames; ++i)
                    partial.setSample(ch, i + 3, .2f + float(cursor + i) * .00001f);
            }
            probing = true;
            (void)vstHost.processBlock(partial, 3, frames, double(cursor) / 48000., 120., true);
            probing = false;
            for (int ch = 0; ch < 2; ++ch) {
                exact = exact && partial.getSample(ch, 2) == -.9f && partial.getSample(ch, frames + 3) == -.9f;
                for (int i = 0; i < frames; ++i) {
                    const auto position = cursor + i;
                    const auto expected = position < 64 ? 0.f : (.2f + float(position - 64) * .00001f) * .5f;
                    exact = exact && std::abs(partial.getSample(ch, i + 3) - expected) < 1e-6f;
                }
            }
            cursor += frames;
            cydaw::base::Thread::sleep(3);
        }
    passed = expect(exact, "partial callbacks preserve all PCM at exactly 64 frames latency and start offset") && passed;
    passed = expect(allocations == 0, "prepared SDK callback performs no C++ allocation") && passed;
    passed = expect(vstHost.missedBlockCount() == missesBefore, "paced partial-block oracle has no missed quantum") && passed;
    if (jsfx) {
        passed = expect(vstHost.setState(initialState).wasOk() &&
                        vstHost.setParameter(gainIndex, .25f).wasOk(), "prepare fragment automation") && passed;
        std::size_t position = 0;
        bool offsetExact = true;
        for (int cycle = 0; cycle < 5; ++cycle)
            for (int frames : {17, 47}) {
                for (int ch = 0; ch < 2; ++ch) std::fill_n(buffer.getWritePointer(ch), 64, .4f);
                const cydaw::app::IsolatedPluginHost::RealtimeParameterValue value{gainIndex, .75f};
                probing = true;
                if (position == 17) vstHost.publishRealtimeParameters(&value, 1);
                (void)vstHost.processBlock(buffer, 0, frames, double(position) / 48000., 120., true);
                probing = false;
                for (int i = 0; i < frames; ++i) {
                    const auto frame = position + i;
                    const float expected = frame < 64 ? 0.f : frame < 81 ? .1f : .3f;
                    offsetExact = offsetExact && std::abs(buffer.getSample(0, i) - expected) < 1e-6f;
                }
                position += frames;
                cydaw::base::Thread::sleep(3);
            }
        passed = expect(offsetExact, "JSFX fragment automation retains exact offset 17 through the stream adapter") && passed;
        passed = expect(allocations == 0, "fragment automation performs no callback allocation") && passed;
    }
#if defined(_WIN32)
    vstHost.terminateWorkerForTest();
    for (int i = 0; i < 100 && vstHost.isRunning(); ++i) {
        cydaw::base::Thread::sleep(2); vstHost.pollWorkerHealth();
    }
    passed = expect(!vstHost.isRunning() && vstHost.statusMessage().containsIgnoreCase("bypass"),
                    "SDK worker crash activates safety bypass") && passed;
    for (int ch = 0; ch < 2; ++ch) std::fill_n(buffer.getWritePointer(ch), 64, .4f);
    passed = expect(!vstHost.processBlock(buffer, 0, 64) && buffer.getSample(0, 12) == .4f,
                    "crashed SDK path leaves dry audio untouched") && passed;
#endif
    vstHost.stop();
#if defined(_WIN32)
    // A forced Windows process termination completes asynchronously. Observe
    // the owned process tree for a bounded grace period, as the app acceptance
    // does; still require zero live workers, without removing any from the tree.
    for (int i = 0; i < 200 && workerCounts() != std::pair{0, 0}; ++i)
        cydaw::base::Thread::sleep(10);
    const auto closedCounts = workerCounts();
    if (closedCounts != std::pair{0, 0})
        std::cerr << "Live workers after shutdown grace period: direct=" << closedCounts.first
                  << " total=" << closedCounts.second << '\n';
    passed = expect(closedCounts == std::pair{0, 0}, "all owned SDK workers closed") && passed;
#endif
    if (passed) std::cout << "Single SDK worker, PCM fragments, zero callback allocations, state, automation, editor and crash cleanup passed\n";
    return passed ? 0 : 1;
}
