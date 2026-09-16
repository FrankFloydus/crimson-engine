////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_settings_presentation_types.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/instances/instance_edit_context.hpp"
#include "scene/document/scene_content_document.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

class AssetCommandService;
class CurveToolService;
class EditorSelectionService;
class LinearArrayToolService;
class LightCreationToolService;
class ModelingSessionService;
class MeshTransformBakeService;
class NativeViewportToolCoordinator;
struct PathArraySessionDefaults;
class PathArrayToolService;
class PivotToolService;
class PrimitiveCreationToolService;
class RadialArrayToolService;
class SceneInstanceService;
class SceneSurfaceQuery;
class SceneStructuralLifecycleService;
class SnapToFloorService;
class UvPeelToolService;
class ViewportLocalVisibilityService;
class ViewportPointPlacementService;

/**
 * Stores the persisted-content revision stamp used by the project session dirty-state boundary.
 */
struct SceneContentRevisionStamp {
  std::uint64_t project_revision = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t modeling_content_revision = 0;

  [[nodiscard]] bool
  operator==(const SceneContentRevisionStamp &) const = default;
};

/**
 * Stores the result of project-owned asset mutations that depend on scene ownership.
 */
struct SceneAssetMutationResult {
  bool ok = true;
  bool changed = false;
  std::string message;
};

/**
 * Stores const project-session owner references for read-only consumers.
 */
struct SceneSessionReadView final {
  const SceneContentDocument &document;
  const SceneDocument &scene;
  const AssetRegistry &assets;
  const ModelingSessionService &modeling;
  const EditorSelectionService &selection;
};

/**
 * Owns project session behavior within the editor session ownership layer.
 */
class SceneSession {
public:
  SceneSession();
  explicit SceneSession(SceneContentDocument document);
  explicit SceneSession(
      std::shared_ptr<PathArraySessionDefaults> path_array_defaults);
  SceneSession(std::shared_ptr<PathArraySessionDefaults> path_array_defaults,
               SceneContentDocument document);
  ~SceneSession();

  SceneSession(const SceneSession &) = delete;
  SceneSession &operator=(const SceneSession &) = delete;
  SceneSession(SceneSession &&) = delete;
  SceneSession &operator=(SceneSession &&) = delete;

  SceneContentDocument &document() { return document_; }
  const SceneContentDocument &document() const { return document_; }

  SceneDocument &scene() { return document_.scene(); }
  const SceneDocument &scene() const { return document_.scene(); }

  AssetRegistry &assets() { return document_.assets(); }
  const AssetRegistry &assets() const { return document_.assets(); }

  EditorHistoryService &history() { return *history_; }
  const EditorHistoryService &history() const { return *history_; }
  void set_history_max_steps(std::size_t max_steps);
  /// Reports committed content or a rollback whose revision baseline needs syncing.
  void set_content_settled_observer(std::function<void(bool)> observer);

  DocumentCommandService &document_commands() { return *document_commands_; }
  const DocumentCommandService &document_commands() const {
    return *document_commands_;
  }
  AssetCommandService &asset_commands() { return *asset_commands_; }
  ModelingSessionService &modeling() { return *modeling_; }
  const ModelingSessionService &modeling() const { return *modeling_; }
  SceneStructuralLifecycleService &structural_lifecycle() {
    return *structural_lifecycle_;
  }
  const SceneStructuralLifecycleService &structural_lifecycle() const {
    return *structural_lifecycle_;
  }
  EditorSelectionService &selection() { return *selection_; }
  const EditorSelectionService &selection() const { return *selection_; }
  SceneInstanceService &instances() { return *instances_; }
  const SceneInstanceService &instances() const { return *instances_; }
  PivotToolService &pivot() { return *pivot_; }
  const PivotToolService &pivot() const { return *pivot_; }
  MeshTransformBakeService &mesh_transform_bake() { return *mesh_transform_bake_; }
  const MeshTransformBakeService &mesh_transform_bake() const {
    return *mesh_transform_bake_;
  }
  LinearArrayToolService &linear_array() { return *linear_array_; }
  const LinearArrayToolService &linear_array() const { return *linear_array_; }
  RadialArrayToolService &radial_array() { return *radial_array_; }
  const RadialArrayToolService &radial_array() const { return *radial_array_; }
  PathArrayToolService &path_array() { return *path_array_; }
  const PathArrayToolService &path_array() const { return *path_array_; }
  CurveToolService &curve() { return *curve_; }
  const CurveToolService &curve() const { return *curve_; }
  UvPeelToolService &uv_peel() { return *uv_peel_; }
  const UvPeelToolService &uv_peel() const { return *uv_peel_; }
  PrimitiveCreationToolService &primitive_creation() {
    return *primitive_creation_;
  }
  const PrimitiveCreationToolService &primitive_creation() const {
    return *primitive_creation_;
  }
  ViewportPointPlacementService &point_placement() {
    return *point_placement_;
  }
  const ViewportPointPlacementService &point_placement() const {
    return *point_placement_;
  }
  NativeViewportToolCoordinator &native_tools() { return *native_tools_; }
  const NativeViewportToolCoordinator &native_tools() const {
    return *native_tools_;
  }
  ViewportWorkspaceService &viewport_workspace() { return viewport_workspace_; }
  const ViewportWorkspaceService &viewport_workspace() const {
    return viewport_workspace_;
  }
  [[nodiscard]] const QuaderSettingsPresentationOrigin &
  settings_presentation_origin() const noexcept {
    return settings_presentation_origin_;
  }
  /// Invalidates captured operation dispatch when a document is replaced in place.
  [[nodiscard]] std::uint64_t document_binding_generation() const noexcept {
    return document_binding_generation_;
  }
  void record_settings_presentation_origin(std::string_view target_id,
                                           ViewportPaneId pane);
  [[nodiscard]] ViewportCameraPose persisted_viewport_camera_pose() const {
    return viewport_workspace_.persisted_perspective_camera_pose();
  }
  bool restore_persisted_viewport_camera_pose(
      const std::optional<ViewportCameraPose> &camera_pose) {
    return viewport_workspace_.restore_persisted_perspective_camera_pose(
        camera_pose);
  }
  ViewportLocalVisibilityService &viewport_local_visibility() {
    return *viewport_local_visibility_;
  }
  const ViewportLocalVisibilityService &viewport_local_visibility() const {
    return *viewport_local_visibility_;
  }
  SceneSurfaceQuery &scene_surface_query() { return *scene_surface_query_; }
  const SceneSurfaceQuery &scene_surface_query() const {
    return *scene_surface_query_;
  }
  SnapToFloorService &snap_to_floor() { return *snap_to_floor_; }
  const SnapToFloorService &snap_to_floor() const { return *snap_to_floor_; }

