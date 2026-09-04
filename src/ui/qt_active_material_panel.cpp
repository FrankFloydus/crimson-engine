////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_active_material_panel.hpp"

#include "ui/qt_ui_scale_metrics.hpp"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace quader {

QtActiveMaterialPanel::QtActiveMaterialPanel(QWidget *parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("activeMaterial.panel"));
  auto *layout = new QVBoxLayout(this);

  preview_ = new QLabel(this);
  preview_->setObjectName(QStringLiteral("activeMaterial.preview"));
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setFrameShape(QFrame::StyledPanel);
  preview_->setFrameShadow(QFrame::Sunken);
  preview_->setTextFormat(Qt::PlainText);
  preview_->setWordWrap(true);
  preview_->setScaledContents(false);
  preview_->setMinimumSize(96, 96);
  // The last scaled image must not become the dock's next minimum size.
  preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  layout->addWidget(preview_, 1);

  browse_button_ = new QPushButton(this);
  browse_button_->setObjectName(QStringLiteral("activeMaterial.browse"));
  browse_button_->setEnabled(false);
  layout->addWidget(browse_button_);
  setFocusProxy(browse_button_);
  QObject::connect(browse_button_, &QPushButton::clicked, this, [this]() {
    if (browse_available_ && browse_material_) {
      browse_material_();
    }
  });

  preview_->installEventFilter(this);
  refresh_labels();
}

QtActiveMaterialPanel::~QtActiveMaterialPanel() = default;

void QtActiveMaterialPanel::set_labels(
    const QtActiveMaterialPanelLabels &labels) {
  if (labels_ == labels) {
    return;
  }
  labels_ = labels;
  refresh_labels();
}

void QtActiveMaterialPanel::set_presentation(
    const std::optional<QtMaterialPresentationItem> &material,
    bool browse_available) {
  const bool material_changed = material_ != material;
  if (!material_changed && browse_available_ == browse_available) {
    return;
  }
  if (browse_available_ != browse_available) {
    browse_available_ = browse_available;
    browse_button_->setEnabled(browse_available_);
  }
  if (!material_changed) {
    return;
  }

  material_ = material;
  preview_thumbnail_ = material_ ? material_->thumbnail : nullptr;
  preview_dirty_ = true;
  refresh_material_text();
  refresh_preview();
}

void QtActiveMaterialPanel::set_browse_material_callback(
    BrowseMaterialCallback callback) {
  browse_material_ = std::move(callback);
}

void QtActiveMaterialPanel::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_revision_ == metrics.revision() &&
      qFuzzyCompare(ui_scale_factor_,
                    static_cast<qreal>(metrics.factor()))) {
    return;
  }
  ui_scale_revision_ = metrics.revision();
  ui_scale_factor_ = metrics.factor();
  scaled_size_hint_ = metrics.size(QSize(240, 260));
  preview_->setMinimumSize(metrics.size(QSize(96, 96)));
  preview_dirty_ = true;
  preview_bounds_ = {};
  preview_device_pixel_ratio_ = 0.0;
  updateGeometry();
  if (layout() != nullptr) {
    layout()->invalidate();
  }
  refresh_preview();
}

QSize QtActiveMaterialPanel::sizeHint() const { return scaled_size_hint_; }

bool QtActiveMaterialPanel::eventFilter(QObject *watched, QEvent *event) {
  if (watched == preview_ && event != nullptr &&
      (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
       event->type() == QEvent::DevicePixelRatioChange ||
       event->type() == QEvent::ScreenChangeInternal)) {
    refresh_preview();
  }
  return QWidget::eventFilter(watched, event);
}

void QtActiveMaterialPanel::refresh_labels() {
  if (accessibleName() != labels_.preview_title) {
    setAccessibleName(labels_.preview_title);
  }
  if (browse_button_->text() != labels_.browse) {
    browse_button_->setText(labels_.browse);
  }
  if (browse_button_->accessibleName() != labels_.browse) {
    browse_button_->setAccessibleName(labels_.browse);
  }
  refresh_material_text();
}

void QtActiveMaterialPanel::refresh_material_text() {
  const bool has_material = material_.has_value() && material_->valid();
  const QString text = has_material ? QString{} : labels_.empty;
  if (preview_->text() != text ||
      (!has_material && !preview_->pixmap().isNull())) {
    preview_->setText(text);
  }
  const QString accessible_name =
      labels_.preview_title + QStringLiteral(": ") +
      (has_material ? material_->name : labels_.empty);
  if (preview_->accessibleName() != accessible_name) {
    preview_->setAccessibleName(accessible_name);
  }
  QString tooltip;
  if (has_material) {
    tooltip = material_->name;
    if (!material_->project_relative_path.isEmpty()) {
      if (!tooltip.isEmpty()) {
        tooltip += QLatin1Char('\n');
      }
      tooltip += material_->project_relative_path;
    }
  }
  if (preview_->toolTip() != tooltip) {
    preview_->setToolTip(tooltip);
  }
}

void QtActiveMaterialPanel::refresh_preview() {
  if (!preview_->isVisible() || !material_.has_value() ||
      !material_->valid()) {
    return;
  }
  const QSize bounds = preview_->contentsRect().size();
  const qreal device_pixel_ratio =
      std::max<qreal>(1.0, preview_->devicePixelRatioF());
  if (!preview_dirty_ && preview_bounds_ == bounds &&
      preview_device_pixel_ratio_ == device_pixel_ratio) {
    return;
  }

  if (!preview_thumbnail_ || preview_thumbnail_->image.isNull()) {
    preview_thumbnail_ = make_qt_material_thumbnail(*material_);
  }
  QtMaterialPresentationItem preview_material = *material_;
  preview_material.thumbnail = preview_thumbnail_;
  preview_->setPixmap(qt_material_preview_pixmap(
      preview_material, bounds, device_pixel_ratio));
  preview_bounds_ = bounds;
  preview_device_pixel_ratio_ = device_pixel_ratio;
  preview_dirty_ = false;
}

} // namespace quader
