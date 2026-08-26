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
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

struct BoundContextCommand final {
  QAction *action = nullptr;
  std::string command_id;
  std::string source_id;
};

QString localized_action_label(const QuaderActionItem &action,
                               const LocalizationService &localization) {
  std::string key = "ui.viewport.context_menu.action.";
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
      action.disabled_reason_key, action.disabled_reason, {{"count", count}}));
}

QString with_shortcut(QString label, std::string_view shortcut) {
  if (!shortcut.empty()) {
    QString display_shortcut = QString::fromUtf8(
        shortcut.data(), static_cast<qsizetype>(shortcut.size()));
    display_shortcut.replace(QStringLiteral("RightMouseClick"),
                             QStringLiteral("RMouse"));
    display_shortcut.replace(QStringLiteral("LeftMouseClick"),
                             QStringLiteral("LMouse"));
    label.append(QLatin1Char('\t'));
    label.append(display_shortcut);
  }
  return label;
}

std::pair<std::string_view, std::string_view> modeling_section_label(
    ViewportContextMenuModelingSection section) {
  switch (section) {
  case ViewportContextMenuModelingSection::Mesh:
    return {"ui.viewport.context_menu.section.mesh", "Mesh"};
  case ViewportContextMenuModelingSection::Vertex:
    return {"ui.viewport.context_menu.section.vertex", "Vertex"};
  case ViewportContextMenuModelingSection::Edge:
    return {"ui.viewport.context_menu.section.edge", "Edge"};
  case ViewportContextMenuModelingSection::Face:
    return {"ui.viewport.context_menu.section.face", "Face"};
  }
  return {"ui.viewport.context_menu.section.mesh", "Mesh"};
}

