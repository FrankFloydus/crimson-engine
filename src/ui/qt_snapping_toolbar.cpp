////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_snapping_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_numeric_format.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QGuiApplication>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QScreen>
#include <QSignalBlocker>
#include <QSize>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace quader {
namespace {

constexpr int kTopToolbarButtonHeight = 30;

/**
 * Keeps Fusion's native combo popup while preferring a conventional dropdown
 * position below the field whenever the complete popup fits on-screen.
 */
class QtDownwardPopupComboBox final : public QComboBox {
public:
  using QComboBox::QComboBox;

  void showPopup() override {
    QComboBox::showPopup();

    QAbstractItemView *item_view = view();
    if (item_view != nullptr && currentIndex() >= 0) {
      const QModelIndex committed_index = model()->index(
          currentIndex(), modelColumn(), rootModelIndex());
      if (committed_index.isValid()) {
        item_view->setCurrentIndex(committed_index);
        if (QItemSelectionModel *selection = item_view->selectionModel()) {
          selection->select(
              committed_index,
              QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        item_view->scrollTo(committed_index,
                            QAbstractItemView::PositionAtCenter);
      }
    }

    QWidget *popup = item_view != nullptr ? item_view->window() : nullptr;
    if (popup == nullptr || popup == window() || !popup->isVisible() ||
        popup->windowType() != Qt::Popup) {
      return;
    }

    const QPoint below = mapToGlobal(QPoint(0, height()));
    QScreen *popup_screen = QGuiApplication::screenAt(below);
    if (popup_screen == nullptr) {
      popup_screen = screen();
    }
    if (popup_screen == nullptr) {
      return;
    }

    QRect popup_geometry = popup->geometry();
    const QRect available = popup_screen->availableGeometry();
    const int proposed_bottom = below.y() + popup_geometry.height() - 1;
    if (below.y() < available.top() || proposed_bottom > available.bottom()) {
      return;
    }

    popup_geometry.moveTop(below.y());
    popup->setGeometry(popup_geometry);
  }
};

/// Provides stable indexes for the four retained snapping toggle actions.
enum ToggleIndex : std::size_t {
  SnappingEnabled = 0,
  GridSnapping,
  VertexSnapping,
  RotationSnapping,
  ToggleCount,
};

static_assert(ToggleCount == 4);

/// Describes one stable snapping toolbar action and its localized resources.
struct ToggleSpec {
  const char *icon_path;
  const char *object_name;
  std::string_view localization_key;
  std::string_view fallback;
};

constexpr std::array<ToggleSpec, ToggleCount> kToggleSpecs{{
    {":/quader/icons/snapping_master.png", "snappingEnabled",
     "ui.toolbar.snapping.enabled", "Toggle Snapping"},
    {":/quader/icons/snapping_grid.png", "gridSnapping",
     "ui.toolbar.snapping.grid_snap", "Grid Snapping"},
    {":/quader/icons/snapping_vertex.png", "vertexSnapping",
     "ui.toolbar.snapping.vertex_snap", "Vertex Snapping"},
    {":/quader/icons/snapping_rotation.png", "rotationSnapping",
     "ui.toolbar.snapping.rotation_snap", "Rotation Snapping"},
}};

QString localized_text(const LocalizationService &localization,
                       std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

} // namespace

QtSnappingToolbar::QtSnappingToolbar(QWidget *parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("snappingToolbar"));
  setWindowTitle(QStringLiteral("Snapping"));
  setAllowedAreas(Qt::AllToolBarAreas);
  setMovable(true);
  setFloatable(true);
  setIconSize(QSize(32, 32));
  setToolButtonStyle(Qt::ToolButtonIconOnly);

  grid_label_ = new QLabel(QStringLiteral("Grid:"), this);
  grid_label_->setObjectName(QStringLiteral("snappingGridSizeLabel"));
  addWidget(grid_label_);

  grid_size_combo_ = new QtDownwardPopupComboBox(this);
  grid_size_combo_->setObjectName(QStringLiteral("snappingGridSizeCombo"));
  grid_size_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  grid_size_combo_->setMaxVisibleItems(10);
  grid_size_combo_->setFixedHeight(kTopToolbarButtonHeight);
  const auto grid_presets = viewport_grid_preset_catalog();
  for (const ViewportGridPresetDescriptor &preset : grid_presets) {
    grid_size_combo_->addItem(qt_format_number(preset.map_units, QLocale::c()),
                             preset.preset);
  }
  grid_label_->setBuddy(grid_size_combo_);
  addWidget(grid_size_combo_);

  angle_label_ = new QLabel(QStringLiteral("Angle:"), this);
  angle_label_->setObjectName(QStringLiteral("snappingAngleLabel"));
  addWidget(angle_label_);

  angle_combo_ = new QtDownwardPopupComboBox(this);
  angle_combo_->setObjectName(QStringLiteral("snappingAngleCombo"));
  angle_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  angle_combo_->setMaxVisibleItems(
      static_cast<int>(kViewportRotationSnapAngleCount));
  angle_combo_->setFixedHeight(kTopToolbarButtonHeight);
  for (const double degrees : viewport_rotation_snap_degrees_catalog()) {
    angle_combo_->addItem(
        QStringLiteral("%1\u00B0")
            .arg(qt_format_number(degrees, QLocale::c())), degrees);
  }
  angle_label_->setBuddy(angle_combo_);
  addWidget(angle_combo_);

  transform_space_action_ = new QAction(
      qt_multiresolution_icon(
          QStringLiteral(":/quader/icons/tool_transform_local.png")),
      QStringLiteral("Local Transforms"), this);
  transform_space_action_->setObjectName(
      QStringLiteral("toggle_transform_space"));
  transform_space_action_->setCheckable(true);
  QObject::connect(transform_space_action_, &QAction::triggered, this,
                   [this](bool) {
                     transform_presentation_stamp_.reset();
                     if (transform_space_toggle_requested_) {
                       transform_space_toggle_requested_();
                     }
                   });

  QObject::connect(grid_size_combo_, &QComboBox::activated, this,
                   [this](int index) {
                     presented_state_.reset();
                     if (grid_preset_requested_ == nullptr) {
                       return;
                     }
                     bool valid = false;
                     const int preset =
                         grid_size_combo_->itemData(index).toInt(&valid);
                     if (valid) {
                       grid_preset_requested_(preset);
                     }
                   });
  QObject::connect(angle_combo_, &QComboBox::activated, this,
                   [this](int index) {
                     presented_state_.reset();
                     if (rotation_snap_degrees_requested_ != nullptr) {
                       rotation_snap_degrees_requested_(
                           angle_combo_->itemData(index).toDouble());
                     }
                   });

  for (std::size_t index = 0; index < kToggleSpecs.size(); ++index) {
    const ToggleSpec &spec = kToggleSpecs[index];
    QAction *action = new QAction(
        qt_multiresolution_icon(QString::fromUtf8(spec.icon_path)),
        QString::fromUtf8(spec.fallback.data(),
                          static_cast<qsizetype>(spec.fallback.size())), this);
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    QObject::connect(action, &QAction::triggered, this,
                     [this, index](bool checked) {
                       presented_state_.reset();
                       if (toggle_requested_[index] != nullptr) {
                         toggle_requested_[index](checked);
                       }
                     });
    toggle_actions_[index] = {
        .action = action,
        .localization_key = spec.localization_key,
        .fallback = spec.fallback,
    };
  }

  install_native_toolbar_button_presentation(*this);
}

std::array<QAction *, 5> QtSnappingToolbar::viewport_actions() const {
  return {transform_space_action_, toggle_actions_[SnappingEnabled].action,
          toggle_actions_[GridSnapping].action,
          toggle_actions_[VertexSnapping].action,
          toggle_actions_[RotationSnapping].action};
}

void QtSnappingToolbar::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  apply_native_toolbar_presentation_ui_scale(*this, metrics);
  constexpr QSize kToolbarIconSize(32, 32);
  const QSize scaled_icon_size = metrics.size(kToolbarIconSize);
  if (iconSize() != scaled_icon_size) {
    setIconSize(scaled_icon_size);
  }

