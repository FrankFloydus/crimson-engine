////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_standard_material_preview_image_cache.hpp"

#include <gtest/gtest.h>

#include <QColor>

#include <cstddef>
#include <memory>
#include <vector>

using namespace quader;

namespace {

std::shared_ptr<std::vector<std::byte>> rgba_pixel(
    std::byte red, std::byte green, std::byte blue, std::byte alpha) {
  return std::make_shared<std::vector<std::byte>>(
      std::initializer_list<std::byte>{red, green, blue, alpha});
}

} // namespace

TEST(QtStandardMaterialPreviewImageCacheTests,
     CopiesOnceAndKeysEachSlotByDocumentSessionAndContentRevision) {
  QtStandardMaterialPreviewImageCache cache;
  const std::shared_ptr<std::vector<std::byte>> source = rgba_pixel(
      std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255});

  const QImage first =
      cache.image(0u, "material-a", 7u, 11u, source, 1u, 1u);
  ASSERT_FALSE(first.isNull());
  EXPECT_EQ(first.pixelColor(0, 0), QColor(255, 0, 0, 255));

  (*source)[0] = std::byte{0};
  (*source)[1] = std::byte{255};
  const QImage same_revision =
      cache.image(0u, "material-a", 7u, 11u, source, 1u, 1u);
  EXPECT_EQ(same_revision.cacheKey(), first.cacheKey());
  EXPECT_EQ(same_revision.constBits(), first.constBits());
  EXPECT_EQ(same_revision.pixelColor(0, 0), QColor(255, 0, 0, 255));

  const QImage changed_revision =
      cache.image(0u, "material-a", 7u, 12u, source, 1u, 1u);
  ASSERT_FALSE(changed_revision.isNull());
  EXPECT_NE(changed_revision.cacheKey(), first.cacheKey());
  EXPECT_EQ(changed_revision.pixelColor(0, 0), QColor(0, 255, 0, 255));

  const QImage changed_session =
      cache.image(0u, "material-a", 8u, 12u, source, 1u, 1u);
  ASSERT_FALSE(changed_session.isNull());
  EXPECT_NE(changed_session.cacheKey(), changed_revision.cacheKey());

  const QImage changed_document =
      cache.image(0u, "material-b", 8u, 12u, source, 1u, 1u);
  ASSERT_FALSE(changed_document.isNull());
  EXPECT_NE(changed_document.cacheKey(), changed_session.cacheKey());
}

TEST(QtStandardMaterialPreviewImageCacheTests,
     ClearAndMalformedInputCannotRetainAStaleTexture) {
  QtStandardMaterialPreviewImageCache cache;
  const std::shared_ptr<std::vector<std::byte>> source = rgba_pixel(
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255});
  const QImage first =
      cache.image(2u, "material-a", 3u, 4u, source, 1u, 1u);
  ASSERT_FALSE(first.isNull());

  cache.clear_slot(2u);
  const QImage after_clear =
      cache.image(2u, "material-a", 3u, 4u, source, 1u, 1u);
  ASSERT_FALSE(after_clear.isNull());
  EXPECT_NE(after_clear.cacheKey(), first.cacheKey());

  const auto malformed =
      std::make_shared<const std::vector<std::byte>>(3u, std::byte{0});
  EXPECT_TRUE(cache
                  .image(2u, "material-a", 3u, 5u, malformed, 1u, 1u)
                  .isNull());
  EXPECT_TRUE(cache
                  .image(2u, "", 3u, 5u, source, 1u, 1u)
                  .isNull());
  EXPECT_TRUE(cache
                  .image(9u, "material-a", 3u, 5u, source, 1u, 1u)
                  .isNull());

  cache.clear();
}
