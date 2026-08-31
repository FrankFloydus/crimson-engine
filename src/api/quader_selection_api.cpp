////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_selection_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/selection/quader_selection_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

QuaderApiResult<QuaderSelectionSummary>
selection_result_from_status(QuaderApiVoidResult status,
                             ModelingSelectionSummary value) {
  return quader_api_result_from_status(status, to_api_selection_summary(value));
}

QuaderApiResult<QuaderHoverSummary>
hover_result_from_status(QuaderApiVoidResult status,
                         ModelingHoverSummary value) {
  return quader_api_result_from_status(status, to_api_hover_summary(value));
}

EditorSelectionEdit to_editor_selection_edit(QuaderSelectionEdit edit) {
  switch (edit) {
  case QuaderSelectionEdit::Add:
    return EditorSelectionEdit::Add;
  case QuaderSelectionEdit::Remove:
    return EditorSelectionEdit::Remove;
  case QuaderSelectionEdit::Toggle:
    return EditorSelectionEdit::Toggle;
  case QuaderSelectionEdit::Replace:
    return EditorSelectionEdit::Replace;
  }
  return EditorSelectionEdit::Replace;
}

ModelingSelectionEdit to_modeling_edit(QuaderSelectionEdit edit) {
  switch (edit) {
  case QuaderSelectionEdit::Add:
    return ModelingSelectionEdit::Add;
  case QuaderSelectionEdit::Remove:
    return ModelingSelectionEdit::Remove;
  case QuaderSelectionEdit::Toggle:
    return ModelingSelectionEdit::Toggle;
  case QuaderSelectionEdit::Replace:
    return ModelingSelectionEdit::Replace;
  }
  return ModelingSelectionEdit::Replace;
}

} // namespace

/**
 * Stores QuaderSelectionBoxBaseline implementation data outside the public API header.
 */
class QuaderSelectionBoxBaseline::Impl final {
public:
    Impl(EditorSelectionBoxBaseline baseline, SceneSessionId scene_session_id,
         const SceneSession *owner)
        : baseline(std::move(baseline)), scene_session_id(scene_session_id),
          owner(owner) {}

    EditorSelectionBoxBaseline baseline;
    SceneSessionId scene_session_id;
    const SceneSession *owner = nullptr;
};

QuaderSelectionBoxBaseline::QuaderSelectionBoxBaseline() = default;
QuaderSelectionBoxBaseline::~QuaderSelectionBoxBaseline() = default;

