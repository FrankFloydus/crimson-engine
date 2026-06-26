////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputTypes.h"

#include <memory>

namespace quader {

struct ModelingBoxPreview;
struct ViewportToolContext;
struct ViewportToolEvent;
struct ViewportToolResult;

/**
 * Owns Viewport Input Router behavior within the viewport input, layout, and camera layer.
 */
class ViewportInputRouter {
public:
    ViewportInputRouter();
    ~ViewportInputRouter();

    ViewportInputResult handleFrame(const ViewportInputFrame& input, ViewportCameraController& camera);
    void reset();
    void setActiveTool(ViewportToolId tool);
    void setSelectionMode(ViewportSelectionMode mode);
    void setGridPreset(int preset);
    bool activeToolOwnsViewport() const;
    bool activeToolBlocksViewportFocusShortcuts() const;
    bool activeToolAllowsSelectionFallback() const;
    bool hasActiveModalPreview() const;
    const ModelingBoxPreview& boxPreview() const;
    void clearModalToolState();
    void clearModalDragState();
    ViewportToolResult handleActiveToolEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event);

    const ViewportToolState& state() const { return state_; }
    bool navigating() const { return navigationMode_ != ViewportDragMode::None; }
    bool flyNavigating() const { return navigationMode_ == ViewportDragMode::Fly; }
    bool orbitNavigating() const { return navigationMode_ == ViewportDragMode::Orbit || navigationMode_ == ViewportDragMode::OrbitSnap; }
    bool panNavigating() const { return navigationMode_ == ViewportDragMode::Pan; }
    bool flyCursorCaptureActive() const { return flyCursorCaptureActive_; }
    bool flyHudActive() const { return flyHudActive_; }
    const class ViewportTool* activeTool() const;

private:
    bool handleShortcut(const ViewportShortcut& shortcut, ViewportCameraController& camera, ViewportInputResult& result);

    ViewportToolState state_;
    ViewportDragMode navigationMode_ = ViewportDragMode::None;
    ViewportInputActionKind activeNavigationAction_ = ViewportInputActionKind::None;
    bool orbitSnapActionUsesAltModifier_ = false;
    bool flyCursorCaptureActive_ = false;
    bool flyHudActive_ = false;
    std::unique_ptr<class ViewportToolRuntime> toolRuntime_;
};

} // namespace quader
