////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_multiresolution_icon.hpp"

#include <QApplication>
#include <QCache>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

constexpr std::array<int, 8> kAdvertisedLogicalExtents{
    16, 24, 32, 48, 64, 96, 192,
    kQtMultiresolutionIconMaximumPhysicalExtent};
constexpr int kSourceIconCacheCapacity = 64;

qreal sanitized_device_pixel_ratio(qreal value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 1.0;
}

QSize bounded_physical_size(const QSize &logical_size,
                            qreal device_pixel_ratio) {
  if (!logical_size.isValid() || logical_size.isEmpty()) {
    return {};
  }

  device_pixel_ratio = sanitized_device_pixel_ratio(device_pixel_ratio);
  const auto physical_dimension = [device_pixel_ratio](int logical) {
    const double requested =
        static_cast<double>(std::max(1, logical)) * device_pixel_ratio;
    return static_cast<int>(std::clamp(
        std::ceil(requested), 1.0,
        static_cast<double>(std::numeric_limits<int>::max())));
  };
  QSize physical(physical_dimension(logical_size.width()),
                 physical_dimension(logical_size.height()));
  if (physical.width() > kQtMultiresolutionIconMaximumPhysicalExtent ||
      physical.height() > kQtMultiresolutionIconMaximumPhysicalExtent) {
    physical.scale(QSize(kQtMultiresolutionIconMaximumPhysicalExtent,
                         kQtMultiresolutionIconMaximumPhysicalExtent),
                   Qt::KeepAspectRatio);
  }
  return physical;
}

QImage tinted_source(QImage source, const QColor &tint) {
  if (source.isNull() || !tint.isValid()) {
    return source;
  }
  source = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&source);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(source.rect(), tint);
  return source;
}

QImage edge_preserving_scale(const QImage &source,
                             const QSize &physical_size) {
  if (source.isNull() || physical_size.isEmpty()) {
    return {};
  }
  if (source.size() == physical_size) {
    return source;
  }

  const double horizontal_ratio =
      static_cast<double>(physical_size.width()) /
      static_cast<double>(std::max(1, source.width()));
  const double vertical_ratio =
      static_cast<double>(physical_size.height()) /
      static_cast<double>(std::max(1, source.height()));
  const double enlargement = std::min(horizontal_ratio, vertical_ratio);

  QImage scaled =
      source.convertToFormat(QImage::Format_ARGB32)
          .scaled(physical_size, Qt::KeepAspectRatio,
                  Qt::SmoothTransformation);
  if (enlargement <= 1.0) {
    return scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }

  // Reconstruct opaque internal edges as well as the silhouette. Smooth
  // interpolation is still the stable color estimate, but its low-pass fringe
  // is reduced with a bounded, local high-pass term wherever neighboring
  // opaque samples contain real contrast. Gradients and transparent contours
  // keep the smooth estimate. Working non-premultiplied avoids colored halos
  // when the alpha coverage is adjusted below.
  QImage reconstructed = scaled.copy();
  const double edge_strength =
      std::clamp(enlargement - 0.5, 1.0, 3.0);
  const auto channel = [](QRgb pixel, int shift) {
    return static_cast<int>((pixel >> shift) & 0xffU);
  };
  for (int y = 1; y + 1 < scaled.height(); ++y) {
    const auto *above =
        reinterpret_cast<const QRgb *>(scaled.constScanLine(y - 1));
    const auto *current =
        reinterpret_cast<const QRgb *>(scaled.constScanLine(y));
    const auto *below =
        reinterpret_cast<const QRgb *>(scaled.constScanLine(y + 1));
    auto *output = reinterpret_cast<QRgb *>(reconstructed.scanLine(y));
    for (int x = 1; x + 1 < scaled.width(); ++x) {
      const QRgb center = current[x];
      const QRgb left = current[x - 1];
      const QRgb right = current[x + 1];
      const QRgb top = above[x];
      const QRgb bottom = below[x];
      if (std::min({qAlpha(center), qAlpha(left), qAlpha(right),
                    qAlpha(top), qAlpha(bottom)}) < 240) {
        continue;
      }

      std::array<int, 3> sharpened{};
      int local_contrast = 0;
      constexpr std::array<int, 3> kRgbShifts{16, 8, 0};
      for (std::size_t component = 0; component < kRgbShifts.size();
           ++component) {
        const int shift = kRgbShifts[component];
        const int center_value = channel(center, shift);
        const std::array<int, 4> neighbors{
            channel(left, shift), channel(right, shift),
            channel(top, shift), channel(bottom, shift)};
        for (const int neighbor : neighbors) {
          local_contrast =
              std::max(local_contrast, std::abs(center_value - neighbor));
        }
        const int average =
            (neighbors[0] + neighbors[1] + neighbors[2] + neighbors[3] +
             2) /
            4;
        sharpened[component] = static_cast<int>(std::clamp(
            std::lround(static_cast<double>(center_value) +
                        edge_strength *
                            static_cast<double>(center_value - average)),
            0L, 255L));
      }
      if (local_contrast >= 8) {
        output[x] = qRgba(sharpened[0], sharpened[1], sharpened[2],
                          qAlpha(center));
      }
    }
  }

  // Smooth interpolation otherwise stretches a one-source-pixel alpha fringe
  // across the complete enlargement ratio. Applying the inverse coverage
  // slope retains a roughly one-physical-pixel antialias transition without
  // introducing another application-scale factor.
  for (int y = 0; y < reconstructed.height(); ++y) {
    const auto *coverage =
        reinterpret_cast<const QRgb *>(scaled.constScanLine(y));
    auto *row = reinterpret_cast<QRgb *>(reconstructed.scanLine(y));
    for (int x = 0; x < scaled.width(); ++x) {
      const QRgb pixel = row[x];
      const int alpha = qAlpha(coverage[x]);
      if (alpha == 0 || alpha == 255) {
        continue;
      }
      const double sharpened =
          (static_cast<double>(alpha) - 127.5) * enlargement + 127.5;
      row[x] = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel),
                     static_cast<int>(std::clamp(
                         std::lround(sharpened), 0L, 255L)));
    }
  }
  return reconstructed.convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
}

