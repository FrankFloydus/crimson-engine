////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_quick_menu.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_scene_documents_api.hpp"
#include "api/quader_viewport_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

struct BoundQuickMenuCommand final {
  QAction *action = nullptr;
  QuaderCommandId command_id;
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

QString localized(const LocalizationService &localization,
                  std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(std::string(key),
                                                   std::string(fallback)));
}

void add_menu_title(QMenu &menu, const QString &title,
                    const QString &object_name) {
  auto *title_action = new QWidgetAction(&menu);
  title_action->setObjectName(object_name);
  title_action->setText(title);

  auto *title_label = new QLabel(title);
  title_label->setObjectName(object_name + QStringLiteral("_label"));
  title_label->setAccessibleName(title);
  title_label->setFocusPolicy(Qt::NoFocus);
  title_label->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_label->setContentsMargins(10, 4, 10, 4);

  QFont title_font = title_label->font();
  title_font.setBold(true);
  title_label->setFont(title_font);

  // A disabled QWidgetAction keeps the header out of pointer and keyboard
  // selection. Retain the menu's normal text color so it reads as a title,
  // rather than as an unavailable command.
  QPalette title_palette = title_label->palette();
  const auto title_color =
      menu.palette().color(QPalette::Active, QPalette::Text);
  title_palette.setColor(QPalette::Active, QPalette::WindowText, title_color);
  title_palette.setColor(QPalette::Inactive, QPalette::WindowText,
                         title_color);
  title_palette.setColor(QPalette::Disabled, QPalette::WindowText,
                         title_color);
  title_label->setPalette(title_palette);

  title_action->setDefaultWidget(title_label);
  title_action->setEnabled(false);
  menu.addAction(title_action);

  QAction *separator = menu.addSeparator();
  separator->setObjectName(object_name + QStringLiteral("_separator"));
}

QAction *add_command_action(
    QMenu &menu, std::vector<BoundQuickMenuCommand> &bound_commands,
    QuaderEditorApi &editor, const LocalizationService &localization,
    std::string_view localization_key, std::string_view fallback,
    QuaderCommandId command_id) {
  if (!release_allows_command(command_id.string())) {
    return nullptr;
  }
  QString label = localized(localization, localization_key, fallback);
  label.replace(QLatin1Char('&'), QStringLiteral("&&"));
  QAction *action = menu.addAction(label);
  action->setObjectName(QString::fromUtf8(
      command_id.string().data(),
      static_cast<qsizetype>(command_id.string().size())));
  const std::optional<QuaderCommandDescriptor> descriptor =
      editor.commands().descriptor(command_id);
  action->setEnabled(descriptor.has_value() && descriptor->enabled);
  bound_commands.push_back({.action = action, .command_id = command_id});
  return action;
}

void build_add_menu(QMenu &menu,
                    std::vector<BoundQuickMenuCommand> &bound_commands,
                    QuaderEditorApi &editor,
                    const LocalizationService &localization) {
  menu.setObjectName(QStringLiteral("viewport_add_quick_menu"));
  const QString title = localized(
      localization, "ui.viewport.quick_menu.add.title", "Add");
  menu.setTitle(title);
  menu.setAccessibleName(title);
  add_menu_title(menu, title,
                 QStringLiteral("viewport_add_quick_menu_title"));

  QMenu *mesh_menu = menu.addMenu(localized(
      localization, "ui.viewport.quick_menu.add.mesh", "Mesh"));
  mesh_menu->setObjectName(QStringLiteral("viewport_add_mesh_submenu"));
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.box", "Box",
      quader_command::kToolsActivateCreateBox);
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.cylinder", "Cylinder",
      quader_command::kToolsActivateCreateCylinder);
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.sphere", "Sphere",
      quader_command::kToolsActivateCreateSphere);
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.torus", "Torus",
      quader_command::kToolsActivateCreateTorus);
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.cone", "Cone",
      quader_command::kToolsActivateCreateCone);
  (void)add_command_action(
      *mesh_menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.stairs", "Stairs & Ramps",
      quader_command::kToolsActivateCreateStairs);

  (void)add_command_action(
      menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.curve", "Curve",
      quader_command::kToolsActivateCreateCurve);

  if (release_allows_command(quader_command::kSceneCreatePointLight.string())) {
    QMenu *light_menu = menu.addMenu(localized(
        localization, "ui.viewport.quick_menu.add.light", "Light"));
    light_menu->setObjectName(QStringLiteral("viewport_add_light_submenu"));
    (void)add_command_action(
        *light_menu, bound_commands, editor, localization,
        "ui.viewport.quick_menu.add.point", "Point",
        quader_command::kSceneCreatePointLight);
    QAction *spotlight_action = light_menu->addAction(localized(
        localization, "ui.viewport.quick_menu.add.spotlight_nyi",
        "Spotlight (NYI)"));
    spotlight_action->setObjectName(
        QStringLiteral("viewport_add_spotlight_nyi"));
    spotlight_action->setEnabled(false);
    (void)add_command_action(
        *light_menu, bound_commands, editor, localization,
        "ui.viewport.quick_menu.add.directional", "Directional",
        quader_command::kSceneCreateDirectionalLight);
  }

  (void)add_command_action(
      menu, bound_commands, editor, localization,
      "ui.viewport.quick_menu.add.empty", "Empty",
      quader_command::kSceneCreateEmpty);
}

} // namespace

