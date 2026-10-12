#include "MessageThreadSampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment(lib, "dbghelp.lib")
#endif

namespace cydaw::app {

#if JUCE_WINDOWS && JUCE_64BIT

struct MessageThreadSampler::Impl final : public juce::Thread {
    Impl() : juce::Thread("cydaw-hitch-sampler")
    {
        // A real handle to the message thread: the pseudo-handle from
        // GetCurrentThread() means "whoever calls" and would suspend the
        // sampler itself.
        (void) DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                               GetCurrentProcess(), &target_, 0, FALSE,
                               DUPLICATE_SAME_ACCESS);
        lastBeat_.store(juce::Time::getMillisecondCounterHiRes());
    }

    ~Impl() override
    {
        stopThread(2'000);
        if (symbols_)
            SymCleanup(GetCurrentProcess());
        if (target_ != nullptr)
            CloseHandle(target_);
    }

    void run() override
    {
        // Symbols are loaded on this thread and only used here: dbghelp
        // is not thread-safe, and the message thread never touches it.
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME
                      | SYMOPT_LOAD_LINES);
        symbols_ = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != 0;
        auto lastSample = 0.0;
        while (!threadShouldExit()) {
            wait(2);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastBeat_.load() < stuckAfterMs_
                || now - lastSample < 6.0)
                continue;
            lastSample = now;
            std::array<DWORD64, 20> frames {};
            const auto count = capture(frames);
            if (count == 0)
                continue;
            ++samples_;
            // Named after the thread runs again; naming allocates, and the
            // thread might have been stopped inside the allocator.
            juce::String stack;
            for (std::size_t index = 0; index < count; ++index) {
                if (index > 0)
                    stack << "<";
                stack << name(frames[index]);
            }
            ++stacks_[stack];
        }
    }

    // Suspends the message thread, walks its stack with the unwinder the
    // system uses for exceptions — no allocation, no symbol lookup — and
    // resumes it. Frames are return addresses, innermost first.
    std::size_t capture(std::array<DWORD64, 20>& frames)
    {
        if (target_ == nullptr || SuspendThread(target_) == DWORD(-1))
            return 0;
        std::size_t count = 0;
        CONTEXT context {};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(target_, &context)) {
            while (count < frames.size() && context.Rip != 0) {
                frames[count++] = context.Rip;
                DWORD64 imageBase = 0;
                auto* entry = RtlLookupFunctionEntry(context.Rip, &imageBase,
                                                     nullptr);
                if (entry == nullptr) {
                    // A leaf function: the return address sits at the
                    // stack pointer.
                    DWORD64 returnAddress = 0;
                    SIZE_T read = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(),
                                           reinterpret_cast<void*>(context.Rsp),
                                           &returnAddress, sizeof(returnAddress),
                                           &read)
                        || read != sizeof(returnAddress))
                        break;
                    context.Rip = returnAddress;
                    context.Rsp += 8;
                    continue;
                }
                void* handlerData = nullptr;
                DWORD64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip,
                                 entry, &context, &handlerData,
                                 &establisherFrame, nullptr);
            }
        }
        ResumeThread(target_);
        return count;
    }

    [[nodiscard]] juce::String name(const DWORD64 address)
    {
        if (const auto known = names_.find(address); known != names_.end())
            return known->second;
        juce::String text;
        if (symbols_) {
            alignas(SYMBOL_INFO) std::array<char, sizeof(SYMBOL_INFO) + 512>
                buffer {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 511;
            DWORD64 displacement = 0;
            if (SymFromAddr(GetCurrentProcess(), address, &displacement,
                            symbol)) {
                text = juce::String(symbol->Name);
                IMAGEHLP_LINE64 line {};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;
                if (SymGetLineFromAddr64(GetCurrentProcess(), address,
                                         &lineDisplacement, &line)
                    && line.FileName != nullptr)
                    text << ":" << static_cast<int>(line.LineNumber);
            }
        }
        if (text.isEmpty()) {
            HMODULE module = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(address), &module)
                && module != nullptr) {
                std::array<wchar_t, MAX_PATH> path {};
                GetModuleFileNameW(module, path.data(),
                                   static_cast<DWORD>(path.size()));
                text = juce::File(juce::String(path.data())).getFileName()
                       + "+0x"
                       + juce::String::toHexString(
                           static_cast<juce::int64>(
                               address - reinterpret_cast<DWORD64>(module)));
            } else {
                text = "0x" + juce::String::toHexString(
                           static_cast<juce::int64>(address));
            }
        }
        // Short enough to read a dozen of in a row.
        text = text.replace("cydaw::app::", "")
                   .replace("cydaw::engine::", "")
                   .replace("juce::", "")
                   .replace("std::", "");
        if (text.length() > 56)
            text = text.substring(0, 56);
        names_.emplace(address, text);
        return text;
    }

    [[nodiscard]] juce::String describe() const
    {
        juce::String text;
        text << "stacks[samples=" << samples_;
        std::vector<std::pair<int, juce::String>> ranked;
        ranked.reserve(stacks_.size());
        for (const auto& [stack, count] : stacks_)
            ranked.emplace_back(count, stack);
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t index = 0;
             index < std::min<std::size_t>(8, ranked.size()); ++index)
            text << (index == 0 ? ": " : " | ") << ranked[index].first << "x "
                 << ranked[index].second;
        text << "]";
        return text;
    }

    HANDLE target_ {};
    bool symbols_ {};
    double stuckAfterMs_ { 12.0 };
    std::atomic<double> lastBeat_ { 0.0 };
    int samples_ {};
    std::map<juce::String, int> stacks_;
    std::map<DWORD64, juce::String> names_;
};

