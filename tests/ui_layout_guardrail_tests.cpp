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

using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_slice;

void expect_contains(std::string_view source, std::string_view needle) {
  EXPECT_NE(source.find(needle), std::string_view::npos) << needle;
}

void expect_missing(std::string_view source, std::string_view needle) {
  EXPECT_EQ(source.find(needle), std::string_view::npos) << needle;
}

std::size_t count_occurrences(std::string_view source,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while ((offset = source.find(needle, offset)) != std::string_view::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

void expect_continuous_helper_live_applies(std::string_view source,
                                           std::string_view helper) {
  const std::size_t helper_count = count_occurrences(source, helper);
  EXPECT_GT(helper_count, 0u) << helper;
  EXPECT_GE(count_occurrences(source, ".should_apply()"), helper_count)
      << helper;
  EXPECT_GE(count_occurrences(source, ".merge_key"), helper_count) << helper;
}

} // namespace

TEST(UiLayoutGuardrailTests, SidebarPanelsDoNotForceEmptyVerticalScrollbars) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string outliner =
      read_source_file({"src", "ui", "outliner_panel.cpp"});

  const std::string inspector_flags =
      source_slice(inspector, "ImGuiWindowFlags InspectorPanel::window_flags",
                   "void InspectorPanel::draw_content");
  const std::string outliner_flags =
      source_slice(outliner, "ImGuiWindowFlags OutlinerPanel::window_flags",
                   "void OutlinerPanel::draw_content");

  expect_contains(inspector_flags, "return context.style.panel_window_flags;");
  expect_contains(outliner_flags, "return context.style.panel_window_flags;");
  expect_missing(inspector_flags, "ImGuiWindowFlags_AlwaysVerticalScrollbar");
  expect_missing(outliner_flags, "ImGuiWindowFlags_AlwaysVerticalScrollbar");
}

TEST(UiLayoutGuardrailTests,
     InspectorTransformAxisInputsStretchInsideSidebarWidth) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string axis_drag_float = source_slice(
      inspector, "AxisDragFloatResult axis_drag_float(const char *axis",
      "axis_drag_float3(const char *stable_id");
  const std::string axis_drag_float3 =
      source_slice(inspector, "axis_drag_float3(const char *stable_id",
                   "bool input_text_with_left_label");

  expect_contains(axis_drag_float, "input_width < 0.0f");
  expect_contains(axis_drag_float3,
                  "inspector_property_row(stable_id, label);");
  expect_contains(axis_drag_float3, "ImGuiTableFlags_SizingStretchProp");
  expect_contains(axis_drag_float3, "ImGuiTableColumnFlags_WidthStretch");
  expect_contains(axis_drag_float3, "min_axis_column_width");
  expect_contains(axis_drag_float3, "kMinAxisInputWidth");
  EXPECT_EQ(count_occurrences(axis_drag_float3, "-FLT_MIN"), 3u);
  expect_missing(axis_drag_float3, "ImGuiTableFlags_SizingFixedFit");
}

TEST(UiLayoutGuardrailTests,
     InspectorPropertiesUseFramelessDefaultControls) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});

  expect_contains(inspector, "bool begin_inspector_property_table");
  expect_contains(inspector, "ImGui::BeginTable(\"##PropertyTable\"");
  expect_missing(inspector, "ImGuiTableFlags_Resizable");
  expect_missing(inspector, "ImGuiTableFlags_RowBg");
  expect_missing(inspector, "ImGuiTableFlags_BordersOuter");
  expect_missing(inspector, "ImGuiTableFlags_BordersInnerV");
  expect_contains(inspector, "ImGui::TableSetupColumn(\"Label\"");
  expect_contains(inspector, "ImGui::TableSetupColumn(\"Value\"");
  expect_contains(inspector, "ImGui::CollapsingHeader(");
  expect_contains(inspector,
                  "begin_inspector_property_table(\"inspector_properties\"");
  expect_contains(inspector, "inspector_property_section(\"general\"");
  expect_contains(inspector, "inspector_property_section(\"transform\"");
  expect_contains(inspector, "inspector_property_section(\"light\"");
  expect_missing(inspector, "quader_begin_property_table");
  expect_missing(inspector, "quader_property_section");
  expect_missing(inspector, "begin_labeled_control_table");
}

