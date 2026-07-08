////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_polygon_document_codec.hpp"

#include "io/qdr/qdr_diagnostic.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_io {
namespace {

using Json = QdrJson;

Json vec2_to_json(quader::QVec2 value) {
  return Json{
      {"x", value.x},
      {"y", value.y},
  };
}

Json vec3_to_json(quader::QVec3 value) {
  return Json{
      {"x", value.x},
      {"y", value.y},
      {"z", value.z},
  };
}

Json edge_to_json(quader_poly::Edge edge) {
  return Json{
      {"a", edge.a},
      {"b", edge.b},
  };
}

std::string surface_shading_name(quader_poly::SurfaceShading shading) {
    switch (shading) {
    case quader_poly::SurfaceShading::Flat:
      return "flat";
    case quader_poly::SurfaceShading::Smooth:
      return "smooth";
    case quader_poly::SurfaceShading::Authored:
      return "authored";
    }
    return "flat";
}

quader::QVec2 vec2_from_json(const Json &value) {
  return {
      value.at("x").get<float>(),
      value.at("y").get<float>(),
  };
}

quader::QVec3 vec3_from_json(const Json &value) {
  return {
      value.at("x").get<float>(),
      value.at("y").get<float>(),
      value.at("z").get<float>(),
  };
}

quader_poly::Edge edge_from_json(const Json &value) {
  return quader_poly::make_edge(value.at("a").get<quader_poly::ElementId>(),
                                value.at("b").get<quader_poly::ElementId>());
}

quader_poly::SurfaceShading parse_surface_shading(const std::string& value) {
    if (value == "flat") {
      return quader_poly::SurfaceShading::Flat;
    }
    if (value == "smooth") {
      return quader_poly::SurfaceShading::Smooth;
    }
    if (value == "authored") {
      return quader_poly::SurfaceShading::Authored;
    }
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.unsupported_enum", "",
        "QDR polygon document contains unsupported face normal shading enum "
        "value: " +
            value + "."));
}

std::string enum_field_string(const Json &value, const char *key,
                              std::string_view label,
                              QdrPolygonDocumentParseMode mode,
                              std::string_view legacy_fallback) {
  const auto field = value.find(key);
  if (field == value.end()) {
    if (mode == QdrPolygonDocumentParseMode::LegacyDefault) {
      return std::string(legacy_fallback);
    }
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.missing_enum", "",
        "QDR polygon document is missing " + std::string(label) +
            " enum field."));
  }
  if (!field->is_string()) {
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.invalid_enum", "",
        "QDR polygon document " + std::string(label) +
            " enum field must be a string."));
  }
  return field->get<std::string>();
}

bool contains_duplicate_ids(std::vector<quader_poly::ElementId> ids) {
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

bool face_references_existing_vertices(
        const std::unordered_set<quader_poly::ElementId>& vertex_ids,
        const quader_poly::Face& face) {
    return std::all_of(face.vertices.begin(), face.vertices.end(), [&vertex_ids](quader_poly::ElementId id) {
        return vertex_ids.contains(id);
    });
}

} // namespace

QdrJson qdr_polygon_document_to_json(const quader_poly::Document& document) {
  Json vertices = Json::array();
  vertices.get_ref<Json::array_t &>().reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    vertices.push_back({
        {"id", vertex.id},
        {"position", vec3_to_json(vertex.position)},
    });
  }

  Json faces = Json::array();
  faces.get_ref<Json::array_t &>().reserve(document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    Json uvs = Json::array();
    uvs.get_ref<Json::array_t &>().reserve(face.uvs.size());
    for (quader::QVec2 uv : face.uvs) {
      uvs.push_back(vec2_to_json(uv));
    }

    faces.push_back({
        {"id", face.id},
        {"vertices", face.vertices},
        {"uvs", uvs},
        {"materialSlot", face.material_slot},
        {"normalShading", surface_shading_name(face.normal_shading)},
    });
  }

  Json hard_normal_edges = Json::array();
  if (!document.hard_normal_edges.empty()) {
    hard_normal_edges.get_ref<Json::array_t &>().reserve(
        document.hard_normal_edges.size());
    const std::vector<quader_poly::Edge> document_edges =
        quader_poly::document_edges(document);
    std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>>
        serialized_hard_edges;
    for (quader_poly::Edge edge : document.hard_normal_edges) {
      edge = quader_poly::make_edge(edge.a, edge.b);
      if (std::find(document_edges.begin(), document_edges.end(), edge) ==
          document_edges.end()) {
        continue;
      }
      if (!serialized_hard_edges.insert({edge.a, edge.b}).second) {
        continue;
      }
      hard_normal_edges.push_back(edge_to_json(edge));
    }
    }

    return {
            {"nextVertexId", document.next_vertex_id},
            {"nextFaceId", document.next_face_id},
            {"vertices", vertices},
            {"faces", faces},
            {"hardNormalEdges", hard_normal_edges},
    };
}

