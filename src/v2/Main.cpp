#include <iostream>
#include <cydaw/base/Utilities.h>
#include <cydaw/audio/SampleBuffer.h>
#include <cydaw/base/EventLoop.h>
#include "SamplerMapFile.h"
#include <cydaw/base/File.h>
#include "AppUi.h"
#include "NativeDspRuntime.h"

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectProcessors.h>
#include "RemoteControlServer.h"
#include "CrashReport.h"
#include "ProjectFileAssociation.h"
#include "ImGuiHost.h"
#include "Studio.h"
#include "Widgets.h"

#include "Localization.h"


// clang-format off
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <windowsx.h>
// clang-format on
#undef min
#undef max
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace cydaw::v2 {
namespace {

// Probes: --snapshot=<png> renders the window into a PNG after
// --snapshot-delay=<ms> (default 1500), then quits when --quit is given.
struct LaunchOptions {
    cydaw::base::File snapshot;
    int snapshotDelayMs { 1'500 };
    bool quitAfterSnapshot {};
    bool silent {};
    // Off every screen, never in front (automated sessions: --hidden, or
    // --silent unless --visible).
    bool hidden {};
    bool visible {};
    int width { 1'600 };
    int height { 960 };
    cydaw::base::Text language;
    cydaw::base::File project;
    bool demo {};
    int view {};
    int track { -1 };
    cydaw::base::File dumpGraphs;
    cydaw::base::File saveAs;
    bool audioSettings {};
    bool registerProjectFiles {};
    cydaw::base::File roundTrip;
    cydaw::base::File modulationTest;
    cydaw::base::File displayTest;
    bool effectsTest {};
    int controlPort { 47'820 };
    int detach {};
};

LaunchOptions parseOptions(const cydaw::base::Text& commandLine)
{
    LaunchOptions options;
    for (const auto& argument : cydaw::base::TextList::fromTokens(commandLine, true)) {
        const auto value = argument.unquoted();
        if (value.startsWith("--snapshot="))
            options.snapshot = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value.startsWith("--snapshot-delay="))
            options.snapshotDelayMs = value.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (value == "--quit")
            options.quitAfterSnapshot = true;
        else if (value == "--silent")
            options.silent = true;
        else if (value == "--hidden")
            options.hidden = true;
        else if (value == "--visible")
            options.visible = true;
        else if (value.startsWith("--window-size=")) {
            const auto size = value.fromFirstOccurrenceOf("=", false, false);
            options.width = cydaw::base::maximum(800, size.upToFirstOccurrenceOf("x", false, true).getIntValue());
            options.height = cydaw::base::maximum(500, size.fromFirstOccurrenceOf("x", false, true).getIntValue());
        } else if (value.startsWith("--language="))
            options.language = value.fromFirstOccurrenceOf("=", false, false);
        else if (value == "--demo")
            options.demo = true;
        else if (value.startsWith("--view="))
            options.view = value.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (value.startsWith("--roundtrip-test="))
            options.roundTrip = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value == "--effects-test")
            options.effectsTest = true;
        else if (value.startsWith("--display-test="))
            options.displayTest = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value.startsWith("--modulation-test="))
            options.modulationTest = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value.startsWith("--detach="))
            options.detach = value.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (value.startsWith("--control-port="))
            options.controlPort = value.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (value == "--register-project-files")
            options.registerProjectFiles = true;
        else if (value == "--audio-settings")
            options.audioSettings = true;
        else if (value.startsWith("--save-as="))
            options.saveAs = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value.startsWith("--dump-graphs="))
            options.dumpGraphs = cydaw::base::File(value.fromFirstOccurrenceOf("=", false, false).unquoted());
        else if (value.startsWith("--track="))
            options.track = value.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (!value.startsWith("--") && cydaw::base::File::isAbsolutePath(value) && cydaw::base::File(value).existsAsFile())
            options.project = cydaw::base::File(value);
    }
    // A silent session is an automated one (tests, probes, builders): out of
    // sight too, unless asked for.
    if (options.silent && !options.visible)
        options.hidden = true;
    return options;
}

class MainContent final {
public:
    explicit MainContent(Studio& studio) : ui_(studio, host_) {
        host_.onInitialised = [this] { ui_.initialise(); };
        host_.onFrame = [this] { ui_.frame(); };
        host_.beforeNewFrame = [this] { ui_.beforeNewFrame(); };
        host_.onFilesDropped = [this](const cydaw::base::TextList& files, float x, float y) { ui_.filesDropped(files, x, y); };
    }
    ImGuiHost& host() noexcept { return host_; }
    AppUi& ui() noexcept { return ui_; }
private:
    ImGuiHost host_;
    AppUi ui_;
};
class MainWindow final {
    HWND window_{};
    std::unique_ptr<MainContent> content_;
    bool hidden_{};
    POINT automationSize_{};
    std::function<void()> close_;
    int hitTest(const int x, const int y) const {
        RECT bounds{};GetWindowRect(window_, &bounds);
        if (!IsZoomed(window_)) {
            const auto dpi=GetDpiForWindow(window_);
            const auto edge=GetSystemMetricsForDpi(SM_CXSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
            const auto top=y<bounds.top+edge, bottom=y>=bounds.bottom-edge;
            const auto left=x<bounds.left+edge, right=x>=bounds.right-edge;
            if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
            if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
            if(left)return HTLEFT;
            if(right)return HTRIGHT;
        }
        if(content_) {
            POINT point{x,y};ScreenToClient(static_cast<HWND>(content_->host().nativeHandle()),&point);
            if(content_->host().windowDragRegion.contains(cydaw::base::Point<int>{point.x,point.y}))return HTCAPTION;
        }
        return HTCLIENT;
    }

    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(message==WM_NCCALCSIZE) {
                // Keep overlapped-window styles for the taskbar and native move,
                // resize and snap behaviour, but draw the caption in the client.
                if(w && IsZoomed(window)) {
                    MONITORINFO monitor{sizeof(monitor)};
                    if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))
                        reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0]=monitor.rcWork;
                }
                return 0;
            }
            if(message==WM_NCHITTEST)return self->hitTest(GET_X_LPARAM(l),GET_Y_LPARAM(l));
            if(message==WM_ACTIVATE || message==WM_DWMCOMPOSITIONCHANGED) {
                const MARGINS shadow{1,1,1,1};DwmExtendFrameIntoClientArea(window,&shadow);
            }
            if(message==WM_CLOSE){if(self->close_)self->close_();return 0;}
            if(message==WM_SIZE&&w!=SIZE_MINIMIZED){if(self->content_)self->content_->host().setBounds({0,0,int(LOWORD(l)),int(HIWORD(l))});return 0;}
            if(message==WM_GETMINMAXINFO){
                auto* info=reinterpret_cast<MINMAXINFO*>(l);
                // Hidden automation surfaces must honour --window-size even
                // when a headless runner exposes only a 1024x768 monitor.
                if(self->hidden_) {
                    info->ptMaxTrackSize.x=std::max(info->ptMaxTrackSize.x,self->automationSize_.x);
                    info->ptMaxTrackSize.y=std::max(info->ptMaxTrackSize.y,self->automationSize_.y);
                }
                const auto dpi=GetDpiForWindow(window);const auto scale=dpi?dpi:96;
                info->ptMinTrackSize={MulDiv(960,scale,96),MulDiv(600,scale,96)};
                MONITORINFO monitor{sizeof(monitor)};
                if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
                    info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
                    info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
                    info->ptMinTrackSize.x=std::min(info->ptMinTrackSize.x,info->ptMaxSize.x);
                    info->ptMinTrackSize.y=std::min(info->ptMinTrackSize.y,info->ptMaxSize.y);
                }
                return 0;
            }
            if(message==WM_SETFOCUS&&self->content_&&!self->hidden_){if(auto child=self->content_->host().nativeHandle())SetFocus(static_cast<HWND>(child));return 0;}
            if(message==WM_DPICHANGED){const auto* r=reinterpret_cast<const RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_MOUSEACTIVATE&&self->hidden_)return MA_NOACTIVATE;
        }
        return DefWindowProcW(window,message,w,l);
    }
