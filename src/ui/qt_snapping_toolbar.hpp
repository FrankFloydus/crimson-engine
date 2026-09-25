////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QString>
#include <QToolBar>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

class QAction;
class QComboBox;
class QLabel;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/**
 * Presents retained transform-space, grid and snapping controls in one toolbar.
 * Preference, viewport, command, and keybinding truth remain public API owned.
 */
class QtSnappingToolbar final : public QToolBar {
public:
  explicit QtSnappingToolbar(QWidget *parent = nullptr);

  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  void synchronize(const QuaderEditorApi &editor, bool force = false);

  void set_transform_space_toggle_requested_callback(
      std::function<void()> callback);
  void set_grid_preset_requested_callback(std::function<void(int)> callback);
  void set_rotation_snap_degrees_requested_callback(
      std::function<void(double)> callback);
  void set_snapping_enabled_requested_callback(
      std::function<void(bool)> callback);
  void set_grid_snap_enabled_requested_callback(
      std::function<void(bool)> callback);
  void set_vertex_snap_enabled_requested_callback(
      std::function<void(bool)> callback);
  void set_rotation_snap_enabled_requested_callback(
      std::function<void(bool)> callback);

private:
  void synchronize_transform_space(const QuaderEditorApi &editor, bool force);

  /// Binds one stable snapping preference to its retained presentation action.
  struct ToggleAction {
    QAction *action = nullptr;
    std::string_view localization_key;
    std::string_view fallback;
  };

  /// Caches every owner value that can alter retained toolbar presentation.
  struct PresentedState {
    int grid_preset = 0;
    double rotation_snap_degrees = 0.0;
    std::array<bool, 4> toggles{};

    bool operator==(const PresentedState &) const = default;
  };

  /// Gates command and shortcut queries independently of snapping preferences.
  struct TransformPresentationStamp {
    const QuaderEditorApi *editor = nullptr;
    std::uint64_t transform_space_revision = 0;
    std::uint64_t keybindings_revision = 0;

    bool operator==(const TransformPresentationStamp &) const = default;
  };

  QLabel *grid_label_ = nullptr;
  QComboBox *grid_size_combo_ = nullptr;
  QLabel *angle_label_ = nullptr;
  QComboBox *angle_combo_ = nullptr;
  QAction *transform_space_action_ = nullptr;
  std::array<ToggleAction, 4> toggle_actions_{};
  std::array<std::function<void(bool)>, 4> toggle_requested_{};
  std::optional<PresentedState> presented_state_;
  std::optional<TransformPresentationStamp> transform_presentation_stamp_;
  QString grid_tooltip_base_;
  QString angle_tooltip_base_;
  std::function<void()> transform_space_toggle_requested_;
  std::function<void(int)> grid_preset_requested_;
  std::function<void(double)> rotation_snap_degrees_requested_;
};

} // namespace quader
