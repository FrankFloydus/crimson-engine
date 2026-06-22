////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "modeling/ModelingTypes.h"
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

/**
 * Represents a Behavior Bounds value used by the behavior test DSL.
 */
struct BehaviorBounds {
    Vec3 min = {};
    Vec3 max = {};
};

/**
 * Stores the Entity Snapshot data contract used by the behavior test DSL.
 */
struct EntitySnapshot {
    EntityId id = InvalidEntityId;
    std::string alias;
    std::string name;
    TransformComponent transform;
    bool selected = false;
    AssetId mesh = InvalidAssetId;
    ModelingObjectId modelingObjectId = InvalidModelingObjectId;
};

/**
 * Stores the Mesh Snapshot data contract used by the behavior test DSL.
 */
struct MeshSnapshot {
    AssetId mesh = InvalidAssetId;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    BehaviorBounds bounds;
};

/**
 * Represents a Viewport Overlay Stats value used by the behavior test DSL.
 */
struct ViewportOverlayStats {
    std::size_t triangleCount = 0;
    std::size_t lineCount = 0;
    std::size_t pointCount = 0;
    std::unordered_map<ViewportOverlayLayer, std::size_t> linesByLayer;
    std::unordered_map<ViewportOverlayLayer, std::size_t> pointsByLayer;
    std::unordered_map<ViewportOverlayLayer, std::size_t> trianglesByLayer;
};

/**
 * Stores the Behavior Snapshot data contract used by the behavior test DSL.
 */
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
