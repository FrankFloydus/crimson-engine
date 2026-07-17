////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_editor_api_types.hpp"

#include <cstddef>
#include <memory>

namespace quader {

class QuaderCameraHandle;
class QuaderViewportApiRuntimeAccess;
class QuaderViewportHandle;

/**
 * Exposes Quader Viewport API operations through the public Quader API surface.
 */
class QuaderViewportApi final {
public:
    QuaderViewportApi();
    ~QuaderViewportApi();

    QuaderViewportApi(const QuaderViewportApi&) = delete;
    QuaderViewportApi& operator=(const QuaderViewportApi&) = delete;
    QuaderViewportApi(QuaderViewportApi&&) noexcept;
    QuaderViewportApi& operator=(QuaderViewportApi&&) noexcept;

    void set_runtime_hooks(QuaderViewportRuntimeHooks hooks);

    [[nodiscard]] ViewportLayoutId layout() const;
    QuaderApiVoidResult set_layout(ViewportLayoutId layout);
    [[nodiscard]] std::size_t active_pane_count() const;
    [[nodiscard]] ViewportPaneId focused_pane() const;

    [[nodiscard]] int grid_preset() const;
    QuaderApiVoidResult set_grid_preset(int preset);

    [[nodiscard]] QuaderShadingMode shading_mode(ViewportPaneId pane) const;
    QuaderApiVoidResult set_shading_mode(ViewportPaneId pane,
                                         QuaderShadingMode mode);

    [[nodiscard]] ViewportView view(ViewportPaneId pane) const;
    QuaderApiVoidResult set_view(ViewportPaneId pane, ViewportView view);

    [[nodiscard]] bool local_visibility_active() const;
    [[nodiscard]] bool can_enter_local_visibility() const;
    [[nodiscard]] bool can_toggle_local_visibility() const;
    QuaderApiVoidResult toggle_local_visibility();

    QuaderApiVoidResult
    frame_all(ViewportPaneId pane = ViewportPaneId::Perspective);
    QuaderApiVoidResult
    frame_selection(ViewportPaneId pane = ViewportPaneId::Perspective);
    QuaderApiVoidResult
    focus_origin(ViewportPaneId pane = ViewportPaneId::Perspective);
    [[nodiscard]] bool can_spawn_window() const;
    QuaderApiVoidResult spawn_window();
    QuaderApiVoidResult
    capture_screenshot(const std::filesystem::path &path,
                       ViewportPaneId pane = ViewportPaneId::Perspective);
    [[nodiscard]] QuaderViewportHandle main();
    [[nodiscard]] QuaderViewportHandle pane(ViewportPaneId pane);

private:
    friend class QuaderEditorApi;
    friend class QuaderViewportApiRuntimeAccess;

    class Impl;

    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;
    [[nodiscard]] QuaderApiVoidResult
    runtime_status(bool changed, std::string message = {}) const;

    std::unique_ptr<Impl> impl_;
};

/**
 * Owns Quader Camera Handle behavior within the public Quader API surface.
 */
class QuaderCameraHandle final {
public:
    QuaderCameraHandle(QuaderViewportApi& viewports, ViewportPaneId pane);

    QuaderApiVoidResult frame_all();
    QuaderApiVoidResult frame_selection();
    QuaderApiVoidResult focus_origin();

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

    QuaderApiVoidResult set_shading_mode(QuaderShadingMode mode);
    QuaderApiVoidResult set_view(ViewportView view);
    QuaderApiVoidResult capture_screenshot(const std::filesystem::path &path);
    [[nodiscard]] QuaderCameraHandle camera();

private:
    QuaderViewportApi& viewports_;
    ViewportPaneId pane_;
};

} // namespace quader
