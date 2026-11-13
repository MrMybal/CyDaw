#include "NativeHostedPlugin.h"
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
#endif
namespace cydaw::plugins {
struct NativePluginEditor::Impl {
  NativeHostedPlugin &plugin;
  NativePluginEditor &editor;
  unsigned width = 640, height = 420;
  bool native{}, visible{}, resizing{};
  int scroll{};
#if defined(_WIN32)
  HWND window{};
  std::vector<HWND> labels, sliders, values;
  static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *p =
        reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      p = static_cast<Impl *>(
          reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      p->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (!p)
      return DefWindowProcW(window, message, w, l);
    if (message == WM_CLOSE) {
      p->editor.setVisible(false);
      return 0;
    }
    if (message == WM_GETMINMAXINFO) {
      auto *info = reinterpret_cast<MINMAXINFO *>(l);
      info->ptMinTrackSize = {320, 180};
      return 0;
    }
    if (message == WM_SIZE) {
      p->width = LOWORD(l);
      p->height = HIWORD(l);
      if (p->native && !p->resizing) {
        cyaudio::PluginEditorSize size{p->width, p->height};
        p->resizing = true;
        (void)p->plugin.plugin_.resizeEditor(size);
        p->resizing = false;
        if (size.width != p->width || size.height != p->height)
          p->editor.resize(size.width, size.height);
      }
      p->layout();
      return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
      if (p->native)
        (void)p->plugin.plugin_.focusEditor(message == WM_SETFOCUS);
    }
    if (message == WM_HSCROLL) {
      auto child = reinterpret_cast<HWND>(l);
      for (std::size_t i = 0; i < p->sliders.size(); ++i)
        if (p->sliders[i] == child) {
          const auto value = SendMessageW(child, TBM_GETPOS, 0, 0);
          p->plugin.parameters_[i]->setValue(float(value) / 10000);
          p->updateValues();
          return 0;
        }
    }
    if (message == WM_MOUSEWHEEL) {
      const auto delta = GET_WHEEL_DELTA_WPARAM(w);
      if (p->native)
        (void)p->plugin.plugin_.wheelEditor(float(delta) / WHEEL_DELTA);
      else {
        p->scroll = std::clamp(
            p->scroll - delta / 4, 0,
            std::max(0, int(p->sliders.size()) * 44 - int(p->height) + 16));
        p->layout();
      }
      return 0;
    }
    if (message == WM_VSCROLL && !p->native) {
      int next = p->scroll;
      switch (LOWORD(w)) {
      case SB_LINEUP:
        next -= 44;
        break;
      case SB_LINEDOWN:
        next += 44;
        break;
      case SB_PAGEUP:
        next -= int(p->height);
        break;
      case SB_PAGEDOWN:
        next += int(p->height);
        break;
      case SB_THUMBPOSITION:
      case SB_THUMBTRACK: {
        SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
        GetScrollInfo(window, SB_VERT, &info);
        next = info.nTrackPos;
        break;
      }
      }
      p->scroll = std::clamp(
          next, 0,
          std::max(0, int(p->sliders.size()) * 44 - int(p->height) + 16));
      p->layout();
      return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR) &&
        p->native) {
      cyaudio::PluginEditorKey key;
      key.down = message != WM_KEYUP;
      key.character = message == WM_CHAR ? std::uint16_t(w) : 0;
      key.modifiers =
          (GetKeyState(VK_SHIFT) < 0 ? cyaudio::editorShift : 0) |
          (GetKeyState(VK_CONTROL) < 0 ? cyaudio::editorControl : 0) |
          (GetKeyState(VK_MENU) < 0 ? cyaudio::editorAlt : 0);
      if (message != WM_CHAR) {
        switch (w) {
        case VK_RETURN:
          key.key = cyaudio::PluginEditorKeyCode::enter;
          break;
        case VK_ESCAPE:
          key.key = cyaudio::PluginEditorKeyCode::escape;
          break;
        case VK_SPACE:
          key.key = cyaudio::PluginEditorKeyCode::space;
          break;
        case VK_TAB:
          key.key = cyaudio::PluginEditorKeyCode::tab;
          break;
        case VK_BACK:
          key.key = cyaudio::PluginEditorKeyCode::backspace;
          break;
        case VK_DELETE:
          key.key = cyaudio::PluginEditorKeyCode::deleteKey;
          break;
        case VK_LEFT:
          key.key = cyaudio::PluginEditorKeyCode::left;
          break;
        case VK_RIGHT:
          key.key = cyaudio::PluginEditorKeyCode::right;
          break;
        case VK_UP:
          key.key = cyaudio::PluginEditorKeyCode::up;
          break;
        case VK_DOWN:
          key.key = cyaudio::PluginEditorKeyCode::down;
          break;
        }
      }
      if (p->plugin.plugin_.keyEditor(key) == cyaudio::Status::ok)
        return 0;
    }
    if (message == WM_TIMER) {
      p->updateValues();
      return 0;
    }
    return DefWindowProcW(window, message, w, l);
  }
  void updateValues() {
    for (std::size_t i = 0; i < sliders.size(); ++i) {
      auto *c = plugin.parameters_[i];
      const auto text = c->getCurrentValueAsText() + " " + c->getLabel();
      SetWindowTextW(values[i], text.toWideCharPointer());
      if (GetCapture() != sliders[i])
        SendMessageW(sliders[i], TBM_SETPOS, TRUE,
                     LPARAM(c->getValue() * 10000));
    }
  }
  void layout() {
    if (native)
      return;
    for (std::size_t i = 0; i < sliders.size(); ++i) {
      const int y = int(i) * 44 + 12 - scroll;
      MoveWindow(labels[i], 12, y, 200, 20, TRUE);
      MoveWindow(sliders[i], 214, y, std::max(40, int(width) - 370), 24, TRUE);
      MoveWindow(values[i], std::max(260, int(width) - 145), y, 135, 24, TRUE);
    }
    SCROLLINFO info{sizeof(info),
                    SIF_RANGE | SIF_PAGE | SIF_POS,
                    0,
                    int(sliders.size()) * 44 + 16,
                    UINT(height),
                    scroll,
                    0};
    SetScrollInfo(window, SB_VERT, &info, TRUE);
  }
#endif
  Impl(NativeHostedPlugin &p, NativePluginEditor &e) : plugin(p), editor(e) {}
};
NativePluginEditor::NativePluginEditor(NativeHostedPlugin &plugin)
    : impl_(std::make_unique<Impl>(plugin, *this)) {
#if defined(_WIN32)
  auto &p = *impl_;
  WNDCLASSEXW c{sizeof(c)};
  c.lpfnWndProc = Impl::proc;
  c.hInstance = GetModuleHandleW(nullptr);
  c.hCursor = LoadCursor(nullptr, IDC_ARROW);
  c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  c.lpszClassName = L"CyDaw.NativePluginEditor";
  RegisterClassExW(&c);
  const auto title = plugin.getName() + " — CyDaw";
  p.window = CreateWindowExW(0, c.lpszClassName, title.toWideCharPointer(),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 420, nullptr,
                             nullptr, c.hInstance, &p);
  if (!p.window)
    throw std::runtime_error("Plugin parent window creation failed");
  plugin.editor_ = this;
  cyaudio::PluginWindow parent;
  parent.handle = reinterpret_cast<std::uintptr_t>(p.window);
  cyaudio::PluginEditorSize size{640, 420};
  if (plugin.plugin_.description()->editor &&
      plugin.plugin_.openEditor(parent, size) == cyaudio::Status::ok) {
    p.native = true;
    resize(size.width, size.height);
  } else {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    SetWindowLongPtrW(p.window, GWL_STYLE,
                      GetWindowLongPtrW(p.window, GWL_STYLE) | WS_VSCROLL);
    refreshParameters();
    resize(760, 480);
    p.layout();
    SetTimer(p.window, 1, 50, nullptr);
    p.updateValues();
  }
#else
  throw std::runtime_error(
      "Native plugin editor is not implemented on this platform");
#endif
}
NativePluginEditor::~NativePluginEditor() {
  auto &p = *impl_;
#if defined(_WIN32)
  if (p.native)
    (void)p.plugin.plugin_.closeEditor();
  p.plugin.editor_ = nullptr;
  if (p.window)
    DestroyWindow(p.window);
#endif
}
void NativePluginEditor::refreshParameters(){
#if defined(_WIN32)
 auto& p=*impl_;if(p.native)return;
 for(auto window:p.sliders)DestroyWindow(window);
 for(auto window:p.labels)DestroyWindow(window);
 for(auto window:p.values)DestroyWindow(window);
 p.sliders.clear();p.labels.clear();p.values.clear();p.scroll=0;
    for (auto *parameter : p.plugin.parameters_) {
      auto name = parameter->getName(128);
      p.labels.push_back(CreateWindowExW(
          0, L"STATIC", name.toWideCharPointer(), WS_CHILD | WS_VISIBLE, 0, 0,
          1, 1, p.window, nullptr, GetModuleHandleW(nullptr), nullptr));
      auto slider =
          CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 0,
                          0, 1, 1, p.window, nullptr, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10000));
      EnableWindow(slider, !parameter->info.readOnly);
      p.sliders.push_back(slider);
      p.values.push_back(
          CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1,
                          p.window, nullptr, GetModuleHandleW(nullptr), nullptr));
    }
 p.layout();p.updateValues();
