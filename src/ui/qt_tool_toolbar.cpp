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
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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

/// Retains all tool sources while choosing the fixed modeling toolbar contents.
struct ToolIconSpec {
  ViewportToolId tool;
  const char *icon_path;
  bool toolbar_visible = true;
  bool separator_before = false;
};

constexpr std::array<ToolIconSpec, 19> kToolIconSpecs{{
    {ViewportToolId::CreatePrimitive, ":/quader/icons/tool_create_box.png"},
    {ViewportToolId::Pivot, ":/quader/icons/tool_pivot.png"},
    {ViewportToolId::Align, ":/quader/icons/tool_align.png"},
    {ViewportToolId::Extrude, ":/quader/icons/tool_extrude.png"},
    {ViewportToolId::InsertEdgeLoop,
     ":/quader/icons/tool_insert_edge_loop.png"},
    {ViewportToolId::Knife, ":/quader/icons/tool_knife.png"},
    {ViewportToolId::Cut, ":/quader/icons/tool_cut.png"},
    {ViewportToolId::Mirror, ":/quader/icons/tool_mirror.png"},
    {ViewportToolId::Poly, ":/quader/icons/tool_poly.png"},
    {ViewportToolId::LinearArray, ":/quader/icons/tool_linear_array.png", true,
     true},
    {ViewportToolId::RadialArray, ":/quader/icons/tool_radial_array.png"},
    {ViewportToolId::Curve, nullptr, true, true},
    {ViewportToolId::PathArray, ":/quader/icons/tool_path_array.png"},
    {ViewportToolId::UvPeel, ":/quader/icons/tool_uv_peel.png", true, true},
    {ViewportToolId::Select, ":/quader/icons/tool_select.png", false},
    {ViewportToolId::LassoSelect, ":/quader/icons/tool_lasso.png", false},
    {ViewportToolId::Move, ":/quader/icons/tool_move.png", false},
    {ViewportToolId::Rotate, ":/quader/icons/tool_rotate.png", false},
    {ViewportToolId::Scale, ":/quader/icons/tool_scale.png", false},
}};

/// Keeps menu ordering independent of the serialized primitive enum values.
struct PrimitiveIconSpec {
  QuaderPrimitiveCreationShape shape;
  QuaderCommandId command_id;
  const char *label;
  const char *icon_path;
};

