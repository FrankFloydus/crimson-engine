////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_main_menu.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/viewport_layout.hpp"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

QString qtext(const LocalizationService &localization, std::string_view key,
              std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

QString action_text(const LocalizationService &localization,
                    std::string_view key, std::string_view fallback,
                    std::string_view shortcut = {}) {
  QString text = qtext(localization, key, fallback);
  if (!shortcut.empty()) {
    text.append(QLatin1Char('\t'));
    text.append(QString::fromUtf8(shortcut.data(),
                                  static_cast<qsizetype>(shortcut.size())));
  }
  return text;
}

std::string stable_object_suffix(std::string_view value) {
  std::string result(value);
  for (char &character : result) {
    if (character == '.' || character == '-' || character == ' ') {
      character = '_';
    }
  }
  return result;
}

const QuaderCommandDescriptor *
find_command(const std::vector<QuaderCommandDescriptor> &commands,
             QuaderCommandId command_id) {
  for (const QuaderCommandDescriptor &command : commands) {
    if (command.id == command_id.string()) {
      return &command;
    }
  }
  return nullptr;
}

std::string layout_localization_key(ViewportLayoutId layout) {
  std::string key = "viewport.layout.";
  key.append(viewport_layout_stable_id(layout));
  key.append(".label");
  return key;
}

std::string tool_localization_key(const Tool &tool) {
  std::string key = "viewport.tool.";
  key.append(tool.id());
  key.append(".label");
  return key;
}

const QuaderMeshArrangementDescriptor *arrangement_descriptor(
    QuaderMeshArrangementKind kind, QuaderTransformAxis axis,
    QuaderMeshAlignment alignment = QuaderMeshAlignment::Center,
    QuaderMeshDistribution distribution = QuaderMeshDistribution::Centers) {
  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    if (descriptor.kind == kind && descriptor.axis == axis &&
        (kind != QuaderMeshArrangementKind::Alignment ||
         descriptor.alignment == alignment) &&
        (kind != QuaderMeshArrangementKind::Distribution ||
         descriptor.distribution == distribution)) {
      return &descriptor;
    }
  }
  return nullptr;
}

} // namespace

/// Owns the retained menu hierarchy and its editor-action bindings.
struct QtMainMenu::Impl {
  /// Associates a QAction with a typed editor command and localized label.
  struct CommandAction {
    QAction *action = nullptr;
    QuaderCommandId command_id;
    std::string localization_key;
    std::string fallback;
  };

  /// Associates a checkable QAction with one viewport boolean setting.
  struct BoolSettingAction {
    QAction *action = nullptr;
    ViewportBoolSettingId setting = ViewportBoolSettingId::Count;
  };

  /// Associates a QAction with a viewport workspace layout.
  struct LayoutAction {
    QAction *action = nullptr;
    ViewportLayoutId layout = ViewportLayoutId::Single;
  };

  /// Associates a QAction with a stable dock-panel identifier.
  struct PanelAction {
    QAction *action = nullptr;
    UIPanelId panel = UIPanelId::Viewport;
  };

  /// Associates a QAction with a viewport tool identifier.
  struct ToolAction {
    QAction *action = nullptr;
    ViewportToolId tool = ViewportToolId::Select;
  };

  explicit Impl(QtMainMenuContext value) : context(std::move(value)) {}

  QMenuBar *menu_bar = nullptr;
  QtMainMenuContext context;
  QMenu *align_menu = nullptr;
  QAction *navigation_gizmo_action = nullptr;
  std::vector<CommandAction> command_actions;
  std::vector<BoolSettingAction> bool_setting_actions;
  std::vector<LayoutAction> layout_actions;
  std::vector<PanelAction> panel_actions;
  std::vector<ToolAction> tool_actions;

