////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QFrame>
#include <cstdint>
#include <memory>

class QHideEvent;
class QKeyEvent;

namespace quader {
class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;
class SceneDocument;
struct UIContext;

/// Retained document settings shared by all viewport Environment launchers.
class QtEnvironmentSettingsPopup final : public QFrame {
public:
  explicit QtEnvironmentSettingsPopup(QWidget *parent = nullptr);
  ~QtEnvironmentSettingsPopup() override;
  void refresh(UIContext &context);
  void refresh(QuaderEditorApi &editor, const SceneDocument &document,
               LocalizationService *localization);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void show_below(QWidget &anchor);
  /// Disarms outgoing interactions without dereferencing their runtime owners.
  void abandon_runtime() noexcept;
  [[nodiscard]] std::uint64_t presentation_update_count() const;

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace quader
