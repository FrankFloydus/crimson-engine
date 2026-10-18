////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/modeling_face_texture_settings.hpp"
#include "editor/modeling/modeling_face_texture_canvas.hpp"
#include "editor/modeling/modeling_uv_peel.hpp"
#include "editor/modeling/operations/editor_operation.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/modeling_structural_duplicate.hpp"
#include "editor/interactions/cut_tool.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "scene/scene_bounds.hpp"

#include <quader/modeling/payloads.hpp>
#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/neutral/polygon/document_read_snapshot.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {
struct RectangleSelectionState;
}

namespace quader {

class AssetRegistry;
class EditorModeService;
struct EditorModeSnapshot;
class ModelingMeshSyncService;
class ModelingSelectionService;
class SceneDocument;
struct ViewportOverlayFrame;

/// Identifies deterministic host-publication failure boundaries used by tests.
enum class ModelingSdkProjectionFailurePoint {
  None,
  AfterSdkMutation,
  AfterAssetInstall,
  AfterEntityCreate,
  AfterSceneBinding,
  AfterSelection,
  BeforeCommit,
  SelectionScenePublication,
  CommittedAssetReplace,
  CommittedSceneNotify,
  PreviewAssetReplace,
  PreviewSceneNotify,
};

/// Reports deterministic projection work without introducing frame polling.
struct ModelingSdkProjectionStats {
  std::uint64_t mesh_compile_count = 0;
  std::uint64_t asset_publication_count = 0;
  std::uint64_t scene_publication_count = 0;
  std::uint64_t preview_publication_count = 0;
  std::uint64_t selection_summary_build_count = 0;
  std::uint64_t selection_topology_build_count = 0;
  std::uint64_t face_texture_summary_build_count = 0;
  std::uint64_t face_texture_canvas_build_count = 0;
  std::uint64_t face_texture_canvas_document_query_count = 0;
  std::uint64_t face_texture_canvas_document_copy_count = 0;
  std::uint64_t uv_peel_summary_build_count = 0;
  std::uint64_t uv_peel_preparation_count = 0;
  std::uint64_t full_scene_reset_count = 0;
  std::uint64_t scene_import_document_count = 0;
  std::uint64_t targeted_scene_sync_count = 0;
  std::uint64_t targeted_scene_sync_document_count = 0;
  std::uint64_t native_object_id_scene_scan_count = 0;
  std::uint64_t native_object_id_scene_entity_visit_count = 0;
  std::uint64_t sdk_duplicate_batch_mutation_count = 0;
  std::uint64_t sdk_duplicate_object_count = 0;
  std::uint64_t native_selection_projection_build_count = 0;
  std::uint64_t curve_preview_checkpoint_capture_count = 0;
  std::uint64_t curve_preview_checkpoint_curve_asset_count = 0;
  std::uint64_t curve_preview_shared_binding_scan_count = 0;
  std::uint64_t curve_preview_shared_binding_entity_visit_count = 0;
};

/// Describes one host-decided material slot fact to publish to SDK topology.
struct ModelingMaterialSlotAssignment {
  int modeling_object_id = kInvalidModelingObjectId;
  int face_id = 0;
  std::uint32_t material_slot = 0;
};

/// Stores one selected authored vertex in its polygon-document frame.
struct ModelingSelectedVertexSnapshot final {
  quader::modeling::VertexId vertex{};
  QVec3 local_position{};
};

/**
 * Stores the immutable host projection needed to resolve selected vertices in
 * world space without exposing an SDK document to native coordinators.
 */
struct ModelingSelectedVertexObjectSnapshot final {
  int modeling_object_id = kInvalidModelingObjectId;
  EntityId entity = kInvalidEntityId;
  QMat4 world_from_object{};
  QMat4 object_from_world{};
  std::vector<ModelingSelectedVertexSnapshot> vertices;
};

/// Reports one all-or-nothing selected-vertex projection snapshot.
struct ModelingSelectedVerticesSnapshot final {
  bool ok = false;
  std::string message;
  std::vector<ModelingSelectedVertexObjectSnapshot> objects;
};

/// Pins one authored polygon publication and its generation-bearing SDK owner.
struct ModelingPolygonDocumentReadSnapshot final {
  modeling::ObjectId object{};
  modeling::PolygonDocumentReadSnapshot document;
  modeling::PolygonSelectionSnapshot selection;
  bool preview = false;
};

/// Carries host-resolved local vertex targets back to the SDK owner.
struct ModelingResolvedVertexPositionBatch final {
  int modeling_object_id = kInvalidModelingObjectId;
  std::vector<quader::modeling::VertexPositionUpdate> positions;
};

/// Pins immutable source faces while their committed extrusion is adjustable.
class ModelingFaceExtrusionSource final {
public:
  ModelingFaceExtrusionSource() = default;
  [[nodiscard]] bool valid() const noexcept;

private:
  friend class ModelingSdkProjectionAdapter;
  struct Impl;
  explicit ModelingFaceExtrusionSource(std::shared_ptr<const Impl> impl);
  std::shared_ptr<const Impl> impl_;
};

/// Reports source capture and its operation-space placement for a tool handle.
struct ModelingFaceExtrusionSourceResult final {
  bool ok = false;
  std::string message;
  ModelingFaceExtrusionSource source;
  QVec3 center_world{};
  QVec3 direction_world{0.0f, 1.0f, 0.0f};
  /// Handle orientation falls back to world up when selected normals cancel.
  QVec3 handle_direction_world{0.0f, 1.0f, 0.0f};
  std::size_t face_count = 0;
};

/// Selects the portable mutation used to produce an interactive transform preview.
enum class ModelingSdkTransformPreviewKind {
  Transform,
  ExtrudeOffset,
  TransformExtrude,
  Inset,
};

/// Carries one object-local transform into a portable preview update.
struct ModelingSdkObjectTransformPreview {
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingTransform3 transform;
};

/// Reports a projected SDK duplicate batch and its new native records.
struct ModelingSdkDuplicateBatchResult {
  ModelingCommandResult command;
  std::vector<ModelingObjectRecord> duplicates;
};

/// Reports the SDK clipboard copy and ordered native source identities.
struct ModelingSdkClipboardCopyResult {
  ModelingCommandResult command;
  std::vector<int> source_modeling_object_ids;
};

/// Defines one object-local cutting plane for a portable batch operation.
struct ModelingSdkPlaneCutRequest {
  int modeling_object_id = kInvalidModelingObjectId;
  std::array<QVec3, 3> local_plane_points{};
  quader_editor_core::CutScope scope = quader_editor_core::CutScope::Object;
  std::vector<modeling::FaceId> faces;
  std::vector<modeling::EdgeKey> edges;
  bool all_edges = false;
  bool cap = false;
};

/// Renderer-ready host projection of an SDK-owned plane-cut preview.
struct ModelingSdkPlaneCutPreviewResult {
  ModelingCommandResult command;
  std::vector<ModelingToolPreviewMesh> kept_meshes;
  std::vector<ModelingToolPreviewMesh> discarded_meshes;
  std::vector<QVec3> inserted_points;
  /// Exact accepted plane sections and authored candidate edges in world space.
  std::vector<std::array<QVec3, 2>> intersection_segments;
  std::vector<std::array<QVec3, 2>> kept_segments;
  std::vector<std::array<QVec3, 2>> discarded_segments;
  std::uint64_t revision = 0;
  int target_count = 0;
  int split_count = 0;
  int removed_count = 0;
};

/// Renderer-ready host projection of SDK-owned reflected document copies.
struct ModelingSdkMirrorPreviewResult {
  ModelingCommandResult command;
  std::vector<ModelingToolPreviewMesh> meshes;
  int target_count = 0;
};

/// Identifies how one knife point maps into portable polygon topology.
enum class ModelingSdkKnifeTargetKind {
  ExistingVertex,
  ExistingEdge,
  InsertedVertex,
  FacePoint,
};

/// Carries one host-resolved knife point target into the portable core.
struct ModelingSdkKnifeTarget {
  ModelingSdkKnifeTargetKind kind =
      ModelingSdkKnifeTargetKind::ExistingVertex;
  int vertex_id = 0;
  int edge_a = 0;
  int edge_b = 0;
  float edge_factor = 0.5F;
  int face_id = 0;
  QVec3 position{};
};

/// Connects two points in an ordered portable knife stroke.
struct ModelingSdkKnifeStrokeSegment {
  std::uint32_t first_point = 0;
  std::uint32_t second_point = 0;
};

/// Stores an opaque SDK-session plus workspace-projection history snapshot.
class ModelingSdkProjectionSnapshot final {
public:
  ModelingSdkProjectionSnapshot();
  ~ModelingSdkProjectionSnapshot();
  ModelingSdkProjectionSnapshot(const ModelingSdkProjectionSnapshot &) noexcept =
      default;
  ModelingSdkProjectionSnapshot &
  operator=(const ModelingSdkProjectionSnapshot &) noexcept = default;
  ModelingSdkProjectionSnapshot(ModelingSdkProjectionSnapshot &&) noexcept =
      default;
  ModelingSdkProjectionSnapshot &
  operator=(ModelingSdkProjectionSnapshot &&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept;

private:
  friend class ModelingSdkProjectionAdapter;
  struct Impl;
  explicit ModelingSdkProjectionSnapshot(std::shared_ptr<const Impl> impl);
  std::shared_ptr<const Impl> impl_;
};

/// Keeps sparse positions, upgrading only welded meshes to exact documents.
class ModelingSdkTransformHistoryDelta final {
public:
  ModelingSdkTransformHistoryDelta();
  ~ModelingSdkTransformHistoryDelta();
  ModelingSdkTransformHistoryDelta(
      const ModelingSdkTransformHistoryDelta &) noexcept = default;
  ModelingSdkTransformHistoryDelta &
  operator=(const ModelingSdkTransformHistoryDelta &) noexcept = default;
  ModelingSdkTransformHistoryDelta(
      ModelingSdkTransformHistoryDelta &&) noexcept = default;
  ModelingSdkTransformHistoryDelta &
  operator=(ModelingSdkTransformHistoryDelta &&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept;

private:
  friend class ModelingSdkProjectionAdapter;
  struct Impl;
  explicit ModelingSdkTransformHistoryDelta(std::shared_ptr<const Impl> impl);
  std::shared_ptr<const Impl> impl_;
};

/**
 * Owns one workspace's portable modeling session and only its SDK/native
 * identity mappings plus transactional host-projection bookkeeping.
 */
class ModelingSdkProjectionAdapter final {
public:
  using CreationFinalizer =
      std::function<bool(const ModelingObjectRecord &created)>;