TEST(UiLayoutGuardrailTests, NumericSlidersUseDefaultImGuiWidgets) {
  const std::string viewport_settings =
      read_source_file({"src", "ui", "viewport_settings_panel.cpp"});
  const std::string tool_settings =
      read_source_file({"src", "ui", "tool_settings_panel.cpp"});
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string continuous_float_helper = source_slice(
      inspector, "UIContinuousEditResult drag_float_with_left_label",
      "UIContinuousEditResult color_edit3_with_left_label");
  const std::string axis_float_helper = source_slice(
      inspector, "AxisDragFloatResult axis_drag_float(const char *axis",
      "axis_drag_float3(const char *stable_id");

  expect_contains(viewport_settings, "ImGui::SliderFloat(");
  expect_contains(viewport_settings, "ImGuiSliderFlags_AlwaysClamp");
  expect_contains(tool_settings, "ImGui::SliderFloat(");
  expect_contains(tool_settings, "ImGui::SliderInt(");
  expect_contains(tool_settings, "ImGuiSliderFlags_AlwaysClamp");
  expect_missing(tool_settings, "ImGui::DragFloat(");
  expect_contains(continuous_float_helper, "min_value < max_value");
  expect_contains(continuous_float_helper, "ImGui::SliderFloat");
  expect_contains(continuous_float_helper, "ImGui::DragFloat");
  expect_contains(axis_float_helper, "ImGui::DragFloat");
  expect_missing(axis_float_helper, "quader_slider_float");
  expect_missing(inspector, "imgui_quader_widgets");
  expect_missing(inspector, "quader_slider_float");
  expect_missing(viewport_settings, "quader_slider_float(");
  expect_missing(tool_settings, "quader_slider_float(");
  expect_missing(tool_settings, "quader_slider_int(");
  expect_missing(inspector, "0.5f, 0.0f, 360.0f");
}

TEST(UiLayoutGuardrailTests, OutlinerUsesDefaultTreeHierarchyLines) {
  const std::string outliner =
      read_source_file({"src", "ui", "outliner_panel.cpp"});

  expect_contains(outliner, "ImGuiTreeNodeFlags_DrawLinesFull");
  expect_contains(outliner,
                  "ImGui::TreeNodeEx(record.name.c_str(), tree_flags)");
  expect_missing(outliner, "kOutlinerTreeIndent");
  expect_missing(outliner, "ImGuiStyleVar_IndentSpacing");
  expect_missing(outliner, "draw_hierarchy_guides");
  expect_missing(outliner, "ancestor_has_next");
  expect_missing(outliner, "has_next_sibling ? row_max.y + 1.0f");
}

TEST(UiLayoutGuardrailTests, InspectorLightEditsRouteThroughPublicSceneApi) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string light_section = source_slice(
      inspector,
      "if (const LightComponent *current_light = document.light(primary))",
      "if (selected.size() > 1u)");

  expect_contains(light_section,
                  "context.intent.editor.scene().set_light_component");
  expect_contains(light_section, "to_api_light_component");
  expect_contains(inspector, "constexpr float kInspectorLightRangeMin = 0.1f");
  expect_contains(inspector,
                  "constexpr float kInspectorLightRangeSliderMax = 100.0f");
  expect_contains(light_section, "kInspectorLightRangeMin");
  expect_contains(light_section, "kInspectorLightRangeSliderMax");
  expect_contains(light_section, "ImGuiSliderFlags_None");
  expect_missing(light_section, "10000.0f");
  expect_missing(inspector, "document.set_light(");
}

