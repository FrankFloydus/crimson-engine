////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader_host_contract {
enum class HostPreviewKind;
}

namespace quader {

/**
 * Enumerates ModelingActionSurface values used by the modeling layer.
 */
enum class ModelingActionSurface {
    ViewportContextMenu,
    OperationsPanel,
};

/**
 * Enumerates ModelingActionVisibility values used by the modeling layer.
 */
enum class ModelingActionVisibility {
    Hidden,
    Disabled,
    Enabled,
};

/**
 * Enumerates ModelingActionKind values used by the modeling layer.
 */
enum class ModelingActionKind {
    Operation,
    Tool,
};

/**
 * Enumerates ModelingActionPreviewKind values used by the modeling layer.
 */
enum class ModelingActionPreviewKind {
    None,
    Bridge,
    EdgeBevel,
    Thicken,
    SliceQuad,
    MergeByDistance,
    HalfCut,
};

/**
 * Enumerates selection requirement shapes for modeling action availability.
 */
enum class ModelingActionSelectionRequirement {
    AnySelection,
    SelectionMode,
    ComponentSelection,
    ObjectOrGroupSelection,
    ObjectOrFaceSelection,
    EdgeOrFaceSelection,
};

/**
 * Represents the app-side action availability rule evaluated for host and instant modeling actions.
 */
struct ModelingActionAvailabilityRule {
    ModelingActionSelectionRequirement requirement = ModelingActionSelectionRequirement::AnySelection;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    std::uint32_t minimum_selected_vertices = 0;
    std::uint32_t minimum_selected_edges = 0;
    std::uint32_t exact_selected_edges = 0;
    std::uint32_t minimum_selected_faces = 0;
    std::uint32_t minimum_selected_objects = 0;
    ModelingActiveSelectionKind required_active_kind =
        ModelingActiveSelectionKind::None;
    bool requires_single_object = false;
};

/**
 * Represents an evaluated modeling action availability result.
 */
struct ModelingActionAvailabilityResult {
    ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
    std::string disabled_reason;
    std::string disabled_reason_key;
    std::uint32_t disabled_reason_count = 0;
};

/**
 * Represents a Modeling Action Query value used by the modeling application service layer.
 */
struct ModelingActionQuery {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    bool modal_tool_owns_viewport = false;
    bool suppress_when_modal_tool_owns_viewport = true;
    bool include_disabled = true;
    bool include_hidden = false;
};

/**
 * Represents a Modeling Action Item value used by the modeling application service layer.
 */
struct ModelingActionItem {
  std::string action_id;
  std::string operation_id;
  std::string tool_id;
  std::string label;
  ModelingActionKind kind = ModelingActionKind::Operation;
  ModelingActionPreviewKind preview_kind = ModelingActionPreviewKind::None;
  ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
  std::string disabled_reason;
  std::string disabled_reason_key;
  std::uint32_t disabled_reason_count = 0;
  bool rebuilds_mesh_on_success = true;
};

/**
 * Represents a Modeling Action Panel value used by the modeling application service layer.
 */
struct ModelingActionPanel {
  std::string panel_id;
  std::string label;
  std::string selection_mode_id;
  ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
  std::vector<ModelingActionItem> actions;
};

/**
 * Represents a Modeling Action Set value used by the modeling application service layer.
 */
struct ModelingActionSet {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    std::string surface_id;
    std::vector<ModelingActionPanel> panels;
};

/**
 * Owns Modeling Action Catalog behavior within the modeling application service layer.
 */
class ModelingActionCatalog {
public:
  [[nodiscard]] static ModelingActionSet
  action_set_for(const ModelingSelectionSummary &summary,
                 const ModelingActionQuery &query);
  [[nodiscard]] static std::vector<ModelingActionItem>
  actions_for(const ModelingSelectionSummary &summary,
              const ModelingActionQuery &query);
  [[nodiscard]] static std::optional<ModelingActionItem>
  action_for(const ModelingSelectionSummary &summary,
             std::string_view action_id);
  [[nodiscard]] static bool
  action_available(const ModelingSelectionSummary &summary,
                   std::string_view action_id);
  [[nodiscard]] static std::vector<ModelingSelectionMode>
  selection_modes_for_action(std::string_view action_id);
  [[nodiscard]] static ModelingActionAvailabilityResult
  evaluate_availability(const ModelingSelectionSummary &summary,
                        const ModelingActionAvailabilityRule &rule);
  [[nodiscard]] static ModelingActionAvailabilityResult
  evaluate_operation_availability(const ModelingSelectionSummary &summary,
                                  std::string_view operation_id,
                                  const ModelingActionAvailabilityRule &rule);
  [[nodiscard]] static ModelingActionPreviewKind
  preview_kind_from_host(quader_host_contract::HostPreviewKind kind);
};

} // namespace quader