class QtMultiresolutionIconEngine final : public QIconEngine {
public:
  explicit QtMultiresolutionIconEngine(QImage source)
      : source_(std::move(source)) {}

  QtMultiresolutionIconEngine *clone() const override {
    return new QtMultiresolutionIconEngine(source_);
  }

  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
             QIcon::State state) override {
    if (painter == nullptr || rect.isEmpty() || source_.isNull()) {
      return;
    }
    const QSize fitted = actualSize(rect.size(), mode, state);
    if (fitted.isEmpty()) {
      return;
    }
    const qreal device_pixel_ratio =
        painter->device() != nullptr
            ? painter->device()->devicePixelRatioF()
            : 1.0;
    const QPixmap raster =
        scaledPixmap(fitted, mode, state, device_pixel_ratio);
    if (raster.isNull()) {
      return;
    }
    QRect target(QPoint{}, fitted);
    target.moveCenter(rect.center());
    painter->drawPixmap(target, raster);
  }

  QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override {
    if (!size.isValid() || size.isEmpty() || source_.isNull()) {
      return {};
    }
    return source_.size().scaled(size, Qt::KeepAspectRatio);
  }

  QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                 QIcon::State state) override {
    return scaledPixmap(size, mode, state, 1.0);
  }

  QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode,
                       QIcon::State state, qreal scale) override {
    const QSize fitted_logical_size =
        source_.size().scaled(size, Qt::KeepAspectRatio);
    const QSize physical_size =
        bounded_physical_size(fitted_logical_size, scale);
    if (physical_size.isEmpty() || source_.isNull()) {
      return {};
    }

    const qreal requested_scale = sanitized_device_pixel_ratio(scale);
    const qreal width_scale =
        static_cast<qreal>(physical_size.width()) /
        static_cast<qreal>(std::max(1, fitted_logical_size.width()));
    const qreal height_scale =
        static_cast<qreal>(physical_size.height()) /
        static_cast<qreal>(std::max(1, fitted_logical_size.height()));
    const qreal resolved_scale =
        std::min({requested_scale, width_scale, height_scale});

    const qulonglong palette_key = QGuiApplication::instance() != nullptr
                                       ? QGuiApplication::palette().cacheKey()
                                       : 0U;
    const QString cache_key =
        QStringLiteral(
            "quader.multiresolution-icon.%1.%2x%3.%4x%5.%6.%7.%8.%9")
            .arg(static_cast<qulonglong>(source_.cacheKey()), 0, 16)
            .arg(physical_size.width())
            .arg(physical_size.height())
            .arg(fitted_logical_size.width())
            .arg(fitted_logical_size.height())
            .arg(qt_multiresolution_icon_dpr_cache_identity(
                resolved_scale))
            .arg(static_cast<int>(mode))
            .arg(static_cast<int>(state))
            .arg(palette_key, 0, 16);
    QPixmap raster;
    if (QPixmapCache::find(cache_key, &raster)) {
      return raster;
    }

    const QImage image = edge_preserving_scale(source_, physical_size);
    raster = QPixmap::fromImage(image);
    if (mode != QIcon::Normal && QApplication::style() != nullptr) {
      QStyleOption option;
      option.palette = QApplication::palette();
      raster = QApplication::style()->generatedIconPixmap(mode, raster,
                                                           &option);
    }

    raster.setDevicePixelRatio(resolved_scale);
    QPixmapCache::insert(cache_key, raster);
    return raster;
  }

  QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override {
    QList<QSize> sizes;
    sizes.reserve(static_cast<qsizetype>(kAdvertisedLogicalExtents.size()));
    for (const int extent : kAdvertisedLogicalExtents) {
      sizes.push_back(source_.size().scaled(QSize(extent, extent),
                                            Qt::KeepAspectRatio));
    }
    return sizes;
  }

  QString key() const override {
    return QStringLiteral("quader.multiresolution-raster");
  }

  bool isNull() override { return source_.isNull(); }