  ModelingSdkProjectionAdapter(SceneDocument &scene, AssetRegistry &assets,
                               ModelingObjectRuntimeMap &object_map,
                               ModelingMeshSyncService &mesh_sync);
  ~ModelingSdkProjectionAdapter();

  ModelingSdkProjectionAdapter(const ModelingSdkProjectionAdapter &) = delete;
  ModelingSdkProjectionAdapter &
  operator=(const ModelingSdkProjectionAdapter &) = delete;

  /// Binds the scene-owned mode context without introducing another membership owner.
  void bind_editor_mode_service(EditorModeService *service) noexcept;
  [[nodiscard]] ModelingCommandResult apply_editor_mode_context(
      const EditorModeSnapshot &context,
      std::span<const EntityId> first_admission_entities = {});

  void set_modeling_object_world_transform_resolver(
      std::function<std::optional<QMat4>(int)> resolver);
  void set_instance_edit_scope_provider(
      std::function<ModelingInstanceEditScope()> provider);
  /// Resolves displayed sampling while retaining the active occurrence scope.
  void set_modeling_object_texture_sampling_resolver(
      std::function<std::optional<QMat4>(int)> resolver);
  void reset_from_scene();
  [[nodiscard]] ModelingCommandResult
  restore_mesh_documents_from_scene(std::span<const EntityId> entities);

