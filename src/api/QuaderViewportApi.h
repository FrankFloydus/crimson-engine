#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class QuaderCameraHandle;
class QuaderViewportHandle;

class QuaderViewportApi final {
public:
    QuaderViewportApi();

    void setRuntimeHooks(QuaderViewportRuntimeHooks hooks);

    [[nodiscard]] ViewportLayoutId layout() const;
    QuaderApiVoidResult setLayout(ViewportLayoutId layout);

    [[nodiscard]] int gridPreset() const;
    QuaderApiVoidResult setGridPreset(int preset);

    [[nodiscard]] QuaderShadingMode shadingMode(ViewportPaneId pane) const;
    QuaderApiVoidResult setShadingMode(ViewportPaneId pane, QuaderShadingMode mode);

    [[nodiscard]] ViewportView view(ViewportPaneId pane) const;
    QuaderApiVoidResult setView(ViewportPaneId pane, ViewportView view);

    QuaderApiVoidResult frameSelection(ViewportPaneId pane = ViewportPaneId::Perspective);
    QuaderApiVoidResult focusOrigin(ViewportPaneId pane = ViewportPaneId::Perspective);
    QuaderApiVoidResult captureScreenshot(
            const std::filesystem::path& path,
            ViewportPaneId pane = ViewportPaneId::Perspective);
    [[nodiscard]] QuaderViewportHandle main();
    [[nodiscard]] QuaderViewportHandle pane(ViewportPaneId pane);

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderViewportRuntimeHooks hooks_;
};

class QuaderCameraHandle final {
public:
    QuaderCameraHandle(QuaderViewportApi& viewports, ViewportPaneId pane);

    QuaderApiVoidResult frameSelection();
    QuaderApiVoidResult focusOrigin();

private:
    QuaderViewportApi& viewports_;
    ViewportPaneId pane_;
};

class QuaderViewportHandle final {
public:
    QuaderViewportHandle(QuaderViewportApi& viewports, ViewportPaneId pane);

    QuaderApiVoidResult setShadingMode(QuaderShadingMode mode);
    QuaderApiVoidResult setView(ViewportView view);
    QuaderApiVoidResult captureScreenshot(const std::filesystem::path& path);
    [[nodiscard]] QuaderCameraHandle camera();

private:
    QuaderViewportApi& viewports_;
    ViewportPaneId pane_;
};

} // namespace quader
