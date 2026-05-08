#include "plugin/processor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/futils.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace HCPlugin {

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    // Configure as instrument: 0 inputs, 2 outputs (stereo)
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    
    // Add MIDI event input bus for instrument variant
    addEventInput(STR16("MIDI In"), 1);

    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
    {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
               ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& newSetup)
{
    // Store the new sample rate and block size
    m_engineConfig.sampleRate = static_cast<uint32_t>(newSetup.sampleRate);
    m_engineConfig.maxBlockSize = static_cast<uint32_t>(newSetup.maxSamplesPerBlock);
    m_engineConfig.isEffect = false; // Instrument variant

    return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state && !m_isActive)
    {
        // Activate: initialize the engine
        if (!m_engine.init(m_engineConfig))
        {
            return kResultFalse;
        }
        m_isActive = true;
    }
    else if (!state && m_isActive)
    {
        // Deactivate: shutdown the engine
        m_engine.shutdown();
        m_isActive = false;
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    if (data.numOutputs == 0 || data.numSamples == 0)
        return kResultOk;

    // Process MIDI events from input bus
    if (data.inputEvents && data.inputEvents->getEventCount() > 0)
    {
        processMidiEvents(data.inputEvents);
    }

    if (processSetup.symbolicSampleSize == kSample64)
        return processAudio<double>(data);

    return processAudio<float>(data);
}

void Processor::processMidiEvents(Steinberg::Vst::IEventList* events)
{
    if (!events || !m_isActive) return;

    const int32_t numEvents = events->getEventCount();
    for (int32_t i = 0; i < numEvents; ++i)
    {
        Steinberg::Vst::Event event;
        if (events->getEvent(i, event) != kResultOk) continue;

        // Only process note events
        if (event.type != Steinberg::Vst::Event::kNoteOnEvent &&
            event.type != Steinberg::Vst::Event::kNoteOffEvent)
        {
            continue;
        }

        // Access the note event data from the union
        const uint8_t status = static_cast<uint8_t>(event.type == Steinberg::Vst::Event::kNoteOnEvent ? 0x90 : 0x80);
        const uint8_t note = static_cast<uint8_t>(event.noteOn.pitch);
        const uint8_t velocity = static_cast<uint8_t>(event.noteOn.velocity * 127.0f);
        const uint8_t channel = static_cast<uint8_t>(event.noteOn.channel);

        // Forward to hclang engine
        HCLangEngine::MidiEvent midiEvent{status, channel, note, velocity};
        m_engine.postMidiEvent(midiEvent);
    }
}

template <typename SampleType>
tresult Processor::processAudio(ProcessData& data)
{
    void** outputBufs = getChannelBuffersPointer(processSetup, data.outputs[0]);
    
    const int32 numOutChannels = data.outputs[0].numChannels;
    
    const bool isDouble = sizeof(SampleType) == sizeof(double);
    std::vector<float> tmpOutL, tmpOutR;
    float* outL = nullptr;
    float* outR = nullptr;

    if (isDouble)
    {
        tmpOutL.resize(data.numSamples, 0.0f);
        tmpOutR.resize(data.numSamples, 0.0f);
        outL = tmpOutL.data();
        outR = tmpOutR.data();
    }
    else
    {
        outL = static_cast<float*>(outputBufs[0]);
        outR = (numOutChannels >= 2) ? static_cast<float*>(outputBufs[1]) : nullptr;
    }

    // If no stereo output, create a dummy right channel
    std::vector<float> tmpSingleR;
    if (!outR)
    {
        tmpSingleR.resize(data.numSamples, 0.0f);
        outR = tmpSingleR.data();
    }

    // Call the engine to render audio
    m_engine.render(nullptr, nullptr, outL, outR, static_cast<uint32_t>(data.numSamples));

    // Copy back to output buffers if double precision
    if (isDouble)
    {
        auto* outputL = static_cast<SampleType*>(outputBufs[0]);
        auto* outputR = (numOutChannels >= 2) ? static_cast<SampleType*>(outputBufs[1]) : nullptr;

        if (numOutChannels >= 1)
            for (int i = 0; i < data.numSamples; ++i)
                outputL[i] = static_cast<SampleType>(tmpOutL[i]);
        if (numOutChannels >= 2)
            for (int i = 0; i < data.numSamples; ++i)
                outputR[i] = static_cast<SampleType>(tmpOutR[i]);
    }

    return kResultOk;
}

} // namespace HCPlugin