  [[nodiscard]] bool owns_object(int modeling_object_id) const;
  [[nodiscard]] bool owns_entity(EntityId entity) const;
  [[nodiscard]] std::size_t object_count() const noexcept;
  [[nodiscard]] bool has_selection() const;
  [[nodiscard]] const void *session_identity() const noexcept;
  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const;
  /// Returns accepted preview coordinates, rejecting suppressed participants.
  [[nodiscard]] modeling::Result<ModelingPolygonDocumentReadSnapshot>
  polygon_document_read_snapshot(int modeling_object_id,
                                 bool include_preview = true) const;
  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points,
      ModelingCutMode mode) const;
  [[nodiscard]] modeling::Result<std::vector<ModelingSdkPlaneCutRequest>>
  plane_cut_requests(const quader_editor_core::CutToolState &state) const;
  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      const quader_editor_core::CutToolState &state) const;
  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode) const;
  [[nodiscard]] const ModelingSdkPlaneCutPreviewResult &
  plane_cut_preview() const noexcept;
  [[nodiscard]] std::uint64_t plane_cut_preview_revision() const noexcept;
  void clear_plane_cut_preview();
  [[nodiscard]] ModelingSdkMirrorPreviewResult preview_mirror(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points) const;
  /** Borrows the revision-cached immutable selection read model. */
  [[nodiscard]] const ModelingSelectionProjectionSnapshot &
  selection_projection_snapshot() const;
  /// Hashes the acknowledged polygon preview identity and payload publication.
  [[nodiscard]] std::uint64_t selection_overlay_preview_signature() const noexcept;
  [[nodiscard]] std::optional<BridgePreviewHandleState>
  bridge_preview_handle_state() const;
  [[nodiscard]] std::optional<EntityId> pick_object_entity(
      const ModelingPickSelectRequest &request,
      std::optional<EntityId> competing_native = std::nullopt) const;
  [[nodiscard]] std::vector<EntityId>
  box_pick_object_entities(const ModelingBoxSelectRequest &request) const;

