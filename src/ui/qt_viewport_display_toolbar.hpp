////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_viewport_layout_types.hpp"

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

/**
 * Presents viewport display switches as retained command-backed actions.
 * Focused-pane display state remains public Viewport API owned.
 */
class QtViewportDisplayToolbar final : public QToolBar {
public:
  explicit QtViewportDisplayToolbar(QWidget *parent = nullptr);

  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor, bool force = false);
  /** Shares display actions in toolbar order; excluded actions remain null. */
  [[nodiscard]] std::array<QAction *, 6> viewport_actions() const;
  void set_command_requested_callback(
      std::function<void(QuaderCommandId)> callback);

private:
  /// Binds one retained toolbar action to its public command and label.
  struct DisplayAction {
    QAction *action = nullptr;
    QuaderCommandId command_id;
    std::string_view localization_key;
    std::string_view fallback;
  };

  /// Gates command queries and caches focused-pane checked presentation.
  struct PresentedState {
    const QuaderEditorApi *editor = nullptr;
    ViewportPaneId pane = ViewportPaneId::Perspective;
    std::uint64_t control_revision = 0;
    std::uint64_t keybindings_revision = 0;
    std::array<bool, 6> toggles{};

    bool operator==(const PresentedState &) const = default;
  };

  std::array<DisplayAction, 6> display_actions_{};
  std::optional<PresentedState> presented_state_;
  std::function<void(QuaderCommandId)> command_requested_;
};

} // namespace quader
