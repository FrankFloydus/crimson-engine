////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_context_menu_presenter.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "viewport/viewport_input_router.hpp"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

bool is_generic_context_action(std::string_view action_id) {
  return action_id == "delete_selection" || action_id == "duplicate_meshes";
}

bool should_close_operations_popup_for_mouse_click() {
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
      ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    return true;
  }
  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    return false;
  }
  return !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                 ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

} // namespace

void ViewportContextMenuPresenter::draw_operations_popup(
    QuaderEditorApi &editor, ModelingSessionService &modeling,
    ViewportInputRouter &input_router, LocalizationService &localization,
    bool modal_tool_owns_viewport) const {
  if (!ImGui::BeginPopup("ViewportOperationsContext")) {
    return;
  }

  if (should_close_operations_popup_for_mouse_click()) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }

  const ModelingActionQuery query{
      .surface = ModelingActionSurface::ViewportContextMenu,
      .modal_tool_owns_viewport = modal_tool_owns_viewport,
      .include_disabled = true,
      .include_hidden = false,
  };
  const std::vector<ModelingActionItem> actions =
      modeling.context_actions(query);
  bool separated_generic_actions = false;
  for (const ModelingActionItem &action : actions) {
    if (!separated_generic_actions &&
        is_generic_context_action(action.action_id)) {
      ImGui::Separator();
      separated_generic_actions = true;
    }
    const bool enabled = action.visibility == ModelingActionVisibility::Enabled;
    std::string label_key = "modeling.action.";
    label_key.append(action.action_id);
    label_key.append(".label");
    std::string stable_id = "modeling.action.";
    stable_id.append(action.action_id);
    const std::string label =
        localization.imgui_label(label_key, stable_id, action.label);
    if (ImGui::MenuItem(label.c_str(), nullptr, false, enabled)) {
      const std::string command_id =
          action.kind == ModelingActionKind::Tool
              ? editor.commands().command_id_for_tool_activation(action.tool_id)
              : editor.commands().command_id_for_modeling_action(
                    action.action_id);
      const QuaderApiVoidResult result = editor.commands().execute(command_id);
      if (!result.ok) {
        LOG_ERROR("Operation '{}' failed: {}", action.action_id,
                  result.message.empty() ? "Operation failed."
                                         : result.message);
      }
      input_router.set_selection_mode(app_selection_mode_to_viewport(
          to_modeling_selection_mode(editor.selection().summary().mode)));
      ImGui::CloseCurrentPopup();
    }
    if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !action.disabled_reason.empty()) {
      ImGui::SetTooltip("%s", action.disabled_reason.c_str());
    }
  }

  ImGui::EndPopup();
}

} // namespace quader
