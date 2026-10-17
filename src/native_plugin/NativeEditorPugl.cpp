#include "NativeEditorContent.h"
#include <pugl/pugl.h>
#include <pugl/gl.h>
#include <GL/gl.h>
#include <backends/imgui_impl_opengl3.h>
#include <cfloat>
namespace cydaw::native {
class PuglEditor final : public EditorSurface, private EditorContent {
    PuglWorld* world_{};
    PuglView* view_{};
    bool renderer_{};
    bool visible_{};
    int width_{}, height_{};
    double previous_{};
    static void modifiers(PuglMods mods) {
        auto& io=ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl,mods&PUGL_MOD_CTRL);io.AddKeyEvent(ImGuiMod_Shift,mods&PUGL_MOD_SHIFT);
        io.AddKeyEvent(ImGuiMod_Alt,mods&PUGL_MOD_ALT);io.AddKeyEvent(ImGuiMod_Super,mods&PUGL_MOD_SUPER);
    }
    static ImGuiKey key(uint32_t k) {
        if(k>='a'&&k<='z') return ImGuiKey(ImGuiKey_A+k-'a');
        if(k>='0'&&k<='9') return ImGuiKey(ImGuiKey_0+k-'0');
        if(k>=PUGL_KEY_F1&&k<=PUGL_KEY_F12) return ImGuiKey(ImGuiKey_F1+k-PUGL_KEY_F1);
        switch(k) {
        case PUGL_KEY_TAB:return ImGuiKey_Tab; case PUGL_KEY_ENTER:return ImGuiKey_Enter;
        case PUGL_KEY_ESCAPE:return ImGuiKey_Escape; case PUGL_KEY_BACKSPACE:return ImGuiKey_Backspace;
        case PUGL_KEY_DELETE:return ImGuiKey_Delete; case PUGL_KEY_SPACE:return ImGuiKey_Space;
        case PUGL_KEY_LEFT:return ImGuiKey_LeftArrow; case PUGL_KEY_RIGHT:return ImGuiKey_RightArrow;
        case PUGL_KEY_UP:return ImGuiKey_UpArrow; case PUGL_KEY_DOWN:return ImGuiKey_DownArrow;
        case PUGL_KEY_HOME:return ImGuiKey_Home; case PUGL_KEY_END:return ImGuiKey_End;
        case PUGL_KEY_PAGE_UP:return ImGuiKey_PageUp; case PUGL_KEY_PAGE_DOWN:return ImGuiKey_PageDown;
        case PUGL_KEY_SHIFT_L:return ImGuiKey_LeftShift; case PUGL_KEY_SHIFT_R:return ImGuiKey_RightShift;
        case PUGL_KEY_CTRL_L:return ImGuiKey_LeftCtrl; case PUGL_KEY_CTRL_R:return ImGuiKey_RightCtrl;
        case PUGL_KEY_ALT_L:return ImGuiKey_LeftAlt; case PUGL_KEY_ALT_R:return ImGuiKey_RightAlt;
        case PUGL_KEY_SUPER_L:return ImGuiKey_LeftSuper; case PUGL_KEY_SUPER_R:return ImGuiKey_RightSuper;
        default:return ImGuiKey_None;
        }
    }
    static PuglStatus event(PuglView* view, const PuglEvent* e) {
        auto& self=*static_cast<PuglEditor*>(puglGetHandle(view));
        auto* kept=ImGui::GetCurrentContext();
        if(e->type==PUGL_REALIZE) {
            self.context_=ImGui::CreateContext(); ImGui::SetCurrentContext(self.context_);
            auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.BackendPlatformName="cydaw_pugl";
            theme::loadFonts(); theme::apply(1); self.renderer_=ImGui_ImplOpenGL3_Init("#version 130");
        }
        if(self.context_) {
            ImGui::SetCurrentContext(self.context_); auto& io=ImGui::GetIO();
            switch(e->type) {
            case PUGL_UNREALIZE:
                if(self.renderer_) ImGui_ImplOpenGL3_Shutdown(); self.renderer_=false; break;
            case PUGL_CONFIGURE:self.width_=e->configure.width; self.height_=e->configure.height;break;
            case PUGL_FOCUS_IN: case PUGL_FOCUS_OUT:io.AddFocusEvent(e->type==PUGL_FOCUS_IN);break;
            case PUGL_POINTER_OUT:io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);break;
            case PUGL_MOTION:modifiers(e->motion.state);io.AddMousePosEvent(float(e->motion.x),float(e->motion.y));break;
            case PUGL_BUTTON_PRESS: case PUGL_BUTTON_RELEASE:
                modifiers(e->button.state);puglGrabFocus(view);
                io.AddMousePosEvent(float(e->button.x),float(e->button.y));
                if(e->button.button<5)io.AddMouseButtonEvent(int(e->button.button),e->type==PUGL_BUTTON_PRESS);break;
            case PUGL_SCROLL:modifiers(e->scroll.state);io.AddMouseWheelEvent(float(e->scroll.dx),float(e->scroll.dy));break;
            case PUGL_KEY_PRESS:case PUGL_KEY_RELEASE: {
                const auto k=key(e->key.key);if(k!=ImGuiKey_None)io.AddKeyEvent(k,e->type==PUGL_KEY_PRESS);
                auto mods=e->key.state;
                const auto mask=(k==ImGuiKey_LeftCtrl||k==ImGuiKey_RightCtrl)?PUGL_MOD_CTRL:
                    (k==ImGuiKey_LeftShift||k==ImGuiKey_RightShift)?PUGL_MOD_SHIFT:
                    (k==ImGuiKey_LeftAlt||k==ImGuiKey_RightAlt)?PUGL_MOD_ALT:
                    (k==ImGuiKey_LeftSuper||k==ImGuiKey_RightSuper)?PUGL_MOD_SUPER:0;
                if(e->type==PUGL_KEY_PRESS)mods|=mask;else mods&=~PuglMods(mask);
                modifiers(mods);break;
            }
            case PUGL_TEXT:io.AddInputCharactersUTF8(e->text.string);break;
            case PUGL_EXPOSE:
                if(self.renderer_) {
                    const auto now=puglGetTime(self.world_);
                    io.DeltaTime=float(self.previous_>0?std::clamp(now-self.previous_,.001,.1):1./30); self.previous_=now;
                    io.DisplaySize=ImVec2(float(self.width_),float(self.height_));
                    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();self.drawContent();ImGui::Render();
                    glViewport(0,0,self.width_,self.height_);glClearColor(.1f,.1f,.1f,1);glClear(GL_COLOR_BUFFER_BIT);
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                }break;
            default:break;
            }
        }
        ImGui::SetCurrentContext(kept);return PUGL_SUCCESS;
    }
