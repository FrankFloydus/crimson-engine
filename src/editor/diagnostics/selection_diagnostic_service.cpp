////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/diagnostics/selection_diagnostic_service.hpp"

#include "quader_app_version.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;
using EdgeKey = std::pair<quader_poly::ElementId, quader_poly::ElementId>;

struct CapturedObject final {
  ModelingSelectionExportObject selected;
  quader_poly::Selection source_selection;
  TransformComponent transform;
  QMat4 world_from_object;
  MaterialComponent material;
  AssetId mesh_asset = kInvalidAssetId;
  modeling::ObjectId sdk_object_id;
  std::optional<ModelingPolygonDocumentReadSnapshot> polygon;
  std::optional<ModelingPolygonDocumentReadSnapshot> committed_polygon;
  bool suppressed_by_preview = false;
  std::string geometry_error;
};

struct CapturedSelection final {
  std::string id;
  std::string captured_utc;
  SceneSessionId scene_session_id;
  std::uint64_t document_binding_generation = 0;
  SceneContentRevisionStamp revisions;
  std::uint64_t selection_revision = 0;
  std::uint64_t scene_selection_revision = 0;
  std::uint64_t history_revision = 0;
  EditorSelectionState selection;
  std::vector<CapturedObject> objects;
  std::vector<ModelingActionItem> operations;
  std::string project_id;
  std::string project_name;
  std::filesystem::path project_root;
  std::filesystem::path project_manifest;
  std::string scene_id;
  std::string scene_name;
  std::string scene_path;
  std::uint64_t project_generation = 0;
  std::uint64_t activation_generation = 0;
  bool preview_active = false;
};

std::string path_text(const std::filesystem::path &path) {
  const auto utf8 = path.u8string();
  return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

std::tm utc_time(std::time_t time) {
  std::tm result{};
#ifdef _WIN32
  gmtime_s(&result, &time);
#else
  gmtime_r(&time, &result);
#endif
  return result;
}

std::string capture_time() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  const auto time = utc_time(now);
  std::ostringstream text;
  text << std::put_time(&time, "%Y-%m-%dT%H:%M:%SZ");
  return text.str();
}

std::string make_dump_id(std::string_view captured_utc) {
  // Project UUID generation is already backed by the platform's random UUID owner.
  const auto uuid = ProjectId::generate();
  std::string suffix;
  for (const char value : uuid.str()) {
    if (value != '-' && suffix.size() < 8u) {
      suffix.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(value))));
    }
  }
  return "SEL-" + std::string(captured_utc.substr(0, 4)) +
         std::string(captured_utc.substr(5, 2)) + std::string(captured_utc.substr(8, 2)) + "-" + suffix;
}

Json vec2_json(QVec2 value) { return Json::array({value.x, value.y}); }
Json vec3_json(QVec3 value) { return Json::array({value.x, value.y, value.z}); }

Json component_id_json(quader_poly::ElementId index) {
  // Polygon SDK IDs use generation 1 and monotonic allocation high-water marks.
  return Json{{"index", index}, {"generation", index == 0u ? 0u : 1u}};
}

Json edge_json(quader_poly::Edge edge) {
  return Json{{"a", edge.a}, {"b", edge.b},
              {"a_id", component_id_json(edge.a)}, {"b_id", component_id_json(edge.b)}};
}

std::string_view mode_name(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex: return "vertex";
  case ModelingSelectionMode::Edge: return "edge";
  case ModelingSelectionMode::Face: return "face";
  case ModelingSelectionMode::Object: return "object";
  case ModelingSelectionMode::Group: return "group";
  }
  return "unknown";
}

std::string_view active_name(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None: return "none";
  case ModelingActiveSelectionKind::Object: return "object";
  case ModelingActiveSelectionKind::Vertex: return "vertex";
  case ModelingActiveSelectionKind::Edge: return "edge";
  case ModelingActiveSelectionKind::Face: return "face";
  case ModelingActiveSelectionKind::Light: return "light";
  }
  return "unknown";
}