  QAction *add_command_action(QMenu &menu, QuaderCommandId command_id,
                              std::string localization_key,
                              std::string fallback) {
    QAction *action = menu.addAction(QString::fromStdString(fallback));
    action->setObjectName(
        QStringLiteral("command_%1")
            .arg(QString::fromStdString(
                stable_object_suffix(command_id.string()))));
    QObject::connect(action, &QAction::triggered, menu_bar,
                     [this, command_id]() {
                       (void)context.editor.commands().execute(command_id);
                     });
    command_actions.push_back(CommandAction{
        .action = action,
        .command_id = command_id,
        .localization_key = std::move(localization_key),
        .fallback = std::move(fallback),
    });
    return action;
  }

  QAction *add_bool_setting_action(QMenu &menu,
                                   ViewportBoolSettingId setting,
                                   std::string_view key,
                                   std::string_view fallback) {
    QAction *action = menu.addAction(qtext(context.localization, key, fallback));
    action->setObjectName(
        QStringLiteral("viewportSetting_%1")
            .arg(QString::fromUtf8(key.data(),
                                   static_cast<qsizetype>(key.size()))));
    action->setCheckable(true);
    QObject::connect(action, &QAction::toggled, menu_bar,
                     [this, setting](bool checked) {
                       if (context.viewport_settings.set_bool_value(setting,
                                                                    checked)) {
                         if (context.viewport_settings_changed) {
                           context.viewport_settings_changed();
                         }
                       }
                     });
    bool_setting_actions.push_back({action, setting});
    return action;
  }

  void add_arrangement_action(QMenu &menu,
                              const QuaderMeshArrangementDescriptor *descriptor) {
    if (descriptor == nullptr) {
      return;
    }
    add_command_action(menu, descriptor->command_id,
                       std::string(descriptor->localization_key),
                       std::string(descriptor->fallback_label));
  }

