////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/commands/command.hpp>
#include <quader/modeling/input/viewport_host_input_snapshot.hpp>
#include <quader/modeling/io/qdr_document.hpp>
#include <quader/modeling/io/obj_mesh_view.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/render/render_payload.hpp>
#include <quader/modeling/session/modeling_session.hpp>
#include <quader/modeling/tools/tool_payload.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader::modeling {

struct ModelingApiContext;
struct ModelingBatchState;
struct ModelingPreviewState;

class ModelingApi;
class ModelingBatch;
class BatchCreatedMeshStep;
class CheckedModelingApi;
class CheckedIoApi;
class CheckedOperationsApi;
class CheckedToolsApi;
class MeshHandle;
class MeshCollection;
class MeshVertices;
class MeshEdges;
class MeshFaces;
class MeshTransform;
class MeshMaterials;
class MeshPayloads;
class MeshValidation;
class Selection;
class ObjectSelection;
class VertexSelection;
class EdgeSelection;
class FaceSelection;
class SelectionApi;
class OperationsApi;
class PreviewHandle;
class PreviewApi;
class ToolsApi;
class BoxTool;
class KnifeTool;
class CutTool;
class MeshSyncApi;
class IoApi;
class CommandApi;
class ProfilingApi;
class RenderApi;
class SelectTool;
class TransformTool;
class InsertEdgeLoopTool;
class PolyTool;
class MirrorTool;
class PivotTool;

/**
 * Selects how direct public API calls report structured failures.
 */
enum class ErrorPolicy {
  ThrowException,
  StoreLastError,
};

/**
 * Configures a public modeling API instance.
 */
struct ModelingApiOptions {
  ErrorPolicy error_policy = ErrorPolicy::ThrowException;
};

/**
 * Exception raised by direct public API calls when configured to throw.
 */
class ModelingException : public std::runtime_error {
public:
  explicit ModelingException(Error error);
  [[nodiscard]] const Error &error() const noexcept;

private:
  Error error_;
};

/**
 * Tracks content, selection, scene, and asset revisions visible to API users.
 */
struct RevisionStamp {
  std::uint64_t content = 0;
  std::uint64_t selection = 0;
  std::uint64_t scene = 0;
  std::uint64_t assets = 0;
};

/**
 * Reports the status, revisions, dirty flags, and deltas for one mutation.
 */
struct OperationReceipt {
  bool success = true;
  bool changed = false;
  RevisionStamp revisions;
  DirtyFlags dirty;
  ElementDelta created;
  ElementDelta deleted;
  ElementDelta affected;
  ElementDelta modified;
  SelectionRemap selection_remap;
  Error error;
  std::vector<Diagnostic> diagnostics;
};

/**
 * Summarizes one mesh object without exposing internal session storage.
 */