Json material_json(MaterialReference material) {
  if (material.project_owned()) return Json{{"kind", "project"}, {"uuid", material.project_asset().str()}};
  if (material.embedded()) return Json{{"kind", "embedded"}, {"asset_id", material.embedded_asset()}};
  return Json{{"kind", "none"}};
}

Json polygon_selection_json(const quader_poly::Selection &selection) {
  Json edges = Json::array();
  for (const auto edge : selection.edges) edges.push_back(edge_json(edge));
  const auto mode = selection.mode == quader_poly::SelectionMode::Face ? "face"
      : selection.mode == quader_poly::SelectionMode::Edge ? "edge" : "vertex";
  return Json{{"mode", mode}, {"vertices", selection.vertices}, {"edges", std::move(edges)},
              {"faces", selection.faces},
              {"active", Json{{"present", selection.has_active},
                              {"vertex", selection.active_vertex},
                              {"edge", edge_json(selection.active_edge)},
                              {"face", selection.active_face},
                              {"kind", static_cast<int>(selection.active_kind)}}}};
}

quader_poly::Selection captured_polygon_selection(const modeling::PolygonSelectionSnapshot &snapshot) {
  quader_poly::Selection selection;
  switch (snapshot.kind) {
  case modeling::SelectionKind::Vertex:
  case modeling::SelectionKind::Object:
    selection.mode = quader_poly::SelectionMode::Vertex;
    selection.active_kind = quader_poly::ElementKind::Vertex;
    break;
  case modeling::SelectionKind::Edge:
    selection.mode = quader_poly::SelectionMode::Edge;
    selection.active_kind = quader_poly::ElementKind::Edge;
    break;
  case modeling::SelectionKind::Face:
    selection.mode = quader_poly::SelectionMode::Face;
    selection.active_kind = quader_poly::ElementKind::Face;
    break;
  }
  for (const auto vertex : snapshot.vertices) selection.vertices.push_back(vertex.index);
  for (const auto edge : snapshot.edges) selection.edges.push_back({edge.a.index, edge.b.index});
  for (const auto face : snapshot.faces) selection.faces.push_back(face.index);
  selection.has_active = snapshot.has_active;
  selection.active_vertex = snapshot.active_vertex.index;
  selection.active_edge = {snapshot.active_edge.a.index, snapshot.active_edge.b.index};
  selection.active_face = snapshot.active_face.index;
  return selection;
}

EdgeKey edge_key(quader_poly::ElementId a, quader_poly::ElementId b) {
  return a < b ? EdgeKey{a, b} : EdgeKey{b, a};
}

QVec3 world_normal(const QMat4 &world_from_object, QVec3 normal) {
  const auto inverse_transpose = glm::transpose(glm::inverse(glm::mat3(to_glm(world_from_object))));
  const auto transformed = inverse_transpose * to_glm(normal);
  return normalize_or_zero(from_glm(transformed));
}

