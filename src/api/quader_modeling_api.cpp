////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_modeling_api.hpp"
#include "editor/commands/document_command_service.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_settings_presentation_internal.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "api/quader_selection_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/commands/modeling/commit_box_preview_command.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/curve_history_commands.hpp"
#include "editor/history/face_texture_settings_history_command.hpp"
#include "editor/history/face_texture_action_history_command.hpp"
#include "editor/history/face_texture_transfer_history_command.hpp"
#include "editor/history/face_texture_gesture_history_command.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/history/uv_peel_history_command.hpp"
#include "editor/placement/snap_vertices_to_floor_history_command.hpp"
#include "editor/instances/project_clipboard_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"
#include "editor/transform/mesh_transform_bake_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/glm_bridge.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

ModelingStructuralDuplicate
structural_duplicate_for_transform(SceneSession &session) {
  return [&session](EditorTransaction &transaction, ModelingSelectionMode mode,
                    std::string_view label) {
    return modeling_structural_duplicate_result(
        session.structural_lifecycle().duplicate_hierarchy_in_transaction(
            transaction, session.structural_lifecycle().selected_roots(), mode,
            std::string(label)));
  };
}

std::optional<ModelingUvPeelOptions>
to_modeling_uv_peel_options(const QuaderUvPeelOptions &options) {
  ModelingUvPeelAxis axis;
  switch (options.axis) {
  case QuaderUvPeelAxis::U:
    axis = ModelingUvPeelAxis::U;
    break;
  case QuaderUvPeelAxis::V:
    axis = ModelingUvPeelAxis::V;
    break;
  default:
    return std::nullopt;
  }
  return ModelingUvPeelOptions{
      .axis = axis,
      .world_space = options.world_space,
      .u_repeat = options.u_repeat,
      .v_repeat = options.v_repeat,
      .u_offset = options.u_offset,
      .v_offset = options.v_offset,
  };
}

QuaderUvPeelOptions to_api_uv_peel_options(const ModelingUvPeelOptions &options) {
  return {
      .axis = options.axis == ModelingUvPeelAxis::V ? QuaderUvPeelAxis::V
                                                   : QuaderUvPeelAxis::U,
      .world_space = options.world_space,
      .u_repeat = options.u_repeat,
      .v_repeat = options.v_repeat,
      .u_offset = options.u_offset,
      .v_offset = options.v_offset,
  };
}

std::optional<ModelingFaceTextureAction>
to_modeling_face_texture_action(QuaderFaceTextureAction action) {
  switch (action) {
  case QuaderFaceTextureAction::AlignToGrid:
    return ModelingFaceTextureAction::AlignToGrid;
  case QuaderFaceTextureAction::AlignToFace:
    return ModelingFaceTextureAction::AlignToFace;
  case QuaderFaceTextureAction::AlignToView:
    return ModelingFaceTextureAction::AlignToView;
  case QuaderFaceTextureAction::ProjectCube:
    return ModelingFaceTextureAction::ProjectCube;
  case QuaderFaceTextureAction::ProjectCylinder:
    return ModelingFaceTextureAction::ProjectCylinder;
  case QuaderFaceTextureAction::ProjectSphere:
    return ModelingFaceTextureAction::ProjectSphere;
  case QuaderFaceTextureAction::ScaleXUp:
    return ModelingFaceTextureAction::ScaleXUp;
  case QuaderFaceTextureAction::ScaleXDown:
    return ModelingFaceTextureAction::ScaleXDown;
  case QuaderFaceTextureAction::ScaleYUp:
    return ModelingFaceTextureAction::ScaleYUp;
  case QuaderFaceTextureAction::ScaleYDown:
    return ModelingFaceTextureAction::ScaleYDown;
  case QuaderFaceTextureAction::ShiftLeft:
    return ModelingFaceTextureAction::ShiftLeft;
  case QuaderFaceTextureAction::ShiftRight:
    return ModelingFaceTextureAction::ShiftRight;
  case QuaderFaceTextureAction::ShiftUp:
    return ModelingFaceTextureAction::ShiftUp;
  case QuaderFaceTextureAction::ShiftDown:
    return ModelingFaceTextureAction::ShiftDown;
  case QuaderFaceTextureAction::RotateClockwise:
    return ModelingFaceTextureAction::RotateClockwise;
  case QuaderFaceTextureAction::RotateCounterClockwise:
    return ModelingFaceTextureAction::RotateCounterClockwise;
  case QuaderFaceTextureAction::FlipTextureHorizontal:
    return ModelingFaceTextureAction::FlipTextureHorizontal;
  case QuaderFaceTextureAction::FlipTextureVertical:
    return ModelingFaceTextureAction::FlipTextureVertical;
  case QuaderFaceTextureAction::FlipFaceHorizontal:
    return ModelingFaceTextureAction::FlipFaceHorizontal;
  case QuaderFaceTextureAction::FlipFaceVertical:
    return ModelingFaceTextureAction::FlipFaceVertical;
  case QuaderFaceTextureAction::FitBoth:
    return ModelingFaceTextureAction::FitBoth;
  case QuaderFaceTextureAction::FitX:
    return ModelingFaceTextureAction::FitX;
  case QuaderFaceTextureAction::FitY:
    return ModelingFaceTextureAction::FitY;
  case QuaderFaceTextureAction::JustifyLeft:
    return ModelingFaceTextureAction::JustifyLeft;
  case QuaderFaceTextureAction::JustifyRight:
    return ModelingFaceTextureAction::JustifyRight;
  case QuaderFaceTextureAction::JustifyTop:
    return ModelingFaceTextureAction::JustifyTop;
  case QuaderFaceTextureAction::JustifyBottom:
    return ModelingFaceTextureAction::JustifyBottom;
  case QuaderFaceTextureAction::JustifyCenter:
    return ModelingFaceTextureAction::JustifyCenter;
  }
  return std::nullopt;
}

QuaderEntityRef instance_entity_ref(const SceneSession &session,
                                    SceneSessionId scene_session_id,
                                    EntityId entity) {
  if (entity == kInvalidEntityId || !session.scene().exists(entity)) {
    return {};
  }
  return {
      .scene_session_id = scene_session_id,
      .id = entity,
      .uuid = session.scene().uuid(entity),
      .editor_object_id = session.scene().editor_object_id(entity),
  };
}

MaterialReference default_project_material(QuaderApiSession session) {
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  return project != nullptr && project->manifest() != nullptr
             ? MaterialReference::project(
                   *ProjectAssetId::parse(kDefaultProjectMaterialId))
             : MaterialReference{};
}

MaterialReference native_material_reference(QuaderMaterialReference material) {
  return material.project_owned()
             ? MaterialReference::project(material.project_asset)
             : MaterialReference(material.embedded_asset);
}

/// Samples deleted project references using the same visible fallback as rendering.
QuaderMaterialReference copied_face_material(QuaderApiSession session,
                                            MaterialReference material) {
  if (material.embedded()) {
    return QuaderMaterialReference::embedded(material.embedded_asset());
  }
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  const ProjectAssetId id =
      project != nullptr && project->manifest() != nullptr
          ? ProjectMaterialReferenceResolver::canonical_material_id(
                *project->manifest(), material.project_asset())
          : material.project_asset();
  return QuaderMaterialReference::project(id);
}

bool assignment_material_exists(QuaderApiSession api_session,
                                QuaderMaterialReference material) {
  if (!material.valid()) {
    return false;
  }
  if (material.embedded()) {
    return scene_session(api_session).assets().material(material.embedded_asset) !=
           nullptr;
  }
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(api_session);
  return project != nullptr && project->manifest() != nullptr &&
         !project->manifest()->is_material_deleted(material.project_asset) &&
         project->material_catalog().find(material.project_asset) != nullptr;
}

QuaderApiResult<QuaderModelingObjectRef>
result_from_create(QuaderApiVoidResult status, QuaderModelingObjectRef value) {
  return quader_api_result_from_status(status, value);
}

QuaderApiResult<QuaderTransformDragPreview>
result_from_transform_drag_preview(const SceneSession &session,
                                   ModelingTransformDragPreview preview) {
  QuaderApiResult<QuaderTransformDragPreview> result;
  result.ok = preview.ok;
  result.changed = preview.changed;
  result.message = preview.message;
  result.mutation_kind = quader_api_preview_mutation_kind(
      preview.ok, preview.changed, preview.active,
      QuaderMutationKind::Modeling);
  result.revisions = quader_api_current_revisions(session);
  result.value = to_api_transform_drag_preview(preview);
  return result;
}

QuaderTransform3 rotation_transform(QVec3 euler_radians) {
  const glm::mat3 rotation =
      glm::mat3_cast(to_glm(quat_from_euler_radians(euler_radians)));
  return {
      .x_axis = from_glm(glm::vec3(rotation[0])),
      .y_axis = from_glm(glm::vec3(rotation[1])),
      .z_axis = from_glm(glm::vec3(rotation[2])),
      .origin = {},
  };
}

QuaderTransform3 scale_transform(QVec3 scale) {
  return {
      .x_axis = {scale.x, 0.0f, 0.0f},
      .y_axis = {0.0f, scale.y, 0.0f},
      .z_axis = {0.0f, 0.0f, scale.z},
      .origin = {},
  };
}

QuaderApiVoidResult invalid_api_status(QuaderApiSession session,
                                       std::string message) {
  return quader_api_failure_status(scene_session(session),
                                   std::move(message));
}

QuaderApiVoidResult invalid_api_status(
    const std::optional<QuaderApiSession> &session, std::string message) {
  if (session.has_value() && session->valid()) {
    return invalid_api_status(*session, std::move(message));
  }
  QuaderApiVoidResult result;
  result.ok = false;
  result.changed = false;
  result.message = std::move(message);
  return result;
}

/// Validates editing scope before the mutation runner can settle pending work.
bool material_mutation_target_is_available(SceneSession &session,
                                            QuaderModelingObjectId object_id,
                                            bool whole_mesh) {
  const auto entity = session.modeling().entity_for_modeling_object(object_id);
  if (!entity.has_value() || !session.scene().exists(*entity)) {
    return false;
  }
  return (whole_mesh && session.editor_mode().mode() == EditorMode::Object) ||
         session.modeling().component_target_is_editable(*entity);
}

QuaderPreviewSettings preview_settings(QuaderEdgeBevelArgs args) {
  QuaderPreviewSettings settings;
  settings.interaction.edge_bevel_width = args.width;
  settings.interaction.edge_bevel_profile = args.profile;
  settings.interaction.edge_bevel_segments = args.segments;
  return settings;
}

QuaderPreviewSettings preview_settings(QuaderThickenArgs args) {
  QuaderPreviewSettings settings;
  settings.interaction.thicken_thickness = args.thickness;
  settings.interaction.thicken_from_center = args.from_center;
  return settings;
}

QuaderPreviewSettings preview_settings(QuaderSliceQuadArgs args) {
  QuaderPreviewSettings settings;
  settings.interaction.slice_quad_x_slices = args.x_slices;
  settings.interaction.slice_quad_y_slices = args.y_slices;
  return settings;
}

QuaderPreviewSettings preview_settings(QuaderMergeByDistanceArgs args) {
  QuaderPreviewSettings settings;
  settings.interaction.merge_by_distance_tolerance = args.tolerance;
  return settings;
}

QuaderPreviewSettings bridge_preview_settings(int steps) {
  QuaderPreviewSettings settings;
  settings.interaction.bridge_steps = steps;
  return settings;
}

std::vector<QuaderElementId>
element_vector(std::span<const QuaderElementId> elements) {
  return {elements.begin(), elements.end()};
}

std::vector<QuaderEdgeRef> edge_vector(std::span<const QuaderEdgeRef> edges) {
  return {edges.begin(), edges.end()};
}

std::vector<std::pair<QuaderElementId, QuaderElementId>>
edge_pairs(std::span<const QuaderEdgeRef> edges) {
  std::vector<std::pair<QuaderElementId, QuaderElementId>> result;
  result.reserve(edges.size());
  for (const QuaderEdgeRef edge : edges) {
    result.emplace_back(edge.a, edge.b);
  }
  return result;
}

QuaderComponentRef active_component(QuaderModelingObjectRef object,
                                    QuaderModelingSelection::Kind kind,
                                    std::span<const QuaderElementId> elements,
                                    std::span<const QuaderEdgeRef> edges) {
  QuaderComponentRef active;
  active.object = object;
  switch (kind) {
  case QuaderModelingSelection::Kind::Vertex:
    if (!elements.empty()) {
      active.kind = QuaderComponentKind::Vertex;
      active.vertex = elements.back();
    }
    break;
  case QuaderModelingSelection::Kind::Edge:
    if (!edges.empty()) {
      active.kind = QuaderComponentKind::Edge;
      active.edge_a = edges.back().a;
      active.edge_b = edges.back().b;
    }
    break;
  case QuaderModelingSelection::Kind::Face:
    if (!elements.empty()) {
      active.kind = QuaderComponentKind::Face;
      active.face = elements.back();
    }
    break;
  case QuaderModelingSelection::Kind::Object:
    active.kind = QuaderComponentKind::Object;
    break;
  }
  return active;
}

bool entity_selection_owns_transforms(SceneSession &session) {
  const ModelingSelectionSummary summary = session.selection().summary();
  return summary.has_selection &&
         selection_mode_uses_scene_document(summary.mode);
}

bool native_modeling_action_execution_is_available(
    const SceneSession &session) {
  return !session.native_tools().has_pending_tool() &&
         !session.modeling().can_cancel_active_tool_or_preview();
}

bool copy_pivot_from_active_is_available(const SceneSession &session) {
  return native_modeling_action_execution_is_available(session) &&
         session.pivot().can_copy_pivot_from_active(session.selection());
}

std::optional<MeshTransformBakeMode>
to_mesh_transform_bake_mode(QuaderTransformBakeMode mode) {
  switch (mode) {
  case QuaderTransformBakeMode::Transforms:
    return MeshTransformBakeMode::Transforms;
  case QuaderTransformBakeMode::Location:
    return MeshTransformBakeMode::Location;
  case QuaderTransformBakeMode::Rotation:
    return MeshTransformBakeMode::Rotation;
  case QuaderTransformBakeMode::Scale:
    return MeshTransformBakeMode::Scale;
  }
  return std::nullopt;
}

std::string_view transform_bake_label(QuaderTransformBakeMode mode) {
  switch (mode) {
  case QuaderTransformBakeMode::Transforms:
    return "Bake Transforms";
  case QuaderTransformBakeMode::Location:
    return "Bake Location";
  case QuaderTransformBakeMode::Rotation:
    return "Bake Rotation";
  case QuaderTransformBakeMode::Scale:
    return "Bake Scale";
  }
  return "Bake Transforms";
}

