////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_modeling_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "api/quader_selection_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/modeling/commit_box_preview_command.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/placement/snap_vertices_to_floor_history_command.hpp"
#include "editor/instances/project_clipboard_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <span>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
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

MaterialReference active_project_material(QuaderApiSession session) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  if (project == nullptr) {
    return {};
  }
  const std::optional<ProjectAssetId> id = project->active_material_id();
  return id.has_value() ? MaterialReference::project(*id)
                        : MaterialReference{};
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

/// Selects the narrow owner predicate used by one native modeling action.
enum class NativeModelingActionAvailability {
  SnapMeshesToFloor,
  CopyPivotFromActive,
  SnapVerticesToFloor,
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

constexpr std::array<NativeModelingActionDescriptor, 3>
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
  const bool active_is_mesh =
      active != kInvalidEntityId &&
      session.scene().modeling_object(active) != nullptr &&
      session.scene().mesh(active) != nullptr;
  return {
      .object_mode = mode == ModelingSelectionMode::Object,
      .active_mesh = active_is_mesh ? active : kInvalidEntityId,
  };
}

} // namespace

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
  const QuaderApiVoidResult selected = replace();
  if (!selected.ok()) {
    return selected;
  }
  return QuaderModelingApi(*session_, *mutations_).execute_tool(action_id);
}

