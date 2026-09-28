////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/release_features.hpp"

#include <array>
#include <cstddef>

namespace quader {

/**
 * Enumerates UIPanelId values used by the ui layer.
 */
enum class UIPanelId {
  Viewport,
  Outliner,
  Inspector,
  Console,
  Layers,
  FaceTexture,
  AssetBrowser,
  Count,
};

/// Reports whether a panel belongs to the immutable executable's presentation.
inline bool ui_panel_available(UIPanelId id) noexcept {
  if (id == UIPanelId::FaceTexture) {
    return release_has_feature(ReleaseFeature::UvEditing);
  }
  if (id == UIPanelId::Layers) {
    return release_has_feature(ReleaseFeature::Layers);
  }
  return true;
}

inline constexpr std::size_t kUiPanelCount =
    static_cast<std::size_t>(UIPanelId::Count);
inline constexpr std::array<UIPanelId, 6> kUiDockPanelIds = {
    UIPanelId::Outliner,
    UIPanelId::Inspector,
    UIPanelId::Layers,
    UIPanelId::FaceTexture,
    UIPanelId::Console,
    UIPanelId::AssetBrowser,
};

inline const char *ui_panel_key(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "Viewport";
  case UIPanelId::Outliner:
    return "Outliner";
  case UIPanelId::Inspector:
    return "Inspector";
  case UIPanelId::Console:
    return "Console";
  case UIPanelId::Layers:
    return "Layers";
  case UIPanelId::FaceTexture:
    return "Face Texture";
  case UIPanelId::AssetBrowser:
    return "Asset Browser";
  case UIPanelId::Count:
    break;
  }
  return "Unknown";
}

inline const char *ui_panel_stable_id(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "viewport";
  case UIPanelId::Outliner:
    return "outliner";
  case UIPanelId::Inspector:
    return "inspector";
  case UIPanelId::Console:
    return "console";
  case UIPanelId::Layers:
    return "layers";
  case UIPanelId::FaceTexture:
    return "face_texture";
  case UIPanelId::AssetBrowser:
    return "asset_browser";
  case UIPanelId::Count:
    break;
  }
  return "unknown";
}

inline bool ui_panel_default_visible(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
  case UIPanelId::Outliner:
  case UIPanelId::Inspector:
  case UIPanelId::Layers:
  case UIPanelId::FaceTexture:
    return true;
  case UIPanelId::Console:
  case UIPanelId::AssetBrowser:
    return false;
  case UIPanelId::Count:
    break;
  }
  return true;
}

inline const char *ui_panel_localization_key(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "ui.panel.viewport";
  case UIPanelId::Outliner:
    return "ui.panel.outliner";
  case UIPanelId::Inspector:
    return "ui.panel.inspector";
  case UIPanelId::Console:
    return "ui.panel.console";
  case UIPanelId::Layers:
    return "ui.panel.layers";
  case UIPanelId::FaceTexture:
    return "ui.panel.face_texture";
  case UIPanelId::AssetBrowser:
    return "ui.panel.asset_browser";
  case UIPanelId::Count:
    break;
  }
  return "ui.panel.unknown";
}

} // namespace quader
