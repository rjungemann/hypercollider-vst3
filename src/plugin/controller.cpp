#include "plugin/controller.h"
#include "plugin/processor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include <nlohmann/json.hpp>

#if BUILD_IMGUI_UI
#include "ui/imgui_editor.h"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace HCPlugin {

// Helper to get the engine from the processor
HCLangEngine* Controller::getEngine() const {
    if (!engineCache_) {
        // Try to get the processor and its engine
        // This is a workaround for Phase 3 - in a proper implementation,
        // we'd use component messaging or a shared state object
        IComponentHandler* handler = getComponentHandler();
        if (handler) {
            IAudioProcessor* processor = nullptr;
            if (handler->queryInterface(IAudioProcessor::iid, (void**)&processor) == kResultOk) {
                // The processor is our Processor class which has m_engine
                // We need to cast to access it
                // This is a hack for Phase 3
                auto* hcProcessor = dynamic_cast<Processor*>(static_cast<AudioEffect*>(processor));
                if (hcProcessor) {
                    const_cast<HCLangEngine*&>(engineCache_) = &hcProcessor->getEngine();
                }
                processor->release();
            }
        }
    }
    return engineCache_;
}

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    const auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // No parameters for Phase 3 (minimal)
    return kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name)
{
#if BUILD_IMGUI_UI
    if (FIDStringsEqual(name, ViewType::kEditor))
    {
        return new ImGuiEditor(this);
    }
#endif
    return nullptr;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // Read the JSON state blob
    int32 jsonSize = 0;
    if (streamer.readInt32(jsonSize) != kResultOk || jsonSize <= 0) {
        return kResultFalse;
    }

    std::vector<char> jsonBuffer(jsonSize + 1, 0);
    if (streamer.readRaw(jsonBuffer.data(), jsonSize) != kResultOk) {
        return kResultFalse;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(jsonBuffer.data());
        
        // Extract state
        savedCode_ = j.value("code", "");
        savedPresetName_ = j.value("presetName", "");
        masterVolume_ = j.value("masterVolume", 1.0f);
        bypass_ = j.value("bypass", false);
        
        // Try to set the code on the processor
        if (auto* handler = getComponentHandler()) {
            IAudioProcessor* audioProc = nullptr;
            if (handler->queryInterface(IAudioProcessor::iid, (void**)&audioProc) == kResultOk) {
                if (auto* hcProcessor = dynamic_cast<Processor*>(static_cast<AudioEffect*>(audioProc))) {
                    // Defer evaluation until engine is active
                    // The engine will evaluate this code when it initializes
                    hcProcessor->getEngine().setCode(savedCode_);
                    if (hcProcessor->isActive()) {
                        hcProcessor->getEngine().evaluate(savedCode_);
                    }
                }
                audioProc->release();
            }
        }
        
        // Signal that engine status is ready after state restore
        if (engineCache_) {
            engineCache_->postLogLine("State restored: " + savedPresetName_);
        }
    } catch (...) {
        // Failed to parse JSON
        return kResultFalse;
    }

    return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // Try to get current code from processor
    std::string currentCode;
    if (auto* handler = getComponentHandler()) {
        IAudioProcessor* audioProc = nullptr;
        if (handler->queryInterface(IAudioProcessor::iid, (void**)&audioProc) == kResultOk) {
            if (auto* hcProcessor = dynamic_cast<Processor*>(static_cast<AudioEffect*>(audioProc))) {
                currentCode = hcProcessor->getCurrentCode();
                savedCode_ = currentCode;
            }
            audioProc->release();
        }
    }

    // Build JSON state blob
    nlohmann::json j;
    j["version"] = 1;
    j["code"] = currentCode;
    j["presetName"] = savedPresetName_;
    j["masterVolume"] = masterVolume_;
    j["bypass"] = bypass_;

    std::string jsonStr = j.dump();
    int32 jsonSize = static_cast<int32>(jsonStr.size());

    // Write size first
    if (streamer.writeInt32(jsonSize) != kResultOk) {
        return kResultFalse;
    }

    // Write JSON data
    if (streamer.writeRaw(jsonStr.data(), jsonSize) != kResultOk) {
        return kResultFalse;
    }

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(
    ParamID tag, ParamValue valueNormalized, String128 string)
{
    return kResultFalse;
}

tresult PLUGIN_API Controller::getParamValueByString(
    ParamID tag, TChar* string, ParamValue& valueNormalized)
{
    return kResultFalse;
}

} // namespace HCPlugin
