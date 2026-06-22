////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderViewportApi.h"

#include <utility>

namespace quader {
namespace {

ViewportShadingMode toViewportShadingMode(QuaderShadingMode mode) {
    switch (mode) {
    case QuaderShadingMode::Wireframe:
        return ViewportShadingMode::Wireframe;
    case QuaderShadingMode::Material:
        return ViewportShadingMode::Rendered;
    case QuaderShadingMode::Solid:
    case QuaderShadingMode::XRay:
        return ViewportShadingMode::Shaded;
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

QuaderViewportApi::QuaderViewportApi(ViewportWorkspaceService& workspace)
    : workspace_(workspace) {}

void QuaderViewportApi::setRuntimeHooks(QuaderViewportRuntimeHooks hooks) {
    hooks_ = std::move(hooks);
}

ViewportWorkspaceService& QuaderViewportApi::workspace() {
    return workspace_;
}

const ViewportWorkspaceService& QuaderViewportApi::workspace() const {
    return workspace_;
}

ViewportLayoutId QuaderViewportApi::layout() const {
    return workspace_.layout();
}

QuaderApiVoidResult QuaderViewportApi::setLayout(ViewportLayoutId layout) {
    return runtimeStatus(workspace_.setLayout(layout));
}

int QuaderViewportApi::gridPreset() const {
    return workspace_.gridPreset();
}

QuaderApiVoidResult QuaderViewportApi::setGridPreset(int preset) {
    return runtimeStatus(workspace_.setGridPreset(preset));
}

QuaderShadingMode QuaderViewportApi::shadingMode(ViewportPaneId pane) const {
    return toQuaderShadingMode(workspace_.shadingMode(pane));
}

QuaderApiVoidResult QuaderViewportApi::setShadingMode(ViewportPaneId pane, QuaderShadingMode mode) {
    return runtimeStatus(workspace_.setShadingMode(pane, toViewportShadingMode(mode)));
}

ViewportView QuaderViewportApi::view(ViewportPaneId pane) const {
    return workspace_.view(pane);
}

QuaderApiVoidResult QuaderViewportApi::setView(ViewportPaneId pane, ViewportView view) {
    return runtimeStatus(workspace_.setView(pane, view));
}

QuaderApiVoidResult QuaderViewportApi::frameSelection(ViewportPaneId pane) {
    return hooks_.frameSelection ? hooks_.frameSelection(pane) : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::focusOrigin(ViewportPaneId pane) {
    if (hooks_.focusOrigin) {
        return hooks_.focusOrigin(pane);
    }
    return runtimeStatus(workspace_.focusOrigin(pane));
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