  const int scaled_control_height = metrics.px(kTopToolbarButtonHeight);
  if (grid_size_combo_->minimumHeight() != scaled_control_height ||
      grid_size_combo_->maximumHeight() != scaled_control_height) {
    grid_size_combo_->setFixedHeight(scaled_control_height);
  }
  if (angle_combo_->minimumHeight() != scaled_control_height ||
      angle_combo_->maximumHeight() != scaled_control_height) {
    angle_combo_->setFixedHeight(scaled_control_height);
  }
}

void QtSnappingToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(localized_text(localization, "ui.toolbar.snapping.title",
                                "Snapping"));

  const QString grid_label = localized_text(
      localization, "ui.toolbar.snapping.grid_label", "Grid:");
  const QString angle_label = localized_text(
      localization, "ui.toolbar.snapping.angle_label", "Angle:");
  grid_tooltip_base_ = localized_text(
      localization, "ui.toolbar.snapping.grid_size", "Grid Size");
  angle_tooltip_base_ = localized_text(
      localization, "ui.toolbar.snapping.rotation_angle",
      "Rotation Snap Angle");

  grid_label_->setText(grid_label);
  angle_label_->setText(angle_label);
  grid_size_combo_->setAccessibleName(grid_tooltip_base_);
  grid_size_combo_->setAccessibleDescription(grid_tooltip_base_);
  grid_size_combo_->setToolTip(grid_tooltip_base_);
  angle_combo_->setAccessibleName(angle_tooltip_base_);
  angle_combo_->setAccessibleDescription(angle_tooltip_base_);
  angle_combo_->setToolTip(angle_tooltip_base_);

