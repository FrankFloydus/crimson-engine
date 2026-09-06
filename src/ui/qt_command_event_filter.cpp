////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_command_event_filter.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTextEdit>
#include <QWidget>
#include <QWindow>

#include <optional>
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

const QAbstractItemView *focused_item_view(const QWidget *focus_widget) {
  if (focus_widget == nullptr) {
    return nullptr;
  }

  // Delegate editors live below the viewport and own their own edit keys. A
  // view itself (or its focus proxy viewport) owns the documented item-view
  // navigation, selection, activation, and keyboard-search behavior.
  for (const QWidget *current = focus_widget; current != nullptr;
       current = current->parentWidget()) {
    const auto *view = dynamic_cast<const QAbstractItemView *>(current);
    if (view == nullptr) {
      continue;
    }
    return focus_widget == view || focus_widget == view->viewport() ? view
                                                                    : nullptr;
  }
  return nullptr;
}

bool item_view_owns_key_press(const QWidget *focus_widget,
                             const QKeyEvent &event) {
  const QAbstractItemView *view = focused_item_view(focus_widget);
  if (view == nullptr) {
    return false;
  }

  if (event.matches(QKeySequence::SelectAll)) {
    return view->selectionMode() != QAbstractItemView::NoSelection;
  }

  Qt::KeyboardModifiers modifiers = event.modifiers();
  modifiers.setFlag(Qt::KeypadModifier, false);
  if (modifiers.testFlag(Qt::AltModifier) ||
      modifiers.testFlag(Qt::MetaModifier)) {
    return false;
  }

  switch (event.key()) {
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Left:
  case Qt::Key_Right:
  case Qt::Key_Home:
  case Qt::Key_End:
  case Qt::Key_PageUp:
  case Qt::Key_PageDown:
    return true;
  case Qt::Key_Tab:
    return modifiers == Qt::NoModifier;
  case Qt::Key_Backtab:
    return modifiers == Qt::NoModifier ||
           modifiers == Qt::ShiftModifier;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_F2:
    return modifiers == Qt::NoModifier;
  case Qt::Key_Space:
    return true;
  default:
    break;
  }

  // QAbstractItemView uses unmodified text for its built-in keyboard search.
  return !event.text().isEmpty() &&
         !modifiers.testFlag(Qt::ControlModifier);
}

bool standard_control_owns_key_press(const QWidget *focus_widget,
                                    const QKeyEvent &event) {
  if (focus_widget == nullptr) {
    return false;
  }

  Qt::KeyboardModifiers modifiers = event.modifiers();
  modifiers.setFlag(Qt::KeypadModifier, false);

  if ((event.key() == Qt::Key_Tab || event.key() == Qt::Key_Backtab) &&
      (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)) {
    return true;
  }

  if (dynamic_cast<const QTabBar *>(focus_widget) != nullptr &&
      modifiers == Qt::NoModifier) {
    switch (event.key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
      return true;
    default:
      break;
    }
  }

  if (const auto *combo = dynamic_cast<const QComboBox *>(focus_widget);
      combo != nullptr && !combo->isEditable()) {
    const bool unmodified = modifiers == Qt::NoModifier;
    switch (event.key()) {
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_F4:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return unmodified;
    case Qt::Key_Down:
      return unmodified || modifiers == Qt::AltModifier;
    case Qt::Key_PageDown:
      return unmodified;
    default:
      return !event.text().isEmpty() &&
             !modifiers.testFlag(Qt::ControlModifier) &&
             !modifiers.testFlag(Qt::AltModifier) &&
             !modifiers.testFlag(Qt::MetaModifier);
    }
  }

  if (dynamic_cast<const QAbstractButton *>(focus_widget) != nullptr &&
      modifiers == Qt::NoModifier) {
    switch (event.key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
      return true;
    default:
      break;
    }
  }

  if (dynamic_cast<const QAbstractSlider *>(focus_widget) != nullptr &&
      !modifiers.testFlag(Qt::AltModifier) &&
      !modifiers.testFlag(Qt::MetaModifier)) {
    switch (event.key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
      return true;
    default:
      break;
    }
  }

  return false;
}

