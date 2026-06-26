////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "math/Types.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

using QuaderElementId = std::int32_t;
inline constexpr QuaderElementId InvalidQuaderElementId = -1;
using QuaderModelingObjectId = std::int32_t;
inline constexpr QuaderModelingObjectId InvalidQuaderModelingObjectId = -1;
using QuaderEditorObjectId = std::uint64_t;
inline constexpr QuaderEditorObjectId InvalidQuaderEditorObjectId = 0;

struct QuaderPolygonDocumentStorage;

/**
 * Owns an opaque polygon document value for the public Quader API surface.
 */
class QuaderPolygonDocument final {
public:
    QuaderPolygonDocument();
    ~QuaderPolygonDocument();

    QuaderPolygonDocument(const QuaderPolygonDocument&);
    QuaderPolygonDocument& operator=(const QuaderPolygonDocument&);
    QuaderPolygonDocument(QuaderPolygonDocument&&) noexcept;
    QuaderPolygonDocument& operator=(QuaderPolygonDocument&&) noexcept;

private:
    friend class QuaderPolygonDocumentInternalAccess;

    explicit QuaderPolygonDocument(std::shared_ptr<QuaderPolygonDocumentStorage> storage);

    std::shared_ptr<QuaderPolygonDocumentStorage> storage_;
};

/**
 * Enumerates Quader Entity Kind values used by the public Quader API surface.
 */
enum class QuaderEntityKind {
    Empty,
    Mesh,
    Light,
    Generic,
};

/**
 * Represents a Quader Transform value used by the public Quader API surface.
 */
struct QuaderTransform {
    QVec3 position = {};
    QVec3 rotationEuler = {};
    QVec3 scale = {1.0f, 1.0f, 1.0f};
};

/**
 * Enumerates QuaderComponentKind values used by the api layer.
 */
enum class QuaderComponentKind {
    Object,
    Vertex,
    Edge,
    Face,
};

/**
 * Enumerates QuaderActiveSelectionKind values used by the api layer.
 */
enum class QuaderActiveSelectionKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Light,
};

/**
 * Enumerates QuaderAssetKind values used by the api layer.
 */
enum class QuaderAssetKind {
    Mesh,
    Material,
    Texture,
};

/**
 * Enumerates QuaderSelectionEdit values used by the api layer.
 */
enum class QuaderSelectionEdit {
    Replace,
    Add,
    Remove,
    Toggle,
};

/**
 * Enumerates QuaderBoxSelectionRule values used by viewport box-selection requests.
 */
enum class QuaderBoxSelectionRule {
    DragDirection,
    Contain,
    Crossing,
};

/**
 * Enumerates QuaderMutationKind values used by the api layer.
 */
enum class QuaderMutationKind {
    None,
    Document,
    Modeling,
    Asset,
    Runtime,
};

/**
 * Represents a Quader Revision Stamp value used by the public Quader API surface.
 */
struct QuaderRevisionStamp {
    std::uint64_t projectRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t modelingContentRevision = 0;
    std::uint64_t modelingSelectionRevision = 0;
};

/**
 * Represents a Quader Entity Ref value used by the public Quader API surface.
 */
struct QuaderEntityRef {
    EntityId id = InvalidEntityId;
    QuaderEditorObjectId editorObjectId = InvalidQuaderEditorObjectId;
};

/**
 * Represents the selected hierarchy duplicate result used by the public Quader API surface.
 */
struct QuaderDuplicateHierarchyResult {
    std::vector<QuaderEntityRef> rootEntities;
};

/**
 * Represents a Quader Asset Ref value used by the public Quader API surface.
 */
struct QuaderAssetRef {
    AssetId id = InvalidAssetId;
    QuaderAssetKind kind = QuaderAssetKind::Mesh;
};

/**
 * Represents a Quader Modeling Object Ref value used by the public Quader API surface.
 */
struct QuaderModelingObjectRef {
    EntityId entity = InvalidEntityId;
    QuaderEditorObjectId editorObjectId = InvalidQuaderEditorObjectId;
    QuaderModelingObjectId modelingObjectId = InvalidQuaderModelingObjectId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
};

/**
 * Represents a Quader Component Ref value used by the public Quader API surface.
 */
struct QuaderComponentRef {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Object;
    QuaderElementId vertex = InvalidQuaderElementId;
    QuaderElementId edgeA = InvalidQuaderElementId;
    QuaderElementId edgeB = InvalidQuaderElementId;
    QuaderElementId face = InvalidQuaderElementId;
};

/**
 * Represents a Quader Bounds value used by the public Quader API surface.
 */
