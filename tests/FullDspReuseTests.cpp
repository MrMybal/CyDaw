// Offline numerical/RT checks and optional before/after evidence. No device is
// opened.
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
#include "SynthParts.h"
#include <cydaw/engine/PrismSynth.h>
#include <cydaw/engine/SynthFamily.h>
using namespace cydaw::engine;
namespace
{
std::uint64_t dumpOffset{};
void emit(std::string name, std::vector<float>& samples, std::ostream* dump, double micros, std::size_t alloc)
{
    double peak = 0, energy = 0;
    for (auto v : samples)
    {
        if (!std::isfinite(v))
            throw std::runtime_error(name + ": nonfinite audio");
        peak = std::max(peak, std::abs(double(v)));
        energy += double(v) * v;
    }
    if (alloc)
        throw std::runtime_error(name + ": callback allocation");
    if (peak > 64)
        throw std::runtime_error(name + ": unbounded audio");
    if (dump)
        dump->write(reinterpret_cast<const char*>(samples.data()), std::streamsize(samples.size() * sizeof(float)));
    std::cout << name << "," << dumpOffset << "," << samples.size() << "," << peak << "," << energy << "," << micros << "," << alloc
              << "\n";
    dumpOffset += samples.size();
}
void effects(std::ostream* dump)
{
    constexpr unsigned maximum = 1024;
    constexpr std::array<unsigned, 7> blocks{0, 1, 17, 64, 256, 511, 1024};
    for (const auto& desc : effectCatalogue)
    {
        if (desc.midi || desc.control || desc.container)
            continue;
        NativeDspNode node;
        node.id = 1;
        node.kind = desc.kind;
        applyEffectDefaults(desc, node.parameters);
        auto fx = makeEffectProcessor(node);
        if (!fx)
            throw std::runtime_error("effect missing");
        fx->prepare({48000, maximum, 2});
        AudioBuffer in(2, maximum), out(2, maximum);
        std::vector<float> samples;
        samples.reserve(150000);
        ProcessContext context;
        std::uint64_t at = 0;
        std::size_t countAlloc = 0;
        double micros = 0;
        std::size_t measuredFrames = 0;
        for (unsigned b = 0; b < 168; ++b)
        {
            auto n = blocks[b % blocks.size()];
            if (b == 84)
            {
                auto changed = node;
                for (const auto& p : desc.parameters)
                    if (p.key == "mix" || p.key == "depth")
                        setEffectValue(changed.parameters, p, p.minimum + .65f * (p.maximum - p.minimum));
                updateEffectProcessor(*fx, changed);
            }
            for (unsigned ch = 0; ch < 2; ++ch)
                for (unsigned i = 0; i < n; ++i)
                    in.view(n).channel(ch)[i] =
                        at + i < 24000 ? float(.12 * std::sin((at + i) * (.021 + ch * .003)) + .03 * std::cos((at + i) * .09)) : 0;
            allocations = 0;
            probing = true;
            const auto start = std::chrono::steady_clock::now();
            fx->glideValues(n, 48000);
            fx->process(in.view(n), out.view(n), context);
            const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            probing = false;
            countAlloc += allocations;
            if (b >= 28)
            {
                micros += elapsed;
                measuredFrames += n;
            }
            for (unsigned ch = 0; ch < 2; ++ch)
                samples.insert(samples.end(), out.view(n).channel(ch), out.view(n).channel(ch) + n);
            at += n;
        }
        emit("fx:" + std::string(desc.typeKey), samples, dump, micros / std::max(1., double(measuredFrames) / 1024), countAlloc);
        fx->reset();
    }
}
void instruments(std::ostream* dump)
{
    for (int model = 1; model < synth::modelCount; ++model)
    {
        for (int variant = 0; variant < 3; ++variant)
        {
            const auto m = static_cast<synth::Model>(model);
            synth::warmUp(m);
            std::uint32_t seed = 0x13579bdu;
            synth::detail::parts::fixedSeeds = &seed;
            synth::Voice voice;
            voice.prepare(48000);
            auto values = synth::defaultValues(m);
            if (variant)
                for (std::size_t i = 0; i < synth::info(m).parameters.size(); ++i)
                {
                    const auto& p = synth::info(m).parameters[i];
                    if (p.kind == synth::Kind::choice && !p.key.starts_with("voice."))
                        values[i] = variant == 1 ? std::min(p.maximum, p.minimum + 1) : p.maximum;
                    if (variant == 2 && (p.key == "noise.thump" || p.key == "noise.release"))
                        values[i] = 0;
                }

            std::vector<float> samples;
            samples.reserve(131072);
            std::array<float, 512> left{}, right{};
            prism::Context context;
            voice.start(m, values, m == synth::Model::silex ? 42 : 57, .8f, -1, 48000);
            std::size_t countAlloc = 0;
            double micros = 0;
            std::size_t measuredFrames = 0;
            for (unsigned b = 0; b < 96; ++b)
            {
                auto n = b % 3 == 0 ? 17u : 512u;
                left.fill(0);
                right.fill(0);
                allocations = 0;
                probing = true;
                const auto start = std::chrono::steady_clock::now();
                if (b == 24)
                    voice.retarget(values, 64, .6f);
                if (b == 48)
                    voice.setExpression(.15, .85);
                if (b == 64)
                    voice.release();
                voice.render(values, context, left.data(), right.data(), int(n), 48000);
                const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
                if (b >= 16)
                {
                    micros += elapsed;
                    measuredFrames += n;
                }
                probing = false;
                countAlloc += allocations;
                samples.insert(samples.end(), left.begin(), left.begin() + n);
                samples.insert(samples.end(), right.begin(), right.begin() + n);
            }
            synth::detail::parts::fixedSeeds = nullptr;
            emit("synth:" + synth::info(m).prefix + ":" + std::to_string(variant), samples, dump, micros / (double(measuredFrames) / 1024),
                 countAlloc);
        }
    }
    auto values = prism::defaultValues();
    for (int osc = 0; osc < prism::oscillatorCount; ++osc)
        values[prism::oscBase(osc) + prism::oscRandom] = 0;
    auto tables = prism::resolveTables(values);
    prism::Voice voice;
    voice.prepare(48000);
    voice.start(values, 60, .8f, -1, 48000);
    std::vector<float> samples;
    std::array<float, 512> left{}, right{};
    prism::Context context;
    std::size_t countAlloc = 0;
    double micros = 0;
    std::size_t measuredFrames = 0;
    for (int b = 0; b < 96; ++b)
    {
        left.fill(0);
        right.fill(0);
        allocations = 0;
        probing = true;
        const auto start = std::chrono::steady_clock::now();
        if (b == 64)
            voice.release();
        voice.render(values, *tables, context, left.data(), right.data(), 512, 48000);
        const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        if (b >= 16)
        {
            micros += elapsed;
            measuredFrames += 512;
        }
        probing = false;
        countAlloc += allocations;
        samples.insert(samples.end(), left.begin(), left.end());
        samples.insert(samples.end(), right.begin(), right.end());
    }
    emit("synth:prisme", samples, dump, micros / (double(measuredFrames) / 1024), countAlloc);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        std::ofstream dump;
        if (argc == 3 && std::string_view(argv[1]) == "--dump")
        {
            dump.open(argv[2], std::ios::binary);
            if (!dump)
                throw std::runtime_error("dump");
        }
        std::cout << "name,offset,samples,peak,energy,usPer1024,callbackAllocations\n";
        effects(dump.is_open() ? &dump : nullptr);
        instruments(dump.is_open() ? &dump : nullptr);
        return 0;
    }
    catch (const std::exception& e)
    {
        probing = false;
        std::cerr << e.what() << "\n";
        return 1;
    }
}