QuaderSelectionBoxBaseline::QuaderSelectionBoxBaseline(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool QuaderSelectionBoxBaseline::valid() const {
    return impl_ != nullptr;
}

QuaderSelectionApi::QuaderSelectionApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

EditorSelectionService &
QuaderSelectionApiRuntimeAccess::service(QuaderSelectionApi &api) {
  return scene_session(api.session_).selection();
}

std::vector<ResolvedSelectionTarget> resolved_targets_from_hits(
    std::span<const ViewportSceneHitCandidate> hits) {
  std::vector<ResolvedSelectionTarget> targets;
  targets.reserve(hits.size());
  for (const ViewportSceneHitCandidate &hit : hits) {
    if (std::optional<ResolvedSelectionTarget> target =
            resolved_selection_target(hit)) {
      targets.push_back(std::move(*target));
    }
  }
  return targets;
}

QuaderApiVoidResult publish_resolved_selection_result(
    QuaderApiMutationRunner &mutations, ModelingSelectionMode mode,
    ModelingCommandResult command, std::string_view label) {
  if (selection_mode_uses_scene_document(mode)) {
    return mutations.run_document_result(
        {.ok = command.ok,
         .changed = command.changed,
         .message = std::move(command.message)},
        label);
  }
  return mutations.run_modeling_result(std::move(command), label);
}

const EditorSelectionService &
QuaderSelectionApiRuntimeAccess::service(const QuaderSelectionApi &api) {
  return QuaderApiSessionInternalAccess::read_scene_session(api.session_)
      .selection();
}

QuaderApiVoidResult
QuaderSelectionApi::select_objects(std::span<const EntityId> entities,
                                   EntityId active) {
  return mutations_.run_document_result(
      scene_session(session_).selection().set_scene_selection(entities,
                                                                active),
      "Select Objects");
}

QuaderApiVoidResult
QuaderSelectionApi::select_objects(std::initializer_list<EntityId> entities,
                                   EntityId active) {
  return select_objects(
      std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult QuaderSelectionApi::select_objects_for_inspection(
    std::span<const EntityId> entities, EntityId active) {
  return mutations_.run_document_result(
      scene_session(session_).selection().set_inspection_scene_selection(
          entities, active),
      "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::select_objects_for_inspection(
    std::initializer_list<EntityId> entities, EntityId active) {
  return select_objects_for_inspection(
      std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult
QuaderSelectionApi::select_object(EntityId entity, QuaderSelectionEdit edit) {
  return mutations_.run_document_result(
      scene_session(session_).selection().edit_scene_selection(
          entity, to_editor_selection_edit(edit)),
      "Select Objects");
}

QuaderApiVoidResult
QuaderSelectionApi::select_object(QuaderModelingObjectRef object,
                                  QuaderSelectionEdit edit) {
  if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
    return mutations_.stale_scene_reference_status("Modeling object");
  }
  return select_object(object.entity, edit);
}

QuaderApiVoidResult QuaderSelectionApi::select_objects(
    std::initializer_list<QuaderModelingObjectRef> objects,
    QuaderSelectionEdit edit) {
  std::vector<EntityId> entities;
  entities.reserve(objects.size());
  for (const QuaderModelingObjectRef &object : objects) {
    if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
      return mutations_.stale_scene_reference_status("Modeling object");
    }
    entities.push_back(object.entity);
  }
  return mutations_.run_document_result(
      scene_session(session_).selection().edit_scene_selection(
          entities, to_editor_selection_edit(edit)),
      "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::select_components(
    const QuaderComponentSelectionArgs &args) {
  if (!mutations_.belongs_to_active_scene(args.object.scene_session_id)) {
    return mutations_.stale_scene_reference_status("Modeling object");
  }
  ModelingComponentSelectionRequest request;
  request.modeling_object_id = args.object.modeling_object_id;
  request.edit = to_modeling_edit(args.edit);
  switch (args.kind) {
  case QuaderComponentKind::Vertex:
    request.mode = ModelingSelectionMode::Vertex;
    request.vertices.reserve(args.vertices.size());
    for (const QuaderElementId vertex : args.vertices) {
      request.vertices.push_back(vertex);
    }
    if (args.active.kind == QuaderComponentKind::Vertex) {
      request.active_kind = ModelingActiveSelectionKind::Vertex;
      request.active_vertex = args.active.vertex;
    }
    break;
  case QuaderComponentKind::Edge:
    request.mode = ModelingSelectionMode::Edge;
    request.edges.reserve(args.edges.size());
    for (const auto &edge : args.edges) {
      request.edges.emplace_back(edge.first, edge.second);
    }
    if (args.active.kind == QuaderComponentKind::Edge) {
      request.active_kind = ModelingActiveSelectionKind::Edge;
      request.active_edge = {args.active.edge_a, args.active.edge_b};
    }
    break;
  case QuaderComponentKind::Face:
    request.mode = ModelingSelectionMode::Face;
    request.faces.reserve(args.faces.size());
    for (const QuaderElementId face : args.faces) {
      request.faces.push_back(face);
    }
    if (args.active.kind == QuaderComponentKind::Face) {
      request.active_kind = ModelingActiveSelectionKind::Face;
      request.active_face = args.active.face;
    }
    break;
  case QuaderComponentKind::Object:
    return select_object(args.object, args.edit);
  }

  return mutations_.run_modeling_result(
      scene_session(session_).selection().set_component_selection(request),
      "Select Components");
}

QuaderApiVoidResult
QuaderSelectionApi::select_vertex(QuaderModelingObjectRef object,
                                  QuaderElementId vertex,
                                  QuaderSelectionEdit edit) {
  return select_components(QuaderComponentSelectionArgs{
      .object = object,
      .kind = QuaderComponentKind::Vertex,
      .vertices = {vertex},
      .active = QuaderComponentRef{.object = object,
                                   .kind = QuaderComponentKind::Vertex,
                                   .vertex = vertex},
      .edit = edit,
  });
}

QuaderApiVoidResult
QuaderSelectionApi::select_edge(QuaderModelingObjectRef object,
                                QuaderElementId a, QuaderElementId b,
                                QuaderSelectionEdit edit) {
  return select_components(QuaderComponentSelectionArgs{
      .object = object,
      .kind = QuaderComponentKind::Edge,
      .edges = {{a, b}},
      .active = QuaderComponentRef{.object = object,
                                   .kind = QuaderComponentKind::Edge,
                                   .edge_a = a,
                                   .edge_b = b},
      .edit = edit,
  });
}

QuaderApiVoidResult
QuaderSelectionApi::select_face(QuaderModelingObjectRef object,
                                QuaderElementId face,
                                QuaderSelectionEdit edit) {
  return select_components(QuaderComponentSelectionArgs{
      .object = object,
      .kind = QuaderComponentKind::Face,
      .faces = {face},
      .active = QuaderComponentRef{.object = object,
                                   .kind = QuaderComponentKind::Face,
                                   .face = face},
      .edit = edit,
  });
}

QuaderApiVoidResult QuaderSelectionApi::select_vertices(
    QuaderModelingObjectRef object,
    std::initializer_list<QuaderElementId> vertices, QuaderSelectionEdit edit) {
  return select_components(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Vertex,
            .vertices = std::vector<QuaderElementId>(vertices.begin(), vertices.end()),
            .active = vertices.size() == 0u ? QuaderComponentRef{} : QuaderComponentRef{
                    .object = object,
                    .kind = QuaderComponentKind::Vertex,
                    .vertex = *(vertices.end() - 1),
            },
            .edit = edit,
    });
}

QuaderApiVoidResult
QuaderSelectionApi::select_faces(QuaderModelingObjectRef object,
                                 std::initializer_list<QuaderElementId> faces,
                                 QuaderSelectionEdit edit) {
  return select_components(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Face,
            .faces = std::vector<QuaderElementId>(faces.begin(), faces.end()),
            .active = faces.size() == 0u ? QuaderComponentRef{} : QuaderComponentRef{
                    .object = object,
                    .kind = QuaderComponentKind::Face,
                    .face = *(faces.end() - 1),
            },
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::clear() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().clear(), "Clear Selection");
}

QuaderApiVoidResult QuaderSelectionApi::select_all() {
  QDR_PROFILE_FUNCTION();
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_all(), "Select All");
}

QuaderApiVoidResult QuaderSelectionApi::select_inverse() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().invert(), "Select Inverse");
}

QuaderApiVoidResult QuaderSelectionApi::select_edge_direction() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_edge_direction(),
      "Select Direction");
}

