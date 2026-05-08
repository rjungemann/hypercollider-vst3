#pragma once

#include "plugin/ids.h"
#include "plugin/version.h"
#include "core/hclang_engine.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <string>
#include <vector>

namespace Steinberg::Vst {
class IPlugView;
}

namespace HCPlugin {

class Controller : public Steinberg::Vst::EditControllerEx1
{
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                         Steinberg::Vst::ParamValue valueNormalized,
                                                         Steinberg::Vst::String128 string) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
                                                         Steinberg::Vst::TChar* string,
                                                         Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;

    // Access to saved state for UI
    const std::string& getSavedCode() const { return savedCode_; }
    const std::string& getSavedPresetName() const { return savedPresetName_; }

private:
    HCLangEngine* getEngine() const;

    // For Phase 3: we need access to the processor's engine
    // This is a workaround - in a real implementation, we'd have a better
    // way to share state between processor and controller
    mutable HCLangEngine* engineCache_ { nullptr };

    // Saved state for serialization
    std::string savedCode_;
    std::string savedPresetName_;
    float masterVolume_ { 1.0f };
    bool bypass_ { false };
};

} // namespace HCPlugin
