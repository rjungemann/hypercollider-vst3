#pragma once

#include <clap/clap.h>
#include <clap/ext/params.h>

namespace HCPlugin {

class ClapPlugin;

class ClapParams {
public:
    explicit ClapParams(ClapPlugin* plugin);
    ~ClapParams();

    const void* getExtension() const { return &m_pluginParams; }
    const clap_plugin_params_t* getDescriptor() const { return &m_pluginParams; }

private:
    static uint32_t clap_params_count(const clap_plugin_t* plugin);
    static bool clap_params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info);
    static bool clap_params_get_value(const clap_plugin_t* plugin, clap_id param_id, double* out_value);
    static void clap_params_set_value(const clap_plugin_t* plugin, clap_id param_id, double value);
    static bool clap_params_value_to_text(const clap_plugin_t* plugin, clap_id param_id, double value, char* buffer, uint32_t buffer_size);
    static bool clap_params_text_to_value(const clap_plugin_t* plugin, clap_id param_id, const char* text, double* out_value);
    static void clap_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out);

    ClapPlugin* m_plugin;
    clap_plugin_params_t m_pluginParams;
};

} // namespace HCPlugin
