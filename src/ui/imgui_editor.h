#pragma once

#include "base/source/fobject.h"
#include "public.sdk/source/common/pluginview.h"

#include "ui/editor_core.h"
#include "core/hclang_engine.h"

namespace Steinberg::Vst {
class EditController;
}

namespace HCPlugin {

class ImGuiEditor final : public Steinberg::CPluginView
{
public:
    explicit ImGuiEditor(Steinberg::Vst::EditController* controller);
    ~ImGuiEditor() SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

    // Set the engine reference (called from render loop)
    void setEngine(HCLangEngine* engine) {
        editorCore_.setEngine(engine);
        if (engine) {
            switch (engine->getStatus()) {
                case HCLangEngine::Status::Ready:
                    editorCore_.setEngineStatus(EditorCore::EngineStatus::Ready);
                    // When engine is ready, sync saved state from controller
                    syncSavedState();
                    break;
                case HCLangEngine::Status::Booting:
                    editorCore_.setEngineStatus(EditorCore::EngineStatus::Booting);
                    break;
                case HCLangEngine::Status::Error:
                    editorCore_.setEngineStatus(EditorCore::EngineStatus::Error);
                    break;
            }
        }
    }

    // Sync saved state from controller to editor core
    void syncSavedState();

protected:
    Steinberg::IPtr<Steinberg::Vst::EditController> controller_;
    EditorCore editorCore_;
    void* nativeView_ {nullptr};
    void* rendererHandle_ {nullptr};
};

} // namespace HCPlugin