  [[nodiscard]] SceneSessionReadView read_view() const;
  [[nodiscard]] SceneContentRevisionStamp content_revision_stamp() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] SceneAssetMutationResult
  replace_standalone_mesh(AssetId mesh, MeshAsset replacement);
  void replace_document(SceneContentDocument document);
  bool replace_document_with_persisted_viewport_camera(
      SceneContentDocument document,
      const std::optional<ViewportCameraPose> &camera_pose);
  void set_document_replacement_finalizer(
      std::function<void(SceneSession &)> finalizer);
  void mark_clean();
  /// Keeps a previously saved scene clean after an external reference rewrite,
  /// without accepting or discarding any unrelated transient modeling preview.
  void mark_clean_after_external_reference_update(
      SceneContentRevisionStamp previous_stamp) noexcept;
  void mark_clean_at(SceneContentRevisionStamp stamp) noexcept;

private:
  /**
   * Tracks revision writes owned by one live operation projection.
   */
  struct PreviewContentCheckpoint {
    SceneContentRevisionStamp expected_stamp;
    bool began_clean = false;
    bool contaminated = false;
  };

  void rebuild_services();
  void invalidate_document_binding();
  void begin_modeling_preview_checkpoint();
  void observe_before_modeling_preview_projection();
  void record_after_modeling_preview_projection();
  void finish_modeling_preview_checkpoint(bool restored_by_cancel);
  void replace_document_impl(
      SceneContentDocument document,
      const std::optional<ViewportCameraPose> &camera_pose);

  SceneContentDocument document_;
  std::function<void(bool)> content_settled_observer_;
  SceneContentRevisionStamp clean_stamp_;
  std::optional<PreviewContentCheckpoint> preview_content_checkpoint_;
  std::size_t history_max_steps_ = EditorHistoryService::kDefaultMaxSteps;
  std::unique_ptr<EditorHistoryService> history_;
  std::unique_ptr<DocumentCommandService> document_commands_;
  std::unique_ptr<AssetCommandService> asset_commands_;
  std::unique_ptr<ModelingSessionService> modeling_;
  std::unique_ptr<EditorSelectionService> selection_;
  std::unique_ptr<SceneStructuralLifecycleService> structural_lifecycle_;
  InstanceEditContext instance_edit_context_;
  std::unique_ptr<SceneInstanceService> instances_;
  std::unique_ptr<PivotToolService> pivot_;
  std::unique_ptr<MeshTransformBakeService> mesh_transform_bake_;
  std::unique_ptr<LinearArrayToolService> linear_array_;
  std::unique_ptr<RadialArrayToolService> radial_array_;
  std::shared_ptr<PathArraySessionDefaults> path_array_defaults_;
  std::unique_ptr<PathArrayToolService> path_array_;
  std::unique_ptr<CurveToolService> curve_;
  std::unique_ptr<UvPeelToolService> uv_peel_;
  std::unique_ptr<PrimitiveCreationToolService> primitive_creation_;
  std::unique_ptr<LightCreationToolService> light_creation_;
  std::unique_ptr<NativeViewportToolCoordinator> native_tools_;
  ViewportWorkspaceService viewport_workspace_{
      ViewportWorkspaceLayoutPolicy::MultiPane};
  QuaderSettingsPresentationOrigin settings_presentation_origin_;
  std::uint64_t document_binding_generation_ = 0;
  std::unique_ptr<ViewportLocalVisibilityService> viewport_local_visibility_;
  std::unique_ptr<SceneSurfaceQuery> scene_surface_query_;
  std::unique_ptr<ViewportPointPlacementService> point_placement_;
  std::unique_ptr<SnapToFloorService> snap_to_floor_;
  std::function<void(SceneSession &)> document_replacement_finalizer_;
};

/**
 * Coordinates project session creation for editor-owned session composition.
 */
class SceneSessionService final {
public:
  SceneSessionService();

  std::unique_ptr<SceneSession> create_session() const;
  std::unique_ptr<SceneSession> create_session(SceneContentDocumentRecord record) const;
  std::unique_ptr<SceneSession> open_session(SceneContentDocument document) const;

private:
  std::shared_ptr<PathArraySessionDefaults> path_array_defaults_;
};

} // namespace quader
