#include "clap_state.h"
#include "clap_plugin.h"

namespace HCPlugin {

ClapState::ClapState(ClapPlugin* plugin) : m_plugin(plugin) {
    clap_plugin_state_t state = {
        clap_state_save,
        clap_state_load,
    };
    m_pluginState = state;
}

ClapState::~ClapState() = default;

bool ClapState::clap_state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->saveState(stream);
}

bool ClapState::clap_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->loadState(stream);
}

} // namespace HCPlugin