TEST(UiLayoutGuardrailTests, InspectorSceneSettingsEditThroughPublicSceneApi) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string scene_settings_section = source_slice(
      inspector, "void draw_scene_settings_inspector", "struct BoolAggregate");

  expect_contains(scene_settings_section,
                  "context.intent.editor.scene().set_scene_settings");
  expect_contains(scene_settings_section, "to_api_scene_settings");
  expect_contains(scene_settings_section, "environment_clear_color");
  expect_contains(scene_settings_section, "ui.inspector.clear_color");
  expect_contains(scene_settings_section, "post_process_tone_mapping");
  expect_contains(scene_settings_section, "post_process_dithering");
  expect_contains(scene_settings_section, "camera_near");
  expect_contains(scene_settings_section, "camera_far");
  expect_missing(inspector, "document.set_scene_settings(");
}

TEST(UiLayoutGuardrailTests, InspectorSectionsStartCollapsed) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});

  expect_contains(inspector, "inspector_property_section(");
  expect_contains(inspector, "ImGui::CollapsingHeader(");
  expect_missing(inspector, "quader_collapsing_header(");
  expect_missing(inspector, "ImGuiTreeNodeFlags_DefaultOpen");
}

TEST(UiLayoutGuardrailTests, OutlinerNonLeafNodesStartExpanded) {
  const std::string outliner =
      read_source_file({"src", "ui", "outliner_panel.cpp"});
  const std::string tree_node_setup =
      source_slice(outliner, "ImGuiTreeNodeFlags tree_flags",
                   "const bool open = ImGui::TreeNodeEx");

  expect_contains(tree_node_setup, "record.has_children");
  expect_contains(tree_node_setup, "ImGuiTreeNodeFlags_DefaultOpen");
}

TEST(UiLayoutGuardrailTests, OutlinerCachesTreeRecords) {
  const std::string outliner_header =
      read_source_file({"src", "ui", "outliner_panel.hpp"});
  const std::string outliner =
      read_source_file({"src", "ui", "outliner_panel.cpp"});

  expect_contains(outliner_header, "std::unique_ptr<TreeCache> tree_cache_");
  expect_contains(outliner, "cache.scene_revision == document.revision()");
  expect_contains(outliner,
                  "cache.records = document.entity_records_tree_order()");
  expect_contains(
      outliner, "cache.children_by_entity[record.parent].push_back(record.id)");
  expect_missing(outliner, "ImGui::IsRectVisible");
  expect_missing(outliner, "if (row_visible)");
}

TEST(UiLayoutGuardrailTests, InspectorContinuousInputsLiveApplyWithMergeKeys) {
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});
  const std::string inspector_header =
      read_source_file({"src", "ui", "inspector_panel.hpp"});
  const std::string tracker =
      read_source_file({"src", "ui", "ui_continuous_edit_tracker.hpp"});
  const std::string continuous_helpers =
      source_slice(inspector, "void update_continuous_edit_tracker",
                   "template <typename Enum>");
  const std::string scene_settings_section = source_slice(
      inspector, "void draw_scene_settings_inspector", "struct BoolAggregate");
  const std::string light_section = source_slice(
      inspector,
      "if (const LightComponent *current_light = document.light(primary))",
      "if (selected.size() > 1u)");

  expect_contains(inspector_header,
                  "UIContinuousEditTracker active_continuous_edit_");
  expect_contains(tracker, "class UIContinuousEditTracker");
  expect_contains(tracker, "std::string merge_key");
  expect_contains(tracker, "bool should_apply() const");
  expect_contains(tracker, "ensure_merge_key");
  expect_contains(continuous_helpers, "ImGui::IsItemDeactivatedAfterEdit()");
  expect_contains(continuous_helpers, "active_edit.stage");
  expect_contains(continuous_helpers, "active_edit.ensure_merge_key");
  expect_missing(continuous_helpers, "result.commit");
  expect_contains(scene_settings_section,
                  "QuaderSceneEditOptions{.merge_key = std::move(merge_key)}");
  expect_contains(light_section,
                  "QuaderSceneEditOptions{.merge_key = std::move(merge_key)}");

  expect_continuous_helper_live_applies(scene_settings_section,
                                        "drag_float_with_left_label(");
  expect_continuous_helper_live_applies(light_section,
                                        "drag_float_with_left_label(");
  expect_continuous_helper_live_applies(light_section,
                                        "color_edit3_with_left_label(");
}

