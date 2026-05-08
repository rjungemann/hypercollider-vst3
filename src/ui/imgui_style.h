#pragma once

#include "imgui.h"

namespace HCPlugin {

// Color palette matching the browser IDE (sc_ide.html)
namespace Palette {
    constexpr ImU32 kBackground      = IM_COL32( 30,  30,  30, 255); // #1e1e1e
    constexpr ImU32 kToolbarBg      = IM_COL32( 51,  51,  51, 255); // #333333
    constexpr ImU32 kEditorBg        = IM_COL32( 30,  30,  30, 255); // #1e1e1e
    constexpr ImU32 kDefaultText     = IM_COL32(224, 224, 224, 255); // #e0e0e0
    constexpr ImU32 kKeyword         = IM_COL32( 78, 201, 176, 255); // #4ec9b0 (teal)
    constexpr ImU32 kKeywordDim      = IM_COL32( 60, 150, 130, 255); // dimmer teal
    constexpr ImU32 kNumber          = IM_COL32(181, 206, 168, 255); // #b5cea8 (sage green)
    constexpr ImU32 kString          = IM_COL32(206, 145, 120, 255); // #ce9178 (warm orange)
    constexpr ImU32 kComment         = IM_COL32(106, 153,  85, 255); // #6a9955 (muted green)
    constexpr ImU32 kSymbol          = IM_COL32(197, 134, 192, 255); // #c586c0 (mauve)
    constexpr ImU32 kLogNormal       = IM_COL32(224, 224, 224, 255); // #e0e0e0
    constexpr ImU32 kLogError        = IM_COL32(244,  71,  71, 255); // #f44747 (red)
    constexpr ImU32 kEvaluateBtn     = IM_COL32( 14,  99, 156, 255); // #0e639c (VS Code blue)
    constexpr ImU32 kStopBtn         = IM_COL32(139,  62,  62, 255); // #8b3e3e (dark red)
    constexpr ImU32 kStatusReady     = IM_COL32( 76, 175,  80, 255); // green
    constexpr ImU32 kStatusBooting   = IM_COL32(245, 124,   0, 255); // orange
    constexpr ImU32 kStatusError     = IM_COL32(244,  71,  71, 255); // red
} // namespace Palette

class StyleManager {
public:
    static void setupStyle() {
        ImGuiStyle& style = ImGui::GetStyle();

        auto c = [] (ImU32 col) { return ImGui::ColorConvertU32ToFloat4(col); };

        const ImVec4 colorBg         = c(Palette::kBackground);
        const ImVec4 colorToolbarBg  = c(Palette::kToolbarBg);
        const ImVec4 colorText       = c(Palette::kDefaultText);
        const ImVec4 colorTextDim    = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        const ImVec4 colorBorder     = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        const ImVec4 colorAccent     = c(Palette::kKeyword);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                 = colorText;
        colors[ImGuiCol_TextDisabled]         = colorTextDim;
        colors[ImGuiCol_WindowBg]             = colorBg;
        colors[ImGuiCol_ChildBg]              = colorBg;
        colors[ImGuiCol_PopupBg]              = colorToolbarBg;
        colors[ImGuiCol_Border]               = colorBorder;
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]             = colorToolbarBg;
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        colors[ImGuiCol_TitleBg]              = colorToolbarBg;
        colors[ImGuiCol_TitleBgActive]        = colorToolbarBg;
        colors[ImGuiCol_TitleBgCollapsed]     = colorToolbarBg;
        colors[ImGuiCol_MenuBarBg]            = colorToolbarBg;
        colors[ImGuiCol_ScrollbarBg]           = colorToolbarBg;
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        colors[ImGuiCol_CheckMark]           = colorAccent;
        colors[ImGuiCol_SliderGrab]           = colorAccent;
        colors[ImGuiCol_SliderGrabActive]    = colorAccent;
        colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
        colors[ImGuiCol_ButtonActive]         = colorAccent;
        colors[ImGuiCol_Header]               = colorToolbarBg;
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        colors[ImGuiCol_HeaderActive]         = colorAccent;
        colors[ImGuiCol_Separator]            = colorBorder;
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        colors[ImGuiCol_SeparatorActive]      = colorAccent;
        colors[ImGuiCol_ResizeGrip]           = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        colors[ImGuiCol_ResizeGripActive]     = colorAccent;
        colors[ImGuiCol_Tab]                  = colorToolbarBg;
        colors[ImGuiCol_TabHovered]           = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        colors[ImGuiCol_TabActive]            = colorToolbarBg;
        colors[ImGuiCol_TabUnfocused]         = colorToolbarBg;
        colors[ImGuiCol_TabUnfocusedActive]   = colorToolbarBg;
        colors[ImGuiCol_PlotLines]            = colorAccent;
        colors[ImGuiCol_PlotLinesHovered]      = colorAccent;
        colors[ImGuiCol_PlotHistogram]        = colorAccent;
        colors[ImGuiCol_PlotHistogramHovered] = colorAccent;

        // Transparent for input text background to show editor bg
        colors[ImGuiCol_InputTextBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Borders
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;

        // Rounding
        style.WindowRounding = 0.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        // Padding
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.CellPadding = ImVec2(4.0f, 2.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);

        // Sizing
        style.IndentSpacing = 20.0f;
        style.ColumnsMinSpacing = 6.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 10.0f;

        // Other
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    }
};

} // namespace HCPlugin
