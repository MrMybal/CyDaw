#include "FoundryPlayer.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {

namespace {
constexpr std::int64_t cacheFrames = 8'192;
} // namespace

FoundryPlayer::FoundryPlayer()
{
    for (auto& side : cache_)
        side.assign(static_cast<std::size_t>(cacheFrames), 0.0f);
    scratch_.assign(static_cast<std::size_t>(cacheFrames) * 2, 0.0f);
}

FoundryPlayer::~FoundryPlayer() = default;

void FoundryPlayer::prepareToPlay(int samplesPerBlockExpected, const double sampleRate)
{
    rate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    cacheStart_ = -1;
    const auto p=processor_.read();if(p&&*p)(*p)->prepare(rate_,samplesPerBlockExpected);
}

void FoundryPlayer::releaseResources() {}

void FoundryPlayer::setSession(std::shared_ptr<const engine::foundry::Session> session)
{
    freeRetired();
    std::shared_ptr<const engine::foundry::Session> old;
    {
        const std::scoped_lock lock(lock_);
        old = std::move(session_);
        session_ = std::move(session);
        pending_.store(session_.get(), std::memory_order_release);
        hasPending_.store(true, std::memory_order_release);
        if (old != nullptr)
            retired_.emplace_back(blocks_.load(), std::move(old));
    }
}

void FoundryPlayer::freeRetired()
{
    const auto now = blocks_.load();
    const std::scoped_lock lock(lock_);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const auto& entry) { return now >= entry.first + 2; }),
                   retired_.end());
}

void FoundryPlayer::play(const std::int64_t from, const std::int64_t to, const bool loop)
{
    from_.store(std::max<std::int64_t>(0, from), std::memory_order_relaxed);
    to_.store(to, std::memory_order_relaxed);
    loop_.store(loop, std::memory_order_relaxed);
    position_.store(static_cast<double>(std::max<std::int64_t>(0, from)), std::memory_order_relaxed);
    stopping_.store(false, std::memory_order_relaxed);
    restart_.store(true, std::memory_order_release);
    playing_.store(true, std::memory_order_release);
}

void FoundryPlayer::stop()
{
    stopping_.store(true, std::memory_order_release);
}

float FoundryPlayer::takePeak(const int side) noexcept
{
    return side >= 0 && side < 2 ? peaks_[static_cast<std::size_t>(side)].exchange(0.0f, std::memory_order_relaxed) : 0.0f;
}

