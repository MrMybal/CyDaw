#include "ImGuiHost.h"

#include "Theme.h"

#include <imgui.h>

#if JUCE_WINDOWS
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

#if JUCE_WINDOWS

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
    juce::String adapter;
    std::map<juce::String, ID3D11ShaderResourceView*> textures;

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
            adapter = juce::String(description.Description);
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
        juce::StringArray files;
        const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const auto length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring buffer(length + 1, L'\0');
            DragQueryFileW(drop, i, buffer.data(), length + 1);
            files.add(juce::String(buffer.c_str()));
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

ImGuiHost::ImGuiHost()
    : native_(std::make_unique<Native>()),
      vblank_(this, [this](const double timestamp) {
          lastVBlankMs_ = juce::Time::getMillisecondCounterHiRes();
          vblank(timestamp);
      })
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(embed_);
    fallback_.startTimerHz(30);
}

void ImGuiHost::Fallback::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - host.lastVBlankMs_ > 200.0)
        host.vblank(now * 0.001);
}

ImGuiHost::~ImGuiHost()
{
    fallback_.stopTimer();
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
    // The embed destroys the window.
    embed_.setHWND(nullptr);
}

void ImGuiHost::createNativeWindow()
{
    if (native_->hwnd != nullptr || getPeer() == nullptr)
        return;
    registerWindowClass();
    native_->hwnd = CreateWindowExW(0, windowClassName().c_str(), L"CyDAW", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    0, 0, std::max(1, getWidth()), std::max(1, getHeight()),
                                    nullptr, nullptr, thisModule(), nullptr);
    if (native_->hwnd == nullptr)
        return;
    SetWindowLongPtrW(native_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DragAcceptFiles(native_->hwnd, TRUE);
    embed_.setHWND(native_->hwnd);
    embed_.setBounds(getLocalBounds());
    embed_.updateHWNDBounds();

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
    if (ownWindows && juce::SystemStats::getEnvironmentVariable("CYDAW_SINGLE_VIEWPORT", {}).isEmpty())
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
    SetFocus(native_->hwnd);
    wake(30);
}

void ImGuiHost::resized()
{
    embed_.setBounds(getLocalBounds());
    wake();
}

void ImGuiHost::parentHierarchyChanged()
{
    if (isShowing())
        createNativeWindow();
}

void ImGuiHost::visibilityChanged()
{
    if (isShowing())
        createNativeWindow();
}

void ImGuiHost::focusGained(FocusChangeType)
{
    if (native_->hwnd != nullptr)
        SetFocus(native_->hwnd);
}

bool ImGuiHost::isReady() const noexcept
{
    return native_->backendsReady;
}

float ImGuiHost::dpiScale() const noexcept
{
    return native_->dpi;
}

juce::String ImGuiHost::rendererDescription() const
{
    return "Direct3D 11 — " + native_->adapter;
}

void ImGuiHost::wake(const int frames)
{
    wakeFrames_ = std::max(wakeFrames_, frames);
}

void ImGuiHost::setAnimating(const bool animating)
{
    animating_ = animating;
}

void ImGuiHost::captureNextFrame(const juce::File& target, std::function<void(bool)> done)
{
    captureTarget_ = target;
    captureDone_ = std::move(done);
    wake();
}

void ImGuiHost::vblank(const double timestampSeconds)
{
    if (!native_->backendsReady) {
        if (isShowing())
            createNativeWindow();
        return;
    }
    // Idle: a dozen frames a second keeps tooltips and slow changes alive.
    const auto idle = wakeFrames_ <= 0 && !animating_;
    if (idle && timestampSeconds - lastFrameSeconds_ < 1.0 / 12.0)
        return;
    lastFrameSeconds_ = timestampSeconds;
    if (wakeFrames_ > 0)
        --wakeFrames_;
    frame();
}

unsigned long long ImGuiHost::texture(const juce::String& key, const juce::Image& image)
{
    auto& n = *native_;
    if (const auto found = n.textures.find(key); found != n.textures.end())
        return reinterpret_cast<unsigned long long>(found->second);
    if (n.device == nullptr || !image.isValid())
        return 0;
    // Straight RGBA, as the renderer blends it.
    const auto width = image.getWidth();
    const auto height = image.getHeight();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto colour = image.getPixelAt(x, y);
            auto* pixel = pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            pixel[0] = colour.getRed();
            pixel[1] = colour.getGreen();
            pixel[2] = colour.getBlue();
            pixel[3] = colour.getAlpha();
        }
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
    data.pSysMem = pixels.data();
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

void ImGuiHost::renderNow()
{
    if (native_->backendsReady)
        frame();
}

void ImGuiHost::frame()
{
    auto& n = *native_;
    if (n.rendering)
        return;
    const juce::ScopedValueSetter<bool> guard(n.rendering, true);
    ImGui::SetCurrentContext(n.imgui);

    // The surface keeps the keyboard while the window is the active one.
    if (const auto root = GetAncestor(n.hwnd, GA_ROOT); root != nullptr
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
    if (onFrame)
        onFrame();
    ImGui::Render();

    const auto background = theme::vec(theme::palette().bg0);
    const float clear[4] = { background.x, background.y, background.z, 1.0f };
    n.context->OMSetRenderTargets(1, &n.target, nullptr);
    n.context->ClearRenderTargetView(n.target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (captureTarget_ != juce::File {}) {
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
                    juce::Image image(juce::Image::RGB, static_cast<int>(desc.Width), static_cast<int>(desc.Height), false);
                    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
                    for (UINT y = 0; y < desc.Height; ++y) {
                        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
                        for (UINT x = 0; x < desc.Width; ++x)
                            pixels.setPixelColour(static_cast<int>(x), static_cast<int>(y),
                                                  juce::Colour(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]));
                    }
                    n.context->Unmap(staging, 0);
                    captureTarget_.getParentDirectory().createDirectory();
                    captureTarget_.deleteFile();
                    juce::FileOutputStream stream(captureTarget_);
                    juce::PNGImageFormat png;
                    ok = stream.openedOk() && png.writeImageToStream(image, stream);
                }
                staging->Release();
            }
            back->Release();
        }
        captureTarget_ = juce::File {};
        if (auto done = std::exchange(captureDone_, {}))
            juce::MessageManager::callAsync([done, ok] { done(ok); });
    }

    n.swapChain->Present(0, 0);

    // Panels in windows of their own.
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        // Their input does not reach this window's messages: while there are
        // any, the interface runs at the display's rate.
        if (ImGui::GetPlatformIO().Viewports.Size > 1)
            wake(2);
    }
}

#else

struct ImGuiHost::Native {};

ImGuiHost::ImGuiHost() : native_(std::make_unique<Native>()) {}
ImGuiHost::~ImGuiHost() = default;
void ImGuiHost::createNativeWindow() {}
void ImGuiHost::resized() {}
void ImGuiHost::parentHierarchyChanged() {}
void ImGuiHost::visibilityChanged() {}
void ImGuiHost::focusGained(FocusChangeType) {}
bool ImGuiHost::isReady() const noexcept { return false; }
float ImGuiHost::dpiScale() const noexcept { return 1.0f; }
juce::String ImGuiHost::rendererDescription() const { return "unavailable on this platform"; }
void ImGuiHost::wake(int) {}
void ImGuiHost::setAnimating(bool) {}
void ImGuiHost::captureNextFrame(const juce::File&, std::function<void(bool)> done)
{
    if (done)
        done(false);
}
void ImGuiHost::vblank(double) {}
void ImGuiHost::renderNow() {}
unsigned long long ImGuiHost::texture(const juce::String&, const juce::Image&) { return 0; }
void ImGuiHost::frame() {}

#endif

} // namespace cydaw::v2
