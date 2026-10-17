#include "cydaw/engine/FoundrySound.h"

#include "cydaw/engine/Fft.h"
#include "cydaw/engine/Loudness.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace cydaw::engine::foundry {

namespace {

// Plain channels read as a stretcher's source.
class ChannelsSource final : public StretchSource {
public:
    explicit ChannelsSource(const std::vector<std::vector<float>>& channels) : channels_(channels) {}

    void read(const std::int64_t start, const int count, float* const* out, const int channelCount) override
    {
        for (int c = 0; c < channelCount; ++c) {
            const auto& data = channels_[static_cast<std::size_t>(std::min<int>(c, static_cast<int>(channels_.size()) - 1))];
            const auto frames = static_cast<std::int64_t>(data.size());
            for (int i = 0; i < count; ++i) {
                const auto at = start + i;
                out[c][i] = at >= 0 && at < frames ? data[static_cast<std::size_t>(at)] : 0.0f;
            }
        }
    }

private:
    const std::vector<std::vector<float>>& channels_;
};

std::vector<std::vector<float>> stretchChannels(const std::vector<std::vector<float>>& channels, const double rate,
                                                const double outputRate, const double speed, const double pitch,
                                                const StretchAlgorithm algorithm, const std::int64_t outputFrames)
{
    const auto count = static_cast<int>(channels.size());
    std::vector<std::vector<float>> output(channels.size(), std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(0, outputFrames))));
    if (count == 0 || outputFrames <= 0)
        return output;
    StretchSettings settings;
    settings.algorithm = algorithm;
    settings.sourceRate = rate;
    settings.outputRate = outputRate;
    settings.speed = speed;
    settings.pitch = pitch;
    settings.channels = count;
    TimeStretcher stretcher;
    stretcher.prepare(settings);
    ChannelsSource source(channels);
    constexpr int block = 4'096;
    std::vector<float*> pointers(static_cast<std::size_t>(count));
    for (std::int64_t done = 0; done < outputFrames; done += block) {
        const auto n = static_cast<int>(std::min<std::int64_t>(block, outputFrames - done));
        for (int c = 0; c < count; ++c)
            pointers[static_cast<std::size_t>(c)] = output[static_cast<std::size_t>(c)].data() + done;
        stretcher.render(source, done, n, pointers.data());
    }
    return output;
}

// Another sound's channels made `count`: a mono one on every channel,
// more channels than wanted mixed down, fewer repeated.
Sound conformed(const Sound& other, const int count)
{
    if (other.channels() == count)
        return other;
    std::vector<std::vector<float>> matrix(static_cast<std::size_t>(count), std::vector<float>(static_cast<std::size_t>(other.channels()), 0.0f));
    for (int out = 0; out < count; ++out) {
        if (count == 1) {
            for (int in = 0; in < other.channels(); ++in)
                matrix[0][static_cast<std::size_t>(in)] = 1.0f / static_cast<float>(other.channels());
        } else {
            matrix[static_cast<std::size_t>(out)][static_cast<std::size_t>(out % other.channels())] = 1.0f;
        }
    }
    return other.remixed(count, matrix);
}

} // namespace

std::shared_ptr<const Block> Block::make(std::vector<std::vector<float>> channels)
{
    auto block = std::make_shared<Block>();
    block->channels = std::move(channels);
    block->summary.resize(block->channels.size());
    for (std::size_t c = 0; c < block->channels.size(); ++c) {
        const auto& data = block->channels[c];
        auto& summary = block->summary[c];
        summary.resize((data.size() + summaryFrames - 1) / summaryFrames);
        for (std::size_t cell = 0; cell < summary.size(); ++cell) {
            const auto from = cell * summaryFrames;
            const auto to = std::min(data.size(), from + summaryFrames);
            auto low = data[from];
            auto high = data[from];
            for (auto i = from + 1; i < to; ++i) {
                low = std::min(low, data[i]);
                high = std::max(high, data[i]);
            }
            summary[cell] = { low, high };
        }
    }
    return block;
}