/// Selects the narrow owner predicate used by one native modeling action.
enum class NativeModelingActionAvailability {
  SnapMeshesToFloor,
  CopyPivotFromActive,
  SnapVerticesToFloor,
  BakeTransforms,
  BakeLocation,
  BakeRotation,
  BakeScale,
};

/// Defines relative placement inside retained native action collections.
enum class NativeModelingActionPlacement {
  First,
  AfterAnchor,
  BeforeEditActions,
  Last,
};

/// Describes one native action injected beside catalog-generated operations.
struct NativeModelingActionDescriptor final {
  std::string_view action_id;
  std::string_view label;
  ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
  std::string_view panel_id;
  NativeModelingActionAvailability availability =
      NativeModelingActionAvailability::SnapMeshesToFloor;
  NativeModelingActionPlacement operations_placement =
      NativeModelingActionPlacement::First;
  NativeModelingActionPlacement context_placement =
      NativeModelingActionPlacement::First;
  std::string_view anchor_action_id;
  std::string_view unavailable_reason_key;
  std::string_view active_tool_reason_key;
  std::string_view instance_edit_reason_key;
  bool rebuilds_mesh_on_success = true;
};

constexpr std::array<NativeModelingActionDescriptor, 7>
    kNativeModelingActions{{
        {
            .action_id = "snap_meshes_to_floor",
            .label = "Snap Meshes to Floor",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability =
                NativeModelingActionAvailability::SnapMeshesToFloor,
            .operations_placement = NativeModelingActionPlacement::First,
            .context_placement = NativeModelingActionPlacement::First,
            .unavailable_reason_key =
                "modeling.action.disabled.snap_meshes_to_floor_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.snap_to_floor_active_tool",
            .instance_edit_reason_key = {},
            .rebuilds_mesh_on_success = false,
        },
        {
            .action_id = "bake_transforms",
            .label = "Bake Transforms",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability = NativeModelingActionAvailability::BakeTransforms,
            .operations_placement = NativeModelingActionPlacement::AfterAnchor,
            .context_placement = NativeModelingActionPlacement::AfterAnchor,
            .anchor_action_id = "snap_meshes_to_floor",
            .unavailable_reason_key =
                "modeling.action.disabled.bake_transforms_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.bake_transforms_active_tool",
            .instance_edit_reason_key =
                "modeling.action.disabled.bake_transforms_instance_edit",
            .rebuilds_mesh_on_success = true,
        },
        {
            .action_id = "bake_location",
            .label = "Bake Location",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability = NativeModelingActionAvailability::BakeLocation,
            .operations_placement = NativeModelingActionPlacement::AfterAnchor,
            .context_placement = NativeModelingActionPlacement::AfterAnchor,
            .anchor_action_id = "bake_transforms",
            .unavailable_reason_key =
                "modeling.action.disabled.bake_transforms_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.bake_transforms_active_tool",
            .instance_edit_reason_key =
                "modeling.action.disabled.bake_transforms_instance_edit",
            .rebuilds_mesh_on_success = true,
        },
        {
            .action_id = "bake_rotation",
            .label = "Bake Rotation",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability = NativeModelingActionAvailability::BakeRotation,
            .operations_placement = NativeModelingActionPlacement::AfterAnchor,
            .context_placement = NativeModelingActionPlacement::AfterAnchor,
            .anchor_action_id = "bake_location",
            .unavailable_reason_key =
                "modeling.action.disabled.bake_transforms_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.bake_transforms_active_tool",
            .instance_edit_reason_key =
                "modeling.action.disabled.bake_transforms_instance_edit",
            .rebuilds_mesh_on_success = true,
        },
        {
            .action_id = "bake_scale",
            .label = "Bake Scale",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability = NativeModelingActionAvailability::BakeScale,
            .operations_placement = NativeModelingActionPlacement::AfterAnchor,
            .context_placement = NativeModelingActionPlacement::AfterAnchor,
            .anchor_action_id = "bake_rotation",
            .unavailable_reason_key =
                "modeling.action.disabled.bake_transforms_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.bake_transforms_active_tool",
            .instance_edit_reason_key =
                "modeling.action.disabled.bake_transforms_instance_edit",
            .rebuilds_mesh_on_success = true,
        },
        {
            .action_id = "copy_pivot_from_active",
            .label = "Copy Pivot From Active",
            .selection_mode = ModelingSelectionMode::Object,
            .panel_id = "object_operations",
            .availability =
                NativeModelingActionAvailability::CopyPivotFromActive,
            .operations_placement = NativeModelingActionPlacement::Last,
            .context_placement =
                NativeModelingActionPlacement::BeforeEditActions,
            .unavailable_reason_key =
                "modeling.action.disabled.copy_pivot_from_active_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.copy_pivot_from_active_selection",
            .instance_edit_reason_key = {},
            .rebuilds_mesh_on_success = true,
        },
        {
            .action_id = "snap_vertices_to_floor",
            .label = "Snap Vertices to Floor",
            .selection_mode = ModelingSelectionMode::Vertex,
            .panel_id = "vertex_operations",
            .availability =
                NativeModelingActionAvailability::SnapVerticesToFloor,
            .operations_placement =
                NativeModelingActionPlacement::AfterAnchor,
            .context_placement = NativeModelingActionPlacement::AfterAnchor,
            .anchor_action_id = "snap_vertices_to_active",
            .unavailable_reason_key =
                "modeling.action.disabled.snap_vertices_to_floor_selection",
            .active_tool_reason_key =
                "modeling.action.disabled.snap_to_floor_active_tool",
            .instance_edit_reason_key =
                "modeling.action.disabled.snap_vertices_to_floor_instance_edit",
            .rebuilds_mesh_on_success = true,
        },
    }};

bool native_modeling_action_is_available(
    const SceneSession &session,
    NativeModelingActionAvailability availability) {
  switch (availability) {
  case NativeModelingActionAvailability::SnapMeshesToFloor:
    return session.snap_to_floor().can_snap_selected_meshes_to_floor();
  case NativeModelingActionAvailability::CopyPivotFromActive:
    return session.pivot().can_copy_pivot_from_active(session.selection());
  case NativeModelingActionAvailability::SnapVerticesToFloor:
    return session.snap_to_floor().can_snap_selected_vertices_to_floor();
  case NativeModelingActionAvailability::BakeTransforms:
    return session.mesh_transform_bake().can_bake(
        session.selection(), MeshTransformBakeMode::Transforms);
  case NativeModelingActionAvailability::BakeLocation:
    return session.mesh_transform_bake().can_bake(
        session.selection(), MeshTransformBakeMode::Location);
  case NativeModelingActionAvailability::BakeRotation:
    return session.mesh_transform_bake().can_bake(
        session.selection(), MeshTransformBakeMode::Rotation);
  case NativeModelingActionAvailability::BakeScale:
    return session.mesh_transform_bake().can_bake(
        session.selection(), MeshTransformBakeMode::Scale);
  }
  return false;
}

QuaderActionItem native_modeling_action(
    const SceneSession &session, const ModelingSelectionSummary &summary,
    bool execution_is_available,
    const NativeModelingActionDescriptor &descriptor) {
  QuaderActionVisibility visibility = QuaderActionVisibility::Hidden;
  if (summary.mode == descriptor.selection_mode) {
    visibility = execution_is_available &&
                         native_modeling_action_is_available(
                             session, descriptor.availability)
                     ? QuaderActionVisibility::Enabled
                     : QuaderActionVisibility::Disabled;
  }
  const bool blocked_by_active_tool =
      visibility == QuaderActionVisibility::Disabled &&
      !execution_is_available;
  const bool blocked_by_instance_edit =
      visibility == QuaderActionVisibility::Disabled &&
      !descriptor.instance_edit_reason_key.empty() &&
      session.instances().instance_edit_state().active;
  std::string disabled_reason_key;
  if (visibility == QuaderActionVisibility::Disabled) {
    if (blocked_by_active_tool) {
      disabled_reason_key = descriptor.active_tool_reason_key;
    } else if (blocked_by_instance_edit) {
      disabled_reason_key = descriptor.instance_edit_reason_key;
    } else {
      disabled_reason_key = descriptor.unavailable_reason_key;
    }
  }
  return {
      .action_id = std::string(descriptor.action_id),
      .label = std::string(descriptor.label),
      .kind = QuaderActionKind::Operation,
      .visibility = visibility,
      .disabled_reason_key = std::move(disabled_reason_key),
      .rebuilds_mesh_on_success = descriptor.rebuilds_mesh_on_success,
  };
}

bool action_is_included(const QuaderActionItem &action,
                        const QuaderActionQuery &query) {
  if (action.visibility == QuaderActionVisibility::Hidden) {
    return query.include_hidden;
  }
  if (action.visibility == QuaderActionVisibility::Disabled) {
    return query.include_disabled;
  }
  return true;
}

void insert_native_modeling_action(
    QuaderActionPanel &panel, QuaderActionItem action,
    const NativeModelingActionDescriptor &descriptor,
    NativeModelingActionPlacement placement) {
  auto insertion = panel.actions.end();
  switch (placement) {
  case NativeModelingActionPlacement::First:
    insertion = panel.actions.begin();
    break;
  case NativeModelingActionPlacement::AfterAnchor: {
    const auto anchor = std::find_if(
        panel.actions.begin(), panel.actions.end(), [&](const auto &candidate) {
          return candidate.action_id == descriptor.anchor_action_id;
        });
    insertion = anchor == panel.actions.end() ? panel.actions.begin()
                                               : std::next(anchor);
    break;
  }
  case NativeModelingActionPlacement::BeforeEditActions:
    insertion = std::find_if(
        panel.actions.begin(), panel.actions.end(), [](const auto &candidate) {
          return candidate.action_id == "delete_selection" ||
                 candidate.action_id == "duplicate_meshes";
        });
    break;
  case NativeModelingActionPlacement::Last:
    break;
  }
  panel.actions.insert(insertion, std::move(action));
}

void inject_native_modeling_actions(QuaderActionSet &action_set,
                                    const QuaderActionQuery &query,
                                    const SceneSession &session) {
  if (action_set.panels.empty()) {
    return;
  }
  const ModelingSelectionSummary summary = session.selection().summary();
  const bool execution_is_available =
      native_modeling_action_execution_is_available(session);
  for (const NativeModelingActionDescriptor &descriptor :
       kNativeModelingActions) {
    if (!release_allows_action(descriptor.action_id)) {
      continue;
    }
    QuaderActionItem action = native_modeling_action(
        session, summary, execution_is_available, descriptor);
    if (!action_is_included(action, query)) {
      continue;
    }

    const std::string_view target_panel_id =
        query.surface == QuaderActionSurface::OperationsPanel
            ? descriptor.panel_id
            : std::string_view{"viewport_context_menu"};
    const auto panel = std::find_if(
        action_set.panels.begin(), action_set.panels.end(),
        [&](const QuaderActionPanel &candidate) {
          return candidate.panel_id == target_panel_id;
        });
    if (panel != action_set.panels.end()) {
      const NativeModelingActionPlacement placement =
          query.surface == QuaderActionSurface::OperationsPanel
              ? descriptor.operations_placement
              : descriptor.context_placement;
      insert_native_modeling_action(*panel, std::move(action), descriptor,
                                    placement);
    }
  }
}

QMat4 world_transform_delta_from_api_transform(
    const QuaderTransform3 &transform, QVec3 pivot) {
  glm::mat4 local_transform{1.0f};
  local_transform[0] = glm::vec4{to_glm(transform.x_axis), 0.0f};
  local_transform[1] = glm::vec4{to_glm(transform.y_axis), 0.0f};
  local_transform[2] = glm::vec4{to_glm(transform.z_axis), 0.0f};
  local_transform[3] = glm::vec4{to_glm(transform.origin), 1.0f};

  const glm::mat4 around_pivot =
      glm::translate(glm::mat4{1.0f}, to_glm(pivot)) * local_transform *
      glm::translate(glm::mat4{1.0f}, -to_glm(pivot));
  return from_glm(around_pivot);
}

QuaderApiVoidResult apply_selection_transform(
    QuaderApiSession api_session, QuaderApiMutationRunner &mutations,
    const QuaderTransform3 &transform, QVec3 pivot,
    SceneTextureTransformKind kind) {
  SceneSession &session = scene_session(api_session);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    const QMat4 display_from_scene =
        session.instances().active_edit_display_from_scene().value_or(
            scene_transform_identity_matrix());
    return mutations.run_document_result(
        transforms.transform_selected_by_world_delta(
            session.document_commands(), session.scene(),
            world_transform_delta_from_api_transform(transform, pivot),
            "Transform Selection", display_from_scene, kind),
        "Transform Selection");
  }
  return mutations.run_modeling_command(
      std::make_unique<TransformSelectionHistoryCommand>(
          session.modeling(), to_modeling_transform3(transform), pivot),
      "Transform Selection");
}

std::optional<MeshArrangementAxis>
mesh_arrangement_axis(QuaderTransformAxis axis) {
  switch (axis) {
  case QuaderTransformAxis::X:
    return MeshArrangementAxis::X;
  case QuaderTransformAxis::Y:
    return MeshArrangementAxis::Y;
  case QuaderTransformAxis::Z:
    return MeshArrangementAxis::Z;
  case QuaderTransformAxis::None:
  case QuaderTransformAxis::XY:
  case QuaderTransformAxis::XZ:
  case QuaderTransformAxis::YZ:
  case QuaderTransformAxis::All:
    return std::nullopt;
  }
  return std::nullopt;
}

MeshAlignmentMode mesh_alignment_mode(QuaderMeshAlignment alignment) {
  switch (alignment) {
  case QuaderMeshAlignment::Minimum:
    return MeshAlignmentMode::Minimum;
  case QuaderMeshAlignment::Center:
    return MeshAlignmentMode::Center;
  case QuaderMeshAlignment::Maximum:
    return MeshAlignmentMode::Maximum;
  }
  return MeshAlignmentMode::Center;
}

MeshDistributionMode
mesh_distribution_mode(QuaderMeshDistribution distribution) {
  return distribution == QuaderMeshDistribution::Gaps
             ? MeshDistributionMode::Gaps
             : MeshDistributionMode::Centers;
}

MeshArrangementOptions
mesh_arrangement_options(QuaderMeshArrangementOptions options) {
  return {
      .snap_to_grid = options.snap_to_grid,
      .grid_size = options.grid_size,
  };
}

MeshArrangementSelectionContext
mesh_arrangement_selection_context(SceneSession &session) {
  const ModelingSelectionMode mode = session.modeling().selection_mode();
  const EntityId active = session.scene().primary_selection();
  return {
      .object_mode = mode == ModelingSelectionMode::Object || mode == ModelingSelectionMode::Group,
      .active_object = active,
  };
}

} // namespace

