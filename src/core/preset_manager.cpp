#include "preset_manager.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace HCPlugin {

PresetManager::PresetManager() {}

std::filesystem::path PresetManager::getUserPresetsDir() const {
#if defined(__APPLE__)
    // macOS: ~/Library/Application Support/HCPlugin/presets/
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "HCPlugin" / "presets";
    }
#elif defined(_WIN32)
    // Windows: %APPDATA%\HCPlugin\presets\
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::filesystem::path(appdata) / "HCPlugin" / "presets";
    }
#else
    // Linux: ~/.local/share/HCPlugin/presets/
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".local" / "share" / "HCPlugin" / "presets";
    }
#endif
    return std::filesystem::path(".") / "presets";
}

std::vector<PresetManager::PresetInfo> PresetManager::list() const {
    std::vector<PresetInfo> presets;

    // Add factory presets first
    auto factory = getFactoryPresets();
    presets.insert(presets.end(), factory.begin(), factory.end());

    // Add user presets
    auto userDir = getUserPresetsDir();
    if (std::filesystem::exists(userDir) && std::filesystem::is_directory(userDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(userDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    std::ifstream f(entry.path());
                    nlohmann::json j;
                    f >> j;
                    auto info = makePresetInfo(j, entry.path().string(), false);
                    presets.push_back(info);
                } catch (...) {
                    // Skip invalid preset files
                }
            }
        }
    }

    // Sort user presets alphabetically (factory presets are already sorted)
    auto userStart = presets.begin() + factory.size();
    std::sort(userStart, presets.end(), [](const PresetInfo& a, const PresetInfo& b) {
        return a.name < b.name;
    });

    return presets;
}

PresetManager::PresetInfo PresetManager::load(const std::string& name) const {
    // Check factory presets first
    auto factory = getFactoryPresets();
    for (const auto& preset : factory) {
        if (preset.name == name) {
            return preset;
        }
    }

    // Check user presets
    auto userDir = getUserPresetsDir();
    auto presetPath = userDir / (name + ".json");
    
    if (std::filesystem::exists(presetPath)) {
        try {
            std::ifstream f(presetPath);
            nlohmann::json j;
            f >> j;
            return makePresetInfo(j, presetPath.string(), false);
        } catch (...) {
            // Return empty preset
        }
    }

    return {};
}

bool PresetManager::save(const std::string& name, const std::string& code,
                         const std::string& synthDefName) {
    if (name.empty()) return false;

    auto userDir = getUserPresetsDir();
    if (!std::filesystem::exists(userDir)) {
        std::filesystem::create_directories(userDir);
    }

    auto presetPath = userDir / (name + ".json");
    
    PresetInfo info;
    info.name = name;
    info.code = code;
    info.synthDefName = synthDefName;
    info.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    try {
        std::ofstream f(presetPath);
        f << presetToJson(info).dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::remove(const std::string& name) {
    if (name.empty()) return false;

    auto userDir = getUserPresetsDir();
    auto presetPath = userDir / (name + ".json");
    
    if (std::filesystem::exists(presetPath)) {
        try {
            std::filesystem::remove(presetPath);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::vector<PresetManager::PresetInfo> PresetManager::getFactoryPresets() {
    std::vector<PresetInfo> presets;

    // Try to load factory presets from the source tree
    // This is for development - in Phase 7 they'll be embedded
    const auto sourceFactoryDir = std::filesystem::path("presets/factory");
    if (std::filesystem::exists(sourceFactoryDir) && std::filesystem::is_directory(sourceFactoryDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(sourceFactoryDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    auto info = parsePresetFile(entry.path());
                    if (info) {
                        info->isFactory = true;
                        info->path = entry.path().string();
                        presets.push_back(*info);
                    }
                } catch (...) {
                    // Skip invalid preset files
                }
            }
        }
    }

    // Fallback hardcoded presets if no factory directory found
    // (for when embedded in binary or source tree not available)
    if (presets.empty()) {
        PresetInfo init;
        init.name = "Init";
        init.code = "{ SinOsc.ar(440, 0, 0.2) ! 2 }.play";
        init.synthDefName = "default";
        init.isFactory = true;
        init.createdAt = 0;
        presets.push_back(init);

        PresetInfo sineTone;
        sineTone.name = "SineTone";
        sineTone.code = "{ SinOsc.ar(440, 0, 0.2) ! 2 }.play";
        sineTone.synthDefName = "SineTone";
        sineTone.isFactory = true;
        sineTone.createdAt = 0;
        presets.push_back(sineTone);

        PresetInfo fmBass;
        fmBass.name = "FMBass";
        fmBass.code = "{ SinOsc.ar([440, 441], 0, 0.2) ! 2 }.play";
        fmBass.synthDefName = "FMBass";
        fmBass.isFactory = true;
        fmBass.createdAt = 0;
        presets.push_back(fmBass);
    }

    // Sort by name
    std::sort(presets.begin(), presets.end(), [](const PresetInfo& a, const PresetInfo& b) {
        return a.name < b.name;
    });

    return presets;
}

PresetManager::PresetInfo PresetManager::makePresetInfo(const nlohmann::json& j,
                                                         const std::string& path,
                                                         bool isFactory) {
    PresetInfo info;
    info.name = j.value("name", "");
    info.code = j.value("code", "");
    info.synthDefName = j.value("synthDefName", "");
    info.path = path;
    info.isFactory = isFactory;
    info.createdAt = j.value("createdAt", 0);
    return info;
}

nlohmann::json PresetManager::presetToJson(const PresetInfo& preset) const {
    nlohmann::json j;
    j["name"] = preset.name;
    j["code"] = preset.code;
    j["synthDefName"] = preset.synthDefName;
    j["createdAt"] = preset.createdAt;
    return j;
}

std::optional<PresetManager::PresetInfo> PresetManager::parsePresetFile(const std::filesystem::path& path) {
    try {
        std::ifstream f(path);
        nlohmann::json j;
        f >> j;
        PresetInfo info = makePresetInfo(j, path.string(), false);
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

bool PresetManager::isValidPresetName(const std::string& name) {
    if (name.empty()) return false;
    // Only allow alphanumeric, spaces, underscores, and hyphens
    for (char c : name) {
        if (!isalnum(c) && c != ' ' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

} // namespace HCPlugin