struct QuaderBounds {
    QVec3 min;
    QVec3 max;
};

/**
 * Enumerates Quader Selection Mode values used by the public Quader API surface.
 */
enum class QuaderSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
    Group,
};

/**
 * Enumerates Quader Transform Tool values used by the public Quader API surface.
 */
enum class QuaderTransformTool {
    Move,
    Rotate,
    Scale,
};

/**
 * Enumerates Quader Transform Operation values used by the public Quader API surface.
 */
enum class QuaderTransformOperation {
    Transform,
    Extrude,
    Inset,
};

/**
 * Enumerates Quader Transform Axis values used by the public Quader API surface.
 */
enum class QuaderTransformAxis {
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
 * Enumerates Quader Viewport Tool values used by the public Quader API surface.
 */
enum class QuaderViewportTool {
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
 * Enumerates Quader Cut Mode values used by the public Quader API surface.
 */
enum class QuaderCutMode {
    DiscardLeft,
    DiscardRight,
    KeepBoth,
};

/**
 * Stores the Quader Modeling Object Record data contract used by the public Quader API surface.
 */
struct QuaderModelingObjectRecord {
    QuaderModelingObjectId modelingObjectId = InvalidQuaderModelingObjectId;
    EntityId entity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    std::string name;
};

/**
 * Represents a Quader Selection Summary value used by the public Quader API surface.
 */
struct QuaderSelectionSummary {
    QuaderSelectionMode mode = QuaderSelectionMode::Object;
    bool hasSelection = false;
    QuaderActiveSelectionKind activeKind = QuaderActiveSelectionKind::None;
    QuaderElementId activeObjectId = InvalidQuaderElementId;
    QuaderElementId activeVertexId = InvalidQuaderElementId;
    QuaderElementId activeEdgeA = InvalidQuaderElementId;
    QuaderElementId activeEdgeB = InvalidQuaderElementId;
    QuaderElementId activeFaceId = InvalidQuaderElementId;
    std::uint32_t selectedObjectCount = 0;
    std::uint32_t selectedVertexCount = 0;
    std::uint32_t selectedEdgeCount = 0;
    std::uint32_t selectedFaceCount = 0;
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
};

/**
 * Represents a Quader Tool Status value used by the public Quader API surface.
 */
struct QuaderToolStatus {
    QuaderViewportTool activeTool = QuaderViewportTool::Select;
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
    QuaderCutMode cutMode = QuaderCutMode::KeepBoth;
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
 * Represents a Quader Interaction Settings value used by the public Quader API surface.
 */
struct QuaderInteractionSettings {
    int bridgeSteps = 1;
    double edgeBevelWidth = 1.0;
    double edgeBevelProfile = 0.5;
    int edgeBevelSegments = 1;
    double thickenThickness = 0.25;
    bool thickenFromCenter = false;
    int sliceQuadXSlices = 1;
    int sliceQuadYSlices = 1;
    double mergeByDistanceTolerance = 0.125;
    QuaderCutMode cutMode = QuaderCutMode::KeepBoth;
};

/**
 * Represents a Quader Ray value used by the public Quader API surface.
 */
struct QuaderRay {
    QVec3 origin;
    QVec3 direction;
};

/**
 * Represents a Quader Viewport Camera value used by the public Quader API surface.
 */
struct QuaderViewportCamera {
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
 * Represents a Quader 3x4 Transform value used by the public Quader API surface.
 */
struct QuaderTransform3 {
    QVec3 xAxis = {1.0f, 0.0f, 0.0f};
    QVec3 yAxis = {0.0f, 1.0f, 0.0f};
    QVec3 zAxis = {0.0f, 0.0f, 1.0f};
    QVec3 origin = {};
};

/**
 * Stores a Quader Pick Select Request value used by the public Quader API surface.
 */
struct QuaderPickSelectRequest {
    QuaderRay ray;
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

/**
 * Stores a Quader Box Select Request value used by the public Quader API surface.
 */
struct QuaderBoxSelectRequest {
    QuaderViewportCamera camera;
    QVec2 start = {};
    QVec2 end = {};
    QuaderSelectionEdit edit = QuaderSelectionEdit::Replace;
    QuaderBoxSelectionRule rule = QuaderBoxSelectionRule::DragDirection;
};

/**
 * Stores a Quader Hover Request value used by the public Quader API surface.
 */
struct QuaderHoverRequest {
    QuaderRay ray;
    bool selectedModifierPressed = false;
    double vertexRadius = 0.3;
    double edgeRadius = 0.2;
};

/**
 * Represents a Quader Hover Target value used by the public Quader API surface.
 */
struct QuaderHoverTarget {
    QuaderActiveSelectionKind kind = QuaderActiveSelectionKind::None;
    int objectId = -1;
    int vertexId = -1;
    int edgeA = -1;
    int edgeB = -1;
    int faceId = -1;
};

/**
 * Represents a Quader Hover Summary value used by the public Quader API surface.
 */
struct QuaderHoverSummary {
    bool changed = false;
    QuaderHoverTarget target;
    bool targetSelected = false;
};

/**
 * Stores a Quader Transform Drag Begin Request value used by the public Quader API surface.
 */
struct QuaderTransformDragBeginRequest {
    QuaderTransformTool tool = QuaderTransformTool::Move;
    QuaderTransformOperation operation = QuaderTransformOperation::Transform;
    QuaderTransformAxis axis = QuaderTransformAxis::None;
    QVec3 pivot = {};
    QVec2 screenPosition = {};
    QVec2 screenPivot = {};
    QVec2 screenTip = {};
    bool hasWorldDirection = false;
    QVec3 worldDirection = {};
    QuaderRay ray;
    QuaderViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool duplicateSelected = false;
};

/**
 * Stores a Quader Transform Drag Update Request value used by the public Quader API surface.
 */
struct QuaderTransformDragUpdateRequest {
    QVec2 screenPosition = {};
    QuaderRay ray;
    QuaderViewportCamera camera;
    double gridSize = 1.0;
    double vertexSnapRadius = 0.3;
    bool precision = false;
};

/**
 * Represents a Quader Transform Drag Preview value used by the public Quader API surface.
 */
struct QuaderTransformDragPreview {
    bool ok = false;
    bool active = false;
    bool changed = false;
    bool previewDriven = false;
    std::string message;
    QuaderTransformTool tool = QuaderTransformTool::Move;
    QuaderTransformOperation operation = QuaderTransformOperation::Transform;
    QuaderTransformAxis axis = QuaderTransformAxis::None;
    QVec3 pivot = {};
    QVec3 moveDelta = {};
    double rotateAngleDegrees = 0.0;
    QVec3 scale = {1.0f, 1.0f, 1.0f};
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    bool sceneTransform = false;
    bool hasScenePreviewTransform = false;
    QuaderTransform3 scenePreviewTransform = {};
};

/**
 * Enumerates Quader Action Surface values used by the public Quader API surface.
 */
enum class QuaderActionSurface {
    ViewportContextMenu,
    OperationsPanel,
};

/**
 * Enumerates Quader Action Visibility values used by the public Quader API surface.
 */
enum class QuaderActionVisibility {
    Hidden,
    Disabled,
    Enabled,
};

/**
 * Enumerates Quader Action Kind values used by the public Quader API surface.
 */
enum class QuaderActionKind {
    Operation,
    Tool,
};

/**
 * Enumerates Quader Action Preview Kind values used by the public Quader API surface.
 */
enum class QuaderActionPreviewKind {
    None,
    Bridge,
    EdgeBevel,
    Thicken,
    SliceQuad,
    MergeByDistance,
};

/**
 * Stores a Quader Action Query value used by the public Quader API surface.
 */
struct QuaderActionQuery {
    QuaderActionSurface surface = QuaderActionSurface::OperationsPanel;
    bool modalToolOwnsViewport = false;
    bool suppressWhenModalToolOwnsViewport = true;
    bool includeDisabled = true;
    bool includeHidden = false;
};

/**
 * Stores a Quader Action Item value used by the public Quader API surface.
 */
struct QuaderActionItem {
    std::string actionId;
    std::string operationId;
    std::string toolId;
    std::string label;
    QuaderActionKind kind = QuaderActionKind::Operation;
    QuaderActionPreviewKind previewKind = QuaderActionPreviewKind::None;
    QuaderActionVisibility visibility = QuaderActionVisibility::Hidden;
    std::string disabledReason;
    std::string disabledReasonKey;
    std::uint32_t disabledReasonCount = 0;
    bool rebuildsMeshOnSuccess = true;
};

/**
 * Stores a Quader Action Panel value used by the public Quader API surface.
 */
struct QuaderActionPanel {
    std::string panelId;
    std::string label;
    std::string selectionModeId;
    QuaderActionVisibility visibility = QuaderActionVisibility::Hidden;
    std::vector<QuaderActionItem> actions;
};

/**
 * Stores a Quader Action Set value used by the public Quader API surface.
 */
struct QuaderActionSet {
    QuaderActionSurface surface = QuaderActionSurface::OperationsPanel;
    std::string surfaceId;
    std::vector<QuaderActionPanel> panels;
};

/**
 * Stores a Quader Instant Operation Descriptor value used by the public Quader API surface.
 */
struct QuaderInstantOperationDescriptor {
    std::string id;
    std::string label;
    bool enabled = false;
    bool rebuildsMeshOnSuccess = true;
};

/**
 * Enumerates Quader Overlay Layer values used by the public Quader API surface.
 */
enum class QuaderOverlayLayer {
    SelectedVisibleFace = 0,
    SelectedFaceEdge = 1,
    UnselectedVertex = 2,
    SelectedVertex = 3,
    HoverFace = 4,
    HoverFaceEdge = 5,
    HoverEdge = 6,
    HoverVertex = 7,
    SelectedEdge = 8,
    SelectedXrayFace = 9,
    SelectedFaceMask = 10,
    SourceWire = 11,
    OpenEdge = 12,
    SnapTargetVertex = 13,
    DiagnosticEdge = 14,
    ToolPlane = 15,
    ToolPreviewFill = 16,
    ToolPlaneLine = 17,
    ToolPreviewLine = 18,
    ToolPreviewSeam = 19,
    ToolHandle = 20,
    GroupBounds = 21,
    WireframeBase = 22,
};

/**
 * Enumerates Quader Overlay Style Role values used by the public Quader API surface.
 */
enum class QuaderOverlayStyleRole {
    LayerDefault,
    ExplicitColor,
    SelectionFace,
    SelectionWire,
    SelectedSourceWire,
    SourceWire,
    OpenEdge,
    DiagnosticEdge,
    HoverFace,
    HoverWire,
    RemoveSelectionFace,
    RemoveSelectionWire,
    Vertex,
    SelectedVertex,
    HoverVertex,
    RemoveSelectionVertex,
    SnapTargetVertex,
    GroupBounds,
    WireframeBase,
    ToolPlaneFill,
    ToolPlaneLine,
    ToolHandle,
    ToolPreviewFill,
    ToolPreviewLine,
    ToolPreviewSeam,
    CutPreviewKeepFill,
    CutPreviewKeepBothDiscardFill,
    CutPreviewDiscardFill,
    CutPreviewKeepBothOutline,
    CutPreviewDiscardOutline,
    CutPreviewSeam,
};

/**
 * Enumerates Quader Overlay Source values used by the public Quader API surface.
 */
enum class QuaderOverlaySource {
    Unknown,
    Selection,
    Hover,
    SceneWireframe,
    HierarchySelection,
    GroupBounds,
    Tool,
    Diagnostic,
    External,
};

/**
 * Represents a Quader Overlay Element Ref value used by the public Quader API surface.
 */
struct QuaderOverlayElementRef {
    int objectId = -1;
    int vertexId = -1;
    int edgeA = -1;
    int edgeB = -1;
    int faceId = -1;
};

constexpr std::uint32_t quaderRgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255u) {
    return static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8u) |
            (static_cast<std::uint32_t>(b) << 16u) |
            (static_cast<std::uint32_t>(a) << 24u);
}

/**
 * Represents a Quader Overlay Line value used by the public Quader API surface.
 */
struct QuaderOverlayLine {
    QuaderOverlayLayer layer = QuaderOverlayLayer::SourceWire;
    QVec3 start;
    QVec3 end;
    std::uint32_t color = quaderRgba8(255, 255, 255);
    float widthPixels = 1.0f;
    QuaderOverlayStyleRole styleRole = QuaderOverlayStyleRole::LayerDefault;
    QuaderOverlaySource source = QuaderOverlaySource::Unknown;
    QuaderOverlayElementRef element;
};

/**
 * Represents a Quader Overlay Triangle value used by the public Quader API surface.
 */
struct QuaderOverlayTriangle {
    QuaderOverlayLayer layer = QuaderOverlayLayer::SelectedVisibleFace;
    QVec3 a;
    QVec3 b;
    QVec3 c;
    std::uint32_t color = quaderRgba8(255, 255, 255, 64);
    QuaderOverlayStyleRole styleRole = QuaderOverlayStyleRole::LayerDefault;
    QuaderOverlaySource source = QuaderOverlaySource::Unknown;
    QuaderOverlayElementRef element;
};

/**
 * Represents a Quader Overlay Point value used by the public Quader API surface.
 */
struct QuaderOverlayPoint {
    QuaderOverlayLayer layer = QuaderOverlayLayer::UnselectedVertex;
    QVec3 position;
    std::uint32_t color = quaderRgba8(255, 255, 255);
    float sizePixels = 8.0f;
    QuaderOverlayStyleRole styleRole = QuaderOverlayStyleRole::LayerDefault;
    QuaderOverlaySource source = QuaderOverlaySource::Unknown;
    QuaderOverlayElementRef element;
    std::optional<float> outlineGrowthPixels;
};

/**
 * Stores the Quader Overlay Frame data contract used by the public Quader API surface.
 */
struct QuaderOverlayFrame {
    std::vector<QuaderOverlayLine> lines;
    std::vector<QuaderOverlayTriangle> triangles;
    std::vector<QuaderOverlayPoint> points;
    bool hoverTargetSelected = false;
};

/**
 * Stores the Quader Create Entity Args data contract used by the public Quader API surface.
 */
struct QuaderCreateEntityArgs {
    std::string name;
    QuaderEntityKind kind = QuaderEntityKind::Generic;
    EntityId parent = InvalidEntityId;
    QuaderTransform transform;
};

/**
 * Stores the Quader Create Box Args data contract used by the public Quader API surface.
 */
struct QuaderCreateBoxArgs {
    QuaderBounds bounds;
    std::string name = "Box";
};

/**
 * Stores the Quader Create Mesh Args data contract used by the public Quader API surface.
 */
struct QuaderCreateMeshArgs {
    std::string name = "Mesh";
    std::uint32_t materialSlot = 0;
};

/**
 * Stores the Quader Component Selection Args data contract used by the public Quader API surface.
 */
struct QuaderComponentSelectionArgs {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Vertex;
    std::vector<QuaderElementId> vertices;
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges;
    std::vector<QuaderElementId> faces;
    QuaderComponentRef active;
    QuaderSelectionEdit edit = QuaderSelectionEdit::Replace;
};

/**
 * Represents a Quader Preview Settings value used by the public Quader API surface.
 */
struct QuaderPreviewSettings {
    QuaderInteractionSettings interaction;
};

/**
 * Stores the Quader Material Args data contract used by the public Quader API surface.
 */
struct QuaderMaterialArgs {
    std::string name = "Material";
    QVec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
};

/**
 * Represents a Quader Mesh Vertex value used by the public Quader API surface.
 */
struct QuaderMeshVertex {
    QVec3 position;
    QVec3 normal;
    QVec2 uv;
    std::uint32_t rgba = 0xffffffffu;
};

/**
 * Represents a Quader Mesh Primitive Span value used by the public Quader API surface.
 */
struct QuaderMeshPrimitiveSpan {
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
};

/**
 * Represents a Quader Mesh Asset value used by the public Quader API surface.
 */
struct QuaderMeshAsset {
    AssetId id = InvalidAssetId;
    std::uint64_t revision = 0;
    std::string name;
    std::vector<QuaderMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<QuaderMeshPrimitiveSpan> primitives;
    bool hasStableCpuTopology = true;
    bool hasAuthoredPolygonDocument = false;
};

/**
 * Enumerates Quader Texture Usage values used by the public Quader API surface.
 */
enum class QuaderTextureUsage {
    Unknown,
    BaseColor,
    Normal,
    Roughness,
    Metallic,
    Curvature,
    Thickness,
    AmbientOcclusion,
    Position,
    Id,
    Mask,
};

/**
 * Enumerates Quader Texture Color Space values used by the public Quader API surface.
 */
enum class QuaderTextureColorSpace {
    Linear,
    Srgb,
};

/**
 * Represents a Quader Texture Asset value used by the public Quader API surface.
 */
struct QuaderTextureAsset {
    AssetId id = InvalidAssetId;
    std::uint64_t revision = 0;
    std::string name;
    QuaderTextureUsage usage = QuaderTextureUsage::Unknown;
    QuaderTextureColorSpace colorSpace = QuaderTextureColorSpace::Srgb;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string sourcePath;
    bool generated = false;
};

/**
 * Represents a Quader Material Asset value used by the public Quader API surface.
 */
struct QuaderMaterialAsset {
    AssetId id = InvalidAssetId;
    std::uint64_t revision = 0;
    std::string name;
    QVec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    std::unordered_map<std::string, AssetId> textureSlots;
    bool nodeGraphReserved = true;
};

/**
 * Represents a Quader Mesh Component value used by the public Quader API surface.
 */
struct QuaderMeshComponent {
    AssetId mesh = InvalidAssetId;
};

/**
 * Represents a Quader Tool Preview Mesh value used by the public Quader API surface.
 */
struct QuaderToolPreviewMesh {
    EntityId sourceEntity = InvalidEntityId;
    AssetId material = InvalidAssetId;
    QuaderMeshAsset mesh;
};

} // namespace quader