std::string_view quader_face_texture_action_label(QuaderFaceTextureAction action) {
  const auto native = to_modeling_face_texture_action(action);
  return native.has_value() ? modeling_face_texture_action_label(*native)
                            : std::string_view{};
}

QuaderPreviewHandle::QuaderPreviewHandle(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    std::string default_label, QuaderApiVoidResult begin_status)
    : session_(std::move(session)), mutations_(&mutations),
      scene_session_id_(mutations.scene_session_id()),
      default_label_(std::move(default_label)),
      begin_status_(std::move(begin_status)) {}

bool QuaderPreviewHandle::valid() const {
  return session_.has_value() && session_->valid() && mutations_ != nullptr &&
         mutations_->belongs_to_active_scene(scene_session_id_) &&
         begin_status_.ok();
}

const QuaderApiVoidResult &QuaderPreviewHandle::begin_result() const {
  return begin_status_;
}

QuaderApiVoidResult
QuaderPreviewHandle::update(const QuaderPreviewSettings &settings) {
  if (mutations_ != nullptr &&
      !mutations_->belongs_to_active_scene(scene_session_id_)) {
    return mutations_->stale_scene_reference_status("Preview handle");
  }
  if (!valid()) {
    return begin_status_;
  }
  return QuaderModelingApi(*session_, *mutations_).update_preview(settings);
}

QuaderApiVoidResult QuaderPreviewHandle::update(QuaderEdgeBevelArgs args) {
  return update(preview_settings(args));
}

QuaderApiVoidResult QuaderPreviewHandle::update(QuaderThickenArgs args) {
  return update(preview_settings(args));
}

QuaderApiVoidResult QuaderPreviewHandle::update(QuaderSliceQuadArgs args) {
  return update(preview_settings(args));
}

QuaderApiVoidResult
QuaderPreviewHandle::update(QuaderMergeByDistanceArgs args) {
  return update(preview_settings(args));
}

QuaderApiVoidResult QuaderPreviewHandle::commit(std::string label) {
  if (mutations_ != nullptr &&
      !mutations_->belongs_to_active_scene(scene_session_id_)) {
    return mutations_->stale_scene_reference_status("Preview handle");
  }
  if (!valid()) {
    return begin_status_;
  }
  if (label.empty()) {
    label = default_label_;
  }
  return QuaderModelingApi(*session_, *mutations_).commit_preview(
      std::move(label));
}

QuaderApiVoidResult QuaderPreviewHandle::cancel() {
  if (mutations_ != nullptr &&
      !mutations_->belongs_to_active_scene(scene_session_id_)) {
    return mutations_->stale_scene_reference_status("Preview handle");
  }
  if (!valid()) {
    return begin_status_;
  }
  return QuaderModelingApi(*session_, *mutations_).cancel_preview();
}

QuaderModelingSelection::QuaderModelingSelection(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object, Kind kind)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)), kind_(kind) {}

QuaderModelingObjectRef QuaderModelingSelection::object() const {
  return object_;
}

bool QuaderModelingSelection::valid_context() const {
  return session_.has_value() && session_->valid() && mutations_ != nullptr &&
         mutations_->belongs_to_active_scene(object_.scene_session_id) &&
         object_.modeling_object_id != kInvalidQuaderModelingObjectId &&
         object_.entity != kInvalidEntityId;
}

bool QuaderModelingSelection::empty() const { return count() == 0U; }

std::size_t QuaderModelingSelection::count() const {
  switch (kind_) {
  case Kind::Object:
    return valid_context() ? 1U : 0U;
  case Kind::Edge:
    return edges_.size();
  case Kind::Vertex:
  case Kind::Face:
    return elements_.size();
  }
  return 0U;
}

QuaderApiVoidResult
QuaderModelingSelection::apply(QuaderSelectionEdit edit) const {
  if (mutations_ != nullptr &&
      !mutations_->belongs_to_active_scene(object_.scene_session_id)) {
    return mutations_->stale_scene_reference_status("Modeling selection");
  }
  if (!valid_context()) {
    return invalid_api_status(session_, "Invalid modeling selection.");
  }
  QuaderSelectionApi selection(*session_, *mutations_);
  if (kind_ == Kind::Object) {
    return selection.select_object(object_, edit);
  }

  QuaderComponentSelectionArgs args;
  args.object = object_;
  args.edit = edit;
  args.active = active_component(object_, kind_, elements_, edges_);
  switch (kind_) {
  case Kind::Vertex:
    args.kind = QuaderComponentKind::Vertex;
    args.vertices = elements_;
    break;
  case Kind::Edge:
    args.kind = QuaderComponentKind::Edge;
    args.edges = edge_pairs(edges_);
    break;
  case Kind::Face:
    args.kind = QuaderComponentKind::Face;
    args.faces = elements_;
    break;
  case Kind::Object:
    break;
  }
  return selection.select_components(args);
}

QuaderApiVoidResult QuaderModelingSelection::replace() const {
  return apply(QuaderSelectionEdit::Replace);
}

QuaderApiVoidResult QuaderModelingSelection::add() const {
  return apply(QuaderSelectionEdit::Add);
}

QuaderApiVoidResult QuaderModelingSelection::remove() const {
  return apply(QuaderSelectionEdit::Remove);
}

QuaderApiVoidResult QuaderModelingSelection::toggle() const {
  return apply(QuaderSelectionEdit::Toggle);
}

QuaderApiVoidResult QuaderModelingSelection::execute_after_replace(
    std::string_view action_id) const {
  if (!release_allows_action(action_id) ||
      !release_allows_operation(action_id)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return selected;
  }
  return QuaderModelingApi(*session_, *mutations_).execute_tool(action_id);
}

QuaderPreviewHandle QuaderModelingSelection::begin_preview_after_replace(
    std::string_view action_id, const QuaderPreviewSettings &settings,
    std::string default_label) const {
  if (!release_allows_action(action_id) ||
      !release_allows_operation(action_id)) {
    if (mutations_ == nullptr) {
      QuaderPreviewHandle unavailable;
      unavailable.begin_status_ =
          invalid_api_status(session_, release_feature_unavailable_message());
      return unavailable;
    }
    return QuaderPreviewHandle(
        session_, *mutations_, std::move(default_label),
        invalid_api_status(session_, release_feature_unavailable_message()));
  }
  QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return QuaderPreviewHandle(session_, *mutations_, std::move(default_label),
                               std::move(selected));
  }

  QuaderModelingApi modeling(*session_, *mutations_);
  QuaderApiVoidResult began = modeling.begin_preview(action_id);
  if (began.ok()) {
    QuaderApiVoidResult updated = modeling.update_preview(settings);
    if (!updated.ok()) {
      began = std::move(updated);
    }
  }
  return QuaderPreviewHandle(session_, *mutations_, std::move(default_label),
                             std::move(began));
}

QuaderObjectSelection::QuaderObjectSelection(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object)
    : QuaderModelingSelection(session, mutations, std::move(object),
                              Kind::Object) {}

QuaderMeshHandle QuaderObjectSelection::mesh() const {
  return mutations_ == nullptr ? QuaderMeshHandle{}
                               : QuaderMeshHandle(session_, *mutations_,
                                                  object_);
}

QuaderApiResult<QuaderMeshHandle> QuaderObjectSelection::duplicate() const {
  QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return quader_api_result_from_status<QuaderMeshHandle>(
        selected, QuaderMeshHandle{});
  }
  QuaderApiVoidResult duplicated =
      QuaderModelingApi(*session_, *mutations_).duplicate_meshes();
  QuaderMeshHandle handle(session_, *mutations_, {});
  if (duplicated.ok() && duplicated.changed()) {
    const QuaderSelectionSummary summary =
        QuaderQueryApi(*session_, *mutations_).selection();
    if (const std::optional<QuaderModelingObjectRef> duplicate =
            QuaderQueryApi(*session_, *mutations_)
                .modeling_object_for_id(summary.active_object_id)) {
      handle = QuaderMeshHandle(session_, *mutations_, *duplicate);
    }
  }
  return quader_api_result_from_status<QuaderMeshHandle>(duplicated, handle);
}

QuaderApiVoidResult QuaderObjectSelection::combine_selected() const {
  if (!valid_context()) {
    return invalid_api_status(session_, "Invalid modeling selection.");
  }
  return QuaderModelingApi(*session_, *mutations_).combine_meshes();
}

QuaderApiVoidResult QuaderObjectSelection::snap_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return selected;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .snap_selected_meshes_to_floor();
}

QuaderVertexSelection::QuaderVertexSelection(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object, std::vector<QuaderElementId> vertices)
    : QuaderModelingSelection(session, mutations, std::move(object),
                              Kind::Vertex) {
  elements_ = std::move(vertices);
}

std::span<const QuaderElementId> QuaderVertexSelection::vertices() const {
  return {elements_.data(), elements_.size()};
}

QuaderApiVoidResult QuaderVertexSelection::snap_to_active() const {
  return execute_after_replace("snap_vertices_to_active");
}

QuaderApiVoidResult QuaderVertexSelection::snap_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return selected;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .snap_selected_vertices_to_floor();
}

QuaderApiVoidResult QuaderVertexSelection::merge_to_active() const {
  return execute_after_replace("merge_vertices_to_active");
}

QuaderApiVoidResult QuaderVertexSelection::merge_to_center() const {
  return execute_after_replace("merge_vertices_to_center");
}

QuaderPreviewHandle
QuaderVertexSelection::merge_by_distance(QuaderMergeByDistanceArgs args) const {
  return begin_preview_after_replace("merge_by_distance",
                                     preview_settings(args),
                                     "Merge By Distance");
}

QuaderApiVoidResult
QuaderVertexSelection::remove_doubles(QuaderMergeByDistanceArgs args) const {
  QuaderPreviewHandle preview = merge_by_distance(args);
  return preview.commit("Remove Doubles");
}

QuaderApiVoidResult QuaderVertexSelection::bevel() const {
  return execute_after_replace("bevel_vertices");
}

QuaderApiVoidResult QuaderVertexSelection::connect() const {
  return execute_after_replace("connect_vertices");
}

QuaderApiVoidResult QuaderVertexSelection::dissolve() const {
  return execute_after_replace("dissolve_vertices");
}

QuaderApiVoidResult QuaderVertexSelection::radial_align() const {
  return execute_after_replace("radial_align_vertices");
}

QuaderEdgeSelection::QuaderEdgeSelection(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object, std::vector<QuaderEdgeRef> edges)
    : QuaderModelingSelection(session, mutations, std::move(object),
                              Kind::Edge) {
  edges_ = std::move(edges);
}

std::span<const QuaderEdgeRef> QuaderEdgeSelection::edges() const {
  return {edges_.data(), edges_.size()};
}

QuaderApiVoidResult QuaderEdgeSelection::snap_to_active() const {
  return execute_after_replace("snap_edges_to_active");
}

QuaderApiVoidResult QuaderEdgeSelection::connect() const {
  return execute_after_replace("connect_edges");
}

QuaderApiVoidResult QuaderEdgeSelection::split() const {
  return execute_after_replace("split_edges");
}

QuaderApiVoidResult QuaderEdgeSelection::reset_normals_to_default() const {
  return execute_after_replace("default_edge_normals");
}

QuaderApiVoidResult QuaderEdgeSelection::harden_normals() const {
  return execute_after_replace("harden_edge_normals");
}

QuaderApiVoidResult QuaderEdgeSelection::soften_normals() const {
  return execute_after_replace("soften_edge_normals");
}

QuaderPreviewHandle QuaderEdgeSelection::bevel(QuaderEdgeBevelArgs args) const {
  return begin_preview_after_replace("bevel_edges", preview_settings(args),
                                     "Bevel Edges");
}

QuaderApiVoidResult QuaderEdgeSelection::bridge() const {
  return execute_after_replace("bridge_edges");
}

QuaderPreviewHandle QuaderEdgeSelection::interpolated_bridge(int steps) const {
  return begin_preview_after_replace("interpolated_bridge_edges",
                                     bridge_preview_settings(steps),
                                     "Interpolated Bridge Edges");
}

QuaderApiVoidResult QuaderEdgeSelection::dissolve() const {
  return execute_after_replace("dissolve_edges");
}

QuaderApiVoidResult QuaderEdgeSelection::merge() const {
  return execute_after_replace("merge_edges");
}

QuaderApiVoidResult QuaderEdgeSelection::collapse() const {
  return execute_after_replace("collapse_edges");
}

QuaderApiVoidResult QuaderEdgeSelection::fill_hole() const {
  return execute_after_replace("fill_hole");
}

QuaderApiVoidResult QuaderEdgeSelection::radial_align() const {
  return execute_after_replace("radial_align_edges");
}

QuaderFaceSelection::QuaderFaceSelection(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object, std::vector<QuaderElementId> faces)
    : QuaderModelingSelection(session, mutations, std::move(object),
                              Kind::Face) {
  elements_ = std::move(faces);
}

std::span<const QuaderElementId> QuaderFaceSelection::faces() const {
  return {elements_.data(), elements_.size()};
}

QuaderApiVoidResult QuaderFaceSelection::bridge() const {
  return execute_after_replace("bridge_faces");
}

QuaderPreviewHandle QuaderFaceSelection::interpolated_bridge(int steps) const {
  return begin_preview_after_replace("interpolated_bridge_faces",
                                     bridge_preview_settings(steps),
                                     "Interpolated Bridge Faces");
}

QuaderApiVoidResult QuaderFaceSelection::invert() const {
  return execute_after_replace("invert_faces");
}

QuaderApiVoidResult QuaderFaceSelection::combine() const {
  return execute_after_replace("combine_faces");
}

QuaderApiVoidResult QuaderFaceSelection::collapse() const {
  return execute_after_replace("collapse_faces");
}

QuaderApiVoidResult QuaderFaceSelection::radial_align() const {
  return execute_after_replace("radial_align_faces");
}

QuaderPreviewHandle QuaderFaceSelection::slice(QuaderSliceQuadArgs args) const {
  return begin_preview_after_replace("slice_quad", preview_settings(args),
                                     "Slice Quad");
}

QuaderPreviewHandle
QuaderFaceSelection::thicken(QuaderThickenArgs args) const {
  return begin_preview_after_replace("thicken_faces", preview_settings(args),
                                     "Thicken Faces");
}

QuaderApiVoidResult QuaderFaceSelection::extract() const {
  return execute_after_replace("extract_faces");
}

QuaderApiVoidResult QuaderFaceSelection::detach() const {
  return execute_after_replace("detach_faces");
}

QuaderApiVoidResult QuaderFaceSelection::shade_smooth() const {
  return execute_after_replace("shade_faces_smooth");
}

QuaderApiVoidResult QuaderFaceSelection::shade_flat() const {
  return execute_after_replace("shade_faces_flat");
}

