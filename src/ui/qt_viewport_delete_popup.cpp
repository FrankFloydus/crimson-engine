////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_delete_popup.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPoint>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace quader {
namespace {

QString localized(const LocalizationService &localization,
                  std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(std::string(key),
                                                   std::string(fallback)));
}

QuaderApiVoidResult execute_delete_popup_command(
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    QuaderCommandId command_id) {
  const QuaderApiVoidResult result = editor.commands().execute(command_id);
  if (!result.ok()) {
    LOG_ERROR("Delete popup command '{}' failed: {}", command_id.string(),
              result.error_message().empty() ? "Operation failed."
                                             : result.error_message());
  }
  input_router.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  return result;
}

void restore_viewport_focus(const QPointer<QWidget> &focus_parent) {
  if (focus_parent != nullptr) {
    focus_parent->setFocus(Qt::ShortcutFocusReason);
  }
}

bool show_object_confirmation(QWidget &parent, QuaderEditorApi &editor,
                              ViewportInputRouter &input_router,
                              LocalizationService &localization) {
  QMessageBox confirmation(QMessageBox::NoIcon,
                           localized(localization,
                                     "ui.viewport.delete_popup.window_title",
                                     "Delete"),
                           localized(localization,
                                     "ui.viewport.delete_popup.title",
                                     "Delete Selected Objects"),
                           QMessageBox::NoButton, &parent);
  confirmation.setObjectName(
      QStringLiteral("viewport_delete_objects_confirmation"));
  confirmation.setWindowModality(Qt::WindowModal);
  QPushButton *delete_button = confirmation.addButton(
      localized(localization, "ui.viewport.delete_popup.delete", "Delete"),
      QMessageBox::AcceptRole);
  QPushButton *cancel_button = confirmation.addButton(
      localized(localization, "ui.viewport.delete_popup.cancel", "Cancel"),
      QMessageBox::RejectRole);
  confirmation.setEscapeButton(cancel_button);
  confirmation.exec();
  if (confirmation.clickedButton() != delete_button) {
    return false;
  }
  return execute_delete_popup_command(editor, input_router,
                                      quader_command::kDeleteSelection)
      .changed();
}

bool show_component_menu(QWidget &parent, const QPoint &global_point,
                         QuaderEditorApi &editor,
                         ViewportInputRouter &input_router,
                         LocalizationService &localization) {
  const ModelingSelectionSummary summary =
      to_modeling_selection_summary(editor.selection().summary());
  const std::array<QtViewportDeletePopupAction, 5> actions =
      qt_viewport_delete_popup_actions(summary);

  QMenu menu(&parent);
  menu.setObjectName(QStringLiteral("viewport_delete_elements_menu"));
  std::array<QAction *, 5> qt_actions{};
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const QtViewportDeletePopupAction &action = actions[index];
    QAction *qt_action = menu.addAction(localized(
        localization, action.label_key, action.fallback_label));
    qt_action->setObjectName(QString::fromUtf8(action.id.data(),
                                                action.id.size()));
    qt_action->setEnabled(action.enabled);
    qt_actions[index] = qt_action;
  }

  QAction *selected_action = menu.exec(global_point);
  const auto selected =
      std::find(qt_actions.begin(), qt_actions.end(), selected_action);
  if (selected == qt_actions.end()) {
    return false;
  }
  const std::size_t index =
      static_cast<std::size_t>(std::distance(qt_actions.begin(), selected));
  return execute_delete_popup_command(editor, input_router,
                                      actions[index].command_id)
      .changed();
}

} // namespace

QtViewportDeletePopupKind qt_viewport_delete_popup_kind(
    ModelingSelectionMode mode, bool has_selected_scene_root) {
  if (mode == ModelingSelectionMode::Object ||
      mode == ModelingSelectionMode::Group || has_selected_scene_root) {
    return QtViewportDeletePopupKind::ObjectConfirmation;
  }
  return QtViewportDeletePopupKind::ComponentMenu;
}

std::array<QtViewportDeletePopupAction, 5>
qt_viewport_delete_popup_actions(const ModelingSelectionSummary &summary) {
  const bool vertices = summary.mode == ModelingSelectionMode::Vertex &&
                        summary.selected_vertex_count > 0U;
  const bool edges = summary.mode == ModelingSelectionMode::Edge &&
                     summary.selected_edge_count > 0U;
  const bool faces = summary.mode == ModelingSelectionMode::Face &&
                     summary.selected_face_count > 0U;
  return {{
      {.id = "delete_vertices",
       .label_key = "ui.viewport.delete_vertices",
       .fallback_label = "Delete Vertices",
       .command_id = quader_command::kDeleteSelection,
       .enabled = vertices},
      {.id = "delete_face",
       .label_key = "ui.viewport.delete_face",
       .fallback_label = "Delete Face",
       .command_id = quader_command::kDeleteSelection,
       .enabled = faces},
      {.id = "delete_edges",
       .label_key = "ui.viewport.delete_edges",
       .fallback_label = "Delete Edges",
       .command_id = quader_command::kDeleteSelection,
       .enabled = edges},
      {.id = "dissolve_edges",
       .label_key = "ui.viewport.dissolve_edges",
       .fallback_label = "Dissolve Edges",
       .command_id = quader_command::kModelingDissolveEdges,
       .enabled = edges},
      {.id = "dissolve_vertices",
       .label_key = "ui.viewport.dissolve_vertex",
       .fallback_label = "Dissolve Vertex",
       .command_id = quader_command::kModelingDissolveVertices,
       .enabled = vertices},
  }};
}

bool QtViewportDeletePopupPresenter::show(
    QWidget &focus_parent, const QPoint &global_point,
    QuaderEditorApi &editor, ModelingSessionService &modeling,
    ViewportInputRouter &input_router,
    LocalizationService &localization) const {
  const QPointer<QWidget> guarded_focus_parent(&focus_parent);
  const ModelingSelectionSummary summary =
      to_modeling_selection_summary(editor.selection().summary());
  const QtViewportDeletePopupKind kind = qt_viewport_delete_popup_kind(
      summary.mode, modeling.has_selected_scene_aware_delete_root());
  const bool changed = kind == QtViewportDeletePopupKind::ObjectConfirmation
                           ? show_object_confirmation(focus_parent, editor,
                                                      input_router,
                                                      localization)
                           : show_component_menu(focus_parent, global_point,
                                                 editor, input_router,
                                                 localization);
  restore_viewport_focus(guarded_focus_parent);
  return changed;
}

} // namespace quader