Json mesh_json(const CapturedObject &object, const std::optional<ModelingPolygonDocumentReadSnapshot> &polygon) {
  if (!polygon) return Json{{"available", false}, {"error", object.geometry_error}};
  const auto &snapshot = *polygon;
  const auto document = snapshot.document.view();
  const auto &selection = object.selected.selection;
  const std::set<quader_poly::ElementId> selected_vertices(selection.vertices.begin(), selection.vertices.end());
  const std::set<quader_poly::ElementId> selected_faces(selection.faces.begin(), selection.faces.end());
  std::set<EdgeKey> selected_edges;
  for (const auto edge : selection.edges) selected_edges.insert(edge_key(edge.a, edge.b));

  std::map<quader_poly::ElementId, std::set<quader_poly::ElementId>> vertex_faces;
  std::map<quader_poly::ElementId, std::set<quader_poly::ElementId>> vertex_neighbors;
  std::map<EdgeKey, std::set<quader_poly::ElementId>> edge_faces;
  for (const auto &face : document.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const auto a = face.vertices[index];
      const auto b = face.vertices[(index + 1u) % face.vertices.size()];
      vertex_faces[a].insert(face.id);
      vertex_neighbors[a].insert(b);
      vertex_neighbors[b].insert(a);
      edge_faces[edge_key(a, b)].insert(face.id);
    }
  }

  Json vertices = Json::array();
  std::map<std::tuple<float, float, float>, std::vector<quader_poly::ElementId>> coincident;
  for (const auto &vertex : document.vertices) {
    const auto position = vertex.position;
    coincident[{position.x, position.y, position.z}].push_back(vertex.id);
    vertices.push_back(Json{{"id", vertex.id}, {"identity", component_id_json(vertex.id)},
        {"local_position", vec3_json(position)},
        {"world_position", vec3_json(transform_point(object.world_from_object, position))},
        {"selected", selected_vertices.contains(vertex.id)},
        {"adjacent_vertices", vertex_neighbors[vertex.id]}, {"incident_faces", vertex_faces[vertex.id]}});
  }
  Json faces = Json::array();
  for (const auto &face : document.faces) {
    Json uvs = Json::array();
    for (const auto uv : face.uvs) uvs.push_back(vec2_json(uv));
    const auto normal = quader_poly::face_normal(document, face);
    faces.push_back(Json{{"id", face.id}, {"identity", component_id_json(face.id)},
        {"loop", face.vertices}, {"uvs", std::move(uvs)},
        {"material_slot", face.material_slot}, {"material", material_json(object.material.material_for_slot(face.material_slot))},
        {"normal_shading", static_cast<int>(face.normal_shading)},
        {"local_normal", vec3_json(normal)}, {"world_normal", vec3_json(world_normal(object.world_from_object, normal))},
        {"texture_transform", Json{{"scale", vec2_json(face.texture_transform.scale)},
            {"shift_uv", vec2_json(face.texture_transform.shift_uv)},
            {"rotation_degrees", face.texture_transform.rotation_degrees}}},
        {"selected", selected_faces.contains(face.id)}});
  }
  Json edges = Json::array();
  for (const auto &[edge, faces_for_edge] : edge_faces) {
    auto value = edge_json({edge.first, edge.second});
    value["incident_faces"] = faces_for_edge;
    value["boundary"] = faces_for_edge.size() == 1u;
    value["non_manifold"] = faces_for_edge.size() > 2u;
    value["selected"] = selected_edges.contains(edge);
    edges.push_back(std::move(value));
  }
  Json coincident_sets = Json::array();
  for (const auto &[position, ids] : coincident) {
    if (ids.size() > 1u) coincident_sets.push_back(ids);
  }
  Json hard_edges = Json::array();
  for (const auto edge : document.hard_normal_edges) hard_edges.push_back(edge_json(edge));
  Json soft_edges = Json::array();
  for (const auto edge : document.soft_normal_edges) soft_edges.push_back(edge_json(edge));

  // Effective corner normals and UVs are derived only from this immutable view.
  const auto compiled = quader_poly::compile_document(document);
  Json corners = Json::array();
  for (const auto &corner : compiled.vertices) {
    corners.push_back(Json{{"source_vertex", corner.source_vertex_id},
        {"local_position", vec3_json(corner.position)},
        {"normal", vec3_json(corner.normal)},
        {"world_normal", vec3_json(world_normal(object.world_from_object, corner.normal))},
        {"uv", vec2_json(corner.uv0)}});
  }
  Json primitives = Json::array();
  for (const auto &primitive : compiled.primitives) {
    primitives.push_back(Json{{"index_offset", primitive.index_offset},
        {"index_count", primitive.index_count}, {"material_slot", primitive.material_slot}});
  }

  std::set<quader_poly::ElementId> component_vertices = selected_vertices;
  for (const auto edge : selection.edges) {
    component_vertices.insert(edge.a);
    component_vertices.insert(edge.b);
  }
  std::set<quader_poly::ElementId> common_faces;
  bool first = true;
  for (const auto vertex : component_vertices) {
    if (first) { common_faces = vertex_faces[vertex]; first = false; }
    else {
      std::set<quader_poly::ElementId> intersection;
      const auto &incident = vertex_faces[vertex];
      std::set_intersection(common_faces.begin(), common_faces.end(), incident.begin(), incident.end(),
                            std::inserter(intersection, intersection.end()));
      common_faces = std::move(intersection);
    }
  }
  return Json{{"available", true}, {"preview", snapshot.preview},
      {"content_identity", snapshot.document.content_identity().value},
      {"topology_identity", snapshot.document.topology_identity().value},
      {"next_vertex_id", document.next_vertex_id}, {"next_face_id", document.next_face_id},
      {"vertices", std::move(vertices)}, {"edges", std::move(edges)}, {"faces", std::move(faces)},
      {"hard_normal_edges", std::move(hard_edges)}, {"soft_normal_edges", std::move(soft_edges)},
      {"compiled_corners", std::move(corners)}, {"compiled_indices", compiled.indices},
      {"compiled_primitives", std::move(primitives)},
      {"diagnostics", Json{{"coincident_tolerance", 0.0},
          {"coincident_vertex_sets", std::move(coincident_sets)}, {"selected_components_common_faces", common_faces}}}};
}

