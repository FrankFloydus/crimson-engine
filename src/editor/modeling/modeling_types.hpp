////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <mesh/polygon/document.hpp>

namespace quader {

/**
 * Enumerates ModelingCommandId values used by the modeling layer.
 */
enum class ModelingCommandId {
    ActivateBoxTool,
    CreateBoxFromBounds,
    SelectAll,
    InvertSelection,
    ClearSelection,
    DeleteSelection,
    ExecuteInstantOperation,
};

/**
 * Enumerates ModelingInstantOperationId values used by the modeling layer.
 */
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

/**
 * Enumerates ModelingMirrorAxis values used by the modeling layer.
 */
enum class ModelingMirrorAxis {
    X,
    Y,
    Z,
};

/**
 * Enumerates ModelingFlipOrientation values used by the modeling layer.
 */
enum class ModelingFlipOrientation {
    Horizontal,
    Vertical,
};

/**
 * Enumerates ModelingTransformTool values used by the modeling layer.
 */
enum class ModelingTransformTool {
    Move,
    Rotate,
    Scale,
};

/**
 * Enumerates ModelingViewportTool values used by the modeling layer.
 */
enum class ModelingViewportTool {
    Select,
    Move,
    Extend,
    Extrude,
    Rotate,
    Scale,
    Box,
    InsertEdgeLoop,
    Knife,
    Cut,
    Poly,
    Mirror,
};

/**
 * Enumerates ModelingCutMode values used by the modeling layer.
 */
enum class ModelingCutMode {
    DiscardLeft,
    DiscardRight,
    KeepBoth,
};

/**
 * Enumerates ModelingTransformOperation values used by the modeling layer.
 */
enum class ModelingTransformOperation {
    Transform,
    Extrude,
    Inset,
};

/**
 * Enumerates ModelingTransformAxis values used by the modeling layer.
 */
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

/**
 * Stores the Modeling Command Descriptor data contract used by the modeling domain.
 */
struct ModelingCommandDescriptor {
    ModelingCommandId command = ModelingCommandId::ActivateBoxTool;
    std::string id;
    std::string label;
    std::string shortcut;
    bool viewportScoped = true;
    bool rebuildsMeshOnSuccess = false;
};

/**
 * Stores the Modeling Instant Operation Descriptor data contract used by the modeling domain.
 */
struct ModelingInstantOperationDescriptor {
    ModelingInstantOperationId operation = ModelingInstantOperationId::DuplicateMeshes;
    std::string id;
    std::string label;
    bool enabled = false;
    bool rebuildsMeshOnSuccess = true;
};

/**
 * Represents a Modeling Bounds value used by the modeling domain.
 */
struct ModelingBounds {
    QVec3 min;
    QVec3 max;
};

/**
 * Enumerates ModelingBoxDragState values used by the modeling layer.
 */
enum class ModelingBoxDragState {
    Idle,
    Footprint,
    Height,
};

/**
 * Enumerates ModelingSelectionMode values used by the modeling layer.
 */
enum class ModelingSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
    Group,
};

/**
 * Enumerates ModelingActiveSelectionKind values used by the modeling layer.
 */
enum class ModelingActiveSelectionKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Light,
};

/**
 * Represents a Modeling Ray value used by the modeling domain.
 */
struct ModelingRay {
    QVec3 origin;
    QVec3 direction;
};

/**
 * Represents a Modeling Viewport Camera value used by the modeling domain.
 */
struct ModelingViewportCamera {
    bool orthographic = false;
    QVec3 origin = {};
    QVec3 xAxis = {1.0f, 0.0f, 0.0f};
    QVec3 yAxis = {0.0f, 1.0f, 0.0f};
    QVec3 zAxis = {0.0f, 0.0f, 1.0f};
    QVec2 viewportSize = {1.0f, 1.0f};
    double orthographicSize = 24.0;
    double fovDegrees = 60.0;
};

/**
 * Represents a Modeling Transform3 value used by the modeling domain.
 */
struct ModelingTransform3 {
    QVec3 xAxis = {1.0f, 0.0f, 0.0f};
    QVec3 yAxis = {0.0f, 1.0f, 0.0f};
    QVec3 zAxis = {0.0f, 0.0f, 1.0f};
    QVec3 origin = {};
};