QuaderApiVoidResult
QuaderFaceSelection::assign_material_slot(std::uint32_t material_slot) const {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return selected;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .assign_material_slot_to_selection(material_slot);
}

QuaderMeshVertices::QuaderMeshVertices(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)) {}

QuaderVertexSelection
QuaderMeshVertices::only(std::span<const QuaderElementId> vertices) const {
  return mutations_ == nullptr
             ? QuaderVertexSelection{}
             : QuaderVertexSelection(session_, *mutations_, object_,
                                     element_vector(vertices));
}

QuaderVertexSelection QuaderMeshVertices::only(
    std::initializer_list<QuaderElementId> vertices) const {
  return only(std::span<const QuaderElementId>(vertices.begin(),
                                              vertices.size()));
}

QuaderMeshEdges::QuaderMeshEdges(std::optional<QuaderApiSession> session,
                                 QuaderApiMutationRunner &mutations,
                                 QuaderModelingObjectRef object)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)) {}

QuaderEdgeSelection
QuaderMeshEdges::only(std::span<const QuaderEdgeRef> edges) const {
  return mutations_ == nullptr
             ? QuaderEdgeSelection{}
             : QuaderEdgeSelection(session_, *mutations_, object_,
                                   edge_vector(edges));
}

QuaderEdgeSelection
QuaderMeshEdges::only(std::initializer_list<QuaderEdgeRef> edges) const {
  return only(std::span<const QuaderEdgeRef>(edges.begin(), edges.size()));
}

QuaderMeshFaces::QuaderMeshFaces(std::optional<QuaderApiSession> session,
                                 QuaderApiMutationRunner &mutations,
                                 QuaderModelingObjectRef object)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)) {}

QuaderFaceSelection
QuaderMeshFaces::only(std::span<const QuaderElementId> faces) const {
  return mutations_ == nullptr
             ? QuaderFaceSelection{}
             : QuaderFaceSelection(session_, *mutations_, object_,
                                   element_vector(faces));
}

QuaderFaceSelection
QuaderMeshFaces::only(std::initializer_list<QuaderElementId> faces) const {
  return only(std::span<const QuaderElementId>(faces.begin(), faces.size()));
}

QuaderMeshTransform::QuaderMeshTransform(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)) {}

QuaderApiVoidResult QuaderMeshTransform::translate(QVec3 delta) const {
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  QuaderObjectSelection selected(session_, *mutations_, object_);
  const QuaderApiVoidResult status = selected.replace();
  if (!status.ok()) {
    return status;
  }
  return QuaderModelingApi(*session_, *mutations_).move_selection(delta);
}

QuaderApiVoidResult QuaderMeshTransform::rotate(QVec3 euler_radians,
                                                QVec3 pivot) const {
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  QuaderObjectSelection selected(session_, *mutations_, object_);
  const QuaderApiVoidResult status = selected.replace();
  if (!status.ok()) {
    return status;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .rotate_selection(euler_radians, pivot);
}

QuaderApiVoidResult QuaderMeshTransform::scale(QVec3 scale,
                                               QVec3 pivot) const {
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  QuaderObjectSelection selected(session_, *mutations_, object_);
  const QuaderApiVoidResult status = selected.replace();
  if (!status.ok()) {
    return status;
  }
  return QuaderModelingApi(*session_, *mutations_).scale_selection(scale, pivot);
}

QuaderApiVoidResult
QuaderMeshTransform::apply(const QuaderTransform3 &transform, QVec3 pivot) const {
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  QuaderObjectSelection selected(session_, *mutations_, object_);
  const QuaderApiVoidResult status = selected.replace();
  if (!status.ok()) {
    return status;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .transform_selection(transform, pivot);
}

QuaderMeshMaterials::QuaderMeshMaterials(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    QuaderModelingObjectRef object)
    : session_(std::move(session)), mutations_(&mutations),
      object_(std::move(object)) {}

QuaderApiVoidResult
QuaderMeshMaterials::assign_slot(std::uint32_t material_slot) const {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  QuaderObjectSelection selected(session_, *mutations_, object_);
  const QuaderApiVoidResult status = selected.replace();
  if (!status.ok()) {
    return status;
  }
  return QuaderModelingApi(*session_, *mutations_)
      .assign_material_slot_to_selection(material_slot);
}

QuaderApiVoidResult QuaderMeshMaterials::assign_to_face(
    QuaderElementId face, std::uint32_t material_slot) const {
  if (mutations_ == nullptr) {
    return invalid_api_status(session_, "Invalid mesh handle.");
  }
  return QuaderModelingApi(*session_, *mutations_)
      .assign_material_slot_to_face(object_, face, material_slot);
}

QuaderApiResult<QuaderFaceProjectMaterial>
QuaderMeshMaterials::project_material_for_face(QuaderElementId face) const {
  if (mutations_ == nullptr || !session_.has_value() ||
      !mutations_->belongs_to_active_scene(object_.scene_session_id)) {
    return quader_api_result_from_status<QuaderFaceProjectMaterial>(
        invalid_api_status(session_, "Invalid or stale mesh handle."));
  }
  return QuaderModelingApi(*session_, *mutations_)
      .project_material_for_face(object_.modeling_object_id, face);
}

QuaderApiVoidResult QuaderMeshMaterials::apply_face_material_to_mesh(
    QuaderElementId face) const {
  if (mutations_ == nullptr || !session_.has_value() ||
      !mutations_->belongs_to_active_scene(object_.scene_session_id)) {
    return invalid_api_status(session_, "Invalid or stale mesh handle.");
  }
  return QuaderModelingApi(*session_, *mutations_)
      .apply_face_material_to_mesh(object_.modeling_object_id, face);
}

QuaderMeshHandle::QuaderMeshHandle(std::optional<QuaderApiSession> session,
                                   QuaderApiMutationRunner &mutations,
                                   QuaderModelingObjectRef ref)
    : session_(std::move(session)), mutations_(&mutations),
      ref_(std::move(ref)) {}

QuaderModelingObjectRef QuaderMeshHandle::ref() const { return ref_; }

QuaderModelingObjectId QuaderMeshHandle::id() const {
  return ref_.modeling_object_id;
}

bool QuaderMeshHandle::valid() const {
  if (!session_.has_value() || !session_->valid() || mutations_ == nullptr ||
      !mutations_->belongs_to_active_scene(ref_.scene_session_id) ||
      ref_.modeling_object_id == kInvalidQuaderModelingObjectId) {
    return false;
  }
  return QuaderQueryApi(*session_, *mutations_)
      .modeling_object_for_id(ref_.modeling_object_id)
      .has_value();
}

QuaderObjectSelection QuaderMeshHandle::select() const {
  return mutations_ == nullptr ? QuaderObjectSelection{}
                               : QuaderObjectSelection(session_, *mutations_,
                                                       ref_);
}

QuaderMeshVertices QuaderMeshHandle::vertices() const {
  return mutations_ == nullptr ? QuaderMeshVertices{}
                               : QuaderMeshVertices(session_, *mutations_,
                                                    ref_);
}

QuaderMeshEdges QuaderMeshHandle::edges() const {
  return mutations_ == nullptr ? QuaderMeshEdges{}
                               : QuaderMeshEdges(session_, *mutations_, ref_);
}

QuaderMeshFaces QuaderMeshHandle::faces() const {
  return mutations_ == nullptr ? QuaderMeshFaces{}
                               : QuaderMeshFaces(session_, *mutations_, ref_);
}

QuaderMeshTransform QuaderMeshHandle::transform() const {
  return mutations_ == nullptr ? QuaderMeshTransform{}
                               : QuaderMeshTransform(session_, *mutations_,
                                                     ref_);
}

QuaderMeshMaterials QuaderMeshHandle::materials() const {
  return mutations_ == nullptr ? QuaderMeshMaterials{}
                               : QuaderMeshMaterials(session_, *mutations_,
                                                     ref_);
}

std::optional<QuaderMeshAsset> QuaderMeshHandle::compiled_mesh() const {
  if (!session_.has_value() || !session_->valid() || mutations_ == nullptr) {
    return std::nullopt;
  }
  return QuaderQueryApi(*session_, *mutations_).compiled_object_mesh(ref_);
}

QuaderModelingApi::QuaderModelingApi(QuaderApiSession session,
                                     QuaderApiMutationRunner &mutations)
    : session_(session), mutations_(mutations) {}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_box(const QuaderCreateBoxArgs &args) {
  if (!release_has_feature(ReleaseFeature::Materials) && args.material.has_value()) {
    return result_from_create(
        invalid_api_status(session_, release_feature_unavailable_message()), {});
  }
  if (args.material.has_value() &&
      (!mutations_.available() || !assignment_material_exists(session_, *args.material))) {
    return result_from_create(
        invalid_api_status(session_, "The requested material is unavailable."), {});
  }
  auto command = std::make_unique<CreateBoxFromBoundsHistoryCommand>(
      scene_session(session_).modeling(),
      ModelingCreateBoxRequest{
          .bounds = to_modeling_bounds(args.bounds),
          .name = args.name,
          .material = args.material.has_value()
                          ? native_material_reference(*args.material)
                          : default_project_material(session_),
      });
  CreateBoxFromBoundsHistoryCommand *create_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Create Box");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(create_command->created().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_box(QuaderBounds bounds, std::string name) {
  return create_box(QuaderCreateBoxArgs{
      .bounds = bounds,
      .name = std::move(name),
  });
}

QuaderApiResult<QuaderMeshHandle>
QuaderModelingApi::create_box_mesh(const QuaderCreateBoxArgs &args) {
  const QuaderApiResult<QuaderModelingObjectRef> created = create_box(args);
  QuaderMeshHandle handle(session_, mutations_, created.value());
  return quader_api_result_from_status<QuaderMeshHandle>(created, handle);
}

QuaderApiResult<QuaderMeshHandle>
QuaderModelingApi::create_box_mesh(QuaderBounds bounds, std::string name) {
  return create_box_mesh(QuaderCreateBoxArgs{
      .bounds = bounds,
      .name = std::move(name),
  });
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::commit_box_preview(std::string name,
                                      AssetId default_material) {
  if (!release_has_feature(ReleaseFeature::Materials) &&
      default_material != kInvalidAssetId) {
    return result_from_create(
        invalid_api_status(session_, release_feature_unavailable_message()), {});
  }
  const MaterialReference material = default_material != kInvalidAssetId
      ? MaterialReference(default_material)
      : default_project_material(session_);
  auto command = std::make_unique<CommitBoxPreviewCommand>(
      scene_session(session_).modeling(), scene_session(session_).scene(),
      scene_session(session_).assets(), std::move(name), material);
  CommitBoxPreviewCommand *commit_command = command.get();
  QuaderApiVoidResult status = mutations_.run_modeling_undoable_command(
      std::move(command), "Create Box");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(commit_command->committed().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_mesh(QuaderPolygonDocument document,
                               QuaderCreateMeshArgs args) {
  if (!release_has_feature(ReleaseFeature::Materials) && args.material.has_value()) {
    return result_from_create(
        invalid_api_status(session_, release_feature_unavailable_message()), {});
  }
  if (args.material.has_value() &&
      (!mutations_.available() || !assignment_material_exists(session_, *args.material))) {
    return result_from_create(
        invalid_api_status(session_, "The requested material is unavailable."), {});
  }
  ModelingCreateMeshRequest request;
  request.document = QuaderPolygonDocumentInternalAccess::to_backend(document);
  request.name = std::move(args.name);
  request.material_slot = args.material_slot;
  request.material = args.material.has_value()
                         ? native_material_reference(*args.material)
                         : default_project_material(session_);
  auto command = std::make_unique<CreateMeshHistoryCommand>(
      scene_session(session_).modeling(), std::move(request));
  CreateMeshHistoryCommand *create_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Create Mesh");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(create_command->created().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_curve(const QuaderCreateCurveArgs &args) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return quader_api_result_from_status<QuaderModelingObjectRef>(invalid_api_status(session_, release_feature_unavailable_message()));
  }
  if (!mutations_.can_mutate_modeling_content()) {
    return quader_api_result_from_status<QuaderModelingObjectRef>(
        mutations_.modeling_content_blocked_status("creating a curve."));
  }
  auto document = modeling::CurveDocument::make_bezier(args.points, args.closed);
  if (!document.ok()) {
    return quader_api_result_from_status<QuaderModelingObjectRef>(
        invalid_api_status(session_, document.error_message()));
  }
  auto command = std::make_unique<CreateCurveHistoryCommand>(
      scene_session(session_).modeling(), std::move(document).value(), args.name);
  const auto *created_command = command.get();
  auto status = mutations_.run_modeling_command(std::move(command), "Create Curve");
  QuaderModelingObjectRef created;
  if (status.ok() && status.changed()) {
    created = ref_from_record(to_api_modeling_object_record(created_command->created().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderCurveSnapshot> QuaderModelingApi::curve_snapshot(
    QuaderModelingObjectRef object, bool include_preview) const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return quader_api_result_from_status<QuaderCurveSnapshot>(invalid_api_status(session_, release_feature_unavailable_message()));
  }
  if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
    return quader_api_result_from_status<QuaderCurveSnapshot>(
        mutations_.stale_scene_reference_status("Curve"));
  }
  const auto &session = scene_session(session_);
  const auto &scene = session.scene();
  const auto *binding = scene.modeling_object(object.entity);
  if (!scene.exists(object.entity) || scene.curve(object.entity) == nullptr ||
      binding == nullptr || binding->object_id != object.modeling_object_id ||
      (!object.entity_uuid.empty() && scene.uuid(object.entity) != object.entity_uuid) ||
      (object.editor_object_id != kInvalidQuaderEditorObjectId &&
       scene.editor_object_id(object.entity) != object.editor_object_id)) {
    return quader_api_result_from_status<QuaderCurveSnapshot>(
        invalid_api_status(session_, "Curve reference is not live."));
  }
  auto document = session.modeling().curve_document(object.modeling_object_id,
                                                     include_preview);
  if (!document.has_value()) {
    return quader_api_result_from_status<QuaderCurveSnapshot>(
        invalid_api_status(session_, "Curve document is unavailable."));
  }
  return quader_api_result_from_status(
      QuaderApiStatus{.ok = true, .revisions = mutations_.current_revisions()},
      QuaderCurveSnapshot{.object = std::move(object), .document = std::move(*document)});
}

QuaderApiVoidResult QuaderModelingApi::replace_curve(
    QuaderModelingObjectRef object, modeling::CurveDocument document) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const auto current = curve_snapshot(object, false);
  if (!current.ok()) {
    return quader_api_status_from_result(current);
  }
  if (!scene_session(session_).modeling().curve_target_is_editable(
          object.modeling_object_id)) {
    return invalid_api_status(session_, "Curve is not editable in the current context.");
  }
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status("editing a curve.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ReplaceCurveHistoryCommand>(scene_session(session_).modeling(),
                                                  object.modeling_object_id,
                                                  std::move(document)),
      "Edit Curve");
}

QuaderApiVoidResult QuaderModelingApi::set_curve_point(
    QuaderModelingObjectRef object, modeling::CurvePointId point,
    const modeling::CurvePointSpec &value) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  auto current = curve_snapshot(object, false);
  if (!current.ok()) {
    return quader_api_status_from_result(current);
  }
  auto edited = current.value().document.set_point(point, value);
  if (!edited.ok()) {
    return invalid_api_status(session_, edited.error_message());
  }
  return replace_curve(object, std::move(current.value().document));
}

QuaderApiVoidResult QuaderModelingApi::toggle_curve_point_smoothness(
    QuaderModelingObjectRef object, modeling::CurvePointId point) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  auto current = curve_snapshot(object, false);
  if (!current.ok()) {
    return quader_api_status_from_result(current);
  }
  const auto edited = current.value().document.toggle_point_smoothness(point);
  if (!edited.ok()) {
    return invalid_api_status(session_, edited.error_message());
  }
  return replace_curve(object, std::move(current.value().document));
}

QuaderApiVoidResult QuaderModelingApi::set_curve_closed(
    QuaderModelingObjectRef object, bool closed) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  auto current = curve_snapshot(object, false);
  if (!current.ok()) {
    return quader_api_status_from_result(current);
  }
  auto edited = current.value().document.set_closed(closed);
  if (!edited.ok()) {
    return invalid_api_status(session_, edited.error_message());
  }
  return replace_curve(object, std::move(current.value().document));
}

