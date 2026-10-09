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
#include <QComboBox>
#include <QIcon>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
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

  editor_mode_combo_ = new QComboBox(this);
  editor_mode_combo_->setObjectName(QStringLiteral("editorModeCombo"));
  editor_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  // Keep the current native contents width as the toolbar minimum. Qt refreshes
  // sizeHint for font/label changes but may retain an earlier minimumSizeHint.
  editor_mode_combo_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  editor_mode_combo_->setFixedHeight(kSelectionModeButtonHeight);
  editor_mode_combo_->addItem(QStringLiteral("Object Mode"),
                             static_cast<int>(QuaderEditorMode::Object));
  editor_mode_combo_->addItem(QStringLiteral("Edit Mode"),
                             static_cast<int>(QuaderEditorMode::Edit));
  editor_mode_tooltip_ = QStringLiteral("Editor Mode");
  editor_mode_combo_->setAccessibleName(editor_mode_tooltip_);
  addWidget(editor_mode_combo_);
  addSeparator();
  QObject::connect(editor_mode_combo_, &QComboBox::activated, this,
                   [this](int index) {
                     if (editor_mode_requested_) {
                       editor_mode_requested_(static_cast<QuaderEditorMode>(
                           editor_mode_combo_->itemData(index).toInt()));
                     }
                   });

  auto *action_group = new QActionGroup(this);
  action_group->setExclusive(true);
  for (std::size_t index = 0; index < kSelectionModeSpecs.size(); ++index) {
    const SelectionModeSpec &spec = kSelectionModeSpecs[index];
    QAction *action = addAction(
        qt_multiresolution_icon(QString::fromUtf8(spec.icon_path)),
        QString::fromUtf8(spec.fallback.data()));
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    const bool object_mode = spec.mode == QuaderSelectionMode::Object ||
        spec.mode == QuaderSelectionMode::Group;
    action->setEnabled(object_mode);
    action->setVisible(object_mode);
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

void QtSelectionToolbar::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  constexpr QSize kToolbarIconSize(32, 32);
  const QSize scaled_icon_size = metrics.size(kToolbarIconSize);
  if (iconSize() != scaled_icon_size) {
    setIconSize(scaled_icon_size);
  }

  const int scaled_button_height = metrics.px(kSelectionModeButtonHeight);
  if (editor_mode_combo_->minimumHeight() != scaled_button_height ||
      editor_mode_combo_->maximumHeight() != scaled_button_height) {
    editor_mode_combo_->setFixedHeight(scaled_button_height);
  }
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
  editor_mode_combo_->setItemText(0, localized_text(
      localization, "command.editor.mode.object", "Object Mode"));
  editor_mode_combo_->setItemText(1, localized_text(
      localization, "command.editor.mode.edit", "Edit Mode"));
  editor_mode_tooltip_ = localized_text(
      localization, "ui.toolbar.editor_mode", "Editor Mode");
  editor_mode_combo_->setAccessibleName(editor_mode_tooltip_);
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
  const bool edit_mode = editor.mode().current() == QuaderEditorMode::Edit;
  const int editor_mode_index = edit_mode ? 1 : 0;
  if (editor_mode_combo_->currentIndex() != editor_mode_index) {
    const QSignalBlocker blocker(editor_mode_combo_);
    editor_mode_combo_->setCurrentIndex(editor_mode_index);
  }
  const std::string mode_shortcut = editor.commands().shortcut_for(
      quader_command::kEditorModeToggleEdit.string());
  QString mode_tooltip = editor_mode_tooltip_;
  if (!mode_shortcut.empty()) {
    mode_tooltip.append(QStringLiteral(" [%1]").arg(
        QString::fromStdString(mode_shortcut)));
  }
  if (editor_mode_combo_->toolTip() != mode_tooltip) {
    editor_mode_combo_->setToolTip(mode_tooltip);
  }
  for (const ModeAction &entry : mode_actions_) {
    const bool component_mode = entry.mode == QuaderSelectionMode::Vertex ||
        entry.mode == QuaderSelectionMode::Edge ||
        entry.mode == QuaderSelectionMode::Face;
    if (entry.action->isEnabled() != (component_mode == edit_mode)) {
      entry.action->setEnabled(component_mode == edit_mode);
    }
    const bool visible = component_mode == edit_mode;
    if (entry.action->isVisible() != visible) {
      entry.action->setVisible(visible);
    }
    const std::string target_id =
        editor.commands().keybinding_target_id_for_selection_mode(entry.mode);
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
    if (entry.action->isChecked() != (entry.mode == mode)) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(entry.mode == mode);
    }
  }
}

void QtSelectionToolbar::set_mode_requested_callback(
    std::function<void(QuaderSelectionMode)> callback) {
  mode_requested_ = std::move(callback);
}

void QtSelectionToolbar::set_editor_mode_requested_callback(
    std::function<void(QuaderEditorMode)> callback) {
  editor_mode_requested_ = std::move(callback);
}

} // namespace quader
