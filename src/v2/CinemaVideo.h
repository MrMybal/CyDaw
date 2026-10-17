#pragma once
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <functional>
namespace cydaw::v2 {
// Background FFmpeg decoder, video only; no media audio endpoint is opened.
class CinemaVideo final {
public:
    struct Snapshot {
        juce::Image image;
        juce::String file,error;
        juce::String backend{"FFmpeg"},codec;
        double duration{},fps{25},time{};
        int width{},height{};
        std::uint64_t revision{};
        bool loading{};
    };
    explicit CinemaVideo(int maximumWidth = 960);
    ~CinemaVideo();
    void request(const juce::File&,double seconds);
    Snapshot snapshot() const;
    static juce::Result extractAudio(const juce::File& source,const juce::File& wav,const std::function<bool()>& cancel,bool* hasAudio = nullptr);
private:
    class Worker;
    std::unique_ptr<Worker> worker_;
};
}
