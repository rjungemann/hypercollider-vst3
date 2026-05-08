#include "clap_plugin.h"

#include <clap/clap.h>
#include <clap/factory/plugin-factory.h>
#include <clap/entry.h>
#include <clap/version.h>
#include <clap/plugin-features.h>

#include <cstring>

namespace HCPlugin {

// Get plugin descriptors
const clap_plugin_descriptor_t* get_instrument_descriptor();
const clap_plugin_descriptor_t* get_effect_descriptor();

} // namespace HCPlugin

// Plugin creation - returns clap_plugin_t*
static const clap_plugin_t* clap_plugin_create(const clap_plugin_descriptor_t* desc, const clap_host_t* host) {
    return (new HCPlugin::ClapPlugin(desc, host))->getClapPlugin();
}

static void clap_plugin_destroy(const clap_plugin_t* plugin) {
    if (plugin) {
        auto* hcPlugin = static_cast<HCPlugin::ClapPlugin*>(plugin->plugin_data);
        delete hcPlugin;
    }
}

// Get plugin count
static uint32_t clap_factory_get_plugin_count(const clap_plugin_factory_t* factory) {
    (void)factory;
    return 2; // instrument + effect
}

// Get plugin descriptor by index
static const clap_plugin_descriptor_t* clap_factory_get_plugin_descriptor(
    const clap_plugin_factory_t* factory, uint32_t index) {
    (void)factory;
    if (index == 0) {
        return HCPlugin::get_instrument_descriptor();
    }
    if (index == 1) {
        return HCPlugin::get_effect_descriptor();
    }
    return nullptr;
}

// Create plugin instance by ID
static const clap_plugin_t* clap_factory_create_plugin(
    const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id) {
    (void)factory;
    
    const clap_plugin_descriptor_t* desc = nullptr;
    
    if (strcmp(plugin_id, "io.hypercollider.HCPlugin.Instrument") == 0) {
        desc = HCPlugin::get_instrument_descriptor();
    } else if (strcmp(plugin_id, "io.hypercollider.HCPlugin.Effect") == 0) {
        desc = HCPlugin::get_effect_descriptor();
    }
    
    if (desc) {
        return clap_plugin_create(desc, host);
    }
    return nullptr;
}

// Factory instance
static const clap_plugin_factory_t kFactory = {
    clap_factory_get_plugin_count,
    clap_factory_get_plugin_descriptor,
    clap_factory_create_plugin,
};

// DSO init/deinit
static bool clap_dso_init(const char* plugin_path) {
    (void)plugin_path;
    return true;
}

static void clap_dso_deinit() {
    // Nothing to do
}

// Get factory for a given factory_id
static const void* clap_entry_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &kFactory;
    }
    return nullptr;
}

// The main entry point
extern "C" const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    clap_dso_init,
    clap_dso_deinit,
    clap_entry_get_factory,
};