QuaderApiVoidResult QuaderModelingApi::begin_curve_preview(QuaderModelingObjectRef object) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const auto current = curve_snapshot(object, false);
  if (!current.ok()) {
    return quader_api_status_from_result(current);
  }
  if (!scene_session(session_).modeling().curve_target_is_editable(
          object.modeling_object_id)) {
    return invalid_api_status(session_, "Curve is not editable in the current context.");
  }
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status("starting a curve preview.");
  }
  const QuaderSettingsOperationOriginCapture origin(session_);
  const ModelingCommandResult result =
      scene_session(session_).modeling().begin_curve_document_preview(
          object.modeling_object_id);
  if (result.ok && result.changed) {
    origin.publish();
  }
  return mutations_.run_modeling_result(result, "Begin Curve Preview");
}

QuaderApiVoidResult QuaderModelingApi::update_curve_preview(modeling::CurveDocument document) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().update_curve_document_preview(std::move(document)),
      "Update Curve Preview");
}

QuaderApiVoidResult QuaderModelingApi::commit_curve_preview() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<CommitCurvePreviewHistoryCommand>(scene_session(session_).modeling()),
      "Edit Curve");
}

QuaderApiVoidResult QuaderModelingApi::cancel_curve_preview() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().cancel_curve_document_preview(),
      "Cancel Curve Preview");
}

QuaderApiResult<QuaderLinearArrayResult>
QuaderModelingApi::create_linear_array(const QuaderLinearArrayArgs &args) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return quader_api_result_from_status<QuaderLinearArrayResult>(invalid_api_status(session_, release_feature_unavailable_message()));
  }
  SceneSession &session = scene_session(session_);
  if (!mutations_.belongs_to_active_scene(args.source.scene_session_id)) {
    return quader_api_result_from_status<QuaderLinearArrayResult>(
        mutations_.stale_scene_reference_status("Linear Array source"), {});
  }
  const SceneDocument &scene = session.scene();
  const ModelingObjectComponent *modeling =
      scene.modeling_object(args.source.entity);
  const MeshComponent *mesh = scene.mesh(args.source.entity);
  const MaterialComponent *material = scene.material(args.source.entity);
  const bool source_matches =
      args.source.entity != kInvalidEntityId &&
      scene.exists(args.source.entity) && modeling != nullptr &&
      mesh != nullptr &&
      modeling->object_id == args.source.modeling_object_id &&
      (args.source.entity_uuid.empty() ||
       scene.uuid(args.source.entity) == args.source.entity_uuid) &&
      (args.source.editor_object_id == kInvalidQuaderEditorObjectId ||
       scene.editor_object_id(args.source.entity) ==
           args.source.editor_object_id) &&
      (args.source.mesh == kInvalidAssetId || mesh->mesh == args.source.mesh) &&
      (args.source.material == kInvalidAssetId ||
       (material != nullptr &&
        material->material == args.source.material));
  if (!source_matches) {
    return quader_api_result_from_status<QuaderLinearArrayResult>(
        invalid_api_status(session_,
                           "Linear Array source reference is stale."),
        {});
  }

  auto command = std::make_unique<LinearArrayHistoryCommand>(
      session.modeling(),
      ModelingLinearArrayRequest{
          .sources = {{
              .entity = args.source.entity,
              .modeling_object_id = args.source.modeling_object_id,
          }},
          .span_world =
              args.endpoint_world -
              transform_point(session.instances()
                                  .effective_member_world_transform(args.source.entity)
                                  .value_or(scene.world_transform_matrix(args.source.entity)),
                              {}),
          .count = args.count,
      });
  LinearArrayHistoryCommand *array_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Linear Array");

  QuaderLinearArrayResult created;
  if (status.ok && status.changed &&
      !array_command->created().source_arrays.empty()) {
    const ModelingLinearArraySourceResult &source_array =
        array_command->created().source_arrays.front();
    created.elements.reserve(source_array.elements.size());
    for (const ModelingObjectRecord &record :
         source_array.elements) {
      created.elements.push_back(
          ref_from_record(to_api_modeling_object_record(record)));
    }
  }
  return quader_api_result_from_status(status, std::move(created));
}

QuaderApiResult<QuaderRadialArrayResult>
QuaderModelingApi::create_radial_array(const QuaderRadialArrayArgs &args) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return quader_api_result_from_status<QuaderRadialArrayResult>(invalid_api_status(session_, release_feature_unavailable_message()));
  }
  SceneSession &session = scene_session(session_);
  const SceneDocument &scene = session.scene();
  if (args.sources.empty()) {
    return quader_api_result_from_status<QuaderRadialArrayResult>(
        invalid_api_status(session_,
                           "Radial Array needs at least one source mesh."),
        {});
  }
  if (args.mode != QuaderRadialArrayMode::Total &&
      args.mode != QuaderRadialArrayMode::Spacing) {
    return quader_api_result_from_status<QuaderRadialArrayResult>(
        invalid_api_status(session_, "Radial Array angle mode is invalid."),
        {});
  }

  std::unordered_set<EntityId> source_entities;
  source_entities.reserve(args.sources.size());
  std::unordered_set<QuaderModelingObjectId> source_modeling_objects;
  source_modeling_objects.reserve(args.sources.size());
  ModelingRadialArrayRequest request{
      .center_world = args.center_world,
      .plane_normal_world = args.plane_normal_world,
      .degrees = args.degrees,
      .copies = args.copies,
      .repeats = args.repeats,
      .repeat_spacing = args.repeat_spacing,
      .mode = args.mode == QuaderRadialArrayMode::Spacing
                  ? RadialArrayMode::Spacing
                  : RadialArrayMode::Total,
  };
  request.sources.reserve(args.sources.size());
  for (const QuaderModelingObjectRef &source : args.sources) {
    if (!mutations_.belongs_to_active_scene(source.scene_session_id)) {
      return quader_api_result_from_status<QuaderRadialArrayResult>(
          mutations_.stale_scene_reference_status("Radial Array source"),
          {});
    }
    const ModelingObjectComponent *modeling =
        scene.modeling_object(source.entity);
    const MeshComponent *mesh = scene.mesh(source.entity);
    const MaterialComponent *material = scene.material(source.entity);
    const bool source_matches =
        source.entity != kInvalidEntityId && scene.exists(source.entity) &&
        modeling != nullptr && mesh != nullptr &&
        modeling->object_id == source.modeling_object_id &&
        (source.entity_uuid.empty() ||
         scene.uuid(source.entity) == source.entity_uuid) &&
        (source.editor_object_id == kInvalidQuaderEditorObjectId ||
         scene.editor_object_id(source.entity) == source.editor_object_id) &&
        (source.mesh == kInvalidAssetId || mesh->mesh == source.mesh) &&
        (source.material == kInvalidAssetId ||
         (material != nullptr && material->material == source.material));
    if (!source_matches) {
      return quader_api_result_from_status<QuaderRadialArrayResult>(
          invalid_api_status(session_,
                             "Radial Array source reference is stale."),
          {});
    }
    if (!source_entities.insert(source.entity).second ||
        !source_modeling_objects.insert(source.modeling_object_id).second) {
      return quader_api_result_from_status<QuaderRadialArrayResult>(
          invalid_api_status(
              session_, "Radial Array source references must be unique."),
          {});
    }
    request.sources.push_back({
        .entity = source.entity,
        .modeling_object_id = source.modeling_object_id,
    });
  }

  auto command = std::make_unique<RadialArrayHistoryCommand>(
      session.modeling(), std::move(request));
  RadialArrayHistoryCommand *array_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Radial Array");

  QuaderRadialArrayResult created;
  if (status.ok && status.changed) {
    created.source_arrays.reserve(
        array_command->created().source_arrays.size());
    for (const ModelingRadialArraySourceResult &source_result :
         array_command->created().source_arrays) {
      QuaderRadialArraySourceResult api_source_result;
      api_source_result.source = ref_from_record(
          to_api_modeling_object_record(source_result.source));
      api_source_result.elements.reserve(source_result.elements.size());
      for (const ModelingObjectRecord &record : source_result.elements) {
        api_source_result.elements.push_back(
            ref_from_record(to_api_modeling_object_record(record)));
      }
      created.source_arrays.push_back(std::move(api_source_result));
    }
  }
  return quader_api_result_from_status(status, std::move(created));
}

QuaderApiResult<QuaderMeshHandle>
QuaderModelingApi::create_mesh_handle(QuaderPolygonDocument document,
                                      QuaderCreateMeshArgs args) {
  const QuaderApiResult<QuaderModelingObjectRef> created =
      create_mesh(std::move(document), std::move(args));
  QuaderMeshHandle handle(session_, mutations_, created.value());
  return quader_api_result_from_status<QuaderMeshHandle>(created, handle);
}

QuaderMeshHandle QuaderModelingApi::mesh(QuaderModelingObjectRef object) const {
  return QuaderMeshHandle(session_, mutations_, std::move(object));
}

QuaderApiResult<QuaderMeshHandle>
QuaderModelingApi::mesh(QuaderModelingObjectId object_id) const {
  const std::optional<QuaderModelingObjectRef> object =
      QuaderQueryApi(session_, mutations_).modeling_object_for_id(object_id);
  if (!object.has_value()) {
    return quader_api_result_from_status<QuaderMeshHandle>(
        invalid_api_status(session_, "Modeling object was not found."),
        QuaderMeshHandle(session_, mutations_, {}));
  }
  return quader_api_result_from_status<QuaderMeshHandle>(
      quader_api_void_status(scene_session(session_), true, false, {},
                             QuaderMutationKind::None),
      QuaderMeshHandle(session_, mutations_, *object));
}

QuaderApiVoidResult
QuaderModelingApi::set_selection_mode(QuaderSelectionMode mode) {
  return mutations_.set_selection_mode(to_modeling_selection_mode(mode));
}