Sound::Sound(std::vector<std::vector<float>> channels, const double rate)
    : channels_(std::clamp(static_cast<int>(channels.size()), 1, maximumChannels)), rate_(rate > 0.0 ? rate : 48'000.0)
{
    if (channels.empty())
        channels.emplace_back();
    channels.resize(static_cast<std::size_t>(channels_));
    const auto length = channels.front().size();
    for (auto& channel : channels)
        channel.resize(length, 0.0f);
    if (length > 0)
        pieces_.push_back({ Block::make(std::move(channels)), 0, static_cast<std::int64_t>(length) });
    rebuild();
}

Sound::Sound(const int channels, const std::int64_t frames, const double rate)
    : Sound(std::vector<std::vector<float>>(static_cast<std::size_t>(std::clamp(channels, 1, maximumChannels)),
                                            std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(0, frames)), 0.0f)),
            rate)
{
}

void Sound::rebuild()
{
    pieces_.erase(std::remove_if(pieces_.begin(), pieces_.end(), [](const Piece& piece) { return piece.length <= 0 || piece.block == nullptr; }),
                  pieces_.end());
    starts_.resize(pieces_.size());
    frames_ = 0;
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        starts_[i] = frames_;
        frames_ += pieces_[i].length;
    }
}

std::size_t Sound::pieceAt(const std::int64_t frame) const noexcept
{
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), frame);
    return after == starts_.begin() ? 0 : static_cast<std::size_t>(after - starts_.begin() - 1);
}

void Sound::read(const int channel, const std::int64_t start, const std::int64_t count, float* const out) const noexcept
{
    std::int64_t done = 0;
    while (done < count) {
        const auto frame = start + done;
        if (frame < 0) {
            const auto n = std::min(count - done, -frame);
            std::fill_n(out + done, n, 0.0f);
            done += n;
            continue;
        }
        if (frame >= frames_ || pieces_.empty()) {
            std::fill_n(out + done, count - done, 0.0f);
            return;
        }
        const auto index = pieceAt(frame);
        const auto& piece = pieces_[index];
        const auto within = frame - starts_[index];
        const auto n = std::min(count - done, piece.length - within);
        const auto& channels = piece.block->channels;
        const auto& data = channels[static_cast<std::size_t>(std::min<int>(channel, static_cast<int>(channels.size()) - 1))];
        std::copy_n(data.data() + piece.offset + within, n, out + done);
        done += n;
    }
}

std::pair<float, float> Sound::range(const int channel, std::int64_t start, std::int64_t end) const noexcept
{
    start = std::max<std::int64_t>(0, start);
    end = std::min(end, frames_);
    auto low = 0.0f;
    auto high = 0.0f;
    auto any = false;
    const auto take = [&](const float a, const float b) {
        if (!any) {
            low = a;
            high = b;
            any = true;
        } else {
            low = std::min(low, a);
            high = std::max(high, b);
        }
    };
    auto frame = start;
    while (frame < end) {
        const auto index = pieceAt(frame);
        const auto& piece = pieces_[index];
        const auto within = frame - starts_[index];
        const auto n = std::min(end - frame, piece.length - within);
        const auto c = static_cast<std::size_t>(std::min<int>(channel, static_cast<int>(piece.block->channels.size()) - 1));
        const auto& data = piece.block->channels[c];
        const auto& summary = piece.block->summary[c];
        // In the block: [from, to); whole cells through the summary.
        const auto from = piece.offset + within;
        const auto to = from + n;
        auto at = from;
        const auto firstCell = (from + summaryFrames - 1) / summaryFrames;
        const auto lastCell = to / summaryFrames;
        if (lastCell > firstCell) {
            for (; at < firstCell * summaryFrames; ++at)
                take(data[static_cast<std::size_t>(at)], data[static_cast<std::size_t>(at)]);
            for (auto cell = firstCell; cell < lastCell; ++cell)
                take(summary[static_cast<std::size_t>(cell)].first, summary[static_cast<std::size_t>(cell)].second);
            at = lastCell * summaryFrames;
        }
        for (; at < to; ++at)
            take(data[static_cast<std::size_t>(at)], data[static_cast<std::size_t>(at)]);
        frame += n;
    }
    return { low, high };
}

