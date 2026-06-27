////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_workspace_service.hpp"

#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace quader {

void resetViewportCameraForView(ViewportCameraController& camera, ViewportView view) {
    camera = ViewportCameraController{};
    const float focusOriginDistance = static_cast<float>(viewportHostDefaults().focusOriginDistance);
    switch (view) {
    case ViewportView::Top:
        camera.focusOrigin(focusOriginDistance);
        camera.viewAxis(ViewportViewAxis::Y, 1);
        break;
    case ViewportView::Bottom:
        camera.focusOrigin(focusOriginDistance);
        camera.viewAxis(ViewportViewAxis::Y, -1);
        break;
    case ViewportView::Front:
        camera.focusOrigin(focusOriginDistance);
        camera.viewAxis(ViewportViewAxis::Z, -1);
        break;
    case ViewportView::Back:
        camera.focusOrigin(focusOriginDistance);
        camera.viewAxis(ViewportViewAxis::Z, 1);
        break;
    case ViewportView::Side:
        camera.focusOrigin(focusOriginDistance);
        camera.viewAxis(ViewportViewAxis::X, 1);
        break;
    case ViewportView::Left:
        camera.focusOrigin(focusOriginDistance);
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

ViewportWorkspaceService::ViewportWorkspaceService(ViewportWorkspaceLayoutPolicy layoutPolicy)
    : layoutPolicy_(layoutPolicy) {
    gridPreset_ = viewportDefaultGridPreset();
    panes_[viewportPaneIndex(ViewportPaneId::Perspective)].id = ViewportPaneId::Perspective;
    panes_[viewportPaneIndex(ViewportPaneId::Top)].id = ViewportPaneId::Top;
    panes_[viewportPaneIndex(ViewportPaneId::Front)].id = ViewportPaneId::Front;
    panes_[viewportPaneIndex(ViewportPaneId::Side)].id = ViewportPaneId::Side;
    for (ViewportWorkspacePaneState& paneState : panes_) {
        resetViewportCameraForView(paneState.camera, viewportViewForPane(paneState.id));
    }
}

bool ViewportWorkspaceService::setLayout(ViewportLayoutId layout) {
    if (!supportsLayout(layout)) {
        return false;
    }
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

bool ViewportWorkspaceService::setLayoutPolicy(ViewportWorkspaceLayoutPolicy policy) {
    if (layoutPolicy_ == policy) {
        return false;
    }
    layoutPolicy_ = policy;
    if (!supportsLayout(layout_)) {
        layout_ = ViewportLayoutId::Single;
        focusedPaneMaximized_ = false;
        focusedPane_ = ViewportPaneId::Perspective;
    }
    return true;
}

bool ViewportWorkspaceService::supportsLayout(ViewportLayoutId layout) const {
    if (layoutPolicy_ == ViewportWorkspaceLayoutPolicy::SinglePaneOnly) {
        return layout == ViewportLayoutId::Single;
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
    const int clampedPreset = viewportClampGridPreset(preset);
    if (gridPreset_ == clampedPreset) {
        return false;
    }
    gridPreset_ = clampedPreset;
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
    if (viewportViewForCamera(paneState.camera) == view) {
        return false;
    }
    resetViewportCameraForView(paneState.camera, view);
    return true;
}

ViewportCameraController& ViewportWorkspaceService::camera(ViewportPaneId paneId) {
    return pane(paneId).camera;
}

const ViewportCameraController& ViewportWorkspaceService::camera(ViewportPaneId paneId) const {
    return pane(paneId).camera;
}

bool ViewportWorkspaceService::focusOrigin(ViewportPaneId paneId, float distance) {
    camera(paneId).focusOrigin(distance);
    return true;
}

ViewportWorkspacePaneState& ViewportWorkspaceService::pane(ViewportPaneId paneId) {
    return panes_[viewportPaneIndex(paneId)];
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
    for (ViewportWorkspaceAuxiliaryViewportState& viewportState : auxiliaryViewports_) {
        viewportState.visibleThisFrame = false;
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

ViewportWorkspaceAuxiliaryViewportState& ViewportWorkspaceService::ensureAuxiliaryViewport(
        std::string stableId,
        std::uint8_t layerIndex) {
    if (ViewportWorkspaceAuxiliaryViewportState* existing = auxiliaryViewport(stableId)) {
        existing->layerIndex = layerIndex;
        return *existing;
    }

    ViewportWorkspaceAuxiliaryViewportState state;
    state.stableId = std::move(stableId);
    state.layerIndex = layerIndex;
    state.camera.focusOrigin(static_cast<float>(viewportHostDefaults().focusOriginDistance));
    auxiliaryViewports_.push_back(std::move(state));
    return auxiliaryViewports_.back();
}

bool ViewportWorkspaceService::removeAuxiliaryViewport(std::string_view stableId) {
    const auto found = std::find_if(
            auxiliaryViewports_.begin(),
            auxiliaryViewports_.end(),
            [stableId](const ViewportWorkspaceAuxiliaryViewportState& state) {
                return state.stableId == stableId;
            });
    if (found == auxiliaryViewports_.end()) {
        return false;
    }
    auxiliaryViewports_.erase(found);
    return true;
}

ViewportWorkspaceAuxiliaryViewportState* ViewportWorkspaceService::auxiliaryViewport(std::string_view stableId) {
    const auto found = std::find_if(
            auxiliaryViewports_.begin(),
            auxiliaryViewports_.end(),
            [stableId](const ViewportWorkspaceAuxiliaryViewportState& state) {
                return state.stableId == stableId;
            });
    return found == auxiliaryViewports_.end() ? nullptr : &*found;
}

const ViewportWorkspaceAuxiliaryViewportState* ViewportWorkspaceService::auxiliaryViewport(std::string_view stableId) const {
    const auto found = std::find_if(
            auxiliaryViewports_.begin(),
            auxiliaryViewports_.end(),
            [stableId](const ViewportWorkspaceAuxiliaryViewportState& state) {
                return state.stableId == stableId;
            });
    return found == auxiliaryViewports_.end() ? nullptr : &*found;
}

void ViewportWorkspaceService::setAuxiliaryViewportVisible(std::string_view stableId, bool visible) {
    if (ViewportWorkspaceAuxiliaryViewportState* state = auxiliaryViewport(stableId)) {
        state->visibleThisFrame = visible;
    }
}

bool ViewportWorkspaceService::auxiliaryViewportVisible(std::string_view stableId) const {
    const ViewportWorkspaceAuxiliaryViewportState* state = auxiliaryViewport(stableId);
    return state != nullptr && state->visibleThisFrame;
}

bool ViewportWorkspaceService::setAuxiliaryViewportRenderSize(std::string_view stableId, QVec2 size) {
    ViewportWorkspaceAuxiliaryViewportState* state = auxiliaryViewport(stableId);
    if (state == nullptr) {
        return false;
    }
    if (state->renderSize.x == size.x && state->renderSize.y == size.y) {
        return false;
    }
    state->renderSize = size;
    state->camera.setViewportSize(size.x, size.y);
    return true;
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