TEST(UiLayoutGuardrailTests, CustomChromeKeepsCurrentLogoAndTopMenuSpacing) {
  const std::string ui_system =
      read_source_file({"src", "ui", "ui_system.cpp"});
  const std::string chrome =
      source_slice(ui_system, "UISystem::draw_window_chrome",
                   "void UISystem::draw_dockspace");

  expect_contains(ui_system, "constexpr float kChromeLogoSize = 18.0f;");
  expect_contains(ui_system, "constexpr float kChromeLogoLeft = 16.0f;");
  expect_contains(ui_system, "constexpr float kChromeMenuLeft = 48.0f;");
  expect_contains(ui_system, "constexpr float kChromeMenuSpacing = 10.0f;");
  expect_contains(chrome, "ImGui::SetCursorPos({kChromeMenuLeft, 0.0f});");
  expect_contains(chrome, "ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, "
                          "{kChromeMenuSpacing, 0.0f});");
  expect_contains(chrome, "ImGui::GetForegroundDrawList()->AddImage(icons_."
                          "logo, logo_min, logo_max);");
}

TEST(UiLayoutGuardrailTests, StaticBottomBarTextCentersAboveAccentStrip) {
  const std::string ui_system =
      read_source_file({"src", "ui", "ui_system.cpp"});
  const std::string bottom_bar = source_slice(
      ui_system, "void draw_static_bottom_bar", "UISystem::~UISystem");

  expect_contains(bottom_bar,
                  "kStaticBottomBarHeight - kStaticBottomBarAccentHeight");
  expect_contains(bottom_bar, "ImGui::GetTextLineHeight()");
  expect_contains(
      bottom_bar,
      "status_bar_message(live_status, localization, command_status)");
  expect_contains(ui_system, "modeling.live_status()");
  expect_contains(bottom_bar, "ellipsized_text_to_width");
  expect_missing(bottom_bar, "ui.shell.status.ready");
  expect_missing(bottom_bar, "\"Ready\"");
  expect_missing(bottom_bar, "kStaticBottomBarHeight - ImGui::GetFontSize()");
}

TEST(UiLayoutGuardrailTests, ViewportRuntimeHooksRestoreFrameSelectionCommand) {
  const std::string ui_system =
      read_source_file({"src", "ui", "ui_system.cpp"});
  const std::string viewport_panel_header =
      read_source_file({"src", "ui", "viewport_panel.hpp"});
  const std::string runtime_hooks =
      source_slice(ui_system, "void UISystem::install_runtime_hooks",
                   "editor.ui().set_runtime_hooks");

  expect_contains(runtime_hooks, "editor.viewports().set_runtime_hooks");
  expect_contains(runtime_hooks, ".frame_all =");
  expect_contains(runtime_hooks, "viewport_panel_->frame_all");
  expect_contains(runtime_hooks, ".frame_selection =");
  expect_contains(runtime_hooks, "viewport_panel_->frame_selection");
  expect_contains(
      runtime_hooks,
      "QuaderViewportApiRuntimeAccess::workspace(editor.viewports())");
  expect_contains(viewport_panel_header, "QuaderApiVoidResult frame_all");
  expect_contains(viewport_panel_header, "QuaderApiVoidResult frame_selection");
}

