////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "math/Types.h"
#include "scene/SceneDocument.h"

#include <optional>

namespace quader {

/**
 * Represents a Scene Bounds value used by the scene graph document layer.
 */
struct SceneBounds {
    Vec3 min;
    Vec3 max;
};

std::optional<SceneBounds> localMeshBounds(const MeshAsset& mesh);
void includePoint(SceneBounds& bounds, Vec3 point);
Vec3 boundsCenter(const SceneBounds& bounds);
std::optional<SceneBounds> entityWorldBounds(const SceneDocument& document, const AssetRegistry& assets, EntityId entity);
std::optional<SceneBounds> entityHierarchyWorldBounds(const SceneDocument& document, const AssetRegistry& assets, EntityId entity);
std::optional<SceneBounds> documentWorldBounds(const SceneDocument& document, const AssetRegistry& assets);
std::optional<SceneBounds> selectedWorldBounds(const SceneDocument& document, const AssetRegistry& assets);

} // namespace quader
