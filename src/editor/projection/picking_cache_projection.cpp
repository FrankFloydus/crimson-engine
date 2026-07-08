////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/picking_cache_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cstring>
#include <optional>

namespace quader {
namespace {

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

constexpr std::uint64_t kPickGeometryHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kPickGeometryHashPrime = 1099511628211ull;

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, std::uint64_t value) {
  return (hash ^ value) * kPickGeometryHashPrime;
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, int value) {
  return mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, std::uint32_t value) {
  return mix_pick_geometry_hash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, bool value) {
  return mix_pick_geometry_hash(hash, value ? 1ull : 0ull);
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return mix_pick_geometry_hash(hash, bits);
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, quader::QVec3 value) {
  hash = mix_pick_geometry_hash(hash, value.x);
  hash = mix_pick_geometry_hash(hash, value.y);
  hash = mix_pick_geometry_hash(hash, value.z);
  return hash;
}

void include_pick_bounds_point(quader_editor_core::PickObjectBounds &bounds,
                               quader::QVec3 point) {
  if (bounds.empty) {
    bounds.min = point;
    bounds.max = point;
    bounds.empty = false;
    return;
  }
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

} // namespace

std::vector<quader_editor_core::PolygonObject>
PickingCacheProjection::scene_transformed_objects_for_picking(
    const std::vector<quader_editor_core::PolygonObject> &objects,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map) {
  QDR_PROFILE_FUNCTION();
  std::vector<quader_editor_core::PolygonObject> transformed;
  transformed.reserve(objects.size());
  for (quader_editor_core::PolygonObject object : objects) {
    const std::optional<EntityId> entity =
        object_map.entity_for_modeling_object(object.id);
    if (!entity.has_value()) {
      continue;
    }
    const VisibilityComponent *visibility = scene.visibility(*entity);
    if (!scene.is_effectively_visible(*entity) ||
        (visibility != nullptr && !visibility->selectable)) {
      continue;
    }
    const QMat4 world_from_object = scene.world_transform_matrix(*entity);
    for (quader_poly::Vertex &vertex : object.document.vertices) {
      vertex.position = to_runtime(
          transform_point(world_from_object, from_runtime(vertex.position)));
    }
    transformed.push_back(std::move(object));
  }
  return transformed;
}

quader_editor_core::PickObjectBounds
PickingCacheProjection::build_object_pick_bounds(
    const quader_editor_core::PolygonObject &object) {
  QDR_PROFILE_FUNCTION();
  quader_editor_core::PickObjectBounds bounds;
  bounds.object_id = object.id;
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    include_pick_bounds_point(bounds, vertex.position);
  }
  return bounds;
}

std::uint64_t PickingCacheProjection::object_face_geometry_signature(
    const quader_editor_core::PolygonObject &object,
    quader_poly::PickOptions pick_options) {
  QDR_PROFILE_FUNCTION();
  std::uint64_t hash = mix_pick_geometry_hash(kPickGeometryHashSeed, object.id);
  hash = mix_pick_geometry_hash(hash, static_cast<int>(pick_options.mode));
  hash = mix_pick_geometry_hash(hash, pick_options.vertex_radius);
  hash = mix_pick_geometry_hash(hash, pick_options.edge_radius);
  hash = mix_pick_geometry_hash(hash, pick_options.has_hidden_material_slot);
  hash = mix_pick_geometry_hash(hash, pick_options.hidden_material_slot);
  hash = mix_pick_geometry_hash(hash, pick_options.pick_backfaces);
  hash = mix_pick_geometry_hash(hash, pick_options.prefer_front_facing_faces);
  hash = mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(object.document.vertices.size()));
  hash = mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(object.document.faces.size()));
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    hash = mix_pick_geometry_hash(hash, vertex.id);
    hash = mix_pick_geometry_hash(hash, vertex.position);
  }
  for (const quader_poly::Face &face : object.document.faces) {
    hash = mix_pick_geometry_hash(hash, face.id);
    hash = mix_pick_geometry_hash(hash, face.material_slot);
    hash = mix_pick_geometry_hash(
        hash, static_cast<std::uint64_t>(face.vertices.size()));
    for (const quader_poly::ElementId vertex_id : face.vertices) {
      hash = mix_pick_geometry_hash(hash, vertex_id);
    }
  }
  return hash;
}

} // namespace quader