private:
  QImage source_;
};

QPainterPath material_visibility_path() {
  QPainterPath path;
  path.setFillRule(Qt::OddEvenFill);
  path.moveTo(12.0, 4.5);
  path.cubicTo(7.0, 4.5, 2.73, 7.61, 1.0, 12.0);
  path.cubicTo(2.73, 16.39, 7.0, 19.5, 12.0, 19.5);
  path.cubicTo(17.0, 19.5, 21.27, 16.39, 23.0, 12.0);
  path.cubicTo(21.27, 7.61, 17.0, 4.5, 12.0, 4.5);
  path.closeSubpath();
  path.addEllipse(QRectF(7.0, 7.0, 10.0, 10.0));
  path.addEllipse(QRectF(9.0, 9.0, 6.0, 6.0));
  return path;
}

QPainterPath material_visibility_off_path() {
  QPainterPath path;
  path.moveTo(12.0, 7.0);
  path.cubicTo(14.76, 7.0, 17.0, 9.24, 17.0, 12.0);
  path.cubicTo(17.0, 12.65, 16.87, 13.26, 16.64, 13.83);
  path.lineTo(19.56, 16.75);
  path.cubicTo(21.07, 15.49, 22.26, 13.86, 22.99, 12.0);
  path.cubicTo(21.26, 7.61, 17.0, 4.5, 12.0, 4.5);
  path.cubicTo(10.6, 4.5, 9.26, 4.75, 8.02, 5.2);
  path.lineTo(10.18, 7.36);
  path.cubicTo(10.74, 7.13, 11.35, 7.0, 12.0, 7.0);
  path.closeSubpath();

  path.moveTo(2.0, 4.27);
  path.lineTo(4.74, 7.01);
  path.cubicTo(3.08, 8.3, 1.78, 10.02, 1.0, 12.0);
  path.cubicTo(2.73, 16.39, 7.0, 19.5, 12.0, 19.5);
  path.cubicTo(13.55, 19.5, 15.03, 19.2, 16.38, 18.66);
  path.lineTo(19.73, 22.0);
  path.lineTo(21.0, 20.73);
  path.lineTo(3.27, 3.0);
  path.closeSubpath();

  path.moveTo(7.53, 9.8);
  path.lineTo(9.08, 11.35);
  path.cubicTo(9.03, 11.56, 9.0, 11.78, 9.0, 12.0);
  path.cubicTo(9.0, 13.66, 10.34, 15.0, 12.0, 15.0);
  path.cubicTo(12.22, 15.0, 12.44, 14.97, 12.65, 14.92);
  path.lineTo(14.2, 16.47);
  path.cubicTo(13.53, 16.8, 12.79, 17.0, 12.0, 17.0);
  path.cubicTo(9.24, 17.0, 7.0, 14.76, 7.0, 12.0);
  path.cubicTo(7.0, 11.21, 7.2, 10.47, 7.53, 9.8);
  path.closeSubpath();

  path.moveTo(11.84, 9.02);
  path.lineTo(14.99, 12.17);
  path.lineTo(15.01, 12.01);
  path.cubicTo(15.01, 10.35, 13.67, 9.01, 12.01, 9.01);
  path.closeSubpath();
  return path;
}

