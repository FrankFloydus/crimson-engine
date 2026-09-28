////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/material_thumbnail_renderer.hpp"
#include "render/png_texture_loader.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_asset_browser.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QString>

#include <filament/Engine.h>
#include <filament/Texture.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace quader;

namespace {

/// Exercises the production sphere offscreen without a native window/swapchain.
class ThumbnailRuntimeHarness final {
public:
  ~ThumbnailRuntimeHarness() {
    if (engine == nullptr) {
      return;
    }
    thumbnail.shutdown();
    material_library.shutdown();
    if (white != nullptr) {
      engine->destroy(white);
    }
    filament::Engine::destroy(&engine);
  }

  bool initialize() {
    engine = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine == nullptr) {
      return false;
    }
    resources = std::filesystem::path(qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    if (!material_library.initialize(*engine, resources)) {
      return false;
    }
    const std::array<std::byte, 4u> texel{
        std::byte{255u}, std::byte{255u}, std::byte{255u}, std::byte{255u}};
    white = load_rgba_texture(*engine, texel, 1u, 1u);
    return white != nullptr && thumbnail.initialize(
        *engine, material_library.quader_mesh(), material_library.quader_mesh_lit(),
        *white, resources);
  }

  bool finish(const std::shared_ptr<const MaterialThumbnailReadback> &readback) {
    for (unsigned int attempt = 0u; attempt < 10u && !readback->ready(); ++attempt) {
      if (!engine->flushAndWait(1'000'000'000ull)) {
        return false;
      }
      thumbnail.poll();
    }
    return readback->ready();
  }

  void save(const MaterialThumbnailReadback &readback, const QString &name) {
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (directory.isEmpty()) {
      return;
    }
    ASSERT_TRUE(QDir().mkpath(directory));
    const QImage image(reinterpret_cast<const uchar *>(readback.rgba8.data()),
                        static_cast<int>(readback.size), static_cast<int>(readback.size),
                        static_cast<int>(readback.size * 4u),
                        QImage::Format_RGBA8888_Premultiplied);
    EXPECT_TRUE(image.save(QDir(directory).filePath(name)));
  }

  filament::Engine *engine = nullptr;
  FilamentMaterialLibrary material_library;
  filament::Texture *white = nullptr;
  MaterialThumbnailRenderer thumbnail;
  std::filesystem::path resources;
};

int channel(const MaterialThumbnailReadback &readback, std::size_t pixel,
            std::size_t component) {
  return std::to_integer<int>(readback.rgba8[pixel * 4u + component]);
}

void save_browser_review(ThumbnailRuntimeHarness &harness,
                         const RenderMaterialEvaluationSnapshot &metallic,
                         const RenderMaterialEvaluationSnapshot &textured) {
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (directory.isEmpty()) {
    return;
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-thumbnail-review";
  char *argv[]{name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(QApplication::palette());
  QApplication::setWindowIcon(QIcon(
      QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader_256.png")));
  QtUiScaleApplicationController scale(application);
  const auto owned_image = [](const MaterialThumbnailReadback &readback) {
    const QImage image(reinterpret_cast<const uchar *>(readback.rgba8.data()),
                        static_cast<int>(readback.size),
                        static_cast<int>(readback.size),
                        static_cast<int>(readback.size * 4u),
                        QImage::Format_RGBA8888_Premultiplied);
    return image.copy();
  };
  QImage polished_image;
  QImage red_image;
  QtAssetBrowser browser;
  browser.setAttribute(Qt::WA_DontShowOnScreen, true);
  browser.set_snapshot({
      .generation = 1u,
      .revision = 1u,
      .items = {
          {.id = QStringLiteral("folder"),
           .relative_path = QStringLiteral("Materials"),
           .name = QStringLiteral("Materials"),
           .kind = QtAssetBrowserKind::Folder},
          {.id = QStringLiteral("scene"),
           .relative_path = QStringLiteral("Atrium.qdr"),
           .name = QStringLiteral("Atrium"),
           .kind = QtAssetBrowserKind::Scene},
          {.id = QStringLiteral("polished"),
           .relative_path = QStringLiteral("Polished metal.qmat"),
           .name = QStringLiteral("Polished metal"),
           .kind = QtAssetBrowserKind::Material},
          {.id = QStringLiteral("red"),
           .relative_path = QStringLiteral("Red paint.qmat"),
           .name = QStringLiteral("Red paint"),
           .kind = QtAssetBrowserKind::Material},
      },
  });
  browser.set_thumbnail_provider(
      [&polished_image, &red_image](const QtAssetBrowserItem &item,
                                     QSize logical_size, qreal dpr) {
        const QImage *source = nullptr;
        if (item.id == QStringLiteral("polished")) source = &polished_image;
        if (item.id == QStringLiteral("red")) source = &red_image;
        if (source == nullptr || source->isNull()) return QIcon{};
        QPixmap pixmap = QPixmap::fromImage(source->scaled(
            logical_size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pixmap.setDevicePixelRatio(dpr);
        return QIcon(pixmap);
      });
  browser.restore_presentation_state(QtAssetBrowserViewMode::Grid, 112, true, 200);
  browser.show();
  ASSERT_TRUE(QDir().mkpath(directory));
  for (const float factor : {1.0F, 2.0F}) {
    scale.apply_factor(factor);
    browser.apply_ui_scale(scale.metrics());
    browser.resize(static_cast<int>(1040 * factor), static_cast<int>(340 * factor));
    const auto render_size = static_cast<std::uint32_t>(
        std::ceil(112 * factor * browser.devicePixelRatioF()));
    const auto polished = harness.thumbnail.submit(metallic, render_size);
    ASSERT_NE(polished, nullptr);
    ASSERT_TRUE(harness.finish(polished));
    polished_image = owned_image(*polished);
    const auto red = harness.thumbnail.submit(textured, render_size);
    ASSERT_NE(red, nullptr);
    ASSERT_TRUE(harness.finish(red));
    red_image = owned_image(*red);
    browser.invalidate_thumbnail();
    QCoreApplication::processEvents();
    EXPECT_TRUE(browser.grab().save(QDir(directory).filePath(
        QStringLiteral("asset-browser-gpu-%1.png").arg(static_cast<int>(factor * 100)))));
  }
}

} // namespace

TEST(MaterialThumbnailRuntimeTests, ShadedSphereUsesPackagedReflectionsAndSavedTextures) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_THUMBNAIL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_THUMBNAIL_TESTS=1 for offscreen material sphere pixels";
  }
  ThumbnailRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(std::filesystem::is_regular_file(
      harness.resources / "ibl/lightroom_14b/lightroom_14b_ibl.ktx"));
  RenderMaterialEvaluationSnapshot metallic;
  metallic.standard_material()->roughness = 0.12f;
  metallic.standard_material()->metallic = 1.0f;
  const auto polished = harness.thumbnail.submit(metallic, 128u);
  ASSERT_NE(polished, nullptr);
  ASSERT_TRUE(harness.finish(polished));
  ASSERT_EQ(polished->rgba8.size(), 128u * 128u * 4u);
  // The thumbnail is composited over the browser's live palette, not a black
  // rectangle. Verify real GPU alpha and composition, including antialiased edges.
  EXPECT_EQ(channel(*polished, 0u, 3u), 0);
  EXPECT_EQ(channel(*polished, 127u, 3u), 0);
  EXPECT_EQ(channel(*polished, 127u * 128u, 3u), 0);
  EXPECT_EQ(channel(*polished, 128u * 128u - 1u, 3u), 0);
  EXPECT_EQ(channel(*polished, 64u * 128u + 64u, 3u), 255);
  std::size_t transparent_pixels = 0u;
  std::size_t edge_pixels = 0u;
  for (std::size_t pixel = 0u; pixel < 128u * 128u; ++pixel) {
    const int alpha = channel(*polished, pixel, 3u);
    transparent_pixels += alpha == 0 ? 1u : 0u;
    edge_pixels += alpha > 0 && alpha < 255 ? 1u : 0u;
  }
  EXPECT_GT(transparent_pixels, 1000u);
  EXPECT_GT(edge_pixels, 20u);
  const QImage sphere(reinterpret_cast<const uchar *>(polished->rgba8.data()),
                      128, 128, 128 * 4, QImage::Format_RGBA8888_Premultiplied);
  for (const QColor background : {QColor(23, 24, 25), QColor(240, 240, 240)}) {
    QImage composition(128, 128, QImage::Format_RGBA8888_Premultiplied);
    composition.fill(background);
    {
      QPainter painter(&composition);
      painter.drawImage(0, 0, sphere);
    }
    EXPECT_EQ(composition.pixelColor(0, 0), background);
    EXPECT_EQ(composition.pixelColor(127, 127), background);
    EXPECT_EQ(composition.pixelColor(64, 64), sphere.pixelColor(64, 64));
  }
  harness.save(*polished, QStringLiteral("asset-thumbnail-polished.png"));

  // The same production shader/lights without the packaged reflection map
  // must differ over the sphere, not just at the direct-light highlight.
  harness.thumbnail.shutdown();
  ASSERT_TRUE(harness.thumbnail.initialize(
      *harness.engine, harness.material_library.quader_mesh(),
      harness.material_library.quader_mesh_lit(), *harness.white,
      harness.resources / "missing-thumbnail-environment"));
  const auto without_environment = harness.thumbnail.submit(metallic, 128u);
  ASSERT_NE(without_environment, nullptr);
  ASSERT_TRUE(harness.finish(without_environment));
  std::size_t reflection_pixels = 0u;
  std::size_t shaded_pixels = 0u;
  for (std::size_t y = 24u; y < 104u; ++y) {
    for (std::size_t x = 24u; x < 104u; ++x) {
      const std::size_t pixel = y * 128u + x;
      const int brightness = channel(*polished, pixel, 0u) +
                             channel(*polished, pixel, 1u) +
                             channel(*polished, pixel, 2u);
      shaded_pixels += brightness > 30 ? 1u : 0u;
      int difference = 0;
      for (std::size_t component = 0u; component < 3u; ++component) {
        difference += std::abs(channel(*polished, pixel, component) -
                               channel(*without_environment, pixel, component));
      }
      reflection_pixels += difference > 15 ? 1u : 0u;
    }
  }
  EXPECT_GT(shaded_pixels, 1000u);
  EXPECT_GT(reflection_pixels, 500u);

  harness.thumbnail.shutdown();
  ASSERT_TRUE(harness.thumbnail.initialize(
      *harness.engine, harness.material_library.quader_mesh(),
      harness.material_library.quader_mesh_lit(), *harness.white,
      harness.resources));
  RenderMaterialEvaluationSnapshot textured;
  auto &albedo = textured.standard_material()->textures[0u];
  albedo.width = albedo.height = 1u;
  albedo.content_revision = 1u;
  albedo.decoded_rgba8 = std::make_shared<const std::vector<std::byte>>(
      std::initializer_list<std::byte>{std::byte{255u}, std::byte{0u},
                                       std::byte{0u}, std::byte{255u}});
  const auto red = harness.thumbnail.submit(textured, 128u);
  ASSERT_NE(red, nullptr);
  ASSERT_TRUE(harness.finish(red));
  const std::size_t center = 64u * 128u + 64u;
  EXPECT_GT(channel(*red, center, 0u), channel(*red, center, 1u) + 20);
  int upper_red = 0;
  int lower_red = 0;
  for (std::size_t y = 25u; y < 49u; ++y) {
    for (std::size_t x = 40u; x < 88u; ++x) {
      upper_red += channel(*red, y * 128u + x, 0u);
      lower_red += channel(*red, (127u - y) * 128u + x, 0u);
    }
  }
  // The production key light comes from above the sphere. Filament readback
  // already uses top-down client rows; an extra image flip reverses this rig.
  EXPECT_GT(upper_red, lower_red);
  harness.save(*red, QStringLiteral("asset-thumbnail-red.png"));
  save_browser_review(harness, metallic, textured);
}

TEST(MaterialThumbnailRuntimeTests, ShutdownRetiresPendingReadbackAndExternalHandle) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_THUMBNAIL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_THUMBNAIL_TESTS=1 for pending readback shutdown";
  }
  ThumbnailRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const auto pending = harness.thumbnail.submit({}, 64u);
  ASSERT_NE(pending, nullptr);
  harness.thumbnail.shutdown();
  EXPECT_TRUE(pending->ready());
  EXPECT_EQ(pending->rgba8.size(), 64u * 64u * 4u);
  EXPECT_FALSE(harness.thumbnail.busy());
  EXPECT_EQ(harness.thumbnail.submit({}, 64u), nullptr);
}
