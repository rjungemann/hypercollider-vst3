#pragma once

#include "core/hclang_engine.h"

#include <clap/clap.h>

#include <string>

namespace HCPlugin {

// Forward declarations
class ClapAudioPorts;
class ClapParams;
class ClapState;

class ClapPlugin {
public:
    explicit ClapPlugin(const clap_plugin_descriptor_t* desc, const clap_host_t* host);
    ~ClapPlugin();

    // Get the CLAP plugin interface pointer
    const clap_plugin_t* getClapPlugin() const { return &m_clapPlugin; }

    // CLAP plugin interface methods (called by static wrappers)
    bool init();
    void destroy();
    bool activate(double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount);
    void deactivate();
    bool startProcessing();
    void stopProcessing();
    void reset();
    clap_process_status process(const clap_process_t* process);
    const void* getExtension(const char* id);
    void onMainThread();

    // Audio ports
    uint32_t getAudioPortsCount(bool isInput) const;
    bool getAudioPortInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const;

    // Parameters
    uint32_t getParamCount() const;
    bool getParamInfo(uint32_t index, clap_param_info_t* info) const;
    double getParamValue(uint32_t index) const;
    void setParamValue(uint32_t index, double value);
    void paramFlush();

    // State
    bool saveState(const clap_ostream_t* stream);
    bool loadState(const clap_istream_t* stream);

    // Access to engine
    HCLangEngine* getEngine() { return &m_engine; }
    const HCLangEngine* getEngine() const { return &m_engine; }

    // Access to descriptor
    const clap_plugin_descriptor_t* getDescriptor() const { return m_desc; }

    // Access to host
    const clap_host_t* getHost() const { return m_host; }

    // Static wrapper functions for CLAP plugin interface
    static bool s_init(const clap_plugin_t* plugin);
    static void s_destroy(const clap_plugin_t* plugin);
    static bool s_activate(const clap_plugin_t* plugin, double sampleRate, uint32_t minFramesCount, uint32_t maxFramesCount);
    static void s_deactivate(const clap_plugin_t* plugin);
    static bool s_start_processing(const clap_plugin_t* plugin);
    static void s_stop_processing(const clap_plugin_t* plugin);
    static void s_reset(const clap_plugin_t* plugin);
    static clap_process_status s_process(const clap_plugin_t* plugin, const clap_process_t* process);
    static const void* s_get_extension(const clap_plugin_t* plugin, const char* id);
    static void s_on_main_thread(const clap_plugin_t* plugin);

private:
    const clap_plugin_descriptor_t* m_desc;
    const clap_host_t* m_host;
    clap_plugin_t m_clapPlugin;
    HCLangEngine m_engine;
    bool m_initialized { false };
    bool m_activated { false };
    bool m_processing { false };
    float m_masterVolume { 1.0f };
    bool m_bypass { false };
    bool m_isEffect { false };

    // Extensions
    ClapAudioPorts* m_audioPorts { nullptr };
    ClapParams* m_params { nullptr };
    ClapState* m_state { nullptr };
};

// Plugin descriptor helper
const clap_plugin_descriptor_t* get_instrument_descriptor();
const clap_plugin_descriptor_t* get_effect_descriptor();

} // namespace HCPlugin
