////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_materials_api.hpp"

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>

#include <cstdint>
#include <memory>

namespace quader {

/// Collision-proof UI key for proxy-model selection and retained widget state.
/// It is presentation-only; semantic callbacks always return the typed value.
[[nodiscard]] QString qt_material_reference_stable_id(
    const QuaderMaterialReference &reference);

/// Baseline presentation data cached once by UISystem and implicitly shared by
/// retained consumers. Larger high-DPI rasters may be regenerated from the
/// item's encoded source instead of enlarging this bounded thumbnail.
struct QtMaterialThumbnail final {
  QImage image;
  QIcon icon;
};

/// Immutable, owner-supplied material presentation shared by retained Qt
/// material surfaces. `stable_id` is an opaque public material-reference key;
/// widgets must return it unchanged and never interpret owner identity.
struct QtMaterialPresentationItem final {
  QuaderMaterialReference reference;
  QString stable_id;
  QString name;
  QString project_relative_path;
  QString type_display_name;
  qint64 qmat_size_bytes = 0;
  QByteArray encoded_albedo;
  QColor fallback_color = Qt::white;
  std::uint64_t content_revision = 0;
  bool available = true;
  bool editable = false;
  std::shared_ptr<const QtMaterialThumbnail> thumbnail;

  [[nodiscard]] bool valid() const noexcept {
    return reference.valid() && !stable_id.isEmpty();
  }
  [[nodiscard]] bool replacement_valid() const noexcept {
    return valid() && available;
  }

  bool operator==(const QtMaterialPresentationItem &) const = default;
};

/// Builds the decoded cache entry that UISystem shares across the Inspector and
/// browser variants. Call it only from the project/material event gate.
[[nodiscard]] std::shared_ptr<const QtMaterialThumbnail>
make_qt_material_thumbnail(const QtMaterialPresentationItem &item,
                           int maximum_dimension = 512);

/// Decodes a presentation thumbnail at most to `maximum_dimension`. When the
/// retained thumbnail is smaller than the request, the encoded source is read
/// again at the requested resolution instead of enlarging the cached raster.
[[nodiscard]] QImage
qt_material_preview_image(const QtMaterialPresentationItem &item,
                          int maximum_dimension = 512);

/// Produces an aspect-preserving pixmap centered by the receiving widget.
/// `device_pixel_ratio` allows crisp fixed-size tiles on scaled displays.
[[nodiscard]] QPixmap
qt_material_preview_pixmap(const QtMaterialPresentationItem &item,
                           QSize logical_bounds,
                           qreal device_pixel_ratio = 1.0);

} // namespace quader
