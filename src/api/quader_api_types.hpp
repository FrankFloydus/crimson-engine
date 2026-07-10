////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "core/scene_environment_catalog.hpp"

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
inline constexpr QuaderElementId kInvalidQuaderElementId = -1;
using QuaderModelingObjectId = std::int32_t;
inline constexpr QuaderModelingObjectId kInvalidQuaderModelingObjectId = -1;
using QuaderEditorObjectId = std::uint64_t;
inline constexpr QuaderEditorObjectId kInvalidQuaderEditorObjectId = 0;

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
 * Enumerates Quader Light Type values used by the public Quader API surface.
 */
enum class QuaderLightType {
    Directional,
    Point,
    Spot,
};

/**
 * Represents a Quader Transform value used by the public Quader API surface.
 */
struct QuaderTransform {
    QVec3 position = {};
    QVec3 rotation_euler = {};
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
  std::uint64_t project_revision = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t modeling_content_revision = 0;
  std::uint64_t modeling_selection_revision = 0;
};

/**
 * Represents a Quader Entity Ref value used by the public Quader API surface.
 */
struct QuaderEntityRef {
  EntityId id = kInvalidEntityId;
  std::string uuid;
  QuaderEditorObjectId editor_object_id = kInvalidQuaderEditorObjectId;
};

/**
 * Represents the selected hierarchy duplicate result used by the public Quader API surface.
 */
struct QuaderDuplicateHierarchyResult {
  std::vector<QuaderEntityRef> root_entities;
};

/**
 * Represents a Quader Asset Ref value used by the public Quader API surface.
 */
struct QuaderAssetRef {
  AssetId id = kInvalidAssetId;
  QuaderAssetKind kind = QuaderAssetKind::Mesh;
};

/**
 * Represents a Quader Modeling Object Ref value used by the public Quader API surface.
 */
struct QuaderModelingObjectRef {
  EntityId entity = kInvalidEntityId;
  std::string entity_uuid;
  QuaderEditorObjectId editor_object_id = kInvalidQuaderEditorObjectId;
  QuaderModelingObjectId modeling_object_id = kInvalidQuaderModelingObjectId;
  AssetId mesh = kInvalidAssetId;
  AssetId material = kInvalidAssetId;
};

/**
 * Represents a Quader Component Ref value used by the public Quader API surface.
 */
struct QuaderComponentRef {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Object;
    QuaderElementId vertex = kInvalidQuaderElementId;
    QuaderElementId edge_a = kInvalidQuaderElementId;
    QuaderElementId edge_b = kInvalidQuaderElementId;
    QuaderElementId face = kInvalidQuaderElementId;
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
    Pivot,
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
  QuaderModelingObjectId modeling_object_id = kInvalidQuaderModelingObjectId;
  EntityId entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  std::string name;
};

/**
 * Represents a Quader Selection Summary value used by the public Quader API surface.
 */
struct QuaderSelectionSummary {
    QuaderSelectionMode mode = QuaderSelectionMode::Object;
    bool has_selection = false;
    QuaderActiveSelectionKind active_kind = QuaderActiveSelectionKind::None;
    QuaderElementId active_object_id = kInvalidQuaderElementId;
    QuaderElementId active_vertex_id = kInvalidQuaderElementId;
    QuaderElementId active_edge_a = kInvalidQuaderElementId;
    QuaderElementId active_edge_b = kInvalidQuaderElementId;
    QuaderElementId active_face_id = kInvalidQuaderElementId;
    std::uint32_t selected_object_count = 0;
    std::uint32_t selected_vertex_count = 0;
    std::uint32_t selected_edge_count = 0;
    std::uint32_t selected_face_count = 0;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
};

/**
 * Represents a Quader Tool Status value used by the public Quader API surface.
 */
