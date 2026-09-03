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
#include "editor/metadata/command_presentation_catalog.hpp"
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

bool same_context_revision(const QuaderRevisionStamp &left,
                           const QuaderRevisionStamp &right) {
  return left.project_revision == right.project_revision &&
         left.scene_revision == right.scene_revision &&
         left.scene_layer_revision == right.scene_layer_revision &&
         left.asset_revision == right.asset_revision &&
         left.modeling_content_revision == right.modeling_content_revision &&
         left.modeling_selection_revision ==
             right.modeling_selection_revision;
}

QString localized_action_label(const QuaderActionItem &action,
                               QuaderSelectionMode mode,
                               const LocalizationService &localization) {
  if (action.action_id == "duplicate_meshes") {
    return QString::fromStdString(localization.text(
        "ui.viewport.context_menu.command.duplicate_mesh_data",
        "Duplicate Mesh Data"));
  }
  if (action.action_id == "delete_selection") {
    switch (mode) {
    case QuaderSelectionMode::Object:
      return QString::fromStdString(localization.text(
          "ui.viewport.context_menu.command.delete_objects", "Delete Objects"));
    case QuaderSelectionMode::Group:
      return QString::fromStdString(localization.text(
          "ui.viewport.context_menu.command.delete_groups", "Delete Groups"));
    case QuaderSelectionMode::Vertex:
      return QString::fromStdString(
          localization.text("ui.viewport.context_menu.command.delete_vertices",
                            "Delete Vertices"));
    case QuaderSelectionMode::Edge:
      return QString::fromStdString(localization.text(
          "ui.viewport.context_menu.command.delete_edges", "Delete Edges"));
    case QuaderSelectionMode::Face:
      return QString::fromStdString(localization.text(
          "ui.viewport.context_menu.command.delete_faces", "Delete Faces"));
    }
  }

  std::string popup_key = "ui.viewport.context_menu.action.";
  popup_key.append(action.action_id);
  popup_key.append(".label");
  const std::string compact_fallback =
      localization.text(popup_key, action.label);

  // First-level actions no longer inherit an implicit Mesh/Edge/Face label
  // from a catch-all submenu. Reuse the canonical, localized operation label
  // so operands remain explicit wherever the action is projected.
  std::string operation_key = "modeling.action.";
  operation_key.append(action.action_id);
  operation_key.append(".label");
  return QString::fromStdString(
      localization.text(operation_key, compact_fallback));
}