  void build_file_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.file", "File"));
    menu->setObjectName(QStringLiteral("menu_file"));
    add_command_action(*menu, quader_command::kProjectNew, "ui.menu.file.new",
                       "New");
    add_command_action(*menu, quader_command::kProjectOpen,
                       "ui.menu.file.open", "Open...");
    add_command_action(*menu, quader_command::kProjectSave,
                       "ui.menu.file.save", "Save");
    add_command_action(*menu, quader_command::kProjectSaveAs,
                       "ui.menu.file.save_as", "Save As...");
    add_command_action(*menu, quader_command::kProjectExportSelectionObj,
                       "ui.menu.file.export_obj", "Export OBJ...");
    menu->addSeparator();
    QAction *exit_action =
        menu->addAction(qtext(context.localization, "ui.menu.file.exit", "Exit"));
    exit_action->setObjectName(QStringLiteral("action_exit"));
    QObject::connect(exit_action, &QAction::triggered, menu_bar,
                     [this]() {
                       if (context.request_quit) {
                         context.request_quit();
                       }
                     });
  }

  void build_edit_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.edit", "Edit"));
    menu->setObjectName(QStringLiteral("menu_edit"));
    add_command_action(*menu, quader_command::kEditUndo, "ui.menu.edit.undo",
                       "Undo");
    add_command_action(*menu, quader_command::kEditRedo, "ui.menu.edit.redo",
                       "Redo");
    add_command_action(*menu, quader_command::kEditRepeatLastAction,
                       "ui.menu.edit.repeat_last_action",
                       "Repeat Last Action");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kCopySelection,
                       "ui.menu.edit.copy", "Copy");
    add_command_action(*menu, quader_command::kPasteSelection,
                       "ui.menu.edit.paste", "Paste");
    add_command_action(*menu, quader_command::kDuplicateSelection,
                       "ui.menu.edit.duplicate", "Duplicate");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kModelingFlipHorizontal,
                       "ui.menu.edit.flip_horizontal", "Flip Horizontal");
    add_command_action(*menu, quader_command::kModelingFlipVertical,
                       "ui.menu.edit.flip_vertical", "Flip Vertical");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kEditSelectAll,
                       "ui.menu.edit.select_all", "Select All");
    add_command_action(*menu, quader_command::kEditClearSelection,
                       "ui.menu.edit.clear_selection", "Clear Selection");
    add_command_action(*menu, quader_command::kEditInvertSelection,
                       "ui.menu.edit.invert_selection", "Invert Selection");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kDeleteSelection,
                       "ui.menu.edit.delete", "Delete");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kGroupSelection,
                       "ui.menu.edit.group", "Group");
    add_command_action(*menu, quader_command::kUngroupSelection,
                       "ui.menu.edit.ungroup", "Ungroup");
    menu->addSeparator();
    QMenu *snap = menu->addMenu(
        qtext(context.localization, "ui.menu.edit.snap", "Snap"));
    snap->setObjectName(QStringLiteral("menu_snap"));
    add_bool_setting_action(*snap, ViewportBoolSettingId::GridSnap,
                            "ui.menu.edit.snap.grid", "Grid");
    add_bool_setting_action(*snap, ViewportBoolSettingId::VertexSnap,
                            "ui.menu.edit.snap.vertex", "Vertex");
    menu->addSeparator();
    QAction *settings = menu->addAction(qtext(
        context.localization, "ui.menu.edit.settings", "Settings"));
    settings->setObjectName(QStringLiteral("action_settings"));
    QObject::connect(settings, &QAction::triggered, menu_bar, [this]() {
      if (context.show_settings) {
        context.show_settings();
      }
    });
  }

  void build_align_menu() {
    align_menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.align", "Align"));
    align_menu->setObjectName(QStringLiteral("menu_align"));
    QAction *snap = align_menu->addAction(qtext(
        context.localization, "ui.menu.align.snap_to_grid", "Snap to Grid"));
    snap->setObjectName(QStringLiteral("action_align_snap_to_grid"));
    snap->setCheckable(true);
    QObject::connect(snap, &QAction::toggled, menu_bar, [this](bool checked) {
      (void)context.editor.preferences().set_grid_snap_enabled(checked);
    });
    bool_setting_actions.push_back(
        {snap, ViewportBoolSettingId::GridSnap});
    align_menu->addSeparator();
    constexpr std::array<
        std::tuple<QuaderTransformAxis, std::string_view, std::string_view>, 3>
        kAxes{{
            {QuaderTransformAxis::X, "ui.menu.align.axis_x", "X"},
            {QuaderTransformAxis::Y, "ui.menu.align.axis_y", "Y"},
            {QuaderTransformAxis::Z, "ui.menu.align.axis_z", "Z"},
        }};
    for (const auto &[axis, key, fallback] : kAxes) {
      QMenu *axis_menu =
          align_menu->addMenu(qtext(context.localization, key, fallback));
      axis_menu->setObjectName(
          QStringLiteral("menu_align_%1")
              .arg(QString::fromUtf8(fallback.data()).toLower()));
      for (QuaderMeshAlignment alignment :
           {QuaderMeshAlignment::Minimum, QuaderMeshAlignment::Center,
            QuaderMeshAlignment::Maximum}) {
        add_arrangement_action(
            *axis_menu,
            arrangement_descriptor(QuaderMeshArrangementKind::Alignment,
                                   axis, alignment));
      }
    }
    align_menu->addSeparator();
    for (const auto &[distribution, key, fallback] :
         {std::tuple{QuaderMeshDistribution::Centers,
                     std::string_view("ui.menu.align.distribute_centers"),
                     std::string_view("Distribute Centers")},
          std::tuple{QuaderMeshDistribution::Gaps,
                     std::string_view("ui.menu.align.distribute_gaps"),
                     std::string_view("Distribute Gaps")}}) {
      QMenu *distribution_menu = align_menu->addMenu(
          qtext(context.localization, key, fallback));
      distribution_menu->setObjectName(
          QStringLiteral("menu_%1")
              .arg(QString::fromUtf8(key.data(),
                                     static_cast<qsizetype>(key.size()))));
      for (const auto &[axis, key, fallback] : kAxes) {
        (void)key;
        (void)fallback;
        add_arrangement_action(
            *distribution_menu,
            arrangement_descriptor(QuaderMeshArrangementKind::Distribution,
                                   axis, QuaderMeshAlignment::Center,
                                   distribution));
      }
    }
  }

  void build_cleanup_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.cleanup", "Clean Up"));
    menu->setObjectName(QStringLiteral("menu_cleanup"));
    add_command_action(*menu, quader_command::kModelingRemoveDoubles,
                       "ui.menu.cleanup.remove_doubles", "Remove Doubles");
  }

  void build_tools_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.tools", "Tools"));
    menu->setObjectName(QStringLiteral("menu_tools"));
    ToolMenuGroup current_group = ToolMenuGroup::Navigation;
    bool first = true;
    for (const Tool *tool : viewport_tool_catalog()) {
      if (tool == nullptr) {
        continue;
      }
      if (!first && tool->menu_group() != current_group) {
        menu->addSeparator();
      }
      first = false;
      current_group = tool->menu_group();
      QAction *action = menu->addAction(
          action_text(context.localization, tool_localization_key(*tool),
                      tool->label(), tool->shortcut()));
      action->setObjectName(
          QStringLiteral("tool_%1")
              .arg(QString::fromUtf8(tool->id().data(),
                                     static_cast<qsizetype>(tool->id().size()))));
      action->setCheckable(true);
      const ViewportToolId tool_id = tool->viewport_tool();
      QObject::connect(action, &QAction::triggered, menu_bar,
                       [this, tool_id]() {
                         (void)context.editor.tools().activate(tool_id);
                       });
      tool_actions.push_back({action, tool_id});
    }
  }

  void build_create_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.create", "Create"));
    menu->setObjectName(QStringLiteral("menu_create"));
    add_command_action(*menu, quader_command::kSceneCreatePointLight,
                       "ui.menu.create.point_light", "Point Light");
    add_command_action(*menu, quader_command::kSceneCreateDirectionalLight,
                       "ui.menu.create.directional_light",
                       "Directional Light");
  }

  void build_view_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.view", "View"));
    menu->setObjectName(QStringLiteral("menu_view"));
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowGrid,
                            "ui.menu.view.grid", "Grid");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowMeshGrid,
                            "ui.menu.view.mesh_grid", "Mesh Grid");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowGizmos,
                            "ui.menu.view.gizmos", "Gizmos");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowOverlays,
                            "ui.menu.view.overlays", "Overlays");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowInfo,
                            "ui.menu.view.show_info", "Show Info");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kViewportFocusAll,
                       "ui.menu.view.focus_all", "Focus All");
    add_command_action(*menu, quader_command::kViewportFrameSelection,
                       "ui.menu.view.focus_selection", "Focus Selection");
    menu->addSeparator();
    navigation_gizmo_action = menu->addAction(qtext(
        context.localization, "ui.menu.show.navigation_gizmo",
        "Navigation Gizmo"));
    navigation_gizmo_action->setObjectName(
        QStringLiteral("action_navigation_gizmo"));
    navigation_gizmo_action->setCheckable(true);
    QObject::connect(navigation_gizmo_action, &QAction::toggled, menu_bar,
                     [this](bool visible) {
                       (void)context.editor.preferences()
                           .set_navigation_gizmo_visible(visible);
                     });
    menu->addSeparator();
    QMenu *panels = menu->addMenu(
        qtext(context.localization, "ui.menu.view.toolbars", "Toolbars"));
    panels->setObjectName(QStringLiteral("menu_panels"));
    for (UIPanelId id :
         {UIPanelId::Outliner, UIPanelId::Inspector, UIPanelId::Console}) {
      QAction *action = panels->addAction(qtext(
          context.localization, ui_panel_localization_key(id),
          ui_panel_key(id)));
      action->setObjectName(
          QStringLiteral("panel_%1")
              .arg(QString::fromUtf8(ui_panel_stable_id(id))));
      action->setCheckable(true);
      QObject::connect(action, &QAction::toggled, menu_bar,
                       [this, id](bool visible) {
                         if (context.set_panel_visible) {
                           context.set_panel_visible(id, visible);
                         }
                       });
      panel_actions.push_back({action, id});
    }
  }

  void build_layout_menu() {
    QMenu *menu = menu_bar->addMenu(
        qtext(context.localization, "ui.menu.layout", "Layout"));
    menu->setObjectName(QStringLiteral("menu_layout"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (const ViewportLayoutOption &option : viewport_layout_options()) {
      QAction *action = menu->addAction(qtext(
          context.localization, layout_localization_key(option.id),
          option.label));
      action->setObjectName(
          QStringLiteral("layout_%1")
              .arg(QString::fromUtf8(viewport_layout_stable_id(option.id).data())));
      action->setCheckable(true);
      group->addAction(action);
      QObject::connect(action, &QAction::triggered, menu_bar,
                       [this, layout = option.id]() {
                         (void)context.editor.viewports().set_layout(layout);
                       });
      layout_actions.push_back({action, option.id});
    }
  }
};

QtMainMenu::QtMainMenu() = default;
QtMainMenu::~QtMainMenu() = default;

void QtMainMenu::initialize(QMenuBar &menu_bar, QtMainMenuContext context) {
  impl_ = std::make_unique<Impl>(std::move(context));
  impl_->menu_bar = &menu_bar;
  rebuild();
}

void QtMainMenu::rebuild() {
  if (!impl_ || impl_->menu_bar == nullptr) {
    return;
  }
  impl_->menu_bar->clear();
  impl_->command_actions.clear();
  impl_->bool_setting_actions.clear();
  impl_->layout_actions.clear();
  impl_->panel_actions.clear();
  impl_->tool_actions.clear();
  impl_->align_menu = nullptr;
  impl_->navigation_gizmo_action = nullptr;
  impl_->build_file_menu();
  impl_->build_edit_menu();
  impl_->build_align_menu();
  impl_->build_cleanup_menu();
  impl_->build_tools_menu();
  impl_->build_create_menu();
  impl_->build_view_menu();
  impl_->build_layout_menu();
  for (QAction *top_level_action : impl_->menu_bar->actions()) {
    if (QMenu *menu = top_level_action->menu()) {
      QObject::connect(menu, &QMenu::aboutToShow, impl_->menu_bar,
                       [this]() { refresh(); });
    }
  }
  refresh();
}

void QtMainMenu::refresh() {
  if (!impl_) {
    return;
  }
  const std::vector<QuaderCommandDescriptor> commands =
      impl_->context.editor.commands().available();
  for (Impl::CommandAction &entry : impl_->command_actions) {
    const QuaderCommandDescriptor *command =
        find_command(commands, entry.command_id);
    const std::string_view shortcut =
        command != nullptr ? std::string_view(command->shortcut)
                           : std::string_view{};
    entry.action->setText(action_text(impl_->context.localization,
                                      entry.localization_key, entry.fallback,
                                      shortcut));
    entry.action->setEnabled(command != nullptr && command->enabled);
  }
  for (const Impl::BoolSettingAction &entry :
       impl_->bool_setting_actions) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(
        impl_->context.viewport_settings.bool_value(entry.setting));
  }
  for (const Impl::LayoutAction &entry : impl_->layout_actions) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(impl_->context.editor.viewports().layout() ==
                             entry.layout);
  }
  for (const Impl::PanelAction &entry : impl_->panel_actions) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(impl_->context.panel_visible
                                 ? impl_->context.panel_visible(entry.panel)
                                 : false);
  }
  const std::string active_tool =
      impl_->context.editor.tools().active_tool();
  for (const Impl::ToolAction &entry : impl_->tool_actions) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(active_tool == viewport_tool_id(entry.tool));
    entry.action->setEnabled(viewport_tool_available(entry.tool) &&
                             impl_->context.editor.tools().can_activate(
                                 entry.tool));
  }
  if (impl_->navigation_gizmo_action != nullptr) {
    const QSignalBlocker blocker(impl_->navigation_gizmo_action);
    impl_->navigation_gizmo_action->setChecked(
        impl_->context.editor.preferences().navigation_gizmo_visible());
  }
  if (impl_->align_menu != nullptr) {
    impl_->align_menu->menuAction()->setVisible(
        impl_->context.modeling.selection_summary().mode ==
        ModelingSelectionMode::Object);
  }
}

} // namespace quader