bool has_modeling_bucket_actions(const QuaderActionSet &action_set,
                                 QuaderSelectionMode mode) {
  for (const QuaderActionPanel &panel : action_set.panels) {
    for (const QuaderActionItem &action : panel.actions) {
      if (action.visibility != QuaderActionVisibility::Hidden &&
          viewport_context_menu_action_bucket(mode, action.action_id) ==
              ViewportContextMenuActionBucket::Modeling) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

void QtViewportContextMenuPresenter::show(
    QWidget &parent, const QPoint &global_point, QuaderEditorApi &editor,
    ViewportInputRouter &input_router, LocalizationService &localization,
    const QuaderActionSet &action_set, QuaderSelectionMode selection_mode,
    const std::optional<ProjectAssetId> &material_under_mouse,
    const std::function<void(ProjectAssetId)> &open_material_editor) const {
  QDR_PROFILE_SCOPE("Qt viewport context menu show");

  QMenu menu(&parent);
  std::vector<BoundContextCommand> bound_commands;
  QAction *set_material_action = nullptr;
  QAction *edit_material_action = nullptr;

  const auto add_catalog_action =
      [&](QMenu &owner, const QuaderActionItem &action) {
        const std::string command_id =
            action.kind == QuaderActionKind::Tool
                ? editor.commands().command_id_for_tool_activation(
                      action.tool_id)
                : editor.commands().command_id_for_modeling_action(
                      action.action_id);
        QAction *menu_action = owner.addAction(with_shortcut(
            localized_action_label(action, localization),
            editor.commands().shortcut_for(command_id)));
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
        bound_commands.push_back(
            {.action = menu_action,
             .command_id = command_id,
             .source_id = action.action_id});
        return menu_action;
      };

  const auto add_command_action =
      [&](QMenu &owner, std::string_view localization_key,
          std::string_view fallback, QuaderCommandId command_id,
          std::string_view object_name) {
        const std::optional<QuaderCommandDescriptor> descriptor =
            editor.commands().descriptor(command_id);
        const std::string shortcut =
            descriptor.has_value() ? descriptor->shortcut
                                   : editor.commands().shortcut_for(command_id);
        QAction *action = owner.addAction(with_shortcut(
            QString::fromStdString(
                localization.text(localization_key, fallback)),
            shortcut));
        action->setObjectName(QString::fromUtf8(
            object_name.data(), static_cast<qsizetype>(object_name.size())));
        action->setEnabled(descriptor.has_value() && descriptor->enabled);
        bound_commands.push_back(
            {.action = action,
             .command_id = std::string(command_id.string()),
             .source_id = std::string(object_name)});
        return action;
      };

  {
    QDR_PROFILE_SCOPE("Qt viewport context menu build");
    menu.setObjectName(QStringLiteral("viewport_operations_context_menu"));
    menu.setStyleSheet(QStringLiteral(
        "QMenu#viewport_operations_context_menu::separator {"
        "height: 1px; background: palette(mid); margin: 4px 8px; }"));
    menu.setToolTipsVisible(true);

    bool has_mode_section = false;
    const std::optional<ViewportContextMenuModelingSection> modeling_section =
        viewport_context_menu_modeling_section(selection_mode);
    if (modeling_section.has_value() &&
        has_modeling_bucket_actions(action_set, selection_mode)) {
      const auto [label_key, fallback] =
          modeling_section_label(*modeling_section);
      QMenu *modeling_menu = menu.addMenu(QString::fromStdString(
          localization.text(label_key, fallback)));
      modeling_menu->setObjectName(
          QStringLiteral("viewport_context_modeling_submenu"));
      modeling_menu->setToolTipsVisible(true);
      for (const QuaderActionPanel &panel : action_set.panels) {
        for (const QuaderActionItem &action : panel.actions) {
          if (action.visibility == QuaderActionVisibility::Hidden ||
              viewport_context_menu_action_bucket(selection_mode,
                                                  action.action_id) !=
                  ViewportContextMenuActionBucket::Modeling) {
            continue;
          }
          (void)add_catalog_action(*modeling_menu, action);
        }
      }
      has_mode_section = true;
    }

    if (selection_mode == QuaderSelectionMode::Object ||
        selection_mode == QuaderSelectionMode::Group) {
      QMenu *object_menu = menu.addMenu(QString::fromStdString(
          localization.text("ui.viewport.context_menu.section.object",
                            "Object")));
      object_menu->setObjectName(
          QStringLiteral("viewport_context_object_submenu"));
      object_menu->setToolTipsVisible(true);
      (void)add_command_action(
          *object_menu, "ui.viewport.context_menu.command.duplicate_object",
          "Duplicate Object", quader_command::kDuplicateSelection,
          "edit.duplicate");
      if (selection_mode == QuaderSelectionMode::Object) {
        (void)add_command_action(*object_menu, "ui.menu.edit.group", "Group",
                                 quader_command::kGroupSelection,
                                 "edit.group");
      } else {
        (void)add_command_action(*object_menu, "ui.menu.edit.ungroup",
                                 "Ungroup", quader_command::kUngroupSelection,
                                 "edit.ungroup");
      }
      (void)add_command_action(*object_menu, "ui.menu.edit.create_instance",
                               "Create Instance",
                               quader_command::kEditCreateInstance,
                               "edit.create_instance");
      (void)add_command_action(*object_menu, "ui.menu.edit.flatten_instance",
                               "Flatten Instance",
                               quader_command::kEditFlattenInstance,
                               "edit.flatten_instance");
      for (const QuaderActionPanel &panel : action_set.panels) {
        for (const QuaderActionItem &action : panel.actions) {
          if (action.visibility != QuaderActionVisibility::Hidden &&
              viewport_context_menu_action_bucket(selection_mode,
                                                  action.action_id) ==
                  ViewportContextMenuActionBucket::Object) {
            (void)add_catalog_action(*object_menu, action);
          }
        }
      }
      (void)add_command_action(
          *object_menu, "ui.layers.remove_selection",
          "Remove Object from Layer",
          quader_command::kSceneLayersRemoveSelection,
          "scene.layers.remove_selection");
      has_mode_section = true;
    }

    if (has_mode_section) {
      menu.addSeparator();
    }
    (void)add_command_action(
        menu, "ui.viewport.context_menu.command.center_selection",
        "Center View on Selection", quader_command::kViewportFrameSelection,
        "viewport.frame_selection");

    const std::string sample_shortcut = editor.commands().shortcut_for(
        "viewport.material.sample_face");
    set_material_action = menu.addAction(with_shortcut(
        QString::fromStdString(localization.text(
            "ui.viewport.context_menu.command.set_material_under_mouse",
            "Set Material under Mouse as Active")),
        sample_shortcut));
    set_material_action->setObjectName(
        QStringLiteral("viewport.material.sample_face"));
    set_material_action->setEnabled(material_under_mouse.has_value());

    edit_material_action = menu.addAction(QString::fromStdString(
        localization.text(
            "ui.viewport.context_menu.command.edit_material_under_mouse",
            "Edit Material under Mouse")));
    edit_material_action->setObjectName(
        QStringLiteral("viewport.material.edit_under_mouse"));
    edit_material_action->setEnabled(material_under_mouse.has_value() &&
                                     static_cast<bool>(open_material_editor));

    menu.addSeparator();
    (void)add_command_action(menu, "ui.menu.edit.cut", "Cut",
                             quader_command::kCutSelection, "edit.cut");
    (void)add_command_action(menu, "ui.menu.edit.copy", "Copy",
                             quader_command::kCopySelection, "edit.copy");
    (void)add_command_action(menu, "ui.menu.edit.paste", "Paste",
                             quader_command::kPasteSelection, "edit.paste");
  }

  QAction *selected_action = nullptr;
  {
    QDR_PROFILE_SCOPE("Qt viewport context menu exec");
    selected_action = menu.exec(global_point);
  }
  if (selected_action == nullptr) {
    return;
  }

  if (selected_action == set_material_action) {
    if (!material_under_mouse.has_value()) {
      return;
    }
    const QuaderApiVoidResult result =
        editor.project_assets().set_active_material(*material_under_mouse);
    if (!result.ok()) {
      LOG_ERROR("Material-under-mouse action failed: {}",
                result.error_message().empty() ? "Operation failed."
                                               : result.error_message());
      return;
    }
    return;
  }
  if (selected_action == edit_material_action &&
      material_under_mouse.has_value() && open_material_editor) {
    open_material_editor(*material_under_mouse);
    return;
  }

  const auto selected = std::find_if(
      bound_commands.begin(), bound_commands.end(),
      [selected_action](const BoundContextCommand &entry) {
        return entry.action == selected_action;
      });
  if (selected == bound_commands.end()) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("Qt viewport context menu dispatch command");
    const QuaderApiVoidResult result =
        editor.commands().execute(selected->command_id);
    if (!result.ok()) {
      LOG_ERROR("Context command '{}' failed: {}", selected->source_id,
                result.error_message().empty() ? "Operation failed."
                                               : result.error_message());
    }
    input_router.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
  }
}

} // namespace quader