struct MeshSummary {
  ObjectId id{};
  std::string name;
  bool selected = false;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Defines box creation bounds, name, and initial material.
 */
struct BoxOptions {
  std::string name = "Mesh";
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
};

/**
 * Configures the host-neutral box tool before commit.
 */
struct BoxToolOptions {
  std::string name = "Mesh";
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
};

/**
 * Configures bridge operations.
 */
struct BridgeOptions {
  int steps = 1;
};

/**
 * Configures edge bevel operations.
 */
struct EdgeBevelOptions {
  float width = 1.0F;
  float profile = 0.5F;
  int segments = 1;
};

/**
 * Configures face thickening and outer hull operations.
 */
struct ThickenOptions {
  float thickness = 0.25F;
  bool from_center = false;
};

/**
 * Configures quad slicing density.
 */
struct SliceQuadOptions {
  int x_slices = 1;
  int y_slices = 1;
};

/**
 * Configures distance-based vertex merging.
 */
struct MergeByDistanceOptions {
  float tolerance = 0.125F;
};

/**
 * Configures vertex bevel operations.
 */
struct BevelVerticesOptions {
  std::optional<float> distance;
};

/**
 * Configures object duplication placement.
 */
struct DuplicateOptions {
  Vec3 offset{};
  bool use_grid_default = false;
};

/**
 * Configures object clipboard paste placement.
 */
struct PasteOptions {
  Vec3 offset{1.0F, 0.0F, 0.0F};
};

/**
 * Configures affine transform operations.
 */
struct TransformOptions {
  Transform3 transform;
  Vec3 pivot{};
};

/**
 * Configures Euler rotation operations around a pivot.
 */
struct RotateOptions {
  Vec3 radians{};
  Vec3 pivot{};
};

/**
 * Configures scale operations around a pivot.
 */
struct ScaleOptions {
  Vec3 scale{1.0F, 1.0F, 1.0F};
  Vec3 pivot{};
};

/**
 * Configures extrusion offset and closed-edge ledge generation.
 */
struct ExtrudeOptions {
  Vec3 offset{};
  float closed_edge_ledge_size = 0.0F;
};

/**
 * Configures element inset transforms.
 */
struct InsetOptions {
  Transform3 transform;
  Vec3 pivot{};
};

/**
 * Selects which side of a cut operation remains.
 */
enum class CutKeepMode {
  DiscardLeft,
  DiscardRight,
  KeepBoth,
};

/**
 * Configures the host-neutral knife tool.
 */
struct KnifeToolOptions {};

/**
 * Configures the host-neutral cut tool.
 */
struct CutToolOptions {
  CutKeepMode keep = CutKeepMode::KeepBoth;
};

/**
 * Defines a plane cut from three points and a keep mode.
 */
struct PlaneCutOptions {
  Vec3 a{};
  Vec3 b{};
  Vec3 c{};
  CutKeepMode keep = CutKeepMode::KeepBoth;
};

/**
 * Defines one knife segment using resolved topology targets.
 */
struct KnifeSegmentOptions {
  KnifeTarget from;
  KnifeTarget to;
};

/**
 * Defines a knife stroke as resolved topology targets and point links.
 */
struct KnifeStrokeOptions {
  std::vector<KnifeTarget> points;
  std::vector<KnifeStrokeSegment> segments;
};

/**
 * Configures edge-loop insertion from a seed edge.
 */
struct InsertEdgeLoopOptions {
  EdgeKey edge{};
  float t = 0.5F;
};

/**
 * Configures selected-face normal recalculation.
 */
struct RecalculateNormalsOptions {
  bool outside = true;
};

/**
 * Identifies a principal modeling axis.
 */
enum class Axis {
  X,
  Y,
  Z,
};

/**
 * Configures the edge-loop insertion tool.
 */
struct InsertEdgeLoopToolOptions {
  InsertEdgeLoopOptions loop;
};

/**
 * Configures the host-neutral polygon drawing tool.
 */
struct PolyToolOptions {};

/**
 * Configures the mirror tool axis.
 */
struct MirrorToolOptions {
  Axis axis = Axis::X;
};

/**
 * Configures the native-editor-owned pivot tool placeholder.
 */
struct PivotToolOptions {};

/**
 * Configures explicit mesh compilation.
 */
struct MeshCompileOptions {
  SnapshotLifetime lifetime = SnapshotLifetime::Owned;
};

/**
 * Reports whether an explicit mesh compilation reused previous content.
 */
struct MeshCompileResult {
  MeshPayload mesh;
  bool changed = true;
  bool reused_previous = false;
};

/**
 * Configures payload lifetime and revision-gated materialization.
 */
struct PayloadRequest {
  SnapshotLifetime lifetime = SnapshotLifetime::Owned;
  bool only_if_changed = false;
  std::uint64_t previous_revision = 0;
};

/**
 * Carries one object mesh delta for renderer or host sync.
 */
struct ObjectMeshDelta {
  ObjectId object;
  std::string name;
  MeshPayload mesh;
  DirtyFlags dirty;
  bool deleted = false;
};

/**
 * Carries all mesh deltas after a content revision.
 */
struct MeshSyncSnapshot {
  std::uint64_t content_revision = 0;
  std::vector<ObjectMeshDelta> objects;
};

/**
 * Carries string-dispatched command settings.
 */
struct OperationSettings {
  Vec3 delta{};
  TransformOptions transform;
  RotateOptions rotate;
  ScaleOptions scale;
  ExtrudeOptions extrude;
  InsetOptions inset;
  Axis axis = Axis::X;
  DuplicateOptions duplicate;
  PasteOptions paste;
  BridgeOptions bridge;
  EdgeBevelOptions edge_bevel;
  ThickenOptions thicken;
  SliceQuadOptions slice_quad;
  MergeByDistanceOptions merge_by_distance;
  BevelVerticesOptions bevel_vertices;
  std::uint32_t material_slot = 0;
  PlaneCutOptions plane_cut;
  KnifeSegmentOptions knife_segment;
  KnifeStrokeOptions knife_stroke;
  InsertEdgeLoopOptions insert_edge_loop;
};

/**
 * Describes one host-neutral SDK tool.
 */
struct ToolDescriptor {
  std::string id;
  std::string label;
};

/**
 * Reports active tool and preview state.
 */
struct ToolStatus {
  std::string active_tool;
  bool preview_active = false;
};

/**
 * Captures one pointer event in SDK value types.
 */
struct PointerEvent {
  Vec2 position;
  Ray ray;
};

/**
 * Requests host-projected picking from a ray.
 */
struct PickRequest {
  Ray ray;
  std::optional<SelectionUnion> resolved;
};

/**
 * Reports the result of a host-projected pick.
 */
struct PickResult {
  bool hit = false;
  SelectionUnion selection;
};

/**
 * Combines picking and selection edit policy.
 */
struct PickSelectRequest {
  PickRequest pick;
  SelectionEdit edit = SelectionEdit::Replace;
};

/**
 * Captures selection state before box selection previews.
 */
struct BoxSelectionBaseline {
  SelectionSummary summary;
};

/**
 * Describes a host-projected box selection request.
 */
struct BoxSelectionRequest {
  Vec2 min{};
  Vec2 max{};
  SelectionEdit edit = SelectionEdit::Replace;
  std::optional<SelectionUnion> resolved;
};

/**
 * Requests hover target resolution from a ray.
 */
struct HoverRequest {
  Ray ray;
  std::optional<SelectionUnion> resolved;
};

/**
 * Describes a hover target as a typed selection.
 */
struct HoverTarget {
  SelectionUnion selection;
};

/**
 * Reports hover target resolution.
 */
struct HoverResult {
  bool hit = false;
  HoverTarget target;
};

/**
 * Reports one committed batch and its per-step receipts.
 */
struct BatchResult {
  OperationReceipt receipt;
  std::vector<OperationReceipt> step_receipts;
  std::vector<std::pair<std::string, ObjectId>> meshes;

  [[nodiscard]] bool success() const noexcept { return receipt.success; }
  [[nodiscard]] MeshHandle mesh(std::string_view alias) const;

private:
  friend class ModelingBatch;
  std::shared_ptr<ModelingApiContext> context_;
};

using PreviewId = Id<struct PreviewTag>;
using ViewportId = Id<struct ViewportTag>;

/**
 * Provides explicit-result access to root modeling operations.
 */
class CheckedModelingApi {
public:
  CheckedModelingApi() = default;
  [[nodiscard]] Result<MeshHandle> create_box(BoxOptions options = {});
  [[nodiscard]] Result<MeshHandle>
  add_document(PolygonDocument document, std::string name = "Mesh");
  [[nodiscard]] CheckedIoApi io();
  [[nodiscard]] CheckedToolsApi tools();
  [[nodiscard]] CheckedOperationsApi operations();

private:
  friend class ModelingApi;
  friend class CheckedIoApi;
  friend class CheckedToolsApi;
  explicit CheckedModelingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Root public modeling API facade for portable C++ consumers.
 */
class ModelingApi {
public:
  ModelingApi();
  [[nodiscard]] static ModelingApi create(ModelingApiOptions options = {});

  [[nodiscard]] CheckedModelingApi checked();
  [[nodiscard]] const Error *last_error() const;
  void clear_error();

  [[nodiscard]] MeshHandle create_box(BoxOptions options = {});
  [[nodiscard]] MeshHandle add_document(PolygonDocument document,
                                        std::string name = "Mesh");

