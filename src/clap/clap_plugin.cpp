#include "clap_plugin.h"
#include "clap_audio_ports.h"
#include "clap_params.h"
#include "clap_state.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/plugin-features.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <cmath>

namespace HCPlugin {

// Audio port info
static const clap_audio_port_info_t kStereoOutPort = {
    0, // id
    "Stereo Out", // name
    CLAP_AUDIO_PORT_IS_MAIN, // flags
    2, // channel_count
    CLAP_PORT_STEREO, // port_type
    CLAP_INVALID_ID, // in_place_pair
};

static const clap_audio_port_info_t kStereoInPort = {
    0, // id
    "Stereo In", // name
    CLAP_AUDIO_PORT_IS_MAIN, // flags
    2, // channel_count
    CLAP_PORT_STEREO, // port_type
    CLAP_INVALID_ID, // in_place_pair
};

ClapPlugin::ClapPlugin(const clap_plugin_descriptor_t* desc, const clap_host_t* host)
    : m_desc(desc), m_host(host) {
    // Determine if this is an effect from the descriptor's features
    for (const char* const* f = desc->features; f && *f; ++f) {
        if (std::string(*f) == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) {
            m_isEffect = true;
            break;
        }
    }

    // Set up the CLAP plugin structure
    m_clapPlugin.desc = desc;
    m_clapPlugin.plugin_data = this;
    m_clapPlugin.init = s_init;
    m_clapPlugin.destroy = s_destroy;
    m_clapPlugin.activate = s_activate;
    m_clapPlugin.deactivate = s_deactivate;
    m_clapPlugin.start_processing = s_start_processing;
    m_clapPlugin.stop_processing = s_stop_processing;
    m_clapPlugin.reset = s_reset;
    m_clapPlugin.process = s_process;
    m_clapPlugin.get_extension = s_get_extension;
    m_clapPlugin.on_main_thread = s_on_main_thread;
}

ClapPlugin::~ClapPlugin() {
    destroy();
}

bool ClapPlugin::init() {
    if (m_initialized) return true;

    // Initialize extensions
    m_audioPorts = new ClapAudioPorts(this);
    m_params = new ClapParams(this);
    m_state = new ClapState(this);

    if (!m_audioPorts || !m_params || !m_state) {
        destroy();
        return false;
    }

    m_initialized = true;
    return true;
}

void ClapPlugin::destroy() {
    deactivate();
    
    delete m_state;
    m_state = nullptr;
    delete m_params;
    m_params = nullptr;
    delete m_audioPorts;
    m_audioPorts = nullptr;
    
    m_engine.shutdown();
    m_initialized = false;
}

bool ClapPlugin::activate(double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount) {
    if (!m_initialized) {
        if (!init()) return false;
    }
    if (m_activated) return true;

    HCLangEngine::Config cfg;
    cfg.sampleRate = static_cast<uint32_t>(sampleRate);
    cfg.maxBlockSize = maxFramesCount;
    cfg.isEffect = m_isEffect;

    if (!m_engine.init(cfg)) {
        return false;
    }

    m_activated = true;
    return true;
}

void ClapPlugin::deactivate() {
    if (!m_activated) return;
    m_engine.shutdown();
    m_activated = false;
    m_processing = false;
}

bool ClapPlugin::startProcessing() {
    if (!m_activated) return false;
    m_processing = true;
    return true;
}

void ClapPlugin::stopProcessing() {
    m_processing = false;
}

void ClapPlugin::reset() {
    // Reset the engine - clear buffers and restart
    if (m_activated) {
        const auto& cfg = m_engine.getConfig();
        m_engine.shutdown();
        m_engine.init(cfg);
    }
}

clap_process_status ClapPlugin::process(const clap_process_t* process) {
    if (!m_processing || !m_activated) {
        // Fill output with silence
        for (uint32_t i = 0; i < process->audio_outputs_count; ++i) {
            const clap_audio_buffer_t* output = &process->audio_outputs[i];
            if (output && output->data32) {
                for (uint32_t ch = 0; ch < output->channel_count; ++ch) {
                    if (output->data32[ch]) {
                        std::memset(output->data32[ch], 0, process->frames_count * sizeof(float));
                    }
                }
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

    // Get input/output buffers
    const float* inputL = nullptr;
    const float* inputR = nullptr;
    float* outputL = nullptr;
    float* outputR = nullptr;

    if (process->audio_inputs_count > 0 && process->audio_inputs) {
        const clap_audio_buffer_t* input = &process->audio_inputs[0];
        if (input && input->data32 && input->channel_count >= 1) {
            inputL = input->data32[0];
        }
        if (input && input->data32 && input->channel_count >= 2) {
            inputR = input->data32[1];
        }
    }

    if (process->audio_outputs_count > 0 && process->audio_outputs) {
        const clap_audio_buffer_t* output = &process->audio_outputs[0];
        if (output && output->data32 && output->channel_count >= 1) {
            outputL = output->data32[0];
        }
        if (output && output->data32 && output->channel_count >= 2) {
            outputR = output->data32[1];
        }
    }

    if (!outputL || !outputR) {
        return CLAP_PROCESS_CONTINUE;
    }

    // Process audio through hcsynth
    m_engine.render(inputL, inputR, outputL, outputR, process->frames_count);
    
    return CLAP_PROCESS_CONTINUE;
}

const void* ClapPlugin::getExtension(const char* id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return m_audioPorts->getExtension();
    }
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return m_params->getExtension();
    }
    if (strcmp(id, CLAP_EXT_STATE) == 0) {
        return m_state->getExtension();
    }
    return nullptr;
}

void ClapPlugin::onMainThread() {
    // Called by host on main thread
}

uint32_t ClapPlugin::getAudioPortsCount(bool isInput) const {
    if (isInput) {
        return m_isEffect ? 1 : 0; // Effect has input, instrument doesn't
    }
    return 1; // Both have stereo output
}

bool ClapPlugin::getAudioPortInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const {
    if (!info) return false;
    
    if (isInput) {
        if (index != 0 || !m_isEffect) return false;
        *info = kStereoInPort;
        return true;
    } else {
        if (index != 0) return false;
        *info = kStereoOutPort;
        return true;
    }
}

uint32_t ClapPlugin::getParamCount() const {
    return 2; // Master Volume + Bypass
}

bool ClapPlugin::getParamInfo(uint32_t index, clap_param_info_t* info) const {
    if (!info || index >= 2) return false;

    std::memset(info, 0, sizeof(clap_param_info_t));
    
    switch (index) {
        case 0:
            info->id = 0;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
            info->cookie = const_cast<void*>(static_cast<const void*>(this));
            strncpy(info->name, "Master Volume", CLAP_NAME_SIZE - 1);
            strncpy(info->module, "", CLAP_PATH_SIZE - 1);
            info->min_value = -60.0;
            info->max_value = 12.0;
            info->default_value = 0.0;
            return true;
        case 1:
            info->id = 1;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_BYPASS;
            info->cookie = const_cast<void*>(static_cast<const void*>(this));
            strncpy(info->name, "Bypass", CLAP_NAME_SIZE - 1);
            strncpy(info->module, "", CLAP_PATH_SIZE - 1);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.0;
            return true;
        default:
            return false;
    }
}

double ClapPlugin::getParamValue(uint32_t index) const {
    switch (index) {
        case 0: return 20.0f * std::log10f(m_masterVolume + 0.0001f);
        case 1: return m_bypass ? 1.0 : 0.0;
        default: return 0.0;
    }
}

void ClapPlugin::setParamValue(uint32_t index, double value) {
    switch (index) {
        case 0: {
            float linear = static_cast<float>(std::pow(10.0, value / 20.0));
            m_masterVolume = std::max(0.0f, std::min(1.0f, linear));
            break;
        }
        case 1:
            m_bypass = value >= 0.5;
            break;
    }
}

void ClapPlugin::paramFlush() {
    // Parameters have been changed by host
}

bool ClapPlugin::saveState(const clap_ostream_t* stream) {
    nlohmann::json j;
    j["version"] = 1;
    j["code"] = m_engine.currentCode();
    j["masterVolume"] = m_masterVolume;
    j["bypass"] = m_bypass;

    std::string jsonStr = j.dump();
    return stream->write(stream, jsonStr.data(), static_cast<uint64_t>(jsonStr.size())) == (int64_t)jsonStr.size();
}

bool ClapPlugin::loadState(const clap_istream_t* stream) {
    // Read in chunks since we don't know the size
    const uint64_t chunkSize = 4096;
    std::string jsonStr;
    char buffer[chunkSize];
    
    while (true) {
        int64_t bytesRead = stream->read(stream, buffer, chunkSize);
        if (bytesRead <= 0) {
            break; // EOF or error
        }
        jsonStr.append(buffer, static_cast<size_t>(bytesRead));
    }
    
    if (jsonStr.empty()) {
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        std::string code = j.value("code", "");
        m_masterVolume = j.value("masterVolume", 1.0f);
        m_bypass = j.value("bypass", false);
        
        if (!code.empty()) {
            m_engine.setCode(code);
            if (m_activated) {
                m_engine.evaluate(code);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Static wrapper functions
bool ClapPlugin::s_init(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    return self->init();
}

void ClapPlugin::s_destroy(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    self->destroy();
}

bool ClapPlugin::s_activate(const clap_plugin_t* plugin, double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    return self->activate(sampleRate, minFramesCount, maxFramesCount);
}

void ClapPlugin::s_deactivate(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    self->deactivate();
}

bool ClapPlugin::s_start_processing(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    return self->startProcessing();
}

void ClapPlugin::s_stop_processing(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    self->stopProcessing();
}

void ClapPlugin::s_reset(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    self->reset();
}

clap_process_status ClapPlugin::s_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    return self->process(process);
}

const void* ClapPlugin::s_get_extension(const clap_plugin_t* plugin, const char* id) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    return self->getExtension(id);
}

void ClapPlugin::s_on_main_thread(const clap_plugin_t* plugin) {
    auto* self = static_cast<ClapPlugin*>(plugin->plugin_data);
    self->onMainThread();
}

// Plugin descriptors
static const char* kInstrumentFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    nullptr
};

static const char* kEffectFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    nullptr
};

static clap_plugin_descriptor_t kInstrumentDescriptor = {
    CLAP_VERSION,
    "io.hypercollider.HCPlugin.Instrument",
    "HCPlugin",
    "HyperCollider",
    nullptr, // URL
    nullptr, // Manual URL
    nullptr, // Support URL
    "0.1.0",
    "HyperCollider WASM synthesis plugin",
    kInstrumentFeatures,
};

static clap_plugin_descriptor_t kEffectDescriptor = {
    CLAP_VERSION,
    "io.hypercollider.HCPlugin.Effect",
    "HCPlugin Effect",
    "HyperCollider",
    nullptr, // URL
    nullptr, // Manual URL
    nullptr, // Support URL
    "0.1.0",
    "HyperCollider WASM effect plugin",
    kEffectFeatures,
};

const clap_plugin_descriptor_t* get_instrument_descriptor() {
    return &kInstrumentDescriptor;
}

const clap_plugin_descriptor_t* get_effect_descriptor() {
    return &kEffectDescriptor;
}

} // namespace HCPlugin
