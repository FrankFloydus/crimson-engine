////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_assets_api.hpp"
#include "ui/qt_asset_thumbnail_cache.hpp"

#include <gtest/gtest.h>

#include <QColor>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace quader;

namespace {

ProjectAssetId asset_id(unsigned char value) {
  std::array<std::byte, 16u> bytes{};
  bytes[0] = std::byte{1u};
  bytes[15] = static_cast<std::byte>(value);
  return ProjectAssetId::from_bytes(bytes);
}

/// Driver stand-in retaining readbacks independently of the cache's lifetime.
struct ThumbnailDriver final {
  std::size_t loads = 0u;
  std::size_t submissions = 0u;
  std::uint64_t content_revision = 7u;
  std::uint64_t texture_revision = 11u;
  std::optional<ProjectAssetId> edited_texture_material;
  float roughness = 1.0f;
  bool available = true;
  std::vector<std::shared_ptr<MaterialThumbnailReadback>> readbacks;

  void bind(QtAssetThumbnailCache &cache) {
    cache.initialize(
        [this](ProjectAssetId id)
            -> std::optional<RenderMaterialEvaluationSnapshot> {
          ++loads;
          if (!available) {
            return std::nullopt;
          }
          RenderMaterialEvaluationSnapshot snapshot;
          snapshot.project_material_id = id;
          snapshot.content_revision = content_revision;
          snapshot.standard_material()->roughness = roughness;
          snapshot.standard_material()->textures[0].content_revision =
              !edited_texture_material || *edited_texture_material == id
                  ? texture_revision : 11u;
          return snapshot;
        },
        [this](RenderMaterialEvaluationSnapshot, std::uint32_t size) {
          ++submissions;
          auto readback = std::make_shared<MaterialThumbnailReadback>();
          readback->size = size;
          readback->rgba8.resize(static_cast<std::size_t>(size) * size * 4u,
                                 std::byte{255u});
          readbacks.push_back(readback);
          return readback;
        });
    cache.set_project_generation(1u);
    cache.set_catalog_revision(1u);
    cache.set_visible(true);
  }

  void finish() {
    readbacks.back()->complete.store(true, std::memory_order_release);
  }
};

} // namespace

TEST(QtAssetThumbnailCacheTests, PaintMissesDeferLoadingAndUnchangedFramesReusePixels) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_TRUE(cache.request(asset_id(1u), 7u, {4, 4}, 2.0).isNull());
  }
  EXPECT_EQ(driver.loads, 0u);
  EXPECT_EQ(cache.pending_count(), 1u);
  EXPECT_TRUE(cache.poll().empty());
  ASSERT_EQ(driver.loads, 1u);
  ASSERT_EQ(driver.submissions, 1u);
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_TRUE(cache.poll().empty());
  }
  EXPECT_EQ(driver.loads, 1u);
  EXPECT_EQ(driver.submissions, 1u);
  driver.finish();
  EXPECT_EQ(cache.poll(), std::vector<ProjectAssetId>{asset_id(1u)});
  const QImage first = cache.request(asset_id(1u), 7u, {4, 4}, 2.0);
  ASSERT_FALSE(first.isNull());
  EXPECT_EQ(first.size(), QSize(8, 8));
  EXPECT_EQ(first.devicePixelRatio(), 2.0);
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_EQ(cache.request(asset_id(1u), 7u, {4, 4}, 2.0).cacheKey(), first.cacheKey());
    EXPECT_TRUE(cache.poll().empty());
  }
  EXPECT_EQ(driver.loads, 1u);
  EXPECT_EQ(cache.cached_bytes(), 8u * 8u * 4u);
}

TEST(QtAssetThumbnailCacheTests, ReadbackOwnsTopDownPixelsAfterDriverStorageExpires) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  (void)cache.request(asset_id(1u), 7u, {2, 2}, 1.0);
  (void)cache.poll();
  ASSERT_EQ(driver.readbacks.size(), 1u);
  auto &bytes = driver.readbacks.back()->rgba8;
  bytes[0u] = std::byte{12u};
  bytes[8u] = std::byte{201u};
  driver.finish();
  (void)cache.poll();
  driver.readbacks.clear();
  const QImage image = cache.request(asset_id(1u), 7u, {2, 2}, 1.0);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.pixelColor(0, 0).red(), 12);
  EXPECT_EQ(image.pixelColor(0, 1).red(), 201);
}

