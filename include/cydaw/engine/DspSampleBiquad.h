#pragma once
#include <cassert>
#include <cydsp/processors/Biquad.h>
#include <new>
#include <span>
namespace cydaw::engine
{
// SDK filter for sample-interleaved instrument topologies. Construct on the
// preparation owner; parameter updates/reset/process retain the prepared state.
class DspSampleBiquad
{
  public:
    explicit DspSampleBiquad(const cydsp::BiquadCoefficients& c = {})
    {
        if (processor_.prepare({48000, 64, 1}) != cydsp::Status::ok)
            throw std::bad_alloc();
        setCoefficients(c);
    }
    DspSampleBiquad(DspSampleBiquad&&) noexcept = default;
    DspSampleBiquad& operator=(DspSampleBiquad&&) noexcept = default;
    void setCoefficients(const cydsp::BiquadCoefficients& c) noexcept
    {
        const auto status = processor_.setCoefficients(c);
        assert(status == cydsp::Status::ok);
        (void)status;
    }
    void reset() noexcept { processor_.reset(); }
    double run(double input) noexcept
    {
        double output{};
        const double* from = &input;
        double* to = &output;
        const auto status = processor_.process(cydsp::AudioBlock<const double>(&from, 1, 1), cydsp::AudioBlock<double>(&to, 1, 1));
        assert(status == cydsp::Status::ok);
        (void)status;
        return output;
    }
    void process(std::span<double> samples) noexcept
    {
        auto* pointer=samples.data();
        const auto block=cydsp::AudioBlock<double>(&pointer,1,samples.size());
        const auto status=processor_.process(block,block);
        assert(status==cydsp::Status::ok); (void)status;
    }
    double operator()(double input) noexcept { return run(input); }

  private:
    cydsp::Biquad<double> processor_;
};
} // namespace cydaw::engine
