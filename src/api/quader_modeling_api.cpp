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
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/glm_bridge.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <span>
#include <utility>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

QuaderApiResult<QuaderModelingObjectRef>
result_from_create(QuaderApiVoidResult status, QuaderModelingObjectRef value) {
  return quader_api_result_from_status(status, value);
}

QuaderApiResult<QuaderTransformDragPreview>
result_from_transform_drag_preview(const ProjectSession &session,
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
  return quader_api_failure_status(project_session(session),
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

bool entity_selection_owns_transforms(ProjectSession &session) {
  const ModelingSelectionSummary summary = session.selection().summary();
  return summary.has_selection &&
         selection_mode_uses_scene_document(summary.mode);
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
mesh_arrangement_selection_context(ProjectSession &session) {
  const ModelingSelectionSummary summary =
      session.modeling().selection_summary();
  const std::optional<EntityId> active_mesh =
      summary.active_object_id > 0
          ? session.modeling().entity_for_modeling_object(
                summary.active_object_id)
          : std::nullopt;
  return {
      .object_mode = summary.mode == ModelingSelectionMode::Object,
      .active_mesh = active_mesh.value_or(kInvalidEntityId),
  };
}

} // namespace

QuaderPreviewHandle::QuaderPreviewHandle(
    std::optional<QuaderApiSession> session, QuaderApiMutationRunner &mutations,
    std::string default_label, QuaderApiVoidResult begin_status)
    : session_(std::move(session)), mutations_(&mutations),
      default_label_(std::move(default_label)),
      begin_status_(std::move(begin_status)) {}

bool QuaderPreviewHandle::valid() const {
  return session_.has_value() && session_->valid() && mutations_ != nullptr &&
         begin_status_.ok();
}

const QuaderApiVoidResult &QuaderPreviewHandle::begin_result() const {
  return begin_status_;
}

QuaderApiVoidResult
QuaderPreviewHandle::update(const QuaderPreviewSettings &settings) {
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
      project_session(session_).modeling(),
      ModelingCreateBoxRequest{
          .bounds = to_modeling_bounds(args.bounds),
          .name = args.name,
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
  auto command = std::make_unique<CommitBoxPreviewCommand>(
      project_session(session_).modeling(), project_session(session_).scene(),
      std::move(name), default_material);
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
  auto command = std::make_unique<CreateMeshHistoryCommand>(
      project_session(session_).modeling(), std::move(request));
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
  ProjectSession &session = project_session(session_);
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
          .source_entity = args.source.entity,
          .source_modeling_object_id = args.source.modeling_object_id,
          .endpoint_world = args.endpoint_world,
          .count = args.count,
      });
  LinearArrayHistoryCommand *array_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Linear Array");

  QuaderLinearArrayResult created;
  if (status.ok && status.changed) {
    created.elements.reserve(array_command->created().elements.size());
    for (const ModelingObjectRecord &record :
         array_command->created().elements) {
      created.elements.push_back(
          ref_from_record(to_api_modeling_object_record(record)));
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
      quader_api_void_status(project_session(session_), true, false, {},
                             QuaderMutationKind::None),
      QuaderMeshHandle(session_, mutations_, *object));
}

QuaderApiVoidResult
QuaderModelingApi::set_selection_mode(QuaderSelectionMode mode) {
  return mutations_.run_modeling_result(
      project_session(session_).selection().set_mode(
          to_modeling_selection_mode(mode)),
      "Set Selection Mode");
}

bool QuaderModelingApi::can_execute_tool(std::string_view tool_id) const {
  return mutations_.can_run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::execute_tool(std::string_view tool_id) {
  return mutations_.run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::delete_selection() {
  return mutations_.run_tracked_modeling_tool("delete_selection");
}

QuaderApiVoidResult QuaderModelingApi::copy_selection() {
  return mutations_.run_tracked_modeling_tool("copy_selection");
}

QuaderApiVoidResult QuaderModelingApi::paste_selection() {
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

bool QuaderModelingApi::can_align_selected_meshes() const {
  ProjectSession &session = project_session(session_);
  MeshArrangementService arrangement(
      session.scene(), session.assets(),
      mesh_arrangement_selection_context(session),
      session.document_commands());
  return arrangement.can_align_selected_meshes();
}

bool QuaderModelingApi::can_distribute_selected_meshes() const {
  ProjectSession &session = project_session(session_);
  MeshArrangementService arrangement(
      session.scene(), session.assets(),
      mesh_arrangement_selection_context(session),
      session.document_commands());
  return arrangement.can_distribute_selected_meshes();
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
  ProjectSession &session = project_session(session_);
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
  ProjectSession &session = project_session(session_);
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
      project_session(session_).modeling().set_interaction_settings(
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
  return project_session(session_)
      .modeling()
      .can_cancel_active_tool_or_preview();
}

QuaderApiVoidResult
QuaderModelingApi::begin_transform_preview(bool duplicate_selected) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().begin_transform_preview(
          duplicate_selected),
      "Begin Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::update_transform_translate(QVec3 delta) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_translate_preview(
          delta),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_matrix(const QuaderTransform3 &transform,
                                           QVec3 pivot) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_matrix_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_extrude(QVec3 offset,
                                            float closed_edge_ledge_size) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_extrude_preview(
          offset, closed_edge_ledge_size),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_inset(const QuaderTransform3 &transform,
                                          QVec3 pivot) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_inset_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::commit_transform_preview(std::string label) {
  return mutations_.run_modeling_command(
      std::make_unique<CommitTransformPreviewHistoryCommand>(
          project_session(session_).modeling()),
      label.empty() ? "Commit Transform Preview" : label);
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_preview() {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().cancel_transform_preview(),
      "Cancel Transform Preview");
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::begin_transform_drag(
    const QuaderTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().begin_transform_drag(
          to_modeling_transform_drag_begin_request(request)));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::update_transform_drag(
    const QuaderTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().update_transform_drag(
          to_modeling_transform_drag_update_request(request)));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::commit_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().commit_transform_drag(
          project_session(session_).history()));
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_drag() {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().cancel_transform_drag(),
      "Cancel Transform Drag");
}

QuaderApiVoidResult QuaderModelingApi::move_selection(QVec3 delta) {
  ProjectSession &session = project_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    return mutations_.run_document_result(
        transforms.move_selected_by_world_delta(session.document_commands(),
                                                session.scene(), delta,
                                                "Move Selection"),
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
  ProjectSession &session = project_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    return mutations_.run_document_result(
        transforms.transform_selected_by_world_delta(
            session.document_commands(), session.scene(),
            world_transform_delta_from_api_transform(transform, pivot),
            "Transform Selection"),
        "Transform Selection");
  }

  return mutations_.run_modeling_command(
      std::make_unique<TransformSelectionHistoryCommand>(
          session.modeling(), to_modeling_transform3(transform), pivot),
      "Transform Selection");
}

bool QuaderModelingApi::can_duplicate_selected_entity_hierarchy() const {
  QDR_PROFILE_FUNCTION();
  const ProjectSession &session = project_session(session_);
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
  ProjectSession &session = project_session(session_);
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Selection");
  }
  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate selected entity hierarchy owner");
    return session.modeling().duplicate_selected_entity_hierarchy(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
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
  ProjectSession &session = project_session(session_);
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Group Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Group Selection");
  }

  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate group hierarchy owner");
    return session.modeling().duplicate_selected_hierarchy_for_group_drag(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }

  {
    QDR_PROFILE_SCOPE("Duplicate group commit transaction");
    return mutations_.run_document_command(transaction.commit(),
                                           "Duplicate Group Selection");
  }
}

QuaderApiVoidResult
QuaderModelingApi::apply_active_material(std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<ApplyActiveMaterialHistoryCommand>(
          project_session(session_).modeling(), material_slot),
      "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(
          project_session(session_).modeling(), material_slot),
      "Assign Material Slot");
}

QuaderApiVoidResult
QuaderModelingApi::assign_material_slot_to_face(QuaderModelingObjectRef object,
                                                QuaderElementId face,
                                                std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToFaceHistoryCommand>(
          project_session(session_).modeling(), object.modeling_object_id, face,
          material_slot),
      "Assign Face Material Slot");
}

