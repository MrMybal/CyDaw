#include <cydaw/engine/TimeStretch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

namespace {

constexpr int kernelZeroCrossings = 16;
constexpr int kernelResolution = 512; // table points per zero crossing

void transform(std::complex<float>* data, const std::size_t size, const std::vector<std::complex<float>>& twiddles,
               const std::vector<std::size_t>& reversed) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
        if (i < reversed[i])
            std::swap(data[i], data[reversed[i]]);
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto half = length / 2;
        const auto stride = size / length;
        for (std::size_t start = 0; start < size; start += length)
            for (std::size_t k = 0; k < half; ++k) {
                const auto t = twiddles[k * stride] * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
    }
}

std::int64_t floorDivide(const std::int64_t value, const std::int64_t by) noexcept
{
    const auto quotient = value / by;
    return (value % by != 0 && ((value < 0) != (by < 0))) ? quotient - 1 : quotient;
}

float wrap(float phase) noexcept
{
    constexpr auto twoPi = 2.0f * std::numbers::pi_v<float>;
    phase -= twoPi * std::round(phase / twoPi);
    return phase;
}

int windowFor(const StretchAlgorithm algorithm, const double rate) noexcept
{
    const auto seconds = algorithm == StretchAlgorithm::beats     ? 0.043
                         : algorithm == StretchAlgorithm::texture ? 0.17
                                                                  : 0.085;
    auto size = 512;
    while (size < 32'768 && static_cast<double>(size) < rate * seconds)
        size <<= 1;
    return size;
}

} // namespace

