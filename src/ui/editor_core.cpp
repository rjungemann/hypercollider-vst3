#include "ui/editor_core.h"

#include "imgui.h"
#include "ui/syntax_highlight.h"
#include "ui/imgui_style.h"

#include <algorithm>
#include <cstring>

namespace HCPlugin {

EditorCore::EditorCore() {
    // Will be initialized lazily
    presetManager_ = nullptr;
}

EditorCore::~EditorCore() {
    if (presetManager_) {
        delete presetManager_;
        presetManager_ = nullptr;
    }
}

void EditorCore::refreshPresets() {
    if (!presetManager_) {
        presetManager_ = new PresetManager();
    }

    auto presets = presetManager_->list();
    presetUiState_.presetNames.clear();
    for (const auto& preset : presets) {
        presetUiState_.presetNames.push_back(preset.name);
    }
}

void EditorCore::loadPreset(const std::string& name) {
    if (!presetManager_) {
        presetManager_ = new PresetManager();
    }

    auto preset = presetManager_->load(name);
    if (preset) {
        codeText_ = preset->code;
        // Update selected index
        auto& names = presetUiState_.presetNames;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                presetUiState_.selectedPresetIndex = static_cast<int>(i);
                break;
            }
        }
        // Evaluate the preset code
        if (engine_) {
            engine_->evaluate(codeText_);
        }
    }
}

bool EditorCore::savePreset(const std::string& name) {
    if (!presetManager_) {
        presetManager_ = new PresetManager();
    }

    if (!PresetManager::isValidPresetName(name)) {
        return false;
    }

    // Check if we're overwriting a factory preset
    auto factoryPresets = PresetManager::getFactoryPresets();
    for (const auto& fp : factoryPresets) {
        if (fp.name == name) {
            // Can't overwrite factory presets
            return false;
        }
    }

    bool success = presetManager_->save(name, codeText_, "");
    if (success) {
        refreshPresets();
        // Select the new preset
        for (size_t i = 0; i < presetUiState_.presetNames.size(); ++i) {
            if (presetUiState_.presetNames[i] == name) {
                presetUiState_.selectedPresetIndex = static_cast<int>(i);
                break;
            }
        }
    }
    return success;
}

bool EditorCore::deletePreset(const std::string& name) {
    if (!presetManager_) {
        presetManager_ = new PresetManager();
    }

    // Can't delete factory presets
    auto factoryPresets = PresetManager::getFactoryPresets();
    for (const auto& fp : factoryPresets) {
        if (fp.name == name) {
            return false;
        }
    }

    bool success = presetManager_->remove(name);
    if (success) {
        refreshPresets();
        // Clear selection if we deleted the selected preset
        if (presetUiState_.selectedPresetIndex >= 0 && 
            presetUiState_.selectedPresetIndex < static_cast<int>(presetUiState_.presetNames.size())) {
            if (presetUiState_.presetNames[presetUiState_.selectedPresetIndex] == name) {
                presetUiState_.selectedPresetIndex = -1;
            }
        }
    }
    return success;
}

