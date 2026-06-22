#include "api/QuaderViewportApi.h"

#include <utility>

namespace quader {

QuaderViewportApi::QuaderViewportApi() = default;

void QuaderViewportApi::setRuntimeHooks(QuaderViewportRuntimeHooks hooks) {
    hooks_ = std::move(hooks);
}

ViewportLayoutId QuaderViewportApi::layout() const {
    return hooks_.layout ? hooks_.layout() : ViewportLayoutId::Single;
}

QuaderApiVoidResult QuaderViewportApi::setLayout(ViewportLayoutId layout) {
    return hooks_.setLayout ? hooks_.setLayout(layout) : unavailable("Viewport layout runtime is not attached.");
}

int QuaderViewportApi::gridPreset() const {
    return hooks_.gridPreset ? hooks_.gridPreset() : 0;
}

QuaderApiVoidResult QuaderViewportApi::setGridPreset(int preset) {
    return hooks_.setGridPreset ? hooks_.setGridPreset(preset) : unavailable("Viewport grid runtime is not attached.");
}

QuaderShadingMode QuaderViewportApi::shadingMode(ViewportPaneId pane) const {
    return hooks_.shadingMode ? hooks_.shadingMode(pane) : QuaderShadingMode::Solid;
}

QuaderApiVoidResult QuaderViewportApi::setShadingMode(ViewportPaneId pane, QuaderShadingMode mode) {
    return hooks_.setShadingMode ? hooks_.setShadingMode(pane, mode) : unavailable("Viewport shading runtime is not attached.");
}

ViewportView QuaderViewportApi::view(ViewportPaneId pane) const {
    return hooks_.view ? hooks_.view(pane) : viewportViewForPane(pane);
}

QuaderApiVoidResult QuaderViewportApi::setView(ViewportPaneId pane, ViewportView view) {
    return hooks_.setView ? hooks_.setView(pane, view) : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::frameSelection(ViewportPaneId pane) {
    return hooks_.frameSelection ? hooks_.frameSelection(pane) : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::focusOrigin(ViewportPaneId pane) {
    return hooks_.focusOrigin ? hooks_.focusOrigin(pane) : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::captureScreenshot(const std::filesystem::path& path, ViewportPaneId pane) {
    return hooks_.captureScreenshot ? hooks_.captureScreenshot(pane, path) : unavailable("Viewport screenshot runtime is not attached.");
}

QuaderViewportHandle QuaderViewportApi::main() {
    return QuaderViewportHandle(*this, ViewportPaneId::Perspective);
}

QuaderViewportHandle QuaderViewportApi::pane(ViewportPaneId pane) {
    return QuaderViewportHandle(*this, pane);
}

QuaderApiVoidResult QuaderViewportApi::unavailable(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

QuaderCameraHandle::QuaderCameraHandle(QuaderViewportApi& viewports, ViewportPaneId pane)
    : viewports_(viewports)
    , pane_(pane) {}

QuaderApiVoidResult QuaderCameraHandle::frameSelection() {
    return viewports_.frameSelection(pane_);
}

QuaderApiVoidResult QuaderCameraHandle::focusOrigin() {
    return viewports_.focusOrigin(pane_);
}

QuaderViewportHandle::QuaderViewportHandle(QuaderViewportApi& viewports, ViewportPaneId pane)
    : viewports_(viewports)
    , pane_(pane) {}

QuaderApiVoidResult QuaderViewportHandle::setShadingMode(QuaderShadingMode mode) {
    return viewports_.setShadingMode(pane_, mode);
}

QuaderApiVoidResult QuaderViewportHandle::setView(ViewportView view) {
    return viewports_.setView(pane_, view);
}

QuaderApiVoidResult QuaderViewportHandle::captureScreenshot(const std::filesystem::path& path) {
    return viewports_.captureScreenshot(path, pane_);
}

QuaderCameraHandle QuaderViewportHandle::camera() {
    return QuaderCameraHandle(viewports_, pane_);
}

} // namespace quader
