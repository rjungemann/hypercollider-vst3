#pragma once

#include <clap/clap.h>
#include <clap/ext/state.h>

namespace HCPlugin {

class ClapPlugin;

class ClapState {
public:
    explicit ClapState(ClapPlugin* plugin);
    ~ClapState();

    const void* getExtension() const { return &m_pluginState; }
    const clap_plugin_state_t* getDescriptor() const { return &m_pluginState; }

private:
    static bool clap_state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream);
    static bool clap_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream);

    ClapPlugin* m_plugin;
    clap_plugin_state_t m_pluginState;
};

} // namespace HCPlugin