Json object_json(const CapturedObject &object) {
  const auto &selected = object.selected;
  const auto sdk_id = object.sdk_object_id;
  const auto packed_id = (static_cast<std::uint64_t>(sdk_id.generation) << 32u) | sdk_id.index;
  Json material_slots = Json::array();
  std::map<std::uint32_t, MaterialReference> slots(object.material.material_slots.begin(), object.material.material_slots.end());
  for (const auto &[slot, material] : slots) material_slots.push_back(Json{{"slot", slot}, {"material", material_json(material)}});
  return Json{{"modeling_object_id", selected.modeling_object_id}, {"entity_id", selected.entity},
      {"uuid", selected.entity_uuid}, {"name", selected.name}, {"selected", selected.selected},
      {"sdk_id", Json{{"index", sdk_id.index}, {"generation", sdk_id.generation}, {"packed", packed_id}}},
      {"mesh_asset_id", object.mesh_asset},
      {"transform", Json{{"location", vec3_json(object.transform.location)},
          {"rotation_euler_radians", vec3_json(object.transform.rotation_euler)},
          {"scale", vec3_json(object.transform.scale)}, {"custom_pivot", object.transform.custom_pivot},
          {"parenting_offset_column_major", object.transform.parenting_offset.values}}},
      {"world_transform_column_major", object.world_from_object.values},
      {"default_material", material_json(object.material.material)}, {"material_slots", std::move(material_slots)},
      {"selection", polygon_selection_json(selected.selection)},
      {"source_selection", polygon_selection_json(object.source_selection)},
      {"suppressed_by_preview", object.suppressed_by_preview},
      {"mesh", mesh_json(object, object.polygon)},
      {"committed_mesh", object.committed_polygon ? mesh_json(object, object.committed_polygon) : Json()}};
}