TEST(UiLayoutGuardrailTests,
     MainMenuDropdownsUsePopupPaddingAndVerticalSpacing) {
  const std::string main_menu =
      read_source_file({"src", "ui", "main_menu_bar.cpp"});
  const std::string main_menu_header =
      read_source_file({"src", "ui", "main_menu_bar.hpp"});
  const std::string ui_system =
      read_source_file({"src", "ui", "ui_system.cpp"});
  const std::string ui_system_header =
      read_source_file({"src", "ui", "ui_system.hpp"});
  const std::string popup_utils =
      read_source_file({"src", "ui", "imgui_popup_utils.cpp"});

  expect_contains(main_menu, "constexpr float kMenuPopupPaddingX = 12.0f;");
  expect_contains(main_menu, "constexpr float kMenuPopupPaddingY = 8.0f;");
  expect_contains(main_menu, "constexpr float kMenuPopupFramePaddingY = 5.0f;");
  expect_contains(main_menu, "constexpr float kMenuPopupItemSpacingY = 4.0f;");
  expect_contains(main_menu, "bool begin_menu_with_popup_spacing");
  expect_contains(main_menu, "void end_menu_with_popup_spacing()");
  expect_contains(main_menu, "ImGuiStyleVar_WindowPadding");
  expect_contains(main_menu, "ImGuiStyleVar_FramePadding");
  expect_contains(main_menu, "ImGuiStyleVar_ItemSpacing");
  EXPECT_EQ(count_occurrences(main_menu, "ImGui::BeginMenu("), 1u);
  EXPECT_EQ(count_occurrences(main_menu, "ImGui::EndMenu();"), 1u);
  expect_missing(main_menu, "ImGui::IsPopupOpen(menuId, ImGuiPopupFlags_None)");
  expect_missing(main_menu,
                 "wasOpen && ImGui::IsItemClicked(ImGuiMouseButton_Left)");
  expect_missing(main_menu, "ImGui::CloseCurrentPopup();");
  expect_contains(main_menu, "bool draw_command_menu_item");
  expect_contains(main_menu, "context.editor.commands().available()");
  expect_contains(main_menu, "context.editor.commands().execute(command_id)");
  expect_contains(main_menu, "command->shortcut");
  expect_contains(main_menu, "ui.menu.file.new");
  expect_contains(main_menu, "ui.menu.file.open");
  expect_contains(main_menu, "ui.menu.file.save");
  expect_contains(main_menu, "ui.menu.file.save_as");
  expect_contains(main_menu, "ui.menu.file.export_obj");
  expect_contains(main_menu, "quader_command::kProjectNew");
  expect_contains(main_menu, "quader_command::kProjectOpen");
  expect_contains(main_menu, "quader_command::kProjectSave");
  expect_contains(main_menu, "quader_command::kProjectSaveAs");
  expect_contains(main_menu, "quader_command::kProjectExportSelectionObj");
  expect_contains(main_menu, "quader_command::kEditUndo");
  expect_contains(main_menu, "quader_command::kEditRedo");
  expect_contains(main_menu, "quader_command::kEditRepeatLastAction");
  expect_contains(main_menu, "quader_command::kCopySelection");
  expect_contains(main_menu, "quader_command::kPasteSelection");
  expect_contains(main_menu, "quader_command::kDuplicateSelection");
  expect_contains(main_menu, "quader_command::kModelingFlipHorizontal");
  expect_contains(main_menu, "quader_command::kModelingFlipVertical");
  expect_contains(main_menu, "quader_command::kEditSelectAll");
  expect_contains(main_menu, "quader_command::kEditClearSelection");
  expect_contains(main_menu, "quader_command::kEditInvertSelection");
  expect_contains(main_menu, "quader_command::kDeleteSelection");
  expect_contains(main_menu, "quader_command::kGroupSelection");
  expect_contains(main_menu, "quader_command::kUngroupSelection");
  expect_contains(main_menu, "ui.menu.view.focus_all");
  expect_contains(main_menu, "quader_command::kViewportFocusAll");
  expect_contains(main_menu, "ui.menu.view.focus_selection");
  expect_contains(main_menu, "quader_command::kViewportFrameSelection");
  expect_missing(main_menu, "ui.menu.help");
  expect_missing(main_menu, "Symmetrize");
  expect_missing(main_menu, "kViewportFocusOrigin");
  expect_contains(main_menu, "bool draw_viewport_bool_setting_menu_item");
  expect_contains(main_menu, "ui.menu.edit.snap");
  expect_contains(main_menu, "ui.menu.edit.snap.grid");
  expect_contains(main_menu, "ui.menu.edit.snap.vertex");
  expect_contains(main_menu, "ui.menu.view.grid");
  expect_contains(main_menu, "ui.menu.view.mesh_grid");
  expect_contains(main_menu, "ui.menu.view.gizmos");
  expect_contains(main_menu, "ui.menu.view.overlays");
  expect_contains(main_menu, "ui.menu.view.show_info");
  expect_contains(main_menu, "ViewportBoolSettingId::ShowGrid");
  expect_contains(main_menu, "ViewportBoolSettingId::ShowMeshGrid");
  expect_contains(main_menu, "ViewportBoolSettingId::ShowGizmos");
  expect_contains(main_menu, "ViewportBoolSettingId::ShowOverlays");
  expect_contains(main_menu, "ViewportBoolSettingId::ShowInfo");
  expect_contains(main_menu, "ViewportBoolSettingId::GridSnap");
  expect_contains(main_menu, "ViewportBoolSettingId::VertexSnap");
  expect_contains(main_menu_header, "ViewportSettings &viewport_settings;");
  expect_contains(main_menu_header, "bool viewport_settings_changed = false;");
  expect_contains(ui_system, "save_viewport_settings();");
  expect_contains(main_menu_header, "bool popup_open = false;");
  expect_contains(main_menu_header, "CleanUpMenu cleanup_menu_;");
  expect_contains(main_menu_header, "CreateMenu create_menu_;");
  expect_contains(main_menu, "result.popup_open");
  expect_contains(main_menu, "ui.menu.cleanup");
  expect_contains(main_menu, "ui.menu.cleanup.remove_doubles");
  expect_contains(main_menu, "quader_command::kModelingRemoveDoubles");
  expect_contains(
      main_menu,
      "result.popup_open = cleanup_menu_.draw(context) || result.popup_open;");
  expect_contains(
      main_menu,
      "result.popup_open = tools_menu_.draw(context) || result.popup_open;");
  expect_contains(
      main_menu,
      "result.popup_open = create_menu_.draw(context) || result.popup_open;");
  EXPECT_LT(main_menu.find("result.popup_open = cleanup_menu_.draw(context) || "
                           "result.popup_open;"),
            main_menu.find("result.popup_open = tools_menu_.draw(context) || "
                           "result.popup_open;"));
  EXPECT_LT(main_menu.find("result.popup_open = tools_menu_.draw(context) || "
                           "result.popup_open;"),
            main_menu.find("result.popup_open = create_menu_.draw(context) || "
                           "result.popup_open;"));
  EXPECT_LT(
      main_menu.find("result.popup_open = create_menu_.draw(context) || "
                     "result.popup_open;"),
      main_menu.find("result.popup_open = view_menu_.draw(context, result) || "
                     "result.popup_open;"));
  expect_contains(main_menu, "ui.menu.create.point_light");
  expect_contains(main_menu, "quader_command::kSceneCreatePointLight");
  expect_contains(main_menu, "ui.menu.create.directional_light");
  expect_contains(main_menu, "quader_command::kSceneCreateDirectionalLight");
  expect_contains(ui_system_header,
                  "close_menu_popup_for_pass_through_click()");
  expect_contains(ui_system_header, "bool main_menu_popup_open_ = false;");
  expect_contains(ui_system, "#include \"ui/imgui_popup_utils.hpp\"");
  expect_contains(ui_system, "close_menu_popup_for_pass_through_click();");
  expect_contains(ui_system, "const float menu_click_exclusion_height =");
  expect_contains(
      ui_system,
      "custom_chrome_ ? kWindowChromeHeight : ImGui::GetFrameHeight();");
  expect_contains(ui_system,
                  "mouse.y <= viewport->Pos.y + menu_click_exclusion_height");
  expect_contains(ui_system, "viewport->Pos.y + menu_click_exclusion_height");
  expect_contains(ui_system,
                  "imgui_close_popup_for_pass_through_click(pass_through_min");
  expect_contains(popup_utils, "ImGui::ClosePopupsExceptModals();");
}

