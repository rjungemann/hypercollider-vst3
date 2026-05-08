#pragma once

#include "ui/editor_parameter_access.h"
#include "core/hclang_engine.h"
#include "core/preset_manager.h"
#include "ui/syntax_highlight.h"

#include <string>
#include <vector>
#include <deque>
#include <optional>

namespace HCPlugin {

// Log entry for the log panel
struct LogEntry {
    std::string text;
    bool isError { false };
};

class EditorCore
{
public:
    EditorCore();
    ~EditorCore();

    void draw(EditorParameterAccess& parameterAccess, float width, float height,
              const char* buildHash) const;

    // Refresh the preset list from PresetManager
    void refreshPresets();

    // Load a preset by name
    void loadPreset(const std::string& name);

    // Save current code as a new preset
    bool savePreset(const std::string& name);

    // Delete a preset by name
    bool deletePreset(const std::string& name);

    struct PresetUiState
    {
        std::vector<std::string> presetNames;
        int selectedPresetIndex { -1 };
        bool showSaveDialog { false };
        bool showDeleteDialog { false };
        std::string newPresetName;
        std::string deletePresetName;
    };

    PresetUiState& presetUiState() { return presetUiState_; }
    const PresetUiState& presetUiState() const { return presetUiState_; }

    // Access to code and log
    std::string& codeText() { return codeText_; }
    const std::string& codeText() const { return codeText_; }
    std::deque<LogEntry>& logEntries() { return logEntries_; }
    const std::deque<LogEntry>& logEntries() const { return logEntries_; }

    // Engine reference (set by controller)
    void setEngine(HCLangEngine* engine) { engine_ = engine; }
    HCLangEngine* getEngine() const { return engine_; }

    // Status
    enum class EngineStatus { Booting, Ready, Error };
    void setEngineStatus(EngineStatus status) { engineStatus_ = status; }
    EngineStatus getEngineStatus() const { return engineStatus_; }

private:
    mutable PresetUiState presetUiState_;
    std::string codeText_;
    std::deque<LogEntry> logEntries_;
    HCLangEngine* engine_ { nullptr };
    EngineStatus engineStatus_ { EngineStatus::Booting };
    PresetManager* presetManager_ { nullptr };
};

} // namespace HCPlugin
