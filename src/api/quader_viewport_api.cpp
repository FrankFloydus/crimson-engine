////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_viewport_api.hpp"

#include "core/release_features.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "viewport/viewport_navigation_defaults.hpp"
#include "viewport/viewport_shading_mode_conversions.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <algorithm>
#include <memory>
#include <cmath>
#include <utility>

namespace quader {
namespace {

bool valid_pane(ViewportPaneId pane) {
  return static_cast<std::size_t>(pane) < kViewportPaneCount;
}

bool valid_control(QuaderViewportControl control) {
  return static_cast<std::size_t>(control) < kQuaderViewportControlCount;
}

} // namespace

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

QuaderApiVoidResult QuaderViewportApi::focus_pane(ViewportPaneId pane) {
  if (!valid_pane(pane)) {
    return unavailable("The requested viewport pane is not valid.");
  }
  return runtime_status(impl_->workspace->focus_pane(pane));
}

ViewportToolId QuaderViewportApi::selected_tool() const {
  return selected_tool(focused_pane());
}

ViewportToolId QuaderViewportApi::selected_tool(ViewportPaneId pane) const {
  return impl_->workspace->selected_tool(pane);
}

ViewportToolId QuaderViewportApi::preferred_selection_tool() const {
  return preferred_selection_tool(focused_pane());
}

ViewportToolId
QuaderViewportApi::preferred_selection_tool(ViewportPaneId pane) const {
  return impl_->workspace->preferred_selection_tool(pane);
}

QuaderApiVoidResult QuaderViewportApi::record_selected_tool(
    ViewportPaneId pane, ViewportToolId tool) {
  if (!valid_pane(pane)) {
    return unavailable("The requested viewport pane is not valid.");
  }
  if (!viewport_tool_available(tool)) {
    return unavailable("The requested viewport tool is unavailable.");
  }
  return runtime_status(impl_->workspace->record_selected_tool(pane, tool));
}

bool QuaderViewportApi::control(QuaderViewportControl control_id) const {
  return control(focused_pane(), control_id);
}

bool QuaderViewportApi::control(ViewportPaneId pane,
                                QuaderViewportControl control_id) const {
  return impl_->workspace->control(pane, control_id);
}

bool QuaderViewportApi::control_overridden(
    ViewportPaneId pane, QuaderViewportControl control_id) const {
  return valid_pane(pane) && valid_control(control_id) &&
         impl_->workspace->pane(pane)
             .control_overrides[static_cast<std::size_t>(control_id)]
             .has_value();
}

QuaderApiVoidResult QuaderViewportApi::set_control(
    QuaderViewportControl control_id, bool enabled) {
  return set_control(focused_pane(), control_id, enabled);
}

QuaderApiVoidResult QuaderViewportApi::set_control(
    ViewportPaneId pane, QuaderViewportControl control_id, bool enabled) {
  if (!valid_pane(pane) || !valid_control(control_id)) {
    return unavailable("The requested viewport pane or control is not valid.");
  }
  if (control_id == QuaderViewportControl::ShowMeshGrid &&
      !release_has_feature(ReleaseFeature::MeshGrid)) {
    return unavailable(release_feature_unavailable_message());
  }
  return runtime_status(impl_->workspace->set_control(pane, control_id, enabled));
}

double QuaderViewportApi::rotation_snap_degrees() const {
  return rotation_snap_degrees(focused_pane());
}

double QuaderViewportApi::rotation_snap_degrees(ViewportPaneId pane) const {
  return impl_->workspace->rotation_snap_degrees(pane);
}

bool QuaderViewportApi::rotation_snap_overridden(ViewportPaneId pane) const {
  return valid_pane(pane) &&
         impl_->workspace->pane(pane).rotation_snap_override.has_value();
}

QuaderApiVoidResult QuaderViewportApi::set_rotation_snap_degrees(double degrees) {
  return set_rotation_snap_degrees(focused_pane(), degrees);
}

QuaderApiVoidResult QuaderViewportApi::set_rotation_snap_degrees(
    ViewportPaneId pane, double degrees) {
  if (!valid_pane(pane) || !std::isfinite(degrees) || degrees <= 0.0) {
    return unavailable("The rotation snap angle must be finite and positive.");
  }
  return runtime_status(impl_->workspace->set_rotation_snap_degrees(pane, degrees));
}

double QuaderViewportApi::fly_navigation_speed() const {
  return fly_navigation_speed(focused_pane());
}

double QuaderViewportApi::fly_navigation_speed(ViewportPaneId pane) const {
  return impl_->workspace->camera(pane).fly_speed();
}

QuaderApiVoidResult QuaderViewportApi::set_fly_navigation_speed(double speed) {
  return set_fly_navigation_speed(focused_pane(), speed);
}

QuaderApiVoidResult QuaderViewportApi::set_fly_navigation_speed(
    ViewportPaneId pane, double speed) {
  if (!valid_pane(pane)) {
    return unavailable("The requested viewport pane is not valid.");
  }
  if (!std::isfinite(speed) || speed <= 0.0) {
    return unavailable("The fly navigation speed must be finite and positive.");
  }
  const double clamped = std::clamp(
      speed, static_cast<double>(kMinimumViewportFlyNavigationSpeed),
      static_cast<double>(kMaximumViewportFlyNavigationSpeed));
  return runtime_status(impl_->workspace->camera(pane).set_fly_speed(
      static_cast<float>(clamped)));
}

std::uint64_t QuaderViewportApi::control_revision() const {
  return control_revision(focused_pane());
}

std::uint64_t QuaderViewportApi::control_revision(ViewportPaneId pane) const {
  return impl_->workspace->control_revision(pane);
}

int QuaderViewportApi::grid_preset() const {
  return impl_->workspace->grid_preset();
}

QuaderApiVoidResult QuaderViewportApi::set_grid_preset(int preset) {
  return runtime_status(impl_->workspace->set_grid_preset(preset));
}

QuaderTransformSpace QuaderViewportApi::transform_space() const {
  return transform_space(focused_pane());
}

QuaderTransformSpace
QuaderViewportApi::transform_space(ViewportPaneId pane) const {
  switch (impl_->workspace->transform_space(pane)) {
  case ViewportTransformSpace::Global:
    return QuaderTransformSpace::Global;
  case ViewportTransformSpace::Local:
    return QuaderTransformSpace::Local;
  }
  return QuaderTransformSpace::Global;
}

QuaderApiVoidResult
QuaderViewportApi::set_transform_space(QuaderTransformSpace space) {
  return set_transform_space(focused_pane(), space);
}

QuaderApiVoidResult QuaderViewportApi::set_transform_space(
    ViewportPaneId pane, QuaderTransformSpace space) {
  if (!valid_pane(pane) ||
      (space != QuaderTransformSpace::Global &&
       space != QuaderTransformSpace::Local)) {
    return unavailable("The requested viewport pane or transform space is not valid.");
  }
  const ViewportTransformSpace viewport_space =
      space == QuaderTransformSpace::Local ? ViewportTransformSpace::Local
                                           : ViewportTransformSpace::Global;
  return runtime_status(impl_->workspace->set_transform_space(pane, viewport_space));
}

QuaderApiVoidResult QuaderViewportApi::toggle_transform_space() {
  return toggle_transform_space(focused_pane());
}

QuaderApiVoidResult
QuaderViewportApi::toggle_transform_space(ViewportPaneId pane) {
  if (!valid_pane(pane)) {
    return unavailable("The requested viewport pane is not valid.");
  }
  return runtime_status(impl_->workspace->toggle_transform_space(pane));
}

std::uint64_t QuaderViewportApi::transform_space_revision() const {
  return impl_->workspace->transform_space_revision();
}

std::uint64_t
QuaderViewportApi::transform_space_revision(ViewportPaneId pane) const {
  return impl_->workspace->transform_space_revision(pane);
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

QuaderApiVoidResult QuaderViewportHandle::focus() {
  return viewports_.focus_pane(pane_);
}

ViewportToolId QuaderViewportHandle::selected_tool() const {
  return viewports_.selected_tool(pane_);
}

ViewportToolId QuaderViewportHandle::preferred_selection_tool() const {
  return viewports_.preferred_selection_tool(pane_);
}

bool QuaderViewportHandle::control(QuaderViewportControl control_id) const {
  return viewports_.control(pane_, control_id);
}

QuaderApiVoidResult QuaderViewportHandle::set_control(
    QuaderViewportControl control_id, bool enabled) {
  return viewports_.set_control(pane_, control_id, enabled);
}

double QuaderViewportHandle::rotation_snap_degrees() const {
  return viewports_.rotation_snap_degrees(pane_);
}

QuaderApiVoidResult
QuaderViewportHandle::set_rotation_snap_degrees(double degrees) {
  return viewports_.set_rotation_snap_degrees(pane_, degrees);
}

double QuaderViewportHandle::fly_navigation_speed() const {
  return viewports_.fly_navigation_speed(pane_);
}

QuaderApiVoidResult QuaderViewportHandle::set_fly_navigation_speed(double speed) {
  return viewports_.set_fly_navigation_speed(pane_, speed);
}

QuaderTransformSpace QuaderViewportHandle::transform_space() const {
  return viewports_.transform_space(pane_);
}

QuaderApiVoidResult
QuaderViewportHandle::set_transform_space(QuaderTransformSpace space) {
  return viewports_.set_transform_space(pane_, space);
}

std::uint64_t QuaderViewportHandle::control_revision() const {
  return viewports_.control_revision(pane_);
}

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
