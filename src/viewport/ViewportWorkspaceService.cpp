////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/ViewportWorkspaceService.h"

#include "viewport/HostViewportContractAdapter.h"
#include "viewport/ViewportCameraAdapter.h"

#include <algorithm>
#include <cmath>

namespace quader {

void resetViewportCameraForView(ViewportCameraController& camera, ViewportView view) {
    camera = ViewportCameraController{};
    switch (view) {
    case ViewportView::Top:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Y, 1);
        break;
    case ViewportView::Bottom:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Y, -1);
        break;
    case ViewportView::Front:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Z, -1);
        break;
    case ViewportView::Back:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Z, 1);
        break;
    case ViewportView::Side:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::X, 1);
        break;
    case ViewportView::Left:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::X, -1);
        break;
    case ViewportView::Perspective:
        break;
    }
}

ViewportView viewportViewForCamera(const ViewportCameraController& camera) {
    if (camera.state().projection != ViewportProjection::Orthographic) {
        return ViewportView::Perspective;
    }

    const QVec3 forward = ViewportCameraAdapter::basis(camera).forward;
    const float absX = std::abs(forward.x);
    const float absY = std::abs(forward.y);
    const float absZ = std::abs(forward.z);
    constexpr float kAxisDotThreshold = 0.985f;
    if (absY >= absX && absY >= absZ && absY >= kAxisDotThreshold) {
        return forward.y < 0.0f ? ViewportView::Top : ViewportView::Bottom;
    }
    if (absZ >= absX && absZ >= absY && absZ >= kAxisDotThreshold) {
        return forward.z > 0.0f ? ViewportView::Front : ViewportView::Back;
    }
    if (absX >= kAxisDotThreshold) {
        return forward.x < 0.0f ? ViewportView::Side : ViewportView::Left;
    }
    return ViewportView::Perspective;
}

ViewportWorkspaceService::ViewportWorkspaceService() {
    gridPreset_ = viewportDefaultGridPreset();
    panes_[viewportPaneIndex(ViewportPaneId::Perspective)].id = ViewportPaneId::Perspective;
    panes_[viewportPaneIndex(ViewportPaneId::Top)].id = ViewportPaneId::Top;
    panes_[viewportPaneIndex(ViewportPaneId::Front)].id = ViewportPaneId::Front;
    panes_[viewportPaneIndex(ViewportPaneId::Side)].id = ViewportPaneId::Side;
    for (ViewportWorkspacePaneState& paneState : panes_) {
        paneState.view = viewportViewForPane(paneState.id);
        resetViewportCameraForView(paneState.camera, paneState.view);
    }
}

bool ViewportWorkspaceService::setLayout(ViewportLayoutId layout) {
    const bool layoutChanged = layout_ != layout;
    const bool maximizedChanged = focusedPaneMaximized_;
    if (!layoutChanged && !maximizedChanged) {
        return false;
    }
    layout_ = layout;
    focusedPaneMaximized_ = false;
    if (!focusedPaneIsVisible()) {
        focusedPane_ = ViewportPaneId::Perspective;
    }
    return true;
}

bool ViewportWorkspaceService::focusPane(ViewportPaneId pane) {
    if (focusedPane_ == pane) {
        return false;
    }
    focusedPane_ = pane;
    return true;
}

bool ViewportWorkspaceService::setFocusedPaneMaximized(bool maximized) {
    if (focusedPaneMaximized_ == maximized) {
        return false;
    }
    focusedPaneMaximized_ = maximized;
    return true;
}

bool ViewportWorkspaceService::toggleFocusedPaneMaximized() {
    focusedPaneMaximized_ = !focusedPaneMaximized_;
    return true;
}

bool ViewportWorkspaceService::setGridPreset(int preset) {
    if (gridPreset_ == preset) {
        return false;
    }
    gridPreset_ = preset;
    return true;
}

ViewportShadingMode ViewportWorkspaceService::shadingMode(ViewportPaneId paneId) const {
    return pane(paneId).shading;
}