public:
    explicit PuglEditor(EditorModel& model):EditorContent(model){}
    ~PuglEditor() override {
        if(view_)puglFreeView(view_);
        if(context_) {auto* kept=ImGui::GetCurrentContext();ImGui::DestroyContext(context_);ImGui::SetCurrentContext(kept==context_?nullptr:kept);}
        if(world_)puglFreeWorld(world_);
    }
    bool attach(void* parent) override {
        if(!parent||view_)return false;
        world_=puglNewWorld(PUGL_MODULE,0);if(!world_)return false;
        view_=puglNewView(world_);if(!view_)return false;
        puglSetHandle(view_,this);puglSetEventFunc(view_,event);puglSetBackend(view_,puglGlBackend());
        puglSetParent(view_,PuglNativeView(reinterpret_cast<uintptr_t>(parent)));
        width_=plugin_.definition().width;height_=plugin_.definition().height;
        puglSetSizeHint(view_,PUGL_DEFAULT_SIZE,PuglSpan(width_),PuglSpan(height_));
        puglSetViewHint(view_,PUGL_CONTEXT_API,PUGL_OPENGL_API);puglSetViewHint(view_,PUGL_CONTEXT_VERSION_MAJOR,3);
        puglSetViewHint(view_,PUGL_CONTEXT_VERSION_MINOR,0);puglSetViewHint(view_,PUGL_RESIZABLE,PUGL_FALSE);
        if(puglRealize(view_)!=PUGL_SUCCESS||!renderer_)return false;
        setVisible(true);idle();return true;
    }
    void* handle()const override{return view_?reinterpret_cast<void*>(uintptr_t(puglGetNativeView(view_))):nullptr;}
    void setVisible(bool show)override {visible_=show;if(view_) {if(show)puglShow(view_,PUGL_SHOW_PASSIVE);else puglHide(view_);}}
    void idle()override {if(world_&&view_){if(visible_)puglObscureView(view_);puglUpdate(world_,0);}}
};
std::unique_ptr<EditorSurface> makeEditor(EditorModel& model){return std::make_unique<PuglEditor>(model);}
}
