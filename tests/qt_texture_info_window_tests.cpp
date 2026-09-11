////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_texture_info_window.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QHeaderView>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {
namespace {

class ScopedDefaultLocale final {
public:
  explicit ScopedDefaultLocale(const QLocale &locale) : previous_(QLocale()) {
    QLocale::setDefault(locale);
  }

  ~ScopedDefaultLocale() { QLocale::setDefault(previous_); }

private:
  QLocale previous_;
};

TextureInfoPresentation texture_presentation(
    const std::vector<std::byte> &pixels) {
  return TextureInfoPresentation{
      .context =
          TextureInfoContext{
              .project_identity = "texture-info-project",
              .activation_generation = 4u,
              .dependency_revision = 11u,
              .material_identity = "material-id",
              .texture_identity = "texture-id",
          },
      .name = "wall_albedo.png",
      .location = "C:\\Project\\Textures\\wall_albedo.png",
      .format = "pNg",
      .width = 1u,
      .height = 1u,
      .encoded_byte_size = 1'250'000u,
      .source_has_alpha = true,
      .rgba8 = pixels,
      .content_revision = 7u,
  };
}

QColor rendered_canvas_center(QWidget &canvas) {
  QImage rendered(canvas.size(), QImage::Format_ARGB32_Premultiplied);
  rendered.fill(Qt::transparent);
  QPainter painter(&rendered);
  canvas.render(&painter);
  painter.end();
  return rendered.pixelColor(rendered.rect().center());
}

TEST(QtTextureInfoWindowTests,
     HasCloseOnlyToolChromeResizableLayoutAndReadonlyMetadata) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-texture-info-layout-tests";
  char *argv[] = {application_name, nullptr};
  const ScopedDefaultLocale locale(QLocale::c());
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  const Qt::WindowFlags flags = window.windowFlags();
  EXPECT_EQ(window.windowType(), Qt::Tool);
  EXPECT_TRUE(flags.testFlag(Qt::CustomizeWindowHint));
  EXPECT_TRUE(flags.testFlag(Qt::WindowTitleHint));
  EXPECT_TRUE(flags.testFlag(Qt::WindowSystemMenuHint));
  EXPECT_TRUE(flags.testFlag(Qt::WindowCloseButtonHint));
  EXPECT_FALSE(flags.testFlag(Qt::WindowMinimizeButtonHint));
  EXPECT_FALSE(flags.testFlag(Qt::WindowMaximizeButtonHint));
  EXPECT_FALSE(window.testAttribute(Qt::WA_DeleteOnClose));

  window.show();
  QCoreApplication::processEvents();
  const QSize initial_size = window.size();
  window.resize(initial_size + QSize(96, 72));
  QCoreApplication::processEvents();
  EXPECT_GE(window.width(), initial_size.width() + 96);
  EXPECT_GE(window.height(), initial_size.height() + 72);

  auto *splitter = window.findChild<QSplitter *>(
      QStringLiteral("textureInfo.splitter"));
  auto *table = window.findChild<QTableWidget *>(
      QStringLiteral("textureInfo.table"));
  auto *canvas = window.findChild<QWidget *>(
      QStringLiteral("textureInfo.canvas"));
  ASSERT_NE(splitter, nullptr);
  ASSERT_NE(table, nullptr);
  ASSERT_NE(canvas, nullptr);
  EXPECT_EQ(splitter->orientation(), Qt::Vertical);
  EXPECT_EQ(splitter->count(), 2);
  EXPECT_FALSE(splitter->childrenCollapsible());
  EXPECT_EQ(splitter->widget(0), canvas);
  EXPECT_EQ(splitter->widget(1), table);
  EXPECT_EQ(table->rowCount(), 5);
  EXPECT_EQ(table->columnCount(), 2);
  EXPECT_EQ(table->horizontalHeaderItem(0)->text(),
            QStringLiteral("Property"));
  EXPECT_EQ(table->horizontalHeaderItem(1)->text(), QStringLiteral("Value"));
  EXPECT_EQ(table->editTriggers(), QAbstractItemView::NoEditTriggers);
  EXPECT_EQ(table->selectionMode(), QAbstractItemView::NoSelection);
  EXPECT_EQ(table->horizontalHeader()->sectionResizeMode(0),
            QHeaderView::Interactive);
  EXPECT_EQ(table->horizontalHeader()->sectionResizeMode(1),
            QHeaderView::Stretch);

