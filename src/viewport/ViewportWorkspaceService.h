////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportLayout.h"

#include <array>
#include <vector>

namespace quader {

/**
 * Enumerates ViewportShadingMode values used by the viewport layer.
 */
enum class ViewportShadingMode {
    Wireframe,
    Shaded,
    Rendered,
};

/**
 * Enumerates Viewport Workspace Layout Policy values used by the viewport layer.
 */
enum class ViewportWorkspaceLayoutPolicy {
    // Current embedded Rml host policy; separate OS windowed viewports remain runtime-only.
    SinglePaneOnly,
    MultiPane,
};

/**
 * Stores the Viewport Workspace Pane State data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportWorkspacePaneState {
    ViewportPaneId id = ViewportPaneId::Perspective;
    ViewportShadingMode shading = ViewportShadingMode::Shaded;
    ViewportCameraController camera;
    QVec2 renderSize = {1.0f, 1.0f};
    bool visibleThisFrame = false;
};

void resetViewportCameraForView(ViewportCameraController& camera, ViewportView view);
[[nodiscard]] ViewportView viewportViewForCamera(const ViewportCameraController& camera);

/**
 * Coordinates Viewport Workspace Service responsibilities for the viewport input, layout, and camera layer.
 */
class ViewportWorkspaceService final {
public:
    explicit ViewportWorkspaceService(
            ViewportWorkspaceLayoutPolicy layoutPolicy = ViewportWorkspaceLayoutPolicy::MultiPane);

    [[nodiscard]] ViewportLayoutId layout() const { return layout_; }
    bool setLayout(ViewportLayoutId layout);
    [[nodiscard]] ViewportWorkspaceLayoutPolicy layoutPolicy() const { return layoutPolicy_; }
    bool setLayoutPolicy(ViewportWorkspaceLayoutPolicy policy);
    [[nodiscard]] bool supportsLayout(ViewportLayoutId layout) const;

    [[nodiscard]] ViewportPaneId focusedPane() const { return focusedPane_; }
    bool focusPane(ViewportPaneId pane);
    [[nodiscard]] bool focusedPaneMaximized() const { return focusedPaneMaximized_; }
    bool setFocusedPaneMaximized(bool maximized);
    bool toggleFocusedPaneMaximized();

    [[nodiscard]] int gridPreset() const { return gridPreset_; }
    bool setGridPreset(int preset);

    [[nodiscard]] ViewportShadingMode shadingMode(ViewportPaneId pane) const;
    bool setShadingMode(ViewportPaneId pane, ViewportShadingMode mode);

    [[nodiscard]] ViewportView view(ViewportPaneId pane) const;
    bool setView(ViewportPaneId pane, ViewportView view);

    [[nodiscard]] ViewportCameraController& camera(ViewportPaneId pane);
    [[nodiscard]] const ViewportCameraController& camera(ViewportPaneId pane) const;
    bool focusOrigin(ViewportPaneId pane, float distance = 64.0f);

    [[nodiscard]] ViewportWorkspacePaneState& pane(ViewportPaneId pane);
    [[nodiscard]] const ViewportWorkspacePaneState& pane(ViewportPaneId pane) const;

    [[nodiscard]] std::vector<ViewportPanePlacement> activePanePlacements() const;
    [[nodiscard]] std::vector<ViewportSplitHandle> activeSplitHandles() const;
    void setSplitHandlePosition(ViewportSplitHandleId handle, float normalizedPosition);

    void clearPaneVisibility();
    void setPaneVisible(ViewportPaneId pane, bool visible);
    [[nodiscard]] bool paneVisible(ViewportPaneId pane) const;
    bool setPaneRenderSize(ViewportPaneId pane, QVec2 size);
    [[nodiscard]] QVec2 paneRenderSize(ViewportPaneId pane) const;

private:
    [[nodiscard]] bool focusedPaneIsVisible() const;
    [[nodiscard]] std::vector<ViewportPanePlacement> layoutPanePlacements() const;

    std::array<ViewportWorkspacePaneState, kViewportPaneCount> panes_;
    ViewportLayoutState layoutState_;
    ViewportLayoutId layout_ = ViewportLayoutId::Single;
    ViewportWorkspaceLayoutPolicy layoutPolicy_ = ViewportWorkspaceLayoutPolicy::MultiPane;
    ViewportPaneId focusedPane_ = ViewportPaneId::Perspective;
    bool focusedPaneMaximized_ = false;
    int gridPreset_ = 0;
};

} // namespace quader
