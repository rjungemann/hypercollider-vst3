#include "ui/imgui_editor.h"

#include "plugin/controller.h"
#include "plugin/version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "ui/imgui_style.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr auto kEditorWidth = 900;
constexpr auto kEditorHeight = 600;
constexpr auto kWindowClassName = L"HCPluginEditorWindow";
constexpr UINT_PTR kRenderTimerId = 1;

class Win32Renderer
{
public:
    Win32Renderer(HCPlugin::ImGuiEditor* editor, EditController* controller)
    : editor_(editor), controller_(controller) {}

    ~Win32Renderer() { shutdown(); }

    bool attach(HWND parentWindow, int width, int height)
    {
        parentWindow_ = parentWindow;

        if (!registerWindowClass())
            return false;

        window_ = CreateWindowExW(
            0, kWindowClassName, L"HCPlugin",
            WS_CHILD | WS_VISIBLE, 0, 0, width, height,
            parentWindow_, nullptr, GetModuleHandleW(nullptr), this);

        if (!window_)
            return false;

        if (!createDeviceD3D())
        {
            shutdown();
            return false;
        }

        IMGUI_CHECKVERSION();
        imguiContext_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(imguiContext_);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // Disable imgui.ini saving

        HCPlugin::StyleManager::setupStyle();

        const auto dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(dpiScale);
        style.FontScaleDpi = dpiScale;

        if (!ImGui_ImplWin32_Init(window_) || !ImGui_ImplDX11_Init(device_, deviceContext_))
        {
            shutdown();
            return false;
        }

        SetTimer(window_, kRenderTimerId, 16, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
        return true;
    }

    void shutdown()
    {
        if (window_)
            KillTimer(window_, kRenderTimerId);

        if (imguiContext_)
        {
            ImGui::SetCurrentContext(imguiContext_);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(imguiContext_);
            imguiContext_ = nullptr;
        }

        cleanupDeviceD3D();

        if (window_)
        {
            SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool resize(int width, int height)
    {
        if (!window_)
            return false;

        SetWindowPos(window_, nullptr, 0, 0, width, height,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        return resizeSwapChain(width, height);
    }

    HWND windowHandle() const { return window_; }

    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* r = static_cast<Win32Renderer*>(cs->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(r));
        }

        auto* renderer = reinterpret_cast<Win32Renderer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!renderer)
            return DefWindowProcW(window, message, wParam, lParam);

        return renderer->handleMessage(window, message, wParam, lParam);
    }

private:
    bool registerWindowClass()
    {
        static bool registered = false;
        if (registered)
            return true;

        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClassName;

        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        return registered;
    }

    bool createDeviceD3D()
    {
        DXGI_SWAP_CHAIN_DESC desc {};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator = 60;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window_;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        auto result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION, &desc, &swapChain_, &device_, &fl, &deviceContext_);

        if (result == DXGI_ERROR_UNSUPPORTED)
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, static_cast<UINT>(std::size(featureLevels)),
                D3D11_SDK_VERSION, &desc, &swapChain_,
                &device_, &fl, &deviceContext_);

        if (FAILED(result))
            return false;

