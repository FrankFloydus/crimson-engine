////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores the Quader Entity Snapshot data contract used by the public Quader API surface.
 */
struct QuaderEntitySnapshot {
    EntityId id = InvalidEntityId;
    std::string uuid;
    QuaderEditorObjectId editorObjectId = InvalidQuaderEditorObjectId;
    std::string name;
    QuaderEntityKind kind = QuaderEntityKind::Generic;
    EntityId parent = InvalidEntityId;
    QuaderTransform transform;
    bool visible = true;
    bool selectable = true;
    bool locked = false;
    bool selected = false;
    bool active = false;
    bool hasMesh = false;
    bool hasMaterial = false;
    bool hasModelingObject = false;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
    QuaderModelingObjectId modelingObjectId = InvalidQuaderModelingObjectId;
};

/**
 * Stores the Quader Mesh Snapshot data contract used by the public Quader API surface.
 */
struct QuaderMeshSnapshot {
    AssetId mesh = InvalidAssetId;
    std::uint64_t revision = 0;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::size_t primitiveCount = 0;
    bool stableCpuTopology = false;
    bool hasBounds = false;
    QuaderBounds bounds;
};

/**
 * Stores the Quader Asset Snapshot data contract used by the public Quader API surface.
 */
struct QuaderAssetSnapshot {
    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::uint64_t revision = 0;
};

/**
 * Stores the Quader History Snapshot data contract used by the public Quader API surface.
 */
struct QuaderHistorySnapshot {
    bool canUndo = false;
    bool canRedo = false;
    std::string undoLabel;
    std::string redoLabel;
};

/**
 * Stores the Quader API Snapshot data contract used by the public Quader API surface.
 */
struct QuaderApiSnapshot {
    QuaderRevisionStamp revisions;
    std::vector<QuaderEntitySnapshot> entities;
    std::vector<QuaderModelingObjectRecord> modelingObjects;
    QuaderSelectionSummary selection;
    QuaderToolStatus toolStatus;
    QuaderAssetSnapshot assets;
    QuaderHistorySnapshot history;
    QuaderOverlayFrame selectionOverlay;
};

} // namespace quader