bool QtViewportQuickMenuPresenter::show(
    QWidget &focus_parent, const QPoint &global_point,
    ViewportPaneId initiating_pane, QuaderEditorApi &editor,
    ViewportInputRouter &input_router, LocalizationService &localization,
    ViewportRuntimeBindingToken runtime_binding) const {
  QDR_PROFILE_SCOPE("Qt viewport quick menu show");
  if (!runtime_binding.current() ||
      editor.viewports().focused_pane() != initiating_pane) {
    return false;
  }

  // QMenu::exec() enters a nested event loop. Fence every owner whose state is
  // used to interpret the selected row before dispatching the chosen command.
  const QPointer<QWidget> guarded_focus_parent(&focus_parent);
  const std::optional<SceneSessionId> presented_scene_session =
      editor.scene_documents().active_scene_id();
  const QuaderRevisionStamp presented_context = editor.revisions();
  const ViewportLayoutId presented_layout = editor.viewports().layout();

  // The popup is stack-owned. Do not QObject-parent it to focus_parent: the
  // nested exec() loop may destroy that widget, which would otherwise delete
  // this stack object before show() can reach its QPointer fence.
  QMenu menu;
  std::vector<BoundQuickMenuCommand> bound_commands;
  bound_commands.reserve(9U);
  {
    QDR_PROFILE_SCOPE("Qt viewport quick menu build");
    build_add_menu(menu, bound_commands, editor, localization);
  }

  QAction *selected_action = nullptr;
  {
    QDR_PROFILE_SCOPE("Qt viewport quick menu exec");
    selected_action = menu.exec(global_point);
  }

  const bool pane_context_current =
      guarded_focus_parent != nullptr && runtime_binding.current() &&
      editor.scene_documents().active_scene_id() == presented_scene_session &&
      same_context_revision(presented_context, editor.revisions()) &&
      editor.viewports().layout() == presented_layout &&
      editor.viewports().focused_pane() == initiating_pane;
  if (!pane_context_current) {
    return false;
  }

  guarded_focus_parent->setFocus(Qt::ShortcutFocusReason);
  if (selected_action == nullptr) {
    return false;
  }

  const auto selected = std::find_if(
      bound_commands.begin(), bound_commands.end(),
      [selected_action](const BoundQuickMenuCommand &entry) {
        return entry.action == selected_action;
      });
  if (selected == bound_commands.end()) {
    return false;
  }
  const std::optional<QuaderCommandDescriptor> current_descriptor =
      editor.commands().descriptor(selected->command_id);
  if (!current_descriptor.has_value() || !current_descriptor->enabled) {
    return false;
  }

  const QuaderApiVoidResult result = [&] {
    QDR_PROFILE_SCOPE("Qt viewport quick menu dispatch command");
    return editor.commands().execute(selected->command_id);
  }();
  if (!runtime_binding.current()) {
    return false;
  }
  if (!result.ok()) {
    LOG_ERROR("Viewport quick menu command '{}' failed: {}",
              selected->command_id.string(),
              result.error_message().empty() ? "Operation failed."
                                             : result.error_message());
    return false;
  }
  input_router.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  return result.changed();
}

} // namespace quader