void EditorCore::draw(EditorParameterAccess& parameterAccess, float width, float height,
                       const char* buildHash) const
{
    (void)parameterAccess; // Unused in Phase 3

    // Poll log entries from engine
    if (engine_) {
        std::string logLine;
        while (engine_->pollLogLine(logLine)) {
            // Determine if it's an error (starts with "!" or "error")
            bool isError = !logLine.empty() && (logLine[0] == '!' || 
                     logLine.find("error") != std::string::npos ||
                     logLine.find("Error") != std::string::npos);
            logEntries_.push_back({logLine, isError});
            if (logEntries_.size() > 500) {
                logEntries_.pop_front();
            }
        }
    }

    // Set up main window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));

    const ImGuiWindowFlags mainFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("HCPluginMain", nullptr, mainFlags))
    {
        ImGui::SetCursorPos(ImVec2(0, 0));

        // ============================================================
        // TOOLBAR
        // ============================================================
        const float toolbarHeight = 38.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        
        // Toolbar background
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarMin = ImGui::GetCursorScreenPos();
        const ImVec2 toolbarMax = ImVec2(toolbarMin.x + width, toolbarMin.y + toolbarHeight);
        drawList->AddRectFilled(toolbarMin, toolbarMax, Palette::kToolbarBg);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
        
        // Evaluate button (blue)
        ImGui::SetCursorPos(ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.25f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.35f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.40f, 0.60f, 1.0f));
        if (ImGui::Button("\xE2\x96\xB6 Evaluate", ImVec2(100, 26)) && engine_) {
            engine_->evaluate(codeText());
        }
        ImGui::PopStyleColor(3);
        
        // Stop button (dark red)
        ImGui::SameLine();
        ImGui::SetCursorPosX(116);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("\xE2\x96\xA0 Stop", ImVec2(80, 26)) && engine_) {
            engine_->stop();
        }
        ImGui::PopStyleColor(3);
        
        // Separator
        ImGui::SameLine();
        ImGui::SetCursorPosX(204);
        drawList->AddRectFilled(
            ImVec2(toolbarMin.x + 204, toolbarMin.y + 8),
            ImVec2(toolbarMin.x + 212, toolbarMin.y + toolbarHeight - 8),
            ImColor(60, 60, 70));
        
        // Preset dropdown - refresh preset list if empty
        ImGui::SameLine();
        ImGui::SetCursorPosX(220);
        ImGui::SetNextItemWidth(180);
        
        // Build preset list if needed
        if (presetUiState_.presetNames.empty()) {
            const_cast<EditorCore*>(this)->refreshPresets();
        }
        
        int currentPreset = presetUiState_.selectedPresetIndex;
        if (currentPreset < 0) currentPreset = -1;
        
        std::vector<const char*> presetItems;
        for (const auto& name : presetUiState_.presetNames) {
            presetItems.push_back(name.c_str());
        }
        
        if (ImGui::Combo("##Preset", &currentPreset, presetItems.data(), 
                         static_cast<int>(presetItems.size()))) {
            presetUiState_.selectedPresetIndex = currentPreset;
            if (currentPreset >= 0 && presetUiState_.presetNames.size() > static_cast<size_t>(currentPreset)) {
                const_cast<EditorCore*>(this)->loadPreset(presetUiState_.presetNames[currentPreset]);
            }
        }
        
        // Save button
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(60, 26))) {
            presetUiState_.showSaveDialog = true;
            presetUiState_.newPresetName = "";
        }
        
        // Delete button (for user presets) - appears as "X" on hover
        ImGui::SameLine();
        if (currentPreset >= 0 && presetUiState_.presetNames.size() > static_cast<size_t>(currentPreset)) {
            // Check if it's a user preset (not factory)
            auto factoryPresets = PresetManager::getFactoryPresets();
            bool isFactory = false;
            for (const auto& fp : factoryPresets) {
                if (fp.name == presetUiState_.presetNames[currentPreset]) {
                    isFactory = true;
                    break;
                }
            }
            if (!isFactory) {
                if (ImGui::Button("-", ImVec2(30, 26))) {
                    presetUiState_.deletePresetName = presetUiState_.presetNames[currentPreset];
                    presetUiState_.showDeleteDialog = true;
                }
            } else {
                // Show disabled button for factory presets
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImGui::Button("-", ImVec2(30, 26));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cannot delete factory presets");
                }
                ImGui::PopStyleVar();
            }
        } else {
            ImGui::Button("-", ImVec2(30, 26));
        }
        
        // Add button (clear editor)
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2(30, 26))) {
            codeText() = "";
            presetUiState_.selectedPresetIndex = -1;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        
        ImGui::SetCursorPosY(toolbarHeight + 2);
        
        // ============================================================
        // EDITOR PANE
        // ============================================================
        const float editorHeight = height - toolbarHeight - 140 - 24; // 140 for log, 24 for status
        
        // Draw editor background
        drawList->AddRectFilled(
            ImVec2(toolbarMin.x, toolbarMin.y + toolbarHeight),
            ImVec2(toolbarMin.x + width, toolbarMin.y + toolbarHeight + editorHeight),
            Palette::kEditorBg);
        
        ImGui::SetCursorPos(ImVec2(4, toolbarHeight + 4));
        
        // Text editor using InputTextMultiline with syntax highlighting
        // For Phase 3, we use a simple approach - in Phase 4 we could use ImGuiColorTextEdit
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        
        // Create a child region for the editor
        ImGui::BeginChild("EditorRegion", ImVec2(width - 8, editorHeight - 4),
                         false, ImGuiWindowFlags_NoScrollbar);
        
        // Render with syntax highlighting
        renderSyntaxHighlightedText(codeText());
        
        ImGui::EndChild();
        
        ImGui::PopStyleVar(2);
        
        ImGui::SetCursorPosY(toolbarHeight + editorHeight + 4);
        
        // ============================================================
        // STATUS BAR
        // ============================================================
        const float statusHeight = 24.0f;
        drawList->AddRectFilled(
            ImVec2(toolbarMin.x, toolbarMin.y + toolbarHeight + editorHeight),
            ImVec2(toolbarMin.x + width, toolbarMin.y + toolbarHeight + editorHeight + statusHeight),
            Palette::kToolbarBg);
        
        ImGui::SetCursorPos(ImVec2(8, toolbarHeight + editorHeight + 4));
        
        // Status LED
        ImU32 ledColor;
        const char* statusText;
        switch (engineStatus_) {
            case EngineStatus::Ready:
                ledColor = Palette::kStatusReady;
                statusText = "Ready";
                break;
            case EngineStatus::Booting:
                ledColor = Palette::kStatusBooting;
                statusText = "Booting...";
                break;
            case EngineStatus::Error:
                ledColor = Palette::kStatusError;
                statusText = "Error";
                break;
        }
        
        drawList->AddCircleFilled(
            ImVec2(ImGui::GetCursorScreenPos().x + 5, ImGui::GetCursorScreenPos().y + 11),
            5.0f, ledColor);
        
        ImGui::SetCursorPosX(18);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", statusText);
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(width - 150);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "hclang %s", buildHash ? buildHash : "0.1.0");
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(width - 60);
        if (ImGui::SmallButton("Clear")) {
            logEntries_.clear();
        }
        
        ImGui::SetCursorPosY(toolbarHeight + editorHeight + statusHeight + 4);
        
        // ============================================================
        // LOG PANE
        // ============================================================
        const float logHeight = 140.0f;
        drawList->AddRectFilled(
            ImVec2(toolbarMin.x, toolbarMin.y + toolbarHeight + editorHeight + statusHeight),
            ImVec2(toolbarMin.x + width, toolbarMin.y + height),
            Palette::kToolbarBg);
        
        ImGui::SetCursorPos(ImVec2(4, toolbarHeight + editorHeight + statusHeight + 4));
        ImGui::BeginChild("LogRegion", ImVec2(width - 8, logHeight - 4), true);
        
        // Display log entries
        for (const auto& entry : logEntries_) {
            if (entry.isError) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", entry.text.c_str());
            } else {
                ImGui::Text("%s", entry.text.c_str());
            }
        }
        
        // Auto-scroll to bottom
        if (ImGui::GetScrollMaxY() > 0) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndChild();
    }
    ImGui::End();

    // Save preset dialog
    if (presetUiState_.showSaveDialog) {
        ImGui::OpenPopup("Save Preset");
        presetUiState_.showSaveDialog = false;
    }
    
    if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Enter preset name:");
        ImGui::InputText("##PresetName", &presetUiState_.newPresetName);
        
        // Show validation error if name is invalid
        if (!presetUiState_.newPresetName.empty() && !PresetManager::isValidPresetName(presetUiState_.newPresetName)) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Invalid name: use only alphanumeric, spaces, underscores, hyphens");
        }
        
        if (ImGui::Button("Save") && !presetUiState_.newPresetName.empty() && 
            PresetManager::isValidPresetName(presetUiState_.newPresetName)) {
            if (const_cast<EditorCore*>(this)->savePreset(presetUiState_.newPresetName)) {
                presetUiState_.newPresetName.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            presetUiState_.newPresetName.clear();
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    // Delete preset dialog
    if (presetUiState_.showDeleteDialog) {
        ImGui::OpenPopup("Delete Preset");
        presetUiState_.showDeleteDialog = false;
    }
    
    if (ImGui::BeginPopupModal("Delete Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete preset '%s'?", presetUiState_.deletePresetName.c_str());
        
        if (ImGui::Button("Delete")) {
            if (const_cast<EditorCore*>(this)->deletePreset(presetUiState_.deletePresetName)) {
                presetUiState_.deletePresetName.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            presetUiState_.deletePresetName.clear();
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void EditorCore::renderSyntaxHighlightedText(const std::string& text) const
{
    if (text.empty()) {
        ImGui::Text("(empty)");
        return;
    }

    // For Phase 3: simple line-by-line with syntax highlighting
    // In Phase 4, we could use a more sophisticated text editor widget
    
    const char* textBegin = text.c_str();
    const char* textEnd = textBegin + text.size();
    
    // Get current font size and line height
    const float fontSize = ImGui::GetFontSize();
    const float lineHeight = fontSize * 1.2f;
    
    // Get cursor position
    const ImVec2 cursorPos = ImGui::GetCursorPos();
    const ImVec2 startPos = ImGui::GetCursorScreenPos();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Split into lines and render each with syntax highlighting
    const char* lineStart = textBegin;
    int lineNumber = 1;
    
    while (lineStart < textEnd) {
        // Find end of line
        const char* lineEnd = lineStart;
        while (lineEnd < textEnd && *lineEnd != '\n') {
            lineEnd++;
        }
        
        const std::string line(lineStart, lineEnd);
        
        // Tokenize the line
        auto tokens = SyntaxHighlighter::tokenizeLine(line);
        
        // Draw line number
        char lineNumBuf[16];
        snprintf(lineNumBuf, sizeof(lineNumBuf), "%d", lineNumber);
        const float lineNumWidth = ImGui::CalcTextSize(lineNumBuf).x + 8;
        ImGui::SetCursorPosX(cursorPos.x);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", lineNumBuf);
        
        // Draw tokens
        float xOffset = cursorPos.x + lineNumWidth;
        for (const auto& token : tokens) {
            if (token.start >= token.end) continue;
            
            const std::string tokenStr = line.substr(token.start, token.end - token.start);
            const float tokenWidth = ImGui::CalcTextSize(tokenStr.c_str()).x;
            
            ImGui::SetCursorPosX(xOffset);
            ImGui::TextColored(
                ImVec4(ImGui::ColorConvertU32ToFloat4(SyntaxHighlighter::getColor(token.type))),
                "%s", tokenStr.c_str());
            
            xOffset += tokenWidth;
        }
        
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + lineHeight);
        
        lineStart = (lineEnd < textEnd) ? lineEnd + 1 : textEnd;
        lineNumber++;
    }
}

} // namespace HCPlugin
