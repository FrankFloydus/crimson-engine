////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QPoint>
#include <QWidget>

#include <memory>

class QObject;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/**
 * Presents active tool settings plus face and edge texture workflows as
 * retained dock content, independent of the active tool.
 * Call refresh from the host UI update path so activation, value changes, and
 * preview completion are reflected immediately.
 */
class QtToolSettingsPanel final : public QWidget {
public:
  explicit QtToolSettingsPanel(QWidget *parent = nullptr);
  ~QtToolSettingsPanel() override;

  QtToolSettingsPanel(const QtToolSettingsPanel &) = delete;
  QtToolSettingsPanel &operator=(const QtToolSettingsPanel &) = delete;

  void bind(QuaderEditorApi &editor);
  void unbind();
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  void refresh();
  /** Refreshes both face sections behind owner and preference revision gates. */
  void refresh_texture_settings();
  [[nodiscard]] bool has_active_target() const;
  /**
   * Returns whether a local numeric edit is still in progress or waiting to
   * dispatch. This is a presentation-only check and performs no owner query.
   */
  [[nodiscard]] bool numeric_interaction_active() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Returns whether an application event target belongs to Tool Settings,
 * including the native window used when its owning dock is floated.
 */
[[nodiscard]] bool
qt_object_belongs_to_tool_settings_panel(const QObject *target);

/**
 * Resolves native-window pointer delivery for a docked panel. A docked
 * QWindow is shared with the main window, so only the visible Tool Settings
 * region at the event position is treated as panel-owned.
 */
[[nodiscard]] bool qt_object_belongs_to_tool_settings_panel(
    const QObject *target, const QPoint &global_position);

} // namespace quader
