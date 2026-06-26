////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderPreferencesApi.h"

#include "api/QuaderCommandsApi.h"

#include <utility>

namespace quader {

QuaderPreferencesApi::QuaderPreferencesApi() = default;

QuaderPreferencesApi::QuaderPreferencesApi(QuaderCommandsApi& commands)
    : commands_(&commands) {}

void QuaderPreferencesApi::attachCommands(QuaderCommandsApi& commands) {
    commands_ = &commands;
}

void QuaderPreferencesApi::setRuntimeHooks(QuaderPreferencesRuntimeHooks hooks) {
    hooks_ = std::move(hooks);
}

std::string QuaderPreferencesApi::locale() const {
    return hooks_.locale ? hooks_.locale() : std::string{};
}

QuaderApiVoidResult QuaderPreferencesApi::setLocale(std::string locale) {
    return hooks_.setLocale ? hooks_.setLocale(std::move(locale)) : unavailable("Preferences runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::setShortcut(std::string_view commandId, std::string shortcut) {
    return commands_ != nullptr ?
            commands_->setShortcut(commandId, std::move(shortcut)) :
            unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::setShortcut(QuaderCommandId commandId, std::string shortcut) {
    return setShortcut(commandId.string(), std::move(shortcut));
}

QuaderApiVoidResult QuaderPreferencesApi::removeShortcut(std::string_view commandId) {
    return commands_ != nullptr ?
            commands_->removeShortcut(commandId) :
            unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::removeShortcut(QuaderCommandId commandId) {
    return removeShortcut(commandId.string());
}

QuaderApiVoidResult QuaderPreferencesApi::resetShortcut(std::string_view commandId) {
    return commands_ != nullptr ?
            commands_->resetShortcut(commandId) :
            unavailable("Command runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::resetShortcut(QuaderCommandId commandId) {
    return resetShortcut(commandId.string());
}

QuaderApiVoidResult QuaderPreferencesApi::setNavigationGizmoVisible(bool visible) {
    return hooks_.setNavigationGizmoVisible ?
            hooks_.setNavigationGizmoVisible(visible) :
            unavailable("Preferences runtime is not attached.");
}

bool QuaderPreferencesApi::navigationGizmoVisible() const {
    return hooks_.navigationGizmoVisible ? hooks_.navigationGizmoVisible() : false;
}

QuaderApiVoidResult QuaderPreferencesApi::save() {
    return hooks_.save ? hooks_.save() : unavailable("Preferences runtime is not attached.");
}

QuaderApiVoidResult QuaderPreferencesApi::unavailable(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

} // namespace quader
