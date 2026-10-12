#include <cydaw/engine/WavetableShape.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

WavetableShape defaultWavetableShape()
{
    // The same eight frames the built-in bank has always held: a sawtooth
    // truncated at a rising number of partials, each at 1/h. Seeding the
    // editor from this is why opening it changes nothing.
    static constexpr std::array<std::size_t, WavetableShape::frameCount>
        partials { 1, 2, 3, 5, 8, 14, 24, 40 };
    WavetableShape shape;
    for (std::size_t frame = 0; frame < WavetableShape::frameCount; ++frame)
        for (std::size_t harmonic = 0;
             harmonic < partials[frame]
             && harmonic < WavetableShape::harmonicCount;
             ++harmonic)
            shape.harmonics[frame][harmonic]
                = 1.0f / static_cast<float>(harmonic + 1);
    return shape;
}

WavetableBankRef buildWavetableBank(const WavetableShape& shape)
{
    auto bank = std::make_shared<WavetableBank>();
    for (std::size_t frame = 0; frame < WavetableBank::frameCount; ++frame) {
        auto peak = 0.0f;
        for (std::size_t sample = 0; sample < WavetableBank::frameLength;
             ++sample) {
            const auto phase = 2.0 * std::numbers::pi
                               * static_cast<double>(sample)
                               / static_cast<double>(WavetableBank::frameLength);
            auto value = 0.0;
            for (std::size_t harmonic = 0;
                 harmonic < WavetableShape::harmonicCount; ++harmonic) {
                const auto amplitude = shape.harmonics[frame][harmonic];
                if (amplitude == 0.0f)
                    continue;
                value += static_cast<double>(amplitude)
                         * std::sin(phase * static_cast<double>(harmonic + 1));
            }
            bank->frames[frame][sample] = static_cast<float>(value);
            peak = std::max(peak, std::abs(bank->frames[frame][sample]));
        }
        // Every frame reaches full scale, so morphing between them changes
        // the shape of the spectrum and not its loudness.
        if (peak > 1.0e-6f)
            for (auto& sample : bank->frames[frame])
                sample /= peak;
    }
    return bank;
}

void renderWavetableFrame(const WavetableShape::HarmonicRow& harmonics,
                          float* const samples, const std::size_t count)
{
    if (samples == nullptr || count == 0)
        return;
    auto peak = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index)
                           / static_cast<double>(count);
        auto value = 0.0;
        for (std::size_t harmonic = 0; harmonic < WavetableShape::harmonicCount;
             ++harmonic) {
            const auto amplitude = harmonics[harmonic];
            if (amplitude == 0.0f)
                continue;
            value += static_cast<double>(amplitude)
                     * std::sin(phase * static_cast<double>(harmonic + 1));
        }
        samples[index] = static_cast<float>(value);
        peak = std::max(peak, std::abs(samples[index]));
    }
    // Same normalisation the bank uses, so what is drawn is what is heard.
    if (peak > 1.0e-6f)
        for (std::size_t index = 0; index < count; ++index)
            samples[index] /= peak;
}

WavetableImport wavetableShapeFromSamples(const float* const samples,
                                          const std::size_t count)
{
    WavetableImport result;
    if (samples == nullptr || count < 8)
        return result;

    // Longest first: a file that divides by 2048 also divides by 1024, and
    // reading it as the shorter cycle would halve every pitch in it.
    static constexpr std::array<std::size_t, 4> cycleLengths {
        2'048, 1'024, 512, 256 };
    for (const auto length : cycleLengths)
        if (count % length == 0 && count / length >= 2) {
            result.frameLength = length;
            result.framesFound = count / length;
            break;
        }
    if (result.frameLength == 0) {
        result.frameLength = count;
        result.framesFound = 1;
        result.singleCycle = true;
    }

    static constexpr std::array<std::size_t, WavetableShape::frameCount>
        partials { 1, 2, 3, 5, 8, 14, 24, 40 };
    if (result.singleCycle) {
        const auto row = analyseWavetableFrame(samples, count);
        for (std::size_t frame = 0; frame < WavetableShape::frameCount;
             ++frame)
            for (std::size_t harmonic = 0;
                 harmonic < WavetableShape::harmonicCount
                 && harmonic < partials[frame];
                 ++harmonic)
                result.shape.harmonics[frame][harmonic] = row[harmonic];
        result.ok = true;
        return result;
    }

    for (std::size_t frame = 0; frame < WavetableShape::frameCount; ++frame) {
        // Spread across the file rather than taken from its head: the first
        // eight cycles of a sixty-four frame table are nearly identical, and
        // importing them would throw the sweep away.
        const auto source = WavetableShape::frameCount == 1
            ? std::size_t {}
            : frame * (result.framesFound - 1)
                  / (WavetableShape::frameCount - 1);
        result.shape.harmonics[frame] = analyseWavetableFrame(
            samples + source * result.frameLength, result.frameLength);
    }
    result.ok = true;
    return result;
}

WavetableShape::HarmonicRow analyseWavetableFrame(const float* const samples,
                                                  const std::size_t count)
{
    WavetableShape::HarmonicRow row {};
    if (samples == nullptr || count < 2)
        return row;
    // A real DFT of one period. Only the sine terms are kept: the bank reads
    // sines, and a drawn cycle's phase is not something a listener can hear
    // on a single oscillator anyway.
    for (std::size_t harmonic = 0; harmonic < WavetableShape::harmonicCount;
         ++harmonic) {
        auto sine = 0.0;
        for (std::size_t sample = 0; sample < count; ++sample) {
            const auto phase = 2.0 * std::numbers::pi
                               * static_cast<double>(harmonic + 1)
                               * static_cast<double>(sample)
                               / static_cast<double>(count);
            sine += static_cast<double>(samples[sample]) * std::sin(phase);
        }
        row[harmonic] = static_cast<float>(2.0 * sine
                                           / static_cast<double>(count));
    }
    return row;
}

} // namespace cydaw::engine
