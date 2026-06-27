////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_settings.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

class ModelingSessionService;

/**
 * Enumerates ViewportToolEventKind values used by editor-owned viewport tool interactions.
 */
enum class ViewportToolEventKind {
    Activation,
    Frame,
    Cancel,
};

/**
 * Stores the Viewport Pointer Tool Event data contract used by editor-owned viewport tool interactions.
 */
struct ViewportPointerToolEvent {
    bool hovered = false;
    bool leftClicked = false;
    bool leftDown = false;
    bool leftReleased = false;
    bool canInteract = false;
    bool finalizePressed = false;
    bool modeCyclePressed = false;
    bool suppressHoverWork = false;
    bool gizmoConsumed = false;
    bool navigationActive = false;
    bool textInputActive = false;
    QVec2 mouseLocal = {};
    QVec2 leftClickLocal = {};
    QVec2 viewportSize = {};
    ViewportModifiers modifiers = {};
};

/**
 * Stores the Viewport Tool Event data contract used by editor-owned viewport tool interactions.
 */
struct ViewportToolEvent {
    ViewportToolEventKind kind = ViewportToolEventKind::Activation;
    ViewportToolId tool = ViewportToolId::Select;
    ViewportShortcut shortcut = {};
    bool fromShortcut = false;
    bool exitActiveModalTool = false;
    ViewportPointerToolEvent pointer = {};
};

/**
 * Stores the Viewport Modal Tool State data contract used by editor-owned viewport tool interactions.
 */
struct ViewportModalToolState {
    ModelingBoxPreview boxPreview = {};
    bool insertEdgeLoopDragActive = false;
    bool cutDragActive = false;
    bool mirrorDragActive = false;
};

/**
 * Stores the Viewport Tool Result data contract used by editor-owned viewport tool interactions.
 */
struct ViewportToolResult {
    bool handled = false;
    bool consumed = false;
    bool modalStateChanged = false;
    bool activeToolChanged = false;
    bool syncActiveToolFromModeling = false;
    bool selectionModeChanged = false;
};

/**
 * Stores the Box Tool Context data contract used by box-shaped viewport tool interactions.
 */
struct ViewportBoxToolContext {
    AssetId defaultMaterial = InvalidAssetId;
    std::function<ModelingBoxToolEvent()> makeEvent;
    std::function<ModelingCreateBoxResult(std::string, AssetId)> commitPreview;
    std::function<std::string()> nextMeshName;
};

/**
 * Stores the Point Tool Context data contract used by point/ray viewport tool interactions.
 */
struct ViewportPointToolContext {
    std::function<ModelingToolRayEvent(QVec2, bool)> makeEventAt;
    std::function<std::optional<int>(const std::array<QVec3, 3>&, int)> hitDrawnPlaneCapturedPoint;
    std::function<bool(const std::array<QVec3, 3>&, int, bool)> hitDrawnPlaneLine;
};

/**
 * Stores the Viewport Tool Context data contract used by shared editor-owned viewport tool interactions.
 */
struct ViewportToolContext {
    ViewportToolState& state;
    ViewportCameraController& camera;
    ModelingSessionService& modeling;
    SceneDocument* document = nullptr;
    const ViewportSettings* settings = nullptr;
    ViewportModalToolState* modalState = nullptr;
    ViewportBoxToolContext* box = nullptr;
    ViewportPointToolContext* point = nullptr;

    std::function<bool(ModelingBoxDragState)> claimHover;
    std::function<void()> invalidateHover;
    std::function<void()> markOverlayDirty;
    std::function<void(ViewportToolId)> setActiveTool;
    std::function<void(ViewportSelectionMode)> setSelectionMode;
    std::function<void(ModelingSelectionMode)> setAppSelectionMode;
    std::function<void(std::string_view, const ModelingCommandResult&)> handleToolCommandResult;
};

/**
 * Implements Tool behavior for editor-owned viewport tool interactions.
 */
class Tool {
public:
    explicit Tool(ToolDescriptor descriptor);
    virtual ~Tool();

    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;
    Tool(Tool&&) = delete;
    Tool& operator=(Tool&&) = delete;

    [[nodiscard]] const ToolDescriptor& descriptor() const;
    [[nodiscard]] ViewportToolId viewportTool() const;
    [[nodiscard]] ModelingViewportTool modelingTool() const;
    [[nodiscard]] std::string_view id() const;
    [[nodiscard]] std::string_view label() const;
    [[nodiscard]] std::string_view shortcut() const;
    [[nodiscard]] ToolKind kind() const;
    [[nodiscard]] ToolMenuGroup menuGroup() const;
    [[nodiscard]] virtual bool available() const;
    [[nodiscard]] virtual bool ownsViewport() const;
    [[nodiscard]] virtual bool blocksViewportFocusShortcuts() const;
    [[nodiscard]] virtual ModelingTransformTool transformTool() const;
    [[nodiscard]] virtual bool supportsTransformGizmo() const;

    virtual ModelingCommandResult activate(ModelingSessionService& modeling) const;

private:
    ToolDescriptor descriptor_;
};

/**
 * Implements Viewport Tool behavior for editor-owned viewport tool interactions.
 */
class ViewportTool : public Tool {
public:
    static constexpr ViewportToolId Select = ViewportToolId::Select;
    static constexpr ViewportToolId Move = ViewportToolId::Move;
    static constexpr ViewportToolId Extend = ViewportToolId::Extend;
    static constexpr ViewportToolId Extrude = ViewportToolId::Extrude;
    static constexpr ViewportToolId Rotate = ViewportToolId::Rotate;
    static constexpr ViewportToolId Scale = ViewportToolId::Scale;
    static constexpr ViewportToolId Box = ViewportToolId::Box;
    static constexpr ViewportToolId InsertEdgeLoop = ViewportToolId::InsertEdgeLoop;
    static constexpr ViewportToolId Knife = ViewportToolId::Knife;
    static constexpr ViewportToolId Cut = ViewportToolId::Cut;
    static constexpr ViewportToolId Poly = ViewportToolId::Poly;
    static constexpr ViewportToolId Mirror = ViewportToolId::Mirror;

    explicit ViewportTool(ToolDescriptor descriptor);
    ViewportTool(ViewportToolId tool);

    [[nodiscard]] operator ViewportToolId() const;
    [[nodiscard]] virtual bool allowsSelectionFallback() const;
    [[nodiscard]] virtual ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const;

protected:
    [[nodiscard]] ViewportToolResult activateById(
            ViewportToolContext& context,
            ViewportToolId tool) const;
};

} // namespace quader