bool QuaderModelingApi::can_execute_tool(std::string_view tool_id) const {
  if ((tool_id == "copy_selection" || tool_id == "paste_selection") &&
      (!mutations_.available() || scene_session(session_).editor_mode().mode() != EditorMode::Object)) {
    return false;
  }
  if (!release_allows_action(tool_id) || !release_allows_operation(tool_id) ||
      !release_allows_tool(tool_id)) {
    return false;
  }
  if (ProjectSession *project =
          QuaderApiSessionInternalAccess::project_session(session_)) {
    if (tool_id == "copy_selection" &&
        project->clipboard().selection_contains_instance(*project)) {
      return project->clipboard().can_copy_instance_selection(*project);
    }
    if (tool_id == "paste_selection") {
      if (project->clipboard().has_instance_payload()) {
        return project->clipboard().can_paste_instance_selection(*project);
      }
      if (project->clipboard().has_ordinary_payload() &&
          !project->clipboard().ordinary_payload_belongs_to(
              project->active_scene_id())) {
        return false;
      }
    }
  }
  return mutations_.can_run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::execute_tool(std::string_view tool_id) {
  if (!release_allows_action(tool_id) || !release_allows_operation(tool_id) ||
      !release_allows_tool(tool_id)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (tool_id == "copy_selection") {
    return copy_selection();
  }
  if (tool_id == "paste_selection") {
    return paste_selection();
  }
  if (tool_id == "repeat_last_action") {
    return repeat_last_action();
  }
  return mutations_.run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::delete_selection() {
  return mutations_.run_tracked_modeling_tool("delete_selection");
}

bool QuaderModelingApi::can_cut_selection() const {
  return can_execute_tool("copy_selection") &&
         can_execute_tool("delete_selection");
}

QuaderApiVoidResult QuaderModelingApi::cut_selection() {
  if (!can_cut_selection()) {
    return invalid_api_status(
        session_, "Cut needs a selection that can be copied and deleted.");
  }
  QuaderApiVoidResult copied = copy_selection();
  if (!copied.ok()) {
    return copied;
  }
  return delete_selection();
}

QuaderApiVoidResult QuaderModelingApi::copy_selection() {
  if (!mutations_.available() || scene_session(session_).editor_mode().mode() != EditorMode::Object) {
    return invalid_api_status(session_, "Whole-object clipboard actions require Object Mode.");
  }
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project != nullptr &&
      project->clipboard().selection_contains_instance(*project)) {
    return mutations_.run_document_command(
        project->clipboard().copy_instance_selection(*project),
        "Copy Instances");
  }

  QuaderApiVoidResult copied =
      mutations_.run_tracked_modeling_tool("copy_selection");
  if (project != nullptr && copied.ok()) {
    project->clipboard().note_ordinary_copy(project->active_scene_id());
  }
  return copied;
}

QuaderApiVoidResult QuaderModelingApi::paste_selection() {
  if (!mutations_.available() || scene_session(session_).editor_mode().mode() != EditorMode::Object) {
    return invalid_api_status(session_, "Whole-object clipboard actions require Object Mode.");
  }
  if (ProjectSession *project =
          QuaderApiSessionInternalAccess::project_session(session_)) {
    if (project->clipboard().has_instance_payload()) {
      return mutations_.run_document_command(
          project->clipboard().paste_instance_selection(*project),
          "Paste Instances");
    }
    if (project->clipboard().has_ordinary_payload() &&
        !project->clipboard().ordinary_payload_belongs_to(
            project->active_scene_id())) {
      return invalid_api_status(
          session_,
          "Ordinary mesh clipboard data belongs to another scene. Copy the "
          "mesh in this scene before pasting.");
    }
  }
  return mutations_.run_tracked_modeling_tool("paste_selection");
}

QuaderApiVoidResult QuaderModelingApi::repeat_last_action() {
  return mutations_.repeat_last_action();
}

QuaderApiVoidResult QuaderModelingApi::duplicate_meshes() {
  return mutations_.run_tracked_modeling_tool("duplicate_meshes");
}

QuaderApiVoidResult QuaderModelingApi::combine_meshes() {
  return mutations_.run_tracked_modeling_tool("combine_meshes");
}

QuaderApiVoidResult QuaderModelingApi::remove_doubles() {
  return mutations_.run_tracked_modeling_tool("remove_doubles");
}

bool QuaderModelingApi::can_copy_pivot_from_active() const {
  if (!release_has_feature(ReleaseFeature::Pivot)) {
    return false;
  }
  const SceneSession &session = scene_session(session_);
  return copy_pivot_from_active_is_available(session);
}

QuaderApiVoidResult QuaderModelingApi::copy_pivot_from_active() {
  if (!release_has_feature(ReleaseFeature::Pivot)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool() ||
      session.modeling().can_cancel_active_tool_or_preview()) {
    return mutations_.run_document_command(
        command_invalid_result(
            "Apply or cancel the active tool before copying a pivot."),
        "Copy Pivot From Active");
  }
  return mutations_.run_document_command(
      session.pivot().copy_pivot_from_active(session.selection()),
      "Copy Pivot From Active");
}

bool QuaderModelingApi::can_bake_transforms(QuaderTransformBakeMode mode) const {
  if (!release_has_feature(ReleaseFeature::TransformBaking)) {
    return false;
  }
  const SceneSession &session = scene_session(session_);
  const auto native_mode = to_mesh_transform_bake_mode(mode);
  return native_mode.has_value() &&
         native_modeling_action_execution_is_available(session) &&
         session.mesh_transform_bake().can_bake(session.selection(),
                                                *native_mode);
}

QuaderApiVoidResult
QuaderModelingApi::bake_transforms(QuaderTransformBakeMode mode) {
  if (!release_has_feature(ReleaseFeature::TransformBaking)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  const auto native_mode = to_mesh_transform_bake_mode(mode);
  if (!native_mode.has_value()) {
    return invalid_api_status(session_, "Invalid transform bake mode.");
  }
  if (!native_modeling_action_execution_is_available(session)) {
    return invalid_api_status(
        session_, "Finish or cancel the active tool before baking transforms.");
  }
  return mutations_.run_document_command(
      session.mesh_transform_bake().bake(session.selection(), *native_mode),
      std::string(transform_bake_label(mode)));
}

bool QuaderModelingApi::can_snap_selected_meshes_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return false;
  }
  const SceneSession &session = scene_session(session_);
  return native_modeling_action_execution_is_available(session) &&
         session.snap_to_floor().can_snap_selected_meshes_to_floor();
}

QuaderApiVoidResult QuaderModelingApi::snap_selected_meshes_to_floor() {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (!native_modeling_action_execution_is_available(session)) {
    return invalid_api_status(
        session_,
        "Apply or cancel the active tool before snapping to floor.");
  }
  return mutations_.run_document_result(
      session.snap_to_floor().snap_selected_meshes_to_floor(),
      "Snap Meshes to Floor");
}

bool QuaderModelingApi::can_snap_selected_vertices_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return false;
  }
  const SceneSession &session = scene_session(session_);
  return native_modeling_action_execution_is_available(session) &&
         session.snap_to_floor().can_snap_selected_vertices_to_floor();
}

QuaderApiVoidResult QuaderModelingApi::snap_selected_vertices_to_floor() {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (!native_modeling_action_execution_is_available(session)) {
    return invalid_api_status(
        session_,
        "Apply or cancel the active tool before snapping to floor.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<SnapVerticesToFloorHistoryCommand>(
          session.modeling(), session.snap_to_floor()),
      "Snap Vertices to Floor");
}

const QuaderModelingApi::MeshArrangementAvailabilityCache &
QuaderModelingApi::mesh_arrangement_availability() const {
  QDR_PROFILE_SCOPE("Quader modeling arrangement availability");
  SceneSession &session = scene_session(session_);
  const SceneDocument &scene = session.scene();
  const AssetRegistry &assets = session.assets();
  const ModelingSessionService &modeling = session.modeling();
  const std::uint64_t scene_session_id =
      QuaderApiSessionInternalAccess::scene_session_id(session_).packed();
  const std::uint64_t scene_content_revision = scene.content_revision();
  const std::uint64_t scene_selection_revision = scene.selection_revision();
  const std::uint64_t asset_revision = assets.revision();
  const std::uint64_t modeling_content_revision = modeling.content_revision();
  const std::uint64_t modeling_selection_revision =
      modeling.selection_revision();
  const std::uint64_t modeling_object_map_revision =
      modeling.modeling_object_map_revision();

  const bool unchanged =
      mesh_arrangement_availability_.valid &&
      mesh_arrangement_availability_.scene_identity == &session &&
      mesh_arrangement_availability_.scene_session_id == scene_session_id &&
      mesh_arrangement_availability_.scene_content_revision ==
          scene_content_revision &&
      mesh_arrangement_availability_.scene_selection_revision ==
          scene_selection_revision &&
      mesh_arrangement_availability_.asset_revision == asset_revision &&
      mesh_arrangement_availability_.modeling_content_revision ==
          modeling_content_revision &&
      mesh_arrangement_availability_.modeling_selection_revision ==
          modeling_selection_revision &&
      mesh_arrangement_availability_.modeling_object_map_revision ==
          modeling_object_map_revision;
  if (unchanged) {
    return mesh_arrangement_availability_;
  }

  {
    QDR_PROFILE_SCOPE("Quader modeling arrangement availability cache miss");
    MeshArrangementService arrangement(
        session.scene(), session.assets(),
        mesh_arrangement_selection_context(session),
        session.document_commands());
    const MeshArrangementAvailability availability =
        arrangement.availability();
    mesh_arrangement_availability_ = {
        .scene_identity = &session,
        .scene_session_id = scene_session_id,
        .scene_content_revision = scene_content_revision,
        .scene_selection_revision = scene_selection_revision,
        .asset_revision = asset_revision,
        .modeling_content_revision = modeling_content_revision,
        .modeling_selection_revision = modeling_selection_revision,
        .modeling_object_map_revision = modeling_object_map_revision,
        .can_align = availability.can_align,
        .can_distribute = availability.can_distribute,
        .valid = true,
    };
  }
  return mesh_arrangement_availability_;
}

bool QuaderModelingApi::can_align_selected_meshes() const {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return false;
  }
  return mesh_arrangement_availability().can_align;
}

bool QuaderModelingApi::can_distribute_selected_meshes() const {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return false;
  }
  return mesh_arrangement_availability().can_distribute;
}

QuaderApiVoidResult QuaderModelingApi::align_selected_meshes(
    QuaderTransformAxis axis, QuaderMeshAlignment alignment,
    QuaderMeshArrangementOptions options) {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const std::optional<MeshArrangementAxis> arrangement_axis =
      mesh_arrangement_axis(axis);
  if (!arrangement_axis.has_value()) {
    return invalid_api_status(session_,
                              "Mesh alignment needs the X, Y, or Z axis.");
  }
  SceneSession &session = scene_session(session_);
  MeshArrangementService arrangement(
      session.scene(), session.assets(),
      mesh_arrangement_selection_context(session),
      session.document_commands());
  return mutations_.run_document_result(
      arrangement.align_selected_meshes(
          *arrangement_axis, mesh_alignment_mode(alignment),
          mesh_arrangement_options(options)),
      "Align Meshes");
}

QuaderApiVoidResult QuaderModelingApi::distribute_selected_meshes(
    QuaderTransformAxis axis, QuaderMeshDistribution distribution,
    QuaderMeshArrangementOptions options) {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  const std::optional<MeshArrangementAxis> arrangement_axis =
      mesh_arrangement_axis(axis);
  if (!arrangement_axis.has_value()) {
    return invalid_api_status(
        session_, "Mesh distribution needs the X, Y, or Z axis.");
  }
  SceneSession &session = scene_session(session_);
  MeshArrangementService arrangement(
      session.scene(), session.assets(),
      mesh_arrangement_selection_context(session),
      session.document_commands());
  return mutations_.run_document_result(
      arrangement.distribute_selected_meshes(
          *arrangement_axis, mesh_distribution_mode(distribution),
          mesh_arrangement_options(options)),
      "Distribute Meshes");
}

QuaderApiVoidResult
QuaderModelingApi::begin_preview(std::string_view action_id) {
  if (!release_allows_action(action_id) || !release_allows_operation(action_id) ||
      !release_allows_tool(action_id)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  return mutations_.begin_tracked_modeling_preview(action_id);
}

QuaderApiVoidResult
QuaderModelingApi::update_preview(const QuaderPreviewSettings &settings) {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().set_interaction_settings(
          to_modeling_interaction_settings(settings.interaction)),
      "Update Operation Preview");
}

QuaderApiVoidResult QuaderModelingApi::commit_preview(std::string label) {
  return mutations_.commit_modeling_preview(std::move(label));
}

QuaderApiVoidResult QuaderModelingApi::cancel_preview() {
  return mutations_.cancel_modeling_preview();
}

bool QuaderModelingApi::can_cancel_active_tool_or_preview() const {
  return scene_session(session_)
      .modeling()
      .can_cancel_active_tool_or_preview();
}

