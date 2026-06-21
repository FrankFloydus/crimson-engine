#pragma once

#include "modeling/ModelingActionCatalog.h"
#include "assets/MeshAsset.h"
#include "core/Types.h"
#include "math/Types.h"
#include "render/ViewportOverlayFrame.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;

enum class ModelingCommandId {
    ActivateBoxTool,
    CreateBoxFromBounds,
    SelectAll,
    InvertSelection,
    ClearSelection,
    DeleteSelection,
    ExecuteInstantOperation,
};

enum class ModelingInstantOperationId {
    DeleteSelection,
    DuplicateMeshes,
    CombineMeshes,
    ExtractFaces,
    DetachFaces,
    Bridge,
    SnapVerticesToActive,
    ConnectVertices,
    DissolveVertices,
    BevelVertices,
    MergeVerticesToActive,
    MergeVerticesToCenter,
    SnapEdgesToActive,
    ConnectEdges,
    SplitEdges,
    DissolveEdges,
    MergeEdges,
    CollapseEdges,
    FillHole,
    CombineFaces,
    CollapseFaces,
    RadialAlign,
    FlipFaceNormals,
    RecalculateFaceNormals,
    ShadeFacesSmooth,
    ShadeFacesFlat,
    HardenEdgeNormals,
    SoftenEdgeNormals,
};

enum class ModelingMirrorAxis {
    X,
    Y,
    Z,
};

enum class ModelingFlipOrientation {
    Horizontal,
    Vertical,
};

enum class ModelingTransformTool {
    Move,
    Rotate,
    Scale,
};

enum class ModelingViewportTool {
    Select,
    Move,
    Rotate,
    Scale,
    Box,
    InsertEdgeLoop,
    Knife,
    Cut,
    Poly,
    Mirror,
};

enum class ModelingCutMode {
    DiscardLeft,
    DiscardRight,
    KeepBoth,
};

enum class ModelingTransformOperation {
    Transform,
    Extrude,
    Inset,
};

enum class ModelingTransformAxis {
    None,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ,
    All,
};

struct ModelingCommandDescriptor {
    ModelingCommandId command = ModelingCommandId::ActivateBoxTool;
    std::string id;
    std::string label;
    std::string shortcut;
    bool viewportScoped = true;
    bool rebuildsMeshOnSuccess = false;
};

