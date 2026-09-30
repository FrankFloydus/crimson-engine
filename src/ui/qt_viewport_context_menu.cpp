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
#include "core/release_features.hpp"
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
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

struct BoundContextCommand final {
  QAction *action = nullptr;
  std::string command_id;
  std::string source_id;
};

ViewportContextMenuActionSection presentation_section(
    const CommandPresentationDescriptor *presentation) {
  if (presentation == nullptr) {
    return ViewportContextMenuActionSection::Topology;
  }
  if (presentation->danger == CommandPresentationDanger::Destructive) {
    return ViewportContextMenuActionSection::Destructive;
  }
  switch (presentation->group) {
  case CommandPresentationGroup::Duplication:
  case CommandPresentationGroup::Instances:
  case CommandPresentationGroup::Grouping:
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
}

ViewportContextMenuScope context_menu_scope(
    QuaderEditorApi &editor, const QuaderSelectionSummary &selection,
    bool target_hit) {
  ViewportContextMenuScope scope{
      .mode = selection.mode,
      .has_selection = target_hit && selection.has_selection};
  scope.curve_controls_selected =
      selection.sub_selection_kind == QuaderSubSelectionKind::CurveControl &&
      selection.selected_sub_element_count != 0u;
  if (!scope.has_selection || scope.curve_controls_selected) {
    return scope;
  }
  if (scope.mode != QuaderSelectionMode::Object &&
      scope.mode != QuaderSelectionMode::Group) {
    scope.polygon_targets = true;
    return scope;
  }

  // Sample entity metadata once when opening the popup. The usual single-mesh
  // case needs one narrow lookup; mixed/hierarchy selection needs no topology.
  std::vector<QuaderEntitySnapshot> entities;
  const auto active = editor.query().modeling_object_for_id(
      selection.active_object_id);
  if (selection.selected_object_count == 1u && active.has_value()) {
    const auto entity = editor.query().entity(active->entity);
    if (entity.has_value() && entity->kind != QuaderEntityKind::Empty) {
      entities.push_back(*entity);
    }
  }
  if (entities.empty()) {
    entities = editor.query().entities();
  }
  std::unordered_set<EntityId> parents;
  for (const auto &entity : entities) {
    parents.insert(entity.parent);
  }
  bool any_target = false;
  bool all_polygons = true;
  bool all_groupable = true;
  bool all_clipboard = true;
  for (const auto &entity : entities) {
    if (!entity.selected) {
      continue;
    }
    any_target = true;
    const bool editable = entity.selectable && !entity.locked &&
                          !entity.is_instance_definition;
    const bool instance = editable && entity.has_instance_occurrence;
    const bool polygon = editable && entity.has_modeling_object &&
                         entity.has_mesh && !entity.has_curve && !instance;
    const bool group = editable && entity.kind == QuaderEntityKind::Empty &&
                       parents.contains(entity.id);
    all_polygons &= polygon;
    all_groupable &= polygon || group || instance;
    all_clipboard &= polygon || instance;
    scope.has_group_target |= group;
    scope.editable_curve |= editable && entity.has_curve &&
                            selection.selected_object_count == 1u;
  }
  scope.polygon_targets = any_target && all_polygons;
  scope.groupable_targets = any_target && all_groupable;
  scope.clipboard_targets = any_target && all_clipboard;
  return scope;
}

struct SupplementalContextCommand final {
  QuaderCommandDescriptor descriptor;
  const CommandPresentationDescriptor *presentation = nullptr;
};

std::vector<SupplementalContextCommand> supplemental_modeling_commands(
    QuaderEditorApi &editor, const QuaderActionSet &action_set,
    const ViewportContextMenuScope &scope) {
  // Catalog visibility remains authoritative even for omitted/disabled items.
  // Reserve their command IDs so supplements never reintroduce those actions.
  std::unordered_set<std::string> catalog_commands;
  for (const QuaderActionPanel &panel : action_set.panels) {
    for (const QuaderActionItem &action : panel.actions) {
      catalog_commands.insert(
          action.kind == QuaderActionKind::Tool
              ? editor.commands().command_id_for_tool_activation(action.tool_id)
              : editor.commands().command_id_for_modeling_action(action.action_id));
    }
  }

  std::vector<SupplementalContextCommand> result;
  for (const auto &presentation : command_presentation_catalog()) {
    // Clipboard and empty-space creation have their own fixed block below.
    if (presentation.group == CommandPresentationGroup::Clipboard ||
        !has_command_presentation_surface(
            presentation, CommandPresentationSurface::ViewportContextMenu) ||
        !viewport_context_menu_allows_command(scope, presentation.command_id) ||
        catalog_commands.contains(std::string(presentation.command_id.string()))) {
      continue;
    }
    auto descriptor = editor.commands().descriptor(presentation.command_id);
    if (descriptor.has_value() && descriptor->enabled) {
      result.push_back({std::move(*descriptor), &presentation});
    }
  }
  std::ranges::stable_sort(result, {}, [](const auto &command) {
    return command.presentation->rank;
  });
  return result;
}

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
                               const ViewportContextMenuScope &scope,
                               const LocalizationService &localization) {
  if (action.action_id == "duplicate_meshes") {
    return QString::fromStdString(localization.text(
        "ui.viewport.context_menu.command.duplicate_mesh_data",
        "Duplicate Mesh Data"));
  }
  if (action.action_id == "delete_selection") {
    if (scope.curve_controls_selected) {
      return QString::fromStdString(localization.text(
          "ui.viewport.context_menu.command.delete_control_points",
          "Delete Control Points"));
    }
    switch (scope.mode) {
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

QString localized_command_label(const QuaderCommandDescriptor &command,
                                const ViewportContextMenuScope &scope,
                                const LocalizationService &localization) {
  if (command.id == quader_command::kDeleteSelection.string()) {
    return localized_action_label({.action_id = "delete_selection"}, scope,
                                  localization);
  }
  for (const auto &[id, key, label] : {
           std::tuple{quader_command::kDuplicateSelection, "ui.menu.edit.duplicate", "Duplicate"},
           std::tuple{quader_command::kGroupSelection, "ui.menu.edit.group", "Group"},
           std::tuple{quader_command::kUngroupSelection, "ui.menu.edit.ungroup", "Ungroup"},
           std::tuple{quader_command::kParentToActive, "ui.menu.edit.parent_to_active", "Parent to Active"},
           std::tuple{quader_command::kUnparentSelection, "ui.menu.edit.unparent", "Unparent"}}) {
    if (command.id == id.string()) {
      return QString::fromStdString(localization.text(key, label));
    }
  }
  const std::string key = "keybindings.target." + command.id + ".label";
  return QString::fromStdString(localization.text(key, command.label));
}

QString with_shortcut(QString label, std::string_view shortcut) {
  if (!shortcut.empty()) {
    QString display_shortcut = QString::fromUtf8(
        shortcut.data(), static_cast<qsizetype>(shortcut.size()));
    display_shortcut.replace(QStringLiteral("RightMouseClick"),
                             QStringLiteral("RMouse"));
    display_shortcut.replace(QStringLiteral("LeftMouseClick"),
                             QStringLiteral("LMouse"));
    display_shortcut.replace(QStringLiteral("RightMouse"),
                             QStringLiteral("RMouse"));
    display_shortcut.replace(QStringLiteral("LeftMouse"),
                             QStringLiteral("LMouse"));
    label.append(QLatin1Char('\t'));
    label.append(display_shortcut);
  }
  return label;
}

std::vector<const QuaderActionItem *>
catalog_actions_for_section(const QuaderActionSet &action_set,
                            const ViewportContextMenuScope &scope,
                            QuaderEditorApi &editor,
                            ViewportContextMenuActionSection section) {
  const auto mode = scope.mode;
  const auto catalog_section = [&](const QuaderActionItem &action) {
    const ViewportContextMenuActionDisposition disposition =
        viewport_context_menu_action_disposition(mode, action.action_id);
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
    if (!viewport_context_menu_allows_command(scope, command_id)) {
      return ViewportContextMenuActionSection::Suppressed;
    }
    if (disposition == ViewportContextMenuActionDisposition::Primary) {
      return ViewportContextMenuActionSection::Primary;
    }
    return presentation_section(presentation);
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
      if (action.visibility != QuaderActionVisibility::Enabled ||
          catalog_section(action) != section) {
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

std::vector<QtViewportContextCommand> qt_viewport_context_commands(
    QuaderEditorApi &editor, const QuaderSelectionSummary &selection,
    const LocalizationService &localization) {
  const auto scope = context_menu_scope(editor, selection, selection.has_selection);
  const auto action_set = editor.modeling().available_action_set({
      .surface = QuaderActionSurface::ViewportContextMenu,
      .include_disabled = true,
  });
  const auto supplemental = supplemental_modeling_commands(editor, action_set, scope);
  std::vector<QtViewportContextCommand> result;
  std::unordered_set<std::string> added;
  const auto append = [&](std::string command_id, QString label,
                          ViewportContextMenuActionSection section) {
    if (!viewport_context_menu_allows_command(scope, command_id) ||
        added.contains(command_id)) {
      return;
    }
    const auto descriptor = editor.commands().descriptor(command_id);
    if (!descriptor.has_value() || !descriptor->enabled) {
      return;
    }
    added.insert(command_id);
    result.push_back({std::move(command_id), std::move(label), section});
  };
  const auto append_fixed = [&](QuaderCommandId id, std::string_view key,
                                std::string_view fallback,
                                ViewportContextMenuActionSection section) {
    append(std::string(id.string()),
           QString::fromStdString(localization.text(key, fallback)), section);
  };
  using Section = ViewportContextMenuActionSection;
  if (!scope.has_selection) {
    append_fixed(quader_command::kPasteSelection, "ui.menu.edit.paste", "Paste",
                 Section::MeshData);
    return result;
  }
  if (selection.mode == QuaderSelectionMode::Object ||
      selection.mode == QuaderSelectionMode::Group) {
    const bool group = selection.mode == QuaderSelectionMode::Group;
    append_fixed(quader_command::kDuplicateSelection,
                 group ? "ui.viewport.context_menu.command.duplicate_group"
                       : "ui.viewport.context_menu.command.duplicate_object",
                 group ? "Duplicate Groups" : "Duplicate Objects", Section::Primary);
    append_fixed(quader_command::kEditCreateInstance, "ui.menu.edit.create_instance",
                 "Create Instance", Section::Primary);
    append_fixed(group ? quader_command::kUngroupSelection : quader_command::kGroupSelection,
                 group ? "ui.menu.edit.ungroup" : "ui.menu.edit.group",
                 group ? "Ungroup" : "Group", Section::Primary);
    append_fixed(quader_command::kParentToActive, "ui.menu.edit.parent_to_active",
                 "Parent to Active", Section::Primary);
    append_fixed(quader_command::kUnparentSelection, "ui.menu.edit.unparent",
                 "Unparent", Section::Primary);
    append_fixed(quader_command::kEditFlattenInstance, "ui.menu.edit.flatten_instance",
                 "Flatten Instance", Section::Primary);
  }
  const auto append_section = [&](Section section) {
    for (const QuaderActionItem *action :
         catalog_actions_for_section(action_set, scope, editor, section)) {
      const auto command_id = action->kind == QuaderActionKind::Tool
          ? editor.commands().command_id_for_tool_activation(action->tool_id)
          : editor.commands().command_id_for_modeling_action(action->action_id);
      append(command_id, localized_action_label(*action, scope, localization), section);
    }
    for (const auto &command : supplemental) {
      if (presentation_section(command.presentation) == section) {
        append(command.descriptor.id,
               localized_command_label(command.descriptor, scope, localization), section);
      }
    }
  };
  for (const Section section : {Section::Primary, Section::MeshData,
                               Section::Topology, Section::Merge, Section::Normals,
                               Section::Arrange}) {
    append_section(section);
  }
  append_fixed(quader_command::kCutSelection, "ui.menu.edit.cut", "Cut", Section::MeshData);
  append_fixed(quader_command::kCopySelection, "ui.menu.edit.copy", "Copy", Section::MeshData);
  append_fixed(quader_command::kPasteSelection, "ui.menu.edit.paste", "Paste", Section::MeshData);
  append_section(Section::Destructive);
  return result;
}

void QtViewportContextMenuPresenter::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  ui_scale_metrics_ = metrics;
}

void QtViewportContextMenuPresenter::show(
    QWidget &parent, const QPoint &global_point, QuaderEditorApi &editor,
    ViewportInputRouter &input_router, LocalizationService &localization,
    const QuaderActionSet &action_set, const QuaderSelectionSummary &selection,
    bool target_hit, const std::optional<QuaderHoverTarget> &material_face_target,
    ViewportRuntimeBindingToken runtime_binding) const {
  QDR_PROFILE_SCOPE("Qt viewport context menu show");

  // QMenu::exec() runs a nested event loop. Capture the complete public
  // editor context used to build this menu so an action cannot later execute
  // against a selection, document, layer, or asset state different from the
  // one the user inspected.
  const QuaderRevisionStamp presented_context = editor.revisions();

  QMenu menu(&parent);
  std::vector<BoundContextCommand> bound_commands;
  std::unordered_set<std::string> added_commands;
  QAction *copy_material_to_face = nullptr;
  QAction *copy_material_to_selection = nullptr;
  QAction *continue_texture = nullptr;
  QAction *apply_curve_solidify = nullptr;
  const auto scope = context_menu_scope(editor, selection, target_hit);
  const auto context_commands = scope.has_selection
      ? supplemental_modeling_commands(editor, action_set, scope)
      : std::vector<SupplementalContextCommand>{};

  const auto add_catalog_action = [&](QMenu &owner,
                                      const QuaderActionItem &action) {
    const std::string command_id =
        action.kind == QuaderActionKind::Tool
            ? editor.commands().command_id_for_tool_activation(action.tool_id)
            : editor.commands().command_id_for_modeling_action(
                  action.action_id);
    if (!viewport_context_menu_allows_command(scope, command_id) ||
        !added_commands.insert(command_id).second) {
      return static_cast<QAction *>(nullptr);
    }
    QAction *menu_action = owner.addAction(with_shortcut(
        localized_action_label(action, scope, localization),
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
    if (!viewport_context_menu_allows_command(scope, command_id)) {
      return static_cast<QAction *>(nullptr);
    }
    const std::optional<QuaderCommandDescriptor> descriptor =
        editor.commands().descriptor(command_id);
    if (omit_when_disabled &&
        (!descriptor.has_value() || !descriptor->enabled)) {
      return static_cast<QAction *>(nullptr);
    }
    if (!added_commands.insert(std::string(command_id.string())).second) {
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
    menu.setToolTipsVisible(true);

    const auto add_catalog_section =
        [&](QMenu &owner, ViewportContextMenuActionSection section) {
          const std::vector<const QuaderActionItem *> actions =
              catalog_actions_for_section(action_set, scope, editor, section);
          if (!actions.empty()) {
            add_separator_after_content(owner);
          }
          for (const QuaderActionItem *action : actions) {
            if (action != nullptr) {
              (void)add_catalog_action(owner, *action);
            }
          }
          bool added_supplement = false;
          for (const auto &command : context_commands) {
            const auto &descriptor = command.descriptor;
            if (presentation_section(command.presentation) != section ||
                !added_commands.insert(descriptor.id).second) {
              continue;
            }
            if (actions.empty() && !added_supplement) {
              add_separator_after_content(owner);
            }
            QAction *action = owner.addAction(with_shortcut(
                localized_command_label(descriptor, scope, localization),
                descriptor.shortcut));
            action->setObjectName(QString::fromStdString(descriptor.id));
            bound_commands.push_back({action, descriptor.id, descriptor.id});
            added_supplement = true;
          }
          return !actions.empty() || added_supplement;
        };

    const auto add_material_actions = [&]() {
      if (!release_has_feature(ReleaseFeature::Materials) ||
          !release_has_feature(ReleaseFeature::UvEditing) ||
          !material_face_target.has_value() ||
          material_face_target->kind != QuaderActiveSelectionKind::Face ||
          selection.mode != QuaderSelectionMode::Face ||
          selection.selected_face_count == 0u) {
        return false;
      }
      const auto target = editor.query().modeling_object_for_id(
          material_face_target->object_id);
      const auto target_entity = target.has_value()
          ? editor.query().entity(target->entity) : std::nullopt;
      const auto settings = editor.modeling().face_texture_settings();
      if (!target_entity.has_value() || !target_entity->has_mesh ||
          target_entity->has_curve || !target_entity->selectable ||
          target_entity->locked || !settings.editable) {
        return false;
      }
      // Copying a single selected face onto itself has no useful effect.
      if (selection.selected_face_count == 1u &&
          selection.active_object_id == material_face_target->object_id &&
          selection.active_face_id == material_face_target->face_id) {
        return false;
      }
      add_separator_after_content(menu);
      const auto add_transfer = [&](std::string_view id, std::string_view key,
                                    std::string_view label) {
        QAction *action = menu.addAction(with_shortcut(
            QString::fromStdString(localization.text(key, label)),
            editor.commands().shortcut_for(id)));
        action->setObjectName(QString::fromUtf8(id.data(),
                                               static_cast<qsizetype>(id.size())));
        return action;
      };
      if (selection.selected_face_count == 1u && target_entity->selectable &&
          !target_entity->locked) {
        copy_material_to_face = add_transfer(
            "viewport.material.apply_selected_face_to_face",
            "ui.viewport.context_menu.command.copy_material_to_face",
            "Copy Material to Face");
        continue_texture = add_transfer(
            "viewport.material.transfer_face_texture",
            "ui.viewport.context_menu.command.continue_texture",
            "Continue Texture");
      }
      copy_material_to_selection = add_transfer(
          "viewport.material.apply_face_to_selection",
          "ui.viewport.context_menu.command.copy_material_to_faces",
          "Copy Material to Faces");
      return true;
    };

    if (!target_hit) {
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
            group_mode ? "Duplicate Groups" : "Duplicate Objects",
            quader_command::kDuplicateSelection, "edit.duplicate", true);
        (void)add_command_action(
            menu, "ui.menu.edit.create_instance", "Create Instance",
            quader_command::kEditCreateInstance, "edit.create_instance", true);
        if (group_mode) {
          (void)add_command_action(menu, "ui.menu.edit.ungroup", "Ungroup",
                                   quader_command::kUngroupSelection,
                                   "edit.ungroup", true);
        } else {
          (void)add_command_action(menu, "ui.menu.edit.group", "Group",
                                   quader_command::kGroupSelection,
                                   "edit.group", true);
        }
        (void)add_command_action(menu, "ui.menu.edit.parent_to_active",
                                 "Parent to Active", quader_command::kParentToActive,
                                 "edit.parent_to_active", true);
        (void)add_command_action(menu, "ui.menu.edit.unparent", "Unparent",
                                 quader_command::kUnparentSelection,
                                 "edit.unparent", true);
        (void)add_command_action(menu, "ui.menu.edit.flatten_instance",
                                 "Flatten Instance",
                                 quader_command::kEditFlattenInstance,
                                 "edit.flatten_instance", true);
        add_separator_after_content(menu);
      }

      (void)add_catalog_section(menu,
                                ViewportContextMenuActionSection::Primary);
      if (scope.editable_curve && !scope.curve_controls_selected) {
        const auto settings = editor.tools().active_settings();
        if (settings.tool_id == "curve") {
          for (const auto &control : settings.controls) {
            if (control.id == "curve.solidify.apply" && control.enabled &&
                control.kind == QuaderToolSettingControlKind::Action) {
              add_separator_after_content(menu);
              apply_curve_solidify = menu.addAction(QString::fromStdString(
                  localization.text("ui.viewport.context_menu.command.apply_solidify",
                                    control.label)));
              apply_curve_solidify->setObjectName(
                  QStringLiteral("curve.solidify.apply"));
            }
          }
        }
      }
      (void)add_catalog_section(menu, ViewportContextMenuActionSection::MeshData);
      (void)add_catalog_section(menu, ViewportContextMenuActionSection::Topology);
      (void)add_catalog_section(menu, ViewportContextMenuActionSection::Merge);
      (void)add_catalog_section(menu, ViewportContextMenuActionSection::Normals);
      (void)add_catalog_section(menu, ViewportContextMenuActionSection::Arrange);

      (void)add_material_actions();

      add_separator_after_content(menu);
      (void)add_command_action(menu, "ui.menu.edit.cut", "Cut",
                               quader_command::kCutSelection, "edit.cut", true);
      (void)add_command_action(menu, "ui.menu.edit.copy", "Copy",
                               quader_command::kCopySelection, "edit.copy", true);
      (void)add_command_action(menu, "ui.menu.edit.paste", "Paste",
                               quader_command::kPasteSelection, "edit.paste",
                               true);

      (void)add_catalog_section(menu,
                                ViewportContextMenuActionSection::Destructive);
    }
    const auto actions = menu.actions();
    if (!actions.empty() && actions.back()->isSeparator()) {
      delete actions.back();
    }
  }

  if (menu.actions().empty()) {
    return;
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

  if (selected_action == apply_curve_solidify) {
    const auto settings = editor.tools().active_settings();
    if (settings.tool_id != "curve" ||
        !std::ranges::any_of(settings.controls, [](const auto &control) {
          return control.id == "curve.solidify.apply" && control.enabled &&
                 control.kind == QuaderToolSettingControlKind::Action;
        })) {
      return;
    }
    const auto result = editor.tools().execute_action("curve", "curve.solidify.apply");
    if (!runtime_binding.current()) {
      return;
    }
    if (!result.ok()) {
      (void)editor.ui().notify({.level = QuaderNotificationLevel::Error,
                               .message = result.error_message()});
    }
    return;
  }

  if (material_face_target.has_value() &&
      (selected_action == copy_material_to_face ||
       selected_action == copy_material_to_selection ||
       selected_action == continue_texture)) {
    const auto object = static_cast<QuaderModelingObjectId>(
        material_face_target->object_id);
    const auto face = static_cast<QuaderElementId>(material_face_target->face_id);
    const QuaderApiVoidResult result = selected_action == copy_material_to_face
        ? editor.modeling().apply_selected_face_material_to_face(object, face)
        : selected_action == copy_material_to_selection
            ? editor.modeling().apply_face_material_to_selected_faces(object, face)
            : editor.modeling().transfer_face_texture_to_face(object, face);
    if (!runtime_binding.current()) {
      return;
    }
    if (!result.ok()) {
      (void)editor.ui().notify({.level = QuaderNotificationLevel::Error,
                               .message = result.error_message()});
    }
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
