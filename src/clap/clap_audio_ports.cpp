#include "clap_audio_ports.h"
#include "clap_plugin.h"

namespace HCPlugin {

ClapAudioPorts::ClapAudioPorts(ClapPlugin* plugin) : m_plugin(plugin) {
    clap_plugin_audio_ports_t ports = {
        clap_audio_ports_count,
        clap_audio_ports_get,
    };
    m_pluginAudioPorts = ports;
}

ClapAudioPorts::~ClapAudioPorts() = default;

uint32_t ClapAudioPorts::clap_audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->getAudioPortsCount(is_input);
}

bool ClapAudioPorts::clap_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->getAudioPortInfo(index, is_input, info);
}

} // namespace HCPlugin