  const QString transform_space_text = localized_text(
      localization, "ui.toolbar.tools.transform_space", "Local Transforms");
  transform_space_action_->setText(transform_space_text);
  transform_space_action_->setToolTip(transform_space_text);
  transform_presentation_stamp_.reset();

  for (const ToggleAction &entry : toggle_actions_) {
    const QString text = localized_text(localization, entry.localization_key,
                                        entry.fallback);
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
  presented_state_.reset();
}

void QtSnappingToolbar::synchronize(const QuaderEditorApi &editor,
                                    bool force) {
  synchronize_transform_space(editor, force);

  const auto &viewports = editor.viewports();
  const auto pane = viewports.focused_pane();
  const PresentedState state{
      .editor = &editor,
      .pane = pane,
      .control_revision = viewports.control_revision(pane),
      .keybindings_revision = editor.commands().keybindings_revision(),
      .grid_preset = viewports.grid_preset(),
      .rotation_snap_degrees = viewports.rotation_snap_degrees(pane),
      .toggles =
          {
              viewports.control(pane, QuaderViewportControl::SnappingEnabled),
              viewports.control(pane, QuaderViewportControl::GridSnap),
              viewports.control(pane, QuaderViewportControl::VertexSnap),
              viewports.control(pane, QuaderViewportControl::RotationSnap),
          },
  };
  if (!force && presented_state_.has_value() &&
      *presented_state_ == state) {
    return;
  }

  const bool update_grid =
      force || !presented_state_.has_value() ||
      presented_state_->editor != state.editor ||
      presented_state_->keybindings_revision != state.keybindings_revision ||
      presented_state_->grid_preset != state.grid_preset;
  const bool update_angle =
      force || !presented_state_.has_value() ||
      presented_state_->rotation_snap_degrees !=
          state.rotation_snap_degrees;

  if (update_grid) {
    const QSignalBlocker blocker(grid_size_combo_);
    const int selected_index = grid_size_combo_->findData(state.grid_preset);
    if (selected_index >= 0 &&
        grid_size_combo_->currentIndex() != selected_index) {
      grid_size_combo_->setCurrentIndex(selected_index);
    }

    const std::string target_id =
        editor.commands().keybinding_target_id_for_grid_preset(
            state.grid_preset);
    const std::string shortcut = editor.commands().shortcut_for(target_id);
    QString tooltip = grid_tooltip_base_;
    if (!shortcut.empty()) {
      tooltip.append(QStringLiteral(" [%1]")
                         .arg(QString::fromStdString(shortcut)));
    }
    if (grid_size_combo_->toolTip() != tooltip) {
      grid_size_combo_->setToolTip(tooltip);
    }
    if (grid_size_combo_->accessibleDescription() != tooltip) {
      grid_size_combo_->setAccessibleDescription(tooltip);
    }
  }

  if (update_angle) {
    const QSignalBlocker blocker(angle_combo_);
    const int selected_index =
        angle_combo_->findData(state.rotation_snap_degrees);
    if (selected_index >= 0 && angle_combo_->currentIndex() != selected_index) {
      angle_combo_->setCurrentIndex(selected_index);
    }
  }

  for (std::size_t index = 0; index < toggle_actions_.size(); ++index) {
    const bool changed = force || !presented_state_.has_value() ||
                         presented_state_->toggles[index] !=
                             state.toggles[index];
    if (!changed || toggle_actions_[index].action->isChecked() ==
                        state.toggles[index]) {
      continue;
    }
    const QSignalBlocker blocker(toggle_actions_[index].action);
    toggle_actions_[index].action->setChecked(state.toggles[index]);
  }

  presented_state_ = state;
}

void QtSnappingToolbar::synchronize_transform_space(
    const QuaderEditorApi &editor, bool force) {
  const TransformPresentationStamp stamp{
      .editor = &editor,
      .pane = editor.viewports().focused_pane(),
      .transform_space_revision = editor.viewports().transform_space_revision(),
      .keybindings_revision = editor.commands().keybindings_revision(),
  };
  if (!force && transform_presentation_stamp_ == stamp) {
    return;
  }

  const std::optional<QuaderCommandDescriptor> command =
      editor.commands().descriptor(
          quader_command::kViewportToggleTransformSpace);
  const bool enabled = command.has_value() && command->enabled;
  if (transform_space_action_->isEnabled() != enabled) {
    transform_space_action_->setEnabled(enabled);
  }
  const bool local =
      editor.viewports().transform_space() == QuaderTransformSpace::Local;
  if (transform_space_action_->isChecked() != local) {
    const QSignalBlocker blocker(transform_space_action_);
    transform_space_action_->setChecked(local);
  }
  QString tooltip = transform_space_action_->text();
  if (command.has_value() && !command->shortcut.empty()) {
    tooltip.append(QStringLiteral(" [%1]")
                       .arg(QString::fromStdString(command->shortcut)));
  }
  if (transform_space_action_->toolTip() != tooltip) {
    transform_space_action_->setToolTip(tooltip);
  }
  transform_presentation_stamp_ = stamp;
}

void QtSnappingToolbar::set_transform_space_toggle_requested_callback(
    std::function<void()> callback) {
  transform_space_toggle_requested_ = std::move(callback);
}

void QtSnappingToolbar::set_grid_preset_requested_callback(
    std::function<void(int)> callback) {
  grid_preset_requested_ = std::move(callback);
}

void QtSnappingToolbar::set_rotation_snap_degrees_requested_callback(
    std::function<void(double)> callback) {
  rotation_snap_degrees_requested_ = std::move(callback);
}

void QtSnappingToolbar::set_snapping_enabled_requested_callback(
    std::function<void(bool)> callback) {
  toggle_requested_[SnappingEnabled] = std::move(callback);
}

void QtSnappingToolbar::set_grid_snap_enabled_requested_callback(
    std::function<void(bool)> callback) {
  toggle_requested_[GridSnapping] = std::move(callback);
}

void QtSnappingToolbar::set_vertex_snap_enabled_requested_callback(
    std::function<void(bool)> callback) {
  toggle_requested_[VertexSnapping] = std::move(callback);
}

void QtSnappingToolbar::set_rotation_snap_enabled_requested_callback(
    std::function<void(bool)> callback) {
  toggle_requested_[RotationSnapping] = std::move(callback);
}

} // namespace quader