  [[nodiscard]] BoxTool activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] CutTool activate_cut_tool(CutToolOptions options = {});

  [[nodiscard]] MeshHandle mesh(ObjectId object);
  [[nodiscard]] MeshCollection meshes();
  [[nodiscard]] std::vector<MeshSummary> mesh_summaries() const;
  [[nodiscard]] std::vector<MeshHandle> selected_meshes() const;

  [[nodiscard]] SelectionApi selection();
  [[nodiscard]] OperationsApi operations();
  [[nodiscard]] ToolsApi tools();
  [[nodiscard]] PreviewApi preview();
  [[nodiscard]] MeshSyncApi mesh_sync();
  [[nodiscard]] IoApi io();
  [[nodiscard]] CommandApi commands();
  [[nodiscard]] ProfilingApi profiling();
  [[nodiscard]] RenderApi render();
  [[nodiscard]] ModelingBatch batch(std::string label = {});

  [[nodiscard]] OperationReceipt undo();
  [[nodiscard]] OperationReceipt redo();
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] RevisionStamp revisions() const;

private:
  friend class MeshHandle;
  friend class CheckedModelingApi;
  friend class BoxTool;
  friend class IoApi;
  friend class ModelingBatch;
  friend class RenderApi;
  explicit ModelingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Builder step returned after scheduling a mesh creation operation.
 */
class BatchCreatedMeshStep {
public:
  BatchCreatedMeshStep() = default;
  [[nodiscard]] ModelingBatch as(std::string alias);

private:
  friend class ModelingBatch;
  BatchCreatedMeshStep(std::shared_ptr<ModelingBatchState> state,
                       std::size_t step_index);
  std::shared_ptr<ModelingBatchState> state_;
  std::size_t step_index_ = 0;
};

/**
 * Collects public modeling intent and commits it as one batch operation.
 */
class ModelingBatch {
public:
  ModelingBatch() = default;
  [[nodiscard]] BatchCreatedMeshStep create_box(BoxOptions options = {});
  [[nodiscard]] ModelingBatch &
  with_mesh(std::string alias, std::function<void(MeshHandle)> operation);
  [[nodiscard]] BatchResult commit();
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class BatchCreatedMeshStep;
  ModelingBatch(std::shared_ptr<ModelingApiContext> context, std::string label);
  explicit ModelingBatch(std::shared_ptr<ModelingBatchState> state);
  std::shared_ptr<ModelingBatchState> state_;
};

/**
 * Provides object-scope queries and mutations for all session meshes.
 */
class MeshCollection {
public:
  MeshCollection() = default;
  [[nodiscard]] std::vector<MeshHandle> all() const;
  [[nodiscard]] ObjectSelection selected() const;
  [[nodiscard]] ObjectSelection only(std::span<const ObjectId> objects) const;
  [[nodiscard]] MeshHandle first_selected() const;
  [[nodiscard]] OperationReceipt delete_selected();
  [[nodiscard]] MeshHandle duplicate_selected(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_selected();

private:
  friend class ModelingApi;
  friend class MeshHandle;
  friend class OperationsApi;
  friend class SelectionApi;
  friend class TransformTool;
  explicit MeshCollection(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Lightweight stale-checkable handle to one SDK mesh object.
 */
class MeshHandle {
public:
  MeshHandle() = default;
  [[nodiscard]] ObjectId id() const;
  [[nodiscard]] bool valid() const;
  [[nodiscard]] MeshSummary summary() const;
  [[nodiscard]] RevisionStamp revisions() const;

  [[nodiscard]] OperationReceipt rename(std::string name);
  [[nodiscard]] OperationReceipt destroy();
  [[nodiscard]] MeshHandle duplicate(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_with(std::span<const MeshHandle> meshes);

  [[nodiscard]] ObjectSelection select();
  [[nodiscard]] VertexSelection select_all_vertices();
  [[nodiscard]] EdgeSelection select_all_edges();
  [[nodiscard]] FaceSelection select_all_faces();

  [[nodiscard]] MeshVertices vertices();
  [[nodiscard]] MeshEdges edges();
  [[nodiscard]] MeshFaces faces();
  [[nodiscard]] MeshTransform transform();
  [[nodiscard]] MeshMaterials materials();
  [[nodiscard]] MeshPayloads payloads() const;
  [[nodiscard]] MeshValidation validation() const;

private:
  friend class ModelingApi;
  friend class CheckedModelingApi;
  friend class MeshCollection;
  friend class MeshVertices;
  friend class MeshEdges;
  friend class MeshFaces;
  friend class MeshTransform;
  friend class MeshMaterials;
  friend class MeshPayloads;
  friend class MeshValidation;
  friend class MeshSyncApi;
  friend class IoApi;
  friend class FaceSelection;
  friend class ObjectSelection;
  friend class OperationsApi;
  friend struct BatchResult;
  friend class ModelingBatch;
  friend class RenderApi;
  explicit MeshHandle(std::shared_ptr<ModelingApiContext> context,
                      ObjectId object);
  std::shared_ptr<ModelingApiContext> context_;
  ObjectId object_{};
};

/**
 * Provides vertex selection scopes for one mesh.
 */
class MeshVertices {
public:
  MeshVertices() = default;
  [[nodiscard]] VertexSelection all() const;
  [[nodiscard]] VertexSelection selected() const;
  [[nodiscard]] VertexSelection only(std::span<const VertexId> vertices) const;
  [[nodiscard]] VertexSelection active() const;

private:
  friend class MeshHandle;
  explicit MeshVertices(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides edge selection scopes for one mesh.
 */
class MeshEdges {
public:
  MeshEdges() = default;
  [[nodiscard]] EdgeSelection all() const;
  [[nodiscard]] EdgeSelection selected() const;
  [[nodiscard]] EdgeSelection only(std::span<const EdgeKey> edges) const;
  [[nodiscard]] EdgeSelection boundary() const;
  [[nodiscard]] EdgeSelection hard() const;
  [[nodiscard]] EdgeSelection soft() const;

private:
  friend class MeshHandle;
  explicit MeshEdges(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides face selection scopes for one mesh.
 */
class MeshFaces {
public:
  MeshFaces() = default;
  [[nodiscard]] FaceSelection all() const;
  [[nodiscard]] FaceSelection selected() const;
  [[nodiscard]] FaceSelection only(std::span<const FaceId> faces) const;
  [[nodiscard]] FaceSelection by_material_slot(std::uint32_t slot) const;
  [[nodiscard]] FaceSelection by_normal(Vec3 direction,
                                        float tolerance = 0.01F) const;

private:
  friend class MeshHandle;
  explicit MeshFaces(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides object-level transform operations for one mesh.
 */
class MeshTransform {
public:
  MeshTransform() = default;
  [[nodiscard]] OperationReceipt translate(Vec3 delta);
  [[nodiscard]] OperationReceipt rotate(RotateOptions options);
  [[nodiscard]] OperationReceipt scale(ScaleOptions options);
  [[nodiscard]] OperationReceipt apply(TransformOptions options);
  [[nodiscard]] PreviewHandle preview(TransformOptions options = {});

private:
  friend class MeshHandle;
  explicit MeshTransform(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides material assignment operations for one mesh.
 */
class MeshMaterials {
public:
  MeshMaterials() = default;
  [[nodiscard]] OperationReceipt assign(MaterialId material);
  [[nodiscard]] OperationReceipt assign_slot(std::uint32_t slot);
  [[nodiscard]] OperationReceipt assign_slot(FaceSelection faces,
                                             std::uint32_t slot);

private:
  friend class MeshHandle;
  explicit MeshMaterials(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides explicit authored and compiled mesh payload materialization.
 */
class MeshPayloads {
public:
  MeshPayloads() = default;
  [[nodiscard]] AuthoredPolygonPayload authored_polygon() const;
  [[nodiscard]] MeshPayload compile_mesh(MeshCompileOptions options = {}) const;
  [[nodiscard]] MeshCompileResult
  compile_mesh_if_changed(std::uint64_t previous_revision,
                          MeshCompileOptions options = {}) const;

private:
  friend class MeshHandle;
  explicit MeshPayloads(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Provides validation for one mesh document.
 */
class MeshValidation {
public:
  MeshValidation() = default;
  [[nodiscard]] OperationReceipt validate() const;

private:
  friend class MeshHandle;
  explicit MeshValidation(MeshHandle mesh);
  MeshHandle mesh_;
};

/**
 * Shared immutable selection contract for typed selections.
 */
class Selection {
public:
  Selection() = default;
  Selection(std::shared_ptr<ModelingApiContext> context,
            SelectionUnion selection);
  [[nodiscard]] SelectionKind kind() const;
  [[nodiscard]] ObjectId object() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] bool is_object_selection() const;
  [[nodiscard]] bool is_vertex_selection() const;
  [[nodiscard]] bool is_edge_selection() const;
  [[nodiscard]] bool is_face_selection() const;
  [[nodiscard]] OperationReceipt
  apply(SelectionEdit edit = SelectionEdit::Replace) const;
  [[nodiscard]] OperationReceipt replace() const;
  [[nodiscard]] OperationReceipt add() const;
  [[nodiscard]] OperationReceipt remove() const;
  [[nodiscard]] OperationReceipt toggle() const;
  [[nodiscard]] SelectionUnion to_union() const;

protected:
  std::shared_ptr<ModelingApiContext> context_;
  SelectionUnion selection_;
};

/**
 * Object selection value with object-level operations.
 */
class ObjectSelection final : public Selection {
public:
  ObjectSelection() = default;
  using Selection::Selection;
  [[nodiscard]] std::span<const ObjectId> objects() const;
  [[nodiscard]] OperationReceipt delete_objects() const;
  [[nodiscard]] MeshHandle duplicate(DuplicateOptions options = {}) const;
  [[nodiscard]] OperationReceipt combine() const;

private:
  friend class MeshCollection;
  friend class MeshHandle;
  friend class SelectionApi;
};

/**
 * Vertex selection value with vertex operations.
 */
class VertexSelection final : public Selection {
public:
  VertexSelection() = default;
  using Selection::Selection;
  [[nodiscard]] std::span<const VertexId> vertices() const;
  [[nodiscard]] OperationReceipt snap_to_active() const;
  [[nodiscard]] OperationReceipt merge_to_active() const;
  [[nodiscard]] OperationReceipt merge_to_center() const;
  [[nodiscard]] PreviewHandle
  merge_by_distance(MergeByDistanceOptions options = {}) const;
  [[nodiscard]] OperationReceipt
  remove_doubles(MergeByDistanceOptions options = {}) const;
  [[nodiscard]] OperationReceipt bevel(BevelVerticesOptions options = {}) const;
  [[nodiscard]] OperationReceipt connect() const;
  [[nodiscard]] OperationReceipt dissolve() const;
  [[nodiscard]] OperationReceipt radial_align() const;

private:
  friend class MeshVertices;
  friend class SelectionApi;
};

/**
 * Edge selection value with edge operations.
 */
class EdgeSelection final : public Selection {
public:
  EdgeSelection() = default;
  using Selection::Selection;
  [[nodiscard]] std::span<const EdgeKey> edges() const;
  [[nodiscard]] OperationReceipt snap_to_active() const;
  [[nodiscard]] OperationReceipt connect() const;
  [[nodiscard]] OperationReceipt split() const;
  [[nodiscard]] OperationReceipt harden_normals() const;
  [[nodiscard]] OperationReceipt soften_normals() const;
  [[nodiscard]] PreviewHandle bevel(EdgeBevelOptions options = {}) const;
  [[nodiscard]] OperationReceipt bridge(BridgeOptions options = {}) const;
  [[nodiscard]] PreviewHandle
  interpolated_bridge(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt bridge_pairs(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt
  bridge_boundaries(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt dissolve() const;
  [[nodiscard]] OperationReceipt merge() const;
  [[nodiscard]] OperationReceipt collapse() const;
  [[nodiscard]] OperationReceipt fill_hole() const;
  [[nodiscard]] OperationReceipt radial_align() const;
  [[nodiscard]] OperationReceipt insert_loop(InsertEdgeLoopOptions options) const;

private:
  friend class MeshEdges;
  friend class SelectionApi;
};

/**
 * Face selection value with face operations.
 */
class FaceSelection final : public Selection {
public:
  FaceSelection() = default;
  using Selection::Selection;
  [[nodiscard]] std::span<const FaceId> faces() const;
  [[nodiscard]] OperationReceipt bridge(BridgeOptions options = {}) const;
  [[nodiscard]] PreviewHandle
  interpolated_bridge(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt flip_normals() const;
  [[nodiscard]] OperationReceipt
  recalculate_normals(RecalculateNormalsOptions options = {}) const;
  [[nodiscard]] OperationReceipt shade_smooth() const;
  [[nodiscard]] OperationReceipt shade_flat() const;
  [[nodiscard]] OperationReceipt combine() const;
  [[nodiscard]] OperationReceipt collapse() const;
  [[nodiscard]] OperationReceipt radial_align() const;
  [[nodiscard]] MeshHandle extract() const;
  [[nodiscard]] OperationReceipt detach() const;
  [[nodiscard]] PreviewHandle slice_quads(SliceQuadOptions options = {}) const;
  [[nodiscard]] PreviewHandle thicken(ThickenOptions options = {}) const;
  [[nodiscard]] OperationReceipt extrude(ExtrudeOptions options = {}) const;
  [[nodiscard]] OperationReceipt inset(InsetOptions options = {}) const;
  [[nodiscard]] OperationReceipt delete_faces() const;

private:
  friend class MeshFaces;
  friend class SelectionApi;
};

/**
 * Handle to one pending public modeling preview operation.
 */
class PreviewHandle {
public:
  PreviewHandle() = default;
  [[nodiscard]] PreviewId id() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] PreviewTransactionSummary summary() const;
  [[nodiscard]] OperationReceipt update(BridgeOptions options);
  [[nodiscard]] OperationReceipt update(EdgeBevelOptions options);
  [[nodiscard]] OperationReceipt update(ThickenOptions options);
  [[nodiscard]] OperationReceipt update(SliceQuadOptions options);
  [[nodiscard]] OperationReceipt update(MergeByDistanceOptions options);
  [[nodiscard]] OperationReceipt update(TransformOptions options);
  [[nodiscard]] ToolPreviewPayload payload(PayloadRequest request = {}) const;
  [[nodiscard]] MeshPayload mesh_payload(PayloadRequest request = {}) const;
  [[nodiscard]] SemanticOverlayPayload
  overlay_payload(PayloadRequest request = {}) const;
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class VertexSelection;
  friend class EdgeSelection;
  friend class FaceSelection;
  friend class MeshTransform;
  explicit PreviewHandle(std::shared_ptr<ModelingPreviewState> state);
  std::shared_ptr<ModelingPreviewState> state_;
};

/**
 * Root selection facade for session selection and host-projected targets.
 */
class SelectionApi {
public:
  SelectionApi() = default;
  [[nodiscard]] SelectionEdit default_edit() const;
  [[nodiscard]] OperationReceipt set_default_edit(SelectionEdit edit);
  [[nodiscard]] OperationReceipt clear();
  [[nodiscard]] OperationReceipt select_all();
  [[nodiscard]] OperationReceipt invert();
  [[nodiscard]] ObjectSelection
  objects(std::span<const ObjectId> objects,
          SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] VertexSelection
  vertices(ObjectId object, std::span<const VertexId> vertices,
           SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] EdgeSelection edges(ObjectId object,
                                    std::span<const EdgeKey> edges,
                                    SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] FaceSelection faces(ObjectId object,
                                    std::span<const FaceId> faces,
                                    SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] PickResult pick(PickRequest request);
  [[nodiscard]] OperationReceipt pick_select(PickSelectRequest request);
  [[nodiscard]] BoxSelectionBaseline capture_box_baseline();
  [[nodiscard]] OperationReceipt preview_box(BoxSelectionRequest request);
  [[nodiscard]] OperationReceipt commit_box(BoxSelectionRequest request);
  [[nodiscard]] HoverResult preview_hover(HoverRequest request);
  [[nodiscard]] OperationReceipt apply_hover(HoverTarget target);
  [[nodiscard]] OperationReceipt clear_hover();
  [[nodiscard]] SelectionSummary summary() const;

private:
  friend class ModelingApi;
  explicit SelectionApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * String-dispatch and current-selection operation facade.
 */
class OperationsApi {
public:
  OperationsApi() = default;
  [[nodiscard]] std::vector<CommandDescriptor> catalog() const;
  [[nodiscard]] CommandDescriptor describe(std::string_view id) const;
  [[nodiscard]] OperationReceipt execute(std::string_view id,
                                         OperationSettings settings = {});
  [[nodiscard]] OperationReceipt delete_selection();
  [[nodiscard]] OperationReceipt copy_selection();
  [[nodiscard]] OperationReceipt paste_selection(PasteOptions options = {});
  [[nodiscard]] OperationReceipt repeat_last_action();
  [[nodiscard]] MeshHandle duplicate_meshes(DuplicateOptions options = {});
  [[nodiscard]] OperationReceipt combine_meshes();
  [[nodiscard]] OperationReceipt translate_selection(Vec3 delta);
  [[nodiscard]] OperationReceipt transform_selection(TransformOptions options);
  [[nodiscard]] OperationReceipt rotate_selection(RotateOptions options);
  [[nodiscard]] OperationReceipt scale_selection(ScaleOptions options);
  [[nodiscard]] OperationReceipt extrude(ExtrudeOptions options);
  [[nodiscard]] OperationReceipt inset(InsetOptions options);
  [[nodiscard]] OperationReceipt mirror_selection(Axis axis);
  [[nodiscard]] OperationReceipt flip_horizontal();
  [[nodiscard]] OperationReceipt flip_vertical();
  [[nodiscard]] OperationReceipt invert_mesh_normals();
  [[nodiscard]] OperationReceipt shade_smooth_mesh();
  [[nodiscard]] OperationReceipt shade_flat_mesh();
  [[nodiscard]] PreviewHandle create_outer_hull(ThickenOptions options = {});
  [[nodiscard]] OperationReceipt snap_vertices_to_active();
  [[nodiscard]] OperationReceipt merge_vertices_to_active();
  [[nodiscard]] OperationReceipt merge_vertices_to_center();
  [[nodiscard]] PreviewHandle
  merge_by_distance(MergeByDistanceOptions options = {});
  [[nodiscard]] OperationReceipt
  remove_doubles(MergeByDistanceOptions options = {});
  [[nodiscard]] OperationReceipt bevel_vertices(BevelVerticesOptions options = {});
  [[nodiscard]] OperationReceipt connect_vertices();
  [[nodiscard]] OperationReceipt dissolve_vertices();
  [[nodiscard]] OperationReceipt radial_align_vertices();
  [[nodiscard]] OperationReceipt snap_edges_to_active();
  [[nodiscard]] OperationReceipt connect_edges();
  [[nodiscard]] OperationReceipt split_edges();
  [[nodiscard]] OperationReceipt harden_edge_normals();
  [[nodiscard]] OperationReceipt soften_edge_normals();
  [[nodiscard]] PreviewHandle bevel_edges(EdgeBevelOptions options = {});
  [[nodiscard]] OperationReceipt bridge_edges(BridgeOptions options = {});
  [[nodiscard]] PreviewHandle
  interpolated_bridge_edges(BridgeOptions options = {});
  [[nodiscard]] OperationReceipt dissolve_edges();
  [[nodiscard]] OperationReceipt merge_edges();
  [[nodiscard]] OperationReceipt collapse_edges();
  [[nodiscard]] OperationReceipt fill_hole();
  [[nodiscard]] OperationReceipt radial_align_edges();
  [[nodiscard]] OperationReceipt bridge_faces(BridgeOptions options = {});
  [[nodiscard]] PreviewHandle
  interpolated_bridge_faces(BridgeOptions options = {});
  [[nodiscard]] OperationReceipt invert_faces();
  [[nodiscard]] OperationReceipt combine_faces();
  [[nodiscard]] OperationReceipt collapse_faces();
  [[nodiscard]] OperationReceipt radial_align_faces();
  [[nodiscard]] PreviewHandle slice_quad(SliceQuadOptions options = {});
  [[nodiscard]] PreviewHandle thicken_faces(ThickenOptions options = {});
  [[nodiscard]] OperationReceipt extract_faces();
  [[nodiscard]] OperationReceipt detach_faces();
  [[nodiscard]] OperationReceipt
  assign_material_slot_to_selection(std::uint32_t slot);
  [[nodiscard]] OperationReceipt plane_cut(PlaneCutOptions options);
  [[nodiscard]] OperationReceipt knife_segment(KnifeSegmentOptions options);
  [[nodiscard]] OperationReceipt knife_stroke(KnifeStrokeOptions options);
  [[nodiscard]] OperationReceipt insert_edge_loop(InsertEdgeLoopOptions options);

private:
  friend class ModelingApi;
  friend class CheckedOperationsApi;
  friend class CommandApi;
  friend class CutTool;
  friend class InsertEdgeLoopTool;
  friend class KnifeTool;
  friend class MirrorTool;
  friend class TransformTool;
  explicit OperationsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Facade for the active session preview transaction.
 */
class PreviewApi {
public:
  PreviewApi() = default;
  [[nodiscard]] bool active() const;
  [[nodiscard]] PreviewTransactionSummary summary() const;
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  friend class TransformTool;
  explicit PreviewApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides revision-gated mesh synchronization payloads.
 */
class MeshSyncApi {
public:
  MeshSyncApi() = default;
  [[nodiscard]] MeshSyncSnapshot
  changes_since(std::uint64_t previous_content_revision) const;
  [[nodiscard]] MeshPayload mesh(ObjectId object,
                                 MeshCompileOptions options = {}) const;
  [[nodiscard]] AuthoredPolygonPayload authored_polygon(ObjectId object) const;

private:
  friend class ModelingApi;
  explicit MeshSyncApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides portable QDR and OBJ serialization.
 */
class IoApi {
public:
  IoApi() = default;
  [[nodiscard]] std::string serialize_qdr() const;
  [[nodiscard]] std::string serialize_qdr(ObjectId object) const;
  [[nodiscard]] std::string serialize_qdr(std::span<const ObjectId> objects) const;
  [[nodiscard]] MeshHandle deserialize_qdr_object(std::string_view text,
                                                  std::string name = "Mesh");
  [[nodiscard]] std::vector<MeshHandle>
  deserialize_qdr_document(std::string_view text);
  [[nodiscard]] std::string serialize_obj(ObjectId object) const;
  [[nodiscard]] std::string serialize_obj(std::span<const ObjectId> objects) const;

private:
  friend class ModelingApi;
  friend class CheckedIoApi;
  friend class CheckedModelingApi;
  explicit IoApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides explicit-result portable IO methods.
 */
class CheckedIoApi {
public:
  CheckedIoApi() = default;
  [[nodiscard]] Result<std::string> serialize_qdr() const;
  [[nodiscard]] Result<std::string> serialize_qdr(ObjectId object) const;
  [[nodiscard]] Result<std::string>
  serialize_qdr(std::span<const ObjectId> objects) const;
  [[nodiscard]] Result<MeshHandle>
  deserialize_qdr_object(std::string_view text, std::string name = "Mesh");
  [[nodiscard]] Result<std::vector<MeshHandle>>
  deserialize_qdr_document(std::string_view text);
  [[nodiscard]] Result<std::string> serialize_obj(ObjectId object) const;
  [[nodiscard]] Result<std::string>
  serialize_obj(std::span<const ObjectId> objects) const;

private:
  friend class CheckedModelingApi;
  friend class IoApi;
  explicit CheckedIoApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides explicit-result current-selection operation methods.
 */
class CheckedOperationsApi {
public:
  CheckedOperationsApi() = default;
  [[nodiscard]] Result<OperationReceipt> delete_selection();
  [[nodiscard]] Result<OperationReceipt> copy_selection();
  [[nodiscard]] Result<OperationReceipt>
  paste_selection(PasteOptions options = {});
  [[nodiscard]] Result<OperationReceipt> repeat_last_action();
  [[nodiscard]] Result<MeshHandle>
  duplicate_meshes(DuplicateOptions options = {});
  [[nodiscard]] Result<OperationReceipt> combine_meshes();
  [[nodiscard]] Result<OperationReceipt> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationReceipt>
  transform_selection(TransformOptions options);
  [[nodiscard]] Result<OperationReceipt> rotate_selection(RotateOptions options);
  [[nodiscard]] Result<OperationReceipt> scale_selection(ScaleOptions options);
  [[nodiscard]] Result<OperationReceipt> extrude(ExtrudeOptions options);
  [[nodiscard]] Result<OperationReceipt> inset(InsetOptions options);
  [[nodiscard]] Result<OperationReceipt> mirror_selection(Axis axis);
  [[nodiscard]] Result<OperationReceipt> flip_horizontal();
  [[nodiscard]] Result<OperationReceipt> flip_vertical();
  [[nodiscard]] Result<OperationReceipt> invert_mesh_normals();
  [[nodiscard]] Result<OperationReceipt> shade_smooth_mesh();
  [[nodiscard]] Result<OperationReceipt> shade_flat_mesh();
  [[nodiscard]] Result<PreviewHandle>
  create_outer_hull(ThickenOptions options = {});
  [[nodiscard]] Result<OperationReceipt> snap_vertices_to_active();
  [[nodiscard]] Result<OperationReceipt> merge_vertices_to_active();
  [[nodiscard]] Result<OperationReceipt> merge_vertices_to_center();
  [[nodiscard]] Result<PreviewHandle>
  merge_by_distance(MergeByDistanceOptions options = {});
  [[nodiscard]] Result<OperationReceipt>
  remove_doubles(MergeByDistanceOptions options = {});
  [[nodiscard]] Result<OperationReceipt>
  bevel_vertices(BevelVerticesOptions options = {});
  [[nodiscard]] Result<OperationReceipt> connect_vertices();
  [[nodiscard]] Result<OperationReceipt> dissolve_vertices();
  [[nodiscard]] Result<OperationReceipt> radial_align_vertices();
  [[nodiscard]] Result<OperationReceipt> snap_edges_to_active();
  [[nodiscard]] Result<OperationReceipt> connect_edges();
  [[nodiscard]] Result<OperationReceipt> split_edges();
  [[nodiscard]] Result<OperationReceipt> harden_edge_normals();
  [[nodiscard]] Result<OperationReceipt> soften_edge_normals();
  [[nodiscard]] Result<PreviewHandle>
  bevel_edges(EdgeBevelOptions options = {});
  [[nodiscard]] Result<OperationReceipt> bridge_edges(BridgeOptions options = {});
  [[nodiscard]] Result<PreviewHandle>
  interpolated_bridge_edges(BridgeOptions options = {});
  [[nodiscard]] Result<OperationReceipt> dissolve_edges();
  [[nodiscard]] Result<OperationReceipt> merge_edges();
  [[nodiscard]] Result<OperationReceipt> collapse_edges();
  [[nodiscard]] Result<OperationReceipt> fill_hole();
  [[nodiscard]] Result<OperationReceipt> radial_align_edges();
  [[nodiscard]] Result<OperationReceipt> bridge_faces(BridgeOptions options = {});
  [[nodiscard]] Result<PreviewHandle>
  interpolated_bridge_faces(BridgeOptions options = {});
  [[nodiscard]] Result<OperationReceipt> invert_faces();
  [[nodiscard]] Result<OperationReceipt> combine_faces();
  [[nodiscard]] Result<OperationReceipt> collapse_faces();
  [[nodiscard]] Result<OperationReceipt> radial_align_faces();
  [[nodiscard]] Result<PreviewHandle> slice_quad(SliceQuadOptions options = {});
  [[nodiscard]] Result<PreviewHandle> thicken_faces(ThickenOptions options = {});
  [[nodiscard]] Result<OperationReceipt> extract_faces();
  [[nodiscard]] Result<OperationReceipt> detach_faces();
  [[nodiscard]] Result<OperationReceipt>
  assign_material_slot_to_selection(std::uint32_t slot);
  [[nodiscard]] Result<OperationReceipt> plane_cut(PlaneCutOptions options);
  [[nodiscard]] Result<OperationReceipt>
  knife_segment(KnifeSegmentOptions options);
  [[nodiscard]] Result<OperationReceipt> knife_stroke(KnifeStrokeOptions options);
  [[nodiscard]] Result<OperationReceipt>
  insert_edge_loop(InsertEdgeLoopOptions options);

private:
  friend class CheckedModelingApi;
  explicit CheckedOperationsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides explicit-result tool activation methods.
 */
class CheckedToolsApi {
public:
  CheckedToolsApi() = default;
  [[nodiscard]] Result<BoxTool> activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] Result<KnifeTool>
  activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] Result<CutTool> activate_cut_tool(CutToolOptions options = {});

private:
  friend class CheckedModelingApi;
  explicit CheckedToolsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides stable string command catalog and dispatch.
 */
class CommandApi {
public:
  CommandApi() = default;
  [[nodiscard]] std::vector<CommandDescriptor> catalog() const;
  [[nodiscard]] CommandDescriptor describe(std::string_view command_id) const;
  [[nodiscard]] OperationReceipt execute(std::string_view command_id,
                                         OperationSettings settings = {});

private:
  friend class ModelingApi;
  explicit CommandApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides host-neutral tool activation and frame handling.
 */
class ToolsApi {
public:
  ToolsApi() = default;
  [[nodiscard]] std::vector<ToolDescriptor> catalog() const;
  [[nodiscard]] std::string active_tool() const;
  [[nodiscard]] ToolStatus status() const;
  [[nodiscard]] SelectTool activate_select_tool();
  [[nodiscard]] TransformTool activate_move_tool();
  [[nodiscard]] TransformTool activate_extend_tool();
  [[nodiscard]] TransformTool activate_extrude_tool();
  [[nodiscard]] TransformTool activate_rotate_tool();
  [[nodiscard]] TransformTool activate_scale_tool();
  [[nodiscard]] BoxTool activate_box_tool(BoxToolOptions options = {});
  [[nodiscard]] InsertEdgeLoopTool
  activate_insert_edge_loop_tool(InsertEdgeLoopToolOptions options = {});
  [[nodiscard]] KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  [[nodiscard]] CutTool activate_cut_tool(CutToolOptions options = {});
  [[nodiscard]] PolyTool activate_poly_tool(PolyToolOptions options = {});
  [[nodiscard]] MirrorTool activate_mirror_tool(MirrorToolOptions options = {});
  [[nodiscard]] PivotTool activate_pivot_tool(PivotToolOptions options = {});
  [[nodiscard]] OperationReceipt
  handle_frame(ViewportId viewport, const ViewportHostInputSnapshot &input,
               const ViewportCameraSnapshot &camera);
  [[nodiscard]] OperationReceipt commit_active_tool_preview();
  [[nodiscard]] OperationReceipt cancel_active_tool_preview();
  [[nodiscard]] OperationReceipt cancel_modal_tool();

private:
  friend class ModelingApi;
  friend class CheckedToolsApi;
  explicit ToolsApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Controller for host-neutral box creation.
 */
class BoxTool {
public:
  BoxTool() = default;
  [[nodiscard]] BoxTool &drag_footprint(PointerEvent begin, PointerEvent update);
  [[nodiscard]] BoxTool &drag_height(PointerEvent update);
  [[nodiscard]] MeshHandle commit(std::string label = "Create Box");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  BoxTool(std::shared_ptr<ModelingApiContext> context, BoxToolOptions options);
  std::shared_ptr<ModelingApiContext> context_;
  BoxToolOptions options_;
};

/**
 * Controller for host-neutral select tool state.
 */
class SelectTool {
public:
  SelectTool() = default;
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit SelectTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Controller for host-neutral transform preview tools.
 */
class TransformTool {
public:
  TransformTool() = default;
  [[nodiscard]] OperationReceipt begin(Vec3 delta = {});
  [[nodiscard]] OperationReceipt update(Vec3 delta);
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  TransformTool(std::shared_ptr<ModelingApiContext> context,
                std::string operation_id);
  std::shared_ptr<ModelingApiContext> context_;
  std::string operation_id_;
  Vec3 value_{};
  bool has_value_ = false;
};

/**
 * Controller for edge-loop insertion.
 */
class InsertEdgeLoopTool {
public:
  InsertEdgeLoopTool() = default;
  [[nodiscard]] InsertEdgeLoopTool &set_edge(EdgeKey edge);
  [[nodiscard]] InsertEdgeLoopTool &set_factor(float factor);
  [[nodiscard]] OperationReceipt commit(std::string label = "Insert Edge Loop");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  InsertEdgeLoopTool(std::shared_ptr<ModelingApiContext> context,
                     InsertEdgeLoopToolOptions options);
  std::shared_ptr<ModelingApiContext> context_;
  InsertEdgeLoopToolOptions options_;
};

/**
 * Controller for host-neutral knife input.
 */
class KnifeTool {
public:
  KnifeTool() = default;
  [[nodiscard]] KnifeTool &add_point(Ray ray);
  [[nodiscard]] KnifeTool &add_segment(Ray from, Ray to);
  [[nodiscard]] KnifeTool &add_point(KnifeTarget target);
  [[nodiscard]] KnifeTool &add_segment(KnifeTarget from, KnifeTarget to);
  [[nodiscard]] OperationReceipt commit(std::string label = "Knife");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  explicit KnifeTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
  std::vector<Ray> points_;
  std::vector<KnifeTarget> targets_;
  std::vector<KnifeStrokeSegment> segments_;
};

/**
 * Controller for host-neutral plane cut input.
 */
class CutTool {
public:
  CutTool() = default;
  [[nodiscard]] CutTool &set_keep_mode(CutKeepMode mode);
  [[nodiscard]] CutTool &set_plane(Vec3 a, Vec3 b, Vec3 c);
  [[nodiscard]] OperationReceipt commit(std::string label = "Cut");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  CutTool(std::shared_ptr<ModelingApiContext> context, CutToolOptions options);
  std::shared_ptr<ModelingApiContext> context_;
  PlaneCutOptions options_;
};

/**
 * Controller for host-neutral polygon drawing input.
 */
class PolyTool {
public:
  PolyTool() = default;
  [[nodiscard]] PolyTool &add_point(Vec3 point);
  [[nodiscard]] PolyTool &set_material(MaterialId material);
  [[nodiscard]] OperationReceipt commit(std::string label = "Poly");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit PolyTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
  std::vector<Vec3> points_;
  MaterialId material_{};
};

/**
 * Controller for mirror tool settings.
 */
class MirrorTool {
public:
  MirrorTool() = default;
  [[nodiscard]] MirrorTool &set_axis(Axis axis);
  [[nodiscard]] OperationReceipt commit(std::string label = "Mirror");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  MirrorTool(std::shared_ptr<ModelingApiContext> context,
             MirrorToolOptions options);
  std::shared_ptr<ModelingApiContext> context_;
  MirrorToolOptions options_;
};

/**
 * Controller for native-editor-owned pivot tool activation.
 */
class PivotTool {
public:
  PivotTool() = default;
  [[nodiscard]] OperationReceipt commit(std::string label = "Pivot");
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ToolsApi;
  explicit PivotTool(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Facade reserved for SDK profiling scenarios.
 */
class ProfilingApi {
public:
  ProfilingApi() = default;

private:
  friend class ModelingApi;
  explicit ProfilingApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

/**
 * Provides explicit renderer-neutral scene snapshot materialization.
 */
class RenderApi {
public:
  RenderApi() = default;
  [[nodiscard]] RenderSnapshotPayload
  scene_snapshot(PayloadRequest request = {}) const;

private:
  friend class ModelingApi;
  explicit RenderApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_;
};

} // namespace quader::modeling
