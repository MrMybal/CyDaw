#pragma once

#include <cydaw/engine/AudioProcessor.h>
#include <cydaw/engine/Convolver.h>
#include <cydaw/engine/EffectCatalogue.h>

#include <array>
#include <atomic>
#include <memory>

namespace cydaw::engine {

// The processors behind the effect catalogue: chorus, flanger, phaser,
// tremolo, bitcrusher, multimode and auto filters, three-band and parametric
// EQs, studio compressor, limiter, gate, transient shaper and ping-pong delay. Each reads its values from the
// payload fields its catalogue entry names, clamped to the entry's ranges,
// and allocates nothing outside prepare().
class EffectProcessor : public AudioProcessor {
public:
    explicit EffectProcessor(const EffectDescriptor& descriptor) noexcept;
    // Realtime-safe: the values are atomics.
    void setParameters(const NativeDspNodeParameters& parameters) noexcept;
    [[nodiscard]] const EffectDescriptor& descriptor() const noexcept { return descriptor_; }
    // The response a convolving effect plays (the cabinet's), given before
    // prepare(), off the audio thread. Others ignore it.
    virtual void setImpulse(ImpulseResponseRef) {}
    [[nodiscard]] virtual const ImpulseResponse* impulse() const noexcept { return nullptr; }

protected:
    // The n-th parameter of the catalogue entry.
    [[nodiscard]] float value(std::size_t index) const noexcept
    {
        return values_[index].load(std::memory_order_relaxed);
    }

private:
    const EffectDescriptor& descriptor_;
    std::array<std::atomic<float>, 64> values_ {};
};

// The processor for one of the catalogue's kinds, its values set; empty for
// any other kind.
[[nodiscard]] std::unique_ptr<AudioProcessor> makeEffectProcessor(const NativeDspNode& node);
// New values for an effect processor; false when it is not one of them.
bool updateEffectProcessor(AudioProcessor& processor, const NativeDspNode& node) noexcept;

} // namespace cydaw::engine
