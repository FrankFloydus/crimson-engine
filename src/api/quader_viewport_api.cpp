////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_viewport_api.hpp"

#include "core/release_features.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "viewport/viewport_shading_mode_conversions.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <memory>
#include <utility>

namespace quader {
/**
 * Owns QuaderViewportApi implementation state that should not leak through public headers.
 */
class QuaderViewportApi::Impl final {
public:
    ViewportWorkspaceService owned_workspace{
        ViewportWorkspaceLayoutPolicy::MultiPane};
    ViewportWorkspaceService *workspace = &owned_workspace;
    ViewportLocalVisibilityService *local_visibility = nullptr;
    QuaderViewportRuntimeHooks hooks;
};

ViewportWorkspaceService& QuaderViewportApiRuntimeAccess::workspace(QuaderViewportApi& api) {
    return *api.impl_->workspace;
}

const ViewportWorkspaceService& QuaderViewportApiRuntimeAccess::workspace(const QuaderViewportApi& api) {
    return *api.impl_->workspace;
}

void QuaderViewportApiRuntimeAccess::bind_scene_workspace(
    QuaderViewportApi &api, ViewportWorkspaceService &next) {
  api.impl_->workspace = &next;
}

void QuaderViewportApiRuntimeAccess::bind_scene_local_visibility(
    QuaderViewportApi &api, ViewportLocalVisibilityService &service) {
  api.impl_->local_visibility = &service;
}

QuaderViewportApi::QuaderViewportApi()
    : impl_(std::make_unique<Impl>()) {}

QuaderViewportApi::~QuaderViewportApi() = default;

QuaderViewportApi::QuaderViewportApi(QuaderViewportApi&&) noexcept = default;
QuaderViewportApi& QuaderViewportApi::operator=(QuaderViewportApi&&) noexcept = default;

void QuaderViewportApi::set_runtime_hooks(QuaderViewportRuntimeHooks hooks) {
  impl_->hooks = std::move(hooks);
}

ViewportLayoutId QuaderViewportApi::layout() const {
    return impl_->workspace->layout();
}

QuaderApiVoidResult QuaderViewportApi::set_layout(ViewportLayoutId layout) {
  if (!impl_->workspace->supports_layout(layout)) {
    return unavailable("The active viewport workspace policy does not support "
                       "the requested layout.");
  }
  return runtime_status(impl_->workspace->set_layout(layout));
}

std::size_t QuaderViewportApi::active_pane_count() const {
  return impl_->workspace->active_pane_placements().size();
}

ViewportPaneId QuaderViewportApi::focused_pane() const {
  return impl_->workspace->focused_pane();
}

int QuaderViewportApi::grid_preset() const {
  return impl_->workspace->grid_preset();
}

QuaderApiVoidResult QuaderViewportApi::set_grid_preset(int preset) {
  return runtime_status(impl_->workspace->set_grid_preset(preset));
}

QuaderTransformSpace QuaderViewportApi::transform_space() const {
  switch (impl_->workspace->transform_space()) {
  case ViewportTransformSpace::Global:
    return QuaderTransformSpace::Global;
  case ViewportTransformSpace::Local:
    return QuaderTransformSpace::Local;
  }
  return QuaderTransformSpace::Global;
}

QuaderApiVoidResult
QuaderViewportApi::set_transform_space(QuaderTransformSpace space) {
  const ViewportTransformSpace viewport_space =
      space == QuaderTransformSpace::Local ? ViewportTransformSpace::Local
                                           : ViewportTransformSpace::Global;
  return runtime_status(impl_->workspace->set_transform_space(viewport_space));
}

QuaderApiVoidResult QuaderViewportApi::toggle_transform_space() {
  return runtime_status(impl_->workspace->toggle_transform_space());
}

std::uint64_t QuaderViewportApi::transform_space_revision() const {
  return impl_->workspace->transform_space_revision();
}

QuaderShadingMode QuaderViewportApi::shading_mode(ViewportPaneId pane) const {
  return quader_api_from_viewport_shading_mode(
      impl_->workspace->shading_mode(pane));
}

QuaderApiVoidResult
QuaderViewportApi::set_shading_mode(ViewportPaneId pane,
                                    QuaderShadingMode mode) {
  if (!release_has_feature(ReleaseFeature::RenderedViewport) &&
      mode != QuaderShadingMode::Wireframe &&
      mode != QuaderShadingMode::Solid && mode != QuaderShadingMode::Shaded) {
    return unavailable(release_feature_unavailable_message());
  }
  if (mode == QuaderShadingMode::XRay) {
    return unavailable(
        "XRay shading mode is not supported by the native viewport.");
  }
  return runtime_status(impl_->workspace->set_shading_mode(
      pane, quader_api_to_viewport_shading_mode(mode)));
}

ViewportView QuaderViewportApi::view(ViewportPaneId pane) const {
    return impl_->workspace->view(pane);
}

QuaderApiVoidResult QuaderViewportApi::set_view(ViewportPaneId pane,
                                                ViewportView view) {
  return runtime_status(impl_->workspace->set_view(pane, view));
}

bool QuaderViewportApi::local_visibility_active() const {
  return impl_->local_visibility != nullptr
             ? impl_->local_visibility->active()
             : impl_->workspace->local_visibility_active();
}

bool QuaderViewportApi::can_enter_local_visibility() const {
  return impl_->local_visibility != nullptr &&
         impl_->local_visibility->can_enter();
}

bool QuaderViewportApi::can_toggle_local_visibility() const {
  return impl_->local_visibility != nullptr &&
         impl_->local_visibility->can_toggle();
}

QuaderApiVoidResult QuaderViewportApi::toggle_local_visibility() {
  if (impl_->local_visibility == nullptr) {
    return unavailable("Viewport Local Visibility runtime is not attached.");
  }
  ViewportLocalVisibilityMutation result =
      impl_->local_visibility->toggle();
  if (!result.ok) {
    return unavailable(std::move(result.message));
  }
  return runtime_status(result.changed, std::move(result.message));
}

QuaderApiVoidResult QuaderViewportApi::frame_all(ViewportPaneId pane) {
  if (impl_->local_visibility != nullptr) {
    (void)impl_->local_visibility->snapshot();
  }
  return impl_->hooks.frame_all
             ? impl_->hooks.frame_all(pane)
             : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::frame_selection(ViewportPaneId pane) {
  return impl_->hooks.frame_selection
             ? impl_->hooks.frame_selection(pane)
             : unavailable("Viewport camera runtime is not attached.");
}

QuaderApiVoidResult QuaderViewportApi::focus_origin(ViewportPaneId pane) {
  if (impl_->hooks.focus_origin) {
    return impl_->hooks.focus_origin(pane);
  }
  return runtime_status(impl_->workspace->focus_origin(pane));
}

bool QuaderViewportApi::can_spawn_window() const {
  return static_cast<bool>(impl_->hooks.spawn_window);
}

QuaderApiVoidResult QuaderViewportApi::spawn_window() {
  return impl_->hooks.spawn_window
             ? impl_->hooks.spawn_window()
             : unavailable("Viewport window runtime is not attached.");
}

QuaderApiVoidResult
QuaderViewportApi::capture_screenshot(const std::filesystem::path &path,
                                      ViewportPaneId pane) {
  return impl_->hooks.capture_screenshot
             ? impl_->hooks.capture_screenshot(pane, path)
             : unavailable("Viewport screenshot runtime is not attached.");
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
      .mutation_kind = QuaderMutationKind::None,
  };
}

QuaderApiVoidResult
QuaderViewportApi::runtime_status(bool changed, std::string message) const {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .mutation_kind =
          changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
  };
}

