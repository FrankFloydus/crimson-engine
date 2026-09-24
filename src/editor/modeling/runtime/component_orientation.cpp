////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/component_orientation.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <cmath>

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

quader::QVec3 normalized_gradient(quader::QVec3 value) {
  if (!finite_vector(value)) {
    return {};
  }
  const float magnitude = std::max({std::abs(value.x), std::abs(value.y),
                                     std::abs(value.z)});
  // Texture scale changes gradient magnitude, never the direction of its axes.
  return magnitude > 0.0F ? quader::normalize_or_zero(value / magnitude)
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
    quader::QVec3 face_normal,
    const quader_poly::Transform3 &object_fallback) {
  if (!usable_vector(edge_direction)) {
    return frame_from_normal_and_tangent(
        origin, object_fallback.z_axis, object_fallback.x_axis,
        object_fallback);
  }
  const quader::QVec3 z_axis = resolved_normal(face_normal, object_fallback);
  const quader::QVec3 x_axis = quader::cross(z_axis, edge_direction);
  if (!usable_vector(x_axis)) {
    return frame_from_normal_and_tangent(
        origin, object_fallback.z_axis, object_fallback.x_axis,
        object_fallback);
  }
  // The supporting face supplies the normal; its directed edge lies on -Y.
  return frame_from_normal_and_tangent(origin, z_axis, x_axis,
                                        object_fallback);
}

quader::QVec3 face_texture_v_gradient(const quader_poly::Document &document,
                                      const quader_poly::Face &face,
                                      quader::QVec3 normal) {
  const auto carrier = quader_poly::face_texture_carrier(document, face, normal);
  const auto component = [&](quader::QVec3 axis) {
    const quader::QVec2 uv = quader_poly::generated_texture_uv(axis, normal);
    return carrier[2] * uv.x + carrier[3] * uv.y;
  };
  return {component({1.0F, 0.0F, 0.0F}),
          component({0.0F, 1.0F, 0.0F}),
          component({0.0F, 0.0F, 1.0F})};
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
    const quader_poly::Transform3 &object_fallback,
    quader::QVec3 *directed_edge) {
  if (directed_edge != nullptr) {
    *directed_edge = {};
  }
  const quader_poly::Vertex *vertex =
      quader_poly::find_vertex(document, vertex_id);
  if (vertex == nullptr || !finite_vector(vertex->position)) {
    return std::nullopt;
  }

  for (const quader_poly::Face &face : document.faces) {
    const auto found = std::find(face.vertices.begin(), face.vertices.end(),
                                  vertex_id);
    if (found == face.vertices.end()) {
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(
        found - face.vertices.begin());
    const quader_poly::Vertex *next = quader_poly::find_vertex(
        document, face.vertices[(index + 1) % face.vertices.size()]);
    const quader::QVec3 normal = quader_poly::face_normal(document, face);
    if (next != nullptr && finite_vector(next->position) &&
        usable_vector(normal) && usable_vector(next->position - vertex->position)) {
      if (directed_edge != nullptr) {
        *directed_edge = next->position - vertex->position;
      }
      return frame_from_edge_and_normal(vertex->position,
                                        next->position - vertex->position,
                                        normal, object_fallback);
    }
  }
  return frame_from_normal_and_tangent(
      vertex->position, object_fallback.z_axis, object_fallback.x_axis,
      object_fallback);
}

std::optional<quader_poly::Transform3>
component_edge_local_orientation(
    const quader_poly::Document &document, quader_poly::Edge edge,
    const quader_poly::Transform3 &object_fallback,
    quader::QVec3 *directed_edge) {
  if (directed_edge != nullptr) {
    *directed_edge = {};
  }
  edge = quader_poly::make_edge(edge.a, edge.b);
  const quader_poly::Vertex *first =
      quader_poly::find_vertex(document, edge.a);
  const quader_poly::Vertex *second =
      quader_poly::find_vertex(document, edge.b);
  if (first == nullptr || second == nullptr ||
      !finite_vector(first->position) || !finite_vector(second->position)) {
    return std::nullopt;
  }

  const quader::QVec3 origin = (first->position + second->position) * 0.5F;
  bool live_edge = false;
  for (const quader_poly::Face &face : document.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const auto a = face.vertices[index];
      const auto b = face.vertices[(index + 1) % face.vertices.size()];
      if (quader_poly::make_edge(a, b) != edge) {
        continue;
      }
      live_edge = true;
      const quader::QVec3 normal = quader_poly::face_normal(document, face);
      if (usable_vector(normal)) {
        const quader::QVec3 direction = a == edge.a
            ? second->position - first->position
            : first->position - second->position;
        if (directed_edge != nullptr && usable_vector(direction)) {
          *directed_edge = direction;
        }
        return frame_from_edge_and_normal(origin, direction, normal,
                                          object_fallback);
      }
    }
  }
  return live_edge
      ? std::optional{frame_from_normal_and_tangent(
            origin, object_fallback.z_axis, object_fallback.x_axis,
            object_fallback)}
      : std::nullopt;
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

  const quader::QVec3 normal = quader_poly::face_normal(document, *face);
  // The texture gradient fixes in-plane roll independently of perimeter order.
  const quader::QVec3 tangent = quader::cross(
      normal, normalized_gradient(face_texture_v_gradient(document, *face, normal)));
  return frame_from_normal_and_tangent(
      *center, normal, tangent, object_fallback);
}

} // namespace quader_editor_core
