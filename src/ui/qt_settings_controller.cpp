////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_settings_controller.hpp"

#include "core/localization_service.hpp"
#include "ui/ui_settings.hpp"

#include <string>
#include <utility>

namespace quader {

void QtSettingsController::bind(UISettings &ui_settings,
                                ViewportSettings &viewport_settings,
                                LocalizationService &localization,
                                ChangeCallback callback) {
  ui_settings_ = &ui_settings;
  viewport_settings_ = &viewport_settings;
  localization_ = &localization;
  callback_ = std::move(callback);
}

bool QtSettingsController::preview_color(ViewportColorSettingId id,
                                         ViewportSettingColor value) {
  if (viewport_settings_ == nullptr ||
      !viewport_settings_->set_color(id, value)) {
    return false;
  }
  notify(QtSettingsChangeKind::ViewportPreview);
  return true;
}

bool QtSettingsController::rollback_color(
    ViewportColorSettingId id, ViewportSettingColor initial_value) {
  if (viewport_settings_ == nullptr ||
      !viewport_settings_->restore_color(id, initial_value)) {
    return false;
  }
  notify(QtSettingsChangeKind::ViewportPreview);
  return true;
}

bool QtSettingsController::commit_color(
    ViewportColorSettingId id, ViewportSettingColor initial_value) {
  if (viewport_settings_ == nullptr) {
    return false;
  }
  const ViewportSettingColor current = viewport_settings_->color(id);
  if (current.r == initial_value.r && current.g == initial_value.g &&
      current.b == initial_value.b && current.a == initial_value.a) {
    return false;
  }
  notify(QtSettingsChangeKind::ViewportCommit);
  return true;
}

bool QtSettingsController::set_float(ViewportFloatSettingId id, float value) {
  if (viewport_settings_ == nullptr ||
      !viewport_settings_->set_float_value(id, value)) {
    return false;
  }
  notify(QtSettingsChangeKind::ViewportCommit);
  return true;
}

bool QtSettingsController::set_bool(ViewportBoolSettingId id, bool value) {
  if (viewport_settings_ == nullptr ||
      !viewport_settings_->set_bool_value(id, value)) {
    return false;
  }
  notify(QtSettingsChangeKind::ViewportCommit);
  return true;
}

bool QtSettingsController::select_locale(std::string_view locale_id) {
  if (ui_settings_ == nullptr || localization_ == nullptr ||
      locale_id.empty() || !localization_->has_locale(locale_id)) {
    return false;
  }

  const std::string previous_locale = ui_settings_->locale();
  ui_settings_->set_locale(std::string{locale_id});
  if (ui_settings_->locale() == previous_locale) {
    return false;
  }
  notify(QtSettingsChangeKind::InterfaceCommit);
  return true;
}

void QtSettingsController::notify(QtSettingsChangeKind kind) const {
  if (callback_) {
    callback_(kind);
  }
}

} // namespace quader
