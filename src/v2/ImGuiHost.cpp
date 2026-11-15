#include <cydaw/base/DispatchPause.h>
#include <cydaw/base/EventLoop.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#include "ImGuiHost.h"
#include "FramePacing.h"

#include "Theme.h"
#include "Widgets.h"
#include <cyui/Core.h>

#include <imgui.h>

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shellapi.h>
 #include <d3d11.h>
 #include <dxgi1_2.h>
 #include "backends/imgui_impl_dx11.h"
 #include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace cydaw::v2 {

#if defined(_WIN32)

struct ImGuiHost::Native {
    HWND hwnd {};
    ID3D11Device* device {};
    ID3D11DeviceContext* context {};
    IDXGISwapChain1* swapChain {};
    ID3D11RenderTargetView* target {};
    UINT width {};
    UINT height {};
    UINT pendingWidth {};
    UINT pendingHeight {};
    bool resizePending {};
    ImGuiContext* imgui {};
    bool backendsReady {};
    float dpi { 1.0f };
    bool rendering {};
    cydaw::base::Text adapter;
    std::map<cydaw::base::Text, ID3D11ShaderResourceView*> textures;

    void releaseTextures()
    {
        for (auto& [key, view] : textures)
            if (view != nullptr)
                view->Release();
        textures.clear();
    }

    void releaseTarget()
    {
        if (target != nullptr) {
            target->Release();
            target = nullptr;
        }
    }

    bool createTarget()
    {
        ID3D11Texture2D* back {};
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) || back == nullptr)
            return false;
        const auto created = SUCCEEDED(device->CreateRenderTargetView(back, nullptr, &target));
        back->Release();
        return created;
    }

    bool createDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained {};
        auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3,
                                        D3D11_SDK_VERSION, &device, &obtained, &context);
        if (FAILED(result))
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 3,
                                       D3D11_SDK_VERSION, &device, &obtained, &context);
        if (FAILED(result))
            return false;

        IDXGIDevice* dxgiDevice {};
        IDXGIAdapter* dxgiAdapter {};
        IDXGIFactory2* factory {};
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
            return false;
        dxgiDevice->GetAdapter(&dxgiAdapter);
        dxgiDevice->Release();
        if (dxgiAdapter == nullptr)
            return false;
        DXGI_ADAPTER_DESC description {};
        if (SUCCEEDED(dxgiAdapter->GetDesc(&description)))
            adapter = cydaw::base::Text(description.Description);
        dxgiAdapter->GetParent(IID_PPV_ARGS(&factory));
        dxgiAdapter->Release();
        if (factory == nullptr)
            return false;

        RECT client {};
        GetClientRect(hwnd, &client);
        width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
        height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_NONE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        result = factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &swapChain);
        if (FAILED(result)) {
            // Older systems: the stretch scaling the flip model may require.
            desc.Scaling = DXGI_SCALING_STRETCH;
            result = factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &swapChain);
        }
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
        if (FAILED(result) || swapChain == nullptr)
            return false;
        return createTarget();
    }

    void destroyDevice()
    {
        releaseTextures();
        releaseTarget();
        if (swapChain != nullptr) {
            swapChain->Release();
            swapChain = nullptr;
        }
        if (context != nullptr) {
            context->Release();
            context = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
    }

    void applyResize()
    {
        if (!resizePending || swapChain == nullptr)
            return;
        resizePending = false;
        if (pendingWidth == 0 || pendingHeight == 0)
            return;
        if (pendingWidth == width && pendingHeight == height)
            return;
        releaseTarget();
        if (SUCCEEDED(swapChain->ResizeBuffers(0, pendingWidth, pendingHeight, DXGI_FORMAT_UNKNOWN, 0))) {
            width = pendingWidth;
            height = pendingHeight;
        }
        createTarget();
    }
};

