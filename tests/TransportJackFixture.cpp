// Contract fixture for the dynamically loaded JACK transport API. Never an audio backend.
#include <atomic>
#include <cmath>
#include <jack/jack.h>
#include <thread>
#include <windows.h>
struct Shared {
    double seconds, bpm, stamp;
    int playing, denominator, starts, stops, locates, noStart, opens, closes, occupied, shutdown;
};
static HANDLE mapping {};
static Shared* shared()
{
    static Shared* p = []() {
        mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"Local\\CyDAWTransportQa20261017");
        return static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096));
    }();
    return p;
}
static double now() { return GetTickCount64() * .001; }
static double position()
{
    auto* p = shared();
    return p->seconds + (p->playing ? now() - p->stamp : 0);
}
static JackTimebaseCallback timebase {};
static void* timebaseArg {};
static JackShutdownCallback shutdownCallback {};
static void* shutdownArg {};
static std::atomic<bool> stopping { false };
static std::thread worker;
static void fill(jack_position_t& p)
{
    auto* s = shared();
    p = {};
    p.frame_rate = 48000;
    p.frame = static_cast<jack_nframes_t>(position() * 48000);
    p.valid = JackPositionBBT;
    p.beats_per_bar = 4;
    p.beat_type = static_cast<float>(s->denominator);
    p.beats_per_minute = s->bpm;
}
extern "C" {
__declspec(dllexport) void qa_set(double seconds, int rolling, double bpm, int denominator)
{
    auto* p = shared();
    p->seconds = seconds;
    p->playing = rolling;
    p->bpm = bpm;
    p->denominator = denominator;
    p->stamp = now();
    p->shutdown = 0;
}
__declspec(dllexport) double qa_number(int i)
{
    auto* p = shared();
    switch (i) {
    case 0:
        return position();
    case 1:
        return p->playing;
    case 2:
        return p->bpm;
    case 3:
        return p->starts;
    case 4:
        return p->stops;
    case 5:
        return p->locates;
    case 6:
        return p->noStart;
    case 7:
        return p->opens;
    case 8:
        return p->closes;
    }
    return -1;
}
__declspec(dllexport) void qa_occupied(int v) { shared()->occupied = v; }
__declspec(dllexport) void qa_shutdown() { shared()->shutdown = 1; }
jack_client_t* jack_client_open(const char*, jack_options_t options, jack_status_t* status, ...)
{
    *status = static_cast<jack_status_t>(0);
    shared()->noStart = (options & JackNoStartServer) != 0;
    shared()->opens++;
    return reinterpret_cast<jack_client_t*>(1);
}
int jack_client_close(jack_client_t*)
{
    stopping = true;
    if (worker.joinable())
        worker.join();
    timebase = nullptr;
    shutdownCallback = nullptr;
    shared()->closes++;
    return 0;
}
void jack_on_shutdown(jack_client_t*, JackShutdownCallback callback, void* arg)
{
    shutdownCallback = callback;
    shutdownArg = arg;
}
int jack_activate(jack_client_t*)
{
    stopping = false;
    worker = std::thread([]() {
        while (!stopping) {
            auto* s = shared();
            if (s->shutdown && shutdownCallback) {
                shutdownCallback(shutdownArg);
                return;
            }
            if (timebase) {
                jack_position_t p {};
                fill(p);
                timebase(s->playing ? JackTransportRolling : JackTransportStopped, 480, &p, 0, timebaseArg);
                s->bpm = p.beats_per_minute;
                s->denominator = static_cast<int>(p.beat_type);
            }
            Sleep(10);
        }
    });
    return 0;
}
jack_transport_state_t jack_transport_query(const jack_client_t*, jack_position_t* p)
{
    fill(*p);
    return shared()->playing ? JackTransportRolling : JackTransportStopped;
}
void jack_transport_start(jack_client_t*)
{
    auto* s = shared();
    s->seconds = position();
    s->stamp = now();
    s->playing = 1;
    s->starts++;
}
void jack_transport_stop(jack_client_t*)
{
    auto* s = shared();
    s->seconds = position();
    s->stamp = now();
    s->playing = 0;
    s->stops++;
}
int jack_transport_locate(jack_client_t*, jack_nframes_t frame)
{
    auto* s = shared();
    s->seconds = frame / 48000.0;
    s->stamp = now();
    s->locates++;
    return 0;
}
jack_nframes_t jack_get_sample_rate(jack_client_t*) { return 48000; }
int jack_set_timebase_callback(jack_client_t*, int conditional, JackTimebaseCallback callback, void* arg)
{
    if (conditional && shared()->occupied)
        return 16;
    timebase = callback;
    timebaseArg = arg;
    return 0;
}
}
