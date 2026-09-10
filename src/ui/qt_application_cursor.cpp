////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_application_cursor.hpp"

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QResource>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>

static void initialize_quader_selection_cursor_resources() {
  Q_INIT_RESOURCE(quader_selection_cursors);
}

namespace quader {
namespace {

constexpr auto kCursorFilterInstalledProperty =
    "quaderApplicationCursorFilterInstalled";
constexpr auto kManagedCursorKeyProperty = "quaderManagedDefaultCursorKey";

QString cursor_resource_path(QtApplicationCursorVariant variant) {
  switch (variant) {
  case QtApplicationCursorVariant::SelectionAdd:
    return QStringLiteral(":/quader/cursors/selection_add.png");
  case QtApplicationCursorVariant::SelectionSubtract:
    return QStringLiteral(":/quader/cursors/selection_subtract.png");
  case QtApplicationCursorVariant::Default:
    return QStringLiteral(":/quader/cursors/selection_default.png");
  }
  return QStringLiteral(":/quader/cursors/selection_default.png");
}

qreal normalized_device_pixel_ratio(qreal device_pixel_ratio) {
  return std::isfinite(device_pixel_ratio) && device_pixel_ratio > 0.0
             ? std::max<qreal>(1.0, device_pixel_ratio)
             : 1.0;
}

class ApplicationDefaultCursorFilter final : public QObject {
public:
  explicit ApplicationDefaultCursorFilter(QApplication &application)
      : QObject(&application) {}

  void synchronize(QWidget &widget) {
    if (!widget.isWindow()) {
      return;
    }
    const bool managed = has_managed_cursor(widget);
    if (widget.cursor().shape() == Qt::ArrowCursor || managed) {
      apply_default_cursor(widget);
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    auto *widget = qobject_cast<QWidget *>(watched);
    if (applying_ || widget == nullptr || !widget->isWindow()) {
      return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Polish:
    case QEvent::Show:
    case QEvent::DevicePixelRatioChange:
      synchronize(*widget);
      break;
    case QEvent::CursorChange:
      if (widget->cursor().shape() == Qt::ArrowCursor) {
        apply_default_cursor(*widget);
      } else if (!has_managed_cursor(*widget)) {
        widget->setProperty(kManagedCursorKeyProperty, QVariant{});
      }
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  bool has_managed_cursor(const QWidget &widget) const {
    const QVariant managed_key = widget.property(kManagedCursorKeyProperty);
    return managed_key.isValid() &&
           widget.cursor().shape() == Qt::BitmapCursor &&
           widget.cursor().pixmap().cacheKey() == managed_key.toLongLong();
  }

  void apply_default_cursor(QWidget &widget) {
    const QCursor cursor = qt_application_cursor(
        QtApplicationCursorVariant::Default,
        normalized_device_pixel_ratio(widget.devicePixelRatioF()));
    if (cursor.shape() != Qt::BitmapCursor) {
      return;
    }
    applying_ = true;
    widget.setCursor(cursor);
    widget.setProperty(kManagedCursorKeyProperty,
                       cursor.pixmap().cacheKey());
    applying_ = false;
  }

  bool applying_ = false;
};

} // namespace

QPixmap qt_application_cursor_pixmap(QtApplicationCursorVariant variant,
                                     qreal device_pixel_ratio) {
  initialize_quader_selection_cursor_resources();
  QPixmap source(cursor_resource_path(variant));
  if (source.isNull()) {
    return {};
  }

  const qreal scale = normalized_device_pixel_ratio(device_pixel_ratio);
  if (qFuzzyCompare(scale, 1.0)) {
    source.setDevicePixelRatio(1.0);
    return source;
  }

  const int physical_extent = static_cast<int>(std::lround(
      static_cast<double>(kQtApplicationCursorCanvasLogicalExtent) * scale));
  QPixmap rendered = source.scaled(
      QSize(physical_extent, physical_extent), Qt::IgnoreAspectRatio,
      Qt::SmoothTransformation);
  rendered.setDevicePixelRatio(scale);
  return rendered;
}

QCursor qt_application_cursor(QtApplicationCursorVariant variant,
                              qreal device_pixel_ratio) {
  const QPixmap pixmap =
      qt_application_cursor_pixmap(variant, device_pixel_ratio);
  return pixmap.isNull() ? QCursor(Qt::ArrowCursor) : QCursor(pixmap, 0, 0);
}

void install_qt_application_default_cursor(QApplication &application) {
  if (application.property(kCursorFilterInstalledProperty).toBool()) {
    return;
  }

  auto *filter = new ApplicationDefaultCursorFilter(application);
  application.installEventFilter(filter);
  application.setProperty(kCursorFilterInstalledProperty, true);
  for (QWidget *widget : application.topLevelWidgets()) {
    if (widget != nullptr) {
      filter->synchronize(*widget);
    }
  }
}

} // namespace quader
