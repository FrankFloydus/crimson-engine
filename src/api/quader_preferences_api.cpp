////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_preferences_api.hpp"

#include "api/quader_commands_api.hpp"

#include <cmath>
#include <utility>

namespace quader {

QuaderPreferencesApi::QuaderPreferencesApi() = default;

QuaderPreferencesApi::QuaderPreferencesApi(QuaderCommandsApi& commands)
    : commands_(&commands) {}

void QuaderPreferencesApi::attach_commands(QuaderCommandsApi &commands) {
  commands_ = &commands;
}

void QuaderPreferencesApi::set_runtime_hooks(
    QuaderPreferencesRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

std::string QuaderPreferencesApi::locale() const {
    return hooks_.locale ? hooks_.locale() : std::string{};
}

QuaderApiVoidResult QuaderPreferencesApi::set_locale(std::string locale) {
  return hooks_.set_locale
             ? hooks_.set_locale(std::move(locale))
             : unavailable("Preferences runtime is not attached.");
}

double QuaderPreferencesApi::interface_scale() const {
  return hooks_.interface_scale ? hooks_.interface_scale() : 1.0;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_interface_scale(double scale) {
  if (!std::isfinite(scale)) {
    return {
        .ok = false,
        .changed = false,
        .message = "Interface scale must be finite.",
        .mutation_kind = QuaderMutationKind::None,
    };
  }
  return hooks_.set_interface_scale
             ? hooks_.set_interface_scale(scale)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::vsync_enabled() const {
  return hooks_.vsync_enabled ? hooks_.vsync_enabled() : true;
}

QuaderApiVoidResult QuaderPreferencesApi::set_vsync_enabled(bool enabled) {
  return hooks_.set_vsync_enabled
             ? hooks_.set_vsync_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

SessionPreferences QuaderPreferencesApi::session_settings() const {
  return hooks_.session_settings ? hooks_.session_settings()
                                 : SessionPreferences{};
}

QuaderApiVoidResult
QuaderPreferencesApi::set_session_settings(SessionPreferences settings) {
  if (!settings.valid()) {
    return unavailable("Session preferences are outside their supported ranges.");
  }
  return hooks_.set_session_settings
             ? hooks_.set_session_settings(settings)
             : unavailable("Preferences runtime is not attached.");
}

std::uint64_t QuaderPreferencesApi::keybindings_revision() const noexcept {
  return commands_ != nullptr ? commands_->keybindings_revision() : 0;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_shortcut(std::string_view command_id,
                                   std::string shortcut) {
  return commands_ != nullptr
             ? commands_->set_shortcut(command_id, std::move(shortcut))
             : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult
QuaderPreferencesApi::set_shortcut(QuaderCommandId command_id,
                                   std::string shortcut) {
  return set_shortcut(command_id.string(), std::move(shortcut));
}

QuaderApiVoidResult
QuaderPreferencesApi::remove_shortcut(std::string_view command_id) {
  return commands_ != nullptr ? commands_->remove_shortcut(command_id)
                              : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult
QuaderPreferencesApi::remove_shortcut(QuaderCommandId command_id) {
  return remove_shortcut(command_id.string());
}

QuaderApiVoidResult
QuaderPreferencesApi::reset_shortcut(std::string_view command_id) {
  return commands_ != nullptr ? commands_->reset_shortcut(command_id)
                              : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult
QuaderPreferencesApi::reset_shortcut(QuaderCommandId command_id) {
  return reset_shortcut(command_id.string());
}

QuaderApiVoidResult QuaderPreferencesApi::set_action_shortcut(
    const QuaderKeybindingActionKey &action,
    std::string shortcut,
    QuaderKeybindingConflictPolicy policy) {
  return commands_ != nullptr
             ? commands_->set_action_shortcut(action, std::move(shortcut),
                                               policy)
             : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::remove_action_shortcut(
    const QuaderKeybindingActionKey &action) {
  return commands_ != nullptr
             ? commands_->remove_action_shortcut(action)
             : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::reset_action_shortcut(
    const QuaderKeybindingActionKey &action,
    QuaderKeybindingConflictPolicy policy) {
  return commands_ != nullptr
             ? commands_->reset_action_shortcut(action, policy)
             : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::reset_all_shortcuts() {
  return commands_ != nullptr
             ? commands_->reset_all_shortcuts()
             : unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult
QuaderPreferencesApi::set_snapping_enabled(bool enabled) {
  return hooks_.set_snapping_enabled
             ? hooks_.set_snapping_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::snapping_enabled() const {
  return hooks_.snapping_enabled ? hooks_.snapping_enabled() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_backface_selection_enabled(bool enabled) {
  return hooks_.set_backface_selection_enabled
             ? hooks_.set_backface_selection_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::backface_selection_enabled() const {
  return hooks_.backface_selection_enabled
             ? hooks_.backface_selection_enabled()
             : false;
}

QuaderApiVoidResult QuaderPreferencesApi::set_grid_visible(bool visible) {
  return hooks_.set_grid_visible
             ? hooks_.set_grid_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::grid_visible() const {
  return hooks_.grid_visible ? hooks_.grid_visible() : false;
}

QuaderApiVoidResult QuaderPreferencesApi::set_gizmos_visible(bool visible) {
  return hooks_.set_gizmos_visible
             ? hooks_.set_gizmos_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::gizmos_visible() const {
  return hooks_.gizmos_visible ? hooks_.gizmos_visible() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_mesh_grid_visible(bool visible) {
  return hooks_.set_mesh_grid_visible
             ? hooks_.set_mesh_grid_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::mesh_grid_visible() const {
  return hooks_.mesh_grid_visible ? hooks_.mesh_grid_visible() : false;
}

QuaderApiVoidResult QuaderPreferencesApi::set_overlays_visible(bool visible) {
  return hooks_.set_overlays_visible
             ? hooks_.set_overlays_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::overlays_visible() const {
  return hooks_.overlays_visible ? hooks_.overlays_visible() : false;
}

QuaderApiVoidResult QuaderPreferencesApi::set_collisions_visible(bool visible) {
  return hooks_.set_collisions_visible
             ? hooks_.set_collisions_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::collisions_visible() const {
  return hooks_.collisions_visible ? hooks_.collisions_visible() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_texture_lock_enabled(bool enabled) {
  return hooks_.set_texture_lock_enabled
             ? hooks_.set_texture_lock_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::texture_lock_enabled() const {
  return hooks_.texture_lock_enabled ? hooks_.texture_lock_enabled() : true;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_texture_lock_scale_enabled(bool enabled) {
  return hooks_.set_texture_lock_scale_enabled
             ? hooks_.set_texture_lock_scale_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::texture_lock_scale_enabled() const {
  return hooks_.texture_lock_scale_enabled ? hooks_.texture_lock_scale_enabled()
                                         : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_grid_snap_enabled(bool enabled) {
  return hooks_.set_grid_snap_enabled
             ? hooks_.set_grid_snap_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::grid_snap_enabled() const {
  return hooks_.grid_snap_enabled ? hooks_.grid_snap_enabled() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_vertex_snap_enabled(bool enabled) {
  return hooks_.set_vertex_snap_enabled
             ? hooks_.set_vertex_snap_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::vertex_snap_enabled() const {
  return hooks_.vertex_snap_enabled ? hooks_.vertex_snap_enabled() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_rotation_snap_enabled(bool enabled) {
  return hooks_.set_rotation_snap_enabled
             ? hooks_.set_rotation_snap_enabled(enabled)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::rotation_snap_enabled() const {
  return hooks_.rotation_snap_enabled ? hooks_.rotation_snap_enabled() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_rotation_snap_degrees(double degrees) {
  return hooks_.set_rotation_snap_degrees
             ? hooks_.set_rotation_snap_degrees(degrees)
             : unavailable("Preferences runtime is not attached.");
}

double QuaderPreferencesApi::rotation_snap_degrees() const {
  return hooks_.rotation_snap_degrees ? hooks_.rotation_snap_degrees() : 15.0;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_viewport_info_visible(bool visible) {
  return hooks_.set_viewport_info_visible
             ? hooks_.set_viewport_info_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::viewport_info_visible() const {
  return hooks_.viewport_info_visible ? hooks_.viewport_info_visible() : false;
}

QuaderApiVoidResult
QuaderPreferencesApi::set_navigation_gizmo_visible(bool visible) {
  return hooks_.set_navigation_gizmo_visible
             ? hooks_.set_navigation_gizmo_visible(visible)
             : unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::navigation_gizmo_visible() const {
  return hooks_.navigation_gizmo_visible ? hooks_.navigation_gizmo_visible()
                                         : false;
}

QuaderApiVoidResult QuaderPreferencesApi::save() {
    return hooks_.save ? hooks_.save() : unavailable("Preferences runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::unavailable(std::string message) const {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

} // namespace quader