  const std::vector<std::byte> pixels{
      std::byte{12u}, std::byte{34u}, std::byte{56u}, std::byte{78u}};
  TextureInfoPresentation presentation = texture_presentation(pixels);
  ASSERT_TRUE(window.present(presentation));
  EXPECT_TRUE(window.has_presentation());
  ASSERT_NE(window.current_context(), nullptr);
  EXPECT_EQ(*window.current_context(), presentation.context);
  EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("name"));
  EXPECT_EQ(table->item(1, 0)->text(), QStringLiteral("location"));
  EXPECT_EQ(table->item(2, 0)->text(), QStringLiteral("format"));
  EXPECT_EQ(table->item(3, 0)->text(), QStringLiteral("has alpha"));
  EXPECT_EQ(table->item(4, 0)->text(), QStringLiteral("size"));
  EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("wall_albedo.png"));
  EXPECT_EQ(table->item(1, 1)->text(),
            QStringLiteral("C:\\Project\\Textures\\wall_albedo.png"));
  EXPECT_EQ(table->item(2, 1)->text(), QStringLiteral("PNG"));
  EXPECT_EQ(table->item(3, 1)->checkState(), Qt::Checked);
  const Qt::ItemFlags alpha_flags = table->item(3, 1)->flags();
  EXPECT_TRUE(alpha_flags.testFlag(Qt::ItemIsEnabled));
  EXPECT_FALSE(alpha_flags.testFlag(Qt::ItemIsSelectable));
  EXPECT_FALSE(alpha_flags.testFlag(Qt::ItemIsEditable));
  EXPECT_FALSE(alpha_flags.testFlag(Qt::ItemIsUserCheckable));
  EXPECT_EQ(table->item(4, 1)->text(), QStringLiteral("1.3 MB"));

  const std::uint64_t rebuilds = window.masked_image_rebuild_count();
  presentation.source_has_alpha = false;
  presentation.name = "updated_name.png";
  presentation.encoded_byte_size = 750'000u;
  ASSERT_TRUE(window.present(presentation));
  EXPECT_EQ(window.masked_image_rebuild_count(), rebuilds)
      << "metadata-only changes must not rebuild the masked image";
  EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("updated_name.png"));
  EXPECT_EQ(table->item(3, 1)->checkState(), Qt::Unchecked);
  EXPECT_EQ(table->item(4, 1)->text(), QStringLiteral("750 KB"));

  EXPECT_EQ(QtTextureInfoWindow::format_storage_size(1'250u),
            QStringLiteral("1.3 KB"));
  EXPECT_EQ(QtTextureInfoWindow::format_storage_size(999'999u),
            QStringLiteral("1000 KB"));
  EXPECT_EQ(QtTextureInfoWindow::format_storage_size(1'000'000u),
            QStringLiteral("1 MB"));
  EXPECT_EQ(QtTextureInfoWindow::format_storage_size(999'999'999u),
            QStringLiteral("1000 MB"));
  EXPECT_EQ(QtTextureInfoWindow::format_storage_size(1'000'000'000u),
            QStringLiteral("1 GB"));
}

TEST(QtTextureInfoWindowTests,
     AppliesEveryChannelMaskAndRebuildsOnlyForSourceOrMaskChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-texture-info-channel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtTextureInfoWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.show();
  QCoreApplication::processEvents();

  auto *canvas = window.findChild<QWidget *>(
      QStringLiteral("textureInfo.canvas"));
  auto *red = window.findChild<QToolButton *>(
      QStringLiteral("textureInfo.channel.red"));
  auto *green = window.findChild<QToolButton *>(
      QStringLiteral("textureInfo.channel.green"));
  auto *blue = window.findChild<QToolButton *>(
      QStringLiteral("textureInfo.channel.blue"));
  auto *alpha = window.findChild<QToolButton *>(
      QStringLiteral("textureInfo.channel.alpha"));
  ASSERT_NE(canvas, nullptr);
  ASSERT_NE(red, nullptr);
  ASSERT_NE(green, nullptr);
  ASSERT_NE(blue, nullptr);
  ASSERT_NE(alpha, nullptr);

  QPalette canvas_palette = canvas->palette();
  canvas_palette.setColor(QPalette::Window, Qt::white);
  canvas_palette.setColor(QPalette::Base, Qt::white);
  canvas_palette.setColor(QPalette::Mid, Qt::white);
  canvas->setPalette(canvas_palette);

  constexpr std::array<std::uint8_t, 4> source{200u, 100u, 50u, 128u};
  const std::vector<std::byte> pixels{
      std::byte{source[0]}, std::byte{source[1]}, std::byte{source[2]},
      std::byte{source[3]}};
  TextureInfoPresentation presentation = texture_presentation(pixels);
  ASSERT_TRUE(window.present(presentation));
  ASSERT_EQ(window.masked_image_rebuild_count(), 1u);
  ASSERT_TRUE(window.present(presentation));
  EXPECT_EQ(window.masked_image_rebuild_count(), 1u);

  const std::array<QToolButton *, 4> buttons{red, green, blue, alpha};
  std::uint8_t previous_mask = 0x0fu;
  std::uint64_t expected_rebuilds = 1u;
  for (std::uint8_t mask = 0u; mask < 16u; ++mask) {
    expected_rebuilds += std::popcount(
        static_cast<unsigned int>(previous_mask ^ mask));
    for (std::size_t channel = 0u; channel < buttons.size(); ++channel) {
      buttons[channel]->setChecked((mask & (1u << channel)) != 0u);
    }
    previous_mask = mask;

    EXPECT_EQ(window.property("channelMask").toUInt(), mask);
    EXPECT_EQ(window.masked_image_rebuild_count(), expected_rebuilds);
    EXPECT_EQ(canvas->property("maskedImageRebuildCount").toULongLong(),
              expected_rebuilds);

    const QColor observed = rendered_canvas_center(*canvas);
    const std::array<int, 3> observed_components{
        observed.red(), observed.green(), observed.blue()};
    const int effective_alpha = (mask & 0x08u) != 0u ? source[3] : 255;
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
      const int component =
          (mask & (1u << channel)) != 0u ? source[channel] : 0;
      const int expected =
          (component * effective_alpha + 255 * (255 - effective_alpha) +
           127) /
          255;
      EXPECT_NEAR(observed_components[channel], expected, 2)
          << "mask=" << static_cast<unsigned int>(mask)
          << " channel=" << channel;
    }
    EXPECT_EQ(observed.alpha(), 255);

    ASSERT_TRUE(window.present(presentation));
    EXPECT_EQ(window.masked_image_rebuild_count(), expected_rebuilds)
        << "unchanged source and channel mask must reuse the masked image";
  }

  presentation.context.dependency_revision += 1u;
  ASSERT_TRUE(window.present(presentation));
  EXPECT_EQ(window.masked_image_rebuild_count(), expected_rebuilds)
      << "dependency metadata does not change texture content";
  presentation.content_revision += 1u;
  ASSERT_TRUE(window.present(presentation));
  EXPECT_EQ(window.masked_image_rebuild_count(), expected_rebuilds + 1u);
}

} // namespace
} // namespace quader