TEST(UiLayoutGuardrailTests,
     ViewportInputHoverRespectsActiveDockingAndPanelItems) {
  const std::string viewport_panel =
      read_source_file({"src", "ui", "viewport_panel.cpp"});
  const std::string pane_collection =
      source_slice(viewport_panel, "DrawPane draw_pane{",
                   "draw_panes.push_back(draw_pane);");

  expect_contains(pane_collection, ".hovered = ImGui::IsItemHovered(),");
  expect_missing(pane_collection,
                 "ImGuiHoveredFlags_AllowWhenBlockedByActiveItem");
}

TEST(UiLayoutGuardrailTests,
     ViewportUsesRendererCapacityUvsDuringPanelSplitterDrag) {
  const std::string viewport_panel_header =
      read_source_file({"src", "ui", "viewport_panel.hpp"});
  const std::string viewport_panel =
      read_source_file({"src", "ui", "viewport_panel.cpp"});
  const std::string pane_image = source_slice(
      viewport_panel, "const QVec2 render_size", "DrawPane draw_pane{");

  expect_contains(
      viewport_panel,
      "should_defer_pane_render_resize(const UIContext &ui_context,");
  expect_contains(viewport_panel, "const ViewportRenderer &renderer)");
  expect_contains(viewport_panel,
                  "ui_context.viewport->render_resize_suspended");
  expect_contains(viewport_panel,
                  "renderer.texture_handle() != kInvalidRenderTextureHandle");
  expect_contains(
      viewport_panel,
      "renderer.overlay_texture_handle() != kInvalidRenderTextureHandle");
  expect_contains(pane_image, "viewport_workspace.set_pane_render_size("
                              "placement.pane_id, render_size)");
  expect_contains(pane_image,
                  "pane_state.renderer.resize(render_width, render_height);");
  expect_contains(
      pane_image,
      "if (!should_defer_pane_render_resize(ui_context, pane_state.renderer))");
  expect_contains(pane_image, "pane_state.renderer.texture_max_v()");
  expect_contains(pane_image, "pane_state.renderer.texture_max_u()");
  expect_contains(pane_image, "texture_uv_min");
  expect_contains(pane_image, "texture_uv_max");
  expect_missing(pane_image, "activeRenderSize");
  expect_missing(pane_image, "viewportPaneTextureUvRect(");
  expect_missing(pane_image,
                 "if (!ui_context.viewport->render_resize_suspended)");
}

