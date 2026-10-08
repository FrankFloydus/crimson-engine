////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QHideEvent;
class QPaintEvent;

namespace quader {

struct QuaderInputChord;
class QtUiScaleMetrics;

/// A presentation-only, hold-and-release command chooser. Actions are borrowed;
/// the caller owns their lifetime and dispatches the returned action.
class QtMarkingMenu final : public QWidget {
public:
  explicit QtMarkingMenu(QWidget *parent = nullptr);
  ~QtMarkingMenu() override;

  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  /// Eight stable compass slots run clockwise from north. Supplemental actions
  /// and separators appear in a native menu list below the ring. Menu actions
  /// open native submenus; only their borrowed leaves can be returned, without
  /// triggering them. The complete
  /// layout fits the monitor once before opening; a tall list can sit beside
  /// it.
  [[nodiscard]] QAction *exec(const QPoint &global_anchor,
                              const QuaderInputChord &initiating_chord,
                              const std::array<QAction *, 8> &slot_actions,
                              const std::vector<QAction *> &overflow_actions,
                              const QString &title,
                              std::optional<std::uint64_t> initiating_key_identity =
                                  std::nullopt);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
