#include "ui/imgui_editor.h"

#include "plugin/controller.h"
#include "plugin/version.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "ui/imgui_style.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr auto kEditorWidth  = 900;
constexpr auto kEditorHeight = 600;

@interface ImGuiRenderer : NSObject <MTKViewDelegate>
- (instancetype)initWithView:(MTKView*)view editor:(HCPlugin::ImGuiEditor*)editor controller:(EditController*)controller;
- (void)shutdown;
@end

@implementation ImGuiRenderer
{
    ImGuiContext* imguiContext_;
    HCPlugin::ImGuiEditor* editor_;
    EditController* controller_;
    id<MTLDevice> device_;
    id<MTLCommandQueue> commandQueue_;
}

- (instancetype)initWithView:(MTKView*)view editor:(HCPlugin::ImGuiEditor*)editor controller:(EditController*)controller
{
    self = [super init];
    if (!self)
        return nil;

    editor_ = editor;
    controller_ = controller;
    device_ = MTLCreateSystemDefaultDevice();
    commandQueue_ = [device_ newCommandQueue];

    view.device = device_;
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view.clearColor = MTLClearColorMake(0.075f, 0.075f, 0.082f, 1.0f);
    view.preferredFramesPerSecond = 60;
    view.paused = NO;
    view.enableSetNeedsDisplay = NO;

    IMGUI_CHECKVERSION();
    imguiContext_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguiContext_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Disable imgui.ini saving

    HCPlugin::StyleManager::setupStyle();

    ImGui_ImplMetal_Init(device_);
    ImGui_ImplOSX_Init(view);

    return self;
}

- (void)shutdown
{
    if (!imguiContext_)
        return;

    ImGui::SetCurrentContext(imguiContext_);
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext(imguiContext_);
    imguiContext_ = nullptr;
}

- (void)dealloc
{
    [self shutdown];
}

- (void)drawInMTKView:(MTKView*)view
{
    if (!imguiContext_ || !editor_ || !controller_)
        return;

    id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor == nil)
    {
        [commandBuffer commit];
        return;
    }

    // Update engine reference
    auto* hcController = static_cast<HCPlugin::Controller*>(controller_);
    if (hcController) {
        auto* engine = hcController->getEngine();
        if (engine) {
            editor_->setEngine(engine);
        }
    }

    ImGui::SetCurrentContext(imguiContext_);
    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
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
                  static_cast<float>(view.bounds.size.width),
                  static_cast<float>(view.bounds.size.height),
                  FULL_VERSION_STR);

    ImGui::Render();

    id<MTLRenderCommandEncoder> renderEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
    [renderEncoder endEncoding];

    if (view.currentDrawable)
        [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    (void)view;
    (void)size;
}

@end

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
        auto* renderer = (__bridge_transfer ImGuiRenderer*)rendererHandle_;
        rendererHandle_ = nullptr;
        [renderer shutdown];
    }

    if (nativeView_)
    {
        auto* metalView = (__bridge_transfer MTKView*)nativeView_;
        nativeView_ = nullptr;
        metalView.delegate = nil;
        [metalView removeFromSuperview];
    }
}

tresult PLUGIN_API ImGuiEditor::isPlatformTypeSupported(FIDString type)
{
    return FIDStringsEqual(type, kPlatformTypeNSView) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ImGuiEditor::attached(void* parent, FIDString type)
{
    if (!parent || !FIDStringsEqual(type, kPlatformTypeNSView) || systemWindow)
        return kResultFalse;

    systemWindow = parent;
    auto* parentView = (__bridge NSView*)parent;
    auto* metalView = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, getRect().getWidth(), getRect().getHeight())];
    [metalView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [parentView addSubview:metalView];

    auto* renderer = [[ImGuiRenderer alloc] initWithView:metalView
                                                   editor:this
                                               controller:controller_];
    metalView.delegate = renderer;

    nativeView_ = (__bridge_retained void*)metalView;
    rendererHandle_ = (__bridge_retained void*)renderer;
    attachedToParent();
    return kResultOk;
}

tresult PLUGIN_API ImGuiEditor::removed()
{
    if (rendererHandle_)
    {
        auto* renderer = (__bridge_transfer ImGuiRenderer*)rendererHandle_;
        rendererHandle_ = nullptr;
        [renderer shutdown];
    }

    if (nativeView_)
    {
        auto* metalView = (__bridge_transfer MTKView*)nativeView_;
        nativeView_ = nullptr;
        metalView.delegate = nil;
        [metalView removeFromSuperview];
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
    if (nativeView_)
    {
        auto* metalView = (__bridge MTKView*)nativeView_;
        [metalView setFrame:NSMakeRect(0, 0, newSize->getWidth(), newSize->getHeight())];
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