QuaderApiVoidResult QuaderSelectionApi::select_edge_loops() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_edge_loops(),
      "Select Edge Loops");
}

QuaderApiVoidResult QuaderSelectionApi::select_edge_ring() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_edge_ring(),
      "Select Edge Ring");
}

QuaderApiVoidResult QuaderSelectionApi::select_faces_with_same_material() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_faces_with_same_material(),
      "Select Faces With Same Material");
}

QuaderApiVoidResult QuaderSelectionApi::select_coplanar_faces() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_coplanar_faces(),
      "Select Co-planar Faces");
}

QuaderApiVoidResult QuaderSelectionApi::select_face_normal() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_face_normal(),
      "Select Normal");
}

QuaderApiVoidResult QuaderSelectionApi::select_mesh_ngons() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_mesh_ngons(),
      "Select N-Gons");
}

QuaderApiVoidResult QuaderSelectionApi::select_similar_meshes() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().select_similar_meshes(),
      "Select Similar Meshes");
}

QuaderApiVoidResult QuaderSelectionApi::grow() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().grow(), "Grow Selection");
}

QuaderApiVoidResult QuaderSelectionApi::shrink() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().shrink(), "Shrink Selection");
}

QuaderApiVoidResult QuaderSelectionApi::set_mode(QuaderSelectionMode mode) {
  return mutations_.set_selection_mode(to_modeling_selection_mode(mode));
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::pick_select(const QuaderPickSelectRequest &request) {
  return pick_select(request, nullptr);
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::pick_scene_entity(
    EntityId entity, const QuaderPickSelectRequest &request) {
  SceneSession &session = scene_session(session_);
  QuaderApiVoidResult status = mutations_.run_document_result(
      session.selection().pick_scene_entity(
          entity, to_modeling_pick_select_request(request)),
      "Pick Select");
  return selection_result_from_status(std::move(status),
                                       session.selection().summary());
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::pick_select(
    const QuaderPickSelectRequest &request,
    const QuaderSelectionBoxBaseline &baseline) {
  return pick_select(request, &baseline);
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::pick_select(
    const QuaderPickSelectRequest &request,
    const QuaderSelectionBoxBaseline *baseline) {
  QDR_PROFILE_FUNCTION();
  if (baseline != nullptr && baseline->impl_ != nullptr &&
      (!mutations_.belongs_to_active_scene(baseline->impl_->scene_session_id) ||
       baseline->impl_->owner !=
           QuaderApiSessionInternalAccess::active_scene_session(session_))) {
    return quader_api_result_from_status<QuaderSelectionSummary>(
        mutations_.stale_scene_reference_status("Selection baseline"));
  }
  SceneSession &session = scene_session(session_);
  EditorSelectionService &selection = session.selection();
  const ModelingSelectionMode mode = selection.mode();
  const ModelingPickSelectRequest modeling_request =
      to_modeling_pick_select_request(request);
  std::vector<ResolvedSelectionHit> modeling_hits;
  if (const std::optional<ResolvedSelectionHit> hit =
          selection.query_modeling_hit(modeling_request)) {
    modeling_hits.push_back(*hit);
  }
  const bool scene_mode = selection_mode_uses_scene_document(mode);
  const SceneSurfaceQuerySnapshot scene_surfaces =
      session.scene_surface_query().snapshot();
  SceneSurfaceQueryScratch surface_scratch;
  const ViewportSceneRayHitQuery ordinary_query{
      .ray = modeling_request.ray,
      .scene_surfaces = &scene_surfaces,
      .resolved_selection_hits = modeling_hits,
      .include_mesh_surfaces = scene_mode,
      .select_backfaces = modeling_request.select_backfaces,
      .surface_scratch = &surface_scratch,
  };
  const std::optional<ViewportSceneHitCandidate> ordinary_hit =
      resolve_viewport_scene_ray_hit(ordinary_query);
  std::optional<ResolvedSelectionTarget> winner =
      ordinary_hit.has_value() ? resolved_selection_target(*ordinary_hit)
                               : std::nullopt;
  ModelingPickSelectRequest apply_policy = modeling_request;
  if (modeling_request.depth_cycle_requested(mode)) {
    std::vector<ResolvedSelectionHit> depth_modeling_hits =
        selection.query_modeling_depth_hits(modeling_request);
    if (depth_modeling_hits.empty()) {
      depth_modeling_hits = modeling_hits;
    }
    const std::vector<ViewportSceneHitCandidate> raw_depth_hits =
        collect_viewport_scene_ray_hits({
            .ray = modeling_request.ray,
            .scene_surfaces = &scene_surfaces,
            .resolved_selection_hits = depth_modeling_hits,
            .include_mesh_surfaces = scene_mode,
            .select_backfaces = modeling_request.select_backfaces,
            .include_occluded = true,
            .collect_all_mesh_surfaces = true,
            .surface_scratch = &surface_scratch,
        });
    const std::vector<ViewportSceneHitCandidate> depth_hits =
        resolve_viewport_scene_marquee_hits(raw_depth_hits);
    const std::vector<ResolvedSelectionTarget> depth_targets =
        resolved_targets_from_hits(depth_hits);
    winner = selection.resolve_depth_arbitrated_target(
        depth_targets, std::move(winner), modeling_request);
    apply_policy.preserve_selection_on_miss = true;
  }
  ResolvedSelectionApplyResult applied = selection.apply_resolved_click({
      .winner = std::move(winner),
      .policy = apply_policy,
      .baseline = baseline != nullptr && baseline->impl_ != nullptr
                      ? std::optional<EditorSelectionBoxBaseline>(
                            baseline->impl_->baseline)
                      : std::nullopt,
  });
  QuaderApiVoidResult status = publish_resolved_selection_result(
      mutations_, mode, std::move(applied.command), "Pick Select");
  return selection_result_from_status(std::move(status),
                                      std::move(applied.selection));
}

QuaderSelectionBoxBaseline
QuaderSelectionApi::capture_box_selection_baseline() const {
  QDR_PROFILE_SCOPE("Box selection API capture baseline");
  return QuaderSelectionBoxBaseline(
      std::shared_ptr<const QuaderSelectionBoxBaseline::Impl>(
          new QuaderSelectionBoxBaseline::Impl(
              scene_session(session_)
                  .selection()
                  .capture_box_selection_baseline(),
              mutations_.scene_session_id(), &scene_session(session_))));
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::box_select(const QuaderBoxSelectRequest &request) {
  return box_select(request, nullptr);
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::box_select(const QuaderBoxSelectRequest &request,
                               const QuaderSelectionBoxBaseline &baseline) {
  return box_select(request, &baseline);
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::box_select(
    const QuaderBoxSelectRequest &request,
    const QuaderSelectionBoxBaseline *baseline) {
  QDR_PROFILE_FUNCTION();
  if (baseline != nullptr && baseline->impl_ != nullptr &&
      (!mutations_.belongs_to_active_scene(baseline->impl_->scene_session_id) ||
       baseline->impl_->owner !=
           QuaderApiSessionInternalAccess::active_scene_session(session_))) {
    return quader_api_result_from_status<QuaderSelectionSummary>(
        mutations_.stale_scene_reference_status("Selection baseline"));
  }
  SceneSession &session = scene_session(session_);
  EditorSelectionService &selection = session.selection();
  const ModelingSelectionMode mode = selection.mode();
  const ModelingBoxSelectRequest modeling_request =
      to_modeling_box_select_request(request);
  const EditorSelectionBoxBaseline effective_baseline =
      baseline != nullptr && baseline->impl_ != nullptr
          ? baseline->impl_->baseline
          : selection.capture_box_selection_baseline();
  const bool scene_mode = selection_mode_uses_scene_document(mode);
  const std::vector<ResolvedSelectionHit> modeling_hits =
      selection.query_modeling_marquee_hits(modeling_request);
  const ViewportLocalVisibilitySnapshot local_visibility =
      session.viewport_local_visibility().snapshot();
  const std::vector<ViewportSceneBillboardPickCandidate> billboards =
      scene_mode ? ViewportSceneProjection::point_light_pick_candidates(
                       session.scene(), local_visibility)
                 : std::vector<ViewportSceneBillboardPickCandidate>{};
  const std::span<const ViewportSceneCurvePickCandidate> curves =
      scene_mode
          ? session.modeling().scene_curve_pick_candidates()
          : std::span<const ViewportSceneCurvePickCandidate>{};
  const SceneSurfaceQuerySnapshot scene_surfaces =
      session.scene_surface_query().snapshot();
  SceneSurfaceQueryScratch surface_scratch;
  const std::vector<ViewportSceneHitCandidate> hits =
      collect_viewport_scene_marquee_hits({
          .camera = modeling_request.camera,
          .start = modeling_request.start,
          .end = modeling_request.end,
          .rule = modeling_request.rule,
          .billboards = billboards,
          .curves = curves,
          .scene_surfaces = &scene_surfaces,
          .resolved_selection_hits = modeling_hits,
          .include_occluded_resolved_modeling_hits =
              modeling_request.select_backfaces,
          .surface_scratch = &surface_scratch,
      });
  ResolvedSelectionApplyResult applied = selection.apply_resolved_marquee({
      .targets = resolved_targets_from_hits(hits),
      .edit = modeling_request.edit,
      .mode = mode,
      .rule = modeling_request.rule,
      .baseline = effective_baseline,
  });
  QuaderApiVoidResult status = publish_resolved_selection_result(
      mutations_, mode, std::move(applied.command), "Box Select");
  return selection_result_from_status(std::move(status),
                                      std::move(applied.selection));
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hover(const QuaderHoverRequest& request) {
    QDR_PROFILE_FUNCTION();
    QuaderApiVoidResult status = mutations_.run_modeling_result(
        scene_session(session_).selection().update_hover(
            to_modeling_hover_request(request)),
        "Update Hover");
    return hover_result_from_status(
        std::move(status),
        scene_session(session_).selection().hover_summary());
}

QuaderApiResult<QuaderHoverSummary>
QuaderSelectionApi::hover_vertex(QuaderModelingObjectRef object,
                                 QuaderElementId vertex) {
  return direct_hover(object, QuaderActiveSelectionKind::Vertex, vertex, -1, -1,
                      -1);
}

QuaderApiResult<QuaderHoverSummary>
QuaderSelectionApi::hover_edge(QuaderModelingObjectRef object,
                               QuaderElementId a, QuaderElementId b) {
  return direct_hover(object, QuaderActiveSelectionKind::Edge, -1, a, b, -1);
}

QuaderApiResult<QuaderHoverSummary>
QuaderSelectionApi::hover_face(QuaderModelingObjectRef object,
                               QuaderElementId face) {
  return direct_hover(object, QuaderActiveSelectionKind::Face, -1, -1, -1,
                      face);
}

QuaderApiVoidResult QuaderSelectionApi::clear_hover() {
  return mutations_.run_modeling_result(
      scene_session(session_).selection().clear_hover(), "Clear Hover");
}

QuaderSelectionSummary QuaderSelectionApi::summary() const {
  return to_api_selection_summary(
      scene_session(session_).selection().summary());
}

QuaderHoverSummary QuaderSelectionApi::hover_summary() const {
  return to_api_hover_summary(
      scene_session(session_).selection().hover_summary());
}

QuaderHoverSummary
QuaderSelectionApi::preview_hover(const QuaderHoverRequest &request) const {
  return to_api_hover_summary(
      scene_session(session_).selection().preview_hover(
          to_modeling_hover_request(request)));
}

QuaderApiResult<QuaderHoverSummary>
QuaderSelectionApi::direct_hover(QuaderModelingObjectRef object,
                                 QuaderActiveSelectionKind kind,
                                 QuaderElementId vertex, QuaderElementId edge_a,
                                 QuaderElementId edge_b, QuaderElementId face) {
  if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
    return quader_api_result_from_status<QuaderHoverSummary>(
        mutations_.stale_scene_reference_status("Modeling object"), {});
  }
  ModelingDirectHoverRequest request;
  request.modeling_object_id = object.modeling_object_id;
  request.vertex_id = vertex;
  request.edge = {edge_a, edge_b};
  request.face_id = face;
  switch (kind) {
  case QuaderActiveSelectionKind::Vertex:
    request.kind = ModelingActiveSelectionKind::Vertex;
    break;
  case QuaderActiveSelectionKind::Edge:
    request.kind = ModelingActiveSelectionKind::Edge;
    break;
  case QuaderActiveSelectionKind::Face:
    request.kind = ModelingActiveSelectionKind::Face;
    break;
  case QuaderActiveSelectionKind::None:
  case QuaderActiveSelectionKind::Object:
  case QuaderActiveSelectionKind::Light:
    request.kind = ModelingActiveSelectionKind::None;
    break;
  }
  QuaderApiVoidResult status = mutations_.run_modeling_result(
      scene_session(session_).selection().set_hover(request), "Update Hover");
  return hover_result_from_status(
      std::move(status), scene_session(session_).selection().hover_summary());
}

} // namespace quader