TEST(QtAssetThumbnailCacheTests, TransparentReadbackPreservesPremultipliedEdgeColors) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  (void)cache.request(asset_id(1u), 7u, {1, 1}, 1.0);
  (void)cache.poll();
  ASSERT_EQ(driver.readbacks.size(), 1u);
  driver.readbacks.back()->rgba8 = {
      std::byte{128u}, std::byte{0u}, std::byte{0u}, std::byte{128u}};
  driver.finish();
  (void)cache.poll();
  const auto image = cache.request(asset_id(1u), 7u, {1, 1}, 1.0);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(image.format(), QImage::Format_RGBA8888_Premultiplied);
  EXPECT_EQ(image.pixelColor(0, 0), QColor(255, 0, 0, 128));
}

TEST(QtAssetThumbnailCacheTests, HiddenAndReboundBrowsersDropPendingWorkAndStaleCallbacks) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  (void)cache.poll();
  cache.set_visible(false);
  driver.finish();
  EXPECT_TRUE(cache.poll().empty());
  EXPECT_TRUE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  EXPECT_EQ(cache.pending_count(), 0u);
  EXPECT_EQ(cache.cached_bytes(), 0u);
  EXPECT_EQ(driver.submissions, 1u);

  cache.set_visible(true);
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  (void)cache.poll();
  cache.set_project_generation(2u);
  driver.finish();
  EXPECT_TRUE(cache.poll().empty());
  EXPECT_EQ(cache.cached_bytes(), 0u);
  cache.shutdown();
  EXPECT_TRUE(cache.poll().empty());
  EXPECT_EQ(driver.loads, 2u);
}

TEST(QtAssetThumbnailCacheTests, TextureCatalogDpiAndSavedRevisionRefreshInTheBackground) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  (void)cache.poll();
  driver.finish();
  (void)cache.poll();
  ASSERT_FALSE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  cache.set_catalog_revision(1u);
  EXPECT_FALSE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  EXPECT_EQ(driver.loads, 1u);

  driver.texture_revision = 12u;
  cache.set_catalog_revision(2u);
  EXPECT_FALSE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  (void)cache.poll();
  EXPECT_EQ(driver.loads, 2u);
  EXPECT_TRUE(cache.request(asset_id(1u), 7u, {4, 4}, 2.0).isNull());
  driver.finish();
  (void)cache.poll();
  EXPECT_EQ(driver.loads, 3u);
  driver.finish();
  (void)cache.poll();
  EXPECT_EQ(cache.request(asset_id(1u), 7u, {4, 4}, 2.0).size(), QSize(8, 8));

  driver.content_revision = 8u;
  driver.roughness = 0.25f;
  EXPECT_FALSE(cache.request(asset_id(1u), 8u, {4, 4}, 2.0).isNull());
  (void)cache.poll();
  EXPECT_EQ(driver.loads, 4u);
  EXPECT_EQ(driver.submissions, 4u);
}

