////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_presentation.hpp"

#include <QSize>

#include <algorithm>
#include <string_view>

namespace quader {
namespace {

bool has_truevision_tga_footer(const QByteArray &encoded) {
  static const QByteArray kSignature("TRUEVISION-XFILE.\0", 18);
  return encoded.endsWith(kSignature);
}

QImage decode_material_preview(const QtMaterialPresentationItem &item) {
  if (item.thumbnail && !item.thumbnail->image.isNull()) {
    return item.thumbnail->image;
  }

  QImage image;
  if (!item.encoded_albedo.isEmpty()) {
    const bool has_tga_footer =
        has_truevision_tga_footer(item.encoded_albedo);
    if (has_tga_footer) {
      (void)image.loadFromData(item.encoded_albedo, "TGA");
    } else {
      (void)image.loadFromData(item.encoded_albedo);
    }
    if (image.isNull()) {
      // A true-color TGA header can resemble a Windows cursor header, causing
      // Qt's generic content probe to select CUR before TGA.
      (void)image.loadFromData(item.encoded_albedo, "TGA");
    }
  }
  if (image.isNull()) {
    image = QImage(96, 96, QImage::Format_ARGB32_Premultiplied);
    image.fill(item.fallback_color);
  }
  return image;
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
  maximum_dimension = std::max(1, maximum_dimension);
  thumbnail->image = decode_material_preview(item).scaled(
      QSize(maximum_dimension, maximum_dimension), Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
  thumbnail->icon = QIcon(QPixmap::fromImage(thumbnail->image));
  return thumbnail;
}

QImage qt_material_preview_image(const QtMaterialPresentationItem &item,
                                 int maximum_dimension) {
  maximum_dimension = std::max(1, maximum_dimension);
  return decode_material_preview(item).scaled(
      QSize(maximum_dimension, maximum_dimension), Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
}

QPixmap qt_material_preview_pixmap(const QtMaterialPresentationItem &item,
                                   QSize logical_bounds,
                                   qreal device_pixel_ratio) {
  logical_bounds.setWidth(std::max(1, logical_bounds.width()));
  logical_bounds.setHeight(std::max(1, logical_bounds.height()));
  device_pixel_ratio = std::max<qreal>(1.0, device_pixel_ratio);
  const QSize pixel_bounds(
      std::max(1, qRound(logical_bounds.width() * device_pixel_ratio)),
      std::max(1, qRound(logical_bounds.height() * device_pixel_ratio)));
  QImage image = decode_material_preview(item).scaled(
      pixel_bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap pixmap = QPixmap::fromImage(std::move(image));
  pixmap.setDevicePixelRatio(device_pixel_ratio);
  return pixmap;
}

} // namespace quader