bool ViewportWorkspaceService::setShadingMode(ViewportPaneId paneId, ViewportShadingMode mode) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    if (paneState.shading == mode) {
        return false;
    }
    paneState.shading = mode;
    return true;
}

ViewportView ViewportWorkspaceService::view(ViewportPaneId paneId) const {
    return viewportViewForCamera(pane(paneId).camera);
}

bool ViewportWorkspaceService::setView(ViewportPaneId paneId, ViewportView view) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    const bool changed = paneState.view != view || viewportViewForCamera(paneState.camera) != view;
    if (!changed) {
        return false;
    }
    paneState.view = view;
    resetViewportCameraForView(paneState.camera, view);
    return true;
}

void ViewportWorkspaceService::syncViewFromCamera(ViewportPaneId paneId) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    paneState.view = viewportViewForCamera(paneState.camera);
}

ViewportCameraController& ViewportWorkspaceService::camera(ViewportPaneId paneId) {
    return pane(paneId).camera;
}

const ViewportCameraController& ViewportWorkspaceService::camera(ViewportPaneId paneId) const {
    return pane(paneId).camera;
}

bool ViewportWorkspaceService::focusOrigin(ViewportPaneId paneId, float distance) {
    camera(paneId).focusOrigin(distance);
    syncViewFromCamera(paneId);
    return true;
}

ViewportWorkspacePaneState& ViewportWorkspaceService::pane(ViewportPaneId paneId) {
    ViewportWorkspacePaneState& paneState = panes_[viewportPaneIndex(paneId)];
    paneState.view = viewportViewForCamera(paneState.camera);
    return paneState;
}

const ViewportWorkspacePaneState& ViewportWorkspaceService::pane(ViewportPaneId paneId) const {
    return panes_[viewportPaneIndex(paneId)];
}

std::vector<ViewportPanePlacement> ViewportWorkspaceService::activePanePlacements() const {
    if (focusedPaneMaximized_) {
        return {ViewportPanePlacement{
                .paneId = focusedPane_,
                .normalizedRect = {},
        }};
    }
    return layoutPanePlacements();
}

std::vector<ViewportSplitHandle> ViewportWorkspaceService::activeSplitHandles() const {
    if (focusedPaneMaximized_) {
        return {};
    }
    return viewportLayoutSplitHandles(layout_, layoutState_);
}

void ViewportWorkspaceService::setSplitHandlePosition(
        ViewportSplitHandleId handle,
        float normalizedPosition) {
    layoutState_.setHandlePosition(handle, normalizedPosition);
}

void ViewportWorkspaceService::clearPaneVisibility() {
    for (ViewportWorkspacePaneState& paneState : panes_) {
        paneState.visibleThisFrame = false;
    }
}

void ViewportWorkspaceService::setPaneVisible(ViewportPaneId paneId, bool visible) {
    pane(paneId).visibleThisFrame = visible;
}

bool ViewportWorkspaceService::paneVisible(ViewportPaneId paneId) const {
    return pane(paneId).visibleThisFrame;
}

bool ViewportWorkspaceService::setPaneRenderSize(ViewportPaneId paneId, QVec2 size) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    if (paneState.renderSize.x == size.x && paneState.renderSize.y == size.y) {
        return false;
    }
    paneState.renderSize = size;
    paneState.camera.setViewportSize(size.x, size.y);
    return true;
}

QVec2 ViewportWorkspaceService::paneRenderSize(ViewportPaneId paneId) const {
    return pane(paneId).renderSize;
}

bool ViewportWorkspaceService::focusedPaneIsVisible() const {
    const std::vector<ViewportPanePlacement> placements = layoutPanePlacements();
    return std::any_of(placements.begin(), placements.end(), [&](const ViewportPanePlacement& placement) {
        return placement.paneId == focusedPane_;
    });
}

std::vector<ViewportPanePlacement> ViewportWorkspaceService::layoutPanePlacements() const {
    return viewportLayoutPlacements(layout_, layoutState_);
}

} // namespace quader
