////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/component_orientation.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace quader_editor_core {
namespace {

constexpr float kOrientationEpsilon = 0.000001F;

bool finite_vector(quader::QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool usable_vector(quader::QVec3 value) {
  return finite_vector(value) &&
         quader::length_squared(value) >
             kOrientationEpsilon * kOrientationEpsilon;
}

quader::QVec3 normalized_or_zero(quader::QVec3 value) {
  return usable_vector(value) ? quader::normalize_or_zero(value)
                              : quader::QVec3{};
}

quader_geometry::QVec3f geometry_vector(quader::QVec3 value) {
  return {value.x, value.y, value.z};
}

quader::QVec3 app_vector(quader_geometry::QVec3f value) {
  return {value.x, value.y, value.z};
}

quader::QVec3 resolved_normal(
    quader::QVec3 requested,
    const quader_poly::Transform3 &object_fallback) {
  if (quader::QVec3 normal = normalized_or_zero(requested);
      usable_vector(normal)) {
    return normal;
  }
  if (quader::QVec3 normal = normalized_or_zero(object_fallback.z_axis);
      usable_vector(normal)) {
    return normal;
  }
  return {0.0F, 0.0F, 1.0F};
}

quader_poly::Transform3 frame_from_normal_and_tangent(
    quader::QVec3 origin, quader::QVec3 normal,
    quader::QVec3 preferred_tangent,
    const quader_poly::Transform3 &object_fallback) {
  const quader::QVec3 z_axis = resolved_normal(normal, object_fallback);
  if (!usable_vector(preferred_tangent)) {
    preferred_tangent = object_fallback.x_axis;
  }

  quader_geometry::QPlaneBasis3<float> plane =
      quader_geometry::make_plane_basis(
          geometry_vector(origin), geometry_vector(z_axis),
          geometry_vector(preferred_tangent), kOrientationEpsilon);
  if (!plane.valid) {
    plane = quader_geometry::make_plane_basis(
        geometry_vector(origin), geometry_vector({0.0F, 0.0F, 1.0F}),
        geometry_vector({1.0F, 0.0F, 0.0F}), kOrientationEpsilon);
  }

  return {
      .x_axis = app_vector(plane.u),
      .y_axis = app_vector(plane.v),
      .z_axis = app_vector(plane.normal),
      .origin = origin,
  };
}

quader_poly::Transform3 frame_from_edge_and_normal(
    quader::QVec3 origin, quader::QVec3 edge_direction,
    quader::QVec3 incident_normal,
    const quader_poly::Transform3 &object_fallback) {
  const quader::QVec3 x_axis = normalized_or_zero(edge_direction);
  if (!usable_vector(x_axis)) {
    return frame_from_normal_and_tangent(
        origin, object_fallback.z_axis, object_fallback.x_axis,
        object_fallback);
  }

  quader::QVec3 z_axis =
      incident_normal - x_axis * quader::dot(incident_normal, x_axis);
  z_axis = normalized_or_zero(z_axis);
  if (!usable_vector(z_axis)) {
    const auto fallback_plane = quader_geometry::make_plane_basis(
        geometry_vector(origin), geometry_vector(x_axis),
        geometry_vector(object_fallback.z_axis), kOrientationEpsilon);
    z_axis = fallback_plane.valid ? app_vector(fallback_plane.u)
                                  : quader::QVec3{0.0F, 0.0F, 1.0F};
  }

  quader::QVec3 y_axis =
      normalized_or_zero(quader::cross(z_axis, x_axis));
  if (!usable_vector(y_axis)) {
    return frame_from_normal_and_tangent(
        origin, object_fallback.z_axis, object_fallback.x_axis,
        object_fallback);
  }
  z_axis = normalized_or_zero(quader::cross(x_axis, y_axis));
  return {
      .x_axis = x_axis,
      .y_axis = y_axis,
      .z_axis = z_axis,
      .origin = origin,
  };
}

bool face_contains_vertex(const quader_poly::Face &face,
                          quader_poly::ElementId vertex_id) {
  return std::find(face.vertices.begin(), face.vertices.end(), vertex_id) !=
         face.vertices.end();
}

bool face_contains_edge(const quader_poly::Face &face,
                        quader_poly::Edge edge) {
  edge = quader_poly::make_edge(edge.a, edge.b);
  for (std::size_t index = 0; index < face.vertices.size(); ++index) {
    const quader_poly::Edge perimeter = quader_poly::make_edge(
        face.vertices[index], face.vertices[(index + 1) % face.vertices.size()]);
    if (perimeter == edge) {
      return true;
    }
  }
  return false;
}

quader::QVec3 incident_vertex_normal(
    const quader_poly::Document &document,
    quader_poly::ElementId vertex_id) {
  quader::QVec3 normal;
  for (const quader_poly::Face &face : document.faces) {
    if (face_contains_vertex(face, vertex_id)) {
      normal += quader_poly::face_normal(document, face);
    }
  }
  return normal;
}

quader::QVec3 incident_edge_normal(const quader_poly::Document &document,
                                   quader_poly::Edge edge) {
  quader::QVec3 normal;
  for (const quader_poly::Face &face : document.faces) {
    if (face_contains_edge(face, edge)) {
      normal += quader_poly::face_normal(document, face);
    }
  }
  return normal;
}

std::vector<quader_poly::ElementId> vertex_neighbors(
    const quader_poly::Document &document,
    quader_poly::ElementId vertex_id) {
  std::vector<quader_poly::ElementId> neighbors;
  for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
    if (edge.a == vertex_id) {
      neighbors.push_back(edge.b);
    } else if (edge.b == vertex_id) {
      neighbors.push_back(edge.a);
    }
  }
  std::ranges::sort(neighbors);
  neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                  neighbors.end());
  return neighbors;
}

