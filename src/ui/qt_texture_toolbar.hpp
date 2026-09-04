////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"

#include <QToolBar>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

class QAction;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/// Presents independent texture placement preferences as retained actions.
class QtTextureToolbar final : public QToolBar {
public:
  explicit QtTextureToolbar(QWidget *parent = nullptr);

  [[nodiscard]] QSize minimumSizeHint() const override;
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor, bool force = false);
  void set_command_requested_callback(
      std::function<void(QuaderCommandId)> callback);

private:
  /// Associates a retained control with its command and localized label.
  struct TextureAction {
    QAction *action = nullptr;
    QuaderCommandId command_id;
    std::string_view localization_key;
    std::string_view fallback;
  };

  std::array<TextureAction, 2> texture_actions_{};
  std::optional<std::array<bool, 2>> presented_state_;
  std::uint64_t presented_keybindings_revision_ = 0;
  std::function<void(QuaderCommandId)> command_requested_;
};

} // namespace quader
