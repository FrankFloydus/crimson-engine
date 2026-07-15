////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_context_menu.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool is_generic_context_action(std::string_view action_id) {
  return action_id == "delete_selection" || action_id == "duplicate_meshes";
}

QString localized_action_label(const QuaderActionItem &action,
                               const LocalizationService &localization) {
  std::string key = "modeling.action.";
  key.append(action.action_id);
  key.append(".label");
  return QString::fromStdString(localization.text(key, action.label));
}

QString localized_disabled_reason(const QuaderActionItem &action,
                                  const LocalizationService &localization) {
  if (action.disabled_reason_key.empty()) {
    return QString::fromStdString(action.disabled_reason);
  }
  if (action.disabled_reason_count == 0U) {
    return QString::fromStdString(localization.text(
        action.disabled_reason_key, action.disabled_reason));
  }
  const std::string count = std::to_string(action.disabled_reason_count);
  return QString::fromStdString(localization.format(
      action.disabled_reason_key, action.disabled_reason,
      {{"count", count}}));
}

} // namespace

void QtViewportContextMenuPresenter::show(
    QWidget &parent, const QPoint &global_point, QuaderEditorApi &editor,
    ViewportInputRouter &input_router, LocalizationService &localization,
    bool modal_tool_owns_viewport) const {
  const QuaderActionQuery query{
      .surface = QuaderActionSurface::ViewportContextMenu,
      .modal_tool_owns_viewport = modal_tool_owns_viewport,
      .include_disabled = true,
      .include_hidden = false,
  };
  const std::vector<QuaderActionItem> actions =
      editor.modeling().available_actions(query);

  QMenu menu(&parent);
  menu.setObjectName(QStringLiteral("viewport_operations_context_menu"));
  menu.setToolTipsVisible(true);

  QAction *create_instance_action = menu.addAction(QString::fromStdString(
      localization.text("ui.menu.edit.create_instance", "Create Instance")));
  create_instance_action->setObjectName(QStringLiteral("create_instance"));
  create_instance_action->setEnabled(editor.modeling().can_create_instance());
  QAction *flatten_instance_action = menu.addAction(QString::fromStdString(
      localization.text("ui.menu.edit.flatten_instance", "Flatten Instance")));
  flatten_instance_action->setObjectName(QStringLiteral("flatten_instance"));
  flatten_instance_action->setEnabled(
      editor.modeling().can_flatten_instance());
  menu.addSeparator();

  std::vector<std::pair<QAction *, const QuaderActionItem *>> menu_actions;
  menu_actions.reserve(actions.size());
  bool separated_generic_actions = false;
  for (const QuaderActionItem &action : actions) {
    if (action.visibility == QuaderActionVisibility::Hidden) {
      continue;
    }
    if (!separated_generic_actions &&
        is_generic_context_action(action.action_id)) {
      menu.addSeparator();
      separated_generic_actions = true;
    }

    QAction *menu_action =
        menu.addAction(localized_action_label(action, localization));
    menu_action->setObjectName(QString::fromStdString(action.action_id));
    const bool enabled =
        action.visibility == QuaderActionVisibility::Enabled;
    menu_action->setEnabled(enabled);
    if (!enabled) {
      const QString reason = localized_disabled_reason(action, localization);
      if (!reason.isEmpty()) {
        menu_action->setToolTip(reason);
      }
    }
    menu_actions.emplace_back(menu_action, &action);
  }

  QAction *selected_action = menu.exec(global_point);
  if (selected_action == nullptr) {
    return;
  }
  if (selected_action == create_instance_action ||
      selected_action == flatten_instance_action) {
    const QuaderCommandId command =
        selected_action == create_instance_action
            ? quader_command::kEditCreateInstance
            : quader_command::kEditFlattenInstance;
    const QuaderApiVoidResult result = editor.commands().execute(command);
    if (!result.ok()) {
      LOG_ERROR("Instance command '{}' failed: {}", command.string(),
                result.error_message().empty() ? "Operation failed."
                                               : result.error_message());
    }
    input_router.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
    return;
  }
  const auto selected = std::find_if(
      menu_actions.begin(), menu_actions.end(),
      [selected_action](const auto &entry) {
        return entry.first == selected_action;
      });
  if (selected == menu_actions.end() || selected->second == nullptr) {
    return;
  }

  const QuaderActionItem &action = *selected->second;
  const std::string command_id =
      action.kind == QuaderActionKind::Tool
          ? editor.commands().command_id_for_tool_activation(action.tool_id)
          : editor.commands().command_id_for_modeling_action(action.action_id);
  const QuaderApiVoidResult result = editor.commands().execute(command_id);
  if (!result.ok()) {
    LOG_ERROR("Operation '{}' failed: {}", action.action_id,
              result.error_message().empty() ? "Operation failed."
                                             : result.error_message());
  }
  input_router.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
}

} // namespace quader