float FoundryPlayer::sampleAt(const int channel, const double frame) noexcept
{
    const auto i = static_cast<std::int64_t>(std::floor(frame));
    if (cacheStart_ < 0 || i - 1 < cacheStart_ || i + 2 >= cacheStart_ + cacheLength_) {
        cacheStart_ = i - 2;
        cacheLength_ = cacheFrames;
        engine::foundry::mix(*current_, cacheStart_, static_cast<int>(cacheLength_), cache_[0].data(), cache_[1].data(), scratch_.data());
    }
    const auto& data = cache_[static_cast<std::size_t>(channel)];
    const auto k = static_cast<std::size_t>(i - cacheStart_);
    const auto t = static_cast<float>(frame - static_cast<double>(i));
    const auto xm1 = data[k - 1];
    const auto x0 = data[k];
    const auto x1 = data[k + 1];
    const auto x2 = data[k + 2];
    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void FoundryPlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& output)
{
    output.clearActiveBufferRegion();
    if (hasPending_.exchange(false, std::memory_order_acq_rel)) {
        current_ = pending_.load(std::memory_order_acquire);
        currentFrames_ = current_ != nullptr ? current_->frames() : 0;
        cacheStart_ = -1;
    }
    blocks_.fetch_add(1, std::memory_order_acq_rel);
    if (output.buffer == nullptr || output.numSamples <= 0)
        return;
    if (restart_.exchange(false, std::memory_order_acq_rel)) {
        playFrom_ = from_.load(std::memory_order_relaxed);
        playTo_ = to_.load(std::memory_order_relaxed);
        looping_ = loop_.load(std::memory_order_relaxed);
        at_ = static_cast<double>(playFrom_);
        running_ = true;tailSamples_=0;
        level_ = 0.0f;
        cacheStart_ = -1;
    }
    if ((!running_ && tailSamples_<=0) || current_ == nullptr || currentFrames_ <= 0) {
        running_ = false;
        playing_.store(false, std::memory_order_release);
        return;
    }
    const auto end = playTo_ > playFrom_ ? std::min(playTo_, currentFrames_) : currentFrames_;
    const auto step = current_->rate / rate_;
    const auto gain = gain_.load(std::memory_order_relaxed);
    const auto stopping = stopping_.load(std::memory_order_acquire);
    const auto fade = 1.0f / static_cast<float>(std::max(1.0, rate_ * 0.003));
    const auto outputs = std::min(2, output.buffer->getNumChannels());
    auto* left = output.buffer->getWritePointer(0, output.startSample);
    auto* right = outputs > 1 ? output.buffer->getWritePointer(1, output.startSample) : nullptr;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    const auto processor=processor_.read();
    if(processor&&*processor) {
        int done=0;
        while(done<output.numSamples && running_) {
            if(at_>=static_cast<double>(end)) {
                if(looping_&&end>playFrom_)at_=playFrom_+std::fmod(at_-end,static_cast<double>(end-playFrom_));
                else {running_=false;tailSamples_=static_cast<int>(rate_*3);break;}
            }
            const int n=std::min(output.numSamples-done,std::max(1,static_cast<int>(std::ceil((end-at_)/step))));
            // Stereo scratch is fixed in the player; processing allocates nothing here.
            std::array<float,128> l{},r{};
            for(int offset=0;offset<n;offset+=128) {
                const int count=std::min(128,n-offset);
                (*processor)->processFoundry(*current_,at_,step,l.data(),r.data(),count);
                for(int i=0;i<count;++i) {
                    level_=stopping?std::max(0.0f,level_-fade):std::min(1.0f,level_+fade);
                    if(stopping&&level_<=0)running_=false;
                    const float x=l[i]*gain*level_,y=r[i]*gain*level_;
                    left[done+offset+i]=x;if(right)right[done+offset+i]=y;
                    peakLeft=std::max(peakLeft,std::abs(x));peakRight=std::max(peakRight,std::abs(y));at_+=step;
                }
            }
            done+=n;
        }
        if(!running_&&tailSamples_>0&&!stopping) {
            for(int offset=done;offset<output.numSamples;offset+=128) {
                const int n=std::min({128,output.numSamples-offset,tailSamples_});if(n<=0)break;
                std::array<float,128> l{},r{};(*processor)->process(nullptr,0,l.data(),r.data(),n);
                for(int i=0;i<n;++i){left[offset+i]=l[i]*gain;if(right)right[offset+i]=r[i]*gain;peakLeft=std::max(peakLeft,std::abs(left[offset+i]));peakRight=std::max(peakRight,std::abs(r[i]*gain));}tailSamples_-=n;
            }
        } else if(stopping)tailSamples_=0;
    } else for (int i = 0; i < output.numSamples; ++i) {
        level_ = stopping ? std::max(0.0f, level_ - fade) : std::min(1.0f, level_ + fade);
        if (stopping && level_ <= 0.0f) {
            running_ = false;
            break;
        }
        if (at_ >= static_cast<double>(end)) {
            if (looping_ && end > playFrom_) {
                at_ = static_cast<double>(playFrom_) + std::fmod(at_ - static_cast<double>(end), static_cast<double>(end - playFrom_));
            } else {
                running_ = false;
                break;
            }
        }
        const auto l = sampleAt(0, at_) * gain * level_;
        const auto r = sampleAt(1, at_) * gain * level_;
        left[i] += l;
        if (right != nullptr)
            right[i] += r;
        peakLeft = std::max(peakLeft, std::abs(l));
        peakRight = std::max(peakRight, std::abs(r));
        at_ += step;
    }
    position_.store(at_, std::memory_order_relaxed);
    if (peakLeft > peaks_[0].load(std::memory_order_relaxed))
        peaks_[0].store(peakLeft, std::memory_order_relaxed);
    if (peakRight > peaks_[1].load(std::memory_order_relaxed))
        peaks_[1].store(peakRight, std::memory_order_relaxed);
    if (!running_ && tailSamples_<=0)
        playing_.store(false, std::memory_order_release);
}

} // namespace cydaw::app
