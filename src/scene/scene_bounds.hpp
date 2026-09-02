////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "math/types.hpp"
#include "scene/scene_document.hpp"

#include <optional>
#include <functional>

namespace quader {

/**
 * Represents a Scene Bounds value used by the scene graph document layer.
 */
struct SceneBounds {
    QVec3 min;
    QVec3 max;
};

/// Carries tight local and transformed AABBs computed in one vertex pass.
struct MeshLocalWorldBounds final {
  SceneBounds local;
  SceneBounds world;
};

std::optional<SceneBounds> local_mesh_bounds(const MeshAsset &mesh);
/// Computes authored cubic bounds and includes an enabled derived tube radius.
std::optional<SceneBounds> local_curve_bounds(
    const CurveAsset &curve, const CurveComponent &component);
std::optional<SceneBounds> curve_world_bounds(
    const CurveAsset &curve, const CurveComponent &component,
    const QMat4 &world_from_local);
/// Computes tight extents from transformed geometry, not transformed AABB
/// corners.
std::optional<MeshLocalWorldBounds> mesh_local_world_bounds(
    const MeshAsset &mesh, const QMat4 &world_from_local);
/// Computes the tight world AABB of the actual transformed mesh vertices.
std::optional<SceneBounds> mesh_world_bounds(
    const MeshAsset &mesh, const QMat4 &world_from_local);
std::optional<SceneBounds> world_bounds_from_local_mesh_bounds(
    const QMat4 &world_from_local,
    const std::optional<SceneBounds> &local_bounds);
void include_point(SceneBounds &bounds, QVec3 point);
QVec3 bounds_center(const SceneBounds &bounds);
std::optional<SceneBounds> entity_world_bounds(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               EntityId entity);
std::optional<SceneBounds> entity_world_bounds_from_local_mesh_bounds(
    const SceneDocument &document, EntityId entity,
    const std::optional<SceneBounds> &local_bounds);
std::optional<SceneBounds>
entity_hierarchy_world_bounds(const SceneDocument &document,
                              const AssetRegistry &assets, EntityId entity);
std::optional<SceneBounds> document_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets);
std::optional<SceneBounds> document_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const std::function<bool(EntityId)> &entity_visible);
std::optional<SceneBounds> selected_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets);
std::optional<SceneBounds>
selected_transform_world_bounds(const SceneDocument &document,
                                const AssetRegistry &assets);

} // namespace quader
