////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_dev_tools_api.hpp"

#ifndef NDEBUG

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

std::string_view selection_mode_name(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return "vertex";
  case ModelingSelectionMode::Edge:
    return "edge";
  case ModelingSelectionMode::Face:
    return "face";
  case ModelingSelectionMode::Object:
    return "object";
  case ModelingSelectionMode::Group:
    return "group";
  }
  return "object";
}

std::string_view active_kind_name(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::Object:
    return "object";
  case ModelingActiveSelectionKind::Vertex:
    return "vertex";
  case ModelingActiveSelectionKind::Edge:
    return "edge";
  case ModelingActiveSelectionKind::Face:
    return "face";
  case ModelingActiveSelectionKind::Light:
    return "light";
  case ModelingActiveSelectionKind::None:
    return "none";
  }
  return "none";
}

std::string_view polygon_selection_mode_name(quader_poly::SelectionMode mode) {
  switch (mode) {
  case quader_poly::SelectionMode::Vertex:
    return "vertex";
  case quader_poly::SelectionMode::Edge:
    return "edge";
  case quader_poly::SelectionMode::Face:
    return "face";
  }
  return "vertex";
}

std::string_view polygon_active_kind_name(quader_poly::ElementKind kind) {
  switch (kind) {
  case quader_poly::ElementKind::Vertex:
    return "vertex";
  case quader_poly::ElementKind::Edge:
    return "edge";
  case quader_poly::ElementKind::Face:
    return "face";
  }
  return "vertex";
}

