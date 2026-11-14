// Offline numerical/RT checks and optional before/after evidence. No device is opened.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/EffectProcessors.h>
#include <fstream>
#include <iostream>
#include <new>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#include <stdexcept>
#include <vector>
namespace {
bool probing{};
std::size_t allocations{};
} // namespace
void *operator new(std::size_t n) {
    if (probing)
        ++allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, std::align_val_t alignment) {
    if (probing)
        ++allocations;
    const auto a = static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    auto *p = _aligned_malloc(n ? n : a, a);
#else
    auto *p = std::aligned_alloc(a, ((std::max(n, a) + a - 1) / a) * a);
#endif
    if (p)
        return p;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void *p, std::align_val_t) noexcept {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
using namespace cydaw::engine;
namespace {
void require(bool yes, const char *why) {
    if (!yes)
        throw std::runtime_error(why);
}
NativeDspNode nodeFor(bool eq, int variant) {
    NativeDspNode node;
    node.id = 1;
    node.kind = eq ? NativeDspNodeKind::threeBandEq : NativeDspNodeKind::reverbPro;
    const auto &desc = *effectForKind(node.kind);
    applyEffectDefaults(desc, node.parameters);
    const auto set = [&](const char *key, float v) {
        const auto *p = effectParameter(desc, key);
        require(p != nullptr, "missing parameter");
        setEffectValue(node.parameters, *p, v);
    };
    if (eq) {
        set("low", 6);
        set("mid", -4);
        set("high", 3);
    } else {
        set("mix", .7f);
        set("decay", 1.5f);
        set("brightness", .65f);
        set("low-cut", 90);
        set("high-cut", 9000);
        set("ducking", variant == 1 ? 9.f : 0.f);
        set("character", .5f);
    }
    return node;
}
void fill(AudioBlockView in, std::uint64_t offset, double rate) {
    for (std::uint32_t ch = 0; ch < in.channelCount; ++ch)
        for (std::uint32_t f = 0; f < in.frameCount; ++f) {
            const auto n = offset + f;
            in.channel(ch)[f] =
                n < static_cast<std::uint64_t>(rate)
                    ? float(.21 * std::sin(n * (.017 + ch * .003)) + .04 * std::cos(n * .401) + (n == 0 ? .6 : 0))
                    : 0.f;
        }
}
void scenario(bool eq, int variant, double rate, unsigned channels, std::ostream *dump) {
    auto node = nodeFor(eq, variant);
    auto fx = makeEffectProcessor(node);
    fx->prepare({rate, 1024, channels});
    AudioBuffer in(channels, 1024), out(channels, 1024);
    ProcessContext context;
    std::uint64_t offset = 0;
    allocations = 0;
    constexpr std::array<unsigned, 6> sizes{1024, 17, 256, 1, 511, 64};
    // Includes coefficient changes and freeze/unfreeze, well into the tail.
    for (unsigned block = 0; block < 384; ++block) {
        auto n = sizes[block % sizes.size()];
        auto source = in.view(n), dest = out.view(n);
        fill(source, offset, rate);
        probing = true;
        if (block == 96 || block == 192 || block == 288) {
            const auto &desc = *effectForKind(node.kind);
            const auto *p = effectParameter(desc, eq ? "mid" : "freeze");
            setEffectValue(node.parameters, *p, block == 192 ? 0.f : 1.f);
            require(updateEffectProcessor(*fx, node), "parameter update");
            fx->glideValues(n, rate);
        }
        fx->process(source, dest, context);
        probing = false;
        for (unsigned ch = 0; ch < channels; ++ch) {
            for (unsigned f = 0; f < n; ++f)
                require(std::isfinite(dest.channel(ch)[f]), "non-finite DSP output");
            if (dump)
                dump->write(reinterpret_cast<const char *>(dest.channel(ch)), n * sizeof(float));
        }
        offset += n;
    }
    require(allocations == 0, "DSP callback/automation allocated");
    in.clear(1024);
    probing = true;
    fx->reset();
    fx->process(in.view(0), out.view(0), context);
    fx->process(in.view(1024), out.view(1024), context);
    probing = false;
    require(allocations == 0, "DSP reset/empty callback allocated");
    for (unsigned ch = 0; ch < channels; ++ch)
        for (unsigned f = 0; f < 1024; ++f)
            require(out.view(1024).channel(ch)[f] == 0, "reset leaves a tail");
}
void convolution(std::ostream *dump, bool benchmark) {
    Convolver fx;
    std::vector<float> impulse(4096);
    for (std::size_t i = 0; i < impulse.size(); ++i)
        impulse[i] = float(.04 * std::exp(-double(i) / 900) * std::cos(i * .09));
    fx.prepare(impulse);
    std::vector<float> input(32768), output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = float(.2 * std::sin(i * .017) + (i == 0 ? .5 : 0));
    allocations = 0;
    probing = true;
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = fx.process(input[i]);
    probing = false;
    require(allocations == 0, "convolver callback allocated");
    for (std::size_t i = 0; i < 5000; ++i) {
        double reference = 0;
        for (std::size_t k = 0; k < std::min(i + 1, impulse.size()); ++k)
            reference += double(impulse[k]) * input[i - k];
        require(std::abs(output[i] - reference) < 2.e-6, "convolver differs from direct FIR");
    }
    if (dump)
        dump->write(reinterpret_cast<const char *>(output.data()), output.size() * sizeof(float));
    if (benchmark) {
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < 8; ++pass)
            for (std::size_t i = 0; i < input.size(); ++i)
                output[i] = fx.process(input[i]);
        const auto us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 256;
        std::cout << "convolver,1024," << us << '\n';
    }
}
void basicCompressor(std::ostream *dump, bool benchmark) {
    CompressorProcessor fx(-18.0f, 4.0f);
    fx.prepare({48000, 1024, 2});
    AudioBuffer input(2, 1024), output(2, 1024), key(3, 1024);
    ProcessContext context;
    float envelope = 0;
    double maxError = 0;
    const auto attack = float(std::exp(-1.0 / (.008 * 48000))), release = float(std::exp(-1.0 / (.090 * 48000)));
    for (unsigned k = 0; k < 128; ++k) {
        fill(input.view(1024), k * 1024, 48000);
        fill(key.view(1024), k * 1024, 48000);
        std::array<AudioInputBusView, 2> buses{
            {{0, input.view(1024)}, {CompressorProcessor::sidechainPort, key.view(1024)}}};
        allocations = 0;
        probing = true;
        fx.processAudioBuses(buses, output.view(1024), context);
        probing = false;
        require(allocations == 0, "compressor callback allocated");
        float deepest = 0;
        for (unsigned f = 0; f < 1024; ++f) {
            float peak = 0;
            for (unsigned ch = 0; ch < 3; ++ch)
                peak = std::max(peak, std::abs(key.view(1024).channel(ch)[f]));
            const auto c = peak > envelope ? attack : release;
            envelope = c * envelope + (1 - c) * peak;
            const auto over = std::max(0.f, 20.f * std::log10(std::max(envelope, 1.e-9f)) + 18.f);
            const auto reduction = -over * .75f;
            deepest = std::min(deepest, reduction);
            const auto gain = std::pow(10.f, reduction / 20.f);
            for (unsigned ch = 0; ch < 2; ++ch)
                maxError = std::max(maxError, double(std::abs(output.view(1024).channel(ch)[f] -
                                                              input.view(1024).channel(ch)[f] * gain)));
        }
        require(std::abs(fx.gainReductionDb() - deepest) < .01, "compressor deepest gain reading changed");
        if (dump)
            for (unsigned ch = 0; ch < 2; ++ch)
                dump->write(reinterpret_cast<const char *>(output.view(1024).channel(ch)), 1024 * sizeof(float));
    }
    require(maxError < 2.e-5, "compressor differs from its legacy peak/8ms/90ms envelope");
    if (benchmark) {
        fill(input.view(1024), 0, 48000);
        for (int i = 0; i < 256; ++i)
            fx.process(input.view(1024), output.view(1024), context);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 2048; ++i)
            fx.process(input.view(1024), output.view(1024), context);
        const auto us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 2048;
        std::cout << "compressor,1024," << us << '\n';
    }
}
void bench(bool eq, unsigned n) {
    auto fx = makeEffectProcessor(nodeFor(eq, 1));
    fx->prepare({48000, n, 2});
    AudioBuffer in(2, n), out(2, n);
    fill(in.view(n), 0, 48000);
    ProcessContext context;
    for (int k = 0; k < 256; ++k)
        fx->process(in.view(n), out.view(n), context);
    auto start = std::chrono::steady_clock::now();
    for (int k = 0; k < 1024; ++k)
        fx->process(in.view(n), out.view(n), context);
    const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 1024;
    std::cout << (eq ? "eq" : "reverb") << ',' << n << ',' << us << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--bench") {
            for (auto n : {64u, 256u, 1024u}) {
                bench(false, n);
                bench(true, n);
            }
            convolution(nullptr, true);
            basicCompressor(nullptr, true);
            return 0;
        }
        std::ofstream dump;
        if (argc == 3 && std::string_view(argv[1]) == "--dump") {
            dump.open(argv[2], std::ios::binary);
            require(bool(dump), "cannot open dump");
        }
        for (auto rate : {44100., 48000., 96000.})
            for (auto channels : {1u, 2u, 4u})
                for (int v = 0; v < 2; ++v)
                    scenario(false, v, rate, channels, dump.is_open() ? &dump : nullptr);
        for (auto channels : {1u, 2u, 4u})
            scenario(true, 0, 48000, channels, dump.is_open() ? &dump : nullptr);
        convolution(dump.is_open() ? &dump : nullptr, false);
        basicCompressor(dump.is_open() ? &dump : nullptr, false);
        std::cout << "DSP reuse: finite audio, automation, variable/empty blocks, mono/stereo/4ch, reset and zero "
                     "callback allocations passed\n";
        return 0;
    } catch (const std::exception &e) {
        probing = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
