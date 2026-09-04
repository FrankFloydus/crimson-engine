////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_presentation.hpp"

#include <QBuffer>
#include <QImageReader>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace quader {
namespace {

bool has_truevision_tga_footer(const QByteArray &encoded) {
  static const QByteArray kSignature("TRUEVISION-XFILE.\0", 18);
  return encoded.endsWith(kSignature);
}

// One decoded preview can be retained by a QIcon. Bound that raster even when
// a malformed caller supplies an extreme logical size or device scale.
constexpr int kMaximumPreviewRasterDimension = 4096;

QSize bounded_preview_size(QSize requested) {
  requested.setWidth(std::max(1, requested.width()));
  requested.setHeight(std::max(1, requested.height()));
  if (requested.width() > kMaximumPreviewRasterDimension ||
      requested.height() > kMaximumPreviewRasterDimension) {
    requested.scale(QSize(kMaximumPreviewRasterDimension,
                          kMaximumPreviewRasterDimension),
                    Qt::KeepAspectRatio);
  }
  return requested;
}

QImage decode_encoded_preview(const QByteArray &encoded, QSize bounds,
                              bool force_tga) {
  if (encoded.isEmpty()) {
    return {};
  }

  QBuffer buffer;
  buffer.setData(encoded);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return {};
  }

  QImageReader reader(&buffer);
  if (force_tga) {
    reader.setFormat("TGA");
  }
  const QSize source_size = reader.size();
  if (source_size.isValid()) {
    const QSize decoded_size = source_size.scaled(
        bounded_preview_size(bounds), Qt::KeepAspectRatio);
    if (decoded_size.isValid() && decoded_size != source_size) {
      reader.setScaledSize(decoded_size);
    }
  }

  QImage image = reader.read();
  if (!image.isNull() &&
      (image.width() > bounds.width() || image.height() > bounds.height())) {
    image = image.scaled(bounded_preview_size(bounds), Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
  }
  return image;
}

bool retained_preview_satisfies(const QImage &image, QSize bounds) {
  if (image.isNull()) {
    return false;
  }
  bounds = bounded_preview_size(bounds);
  const QSize required = image.size().scaled(bounds, Qt::KeepAspectRatio);
  return image.width() >= required.width() &&
         image.height() >= required.height();
}

QImage decode_material_preview(const QtMaterialPresentationItem &item,
                               QSize requested_bounds) {
  requested_bounds = bounded_preview_size(requested_bounds);
  if (item.thumbnail &&
      retained_preview_satisfies(item.thumbnail->image,
                                 requested_bounds)) {
    return item.thumbnail->image;
  }

  QImage image;
  if (!item.encoded_albedo.isEmpty()) {
    const bool has_tga_footer =
        has_truevision_tga_footer(item.encoded_albedo);
    image = decode_encoded_preview(item.encoded_albedo, requested_bounds,
                                   has_tga_footer);
    if (image.isNull()) {
      // A true-color TGA header can resemble a Windows cursor header, causing
      // Qt's generic content probe to select CUR before TGA.
      image = decode_encoded_preview(item.encoded_albedo, requested_bounds,
                                     true);
    }
  }
  if (image.isNull() && item.thumbnail &&
      !item.thumbnail->image.isNull()) {
    image = item.thumbnail->image;
  }
  if (image.isNull()) {
    const QSize fallback_size = requested_bounds.boundedTo(QSize(96, 96));
    image = QImage(fallback_size, QImage::Format_ARGB32_Premultiplied);
    image.fill(item.fallback_color);
  }
  return image;
}

QSize physical_preview_bounds(QSize logical_bounds,
                              qreal &device_pixel_ratio) {
  logical_bounds.setWidth(std::max(1, logical_bounds.width()));
  logical_bounds.setHeight(std::max(1, logical_bounds.height()));
  if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio < 1.0) {
    device_pixel_ratio = 1.0;
  }
  const qreal largest_logical_dimension = static_cast<qreal>(
      std::max(logical_bounds.width(), logical_bounds.height()));
  device_pixel_ratio = std::min(
      device_pixel_ratio,
      static_cast<qreal>(kMaximumPreviewRasterDimension) /
          largest_logical_dimension);
  device_pixel_ratio = std::max<qreal>(device_pixel_ratio, 1.0);
  const auto scaled_dimension = [device_pixel_ratio](int logical) {
    const qreal scaled = static_cast<qreal>(logical) * device_pixel_ratio;
    return static_cast<int>(std::clamp<qreal>(
        std::round(scaled), 1.0,
        static_cast<qreal>(kMaximumPreviewRasterDimension)));
  };
  return bounded_preview_size(
      QSize(scaled_dimension(logical_bounds.width()),
            scaled_dimension(logical_bounds.height())));
}

} // namespace

QString qt_material_reference_stable_id(
    const QuaderMaterialReference &reference) {
  if (!reference.valid()) {
    return {};
  }
  if (reference.project_owned()) {
    const std::string_view value = reference.project_asset.str();
    return QStringLiteral("project:") +
           QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
  }
  return QStringLiteral("embedded:") +
         QString::number(static_cast<qulonglong>(reference.embedded_asset));
}

std::shared_ptr<const QtMaterialThumbnail>
make_qt_material_thumbnail(const QtMaterialPresentationItem &item,
                           int maximum_dimension) {
  auto thumbnail = std::make_shared<QtMaterialThumbnail>();
  maximum_dimension =
      std::clamp(maximum_dimension, 1, kMaximumPreviewRasterDimension);
  const QSize bounds(maximum_dimension, maximum_dimension);
  thumbnail->image = decode_material_preview(item, bounds).scaled(
      QSize(maximum_dimension, maximum_dimension), Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
  thumbnail->icon = QIcon(QPixmap::fromImage(thumbnail->image));
  return thumbnail;
}

QImage qt_material_preview_image(const QtMaterialPresentationItem &item,
                                 int maximum_dimension) {
  maximum_dimension =
      std::clamp(maximum_dimension, 1, kMaximumPreviewRasterDimension);
  const QSize bounds(maximum_dimension, maximum_dimension);
  return decode_material_preview(item, bounds).scaled(
      QSize(maximum_dimension, maximum_dimension), Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
}

QPixmap qt_material_preview_pixmap(const QtMaterialPresentationItem &item,
                                   QSize logical_bounds,
                                   qreal device_pixel_ratio) {
  if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio < 1.0) {
    device_pixel_ratio = 1.0;
  }
  const QSize pixel_bounds =
      physical_preview_bounds(logical_bounds, device_pixel_ratio);
  QImage image = decode_material_preview(item, pixel_bounds).scaled(
      pixel_bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap pixmap = QPixmap::fromImage(std::move(image));
  pixmap.setDevicePixelRatio(device_pixel_ratio);
  return pixmap;
}

} // namespace quader
