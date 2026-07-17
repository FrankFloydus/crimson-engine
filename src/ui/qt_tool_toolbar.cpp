////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QSignalBlocker>
#include <QSize>
#include <QStyle>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace quader {
namespace {

/// Describes one requested toolbar tool, its icon, and group separation.
struct ToolIconSpec {
  ViewportToolId tool;
  const char *icon_path;
  bool separator_before = false;
};

constexpr std::array<ToolIconSpec, 14> kToolIconSpecs{{
    {ViewportToolId::Select, ":/quader/icons/tool_select.png"},
    {ViewportToolId::Move, ":/quader/icons/tool_move.png"},
    {ViewportToolId::Rotate, ":/quader/icons/tool_rotate.png"},
    {ViewportToolId::Scale, ":/quader/icons/tool_scale.png"},
    {ViewportToolId::Pivot, ":/quader/icons/tool_pivot.png"},
    {ViewportToolId::CreatePrimitive,
     ":/quader/icons/tool_create_primitive.png", true},
    {ViewportToolId::Extrude, ":/quader/icons/tool_extrude.png"},
    {ViewportToolId::InsertEdgeLoop,
     ":/quader/icons/tool_insert_edge_loop.png"},
    {ViewportToolId::Knife, ":/quader/icons/tool_knife.png"},
    {ViewportToolId::Cut, ":/quader/icons/tool_cut.png"},
    {ViewportToolId::Poly, ":/quader/icons/tool_poly.png"},
    {ViewportToolId::Mirror, ":/quader/icons/tool_mirror.png"},
    {ViewportToolId::LinearArray, ":/quader/icons/tool_linear_array.png"},
    {ViewportToolId::RadialArray, ":/quader/icons/tool_radial_array.png"},
}};

QString localized_tool_text(const LocalizationService &localization,
                            std::string_view tool_id,
                            std::string_view fallback) {
  std::string key = "viewport.tool.";
  key.append(tool_id);
  key.append(".label");
  return QString::fromStdString(localization.text(key, fallback));
}

} // namespace

QtToolToolbar::QtToolToolbar(QWidget *parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("toolToolbar"));
  setWindowTitle(QStringLiteral("Tools"));
  setAllowedAreas(Qt::AllToolBarAreas);
  setMovable(true);
  setFloatable(true);
  setIconSize(QSize(32, 32));
  setToolButtonStyle(Qt::ToolButtonIconOnly);

  auto *action_group = new QActionGroup(this);
  action_group->setExclusive(true);
  for (std::size_t index = 0; index < kToolIconSpecs.size(); ++index) {
    const ToolIconSpec &spec = kToolIconSpecs[index];
    if (spec.separator_before) {
      addSeparator();
    }
    const std::string_view tool_id = viewport_tool_catalog_id(spec.tool);
    const std::string_view fallback = viewport_tool_label(spec.tool);
    QAction *action = addAction(QIcon(QString::fromUtf8(spec.icon_path)),
                                QString::fromUtf8(fallback.data()));
    action->setObjectName(
        QStringLiteral("tool_%1").arg(QString::fromUtf8(tool_id.data())));
    action->setCheckable(true);
    action_group->addAction(action);
    QObject::connect(action, &QAction::triggered, this,
                     [this, tool_id](bool checked) {
                       if (checked && tool_requested_) {
                         tool_requested_(tool_id);
                       }
                     });
    tool_actions_[index] = {
        .action = action,
        .tool = spec.tool,
        .tool_id = tool_id,
    };
  }
  QStyle &button_presentation =
      install_native_toolbar_button_presentation(*this);
  for (const ToolAction &entry : tool_actions_) {
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
  }
}

void QtToolToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(QString::fromStdString(
      localization.text("ui.toolbar.tools.title", "Tools")));
  for (const ToolAction &entry : tool_actions_) {
    const QString text = localized_tool_text(
        localization, entry.tool_id, viewport_tool_label(entry.tool));
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
  synchronization_valid_ = false;
}

void QtToolToolbar::synchronize(const QuaderEditorApi &editor,
                                const QuaderSelectionSummary &selection,
                                bool force) {
  const std::string active_tool = editor.tools().active_tool();
  if (!force && synchronization_valid_ &&
      synchronized_active_tool_ == active_tool &&
      synchronized_selection_mode_ == selection.mode &&
      synchronized_selection_revision_ == selection.selection_revision) {
    return;
  }
  synchronization_valid_ = true;
  synchronized_active_tool_ = active_tool;
  synchronized_selection_mode_ = selection.mode;
  synchronized_selection_revision_ = selection.selection_revision;

  for (const ToolAction &entry : tool_actions_) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(active_tool == entry.tool_id);
    const std::string command_id =
        editor.commands().command_id_for_tool_activation(entry.tool_id);
    const std::optional<QuaderCommandDescriptor> command =
        editor.commands().descriptor(command_id);
    entry.action->setEnabled(command.has_value() && command->enabled);
    QString tooltip = entry.action->text();
    if (command.has_value() && !command->shortcut.empty()) {
      tooltip.append(QStringLiteral(" [%1]")
                         .arg(QString::fromStdString(command->shortcut)));
    }
    entry.action->setToolTip(tooltip);
  }
}

void QtToolToolbar::set_tool_requested_callback(
    std::function<void(std::string_view)> callback) {
  tool_requested_ = std::move(callback);
}

} // namespace quader