/**
 * Stores the Modeling Box Tool Event data contract used by the modeling domain.
 */
struct ModelingBoxToolEvent {
    QVec2 screenPosition;
    ModelingRay ray;
    double gridSize = 1.0;
    bool hasCamera = false;
    ModelingViewportCamera camera;
    bool snapEnabled = true;
};

/**
 * Stores the Modeling Tool Ray Event data contract used by the modeling domain.
 */
struct ModelingToolRayEvent {
    QVec2 screenPosition;
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
    bool snapEnabled = true;
    bool constrainAxis = false;
    bool dragging = false;
};

/**
 * Represents a Modeling Box Preview value used by the modeling domain.
 */
struct ModelingBoxPreview {
    bool ok = false;
    bool valid = false;
    ModelingBoxDragState dragState = ModelingBoxDragState::Idle;
    std::string message;
    std::array<QVec3, 4> footprint = {};
    std::size_t footprintCount = 0;
    std::array<QVec3, 8> corners = {};
    std::size_t cornerCount = 0;
    QVec3 normal = {0.0f, 1.0f, 0.0f};
    double distance = 0.0;
    ModelingBounds bounds {};
    bool boundsEmpty = true;
};

/**
 * Stores the Modeling Create Box Request data contract used by the modeling domain.
 */
