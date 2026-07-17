////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "api/quader_viewport_input_types.hpp"

#include <QToolBar>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

class QAction;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;

/**
 * Presents the primary modeling tools as one movable native Qt toolbar.
 * Tool availability, activation, and active state remain public API concerns.
 */
class QtToolToolbar final : public QToolBar {
public:
  explicit QtToolToolbar(QWidget *parent = nullptr);

  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor,
                   const QuaderSelectionSummary &selection,
                   bool force = false);
  void set_tool_requested_callback(
      std::function<void(std::string_view)> callback);

private:
  /// Binds one canonical viewport tool identity to its retained Qt action.
  struct ToolAction {
    QAction *action = nullptr;
    ViewportToolId tool = ViewportToolId::Select;
    std::string_view tool_id;
  };

  std::array<ToolAction, 14> tool_actions_{};
  bool synchronization_valid_ = false;
  std::string synchronized_active_tool_;
  QuaderSelectionMode synchronized_selection_mode_ =
      QuaderSelectionMode::Object;
  std::uint64_t synchronized_selection_revision_ = 0;
  std::function<void(std::string_view)> tool_requested_;
};

} // namespace quader
