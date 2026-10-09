////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_display_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QIcon>
#include <QSignalBlocker>
#include <QSize>
#include <QStyle>
#include <QToolButton>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace quader {
namespace {

constexpr int kTopToolbarButtonHeight = 30;

/// Describes one stable viewport-display action and its localized resources.
struct DisplaySpec {
  QuaderCommandId command_id;
  const char *icon_path;
  const char *object_name;
  std::string_view localization_key;
  std::string_view fallback;
};

constexpr std::array<DisplaySpec, 6> kDisplaySpecs{{
    {quader_command::kViewportToggleWireframeOnShaded,
     ":/quader/icons/viewport_wireframe_on_shaded.png", "viewportWireframeOnShaded",
     "ui.toolbar.viewport_display.wireframe_on_shaded", "Show Wireframe"},
    {quader_command::kViewportToggleBackfaceCulling,
     ":/quader/icons/viewport_backface_culling.png", "viewportBackfaceCulling",
     "ui.toolbar.viewport_display.backface_culling", "Backface Culling"},
    {quader_command::kViewportToggleGrid,
     ":/quader/icons/viewport_grid.png", "viewportGrid",
     "ui.toolbar.viewport_display.grid", "Toggle Grid"},
    {quader_command::kViewportToggleGizmos,
     ":/quader/icons/viewport_gizmos.png", "viewportGizmos",
     "ui.toolbar.viewport_display.gizmos", "Toggle Gizmos"},
    {quader_command::kViewportToggleMeshGrid,
     ":/quader/icons/viewport_mesh_grid.png", "viewportMeshGrid",
     "ui.toolbar.viewport_display.mesh_grid", "Toggle Mesh Grid"},
    {quader_command::kViewportToggleOverlays,
     ":/quader/icons/viewport_overlays.png", "viewportOverlays",
     "ui.toolbar.viewport_display.overlays", "Toggle Overlays"},
}};

QString localized_text(const LocalizationService &localization,
                       std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

} // namespace

QtViewportDisplayToolbar::QtViewportDisplayToolbar(QWidget *parent)
    : QToolBar(parent) {
  setObjectName(QStringLiteral("viewportDisplayToolbar"));
  setWindowTitle(QStringLiteral("Viewport Display"));
  setAllowedAreas(Qt::AllToolBarAreas);
  setMovable(true);
  setFloatable(true);
  setIconSize(QSize(32, 32));
  setToolButtonStyle(Qt::ToolButtonIconOnly);

  for (std::size_t index = 0; index < kDisplaySpecs.size(); ++index) {
    const DisplaySpec &spec = kDisplaySpecs[index];
    if (!release_allows_command(spec.command_id.string())) {
      continue;
    }
    QAction *action = addAction(
        qt_multiresolution_icon(QString::fromUtf8(spec.icon_path)),
        QString::fromUtf8(spec.fallback.data(),
                          static_cast<qsizetype>(spec.fallback.size())));
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    QObject::connect(action, &QAction::triggered, this,
                     [this, command_id = spec.command_id](bool) {
                       presented_state_.reset();
                       if (command_requested_ != nullptr) {
                         command_requested_(command_id);
                       }
                     });
    display_actions_[index] = {
        .action = action,
        .command_id = spec.command_id,
        .localization_key = spec.localization_key,
        .fallback = spec.fallback,
    };
  }

  QStyle &button_presentation =
      install_native_toolbar_button_presentation(*this);
  for (const DisplayAction &entry : display_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
    if (auto *button =
            qobject_cast<QToolButton *>(widgetForAction(entry.action))) {
      button->setFixedHeight(kTopToolbarButtonHeight);
    }
  }
}

std::array<QAction *, 6> QtViewportDisplayToolbar::viewport_actions() const {
  return {display_actions_[0].action, display_actions_[1].action,
          display_actions_[2].action, display_actions_[3].action,
          display_actions_[4].action, display_actions_[5].action};
}

void QtViewportDisplayToolbar::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  constexpr QSize kToolbarIconSize(32, 32);
  const QSize scaled_icon_size = metrics.size(kToolbarIconSize);
  if (iconSize() != scaled_icon_size) {
    setIconSize(scaled_icon_size);
  }

  const int scaled_button_height = metrics.px(kTopToolbarButtonHeight);
  for (const DisplayAction &entry : display_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
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

void QtViewportDisplayToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(localized_text(localization,
                                "ui.toolbar.viewport_display.title",
                                "Viewport Display"));
  for (const DisplayAction &entry : display_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    const QString text = localized_text(localization, entry.localization_key,
                                        entry.fallback);
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
  presented_state_.reset();
}

void QtViewportDisplayToolbar::synchronize(const QuaderEditorApi &editor,
                                           bool force) {
  const auto &viewports = editor.viewports();
  const auto pane = viewports.focused_pane();
  const PresentedState state{
      .editor = &editor,
      .pane = pane,
      .control_revision = viewports.control_revision(pane),
      .keybindings_revision = editor.commands().keybindings_revision(),
      .toggles =
          {
              viewports.control(pane, QuaderViewportControl::WireframeOnShaded),
              editor.preferences().backface_culling_enabled(),
              viewports.control(pane, QuaderViewportControl::ShowGrid),
              viewports.control(pane, QuaderViewportControl::ShowGizmos),
              viewports.control(pane, QuaderViewportControl::ShowMeshGrid),
              viewports.control(pane, QuaderViewportControl::ShowOverlays),
          },
  };
  if (!force && presented_state_.has_value() &&
      *presented_state_ == state) {
    return;
  }

  for (std::size_t index = 0; index < display_actions_.size(); ++index) {
    const DisplayAction &entry = display_actions_[index];
    if (entry.action == nullptr) {
      continue;
    }
    if (entry.action->isChecked() != state.toggles[index]) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(state.toggles[index]);
    }

    const std::optional<QuaderCommandDescriptor> command =
        editor.commands().descriptor(entry.command_id.string());
    const bool enabled = command.has_value() && command->enabled;
    if (entry.action->isEnabled() != enabled) {
      entry.action->setEnabled(enabled);
    }
    QString tooltip = entry.action->text();
    const std::string shortcut =
        editor.commands().shortcut_for(entry.command_id);
    if (!shortcut.empty()) {
      tooltip.append(QStringLiteral(" [%1]")
                         .arg(QString::fromStdString(shortcut)));
    }
    if (entry.action->toolTip() != tooltip) {
      entry.action->setToolTip(tooltip);
    }
  }
  presented_state_ = state;
}

void QtViewportDisplayToolbar::set_command_requested_callback(
    std::function<void(QuaderCommandId)> callback) {
  command_requested_ = std::move(callback);
}

} // namespace quader