std::vector<std::vector<float>> Sound::render(std::int64_t start, std::int64_t end) const
{
    start = std::clamp<std::int64_t>(start, 0, frames_);
    end = std::clamp<std::int64_t>(end, start, frames_);
    std::vector<std::vector<float>> channels(static_cast<std::size_t>(channels_), std::vector<float>(static_cast<std::size_t>(end - start)));
    for (int c = 0; c < channels_; ++c)
        read(c, start, end - start, channels[static_cast<std::size_t>(c)].data());
    return channels;
}

std::vector<Piece> Sound::piecesOf(std::int64_t start, std::int64_t end) const
{
    start = std::clamp<std::int64_t>(start, 0, frames_);
    end = std::clamp<std::int64_t>(end, start, frames_);
    std::vector<Piece> result;
    auto frame = start;
    while (frame < end) {
        const auto index = pieceAt(frame);
        const auto& piece = pieces_[index];
        const auto within = frame - starts_[index];
        const auto n = std::min(end - frame, piece.length - within);
        result.push_back({ piece.block, piece.offset + within, n });
        frame += n;
    }
    return result;
}

Sound Sound::excerpt(const std::int64_t start, const std::int64_t end) const
{
    Sound result;
    result.channels_ = channels_;
    result.rate_ = rate_;
    result.pieces_ = piecesOf(start, end);
    result.rebuild();
    return result;
}

Sound Sound::removed(const std::int64_t start, const std::int64_t end) const
{
    Sound result;
    result.channels_ = channels_;
    result.rate_ = rate_;
    result.pieces_ = piecesOf(0, start);
    const auto rest = piecesOf(end, frames_);
    result.pieces_.insert(result.pieces_.end(), rest.begin(), rest.end());
    result.rebuild();
    return result;
}

Sound Sound::cropped(const std::int64_t start, const std::int64_t end) const
{
    return excerpt(start, end);
}

Sound Sound::inserted(const std::int64_t at, const Sound& other) const
{
    return replaced(at, at, other);
}

Sound Sound::replaced(std::int64_t start, std::int64_t end, const Sound& other) const
{
    start = std::clamp<std::int64_t>(start, 0, frames_);
    end = std::clamp<std::int64_t>(end, start, frames_);
    const auto fitted = conformed(other, channels_);
    Sound result;
    result.channels_ = channels_;
    result.rate_ = rate_;
    result.pieces_ = piecesOf(0, start);
    result.pieces_.insert(result.pieces_.end(), fitted.pieces_.begin(), fitted.pieces_.end());
    const auto rest = piecesOf(end, frames_);
    result.pieces_.insert(result.pieces_.end(), rest.begin(), rest.end());
    result.rebuild();
    return result;
}

Sound Sound::replaced(const std::int64_t start, const std::int64_t end, std::vector<std::vector<float>> channels) const
{
    channels.resize(static_cast<std::size_t>(channels_));
    return replaced(start, end, Sound(std::move(channels), rate_));
}

Sound Sound::mixedIn(std::int64_t at, const Sound& other, const float gain) const
{
    at = std::clamp<std::int64_t>(at, 0, frames_);
    const auto fitted = conformed(other, channels_);
    const auto length = fitted.frames();
    if (length <= 0)
        return *this;
    const auto ours = std::min(frames_, at + length) - at;
    auto mixed = render(at, at + ours);
    for (int c = 0; c < channels_; ++c) {
        auto& data = mixed[static_cast<std::size_t>(c)];
        data.resize(static_cast<std::size_t>(length), 0.0f);
        std::vector<float> theirs(static_cast<std::size_t>(length));
        fitted.read(c, 0, length, theirs.data());
        for (std::size_t i = 0; i < theirs.size(); ++i)
            data[i] += theirs[i] * gain;
    }
    return replaced(at, at + ours, std::move(mixed));
}