struct QuaderToolStatus {
  QuaderViewportTool active_tool = QuaderViewportTool::Select;
  bool modal_tool_owns_viewport = false;
  bool insert_edge_loop_active = false;
  bool insert_edge_loop_dragging = false;
  bool insert_edge_loop_has_preview = false;
  bool knife_active = false;
  bool knife_has_current = false;
  bool knife_can_commit = false;
  bool cut_active = false;
  bool cut_can_preview = false;
  bool cut_preview_valid = false;
  bool cut_dragging = false;
  bool cut_dragging_new_point = false;
  bool cut_dragging_line = false;
  int cut_dragging_point_index = -1;
  int cut_point_count = 0;
  int cut_captured_point_count = 0;
  bool cut_has_line = false;
  std::array<QVec3, 3> cut_points = {};
  QuaderCutMode cut_mode = QuaderCutMode::KeepBoth;
  bool mirror_active = false;
  bool mirror_can_preview = false;
  bool mirror_preview_valid = false;
  bool mirror_dragging = false;
  bool mirror_dragging_new_point = false;
  bool mirror_dragging_line = false;
  int mirror_dragging_point_index = -1;
  int mirror_point_count = 0;
  int mirror_captured_point_count = 0;
  bool mirror_has_line = false;
  std::array<QVec3, 3> mirror_points = {};
  bool poly_active = false;
  bool poly_closed = false;
  bool poly_has_current = false;
  bool poly_can_commit = false;
  int poly_point_count = 0;
};

/**
 * Represents a Quader Interaction Settings value used by the public Quader API surface.
 */
struct QuaderInteractionSettings {
  int bridge_steps = 1;
  double edge_bevel_width = 1.0;
  double edge_bevel_profile = 0.5;
  int edge_bevel_segments = 1;
  double thicken_thickness = 0.25;
  bool thicken_from_center = false;
  int slice_quad_x_slices = 1;
  int slice_quad_y_slices = 1;
  double merge_by_distance_tolerance = 0.125;
  QuaderCutMode cut_mode = QuaderCutMode::KeepBoth;
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
    QVec3 x_axis = {1.0f, 0.0f, 0.0f};
    QVec3 y_axis = {0.0f, 1.0f, 0.0f};
    QVec3 z_axis = {0.0f, 0.0f, 1.0f};
    QVec2 viewport_size = {1.0f, 1.0f};
    double orthographic_size = 24.0;
    double fov_degrees = 60.0;
};

/**
 * Represents a Quader 3x4 Transform value used by the public Quader API surface.
 */
struct QuaderTransform3 {
  QVec3 x_axis = {1.0f, 0.0f, 0.0f};
  QVec3 y_axis = {0.0f, 1.0f, 0.0f};
  QVec3 z_axis = {0.0f, 0.0f, 1.0f};
  QVec3 origin = {};
};

/**
 * Stores a Quader Pick Select Request value used by the public Quader API surface.
 */
struct QuaderPickSelectRequest {
    QuaderRay ray;
    int click_count = 1;
    bool control_pressed = false;
    bool shift_pressed = false;
    bool command_pressed = false;
    bool alt_pressed = false;
    bool preserve_selection_on_miss = false;
    bool preserve_selection_on_selected_hit = false;
    bool preserve_existing_component_selection_on_hit = false;
    double vertex_radius = 0.3;
    double edge_radius = 0.2;
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
    bool selected_modifier_pressed = false;
    bool face_plane_hover = false;
    double vertex_radius = 0.3;
    double edge_radius = 0.2;
};

/**
 * Represents a Quader Hover Target value used by the public Quader API surface.
 */
struct QuaderHoverTarget {
    QuaderActiveSelectionKind kind = QuaderActiveSelectionKind::None;
    int object_id = -1;
    int vertex_id = -1;
    int edge_a = -1;
    int edge_b = -1;
    int face_id = -1;
};

/**
 * Represents a Quader Hover Summary value used by the public Quader API surface.
 */
struct QuaderHoverSummary {
    bool changed = false;
    QuaderHoverTarget target;
    bool target_selected = false;
};

