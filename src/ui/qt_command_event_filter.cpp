////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_command_event_filter.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>
#include <QWindow>

#include <optional>

namespace quader {
namespace {

bool object_belongs_to_viewport_surface(const QObject *object) {
  for (const QObject *current = object; current != nullptr;
       current = current->parent()) {
    if (dynamic_cast<const QtViewportSurface *>(current) != nullptr) {
      return true;
    }
  }
  return false;
}

bool is_viewport_surface_container(const QWidget *widget) {
  for (const QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (current->objectName().startsWith(
            QStringLiteral("viewportSurfaceContainer_"))) {
      return true;
    }
  }
  return false;
}

} // namespace

QtCommandEventFilter::QtCommandEventFilter(QuaderEditorApi &editor,
                                           QObject *parent)
    : QObject(parent), editor_(&editor) {}

bool QtCommandEventFilter::eventFilter(QObject *watched, QEvent *event) {
  if (editor_ == nullptr || event == nullptr) {
    return QObject::eventFilter(watched, event);
  }

  switch (event->type()) {
  case QEvent::ApplicationDeactivate:
  case QEvent::WindowDeactivate:
  case QEvent::FocusOut:
    reset_pending_chord();
    break;
  case QEvent::KeyPress: {
    if (modal_or_popup_owns_input()) {
      // QColorDialog, QMessageBox, QMenu and other transient owners must keep
      // Delete, tool, undo and chord keys inside their own interaction scope.
      reset_pending_chord();
      return QObject::eventFilter(watched, event);
    }
    if (viewport_surface_has_focus(watched)) {
      return QObject::eventFilter(watched, event);
    }

    const std::optional<QuaderInputChord> chord =
        quader_pressed_input_chord_from_qt_event(
            *static_cast<QKeyEvent *>(event));
    if (!chord.has_value()) {
      return QObject::eventFilter(watched, event);
    }

    const ViewportToolState state{
        .grid_preset = editor_->viewports().grid_preset(),
    };
    const QuaderViewportKeybindingDispatch dispatch =
        editor_->commands().dispatch_viewport_input_binding(
            *chord, state, false, text_input_has_focus());
    switch (dispatch.kind) {
    case QuaderViewportKeybindingDispatch::Kind::PendingChord:
      event->accept();
      return true;
    case QuaderViewportKeybindingDispatch::Kind::Command: {
      const QuaderApiVoidResult result =
          editor_->commands().execute(dispatch.command_id);
      if (!result.ok) {
        LOG_ERROR("Qt keybinding command failed: {}",
                  result.message.empty() ? "Command failed." : result.message);
      }
      event->accept();
      return true;
    }
    case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
    case QuaderViewportKeybindingDispatch::Kind::NoMatch:
      break;
    }
    break;
  }
  default:
    break;
  }

  return QObject::eventFilter(watched, event);
}

bool QtCommandEventFilter::modal_or_popup_owns_input() {
  return QApplication::activeModalWidget() != nullptr ||
         QApplication::activePopupWidget() != nullptr;
}

bool QtCommandEventFilter::viewport_surface_has_focus(QObject *watched) {
  if (object_belongs_to_viewport_surface(QGuiApplication::focusObject()) ||
      object_belongs_to_viewport_surface(QGuiApplication::focusWindow()) ||
      object_belongs_to_viewport_surface(watched)) {
    return true;
  }

  // QWidget::createWindowContainer can retain QWidget focus while forwarding
  // key events to its child QWindow. Its stable object name identifies that
  // focus proxy without mirroring viewport focus state in this dispatcher.
  return is_viewport_surface_container(QApplication::focusWidget());
}

bool QtCommandEventFilter::text_input_has_focus() {
  return is_text_input_widget(QApplication::focusWidget());
}

bool QtCommandEventFilter::is_text_input_widget(const QWidget *widget) {
  if (widget == nullptr) {
    return false;
  }

  for (const QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (dynamic_cast<const QLineEdit *>(current) != nullptr ||
        dynamic_cast<const QTextEdit *>(current) != nullptr ||
        dynamic_cast<const QPlainTextEdit *>(current) != nullptr ||
        dynamic_cast<const QAbstractSpinBox *>(current) != nullptr) {
      return true;
    }
    if (const auto *combo = dynamic_cast<const QComboBox *>(current);
        combo != nullptr && combo->isEditable()) {
      return true;
    }
  }

  // Standard item delegates create their editor below the item view's
  // viewport. The view or viewport itself having focus is ordinary browsing,
  // while a focused descendant is an active delegate editor.
  for (const QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    const auto *view = dynamic_cast<const QAbstractItemView *>(current);
    if (view == nullptr || view->viewport() == nullptr) {
      continue;
    }
    const QWidget *viewport = view->viewport();
    if (widget != viewport && viewport->isAncestorOf(widget)) {
      return true;
    }
  }
  return false;
}

void QtCommandEventFilter::reset_pending_chord() {
  if (editor_ != nullptr && editor_->commands().has_pending_keybinding_chord()) {
    editor_->commands().reset_pending_keybinding_chord();
  }
}

} // namespace quader