QuaderApiVoidResult
QuaderModelingApi::begin_transform_preview(bool duplicate_selected) {
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status(
        "starting a transform preview.");
  }
  SceneSession &session = scene_session(session_);
  const QuaderSettingsOperationOriginCapture origin(session_);
  const ModelingStructuralDuplicate structural_duplicate =
      structural_duplicate_for_transform(session);
  const ModelingCommandResult result = session.modeling().begin_transform_preview(
      duplicate_selected, structural_duplicate, session.history());
  if (result.ok && result.changed) {
    origin.publish();
  }
  return mutations_.run_modeling_result(result, "Begin Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::update_transform_translate(QVec3 delta) {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().update_transform_translate_preview(
          delta),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_matrix(const QuaderTransform3 &transform,
                                           QVec3 pivot) {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().update_transform_matrix_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_extrude(QVec3 offset,
                                            float closed_edge_ledge_size) {
  if (!release_has_feature(ReleaseFeature::ExtrudeTool)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().update_transform_extrude_preview(
          offset, closed_edge_ledge_size),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_inset(const QuaderTransform3 &transform,
                                          QVec3 pivot) {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().update_transform_inset_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::commit_transform_preview(std::string label) {
  SceneSession &session = scene_session(session_);
  const std::string history_label =
      label.empty() ? "Commit Transform Preview" : std::move(label);
  if (session.modeling().structural_transform_preview_active()) {
    return mutations_.run_modeling_result(
        session.modeling().commit_structural_transform_preview(history_label),
        history_label);
  }
  return mutations_.run_modeling_command(
      std::make_unique<CommitTransformPreviewHistoryCommand>(
          session.modeling()),
      history_label);
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_preview() {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().cancel_transform_preview(),
      "Cancel Transform Preview");
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::begin_transform_drag(
    const QuaderTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  if (!mutations_.can_mutate_modeling_content()) {
    return quader_api_result_from_status<QuaderTransformDragPreview>(
        mutations_.modeling_content_blocked_status(
            "starting a transform drag."));
  }
  SceneSession &session = scene_session(session_);
  const ModelingStructuralDuplicate structural_duplicate =
      structural_duplicate_for_transform(session);
  return result_from_transform_drag_preview(
      session, session.modeling().begin_transform_drag(
                   to_modeling_transform_drag_begin_request(request),
                   session.document_commands(),
                   structural_duplicate, session.history()));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::update_transform_drag(
    const QuaderTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      scene_session(session_),
      scene_session(session_).modeling().update_transform_drag(
          to_modeling_transform_drag_update_request(request)));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::commit_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      scene_session(session_),
      scene_session(session_).modeling().commit_transform_drag(
          scene_session(session_).history()));
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_drag() {
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().cancel_transform_drag(),
      "Cancel Transform Drag");
}

QuaderApiVoidResult QuaderModelingApi::move_selection(QVec3 delta) {
  SceneSession &session = scene_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    const QMat4 display_from_scene =
        session.instances().active_edit_display_from_scene().value_or(
            scene_transform_identity_matrix());
    return mutations_.run_document_result(
        transforms.move_selected_by_world_delta(session.document_commands(),
                                                session.scene(), delta,
                                                "Move Selection",
                                                display_from_scene),
        "Move Selection");
  }

  return mutations_.run_modeling_command(
      std::make_unique<MoveSelectionHistoryCommand>(session.modeling(), delta),
      "Move Selection");
}

bool QuaderModelingApi::can_nudge_selection(bool duplicate_selected) const {
  return mutations_.can_nudge_selection(duplicate_selected);
}

QuaderApiVoidResult QuaderModelingApi::nudge_selection(
    const QuaderResolvedNudgeRequest &request) {
  if (!can_nudge_selection(request.duplicate_selected)) {
    return mutations_.modeling_content_blocked_status("nudging the selection.");
  }
  SceneSession &session = scene_session(session_);
  if ((request.tool != QuaderTransformTool::Move && request.tool != QuaderTransformTool::Rotate) ||
      (request.space != QuaderTransformSpace::Global && request.space != QuaderTransformSpace::Local)) {
    return quader_api_failure_status(session, "The resolved nudge transform is invalid.");
  }
  const ModelingStructuralDuplicate structural_duplicate =
      structural_duplicate_for_transform(session);
  return mutations_.run_modeling_result(
      session.modeling().nudge_selection(
          {.tool = to_modeling_transform_tool(request.tool),
           .axis = to_modeling_transform_axis(request.axis),
           .space = request.space == QuaderTransformSpace::Local
                        ? ModelingTransformSpace::Local : ModelingTransformSpace::Global,
           .orientation = to_modeling_transform3(request.orientation),
           .pivot = request.pivot,
           .move_delta = request.move_delta,
           .rotate_angle_degrees = request.rotate_angle_degrees,
           .duplicate_selected = request.duplicate_selected,
           .extrusion_grid_size = request.extrusion_grid_size},
          session.document_commands(), session.history(), structural_duplicate),
      "Nudge Selection");
}

QuaderApiVoidResult QuaderModelingApi::rotate_selection(QVec3 euler_radians,
                                                        QVec3 pivot) {
  return apply_selection_transform(session_, mutations_,
                                    rotation_transform(euler_radians), pivot,
                                    SceneTextureTransformKind::Rotation);
}

QuaderApiVoidResult QuaderModelingApi::scale_selection(QVec3 scale,
                                                       QVec3 pivot) {
  return apply_selection_transform(session_, mutations_, scale_transform(scale),
                                    pivot, SceneTextureTransformKind::Scale);
}

QuaderApiVoidResult
QuaderModelingApi::transform_selection(const QuaderTransform3 &transform,
                                       QVec3 pivot) {
  return apply_selection_transform(session_, mutations_, transform, pivot,
                                    SceneTextureTransformKind::Combined);
}

bool QuaderModelingApi::can_duplicate_selected_entity_hierarchy() const {
  QDR_PROFILE_FUNCTION();
  const SceneSession &session = scene_session(session_);
  if (session.native_tools().blocks_modeling_content_mutation()) {
    return false;
  }
  const QuaderSelectionSummary selection = selection_summary();
  if (!selection.has_selection ||
      (selection.mode != QuaderSelectionMode::Object &&
       selection.mode != QuaderSelectionMode::Group)) {
    return false;
  }

  if (!session.structural_lifecycle().can_duplicate_selected_hierarchy()) {
    return false;
  }
  const SceneDocument &scene = session.scene();
  for (const EntityId entity : scene.selected_entities()) {
    if (!scene.is_effectively_locked(entity)) {
      return true;
    }
  }
  return false;
}

QuaderApiVoidResult QuaderModelingApi::duplicate_selected_entity_hierarchy() {
  QDR_PROFILE_FUNCTION();
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status(
        "duplicating modeling content.");
  }
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_command(
      session.structural_lifecycle().duplicate_selected_hierarchy(
          session.modeling().selection_mode()),
      "Duplicate Selection");
}

bool QuaderModelingApi::can_create_instance() const {
  const SceneSession &session = scene_session(session_);
  return !session.native_tools().has_pending_tool() &&
         session.instances().can_create_instance();
}

QuaderApiVoidResult QuaderModelingApi::create_instance() {
  return quader_api_status_from_result(create_instance_from_selection());
}

bool QuaderModelingApi::can_create_instance_from_selection() const {
  return can_create_instance();
}

QuaderApiResult<QuaderInstanceCreateResult>
QuaderModelingApi::create_instance_from_selection() {
  if (!release_has_feature(ReleaseFeature::Instancing)) {
    return quader_api_result_from_status<QuaderInstanceCreateResult>(
        invalid_api_status(session_, release_feature_unavailable_message()));
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return quader_api_result_from_status<QuaderInstanceCreateResult>(
        mutations_.run_document_command(
            command_invalid_result(
                "Apply or cancel the active tool before creating an instance."),
            "Create Instance"));
  }
  const SceneInstanceMutationResult mutation =
      session.instances().create_instance();
  const QuaderApiVoidResult status = mutations_.run_document_command(
      mutation.command_result(), "Create Instance");
  QuaderInstanceCreateResult value;
  value.definition.scene_session_id = mutations_.scene_session_id();
  value.definition.root = instance_entity_ref(
      session, mutations_.scene_session_id(), mutation.definition);
  if (session.scene().exists(mutation.definition)) {
    for (const EntityId member : session.scene().children(mutation.definition)) {
      value.definition.members.push_back(instance_entity_ref(
          session, mutations_.scene_session_id(), member));
    }
  }
  value.occurrence.scene_session_id = mutations_.scene_session_id();
  value.occurrence.root = instance_entity_ref(
      session, mutations_.scene_session_id(), mutation.occurrence);
  value.occurrence.definition = value.definition.root;
  return quader_api_result_from_status(status, std::move(value));
}

bool QuaderModelingApi::can_flatten_instance() const {
  return can_flatten_selected_instances();
}

QuaderApiVoidResult QuaderModelingApi::flatten_instance() {
  return quader_api_status_from_result(flatten_selected_instances());
}

bool QuaderModelingApi::can_flatten_selected_instances() const {
  const SceneSession &session = scene_session(session_);
  return !session.native_tools().has_pending_tool() &&
         session.instances().can_flatten_selected_instances();
}

QuaderApiResult<QuaderInstanceFlattenResult>
QuaderModelingApi::flatten_selected_instances() {
  if (!release_has_feature(ReleaseFeature::Instancing)) {
    return quader_api_result_from_status<QuaderInstanceFlattenResult>(
        invalid_api_status(session_, release_feature_unavailable_message()));
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return quader_api_result_from_status<QuaderInstanceFlattenResult>(
        mutations_.run_document_command(
            command_invalid_result(
                "Apply or cancel the active tool before flattening an instance."),
            "Flatten Instance"));
  }
  const SceneInstanceMutationResult mutation =
      session.instances().flatten_selected_instances();
  const QuaderApiVoidResult status = mutations_.run_document_command(
      mutation.command_result(), "Flatten Instance");
  QuaderInstanceFlattenResult value;
  value.scene_session_id = mutations_.scene_session_id();
  value.definition = instance_entity_ref(
      session, mutations_.scene_session_id(), mutation.definition);
  value.root_entities.reserve(mutation.entities.size());
  for (const EntityId entity : mutation.entities) {
    value.root_entities.push_back(instance_entity_ref(
        session, mutations_.scene_session_id(), entity));
  }
  return quader_api_result_from_status(status, std::move(value));
}

bool QuaderModelingApi::can_enter_instance_edit(EntityId occurrence) const {
  const SceneSession &session = scene_session(session_);
  return !session.native_tools().has_pending_tool() &&
         session.instances().can_enter_instance_edit(occurrence);
}

QuaderApiVoidResult
QuaderModelingApi::enter_instance_edit(EntityId occurrence) {
  if (!release_has_feature(ReleaseFeature::Instancing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.run_document_command(
        command_invalid_result(
            "Apply or cancel the active tool before editing an instance."),
        "Enter Instance Edit");
  }
  const SceneInstanceMutationResult result =
      session.instances().enter_instance_edit(occurrence);
  return mutations_.run_runtime_result(result.command_result(),
                                       "Enter Instance Edit");
}

bool QuaderModelingApi::can_exit_instance_edit() const {
  return scene_session(session_).instances().can_exit_instance_edit();
}

QuaderApiVoidResult QuaderModelingApi::exit_instance_edit() {
  if (!release_has_feature(ReleaseFeature::Instancing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (!session.instances().can_exit_instance_edit()) {
    return invalid_api_status(session_, "No instance edit scope is active.");
  }
  const CommandResult accepted = session.document_commands().accept_active_property_edit();
  if (!accepted.ok) {
    return mutations_.run_runtime_result(accepted, "Exit Instance Edit");
  }
  const CommandResult settled = session.native_tools().settle_transient_state();
  if (!settled.ok) {
    return mutations_.run_runtime_result(settled, "Exit Instance Edit");
  }
  const SceneInstanceMutationResult result =
      session.instances().exit_instance_edit();
  return mutations_.run_runtime_result(result.command_result(),
                                       "Exit Instance Edit");
}

std::uint64_t QuaderModelingApi::instance_edit_revision() const {
  return scene_session(session_).instances().instance_edit_revision();
}

QuaderInstanceEditState QuaderModelingApi::instance_edit_state() const {
  const SceneSession &session = scene_session(session_);
  const SceneInstanceEditState state =
      session.instances().instance_edit_state();
  return {
      .active = state.active,
      .occurrence = instance_entity_ref(session, mutations_.scene_session_id(),
                                        state.occurrence),
      .definition = instance_entity_ref(session, mutations_.scene_session_id(),
                                        state.definition),
      .scene_name = state.scene_name,
      .instance_name = state.instance_name,
  };
}

QuaderApiVoidResult
QuaderModelingApi::apply_material(QuaderMaterialReference material) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available() || !assignment_material_exists(session_, material)) {
    return invalid_api_status(session_, "The requested material is unavailable.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyMaterialHistoryCommand>(
          scene_session(session_).modeling(), native_material_reference(material)),
      "Assign Material");
}

QuaderApiVoidResult QuaderModelingApi::apply_material_to_selected_faces(
    QuaderMaterialReference material, SceneSessionId scene_session_id,
    std::uint64_t selection_token) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available() ||
      scene_session_id != mutations_.scene_session_id()) {
    return invalid_api_status(session_, "The scene changed before material assignment.");
  }
  if (!assignment_material_exists(session_, material)) {
    return invalid_api_status(session_, "The requested material is unavailable.");
  }
  const ModelingSelectionSummary selected =
      scene_session(session_).modeling().selection_summary();
  if (selected.mode != ModelingSelectionMode::Face || selected.selected_face_count == 0u) {
    return invalid_api_status(session_, "Select one or more faces before assigning material.");
  }
  if (selection_token == 0u || selection_token !=
      scene_session(session_).modeling().face_texture_settings().selection_token) {
    return invalid_api_status(session_, "The face selection changed before material assignment.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyMaterialHistoryCommand>(
          scene_session(session_).modeling(), native_material_reference(material),
          selection_token),
      "Assign Face Material");
}

QuaderApiVoidResult QuaderModelingApi::apply_material_to_mesh(
    QuaderModelingObjectId object_id, QuaderElementId face_id,
    QuaderMaterialReference material) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available() || !assignment_material_exists(session_, material)) {
    return invalid_api_status(session_, "The requested material is unavailable.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             true)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyMaterialToTargetHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id,
          native_material_reference(material),
          ApplyMaterialToTargetHistoryCommand::Scope::Mesh),
      "Assign Material to Mesh");
}

QuaderApiVoidResult QuaderModelingApi::apply_material_to_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id,
    QuaderMaterialReference material) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available() || !assignment_material_exists(session_, material)) {
    return invalid_api_status(session_, "The requested material is unavailable.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             false)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyMaterialToTargetHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id,
          native_material_reference(material),
          ApplyMaterialToTargetHistoryCommand::Scope::Face),
      "Assign Material to Face");
}

QuaderApiVoidResult QuaderModelingApi::apply_selected_face_material_to_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             false)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("copying face materials.");
  }
  const ModelingFaceMaterialQueryResult source =
      session.modeling().selected_face_material();
  if (!source.command.ok) {
    return invalid_api_status(session_, source.command.message);
  }
  return apply_material_to_face(object_id, face_id,
                               copied_face_material(session_, source.material));
}

QuaderApiVoidResult QuaderModelingApi::apply_selected_face_material_to_mesh(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             false)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("copying face materials.");
  }
  const ModelingFaceMaterialQueryResult source =
      session.modeling().selected_face_material();
  if (!source.command.ok) {
    return invalid_api_status(session_, source.command.message);
  }
  return apply_material_to_mesh(object_id, face_id,
                               copied_face_material(session_, source.material));
}

QuaderApiVoidResult QuaderModelingApi::apply_face_material_to_selected_faces(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             false)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("copying face materials.");
  }
  const ModelingFaceMaterialQueryResult source =
      session.modeling().material_for_face(object_id, face_id);
  if (!source.command.ok) {
    return invalid_api_status(session_, source.command.message);
  }
  return apply_material_to_selected_faces(
      copied_face_material(session_, source.material), mutations_.scene_session_id(),
      session.modeling().face_texture_settings().selection_token);
}

QuaderApiResult<QuaderFaceProjectMaterial>
QuaderModelingApi::project_material_for_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id) const {
  ModelingProjectMaterialQueryResult query =
      scene_session(session_).modeling().project_material_for_face(object_id,
                                                                  face_id);
  const QuaderApiVoidResult status = mutations_.run_modeling_result(
      std::move(query.command), "Resolve Face Material");
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (status.ok() && project != nullptr && project->manifest() != nullptr) {
    query.material = ProjectMaterialReferenceResolver::canonical_material_id(
        *project->manifest(), query.material);
  }
  return quader_api_result_from_status(
      status, QuaderFaceProjectMaterial{.id = query.material});
}

QuaderApiVoidResult QuaderModelingApi::apply_face_material_to_mesh(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             true)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project != nullptr && project->manifest() != nullptr) {
    const auto source = scene_session(session_).modeling()
                            .project_material_for_face(object_id, face_id);
    if (source.command.ok &&
        project->manifest()->is_material_deleted(source.material)) {
      const auto canonical =
          ProjectMaterialReferenceResolver::canonical_material_id(
              *project->manifest(), source.material);
      return mutations_.run_modeling_command(
          std::make_unique<ApplyMaterialToTargetHistoryCommand>(
              scene_session(session_).modeling(), object_id, face_id,
              MaterialReference::project(canonical),
              ApplyMaterialToTargetHistoryCommand::Scope::Mesh),
          "Apply Face Material to Mesh");
    }
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyFaceMaterialToMeshHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id),
      "Apply Face Material to Mesh");
}

QuaderApiVoidResult QuaderModelingApi::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(
          scene_session(session_).modeling(), material_slot),
      "Assign Material Slot");
}

QuaderFaceTextureRevision QuaderModelingApi::face_texture_settings_revision() const {
  QuaderFaceTextureRevision revision;
  revision.scene_session_id = mutations_.scene_session_id();
  if (!mutations_.available()) {
    return revision;
  }
  const SceneSession &session = scene_session(session_);
  revision.content_revision = session.modeling().content_revision();
  revision.selection_revision = session.modeling().selection_revision();
  revision.scene_revision = session.scene().revision();
  revision.asset_revision = session.assets().revision();
  revision.interaction_revision =
      session.modeling().face_texture_settings_revision() * 2u +
      (session.native_tools().has_pending_tool() ? 1u : 0u);
  return revision;
}

bool QuaderModelingApi::can_project_uv() const {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return false;
  }
  if (!mutations_.available()) {
    return false;
  }
  const SceneSession &session = scene_session(session_);
  return !session.native_tools().has_pending_tool() &&
         session.modeling().can_project_uv();
}

QuaderFaceTextureSettings QuaderModelingApi::face_texture_settings() const {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return {};
  }
  if (!mutations_.available()) {
    return {};
  }
  const auto settings = scene_session(session_).modeling().face_texture_settings();
  QuaderFaceTextureSettings result{
      .selected_face_count = settings.selected_face_count,
      .editable = settings.editable,
      .treat_as_one = settings.treat_as_one,
      .disabled_reason = settings.disabled_reason,
      .scale_x = settings.scale_x,
      .scale_y = settings.scale_y,
      .shift_x = settings.shift_x,
      .shift_y = settings.shift_y,
      .rotation_degrees = settings.rotation_degrees,
      .rotation_mixed = settings.rotation_mixed,
      .selection_token = settings.selection_token,
  };
  switch (settings.block_reason) {
  case ModelingFaceTextureBlockReason::None:
    result.block_reason = QuaderFaceTextureBlockReason::None;
    break;
  case ModelingFaceTextureBlockReason::NoSelection:
    result.block_reason = QuaderFaceTextureBlockReason::NoSelection;
    break;
  case ModelingFaceTextureBlockReason::PreviewActive:
    result.block_reason = QuaderFaceTextureBlockReason::PreviewActive;
    break;
  case ModelingFaceTextureBlockReason::TargetUnavailable:
    result.block_reason = QuaderFaceTextureBlockReason::TargetUnavailable;
    break;
  }
  if (result.selected_face_count != 0u &&
      scene_session(session_).native_tools().has_pending_tool()) {
    result.editable = false;
    result.block_reason = QuaderFaceTextureBlockReason::PreviewActive;
    result.disabled_reason =
        "Apply or cancel the active tool before editing texture settings.";
  }
  return result;
}

