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
#include "scene/document/project_session.hpp"
#include "editor/selection/editor_selection_service.hpp"

#include <memory>
#include <utility>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
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
    explicit Impl(EditorSelectionBoxBaseline baseline)
        : baseline(std::move(baseline)) {}

    EditorSelectionBoxBaseline baseline;
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

QuaderApiVoidResult
QuaderSelectionApi::select_objects(std::span<const EntityId> entities,
                                   EntityId active) {
  return mutations_.run_document_result(
      project_session(session_).selection().set_scene_selection(entities,
                                                                active),
      "Select Objects");
}

QuaderApiVoidResult
QuaderSelectionApi::select_objects(std::initializer_list<EntityId> entities,
                                   EntityId active) {
  return select_objects(
      std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult
QuaderSelectionApi::select_object(EntityId entity, QuaderSelectionEdit edit) {
  return mutations_.run_document_result(
      project_session(session_).selection().edit_scene_selection(
          entity, to_editor_selection_edit(edit)),
      "Select Objects");
}

QuaderApiVoidResult
QuaderSelectionApi::select_object(QuaderModelingObjectRef object,
                                  QuaderSelectionEdit edit) {
  return select_object(object.entity, edit);
}

QuaderApiVoidResult QuaderSelectionApi::select_objects(
    std::initializer_list<QuaderModelingObjectRef> objects,
    QuaderSelectionEdit edit) {
  std::vector<EntityId> entities;
  entities.reserve(objects.size());
  for (const QuaderModelingObjectRef &object : objects) {
    entities.push_back(object.entity);
  }
  return mutations_.run_document_result(
      project_session(session_).selection().edit_scene_selection(
          entities, to_editor_selection_edit(edit)),
      "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::select_components(
    const QuaderComponentSelectionArgs &args) {
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
      project_session(session_).selection().set_component_selection(request),
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
      project_session(session_).selection().clear(), "Clear Selection");
}

QuaderApiVoidResult QuaderSelectionApi::select_all() {
  QDR_PROFILE_FUNCTION();
  return mutations_.run_modeling_result(
      project_session(session_).selection().select_all(), "Select All");
}

QuaderApiVoidResult QuaderSelectionApi::invert() {
  return mutations_.run_modeling_result(
      project_session(session_).selection().invert(), "Invert Selection");
}

QuaderApiVoidResult QuaderSelectionApi::set_mode(QuaderSelectionMode mode) {
  return mutations_.run_modeling_result(
      project_session(session_).selection().set_mode(
          to_modeling_selection_mode(mode)),
      "Set Selection Mode");
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::pick_select(const QuaderPickSelectRequest &request) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before =
      project_session(session_).selection().summary();
  const ModelingSelectionSummary picked =
      project_session(session_).selection().pick_select(
          to_modeling_pick_select_request(request));
  const ModelingSelectionSummary after =
      project_session(session_).selection().summary();
  const bool changed = before.content_revision != after.content_revision ||
                       before.selection_revision != after.selection_revision;
  QuaderApiVoidResult status = mutations_.run_modeling_result(
      ModelingCommandResult{.ok = true, .changed = changed}, "Pick Select");
  return selection_result_from_status(std::move(status), picked);
}

QuaderSelectionBoxBaseline
QuaderSelectionApi::capture_box_selection_baseline() const {
  return QuaderSelectionBoxBaseline(
      std::shared_ptr<const QuaderSelectionBoxBaseline::Impl>(
          new QuaderSelectionBoxBaseline::Impl(
              project_session(session_)
                  .selection()
                  .capture_box_selection_baseline())));
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::box_select(const QuaderBoxSelectRequest &request) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before =
      project_session(session_).selection().summary();
  const ModelingSelectionSummary selected =
      project_session(session_).selection().box_select(
          to_modeling_box_select_request(request));
  const ModelingSelectionSummary after =
      project_session(session_).selection().summary();
  const bool changed = before.content_revision != after.content_revision ||
                       before.selection_revision != after.selection_revision;
  QuaderApiVoidResult status = mutations_.run_modeling_result(
      ModelingCommandResult{.ok = true, .changed = changed}, "Box Select");
  return selection_result_from_status(std::move(status), selected);
}

QuaderApiResult<QuaderSelectionSummary>
QuaderSelectionApi::box_select(const QuaderBoxSelectRequest &request,
                               const QuaderSelectionBoxBaseline &baseline) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before =
      project_session(session_).selection().summary();
  const ModelingSelectionSummary selected =
      baseline.impl_ != nullptr
          ? project_session(session_).selection().box_select(
                to_modeling_box_select_request(request),
                baseline.impl_->baseline)
          : project_session(session_).selection().box_select(
                to_modeling_box_select_request(request));
  const ModelingSelectionSummary after =
      project_session(session_).selection().summary();
  const bool changed = before.content_revision != after.content_revision ||
                       before.selection_revision != after.selection_revision;
  QuaderApiVoidResult status = mutations_.run_modeling_result(
      ModelingCommandResult{.ok = true, .changed = changed}, "Box Select");
  return selection_result_from_status(std::move(status), selected);
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hover(const QuaderHoverRequest& request) {
    QDR_PROFILE_FUNCTION();
    QuaderApiVoidResult status = mutations_.run_modeling_result(
        project_session(session_).selection().update_hover(
            to_modeling_hover_request(request)),
        "Update Hover");
    return hover_result_from_status(
        std::move(status),
        project_session(session_).selection().hover_summary());
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
      project_session(session_).selection().clear_hover(), "Clear Hover");
}

QuaderSelectionSummary QuaderSelectionApi::summary() const {
  return to_api_selection_summary(
      project_session(session_).selection().summary());
}

QuaderHoverSummary QuaderSelectionApi::hover_summary() const {
  return to_api_hover_summary(
      project_session(session_).selection().hover_summary());
}

QuaderHoverSummary
QuaderSelectionApi::preview_hover(const QuaderHoverRequest &request) const {
  return to_api_hover_summary(
      project_session(session_).selection().preview_hover(
          to_modeling_hover_request(request)));
}

QuaderApiResult<QuaderHoverSummary>
QuaderSelectionApi::direct_hover(QuaderModelingObjectRef object,
                                 QuaderActiveSelectionKind kind,
                                 QuaderElementId vertex, QuaderElementId edge_a,
                                 QuaderElementId edge_b, QuaderElementId face) {
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
      project_session(session_).selection().set_hover(request), "Update Hover");
  return hover_result_from_status(
      std::move(status), project_session(session_).selection().hover_summary());
}

} // namespace quader
