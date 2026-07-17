////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_command_event_filter.hpp"

#include "api/quader_command_ids.hpp"
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
#include <string>
#include <utility>

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

bool is_primitive_tool(ViewportToolId tool) {
  return tool == ViewportToolId::CreatePrimitive;
}

bool belongs_to_tool_settings_window(const QWidget *widget) {
  for (const QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (current->objectName() == QStringLiteral("floating.tool_settings")) {
      return true;
    }
  }
  return false;
}

QAbstractSpinBox *focused_spin_box(QWidget *widget) {
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (auto *spin = dynamic_cast<QAbstractSpinBox *>(current)) {
      return spin;
    }
  }
  return nullptr;
}

QComboBox *focused_combo_box(QWidget *widget) {
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (auto *combo = dynamic_cast<QComboBox *>(current)) {
      return combo;
    }
  }
  return nullptr;
}

bool is_primitive_finalize_command(const std::string &command_id) {
  return command_id ==
             quader_command::kToolsApplyPrimitiveCreation.string() ||
         command_id ==
             quader_command::kToolsCancelPrimitiveCreation.string();
}

} // namespace

ViewportToolState
qt_command_dispatch_tool_state(const QtViewportToolStateProvider &provider,
                               int grid_preset) {
  ViewportToolState state = provider ? provider() : ViewportToolState{};
  state.grid_preset = grid_preset;
  return state;
}

bool qt_application_command_input_owner_active(
    const QtApplicationCommandInputOwnerProvider &provider) {
  return provider && provider();
}

QtCommandEventFilter::QtCommandEventFilter(
    QuaderEditorApi &editor, QtViewportToolStateProvider tool_state_provider,
    QObject *parent)
    : QObject(parent), editor_(&editor),
      tool_state_provider_(std::move(tool_state_provider)) {}

void QtCommandEventFilter::set_command_input_owner_provider(
    QtApplicationCommandInputOwnerProvider provider) {
  command_input_owner_provider_ = std::move(provider);
}

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
    if (qt_application_command_input_owner_active(
            command_input_owner_provider_)) {
      // Retained tool windows own their window-scoped document shortcuts.
      // Yield before resolving the same chord as an active scene command.
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

    const ViewportToolState state = qt_command_dispatch_tool_state(
        tool_state_provider_, editor_->viewports().grid_preset());
    QWidget *const focus_widget = QApplication::focusWidget();
    const bool text_input_active = text_input_has_focus();
    const bool primitive_tool_settings_editor_active =
        is_primitive_tool(state.active_tool) &&
        belongs_to_tool_settings_window(focus_widget) &&
        (text_input_active || focused_combo_box(focus_widget) != nullptr);
    QuaderViewportKeybindingDispatch dispatch =
        editor_->commands().dispatch_viewport_input_binding(
            *chord, state, false, text_input_active);
    if (dispatch.kind ==
            QuaderViewportKeybindingDispatch::Kind::NoMatch &&
        primitive_tool_settings_editor_active) {
      QuaderViewportKeybindingDispatch tool_settings_dispatch =
          editor_->commands().dispatch_viewport_input_binding(
              *chord, state, true, false);
      if (tool_settings_dispatch.kind ==
              QuaderViewportKeybindingDispatch::Kind::Command &&
          is_primitive_finalize_command(
              tool_settings_dispatch.command_id)) {
        if (QAbstractSpinBox *spin =
                focused_spin_box(focus_widget)) {
          // Commit the visible text before Apply consumes Enter so the live
          // selected primitive and the persistent setting match the field.
          spin->interpretText();
        }
        dispatch = std::move(tool_settings_dispatch);
      } else if (tool_settings_dispatch.kind ==
                     QuaderViewportKeybindingDispatch::Kind::ViewportShortcut &&
                 tool_settings_dispatch.shortcut.kind ==
                     ViewportShortcutKind::CancelActiveTool) {
        // The grouped Cancel Tool action uses the viewport alias on viewport
        // surfaces. Inside primitive settings, route that same configurable
        // mapping through the primitive command so the retained editor closes.
        dispatch = {
            .kind = QuaderViewportKeybindingDispatch::Kind::Command,
            .command_id = std::string(
                quader_command::kToolsCancelPrimitiveCreation.string()),
        };
      } else if (tool_settings_dispatch.kind ==
                 QuaderViewportKeybindingDispatch::Kind::PendingChord) {
        editor_->commands().reset_pending_keybinding_chord();
      }
    }
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