TEST(UiLayoutGuardrailTests,
     SidebarDockMinimumsCompressBeforeViewportCollapses) {
  const std::string ui_system =
      read_source_file({"src", "ui", "ui_system.cpp"});
  const std::string ui_system_header =
      read_source_file({"src", "ui", "ui_system.hpp"});
  const std::string outliner =
      read_source_file({"src", "ui", "outliner_panel.cpp"});
  const std::string inspector =
      read_source_file({"src", "ui", "inspector_panel.cpp"});

  expect_contains(ui_system, "kCompressedSidebarMinWidth = 220.0f");
  expect_contains(ui_system, "kReservedViewportDockWidth = 300.0f");
  expect_contains(ui_system,
                  "visible_sidebar_count(const UIPanelManager &panels)");
  expect_contains(ui_system,
                  "effective_sidebar_minimum_width(float dockspace_width,");
  expect_contains(ui_system, "const UIPanelManager &panels,");
  expect_contains(ui_system, "dockspace_width - kReservedViewportDockWidth");
  expect_contains(ui_system, "std::min(preferred_minimum_width,");
  expect_contains(ui_system,
                  "std::max(kCompressedSidebarMinWidth, width_per_sidebar)");
  expect_contains(
      ui_system,
      "frame_style.sidebar_min_width = effective_sidebar_minimum_width");
  expect_contains(ui_system, "draw_dockspace(editor, quit_requested, document, "
                             "modeling,");
  expect_contains(
      ui_system,
      "enforce_sidebar_dock_minimums(frame_style.sidebar_min_width)");
  expect_contains(
      ui_system_header,
      "draw_dockspace(QuaderEditorApi &editor, bool &quit_requested,");
  expect_contains(ui_system_header,
                  "SceneDocument &document, ModelingSessionService &modeling,");
  expect_contains(ui_system_header,
                  "enforce_sidebar_dock_minimums(float sidebar_minimum_width)");
  expect_contains(outliner, "return context.style.sidebar_min_width");
  expect_contains(inspector, "return context.style.sidebar_min_width");
  expect_missing(ui_system, "clamp_docked_window_width(ui_panel_key(UIPanelId::"
                            "Outliner), style_.sidebar_min_width)");
  expect_missing(
      ui_system,
      "ImVec2(std::max(saved_window_min_size.x, style_.sidebar_min_width)");
}

