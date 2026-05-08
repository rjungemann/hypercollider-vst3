#include "clap_params.h"
#include "clap_plugin.h"

#include <cstdio>
#include <cstring>

namespace HCPlugin {

ClapParams::ClapParams(ClapPlugin* plugin) : m_plugin(plugin) {
    clap_plugin_params_t params = {
        clap_params_count,
        clap_params_get_info,
        clap_params_get_value,
        clap_params_value_to_text,
        clap_params_text_to_value,
        clap_params_flush,
    };
    m_pluginParams = params;
}

ClapParams::~ClapParams() = default;

uint32_t ClapParams::clap_params_count(const clap_plugin_t* plugin) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->getParamCount();
}

bool ClapParams::clap_params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    return hcPlugin->getParamInfo(index, info);
}

bool ClapParams::clap_params_get_value(const clap_plugin_t* plugin, clap_id param_id, double* out_value) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    *out_value = hcPlugin->getParamValue(static_cast<uint32_t>(param_id));
    return true;
}

bool ClapParams::clap_params_value_to_text(const clap_plugin_t* plugin, clap_id param_id, double value, char* buffer, uint32_t buffer_size) {
    (void)plugin; (void)param_id;
    snprintf(buffer, buffer_size, "%.2f", value);
    return true;
}

bool ClapParams::clap_params_text_to_value(const clap_plugin_t* plugin, clap_id param_id, const char* text, double* out_value) {
    (void)plugin; (void)param_id;
    try {
        *out_value = std::stod(text);
        return true;
    } catch (...) {
        return false;
    }
}

void ClapParams::clap_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    auto* hcPlugin = static_cast<ClapPlugin*>(plugin->plugin_data);
    hcPlugin->paramFlush();
}

} // namespace HCPlugin