public:
    MainWindow(Studio& studio,const LaunchOptions& options,std::function<void()> close,std::function<void()> forceQuit)
        :content_(std::make_unique<MainContent>(studio)),hidden_(options.hidden),automationSize_{options.width,options.height},close_(std::move(close)) {
        WNDCLASSEXW type{sizeof(type)};type.style=CS_DBLCLKS;type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));type.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));type.lpszClassName=L"CyDaw.Desktop";type.hIcon=LoadIconW(type.hInstance,MAKEINTRESOURCEW(101));RegisterClassExW(&type);
        const DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;const DWORD extra=hidden_?WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE:WS_EX_APPWINDOW;
        RECT bounds{0,0,options.width,options.height};
        const int x=hidden_?-20000:std::max<LONG>(0,(GetSystemMetrics(SM_CXSCREEN)-(bounds.right-bounds.left))/2);
        const int y=hidden_?-20000:std::max<LONG>(0,(GetSystemMetrics(SM_CYSCREEN)-(bounds.bottom-bounds.top))/2);
        window_=CreateWindowExW(extra,type.lpszClassName,L"CyDaw",style,x,y,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,type.hInstance,this);
        if(!window_)throw std::runtime_error("CyDaw main window creation failed");
        SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        // The initial WM_GETMINMAXINFO can precede WM_NCCREATE/user data.
        // Reapply the requested hidden size now that our track limit is active.
        if(hidden_)SetWindowPos(window_,nullptr,0,0,options.width,options.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        const MARGINS shadow{1,1,1,1};DwmExtendFrameIntoClientArea(window_,&shadow);
        content_->host().onWindowHitTest=[this](int x,int y){return hitTest(x,y);};
        content_->host().windowMaximized=[this]{return IsZoomed(window_)!=FALSE;};
        content_->host().onWindowAction=[this](ImGuiHost::WindowAction action){
            const auto command=action==ImGuiHost::WindowAction::minimize?SC_MINIMIZE:
                action==ImGuiHost::WindowAction::maximizeRestore?(IsZoomed(window_)?SC_RESTORE:SC_MAXIMIZE):SC_CLOSE;
            PostMessageW(window_,WM_SYSCOMMAND,command,0);
        };
        content_->ui().onQuit=close_;content_->ui().onQuitNow=std::move(forceQuit);
        content_->ui().onTitle=[this](const cydaw::base::Text& title){SetWindowTextW(window_,title.toWideCharPointer());};
        content_->host().background=hidden_;RECT client{};GetClientRect(window_,&client);content_->host().create(window_,client.right,client.bottom);
        if(!content_->host().isReady())throw std::runtime_error("CyDaw Direct3D surface creation failed");
        ShowWindow(window_,hidden_?SW_SHOWNOACTIVATE:SW_SHOW);if(hidden_)SetWindowPos(window_,HWND_BOTTOM,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    }
    ~MainWindow(){content_.reset();if(window_&&IsWindow(window_))DestroyWindow(window_);}
    MainContent& content() noexcept {return *content_;}
    void poll(){if(content_)content_->host().poll();}
    void toFront(bool focus){if(hidden_)return;ShowWindow(window_,SW_RESTORE);if(focus)SetForegroundWindow(window_);}
};

} // namespace

// Probes: every track's graph as text — cards, positions, sockets, knobs.
cydaw::base::Text describeSession(Studio& studio)
{
    cydaw::base::Text out;
    auto& engine = studio.engine();
    out << "tempo " << engine.tempo() << " master " << engine.masterGainDb() << " loop " << (studio.loopEnabled() ? 1 : 0)
        << " " << engine.loopStart() << " " << engine.loopEnd() << "\n";
    for (const auto& track : studio.tracks()) {
        out << "== " << studio.trackName(track) << (track.midi ? " (midi)" : " (audio)") << "\n";
        const auto mix = studio.trackMix(track);
        out << "  mix " << mix.gainDb << " " << mix.pan << " " << (mix.muted ? 1 : 0) << (mix.solo ? 1 : 0) << " colour "
            << cydaw::base::Text::toHexString(static_cast<std::int64_t>(studio.trackColour(track))) << "\n";
        if (track.midi) {
            if (const auto* clips = engine.midiClipsView(track.midiId))
                for (const auto& clip : *clips) {
                    auto pitches = 0;
                    for (const auto& note : clip.notes)
                        pitches += note.noteNumber;
                    out << "  midi clip " << clip.name << " " << clip.startBeats << " " << clip.lengthBeats << " loop "
                        << (clip.looping ? 1 : 0) << " notes " << static_cast<int>(clip.notes.size()) << " sum " << pitches << "\n";
                }
        } else if (const auto* clips = engine.audioClipsView(track.audioIndex)) {
            for (const auto& clip : *clips)
                out << "  audio clip " << clip.name << " " << clip.startSeconds << " " << clip.durationSeconds << " "
                    << clip.sourceOffsetSeconds << " " << clip.sourceFile.getFileName() << "\n";
        }
        if (auto* slot = studio.graphFor(track)) {
            for (const auto& node : slot->graph().editorNodes()) {
                out << "  node " << static_cast<std::int64_t>(node.id) << " " << node.typeKey << " \"" << node.title << "\" at "
                    << node.position.x << ", " << node.position.y << " ports " << static_cast<int>(node.ports.size())
                    << " params " << static_cast<int>(node.parameters.size()) << (node.boundary ? " boundary" : "") << "\n";
                for (const auto& port : node.ports)
                    out << "     port " << static_cast<std::int64_t>(port.id) << (port.input ? " in " : " out ") << port.key
                        << " kind " << port.kind << " ch " << port.channels << "\n";
            }
            for (const auto& connection : slot->graph().editorConnections())
                out << "  wire " << static_cast<std::int64_t>(connection.source) << ":" << static_cast<std::int64_t>(connection.sourcePort)
                    << " -> " << static_cast<std::int64_t>(connection.destination) << ":"
                    << static_cast<std::int64_t>(connection.destinationPort) << " kind " << connection.kind << "\n";
            out << "  compile: " << (slot->lastCompileError().isEmpty() ? cydaw::base::Text("ok") : slot->lastCompileError()) << "\n";
        }
    }
    return out;
}

void dumpGraphs(Studio& studio, const cydaw::base::File& file)
{
    auto text = describeSession(studio);
    // The compiled graphs too: which engine node stands for which card.
    for (const auto& track : studio.tracks()) {
        const auto* slot = studio.graphFor(track);
        if (slot == nullptr)
            continue;
        text << "\n== " << studio.trackName(track) << (track.midi ? " (midi)" : " (audio)") << "\n  cards:";
        for (const auto& node : slot->graph().editorNodes())
            text << " " << cydaw::base::Text(static_cast<std::int64_t>(node.id)) << ":" << node.typeKey
                 << (node.composite ? "*" : "");
        const auto native = slot->graph().nativeDspGraph();
        text << "\n  native:";
        for (const auto& node : native.nodes)
            text << " " << cydaw::base::Text(static_cast<std::int64_t>(node.id)) << ":" << static_cast<int>(node.kind);
        text << "\n  wires:";
        for (const auto& connection : native.connections)
            text << " " << cydaw::base::Text(static_cast<std::int64_t>(connection.source)) << ">"
                 << cydaw::base::Text(static_cast<std::int64_t>(connection.destination));
        text << "\n  out: " << cydaw::base::Text(static_cast<std::int64_t>(native.outputNodeId)) << "\n";
    }
    file.replaceWithText(text);
}

