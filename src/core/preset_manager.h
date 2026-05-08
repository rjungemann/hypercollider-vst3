#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace HCPlugin {

class PresetManager {
public:
    struct PresetInfo {
        std::string name;
        std::string code;
        std::string synthDefName;
        std::string path; // empty for factory presets
        bool isFactory { false };
        int64_t createdAt { 0 };
    };

    PresetManager();

    // List all presets (factory first, then user sorted alphabetically)
    std::vector<PresetInfo> list() const;

    // Load a preset by name
    PresetInfo load(const std::string& name) const;

    // Save a preset (user preset)
    bool save(const std::string& name, const std::string& code,
              const std::string& synthDefName = "");

    // Delete a user preset
    bool remove(const std::string& name);

    // Get factory presets (embedded or from disk)
    static std::vector<PresetInfo> getFactoryPresets();

    // Check if a preset name is valid
    static bool isValidPresetName(const std::string& name);

private:
    std::filesystem::path getUserPresetsDir() const;
    static PresetInfo makePresetInfo(const nlohmann::json& j, const std::string& path = "", bool isFactory = false);
    nlohmann::json presetToJson(const PresetInfo& preset) const;
    static std::optional<PresetInfo> parsePresetFile(const std::filesystem::path& path);
};

} // namespace HCPlugin