/**
 * Stores a Quader Transform Drag Begin Request value used by the public Quader API surface.
 */
struct QuaderTransformDragBeginRequest {
    QuaderTransformTool tool = QuaderTransformTool::Move;
    QuaderTransformOperation operation = QuaderTransformOperation::Transform;
    QuaderTransformAxis axis = QuaderTransformAxis::None;
    QVec3 pivot = {};
    QVec2 screen_position = {};
    QVec2 screen_pivot = {};
    QVec2 screen_tip = {};
    bool has_world_direction = false;
    QVec3 world_direction = {};
    QuaderRay ray;
    QuaderViewportCamera camera;
    double grid_size = 1.0;
    double vertex_snap_radius = 0.3;
    bool grid_snap_enabled = true;
    bool precision = false;
    bool duplicate_selected = false;
};

/**
 * Stores a Quader Transform Drag Update Request value used by the public Quader API surface.
 */
struct QuaderTransformDragUpdateRequest {
  QVec2 screen_position = {};
  QuaderRay ray;
  QuaderViewportCamera camera;
  double grid_size = 1.0;
  double vertex_snap_radius = 0.3;
  bool grid_snap_enabled = true;
  bool precision = false;
};

/**
 * Represents a Quader Transform Drag Preview value used by the public Quader API surface.
 */
struct QuaderTransformDragPreview {
    bool ok = false;
    bool active = false;
    bool changed = false;
    bool preview_driven = false;
    std::string message;
    QuaderTransformTool tool = QuaderTransformTool::Move;
    QuaderTransformOperation operation = QuaderTransformOperation::Transform;
    QuaderTransformAxis axis = QuaderTransformAxis::None;
    QVec3 pivot = {};
    QVec3 move_delta = {};
    double rotate_angle_degrees = 0.0;
    QVec3 scale = {1.0f, 1.0f, 1.0f};
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    bool scene_transform = false;
    bool has_scene_preview_transform = false;
    QuaderTransform3 scene_preview_transform = {};
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
    bool modal_tool_owns_viewport = false;
    bool suppress_when_modal_tool_owns_viewport = true;
    bool include_disabled = true;
    bool include_hidden = false;
};

/**
 * Stores a Quader Action Item value used by the public Quader API surface.
 */
struct QuaderActionItem {
  std::string action_id;
  std::string operation_id;
  std::string tool_id;
  std::string label;
  QuaderActionKind kind = QuaderActionKind::Operation;
  QuaderActionPreviewKind preview_kind = QuaderActionPreviewKind::None;
  QuaderActionVisibility visibility = QuaderActionVisibility::Hidden;
  std::string disabled_reason;
  std::string disabled_reason_key;
  std::uint32_t disabled_reason_count = 0;
  bool rebuilds_mesh_on_success = true;
};

/**
 * Stores a Quader Action Panel value used by the public Quader API surface.
 */
struct QuaderActionPanel {
  std::string panel_id;
  std::string label;
  std::string selection_mode_id;
  QuaderActionVisibility visibility = QuaderActionVisibility::Hidden;
  std::vector<QuaderActionItem> actions;
};

/**
 * Stores a Quader Action Set value used by the public Quader API surface.
 */
struct QuaderActionSet {
    QuaderActionSurface surface = QuaderActionSurface::OperationsPanel;
    std::string surface_id;
    std::vector<QuaderActionPanel> panels;
};

/**
 * Stores a Quader Instant Operation Descriptor value used by the public Quader API surface.
 */
struct QuaderInstantOperationDescriptor {
    std::string id;
    std::string label;
    bool enabled = false;
    bool rebuilds_mesh_on_success = true;
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
    SourceWireDepthStamp = 23,
    WireframeXray = 24,
    PointLightGizmo = 25,
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
    FacePlaneHoverFace,
    FacePlaneHoverWire,
};

