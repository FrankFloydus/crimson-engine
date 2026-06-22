////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderEditorApiTypes.h"
#include "viewport/ViewportWorkspaceService.h"

namespace quader {

class QuaderCameraHandle;
class QuaderViewportHandle;

/**
 * Exposes Quader Viewport API operations through the public Quader API surface.
 */
class QuaderViewportApi final {
public:
    explicit QuaderViewportApi(ViewportWorkspaceService& workspace);

    void setRuntimeHooks(QuaderViewportRuntimeHooks hooks);
    [[nodiscard]] ViewportWorkspaceService& workspace();
    [[nodiscard]] const ViewportWorkspaceService& workspace() const;

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
    [[nodiscard]] QuaderApiVoidResult runtimeStatus(bool changed, std::string message = {}) const;

    ViewportWorkspaceService& workspace_;
    QuaderViewportRuntimeHooks hooks_;
};

/**
 * Owns Quader Camera Handle behavior within the public Quader API surface.
 */
class QuaderCameraHandle final {
public:
    QuaderCameraHandle(QuaderViewportApi& viewports, ViewportPaneId pane);

    QuaderApiVoidResult frameSelection();
    QuaderApiVoidResult focusOrigin();

private:
    QuaderViewportApi& viewports_;
    ViewportPaneId pane_;
};

/**
 * Owns Quader Viewport Handle behavior within the public Quader API surface.
 */
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
