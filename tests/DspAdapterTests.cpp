#include "SynthParts.h"
#include <cmath>
#include <complex>
#include <cydaw/engine/DspHistory.h>
#include <cydaw/engine/DspSampleBiquad.h>
#include <cydaw/engine/Fft.h>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace cydaw::engine;
void check(bool ok, const char* reason)
{
    if (!ok)
        throw std::runtime_error(reason);
}
double blep(double t, double dt)
{
    if (dt <= 0)
        return 0;
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt)
    {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}
int main()
{
    try
    {
        synth::detail::parts::PhaseWaveforms wave;
        for (double dt : {0., 1e-6, .001, .1, .49})
            for (double w : {.01, .17, .5, .93, .99})
                for (int i = 0; i < 1001; ++i)
                {
                    const double t = double(i) / 1001;
                    auto end = t - w;
                    if (end < 0)
                        end += 1;
                    const double saw = 2 * t - 1 - blep(t, dt);
                    const double pulse = (t < w ? 1. : -1.) + blep(t, dt) - blep(end, dt) - (2 * w - 1);
                    check(std::abs(wave(0, t, dt, w) - saw) < 1e-12, "SDK saw contract");
                    check(std::abs(wave(1, t, dt, w) - pulse) < 1e-12, "SDK PWM balance/edge contract");
                    check(std::abs(wave(2, t, dt, w) - (4 * std::abs(t - .5) - 1)) < 1e-12, "triangle polarity");
                    check(std::abs(wave(3, t, dt, w) - std::sin(2 * std::numbers::pi * t)) < 1e-12, "sine phase");
                }
        DspHistory<double> history;
        history.prepare(2, 8);
        check(history.read(0, 0) == 0, "empty history");
        for (int i = 0; i < 21; ++i)
        {
            const double f[]{double(i), -double(i)};
            history.push(f);
        }
        check(history.read(0, 0) == 20 && history.read(1, 7) == -13, "wrapped channel history");
        check(history.read(0, 2.25) == 17.75, "history fractional age");
        auto copy = history;
        history.reset();
        check(history.read(0, 0) == 0 && copy.read(0, 0) == 20, "history independent copy/reset");
        auto copy2 = copy;
        copy2 = history;
        check(copy2.read(0, 0) == 0, "history copy assignment");
        for (std::size_t requested : {0u, 1u, 3u, 17u})
        {
            Fft fft;
            fft.prepare(requested);
            const auto n = fft.size();
            std::vector<std::complex<float>> data(n), original(n);
            for (std::size_t i = 0; i < n; ++i)
                data[i] = original[i] = {float(std::sin(i * .7)), float(std::cos(i * .31))};
            fft.forward(data.data());
            for (std::size_t k = 0; k < n; ++k)
            {
                std::complex<double> expected{};
                for (std::size_t i = 0; i < n; ++i)
                    expected += std::complex<double>(original[i]) * std::polar(1., -2 * std::numbers::pi * double(i * k) / n);
                check(std::abs(std::complex<double>(data[k]) - expected) < 2e-4, "FFT DFT oracle/scaling");
            }
            auto copied = fft;
            copied.inverse(data.data());
            for (std::size_t i = 0; i < n; ++i)
                check(std::abs(data[i] - original[i]) < 2e-6, "FFT inverse round trip");
        }
        DspSampleBiquad filter({.2, .1, .05, -.7, .2});
        double z1 = 0, z2 = 0;
        for (int i = 0; i < 4096; ++i)
        {
            const auto x = std::sin(i * .13);
            auto y = .2 * x + z1;
            z1 = .1 * x + .7 * y + z2;
            z2 = .05 * x - .2 * y;
            check(std::abs(filter(x) - y) < 1e-12, "SDK biquad recurrence/state");
        }
        filter.reset();
        check(filter(0) == 0, "SDK biquad reset");
        std::cout << "waveforms, history ownership, DFT oracle and filter "
                     "recurrence passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