  [[nodiscard]] ModelingCreateBoxResult create_box(
      const ModelingCreateBoxRequest &request,
      const CreationFinalizer &finalize = {});
  /** Localizes oriented world corners beneath a matching native scene frame. */
  [[nodiscard]] ModelingCreateBoxResult create_box_from_corners(
      std::span<const QVec3> corners, std::string_view name,
      MaterialReference material = {},
      const CreationFinalizer &finalize = {});
  [[nodiscard]] ModelingCreateBoxResult create_mesh(
      const ModelingCreateMeshRequest &request,
      const CreationFinalizer &finalize = {});
  /// Publishes an already-portable authored mesh through the atomic host path.
  [[nodiscard]] ModelingCreateBoxResult create_mesh_document(
      modeling::PolygonDocument document, std::string_view name,
      MaterialReference material = {},
      const CreationFinalizer &finalize = {});
  /// Publishes an SDK-authored path as a first-class scene curve.
  [[nodiscard]] ModelingCreateBoxResult create_curve(
      modeling::CurveDocument document, std::string_view name,
      const CreationFinalizer &finalize = {});
  [[nodiscard]] std::optional<modeling::CurveDocument> curve_document(
      int modeling_object_id, bool include_preview = true) const;
  [[nodiscard]] ModelingCommandResult replace_curve_document(
      int modeling_object_id, modeling::CurveDocument document);
  [[nodiscard]] ModelingCommandResult
  begin_curve_document_preview(int modeling_object_id);
  [[nodiscard]] ModelingCommandResult
  update_curve_document_preview(modeling::CurveDocument document);
  [[nodiscard]] bool curve_document_preview_active() const;
  [[nodiscard]] int curve_document_preview_object_id() const;
  /** Localizes world points beneath their deterministic polygon frame. */
  [[nodiscard]] ModelingCreateBoxResult create_face(
      std::span<const QVec3> points, QVec3 requested_normal,
      std::string_view name, MaterialReference material = {},
      const CreationFinalizer &finalize = {});
  [[nodiscard]] ModelingSdkDuplicateBatchResult duplicate_objects(
      std::span<const int> source_modeling_object_ids,
      std::size_t copies_per_source, bool select_sources = true,
      bool share_source_mesh_assets = false);
  [[nodiscard]] ModelingSdkClipboardCopyResult copy_selection_to_clipboard();
  [[nodiscard]] ModelingSdkDuplicateBatchResult paste_clipboard(
      std::span<const ModelingObjectSceneIdentity> source_identities);
  [[nodiscard]] bool clipboard_available() const;
  [[nodiscard]] std::size_t clipboard_object_count() const;
  /// Retaining selection preserves SDK components, object flags/active target,
  /// and native scene selection through the existing publication transaction.
  [[nodiscard]] ModelingCommandResult
  replace_mesh_document(int modeling_object_id,
                        const quader_poly::Document &document,
                        bool preserve_selection = false);
  [[nodiscard]] ModelingCommandResult execute_operation(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings);
  [[nodiscard]] ModelingCommandResult plane_cut(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode, bool select_results = true);
  [[nodiscard]] ModelingCommandResult insert_edge_loop(
      int modeling_object_id, int edge_a, int edge_b, float factor);
  [[nodiscard]] ModelingCommandResult knife_stroke(
      int modeling_object_id,
      std::span<const ModelingSdkKnifeTarget> points,
      std::span<const ModelingSdkKnifeStrokeSegment> segments);
  [[nodiscard]] ModelingCommandResult begin_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings);
  [[nodiscard]] ModelingCommandResult update_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings);
  [[nodiscard]] ModelingCommandResult commit_operation_preview();
  [[nodiscard]] ModelingCommandResult cancel_operation_preview();
  [[nodiscard]] std::string_view active_preview_operation_id() const noexcept;
  [[nodiscard]] std::optional<quader_editor_core::EditorOperationId>
  active_preview_operation() const noexcept;
  [[nodiscard]] std::optional<double>
  active_edge_bevel_maximum_width() const noexcept;
  [[nodiscard]] std::optional<quader_editor_core::EdgeBevelOperationSettings>
  active_edge_bevel_settings() const noexcept;
  /// Publishes accepted scalars after successful preview geometry publication,
  /// including adapter-owned selection restarts.
  void set_edge_bevel_preview_settings_handler(
      std::function<void(const quader_editor_core::EdgeBevelOperationSettings &)>
          handler);
  [[nodiscard]] std::size_t
  active_preview_deleted_vertex_count() const noexcept;
  [[nodiscard]] ModelingCommandResult
  set_object_selection(std::span<const EntityId> entities,
                       EntityId active = kInvalidEntityId);
  [[nodiscard]] ModelingCommandResult project_object_selection_from_scene(
      std::span<const EntityId> entities,
      EntityId active = kInvalidEntityId);
  [[nodiscard]] ModelingCommandResult
  clear_object_selection_preserving_mode();
  [[nodiscard]] ModelingCommandResult
  set_selection_mode(ModelingSelectionMode mode);
  [[nodiscard]] ModelingCommandResult set_component_selection(
      const ModelingComponentSelectionRequest &request);
  [[nodiscard]] ModelingCommandResult set_component_selections(
      std::span<const ModelingComponentSelectionRequest> requests);
  [[nodiscard]] ModelingCommandResult
  select_mesh_ngons(std::span<const int> modeling_object_ids);
  [[nodiscard]] ModelingCommandResult select_all();
  [[nodiscard]] ModelingCommandResult invert_selection(
      std::span<const ModelingComponentSelectionUniverse> universes);
  [[nodiscard]] ModelingCommandResult clear_selection();
  [[nodiscard]] ModelingCommandResult
  remove_objects(std::span<const int> modeling_object_ids);
  [[nodiscard]] ModelingCommandResult apply_selection_projection_checked(
      const ModelingSelectionProjectionSnapshot &projection);
  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request,
              ModelingSelectionService &selection);
  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             ModelingSelectionService &selection);
  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection);
  [[nodiscard]] ModelingCommandResult move_selection(QVec3 delta);
  [[nodiscard]] ModelingCommandResult
  transform_selection(const ModelingTransform3 &transform, QVec3 pivot);
  [[nodiscard]] ModelingCommandResult mirror_selected_objects_across_plane(
      QVec3 origin, QVec3 normal);
  [[nodiscard]] ModelingCommandResult mirror_objects_across_planes(
      std::span<const ModelingSdkPlaneCutRequest> requests);
  [[nodiscard]] ModelingCommandResult
  flip_selected_objects(ModelingFlipOrientation orientation);
  [[nodiscard]] ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot);
  [[nodiscard]] ModelingCommandResult assign_material_slot_to_objects(
      std::span<const int> modeling_object_ids, std::uint32_t material_slot,
      bool preserve_scene_selection = false);
  [[nodiscard]] ModelingCommandResult assign_material_slot_to_face(
      int modeling_object_id, int face_id, std::uint32_t material_slot);
  [[nodiscard]] ModelingCommandResult assign_material_slots_to_faces(
      std::span<const ModelingMaterialSlotAssignment> assignments);
  [[nodiscard]] ModelingSelectedVerticesSnapshot
  selected_vertices_snapshot() const;
  [[nodiscard]] ModelingCommandResult apply_resolved_vertex_positions(
      std::span<const ModelingResolvedVertexPositionBatch> batches);

  [[nodiscard]] ModelingFaceExtrusionSourceResult
  capture_face_extrusion_source() const;
  [[nodiscard]] ModelingCommandResult validate_next_face_extrusion(
      modeling::FaceExtrudeMode mode, double amount_internal) const;
  [[nodiscard]] bool face_extrusion_source_current(
      const ModelingFaceExtrusionSource &source) const;
  [[nodiscard]] ModelingCommandResult validate_face_extrusion(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction = {}) const;
  /// Amount is already converted from displayed units to native world units.
  [[nodiscard]] ModelingCommandResult apply_face_extrusion(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction = {});
  [[nodiscard]] ModelingCommandResult begin_face_extrusion_preview(
      const ModelingFaceExtrusionSource &source,
      const ModelingFaceExtrusionSource &retained_cancellation_source = {});
  /// Pins the current result for cancel while regenerating the original faces.
  [[nodiscard]] ModelingCommandResult begin_face_extrusion_adjustment_preview(
      const ModelingFaceExtrusionSource &source);
  [[nodiscard]] ModelingCommandResult update_face_extrusion_preview(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction = {});
  [[nodiscard]] bool face_extrusion_preview_active() const;
  /// Derived meshes replace the visible source only for the active gesture.
  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  face_extrusion_preview_meshes() const;
  [[nodiscard]] std::uint64_t face_extrusion_preview_revision() const;
  [[nodiscard]] ModelingCommandResult commit_face_extrusion_preview();
  [[nodiscard]] ModelingCommandResult cancel_face_extrusion_preview();

  [[nodiscard]] ModelingCommandResult
  begin_translate_preview(bool duplicate_selected = false,
                           bool topology_stable_transform = false,
                           std::span<const ModelingStructuralEntityClone>
                               structural_duplicates = {});
  [[nodiscard]] ModelingCommandResult update_translate_preview(QVec3 delta);
  [[nodiscard]] ModelingCommandResult update_transform_preview(
      const ModelingTransform3 &transform, QVec3 pivot);
  [[nodiscard]] ModelingCommandResult update_object_transform_preview(
      std::span<const ModelingSdkObjectTransformPreview> transforms,
      ModelingSdkTransformPreviewKind kind,
      float closed_edge_ledge_size = 0.0F);
  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  workspace_transform_preview_meshes() const;
  [[nodiscard]] std::shared_ptr<const ModelingAcceptedOverlayCoordinates>
  accepted_overlay_coordinates() const;
  [[nodiscard]] std::uint64_t workspace_transform_preview_revision() const;
  [[nodiscard]] bool workspace_transform_preview_active() const;
  void apply_workspace_transform_preview_to_overlay(
      ViewportOverlayFrame &frame) const;
  [[nodiscard]] ModelingCommandResult
  update_extrude_preview(QVec3 offset, float closed_edge_ledge_size = 0.0F);
  [[nodiscard]] ModelingCommandResult
  update_inset_preview(const ModelingTransform3 &transform, QVec3 pivot);
  [[nodiscard]] ModelingCommandResult
  commit_translate_preview(bool auto_merge_vertices = false);
  [[nodiscard]] bool transform_preview_supports_sparse_history() const;
  [[nodiscard]] ModelingCommandResult cancel_translate_preview();
  [[nodiscard]] ModelingSdkTransformHistoryDelta
  take_committed_transform_history_delta();
  [[nodiscard]] ModelingCommandResult apply_transform_history_delta(
      const ModelingSdkTransformHistoryDelta &delta, bool use_after);
  [[nodiscard]] bool preview_active() const noexcept;

  [[nodiscard]] ModelingSelectionMode selection_mode() const noexcept;
  [[nodiscard]] ModelingSelectionSummary selection_summary() const;
  [[nodiscard]] bool selected_faces_have_open_perimeter() const;
  [[nodiscard]] modeling::PolygonSelectionTopologyEligibility
  selection_topology_eligibility() const;
  [[nodiscard]] std::span<const EntityId>
  selected_component_entities() const;
  [[nodiscard]] ModelingSelectionExport export_selection() const;
  [[nodiscard]] ModelingSelectedFaceTextureSnapshot
  selected_face_texture_settings(bool include_whole_meshes = false) const;
  [[nodiscard]] modeling::Result<modeling::FaceTextureCanvasChart>
  face_texture_canvas(const ModelingFaceTextureTransferTarget &target) const;
  /// Retains one document per object and returns each face result in input order.
  [[nodiscard]] std::vector<modeling::Result<modeling::FaceTextureCanvasChart>>
  face_texture_canvases(
      std::span<const ModelingFaceTextureActionTarget> targets) const;
  using FaceTextureCanvasVisitor = std::function<bool(
      std::size_t, std::size_t,
      modeling::Result<modeling::FaceTextureCanvasChart>)>;
  /// Visits canonical charts in input order until the consumer returns false.
  void visit_face_texture_canvases(
      std::span<const ModelingFaceTextureActionTarget> targets,
      const FaceTextureCanvasVisitor &visitor) const;
  [[nodiscard]] ModelingCommandResult refresh_face_texture_gesture(
      const ModelingFaceTextureTransferTarget &target,
      const modeling::FaceTextureCanvasMap &map,
      std::optional<modeling::FaceTextureTransformUpdate> scalar_update = {});
  [[nodiscard]] bool face_texture_gesture_active() const noexcept;
  void set_face_texture_gesture_selection_cancel_handler(
      std::function<ModelingCommandResult()> handler);
  [[nodiscard]] ModelingCommandResult apply_face_texture_updates(
      std::span<const ModelingFaceTextureUpdateBatch> batches);
  [[nodiscard]] ModelingCommandResult apply_face_texture_canvas_edits(
      std::span<const ModelingFaceTextureCanvasEdit> edits);
  [[nodiscard]] ModelingCommandResult apply_face_texture_action(
      std::span<const ModelingFaceTextureActionTarget> targets,
      const modeling::FaceTextureActionOptions &options);
  [[nodiscard]] ModelingCommandResult transfer_face_texture(
      const ModelingFaceTextureTransferTarget &source,
      const ModelingFaceTextureTransferTarget &target,
      std::uint32_t target_material_slot);
  [[nodiscard]] ModelingSelectedUvPeelSnapshot selected_uv_peel_edges() const;
  [[nodiscard]] ModelingCommandResult apply_uv_peel(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options);
  /// Reuses one retained SDK baseline for all edge-guided UV candidates.
  [[nodiscard]] ModelingCommandResult refresh_uv_peel_preview(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options);
  [[nodiscard]] bool uv_peel_preview_active() const noexcept;
  /// Rebuilds from canonical guides after selection suspends the UV candidate.
  void set_uv_peel_preview_selection_refresh_handler(
      std::function<ModelingCommandResult()> handler,
      std::function<void()> before_suspend = {},
      std::function<void()> after_suspend = {},
      std::function<bool()> tool_active = {});
  [[nodiscard]] std::optional<SceneBounds> selection_bounds() const;
  [[nodiscard]] std::vector<EntityId> selected_entities() const;
  [[nodiscard]] EntityId active_selected_entity() const;
  void add_selection_to_scene();
  /**
   * Extends the retained preview fence over host publication performed by the
   * owning ModelingSessionService after the adapter operation returns.
   */
  [[nodiscard]] bool acknowledge_active_preview_host_publication();

  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  /// Releases SDK-local rollback history after the native history owner has
  /// captured the completed edit. Active publication and previews defer it.
  void release_redundant_sdk_history();
  [[nodiscard]] ModelingSdkProjectionSnapshot capture_snapshot() const;
  /** Captures only promoted non-modeling children for an explicit merge history item. */
  [[nodiscard]] ModelingSdkProjectionSnapshot capture_component_merge_snapshot(
      const ModelingSdkProjectionSnapshot &before = {}) const;
  [[nodiscard]] ModelingSdkProjectionSnapshot rebase_snapshot_host_projection(
      const ModelingSdkProjectionSnapshot &snapshot) const;
  [[nodiscard]] ModelingSdkProjectionSnapshot
  capture_active_preview_base_snapshot() const;
  [[nodiscard]] ModelingCommandResult
  restore_snapshot(const ModelingSdkProjectionSnapshot &snapshot);
  /**
   * Restores SDK payloads and bindings while an enclosing Scene command owns
   * exact parent and sibling placement for every recreated entity.
   */
  [[nodiscard]] ModelingCommandResult restore_snapshot_deferred_scene_hierarchy(
      const ModelingSdkProjectionSnapshot &snapshot);
  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact_deferred_scene_hierarchy(
      const ModelingSdkProjectionSnapshot &snapshot);
  /** Restores SDK-owned state without projecting its selection into Scene. */
  [[nodiscard]] ModelingCommandResult
  restore_snapshot_preserving_scene_selection(
      const ModelingSdkProjectionSnapshot &snapshot);
  /** Restores a failed host operation without publishing a history revision. */
  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact(const ModelingSdkProjectionSnapshot &snapshot);
  /**
   * Exactly restores SDK-owned state while leaving the Scene selection to the
   * enclosing host transaction.
   */
  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact_preserving_scene_selection(
      const ModelingSdkProjectionSnapshot &snapshot);

  void set_failure_point_for_tests(ModelingSdkProjectionFailurePoint point);
  [[nodiscard]] ModelingSdkProjectionStats stats() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