Json selection_json(const CapturedSelection &capture) {
  const auto &summary = capture.selection.summary;
  Json operations = Json::array();
  for (const auto &operation : capture.operations) {
    operations.push_back(Json{{"name", operation.action_id}, {"operation_id", operation.operation_id},
        {"eligible", operation.visibility == ModelingActionVisibility::Enabled},
        {"visibility", static_cast<int>(operation.visibility)},
        {"reason", operation.disabled_reason}, {"reason_key", operation.disabled_reason_key},
        {"reason_count", operation.disabled_reason_count}});
  }
  Json objects = Json::array();
  for (const auto &object : capture.objects) objects.push_back(object_json(object));
  std::map<std::tuple<float, float, float>, Json> world_groups;
  for (const auto &object : objects) {
    if (!object["mesh"]["available"].get<bool>()) continue;
    for (const auto &vertex : object["mesh"]["vertices"]) {
      const auto &world = vertex["world_position"];
      auto &group = world_groups[{world[0].get<float>(), world[1].get<float>(), world[2].get<float>()}];
      if (group.is_null()) group = Json::array();
      group.push_back(Json{{"modeling_object_id", object["modeling_object_id"]}, {"object_uuid", object["uuid"]},
                          {"vertex", vertex["id"]}, {"selected", vertex["selected"]}});
    }
  }
  Json world_coincident = Json::array();
  for (const auto &[position, members] : world_groups) {
    if (members.size() < 2u) continue;
    const auto first_object = members[0]["object_uuid"];
    const bool cross_object = std::any_of(members.begin() + 1, members.end(),
        [&first_object](const auto &member) { return member["object_uuid"] != first_object; });
    if (cross_object) world_coincident.push_back(Json{{"world_position", Json::array({std::get<0>(position), std::get<1>(position), std::get<2>(position)})},
                                                    {"members", members}});
  }
  return Json{{"kind", "quader.selection.dump"}, {"schema_version", 1}, {"id", capture.id},
      {"captured_utc", capture.captured_utc}, {"app_version", kQuaderAppVersionDisplay},
      {"preview_active", capture.preview_active},
      {"scene", Json{{"uuid", capture.scene_id}, {"name", capture.scene_name}, {"path", capture.scene_path},
          {"session_id", capture.scene_session_id.packed()}, {"session_generation", capture.scene_session_id.generation()},
          {"document_binding_generation", capture.document_binding_generation}}},
      {"project", Json{{"uuid", capture.project_id}, {"name", capture.project_name},
          {"root", path_text(capture.project_root)}, {"manifest", path_text(capture.project_manifest)},
          {"generation", capture.project_generation}, {"activation_generation", capture.activation_generation}}},
      {"revisions", Json{{"document", capture.revisions.project_revision}, {"scene", capture.revisions.scene_revision},
          {"assets", capture.revisions.asset_revision}, {"modeling_content", capture.revisions.modeling_content_revision},
          {"modeling_selection", capture.selection_revision}, {"scene_selection", capture.scene_selection_revision},
          {"history", capture.history_revision}}},
      {"selection", Json{{"mode", mode_name(capture.selection.mode)},
          {"scene_entities", capture.selection.scene_selection}, {"active_entity", capture.selection.active_entity},
          {"counts", Json{{"objects", summary.selected_object_count}, {"vertices", summary.selected_vertex_count},
              {"edges", summary.selected_edge_count}, {"faces", summary.selected_face_count}}},
          {"active", Json{{"kind", active_name(summary.active_kind)}, {"modeling_object_id", summary.active_object_id},
              {"vertex", summary.active_vertex_id}, {"edge_a", summary.active_edge_a}, {"edge_b", summary.active_edge_b},
              {"face", summary.active_face_id}}}}},
      {"operation_eligibility", std::move(operations)},
      {"diagnostics", Json{{"world_coincident_tolerance", 0.0}, {"cross_object_coincident_vertices", std::move(world_coincident)}}},
      {"objects", std::move(objects)}};
}