QuaderCameraHandle::QuaderCameraHandle(QuaderViewportApi& viewports, ViewportPaneId pane)
    : viewports_(viewports)
    , pane_(pane) {}

QuaderApiVoidResult QuaderCameraHandle::frame_all() {
  return viewports_.frame_all(pane_);
}

QuaderApiVoidResult QuaderCameraHandle::frame_selection() {
  return viewports_.frame_selection(pane_);
}

QuaderApiVoidResult QuaderCameraHandle::focus_origin() {
  return viewports_.focus_origin(pane_);
}

QuaderViewportHandle::QuaderViewportHandle(QuaderViewportApi& viewports, ViewportPaneId pane)
    : viewports_(viewports)
    , pane_(pane) {}

QuaderApiVoidResult
QuaderViewportHandle::set_shading_mode(QuaderShadingMode mode) {
  return viewports_.set_shading_mode(pane_, mode);
}

QuaderApiVoidResult QuaderViewportHandle::set_view(ViewportView view) {
  return viewports_.set_view(pane_, view);
}

QuaderApiVoidResult
QuaderViewportHandle::capture_screenshot(const std::filesystem::path &path) {
  return viewports_.capture_screenshot(path, pane_);
}

QuaderCameraHandle QuaderViewportHandle::camera() {
    return QuaderCameraHandle(viewports_, pane_);
}

} // namespace quader
