#include "NativeEditorContent.h"
#include "NativeProcessor.h"

#include "DeviceVisuals.h"
#include "Theme.h"
#include "Widgets.h"
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <d3d11.h>
#include <imgui.h>
#include <set>
#include <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace cydaw::native
{

namespace visual = cydaw::v2::visual;
namespace instrument_ui = cydaw::v2::instrument_ui;
namespace ui = cydaw::v2::ui;
namespace theme = cydaw::v2::theme;
class Win32Editor final : public EditorSurface, private EditorContent
{
    HWND hwnd_{};
    ID3D11Device *device_{};
    ID3D11DeviceContext *gpu_{};
    IDXGISwapChain *swap_{};
    ID3D11RenderTargetView *target_{};
    static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto *self = reinterpret_cast<Win32Editor *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            self = static_cast<Win32Editor *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && self->context_)
        {
            if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN)SetFocus(h);
            auto *kept = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(self->context_);
            const auto handled = ImGui_ImplWin32_WndProcHandler(h, m, w, l);
            if (m == WM_TIMER)
            {
                if (IsWindowVisible(h))
                    self->frame();
                ImGui::SetCurrentContext(kept);
                return 0;
            }
            ImGui::SetCurrentContext(kept);
            if (handled)
                return handled;
        }
        return DefWindowProcW(h, m, w, l);
    }
    void frame()
    {
        if (!target_ || !swap_)
            return;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawContent();
        ImGui::Render();
        const float clear[]{.1f, .1f, .1f, 1};
        gpu_->OMSetRenderTargets(1, &target_, nullptr);
        gpu_->ClearRenderTargetView(target_, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_->Present(0, 0);
    }
    void cleanup()
    {
        if (hwnd_)
            KillTimer(hwnd_, 1);
        if (context_)
        {
            auto *kept = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context_);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(context_);
            ImGui::SetCurrentContext(kept == context_ ? nullptr : kept);
            context_ = nullptr;
        }
        if (target_)
        {
            target_->Release();
            target_ = nullptr;
        }
        if (swap_)
        {
            swap_->Release();
            swap_ = nullptr;
        }
        if (gpu_)
        {
            gpu_->Release();
            gpu_ = nullptr;
        }
        if (device_)
        {
            device_->Release();
            device_ = nullptr;
        }
        if (hwnd_)
        {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

  public:
    explicit Win32Editor(EditorModel &p) : EditorContent(p) {}
    ~Win32Editor() override { cleanup(); }
    void *handle() const override { return hwnd_; }
    void setVisible(bool show) override
    {
        if (hwnd_)
            ShowWindow(hwnd_, show ? SW_SHOWNA : SW_HIDE);
    }
    bool attach(void *parent) override
    {
        if (!parent || hwnd_)
            return false;
        HMODULE module{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&wndProc), &module);
        WNDCLASSW wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = module;
        wc.lpszClassName = L"CyDAWNativePlugin";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                plugin_.definition().width, plugin_.definition().height,
                                static_cast<HWND>(parent), nullptr, module, this);
        if (!hwnd_)
            return false;
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd_;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                 D3D11_SDK_VERSION, &desc, &swap_, &device_, nullptr, &gpu_)))
        {
            cleanup();
            return false;
        }
        ID3D11Texture2D *back{};
        swap_->GetBuffer(0, IID_PPV_ARGS(&back));
        if (back)
        {
            device_->CreateRenderTargetView(back, nullptr, &target_);
            back->Release();
        }
        if (!target_)
        {
            cleanup();
            return false;
        }
        auto *kept = ImGui::GetCurrentContext();
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().IniFilename = nullptr;
        theme::loadFonts();
        theme::apply(1);
        ImGui_ImplWin32_Init(hwnd_);
        ImGui_ImplDX11_Init(device_, gpu_);
        frame();
        ImGui::SetCurrentContext(kept);
        SetTimer(hwnd_, 1, 33, nullptr);
        return true;
    }
};
std::unique_ptr<EditorSurface> makeEditor(EditorModel &model) { return std::make_unique<Win32Editor>(model); }
} // namespace cydaw::native
