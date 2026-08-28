////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"

#include <QSize>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QPushButton;

namespace quader {

/// Localized presentation supplied by the native shell for the active tile.
struct QtActiveMaterialPanelLabels final {
  QString preview_title = QStringLiteral("Active Material");
  QString empty = QStringLiteral("No active material");
  QString browse = QStringLiteral("Browse");

  bool operator==(const QtActiveMaterialPanelLabels &) const = default;
};

/**
 * Retained active-material tile and browser action. The shell supplies immutable
 * material presentation; this widget owns only sizing, accessibility, and the
 * pixmap derived from the shared thumbnail.
 */
class QtActiveMaterialPanel final : public QWidget {
public:
  using BrowseMaterialCallback = std::function<void()>;

  explicit QtActiveMaterialPanel(QWidget *parent = nullptr);
  ~QtActiveMaterialPanel() override;

  QtActiveMaterialPanel(const QtActiveMaterialPanel &) = delete;
  QtActiveMaterialPanel &operator=(const QtActiveMaterialPanel &) = delete;

  void set_labels(const QtActiveMaterialPanelLabels &labels);
  void set_presentation(
      const std::optional<QtMaterialPresentationItem> &material,
      bool browse_available);
  void set_browse_material_callback(BrowseMaterialCallback callback);

  [[nodiscard]] QSize sizeHint() const override;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void refresh_labels();
  void refresh_material_text();
  void refresh_preview();

  QLabel *preview_ = nullptr;
  QPushButton *browse_button_ = nullptr;
  QtActiveMaterialPanelLabels labels_;
  std::optional<QtMaterialPresentationItem> material_;
  std::shared_ptr<const QtMaterialThumbnail> preview_thumbnail_;
  BrowseMaterialCallback browse_material_;
  QSize preview_bounds_;
  qreal preview_device_pixel_ratio_ = 0.0;
  bool preview_dirty_ = true;
  bool browse_available_ = false;
};

} // namespace quader
