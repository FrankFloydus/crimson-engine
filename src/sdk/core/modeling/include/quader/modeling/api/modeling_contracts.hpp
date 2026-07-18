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
#include <quader/modeling/profiling/profiling.hpp>
#include <quader/modeling/render/render_payload.hpp>
#include <quader/modeling/session/modeling_session.hpp>
#include <quader/modeling/tools/tool_payload.hpp>

#include <cstddef>
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

namespace detail {
[[nodiscard]] std::shared_ptr<ModelingApiContext>
make_unbound_modeling_api_context();
}

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
 * Defines box creation bounds, subdivisions, winding, name, and material.
 */
struct BoxOptions {
  std::string name = "Mesh";
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
  int x_subdivisions = 1;
  int y_subdivisions = 1;
  int z_subdivisions = 1;
  bool flip_faces = false;
};

/**
 * Defines origin-centered cylinder creation settings.
 */
struct CylinderOptions {
  std::string name = "Mesh";
  float width = 2.0F;
  float height = 2.0F;
  float depth = 2.0F;
  int side_count = 32;
  bool capped = true;
  bool flip_faces = false;
  MaterialId material{};
};

/**
 * Defines origin-centered sphere or upper-hemisphere creation settings.
 */
struct SphereOptions {
  std::string name = "Mesh";
  float width = 2.0F;
  float height = 2.0F;
  float depth = 2.0F;
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  bool flip_faces = false;
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
  int x_subdivisions = 1;
  int y_subdivisions = 1;
  int z_subdivisions = 1;
  bool flip_faces = false;
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
 * Configures the deprecated native-editor-owned pivot tool placeholder.
 *
 * @deprecated Portable pivot semantics are not implemented. This compatibility
 * type is scheduled for removal in 0.2.0; use the native editor tools API.
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
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

using ViewportId = Id<struct ViewportTag>;

} // namespace quader::modeling