#endif
}
bool NativePluginEditor::nativeInterface() const { return impl_->native; }
int NativePluginEditor::getWidth() const { return int(impl_->width); }
int NativePluginEditor::getHeight() const { return int(impl_->height); }
bool NativePluginEditor::isVisible() const { return impl_->visible; }
void NativePluginEditor::setVisible(bool visible) {
  auto &p = *impl_;
  p.visible = visible;
#if defined(_WIN32)
  if (p.native)
    (void)p.plugin.plugin_.showEditor(visible);
  ShowWindow(p.window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
#endif
}
void NativePluginEditor::toFront(bool focus) {
#if defined(_WIN32)
  auto &p = *impl_;
  SetWindowPos(p.window, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | (focus ? 0 : SWP_NOACTIVATE));
  if (focus) {
    SetForegroundWindow(p.window);
    SetFocus(p.window);
  }
#endif
}
void NativePluginEditor::resize(unsigned width, unsigned height) {
  auto &p = *impl_;
#if defined(_WIN32)
  if (!width || !height || width > 16384 || height > 16384)
    return;
  RECT r{0, 0, LONG(width), LONG(height)};
  AdjustWindowRectEx(&r, DWORD(GetWindowLongPtrW(p.window, GWL_STYLE)), FALSE,
                     DWORD(GetWindowLongPtrW(p.window, GWL_EXSTYLE)));
  const bool previous = p.resizing;
  p.resizing = true;
  SetWindowPos(p.window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  p.resizing = previous;
#else
  p.width = width;
  p.height = height;
#endif
}
void NativePluginEditor::closeRequested() { setVisible(false); }
} // namespace cydaw::plugins
