////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>

namespace quader_editor_core {
namespace {

quader_geometry::QVec3f to_geometry_vec3(quader::QVec3 value) {
  return quader_geometry::QVec3f(value);
}

quader::QVec3 to_poly_vec3(quader_geometry::QVec3f value) {
  return {value.x, value.y, value.z};
}

void include_point(Bounds3 &bounds, quader::QVec3 point) {
  quader_geometry::QAabb3f geometry_bounds =
      bounds.empty
          ? quader_geometry::empty_aabb3<float>()
          : quader_geometry::QAabb3f{to_geometry_vec3(bounds.min),
                                    to_geometry_vec3(bounds.max)};
  quader_geometry::aabb_include(geometry_bounds, to_geometry_vec3(point));
  bounds.empty = !quader_geometry::aabb_is_valid(geometry_bounds);
  if (!bounds.empty) {
    bounds.min = to_poly_vec3(geometry_bounds.min);
    bounds.max = to_poly_vec3(geometry_bounds.max);
  }
}

} // namespace

Bounds3 document_bounds(const std::vector<PolygonObject> &objects) {
  Bounds3 bounds;
  for (const PolygonObject &object : objects) {
    const Bounds3 object_bounds = document_bounds(object);
    if (!object_bounds.empty) {
      include_point(bounds, object_bounds.min);
      include_point(bounds, object_bounds.max);
    }
  }
  return bounds;
}

Bounds3 document_bounds(const PolygonObject &object) {
  Bounds3 bounds;
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    include_point(bounds, vertex.position);
  }
  return bounds;
}

Bounds3 selection_bounds(const std::vector<PolygonObject> &objects,
                         SelectionMode mode) {
  Bounds3 bounds;
  for (const PolygonObject &object : objects) {
    if (!object.selected) {
      continue;
    }
    if (mode == SelectionMode::Object) {
      for (const quader_poly::Vertex &vertex : object.document.vertices) {
        include_point(bounds, vertex.position);
      }
      continue;
    }
    for (const quader_poly::ElementId vertex_id :
         quader_poly::selected_vertex_ids(object.document, object.selection)) {
      if (const quader_poly::Vertex *vertex =
              quader_poly::find_vertex(object.document, vertex_id)) {
        include_point(bounds, vertex->position);
      }
    }
  }
  return bounds;
}

} // namespace quader_editor_core
