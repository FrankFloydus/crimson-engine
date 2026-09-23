////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QObject>
#include <QPointer>

#include <functional>
#include <unordered_set>

class QAbstractButton;
class QWidget;

namespace quader {

class QuaderCommandsApi;

/**
 * Adapts canonical Apply/Cancel bindings within one preference dialog or popup.
 * Existing buttons remain the action owners. Free-text editing and child
 * windows retain input; consumed releases survive closure of the input surface.
 * An optional OK button shares suppression of unmapped native Space activation.
 */
class QtApplyCancelInputFilter final : public QObject {
public:
  using CommandsProvider = std::function<QuaderCommandsApi *()>;
  using InputReservation = std::function<bool(const QWidget *)>;

  QtApplyCancelInputFilter(QWidget &surface, QAbstractButton &apply,
                          QAbstractButton &cancel,
                          CommandsProvider commands_provider,
                          InputReservation input_reservation = {},
                          QAbstractButton *accept = nullptr);
  /// Retires callbacks before a host destroys or replaces their runtime owners.
  void abandon();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void reset_pending();
  void retire_if_unused();

  QPointer<QWidget> surface_;
  QPointer<QAbstractButton> apply_;
  QPointer<QAbstractButton> cancel_;
  QPointer<QAbstractButton> accept_;
  CommandsProvider commands_provider_;
  InputReservation input_reservation_;
  QuaderCommandsApi *observed_commands_ = nullptr;
  std::unordered_set<int> consumed_keys_;
  bool sequence_pending_ = false;
};

} // namespace quader
