#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "modeling/ModelingSessionService.h"
#include "render/ViewportOverlayFrame.h"
#include "scene/Components.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef QUADER_TEST_HAS_PROGRAMMATIC_API
#if defined(QUADER_TEST_ENABLE_PROGRAMMATIC_API) && QUADER_TEST_ENABLE_PROGRAMMATIC_API && __has_include("api/QuaderApi.h")
#define QUADER_TEST_HAS_PROGRAMMATIC_API 1
#include "api/QuaderApi.h"
#else
#define QUADER_TEST_HAS_PROGRAMMATIC_API 0
#endif
#endif

namespace quader::test {

struct BehaviorBounds {
    Vec3 min = {};
    Vec3 max = {};
};

struct EntitySnapshot {
    EntityId id = InvalidEntityId;
    std::string alias;
    std::string name;
    TransformComponent transform;
    bool selected = false;
    AssetId mesh = InvalidAssetId;
    ModelingObjectId sdkObjectId = InvalidModelingObjectId;
};

struct MeshSnapshot {
    AssetId mesh = InvalidAssetId;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    BehaviorBounds bounds;
};

struct ViewportOverlayStats {
    std::size_t triangleCount = 0;
    std::size_t lineCount = 0;
    std::size_t pointCount = 0;
    std::unordered_map<ViewportOverlayLayer, std::size_t> linesByLayer;
    std::unordered_map<ViewportOverlayLayer, std::size_t> pointsByLayer;
    std::unordered_map<ViewportOverlayLayer, std::size_t> trianglesByLayer;
};

struct BehaviorSnapshot {
    std::string label;
    ModelingSelectionSummary selection;
    ModelingToolStatus toolStatus;
    std::vector<EntitySnapshot> entities;
    std::vector<MeshSnapshot> meshes;
    ViewportOverlayStats overlay;
    std::optional<BehaviorBounds> selectedBounds;
};

} // namespace quader::test