QuaderFaceTextureCanvas QuaderModelingApi::face_texture_canvas() const {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return {};
  }
  if (!mutations_.available()) return {};
  const auto native = scene_session(session_).modeling().face_texture_canvas();
  ProjectAssetId material_id = native.material.project_owned()
      ? native.material.project_asset() : ProjectAssetId{};
  const ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (native.material.project_owned() && project != nullptr && project->manifest() != nullptr) {
    material_id = ProjectMaterialReferenceResolver::canonical_material_id(
        *project->manifest(), material_id);
  }
  QuaderFaceTextureCanvas result{
      .selected_face_count = native.selected_face_count,
      .editable = native.editable && !scene_session(session_).native_tools().has_pending_tool(),
      .disabled_reason = native.disabled_reason,
      .selection_token = native.selection_token,
      .object_id = native.target.modeling_object_id,
      .face_id = native.target.face.valid()
          ? static_cast<QuaderElementId>(native.target.face.index) : kInvalidQuaderElementId,
      .uv_from_plane = native.chart.map.uv_from_plane,
      .uv_offset = {native.chart.map.uv_offset[0], native.chart.map.uv_offset[1]},
      .material_id = material_id,
      .embedded_material = native.material.embedded() ? native.material.embedded_asset() : kInvalidAssetId,
      .texture_extent = native.texture_extent,
      .texture_available = native.texture_available,
      .repeat_width = native.repeat_width,
      .repeat_height = native.repeat_height,
      .rotation_degrees = native.rotation_degrees,
      .gesture_active = native.gesture_active,
  };
  result.outline.reserve(native.chart.outline.size());
  for (const auto &point : native.chart.outline) {
    result.outline.push_back({static_cast<float>(point[0]), static_cast<float>(point[1])});
  }
  return result;
}

QuaderApiVoidResult QuaderModelingApi::begin_face_texture_gesture(
    const QuaderFaceTextureGestureBegin &request) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status("starting a face texture gesture.");
  }
  ModelingFaceTextureGestureKind kind;
  switch (request.kind) {
  case QuaderFaceTextureGestureKind::Translate: kind = ModelingFaceTextureGestureKind::Translate; break;
  case QuaderFaceTextureGestureKind::ScaleWidth: kind = ModelingFaceTextureGestureKind::ScaleWidth; break;
  case QuaderFaceTextureGestureKind::ScaleHeight: kind = ModelingFaceTextureGestureKind::ScaleHeight; break;
  case QuaderFaceTextureGestureKind::Rotate: kind = ModelingFaceTextureGestureKind::Rotate; break;
  default: return invalid_api_status(session_, "Unknown face texture gesture.");
  }
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().begin_face_texture_gesture({
          .kind = kind, .selection_token = request.selection_token,
          .start = request.start, .pivot = request.pivot}), "Begin Face Texture Gesture");
}

QuaderApiVoidResult QuaderModelingApi::update_face_texture_gesture(
    const QuaderFaceTextureGestureUpdate &request) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) return invalid_api_status(session_, "No scene is active.");
  auto &session = scene_session(session_);
  return mutations_.run_modeling_result(session.modeling().update_face_texture_gesture({
      .current = request.current, .precision = request.precision, .snap = request.snap,
      .grid_step = viewport_grid_world_size_from_contract(session.viewport_workspace().grid_preset()),
      .rotation_step = session.modeling().face_texture_rotation_step()}), "Update Face Texture Gesture");
}

QuaderApiVoidResult QuaderModelingApi::commit_face_texture_gesture() {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) return invalid_api_status(session_, "No scene is active.");
  return mutations_.run_modeling_command(
      std::make_unique<FaceTextureGestureHistoryCommand>(scene_session(session_).modeling()),
      "Edit Face Texture");
}

QuaderApiVoidResult QuaderModelingApi::cancel_face_texture_gesture() {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) return invalid_api_status(session_, "No scene is active.");
  return mutations_.run_modeling_result(scene_session(session_).modeling().cancel_face_texture_gesture(),
                                         "Cancel Face Texture Gesture");
}

QuaderApiVoidResult QuaderModelingApi::set_face_texture_setting(
    const QuaderFaceTextureEdit &edit) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (mutations_.available() &&
      scene_session(session_).native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("editing texture settings.");
  }
  ModelingFaceTextureField field;
  switch (edit.field) {
  case QuaderFaceTextureField::ScaleX:
    field = ModelingFaceTextureField::ScaleX;
    break;
  case QuaderFaceTextureField::ScaleY:
    field = ModelingFaceTextureField::ScaleY;
    break;
  case QuaderFaceTextureField::ShiftX:
    field = ModelingFaceTextureField::ShiftX;
    break;
  case QuaderFaceTextureField::ShiftY:
    field = ModelingFaceTextureField::ShiftY;
    break;
  case QuaderFaceTextureField::Rotate:
    field = ModelingFaceTextureField::Rotate;
    break;
  default:
    return invalid_api_status(session_, "Unknown face texture setting.");
  }
  return mutations_.run_modeling_undoable_command(
      std::make_unique<FaceTextureSettingsHistoryCommand>(
          scene_session(session_).modeling(),
          ModelingFaceTextureEdit{.field = field,
                                   .value = edit.value,
                                   .selection_token = edit.selection_token,
                                   .preserve_scale_ratio = edit.preserve_scale_ratio}),
      "Edit Texture Settings", {.merge_key = edit.merge_key});
}

QuaderApiVoidResult QuaderModelingApi::apply_face_texture_action(
    const QuaderFaceTextureActionRequest &request) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("editing face textures.");
  }
  const auto action = to_modeling_face_texture_action(request.action);
  if (!action.has_value()) {
    return invalid_api_status(session_, "Unknown face texture action.");
  }
  const auto &workspace = session.viewport_workspace();
  const auto &camera = workspace.camera(workspace.focused_pane());
  const ViewportCameraBasis basis = ViewportCameraAdapter::basis(camera);
  ModelingFaceTextureActionRequest native{
      .options = {
          .action = *action,
          .treat_as_one = session.modeling().face_texture_treat_as_one(),
          .rotation_degrees = session.modeling().face_texture_rotation_step(),
          .view_right = {basis.right.x, basis.right.y, basis.right.z},
          .view_up = {basis.up.x, basis.up.y, basis.up.z},
      },
      .selection_token = request.selection_token,
  };
  return mutations_.run_modeling_command(
      std::make_unique<FaceTextureActionHistoryCommand>(session.modeling(),
                                                       std::move(native)),
      quader_face_texture_action_label(request.action));
}

QuaderApiVoidResult QuaderModelingApi::transfer_face_texture_to_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  if (!material_mutation_target_is_available(scene_session(session_), object_id,
                                             false)) {
    return invalid_api_status(session_,
                              "The material target is outside the editable mesh context.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("transferring face textures.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<FaceTextureTransferHistoryCommand>(
          session.modeling(), object_id, face_id),
      "Transfer Face Texture");
}

bool QuaderModelingApi::face_texture_treat_as_one() const {
  return mutations_.available() &&
         scene_session(session_).modeling().face_texture_treat_as_one();
}

QuaderApiVoidResult QuaderModelingApi::set_face_texture_treat_as_one(bool enabled) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  const ModelingCommandResult result =
      scene_session(session_).modeling().set_face_texture_treat_as_one(enabled);
  return mutations_.run_runtime_result(
      modeling_memento_command_result(result),
      "Treat Selected Faces as One");
}

QuaderUvPeelRevision QuaderModelingApi::uv_peel_settings_revision() const {
  QuaderUvPeelRevision revision;
  revision.scene_session_id = mutations_.scene_session_id();
  if (!mutations_.available()) {
    return revision;
  }
  const SceneSession &session = scene_session(session_);
  revision.content_revision = session.modeling().content_revision();
  revision.selection_revision = session.modeling().selection_revision();
  revision.scene_revision = session.scene().revision();
  revision.asset_revision = session.assets().revision();
  revision.interaction_revision =
      session.modeling().uv_peel_settings_revision() * 2u +
      (session.native_tools().has_pending_tool() ? 1u : 0u);
  return revision;
}

QuaderUvPeelSettings QuaderModelingApi::uv_peel_settings() const {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return {};
  }
  if (!mutations_.available()) {
    return {};
  }
  const SceneSession &session = scene_session(session_);
  const ModelingUvPeelSettings settings = session.modeling().uv_peel_settings();
  QuaderUvPeelSettings result{
      .selected_edge_count = settings.selected_edge_count,
      .editable = settings.editable,
      .options = to_api_uv_peel_options(settings.options),
      .disabled_reason = settings.disabled_reason,
      .selection_token = settings.selection_token,
  };
  switch (settings.block_reason) {
  case ModelingUvPeelBlockReason::None:
    result.block_reason = QuaderUvPeelBlockReason::None;
    break;
  case ModelingUvPeelBlockReason::NoSelection:
    result.block_reason = QuaderUvPeelBlockReason::NoSelection;
    break;
  case ModelingUvPeelBlockReason::PreviewActive:
    result.block_reason = QuaderUvPeelBlockReason::PreviewActive;
    break;
  case ModelingUvPeelBlockReason::TargetUnavailable:
    result.block_reason = QuaderUvPeelBlockReason::TargetUnavailable;
    break;
  }
  if (result.selected_edge_count != 0u && session.native_tools().has_pending_tool() &&
      !session.modeling().uv_peel_preview_active()) {
    result.editable = false;
    result.block_reason = QuaderUvPeelBlockReason::PreviewActive;
    result.disabled_reason = "Apply or cancel the active tool before UV Peel.";
  }
  return result;
}

QuaderApiVoidResult
QuaderModelingApi::set_uv_peel_options(const QuaderUvPeelOptions &options) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  const auto native = to_modeling_uv_peel_options(options);
  if (!native.has_value()) {
    return invalid_api_status(session_, "Unknown UV Peel axis.");
  }
  return mutations_.run_runtime_result(
      modeling_memento_command_result(
          scene_session(session_).modeling().set_uv_peel_options(*native)),
      "UV Peel Settings");
}

QuaderApiVoidResult QuaderModelingApi::peel_uv(const QuaderUvPeelRequest &request) {
  if (!release_has_feature(ReleaseFeature::UvEditing)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.available()) {
    return invalid_api_status(session_, "No scene is active.");
  }
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return mutations_.modeling_content_blocked_status("UV Peel.");
  }
  const auto options = to_modeling_uv_peel_options(request.options);
  if (!options.has_value()) {
    return invalid_api_status(session_, "Unknown UV Peel axis.");
  }
  return mutations_.run_modeling_command(
      std::make_unique<UvPeelHistoryCommand>(
          session.modeling(), ModelingUvPeelRequest{
                                  .options = *options,
                                  .selection_token = request.selection_token}),
      "UV Peel");
}

QuaderApiVoidResult
QuaderModelingApi::assign_material_slot_to_face(QuaderModelingObjectRef object,
                                                 QuaderElementId face,
                                                 std::uint32_t material_slot) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
    return mutations_.stale_scene_reference_status("Modeling object");
  }
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToFaceHistoryCommand>(
          scene_session(session_).modeling(), object.modeling_object_id, face,
          material_slot),
      "Assign Face Material Slot");
}

QuaderApiVoidResult
QuaderModelingApi::cut_selected_meshes_by_hovered_face_plane() {
  if (!release_has_feature(ReleaseFeature::CutTool)) {
    return invalid_api_status(session_, release_feature_unavailable_message());
  }
  return mutations_.run_modeling_command(
      std::make_unique<CutSelectedMeshesByHoveredFacePlaneHistoryCommand>(
          scene_session(session_).modeling()),
      "Cut By Face Plane");
}

QuaderActionSet
QuaderModelingApi::available_action_set(const QuaderActionQuery &query) const {
  QuaderActionSet result = to_api_action_set(
      scene_session(session_).modeling().context_action_set(
          to_modeling_action_query(query)));
  inject_native_modeling_actions(result, query, scene_session(session_));
  return result;
}

std::vector<QuaderActionItem>
QuaderModelingApi::available_actions(const QuaderActionQuery &query) const {
  const QuaderActionSet action_set = available_action_set(query);
  std::vector<QuaderActionItem> result;
  for (const QuaderActionPanel &panel : action_set.panels) {
    result.insert(result.end(), panel.actions.begin(), panel.actions.end());
  }
  return result;
}

std::vector<QuaderInstantOperationDescriptor>
QuaderModelingApi::instant_operations() const {
  const std::vector<ModelingInstantOperationDescriptor> operations =
      scene_session(session_).modeling().instant_operations();
  std::vector<QuaderInstantOperationDescriptor> result;
  result.reserve(operations.size());
  for (const ModelingInstantOperationDescriptor &operation : operations) {
    result.push_back(to_api_instant_operation_descriptor(operation));
  }
  return result;
}

QuaderSelectionSummary QuaderModelingApi::selection_summary() const {
  return to_api_selection_summary(
      scene_session(session_).selection().summary());
}

std::vector<QuaderModelingObjectRecord> QuaderModelingApi::objects() const {
  const std::vector<ModelingObjectRecord> records =
      scene_session(session_).modeling().objects();
  std::vector<QuaderModelingObjectRecord> result;
  result.reserve(records.size());
  for (const ModelingObjectRecord &record : records) {
    result.push_back(to_api_modeling_object_record(record));
  }
  return result;
}

QuaderModelingObjectRef QuaderModelingApi::ref_from_record(
    const QuaderModelingObjectRecord &record) const {
  QuaderModelingObjectRef ref;
  ref.scene_session_id = mutations_.scene_session_id();
  ref.entity = record.entity;
  ref.entity_uuid = scene_session(session_).scene().uuid(record.entity);
  ref.editor_object_id =
      scene_session(session_).scene().editor_object_id(record.entity);
  ref.modeling_object_id = record.modeling_object_id;
  ref.mesh = record.mesh;
  if (const MaterialComponent *material =
          scene_session(session_).scene().material(record.entity)) {
    ref.material = material->material;
  }
  return ref;
}

} // namespace quader
