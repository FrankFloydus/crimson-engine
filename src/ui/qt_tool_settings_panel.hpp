////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QPoint>
#include <QWidget>

#include <functional>
#include <memory>
#include <string_view>

class QObject;
class QResizeEvent;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;
enum class QuaderNativeToolSettingsIntent;

/**
 * Presents active settings in a retained dock beneath the Outliner.
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
  [[nodiscard]] bool has_active_target() const;
  [[nodiscard]] bool has_settings() const;
  [[nodiscard]] std::string_view target_id() const;
  [[nodiscard]] bool expanded() const;
  void set_expanded(bool expanded);
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  /// Notifies the host after presence or preferred geometry changes.
  std::function<void()> presentation_changed;
  /**
   * Returns whether a local numeric edit is still in progress or waiting to
   * dispatch. This is a presentation-only check and performs no owner query.
   */
  [[nodiscard]] bool numeric_interaction_active() const;

  /// Settles Apply or abandons Cancel drafts before dispatching the intent.
  /// Returns false if validation or synchronous runtime replacement stops it.
  [[nodiscard]] bool prepare_lifecycle_intent(
      QuaderEditorApi &editor, QuaderNativeToolSettingsIntent intent);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Returns whether an application event target belongs to the settings panel,
 * including native child windows and settings control popups.
 */
[[nodiscard]] bool
qt_object_belongs_to_tool_settings_panel(const QObject *target);

/**
 * Resolves pointer delivery shared with an ancestor native window by testing
 * the visible settings region at the event position.
 */
[[nodiscard]] bool qt_object_belongs_to_tool_settings_panel(
    const QObject *target, const QPoint &global_position);

} // namespace quader