// Loads a project, saves it, loads the saved file into a fresh session and
// compares the two. Empty when they match.
cydaw::base::Text roundTrip(const cydaw::base::File& project)
{
    Studio first;
    if (const auto loaded = first.loadProject(project); loaded.failed())
        return "load: " + loaded.getErrorMessage();
    const auto before = describeSession(first);
    if (before.contains("compile: ") && !before.contains("compile: ok"))
        return "a graph does not compile:\n" + before;
    // In the temporary folder: the audio paths are written so they resolve
    // from there, as a "save as" elsewhere would.
    const auto copy = cydaw::base::File::getSpecialLocation(cydaw::base::File::tempDirectory)
                          .getChildFile(project.getFileNameWithoutExtension() + ".v2-roundtrip.cydaw");
    if (const auto saved = first.saveProject(copy); saved.failed())
        return "save: " + saved.getErrorMessage();
    Studio second;
    const auto reloaded = second.loadProject(copy);
    copy.deleteFile();
    if (reloaded.failed())
        return "reload: " + reloaded.getErrorMessage();
    const auto after = describeSession(second);
    if (before != after)
        return "sessions differ\n--- before\n" + before + "\n--- after\n" + after;
    return {};
}

// On each audio track, the first parameter a modulation can move is given a
// sine LFO at half amount: the engine's value must swing either side of the
// playhead's quarter and three-quarter second, and come back to the graph's
// value once the modulation is off. Empty when it all holds.
cydaw::base::Text modulationTest(const cydaw::base::File& project)
{
    Studio studio;
    // tick() would write an automatic copy of this throwaway session.
    studio.setAutosaveEnabled(false);
    if (const auto loaded = studio.loadProject(project); loaded.failed())
        return "load: " + loaded.getErrorMessage();
    auto& engine = studio.engine();
    auto checked = 0;
    cydaw::base::TextList report;
    for (std::size_t index = 0; index < engine.trackCount(); ++index) {
        const TrackRef track { false, index, 0 };
        const auto graph = engine.nativeDspGraph(index);
        for (const auto& target : studio.modulationTargets(track)) {
            auto base = modulation::readFromNativeGraph(graph, target.binding);
            if (!base || target.maximum <= target.minimum)
                continue;
            // A knob turned in the editors reaches the engine at once.
            if (auto* slot = studio.graphFor(track)) {
                const auto turned = target.minimum + (target.maximum - target.minimum) * 0.37;
                (void) slot->graph().editorSetParameter(target.binding, turned);
                const auto heard = modulation::readFromNativeGraph(engine.nativeDspGraph(index), target.binding);
                report.add(studio.trackName(track) + " " + target.binding + ": knob " + cydaw::base::Text(turned) + " -> engine "
                           + cydaw::base::Text(heard.value_or(-1.0e9)));
                if (!heard || std::abs(*heard - turned) > 1.0e-3 * std::max(1.0, std::abs(turned)))
                    return "a knob change does not reach the engine\n" + report.joinIntoString("\n");
                base = heard;
            }
            studio.setModulationLfo(track, { true, 1.0, 1.0, 0.0, 0 });
            studio.setModulationAssignment(track, target.binding, true, 0.5);
            const auto at = [&](const double seconds) {
                engine.setPosition(seconds);
                studio.tick();
                return modulation::readFromNativeGraph(engine.nativeDspGraph(index), target.binding).value_or(-1.0e9);
            };
            const auto high = at(0.25);
            const auto low = at(0.75);
            studio.setModulationAssignment(track, target.binding, false, 0.5);
            const auto restored = at(0.25);
            report.add(studio.trackName(track) + " " + target.binding + ": base " + cydaw::base::Text(*base) + ", +1 "
                       + cydaw::base::Text(high) + ", -1 " + cydaw::base::Text(low) + ", off " + cydaw::base::Text(restored));
            if (!(high > low))
                return "no swing\n" + report.joinIntoString("\n");
            if (std::abs(restored - *base) > 1.0e-3 * std::max(1.0, std::abs(*base)))
                return "not restored\n" + report.joinIntoString("\n");
            ++checked;
            break;
        }
    }
    if (checked == 0)
        return "no parameter a modulation can move";
    std::printf("%s\n", report.joinIntoString("\n").toRawUTF8());
    return {};
}

// The device panel's signal taps: every audio track's compiled graph runs
// in a runtime of its own on white noise, each device tapped where its
// signal enters and leaves; every tap must fill and carry sound. Empty when
// it all holds.
cydaw::base::Text displayTest(const cydaw::base::File& project)
{
    Studio studio;
    studio.setAutosaveEnabled(false);
    if (const auto loaded = studio.loadProject(project); loaded.failed())
        return "load: " + loaded.getErrorMessage();
    cydaw::base::TextList report;
    auto checked = 0;
    cydaw::base::Random random(7);
    for (const auto& track : studio.tracks()) {
        auto* slot = studio.graphFor(track);
        if (slot == nullptr)
            continue;
        const auto spans = slot->graph().editorNativeSpans();
        app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 512, 2);
        if (const auto set = runtime.setGraph(slot->graph().nativeDspGraph()); set.failed())
            return studio.trackName(track) + ": " + set.getErrorMessage();
        std::vector<app::NativeDspRuntime::TapRequest> taps;
        for (const auto& [card, span] : spans) {
            cydaw::base::ignoreUnused(card);
            for (const auto entry : span.entries)
                taps.push_back({ entry, true });
            for (const auto exit : span.exits)
                taps.push_back({ exit, false });
        }
        runtime.setTaps(taps);
        cydaw::audio::SampleBuffer<float> buffer(2, 512);
        for (auto block = 0; block < 8; ++block) {
            for (auto channel = 0; channel < 2; ++channel)
                for (auto sample = 0; sample < 512; ++sample)
                    buffer.setSample(channel, sample, random.nextFloat() * 0.5f - 0.25f);
            if (!runtime.process(buffer, 0, 512))
                return studio.trackName(track) + ": the runtime did not process";
        }
        // The cables that move the voices leave control nodes the chain
        // evaluates: each has a value once the chain has run.
        for (const auto& modulation : slot->graph().editorVoiceModulations()) {
            auto value = 0.0f;
            runtime.controlValuesNow(&modulation.controlNodeId, &value, 1);
            report.add(studio.trackName(track) + " voice modulation from " + cydaw::base::Text(static_cast<std::int64_t>(modulation.controlNodeId))
                       + " target " + cydaw::base::Text(static_cast<int>(modulation.target)) + " = " + cydaw::base::Text(value));
            if (!std::isfinite(value))
                return "a voice modulation's source is not evaluated\n" + report.joinIntoString("\n");
        }
        for (const auto& [card, span] : spans) {
            cydaw::base::Text line = studio.trackName(track) + " card " + cydaw::base::Text(static_cast<std::int64_t>(card)) + ":";
            for (const auto& tap : taps) {
                const auto isEntry = std::find(span.entries.begin(), span.entries.end(), tap.nodeId) != span.entries.end();
                const auto isExit = std::find(span.exits.begin(), span.exits.end(), tap.nodeId) != span.exits.end();
                if (!(tap.input ? isEntry : isExit))
                    continue;
                engine::AudioAnalysisFrame frame;
                if (!runtime.readTap(tap.nodeId, tap.input, frame))
                    return line + " tap " + cydaw::base::Text(static_cast<std::int64_t>(tap.nodeId)) + " not kept";
                auto energy = 0.0;
                for (std::uint32_t index = 0; index < frame.waveformCount; ++index)
                    energy += frame.waveform[index] * frame.waveform[index];
                const auto rms = frame.waveformCount > 0 ? std::sqrt(energy / frame.waveformCount) : 0.0;
                line << (tap.input ? " in " : " out ") << cydaw::base::Text(static_cast<std::int64_t>(tap.nodeId)) << " n="
                     << static_cast<int>(frame.waveformCount) << " rms=" << cydaw::base::Text(rms, 4) << " blocks="
                     << cydaw::base::Text(static_cast<std::int64_t>(frame.sequence));
                if (frame.waveformCount != engine::AudioAnalysisFrame::waveformCapacity || frame.sequence != 8)
                    return "a tap did not fill\n" + report.joinIntoString("\n") + "\n" + line;
                if (tap.input && rms <= 1.0e-4)
                    return "a device receives silence\n" + report.joinIntoString("\n") + "\n" + line;
                ++checked;
            }
            report.add(line);
        }
    }
    if (checked == 0)
        return "no device to tap";
    std::printf("%s\n", report.joinIntoString("\n").toRawUTF8());
    return {};
}