bool focused_widget_owns_key_press(QWidget *focus_widget,
                                  const QKeyEvent &event) {
  return item_view_owns_key_press(focus_widget, event) ||
         standard_control_owns_key_press(focus_widget, event);
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

bool qt_window_owns_command_input(const QWidget *window) {
  if (window == nullptr || !window->isVisible()) {
    return false;
  }

  const QWidget *const focus_widget = QApplication::focusWidget();
  return window->isActiveWindow() ||
         (focus_widget != nullptr &&
          (focus_widget == window || window->isAncestorOf(focus_widget)));
}

QtCommandEventFilter::QtCommandEventFilter(
    QuaderEditorApi &editor, QtViewportToolStateProvider tool_state_provider,
    QObject *parent)
    : QObject(parent), editor_(&editor),
      tool_state_provider_(std::move(tool_state_provider)) {}

void QtCommandEventFilter::rebind_editor(QuaderEditorApi &editor) noexcept {
  editor_ = &editor;
  reset_pending_chord();
}

bool qt_outliner_owns_command_input(const QWidget *outliner) {
  if (outliner == nullptr || !outliner->isVisible()) {
    return false;
  }
  const QWidget *const focus_widget = QApplication::focusWidget();
  return outliner->underMouse() ||
         (focus_widget != nullptr &&
          (focus_widget == outliner || outliner->isAncestorOf(focus_widget)));
}

void QtCommandEventFilter::set_command_input_owner_provider(
    QtApplicationCommandInputOwnerProvider provider) {
  command_input_owner_provider_ = std::move(provider);
}

void QtCommandEventFilter::set_outliner_input_owner_provider(
    QtApplicationCommandInputOwnerProvider provider) {
  outliner_input_owner_provider_ = std::move(provider);
}

void QtCommandEventFilter::set_input_context_provider(
    QtCommandInputContextProvider provider) {
  input_context_provider_ = std::move(provider);
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
    QDR_PROFILE_SCOPE("Qt command key event dispatch");
    if (modal_or_popup_owns_input()) {
      // QColorDialog, QMessageBox, QMenu and other transient owners must keep
      // Delete, tool, undo and chord keys inside their own interaction scope.
      reset_pending_chord();
      return QObject::eventFilter(watched, event);
    }
    QWidget *const focus_widget = QApplication::focusWidget();
    if (qt_application_command_input_owner_active(
            command_input_owner_provider_)) {
      // Retained tool windows own their window-scoped document shortcuts.
      // Yield before resolving the same chord as an active scene command.
      reset_pending_chord();
      return QObject::eventFilter(watched, event);
    }
    const bool text_input_active = text_input_has_focus();
    const bool outliner_focused =
        !text_input_active && qt_application_command_input_owner_active(
                                  outliner_input_owner_provider_);
    const bool viewport_focused = !outliner_focused && !text_input_active &&
                                 viewport_surface_has_focus(watched);

    const std::optional<QuaderInputChord> chord =
        quader_pressed_input_chord_from_qt_event(
            *static_cast<QKeyEvent *>(event));
    if (!chord.has_value()) {
      return QObject::eventFilter(watched, event);
    }

    ViewportToolState state = qt_command_dispatch_tool_state(
        tool_state_provider_, editor_->viewports().grid_preset());
    QuaderViewportKeybindingContext input_context =
        input_context_provider_ ? input_context_provider_()
                                : QuaderViewportKeybindingContext{};
    input_context.outliner_focused = outliner_focused;
    if (outliner_focused) {
      // Tree selection can publish before the next viewport frame samples it.
      state.selection_mode = app_selection_mode_to_viewport(
          to_modeling_selection_mode(editor_->selection().summary().mode));
    }
    if (viewport_focused) {
      const std::optional<std::string> immediate_command =
          editor_->commands().resolve_immediate_global_command(
              *chord, state, false, input_context);
      if (!immediate_command.has_value()) {
        return QObject::eventFilter(watched, event);
      }

      // The viewport queues input until the next app frame. Global commands
      // must run at the Qt event boundary so a following close event observes
      // effects such as a completed save. Consuming the edge also prevents the
      // viewport router from executing the same command a second time.
      reset_pending_chord();
      const QuaderApiVoidResult result = [&]() {
        QDR_PROFILE_SCOPE("Qt viewport global command execute");
        return editor_->commands().execute(*immediate_command);
      }();
      if (!result.ok) {
        LOG_ERROR("Qt viewport global keybinding command failed: {}",
                  result.message.empty() ? "Command failed."
                                         : result.message);
      }
      event->accept();
      return true;
    }

    if (text_input_active) {
      reset_pending_chord();
    }
    const bool native_tool_settings_editor_focused =
        qt_object_belongs_to_tool_settings_panel(focus_widget) &&
        (text_input_active || focused_combo_box(focus_widget) != nullptr);
    if (outliner_focused) {
      // Hover can transfer command ownership without a Qt focus event. Clear
      // an outgoing viewport prefix before checking for a tree continuation.
      editor_->commands().reset_pending_viewport_keybinding_chord();
    }
    const bool outliner_sequence_pending =
        outliner_focused && editor_->commands().has_pending_keybinding_chord();
    QuaderViewportKeybindingDispatch dispatch = [&]() {
      QDR_PROFILE_SCOPE("Qt command resolve keybinding");
      return editor_->commands().dispatch_viewport_input_binding(
          *chord, state, false, text_input_active,
          input_context);
    }();
    if (outliner_sequence_pending &&
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::NoMatch) {
      // A mismatched continuation belongs to the sequence that consumed its
      // prefix. Do not replay it as a fresh command through a parent widget or
      // the still-focused viewport's deferred input queue.
      event->accept();
      return true;
    }
    if (dispatch.kind ==
            QuaderViewportKeybindingDispatch::Kind::NoMatch &&
        native_tool_settings_editor_focused) {
      const QuaderNativeToolSettingsDispatch tool_settings_dispatch = [&]() {
        QDR_PROFILE_SCOPE("Qt command resolve tool-settings keybinding");
        return editor_->commands()
            .dispatch_native_tool_settings_input_binding(*chord, state);
      }();
      if (tool_settings_dispatch.kind ==
          QuaderNativeToolSettingsDispatch::Kind::Intent) {
        if (tool_settings_dispatch.intent ==
            QuaderNativeToolSettingsIntent::Apply) {
          if (QAbstractSpinBox *spin = focused_spin_box(focus_widget)) {
            // Commit the visible text before Apply consumes Enter so the live
            // native-tool preview and its persistent setting match the field.
            spin->interpretText();
          }
        }
        QuaderEditorApi *const command_editor = editor_;
        const std::uint64_t status_revision =
            command_editor->commands().last_status_revision();
        const QuaderApiVoidResult result = [&]() {
          QDR_PROFILE_SCOPE("Qt command execute tool-settings lifecycle");
          return command_editor->commands().execute_native_tool_settings_intent(
              tool_settings_dispatch.intent);
        }();
        if (!result.ok) {
          LOG_ERROR("Qt Tool Settings lifecycle failed: {}",
                    result.message.empty() ? "Command failed."
                                           : result.message);
          if (editor_ == command_editor &&
              command_editor->commands().last_status_revision() ==
                  status_revision) {
            (void)command_editor->ui().notify(
                {.level = QuaderNotificationLevel::Error,
                 .message = result.message.empty() ? "Command failed."
                                                   : result.message});
          }
        }
        event->accept();
        return true;
      } else if (tool_settings_dispatch.kind ==
                 QuaderNativeToolSettingsDispatch::Kind::PendingChord) {
        event->accept();
        return true;
      }
    }

    if (!outliner_focused && focused_widget_owns_key_press(
            focus_widget, *static_cast<QKeyEvent *>(event))) {
      // Preserve the documented key behavior of the focused Qt control,
      // including item-view Ctrl+A, range navigation, activation, and
      // keyboard search.
      reset_pending_chord();
      return QObject::eventFilter(watched, event);
    }
    switch (dispatch.kind) {
    case QuaderViewportKeybindingDispatch::Kind::PendingChord:
      event->accept();
      return true;
    case QuaderViewportKeybindingDispatch::Kind::Command: {
      const QuaderApiVoidResult result = [&]() {
        QDR_PROFILE_SCOPE("Qt command execute keybinding");
        return editor_->commands().execute(dispatch.command_id);
      }();
      if (!result.ok) {
        LOG_ERROR("Qt keybinding command failed: {}",
                  result.message.empty() ? "Command failed." : result.message);
      }
      event->accept();
      return true;
    }
    case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
    case QuaderViewportKeybindingDispatch::Kind::ViewportInputAction:
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
