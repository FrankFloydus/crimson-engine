////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <QToolBar>

#include <array>
#include <functional>
#include <optional>
#include <string_view>

class QAction;
class QLabel;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;

/**
 * Presents Quader's selection modes as one movable native Qt toolbar.
 * Selection truth and mutations remain owned by the public editor API.
 */
class QtSelectionToolbar final : public QToolBar {
public:
  explicit QtSelectionToolbar(QWidget *parent = nullptr);

  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor, QuaderSelectionMode mode,
                   bool force = false);
  void set_selection_mode(QuaderSelectionMode mode);
  void set_mode_requested_callback(
      std::function<void(QuaderSelectionMode)> callback);

private:
  /// Binds one public selection mode to its retained presentation action.
  struct ModeAction {
    QAction *action = nullptr;
    QuaderSelectionMode mode = QuaderSelectionMode::Object;
    std::string_view localization_key;
    std::string_view fallback;
  };

  QLabel *selection_label_ = nullptr;
  std::array<ModeAction, 5> mode_actions_{};
  bool synchronization_valid_ = false;
  std::optional<QuaderSelectionMode> presented_mode_;
  std::function<void(QuaderSelectionMode)> mode_requested_;
};

} // namespace quader