Json vec3_json(QVec3 value) {
  return Json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

Json edge_json(quader_poly::ElementId a, quader_poly::ElementId b) {
  return Json{{"a", a}, {"b", b}};
}

Json edge_json(quader_poly::Edge edge) { return edge_json(edge.a, edge.b); }

Json active_selection_json(const ModelingSelectionSummary &summary) {
  Json active = Json::object();
  active["kind"] = active_kind_name(summary.active_kind);
  active["modelingObjectId"] = summary.active_object_id;
  switch (summary.active_kind) {
  case ModelingActiveSelectionKind::Vertex:
    active["vertex"] = summary.active_vertex_id;
    break;
  case ModelingActiveSelectionKind::Edge:
    active["edge"] = edge_json(static_cast<quader_poly::ElementId>(
                                   summary.active_edge_a),
                               static_cast<quader_poly::ElementId>(
                                   summary.active_edge_b));
    break;
  case ModelingActiveSelectionKind::Face:
    active["face"] = summary.active_face_id;
    break;
  case ModelingActiveSelectionKind::Light:
  case ModelingActiveSelectionKind::Object:
  case ModelingActiveSelectionKind::None:
    break;
  }
  return active;
}

Json polygon_active_selection_json(const quader_poly::Selection &selection) {
  Json active = Json::object();
  if (!selection.has_active) {
    active["kind"] = "none";
    return active;
  }
  active["kind"] = polygon_active_kind_name(selection.active_kind);
  switch (selection.active_kind) {
  case quader_poly::ElementKind::Vertex:
    active["vertex"] = selection.active_vertex;
    break;
  case quader_poly::ElementKind::Edge:
    active["edge"] = edge_json(selection.active_edge);
    break;
  case quader_poly::ElementKind::Face:
    active["face"] = selection.active_face;
    break;
  }
  return active;
}

Json polygon_selection_json(const quader_poly::Selection &selection) {
  Json vertices = Json::array();
  for (const quader_poly::ElementId vertex : selection.vertices) {
    vertices.push_back(vertex);
  }

  Json edges = Json::array();
  for (const quader_poly::Edge edge : selection.edges) {
    edges.push_back(edge_json(edge));
  }

  Json faces = Json::array();
  for (const quader_poly::ElementId face : selection.faces) {
    faces.push_back(face);
  }

  return Json{{"mode", polygon_selection_mode_name(selection.mode)},
              {"vertices", std::move(vertices)},
              {"edges", std::move(edges)},
              {"faces", std::move(faces)},
              {"active", polygon_active_selection_json(selection)}};
}

Json selected_geometry_json(const ModelingSelectionExportObject &object) {
  Json vertices = Json::array();
  for (const ModelingSelectionExportVertex &vertex :
       object.selected_vertices) {
    vertices.push_back(Json{{"id", vertex.id},
                            {"position", vec3_json(vertex.position)}});
  }

  Json edges = Json::array();
  for (const ModelingSelectionExportEdge &edge : object.selected_edges) {
    Json edge_value = edge_json(edge.a, edge.b);
    edge_value["aPosition"] =
        edge.a_position.has_value() ? vec3_json(*edge.a_position) : Json();
    edge_value["bPosition"] =
        edge.b_position.has_value() ? vec3_json(*edge.b_position) : Json();
    edges.push_back(std::move(edge_value));
  }

  Json faces = Json::array();
  for (const ModelingSelectionExportFace &face : object.selected_faces) {
    Json vertices_value = Json::array();
    for (const quader_poly::ElementId vertex : face.vertices) {
      vertices_value.push_back(vertex);
    }
    faces.push_back(Json{{"id", face.id},
                         {"vertices", std::move(vertices_value)}});
  }

  return Json{{"vertices", std::move(vertices)},
              {"edges", std::move(edges)},
              {"faces", std::move(faces)}};
}

Json selection_export_object_json(const ModelingSelectionExportObject &object) {
  return Json{{"modelingObjectId", object.modeling_object_id},
              {"entity", object.entity},
              {"entityUuid", object.entity_uuid},
              {"name", object.name},
              {"selected", object.selected},
              {"selection", polygon_selection_json(object.selection)},
              {"selectedGeometry", selected_geometry_json(object)}};
}

Json selection_export_json(const ModelingSelectionExport &selection) {
  Json objects = Json::array();
  for (const ModelingSelectionExportObject &object : selection.objects) {
    objects.push_back(selection_export_object_json(object));
  }

  const ModelingSelectionSummary &summary = selection.summary;
  return Json{
      {"kind", "quader.dev.selection"},
      {"version", 1},
      {"selection",
       Json{{"mode", selection_mode_name(summary.mode)},
            {"hasSelection", summary.has_selection},
            {"active", active_selection_json(summary)},
            {"counts",
             Json{{"objects", summary.selected_object_count},
                  {"vertices", summary.selected_vertex_count},
                  {"edges", summary.selected_edge_count},
                  {"faces", summary.selected_face_count}}},
            {"revisions",
             Json{{"content", summary.content_revision},
                  {"selection", summary.selection_revision}}},
            {"objects", std::move(objects)}}}};
}

} // namespace

QuaderDevToolsApi::QuaderDevToolsApi(QuaderApiSession session)
    : session_(session) {}

void QuaderDevToolsApi::set_runtime_hooks(QuaderDevToolsRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

std::string QuaderDevToolsApi::current_selection_json() const {
  const ModelingSelectionExport selection =
      read_scene_session(session_).modeling().export_selection();
  return selection_export_json(selection).dump(2);
}

QuaderApiVoidResult QuaderDevToolsApi::copy_selection_json_to_clipboard() {
  SceneSession &session = scene_session(session_);
  if (!hooks_.set_clipboard_text) {
    return quader_api_failure_status(
        session, "Dev tools clipboard runtime is not attached.");
  }

  std::string error_message;
  const std::string json = current_selection_json();
  if (!hooks_.set_clipboard_text(json, error_message)) {
    if (error_message.empty()) {
      error_message = "Unknown clipboard error.";
    }
    return quader_api_failure_status(
        session, "Could not copy selection JSON to clipboard: " +
                     std::move(error_message));
  }

  return quader_api_void_status(session, true, false,
                                "Copied selection JSON to clipboard.",
                                QuaderMutationKind::None);
}

} // namespace quader

#endif
