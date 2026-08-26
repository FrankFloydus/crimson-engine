////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_selection_box_baseline.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/scene_bounds.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class EditorHistoryService;
class IUndoableCommand;
class SceneDocument;

/**
 * Reports operation-preview projection boundaries to the project checkpoint
 * owner without exposing project-session state to the modeling package.
 */
struct ModelingOperationPreviewCheckpointHooks {
  std::function<void()> begin;
  std::function<void()> observe_before_projection;
  std::function<void()> record_after_projection;
  std::function<void(bool restored_by_cancel)> finish;
};

/// Returns one face's resolved project-owned material without mutating state.
struct ModelingProjectMaterialQueryResult final {
  ModelingCommandResult command;
  ProjectAssetId material;
};

/**
 * Stores an opaque Modeling Session Memento restore point for editor-owned
 * history commands.
 */
class ModelingSessionMemento final {
public:
  ModelingSessionMemento();
  ~ModelingSessionMemento();
  ModelingSessionMemento(const ModelingSessionMemento &) noexcept = default;
  ModelingSessionMemento &
  operator=(const ModelingSessionMemento &) noexcept = default;
  ModelingSessionMemento(ModelingSessionMemento &&) noexcept = default;
  ModelingSessionMemento &
  operator=(ModelingSessionMemento &&) noexcept = default;

  [[nodiscard]] bool valid() const;

private:
  friend class ModelingSessionService;

  struct Impl;
  explicit ModelingSessionMemento(std::shared_ptr<const Impl> impl);

  std::shared_ptr<const Impl> impl_;
};

/**
 * Coordinates Modeling Session Service responsibilities for the modeling
 * application service layer.
 */
class ModelingSessionService {
public:
  ModelingSessionService(
      SceneDocument &scene, AssetRegistry &assets,
      ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks = {});
  ~ModelingSessionService();

  ModelingSessionService(const ModelingSessionService &) = delete;
  ModelingSessionService &operator=(const ModelingSessionService &) = delete;
  ModelingSessionService(ModelingSessionService &&) noexcept;
  ModelingSessionService &operator=(ModelingSessionService &&) noexcept;

