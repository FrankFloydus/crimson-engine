////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_preferences_api.hpp"

#include "api/quader_commands_api.hpp"

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