QString localized_disabled_reason(const QuaderActionItem &action,
                                  const LocalizationService &localization) {
  if (action.disabled_reason_key.empty()) {
    return QString::fromStdString(action.disabled_reason);
  }
  if (action.disabled_reason_count == 0U) {
    return QString::fromStdString(
        localization.text(action.disabled_reason_key, action.disabled_reason));
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

std::pair<std::string_view, std::string_view>
action_section_label(ViewportContextMenuActionSection section) {
  switch (section) {
  case ViewportContextMenuActionSection::MeshData:
    return {"ui.viewport.context_menu.section.mesh_data", "Mesh Data"};
  case ViewportContextMenuActionSection::Topology:
    return {"ui.viewport.context_menu.section.topology", "Topology"};
  case ViewportContextMenuActionSection::Merge:
    return {"ui.viewport.context_menu.section.merge", "Merge"};
  case ViewportContextMenuActionSection::Normals:
    return {"ui.viewport.context_menu.section.normals", "Normals and Shading"};
  case ViewportContextMenuActionSection::Arrange:
    return {"ui.viewport.context_menu.section.arrange", "Arrange"};
  case ViewportContextMenuActionSection::Primary:
  case ViewportContextMenuActionSection::Destructive:
  case ViewportContextMenuActionSection::Suppressed:
    break;
  }
  return {"ui.viewport.context_menu.section.topology", "Topology"};
}

std::vector<const QuaderActionItem *>
catalog_actions_for_section(const QuaderActionSet &action_set,
                            QuaderSelectionMode mode, QuaderEditorApi &editor,
                            ViewportContextMenuActionSection section) {
  const auto presentation_section = [&](const QuaderActionItem &action) {
    const ViewportContextMenuActionDisposition disposition =
        viewport_context_menu_action_disposition(mode, action.action_id);
    if (disposition == ViewportContextMenuActionDisposition::Primary) {
      return ViewportContextMenuActionSection::Primary;
    }
    if (disposition == ViewportContextMenuActionDisposition::Suppressed) {
      return ViewportContextMenuActionSection::Suppressed;
    }

    const std::string command_id =
        action.kind == QuaderActionKind::Tool
            ? editor.commands().command_id_for_tool_activation(action.tool_id)
            : editor.commands().command_id_for_modeling_action(
                  action.action_id);
    const CommandPresentationDescriptor *presentation =
        find_command_presentation(command_id);
    if (presentation == nullptr) {
      return ViewportContextMenuActionSection::Topology;
    }
    if (presentation->danger == CommandPresentationDanger::Destructive) {
      return ViewportContextMenuActionSection::Destructive;
    }
    switch (presentation->group) {
    case CommandPresentationGroup::Duplication:
    case CommandPresentationGroup::Instances:
      return ViewportContextMenuActionSection::MeshData;
    case CommandPresentationGroup::Merge:
      return ViewportContextMenuActionSection::Merge;
    case CommandPresentationGroup::Normals:
    case CommandPresentationGroup::Shading:
      return ViewportContextMenuActionSection::Normals;
    case CommandPresentationGroup::Transform:
    case CommandPresentationGroup::Arrangement:
      return ViewportContextMenuActionSection::Arrange;
    default:
      return ViewportContextMenuActionSection::Topology;
    }
  };
  const auto presentation_rank = [&](const QuaderActionItem *action) {
    if (viewport_context_menu_action_disposition(mode, action->action_id) ==
        ViewportContextMenuActionDisposition::Primary) {
      return viewport_context_menu_primary_rank(mode, action->action_id);
    }
    const std::string command_id =
        action->kind == QuaderActionKind::Tool
            ? editor.commands().command_id_for_tool_activation(action->tool_id)
            : editor.commands().command_id_for_modeling_action(
                  action->action_id);
    const CommandPresentationDescriptor *presentation =
        find_command_presentation(command_id);
    return presentation != nullptr ? presentation->rank : 500U;
  };

  std::vector<const QuaderActionItem *> result;
  for (const QuaderActionPanel &panel : action_set.panels) {
    for (const QuaderActionItem &action : panel.actions) {
      if (action.visibility == QuaderActionVisibility::Hidden ||
          presentation_section(action) != section) {
        continue;
      }
      result.push_back(&action);
    }
  }
  std::ranges::stable_sort(result, {}, presentation_rank);
  return result;
}

void add_separator_after_content(QMenu &menu) {
  const QList<QAction *> actions = menu.actions();
  if (!actions.empty() && actions.back() != nullptr &&
      !actions.back()->isSeparator()) {
    menu.addSeparator();
  }
}

} // namespace

void QtViewportContextMenuPresenter::show(
    QWidget &parent, const QPoint &global_point, QuaderEditorApi &editor,
    ViewportInputRouter &input_router, LocalizationService &localization,
    const QuaderActionSet &action_set, const QuaderSelectionSummary &selection,
    bool target_hit, const std::optional<ProjectAssetId> &material_under_mouse,
    const std::function<void(ProjectAssetId)> &open_material_editor,
    ViewportRuntimeBindingToken runtime_binding) const {
  QDR_PROFILE_SCOPE("Qt viewport context menu show");

  // QMenu::exec() runs a nested event loop. Capture the complete public
  // editor context used to build this menu so an action cannot later execute
  // against a selection, document, layer, or asset state different from the
  // one the user inspected.
  const QuaderRevisionStamp presented_context = editor.revisions();

  QMenu menu(&parent);
  std::vector<BoundContextCommand> bound_commands;
  QAction *set_material_action = nullptr;
  QAction *edit_material_action = nullptr;

  const auto add_catalog_action = [&](QMenu &owner,
                                      const QuaderActionItem &action) {
    const std::string command_id =
        action.kind == QuaderActionKind::Tool
            ? editor.commands().command_id_for_tool_activation(action.tool_id)
            : editor.commands().command_id_for_modeling_action(
                  action.action_id);
    QAction *menu_action = owner.addAction(with_shortcut(
        localized_action_label(action, selection.mode, localization),
        editor.commands().shortcut_for(command_id)));
    menu_action->setObjectName(QString::fromStdString(action.action_id));
    const bool enabled = action.visibility == QuaderActionVisibility::Enabled;
    menu_action->setEnabled(enabled);
    if (!enabled) {
      const QString reason = localized_disabled_reason(action, localization);
      if (!reason.isEmpty()) {
        menu_action->setToolTip(reason);
      }
    }
    bound_commands.push_back({.action = menu_action,
                              .command_id = command_id,
                              .source_id = action.action_id});
    return menu_action;
  };

  const auto add_command_action = [&](QMenu &owner,
                                      std::string_view localization_key,
                                      std::string_view fallback,
                                      QuaderCommandId command_id,
                                      std::string_view object_name,
                                      bool omit_when_disabled = false) {
    const std::optional<QuaderCommandDescriptor> descriptor =
        editor.commands().descriptor(command_id);
    if (omit_when_disabled &&
        (!descriptor.has_value() || !descriptor->enabled)) {
      return static_cast<QAction *>(nullptr);
    }
    const std::string shortcut =
        descriptor.has_value() ? descriptor->shortcut
                               : editor.commands().shortcut_for(command_id);
    QAction *action = owner.addAction(with_shortcut(
        QString::fromStdString(localization.text(localization_key, fallback)),
        shortcut));
    action->setObjectName(QString::fromUtf8(
        object_name.data(), static_cast<qsizetype>(object_name.size())));
    action->setEnabled(descriptor.has_value() && descriptor->enabled);
    bound_commands.push_back({.action = action,
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

    const auto add_catalog_section =
        [&](QMenu &owner, ViewportContextMenuActionSection section) {
          const std::vector<const QuaderActionItem *> actions =
              catalog_actions_for_section(action_set, selection.mode, editor,
                                          section);
          for (const QuaderActionItem *action : actions) {
            if (action != nullptr) {
              (void)add_catalog_action(owner, *action);
            }
          }
          return !actions.empty();
        };

    const auto add_catalog_submenu =
        [&](ViewportContextMenuActionSection section,
            std::string_view object_name) {
          const std::vector<const QuaderActionItem *> actions =
              catalog_actions_for_section(action_set, selection.mode, editor,
                                          section);
          if (actions.empty()) {
            return static_cast<QMenu *>(nullptr);
          }
          const auto [label_key, fallback] = action_section_label(section);
          QMenu *submenu = menu.addMenu(
              QString::fromStdString(localization.text(label_key, fallback)));
          submenu->setObjectName(QString::fromUtf8(
              object_name.data(), static_cast<qsizetype>(object_name.size())));
          submenu->setToolTipsVisible(true);
          for (const QuaderActionItem *action : actions) {
            if (action != nullptr) {
              (void)add_catalog_action(*submenu, *action);
            }
          }
          return submenu;
        };

    const auto add_material_actions = [&]() {
      if (!material_under_mouse.has_value()) {
        return false;
      }
      const std::string sample_shortcut =
          editor.commands().shortcut_for("viewport.material.sample_face");
      set_material_action = menu.addAction(with_shortcut(
          QString::fromStdString(localization.text(
              "ui.viewport.context_menu.command.set_material_under_mouse",
              "Set Material under Mouse as Active")),
          sample_shortcut));
      set_material_action->setObjectName(
          QStringLiteral("viewport.material.sample_face"));

      if (open_material_editor) {
        edit_material_action =
            menu.addAction(QString::fromStdString(localization.text(
                "ui.viewport.context_menu.command.edit_material_under_mouse",
                "Edit Material under Mouse")));
        edit_material_action->setObjectName(
            QStringLiteral("viewport.material.edit_under_mouse"));
      }
      return true;
    };

    if (!target_hit) {
      // Empty-space menus are an action launcher, not a disabled summary of
      // the selection that used to be active before the click.
      (void)add_command_action(
          menu, "ui.viewport.context_menu.command.create_primitive",
          "Create Primitive", quader_command::kToolsActivateCreatePrimitive,
          "tools.activate.create_primitive", true);
      (void)add_command_action(menu, "ui.menu.select.all", "Select All",
                               quader_command::kSelectAll, "select.all", true);
      (void)add_command_action(menu, "ui.menu.view.focus_all", "Focus All",
                               quader_command::kViewportFocusAll,
                               "viewport.focus_all", true);
      (void)add_command_action(menu, "ui.menu.edit.paste", "Paste",
                               quader_command::kPasteSelection, "edit.paste",
                               true);
    } else if (selection.has_selection) {
      if (selection.mode == QuaderSelectionMode::Object ||
          selection.mode == QuaderSelectionMode::Group) {
        const bool group_mode = selection.mode == QuaderSelectionMode::Group;
        (void)add_command_action(
            menu,
            group_mode ? "ui.viewport.context_menu.command.duplicate_group"
                       : "ui.viewport.context_menu.command.duplicate_object",
            group_mode ? "Duplicate Group" : "Duplicate Object",
            quader_command::kDuplicateSelection, "edit.duplicate");
        (void)add_command_action(
            menu, "ui.menu.edit.create_instance", "Create Instance",
            quader_command::kEditCreateInstance, "edit.create_instance");
        if (group_mode) {
          (void)add_command_action(menu, "ui.menu.edit.ungroup", "Ungroup",
                                   quader_command::kUngroupSelection,
                                   "edit.ungroup");
        } else {
          (void)add_command_action(menu, "ui.menu.edit.group", "Group",
                                   quader_command::kGroupSelection,
                                   "edit.group");
        }
        (void)add_command_action(menu, "ui.menu.edit.flatten_instance",
                                 "Flatten Instance",
                                 quader_command::kEditFlattenInstance,
                                 "edit.flatten_instance", true);
        add_separator_after_content(menu);
      }

      (void)add_catalog_section(menu,
                                ViewportContextMenuActionSection::Primary);
      (void)add_catalog_submenu(ViewportContextMenuActionSection::MeshData,
                                "viewport_context_mesh_data_submenu");
      (void)add_catalog_submenu(ViewportContextMenuActionSection::Topology,
                                "viewport_context_topology_submenu");
      (void)add_catalog_submenu(ViewportContextMenuActionSection::Merge,
                                "viewport_context_merge_submenu");
      (void)add_catalog_submenu(ViewportContextMenuActionSection::Normals,
                                "viewport_context_normals_submenu");
      (void)add_catalog_submenu(ViewportContextMenuActionSection::Arrange,
                                "viewport_context_arrange_submenu");

      if ((selection.mode == QuaderSelectionMode::Object ||
           selection.mode == QuaderSelectionMode::Group) &&
          add_command_action(
              menu, "ui.layers.remove_selection", "Remove Objects from Layers",
              quader_command::kSceneLayersRemoveSelection,
              "scene.layers.remove_selection", true) != nullptr) {
        // Layer membership is secondary organization intent and remains
        // adjacent to the other contextual object operations.
      }

      add_separator_after_content(menu);
      (void)add_command_action(
          menu, "ui.viewport.context_menu.command.center_selection",
          "Center View on Selection", quader_command::kViewportFrameSelection,
          "viewport.frame_selection");

      if (material_under_mouse.has_value()) {
        add_separator_after_content(menu);
        (void)add_material_actions();
      }

      add_separator_after_content(menu);
      (void)add_command_action(menu, "ui.menu.edit.cut", "Cut",
                               quader_command::kCutSelection, "edit.cut");
      (void)add_command_action(menu, "ui.menu.edit.copy", "Copy",
                               quader_command::kCopySelection, "edit.copy");
      (void)add_command_action(menu, "ui.menu.edit.paste", "Paste",
                               quader_command::kPasteSelection, "edit.paste",
                               true);

      const std::vector<const QuaderActionItem *> destructive =
          catalog_actions_for_section(
              action_set, selection.mode, editor,
              ViewportContextMenuActionSection::Destructive);
      if (!destructive.empty()) {
        add_separator_after_content(menu);
        for (const QuaderActionItem *action : destructive) {
          if (action != nullptr) {
            (void)add_catalog_action(menu, *action);
          }
        }
      }
    } else {
      // A material-only hit is possible even if selection projection rejected
      // the target (for example, a locked mesh). Keep that menu focused on the
      // operation that is still meaningful.
      (void)add_material_actions();
    }
  }

  QAction *selected_action = nullptr;
  {
    QDR_PROFILE_SCOPE("Qt viewport context menu exec");
    selected_action = menu.exec(global_point);
  }
  if (!runtime_binding.current()) {
    return;
  }
  if (selected_action == nullptr) {
    return;
  }
  if (!same_context_revision(presented_context, editor.revisions())) {
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

  const auto selected =
      std::find_if(bound_commands.begin(), bound_commands.end(),
                   [selected_action](const BoundContextCommand &entry) {
                     return entry.action == selected_action;
                   });
  if (selected == bound_commands.end()) {
    return;
  }

  // Availability can also depend on non-document runtime state. Refresh only
  // the chosen descriptor after the nested event loop; never rescan the broad
  // command catalog here or from the frame loop.
  const std::optional<QuaderCommandDescriptor> current_descriptor =
      editor.commands().descriptor(selected->command_id);
  if (!current_descriptor.has_value() || !current_descriptor->enabled) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("Qt viewport context menu dispatch command");
    const QuaderApiVoidResult result =
        editor.commands().execute(selected->command_id);
    if (!runtime_binding.current()) {
      return;
    }
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