struct ModelingInstantOperationDescriptor {
    ModelingInstantOperationId operation = ModelingInstantOperationId::DeleteSelection;
    std::string id;
    std::string label;
    bool enabled = false;
    bool rebuildsMeshOnSuccess = true;
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

struct ModelingViewportCamera {
    bool orthographic = false;
    Vec3 origin = {};
    Vec3 xAxis = {1.0f, 0.0f, 0.0f};
    Vec3 yAxis = {0.0f, 1.0f, 0.0f};
    Vec3 zAxis = {0.0f, 0.0f, 1.0f};
    Vec2 viewportSize = {1.0f, 1.0f};
    double orthographicSize = 24.0;
    double fovDegrees = 60.0;
};

struct ModelingTransform3 {
    Vec3 xAxis = {1.0f, 0.0f, 0.0f};
    Vec3 yAxis = {0.0f, 1.0f, 0.0f};
    Vec3 zAxis = {0.0f, 0.0f, 1.0f};
    Vec3 origin = {};
};

struct ModelingBoxToolEvent {
    Vec2 screenPosition;
    ModelingRay ray;
    double gridSize = 1.0;
    bool hasCamera = false;
    ModelingViewportCamera camera;
};

struct ModelingToolRayEvent {
    Vec2 screenPosition;
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
    bool snapEnabled = true;
    bool constrainAxis = false;
    bool dragging = false;
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
    bool preserveSelectionOnMiss = false;
    bool preserveSelectionOnSelectedHit = false;
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

struct ModelingRepeatTransform {
    bool active = false;
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    bool duplicateSelected = false;
    Vec3 moveDelta = {};
    double rotateAngleDegrees = 0.0;
    Vec3 scale = {1.0f, 1.0f, 1.0f};
};

struct ModelingToolStatus {
    ModelingViewportTool activeTool = ModelingViewportTool::Select;
    bool modalToolOwnsViewport = false;
    bool insertEdgeLoopActive = false;
    bool insertEdgeLoopDragging = false;
    bool insertEdgeLoopHasPreview = false;
    bool knifeActive = false;
    bool knifeHasCurrent = false;
    bool knifeCanCommit = false;
    bool cutActive = false;
    bool cutCanPreview = false;
    bool cutPreviewValid = false;
    bool cutDragging = false;
    bool cutDraggingNewPoint = false;
    bool cutDraggingLine = false;
    int cutDraggingPointIndex = -1;
    int cutPointCount = 0;
    int cutCapturedPointCount = 0;
    bool cutHasLine = false;
    std::array<Vec3, 3> cutPoints = {};
    ModelingCutMode cutMode = ModelingCutMode::KeepBoth;
    bool mirrorActive = false;
    bool mirrorCanPreview = false;
    bool mirrorPreviewValid = false;
    bool mirrorDragging = false;
    bool mirrorDraggingNewPoint = false;
    bool mirrorDraggingLine = false;
    int mirrorDraggingPointIndex = -1;
    int mirrorPointCount = 0;
    int mirrorCapturedPointCount = 0;
    bool mirrorHasLine = false;
    std::array<Vec3, 3> mirrorPoints = {};
    bool polyActive = false;
    bool polyClosed = false;
    bool polyHasCurrent = false;
    bool polyCanCommit = false;
    int polyPointCount = 0;
};

enum class ModelingToolSettingsTargetKind {
    None,
    Cut,
    Mirror,
    Bridge,
    EdgeBevel,
    Thicken,
    SliceQuad,
    MergeByDistance,
};

struct ModelingToolSettingsTarget {
    ModelingToolSettingsTargetKind kind = ModelingToolSettingsTargetKind::None;
    bool active = false;
    bool operationPreviewActive = false;
    std::string label;
    std::string actionId;
    std::string operationId;
};

struct ModelingInteractionSettings {
    int bridgeSteps = 1;
    double edgeBevelWidth = 1.0;
    double edgeBevelProfile = 0.5;
    int edgeBevelSegments = 1;
    double thickenThickness = 0.25;
    bool thickenFromCenter = false;
    int sliceQuadXSlices = 1;
    int sliceQuadYSlices = 1;
    double mergeByDistanceTolerance = 0.125;
    ModelingCutMode cutMode = ModelingCutMode::KeepBoth;
};

struct ModelingToolPreviewMesh {
    EntityId sourceEntity = InvalidEntityId;
    AssetId material = InvalidAssetId;
    MeshAsset mesh;
};

struct ModelingTransformDragBeginRequest {
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    Vec3 pivot = {};
    Vec2 screenPosition = {};
    Vec2 screenPivot = {};
    Vec2 screenTip = {};
    bool hasWorldDirection = false;
    Vec3 worldDirection = {};
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool duplicateSelected = false;
};

struct ModelingTransformDragUpdateRequest {
    Vec2 screenPosition = {};
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool precision = false;
};

struct ModelingTransformDragPreview {
    bool ok = false;
    bool active = false;
    bool changed = false;
    bool previewDriven = false;
    std::string message;
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    Vec3 pivot = {};
    Vec3 moveDelta = {};
    double rotateAngleDegrees = 0.0;
    Vec3 scale = {1.0f, 1.0f, 1.0f};
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    bool sceneTransform = false;
    bool hasScenePreviewTransform = false;
    ModelingTransform3 scenePreviewTransform = {};
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
    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    void syncObjectSelectionFromScene();
    void syncSceneSelectionFromSdk();
    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode);
    ModelingCommandResult selectAll();
    ModelingCommandResult invertSelection();
    ModelingCommandResult clearSelection();
    ModelingCommandResult deleteSelection();
    ModelingCommandResult copySelection();
    ModelingCommandResult pasteSelection();
    ModelingCommandResult undo();
    ModelingCommandResult redo();
    ModelingCommandResult repeatLastAction();
    ModelingCommandResult mirrorSelection(ModelingMirrorAxis axis);
    ModelingCommandResult flipSelection(ModelingFlipOrientation orientation);
    ModelingCommandResult applyActiveMaterial(std::uint32_t materialSlot = 0);
    [[nodiscard]] bool modalToolOwnsViewport() const;
    [[nodiscard]] ModelingToolStatus toolStatus() const;
    [[nodiscard]] ModelingToolSettingsTarget toolSettingsTarget() const;
    [[nodiscard]] ModelingInteractionSettings interactionSettings() const;
    ModelingCommandResult setInteractionSettings(const ModelingInteractionSettings& settings);
    [[nodiscard]] ModelingActionSet contextActionSet(const ModelingActionQuery& query = {}) const;
    [[nodiscard]] std::vector<ModelingActionItem> contextActions(const ModelingActionQuery& query = {}) const;
    ModelingCommandResult executeContextAction(std::string_view actionId);
    ModelingCommandResult commitActiveOperationPreview();
    ModelingCommandResult cancelActiveOperationPreview();
    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const;
    ModelingCommandResult executeInstantOperation(ModelingInstantOperationId operation);
    ModelingCommandResult beginTransformPreview(bool duplicateSelected);
    ModelingCommandResult updateTransformTranslatePreview(Vec3 delta);
    ModelingCommandResult updateTransformMatrixPreview(const ModelingTransform3& transform, Vec3 pivot);
    ModelingCommandResult updateTransformExtrudePreview(Vec3 offset, float closedEdgeLedgeSize = 0.0f);
    ModelingCommandResult updateTransformInsetPreview(const ModelingTransform3& transform, Vec3 pivot);
    ModelingCommandResult commitTransformPreview();
    ModelingCommandResult cancelTransformPreview();
    ModelingCommandResult recordRepeatableTransform(const ModelingRepeatTransform& transform);
    ModelingTransformDragPreview beginTransformDrag(const ModelingTransformDragBeginRequest& request);
    ModelingTransformDragPreview updateTransformDrag(const ModelingTransformDragUpdateRequest& request);
    ModelingTransformDragPreview commitTransformDrag();
    ModelingCommandResult cancelTransformDrag();
    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request);
    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request);
    ModelingCommandResult clearSelectionHover();
    ModelingCommandResult activateViewportTool(ModelingViewportTool tool);
    ModelingCommandResult cancelActiveModalTool();
    ModelingCommandResult updateInsertEdgeLoopHover(const ModelingToolRayEvent& event);
    ModelingCommandResult beginInsertEdgeLoopDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult updateInsertEdgeLoopDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult commitInsertEdgeLoopDrag();
    ModelingCommandResult updateKnifeHover(const ModelingToolRayEvent& event);
    ModelingCommandResult acceptKnifePoint(const ModelingToolRayEvent& event);
    ModelingCommandResult commitKnifeTool();
    ModelingCommandResult updateCutHover(const ModelingToolRayEvent& event);
    ModelingCommandResult clickCut(const ModelingToolRayEvent& event);
    ModelingCommandResult beginCutPointDrag(int pointIndex);
    ModelingCommandResult beginCutLineDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult endCutDrag();
    ModelingCommandResult setCutMode(ModelingCutMode mode);
    ModelingCommandResult cycleCutMode();
    ModelingCommandResult finalizeCutTool();
    ModelingCommandResult updateMirrorHover(const ModelingToolRayEvent& event);
    ModelingCommandResult clickMirror(const ModelingToolRayEvent& event);
    ModelingCommandResult beginMirrorPointDrag(int pointIndex);
    ModelingCommandResult beginMirrorLineDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult endMirrorDrag();
    ModelingCommandResult finalizeMirrorTool();
    ModelingCommandResult updatePolyHover(const ModelingToolRayEvent& event);
    ModelingCommandResult acceptPolyPoint();
    ModelingCommandResult commitPolyTool();
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