Sound Sound::remixed(const int count, const std::vector<std::vector<float>>& matrix) const
{
    const auto channels = std::clamp(count, 1, maximumChannels);
    const auto ours = render(0, frames_);
    std::vector<std::vector<float>> mixed(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames_), 0.0f));
    for (int out = 0; out < channels; ++out) {
        if (static_cast<std::size_t>(out) >= matrix.size())
            continue;
        const auto& row = matrix[static_cast<std::size_t>(out)];
        for (int in = 0; in < channels_ && static_cast<std::size_t>(in) < row.size(); ++in) {
            const auto weight = row[static_cast<std::size_t>(in)];
            if (weight == 0.0f)
                continue;
            const auto& from = ours[static_cast<std::size_t>(in)];
            auto& to = mixed[static_cast<std::size_t>(out)];
            for (std::size_t i = 0; i < to.size(); ++i)
                to[i] += from[i] * weight;
        }
    }
    return Sound(std::move(mixed), rate_);
}

Sound Sound::withRate(const double rate) const
{
    if (rate <= 0.0 || std::abs(rate - rate_) < 1.0e-9)
        return *this;
    const auto frames = static_cast<std::int64_t>(std::llround(static_cast<double>(frames_) * rate / rate_));
    auto channels = stretchChannels(render(0, frames_), rate_, rate, 1.0, 1.0, StretchAlgorithm::repitch, frames);
    return Sound(std::move(channels), rate);
}

// ---- Processes ---------------------------------------------------------------------------------

Sound processed(const Sound& sound, std::int64_t start, std::int64_t end, const unsigned mask,
                const std::function<void(std::vector<float>& channel, int index)>& process)
{
    start = std::clamp<std::int64_t>(start, 0, sound.frames());
    end = std::clamp<std::int64_t>(end, start, sound.frames());
    if (end <= start)
        return sound;
    auto channels = sound.render(start, end);
    for (int c = 0; c < sound.channels(); ++c)
        if (inMask(mask, c))
            process(channels[static_cast<std::size_t>(c)], c);
    return sound.replaced(start, end, std::move(channels));
}

float fadeShape(const FadeCurve curve, const float t) noexcept
{
    const auto x = std::clamp(t, 0.0f, 1.0f);
    switch (curve) {
    case FadeCurve::fast: return std::sin(x * std::numbers::pi_v<float> * 0.5f);
    case FadeCurve::slow: return x * x;
    case FadeCurve::smooth: return 0.5f - 0.5f * std::cos(x * std::numbers::pi_v<float>);
    case FadeCurve::linear: break;
    }
    return x;
}

Sound gained(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask, const float gainDb)
{
    const auto gain = std::pow(10.0f, gainDb / 20.0f);
    return processed(sound, start, end, mask, [gain](std::vector<float>& data, int) {
        for (auto& value : data)
            value *= gain;
    });
}

Sound normalised(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask, const float targetDb,
                 const bool rms)
{
    const auto stats = statistics(sound, start, end);
    double level = 0.0;
    double sum = 0.0;
    int count = 0;
    for (int c = 0; c < sound.channels(); ++c) {
        if (!inMask(mask, c))
            continue;
        level = std::max(level, static_cast<double>(stats.peak[static_cast<std::size_t>(c)]));
        sum += stats.rms[static_cast<std::size_t>(c)] * stats.rms[static_cast<std::size_t>(c)];
        ++count;
    }
    if (rms)
        level = count > 0 ? std::sqrt(sum / count) : 0.0;
    if (level <= 1.0e-9)
        return sound;
    const auto gainDb = static_cast<float>(targetDb - 20.0 * std::log10(level));
    return gained(sound, start, end, mask, gainDb);
}

