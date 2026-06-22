#include "viewport/ViewportWorkspaceService.h"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

void resetCameraForView(ViewportCameraController& camera, ViewportView view) {
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

ViewportView viewForCamera(const ViewportCameraController& camera) {
    if (camera.state().projection != ViewportProjection::Orthographic) {
        return ViewportView::Perspective;
    }

    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
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

} // namespace

ViewportWorkspaceService::ViewportWorkspaceService() {
    panes_[viewportPaneIndex(ViewportPaneId::Perspective)].id = ViewportPaneId::Perspective;
    panes_[viewportPaneIndex(ViewportPaneId::Top)].id = ViewportPaneId::Top;
    panes_[viewportPaneIndex(ViewportPaneId::Front)].id = ViewportPaneId::Front;
    panes_[viewportPaneIndex(ViewportPaneId::Side)].id = ViewportPaneId::Side;
    for (ViewportWorkspacePaneState& paneState : panes_) {
        paneState.view = viewportViewForPane(paneState.id);
        resetCameraForView(paneState.camera, paneState.view);
    }
}

bool ViewportWorkspaceService::setLayout(ViewportLayoutId layout) {
    if (layout_ == layout) {
        return false;
    }
    layout_ = layout;
    if (!focusedPaneIsVisible(false)) {
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
    return pane(paneId).view;
}

bool ViewportWorkspaceService::setView(ViewportPaneId paneId, ViewportView view) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    const bool changed = paneState.view != view || viewForCamera(paneState.camera) != view;
    if (!changed) {
        return false;
    }
    paneState.view = view;
    resetCameraForView(paneState.camera, view);
    return true;
}

void ViewportWorkspaceService::syncViewFromCamera(ViewportPaneId paneId) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    paneState.view = viewForCamera(paneState.camera);
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
    return panes_[viewportPaneIndex(paneId)];
}

const ViewportWorkspacePaneState& ViewportWorkspaceService::pane(ViewportPaneId paneId) const {
    return panes_[viewportPaneIndex(paneId)];
}

std::vector<ViewportPanePlacement> ViewportWorkspaceService::activePanePlacements(bool maximized) const {
    if (maximized) {
        return {ViewportPanePlacement{
                .paneId = focusedPane_,
                .normalizedRect = {},
        }};
    }
    return viewportLayoutPlacements(layout_, layoutState_);
}

std::vector<ViewportSplitHandle> ViewportWorkspaceService::activeSplitHandles(bool maximized) const {
    if (maximized) {
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

bool ViewportWorkspaceService::setPaneRenderSize(ViewportPaneId paneId, Vec2 size) {
    ViewportWorkspacePaneState& paneState = pane(paneId);
    if (paneState.renderSize.x == size.x && paneState.renderSize.y == size.y) {
        return false;
    }
    paneState.renderSize = size;
    paneState.camera.setViewportSize(size.x, size.y);
    return true;
}

Vec2 ViewportWorkspaceService::paneRenderSize(ViewportPaneId paneId) const {
    return pane(paneId).renderSize;
}

bool ViewportWorkspaceService::focusedPaneIsVisible(bool maximized) const {
    const std::vector<ViewportPanePlacement> placements = activePanePlacements(maximized);
    return std::any_of(placements.begin(), placements.end(), [&](const ViewportPanePlacement& placement) {
        return placement.paneId == focusedPane_;
    });
}

} // namespace quader
