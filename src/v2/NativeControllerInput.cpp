#include "NativeControllerInput.h"
#include "../app/KeyLatencyProbe.h"
#include <atomic>
#include <future>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <map>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace cydaw::v2 {
struct NativeControllerInput::Impl {
    Callback callback;
    std::atomic<bool> running {true}, ready {false};
    std::thread thread;
    std::atomic<std::uint64_t> probeAt {};
    int probeScan {}, probeKey {};
    std::uint64_t releaseAt {};
    bool probePressed {};
    std::map<std::string,Event> held;
    void deliver(const Event& event) {
        if(event.keyboard || event.address.starts_with("button/")) {
            const auto id=event.source+"|"+event.address;
            if(event.value>0)held[id]=event;else held.erase(id);
        }
        callback(event);
    }
    void probePoll() {
        if (!app::key_latency::enabled) return;
        const auto now=app::key_latency::now();
        if (const auto at=probeAt.load(std::memory_order_acquire); at && !probePressed && now>=at) {
            app::key_latency::window(probeKey);
            deliver({"keyboard:computer","key/"+std::string(1,char(probeKey)),1,probeScan,true,true});
            releaseAt=now+100'000'000;
            probePressed=true;
        }
        if(releaseAt && now>=releaseAt) {
            deliver({"keyboard:computer","key/"+std::string(1,char(probeKey)),0,probeScan,true,false});releaseAt=0;probePressed=false;
            probeAt.store(0,std::memory_order_release);
        }
    }
#if defined(_WIN32)
    HWND window {};
    bool focused {};
    std::string keyName(const RAWKEYBOARD& key) {
        if (key.VKey >= 'A' && key.VKey <= 'Z') return std::string(1,char(key.VKey));
        if (key.VKey >= '0' && key.VKey <= '9') return std::string(1,char(key.VKey));
        if (key.VKey >= VK_F1 && key.VKey <= VK_F24) return "F" + std::to_string(key.VKey - VK_F1 + 1);
        switch (key.VKey) {
        case VK_SPACE:return "Space"; case VK_ESCAPE:return "Escape"; case VK_RETURN:return "Enter";
        case VK_TAB:return "Tab"; case VK_BACK:return "Backspace"; case VK_LEFT:return "LeftArrow";
        case VK_RIGHT:return "RightArrow"; case VK_UP:return "UpArrow"; case VK_DOWN:return "DownArrow";
        default:return "scan-"+std::to_string(key.MakeCode);
        }
    }
    bool appFocused() const {
        DWORD pid {}; GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid == GetCurrentProcessId();
    }
    void panic() {
        // Release DAW-controller edges as well as musical notes. No missing key-up
        // may leave a toggle waiting for the next press after focus is regained.
        auto released=std::move(held);held.clear();
        for(auto& [id,event]:released){event.value=0;event.down=false;callback(event);}
        callback({"keyboard:computer","disconnect",0,0,true,false});
        callback({"mouse:computer","disconnect"});
    }
    void input(HRAWINPUT handle) {
        RAWINPUT input {}; UINT size=sizeof(input);
        if(GetRawInputData(handle,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))==UINT(-1))return;
        if(!appFocused()) { if(focused)panic(); focused=false; return; }
        focused=true;
        if(input.header.dwType==RIM_TYPEKEYBOARD) {
            const auto& key=input.data.keyboard;
            if(key.VKey==VK_ESCAPE || key.VKey==255)return;
            const bool down=(key.Flags&RI_KEY_BREAK)==0;
            if(down && ((GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000)) {
                panic();return;
            }
            const int scan=key.MakeCode | ((key.Flags&RI_KEY_E0)?0x100:0);
            deliver({"keyboard:computer","key/"+keyName(key),down?1.0:0.0,scan,true,down});
        } else if(input.header.dwType==RIM_TYPEMOUSE) {
            const auto& mouse=input.data.mouse;
            auto say=[&](const char* name,double value){deliver({"mouse:computer",name,value});};
            const USHORT downs[]={RI_MOUSE_LEFT_BUTTON_DOWN,RI_MOUSE_RIGHT_BUTTON_DOWN,RI_MOUSE_MIDDLE_BUTTON_DOWN,RI_MOUSE_BUTTON_4_DOWN,RI_MOUSE_BUTTON_5_DOWN};
            const USHORT ups[]={RI_MOUSE_LEFT_BUTTON_UP,RI_MOUSE_RIGHT_BUTTON_UP,RI_MOUSE_MIDDLE_BUTTON_UP,RI_MOUSE_BUTTON_4_UP,RI_MOUSE_BUTTON_5_UP};
            const char* names[]={"button/left","button/right","button/middle","button/x1","button/x2"};
            for(int i=0;i<5;++i){if(mouse.usButtonFlags&downs[i])say(names[i],1);if(mouse.usButtonFlags&ups[i])say(names[i],0);}
            if(mouse.lLastX || mouse.lLastY) {
                POINT at{};GetCursorPos(&at);
                const auto x=double(at.x-GetSystemMetrics(SM_XVIRTUALSCREEN))/std::max(1,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
                const auto y=double(at.y-GetSystemMetrics(SM_YVIRTUALSCREEN))/std::max(1,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
                say("axis/x",std::clamp(x,0.0,1.0));say("axis/y",std::clamp(y,0.0,1.0));
            }
            if(mouse.usButtonFlags&RI_MOUSE_WHEEL) {
                const auto ticks=SHORT(mouse.usButtonData);say("wheel",(ticks>0?1.0:127.0)/127.0);
            }
        }
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self && msg==WM_INPUT_DEVICE_CHANGE && w==GIDC_REMOVAL)self->panic();
        if(self && msg==WM_INPUT)self->input(reinterpret_cast<HRAWINPUT>(l));
        return DefWindowProcW(hwnd,msg,w,l);
    }
#endif
    explicit Impl(Callback cb):callback(std::move(cb)) {
#if defined(_WIN32)
        std::promise<void> started;auto done=started.get_future();
        thread=std::thread([this,&started] {
            const auto name=L"CyDAW-Direct-Controller-Input";
            WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=name;RegisterClassW(&cls);
            window=CreateWindowExW(0,name,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);
            RAWINPUTDEVICE devices[]={{1,6,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window},{1,2,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window}};
            ready=window && RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE));started.set_value();
            while(running.load(std::memory_order_acquire)) {
                MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(focused && !appFocused()){panic();focused=false;}
                probePoll();
            }
            panic();
            if(ready){RAWINPUTDEVICE remove[]={{1,6,RIDEV_REMOVE,nullptr},{1,2,RIDEV_REMOVE,nullptr}};RegisterRawInputDevices(remove,2,sizeof(RAWINPUTDEVICE));}
            if(window)DestroyWindow(window);ready=false;
        });
        done.wait();
#endif
    }
    ~Impl(){running=false;if(thread.joinable())thread.join();}
};
NativeControllerInput::NativeControllerInput(Callback cb):impl_(std::make_unique<Impl>(std::move(cb))){}
NativeControllerInput::~NativeControllerInput()=default;
bool NativeControllerInput::available()const noexcept{return impl_->ready.load();}
}

namespace cydaw::v2 {
bool NativeControllerInput::scheduleProbe(int delayMs,int scan,int key) {
    if(!app::key_latency::enabled || !available() || impl_->probeAt.load(std::memory_order_acquire))return false;
    // Commands are serialized on the application dispatcher; trials are spaced by >100 ms.
    impl_->probeScan=scan;impl_->probeKey=key;
    impl_->probeAt.store(app::key_latency::now()+std::uint64_t(std::clamp(delayMs,20,2000))*1'000'000,std::memory_order_release);
    return true;
}
}
