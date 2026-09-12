////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/command_presentation_catalog.hpp"

#include <algorithm>
#include <iterator>

namespace quader {
namespace {

using Domain = CommandPresentationDomain;
using Group = CommandPresentationGroup;
using Activation = CommandPresentationActivation;
using Danger = CommandPresentationDanger;
using Palette = CommandPalettePolicy;
using Surface = CommandPresentationSurface;

constexpr Surface kMenuPalette = Surface::MainMenu | Surface::CommandPalette;
constexpr Surface kMenuViewportPalette =
    kMenuPalette | Surface::ViewportContextMenu;
constexpr Surface kMenuHierarchyPalette =
    kMenuPalette | Surface::HierarchyContextMenu;
constexpr Surface kMenuLayersPalette =
    kMenuPalette | Surface::LayersContextMenu;
constexpr Surface kMenuViewportHierarchyPalette =
    kMenuViewportPalette | Surface::HierarchyContextMenu;
constexpr Surface kMenuViewportHierarchyLayersPalette =
    kMenuViewportHierarchyPalette | Surface::LayersContextMenu;
constexpr Surface kViewportPalette =
    Surface::ViewportContextMenu | Surface::CommandPalette;
constexpr Surface kHierarchyPalette =
    Surface::HierarchyContextMenu | Surface::CommandPalette;
constexpr Surface kPalette = Surface::CommandPalette;
constexpr Surface kToolbarPalette =
    Surface::Toolbar | Surface::CommandPalette;
constexpr Surface kMenuToolbarPalette =
    Surface::MainMenu | Surface::Toolbar | Surface::CommandPalette;
constexpr Surface kMenuViewportToolbarPalette =
    kMenuViewportPalette | Surface::Toolbar;

constexpr std::string_view kUndoAliases[]{"step backward"};
constexpr std::string_view kRedoAliases[]{"step forward"};
constexpr std::string_view kRepeatAliases[]{"repeat last", "redo last action"};
constexpr std::string_view kClearSelectionAliases[]{"deselect all", "select none"};
constexpr std::string_view kDuplicateAliases[]{"clone"};
constexpr std::string_view kInstanceAliases[]{"linked duplicate", "reference"};
constexpr std::string_view kFlattenInstanceAliases[]{"make unique", "unlink instance"};
constexpr std::string_view kRemoveDoublesAliases[]{"weld duplicate vertices"};
constexpr std::string_view kMergeDistanceAliases[]{"weld nearby", "remove doubles"};
constexpr std::string_view kOuterHullAliases[]{"solidify"};
constexpr std::string_view kFrameSelectionAliases[]{
    "focus selection", "center selection", "zoom selected"};
constexpr std::string_view kFocusAllAliases[]{"frame all", "show all"};
constexpr std::string_view kFocusOriginAliases[]{"frame origin", "center origin"};
constexpr std::string_view kPerspectiveAliases[]{"perspective camera"};
constexpr std::string_view kWireframeAliases[]{"wire frame"};
constexpr std::string_view kSearchAliases[]{
    "command palette", "commander", "find action"};

constexpr std::string_view kFileKeywords[]{"file", "document"};
constexpr std::string_view kExportKeywords[]{"export", "wavefront", "obj"};
constexpr std::string_view kSelectionKeywords[]{"selection", "select"};
constexpr std::string_view kNormalKeywords[]{"normal", "orientation"};
constexpr std::string_view kShadingKeywords[]{"shade", "surface"};
constexpr std::string_view kTextureKeywords[]{"texture", "uv"};
constexpr std::string_view kSearchKeywords[]{"search", "command", "palette", "finder"};
constexpr std::string_view kViewportInfoKeywords[]{
    "info", "stats", "statistics", "performance"};
constexpr std::string_view kNavigationGizmoAliases[]{"view cube"};
constexpr std::string_view kNavigationGizmoKeywords[]{
    "navigation gizmo", "orientation"};

constexpr CommandPresentationDescriptor presentation(
    QuaderCommandId command_id, Domain domain, Group group,
    std::uint32_t rank, Surface surfaces,
    Palette palette_policy = Palette::Searchable,
    Activation activation = Activation::Immediate,
    Danger danger = Danger::None,
    std::span<const std::string_view> aliases = {},
    std::span<const std::string_view> keywords = {},
    std::string_view settings_route = {},
    std::string_view help_route = {}) {
  return {
      .command_id = command_id,
      .domain = domain,
      .group = group,
      .rank = rank,
      .surfaces = surfaces,
      .palette_policy = palette_policy,
      .activation = activation,
      .danger = danger,
      .aliases = aliases,
      .keywords = keywords,
      .settings_route = settings_route,
      .help_route = help_route,
  };
}

constexpr CommandPresentationDescriptor kCatalog[]{
    // File: project and scene-document lifecycle.
    presentation(quader_command::kProjectNew, Domain::File, Group::Project,
                 10u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::Caution, {}, kFileKeywords),
    presentation(quader_command::kProjectOpen, Domain::File, Group::Project,
                 20u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::Caution, {}, kFileKeywords),
    presentation(quader_command::kProjectClose, Domain::File, Group::Project,
                 30u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::Caution, {}, kFileKeywords),
    presentation(quader_command::kProjectExportSelectionObj, Domain::File,
                 Group::Project, 40u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {}, kExportKeywords),
    presentation(quader_command::kSceneDocumentNew, Domain::File,
                 Group::SceneDocument, 100u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate,
                 Danger::Caution, {}, kFileKeywords),
    presentation(quader_command::kSceneDocumentOpen, Domain::File,
                 Group::SceneDocument, 110u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate,
                 Danger::Caution, {}, kFileKeywords),
    presentation(quader_command::kSceneDocumentSave, Domain::File,
                 Group::SceneDocument, 120u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kFileKeywords),
    presentation(quader_command::kSceneDocumentSaveAs, Domain::File,
                 Group::SceneDocument, 130u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kFileKeywords),
    presentation(quader_command::kSceneDocumentSaveAll, Domain::File,
                 Group::SceneDocument, 140u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kFileKeywords),
    presentation(quader_command::kSceneDocumentNext, Domain::File,
                 Group::SceneDocument, 150u, kMenuPalette),
    presentation(quader_command::kSceneDocumentPrevious, Domain::File,
                 Group::SceneDocument, 160u, kMenuPalette),
    presentation(quader_command::kSceneDocumentClose, Domain::File,
                 Group::SceneDocument, 170u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate,
                 Danger::Caution, {}, kFileKeywords),

    // Edit: history and clipboard remain stable, selection/object intent is
    // projected in its own domain below.
    presentation(quader_command::kEditUndo, Domain::Edit, Group::History, 10u,
                 kMenuPalette, Palette::Searchable, Activation::Immediate,
                 Danger::None, kUndoAliases),
    presentation(quader_command::kEditRedo, Domain::Edit, Group::History, 20u,
                 kMenuPalette, Palette::Searchable, Activation::Immediate,
                 Danger::None, kRedoAliases),
    presentation(quader_command::kEditRepeatLastAction, Domain::Edit,
                 Group::History, 30u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, kRepeatAliases),
    presentation(quader_command::kCutSelection, Domain::Edit,
                 Group::Clipboard, 100u, kMenuViewportPalette),
    presentation(quader_command::kCopySelection, Domain::Edit,
                 Group::Clipboard, 110u, kMenuViewportPalette),
    presentation(quader_command::kPasteSelection, Domain::Edit,
                 Group::Clipboard, 120u, kMenuViewportPalette),

    // Select: one stable menu, with the command owner deciding availability.
    presentation(quader_command::kToolsActivateSelect, Domain::Select,
                 Group::Tools, 10u, kToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool),
    presentation(quader_command::kToolsActivateLassoSelect, Domain::Select,
                 Group::Tools, 20u, kToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool),
    presentation(quader_command::kEditClearSelection, Domain::Select,
                 Group::Selection, 100u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kClearSelectionAliases, kSelectionKeywords),
    presentation(quader_command::kSelectAll, Domain::Select, Group::Selection,
                 110u, kMenuViewportPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {},
                 kSelectionKeywords),
    presentation(quader_command::kSelectInverse, Domain::Select,
                 Group::Selection, 120u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectToggleBackfaceSelection,
                 Domain::Select, Group::Selection, 130u, kMenuPalette,
                 Palette::Searchable, Activation::Toggle, Danger::None, {},
                 kSelectionKeywords),
    presentation(quader_command::kSelectEdgeDirection, Domain::Select,
                 Group::Selection, 200u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectEdgeLoops, Domain::Select,
                 Group::Selection, 210u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectEdgeRing, Domain::Select,
                 Group::Selection, 220u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectFacesWithSameMaterial,
                 Domain::Select, Group::Selection, 230u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {},
                 kSelectionKeywords),
    presentation(quader_command::kSelectCoplanarFaces, Domain::Select,
                 Group::Selection, 240u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectFaceNormal, Domain::Select,
                 Group::Selection, 250u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectMeshNGons, Domain::Select,
                 Group::Selection, 260u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {},
                 kSelectionKeywords),
    presentation(quader_command::kSelectSimilarMeshes, Domain::Select,
                 Group::Selection, 270u, kMenuPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {},
                 kSelectionKeywords),
    presentation(quader_command::kSelectGrow, Domain::Select,
                 Group::Selection, 280u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),
    presentation(quader_command::kSelectShrink, Domain::Select,
                 Group::Selection, 290u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kSelectionKeywords),

    // Create: creation tools and scene primitives share one discoverable home.
    presentation(quader_command::kToolsActivateCreatePrimitive,
                 Domain::Create, Group::Creation, 10u,
                 kMenuViewportToolbarPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateCreateBox, Domain::Create,
                 Group::Creation, 11u, kPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateCreateCylinder,
                 Domain::Create, Group::Creation, 12u, kPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateCreateSphere, Domain::Create,
                 Group::Creation, 13u, kPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateCreateTorus, Domain::Create,
                 Group::Creation, 14u, kPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateCreateCone, Domain::Create,
                 Group::Creation, 15u, kPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateCreateCurve, Domain::Create,
                 Group::Creation, 16u, kPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivatePoly, Domain::Create,
                 Group::Creation, 20u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateCurve, Domain::Create,
                 Group::Creation, 30u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kSceneCreatePointLight, Domain::Create,
                 Group::Creation, 100u, kMenuViewportPalette,
                 Palette::Searchable, Activation::InteractiveTool),
    presentation(quader_command::kSceneCreateDirectionalLight,
                 Domain::Create, Group::Creation, 110u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::InteractiveTool),
    presentation(quader_command::kSceneCreateEmpty, Domain::Create,
                 Group::Creation, 120u, kPalette, Palette::Searchable,
                 Activation::Immediate),
    presentation(quader_command::kToolsApplyPrimitiveCreation,
                 Domain::Create, Group::Preview, 200u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::PreviewCommit),
    presentation(quader_command::kToolsCancelPrimitiveCreation,
                 Domain::Create, Group::Preview, 210u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::PreviewCancel),

    presentation(quader_command::kToolsFlipPrimitiveFaces,
                 Domain::Create, Group::Creation, 220u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::Toggle, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kToolsTogglePrimitiveCaps,
                 Domain::Create, Group::Creation, 230u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::Toggle),
    presentation(quader_command::kToolsTogglePrimitiveHemisphere,
                 Domain::Create, Group::Creation, 240u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::Toggle),

    // Object: hierarchy, transforms, arrangement, duplication and visibility.
    presentation(quader_command::kEditCreateInstance, Domain::Object,
                 Group::Instances, 10u, kMenuViewportHierarchyPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kInstanceAliases),
    presentation(quader_command::kEditFlattenInstance, Domain::Object,
                 Group::Instances, 20u, kMenuViewportHierarchyPalette,
                 Palette::Searchable, Activation::Immediate,
                 Danger::Caution, kFlattenInstanceAliases),
    presentation(quader_command::kEditExitInstanceEdit, Domain::Object,
                 Group::Instances, 30u,
                 Surface::ViewportContextMenu | Surface::HierarchyContextMenu,
                 Palette::ContextOnly),
    presentation(quader_command::kGroupSelection, Domain::Object,
                 Group::Grouping, 100u, kMenuViewportHierarchyPalette),
    presentation(quader_command::kUngroupSelection, Domain::Object,
                 Group::Grouping, 110u, kMenuViewportHierarchyPalette),
    presentation(quader_command::kSceneLayersRemoveSelection, Domain::Object,
                 Group::Grouping, 120u, kMenuLayersPalette),
    presentation(quader_command::kToolsActivateMove, Domain::Object,
                 Group::Transform, 200u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateRotate, Domain::Object,
                 Group::Transform, 210u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateScale, Domain::Object,
                 Group::Transform, 220u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivatePivot, Domain::Object,
                 Group::Transform, 230u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kModelingCopyPivotFromActive,
                 Domain::Object, Group::Transform, 240u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingSnapMeshesToFloor, Domain::Object,
                 Group::Transform, 250u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignXMin, Domain::Object,
                 Group::Arrangement, 300u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignXCenter, Domain::Object,
                 Group::Arrangement, 310u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignXMax, Domain::Object,
                 Group::Arrangement, 320u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignYMin, Domain::Object,
                 Group::Arrangement, 330u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignYCenter, Domain::Object,
                 Group::Arrangement, 340u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignYMax, Domain::Object,
                 Group::Arrangement, 350u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignZMin, Domain::Object,
                 Group::Arrangement, 360u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignZCenter, Domain::Object,
                 Group::Arrangement, 370u, kMenuViewportPalette),
    presentation(quader_command::kModelingAlignZMax, Domain::Object,
                 Group::Arrangement, 380u, kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeCentersX,
                 Domain::Object, Group::Arrangement, 390u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeCentersY,
                 Domain::Object, Group::Arrangement, 400u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeCentersZ,
                 Domain::Object, Group::Arrangement, 410u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeGapsX, Domain::Object,
                 Group::Arrangement, 420u, kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeGapsY, Domain::Object,
                 Group::Arrangement, 430u, kMenuViewportPalette),
    presentation(quader_command::kModelingDistributeGapsZ, Domain::Object,
                 Group::Arrangement, 440u, kMenuViewportPalette),
    presentation(quader_command::kDuplicateSelection, Domain::Object,
                 Group::Duplication, 500u,
                 kMenuViewportHierarchyPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, kDuplicateAliases),
    presentation(quader_command::kToolsActivateMirror, Domain::Object,
                 Group::Duplication, 510u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateLinearArray,
                 Domain::Object, Group::Duplication, 520u,
                 kMenuViewportToolbarPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivateRadialArray,
                 Domain::Object, Group::Duplication, 530u,
                 kMenuViewportToolbarPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kToolsActivatePathArray, Domain::Object,
                 Group::Duplication, 540u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kApplyMaterial, Domain::Object,
                 Group::Materials, 600u, kMenuViewportHierarchyPalette),
    presentation(quader_command::kSceneHideSelection, Domain::Object,
                 Group::Visibility, 700u,
                 kMenuViewportHierarchyLayersPalette),
    presentation(quader_command::kSceneUnhideHiddenObjects, Domain::Object,
                 Group::Visibility, 710u,
                 kMenuViewportHierarchyLayersPalette),
    presentation(quader_command::kDeleteSelection, Domain::Object,
                 Group::Cleanup, 900u,
                 kMenuViewportHierarchyLayersPalette, Palette::Searchable,
                 Activation::Immediate, Danger::Destructive),

    // Mesh: whole-mesh and cross-component workflows. Component-native
    // operations live in the Vertex, Edge, and Face domains below.
    presentation(quader_command::kToolsActivateExtrude, Domain::Mesh,
                 Group::Tools, 10u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateKnife, Domain::Mesh,
                 Group::Tools, 20u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kToolsActivateCut, Domain::Mesh,
                 Group::Tools, 30u, kMenuViewportToolbarPalette,
                 Palette::Searchable, Activation::InteractiveTool,
                 Danger::None, {}, {}, "tool_settings"),
    presentation(quader_command::kModelingDuplicateMeshes, Domain::Mesh,
                 Group::Duplication, 100u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kDuplicateAliases),
    presentation(quader_command::kModelingCombineMesh, Domain::Mesh,
                 Group::Topology, 200u, kMenuViewportPalette),
    presentation(quader_command::kModelingCombineMeshes, Domain::Mesh,
                 Group::Topology, 210u, kPalette),
    presentation(quader_command::kModelingBridge, Domain::Mesh,
                 Group::Topology, 220u, kPalette),
    presentation(quader_command::kModelingCreateOuterHull, Domain::Mesh,
                 Group::Topology, 230u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None,
                 kOuterHullAliases, {}, "tool_settings"),
    presentation(quader_command::kModelingHalfCut, Domain::Mesh,
                 Group::Topology, 240u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None, {},
                 {}, "tool_settings"),
    presentation(quader_command::kModelingRadialAlign, Domain::Mesh,
                 Group::Transform, 300u, kPalette),
    presentation(quader_command::kModelingMirrorSelectionX, Domain::Mesh,
                 Group::Transform, 310u, kMenuViewportPalette),
    presentation(quader_command::kModelingMirrorSelectionY, Domain::Mesh,
                 Group::Transform, 320u, kMenuViewportPalette),
    presentation(quader_command::kModelingMirrorSelectionZ, Domain::Mesh,
                 Group::Transform, 330u, kMenuViewportPalette),
    presentation(quader_command::kModelingFlipHorizontal, Domain::Mesh,
                 Group::Transform, 340u, kMenuViewportPalette),
    presentation(quader_command::kModelingFlipVertical, Domain::Mesh,
                 Group::Transform, 350u, kMenuViewportPalette),
    presentation(quader_command::kModelingFlipFaceNormals, Domain::Mesh,
                 Group::Normals, 400u, kPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {}, kNormalKeywords),
    presentation(quader_command::kModelingInvertMesh, Domain::Mesh,
                 Group::Normals, 410u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kModelingRecalculateFaceNormals,
                 Domain::Mesh, Group::Normals, 420u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::Immediate, Danger::None, {}, kNormalKeywords),
    presentation(quader_command::kModelingShadeSmoothMesh, Domain::Mesh,
                 Group::Shading, 500u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kShadingKeywords),
    presentation(quader_command::kModelingShadeFlatMesh, Domain::Mesh,
                 Group::Shading, 510u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kShadingKeywords),
    presentation(quader_command::kModelingRemoveDoubles, Domain::Mesh,
                 Group::Cleanup, 900u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kRemoveDoublesAliases),
    presentation(quader_command::kModelingApplyActivePreview,
                 Domain::Mesh, Group::Preview, 1000u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::PreviewCommit),
    presentation(quader_command::kModelingCancelActivePreview,
                 Domain::Mesh, Group::Preview, 1010u,
                 Surface::ViewportContextMenu | Surface::ToolSettings,
                 Palette::ContextOnly, Activation::PreviewCancel),

    // Vertex: topology and transforms first, merge workflows together, and
    // removal last so menus follow the modeling workflow.
    presentation(quader_command::kModelingConnectVertices, Domain::Vertex,
                 Group::Topology, 100u, kMenuViewportPalette),
    presentation(quader_command::kModelingBevelVertices, Domain::Vertex,
                 Group::Topology, 110u, kMenuViewportPalette),
    presentation(quader_command::kModelingSnapVerticesToActive,
                 Domain::Vertex, Group::Transform, 200u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingSnapVerticesToFloor,
                 Domain::Vertex, Group::Transform, 210u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingRadialAlignVertices,
                 Domain::Vertex, Group::Transform, 220u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingMergeVertices, Domain::Vertex,
                 Group::Merge, 300u, kMenuViewportPalette),
    presentation(quader_command::kModelingMergeVerticesAtCenter,
                 Domain::Vertex, Group::Merge, 310u,
                 kMenuViewportPalette),
    presentation(quader_command::kModelingMergeVerticesToActive,
                 Domain::Vertex, Group::Merge, 320u, kPalette),
    presentation(quader_command::kModelingMergeVerticesToCenter,
                 Domain::Vertex, Group::Merge, 330u, kPalette),
    presentation(quader_command::kModelingMergeByDistance, Domain::Vertex,
                 Group::Merge, 340u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None,
                 kMergeDistanceAliases, {}, "tool_settings"),
    presentation(quader_command::kModelingDissolveVertices,
                 Domain::Vertex, Group::Cleanup, 900u,
                 kMenuViewportPalette),

    // Edge: edge-loop activation and topology lead; cleanup removals are
    // deliberately last.
    presentation(quader_command::kToolsActivateInsertEdgeLoop,
                 Domain::Edge, Group::Tools, 10u,
                 kMenuViewportToolbarPalette, Palette::Searchable,
                 Activation::InteractiveTool, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kModelingConnectEdges, Domain::Edge,
                 Group::Topology, 100u, kMenuViewportPalette),
    presentation(quader_command::kModelingSplitEdges, Domain::Edge,
                 Group::Topology, 110u, kMenuViewportPalette),
    presentation(quader_command::kModelingFillHole, Domain::Edge,
                 Group::Topology, 120u, kMenuViewportPalette),
    presentation(quader_command::kModelingBridgeEdges, Domain::Edge,
                 Group::Topology, 130u, kMenuViewportPalette),
    presentation(quader_command::kModelingInterpolatedBridgeEdges,
                 Domain::Edge, Group::Topology, 140u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::Preview, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kModelingBevelEdges, Domain::Edge,
                 Group::Topology, 150u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None, {},
                 {}, "tool_settings"),
    presentation(quader_command::kModelingSnapEdgesToActive, Domain::Edge,
                 Group::Transform, 200u, kMenuViewportPalette),
    presentation(quader_command::kModelingRadialAlignEdges, Domain::Edge,
                 Group::Transform, 210u, kMenuViewportPalette),
    presentation(quader_command::kModelingMergeEdges, Domain::Edge,
                 Group::Merge, 300u, kMenuViewportPalette),
    presentation(quader_command::kModelingDefaultEdgeNormals, Domain::Edge,
                 Group::Normals, 400u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kModelingHardenEdgeNormals, Domain::Edge,
                 Group::Normals, 410u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kModelingSoftenEdgeNormals, Domain::Edge,
                 Group::Normals, 420u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kModelingDissolveEdges, Domain::Edge,
                 Group::Cleanup, 900u, kMenuViewportPalette),
    presentation(quader_command::kModelingCollapseEdges, Domain::Edge,
                 Group::Cleanup, 910u, kMenuViewportPalette),

    // Face: face topology, transforms, normals, and shading. Collapse remains
    // the last destructive-shape operation.
    presentation(quader_command::kModelingExtractFaces, Domain::Face,
                 Group::Topology, 100u, kMenuViewportPalette),
    presentation(quader_command::kModelingDetachFaces, Domain::Face,
                 Group::Topology, 110u, kMenuViewportPalette),
    presentation(quader_command::kModelingCombineFaces, Domain::Face,
                 Group::Topology, 120u, kMenuViewportPalette),
    presentation(quader_command::kModelingBridgeFaces, Domain::Face,
                 Group::Topology, 130u, kMenuViewportPalette),
    presentation(quader_command::kModelingInterpolatedBridgeFaces,
                 Domain::Face, Group::Topology, 140u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::Preview, Danger::None, {}, {},
                 "tool_settings"),
    presentation(quader_command::kModelingSliceQuad, Domain::Face,
                 Group::Topology, 150u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None, {},
                 {}, "tool_settings"),
    presentation(quader_command::kModelingThickenFaces, Domain::Face,
                 Group::Topology, 160u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Preview, Danger::None, {},
                 {}, "tool_settings"),
    presentation(quader_command::kModelingRadialAlignFaces, Domain::Face,
                 Group::Transform, 200u, kMenuViewportPalette),
    presentation(quader_command::kModelingInvertFaces, Domain::Face,
                 Group::Normals, 300u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kNormalKeywords),
    presentation(quader_command::kModelingShadeFacesSmooth, Domain::Face,
                 Group::Shading, 400u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kShadingKeywords),
    presentation(quader_command::kModelingShadeFacesFlat, Domain::Face,
                 Group::Shading, 410u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kShadingKeywords),
    presentation(quader_command::kModelingCollapseFaces, Domain::Face,
                 Group::Cleanup, 900u, kMenuViewportPalette),

    // UV: the main menu is a curated modeling surface; lower-frequency scale,
    // shift, and face-flip variants remain searchable without crowding it.
    presentation(quader_command::kModelingUvPeel, Domain::UV,
                 Group::Texture, 500u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords, "tool_settings"),
    presentation(quader_command::kModelingTextureProjectCube,
                 Domain::UV, Group::Texture, 501u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureProjectCylinder,
                 Domain::UV, Group::Texture, 502u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureProjectSphere,
                 Domain::UV, Group::Texture, 503u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureAlignToGrid,
                 Domain::UV, Group::Texture, 510u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureAlignToFace,
                 Domain::UV, Group::Texture, 520u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureAlignToView,
                 Domain::UV, Group::Texture, 530u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureScaleXUp, Domain::UV,
                 Group::Texture, 540u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureScaleXDown, Domain::UV,
                 Group::Texture, 550u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureScaleYUp, Domain::UV,
                 Group::Texture, 560u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureScaleYDown, Domain::UV,
                 Group::Texture, 570u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureShiftLeft, Domain::UV,
                 Group::Texture, 580u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureShiftRight, Domain::UV,
                 Group::Texture, 590u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureShiftUp, Domain::UV,
                 Group::Texture, 600u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureShiftDown, Domain::UV,
                 Group::Texture, 610u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureRotateClockwise,
                 Domain::UV, Group::Texture, 620u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureRotateCounterClockwise,
                 Domain::UV, Group::Texture, 630u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFlipTextureHorizontal,
                 Domain::UV, Group::Texture, 640u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFlipTextureVertical,
                 Domain::UV, Group::Texture, 650u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFlipFaceHorizontal,
                 Domain::UV, Group::Texture, 660u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFlipFaceVertical,
                 Domain::UV, Group::Texture, 670u, kPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFitBoth, Domain::UV,
                 Group::Texture, 680u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFitX, Domain::UV,
                 Group::Texture, 690u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureFitY, Domain::UV,
                 Group::Texture, 700u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureJustifyLeft, Domain::UV,
                 Group::Texture, 710u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureJustifyRight,
                 Domain::UV, Group::Texture, 720u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureJustifyTop, Domain::UV,
                 Group::Texture, 730u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureJustifyBottom,
                 Domain::UV, Group::Texture, 740u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    presentation(quader_command::kModelingTextureJustifyCenter,
                 Domain::UV, Group::Texture, 750u, kMenuPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 {}, kTextureKeywords),
    // View: focused viewport navigation and presentation, including layouts.
    presentation(quader_command::kViewportFrameSelection, Domain::View,
                 Group::Camera, 10u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kFrameSelectionAliases),
    presentation(quader_command::kViewportFocusAll, Domain::View,
                 Group::Camera, 20u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kFocusAllAliases),
    presentation(quader_command::kViewportFocusOrigin, Domain::View,
                 Group::Camera, 30u, kMenuViewportPalette,
                 Palette::Searchable, Activation::Immediate, Danger::None,
                 kFocusOriginAliases),
    presentation(quader_command::kViewportViewPerspective, Domain::View,
                 Group::Camera, 100u, kMenuPalette, Palette::Searchable,
                 Activation::Choice, Danger::None, kPerspectiveAliases),
    presentation(quader_command::kViewportViewTop, Domain::View,
                 Group::Camera, 110u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportViewBottom, Domain::View,
                 Group::Camera, 120u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportViewLeft, Domain::View,
                 Group::Camera, 130u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportViewRight, Domain::View,
                 Group::Camera, 140u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportViewFront, Domain::View,
                 Group::Camera, 150u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportViewBack, Domain::View,
                 Group::Camera, 160u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportToggleLocalVisibility,
                 Domain::View, Group::Display, 200u,
                 kMenuViewportPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleTransformSpace,
                 Domain::View, Group::Display, 210u, kMenuPalette,
                 Palette::Searchable, Activation::Toggle),
    presentation(quader_command::kViewportToggleGrid, Domain::View,
                 Group::Display, 220u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleMeshGrid, Domain::View,
                 Group::Display, 230u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleGizmos, Domain::View,
                 Group::Display, 240u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleOverlays, Domain::View,
                 Group::Display, 250u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleCollisions, Domain::View,
                 Group::Display, 255u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleTextureLock, Domain::View,
                 Group::Display, 260u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle),
    presentation(quader_command::kViewportToggleTextureLockScale,
                 Domain::View, Group::Display, 270u, kMenuPalette,
                 Palette::Searchable, Activation::Toggle),
    presentation(quader_command::kViewportToggleInfo, Domain::View,
                 Group::Display, 280u, kMenuPalette, Palette::Searchable,
                 Activation::Toggle, Danger::None, {},
                 kViewportInfoKeywords),
    presentation(quader_command::kViewportToggleNavigationGizmo,
                 Domain::View, Group::Display, 290u, kMenuPalette,
                 Palette::Searchable, Activation::Toggle, Danger::None,
                 kNavigationGizmoAliases, kNavigationGizmoKeywords),
    presentation(quader_command::kViewportShadingWireframe, Domain::View,
                 Group::Shading, 300u, kMenuPalette, Palette::Searchable,
                 Activation::Choice, Danger::None, kWireframeAliases),
    presentation(quader_command::kViewportShadingSolid, Domain::View,
                 Group::Shading, 310u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportShadingShaded, Domain::View,
                 Group::Shading, 320u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportShadingRendered, Domain::View,
                 Group::Shading, 330u, kMenuPalette, Palette::Searchable,
                 Activation::Choice),
    presentation(quader_command::kViewportGridIncrement, Domain::View,
                 Group::Grid, 400u, kMenuPalette),
    presentation(quader_command::kViewportGridDecrement, Domain::View,
                 Group::Grid, 410u, kMenuPalette),
    presentation(quader_command::kViewportGridReset, Domain::View,
                 Group::Grid, 420u, kMenuPalette),
    // Window: auxiliary viewport windows and stable workspace layouts.
    presentation(quader_command::kViewportSpawnWindow, Domain::Window,
                 Group::Layout, 10u, kMenuPalette),
    presentation(quader_command::kViewportLayoutSingle, Domain::Window,
                 Group::Layout, 100u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutTwoVertical, Domain::Window,
                 Group::Layout, 110u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutTwoHorizontal, Domain::Window,
                 Group::Layout, 120u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutThreePrimaryRight,
                 Domain::Window, Group::Layout, 130u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutThreePrimaryLeft,
                 Domain::Window, Group::Layout, 140u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutThreePrimaryBottom,
                 Domain::Window, Group::Layout, 150u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutThreePrimaryTop,
                 Domain::Window, Group::Layout, 160u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),
    presentation(quader_command::kViewportLayoutFourQuad, Domain::Window,
                 Group::Layout, 170u, kMenuPalette,
                 Palette::Searchable, Activation::Choice),

    // Help remains a small stable application surface.
    presentation(quader_command::kUiSearchCommands, Domain::Help,
                 Group::Discovery, 10u, Surface::MainMenu,
                 Palette::SelfExempt, Activation::Immediate, Danger::None,
                 kSearchAliases, kSearchKeywords),

#ifndef NDEBUG
    presentation(quader_command::kDevCopySelectionJson, Domain::Internal,
                 Group::Internal, 10u, Surface::None, Palette::Internal),
#endif
};

} // namespace

std::span<const CommandPresentationDescriptor>
command_presentation_catalog() noexcept {
  return kCatalog;
}

const CommandPresentationDescriptor *
find_command_presentation(std::string_view command_id) noexcept {
  const auto found = std::find_if(
      std::begin(kCatalog), std::end(kCatalog),
      [&](const CommandPresentationDescriptor &descriptor) {
        return descriptor.command_id.string() == command_id;
      });
  return found == std::end(kCatalog) ? nullptr : &*found;
}

std::string_view
command_presentation_domain_id(CommandPresentationDomain domain) noexcept {
  switch (domain) {
  case Domain::File:
    return "file";
  case Domain::Edit:
    return "edit";
  case Domain::Select:
    return "select";
  case Domain::Create:
    return "create";
  case Domain::Object:
    return "object";
  case Domain::Mesh:
    return "mesh";
  case Domain::Vertex:
    return "vertex";
  case Domain::Edge:
    return "edge";
  case Domain::Face:
    return "face";
  case Domain::UV:
    return "uv";
  case Domain::View:
    return "view";
  case Domain::Window:
    return "window";
  case Domain::Help:
    return "help";
  case Domain::Internal:
    return "internal";
  }
  return "internal";
}

std::string_view command_presentation_domain_label(
    CommandPresentationDomain domain) noexcept {
  switch (domain) {
  case Domain::File:
    return "File";
  case Domain::Edit:
    return "Edit";
  case Domain::Select:
    return "Select";
  case Domain::Create:
    return "Create";
  case Domain::Object:
    return "Object";
  case Domain::Mesh:
    return "Mesh";
  case Domain::Vertex:
    return "Vertex";
  case Domain::Edge:
    return "Edge";
  case Domain::Face:
    return "Face";
  case Domain::UV:
    return "UV";
  case Domain::View:
    return "View";
  case Domain::Window:
    return "Window";
  case Domain::Help:
    return "Help";
  case Domain::Internal:
    return "Internal";
  }
  return "Internal";
}

std::string_view
command_presentation_group_id(CommandPresentationGroup group) noexcept {
  switch (group) {
  case Group::Project:
    return "project";
  case Group::SceneDocument:
    return "scene_document";
  case Group::History:
    return "history";
  case Group::Clipboard:
    return "clipboard";
  case Group::Selection:
    return "selection";
  case Group::Instances:
    return "instances";
  case Group::Grouping:
    return "grouping";
  case Group::Materials:
    return "materials";
  case Group::Visibility:
    return "visibility";
  case Group::Creation:
    return "creation";
  case Group::Transform:
    return "transform";
  case Group::Arrangement:
    return "arrangement";
  case Group::Duplication:
    return "duplication";
  case Group::Topology:
    return "topology";
  case Group::Merge:
    return "merge";
  case Group::Normals:
    return "normals";
  case Group::Shading:
    return "shading";
  case Group::Texture:
    return "texture";
  case Group::Cleanup:
    return "cleanup";
  case Group::Tools:
    return "tools";
  case Group::Camera:
    return "camera";
  case Group::Display:
    return "display";
  case Group::Layout:
    return "layout";
  case Group::Grid:
    return "grid";
  case Group::Preview:
    return "preview";
  case Group::Discovery:
    return "discovery";
  case Group::Internal:
    return "internal";
  }
  return "internal";
}

std::string_view command_presentation_group_label(
    CommandPresentationGroup group) noexcept {
  switch (group) {
  case Group::Project:
    return "Project";
  case Group::SceneDocument:
    return "Scene";
  case Group::History:
    return "History";
  case Group::Clipboard:
    return "Clipboard";
  case Group::Selection:
    return "Selection";
  case Group::Instances:
    return "Instances";
  case Group::Grouping:
    return "Grouping";
  case Group::Materials:
    return "Materials";
  case Group::Visibility:
    return "Visibility";
  case Group::Creation:
    return "Create";
  case Group::Transform:
    return "Transform";
  case Group::Arrangement:
    return "Align and Distribute";
  case Group::Duplication:
    return "Duplicate and Array";
  case Group::Topology:
    return "Topology";
  case Group::Merge:
    return "Merge";
  case Group::Normals:
    return "Normals";
  case Group::Shading:
    return "Shading";
  case Group::Texture:
    return "Texture";
  case Group::Cleanup:
    return "Cleanup";
  case Group::Tools:
    return "Tools";
  case Group::Camera:
    return "Camera";
  case Group::Display:
    return "Display";
  case Group::Layout:
    return "Viewport Layout";
  case Group::Grid:
    return "Grid";
  case Group::Preview:
    return "Active Operation";
  case Group::Discovery:
    return "Discovery";
  case Group::Internal:
    return "Internal";
  }
  return "Internal";
}

} // namespace quader
