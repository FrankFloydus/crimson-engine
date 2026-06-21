#pragma once

#include "api/QuaderApiTypes.h"
#include "assets/AssetRevision.h"
#include "render/ViewportOverlayFrame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader {

struct QuaderEntitySnapshot {
    EntityId id = InvalidEntityId;
    std::string name;
    EntityKind kind = EntityKind::Generic;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
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
    ModelingObjectId sdkObjectId = InvalidModelingObjectId;
};

struct QuaderMeshSnapshot {
    AssetId mesh = InvalidAssetId;
    MeshAssetRevision revision;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::size_t primitiveCount = 0;
    bool stableCpuTopology = false;
    bool hasBounds = false;
    QuaderBounds bounds;
};

struct QuaderAssetSnapshot {
    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::uint64_t revision = 0;
};

struct QuaderHistorySnapshot {
    bool canUndo = false;
    bool canRedo = false;
    std::string undoLabel;
    std::string redoLabel;
};

struct QuaderApiSnapshot {
    QuaderRevisionStamp revisions;
    std::vector<QuaderEntitySnapshot> entities;
    std::vector<ModelingObjectRecord> modelingObjects;
    ModelingSelectionSummary selection;
    ModelingToolStatus toolStatus;
    QuaderAssetSnapshot assets;
    QuaderHistorySnapshot history;
    ViewportOverlayFrame selectionOverlay;
};

} // namespace quader
