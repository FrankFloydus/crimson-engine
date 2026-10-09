////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"

#include "assets/asset_registry.hpp"
#include "assets/curve_asset.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/modeling/operations/portable_operation_mapping.hpp"
#include "editor/modeling/services/modeling_host_publication_transaction.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_selection_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_material_surface.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <quader/modeling/modeling.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace quader {
namespace {

using quader::modeling::AuthoredTopology;
using quader::modeling::EdgeKey;
using quader::modeling::FaceId;
using quader::modeling::KnifeStrokeSegment;
using quader::modeling::KnifeTarget;
using quader::modeling::KnifeTargetKind;
using quader::modeling::ObjectId;
using quader::modeling::OperationResult;
using quader::modeling::PolygonDocument;
using quader::modeling::PolygonSelectionTopologyEligibility;
using quader::modeling::PolygonSelectionSnapshot;
using quader::modeling::PolygonSelectionUniverse;
using quader::modeling::PreviewId;
using quader::modeling::Result;
using quader::modeling::SelectionEdit;
using quader::modeling::SelectionKind;
using quader::modeling::SelectionMode;
using quader::modeling::SessionComponentSelectionUniverse;
using quader::modeling::SessionDocumentHistoryRestore;
using quader::modeling::SessionDocumentUpdate;
using quader::modeling::SessionWorkspaceDocument;
using quader::modeling::Vec3;
using quader::modeling::VertexId;
using quader::modeling::VertexPositionUpdate;

template <typename ResultType>
[[nodiscard]] ResultType rollback_publication_result(
    ModelingHostPublicationTransaction &publication, ResultType result) {
  if (publication.rollback()) {
    return result;
  }
  const auto mark_rollback_failure = [](auto &command) {
    command.ok = false;
    command.changed = true;
    if (!command.message.empty()) {
      command.message += " ";
    }
    command.message += "Host publication rollback failed.";
  };
  if constexpr (requires { result.command; }) {
    mark_rollback_failure(result.command);
  } else {
    mark_rollback_failure(result);
  }
  return result;
}

template <typename ResultType>
[[nodiscard]] ResultType commit_publication_result(
    ModelingHostPublicationTransaction &publication, ResultType result) {
  if (publication.commit()) {
    return result;
  }
  const auto mark_commit_failure = [](auto &command) {
    command.ok = false;
    command.changed = false;
    if (!command.message.empty()) {
      command.message += " ";
    }
    command.message += "Host publication finalize failed.";
  };
  if constexpr (requires { result.command; }) {
    mark_commit_failure(result.command);
  } else {
    mark_commit_failure(result);
  }
  return rollback_publication_result(publication, std::move(result));
}

[[nodiscard]] constexpr std::uint64_t sdk_object_key(ObjectId object) {
  return (static_cast<std::uint64_t>(object.generation) << 32U) |
         static_cast<std::uint64_t>(object.index);
}

[[nodiscard]] std::uint64_t transform_mesh_layout_key(
    ObjectId object, std::span<const MeshVertex> vertices) {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t key = kOffsetBasis;
  const auto mix = [&key](std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      key ^= (value >> (byte * 8U)) & 0xffU;
      key *= kPrime;
    }
  };
  mix(sdk_object_key(object));
  mix(vertices.size());
  for (const MeshVertex &vertex : vertices) {
    mix(vertex.source_vertex_id);
  }
  return key;
}

/// Captures one SDK-owned object's durable native projection for history.
struct SdkProjectedObjectSnapshot {
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingObjectSceneIdentity identity;
  std::uint32_t material_slot = 0;
  AssetId mesh_id = kInvalidAssetId;
  MeshAsset mesh;
  AssetId curve_id = kInvalidAssetId;
  std::optional<CurveAsset> curve;
};

/// Caches one exact scene/asset-revision proof for a curve preview binding.
struct CurvePreviewSharingProof {
  bool valid = false;
  EntityId entity = kInvalidEntityId;
  AssetId curve = kInvalidAssetId;
  bool shared = false;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
};

[[nodiscard]] std::uint32_t identity_material_slot(
    const ModelingObjectSceneIdentity &identity) {
  return identity.modeling_object.has_value()
             ? identity.modeling_object->material_slot
             : 0u;
}

[[nodiscard]] std::uint32_t identity_local_frame_version(
    const ModelingObjectSceneIdentity &identity) {
  return identity.modeling_object.has_value()
             ? identity.modeling_object->local_frame_version
             : kLegacyUnknownModelingObjectLocalFrameVersion;
}

[[nodiscard]] quader::modeling::Vec3 to_sdk(QVec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] quader::modeling::Transform3
to_sdk(const ModelingTransform3 &value, QVec3 pivot = {}) {
  const QVec3 linear_pivot =
      value.x_axis * pivot.x + value.y_axis * pivot.y +
      value.z_axis * pivot.z;
  return {
      .x_axis = to_sdk(value.x_axis),
      .y_axis = to_sdk(value.y_axis),
      .z_axis = to_sdk(value.z_axis),
      .origin = to_sdk(value.origin + pivot - linear_pivot),
  };
}

[[nodiscard]] QVec3 transform_sdk_point(
    const quader::modeling::Transform3 &transform, QVec3 point) {
  return {
      transform.x_axis.x * point.x + transform.y_axis.x * point.y +
          transform.z_axis.x * point.z + transform.origin.x,
      transform.x_axis.y * point.x + transform.y_axis.y * point.y +
          transform.z_axis.y * point.z + transform.origin.y,
      transform.x_axis.z * point.x + transform.y_axis.z * point.y +
          transform.z_axis.z * point.z + transform.origin.z,
  };
}

[[nodiscard]] quader::modeling::Transform3 to_sdk(const QMat4 &value) {
  const glm::mat4 matrix = to_glm(value);
  return {
      .x_axis = to_sdk(from_glm(glm::vec3{matrix[0]})),
      .y_axis = to_sdk(from_glm(glm::vec3{matrix[1]})),
      .z_axis = to_sdk(from_glm(glm::vec3{matrix[2]})),
      .origin = to_sdk(from_glm(glm::vec3{matrix[3]})),
  };
}

[[nodiscard]] std::string_view
sdk_operation_id(quader_editor_core::EditorOperationId operation) {
  const quader::modeling::OperationDescriptor *descriptor =
      quader_editor_core::portable_operation_descriptor(operation);
  return descriptor != nullptr ? descriptor->dispatch_id : std::string_view{};
}

[[nodiscard]] SelectionEdit to_sdk(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Replace:
    return SelectionEdit::Replace;
  case ModelingSelectionEdit::Add:
    return SelectionEdit::Add;
  case ModelingSelectionEdit::Remove:
    return SelectionEdit::Remove;
  case ModelingSelectionEdit::Toggle:
    return SelectionEdit::Toggle;
  }
  return SelectionEdit::Replace;
}

[[nodiscard]] SelectionMode to_sdk_component_mode(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Edge:
    return SelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return SelectionMode::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
  case ModelingSelectionMode::Vertex:
    return SelectionMode::Vertex;
  }
  return SelectionMode::Vertex;
}

[[nodiscard]] SelectionKind to_sdk_selection_kind(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Edge:
    return SelectionKind::Edge;
  case ModelingSelectionMode::Face:
    return SelectionKind::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
  case ModelingSelectionMode::Vertex:
    return SelectionKind::Vertex;
  }
  return SelectionKind::Vertex;
}

[[nodiscard]] bool component_selection_participates_in_mode(
    const PolygonSelectionSnapshot &selection, ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return !selection.vertices.empty();
  case ModelingSelectionMode::Edge:
    return !selection.edges.empty();
  case ModelingSelectionMode::Face:
    return !selection.faces.empty();
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
    return false;
  }
  return false;
}

[[nodiscard]] ModelingSelectionMode from_native_selection_mode(
    quader_editor_core::SelectionMode mode) {
  switch (mode) {
  case quader_editor_core::SelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case quader_editor_core::SelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case quader_editor_core::SelectionMode::Face:
    return ModelingSelectionMode::Face;
  case quader_editor_core::SelectionMode::Object:
    return ModelingSelectionMode::Object;
  }
  return ModelingSelectionMode::Object;
}

[[nodiscard]] bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

constexpr float kCreationFrameEpsilonSquared = 0.0000000001F;

/// Carries one rigid scene placement and its world-to-local point projection.
struct CreationFrame {
  ModelingTransform3 transform;

  [[nodiscard]] QVec3 local_point(QVec3 world_point) const {
    const QVec3 offset = world_point - transform.origin;
    return {
        dot(offset, transform.x_axis),
        dot(offset, transform.y_axis),
        dot(offset, transform.z_axis),
    };
  }
};

[[nodiscard]] std::optional<CreationFrame>
creation_frame_from_xy(QVec3 origin, QVec3 x_hint, QVec3 y_hint) {
  if (!finite(origin) || !finite(x_hint) || !finite(y_hint) ||
      length_squared(x_hint) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 x_axis = normalize(x_hint);
  const QVec3 rejected_y = y_hint - x_axis * dot(y_hint, x_axis);
  if (length_squared(rejected_y) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 y_axis = normalize(rejected_y);
  const QVec3 z_axis = normalize(cross(x_axis, y_axis));
  if (!finite(x_axis) || !finite(y_axis) || !finite(z_axis) ||
      length_squared(z_axis) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  return CreationFrame{.transform = {
                           .x_axis = x_axis,
                           .y_axis = y_axis,
                           .z_axis = z_axis,
                           .origin = origin,
                       }};
}

[[nodiscard]] std::optional<CreationFrame>
creation_frame_from_xz(QVec3 origin, QVec3 x_hint, QVec3 z_hint) {
  if (!finite(origin) || !finite(x_hint) || !finite(z_hint) ||
      length_squared(z_hint) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 z_axis = normalize(z_hint);
  const QVec3 rejected_x = x_hint - z_axis * dot(x_hint, z_axis);
  if (length_squared(rejected_x) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 x_axis = normalize(rejected_x);
  const QVec3 y_axis = normalize(cross(z_axis, x_axis));
  if (!finite(x_axis) || !finite(y_axis) || !finite(z_axis) ||
      length_squared(y_axis) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  return CreationFrame{.transform = {
                           .x_axis = x_axis,
                           .y_axis = y_axis,
                           .z_axis = z_axis,
                           .origin = origin,
                       }};
}

[[nodiscard]] std::optional<TransformComponent>
scene_transform_from_modeling_transform(const ModelingTransform3 &transform) {
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return std::nullopt;
  }
  const QMat4 matrix{.values = {
                        transform.x_axis.x,
                        transform.x_axis.y,
                        transform.x_axis.z,
                        0.0F,
                        transform.y_axis.x,
                        transform.y_axis.y,
                        transform.y_axis.z,
                        0.0F,
                        transform.z_axis.x,
                        transform.z_axis.y,
                        transform.z_axis.z,
                        0.0F,
                        transform.origin.x,
                        transform.origin.y,
                        transform.origin.z,
                        1.0F,
                    }};
  const std::optional<TransformComponent> component =
      try_transform_component_from_matrix(matrix);
  if (!component.has_value()) {
    return std::nullopt;
  }
  const QMat4 represented = compose_transform(component->spatial_transform());
  for (std::size_t index = 0; index < matrix.values.size(); ++index) {
    const float tolerance =
        0.0001F * std::max(1.0F, std::abs(matrix.values[index]));
    if (std::abs(represented.values[index] - matrix.values[index]) >
        tolerance) {
      return std::nullopt;
    }
  }
  return component;
}

[[nodiscard]] ModelingCommandResult failure(std::string message) {
  return {.ok = false, .changed = false, .message = std::move(message)};
}

/** Classifies the canonical affine request without inferring from planar points. */
[[nodiscard]] bool has_nonsingular_transform(
    const modeling::Transform3 &transform) {
  const auto &x = transform.x_axis;
  const auto &y = transform.y_axis;
  const auto &z = transform.z_axis;
  const double determinant =
      static_cast<double>(x.x) * (static_cast<double>(y.y) * z.z -
                                  static_cast<double>(y.z) * z.y) -
      static_cast<double>(y.x) * (static_cast<double>(x.y) * z.z -
                                  static_cast<double>(x.z) * z.y) +
      static_cast<double>(z.x) * (static_cast<double>(x.y) * y.z -
                                  static_cast<double>(x.z) * y.y);
  return std::isfinite(determinant) && determinant != 0.0;
}

/** Compares canonical affine inputs without padding or hash collisions. */
[[nodiscard]] bool same_transform_request(
    const std::optional<modeling::Transform3> &left,
    const std::optional<modeling::Transform3> &right) {
  if (left.has_value() != right.has_value()) return false;
  if (!left.has_value()) return true;
  const auto same_vector = [](modeling::Vec3 first, modeling::Vec3 second) {
    return first.x == second.x && first.y == second.y && first.z == second.z;
  };
  return same_vector(left->x_axis, right->x_axis) &&
         same_vector(left->y_axis, right->y_axis) &&
         same_vector(left->z_axis, right->z_axis) &&
         same_vector(left->origin, right->origin);
}

/** Hashes affine inputs for candidate lookup; equality still decides reuse. */
[[nodiscard]] std::uint64_t transform_request_hash(
    const std::optional<modeling::Transform3> &transform, std::uint64_t seed) {
  using namespace mesh_asset_payload_detail;
  seed = append_authored_hash(seed, transform.has_value());
  if (transform) {
    for (const auto vector : {transform->x_axis, transform->y_axis,
                              transform->z_axis, transform->origin}) {
      seed = append_authored_hash(seed, authored_float_hash_value(vector.x));
      seed = append_authored_hash(seed, authored_float_hash_value(vector.y));
      seed = append_authored_hash(seed, authored_float_hash_value(vector.z));
    }
  }
  return seed;
}

/** Exact derived-source equality, independent of object/material identity. */
[[nodiscard]] bool same_transform_projection_source(const MeshAsset &left,
                                                    const MeshAsset &right) {
  return left.has_stable_cpu_topology == right.has_stable_cpu_topology &&
         left.has_authored_polygon_document == right.has_authored_polygon_document &&
         (left.geometry_token() == right.geometry_token() ||
          mesh_geometry_payload_equal(left.geometry(), right.geometry())) &&
         (left.authored_polygon_document == right.authored_polygon_document ||
          mesh_asset_payload_detail::same_authored_document(left, right));
}

/** Compares immutable authored publications without materializing SDK views. */
[[nodiscard]] bool same_authored_read_view(quader_poly::DocumentReadView left,
                                           quader_poly::DocumentReadView right) {
  return std::ranges::equal(left.vertices, right.vertices,
                            mesh_asset_payload_detail::same_polygon_vertex) &&
         std::ranges::equal(left.faces, right.faces,
                            mesh_asset_payload_detail::same_polygon_face) &&
         std::ranges::equal(left.hard_normal_edges, right.hard_normal_edges) &&
         std::ranges::equal(left.soft_normal_edges, right.soft_normal_edges) &&
         left.next_vertex_id == right.next_vertex_id &&
         left.next_face_id == right.next_face_id;
}

/** Completes authored layout preservation only for a committed document edit. */
[[nodiscard]] Result<OperationResult> transform_document_for_commit(
    PolygonDocument &document, const modeling::Transform3 &transform) {
  const PolygonDocument before = document;
  auto result = document.transform_selection(transform);
  if (!result.ok() || !result.value().success || !result.value().changed ||
      !has_nonsingular_transform(transform)) {
    return result;
  }
  auto preserved = MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
      before, document);
  if (!preserved.ok()) return preserved;
  if (preserved.value().changed) {
    result.value().dirty.topology = true;
    result.value().dirty.materials = true;
    result.value().dirty.overlays = true;
    result.value().modified.faces.insert(result.value().modified.faces.end(),
        preserved.value().modified.faces.begin(),
        preserved.value().modified.faces.end());
  }
  return result;
}

[[nodiscard]] ModelingCommandResult from_sdk(
    const Result<OperationResult> &result, std::string fallback = {}) {
  if (!result.ok()) {
    return failure(result.error_message().empty() ? std::move(fallback)
                                                  : result.error_message());
  }
  return {
      .ok = result.value().success,
      .changed = result.value().changed,
      .message = result.value().message,
  };
}

[[nodiscard]] bool scene_mesh_is_shared(const SceneDocument &scene,
                                        AssetId mesh) {
  if (mesh == kInvalidAssetId) {
    return false;
  }
  return scene.mesh_binding_reference_count(mesh) > 1u;
}

} // namespace

/// Stores the opaque SDK and native mapping state carried by one memento.
struct ModelingSdkProjectionSnapshot::Impl {
  struct MergeChildPlacement {
    EntityId entity = kInvalidEntityId;
    EntityId parent = kInvalidEntityId;
    TransformComponent transform;
    std::optional<std::size_t> sibling_ordinal;
  };
  quader::modeling::ModelingSession session;
  std::map<int, ObjectId> object_ids;
  std::vector<SdkProjectedObjectSnapshot> projected_objects;
  bool component_merge_history = false;
  std::vector<MergeChildPlacement> merge_child_placements;
  int next_native_object_id = 1;
  int active_native_object_id = kInvalidModelingObjectId;
  ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
  std::uint64_t persistent_content_revision = 0;
  std::uint64_t mode_revision = 0;
  std::optional<EditorModeSnapshot> editor_mode;
};

ModelingSdkProjectionSnapshot::ModelingSdkProjectionSnapshot() = default;
ModelingSdkProjectionSnapshot::~ModelingSdkProjectionSnapshot() = default;

ModelingSdkProjectionSnapshot::ModelingSdkProjectionSnapshot(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSdkProjectionSnapshot::valid() const noexcept {
  return static_cast<bool>(impl_);
}

/// Keeps sparse authored positions and exact documents only for welded meshes.
struct ModelingSdkTransformHistoryDelta::Impl {
  /// Pairs one SDK object with its exact before and after transform state.
  struct ObjectDelta {
    ObjectId object{};
    std::vector<VertexPositionUpdate> before;
    std::vector<VertexPositionUpdate> after;
    std::optional<PolygonDocument> topology_before;
    std::optional<PolygonDocument> topology_after;
  };

  std::vector<ObjectDelta> objects;
};

ModelingSdkTransformHistoryDelta::ModelingSdkTransformHistoryDelta() = default;
ModelingSdkTransformHistoryDelta::~ModelingSdkTransformHistoryDelta() = default;

ModelingSdkTransformHistoryDelta::ModelingSdkTransformHistoryDelta(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSdkTransformHistoryDelta::valid() const noexcept {
  return impl_ != nullptr && !impl_->objects.empty();
}

[[nodiscard]] bool same_polygon_selection(
    const PolygonSelectionSnapshot &left,
    const PolygonSelectionSnapshot &right) {
  return left.kind == right.kind && left.vertices == right.vertices &&
         left.edges == right.edges && left.faces == right.faces &&
         left.has_active == right.has_active &&
         left.active_vertex == right.active_vertex &&
         left.active_edge == right.active_edge &&
         left.active_face == right.active_face;
}

/// Keeps source documents private to the publication owner across adjustments.
struct ModelingFaceExtrusionSource::Impl {
  /// Revision proofs are bookkeeping; all source documents remain immutable.
  struct Fence {
    std::uint64_t content = 0;
    std::uint64_t selection = 0;
    std::uint64_t mesh_bindings = 0;
    std::uint64_t assets = 0;
    std::uint64_t object_map = 0;
    std::uint64_t transforms = 0;
    ModelingInstanceEditScope scope;
    friend bool operator==(const Fence &, const Fence &) = default;
  };
  struct Object {
    ObjectId object{};
    int native_id = kInvalidModelingObjectId;
    EntityId entity = kInvalidEntityId;
    modeling::Transform3 world_from_local{};
    PolygonDocument document;
  };

  const void *owner = nullptr;
  std::uint64_t generation = 0;
  std::vector<Object> objects;
  QVec3 direction_world{};
  mutable Fence fence;
  struct Request {
    modeling::FaceExtrudeMode mode = modeling::FaceExtrudeMode::Region;
    float amount = 0.0f;
    QVec3 direction{};
  };
  mutable std::optional<Request> last_request;
};

ModelingFaceExtrusionSource::ModelingFaceExtrusionSource(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingFaceExtrusionSource::valid() const noexcept {
  return impl_ != nullptr && !impl_->objects.empty();
}

/// Implements one workspace-scoped SDK session and its host projection map.
class ModelingSdkProjectionAdapter::Impl final {
private:
  using DocumentMutation =
      std::function<Result<OperationResult>(PolygonDocument &)>;
  using DocumentMutationByObject =
      std::function<Result<OperationResult>(ObjectId, PolygonDocument &)>;
  using CurveDocumentMutationByObject =
      std::function<Result<OperationResult>(ObjectId,
                                            modeling::CurveDocument &)>;
  using WorkspaceTransformResolver = std::function<
      std::optional<quader::modeling::Transform3>(ObjectId)>;

  /// Tracks one selected authored vertex and every compiled position derived
  /// from it during a topology-stable transform preview.
  struct TransientTransformSelectedVertex {
    std::uint32_t source_vertex_id = 0u;
    QVec3 base_position;
    QVec3 preview_position;
    std::vector<std::size_t> compiled_vertex_indices;
  };

  /// Owns reusable preview buffers and retained topology state for one SDK
  /// object participating in a workspace transform.
  struct TransientTransformParticipant {
    ObjectId object;
    int native_object_id = kInvalidModelingObjectId;
    EntityId entity = kInvalidEntityId;
    MaterialComponent material;
    QMat4 world_transform;
    bool double_sided = false;
    std::uint64_t layout_key = 0u;
    std::uint64_t position_revision = 0u;
    bool preserve_preview_uv_layout = false;
    // Exact authored/layout/movable-set equivalence established at gesture start.
    std::size_t projection_group = 0U;
    std::shared_ptr<const quader_poly::Document> transform_source;
    std::optional<modeling::PreparedSelectionTransform> prepared_transform;
    PolygonDocument base_document;
    PolygonDocument preview_document;
    MeshAsset preview_mesh;
    std::shared_ptr<const MeshProjectionPositionUpdatePlan> position_update_plan;
    std::shared_ptr<const MeshRetainedPositionProjectionPlan> retained_position_plan;
    std::shared_ptr<const MeshGeometryProjection> geometry_projection;
    std::vector<QVec3> base_mesh_positions;
    std::vector<TransientTransformSelectedVertex> selected_vertices;
    std::unordered_map<std::uint32_t, std::size_t> selected_vertex_lookup;
    // The pool owns mutable packed position buffers. A buffer is reused only
    // when no immutable render snapshot still pins it (use_count == 1).
    std::vector<std::shared_ptr<std::vector<QVec3>>> position_buffers;
    std::size_t published_position_buffer = 0u;
    std::unordered_map<std::uint32_t, QVec3> base_positions;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
        face_vertices;
  };

  /// Revision proof over every owner that can invalidate accepted coordinates.
  struct AcceptedOverlayProof final {
    std::uint64_t content = 0u;
    std::uint64_t sdk_content = 0u;
    std::uint64_t selection = 0u;
    std::uint64_t scene = 0u;
    std::uint64_t assets = 0u;
    std::uint64_t object_map = 0u;
    int active_object = -1;
    ModelingInstanceEditScope instance_scope;
    friend bool operator==(const AcceptedOverlayProof &,
                           const AcceptedOverlayProof &) = default;
  };

  /// Immutable SDK preparation and cancellation origin for one drag participant.
  struct FaceExtrusionPreviewObject {
    ObjectId object;
    PolygonDocument baseline;
    modeling::FaceExtrudeMode mode = modeling::FaceExtrudeMode::Region;
    std::optional<modeling::PreparedFaceExtrusion> prepared;
  };

  /** Restores dormant component intent after a whole-object document operation. */
  [[nodiscard]] static Result<OperationResult> restore_dormant_selection(
      PolygonDocument &document, PolygonSelectionSnapshot selection,
      const OperationResult &receipt) {
    const auto remap = []<typename Element>(std::vector<Element> &selected,
        const std::vector<Element> &source, const std::vector<Element> &destination,
        const std::vector<Element> &live) {
      std::vector<Element> mapped;
      for (const Element element : selected) {
        const auto found = std::ranges::find(source, element);
        if (found != source.end() && source.size() == destination.size()) {
          mapped.push_back(destination[static_cast<std::size_t>(found - source.begin())]);
        } else if (found != source.end() && source.size() == 1u) {
          mapped.insert(mapped.end(), destination.begin(), destination.end());
        } else {
          mapped.push_back(element);
        }
      }
      selected.clear();
      for (const Element element : mapped) {
        if (std::ranges::find(live, element) != live.end() &&
            std::ranges::find(selected, element) == selected.end()) {
          selected.push_back(element);
        }
      }
    };
    if (receipt.dirty.topology) {
      const auto &mapping = receipt.selection_remap;
      remap(selection.vertices, mapping.source.vertices, mapping.destination.vertices,
            document.vertex_ids());
      remap(selection.edges, mapping.source.edges, mapping.destination.edges,
            document.edge_ids());
      remap(selection.faces, mapping.source.faces, mapping.destination.faces,
            document.face_ids());
    }
    selection.has_active = false;
    if (selection.kind == SelectionKind::Vertex && !selection.vertices.empty()) {
      selection.has_active = true;
      if (std::ranges::find(selection.vertices, selection.active_vertex) == selection.vertices.end()) {
        selection.active_vertex = selection.vertices.front();
      }
    } else if (selection.kind == SelectionKind::Edge && !selection.edges.empty()) {
      selection.has_active = true;
      if (std::ranges::find(selection.edges, selection.active_edge) == selection.edges.end()) {
        selection.active_edge = selection.edges.front();
      }
    } else if (selection.kind == SelectionKind::Face && !selection.faces.empty()) {
      selection.has_active = true;
      if (std::ranges::find(selection.faces, selection.active_face) == selection.faces.end()) {
        selection.active_face = selection.faces.front();
      }
    }
    return document.apply_selection(selection, SelectionEdit::Replace);
  }

  [[nodiscard]] static Result<OperationResult>
  apply_with_whole_object_face_selection(
      ModelingSelectionMode mode, PolygonDocument &document,
      const DocumentMutation &operation) {
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group) {
      return operation(document);
    }
    const auto dormant = document.selection();
    Result<OperationResult> selected = document.select_all_faces();
    if (!selected.ok()) {
      return selected;
    }
    Result<OperationResult> operated = operation(document);
    if (!operated.ok()) {
      return operated;
    }
    auto restored = restore_dormant_selection(document, dormant, operated.value());
    if (!restored.ok()) {
      return restored;
    }
    operated.value().dirty.selection = operated.value().dirty.topology && restored.value().changed;
    return operated;
  }

  /// Retains enough portable operation state to rebuild a preview after a
  /// selection edit is applied to its committed base document.
  struct PreviewSelectionEditState {
    bool active = false;
    bool uv_peel = false;
    bool ok = true;
    std::string message;
    quader_editor_core::EditorOperationId operation{};
    quader_editor_core::EditorOperationSettings settings;
    std::optional<double> edge_bevel_maximum_width;
  };

  /// Keys the derived selection summary by every SDK and host owner revision.
  struct SelectionSummaryCacheKey {
    std::uint64_t persistent_content_revision = 0;
    std::uint64_t session_content_revision = 0;
    std::uint64_t session_selection_revision = 0;
    std::uint64_t mode_revision = 0;
    std::uint64_t object_map_revision = 0;
    int active_native_object_id = kInvalidModelingObjectId;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    bool sdk_preview_active = false;
    PreviewId preview_id{};
    ObjectId preview_object_id{};
    std::uint64_t preview_content_revision = 0;

    friend bool operator==(const SelectionSummaryCacheKey &,
                           const SelectionSummaryCacheKey &) = default;
  };

  /// Keys the immutable selection snapshot used by native host queries.
  struct SelectionProjectionSnapshotCacheKey {
    std::uint64_t persistent_content_revision = 0;
    std::uint64_t session_content_revision = 0;
    std::uint64_t session_selection_revision = 0;
    std::uint64_t mode_revision = 0;
    std::uint64_t scene_mesh_binding_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t object_map_revision = 0;
    int active_native_object_id = kInvalidModelingObjectId;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    bool sdk_preview_active = false;
    PreviewId preview_id{};
    ObjectId preview_object_id{};
    std::uint64_t preview_content_revision = 0;
    std::uint64_t plane_cut_revision = 0;

    friend bool operator==(const SelectionProjectionSnapshotCacheKey &,
                           const SelectionProjectionSnapshotCacheKey &) =
        default;
  };

  /// Pins the accepted Cut documents used by rendering, picking and commit.
  struct PlaneCutCandidate {
    int native_id = kInvalidModelingObjectId;
    ObjectId object;
    PolygonDocument kept;
    std::optional<PolygonDocument> second;
    PolygonDocument projected;
    MeshAsset projected_mesh;
    QMat4 world_from_object;
    PolygonSelectionSnapshot dormant_selection;
    OperationResult receipt;
  };

  struct PlaneCutPreviewCache {
    std::vector<ModelingSdkPlaneCutRequest> requests;
    ModelingCutMode mode = ModelingCutMode::KeepBoth;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t scene_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t object_map_revision = 0;
    std::vector<PlaneCutCandidate> candidates;
    ModelingSdkPlaneCutPreviewResult preview;
  };

  /// Stores only adapter-local state; owner checkpoints live in the shared
  /// host-publication transaction and binding registry.
  struct AdapterPublicationState {
    std::optional<EditorModeSnapshot> editor_mode;
    int next_native_object_id = 1;
    int active_native_object_id = kInvalidModelingObjectId;
    ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
    std::uint64_t persistent_content_revision = 0;
    std::uint64_t mode_revision = 0;
    std::uint64_t curve_preview_scene_revision = 0;
    std::uint64_t curve_preview_asset_revision = 0;
    bool curve_preview_external_changes = false;
    AssetId curve_preview_asset_id = kInvalidAssetId;
    CurvePreviewSharingProof curve_preview_sharing_proof;
    bool preview_changed = false;
    std::string active_preview_operation_id;
    std::optional<quader_editor_core::EditorOperationId>
        active_preview_operation;
    std::optional<quader_editor_core::EditorOperationSettings>
        active_preview_settings;
    std::optional<double> active_edge_bevel_maximum_width;
    std::size_t active_preview_deleted_vertex_count = 0;
    ModelingSdkProjectionStats stats;
  };

  /** Host identities created or suppressed by one SDK structural preview. */
  struct StructuralPreviewHostState {
    struct AddedObject {
      int modeling_object_id = kInvalidModelingObjectId;
    };

    struct MergeSources {
      ObjectId survivor;
      std::vector<modeling::SessionMergeSource> sources;
      modeling::Transform3 world_from_survivor;
    };

    PreviewId preview_id{};
    int next_native_object_id_before = 1;
    std::vector<AddedObject> added_objects;
    std::vector<ObjectId> source_objects;
    std::vector<std::pair<ObjectId, ObjectId>> half_cut_added_by_source;
    std::optional<ObjectId> bridge_survivor;
    std::optional<ObjectId> bridge_source;
    std::optional<quader::modeling::Transform3> bridge_survivor_from_source;
    std::optional<MergeSources> merge_sources;

    [[nodiscard]] bool active_for(PreviewId id) const noexcept {
      return preview_id.valid() && preview_id == id;
    }
  };

  /** Participant set for one bounded native host publication. */
  struct HostPublicationScope {
    std::vector<EntityId> scene_entities;
    std::vector<AssetId> asset_ids;
    std::vector<int> modeling_object_ids;
  };

  /** Exact pre-preview host identity and binding proof retained for rollback. */
  struct PreviewHistoryAssetIdentity {
    ObjectId object{};
    modeling::SessionObjectKind kind = modeling::SessionObjectKind::Mesh;
    AssetId asset = kInvalidAssetId;
    ModelingObjectSceneIdentity identity;
    bool binding_was_unique = false;
  };

  /** Exact SDK participant state acknowledged after our last publication. */
  struct PreviewSdkObjectFence {
    ObjectId object{};
    modeling::SessionPreviewAction action =
        modeling::SessionPreviewAction::ReplaceExisting;
    modeling::SessionObjectKind kind = modeling::SessionObjectKind::Mesh;
    std::uint64_t base_content_revision = 0U;
    std::uint64_t content_revision = 0U;
    bool changed = false;
    std::uint32_t material_slot = 0U;
    bool selected = false;

    friend bool operator==(const PreviewSdkObjectFence &,
                           const PreviewSdkObjectFence &) = default;
  };

  struct PreviewSdkFence {
    PreviewId preview{};
    std::string operation_id;
    std::uint64_t preview_content_revision = 0U;
    // Derived during the existing participant walk, never a separate counter.
    std::uint64_t polygon_content_revision = 0U;
    std::vector<PreviewSdkObjectFence> preview_objects;

    friend bool operator==(const PreviewSdkFence &,
                           const PreviewSdkFence &) = default;
  };

  /** Bounded native projection baseline keyed to the SDK preview identity. */
  struct MeshPreviewHostBaseline {
    /// Retains only the last accepted canonical request for one participant.
    struct AffineInput {
      ObjectId object;
      std::optional<modeling::Transform3> transform;
    };
    PreviewId preview_id{};
    // Only used to transfer a committed transform into native history. It is
    // never a rollback cursor; the active SDK preview is the rollback owner.
    std::size_t transform_history_undo_mark = 0U;
    AdapterPublicationState adapter_before;
    HostPublicationScope scope;
    std::optional<ModelingHostProjectionCheckpoint> owner;
    std::vector<EntityId> created_entities;
    std::vector<AssetId> created_assets;
    std::vector<PreviewHistoryAssetIdentity> history_asset_identities;
    // Canonical affine eligibility for final document preparation; no geometry.
    std::vector<ObjectId> preserve_uv_layout_objects;
    std::vector<AffineInput> affine_inputs;
    std::optional<std::uint64_t> affine_input_revision;
    PreviewSdkFence acknowledged_sdk;
    bool fence_acknowledged = false;

    [[nodiscard]] bool valid() const noexcept {
      return owner.has_value() && owner->valid();
    }

    [[nodiscard]] bool active_for(PreviewId id) const noexcept {
      return valid() && preview_id.valid() && preview_id == id;
    }
  };

  /// Carries the complete candidate set required by one SDK mesh preview.
  struct PreparedMeshPreviewBatch {
    std::vector<modeling::SessionPreviewDocumentUpdate> updates;
    std::size_t deleted_vertex_count = 0U;
    bool changed = false;
    std::string message;
  };

  struct PreparedHalfCutObject {
    ObjectId source{};
    PolygonDocument front;
    std::optional<PolygonDocument> back;
    OperationResult receipt;
    ModelingObjectSceneIdentity source_identity;
    std::uint32_t material_slot = 0u;
  };

  template <typename Value>
  static void sort_unique(std::vector<Value> &values) {
    std::ranges::sort(values);
    values.erase(std::unique(values.begin(), values.end()), values.end());
  }

  static void finalize_host_publication_scope(HostPublicationScope &scope) {
    std::erase(scope.scene_entities, kInvalidEntityId);
    std::erase(scope.asset_ids, kInvalidAssetId);
    std::erase(scope.modeling_object_ids, kInvalidModelingObjectId);
    sort_unique(scope.scene_entities);
    sort_unique(scope.asset_ids);
    sort_unique(scope.modeling_object_ids);
  }

  void append_scene_entity_publication_scope(HostPublicationScope &scope,
                                             EntityId entity,
                                             bool include_descendants) const {
    if (entity == kInvalidEntityId) {
      return;
    }
    if (!scene_.exists(entity)) {
      scope.scene_entities.push_back(entity);
      return;
    }

    std::vector<EntityId> entities;
    if (include_descendants) {
      entities = scene_.descendants(entity, true);
    } else {
      entities.push_back(entity);
    }
    scope.scene_entities.insert(scope.scene_entities.end(), entities.begin(),
                                entities.end());
    for (const EntityId scoped_entity : entities) {
      if (const MeshComponent *mesh = scene_.mesh(scoped_entity)) {
        scope.asset_ids.push_back(mesh->mesh);
      }
      if (const CurveComponent *curve = scene_.curve(scoped_entity)) {
        scope.asset_ids.push_back(curve->curve);
      }
      if (const ModelingObjectComponent *modeling =
              scene_.modeling_object(scoped_entity)) {
        scope.modeling_object_ids.push_back(modeling->object_id);
      }
    }
  }

  [[nodiscard]] bool append_native_modeling_publication_scope(
      HostPublicationScope &scope, int native_id,
      bool include_descendants) const {
    if (native_id == kInvalidModelingObjectId) {
      return false;
    }
    scope.modeling_object_ids.push_back(native_id);
    const std::optional<EntityId> entity =
        object_map_.entity_for_modeling_object(native_id);
    if (!entity.has_value() || !scene_.exists(*entity)) {
      return false;
    }
    append_scene_entity_publication_scope(scope, *entity, include_descendants);
    return true;
  }

  [[nodiscard]] bool append_sdk_object_publication_scope(
      HostPublicationScope &scope, ObjectId object_id,
      bool include_descendants) const {
    const std::optional<int> native = native_object(object_id);
    return native.has_value() &&
           append_native_modeling_publication_scope(
               scope, *native, include_descendants);
  }

  void append_selected_scene_publication_scope(HostPublicationScope &scope,
                                               bool include_descendants) const {
    for (const EntityId selected : scene_.selected_entities()) {
      append_scene_entity_publication_scope(scope, selected,
                                            include_descendants);
    }
  }

  void append_future_modeling_ids(HostPublicationScope &scope, int first,
                                  std::size_t count) const {
    if (first == kInvalidModelingObjectId) {
      return;
    }
    for (std::size_t index = 0; index < count; ++index) {
      if (index >
          static_cast<std::size_t>(std::numeric_limits<int>::max() - first)) {
        break;
      }
      scope.modeling_object_ids.push_back(first + static_cast<int>(index));
    }
  }

  friend class ModelingSdkProjectionAdapter;

public:
  Impl(SceneDocument &scene, AssetRegistry &assets,
       ModelingObjectRuntimeMap &object_map,
       ModelingMeshSyncService &mesh_sync)
      : scene_(scene), assets_(assets), object_map_(object_map),
        mesh_sync_(mesh_sync) {}

  void set_modeling_object_world_transform_resolver(
      std::function<std::optional<QMat4>(int)> resolver) {
    modeling_object_world_transform_resolver_ = std::move(resolver);
  }

  void set_instance_edit_scope_provider(
      std::function<ModelingInstanceEditScope()> provider) {
    instance_edit_scope_provider_ = std::move(provider);
  }

  void set_modeling_object_texture_sampling_resolver(
      std::function<std::optional<QMat4>(int)> resolver) {
    modeling_object_texture_sampling_resolver_ = std::move(resolver);
  }

  [[nodiscard]] Result<PolygonDocument> reconciled_sdk_document(
      ObjectId object, const quader_poly::Document &document) const {
    quader_poly::Document reconciled = document;
    Result<PolygonDocument> current = session_.document_copy(object);
    Result<AuthoredTopology> current_topology =
        current.ok()
            ? current.value().authored_topology()
            : Result<AuthoredTopology>::failure(current.error());
    if (!current_topology.ok()) {
      return Result<PolygonDocument>::failure(current_topology.error());
    }
    reconciled.next_vertex_id =
        std::max(reconciled.next_vertex_id,
                 current_topology.value().next_vertex_id);
    reconciled.next_face_id =
        std::max(reconciled.next_face_id,
                 current_topology.value().next_face_id);
    return MeshProjectionService::sdk_document_from_polygon_document(
        reconciled);
  }

  void reset_from_scene() {
    clear_plane_cut_preview();
    accepted_overlay_coordinates_.reset();
    ++face_extrusion_generation_;
    ++stats_.full_scene_reset_count;
    // Workspace replacement allocates fresh SDK objects. Retain only allocation
    // cursors from still-matching native identities, including canceled preview
    // IDs, without changing the authored asset or its publication revision.
    std::map<int, modeling::CurveIdentityHighWater> retained_curve_cursors;
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      const auto identity =
          object_map_.scene_identity_for_modeling_object(native_id);
      if (!identity.has_value() || !identity->curve.has_value() ||
          !scene_.exists(identity->id) ||
          scene_.uuid(identity->id) != identity->uuid ||
          scene_.editor_object_id(identity->id) != identity->editor_object_id) {
        continue;
      }
      const auto *component = scene_.modeling_object(identity->id);
      if (component == nullptr || component->object_id != native_id ||
          scene_.curve(identity->id) == nullptr) {
        continue;
      }
      const auto document = session_.curve_document_copy(object_id);
      if (document.ok()) {
        retained_curve_cursors.emplace(native_id,
                                       document.value().identity_high_water());
      }
    }
    object_map_.clear();
    object_map_.clear_sdk_object_bindings();
    mesh_preview_host_baseline_.reset();
    structural_preview_host_state_.reset();
    preview_changed_ = false;
    curve_preview_external_changes_ = false;
    curve_preview_asset_id_ = kInvalidAssetId;
    curve_preview_sharing_proof_ = {};
    transient_transform_payload_participants_.clear();
    transient_transform_payload_active_ = false;
    face_extrusion_preview_objects_.clear();
    clear_face_extrusion_preview_projection();
    committed_transform_history_delta_ = {};
    committed_transform_undo_mark_.reset();
    ++transient_transform_payload_revision_;
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    active_edge_bevel_maximum_width_.reset();
    selection_mode_ = ModelingSelectionMode::Object;
    persistent_content_revision_ = 0;
    mode_revision_ = 0;
    active_native_object_id_ = kInvalidModelingObjectId;
    next_native_object_id_ = 1;

    /// Associates one hydrated SDK document with its native scene identity.
    struct SceneWorkspaceObject {
      int native_object_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      bool mark_sdk_owned = false;
    };
    std::vector<quader::modeling::SessionWorkspaceDocument>
        workspace_documents;
    std::vector<SceneWorkspaceObject> workspace_objects;
    const std::vector<EntityId> scene_entities = scene_.entity_tree_order();
    workspace_documents.reserve(scene_entities.size());
    workspace_objects.reserve(scene_entities.size());
    const EntityId active_entity = scene_.primary_selection();
    for (const EntityId entity : scene_entities) {
      const ModelingObjectComponent *component = scene_.modeling_object(entity);
      if (component == nullptr ||
          component->object_id == kInvalidModelingObjectId) {
        continue;
      }
      if (component->object_id < std::numeric_limits<int>::max()) {
        next_native_object_id_ =
            std::max(next_native_object_id_, component->object_id + 1);
      }
      if (const CurveComponent *binding = scene_.curve(entity)) {
        const CurveAsset *curve = assets_.curve(binding->curve);
        if (curve == nullptr || curve->document == nullptr ||
            !curve->document->validate().ok()) {
          continue;
        }
        const NameComponent *name = scene_.name(entity);
        const SelectionComponent *selection = scene_.selection(entity);
        modeling::CurveDocument document = *curve->document;
        if (const auto cursor = retained_curve_cursors.find(component->object_id);
            cursor != retained_curve_cursors.end()) {
          document.preserve_identity_high_water(cursor->second);
        }
        workspace_documents.push_back({
            .name = name != nullptr && !name->name.empty() ? name->name : "Curve",
            .selected = selection != nullptr && selection->selected,
            .curve_document = std::move(document),
        });
        workspace_objects.push_back({
            .native_object_id = component->object_id,
            .entity = entity,
            .mark_sdk_owned = !component->sdk_owned,
        });
        if (entity == active_entity) {
          active_native_object_id_ = component->object_id;
        }
        continue;
      }
      const MeshComponent *mesh_component = scene_.mesh(entity);
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      if (mesh == nullptr || !mesh->has_authored_polygon_document ||
          !mesh->authored_polygon_document) {
        continue;
      }
      Result<PolygonDocument> document =
          MeshProjectionService::sdk_document_from_polygon_document(
              *mesh->authored_polygon_document);
      if (!document.ok()) {
        continue;
      }
      const NameComponent *name = scene_.name(entity);
      if (const SelectionComponent *selection = scene_.selection(entity);
          selection != nullptr) {
        workspace_documents.push_back({
            .document = std::move(document).value(),
            .name = name != nullptr && !name->name.empty() ? name->name
                                                            : "Mesh",
            .selected = selection->selected,
        });
      } else {
        workspace_documents.push_back({
            .document = std::move(document).value(),
            .name = name != nullptr && !name->name.empty() ? name->name
                                                            : "Mesh",
        });
      }
      workspace_objects.push_back({
          .native_object_id = component->object_id,
          .entity = entity,
          .mark_sdk_owned = !component->sdk_owned,
      });
      if (entity == active_entity) {
        active_native_object_id_ = component->object_id;
      }
    }

    Result<std::vector<ObjectId>> hydrated =
        session_.replace_workspace_preserving_clipboard(
            std::move(workspace_documents));
    if (!hydrated.ok() ||
        hydrated.value().size() != workspace_objects.size()) {
      session_.reset_workspace_preserving_clipboard();
      active_native_object_id_ = kInvalidModelingObjectId;
      return;
    }
    for (std::size_t index = 0; index < workspace_objects.size(); ++index) {
      const SceneWorkspaceObject &workspace_object = workspace_objects[index];
      const ObjectId object_id = hydrated.value()[index];
      if (!object_map_.bind_sdk_object(workspace_object.native_object_id,
                                       object_id) ||
          !object_map_.bind(workspace_object.native_object_id,
                            workspace_object.entity)) {
        session_.reset_workspace_preserving_clipboard();
        object_map_.clear();
        active_native_object_id_ = kInvalidModelingObjectId;
        return;
      }
      object_map_.remember_scene_entity(workspace_object.native_object_id,
                                        scene_, workspace_object.entity);
      if (workspace_object.mark_sdk_owned) {
        (void)mesh_sync_.mark_projected_object_sdk_owned(
            scene_, workspace_object.entity);
      }
    }
    stats_.scene_import_document_count += workspace_objects.size();
  }

  [[nodiscard]] ModelingCommandResult restore_mesh_documents_from_scene(
      std::span<const EntityId> entities) {
    if (entities.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "No modeling mesh documents changed."};
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }

    std::unordered_set<int> restored_native_ids;
    restored_native_ids.reserve(entities.size());
    std::vector<SessionDocumentHistoryRestore> restores;
    restores.reserve(entities.size());
    for (const EntityId entity : entities) {
      const ModelingObjectComponent *component =
          scene_.modeling_object(entity);
      if (component != nullptr && scene_.curve(entity) != nullptr) {
        const auto object = sdk_object(component->object_id);
        const CurveAsset *curve = assets_.curve(scene_.curve(entity)->curve);
        if (!object.has_value() || curve == nullptr || curve->document == nullptr) {
          return failure("Curve modeling document could not be restored.");
        }
        if (restored_native_ids.emplace(component->object_id).second) {
          restores.push_back({
              .object = *object,
              .dirty = {.topology = true, .geometry = true, .overlays = true},
              .curve_document = *curve->document,
          });
        }
        continue;
      }
      const MeshComponent *mesh_component = scene_.mesh(entity);
      const MeshAsset *mesh =
          mesh_component != nullptr ? assets_.mesh(mesh_component->mesh)
                                    : nullptr;
      if (component == nullptr ||
          component->object_id == kInvalidModelingObjectId ||
          mesh == nullptr || !mesh->has_authored_polygon_document ||
          mesh->authored_polygon_document == nullptr) {
        return failure(
            "Pivot mesh is not an SDK-owned authored modeling document.");
      }
      if (!restored_native_ids.emplace(component->object_id).second) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(component->object_id);
      if (!object.has_value()) {
        return failure("Pivot modeling object does not exist.");
      }
      Result<PolygonDocument> document = reconciled_sdk_document(
          *object, *mesh->authored_polygon_document);
      if (!document.ok()) {
        return failure(document.error_message());
      }
      restores.push_back({
          .object = *object,
          .document = std::move(document).value(),
          .dirty = {.topology = true,
                    .geometry = true,
                    .materials = true,
                    .overlays = true},
      });
    }

    Result<OperationResult> restored =
        session_.restore_documents_as_history_transition(std::move(restores));
    if (!restored.ok()) {
      return from_sdk(restored,
                      "Pivot modeling documents could not be restored.");
    }
    ++persistent_content_revision_;
    ++stats_.targeted_scene_sync_count;
    stats_.targeted_scene_sync_document_count += restored_native_ids.size();
    return from_sdk(restored);
  }

  [[nodiscard]] bool owns_object(int modeling_object_id) const {
    return sdk_bindings().contains(modeling_object_id);
  }

  [[nodiscard]] bool owns_entity(EntityId entity) const {
    const std::optional<int> object = object_map_.modeling_object_for_entity(entity);
    return object.has_value() && owns_object(*object);
  }

  [[nodiscard]] std::size_t object_count() const noexcept {
    return sdk_bindings().size();
  }

  [[nodiscard]] bool has_selection() const {
    const auto summary = selection_summary();
    return selection_mode_ == ModelingSelectionMode::Object ||
                   selection_mode_ == ModelingSelectionMode::Group
               ? summary.selected_object_count != 0u : summary.has_selection;
  }

  [[nodiscard]] const void *session_identity() const noexcept {
    return &session_;
  }

  /// The acknowledged candidate is the affine update represented by the packed
  /// position streams. Operation switches clear this marker before publication.
  [[nodiscard]] bool has_acknowledged_affine_transform_preview() const noexcept {
    return transient_transform_payload_active_ &&
           mesh_preview_host_baseline_.has_value() &&
           mesh_preview_host_baseline_->fence_acknowledged &&
           session_.preview_active(mesh_preview_host_baseline_->preview_id) &&
           mesh_preview_host_baseline_->affine_input_revision.has_value() &&
           mesh_preview_host_baseline_->affine_input_revision ==
               mesh_preview_host_baseline_->acknowledged_sdk.preview_content_revision;
  }

  [[nodiscard]] std::uint64_t selection_overlay_preview_signature() const noexcept {
    if (!plane_cut_preview_cache_.candidates.empty()) {
      return (plane_cut_preview_revision_ ^ 0x437574ULL) * 1099511628211ULL;
    }
    if (!mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->fence_acknowledged ||
        !session_.preview_active(mesh_preview_host_baseline_->preview_id)) {
      return 0U;
    }
    const auto &fence = mesh_preview_host_baseline_->acknowledged_sdk;
    if (fence.polygon_content_revision == 0U) {
      return 0U;
    }
    std::uint64_t signature = 1469598103934665603ULL;
    signature = (signature ^ fence.preview.index) * 1099511628211ULL;
    signature = (signature ^ fence.preview.generation) * 1099511628211ULL;
    // Affine component previews keep semantic topology at its committed base;
    // the independently revised packed streams move every retained primitive.
    // Retain the transaction identity so begin/end and replacement still refresh.
    const std::uint64_t semantic_revision =
        has_acknowledged_affine_transform_preview() ? 0U : fence.polygon_content_revision;
    return (signature ^ semantic_revision) * 1099511628211ULL;
  }

  /// Read the visible extrusion candidate without replacing its durable asset.
  [[nodiscard]] Result<PolygonDocument>
  query_polygon_document(ObjectId object,
                         const PolygonDocument *committed = nullptr) const {
    if (face_extrusion_preview_active() &&
        std::ranges::find(face_extrusion_preview_objects_, object,
                          &FaceExtrusionPreviewObject::object) !=
            face_extrusion_preview_objects_.end()) {
      return session_.preview_document_copy(session_.preview_summary().id, object);
    }
    return committed != nullptr
               ? Result<PolygonDocument>::success(*committed)
               : session_.document_copy(object);
  }

  [[nodiscard]] Result<ModelingPolygonDocumentReadSnapshot>
  polygon_document_read_snapshot(int modeling_object_id,
                                 bool include_preview) const {
    const auto object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return Result<ModelingPolygonDocumentReadSnapshot>::failure(
          modeling::make_error(modeling::ErrorCode::InvalidId,
                               "Modeling object identity is not live."));
    }
    if (include_preview) {
      if (const auto *candidate = plane_cut_candidate(modeling_object_id)) {
        return Result<ModelingPolygonDocumentReadSnapshot>::success({
            .object = *object,
            .document = modeling::polygon_document_read_snapshot(candidate->projected),
            .selection = candidate->projected.selection(),
            .preview = true,
        });
      }
    }
    const auto preview = session_.preview_summary();
    const auto participant = std::ranges::find(
        preview.objects, *object,
        &modeling::PreviewTransactionObjectSummary::object_id);
    const bool use_preview = include_preview && preview.active &&
                             participant != preview.objects.end();
    auto document = use_preview
        ? session_.preview_document_copy(preview.id, *object)
        : session_.document_copy(*object);
    if (!document.ok()) {
      return Result<ModelingPolygonDocumentReadSnapshot>::failure(
          document.error());
    }
    return Result<ModelingPolygonDocumentReadSnapshot>::success({
        .object = *object,
        .document = modeling::polygon_document_read_snapshot(document.value()),
        .selection = document.value().selection(),
        .preview = use_preview,
    });
  }

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const {
    if (const auto *candidate = plane_cut_candidate(modeling_object_id)) {
      return candidate->projected_mesh;
    }
    const auto extrusion = std::ranges::find(
        face_extrusion_preview_meshes_, modeling_object_id,
        &ModelingToolPreviewMesh::source_object_id);
    if (extrusion != face_extrusion_preview_meshes_.end()) {
      return extrusion->mesh;
    }
    const std::optional<EntityId> entity =
        object_map_.entity_for_modeling_object(modeling_object_id);
    const MeshComponent *binding =
        entity.has_value() && scene_.exists(*entity) ? scene_.mesh(*entity)
                                                     : nullptr;
    const MeshAsset *projected =
        binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
    if (projected != nullptr) {
      return *projected;
    }
    return std::nullopt;
  }

  [[nodiscard]] static std::array<QVec3, 3> local_cut_plane_points(
      const QMat4 &world_from_object, const QMat4 &object_from_world,
      const std::array<QVec3, 3> &world_points) {
    std::array<QVec3, 3> local_points{
        transform_point(object_from_world, world_points[0]),
        transform_point(object_from_world, world_points[1]),
        transform_point(object_from_world, world_points[2]),
    };
    // A reflected basis reverses the cross product of the transformed plane
    // tangents. Preserve the world-space signed half-space chosen by Cut.
    if (glm::determinant(glm::dmat3{to_glm(world_from_object)}) < 0.0) {
      std::swap(local_points[1], local_points[2]);
    }
    return local_points;
  }

  [[nodiscard]] Result<std::vector<ModelingSdkPlaneCutRequest>>
  plane_cut_requests(const quader_editor_core::CutToolState &state) const {
    using Requests = std::vector<ModelingSdkPlaneCutRequest>;
    const auto points = quader_editor_core::cut_plane_points(state);
    if (points.size() != 3U || state.target_object_ids.empty()) {
      return Result<Requests>::failure(modeling::make_error(
          modeling::ErrorCode::InvalidArgument, "Cut needs a plane and target meshes."));
    }
    Requests requests;
    requests.reserve(state.target_object_ids.size());
    for (const int native_id : state.target_object_ids) {
      const auto entity = object_map_.entity_for_modeling_object(native_id);
      const auto world = modeling_object_world_transform_resolver_
          ? modeling_object_world_transform_resolver_(native_id)
          : entity.has_value() && scene_.exists(*entity)
                ? std::optional<QMat4>{scene_.world_transform_matrix(*entity)}
                : std::nullopt;
      const auto local = world.has_value() ? try_scene_matrix_inverse(*world)
                                         : std::nullopt;
      if (!local.has_value()) {
        return Result<Requests>::failure(modeling::make_error(
            modeling::ErrorCode::InvalidArgument,
            "Cut target mesh has a non-invertible world transform."));
      }
      ModelingSdkPlaneCutRequest request{
          .modeling_object_id = native_id,
          .local_plane_points = local_cut_plane_points(
              *world, *local, {points[0], points[1], points[2]}),
          .scope = state.scope,
          .cap = state.cap,
      };
      for (const auto face : state.target_faces) {
        if (face.object_id == native_id) {
          request.faces.push_back(modeling::make_id<modeling::FaceTag>(face.face_index));
        }
      }
      for (const auto edge : state.target_edges) {
        if (edge.object_id == native_id) {
          request.edges.push_back(modeling::make_edge_key(
              modeling::make_id<modeling::VertexTag>(edge.a_index),
              modeling::make_id<modeling::VertexTag>(edge.b_index)));
        }
      }
      request.all_edges = std::ranges::find(state.target_edge_full_object_ids,
                                             native_id) !=
                          state.target_edge_full_object_ids.end();
      requests.push_back(std::move(request));
    }
    return Result<Requests>::success(std::move(requests));
  }

  [[nodiscard]] ModelingSdkPlaneCutPreviewResult rejected_plane_cut_preview(
      std::string message) const {
    auto rejected = plane_cut_preview_cache_.preview;
    rejected.command = failure(std::move(message));
    return rejected;
  }

  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      const quader_editor_core::CutToolState &state) const {
    const auto requests = plane_cut_requests(state);
    if (!requests.ok()) return rejected_plane_cut_preview(requests.error_message());
    const auto mode = state.mode == quader_editor_core::CutMode::KeepBack
        ? ModelingCutMode::DiscardLeft
        : state.mode == quader_editor_core::CutMode::KeepFront
              ? ModelingCutMode::DiscardRight : ModelingCutMode::KeepBoth;
    return preview_plane_cut(requests.value(), mode);
  }

  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points,
      ModelingCutMode mode) const {
    std::vector<ModelingSdkPlaneCutRequest> requests;
    requests.reserve(modeling_object_ids.size());
    for (const int native_id : modeling_object_ids) {
      const auto entity = object_map_.entity_for_modeling_object(native_id);
      const auto world = modeling_object_world_transform_resolver_
          ? modeling_object_world_transform_resolver_(native_id)
          : entity.has_value() && scene_.exists(*entity)
                ? std::optional<QMat4>{scene_.world_transform_matrix(*entity)}
                : std::nullopt;
      const auto local = world.has_value() ? try_scene_matrix_inverse(*world)
                                         : std::nullopt;
      if (!local.has_value()) {
        return rejected_plane_cut_preview("Cut target transform is not invertible.");
      }
      requests.push_back({.modeling_object_id = native_id,
          .local_plane_points = local_cut_plane_points(
              *world, *local, world_plane_points)});
    }
    return preview_plane_cut(requests, mode);
  }

  [[nodiscard]] bool matches_plane_cut_preview(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode) const {
    const auto &cache = plane_cut_preview_cache_;
    if (cache.requests.size() != requests.size() || !cache.preview.command.ok ||
        cache.mode != mode || cache.content_revision != session_.content_revision() ||
        cache.selection_revision != session_.selection_revision() ||
        cache.scene_revision != scene_.revision() ||
        cache.asset_revision != assets_.revision() ||
        cache.object_map_revision != object_map_.revision()) return false;
    for (std::size_t index = 0; index < requests.size(); ++index) {
      const auto &left = cache.requests[index];
      const auto &right = requests[index];
      if (left.modeling_object_id != right.modeling_object_id ||
          left.scope != right.scope || left.faces != right.faces ||
          left.edges != right.edges || left.all_edges != right.all_edges ||
          left.cap != right.cap) return false;
      for (std::size_t point = 0; point < 3U; ++point) {
        const auto a = left.local_plane_points[point];
        const auto b = right.local_plane_points[point];
        if (a.x != b.x || a.y != b.y || a.z != b.z) return false;
      }
    }
    return true;
  }

  [[nodiscard]] static Result<PolygonDocument> combined_cut_projection(
      const PolygonDocument &front, const PolygonDocument &back) {
    // Combine through the portable owner so the temporary read projection uses
    // the same topology/attribute handling as an authored object combination.
    modeling::ModelingSession projection;
    auto first = projection.add_document(front, "Cut front projection");
    if (!first.ok()) return Result<PolygonDocument>::failure(first.error());
    auto second = projection.add_document(back, "Cut back projection");
    if (!second.ok()) return Result<PolygonDocument>::failure(second.error());
    const std::array objects{first.value(), second.value()};
    const auto combined = projection.combine_objects(objects);
    if (!combined.ok()) return Result<PolygonDocument>::failure(combined.error());
    return projection.document_copy(first.value());
  }

  [[nodiscard]] static Result<std::vector<std::array<QVec3, 2>>>
  plane_cut_preview_segments(
      std::span<const EdgeKey> edges,
      std::span<const PolygonDocument *const> documents,
      const QMat4 &world_from_object) {
    using Segments = std::vector<std::array<QVec3, 2>>;
    if (edges.empty()) return Result<Segments>::success({});
    std::map<VertexId, QVec3> positions;
    for (const auto *document : documents) {
      if (document == nullptr) continue;
      const auto topology = document->authored_topology();
      if (!topology.ok()) return Result<Segments>::failure(topology.error());
      for (const auto &vertex : topology.value().vertices) {
        positions.try_emplace(vertex.id, QVec3{vertex.position.x,
            vertex.position.y, vertex.position.z});
      }
    }
    Segments segments;
    segments.reserve(edges.size());
    std::set<EdgeKey> included;
    for (const auto edge : edges) {
      if (!included.insert(modeling::make_edge_key(edge.a, edge.b)).second) continue;
      const auto first = positions.find(edge.a);
      const auto second = positions.find(edge.b);
      if (first == positions.end() || second == positions.end()) {
        return Result<Segments>::failure(modeling::make_error(
            modeling::ErrorCode::InternalError,
            "Cut preview edge is missing from its accepted geometry."));
      }
      const auto start = transform_point(world_from_object, first->second);
      const auto end = transform_point(world_from_object, second->second);
      if (length_squared(end - start) != 0.0F) segments.push_back({start, end});
    }
    return Result<Segments>::success(std::move(segments));
  }

  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode) const {
    using Scope = quader_editor_core::CutScope;
    if (requests.empty()) return rejected_plane_cut_preview("Cut needs a selected mesh.");
    if (matches_plane_cut_preview(requests, mode)) return plane_cut_preview_cache_.preview;
    PlaneCutPreviewCache prepared;
    prepared.requests.assign(requests.begin(), requests.end());
    prepared.mode = mode;
    prepared.content_revision = session_.content_revision();
    prepared.selection_revision = session_.selection_revision();
    prepared.scene_revision = scene_.revision();
    prepared.asset_revision = assets_.revision();
    prepared.object_map_revision = object_map_.revision();
    prepared.preview.target_count = static_cast<int>(requests.size());
    std::unordered_set<int> seen;
    for (const auto &request : requests) {
      const int native_id = request.modeling_object_id;
      const auto object = sdk_object(native_id);
      const auto entity = object_map_.entity_for_modeling_object(native_id);
      if (!object.has_value() || !entity.has_value() || !scene_.exists(*entity) ||
          !seen.insert(native_id).second) {
        return rejected_plane_cut_preview("Cut target mesh was not found or was duplicated.");
      }
      const auto world = modeling_object_world_transform_resolver_
          ? modeling_object_world_transform_resolver_(native_id)
          : std::optional<QMat4>{scene_.world_transform_matrix(*entity)};
      if (!world.has_value() || !try_scene_matrix_inverse(*world).has_value()) {
        return rejected_plane_cut_preview("Cut target transform is not invertible.");
      }
      const auto &points = request.local_plane_points;
      const auto normal = cross(points[1] - points[0], points[2] - points[0]);
      if (!finite(points[0]) || !finite(points[1]) || !finite(points[2]) ||
          !finite(normal) || length_squared(normal) <= 0.0000001F) {
        return rejected_plane_cut_preview("Cut plane is degenerate.");
      }
      auto source = session_.document_copy(*object);
      if (!source.ok()) return rejected_plane_cut_preview(source.error_message());
      PlaneCutCandidate candidate;
      candidate.native_id = native_id;
      candidate.object = *object;
      candidate.world_from_object = *world;
      candidate.dormant_selection = source.value().selection();
      const modeling::PlaneSplitSpec plane{.a = to_sdk(points[0]),
          .b = to_sdk(points[1]), .c = to_sdk(points[2]),
          .cap = request.cap && request.scope != Scope::Edge};
      std::optional<PolygonDocument> discarded;
      std::vector<EdgeKey> intersection_edges;
      if (request.scope == Scope::Object) {
        auto split = source.value().split_by_plane(plane);
        if (!split.ok()) {
          // A valid plane beyond the mesh is a complete keep or discard.
          // Reject other split failures instead of masking invalid topology.
          const auto topology = source.value().authored_topology();
          if (!topology.ok()) return rejected_plane_cut_preview(topology.error_message());
          bool front = false;
          bool back = false;
          const auto unit_normal = normalize_or_zero(normal);
          // Match the portable plane-side classifier when its tangent result
          // reaches this whole-object keep/discard fallback.
          constexpr float kPlaneSideTolerance = 0.0001F;
          for (const auto &vertex : topology.value().vertices) {
            const auto position = vertex.position;
            const float distance = dot(QVec3{position.x, position.y, position.z} - points[0], unit_normal);
            front = front || distance > kPlaneSideTolerance;
            back = back || distance < -kPlaneSideTolerance;
          }
          if (front == back) return rejected_plane_cut_preview(split.error_message());
          const bool keep_source = mode == ModelingCutMode::KeepBoth ||
              (front && mode == ModelingCutMode::DiscardRight) ||
              (back && mode == ModelingCutMode::DiscardLeft);
          candidate.kept = source.value();
          if (!keep_source) {
            discarded = source.value();
            auto selected = candidate.kept.select_all_faces();
            if (!selected.ok()) return rejected_plane_cut_preview(selected.error_message());
            auto removed = candidate.kept.delete_selection();
            if (!removed.ok()) return rejected_plane_cut_preview(removed.error_message());
            candidate.receipt = std::move(removed).value();
            ++prepared.preview.removed_count;
          }
        } else {
          candidate.receipt = split.value().receipt;
          intersection_edges = candidate.receipt.affected.edges;
          prepared.preview.split_count += split.value().split_count;
          candidate.kept = mode == ModelingCutMode::DiscardLeft
              ? std::move(split.value().back) : std::move(split.value().front);
          if (mode == ModelingCutMode::KeepBoth) {
            candidate.second = std::move(split.value().back);
          } else {
            discarded = mode == ModelingCutMode::DiscardLeft
                ? std::move(split.value().front) : std::move(split.value().back);
            prepared.preview.removed_count += std::max(1, split.value().removed_count);
          }
        }
      } else {
        auto selected = request.scope == Scope::Face
            ? source.value().select_faces(request.faces)
            : request.all_edges ? source.value().select_all_edges()
                                : source.value().select_edges(request.edges);
        if (!selected.ok()) return rejected_plane_cut_preview(selected.error_message());
        candidate.kept = source.value();
        const auto keep = mode == ModelingCutMode::DiscardLeft ? modeling::CutKeepMode::DiscardLeft
            : mode == ModelingCutMode::DiscardRight ? modeling::CutKeepMode::DiscardRight
                                                   : modeling::CutKeepMode::KeepBoth;
        auto cut = request.scope == Scope::Face
            ? candidate.kept.cut_selected_faces_by_plane(plane, keep)
            : candidate.kept.cut_selected_edges_by_plane(plane);
        if (!cut.ok()) return rejected_plane_cut_preview(cut.error_message());
        candidate.receipt = std::move(cut).value();
        if (request.scope == Scope::Face) intersection_edges = candidate.receipt.affected.edges;
        prepared.preview.split_count += static_cast<int>(candidate.receipt.created.vertices.size());
        prepared.preview.removed_count += static_cast<int>(candidate.receipt.deleted.faces.size());
        if (request.scope == Scope::Face && mode != ModelingCutMode::KeepBoth) {
          auto opposite = source.value();
          auto opposite_cut = opposite.cut_selected_faces_by_plane(plane,
              keep == modeling::CutKeepMode::DiscardRight ? modeling::CutKeepMode::DiscardLeft
                                                      : modeling::CutKeepMode::DiscardRight);
          if (!opposite_cut.ok()) return rejected_plane_cut_preview(opposite_cut.error_message());
          if (!opposite.selection().faces.empty()) {
            auto extracted = opposite.extract_selected_faces();
            if (!extracted.ok()) return rejected_plane_cut_preview(extracted.error_message());
            discarded = std::move(extracted.value().document);
          }
        }
        if (request.scope == Scope::Edge && !candidate.receipt.created.vertices.empty()) {
          const auto payload = candidate.kept.authored_payload();
          if (!payload.ok()) return rejected_plane_cut_preview(payload.error_message());
          for (std::size_t index = 0; index < payload.value().vertices.size(); ++index) {
            if (std::ranges::find(candidate.receipt.created.vertices, payload.value().vertices[index]) !=
                candidate.receipt.created.vertices.end()) {
              const auto point = payload.value().positions[index];
              prepared.preview.inserted_points.push_back(transform_point(*world, {point.x, point.y, point.z}));
            }
          }
        }
      }
      if (request.scope != Scope::Edge) {
        // The portable receipt identifies actual cut sections, including
        // existing plane-aligned boundaries. Never infer these from triangles.
        const std::array<const PolygonDocument *, 4> documents{
            &candidate.kept,
            candidate.second.has_value() ? &*candidate.second : nullptr,
            discarded.has_value() ? &*discarded : nullptr,
            &source.value(),
        };
        auto segments = plane_cut_preview_segments(
            intersection_edges, documents, *world);
        if (!segments.ok()) return rejected_plane_cut_preview(segments.error_message());
        prepared.preview.intersection_segments.insert(
            prepared.preview.intersection_segments.end(),
            segments.value().begin(), segments.value().end());
      }
      const std::array<const PolygonDocument *, 2> kept_documents{
          &candidate.kept,
          candidate.second.has_value() ? &*candidate.second : nullptr,
      };
      for (const auto *document : kept_documents) {
        if (document == nullptr) continue;
        const auto edges = document->edge_ids();
        const std::array<const PolygonDocument *, 1> documents{document};
        auto segments = plane_cut_preview_segments(edges, documents, *world);
        if (!segments.ok()) return rejected_plane_cut_preview(segments.error_message());
        prepared.preview.kept_segments.insert(
            prepared.preview.kept_segments.end(),
            segments.value().begin(), segments.value().end());
      }
      if (discarded.has_value()) {
        const auto edges = discarded->edge_ids();
        const std::array<const PolygonDocument *, 1> documents{&*discarded};
        auto segments = plane_cut_preview_segments(edges, documents, *world);
        if (!segments.ok()) return rejected_plane_cut_preview(segments.error_message());
        prepared.preview.discarded_segments.insert(
            prepared.preview.discarded_segments.end(),
            segments.value().begin(), segments.value().end());
      }
      const auto *material = scene_.material(*entity);
      const auto *binding = scene_.mesh(*entity);
      const auto append_mesh = [&](const PolygonDocument &document,
                                   std::vector<ModelingToolPreviewMesh> &meshes,
                                   bool suppress_source) {
        auto mesh = MeshProjectionService::compile_sdk_document_mesh(document);
        ++stats_.mesh_compile_count;
        if (!mesh.has_value()) return false;
        const bool empty = mesh->geometry().indices.empty();
        mesh->name = "Cut Preview";
        meshes.push_back({.source_object_id = native_id, .source_entity = *entity,
            .material = material != nullptr ? *material : MaterialComponent{},
            .mesh = std::move(*mesh), .transform = *world,
            .double_sided = binding != nullptr && binding->double_sided,
            .suppress_source = suppress_source,
            .suppress_source_only = suppress_source && empty});
        return true;
      };
      if (!append_mesh(candidate.kept, prepared.preview.kept_meshes, true) ||
          (candidate.second.has_value() &&
           !append_mesh(*candidate.second, prepared.preview.kept_meshes, true)) ||
          (discarded.has_value() &&
           !append_mesh(*discarded, prepared.preview.discarded_meshes, false))) {
        return rejected_plane_cut_preview("Cut candidate mesh could not be compiled.");
      }
      candidate.projected = candidate.kept;
      if (candidate.second.has_value()) {
        auto combined = combined_cut_projection(candidate.kept, *candidate.second);
        if (!combined.ok()) return rejected_plane_cut_preview(combined.error_message());
        candidate.projected = std::move(combined).value();
        auto mesh = MeshProjectionService::compile_sdk_document_mesh(candidate.projected);
        ++stats_.mesh_compile_count;
        if (!mesh.has_value()) return rejected_plane_cut_preview("Cut picking projection failed.");
        candidate.projected_mesh = std::move(*mesh);
      } else {
        candidate.projected_mesh = prepared.preview.kept_meshes.back().mesh;
      }
      prepared.candidates.push_back(std::move(candidate));
    }
    prepared.preview.command = {.ok = true, .changed = false};
    prepared.preview.revision = ++plane_cut_preview_revision_;
    plane_cut_preview_cache_ = std::move(prepared);
    return plane_cut_preview_cache_.preview;
  }

  [[nodiscard]] const PlaneCutCandidate *plane_cut_candidate(int native_id) const {
    if (plane_cut_preview_cache_.content_revision != session_.content_revision()) return nullptr;
    const auto found = std::ranges::find(plane_cut_preview_cache_.candidates,
                                       native_id, &PlaneCutCandidate::native_id);
    return found != plane_cut_preview_cache_.candidates.end() &&
                   sdk_object(native_id) == found->object
               ? &*found : nullptr;
  }

  [[nodiscard]] const ModelingSdkPlaneCutPreviewResult &plane_cut_preview() const noexcept {
    return plane_cut_preview_cache_.preview;
  }

  [[nodiscard]] std::uint64_t plane_cut_preview_revision() const noexcept {
    return plane_cut_preview_revision_;
  }

  void clear_plane_cut_preview() {
    if (plane_cut_preview_cache_.candidates.empty()) return;
    plane_cut_preview_cache_ = {};
    ++plane_cut_preview_revision_;
  }

  [[nodiscard]] ModelingSdkMirrorPreviewResult preview_mirror(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points) const {
    ModelingSdkMirrorPreviewResult preview;
    preview.target_count = static_cast<int>(modeling_object_ids.size());
    if (modeling_object_ids.empty()) {
      preview.command = failure("Mirror needs selected mesh objects.");
      return preview;
    }

    for (const int native_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (!object.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        preview.command = failure("Mirror target mesh was not found.");
        preview.meshes.clear();
        return preview;
      }

      const std::optional<QMat4> world =
          modeling_object_world_transform_resolver_
              ? modeling_object_world_transform_resolver_(native_id)
              : std::optional<QMat4>{scene_.world_transform_matrix(*entity)};
      const std::optional<QMat4> local_from_world =
          world.has_value() ? try_scene_matrix_inverse(*world) : std::nullopt;
      if (!world.has_value() || !local_from_world.has_value()) {
        preview.command =
            failure("Mirror target mesh has a non-invertible world transform.");
        preview.meshes.clear();
        return preview;
      }
      const QVec3 first =
          transform_point(*local_from_world, world_plane_points[0]);
      const QVec3 second =
          transform_point(*local_from_world, world_plane_points[1]);
      const QVec3 third =
          transform_point(*local_from_world, world_plane_points[2]);
      const QVec3 normal = cross(second - first, third - first);
      if (!finite(first) || !finite(normal) ||
          length_squared(normal) <= 0.0000001F) {
        preview.command = failure("Mirror plane is degenerate.");
        preview.meshes.clear();
        return preview;
      }

      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        preview.command = failure(copied.error_message());
        preview.meshes.clear();
        return preview;
      }
      Result<OperationResult> reflected = copied.value().reflect_across_plane(
          to_sdk(first), to_sdk(normal));
      if (!reflected.ok()) {
        preview.command = failure(reflected.error_message());
        preview.meshes.clear();
        return preview;
      }
      std::optional<MeshAsset> mesh =
          MeshProjectionService::compile_sdk_document_mesh(copied.value());
      ++stats_.mesh_compile_count;
      if (!mesh.has_value() || mesh->geometry().vertices.empty() ||
          mesh->geometry().indices.empty()) {
        preview.command = failure("Mirror preview failed.");
        preview.meshes.clear();
        return preview;
      }
      const MaterialComponent *material = scene_.material(*entity);
      const MeshComponent *source_mesh = scene_.mesh(*entity);
      mesh->name = "Mirror Preview";
      preview.meshes.push_back({
          .source_entity = *entity,
          .material = material != nullptr ? *material : MaterialComponent{},
          .mesh = std::move(*mesh),
          .transform = *world,
          .double_sided = source_mesh != nullptr && source_mesh->double_sided,
      });
    }

    preview.command = {.ok = true, .changed = false};
    return preview;
  }

  [[nodiscard]] ModelingSelectionProjectionSnapshot
  build_selection_projection_snapshot() const {
    QDR_PROFILE_SCOPE("Modeling SDK rebuild selection projection snapshot");
    ++stats_.native_selection_projection_build_count;
    std::vector<quader_editor_core::PolygonObject> objects;
    objects.reserve(sdk_bindings().size());
    const std::vector<quader::modeling::SessionObjectSummary> summaries =
        session_.objects();
    struct ProjectionObjectSource final {
      const modeling::SessionObjectSummary *summary = nullptr;
      std::size_t document_index = 0u;
    };
    std::unordered_map<std::uint64_t, ProjectionObjectSource> sources;
    sources.reserve(summaries.size());
    std::vector<ObjectId> committed_object_ids;
    committed_object_ids.reserve(summaries.size());
    for (const auto &summary : summaries) {
      if (summary.kind != modeling::SessionObjectKind::Mesh) continue;
      sources.emplace(sdk_object_key(summary.id), ProjectionObjectSource{
          .summary = &summary, .document_index = committed_object_ids.size()});
      committed_object_ids.push_back(summary.id);
    }
    // Both scalar summary matching and scalar SDK document reads scan the
    // complete workspace. Index once so one preview update stays linear in
    // the number of projected objects, while retaining the binding order.
    const auto committed_documents = session_.document_copies(committed_object_ids);
    const auto preview = session_.preview_summary();
    std::unordered_set<std::uint64_t> authored_preview_objects;
    if (preview.active && preview.operation_id == "translate_selection" &&
        mesh_preview_host_baseline_.has_value() &&
        mesh_preview_host_baseline_->active_for(preview.id) &&
        mesh_preview_host_baseline_->fence_acknowledged &&
        mesh_preview_host_baseline_->acknowledged_sdk.preview_content_revision ==
            preview.content_revision &&
        !has_acknowledged_affine_transform_preview()) {
      // Authored transform candidates can replace selected element identities.
      // Read their selection from the same acknowledged candidate as the mesh;
      // packed affine previews continue sharing committed semantic selection.
      const auto &participants =
          mesh_preview_host_baseline_->acknowledged_sdk.preview_objects;
      authored_preview_objects.reserve(participants.size());
      for (const auto &participant : participants) {
        if (participant.kind == modeling::SessionObjectKind::Mesh &&
            participant.action != modeling::SessionPreviewAction::RemoveExisting) {
          authored_preview_objects.insert(sdk_object_key(participant.object));
        }
      }
    }
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *mesh = binding != nullptr
                                  ? assets_.mesh(binding->mesh)
                                  : nullptr;
      const auto extrusion = std::ranges::find(
          face_extrusion_preview_meshes_, native_id,
          &ModelingToolPreviewMesh::source_object_id);
      if (extrusion != face_extrusion_preview_meshes_.end()) {
        mesh = &extrusion->mesh;
      }
      const auto *cut_candidate = plane_cut_candidate(native_id);
      if (cut_candidate != nullptr) mesh = &cut_candidate->projected_mesh;
      if (!entity.has_value() || !scene_.exists(*entity) || mesh == nullptr ||
          !mesh->has_authored_polygon_document ||
          !mesh->authored_polygon_document) {
        continue;
      }

      quader_editor_core::PolygonObject object;
      object.id = native_id;
      if (const ModelingObjectComponent *component =
              scene_.modeling_object(*entity)) {
        object.material_id = static_cast<int>(component->material_slot);
      }
      const auto source = sources.find(sdk_object_key(object_id));
      object.selected = editor_mode_service_ != nullptr &&
                                editor_mode_service_->mode() == EditorMode::Edit
                            ? edit_scope_contains(native_id)
                            : source != sources.end() && source->second.summary->selected;
      if (const auto *snapshot = mesh->authored_polygon_document.read_snapshot()) {
        object.document.share(*snapshot);
      } else {
        object.document.share(mesh->authored_polygon_document);
      }

      Result<PolygonDocument> document =
          cut_candidate != nullptr
              ? Result<PolygonDocument>::success(cut_candidate->projected)
              : extrusion != face_extrusion_preview_meshes_.end() ||
                  authored_preview_objects.contains(sdk_object_key(object_id))
              ? session_.preview_document_copy(preview.id, object_id)
              : committed_documents.ok() && source != sources.end()
                    ? Result<PolygonDocument>::success(
                          committed_documents.value()[source->second.document_index])
                    : session_.document_copy(object_id);
      if (document.ok() && (editor_mode_service_ == nullptr ||
                            edit_scope_contains(native_id))) {
        const PolygonSelectionSnapshot selected = document.value().selection();
        switch (selected.kind) {
        case SelectionKind::Vertex:
          object.selection.mode = quader_poly::SelectionMode::Vertex;
          break;
        case SelectionKind::Edge:
          object.selection.mode = quader_poly::SelectionMode::Edge;
          break;
        case SelectionKind::Face:
          object.selection.mode = quader_poly::SelectionMode::Face;
          break;
        case SelectionKind::Object:
          object.selection.mode = quader_poly::SelectionMode::Vertex;
          break;
        }
        for (const VertexId id : selected.vertices) {
          object.selection.vertices.push_back(id.index);
        }
        for (const EdgeKey edge : selected.edges) {
          object.selection.edges.push_back(
              quader_poly::make_edge(edge.a.index, edge.b.index));
        }
        for (const FaceId id : selected.faces) {
          object.selection.faces.push_back(id.index);
        }
        object.selection.has_active = selected.has_active;
        object.selection.active_vertex = selected.active_vertex.index;
        object.selection.active_edge = quader_poly::make_edge(
            selected.active_edge.a.index, selected.active_edge.b.index);
        object.selection.active_face = selected.active_face.index;
        if (selected.kind == SelectionKind::Edge) {
          object.selection.active_kind = quader_poly::ElementKind::Edge;
        } else if (selected.kind == SelectionKind::Face) {
          object.selection.active_kind = quader_poly::ElementKind::Face;
        } else {
          object.selection.active_kind = quader_poly::ElementKind::Vertex;
        }
      }
      objects.push_back(std::move(object));
    }

    quader_editor_core::SelectionMode projected_mode =
        quader_editor_core::SelectionMode::Object;
    switch (selection_mode_) {
    case ModelingSelectionMode::Object:
    case ModelingSelectionMode::Group:
      break;
    case ModelingSelectionMode::Vertex:
      projected_mode = quader_editor_core::SelectionMode::Vertex;
      break;
    case ModelingSelectionMode::Edge:
      projected_mode = quader_editor_core::SelectionMode::Edge;
      break;
    case ModelingSelectionMode::Face:
      projected_mode = quader_editor_core::SelectionMode::Face;
      break;
    }
    return ModelingSelectionProjectionSnapshot(
        std::move(objects), projected_mode, active_native_object_id_,
        content_revision() + plane_cut_preview_revision_, selection_revision());
  }

  [[nodiscard]] const ModelingSelectionProjectionSnapshot &
  selection_projection_snapshot() const {
    QDR_PROFILE_FUNCTION();
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    // Curve previews cannot change the polygon documents represented by this
    // mesh-only projection. Keep the retained view keyed to mesh owner state
    // while a curve candidate advances SDK and global asset revisions.
    // Ordinary affine previews also read committed authored/selection data here;
    // their candidate positions belong to the independently revised GPU streams.
    // Extrusion and other authored candidate projections retain their revision.
    const bool curve_preview_active =
        preview.active && preview.kind == modeling::SessionObjectKind::Curve;
    const SelectionProjectionSnapshotCacheKey cache_key{
        .persistent_content_revision = persistent_content_revision_,
        .session_content_revision =
            curve_preview_active ? 0U : session_.content_revision(),
        .session_selection_revision = session_.selection_revision(),
        .mode_revision = mode_revision_ +
            (editor_mode_service_ != nullptr ? editor_mode_service_->revision() : 0u),
        .scene_mesh_binding_revision = scene_.mesh_binding_revision(),
        .asset_revision = curve_preview_active
                              ? assets_.mesh_material_usage_revision()
                              : assets_.revision(),
        .object_map_revision = object_map_.revision(),
        .active_native_object_id = active_native_object_id_,
        .mode = selection_mode_,
        .sdk_preview_active = preview.active && !curve_preview_active,
        .preview_id = curve_preview_active ? PreviewId{} : preview.id,
        .preview_object_id = curve_preview_active ? ObjectId{} : preview.object_id,
        .preview_content_revision =
            curve_preview_active || has_acknowledged_affine_transform_preview()
                ? 0U : preview.content_revision,
        .plane_cut_revision = plane_cut_preview_revision_,
    };
    if (!selection_projection_snapshot_cache_key_.has_value() ||
        *selection_projection_snapshot_cache_key_ != cache_key ||
        !selection_projection_snapshot_cache_.has_value()) {
      selection_projection_snapshot_cache_ =
          build_selection_projection_snapshot();
      selection_projection_snapshot_cache_key_ = cache_key;
    }
    return *selection_projection_snapshot_cache_;
  }

  [[nodiscard]] std::optional<EntityId> pick_object_entity(
      const ModelingPickSelectRequest &request,
      std::optional<EntityId> competing_native) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    ModelingSelectionService selection;
    const std::optional<int> object = selection.pick_object_id(
        projected, scene_, object_map_, request);
    if (!object.has_value()) {
      return competing_native;
    }
    return object_map_.entity_for_modeling_object(*object);
  }

  [[nodiscard]] std::vector<EntityId> box_pick_object_entities(
      const ModelingBoxSelectRequest &request) const {
    QDR_PROFILE_SCOPE("Modeling SDK box pick objects");
    const ModelingSelectionProjectionSnapshot &projected = [&]()
        -> const ModelingSelectionProjectionSnapshot & {
      QDR_PROFILE_SCOPE("Box selection build SDK pick projection");
      return selection_projection_snapshot();
    }();
    ModelingSelectionService selection;
    const std::vector<int> object_ids = [&]() {
      QDR_PROFILE_SCOPE("Box selection test SDK object candidates");
      return selection.box_pick_object_ids(projected, scene_, object_map_,
                                           request);
    }();
    std::vector<EntityId> entities;
    {
      QDR_PROFILE_SCOPE("Box selection map SDK object hits");
      entities.reserve(object_ids.size());
      for (const int object_id : object_ids) {
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(object_id);
        if (entity.has_value()) {
          entities.push_back(*entity);
        }
      }
    }
    return entities;
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_box(const ModelingCreateBoxRequest &request,
             const CreationFinalizer &finalize) {
    if (!finite(request.bounds.min) || !finite(request.bounds.max)) {
      return {.ok = false,
              .changed = false,
              .message = "Box bounds must be finite."};
    }

    std::optional<TransformComponent> initial_scene_transform;
    if (request.initial_scene_transform.has_value()) {
      initial_scene_transform = scene_transform_from_modeling_transform(
          *request.initial_scene_transform);
      if (!initial_scene_transform.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Initial box scene transform is invalid."};
      }
    }

    const int native_object_id = next_available_native_object_id();
    if (native_object_id == kInvalidModelingObjectId ||
        native_object_id == std::numeric_limits<int>::max()) {
      return {.ok = false,
              .changed = false,
              .message = "Native modeling object IDs are exhausted."};
    }
    std::vector<EntityId> affected_entities;
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    const std::array affected_modeling_ids{native_object_id};
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return {.ok = false,
              .changed = false,
              .message = "SDK box publication checkpoint is unavailable."};
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(
          publication,
          ModelingCreateBoxResult{
              .ok = false,
              .changed = false,
              .message = std::move(message),
          });
    };

    Result<PolygonDocument> document = PolygonDocument::make_box({
        .min = to_sdk(request.bounds.min),
        .max = to_sdk(request.bounds.max),
    });
    if (!document.ok()) {
      return rollback(document.error_message());
    }
    const std::string name = request.name.empty() ? "Box" : request.name;
    Result<ObjectId> added =
        session_.add_document(std::move(document).value(), name);
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    Result<OperationResult> selected = session_.select_object(added.value());
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK box mutation.");
    }

    Result<PolygonDocument> committed = session_.document_copy(added.value());
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(committed.value());
    if (!projected.has_value()) {
      return rollback("SDK box mesh projection failed.");
    }
    projected->name = name;
    const AssetRegistryValidationResult validation =
        assets_.validate_mesh(*projected);
    if (!validation.ok) {
      return rollback(validation.message.empty()
                          ? "SDK box mesh projection is invalid."
                          : validation.message);
    }
    const AssetId mesh_id =
        mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
    if (mesh_id == kInvalidAssetId) {
      return rollback("SDK box mesh asset could not be installed.");
    }
    publication.track_created_asset(mesh_id);
    ++stats_.asset_publication_count;
    if (fails(ModelingSdkProjectionFailurePoint::AfterAssetInstall)) {
      return rollback("Injected failure after SDK mesh asset publication.");
    }

    const EntityId entity =
        mesh_sync_.create_projected_mesh_entity(scene_, name);
    if (entity == kInvalidEntityId) {
      return rollback("SDK box scene entity could not be created.");
    }
    publication.track_created_entity(entity);
    if (fails(ModelingSdkProjectionFailurePoint::AfterEntityCreate)) {
      return rollback("Injected failure after SDK scene entity creation.");
    }

    if (initial_scene_transform.has_value() &&
        !mesh_sync_.set_projected_transform(scene_, entity,
                                            *initial_scene_transform)) {
      return rollback("SDK box initial scene transform could not be stored.");
    }
    if (!mesh_sync_.set_projected_texture_mapping(
            scene_, entity,
            {.sampling_from_local = scene_.world_transform_matrix(entity)})) {
      return rollback("SDK box initial world texture mapping could not be stored.");
    }
    if (!mesh_sync_.bind_projected_object(
            scene_, entity, native_object_id, 0u, true, mesh_id)) {
      return rollback("SDK box scene binding could not be installed.");
    }
    if (request.material.valid()) {
      const MaterialComponent material{.material = request.material};
      if (!mesh_sync_.set_projected_material(scene_, entity, &material)) {
        return rollback("SDK box material binding could not be installed.");
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSceneBinding)) {
      return rollback("Injected failure after SDK scene binding publication.");
    }

    if (!object_map_.bind_sdk_object(native_object_id, added.value()) ||
        !object_map_.bind(native_object_id, entity)) {
      return rollback("SDK box identity binding could not be installed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    scene_.clear_selection();
    if (!scene_.set_selected(entity, true)) {
      return rollback("SDK box scene selection could not be updated.");
    }
    active_native_object_id_ = native_object_id;
    selection_mode_ = ModelingSelectionMode::Object;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSelection)) {
      return rollback("Injected failure after SDK selection publication.");
    }

    ModelingObjectRecord record{
        .modeling_object_id = native_object_id,
        .entity = entity,
        .mesh = mesh_id,
        .name = name,
    };
    if (finalize && !finalize(record)) {
      return rollback("SDK box host finalization failed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK projection commit.");
    }

    next_native_object_id_ = native_object_id + 1;
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    return commit_publication_result<ModelingCreateBoxResult>(
        publication,
        {.ok = true,
         .changed = true,
         .message = "Created box.",
         .object = std::move(record)});
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_mesh(const ModelingCreateMeshRequest &request,
              const CreationFinalizer &finalize) {
    if (request.document.vertices.empty() || request.document.faces.empty()) {
      return {.ok = false,
              .changed = false,
              .message = "Document mesh must contain vertices and faces."};
    }

    Result<PolygonDocument> document =
        MeshProjectionService::sdk_document_from_polygon_document(
            request.document);
    if (!document.ok()) {
      return {.ok = false,
              .changed = false,
              .message = document.error_message()};
    }
    return create_document(std::move(document).value(), request.name,
                           request.material_slot, request.material,
                           request.initial_scene_transform,
                           request.world_texture_projection, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult create_mesh_document(
      modeling::PolygonDocument document, std::string_view name,
      MaterialReference material, const CreationFinalizer &finalize) {
    if (preview_active()) {
      return {.ok = false,
              .changed = false,
              .message = "Commit or cancel the active preview first."};
    }
    if (document.vertex_count() == 0U || document.face_count() == 0U) {
      return {.ok = false,
              .changed = false,
              .message = "Document mesh must contain vertices and faces."};
    }
    const Result<OperationResult> validated = document.validate();
    if (!validated.ok()) {
      return {.ok = false,
              .changed = false,
              .message = validated.error_message()};
    }
    return create_document(std::move(document), name, 0U, material,
                           std::nullopt, false, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult create_box_from_corners(
      std::span<const QVec3> corners, std::string_view requested_name,
      MaterialReference material, const CreationFinalizer &finalize) {
    if (corners.size() != 8U) {
      return {.ok = false,
              .changed = false,
              .message = "Box needs eight corner points."};
    }
    QVec3 center{};
    for (const QVec3 corner : corners) {
      if (!finite(corner)) {
        return {.ok = false,
                .changed = false,
                .message = "Box corners must be finite."};
      }
      center += corner;
    }
    center = center / static_cast<float>(corners.size());
    const std::optional<CreationFrame> frame = creation_frame_from_xy(
        center, corners[3] - corners[0], corners[4] - corners[0]);
    if (!frame.has_value()) {
      return {.ok = false,
              .changed = false,
              .message = "Box needs finite width, height, and depth."};
    }

    std::vector<Vec3> sdk_corners;
    sdk_corners.reserve(corners.size());
    for (const QVec3 corner : corners) {
      sdk_corners.push_back(to_sdk(frame->local_point(corner)));
    }
    Result<PolygonDocument> document =
        PolygonDocument::make_box_from_corners(sdk_corners);
    if (!document.ok()) {
      return {.ok = false,
              .changed = false,
              .message = document.error_message()};
    }
    return create_document(
        std::move(document).value(),
        requested_name.empty() ? std::string_view{"Box"} : requested_name,
        0U, material, frame->transform, true, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_face(std::span<const QVec3> points, QVec3 requested_normal,
              std::string_view requested_name, MaterialReference material,
              const CreationFinalizer &finalize) {
    constexpr float kEpsilonSquared = 0.00000001F;
    std::vector<QVec3> ordered_points;
    ordered_points.reserve(points.size());
    QVec3 polygon_normal{};
    QVec3 center{};
    for (std::size_t index = 0; index < points.size(); ++index) {
      const QVec3 point = points[index];
      if (!finite(point)) {
        return {.ok = false,
                .message = "Poly vertices must be finite."};
      }
      const QVec3 next = points[(index + 1U) % points.size()];
      polygon_normal = polygon_normal + cross(point, next);
      center += point;
      ordered_points.push_back(point);
    }
    if (ordered_points.size() < 3U ||
        length_squared(polygon_normal) <= kEpsilonSquared) {
      return {.ok = false,
              .message = "Poly needs a non-degenerate polygon."};
    }
    if (length_squared(requested_normal) > kEpsilonSquared &&
        dot(polygon_normal, requested_normal) < 0.0F) {
      std::reverse(ordered_points.begin(), ordered_points.end());
      polygon_normal = polygon_normal * -1.0F;
    }
    center = center / static_cast<float>(ordered_points.size());

    QVec3 x_hint{};
    for (std::size_t index = 0; index < ordered_points.size(); ++index) {
      const QVec3 edge =
          ordered_points[(index + 1U) % ordered_points.size()] -
          ordered_points[index];
      if (length_squared(edge) > kCreationFrameEpsilonSquared) {
        x_hint = edge;
        break;
      }
    }
    const std::optional<CreationFrame> frame =
        creation_frame_from_xz(center, x_hint, polygon_normal);
    if (!frame.has_value()) {
      return {.ok = false,
              .message = "Poly needs a non-degenerate local frame."};
    }

    std::vector<Vec3> sdk_points;
    sdk_points.reserve(ordered_points.size());
    for (const QVec3 point : ordered_points) {
      sdk_points.push_back(to_sdk(frame->local_point(point)));
    }
    Result<PolygonDocument> document = PolygonDocument::make_face(sdk_points);
    if (!document.ok()) {
      return {.ok = false,
              .message = document.error_message().empty()
                             ? "Could not create the poly face."
                             : document.error_message()};
    }
    return create_document(std::move(document).value(), requested_name, 0U,
                           material, frame->transform, true, finalize);
  }

  template <typename Document>
  [[nodiscard]] ModelingCreateBoxResult
  create_document(Document document, std::string_view requested_name,
                  std::uint32_t material_slot, MaterialReference material,
                  const std::optional<ModelingTransform3> &initial_transform,
                  bool world_texture_projection,
                  const CreationFinalizer &finalize) {
    constexpr bool kCurve =
        std::is_same_v<Document, modeling::CurveDocument>;

    std::optional<TransformComponent> initial_scene_transform;
    if (initial_transform.has_value()) {
      initial_scene_transform =
          scene_transform_from_modeling_transform(*initial_transform);
      if (!initial_scene_transform.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Initial mesh scene transform is invalid."};
      }
    }

    const int native_object_id = next_available_native_object_id();
    if (native_object_id == kInvalidModelingObjectId ||
        native_object_id == std::numeric_limits<int>::max()) {
      return {.ok = false,
              .changed = false,
              .message = "Native modeling object IDs are exhausted."};
    }
    std::vector<EntityId> affected_entities;
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    const std::array affected_modeling_ids{native_object_id};
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return {.ok = false,
              .changed = false,
              .message = "SDK object publication checkpoint is unavailable."};
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(
          publication,
          ModelingCreateBoxResult{
              .ok = false,
              .changed = false,
              .message = std::move(message),
          });
    };

    const std::string name = requested_name.empty()
                                 ? (kCurve ? "Curve" : "Mesh")
                                 : std::string(requested_name);
    Result<ObjectId> added = [&]() {
      if constexpr (kCurve) {
        return session_.add_curve_document(std::move(document), name);
      } else {
        return session_.add_document(std::move(document), name);
      }
    }();
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    Result<OperationResult> selected = session_.select_object(added.value());
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK mesh mutation.");
    }

    std::string projection_error;
    const AssetId mesh_id = [&]() -> AssetId {
      if constexpr (kCurve) {
        const auto committed = session_.curve_document_snapshot(added.value());
        if (!committed.ok()) {
          projection_error = committed.error_message();
          return kInvalidAssetId;
        }
        CurveAsset projected;
        projected.name = name;
        projected.document = committed.value();
        return mesh_sync_.install_projected_curve(assets_, std::move(projected));
      } else {
        const auto committed = session_.document_copy(added.value());
        if (!committed.ok()) {
          projection_error = committed.error_message();
          return kInvalidAssetId;
        }
        ++stats_.mesh_compile_count;
        auto projected =
            MeshProjectionService::compile_sdk_document_mesh(committed.value());
        if (!projected.has_value()) {
          projection_error = "SDK mesh projection failed.";
          return kInvalidAssetId;
        }
        projected->name = name;
        const auto validation = assets_.validate_mesh(*projected);
        if (!validation.ok) {
          projection_error = validation.message.empty()
                                 ? "SDK mesh projection is invalid."
                                 : validation.message;
          return kInvalidAssetId;
        }
        return mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
      }
    }();
    if (mesh_id == kInvalidAssetId) {
      return rollback(projection_error.empty()
                          ? (kCurve ? "SDK curve asset could not be installed."
                                    : "SDK mesh asset could not be installed.")
                          : std::move(projection_error));
    }
    publication.track_created_asset(mesh_id);
    ++stats_.asset_publication_count;
    if (fails(ModelingSdkProjectionFailurePoint::AfterAssetInstall)) {
      return rollback("Injected failure after SDK mesh asset publication.");
    }

    const EntityId entity = kCurve
        ? mesh_sync_.create_projected_curve_entity(scene_, name)
        : mesh_sync_.create_projected_mesh_entity(scene_, name);
    if (entity == kInvalidEntityId) {
      return rollback("SDK mesh scene entity could not be created.");
    }
    publication.track_created_entity(entity);
    if (fails(ModelingSdkProjectionFailurePoint::AfterEntityCreate)) {
      return rollback("Injected failure after SDK scene entity creation.");
    }

    if (initial_scene_transform.has_value() &&
        !mesh_sync_.set_projected_transform(scene_, entity,
                                            *initial_scene_transform)) {
      return rollback("SDK mesh initial scene transform could not be stored.");
    }
    if (world_texture_projection &&
        !mesh_sync_.set_projected_texture_mapping(
            scene_, entity,
            {.sampling_from_local = scene_.world_transform_matrix(entity)})) {
      return rollback("SDK mesh initial world texture mapping could not be stored.");
    }
    const bool bound = kCurve
        ? mesh_sync_.bind_projected_curve_object(
              scene_, entity, native_object_id, mesh_id)
        : mesh_sync_.bind_projected_object(
              scene_, entity, native_object_id, material_slot, true, mesh_id);
    if (!bound) {
      return rollback("SDK mesh scene binding could not be installed.");
    }
    if (material.valid()) {
      const MaterialComponent component{.material = material};
      if (!mesh_sync_.set_projected_material(scene_, entity, &component)) {
        return rollback("SDK mesh material binding could not be installed.");
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSceneBinding)) {
      return rollback("Injected failure after SDK scene binding publication.");
    }

    if (!object_map_.bind_sdk_object(native_object_id, added.value()) ||
        !object_map_.bind(native_object_id, entity)) {
      return rollback("SDK mesh identity binding could not be installed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    scene_.clear_selection();
    if (!scene_.set_selected(entity, true)) {
      return rollback("SDK mesh scene selection could not be updated.");
    }
    active_native_object_id_ = native_object_id;
    selection_mode_ = ModelingSelectionMode::Object;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSelection)) {
      return rollback("Injected failure after SDK selection publication.");
    }

    ModelingObjectRecord record{
        .modeling_object_id = native_object_id,
        .entity = entity,
        .mesh = kCurve ? kInvalidAssetId : mesh_id,
        .name = name,
    };
    if (finalize && !finalize(record)) {
      return rollback("SDK mesh host finalization failed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK projection commit.");
    }

    next_native_object_id_ = native_object_id + 1;
    (void)session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    return commit_publication_result<ModelingCreateBoxResult>(
        publication,
        {.ok = true,
         .changed = true,
         .message = kCurve ? "Created curve." : "Created mesh.",
         .object = std::move(record)});
  }

  [[nodiscard]] ModelingCreateBoxResult create_curve(
      modeling::CurveDocument document, std::string_view name,
      const CreationFinalizer &finalize) {
    if (preview_active()) {
      return {.ok = false,
              .message = "Commit or cancel the active preview first."};
    }
    const auto validated = document.validate();
    if (!validated.ok() || document.point_count() < 2U) {
      return {.ok = false,
              .message = validated.ok() ? "A curve needs at least two points."
                                        : validated.error_message()};
    }
    return create_document(std::move(document), name, 0U, {}, std::nullopt,
                           false, finalize);
  }

  [[nodiscard]] std::optional<modeling::CurveDocument> curve_document(
      int modeling_object_id, bool include_preview) const {
    const auto object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return std::nullopt;
    }
    const auto preview = session_.preview_summary();
    auto document = include_preview && preview.active &&
                            preview.kind == modeling::SessionObjectKind::Curve &&
                            preview.object_id == *object
                        ? session_.preview_curve_document_copy(preview.id)
                        : session_.curve_document_copy(*object);
    return document.ok()
               ? std::optional<modeling::CurveDocument>{std::move(document).value()}
               : std::nullopt;
  }

  [[nodiscard]] ModelingCommandResult replace_curve_document(
      int modeling_object_id, modeling::CurveDocument document) {
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const auto object = sdk_object(modeling_object_id);
    const auto current = curve_document(modeling_object_id, false);
    if (!object.has_value() || !current.has_value()) {
      return failure("Curve object does not exist.");
    }
    const auto validated = document.validate();
    if (!validated.ok() || document.point_count() < 2U) {
      return failure(validated.ok() ? "A curve needs at least two points."
                                    : validated.error_message());
    }
    if (document.has_same_geometry(*current)) {
      return {.ok = true, .message = "Curve unchanged."};
    }
    const auto native = native_object(*object);
    const auto entity = native.has_value()
                            ? object_map_.entity_for_modeling_object(*native)
                            : std::nullopt;
    if (!native.has_value() || !entity.has_value() ||
        !scene_.exists(*entity)) {
      return failure("Curve object has no live host identity.");
    }
    const std::array<EntityId, 1> affected_entity{
        *entity};
    const std::array<int, 1> affected_modeling_object{*native};
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::CurveProjection,
         .capture_hierarchy = false,
         .affected_scene_entities = affected_entity,
         .affected_modeling_object_ids = affected_modeling_object});
    if (!publication.ready()) {
      return failure("Curve publication checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    auto committed = session_.commit_curve_document(
        *object, std::move(document),
        {.changed = true,
         .dirty = {.topology = true, .geometry = true, .overlays = true}});
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_committed_object(publication, *object) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Curve projection could not be published.");
    }
    ++persistent_content_revision_;
    return commit_publication_result(publication,
                                     from_sdk(committed, "Edited curve."));
  }

  [[nodiscard]] bool curve_document_preview_active() const {
    const auto summary = session_.preview_summary();
    return summary.active && summary.kind == modeling::SessionObjectKind::Curve;
  }

  [[nodiscard]] int curve_document_preview_object_id() const {
    const auto summary = session_.preview_summary();
    return summary.active && summary.kind == modeling::SessionObjectKind::Curve
               ? native_object(summary.object_id).value_or(kInvalidModelingObjectId)
               : kInvalidModelingObjectId;
  }

  [[nodiscard]] ModelingCommandResult
  begin_curve_document_preview(int modeling_object_id) {
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const auto object = sdk_object(modeling_object_id);
    auto document = curve_document(modeling_object_id, false);
    if (!object.has_value() || !document.has_value()) {
      return failure("Curve object does not exist.");
    }
    if (!capture_mesh_preview_host_baseline(
            std::span<const ObjectId>{&*object, 1u}, {}, std::nullopt,
            SceneModelingProjectionDomain::CurveBindings)) {
      return failure("Bounded Curve preview baseline is unavailable.");
    }

    auto begun = session_.begin_curve_document_preview(
        *object, "edit_curve", std::move(*document), {});
    if (!begun.ok()) {
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    if (!activate_mesh_preview_host_baseline(begun.value()) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return abort_preview_publication(begun.value(),
                                       "Curve preview could not begin.");
    }

    preview_changed_ = false;
    active_preview_operation_id_ = "edit_curve";
    curve_preview_scene_revision_ = scene_.revision();
    curve_preview_asset_revision_ = assets_.revision();
    curve_preview_external_changes_ = false;
    const auto entity =
        object_map_.entity_for_modeling_object(modeling_object_id);
    curve_preview_asset_id_ =
        entity.has_value() && scene_.curve(*entity) != nullptr
            ? scene_.curve(*entity)->curve
            : kInvalidAssetId;
    refresh_curve_preview_sharing_proof(
        entity.value_or(kInvalidEntityId), curve_preview_asset_id_);
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          begun.value(), "Curve preview baseline could not be acknowledged.");
    }
    return {
        .ok = true,
        .changed = true,
        .message = "Curve preview started.",
    };
  }

  [[nodiscard]] ModelingCommandResult
  update_curve_document_preview(modeling::CurveDocument document) {
    if (!curve_document_preview_active()) {
      return failure("No curve preview is active.");
    }
    const auto validated = document.validate();
    if (!validated.ok() || document.point_count() < 2U) {
      return failure(validated.ok() ? "A curve needs at least two points."
                                    : validated.error_message());
    }
    const auto preview = session_.preview_summary();
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Curve preview participants changed outside this preview.");
    }
    const auto current = session_.preview_curve_document_copy(preview.id);
    if (!current.ok()) {
      return failure(current.error_message());
    }
    if (document.has_same_geometry(current.value())) {
      return {.ok = true, .message = "Curve preview unchanged."};
    }
    const auto base = session_.preview_base_curve_document_copy(
        preview.id, preview.object_id);
    if (!base.ok()) {
      return failure(base.error_message());
    }
    const bool changed_from_base = !document.has_same_geometry(base.value());

    ++stats_.curve_preview_checkpoint_capture_count;
    stats_.curve_preview_checkpoint_curve_asset_count +=
        mesh_preview_host_baseline_->scope.asset_ids.size();

    auto updated = session_.update_curve_document_preview(
        preview.id, std::move(document),
        {.changed = changed_from_base,
         .dirty = {.topology = true, .geometry = true, .overlays = true}});
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_preview_object()) {
      return abort_preview_publication(
          preview.id, "Curve preview projection could not be published.");
    }

    preview_changed_ = changed_from_base;
    curve_preview_scene_revision_ = scene_.revision();
    curve_preview_asset_revision_ = assets_.revision();
    if (const auto native = native_object(preview.object_id)) {
      const auto entity = object_map_.entity_for_modeling_object(*native);
      if (entity.has_value() && scene_.curve(*entity) != nullptr) {
        curve_preview_asset_id_ = scene_.curve(*entity)->curve;
      }
    }
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "Curve preview publication could not be acknowledged.");
    }
    return {
        .ok = true,
        .changed = updated.value().changed,
        .message = "Curve preview updated.",
    };
  }
  [[nodiscard]] ModelingCommandResult
  replace_mesh_document(int modeling_object_id,
                        const quader_poly::Document &document,
                        bool preserve_selection) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return failure("Modeling object does not exist.");
    }
    Result<PolygonDocument> converted =
        reconciled_sdk_document(*object, document);
    if (!converted.ok()) {
      return failure(converted.error_message());
    }
    PolygonDocument replacement = std::move(converted).value();
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents_by_object(
        objects,
        [replacement = std::move(replacement), preserve_selection](
            ObjectId, PolygonDocument &working) mutable -> Result<OperationResult> {
          if (preserve_selection) {
            // A canceled resize changes dimensions, not topology. Retain the
            // current component domain and active element if the user changed
            // selection before the gesture was retired. Invalid IDs reject the
            // replacement inside the existing publication transaction.
            Result<OperationResult> retained =
                replacement.apply_selection(working.selection());
            if (!retained.ok()) {
              return retained;
            }
          }
          working = replacement;
          OperationResult receipt;
          receipt.success = true;
          receipt.changed = true;
          receipt.message = "Replaced mesh document.";
          receipt.dirty.topology = true;
          receipt.dirty.geometry = true;
          receipt.dirty.materials = true;
          receipt.dirty.selection = !preserve_selection;
          receipt.dirty.overlays = true;
          return Result<OperationResult>::success(std::move(receipt));
        },
        "Modeling mesh document could not be replaced.", false, false, false,
        preserve_selection, preserve_selection);
  }

  [[nodiscard]] ModelingFaceExtrusionSource::Impl::Fence
  face_extrusion_fence() const {
    return {.content = session_.content_revision(),
            .selection = selection_revision(),
            .mesh_bindings = scene_.mesh_binding_revision(),
            .assets = assets_.revision(),
            .object_map = object_map_.revision(),
            .transforms = scene_.transform_revision(),
            .scope = instance_edit_scope_provider_
                         ? instance_edit_scope_provider_()
                         : ModelingInstanceEditScope{}};
  }

  [[nodiscard]] ModelingFaceExtrusionSourceResult
  capture_face_extrusion_source(bool include_extrusion_preview = false) const {
    if (preview_active() &&
        !(include_extrusion_preview && face_extrusion_preview_active())) {
      return {.message = "Commit or cancel the active preview first."};
    }
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return {.ok = true};
    }
    const std::vector<ObjectId> selected = selected_component_objects();
    if (selected.empty()) {
      return {.ok = true};
    }
    auto documents = session_.document_copies(selected);
    if (!documents.ok()) {
      return {.message = documents.error_message()};
    }
    auto source = std::make_shared<ModelingFaceExtrusionSource::Impl>();
    source->owner = this;
    source->generation = face_extrusion_generation_;
    source->fence = face_extrusion_fence();
    ModelingFaceExtrusionSourceResult result;
    std::size_t vertex_count = 0;
    QVec3 summed_normal{};
    QVec3 fallback_normal{};
    for (std::size_t index = 0; index < selected.size(); ++index) {
      PolygonDocument &document = documents.value()[index];
      if (include_extrusion_preview && face_extrusion_preview_active()) {
        auto current = session_.preview_document_copy(
            session_.preview_summary().id, selected[index]);
        if (!current.ok()) {
          return {.message = current.error_message()};
        }
        document = std::move(current).value();
      }
      if (document.selection().faces.empty()) {
        continue;
      }
      const auto native = native_object(selected[index]);
      const auto entity = native.has_value()
                              ? object_map_.entity_for_modeling_object(*native)
                              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity) || scene_.mesh(*entity) == nullptr ||
          !scene_.is_effectively_selectable(*entity)) {
        return {.message = "A selected extrusion mesh is not editable."};
      }
      const auto world = modeling_object_world_transform_resolver_
                             ? modeling_object_world_transform_resolver_(*native)
                             : std::optional{scene_.world_transform_matrix(*entity)};
      if (!world.has_value()) {
        return {.message = "An extrusion occurrence transform is unavailable."};
      }
      const auto frame = document.selected_face_extrude_frame(to_sdk(*world));
      if (!frame.ok()) {
        return {.message = frame.error_message()};
      }
      const auto &value = frame.value();
      result.center_world += QVec3{value.center.x, value.center.y, value.center.z} *
                             static_cast<float>(value.vertex_count);
      vertex_count += value.vertex_count;
      result.face_count += value.face_count;
      summed_normal += QVec3{value.summed_normal.x, value.summed_normal.y,
                            value.summed_normal.z};
      if (source->objects.empty() || *native == active_native_object_id_) {
        fallback_normal = {value.active_normal.x, value.active_normal.y,
                           value.active_normal.z};
      }
      source->objects.push_back({.object = selected[index],
                                  .native_id = *native,
                                  .entity = *entity,
                                  .world_from_local = to_sdk(*world),
                                  .document = std::move(document)});
    }
    if (result.face_count == 0) {
      return {.ok = true};
    }
    const float normal_length = length(summed_normal);
    result.direction_world = normal_length > 1.0e-6f
                                 ? summed_normal / normal_length
                                 : fallback_normal;
    result.handle_direction_world = normal_length > 1.0e-6f
                                        ? result.direction_world
                                        : QVec3{0.0f, 1.0f, 0.0f};
    result.center_world = result.center_world / static_cast<float>(vertex_count);
    source->direction_world = result.direction_world;
    result.source = ModelingFaceExtrusionSource{std::move(source)};
    result.ok = true;
    return result;
  }

  [[nodiscard]] bool face_extrusion_source_current(
      const ModelingFaceExtrusionSource &source) const {
    return source.valid() && source.impl_->owner == this &&
           source.impl_->generation == face_extrusion_generation_ &&
           source.impl_->fence == face_extrusion_fence();
  }

  [[nodiscard]] bool face_extrusion_source_live(
      const ModelingFaceExtrusionSource &source) const {
    if (!face_extrusion_source_current(source)) {
      return false;
    }
    for (const auto &entry : source.impl_->objects) {
      if (sdk_object(entry.native_id) != std::optional{entry.object} ||
          object_map_.entity_for_modeling_object(entry.native_id) !=
              std::optional{entry.entity} ||
          !scene_.exists(entry.entity) || scene_.mesh(entry.entity) == nullptr ||
          assets_.mesh(scene_.mesh(entry.entity)->mesh) == nullptr ||
          !scene_.is_effectively_selectable(entry.entity)) {
        return false;
      }
      const auto world = modeling_object_world_transform_resolver_
                             ? modeling_object_world_transform_resolver_(entry.native_id)
                             : std::optional{scene_.world_transform_matrix(entry.entity)};
      if (!world.has_value() ||
          !same_transform_request(entry.world_from_local, to_sdk(*world))) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] Result<OperationResult> prepare_face_extrusion_document(
      const ModelingFaceExtrusionSource::Impl::Object &entry,
      PolygonDocument &document, modeling::FaceExtrudeMode mode,
      float amount_internal, QVec3 world_direction) const {
    PolygonDocument candidate = entry.document;
    // Existing source cap faces keep their IDs. Newly regenerated walls and
    // vertices must not alias IDs retired by a previous extrusion candidate.
    candidate.preserve_identity_high_water(document);
    auto result = candidate.extrude_selected_faces({
        .mode = mode,
        .amount = amount_internal,
        .region_direction = to_sdk(world_direction),
        .extrusion_from_local = entry.world_from_local});
    if (!result.ok() || !result.value().success) {
      return result;
    }
    auto &receipt = result.value();
    receipt.changed = !candidate.has_same_geometry(document) ||
                      !same_polygon_selection(candidate.selection(),
                                              document.selection());
    if (receipt.changed) {
      receipt.dirty = {.topology = true, .geometry = true, .selection = true,
                       .materials = true, .overlays = true};
    }
    document = std::move(candidate);
    return result;
  }

  [[nodiscard]] bool same_face_extrusion_request(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      float amount, QVec3 direction) const {
    const auto &last = source.impl_->last_request;
    return last.has_value() && last->mode == mode && last->amount == amount &&
           last->direction.x == direction.x && last->direction.y == direction.y &&
           last->direction.z == direction.z;
  }

  [[nodiscard]] ModelingCommandResult validate_face_extrusion(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction,
      bool include_extrusion_preview = false) const {
    if ((preview_active() && !include_extrusion_preview) ||
        !face_extrusion_source_live(source)) {
      return failure("The extrusion source is no longer available.");
    }
    if (!std::isfinite(amount_internal) ||
        std::abs(amount_internal) > std::numeric_limits<float>::max()) {
      return failure("Extrusion amount must be finite.");
    }
    for (const auto &entry : source.impl_->objects) {
      auto current = session_.document_copy(entry.object);
      if (!current.ok()) {
        return failure(current.error_message());
      }
      auto prepared = prepare_face_extrusion_document(
          entry, current.value(), mode, static_cast<float>(amount_internal),
          world_direction.value_or(source.impl_->direction_world));
      if (!prepared.ok() || !prepared.value().success) {
        return from_sdk(prepared, "Extrusion preparation failed.");
      }
    }
    return {.ok = true};
  }

  [[nodiscard]] ModelingCommandResult validate_next_face_extrusion(
      modeling::FaceExtrudeMode mode, double amount_internal) const {
    const auto captured = capture_face_extrusion_source(true);
    if (!captured.ok) {
      return failure(captured.message);
    }
    if (!captured.source.valid()) {
      return {.ok = true};
    }
    return validate_face_extrusion(captured.source, mode, amount_internal,
                                  std::nullopt, true);
  }

  [[nodiscard]] ModelingCommandResult apply_face_extrusion(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction) {
    if (preview_active() || !face_extrusion_source_live(source)) {
      return failure("The extrusion source is no longer available.");
    }
    if (!std::isfinite(amount_internal) ||
        std::abs(amount_internal) > std::numeric_limits<float>::max()) {
      return failure("Extrusion amount must be finite.");
    }
    const QVec3 direction = world_direction.value_or(source.impl_->direction_world);
    if (same_face_extrusion_request(source, mode,
                                    static_cast<float>(amount_internal), direction)) {
      return {.ok = true};
    }
    std::vector<ObjectId> objects;
    objects.reserve(source.impl_->objects.size());
    for (const auto &entry : source.impl_->objects) {
      objects.push_back(entry.object);
    }
    auto result = mutate_documents_by_object(
        objects,
        [this, &source, mode, amount_internal, world_direction](
            ObjectId object, PolygonDocument &document) {
          const auto entry = std::ranges::find(source.impl_->objects, object,
              &ModelingFaceExtrusionSource::Impl::Object::object);
          return prepare_face_extrusion_document(
              *entry, document, mode, static_cast<float>(amount_internal),
              world_direction.value_or(source.impl_->direction_world));
        },
        "Extrusion could not be published.", false, false, false, true);
    if (result.ok) {
      source.impl_->fence = face_extrusion_fence();
      source.impl_->last_request = ModelingFaceExtrusionSource::Impl::Request{
          .mode = mode, .amount = static_cast<float>(amount_internal),
          .direction = direction};
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult begin_face_extrusion_preview(
      const ModelingFaceExtrusionSource &source, bool adjust_existing = false,
      const ModelingFaceExtrusionSource &retained_cancellation_source = {}) {
    if (preview_active() || !face_extrusion_source_live(source) ||
        (retained_cancellation_source.valid() &&
         !face_extrusion_source_live(retained_cancellation_source))) {
      return failure("The extrusion source cannot begin a preview.");
    }
    std::vector<ObjectId> objects;
    PreparedMeshPreviewBatch prepared;
    std::vector<FaceExtrusionPreviewObject> preview_objects;
    for (const auto &entry : source.impl_->objects) {
      auto current = session_.document_copy(entry.object);
      if (!current.ok()) {
        return failure(current.error_message());
      }
      if (!adjust_existing &&
          (!entry.document.has_same_geometry(current.value()) ||
           !same_polygon_selection(entry.document.selection(),
                                   current.value().selection()))) {
        return failure("Select current faces before beginning an extrusion drag.");
      }
      objects.push_back(entry.object);
      preview_objects.push_back({.object = entry.object,
                                 .baseline = current.value()});
      prepared.updates.push_back({.object = entry.object,
                                 // Cancellation keeps allocation cursors above
                                 // all retired preview IDs. The source geometry
                                 // is unchanged, but its old cursors are stale.
                                 .document = std::move(current).value(),
                                 .receipt = {.success = true}});
    }
    auto begun = begin_prepared_mesh_preview_transaction(
        objects, std::move(prepared), "face_extrusion");
    if (begun.ok) {
      face_extrusion_preview_objects_ = std::move(preview_objects);
      clear_face_extrusion_preview_projection();
      face_extrusion_preview_source_ = source.impl_;
      // A new gesture advances owner revisions even when canceled. Register
      // only the prior source proven current before begin; compensation may
      // refresh its fence without making arbitrary stale sources eligible.
      face_extrusion_retained_cancellation_source_ =
          retained_cancellation_source.impl_;
      face_extrusion_preview_request_before_ = source.impl_->last_request;
      source.impl_->fence = face_extrusion_fence();
      if (!adjust_existing) {
        source.impl_->last_request.reset();
      }
    } else if (!begun.changed && !preview_active()) {
      source.impl_->fence = face_extrusion_fence();
      if (retained_cancellation_source.valid()) {
        retained_cancellation_source.impl_->fence = face_extrusion_fence();
      }
    }
    return begun;
  }

  [[nodiscard]] bool face_extrusion_preview_active() const {
    return preview_active() && active_preview_operation_id_ == "face_extrusion";
  }

  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  face_extrusion_preview_meshes() const {
    return face_extrusion_preview_meshes_;
  }

  [[nodiscard]] std::uint64_t face_extrusion_preview_revision() const {
    return face_extrusion_preview_revision_;
  }

  void clear_face_extrusion_preview_projection() {
    if (!face_extrusion_preview_meshes_.empty()) {
      face_extrusion_preview_meshes_.clear();
      ++face_extrusion_preview_revision_;
    }
  }

  /// Publishes only derived render data; authored candidates stay in the SDK.
  [[nodiscard]] bool project_face_extrusion_preview() {
    QDR_PROFILE_SCOPE("Extrusion preview derive dynamic meshes");
    const auto payload = session_.preview_transaction_payload();
    if (!payload.ok() || !face_extrusion_preview_source_) {
      return false;
    }
    std::vector<ModelingToolPreviewMesh> meshes;
    meshes.reserve(payload.value().objects.size());
    for (const auto &object : payload.value().objects) {
      const auto entry = std::ranges::find(
          face_extrusion_preview_source_->objects, object.object_id,
          &ModelingFaceExtrusionSource::Impl::Object::object);
      const auto *compiled =
          std::get_if<modeling::ToolPreviewPayload>(&object.payload);
      if (entry == face_extrusion_preview_source_->objects.end() ||
          compiled == nullptr || !compiled->valid) {
        return false;
      }
      const auto document = session_.preview_document_copy(payload.value().id,
                                                           object.object_id);
      const auto *binding = scene_.mesh(entry->entity);
      const auto *current = binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
      if (!document.ok() || current == nullptr) {
        return false;
      }
      auto mesh = MeshProjectionService::mesh_asset_from_sdk_payload(
          compiled->mesh, document.value());
      if (!mesh.has_value() || !assets_.validate_mesh(*mesh).ok) {
        return false;
      }
      mesh->name = current->name;
      auto positions = std::make_shared<std::vector<QVec3>>();
      positions->reserve(mesh->geometry().vertices.size());
      for (const auto &vertex : mesh->geometry().vertices) {
        positions->push_back(vertex.position);
      }
      const auto *material = scene_.material(entry->entity);
      const auto layout = transform_mesh_layout_key(object.object_id,
                                                     mesh->geometry().vertices);
      meshes.push_back({
          .source_object_id = entry->native_id,
          .source_entity = entry->entity,
          .material = material != nullptr ? *material : MaterialComponent{},
          .mesh = std::move(*mesh),
          .transform = modeling_object_world_transform_resolver_
                           ? modeling_object_world_transform_resolver_(entry->native_id)
                                 .value_or(scene_.world_transform_matrix(entry->entity))
                           : scene_.world_transform_matrix(entry->entity),
          .double_sided = binding->double_sided,
          .dynamic_positions = true,
          .suppress_source = true,
          .layout_key = layout,
          .position_revision = object.content_revision,
          .dynamic_position_payload = std::move(positions)});
    }
    face_extrusion_preview_meshes_ = std::move(meshes);
    ++face_extrusion_preview_revision_;
    return true;
  }

  [[nodiscard]] ModelingCommandResult update_face_extrusion_preview(
      const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
      double amount_internal, std::optional<QVec3> world_direction) {
    if (!face_extrusion_preview_active() ||
        face_extrusion_preview_source_ != source.impl_ ||
        !face_extrusion_source_live(source)) {
      return failure("No matching extrusion preview is active.");
    }
    if (!std::isfinite(amount_internal) ||
        std::abs(amount_internal) > std::numeric_limits<float>::max()) {
      return failure("Extrusion amount must be finite.");
    }
    const auto preview = session_.preview_summary();
    const QVec3 direction = world_direction.value_or(source.impl_->direction_world);
    if (same_face_extrusion_request(source, mode,
                                    static_cast<float>(amount_internal), direction)) {
      return {.ok = true};
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure("Extrusion preview participants changed outside the gesture.");
    }
    PreparedMeshPreviewBatch prepared;
    auto next_objects = face_extrusion_preview_objects_;
    const float amount = static_cast<float>(amount_internal);
    for (std::size_t index = 0; index < source.impl_->objects.size(); ++index) {
      const auto &entry = source.impl_->objects[index];
      auto current = session_.preview_document_copy(preview.id, entry.object);
      if (!current.ok()) {
        return failure(current.error_message());
      }
      auto &participant = next_objects[index];
      if (!participant.prepared.has_value() || participant.mode != mode) {
        PolygonDocument origin = entry.document;
        origin.preserve_identity_high_water(current.value());
        auto operation = origin.prepare_face_extrusion(mode, entry.world_from_local);
        if (!operation.ok()) {
          return failure(operation.error_message());
        }
        participant.prepared = std::move(operation).value();
        participant.mode = mode;
      }
      auto evaluated = participant.prepared->evaluate(amount, to_sdk(direction));
      if (!evaluated.ok() || !evaluated.value().receipt.success) {
        return failure(evaluated.ok() ? evaluated.value().receipt.message
                                     : evaluated.error_message());
      }
      auto document = std::move(evaluated.value().document);
      auto receipt = std::move(evaluated.value().receipt);
      if (amount == 0.0F) {
        document.preserve_identity_high_water(current.value());
        participant.prepared.reset();
        receipt.dirty = {.topology = true, .geometry = true, .selection = true,
                         .materials = true, .overlays = true};
      }
      // The SDK receipt is relative to the immutable extrusion source. A redo
      // drag may instead begin at a committed extrusion, including when zero
      // removes it. Exact cancellation-origin equivalence is checked once on
      // release, without parsing whole meshes for every pointer sample.
      receipt.changed = true;
      prepared.changed = true;
      prepared.updates.push_back({.object = entry.object,
                                 .document = std::move(document),
                                 .receipt = std::move(receipt)});
    }
    const auto request_before = face_extrusion_preview_request_before_;
    const auto retained_source = face_extrusion_retained_cancellation_source_;
    auto result = update_prepared_mesh_preview_transaction(
        std::move(prepared), "Extrusion preview updated.", true);
    if (result.ok) {
      face_extrusion_preview_objects_ = std::move(next_objects);
      source.impl_->fence = face_extrusion_fence();
      source.impl_->last_request = ModelingFaceExtrusionSource::Impl::Request{
          .mode = mode, .amount = static_cast<float>(amount_internal),
          .direction = direction};
    } else if (!result.changed && !face_extrusion_preview_active()) {
      // The bounded publication owner already compensated this failed update
      // to the gesture baseline. The same source may still adjust that result.
      source.impl_->fence = face_extrusion_fence();
      source.impl_->last_request = request_before;
      if (retained_source) {
        retained_source->fence = face_extrusion_fence();
      }
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult commit_face_extrusion_preview() {
    if (!face_extrusion_preview_active()) {
      return failure("No extrusion preview is active.");
    }
    const auto preview = session_.preview_summary();
    bool changed = false;
    for (const auto &entry : face_extrusion_preview_objects_) {
      const auto current = session_.preview_document_copy(preview.id, entry.object);
      if (!current.ok()) {
        return failure(current.error_message());
      }
      changed = changed || !current.value().has_same_geometry(entry.baseline) ||
                !same_polygon_selection(current.value().selection(),
                                        entry.baseline.selection());
    }
    preview_changed_ = changed;
    const auto source = face_extrusion_preview_source_;
    const auto retained_source = face_extrusion_retained_cancellation_source_;
    const auto request_before = face_extrusion_preview_request_before_;
    auto result = commit_translate_preview();
    if (result.ok && source) {
      source->fence = face_extrusion_fence();
    } else if (!result.changed && !preview_active() && source) {
      source->fence = face_extrusion_fence();
      source->last_request = request_before;
    }
    if (!result.changed && !preview_active() && retained_source) {
      retained_source->fence = face_extrusion_fence();
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult cancel_face_extrusion_preview() {
    if (!face_extrusion_preview_active()) {
      return {.ok = true};
    }
    const auto source = face_extrusion_preview_source_;
    const auto retained_source = face_extrusion_retained_cancellation_source_;
    const auto request_before = face_extrusion_preview_request_before_;
    auto result = cancel_translate_preview();
    if (result.ok && source) {
      source->fence = face_extrusion_fence();
      source->last_request = request_before;
    }
    if (result.ok && retained_source) {
      retained_source->fence = face_extrusion_fence();
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult execute_operation(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    using Id = quader_editor_core::EditorOperationId;

    if (operation == Id::DuplicateMeshes) {
      const auto *value =
          std::get_if<quader_editor_core::DuplicateMeshesOperationSettings>(
              &settings);
      const QVec3 offset =
          value != nullptr
              ? quader_editor_core::duplicate_meshes_offset_from_settings(
                    *value)
              : QVec3{};
      return duplicate_selected_objects(offset, {},
                                        "Duplicated selected meshes.", false,
                                        "Mesh");
    }
    if (operation == Id::CombineMeshes) {
      return combine_selected_objects();
    }
    if (operation == Id::ExtractFaces) {
      return extract_selected_faces();
    }
    if (operation == Id::HalfCut) {
      const auto *value =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      return half_cut_selected_objects(
          quader_editor_core::normalize_half_cut_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::HalfCutOperationSettings{}));
    }
    if (operation == Id::Bridge &&
        selected_component_objects().size() > 1U) {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const quader_editor_core::BridgeOperationSettings normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      return bridge_selected_components(normalized.steps);
    }
    if (operation == Id::MergeVerticesToActive) {
      const auto active = active_sdk_object();
      const auto objects = selected_component_objects();
      if (!active.has_value() ||
          std::ranges::find(objects, *active) == objects.end()) {
        return failure("Merge to Active needs a selected active vertex owner.");
      }
      const auto document = session_.document_copy(*active);
      if (!document.ok()) {
        return failure(document.error_message());
      }
      const auto selection = document.value().selection();
      if (!selection.has_active || !selection.active_vertex.valid() ||
          std::ranges::find(selection.vertices, selection.active_vertex) ==
              selection.vertices.end()) {
        return failure("Merge to Active needs a live selected active vertex.");
      }
    }
    if ((operation == Id::MergeByDistance || operation == Id::MergeEdges ||
         operation == Id::MergeVerticesToActive ||
         operation == Id::MergeVerticesToCenter) &&
        selected_component_objects().size() > 1U) {
      const auto *value =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::MergeByDistanceOperationSettings{});
      const ModelingCommandResult begun = begin_component_merge_preview(
          operation, static_cast<float>(normalized.tolerance));
      if (!begun.ok) {
        return begun;
      }
      const ModelingCommandResult committed = commit_translate_preview();
      if (!committed.ok && preview_active()) {
        const ModelingCommandResult canceled = cancel_translate_preview();
        if (!canceled.ok) {
          return {.ok = false, .changed = true,
                  .message = committed.message +
                             " Merge publication rollback failed."};
        }
      }
      return committed;
    }

    const ModelingSelectionMode mode_before = selection_mode_;
    const auto whole_object_faces = [mode_before](
                                        PolygonDocument &document,
                                        const DocumentMutation &operation_fn) {
      return apply_with_whole_object_face_selection(
          mode_before, document, operation_fn);
    };

    DocumentMutation mutation;
    switch (operation) {
    case Id::Bridge: {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      mutation = [this, normalized](PolygonDocument &document) {
        return selection_mode_ == ModelingSelectionMode::Face
                   ? document.bridge_selected_faces(normalized.steps)
                   : document.bridge_selected_edges(normalized.steps);
      };
      break;
    }
    case Id::EdgeBevel: {
      const auto *value =
          std::get_if<quader_editor_core::EdgeBevelOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_edge_bevel_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::EdgeBevelOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.bevel_selected_edges({
            .width = static_cast<float>(normalized.width),
            .profile = static_cast<float>(normalized.profile),
            .segments = normalized.segments,
        });
      };
      break;
    }
    case Id::Thicken: {
      const auto *value =
          std::get_if<quader_editor_core::ThickenOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_thicken_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::ThickenOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.thicken_selected_faces(
            static_cast<float>(normalized.thickness), normalized.from_center);
      };
      break;
    }
    case Id::SliceQuad: {
      const auto *value =
          std::get_if<quader_editor_core::SliceQuadOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_slice_quad_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::SliceQuadOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.slice_selected_quads(normalized.x_slices,
                                             normalized.y_slices);
      };
      break;
    }
    case Id::MergeByDistance: {
      const auto *value =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::MergeByDistanceOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.merge_selected_vertices_by_distance(
            static_cast<float>(normalized.tolerance));
      };
      break;
    }
    case Id::RemoveDoubles:
      mutation = [](PolygonDocument &document) {
        return document.remove_double_vertices();
      };
      break;
    case Id::BevelVertices: {
      const auto *value =
          std::get_if<quader_editor_core::BevelVerticesOperationSettings>(
              &settings);
      const float distance = static_cast<float>(
          quader_editor_core::bevel_vertices_distance_from_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::BevelVerticesOperationSettings{}));
      mutation = [distance](PolygonDocument &document) {
        return document.bevel_selected_vertices(distance);
      };
      break;
    }
    case Id::ConnectVertices:
      mutation = [](PolygonDocument &document) {
        return document.connect_selected_vertices();
      };
      break;
    case Id::DissolveVertices:
      mutation = [](PolygonDocument &document) {
        return document.dissolve_selected_vertices();
      };
      break;
    case Id::ConnectEdges:
      mutation = [](PolygonDocument &document) {
        return document.connect_selected_edges();
      };
      break;
    case Id::SplitEdges:
      mutation = [](PolygonDocument &document) {
        return document.split_selected_edges();
      };
      break;
    case Id::DissolveEdges:
      mutation = [](PolygonDocument &document) {
        return document.dissolve_selected_edges();
      };
      break;
    case Id::MergeEdges:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_edges();
      };
      break;
    case Id::CollapseEdges:
      mutation = [](PolygonDocument &document) {
        return document.collapse_selected_edges();
      };
      break;
    case Id::FillHole:
      mutation = [](PolygonDocument &document) {
        return document.fill_hole_from_selected_edges();
      };
      break;
    case Id::CombineFaces:
      mutation = [](PolygonDocument &document) {
        return document.combine_selected_faces();
      };
      break;
    case Id::CollapseFaces:
      mutation = [](PolygonDocument &document) {
        return document.collapse_selected_faces();
      };
      break;
    case Id::RadialAlign:
      mutation = [](PolygonDocument &document) {
        return document.radial_align_selection();
      };
      break;
    case Id::FlipFaceNormals:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.flip_selected_face_normals();
            });
      };
      break;
    case Id::ShadeFacesSmooth:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.shade_selected_faces_smooth();
            });
      };
      break;
    case Id::ShadeFacesFlat:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.shade_selected_faces_flat();
            });
      };
      break;
    case Id::HardenEdgeNormals:
      mutation = [](PolygonDocument &document) {
        return document.harden_selected_edge_normals();
      };
      break;
    case Id::SoftenEdgeNormals:
      mutation = [](PolygonDocument &document) {
        return document.soften_selected_edge_normals();
      };
      break;
    case Id::DefaultEdgeNormals:
      mutation = [](PolygonDocument &document) {
        return document.reset_selected_edge_normals_to_default();
      };
      break;
    case Id::DetachFaces:
      mutation = [](PolygonDocument &document) {
        return document.detach_selected_faces();
      };
      break;
    case Id::SnapVerticesToActive:
      mutation = [](PolygonDocument &document) {
        return document.snap_selected_vertices_to_active();
      };
      break;
    case Id::SnapEdgesToActive:
      mutation = [](PolygonDocument &document) {
        return document.snap_selected_edges_to_active();
      };
      break;
    case Id::MergeVerticesToActive:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_vertices_to_active();
      };
      break;
    case Id::MergeVerticesToCenter:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_vertices_to_center();
      };
      break;
    case Id::RecalculateFaceNormals: {
      const auto *value = std::get_if<
          quader_editor_core::RecalculateFaceNormalsOperationSettings>(
          &settings);
      const bool outside = value == nullptr || value->outside;
      mutation = [whole_object_faces, outside](PolygonDocument &document) {
        return whole_object_faces(
            document, [outside](PolygonDocument &selected_document) {
              return selected_document.recalculate_selected_face_normals(
                  outside);
            });
      };
      break;
    }
    case Id::AssignMaterial: {
      const auto *value =
          std::get_if<quader_editor_core::AssignMaterialOperationSettings>(
              &settings);
      if (value != nullptr && value->explicit_target) {
        return assign_material_slot_to_face(
            value->object_id, static_cast<int>(value->face_id),
            value->material_slot);
      }
      const std::uint32_t slot = value != nullptr ? value->material_slot : 0U;
      return assign_material_slot_to_selection(slot);
    }
    case Id::DeleteSelection:
      mutation = [](PolygonDocument &document) {
        return document.delete_selection();
      };
      break;
    case Id::DuplicateMeshes:
    case Id::CombineMeshes:
    case Id::ExtractFaces:
    case Id::HalfCut:
      break;
    }

    if (!mutation) {
      return failure("SDK operation mapping is incomplete.");
    }
    ModelingCommandResult result;
    if (operation == Id::ConnectVertices) {
      std::vector<ObjectId> objects;
      for (const ObjectId object : selected_component_objects()) {
        const auto document = session_.document_copy(object);
        if (!document.ok()) {
          return failure(document.error_message());
        }
        if (document.value().selection_topology_eligibility()
                .selected_vertices_share_face) {
          objects.push_back(object);
        }
      }
      if (objects.empty()) {
        return failure("Connect Vertices needs selected vertices on a common face.");
      }
      result = mutate_documents(objects, mutation, sdk_operation_id(operation));
    } else {
      result = mutate_selected_documents(mutation, sdk_operation_id(operation));
    }
    if (result.ok && result.changed && operation == Id::FillHole) {
      selection_mode_ = ModelingSelectionMode::Face;
      ++mode_revision_;
      if (editor_mode_service_ != nullptr) {
        editor_mode_service_->record_component_kind(selection_mode_);
      }
    }
    return result;
  }

  [[nodiscard]] std::optional<PreparedHalfCutObject>
  prepare_half_cut_preview_object(
      ObjectId source,
      const quader_editor_core::HalfCutOperationSettings &settings,
      std::optional<PreviewId> preview_id, std::string &error) const {
    const std::optional<int> native = native_object(source);
    const std::optional<EntityId> entity =
        native.has_value()
            ? object_map_.entity_for_modeling_object(*native)
            : std::nullopt;
    const ModelingObjectComponent *component =
        entity.has_value() && scene_.exists(*entity)
            ? scene_.modeling_object(*entity)
            : nullptr;
    const std::optional<SceneEntityState> scene_identity =
        entity.has_value() && scene_.exists(*entity)
            ? capture_scene_entity_state(scene_, *entity)
            : std::nullopt;
    if (!native.has_value() || !entity.has_value() || component == nullptr ||
        !scene_identity.has_value()) {
      error = "Half-Cut source scene identity is incomplete.";
      return std::nullopt;
    }

    Result<PolygonDocument> copied =
        preview_id.has_value()
            ? session_.preview_base_document_copy(*preview_id, source)
            : session_.document_copy(source);
    if (!copied.ok()) {
      error = copied.error_message();
      return std::nullopt;
    }
    PolygonDocument working = std::move(copied).value();
    Result<AuthoredTopology> topology = working.authored_topology();
    if (!topology.ok() || topology.value().vertices.empty()) {
      error = topology.ok() ? "Half-Cut target mesh has no vertices."
                            : topology.error_message();
      return std::nullopt;
    }
    quader::modeling::Vec3 minimum = topology.value().vertices.front().position;
    quader::modeling::Vec3 maximum = minimum;
    for (const quader::modeling::AuthoredTopologyVertex &vertex :
         topology.value().vertices) {
      minimum.x = std::min(minimum.x, vertex.position.x);
      minimum.y = std::min(minimum.y, vertex.position.y);
      minimum.z = std::min(minimum.z, vertex.position.z);
      maximum.x = std::max(maximum.x, vertex.position.x);
      maximum.y = std::max(maximum.y, vertex.position.y);
      maximum.z = std::max(maximum.z, vertex.position.z);
    }
    const quader::modeling::Vec3 center{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };
    quader::modeling::PlaneSplitSpec split{
        .a = center,
        .cap = settings.fill_parts,
    };
    switch (settings.axis) {
    case quader_editor_core::HalfCutAxis::X:
      split.b = {center.x, center.y + 1.0F, center.z};
      split.c = {center.x, center.y, center.z + 1.0F};
      break;
    case quader_editor_core::HalfCutAxis::Y:
      split.b = {center.x, center.y, center.z + 1.0F};
      split.c = {center.x + 1.0F, center.y, center.z};
      break;
    case quader_editor_core::HalfCutAxis::Z:
      split.b = {center.x + 1.0F, center.y, center.z};
      split.c = {center.x, center.y + 1.0F, center.z};
      break;
    }

    const auto dormant = working.selection();
    PreparedHalfCutObject prepared;
    prepared.source = source;
    static_cast<SceneEntityState &>(prepared.source_identity) = *scene_identity;
    prepared.material_slot = component->material_slot;
    if (!settings.separate_parts) {
      Result<OperationResult> cut = working.cut_surface_by_plane(split);
      if (!cut.ok() || !cut.value().success || !cut.value().changed) {
        error = cut.ok() ? cut.value().message : cut.error_message();
        return std::nullopt;
      }
      auto cleared = restore_dormant_selection(working, dormant, cut.value());
      if (!cleared.ok()) {
        error = cleared.error_message();
        return std::nullopt;
      }
      prepared.front = std::move(working);
      prepared.receipt = std::move(cut).value();
      return prepared;
    }

    Result<quader::modeling::SplitPolygonDocuments> cut =
        working.split_by_plane(split);
    if (!cut.ok()) {
      error = cut.error_message();
      return std::nullopt;
    }
    auto cleared_front = restore_dormant_selection(
        cut.value().front, dormant, cut.value().receipt);
    auto cleared_back = restore_dormant_selection(
        cut.value().back, dormant, cut.value().receipt);
    if (!cleared_front.ok() || !cleared_back.ok()) {
      error = !cleared_front.ok() ? cleared_front.error_message()
                                  : cleared_back.error_message();
      return std::nullopt;
    }
    prepared.front = std::move(cut.value().front);
    prepared.back.emplace(std::move(cut.value().back));
    prepared.receipt = cut.value().receipt;
    return prepared;
  }

  [[nodiscard]] ModelingCommandResult begin_half_cut_preview(
      const quader_editor_core::HalfCutOperationSettings &settings) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Half-Cut requires object selection mode.");
    }
    const std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.empty()) {
      return failure("Half-Cut needs one or more selected meshes.");
    }

    std::vector<PreparedHalfCutObject> prepared;
    prepared.reserve(sources.size());
    std::string error;
    for (const ObjectId source : sources) {
      std::optional<PreparedHalfCutObject> object =
          prepare_half_cut_preview_object(source, settings, std::nullopt, error);
      if (!object.has_value()) {
        return failure(std::move(error));
      }
      prepared.push_back(std::move(*object));
    }

    modeling::SessionPreviewTransactionRequest request;
    request.replace_object_selection = true;
    request.participants.reserve(prepared.size() *
                                 (settings.separate_parts ? 2u : 1u));
    std::vector<std::pair<std::size_t, std::size_t>> add_request_sources;
    for (std::size_t index = 0; index < prepared.size(); ++index) {
      PreparedHalfCutObject &object = prepared[index];
      request.participants.push_back({
          .action = modeling::SessionPreviewAction::ReplaceExisting,
          .object = object.source,
          .document = modeling::SessionPreviewDocument{object.front},
          .receipt = object.receipt,
          .name = object.source_identity.name,
          .material_slot = object.material_slot,
          .selected = true,
      });
      if (object.back.has_value()) {
        const std::size_t request_index = request.participants.size();
        request.participants.push_back({
            .action = modeling::SessionPreviewAction::AddProvisional,
            .object = {},
            .document = modeling::SessionPreviewDocument{*object.back},
            .receipt = object.receipt,
            .name = object.source_identity.name,
            .material_slot = object.material_slot,
            .selected = true,
        });
        add_request_sources.emplace_back(request_index, index);
      }
    }
    request.active_participant_index = request.participants.size() - 1u;

    AdapterPublicationState host_before =
        capture_adapter_publication_state();
    std::optional<int> first_added_native;
    if (!add_request_sources.empty()) {
      first_added_native = reserve_native_object_ids(add_request_sources.size());
      if (!first_added_native.has_value()) {
        return failure("Native modeling object IDs are exhausted.");
      }
    }
    std::vector<int> affected_modeling_ids;
    affected_modeling_ids.reserve(sources.size() + add_request_sources.size());
    for (const ObjectId source : sources) {
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value()) {
        restore_adapter_publication_state(std::move(host_before));
        return failure("Half-Cut source scene identity is not live.");
      }
      affected_modeling_ids.push_back(*native);
    }
    for (std::size_t index = 0; index < add_request_sources.size(); ++index) {
      affected_modeling_ids.push_back(*first_added_native +
                                      static_cast<int>(index));
    }
    if (!capture_mesh_preview_host_baseline(
            sources, affected_modeling_ids, host_before,
            SceneModelingProjectionDomain::MeshBindings |
                SceneModelingProjectionDomain::Selection)) {
      restore_adapter_publication_state(std::move(host_before));
      return failure("Bounded Half-Cut preview baseline is unavailable.");
    }

    Result<modeling::SessionPreviewBeginResult> begun =
        session_.begin_preview_transaction("half_cut", std::move(request));
    if (!begun.ok()) {
      restore_adapter_publication_state(
          std::move(mesh_preview_host_baseline_->adapter_before));
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    if (!activate_mesh_preview_host_baseline(begun.value().preview_id) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_preview_object(true)) {
      return abort_preview_publication(
          begun.value().preview_id,
          "Half-Cut preview publication failed.");
    }
    StructuralPreviewHostState structural;
    structural.preview_id = begun.value().preview_id;
    structural.next_native_object_id_before = host_before.next_native_object_id;
    structural.source_objects = sources;
    std::size_t added_index = 0u;
    for (const auto &[request_index, prepared_index] : add_request_sources) {
      const auto identity = std::ranges::find(
          begun.value().participants, request_index,
          &modeling::SessionPreviewParticipantIdentity::participant_index);
      if (identity == begun.value().participants.end() ||
          identity->action != modeling::SessionPreviewAction::AddProvisional) {
        return abort_preview_publication(
            begun.value().preview_id,
            "Half-Cut preview identity mapping failed.");
      }
      int native = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      PreparedHalfCutObject &source = prepared[prepared_index];
      const bool published = publish_new_object(
          nullptr, identity->object_id, source.source_identity,
          source.material_slot,
          native, entity, false,
          *first_added_native + static_cast<int>(added_index),
          begun.value().preview_id);
      if (!published) {
        return abort_preview_publication(
            begun.value().preview_id,
            "Half-Cut back preview projection failed.");
      }
      structural.added_objects.push_back(
          {.modeling_object_id = native});
      structural.half_cut_added_by_source.emplace_back(source.source,
                                                       identity->object_id);
      ++added_index;
    }
    structural_preview_host_state_ = std::move(structural);
    preview_changed_ = true;
    active_preview_operation_id_ = "half_cut";
    active_preview_operation_ = quader_editor_core::EditorOperationId::HalfCut;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          begun.value().preview_id,
          "Half-Cut preview publication could not be acknowledged.");
    }
    return {.ok = true,
            .changed = true,
            .message = "Half-Cut preview started."};
  }

  [[nodiscard]] ModelingCommandResult update_half_cut_preview(
      const quader_editor_core::HalfCutOperationSettings &settings) {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!structural_preview_host_state_.has_value() ||
        !structural_preview_host_state_->active_for(preview.id)) {
      return failure("Half-Cut preview host state is unavailable.");
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Half-Cut preview participants changed outside this preview.");
    }
    const bool preview_separates_parts =
        !structural_preview_host_state_->half_cut_added_by_source.empty();
    if (preview_separates_parts != settings.separate_parts) {
      const auto *previous_settings =
          active_preview_settings_.has_value()
              ? std::get_if<quader_editor_core::HalfCutOperationSettings>(
                    &*active_preview_settings_)
              : nullptr;
      if (previous_settings == nullptr) {
        return failure("Half-Cut preview settings are unavailable.");
      }
      const quader_editor_core::HalfCutOperationSettings rollback_settings =
          *previous_settings;
      // AddProvisional participants cannot be inserted into or removed from an
      // active portable transaction. End only this preview generation at its
      // retained owner baseline, then begin a new generation directly in the
      // adapter. The outer command remains UpdatePreview, so this structural
      // settings edit cannot create an editor or SDK history entry.
      // Failure injection is checked while the prior generation is still
      // intact. It models a rejected candidate rebuild without sacrificing
      // the live preview that still represents the committed settings.
      if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
        return failure("Half-Cut preview rebuild failed.");
      }
      if (!restore_preview_to_original_baseline(preview.id, false)) {
        return {.ok = false,
                .changed = true,
                .message = "Half-Cut preview could not be rebuilt."};
      }
      ModelingCommandResult rebuilt = begin_half_cut_preview(settings);
      if (!rebuilt.ok) {
        const std::string rebuild_message = rebuilt.message.empty()
                                                ? "Half-Cut preview rebuild failed."
                                                : rebuilt.message;
        if (preview_active()) {
          return rebuilt;
        }
        ModelingCommandResult rolled_back =
            begin_half_cut_preview(rollback_settings);
        if (!rolled_back.ok) {
          return {
              .ok = false,
              .changed = true,
              .message = rebuild_message +
                         " The previous preview could not be restored.",
          };
        }
        active_preview_settings_ = rollback_settings;
        return failure(rebuild_message);
      }
      rebuilt.message = "Half-Cut preview rebuilt.";
      return rebuilt;
    }
    std::vector<modeling::SessionPreviewDocumentUpdate> updates;
    updates.reserve(preview.objects.size());
    std::string error;
    for (const ObjectId source :
         structural_preview_host_state_->source_objects) {
      std::optional<PreparedHalfCutObject> object =
          prepare_half_cut_preview_object(source, settings, preview.id, error);
      if (!object.has_value()) {
        return failure(std::move(error));
      }
      updates.push_back({.object = source,
                         .document = std::move(object->front),
                         .receipt = object->receipt});
      const auto added = std::ranges::find(
          structural_preview_host_state_->half_cut_added_by_source, source,
          &std::pair<ObjectId, ObjectId>::first);
      if (object->back.has_value()) {
        if (added ==
            structural_preview_host_state_->half_cut_added_by_source.end()) {
          return failure("Half-Cut preview participant mapping is incomplete.");
        }
        updates.push_back({.object = added->second,
                           .document = std::move(*object->back),
                           .receipt = object->receipt});
      } else if (added !=
                 structural_preview_host_state_->half_cut_added_by_source.end()) {
        return failure("Half-Cut preview participant mapping is inconsistent.");
      }
    }

    Result<modeling::SessionPreviewCheckpoint> sdk_checkpoint =
        session_.capture_preview_checkpoint(preview.id);
    if (!sdk_checkpoint.ok()) {
      return failure(sdk_checkpoint.error_message());
    }
    Result<OperationResult> updated =
        session_.update_preview_transaction(preview.id, std::move(updates));
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      Result<OperationResult> restored =
          session_.restore_preview_checkpoint(sdk_checkpoint.value());
      if (!restored.ok() || !preview_fence_matches(preview.id)) {
        return {.ok = false,
                .changed = true,
                .message =
                    "Half-Cut preview candidate and rollback both failed."};
      }
      return failure("Half-Cut preview publication failed.");
    }
    if (!project_preview_object()) {
      return abort_preview_publication(
          preview.id, "Half-Cut preview publication failed.");
    }
    preview_changed_ = true;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "Half-Cut preview publication could not be acknowledged.");
    }
    return {.ok = true,
            .changed = true,
            .message = "Half-Cut preview updated."};
  }

  [[nodiscard]] ModelingCommandResult begin_bridge_preview(int steps) {
    if (selection_mode_ != ModelingSelectionMode::Edge &&
        selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Bridge works only in edge or face mode.");
    }
    std::vector<ObjectId> sources = selected_component_objects();
    if (sources.size() != 2u) {
      return failure("Bridge preview needs exactly two selected meshes.");
    }
    if (const std::optional<ObjectId> active = active_sdk_object()) {
      const auto found = std::ranges::find(sources, *active);
      if (found != sources.end()) {
        std::rotate(sources.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = sources[0];
    const ObjectId source = sources[1];
    const std::optional<int> survivor_native = native_object(survivor);
    const std::optional<int> source_native = native_object(source);
    const std::optional<EntityId> survivor_entity =
        survivor_native.has_value()
            ? object_map_.entity_for_modeling_object(*survivor_native)
            : std::nullopt;
    const std::optional<EntityId> source_entity =
        source_native.has_value()
            ? object_map_.entity_for_modeling_object(*source_native)
            : std::nullopt;
    if (!survivor_entity.has_value() || !source_entity.has_value() ||
        !scene_.exists(*survivor_entity) || !scene_.exists(*source_entity)) {
      return failure("Bridge source scene identity is not live.");
    }
    const std::optional<QMat4> survivor_from_world =
        try_scene_matrix_inverse(scene_.world_transform_matrix(*survivor_entity));
    if (!survivor_from_world.has_value()) {
      return failure("Bridge survivor transform is not invertible.");
    }
    const quader::modeling::Transform3 survivor_from_source = to_sdk(
        scene_transform_multiply(*survivor_from_world,
                                 scene_.world_transform_matrix(*source_entity)));
    Result<modeling::SessionBridgePreviewCandidate> candidate =
        session_.prepare_bridge_documents(
            survivor, source, survivor_from_source,
            selection_mode_ == ModelingSelectionMode::Face
                ? SelectionKind::Face
                : SelectionKind::Edge,
            steps);
    if (!candidate.ok()) {
      return failure(candidate.error_message());
    }
    const ModelingObjectComponent *survivor_component =
        scene_.modeling_object(*survivor_entity);
    const ModelingObjectComponent *source_component =
        scene_.modeling_object(*source_entity);
    if (survivor_component == nullptr || source_component == nullptr) {
      return failure("Bridge source modeling identity is incomplete.");
    }
    modeling::SessionPreviewTransactionRequest request;
    request.replace_object_selection = true;
    request.active_participant_index = 0u;
    request.participants.push_back({
        .action = modeling::SessionPreviewAction::ReplaceExisting,
        .object = survivor,
        .document = modeling::SessionPreviewDocument{
            candidate.value().survivor_document},
        .receipt = candidate.value().receipt,
        .name = scene_.name(*survivor_entity) != nullptr
                    ? scene_.name(*survivor_entity)->name
                    : "Mesh",
        .material_slot = survivor_component->material_slot,
        .selected = true,
    });
    request.participants.push_back({
        .action = modeling::SessionPreviewAction::RemoveExisting,
        .object = source,
        .document = std::nullopt,
        .receipt = candidate.value().receipt,
        .name = scene_.name(*source_entity) != nullptr
                    ? scene_.name(*source_entity)->name
                    : "Mesh",
        .material_slot = source_component->material_slot,
        .selected = false,
    });
    const std::array<int, 2> affected_modeling_ids{*survivor_native,
                                                   *source_native};
    AdapterPublicationState host_before =
        capture_adapter_publication_state();
    if (!capture_mesh_preview_host_baseline(
            sources, affected_modeling_ids, host_before)) {
      return failure("Bounded Bridge preview baseline is unavailable.");
    }
    Result<modeling::SessionPreviewBeginResult> begun =
        session_.begin_preview_transaction("bridge", std::move(request));
    if (!begun.ok()) {
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    if (!activate_mesh_preview_host_baseline(begun.value().preview_id) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_preview_object(true) || !scene_.clear_mesh(*source_entity)) {
      return abort_preview_publication(
          begun.value().preview_id, "Bridge preview publication failed.");
    }
    structural_preview_host_state_ = StructuralPreviewHostState{
        .preview_id = begun.value().preview_id,
        .next_native_object_id_before = next_native_object_id_,
        .source_objects = sources,
        .bridge_survivor = survivor,
        .bridge_source = source,
        .bridge_survivor_from_source = survivor_from_source,
    };
    preview_changed_ = true;
    active_preview_operation_id_ = "bridge";
    active_preview_operation_ = quader_editor_core::EditorOperationId::Bridge;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          begun.value().preview_id,
          "Bridge preview publication could not be acknowledged.");
    }
    return {.ok = true,
            .changed = true,
            .message = "Bridge preview started."};
  }

  [[nodiscard]] ModelingCommandResult update_bridge_preview(int steps) {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!structural_preview_host_state_.has_value() ||
        !structural_preview_host_state_->active_for(preview.id) ||
        !structural_preview_host_state_->bridge_survivor.has_value() ||
        !structural_preview_host_state_->bridge_source.has_value() ||
        !structural_preview_host_state_->bridge_survivor_from_source.has_value()) {
      return failure("Bridge preview host state is unavailable.");
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Bridge preview participants changed outside this preview.");
    }
    Result<modeling::SessionBridgePreviewCandidate> candidate =
        session_.prepare_bridge_documents(
            *structural_preview_host_state_->bridge_survivor,
            *structural_preview_host_state_->bridge_source,
            *structural_preview_host_state_->bridge_survivor_from_source,
            selection_mode_ == ModelingSelectionMode::Face
                ? SelectionKind::Face
                : SelectionKind::Edge,
            steps);
    if (!candidate.ok()) {
      return failure(candidate.error_message());
    }
    const ObjectId survivor =
        *structural_preview_host_state_->bridge_survivor;
    std::vector<modeling::SessionPreviewDocumentUpdate> updates;
    updates.push_back({
        .object = survivor,
        .document = std::move(candidate.value().survivor_document),
        .receipt = std::move(candidate.value().receipt),
    });
    Result<OperationResult> updated =
        session_.update_preview_transaction(preview.id, std::move(updates));
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_preview_object()) {
      return abort_preview_publication(
          preview.id, "Bridge preview publication failed.");
    }
    preview_changed_ = true;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "Bridge preview publication could not be acknowledged.");
    }
    return {.ok = true,
            .changed = true,
            .message = "Bridge preview updated."};
  }

  [[nodiscard]] ModelingCommandResult begin_component_merge_preview(
      quader_editor_core::EditorOperationId operation, float tolerance) {
    const bool merge_edges =
        operation == quader_editor_core::EditorOperationId::MergeEdges;
    if (preview_active() ||
        (merge_edges ? selection_mode_ != ModelingSelectionMode::Edge
                     : selection_mode_ != ModelingSelectionMode::Vertex)) {
      return failure("Merge needs a matching component selection and no active preview.");
    }
    std::vector<ObjectId> objects = selected_component_objects();
    if (objects.size() < 2U) {
      return failure("Cross-object Merge needs at least two selected meshes.");
    }
    if (const auto active = active_sdk_object()) {
      const auto found = std::ranges::find(objects, *active);
      if (found != objects.end()) {
        std::rotate(objects.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = objects.front();
    std::vector<EntityId> entities;
    entities.reserve(objects.size());
    for (const ObjectId object : objects) {
      const auto native = native_object(object);
      const auto entity = native.has_value()
          ? object_map_.entity_for_modeling_object(*native) : std::nullopt;
      if (!entity.has_value() || !scene_.exists(*entity) ||
          scene_.modeling_object(*entity) == nullptr) {
        return failure("Merge source scene identity is not live.");
      }
      entities.push_back(*entity);
    }
    const QMat4 survivor_world = scene_.world_transform_matrix(entities.front());
    const auto survivor_from_world = try_scene_matrix_inverse(survivor_world);
    if (!survivor_from_world.has_value()) {
      return failure("Merge survivor transform is not invertible.");
    }
    const auto survivor_sampling = modeling_object_texture_sampling_resolver_
        ? modeling_object_texture_sampling_resolver_(*native_object(survivor))
        : effective_texture_sampling_from_local(scene_, entities.front());
    if (!survivor_sampling.has_value()) {
      return failure("Merge survivor texture placement is unavailable.");
    }
    const auto *material_before = scene_.material(entities.front());
    MaterialComponent combined_material = material_before != nullptr
        ? *material_before : MaterialComponent{};
    const auto survivor_document = session_.document_copy(survivor);
    if (!survivor_document.ok()) {
      return failure(survivor_document.error_message());
    }
    const auto survivor_settings = survivor_document.value().face_texture_settings(
        survivor_document.value().face_ids());
    if (!survivor_settings.ok()) {
      return failure(survivor_settings.error_message());
    }
    for (const auto &face : survivor_settings.value()) {
      if (face.material_slot != 0U) {
        combined_material.material_slots.try_emplace(
            face.material_slot,
            combined_material.material_for_slot(face.material_slot));
      }
    }

    StructuralPreviewHostState::MergeSources merge_sources{
        .survivor = survivor,
        .world_from_survivor = to_sdk(survivor_world),
    };
    merge_sources.sources.reserve(objects.size() - 1U);
    for (std::size_t index = 1U; index < objects.size(); ++index) {
      const auto copied = session_.document_copy(objects[index]);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      PolygonDocument working = copied.value();
      const PolygonSelectionSnapshot selection = working.selection();
      const QMat4 survivor_from_source = scene_transform_multiply(
          *survivor_from_world, scene_.world_transform_matrix(entities[index]));
      const auto source_sampling = modeling_object_texture_sampling_resolver_
          ? modeling_object_texture_sampling_resolver_(*native_object(objects[index]))
          : effective_texture_sampling_from_local(scene_, entities[index]);
      if (!source_sampling.has_value()) {
        return failure("Merge source texture placement is unavailable.");
      }
      if (survivor_from_source.values != QMat4{}.values ||
          source_sampling->values != survivor_sampling->values) {
        const auto authored =
            MeshProjectionService::polygon_document_from_sdk_document(working);
        if (!authored.has_value()) {
          return failure("Merge source texture topology is unavailable.");
        }
        const auto selected = working.select_all_vertices();
        if (!selected.ok()) {
          return failure(selected.error_message());
        }
        const auto transformed = working.transform_selection(to_sdk(survivor_from_source));
        if (!transformed.ok()) {
          return failure(transformed.error_message());
        }
        auto preserved = MeshProjectionService::rebase_sdk_document_texture_mapping(
            *authored, working, *source_sampling, *survivor_sampling,
            glm::determinant(glm::mat3{to_glm(survivor_from_source)}) < 0.0F);
        if (!preserved.ok()) {
          return failure(preserved.error_message());
        }
        working = std::move(preserved).value();
      }
      const auto restored = working.apply_selection(selection);
      if (!restored.ok()) {
        return failure(restored.error_message());
      }
      const auto *source_material = scene_.material(entities[index]);
      const auto face_settings = working.face_texture_settings(working.face_ids());
      if (!face_settings.ok()) {
        return failure(face_settings.error_message());
      }
      for (const auto &face : face_settings.value()) {
        const MaterialReference material = source_material != nullptr
            ? source_material->material_for_slot(face.material_slot)
            : MaterialReference{};
        std::uint32_t slot = 0U;
        try {
          slot = stage_scene_material_slot(combined_material, material);
        } catch (const std::overflow_error &error) {
          return failure(error.what());
        }
        if (slot != face.material_slot) {
          const auto assigned = working.assign_face_material_slot(face.face, slot);
          if (!assigned.ok()) {
            return failure(assigned.error_message());
          }
        }
      }
      merge_sources.sources.push_back({
          .object = objects[index],
          .survivor_from_source = {},
          .prepared_document = std::move(working),
      });
    }
    const modeling::SessionMergeIntent intent =
        operation == quader_editor_core::EditorOperationId::MergeVerticesToActive
            ? modeling::SessionMergeIntent::ToActive
        : operation == quader_editor_core::EditorOperationId::MergeVerticesToCenter
            ? modeling::SessionMergeIntent::ToCenter
            : modeling::SessionMergeIntent::ByDistance;
    auto candidate = session_.prepare_merge_documents(
        survivor, merge_sources.sources,
        merge_edges ? SelectionKind::Edge : SelectionKind::Vertex,
        tolerance, merge_sources.world_from_survivor, intent);
    if (!candidate.ok()) {
      return failure(candidate.error_message());
    }
    const bool changed = candidate.value().receipt.changed;
    const std::size_t deleted_vertices = candidate.value().receipt.deleted.vertices.size();
    modeling::SessionPreviewTransactionRequest request;
    request.replace_object_selection = changed;
    request.active_participant_index = 0U;
    for (std::size_t index = 0U; index < objects.size(); ++index) {
      modeling::SessionPreviewParticipant participant{
          .object = objects[index],
          .name = scene_.name(entities[index]) != nullptr
              ? scene_.name(entities[index])->name : "Mesh",
          .material_slot = scene_.modeling_object(entities[index])->material_slot,
          .selected = !changed || index == 0U,
      };
      if (!changed) {
        auto original = session_.document_copy(objects[index]);
        if (!original.ok()) {
          return failure(original.error_message());
        }
        participant.document = std::move(original).value();
        participant.receipt.success = true;
      } else if (index == 0U) {
        participant.document = std::move(candidate.value().survivor_document);
        participant.receipt = std::move(candidate.value().receipt);
      } else {
        participant.action = modeling::SessionPreviewAction::RemoveExisting;
        participant.receipt.success = true;
        participant.receipt.changed = true;
      }
      request.participants.push_back(std::move(participant));
    }
    if (!capture_mesh_preview_host_baseline(
            objects, {}, std::nullopt,
            SceneModelingProjectionDomain::MeshBindings |
                SceneModelingProjectionDomain::CurveBindings |
                SceneModelingProjectionDomain::Selection |
                SceneModelingProjectionDomain::Structure)) {
      return failure("Bounded Merge preview baseline is unavailable.");
    }
    const auto begun = session_.begin_preview_transaction(
        std::string(sdk_operation_id(operation)), std::move(request));
    if (!begun.ok()) {
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    const PreviewId preview_id = begun.value().preview_id;
    if (!activate_mesh_preview_host_baseline(preview_id) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return abort_preview_publication(preview_id, "Merge preview publication failed.");
    }
    if (changed) {
      if ((material_before == nullptr ? combined_material != MaterialComponent{}
                                    : combined_material != *material_before) &&
          !mesh_sync_.set_projected_material(scene_, entities.front(), &combined_material)) {
        return abort_preview_publication(preview_id, "Merge material publication failed.");
      }
      if (!project_preview_object(true)) {
        return abort_preview_publication(preview_id, "Merge preview publication failed.");
      }
      for (std::size_t index = 1U; index < entities.size(); ++index) {
        if (!scene_.clear_mesh(entities[index])) {
          return abort_preview_publication(preview_id, "Merge source preview suppression failed.");
        }
      }
    }
    structural_preview_host_state_ = StructuralPreviewHostState{
        .preview_id = preview_id,
        .next_native_object_id_before = next_native_object_id_,
        .source_objects = objects,
        .merge_sources = std::move(merge_sources),
    };
    preview_changed_ = changed;
    active_preview_deleted_vertex_count_ = deleted_vertices;
    active_preview_operation_id_ = sdk_operation_id(operation);
    active_preview_operation_ = operation;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(preview_id, "Merge preview could not be acknowledged.");
    }
    return {.ok = true, .changed = changed, .message = "Merge preview started."};
  }

  [[nodiscard]] ModelingCommandResult update_component_merge_preview(float tolerance) {
    const auto preview = session_.preview_summary();
    if (!structural_preview_host_state_.has_value() ||
        !structural_preview_host_state_->active_for(preview.id) ||
        !structural_preview_host_state_->merge_sources.has_value() ||
        !preflight_preview_mutation(preview.id)) {
      return failure("Merge preview participants changed outside this preview.");
    }
    const auto &sources = *structural_preview_host_state_->merge_sources;
    auto candidate = session_.prepare_merge_documents(
        sources.survivor, sources.sources, SelectionKind::Vertex, tolerance,
        sources.world_from_survivor);
    if (!candidate.ok()) {
      return failure(candidate.error_message());
    }
    if (candidate.value().receipt.changed != preview_changed_) {
      if (!active_preview_settings_.has_value()) {
        return failure("Merge preview settings are unavailable.");
      }
      const auto previous_settings = *active_preview_settings_;
      const auto *previous =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(&previous_settings);
      if (previous == nullptr) {
        return failure("Merge preview settings are invalid.");
      }
      // Structural actions stay fixed within a preview generation. Rebuild at
      // the original baseline when a tolerance edit adds or removes the weld.
      if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
        return failure("Merge preview rebuild failed.");
      }
      if (!restore_preview_to_original_baseline(preview.id, false)) {
        return {.ok = false, .changed = true,
                .message = "Merge preview could not be rebuilt."};
      }
      auto rebuilt = begin_component_merge_preview(
          quader_editor_core::EditorOperationId::MergeByDistance, tolerance);
      if (!rebuilt.ok && !preview_active()) {
        const auto restored = begin_component_merge_preview(
            quader_editor_core::EditorOperationId::MergeByDistance,
            static_cast<float>(previous->tolerance));
        if (!restored.ok) {
          return {.ok = false, .changed = true,
                  .message = rebuilt.message + " The previous preview could not be restored."};
        }
        active_preview_settings_ = previous_settings;
      }
      return rebuilt;
    }
    if (!preview_changed_) {
      active_preview_deleted_vertex_count_ = 0U;
      return {.ok = true, .message = "Merge preview unchanged."};
    }
    const std::size_t deleted_vertices = candidate.value().receipt.deleted.vertices.size();
    std::vector<modeling::SessionPreviewDocumentUpdate> updates;
    updates.push_back({
        .object = sources.survivor,
        .document = std::move(candidate.value().survivor_document),
        .receipt = std::move(candidate.value().receipt),
    });
    HostPublicationScope scope;
    if (!append_sdk_object_publication_scope(scope, sources.survivor, false)) {
      return failure("Merge preview survivor projection is unavailable.");
    }
    finalize_host_publication_scope(scope);
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::PreviewCheckpoint,
         .checkpoint_scope = ModelingHostPublicationCheckpointScope::MeshProjection,
         .capture_hierarchy = false,
         .affected_scene_entities = scope.scene_entities,
         .affected_asset_ids = scope.asset_ids,
         .affected_modeling_object_ids = scope.modeling_object_ids});
    if (!publication.ready()) {
      return failure("Merge preview candidate checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      ModelingCommandResult result =
          rollback_publication_result(publication, failure(std::move(message)));
      if (!result.changed && !preview_fence_matches(preview.id) &&
          !acknowledge_preview_owner_fence()) {
        result.changed = true;
        result.message += " Merge preview rollback could not be acknowledged.";
      }
      return result;
    };
    const auto updated = session_.update_preview_transaction(preview.id, std::move(updates));
    if (!updated.ok()) {
      return rollback(updated.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_preview_object()) {
      return rollback("Merge preview publication failed.");
    }
    active_preview_deleted_vertex_count_ = deleted_vertices;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return rollback("Merge preview could not be acknowledged.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true, .changed = true, .message = "Merge preview updated."});
  }

  [[nodiscard]] ModelingCommandResult begin_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    if (preview_active()) {
      return failure("An SDK modeling preview is already active.");
    }
    active_preview_deleted_vertex_count_ = 0U;
    if (operation == quader_editor_core::EditorOperationId::MergeByDistance &&
        selected_component_objects().size() > 1U) {
      const auto *merge =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(&settings);
      if (merge == nullptr) {
        return failure("Merge preview settings are invalid.");
      }
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(*merge);
      auto result = begin_component_merge_preview(
          operation, static_cast<float>(normalized.tolerance));
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    if (operation == quader_editor_core::EditorOperationId::HalfCut) {
      const auto *half_cut =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      if (half_cut == nullptr) {
        return failure("Half-Cut preview settings are invalid.");
      }
      ModelingCommandResult result = begin_half_cut_preview(*half_cut);
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    if (operation == quader_editor_core::EditorOperationId::Bridge &&
        selected_component_objects().size() > 1U) {
      const auto *bridge =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      if (bridge == nullptr) {
        return failure("Bridge preview settings are invalid.");
      }
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(*bridge);
      ModelingCommandResult result = begin_bridge_preview(normalized.steps);
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    const DocumentMutation mutation = preview_mutation_for(operation, settings);
    if (!mutation) {
      return failure("SDK operation does not support a document preview.");
    }
    const std::vector<ObjectId> component_objects =
        selected_component_objects();
    std::vector<ObjectId> preview_objects = component_objects;
    if (preview_objects.empty()) {
      const std::optional<ObjectId> object = active_sdk_object();
      if (object.has_value()) {
        preview_objects.push_back(*object);
      }
    }
    if (preview_objects.empty()) {
      return failure("No active SDK modeling selection.");
    }
    ModelingCommandResult result = begin_mesh_preview_transaction(
        preview_objects, mutation, sdk_operation_id(operation),
        "SDK operation preview failed.");
    if (result.ok) {
      active_preview_operation_ = operation;
      active_preview_settings_ = settings;
      if (operation == quader_editor_core::EditorOperationId::EdgeBevel) {
        accept_edge_bevel_preview_settings(settings, true);
      }
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult update_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    if (!active_preview_operation_.has_value() ||
        *active_preview_operation_ != operation) {
      return failure("Active SDK preview belongs to another operation.");
    }
    if (operation == quader_editor_core::EditorOperationId::MergeByDistance &&
        structural_preview_host_state_.has_value() &&
        structural_preview_host_state_->merge_sources.has_value()) {
      const auto *merge =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(&settings);
      if (merge == nullptr) {
        return failure("Merge preview settings are invalid.");
      }
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(*merge);
      auto result = update_component_merge_preview(static_cast<float>(normalized.tolerance));
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    if (operation == quader_editor_core::EditorOperationId::HalfCut) {
      const auto *half_cut =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      if (half_cut == nullptr) {
        return failure("Half-Cut preview settings are invalid.");
      }
      ModelingCommandResult result = update_half_cut_preview(*half_cut);
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    // Only cross-document Bridge owns structural participants; a Bridge within
    // one document must keep updating its original mesh preview transaction.
    if (operation == quader_editor_core::EditorOperationId::Bridge &&
        structural_preview_host_state_.has_value()) {
      const auto *bridge =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      if (bridge == nullptr) {
        return failure("Bridge preview settings are invalid.");
      }
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(*bridge);
      ModelingCommandResult result =
          update_bridge_preview(normalized.steps);
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    const DocumentMutation mutation = preview_mutation_for(operation, settings);
    if (!mutation) {
      return failure("SDK operation does not support a document preview.");
    }
    const auto previous_bevel = active_edge_bevel_settings();
    ModelingCommandResult result = update_mesh_preview_transaction(
        mutation, "SDK operation preview failed.");
    if (result.ok) {
      active_preview_settings_ = settings;
      if (operation == quader_editor_core::EditorOperationId::EdgeBevel) {
        const auto &bevel =
            std::get<quader_editor_core::EdgeBevelOperationSettings>(settings);
        const bool geometry_parameters_changed =
            !previous_bevel.has_value() ||
            previous_bevel->segments != bevel.segments ||
            previous_bevel->profile != bevel.profile ||
            previous_bevel->profile_type != bevel.profile_type;
        accept_edge_bevel_preview_settings(settings,
                                           geometry_parameters_changed);
      }
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult commit_operation_preview() {
    return commit_translate_preview();
  }

  [[nodiscard]] ModelingCommandResult cancel_operation_preview() {
    return cancel_translate_preview();
  }

  [[nodiscard]] std::string_view active_preview_operation_id() const noexcept {
    return active_preview_operation_id_;
  }

  [[nodiscard]] std::optional<quader_editor_core::EditorOperationId>
  active_preview_operation() const noexcept {
    return active_preview_operation_;
  }

  [[nodiscard]] std::optional<double>
  active_edge_bevel_maximum_width() const noexcept {
    return active_preview_operation_ ==
                   quader_editor_core::EditorOperationId::EdgeBevel
               ? active_edge_bevel_maximum_width_
               : std::nullopt;
  }

  [[nodiscard]] std::optional<quader_editor_core::EdgeBevelOperationSettings>
  active_edge_bevel_settings() const noexcept {
    if (active_preview_operation_ !=
            quader_editor_core::EditorOperationId::EdgeBevel ||
        !active_preview_settings_.has_value()) return std::nullopt;
    const auto *settings =
        std::get_if<quader_editor_core::EdgeBevelOperationSettings>(
            &*active_preview_settings_);
    return settings != nullptr ? std::optional(*settings) : std::nullopt;
  }

  void set_edge_bevel_preview_settings_handler(
      std::function<void(const quader_editor_core::EdgeBevelOperationSettings &)>
          handler) {
    edge_bevel_preview_settings_handler_ = std::move(handler);
  }

  void accept_edge_bevel_preview_settings(
      const quader_editor_core::EditorOperationSettings &settings,
      bool reset_limit) {
    auto accepted =
        std::get<quader_editor_core::EdgeBevelOperationSettings>(settings);
    if (reset_limit) active_edge_bevel_maximum_width_.reset();
    const auto feedback = session_.preview_edge_bevel_width();
    if (feedback.has_value() &&
        feedback->applied_width < feedback->requested_width) {
      accepted.width = static_cast<double>(feedback->applied_width);
      active_edge_bevel_maximum_width_ = accepted.width;
    }
    active_preview_settings_ = accepted;
    if (edge_bevel_preview_settings_handler_) {
      edge_bevel_preview_settings_handler_(accepted);
    }
  }

  [[nodiscard]] std::size_t
  active_preview_deleted_vertex_count() const noexcept {
    return active_preview_deleted_vertex_count_;
  }

  void bind_editor_mode_service(EditorModeService *service) noexcept {
    editor_mode_service_ = service;
  }

  [[nodiscard]] bool edit_scope_contains(int native_id) const {
    if (editor_mode_service_ == nullptr) {
      return true;
    }
    const auto entity = object_map_.entity_for_modeling_object(native_id);
    return editor_mode_service_->mode() == EditorMode::Edit &&
           entity.has_value() && editor_mode_service_->contains(*entity);
  }

  [[nodiscard]] ModelingCommandResult apply_editor_mode_context(
      const EditorModeSnapshot &context,
      std::span<const EntityId> first_admission_entities) {
    if (preview_active()) {
      return failure("Accept or cancel the active operation before changing mode.");
    }
    std::vector<ObjectId> selected;
    std::vector<EntityId> entities;
    std::vector<modeling::SessionComponentSelectionUpdate> updates;
    int active_native = kInvalidModelingObjectId;
    const auto append = [&](EntityId entity) {
      const auto native = object_map_.modeling_object_for_entity(entity);
      const auto object = native.has_value() ? sdk_object(*native) : std::nullopt;
      if (!object.has_value() || !scene_.exists(entity)) {
        return false;
      }
      selected.push_back(*object);
      entities.push_back(entity);
      if (entity == context.active_entity) {
        active_native = *native;
        std::rotate(selected.begin(), std::prev(selected.end()), selected.end());
      }
      return true;
    };
    if (context.mode == EditorMode::Edit) {
      for (const auto &participant : context.participants) {
        if (!append(participant.entity)) {
          return failure("Edit participant is no longer a live modeling mesh.");
        }
        const auto object = sdk_object(participant.modeling_object_id);
        if (!object.has_value()) {
          return failure("Edit participant has no portable modeling identity.");
        }
        auto document = session_.document_copy(*object);
        if (!document.ok()) {
          return failure(document.error_message());
        }
        if (std::ranges::find(first_admission_entities, participant.entity) !=
            first_admission_entities.end()) {
          auto cleared = document.value().clear_selection();
          if (!cleared.ok()) {
            return failure(cleared.error_message());
          }
        }
        auto converted = document.value().set_selection_mode(
            to_sdk_component_mode(context.component_kind));
        if (!converted.ok()) {
          return failure(converted.error_message());
        }
        updates.push_back({.object = *object,
                           .selection = document.value().selection()});
      }
    } else {
      for (const EntityId entity : context.object_results) {
        if (!scene_.exists(entity)) {
          return failure("Object mode result is no longer a live scene object.");
        }
        if (!append(entity)) {
          entities.push_back(entity);
        }
      }
    }
    if (active_native == kInvalidModelingObjectId && !selected.empty()) {
      active_native = native_object(selected.front()).value_or(kInvalidModelingObjectId);
    }
    std::vector<ObjectId> affected;
    for (const auto &object : session_.objects()) {
      affected.push_back(object.id);
    }
    auto host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = affected.empty() ? ModelingSdkRollbackPolicy::None
                                         : ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope = ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene_entities = false,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .capture_scene_selection = true,
         .affected_sdk_object_ids = affected});
    if (!publication.ready()) {
      return failure("Editor mode selection checkpoint is unavailable.");
    }
    auto committed = session_.commit_component_selection_batch(
        updates, selected, selected.empty() ? ObjectId{} : selected.front());
    if (!committed.ok()) {
      return rollback_publication_result(publication, failure(committed.error_message()));
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
      return rollback_publication_result(publication,
          failure("Injected failure during editor mode selection publication."));
    }
    const auto desired = make_scene_selection(scene_, entities, context.active_entity);
    (void)apply_scene_selection(scene_, desired, SceneSelectionApplicationMode::RuntimeSync);
    if (!same_scene_selection(capture_scene_selection(scene_), desired)) {
      return rollback_publication_result(publication,
          failure("Editor mode selection could not be published."));
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSelection) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback_publication_result(publication,
          failure("Injected failure before editor mode publication commit."));
    }
    active_native_object_id_ = active_native;
    selection_mode_ = context.mode == EditorMode::Edit
                          ? context.component_kind : ModelingSelectionMode::Object;
    ++mode_revision_;
    return commit_publication_result<ModelingCommandResult>(
        publication, {.ok = true, .changed = true});
  }

  [[nodiscard]] ModelingCommandResult
  set_object_selection(std::span<const EntityId> entities, EntityId active,
                       bool publish_scene = true,
                       bool preserve_selection_mode = false) {
    if (editor_mode_service_ != nullptr &&
        editor_mode_service_->mode() == EditorMode::Edit) {
      return failure("Object selection cannot replace the current Edit participants.");
    }
    std::vector<ObjectId> selected;
    selected.reserve(entities.size());
    int active_native = kInvalidModelingObjectId;
    for (const EntityId entity : entities) {
      const std::optional<int> native =
          object_map_.modeling_object_for_entity(entity);
      if (!native.has_value()) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(*native);
      if (!object.has_value()) {
        continue;
      }
      selected.push_back(*object);
      if (entity == active) {
        active_native = *native;
      }
    }
    if (selected.empty() && !entities.empty()) {
      return failure("Selection does not contain SDK modeling objects.");
    }
    if (active_native == kInvalidModelingObjectId && !selected.empty()) {
      active_native = native_object(selected.front()).value_or(
          kInvalidModelingObjectId);
    }
    if (active_native != kInvalidModelingObjectId) {
      const std::optional<ObjectId> active_object = sdk_object(active_native);
      const auto found = active_object.has_value()
                             ? std::find(selected.begin(), selected.end(),
                                         *active_object)
                             : selected.end();
      if (found != selected.end() && found != selected.begin()) {
        std::rotate(selected.begin(), found, std::next(found));
      }
    }

    const std::vector<quader::modeling::SessionObjectSummary> current_objects =
        session_.objects();
    std::unordered_set<std::uint64_t> selected_ids;
    selected_ids.reserve(selected.size());
    for (const ObjectId object : selected) {
      selected_ids.insert(sdk_object_key(object));
    }
    const std::size_t current_selected_count = static_cast<std::size_t>(
        std::ranges::count_if(current_objects,
                              [](const auto &object) {
                                return object.selected;
                              }));
    const bool same_selection =
        current_selected_count == selected_ids.size() &&
        std::ranges::all_of(current_objects, [&](const auto &object) {
          return object.selected ==
                 selected_ids.contains(sdk_object_key(object.id));
        });
    std::vector<EntityId> projected_entities;
    if (publish_scene) {
      projected_entities.reserve(entities.size());
      for (const EntityId entity : entities) {
        if (!owns_entity(entity)) {
          continue;
        }
        if (!scene_.exists(entity)) {
          return failure("SDK scene selection could not be projected.");
        }
        projected_entities.push_back(entity);
      }
    }
    const EntityId projected_active =
        projected_entities.empty() ? kInvalidEntityId
                                   : projected_entities.back();
    const SceneSelectionState desired_scene_selection =
        make_scene_selection(scene_, projected_entities, projected_active);
    const bool scene_selection_changed =
        publish_scene &&
        !same_scene_selection(capture_scene_selection(scene_),
                              desired_scene_selection);
    const bool adapter_selection_changed =
        active_native_object_id_ != active_native ||
        (!preserve_selection_mode &&
         selection_mode_ != ModelingSelectionMode::Object);
    if (same_selection && !scene_selection_changed &&
        !adapter_selection_changed) {
      return {.ok = true, .changed = false};
    }

    if (curve_document_preview_active()) {
      return set_object_selection_during_curve_preview(
          selected, active_native, desired_scene_selection,
          scene_selection_changed, adapter_selection_changed,
          preserve_selection_mode);
    }

    std::vector<ObjectId> mutation_participants;
    mutation_participants.reserve(current_objects.size());
    std::ranges::transform(current_objects,
                           std::back_inserter(mutation_participants),
                           &modeling::SessionObjectSummary::id);

    AdapterPublicationState host_before = capture_adapter_publication_state();
    const ModelingSdkRollbackPolicy sdk_rollback =
        current_objects.empty() ? ModelingSdkRollbackPolicy::None
                                : ModelingSdkRollbackPolicy::MutationCheckpoint;
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = sdk_rollback,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = publish_scene,
         .capture_scene_entities = false,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .capture_scene_selection = publish_scene,
         .affected_sdk_object_ids = mutation_participants});
    if (!publication.ready()) {
      return failure("SDK object-selection checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    Result<OperationResult> result =
        current_objects.empty()
            ? Result<OperationResult>::success(OperationResult{})
            : session_.apply_object_selection(selected, SelectionEdit::Replace);
    if (!result.ok()) {
      return rollback(result.error_message());
    }
    if (!result.value().changed && !scene_selection_changed &&
        !adapter_selection_changed) {
      return rollback_publication_result(publication, from_sdk(result));
    }
    if (result.value().changed &&
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback(
          "Injected failure after SDK selection mutation.");
    }

    if (scene_selection_changed) {
      if (fails(
              ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
        return rollback(
            "Injected failure during SDK selection publication.");
      }
      QDR_PROFILE_SCOPE("Object selection atomic scene publication");
      (void)apply_scene_selection(scene_, desired_scene_selection,
                                  SceneSelectionApplicationMode::RuntimeSync);
      if (!same_scene_selection(capture_scene_selection(scene_),
                                desired_scene_selection)) {
        return rollback("SDK scene selection could not be published.");
      }
    }
    active_native_object_id_ = active_native;
    if (!preserve_selection_mode) {
      if (selection_mode_ != ModelingSelectionMode::Object) {
        ++mode_revision_;
      }
      selection_mode_ = ModelingSelectionMode::Object;
    }
    result.value().changed = result.value().changed ||
                             scene_selection_changed ||
                             adapter_selection_changed;
    return commit_publication_result(publication, from_sdk(result));
  }

  [[nodiscard]] ModelingCommandResult
  set_selection_mode(ModelingSelectionMode mode) {
    if (editor_mode_service_ != nullptr &&
        !editor_mode_service_->permits_selection_mode(mode)) {
      return failure("Selection kind is unavailable in the current editor mode.");
    }
    if (mode == selection_mode_) {
      return {.ok = true, .changed = false};
    }
    if (mode == ModelingSelectionMode::Object ||
        mode == ModelingSelectionMode::Group) {
      selection_mode_ = mode;
      ++mode_revision_;
      return {.ok = true, .changed = true};
    }
    const auto summaries = session_.objects();
    if (std::ranges::any_of(summaries, [](const auto &object) {
          return object.selected && object.kind == modeling::SessionObjectKind::Curve;
        })) {
      std::vector<EntityId> meshes;
      EntityId active_mesh = kInvalidEntityId;
      for (const auto &object : summaries) {
        if (!object.selected || object.kind != modeling::SessionObjectKind::Mesh) {
          continue;
        }
        const auto native = native_object(object.id);
        const auto entity = native.has_value()
                                ? object_map_.entity_for_modeling_object(*native)
                                : std::nullopt;
        if (entity.has_value()) {
          meshes.push_back(*entity);
          if (*native == active_native_object_id_) {
            active_mesh = *entity;
          }
        }
      }
      auto selected = set_object_selection(meshes, active_mesh);
      if (!selected.ok) {
        return selected;
      }
    }
    std::vector<int> targets;
    targets.reserve(summaries.size());
    for (const auto &object : summaries) {
      if (!object.selected ||
          object.kind != modeling::SessionObjectKind::Mesh) {
        continue;
      }
      const std::optional<int> native = native_object(object.id);
      if (native.has_value()) {
        targets.push_back(*native);
      }
    }
    const auto active =
        std::find(targets.begin(), targets.end(), active_native_object_id_);
    if (active != targets.end()) {
      std::rotate(active, std::next(active), targets.end());
    }
    if (targets.empty()) {
      selection_mode_ = mode;
      ++mode_revision_;
      return {.ok = true, .changed = true};
    }

    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(targets.size());
    for (const int target : targets) {
      requests.push_back({
          .modeling_object_id = target,
          .mode = mode,
          .edit = ModelingSelectionEdit::Add,
      });
    }
    ModelingCommandResult result = set_component_selections(requests);
    if (result.ok && !result.changed) {
      result.changed = true;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult set_component_selection(
      const ModelingComponentSelectionRequest &request) {
    const std::span<const ModelingComponentSelectionRequest> requests(
        &request, 1U);
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult
  select_mesh_ngons(std::span<const int> modeling_object_ids) {
    if (editor_mode_service_ != nullptr &&
        editor_mode_service_->mode() != EditorMode::Edit) {
      return failure("Select N-Gons requires Edit Mode.");
    }

    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(modeling_object_ids.size());
    std::unordered_set<int> visited;
    for (const int native_id : modeling_object_ids) {
      if (!visited.insert(native_id).second) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (!object.has_value()) {
        return failure("SDK modeling object ID is not live.");
      }
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      Result<AuthoredTopology> topology = copied.value().authored_topology();
      if (!topology.ok()) {
        return failure(topology.error_message());
      }

      ModelingComponentSelectionRequest request;
      request.modeling_object_id = native_id;
      request.mode = ModelingSelectionMode::Face;
      request.edit = ModelingSelectionEdit::Replace;
      for (const auto &face : topology.value().faces) {
        if (face.vertices.size() > 4U) {
          request.faces.push_back(static_cast<int>(face.id.index));
        }
      }
      if (!request.faces.empty()) {
        request.active_kind = ModelingActiveSelectionKind::Face;
        request.active_face = request.faces.back();
      }
      requests.push_back(std::move(request));
    }
    if (requests.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "No directly editable mesh is selected."};
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult set_component_selections(
      std::span<const ModelingComponentSelectionRequest> requests) {
    // Membership belongs to the mode context. A selection request cannot enroll
    // another mesh, and validation must not settle or suspend an active draft.
    for (const auto &request : requests) {
      if (!edit_scope_contains(request.modeling_object_id) ||
          request.mode == ModelingSelectionMode::Object ||
          request.mode == ModelingSelectionMode::Group ||
          request.mode != requests.front().mode) {
        return failure("Component selection requires current Edit participants and one component kind.");
      }
    }
    std::vector<ModelingComponentSelectionRequest> scoped_requests;
    if (editor_mode_service_ != nullptr && !requests.empty() &&
        (requests.front().edit == ModelingSelectionEdit::Replace ||
         requests.front().mode != editor_mode_service_->snapshot().component_kind)) {
      // Replace clears other participants' components, never their membership.
      // Kind changes convert dormant selections in every untouched participant.
      for (const auto &participant : editor_mode_service_->snapshot().participants) {
        if (std::ranges::none_of(requests, [&](const auto &request) {
              return request.modeling_object_id == participant.modeling_object_id;
            })) {
          scoped_requests.push_back({.modeling_object_id = participant.modeling_object_id,
                                     .mode = requests.front().mode,
                                     .edit = requests.front().edit == ModelingSelectionEdit::Replace
                                                 ? ModelingSelectionEdit::Replace
                                                 : ModelingSelectionEdit::Add});
        }
      }
      scoped_requests.insert(scoped_requests.end(), requests.begin(), requests.end());
      requests = scoped_requests;
    }
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return failure(preview_edit.message);
    }
    if (requests.empty()) {
      return finish_operation_preview_selection_edit(
          preview_edit, {.ok = true, .changed = false});
    }

    /// Holds one validated selection replacement until the complete batch can
    /// be committed atomically.
    struct StagedSelectionDocument {
      ObjectId object{};
      std::optional<PolygonDocument> document;
      PolygonSelectionSnapshot selection;
    };

    // Box selection publishes one exact Replace snapshot per object. Let the
    // SDK validate and apply those snapshots once instead of first cloning and
    // selecting every document here, then repeating that same work in the SDK
    // atomic commit.
    bool exact_replace_batch = true;
    std::unordered_set<int> exact_replace_ids;
    exact_replace_ids.reserve(requests.size());
    for (const ModelingComponentSelectionRequest &request : requests) {
      if (request.edit != ModelingSelectionEdit::Replace ||
          !exact_replace_ids.emplace(request.modeling_object_id).second) {
        exact_replace_batch = false;
        break;
      }
    }

    const std::vector<quader::modeling::SessionObjectSummary> session_objects =
        session_.objects();
    std::unordered_map<int, std::size_t> session_index_by_native;
    session_index_by_native.reserve(session_objects.size());
    std::vector<bool> selected_by_index;
    selected_by_index.reserve(session_objects.size());
    for (std::size_t index = 0; index < session_objects.size(); ++index) {
      const std::optional<int> native = native_object(session_objects[index].id);
      if (native.has_value()) {
        session_index_by_native.emplace(*native, index);
      }
      selected_by_index.push_back(
          editor_mode_service_ != nullptr
              ? native.has_value() && edit_scope_contains(*native)
              : session_objects[index].selected);
    }

    std::vector<StagedSelectionDocument> staged_documents;
    staged_documents.reserve(requests.size());
    std::unordered_map<int, std::size_t> staged_index_by_native;
    staged_index_by_native.reserve(requests.size());
    std::vector<ObjectId> staged_objects;
    staged_objects.reserve(requests.size());
    for (const ModelingComponentSelectionRequest &request : requests) {
      const std::optional<ObjectId> object =
          sdk_object(request.modeling_object_id);
      if (!object.has_value() ||
          !session_index_by_native.contains(request.modeling_object_id)) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure("SDK modeling object ID is not live."));
      }
      if (staged_index_by_native.contains(request.modeling_object_id)) {
        continue;
      }
      staged_index_by_native.emplace(request.modeling_object_id,
                                     staged_objects.size());
      staged_objects.push_back(*object);
    }
    if (exact_replace_batch) {
      for (const ObjectId object : staged_objects) {
        staged_documents.push_back({.object = object});
      }
    } else {
      Result<std::vector<PolygonDocument>> staged_copies =
          session_.document_copies(staged_objects);
      if (!staged_copies.ok()) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure(staged_copies.error_message()));
      }
      for (std::size_t index = 0; index < staged_objects.size(); ++index) {
        staged_documents.push_back({
            .object = staged_objects[index],
            .document = std::move(staged_copies.value()[index]),
        });
      }
    }
    std::string result_message;
    for (std::size_t request_index = 0; request_index < requests.size();
         ++request_index) {
      const ModelingComponentSelectionRequest &request =
          requests[request_index];
      const std::optional<ObjectId> object =
          sdk_object(request.modeling_object_id);
      const auto session_index =
          session_index_by_native.find(request.modeling_object_id);
      if (!object.has_value() || session_index == session_index_by_native.end()) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure("SDK modeling object ID is not live."));
      }

      const std::size_t staged_index =
          staged_index_by_native.at(request.modeling_object_id);
      const bool target_was_selected =
          selected_by_index[session_index->second];
      PolygonSelectionSnapshot selection;
      selection.kind = to_sdk_selection_kind(request.mode);
      selection.vertices.reserve(request.vertices.size());
      for (const int id : request.vertices) {
        if (id > 0) {
          selection.vertices.push_back(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(id)));
        }
      }
      selection.edges.reserve(request.edges.size());
      for (const auto [a, b] : request.edges) {
        if (a > 0 && b > 0 && a != b) {
          selection.edges.push_back(quader::modeling::make_edge_key(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(a)),
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(b))));
        }
      }
      selection.faces.reserve(request.faces.size());
      for (const int id : request.faces) {
        if (id > 0) {
          selection.faces.push_back(
              quader::modeling::make_id<quader::modeling::FaceTag>(
                  static_cast<std::uint32_t>(id)));
        }
      }
      selection.has_active = false;
      if (request.mode == ModelingSelectionMode::Vertex &&
          request.active_kind == ModelingActiveSelectionKind::Vertex &&
          request.active_vertex > 0) {
        selection.has_active = true;
        selection.active_vertex =
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_vertex));
      }
      if (request.mode == ModelingSelectionMode::Edge &&
          request.active_kind == ModelingActiveSelectionKind::Edge &&
          request.active_edge.first > 0 && request.active_edge.second > 0) {
        selection.has_active = true;
        selection.active_edge = quader::modeling::make_edge_key(
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_edge.first)),
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_edge.second)));
      }
      if (request.mode == ModelingSelectionMode::Face &&
          request.active_kind == ModelingActiveSelectionKind::Face &&
          request.active_face > 0) {
        selection.has_active = true;
        selection.active_face =
            quader::modeling::make_id<quader::modeling::FaceTag>(
                static_cast<std::uint32_t>(request.active_face));
      }

      StagedSelectionDocument &staged = staged_documents[staged_index];
      if (exact_replace_batch) {
        // Exact SDK snapshots require one canonical active component whenever
        // the selection is non-empty. Preserve an explicit caller choice and
        // synthesize only the missing per-object active value used by native
        // multi-object projections.
        if (!selection.has_active && selection.kind == SelectionKind::Vertex &&
            !selection.vertices.empty()) {
          selection.has_active = true;
          selection.active_vertex = selection.vertices.front();
        } else if (!selection.has_active &&
                   selection.kind == SelectionKind::Edge &&
                   !selection.edges.empty()) {
          selection.has_active = true;
          selection.active_edge = selection.edges.front();
        } else if (!selection.has_active &&
                   selection.kind == SelectionKind::Face &&
                   !selection.faces.empty()) {
          selection.has_active = true;
          selection.active_face = selection.faces.front();
        }
        staged.selection = std::move(selection);
      } else {
        PolygonDocument &document = *staged.document;
        // Native Add enrolls a fresh component participant. An inactive SDK
        // document can retain an earlier selection through object workflows;
        // that dormant payload must not become input to mode conversion.
        if (editor_mode_service_ == nullptr &&
            request.edit == ModelingSelectionEdit::Add && !target_was_selected) {
          Result<OperationResult> cleared = document.clear_selection();
          if (!cleared.ok()) {
            return finish_operation_preview_selection_edit(
                preview_edit, failure(cleared.error_message()));
          }
        }
        std::string mode_message;
        if (request.edit != ModelingSelectionEdit::Replace) {
          Result<OperationResult> mode = document.set_selection_mode(
              to_sdk_component_mode(request.mode));
          if (!mode.ok()) {
            return finish_operation_preview_selection_edit(
                preview_edit, failure(mode.error_message()));
          }
          mode_message = mode.value().message;
        }

        Result<OperationResult> selected =
            document.apply_selection(selection, to_sdk(request.edit));
        if (!selected.ok()) {
          return finish_operation_preview_selection_edit(
              preview_edit, failure(selected.error_message()));
        }
        if (!selected.value().message.empty()) {
          result_message = selected.value().message;
        } else if (!mode_message.empty()) {
          result_message = std::move(mode_message);
        }
        staged.selection = document.selection();
      }

      const PolygonSelectionSnapshot &effective_selection = staged.selection;
      const bool target_has_selection =
          !effective_selection.vertices.empty() ||
          !effective_selection.edges.empty() ||
          !effective_selection.faces.empty();
      if (editor_mode_service_ == nullptr && request_index == 0U &&
          request.edit == ModelingSelectionEdit::Replace) {
        std::fill(selected_by_index.begin(), selected_by_index.end(), false);
      }
      selected_by_index[session_index->second] =
          editor_mode_service_ != nullptr ||
          target_has_selection || request.retain_component_edit_target ||
          (request.edit == ModelingSelectionEdit::Add && target_was_selected);
    }

    const ModelingComponentSelectionRequest &final_request = requests.back();
    const std::size_t final_session_index =
        session_index_by_native.at(final_request.modeling_object_id);
    const ObjectId final_object = session_objects[final_session_index].id;
    std::vector<ObjectId> selected_objects;
    selected_objects.reserve(session_objects.size());
    if (selected_by_index[final_session_index]) {
      selected_objects.push_back(final_object);
    }
    for (std::size_t index = 0; index < session_objects.size(); ++index) {
      if (index != final_session_index && selected_by_index[index]) {
        selected_objects.push_back(session_objects[index].id);
      }
    }
    const ObjectId sdk_active =
        selected_objects.empty() ? ObjectId{} : selected_objects.front();

    std::vector<quader::modeling::SessionComponentSelectionUpdate> updates;
    updates.reserve(staged_documents.size());
    for (const StagedSelectionDocument &staged : staged_documents) {
      updates.push_back({
          .object = staged.object,
          .selection = staged.selection,
      });
    }

    std::vector<EntityId> selected_entities;
    selected_entities.reserve(selected_objects.size());
    EntityId final_entity = kInvalidEntityId;
    for (const ObjectId selected_object : selected_objects) {
      const std::optional<int> native = native_object(selected_object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!entity.has_value() || !scene_.exists(*entity)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            failure("SDK component selection scene projection failed."));
      }
      selected_entities.push_back(*entity);
      if (selected_object == final_object) {
        final_entity = *entity;
      }
    }
    const std::optional<EntityId> mapped_final_entity =
        object_map_.entity_for_modeling_object(
            final_request.modeling_object_id);
    if (!mapped_final_entity.has_value() ||
        !scene_.exists(*mapped_final_entity)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          failure("SDK component selection has no scene projection."));
    }
    const EntityId scene_active =
        final_entity != kInvalidEntityId
            ? final_entity
            : (selected_entities.empty() ? kInvalidEntityId
                                         : selected_entities.back());
    const SceneSelectionState desired_scene_selection =
        make_scene_selection(scene_, selected_entities, scene_active);
    const bool scene_selection_changed =
        !same_scene_selection(capture_scene_selection(scene_),
                              desired_scene_selection);
    const bool adapter_selection_changed =
        active_native_object_id_ != final_request.modeling_object_id ||
        selection_mode_ != final_request.mode;

    std::vector<ObjectId> mutation_participants;
    mutation_participants.reserve(session_objects.size());
    std::ranges::transform(session_objects,
                           std::back_inserter(mutation_participants),
                           &modeling::SessionObjectSummary::id);

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene_entities = false,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .capture_scene_selection = true,
         .affected_sdk_object_ids = mutation_participants});
    if (!publication.ready()) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          failure("SDK component-selection checkpoint is unavailable."));
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    Result<OperationResult> committed = [&]() {
      QDR_PROFILE_SCOPE("Component selection atomic SDK publication");
      return session_.commit_component_selection_batch(
          updates, selected_objects, sdk_active);
    }();
    if (!committed.ok()) {
      return finish_operation_preview_selection_edit(
          preview_edit, rollback(committed.error_message()));
    }
    if (!committed.value().changed && !scene_selection_changed &&
        !adapter_selection_changed) {
      if (!result_message.empty()) {
        committed.value().message = std::move(result_message);
      }
      return finish_operation_preview_selection_edit(
          preview_edit,
          commit_publication_result(publication, from_sdk(committed)));
    }
    if (committed.value().changed &&
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure after SDK component selection."));
    }
    if (fails(ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure during SDK component selection publication."));
    }
    {
      QDR_PROFILE_SCOPE("Component selection atomic scene publication");
      (void)apply_scene_selection(scene_, desired_scene_selection,
                                  SceneSelectionApplicationMode::RuntimeSync);
      if (!same_scene_selection(capture_scene_selection(scene_),
                                desired_scene_selection)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            rollback("SDK component selection scene publication failed."));
      }
    }

    active_native_object_id_ = final_request.modeling_object_id;
    if (editor_mode_service_ != nullptr) {
      editor_mode_service_->set_active_participant(*mapped_final_entity);
      editor_mode_service_->record_component_kind(final_request.mode);
    }
    if (selection_mode_ != final_request.mode) {
      ++mode_revision_;
    }
    selection_mode_ = final_request.mode;
    committed.value().changed = committed.value().changed ||
                                scene_selection_changed ||
                                adapter_selection_changed;
    session_.squash_undo_since(undo_mark);
    if (!result_message.empty()) {
      committed.value().message = std::move(result_message);
    }
    return finish_operation_preview_selection_edit(
        preview_edit,
        commit_publication_result(publication, from_sdk(committed)));
  }

  [[nodiscard]] ModelingCommandResult select_all() {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      std::vector<EntityId> entities;
      entities.reserve(sdk_bindings().size());
      for (const auto &[native_id, object_id] : sdk_bindings()) {
        (void)object_id;
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(native_id);
        if (entity.has_value() && scene_.exists(*entity)) {
          entities.push_back(*entity);
        }
      }
      return set_object_selection(
          entities, entities.empty() ? kInvalidEntityId : entities.back());
    }

    std::vector<int> targets;
    targets.reserve(sdk_bindings().size());
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      if (editor_mode_service_ != nullptr) {
        if (edit_scope_contains(native_id)) {
          targets.push_back(native_id);
        }
        continue;
      }
      const auto entity = object_map_.entity_for_modeling_object(native_id);
      if (entity.has_value() && scene_.curve(*entity) != nullptr) {
        continue;
      }
      Result<PolygonDocument> copied = session_.document_copy(object_id);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      const PolygonSelectionSnapshot selection = copied.value().selection();
      if (component_selection_participates_in_mode(selection,
                                                   selection_mode_)) {
        targets.push_back(native_id);
      }
    }
    const auto active =
        std::find(targets.begin(), targets.end(), active_native_object_id_);
    if (active != targets.end()) {
      std::rotate(active, std::next(active), targets.end());
    }
    if (targets.empty()) {
      return {.ok = true, .changed = false, .message = "Selection unchanged."};
    }
    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(targets.size());
    for (const int native_id : targets) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (!object.has_value()) {
        return failure("SDK modeling object ID is not live.");
      }
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      Result<AuthoredTopology> topology = copied.value().authored_topology();
      if (!topology.ok()) {
        return failure(topology.error_message());
      }
      ModelingComponentSelectionRequest request;
      request.modeling_object_id = native_id;
      request.mode = selection_mode_;
      request.edit = ModelingSelectionEdit::Replace;
      if (selection_mode_ == ModelingSelectionMode::Vertex) {
        for (const auto &vertex : topology.value().vertices) {
          request.vertices.push_back(static_cast<int>(vertex.id.index));
        }
        if (!request.vertices.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Vertex;
          request.active_vertex = request.vertices.back();
        }
      } else if (selection_mode_ == ModelingSelectionMode::Edge) {
        for (const EdgeKey edge : copied.value().edge_ids()) {
          request.edges.emplace_back(static_cast<int>(edge.a.index),
                                     static_cast<int>(edge.b.index));
        }
        if (!request.edges.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Edge;
          request.active_edge = request.edges.back();
        }
      } else {
        for (const auto &face : topology.value().faces) {
          request.faces.push_back(static_cast<int>(face.id.index));
        }
        if (!request.faces.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Face;
          request.active_face = request.faces.back();
        }
      }
      requests.push_back(std::move(request));
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult invert_selection(
      std::span<const ModelingComponentSelectionUniverse> universes) {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      return failure(
          "Object and Group selection inversion must be routed through "
          "EditorSelectionService.");
    }

    for (const auto &universe : universes) {
      if (!edit_scope_contains(universe.modeling_object_id)) {
        return failure("Selection inversion requires a current Edit participant.");
      }
    }
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return failure(preview_edit.message);
    }
    if (universes.empty()) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          {.ok = true, .changed = false, .message = "Selection unchanged."});
    }

    std::vector<SessionComponentSelectionUniverse> sdk_universes;
    sdk_universes.reserve(universes.size());
    std::unordered_map<std::uint64_t, std::size_t> universe_by_object;
    universe_by_object.reserve(universes.size());
    const auto append_unique = []<typename Value>(std::vector<Value> &values,
                                                   Value value) {
      if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
      }
    };
    for (const ModelingComponentSelectionUniverse &universe : universes) {
      if (universe.mode != selection_mode_) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            failure("Component inversion universe mode does not match the "
                    "active selection mode."));
      }
      const std::optional<ObjectId> object =
          sdk_object(universe.modeling_object_id);
      if (!object.has_value()) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure("SDK modeling object ID is not live."));
      }

      const std::uint64_t object_key = sdk_object_key(*object);
      auto found = universe_by_object.find(object_key);
      if (found == universe_by_object.end()) {
        const std::size_t index = sdk_universes.size();
        universe_by_object.emplace(object_key, index);
        sdk_universes.push_back({
            .object = *object,
            .eligible = {.mode = to_sdk_component_mode(universe.mode)},
        });
        found = universe_by_object.find(object_key);
      }
      PolygonSelectionUniverse &eligible =
          sdk_universes[found->second].eligible;
      for (const int vertex : universe.vertices) {
        if (vertex <= 0) {
          return finish_operation_preview_selection_edit(
              preview_edit,
              failure("Component inversion contains an invalid vertex ID."));
        }
        append_unique(
            eligible.vertices,
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(vertex)));
      }
      for (const auto [first, second] : universe.edges) {
        if (first <= 0 || second <= 0 || first == second) {
          return finish_operation_preview_selection_edit(
              preview_edit,
              failure("Component inversion contains an invalid edge ID."));
        }
        append_unique(
            eligible.edges,
            quader::modeling::make_edge_key(
                quader::modeling::make_id<quader::modeling::VertexTag>(
                    static_cast<std::uint32_t>(first)),
                quader::modeling::make_id<quader::modeling::VertexTag>(
                    static_cast<std::uint32_t>(second))));
      }
      for (const int face : universe.faces) {
        if (face <= 0) {
          return finish_operation_preview_selection_edit(
              preview_edit,
              failure("Component inversion contains an invalid face ID."));
        }
        append_unique(
            eligible.faces,
            quader::modeling::make_id<quader::modeling::FaceTag>(
                static_cast<std::uint32_t>(face)));
      }
    }
    if (sdk_universes.empty()) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          {.ok = true, .changed = false, .message = "Selection unchanged."});
    }

    const SceneSelectionState scene_selection_before =
        capture_scene_selection(scene_);
    std::vector<EntityId> preserved_scene_only_entities;
    preserved_scene_only_entities.reserve(
        scene_selection_before.selected.size());
    for (const EntityId entity : scene_selection_before.selected) {
      // Formal instance occurrences are atomic targets only in Object/Group
      // mode. Component editing publishes definition-member entities, so an
      // occurrence is not a scene-only companion to preserve here.
      if (scene_.modeling_object(entity) == nullptr &&
          scene_.instance_occurrence(entity) == nullptr) {
        preserved_scene_only_entities.push_back(entity);
      }
    }
    const EntityId preserved_scene_only_active =
        std::find(preserved_scene_only_entities.begin(),
                  preserved_scene_only_entities.end(),
                  scene_selection_before.active) !=
                preserved_scene_only_entities.end()
            ? scene_selection_before.active
            : kInvalidEntityId;

    const std::vector<quader::modeling::SessionObjectSummary> before_objects =
        session_.objects();
    std::vector<ObjectId> mutation_participants;
    mutation_participants.reserve(before_objects.size());
    std::ranges::transform(before_objects,
                           std::back_inserter(mutation_participants),
                           &modeling::SessionObjectSummary::id);

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene_entities = false,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .capture_scene_selection = true,
         .affected_sdk_object_ids = mutation_participants});
    if (!publication.ready()) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          failure("SDK component-inversion checkpoint is unavailable."));
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    Result<OperationResult> inverted = [&]() {
      QDR_PROFILE_SCOPE("Component inversion atomic SDK mutation");
      return session_.invert_component_selection_batch(sdk_universes);
    }();
    if (!inverted.ok()) {
      return finish_operation_preview_selection_edit(
          preview_edit, rollback(inverted.error_message()));
    }
    if (inverted.value().changed &&
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure after SDK component inversion."));
    }

    if (!synchronize_edit_membership(false)) {
      return finish_operation_preview_selection_edit(
          preview_edit, rollback("Edit membership could not be retained after inversion."));
    }
    const std::vector<quader::modeling::SessionObjectSummary> after_objects =
        session_.objects();
    std::vector<EntityId> selected_entities;
    selected_entities.reserve(after_objects.size() +
                              preserved_scene_only_entities.size());
    for (const quader::modeling::SessionObjectSummary &summary :
         after_objects) {
      if (!summary.selected) {
        continue;
      }
      const std::optional<int> native = native_object(summary.id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!entity.has_value() || !scene_.exists(*entity)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            rollback("SDK component inversion scene projection failed."));
      }
      selected_entities.push_back(*entity);
    }
    selected_entities.insert(selected_entities.end(),
                             preserved_scene_only_entities.begin(),
                             preserved_scene_only_entities.end());

    const ObjectId sdk_active = session_.active_object();
    int active_native = kInvalidModelingObjectId;
    EntityId active_entity = kInvalidEntityId;
    if (sdk_active.valid()) {
      const std::optional<int> native = native_object(sdk_active);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            rollback("SDK component inversion active-object projection "
                     "failed."));
      }
      active_native = *native;
      active_entity = *entity;
    }

    const SceneSelectionState desired_scene_selection = make_scene_selection(
        scene_, selected_entities,
        preserved_scene_only_active != kInvalidEntityId
            ? preserved_scene_only_active
            : active_entity);
    const bool scene_selection_changed =
        !same_scene_selection(scene_selection_before, desired_scene_selection);
    const bool adapter_selection_changed =
        active_native_object_id_ != active_native;
    if (!inverted.value().changed && !scene_selection_changed &&
        !adapter_selection_changed) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          commit_publication_result(publication, from_sdk(inverted)));
    }
    // The component selection still publishes when fixed Edit membership keeps
    // the scene selection unchanged; exercise that same rollback boundary.
    if (fails(ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure during SDK component inversion "
                   "publication."));
    }
    if (scene_selection_changed) {
      QDR_PROFILE_SCOPE("Component inversion atomic scene publication");
      (void)apply_scene_selection(scene_, desired_scene_selection,
                                  SceneSelectionApplicationMode::RuntimeSync);
      if (!same_scene_selection(capture_scene_selection(scene_),
                                desired_scene_selection)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            rollback("SDK component inversion scene publication failed."));
      }
    }
    active_native_object_id_ = active_native;
    inverted.value().changed = inverted.value().changed ||
                               scene_selection_changed ||
                               adapter_selection_changed;
    session_.squash_undo_since(undo_mark);
    return finish_operation_preview_selection_edit(
        preview_edit,
        commit_publication_result(publication, from_sdk(inverted)));
  }

  [[nodiscard]] ModelingCommandResult clear_selection() {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      return set_object_selection({}, kInvalidEntityId);
    }
    std::vector<ModelingComponentSelectionRequest> requests;
    for (const quader::modeling::SessionObjectSummary &object :
         session_.objects()) {
      if (!object.selected || object.kind != modeling::SessionObjectKind::Mesh) {
        continue;
      }
      const std::optional<int> native = native_object(object.id);
      if (native.has_value()) {
        requests.push_back({
            .modeling_object_id = *native,
            .mode = selection_mode_,
            .edit = ModelingSelectionEdit::Replace,
        });
      }
    }
    if (requests.empty()) {
      return {.ok = true, .changed = false};
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult
  remove_objects(std::span<const int> modeling_object_ids) {
    std::vector<std::pair<int, ObjectId>> removed;
    removed.reserve(modeling_object_ids.size());
    for (const int native_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (object.has_value()) {
        removed.emplace_back(native_id, *object);
      }
    }
    if (removed.empty()) {
      return {.ok = true, .changed = false};
    }
    std::vector<int> affected_modeling_ids;
    affected_modeling_ids.reserve(removed.size());
    for (const auto &[native_id, object_id] : removed) {
      (void)object_id;
      affected_modeling_ids.push_back(native_id);
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = false,
         .capture_hierarchy = false,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Object-removal publication checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    for (const auto &[native_id, object_id] : removed) {
      Result<OperationResult> result = session_.remove_object(object_id);
      if (!result.ok()) {
        return rollback(result.error_message());
      }
      object_map_.unbind_sdk_object(native_id);
      object_map_.unbind_object(native_id);
      if (active_native_object_id_ == native_id) {
        active_native_object_id_ = kInvalidModelingObjectId;
      }
    }
    if (sdk_bindings().empty()) {
      selection_mode_ = ModelingSelectionMode::Object;
    }
    ++persistent_content_revision_;
    clear_preview_checkpoints();
    if (!synchronize_edit_membership()) {
      return rollback("Edit membership could not be reconciled after removal.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true, .changed = true, .message = "Deleted SDK objects."});
  }

  [[nodiscard]] ModelingCommandResult apply_selection_projection_checked(
      const ModelingSelectionProjectionSnapshot &projected) {
    const ModelingSelectionMode projected_mode =
        from_native_selection_mode(projected.selection_mode());
    if (projected_mode == ModelingSelectionMode::Object) {
      std::vector<EntityId> selected;
      EntityId active = kInvalidEntityId;
      for (const quader_editor_core::PolygonObject &object :
           projected.objects()) {
        if (!owns_object(object.id) || !object.selected) {
          continue;
        }
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(object.id);
        if (!entity.has_value() || !scene_.exists(*entity)) {
          continue;
        }
        selected.push_back(*entity);
        if (object.id == projected.active_object_id()) {
          active = *entity;
        }
      }
      return set_object_selection(selected, active);
    }

    std::vector<const quader_editor_core::PolygonObject *> desired_objects;
    std::unordered_set<int> desired_ids;
    for (const quader_editor_core::PolygonObject &object :
         projected.objects()) {
      if (!owns_object(object.id) || object.selection.empty()) {
        continue;
      }
      desired_objects.push_back(&object);
      desired_ids.insert(object.id);
    }
    const auto active = std::find_if(
        desired_objects.begin(), desired_objects.end(),
        [&](const quader_editor_core::PolygonObject *object) {
          return object->id == projected.active_object_id();
        });
    if (active != desired_objects.end()) {
      std::rotate(active, std::next(active), desired_objects.end());
    }

    std::vector<ModelingComponentSelectionRequest> requests;
    for (const quader::modeling::SessionObjectSummary &summary :
         session_.objects()) {
      const std::optional<int> native = native_object(summary.id);
      if (!summary.selected || !native.has_value() ||
          desired_ids.contains(*native)) {
        continue;
      }
      requests.push_back({
          .modeling_object_id = *native,
          .mode = projected_mode,
          .edit = ModelingSelectionEdit::Replace,
      });
    }

    const auto append_request = [&](
                                    const quader_editor_core::PolygonObject
                                        &object) {
      ModelingComponentSelectionRequest request;
      request.modeling_object_id = object.id;
      request.mode = projected_mode;
      request.edit = ModelingSelectionEdit::Replace;
      for (const quader_poly::ElementId vertex : object.selection.vertices) {
        request.vertices.push_back(static_cast<int>(vertex));
      }
      for (const quader_poly::Edge edge : object.selection.edges) {
        request.edges.emplace_back(static_cast<int>(edge.a),
                                   static_cast<int>(edge.b));
      }
      for (const quader_poly::ElementId face : object.selection.faces) {
        request.faces.push_back(static_cast<int>(face));
      }
      if (object.selection.has_active) {
        switch (object.selection.active_kind) {
        case quader_poly::ElementKind::Vertex:
          request.active_kind = ModelingActiveSelectionKind::Vertex;
          request.active_vertex =
              static_cast<int>(object.selection.active_vertex);
          break;
        case quader_poly::ElementKind::Edge:
          request.active_kind = ModelingActiveSelectionKind::Edge;
          request.active_edge = {
              static_cast<int>(object.selection.active_edge.a),
              static_cast<int>(object.selection.active_edge.b),
          };
          break;
        case quader_poly::ElementKind::Face:
          request.active_kind = ModelingActiveSelectionKind::Face;
          request.active_face = static_cast<int>(object.selection.active_face);
          break;
        }
      }
      requests.push_back(std::move(request));
    };
    for (const quader_editor_core::PolygonObject *object : desired_objects) {
      append_request(*object);
    }
    if (requests.empty()) {
      return set_selection_mode(projected_mode);
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request,
              ModelingSelectionService &selection) {
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    ModelingPickSelectRequest effective_request = request;
    if (preview_edit.active && !preview_edit.uv_peel) {
      effective_request.preserve_selection_on_miss = true;
    }
    const ModelingSelectionProjectionApplyResult applied =
        selection.apply_pick_selection(projected, scene_, object_map_,
                                       effective_request);
    ModelingCommandResult published{
        .ok = applied.applied,
        .changed = false,
        .message = applied.applied
                       ? std::string{}
                       : "Resolved pick selection could not be applied.",
    };
    if (applied.applied && applied.changed) {
      published = apply_selection_projection_checked(applied.projection);
    }
    (void)finish_operation_preview_selection_edit(preview_edit, published);
    return selection_summary();
  }

  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             ModelingSelectionService &selection) {
    QDR_PROFILE_SCOPE("Modeling SDK box selection");
    const PreviewSelectionEditState preview_edit = [&]() {
      QDR_PROFILE_SCOPE("Box selection suspend operation preview");
      return suspend_operation_preview_for_selection_edit();
    }();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    const ModelingSelectionProjectionSnapshot &projected = [&]()
        -> const ModelingSelectionProjectionSnapshot & {
      QDR_PROFILE_SCOPE("Box selection build SDK selection projection");
      return selection_projection_snapshot();
    }();
    const quader_editor_core::RectangleSelectionState baseline = [&]() {
      QDR_PROFILE_SCOPE("Box selection capture SDK runtime baseline");
      return selection.rectangle_selection_state(projected);
    }();
    return box_select(request, baseline, selection, projected, preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection) {
    QDR_PROFILE_SCOPE("Modeling SDK box selection from baseline");
    const PreviewSelectionEditState preview_edit = [&]() {
      QDR_PROFILE_SCOPE("Box selection suspend operation preview");
      return suspend_operation_preview_for_selection_edit();
    }();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    const ModelingSelectionProjectionSnapshot &projected = [&]()
        -> const ModelingSelectionProjectionSnapshot & {
      QDR_PROFILE_SCOPE("Box selection build SDK selection projection");
      return selection_projection_snapshot();
    }();
    const quader_editor_core::RectangleSelectionState effective_baseline =
        [&]() {
          QDR_PROFILE_SCOPE("Box selection resolve SDK baseline");
          return preview_edit.active && !preview_edit.uv_peel
                     ? selection.rectangle_selection_state(projected)
                     : baseline;
        }();
    return box_select(request, effective_baseline, selection, projected,
                      preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection,
      const ModelingSelectionProjectionSnapshot &projected,
      const PreviewSelectionEditState &preview_edit) {
    QDR_PROFILE_SCOPE("Modeling SDK apply box selection");
    const ModelingSelectionProjectionApplyResult applied = [&]() {
      QDR_PROFILE_SCOPE("Box selection resolve SDK rectangle selection");
      return selection.apply_rectangle_selection(projected, scene_,
                                                  object_map_, request,
                                                  baseline);
    }();
    ModelingCommandResult published{.ok = true, .changed = false};
    if (applied.changed) {
      QDR_PROFILE_SCOPE("Box selection publish SDK native selection");
      published = apply_selection_projection_checked(applied.projection);
    }
    {
      QDR_PROFILE_SCOPE("Box selection finish operation preview edit");
      (void)finish_operation_preview_selection_edit(preview_edit, published);
    }
    {
      QDR_PROFILE_SCOPE("Box selection summarize SDK selection");
      return selection_summary();
    }
  }

  [[nodiscard]] ModelingCommandResult move_selection(QVec3 delta) {
    if (!finite(delta)) {
      return failure("Translation delta must be finite.");
    }
    ModelingTransform3 transform;
    transform.origin = delta;
    return transform_selection(transform, {});
  }

  [[nodiscard]] ModelingCommandResult
  transform_selection(const ModelingTransform3 &transform, QVec3 pivot) {
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform, pivot);
    const ModelingSelectionMode mode = selection_mode_;
    if (mode == ModelingSelectionMode::Object || mode == ModelingSelectionMode::Group) {
      const auto objects = session_.objects();
      if (std::ranges::any_of(objects, [](const auto &object) {
            return object.selected && object.kind == modeling::SessionObjectKind::Curve;
          })) {
        return transform_mixed_object_documents(sdk_transform);
      }
    }
    return mutate_selected_documents(
        [sdk_transform, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return transform_document_for_commit(document, sdk_transform);
          }
          const auto dormant = document.selection();
          auto selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          auto transformed = transform_document_for_commit(document, sdk_transform);
          if (!transformed.ok()) {
            return transformed;
          }
          auto restored = restore_dormant_selection(document, dormant, transformed.value());
          if (!restored.ok()) {
            return restored;
          }
          transformed.value().dirty.selection = false;
          return transformed;
        },
        "Transform selection failed.");
  }

  [[nodiscard]] ModelingCommandResult transform_mixed_object_documents(
      const modeling::Transform3 &transform) {
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    std::vector<ObjectId> affected_sdk_objects;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    std::vector<int> affected_modeling_ids;
    for (const auto &object : session_.objects()) {
      if (!object.selected) {
        continue;
      }
      affected_sdk_objects.push_back(object.id);
      const std::optional<int> native = native_object(object.id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value()) {
        return failure("Selected transform participant has no host identity.");
      }
      affected_modeling_ids.push_back(*native);
      affected_entities.push_back(*entity);
      if (const MeshComponent *mesh = scene_.mesh(*entity)) {
        affected_assets.push_back(mesh->mesh);
      }
      if (const CurveComponent *curve = scene_.curve(*entity)) {
        affected_assets.push_back(curve->curve);
      }
    }
    if (affected_sdk_objects.empty()) {
      return {.ok = true, .changed = false};
    }
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = false,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids,
         .affected_sdk_object_ids = affected_sdk_objects});
    if (!publication.ready()) {
      return failure("Transform publication checkpoint is unavailable.");
    }
    const auto undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    std::vector<ObjectId> changed_objects;
    for (const auto &object : session_.objects()) {
      if (!object.selected) {
        continue;
      }
      Result<OperationResult> committed;
      if (object.kind == modeling::SessionObjectKind::Curve) {
        auto document = session_.curve_document_copy(object.id);
        if (!document.ok()) {
          return rollback(document.error_message());
        }
        auto transformed = document.value().transform(transform);
        if (!transformed.ok()) {
          return rollback(transformed.error_message());
        }
        if (!transformed.value().changed) {
          continue;
        }
        committed = session_.commit_curve_document(
            object.id, std::move(document).value(), std::move(transformed).value());
      } else {
        auto document = session_.document_copy(object.id);
        if (!document.ok()) {
          return rollback(document.error_message());
        }
        auto transformed = apply_with_whole_object_face_selection(
            selection_mode_, document.value(),
            [&transform](PolygonDocument &mesh) {
              return transform_document_for_commit(mesh, transform);
            });
        if (!transformed.ok()) {
          return rollback(transformed.error_message());
        }
        if (!transformed.value().changed) {
          continue;
        }
        std::vector<SessionDocumentUpdate> updates;
        updates.push_back({.object = object.id, .document = std::move(document).value(),
                           .receipt = std::move(transformed).value()});
        committed = session_.commit_documents_batch(std::move(updates), true);
      }
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
      changed_objects.push_back(object.id);
    }
    if (changed_objects.empty()) {
      return commit_publication_result<ModelingCommandResult>(
          publication, {.ok = true, .changed = false});
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Transform selection could not be committed.");
    }
    for (const auto object : changed_objects) {
      if (!project_committed_object(publication, object)) {
        return rollback("Transform selection could not be projected.");
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Transform selection could not be committed.");
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true, .changed = true, .message = "Transformed selection."});
  }

  [[nodiscard]] ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot) {
    const ModelingSelectionMode mode = selection_mode_;
    return mutate_selected_documents(
        [material_slot, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.assign_selected_face_material_slot(material_slot);
          }
          const auto dormant = document.selection();
          auto selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          auto assigned = document.assign_selected_face_material_slot(material_slot);
          if (!assigned.ok()) {
            return assigned;
          }
          auto restored = restore_dormant_selection(document, dormant, assigned.value());
          if (!restored.ok()) {
            return restored;
          }
          assigned.value().dirty.selection = false;
          return assigned;
        },
        "Assign material slot failed.");
  }

  [[nodiscard]] ModelingSelectedFaceTextureSnapshot
  selected_face_texture_settings(bool include_whole_meshes = false) const {
    ++stats_.face_texture_summary_build_count;
    ModelingSelectedFaceTextureSnapshot result;
    const bool whole_meshes = include_whole_meshes &&
                              selection_mode_ == ModelingSelectionMode::Object;
    if (selection_mode_ != ModelingSelectionMode::Face && !whole_meshes) {
      return result;
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    const auto documents = [&]() -> Result<std::vector<PolygonDocument>> {
      if (!face_texture_gesture_active()) {
        return session_.document_copies(selected);
      }
      const auto preview = session_.preview_summary();
      std::vector<PolygonDocument> candidates;
      candidates.reserve(selected.size());
      for (const auto object : selected) {
        auto candidate = session_.preview_document_copy(preview.id, object);
        if (!candidate.ok()) {
          return Result<std::vector<PolygonDocument>>::failure(candidate.error());
        }
        candidates.push_back(std::move(candidate).value());
      }
      return Result<std::vector<PolygonDocument>>::success(std::move(candidates));
    }();
    if (!documents.ok()) {
      result.command = failure(documents.error_message());
      return result;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const auto &document = documents.value()[index];
      const PolygonSelectionSnapshot selection = document.selection();
      const std::vector<FaceId> faces =
          whole_meshes ? document.face_ids() : selection.faces;
      if (faces.empty()) {
        continue;
      }
      const auto native = native_object(selected[index]);
      const auto entity = native.has_value()
                              ? object_map_.entity_for_modeling_object(*native)
                              : std::nullopt;
      if (!native.has_value() || !entity.has_value() || !scene_.exists(*entity)) {
        result.command = failure("A selected face mesh is no longer live.");
        return result;
      }
      auto properties = document.face_texture_settings(faces);
      if (!properties.ok()) {
        result.command = failure(properties.error_message());
        return result;
      }
      std::ranges::sort(properties.value(), {},
                        &quader::modeling::FaceTextureSettings::face);
      result.objects.push_back({
          .modeling_object_id = *native,
          .entity = *entity,
          .faces = std::move(properties).value(),
      });
    }
    std::ranges::sort(result.objects, {},
                      &ModelingSelectedFaceTextureObject::modeling_object_id);
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_face_texture_updates(
      std::span<const ModelingFaceTextureUpdateBatch> batches) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture settings require face selection mode.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(batches.size());
    for (const auto &batch : batches) {
      const auto object = sdk_object(batch.modeling_object_id);
      if (!object.has_value() || batch.updates.empty() ||
          std::ranges::find(objects, *object) != objects.end()) {
        return failure("The selected texture targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    return mutate_documents_by_object(
        objects,
        [&objects, batches](ObjectId object, PolygonDocument &document) {
          const std::size_t index = static_cast<std::size_t>(
              std::ranges::find(objects, object) - objects.begin());
          return document.set_face_texture_transforms(batches[index].updates);
        },
        "Edit face texture settings failed.", false, false, false, true);
  }

  template <typename PreparedDocument>
  [[nodiscard]] ModelingCommandResult publish_prepared_documents(
      std::span<const ObjectId> objects,
      const std::vector<PreparedDocument> &prepared_documents,
      std::string_view fallback_message) {
    return mutate_documents_by_object(
        objects,
        [&](ObjectId object, PolygonDocument &document) {
          const auto native = native_object(object);
          const auto entry = std::ranges::find(
              prepared_documents,
              static_cast<std::uint64_t>(native.value_or(-1)),
              &PreparedDocument::key);
          if (entry == prepared_documents.end()) {
            return Result<OperationResult>::failure(modeling::make_error(
                modeling::ErrorCode::InvalidId,
                "A prepared UV target is no longer live."));
          }
          document = entry->document;
          return Result<OperationResult>::success(entry->receipt);
        },
        fallback_message, false, false, false, true);
  }

  [[nodiscard]] ModelingCommandResult apply_face_texture_action(
      std::span<const ModelingFaceTextureActionTarget> targets,
      const modeling::FaceTextureActionOptions &options) {
    const bool projection =
        modeling_face_texture_action_is_projection(options.action);
    if (selection_mode_ != ModelingSelectionMode::Face &&
        !(projection && selection_mode_ == ModelingSelectionMode::Object)) {
      return failure(
          "Texture actions require face mode, or mesh mode for UV projection.");
    }
    if (targets.empty()) {
      return failure("Select one or more editable faces first.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(targets.size());
    for (const auto &target : targets) {
      const auto object = sdk_object(target.modeling_object_id);
      if (!object.has_value() || target.faces.empty() ||
          std::ranges::find(objects, *object) != objects.end()) {
        return failure("The selected texture targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    // These COW copies remain stable throughout the one selection-wide SDK
    // preparation. No SDK/native scene state is published until every target
    // has produced its validated replacement and receipt.
    auto documents = session_.document_copies(objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    std::vector<modeling::FaceTextureActionTarget> portable_targets;
    portable_targets.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const auto &target = targets[index];
      portable_targets.push_back({
          .key = static_cast<std::uint64_t>(target.modeling_object_id),
          .document = &documents.value()[index],
          .faces = target.faces,
          .world_from_local = to_sdk(target.world_from_local),
          .sampling_from_local = to_sdk(target.sampling_from_local),
      });
    }
    auto prepared = modeling::prepare_face_texture_action(portable_targets, options);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    return publish_prepared_documents(objects, prepared.value().documents,
                                      "Edit face texture failed.");
  }

  [[nodiscard]] ModelingCommandResult transfer_face_texture(
      const ModelingFaceTextureTransferTarget &source,
      const ModelingFaceTextureTransferTarget &target,
      std::uint32_t target_material_slot) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture transfer requires face selection mode.");
    }
    const auto source_object = sdk_object(source.modeling_object_id);
    const auto target_object = sdk_object(target.modeling_object_id);
    if (!source_object.has_value() || !target_object.has_value() ||
        !source.face.valid() || !target.face.valid()) {
      return failure("A texture transfer face is no longer live.");
    }
    std::vector<ObjectId> snapshot_objects{*source_object};
    if (*target_object != *source_object) {
      snapshot_objects.push_back(*target_object);
    }
    auto documents = session_.document_copies(snapshot_objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    const modeling::FaceTextureTransferFace source_face{
        .key = static_cast<std::uint64_t>(source.modeling_object_id),
        .document = &documents.value().front(),
        .face = source.face,
        .world_from_local = to_sdk(source.world_from_local),
        .sampling_from_local = to_sdk(source.sampling_from_local),
    };
    const modeling::FaceTextureTransferFace target_face{
        .key = static_cast<std::uint64_t>(target.modeling_object_id),
        .document = &documents.value().back(),
        .face = target.face,
        .world_from_local = to_sdk(target.world_from_local),
        .sampling_from_local = to_sdk(target.sampling_from_local),
    };
    auto prepared = modeling::prepare_face_texture_transfer(
        source_face, target_face, target_material_slot);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    const std::array<ObjectId, 1> targets{*target_object};
    return publish_prepared_documents(targets, prepared.value().documents,
                                      "Transfer face texture failed.");
  }

  [[nodiscard]] modeling::Result<modeling::FaceTextureCanvasChart>
  face_texture_canvas(const ModelingFaceTextureTransferTarget &target) const {
    ++stats_.face_texture_canvas_build_count;
    const auto object = sdk_object(target.modeling_object_id);
    if (!object.has_value()) {
      return modeling::Result<modeling::FaceTextureCanvasChart>::failure(
          {.code = modeling::ErrorCode::InvalidId,
           .message = "The face texture target is no longer live."});
    }
    ++stats_.face_texture_canvas_document_query_count;
    auto document = face_texture_gesture_active()
        ? session_.preview_document_copy(session_.preview_summary().id, *object)
        : session_.document_copy(*object);
    if (!document.ok()) {
      return modeling::Result<modeling::FaceTextureCanvasChart>::failure(document.error());
    }
    ++stats_.face_texture_canvas_document_copy_count;
    return modeling::query_face_texture_canvas({
        .key = static_cast<std::uint64_t>(target.modeling_object_id),
        .document = &document.value(),
        .face = target.face,
        .world_from_local = to_sdk(target.world_from_local),
        .sampling_from_local = to_sdk(target.sampling_from_local)});
  }

  [[nodiscard]] std::vector<modeling::Result<modeling::FaceTextureCanvasChart>>
  face_texture_canvases(
      std::span<const ModelingFaceTextureActionTarget> targets) const {
    std::size_t face_count = 0;
    for (const auto &target : targets) face_count += target.faces.size();
    std::vector<modeling::Result<modeling::FaceTextureCanvasChart>> charts;
    charts.reserve(face_count);
    visit_face_texture_canvases(targets,
        [&](std::size_t, std::size_t, auto chart) {
          charts.push_back(std::move(chart));
          return true;
        });
    return charts;
  }

  void visit_face_texture_canvases(
      std::span<const ModelingFaceTextureActionTarget> targets,
      const FaceTextureCanvasVisitor &visitor) const {
    QDR_PROFILE_SCOPE("Face texture canvas batch");
    if (!visitor) return;
    using ChartResult = modeling::Result<modeling::FaceTextureCanvasChart>;
    std::vector<ObjectId> objects;
    std::vector<std::optional<std::size_t>> target_document_indices;
    std::unordered_map<int, std::optional<std::size_t>> document_by_native;
    objects.reserve(targets.size());
    target_document_indices.reserve(targets.size());
    document_by_native.reserve(targets.size());
    for (const auto &target : targets) {
      if (target.faces.empty()) {
        target_document_indices.push_back(std::nullopt);
        continue;
      }
      const auto [found, inserted] = document_by_native.try_emplace(
          target.modeling_object_id, std::nullopt);
      if (inserted) {
        const auto object = sdk_object(target.modeling_object_id);
        if (object.has_value()) {
          found->second = objects.size();
          objects.push_back(*object);
        }
      }
      target_document_indices.push_back(found->second);
    }

    // One committed inventory lookup supplies immutable COW documents for the
    // complete selection. The single-face gesture uses its live preview instead.
    std::vector<Result<PolygonDocument>> documents;
    documents.reserve(objects.size());
    const bool gesture_active = face_texture_gesture_active();
    if (!gesture_active && objects.size() > 1u) {
      ++stats_.face_texture_canvas_document_query_count;
      auto copied = session_.document_copies(objects);
      if (copied.ok()) {
        stats_.face_texture_canvas_document_copy_count += copied.value().size();
        for (auto &document : copied.value()) {
          documents.push_back(Result<PolygonDocument>::success(std::move(document)));
        }
      }
    }
    // Single-object and preview queries need no inventory index. On a failed
    // batch, isolate stale objects so the other face results remain available.
    if (documents.empty() && !objects.empty()) {
      const auto preview = gesture_active ? session_.preview_summary().id
                                          : modeling::PreviewId{};
      for (const auto object : objects) {
        ++stats_.face_texture_canvas_document_query_count;
        auto document = gesture_active
            ? session_.preview_document_copy(preview, object)
            : session_.document_copy(object);
        if (document.ok()) {
          ++stats_.face_texture_canvas_document_copy_count;
        }
        documents.push_back(std::move(document));
      }
    }

    for (std::size_t target_index = 0; target_index < targets.size(); ++target_index) {
      const auto &target = targets[target_index];
      const auto document_index = target_document_indices[target_index];
      const auto world = to_sdk(target.world_from_local);
      const auto sampling = to_sdk(target.sampling_from_local);
      for (std::size_t face_index = 0; face_index < target.faces.size(); ++face_index) {
        const auto &face = target.faces[face_index];
        ++stats_.face_texture_canvas_build_count;
        ChartResult chart;
        if (!document_index.has_value()) {
          chart = ChartResult::failure({
              .code = modeling::ErrorCode::InvalidId,
              .message = "The face texture target is no longer live."});
        } else {
          const auto &document = documents[*document_index];
          chart = document.ok()
              ? modeling::query_face_texture_canvas({
                    .key = static_cast<std::uint64_t>(target.modeling_object_id),
                    .document = &document.value(), .face = face.face,
                    .world_from_local = world, .sampling_from_local = sampling})
              : ChartResult::failure(document.error());
        }
        if (!visitor(target_index, face_index, std::move(chart))) return;
      }
    }
  }

  [[nodiscard]] ModelingCommandResult apply_face_texture_canvas_edits(
      std::span<const ModelingFaceTextureCanvasEdit> edits) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture settings require face selection mode.");
    }
    if (edits.empty()) {
      return failure("Select one or more editable faces first.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const auto selected = selected_sdk_objects();
    std::vector<ObjectId> objects;
    std::vector<std::size_t> object_indices;
    std::set<std::pair<int, FaceId>> faces;
    objects.reserve(edits.size());
    object_indices.reserve(edits.size());
    for (const auto &edit : edits) {
      const auto &target = edit.target;
      const auto object = sdk_object(target.modeling_object_id);
      if (!object.has_value() || !target.face.valid() ||
          std::ranges::find(selected, *object) == selected.end() ||
          !faces.emplace(target.modeling_object_id, target.face).second ||
          (edit.scalar_update.has_value() &&
           edit.scalar_update->face != target.face)) {
        return failure("The selected texture targets are no longer valid.");
      }
      const auto found = std::ranges::find(objects, *object);
      const auto index = static_cast<std::size_t>(found - objects.begin());
      if (found == objects.end()) {
        objects.push_back(*object);
      }
      object_indices.push_back(index);
    }
    auto documents = session_.document_copies(objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    std::vector<modeling::PreparedFaceTextureDocument> prepared_documents;
    std::vector<PolygonSelectionSnapshot> selections;
    prepared_documents.reserve(objects.size());
    selections.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto native = native_object(objects[index]);
      if (!native.has_value()) {
        return failure("The selected texture targets are no longer valid.");
      }
      auto selection = documents.value()[index].selection();
      std::ranges::sort(selection.faces);
      selections.push_back(std::move(selection));
      prepared_documents.push_back({
          .key = static_cast<std::uint64_t>(*native),
          .document = std::move(documents.value()[index]),
      });
    }
    bool changed = false;
    for (std::size_t index = 0; index < edits.size(); ++index) {
      const auto &edit = edits[index];
      const auto &target = edit.target;
      auto &working = prepared_documents[object_indices[index]];
      const auto &selection = selections[object_indices[index]];
      if (!std::ranges::binary_search(selection.faces, target.face)) {
        return failure("The selected texture targets are no longer valid.");
      }
      bool face_changed = false;
      if (!edit.apply_mapping && edit.scalar_update.has_value() &&
          edit.scalar_update->rotation_degrees.has_value()) {
        // Nonaffine authored faces keep the existing image-axis scalar rotation
        // contract, including rectangular textures and occurrence compensation.
        const std::array targets{modeling::FaceTextureActionTarget{
            .key = working.key, .document = &working.document,
            .faces = {{.face = target.face,
                       .texture_width = static_cast<std::uint32_t>(edit.texture_extent.x),
                       .texture_height = static_cast<std::uint32_t>(edit.texture_extent.y),
                       .target_rotation_degrees = edit.scalar_update->rotation_degrees}},
            .world_from_local = to_sdk(target.world_from_local),
            .sampling_from_local = to_sdk(target.sampling_from_local)}};
        auto prepared = modeling::prepare_face_texture_action(targets,
            {.action = modeling::FaceTextureAction::RotateCounterClockwise});
        if (!prepared.ok()) return failure(prepared.error_message());
        face_changed = prepared.value().changed;
        if (face_changed) working.document = std::move(prepared.value().documents.front().document);
      } else if (edit.scalar_update.has_value()) {
        const std::array updates{*edit.scalar_update};
        const auto updated = working.document.set_face_texture_transforms(updates);
        if (!updated.ok()) {
          return failure(updated.error_message());
        }
        if (!updated.value().success) {
          return failure(updated.value().message);
        }
        face_changed = updated.value().changed;
      }
      if (edit.apply_mapping) {
        auto prepared = modeling::prepare_face_texture_canvas({
            .key = working.key,
            .document = &working.document,
            .face = target.face,
            .world_from_local = to_sdk(target.world_from_local),
            .sampling_from_local = to_sdk(target.sampling_from_local)}, edit.map);
        if (!prepared.ok()) {
          return failure(prepared.error_message());
        }
        if (!prepared.value().receipt.success) {
          return failure(prepared.value().receipt.message);
        }
        face_changed = face_changed || prepared.value().receipt.changed;
        working.document = std::move(prepared.value().document);
      }
      if (!face_changed) {
        continue;
      }
      // Both preparations retain topology and selection. Each unique changed
      // face contributes to the one geometry-only receipt for its object.
      working.receipt.changed = true;
      working.receipt.message = "Updated face texture settings.";
      working.receipt.dirty.geometry = true;
      working.receipt.affected.faces.push_back(target.face);
      working.receipt.modified.faces.push_back(target.face);
      changed = true;
    }
    if (!changed) {
      return {.ok = true, .changed = false};
    }
    return publish_prepared_documents(objects, prepared_documents,
                                      "Edit face texture settings failed.");
  }

  [[nodiscard]] bool face_texture_gesture_active() const noexcept {
    return preview_active() && active_preview_operation_id_ == "face_texture_gesture";
  }

  void set_face_texture_gesture_selection_cancel_handler(
      std::function<ModelingCommandResult()> handler) {
    face_texture_gesture_selection_cancel_handler_ = std::move(handler);
  }

  [[nodiscard]] ModelingCommandResult refresh_face_texture_gesture(
      const ModelingFaceTextureTransferTarget &target,
      const modeling::FaceTextureCanvasMap &map,
      std::optional<modeling::FaceTextureTransformUpdate> scalar_update) {
    if (preview_active() && !face_texture_gesture_active()) {
      return failure("Apply or cancel the active preview before editing textures.");
    }
    const auto object = sdk_object(target.modeling_object_id);
    if (!object.has_value()) return failure("The face texture target is no longer live.");
    const auto preview = session_.preview_summary();
    if (preview.active && !preflight_preview_mutation(preview.id)) {
      return failure("Texture preview participants changed outside the gesture.");
    }
    const std::array<ObjectId, 1> objects{*object};
    auto document = preview.active
        ? session_.preview_base_document_copy(preview.id, *object)
        : session_.document_copy(*object);
    if (!document.ok()) return failure(document.error_message());
    bool scalar_changed = false;
    if (scalar_update.has_value()) {
      if (scalar_update->face != target.face) {
        return failure("The texture gesture scalar target does not match its face.");
      }
      const std::array updates{*scalar_update};
      const auto updated = document.value().set_face_texture_transforms(updates);
      if (!updated.ok()) return failure(updated.error_message());
      if (!updated.value().success) return failure(updated.value().message);
      scalar_changed = updated.value().changed;
    }
    auto prepared = modeling::prepare_face_texture_canvas({
        .key = static_cast<std::uint64_t>(target.modeling_object_id),
        .document = &document.value(),
        .face = target.face,
        .world_from_local = to_sdk(target.world_from_local),
        .sampling_from_local = to_sdk(target.sampling_from_local)}, map);
    if (!prepared.ok()) return failure(prepared.error_message());
    if (scalar_changed && !prepared.value().receipt.changed) {
      auto &receipt = prepared.value().receipt;
      receipt.changed = true;
      receipt.message = "Updated face texture settings.";
      receipt.dirty.geometry = true;
      receipt.affected.faces.push_back(target.face);
      receipt.modified.faces.push_back(target.face);
    }
    PreparedMeshPreviewBatch batch;
    batch.changed = prepared.value().receipt.changed;
    batch.updates.push_back({.object = *object,
                             .document = std::move(prepared.value().document),
                             .receipt = std::move(prepared.value().receipt)});
    return preview.active
        ? update_prepared_mesh_preview_transaction(std::move(batch), "Edit face texture failed.", true)
        : begin_prepared_mesh_preview_transaction(objects, std::move(batch), "face_texture_gesture");
  }

  [[nodiscard]] ModelingSelectedUvPeelSnapshot selected_uv_peel_edges() const {
    ++stats_.uv_peel_summary_build_count;
    ModelingSelectedUvPeelSnapshot result;
    if (selection_mode_ != ModelingSelectionMode::Edge) {
      return result;
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    const Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok()) {
      result.command = failure(documents.error_message());
      return result;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const PolygonSelectionSnapshot selection = documents.value()[index].selection();
      if (selection.edges.empty()) {
        continue;
      }
      const auto native = native_object(selected[index]);
      const auto entity = native.has_value()
                              ? object_map_.entity_for_modeling_object(*native)
                              : std::nullopt;
      if (!native.has_value() || !entity.has_value() || !scene_.exists(*entity)) {
        result.command = failure("A selected UV Peel mesh is no longer live.");
        return result;
      }
      ModelingSelectedUvPeelObject object{
          .modeling_object_id = *native,
          .entity = *entity,
          .edges = selection.edges,
      };
      std::ranges::sort(object.edges);
      if (active_native_object_id_ == *native) {
        result.active_modeling_object_id = *native;
        if (selection.active_edge.valid()) {
          object.active_edge = selection.active_edge;
        }
      }
      result.objects.push_back(std::move(object));
    }
    std::ranges::sort(result.objects, {},
                      &ModelingSelectedUvPeelObject::modeling_object_id);
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_uv_peel(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options) {
    std::vector<ObjectId> objects;
    auto prepared = prepare_uv_peel_documents(targets, options, objects);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    return publish_prepared_documents(objects, prepared.value().documents,
                                      "UV Peel failed.");
  }

  [[nodiscard]] bool uv_peel_preview_active() const noexcept {
    return preview_active() && active_preview_operation_id_ == "uv_peel";
  }

  void set_uv_peel_preview_selection_refresh_handler(
      std::function<ModelingCommandResult()> handler,
      std::function<void()> before_suspend,
      std::function<void()> after_suspend,
      std::function<bool()> tool_active) {
    uv_peel_preview_selection_refresh_handler_ = std::move(handler);
    uv_peel_preview_before_selection_suspend_ = std::move(before_suspend);
    uv_peel_preview_after_selection_suspend_ = std::move(after_suspend);
    uv_peel_preview_tool_active_ = std::move(tool_active);
  }

  [[nodiscard]] ModelingCommandResult refresh_uv_peel_preview(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options) {
    if (preview_active() && !uv_peel_preview_active()) {
      return failure("Apply or cancel the active preview before UV Peel.");
    }
    const auto preview = session_.preview_summary();
    if (preview.active && !preflight_preview_mutation(preview.id)) {
      return failure("UV Peel preview participants changed outside this preview.");
    }
    std::vector<ObjectId> objects;
    auto prepared = prepare_uv_peel_documents(
        targets, options, objects,
        preview.active ? std::optional{preview.id} : std::nullopt);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    PreparedMeshPreviewBatch batch;
    batch.changed = prepared.value().changed;
    batch.updates.reserve(objects.size());
    for (auto &document : prepared.value().documents) {
      const auto object = sdk_object(static_cast<int>(document.key));
      if (!object.has_value()) {
        return failure("A prepared UV Peel target is no longer live.");
      }
      batch.updates.push_back({
          .object = *object,
          .document = std::move(document.document),
          .receipt = std::move(document.receipt),
      });
    }
    return preview.active
               ? update_prepared_mesh_preview_transaction(std::move(batch),
                                                          "UV Peel failed.", true)
               : begin_prepared_mesh_preview_transaction(
                     objects, std::move(batch), "uv_peel");
  }

  [[nodiscard]] ModelingCommandResult assign_material_slot_to_objects(
      std::span<const int> modeling_object_ids,
      std::uint32_t material_slot, bool preserve_scene_selection = false) {
    if (modeling_object_ids.empty()) {
      return failure("Select one or more meshes before assigning a material.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(modeling_object_ids.size());
    for (const int modeling_object_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(modeling_object_id);
      if (!object.has_value()) {
        return failure("Material target mesh is not live.");
      }
      if (std::ranges::find(objects, *object) == objects.end()) {
        objects.push_back(*object);
      }
    }
    return mutate_documents_by_object(
        objects,
        [material_slot](ObjectId, PolygonDocument &document) {
          return document.assign_all_face_material_slot(material_slot);
        },
        "Assign material slot failed.", false, false, false, true,
        preserve_scene_selection);
  }

  [[nodiscard]] ModelingCommandResult assign_material_slot_to_face(
      int modeling_object_id, int face_id, std::uint32_t material_slot) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value() || face_id <= 0) {
      return failure("Material target face is not live.");
    }
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents_by_object(
        objects,
        [face_id, material_slot](ObjectId, PolygonDocument &document) {
          return document.assign_face_material_slot(
              quader::modeling::make_id<quader::modeling::FaceTag>(
                  static_cast<std::uint32_t>(face_id)),
              material_slot);
        },
        "Assign material slot failed.", false, false, false, true);
  }

  [[nodiscard]] ModelingCommandResult assign_material_slots_to_faces(
      std::span<const ModelingMaterialSlotAssignment> assignments) {
    if (assignments.empty()) {
      return failure("Select one or more faces before assigning a material.");
    }

    /// Stores one validated native-to-SDK material-slot assignment.
    struct SdkAssignment {
      ObjectId object;
      quader::modeling::FaceId face;
      std::uint32_t material_slot = 0;
    };
    std::vector<SdkAssignment> sdk_assignments;
    std::vector<ObjectId> objects;
    sdk_assignments.reserve(assignments.size());
    objects.reserve(assignments.size());
    for (const ModelingMaterialSlotAssignment &assignment : assignments) {
      const std::optional<ObjectId> object =
          sdk_object(assignment.modeling_object_id);
      if (!object.has_value() || assignment.face_id <= 0) {
        return failure("Material target face is not live.");
      }
      sdk_assignments.push_back({
          .object = *object,
          .face = quader::modeling::make_id<quader::modeling::FaceTag>(
              static_cast<std::uint32_t>(assignment.face_id)),
          .material_slot = assignment.material_slot,
      });
      if (std::ranges::find(objects, *object) == objects.end()) {
        objects.push_back(*object);
      }
    }

    return mutate_documents_by_object(
        objects,
        [sdk_assignments = std::move(sdk_assignments)](
            ObjectId object, PolygonDocument &document) {
          OperationResult combined;
          combined.message = "Material assigned.";
          for (const SdkAssignment &assignment : sdk_assignments) {
            if (assignment.object != object) {
              continue;
            }
            Result<OperationResult> assigned = document.assign_face_material_slot(
                assignment.face, assignment.material_slot);
            if (!assigned.ok()) {
              return assigned;
            }
            if (!assigned.value().success) {
              return assigned;
            }
            combined.changed = combined.changed || assigned.value().changed;
            combined.dirty.materials =
                combined.dirty.materials || assigned.value().dirty.materials;
            combined.dirty.topology =
                combined.dirty.topology || assigned.value().dirty.topology;
            combined.dirty.geometry =
                combined.dirty.geometry || assigned.value().dirty.geometry;
          }
          return Result<OperationResult>::success(std::move(combined));
        },
        "Assign material slot failed.");
  }

  [[nodiscard]] ModelingCommandResult insert_edge_loop(
      int modeling_object_id, int edge_a, int edge_b, float factor) {
    if (!edit_scope_contains(modeling_object_id)) {
      return failure("Operation target must be a current Edit participant.");
    }

    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value() || edge_a <= 0 || edge_b <= 0 ||
        edge_a == edge_b) {
      return failure("Edge loop target is not live.");
    }
    const EdgeKey edge = quader::modeling::make_edge_key(
        quader::modeling::make_id<quader::modeling::VertexTag>(
            static_cast<std::uint32_t>(edge_a)),
        quader::modeling::make_id<quader::modeling::VertexTag>(
            static_cast<std::uint32_t>(edge_b)));
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents_by_object(
        objects,
        [edge, factor](ObjectId, PolygonDocument &document) {
          Result<OperationResult> mode =
              document.set_selection_mode(SelectionMode::Edge);
          if (!mode.ok()) {
            return mode;
          }
          const std::array<EdgeKey, 1> edges{edge};
          Result<OperationResult> selected =
              document.select_edges(edges, SelectionEdit::Replace);
          if (!selected.ok()) {
            return selected;
          }
          return document.insert_edge_loop(factor);
        },
        "Could not insert edge loop.", false, false, false, false, true,
        [this, objects, modeling_object_id]() {
          Result<OperationResult> selected = session_.apply_object_selection(
              objects, SelectionEdit::Replace);
          if (!selected.ok()) {
            return failure(selected.error_message());
          }
          selection_mode_ = ModelingSelectionMode::Edge;
          active_native_object_id_ = modeling_object_id;
          if (editor_mode_service_ != nullptr) {
            editor_mode_service_->record_component_kind(selection_mode_);
            if (const auto entity = object_map_.entity_for_modeling_object(modeling_object_id)) {
              editor_mode_service_->set_active_participant(*entity);
            }
          }
          if (fails(
                  ModelingSdkProjectionFailurePoint::SelectionScenePublication) ||
              !add_selection_to_scene()) {
            return failure(
                "Insert Edge Loop selection could not be published.");
          }
          return ModelingCommandResult{.ok = true};
        });
  }

  [[nodiscard]] ModelingCommandResult knife_stroke(
      int modeling_object_id,
      std::span<const ModelingSdkKnifeTarget> points,
      std::span<const ModelingSdkKnifeStrokeSegment> segments) {
    if (!edit_scope_contains(modeling_object_id)) {
      return failure("Operation target must be a current Edit participant.");
    }

    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return failure("Knife stroke needs a locked target mesh.");
    }
    if (segments.empty()) {
      return {.ok = true, .changed = false};
    }

    const auto target_kind = [](ModelingSdkKnifeTargetKind kind) {
      switch (kind) {
      case ModelingSdkKnifeTargetKind::ExistingVertex:
        return KnifeTargetKind::ExistingVertex;
      case ModelingSdkKnifeTargetKind::ExistingEdge:
        return KnifeTargetKind::ExistingEdge;
      case ModelingSdkKnifeTargetKind::InsertedVertex:
        return KnifeTargetKind::InsertedVertex;
      case ModelingSdkKnifeTargetKind::FacePoint:
        return KnifeTargetKind::FacePoint;
      }
      return KnifeTargetKind::ExistingVertex;
    };

    std::vector<KnifeTarget> sdk_points;
    sdk_points.reserve(points.size());
    for (const ModelingSdkKnifeTarget &point : points) {
      sdk_points.push_back({
          .kind = target_kind(point.kind),
          .vertex = quader::modeling::make_id<quader::modeling::VertexTag>(
              static_cast<std::uint32_t>(std::max(point.vertex_id, 0))),
          .edge = quader::modeling::make_edge_key(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(std::max(point.edge_a, 0))),
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(std::max(point.edge_b, 0)))),
          .edge_factor = point.edge_factor,
          .face = quader::modeling::make_id<quader::modeling::FaceTag>(
              static_cast<std::uint32_t>(std::max(point.face_id, 0))),
          .position = to_sdk(point.position),
      });
    }
    std::vector<KnifeStrokeSegment> sdk_segments;
    sdk_segments.reserve(segments.size());
    for (const ModelingSdkKnifeStrokeSegment segment : segments) {
      sdk_segments.push_back({
          .first_point = segment.first_point,
          .second_point = segment.second_point,
      });
    }

    const ModelingSelectionMode selection_mode_before = selection_mode_;
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents_by_object(
        objects,
        [sdk_points = std::move(sdk_points),
         sdk_segments = std::move(sdk_segments),
         selection_mode_before](ObjectId, PolygonDocument &document) {
          Result<OperationResult> cut =
              document.knife_stroke(sdk_points, sdk_segments);
          if (!cut.ok() || !cut.value().success || !cut.value().changed ||
              (selection_mode_before != ModelingSelectionMode::Edge &&
               selection_mode_before != ModelingSelectionMode::Face)) {
            return cut;
          }
          Result<OperationResult> restored = document.set_selection_mode(
              to_sdk_component_mode(selection_mode_before));
          if (!restored.ok()) {
            return restored;
          }
          return cut;
        },
        "Could not apply the knife stroke.", false, false, false, false, true,
        [this, objects, modeling_object_id, selection_mode_before]() {
          Result<OperationResult> selected = session_.apply_object_selection(
              objects, SelectionEdit::Replace);
          if (!selected.ok()) {
            return failure(selected.error_message());
          }
          selection_mode_ = selection_mode_before;
          active_native_object_id_ = modeling_object_id;
          if (editor_mode_service_ != nullptr) {
            editor_mode_service_->record_component_kind(selection_mode_);
            if (const auto entity = object_map_.entity_for_modeling_object(modeling_object_id)) {
              editor_mode_service_->set_active_participant(*entity);
            }
          }
          if (fails(
                  ModelingSdkProjectionFailurePoint::SelectionScenePublication) ||
              !add_selection_to_scene()) {
            return failure("Knife selection could not be published.");
          }
          return ModelingCommandResult{.ok = true};
        });
  }

  [[nodiscard]] bool initialize_transient_transform_payload() {
    QDR_PROFILE_FUNCTION();
    transient_transform_payload_participants_.clear();
    transient_transform_participant_indices_.clear();
    transient_transform_payload_active_ = false;
    std::vector<ObjectId> selected;
    const modeling::PreviewTransactionSummary active_preview =
        session_.preview_summary();
    for (const modeling::SessionObjectSummary &object : session_.objects()) {
      const bool participant = !active_preview.active ||
          std::ranges::any_of(active_preview.objects, [&](const auto &entry) {
            return entry.object_id == object.id &&
                   entry.action != modeling::SessionPreviewAction::RemoveExisting;
          });
      if (object.selected && object.kind == modeling::SessionObjectKind::Mesh && participant) {
        selected.push_back(object.id);
      }
    }
    if (selected.empty()) {
      return true;
    }
    std::vector<PolygonDocument> documents;
    documents.reserve(selected.size());
    for (const ObjectId object : selected) {
      Result<PolygonDocument> document =
          active_preview.active
              ? session_.preview_document_copy(active_preview.id, object)
              : session_.document_copy(object);
      if (!document.ok()) {
        return false;
      }
      documents.push_back(std::move(document).value());
    }
    transient_transform_payload_participants_.reserve(selected.size());
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> projection_groups;
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const ObjectId object = selected[index];
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *current =
          binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
      if (!native.has_value() || !entity.has_value() || current == nullptr) {
        transient_transform_payload_participants_.clear();
        return false;
      }

      PolygonDocument &document = documents[index];
      Result<AuthoredTopology> authored = document.authored_topology();
      if (!authored.ok()) {
        transient_transform_payload_participants_.clear();
        return false;
      }
      MeshAsset base_mesh = *current;
      const bool has_source_mapping =
          !base_mesh.geometry().vertices.empty() &&
          std::ranges::all_of(base_mesh.geometry().vertices,
                              [](const MeshVertex &vertex) {
                                return vertex.source_vertex_id != 0u;
                              });
      if (!has_source_mapping) {
        std::optional<MeshAsset> compiled =
            MeshProjectionService::compile_sdk_document_mesh(document);
        if (!compiled.has_value()) {
          transient_transform_payload_participants_.clear();
          return false;
        }
        compiled->name = current->name;
        base_mesh = std::move(*compiled);
      }

      std::unordered_set<std::uint32_t> selected_vertices;
      if (selection_mode_ == ModelingSelectionMode::Object ||
          selection_mode_ == ModelingSelectionMode::Group) {
        selected_vertices.reserve(authored.value().vertices.size());
        for (const quader::modeling::AuthoredTopologyVertex &vertex :
             authored.value().vertices) {
          selected_vertices.insert(vertex.id.index);
        }
      } else {
        const PolygonSelectionSnapshot selection = document.selection();
        selected_vertices.reserve(selection.vertices.size() +
                                  selection.edges.size() * 2u);
        for (const quader::modeling::VertexId vertex : selection.vertices) {
          selected_vertices.insert(vertex.index);
        }
        for (const EdgeKey edge : selection.edges) {
          selected_vertices.insert(edge.a.index);
          selected_vertices.insert(edge.b.index);
        }
        for (const FaceId face_id : selection.faces) {
          const auto face = std::ranges::find(
              authored.value().faces, face_id,
              &quader::modeling::AuthoredTopologyFace::id);
          if (face == authored.value().faces.end()) {
            continue;
          }
          for (const quader::modeling::VertexId vertex : face->vertices) {
            selected_vertices.insert(vertex.index);
          }
        }
      }
      if (selected_vertices.empty()) {
        continue;
      }
      std::unordered_map<std::uint32_t, QVec3> base_positions;
      base_positions.reserve(authored.value().vertices.size());
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           authored.value().vertices) {
        base_positions.emplace(
            vertex.id.index,
            QVec3{vertex.position.x, vertex.position.y, vertex.position.z});
      }
      std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
          face_vertices;
      bool preserve_preview_uv_layout = false;
      face_vertices.reserve(authored.value().faces.size());
      for (const quader::modeling::AuthoredTopologyFace &face :
           authored.value().faces) {
        const auto selected_corner_count = std::ranges::count_if(
            face.vertices, [&](modeling::VertexId vertex) {
              return selected_vertices.contains(vertex.index);
            });
        preserve_preview_uv_layout = preserve_preview_uv_layout ||
            (face.uvs.size() == face.vertices.size() && selected_corner_count > 0 &&
             (face.vertices.size() > 4U ||
              static_cast<std::size_t>(selected_corner_count) < face.vertices.size()));
        std::vector<std::uint32_t> vertices;
        vertices.reserve(face.vertices.size());
        for (const quader::modeling::VertexId vertex : face.vertices) {
          vertices.push_back(vertex.index);
        }
        face_vertices.emplace(face.id.index, std::move(vertices));
      }
      std::vector<TransientTransformSelectedVertex>
          selected_transform_vertices;
      std::unordered_map<std::uint32_t, std::size_t> selected_vertex_lookup;
      selected_transform_vertices.reserve(selected_vertices.size());
      selected_vertex_lookup.reserve(selected_vertices.size());
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           authored.value().vertices) {
        if (!selected_vertices.contains(vertex.id.index)) {
          continue;
        }
        const QVec3 position{vertex.position.x, vertex.position.y,
                             vertex.position.z};
        selected_vertex_lookup.emplace(vertex.id.index,
                                       selected_transform_vertices.size());
        selected_transform_vertices.push_back({
            .source_vertex_id = vertex.id.index,
            .base_position = position,
            .preview_position = position,
        });
      }
      std::vector<QVec3> base_mesh_positions;
      base_mesh_positions.reserve(base_mesh.geometry().vertices.size());
      for (std::size_t vertex_index = 0;
           vertex_index < base_mesh.geometry().vertices.size();
           ++vertex_index) {
        const MeshVertex &vertex = base_mesh.geometry().vertices[vertex_index];
        base_mesh_positions.push_back(vertex.position);
        const auto selected =
            selected_vertex_lookup.find(vertex.source_vertex_id);
        if (selected != selected_vertex_lookup.end()) {
          selected_transform_vertices[selected->second]
              .compiled_vertex_indices.push_back(vertex_index);
        }
      }
      auto initial_positions =
          std::make_shared<std::vector<QVec3>>(base_mesh_positions);
      std::vector<std::uint32_t> movable_vertex_ids;
      movable_vertex_ids.reserve(selected_transform_vertices.size());
      for (const auto &vertex : selected_transform_vertices) {
        movable_vertex_ids.push_back(vertex.source_vertex_id);
      }
      auto position_update_plan = MeshProjectionService::prepare_position_update_plan(
          base_mesh, movable_vertex_ids);
      auto retained_position_plan = MeshProjectionService::prepare_retained_position_projection(
          document, base_mesh, position_update_plan.get());
      const MaterialComponent *material = scene_.material(*entity);
      TransientTransformParticipant participant{
          .object = object,
          .native_object_id = *native,
          .entity = *entity,
          .material = material != nullptr ? *material : MaterialComponent{},
          .world_transform =
              modeling_object_world_transform_resolver_
                  ? modeling_object_world_transform_resolver_(*native)
                        .value_or(scene_.world_transform_matrix(*entity))
                  : scene_.world_transform_matrix(*entity),
          .double_sided = binding->double_sided,
          .layout_key = transform_mesh_layout_key(
              object, base_mesh.geometry().vertices),
          .position_revision = transient_transform_payload_revision_,
          .preserve_preview_uv_layout = preserve_preview_uv_layout,
          .base_document = document,
          .preview_document = document,
          .preview_mesh = std::move(base_mesh),
          .position_update_plan = std::move(position_update_plan),
          .retained_position_plan = std::move(retained_position_plan),
          .base_mesh_positions = std::move(base_mesh_positions),
          .selected_vertices = std::move(selected_transform_vertices),
          .selected_vertex_lookup = std::move(selected_vertex_lookup),
          .position_buffers = {std::move(initial_positions)},
          .base_positions = std::move(base_positions),
          .face_vertices = std::move(face_vertices),
      };
      if (selection_mode_ != ModelingSelectionMode::Object &&
          selection_mode_ != ModelingSelectionMode::Group) {
        auto prepared_transform = document.prepare_selection_transform();
        if (!prepared_transform.ok()) {
          transient_transform_payload_participants_.clear();
          return false;
        }
        participant.prepared_transform = std::move(prepared_transform).value();
      }
      auto projection_source =
          MeshProjectionService::polygon_document_from_sdk_document(document);
      if (!projection_source) {
        transient_transform_payload_participants_.clear();
        return false;
      }
      const auto participant_index = transient_transform_payload_participants_.size();
      participant.projection_group = participant_index;
      auto group_hash = mesh_asset_payload_detail::polygon_document_hash(*projection_source);
      group_hash = mesh_asset_payload_detail::append_authored_hash(
          group_hash, participant.preview_mesh.geometry_content_id().value);
      for (const auto &vertex : participant.selected_vertices) {
        group_hash = mesh_asset_payload_detail::append_authored_hash(
            group_hash, vertex.source_vertex_id);
      }
      auto &candidates = projection_groups[group_hash];
      for (const auto candidate_index : candidates) {
        const auto &candidate = transient_transform_payload_participants_[candidate_index];
        if (participant.preserve_preview_uv_layout == candidate.preserve_preview_uv_layout &&
            std::ranges::equal(participant.selected_vertices, candidate.selected_vertices,
                [](const auto &left, const auto &right) {
                  return left.source_vertex_id == right.source_vertex_id;
                }) &&
            same_transform_projection_source(participant.preview_mesh, candidate.preview_mesh) &&
            mesh_asset_payload_detail::same_polygon_document(
                *projection_source, *candidate.transform_source)) {
          participant.projection_group = candidate_index;
          participant.transform_source = candidate.transform_source;
          participant.position_update_plan = candidate.position_update_plan;
          break;
        }
      }
      if (participant.projection_group == participant_index) {
        candidates.push_back(participant_index);
        participant.transform_source =
            std::make_shared<const quader_poly::Document>(std::move(*projection_source));
      }
      transient_transform_participant_indices_.emplace(
          sdk_object_key(object), transient_transform_payload_participants_.size());
      transient_transform_payload_participants_.push_back(std::move(participant));
    }
    transient_transform_payload_active_ =
        !transient_transform_payload_participants_.empty();
    return transient_transform_payload_active_;
  }

  [[nodiscard]] ModelingCommandResult
  ensure_component_transform_payload() {
    if (transient_transform_payload_active_ ||
        selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      return {.ok = true};
    }
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preview.active) {
      return failure("No active SDK transform preview.");
    }
    if (!initialize_transient_transform_payload()) {
      return abort_preview_publication(
          preview.id,
          "Transform preview participants could not be built.");
    }
    return {.ok = true};
  }

  [[nodiscard]] bool unchanged_affine_preview_input(
      const modeling::PreviewTransactionSummary &preview,
      const WorkspaceTransformResolver &resolve_transform) const {
    if (!resolve_transform || !mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview.id) ||
        mesh_preview_host_baseline_->affine_input_revision !=
            preview.content_revision) {
      return false;
    }
    const auto &inputs = mesh_preview_host_baseline_->affine_inputs;
    std::size_t index = 0U;
    for (const auto &participant : preview.objects) {
      if (participant.action == modeling::SessionPreviewAction::RemoveExisting) {
        continue;
      }
      if (index == inputs.size() || inputs[index].object != participant.object_id ||
          !same_transform_request(inputs[index].transform,
                                  resolve_transform(participant.object_id))) {
        return false;
      }
      ++index;
    }
    return index == inputs.size();
  }

  [[nodiscard]] std::optional<ModelingCommandResult>
  update_affine_transient_transform_payload(
      const WorkspaceTransformResolver &resolve_transform,
      const DocumentMutationByObject &mutation,
      std::string_view fallback_message, bool translation_only = false) {
    QDR_PROFILE_FUNCTION();
    const auto preview = session_.preview_summary();
    if (preview.objects.size() != transient_transform_payload_participants_.size() ||
        std::ranges::any_of(preview.objects, [](const auto &object) {
          return object.kind != modeling::SessionObjectKind::Mesh ||
                 object.action != modeling::SessionPreviewAction::ReplaceExisting;
        })) {
      return std::nullopt;
    }
    if (unchanged_affine_preview_input(preview, resolve_transform)) {
      return ModelingCommandResult{.ok = true, .changed = false,
                                   .message = "Transform preview unchanged."};
    }

    /// Detached candidate and derived presentation, published only after every
    /// participant and the SDK batch have accepted the same authored result.
    struct PreparedParticipant {
      std::size_t participant_index = 0U;
      std::size_t projection_leader = 0U;
      PolygonDocument document;
      std::optional<PolygonDocument> published_document;
      std::optional<PolygonDocument> uv_baseline;
      MeshAsset current_mesh;
      std::optional<modeling::Transform3> transform;
      OperationResult receipt;
      std::optional<MeshAsset> mesh;
      std::shared_ptr<const MeshGeometryProjection> geometry_projection;
      bool retains_position_buffer = true;
      std::vector<QVec3> selected_positions;
      std::shared_ptr<std::vector<QVec3>> packed_positions;
      std::size_t buffer_index = 0U;
      std::uint64_t layout_key = 0U;
      std::vector<std::vector<std::size_t>> compiled_indices;
      std::optional<std::string> error;
    };
    std::vector<PreparedParticipant> prepared;
    prepared.reserve(preview.objects.size());
    {
      QDR_PROFILE_SCOPE("Transform preview detach authoritative candidates");
      for (const auto &object : preview.objects) {
        const auto found = transient_transform_participant_indices_.find(
            sdk_object_key(object.object_id));
        if (found == transient_transform_participant_indices_.end()) {
          return std::nullopt;
        }
        const auto &participant =
            transient_transform_payload_participants_[found->second];
        PreparedParticipant entry{
            .participant_index = found->second,
            .document = participant.base_document,
            .current_mesh = participant.preview_mesh,
            .transform = resolve_transform(object.object_id),
        };
        if (participant.preserve_preview_uv_layout && entry.transform.has_value() &&
            has_nonsingular_transform(*entry.transform)) {
          entry.uv_baseline.emplace(entry.document);
        }
        entry.buffer_index = participant.position_buffers.size();
        for (std::size_t index = 0U; index < participant.position_buffers.size();
             ++index) {
          // Even an unpinned current buffer is accepted preview state. A failed
          // preparation must leave that buffer's contents untouched.
          if (index != participant.published_position_buffer &&
              participant.position_buffers[index].use_count() == 1) {
            entry.buffer_index = index;
            entry.packed_positions = participant.position_buffers[index];
            break;
          }
        }
        prepared.push_back(std::move(entry));
      }
    }
    {
      QDR_PROFILE_SCOPE("Transform preview group equivalent derived candidates");
      std::unordered_map<std::uint64_t, std::vector<std::size_t>> groups;
      groups.reserve(prepared.size());
      std::size_t projection_count = 0U;
      for (std::size_t index = 0U; index < prepared.size(); ++index) {
        auto &entry = prepared[index];
        const auto &participant =
            transient_transform_payload_participants_[entry.participant_index];
        auto group_hash = mesh_asset_payload_detail::append_authored_hash(
            participant.projection_group, entry.current_mesh.geometry_content_id().value);
        group_hash = transform_request_hash(entry.transform, group_hash);
        auto &candidates = groups[group_hash];
        entry.projection_leader = index;
        for (const auto candidate_index : candidates) {
          const auto &candidate = prepared[candidate_index];
          const auto &source =
              transient_transform_payload_participants_[candidate.participant_index];
          if (participant.projection_group == source.projection_group &&
              same_transform_request(entry.transform, candidate.transform) &&
              same_transform_projection_source(entry.current_mesh, candidate.current_mesh) &&
              std::ranges::equal(participant.selected_vertices, source.selected_vertices,
                  [](const auto &left, const auto &right) {
                    return left.preview_position.x == right.preview_position.x &&
                           left.preview_position.y == right.preview_position.y &&
                           left.preview_position.z == right.preview_position.z;
                  })) {
            entry.projection_leader = candidate_index;
            break;
          }
        }
        if (entry.projection_leader == index) {
          candidates.push_back(index);
          ++projection_count;
        }
      }
      QDR_PROFILE_PLOT("Quader transform derived projections",
                       static_cast<std::int64_t>(projection_count));
      QDR_PROFILE_PLOT("Quader transform derived projection reuse",
                       static_cast<std::int64_t>(prepared.size() - projection_count));
      (void)projection_count;
    }
    const bool whole_object = selection_mode_ == ModelingSelectionMode::Object ||
                              selection_mode_ == ModelingSelectionMode::Group;
      const auto prepare_participant = [&](PreparedParticipant &entry) {
      try {
        const auto &participant =
            transient_transform_payload_participants_[entry.participant_index];
        Result<OperationResult> operated;
        std::shared_ptr<const std::vector<modeling::VertexPositionUpdate>>
            evaluated_positions;
        if (participant.prepared_transform && entry.transform) {
          auto evaluated = translation_only
              ? participant.prepared_transform->evaluate_translation(entry.transform->origin)
              : participant.prepared_transform->evaluate_transform(*entry.transform);
          if (!evaluated.ok()) {
            entry.error = evaluated.error_message();
            return;
          }
          evaluated_positions = std::move(evaluated.value().positions);
          entry.document = std::move(evaluated.value().document);
          operated = Result<OperationResult>::success(std::move(evaluated.value().receipt));
        } else {
          operated = mutation(participant.object, entry.document);
        }
        if (!operated.ok() || !operated.value().success) {
          entry.error = operated.ok() && !operated.value().message.empty()
                            ? operated.value().message
                            : (operated.error_message().empty()
                                   ? std::string(fallback_message)
                                   : operated.error_message());
          return;
        }
        entry.receipt = std::move(operated).value();
        entry.published_document.emplace(entry.document);
        if (&entry != &prepared[entry.projection_leader]) {
          return;
        }
        if (evaluated_positions && participant.retained_position_plan) {
          if (evaluated_positions->size() != participant.selected_vertices.size()) {
            entry.error = "Transform preview selected vertices are unavailable.";
            return;
          }
          entry.selected_positions.resize(evaluated_positions->size());
          bool changed = false;
          for (std::size_t index = 0U; index < evaluated_positions->size(); ++index) {
            const auto &value = (*evaluated_positions)[index];
            const auto &selected = participant.selected_vertices[index];
            if (value.vertex.index != selected.source_vertex_id) {
              entry.error = "Transform preview selected vertices are unavailable.";
              return;
            }
            const QVec3 position{value.position.x, value.position.y, value.position.z};
            entry.selected_positions[index] = position;
            changed = changed || position.x != selected.preview_position.x ||
                position.y != selected.preview_position.y || position.z != selected.preview_position.z;
          }
          if (!changed) return;
          if (!entry.packed_positions) entry.packed_positions = std::make_shared<std::vector<QVec3>>();
          entry.geometry_projection = MeshProjectionService::project_retained_mesh_position_updates(
              *evaluated_positions, *participant.retained_position_plan,
              participant.geometry_projection, entry.packed_positions,
              entry.uv_baseline.has_value());
          if (entry.geometry_projection) {
            entry.layout_key = participant.layout_key;
            return;
          }
        }
        // The SDK candidate owns these exact coordinates over retained topology.
        // Derive one neutral mesh per equivalent projection family without forcing
        // each logical object's lazy SDK document to materialize its topology.
        std::optional<quader_poly::Document> authored;
        if (evaluated_positions && participant.transform_source) {
          authored.emplace(*participant.transform_source);
        } else {
          authored =
              MeshProjectionService::polygon_document_from_sdk_document(entry.document);
        }
        if (!authored.has_value()) {
          entry.error = "Could not derive the transient transform mesh attributes.";
          return;
        }
        entry.selected_positions.resize(participant.selected_vertices.size());
        std::size_t selected_count = 0U;
        bool positions_changed = false;
        for (auto &vertex : authored->vertices) {
          const auto selected = participant.selected_vertex_lookup.find(vertex.id);
          if (evaluated_positions && selected != participant.selected_vertex_lookup.end()) {
            if (selected_count >= evaluated_positions->size() ||
                (*evaluated_positions)[selected_count].vertex.index != vertex.id) {
              entry.error = "Transform preview selected vertices are unavailable.";
              return;
            }
            const auto &position = (*evaluated_positions)[selected_count].position;
            vertex.position = {position.x, position.y, position.z};
          }
          if (!finite(vertex.position)) {
            entry.error = "Vertex position updates must contain finite coordinates.";
            return;
          }
          if (selected == participant.selected_vertex_lookup.end()) {
            continue;
          }
          ++selected_count;
          entry.selected_positions[selected->second] = vertex.position;
          const auto &previous =
              participant.selected_vertices[selected->second].preview_position;
          positions_changed = positions_changed || previous.x != vertex.position.x ||
                              previous.y != vertex.position.y ||
                              previous.z != vertex.position.z;
        }
        if (selected_count != participant.selected_vertices.size() ||
            (evaluated_positions && selected_count != evaluated_positions->size())) {
          entry.error = "Transform preview selected vertices are unavailable.";
          return;
        }
        if (!positions_changed) {
          return;
        }
        if (entry.uv_baseline.has_value()) {
          PolygonDocument derived(entry.document);
          const auto preserved =
              MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
                  *entry.uv_baseline, derived, whole_object);
          if (!preserved.ok() || !preserved.value().success) {
            entry.error = preserved.ok() ? preserved.value().message
                                         : preserved.error_message();
            return;
          }
          if (preserved.value().changed) {
            authored = MeshProjectionService::polygon_document_from_sdk_document(derived);
            if (!authored.has_value()) {
              entry.error = "Could not derive the transient transform mesh attributes.";
              return;
            }
          }
        }
        entry.mesh = MeshProjectionService::update_polygon_document_mesh_positions(
            std::move(*authored), entry.current_mesh,
            participant.position_update_plan.get());
        // Collapsed authored faces can regain triangles as their neighbors move.
        // Permit that larger render layout while retaining collapse rejection.
        if (!entry.mesh.has_value() ||
            entry.mesh->geometry().vertices.size() <
                participant.base_mesh_positions.size()) {
          entry.error = "Could not derive the transient transform mesh attributes.";
          return;
        }
        entry.mesh->name = entry.current_mesh.name;
        entry.layout_key = participant.layout_key;
        if (!std::ranges::equal(entry.mesh->geometry().vertices,
                               entry.current_mesh.geometry().vertices,
                               [](const MeshVertex &left, const MeshVertex &right) {
                                 return left.source_vertex_id == right.source_vertex_id;
                               })) {
          entry.layout_key = transform_mesh_layout_key(
              participant.object, entry.mesh->geometry().vertices);
          entry.compiled_indices.resize(participant.selected_vertices.size());
          for (std::size_t index = 0U; index < entry.mesh->geometry().vertices.size();
               ++index) {
            const auto selected = participant.selected_vertex_lookup.find(
                entry.mesh->geometry().vertices[index].source_vertex_id);
            if (selected != participant.selected_vertex_lookup.end()) {
              entry.compiled_indices[selected->second].push_back(index);
            }
          }
        }
        if (!entry.packed_positions) {
          entry.packed_positions = std::make_shared<std::vector<QVec3>>();
        }
        entry.packed_positions->clear();
        entry.packed_positions->reserve(entry.mesh->geometry().vertices.size());
        for (const auto &vertex : entry.mesh->geometry().vertices) {
          entry.packed_positions->push_back(vertex.position);
        }
        // Freeze the completed detached geometry on its preparation worker.
        // Retained render caches then consume its already-computed content ID.
        (void)entry.mesh->geometry_payload();
      } catch (const std::exception &error) {
        entry.error = error.what();
      } catch (...) {
        entry.error = "Transform preview preparation failed.";
      }
    };
    {
      QDR_PROFILE_SCOPE("Transform preview prepare authoritative mesh batch");
      constexpr std::size_t kParallelParticipantThreshold = 64U;
      const std::size_t worker_count =
          prepared.size() < kParallelParticipantThreshold
              ? 1U
              : std::min<std::size_t>(prepared.size(),
                    std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1U, 8U));
      std::atomic_size_t next_participant{0U};
      const auto prepare_next = [&]() {
        for (;;) {
          const auto index = next_participant.fetch_add(1U, std::memory_order_relaxed);
          if (index >= prepared.size()) {
            return;
          }
          prepare_participant(prepared[index]);
        }
      };
      std::vector<std::jthread> workers;
      workers.reserve(worker_count - 1U);
      for (std::size_t index = 1U; index < worker_count; ++index) {
        workers.emplace_back(prepare_next);
      }
      prepare_next();
      for (auto &worker : workers) {
        worker.join();
      }
    }
    {
      QDR_PROFILE_SCOPE("Transform preview share equivalent derived candidates");
      for (std::size_t index = 0U; index < prepared.size(); ++index) {
        auto &entry = prepared[index];
        if (entry.error || entry.projection_leader == index) continue;
        const auto &source = prepared[entry.projection_leader];
        if (source.error) {
          entry.error = source.error;
          continue;
        }
        if (!source.mesh && !source.geometry_projection) continue;
        const auto &participant =
            transient_transform_payload_participants_[entry.participant_index];
        if (source.geometry_projection) {
          entry.geometry_projection = source.geometry_projection;
          entry.selected_positions = source.selected_positions;
          entry.layout_key = participant.layout_key;
          entry.retains_position_buffer = false;
          entry.packed_positions.reset();
          continue;
        }
        entry.mesh = entry.current_mesh;
        entry.mesh->share_geometry(source.mesh->geometry_payload());
        entry.mesh->share_authored_polygon_document(source.mesh->authored_polygon_document);
        entry.mesh->has_stable_cpu_topology = source.mesh->has_stable_cpu_topology;
        entry.selected_positions = source.selected_positions;
        entry.layout_key = source.compiled_indices.empty()
            ? participant.layout_key
            : transform_mesh_layout_key(participant.object, entry.mesh->geometry().vertices);
        entry.compiled_indices = source.compiled_indices;
        if (!entry.packed_positions) {
          entry.packed_positions = std::make_shared<std::vector<QVec3>>();
        }
        *entry.packed_positions = *source.packed_positions;
      }
    }
    std::vector<modeling::SessionPreviewDocumentUpdate> updates;
    updates.reserve(prepared.size());
    for (auto &entry : prepared) {
      if (entry.error.has_value()) {
        return failure(*entry.error);
      }
      auto &participant =
          transient_transform_payload_participants_[entry.participant_index];
      if ((entry.mesh.has_value() || (entry.geometry_projection && entry.retains_position_buffer)) &&
          entry.buffer_index == participant.position_buffers.size()) {
        participant.position_buffers.reserve(participant.position_buffers.size() + 1U);
      }
      updates.push_back({.object = participant.object,
                         .document = std::move(entry.document),
                         .receipt = std::move(entry.receipt)});
    }
    auto result = update_workspace_transform_preview_by_object(
        mutation, fallback_message, true, {}, true, resolve_transform,
        std::move(updates));
    if (!result.ok) {
      return result;
    }
    const bool positions_changed = std::ranges::any_of(prepared, [](const auto &entry) {
      return entry.mesh.has_value() || entry.geometry_projection;
    });
    if (positions_changed) {
      ++transient_transform_payload_revision_;
    }
    {
      QDR_PROFILE_SCOPE("Transform preview publish accepted mesh batch");
      for (auto &entry : prepared) {
        auto &participant =
            transient_transform_payload_participants_[entry.participant_index];
        participant.preview_document = std::move(*entry.published_document);
        if (!entry.mesh.has_value() && !entry.geometry_projection) {
          continue;
        }
        for (std::size_t index = 0U; index < participant.selected_vertices.size();
             ++index) {
          participant.selected_vertices[index].preview_position =
              entry.selected_positions[index];
          if (!entry.compiled_indices.empty()) {
            participant.selected_vertices[index].compiled_vertex_indices =
                std::move(entry.compiled_indices[index]);
          }
        }
        participant.geometry_projection = std::move(entry.geometry_projection);
        if (entry.mesh) {
          participant.preview_mesh = std::move(*entry.mesh);
          // A canonical layout fallback retires this gesture's direct mapping.
          participant.retained_position_plan.reset();
        }
        if (entry.retains_position_buffer) {
          if (entry.buffer_index == participant.position_buffers.size()) {
            participant.position_buffers.push_back(std::move(entry.packed_positions));
          }
          participant.published_position_buffer = entry.buffer_index;
        }
        participant.layout_key = entry.layout_key;
        participant.position_revision = transient_transform_payload_revision_;
      }
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult update_transient_transform_payload(
      const WorkspaceTransformResolver &resolve_transform,
      DocumentMutationByObject commit_mutation,
      std::string_view fallback_message) {
    QDR_PROFILE_FUNCTION();
    if (!session_.preview_active() ||
        transient_transform_payload_participants_.empty()) {
      return failure("No active transient transform workspace.");
    }
    std::vector<std::optional<quader::modeling::Transform3>> transforms;
    transforms.reserve(transient_transform_payload_participants_.size());
    {
      QDR_PROFILE_SCOPE("Transform preview resolve participant transforms");
      for (const TransientTransformParticipant &participant :
           transient_transform_payload_participants_) {
        transforms.push_back(resolve_transform(participant.object));
      }
    }

    bool changed = false;
    bool effective_changed = false;
    std::vector<std::uint8_t> participant_packed_positions_changed(
        transient_transform_payload_participants_.size(), 0u);
    {
      QDR_PROFILE_SCOPE("Transform preview update selected positions");
      for (std::size_t participant_index = 0u;
           participant_index < transient_transform_payload_participants_.size();
           ++participant_index) {
        TransientTransformParticipant &participant =
            transient_transform_payload_participants_[participant_index];
        const std::optional<quader::modeling::Transform3> &transform =
            transforms[participant_index];
        const std::vector<QVec3> &published_positions =
            *participant
                 .position_buffers[participant.published_position_buffer];
        for (TransientTransformSelectedVertex &vertex :
             participant.selected_vertices) {
          const QVec3 next = transform.has_value()
                                 ? transform_sdk_point(*transform,
                                                       vertex.base_position)
                                 : vertex.base_position;
          const bool vertex_changed =
              vertex.preview_position.x != next.x ||
              vertex.preview_position.y != next.y ||
              vertex.preview_position.z != next.z;
          changed = changed || vertex_changed;
          if (vertex_changed &&
              !participant_packed_positions_changed[participant_index]) {
            for (const std::size_t compiled_index :
                 vertex.compiled_vertex_indices) {
              const QVec3 &published = published_positions[compiled_index];
              if (published.x != next.x || published.y != next.y ||
                  published.z != next.z) {
                participant_packed_positions_changed[participant_index] = 1u;
                break;
              }
            }
          }
          effective_changed =
              effective_changed || vertex.base_position.x != next.x ||
              vertex.base_position.y != next.y ||
              vertex.base_position.z != next.z;
          vertex.preview_position = next;
        }
      }
    }
    bool packed_positions_changed = false;
    {
      QDR_PROFILE_SCOPE("Transform preview publish packed positions");
      for (std::size_t participant_index = 0u;
           participant_index < transient_transform_payload_participants_.size();
           ++participant_index) {
        if (!participant_packed_positions_changed[participant_index]) {
          continue;
        }
        packed_positions_changed = true;
        TransientTransformParticipant &participant =
            transient_transform_payload_participants_[participant_index];
        std::size_t buffer_index = participant.position_buffers.size();
        for (std::size_t candidate = 0u;
             candidate < participant.position_buffers.size(); ++candidate) {
          if (participant.position_buffers[candidate].use_count() == 1) {
            buffer_index = candidate;
            break;
          }
        }
        if (buffer_index == participant.position_buffers.size()) {
          participant.position_buffers.push_back(
              std::make_shared<std::vector<QVec3>>());
        }

        std::vector<VertexPositionUpdate> updates;
        updates.reserve(participant.selected_vertices.size());
        for (const TransientTransformSelectedVertex &vertex :
             participant.selected_vertices) {
          updates.push_back({
              .vertex = quader::modeling::make_id<
                  quader::modeling::VertexTag>(vertex.source_vertex_id),
              .position = to_sdk(vertex.preview_position),
          });
        }
        Result<OperationResult> positioned =
            participant.preview_document.set_vertex_positions(updates);
        if (!positioned.ok()) {
          return failure(positioned.error_message());
        }
        std::optional<PolygonDocument> derived_document;
        const PolygonDocument *render_document = &participant.preview_document;
        if (participant.preserve_preview_uv_layout &&
            transforms[participant_index].has_value() &&
            has_nonsingular_transform(*transforms[participant_index])) {
          if (!mesh_preview_host_baseline_.has_value()) {
            return failure("Transform preview baseline is unavailable.");
          }
          auto baseline = session_.preview_base_document_copy(
              mesh_preview_host_baseline_->preview_id, participant.object);
          if (!baseline.ok()) return failure(baseline.error_message());
          derived_document.emplace(participant.preview_document);
          auto preserved =
              MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
                  baseline.value(), *derived_document,
                  selection_mode_ == ModelingSelectionMode::Object ||
                      selection_mode_ == ModelingSelectionMode::Group);
          if (!preserved.ok() || !preserved.value().success) {
            return failure(preserved.ok() ? preserved.value().message
                                          : preserved.error_message());
          }
          render_document = &*derived_document;
        }
        std::optional<MeshAsset> projected =
            MeshProjectionService::update_sdk_document_mesh_positions(
                *render_document, participant.preview_mesh,
                participant.position_update_plan.get());
        if (!projected.has_value()) {
          projected = MeshProjectionService::compile_sdk_document_mesh(
              *render_document);
        }
        if (!projected.has_value() ||
            projected->geometry().vertices.size() !=
                participant.base_mesh_positions.size()) {
          return failure(
              "Could not derive the transient transform mesh attributes.");
        }
        projected->name = participant.preview_mesh.name;
        if (participant.preserve_preview_uv_layout &&
            !std::ranges::equal(projected->geometry().vertices,
                participant.preview_mesh.geometry().vertices,
                [](const MeshVertex &left, const MeshVertex &right) {
                  return left.source_vertex_id == right.source_vertex_id;
                })) {
          participant.layout_key = transform_mesh_layout_key(
              participant.object, projected->geometry().vertices);
          for (auto &vertex : participant.selected_vertices) {
            vertex.compiled_vertex_indices.clear();
          }
          for (std::size_t index = 0; index < projected->geometry().vertices.size();
               ++index) {
            const auto selected = participant.selected_vertex_lookup.find(
                projected->geometry().vertices[index].source_vertex_id);
            if (selected != participant.selected_vertex_lookup.end()) {
              participant.selected_vertices[selected->second]
                  .compiled_vertex_indices.push_back(index);
            }
          }
        }

        std::vector<QVec3> &packed_positions =
            *participant.position_buffers[buffer_index];
        packed_positions.clear();
        packed_positions.reserve(projected->geometry().vertices.size());
        for (const MeshVertex &vertex : projected->geometry().vertices) {
          packed_positions.push_back(vertex.position);
        }
        participant.preview_mesh = std::move(*projected);
        participant.published_position_buffer = buffer_index;
      }
    }
    (void)commit_mutation;
    transient_transform_payload_active_ = true;
    if (packed_positions_changed) {
      ++transient_transform_payload_revision_;
      for (std::size_t participant_index = 0u;
           participant_index < transient_transform_payload_participants_.size();
           ++participant_index) {
        if (participant_packed_positions_changed[participant_index]) {
          transient_transform_payload_participants_[participant_index]
              .position_revision = transient_transform_payload_revision_;
        }
      }
    }
    preview_changed_ = effective_changed;
    return {.ok = true,
            .changed = changed,
            .message = changed ? std::string(fallback_message)
                               : "Transform preview unchanged."};
  }

  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  workspace_transform_preview_meshes() const {
    QDR_PROFILE_FUNCTION();
    std::vector<ModelingToolPreviewMesh> meshes;
    if (!transient_transform_payload_active_) {
      return meshes;
    }
    meshes.reserve(transient_transform_payload_participants_.size());
    for (const TransientTransformParticipant &participant :
         transient_transform_payload_participants_) {
      meshes.push_back({
          .source_object_id = participant.native_object_id,
          .source_entity = participant.entity,
          .material = participant.material,
          .mesh = participant.preview_mesh,
          .transform = participant.world_transform,
          .double_sided = participant.double_sided,
          .dynamic_positions = true,
          .suppress_source = true,
          .layout_key = participant.layout_key,
          .position_revision = participant.position_revision,
          .dynamic_position_payload =
              participant.geometry_projection ? participant.geometry_projection->positions
                  : participant.position_buffers[participant.published_position_buffer],
          .geometry_projection = participant.geometry_projection,
      });
    }
    return meshes;
  }

  [[nodiscard]] AcceptedOverlayProof accepted_overlay_proof() const {
    return {.content = content_revision(),
            .sdk_content = session_.content_revision(),
            .selection = selection_revision(),
            .scene = scene_.revision(),
            .assets = assets_.revision(),
            .object_map = object_map_.revision(),
            .active_object = active_native_object_id_,
            .instance_scope = instance_edit_scope_provider_
                ? instance_edit_scope_provider_() : ModelingInstanceEditScope{}};
  }

  [[nodiscard]] std::shared_ptr<const ModelingAcceptedOverlayCoordinates>
  accepted_overlay_coordinates() const {
    if (!accepted_overlay_coordinates_ || session_.preview_active() ||
        accepted_overlay_proof_ != accepted_overlay_proof()) {
      return {};
    }
    return accepted_overlay_coordinates_;
  }

  [[nodiscard]] std::uint64_t workspace_transform_preview_revision() const {
    return transient_transform_payload_revision_;
  }

  [[nodiscard]] bool workspace_transform_preview_active() const {
    return transient_transform_payload_active_;
  }

  void apply_workspace_transform_preview_to_overlay(
      ViewportOverlayFrame &frame) const {
    if (!transient_transform_payload_active_) {
      return;
    }
    std::unordered_map<int, const TransientTransformParticipant *>
        participants;
    participants.reserve(transient_transform_payload_participants_.size());
    for (const TransientTransformParticipant &participant :
         transient_transform_payload_participants_) {
      participants.emplace(participant.native_object_id, &participant);
    }
    const auto participant_for = [&participants](int object_id) {
      const auto found = participants.find(object_id);
      return found != participants.end() ? found->second : nullptr;
    };
    const auto preview_world = [](const TransientTransformParticipant &entry,
                                  int vertex_id) -> std::optional<QVec3> {
      if (vertex_id < 0) {
        return std::nullopt;
      }
      const auto found =
          entry.selected_vertex_lookup.find(
              static_cast<std::uint32_t>(vertex_id));
      if (found == entry.selected_vertex_lookup.end()) {
        return std::nullopt;
      }
      return transform_point(
          entry.world_transform,
          entry.selected_vertices[found->second].preview_position);
    };
    const auto hover_edge_inside_reference = [&preview_world](
        const TransientTransformParticipant &entry,
        const ViewportOverlayElementRef &element) -> std::optional<QVec3> {
      if (element.face_id < 0 || element.edge_a < 0 || element.edge_b < 0) {
        return std::nullopt;
      }
      const auto face =
          entry.face_vertices.find(static_cast<std::uint32_t>(element.face_id));
      if (face == entry.face_vertices.end() || face->second.size() < 3U) {
        return std::nullopt;
      }
      const auto current_world_position = [&entry, &preview_world](
                                                std::uint32_t vertex_id)
          -> std::optional<QVec3> {
        if (const std::optional<QVec3> preview =
                preview_world(entry, static_cast<int>(vertex_id))) {
          return preview;
        }
        const auto base = entry.base_positions.find(vertex_id);
        return base == entry.base_positions.end()
                   ? std::nullopt
                   : std::optional<QVec3>{
                         transform_point(entry.world_transform, base->second)};
      };

      QVec3 normal;
      std::optional<QVec3> directed_start;
      std::optional<QVec3> directed_end;
      for (std::size_t index = 0U; index < face->second.size(); ++index) {
        const std::uint32_t start_id = face->second[index];
        const std::uint32_t end_id =
            face->second[(index + 1U) % face->second.size()];
        const std::optional<QVec3> start = current_world_position(start_id);
        const std::optional<QVec3> end = current_world_position(end_id);
        if (!start.has_value() || !end.has_value()) {
          return std::nullopt;
        }
        normal += cross(*start, *end);
        if ((static_cast<int>(start_id) == element.edge_a &&
             static_cast<int>(end_id) == element.edge_b) ||
            (static_cast<int>(start_id) == element.edge_b &&
             static_cast<int>(end_id) == element.edge_a)) {
          directed_start = *start;
          directed_end = *end;
        }
      }
      if (!directed_start.has_value() || !directed_end.has_value()) {
        return std::nullopt;
      }
      constexpr float kDirectionEpsilonSquared = 1.0e-10F;
      const QVec3 directed_edge = *directed_end - *directed_start;
      const float edge_length = length(directed_edge);
      if (edge_length * edge_length <= kDirectionEpsilonSquared ||
          length_squared(normal) <= kDirectionEpsilonSquared) {
        return std::nullopt;
      }
      const QVec3 inward =
          normalize_or_zero(cross(normal, directed_edge / edge_length));
      if (length_squared(inward) <= kDirectionEpsilonSquared) {
        return std::nullopt;
      }
      return (*directed_start + *directed_end) * 0.5F +
             inward * (edge_length * 0.25F);
    };
    if (frame.hover_edge_dimension_context.has_value()) {
      ViewportHoverEdgeDimensionContext &context =
          *frame.hover_edge_dimension_context;
      const TransientTransformParticipant *participant =
          participant_for(context.element.object_id);
      if (participant != nullptr) {
        if (const std::optional<QVec3> inside_reference =
                hover_edge_inside_reference(*participant, context.element)) {
          context.inside_reference = *inside_reference;
        }
      }
    }
    for (ViewportOverlayPoint &point : frame.points) {
      const TransientTransformParticipant *participant =
          participant_for(point.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, point.element.vertex_id)) {
        point.position = *position;
      }
    }
    for (ViewportOverlayLine &line : frame.lines) {
      const TransientTransformParticipant *participant =
          participant_for(line.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, line.element.edge_a)) {
        line.start = *position;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, line.element.edge_b)) {
        line.end = *position;
      }
    }
    const auto remap_face_point = [&preview_world](
                                      const TransientTransformParticipant &entry,
                                      int face_id, QVec3 point) {
      if (face_id < 0) {
        return point;
      }
      const auto face =
          entry.face_vertices.find(static_cast<std::uint32_t>(face_id));
      if (face == entry.face_vertices.end()) {
        return point;
      }
      constexpr float kPositionToleranceSquared = 1.0e-8F;
      for (const std::uint32_t vertex_id : face->second) {
        const auto base = entry.base_positions.find(vertex_id);
        const std::optional<QVec3> preview =
            preview_world(entry, static_cast<int>(vertex_id));
        if (base == entry.base_positions.end() || !preview.has_value()) {
          continue;
        }
        const QVec3 base_world =
            transform_point(entry.world_transform, base->second);
        const QVec3 delta = point - base_world;
        if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <=
            kPositionToleranceSquared) {
          return *preview;
        }
      }
      return point;
    };
    for (ViewportOverlayTriangle &triangle : frame.triangles) {
      const TransientTransformParticipant *participant =
          participant_for(triangle.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      triangle.a = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.a);
      triangle.b = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.b);
      triangle.c = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.c);
    }
  }

  [[nodiscard]] ModelingCommandResult
  begin_translate_preview(bool duplicate_selected = false,
                           bool topology_stable_transform = false,
                           std::span<const ModelingStructuralEntityClone>
                               structural_duplicates = {}) {
    QDR_PROFILE_FUNCTION();
    (void)topology_stable_transform;
    if (preview_active()) {
      return failure("An SDK modeling preview is already active.");
    }
    std::vector<ObjectId> participants;
    if (!structural_duplicates.empty()) {
      participants.reserve(structural_duplicates.size());
      for (const ModelingStructuralEntityClone &clone : structural_duplicates) {
        const std::optional<ObjectId> object =
            sdk_object(clone.duplicate_modeling_object_id);
        if (!object.has_value()) {
          return failure("A structural duplicate has no live SDK identity.");
        }
        participants.push_back(*object);
      }
      std::ranges::sort(participants);
      participants.erase(std::unique(participants.begin(), participants.end()),
                         participants.end());
    } else {
      participants = selection_mode_ == ModelingSelectionMode::Object ||
                             selection_mode_ == ModelingSelectionMode::Group
                         ? selected_sdk_objects() : selected_component_objects();
    }
    if (participants.empty()) {
      return failure("No active SDK modeling selection.");
    }

    const std::vector<modeling::SessionObjectSummary> summaries =
        session_.objects();
    modeling::SessionPreviewTransactionRequest request;
    request.participants.reserve(participants.size());
    for (std::size_t index = 0; index < participants.size(); ++index) {
      const ObjectId object = participants[index];
      const auto summary = std::ranges::find(summaries, object,
                                             &modeling::SessionObjectSummary::id);
      if (summary == summaries.end()) {
        return failure("Transform preview participant is not live.");
      }
      std::optional<modeling::SessionPreviewDocument> document;
      if (summary->kind == modeling::SessionObjectKind::Curve) {
        Result<modeling::CurveDocument> copied =
            session_.curve_document_copy(object);
        if (!copied.ok()) {
          return failure(copied.error_message());
        }
        document.emplace(std::move(copied).value());
      } else {
        Result<PolygonDocument> copied = session_.document_copy(object);
        if (!copied.ok()) {
          return failure(copied.error_message());
        }
        document.emplace(std::move(copied).value());
      }
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const ModelingObjectComponent *modeling_object =
          entity.has_value() ? scene_.modeling_object(*entity) : nullptr;
      request.participants.push_back({
          .action = modeling::SessionPreviewAction::ReplaceExisting,
          .object = object,
          .document = std::move(document),
          .receipt = {},
          .name = summary->name,
          .material_slot = modeling_object != nullptr
                               ? modeling_object->material_slot
                               : 0u,
          .selected = summary->selected,
      });
    }
    request.replace_object_selection = false;

    if (!capture_mesh_preview_host_baseline(participants)) {
      return failure("Bounded transform preview baseline is unavailable.");
    }
    Result<modeling::SessionPreviewBeginResult> begun =
        session_.begin_preview_transaction("translate_selection",
                                           std::move(request));
    if (!begun.ok()) {
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    if (!activate_mesh_preview_host_baseline(begun.value().preview_id) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return abort_preview_publication(
          begun.value().preview_id, "Transform preview could not begin.");
    }
    committed_transform_history_delta_ = {};
    committed_transform_undo_mark_.reset();
    preview_changed_ =
        duplicate_selected || !structural_duplicates.empty();
    transient_transform_payload_participants_.clear();
    transient_transform_payload_active_ = false;
    active_preview_operation_id_ = "translate_selection";
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    active_edge_bevel_maximum_width_.reset();
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          begun.value().preview_id,
          "Transform preview baseline could not be acknowledged.");
    }
    return {.ok = true,
            .changed = duplicate_selected || !structural_duplicates.empty(),
            .message = "Transform preview started."};
  }

  [[nodiscard]] ModelingCommandResult
  update_workspace_transform_preview_by_object(
      const DocumentMutationByObject &mutation,
      std::string_view fallback_message,
      bool position_only_projection = false,
      const CurveDocumentMutationByObject &curve_mutation = {},
      bool preview_preflighted = false,
      const WorkspaceTransformResolver &layout_transform = {},
      std::vector<modeling::SessionPreviewDocumentUpdate> updates = {}) {
    QDR_PROFILE_FUNCTION();
    if (!session_.preview_active()) {
      return failure("No active SDK workspace transform preview.");
    }
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preview_preflighted && !preflight_preview_mutation(preview.id)) {
      return failure(
          "Transform preview participants changed outside this preview.");
    }
    if (unchanged_affine_preview_input(preview, layout_transform)) {
      return {.ok = true, .changed = false,
              .message = "Transform preview unchanged."};
    }
    const bool prepared = !updates.empty();
    if (!prepared) {
      updates.reserve(preview.objects.size());
    }
    bool changed = false;
    std::size_t deleted_vertex_count = 0u;
    std::string message;
    if (!prepared) {
      for (const modeling::PreviewTransactionObjectSummary &participant :
           preview.objects) {
        if (participant.action ==
            modeling::SessionPreviewAction::RemoveExisting) {
          continue;
        }
        Result<OperationResult> operated;
        std::optional<modeling::SessionPreviewDocument> document;
        if (participant.kind == modeling::SessionObjectKind::Curve) {
          Result<modeling::CurveDocument> base =
              session_.preview_base_curve_document_copy(preview.id,
                                                        participant.object_id);
          if (!base.ok()) {
            return failure(base.error_message());
          }
          if (curve_mutation) {
            operated = curve_mutation(participant.object_id, base.value());
          } else {
            OperationResult unchanged;
            unchanged.success = true;
            unchanged.message = "Curve transform preview unchanged.";
            operated = Result<OperationResult>::success(std::move(unchanged));
          }
          document.emplace(std::move(base).value());
        } else {
          Result<PolygonDocument> base = session_.preview_base_document_copy(
              preview.id, participant.object_id);
          if (!base.ok()) {
            return failure(base.error_message());
          }
          operated = mutation(participant.object_id, base.value());
          document.emplace(std::move(base).value());
        }
        if (!operated.ok() || !operated.value().success) {
          return failure(operated.ok() && !operated.value().message.empty()
                             ? operated.value().message
                             : (operated.error_message().empty()
                                    ? std::string(fallback_message)
                                    : operated.error_message()));
        }
        updates.push_back({
            .object = participant.object_id,
            .document = std::move(*document),
            .receipt = std::move(operated).value(),
        });
      }
    }
    for (const auto &update : updates) {
      changed = changed || update.receipt.changed;
      deleted_vertex_count += update.receipt.deleted.vertices.size();
      if (!update.receipt.message.empty()) {
        message = update.receipt.message;
      }
    }
    Result<OperationResult> updated = session_.update_preview_transaction(
        preview.id, std::move(updates));
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    if (mesh_preview_host_baseline_.has_value()) {
      auto &baseline = *mesh_preview_host_baseline_;
      auto &layout_objects = baseline.preserve_uv_layout_objects;
      layout_objects.clear();
      baseline.affine_inputs.clear();
      baseline.affine_input_revision.reset();
      if (layout_transform) {
        layout_objects.reserve(preview.objects.size());
        baseline.affine_inputs.reserve(preview.objects.size());
        for (const auto &participant : preview.objects) {
          if (participant.action == modeling::SessionPreviewAction::RemoveExisting) {
            continue;
          }
          const auto transform = layout_transform(participant.object_id);
          baseline.affine_inputs.push_back({participant.object_id, transform});
          if (participant.kind == modeling::SessionObjectKind::Mesh &&
              transform.has_value() && has_nonsingular_transform(*transform)) {
            layout_objects.push_back(participant.object_id);
          }
        }
      }
    }
    const bool publish_host_projection =
        !position_only_projection && (preview_changed_ || changed);
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        (publish_host_projection && !project_preview_object())) {
      return abort_preview_publication(
          preview.id, "Transform preview publication failed.");
    }
    active_preview_deleted_vertex_count_ = deleted_vertex_count;
    preview_changed_ = changed || updated.value().changed;
    if (publish_host_projection) {
      ++stats_.preview_publication_count;
    }
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "Transform preview publication could not be acknowledged.");
    }
    if (layout_transform && updated.value().success &&
        mesh_preview_host_baseline_.has_value()) {
      mesh_preview_host_baseline_->affine_input_revision =
          mesh_preview_host_baseline_->acknowledged_sdk.preview_content_revision;
    }
    return {.ok = updated.value().success,
            .changed = updated.value().changed,
            .message = message.empty() ? std::string(fallback_message)
                                       : std::move(message)};
  }

  [[nodiscard]] ModelingCommandResult update_workspace_transform_preview(
      const DocumentMutation &mutation, std::string_view fallback_message,
      bool position_only_projection = false) {
    return update_workspace_transform_preview_by_object(
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        fallback_message, position_only_projection);
  }

  [[nodiscard]] ModelingCommandResult update_translate_preview(QVec3 delta) {
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (!finite(delta)) {
      return failure("Translation delta must be finite.");
    }
    const ModelingCommandResult payload =
        ensure_component_transform_payload();
    if (!payload.ok) {
      return payload;
    }
    const ModelingSelectionMode mode = selection_mode_;
    const DocumentMutation mutation =
        [delta, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.translate_selection(to_sdk(delta));
          }
          const auto dormant = document.selection();
          auto selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          auto translated = document.translate_selection(to_sdk(delta));
          if (!translated.ok()) {
            return translated;
          }
          auto restored = restore_dormant_selection(document, dormant, translated.value());
          if (!restored.ok()) {
            return restored;
          }
          translated.value().dirty.selection = false;
          return translated;
        };
    const quader::modeling::Transform3 curve_transform{
        .origin = to_sdk(delta),
    };
    const bool position_only_projection = transient_transform_payload_active_;
    bool preview_preflighted = false;
    if (position_only_projection) {
      const modeling::PreviewTransactionSummary preview =
          session_.preview_summary();
      if (!preflight_preview_mutation(preview.id)) {
        return failure(
            "Transform preview participants changed outside this preview.");
      }
      preview_preflighted = true;
      const WorkspaceTransformResolver transient_transform = [curve_transform](ObjectId) {
        return std::optional{curve_transform};
      };
      const DocumentMutationByObject transient_mutation =
          [mutation](ObjectId, PolygonDocument &document) { return mutation(document); };
      if (auto fused = update_affine_transient_transform_payload(
              transient_transform, transient_mutation, "SDK translate preview failed.", true)) {
        return std::move(*fused);
      }
      ModelingCommandResult transient = update_transient_transform_payload(
          transient_transform, transient_mutation, "Transform preview updated.");
      if (!transient.ok) {
        return transient;
      }
    }
    ModelingCommandResult result = update_workspace_transform_preview_by_object(
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        "SDK translate preview failed.", position_only_projection,
        [curve_transform](ObjectId, modeling::CurveDocument &document) {
          return document.transform(curve_transform);
        },
        preview_preflighted, [curve_transform](ObjectId) {
          return std::optional{curve_transform};
        });
    if (!result.ok && position_only_projection &&
        !initialize_transient_transform_payload()) {
      result.changed = true;
      result.message += " Transient transform payload repair failed.";
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult update_transform_preview(
      const ModelingTransform3 &transform, QVec3 pivot) {
    const ModelingCommandResult payload =
        ensure_component_transform_payload();
    if (!payload.ok) {
      return payload;
    }
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform, pivot);
    const ModelingSelectionMode mode = selection_mode_;
    const DocumentMutation mutation =
        [sdk_transform, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.transform_selection(sdk_transform);
          }
          const auto dormant = document.selection();
          auto selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          auto transformed = document.transform_selection(sdk_transform);
          if (!transformed.ok()) {
            return transformed;
          }
          auto restored = restore_dormant_selection(document, dormant, transformed.value());
          if (!restored.ok()) {
            return restored;
          }
          transformed.value().dirty.selection = false;
          return transformed;
        };
    const bool position_only_projection = transient_transform_payload_active_;
    bool preview_preflighted = false;
    if (position_only_projection) {
      const modeling::PreviewTransactionSummary preview =
          session_.preview_summary();
      if (!preflight_preview_mutation(preview.id)) {
        return failure(
            "Transform preview participants changed outside this preview.");
      }
      preview_preflighted = true;
      const WorkspaceTransformResolver transient_transform = [sdk_transform](ObjectId) {
        return std::optional{sdk_transform};
      };
      const DocumentMutationByObject transient_mutation =
          [mutation](ObjectId, PolygonDocument &document) { return mutation(document); };
      if (auto fused = update_affine_transient_transform_payload(
              transient_transform, transient_mutation, "SDK transform preview failed.")) {
        return std::move(*fused);
      }
      ModelingCommandResult transient = update_transient_transform_payload(
          transient_transform, transient_mutation, "Transform preview updated.");
      if (!transient.ok) {
        return transient;
      }
    }
    ModelingCommandResult result = update_workspace_transform_preview_by_object(
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        "SDK transform preview failed.", position_only_projection,
        [sdk_transform](ObjectId, modeling::CurveDocument &document) {
          return document.transform(sdk_transform);
        },
        preview_preflighted, [sdk_transform](ObjectId) {
          return std::optional{sdk_transform};
        });
    if (!result.ok && position_only_projection &&
        !initialize_transient_transform_payload()) {
      result.changed = true;
      result.message += " Transient transform payload repair failed.";
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult update_object_transform_preview(
      std::span<const ModelingSdkObjectTransformPreview> transforms,
      ModelingSdkTransformPreviewKind kind,
      float closed_edge_ledge_size) {
    if (kind == ModelingSdkTransformPreviewKind::Transform) {
      const ModelingCommandResult payload =
          ensure_component_transform_payload();
      if (!payload.ok) {
        return payload;
      }
    }
    /// Stores one resolved SDK object and its portable preview transform.
    struct SdkTransform {
      ObjectId object;
      quader::modeling::Transform3 transform;
    };
    std::vector<SdkTransform> sdk_transforms;
    sdk_transforms.reserve(transforms.size());
    for (const ModelingSdkObjectTransformPreview &transform : transforms) {
      const std::optional<ObjectId> object =
          sdk_object(transform.modeling_object_id);
      if (!object.has_value()) {
        return failure("Transform preview target is not live.");
      }
      sdk_transforms.push_back({
          .object = *object,
          .transform = to_sdk(transform.transform),
      });
    }
    std::ranges::sort(sdk_transforms, {}, &SdkTransform::object);
    const auto resolved_transforms =
        std::make_shared<const std::vector<SdkTransform>>(
            std::move(sdk_transforms));
    const DocumentMutationByObject mutation =
        [resolved_transforms, kind,
         closed_edge_ledge_size](ObjectId object, PolygonDocument &document) {
          const auto found = std::ranges::lower_bound(
              *resolved_transforms, object, {}, &SdkTransform::object);
          if (found == resolved_transforms->end() || found->object != object) {
            OperationResult unchanged;
            unchanged.message = "Transform preview unchanged.";
            return Result<OperationResult>::success(std::move(unchanged));
          }
          switch (kind) {
          case ModelingSdkTransformPreviewKind::Transform:
            return document.transform_selection(found->transform);
          case ModelingSdkTransformPreviewKind::ExtrudeOffset:
            return document.extrude_selected_elements(
                found->transform.origin, closed_edge_ledge_size);
          case ModelingSdkTransformPreviewKind::TransformExtrude:
            return document.transform_extrude_selected_elements(
                found->transform, {},
                closed_edge_ledge_size > 0.0F ? closed_edge_ledge_size : 1.0F);
          case ModelingSdkTransformPreviewKind::Inset:
            return document.inset_selected_elements(found->transform, {});
          }
          return Result<OperationResult>::failure(
              quader::modeling::make_error(
                  quader::modeling::ErrorCode::UnsupportedOperation,
                  "Transform preview kind is unsupported."));
        };
    const CurveDocumentMutationByObject curve_mutation =
        [resolved_transforms, kind](ObjectId object,
                                    modeling::CurveDocument &document) {
          if (kind != ModelingSdkTransformPreviewKind::Transform) {
            OperationResult unchanged;
            unchanged.success = true;
            unchanged.message = "Curve preview unchanged.";
            return Result<OperationResult>::success(std::move(unchanged));
          }
          const auto found = std::ranges::lower_bound(
              *resolved_transforms, object, {}, &SdkTransform::object);
          if (found == resolved_transforms->end() || found->object != object) {
            OperationResult unchanged;
            unchanged.success = true;
            unchanged.message = "Curve preview unchanged.";
            return Result<OperationResult>::success(std::move(unchanged));
          }
          return document.transform(found->transform);
        };
    const bool position_only_projection =
        kind == ModelingSdkTransformPreviewKind::Transform &&
        transient_transform_payload_active_;
    bool preview_preflighted = false;
    if (position_only_projection) {
      const modeling::PreviewTransactionSummary preview =
          session_.preview_summary();
      if (!preflight_preview_mutation(preview.id)) {
        return failure(
            "Transform preview participants changed outside this preview.");
      }
      preview_preflighted = true;
      const WorkspaceTransformResolver transient_transform =
          [resolved_transforms](ObjectId object) -> std::optional<modeling::Transform3> {
        const auto found = std::ranges::lower_bound(
            *resolved_transforms, object, {}, &SdkTransform::object);
        return found != resolved_transforms->end() && found->object == object
                   ? std::optional{found->transform}
                   : std::nullopt;
      };
      if (auto fused = update_affine_transient_transform_payload(
              transient_transform, mutation, "SDK object-local transform preview failed.")) {
        return std::move(*fused);
      }
      ModelingCommandResult transient = update_transient_transform_payload(
          transient_transform, mutation, "Transform preview updated.");
      if (!transient.ok) {
        return transient;
      }
    }
    WorkspaceTransformResolver layout_transform;
    if (kind == ModelingSdkTransformPreviewKind::Transform) {
      layout_transform = [resolved_transforms](ObjectId object)
          -> std::optional<modeling::Transform3> {
        const auto found = std::ranges::lower_bound(
            *resolved_transforms, object, {}, &SdkTransform::object);
        return found != resolved_transforms->end() && found->object == object
                   ? std::optional{found->transform}
                   : std::nullopt;
      };
    }
    ModelingCommandResult result = update_workspace_transform_preview_by_object(
        mutation, "SDK object-local transform preview failed.",
        position_only_projection, curve_mutation, preview_preflighted,
        layout_transform);
    if (!result.ok && position_only_projection &&
        !initialize_transient_transform_payload()) {
      result.changed = true;
      result.message += " Transient transform payload repair failed.";
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult
  update_extrude_preview(QVec3 offset, float closed_edge_ledge_size) {
    const DocumentMutation mutation =
        [offset, closed_edge_ledge_size](PolygonDocument &document) {
          return document.extrude_selected_elements(
              to_sdk(offset), closed_edge_ledge_size);
        };
    return update_workspace_transform_preview(
        mutation, "SDK extrude preview failed.");
  }

  [[nodiscard]] ModelingCommandResult
  update_inset_preview(const ModelingTransform3 &transform, QVec3 pivot) {
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform);
    const DocumentMutation mutation =
        [sdk_transform, pivot](PolygonDocument &document) {
          return document.inset_selected_elements(sdk_transform,
                                                   to_sdk(pivot));
        };
    return update_workspace_transform_preview(
        mutation, "SDK inset preview failed.");
  }

  [[nodiscard]] ModelingSdkTransformHistoryDelta
  build_transform_history_delta() const {
    QDR_PROFILE_SCOPE("Transform history build sparse authored positions");
    auto delta =
        std::make_shared<ModelingSdkTransformHistoryDelta::Impl>();
    delta->objects.reserve(transient_transform_payload_participants_.size());
    for (const TransientTransformParticipant &participant :
         transient_transform_payload_participants_) {
      ModelingSdkTransformHistoryDelta::Impl::ObjectDelta object_delta;
      object_delta.object = participant.object;
      object_delta.before.reserve(participant.selected_vertices.size());
      object_delta.after.reserve(participant.selected_vertices.size());
      for (const TransientTransformSelectedVertex &vertex :
           participant.selected_vertices) {
        if (vertex.base_position.x == vertex.preview_position.x &&
            vertex.base_position.y == vertex.preview_position.y &&
            vertex.base_position.z == vertex.preview_position.z) {
          continue;
        }
        const VertexId id =
            quader::modeling::make_id<quader::modeling::VertexTag>(
                vertex.source_vertex_id);
        object_delta.before.push_back(
            {.vertex = id, .position = to_sdk(vertex.base_position)});
        object_delta.after.push_back(
            {.vertex = id, .position = to_sdk(vertex.preview_position)});
      }
      if (!object_delta.before.empty()) {
        delta->objects.push_back(std::move(object_delta));
      }
    }
    return delta->objects.empty()
               ? ModelingSdkTransformHistoryDelta{}
               : ModelingSdkTransformHistoryDelta(std::move(delta));
  }

  [[nodiscard]] bool transform_preview_supports_sparse_history() const {
    const auto preview = session_.preview_summary();
    return preview.active && !preview.objects.empty() &&
           std::ranges::all_of(preview.objects, [](const auto &participant) {
             return participant.kind == modeling::SessionObjectKind::Mesh &&
                    participant.action ==
                        modeling::SessionPreviewAction::ReplaceExisting;
           });
  }

  /** Stages texture layout and welding on the same detached commit documents. */
  [[nodiscard]] ModelingCommandResult prepare_transform_acceptance(
      const modeling::PreviewTransactionSummary &preview,
      std::vector<modeling::SessionPreviewDocumentUpdate> &updates,
      ModelingSdkTransformHistoryDelta &history_delta,
      bool auto_merge_vertices) const {
    QDR_PROFILE_FUNCTION();
    const bool whole_objects = selection_mode_ == ModelingSelectionMode::Object ||
                               selection_mode_ == ModelingSelectionMode::Group;
    const bool merge_vertices = auto_merge_vertices && !whole_objects;
    std::unordered_set<std::uint64_t> preserve_layout_objects;
    if (mesh_preview_host_baseline_.has_value()) {
      for (const auto object : mesh_preview_host_baseline_->preserve_uv_layout_objects) {
        preserve_layout_objects.insert(sdk_object_key(object));
      }
    }
    if (!merge_vertices && preserve_layout_objects.empty()) {
      return {.ok = true};
    }
    auto data = history_delta.valid()
                    ? std::make_shared<ModelingSdkTransformHistoryDelta::Impl>(
                          *history_delta.impl_)
                    : std::make_shared<ModelingSdkTransformHistoryDelta::Impl>();
    const bool record_document_history = history_delta.valid() || merge_vertices;
    bool changed = false;
    for (const auto &participant : preview.objects) {
      if (participant.action == modeling::SessionPreviewAction::RemoveExisting) {
        continue;
      }
      if (participant.kind == modeling::SessionObjectKind::Curve) {
        auto curve = session_.preview_curve_document_copy(
            preview.id, participant.object_id);
        if (!curve.ok()) {
          return failure(curve.error_message());
        }
        updates.push_back({.object = participant.object_id,
                           .document = std::move(curve).value(),
                           .receipt = {.success = true}});
        continue;
      }
      Result<PolygonDocument> document = session_.preview_document_copy(
          preview.id, participant.object_id);
      if (!document.ok()) {
        return failure(document.error_message());
      }
      OperationResult receipt;
      if (preserve_layout_objects.contains(sdk_object_key(participant.object_id))) {
        auto baseline = session_.preview_base_document_copy(
            preview.id, participant.object_id);
        if (!baseline.ok()) return failure(baseline.error_message());
        auto preserved =
            MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
                baseline.value(), document.value(), whole_objects);
        if (!preserved.ok() || !preserved.value().success) {
          return failure(preserved.ok() ? preserved.value().message
                                        : preserved.error_message());
        }
        receipt = std::move(preserved).value();
      }
      if (merge_vertices) {
        auto merged = document.value().auto_merge_selected_vertices(0.001F);
        if (!merged.ok() || !merged.value().success) {
          return failure(merged.ok() ? merged.value().message
                                     : merged.error_message());
        }
        if (merged.value().changed) {
          const bool layout_changed = receipt.changed;
          receipt = std::move(merged).value();
          receipt.dirty.materials = receipt.dirty.materials || layout_changed;
        } else if (!receipt.changed) {
          receipt = std::move(merged).value();
        }
      }
      if (!receipt.changed) {
        updates.push_back({.object = participant.object_id,
                           .document = std::move(document).value(),
                           .receipt = std::move(receipt)});
        continue;
      }
      changed = true;
      if (record_document_history &&
          participant.action == modeling::SessionPreviewAction::ReplaceExisting) {
        Result<PolygonDocument> before =
            session_.document_copy(participant.object_id);
        if (!before.ok()) {
          return failure(before.error_message());
        }
        auto entry = std::ranges::find(
            data->objects, participant.object_id,
            &ModelingSdkTransformHistoryDelta::Impl::ObjectDelta::object);
        if (entry == data->objects.end()) {
          data->objects.push_back({.object = participant.object_id});
          entry = std::prev(data->objects.end());
        }
        entry->before.clear();
        entry->after.clear();
        entry->topology_before = std::move(before).value();
        entry->topology_after = document.value();
      }
      updates.push_back({.object = participant.object_id,
                         .document = std::move(document).value(),
                         .receipt = std::move(receipt)});
    }
    if (changed) {
      history_delta = ModelingSdkTransformHistoryDelta(std::move(data));
    } else {
      updates.clear();
    }
    return {.ok = true, .changed = changed};
  }

  /// Reuses only native candidates that exactly match the accepted authored
  /// document. SDK and packed transforms can round differently, so unmatched
  /// participants still pass through canonical compilation before publication.
  [[nodiscard]] bool project_transient_transform_commit(
      const modeling::PreviewTransactionSummary &preview,
      ModelingSdkTransformHistoryDelta &history_delta,
      std::shared_ptr<ModelingAcceptedOverlayCoordinates> &coordinates) {
    QDR_PROFILE_FUNCTION();
    std::unordered_map<std::uint64_t, const TransientTransformParticipant *>
        participants;
    participants.reserve(transient_transform_payload_participants_.size());
    for (const auto &participant : transient_transform_payload_participants_) {
      participants.emplace(sdk_object_key(participant.object), &participant);
    }
    struct PreparedMesh {
      ObjectId object;
      MeshAsset mesh;
      bool retained = false;
    };
    std::vector<PreparedMesh> prepared;
    prepared.reserve(preview.objects.size());
    std::unordered_map<const MeshGeometryProjection *, std::vector<std::size_t>>
        authored_families;
    authored_families.reserve(preview.objects.size());
    std::unordered_set<std::uint64_t> changed_objects;
    changed_objects.reserve(preview.objects.size());
    bool coordinate_compatible =
        selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group &&
        preview.objects.size() == participants.size() && !participants.empty() &&
        (!instance_edit_scope_provider_ ||
         instance_edit_scope_provider_().definition == kInvalidEntityId);
    auto accepted = std::make_shared<ModelingAcceptedOverlayCoordinates>();
    accepted->source_content_revision = content_revision();
    accepted->source_selection_revision = selection_revision();
    accepted->source_scene_revision = scene_.revision();
    accepted->source_object_map_revision = object_map_.revision();
    for (const auto &object : preview.objects) {
      const auto document = session_.preview_document_copy(preview.id, object.object_id);
      if (!document.ok()) return false;
      const auto found = participants.find(sdk_object_key(object.object_id));
      const bool exact_coordinates = found != participants.end() &&
          found->second->geometry_projection &&
          document.value().content_identity() ==
              found->second->preview_document.content_identity();
      coordinate_compatible = coordinate_compatible && exact_coordinates &&
          found->second->geometry_projection->positions &&
          found->second->geometry_projection->positions->size() ==
              found->second->preview_mesh.geometry().vertices.size() &&
          std::ranges::equal(found->second->geometry_projection->indices(),
                             found->second->preview_mesh.geometry().indices) &&
          scene_.containing_instance_definition(found->second->entity) ==
              kInvalidEntityId;
      if (!object.changed) continue;
      changed_objects.insert(sdk_object_key(object.object_id));
      if (exact_coordinates) {
        MeshAsset mesh = MeshProjectionService::materialize_retained_mesh_projection(
            document.value(), found->second->preview_mesh,
            found->second->geometry_projection);
        auto &family = authored_families[found->second->geometry_projection.get()];
        const auto equivalent = std::ranges::find_if(family, [&](std::size_t index) {
          return same_authored_read_view(
              mesh.authored_polygon_document.read_view(),
              prepared[index].mesh.authored_polygon_document.read_view());
        });
        if (equivalent != family.end()) {
          // Only the immutable native publication is shared. Each SDK document
          // keeps its own selection and identity cursors for subsequent edits.
          mesh.authored_polygon_document =
              prepared[*equivalent].mesh.authored_polygon_document;
        } else {
          family.push_back(prepared.size());
        }
        prepared.push_back({object.object_id, std::move(mesh), true});
        continue;
      }
      const auto authored = MeshProjectionService::polygon_document_from_sdk_document(
          document.value());
      if (!authored.has_value()) return false;
      const MeshAsset *retained = found != participants.end()
          ? &found->second->preview_mesh : nullptr;
      if (retained != nullptr && retained->has_authored_polygon_document &&
          retained->authored_polygon_document &&
          mesh_asset_payload_detail::same_polygon_document(
              *retained->authored_polygon_document, *authored)) {
        prepared.push_back({object.object_id, *retained, true});
      } else {
        ++stats_.mesh_compile_count;
        auto compiled = MeshProjectionService::compile_sdk_document_mesh(document.value());
        if (!compiled.has_value()) return false;
        prepared.push_back({object.object_id, std::move(*compiled)});
      }
    }
    const bool unchanged_history_participant = history_delta.valid() &&
        std::ranges::any_of(history_delta.impl_->objects, [&](const auto &object) {
          return !changed_objects.contains(sdk_object_key(object.object));
        });
    if (unchanged_history_participant ||
        std::ranges::any_of(prepared, [](const auto &object) { return !object.retained; })) {
      auto canonical_history = history_delta.valid()
          ? std::make_shared<ModelingSdkTransformHistoryDelta::Impl>(*history_delta.impl_)
          : std::make_shared<ModelingSdkTransformHistoryDelta::Impl>();
      // Packed arithmetic can move a participant that the accepted SDK left
      // unchanged. Such an entry must not reappear as a mutation during redo.
      std::erase_if(canonical_history->objects, [&](const auto &object) {
        return !changed_objects.contains(sdk_object_key(object.object));
      });
      std::unordered_map<std::uint64_t, std::size_t> history_entries;
      history_entries.reserve(canonical_history->objects.size());
      for (std::size_t index = 0U; index < canonical_history->objects.size(); ++index) {
        history_entries.emplace(sdk_object_key(canonical_history->objects[index].object), index);
      }
      for (const auto &object : prepared) {
        if (object.retained) continue;
        const auto participant = participants.find(sdk_object_key(object.object));
        if (participant == participants.end() || !object.mesh.authored_polygon_document) {
          return false;
        }
        std::unordered_map<std::uint32_t, QVec3> positions;
        positions.reserve(object.mesh.authored_polygon_document->vertices.size());
        for (const auto &vertex : object.mesh.authored_polygon_document->vertices) {
          positions.emplace(vertex.id, vertex.position);
        }
        ModelingSdkTransformHistoryDelta::Impl::ObjectDelta entry{.object = object.object};
        entry.before.reserve(participant->second->selected_vertices.size());
        entry.after.reserve(participant->second->selected_vertices.size());
        for (const auto &vertex : participant->second->selected_vertices) {
          const auto accepted = positions.find(vertex.source_vertex_id);
          if (accepted == positions.end()) return false;
          const QVec3 position = accepted->second;
          if (position.x == vertex.base_position.x &&
              position.y == vertex.base_position.y &&
              position.z == vertex.base_position.z) continue;
          const VertexId id = modeling::make_id<modeling::VertexTag>(vertex.source_vertex_id);
          entry.before.push_back({.vertex = id, .position = to_sdk(vertex.base_position)});
          entry.after.push_back({.vertex = id, .position = to_sdk(position)});
        }
        const auto previous = history_entries.find(sdk_object_key(object.object));
        if (previous != history_entries.end()) {
          canonical_history->objects[previous->second] = std::move(entry);
        } else if (!entry.after.empty()) {
          canonical_history->objects.push_back(std::move(entry));
        }
      }
      history_delta = ModelingSdkTransformHistoryDelta(std::move(canonical_history));
    }
    // Finish all derivation before touching assets. The existing preview owner
    // retains the rollback baseline and any private bindings created below.
    {
      QDR_PROFILE_SCOPE("Transform commit publish prepared meshes");
      for (auto &object : prepared) {
        if (!publish_projected_mesh(nullptr, object.object, std::move(object.mesh),
                                    false, std::nullopt, true)) {
          return false;
        }
      }
    }
    if (coordinate_compatible) {
      QDR_PROFILE_SCOPE("Transform commit retain accepted overlay coordinates");
      QDR_PROFILE_PLOT("Accepted overlay participant count",
                       static_cast<std::int64_t>(participants.size()));
      accepted->meshes = workspace_transform_preview_meshes();
      for (auto &mesh : accepted->meshes) mesh.suppress_source = false;
      coordinates = std::move(accepted);
    } else {
      QDR_PROFILE_SCOPE("Transform commit overlay coordinates canonical fallback");
      QDR_PROFILE_PLOT("Accepted overlay participant count", std::int64_t{0});
    }
    return true;
  }

  [[nodiscard]] ModelingCommandResult
  commit_translate_preview(bool auto_merge_vertices = false) {
    QDR_PROFILE_FUNCTION();
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    auto_merge_vertices = auto_merge_vertices &&
                          selection_mode_ == ModelingSelectionMode::Vertex;
    if (!preview_changed_ && !auto_merge_vertices) {
      const bool curve_preview = curve_document_preview_active();
      ModelingCommandResult canceled = cancel_translate_preview();
      if (canceled.ok) {
        canceled.changed = false;
        canceled.message =
            curve_preview ? "Curve unchanged." : "Preview unchanged.";
      }
      return canceled;
    }

    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview.id)) {
      return failure("Preview commit baseline is unavailable.");
    }
    ModelingSdkTransformHistoryDelta transform_history_delta =
        transient_transform_payload_active_ ? build_transform_history_delta()
                                            : ModelingSdkTransformHistoryDelta{};
    std::vector<modeling::SessionPreviewDocumentUpdate> prepared_updates;
    const ModelingCommandResult prepared = prepare_transform_acceptance(
        preview, prepared_updates, transform_history_delta, auto_merge_vertices);
    if (!prepared.ok) {
      return prepared;
    }
    if (!preview_changed_ && !prepared.changed) {
      ModelingCommandResult canceled = cancel_translate_preview();
      if (canceled.ok) {
        canceled.changed = false;
        canceled.message = "Preview unchanged.";
      }
      return canceled;
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Preview participants changed outside this preview.");
    }

    struct RemovedHostObject {
      int modeling_object_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
    };
    std::vector<RemovedHostObject> removed;
    std::vector<EntityId> preview_selected;
    bool structural_preview = false;
    removed.reserve(preview.objects.size());
    preview_selected.reserve(preview.objects.size());
    for (const modeling::PreviewTransactionObjectSummary &object :
         preview.objects) {
      structural_preview =
          structural_preview ||
          object.action != modeling::SessionPreviewAction::ReplaceExisting;
      const std::optional<int> native = native_object(object.object_id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return failure("Preview object has no live host identity.");
      }
      if (object.action ==
          modeling::SessionPreviewAction::RemoveExisting) {
        removed.push_back({.modeling_object_id = *native, .entity = *entity});
      } else if (scene_.mesh(*entity) == nullptr &&
                 scene_.curve(*entity) == nullptr) {
        return failure("Preview object has no published host payload.");
      }
      if (object.selected &&
          object.action != modeling::SessionPreviewAction::RemoveExisting) {
        preview_selected.push_back(*entity);
      }
    }

    // All injected/recoverable publication failures are decided while the
    // SDK preview and retained owner baseline are still untouched.
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return failure("Injected failure before SDK preview commit.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::CommittedAssetReplace) ||
        fails(ModelingSdkProjectionFailurePoint::CommittedSceneNotify)) {
      return failure("Committed preview projection could not be published.");
    }
    const bool prepared_document_changes = !prepared_updates.empty();
    std::shared_ptr<ModelingAcceptedOverlayCoordinates> accepted_coordinates;
    if (prepared_document_changes) {
      const auto candidate_checkpoint =
          session_.capture_preview_checkpoint(preview.id);
      if (!candidate_checkpoint.ok()) {
        return failure(candidate_checkpoint.error_message());
      }
      Result<OperationResult> updated = session_.update_preview_transaction(
          preview.id, std::move(prepared_updates));
      if (!updated.ok() || !updated.value().success ||
          fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
        // Acceptance can stage topology changes such as an automatic weld.
        // Restore the user's live candidate, not the pre-gesture document.
        const auto restored =
            session_.restore_preview_checkpoint(candidate_checkpoint.value());
        if (!restored.ok() || !preview_fence_matches(preview.id)) {
          return {.ok = false, .changed = true,
                  .message = "Transform acceptance and candidate restoration failed."};
        }
        return failure(updated.ok() ? "Prepared transform could not publish."
                                    : updated.error_message());
      }
      preview_changed_ = true;
    }
    if (transient_transform_payload_active_ || prepared_document_changes ||
        face_extrusion_preview_active()) {
      const bool retained_candidate = !prepared_document_changes &&
          has_acknowledged_affine_transform_preview() &&
          mesh_preview_host_baseline_->affine_input_revision == preview.content_revision &&
          std::ranges::all_of(preview.objects, [](const auto &object) {
            return object.kind == modeling::SessionObjectKind::Mesh &&
                   object.action == modeling::SessionPreviewAction::ReplaceExisting;
          });
      const bool projected = retained_candidate
          ? project_transient_transform_commit(preview, transform_history_delta,
                                               accepted_coordinates)
          : project_preview_object(true, true);
      if (!projected) {
        return abort_preview_publication(
            preview.id,
            "Committed transform projection could not be published.");
      }
    }

    if (structural_preview && editor_mode_service_ != nullptr &&
        editor_mode_service_->mode() == EditorMode::Edit) {
      preview_selected.clear();
      for (const auto &participant : editor_mode_service_->snapshot().participants) {
        if (std::ranges::none_of(removed, [&](const RemovedHostObject &object) {
              return object.entity == participant.entity;
            })) {
          preview_selected.push_back(participant.entity);
        }
      }
    }
    if (structural_preview) {
      std::vector<EntityId> combined;
      for (const EntityId entity : scene_.selected_entities()) {
        if (!owns_entity(entity)) {
          combined.push_back(entity);
        }
      }
      combined.insert(combined.end(), preview_selected.begin(),
                      preview_selected.end());
      EntityId active = preview_selected.empty()
                            ? (combined.empty() ? kInvalidEntityId
                                                : combined.front())
                            : preview_selected.back();
      const SceneSelectionState desired =
          make_scene_selection(scene_, combined, active);
      (void)apply_scene_selection(scene_, desired,
                                  SceneSelectionApplicationMode::RuntimeSync);
      if (!same_scene_selection(capture_scene_selection(scene_), desired)) {
        return abort_preview_publication(
            preview.id,
            "Committed preview selection could not be published.");
      }
    }

    if (structural_preview_host_state_.has_value() &&
        structural_preview_host_state_->merge_sources.has_value()) {
      const auto is_removed = [&removed](EntityId entity) {
        return std::ranges::find(removed, entity, &RemovedHostObject::entity) != removed.end();
      };
      // Removing an operand must not recursively delete unrelated descendants,
      // including the survivor when it was parented below that operand.
      for (const RemovedHostObject &object : removed) {
        EntityId parent = scene_.parent(object.entity);
        while (is_removed(parent)) {
          parent = scene_.parent(parent);
        }
        const auto *hierarchy = scene_.hierarchy(object.entity);
        const std::vector<EntityId> children = hierarchy != nullptr
            ? hierarchy->children : std::vector<EntityId>{};
        for (const EntityId child : children) {
          if (!is_removed(child) &&
              !mesh_sync_.parent_projected_entity(scene_, child, parent, true)) {
            return abort_preview_publication(
                preview.id, "Merge source children could not be preserved.");
          }
        }
      }
      const auto depth = [this](EntityId entity) {
        std::size_t result = 0U;
        for (EntityId parent = scene_.parent(entity);
             parent != kInvalidEntityId; parent = scene_.parent(parent)) {
          ++result;
        }
        return result;
      };
      std::ranges::sort(removed, [&depth](const RemovedHostObject &left,
                                        const RemovedHostObject &right) {
        return depth(left.entity) > depth(right.entity);
      });
    }
    for (const RemovedHostObject &object : removed) {
      object_map_.remember_scene_entity(object.modeling_object_id, scene_,
                                        object.entity);
      if (!mesh_sync_.destroy_projected_entity(scene_, object.entity)) {
        return abort_preview_publication(
            preview.id,
            "Removed preview scene entity could not be destroyed.");
      }
      object_map_.unbind_object(object.modeling_object_id);
      object_map_.unbind_sdk_object(object.modeling_object_id);
    }

    Result<modeling::SessionPreviewCommitResult> committed =
        session_.commit_preview_transaction(preview.id);
    if (!committed.ok()) {
      // The SDK still owns the active preview on validation failure, so the
      // original retained baseline can end it and restore every host owner.
      return abort_preview_publication(preview.id,
                                       committed.error_message());
    }

    active_native_object_id_ =
        native_object(committed.value().active_object)
            .value_or(kInvalidModelingObjectId);
    if (editor_mode_service_ != nullptr && structural_preview) {
      std::vector<EntityId> consumed;
      for (const auto &object : removed) {
        consumed.push_back(object.entity);
      }
      const auto survivor = object_map_.entity_for_modeling_object(active_native_object_id_);
      if (survivor.has_value()) {
        editor_mode_service_->remap_participants(consumed, *survivor);
      }
      // This restores empty, unchanged participants after SDK structural commit.
      // Membership only shrinks/remaps; preview-created meshes never enroll here.
      if (!add_selection_to_scene()) {
        return failure("Committed edit membership could not be projected.");
      }
    }
    const bool changed =
        committed.value().operation.changed || preview_changed_;
    if (changed) {
      ++persistent_content_revision_;
    }

    const std::vector<AssetId> original_assets =
        mesh_preview_host_baseline_->scope.asset_ids;
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, original_assets);
    mesh_sync_.remove_unreferenced_history_curve_assets(
        scene_, assets_, original_assets);

    committed_transform_history_delta_ = transform_history_delta;
    committed_transform_undo_mark_ = transform_history_delta.valid()
        ? std::optional{mesh_preview_host_baseline_->transform_history_undo_mark}
        : std::nullopt;
    const ModelingCommandResult result{
        .ok = committed.value().operation.success,
        .changed = changed,
        .message = committed.value().operation.message,
    };
    clear_preview_checkpoints();
    if (result.ok && result.changed && accepted_coordinates) {
      accepted_overlay_proof_ = accepted_overlay_proof();
      accepted_overlay_coordinates_ = std::move(accepted_coordinates);
    }
    return result;
  }
  [[nodiscard]] ModelingCommandResult cancel_translate_preview() {
    QDR_PROFILE_FUNCTION();
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (curve_document_preview_active()) {
      return cancel_curve_preview_projection();
    }
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview.id)) {
      return failure("SDK preview host restore point is unavailable.");
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Preview participants changed outside this preview.");
    }
    const bool changed = preview_changed_;
    if (!restore_preview_to_original_baseline(preview.id, false)) {
      return {.ok = false,
              .changed = true,
              .message = "Preview could not be canceled."};
    }
    return {.ok = true,
            .changed = changed,
            .message = "Preview canceled."};
  }
  [[nodiscard]] ModelingSdkTransformHistoryDelta
  take_committed_transform_history_delta() {
    QDR_PROFILE_SCOPE("Transform history transfer sparse delta");
    ModelingSdkTransformHistoryDelta delta =
        std::move(committed_transform_history_delta_);
    committed_transform_history_delta_ = {};
    if (delta.valid() && committed_transform_undo_mark_.has_value()) {
      session_.discard_undo_since(*committed_transform_undo_mark_);
    }
    committed_transform_undo_mark_.reset();
    return delta;
  }

  [[nodiscard]] ModelingCommandResult apply_transform_history_delta(
      const ModelingSdkTransformHistoryDelta &delta, bool use_after) {
    QDR_PROFILE_FUNCTION();
    if (!delta.valid()) {
      return failure("Transform history delta is unavailable.");
    }
    const std::size_t undo_mark = session_.undo_depth();
    const std::shared_ptr<const ModelingSdkTransformHistoryDelta::Impl> data =
        delta.impl_;
    ModelingCommandResult applied;
    {
      QDR_PROFILE_SCOPE("Transform history apply sparse authored positions");
      applied = commit_sparse_transform_positions(data, use_after);
    }
    if (applied.ok) {
      session_.discard_undo_since(undo_mark);
    }
    return applied;
  }

  [[nodiscard]] bool preview_active() const noexcept {
    return session_.preview_active();
  }

  [[nodiscard]] ModelingSelectionMode selection_mode() const noexcept {
    return selection_mode_;
  }

  [[nodiscard]] ModelingSelectionSummary selection_summary() const {
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    const SelectionSummaryCacheKey cache_key{
        .persistent_content_revision = persistent_content_revision_,
        .session_content_revision = session_.content_revision(),
        .session_selection_revision = session_.selection_revision(),
        .mode_revision = mode_revision_ +
            (editor_mode_service_ != nullptr ? editor_mode_service_->revision() : 0u),
        .object_map_revision = object_map_.revision(),
        .active_native_object_id = active_native_object_id_,
        .mode = selection_mode_,
        .sdk_preview_active = preview.active,
        .preview_id = preview.id,
        .preview_object_id = preview.object_id,
        .preview_content_revision = preview.content_revision,
    };
    if (selection_summary_cache_key_.has_value() &&
        *selection_summary_cache_key_ == cache_key) {
      return selection_summary_cache_;
    }
    const auto cache_summary = [this, &cache_key](
                                   ModelingSelectionSummary summary) {
      selection_summary_cache_key_ = cache_key;
      selection_summary_cache_ = summary;
      return summary;
    };

    ++stats_.selection_summary_build_count;
    selected_component_entities_cache_.clear();
    ModelingSelectionSummary summary;
    summary.mode = selection_mode_;
    summary.content_revision = persistent_content_revision_;
    summary.selection_revision = session_.selection_revision();
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const auto &object : objects) {
      if (object.selected) {
        ++summary.selected_object_count;
      }
    }
    summary.active_object_id = active_native_object_id_;
    if (selection_mode_ == ModelingSelectionMode::Object) {
      summary.has_selection = summary.selected_object_count != 0;
      summary.active_kind = summary.has_selection
                                ? ModelingActiveSelectionKind::Object
                                : ModelingActiveSelectionKind::None;
      return cache_summary(std::move(summary));
    }

    const std::optional<ObjectId> active = active_sdk_object();
    if (!active.has_value()) {
      return cache_summary(std::move(summary));
    }
    std::vector<ObjectId> committed_object_ids;
    for (const auto &object : objects) {
      if (object.kind == modeling::SessionObjectKind::Mesh &&
          (object.selected || object.id == *active)) {
        committed_object_ids.push_back(object.id);
      }
    }
    const auto committed_documents = session_.document_copies(committed_object_ids);
    std::size_t committed_index = 0u;
    PolygonSelectionSnapshot active_selection;
    bool active_selection_available = false;
    for (const auto &object : objects) {
      if (object.kind != modeling::SessionObjectKind::Mesh ||
          (!object.selected && object.id != *active)) {
        continue;
      }
      const PolygonDocument *committed = committed_documents.ok()
          ? &committed_documents.value()[committed_index] : nullptr;
      ++committed_index;
      Result<PolygonDocument> document =
          preview.active && preview.object_id == object.id
              ? session_.preview_document_copy(preview.id)
              : query_polygon_document(object.id, committed);
      if (!document.ok()) {
        continue;
      }
      PolygonSelectionSnapshot selection = document.value().selection();
      const bool preview_selection_empty =
          selection_mode_ == ModelingSelectionMode::Vertex
              ? selection.vertices.empty()
          : selection_mode_ == ModelingSelectionMode::Edge
              ? selection.edges.empty()
              : selection.faces.empty();
      if (preview.active && preview.object_id == object.id &&
          preview_selection_empty) {
        const auto baseline = committed != nullptr
            ? Result<PolygonDocument>::success(*committed)
            : session_.document_copy(object.id);
        if (baseline.ok()) {
          const PolygonSelectionSnapshot committed_selection =
              baseline.value().selection();
          if (!committed_selection.vertices.empty() ||
              !committed_selection.edges.empty() ||
              !committed_selection.faces.empty()) {
            selection = committed_selection;
          }
        }
      }
      summary.selected_vertex_count +=
          static_cast<std::uint32_t>(selection.vertices.size());
      summary.selected_edge_count +=
          static_cast<std::uint32_t>(selection.edges.size());
      summary.selected_face_count +=
          static_cast<std::uint32_t>(selection.faces.size());
      if (object.selected &&
          component_selection_participates_in_mode(selection, selection_mode_)) {
        const std::optional<int> native = native_object(object.id);
        const std::optional<EntityId> entity =
            native.has_value()
                ? object_map_.entity_for_modeling_object(*native)
                : std::nullopt;
        if (entity.has_value() && scene_.exists(*entity)) {
          selected_component_entities_cache_.push_back(*entity);
        }
      }
      if (object.id == *active) {
        active_selection = selection;
        active_selection_available = true;
      }
    }
    summary.has_selection = summary.selected_vertex_count != 0 ||
                            summary.selected_edge_count != 0 ||
                            summary.selected_face_count != 0;
    if (active_selection_available && active_selection.has_active) {
      if (selection_mode_ == ModelingSelectionMode::Vertex &&
          active_selection.active_vertex.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Vertex;
        summary.active_vertex_id =
            static_cast<int>(active_selection.active_vertex.index);
      } else if (selection_mode_ == ModelingSelectionMode::Edge &&
                 active_selection.active_edge.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Edge;
        summary.active_edge_a =
            static_cast<int>(active_selection.active_edge.a.index);
        summary.active_edge_b =
            static_cast<int>(active_selection.active_edge.b.index);
      } else if (selection_mode_ == ModelingSelectionMode::Face &&
                 active_selection.active_face.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Face;
        summary.active_face_id =
            static_cast<int>(active_selection.active_face.index);
      }
    }
    return cache_summary(std::move(summary));
  }

  [[nodiscard]] bool selected_faces_have_open_perimeter() const {
    return selection_topology_eligibility().open_face_perimeter;
  }

  [[nodiscard]] PolygonSelectionTopologyEligibility
  selection_topology_eligibility() const {
    const ModelingSelectionSummary summary = selection_summary();
    if (!summary.has_selection) {
      return {};
    }
    if (selection_topology_cache_key_.has_value() &&
        *selection_topology_cache_key_ == *selection_summary_cache_key_) {
      return selection_topology_cache_;
    }

    ++stats_.selection_topology_build_count;

    PolygonSelectionTopologyEligibility result;
    result.whole_mesh_closed = true;
    result.whole_mesh_has_open_perimeter = true;
    result.open_face_perimeter = true;
    result.combinable_selected_faces = true;
    result.bridge_face_pair = true;
    result.bridge_edge_pair = true;
    result.selected_edges_belong_to_faces = true;
    result.selected_edges_are_interior = true;
    result.selected_edges_are_open_boundary = true;
    result.selected_vertices_belong_to_faces = true;
    bool found = false;
    std::uint32_t selected_face_document_count = 0;
    std::uint32_t selected_face_count = 0;
    std::uint32_t first_single_face_edge_count = 0;
    bool cross_document_face_pair = true;
    std::uint32_t selected_edge_document_count = 0U;
    std::size_t first_selected_edge_count = 0U;
    bool cross_document_edge_pair = true;
    const auto preview = session_.preview_summary();
    const std::optional<ObjectId> active = active_sdk_object();
    for (const auto &object : session_.objects()) {
      if (!object.selected && (!active.has_value() || object.id != *active)) {
        continue;
      }
      Result<PolygonDocument> document =
          preview.active && preview.object_id == object.id
              ? session_.preview_document_copy(preview.id)
              : query_polygon_document(object.id);
      if (!document.ok()) {
        continue;
      }
      if (summary.mode == ModelingSelectionMode::Object) {
        if (!object.selected) {
          continue;
        }
        found = true;
        const auto local =
            document.value().whole_mesh_topology_eligibility();
        result.whole_mesh_closed &= local.whole_mesh_closed;
        result.whole_mesh_has_open_perimeter &=
            local.whole_mesh_has_open_perimeter;
        continue;
      }
      const PolygonSelectionSnapshot selection = document.value().selection();
      const bool selected = summary.mode == ModelingSelectionMode::Vertex
                                ? !selection.vertices.empty()
                            : summary.mode == ModelingSelectionMode::Edge
                                ? !selection.edges.empty()
                                : !selection.faces.empty();
      if (!selected) {
        continue;
      }
      found = true;
      const auto local = document.value().selection_topology_eligibility();
      result.open_face_perimeter &= local.open_face_perimeter;
      result.combinable_selected_faces &= local.combinable_selected_faces;
      if (summary.mode == ModelingSelectionMode::Face) {
        ++selected_face_document_count;
        selected_face_count += static_cast<std::uint32_t>(selection.faces.size());
        if (selection.faces.size() == 1U) {
          const auto edge_count = local.single_selected_face_edge_count;
          if (first_single_face_edge_count == 0U) {
            first_single_face_edge_count = edge_count;
          }
          cross_document_face_pair &= edge_count >= 3U &&
                                      edge_count == first_single_face_edge_count;
        } else {
          result.bridge_face_pair &= local.bridge_face_pair;
          cross_document_face_pair = false;
        }
      }
      result.bridge_edge_pair &= local.bridge_edge_pair;
      if (summary.mode == ModelingSelectionMode::Edge) {
        ++selected_edge_document_count;
        if (first_selected_edge_count == 0U) {
          first_selected_edge_count = selection.edges.size();
        }
        cross_document_edge_pair &= local.selected_edges_are_open_boundary &&
                                    selection.edges.size() ==
                                        first_selected_edge_count;
      }
      result.selected_edges_belong_to_faces &=
          local.selected_edges_belong_to_faces;
      result.selected_edges_are_interior &= local.selected_edges_are_interior;
      result.selected_edges_are_open_boundary &=
          local.selected_edges_are_open_boundary;
      result.three_selected_open_boundary_edges |=
          local.three_selected_open_boundary_edges;
      result.selected_vertices_belong_to_faces &=
          local.selected_vertices_belong_to_faces;
      result.selected_vertices_share_face |= local.selected_vertices_share_face;
    }
    if (!found) {
      result = {};
    } else if (summary.mode == ModelingSelectionMode::Face) {
      result.bridge_face_pair &=
          selected_face_document_count == 1U ||
          (selected_face_document_count == 2U && selected_face_count == 2U &&
           cross_document_face_pair);
    } else if (summary.mode == ModelingSelectionMode::Edge) {
      result.bridge_edge_pair = selected_edge_document_count == 1U
          ? result.bridge_edge_pair
          : selected_edge_document_count == 2U && cross_document_edge_pair;
    }
    selection_topology_cache_key_ = selection_summary_cache_key_;
    selection_topology_cache_ = result;
    return result;
  }

  [[nodiscard]] std::span<const EntityId>
  selected_component_entities() const {
    QDR_PROFILE_FUNCTION();
    (void)selection_summary();
    return selected_component_entities_cache_;
  }

  [[nodiscard]] ModelingSelectionExport export_selection() const {
    ModelingSelectionExport exported;
    exported.summary = selection_summary();
    const auto summaries = session_.objects();
    std::unordered_map<
        std::uint64_t, const quader::modeling::SessionObjectSummary *>
        summaries_by_id;
    summaries_by_id.reserve(summaries.size());
    for (const auto &summary : summaries) {
      summaries_by_id.emplace(sdk_object_key(summary.id), &summary);
    }
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      const auto summary = summaries_by_id.find(sdk_object_key(object_id));
      if (summary == summaries_by_id.end() ||
          (!summary->second->selected &&
           native_id != active_native_object_id_)) {
        continue;
      }
      ModelingSelectionExportObject object;
      object.modeling_object_id = native_id;
      object.entity = object_map_.entity_for_modeling_object(native_id)
                          .value_or(kInvalidEntityId);
      object.selected = summary->second->selected;
      if (object.entity != kInvalidEntityId && scene_.exists(object.entity)) {
        object.entity_uuid = scene_.uuid(object.entity);
        if (const NameComponent *name = scene_.name(object.entity)) {
          object.name = name->name;
        }
      }
      if (summary->second->kind == modeling::SessionObjectKind::Curve) {
        exported.objects.push_back(std::move(object));
        continue;
      }
      Result<PolygonDocument> document = query_polygon_document(object_id);
      Result<AuthoredTopology> topology =
          document.ok() ? document.value().authored_topology()
                        : Result<AuthoredTopology>::failure(
                              quader::modeling::make_error(
                                  quader::modeling::ErrorCode::InvalidId,
                                  "SDK document is unavailable."));
      if (!document.ok() || !topology.ok()) {
        continue;
      }
      const PolygonSelectionSnapshot selection = document.value().selection();
      for (const VertexId id : selection.vertices) {
        const auto vertex = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [id](const auto &candidate) { return candidate.id == id; });
        if (vertex != topology.value().vertices.end()) {
          object.selection.vertices.push_back(id.index);
          object.selected_vertices.push_back({
              .id = id.index,
              .position = {vertex->position.x, vertex->position.y,
                           vertex->position.z},
          });
        }
      }
      for (const EdgeKey edge : selection.edges) {
        object.selection.edges.push_back(
            quader_poly::make_edge(edge.a.index, edge.b.index));
        ModelingSelectionExportEdge exported_edge{
            .a = edge.a.index,
            .b = edge.b.index,
        };
        const auto a = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [edge](const auto &candidate) { return candidate.id == edge.a; });
        const auto b = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [edge](const auto &candidate) { return candidate.id == edge.b; });
        if (a != topology.value().vertices.end()) {
          exported_edge.a_position = {
              a->position.x, a->position.y, a->position.z};
        }
        if (b != topology.value().vertices.end()) {
          exported_edge.b_position = {
              b->position.x, b->position.y, b->position.z};
        }
        object.selected_edges.push_back(std::move(exported_edge));
      }
      for (const FaceId id : selection.faces) {
        const auto face = std::find_if(
            topology.value().faces.begin(), topology.value().faces.end(),
            [id](const auto &candidate) { return candidate.id == id; });
        if (face == topology.value().faces.end()) {
          continue;
        }
        object.selection.faces.push_back(id.index);
        ModelingSelectionExportFace exported_face;
        exported_face.id = id.index;
        for (const VertexId vertex : face->vertices) {
          exported_face.vertices.push_back(vertex.index);
        }
        object.selected_faces.push_back(std::move(exported_face));
      }
      exported.objects.push_back(std::move(object));
    }
    return exported;
  }

  [[nodiscard]] std::optional<SceneBounds> selection_bounds() const {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      const quader::modeling::PreviewTransactionSummary preview =
          session_.preview_summary();
      std::optional<SceneBounds> component_bounds;
      const std::optional<ObjectId> active = active_sdk_object();
      for (const quader::modeling::SessionObjectSummary &object :
           session_.objects()) {
        if (!object.selected &&
            (!active.has_value() || object.id != *active)) {
          continue;
        }
        const std::optional<int> native = native_object(object.id);
        const std::optional<EntityId> entity =
            native.has_value()
                ? object_map_.entity_for_modeling_object(*native)
                : std::nullopt;
        if (!entity.has_value() || !scene_.exists(*entity)) {
          continue;
        }
        const auto *cut_candidate = native.has_value()
            ? plane_cut_candidate(*native) : nullptr;
        Result<PolygonDocument> document =
            cut_candidate != nullptr
                ? Result<PolygonDocument>::success(cut_candidate->projected)
                : preview.active && preview.object_id == object.id
                ? session_.preview_document_copy(preview.id)
                : query_polygon_document(object.id);
        if (!document.ok()) {
          continue;
        }
        PolygonSelectionSnapshot selection = document.value().selection();
        const bool preview_selection_empty =
            selection_mode_ == ModelingSelectionMode::Vertex
                ? selection.vertices.empty()
            : selection_mode_ == ModelingSelectionMode::Edge
                ? selection.edges.empty()
                : selection.faces.empty();
        if (preview.active && preview.object_id == object.id &&
            preview_selection_empty) {
          Result<PolygonDocument> committed = session_.document_copy(object.id);
          if (committed.ok()) {
            selection = committed.value().selection();
            document = std::move(committed);
          }
        }
        Result<AuthoredTopology> topology = document.value().authored_topology();
        if (!topology.ok()) {
          continue;
        }
        const auto id_key = [](const auto id) {
          return (static_cast<std::uint64_t>(id.generation) << 32U) |
                 static_cast<std::uint64_t>(id.index);
        };
        const AuthoredTopology &authored_topology = topology.value();
        std::unordered_map<
            std::uint64_t,
            const quader::modeling::AuthoredTopologyVertex *>
            vertices_by_id;
        vertices_by_id.reserve(authored_topology.vertices.size());
        for (const quader::modeling::AuthoredTopologyVertex &vertex :
             authored_topology.vertices) {
          // Preserve the old find_if first-record behavior for malformed
          // duplicate IDs while making component projection linear.
          vertices_by_id.emplace(id_key(vertex.id), &vertex);
        }
        std::unordered_map<std::uint64_t,
                           const quader::modeling::AuthoredTopologyFace *>
            faces_by_id;
        if (!selection.faces.empty()) {
          faces_by_id.reserve(authored_topology.faces.size());
          for (const quader::modeling::AuthoredTopologyFace &face :
               authored_topology.faces) {
            faces_by_id.emplace(id_key(face.id), &face);
          }
        }
        const QMat4 world_from_object = cut_candidate != nullptr
            ? cut_candidate->world_from_object
            : scene_.world_transform_matrix(*entity);
        const auto include_vertex = [&](VertexId id) {
          const auto vertex = vertices_by_id.find(id_key(id));
          if (vertex == vertices_by_id.end() || vertex->second == nullptr) {
            return;
          }
          const QVec3 point = transform_point(
              world_from_object,
              {vertex->second->position.x, vertex->second->position.y,
               vertex->second->position.z});
          if (!component_bounds.has_value()) {
            component_bounds = {.min = point, .max = point};
          } else {
            include_point(*component_bounds, point);
          }
        };
        for (const VertexId id : selection.vertices) {
          include_vertex(id);
        }
        for (const EdgeKey edge : selection.edges) {
          include_vertex(edge.a);
          include_vertex(edge.b);
        }
        for (const FaceId id : selection.faces) {
          const auto face = faces_by_id.find(id_key(id));
          if (face != faces_by_id.end() && face->second != nullptr) {
            for (const VertexId vertex : face->second->vertices) {
              include_vertex(vertex);
            }
          }
        }
      }
      return component_bounds;
    }

    std::optional<SceneBounds> bounds;
    for (const EntityId entity : selected_entities()) {
      const auto native = object_map_.modeling_object_for_entity(entity);
      const auto *cut_candidate = native.has_value()
          ? plane_cut_candidate(*native) : nullptr;
      const std::optional<SceneBounds> current = cut_candidate != nullptr
          ? mesh_world_bounds(cut_candidate->projected_mesh,
                              cut_candidate->world_from_object)
          : entity_world_bounds(scene_, assets_, entity);
      if (!current.has_value()) {
        continue;
      }
      if (!bounds.has_value()) {
        bounds = current;
      } else {
        include_point(*bounds, current->min);
        include_point(*bounds, current->max);
      }
    }
    return bounds;
  }

  [[nodiscard]] std::vector<EntityId> selected_entities() const {
    std::vector<EntityId> entities;
    for (const auto &object : session_.objects()) {
      if (!object.selected) {
        continue;
      }
      const std::optional<int> native = native_object(object.id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (entity.has_value() && scene_.exists(*entity)) {
        entities.push_back(*entity);
      }
    }
    return entities;
  }

  [[nodiscard]] EntityId active_selected_entity() const {
    if (active_native_object_id_ == kInvalidModelingObjectId) {
      return kInvalidEntityId;
    }
    return object_map_.entity_for_modeling_object(active_native_object_id_)
        .value_or(kInvalidEntityId);
  }

  /** Restores portable selection flags from the scene-owned edit membership. */
  [[nodiscard]] bool synchronize_edit_membership(bool reconcile = true) {
    if (editor_mode_service_ == nullptr) {
      return true;
    }
    const bool was_component_mode = selection_mode_ != ModelingSelectionMode::Object &&
                                    selection_mode_ != ModelingSelectionMode::Group;
    if (reconcile) {
      editor_mode_service_->reconcile_scene_lifecycle();
    }
    const auto &context = editor_mode_service_->snapshot();
    if (context.mode != EditorMode::Edit && !was_component_mode) {
      return true;
    }
    std::vector<ObjectId> objects;
    int active_native = kInvalidModelingObjectId;
    const auto append = [&](EntityId entity) {
      const auto native = object_map_.modeling_object_for_entity(entity);
      const auto object = native.has_value() ? sdk_object(*native) : std::nullopt;
      if (!object.has_value()) {
        return;
      }
      objects.push_back(*object);
      if (entity == context.active_entity) {
        active_native = *native;
        std::rotate(objects.begin(), std::prev(objects.end()), objects.end());
      }
    };
    if (context.mode == EditorMode::Edit) {
      for (const auto &participant : context.participants) {
        append(participant.entity);
      }
    } else {
      for (const auto entity : context.object_results) {
        append(entity);
      }
    }
    std::vector<modeling::SessionComponentSelectionUpdate> updates;
    if (context.mode == EditorMode::Edit && selection_mode_ != context.component_kind) {
      for (const ObjectId object : objects) {
        auto document = session_.document_copy(object);
        if (!document.ok()) {
          return false;
        }
        const auto converted = document.value().set_selection_mode(
            to_sdk_component_mode(context.component_kind));
        if (!converted.ok()) {
          return false;
        }
        updates.push_back({.object = object, .selection = document.value().selection()});
      }
    }
    const auto applied = updates.empty()
        ? session_.apply_object_selection(objects, SelectionEdit::Replace)
        : session_.commit_component_selection_batch(
              updates, objects, objects.empty() ? ObjectId{} : objects.front());
    if (!applied.ok()) {
      return false;
    }
    if (active_native == kInvalidModelingObjectId && !objects.empty()) {
      active_native = native_object(objects.front()).value_or(kInvalidModelingObjectId);
    }
    const auto mode = context.mode == EditorMode::Edit
                          ? context.component_kind : ModelingSelectionMode::Object;
    if (selection_mode_ != mode || active_native_object_id_ != active_native) {
      ++mode_revision_;
      selection_mode_ = mode;
      active_native_object_id_ = active_native;
    }
    return true;
  }

  [[nodiscard]] bool add_selection_to_scene() {
    if (!synchronize_edit_membership()) {
      return false;
    }
    std::vector<EntityId> combined;
    for (const EntityId entity : scene_.selected_entities()) {
      if (!owns_entity(entity)) {
        combined.push_back(entity);
      }
    }
    const std::size_t preserved_count = combined.size();
    std::unordered_set<EntityId> membership(combined.begin(),
                                            combined.end());
    for (const EntityId entity : selected_entities()) {
      if (scene_.exists(entity) && membership.insert(entity).second) {
        combined.push_back(entity);
      }
    }
    EntityId active = active_selected_entity();
    if (active == kInvalidEntityId || !scene_.exists(active) ||
        !membership.contains(active)) {
      active = combined.size() > preserved_count
                   ? combined.back()
                   : (combined.empty() ? kInvalidEntityId
                                       : combined.front());
    }
    const SceneSelectionState desired =
        make_scene_selection(scene_, combined, active);
    (void)apply_scene_selection(scene_, desired,
                                SceneSelectionApplicationMode::RuntimeSync);
    return same_scene_selection(capture_scene_selection(scene_), desired);
  }

  [[nodiscard]] bool acknowledge_active_preview_host_publication() {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    return !preview.active || acknowledge_preview_owner_fence();
  }

  [[nodiscard]] std::uint64_t content_revision() const {
    return persistent_content_revision_;
  }

  [[nodiscard]] std::uint64_t selection_revision() const {
    return session_.selection_revision() + mode_revision_ +
           (editor_mode_service_ != nullptr ? editor_mode_service_->revision() : 0u);
  }

  void release_redundant_sdk_history() {
    if (session_.preview_active() || session_.mutation_checkpoint_active() ||
        ModelingHostPublicationTransaction::is_session_publishing(session_) ||
        mesh_preview_host_baseline_.has_value() ||
        structural_preview_host_state_.has_value() ||
        committed_transform_history_delta_.valid() ||
        committed_transform_undo_mark_.has_value()) {
      return;
    }
    // Native commands own history-free workspace mementos or sparse deltas.
    // SDK undo entries exist only to compensate an in-flight publication;
    // retaining them after native publication pins otherwise expired meshes.
    session_.clear_history();
  }

  // Internal preview/rollback snapshots can retain SDK-local history. The
  // public host memento path passes false so each history item owns only one
  // semantic workspace checkpoint.
  [[nodiscard]] ModelingSdkProjectionSnapshot
  capture_snapshot(bool include_sdk_history) const {
    auto snapshot = std::make_shared<ModelingSdkProjectionSnapshot::Impl>();
    if (include_sdk_history) {
      snapshot->session = session_;
    } else {
      snapshot->session = session_.current_workspace_checkpoint();
    }
    snapshot->object_ids = sdk_bindings();
    snapshot->next_native_object_id = next_native_object_id_;
    snapshot->active_native_object_id = active_native_object_id_;
    snapshot->selection_mode = selection_mode_;
    snapshot->persistent_content_revision = persistent_content_revision_;
    snapshot->mode_revision = mode_revision_;
    if (editor_mode_service_ != nullptr) {
      snapshot->editor_mode = editor_mode_service_->capture_history_state();
    }
    snapshot->projected_objects.reserve(sdk_bindings().size());
    std::unordered_set<int> projected_ids;
    projected_ids.reserve(sdk_bindings().size());
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      (void)object_id;
      projected_ids.insert(native_id);
    }
    const auto live_identities =
        object_map_.capture_scene_identities_for_modeling_objects(
            scene_, projected_ids);
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      (void)object_id;
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (!entity.has_value() || !scene_.exists(*entity)) {
        continue;
      }
      const ModelingObjectComponent *component =
          scene_.modeling_object(*entity);
      const MeshComponent *mesh_component = scene_.mesh(*entity);
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      const auto identity = live_identities.find(native_id);
      if (component != nullptr && identity != live_identities.end()) {
        const CurveComponent *binding = scene_.curve(*entity);
        const CurveAsset *curve = binding != nullptr
                                      ? assets_.curve(binding->curve) : nullptr;
        if (curve != nullptr && curve->document != nullptr) {
          snapshot->projected_objects.push_back({
              .modeling_object_id = native_id,
              .identity = identity->second,
              .curve_id = binding->curve,
              .curve = *curve,
          });
          continue;
        }
      }
      if (component == nullptr || mesh_component == nullptr || mesh == nullptr ||
          identity == live_identities.end()) {
        continue;
      }
      snapshot->projected_objects.push_back({
          .modeling_object_id = native_id,
          .identity = identity->second,
          .material_slot = component->material_slot,
          .mesh_id = mesh_component->mesh,
          .mesh = clone_mesh_asset_payload(*mesh),
      });
    }
    return ModelingSdkProjectionSnapshot(std::move(snapshot));
  }

  [[nodiscard]] static std::shared_ptr<ModelingSdkProjectionSnapshot::Impl>
  clone_host_checkpoint_impl(
      const ModelingSdkProjectionSnapshot &snapshot) {
    if (!snapshot.valid()) {
      return {};
    }
    auto checkpoint =
        std::make_shared<ModelingSdkProjectionSnapshot::Impl>();
    checkpoint->session =
        snapshot.impl_->session.current_workspace_checkpoint();
    checkpoint->object_ids = snapshot.impl_->object_ids;
    checkpoint->projected_objects = snapshot.impl_->projected_objects;
    checkpoint->component_merge_history = snapshot.impl_->component_merge_history;
    checkpoint->merge_child_placements = snapshot.impl_->merge_child_placements;
    checkpoint->next_native_object_id =
        snapshot.impl_->next_native_object_id;
    checkpoint->active_native_object_id =
        snapshot.impl_->active_native_object_id;
    checkpoint->selection_mode = snapshot.impl_->selection_mode;
    checkpoint->persistent_content_revision =
        snapshot.impl_->persistent_content_revision;
    checkpoint->mode_revision = snapshot.impl_->mode_revision;
    checkpoint->editor_mode = snapshot.impl_->editor_mode;
    return checkpoint;
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot capture_component_merge_snapshot(
      const ModelingSdkProjectionSnapshot &before) const {
    std::vector<EntityId> children;
    if (before.valid()) {
      if (!before.impl_->component_merge_history) {
        return {};
      }
      for (const auto &placement : before.impl_->merge_child_placements) {
        children.push_back(placement.entity);
      }
    } else {
      const bool merge_preview = structural_preview_host_state_.has_value() &&
          structural_preview_host_state_->merge_sources.has_value();
      if (preview_active() && !merge_preview) {
        return {};
      }
      const auto objects = merge_preview
          ? structural_preview_host_state_->source_objects
          : selected_component_objects();
      if (objects.size() < 2U) {
        return {};
      }
      auto survivor = merge_preview
          ? std::optional{structural_preview_host_state_->merge_sources->survivor}
          : active_sdk_object();
      if (!survivor.has_value() ||
          std::ranges::find(objects, *survivor) == objects.end()) {
        survivor = objects.front();
      }
      for (const ObjectId object : objects) {
        if (survivor == object) {
          continue;
        }
        const auto native = native_object(object);
        const auto entity = native.has_value()
            ? object_map_.entity_for_modeling_object(*native) : std::nullopt;
        if (!entity.has_value()) {
          return {};
        }
        for (const EntityId child : scene_.children(*entity)) {
          if (!owns_entity(child)) {
            children.push_back(child);
          }
        }
      }
    }
    const auto snapshot = !before.valid() && preview_active()
        ? capture_active_preview_base_snapshot() : capture_snapshot(false);
    auto captured = clone_host_checkpoint_impl(snapshot);
    if (!captured) {
      return {};
    }
    captured->component_merge_history = true;
    captured->merge_child_placements.clear();
    for (const EntityId child : children) {
      const auto *transform = scene_.transform(child);
      if (!scene_.exists(child) || transform == nullptr) {
        return {};
      }
      captured->merge_child_placements.push_back({
          .entity = child,
          .parent = scene_.parent(child),
          .transform = *transform,
          .sibling_ordinal = scene_.sibling_ordinal(child),
      });
    }
    return ModelingSdkProjectionSnapshot(std::move(captured));
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot rebase_snapshot_host_projection(
      const ModelingSdkProjectionSnapshot &snapshot) const {
    auto rebased = clone_host_checkpoint_impl(snapshot);
    if (!rebased) {
      return {};
    }

    std::unordered_set<int> projected_ids;
    projected_ids.reserve(rebased->projected_objects.size());
    for (const SdkProjectedObjectSnapshot &projected :
         rebased->projected_objects) {
      projected_ids.insert(projected.modeling_object_id);
    }
    const auto current_identities =
        object_map_.capture_scene_identities_for_modeling_objects(
            scene_, projected_ids);
    for (SdkProjectedObjectSnapshot &projected :
         rebased->projected_objects) {
      const auto identity =
          current_identities.find(projected.modeling_object_id);
      if (identity == current_identities.end() ||
          !scene_.exists(identity->second.id)) {
        continue;
      }
      const ModelingObjectComponent *component =
          scene_.modeling_object(identity->second.id);
      const MeshComponent *mesh_component =
          scene_.mesh(identity->second.id);
      if (component != nullptr && projected.curve.has_value()) {
        const CurveComponent *binding = scene_.curve(identity->second.id);
        const CurveAsset *curve = binding != nullptr
                                      ? assets_.curve(binding->curve) : nullptr;
        if (curve != nullptr && curve->document != nullptr) {
          projected.identity = identity->second;
          projected.curve_id = binding->curve;
          projected.curve = *curve;
        }
        continue;
      }
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      if (component == nullptr || mesh_component == nullptr || mesh == nullptr) {
        continue;
      }
      projected.identity = identity->second;
      projected.material_slot = component->material_slot;
      projected.mesh_id = mesh_component->mesh;
      projected.mesh = clone_mesh_asset_payload(*mesh);
    }
    return ModelingSdkProjectionSnapshot(std::move(rebased));
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot
  capture_active_preview_base_snapshot() const {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preview.active || !mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->valid() ||
        mesh_preview_host_baseline_->preview_id != preview.id) {
      return {};
    }
    ModelingSdkProjectionSnapshot current = capture_snapshot(false);
    auto base = clone_host_checkpoint_impl(current);
    if (!base) {
      return {};
    }
    base->editor_mode = mesh_preview_host_baseline_->adapter_before.editor_mode;
    for (const modeling::PreviewTransactionObjectSummary &participant :
         preview.objects) {
      if (participant.action ==
          modeling::SessionPreviewAction::AddProvisional) {
        continue;
      }
      const auto history_asset = std::ranges::lower_bound(
          mesh_preview_host_baseline_->history_asset_identities,
          participant.object_id, {}, &PreviewHistoryAssetIdentity::object);
      if (history_asset ==
              mesh_preview_host_baseline_->history_asset_identities.end() ||
          history_asset->object != participant.object_id ||
          history_asset->kind != participant.kind ||
          history_asset->asset == kInvalidAssetId) {
        return {};
      }
      const std::optional<int> native = native_object(participant.object_id);
      if (!native.has_value()) {
        return {};
      }
      SdkProjectedObjectSnapshot projected{
          .modeling_object_id = *native,
          .identity = history_asset->identity,
          .material_slot =
              identity_material_slot(history_asset->identity),
      };
      if (participant.kind == modeling::SessionObjectKind::Curve) {
        Result<modeling::CurveDocument> document =
            session_.preview_base_curve_document_copy(preview.id,
                                                      participant.object_id);
        if (!document.ok()) {
          return {};
        }
        projected.curve_id = history_asset->asset;
        projected.identity.mesh.reset();
        CurveComponent curve_component =
            projected.identity.curve.value_or(CurveComponent{});
        curve_component.curve = history_asset->asset;
        projected.identity.curve = curve_component;
        CurveAsset curve;
        curve.name = history_asset->identity.name;
        curve.document = std::make_shared<const modeling::CurveDocument>(
            std::move(document).value());
        projected.curve = std::move(curve);
      } else {
        Result<PolygonDocument> document =
            session_.preview_base_document_copy(preview.id,
                                                participant.object_id);
        if (!document.ok()) {
          return {};
        }
        std::optional<MeshAsset> mesh =
            MeshProjectionService::compile_sdk_document_mesh(document.value());
        if (!mesh.has_value()) {
          return {};
        }
        mesh->name = history_asset->identity.name;
        projected.mesh_id = history_asset->asset;
        projected.identity.curve.reset();
        MeshComponent mesh_component =
            projected.identity.mesh.value_or(MeshComponent{});
        mesh_component.mesh = history_asset->asset;
        projected.identity.mesh = mesh_component;
        projected.mesh = std::move(*mesh);
      }
      const auto existing = std::ranges::find(
          base->projected_objects, *native,
          &SdkProjectedObjectSnapshot::modeling_object_id);
      if (existing == base->projected_objects.end()) {
        base->projected_objects.push_back(std::move(projected));
      } else {
        *existing = std::move(projected);
      }
    }
    if (structural_preview_host_state_.has_value() &&
        structural_preview_host_state_->active_for(preview.id)) {
      base->next_native_object_id =
          structural_preview_host_state_->next_native_object_id_before;
      for (const StructuralPreviewHostState::AddedObject &added :
           structural_preview_host_state_->added_objects) {
        base->object_ids.erase(added.modeling_object_id);
        std::erase_if(base->projected_objects,
                      [&added](const SdkProjectedObjectSnapshot &projected) {
                        return projected.modeling_object_id ==
                               added.modeling_object_id;
                      });
      }
    }
    return ModelingSdkProjectionSnapshot(std::move(base));
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot(const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, true, false, true);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_deferred_scene_hierarchy(
      const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, true, false, false);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact_deferred_scene_hierarchy(
      const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, false, false, false);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_preserving_scene_selection(
      const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, true, true, true);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact(const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, false, false, true);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact_preserving_scene_selection(
      const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, false, true, true);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot(const ModelingSdkProjectionSnapshot &snapshot,
                   bool history_transition, bool preserve_scene_selection,
                   bool restore_scene_hierarchy) {
    if (!snapshot.valid()) {
      return failure("SDK projection restore point is invalid.");
    }

    const auto mode_before = editor_mode_service_ != nullptr
        ? std::optional{editor_mode_service_->snapshot()} : std::nullopt;
    const SceneSelectionState scene_selection_before = capture_scene_selection(scene_);
    const int host_identity_high_water = next_available_native_object_id();
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    std::vector<int> affected_modeling_ids;
    affected_entities.reserve(object_map_.size() +
                              snapshot.impl_->projected_objects.size());
    affected_modeling_ids.reserve(object_map_.size() +
                                  snapshot.impl_->object_ids.size());
    for (const auto &[modeling_object_id, entity] :
         object_map_.object_entity_bindings_by_modeling_object()) {
      affected_modeling_ids.push_back(modeling_object_id);
      if (scene_.exists(entity)) {
        std::vector<EntityId> subtree = scene_.descendants(entity, true);
        affected_entities.insert(affected_entities.end(), subtree.begin(),
                                 subtree.end());
        if (const MeshComponent *mesh = scene_.mesh(entity)) {
          affected_assets.push_back(mesh->mesh);
        }
        if (const CurveComponent *curve = scene_.curve(entity)) {
          affected_assets.push_back(curve->curve);
        }
      } else {
        affected_entities.push_back(entity);
      }
    }
    for (const auto &[modeling_object_id, object] : snapshot.impl_->object_ids) {
      (void)object;
      affected_modeling_ids.push_back(modeling_object_id);
    }
    for (const SdkProjectedObjectSnapshot &projected :
         snapshot.impl_->projected_objects) {
      if (scene_.exists(projected.identity.id)) {
        std::vector<EntityId> subtree =
            scene_.descendants(projected.identity.id, true);
        affected_entities.insert(affected_entities.end(), subtree.begin(),
                                 subtree.end());
      } else {
        affected_entities.push_back(projected.identity.id);
      }
      affected_assets.push_back(projected.curve.has_value()
                                    ? projected.curve_id
                                    : projected.mesh_id);
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    for (const auto &placement : snapshot.impl_->merge_child_placements) {
      if (!scene_.exists(placement.entity)) {
        return failure("Merge history descendant is no longer live.");
      }
      const auto descendants = scene_.descendants(placement.entity, true);
      affected_entities.insert(affected_entities.end(), descendants.begin(), descendants.end());
    }
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::Snapshot,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_bindings = !affected_modeling_ids.empty(),
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("History restore checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    std::unordered_set<int> restored_ids;
    restored_ids.reserve(snapshot.impl_->object_ids.size());
    for (const auto &[native_id, object_id] : snapshot.impl_->object_ids) {
      (void)object_id;
      restored_ids.insert(native_id);
    }
    const std::unordered_map<int, ModelingObjectSceneIdentity>
        live_scene_identities =
            history_transition
                ? std::unordered_map<int, ModelingObjectSceneIdentity>{}
                : object_map_.capture_scene_identities_for_modeling_objects(
                      scene_, restored_ids);

    std::vector<AssetId> displaced_meshes;
    std::vector<AssetId> displaced_curves;
    for (const auto &placement : snapshot.impl_->merge_child_placements) {
      // Protect surviving generic content before redo removes its former
      // modeling parent. Exact target placement follows model recreation.
      if (!mesh_sync_.parent_projected_entity(
              scene_, placement.entity, kInvalidEntityId, true)) {
        return rollback("Merge history descendant could not be preserved.");
      }
    }
    for (const auto &[native_id, object_id] : sdk_bindings()) {
      (void)object_id;
      if (restored_ids.contains(native_id)) {
        continue;
      }
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (entity.has_value() && scene_.exists(*entity)) {
        const ModelingObjectComponent *component =
            scene_.modeling_object(*entity);
        if (component != nullptr && component->sdk_owned) {
          if (const MeshComponent *mesh = scene_.mesh(*entity)) {
            displaced_meshes.push_back(mesh->mesh);
          }
          if (const CurveComponent *curve = scene_.curve(*entity)) {
            displaced_curves.push_back(curve->curve);
          }
          if (!mesh_sync_.destroy_projected_entity(scene_, *entity)) {
            return rollback("SDK history scene entity could not be removed.");
          }
          object_map_.unbind_object(native_id);
        }
      } else {
        object_map_.unbind_object(native_id);
      }
    }

    std::unordered_map<AssetId, MeshAsset> restored_meshes;
    std::unordered_map<AssetId, CurveAsset> restored_curves;
    std::vector<AssetId> created_mesh_assets;
    std::vector<AssetId> created_curve_assets;
    restored_meshes.reserve(snapshot.impl_->projected_objects.size());
    for (const SdkProjectedObjectSnapshot &projected :
         snapshot.impl_->projected_objects) {
      if (projected.curve.has_value()) {
        if (!assets_.has_curve(projected.curve_id)) {
          created_curve_assets.push_back(projected.curve_id);
        }
        restored_curves.emplace(projected.curve_id, *projected.curve);
      } else {
        if (!assets_.has_mesh(projected.mesh_id)) {
          created_mesh_assets.push_back(projected.mesh_id);
        }
        restored_meshes.emplace(projected.mesh_id,
                                clone_mesh_asset_payload(projected.mesh));
      }
    }
    if (!mesh_sync_.restore_history_mesh_assets(assets_, restored_meshes)) {
      return rollback("SDK history mesh assets could not be restored.");
    }
    publication.track_created_assets(created_mesh_assets);
    if (!mesh_sync_.restore_history_curve_assets(assets_, restored_curves)) {
      return rollback("SDK history curve assets could not be restored.");
    }
    publication.track_created_assets(created_curve_assets);

    struct RestoredSceneIdentity {
      int modeling_object_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      ModelingObjectSceneIdentity identity;
    };
    std::vector<RestoredSceneIdentity> restored_scene_identities;
    restored_scene_identities.reserve(
        snapshot.impl_->projected_objects.size());
    for (const SdkProjectedObjectSnapshot &projected :
         snapshot.impl_->projected_objects) {
      ModelingObjectSceneIdentity restored_identity = projected.identity;
      if (!history_transition) {
        const auto live =
            live_scene_identities.find(projected.modeling_object_id);
        if (live != live_scene_identities.end()) {
          const std::optional<MeshComponent> baseline_mesh =
              restored_identity.mesh;
          const std::optional<CurveComponent> baseline_curve =
              restored_identity.curve;
          restored_identity = live->second;
          if (baseline_mesh.has_value()) {
            MeshComponent mesh = restored_identity.mesh.value_or(*baseline_mesh);
            mesh.mesh = projected.mesh_id;
            restored_identity.mesh = mesh;
          }
          ModelingObjectComponent modeling_object =
              restored_identity.modeling_object.value_or(
                  ModelingObjectComponent{});
          modeling_object.material_slot = projected.material_slot;
          restored_identity.modeling_object = modeling_object;
          if (baseline_curve.has_value()) {
            CurveComponent curve =
                restored_identity.curve.value_or(*baseline_curve);
            curve.curve = projected.curve_id;
            restored_identity.curve = curve;
          }
        }
      }
      const std::optional<EntityId> current_entity =
          object_map_.entity_for_modeling_object(
              projected.modeling_object_id);
      if (current_entity.has_value() && scene_.exists(*current_entity)) {
        if (const MeshComponent *current_mesh = scene_.mesh(*current_entity);
            current_mesh != nullptr &&
            current_mesh->mesh != projected.mesh_id) {
          displaced_meshes.push_back(current_mesh->mesh);
        }
        if (const CurveComponent *current_curve = scene_.curve(*current_entity);
            current_curve != nullptr &&
            current_curve->curve != projected.curve_id) {
          displaced_curves.push_back(current_curve->curve);
        }
      }
      EntityId entity = restored_identity.id;
      if (!scene_.exists(entity)) {
        entity = mesh_sync_.restore_projected_mesh_entity(
            scene_, restored_identity);
        if (entity == kInvalidEntityId) {
          return rollback("SDK history scene entity could not be recreated.");
        }
        publication.track_created_entity(entity);
      }
      if (!mesh_sync_.apply_scene_identity_to_entity(
              scene_, entity, restored_identity,
              !preserve_scene_selection, false)) {
        return rollback(
            "SDK history scene identity payload could not be restored.");
      }
      const bool bound = projected.curve.has_value()
          ? mesh_sync_.bind_projected_curve_object(
                scene_, entity, projected.modeling_object_id, projected.curve_id,
                identity_local_frame_version(restored_identity))
          : mesh_sync_.bind_projected_object(
                scene_, entity, projected.modeling_object_id,
                projected.material_slot, true, projected.mesh_id,
                identity_local_frame_version(restored_identity));
      if (!bound) {
        return rollback("SDK history scene binding could not be restored.");
      }
      if (!mesh_sync_.set_projected_material(
              scene_, entity,
              restored_identity.material.has_value()
                  ? &*restored_identity.material
                  : nullptr)) {
        return rollback("SDK history scene material could not be restored.");
      }
      if (!object_map_.bind(projected.modeling_object_id, entity)) {
        return rollback("SDK history native scene binding could not be restored.");
      }
      restored_scene_identities.push_back({
          .modeling_object_id = projected.modeling_object_id,
          .entity = entity,
          .identity = std::move(restored_identity),
      });
    }

    if (restore_scene_hierarchy) {
      for (const auto &placement : snapshot.impl_->merge_child_placements) {
        if (!mesh_sync_.parent_projected_entity(
                scene_, placement.entity, placement.parent, false) ||
            !mesh_sync_.set_projected_transform(
                scene_, placement.entity, placement.transform)) {
          return rollback("Merge history descendant placement could not be restored.");
        }
      }
      // Restore every captured parent before applying any captured sibling
      // ordinal. Merely recreating every participant is insufficient: a later
      // Bridge participant can exist at top level while an earlier
      // participant's ordinal is validated against their eventual parent.
      for (const RestoredSceneIdentity &restored :
           restored_scene_identities) {
        if (!mesh_sync_.apply_scene_identity_parent(
                scene_, restored.entity, restored.identity)) {
          return rollback(
              "SDK history scene identity parent could not be restored.");
        }
      }
      std::vector<const RestoredSceneIdentity *> ordered_placements;
      std::unordered_map<EntityId, std::size_t> sibling_counts;
      for (const RestoredSceneIdentity &restored : restored_scene_identities) {
        const EntityId parent = scene_.parent(restored.entity);
        if (parent != restored.identity.parent ||
            !restored.identity.sibling_ordinal.has_value() ||
            scene_.sibling_ordinal(restored.entity) ==
                restored.identity.sibling_ordinal) {
          continue;
        }
        auto [count, inserted] = sibling_counts.try_emplace(parent, 0u);
        if (inserted) {
          count->second = scene_.children(parent).size();
        }
        if (*restored.identity.sibling_ordinal >= count->second) {
          return rollback(
              "SDK history scene identity sibling order could not be restored.");
        }
        ordered_placements.push_back(&restored);
      }
      // Determine the displaced subset before moving anything. Parking an
      // already-correct identity would mutate its Structure domain during a
      // semantic inverse even when the forward publication never touched it.
      for (const RestoredSceneIdentity *restored : ordered_placements) {
        if (!scene_.set_sibling_ordinal(
                restored->entity,
                sibling_counts.at(restored->identity.parent) - 1u)) {
          return rollback(
              "SDK history scene identity sibling order could not be restored.");
        }
      }
      auto child_placements = snapshot.impl_->merge_child_placements;
      std::ranges::sort(child_placements, [](const auto &left, const auto &right) {
        return left.parent != right.parent ? left.parent < right.parent
                                           : left.sibling_ordinal < right.sibling_ordinal;
      });
      for (const auto &placement : child_placements) {
        if (placement.sibling_ordinal.has_value() &&
            !scene_.set_sibling_ordinal(placement.entity, *placement.sibling_ordinal)) {
          return rollback("Merge history descendant order could not be restored.");
        }
      }
      std::ranges::sort(ordered_placements, [](const auto *left, const auto *right) {
        return left->identity.parent != right->identity.parent
                   ? left->identity.parent < right->identity.parent
                   : left->identity.sibling_ordinal < right->identity.sibling_ordinal;
      });
      for (const RestoredSceneIdentity *restored : ordered_placements) {
        if (!mesh_sync_.apply_scene_identity_sibling_ordinal(
                scene_, restored->entity, restored->identity)) {
          return rollback(
              "SDK history scene identity sibling order could not be restored.");
        }
      }
    }
    for (const RestoredSceneIdentity &restored :
         restored_scene_identities) {
      object_map_.remember_scene_entity(restored.modeling_object_id, scene_,
                                        restored.entity);
    }

    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    mesh_sync_.remove_unreferenced_history_curve_assets(
        scene_, assets_, displaced_curves);
    if (history_transition) {
      session_.restore_snapshot_as_history_transition(snapshot.impl_->session);
    } else {
      session_.restore_snapshot_preserving_identity_high_water(
          snapshot.impl_->session);
    }
    if (!object_map_.restore_sdk_object_bindings(snapshot.impl_->object_ids)) {
      return rollback("SDK history native object bindings could not be restored.");
    }
    next_native_object_id_ =
        std::max({next_native_object_id_, snapshot.impl_->next_native_object_id,
                  host_identity_high_water});
    active_native_object_id_ = snapshot.impl_->active_native_object_id;
    selection_mode_ = snapshot.impl_->selection_mode;
    if (history_transition) {
      if (persistent_content_revision_ !=
          snapshot.impl_->persistent_content_revision) {
        ++persistent_content_revision_;
      }
    } else {
      persistent_content_revision_ =
          snapshot.impl_->persistent_content_revision;
    }
    mode_revision_ = snapshot.impl_->mode_revision;
    clear_preview_checkpoints();
    if (editor_mode_service_ != nullptr && snapshot.impl_->editor_mode.has_value()) {
      editor_mode_service_->restore_history_state(*snapshot.impl_->editor_mode,
                                                  !history_transition);
      if (history_transition && mode_before.has_value() &&
          mode_before->mode == EditorMode::Object) {
        // An old Edit history item can restore geometry, but cannot re-enter Edit
        // or replace the current Object selection with its old editing targets.
        if (snapshot.impl_->editor_mode->mode == EditorMode::Edit) {
          selection_mode_ = ModelingSelectionMode::Object;
          std::vector<ObjectId> retained;
          active_native_object_id_ = kInvalidModelingObjectId;
          for (const EntityId entity : scene_selection_before.selected) {
            const auto native = object_map_.modeling_object_for_entity(entity);
            const auto object = native.has_value() ? sdk_object(*native) : std::nullopt;
            if (object.has_value() && scene_.exists(entity)) {
              retained.push_back(*object);
              if (entity == scene_selection_before.active) {
                active_native_object_id_ = *native;
                std::rotate(retained.begin(), std::prev(retained.end()), retained.end());
              }
            }
          }
          const auto restored = session_.apply_object_selection(retained, SelectionEdit::Replace);
          if (!restored.ok()) {
            return rollback(restored.error_message());
          }
        }
      } else if (!synchronize_edit_membership(false)) {
        return rollback("Editor mode history membership could not be restored.");
      }
    }
    if (!preserve_scene_selection && !add_selection_to_scene()) {
      return rollback("SDK history selection could not be restored.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication, {.ok = true, .changed = true});
  }

  void set_failure_point_for_tests(ModelingSdkProjectionFailurePoint point) {
    failure_point_ = point;
  }

  [[nodiscard]] ModelingSdkProjectionStats stats() const noexcept {
    return stats_;
  }

private:
  [[nodiscard]] Result<modeling::PreparedUvPeel> prepare_uv_peel_documents(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options, std::vector<ObjectId> &objects,
      std::optional<PreviewId> preview = std::nullopt) {
    const auto invalid = [](std::string message) {
      return Result<modeling::PreparedUvPeel>::failure(modeling::make_error(
          modeling::ErrorCode::InvalidArgument, std::move(message)));
    };
    if (selection_mode_ != ModelingSelectionMode::Edge) {
      return invalid("UV Peel requires edge selection mode.");
    }
    if (targets.empty()) {
      return invalid("Select one or more edge guides first.");
    }
    objects.reserve(targets.size());
    for (const auto &target : targets) {
      const auto object = sdk_object(target.modeling_object_id);
      if (!object.has_value() || !edit_scope_contains(target.modeling_object_id) ||
          target.edges.empty() || std::ranges::find(objects, *object) != objects.end()) {
        return invalid("The selected UV Peel targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    std::vector<PolygonDocument> documents;
    documents.reserve(objects.size());
    if (preview.has_value()) {
      for (const ObjectId object : objects) {
        auto document = session_.preview_base_document_copy(*preview, object);
        if (!document.ok()) return invalid(document.error_message());
        documents.push_back(std::move(document).value());
      }
    } else {
      auto copied = session_.document_copies(objects);
      if (!copied.ok()) return invalid(copied.error_message());
      documents = std::move(copied).value();
    }
    std::vector<modeling::UvPeelTarget> portable_targets;
    portable_targets.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const auto &target = targets[index];
      portable_targets.push_back({
          .key = static_cast<std::uint64_t>(target.modeling_object_id),
          .document = &documents[index],
          .edges = target.edges,
          .active_edge = target.active_edge,
          .world_from_local = to_sdk(target.world_from_local),
          .sampling_from_local = to_sdk(target.sampling_from_local),
      });
    }
    ++stats_.uv_peel_preparation_count;
    return modeling::prepare_uv_peel(portable_targets, options);
  }

  [[nodiscard]] PreviewSelectionEditState
  suspend_operation_preview_for_selection_edit() {
    PreviewSelectionEditState state;
    if (face_texture_gesture_active() && face_texture_gesture_selection_cancel_handler_) {
      const auto canceled = face_texture_gesture_selection_cancel_handler_();
      state.ok = canceled.ok;
      state.active = !canceled.ok;
      state.message = canceled.message;
      return state;
    }
    if (uv_peel_selection_edit_in_progress_) {
      return state;
    }
    if (!preview_active()) {
      if (uv_peel_preview_tool_active_ && uv_peel_preview_tool_active_()) {
        // A tool awaiting its first valid guide has no SDK candidate to suspend,
        // but canonical selection must still synchronously rebuild its preview.
        state.active = true;
        state.uv_peel = true;
        uv_peel_selection_edit_in_progress_ = true;
      }
      return state;
    }
    state.active = true;
    if (uv_peel_preview_active()) {
      state.uv_peel = true;
      if (uv_peel_preview_before_selection_suspend_) {
        uv_peel_preview_before_selection_suspend_();
      }
      const auto canceled = cancel_translate_preview();
      if (uv_peel_preview_after_selection_suspend_) {
        uv_peel_preview_after_selection_suspend_();
      }
      state.ok = canceled.ok;
      state.message = canceled.message;
      uv_peel_selection_edit_in_progress_ = canceled.ok;
      return state;
    }
    if (!active_preview_operation_.has_value() ||
        !active_preview_settings_.has_value()) {
      state.ok = false;
      state.message =
          "Component selection cannot change during this SDK preview.";
      return state;
    }
    state.operation = *active_preview_operation_;
    state.settings = *active_preview_settings_;
    state.edge_bevel_maximum_width = active_edge_bevel_maximum_width_;
    ModelingCommandResult canceled = cancel_translate_preview();
    if (!canceled.ok) {
      state.ok = false;
      state.message = canceled.message;
    }
    return state;
  }

  [[nodiscard]] ModelingCommandResult finish_operation_preview_selection_edit(
      const PreviewSelectionEditState &state, ModelingCommandResult result) {
    if (!state.active) {
      return result;
    }
    if (state.uv_peel) {
      uv_peel_selection_edit_in_progress_ = false;
    }
    const ModelingCommandResult rebuilt = state.uv_peel
        ? (uv_peel_preview_selection_refresh_handler_
               ? uv_peel_preview_selection_refresh_handler_()
               : ModelingCommandResult{.ok = true})
        : begin_operation_preview(state.operation, state.settings);
    if (!rebuilt.ok) {
      return {.ok = false,
              .changed = result.changed,
              .message = rebuilt.message.empty()
                             ? "SDK operation preview could not be rebuilt."
                             : rebuilt.message};
    }
    if (!result.changed && !state.uv_peel &&
        state.operation == quader_editor_core::EditorOperationId::EdgeBevel) {
      // A rejected or identical selection leaves the same source and shape
      // settings. Replaying the accepted width need not hit the clamp again.
      active_edge_bevel_maximum_width_ = state.edge_bevel_maximum_width;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult set_object_selection_during_curve_preview(
      std::span<const ObjectId> selected, int active_native,
      const SceneSelectionState &desired_scene_selection,
      bool scene_selection_changed, bool adapter_selection_changed,
      bool preserve_selection_mode) {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preview.active ||
        preview.kind != modeling::SessionObjectKind::Curve ||
        !mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview.id) ||
        !preflight_preview_mutation(preview.id)) {
      return failure(
          "Curve preview participants changed outside this preview.");
    }

    Result<modeling::CurveDocument> authored =
        session_.preview_curve_document_copy(preview.id);
    if (!authored.ok()) {
      return failure(authored.error_message());
    }

    // A checked scene-selection command has already authored its scene side
    // before it calls this projection. Canceling and republishing the live
    // Curve preview here would advance scene/asset revisions inside that
    // command's scalar fence. Prepare the complete SDK transition on an
    // isolated candidate instead, then publish it with one no-throw move.
    // The portable session's active-preview and mutation-checkpoint exclusion
    // remains intact: selection is applied only after the candidate preview
    // has been canceled.
    modeling::ModelingSession candidate = session_;
    Result<OperationResult> canceled = candidate.cancel_preview(preview.id);
    if (!canceled.ok()) {
      return failure(canceled.error_message());
    }
    Result<OperationResult> selected_result =
        candidate.apply_object_selection(selected, SelectionEdit::Replace);
    if (!selected_result.ok()) {
      return failure(selected_result.error_message());
    }
    if (selected_result.value().changed &&
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return failure("Injected failure after SDK selection mutation.");
    }

    OperationResult preview_receipt;
    preview_receipt.changed = preview_changed_;
    preview_receipt.dirty.topology = true;
    preview_receipt.dirty.geometry = true;
    preview_receipt.dirty.overlays = true;
    Result<PreviewId> rebuilt = candidate.begin_curve_document_preview(
        preview.object_id, preview.operation_id,
        std::move(authored).value(), std::move(preview_receipt));
    if (!rebuilt.ok()) {
      return failure(rebuilt.error_message());
    }
    const PreviewSdkFence rebuilt_fence =
        capture_preview_sdk_fence(candidate, rebuilt.value());
    if (rebuilt_fence.preview != rebuilt.value()) {
      return failure("Curve preview SDK fence could not be rebuilt.");
    }

    std::optional<SceneModelingProjectionCheckpoint> scene_checkpoint;
    if (scene_selection_changed) {
      if (fails(
              ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
        return failure(
            "Injected failure during SDK selection publication.");
      }
      const SceneSelectionState before = capture_scene_selection(scene_);
      scene_checkpoint.emplace(capture_scene_selection_checkpoint(
          scene_, before, desired_scene_selection));
      if (!scene_checkpoint->valid()) {
        return failure("SDK scene-selection checkpoint is unavailable.");
      }
      QDR_PROFILE_SCOPE("Curve preview object selection scene publication");
      (void)apply_scene_selection(scene_, desired_scene_selection,
                                  SceneSelectionApplicationMode::RuntimeSync);
      if (!same_scene_selection(capture_scene_selection(scene_),
                                desired_scene_selection)) {
        const bool restored =
            restore_scene_selection_checkpoint(scene_, *scene_checkpoint);
        return {
            .ok = false,
            .changed = !restored,
            .message =
                restored
                    ? "SDK scene selection could not be published."
                    : "SDK scene selection publication and rollback failed.",
        };
      }
    }

    session_ = std::move(candidate);
    MeshPreviewHostBaseline &baseline = *mesh_preview_host_baseline_;
    baseline.preview_id = rebuilt.value();
    baseline.acknowledged_sdk = rebuilt_fence;
    baseline.fence_acknowledged = true;

    active_native_object_id_ = active_native;
    if (!preserve_selection_mode) {
      if (selection_mode_ != ModelingSelectionMode::Object) {
        ++mode_revision_;
      }
      selection_mode_ = ModelingSelectionMode::Object;
    }
    // Cancel must restore the adapter state that existed after every external
    // selection edit, while the retained Curve/asset checkpoint continues to
    // restore only the original authored Curve projection.
    baseline.adapter_before.active_native_object_id =
        active_native_object_id_;
    baseline.adapter_before.selection_mode = selection_mode_;
    baseline.adapter_before.mode_revision = mode_revision_;

    selected_result.value().changed = selected_result.value().changed ||
                                      scene_selection_changed ||
                                      adapter_selection_changed;
    return from_sdk(selected_result);
  }

  [[nodiscard]] std::vector<ObjectId> selected_sdk_objects() const {
    std::vector<ObjectId> selected;
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const quader::modeling::SessionObjectSummary &object : objects) {
      const auto native = native_object(object.id);
      const bool in_scope = editor_mode_service_ == nullptr ||
          editor_mode_service_->mode() != EditorMode::Edit ||
          (native.has_value() && edit_scope_contains(*native));
      if (object.selected && in_scope) {
        selected.push_back(object.id);
      }
    }
    return selected;
  }

  [[nodiscard]] bool publish_new_object(
      ModelingHostPublicationTransaction *publication, ObjectId object_id,
      const ModelingObjectSceneIdentity &source_identity,
      std::uint32_t material_slot, int &native_id, EntityId &entity,
      bool share_source_mesh = false,
      std::optional<int> reserved_native_id = std::nullopt,
      std::optional<PreviewId> preview_id = std::nullopt) {
    const bool is_curve = source_identity.curve.has_value();
    AssetId curve_id = share_source_mesh && is_curve
                           ? source_identity.curve->curve : kInvalidAssetId;
    if (is_curve && (curve_id == kInvalidAssetId || assets_.curve(curve_id) == nullptr)) {
      Result<modeling::CurveDocument> document = [&]() {
        if (preview_id.has_value()) {
          return session_.preview_curve_document_copy(*preview_id, object_id);
        }
        const auto snapshot = session_.curve_document_snapshot(object_id);
        return snapshot.ok()
                   ? Result<modeling::CurveDocument>::success(*snapshot.value())
                   : Result<modeling::CurveDocument>::failure(snapshot.error());
      }();
      if (!document.ok()) {
        return false;
      }
      CurveAsset projected;
      projected.name = source_identity.name;
      projected.document =
          std::make_shared<const modeling::CurveDocument>(document.value());
      curve_id = mesh_sync_.install_projected_curve(assets_, std::move(projected));
      if (curve_id == kInvalidAssetId) {
        return false;
      }
      if (publication != nullptr) {
        publication->track_created_asset(curve_id);
      }
      if (preview_id.has_value()) {
        record_preview_created_asset(curve_id);
      }
      ++stats_.asset_publication_count;
    }
    AssetId mesh_id = share_source_mesh && source_identity.mesh.has_value()
                          ? source_identity.mesh->mesh
                          : kInvalidAssetId;
    if (!is_curve && (mesh_id == kInvalidAssetId || assets_.mesh(mesh_id) == nullptr)) {
      Result<PolygonDocument> document =
          preview_id.has_value()
              ? session_.preview_document_copy(*preview_id, object_id)
              : session_.document_copy(object_id);
      if (!document.ok()) {
        return false;
      }
      ++stats_.mesh_compile_count;
      std::optional<MeshAsset> projected =
          MeshProjectionService::compile_sdk_document_mesh(document.value());
      if (!projected.has_value()) {
        return false;
      }
      projected->name = source_identity.name;
      const AssetRegistryValidationResult validation =
          assets_.validate_mesh(*projected);
      if (!validation.ok) {
        return false;
      }
      mesh_id = mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
      if (mesh_id == kInvalidAssetId) {
        return false;
      }
      if (publication != nullptr) {
        publication->track_created_asset(mesh_id);
      }
      if (preview_id.has_value()) {
        record_preview_created_asset(mesh_id);
      }
      ++stats_.asset_publication_count;
    }
    entity = is_curve
        ? mesh_sync_.create_projected_curve_entity(scene_, source_identity.name)
        : mesh_sync_.create_projected_mesh_entity(scene_, source_identity.name);
    if (entity == kInvalidEntityId) {
      return false;
    }
    if (publication != nullptr) {
      publication->track_created_entity(entity);
    }
    if (preview_id.has_value()) {
      record_preview_created_entity(entity);
    }
    const std::optional<int> allocated_native_id =
        reserved_native_id.has_value()
            ? reserved_native_id
            : reserve_native_object_ids(1U);
    if (!allocated_native_id.has_value()) {
      return false;
    }
    native_id = *allocated_native_id;
    if (native_id == kInvalidModelingObjectId ||
        native_id >= std::numeric_limits<int>::max()) {
      return false;
    }
    const bool bound = is_curve
        ? mesh_sync_.bind_projected_curve_object(
              scene_, entity, native_id, curve_id,
              identity_local_frame_version(source_identity))
        : mesh_sync_.bind_projected_object(
              scene_, entity, native_id, material_slot, true, mesh_id,
              identity_local_frame_version(source_identity));
    if (!bound) {
      return false;
    }
    if (!mesh_sync_.apply_scene_identity_to_duplicate_entity(
            scene_, entity, source_identity)) {
      return false;
    }
    if (!object_map_.bind_sdk_object(native_id, object_id) ||
        !object_map_.bind(native_id, entity)) {
      return false;
    }
    object_map_.remember_scene_entity(native_id, scene_, entity);
    return true;
  }

  [[nodiscard]] ModelingSdkDuplicateBatchResult duplicate_objects(
      std::span<const int> source_modeling_object_ids,
      std::size_t copies_per_source, bool select_sources,
      bool preview_publication = false,
      bool share_source_mesh_assets = false) {
    const auto failure_result = [](std::string message, bool changed = false) {
      return ModelingSdkDuplicateBatchResult{
          .command = {.ok = false,
                      .changed = changed,
                      .message = std::move(message)},
      };
    };
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure_result("Duplicate Meshes requires object selection mode.");
    }
    if (source_modeling_object_ids.empty() || copies_per_source == 0U) {
      return failure_result("Duplicate Meshes needs source objects and copies.");
    }
    if (preview_active() && !preview_publication) {
      return failure_result("Commit or cancel the active preview first.");
    }

    std::vector<ObjectId> sources;
    sources.reserve(source_modeling_object_ids.size());
    std::unordered_set<std::uint64_t> unique_sources;
    unique_sources.reserve(source_modeling_object_ids.size());
    for (int native_id : source_modeling_object_ids) {
      const std::optional<ObjectId> source = sdk_object(native_id);
      if (!source.has_value() ||
          !unique_sources.emplace(sdk_object_key(*source)).second) {
        return failure_result("Duplicate source object is not live or unique.");
      }
      sources.push_back(*source);
    }
    if (copies_per_source >
        std::numeric_limits<std::size_t>::max() / sources.size()) {
      return failure_result("Duplicate object count is too large.");
    }
    const std::size_t duplicate_count = sources.size() * copies_per_source;
    const int first_native_candidate = next_available_native_object_id();
    const std::size_t available_native_ids =
        first_native_candidate == kInvalidModelingObjectId
            ? 0u
            : static_cast<std::size_t>(std::numeric_limits<int>::max() -
                                       first_native_candidate);
    if (first_native_candidate == kInvalidModelingObjectId ||
        duplicate_count > available_native_ids) {
      return failure_result("Native modeling object IDs are exhausted.");
    }
    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(duplicate_count);
    for (std::size_t index = 0; index < duplicate_count; ++index) {
      affected_modeling_ids.push_back(first_native_candidate +
                                      static_cast<int>(index));
    }
    for (const int source_native : source_modeling_object_ids) {
      const std::optional<EntityId> source_entity =
          object_map_.entity_for_modeling_object(source_native);
      if (!source_entity.has_value() || !scene_.exists(*source_entity)) {
        return failure_result("Duplicate source scene identity is not live.");
      }
      std::vector<EntityId> subtree =
          scene_.descendants(*source_entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
      if (const MeshComponent *mesh = scene_.mesh(*source_entity)) {
        affected_assets.push_back(mesh->mesh);
      }
      if (const CurveComponent *curve = scene_.curve(*source_entity)) {
        affected_assets.push_back(curve->curve);
      }
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure_result("Duplicate publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(
          publication, failure_result(std::move(message)));
    };
    const std::optional<int> first_native_id =
        reserve_preflighted_native_object_ids(first_native_candidate,
                                              duplicate_count);
    if (!first_native_id.has_value() ||
        *first_native_id != first_native_candidate) {
      return rollback("Native modeling object IDs could not be reserved.");
    }

    /// Retains native scene facts needed to publish one duplicated SDK object.
    struct DuplicateSourceProjection {
      ModelingObjectSceneIdentity identity;
      std::uint32_t material_slot = 0U;
    };
    std::vector<DuplicateSourceProjection> source_projections;
    source_projections.reserve(sources.size());
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      const int source_native = source_modeling_object_ids[source_index];
      const std::optional<EntityId> source_entity =
          object_map_.entity_for_modeling_object(source_native);
      if (!source_entity.has_value() || !scene_.exists(*source_entity)) {
        return rollback("Duplicate source scene identity is not live.");
      }
      object_map_.remember_scene_entity(source_native, scene_, *source_entity);
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_map_.scene_identity_for_modeling_object(source_native);
      const ModelingObjectComponent *component =
          scene_.modeling_object(*source_entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Duplicate source scene identity is incomplete.");
      }
      source_projections.push_back({
          .identity = *identity,
          .material_slot = component->material_slot,
      });
    }

    std::vector<ObjectId> expanded_sources;
    expanded_sources.reserve(duplicate_count);
    for (const ObjectId source : sources) {
      for (std::size_t copy = 0; copy < copies_per_source; ++copy) {
        expanded_sources.push_back(source);
      }
    }
    Result<std::vector<ObjectId>> duplicated_batch =
        session_.duplicate_objects(expanded_sources, {});
    if (!duplicated_batch.ok()) {
      return rollback(duplicated_batch.error_message());
    }
    std::vector<ObjectId> duplicated_objects =
        std::move(duplicated_batch).value();
    if (duplicated_objects.size() != duplicate_count) {
      return rollback("SDK duplicate batch returned incomplete results.");
    }
    ++stats_.sdk_duplicate_batch_mutation_count;
    stats_.sdk_duplicate_object_count += duplicated_objects.size();

    ModelingSdkDuplicateBatchResult result{
        .command = {.ok = true,
                    .changed = true,
                    .message = "Duplicated selected meshes."},
    };
    result.duplicates.reserve(duplicate_count);
    std::vector<ObjectId> selection = select_sources ? sources
                                                     : std::vector<ObjectId>{};
    selection.reserve(selection.size() + duplicate_count);
    std::size_t duplicate_index = 0U;
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      for (std::size_t copy = 0; copy < copies_per_source; ++copy) {
        const ObjectId duplicated = duplicated_objects[duplicate_index];
        int native_id = kInvalidModelingObjectId;
        EntityId entity = kInvalidEntityId;
        const bool published = publish_new_object(
            &publication, duplicated, source_projections[source_index].identity,
            source_projections[source_index].material_slot, native_id, entity,
            share_source_mesh_assets,
            *first_native_id + static_cast<int>(duplicate_index));
        if (entity != kInvalidEntityId) {
          publication.track_created_entity(entity);
        }
        if (!published) {
          return rollback("SDK duplicate host projection failed.");
        }
        ModelingObjectRecord record{
            .modeling_object_id = native_id,
            .entity = entity,
            .name = source_projections[source_index].identity.name,
        };
        if (const MeshComponent *mesh = scene_.mesh(entity)) {
          record.mesh = mesh->mesh;
        }
        result.duplicates.push_back(std::move(record));
        selection.push_back(duplicated);
        active_native_object_id_ = native_id;
        ++duplicate_index;
      }
    }
    Result<OperationResult> selected =
        session_.apply_object_selection(selection, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK duplicate batch.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    ++stats_.scene_publication_count;
    if (!add_selection_to_scene()) {
      return rollback("SDK duplicate selection could not be published.");
    }
    return commit_publication_result(publication, std::move(result));
  }

  [[nodiscard]] ModelingSdkClipboardCopyResult
  copy_selection_to_clipboard() {
    ModelingSdkClipboardCopyResult result;
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const quader::modeling::SessionObjectSummary &object : objects) {
      if (!object.selected) {
        continue;
      }
      const std::optional<int> native_id = native_object(object.id);
      if (!native_id.has_value()) {
        result.command = failure(
            "Selected SDK clipboard object has no native projection identity.");
        return result;
      }
      result.source_modeling_object_ids.push_back(*native_id);
    }

    Result<OperationResult> copied = session_.copy_selection_to_clipboard();
    if (!copied.ok()) {
      result.source_modeling_object_ids.clear();
      result.command = failure(copied.error_message());
      return result;
    }
    result.command = {
        .ok = true,
        .changed = copied.value().changed,
        .message = copied.value().message,
    };
    return result;
  }

  [[nodiscard]] ModelingSdkDuplicateBatchResult paste_clipboard(
      std::span<const ModelingObjectSceneIdentity> source_identities) {
    const auto failed = [](std::string message, bool changed = false) {
      return ModelingSdkDuplicateBatchResult{
          .command = {.ok = false,
                      .changed = changed,
                      .message = std::move(message)},
      };
    };
    if (!session_.clipboard_available()) {
      return failed("Copy one or more meshes before pasting.");
    }
    if (source_identities.size() != session_.clipboard_object_count()) {
      return failed("Clipboard host identity payload is incomplete.");
    }
    if (preview_active()) {
      return failed("Commit or cancel the active preview before pasting.");
    }
    const std::size_t pasted_count = source_identities.size();
    const int first_native_candidate = next_available_native_object_id();
    const std::size_t available_native_ids =
        first_native_candidate == kInvalidModelingObjectId
            ? 0u
            : static_cast<std::size_t>(std::numeric_limits<int>::max() -
                                       first_native_candidate);
    if (first_native_candidate == kInvalidModelingObjectId ||
        pasted_count > available_native_ids) {
      return failed("Native modeling object IDs are exhausted.");
    }
    std::vector<int> affected_modeling_ids;
    affected_modeling_ids.reserve(pasted_count);
    for (std::size_t index = 0; index < pasted_count; ++index) {
      affected_modeling_ids.push_back(first_native_candidate +
                                      static_cast<int>(index));
    }
    std::vector<EntityId> affected_entities;
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failed("Paste publication checkpoint is unavailable.");
    }
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failed(std::move(message)));
    };
    const std::optional<int> first_native_id =
        reserve_native_object_ids(pasted_count);
    if (!first_native_id.has_value() ||
        *first_native_id != first_native_candidate) {
      return rollback("Native modeling object IDs could not be reserved.");
    }

    Result<quader::modeling::ClipboardPasteResult> pasted =
        session_.paste_clipboard({});
    if (!pasted.ok()) {
      return rollback(pasted.error_message());
    }
    if (pasted.value().objects.size() != source_identities.size()) {
      return rollback("SDK clipboard paste returned an unexpected object map.");
    }

    ModelingSdkDuplicateBatchResult result{
        .command = {.ok = true,
                    .changed = pasted.value().operation.changed,
                    .message = pasted.value().operation.message},
    };
    result.duplicates.reserve(pasted.value().objects.size());
    const std::vector<quader::modeling::SessionObjectSummary> summaries =
        session_.objects();
    std::unordered_map<std::uint64_t,
                       const quader::modeling::SessionObjectSummary *>
        summary_by_object;
    summary_by_object.reserve(summaries.size());
    for (const quader::modeling::SessionObjectSummary &summary : summaries) {
      summary_by_object.emplace(sdk_object_key(summary.id), &summary);
    }
    for (std::size_t index = 0; index < pasted.value().objects.size(); ++index) {
      ModelingObjectSceneIdentity identity = source_identities[index];
      // Clipboard IDs belong to the source scene. Restore relationships only
      // after every destination entity has been allocated.
      identity.parent = kInvalidEntityId;
      identity.sibling_ordinal.reset();
      const auto summary =
          summary_by_object.find(sdk_object_key(pasted.value().objects[index]));
      if (summary == summary_by_object.end()) {
        return rollback("SDK clipboard object summary is unavailable.");
      }
      if (identity.name.empty()) {
        identity.name = summary->second->name;
      }
      const std::uint32_t material_slot =
          identity_material_slot(identity);
      int native_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      const bool published = publish_new_object(
          &publication, pasted.value().objects[index], identity, material_slot,
          native_id, entity, false,
          *first_native_id + static_cast<int>(index));
      if (entity != kInvalidEntityId) {
        publication.track_created_entity(entity);
      }
      if (!published) {
        return rollback("SDK clipboard host projection failed.");
      }
      ModelingObjectRecord record{
          .modeling_object_id = native_id,
          .entity = entity,
          .name = identity.name,
      };
      if (const MeshComponent *mesh = scene_.mesh(entity)) {
        record.mesh = mesh->mesh;
      }
      result.duplicates.push_back(std::move(record));
      active_native_object_id_ = native_id;
    }

    std::unordered_map<EntityId, EntityId> pasted_entities;
    for (std::size_t index = 0; index < source_identities.size(); ++index) {
      pasted_entities.emplace(source_identities[index].id,
                              result.duplicates[index].entity);
    }
    std::vector<SceneParentAssignment> parents;
    for (std::size_t index = 0; index < source_identities.size(); ++index) {
      const auto &identity = source_identities[index];
      if (identity.parent == kInvalidEntityId) {
        continue;
      }
      const auto parent = pasted_entities.find(identity.parent);
      if (parent == pasted_entities.end()) {
        return rollback("Clipboard parent identity is unavailable.");
      }
      parents.push_back({.child = result.duplicates[index].entity,
                         .parent = parent->second});
    }
    for (const SceneParentAssignment &parent : parents) {
      ModelingObjectSceneIdentity placement;
      placement.parent = parent.parent;
      if (!mesh_sync_.apply_scene_identity_parent(scene_, parent.child,
                                                  placement)) {
        return rollback("Clipboard hierarchy could not be restored.");
      }
    }
    for (const ModelingObjectRecord &record : result.duplicates) {
      object_map_.remember_scene_entity(record.modeling_object_id, scene_,
                                         record.entity);
    }

    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK clipboard publication.");
    }
    if (selection_mode_ != ModelingSelectionMode::Object) {
      selection_mode_ = ModelingSelectionMode::Object;
      ++mode_revision_;
    }
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    if (!add_selection_to_scene()) {
      return rollback("SDK paste selection could not be published.");
    }
    return commit_publication_result(publication, std::move(result));
  }

  [[nodiscard]] bool clipboard_available() const {
    return session_.clipboard_available();
  }

  [[nodiscard]] std::size_t clipboard_object_count() const {
    return session_.clipboard_object_count();
  }

  [[nodiscard]] ModelingCommandResult
  duplicate_selected_objects(QVec3 geometry_offset,
                             const DocumentMutation &duplicate_mutation = {},
                             std::string_view success_message =
                                 "Duplicated selected meshes.",
                             bool preview_publication = false,
                             std::string_view duplicate_name = {}) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Duplicate Meshes requires object selection mode.");
    }
    if (!finite(geometry_offset)) {
      return failure("Duplicate offset must be finite.");
    }
    const std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.empty()) {
      return failure("Select one or more meshes to duplicate.");
    }
    if (preview_active() && !preview_publication) {
      return failure("Commit or cancel the active preview first.");
    }

    const int first_native_candidate = next_available_native_object_id();
    const std::size_t available_native_ids =
        first_native_candidate == kInvalidModelingObjectId
            ? 0u
            : static_cast<std::size_t>(std::numeric_limits<int>::max() -
                                       first_native_candidate);
    if (first_native_candidate == kInvalidModelingObjectId ||
        sources.size() > available_native_ids) {
      return failure("Native modeling object IDs are exhausted.");
    }
    const std::optional<int> first_native_id{first_native_candidate};
    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
      affected_modeling_ids.push_back(*first_native_id +
                                      static_cast<int>(index));
      const std::optional<int> source_native = native_object(sources[index]);
      const std::optional<EntityId> source_entity =
          source_native.has_value()
              ? object_map_.entity_for_modeling_object(*source_native)
              : std::nullopt;
      if (!source_entity.has_value() || !scene_.exists(*source_entity)) {
        return failure("Duplicate source scene identity is not live.");
      }
      std::vector<EntityId> subtree =
          scene_.descendants(*source_entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
      if (const MeshComponent *mesh = scene_.mesh(*source_entity)) {
        affected_assets.push_back(mesh->mesh);
      }
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    const std::optional<int> reserved_native_ids =
        reserve_native_object_ids(sources.size());
    if (reserved_native_ids != first_native_id) {
      return rollback("Native modeling object IDs could not be reserved.");
    }

    Result<std::vector<ObjectId>> duplicated_batch = [&]() {
      QDR_PROFILE_SCOPE("SDK duplicate selected objects atomic batch");
      return session_.duplicate_objects(sources, to_sdk(geometry_offset),
                                        std::string(duplicate_name));
    }();
    if (!duplicated_batch.ok()) {
      return rollback(duplicated_batch.error_message());
    }
    std::vector<ObjectId> duplicates =
        std::move(duplicated_batch).value();
    if (duplicates.size() != sources.size()) {
      return rollback("SDK duplicate batch returned incomplete results.");
    }
    ++stats_.sdk_duplicate_batch_mutation_count;
    stats_.sdk_duplicate_object_count += duplicates.size();
    QDR_PROFILE_SCOPE("SDK duplicate publish host objects");
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      const ObjectId source = sources[source_index];
      const ObjectId duplicated = duplicates[source_index];
      const std::optional<int> source_native = native_object(source);
      const std::optional<EntityId> source_entity =
          source_native.has_value()
              ? object_map_.entity_for_modeling_object(*source_native)
              : std::nullopt;
      if (!source_native.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity)) {
        return rollback("Duplicate source scene identity is not live.");
      }
      object_map_.remember_scene_entity(*source_native, scene_, *source_entity);
      const std::optional<ModelingObjectSceneIdentity> source_identity =
          object_map_.scene_identity_for_modeling_object(*source_native);
      const ModelingObjectComponent *source_component =
          scene_.modeling_object(*source_entity);
      if (!source_identity.has_value() || source_component == nullptr) {
        return rollback("Duplicate source scene identity is incomplete.");
      }
      ModelingObjectSceneIdentity duplicate_identity = *source_identity;
      if (!duplicate_name.empty()) {
        duplicate_identity.name = std::string(duplicate_name);
      }

      Result<PolygonDocument> document =
          session_.document_copy(duplicated);
      if (!document.ok()) {
        return rollback(document.error_message());
      }
      if (duplicate_mutation) {
        Result<OperationResult> mutated = duplicate_mutation(document.value());
        if (!mutated.ok()) {
          return rollback(mutated.error_message());
        }
        if (!mutated.value().success) {
          return rollback(mutated.value().message.empty()
                              ? "SDK duplicate mutation failed."
                              : mutated.value().message);
        }
        if (mutated.value().changed) {
          Result<OperationResult> committed = session_.commit_document(
              duplicated, std::move(document).value(),
              std::move(mutated).value());
          if (!committed.ok()) {
            return rollback(committed.error_message());
          }
          document = session_.document_copy(duplicated);
          if (!document.ok()) {
            return rollback(document.error_message());
          }
        }
      }
      AssetId mesh_id = kInvalidAssetId;
      if (mesh_id == kInvalidAssetId || assets_.mesh(mesh_id) == nullptr) {
        ++stats_.mesh_compile_count;
        std::optional<MeshAsset> projected =
            MeshProjectionService::compile_sdk_document_mesh(document.value());
        if (!projected.has_value()) {
          return rollback("SDK duplicate mesh projection failed.");
        }
        projected->name = duplicate_identity.name;
        const AssetRegistryValidationResult validation =
            assets_.validate_mesh(*projected);
        if (!validation.ok) {
          return rollback(validation.message.empty()
                              ? "SDK duplicate mesh projection is invalid."
                              : validation.message);
        }
        mesh_id =
            mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
        if (mesh_id == kInvalidAssetId) {
          return rollback("SDK duplicate mesh asset could not be installed.");
        }
        publication.track_created_asset(mesh_id);
        ++stats_.asset_publication_count;
      }

      const EntityId entity = mesh_sync_.create_projected_mesh_entity(
          scene_, duplicate_identity.name);
      if (entity == kInvalidEntityId) {
        return rollback("SDK duplicate scene entity could not be created.");
      }
      publication.track_created_entity(entity);
      const int native_id =
          *first_native_id + static_cast<int>(source_index);
      if (native_id == kInvalidModelingObjectId ||
          native_id >= std::numeric_limits<int>::max() ||
          !mesh_sync_.bind_projected_object(
              scene_, entity, native_id, source_component->material_slot, true,
              mesh_id, identity_local_frame_version(duplicate_identity))) {
        return rollback("SDK duplicate scene binding could not be installed.");
      }
      if (!mesh_sync_.apply_scene_identity_to_duplicate_entity(
              scene_, entity, duplicate_identity)) {
        return rollback("SDK duplicate scene identity could not be applied.");
      }
      if (!object_map_.bind_sdk_object(native_id, duplicated) ||
          !object_map_.bind(native_id, entity)) {
        return rollback("SDK duplicate identity binding could not be installed.");
      }
      object_map_.remember_scene_entity(native_id, scene_, entity);
      active_native_object_id_ = native_id;
    }

    Result<OperationResult> selected =
        session_.apply_object_selection(duplicates, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK duplicate publication.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    ++stats_.scene_publication_count;
    if (!add_selection_to_scene()) {
      return rollback("SDK duplicate selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true,
         .changed = true,
         .message = std::string(success_message)});
  }

  [[nodiscard]] ModelingCommandResult mirror_selected_objects_across_plane(
      QVec3 origin, QVec3 normal) {
    if (!finite(origin) || !finite(normal)) {
      return failure("Mirror plane must be finite.");
    }
    return duplicate_selected_objects(
        {},
        [origin, normal](PolygonDocument &document) {
          return document.reflect_across_plane(to_sdk(origin), to_sdk(normal));
        },
        "Mirrored selected meshes.");
  }

  [[nodiscard]] ModelingCommandResult mirror_objects_across_planes(
      std::span<const ModelingSdkPlaneCutRequest> requests) {
    if (requests.empty()) {
      return failure("Mirror needs selected mesh objects.");
    }
    /// Stores one validated source plus its object-local reflection.
    struct PreparedMirror {
      ObjectId source;
      ModelingObjectSceneIdentity identity;
      std::uint32_t material_slot = 0u;
      Vec3 origin;
      Vec3 normal;
    };
    std::vector<PreparedMirror> prepared;
    std::vector<ObjectId> sources;
    std::unordered_set<std::uint64_t> unique_sources;
    prepared.reserve(requests.size());
    sources.reserve(requests.size());
    unique_sources.reserve(requests.size());
    for (const ModelingSdkPlaneCutRequest &request : requests) {
      const std::optional<ObjectId> source =
          sdk_object(request.modeling_object_id);
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(request.modeling_object_id);
      const ModelingObjectComponent *component =
          entity.has_value() && scene_.exists(*entity)
              ? scene_.modeling_object(*entity)
              : nullptr;
      const std::optional<SceneEntityState> state =
          entity.has_value() ? capture_scene_entity_state(scene_, *entity)
                             : std::nullopt;
      const QVec3 first = request.local_plane_points[0];
      const QVec3 second = request.local_plane_points[1];
      const QVec3 third = request.local_plane_points[2];
      const QVec3 normal = cross(second - first, third - first);
      if (!source.has_value() || !entity.has_value() ||
          component == nullptr || !state.has_value() ||
          !unique_sources.emplace(sdk_object_key(*source)).second) {
        return failure("Mirror source object is not live or unique.");
      }
      if (!finite(first) || !finite(normal) ||
          length_squared(normal) <= 0.0000001F) {
        return failure("Mirror plane is degenerate.");
      }
      ModelingObjectSceneIdentity identity;
      static_cast<SceneEntityState &>(identity) = *state;
      prepared.push_back({
          .source = *source,
          .identity = std::move(identity),
          .material_slot = component->material_slot,
          .origin = to_sdk(first),
          .normal = to_sdk(normal),
      });
      sources.push_back(*source);
    }

    const int first_native_candidate = next_available_native_object_id();
    const std::size_t available_native_ids =
        first_native_candidate == kInvalidModelingObjectId
            ? 0u
            : static_cast<std::size_t>(std::numeric_limits<int>::max() -
                                       first_native_candidate);
    if (first_native_candidate == kInvalidModelingObjectId ||
        prepared.size() > available_native_ids) {
      return failure("Native modeling object IDs are exhausted.");
    }
    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(prepared.size());
    for (std::size_t index = 0; index < prepared.size(); ++index) {
      affected_modeling_ids.push_back(first_native_candidate +
                                      static_cast<int>(index));
      const EntityId source_entity = prepared[index].identity.id;
      std::vector<EntityId> subtree =
          scene_.descendants(source_entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
      if (const MeshComponent *mesh = scene_.mesh(source_entity)) {
        affected_assets.push_back(mesh->mesh);
      }
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    const std::optional<int> first_native_id =
        reserve_native_object_ids(prepared.size());
    if (!first_native_id.has_value() ||
        *first_native_id != first_native_candidate) {
      return rollback("Native modeling object IDs could not be reserved.");
    }
    Result<std::vector<ObjectId>> duplicated =
        session_.duplicate_objects(sources, {});
    if (!duplicated.ok() || duplicated.value().size() != prepared.size()) {
      return rollback(duplicated.ok()
                          ? "SDK mirror duplicate batch was incomplete."
                          : duplicated.error_message());
    }
    ++stats_.sdk_duplicate_batch_mutation_count;
    stats_.sdk_duplicate_object_count += duplicated.value().size();

    std::vector<SessionDocumentUpdate> updates;
    updates.reserve(duplicated.value().size());
    for (std::size_t index = 0u; index < duplicated.value().size(); ++index) {
      Result<PolygonDocument> document =
          session_.document_copy(duplicated.value()[index]);
      if (!document.ok()) {
        return rollback(document.error_message());
      }
      Result<OperationResult> reflected = document.value().reflect_across_plane(
          prepared[index].origin, prepared[index].normal);
      if (!reflected.ok() || !reflected.value().success) {
        return rollback(reflected.error_message().empty()
                            ? "Mirror selected meshes failed."
                            : reflected.error_message());
      }
      updates.push_back({.object = duplicated.value()[index],
                         .document = std::move(document).value(),
                         .receipt = std::move(reflected).value()});
    }
    Result<OperationResult> committed =
        session_.commit_documents_batch(std::move(updates), true);
    if (!committed.ok() || !committed.value().success) {
      return rollback(committed.error_message().empty()
                          ? "Mirror selected meshes failed."
                          : committed.error_message());
    }
    for (std::size_t index = 0u; index < duplicated.value().size(); ++index) {
      int native_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      const bool published = publish_new_object(
          &publication, duplicated.value()[index], prepared[index].identity,
          prepared[index].material_slot, native_id, entity, false,
          *first_native_id + static_cast<int>(index));
      if (entity != kInvalidEntityId) {
        publication.track_created_entity(entity);
      }
      if (!published) {
        return rollback("SDK mirror duplicate projection failed.");
      }
      active_native_object_id_ = native_id;
    }
    Result<OperationResult> selected = session_.apply_object_selection(
        duplicated.value(), SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK mirror publication.");
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    if (!add_selection_to_scene()) {
      return rollback("SDK mirror selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true,
         .changed = true,
         .message = "Mirrored selected meshes."});
  }

  [[nodiscard]] ModelingCommandResult
  flip_selected_objects(ModelingFlipOrientation orientation) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Flip works only in object/mesh selection mode.");
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    return mutate_documents(
        selected,
        [orientation](PolygonDocument &document) {
          Result<quader::modeling::AuthoredPolygonPayload> payload =
              document.authored_payload();
          if (!payload.ok()) {
            return Result<OperationResult>::failure(payload.error());
          }
          if (payload.value().positions.empty()) {
            return Result<OperationResult>::failure(quader::modeling::make_error(
                quader::modeling::ErrorCode::ValidationFailed,
                "Flip needs selected mesh bounds."));
          }
          Vec3 minimum = payload.value().positions.front();
          Vec3 maximum = minimum;
          for (Vec3 position : payload.value().positions) {
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
          }
          const Vec3 pivot{(minimum.x + maximum.x) * 0.5F,
                           (minimum.y + maximum.y) * 0.5F,
                           (minimum.z + maximum.z) * 0.5F};
          const Vec3 normal =
              orientation == ModelingFlipOrientation::Horizontal
                  ? Vec3{1.0F, 0.0F, 0.0F}
                  : Vec3{0.0F, 1.0F, 0.0F};
          return document.reflect_across_plane(pivot, normal);
        },
        "Flip selected meshes failed.");
  }

  [[nodiscard]] std::vector<ObjectId> selected_component_objects() const {
    std::vector<ObjectId> selected;
    for (ObjectId object : selected_sdk_objects()) {
      Result<PolygonDocument> document = session_.document_copy(object);
      if (!document.ok()) {
        continue;
      }
      const PolygonSelectionSnapshot selection = document.value().selection();
      const bool has_components =
          selection_mode_ == ModelingSelectionMode::Edge
              ? !selection.edges.empty()
              : selection_mode_ == ModelingSelectionMode::Face
                    ? !selection.faces.empty()
                    : !selection.vertices.empty();
      if (has_components) {
        selected.push_back(object);
      }
    }
    return selected;
  }

  [[nodiscard]] ModelingSelectedVerticesSnapshot
  selected_vertices_snapshot() const {
    if (selection_mode_ != ModelingSelectionMode::Vertex) {
      return {.message =
                  "Snap Vertices to Floor only works in vertex mode."};
    }
    if (preview_active()) {
      return {.message = "Commit or cancel the active preview first."};
    }

    const std::vector<ObjectId> selected = selected_sdk_objects();
    if (selected.empty()) {
      return {.message = "Select one or more vertices first."};
    }
    Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok() || documents.value().size() != selected.size()) {
      return {.message = documents.error_message().empty()
                             ? "Selected vertex documents are unavailable."
                             : documents.error_message()};
    }

    ModelingSelectedVerticesSnapshot result;
    result.objects.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const ObjectId object = selected[index];
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return {.message = "A selected modeling object is no longer live."};
      }

      PolygonDocument &document = documents.value()[index];
      const PolygonSelectionSnapshot selection = document.selection();
      if (selection.vertices.empty()) {
        continue;
      }
      Result<AuthoredTopology> authored = document.authored_topology();
      if (!authored.ok()) {
        return {.message = authored.error_message().empty()
                               ? "Selected vertex geometry is unavailable."
                               : authored.error_message()};
      }
      const std::optional<QMat4> world =
          modeling_object_world_transform_resolver_
              ? modeling_object_world_transform_resolver_(*native)
              : std::optional<QMat4>{scene_.world_transform_matrix(*entity)};
      const std::optional<QMat4> world_inverse =
          world.has_value() ? try_scene_matrix_inverse(*world) : std::nullopt;
      if (!world.has_value() || !world_inverse.has_value()) {
        return {.message =
                    "A selected mesh has a non-invertible world transform."};
      }

      ModelingSelectedVertexObjectSnapshot object_snapshot{
          .modeling_object_id = *native,
          .entity = *entity,
          .world_from_object = *world,
          .object_from_world = *world_inverse,
      };
      object_snapshot.vertices.reserve(selection.vertices.size());
      for (const VertexId selected_vertex : selection.vertices) {
        const auto authored_vertex = std::ranges::find(
            authored.value().vertices, selected_vertex,
            &quader::modeling::AuthoredTopologyVertex::id);
        if (authored_vertex == authored.value().vertices.end()) {
          return {.message = "A selected vertex is no longer live."};
        }
        object_snapshot.vertices.push_back({
            .vertex = selected_vertex,
            .local_position = {authored_vertex->position.x,
                               authored_vertex->position.y,
                               authored_vertex->position.z},
        });
      }
      result.objects.push_back(std::move(object_snapshot));
    }
    if (result.objects.empty()) {
      return {.message = "Select one or more vertices first."};
    }
    result.ok = true;
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_resolved_vertex_positions(
      std::span<const ModelingResolvedVertexPositionBatch> batches) {
    if (selection_mode_ != ModelingSelectionMode::Vertex) {
      return failure("Snap Vertices to Floor only works in vertex mode.");
    }
    if (batches.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "Selected vertices are already on the floor."};
    }

    auto retained =
        std::make_shared<std::vector<ModelingResolvedVertexPositionBatch>>(
            batches.begin(), batches.end());
    auto batch_by_object =
        std::make_shared<std::unordered_map<std::uint64_t, std::size_t>>();
    std::vector<ObjectId> objects;
    objects.reserve(retained->size());
    batch_by_object->reserve(retained->size());
    for (std::size_t index = 0; index < retained->size(); ++index) {
      const ModelingResolvedVertexPositionBatch &batch = (*retained)[index];
      const std::optional<ObjectId> object =
          sdk_object(batch.modeling_object_id);
      if (!object.has_value() || !edit_scope_contains(batch.modeling_object_id) ||
          batch.positions.empty() || !batch_by_object->emplace(sdk_object_key(*object), index).second) {
        return failure(
            "Resolved vertex positions contain an invalid object batch.");
      }
      objects.push_back(*object);
    }

    return mutate_documents_by_object(
        objects,
        [retained, batch_by_object](ObjectId object,
                                    PolygonDocument &document) {
          const auto found = batch_by_object->find(sdk_object_key(object));
          if (found == batch_by_object->end()) {
            return Result<OperationResult>::failure(
                quader::modeling::make_error(
                    quader::modeling::ErrorCode::InvalidId,
                    "Resolved vertex position object is not live."));
          }
          const ModelingResolvedVertexPositionBatch &batch =
              (*retained)[found->second];
          const PolygonSelectionSnapshot selection = document.selection();
          for (const VertexPositionUpdate &update : batch.positions) {
            if (std::ranges::find(selection.vertices, update.vertex) ==
                selection.vertices.end()) {
              return Result<OperationResult>::failure(
                  quader::modeling::make_error(
                      quader::modeling::ErrorCode::ValidationFailed,
                      "Resolved vertex position is not currently selected."));
            }
          }
          return document.set_vertex_positions(batch.positions);
        },
        "Snap vertices to floor failed.", false, true, true, true);
  }

  [[nodiscard]] ModelingCommandResult bridge_selected_components(int steps) {
    if (selection_mode_ != ModelingSelectionMode::Edge &&
        selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Bridge works only in edge or face mode.");
    }
    std::vector<ObjectId> sources = selected_component_objects();
    if (sources.size() != 2U) {
      return failure(selection_mode_ == ModelingSelectionMode::Edge
                         ? "Bridge needs equal selected open-edge counts on "
                           "exactly two meshes."
                         : "Bridge Faces needs selected faces from one mesh "
                           "or two meshes.");
    }
    const std::optional<ObjectId> active = active_sdk_object();
    if (active.has_value()) {
      const auto found = std::ranges::find(sources, *active);
      if (found != sources.end()) {
        std::rotate(sources.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = sources[0];
    const ObjectId merged = sources[1];
    const std::optional<int> survivor_native = native_object(survivor);
    const std::optional<int> merged_native = native_object(merged);
    const std::optional<EntityId> survivor_entity =
        survivor_native.has_value()
            ? object_map_.entity_for_modeling_object(*survivor_native)
            : std::nullopt;
    const std::optional<EntityId> merged_entity =
        merged_native.has_value()
            ? object_map_.entity_for_modeling_object(*merged_native)
            : std::nullopt;
    if (!survivor_native.has_value() || !merged_native.has_value() ||
        !survivor_entity.has_value() || !merged_entity.has_value() ||
        !scene_.exists(*survivor_entity) || !scene_.exists(*merged_entity)) {
      return failure("Bridge source scene identity is not live.");
    }
    const QMat4 survivor_world =
        scene_.world_transform_matrix(*survivor_entity);
    const QMat4 merged_world = scene_.world_transform_matrix(*merged_entity);
    const std::optional<QMat4> survivor_from_world =
        try_scene_matrix_inverse(survivor_world);
    if (!survivor_from_world.has_value()) {
      return failure("Bridge survivor transform is not invertible.");
    }
    const QMat4 survivor_from_merged = scene_transform_multiply(
        *survivor_from_world, merged_world);
    std::vector<EntityId> affected_entities =
        scene_.descendants(*survivor_entity, true);
    std::vector<EntityId> merged_subtree =
        scene_.descendants(*merged_entity, true);
    affected_entities.insert(affected_entities.end(), merged_subtree.begin(),
                             merged_subtree.end());
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    std::vector<AssetId> affected_assets;
    if (const MeshComponent *mesh = scene_.mesh(*survivor_entity)) {
      affected_assets.push_back(mesh->mesh);
    }
    if (const MeshComponent *mesh = scene_.mesh(*merged_entity)) {
      affected_assets.push_back(mesh->mesh);
    }
    const std::array affected_modeling_ids{*survivor_native, *merged_native};
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    Result<OperationResult> bridged = session_.bridge_objects(
        survivor, merged, to_sdk(survivor_from_merged),
        selection_mode_ == ModelingSelectionMode::Face ? SelectionKind::Face
                                                        : SelectionKind::Edge,
        steps);
    if (!bridged.ok() || !bridged.value().success ||
        !bridged.value().changed) {
      return rollback(bridged.ok() ? bridged.value().message
                                   : bridged.error_message());
    }
    const ObjectId survivor_selection_id = survivor;
    Result<OperationResult> selected_survivor = session_.apply_object_selection(
        std::span<const ObjectId>(&survivor_selection_id, 1U),
        SelectionEdit::Replace);
    if (!selected_survivor.ok() ||
        !project_committed_object(publication, survivor)) {
      return rollback(selected_survivor.ok()
                          ? "Bridge survivor projection failed."
                          : selected_survivor.error_message());
    }
    std::vector<AssetId> displaced_meshes;
    if (const MeshComponent *mesh = scene_.mesh(*merged_entity);
        mesh != nullptr && mesh->mesh != kInvalidAssetId) {
      displaced_meshes.push_back(mesh->mesh);
    }
    if (!mesh_sync_.destroy_projected_entity(scene_, *merged_entity)) {
      return rollback("Bridge source scene entity could not be removed.");
    }
    object_map_.unbind_object(*merged_native);
    object_map_.unbind_sdk_object(*merged_native);
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Bridge publication.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    active_native_object_id_ = *survivor_native;
    if (editor_mode_service_ != nullptr) {
      const std::array consumed{*merged_entity};
      editor_mode_service_->remap_participants(consumed, *survivor_entity);
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    if (!add_selection_to_scene()) {
      return rollback("SDK bridge selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true, .changed = true, .message = "Bridged selection."});
  }

  [[nodiscard]] ModelingCommandResult combine_selected_objects() {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Combine Meshes requires object selection mode.");
    }
    std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.size() < 2U) {
      return failure("Select at least two meshes to combine.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const std::optional<ObjectId> active = active_sdk_object();
    if (active.has_value()) {
      const auto found = std::ranges::find(sources, *active);
      if (found != sources.end()) {
        std::rotate(sources.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = sources.front();
    const std::optional<int> survivor_native = native_object(survivor);
    const std::optional<EntityId> survivor_entity =
        survivor_native.has_value()
            ? object_map_.entity_for_modeling_object(*survivor_native)
            : std::nullopt;
    if (!survivor_native.has_value() || !survivor_entity.has_value() ||
        !scene_.exists(*survivor_entity)) {
      return failure("Combine Mesh survivor scene identity is not live.");
    }
    const std::optional<QMat4> survivor_from_world =
        try_scene_matrix_inverse(scene_.world_transform_matrix(
            *survivor_entity));
    if (!survivor_from_world.has_value()) {
      return failure("Combine Mesh survivor transform is not invertible.");
    }
    const auto survivor_sampling = modeling_object_texture_sampling_resolver_
        ? modeling_object_texture_sampling_resolver_(*survivor_native)
        : effective_texture_sampling_from_local(scene_, *survivor_entity);
    if (!survivor_sampling.has_value()) {
      return failure("Combine Mesh survivor texture placement is unavailable.");
    }
    const auto *survivor_material = scene_.material(*survivor_entity);
    MaterialComponent combined_material = survivor_material != nullptr
        ? *survivor_material : MaterialComponent{};
    const MaterialComponent material_before = combined_material;
    const auto survivor_document = session_.document_copy(survivor);
    if (!survivor_document.ok()) return failure(survivor_document.error_message());
    const auto survivor_faces = survivor_document.value().face_ids();
    const auto survivor_settings =
        survivor_document.value().face_texture_settings(survivor_faces);
    if (!survivor_settings.ok()) return failure(survivor_settings.error_message());
    // An unbound nonzero slot still uses the survivor's fallback material.
    // Reserve those live indices before adding bindings from other entities.
    for (const auto &face : survivor_settings.value()) {
      if (face.material_slot != 0U) {
        combined_material.material_slots.try_emplace(
            face.material_slot,
            combined_material.material_for_slot(face.material_slot));
      }
    }

    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(sources.size());
    for (const ObjectId source : sources) {
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return failure("Combine Mesh source scene identity is not live.");
      }
      affected_modeling_ids.push_back(*native);
      std::vector<EntityId> subtree = scene_.descendants(*entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
      if (const MeshComponent *mesh = scene_.mesh(*entity)) {
        affected_assets.push_back(mesh->mesh);
      }
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    for (ObjectId source : sources) {
      if (source == survivor) {
        continue;
      }
      const std::optional<int> source_native = native_object(source);
      const std::optional<EntityId> source_entity =
          source_native.has_value()
              ? object_map_.entity_for_modeling_object(*source_native)
              : std::nullopt;
      if (!source_native.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity)) {
        return rollback("Combine Mesh source scene identity is not live.");
      }
      const QMat4 survivor_from_object = scene_transform_multiply(
          *survivor_from_world,
          scene_.world_transform_matrix(*source_entity));
      Result<PolygonDocument> copied = session_.document_copy(source);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      const auto source_sampling = modeling_object_texture_sampling_resolver_
          ? modeling_object_texture_sampling_resolver_(*source_native)
          : effective_texture_sampling_from_local(scene_, *source_entity);
      if (!source_sampling.has_value()) {
        return rollback("Combine Mesh source texture placement is unavailable.");
      }
      const bool rebase_textures =
          survivor_from_object.values != QMat4{}.values ||
          source_sampling->values != survivor_sampling->values;
      std::optional<quader_poly::Document> source_authored;
      if (rebase_textures) {
        source_authored = MeshProjectionService::polygon_document_from_sdk_document(
            copied.value());
        if (!source_authored.has_value()) {
          return rollback("Combine Mesh source texture topology is unavailable.");
        }
      }
      PolygonDocument working = std::move(copied).value();
      const auto dormant = working.selection();
      Result<OperationResult> selected = working.select_all_faces();
      if (!selected.ok()) {
        return rollback(selected.error_message());
      }
      Result<OperationResult> transformed =
          working.transform_selection(to_sdk(survivor_from_object));
      if (!transformed.ok()) {
        return rollback(transformed.error_message());
      }
      if (source_authored.has_value()) {
        const bool reflected_geometry =
            glm::determinant(glm::mat3{to_glm(survivor_from_object)}) < 0.0F;
        auto preserved =
            MeshProjectionService::rebase_sdk_document_texture_mapping(
                *source_authored, working, *source_sampling, *survivor_sampling,
                reflected_geometry);
        if (!preserved.ok()) {
          return rollback(preserved.error_message());
        }
        working = std::move(preserved).value();
      }
      // Face slots are local to each scene entity. Preserve every source's
      // resolved material while its table is folded into the survivor's table.
      const MaterialComponent *source_material = scene_.material(*source_entity);
      const auto source_faces = working.face_ids();
      const auto settings = working.face_texture_settings(source_faces);
      if (!settings.ok()) return rollback(settings.error_message());
      for (const auto &face : settings.value()) {
        const MaterialReference material = source_material != nullptr
            ? source_material->material_for_slot(face.material_slot)
            : MaterialReference{};
        std::uint32_t slot = 0U;
        try {
          slot = stage_scene_material_slot(combined_material, material);
        } catch (const std::overflow_error &error) {
          return rollback(error.what());
        }
        if (slot != face.material_slot) {
          const auto assigned = working.assign_face_material_slot(face.face, slot);
          if (!assigned.ok()) return rollback(assigned.error_message());
        }
      }
      auto cleared = restore_dormant_selection(working, dormant, transformed.value());
      if (!cleared.ok()) {
        return rollback(cleared.error_message());
      }
      OperationResult receipt = std::move(transformed).value();
      receipt.success = true;
      receipt.changed = true;
      receipt.dirty.topology = true;
      receipt.dirty.geometry = true;
      receipt.dirty.selection = false;
      Result<OperationResult> committed = session_.commit_document(
          source, std::move(working), std::move(receipt));
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
    }

    // The public combine receipt lists created vertices/faces in operand
    // topology order. Retain each dormant component selection while those
    // source identities are remapped into the surviving document.
    struct DormantCombineSource final {
      PolygonSelectionSnapshot selection;
      AuthoredTopology topology;
    };
    std::vector<DormantCombineSource> dormant_sources;
    dormant_sources.reserve(sources.size());
    for (const ObjectId source : sources) {
      auto document = session_.document_copy(source);
      if (!document.ok()) {
        return rollback(document.error_message());
      }
      if (editor_mode_service_ != nullptr) {
        const auto converted = document.value().set_selection_mode(
            to_sdk_component_mode(editor_mode_service_->snapshot().component_kind));
        if (!converted.ok()) {
          return rollback(converted.error_message());
        }
      }
      auto topology = document.value().authored_topology();
      if (!topology.ok()) {
        return rollback(topology.error_message());
      }
      dormant_sources.push_back({document.value().selection(), std::move(topology).value()});
    }
    Result<OperationResult> combined = session_.combine_objects(sources);
    if (!combined.ok() || !combined.value().success ||
        !combined.value().changed) {
      return rollback(combined.ok() ? combined.value().message
                                    : combined.error_message());
    }
    PolygonSelectionSnapshot dormant = dormant_sources.front().selection;
    std::size_t vertex_offset = 0u;
    std::size_t face_offset = 0u;
    for (std::size_t index = 1u; index < dormant_sources.size(); ++index) {
      const auto &source = dormant_sources[index];
      if (vertex_offset + source.topology.vertices.size() > combined.value().created.vertices.size() ||
          face_offset + source.topology.faces.size() > combined.value().created.faces.size()) {
        return rollback("Combine Mesh component identity remap is incomplete.");
      }
      std::unordered_map<std::uint32_t, VertexId> vertices;
      std::unordered_map<std::uint32_t, FaceId> faces;
      for (const auto &vertex : source.topology.vertices) {
        vertices.emplace(vertex.id.index, combined.value().created.vertices[vertex_offset++]);
      }
      for (const auto &face : source.topology.faces) {
        faces.emplace(face.id.index, combined.value().created.faces[face_offset++]);
      }
      for (const VertexId vertex : source.selection.vertices) {
        if (const auto mapped = vertices.find(vertex.index); mapped != vertices.end()) {
          dormant.vertices.push_back(mapped->second);
        }
      }
      for (const EdgeKey edge : source.selection.edges) {
        const auto first = vertices.find(edge.a.index);
        const auto second = vertices.find(edge.b.index);
        if (first != vertices.end() && second != vertices.end()) {
          dormant.edges.push_back(modeling::make_edge_key(first->second, second->second));
        }
      }
      for (const FaceId face : source.selection.faces) {
        if (const auto mapped = faces.find(face.index); mapped != faces.end()) {
          dormant.faces.push_back(mapped->second);
        }
      }
    }
    auto combined_document = session_.document_copy(survivor);
    if (!combined_document.ok()) {
      return rollback(combined_document.error_message());
    }
    const auto restored = restore_dormant_selection(combined_document.value(),
                                                   dormant, OperationResult{});
    if (!restored.ok()) {
      return rollback(restored.error_message());
    }
    const std::array selection_updates{modeling::SessionComponentSelectionUpdate{
        .object = survivor, .selection = combined_document.value().selection()}};
    const std::array selected_survivor{survivor};
    const auto selected = session_.commit_component_selection_batch(
        selection_updates, selected_survivor, survivor);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (combined_material != material_before &&
        !mesh_sync_.set_projected_material(scene_, *survivor_entity,
                                           &combined_material)) {
      return rollback("Combine Mesh material bindings could not be published.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_committed_object(publication, survivor)) {
      return rollback("SDK Combine Mesh projection failed.");
    }

    std::vector<AssetId> displaced_meshes;
    for (ObjectId source : sources) {
      if (source == survivor) {
        continue;
      }
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value()) {
        return rollback("Combined source projection mapping was lost.");
      }
      if (const MeshComponent *mesh = scene_.mesh(*entity);
          mesh != nullptr && mesh->mesh != kInvalidAssetId) {
        displaced_meshes.push_back(mesh->mesh);
      }
      if (!mesh_sync_.destroy_projected_entity(scene_, *entity)) {
        return rollback("Combined source scene entity could not be removed.");
      }
      object_map_.unbind_object(*native);
      object_map_.unbind_sdk_object(*native);
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK Combine Mesh commit.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    active_native_object_id_ = *survivor_native;
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    if (!add_selection_to_scene()) {
      return rollback("SDK combine selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true, .changed = true, .message = combined.value().message});
  }

  [[nodiscard]] ModelingCommandResult extract_selected_faces() {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Extract Faces only works in face mode.");
    }
    const std::optional<ObjectId> source = active_sdk_object();
    const std::optional<int> source_native =
        source.has_value() ? native_object(*source) : std::nullopt;
    const std::optional<EntityId> source_entity =
        source_native.has_value()
            ? object_map_.entity_for_modeling_object(*source_native)
            : std::nullopt;
    if (!source.has_value() || !source_native.has_value() ||
        !source_entity.has_value() || !scene_.exists(*source_entity)) {
      return failure("Select faces before extracting.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    object_map_.remember_scene_entity(*source_native, scene_, *source_entity);
    const std::optional<ModelingObjectSceneIdentity> source_identity =
        object_map_.scene_identity_for_modeling_object(*source_native);
    const ModelingObjectComponent *source_component =
        scene_.modeling_object(*source_entity);
    if (!source_identity.has_value() || source_component == nullptr) {
      return failure("Extract Faces source scene identity is incomplete.");
    }
    const int extracted_native_candidate = next_available_native_object_id();
    if (extracted_native_candidate == kInvalidModelingObjectId ||
        extracted_native_candidate == std::numeric_limits<int>::max()) {
      return failure("Native modeling object IDs are exhausted.");
    }
    std::vector<EntityId> affected_entities =
        scene_.descendants(*source_entity, true);
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }
    std::vector<AssetId> affected_assets;
    if (const MeshComponent *mesh = scene_.mesh(*source_entity)) {
      affected_assets.push_back(mesh->mesh);
    }
    const std::array affected_modeling_ids{*source_native,
                                           extracted_native_candidate};

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    Result<PolygonDocument> copied = session_.document_copy(*source);
    if (!copied.ok()) {
      return rollback(copied.error_message());
    }
    PolygonDocument working = std::move(copied).value();
    Result<quader::modeling::ExtractedPolygonDocument> extracted =
        working.extract_selected_faces();
    if (!extracted.ok() || !extracted.value().receipt.success ||
        !extracted.value().receipt.changed) {
      return rollback(extracted.ok() ? extracted.value().receipt.message
                                     : extracted.error_message());
    }
    const bool remove_empty_source = working.face_count() == 0U;
    OperationResult source_receipt = extracted.value().receipt;
    Result<OperationResult> committed = session_.commit_document(
        *source, std::move(working), std::move(source_receipt));
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    Result<ObjectId> added = session_.add_document(
        std::move(extracted).value().document, "Extracted Faces");
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    if (remove_empty_source) {
      Result<OperationResult> removed = session_.remove_object(*source);
      if (!removed.ok()) {
        return rollback(removed.error_message());
      }
    }
    const ObjectId extracted_id = added.value();
    Result<OperationResult> selected =
        session_.select_object(extracted_id);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }

    Result<PolygonDocument> extracted_document =
        session_.document_copy(extracted_id);
    if (!extracted_document.ok()) {
      return rollback(extracted_document.error_message());
    }
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(
            extracted_document.value());
    if (!projected.has_value()) {
      return rollback("SDK extracted mesh projection failed.");
    }
    projected->name = source_identity->name;
    const AssetId extracted_mesh =
        mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
    if (extracted_mesh == kInvalidAssetId) {
      return rollback("SDK extracted mesh asset could not be installed.");
    }
    publication.track_created_asset(extracted_mesh);
    ++stats_.asset_publication_count;
    const EntityId extracted_entity = mesh_sync_.create_projected_mesh_entity(
        scene_, source_identity->name);
    if (extracted_entity == kInvalidEntityId) {
      return rollback("SDK extracted scene entity could not be created.");
    }
    publication.track_created_entity(extracted_entity);
    const int extracted_native = next_available_native_object_id();
    if (extracted_native == kInvalidModelingObjectId ||
        extracted_native == std::numeric_limits<int>::max() ||
        extracted_native != extracted_native_candidate ||
        !mesh_sync_.bind_projected_object(
            scene_, extracted_entity, extracted_native,
            source_component->material_slot, true, extracted_mesh,
            identity_local_frame_version(*source_identity))) {
      return rollback("SDK extracted scene binding could not be installed.");
    }
    if (!mesh_sync_.apply_scene_identity_to_duplicate_entity(
            scene_, extracted_entity, *source_identity)) {
      return rollback("SDK extracted scene identity could not be applied.");
    }
    if (!object_map_.bind_sdk_object(extracted_native, extracted_id) ||
        !object_map_.bind(extracted_native, extracted_entity)) {
      return rollback("SDK extracted identity binding could not be installed.");
    }
    object_map_.remember_scene_entity(extracted_native, scene_,
                                      extracted_entity);
    next_native_object_id_ = extracted_native + 1;
    active_native_object_id_ = extracted_native;

    std::vector<AssetId> displaced_meshes;
    if (remove_empty_source) {
      if (const MeshComponent *mesh = scene_.mesh(*source_entity);
          mesh != nullptr && mesh->mesh != kInvalidAssetId) {
        displaced_meshes.push_back(mesh->mesh);
      }
      if (!mesh_sync_.destroy_projected_entity(scene_, *source_entity)) {
        return rollback("Empty extraction source entity could not be removed.");
      }
      object_map_.unbind_object(*source_native);
      object_map_.unbind_sdk_object(*source_native);
    } else if (!project_committed_object(publication, *source)) {
      return rollback("SDK extraction source projection failed.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Extract Faces commit.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    if (editor_mode_service_ != nullptr) {
      editor_mode_service_->register_object_result(extracted_entity);
    }
    if (!add_selection_to_scene()) {
      return rollback("SDK extract selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true,
         .changed = true,
         .message = "Extracted selected faces."});
  }

  [[nodiscard]] ModelingCommandResult plane_cut(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode, bool select_results) {
    using Scope = quader_editor_core::CutScope;
    if (requests.empty()) return failure("Cut needs a selected mesh.");
    if (preview_active()) return failure("Commit or cancel the active preview first.");
    if (std::ranges::any_of(requests, [&](const auto &request) {
          return request.scope != requests.front().scope;
        })) return failure("Cut cannot mix component scopes.");
    const auto prepared = preview_plane_cut(requests, mode);
    if (!prepared.command.ok) return prepared.command;
    // These pins are exactly the documents accepted by the preview. Scene and
    // asset publication below cannot change the inputs to a later participant.
    auto candidates = plane_cut_preview_cache_.candidates;
    const bool changed = std::ranges::any_of(candidates, [](const auto &candidate) {
      return candidate.receipt.changed || candidate.second.has_value();
    });
    if (!changed) {
      clear_plane_cut_preview();
      return {.ok = true, .changed = false, .message = "Cut did not cross the selected geometry."};
    }
    const std::size_t duplicate_count = static_cast<std::size_t>(
        std::ranges::count_if(candidates, [](const auto &candidate) {
          return candidate.second.has_value();
        }));
    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(requests.size() + duplicate_count);
    for (const auto &request : requests) {
      const auto entity = object_map_.entity_for_modeling_object(request.modeling_object_id);
      if (!entity.has_value() || !scene_.exists(*entity)) return failure("Cut target mesh was not found.");
      affected_modeling_ids.push_back(request.modeling_object_id);
      const auto subtree = scene_.descendants(*entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(), subtree.end());
      if (const auto *mesh = scene_.mesh(*entity)) affected_assets.push_back(mesh->mesh);
    }
    std::optional<int> first_back_native_candidate;
    if (duplicate_count != 0U) {
      const int first = next_available_native_object_id();
      const auto available = first == kInvalidModelingObjectId ? 0U
          : static_cast<std::size_t>(std::numeric_limits<int>::max() - first);
      if (first == kInvalidModelingObjectId || duplicate_count > available) {
        return failure("Native modeling object IDs are exhausted.");
      }
      first_back_native_candidate = first;
      for (std::size_t index = 0; index < duplicate_count; ++index) {
        affected_modeling_ids.push_back(first + static_cast<int>(index));
      }
    }
    for (const auto selected : scene_.selected_entities()) {
      const auto subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(), subtree.end());
    }
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope = ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = duplicate_count != 0U,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) return failure("Host publication checkpoint is unavailable.");
    const auto undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication, failure(std::move(message)));
    };
    std::optional<int> first_back_native_id;
    if (duplicate_count != 0U) {
      first_back_native_id = reserve_native_object_ids(duplicate_count);
      if (first_back_native_id != first_back_native_candidate) {
        return rollback("Native modeling object IDs could not be reserved.");
      }
    }
    std::vector<ObjectId> result_objects;
    result_objects.reserve(candidates.size() + duplicate_count);
    std::size_t published_back_count = 0U;
    for (auto &candidate : candidates) {
      const auto source_entity = object_map_.entity_for_modeling_object(candidate.native_id);
      if (!source_entity.has_value()) return rollback("Cut source scene identity is missing.");
      object_map_.remember_scene_entity(candidate.native_id, scene_, *source_entity);
      const auto identity = object_map_.scene_identity_for_modeling_object(candidate.native_id);
      const auto *component = scene_.modeling_object(*source_entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Cut target scene identity is incomplete.");
      }
      const auto identity_copy = *identity;
      const auto material_slot = component->material_slot;
      if (!select_results && requests.front().scope == Scope::Object) {
        const auto restored = restore_dormant_selection(candidate.kept,
            candidate.dormant_selection, candidate.receipt);
        if (!restored.ok()) return rollback(restored.error_message());
        if (candidate.second.has_value()) {
          const auto second_restored = restore_dormant_selection(*candidate.second,
              candidate.dormant_selection, candidate.receipt);
          if (!second_restored.ok()) return rollback(second_restored.error_message());
        }
      }
      if (candidate.receipt.changed) {
        const auto committed = session_.commit_document(candidate.object,
            std::move(candidate.kept), std::move(candidate.receipt));
        if (!committed.ok() || !project_committed_object(publication, candidate.object)) {
          return rollback(committed.ok() ? "Cut source projection failed." : committed.error_message());
        }
      }
      result_objects.push_back(candidate.object);
      if (candidate.second.has_value()) {
        auto back = session_.add_document(std::move(*candidate.second), identity_copy.name);
        if (!back.ok()) return rollback(back.error_message());
        int back_native = kInvalidModelingObjectId;
        EntityId back_entity = kInvalidEntityId;
        const bool published = publish_new_object(&publication, back.value(), identity_copy,
            material_slot, back_native, back_entity, false,
            *first_back_native_id + static_cast<int>(published_back_count));
        if (back_entity != kInvalidEntityId) publication.track_created_entity(back_entity);
        if (!published) return rollback("Cut back projection failed.");
        ++published_back_count;
        result_objects.push_back(back.value());
      }
    }
    const std::span<const ObjectId> selection = select_results
        ? std::span<const ObjectId>(result_objects) : std::span<const ObjectId>{};
    const auto selected = session_.apply_object_selection(selection, SelectionEdit::Replace);
    if (!selected.ok()) return rollback(selected.error_message());
    if (select_results) {
      selection_mode_ = requests.front().scope == Scope::Edge
          ? ModelingSelectionMode::Edge : ModelingSelectionMode::Face;
      active_native_object_id_ = native_object(result_objects.back()).value_or(kInvalidModelingObjectId);
    } else {
      active_native_object_id_ = kInvalidModelingObjectId;
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK plane cut publication.");
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    if (!add_selection_to_scene()) return rollback("SDK plane-cut selection could not be published.");
    const auto result = commit_publication_result<ModelingCommandResult>(publication,
        {.ok = true, .changed = true, .message = "Cut selected geometry."});
    if (result.ok) clear_plane_cut_preview();
    return result;
  }

  [[nodiscard]] ModelingCommandResult half_cut_selected_objects(
      quader_editor_core::HalfCutOperationSettings settings) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Half-Cut requires object selection mode.");
    }
    const std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.empty()) {
      return failure("Half-Cut needs one or more selected meshes.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }

    std::vector<int> affected_modeling_ids;
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_modeling_ids.reserve(
        sources.size() * (settings.separate_parts ? 2u : 1u));
    for (const ObjectId source : sources) {
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return failure("Half-Cut source scene identity is not live.");
      }
      affected_modeling_ids.push_back(*native);
      std::vector<EntityId> subtree = scene_.descendants(*entity, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
      if (const MeshComponent *mesh = scene_.mesh(*entity)) {
        affected_assets.push_back(mesh->mesh);
      }
    }
    std::optional<int> first_back_native_candidate;
    if (settings.separate_parts) {
      const int first = next_available_native_object_id();
      const std::size_t available =
          first == kInvalidModelingObjectId
              ? 0u
              : static_cast<std::size_t>(std::numeric_limits<int>::max() -
                                         first);
      if (first == kInvalidModelingObjectId || sources.size() > available) {
        return failure("Native modeling object IDs are exhausted.");
      }
      first_back_native_candidate = first;
      for (std::size_t index = 0; index < sources.size(); ++index) {
        affected_modeling_ids.push_back(first + static_cast<int>(index));
      }
    }
    for (const EntityId selected : scene_.selected_entities()) {
      std::vector<EntityId> subtree = scene_.descendants(selected, true);
      affected_entities.insert(affected_entities.end(), subtree.begin(),
                               subtree.end());
    }

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = settings.separate_parts,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    std::vector<ObjectId> results;
    results.reserve(sources.size() * (settings.separate_parts ? 2U : 1U));
    std::optional<int> first_back_native_id;
    if (settings.separate_parts) {
      first_back_native_id = reserve_native_object_ids(sources.size());
      if (first_back_native_id != first_back_native_candidate) {
        return rollback("Native modeling object IDs could not be reserved.");
      }
    }
    std::size_t published_back_count = 0U;
    for (ObjectId source : sources) {
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return rollback("Half-Cut source scene identity is not live.");
      }
      object_map_.remember_scene_entity(*native, scene_, *entity);
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_map_.scene_identity_for_modeling_object(*native);
      const ModelingObjectComponent *component =
          scene_.modeling_object(*entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Half-Cut source scene identity is incomplete.");
      }
      const ModelingObjectSceneIdentity identity_copy = *identity;
      const std::uint32_t material_slot = component->material_slot;

      Result<PolygonDocument> copied = session_.document_copy(source);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      PolygonDocument working = std::move(copied).value();
      Result<AuthoredTopology> topology = working.authored_topology();
      if (!topology.ok() || topology.value().vertices.empty()) {
        return rollback("Half-Cut target mesh has no vertices.");
      }
      quader::modeling::Vec3 minimum =
          topology.value().vertices.front().position;
      quader::modeling::Vec3 maximum = minimum;
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           topology.value().vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
      }
      const quader::modeling::Vec3 center{
          (minimum.x + maximum.x) * 0.5F,
          (minimum.y + maximum.y) * 0.5F,
          (minimum.z + maximum.z) * 0.5F,
      };
      quader::modeling::PlaneSplitSpec split{
          .a = center,
          .cap = settings.fill_parts,
      };
      switch (settings.axis) {
      case quader_editor_core::HalfCutAxis::X:
        split.b = {center.x, center.y + 1.0F, center.z};
        split.c = {center.x, center.y, center.z + 1.0F};
        break;
      case quader_editor_core::HalfCutAxis::Y:
        split.b = {center.x, center.y, center.z + 1.0F};
        split.c = {center.x + 1.0F, center.y, center.z};
        break;
      case quader_editor_core::HalfCutAxis::Z:
        split.b = {center.x + 1.0F, center.y, center.z};
        split.c = {center.x, center.y + 1.0F, center.z};
        break;
      }

      const auto dormant = working.selection();
      if (!settings.separate_parts) {
        Result<OperationResult> cut = working.cut_surface_by_plane(split);
        if (!cut.ok() || !cut.value().success || !cut.value().changed) {
          return rollback(cut.ok() ? cut.value().message
                                   : cut.error_message());
        }
        auto cleared = restore_dormant_selection(working, dormant, cut.value());
        if (!cleared.ok()) {
          return rollback(cleared.error_message());
        }
        Result<OperationResult> committed = session_.commit_document(
            source, std::move(working), std::move(cut).value());
        if (!committed.ok()) {
          return rollback(committed.error_message());
        }
        if (!project_committed_object(publication, source)) {
          return rollback("Half-Cut source projection failed.");
        }
        results.push_back(source);
        continue;
      }

      Result<quader::modeling::SplitPolygonDocuments> cut =
          working.split_by_plane(split);
      if (!cut.ok()) {
        return rollback(cut.error_message());
      }
      auto cleared_front = restore_dormant_selection(
          cut.value().front, dormant, cut.value().receipt);
      auto cleared_back = restore_dormant_selection(
          cut.value().back, dormant, cut.value().receipt);
      if (!cleared_front.ok() || !cleared_back.ok()) {
        return rollback(!cleared_front.ok() ? cleared_front.error_message()
                                            : cleared_back.error_message());
      }
      OperationResult front_receipt = cut.value().receipt;
      Result<OperationResult> front = session_.commit_document(
          source, std::move(cut.value().front), std::move(front_receipt));
      if (!front.ok()) {
        return rollback(front.error_message());
      }
      Result<ObjectId> back = session_.add_document(
          std::move(cut).value().back, identity_copy.name);
      if (!back.ok()) {
        return rollback(back.error_message());
      }
      if (!project_committed_object(publication, source)) {
        return rollback("Half-Cut front projection failed.");
      }
      int back_native = kInvalidModelingObjectId;
      EntityId back_entity = kInvalidEntityId;
      const bool published = publish_new_object(
          &publication, back.value(), identity_copy, material_slot, back_native,
          back_entity, false,
          *first_back_native_id + static_cast<int>(published_back_count));
      if (back_entity != kInvalidEntityId) {
        publication.track_created_entity(back_entity);
      }
      if (!published) {
        return rollback("Half-Cut back projection failed.");
      }
      ++published_back_count;
      results.push_back(source);
      results.push_back(back.value());
    }

    Result<OperationResult> selected =
        session_.apply_object_selection(results, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (const std::optional<ObjectId> active = active_sdk_object();
        !active.has_value() ||
        std::ranges::find(results, *active) == results.end()) {
      const std::optional<int> native = native_object(results.back());
      active_native_object_id_ =
          native.value_or(kInvalidModelingObjectId);
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Half-Cut commit.");
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    if (!add_selection_to_scene()) {
      return rollback("SDK half-cut selection could not be published.");
    }
    return commit_publication_result<ModelingCommandResult>(
        publication, {.ok = true, .changed = true, .message = "Half-Cut"});
  }

  [[nodiscard]] ModelingCommandResult mutate_documents_by_object(
      std::span<const ObjectId> object_ids,
      const DocumentMutationByObject &mutation,
      std::string_view fallback_message,
      bool preview_publication = false,
      bool reproject_all_objects = false,
      bool position_only_projection = false,
      bool preserve_object_selection = false,
      bool preserve_scene_selection = false,
      const std::function<ModelingCommandResult()> &publication_finalizer = {}) {
    if (object_ids.empty()) {
      return failure("Select a modeling mesh or component first.");
    }
    if (preview_active() && !preview_publication) {
      return failure("Commit or cancel the active preview first.");
    }

    std::vector<ObjectId> affected_sdk_objects(object_ids.begin(),
                                                object_ids.end());
    std::vector<EntityId> affected_entities;
    std::vector<AssetId> affected_assets;
    affected_entities.reserve(object_ids.size());
    affected_assets.reserve(object_ids.size());
    for (const ObjectId object : object_ids) {
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return failure("A modeling publication participant is not live.");
      }
      affected_entities.push_back(*entity);
      if (const MeshComponent *mesh = scene_.mesh(*entity)) {
        affected_assets.push_back(mesh->mesh);
      }
      if (const CurveComponent *curve = scene_.curve(*entity)) {
        affected_assets.push_back(curve->curve);
      }
    }
    if (!preview_publication && !preserve_object_selection) {
      const std::optional<ObjectId> active = active_sdk_object();
      if (active.has_value()) {
        affected_sdk_objects.push_back(*active);
      }
    }
    if (!preview_publication && publication_finalizer) {
      for (const modeling::SessionObjectSummary &object : session_.objects()) {
        if (object.selected) {
          affected_sdk_objects.push_back(object.id);
        }
      }
    }
    if (!preserve_scene_selection) {
      const std::vector<EntityId> selected_entities =
          scene_.selected_entities();
      affected_entities.insert(affected_entities.end(),
                               selected_entities.begin(),
                               selected_entities.end());
      for (const modeling::SessionObjectSummary &object : session_.objects()) {
        if (!object.selected) {
          continue;
        }
        const std::optional<int> native = native_object(object.id);
        const std::optional<EntityId> entity =
            native.has_value()
                ? object_map_.entity_for_modeling_object(*native)
                : std::nullopt;
        if (entity.has_value()) {
          affected_entities.push_back(*entity);
        }
      }
    }
    std::ranges::sort(affected_sdk_objects);
    affected_sdk_objects.erase(
        std::unique(affected_sdk_objects.begin(),
                    affected_sdk_objects.end()),
        affected_sdk_objects.end());
    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = preview_publication
                             ? ModelingSdkRollbackPolicy::PreviewCheckpoint
                             : ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .capture_scene_selection = !preserve_scene_selection,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_sdk_object_ids = affected_sdk_objects});
    if (!publication.ready()) {
      return failure("Host publication checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();

    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };

    bool changed = false;
    std::size_t deleted_vertex_count = 0U;
    std::string message;
    std::vector<SessionDocumentUpdate> updates;
    updates.reserve(object_ids.size());
    std::vector<std::pair<ObjectId, PolygonDocument>> projection_documents;
    projection_documents.reserve(reproject_all_objects ? object_ids.size()
                                                        : 0U);
    Result<std::vector<PolygonDocument>> copied_documents =
        session_.document_copies(object_ids);
    if (!copied_documents.ok()) {
      return rollback(copied_documents.error_message());
    }
    for (std::size_t object_index = 0; object_index < object_ids.size();
         ++object_index) {
      const ObjectId object_id = object_ids[object_index];
      PolygonDocument working =
          std::move(copied_documents.value()[object_index]);
      Result<OperationResult> operated = mutation(object_id, working);
      if (!operated.ok()) {
        return rollback(operated.error_message().empty()
                            ? std::string(fallback_message)
                            : operated.error_message());
      }
      if (!operated.value().success) {
        return rollback(operated.value().message.empty()
                            ? std::string(fallback_message)
                            : operated.value().message);
      }
      message = operated.value().message;
      deleted_vertex_count += operated.value().deleted.vertices.size();
      if (reproject_all_objects) {
        projection_documents.emplace_back(object_id, working);
      }
      if (!operated.value().changed) {
        continue;
      }
      if (!reproject_all_objects) {
        projection_documents.emplace_back(object_id, working);
      }
      updates.push_back({.object = object_id,
                         .document = std::move(working),
                         .receipt = std::move(operated).value()});
    }

    if (!updates.empty()) {
      QDR_PROFILE_SCOPE("Modeling SDK commit documents atomic batch");
      Result<OperationResult> committed =
          session_.commit_documents_batch(std::move(updates),
                                          preserve_object_selection);
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
      changed = committed.value().changed;
    }

    if (!changed) {
      if (preview_publication && !reproject_all_objects) {
        active_preview_deleted_vertex_count_ = deleted_vertex_count;
      }
      if (!reproject_all_objects) {
        return rollback_publication_result<ModelingCommandResult>(
            publication,
            {.ok = true,
             .changed = false,
             .message = message.empty() ? std::string(fallback_message)
                                        : std::move(message)});
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK operation mutation.");
    }
    {
      QDR_PROFILE_SCOPE("Modeling SDK project document batch");
      std::vector<std::optional<MeshAsset>> compiled_documents;
      if (reproject_all_objects &&
          (position_only_projection || projection_documents.size() >= 8u)) {
        QDR_PROFILE_SCOPE("Modeling SDK parallel compile document batch");
        compiled_documents.resize(projection_documents.size());
        std::vector<std::optional<MeshAsset>> retained_meshes;
        if (position_only_projection) {
          QDR_PROFILE_SCOPE(
              "Transform commit collect retained position-only meshes");
          retained_meshes.resize(projection_documents.size());
          for (std::size_t index = 0; index < projection_documents.size();
               ++index) {
            const std::optional<int> native =
                native_object(projection_documents[index].first);
            const std::optional<EntityId> entity =
                native.has_value()
                    ? object_map_.entity_for_modeling_object(*native)
                    : std::nullopt;
            const MeshComponent *binding =
                entity.has_value() ? scene_.mesh(*entity) : nullptr;
            const MeshAsset *current =
                binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
            if (current != nullptr) {
              retained_meshes[index] = *current;
            }
          }
        }
        std::atomic_size_t next_document{0u};
        std::atomic_size_t full_compile_fallbacks{0u};
        const std::size_t worker_count = std::min<std::size_t>(
            projection_documents.size(),
            std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1u,
                                    8u));
        const auto compile_next = [&]() {
          for (;;) {
            const std::size_t index =
                next_document.fetch_add(1u, std::memory_order_relaxed);
            if (index >= projection_documents.size()) {
              return;
            }
            if (!retained_meshes.empty() &&
                retained_meshes[index].has_value()) {
              QDR_PROFILE_SCOPE(
                  "Transform commit update retained mesh positions");
              compiled_documents[index] =
                  MeshProjectionService::update_sdk_document_mesh_positions(
                      projection_documents[index].second,
                      *retained_meshes[index]);
            }
            if (!compiled_documents[index].has_value()) {
              QDR_PROFILE_SCOPE("Transform commit full mesh compile fallback");
              full_compile_fallbacks.fetch_add(1u,
                                               std::memory_order_relaxed);
              compiled_documents[index] =
                  MeshProjectionService::compile_sdk_document_mesh(
                      projection_documents[index].second);
            }
          }
        };
        std::vector<std::jthread> workers;
        workers.reserve(worker_count - 1u);
        for (std::size_t worker = 1u; worker < worker_count; ++worker) {
          workers.emplace_back(compile_next);
        }
        compile_next();
        for (std::jthread &worker : workers) {
          worker.join();
        }
        stats_.mesh_compile_count +=
            full_compile_fallbacks.load(std::memory_order_relaxed);
        if (std::ranges::any_of(compiled_documents,
                                [](const auto &compiled) {
                                  return !compiled.has_value();
                                })) {
          return rollback("SDK operation mesh compilation failed.");
        }
      }
      for (std::size_t index = 0; index < projection_documents.size();
           ++index) {
        const auto &[object_id, document] = projection_documents[index];
        std::optional<MeshAsset> compiled =
            compiled_documents.empty()
                ? std::optional<MeshAsset>{}
                : std::move(compiled_documents[index]);
        if (!project_document(&publication, object_id, document,
                              preview_publication,
                              reproject_all_objects
                                  ? std::optional<bool>{true}
                                  : std::nullopt,
                              std::move(compiled))) {
          return rollback("SDK operation host projection failed.");
        }
      }
    }
    if (projection_documents.empty() && changed) {
      for (ObjectId object_id : object_ids) {
        Result<PolygonDocument> projected = session_.document_copy(object_id);
        if (!projected.ok() ||
            !project_document(&publication, object_id, projected.value(),
                              preview_publication)) {
          return rollback("SDK operation host projection failed.");
        }
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK operation commit.");
    }
    if (publication_finalizer) {
      ModelingCommandResult finalized = publication_finalizer();
      if (!finalized.ok) {
        return rollback(finalized.message.empty()
                            ? "SDK operation final publication failed."
                            : std::move(finalized.message));
      }
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    } else {
      active_preview_deleted_vertex_count_ = deleted_vertex_count;
    }
    if (!preserve_scene_selection) {
      if (!add_selection_to_scene()) {
        return rollback("SDK operation selection could not be published.");
      }
    }
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true,
         .changed = changed,
         .message = message.empty() ? std::string(fallback_message)
                                    : std::move(message)});
  }

  /**
   * Commits a topology-stable transform after all document mutations,
   * compilation, target lookup, and failure preflight have succeeded. The
   * durable SDK/asset/scene owner batches share the host publication
   * transaction; immutable mesh payloads keep its asset checkpoint shallow.
   */
  [[nodiscard]] ModelingCommandResult commit_sparse_transform_positions(
      const std::shared_ptr<const ModelingSdkTransformHistoryDelta::Impl>
          &exact_positions,
      bool use_after = true) {
    QDR_PROFILE_FUNCTION();
    if (!exact_positions || exact_positions->objects.empty()) {
      return failure("Transform commit has no authored position changes.");
    }

    std::vector<ObjectId> objects;
    objects.reserve(exact_positions->objects.size());
    auto exact_index =
        std::make_shared<std::unordered_map<std::uint64_t, std::size_t>>();
    exact_index->reserve(exact_positions->objects.size());
    for (std::size_t index = 0; index < exact_positions->objects.size();
         ++index) {
      const auto &entry = exact_positions->objects[index];
      objects.push_back(entry.object);
      exact_index->emplace(sdk_object_key(entry.object), index);
    }

    const auto exact_mutation =
        [exact_positions, exact_index,
         use_after](ObjectId object, PolygonDocument &document) {
          const auto found = exact_index->find(sdk_object_key(object));
          if (found == exact_index->end()) {
            return Result<OperationResult>::failure(
                quader::modeling::make_error(
                    quader::modeling::ErrorCode::InvalidId,
                    "Transform commit object is not live."));
          }
          const auto &entry = exact_positions->objects[found->second];
          const auto &topology =
              use_after ? entry.topology_after : entry.topology_before;
          if (topology.has_value()) {
            PolygonDocument restored_document = *topology;
            restored_document.preserve_identity_high_water(document);
            document = std::move(restored_document);
            OperationResult restored;
            restored.success = true;
            restored.changed = true;
            restored.dirty = {.topology = true,
                              .geometry = true,
                              .selection = true,
                              .materials = true,
                              .overlays = true};
            return Result<OperationResult>::success(std::move(restored));
          }
          return document.set_vertex_positions(use_after ? entry.after
                                                         : entry.before);
        };

    if (std::ranges::any_of(exact_positions->objects, [](const auto &entry) {
          return entry.topology_before.has_value();
        })) {
      return mutate_documents_by_object(
          objects, exact_mutation, "Transform document history failed.", false,
          false, false, true, true);
    }

    // Shared mesh bindings need the normal detach/rollback machinery. Keep
    // that uncommon path correct while avoiding whole-project checkpoints for
    // the common independently-authored modeling mesh case.
    if (!preview_targets_have_unique_mesh_bindings(objects)) {
      QDR_PROFILE_SCOPE("Transform commit shared-mesh safe fallback");
      return mutate_documents_by_object(
          objects, exact_mutation, "Transform commit failed.", false, false,
          true, true);
    }

    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return failure("Injected failure after SDK operation mutation.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return failure("Injected failure before SDK operation commit.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::CommittedAssetReplace) ||
        fails(ModelingSdkProjectionFailurePoint::CommittedSceneNotify)) {
      return failure("SDK operation host projection failed.");
    }

    Result<std::vector<PolygonDocument>> copied_documents =
        session_.document_copies(objects);
    if (!copied_documents.ok() ||
        copied_documents.value().size() != objects.size()) {
      return failure(copied_documents.ok()
                         ? "Transform commit document batch is incomplete."
                         : copied_documents.error_message());
    }

    std::vector<SessionDocumentUpdate> updates;
    std::vector<std::pair<ObjectId, PolygonDocument>> projection_documents;
    updates.reserve(objects.size());
    projection_documents.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      PolygonDocument working =
          std::move(copied_documents.value()[index]);
      Result<OperationResult> operated =
          exact_mutation(objects[index], working);
      if (!operated.ok() || !operated.value().success) {
        return failure(operated.error_message().empty()
                           ? "Transform commit failed."
                           : operated.error_message());
      }
      if (!operated.value().changed) {
        continue;
      }
      projection_documents.emplace_back(objects[index], working);
      updates.push_back({.object = objects[index],
                         .document = std::move(working),
                         .receipt = std::move(operated).value()});
    }
    if (updates.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "Transform commit unchanged."};
    }

    // Pin affected immutable mesh payloads for compilation and validation.
    std::vector<const MeshAsset *> retained_meshes;
    std::vector<EntityId> target_entities;
    std::vector<AssetId> target_mesh_ids;
    retained_meshes.reserve(projection_documents.size());
    target_entities.reserve(projection_documents.size());
    target_mesh_ids.reserve(projection_documents.size());
    for (const auto &[object, document] : projection_documents) {
      (void)document;
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *current =
          binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
      if (!entity.has_value() || !scene_.exists(*entity) ||
          binding == nullptr || current == nullptr) {
        return failure("Transform commit host target is not live.");
      }
      retained_meshes.push_back(current);
      target_entities.push_back(*entity);
      target_mesh_ids.push_back(binding->mesh);
    }

    std::vector<std::optional<MeshAsset>> compiled_documents(
        projection_documents.size());
    std::vector<std::size_t> projection_representative(
        projection_documents.size());
    {
      QDR_PROFILE_SCOPE("Transform commit group identical projection inputs");
      std::unordered_multimap<std::uint64_t, std::size_t>
          representative_by_signature;
      representative_by_signature.reserve(projection_documents.size());
      const auto positions_for = [&](std::size_t projection_index)
          -> const std::vector<VertexPositionUpdate> & {
        const ObjectId object = projection_documents[projection_index].first;
        const auto exact = exact_index->find(sdk_object_key(object));
        const auto &delta = exact_positions->objects[exact->second];
        return use_after ? delta.after : delta.before;
      };
      const auto same_updates = [](const auto &left, const auto &right) {
        if (left.size() != right.size()) {
          return false;
        }
        for (std::size_t index = 0U; index < left.size(); ++index) {
          if (left[index].vertex != right[index].vertex ||
              left[index].position.x != right[index].position.x ||
              left[index].position.y != right[index].position.y ||
              left[index].position.z != right[index].position.z) {
            return false;
          }
        }
        return true;
      };
      const auto signature_for = [&](std::size_t projection_index) {
        constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kPrime = 1099511628211ULL;
        std::uint64_t signature = kOffsetBasis;
        const auto mix = [&signature](std::uint64_t value) {
          signature ^= value;
          signature *= kPrime;
        };
        const MeshAsset &mesh = *retained_meshes[projection_index];
        mix(static_cast<std::uint64_t>(mesh.geometry_token()));
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
            mesh.authored_polygon_document.get())));
        const auto &positions = positions_for(projection_index);
        mix(positions.size());
        for (const VertexPositionUpdate &position : positions) {
          mix(static_cast<std::uint64_t>(position.vertex.index) |
              (static_cast<std::uint64_t>(position.vertex.generation) <<
               32U));
          mix(std::hash<float>{}(position.position.x));
          mix(std::hash<float>{}(position.position.y));
          mix(std::hash<float>{}(position.position.z));
        }
        return signature;
      };

      for (std::size_t index = 0U; index < projection_documents.size();
           ++index) {
        projection_representative[index] = index;
        const std::uint64_t signature = signature_for(index);
        const auto [first, last] =
            representative_by_signature.equal_range(signature);
        for (auto candidate = first; candidate != last; ++candidate) {
          const std::size_t representative = candidate->second;
          const MeshAsset &mesh = *retained_meshes[index];
          const MeshAsset &representative_mesh =
              *retained_meshes[representative];
          if (mesh.geometry_token() ==
                  representative_mesh.geometry_token() &&
              mesh.authored_polygon_document.get() ==
                  representative_mesh.authored_polygon_document.get() &&
              same_updates(positions_for(index),
                           positions_for(representative))) {
            projection_representative[index] = representative;
            break;
          }
        }
        if (projection_representative[index] == index) {
          representative_by_signature.emplace(signature, index);
        }
      }
    }
    std::atomic_size_t next_document{0u};
    std::atomic_size_t full_compile_fallbacks{0u};
    const std::size_t worker_count = std::min<std::size_t>(
        projection_documents.size(),
        std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1u, 8u));
    const auto compile_next = [&]() {
      for (;;) {
        const std::size_t index =
            next_document.fetch_add(1u, std::memory_order_relaxed);
        if (index >= projection_documents.size()) {
          return;
        }
        if (projection_representative[index] != index) {
          continue;
        }
        {
          QDR_PROFILE_SCOPE("Transform commit canonical retained compile");
          compiled_documents[index] =
              MeshProjectionService::update_sdk_document_mesh_positions(
                  projection_documents[index].second,
                  *retained_meshes[index]);
        }
        if (!compiled_documents[index].has_value()) {
          QDR_PROFILE_SCOPE("Transform commit canonical full fallback");
          full_compile_fallbacks.fetch_add(1u, std::memory_order_relaxed);
          compiled_documents[index] =
              MeshProjectionService::compile_sdk_document_mesh(
                  projection_documents[index].second);
        }
      }
    };
    {
      QDR_PROFILE_SCOPE("Transform commit precompile changed documents");
      std::vector<std::jthread> workers;
      workers.reserve(worker_count - 1u);
      for (std::size_t worker = 1u; worker < worker_count; ++worker) {
        workers.emplace_back(compile_next);
      }
      compile_next();
      for (std::jthread &worker : workers) {
        worker.join();
      }
    }
    {
      QDR_PROFILE_SCOPE("Transform commit share identical projection outputs");
      for (std::size_t index = 0U; index < compiled_documents.size(); ++index) {
        const std::size_t representative = projection_representative[index];
        if (representative != index) {
          compiled_documents[index] = compiled_documents[representative];
        }
      }
    }
    for (std::size_t index = 0; index < compiled_documents.size(); ++index) {
      if (!compiled_documents[index].has_value()) {
        return failure("SDK transform mesh compilation failed.");
      }
      compiled_documents[index]->name = retained_meshes[index]->name;
      if (!assets_.validate_mesh(*compiled_documents[index]).ok) {
        return failure("SDK transform mesh validation failed.");
      }
    }

    if (!scene_.can_notify_mesh_payloads_changed(target_entities)) {
      return failure("Transform commit scene publication is not live.");
    }

    // Stage every replacement and owner precondition before the SDK document
    // commit. Host publication below is then two fixed owner-batch calls.
    std::vector<MeshAssetReplacement> mesh_replacements;
    mesh_replacements.reserve(compiled_documents.size());
    for (std::size_t index = 0; index < compiled_documents.size(); ++index) {
      mesh_replacements.push_back(
          {.id = target_mesh_ids[index],
           .mesh = std::move(*compiled_documents[index])});
    }

    std::vector<ObjectId> mutation_participants;
    mutation_participants.reserve(projection_documents.size());
    std::ranges::transform(
        projection_documents, std::back_inserter(mutation_participants),
        [](const auto &entry) { return entry.first; });

    AdapterPublicationState host_before = capture_adapter_publication_state();
    ModelingHostPublicationTransaction publication(
        scene_, assets_, object_map_, session_,
        [this, host_before = std::move(host_before)]() mutable noexcept {
          restore_adapter_publication_state(std::move(host_before));
        },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::MeshProjection,
         .capture_scene_entities = false,
         .capture_bindings = false,
         .capture_hierarchy = false,
         .affected_scene_entities = target_entities,
         .affected_asset_ids = target_mesh_ids,
         .affected_sdk_object_ids = mutation_participants});
    if (!publication.ready()) {
      return failure("Transform mutation checkpoint is unavailable.");
    }
    const std::size_t undo_mark = publication.sdk_undo_mark();
    const auto rollback = [&](std::string message) {
      return rollback_publication_result(publication,
                                         failure(std::move(message)));
    };
    Result<OperationResult> committed =
        session_.commit_documents_batch(std::move(updates), true);
    if (!committed.ok() || !committed.value().success) {
      return rollback(committed.error_message().empty()
                          ? "Transform document commit failed."
                          : committed.error_message());
    }
    if (!committed.value().changed) {
      return rollback_publication_result(publication, from_sdk(committed));
    }

    if (!assets_.replace_meshes(mesh_replacements)) {
      return rollback("SDK transform asset batch publication failed.");
    }
    if (!scene_.notify_mesh_payloads_changed(target_entities)) {
      return rollback("SDK transform scene batch publication failed.");
    }

    stats_.asset_publication_count += mesh_replacements.size();
    stats_.scene_publication_count += target_entities.size();
    stats_.mesh_compile_count +=
        full_compile_fallbacks.load(std::memory_order_relaxed);
    (void)session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    return commit_publication_result<ModelingCommandResult>(
        publication,
        {.ok = true,
         .changed = committed.value().changed,
         .message = "Transform committed."});
  }

  [[nodiscard]] bool preview_targets_have_unique_mesh_bindings(
      std::span<const ObjectId> object_ids) const {
    for (const ObjectId object_id : object_ids) {
      const std::optional<int> native = native_object(object_id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *mesh =
          entity.has_value() && scene_.exists(*entity) ? scene_.mesh(*entity)
                                                       : nullptr;
      if (mesh == nullptr ||
          scene_.mesh_binding_reference_count(mesh->mesh) != 1u) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] ModelingCommandResult mutate_documents(
      std::span<const ObjectId> object_ids, const DocumentMutation &mutation,
      std::string_view fallback_message, bool preview_publication = false) {
    return mutate_documents_by_object(
        object_ids,
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        fallback_message, preview_publication);
  }

  [[nodiscard]] ModelingCommandResult mutate_selected_documents(
      const DocumentMutation &mutation, std::string_view fallback_message,
      bool preview_publication = false) {
    const std::vector<ObjectId> selected =
        selection_mode_ == ModelingSelectionMode::Object ||
                selection_mode_ == ModelingSelectionMode::Group
            ? selected_sdk_objects()
            : selected_component_objects();
    return mutate_documents(selected, mutation, fallback_message,
                             preview_publication);
  }

  [[nodiscard]] PreviewSdkFence capture_preview_sdk_fence(
      const modeling::ModelingSession &source, PreviewId preview_id) const {
    PreviewSdkFence fence;
    const modeling::PreviewTransactionSummary preview =
        source.preview_summary();
    if (!preview.active || preview.id != preview_id) {
      return fence;
    }
    fence.preview = preview.id;
    fence.operation_id = preview.operation_id;
    fence.preview_content_revision = preview.content_revision;
    fence.preview_objects.reserve(preview.objects.size());
    for (const modeling::PreviewTransactionObjectSummary &object :
         preview.objects) {
      if (object.kind == modeling::SessionObjectKind::Mesh) {
        fence.polygon_content_revision =
            std::max(fence.polygon_content_revision, object.content_revision);
      }
      fence.preview_objects.push_back({
          .object = object.object_id,
          .action = object.action,
          .kind = object.kind,
          .base_content_revision = object.base_content_revision,
          .content_revision = object.content_revision,
          .changed = object.changed,
          .material_slot = object.material_slot,
          .selected = object.selected,
      });
    }
    return fence;
  }

  [[nodiscard]] PreviewSdkFence
  capture_preview_sdk_fence(PreviewId preview_id) const {
    return capture_preview_sdk_fence(session_, preview_id);
  }

  [[nodiscard]] bool capture_mesh_preview_host_baseline(
      std::span<const ObjectId> object_ids,
      std::span<const int> affected_modeling_ids = {},
      std::optional<AdapterPublicationState> adapter_before = std::nullopt,
      SceneModelingProjectionDomain scene_domains =
          SceneModelingProjectionDomain::MeshBindings) {
    if (mesh_preview_host_baseline_.has_value()) {
      return false;
    }
    HostPublicationScope scope;
    scope.modeling_object_ids.assign(affected_modeling_ids.begin(),
                                     affected_modeling_ids.end());
    const bool structural =
        (scene_domains & SceneModelingProjectionDomain::Structure) !=
        SceneModelingProjectionDomain::None;
    for (const ObjectId object_id : object_ids) {
      if (!append_sdk_object_publication_scope(scope, object_id, structural)) {
        return false;
      }
    }
    if (structural) {
      append_selected_scene_publication_scope(scope, true);
    }
    finalize_host_publication_scope(scope);
    if (scope.scene_entities.empty() || scope.asset_ids.empty() ||
        scope.modeling_object_ids.empty()) {
      return false;
    }

    std::vector<PreviewHistoryAssetIdentity> history_asset_identities;
    history_asset_identities.reserve(object_ids.size());
    for (const ObjectId object_id : object_ids) {
      const std::optional<int> native = native_object(object_id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!entity.has_value() || !scene_.exists(*entity)) {
        return false;
      }
      const std::optional<SceneEntityState> scene_identity =
          capture_scene_entity_state(scene_, *entity);
      if (!scene_identity.has_value()) {
        return false;
      }
      ModelingObjectSceneIdentity original_identity;
      static_cast<SceneEntityState &>(original_identity) = *scene_identity;
      if (const CurveComponent *curve = scene_.curve(*entity)) {
        history_asset_identities.push_back({
            .object = object_id,
            .kind = modeling::SessionObjectKind::Curve,
            .asset = curve->curve,
            .identity = std::move(original_identity),
            .binding_was_unique =
                scene_.curve_binding_reference_count(curve->curve) == 1u,
        });
      } else if (const MeshComponent *mesh = scene_.mesh(*entity)) {
        history_asset_identities.push_back({
            .object = object_id,
            .kind = modeling::SessionObjectKind::Mesh,
            .asset = mesh->mesh,
            .identity = std::move(original_identity),
            .binding_was_unique =
                scene_.mesh_binding_reference_count(mesh->mesh) == 1u,
        });
      } else {
        return false;
      }
    }
    std::ranges::sort(history_asset_identities, {},
                      &PreviewHistoryAssetIdentity::object);
    history_asset_identities.erase(
        std::unique(history_asset_identities.begin(),
                    history_asset_identities.end(),
                    [](const PreviewHistoryAssetIdentity &left,
                       const PreviewHistoryAssetIdentity &right) {
                      return left.object == right.object;
                    }),
        history_asset_identities.end());

    MeshPreviewHostBaseline baseline;
    baseline.transform_history_undo_mark = session_.undo_depth();
    baseline.adapter_before = adapter_before.has_value()
                                  ? std::move(*adapter_before)
                                  : capture_adapter_publication_state();
    baseline.scope = std::move(scope);
    baseline.history_asset_identities =
        std::move(history_asset_identities);
    baseline.owner.emplace(
        scene_, assets_, &object_map_,
        ModelingHostProjectionCheckpointOptions{
            .capture_assets = true,
            .capture_scene = true,
            .capture_bindings = true,
            .capture_mutation_fence = true,
            .preserve_concurrent_owner_changes = true,
            .scene_domains = scene_domains,
            .scene_entities = baseline.scope.scene_entities,
            .asset_ids = baseline.scope.asset_ids,
            .modeling_object_ids = baseline.scope.modeling_object_ids,
        });
    if (!baseline.valid() || !baseline.owner->can_restore()) {
      return false;
    }
    mesh_preview_host_baseline_ = std::move(baseline);
    return true;
  }

  [[nodiscard]] bool activate_mesh_preview_host_baseline(
      PreviewId preview_id) {
    if (!preview_id.valid() || !mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->valid() ||
        mesh_preview_host_baseline_->preview_id.valid()) {
      return false;
    }
    mesh_preview_host_baseline_->preview_id = preview_id;
    mesh_preview_host_baseline_->acknowledged_sdk =
        capture_preview_sdk_fence(preview_id);
    mesh_preview_host_baseline_->fence_acknowledged =
        mesh_preview_host_baseline_->acknowledged_sdk.preview == preview_id;
    return mesh_preview_host_baseline_->fence_acknowledged;
  }

  [[nodiscard]] std::vector<int> preview_created_modeling_ids() const {
    std::vector<int> ids;
    if (structural_preview_host_state_.has_value()) {
      ids.reserve(structural_preview_host_state_->added_objects.size());
      for (const StructuralPreviewHostState::AddedObject &added :
           structural_preview_host_state_->added_objects) {
        ids.push_back(added.modeling_object_id);
      }
    }
    sort_unique(ids);
    return ids;
  }

  void record_preview_created_asset(AssetId asset) {
    if (asset == kInvalidAssetId || !mesh_preview_host_baseline_.has_value()) {
      return;
    }
    auto &created = mesh_preview_host_baseline_->created_assets;
    if (std::ranges::find(created, asset) == created.end()) {
      created.push_back(asset);
    }
  }

  void record_preview_created_entity(EntityId entity) {
    if (entity == kInvalidEntityId || !mesh_preview_host_baseline_.has_value()) {
      return;
    }
    auto &created = mesh_preview_host_baseline_->created_entities;
    if (std::ranges::find(created, entity) == created.end()) {
      created.push_back(entity);
    }
  }

  [[nodiscard]] bool preview_asset_was_created(AssetId asset) const {
    return mesh_preview_host_baseline_.has_value() &&
           std::ranges::find(mesh_preview_host_baseline_->created_assets,
                             asset) !=
               mesh_preview_host_baseline_->created_assets.end();
  }

  [[nodiscard]] bool acknowledge_preview_owner_fence() {
    QDR_PROFILE_FUNCTION();
    if (!mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->valid() ||
        !mesh_preview_host_baseline_->preview_id.valid()) {
      return false;
    }
    const std::vector<int> created_modeling_ids =
        preview_created_modeling_ids();
    if (!mesh_preview_host_baseline_->owner->acknowledge_current_mutations(
            mesh_preview_host_baseline_->created_entities,
            mesh_preview_host_baseline_->created_assets,
            created_modeling_ids)) {
      return false;
    }
    mesh_preview_host_baseline_->acknowledged_sdk =
        capture_preview_sdk_fence(mesh_preview_host_baseline_->preview_id);
    mesh_preview_host_baseline_->fence_acknowledged =
        mesh_preview_host_baseline_->acknowledged_sdk.preview ==
        mesh_preview_host_baseline_->preview_id;
    return mesh_preview_host_baseline_->fence_acknowledged;
  }

  [[nodiscard]] bool preview_fence_matches(PreviewId preview_id) const {
    return mesh_preview_host_baseline_.has_value() &&
           mesh_preview_host_baseline_->active_for(preview_id) &&
           mesh_preview_host_baseline_->fence_acknowledged &&
           mesh_preview_host_baseline_->owner
               ->mutations_match_acknowledged_fence() &&
           capture_preview_sdk_fence(preview_id) ==
               mesh_preview_host_baseline_->acknowledged_sdk;
  }

  [[nodiscard]] bool preflight_preview_mutation(PreviewId preview_id) const {
    QDR_PROFILE_FUNCTION();
    return preview_fence_matches(preview_id) &&
           mesh_preview_host_baseline_->owner->can_restore(
               mesh_preview_host_baseline_->created_entities,
               mesh_preview_host_baseline_->created_assets);
  }

  [[nodiscard]] bool restore_preview_to_original_baseline(
      PreviewId preview_id, bool acknowledge_own_publication,
      ModelingHostProjectionRestoreMode cancel_restore_mode =
          ModelingHostProjectionRestoreMode::PreserveConcurrent) {
    if (!mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview_id)) {
      return false;
    }
    MeshPreviewHostBaseline &baseline = *mesh_preview_host_baseline_;
    const std::vector<int> created_modeling_ids =
        preview_created_modeling_ids();
    if (acknowledge_own_publication) {
      if (!baseline.owner->mutations_match_acknowledged_fence()) {
        // A failed native publication may have advanced only a prefix of its
        // owner calls. Its caller preflighted the acknowledged fence before
        // publishing, so only this proven own prefix may be rebased here.
        if (!baseline.owner->acknowledge_current_mutations(
                baseline.created_entities, baseline.created_assets,
                created_modeling_ids)) {
          return false;
        }
      }
    } else if (!preview_fence_matches(preview_id)) {
      return false;
    }
    const ModelingHostProjectionRestoreMode restore_mode =
        acknowledge_own_publication
            ? ModelingHostProjectionRestoreMode::PreferExact
            : cancel_restore_mode;
    if (!baseline.owner->can_restore(baseline.created_entities,
                                     baseline.created_assets,
                                     restore_mode)) {
      return false;
    }

    const modeling::PreviewTransactionSummary current =
        session_.preview_summary();
    if (!current.active || current.id != preview_id ||
        !session_.cancel_preview(preview_id).ok()) {
      return false;
    }
    if (!baseline.owner->restore(baseline.created_entities,
                                 baseline.created_assets,
                                 restore_mode)) {
      return false;
    }

    AdapterPublicationState adapter_before =
        std::move(baseline.adapter_before);
    clear_preview_checkpoints();
    restore_adapter_publication_state(std::move(adapter_before));
    return true;
  }

  [[nodiscard]] ModelingCommandResult abort_preview_publication(
      PreviewId preview_id, std::string message) {
    if (restore_preview_to_original_baseline(preview_id, true)) {
      return failure(std::move(message));
    }
    return {
        .ok = false,
        .changed = true,
        .message = std::move(message) +
                   " Original preview rollback failed.",
    };
  }
  [[nodiscard]] std::optional<PreparedMeshPreviewBatch>
  prepare_mesh_preview_batch(
      const modeling::ModelingSession &base_session,
      std::span<const ObjectId> object_ids, const DocumentMutation &mutation,
      std::string_view fallback_message, std::string &error,
      std::optional<PreviewId> preview_id = std::nullopt) const {
    std::vector<PolygonDocument> copied;
    copied.reserve(object_ids.size());
    if (preview_id.has_value()) {
      for (const ObjectId object_id : object_ids) {
        Result<PolygonDocument> document =
            base_session.preview_base_document_copy(*preview_id, object_id);
        if (!document.ok()) {
          error = document.error_message();
          return std::nullopt;
        }
        copied.push_back(std::move(document).value());
      }
    } else {
      Result<std::vector<PolygonDocument>> documents =
          base_session.document_copies(object_ids);
      if (!documents.ok()) {
        error = documents.error_message();
        return std::nullopt;
      }
      copied = std::move(documents).value();
    }

    PreparedMeshPreviewBatch prepared;
    prepared.updates.reserve(object_ids.size());
    for (std::size_t index = 0; index < object_ids.size(); ++index) {
      PolygonDocument document = std::move(copied[index]);
      Result<OperationResult> operated = mutation(document);
      if (!operated.ok() || !operated.value().success) {
        error = operated.ok() && !operated.value().message.empty()
                    ? operated.value().message
                    : operated.error_message();
        if (error.empty()) {
          error = std::string(fallback_message);
        }
        return std::nullopt;
      }
      prepared.deleted_vertex_count +=
          operated.value().deleted.vertices.size();
      prepared.changed = prepared.changed || operated.value().changed;
      if (!operated.value().message.empty()) {
        prepared.message = operated.value().message;
      }
      prepared.updates.push_back({
          .object = object_ids[index],
          .document = std::move(document),
          .receipt = std::move(operated).value(),
      });
    }
    return prepared;
  }

  [[nodiscard]] ModelingCommandResult begin_mesh_preview_transaction(
      std::span<const ObjectId> object_ids, const DocumentMutation &mutation,
      std::string_view operation_id, std::string_view fallback_message) {
    std::string error;
    std::optional<PreparedMeshPreviewBatch> prepared =
        prepare_mesh_preview_batch(session_, object_ids, mutation,
                                   fallback_message, error);
    if (!prepared.has_value()) {
      return failure(std::move(error));
    }

    return begin_prepared_mesh_preview_transaction(
        object_ids, std::move(*prepared), operation_id);
  }

  [[nodiscard]] ModelingCommandResult begin_prepared_mesh_preview_transaction(
      std::span<const ObjectId> object_ids, PreparedMeshPreviewBatch prepared,
      std::string_view operation_id) {
    if (!capture_mesh_preview_host_baseline(object_ids)) {
      return failure("Bounded SDK preview baseline is unavailable.");
    }
    Result<PreviewId> begun = session_.begin_preview_transaction(
        std::string(operation_id), std::move(prepared.updates));
    if (!begun.ok()) {
      mesh_preview_host_baseline_.reset();
      return failure(begun.error_message());
    }
    if (!activate_mesh_preview_host_baseline(begun.value()) ||
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        (prepared.changed && !project_preview_object(true))) {
      return abort_preview_publication(
          begun.value(), "SDK operation preview publication failed.");
    }

    preview_changed_ = prepared.changed;
    active_preview_deleted_vertex_count_ = prepared.deleted_vertex_count;
    active_preview_operation_id_ = std::string(operation_id);
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          begun.value(),
          "SDK operation preview publication could not be acknowledged.");
    }
    return {
        .ok = true,
        .changed = preview_changed_,
        .message = prepared.message.empty()
                       ? "SDK operation preview started."
                       : std::move(prepared.message),
    };
  }

  [[nodiscard]] ModelingCommandResult update_mesh_preview_transaction(
      const DocumentMutation &mutation, std::string_view fallback_message) {
    if (!session_.preview_active()) {
      return failure("No active SDK operation preview.");
    }
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "SDK preview participants changed outside this preview.");
    }
    std::vector<ObjectId> object_ids;
    object_ids.reserve(preview.objects.size());
    for (const modeling::PreviewTransactionObjectSummary &object :
         preview.objects) {
      if (object.kind != modeling::SessionObjectKind::Mesh) {
        return failure("SDK mesh preview contains a non-mesh object.");
      }
      object_ids.push_back(object.object_id);
    }

    std::string error;
    std::optional<PreparedMeshPreviewBatch> prepared =
        prepare_mesh_preview_batch(session_, object_ids, mutation,
                                   fallback_message, error, preview.id);
    if (!prepared.has_value()) {
      return failure(std::move(error));
    }

    return update_prepared_mesh_preview_transaction(
        std::move(*prepared), fallback_message);
  }

  [[nodiscard]] ModelingCommandResult update_prepared_mesh_preview_transaction(
      PreparedMeshPreviewBatch prepared, std::string_view fallback_message,
      bool prepared_change_is_semantic = false) {
    const auto preview = session_.preview_summary();
    Result<OperationResult> updated = session_.update_preview_transaction(
        preview.id, std::move(prepared.updates));
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    const bool publish_host_projection =
        preview_changed_ || prepared.changed || updated.value().changed;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        (publish_host_projection &&
         !(face_extrusion_preview_active() ? project_face_extrusion_preview()
                                           : project_preview_object()))) {
      return abort_preview_publication(
          preview.id, "SDK operation preview publication failed.");
    }
    active_preview_deleted_vertex_count_ = prepared.deleted_vertex_count;
    // UV preparation compares against the original document. The update
    // receipt instead reports whether the displayed candidate changed.
    preview_changed_ = prepared.changed ||
                       (!prepared_change_is_semantic && updated.value().changed);
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "SDK operation preview publication could not be acknowledged.");
    }
    return {.ok = updated.value().success,
            .changed = updated.value().changed,
            .message = prepared.message.empty()
                           ? std::string(fallback_message)
                           : std::move(prepared.message)};
  }

  [[nodiscard]] DocumentMutation preview_mutation_for(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) const {
    using Id = quader_editor_core::EditorOperationId;
    switch (operation) {
    case Id::Bridge: {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      const ModelingSelectionMode mode = selection_mode_;
      return [normalized, mode](PolygonDocument &document) {
        return mode == ModelingSelectionMode::Face
                   ? document.bridge_selected_faces(normalized.steps)
                   : document.bridge_selected_edges(normalized.steps);
      };
    }
    case Id::EdgeBevel: {
      const auto *value =
          std::get_if<quader_editor_core::EdgeBevelOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_edge_bevel_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::EdgeBevelOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.bevel_selected_edges({
            .width = static_cast<float>(normalized.width),
            .profile = static_cast<float>(normalized.profile),
            .segments = normalized.segments,
        });
      };
    }
    case Id::Thicken: {
      const auto *value =
          std::get_if<quader_editor_core::ThickenOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_thicken_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::ThickenOperationSettings{});
      const ModelingSelectionMode mode = selection_mode_;
      return [normalized, mode](PolygonDocument &document) {
        return apply_with_whole_object_face_selection(
            mode, document,
            [normalized](PolygonDocument &selected_document) {
              return selected_document.thicken_selected_faces(
                  static_cast<float>(normalized.thickness),
                  normalized.from_center);
            });
      };
    }
    case Id::SliceQuad: {
      const auto *value =
          std::get_if<quader_editor_core::SliceQuadOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_slice_quad_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::SliceQuadOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.slice_selected_quads(normalized.x_slices,
                                             normalized.y_slices);
      };
    }
    case Id::MergeByDistance: {
      const auto *value =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::MergeByDistanceOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.merge_selected_vertices_by_distance(
            static_cast<float>(normalized.tolerance));
      };
    }
    default:
      return {};
    }
  }

  [[nodiscard]] ModelingCommandResult update_preview_from_base(
      const DocumentMutation &mutation, std::string_view fallback_message) {
    if (!preview_active()) {
      return failure("No active SDK operation preview.");
    }
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "SDK preview participants changed outside this preview.");
    }
    Result<PolygonDocument> base = session_.preview_base_document_copy(
        preview.id, preview.object_id);
    if (!base.ok()) {
      return failure(base.error_message());
    }
    PolygonDocument working = std::move(base).value();
    Result<OperationResult> operated = mutation(working);
    if (!operated.ok() || !operated.value().success) {
      return failure(operated.ok() && !operated.value().message.empty()
                         ? operated.value().message
                         : (operated.error_message().empty()
                                ? std::string(fallback_message)
                                : operated.error_message()));
    }
    const std::size_t deleted_vertex_count =
        operated.value().deleted.vertices.size();
    Result<OperationResult> updated = session_.update_document_preview(
        preview.id, std::move(working), std::move(operated).value());
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    const bool publish_host_projection =
        preview_changed_ || updated.value().changed;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        (publish_host_projection && !project_preview_object())) {
      return abort_preview_publication(
          preview.id, "SDK operation preview asset publication failed.");
    }
    active_preview_deleted_vertex_count_ = deleted_vertex_count;
    preview_changed_ = updated.value().changed;
    ++stats_.preview_publication_count;
    if (!acknowledge_preview_owner_fence()) {
      return abort_preview_publication(
          preview.id,
          "SDK operation preview publication could not be acknowledged.");
    }
    return from_sdk(updated, std::string(fallback_message));
  }
  [[nodiscard]] bool fails(ModelingSdkProjectionFailurePoint point) const {
    return failure_point_ == point;
  }

  [[nodiscard]] AdapterPublicationState capture_adapter_publication_state() const {
    return {
        .editor_mode = editor_mode_service_ != nullptr
                           ? std::optional{editor_mode_service_->capture_history_state()}
                           : std::nullopt,
        .next_native_object_id = next_native_object_id_,
        .active_native_object_id = active_native_object_id_,
        .selection_mode = selection_mode_,
        .persistent_content_revision = persistent_content_revision_,
        .mode_revision = mode_revision_,
        .curve_preview_scene_revision = curve_preview_scene_revision_,
        .curve_preview_asset_revision = curve_preview_asset_revision_,
        .curve_preview_external_changes = curve_preview_external_changes_,
        .curve_preview_asset_id = curve_preview_asset_id_,
        .curve_preview_sharing_proof = curve_preview_sharing_proof_,
        .preview_changed = preview_changed_,
        .active_preview_operation_id = active_preview_operation_id_,
        .active_preview_operation = active_preview_operation_,
        .active_preview_settings = active_preview_settings_,
        .active_edge_bevel_maximum_width = active_edge_bevel_maximum_width_,
        .active_preview_deleted_vertex_count =
            active_preview_deleted_vertex_count_,
        .stats = stats_,
    };
  }

  void restore_adapter_publication_state(AdapterPublicationState state) noexcept {
    if (editor_mode_service_ != nullptr && state.editor_mode.has_value()) {
      editor_mode_service_->restore_exact_state(std::move(*state.editor_mode));
    }
    next_native_object_id_ = state.next_native_object_id;
    active_native_object_id_ = state.active_native_object_id;
    selection_mode_ = state.selection_mode;
    persistent_content_revision_ = state.persistent_content_revision;
    mode_revision_ = state.mode_revision;
    curve_preview_scene_revision_ = state.curve_preview_scene_revision;
    curve_preview_asset_revision_ = state.curve_preview_asset_revision;
    curve_preview_external_changes_ = state.curve_preview_external_changes;
    curve_preview_asset_id_ = state.curve_preview_asset_id;
    curve_preview_sharing_proof_ = state.curve_preview_sharing_proof;
    preview_changed_ = state.preview_changed;
    active_preview_operation_id_ = std::move(state.active_preview_operation_id);
    active_preview_operation_ = std::move(state.active_preview_operation);
    active_preview_settings_ = std::move(state.active_preview_settings);
    active_edge_bevel_maximum_width_ = state.active_edge_bevel_maximum_width;
    active_preview_deleted_vertex_count_ =
        state.active_preview_deleted_vertex_count;
    stats_ = state.stats;
  }

  [[nodiscard]] const std::map<int, ObjectId> &sdk_bindings() const {
    return object_map_.sdk_object_bindings_by_modeling_object();
  }

  [[nodiscard]] std::optional<ObjectId> sdk_object(int native_id) const {
    return object_map_.sdk_object_for_modeling_object(native_id);
  }

  [[nodiscard]] std::optional<int> native_object(ObjectId object_id) const {
    return object_map_.modeling_object_for_sdk_object(object_id);
  }

  [[nodiscard]] std::optional<ObjectId> active_sdk_object() const {
    return sdk_object(active_native_object_id_);
  }

  [[nodiscard]] int next_available_native_object_id() const {
    int candidate = next_native_object_id_;
    const std::vector<EntityId> entities = scene_.entities();
    ++stats_.native_object_id_scene_scan_count;
    stats_.native_object_id_scene_entity_visit_count += entities.size();
    for (const EntityId entity : entities) {
      const ModelingObjectComponent *component = scene_.modeling_object(entity);
      if (component != nullptr && component->object_id >= candidate &&
          component->object_id < std::numeric_limits<int>::max()) {
        candidate = component->object_id + 1;
      }
    }
    return candidate;
  }

  [[nodiscard]] std::optional<int>
  reserve_native_object_ids(std::size_t count) {
    return reserve_preflighted_native_object_ids(
        next_available_native_object_id(), count);
  }

  /** Reserves a range whose first free ID was already verified this turn. */
  [[nodiscard]] std::optional<int>
  reserve_preflighted_native_object_ids(int first, std::size_t count) {
    if (count == 0U) {
      return std::nullopt;
    }
    if (first == kInvalidModelingObjectId ||
        first < next_native_object_id_ ||
        first >= std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    const std::size_t available = static_cast<std::size_t>(
        std::numeric_limits<int>::max() - first);
    if (count > available) {
      return std::nullopt;
    }
    next_native_object_id_ = first + static_cast<int>(count);
    return first;
  }

  [[nodiscard]] ModelingCommandResult cancel_curve_preview_projection() {
    const modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    if (!preview.active ||
        !mesh_preview_host_baseline_.has_value() ||
        !mesh_preview_host_baseline_->active_for(preview.id)) {
      return failure("Curve preview restore point is unavailable.");
    }
    if (!preflight_preview_mutation(preview.id)) {
      return failure(
          "Curve preview participants changed outside this preview.");
    }
    const bool changed = preview_changed_;
    // Curve previews have no intentionally monotonic host projection
    // policy. Rewind exact owner stamps when uncontaminated so cancel and a
    // returned-to-origin commit preserve clean state and redo; the checkpoint
    // automatically falls back to participant-scoped preserving restore when
    // an unrelated owner advanced concurrently.
    if (!restore_preview_to_original_baseline(
            preview.id, false,
            ModelingHostProjectionRestoreMode::PreferExact)) {
      return {.ok = false,
              .changed = true,
              .message = "Curve preview could not be canceled."};
    }
    return {.ok = true,
            .changed = changed,
            .message = "Curve preview canceled."};
  }
  [[nodiscard]] bool scan_curve_binding_shared(EntityId owner, AssetId curve,
                                                bool track_preview = true) const {
    if (track_preview) {
      ++stats_.curve_preview_shared_binding_scan_count;
    }
    if (owner == kInvalidEntityId || curve == kInvalidAssetId) {
      return false;
    }
    SceneBindingReferenceCountWork work;
    const std::size_t reference_count =
        scene_.curve_binding_reference_count(
            curve, track_preview ? &work : nullptr);
    if (track_preview) {
      stats_.curve_preview_shared_binding_entity_visit_count +=
          work.owner_index_lookup_count;
    }
    return reference_count > 1u;
  }

  void refresh_curve_preview_sharing_proof(EntityId entity, AssetId curve) {
    curve_preview_sharing_proof_ = {};
    if (entity == kInvalidEntityId || curve == kInvalidAssetId ||
        !scene_.exists(entity) || scene_.curve(entity) == nullptr ||
        assets_.curve(curve) == nullptr) {
      return;
    }
    curve_preview_sharing_proof_ = {
        .valid = true,
        .entity = entity,
        .curve = curve,
        .shared = scan_curve_binding_shared(entity, curve),
        .scene_revision = scene_.revision(),
        .asset_revision = assets_.revision(),
    };
  }

  [[nodiscard]] std::optional<bool> preview_mesh_binding_is_unique(
      ObjectId object, EntityId entity, AssetId mesh) const {
    if (!mesh_preview_host_baseline_.has_value()) {
      return std::nullopt;
    }
    if (std::ranges::find(mesh_preview_host_baseline_->scope.scene_entities,
                          entity) ==
        mesh_preview_host_baseline_->scope.scene_entities.end()) {
      return std::nullopt;
    }
    if (preview_asset_was_created(mesh)) {
      return true;
    }
    const auto baseline = std::ranges::lower_bound(
        mesh_preview_host_baseline_->history_asset_identities, object, {},
        &PreviewHistoryAssetIdentity::object);
    if (baseline ==
            mesh_preview_host_baseline_->history_asset_identities.end() ||
        baseline->object != object ||
        baseline->kind != modeling::SessionObjectKind::Mesh ||
        baseline->identity.id != entity || baseline->asset != mesh) {
      return std::nullopt;
    }
    return baseline->binding_was_unique &&
           scene_.mesh_binding_reference_count(mesh) == 1u;
  }

  [[nodiscard]] std::optional<bool> preview_curve_binding_is_unique(
      ObjectId object, EntityId entity, AssetId curve) const {
    if (!mesh_preview_host_baseline_.has_value()) {
      return std::nullopt;
    }
    if (std::ranges::find(mesh_preview_host_baseline_->scope.scene_entities,
                          entity) ==
        mesh_preview_host_baseline_->scope.scene_entities.end()) {
      return std::nullopt;
    }
    if (preview_asset_was_created(curve)) {
      return true;
    }
    const auto baseline = std::ranges::lower_bound(
        mesh_preview_host_baseline_->history_asset_identities, object, {},
        &PreviewHistoryAssetIdentity::object);
    if (baseline ==
            mesh_preview_host_baseline_->history_asset_identities.end() ||
        baseline->object != object ||
        baseline->kind != modeling::SessionObjectKind::Curve ||
        baseline->identity.id != entity || baseline->asset != curve) {
      return std::nullopt;
    }
    return baseline->binding_was_unique &&
           scene_.curve_binding_reference_count(curve) == 1u;
  }

  [[nodiscard]] bool curve_binding_shared_for_preview(EntityId entity,
                                                       AssetId curve) {
    if (!curve_preview_sharing_proof_.valid ||
        curve_preview_sharing_proof_.entity != entity ||
        curve_preview_sharing_proof_.curve != curve ||
        curve_preview_sharing_proof_.scene_revision != scene_.revision() ||
        curve_preview_sharing_proof_.asset_revision != assets_.revision()) {
      refresh_curve_preview_sharing_proof(entity, curve);
    }
    return curve_preview_sharing_proof_.valid &&
           curve_preview_sharing_proof_.shared;
  }

  [[nodiscard]] bool project_curve_document(
      ModelingHostPublicationTransaction *publication, ObjectId object_id,
      const modeling::CurveDocument &document, bool preview,
      bool track_preview_rollback_asset = false) {
    const auto native = native_object(object_id);
    const auto entity = native.has_value()
                            ? object_map_.entity_for_modeling_object(*native)
                            : std::nullopt;
    const CurveComponent *binding =
        entity.has_value() ? scene_.curve(*entity) : nullptr;
    const CurveAsset *current =
        binding != nullptr ? assets_.curve(binding->curve) : nullptr;
    if (!entity.has_value() || !scene_.exists(*entity) || current == nullptr ||
        current->document == nullptr) {
      return false;
    }
    if (current->document->has_same_geometry(document)) {
      return true;
    }
    const auto asset_failure = preview
        ? ModelingSdkProjectionFailurePoint::PreviewAssetReplace
        : ModelingSdkProjectionFailurePoint::CommittedAssetReplace;
    if (fails(asset_failure)) {
      return false;
    }
    const AssetId original = binding->curve;
    const std::optional<bool> preview_unique =
        preview ? preview_curve_binding_is_unique(object_id, *entity,
                                                   original)
                : std::nullopt;
    const bool shared = preview_unique.has_value()
                            ? !*preview_unique
                            : (preview
                                   ? curve_binding_shared_for_preview(*entity,
                                                                      original)
                                   : scan_curve_binding_shared(*entity, original,
                                                               false));
    CurveAsset projected;
    projected.name = current->name;
    projected.document = std::make_shared<const modeling::CurveDocument>(document);
    if (shared) {
      const AssetId private_asset =
          mesh_sync_.install_projected_curve(assets_, std::move(projected));
      if (private_asset == kInvalidAssetId) {
        return false;
      }
      if (publication != nullptr) {
        publication->track_created_asset(private_asset);
      }
      if (preview || track_preview_rollback_asset) {
        record_preview_created_asset(private_asset);
      }
      if (!mesh_sync_.set_projected_curve_binding(scene_, *entity,
                                                  private_asset)) {
        return false;
      }
    } else if (!mesh_sync_.replace_projected_curve(assets_, original,
                                                  std::move(projected))) {
      return false;
    }
    ++stats_.asset_publication_count;
    const auto scene_failure = preview
        ? ModelingSdkProjectionFailurePoint::PreviewSceneNotify
        : ModelingSdkProjectionFailurePoint::CommittedSceneNotify;
    if (fails(scene_failure) ||
        !mesh_sync_.notify_projected_curve_changed(scene_, *entity)) {
      return false;
    }
    ++stats_.scene_publication_count;
    if (preview) {
      const CurveComponent *published = scene_.curve(*entity);
      if (published != nullptr) {
        curve_preview_sharing_proof_ = {
            .valid = true,
            .entity = *entity,
            .curve = published->curve,
            .shared = false,
            .scene_revision = scene_.revision(),
            .asset_revision = assets_.revision(),
        };
      }
    }
    return true;
  }

  [[nodiscard]] bool publish_projected_mesh(
      ModelingHostPublicationTransaction *publication, ObjectId object_id,
      MeshAsset projected, bool preview,
      std::optional<bool> mesh_binding_unique = std::nullopt,
      bool track_preview_rollback_asset = false) {
    const std::optional<int> native = native_object(object_id);
    const std::optional<EntityId> entity =
        native.has_value()
            ? object_map_.entity_for_modeling_object(*native)
            : std::nullopt;
    const MeshComponent *binding =
        entity.has_value() ? scene_.mesh(*entity) : nullptr;
    const MeshAsset *current = binding != nullptr
                                   ? assets_.mesh(binding->mesh)
                                   : nullptr;
    if (!entity.has_value() || !scene_.exists(*entity) || binding == nullptr ||
        current == nullptr) {
      return false;
    }
    projected.name = current->name;
    const ModelingSdkProjectionFailurePoint asset_failure =
        preview ? ModelingSdkProjectionFailurePoint::PreviewAssetReplace
                : ModelingSdkProjectionFailurePoint::CommittedAssetReplace;
    if (fails(asset_failure)) {
      return false;
    }
    // Transform previews only use the reproject-all path after proving every
    // target owns a unique mesh binding. Preserve that proof here instead of
    // rescanning the full scene once for every projected object (O(N^2)).
    if (!mesh_binding_unique.has_value() && preview) {
      mesh_binding_unique =
          preview_mesh_binding_is_unique(object_id, *entity, binding->mesh);
    }
    const bool shared = mesh_binding_unique.has_value()
                            ? !*mesh_binding_unique
                            : scene_mesh_is_shared(scene_, binding->mesh);
    if (shared) {
      const AssetId private_mesh =
          mesh_sync_.install_projected_mesh(assets_, std::move(projected));
      if (private_mesh == kInvalidAssetId) {
        return false;
      }
      if (publication != nullptr) {
        publication->track_created_asset(private_mesh);
      }
      if (preview || track_preview_rollback_asset) {
        record_preview_created_asset(private_mesh);
      }
      if (!mesh_sync_.set_projected_mesh_binding(scene_, *entity,
                                                 private_mesh)) {
        return false;
      }
    } else if (!mesh_sync_.replace_projected_mesh(
                   assets_, binding->mesh, std::move(projected))) {
      return false;
    }
    ++stats_.asset_publication_count;
    const ModelingSdkProjectionFailurePoint scene_failure =
        preview ? ModelingSdkProjectionFailurePoint::PreviewSceneNotify
                : ModelingSdkProjectionFailurePoint::CommittedSceneNotify;
    if (fails(scene_failure)) {
      return false;
    }
    if (!mesh_sync_.notify_projected_mesh_changed(scene_, *entity)) {
      return false;
    }
    ++stats_.scene_publication_count;
    return true;
  }

  [[nodiscard]] bool project_document(
                                      ModelingHostPublicationTransaction *publication,
                                      ObjectId object_id,
                                      const PolygonDocument &document,
                                      bool preview,
                                      std::optional<bool> mesh_binding_unique =
                                          std::nullopt,
                                      std::optional<MeshAsset> projected =
                                          std::nullopt) {
    if (!projected.has_value()) {
      ++stats_.mesh_compile_count;
      projected = MeshProjectionService::compile_sdk_document_mesh(document);
    }
    return projected.has_value() &&
           publish_projected_mesh(publication, object_id,
                                  std::move(*projected), preview,
                                  mesh_binding_unique);
  }

  [[nodiscard]] bool project_committed_object(
      ModelingHostPublicationTransaction &publication, ObjectId object_id) {
    const auto native = native_object(object_id);
    const auto entity = native.has_value()
                            ? object_map_.entity_for_modeling_object(*native)
                            : std::nullopt;
    if (entity.has_value() && scene_.curve(*entity) != nullptr) {
      const auto curve = session_.curve_document_copy(object_id);
      return curve.ok() &&
             project_curve_document(&publication, object_id, curve.value(),
                                    false);
    }
    Result<PolygonDocument> document = session_.document_copy(object_id);
    return document.ok() &&
           project_document(&publication, object_id, document.value(), false);
  }

  [[nodiscard]] bool project_preview_object(bool initial_publication = false,
                                            bool committed_publication = false) {
    const Result<modeling::SessionPreviewPayload> transaction =
        session_.preview_transaction_payload();
    if (!transaction.ok() || transaction.value().objects.empty()) {
      return false;
    }
    for (const modeling::SessionPreviewObjectPayload &object :
         transaction.value().objects) {
      if (object.action == modeling::SessionPreviewAction::RemoveExisting) {
        continue;
      }
      bool publish = initial_publication && object.changed;
      if (!initial_publication && mesh_preview_host_baseline_.has_value()) {
        const auto acknowledged = std::ranges::find(
            mesh_preview_host_baseline_->acknowledged_sdk.preview_objects,
            object.object_id, &PreviewSdkObjectFence::object);
        publish =
            acknowledged ==
                mesh_preview_host_baseline_->acknowledged_sdk.preview_objects
                    .end() ||
            acknowledged->content_revision != object.content_revision;
      }
      if (!publish) {
        continue;
      }
      if (object.action == modeling::SessionPreviewAction::AddProvisional &&
          !native_object(object.object_id).has_value()) {
        // Structural preview begin publishes the provisional host identity
        // after the SDK has assigned its stable ObjectId.
        continue;
      }
      if (object.kind == modeling::SessionObjectKind::Curve) {
        const auto *curve =
            std::get_if<modeling::CurveDocument>(&object.payload);
        if (curve == nullptr ||
            !project_curve_document(nullptr, object.object_id, *curve,
                                    !committed_publication,
                                    committed_publication)) {
          return false;
        }
        continue;
      }
      const auto *payload =
          std::get_if<modeling::ToolPreviewPayload>(&object.payload);
      if (payload == nullptr || !payload->valid) {
        return false;
      }
      Result<PolygonDocument> authored = session_.preview_document_copy(
          transaction.value().id, object.object_id);
      if (!authored.ok()) {
        return false;
      }
      std::optional<MeshAsset> mesh;
      if (!committed_publication && mesh_preview_host_baseline_.has_value() &&
          mesh_preview_host_baseline_->active_for(transaction.value().id) &&
          std::ranges::find(mesh_preview_host_baseline_->preserve_uv_layout_objects,
                            object.object_id) !=
              mesh_preview_host_baseline_->preserve_uv_layout_objects.end()) {
        auto baseline = session_.preview_base_document_copy(
            transaction.value().id, object.object_id);
        if (!baseline.ok()) return false;
        auto preserved =
            MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
                baseline.value(), authored.value(),
                selection_mode_ == ModelingSelectionMode::Object ||
                    selection_mode_ == ModelingSelectionMode::Group);
        if (!preserved.ok() || !preserved.value().success) return false;
        if (preserved.value().changed) {
          ++stats_.mesh_compile_count;
          mesh = MeshProjectionService::compile_sdk_document_mesh(authored.value());
          if (!mesh.has_value()) return false;
        }
      }
      if (!mesh.has_value()) {
        mesh = MeshProjectionService::mesh_asset_from_sdk_payload(
            payload->mesh, authored.value());
      }
      if (!mesh.has_value() ||
          !publish_projected_mesh(nullptr, object.object_id,
                                  std::move(*mesh), !committed_publication,
                                  std::nullopt, committed_publication)) {
        return false;
      }
    }
    return true;
  }

  void clear_preview_checkpoints() {
    accepted_overlay_coordinates_.reset();
    face_extrusion_preview_objects_.clear();
    clear_face_extrusion_preview_projection();
    face_extrusion_preview_source_.reset();
    face_extrusion_retained_cancellation_source_.reset();
    face_extrusion_preview_request_before_.reset();
    mesh_preview_host_baseline_.reset();
    structural_preview_host_state_.reset();
    preview_changed_ = false;
    curve_preview_external_changes_ = false;
    curve_preview_asset_id_ = kInvalidAssetId;
    curve_preview_sharing_proof_ = {};
    transient_transform_payload_participants_.clear();
    transient_transform_payload_active_ = false;
    ++transient_transform_payload_revision_;
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    active_preview_deleted_vertex_count_ = 0U;
    active_edge_bevel_maximum_width_.reset();
  }

  EditorModeService *editor_mode_service_ = nullptr;
  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingObjectRuntimeMap &object_map_;
  ModelingMeshSyncService &mesh_sync_;
  std::function<std::optional<QMat4>(int)>
      modeling_object_world_transform_resolver_;
  std::function<ModelingInstanceEditScope()> instance_edit_scope_provider_;
  std::function<std::optional<QMat4>(int)>
      modeling_object_texture_sampling_resolver_;
  quader::modeling::ModelingSession session_;
  std::uint64_t face_extrusion_generation_ = 0;
  std::shared_ptr<const ModelingFaceExtrusionSource::Impl>
      face_extrusion_preview_source_;
  std::shared_ptr<const ModelingFaceExtrusionSource::Impl>
      face_extrusion_retained_cancellation_source_;
  std::optional<ModelingFaceExtrusionSource::Impl::Request>
      face_extrusion_preview_request_before_;
  std::vector<FaceExtrusionPreviewObject> face_extrusion_preview_objects_;
  std::vector<ModelingToolPreviewMesh> face_extrusion_preview_meshes_;
  std::uint64_t face_extrusion_preview_revision_ = 0;
  mutable PlaneCutPreviewCache plane_cut_preview_cache_;
  mutable std::uint64_t plane_cut_preview_revision_ = 0;
  int next_native_object_id_ = 1;
  int active_native_object_id_ = kInvalidModelingObjectId;
  ModelingSelectionMode selection_mode_ = ModelingSelectionMode::Object;
  std::uint64_t persistent_content_revision_ = 0;
  std::uint64_t mode_revision_ = 0;
  std::optional<MeshPreviewHostBaseline> mesh_preview_host_baseline_;
  std::optional<StructuralPreviewHostState> structural_preview_host_state_;
  std::uint64_t curve_preview_scene_revision_ = 0;
  std::uint64_t curve_preview_asset_revision_ = 0;
  bool curve_preview_external_changes_ = false;
  AssetId curve_preview_asset_id_ = kInvalidAssetId;
  CurvePreviewSharingProof curve_preview_sharing_proof_;
  bool preview_changed_ = false;
  std::function<ModelingCommandResult()> face_texture_gesture_selection_cancel_handler_;
  std::function<ModelingCommandResult()> uv_peel_preview_selection_refresh_handler_;
  std::function<void()> uv_peel_preview_before_selection_suspend_;
  std::function<void()> uv_peel_preview_after_selection_suspend_;
  std::function<bool()> uv_peel_preview_tool_active_;
  bool uv_peel_selection_edit_in_progress_ = false;
  std::vector<TransientTransformParticipant>
      transient_transform_payload_participants_;
  std::unordered_map<std::uint64_t, std::size_t>
      transient_transform_participant_indices_;
  bool transient_transform_payload_active_ = false;
  std::shared_ptr<const ModelingAcceptedOverlayCoordinates>
      accepted_overlay_coordinates_;
  AcceptedOverlayProof accepted_overlay_proof_;
  ModelingSdkTransformHistoryDelta committed_transform_history_delta_;
  std::optional<std::size_t> committed_transform_undo_mark_;
  std::uint64_t transient_transform_payload_revision_ = 0U;
  std::string active_preview_operation_id_;
  std::optional<quader_editor_core::EditorOperationId>
      active_preview_operation_;
  std::optional<quader_editor_core::EditorOperationSettings>
      active_preview_settings_;
  std::optional<double> active_edge_bevel_maximum_width_;
  std::function<void(const quader_editor_core::EdgeBevelOperationSettings &)>
      edge_bevel_preview_settings_handler_;
  std::size_t active_preview_deleted_vertex_count_ = 0U;
  mutable std::optional<SelectionSummaryCacheKey>
      selection_summary_cache_key_;
  mutable ModelingSelectionSummary selection_summary_cache_;
  mutable std::optional<SelectionSummaryCacheKey> selection_topology_cache_key_;
  mutable PolygonSelectionTopologyEligibility selection_topology_cache_;
  mutable std::vector<EntityId> selected_component_entities_cache_;
  mutable std::optional<SelectionProjectionSnapshotCacheKey>
      selection_projection_snapshot_cache_key_;
  mutable std::optional<ModelingSelectionProjectionSnapshot>
      selection_projection_snapshot_cache_;
  ModelingSdkProjectionFailurePoint failure_point_ =
      ModelingSdkProjectionFailurePoint::None;
  mutable ModelingSdkProjectionStats stats_;
};

ModelingSdkProjectionAdapter::ModelingSdkProjectionAdapter(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingObjectRuntimeMap &object_map,
    ModelingMeshSyncService &mesh_sync)
    : impl_(std::make_unique<Impl>(scene, assets, object_map, mesh_sync)) {}

ModelingSdkProjectionAdapter::~ModelingSdkProjectionAdapter() = default;

void ModelingSdkProjectionAdapter::bind_editor_mode_service(
    EditorModeService *service) noexcept {
  impl_->bind_editor_mode_service(service);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_editor_mode_context(
    const EditorModeSnapshot &context,
    std::span<const EntityId> first_admission_entities) {
  return impl_->apply_editor_mode_context(context, first_admission_entities);
}

void ModelingSdkProjectionAdapter::set_modeling_object_world_transform_resolver(
    std::function<std::optional<QMat4>(int)> resolver) {
  impl_->set_modeling_object_world_transform_resolver(std::move(resolver));
}

void ModelingSdkProjectionAdapter::set_modeling_object_texture_sampling_resolver(
    std::function<std::optional<QMat4>(int)> resolver) {
  impl_->set_modeling_object_texture_sampling_resolver(std::move(resolver));
}

void ModelingSdkProjectionAdapter::set_instance_edit_scope_provider(
    std::function<ModelingInstanceEditScope()> provider) {
  impl_->set_instance_edit_scope_provider(std::move(provider));
}

void ModelingSdkProjectionAdapter::reset_from_scene() {
  impl_->reset_from_scene();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::restore_mesh_documents_from_scene(
    std::span<const EntityId> entities) {
  return impl_->restore_mesh_documents_from_scene(entities);
}

bool ModelingSdkProjectionAdapter::owns_object(int modeling_object_id) const {
  return impl_->owns_object(modeling_object_id);
}

bool ModelingSdkProjectionAdapter::owns_entity(EntityId entity) const {
  return impl_->owns_entity(entity);
}

std::size_t ModelingSdkProjectionAdapter::object_count() const noexcept {
  return impl_->object_count();
}

bool ModelingSdkProjectionAdapter::has_selection() const {
  return impl_->has_selection();
}

const void *ModelingSdkProjectionAdapter::session_identity() const noexcept {
  return impl_->session_identity();
}

std::optional<MeshAsset> ModelingSdkProjectionAdapter::compile_object_mesh(
    int modeling_object_id) const {
  return impl_->compile_object_mesh(modeling_object_id);
}

ModelingSdkPlaneCutPreviewResult
ModelingSdkProjectionAdapter::preview_plane_cut(
    std::span<const int> modeling_object_ids,
    const std::array<QVec3, 3> &world_plane_points,
    ModelingCutMode mode) const {
  return impl_->preview_plane_cut(modeling_object_ids, world_plane_points,
                                  mode);
}

modeling::Result<std::vector<ModelingSdkPlaneCutRequest>>
ModelingSdkProjectionAdapter::plane_cut_requests(
    const quader_editor_core::CutToolState &state) const {
  return impl_->plane_cut_requests(state);
}

ModelingSdkPlaneCutPreviewResult ModelingSdkProjectionAdapter::preview_plane_cut(
    const quader_editor_core::CutToolState &state) const {
  return impl_->preview_plane_cut(state);
}

ModelingSdkPlaneCutPreviewResult ModelingSdkProjectionAdapter::preview_plane_cut(
    std::span<const ModelingSdkPlaneCutRequest> requests,
    ModelingCutMode mode) const {
  return impl_->preview_plane_cut(requests, mode);
}

const ModelingSdkPlaneCutPreviewResult &
ModelingSdkProjectionAdapter::plane_cut_preview() const noexcept {
  return impl_->plane_cut_preview();
}

std::uint64_t
ModelingSdkProjectionAdapter::plane_cut_preview_revision() const noexcept {
  return impl_->plane_cut_preview_revision();
}

void ModelingSdkProjectionAdapter::clear_plane_cut_preview() {
  impl_->clear_plane_cut_preview();
}

ModelingSdkMirrorPreviewResult ModelingSdkProjectionAdapter::preview_mirror(
    std::span<const int> modeling_object_ids,
    const std::array<QVec3, 3> &world_plane_points) const {
  return impl_->preview_mirror(modeling_object_ids, world_plane_points);
}

const ModelingSelectionProjectionSnapshot &
ModelingSdkProjectionAdapter::selection_projection_snapshot() const {
  return impl_->selection_projection_snapshot();
}

std::optional<EntityId> ModelingSdkProjectionAdapter::pick_object_entity(
    const ModelingPickSelectRequest &request,
    std::optional<EntityId> competing_native) const {
  return impl_->pick_object_entity(request, competing_native);
}

std::vector<EntityId>
ModelingSdkProjectionAdapter::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Modeling SDK box pick entry");
  return impl_->box_pick_object_entities(request);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_box(
    const ModelingCreateBoxRequest &request, const CreationFinalizer &finalize) {
  return impl_->create_box(request, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_box_from_corners(
    std::span<const QVec3> corners, std::string_view name,
    MaterialReference material, const CreationFinalizer &finalize) {
  return impl_->create_box_from_corners(corners, name, material, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_mesh(
    const ModelingCreateMeshRequest &request,
    const CreationFinalizer &finalize) {
  return impl_->create_mesh(request, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_mesh_document(
    modeling::PolygonDocument document, std::string_view name,
    MaterialReference material, const CreationFinalizer &finalize) {
  return impl_->create_mesh_document(std::move(document), name, material,
                                     finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_curve(
    modeling::CurveDocument document, std::string_view name,
    const CreationFinalizer &finalize) {
  return impl_->create_curve(std::move(document), name, finalize);
}

std::optional<modeling::CurveDocument> ModelingSdkProjectionAdapter::curve_document(
    int modeling_object_id, bool include_preview) const {
  return impl_->curve_document(modeling_object_id, include_preview);
}

ModelingCommandResult ModelingSdkProjectionAdapter::replace_curve_document(
    int modeling_object_id, modeling::CurveDocument document) {
  return impl_->replace_curve_document(modeling_object_id, std::move(document));
}

ModelingCommandResult ModelingSdkProjectionAdapter::begin_curve_document_preview(
    int modeling_object_id) {
  return impl_->begin_curve_document_preview(modeling_object_id);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_curve_document_preview(
    modeling::CurveDocument document) {
  return impl_->update_curve_document_preview(std::move(document));
}

bool ModelingSdkProjectionAdapter::curve_document_preview_active() const {
  return impl_->curve_document_preview_active();
}

int ModelingSdkProjectionAdapter::curve_document_preview_object_id() const {
  return impl_->curve_document_preview_object_id();
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_face(
    std::span<const QVec3> points, QVec3 requested_normal,
    std::string_view name, MaterialReference material,
    const CreationFinalizer &finalize) {
  return impl_->create_face(points, requested_normal, name, material, finalize);
}

ModelingSdkDuplicateBatchResult ModelingSdkProjectionAdapter::duplicate_objects(
    std::span<const int> source_modeling_object_ids,
    std::size_t copies_per_source, bool select_sources,
    bool share_source_mesh_assets) {
  return impl_->duplicate_objects(source_modeling_object_ids,
                                  copies_per_source, select_sources, false,
                                  share_source_mesh_assets);
}

ModelingSdkClipboardCopyResult
ModelingSdkProjectionAdapter::copy_selection_to_clipboard() {
  return impl_->copy_selection_to_clipboard();
}

ModelingSdkDuplicateBatchResult ModelingSdkProjectionAdapter::paste_clipboard(
    std::span<const ModelingObjectSceneIdentity> source_identities) {
  return impl_->paste_clipboard(source_identities);
}

bool ModelingSdkProjectionAdapter::clipboard_available() const {
  return impl_->clipboard_available();
}

std::size_t ModelingSdkProjectionAdapter::clipboard_object_count() const {
  return impl_->clipboard_object_count();
}

ModelingCommandResult ModelingSdkProjectionAdapter::replace_mesh_document(
    int modeling_object_id, const quader_poly::Document &document,
    bool preserve_selection) {
  return impl_->replace_mesh_document(modeling_object_id, document,
                                      preserve_selection);
}

ModelingFaceExtrusionSourceResult
ModelingSdkProjectionAdapter::capture_face_extrusion_source() const {
  return impl_->capture_face_extrusion_source();
}

ModelingCommandResult ModelingSdkProjectionAdapter::validate_next_face_extrusion(
    modeling::FaceExtrudeMode mode, double amount_internal) const {
  return impl_->validate_next_face_extrusion(mode, amount_internal);
}

bool ModelingSdkProjectionAdapter::face_extrusion_source_current(
    const ModelingFaceExtrusionSource &source) const {
  return impl_->face_extrusion_source_current(source);
}

ModelingCommandResult ModelingSdkProjectionAdapter::validate_face_extrusion(
    const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
    double amount_internal, std::optional<QVec3> world_direction) const {
  return impl_->validate_face_extrusion(source, mode, amount_internal, world_direction);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_extrusion(
    const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
    double amount_internal, std::optional<QVec3> world_direction) {
  return impl_->apply_face_extrusion(source, mode, amount_internal, world_direction);
}

ModelingCommandResult ModelingSdkProjectionAdapter::begin_face_extrusion_preview(
    const ModelingFaceExtrusionSource &source,
    const ModelingFaceExtrusionSource &retained_cancellation_source) {
  return impl_->begin_face_extrusion_preview(source, false,
                                           retained_cancellation_source);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::begin_face_extrusion_adjustment_preview(
    const ModelingFaceExtrusionSource &source) {
  return impl_->begin_face_extrusion_preview(source, true);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_face_extrusion_preview(
    const ModelingFaceExtrusionSource &source, modeling::FaceExtrudeMode mode,
    double amount_internal, std::optional<QVec3> world_direction) {
  return impl_->update_face_extrusion_preview(source, mode, amount_internal,
                                            world_direction);
}

bool ModelingSdkProjectionAdapter::face_extrusion_preview_active() const {
  return impl_->face_extrusion_preview_active();
}

std::vector<ModelingToolPreviewMesh>
ModelingSdkProjectionAdapter::face_extrusion_preview_meshes() const {
  return impl_->face_extrusion_preview_meshes();
}

std::uint64_t ModelingSdkProjectionAdapter::face_extrusion_preview_revision() const {
  return impl_->face_extrusion_preview_revision();
}

ModelingCommandResult ModelingSdkProjectionAdapter::commit_face_extrusion_preview() {
  return impl_->commit_face_extrusion_preview();
}

ModelingCommandResult ModelingSdkProjectionAdapter::cancel_face_extrusion_preview() {
  return impl_->cancel_face_extrusion_preview();
}

ModelingCommandResult ModelingSdkProjectionAdapter::execute_operation(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->execute_operation(operation, settings);
}

ModelingCommandResult ModelingSdkProjectionAdapter::plane_cut(
    std::span<const ModelingSdkPlaneCutRequest> requests,
    ModelingCutMode mode, bool select_results) {
  return impl_->plane_cut(requests, mode, select_results);
}

ModelingCommandResult ModelingSdkProjectionAdapter::begin_operation_preview(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->begin_operation_preview(operation, settings);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_operation_preview(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->update_operation_preview(operation, settings);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::commit_operation_preview() {
  return impl_->commit_operation_preview();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::cancel_operation_preview() {
  return impl_->cancel_operation_preview();
}

std::string_view
ModelingSdkProjectionAdapter::active_preview_operation_id() const noexcept {
  return impl_->active_preview_operation_id();
}

std::optional<quader_editor_core::EditorOperationId>
ModelingSdkProjectionAdapter::active_preview_operation() const noexcept {
  return impl_->active_preview_operation();
}

std::optional<double>
ModelingSdkProjectionAdapter::active_edge_bevel_maximum_width() const noexcept {
  return impl_->active_edge_bevel_maximum_width();
}

std::optional<quader_editor_core::EdgeBevelOperationSettings>
ModelingSdkProjectionAdapter::active_edge_bevel_settings() const noexcept {
  return impl_->active_edge_bevel_settings();
}

void ModelingSdkProjectionAdapter::set_edge_bevel_preview_settings_handler(
    std::function<void(const quader_editor_core::EdgeBevelOperationSettings &)>
        handler) {
  impl_->set_edge_bevel_preview_settings_handler(std::move(handler));
}

std::size_t
ModelingSdkProjectionAdapter::active_preview_deleted_vertex_count() const
    noexcept {
  return impl_->active_preview_deleted_vertex_count();
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_object_selection(
    std::span<const EntityId> entities, EntityId active) {
  return impl_->set_object_selection(entities, active);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::project_object_selection_from_scene(
    std::span<const EntityId> entities, EntityId active) {
  return impl_->set_object_selection(entities, active, false);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::clear_object_selection_preserving_mode() {
  return impl_->set_object_selection({}, kInvalidEntityId, false, true);
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_selection_mode(
    ModelingSelectionMode mode) {
  return impl_->set_selection_mode(mode);
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_component_selection(
    const ModelingComponentSelectionRequest &request) {
  return impl_->set_component_selection(request);
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_component_selections(
    std::span<const ModelingComponentSelectionRequest> requests) {
  return impl_->set_component_selections(requests);
}

ModelingCommandResult ModelingSdkProjectionAdapter::select_mesh_ngons(
    std::span<const int> modeling_object_ids) {
  return impl_->select_mesh_ngons(modeling_object_ids);
}

ModelingCommandResult ModelingSdkProjectionAdapter::select_all() {
  return impl_->select_all();
}

ModelingCommandResult ModelingSdkProjectionAdapter::invert_selection(
    std::span<const ModelingComponentSelectionUniverse> universes) {
  return impl_->invert_selection(universes);
}

ModelingCommandResult ModelingSdkProjectionAdapter::clear_selection() {
  return impl_->clear_selection();
}

ModelingCommandResult ModelingSdkProjectionAdapter::remove_objects(
    std::span<const int> modeling_object_ids) {
  return impl_->remove_objects(modeling_object_ids);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::apply_selection_projection_checked(
    const ModelingSelectionProjectionSnapshot &projection) {
  return impl_->apply_selection_projection_checked(projection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::pick_select(
    const ModelingPickSelectRequest &request,
    ModelingSelectionService &selection) {
  return impl_->pick_select(request, selection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::box_select(
    const ModelingBoxSelectRequest &request,
    ModelingSelectionService &selection) {
  QDR_PROFILE_SCOPE("Modeling SDK box select entry");
  return impl_->box_select(request, selection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::box_select(
    const ModelingBoxSelectRequest &request,
    const quader_editor_core::RectangleSelectionState &baseline,
    ModelingSelectionService &selection) {
  QDR_PROFILE_SCOPE("Modeling SDK baseline box select entry");
  return impl_->box_select(request, baseline, selection);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::move_selection(QVec3 delta) {
  return impl_->move_selection(delta);
}

ModelingCommandResult ModelingSdkProjectionAdapter::transform_selection(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->transform_selection(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::mirror_selected_objects_across_plane(
    QVec3 origin, QVec3 normal) {
  return impl_->mirror_selected_objects_across_plane(origin, normal);
}

ModelingCommandResult ModelingSdkProjectionAdapter::mirror_objects_across_planes(
    std::span<const ModelingSdkPlaneCutRequest> requests) {
  return impl_->mirror_objects_across_planes(requests);
}

ModelingCommandResult ModelingSdkProjectionAdapter::flip_selected_objects(
    ModelingFlipOrientation orientation) {
  return impl_->flip_selected_objects(orientation);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_selection(material_slot);
}

ModelingSelectedFaceTextureSnapshot
ModelingSdkProjectionAdapter::selected_face_texture_settings(
    bool include_whole_meshes) const {
  return impl_->selected_face_texture_settings(include_whole_meshes);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_texture_updates(
    std::span<const ModelingFaceTextureUpdateBatch> batches) {
  return impl_->apply_face_texture_updates(batches);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_texture_canvas_edits(
    std::span<const ModelingFaceTextureCanvasEdit> edits) {
  return impl_->apply_face_texture_canvas_edits(edits);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_texture_action(
    std::span<const ModelingFaceTextureActionTarget> targets,
    const modeling::FaceTextureActionOptions &options) {
  return impl_->apply_face_texture_action(targets, options);
}

ModelingCommandResult ModelingSdkProjectionAdapter::transfer_face_texture(
    const ModelingFaceTextureTransferTarget &source,
    const ModelingFaceTextureTransferTarget &target,
    std::uint32_t target_material_slot) {
  return impl_->transfer_face_texture(source, target, target_material_slot);
}

modeling::Result<modeling::FaceTextureCanvasChart>
ModelingSdkProjectionAdapter::face_texture_canvas(
    const ModelingFaceTextureTransferTarget &target) const {
  return impl_->face_texture_canvas(target);
}

std::vector<modeling::Result<modeling::FaceTextureCanvasChart>>
ModelingSdkProjectionAdapter::face_texture_canvases(
    std::span<const ModelingFaceTextureActionTarget> targets) const {
  return impl_->face_texture_canvases(targets);
}

void ModelingSdkProjectionAdapter::visit_face_texture_canvases(
    std::span<const ModelingFaceTextureActionTarget> targets,
    const FaceTextureCanvasVisitor &visitor) const {
  impl_->visit_face_texture_canvases(targets, visitor);
}

ModelingCommandResult ModelingSdkProjectionAdapter::refresh_face_texture_gesture(
    const ModelingFaceTextureTransferTarget &target,
    const modeling::FaceTextureCanvasMap &map,
    std::optional<modeling::FaceTextureTransformUpdate> scalar_update) {
  return impl_->refresh_face_texture_gesture(target, map, std::move(scalar_update));
}

bool ModelingSdkProjectionAdapter::face_texture_gesture_active() const noexcept {
  return impl_->face_texture_gesture_active();
}

void ModelingSdkProjectionAdapter::set_face_texture_gesture_selection_cancel_handler(
    std::function<ModelingCommandResult()> handler) {
  impl_->set_face_texture_gesture_selection_cancel_handler(std::move(handler));
}

ModelingSelectedUvPeelSnapshot
ModelingSdkProjectionAdapter::selected_uv_peel_edges() const {
  return impl_->selected_uv_peel_edges();
}

ModelingCommandResult ModelingSdkProjectionAdapter::refresh_uv_peel_preview(
    std::span<const ModelingUvPeelTarget> targets,
    const ModelingUvPeelOptions &options) {
  return impl_->refresh_uv_peel_preview(targets, options);
}

bool ModelingSdkProjectionAdapter::uv_peel_preview_active() const noexcept {
  return impl_->uv_peel_preview_active();
}

void ModelingSdkProjectionAdapter::set_uv_peel_preview_selection_refresh_handler(
    std::function<ModelingCommandResult()> handler,
    std::function<void()> before_suspend,
    std::function<void()> after_suspend,
    std::function<bool()> tool_active) {
  impl_->set_uv_peel_preview_selection_refresh_handler(
      std::move(handler), std::move(before_suspend), std::move(after_suspend),
      std::move(tool_active));
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_uv_peel(
    std::span<const ModelingUvPeelTarget> targets,
    const ModelingUvPeelOptions &options) {
  return impl_->apply_uv_peel(targets, options);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slot_to_objects(
    std::span<const int> modeling_object_ids, std::uint32_t material_slot,
    bool preserve_scene_selection) {
  return impl_->assign_material_slot_to_objects(modeling_object_ids,
                                                material_slot,
                                                preserve_scene_selection);
}

ModelingCommandResult ModelingSdkProjectionAdapter::assign_material_slot_to_face(
    int modeling_object_id, int face_id, std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_face(modeling_object_id, face_id,
                                              material_slot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slots_to_faces(
    std::span<const ModelingMaterialSlotAssignment> assignments) {
  return impl_->assign_material_slots_to_faces(assignments);
}

ModelingSelectedVerticesSnapshot
ModelingSdkProjectionAdapter::selected_vertices_snapshot() const {
  return impl_->selected_vertices_snapshot();
}

modeling::Result<ModelingPolygonDocumentReadSnapshot>
ModelingSdkProjectionAdapter::polygon_document_read_snapshot(
    int modeling_object_id, bool include_preview) const {
  return impl_->polygon_document_read_snapshot(modeling_object_id,
                                                include_preview);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::apply_resolved_vertex_positions(
    std::span<const ModelingResolvedVertexPositionBatch> batches) {
  return impl_->apply_resolved_vertex_positions(batches);
}

ModelingCommandResult ModelingSdkProjectionAdapter::insert_edge_loop(
    int modeling_object_id, int edge_a, int edge_b, float factor) {
  return impl_->insert_edge_loop(modeling_object_id, edge_a, edge_b, factor);
}

ModelingCommandResult ModelingSdkProjectionAdapter::knife_stroke(
    int modeling_object_id,
    std::span<const ModelingSdkKnifeTarget> points,
    std::span<const ModelingSdkKnifeStrokeSegment> segments) {
  return impl_->knife_stroke(modeling_object_id, points, segments);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::begin_translate_preview(
    bool duplicate_selected, bool topology_stable_transform,
    std::span<const ModelingStructuralEntityClone> structural_duplicates) {
  return impl_->begin_translate_preview(duplicate_selected,
                                        topology_stable_transform,
                                        structural_duplicates);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::update_translate_preview(QVec3 delta) {
  return impl_->update_translate_preview(delta);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_transform_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_preview(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::update_object_transform_preview(
    std::span<const ModelingSdkObjectTransformPreview> transforms,
    ModelingSdkTransformPreviewKind kind, float closed_edge_ledge_size) {
  return impl_->update_object_transform_preview(transforms, kind,
                                                 closed_edge_ledge_size);
}

std::vector<ModelingToolPreviewMesh>
ModelingSdkProjectionAdapter::workspace_transform_preview_meshes() const {
  return impl_->workspace_transform_preview_meshes();
}

std::shared_ptr<const ModelingAcceptedOverlayCoordinates>
ModelingSdkProjectionAdapter::accepted_overlay_coordinates() const {
  return impl_->accepted_overlay_coordinates();
}

std::uint64_t
ModelingSdkProjectionAdapter::workspace_transform_preview_revision() const {
  return impl_->workspace_transform_preview_revision();
}

bool ModelingSdkProjectionAdapter::workspace_transform_preview_active() const {
  return impl_->workspace_transform_preview_active();
}

void ModelingSdkProjectionAdapter::apply_workspace_transform_preview_to_overlay(
    ViewportOverlayFrame &frame) const {
  impl_->apply_workspace_transform_preview_to_overlay(frame);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_extrude_preview(
    QVec3 offset, float closed_edge_ledge_size) {
  return impl_->update_extrude_preview(offset, closed_edge_ledge_size);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_inset_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_inset_preview(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::commit_translate_preview(bool auto_merge_vertices) {
  return impl_->commit_translate_preview(auto_merge_vertices);
}

bool ModelingSdkProjectionAdapter::transform_preview_supports_sparse_history()
    const {
  return impl_->transform_preview_supports_sparse_history();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::cancel_translate_preview() {
  return impl_->cancel_translate_preview();
}

ModelingSdkTransformHistoryDelta
ModelingSdkProjectionAdapter::take_committed_transform_history_delta() {
  return impl_->take_committed_transform_history_delta();
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_transform_history_delta(
    const ModelingSdkTransformHistoryDelta &delta, bool use_after) {
  return impl_->apply_transform_history_delta(delta, use_after);
}

bool ModelingSdkProjectionAdapter::preview_active() const noexcept {
  return impl_->preview_active();
}

ModelingSelectionMode
ModelingSdkProjectionAdapter::selection_mode() const noexcept {
  return impl_->selection_mode();
}

ModelingSelectionSummary
ModelingSdkProjectionAdapter::selection_summary() const {
  return impl_->selection_summary();
}

bool ModelingSdkProjectionAdapter::selected_faces_have_open_perimeter() const {
  return impl_->selected_faces_have_open_perimeter();
}

PolygonSelectionTopologyEligibility
ModelingSdkProjectionAdapter::selection_topology_eligibility() const {
  return impl_->selection_topology_eligibility();
}

std::span<const EntityId>
ModelingSdkProjectionAdapter::selected_component_entities() const {
  return impl_->selected_component_entities();
}

ModelingSelectionExport ModelingSdkProjectionAdapter::export_selection() const {
  return impl_->export_selection();
}

std::optional<SceneBounds>
ModelingSdkProjectionAdapter::selection_bounds() const {
  return impl_->selection_bounds();
}

std::vector<EntityId>
ModelingSdkProjectionAdapter::selected_entities() const {
  return impl_->selected_entities();
}

EntityId ModelingSdkProjectionAdapter::active_selected_entity() const {
  return impl_->active_selected_entity();
}

void ModelingSdkProjectionAdapter::add_selection_to_scene() {
  (void)impl_->add_selection_to_scene();
}

bool ModelingSdkProjectionAdapter::
    acknowledge_active_preview_host_publication() {
  return impl_->acknowledge_active_preview_host_publication();
}

std::uint64_t ModelingSdkProjectionAdapter::content_revision() const {
  return impl_->content_revision();
}

std::uint64_t ModelingSdkProjectionAdapter::selection_overlay_preview_signature() const noexcept {
  return impl_->selection_overlay_preview_signature();
}

std::uint64_t ModelingSdkProjectionAdapter::selection_revision() const {
  return impl_->selection_revision();
}

void ModelingSdkProjectionAdapter::release_redundant_sdk_history() {
  impl_->release_redundant_sdk_history();
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::capture_snapshot() const {
  return impl_->capture_snapshot(false);
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::capture_component_merge_snapshot(
    const ModelingSdkProjectionSnapshot &before) const {
  return impl_->capture_component_merge_snapshot(before);
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::capture_active_preview_base_snapshot() const {
  return impl_->capture_active_preview_base_snapshot();
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::rebase_snapshot_host_projection(
    const ModelingSdkProjectionSnapshot &snapshot) const {
  return impl_->rebase_snapshot_host_projection(snapshot);
}

ModelingCommandResult ModelingSdkProjectionAdapter::restore_snapshot(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot(snapshot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::restore_snapshot_deferred_scene_hierarchy(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_deferred_scene_hierarchy(snapshot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::restore_snapshot_exact_deferred_scene_hierarchy(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_exact_deferred_scene_hierarchy(snapshot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::restore_snapshot_preserving_scene_selection(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_preserving_scene_selection(snapshot);
}

ModelingCommandResult ModelingSdkProjectionAdapter::restore_snapshot_exact(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_exact(snapshot);
}

ModelingCommandResult ModelingSdkProjectionAdapter::
    restore_snapshot_exact_preserving_scene_selection(
        const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_exact_preserving_scene_selection(snapshot);
}

void ModelingSdkProjectionAdapter::set_failure_point_for_tests(
    ModelingSdkProjectionFailurePoint point) {
  impl_->set_failure_point_for_tests(point);
}

ModelingSdkProjectionStats ModelingSdkProjectionAdapter::stats() const noexcept {
  return impl_->stats();
}

} // namespace quader
