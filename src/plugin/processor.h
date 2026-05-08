#pragma once

#include "core/hclang_engine.h"
#include "plugin/ids.h"
#include "plugin/version.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <vector>

namespace HCPlugin {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;

    // Access to engine and code
    HCLangEngine& getEngine() { return m_engine; }
    const HCLangEngine& getEngine() const { return m_engine; }
    std::string getCurrentCode() const { return m_engine.currentCode(); }
    void setCodeAndEvaluate(const std::string& code) { m_engine.setCode(code); if (m_isActive) m_engine.evaluate(code); }
    bool isActive() const { return m_isActive; }

    // MIDI event handling
    void processMidiEvents(Steinberg::Vst::IEventList* events);

private:
    template <typename SampleType>
    Steinberg::tresult processAudio(Steinberg::Vst::ProcessData& data);

    HCLangEngine m_engine;
    HCLangEngine::Config m_engineConfig;
    bool m_isActive { false };
    bool m_isEffect { false }; // Track if this is effect variant
};

} // namespace HCPlugin