void TimeStretcher::prepare(const StretchSettings& settings)
{
    settings_ = settings;
    settings_.speed = std::clamp(settings_.speed, 1.0 / 64.0, 64.0);
    settings_.pitch = std::clamp(settings_.pitch, 0.125, 8.0);
    settings_.channels = std::clamp(settings_.channels, 1, 8);
    settings_.sourceRate = std::max(1'000.0, settings_.sourceRate);
    settings_.outputRate = std::max(1'000.0, settings_.outputRate);
    const auto rates = settings_.sourceRate / settings_.outputRate;
    if (settings_.algorithm == StretchAlgorithm::repitch) {
        direct_ = true;
        step_ = settings_.speed * rates;
    } else if (std::abs(settings_.pitch - 1.0) < 1.0e-9 && std::abs(settings_.speed - 1.0) < 1.0e-9) {
        direct_ = true;
        step_ = rates;
    } else {
        direct_ = false;
        step_ = settings_.pitch * rates;
    }
    copy_ = direct_ && std::abs(step_ - 1.0) < 1.0e-12 && settings_.sourceStart == std::floor(settings_.sourceStart);
    slices_ = !direct_ && settings_.algorithm == StretchAlgorithm::beats;
    // Read faster than the rate: the band above the new Nyquist goes first.
    cutoff_ = step_ > 1.0 ? 0.97 / step_ : 0.97;
    reach_ = std::min(1'024, static_cast<int>(std::ceil(kernelZeroCrossings / cutoff_)) + 1);
    if (kernel_.empty()) {
        kernel_.resize(static_cast<std::size_t>(kernelZeroCrossings * kernelResolution + 2));
        for (std::size_t i = 0; i < kernel_.size(); ++i) {
            const auto d = static_cast<double>(i) / kernelResolution;
            const auto sinc = d < 1.0e-9 ? 1.0 : std::sin(std::numbers::pi * d) / (std::numbers::pi * d);
            const auto t = std::min(1.0, d / kernelZeroCrossings);
            const auto blackman = 0.42 + 0.5 * std::cos(std::numbers::pi * t) + 0.08 * std::cos(2.0 * std::numbers::pi * t);
            kernel_[i] = static_cast<float>(sinc * blackman);
        }
    }
    // The phase vocoder.
    size_ = windowFor(settings_.algorithm, settings_.sourceRate);
    hop_ = size_ / 4;
    window_.resize(static_cast<std::size_t>(size_));
    for (int n = 0; n < size_; ++n)
        window_[static_cast<std::size_t>(n)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / size_));
    // What the windows add up to, squared, every hop apart.
    auto sum = 0.0;
    for (int n = 0; n < size_; ++n)
        sum += static_cast<double>(window_[static_cast<std::size_t>(n)]) * window_[static_cast<std::size_t>(n)];
    norm_ = sum / hop_;
    // Random phases add up as powers, not as amplitudes.
    textureGain_ = std::sqrt(norm_ / (sum / size_));
    twiddles_.resize(static_cast<std::size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
        twiddles_[static_cast<std::size_t>(k)] = std::polar(1.0f, static_cast<float>(-2.0 * std::numbers::pi * k / size_));
    reversed_.resize(static_cast<std::size_t>(size_));
    auto bits = 0;
    while ((1 << bits) < size_)
        ++bits;
    for (int i = 0; i < size_; ++i) {
        std::size_t r = 0;
        for (auto bit = 0; bit < bits; ++bit)
            if ((i >> bit) & 1)
                r |= std::size_t { 1 } << (bits - 1 - bit);
        reversed_[static_cast<std::size_t>(i)] = r;
    }
    const auto channels = static_cast<std::size_t>(settings_.channels);
    const auto bins = static_cast<std::size_t>(size_ / 2 + 1);
    read_.assign(channels, std::vector<float>(static_cast<std::size_t>(size_)));
    spectra_.assign(channels, std::vector<std::complex<float>>(static_cast<std::size_t>(size_)));
    reference_.assign(static_cast<std::size_t>(size_), {});
    magnitude_.assign(bins, 0.0f);
    previousMagnitude_.assign(bins, 0.0f);
    analysisPhase_.assign(bins, 0.0f);
    previousAnalysisPhase_.assign(bins, 0.0f);
    synthesisPhase_.assign(bins, 0.0f);
    peaks_.reserve(bins);
    owner_.assign(bins, 0);
    capacity_ = static_cast<std::size_t>(size_) * 4 + 65'536 + static_cast<std::size_t>(reach_) * 4;
    scan_.assign(static_cast<std::size_t>(settings_.sourceRate * 0.6) + 1'024, 0.0f);
    scanChannels_.assign(channels, std::vector<float>(scan_.size(), 0.0f));
    fade_.assign(channels, std::vector<float>(static_cast<std::size_t>(settings_.sourceRate * 0.012) + 16, 0.0f));
    stretched_.assign(channels, std::vector<float>(capacity_, 0.0f));
    reset();
}

void TimeStretcher::reset() noexcept
{
    expected_ = -1;
    havePrevious_ = false;
    base_ = 0;
    complete_ = 0;
    nextFrame_ = 0;
    for (auto& channel : stretched_)
        std::fill(channel.begin(), channel.end(), 0.0f);
}

void TimeStretcher::restartAt(const std::int64_t intermediate)
{
    for (auto& channel : stretched_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    havePrevious_ = false;
    previousHighEnergy_ = 0.0f;
    if (direct_) {
        base_ = intermediate;
        complete_ = intermediate;
        return;
    }
    if (slices_) {
        // A slice starts where the jump lands (faded in), the next at the
        // next attack.
        base_ = intermediate;
        complete_ = intermediate;
        sliceAt_ = intermediate;
        sliceStart_ = settings_.sourceStart + static_cast<double>(intermediate) * settings_.speed / settings_.pitch;
        sliceFadesIn_ = true;
        crossfading_ = false;
        nextSliceAt_ = -1;
        return;
    }
    // The first window that reaches `intermediate`: every frame from there
    // on gets all the windows it lies under.
    nextFrame_ = floorDivide(intermediate - size_ / 2, hop_) + 1;
    base_ = nextFrame_ * hop_ - size_ / 2;
    complete_ = base_;
}

void TimeStretcher::keepFrom(const std::int64_t intermediate)
{
    // Drop what lies before `intermediate` (and before what is still being
    // added to) by sliding the rest to the front.
    const auto from = std::min(intermediate, complete_);
    if (from <= base_)
        return;
    const auto drop = static_cast<std::size_t>(std::min<std::int64_t>(from - base_, static_cast<std::int64_t>(capacity_)));
    for (auto& channel : stretched_) {
        std::move(channel.begin() + static_cast<std::ptrdiff_t>(drop), channel.end(), channel.begin());
        std::fill(channel.end() - static_cast<std::ptrdiff_t>(drop), channel.end(), 0.0f);
    }
    base_ += static_cast<std::int64_t>(drop);
}

void TimeStretcher::ensureStretched(StretchSource& source, const std::int64_t from, const std::int64_t to)
{
    const auto room = [this](const std::int64_t end) {
        if (end - base_ <= static_cast<std::int64_t>(capacity_))
            return;
        capacity_ = static_cast<std::size_t>(end - base_) + static_cast<std::size_t>(size_) * 2;
        for (auto& channel : stretched_)
            channel.resize(capacity_, 0.0f);
    };
    const auto margin = direct_ ? std::int64_t {} : static_cast<std::int64_t>(size_);
    if (to + margin - base_ > static_cast<std::int64_t>(capacity_))
        keepFrom(from);
    if (direct_) {
        if (complete_ < to) {
            room(to);
            std::array<float*, 8> pointers {};
            const auto count = static_cast<int>(to - complete_);
            for (int channel = 0; channel < settings_.channels; ++channel)
                pointers[static_cast<std::size_t>(channel)] = stretched_[static_cast<std::size_t>(channel)].data() + (complete_ - base_);
            source.read(static_cast<std::int64_t>(std::floor(settings_.sourceStart)) + complete_, count, pointers.data(),
                        settings_.channels);
            complete_ = to;
        }
        return;
    }
    if (slices_) {
        room(to);
        layOutSlices(source, to);
        return;
    }
    while (complete_ < to) {
        room(nextFrame_ * hop_ + size_ / 2);
        synthesiseFrame(source);
    }
}

double TimeStretcher::nextAttack(StretchSource& source, const double from)
{
    // A slice is 40 ms long at least, half a second at most.
    const auto rate = settings_.sourceRate;
    const auto earliest = from + 0.04 * rate;
    const auto latest = from + 0.5 * rate;
    constexpr int frame = 128;
    const auto count = static_cast<int>(std::min<double>(static_cast<double>(scan_.size()) - frame, latest - earliest + 8 * frame));
    const auto start = static_cast<std::int64_t>(std::floor(earliest)) - 4 * frame;
    // Every channel read, then added up.
    std::array<float*, 8> pointers {};
    for (int channel = 0; channel < settings_.channels; ++channel)
        pointers[static_cast<std::size_t>(channel)] = scanChannels_[static_cast<std::size_t>(channel)].data();
    source.read(start, count, pointers.data(), settings_.channels);
    auto& mixed = scan_;
    for (int i = 0; i < count; ++i) {
        auto sum = 0.0f;
        for (int channel = 0; channel < settings_.channels; ++channel)
            sum += scanChannels_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)];
        mixed[static_cast<std::size_t>(i)] = sum;
    }
    // The energy of the signal's change, frame by frame; an attack is a
    // frame well above the four before it.
    const auto frames = count / frame;
    std::vector<double> energy(static_cast<std::size_t>(std::max(0, frames)), 0.0);
    for (int f = 0; f < frames; ++f) {
        auto sum = 0.0;
        for (int i = 1; i < frame; ++i) {
            const auto at = static_cast<std::size_t>(f * frame + i);
            const auto change = static_cast<double>(mixed[at]) - mixed[at - 1];
            sum += change * change;
        }
        energy[static_cast<std::size_t>(f)] = sum;
    }
    for (int f = 4; f < frames; ++f) {
        const auto at = start + static_cast<std::int64_t>(f) * frame;
        if (static_cast<double>(at) < earliest || static_cast<double>(at) > latest)
            continue;
        const auto before = (energy[static_cast<std::size_t>(f - 1)] + energy[static_cast<std::size_t>(f - 2)]
                             + energy[static_cast<std::size_t>(f - 3)] + energy[static_cast<std::size_t>(f - 4)]) * 0.25;
        if (energy[static_cast<std::size_t>(f)] > 4.0 * before + 1.0e-5) {
            // Where it starts in that frame: the first sample past a tenth of
            // its peak, a millisecond before.
            auto peak = 0.0f;
            for (int i = 0; i < frame; ++i)
                peak = std::max(peak, std::abs(mixed[static_cast<std::size_t>(f * frame + i)]));
            auto first = 0;
            while (first < frame - 1 && std::abs(mixed[static_cast<std::size_t>(f * frame + first)]) < peak * 0.1f)
                ++first;
            nextIsAttack_ = true;
            return std::max(earliest, static_cast<double>(at + first) - rate * 0.001);
        }
    }
    nextIsAttack_ = false;
    return from + 0.1 * rate;
}

double TimeStretcher::alignedCut(StretchSource& source, const double continuing, const double nominal)
{
    const auto rate = settings_.sourceRate;
    const auto window = static_cast<int>(rate * 0.01);
    const auto reach = static_cast<int>(rate * 0.012);
    if (static_cast<std::size_t>(window + 2 * reach) > scan_.size())
        return nominal;
    // The sound going on, and the stretch around the nominal cut, each
    // summed over the channels.
    const auto mixInto = [&](const std::int64_t start, const int count, float* into) {
        std::array<float*, 8> pointers {};
        for (int channel = 0; channel < settings_.channels; ++channel)
            pointers[static_cast<std::size_t>(channel)] = scanChannels_[static_cast<std::size_t>(channel)].data();
        source.read(start, count, pointers.data(), settings_.channels);
        for (int i = 0; i < count; ++i) {
            auto sum = 0.0f;
            for (int channel = 0; channel < settings_.channels; ++channel)
                sum += scanChannels_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)];
            into[i] = sum;
        }
    };
    auto* going = scan_.data();
    auto* around = scan_.data() + window;
    mixInto(static_cast<std::int64_t>(std::floor(continuing)), window, going);
    const auto first = static_cast<std::int64_t>(std::floor(nominal)) - reach;
    mixInto(first, window + 2 * reach, around);
    auto energy = 0.0;
    for (int i = 0; i < window; ++i)
        energy += static_cast<double>(going[i]) * going[i];
    if (energy < 1.0e-9)
        return nominal;
    auto best = reach;
    auto bestScore = -1.0e30;
    for (int shift = 0; shift <= 2 * reach; ++shift) {
        auto product = 0.0;
        auto power = 0.0;
        for (int i = 0; i < window; ++i) {
            product += static_cast<double>(going[i]) * around[shift + i];
            power += static_cast<double>(around[shift + i]) * around[shift + i];
        }
        const auto score = product / std::sqrt(std::max(1.0e-12, power));
        if (score > bestScore) {
            bestScore = score;
            best = shift;
        }
    }
    return static_cast<double>(first + best);
}

void TimeStretcher::layOutSlices(StretchSource& source, const std::int64_t to)
{
    const auto ratio = settings_.pitch / settings_.speed; // stretched frames per file frame of the layout
    const auto fadeIn = 32;
    const auto fadeOut = static_cast<std::int64_t>(std::max(16.0, settings_.sourceRate * 0.004));
    std::array<float*, 8> pointers {};
    while (complete_ < to) {
        if (nextSliceAt_ < 0) {
            nextSlice_ = nextAttack(source, sliceStart_);
            nextSliceAt_ = static_cast<std::int64_t>(std::llround((nextSlice_ - settings_.sourceStart) * ratio));
            if (nextSliceAt_ <= sliceAt_)
                nextSliceAt_ = sliceAt_ + 1;
            // A mere cut in a held sound: the next slice starts where its
            // waveform carries on from this one's (no jump of phase, so the
            // pitch holds), the place in time kept.
            if (!nextIsAttack_)
                nextSlice_ = alignedCut(source, sliceStart_ + static_cast<double>(nextSliceAt_ - sliceAt_), nextSlice_);
        }
        if (complete_ >= nextSliceAt_) {
            previousSlice_ = sliceStart_;
            previousSliceAt_ = sliceAt_;
            crossfading_ = !nextIsAttack_;
            sliceStart_ = nextSlice_;
            sliceAt_ = nextSliceAt_;
            sliceFadesIn_ = false;
            nextSliceAt_ = -1;
            continue;
        }
        // This slice plays as it is until the next one starts. Before an
        // attack, slowed down, it stops where its own sound ends (faded out);
        // before a mere cut it plays on, and the next crossfades from it.
        const auto length = static_cast<std::int64_t>(std::floor(nextSlice_ - sliceStart_));
        const auto end = nextIsAttack_ ? std::min(nextSliceAt_, sliceAt_ + length) : nextSliceAt_;
        const auto until = std::min(to, nextSliceAt_);
        const auto first = complete_;
        const auto playedUntil = std::min(until, end);
        if (playedUntil > first) {
            const auto count = static_cast<int>(playedUntil - first);
            for (int channel = 0; channel < settings_.channels; ++channel)
                pointers[static_cast<std::size_t>(channel)] = stretched_[static_cast<std::size_t>(channel)].data() + (first - base_);
            source.read(static_cast<std::int64_t>(std::floor(sliceStart_)) + (first - sliceAt_), count, pointers.data(), settings_.channels);
            // Faded in after a jump, faded out where it is cut.
            for (auto at = first; at < playedUntil; ++at) {
                auto gain = 1.0f;
                if (sliceFadesIn_ && at - sliceAt_ < fadeIn)
                    gain *= static_cast<float>(at - sliceAt_) / fadeIn;
                if (nextIsAttack_ && end - at <= fadeOut)
                    gain *= static_cast<float>(end - at) / static_cast<float>(fadeOut + 1);
                if (gain < 1.0f)
                    for (int channel = 0; channel < settings_.channels; ++channel)
                        stretched_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(at - base_)] *= gain;
            }
            // A held sound: the slice before fades out under this one's start
            // (equal power: two phases that do not match keep their level).
            const auto across = std::min<std::int64_t>(static_cast<std::int64_t>(fade_.front().size()) - 1,
                                                        std::min(nextSliceAt_ - sliceAt_, static_cast<std::int64_t>(settings_.sourceRate * 0.01)));
            if (crossfading_ && first < sliceAt_ + across) {
                const auto overlap = static_cast<int>(std::min(playedUntil, sliceAt_ + across) - first);
                for (int channel = 0; channel < settings_.channels; ++channel)
                    pointers[static_cast<std::size_t>(channel)] = fade_[static_cast<std::size_t>(channel)].data();
                source.read(static_cast<std::int64_t>(std::floor(previousSlice_)) + (first - previousSliceAt_), overlap, pointers.data(),
                            settings_.channels);
                for (int i = 0; i < overlap; ++i) {
                    const auto theta = static_cast<float>(first + i - sliceAt_) / static_cast<float>(across) * std::numbers::pi_v<float> * 0.5f;
                    for (int channel = 0; channel < settings_.channels; ++channel) {
                        auto& value = stretched_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(first + i - base_)];
                        value = value * std::sin(theta) + fade_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] * std::cos(theta);
                    }
                }
            }
        }
        // Silence after a slice slowed down, until the next one.
        for (auto at = std::max(first, playedUntil); at < until; ++at)
            for (int channel = 0; channel < settings_.channels; ++channel)
                stretched_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(at - base_)] = 0.0f;
        complete_ = until;
    }
}