/**
 * Enumerates Quader Overlay Source values used by the public Quader API surface.
 */
enum class QuaderOverlaySource {
    Unknown,
    Selection,
    Hover,
    Gizmo,
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
  int object_id = -1;
  int vertex_id = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face_id = -1;
};

constexpr std::uint32_t quader_rgba8(std::uint8_t r, std::uint8_t g,
                                     std::uint8_t b, std::uint8_t a = 255u) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8u) |
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
    std::uint32_t color = quader_rgba8(255, 255, 255);
    float width_pixels = 1.0f;
    QuaderOverlayStyleRole style_role = QuaderOverlayStyleRole::LayerDefault;
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
    std::uint32_t color = quader_rgba8(255, 255, 255, 64);
    QuaderOverlayStyleRole style_role = QuaderOverlayStyleRole::LayerDefault;
    QuaderOverlaySource source = QuaderOverlaySource::Unknown;
    QuaderOverlayElementRef element;
};

/**
 * Represents a Quader Overlay Point value used by the public Quader API surface.
 */
struct QuaderOverlayPoint {
    QuaderOverlayLayer layer = QuaderOverlayLayer::UnselectedVertex;
    QVec3 position;
    std::uint32_t color = quader_rgba8(255, 255, 255);
    float size_pixels = 8.0f;
    QuaderOverlayStyleRole style_role = QuaderOverlayStyleRole::LayerDefault;
    QuaderOverlaySource source = QuaderOverlaySource::Unknown;
    QuaderOverlayElementRef element;
    std::optional<float> outline_growth_pixels;
};

/**
 * Stores the Quader Overlay Frame data contract used by the public Quader API surface.
 */
struct QuaderOverlayFrame {
    std::vector<QuaderOverlayLine> lines;
    std::vector<QuaderOverlayTriangle> triangles;
    std::vector<QuaderOverlayPoint> points;
    bool hover_target_selected = false;
};

/**
 * Stores the Quader Create Entity Args data contract used by the public Quader API surface.
 */
struct QuaderCreateEntityArgs {
    std::string name;
    QuaderEntityKind kind = QuaderEntityKind::Generic;
    EntityId parent = kInvalidEntityId;
    QuaderTransform transform;
};

/**
 * Represents a Quader Light Component value used by the public Quader API surface.
 */
struct QuaderLightComponent {
    QuaderLightType type = QuaderLightType::Point;
    QVec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    bool casts_shadows = false;
};

/**
 * Returns the default public point light component used by point-light creation.
 */
[[nodiscard]] inline QuaderLightComponent
default_quader_point_light_component() {
  QuaderLightComponent light;
  light.type = QuaderLightType::Point;
  return light;
}

/**
 * Returns the default public directional light component used by directional-light creation.
 */
[[nodiscard]] inline QuaderLightComponent
default_quader_directional_light_component() {
  QuaderLightComponent light;
  light.type = QuaderLightType::Directional;
  return light;
}

/**
 * Stores public scene environment settings.
 */
struct QuaderSceneEnvironmentSettings {
  std::string environment_id = std::string(kDefaultSceneEnvironmentId);
  bool enabled = true;
  bool show_background = true;
  QVec3 clear_color = {kDefaultSceneClearColorChannel,
                       kDefaultSceneClearColorChannel,
                       kDefaultSceneClearColorChannel};
  float intensity = 1.0f;
  float background_intensity = 1.0f;
  float rotation_y = 0.0f;
};

/**
 * Stores public scene camera exposure settings.
 */
struct QuaderSceneCameraSettings {
    float aperture = 16.0f;
    float shutter_speed = 1.0f / 125.0f;
    float sensitivity = 100.0f;
    float near_plane = 0.05f;
    float far_plane = 1000.0f;
};

/**
 * Selects the public rendered viewport tone mapping operator.
 */
enum class QuaderToneMapping {
    Linear,
    AcesLegacy,
    Aces,
    Filmic,
    DisplayRange,
};

