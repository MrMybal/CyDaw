#include <cydaw/base/Image.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once


#include <memory>
#include <functional>
namespace cydaw::v2 {
// Background FFmpeg decoder, video only; no media audio endpoint is opened.
class CinemaVideo final {
public:
    struct Snapshot {
        cydaw::base::Image image;
        cydaw::base::Text file,error;
        cydaw::base::Text backend{"FFmpeg"},codec;
        double duration{},fps{25},time{};
        int width{},height{};
        std::uint64_t revision{};
        bool loading{};
    };
    explicit CinemaVideo(int maximumWidth = 960);
    ~CinemaVideo();
    void request(const cydaw::base::File&,double seconds);
    Snapshot snapshot() const;
    static cydaw::base::Result extractAudio(const cydaw::base::File& source,const cydaw::base::File& wav,const std::function<bool()>& cancel,bool* hasAudio = nullptr);
private:
    class Worker;
    std::unique_ptr<Worker> worker_;
};
}
