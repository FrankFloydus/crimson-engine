////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/picking_cache_projection.hpp"

#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cstring>

namespace quader {
namespace {

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

quader::QVec3 toRuntime(QVec3 value) {
    return {value.x, value.y, value.z};
}

constexpr std::uint64_t kPickGeometryHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kPickGeometryHashPrime = 1099511628211ull;

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * kPickGeometryHashPrime;
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, int value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint32_t value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, bool value) {
    return mixPickGeometryHash(hash, value ? 1ull : 0ull);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixPickGeometryHash(hash, bits);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, quader::QVec3 value) {
    hash = mixPickGeometryHash(hash, value.x);
    hash = mixPickGeometryHash(hash, value.y);
    hash = mixPickGeometryHash(hash, value.z);
    return hash;
}

void includePickBoundsPoint(quader_editor_core::PickObjectBounds& bounds, quader::QVec3 point) {
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

std::vector<quader_editor_core::PolygonObject> PickingCacheProjection::sceneTransformedObjectsForPicking(
        const std::vector<quader_editor_core::PolygonObject>& objects,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectMap) {
    std::vector<quader_editor_core::PolygonObject> transformed;
    transformed.reserve(objects.size());
    for (quader_editor_core::PolygonObject object : objects) {
        const auto entity = objectMap.entitiesByObject().find(object.id);
        if (entity == objectMap.entitiesByObject().end()) {
            continue;
        }
        const VisibilityComponent* visibility = scene.visibility(entity->second);
        if (!scene.isEffectivelyVisible(entity->second) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }
        const QMat4 worldFromObject = scene.worldTransformMatrix(entity->second);
        for (quader_poly::Vertex& vertex : object.document.vertices) {
            vertex.position = toRuntime(transformPoint(worldFromObject, fromRuntime(vertex.position)));
        }
        transformed.push_back(std::move(object));
    }
    return transformed;
}

quader_editor_core::PickObjectBounds PickingCacheProjection::buildObjectPickBounds(
        const quader_editor_core::PolygonObject& object) {
    quader_editor_core::PickObjectBounds bounds;
    bounds.object_id = object.id;
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        includePickBoundsPoint(bounds, vertex.position);
    }
    return bounds;
}

std::uint64_t PickingCacheProjection::objectFaceGeometrySignature(
        const quader_editor_core::PolygonObject& object,
        quader_poly::PickOptions pickOptions) {
    std::uint64_t hash = mixPickGeometryHash(kPickGeometryHashSeed, object.id);
    hash = mixPickGeometryHash(hash, static_cast<int>(pickOptions.mode));
    hash = mixPickGeometryHash(hash, pickOptions.vertex_radius);
    hash = mixPickGeometryHash(hash, pickOptions.edge_radius);
    hash = mixPickGeometryHash(hash, pickOptions.has_hidden_material_slot);
    hash = mixPickGeometryHash(hash, pickOptions.hidden_material_slot);
    hash = mixPickGeometryHash(hash, pickOptions.pick_backfaces);
    hash = mixPickGeometryHash(hash, pickOptions.prefer_front_facing_faces);
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.vertices.size()));
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.faces.size()));
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        hash = mixPickGeometryHash(hash, vertex.id);
        hash = mixPickGeometryHash(hash, vertex.position);
    }
    for (const quader_poly::Face& face : object.document.faces) {
        hash = mixPickGeometryHash(hash, face.id);
        hash = mixPickGeometryHash(hash, face.material_slot);
        hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(face.vertices.size()));
        for (const quader_poly::ElementId vertexId : face.vertices) {
            hash = mixPickGeometryHash(hash, vertexId);
        }
    }
    return hash;
}

} // namespace quader
