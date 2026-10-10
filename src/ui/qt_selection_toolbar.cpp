////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_selection_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include "core/localization_service.hpp"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QSize>
#include <QStyle>
#include <QToolButton>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace quader {
namespace {

constexpr int kSelectionModeButtonHeight = 30;

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
  setToolButtonStyle(Qt::ToolButtonIconOnly);

  auto *action_group = new QActionGroup(this);
  action_group->setExclusive(true);
  for (std::size_t index = 0; index < kSelectionModeSpecs.size(); ++index) {
    const SelectionModeSpec &spec = kSelectionModeSpecs[index];
    QAction *action = addAction(
        qt_multiresolution_icon(QString::fromUtf8(spec.icon_path)),
        QString::fromUtf8(spec.fallback.data()));
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    if (spec.mode != QuaderSelectionMode::Group) {
      action_group->addAction(action);
    }
    QObject::connect(action, &QAction::triggered, this,
                     [this, mode = spec.mode](bool checked) {
                       if (mode == QuaderSelectionMode::Group) {
                         if (group_selection_toggle_requested_) {
                           group_selection_toggle_requested_();
                         }
                       } else if (checked && mode_requested_) {
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

void QtSelectionToolbar::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  constexpr QSize kToolbarIconSize(32, 32);
  const QSize scaled_icon_size = metrics.size(kToolbarIconSize);
  if (iconSize() != scaled_icon_size) {
    setIconSize(scaled_icon_size);
  }

  const int scaled_button_height = metrics.px(kSelectionModeButtonHeight);
  for (const ModeAction &entry : mode_actions_) {
    apply_native_toolbar_button_face_ui_scale(*this, *entry.action, metrics);
    if (auto *button =
            qobject_cast<QToolButton *>(widgetForAction(entry.action));
        button != nullptr &&
        (button->minimumHeight() != scaled_button_height ||
         button->maximumHeight() != scaled_button_height)) {
      button->setFixedHeight(scaled_button_height);
    }
  }
}

void QtSelectionToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(localized_text(localization, "ui.toolbar.selection.title",
                                "Selection"));
  for (const ModeAction &entry : mode_actions_) {
    const QString text = localized_text(localization, entry.localization_key,
                                        entry.fallback);
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
  synchronization_valid_ = false;
}

void QtSelectionToolbar::synchronize(const QuaderEditorApi &editor,
                                     QuaderSelectionMode mode, bool force) {
  const auto mode_revision = editor.mode().revision();
  const auto keybindings_revision = editor.commands().keybindings_revision();
  if (!force && synchronization_valid_ && presented_editor_ == &editor &&
      presented_mode_ == mode &&
      presented_editor_mode_revision_ == mode_revision &&
      presented_keybindings_revision_ == keybindings_revision) {
    return;
  }

  set_selection_mode(mode);
  for (const ModeAction &entry : mode_actions_) {
    if (entry.mode == QuaderSelectionMode::Group) {
      const auto command = editor.commands().descriptor(
          quader_command::kSelectToggleGroupSelection);
      const bool enabled = command.has_value() && command->enabled;
      if (entry.action->isEnabled() != enabled) {
        entry.action->setEnabled(enabled);
      }
    }
    const std::string target_id = entry.mode == QuaderSelectionMode::Group
        ? std::string(quader_command::kSelectToggleGroupSelection.string())
        : editor.commands().keybinding_target_id_for_selection_mode(entry.mode);
    const std::string shortcut = editor.commands().shortcut_for(target_id);
    QString tooltip = entry.action->text();
    if (!shortcut.empty()) {
      tooltip.append(
          QStringLiteral(" [%1]").arg(QString::fromStdString(shortcut)));
    }
    if (entry.action->toolTip() != tooltip) {
      entry.action->setToolTip(tooltip);
    }
  }
  presented_editor_ = &editor;
  presented_editor_mode_revision_ = mode_revision;
  presented_keybindings_revision_ = keybindings_revision;
  synchronization_valid_ = true;
}

void QtSelectionToolbar::set_selection_mode(QuaderSelectionMode mode) {
  presented_mode_ = mode;
  synchronization_valid_ = false;
  for (const ModeAction &entry : mode_actions_) {
    const bool checked = entry.mode == mode ||
        (entry.mode == QuaderSelectionMode::Object &&
         mode == QuaderSelectionMode::Group);
    if (entry.action->isChecked() != checked) {
      // QActionGroup needs changed signals to retain the checked action.
      // setChecked does not emit triggered, which owns selection requests.
      entry.action->setChecked(checked);
    }
  }
}

void QtSelectionToolbar::set_mode_requested_callback(
    std::function<void(QuaderSelectionMode)> callback) {
  mode_requested_ = std::move(callback);
}

void QtSelectionToolbar::set_group_selection_toggle_requested_callback(
    std::function<void()> callback) {
  group_selection_toggle_requested_ = std::move(callback);
}

} // namespace quader