/**
 * Stores public scene post-process settings.
 */
struct QuaderScenePostProcessSettings {
    bool enabled = true;
    bool anti_aliasing = true;
    bool ambient_occlusion = false;
    float ambient_occlusion_intensity = 1.0f;
    float ambient_occlusion_radius = 0.3f;
    bool bloom = false;
    float bloom_strength = 0.1f;
    bool dithering = true;
    QuaderToneMapping tone_mapping = QuaderToneMapping::Aces;
};

/**
 * Stores public scene root settings.
 */
struct QuaderSceneSettings {
    QuaderSceneEnvironmentSettings environment;
    QuaderSceneCameraSettings camera;
    QuaderScenePostProcessSettings post_process;
};

/**
 * Stores the Quader Create Point Light Args data contract used by the public Quader API surface.
 */
struct QuaderCreatePointLightArgs {
    std::string name = "Point Light";
    EntityId parent = kInvalidEntityId;
    QuaderTransform transform;
    QuaderLightComponent light = default_quader_point_light_component();
    bool select_created = false;
};

/**
 * Stores the Quader Create Directional Light Args data contract used by the public Quader API surface.
 */
struct QuaderCreateDirectionalLightArgs {
    std::string name = "Directional Light";
    EntityId parent = kInvalidEntityId;
    QuaderTransform transform;
    QuaderLightComponent light = default_quader_directional_light_component();
    bool select_created = false;
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
    std::uint32_t material_slot = 0;
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
    QVec3 base_color = {1.0f, 1.0f, 1.0f};
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
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  std::uint32_t material_slot = 0;
};

/**
 * Represents a Quader Mesh Asset value used by the public Quader API surface.
 */
struct QuaderMeshAsset {
  AssetId id = kInvalidAssetId;
  std::uint64_t revision = 0;
  std::string name;
  std::vector<QuaderMeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<QuaderMeshPrimitiveSpan> primitives;
  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;
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
  AssetId id = kInvalidAssetId;
  std::uint64_t revision = 0;
  std::string name;
  QuaderTextureUsage usage = QuaderTextureUsage::Unknown;
  QuaderTextureColorSpace color_space = QuaderTextureColorSpace::Srgb;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string source_path;
  bool generated = false;
};

/**
 * Represents a Quader Material Asset value used by the public Quader API surface.
 */
struct QuaderMaterialAsset {
  AssetId id = kInvalidAssetId;
  std::uint64_t revision = 0;
  std::string name;
  QVec3 base_color = {1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  std::unordered_map<std::string, AssetId> texture_slots;
  bool node_graph_reserved = true;
};

/**
 * Enumerates Quader Mesh Collision Type values used by the public Quader API surface.
 */
enum class QuaderMeshCollisionType {
    Box,
    Sphere,
    Cylinder,
    ConvexHull,
    Mesh,
};

/**
 * Enumerates Quader Mesh Physics Body Type values used by the public Quader API surface.
 */
enum class QuaderMeshPhysicsBodyType {
    StaticBody,
    RigidBody,
};

/**
 * Represents a Quader Mesh Component value used by the public Quader API surface.
 */
struct QuaderMeshComponent {
  AssetId mesh = kInvalidAssetId;
  bool editor_only = false;
  bool cast_shadows = true;
  bool double_sided = false;
  bool generate_collisions = true;
  QuaderMeshCollisionType collision_type = QuaderMeshCollisionType::Box;
  bool display_collision = false;
  QuaderMeshPhysicsBodyType body_type = QuaderMeshPhysicsBodyType::StaticBody;
};

/**
 * Represents a Quader Tool Preview Mesh value used by the public Quader API surface.
 */
struct QuaderToolPreviewMesh {
  EntityId source_entity = kInvalidEntityId;
  AssetId material = kInvalidAssetId;
  QuaderMeshAsset mesh;
};

} // namespace quader