        createRenderTarget();
        return renderTargetView_ != nullptr;
    }

    void cleanupDeviceD3D()
    {
        cleanupRenderTarget();
        if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
        if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
        if (device_) { device_->Release(); device_ = nullptr; }
    }

    void createRenderTarget()
    {
        if (!swapChain_ || !device_)
            return;

        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            device_->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView_);
            backBuffer->Release();
        }
    }

    void cleanupRenderTarget()
    {
        if (renderTargetView_)
        {
            renderTargetView_->Release();
            renderTargetView_ = nullptr;
        }
    }

    bool resizeSwapChain(int width, int height)
    {
        if (!swapChain_ || width <= 0 || height <= 0)
            return true;

        cleanupRenderTarget();
        const auto result = swapChain_->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height),
            DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result))
            return false;

        createRenderTarget();
        return renderTargetView_ != nullptr;
    }

    void renderFrame()
    {
        if (!window_ || !imguiContext_ || !editor_ || !controller_ ||
            !deviceContext_ || !renderTargetView_)
            return;

        RECT rc {};
        if (!GetClientRect(window_, &rc))
            return;

        // Update engine reference
        auto* hcController = static_cast<HCPlugin::Controller*>(controller_);
        if (hcController) {
            auto* engine = hcController->getEngine();
            if (engine) {
                editor_->setEngine(engine);
            }
        }

        ImGui::SetCurrentContext(imguiContext_);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        class Vst3ParameterAccess final : public HCPlugin::EditorParameterAccess
        {
        public:
            explicit Vst3ParameterAccess(EditController* controller)
            : controller_(controller) {}
        private:
            EditController* controller_ {nullptr};
        };

        Vst3ParameterAccess parameterAccess{controller_};
        editor_->editorCore_.draw(parameterAccess,
                      static_cast<float>(rc.right - rc.left),
                      static_cast<float>(rc.bottom - rc.top),
                      FULL_VERSION_STR);

        ImGui::Render();

        constexpr float kClearColor[4] = {0.075f, 0.075f, 0.082f, 1.0f};
        deviceContext_->OMSetRenderTargets(1, &renderTargetView_, nullptr);
        deviceContext_->ClearRenderTargetView(renderTargetView_, kClearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain_->Present(1, 0);
    }

    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (imguiContext_)
        {
            ImGui::SetCurrentContext(imguiContext_);
            if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
                return TRUE;
        }

        switch (message)
        {
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED)
                    resizeSwapChain(LOWORD(lParam), HIWORD(lParam));
                return 0;
            case WM_TIMER:
                if (wParam == kRenderTimerId)
                {
                    renderFrame();
                    return 0;
                }
                break;
            case WM_PAINT:
            {
                PAINTSTRUCT ps {};
                BeginPaint(window, &ps);
                renderFrame();
                EndPaint(window, &ps);
                return 0;
            }
            default:
                break;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    HCPlugin::ImGuiEditor* editor_ {nullptr};
    EditController* controller_ {nullptr};
    HWND parentWindow_ {nullptr};
    HWND window_ {nullptr};
    ID3D11Device* device_ {nullptr};
    ID3D11DeviceContext* deviceContext_ {nullptr};
    IDXGISwapChain* swapChain_ {nullptr};
    ID3D11RenderTargetView* renderTargetView_ {nullptr};
    ImGuiContext* imguiContext_ {nullptr};
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
        auto* renderer = static_cast<Win32Renderer*>(rendererHandle_);
        rendererHandle_ = nullptr;
        delete renderer;
    }
}

tresult PLUGIN_API ImGuiEditor::isPlatformTypeSupported(FIDString type)
{
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ImGuiEditor::attached(void* parent, FIDString type)
{
    if (!parent || !FIDStringsEqual(type, kPlatformTypeHWND) || systemWindow)
        return kResultFalse;

    auto* renderer = new Win32Renderer(this, controller_);
    if (!renderer->attach(static_cast<HWND>(parent), getRect().getWidth(), getRect().getHeight()))
    {
        delete renderer;
        return kResultFalse;
    }

    systemWindow = parent;
    nativeView_ = renderer->windowHandle();
    rendererHandle_ = renderer;
    attachedToParent();
    return kResultOk;
}

tresult PLUGIN_API ImGuiEditor::removed()
{
    if (rendererHandle_)
    {
        auto* renderer = static_cast<Win32Renderer*>(rendererHandle_);
        rendererHandle_ = nullptr;
        nativeView_ = nullptr;
        delete renderer;
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
    {
        auto* renderer = static_cast<Win32Renderer*>(rendererHandle_);
        renderer->resize(newSize->getWidth(), newSize->getHeight());
    }
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
