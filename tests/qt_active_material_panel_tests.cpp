////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_active_material_panel.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace quader {
namespace {

QtMaterialPresentationItem active_material_item(
    AssetId id, QString name, QColor color,
    QSize image_size = QSize(128, 64)) {
  const QuaderMaterialReference reference =
      QuaderMaterialReference::embedded(id);
  QtMaterialPresentationItem item{
      .reference = reference,
      .stable_id = qt_material_reference_stable_id(reference),
      .name = std::move(name),
      .project_relative_path = QStringLiteral("Materials/test.qmat"),
      .fallback_color = QColor(200, 20, 20),
      .content_revision = 1,
  };
  auto thumbnail = std::make_shared<QtMaterialThumbnail>();
  thumbnail->image = QImage(image_size, QImage::Format_RGBA8888);
  thumbnail->image.fill(color);
  thumbnail->icon = QIcon(QPixmap::fromImage(thumbnail->image));
  item.thumbnail = std::move(thumbnail);
  return item;
}

/// Owns the Qt lifecycle and a hidden native window for retained-widget tests.
class QtActiveMaterialPanelTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    QApplication::setStyle(QStringLiteral("Fusion"));
    application_->setQuitOnLastWindowClosed(false);
    panel_ = std::make_unique<QtActiveMaterialPanel>();
    panel_->setAttribute(Qt::WA_DontShowOnScreen, true);
    preview_ = panel_->findChild<QLabel *>(
        QStringLiteral("activeMaterial.preview"));
    browse_ = panel_->findChild<QPushButton *>(
        QStringLiteral("activeMaterial.browse"));
    ASSERT_NE(preview_, nullptr);
    ASSERT_NE(browse_, nullptr);
    panel_->resize(320, 300);
    panel_->show();
    QCoreApplication::processEvents();
  }

  void TearDown() override {
    panel_.reset();
    application_.reset();
  }

  void expect_preview_fits(QSize source_size) const {
    const QPixmap pixmap = preview_->pixmap();
    ASSERT_FALSE(pixmap.isNull());
    const QSize bounds = preview_->contentsRect().size();
    const qreal device_pixel_ratio =
        std::max<qreal>(1.0, preview_->devicePixelRatioF());
    const QSize pixel_bounds(
        std::max(1, qRound(bounds.width() * device_pixel_ratio)),
        std::max(1, qRound(bounds.height() * device_pixel_ratio)));
    EXPECT_EQ(pixmap.devicePixelRatio(), device_pixel_ratio);
    EXPECT_EQ(pixmap.size(), source_size.scaled(pixel_bounds,
                                               Qt::KeepAspectRatio));
  }

  int argc_ = 1;
  char application_name_[36] = "quader-active-material-panel-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<QtActiveMaterialPanel> panel_;
  QLabel *preview_ = nullptr;
  QPushButton *browse_ = nullptr;
};

TEST_F(QtActiveMaterialPanelTests,
       ExpandsActivePreviewAndDispatchesBrowseWithStockControls) {
  const QtMaterialPresentationItem material = active_material_item(
      42, QStringLiteral("Blue stone"), QColor(40, 90, 180));
  panel_->set_presentation(material, true);
  QCoreApplication::processEvents();

  EXPECT_TRUE(preview_->text().isEmpty());
  EXPECT_EQ(preview_->accessibleName(),
            QStringLiteral("Active Material: Blue stone"));
  EXPECT_EQ(preview_->toolTip(),
            QStringLiteral("Blue stone\nMaterials/test.qmat"));
  EXPECT_EQ(preview_->pixmap().toImage().pixelColor(0, 0),
            QColor(40, 90, 180));
  EXPECT_EQ(preview_->geometry().left(), browse_->geometry().left());
  EXPECT_EQ(preview_->width(), browse_->width());
  EXPECT_GT(browse_->geometry().top(), preview_->geometry().bottom());
  EXPECT_GT(preview_->height(), browse_->height());
  expect_preview_fits(QSize(128, 64));

  EXPECT_EQ(browse_->text(), QStringLiteral("Browse"));
  EXPECT_TRUE(browse_->isEnabled());
  EXPECT_EQ(panel_->focusProxy(), browse_);
  QPushButton stock_button;
  EXPECT_EQ(browse_->focusPolicy(), stock_button.focusPolicy());
  int browse_count = 0;
  panel_->set_browse_material_callback([&browse_count]() { ++browse_count; });
  browse_->click();
  EXPECT_EQ(browse_count, 1);

  QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(browse_, &press);
  QCoreApplication::sendEvent(browse_, &release);
  EXPECT_EQ(browse_count, 2)
      << "stock keyboard activation must dispatch the same Browse intent";

  panel_->set_presentation(material, false);
  browse_->click();
  EXPECT_EQ(browse_count, 2);
}