TEST(QtAssetThumbnailCacheTests, OrganizationAndRenameRetainEveryUnchangedThumbnail) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  std::array<qint64, 3u> image_keys{};
  for (unsigned char index = 0u; index < image_keys.size(); ++index) {
    const auto id = asset_id(index + 1u);
    (void)cache.request(id, 7u, {4, 4}, 1.0);
    (void)cache.poll();
    driver.finish();
    (void)cache.poll();
    image_keys[index] = cache.request(id, 7u, {4, 4}, 1.0).cacheKey();
  }
  const auto cached_bytes = cache.cached_bytes();
  for (std::uint64_t revision = 2u; revision < 5u; ++revision) {
    cache.set_catalog_revision(revision);
    for (unsigned char index = 0u; index < image_keys.size(); ++index) {
      const auto id = asset_id(index + 1u);
      EXPECT_EQ(cache.request(id, 7u, {4, 4}, 1.0).cacheKey(), image_keys[index]);
      (void)cache.poll();
      EXPECT_EQ(cache.request(id, 7u, {4, 4}, 1.0).cacheKey(), image_keys[index]);
    }
    EXPECT_EQ(driver.submissions, 3u);
    EXPECT_EQ(cache.cached_bytes(), cached_bytes);
  }
  // A QMAT rename changes its saved-file revision but no rendered values.
  driver.content_revision = 8u;
  EXPECT_EQ(cache.request(asset_id(1u), 8u, {4, 4}, 1.0).cacheKey(), image_keys[0]);
  (void)cache.poll();
  EXPECT_EQ(cache.request(asset_id(1u), 8u, {4, 4}, 1.0).cacheKey(), image_keys[0]);
  EXPECT_EQ(driver.submissions, 3u);
  EXPECT_EQ(cache.pending_count(), 0u);
}

TEST(QtAssetThumbnailCacheTests, TextureEditRendersOnlyTheDependentMaterial) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  std::array<qint64, 2u> image_keys{};
  for (unsigned char index = 0u; index < image_keys.size(); ++index) {
    const auto id = asset_id(index + 1u);
    (void)cache.request(id, 7u, {4, 4}, 1.0);
    (void)cache.poll();
    driver.finish();
    (void)cache.poll();
    image_keys[index] = cache.request(id, 7u, {4, 4}, 1.0).cacheKey();
  }
  driver.edited_texture_material = asset_id(1u);
  driver.texture_revision = 12u;
  cache.set_catalog_revision(2u);
  EXPECT_EQ(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).cacheKey(), image_keys[0]);
  EXPECT_EQ(cache.request(asset_id(2u), 7u, {4, 4}, 1.0).cacheKey(), image_keys[1]);
  (void)cache.poll();
  EXPECT_EQ(driver.submissions, 3u);
  driver.finish();
  (void)cache.poll();
  EXPECT_NE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).cacheKey(), image_keys[0]);
  EXPECT_EQ(cache.request(asset_id(2u), 7u, {4, 4}, 1.0).cacheKey(), image_keys[1]);
  EXPECT_EQ(driver.submissions, 3u);
  EXPECT_EQ(cache.pending_count(), 0u);
  EXPECT_EQ(cache.cached_bytes(), 2u * 4u * 4u * 4u);
}

TEST(QtAssetThumbnailCacheTests, CatalogMutationDoesNotDiscardAnInFlightThumbnail) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  (void)cache.poll();
  cache.set_catalog_revision(2u);
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  EXPECT_EQ(cache.pending_count(), 1u);
  driver.finish();
  (void)cache.poll();
  const auto rendered = cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  ASSERT_FALSE(rendered.isNull());
  (void)cache.poll();
  EXPECT_EQ(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).cacheKey(), rendered.cacheKey());
  EXPECT_EQ(driver.loads, 2u);
  EXPECT_EQ(driver.submissions, 1u);
  EXPECT_EQ(cache.pending_count(), 0u);
}

TEST(QtAssetThumbnailCacheTests, QueueIsBoundedAndScrollingCancelsUnseenTiles) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  for (unsigned char id = 1u; id <= 80u; ++id) {
    (void)cache.request(asset_id(id), 7u, {4, 4}, 1.0);
  }
  EXPECT_EQ(cache.pending_count(), QtAssetThumbnailCache::kMaximumPending);
  const std::array visible{asset_id(78u), asset_id(80u)};
  cache.retain_visible_requests(visible);
  EXPECT_EQ(cache.pending_count(), 2u);
  (void)cache.poll();
  EXPECT_EQ(driver.loads, 1u);
  cache.retain_visible_requests({});
  driver.finish();
  EXPECT_TRUE(cache.poll().empty());
  EXPECT_EQ(cache.cached_bytes(), 0u);
}