constexpr std::array<PrimitiveIconSpec, 5> kPrimitiveIconSpecs{{
    {QuaderPrimitiveCreationShape::Box, quader_command::kToolsActivateCreateBox,
     "Create Box", ":/quader/icons/tool_create_box.png"},
    {QuaderPrimitiveCreationShape::Cylinder,
     quader_command::kToolsActivateCreateCylinder, "Create Cylinder",
     ":/quader/icons/tool_create_cylinder.png"},
    {QuaderPrimitiveCreationShape::Sphere,
     quader_command::kToolsActivateCreateSphere, "Create Sphere",
     ":/quader/icons/tool_create_sphere.png"},
    {QuaderPrimitiveCreationShape::Cone, quader_command::kToolsActivateCreateCone,
     "Create Cone", ":/quader/icons/tool_create_cone.png"},
    {QuaderPrimitiveCreationShape::Torus,
     quader_command::kToolsActivateCreateTorus, "Create Torus",
     ":/quader/icons/tool_create_torus.png"},
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
  bool separator_pending = false;
  for (std::size_t index = 0; index < kToolIconSpecs.size(); ++index) {
    const ToolIconSpec &spec = kToolIconSpecs[index];
    separator_pending = separator_pending || spec.separator_before;
    const std::string_view tool_id = viewport_tool_catalog_id(spec.tool);
    if (!release_allows_tool(tool_id)) {
      continue;
    }
    const std::string_view fallback = viewport_tool_label(spec.tool);
    const QIcon icon =
        spec.tool == ViewportToolId::Curve
            ? curve_tool_icon(palette())
            : spec.icon_path != nullptr
                ? qt_multiresolution_icon(QString::fromUtf8(spec.icon_path))
                : QIcon{};
    QAction *action = new QAction(icon, QString::fromUtf8(fallback.data()), this);
    if (spec.toolbar_visible) {
      if (separator_pending && !actions().empty()) {
        addSeparator();
      }
      separator_pending = false;
      addAction(action);
    }
    action->setObjectName(
        QStringLiteral("tool_%1").arg(QString::fromUtf8(tool_id.data())));
    action->setCheckable(true);
    action_group->addAction(action);
    if (spec.tool == ViewportToolId::CreatePrimitive) {
      primitive_action_ = action;
      action->setText(QStringLiteral("Create Box"));
      action->setToolTip(action->text());
      auto *menu = new QMenu(this);
      menu->setObjectName(QStringLiteral("primitiveToolMenu"));
      auto *primitive_group = new QActionGroup(menu);
      primitive_group->setExclusive(true);
      for (std::size_t primitive_index = 0;
           primitive_index < kPrimitiveIconSpecs.size(); ++primitive_index) {
        const PrimitiveIconSpec &primitive = kPrimitiveIconSpecs[primitive_index];
        if (!release_presents_command(primitive.command_id)) {
          continue;
        }
        QAction *choice = menu->addAction(
            qt_multiresolution_icon(QString::fromUtf8(primitive.icon_path)),
            QString::fromUtf8(primitive.label));
        choice->setObjectName(QStringLiteral("command_%1").arg(
            QString::fromUtf8(primitive.command_id.string().data())));
        choice->setCheckable(true);
        choice->setChecked(primitive.shape == QuaderPrimitiveCreationShape::Box);
        primitive_group->addAction(choice);
        primitive_actions_[primitive_index] = {
            .action = choice,
            .shape = primitive.shape,
            .command_id = primitive.command_id,
            .label = choice->text(),
        };
        QObject::connect(choice, &QAction::triggered, this,
                         [this, command_id = primitive.command_id]() {
                           if (command_requested_) {
                             command_requested_(command_id);
                           }
                         });
      }
      action->setMenu(menu);
      if (auto *button = qobject_cast<QToolButton *>(widgetForAction(action))) {
        button->setPopupMode(QToolButton::DelayedPopup);
      }
      QObject::connect(action, &QAction::triggered, this, [this]() {
        for (const PrimitiveAction &primitive : primitive_actions_) {
          if (primitive.action != nullptr &&
              primitive.shape == synchronized_primitive_shape_) {
            if (command_requested_) {
              command_requested_(primitive.command_id);
            }
            return;
          }
        }
      });
    } else {
      QObject::connect(action, &QAction::triggered, this,
                       [this, tool_id](bool checked) {
                         if (checked && tool_requested_) {
                           tool_requested_(tool_id);
                         }
                       });
    }
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
    if (entry.action->icon().isNull()) {
      if (auto *button = qobject_cast<QToolButton *>(widgetForAction(entry.action))) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
      }
    }
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
  }
}

QAction *QtToolToolbar::tool_action(ViewportToolId tool) const {
  for (const ToolAction &entry : tool_actions_) {
    if (entry.tool == tool && entry.action != nullptr) {
      return entry.action;
    }
  }
  return nullptr;
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
    if (entry.action == nullptr || entry.action == primitive_action_) {
      continue;
    }
    const QString text = localized_tool_text(
        localization, entry.tool_id, viewport_tool_label(entry.tool));
    entry.action->setText(text);
    entry.action->setToolTip(text);
  }
  for (PrimitiveAction &entry : primitive_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    std::string key = "keybindings.target.";
    key.append(entry.command_id.string());
    key.append(".label");
    const auto index = static_cast<std::size_t>(&entry - primitive_actions_.data());
    entry.label = QString::fromStdString(
        localization.text(key, kPrimitiveIconSpecs[index].label));
    entry.action->setText(entry.label);
    entry.action->setToolTip(entry.label);
    if (entry.shape == synchronized_primitive_shape_ &&
        primitive_action_ != nullptr) {
      primitive_action_->setText(entry.label);
      primitive_action_->setToolTip(entry.label);
    }
  }
  synchronization_valid_ = false;
}