struct ModelingCreateBoxRequest {
    ModelingBounds bounds {
        .min = {-1.0f, -1.0f, -1.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    std::string name = "Box";
};

/**
 * Stores the Modeling Create Mesh Request data contract used by the modeling domain.
 */
struct ModelingCreateMeshRequest {
    quader_poly::Document document;
    std::string name = "Mesh";
    std::uint32_t materialSlot = 0;
};

/**
 * Enumerates ModelingSelectionEdit values used by the modeling layer.
 */
enum class ModelingSelectionEdit {
    Replace,
    Add,
    Remove,
    Toggle,
};

/**
 * Enumerates ModelingBoxSelectionRule values used by the modeling layer.
 */
enum class ModelingBoxSelectionRule {
    DragDirection,
    Contain,
    Crossing,
};

/**
 * Stores the Modeling Component Selection Request data contract used by the modeling domain.
 */
struct ModelingComponentSelectionRequest {
    int modelingObjectId = -1;
    ModelingSelectionMode mode = ModelingSelectionMode::Vertex;
    std::vector<int> vertices;
    std::vector<std::pair<int, int>> edges;
    std::vector<int> faces;
    ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
    ModelingActiveSelectionKind activeKind = ModelingActiveSelectionKind::None;
    int activeVertex = -1;
    std::pair<int, int> activeEdge = {-1, -1};
    int activeFace = -1;
};

/**
 * Stores the Modeling Direct Hover Request data contract used by the modeling domain.
 */
struct ModelingDirectHoverRequest {
    int modelingObjectId = -1;
    ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
    int vertexId = -1;
    std::pair<int, int> edge = {-1, -1};
    int faceId = -1;
    bool selectedModifierPressed = false;
};

/**
 * Stores the Modeling Pick Select Request data contract used by the modeling domain.
 */
struct ModelingPickSelectRequest {
    ModelingRay ray;
    int clickCount = 1;
    bool controlPressed = false;
    bool shiftPressed = false;
    bool commandPressed = false;
    bool altPressed = false;
    bool preserveSelectionOnMiss = false;
    bool preserveSelectionOnSelectedHit = false;
    bool preserveExistingComponentSelectionOnHit = false;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
};

/**
 * Stores the Modeling Box Select Request data contract used by the modeling domain.
 */
struct ModelingBoxSelectRequest {
    ModelingViewportCamera camera;
    QVec2 start = {};
    QVec2 end = {};
    ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
    ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::DragDirection;
};

/**
 * Stores the Modeling Hover Request data contract used by the modeling domain.
 */
struct ModelingHoverRequest {
    ModelingRay ray;
    bool selectedModifierPressed = false;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
};

/**
 * Represents a Modeling Hover Target value used by the modeling domain.
 */
struct ModelingHoverTarget {
    ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
    int objectId = -1;
    int vertexId = -1;
    int edgeA = -1;
    int edgeB = -1;
    int faceId = -1;
};

/**
 * Represents a Modeling Hover Summary value used by the modeling domain.
 */
struct ModelingHoverSummary {
    bool changed = false;
    ModelingHoverTarget target;
    bool targetSelected = false;
};

/**
 * Represents a Modeling Selection Summary value used by the modeling domain.
 */
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

/**
 * Stores selection overlay projection options for callers that have dedicated renderers for some
 * selection feedback.
 */
struct ModelingSelectionOverlayOptions {
    bool suppressPersistentObjectSelection = false;
};

/**
 * Stores the Modeling Object Record data contract used by the modeling domain.
 */
struct ModelingObjectRecord {
    int modelingObjectId = -1;
    EntityId entity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    std::string name;
};

/**
 * Stores the Modeling Command Result data contract used by the modeling domain.
 */
struct ModelingCommandResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

/**
 * Represents a Modeling Repeat Transform value used by the modeling domain.
 */
struct ModelingRepeatTransform {
    bool active = false;
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    bool duplicateSelected = false;
    QVec3 moveDelta = {};
    double rotateAngleDegrees = 0.0;
    QVec3 scale = {1.0f, 1.0f, 1.0f};
};

/**
 * Represents a Modeling Tool Status value used by the modeling domain.
 */
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
    std::array<QVec3, 3> cutPoints = {};
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
    std::array<QVec3, 3> mirrorPoints = {};
    bool polyActive = false;
    bool polyClosed = false;
    bool polyHasCurrent = false;
    bool polyCanCommit = false;
    int polyPointCount = 0;
};

/**
 * Enumerates ModelingToolSettingsTargetKind values used by the modeling layer.
 */
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

/**
 * Represents a Modeling Tool Settings Target value used by the modeling domain.
 */
struct ModelingToolSettingsTarget {
    ModelingToolSettingsTargetKind kind = ModelingToolSettingsTargetKind::None;
    bool active = false;
    bool operationPreviewActive = false;
    std::string label;
    std::string actionId;
    std::string operationId;
};

/**
 * Represents a Modeling Interaction Settings value used by the modeling domain.
 */
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

/**
 * Represents a Modeling Tool Preview Mesh value used by the modeling domain.
 */
struct ModelingToolPreviewMesh {
    EntityId sourceEntity = InvalidEntityId;
    AssetId material = InvalidAssetId;
    MeshAsset mesh;
};

/**
 * Stores the Modeling Transform Drag Begin Request data contract used by the modeling domain.
 */
struct ModelingTransformDragBeginRequest {
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    QVec3 pivot = {};
    QVec2 screenPosition = {};
    QVec2 screenPivot = {};
    QVec2 screenTip = {};
    bool hasWorldDirection = false;
    QVec3 worldDirection = {};
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool gridSnapEnabled = true;
    bool precision = false;
    bool duplicateSelected = false;
};

/**
 * Stores the Modeling Transform Drag Update Request data contract used by the modeling domain.
 */
struct ModelingTransformDragUpdateRequest {
    QVec2 screenPosition = {};
    ModelingRay ray;
    ModelingViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool gridSnapEnabled = true;
    bool precision = false;
};

/**
 * Represents a Modeling Transform Drag Preview value used by the modeling domain.
 */
struct ModelingTransformDragPreview {
    bool ok = false;
    bool active = false;
    bool changed = false;
    bool previewDriven = false;
    std::string message;
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    ModelingTransformAxis axis = ModelingTransformAxis::None;
    QVec3 pivot = {};
    QVec3 moveDelta = {};
    double rotateAngleDegrees = 0.0;
    QVec3 scale = {1.0f, 1.0f, 1.0f};
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    bool sceneTransform = false;
    bool hasScenePreviewTransform = false;
    ModelingTransform3 scenePreviewTransform = {};
};

/**
 * Stores the Modeling Create Box Result data contract used by the modeling domain.
 */
struct ModelingCreateBoxResult {
    bool ok = false;
    bool changed = false;
    std::string message;
    ModelingObjectRecord object;
};

} // namespace quader
