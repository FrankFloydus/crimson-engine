////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_apply_cancel_input_filter.hpp"

#include "api/quader_commands_api.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_numeric_spin_box.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QAbstractSpinBox *enclosing_spin_box(QWidget *widget) {
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (auto *spin = dynamic_cast<QAbstractSpinBox *>(current)) {
      return spin;
    }
  }
  return nullptr;
}

bool free_text_owns_input(QWidget *widget, const QKeyEvent &key) {
  if (enclosing_spin_box(widget) != nullptr) {
    return false;
  }
  bool text_editor = false;
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    text_editor = text_editor || dynamic_cast<QLineEdit *>(current) != nullptr ||
                  dynamic_cast<QTextEdit *>(current) != nullptr ||
                  dynamic_cast<QPlainTextEdit *>(current) != nullptr;
  }
  if (!text_editor) {
    return false;
  }
  const auto modifiers = key.modifiers();
  const QString text = key.text();
  if (std::any_of(text.cbegin(), text.cend(),
                  [](QChar character) { return character.isPrint(); }) &&
      !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    return true;
  }
  for (const auto standard : {QKeySequence::Copy, QKeySequence::Cut,
                             QKeySequence::Paste, QKeySequence::Undo,
                             QKeySequence::Redo, QKeySequence::SelectAll,
                             QKeySequence::Delete, QKeySequence::Backspace,
                             QKeySequence::InsertParagraphSeparator,
                             QKeySequence::InsertLineSeparator}) {
    if (key.matches(standard)) {
      return true;
    }
  }
  return false;
}

} // namespace

QtApplyCancelInputFilter::QtApplyCancelInputFilter(
    QWidget &surface, QAbstractButton &apply, QAbstractButton &cancel,
    CommandsProvider commands_provider, InputReservation input_reservation,
    QAbstractButton *accept)
    : QObject(qApp), surface_(&surface), apply_(&apply), cancel_(&cancel),
      accept_(accept),
      commands_provider_(std::move(commands_provider)),
      input_reservation_(std::move(input_reservation)) {
  qApp->installEventFilter(this);
  QObject::connect(&surface, &QObject::destroyed, this, [this]() {
    surface_.clear();
    commands_provider_ = {};
    input_reservation_ = {};
    sequence_pending_ = false;
    retire_if_unused();
  });
}

void QtApplyCancelInputFilter::reset_pending() {
  if (sequence_pending_ && commands_provider_) {
    if (auto *commands = commands_provider_()) {
      commands->reset_pending_modal_lifecycle_input_binding();
    }
  }
  sequence_pending_ = false;
}

void QtApplyCancelInputFilter::abandon() {
  commands_provider_ = {};
  input_reservation_ = {};
  surface_.clear();
  sequence_pending_ = false;
  retire_if_unused();
}

void QtApplyCancelInputFilter::retire_if_unused() {
  if (!surface_ && consumed_keys_.empty()) {
    deleteLater();
  }
}

bool QtApplyCancelInputFilter::eventFilter(QObject *watched, QEvent *event) {
  if (qt_handle_numeric_text_edit_key(watched, event)) {
    reset_pending();
    return true;
  }
  if (event == nullptr) {
    return false;
  }
  if (event->type() == QEvent::ShortcutOverride ||
      event->type() == QEvent::KeyRelease ||
      event->type() == QEvent::KeyPress) {
    const auto &key = *static_cast<QKeyEvent *>(event);
    if (consumed_keys_.contains(key.key()) &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyRelease || key.isAutoRepeat())) {
      if (event->type() == QEvent::KeyRelease && !key.isAutoRepeat()) {
        consumed_keys_.erase(key.key());
        retire_if_unused();
      }
      event->accept();
      return true;
    }
  }
  if (event->type() == QEvent::ApplicationDeactivate) {
    reset_pending();
    consumed_keys_.clear();
    retire_if_unused();
    return false;
  }
  if (event->type() == QEvent::FocusOut ||
      (watched == surface_ && (event->type() == QEvent::Hide ||
                               event->type() == QEvent::WindowDeactivate))) {
    reset_pending();
  }
  if (!surface_ || !surface_->isVisible() ||
      (event->type() != QEvent::ShortcutOverride &&
       event->type() != QEvent::KeyPress)) {
    return false;
  }
  auto *widget = qobject_cast<QWidget *>(watched);
  if (widget == nullptr || widget->window() != surface_->window() ||
      (widget != surface_ && !surface_->isAncestorOf(widget))) {
    return false;
  }
  if ((QApplication::activeModalWidget() != nullptr &&
       QApplication::activeModalWidget() != surface_) ||
      (QApplication::activePopupWidget() != nullptr &&
       QApplication::activePopupWidget() != surface_)) {
    reset_pending();
    return false;
  }
  if (input_reservation_ && input_reservation_(widget)) {
    reset_pending();
    return false;
  }
  auto *commands = commands_provider_ ? commands_provider_() : nullptr;
  if (commands == nullptr) {
    sequence_pending_ = false;
    observed_commands_ = nullptr;
    return false;
  }
  if (observed_commands_ != commands) {
    commands->reset_pending_modal_lifecycle_input_binding();
    sequence_pending_ = false;
    observed_commands_ = commands;
  }
  const auto &key = *static_cast<QKeyEvent *>(event);
  if (!sequence_pending_ && free_text_owns_input(widget, key)) {
    reset_pending();
    return false;
  }
  const auto chord = quader_input_chord_from_qt_key(key.key(), key.modifiers());
  const auto resolved = commands->resolve_modal_lifecycle_input_binding(chord);
  if (resolved.kind == QuaderNativeToolSettingsDispatch::Kind::NoMatch) {
    reset_pending();
    // QDialog's built-in Cancel key must not bypass an unassigned or remapped
    // canonical action. This suppresses Qt fallback; it never selects an action.
    const bool lifecycle_button_space =
        (widget == apply_ || widget == cancel_ || widget == accept_) &&
        key.key() == Qt::Key_Space &&
        key.modifiers() == Qt::NoModifier;
    if (key.matches(QKeySequence::Cancel) || lifecycle_button_space) {
      if (event->type() == QEvent::KeyPress && !key.isAutoRepeat()) {
        consumed_keys_.insert(key.key());
      }
      event->accept();
      return true;
    }
    return false;
  }
  event->accept();
  if (event->type() == QEvent::ShortcutOverride || key.isAutoRepeat()) {
    return true;
  }
  const auto dispatch = commands->dispatch_modal_lifecycle_input_binding(chord);
  if (dispatch.kind == QuaderNativeToolSettingsDispatch::Kind::NoMatch) {
    return false;
  }
  consumed_keys_.insert(key.key());
  sequence_pending_ =
      dispatch.kind == QuaderNativeToolSettingsDispatch::Kind::PendingChord;
  if (sequence_pending_) {
    return true;
  }
  if (dispatch.intent == QuaderNativeToolSettingsIntent::Apply) {
    const QPointer<QAbstractButton> apply = apply_;
    const QPointer<QWidget> surface = surface_;
    if (QAbstractSpinBox *spin = enclosing_spin_box(widget)) {
      spin->interpretText();
    }
    if (surface && surface->isVisible() && apply && apply->isEnabled() &&
        commands_provider_ && commands_provider_() == commands) {
      apply->click();
    }
  } else if (dispatch.intent == QuaderNativeToolSettingsIntent::Cancel &&
             cancel_ && cancel_->isEnabled()) {
    cancel_->click();
  }
  return true;
}

} // namespace quader
