#pragma once

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>

namespace HCPlugin {

class ClapPlugin;

class ClapAudioPorts {
public:
    explicit ClapAudioPorts(ClapPlugin* plugin);
    ~ClapAudioPorts();

    const void* getExtension() const { return &m_pluginAudioPorts; }
    const clap_plugin_audio_ports_t* getDescriptor() const { return &m_pluginAudioPorts; }

private:
    static uint32_t clap_audio_ports_count(const clap_plugin_t* plugin, bool is_input);
    static bool clap_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info);

    ClapPlugin* m_plugin;
    clap_plugin_audio_ports_t m_pluginAudioPorts;
};

} // namespace HCPlugin