// The window messages that make JUCE repaint or re-layout everything,
// with the moment they arrived: a paint of the whole window in the
// middle of playback has a cause outside the app, and this names it.
namespace {
struct WindowMessageLog {
    HHOOK hook {};
    double start {};
    std::vector<std::pair<double, UINT>> messages;
    static WindowMessageLog& instance()
    {
        static WindowMessageLog log;
        return log;
    }
};

LRESULT CALLBACK windowMessageHook(const int code, const WPARAM wParam,
                                   const LPARAM lParam)
{
    auto& log = WindowMessageLog::instance();
    if (code == HC_ACTION && lParam != 0) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        switch (message->message) {
            case WM_SETTINGCHANGE:
            case WM_DISPLAYCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED:
            case WM_DWMCOMPOSITIONCHANGED:
            case WM_DWMCOLORIZATIONCOLORCHANGED:
            case WM_DWMNCRENDERINGCHANGED:
            case WM_ERASEBKGND:
            case WM_SIZE:
            case WM_ACTIVATE:
            case WM_ACTIVATEAPP:
            case WM_NCACTIVATE:
            case WM_WINDOWPOSCHANGED:
            case WM_POWERBROADCAST:
            case WM_DEVICECHANGE:
                if (log.messages.size() < 400)
                    log.messages.emplace_back(
                        juce::Time::getMillisecondCounterHiRes() - log.start,
                        message->message);
                break;
            default:
                break;
        }
    }
    return CallNextHookEx(log.hook, code, wParam, lParam);
}

juce::String messageName(const UINT message)
{
    switch (message) {
        case WM_SETTINGCHANGE: return "WM_SETTINGCHANGE";
        case WM_DISPLAYCHANGE: return "WM_DISPLAYCHANGE";
        case WM_SYSCOLORCHANGE: return "WM_SYSCOLORCHANGE";
        case WM_THEMECHANGED: return "WM_THEMECHANGED";
        case WM_DWMCOMPOSITIONCHANGED: return "WM_DWMCOMPOSITIONCHANGED";
        case WM_DWMCOLORIZATIONCOLORCHANGED: return "WM_DWMCOLORIZATIONCOLORCHANGED";
        case WM_DWMNCRENDERINGCHANGED: return "WM_DWMNCRENDERINGCHANGED";
        case WM_ERASEBKGND: return "WM_ERASEBKGND";
        case WM_SIZE: return "WM_SIZE";
        case WM_ACTIVATE: return "WM_ACTIVATE";
        case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
        case WM_NCACTIVATE: return "WM_NCACTIVATE";
        case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
        case WM_POWERBROADCAST: return "WM_POWERBROADCAST";
        case WM_DEVICECHANGE: return "WM_DEVICECHANGE";
        default: return "0x" + juce::String::toHexString(static_cast<int>(message));
    }
}
} // namespace

MessageThreadSampler::MessageThreadSampler() : impl_(std::make_unique<Impl>()) {}

MessageThreadSampler::~MessageThreadSampler() = default;

void MessageThreadSampler::beat() noexcept
{
    impl_->lastBeat_.store(juce::Time::getMillisecondCounterHiRes());
}

void MessageThreadSampler::start(const double stuckAfterMs)
{
    impl_->stuckAfterMs_ = stuckAfterMs;
    impl_->lastBeat_.store(juce::Time::getMillisecondCounterHiRes());
    auto& log = WindowMessageLog::instance();
    log.messages.clear();
    log.start = juce::Time::getMillisecondCounterHiRes();
    if (log.hook == nullptr)
        log.hook = SetWindowsHookExW(WH_CALLWNDPROC, windowMessageHook, nullptr,
                                     GetCurrentThreadId());
    impl_->startThread(juce::Thread::Priority::high);
}

juce::String MessageThreadSampler::stop()
{
    impl_->stopThread(2'000);
    auto& log = WindowMessageLog::instance();
    if (log.hook != nullptr) {
        UnhookWindowsHookEx(log.hook);
        log.hook = nullptr;
    }
    auto text = impl_->describe();
    text << " messages[";
    for (std::size_t index = 0; index < log.messages.size(); ++index)
        text << (index == 0 ? "" : " ") << messageName(log.messages[index].second)
             << "@" << juce::String(log.messages[index].first / 1000.0, 2) << "s";
    text << "]";
    return text;
}

#else

struct MessageThreadSampler::Impl {};

MessageThreadSampler::MessageThreadSampler() : impl_(std::make_unique<Impl>()) {}
MessageThreadSampler::~MessageThreadSampler() = default;
void MessageThreadSampler::beat() noexcept {}
void MessageThreadSampler::start(double) {}
juce::String MessageThreadSampler::stop() { return "stacks[unavailable]"; }

#endif

} // namespace cydaw::app
