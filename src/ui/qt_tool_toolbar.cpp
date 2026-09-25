////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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

constexpr std::array<ToolIconSpec, 18> kToolIconSpecs{{
    {ViewportToolId::Select, ":/quader/icons/tool_select.png"},
    {ViewportToolId::LassoSelect, ":/quader/icons/tool_lasso.png"},
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
    {ViewportToolId::UvPeel, ":/quader/icons/tool_uv_peel.png"},
    {ViewportToolId::LinearArray, ":/quader/icons/tool_linear_array.png"},
    {ViewportToolId::RadialArray, ":/quader/icons/tool_radial_array.png"},
    {ViewportToolId::PathArray, ":/quader/icons/tool_path_array.png"},
    {ViewportToolId::Curve, nullptr},
}};

// Keeps the small curve glyph scalable and palette-aware without a bitmap
// asset. Multiple generated resolutions let QIcon select a sharp source for
// the scaled logical size and the destination screen's device pixel ratio.
QPixmap curve_tool_icon_pixmap(const QPalette &palette, int extent) {
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  constexpr qreal kDesignExtent = 64.0;
  const qreal drawing_scale = static_cast<qreal>(extent) / kDesignExtent;
  painter.scale(drawing_scale, drawing_scale);
  painter.setPen(QPen(palette.color(QPalette::PlaceholderText), 2.0));
  painter.drawLine(QPointF(10, 48), QPointF(16, 12));
  painter.drawLine(QPointF(48, 52), QPointF(54, 16));
  QPainterPath path(QPointF(10, 48));
  path.cubicTo(16, 12, 48, 52, 54, 16);
  painter.setPen(QPen(palette.color(QPalette::ButtonText), 3.5));
  painter.drawPath(path);
  painter.setBrush(palette.color(QPalette::Button));
  painter.drawRect(QRectF(6, 44, 8, 8));
  painter.drawRect(QRectF(50, 12, 8, 8));
  painter.setBrush(palette.color(QPalette::Highlight));
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPointF(16, 12), 3.5, 3.5);
  painter.drawEllipse(QPointF(48, 52), 3.5, 3.5);
  return pixmap;
}

QIcon curve_tool_icon(const QPalette &palette) {
  constexpr std::array<int, 9> kGeneratedExtents{
      16, 24, 32, 48, 64, 96, 128, 192, 384,
  };
  QIcon icon;
  for (const int extent : kGeneratedExtents) {
    icon.addPixmap(curve_tool_icon_pixmap(palette, extent));
  }
  return icon;
}

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
    const std::string_view tool_id = viewport_tool_catalog_id(spec.tool);
    if (!release_allows_tool(tool_id)) {
      continue;
    }
    if (spec.separator_before) {
      addSeparator();
    }
    const std::string_view fallback = viewport_tool_label(spec.tool);
    const QIcon icon =
        spec.tool == ViewportToolId::Curve
            ? curve_tool_icon(palette())
            : qt_multiresolution_icon(QString::fromUtf8(spec.icon_path));
    QAction *action = addAction(icon,
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
    if (spec.tool == ViewportToolId::Curve) {
      curve_action_ = action;
    }
  }
  QStyle &button_presentation =
      install_native_toolbar_button_presentation(*this);
  for (const ToolAction &entry : tool_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
  }
}

bool QtToolToolbar::event(QEvent *event) {
  const bool handled = QToolBar::event(event);
  if (event != nullptr &&
      (event->type() == QEvent::PaletteChange ||
       event->type() == QEvent::ApplicationPaletteChange)) {
    refresh_curve_tool_icon();
  }
  return handled;
}

void QtToolToolbar::refresh_curve_tool_icon() {
  if (curve_action_ != nullptr) {
    curve_action_->setIcon(curve_tool_icon(palette()));
  }
}

void QtToolToolbar::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  constexpr QSize kToolbarIconSize(32, 32);
  const QSize scaled_icon_size = metrics.size(kToolbarIconSize);
  if (iconSize() != scaled_icon_size) {
    setIconSize(scaled_icon_size);
  }

  for (const ToolAction &entry : tool_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    apply_native_toolbar_button_face_ui_scale(*this, *entry.action, metrics);
  }
}

void QtToolToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(QString::fromStdString(
      localization.text("ui.toolbar.tools.title", "Tools")));
  for (const ToolAction &entry : tool_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
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
    if (entry.action == nullptr) {
      continue;
    }
    const bool checked = active_tool == entry.tool_id;
    if (entry.action->isChecked() != checked) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(checked);
    }
    const std::string command_id =
        editor.commands().command_id_for_tool_activation(entry.tool_id);
    const std::optional<QuaderCommandDescriptor> command =
        editor.commands().descriptor(command_id);
    const bool enabled = command.has_value() && command->enabled;
    if (entry.action->isEnabled() != enabled) {
      entry.action->setEnabled(enabled);
    }
    QString tooltip = entry.action->text();
    const std::string shortcut =
        command.has_value() ? command->shortcut : std::string{};
    if (!shortcut.empty()) {
      tooltip.append(QStringLiteral(" [%1]")
                         .arg(QString::fromStdString(shortcut)));
    }
    if (entry.action->toolTip() != tooltip) {
      entry.action->setToolTip(tooltip);
    }
  }
}

void QtToolToolbar::set_tool_requested_callback(
    std::function<void(std::string_view)> callback) {
  tool_requested_ = std::move(callback);
}

} // namespace quader
