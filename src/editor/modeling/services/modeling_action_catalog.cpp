////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_action_catalog.hpp"

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <algorithm>
#include <utility>

#include <editor/metadata/host_contract.hpp>

namespace quader {
namespace {

ModelingActionItem edit_action(std::string action_id, std::string operation_id,
                               std::string label,
                               ModelingActionVisibility visibility,
                               std::string disabled_reason_key = {},
                               std::uint32_t disabled_reason_count = 0) {
  return {
      .action_id = std::move(action_id),
      .operation_id = std::move(operation_id),
      .label = std::move(label),
      .kind = ModelingActionKind::Operation,
      .visibility = visibility,
      .disabled_reason_key = std::move(disabled_reason_key),
      .disabled_reason_count = disabled_reason_count,
      .rebuilds_mesh_on_success = true,
  };
}

std::string minimum_count_reason_key(std::string_view noun,
                                     std::uint32_t required) {
  std::string key("modeling.action.disabled.minimum_");
  key.append(noun);
  if (required != 1U) {
    key.push_back('s');
  }
  return key;
}

ModelingActionAvailabilityResult disabled_reason(std::string key,
                                                 std::uint32_t count = 0) {
  return {
      .visibility = ModelingActionVisibility::Disabled,
      .disabled_reason_key = std::move(key),
      .disabled_reason_count = count,
  };
}

std::string surface_id_for(ModelingActionSurface surface) {
  switch (surface) {
  case ModelingActionSurface::ViewportContextMenu:
    return "viewport_context_menu";
  case ModelingActionSurface::OperationsPanel:
    return "operations_top_bar";
  }
  return {};
}

void append_if_visible(std::vector<ModelingActionItem> &actions,
                       ModelingActionItem item,
                       const ModelingActionQuery &query) {
  if (item.visibility == ModelingActionVisibility::Hidden &&
      !query.include_hidden) {
    return;
  }
  if (item.visibility == ModelingActionVisibility::Disabled &&
      !query.include_disabled) {
    return;
  }
  actions.push_back(std::move(item));
}

void append_edit_actions(std::vector<ModelingActionItem> &actions,
                         const ModelingSelectionSummary &summary,
                         const ModelingActionQuery &query) {
  append_if_visible(
      actions,
      edit_action("delete_selection", "delete_selection", "Delete",
                  summary.has_selection ? ModelingActionVisibility::Enabled
                                        : ModelingActionVisibility::Disabled,
                  summary.has_selection
                      ? std::string{}
                      : "modeling.action.disabled.delete_selection"),
      query);

  const bool object_mode = summary.mode == ModelingSelectionMode::Object;
  append_if_visible(
      actions,
      edit_action("duplicate_meshes", "duplicate_meshes", "Duplicate",
                  object_mode ? (summary.selected_object_count > 0U
                                     ? ModelingActionVisibility::Enabled
                                     : ModelingActionVisibility::Disabled)
                              : ModelingActionVisibility::Hidden,
                  summary.selected_object_count > 0U
                      ? std::string{}
                      : "modeling.action.disabled.minimum_object",
                  1),
      query);
}

ModelingActiveSelectionKind
from_host_active_kind(quader_host_contract::HostActiveSelectionKind kind) {
  switch (kind) {
  case quader_host_contract::HostActiveSelectionKind::Object:
    return ModelingActiveSelectionKind::Object;
  case quader_host_contract::HostActiveSelectionKind::Vertex:
    return ModelingActiveSelectionKind::Vertex;
  case quader_host_contract::HostActiveSelectionKind::Edge:
    return ModelingActiveSelectionKind::Edge;
  case quader_host_contract::HostActiveSelectionKind::Face:
    return ModelingActiveSelectionKind::Face;
  case quader_host_contract::HostActiveSelectionKind::None:
    return ModelingActiveSelectionKind::None;
  }
  return ModelingActiveSelectionKind::None;
}

ModelingActionKind from_host_kind(quader_host_contract::HostActionKind kind) {
  return kind == quader_host_contract::HostActionKind::Tool
             ? ModelingActionKind::Tool
             : ModelingActionKind::Operation;
}

const quader_host_contract::HostOperationPanelDescriptor *
panel_for_mode(ModelingSelectionMode mode) {
  const quader_host_contract::HostSelectionMode host_mode =
      app_selection_mode_to_host_contract(mode);
  for (const quader_host_contract::HostOperationPanelDescriptor &panel :
       quader_host_contract::operation_panel_catalog()) {
    if (panel.selection_mode == host_mode) {
      return &panel;
    }
  }
  return nullptr;
}

bool selection_requirement_satisfied(
    const ModelingSelectionSummary &summary,
    const ModelingActionAvailabilityRule &rule) {
  switch (rule.requirement) {
  case ModelingActionSelectionRequirement::AnySelection:
    return summary.has_selection;
  case ModelingActionSelectionRequirement::SelectionMode:
    return summary.mode == rule.mode;
  case ModelingActionSelectionRequirement::NonObjectSelection:
    return summary.has_selection &&
           summary.mode != ModelingSelectionMode::Object;
  case ModelingActionSelectionRequirement::ObjectOrFaceSelection:
    return (summary.mode == ModelingSelectionMode::Object &&
            summary.has_selection) ||
           (summary.mode == ModelingSelectionMode::Face &&
            summary.selected_face_count > 0U);
  case ModelingActionSelectionRequirement::EdgeOrFaceSelection:
    return (summary.mode == ModelingSelectionMode::Edge &&
            summary.selected_edge_count > 0U) ||
           (summary.mode == ModelingSelectionMode::Face &&
            summary.selected_face_count > 0U);
  }
  return false;
}

ModelingActionAvailabilityRule rule_from_host_availability(
    const quader_host_contract::HostActionAvailabilityDescriptor
        &availability) {
  return {
      .requirement = ModelingActionSelectionRequirement::SelectionMode,
      .mode = host_selection_mode_to_app(availability.selection_mode),
      .minimum_selected_vertices = availability.minimum_selected_vertices,
      .minimum_selected_edges = availability.minimum_selected_edges,
      .exact_selected_edges = availability.exact_selected_edges,
      .minimum_selected_faces = availability.minimum_selected_faces,
      .minimum_selected_objects = availability.minimum_selected_objects,
      .required_active_kind =
          from_host_active_kind(availability.required_active_kind),
      .requires_single_object = availability.requires_single_object,
  };
}

ModelingActionAvailabilityResult
evaluate_availability_rule(const ModelingSelectionSummary &summary,
                           const ModelingActionAvailabilityRule &rule) {
  ModelingActionAvailabilityResult result;
  if (!selection_requirement_satisfied(summary, rule)) {
    result.visibility =
        rule.requirement == ModelingActionSelectionRequirement::SelectionMode
            ? ModelingActionVisibility::Hidden
            : ModelingActionVisibility::Disabled;
    if (result.visibility == ModelingActionVisibility::Disabled) {
      result.disabled_reason_key = "modeling.action.disabled.any_selection";
    }
    return result;
  }
  if (summary.selected_vertex_count < rule.minimum_selected_vertices) {
    return disabled_reason(
        minimum_count_reason_key("vertex", rule.minimum_selected_vertices),
        rule.minimum_selected_vertices);
  }
  if (summary.selected_edge_count < rule.minimum_selected_edges) {
    return disabled_reason(
        minimum_count_reason_key("edge", rule.minimum_selected_edges),
        rule.minimum_selected_edges);
  }
  if (rule.exact_selected_edges != 0U &&
      summary.selected_edge_count != rule.exact_selected_edges) {
    return disabled_reason("modeling.action.disabled.exact_edges",
                           rule.exact_selected_edges);
  }
  if (summary.selected_face_count < rule.minimum_selected_faces) {
    return disabled_reason(
        minimum_count_reason_key("face", rule.minimum_selected_faces),
        rule.minimum_selected_faces);
  }
  if (summary.selected_object_count < rule.minimum_selected_objects) {
    return disabled_reason(
        minimum_count_reason_key("object", rule.minimum_selected_objects),
        rule.minimum_selected_objects);
  }
  if (rule.required_active_kind != ModelingActiveSelectionKind::None &&
      summary.active_kind != rule.required_active_kind) {
    return disabled_reason("modeling.action.disabled.active_element");
  }
  if (rule.requires_single_object && summary.selected_object_count != 1U) {
    return disabled_reason("modeling.action.disabled.single_object");
  }
  result.visibility = ModelingActionVisibility::Enabled;
  return result;
}

std::optional<ModelingActionAvailabilityResult>
evaluate_host_operation_availability(const ModelingSelectionSummary &summary,
                                     std::string_view operation_id,
                                     ModelingSelectionMode mode) {
  const quader_host_contract::HostSelectionMode host_mode =
      app_selection_mode_to_host_contract(mode);
  std::optional<ModelingActionAvailabilityResult> first_unavailable;
  for (const quader_host_contract::HostActionDescriptor &descriptor :
       quader_host_contract::action_catalog()) {
    if (descriptor.kind != quader_host_contract::HostActionKind::Operation ||
        descriptor.operation_id != operation_id ||
        descriptor.availability.selection_mode != host_mode) {
      continue;
    }

    ModelingActionAvailabilityResult result = evaluate_availability_rule(
        summary, rule_from_host_availability(descriptor.availability));
    if (result.visibility == ModelingActionVisibility::Enabled) {
      return result;
    }
    if (!first_unavailable.has_value() ||
        (first_unavailable->visibility == ModelingActionVisibility::Hidden &&
         result.visibility == ModelingActionVisibility::Disabled)) {
      first_unavailable = std::move(result);
    }
  }
  if (first_unavailable.has_value()) {
    return first_unavailable;
  }
  return std::nullopt;
}

ModelingActionItem action_from_host_descriptor(
    const ModelingSelectionSummary &summary,
    const quader_host_contract::HostActionDescriptor &descriptor) {
  ModelingActionAvailabilityResult availability = evaluate_availability_rule(
      summary, rule_from_host_availability(descriptor.availability));
  return {
      .action_id = std::string(descriptor.id),
      .operation_id = std::string(descriptor.operation_id),
      .tool_id = std::string(descriptor.tool_id),
      .label = std::string(descriptor.label),
      .kind = from_host_kind(descriptor.kind),
      .preview_kind = ModelingActionCatalog::preview_kind_from_host(
          descriptor.preview_kind),
      .visibility = availability.visibility,
      .disabled_reason = std::move(availability.disabled_reason),
      .disabled_reason_key = std::move(availability.disabled_reason_key),
      .disabled_reason_count = availability.disabled_reason_count,
      .rebuilds_mesh_on_success =
          descriptor.kind == quader_host_contract::HostActionKind::Operation,
  };
}

void append_host_panel_actions(
    std::vector<ModelingActionItem> &actions,
    const quader_host_contract::HostOperationPanelDescriptor &panel,
    const ModelingSelectionSummary &summary, const ModelingActionQuery &query) {
  for (std::string_view action_id : panel.action_ids) {
    const quader_host_contract::HostActionDescriptor *descriptor =
        quader_host_contract::find_action_descriptor(action_id);
    if (descriptor == nullptr || !descriptor->show_in_operations_panel) {
      continue;
    }
    append_if_visible(actions,
                      action_from_host_descriptor(summary, *descriptor), query);
  }
}

} // namespace

ModelingActionSet
ModelingActionCatalog::action_set_for(const ModelingSelectionSummary &summary,
                                      const ModelingActionQuery &query) {
  ModelingActionSet result;
  result.surface = query.surface;
  result.surface_id = surface_id_for(query.surface);
  if (query.modal_tool_owns_viewport) {
    return result;
  }

  if (query.surface == ModelingActionSurface::ViewportContextMenu) {
    ModelingActionPanel panel;
    panel.panel_id = "viewport_context_menu";
    panel.label = "Context Actions";
    panel.selection_mode_id = std::string(host_selection_mode_id(
        app_selection_mode_to_host_contract(summary.mode)));
    panel.visibility = ModelingActionVisibility::Enabled;

    if (const quader_host_contract::HostOperationPanelDescriptor *host_panel =
            panel_for_mode(summary.mode)) {
      append_host_panel_actions(panel.actions, *host_panel, summary, query);
    }
    append_edit_actions(panel.actions, summary, query);
    if (!panel.actions.empty()) {
      result.panels.push_back(std::move(panel));
    }
    return result;
  }

  const quader_host_contract::HostSelectionMode active_mode =
      app_selection_mode_to_host_contract(summary.mode);
  for (const quader_host_contract::HostOperationPanelDescriptor &host_panel :
       quader_host_contract::operation_panel_catalog()) {
    const bool active_panel = host_panel.selection_mode == active_mode;
    if (!active_panel && !query.include_hidden) {
      continue;
    }

    ModelingActionPanel panel;
    panel.panel_id = std::string(host_panel.id);
    panel.label = std::string(host_panel.label);
    panel.selection_mode_id =
        std::string(host_selection_mode_id(host_panel.selection_mode));
    panel.visibility = active_panel ? ModelingActionVisibility::Enabled
                                    : ModelingActionVisibility::Hidden;
    append_host_panel_actions(panel.actions, host_panel, summary, query);

    if (!panel.actions.empty() || query.include_hidden) {
      result.panels.push_back(std::move(panel));
    }
  }

  return result;
}

std::vector<ModelingActionItem>
ModelingActionCatalog::actions_for(const ModelingSelectionSummary &summary,
                                   const ModelingActionQuery &query) {
  std::vector<ModelingActionItem> actions;
  const ModelingActionSet action_set = action_set_for(summary, query);
  for (const ModelingActionPanel &panel : action_set.panels) {
    actions.insert(actions.end(), panel.actions.begin(), panel.actions.end());
  }
  return actions;
}

std::optional<ModelingActionItem>
ModelingActionCatalog::action_for(const ModelingSelectionSummary &summary,
                                  std::string_view action_id) {
  if (const quader_host_contract::HostActionDescriptor *descriptor =
          quader_host_contract::find_action_descriptor(action_id)) {
    return action_from_host_descriptor(summary, *descriptor);
  }

  std::vector<ModelingActionItem> edit_actions;
  append_edit_actions(edit_actions, summary,
                      {
                          .surface = ModelingActionSurface::ViewportContextMenu,
                          .include_disabled = true,
                          .include_hidden = true,
                      });
  const auto found = std::find_if(edit_actions.begin(), edit_actions.end(),
                                  [&](const ModelingActionItem &action) {
                                    return action.action_id == action_id;
                                  });
  return found == edit_actions.end()
             ? std::nullopt
             : std::optional<ModelingActionItem>(*found);
}

bool ModelingActionCatalog::action_available(
    const ModelingSelectionSummary &summary, std::string_view action_id) {
  const std::optional<ModelingActionItem> action =
      action_for(summary, action_id);
  return action.has_value() &&
         action->visibility == ModelingActionVisibility::Enabled;
}

ModelingActionAvailabilityResult ModelingActionCatalog::evaluate_availability(
    const ModelingSelectionSummary &summary,
    const ModelingActionAvailabilityRule &rule) {
  return evaluate_availability_rule(summary, rule);
}

ModelingActionAvailabilityResult
ModelingActionCatalog::evaluate_operation_availability(
    const ModelingSelectionSummary &summary, std::string_view operation_id,
    const ModelingActionAvailabilityRule &rule) {
  if (std::optional<ModelingActionAvailabilityResult> host_result =
          evaluate_host_operation_availability(summary, operation_id,
                                               summary.mode)) {
    return *host_result;
  }
  return evaluate_availability_rule(summary, rule);
}

ModelingActionPreviewKind ModelingActionCatalog::preview_kind_from_host(
    quader_host_contract::HostPreviewKind kind) {
  switch (kind) {
  case quader_host_contract::HostPreviewKind::Bridge:
    return ModelingActionPreviewKind::Bridge;
  case quader_host_contract::HostPreviewKind::EdgeBevel:
    return ModelingActionPreviewKind::EdgeBevel;
  case quader_host_contract::HostPreviewKind::Thicken:
    return ModelingActionPreviewKind::Thicken;
  case quader_host_contract::HostPreviewKind::SliceQuad:
    return ModelingActionPreviewKind::SliceQuad;
  case quader_host_contract::HostPreviewKind::MergeByDistance:
    return ModelingActionPreviewKind::MergeByDistance;
  case quader_host_contract::HostPreviewKind::None:
    return ModelingActionPreviewKind::None;
  }
  return ModelingActionPreviewKind::None;
}

} // namespace quader
