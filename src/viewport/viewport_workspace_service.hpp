////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_layout.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
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
    // Host policy for integrations that expose only one embedded viewport surface.
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

/**
 * Stores state for a runtime-created viewport surface that is not part of the embedded pane layout.
 */
struct ViewportWorkspaceAuxiliaryViewportState {
    std::string stableId;
    std::uint8_t layerIndex = 0u;
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

    [[nodiscard]] ViewportWorkspaceAuxiliaryViewportState& ensureAuxiliaryViewport(
            std::string stableId,
            std::uint8_t layerIndex);
    bool removeAuxiliaryViewport(std::string_view stableId);
    [[nodiscard]] ViewportWorkspaceAuxiliaryViewportState* auxiliaryViewport(std::string_view stableId);
    [[nodiscard]] const ViewportWorkspaceAuxiliaryViewportState* auxiliaryViewport(std::string_view stableId) const;
    [[nodiscard]] std::size_t auxiliaryViewportCount() const { return auxiliaryViewports_.size(); }
    [[nodiscard]] ViewportView auxiliaryViewportView(std::string_view stableId) const;
    bool setAuxiliaryViewportView(std::string_view stableId, ViewportView view);
    void setAuxiliaryViewportVisible(std::string_view stableId, bool visible);
    [[nodiscard]] bool auxiliaryViewportVisible(std::string_view stableId) const;
    bool setAuxiliaryViewportRenderSize(std::string_view stableId, QVec2 size);

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
    std::vector<ViewportWorkspaceAuxiliaryViewportState> auxiliaryViewports_;
};

} // namespace quader