void TimeStretcher::synthesiseFrame(StretchSource& source)
{
    const auto bins = size_ / 2;
    const auto channels = settings_.channels;
    const auto centre = nextFrame_ * hop_;
    const auto sourceCentre = settings_.sourceStart + static_cast<double>(centre) * settings_.speed / settings_.pitch;
    const auto start = static_cast<std::int64_t>(std::llround(sourceCentre)) - size_ / 2;
    std::array<float*, 8> pointers {};
    for (int channel = 0; channel < channels; ++channel)
        pointers[static_cast<std::size_t>(channel)] = read_[static_cast<std::size_t>(channel)].data();
    source.read(start, size_, pointers.data(), channels);
    for (int channel = 0; channel < channels; ++channel) {
        auto& spectrum = spectra_[static_cast<std::size_t>(channel)];
        const auto& samples = read_[static_cast<std::size_t>(channel)];
        for (int n = 0; n < size_; ++n)
            spectrum[static_cast<std::size_t>(n)] = samples[static_cast<std::size_t>(n)] * window_[static_cast<std::size_t>(n)];
        transform(spectrum.data(), static_cast<std::size_t>(size_), twiddles_, reversed_);
    }
    // The phases follow the channels' sum, so they keep their differences.
    for (int j = 0; j <= bins; ++j) {
        std::complex<float> sum {};
        for (int channel = 0; channel < channels; ++channel)
            sum += spectra_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(j)];
        reference_[static_cast<std::size_t>(j)] = sum;
        magnitude_[static_cast<std::size_t>(j)] = std::abs(sum);
        analysisPhase_[static_cast<std::size_t>(j)] = std::arg(sum);
    }
    // An attack: the highs jump.
    auto high = 0.0f;
    const auto firstHigh = static_cast<int>(1'500.0 * size_ / settings_.sourceRate);
    const auto lastHigh = std::min(bins, static_cast<int>(16'000.0 * size_ / settings_.sourceRate));
    for (int j = firstHigh; j < lastHigh; ++j)
        high += magnitude_[static_cast<std::size_t>(j)] * magnitude_[static_cast<std::size_t>(j)];
    const auto audible = high > 1.0e-7f * static_cast<float>(size_);
    const auto attack = havePrevious_ && audible
                        && high > (settings_.algorithm == StretchAlgorithm::beats ? 4.0f : 40.0f) * previousHighEnergy_;
    const auto hop = static_cast<float>(hop_);
    if (settings_.algorithm == StretchAlgorithm::texture) {
        // Phases at random: no partial lines up with the last window.
        for (int j = 0; j <= bins; ++j) {
            random_ ^= random_ << 13;
            random_ ^= random_ >> 17;
            random_ ^= random_ << 5;
            synthesisPhase_[static_cast<std::size_t>(j)]
                = static_cast<float>(random_ & 0xffffffu) / static_cast<float>(0x1000000) * 2.0f * std::numbers::pi_v<float>;
        }
    } else if (!havePrevious_ || attack) {
        synthesisPhase_ = analysisPhase_;
    } else {
        const auto analysisHop = static_cast<float>(start - previousStart_);
        // The peaks, and the bins each one leads.
        peaks_.clear();
        auto loudest = 0.0f;
        for (int j = 0; j <= bins; ++j)
            loudest = std::max(loudest, magnitude_[static_cast<std::size_t>(j)]);
        const auto floor = loudest * 1.0e-5f;
        for (int j = 2; j < bins - 1; ++j) {
            const auto m = magnitude_[static_cast<std::size_t>(j)];
            if (m > floor && m > magnitude_[static_cast<std::size_t>(j - 1)] && m >= magnitude_[static_cast<std::size_t>(j + 1)]
                && m > magnitude_[static_cast<std::size_t>(j - 2)] && m >= magnitude_[static_cast<std::size_t>(j + 2)])
                peaks_.push_back(j);
        }
        if (peaks_.empty())
            peaks_.push_back(0);
        std::size_t current = 0;
        for (int j = 0; j <= bins; ++j) {
            while (current + 1 < peaks_.size() && j > (peaks_[current] + peaks_[current + 1]) / 2)
                ++current;
            owner_[static_cast<std::size_t>(j)] = peaks_[current];
        }
        // Each peak runs on at the frequency its phase says; the bins
        // around it keep their offsets to it.
        for (const auto peak : peaks_) {
            const auto bin = static_cast<std::size_t>(peak);
            const auto omega = 2.0f * std::numbers::pi_v<float> * static_cast<float>(peak) / static_cast<float>(size_);
            auto frequency = omega;
            if (analysisHop > 0.5f) {
                const auto deviation = wrap(analysisPhase_[bin] - previousAnalysisPhase_[bin] - omega * analysisHop);
                frequency = omega + deviation / analysisHop;
            }
            // Kept in the owner's slot of previousMagnitude_ (free here) until
            // every peak has been advanced.
            previousMagnitude_[bin] = wrap(synthesisPhase_[bin] + frequency * hop);
        }
        for (int j = 0; j <= bins; ++j) {
            const auto owner = static_cast<std::size_t>(owner_[static_cast<std::size_t>(j)]);
            synthesisPhase_[static_cast<std::size_t>(j)]
                = previousMagnitude_[owner] + analysisPhase_[static_cast<std::size_t>(j)] - analysisPhase_[owner];
        }
    }
    // Back to sound, each channel with its own phase offset to the sum.
    const auto scale = (settings_.algorithm == StretchAlgorithm::texture ? static_cast<float>(textureGain_) : 1.0f)
                       / (static_cast<float>(size_) * static_cast<float>(norm_));
    for (int channel = 0; channel < channels; ++channel) {
        auto& spectrum = spectra_[static_cast<std::size_t>(channel)];
        for (int j = 0; j <= bins; ++j) {
            const auto value = spectrum[static_cast<std::size_t>(j)];
            const auto offset = channels > 1 ? std::arg(value) - analysisPhase_[static_cast<std::size_t>(j)] : 0.0f;
            // The conjugate's spectrum: its forward transform, conjugated, is
            // the inverse.
            spectrum[static_cast<std::size_t>(j)] = std::conj(std::polar(std::abs(value), synthesisPhase_[static_cast<std::size_t>(j)] + offset));
        }
        for (int j = 1; j < bins; ++j)
            spectrum[static_cast<std::size_t>(size_ - j)] = std::conj(spectrum[static_cast<std::size_t>(j)]);
        transform(spectrum.data(), static_cast<std::size_t>(size_), twiddles_, reversed_);
        auto* into = stretched_[static_cast<std::size_t>(channel)].data() + (centre - size_ / 2 - base_);
        for (int n = 0; n < size_; ++n)
            into[n] += spectrum[static_cast<std::size_t>(n)].real() * window_[static_cast<std::size_t>(n)] * scale;
    }
    previousAnalysisPhase_ = analysisPhase_;
    previousStart_ = start;
    previousHighEnergy_ = audible ? high : 0.0f;
    havePrevious_ = true;
    ++nextFrame_;
    complete_ = nextFrame_ * hop_ - size_ / 2;
}

float TimeStretcher::sincAt(const int channel, const double position) const noexcept
{
    const auto& samples = stretched_[static_cast<std::size_t>(channel)];
    const auto first = static_cast<std::int64_t>(std::ceil(position - reach_));
    const auto last = static_cast<std::int64_t>(std::floor(position + reach_));
    auto sum = 0.0f;
    const auto limit = static_cast<double>(kernelZeroCrossings);
    for (auto n = std::max<std::int64_t>(0, first); n <= std::min<std::int64_t>(static_cast<std::int64_t>(samples.size()) - 1, last); ++n) {
        const auto d = std::abs(position - static_cast<double>(n)) * cutoff_;
        if (d >= limit)
            continue;
        const auto at = d * kernelResolution;
        const auto index = static_cast<std::size_t>(at);
        const auto fraction = static_cast<float>(at - static_cast<double>(index));
        const auto k = kernel_[index] + (kernel_[index + 1] - kernel_[index]) * fraction;
        sum += samples[static_cast<std::size_t>(n)] * k;
    }
    return sum * static_cast<float>(cutoff_);
}

void TimeStretcher::render(StretchSource& source, const std::int64_t position, const int count, float* const* output)
{
    if (count <= 0)
        return;
    if (copy_) {
        source.read(static_cast<std::int64_t>(settings_.sourceStart) + position, count, output, settings_.channels);
        expected_ = position + count;
        return;
    }
    // Where in the stretched sound the block reads (direct: the file, from
    // the whole frame under sourceStart).
    const auto fraction = direct_ ? settings_.sourceStart - std::floor(settings_.sourceStart) : 0.0;
    const auto x0 = static_cast<double>(position) * step_ + fraction;
    const auto x1 = static_cast<double>(position + count - 1) * step_ + fraction;
    const auto from = static_cast<std::int64_t>(std::floor(x0)) - reach_ - 1;
    const auto to = static_cast<std::int64_t>(std::ceil(x1)) + reach_ + 2;
    if (position != expected_ || from < base_)
        restartAt(from);
    ensureStretched(source, from, to);
    const auto exact = std::abs(step_ - 1.0) < 1.0e-12 && fraction == 0.0;
    for (int channel = 0; channel < settings_.channels; ++channel) {
        auto* out = output[channel];
        if (exact) {
            const auto* in = stretched_[static_cast<std::size_t>(channel)].data() + (static_cast<std::int64_t>(x0) - base_);
            std::copy_n(in, count, out);
            continue;
        }
        for (int i = 0; i < count; ++i)
            out[i] = sincAt(channel, static_cast<double>(position + i) * step_ + fraction - static_cast<double>(base_));
    }
    expected_ = position + count;
}

} // namespace cydaw::engine
