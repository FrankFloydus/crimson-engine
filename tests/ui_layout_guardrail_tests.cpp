////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace {

using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::sourceSlice;

void expectContains(std::string_view source, std::string_view needle) {
    EXPECT_NE(source.find(needle), std::string_view::npos) << needle;
}

void expectMissing(std::string_view source, std::string_view needle) {
    EXPECT_EQ(source.find(needle), std::string_view::npos) << needle;
}

std::size_t countOccurrences(std::string_view source, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = source.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void expectContinuousHelperLiveApplies(std::string_view source, std::string_view helper) {
    const std::size_t helperCount = countOccurrences(source, helper);
    EXPECT_GT(helperCount, 0u) << helper;
    EXPECT_GE(countOccurrences(source, ".shouldApply()"), helperCount) << helper;
    EXPECT_GE(countOccurrences(source, ".mergeKey"), helperCount) << helper;
}

} // namespace

TEST(UiLayoutGuardrailTests, SidebarPanelsDoNotForceEmptyVerticalScrollbars) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});
    const std::string outliner = readSourceFile({"src", "ui", "outliner_panel.cpp"});

    const std::string inspectorFlags = sourceSlice(
            inspector,
            "ImGuiWindowFlags InspectorPanel::windowFlags",
            "void InspectorPanel::drawContent");
    const std::string outlinerFlags = sourceSlice(
            outliner,
            "ImGuiWindowFlags OutlinerPanel::windowFlags",
            "void OutlinerPanel::drawContent");

    expectContains(inspectorFlags, "return context.style.panelWindowFlags;");
    expectContains(outlinerFlags, "return context.style.panelWindowFlags;");
    expectMissing(inspectorFlags, "ImGuiWindowFlags_AlwaysVerticalScrollbar");
    expectMissing(outlinerFlags, "ImGuiWindowFlags_AlwaysVerticalScrollbar");
}

TEST(UiLayoutGuardrailTests, InspectorTransformAxisInputsStretchInsideSidebarWidth) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});
    const std::string axisDragFloat = sourceSlice(
            inspector,
            "AxisDragFloatResult axisDragFloat(const char* axis",
            "AxisDragFloatResult axisDragFloat3");
    const std::string axisDragFloat3 = sourceSlice(
            inspector,
            "AxisDragFloatResult axisDragFloat3",
            "bool beginLabeledControlTable");

    expectContains(axisDragFloat, "inputWidth < 0.0f ? inputWidth");
    expectContains(axisDragFloat3, "ImGuiTableFlags_SizingStretchProp");
    expectContains(axisDragFloat3, "ImGuiTableColumnFlags_WidthStretch");
    expectContains(axisDragFloat3, "minAxisColumnWidth");
    expectContains(axisDragFloat3, "kMinAxisInputWidth");
    EXPECT_EQ(countOccurrences(axisDragFloat3, "-FLT_MIN"), 3u);
    expectMissing(axisDragFloat3, "ImGuiTableFlags_SizingFixedFit");
}

TEST(UiLayoutGuardrailTests, InspectorLightEditsRouteThroughPublicSceneApi) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});
    const std::string lightSection = sourceSlice(
            inspector,
            "if (const LightComponent* currentLight = document.light(primary))",
            "if (selected.size() > 1u)");

    expectContains(lightSection, "context.editor.scene().setLightComponent");
    expectContains(lightSection, "toApiLightComponent");
    expectMissing(inspector, "document.setLight(");
}

TEST(UiLayoutGuardrailTests, InspectorSceneSettingsEditThroughPublicSceneApi) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});
    const std::string sceneSettingsSection = sourceSlice(
            inspector,
            "void drawSceneSettingsInspector",
            "struct BoolAggregate");

    expectContains(sceneSettingsSection, "context.editor.scene().setSceneSettings");
    expectContains(sceneSettingsSection, "toApiSceneSettings");
    expectContains(sceneSettingsSection, "environment_clear_color");
    expectContains(sceneSettingsSection, "ui.inspector.clear_color");
    expectContains(sceneSettingsSection, "post_process_tone_mapping");
    expectContains(sceneSettingsSection, "post_process_dithering");
    expectContains(sceneSettingsSection, "camera_near");
    expectContains(sceneSettingsSection, "camera_far");
    expectMissing(inspector, "document.setSceneSettings(");
}

TEST(UiLayoutGuardrailTests, InspectorSectionsStartCollapsed) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});

    expectContains(inspector, "ImGui::CollapsingHeader(");
    expectMissing(inspector, "ImGuiTreeNodeFlags_DefaultOpen");
}

