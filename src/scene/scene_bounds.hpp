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

namespace quader {

/**
 * Represents a Scene Bounds value used by the scene graph document layer.
 */
struct SceneBounds {
    QVec3 min;
    QVec3 max;
};

std::optional<SceneBounds> local_mesh_bounds(const MeshAsset &mesh);
void include_point(SceneBounds &bounds, QVec3 point);
QVec3 bounds_center(const SceneBounds &bounds);
std::optional<SceneBounds> entity_world_bounds(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               EntityId entity);
std::optional<SceneBounds>
entity_hierarchy_world_bounds(const SceneDocument &document,
                              const AssetRegistry &assets, EntityId entity);
std::optional<SceneBounds> document_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets);
std::optional<SceneBounds> selected_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets);
std::optional<SceneBounds>
selected_transform_world_bounds(const SceneDocument &document,
                                const AssetRegistry &assets);

} // namespace quader
