////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "editor/modeling/ModelingTypes.h"
#include "editor/transform/SceneTransformDragService.h"
#include "scene/SceneDocument.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportSettings.h"

#include <imgui.h>

#include <optional>
#include <vector>

#include <editor/interactions/rotate_tool.h>
#include <editor/interactions/tool_identity.h>

namespace quader {

class EditorSelectionService;
class ModelingSessionService;
class QuaderEditorApi;

/**
 * Enumerates TransformAxisHandle values used by the ui layer.
 */
enum class TransformAxisHandle {
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
 * Stores the Transform Gizmo Frame Request data contract used by the ImGui editor presentation layer.
 */
struct TransformGizmoFrameRequest {
    SceneDocument& document;
    const AssetRegistry& assets;
    QuaderEditorApi* editor = nullptr;
    ModelingSessionService& modeling;
    EditorSelectionService& selection;
    ViewportInputRouter& inputRouter;
    const ViewportSettings& viewportSettings;
    ViewportCameraController& camera;
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ImVec2 itemMin = {};
    ImVec2 itemSize = {};
    bool suppressHoverWork = false;
    bool interactive = true;
};

/**
 * Coordinates Transform Gizmo Controller state transitions for the ImGui editor presentation layer.
 */
class TransformGizmoController {
public:
    [[nodiscard]] bool draw(const TransformGizmoFrameRequest& request);
    [[nodiscard]] bool cancelActiveDrag(SceneDocument& document, ModelingSessionService& modeling);

    [[nodiscard]] bool dragging() const { return dragging_; }
    [[nodiscard]] bool dragLocksInput() const { return dragging_ && dragPaneActive_; }
    [[nodiscard]] ViewportPaneId dragPaneId() const { return dragPaneId_; }
    [[nodiscard]] bool supportsViewportTool(ViewportToolId tool) const;
    [[nodiscard]] bool suppressesHover(bool navigationGizmoConsumed, bool transformGizmoConsumed) const;

    [[nodiscard]] quader_editor_core::ViewportInteractionTool runtimeToolFor(ViewportToolId tool) const;

private:
    void resetDragState();

    bool dragging_ = false;
    bool dragPaneActive_ = false;
    ViewportPaneId dragPaneId_ = ViewportPaneId::Perspective;
    bool modelingDragActive_ = false;
    bool modelingDragPreviewDriven_ = false;
    bool sceneOnlyDrag_ = false;
    std::optional<quader_editor_core::RotateToolState> sceneOnlyRotateState_;
    SceneTransformDragService sceneTransformDrag_;
    TransformAxisHandle activeAxis_ = TransformAxisHandle::None;
    Vec2 lastMouse_ = {};
    Vec3 startWorldPivot_ = {};
    Vec2 startMouse_ = {};
};

} // namespace quader
