////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QString>

namespace quader {

/** Repository glyph identities rendered as resolution-independent paths. */
enum class QtScalableGlyph {
  MaterialVisibility,
  MaterialVisibilityOff,
  MaterialLock,
  MaterialLockOpen,
  Close,
};

// The current authored toolbar and layer raster sources are 96 pixels. This
// bounded physical backing limit covers a 32-design-pixel icon at 300% UI scale
// through DPR 4 without allowing an accidental request to allocate an
// unbounded pixmap.
inline constexpr int kQtMultiresolutionIconMaximumPhysicalExtent = 384;

/**
 * Returns the lossless cache-key component used for one effective DPR.
 *
 * Keeping this conversion beside the icon engine prevents distinct fractional
 * DPRs that share a physical raster extent from aliasing in QPixmapCache.
 */
[[nodiscard]] QString
qt_multiresolution_icon_dpr_cache_identity(qreal device_pixel_ratio);

/**
 * Wraps Quader-owned raster artwork in a lazy, DPR-aware icon engine.
 *
 * Each requested logical size is rendered once at its physical size and then
 * retained by Qt's bounded pixmap cache. Quader's raster glyphs are authored
 * at three times their 32-design-pixel presentation size; enlargement keeps
 * their alpha fringe to approximately one physical pixel so a monitor DPR does
 * not turn the authored antialiasing into a soft multi-pixel halo.
 */
[[nodiscard]] QIcon qt_multiresolution_icon(const QString &source_path);
[[nodiscard]] QIcon qt_multiresolution_icon(const QString &source_path,
                                            const QColor &tint);

// Image overloads keep the engine directly testable without depending on an
// executable-owned Qt resource collection.
[[nodiscard]] QIcon qt_multiresolution_icon(const QImage &source);
[[nodiscard]] QIcon qt_multiresolution_icon(const QImage &source,
                                            const QColor &tint);

/** Renders the repository's monochrome glyphs as QPainter paths. */
[[nodiscard]] QIcon qt_scalable_glyph_icon(QtScalableGlyph glyph,
                                           const QColor &color);

} // namespace quader
