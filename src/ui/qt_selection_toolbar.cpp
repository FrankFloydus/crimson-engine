////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_selection_toolbar.hpp"

#include "ui/qt_toolbar_button_presentation.hpp"

#include "core/localization_service.hpp"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QLabel>
#include <QSignalBlocker>
#include <QSize>
#include <QStyle>
#include <QToolButton>

#include <array>
#include <cstddef>
#include <utility>

namespace quader {
namespace {

constexpr int kSelectionModeButtonHeight = 36;

/// Describes one stable selection toolbar action and its localized resources.
struct SelectionModeSpec {
  QuaderSelectionMode mode;
  const char *icon_path;
  const char *object_name;
  std::string_view localization_key;
  std::string_view fallback;
};

constexpr std::array<SelectionModeSpec, 5> kSelectionModeSpecs{{
    {QuaderSelectionMode::Vertex, ":/quader/icons/selection_vertex.png",
     "selectionModeVertex", "ui.toolbar.selection.vertices", "Vertices"},
    {QuaderSelectionMode::Edge, ":/quader/icons/selection_edge.png",
     "selectionModeEdge", "ui.toolbar.selection.edges", "Edges"},
    {QuaderSelectionMode::Face, ":/quader/icons/selection_face.png",
     "selectionModeFace", "ui.toolbar.selection.faces", "Faces"},
    {QuaderSelectionMode::Object, ":/quader/icons/selection_mesh.png",
     "selectionModeMesh", "ui.toolbar.selection.meshes", "Meshes"},
    {QuaderSelectionMode::Group, ":/quader/icons/selection_group.png",
     "selectionModeGroup", "ui.toolbar.selection.groups", "Groups"},
}};

QString localized_text(const LocalizationService &localization,
                       std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

} // namespace

QtSelectionToolbar::QtSelectionToolbar(QWidget *parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("selectionToolbar"));
  setWindowTitle(QStringLiteral("Selection"));
  setAllowedAreas(Qt::AllToolBarAreas);
  setMovable(true);
  setFloatable(true);
  setIconSize(QSize(32, 32));
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  selection_label_ = new QLabel(QStringLiteral("Select:"), this);
  selection_label_->setObjectName(QStringLiteral("selectionToolbarLabel"));
  addWidget(selection_label_);
  addSeparator();

  auto *action_group = new QActionGroup(this);
  action_group->setExclusive(true);
  for (std::size_t index = 0; index < kSelectionModeSpecs.size(); ++index) {
    const SelectionModeSpec &spec = kSelectionModeSpecs[index];
    QAction *action = addAction(QIcon(QString::fromUtf8(spec.icon_path)),
                                QString::fromUtf8(spec.fallback.data()));
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    action_group->addAction(action);
    QObject::connect(action, &QAction::triggered, this,
                     [this, mode = spec.mode](bool checked) {
                       if (checked && mode_requested_) {
                         mode_requested_(mode);
                       }
                     });
    mode_actions_[index] = {
        .action = action,
        .mode = spec.mode,
        .localization_key = spec.localization_key,
        .fallback = spec.fallback,
    };
  }
  QStyle &button_presentation =
      install_native_toolbar_button_presentation(*this);
  for (const ModeAction &entry : mode_actions_) {
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
    if (auto *button =
            qobject_cast<QToolButton *>(widgetForAction(entry.action))) {
      button->setFixedHeight(kSelectionModeButtonHeight);
    }
  }
  set_selection_mode(QuaderSelectionMode::Object);
}

void QtSelectionToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(localized_text(localization, "ui.toolbar.selection.title",
                                "Selection"));
  selection_label_->setText(localized_text(
      localization, "ui.toolbar.selection.label", "Select:"));
  for (const ModeAction &entry : mode_actions_) {
    const QString text = localized_text(localization, entry.localization_key,
                                        entry.fallback);
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
}

void QtSelectionToolbar::set_selection_mode(QuaderSelectionMode mode) {
  if (presented_mode_ == mode) {
    return;
  }
  presented_mode_ = mode;
  for (const ModeAction &entry : mode_actions_) {
    const QSignalBlocker blocker(entry.action);
    entry.action->setChecked(entry.mode == mode);
  }
}

void QtSelectionToolbar::set_mode_requested_callback(
    std::function<void(QuaderSelectionMode)> callback) {
  mode_requested_ = std::move(callback);
}

} // namespace quader
