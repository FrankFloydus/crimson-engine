////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_ui_api.hpp"

#include "core/logger.hpp"
#include "core/release_features.hpp"

#include <utility>

namespace quader {

QuaderApiVoidResult QuaderConsoleApi::append(std::string message) {
    LOG_INFO("{}", message);
    return {
        .ok = true,
        .changed = false,
        .mutation_kind = QuaderMutationKind::Runtime,
    };
}

QuaderUIApi::QuaderUIApi() = default;

void QuaderUIApi::set_runtime_hooks(QuaderUIRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

QuaderApiVoidResult QuaderUIApi::settle_pending_property_edits() {
  return hooks_.settle_pending_property_edits
             ? hooks_.settle_pending_property_edits()
             : QuaderApiVoidResult{.ok = true, .changed = false};
}

QuaderApiVoidResult QuaderUIApi::show_panel(std::string_view panel) {
  return set_panel_visible(panel, true);
}

QuaderApiVoidResult QuaderUIApi::hide_panel(std::string_view panel) {
  return set_panel_visible(panel, false);
}

QuaderApiVoidResult QuaderUIApi::focus_panel(std::string_view panel) {
  if (((panel == "face_texture" || panel == "Face Texture") &&
       !release_has_feature(ReleaseFeature::UvEditing)) ||
      ((panel == "layers" || panel == "Layers") &&
       !release_has_feature(ReleaseFeature::Layers))) {
    return unavailable(release_feature_unavailable_message());
  }
  return hooks_.focus_panel ? hooks_.focus_panel(panel)
                            : unavailable("UI runtime is not attached.");
}

QuaderApiVoidResult QuaderUIApi::show_command_search() {
  return hooks_.show_command_search
             ? hooks_.show_command_search()
             : unavailable("Command Search is not available.");
}

bool QuaderUIApi::command_search_available() const noexcept {
  return static_cast<bool>(hooks_.show_command_search);
}

QuaderApiVoidResult QuaderUIApi::toggle_asset_browser() {
  return hooks_.toggle_asset_browser
             ? hooks_.toggle_asset_browser()
             : unavailable("Asset Browser is not available.");
}

bool QuaderUIApi::asset_browser_available() const noexcept {
  return static_cast<bool>(hooks_.toggle_asset_browser);
}

bool QuaderUIApi::panel_visible(std::string_view panel) const {
  if (((panel == "face_texture" || panel == "Face Texture") &&
       !release_has_feature(ReleaseFeature::UvEditing)) ||
      ((panel == "layers" || panel == "Layers") &&
       !release_has_feature(ReleaseFeature::Layers))) {
    return false;
  }
  return hooks_.is_panel_visible ? hooks_.is_panel_visible(panel) : false;
}

QuaderApiVoidResult QuaderUIApi::notify(QuaderNotification notification) {
    if (hooks_.notify) {
        return hooks_.notify(std::move(notification));
    }
    switch (notification.level) {
    case QuaderNotificationLevel::Info:
        LOG_INFO("{}", notification.message);
        break;
    case QuaderNotificationLevel::Warning:
        LOG_WARN("{}", notification.message);
        break;
    case QuaderNotificationLevel::Error:
        LOG_ERROR("{}", notification.message);
        break;
    }
    return {
        .ok = true,
        .changed = false,
        .mutation_kind = QuaderMutationKind::Runtime,
    };
}

QuaderConsoleApi& QuaderUIApi::console() {
    return console_;
}

const QuaderConsoleApi& QuaderUIApi::console() const {
    return console_;
}

QuaderApiVoidResult QuaderUIApi::set_panel_visible(std::string_view panel,
                                                   bool visible) {
  if (((panel == "face_texture" || panel == "Face Texture") &&
       !release_has_feature(ReleaseFeature::UvEditing)) ||
      ((panel == "layers" || panel == "Layers") &&
       !release_has_feature(ReleaseFeature::Layers))) {
    return unavailable(release_feature_unavailable_message());
  }
  return hooks_.set_panel_visible ? hooks_.set_panel_visible(panel, visible)
                                  : unavailable("UI runtime is not attached.");
}

QuaderApiVoidResult QuaderUIApi::unavailable(std::string message) const {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

} // namespace quader
