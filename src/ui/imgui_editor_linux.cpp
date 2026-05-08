// Linux VST3 editor backend: X11 + GLX + Dear ImGui (OpenGL3)

#include "ui/imgui_editor.h"

#include "plugin/controller.h"
#include "plugin/version.h"
#include "ui/imgui_style.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <atomic>
#include <cfloat>
#include <chrono>
#include <mutex>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

static void ensureX11Threads()
{
    static std::once_flag flag;
    std::call_once(flag, [] { XInitThreads(); });
}

constexpr int kEditorWidth = 900;
constexpr int kEditorHeight = 600;

class X11Renderer
{
public:
    X11Renderer(HCPlugin::ImGuiEditor* editor, EditController* controller)
    : editor_(editor), controller_(controller) {}

    ~X11Renderer() { shutdown(); }

    bool attach(unsigned long parentWindow, int width, int height)
    {
        display_ = XOpenDisplay(nullptr);
        if (!display_)
            return false;

        const int screen = DefaultScreen(display_);

        static const int kGlxAttribs[] = {
            GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

        XVisualInfo* vi = glXChooseVisual(display_, screen, const_cast<int*>(kGlxAttribs));
        if (!vi)
        {
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }

        Colormap cmap = XCreateColormap(display_, parentWindow, vi->visual, AllocNone);

        XSetWindowAttributes swa {};
        swa.colormap = cmap;
        swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                       | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                       | EnterWindowMask | LeaveWindowMask | StructureNotifyMask
                       | FocusChangeMask;
        swa.background_pixel = 0;

        window_ = XCreateWindow(
            display_, parentWindow, 0, 0,
            static_cast<unsigned>(width), static_cast<unsigned>(height),
            0, vi->depth, InputOutput, vi->visual,
            CWColormap | CWEventMask | CWBackPixel, &swa);

        XFreeColormap(display_, cmap);

        if (!window_)
        {
            XFree(vi);
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }

        XMapWindow(display_, window_);
        XFlush(display_);

        glxContext_ = glXCreateContext(display_, vi, nullptr, GL_TRUE);
        if (!glxContext_)
            glxContext_ = glXCreateContext(display_, vi, nullptr, GL_FALSE);

        XFree(vi);

        if (!glxContext_)
        {
            XDestroyWindow(display_, window_);
            window_ = 0;
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }

        width_ = width;
        height_ = height;

        running_ = true;
        renderThread_ = std::thread([this] { renderLoop(); });
        return true;
    }

    void shutdown()
    {
        if (running_.exchange(false) && renderThread_.joinable())
            renderThread_.join();

        if (display_ && glxContext_)
        {
            glXDestroyContext(display_, glxContext_);
            glxContext_ = nullptr;
        }
        if (display_ && window_)
        {
            XDestroyWindow(display_, window_);
            window_ = 0;
        }
        if (display_)
        {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }

    void resize(int width, int height)
    {
        width_ = width;
        height_ = height;
    }

private:
    void renderLoop()
    {
        if (!glXMakeCurrent(display_, window_, glxContext_))
            return;

        IMGUI_CHECKVERSION();
        imguiContext_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(imguiContext_);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // Disable imgui.ini saving

        HCPlugin::StyleManager::setupStyle();

        ImGui_ImplOpenGL3_Init("#version 130");

        using Clock = std::chrono::steady_clock;
        auto lastTime = Clock::now();

        while (running_.load(std::memory_order_relaxed))
        {
            while (XPending(display_) > 0)
            {
                XEvent event;
                XNextEvent(display_, &event);
                handleX11Event(event);
            }

            const auto now = Clock::now();
            const float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            const int w = width_.load(std::memory_order_relaxed);
            const int h = height_.load(std::memory_order_relaxed);

            io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
            io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);

            // Update engine reference
            auto* hcController = static_cast<HCPlugin::Controller*>(controller_);
            if (hcController) {
                auto* engine = hcController->getEngine();
                if (engine) {
                    editor_->setEngine(engine);
                }
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();

            class Vst3ParameterAccess final : public HCPlugin::EditorParameterAccess
            {
            public:
                explicit Vst3ParameterAccess(EditController* controller)
                : controller_(controller) {}
            private:
                EditController* controller_ {nullptr};
            };

            Vst3ParameterAccess params{controller_};
            editor_->editorCore_.draw(params, static_cast<float>(w), static_cast<float>(h), FULL_VERSION_STR);

            ImGui::Render();

            glViewport(0, 0, w, h);
            glClearColor(0.075f, 0.075f, 0.082f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glXSwapBuffers(display_, window_);

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;

        glXMakeCurrent(display_, None, nullptr);
    }

    void handleX11Event(XEvent& event)
    {
        if (!imguiContext_)
            return;

        ImGui::SetCurrentContext(imguiContext_);
        ImGuiIO& io = ImGui::GetIO();

        switch (event.type)
        {
            case MotionNotify:
                io.AddMousePosEvent(
                    static_cast<float>(event.xmotion.x),
                    static_cast<float>(event.xmotion.y));
                break;

            case ButtonPress:
            case ButtonRelease:
            {
                const bool down = (event.type == ButtonPress);
                switch (event.xbutton.button)
                {
                    case Button1: io.AddMouseButtonEvent(0, down); break;
                    case Button2: io.AddMouseButtonEvent(2, down); break;
                    case Button3: io.AddMouseButtonEvent(1, down); break;
                    case Button4: if (down) io.AddMouseWheelEvent(0.0f, 1.0f); break;
                    case Button5: if (down) io.AddMouseWheelEvent(0.0f, -1.0f); break;
                    default: break;
                }
                break;
            }

            case KeyPress:
            case KeyRelease:
            {
                const bool down = (event.type == KeyPress);
                const unsigned int mods = event.xkey.state;
                io.AddKeyEvent(ImGuiMod_Ctrl, (mods & ControlMask) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (mods & ShiftMask) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (mods & Mod1Mask) != 0);
                io.AddKeyEvent(ImGuiMod_Super, (mods & Mod4Mask) != 0);

                const KeySym ks = XLookupKeysym(&event.xkey, 0);
                if (ks >= XK_space && ks <= XK_asciitilde)
                {
                    const ImGuiKey key = static_cast<ImGuiKey>(ks - XK_space + ImGuiKey_Space);
                    if (key >= ImGuiKey_Space && key <= ImGuiKey_KeypadEqual)
                        io.AddKeyEvent(key, down);
                }

                if (down)
                {
                    char buf[32] = {};
                    const int len = XLookupString(
                        &event.xkey, buf, static_cast<int>(sizeof(buf)) - 1, nullptr, nullptr);
                    for (int i = 0; i < len; ++i)
                    {
                        const auto ch = static_cast<unsigned char>(buf[i]);
                        if (ch >= 32)
                            io.AddInputCharacter(ch);
                    }
                }
                break;
            }

            case EnterNotify:
                io.AddMousePosEvent(
                    static_cast<float>(event.xcrossing.x),
                    static_cast<float>(event.xcrossing.y));
                break;

            case LeaveNotify:
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                break;

            case FocusIn: io.AddFocusEvent(true); break;
            case FocusOut: io.AddFocusEvent(false); break;

            case ConfigureNotify:
                width_ = event.xconfigure.width;
                height_ = event.xconfigure.height;
                break;

            default:
                break;
        }
    }

    HCPlugin::ImGuiEditor* editor_ {nullptr};
    EditController* controller_ {nullptr};
    Display* display_ {nullptr};
    Window window_ {0};
    GLXContext glxContext_ {nullptr};
    ImGuiContext* imguiContext_ {nullptr};
    std::atomic<int> width_ {kEditorWidth};
    std::atomic<int> height_ {kEditorHeight};
    std::atomic<bool> running_ {false};
    std::thread renderThread_;
};

} // namespace

namespace HCPlugin {

ImGuiEditor::ImGuiEditor(EditController* controller) : controller_(controller)
{
    ViewRect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = kEditorWidth;
    rect.bottom = kEditorHeight;
    setRect(rect);
}

ImGuiEditor::~ImGuiEditor()
{
    if (rendererHandle_)
    {
        delete static_cast<X11Renderer*>(rendererHandle_);
        rendererHandle_ = nullptr;
    }
}

tresult PLUGIN_API ImGuiEditor::isPlatformTypeSupported(FIDString type)
{
    return FIDStringsEqual(type, kPlatformTypeX11EmbedWindowID) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ImGuiEditor::attached(void* parent, FIDString type)
{
    if (!parent || !FIDStringsEqual(type, kPlatformTypeX11EmbedWindowID) || systemWindow)
        return kResultFalse;

    ensureX11Threads();

    const auto parentWin = reinterpret_cast<unsigned long>(parent);
    auto* renderer = new X11Renderer(this, controller_);
    if (!renderer->attach(parentWin, getRect().getWidth(), getRect().getHeight()))
    {
        delete renderer;
        return kResultFalse;
    }

    systemWindow = parent;
    rendererHandle_ = renderer;
    attachedToParent();
    return kResultOk;
}

tresult PLUGIN_API ImGuiEditor::removed()
{
    if (rendererHandle_)
    {
        delete static_cast<X11Renderer*>(rendererHandle_);
        rendererHandle_ = nullptr;
    }

    systemWindow = nullptr;
    removedFromParent();
    return kResultOk;
}

tresult PLUGIN_API ImGuiEditor::onSize(ViewRect* newSize)
{
    if (!newSize)
        return kResultFalse;

    setRect(*newSize);
    if (rendererHandle_)
        static_cast<X11Renderer*>(rendererHandle_)->resize(newSize->getWidth(), newSize->getHeight());
    return kResultOk;
}

void ImGuiEditor::syncSavedState()
{
    // Cast controller to our Controller class to access saved state
    if (auto* hcController = dynamic_cast<Controller*>(controller_)) {
        std::string savedCode = hcController->getSavedCode();
        if (!savedCode.empty()) {
            editorCore_.codeText() = savedCode;
        }
    }
}

} // namespace HCPlugin