Sound faded(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask, const bool in,
            const FadeCurve curve)
{
    return processed(sound, start, end, mask, [in, curve](std::vector<float>& data, int) {
        const auto n = data.size();
        if (n == 0)
            return;
        const auto span = static_cast<float>(std::max<std::size_t>(1, n - 1));
        for (std::size_t i = 0; i < n; ++i) {
            const auto t = static_cast<float>(i) / span;
            data[i] *= in ? fadeShape(curve, t) : fadeShape(curve, 1.0f - t);
        }
    });
}

Sound inverted(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask)
{
    return processed(sound, start, end, mask, [](std::vector<float>& data, int) {
        for (auto& value : data)
            value = -value;
    });
}

Sound reversed(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask)
{
    return processed(sound, start, end, mask, [](std::vector<float>& data, int) { std::reverse(data.begin(), data.end()); });
}

Sound silenced(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask)
{
    return processed(sound, start, end, mask, [](std::vector<float>& data, int) { std::fill(data.begin(), data.end(), 0.0f); });
}

Sound centred(const Sound& sound, const std::int64_t start, const std::int64_t end, const unsigned mask)
{
    return processed(sound, start, end, mask, [](std::vector<float>& data, int) {
        if (data.empty())
            return;
        double sum = 0.0;
        for (const auto value : data)
            sum += value;
        const auto mean = static_cast<float>(sum / static_cast<double>(data.size()));
        for (auto& value : data)
            value -= mean;
    });
}

Sound stretched(const Sound& sound, std::int64_t start, std::int64_t end, const double speed, const double pitch,
                const StretchAlgorithm algorithm)
{
    start = std::clamp<std::int64_t>(start, 0, sound.frames());
    end = std::clamp<std::int64_t>(end, start, sound.frames());
    if (end <= start || speed <= 0.0 || pitch <= 0.0)
        return sound;
    const auto frames = static_cast<std::int64_t>(std::llround(static_cast<double>(end - start) / speed));
    auto channels = stretchChannels(sound.render(start, end), sound.rate(), sound.rate(), speed, pitch, algorithm, frames);
    return sound.replaced(start, end, std::move(channels));
}

Statistics statistics(const Sound& sound, std::int64_t start, std::int64_t end)
{
    Statistics result;
    start = std::clamp<std::int64_t>(start, 0, sound.frames());
    end = std::clamp<std::int64_t>(end, start, sound.frames());
    result.frames = end - start;
    if (result.frames <= 0)
        return result;
    const auto channels = std::min(sound.channels(), maximumChannels);
    std::array<double, maximumChannels> squares {};
    std::array<double, maximumChannels> sums {};
    std::vector<std::vector<float>> all(static_cast<std::size_t>(channels));
    const auto keepAll = result.frames <= static_cast<std::int64_t>(sound.rate() * 60.0 * 60.0);
    constexpr std::int64_t chunk = 1 << 16;
    std::vector<float> buffer(chunk);
    std::vector<std::uint8_t> clippedFrame(chunk);
    for (auto at = start; at < end; at += chunk) {
        const auto n = std::min(chunk, end - at);
        std::fill_n(clippedFrame.begin(), n, std::uint8_t {});
        for (int c = 0; c < channels; ++c) {
            sound.read(c, at, n, buffer.data());
            auto& peak = result.peak[static_cast<std::size_t>(c)];
            for (std::int64_t i = 0; i < n; ++i) {
                const auto x = buffer[static_cast<std::size_t>(i)];
                const auto magnitude = std::abs(x);
                peak = std::max(peak, magnitude);
                squares[static_cast<std::size_t>(c)] += static_cast<double>(x) * x;
                sums[static_cast<std::size_t>(c)] += x;
                if (magnitude >= 0.9999f)
                    clippedFrame[static_cast<std::size_t>(i)] = 1;
            }
            if (keepAll)
                all[static_cast<std::size_t>(c)].insert(all[static_cast<std::size_t>(c)].end(), buffer.begin(), buffer.begin() + n);
        }
        for (std::int64_t i = 0; i < n; ++i)
            result.clipped += clippedFrame[static_cast<std::size_t>(i)];
    }
    for (int c = 0; c < channels; ++c) {
        result.rms[static_cast<std::size_t>(c)] = std::sqrt(squares[static_cast<std::size_t>(c)] / static_cast<double>(result.frames));
        result.dc[static_cast<std::size_t>(c)] = sums[static_cast<std::size_t>(c)] / static_cast<double>(result.frames);
    }
    if (keepAll) {
        std::vector<const float*> pointers;
        for (const auto& channel : all)
            pointers.push_back(channel.data());
        result.loudness = integratedLoudness(pointers.data(), channels, result.frames, sound.rate());
    }
    return result;
}