quader_poly::Document qdr_polygon_document_from_json(const QdrJson &value) {
  return qdr_polygon_document_from_json(
      value, QdrPolygonDocumentParseMode::LegacyDefault);
}

quader_poly::Document
qdr_polygon_document_from_json(const QdrJson &value,
                               QdrPolygonDocumentParseMode mode) {
    quader_poly::Document document;
    document.next_vertex_id = value.at("nextVertexId").get<quader_poly::ElementId>();
    document.next_face_id = value.at("nextFaceId").get<quader_poly::ElementId>();

    const Json &vertices_json = value.at("vertices");
    document.vertices.reserve(vertices_json.size());
    std::unordered_set<quader_poly::ElementId> vertex_ids;
    vertex_ids.reserve(vertices_json.size());
    quader_poly::ElementId max_vertex_id = 0;
    for (const Json &vertex_json : vertices_json) {
      quader_poly::Vertex vertex;
      vertex.id = vertex_json.at("id").get<quader_poly::ElementId>();
      vertex.position = vec3_from_json(vertex_json.at("position"));
      if (vertex.id == quader_poly::kInvalidElementId ||
          !vertex_ids.insert(vertex.id).second) {
        throw std::runtime_error(
            "Document contains an invalid or duplicate vertex id.");
      }
      max_vertex_id = std::max(max_vertex_id, vertex.id);
      document.vertices.push_back(vertex);
    }

    const Json &faces_json = value.at("faces");
    document.faces.reserve(faces_json.size());
    std::unordered_set<quader_poly::ElementId> face_ids;
    face_ids.reserve(faces_json.size());
    quader_poly::ElementId max_face_id = 0;
    for (const Json &face_json : faces_json) {
      quader_poly::Face face;
      face.id = face_json.at("id").get<quader_poly::ElementId>();
      face.vertices =
          face_json.at("vertices").get<std::vector<quader_poly::ElementId>>();
      face.material_slot = face_json.value("materialSlot", 0U);
      face.normal_shading = parse_surface_shading(
          enum_field_string(face_json, "normalShading", "face normal shading",
                            mode, "flat"));
      face.uvs.reserve(face_json.at("uvs").size());
      for (const Json &uv_json : face_json.at("uvs")) {
        face.uvs.push_back(vec2_from_json(uv_json));
      }
      if (face.id == quader_poly::kInvalidElementId ||
          !face_ids.insert(face.id).second) {
        throw std::runtime_error(
            "Document contains an invalid or duplicate face id.");
      }
      if (face.vertices.size() < 3U) {
        throw std::runtime_error(
            "Document contains a face with fewer than three vertices.");
      }
      if (contains_duplicate_ids(face.vertices)) {
        throw std::runtime_error(
            "Document contains a face with duplicate vertex ids.");
      }
      if (!face.uvs.empty() && face.uvs.size() != face.vertices.size()) {
        throw std::runtime_error("Document contains a face whose UV count does "
                                 "not match its vertex count.");
      }
      document.faces.push_back(std::move(face));
      max_face_id = std::max(max_face_id, document.faces.back().id);
    }

    for (const quader_poly::Face& face : document.faces) {
        if (!face_references_existing_vertices(vertex_ids, face)) {
            throw std::runtime_error("Document contains a face that references a missing vertex.");
        }
    }

    std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> hard_normal_edges;
    if (const auto hard_edges_json = value.find("hardNormalEdges");
            hard_edges_json != value.end() && !hard_edges_json->empty()) {
        document.hard_normal_edges.reserve(hard_edges_json->size());
        const std::vector<quader_poly::Edge> valid_edges = quader_poly::document_edges(document);
        for (const Json &edge_json : *hard_edges_json) {
          quader_poly::Edge edge = edge_from_json(edge_json);
          if (std::find(valid_edges.begin(), valid_edges.end(), edge) ==
              valid_edges.end()) {
            throw std::runtime_error("Document contains a hard normal edge "
                                     "that is not part of the mesh.");
          }
          if (!hard_normal_edges.insert({edge.a, edge.b}).second) {
            throw std::runtime_error(
                "Document contains duplicate hard normal edges.");
          }
          document.hard_normal_edges.push_back(edge);
        }
    }

    document.next_vertex_id = std::max(document.next_vertex_id, max_vertex_id + 1U);
    document.next_face_id = std::max(document.next_face_id, max_face_id + 1U);
    return document;
}

} // namespace quader_io