std::string readable_summary(const Json &dump, const std::filesystem::path &path) {
  constexpr std::size_t kMaximumLines = 198u;
  std::ostringstream text;
  std::size_t lines = 0u;
  const auto append = [&](const std::string &line) {
    constexpr std::size_t kMaximumLineBytes = 1536u;
    if (lines < kMaximumLines) {
      std::string escaped;
      for (const char value : line) {
        if (escaped.size() >= kMaximumLineBytes) break;
        if (value == '\n') escaped += "\\n";
        else if (value == '\r') escaped += "\\r";
        else escaped.push_back(value);
      }
      text << escaped;
      if (escaped.size() >= kMaximumLineBytes) text << " ... (complete data in JSON)";
      text << '\n';
      ++lines;
    }
  };
  append(dump["id"].get<std::string>() + " — " + path_text(path));
  append("Captured " + dump["captured_utc"].get<std::string>() + " / " + dump["app_version"].get<std::string>());
  const auto &selection = dump["selection"];
  append("Selection " + selection["mode"].get<std::string>() + ": " + selection["counts"].dump());
  append("Active " + selection["active"].dump());
  append("Revisions " + dump["revisions"].dump());
  append(std::string("Live preview: ") + (dump["preview_active"].get<bool>() ? "yes" : "no"));
  append("Cross-object world coincidence " + dump["diagnostics"]["cross_object_coincident_vertices"].dump());
  for (const auto &operation : dump["operation_eligibility"]) {
    const bool eligible = operation["eligible"].get<bool>();
    const auto reason = operation["reason"].get<std::string>();
    const auto key = operation["reason_key"].get<std::string>();
    append(operation["name"].get<std::string>() + ": " + (eligible ? "eligible" : "unavailable") +
           (reason.empty() ? (key.empty() ? "" : " — " + key) : " — " + reason));
  }
  for (const auto &object : dump["objects"]) {
    const auto &mesh = object["mesh"];
    append("Object " + object["name"].get<std::string>() + " / " + object["uuid"].get<std::string>() +
           " / modeling " + object["modeling_object_id"].dump() + " / SDK " + object["sdk_id"].dump());
    if (!mesh["available"].get<bool>()) { append("Geometry unavailable: " + mesh["error"].get<std::string>()); continue; }
    append("Full mesh: " + std::to_string(mesh["vertices"].size()) + " vertices, " +
           std::to_string(mesh["edges"].size()) + " edges, " + std::to_string(mesh["faces"].size()) + " faces");
    append("Common faces " + mesh["diagnostics"]["selected_components_common_faces"].dump());
    append("Coincident sets " + mesh["diagnostics"]["coincident_vertex_sets"].dump());
    for (const auto &vertex : mesh["vertices"]) {
      if (!vertex["selected"].get<bool>()) continue;
      append("Vertex " + vertex["id"].dump() + " local=" + vertex["local_position"].dump() +
             " world=" + vertex["world_position"].dump() + " neighbors=" + vertex["adjacent_vertices"].dump() +
             " faces=" + vertex["incident_faces"].dump());
    }
    for (const auto &edge : mesh["edges"]) {
      if (edge["selected"].get<bool>()) append("Edge " + edge["a"].dump() + ":" + edge["b"].dump() +
                                             " faces=" + edge["incident_faces"].dump());
    }
    for (const auto &face : mesh["faces"]) {
      if (face["selected"].get<bool>()) append("Face " + face["id"].dump() + " loop=" + face["loop"].dump());
    }
  }
  if (lines >= kMaximumLines) text << "Summary truncated; the JSON file contains the complete capture.\n";
  return text.str();
}