TEST(QtAssetThumbnailCacheTests, ByteBudgetEvictsLeastRecentlyUsedImage) {
  QtAssetThumbnailCache cache(2u * 4u * 4u * 4u);
  ThumbnailDriver driver;
  driver.bind(cache);
  const auto render = [&](unsigned char id) {
    (void)cache.request(asset_id(id), 7u, {4, 4}, 1.0);
    (void)cache.poll();
    driver.finish();
    (void)cache.poll();
  };
  render(1u);
  render(2u);
  EXPECT_FALSE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  render(3u);
  EXPECT_EQ(cache.cached_bytes(), 128u);
  EXPECT_FALSE(cache.request(asset_id(1u), 7u, {4, 4}, 1.0).isNull());
  EXPECT_TRUE(cache.request(asset_id(2u), 7u, {4, 4}, 1.0).isNull());
}

TEST(QtAssetThumbnailCacheTests, MissingMaterialsAreNotDecodedEveryPaint) {
  QtAssetThumbnailCache cache;
  ThumbnailDriver driver;
  driver.bind(cache);
  driver.available = false;
  for (int frame = 0; frame < 8; ++frame) {
    (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
    (void)cache.poll();
  }
  EXPECT_EQ(driver.loads, 1u);
  EXPECT_EQ(driver.submissions, 0u);
  cache.set_catalog_revision(2u);
  driver.available = true;
  (void)cache.request(asset_id(1u), 7u, {4, 4}, 1.0);
  (void)cache.poll();
  EXPECT_EQ(driver.loads, 2u);
  EXPECT_EQ(driver.submissions, 1u);
}

TEST(QtAssetThumbnailCacheTests, SavedSnapshotPreservesAllSlotsAndMaterialControls) {
  QuaderProjectMaterialThumbnailInfo source;
  source.material_id = asset_id(1u);
  source.content_revision = 123u;
  source.material.base_color = {0.2f, 0.3f, 0.4f};
  source.material.roughness = 0.25f;
  source.material.metallic = 1.0f;
  source.material.normal_strength = 0.75f;
  source.material.normal_convention = QuaderNormalMapConvention::DirectX;
  source.material.diffuse_model = QuaderMaterialDiffuseModel::Lambert;
  source.material.texture_transform.rotation_degrees = 45.0f;
  for (std::size_t index = 0u; index < source.textures.size(); ++index) {
    auto &texture = source.textures[index].emplace();
    texture.id = asset_id(static_cast<unsigned char>(index + 10u));
    texture.content_revision = index + 20u;
    texture.color_space = index == 0u ? QuaderProjectTextureColorSpace::Srgb
                                       : QuaderProjectTextureColorSpace::Linear;
    texture.rgba8 = std::make_shared<const std::vector<std::byte>>(4u, std::byte{255u});
    texture.width = texture.height = 1u;
  }
  const auto snapshot = asset_material_thumbnail_render_snapshot(source);
  ASSERT_NE(snapshot.standard_material(), nullptr);
  EXPECT_EQ(snapshot.project_material_id, source.material_id);
  EXPECT_EQ(snapshot.content_revision, 123u);
  const auto &material = *snapshot.standard_material();
  EXPECT_FLOAT_EQ(material.base_color.y, 0.3f);
  EXPECT_FLOAT_EQ(material.roughness, 0.25f);
  EXPECT_FLOAT_EQ(material.metallic, 1.0f);
  EXPECT_FLOAT_EQ(material.normal_strength, 0.75f);
  EXPECT_TRUE(material.directx_normal);
  EXPECT_EQ(material.diffuse_model, MaterialDiffuseModel::Lambert);
  EXPECT_FLOAT_EQ(material.texture_transform.rotation_degrees, 45.0f);
  for (std::size_t index = 0u; index < source.textures.size(); ++index) {
    EXPECT_EQ(material.textures[index].project_texture_id, source.textures[index]->id);
    EXPECT_EQ(material.textures[index].content_revision, index + 20u);
    EXPECT_EQ(material.textures[index].decoded_rgba8, source.textures[index]->rgba8);
    EXPECT_TRUE(material.textures[index].decoded_present());
  }
}