std::optional<quader::QVec3>
face_centroid(const quader_poly::Document &document,
              const quader_poly::Face &face) {
  if (face.vertices.empty()) {
    return std::nullopt;
  }
  quader::QVec3 center;
  for (const quader_poly::ElementId vertex_id : face.vertices) {
    const quader_poly::Vertex *vertex =
        quader_poly::find_vertex(document, vertex_id);
    if (vertex == nullptr || !finite_vector(vertex->position)) {
      return std::nullopt;
    }
    center += vertex->position;
  }
  return center / static_cast<float>(face.vertices.size());
}

} // namespace

std::optional<quader_poly::Transform3>
component_vertex_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId vertex_id,
    const quader_poly::Transform3 &object_fallback) {
  const quader_poly::Vertex *vertex =
      quader_poly::find_vertex(document, vertex_id);
  if (vertex == nullptr || !finite_vector(vertex->position)) {
    return std::nullopt;
  }

  const quader::QVec3 z_axis =
      resolved_normal(incident_vertex_normal(document, vertex_id),
                      object_fallback);
  quader::QVec3 preferred_tangent;
  for (const quader_poly::ElementId neighbor_id :
       vertex_neighbors(document, vertex_id)) {
    const quader_poly::Vertex *neighbor =
        quader_poly::find_vertex(document, neighbor_id);
    if (neighbor == nullptr || !finite_vector(neighbor->position)) {
      continue;
    }
    quader::QVec3 tangent = neighbor->position - vertex->position;
    tangent = tangent - z_axis * quader::dot(tangent, z_axis);
    if (usable_vector(tangent)) {
      preferred_tangent = tangent;
      break;
    }
  }

  return frame_from_normal_and_tangent(
      vertex->position, z_axis, preferred_tangent, object_fallback);
}

std::optional<quader_poly::Transform3>
component_edge_local_orientation(
    const quader_poly::Document &document, quader_poly::Edge edge,
    const quader_poly::Transform3 &object_fallback) {
  edge = quader_poly::make_edge(edge.a, edge.b);
  const quader_poly::Vertex *first =
      quader_poly::find_vertex(document, edge.a);
  const quader_poly::Vertex *second =
      quader_poly::find_vertex(document, edge.b);
  if (first == nullptr || second == nullptr ||
      !finite_vector(first->position) || !finite_vector(second->position)) {
    return std::nullopt;
  }

  const bool live_edge = std::any_of(
      document.faces.begin(), document.faces.end(),
      [edge](const quader_poly::Face &face) {
        return face_contains_edge(face, edge);
      });
  if (!live_edge) {
    return std::nullopt;
  }

  return frame_from_edge_and_normal(
      (first->position + second->position) * 0.5F,
      second->position - first->position,
      incident_edge_normal(document, edge), object_fallback);
}

std::optional<quader_poly::Transform3>
component_face_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId face_id,
    const quader_poly::Transform3 &object_fallback) {
  const quader_poly::Face *face = quader_poly::find_face(document, face_id);
  if (face == nullptr) {
    return std::nullopt;
  }
  const std::optional<quader::QVec3> center = face_centroid(document, *face);
  if (!center.has_value()) {
    return std::nullopt;
  }

  quader::QVec3 preferred_tangent;
  for (std::size_t index = 0; index < face->vertices.size(); ++index) {
    const quader_poly::Vertex *current =
        quader_poly::find_vertex(document, face->vertices[index]);
    const quader_poly::Vertex *next = quader_poly::find_vertex(
        document, face->vertices[(index + 1) % face->vertices.size()]);
    if (current == nullptr || next == nullptr) {
      continue;
    }
    const quader::QVec3 edge_direction = next->position - current->position;
    if (usable_vector(edge_direction)) {
      preferred_tangent = edge_direction;
      break;
    }
  }

  return frame_from_normal_and_tangent(
      *center, quader_poly::face_normal(document, *face), preferred_tangent,
      object_fallback);
}

} // namespace quader_editor_core