  [[nodiscard]] ModelingCommandDescriptor box_tool_command() const;
  [[nodiscard]] ModelingCommandDescriptor
  create_box_from_bounds_command() const;
  [[nodiscard]] std::vector<ModelingObjectRecord> objects() const;
  [[nodiscard]] std::optional<EntityId>
  entity_for_modeling_object(int modeling_object_id) const;
  [[nodiscard]] std::optional<int>
  modeling_object_for_entity(EntityId entity) const;
  [[nodiscard]] std::uint64_t modeling_object_map_revision() const;
  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const;
  [[nodiscard]] std::optional<EntityId>
  pick_object_entity(const ModelingPickSelectRequest &request) const;
  [[nodiscard]] std::vector<EntityId>
  box_pick_object_entities(const ModelingBoxSelectRequest &request) const;
  [[nodiscard]] std::optional<QVec3> selection_center() const;
  [[nodiscard]] std::optional<ModelingTransform3>
  selection_transform_orientation() const;
  [[nodiscard]] std::optional<SceneBounds> selection_bounds() const;
  [[nodiscard]] ModelingSelectionMode selection_mode() const;
  [[nodiscard]] ModelingSelectionSummary selection_summary() const;
  [[nodiscard]] std::span<const EntityId>
  selected_component_entities() const;
  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  [[nodiscard]] ModelingSelectionExport export_selection() const;
  [[nodiscard]] ModelingSelectedVerticesSnapshot
  selected_vertices_snapshot() const;
  [[nodiscard]] std::uint64_t selection_overlay_signature(
      ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] ViewportOverlayFrame
  selection_overlay_frame(ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] ViewportOverlayFrame volatile_overlay_frame() const;
  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  tool_preview_meshes() const;
  [[nodiscard]] bool transient_transform_preview_active() const;
  [[nodiscard]] std::uint64_t transient_transform_preview_revision() const;
  [[nodiscard]] std::uint64_t
  transformed_picking_projection_build_count() const;
  [[nodiscard]] std::uint64_t
  rectangle_picking_projection_build_count() const;
  [[nodiscard]] std::uint64_t
  rectangle_selection_universe_build_count() const;
  [[nodiscard]] std::uint64_t
  rectangle_selection_resolution_build_count() const;
  [[nodiscard]] std::uint64_t local_face_geometry_build_count() const;
  [[nodiscard]] std::uint64_t world_pick_bounds_build_count() const;
  [[nodiscard]] const void *modeling_sdk_session_identity_for_tests() const;
  [[nodiscard]] std::size_t modeling_sdk_object_count_for_tests() const;
  void set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint point);
  [[nodiscard]] ModelingSdkProjectionStats
  modeling_sdk_projection_stats_for_tests() const;
  [[nodiscard]] ModelingSessionMemento capture_history_memento() const;
  [[nodiscard]] ModelingSessionMemento capture_history_memento_without_objects(
      const ModelingSessionMemento &selection_baseline,
      std::span<const int> excluded_object_ids) const;
  [[nodiscard]] ModelingSessionMemento
  capture_active_preview_history_memento() const;
  ModelingCommandResult
  restore_history_memento(const ModelingSessionMemento &memento);
  ModelingCommandResult restore_history_memento_preserving_scene_selection(
      const ModelingSessionMemento &memento);
  void clear_transient_state_for_document_replacement();
  void reset_from_scene();
  [[nodiscard]] ModelingCommandResult
  restore_mesh_documents_from_scene(std::span<const EntityId> entities);
  void set_instance_edit_scope_provider(
      std::function<ModelingInstanceEditScope()> provider);
  void set_viewport_visibility_filter(
      std::function<bool(EntityId)> filter,
      std::function<std::uint64_t()> revision_provider,
      std::function<bool()> active_provider);

  ModelingCommandResult duplicate_selected_hierarchy_for_group_drag(
      DocumentCommandService &document_commands);
  ModelingCommandResult duplicate_selected_entity_hierarchy(
      DocumentCommandService &document_commands);
  ModelingCommandResult
  duplicate_selected_entity_hierarchy_with_independent_meshes(
      DocumentCommandService &document_commands);
  ModelingCommandResult set_selection_mode(ModelingSelectionMode mode);
  ModelingCommandResult select_all();
  ModelingCommandResult invert_selection();
  ModelingCommandResult clear_selection();
  ModelingCommandResult apply_component_selection_command(
      ModelingComponentSelectionCommand command);
  ModelingCommandResult
  select_mesh_ngons(std::span<const int> modeling_object_ids);
  [[nodiscard]] std::vector<EntityId> matching_geometry_entities(
      std::span<const EntityId> candidates,
      std::span<const EntityId> references) const;
  std::unique_ptr<IUndoableCommand> create_delete_selection_command();
  [[nodiscard]] bool has_selected_scene_aware_delete_root() const;
  ModelingCommandResult delete_component_selection();
  ModelingCommandResult copy_selection();
  ModelingCommandResult paste_selection();
  ModelingCommandResult repeat_last_action();
  ModelingCommandResult mirror_selection(ModelingMirrorAxis axis);
  ModelingCommandResult flip_selection(ModelingFlipOrientation orientation);
  ModelingCommandResult apply_active_material(std::uint32_t material_slot = 0);
  ModelingCommandResult apply_active_material(MaterialReference material);
  ModelingCommandResult apply_material_to_mesh_at_face(
      int modeling_object_id, int face_id, MaterialReference material);
  ModelingCommandResult apply_material_to_face(
      int modeling_object_id, int face_id, MaterialReference material);
  ModelingCommandResult replace_material_references_on_faces(
      std::span<const ModelingFaceMaterialReplacementTarget> targets,
      MaterialReference source, MaterialReference replacement);
  [[nodiscard]] ModelingProjectMaterialQueryResult
  project_material_for_face(int modeling_object_id, int face_id) const;
  ModelingCommandResult apply_face_material_to_mesh(int modeling_object_id,
                                                    int face_id);
  [[nodiscard]] bool modal_tool_owns_viewport() const;
  [[nodiscard]] bool transform_drag_active() const;
  [[nodiscard]] bool can_cancel_active_tool_or_preview() const;
  [[nodiscard]] bool can_cancel_active_preview_for_undo() const;
  ModelingCommandResult cancel_competing_state_for_native_tool_activation();
  [[nodiscard]] ModelingToolStatus tool_status() const;
  [[nodiscard]] ModelingLiveStatus live_status() const;
  [[nodiscard]] bool operation_preview_active() const;
  [[nodiscard]] ModelingToolSettingsTarget tool_settings_target() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const;
  [[nodiscard]] ModelingInteractionSettings interaction_settings() const;
  ModelingCommandResult
  set_interaction_settings(const ModelingInteractionSettings &settings);
  ModelingCommandResult reset_active_tool_settings();
  [[nodiscard]] ModelingActionSet
  context_action_set(const ModelingActionQuery &query = {}) const;
  [[nodiscard]] std::vector<ModelingActionItem>
  context_actions(const ModelingActionQuery &query = {}) const;
  [[nodiscard]] std::optional<ModelingActionItem>
  context_action(std::string_view action_id,
                 const ModelingActionQuery &query = {}) const;
  ModelingCommandResult execute_context_action(std::string_view action_id);
  ModelingCommandResult commit_active_operation_preview();
  ModelingCommandResult cancel_active_operation_preview();
  [[nodiscard]] std::vector<ModelingInstantOperationDescriptor>
  instant_operations() const;
  ModelingCommandResult
  execute_instant_operation(ModelingInstantOperationId operation);
  ModelingCommandResult begin_transform_preview(bool duplicate_selected);
  ModelingCommandResult update_transform_translate_preview(QVec3 delta);
  ModelingCommandResult
  update_transform_matrix_preview(const ModelingTransform3 &transform,
                                  QVec3 pivot);
  ModelingCommandResult
  update_transform_extrude_preview(QVec3 offset,
                                   float closed_edge_ledge_size = 0.0f);
  ModelingCommandResult
  update_transform_inset_preview(const ModelingTransform3 &transform,
                                 QVec3 pivot);
  ModelingCommandResult commit_transform_preview();
  ModelingCommandResult cancel_transform_preview();
  ModelingCommandResult
  record_repeatable_transform(const ModelingRepeatTransform &transform);
  ModelingTransformDragPreview
  begin_transform_drag(const ModelingTransformDragBeginRequest &request);
  ModelingTransformDragPreview begin_transform_drag(
      const ModelingTransformDragBeginRequest &request,
      DocumentCommandService &document_commands,
      EditorHistoryService &history);
  ModelingTransformDragPreview
  update_transform_drag(const ModelingTransformDragUpdateRequest &request);
  ModelingTransformDragPreview commit_transform_drag();
  ModelingTransformDragPreview
  commit_transform_drag(EditorHistoryService &history);
  ModelingCommandResult cancel_transform_drag();
  ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request);
  [[nodiscard]] ModelingSelectionBoxBaseline
  capture_box_selection_baseline() const;
  ModelingSelectionSummary box_select(const ModelingBoxSelectRequest &request);
  ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             const ModelingSelectionBoxBaseline &baseline);
  ModelingCommandResult
  update_selection_hover(const ModelingHoverRequest &request);
  [[nodiscard]] ModelingHoverSummary selection_hover_summary() const;
  [[nodiscard]] ModelingHoverSummary
  preview_selection_hover(const ModelingHoverRequest &request) const;
  ModelingCommandResult clear_selection_hover();
  ModelingCommandResult cut_selected_meshes_by_hovered_face_plane();
  ModelingCommandResult activate_viewport_tool(ModelingViewportTool tool);
  ModelingCommandResult cancel_active_modal_tool();
  ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &event);
  ModelingCommandResult
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &event);
  ModelingCommandResult
  update_insert_edge_loop_drag(const ModelingToolRayEvent &event);
  ModelingCommandResult commit_insert_edge_loop_drag();
  ModelingCommandResult update_knife_hover(const ModelingToolRayEvent &event);
  ModelingCommandResult clear_knife_hover();
  ModelingCommandResult accept_knife_point(const ModelingToolRayEvent &event);
  ModelingCommandResult commit_knife_tool();
  ModelingCommandResult update_cut_hover(const ModelingToolRayEvent &event);
  ModelingCommandResult click_cut(const ModelingToolRayEvent &event);
  ModelingCommandResult begin_cut_point_drag(int point_index);
  ModelingCommandResult begin_cut_line_drag(const ModelingToolRayEvent &event);
  ModelingCommandResult end_cut_drag();
  ModelingCommandResult set_cut_mode(ModelingCutMode mode);
  ModelingCommandResult cycle_cut_mode();
  ModelingCommandResult finalize_cut_tool();
  ModelingCommandResult update_mirror_hover(const ModelingToolRayEvent &event);
  ModelingCommandResult click_mirror(const ModelingToolRayEvent &event);
  ModelingCommandResult begin_mirror_point_drag(int point_index);
  ModelingCommandResult
  begin_mirror_line_drag(const ModelingToolRayEvent &event);
  ModelingCommandResult end_mirror_drag();
  ModelingCommandResult finalize_mirror_tool();
  ModelingCommandResult update_poly_hover(const ModelingToolRayEvent &event);
  ModelingCommandResult accept_poly_point(const ModelingToolRayEvent &event);
  ModelingCommandResult commit_poly_tool();
  ModelingCommandResult commit_poly_tool(MaterialReference material);
  ModelingCommandResult activate_box_tool();
  ModelingCreateBoxResult
  create_box_from_bounds(const ModelingCreateBoxRequest &request);
  ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &event);
  ModelingBoxPreview begin_box_footprint(const ModelingBoxToolEvent &event);
  ModelingBoxPreview update_box_footprint(const ModelingBoxToolEvent &event);
  ModelingBoxPreview begin_box_height(const ModelingBoxToolEvent &event);
  ModelingBoxPreview update_box_height(const ModelingBoxToolEvent &event);
  [[nodiscard]] ModelingBoxDragState box_drag_state() const;
  [[nodiscard]] ModelingBoxPreview box_preview(bool snap_enabled = true) const;
  ModelingCreateBoxResult commit_box(std::string name);
  ModelingCommandResult cancel_box();
  ModelingCreateBoxResult create_mesh(const ModelingCreateMeshRequest &request);
  ModelingCommandResult replace_mesh_document(
      int modeling_object_id, quader_poly::Document document);
  ModelingLinearArrayResult
  create_linear_array(const ModelingLinearArrayRequest &request);
  ModelingRadialArrayResult
  create_radial_array(const ModelingRadialArrayRequest &request);
  void project_object_selection_from_scene(bool force_object_mode = false);
  void clear_runtime_object_selection_for_scene_owned_selection();
  ModelingCommandResult
  set_object_selection(std::span<const EntityId> entities,
                       EntityId active = kInvalidEntityId);
  ModelingCommandResult
  set_component_selection(const ModelingComponentSelectionRequest &request);
  ModelingCommandResult move_selection(QVec3 delta);
  ModelingCommandResult transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot);
  ModelingCommandResult apply_resolved_vertex_positions(
      std::span<const ModelingResolvedVertexPositionBatch> batches);
  ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot);
  ModelingCommandResult
  assign_material_slot_to_face(int modeling_object_id, int face_id,
                               std::uint32_t material_slot);
  ModelingCommandResult
  set_selection_hover(const ModelingDirectHoverRequest &request);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