TEST(UiLayoutGuardrailTests, OutlinerNonLeafNodesStartExpanded) {
    const std::string outliner = readSourceFile({"src", "ui", "outliner_panel.cpp"});
    const std::string treeNodeSetup = sourceSlice(
            outliner,
            "ImGuiTreeNodeFlags treeFlags",
            "const bool open = ImGui::TreeNodeEx");

    expectContains(treeNodeSetup, "record.hasChildren");
    expectContains(treeNodeSetup, "ImGuiTreeNodeFlags_DefaultOpen");
}

TEST(UiLayoutGuardrailTests, InspectorContinuousInputsLiveApplyWithMergeKeys) {
    const std::string inspector = readSourceFile({"src", "ui", "inspector_panel.cpp"});
    const std::string inspectorHeader = readSourceFile({"src", "ui", "inspector_panel.hpp"});
    const std::string tracker = readSourceFile({"src", "ui", "ui_continuous_edit_tracker.hpp"});
    const std::string continuousHelpers = sourceSlice(
            inspector,
            "void updateContinuousEditTracker",
            "template <typename Enum>");
    const std::string sceneSettingsSection = sourceSlice(
            inspector,
            "void drawSceneSettingsInspector",
            "struct BoolAggregate");
    const std::string lightSection = sourceSlice(
            inspector,
            "if (const LightComponent* currentLight = document.light(primary))",
            "if (selected.size() > 1u)");

    expectContains(inspectorHeader, "UIContinuousEditTracker activeContinuousEdit_");
    expectContains(tracker, "class UIContinuousEditTracker");
    expectContains(tracker, "std::string mergeKey");
    expectContains(tracker, "bool shouldApply() const");
    expectContains(tracker, "ensureMergeKey");
    expectContains(continuousHelpers, "ImGui::IsItemDeactivatedAfterEdit()");
    expectContains(continuousHelpers, "activeEdit.stage");
    expectContains(continuousHelpers, "result.mergeKey = activeEdit.ensureMergeKey");
    expectMissing(continuousHelpers, "result.commit");
    expectContains(sceneSettingsSection, "QuaderSceneEditOptions{.mergeKey = std::move(mergeKey)}");
    expectContains(lightSection, "QuaderSceneEditOptions{.mergeKey = std::move(mergeKey)}");

    expectContinuousHelperLiveApplies(sceneSettingsSection, "dragFloatWithLeftLabel(");
    expectContinuousHelperLiveApplies(lightSection, "dragFloatWithLeftLabel(");
    expectContinuousHelperLiveApplies(lightSection, "colorEdit3WithLeftLabel(");
}

TEST(UiLayoutGuardrailTests, CustomChromeKeepsCurrentLogoAndTopMenuSpacing) {
    const std::string uiSystem = readSourceFile({"src", "ui", "ui_system.cpp"});
    const std::string chrome = sourceSlice(
            uiSystem,
            "MainMenuBarResult UISystem::drawWindowChrome",
            "void UISystem::drawDockspace");

    expectContains(uiSystem, "constexpr float kChromeLogoSize = 18.0f;");
    expectContains(uiSystem, "constexpr float kChromeLogoLeft = 16.0f;");
    expectContains(uiSystem, "constexpr float kChromeMenuLeft = 48.0f;");
    expectContains(uiSystem, "constexpr float kChromeMenuSpacing = 10.0f;");
    expectContains(chrome, "ImGui::SetCursorPos({kChromeMenuLeft, 0.0f});");
    expectContains(chrome, "ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {kChromeMenuSpacing, 0.0f});");
    expectContains(chrome, "ImGui::GetForegroundDrawList()->AddImage(icons_.logo, logoMin, logoMax);");
}

TEST(UiLayoutGuardrailTests, StaticBottomBarTextCentersAboveAccentStrip) {
    const std::string uiSystem = readSourceFile({"src", "ui", "ui_system.cpp"});
    const std::string bottomBar = sourceSlice(
            uiSystem,
            "void drawStaticBottomBar",
            "UISystem::~UISystem");

    expectContains(bottomBar, "kStaticBottomBarHeight - kStaticBottomBarAccentHeight");
    expectContains(bottomBar, "ImGui::GetTextLineHeight()");
    expectMissing(bottomBar, "kStaticBottomBarHeight - ImGui::GetFontSize()");
}