QuaderPreviewHandle QuaderModelingSelection::begin_preview_after_replace(
    std::string_view action_id, const QuaderPreviewSettings &settings,
    std::string default_label) const {
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
  auto command = std::make_unique<CreateBoxFromBoundsHistoryCommand>(
      scene_session(session_).modeling(),
      ModelingCreateBoxRequest{
          .bounds = to_modeling_bounds(args.bounds),
          .name = args.name,
          .material = active_project_material(session_),
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
  MaterialReference material = active_project_material(session_);
  if (!material.valid() && default_material != kInvalidAssetId) {
    material = MaterialReference(default_material);
  }
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
  ModelingCreateMeshRequest request;
  request.document = QuaderPolygonDocumentInternalAccess::to_backend(document);
  request.name = std::move(args.name);
  request.material_slot = args.material_slot;
  request.material = active_project_material(session_);
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

QuaderApiResult<QuaderLinearArrayResult>
QuaderModelingApi::create_linear_array(const QuaderLinearArrayArgs &args) {
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
              transform_point(scene.world_transform_matrix(args.source.entity),
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
  SceneSession &session = scene_session(session_);
  const SceneDocument &scene = session.scene();
  if (args.sources.empty()) {
    return quader_api_result_from_status<QuaderRadialArrayResult>(
        invalid_api_status(session_,
                           "Radial Array needs at least one source mesh."),
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
  if (tool_id == "copy_selection") {
    return copy_selection();
  }
  if (tool_id == "paste_selection") {
    return paste_selection();
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
  const SceneSession &session = scene_session(session_);
  return copy_pivot_from_active_is_available(session);
}

QuaderApiVoidResult QuaderModelingApi::copy_pivot_from_active() {
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

bool QuaderModelingApi::can_snap_selected_meshes_to_floor() const {
  const SceneSession &session = scene_session(session_);
  return native_modeling_action_execution_is_available(session) &&
         session.snap_to_floor().can_snap_selected_meshes_to_floor();
}

QuaderApiVoidResult QuaderModelingApi::snap_selected_meshes_to_floor() {
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
  const SceneSession &session = scene_session(session_);
  return native_modeling_action_execution_is_available(session) &&
         session.snap_to_floor().can_snap_selected_vertices_to_floor();
}

QuaderApiVoidResult QuaderModelingApi::snap_selected_vertices_to_floor() {
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
  return mesh_arrangement_availability().can_align;
}

bool QuaderModelingApi::can_distribute_selected_meshes() const {
  return mesh_arrangement_availability().can_distribute;
}

QuaderApiVoidResult QuaderModelingApi::align_selected_meshes(
    QuaderTransformAxis axis, QuaderMeshAlignment alignment,
    QuaderMeshArrangementOptions options) {
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
  return mutations_.run_modeling_result(
      scene_session(session_).modeling().begin_transform_preview(
          duplicate_selected),
      "Begin Transform Preview");
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
  return mutations_.run_modeling_command(
      std::make_unique<CommitTransformPreviewHistoryCommand>(
          scene_session(session_).modeling()),
      label.empty() ? "Commit Transform Preview" : label);
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
  return result_from_transform_drag_preview(
      session, session.modeling().begin_transform_drag(
                   to_modeling_transform_drag_begin_request(request),
                   session.document_commands(), session.history()));
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

QuaderApiVoidResult QuaderModelingApi::rotate_selection(QVec3 euler_radians,
                                                        QVec3 pivot) {
  return transform_selection(rotation_transform(euler_radians), pivot);
}

QuaderApiVoidResult QuaderModelingApi::scale_selection(QVec3 scale,
                                                       QVec3 pivot) {
  return transform_selection(scale_transform(scale), pivot);
}

QuaderApiVoidResult
QuaderModelingApi::transform_selection(const QuaderTransform3 &transform,
                                       QVec3 pivot) {
  SceneSession &session = scene_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    const QMat4 display_from_scene =
        session.instances().active_edit_display_from_scene().value_or(
            scene_transform_identity_matrix());
    return mutations_.run_document_result(
        transforms.transform_selected_by_world_delta(
            session.document_commands(), session.scene(),
            world_transform_delta_from_api_transform(transform, pivot),
            "Transform Selection", display_from_scene),
        "Transform Selection");
  }

  return mutations_.run_modeling_command(
      std::make_unique<TransformSelectionHistoryCommand>(
          session.modeling(), to_modeling_transform3(transform), pivot),
      "Transform Selection");
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

  const SceneDocument &scene = session.scene();
  for (const EntityId entity : scene.selected_entities()) {
    if (!scene.is_scene_root(entity) && !scene.is_effectively_locked(entity)) {
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
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Selection");
  }
  const ModelingSessionMemento before_modeling =
      session.modeling().capture_history_memento();
  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate selected entity hierarchy owner");
    return session.modeling().duplicate_selected_entity_hierarchy(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
  }

  const CommandResult recorded_modeling =
      transaction.record_executed_before_existing(
          std::make_unique<RecordedModelingMementoCommand>(
              session.modeling(), "Duplicate Selection", before_modeling,
              session.modeling().capture_history_memento(), true));
  if (!recorded_modeling.ok) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore, "Duplicate Selection");
    }
    return mutations_.run_document_command(recorded_modeling,
                                           "Duplicate Selection");
  }

  {
    QDR_PROFILE_SCOPE("Duplicate selection commit transaction");
    return mutations_.run_document_command(transaction.commit(),
                                           "Duplicate Selection");
  }
}

QuaderApiVoidResult
QuaderModelingApi::duplicate_selected_hierarchy_for_group_drag() {
  QDR_PROFILE_FUNCTION();
  if (!mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status(
        "duplicating modeling content.");
  }
  SceneSession &session = scene_session(session_);
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Group Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Group Selection");
  }

  const ModelingSessionMemento before_modeling =
      session.modeling().capture_history_memento();

  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate group hierarchy owner");
    return session.modeling().duplicate_selected_hierarchy_for_group_drag(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore,
                                            "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore,
                                            "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }

  const CommandResult recorded_modeling =
      transaction.record_executed_before_existing(
          std::make_unique<RecordedModelingMementoCommand>(
              session.modeling(), "Duplicate Group Selection",
              before_modeling, session.modeling().capture_history_memento(),
              true));
  if (!recorded_modeling.ok) {
    const CommandResult cancel = transaction.cancel();
    const ModelingCommandResult restore =
        session.modeling().restore_history_memento(before_modeling);
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    if (!restore.ok) {
      return mutations_.run_modeling_result(restore,
                                            "Duplicate Group Selection");
    }
    return mutations_.run_document_command(recorded_modeling,
                                           "Duplicate Group Selection");
  }

  {
    QDR_PROFILE_SCOPE("Duplicate group commit transaction");
    return mutations_.run_document_command(transaction.commit(),
                                           "Duplicate Group Selection");
  }
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
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    const CommandResult canceled = session.native_tools().cancel_active();
    if (!canceled.ok) {
      return mutations_.run_runtime_result(canceled, "Exit Instance Edit");
    }
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
QuaderModelingApi::apply_active_material(std::uint32_t material_slot) {
  const MaterialReference material = active_project_material(session_);
  if (material.valid()) {
    return mutations_.run_modeling_command(
        std::make_unique<ApplyActiveMaterialHistoryCommand>(
            scene_session(session_).modeling(), material),
        "Apply Active Material");
  }
  return mutations_.run_modeling_command(
      std::make_unique<ApplyActiveMaterialHistoryCommand>(
          scene_session(session_).modeling(), material_slot),
      "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::apply_active_material_to_mesh(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  return mutations_.run_modeling_command(
      std::make_unique<ApplyActiveMaterialToTargetHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id,
          active_project_material(session_),
          ApplyActiveMaterialToTargetHistoryCommand::Scope::Mesh),
      "Apply Active Material to Mesh");
}

QuaderApiVoidResult QuaderModelingApi::apply_active_material_to_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  return mutations_.run_modeling_command(
      std::make_unique<ApplyActiveMaterialToTargetHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id,
          active_project_material(session_),
          ApplyActiveMaterialToTargetHistoryCommand::Scope::Face),
      "Apply Active Material to Face");
}

QuaderApiResult<QuaderFaceProjectMaterial>
QuaderModelingApi::project_material_for_face(
    QuaderModelingObjectId object_id, QuaderElementId face_id) const {
  ModelingProjectMaterialQueryResult query =
      scene_session(session_).modeling().project_material_for_face(object_id,
                                                                  face_id);
  const QuaderApiVoidResult status = mutations_.run_modeling_result(
      std::move(query.command), "Resolve Face Material");
  return quader_api_result_from_status(
      status, QuaderFaceProjectMaterial{.id = query.material});
}

QuaderApiVoidResult QuaderModelingApi::apply_face_material_to_mesh(
    QuaderModelingObjectId object_id, QuaderElementId face_id) {
  return mutations_.run_modeling_command(
      std::make_unique<ApplyFaceMaterialToMeshHistoryCommand>(
          scene_session(session_).modeling(), object_id, face_id),
      "Apply Face Material to Mesh");
}

QuaderApiVoidResult QuaderModelingApi::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(
          scene_session(session_).modeling(), material_slot),
      "Assign Material Slot");
}

QuaderApiVoidResult
QuaderModelingApi::assign_material_slot_to_face(QuaderModelingObjectRef object,
                                                 QuaderElementId face,
                                                 std::uint32_t material_slot) {
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