// A studio built with half a megabyte of the stack already taken: an
// exported plug-in opens its editor on a host's thread, deep in its calls.
// The factory graphs' node payloads once filled one frame past a megabyte;
// should they again, this crashes the test rather than a host.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
bool buildsWithHalfTheStack()
{
    volatile char taken[512 * 1'024];
    taken[0] = 1;
    taken[sizeof(taken) - 1] = 1;
    auto studio = std::make_unique<Studio>();
    studio->setAutosaveEnabled(false);
    return taken[0] == 1 && taken[sizeof(taken) - 1] == 1;
}

// Every effect of the catalogue: inserted on a track it compiles, its knobs
// reach the engine, and on noise it does what it says. Empty when it all
// holds.
cydaw::base::Text effectsTest()
{
    if (!buildsWithHalfTheStack())
        return "the studio is not built with half the stack";
    Studio studio;
    studio.setAutosaveEnabled(false);
    cydaw::base::TextList report;
    cydaw::base::Random random(11);
    // The channel converters change a chain's width, so they go between
    // cards rather than into a track's stereo chain: their sound is checked
    // here, one stereo block of a left-only signal through each.
    for (const auto kind : { engine::NativeDspNodeKind::monoToStereo, engine::NativeDspNodeKind::stereoToMono }) {
        engine::NativeDspNode node;
        node.id = 1;
        node.kind = kind;
        auto processor = engine::makeEffectProcessor(node);
        if (processor == nullptr)
            return "no processor for a channel converter";
        processor->prepare({ 48'000.0, 64, 2 });
        engine::AudioBuffer input(2, 64);
        engine::AudioBuffer output(2, 64);
        input.clear(64);
        output.clear(64);
        auto in = input.view(64);
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            in.channel(0)[frame] = 0.5f;
        processor->process(in, output.view(64), {});
        auto out = output.view(64);
        const auto left = out.channel(0)[10];
        const auto right = out.channel(1)[10];
        const auto expected = kind == engine::NativeDspNodeKind::monoToStereo ? 0.5f : 0.25f;
        if (std::abs(left - expected) > 1.0e-5f || std::abs(right - expected) > 1.0e-5f)
            return cydaw::base::Text(kind == engine::NativeDspNodeKind::monoToStereo ? "mono-to-stereo" : "stereo-to-mono")
                   + " gives " + cydaw::base::Text(left) + " / " + cydaw::base::Text(right);
    }
    for (const auto& effect : engine::effectCatalogue) {
        // A modulator is no device of the chain: the modulator check has it.
        if (effect.convertsChannels() || effect.midi || effect.control)
            continue;
        const auto typeKey = cydaw::base::Text(std::string(effect.typeKey));
        const auto track = studio.addAudioTrack();
        if (!track)
            return "no track for " + typeKey;
        if (const auto inserted = studio.insertDevice(*track, typeKey); inserted.failed())
            return typeKey + ": " + inserted.getErrorMessage();
        auto* slot = studio.graphFor(*track);
        if (slot->lastCompileError().isNotEmpty())
            return typeKey + " does not compile: " + slot->lastCompileError();
        auto& graph = slot->graph();
        const app::GraphDocument::EditorNode* card = nullptr;
        const auto nodes = graph.editorNodes();
        for (const auto& node : nodes)
            if (node.typeKey == typeKey)
                card = &node;
        if (card == nullptr)
            return typeKey + ": no card";
        if (card->parameters.size() != effect.parameterCount()) {
            cydaw::base::TextList bindings;
            for (const auto& parameter : card->parameters)
                bindings.add(parameter.binding);
            return typeKey + ": " + cydaw::base::Text(static_cast<int>(card->parameters.size())) + " knobs for "
                   + cydaw::base::Text(static_cast<int>(effect.parameterCount())) + " parameters: " + bindings.joinIntoString(", ");
        }
        // Each knob, moved to the middle of its range, reaches the engine.
        for (const auto& parameter : card->parameters) {
            const auto middle = (parameter.minimum + parameter.maximum) * 0.5;
            (void) graph.editorSetParameter(parameter.binding, middle);
            const auto native = studio.engine().nativeDspGraph(track->audioIndex);
            const auto* descriptor = engine::effectParameterForSuffix(
                parameter.binding.fromFirstOccurrenceOf(".", false, false).fromFirstOccurrenceOf(".", false, false).toStdString());
            if (descriptor == nullptr)
                return typeKey + ": " + parameter.binding + " is not the catalogue's";
            auto found = false;
            for (const auto& node : native.nodes)
                if (node.kind == effect.kind
                    && std::abs(engine::effectValue(node.parameters, *descriptor) - middle) < 1.0e-3 * std::max(1.0, std::abs(middle)))
                    found = true;
            if (!found)
                return typeKey + ": " + parameter.binding + " does not reach the engine";
        }
        // What was set survives a save: the card restored from the slot's
        // state has the same values (the equaliser's bands included).
        {
            GraphSlot copy(GraphSlot::Owner::audioTrack, {});
            copy.restore(slot->serialise());
            const auto before = graph.nativeDspGraph();
            const auto after = copy.graph().nativeDspGraph();
            const auto find = [&effect](const engine::NativeDspGraph& native) -> const engine::NativeDspNode* {
                for (const auto& node : native.nodes)
                    if (node.kind == effect.kind)
                        return &node;
                return nullptr;
            };
            const auto* saved = find(before);
            const auto* restored = find(after);
            if (saved == nullptr || restored == nullptr)
                return typeKey + ": not in the restored graph";
            for (const auto& parameter : effect.parameters)
                if (std::abs(engine::effectValue(saved->parameters, parameter) - engine::effectValue(restored->parameters, parameter))
                    > 1.0e-4f)
                    return typeKey + ": " + cydaw::base::Text(std::string(parameter.key)) + " is not restored";
        }
        // The sound: the engine's graph in a runtime of its own, on noise.
        auto native = graph.nativeDspGraph();
        auto* node = static_cast<engine::NativeDspNode*>(nullptr);
        for (auto& candidate : native.nodes)
            if (candidate.kind == effect.kind)
                node = &candidate;
        engine::applyEffectDefaults(effect, node->parameters);
        const auto set = [&](const char* key, const float value) {
            if (const auto* parameter = engine::effectParameter(effect, key))
                engine::setEffectValue(node->parameters, *parameter, value);
        };
        if (effect.kind == engine::NativeDspNodeKind::limiter) {
            set("gain", 12.0f);
            set("ceiling", -6.0f);
        } else if (effect.kind == engine::NativeDspNodeKind::limiterPro) {
            set("gain", 12.0f);
            set("output", -6.0f);
        } else if (effect.kind == engine::NativeDspNodeKind::bitcrusher) {
            // Two bits are steps of a half: driven up so the noise spans them.
            set("bits", 2.0f);
            set("downsample", 1.0f);
            set("drive", 12.0f);
            set("mix", 1.0f);
        } else if (effect.kind == engine::NativeDspNodeKind::gate) {
            set("threshold", -6.0f);
            set("range", -80.0f);
        }
        app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 512, 2);
        if (const auto result = runtime.setGraph(native); result.failed())
            return typeKey + ": " + result.getErrorMessage();
        cydaw::audio::SampleBuffer<float> buffer(2, 512);
        auto inputEnergy = 0.0;
        auto outputEnergy = 0.0;
        auto peak = 0.0f;
        auto offGrid = 0;
        for (auto block = 0; block < 40; ++block) {
            for (auto channel = 0; channel < 2; ++channel)
                for (auto sample = 0; sample < 512; ++sample)
                    buffer.setSample(channel, sample, random.nextFloat() * 0.5f - 0.25f);
            for (auto channel = 0; channel < 2; ++channel)
                for (auto sample = 0; sample < 512; ++sample)
                    inputEnergy += buffer.getSample(channel, sample) * buffer.getSample(channel, sample);
            (void) runtime.process(buffer, 0, 512);
            for (auto channel = 0; channel < 2; ++channel)
                for (auto sample = 0; sample < 512; ++sample) {
                    const auto value = buffer.getSample(channel, sample);
                    if (!std::isfinite(value))
                        return typeKey + ": not a number out";
                    if (block >= 8) {
                        outputEnergy += value * value;
                        peak = std::max(peak, std::abs(value));
                        if (std::abs(value * 2.0f - std::round(value * 2.0f)) > 1.0e-4f)
                            ++offGrid;
                    }
                }
        }
        const auto ratio = std::sqrt(outputEnergy / (32.0 / 40.0) / std::max(1.0e-12, inputEnergy));
        cydaw::base::Text line = typeKey + ": level " + cydaw::base::Text(ratio, 3) + ", peak " + cydaw::base::Text(peak, 3);
        report.add(line);
        if ((effect.kind == engine::NativeDspNodeKind::limiter || effect.kind == engine::NativeDspNodeKind::limiterPro)
            && peak > std::pow(10.0f, -6.0f / 20.0f) + 1.0e-4f)
            return typeKey + " lets peaks through\n" + report.joinIntoString("\n");
        if ((effect.kind == engine::NativeDspNodeKind::threeBandEq || effect.kind == engine::NativeDspNodeKind::parametricEq)
            && (ratio < 0.9 || ratio > 1.1))
            return "a flat EQ changes the level\n" + report.joinIntoString("\n");
        if (effect.kind == engine::NativeDspNodeKind::bitcrusher && offGrid > 0)
            return "two bits are more than two bits\n" + report.joinIntoString("\n");
        if (effect.kind == engine::NativeDspNodeKind::gate && ratio > 0.1)
            return "the gate stays open under its threshold\n" + report.joinIntoString("\n");
        if (effect.kind != engine::NativeDspNodeKind::gate && ratio < 0.05)
            return typeKey + " is silent\n" + report.joinIntoString("\n");
        if (effect.kind == engine::NativeDspNodeKind::spectrometer && std::abs(ratio - 1.0) > 0.02)
            return "the spectrometer changes the sound\n" + report.joinIntoString("\n");
    }
    // A ping-pong synced to a quarter note at 120 BPM echoes 500 ms later,
    // to the sample, through the runtime the tracks use.
    {
        app::NativeDspRuntime::setSessionTempo(120.0);
        engine::NativeDspGraph graph;
        engine::NativeDspNode echo { 2, engine::NativeDspNodeKind::pingPongDelay, {} };
        const auto& effect = *engine::effectForKind(engine::NativeDspNodeKind::pingPongDelay);
        engine::applyEffectDefaults(effect, echo.parameters);
        const auto set = [&](const char* key, const float value) {
            engine::setEffectValue(echo.parameters, *engine::effectParameter(effect, key), value);
        };
        set("sync", 9.0f); // 1/4
        set("time", 100.0f);
        set("mix", 1.0f);
        set("feedback", 0.0f);
        set("width", 1.0f);
        graph.nodes = { { 1, engine::NativeDspNodeKind::input, {} }, echo, { 3, engine::NativeDspNodeKind::output, {} } };
        graph.connections = { { 1, 2 }, { 2, 3 } };
        graph.outputNodeId = 3;
        app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 512, 2);
        if (const auto result = runtime.setGraph(graph); result.failed())
            return "ping-pong graph: " + result.getErrorMessage();
        cydaw::audio::SampleBuffer<float> buffer(2, 512);
        auto first = -1;
        for (auto block = 0; block < 60 && first < 0; ++block) {
            buffer.clear();
            if (block == 0) {
                buffer.setSample(0, 0, 1.0f);
                buffer.setSample(1, 0, 1.0f);
            }
            (void) runtime.process(buffer, 0, 512);
            for (auto sample = 0; sample < 512 && first < 0; ++sample)
                if (std::abs(buffer.getSample(0, sample)) > 0.1f)
                    first = block * 512 + sample;
        }
        report.add("ping-pong 1/4 at 120 BPM: first echo at sample " + cydaw::base::Text(first));
        if (std::abs(first - 24'000) > 2)
            return "a synced echo is off the beat\n" + report.joinIntoString("\n");
    }
    // The studio compressor follows its transfer curve: a steady sine 12 dB
    // over the threshold comes out 9 dB down at 4:1, makeup adds to it, and
    // with no mix the sound is untouched.
    {
        const auto& effect = *engine::effectForKind(engine::NativeDspNodeKind::compressorPro);
        const auto peakOut = [&](const float mix, const float makeup) {
            engine::NativeDspNode compressor { 2, engine::NativeDspNodeKind::compressorPro, {} };
            engine::applyEffectDefaults(effect, compressor.parameters);
            const auto set = [&](const char* key, const float value) {
                engine::setEffectValue(compressor.parameters, *engine::effectParameter(effect, key), value);
            };
            set("threshold", -18.0f);
            set("ratio", 4.0f);
            set("knee", 0.0f);
            set("attack", 1.0f);
            set("release", 200.0f);
            set("makeup", makeup);
            set("mix", mix);
            engine::NativeDspGraph graph;
            graph.nodes = { { 1, engine::NativeDspNodeKind::input, {} }, compressor, { 3, engine::NativeDspNodeKind::output, {} } };
            graph.connections = { { 1, 2 }, { 2, 3 } };
            graph.outputNodeId = 3;
            app::NativeDspRuntime runtime;
            runtime.prepare(48'000.0, 512, 2);
            if (runtime.setGraph(graph).failed())
                return 999.0;
            cydaw::audio::SampleBuffer<float> buffer(2, 512);
            auto peak = 0.0f;
            for (auto block = 0; block < 60; ++block) {
                for (auto sample = 0; sample < 512; ++sample) {
                    const auto x = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 1'000.0f
                                                   * static_cast<float>(block * 512 + sample) / 48'000.0f);
                    buffer.setSample(0, sample, x);
                    buffer.setSample(1, sample, x);
                }
                (void) runtime.process(buffer, 0, 512);
                if (block >= 40)
                    peak = std::max(peak, buffer.getMagnitude(0, 0, 512));
            }
            return 20.0 * std::log10(std::max(1.0e-9, static_cast<double>(peak)));
        };
        const auto input = 20.0 * std::log10(0.5);
        const auto compressed = peakOut(1.0f, 0.0f);
        const auto lifted = peakOut(1.0f, 6.0f);
        const auto dry = peakOut(0.0f, 0.0f);
        const auto expected = input + ((input + 18.0) / 4.0 - (input + 18.0));
        report.add("studio compressor: " + cydaw::base::Text(compressed, 2) + " dB out for " + cydaw::base::Text(expected, 2)
                   + " expected, +6 dB makeup " + cydaw::base::Text(lifted, 2) + ", dry " + cydaw::base::Text(dry, 2));
        if (std::abs(compressed - expected) > 0.75)
            return "the studio compressor misses its curve\n" + report.joinIntoString("\n");
        if (std::abs(lifted - compressed - 6.0) > 0.3)
            return "the studio compressor's makeup is wrong\n" + report.joinIntoString("\n");
        if (std::abs(dry - input) > 0.05)
            return "the studio compressor changes the sound with no mix\n" + report.joinIntoString("\n");
    }
    // Automation: the equaliser's output follows a lane from -24 dB at 0 s
    // to 0 dB at 1 s, read at each block's place on the timeline; a band
    // switched on meanwhile is heard on top of it; without the lane the
    // output goes back to its knob.
    {
        engine::NativeDspNode eq { 2, engine::NativeDspNodeKind::parametricEq, {} };
        const auto& effect = *engine::effectForKind(engine::NativeDspNodeKind::parametricEq);
        engine::applyEffectDefaults(effect, eq.parameters);
        engine::NativeDspGraph graph;
        graph.nodes = { { 1, engine::NativeDspNodeKind::input, {} }, eq, { 3, engine::NativeDspNodeKind::output, {} } };
        graph.connections = { { 1, 2 }, { 2, 3 } };
        graph.outputNodeId = 3;
        app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 512, 2);
        if (runtime.setGraph(graph).failed())
            return "automation graph does not compile";
        app::NativeDspRuntime::AutomationLane lane;
        lane.nodeId = 2;
        lane.field = &engine::NativeDspNodeParameters::effect1;
        lane.points = { { 0.0, -24.0f, 0, 0.0f }, { 1.0, 0.0f, 0, 0.0f } };
        runtime.setAutomation({ lane });
        cydaw::audio::SampleBuffer<float> buffer(2, 512);
        auto sampleClock = 0;
        // The peak in dB of a few blocks of sine played at a timeline place.
        const auto peakAt = [&](const double seconds, const int blocks) {
            auto peak = 0.0f;
            for (auto block = 0; block < blocks; ++block) {
                for (auto sample = 0; sample < 512; ++sample) {
                    const auto x = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 1'000.0f
                                                   * static_cast<float>(sampleClock++) / 48'000.0f);
                    buffer.setSample(0, sample, x);
                    buffer.setSample(1, sample, x);
                }
                (void) runtime.process(buffer, 0, 512, seconds);
                if (block >= blocks - 4)
                    peak = std::max(peak, buffer.getMagnitude(0, 0, 512));
            }
            return 20.0 * std::log10(std::max(1.0e-9, static_cast<double>(peak))) - 20.0 * std::log10(0.5);
        };
        const auto early = peakAt(0.25, 24);
        const auto late = peakAt(0.75, 24);
        auto withBand = graph;
        const auto slot = [](const engine::EqProValue value) { return static_cast<std::size_t>(engine::eqProSlot(0, value)); };
        withBand.nodes[1].parameters.eqBands[slot(engine::EqProValue::enabled)] = 1.0f;
        withBand.nodes[1].parameters.eqBands[slot(engine::EqProValue::type)] = 0.0f;
        withBand.nodes[1].parameters.eqBands[slot(engine::EqProValue::frequency)] = 1'000.0f;
        withBand.nodes[1].parameters.eqBands[slot(engine::EqProValue::gain)] = 12.0f;
        withBand.nodes[1].parameters.eqBands[slot(engine::EqProValue::q)] = 1.0f;
        if (runtime.updateParameters(withBand).failed())
            return "automation: the band could not be set";
        const auto both = peakAt(0.5, 24);
        runtime.setAutomation({});
        const auto released = peakAt(0.5, 24);
        report.add("automation: " + cydaw::base::Text(early, 2) + " dB at 0.25 s, " + cydaw::base::Text(late, 2) + " dB at 0.75 s, "
                   + cydaw::base::Text(both, 2) + " dB with a +12 dB band at 0.5 s, " + cydaw::base::Text(released, 2) + " dB without the lane");
        if (std::abs(early + 18.0) > 0.6 || std::abs(late + 6.0) > 0.6)
            return "an automated parameter does not follow its lane\n" + report.joinIntoString("\n");
        if (std::abs(both - 0.0) > 0.6)
            return "a knob turned beside an automated one does not hold\n" + report.joinIntoString("\n");
        if (std::abs(released - 12.0) > 0.6)
            return "without its lane a parameter does not go back to its knob\n" + report.joinIntoString("\n");
    }
    // A device taken out of the middle of a chain joins its neighbours: the
    // chain still reaches the output and takes the next device.
    {
        const auto track = studio.addAudioTrack();
        if (!track)
            return "no track for the chain test";
        (void) studio.insertDevice(*track, "chorus");
        (void) studio.insertDevice(*track, "phaser");
        auto* slot = studio.graphFor(*track);
        auto& graph = slot->graph();
        std::uint64_t chorus = 0;
        for (const auto& node : graph.editorNodes())
            if (node.typeKey == "chorus")
                chorus = node.id;
        if (chorus == 0 || !graph.editorRemoveFromChain(chorus))
            return "the chorus could not be taken out of its chain";
        if (slot->lastCompileError().isNotEmpty())
            return "the chain without its chorus does not compile: " + slot->lastCompileError();
        if (const auto inserted = studio.insertDevice(*track, "flanger"); inserted.failed())
            return "a chain is cut once a device is taken out: " + inserted.getErrorMessage();
        auto kinds = 0;
        for (const auto& node : graph.nativeDspGraph().nodes)
            kinds += node.kind == engine::NativeDspNodeKind::phaser || node.kind == engine::NativeDspNodeKind::flanger ? 1 : 0;
        if (kinds != 2)
            return "the chain lost a device when one was taken out";
        report.add("chain: a device taken out, its neighbours joined");
    }
    // The parametric equaliser sounds as its curve is drawn: sines through
    // each shape come out at the gain BiquadDesign.h computes for the
    // display, within a few tenths of a dB.
    {
        struct Case {
            const char* name;
            engine::EqBandType type;
            float frequency;
            float gain;
            float q;
            int slope;
        };
        const Case cases[] {
            { "bell +9 dB 1 kHz", engine::EqBandType::bell, 1'000.0f, 9.0f, 1.4f, 0 },
            { "low cut 48 dB 200 Hz", engine::EqBandType::lowCut, 200.0f, 0.0f, 0.71f, 3 },
            { "high shelf -8 dB 4 kHz", engine::EqBandType::highShelf, 4'000.0f, -8.0f, 0.71f, 0 },
            { "notch 2 kHz", engine::EqBandType::notch, 2'000.0f, 0.0f, 4.0f, 0 },
            { "high cut 24 dB 3 kHz", engine::EqBandType::highCut, 3'000.0f, 0.0f, 2.0f, 1 },
            { "tilt +6 dB 1 kHz", engine::EqBandType::tilt, 1'000.0f, 6.0f, 0.71f, 0 },
        };
        const float probes[] { 60.0f, 150.0f, 400.0f, 1'000.0f, 1'800.0f, 2'500.0f, 6'000.0f, 12'000.0f };
        const auto& effect = *engine::effectForKind(engine::NativeDspNodeKind::parametricEq);
        auto worst = 0.0;
        for (const auto& item : cases) {
            engine::NativeDspNode eq { 2, engine::NativeDspNodeKind::parametricEq, {} };
            engine::applyEffectDefaults(effect, eq.parameters);
            const auto band = [&](const engine::EqProValue which, const float value) {
                eq.parameters.eqBands[static_cast<std::size_t>(engine::eqProSlot(2, which))] = value;
            };
            band(engine::EqProValue::enabled, 1.0f);
            band(engine::EqProValue::type, static_cast<float>(item.type));
            band(engine::EqProValue::frequency, item.frequency);
            band(engine::EqProValue::gain, item.gain);
            band(engine::EqProValue::q, item.q);
            band(engine::EqProValue::slope, static_cast<float>(item.slope));
            engine::NativeDspGraph graph;
            graph.nodes = { { 1, engine::NativeDspNodeKind::input, {} }, eq, { 3, engine::NativeDspNodeKind::output, {} } };
            graph.connections = { { 1, 2 }, { 2, 3 } };
            graph.outputNodeId = 3;
            for (const auto hz : probes) {
                app::NativeDspRuntime runtime;
                runtime.prepare(48'000.0, 512, 2);
                if (const auto result = runtime.setGraph(graph); result.failed())
                    return "parametric EQ graph: " + result.getErrorMessage();
                cydaw::audio::SampleBuffer<float> buffer(2, 512);
                auto inputEnergy = 0.0;
                auto outputEnergy = 0.0;
                for (auto block = 0; block < 48; ++block) {
                    for (auto sample = 0; sample < 512; ++sample) {
                        const auto x = 0.25f * std::sin(2.0f * std::numbers::pi_v<float> * hz
                                                        * static_cast<float>(block * 512 + sample) / 48'000.0f);
                        buffer.setSample(0, sample, x);
                        buffer.setSample(1, sample, x);
                        if (block >= 24)
                            inputEnergy += x * x;
                    }
                    (void) runtime.process(buffer, 0, 512);
                    if (block >= 24)
                        for (auto sample = 0; sample < 512; ++sample)
                            outputEnergy += buffer.getSample(0, sample) * buffer.getSample(0, sample);
                }
                const auto heard = 10.0 * std::log10(std::max(1.0e-14, outputEnergy) / std::max(1.0e-14, inputEnergy));
                const auto drawn = engine::eqBandMagnitudeDb(item.type, item.frequency, item.gain, item.q, item.slope, hz, 48'000.0);
                // Deep in a cut or a notch only the direction matters.
                const auto error = drawn < -40.0 ? std::max(0.0, heard - (-40.0)) : std::abs(heard - drawn);
                worst = std::max(worst, error);
                if (error > 0.35)
                    return cydaw::base::Text("parametric EQ, ") + item.name + " at " + cydaw::base::Text(hz) + " Hz: heard "
                           + cydaw::base::Text(heard, 2) + " dB, drawn " + cydaw::base::Text(drawn, 2) + " dB\n" + report.joinIntoString("\n");
            }
        }
        report.add("parametric EQ: heard within " + cydaw::base::Text(worst, 3) + " dB of its curve");
    }
    std::printf("%s\n", report.joinIntoString("\n").toRawUTF8());
    return {};
}

// A small session for the probes: two audio tracks from the templates'
// audio, an instrument track with a clip.
void buildDemoSession(Studio& studio)
{
    const auto audio = cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile)
                           .getSiblingFile("templates")
                           .getChildFile("audio");
    cydaw::base::Array<cydaw::base::File> files;
    audio.findChildFiles(files, cydaw::base::File::findFiles, false, "*.wav");
    files.sort();
    for (auto index = 0; index < cydaw::base::minimum(3, files.size()); ++index)
        (void) studio.addAudioTrack(files[index], index * 2.0);
    if (const auto track = studio.addMidiTrack("Keys", "synth-keys")) {
        auto& engine = studio.engine();
        const auto clip = engine.addMidiClip(track->midiId, "Accords", 4.0, 8.0, false, 0xff52d1a2);
        const int chords[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };
        for (auto bar = 0; bar < 4; ++bar)
            for (const auto note : chords[bar])
                (void) engine.addMidiNote(track->midiId, clip, note, bar * 2.0, 1.75, 0.8f);
        for (auto step = 0; step < 16; ++step)
            (void) engine.addMidiNote(track->midiId, clip, 72 + (step % 4) * 2, step * 0.5, 0.25, 0.6f);
    }
}

class CyDaw2Application final {
public:
    const cydaw::base::Text getApplicationName() { return "CyDAW 2"; }
    const cydaw::base::Text getApplicationVersion() { return CYDAW_APPLICATION_VERSION; }
    bool moreThanOneInstanceAllowed() { return true; }

    void initialise(const cydaw::base::Text& commandLine)
    {
        installCrashReport();
        options_ = parseOptions(commandLine);
        if (options_.registerProjectFiles) {
            const auto registered = shell::registerCurrentProjectFiles();
            std::printf("CyDaw project files: status=%lu changed=%d default=%d\n",
                static_cast<unsigned long>(registered.error), registered.changed, registered.defaultAssigned);
            std::fflush(stdout);
            setApplicationReturnValue(registered ? 0 : 1);
            quit();
            return;
        }
        if (options_.language.isNotEmpty())
            (void) app::Localization::instance().setLanguage(options_.language);
        ui::setLanguage(app::Localization::instance().languageCode());

        // Headless tests: no window, no audio device, an exit code.
        if (options_.effectsTest) {
            const auto failure = effectsTest();
            std::printf("%s\n", failure.isEmpty() ? "effects ok" : failure.toRawUTF8());
            std::fflush(stdout);
            setApplicationReturnValue(failure.isEmpty() ? 0 : 1);
            quit();
            return;
        }
        if (options_.displayTest != cydaw::base::File {}) {
            const auto failure = displayTest(options_.displayTest);
            std::printf("%s\n", failure.isEmpty() ? "display ok" : failure.toRawUTF8());
            std::fflush(stdout);
            setApplicationReturnValue(failure.isEmpty() ? 0 : 1);
            quit();
            return;
        }
        if (options_.modulationTest != cydaw::base::File {}) {
            const auto failure = modulationTest(options_.modulationTest);
            std::printf("%s\n", failure.isEmpty() ? "modulation ok" : failure.toRawUTF8());
            std::fflush(stdout);
            setApplicationReturnValue(failure.isEmpty() ? 0 : 1);
            quit();
            return;
        }
        if (options_.roundTrip != cydaw::base::File {}) {
            const auto failure = roundTrip(options_.roundTrip);
            std::printf("%s\n", failure.isEmpty() ? "roundtrip ok" : failure.toRawUTF8());
            std::fflush(stdout);
            cydaw::base::File::getSpecialLocation(cydaw::base::File::tempDirectory)
                .getChildFile("cydaw2-roundtrip.txt")
                .replaceWithText(failure.isEmpty() ? "ok" : failure);
            setApplicationReturnValue(failure.isEmpty() ? 0 : 1);
            quit();
            return;
        }

        // Probes keep the speakers silent and leave the saved device alone.
        const auto probe = options_.snapshot != cydaw::base::File {};
        if (!options_.hidden && !options_.silent && !probe) {
            const auto registered = shell::registerCurrentProjectFiles();
            if (!registered) std::fprintf(stderr, "CyDaw project file registration failed (%lu)\n",
                static_cast<unsigned long>(registered.error));
        }
        // Automated sessions (the MCP check) leave no copy to recover.
        noRecovery_ = probe || cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_NO_RECOVERY", {}).isNotEmpty();
        // Where things are kept: the person's choices, which automated
        // sessions read (the banks) and change only with a file of their own.
        locations::load();
        locations::setSaving(!noRecovery_
                             || cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_LOCATIONS_FILE", {}).isNotEmpty());
        studio_ = std::make_unique<Studio>();
        studio_->setAutosaveEnabled(!noRecovery_);
        // The sample library and its banks, read and scanned in the
        // background; an automated session starts it when it asks for samples.
        studio_->startSamples(!noRecovery_);
        const auto audioError = studio_->startAudio(!probe);
        // The software's virtual inputs and outputs, with their effects;
        // automated sessions start from the defaults and leave the file be.
        const auto ownVirtualIo = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_VIRTUAL_IO_FILE", {}).isNotEmpty();
        if (!noRecovery_ || ownVirtualIo)
            studio_->loadVirtualIo();
        studio_->setVirtualIoSaving(!noRecovery_ || ownVirtualIo);
        if (options_.silent || probe) {
            studio_->engine().setMasterGain(-60.0f);
            // An export runs in real time through the master: the file gets
            // it, the speakers do not.
            studio_->engine().setSpeakersSilenced(true);
        }
        if (audioError.isNotEmpty())
            std::clog << "CyDAW audio: " << audioError.toStdString() << '\n';

        // Hidden: its panels stay inside it (no window of their own on a screen).
        if (options_.hidden) {
           #if defined(_WIN32)
            _putenv_s("CYDAW_SINGLE_VIEWPORT", "1");
           #else
            setenv("CYDAW_SINGLE_VIEWPORT", "1", 1);
           #endif
        }
        window_ = std::make_unique<MainWindow>(*studio_, options_, [this]{systemRequestedQuit();}, [this]{quit();});
        window_->content().ui().setLayoutSaving(!noRecovery_);

        tick_ = std::make_unique<TickTimer>(*studio_);
        tick_->startTimerHz(30);

        if (options_.project != cydaw::base::File {})
            window_->content().ui().openProjectNow(options_.project);
        else if (options_.demo)
            buildDemoSession(*studio_);
        if (options_.view > 0 || options_.track >= 0)
            window_->content().ui().showPanel(options_.view, options_.track);
        if (options_.dumpGraphs != cydaw::base::File {})
            dumpGraphs(*studio_, options_.dumpGraphs);
        if (options_.audioSettings)
            window_->content().ui().showAudioSettings();
        if (options_.detach > 0)
            window_->content().ui().detachPanel(options_.detach);
        if (options_.saveAs != cydaw::base::File {}) {
            const auto saved = studio_->saveProject(options_.saveAs);
            std::clog << "save-as " << (saved.wasOk() ? std::string("ok") : saved.getErrorMessage().toStdString()) << '\n';
        }
        if (options_.silent || probe)
            studio_->engine().setMasterGain(-60.0f);

        // The control port an agent drives CyDAW through (tools/mcp): 127.0.0.1,
        // port 47820 unless --control-port=N says otherwise, none for 0 or a probe.
        if (!probe && options_.controlPort > 0)
            remoteControl_ = std::make_unique<app::RemoteControlServer>(
                options_.controlPort,
                [this](const cydaw::base::Value& request) {
                    if (window_ == nullptr || studio_ == nullptr) {
                        auto* object = new cydaw::base::Object();
                        object->setProperty("ok", false);
                        object->setProperty("error", "no window");
                        return cydaw::base::Value(object);
                    }
                    return handleRemoteCommand(*studio_, window_->content().ui(), request);
                },
                cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_CONTROL_TOKEN", {}));

        if (probe) {
            cydaw::base::Timer::callAfterDelay(options_.snapshotDelayMs, [this] {
                if (window_ == nullptr)
                    return;
                window_->content().host().captureNextFrame(options_.snapshot, [this](const bool ok) {
                    std::clog << "snapshot " << (ok ? "written" : "failed") << '\n';
                    if (options_.quitAfterSnapshot)
                        systemRequestedQuit();
                });
            });
        }
    }

    void shutdown()
    {
        remoteControl_.reset();
        tick_.reset();
        if (studio_ != nullptr && options_.snapshot == cydaw::base::File {}) {
            studio_->saveAudioSetup();
            studio_->flushVirtualIo();
            if (!noRecovery_)
                studio_->finishRecovery();
        }
        window_.reset();
        studio_.reset();
        app::SampleStore::shutdown();
    }

    // Unsaved changes are asked about first, except for probes.
    void systemRequestedQuit()
    {
        if (window_ == nullptr || studio_ == nullptr || options_.snapshot != cydaw::base::File {}
            || (!studio_->isDirty() && !studio_->workshopAtRisk())) {
            quit();
            return;
        }
        window_->content().ui().askBeforeQuit();
    }

    void anotherInstanceStarted(const cydaw::base::Text&)
    {
        if (window_ != nullptr && !options_.hidden)
            window_->toFront(true);
    }

private:
    struct TickTimer final : cydaw::base::Timer {
        explicit TickTimer(Studio& studio) : studio_(studio) {}
        void timerCallback() override { studio_.tick(); }
        Studio& studio_;
    };

    bool quitting_{};
    int returnCode_{};
public:
    void quit(){quitting_=true;}
    void setApplicationReturnValue(int value){returnCode_=value;}
    int returnCode()const{return returnCode_;}
    bool quitting()const{return quitting_;}
    void poll(){if(window_)window_->poll();}
private:
    LaunchOptions options_;
    std::unique_ptr<Studio> studio_;
    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<TickTimer> tick_;
    std::unique_ptr<app::RemoteControlServer> remoteControl_;
    bool noRecovery_ {};
};

} // namespace cydaw::v2

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR commandLine,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    timeBeginPeriod(1);
    int result=1;
    try {
        cydaw::v2::CyDaw2Application app;
        app.initialise(cydaw::base::Text(commandLine));
        while(!app.quitting()){
            MSG message;
            for(unsigned count=0;count<128&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++count){
                if(message.message==WM_QUIT){app.quit();break;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            cydaw::base::EventLoop::dispatchPending();
            app.poll();
            MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        app.shutdown();result=app.returnCode();
    } catch(const std::exception& error){std::fprintf(stderr,"CyDaw startup failed: %s\n",error.what());}
    timeEndPeriod(1);if(SUCCEEDED(com))CoUninitialize();return result;
}