TEST_F(QtActiveMaterialPanelTests,
       RefreshesActiveMaterialAndRetainsUnchangedPresentation) {
  const std::optional<QtMaterialPresentationItem> material =
      active_material_item(42, QStringLiteral("Blue stone"),
                           QColor(40, 90, 180));
  panel_->set_presentation(material, true);
  QCoreApplication::processEvents();
  const qint64 initial_cache_key = preview_->pixmap().cacheKey();
  for (int update = 0; update < 32; ++update) {
    panel_->set_presentation(material, true);
    panel_->set_labels(QtActiveMaterialPanelLabels{});
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(preview_->pixmap().cacheKey(), initial_cache_key)
      << "equal owner presentation must not rescale or replace the pixmap";

  panel_->set_presentation(material, false);
  panel_->set_presentation(material, true);
  EXPECT_EQ(preview_->pixmap().cacheKey(), initial_cache_key)
      << "Browse availability is independent from the displayed material";

  panel_->set_labels(QtActiveMaterialPanelLabels{
      .preview_title = QStringLiteral("Materiale attivo"),
      .empty = QStringLiteral("Nessun materiale attivo"),
      .browse = QStringLiteral("Sfoglia"),
  });
  QCoreApplication::processEvents();
  EXPECT_EQ(browse_->text(), QStringLiteral("Sfoglia"));
  EXPECT_EQ(browse_->accessibleName(), QStringLiteral("Sfoglia"));
  EXPECT_EQ(panel_->accessibleName(), QStringLiteral("Materiale attivo"));
  EXPECT_EQ(preview_->accessibleName(),
            QStringLiteral("Materiale attivo: Blue stone"));
  EXPECT_EQ(preview_->pixmap().cacheKey(), initial_cache_key)
      << "localization must not decode or scale an unchanged active image";

  QtMaterialPresentationItem updated = active_material_item(
      42, QStringLiteral("Green stone"), QColor(30, 150, 70));
  updated.content_revision = 2;
  panel_->set_presentation(updated, true);
  EXPECT_NE(preview_->pixmap().cacheKey(), initial_cache_key);
  EXPECT_EQ(preview_->pixmap().toImage().pixelColor(0, 0),
            QColor(30, 150, 70));
  EXPECT_EQ(preview_->accessibleName(),
            QStringLiteral("Materiale attivo: Green stone"));

  const QtMaterialPresentationItem next = active_material_item(
      84, QStringLiteral("Plaster"), QColor(225, 215, 195));
  panel_->set_presentation(next, true);
  EXPECT_EQ(preview_->pixmap().toImage().pixelColor(0, 0),
            QColor(225, 215, 195));
}

TEST_F(QtActiveMaterialPanelTests,
       ClearsInvalidMaterialAndUsesFallbackForMissingImage) {
  QtMaterialPresentationItem material = active_material_item(
      42, QStringLiteral("Missing texture"), QColor(40, 90, 180));
  material.thumbnail.reset();
  material.encoded_albedo = QByteArray("not an image");
  material.fallback_color = QColor(90, 120, 60);
  material.available = false;
  panel_->set_presentation(material, true);
  ASSERT_FALSE(preview_->pixmap().isNull());
  EXPECT_EQ(preview_->pixmap().toImage().pixelColor(0, 0),
            material.fallback_color);
  expect_preview_fits(QSize(96, 96));

  panel_->set_presentation(QtMaterialPresentationItem{}, false);
  EXPECT_TRUE(preview_->pixmap().isNull());
  EXPECT_EQ(preview_->text(), QStringLiteral("No active material"));
  EXPECT_TRUE(preview_->toolTip().isEmpty());
  EXPECT_FALSE(browse_->isEnabled());

  panel_->set_presentation(material, true);
  ASSERT_FALSE(preview_->pixmap().isNull());
  panel_->set_presentation(std::nullopt, false);
  EXPECT_TRUE(preview_->pixmap().isNull());
  EXPECT_EQ(preview_->accessibleName(),
            QStringLiteral("Active Material: No active material"));
  EXPECT_FALSE(browse_->isEnabled());

  panel_->set_labels(QtActiveMaterialPanelLabels{
      .preview_title = QStringLiteral("Materiale attivo"),
      .empty = QStringLiteral("Nessun materiale attivo"),
      .browse = QStringLiteral("Sfoglia"),
  });
  EXPECT_EQ(preview_->text(), QStringLiteral("Nessun materiale attivo"));
  panel_->set_presentation(std::nullopt, true);
  EXPECT_TRUE(browse_->isEnabled())
      << "the owner may allow browsing before an active material exists";
}

TEST_F(QtActiveMaterialPanelTests,
       PreservesAspectThroughResizeAndDefersHiddenImageWork) {
  const std::optional<QtMaterialPresentationItem> material =
      active_material_item(42, QStringLiteral("Wide stone"),
                           QColor(40, 90, 180), QSize(128, 32));
  panel_->set_presentation(material, true);
  for (const QSize size : {QSize(180, 420), QSize(520, 180),
                           QSize(260, 320)}) {
    panel_->resize(size);
    QCoreApplication::processEvents();
    expect_preview_fits(QSize(128, 32));
  }
  const qint64 visible_cache_key = preview_->pixmap().cacheKey();
  QEvent dpi_event(QEvent::DevicePixelRatioChange);
  QCoreApplication::sendEvent(preview_, &dpi_event);
  EXPECT_EQ(preview_->pixmap().cacheKey(), visible_cache_key)
      << "an unchanged effective DPI must retain the scaled image";

  panel_->hide();
  panel_->resize(400, 200);
  const QtMaterialPresentationItem next = active_material_item(
      84, QStringLiteral("Tall plaster"), QColor(220, 210, 190),
      QSize(32, 128));
  panel_->set_presentation(next, true);
  QCoreApplication::processEvents();
  EXPECT_EQ(preview_->pixmap().cacheKey(), visible_cache_key)
      << "hidden preview changes must defer scaling until the tile is shown";

  panel_->show();
  QCoreApplication::processEvents();
  expect_preview_fits(QSize(32, 128));
  EXPECT_EQ(preview_->pixmap().toImage().pixelColor(0, 0),
            QColor(220, 210, 190));
}

} // namespace
} // namespace quader