TEST(UiLayoutGuardrailTests, MainMenuDropdownsUsePopupPaddingAndVerticalSpacing) {
    const std::string mainMenu = readSourceFile({"src", "ui", "main_menu_bar.cpp"});
    const std::string mainMenuHeader = readSourceFile({"src", "ui", "main_menu_bar.hpp"});
    const std::string uiSystem = readSourceFile({"src", "ui", "ui_system.cpp"});
    const std::string uiSystemHeader = readSourceFile({"src", "ui", "ui_system.hpp"});
    const std::string popupUtils = readSourceFile({"src", "ui", "imgui_popup_utils.cpp"});

    expectContains(mainMenu, "constexpr float kMenuPopupPaddingX = 12.0f;");
    expectContains(mainMenu, "constexpr float kMenuPopupPaddingY = 8.0f;");
    expectContains(mainMenu, "constexpr float kMenuPopupFramePaddingY = 5.0f;");
    expectContains(mainMenu, "constexpr float kMenuPopupItemSpacingY = 4.0f;");
    expectContains(mainMenu, "bool beginMenuWithPopupSpacing");
    expectContains(mainMenu, "void endMenuWithPopupSpacing()");
    expectContains(mainMenu, "ImGuiStyleVar_WindowPadding");
    expectContains(mainMenu, "ImGuiStyleVar_FramePadding");
    expectContains(mainMenu, "ImGuiStyleVar_ItemSpacing");
    EXPECT_EQ(countOccurrences(mainMenu, "ImGui::BeginMenu("), 1u);
    EXPECT_EQ(countOccurrences(mainMenu, "ImGui::EndMenu();"), 1u);
    expectContains(mainMenuHeader, "bool popupOpen = false;");
    expectContains(mainMenuHeader, "CreateMenu createMenu_;");
    expectContains(mainMenu, "result.popupOpen");
    expectContains(mainMenu, "result.popupOpen = toolsMenu_.draw(context) || result.popupOpen;");
    expectContains(mainMenu, "result.popupOpen = createMenu_.draw(context) || result.popupOpen;");
    EXPECT_LT(
            mainMenu.find("result.popupOpen = toolsMenu_.draw(context) || result.popupOpen;"),
            mainMenu.find("result.popupOpen = createMenu_.draw(context) || result.popupOpen;"));
    EXPECT_LT(
            mainMenu.find("result.popupOpen = createMenu_.draw(context) || result.popupOpen;"),
            mainMenu.find("result.popupOpen = viewMenu_.draw(context, result) || result.popupOpen;"));
    expectContains(mainMenu, "ui.menu.create.point_light");
    expectContains(mainMenu, "QuaderCommand::SceneCreatePointLight");
    expectContains(mainMenu, "ui.menu.create.directional_light");
    expectContains(mainMenu, "QuaderCommand::SceneCreateDirectionalLight");
    expectContains(uiSystemHeader, "closeMenuPopupForPassThroughClick()");
    expectContains(uiSystemHeader, "bool mainMenuPopupOpen_ = false;");
    expectContains(uiSystem, "#include \"ui/imgui_popup_utils.hpp\"");
    expectContains(uiSystem, "closeMenuPopupForPassThroughClick();");
    expectContains(uiSystem, "imguiClosePopupForPassThroughClick(passThroughMin, passThroughMax)");
    expectContains(popupUtils, "ImGui::ClosePopupsExceptModals();");
}

TEST(UiLayoutGuardrailTests, ViewportInputHoverRespectsActiveDockingAndPanelItems) {
    const std::string viewportPanel = readSourceFile({"src", "ui", "viewport_panel.cpp"});
    const std::string paneCollection = sourceSlice(
            viewportPanel,
            "DrawPane drawPane{",
            "drawPanes.push_back(drawPane);");

    expectContains(paneCollection, ".hovered = ImGui::IsItemHovered(),");
    expectMissing(paneCollection, "ImGuiHoveredFlags_AllowWhenBlockedByActiveItem");
}

TEST(UiLayoutGuardrailTests, ViewportChromeCombosSharePopupPassThroughClickHandling) {
    const std::string viewportPanel = readSourceFile({"src", "ui", "viewport_panel.cpp"});
    const std::string chrome = sourceSlice(
            viewportPanel,
            "bool ViewportPanel::drawPaneChrome",
            "void ViewportPanel::setPaneView");

    expectContains(viewportPanel, "#include \"ui/imgui_popup_utils.hpp\"");
    expectContains(chrome, "imguiClosePopupForPassThroughClick(chromeRect.Min, chromeRect.Max)");
    expectContains(chrome, "ImGui::BeginCombo(\"##ViewportView\"");
    expectContains(chrome, "ImGui::BeginCombo(\"##ViewportShading\"");
}
