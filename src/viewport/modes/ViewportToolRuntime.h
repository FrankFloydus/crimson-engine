////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportInputRouter.h"
#include "viewport/modes/Tool.h"
#include "viewport/modes/base/ModalViewportTool.h"
#include "viewport/modes/base/TransformViewportTool.h"

#include <span>
#include <string_view>

namespace quader {

/**
 * Owns Viewport Tool Registry lookup and registration for the viewport input, layout, and camera layer.
 */
class ViewportToolRegistry {
public:
    [[nodiscard]] std::span<const ViewportTool* const> tools() const;
    [[nodiscard]] const ViewportTool* find(ViewportToolId tool) const;
    [[nodiscard]] const ViewportTool* find(std::string_view id) const;
    [[nodiscard]] bool available(ViewportToolId tool) const;
};

/**
 * Owns Viewport Tool Host behavior within the viewport input, layout, and camera layer.
 */
class ViewportToolHost {
public:
    [[nodiscard]] bool available(const ViewportTool& tool) const;
    [[nodiscard]] bool ownsViewport(const ViewportTool& tool) const;
    [[nodiscard]] bool blocksViewportFocusShortcuts(const ViewportTool& tool) const;
    [[nodiscard]] bool allowsSelectionFallback(const ViewportTool& tool) const;
    ModelingCommandResult activateTool(const ViewportTool& tool, ModelingSessionService& modeling) const;
};

/**
 * Builds Viewport Tool Event Builder data for the viewport input, layout, and camera layer.
 */
class ViewportToolEventBuilder {
public:
    [[nodiscard]] ViewportToolEvent activationEvent(ViewportToolId tool, const ViewportShortcut* shortcut = nullptr) const;
};

/**
 * Coordinates Viewport Tool Controller state transitions for the viewport input, layout, and camera layer.
 */
class ViewportToolController {
public:
    bool activateTool(
            const ViewportToolRegistry& registry,
            const ViewportToolHost& host,
            ViewportToolState& state,
            ViewportToolId tool,
            const ViewportTool*& activeTool,
            ViewportInputResult* result = nullptr) const;
};

/**
 * Coordinates Viewport Modal Tool Controller state transitions for the viewport input, layout, and camera layer.
 */
class ViewportModalToolController {
public:
    [[nodiscard]] bool handles(const ViewportTool& tool) const;
    [[nodiscard]] bool ownsViewport(const ViewportToolHost& host, const ViewportTool& tool) const;
    [[nodiscard]] bool blocksViewportFocusShortcuts(const ViewportToolHost& host, const ViewportTool& tool) const;
};

/**
 * Coordinates Viewport Transform Tool Controller state transitions for the viewport input, layout, and camera layer.
 */
class ViewportTransformToolController {
public:
    [[nodiscard]] bool handles(const ViewportTool& tool) const;
    [[nodiscard]] ModelingTransformTool transformTool(const ViewportTool& tool) const;
};

/**
 * Owns Viewport Tool Runtime behavior within the viewport input, layout, and camera layer.
 */
class ViewportToolRuntime {
public:
    ViewportToolRuntime();

    [[nodiscard]] const ViewportTool* activeTool() const { return activeTool_; }
    [[nodiscard]] const ViewportToolRegistry& registry() const { return registry_; }
    [[nodiscard]] bool activeToolOwnsViewport() const;
    [[nodiscard]] bool activeToolBlocksViewportFocusShortcuts() const;
    [[nodiscard]] bool toolAvailable(ViewportToolId tool) const;
    [[nodiscard]] bool toolAllowsSelectionFallback(ViewportToolId tool) const;
    [[nodiscard]] ModelingTransformTool transformTool(ViewportToolId tool) const;
    [[nodiscard]] bool hasActiveModalPreview() const;
    [[nodiscard]] const ModelingBoxPreview& boxPreview() const;

    bool setActiveTool(ViewportToolState& state, ViewportToolId tool, ViewportInputResult* result = nullptr);
    void syncActiveTool(ViewportToolId tool);
    void clearModalToolState();
    void clearModalDragState();
    bool handleShortcut(
            const ViewportShortcut& shortcut,
            ViewportToolState& state,
            ViewportCameraController& camera,
            ViewportInputResult& result);
    ViewportToolResult handleActiveToolEvent(
            ViewportToolState& state,
            ViewportToolContext& context,
            const ViewportToolEvent& event);
    ModelingCommandResult activateActiveTool(ModelingSessionService& modeling) const;

private:
    ViewportToolRegistry registry_;
    ViewportToolHost host_;
    ViewportToolEventBuilder eventBuilder_;
    ViewportToolController controller_;
    ViewportModalToolController modalController_;
    ViewportTransformToolController transformController_;
    ViewportModalToolState modalState_;
    const ViewportTool* activeTool_ = nullptr;
};

} // namespace quader