QuaderApiVoidResult
QuaderModelingApi::cut_selected_meshes_by_hovered_face_plane() {
  return mutations_.run_modeling_command(
      std::make_unique<CutSelectedMeshesByHoveredFacePlaneHistoryCommand>(
          project_session(session_).modeling()),
      "Cut By Face Plane");
}

QuaderActionSet
QuaderModelingApi::available_action_set(const QuaderActionQuery &query) const {
  return to_api_action_set(
      project_session(session_).modeling().context_action_set(
          to_modeling_action_query(query)));
}

std::vector<QuaderActionItem>
QuaderModelingApi::available_actions(const QuaderActionQuery &query) const {
  const std::vector<ModelingActionItem> actions =
      project_session(session_).modeling().context_actions(
          to_modeling_action_query(query));
  std::vector<QuaderActionItem> result;
  result.reserve(actions.size());
  for (const ModelingActionItem &action : actions) {
    result.push_back(to_api_action_item(action));
  }
  return result;
}

std::vector<QuaderInstantOperationDescriptor>
QuaderModelingApi::instant_operations() const {
  const std::vector<ModelingInstantOperationDescriptor> operations =
      project_session(session_).modeling().instant_operations();
  std::vector<QuaderInstantOperationDescriptor> result;
  result.reserve(operations.size());
  for (const ModelingInstantOperationDescriptor &operation : operations) {
    result.push_back(to_api_instant_operation_descriptor(operation));
  }
  return result;
}

QuaderSelectionSummary QuaderModelingApi::selection_summary() const {
  return to_api_selection_summary(
      project_session(session_).selection().summary());
}

std::vector<QuaderModelingObjectRecord> QuaderModelingApi::objects() const {
  const std::vector<ModelingObjectRecord> records =
      project_session(session_).modeling().objects();
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
  ref.entity = record.entity;
  ref.entity_uuid = project_session(session_).scene().uuid(record.entity);
  ref.editor_object_id =
      project_session(session_).scene().editor_object_id(record.entity);
  ref.modeling_object_id = record.modeling_object_id;
  ref.mesh = record.mesh;
  if (const MaterialComponent *material =
          project_session(session_).scene().material(record.entity)) {
    ref.material = material->material;
  }
  return ref;
}

} // namespace quader
