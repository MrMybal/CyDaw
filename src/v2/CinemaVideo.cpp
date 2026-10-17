#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CinemaVideo.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <cmath>
#include <mutex>
#include <vector>
namespace cydaw::v2 {
namespace {
juce::String mediaError(const char* action,int code)
{
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,message,sizeof(message));
    return juce::String::fromUTF8(action)+" : "+juce::String::fromUTF8(message);
}
// One demuxer and decoder per worker; FFmpeg never touches the audio device.
class MediaStream final {
public:
    AVFormatContext* format{};AVCodecContext* codec{};AVPacket* packet{};AVFrame* frame{};
    int index{-1},error{};bool draining{};double origin{},lastTime{};
    std::function<bool()> cancelled;
    ~MediaStream(){av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);avformat_close_input(&format);}
    juce::Result open(const juce::File& file,AVMediaType type,int threads,const std::function<bool()>& cancel)
    {
        cancelled=cancel;format=avformat_alloc_context();if(!format)return juce::Result::fail("FFmpeg : mémoire insuffisante");
        format->interrupt_callback={[](void* self){const auto& f=static_cast<MediaStream*>(self)->cancelled;return f&&f()?1:0;},this};
        auto result=avformat_open_input(&format,file.getFullPathName().toRawUTF8(),nullptr,nullptr);
        if(result<0)return juce::Result::fail(mediaError("FFmpeg, ouverture",result));
        result=avformat_find_stream_info(format,nullptr);
        if(result<0)return juce::Result::fail(mediaError("FFmpeg, analyse des pistes",result));
        const AVCodec* decoder=nullptr;index=av_find_best_stream(format,type,-1,-1,&decoder,0);
        if(index==AVERROR_STREAM_NOT_FOUND)return juce::Result::ok();
        if(index<0)return juce::Result::fail(mediaError("FFmpeg, décodeur indisponible",index));
        codec=avcodec_alloc_context3(decoder);if(!codec)return juce::Result::fail("FFmpeg : mémoire insuffisante");
        result=avcodec_parameters_to_context(codec,format->streams[index]->codecpar);
        if(result<0)return juce::Result::fail(mediaError("FFmpeg, paramètres du codec",result));
        codec->thread_count=threads;
        result=avcodec_open2(codec,decoder,nullptr);
        if(result<0)return juce::Result::fail(mediaError("FFmpeg, codec",result));
        packet=av_packet_alloc();frame=av_frame_alloc();
        if(!packet||!frame)return juce::Result::fail("FFmpeg : mémoire insuffisante");
        const auto* stream=format->streams[index];
        origin=stream->start_time!=AV_NOPTS_VALUE?stream->start_time*av_q2d(stream->time_base):
            format->start_time!=AV_NOPTS_VALUE?format->start_time/static_cast<double>(AV_TIME_BASE):0.0;
        return juce::Result::ok();
    }
    bool seek(double seconds)
    {
        const auto stamp=static_cast<std::int64_t>((seconds+origin)/av_q2d(format->streams[index]->time_base));
        error=av_seek_frame(format,index,stamp,AVSEEK_FLAG_BACKWARD);
        if(error<0)return false;
        avcodec_flush_buffers(codec);av_packet_unref(packet);av_frame_unref(frame);draining=false;lastTime=seconds;return true;
    }
    bool next()
    {
        av_frame_unref(frame);
        while(!cancelled||!cancelled()){
            error=avcodec_receive_frame(codec,frame);
            if(error>=0){
                const auto pts=frame->best_effort_timestamp;
                if(pts!=AV_NOPTS_VALUE)lastTime=pts*av_q2d(format->streams[index]->time_base)-origin;
                return true;
            }
            if(error==AVERROR_EOF)return false;
            if(error!=AVERROR(EAGAIN))return false;
            if(draining){error=AVERROR_EOF;return false;}
            for(;;){
                error=av_read_frame(format,packet);
                if(error<0){if(error!=AVERROR_EOF)return false;error=avcodec_send_packet(codec,nullptr);draining=true;break;}
                if(packet->stream_index==index){error=avcodec_send_packet(codec,packet);av_packet_unref(packet);break;}
                av_packet_unref(packet);
            }
            if(error<0&&error!=AVERROR_EOF)return false;
        }
        error=AVERROR_EXIT;return false;
    }
};
}
class CinemaVideo::Worker final:public juce::Thread {
public:
    explicit Worker(int width):Thread("CyDAW cinema video"),maximumWidth(width){startThread();}
    ~Worker()override{signalThreadShouldExit();notify();stopThread(10000);}
    const int maximumWidth;mutable std::mutex mutex;Snapshot state;juce::File requested;
    double requestedTime{};std::uint64_t sequence{};
    void request(const juce::File& f,double t){
        std::lock_guard guard(mutex);
        const auto fps=std::clamp(state.fps,1.0,240.0);
        t=std::floor(std::max(0.0,t)*fps+1.0e-8)/fps;
        if(f==requested&&std::abs(t-requestedTime)<.001)return;
        if(f!=requested){const auto revision=state.revision;state={};state.revision=revision;state.file=f.getFullPathName();state.loading=f!=juce::File();}
        requested=f;requestedTime=t;++sequence;notify();
    }
    void run()override{
        std::unique_ptr<MediaStream> reader;juce::File loaded;std::uint64_t handled=0;
        Snapshot metadata;double last=-1;SwsContext* scaler=nullptr;
        struct ScaleCleanup{SwsContext*& context;~ScaleCleanup(){sws_freeContext(context);}} cleanup{scaler};
        auto problem=[&](const juce::String& error,std::uint64_t seq){std::lock_guard guard(mutex);if(seq==sequence){state.error=error;state.loading=false;++state.revision;}};
        while(!threadShouldExit()){
            juce::File file;double wanted;std::uint64_t seq;
            {std::lock_guard guard(mutex);file=requested;wanted=requestedTime;seq=sequence;}
            if(seq==handled){wait(20);continue;}handled=seq;
            if(file==juce::File()){reader.reset();loaded={};continue;}
            if(file!=loaded||!reader){
                reader=std::make_unique<MediaStream>();loaded={};metadata={};metadata.file=file.getFullPathName();
                const auto result=reader->open(file,AVMEDIA_TYPE_VIDEO,maximumWidth<=160?1:2,[this]{return threadShouldExit();});
                if(result.failed()){problem(result.getErrorMessage(),seq);reader.reset();continue;}
                if(reader->index<0){problem("FFmpeg : aucune piste vidéo dans ce fichier",seq);reader.reset();continue;}
                const auto* stream=reader->format->streams[reader->index];
                const auto rate=av_guess_frame_rate(reader->format,reader->format->streams[reader->index],nullptr);
                if(rate.num>0&&rate.den>0)metadata.fps=av_q2d(rate);
                metadata.codec=avcodec_get_name(reader->codec->codec_id);metadata.width=reader->codec->width;metadata.height=reader->codec->height;
                metadata.duration=stream->duration!=AV_NOPTS_VALUE?stream->duration*av_q2d(stream->time_base):
                    reader->format->duration!=AV_NOPTS_VALUE?reader->format->duration/static_cast<double>(AV_TIME_BASE):0;
                if(metadata.width<=0||metadata.height<=0||metadata.width>16384||metadata.height>16384){problem("FFmpeg : dimensions vidéo non prises en charge",seq);reader.reset();continue;}
                loaded=file;last=-1;
            }
            wanted=metadata.duration>0?std::clamp(wanted,0.0,std::max(0.0,metadata.duration-1/metadata.fps)):std::max(0.0,wanted);
            if(last<0||wanted<last-.25/metadata.fps||wanted>last+.5){
                if(!reader->seek(wanted)){problem(mediaError("FFmpeg, déplacement vidéo",reader->error),seq);continue;}last=-1;
            }
            AVFrame* selected=av_frame_alloc();if(!selected){problem("FFmpeg : mémoire insuffisante",seq);continue;}
            bool obsolete=false;
            while(reader->next()){
                av_frame_unref(selected);av_frame_ref(selected,reader->frame);last=reader->lastTime;
                if(last+.5/metadata.fps>=wanted)break;
                {std::lock_guard guard(mutex);if(sequence!=seq){obsolete=true;break;}}
            }
            if(obsolete||threadShouldExit()||!selected->data[0]){av_frame_free(&selected);continue;}
            if(reader->error<0&&reader->error!=AVERROR_EOF){problem(mediaError("FFmpeg, décodage vidéo",reader->error),seq);av_frame_free(&selected);continue;}
            const auto factor=std::min(1.0,static_cast<double>(maximumWidth)/std::max(static_cast<double>(selected->width),selected->height*16.0/9));
            const auto outW=std::max(1,static_cast<int>(selected->width*factor)),outH=std::max(1,static_cast<int>(selected->height*factor));
            scaler=sws_getCachedContext(scaler,selected->width,selected->height,static_cast<AVPixelFormat>(selected->format),outW,outH,AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if(!scaler){problem("FFmpeg : conversion de l'image impossible",seq);av_frame_free(&selected);continue;}
            juce::Image image(juce::Image::ARGB,outW,outH,false);
            {juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
                std::uint8_t* planes[]={pixels.data,nullptr,nullptr,nullptr};int strides[]={pixels.lineStride,0,0,0};
                sws_scale(scaler,selected->data,selected->linesize,0,selected->height,planes,strides);
            }
            av_frame_free(&selected);
            {std::lock_guard guard(mutex);if(sequence==seq){const auto revision=state.revision+1;state=metadata;state.image=image;state.time=last;state.loading=false;state.revision=revision;}}
        }
    }
};
CinemaVideo::CinemaVideo(int width):worker_(std::make_unique<Worker>(std::clamp(width,80,1920))){}
CinemaVideo::~CinemaVideo()=default;
void CinemaVideo::request(const juce::File& file,double seconds){worker_->request(file,seconds);}
CinemaVideo::Snapshot CinemaVideo::snapshot()const{std::lock_guard guard(worker_->mutex);return worker_->state;}

juce::Result CinemaVideo::extractAudio(const juce::File& source,const juce::File& wav,const std::function<bool()>& cancel,bool* hasAudio)
{
    if(hasAudio)*hasAudio=false;
    MediaStream input;const auto opened=input.open(source,AVMEDIA_TYPE_AUDIO,2,cancel);
    if(opened.failed())return opened;
    if(input.index<0)return juce::Result::ok();
    if(hasAudio)*hasAudio=true;
    juce::FileOutputStream out(wav);if(out.failedToOpen())return out.getStatus();
    out.write("RIFF",4);out.writeInt(0);out.write("WAVEfmt ",8);out.writeInt(16);out.writeShort(1);out.writeShort(2);
    out.writeInt(48000);out.writeInt(192000);out.writeShort(4);out.writeShort(16);out.write("data",4);out.writeInt(0);
    SwrContext* converter=nullptr;struct Cleanup{SwrContext*& context;~Cleanup(){swr_free(&context);}} cleanup{converter};
    int sourceRate=0;std::uint64_t bytes=0;std::vector<std::uint8_t> pcm;
    auto write=[&](int count)->juce::Result{
        if(count<0)return juce::Result::fail(mediaError("FFmpeg, conversion audio",count));
        const auto size=static_cast<std::size_t>(count)*4;
        if(bytes+size>0xffffff00u)return juce::Result::fail("Bande son trop longue pour un WAV PCM classique");
        if(!out.write(pcm.data(),size))return out.getStatus();bytes+=size;return juce::Result::ok();
    };
    while(input.next()){
        auto* frame=input.frame;
        if(!converter){
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            sourceRate=frame->sample_rate;
            auto result=swr_alloc_set_opts2(&converter,&stereo,AV_SAMPLE_FMT_S16,48000,&frame->ch_layout,static_cast<AVSampleFormat>(frame->format),sourceRate,0,nullptr);
            if(result>=0)result=swr_init(converter);
            if(result<0)return juce::Result::fail(mediaError("FFmpeg, préparation audio",result));
        }
        const auto count=static_cast<int>(av_rescale_rnd(swr_get_delay(converter,sourceRate)+frame->nb_samples,48000,sourceRate,AV_ROUND_UP));
        pcm.resize(static_cast<std::size_t>(count)*4);std::uint8_t* output[]={pcm.data()};
        const auto written=write(swr_convert(converter,output,count,const_cast<const std::uint8_t**>(frame->extended_data),frame->nb_samples));
        if(written.failed())return written;
    }
    if(cancel&&cancel())return juce::Result::fail("Import audio annulé");
    if(input.error!=AVERROR_EOF)return juce::Result::fail(mediaError("FFmpeg, décodage audio",input.error));
    if(converter)for(;;){
        pcm.resize(4096*4);std::uint8_t* output[]={pcm.data()};const auto count=swr_convert(converter,output,4096,nullptr,0);
        if(!count)break;const auto written=write(count);if(written.failed())return written;
    }
    if(!bytes)return juce::Result::fail("Bande son vide");
    out.setPosition(4);out.writeInt(static_cast<int>(bytes+36));out.setPosition(40);out.writeInt(static_cast<int>(bytes));out.flush();
    return out.getStatus();
}
}
