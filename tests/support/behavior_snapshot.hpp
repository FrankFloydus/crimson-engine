////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/components.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader::test {

/**
 * Represents a Behavior Bounds value used by the behavior test DSL.
 */
struct BehaviorBounds {
    QVec3 min = {};
    QVec3 max = {};
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
    std::vector<QVec3> positions;
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
