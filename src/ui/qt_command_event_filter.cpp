////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_command_event_filter.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTabBar>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#include <optional>
#include <string_view>
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

bool asset_browser_owns_input(const QWidget *widget) {
  for (const QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (current->property("quaderAssetBrowserInput").toBool()) {
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

QtToolSettingsPanel *focused_tool_settings_panel(QWidget *widget) {
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (auto *panel = dynamic_cast<QtToolSettingsPanel *>(current)) {
      return panel;
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

bool suppress_settings_button_space(QuaderEditorApi &editor,
                                    const QWidget *focus,
                                    const QKeyEvent &event) {
  if (event.key() != Qt::Key_Space ||
      event.modifiers() != Qt::NoModifier ||
      dynamic_cast<const QAbstractButton *>(focus) == nullptr) {
    return false;
  }
  const auto settings = editor.tools().settings_for_pane(editor.viewports().focused_pane());
  // An unassigned/remapped lifecycle key must not activate whichever button
  // happens to have focus. This reserves the Qt edge without inventing intent.
  return settings.active && (settings.apply_visible || settings.can_cancel);
}

QLineEdit *focused_line_edit(QWidget *widget) {
  for (QWidget *current = widget; current != nullptr;
       current = current->parentWidget()) {
    if (auto *line_edit = dynamic_cast<QLineEdit *>(current)) {
      return line_edit;
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
                             const QKeyEvent &event,
                             bool scene_selection_owner) {
  const QAbstractItemView *view = focused_item_view(focus_widget);
  if (view == nullptr) {
    return false;
  }

  if (event.matches(QKeySequence::SelectAll)) {
    // The Outliner uses scene selection commands; other item views select
    // their local rows. Both still own explicit navigation and rename keys.
    return !scene_selection_owner &&
           view->selectionMode() != QAbstractItemView::NoSelection;
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

  // Type-ahead search is a fallback after canonical editor command dispatch.
  // It must not reserve every printable tool shortcut merely because a
  // material card or another item view currently owns keyboard focus.
  return false;
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
                                  const QKeyEvent &event,
                                  bool scene_selection_owner) {
  return item_view_owns_key_press(focus_widget, event, scene_selection_owner) ||
         standard_control_owns_key_press(focus_widget, event);
}

bool is_scene_document_save_command(std::string_view command_id) {
  return command_id == quader_command::kSceneDocumentSave.string() ||
         command_id == quader_command::kSceneDocumentSaveAs.string() ||
         command_id == quader_command::kSceneDocumentSaveAll.string();
}

bool is_asset_browser_edit_command(std::string_view command_id) {
  return command_id == quader_command::kCutSelection.string() ||
         command_id == quader_command::kCopySelection.string() ||
         command_id == quader_command::kPasteSelection.string() ||
         command_id == quader_command::kDuplicateSelection.string() ||
         command_id == quader_command::kDeleteSelection.string();
}

void normalize_command_input_context(
    QuaderEditorApi &editor, ViewportToolState &state,
    QuaderViewportKeybindingContext &input_context,
    bool viewport_focused, bool text_input_active, bool asset_browser_focused) {
  if (state.active_tool == ViewportToolId::InsertEdgeLoop &&
      !editor.tools().pane_owns_executing_tool(editor.viewports().focused_pane())) {
    input_context.operation_preview_active = false;
    input_context.active_transform_drag = false;
    input_context.modal_tool_active = false;
  }
  if (!viewport_focused && !text_input_active && !asset_browser_focused) {
    // Panels can publish selection before the next viewport frame samples it.
    state.selection_mode = app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode));
  }
}

struct SettledEditorFocus final {
  QPointer<QWidget> editor;
  QPointer<QWidget> window;
  QPointer<QLineEdit> line_editor;
  QString line_editor_text;
  int cursor_position = 0;
  int selection_start = -1;
  int selection_length = 0;
  bool cursor_at_selection_start = false;
  bool relinquished = false;
};

SettledEditorFocus settle_focused_editor(QWidget *focus_widget) {
  SettledEditorFocus settled{
      .editor = focus_widget,
      .window = focus_widget != nullptr ? focus_widget->window() : nullptr,
  };
  if (focus_widget == nullptr) {
    return settled;
  }

  if (QLineEdit *line_edit = focused_line_edit(focus_widget)) {
    settled.line_editor = line_edit;
    settled.line_editor_text = line_edit->text();
    settled.cursor_position = line_edit->cursorPosition();
    settled.selection_start = line_edit->selectionStart();
    settled.selection_length = line_edit->selectedText().size();
    settled.cursor_at_selection_start =
        settled.selection_start >= 0 &&
        settled.cursor_position == settled.selection_start;
  }

  if (QAbstractSpinBox *spin = focused_spin_box(focus_widget)) {
    // Spin-box drafts are not necessarily interpreted by losing focus from
    // their internal line edit. Commit the displayed value explicitly before
    // the surrounding editor publishes editingFinished.
    spin->interpretText();
    if (settled.editor == nullptr) {
      settled.relinquished = true;
      return settled;
    }
  }

  // Inspector line edits and item delegates publish their authored mutation
  // from FocusOut/editingFinished. Saving before that synchronous settlement
  // serializes the previous value and immediately leaves the project dirty.
  settled.editor->clearFocus();
  settled.relinquished =
      settled.editor == nullptr || QApplication::focusWidget() != settled.editor;
  return settled;
}

void restore_settled_editor_focus(const SettledEditorFocus &settled) {
  if (!settled.relinquished || settled.editor == nullptr ||
      settled.window == nullptr || QApplication::focusWidget() != nullptr ||
      QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr ||
      !settled.editor->isEnabled() || !settled.editor->isVisible() ||
      settled.editor->window() != settled.window ||
      !settled.window->isActiveWindow()) {
    return;
  }

  // A save can close or replace a delegate editor, move focus deliberately,
  // or open another input owner. Restore only the still-live editor in the
  // unchanged active window when no newer focus decision has been made.
  settled.editor->setFocus(Qt::OtherFocusReason);
  if (settled.line_editor == nullptr ||
      settled.line_editor->text() != settled.line_editor_text) {
    return;
  }
  if (settled.selection_start < 0) {
    settled.line_editor->setCursorPosition(settled.cursor_position);
    return;
  }
  if (settled.cursor_at_selection_start) {
    settled.line_editor->setSelection(
        settled.selection_start + settled.selection_length,
        -settled.selection_length);
    return;
  }
  settled.line_editor->setSelection(settled.selection_start,
                                    settled.selection_length);
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

bool qt_foreign_window_owns_command_input(const QWidget *editor_window) {
  if (editor_window == nullptr) {
    return false;
  }
  const auto foreign_widget_window = [editor_window](const QWidget *window) {
    return window != nullptr && window->isVisible() &&
           window != editor_window &&
           !(dynamic_cast<const QDockWidget *>(window) != nullptr &&
             editor_window->isAncestorOf(window));
  };
  if (const QWidget *focus_widget = QApplication::focusWidget();
      focus_widget != nullptr &&
      foreign_widget_window(focus_widget->window())) {
    // QWidget focus can arrive before the native activation notification.
    return true;
  }
  if (const QWindow *focus_window = QGuiApplication::focusWindow()) {
    // A createWindowContainer child is a QWindow input owner with the main
    // editor's native window in its parent chain. Do not require QWidget focus.
    for (const QWindow *current = focus_window; current != nullptr;
         current = current->parent()) {
      if (current == editor_window->windowHandle()) {
        return false;
      }
      if (current->handle() != nullptr) {
        if (const QWidget *widget = QWidget::find(current->winId())) {
          return foreign_widget_window(widget->window());
        }
      }
    }
    return true;
  }
  if (const QWidget *focus_widget = QApplication::focusWidget()) {
    return foreign_widget_window(focus_widget->window());
  }
  return foreign_widget_window(QApplication::activeWindow());
}

QtCommandEventFilter::QtCommandEventFilter(
    QuaderEditorApi &editor, QtViewportToolStateProvider tool_state_provider,
    QObject *parent)
    : QObject(parent), editor_(&editor),
      tool_state_provider_(std::move(tool_state_provider)) {}

void QtCommandEventFilter::rebind_editor(QuaderEditorApi &editor) noexcept {
  ++binding_generation_;
  editor_ = &editor;
  text_input_sequence_pending_ = false;
  asset_browser_sequence_pending_ = false;
  editor_->commands().reset_pending_keybinding_chord();
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

void QtCommandEventFilter::set_asset_browser_action_handler(
    QtAssetBrowserActionHandler handler) {
  asset_browser_action_handler_ = std::move(handler);
}

bool QtCommandEventFilter::eventFilter(QObject *watched, QEvent *event) {
  if (qt_handle_numeric_text_edit_key(watched, event)) {
    reset_pending_chord();
    return true;
  }
  if (editor_ == nullptr || event == nullptr) {
    return QObject::eventFilter(watched, event);
  }

  if (event->type() == QEvent::ShortcutOverride ||
      event->type() == QEvent::KeyRelease ||
      event->type() == QEvent::KeyPress) {
    const auto &key = *static_cast<QKeyEvent *>(event);
    const auto key_identity = quader_keyboard_event_key_identity(key);
    if (consumed_settings_keys_.contains(key_identity) &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyRelease || key.isAutoRepeat() ||
         key.nativeScanCode() != 0u || key.nativeVirtualKey() != 0u)) {
      if (event->type() == QEvent::KeyRelease && !key.isAutoRepeat()) {
        consumed_settings_keys_.erase(key_identity);
      }
      event->accept();
      return true;
    }
  }

  switch (event->type()) {
  case QEvent::ShortcutOverride: {
    if (modal_or_popup_owns_input() ||
        qt_application_command_input_owner_active(
            command_input_owner_provider_)) {
      break;
    }
    QWidget *focus = QApplication::focusWidget();
    const auto &key = *static_cast<QKeyEvent *>(event);
    const auto chord = quader_input_chord_from_qt_event(key);
    if (asset_browser_owns_input(focus)) {
      if (!chord.empty() &&
          editor_->commands().resolve_asset_browser_input_binding(
              chord, text_input_has_focus()).kind !=
              QuaderViewportKeybindingDispatch::Kind::NoMatch) {
        event->accept();
        return true;
      }
      break;
    }
    const bool tool_settings_focused =
        qt_object_belongs_to_tool_settings_panel(focus) ||
        (qt_object_belongs_to_face_texture_panel(focus) &&
         (text_input_has_focus() || focused_combo_box(focus) != nullptr));
    auto state = qt_command_dispatch_tool_state(
        tool_state_provider_, editor_->viewports().grid_preset());
    if (tool_settings_focused &&
        ((!chord.empty() &&
         editor_->commands().resolve_native_tool_settings_input_binding(
            chord, state)
                 .kind != QuaderNativeToolSettingsDispatch::Kind::NoMatch) ||
         suppress_settings_button_space(*editor_, focus, key))) {
      // Reserve the canonical mapping before Qt activates a shortcut/mnemonic;
      // only KeyPress advances the owner's sequence and executes the intent.
      event->accept();
      return true;
    }
    const bool text_input_active = text_input_has_focus();
    const bool outliner_focused = !text_input_active &&
        qt_application_command_input_owner_active(outliner_input_owner_provider_);
    const bool viewport_focused = !tool_settings_focused && !outliner_focused &&
        !text_input_active && viewport_surface_has_focus(watched);
    auto input_context = input_context_provider_ ? input_context_provider_()
        : QuaderViewportKeybindingContext{};
    input_context.outliner_focused = outliner_focused;
    input_context.editor_focused = !viewport_focused && !outliner_focused;
    input_context.asset_browser_focused = false;
    normalize_command_input_context(*editor_, state, input_context,
                                    viewport_focused, text_input_active, false);
    const bool continue_pending = text_input_active
        ? text_input_sequence_pending_ : !text_input_sequence_pending_;
    if (!chord.empty() &&
        editor_->commands().resolve_asset_browser_toggle_input_binding(
             chord, state, viewport_focused, text_input_active, input_context,
             continue_pending, asset_browser_sequence_pending_).kind !=
             QuaderViewportKeybindingDispatch::Kind::NoMatch) {
      event->accept();
      return true;
    }
    break;
  }
  case QEvent::ApplicationDeactivate:
    // Releases outside the application are not delivered to its adapters.
    consumed_settings_keys_.clear();
    reset_pending_chord();
    break;
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
    const bool asset_browser_focused = asset_browser_owns_input(focus_widget);
    const bool tool_settings_focused =
        qt_object_belongs_to_tool_settings_panel(focus_widget) ||
        (qt_object_belongs_to_face_texture_panel(focus_widget) &&
         (text_input_active || focused_combo_box(focus_widget) != nullptr));
    const bool outliner_focused =
        !asset_browser_focused && !text_input_active &&
        qt_application_command_input_owner_active(
                                  outliner_input_owner_provider_);
    const bool viewport_focused = !asset_browser_focused &&
                                 !tool_settings_focused && !outliner_focused &&
                                 !text_input_active &&
                                 viewport_surface_has_focus(watched);

    const std::optional<QuaderInputChord> chord =
        quader_pressed_input_chord_from_qt_event(
            *static_cast<QKeyEvent *>(event));
    if (!chord.has_value()) {
      return QObject::eventFilter(watched, event);
    }
    const auto &key_event = *static_cast<QKeyEvent *>(event);
    const auto reserve_native_release = [&]() {
      if (key_event.nativeScanCode() != 0u ||
          key_event.nativeVirtualKey() != 0u) {
        consumed_settings_keys_.insert(
            quader_keyboard_event_key_identity(key_event));
      }
    };

    ViewportToolState state = qt_command_dispatch_tool_state(
        tool_state_provider_, editor_->viewports().grid_preset());
    QuaderViewportKeybindingContext input_context =
        input_context_provider_ ? input_context_provider_()
                                : QuaderViewportKeybindingContext{};
    input_context.outliner_focused = outliner_focused;
    input_context.editor_focused = !viewport_focused && !outliner_focused;
    input_context.asset_browser_focused = asset_browser_focused;
    normalize_command_input_context(*editor_, state, input_context,
                                    viewport_focused, text_input_active,
                                    asset_browser_focused);
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
      reserve_native_release();
      if (*immediate_command == quader_command::kUiAssetBrowser.string()) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
      }
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

    if (tool_settings_focused) {
      const QuaderNativeToolSettingsDispatch tool_settings_dispatch = [&]() {
        QDR_PROFILE_SCOPE("Qt command resolve tool-settings keybinding");
        return editor_->commands()
            .dispatch_native_tool_settings_input_binding(*chord, state);
      }();
      if (tool_settings_dispatch.kind ==
          QuaderNativeToolSettingsDispatch::Kind::Intent) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
        QuaderEditorApi *const command_editor = editor_;
        const auto command_binding_generation = binding_generation_;
        if (QtToolSettingsPanel *panel =
                focused_tool_settings_panel(focus_widget)) {
          if (!panel->prepare_lifecycle_intent(
                  *command_editor, tool_settings_dispatch.intent) ||
              editor_ != command_editor ||
              binding_generation_ != command_binding_generation) {
            event->accept();
            return true;
          }
        } else if (tool_settings_dispatch.intent ==
            QuaderNativeToolSettingsIntent::Apply) {
          if (QAbstractSpinBox *spin = focused_spin_box(focus_widget)) {
            // Commit the visible text before Apply consumes Enter so the live
            // native-tool preview and its persistent setting match the field.
            spin->interpretText();
          }
        }
        if (editor_ != command_editor ||
            binding_generation_ != command_binding_generation) {
          event->accept();
          return true;
        }
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
              binding_generation_ == command_binding_generation &&
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
                     QuaderNativeToolSettingsDispatch::Kind::PendingChord ||
                 tool_settings_dispatch.kind ==
                     QuaderNativeToolSettingsDispatch::Kind::Consumed) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
        event->accept();
        return true;
      }
      if (suppress_settings_button_space(
              *editor_, focus_widget, *static_cast<QKeyEvent *>(event))) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
        event->accept();
        return true;
      }
    }

    const bool text_input_sequence_was_pending =
        text_input_active && text_input_sequence_pending_ &&
        editor_->commands().has_pending_keybinding_chord();
    if (text_input_active && !text_input_sequence_was_pending) {
      // A prefix started in another owner must not continue inside a text
      // editor. A sequence that began here remains authoritative so remapped
      // multi-chord document saves can complete.
      reset_pending_chord();
    } else if (!text_input_active && text_input_sequence_pending_) {
      reset_pending_chord();
    }
    if (!text_input_active) {
      // Hover can transfer command ownership without a Qt focus event. Clear
      // an outgoing viewport prefix before checking a panel continuation.
      editor_->commands().reset_pending_viewport_keybinding_chord();
    }
    const bool panel_sequence_pending =
        !text_input_active && editor_->commands().has_pending_keybinding_chord();
    const bool asset_browser_toggle_prefix =
        editor_->commands().resolve_asset_browser_toggle_input_binding(
            *chord, state, false, text_input_active, input_context).kind ==
            QuaderViewportKeybindingDispatch::Kind::PendingChord;
    QuaderViewportKeybindingDispatch dispatch = [&]() {
      QDR_PROFILE_SCOPE("Qt command resolve keybinding");
      return editor_->commands().dispatch_viewport_input_binding(
          *chord, state, false, text_input_active,
          input_context);
    }();
    text_input_sequence_pending_ =
        text_input_active &&
        dispatch.kind ==
            QuaderViewportKeybindingDispatch::Kind::PendingChord;
    asset_browser_sequence_pending_ = asset_browser_toggle_prefix &&
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord;
    if (text_input_sequence_was_pending &&
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::NoMatch) {
      // The prefix already consumed the sequence. Do not replay a mismatched
      // continuation into the editor as unrelated text.
      reserve_native_release();
      event->accept();
      return true;
    }
    if (panel_sequence_pending &&
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::NoMatch) {
      // A mismatched continuation belongs to the sequence that consumed its
      // prefix. Do not replay it as a fresh command through a parent widget or
      // the still-focused viewport's deferred input queue.
      reserve_native_release();
      event->accept();
      return true;
    }

    const bool asset_browser_toggle =
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::Command &&
        dispatch.command_id == quader_command::kUiAssetBrowser.string();
    const bool asset_browser_prefix =
        (asset_browser_focused || asset_browser_toggle_prefix) &&
        dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord;
    if (!asset_browser_toggle && !asset_browser_prefix &&
        focused_widget_owns_key_press(
            focus_widget, *static_cast<QKeyEvent *>(event), outliner_focused)) {
      // Preserve the documented key behavior of the focused Qt control,
      // including item-view Ctrl+A, range navigation, and activation.
      reset_pending_chord();
      return QObject::eventFilter(watched, event);
    }
    switch (dispatch.kind) {
    case QuaderViewportKeybindingDispatch::Kind::PendingChord:
      reserve_native_release();
      if (asset_browser_focused) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
      }
      event->accept();
      return true;
    case QuaderViewportKeybindingDispatch::Kind::Command: {
      reserve_native_release();
      if (asset_browser_focused &&
          is_asset_browser_edit_command(dispatch.command_id)) {
        // Asset commands share the canonical mappings but have independent
        // selection, clipboard and filesystem owners. Missing widget handlers
        // must never turn these keys into edits on the active scene.
        if (asset_browser_action_handler_) {
          (void)asset_browser_action_handler_(
              QuaderCommandId{dispatch.command_id});
        }
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
        event->accept();
        return true;
      }
      if (asset_browser_toggle) {
        consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
            *static_cast<QKeyEvent *>(event)));
      }
      const std::optional<SettledEditorFocus> settled_focus =
          text_input_active &&
                  is_scene_document_save_command(dispatch.command_id)
              ? std::optional<SettledEditorFocus>{
                    settle_focused_editor(focus_widget)}
              : std::nullopt;
      const QuaderApiVoidResult result = [&]() {
        QDR_PROFILE_SCOPE("Qt command execute keybinding");
        return editor_->commands().execute(dispatch.command_id);
      }();
      if (settled_focus.has_value()) {
        restore_settled_editor_focus(*settled_focus);
      }
      if (!result.ok) {
        LOG_ERROR("Qt keybinding command failed: {}",
                  result.message.empty() ? "Command failed." : result.message);
      }
      event->accept();
      return true;
    }
    case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut: {
      QuaderEditorApi *const command_editor = editor_;
      const std::optional<QuaderApiVoidResult> result = [&]()
          -> std::optional<QuaderApiVoidResult> {
        switch (dispatch.shortcut.kind) {
        case ViewportShortcutKind::SetSelectionMode:
          return command_editor->selection().set_mode(to_api_selection_mode(
              viewport_selection_mode_to_app(dispatch.shortcut.selection_mode)));
        case ViewportShortcutKind::SetGridPreset:
          return command_editor->viewports().set_grid_preset(
              dispatch.shortcut.grid_preset);
        case ViewportShortcutKind::CancelActiveTool: {
          consumed_settings_keys_.insert(quader_keyboard_event_key_identity(
              *static_cast<QKeyEvent *>(event)));
          const auto pane = command_editor->viewports().focused_pane();
          if (!command_editor->tools().pane_owns_executing_tool(pane)) {
            return command_editor->viewports().record_selected_tool(
                pane, ViewportToolId::Select);
          }
          return command_editor->tools().cancel_modal_tool();
        }
        default:
          return std::nullopt;
        }
      }();
      if (!result.has_value()) {
        break;
      }
      reserve_native_release();
      if (!result->ok()) {
        LOG_ERROR("Qt editor keybinding intent failed: {}",
                  result->error_message());
        if (editor_ == command_editor) {
          (void)command_editor->ui().notify(
              {.level = QuaderNotificationLevel::Error,
               .message = result->error_message()});
        }
      }
      event->accept();
      return true;
    }
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
  if (QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr) {
    return true;
  }
  // A popup may already own QWidget focus before the platform publishes its
  // active-popup registration. Preserve its scoped input in either ordering.
  const QWidget *focus = QApplication::focusWidget();
  const QWidget *window = focus != nullptr ? focus->window() : nullptr;
  return window != nullptr && window->isVisible() &&
         (window->windowType() == Qt::Popup || window->isModal());
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
  text_input_sequence_pending_ = false;
  asset_browser_sequence_pending_ = false;
  if (editor_ != nullptr && editor_->commands().has_pending_keybinding_chord()) {
    // Dialog/popup adapters own their own scope loss. Yielding the application
    // filter must not erase a prefix before that adapter receives its suffix.
    editor_->commands().reset_pending_non_modal_keybinding_chord();
  }
}

} // namespace quader
