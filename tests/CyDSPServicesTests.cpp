#include <cydsp/core/AudioBuffer.h>
#include <cydsp/io/AudioDevice.h>
#include <cydsp/io/AudioMetadata.h>
#include <cydsp/io/EditingPcmProxy.h>
#include <cydsp/io/MidiDevice.h>
#include <cydsp/plugins/Plugin.h>
#include <cydsp/plugins/PluginWorker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace cydsp;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void check(Status status, const char* message) {
    if (status != Status::ok) throw std::runtime_error(std::string(message) + " (status " + std::to_string(int(status)) + ")");
}
struct Scratch {
    std::filesystem::path root;
    Scratch() {
        root = std::filesystem::temp_directory_path() / ("cydaw-cydsp-services-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(root), "unique scratch directory");
    }
    ~Scratch() { std::error_code error; std::filesystem::remove_all(root, error); }
};
void selectors() {
    std::array<AudioDeviceInfo, 2> devices;
    for (auto& d : devices) { d.name = "Same device"; d.backend = AudioBackend::wasapi; d.persistentIdentity = true; }
    devices[0].id = "session-a"; devices[0].persistentId = "stable-a";
    devices[1].id = "session-b"; devices[1].persistentId = "stable-b";
    AudioDeviceSelector selector; selector.backend = AudioBackend::wasapi; selector.name = "Same device"; selector.allowNameFallback = true;
    std::size_t selected = 17;
    require(resolveAudioDevice(devices, selector, selected) == Status::ambiguous && selected == 17, "ambiguous audio selection cannot pick the first device");
    selector.persistentId = "stable-b";
    check(resolveAudioDevice(devices, selector, selected), "persistent audio selection"); require(selected == 1, "persistent device selected");
    MidiDevicePorts ports; ports.backend = "winmm"; ports.inputs = {{0, "Keyboard", "midi-a", true}, {1, "Keyboard", "midi-b", true}};
    MidiPortSelector midi; midi.backend = "winmm"; midi.name = "Keyboard"; midi.allowNameFallback = true;
    std::uint32_t index = 71;
    require(resolveMidiPort(ports, midi, index) == Status::ambiguous && index == 71, "ambiguous MIDI selection");
    midi.persistentId = "midi-b"; std::string serialized; check(serializeMidiPortSelector(midi, serialized), "serialize MIDI selector");
    MidiPortSelector restored; check(parseMidiPortSelector(serialized, restored), "parse MIDI selector");
    check(resolveMidiPort(ports, restored, index), "persistent MIDI selection"); require(index == 1, "persistent MIDI identity");
}
void audioClock() {
    // Explicit null backend: this acceptance never opens a physical endpoint.
    AudioDeviceContext context; check(context.prepare({AudioBackend::nullDevice}), "null audio context");
    struct Observations { std::atomic<unsigned> calls{}, valid{}, errors{}; } observations;
    AudioDevice device; AudioDeviceConfig config; config.periodFrames = 64; config.maxCallbackFrames = 512; config.generation = 7; config.diagnostics = true;
    check(device.openWithContext(context, config, [](void* user, AudioBlock<const float>, AudioBlock<float> out, const AudioCallbackContext& c) noexcept {
        auto& o = *static_cast<Observations*>(user);
        for (std::size_t ch = 0; ch < out.channels(); ++ch) std::fill(out.channel(ch).begin(), out.channel(ch).end(), 0.0f);
        if (c.clockValid && c.callbackTime.valid) ++o.valid;
        if (c.generation != 7 || c.adcTime.valid || c.dacTime.valid) ++o.errors;
        ++o.calls; return Status::ok;
    }, &observations), "null stream");
    check(device.start(), "null start");
    for (unsigned i = 0; i < 100 && observations.calls < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(device.stop(), "null stop");
    require(observations.valid > 0 && observations.errors == 0, "callback generation/clock without fabricated hardware timestamps");
    AudioStreamDescription description; check(device.description(description), "stream description");
    require(description.backend == AudioBackend::nullDevice && description.sampleRate == 48000, "negotiated stream");
    require(!description.output.latency.valid && device.diagnostics().callbacks > 0, "unknown hardware latency, actual diagnostics");
    device.close();
}
void proxy(const std::filesystem::path& root) {
    const auto source = root / "source.wav", cache = root / "editing.wav";
    AudioFileInfo info; info.sampleRate = 48000; info.channels = 2; info.encoding = AudioEncoding::float64; info.formatName = "WAV";
    AudioMetadata metadata; metadata.markers.push_back({7, 19001, "Door impact", "Sync"}); metadata.loops.push_back({2, 32, 128});
    check(setAudioMetadata(info, metadata), "typed metadata write");
    AudioFileWriter writer; check(writer.open(source, AudioFileFormat::wav, info), "source writer");
    AudioBuffer<double> data; check(data.resize(2, 4096), "source storage");
    for (std::uint64_t first = 0; first < 20037;) {
        const auto n = std::size_t(std::min<std::uint64_t>(4096, 20037 - first)); auto b = data.block(n);
        for (std::size_t ch = 0; ch < 2; ++ch) for (std::size_t i = 0; i < n; ++i) b.channel(ch)[i] = std::sin(double(first + i) * .013 + ch) * .7;
        check(writer.write(b), "source samples"); first += n;
    }
    check(writer.close(), "source finalize");
    EditingPcmProxy editing; PcmProxyOptions options; options.maximumBytes = 1 << 20;
    check(editing.build(source, cache, options), "build bounded editing proxy");
    std::size_t got = 0; check(editing.read(19980, data.block(31), got), "exact cache seek near EOF");
    require(got == 31 && editing.statistics().framesCopied == 20037, "proxy frame counts");
    for (std::size_t i = 0; i < 31; ++i) require(std::abs(data.block(31).channel(1)[i] - std::sin(double(19980 + i) * .013 + 1) * .7) < 1e-12, "proxy sample exactness");
    editing.close();
    EditingPcmProxy collision; require(collision.build(source, cache, options) != Status::ok, "cache must never overwrite existing destination");
    AudioFileReader reader; check(reader.open(source), "source reread"); AudioMetadata restored;
    check(getAudioMetadata(*reader.info(), restored), "typed source metadata");
    require(restored.markers.size() == 1 && restored.markers[0].name == "Door impact" && restored.loops[0].endFrame == 128, "public metadata preservation");
}
struct PluginAudio {
    std::vector<AudioBuffer<float>> inputStorage, outputStorage;
    std::vector<AudioBlock<const float>> inputs;
    std::vector<AudioBlock<float>> outputs;
    PluginAudio(const PluginDescription& description, std::size_t frames) {
        inputStorage.resize(description.inputs.size()); outputStorage.resize(description.outputs.size());
        for (std::size_t i = 0; i < inputStorage.size(); ++i) {
            check(inputStorage[i].resize(description.inputs[i].channels, frames), "plugin input allocation");
            auto b = inputStorage[i].block(frames);
            for (std::size_t ch = 0; ch < b.channels(); ++ch) std::fill(b.channel(ch).begin(), b.channel(ch).end(), .5f);
            inputs.push_back(b);
        }
        for (std::size_t i = 0; i < outputStorage.size(); ++i) { check(outputStorage[i].resize(description.outputs[i].channels, frames), "plugin output allocation"); outputs.push_back(outputStorage[i].block(frames)); }
    }
};
void host(PluginFormat format, const std::filesystem::path& path, bool jsfxFixture, const std::filesystem::path& executable) {
    std::unique_ptr<PluginProvider> provider; check(createPluginProvider(format, provider), "create provider");
    std::vector<PluginDescriptor> descriptors; check(provider->scan(path, descriptors), "explicit module scan"); require(!descriptors.empty(), "module descriptors");
    PluginInstance instance; check(provider->create(path, descriptors[0].id, {}, instance), "plugin create");
    const auto description = *instance.description(); require(!description.outputs.empty(), "plugin audio outputs");
    PluginAudio audio(description, 64); PluginEventBuffer in, out; check(in.prepare(), "input events"); check(out.prepare(), "output events");
    if (jsfxFixture) { PluginEvent event; event.kind = PluginEventKind::parameterValue; event.id = 6; event.value = .75; event.frame = 32; check(in.add(event), "sample automation"); }
    PluginPrepareOptions prepare; prepare.maximumFrames = 64; check(instance.activate(prepare), "plugin activate"); check(instance.start(), "plugin start");
    PluginProcess<float> process {audio.inputs, audio.outputs, 64, &in, &out};
    check(instance.process(process), "plugin process");
    if (jsfxFixture) for (std::size_t i = 0; i < 64; ++i) require(std::abs(audio.outputs[0].channel(0)[i] - (i < 32 ? .25f : .375f)) < 1e-6, "JSFX exact-offset automation");
    check(instance.stop(), "plugin stop"); check(instance.deactivate(), "plugin deactivate");
    std::vector<std::uint8_t> state; check(instance.saveState(state), "plugin save state"); require(!state.empty(), "state payload");
    check(instance.loadState(state), "plugin state restore");
    if (jsfxFixture) {
        // Existing CyDaw uses CYJS/version1, not CyJSFX/version1. This test
        // confirms the compatibility boundary without pretending they are equal.
        const std::array<std::uint8_t, 20> legacy {'S','J','Y','C', 1,0,0,0, 0,0,0,0, 0,0,0,0,0,0,0,0};
        require(instance.loadState(legacy) == Status::unsupported_format, "legacy CyDaw JSFX state requires explicit translation");
    }
    check(instance.close(), "plugin close");
    PluginWorker worker; PluginWorkerOptions options; options.processing.maximumFrames = 64;
    check(worker.launch(executable, format, path, descriptors[0].id, options), "separate CyDSP worker");
    require(worker.info() && worker.info()->pipelineLatencyFrames == 64, "worker pipeline latency");
    in.reset(); PluginWorkerBlockResult result; bool received = false;
    for (unsigned tries = 0; tries < 500 && !received; ++tries) {
        const auto status = worker.process(process, result); require(status == Status::ok || status == Status::would_block, "worker process status");
        received = result.received; if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(received, "worker processed response");
    if (jsfxFixture) require(std::abs(audio.outputs[0].channel(0)[0] - .25f) < 1e-6, "worker samples");
    check(worker.saveState(state), "worker state save"); check(worker.loadState(state), "worker state restore"); worker.close();
    std::cout << "CyDSP host and separate worker passed: " << path.filename().string() << '\n';
}
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--cydsp-worker") return cydsp::runPluginWorker(argv[2]);
    try {
        const auto executable = std::filesystem::absolute(argv[0]);
        if (argc == 4 && std::string_view(argv[1]) == "--plugin") {
            const auto path = std::filesystem::absolute(argv[3]); const std::string_view name(argv[2]);
            const auto format = name == "clap" ? PluginFormat::clap : name == "vst3" ? PluginFormat::vst3 : name == "lv2" ? PluginFormat::lv2 : PluginFormat::custom;
            require(format != PluginFormat::custom, "format must be clap, vst3 or lv2"); host(format, path, false, executable); return 0;
        }
        Scratch scratch; selectors(); audioClock(); proxy(scratch.root);
        const auto script = scratch.root / "SparseGain.jsfx";
        { std::ofstream file(script); file << "desc:CyDaw sparse gain fixture\nslider7:0.5<0,1,0.01>Gain\nin_pin:left\nin_pin:right\nout_pin:left\nout_pin:right\n@sample\nspl0 *= slider7; spl1 *= slider7;\n"; require(bool(file), "write original script fixture"); }
        host(PluginFormat::jsfx, script, true, executable);
        std::cout << "CyDSP services acceptance passed without JUCE\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