struct NativeAccess {
    static ImGuiHost* hostFor(HWND hwnd)
    {
        return reinterpret_cast<ImGuiHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    static ImGuiHost::Native* native(ImGuiHost& host)
    {
        return host.native_.get();
    }
    static void dropped(ImGuiHost& host, HDROP drop)
    {
        cydaw::base::TextList files;
        const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const auto length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring buffer(length + 1, L'\0');
            DragQueryFileW(drop, i, buffer.data(), length + 1);
            files.add(cydaw::base::Text(buffer.c_str()));
        }
        POINT point {};
        DragQueryPoint(drop, &point);
        DragFinish(drop);
        // ImGui works in screen coordinates once panels can leave the window.
        if (host.native_->hwnd != nullptr)
            ClientToScreen(host.native_->hwnd, &point);
        if (!files.isEmpty() && host.onFilesDropped)
            host.onFilesDropped(files, static_cast<float>(point.x), static_cast<float>(point.y));
        host.wake();
    }
};

namespace {

// The module the surface's code is in (the application, or a plug-in's DLL):
// its window class is its own, so two plug-ins of CyDAW in one host each
// draw through their own code, and one unloaded leaves the other's alone.
HMODULE thisModule()
{
    HMODULE module {};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &module);
    return module != nullptr ? module : GetModuleHandleW(nullptr);
}

const std::wstring& windowClassName()
{
    static const std::wstring name = L"CyDAW2ImGuiSurface-" + std::to_wstring(reinterpret_cast<std::uintptr_t>(thisModule()));
    return name;
}