SelectionDiagnosticReply serialize_and_write(CapturedSelection capture, std::filesystem::path directory) {
  SelectionDiagnosticReply reply{.id = capture.id};
  std::filesystem::path temporary;
  try {
    directory = std::filesystem::absolute(directory);
    std::filesystem::create_directories(directory);
    // A reservation prevents overwrites while keeping the already-published ticket ID.
    const auto path = directory / (capture.id + ".json");
    const auto reservation = directory / (capture.id + ".pending");
    if (std::filesystem::exists(path) || !std::filesystem::create_directory(reservation)) {
      throw std::runtime_error("The diagnostic ID is already reserved; request another dump.");
    }
    reply.absolute_path = path;
    temporary = reservation / "capture.json";
    const auto dump = selection_json(capture);
    const auto serialized = dump.dump(2);
    reply.summary = readable_summary(dump, reply.absolute_path);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.put('\n');
    output.flush();
    output.close();
    std::filesystem::rename(temporary, reply.absolute_path);
    std::error_code ignored;
    std::filesystem::remove(reservation, ignored);
    temporary.clear();
    reply.ok = true;
    reply.message = "Saved selection dump " + reply.id + ".";
  } catch (const std::exception &error) {
    if (!temporary.empty()) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      std::filesystem::remove(temporary.parent_path(), ignored);
    }
    reply.absolute_path.clear();
    reply.message = "Could not save selection dump " + reply.id + ": " + error.what();
  }
  return reply;
}

CapturedSelection capture_selection(const SceneSession &session, const ProjectSession *project, SceneSessionId scene_id) {
  CapturedSelection capture;
  capture.captured_utc = capture_time();
  capture.id = make_dump_id(capture.captured_utc);
  capture.scene_session_id = scene_id;
  capture.document_binding_generation = session.document_binding_generation();
  capture.revisions = session.content_revision_stamp();
  capture.selection_revision = session.modeling().selection_revision();
  capture.scene_selection_revision = session.scene().selection_revision();
  capture.history_revision = session.history().revision();
  capture.selection = session.selection().state();
  capture.preview_active = session.modeling().operation_preview_active() || session.modeling().transient_transform_preview_active();
  auto exported = session.modeling().export_selection();
  if (capture.selection.mode == ModelingSelectionMode::Object ||
      capture.selection.mode == ModelingSelectionMode::Group) {
    std::set<EntityId> included;
    for (const auto &selected : exported.objects) included.insert(selected.entity);
    for (const auto root : capture.selection.scene_selection) {
      for (const auto entity : session.scene().descendants(root, true)) {
        if (!included.insert(entity).second) continue;
        ModelingSelectionExportObject selected;
        selected.entity = entity;
        selected.entity_uuid = session.scene().uuid(entity);
        selected.selected = true;
        if (const auto *name = session.scene().name(entity)) selected.name = name->name;
        selected.modeling_object_id = session.modeling().modeling_object_for_entity(entity).value_or(kInvalidModelingObjectId);
        exported.objects.push_back(std::move(selected));
      }
    }
  }
  for (const auto &selected : exported.objects) {
    CapturedObject object;
    object.selected = selected;
    object.source_selection = selected.selection;
    if (const auto *transform = session.scene().transform(selected.entity)) object.transform = *transform;
    object.world_from_object = session.scene().world_transform_matrix(selected.entity);
    if (const auto *material = session.scene().material(selected.entity)) object.material = *material;
    if (const auto *mesh = session.scene().mesh(selected.entity)) object.mesh_asset = mesh->mesh;
    if (selected.modeling_object_id != kInvalidModelingObjectId) {
      auto polygon = session.modeling().polygon_document_read_snapshot(selected.modeling_object_id);
      if (polygon.ok()) {
        object.sdk_object_id = polygon.value().object;
        object.selected.selection = captured_polygon_selection(polygon.value().selection);
        object.source_selection = object.selected.selection;
        if (polygon.value().preview) {
          const auto committed = session.modeling().polygon_document_read_snapshot(selected.modeling_object_id, false);
          if (committed.ok()) object.source_selection = captured_polygon_selection(committed.value().selection);
        }
        object.polygon = std::move(polygon).value();
      } else {
        object.geometry_error = polygon.error().message;
        const auto committed = session.modeling().polygon_document_read_snapshot(selected.modeling_object_id, false);
        if (committed.ok()) {
          object.sdk_object_id = committed.value().object;
          object.source_selection = captured_polygon_selection(committed.value().selection);
          object.selected.selection = object.source_selection;
          object.committed_polygon = committed.value();
          object.suppressed_by_preview = object.geometry_error == "A removed preview participant has no current document.";
        }
      }
    } else {
      object.geometry_error = "This selected entity has no authored polygon mesh.";
    }
    capture.objects.push_back(std::move(object));
  }
  constexpr std::array<std::string_view, 11> kOperationNames{
      "connect_vertices", "merge_vertices", "merge_vertices_at_center", "merge_by_distance",
      "merge_edges", "split_edges", "connect_edges", "bridge_edges", "interpolated_bridge_edges",
      "dissolve_vertices", "dissolve_edges"};
  const ModelingActionQuery query{.suppress_when_modal_tool_owns_viewport = false, .include_disabled = true, .include_hidden = true};
  for (const auto name : kOperationNames) {
    auto operation = session.modeling().context_action(name, query);
    if (operation) {
      if (operation->visibility != ModelingActionVisibility::Enabled &&
          operation->disabled_reason.empty() && operation->disabled_reason_key.empty()) {
        if (name == "connect_vertices") operation->disabled_reason = "Selected vertices have no eligible common authored face.";
        else if (name == "dissolve_vertices") operation->disabled_reason = "Selected vertices do not belong to an authored face.";
        else if (name == "split_edges" || name == "dissolve_edges") operation->disabled_reason = "Selected edges must each have exactly two incident authored faces.";
        else if (name == "connect_edges") operation->disabled_reason = "Selected edges do not belong to authored faces.";
        else if (name == "bridge_edges" || name == "interpolated_bridge_edges") operation->disabled_reason = "Selected edges cannot form the required bridge pair.";
        else operation->disabled_reason = "This operation is unavailable for the current selection mode or topology.";
      }
      capture.operations.push_back(std::move(*operation));
    }
    else capture.operations.push_back(ModelingActionItem{.action_id = std::string(name),
        .disabled_reason = "This operation is unavailable for the current selection.",
        .disabled_reason_key = "modeling.action.disabled.selection_mode"});
  }
  if (project != nullptr) {
    capture.project_root = project->root();
    capture.project_manifest = project->manifest_path();
    capture.project_generation = project->project_generation();
    capture.activation_generation = project->activation_generation();
    if (const auto *manifest = project->manifest()) {
      capture.project_id = manifest->id().str();
      capture.project_name = manifest->name();
    }
    if (const auto *scene = project->active_scene()) {
      capture.scene_id = scene->scene_id.str();
      capture.scene_name = scene->display_name;
      if (scene->path) capture.scene_path = scene->path->portable_string();
    }
  }
  return capture;
}

} // namespace