std::vector<float> spectrum(const Sound& sound, std::int64_t start, std::int64_t end, const int bins)
{
    std::vector<float> result(static_cast<std::size_t>(std::max(1, bins)), -120.0f);
    start = std::clamp<std::int64_t>(start, 0, sound.frames());
    end = std::clamp<std::int64_t>(end, start, sound.frames());
    constexpr std::size_t size = 4'096;
    if (end - start < 64)
        return result;
    Fft fft;
    fft.prepare(size);
    std::vector<float> window(size);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < size; ++i) {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / size));
        windowSum += window[i];
    }
    std::vector<double> power(size / 2, 0.0);
    std::vector<std::complex<float>> data(size);
    std::vector<float> channel(size);
    const auto length = end - start;
    // At most 400 windows, spread over the stretch.
    const auto windows = std::clamp<std::int64_t>(length / static_cast<std::int64_t>(size / 2), 1, 400);
    const auto spacing = windows > 1 ? (length - static_cast<std::int64_t>(size)) / (windows - 1) : 0;
    for (std::int64_t w = 0; w < windows; ++w) {
        const auto at = start + std::max<std::int64_t>(0, spacing) * w;
        std::fill(data.begin(), data.end(), std::complex<float> {});
        for (int c = 0; c < sound.channels(); ++c) {
            sound.read(c, at, static_cast<std::int64_t>(size), channel.data());
            for (std::size_t i = 0; i < size; ++i) {
                const auto inside = at + static_cast<std::int64_t>(i) < end;
                data[i] += inside ? channel[i] * window[i] / static_cast<float>(sound.channels()) : 0.0f;
            }
        }
        fft.forward(data.data());
        for (std::size_t bin = 0; bin < size / 2; ++bin)
            power[bin] += std::norm(data[bin]);
    }
    const auto nyquist = sound.rate() * 0.5;
    const auto binHz = sound.rate() / static_cast<double>(size);
    const auto scale = 2.0 / windowSum;
    for (int b = 0; b < bins; ++b) {
        const auto low = 20.0 * std::pow(nyquist / 20.0, static_cast<double>(b) / bins);
        const auto high = 20.0 * std::pow(nyquist / 20.0, static_cast<double>(b + 1) / bins);
        auto first = static_cast<std::size_t>(std::floor(low / binHz));
        auto last = static_cast<std::size_t>(std::ceil(high / binHz));
        first = std::clamp<std::size_t>(first, 1, size / 2 - 1);
        last = std::clamp<std::size_t>(last, first + 1, size / 2);
        double best = 0.0;
        for (auto bin = first; bin < last; ++bin)
            best = std::max(best, power[bin] / static_cast<double>(windows));
        result[static_cast<std::size_t>(b)] = static_cast<float>(10.0 * std::log10(std::max(best * scale * scale, 1.0e-12)));
    }
    return result;
}

} // namespace cydaw::engine::foundry