QPainterPath material_lock_path(bool open) {
  QPainterPath path;
  path.setFillRule(Qt::OddEvenFill);
  path.addRoundedRect(QRectF(4.0, 8.0, 16.0, 14.0), 2.0, 2.0);
  if (open) {
    path.addRect(QRectF(6.0, 10.0, 12.0, 10.0));
  } else {
    path.addEllipse(QRectF(10.0, 13.0, 4.0, 4.0));
  }

  QPainterPath shackle;
  shackle.setFillRule(Qt::OddEvenFill);
  shackle.moveTo(open ? 15.1 : 7.0, 8.0);
  shackle.lineTo(open ? 17.0 : 7.0, 8.0);
  shackle.lineTo(17.0, 6.0);
  shackle.cubicTo(17.0, 3.24, 14.76, 1.0, 12.0, 1.0);
  shackle.cubicTo(9.24, 1.0, 7.0, 3.24, 7.0, 6.0);
  if (open) {
    shackle.lineTo(8.9, 6.0);
  }
  shackle.cubicTo(8.9, 4.29, 10.29, 2.9, 12.0, 2.9);
  shackle.cubicTo(13.71, 2.9, 15.1, 4.29, 15.1, 6.0);
  shackle.closeSubpath();
  path.addPath(shackle);
  if (open) {
    path.addEllipse(QRectF(10.0, 13.0, 4.0, 4.0));
  }
  return path;
}

QPainterPath scalable_glyph_path(QtScalableGlyph glyph) {
  switch (glyph) {
  case QtScalableGlyph::MaterialVisibility:
    return material_visibility_path();
  case QtScalableGlyph::MaterialVisibilityOff:
    return material_visibility_off_path();
  case QtScalableGlyph::MaterialLock:
    return material_lock_path(false);
  case QtScalableGlyph::MaterialLockOpen:
    return material_lock_path(true);
  }
  return {};
}

class QtScalableGlyphIconEngine final : public QIconEngine {
public:
  QtScalableGlyphIconEngine(QtScalableGlyph glyph, QColor color)
      : glyph_(glyph), color_(std::move(color)) {}

