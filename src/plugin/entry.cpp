#include "plugin/controller.h"
#include "plugin/ids.h"
#include "plugin/processor.h"
#include "plugin/version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "HCPlugin"

using namespace Steinberg;

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2(
        INLINE_UID_FROM_FUID(HCPlugin::kProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        stringPluginName,
        Vst::kDistributable,
        "InstrumentSynth",
        FULL_VERSION_STR,
        kVstVersionString,
        HCPlugin::Processor::createInstance)

    DEF_CLASS2(
        INLINE_UID_FROM_FUID(HCPlugin::kControllerUID),
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,
        stringPluginName " Controller",
        0,
        "",
        FULL_VERSION_STR,
        kVstVersionString,
        HCPlugin::Controller::createInstance)

END_FACTORY