void QtToolToolbar::synchronize(const QuaderEditorApi &editor,
                                const QuaderSelectionSummary &selection,
                                bool force) {
  const auto &viewports = editor.viewports();
  const auto pane = viewports.focused_pane();
  const auto selected_tool = viewports.selected_tool(pane);
  const auto control_revision = viewports.control_revision(pane);
  const auto runtime_tool_revision = editor.tools().active_tool_revision();
  const auto primitive_shape = editor.tools().preferred_primitive_shape();
  const auto keybindings_revision = editor.commands().keybindings_revision();
  const auto revisions = editor.revisions();
  const std::array<std::uint64_t, 8> availability_revisions{
      revisions.project_revision, revisions.scene_revision,
      revisions.scene_layer_revision, revisions.asset_revision,
      revisions.modeling_content_revision, selection.content_revision,
      selection.sub_selection_revision, editor.tools().availability_revision()};
  if (!force && synchronization_valid_ &&
      synchronized_editor_ == &editor &&
      synchronized_selected_tool_ == selected_tool &&
      synchronized_pane_ == pane &&
      synchronized_control_revision_ == control_revision &&
      synchronized_runtime_tool_revision_ == runtime_tool_revision &&
      synchronized_selection_mode_ == selection.mode &&
      synchronized_selection_revision_ == selection.selection_revision &&
      synchronized_primitive_shape_ == primitive_shape &&
      synchronized_keybindings_revision_ == keybindings_revision &&
      synchronized_availability_revisions_ == availability_revisions) {
    return;
  }
  synchronization_valid_ = true;
  synchronized_editor_ = &editor;
  synchronized_selected_tool_ = selected_tool;
  synchronized_pane_ = pane;
  synchronized_control_revision_ = control_revision;
  synchronized_runtime_tool_revision_ = runtime_tool_revision;
  synchronized_selection_mode_ = selection.mode;
  synchronized_selection_revision_ = selection.selection_revision;
  synchronized_primitive_shape_ = primitive_shape;
  synchronized_keybindings_revision_ = keybindings_revision;
  synchronized_availability_revisions_ = availability_revisions;

  for (const ToolAction &entry : tool_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    const bool checked = selected_tool == entry.tool;
    if (entry.action->isChecked() != checked) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(checked);
    }
    if (entry.action == primitive_action_) {
      continue;
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

  for (const PrimitiveAction &entry : primitive_actions_) {
    if (entry.action == nullptr) {
      continue;
    }
    const auto command = editor.commands().descriptor(entry.command_id);
    const bool enabled = command.has_value() && command->enabled;
    const bool preferred = entry.shape == primitive_shape;
    QString text = entry.label;
    QString tooltip = entry.label;
    if (command.has_value() && !command->shortcut.empty()) {
      const QString shortcut = QString::fromStdString(command->shortcut);
      // A tab gives QMenu its native shortcut column without installing a
      // second shortcut route alongside the canonical viewport dispatcher.
      text.append(QStringLiteral("\t%1").arg(shortcut));
      tooltip.append(QStringLiteral(" [%1]").arg(shortcut));
    }
    if (entry.action->text() != text) {
      entry.action->setText(text);
    }
    if (entry.action->toolTip() != tooltip) {
      entry.action->setToolTip(tooltip);
    }
    if (entry.action->isEnabled() != enabled) {
      entry.action->setEnabled(enabled);
    }
    if (entry.action->isChecked() != preferred) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(preferred);
    }
    if (preferred && primitive_action_ != nullptr) {
      if (primitive_action_->text() != entry.label) {
        primitive_action_->setText(entry.label);
      }
      if (primitive_action_->icon().cacheKey() != entry.action->icon().cacheKey()) {
        primitive_action_->setIcon(entry.action->icon());
      }
      if (primitive_action_->toolTip() != tooltip) {
        primitive_action_->setToolTip(tooltip);
      }
      if (primitive_action_->isEnabled() != enabled) {
        primitive_action_->setEnabled(enabled);
      }
    }
  }
}

void QtToolToolbar::set_tool_requested_callback(
    std::function<void(std::string_view)> callback) {
  tool_requested_ = std::move(callback);
}

void QtToolToolbar::set_command_requested_callback(
    std::function<void(QuaderCommandId)> callback) {
  command_requested_ = std::move(callback);
}

} // namespace quader