  QtScalableGlyphIconEngine *clone() const override {
    return new QtScalableGlyphIconEngine(glyph_, color_);
  }

  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
             QIcon::State) override {
    if (painter == nullptr || rect.isEmpty() || !color_.isValid()) {
      return;
    }
    QColor presentation_color = color_;
    if (mode == QIcon::Disabled) {
      presentation_color.setAlphaF(presentation_color.alphaF() * 0.45);
    }
    const int extent = std::min(rect.width(), rect.height());
    const QRectF square(rect.center().x() - extent * 0.5,
                        rect.center().y() - extent * 0.5, extent, extent);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(square.topLeft());
    painter->scale(square.width() / 24.0, square.height() / 24.0);
    painter->fillPath(scalable_glyph_path(glyph_), presentation_color);
    painter->restore();
  }

  QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override {
    const int extent = std::min(size.width(), size.height());
    return extent > 0 ? QSize(extent, extent) : QSize{};
  }

  QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                 QIcon::State state) override {
    return scaledPixmap(size, mode, state, 1.0);
  }

  QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode,
                       QIcon::State state, qreal scale) override {
    const QSize logical_size = actualSize(size, mode, state);
    const QSize physical_size = bounded_physical_size(logical_size, scale);
    if (logical_size.isEmpty() || physical_size.isEmpty()) {
      return {};
    }
    const qreal requested_scale = sanitized_device_pixel_ratio(scale);
    const qreal width_scale =
        static_cast<qreal>(physical_size.width()) /
        static_cast<qreal>(logical_size.width());
    const qreal height_scale =
        static_cast<qreal>(physical_size.height()) /
        static_cast<qreal>(logical_size.height());
    const qreal resolved_scale =
        std::min({requested_scale, width_scale, height_scale});
    const QString cache_key =
        QStringLiteral("quader.scalable-glyph.%1.%2.%3x%4.%5.%6.%7")
            .arg(static_cast<int>(glyph_))
            .arg(static_cast<qulonglong>(color_.rgba()), 0, 16)
            .arg(physical_size.width())
            .arg(physical_size.height())
            .arg(QString::number(
                resolved_scale, 'g',
                std::numeric_limits<qreal>::max_digits10))
            .arg(static_cast<int>(mode))
            .arg(static_cast<int>(state));
    QPixmap raster;
    if (QPixmapCache::find(cache_key, &raster)) {
      return raster;
    }
    raster = QPixmap(physical_size);
    raster.fill(Qt::transparent);
    raster.setDevicePixelRatio(resolved_scale);
    QPainter painter(&raster);
    paint(&painter, QRect(QPoint{}, logical_size), mode, state);
    painter.end();
    QPixmapCache::insert(cache_key, raster);
    return raster;
  }

  QString key() const override {
    return QStringLiteral("quader.scalable-glyph");
  }

  bool isNull() override { return !color_.isValid(); }

private:
  QtScalableGlyph glyph_;
  QColor color_;
};

QIcon icon_from_image(QImage source, const QColor &tint) {
  source = tinted_source(std::move(source), tint);
  if (source.isNull()) {
    return {};
  }
  return QIcon(new QtMultiresolutionIconEngine(std::move(source)));
}

QCache<QString, QIcon> &source_icon_cache() {
  static QCache<QString, QIcon> cache(kSourceIconCacheCapacity);
  return cache;
}

QIcon icon_from_path(const QString &source_path, const QColor &tint) {
  const QString cache_key =
      tint.isValid()
          ? source_path + QStringLiteral("#tint=") +
                QString::number(static_cast<qulonglong>(tint.rgba()), 16)
          : source_path + QStringLiteral("#untinted");
  QCache<QString, QIcon> &cache = source_icon_cache();
  if (const QIcon *cached = cache.object(cache_key)) {
    return *cached;
  }

  QImage source(source_path);
  if (source.isNull()) {
    // Preserve QIcon's native fallback behavior for a missing or unsupported
    // resource, and do not retain failure in case resources register later.
    return QIcon(source_path);
  }
  QIcon icon = icon_from_image(std::move(source), tint);
  cache.insert(cache_key, new QIcon(icon));
  return icon;
}

} // namespace

QString
qt_multiresolution_icon_dpr_cache_identity(qreal device_pixel_ratio) {
  return QString::number(
      sanitized_device_pixel_ratio(device_pixel_ratio), 'g',
      std::numeric_limits<qreal>::max_digits10);
}

QIcon qt_multiresolution_icon(const QString &source_path) {
  return icon_from_path(source_path, {});
}

QIcon qt_multiresolution_icon(const QString &source_path,
                              const QColor &tint) {
  return icon_from_path(source_path, tint);
}

QIcon qt_multiresolution_icon(const QImage &source) {
  return icon_from_image(source, {});
}

QIcon qt_multiresolution_icon(const QImage &source, const QColor &tint) {
  return icon_from_image(source, tint);
}

QIcon qt_scalable_glyph_icon(QtScalableGlyph glyph,
                             const QColor &color) {
  return color.isValid()
             ? QIcon(new QtScalableGlyphIconEngine(glyph, color))
             : QIcon{};
}

} // namespace quader