TEST(UiLayoutGuardrailTests,
     ViewportChromeCombosSharePopupPassThroughClickHandling) {
  const std::string viewport_panel =
      read_source_file({"src", "ui", "viewport_panel.cpp"});
  const std::string popup_utils =
      read_source_file({"src", "ui", "imgui_popup_utils.cpp"});
  const std::string chrome =
      source_slice(viewport_panel, "bool ViewportPanel::draw_pane_chrome",
                   "void ViewportPanel::set_pane_view");

  expect_contains(viewport_panel, "#include \"ui/imgui_popup_utils.hpp\"");
  expect_missing(viewport_panel, "imgui_quader_widgets");
  expect_contains(popup_utils, "ImGuiMouseButton_Middle");
  expect_contains(popup_utils, "open_combo_popup_exists");
  expect_contains(popup_utils, "\"##Combo_\"");
  expect_contains(popup_utils,
                  "!screen_point_inside(mouse, target_min, target_max)");
  expect_contains(chrome, "(void)imgui_close_combo_popup_for_click_inside(");
  expect_contains(chrome,
                  "item_min, item_max, chrome_rect.Min, chrome_rect.Max");
  expect_missing(chrome, "imgui_close_popup_for_pass_through_click(chrome_rect."
                         "Min, chrome_rect.Max)");
  expect_missing(
      chrome,
      "imguiClosePopupForClickOutside(chrome_rect.Min, chrome_rect.Max)");
  expect_missing(
      chrome,
      "imguiCloseComboPopupForClickOutside(chrome_rect.Min, chrome_rect.Max)");
  expect_contains(chrome,
                  "ImGui::BeginCombo(\"##ViewportView\", preview.c_str(),");
  expect_contains(chrome, "ImGuiComboFlags_HeightSmall)");
  expect_contains(
      chrome,
      "ImGui::BeginCombo(\"##ViewportShading\", shading_preview.c_str(),");
  expect_missing(chrome, "ImGuiComboFlags_HeightSmall, false");
  expect_missing(chrome, "consumes_input = true;\n    }\n\n    "
                         "ImGui::SetCursorScreenPos(combo_pos)");
}