class SelectionDiagnosticService::Impl final {
public:
  std::future<SelectionDiagnosticReply> worker;
};

SelectionDiagnosticService::SelectionDiagnosticService() : impl_(std::make_unique<Impl>()) {}
SelectionDiagnosticService::~SelectionDiagnosticService() = default;

SelectionDiagnosticRequest SelectionDiagnosticService::request_dump(
    const SceneSession &scene, const ProjectSession *project, SceneSessionId scene_id,
    std::filesystem::path directory) {
  if (pending()) return {.message = "A selection dump is already being saved."};
  const auto selection = scene.selection().summary();
  if (!selection.has_selection && scene.scene().selected_entities().empty()) {
    return {.message = "The selection is empty; no selection dump was created."};
  }
  auto capture = capture_selection(scene, project, scene_id);
  const auto id = capture.id;
  impl_->worker = std::async(std::launch::async, serialize_and_write, std::move(capture), std::move(directory));
  return {.ok = true, .id = id, .message = "Saving selection dump " + id + "."};
}

bool SelectionDiagnosticService::pending() const noexcept { return impl_->worker.valid(); }

std::optional<SelectionDiagnosticReply> SelectionDiagnosticService::take_completed() {
  if (!impl_->worker.valid() || impl_->worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
  return impl_->worker.get();
}

} // namespace quader
