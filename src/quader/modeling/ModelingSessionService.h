#pragma once

#include "quader/assets/MeshAsset.h"
#include "quader/core/Types.h"
#include "quader/math/Types.h"
#include "quader/render/ViewportOverlayFrame.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;

enum class ModelingCommandId {
    ActivateBoxTool,
    CreateBoxFromBounds,
};

struct ModelingCommandDescriptor {
    ModelingCommandId command = ModelingCommandId::ActivateBoxTool;
    std::string id;
    std::string label;
    std::string shortcut;
    bool viewportScoped = true;
    bool rebuildsMeshOnSuccess = false;
};

struct ModelingBounds {
    Vec3 min;
    Vec3 max;
};

enum class ModelingBoxDragState {
    Idle,
    Footprint,
    Height,
};

enum class ModelingSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
};

enum class ModelingActiveSelectionKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Light,
};

struct ModelingRay {
    Vec3 origin;
    Vec3 direction;
};

struct ModelingBoxToolEvent {
    Vec2 screenPosition;
    ModelingRay ray;
    double gridSize = 1.0;
};

struct ModelingBoxPreview {
    bool ok = false;
    bool valid = false;
    ModelingBoxDragState dragState = ModelingBoxDragState::Idle;
    std::string message;
    std::array<Vec3, 4> footprint = {};
    std::size_t footprintCount = 0;
    std::array<Vec3, 8> corners = {};
    std::size_t cornerCount = 0;
    Vec3 normal = {0.0f, 1.0f, 0.0f};
    double distance = 0.0;
    ModelingBounds bounds {};
    bool boundsEmpty = true;
};

struct ModelingCreateBoxRequest {
    ModelingBounds bounds {
        .min = {-1.0f, -1.0f, -1.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    std::string name = "Box";
};

struct ModelingPickSelectRequest {
    ModelingRay ray;
    int clickCount = 1;
    bool controlPressed = false;
    bool shiftPressed = false;
    bool commandPressed = false;
    bool altPressed = false;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
};

struct ModelingHoverRequest {
    ModelingRay ray;
    bool selectedModifierPressed = false;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
};

struct ModelingSelectionSummary {
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    bool hasSelection = false;
    ModelingActiveSelectionKind activeKind = ModelingActiveSelectionKind::None;
    int activeObjectId = -1;
    int activeVertexId = -1;
    int activeEdgeA = -1;
    int activeEdgeB = -1;
    int activeFaceId = -1;
    std::uint32_t selectedObjectCount = 0;
    std::uint32_t selectedVertexCount = 0;
    std::uint32_t selectedEdgeCount = 0;
    std::uint32_t selectedFaceCount = 0;
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
};

struct ModelingObjectRecord {
    int sdkObjectId = -1;
    EntityId entity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    std::string name;
};

struct ModelingCommandResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

struct ModelingCreateBoxResult {
    bool ok = false;
    bool changed = false;
    std::string message;
    ModelingObjectRecord object;
};

class ModelingSessionService {
public:
    ModelingSessionService(SceneDocument& scene, AssetRegistry& assets);
    ~ModelingSessionService();

    ModelingSessionService(const ModelingSessionService&) = delete;
    ModelingSessionService& operator=(const ModelingSessionService&) = delete;
    ModelingSessionService(ModelingSessionService&&) noexcept;
    ModelingSessionService& operator=(ModelingSessionService&&) noexcept;

    [[nodiscard]] bool sdkAvailable() const;
    [[nodiscard]] ModelingCommandDescriptor boxToolCommand() const;
    [[nodiscard]] ModelingCommandDescriptor createBoxFromBoundsCommand() const;
    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const;
    [[nodiscard]] std::optional<EntityId> entityForSdkObject(int sdkObjectId) const;
    [[nodiscard]] std::optional<int> sdkObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int sdkObjectId) const;
    [[nodiscard]] std::optional<Vec3> selectionCenter() const;
    [[nodiscard]] ModelingSelectionMode selectionMode() const;
    [[nodiscard]] ModelingSelectionSummary selectionSummary() const;
    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const;

    void syncObjectSelectionFromScene();
    void syncSceneSelectionFromSdk();
    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode);
    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request);
    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request);
    ModelingCommandResult clearSelectionHover();
    ModelingCommandResult activateBoxTool();
    ModelingCreateBoxResult createBoxFromBounds(const ModelingCreateBoxRequest& request);
    ModelingBoxPreview updateBoxHover(const ModelingBoxToolEvent& event);
    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event);
    ModelingBoxPreview updateBoxFootprint(const ModelingBoxToolEvent& event);
    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event);
    ModelingBoxPreview updateBoxHeight(const ModelingBoxToolEvent& event);
    ModelingCreateBoxResult commitBox(std::string name);
    ModelingCommandResult cancelBox();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