LRESULT CALLBACK surfaceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* host = NativeAccess::hostFor(hwnd);
    auto* native = host != nullptr ? NativeAccess::native(*host) : nullptr;
    if (native != nullptr && native->imgui != nullptr && native->backendsReady) {
        ImGui::SetCurrentContext(native->imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
            return 1;
    }
    switch (message) {
        case WM_TIMER:
            cydaw::base::EventLoop::dispatchPending();
            if(host)host->poll();
            return 0;
        case WM_SIZE:
            if (native != nullptr && wParam != SIZE_MINIMIZED) {
                native->pendingWidth = LOWORD(lParam);
                native->pendingHeight = HIWORD(lParam);
                native->resizePending = true;
                if (host != nullptr)
                    host->wake();
            }
            return 0;
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_MOUSELEAVE:
            if (host != nullptr)
                host->wake();
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
            if (GetFocus() != hwnd)
                SetFocus(hwnd);
            if (host != nullptr)
                host->wake();
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            if (host != nullptr)
                host->wake();
            break;
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;
        case WM_DROPFILES:
            if (host != nullptr)
                NativeAccess::dropped(*host, reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            if (host != nullptr)
                host->wake();
            return 0;
        }
        case WM_SYSCOMMAND:
            // Alt alone must not open the system menu of the parent window.
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void registerWindowClass()
{
    static bool registered = false;
    if (registered)
        return;
    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = surfaceProc;
    windowClass.hInstance = thisModule();
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = windowClassName().c_str();
    RegisterClassExW(&windowClass);
    registered = true;
}

} // namespace

ImGuiHost::ImGuiHost() : native_(std::make_unique<Native>()) {}
void ImGuiHost::create(void* parent,int width,int height){parent_=parent;bounds_={0,0,std::max(1,width),std::max(1,height)};createNativeWindow();}
void ImGuiHost::setBounds(cydaw::base::Rectangle<int> bounds){bounds_=bounds;if(native_->hwnd)MoveWindow(native_->hwnd,bounds.getX(),bounds.getY(),bounds.getWidth(),bounds.getHeight(),TRUE);wake();}
void* ImGuiHost::nativeHandle()const noexcept{return native_->hwnd;}
void ImGuiHost::poll(){vblank(cydaw::base::Time::getMillisecondCounterHiRes()*.001);}

ImGuiHost::~ImGuiHost()
{
    if(native_->hwnd)KillTimer(native_->hwnd,1);
    if (native_->backendsReady) {
        ImGui::SetCurrentContext(native_->imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        native_->backendsReady = false;
    }
    native_->destroyDevice();
    if (native_->imgui != nullptr) {
        ImGui::DestroyContext(native_->imgui);
        native_->imgui = nullptr;
    }
    if (native_->hwnd != nullptr)
        SetWindowLongPtrW(native_->hwnd, GWLP_USERDATA, 0);
    if(native_->hwnd && IsWindow(native_->hwnd))DestroyWindow(native_->hwnd);
    native_->hwnd=nullptr;
}

void ImGuiHost::createNativeWindow()
{
    if (native_->hwnd != nullptr)
        return;
    registerWindowClass();
    // In the background, never the active window, even before it is embedded.
    native_->hwnd = CreateWindowExW(background ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0, windowClassName().c_str(), L"CyDAW",
                                    (parent_ ? WS_CHILD | WS_VISIBLE : WS_POPUP) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    bounds_.getX(), bounds_.getY(), bounds_.getWidth(), bounds_.getHeight(),
                                    static_cast<HWND>(parent_), nullptr, thisModule(), nullptr);
    if (native_->hwnd == nullptr)
        return;
    SetWindowLongPtrW(native_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DragAcceptFiles(native_->hwnd, TRUE);
    // Legacy exported test fixtures can embed this child without owning the
    // application's polling loop. Native desktop polling supplies full pacing.
    SetTimer(native_->hwnd,1,16,nullptr);

    if (!native_->createDevice()) {
        native_->destroyDevice();
        return;
    }

    IMGUI_CHECKVERSION();
    native_->imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(native_->imgui);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (ownWindows)
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // A panel dragged out of the window becomes a window of its own, on any
    // screen; dragged back over the main window, it docks again. Kept inside
    // the main window when CYDAW_SINGLE_VIEWPORT is set, so a snapshot shows
    // the device windows too (tests).
    if (ownWindows && cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_SINGLE_VIEWPORT", {}).isEmpty())
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true;
    io.ConfigViewportsNoDefaultParent = false;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = false;
    io.ConfigDragClickToInputText = true;
    // The layout is saved by the application, in its settings folder.
    io.IniFilename = nullptr;
    // A double click as Windows counts it (half a second by default, and
    // the person's own setting), not ImGui's stricter 0.3 s: a calm double
    // click otherwise reads as two clicks.
    io.MouseDoubleClickTime = std::clamp(static_cast<float>(GetDoubleClickTime()) / 1000.0f, 0.2f, 0.9f);
    io.MouseDoubleClickMaxDist = std::max(6.0f, static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)));

    native_->dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(native_->hwnd);
    theme::loadFonts();
    theme::apply(native_->dpi);

    ImGui_ImplWin32_Init(native_->hwnd);
    ImGui_ImplDX11_Init(native_->device, native_->context);
    native_->backendsReady = true;
    if (onInitialised)
        onInitialised();
    if (!background)
        SetFocus(native_->hwnd);
    wake(30);
}

bool ImGuiHost::isReady() const noexcept
{
    return native_->backendsReady;
}

float ImGuiHost::dpiScale() const noexcept
{
    return native_->dpi;
}

cydaw::base::Text ImGuiHost::rendererDescription() const
{
    return "Direct3D 11 — " + native_->adapter;
}

void ImGuiHost::wake(const int frames)
{
    if (wakeFrames_ <= 0 && !animating_ && frames > 0)
        nextFrameSeconds_ = std::min(nextFrameSeconds_,
            cydaw::base::Time::getMillisecondCounterHiRes() * 0.001 + 1.0 / frameRateHz_);
    wakeFrames_ = std::max(wakeFrames_, frames);
}

void ImGuiHost::setAnimating(const bool animating)
{
    if (animating && !animating_)
        nextFrameSeconds_ = std::min(nextFrameSeconds_,
            cydaw::base::Time::getMillisecondCounterHiRes() * 0.001 + 1.0 / frameRateHz_);
    animating_ = animating;
}

void ImGuiHost::setFrameRateLimit(const int framesPerSecond) noexcept
{
    frameRateHz_ = framesPerSecond == 30 || framesPerSecond == 120 ? framesPerSecond : 60;
    nextFrameSeconds_ = 0.0;
    wake();
}

void ImGuiHost::captureNextFrame(const cydaw::base::File& target, std::function<void(bool)> done)
{
    captureTarget_ = target;
    captureDone_ = std::move(done);
    wake();
}

void ImGuiHost::vblank(const double timestampSeconds)
{
    if (!native_->backendsReady) return;
    // Idle: a dozen frames a second keeps tooltips and slow changes alive.
    const auto idle = wakeFrames_ <= 0 && !animating_;
    if (!detail::frameDue(timestampSeconds, idle ? 12.0 : frameRateHz_, nextFrameSeconds_))
        return;
    if (wakeFrames_ > 0)
        --wakeFrames_;
    frame();
}

unsigned long long ImGuiHost::texture(const cydaw::base::Text& key, const cydaw::base::Image& image)
{
    auto& n = *native_;
    if (const auto found = n.textures.find(key); found != n.textures.end())
        return reinterpret_cast<unsigned long long>(found->second);
    if (n.device == nullptr || !image.isValid())
        return 0;
    // Straight RGBA, as the renderer blends it.
    const auto width = image.getWidth();
    const auto height = image.getHeight();
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data {};
    data.pSysMem = image.data();
    data.SysMemPitch = static_cast<UINT>(width * 4);
    ID3D11Texture2D* created {};
    if (FAILED(n.device->CreateTexture2D(&desc, &data, &created)) || created == nullptr)
        return 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc {};
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* view {};
    const auto made = SUCCEEDED(n.device->CreateShaderResourceView(created, &viewDesc, &view));
    created->Release();
    if (!made || view == nullptr)
        return 0;
    n.textures[key] = view;
    return reinterpret_cast<unsigned long long>(view);
}

// Reuse one GPU resource for a video stream instead of caching every frame.
unsigned long long ImGuiHost::updateTexture(const cydaw::base::Text& key,const cydaw::base::Image& image)
{
    if(!native_||!image.isValid())return 0;
    auto& n=*native_;
    const auto found=n.textures.find(key);
    if(found==n.textures.end())return texture(key,image);
    ID3D11Resource* resource{};found->second->GetResource(&resource);
    if(!resource)return 0;
    D3D11_TEXTURE2D_DESC desc{};static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
    if(desc.Width!=static_cast<UINT>(image.getWidth())||desc.Height!=static_cast<UINT>(image.getHeight())){
        resource->Release();releaseTexture(key);return texture(key,image);
    }
    n.context->UpdateSubresource(resource,0,nullptr,image.data(),static_cast<UINT>(image.stride()),0);
    resource->Release();return reinterpret_cast<unsigned long long>(found->second);
}
void ImGuiHost::releaseTexture(const cydaw::base::Text& key)
{
    if(!native_)return;
    if(auto it=native_->textures.find(key);it!=native_->textures.end()){it->second->Release();native_->textures.erase(it);}
}

void ImGuiHost::renderNow()
{
    if (native_->backendsReady)
        frame();
}

void ImGuiHost::frame()
{
    auto& n = *native_;
    if (n.rendering || cydaw::base::eventDispatchPaused())
        return;
    const cydaw::base::ScopedValueSetter<bool> guard(n.rendering, true);
    ImGui::SetCurrentContext(n.imgui);

    // The surface keeps the keyboard while the window is the active one.
    if (const auto root = GetAncestor(n.hwnd, GA_ROOT); !background && root != nullptr
        && GetForegroundWindow() == root && GetFocus() == root)
        SetFocus(n.hwnd);

    const auto dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(n.hwnd);
    if (std::abs(dpi - n.dpi) > 0.01f) {
        n.dpi = dpi;
        theme::apply(dpi);
    }

    // Its own scale, should another context (another plug-in's window)
    // have drawn at another.
    theme::useDpi(n.dpi);

    n.applyResize();
    if (n.target == nullptr || n.width == 0 || n.height == 0)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (beforeNewFrame)
        beforeNewFrame();
    ImGui::NewFrame();
    cyui::setLocale(ui::language().toRawUTF8());
    if (onFrame)
        onFrame();
    cyui::renderNotifications();
    ImGui::Render();

    const auto background = theme::vec(theme::palette().bg0);
    const float clear[4] = { background.x, background.y, background.z, 1.0f };
    n.context->OMSetRenderTargets(1, &n.target, nullptr);
    n.context->ClearRenderTargetView(n.target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (captureTarget_ != cydaw::base::File {}) {
        auto ok = false;
        ID3D11Texture2D* back {};
        if (SUCCEEDED(n.swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) && back != nullptr) {
            D3D11_TEXTURE2D_DESC desc {};
            back->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            ID3D11Texture2D* staging {};
            if (SUCCEEDED(n.device->CreateTexture2D(&desc, nullptr, &staging)) && staging != nullptr) {
                n.context->CopyResource(staging, back);
                D3D11_MAPPED_SUBRESOURCE mapped {};
                if (SUCCEEDED(n.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                    cydaw::base::Image image(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
                    for (UINT y = 0; y < desc.Height; ++y) {
                        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
                        for (UINT x = 0; x < desc.Width; ++x)
                            image.setPixelAt(static_cast<int>(x), static_cast<int>(y),
                                                  cydaw::base::Colour(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]));
                    }
                    n.context->Unmap(staging, 0);
                    captureTarget_.getParentDirectory().createDirectory();
                    captureTarget_.deleteFile();
                    cydaw::base::FileOutputStream stream(captureTarget_);
                    ok = stream.openedOk() && image.writePng(stream);
                }
                staging->Release();
            }
            back->Release();
        }
        captureTarget_ = cydaw::base::File {};
        if (auto done = std::exchange(captureDone_, {}))
            cydaw::base::EventLoop::post([done, ok] { done(ok); });
    }

    n.swapChain->Present(0, 0);

    // Panels in windows of their own.
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        // Their input does not reach this window's messages: while there are
        // any, the interface keeps its selected active frame rate.
        if (ImGui::GetPlatformIO().Viewports.Size > 1)
            wake(2);
    }
}

#else

struct ImGuiHost::Native {};

ImGuiHost::ImGuiHost() : native_(std::make_unique<Native>()) {}
ImGuiHost::~ImGuiHost() = default;
void ImGuiHost::createNativeWindow() {}
void ImGuiHost::create(void*,int,int) {}
void ImGuiHost::setBounds(cydaw::base::Rectangle<int>) {}
void* ImGuiHost::nativeHandle()const noexcept{return nullptr;}
void ImGuiHost::poll() {}
bool ImGuiHost::isReady() const noexcept { return false; }
float ImGuiHost::dpiScale() const noexcept { return 1.0f; }
cydaw::base::Text ImGuiHost::rendererDescription() const { return "unavailable on this platform"; }
void ImGuiHost::wake(int) {}
void ImGuiHost::setAnimating(bool animating) { animating_ = animating; }
void ImGuiHost::setFrameRateLimit(int fps) noexcept { frameRateHz_ = fps == 30 || fps == 120 ? fps : 60; }
void ImGuiHost::captureNextFrame(const cydaw::base::File&, std::function<void(bool)> done)
{
    if (done)
        done(false);
}
void ImGuiHost::vblank(double) {}
void ImGuiHost::renderNow() {}
unsigned long long ImGuiHost::texture(const cydaw::base::Text&, const cydaw::base::Image&) { return 0; }
unsigned long long ImGuiHost::updateTexture(const cydaw::base::Text&, const cydaw::base::Image&) { return 0; }
void ImGuiHost::releaseTexture(const cydaw::base::Text&) {}
void ImGuiHost::frame() {}

#endif

} // namespace cydaw::v2
