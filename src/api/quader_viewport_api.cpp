////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_viewport_api.hpp"

#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <memory>
#include <utility>

namespace quader {
/**
 * Owns QuaderViewportApi implementation state that should not leak through public headers.
 */
class QuaderViewportApi::Impl final {
public:
    ViewportWorkspaceService workspace{ViewportWorkspaceLayoutPolicy::SinglePaneOnly};
    QuaderViewportRuntimeHooks hooks;
};

ViewportWorkspaceService& QuaderViewportApiRuntimeAccess::workspace(QuaderViewportApi& api) {
    return api.impl_->workspace;
}

const ViewportWorkspaceService& QuaderViewportApiRuntimeAccess::workspace(const QuaderViewportApi& api) {
    return api.impl_->workspace;
}

namespace {

ViewportShadingMode toViewportShadingMode(QuaderShadingMode mode) {
    switch (mode) {
    case QuaderShadingMode::Wireframe:
        return ViewportShadingMode::Wireframe;
    case QuaderShadingMode::Material:
        return ViewportShadingMode::Rendered;
    case QuaderShadingMode::Solid:
        return ViewportShadingMode::Shaded;
    case QuaderShadingMode::XRay:
        break;
    }
    return ViewportShadingMode::Shaded;
}

QuaderShadingMode toQuaderShadingMode(ViewportShadingMode mode) {
    switch (mode) {
    case ViewportShadingMode::Wireframe:
        return QuaderShadingMode::Wireframe;
    case ViewportShadingMode::Rendered:
        return QuaderShadingMode::Material;
    case ViewportShadingMode::Shaded:
        return QuaderShadingMode::Solid;
    }
    return QuaderShadingMode::Solid;
}

} // namespace

QuaderViewportApi::QuaderViewportApi()
    : impl_(std::make_unique<Impl>()) {}

QuaderViewportApi::~QuaderViewportApi() = default;

QuaderViewportApi::QuaderViewportApi(QuaderViewportApi&&) noexcept = default;
QuaderViewportApi& QuaderViewportApi::operator=(QuaderViewportApi&&) noexcept = default;

void QuaderViewportApi::setRuntimeHooks(QuaderViewportRuntimeHooks hooks) {
    impl_->hooks = std::move(hooks);
}

ViewportLayoutId QuaderViewportApi::layout() const {
    return impl_->workspace.layout();
}

QuaderApiVoidResult QuaderViewportApi::setLayout(ViewportLayoutId layout) {
    if (!impl_->workspace.supportsLayout(layout)) {
        return unavailable("The embedded viewport host currently supports only the single-pane layout.");
    }
    return runtimeStatus(impl_->workspace.setLayout(layout));
}

std::size_t QuaderViewportApi::activePaneCount() const {
    return impl_->workspace.activePanePlacements().size();
}

int QuaderViewportApi::gridPreset() const {
    return impl_->workspace.gridPreset();
}

QuaderApiVoidResult QuaderViewportApi::setGridPreset(int preset) {
    return runtimeStatus(impl_->workspace.setGridPreset(preset));
}

QuaderShadingMode QuaderViewportApi::shadingMode(ViewportPaneId pane) const {
    return toQuaderShadingMode(impl_->workspace.shadingMode(pane));
}

QuaderApiVoidResult QuaderViewportApi::setShadingMode(ViewportPaneId pane, QuaderShadingMode mode) {
    if (mode == QuaderShadingMode::XRay) {
        return unavailable("XRay shading mode is not supported by the native viewport.");
    }
    return runtimeStatus(impl_->workspace.setShadingMode(pane, toViewportShadingMode(mode)));
}

ViewportView QuaderViewportApi::view(ViewportPaneId pane) const {
    return impl_->workspace.view(pane);
}

QuaderApiVoidResult QuaderViewportApi::setView(ViewportPaneId pane, ViewportView view) {
    return runtimeStatus(impl_->workspace.setView(pane, view));
}

QuaderApiVoidResult QuaderViewportApi::frameSelection(ViewportPaneId pane) {
    return impl_->hooks.frameSelection ? impl_->hooks.frameSelection(pane) : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::focusOrigin(ViewportPaneId pane) {
    if (impl_->hooks.focusOrigin) {
        return impl_->hooks.focusOrigin(pane);
    }
    return runtimeStatus(impl_->workspace.focusOrigin(pane));
}

QuaderApiVoidResult QuaderViewportApi::spawnWindow() {
    return impl_->hooks.spawnWindow ? impl_->hooks.spawnWindow() : unavailable("Viewport window runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::captureScreenshot(const std::filesystem::path& path, ViewportPaneId pane) {
    return impl_->hooks.captureScreenshot ? impl_->hooks.captureScreenshot(pane, path) : unavailable("Viewport screenshot runtime is not attached.");
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

QuaderApiVoidResult QuaderViewportApi::runtimeStatus(bool changed, std::string message) const {
    return {
            .ok = true,
            .changed = changed,
            .message = std::move(message),
            .mutationKind = changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
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
