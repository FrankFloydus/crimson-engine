#pragma once

#include "assets/AssetRegistry.h"
#include "math/Types.h"
#include "scene/SceneDocument.h"

#include <optional>

namespace quader {

struct SceneBounds {
    Vec3 min;
    Vec3 max;
};

std::optional<SceneBounds> localMeshBounds(const MeshAsset& mesh);
void includePoint(SceneBounds& bounds, Vec3 point);
Vec3 boundsCenter(const SceneBounds& bounds);
std::optional<SceneBounds> entityWorldBounds(const SceneDocument& document, const AssetRegistry& assets, EntityId entity);
std::optional<SceneBounds> documentWorldBounds(const SceneDocument& document, const AssetRegistry& assets);
std::optional<SceneBounds> selectedWorldBounds(const SceneDocument& document, const AssetRegistry& assets);

} // namespace quader
