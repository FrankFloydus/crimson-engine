#pragma once

#include "math/Types.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportLayout.h"

#include <array>
#include <vector>

namespace quader {

enum class ViewportShadingMode {
    Wireframe,
    Shaded,
    Rendered,
};

struct ViewportWorkspacePaneState {
    ViewportPaneId id = ViewportPaneId::Perspective;
    ViewportView view = ViewportView::Perspective;
    ViewportShadingMode shading = ViewportShadingMode::Shaded;
    ViewportCameraController camera;
    Vec2 renderSize = {1.0f, 1.0f};
    bool visibleThisFrame = false;
};

class ViewportWorkspaceService final {
public:
    ViewportWorkspaceService();

    [[nodiscard]] ViewportLayoutId layout() const { return layout_; }
    bool setLayout(ViewportLayoutId layout);

    [[nodiscard]] ViewportPaneId focusedPane() const { return focusedPane_; }
    bool focusPane(ViewportPaneId pane);

    [[nodiscard]] int gridPreset() const { return gridPreset_; }
    bool setGridPreset(int preset);

    [[nodiscard]] ViewportShadingMode shadingMode(ViewportPaneId pane) const;
    bool setShadingMode(ViewportPaneId pane, ViewportShadingMode mode);

    [[nodiscard]] ViewportView view(ViewportPaneId pane) const;
    bool setView(ViewportPaneId pane, ViewportView view);
    void syncViewFromCamera(ViewportPaneId pane);

    [[nodiscard]] ViewportCameraController& camera(ViewportPaneId pane);
    [[nodiscard]] const ViewportCameraController& camera(ViewportPaneId pane) const;
    bool focusOrigin(ViewportPaneId pane, float distance = 64.0f);

    [[nodiscard]] ViewportWorkspacePaneState& pane(ViewportPaneId pane);
    [[nodiscard]] const ViewportWorkspacePaneState& pane(ViewportPaneId pane) const;

    [[nodiscard]] std::vector<ViewportPanePlacement> activePanePlacements(bool maximized) const;
    [[nodiscard]] std::vector<ViewportSplitHandle> activeSplitHandles(bool maximized) const;
    void setSplitHandlePosition(ViewportSplitHandleId handle, float normalizedPosition);

    void clearPaneVisibility();
    void setPaneVisible(ViewportPaneId pane, bool visible);
    [[nodiscard]] bool paneVisible(ViewportPaneId pane) const;
    bool setPaneRenderSize(ViewportPaneId pane, Vec2 size);
    [[nodiscard]] Vec2 paneRenderSize(ViewportPaneId pane) const;

private:
    [[nodiscard]] bool focusedPaneIsVisible(bool maximized) const;

    std::array<ViewportWorkspacePaneState, kViewportPaneCount> panes_;
    ViewportLayoutState layoutState_;
    ViewportLayoutId layout_ = ViewportLayoutId::Single;
    ViewportPaneId focusedPane_ = ViewportPaneId::Perspective;
    int gridPreset_ = 6;
};

} // namespace quader
