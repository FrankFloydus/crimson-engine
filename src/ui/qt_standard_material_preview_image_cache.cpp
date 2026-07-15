////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_standard_material_preview_image_cache.hpp"

#include <limits>
#include <utility>

namespace quader {

QImage QtStandardMaterialPreviewImageCache::image(
    std::size_t slot, std::string_view material_identity,
    std::uint32_t edit_session_generation, std::uint64_t content_revision,
    const std::shared_ptr<const std::vector<std::byte>> &decoded_rgba8,
    std::uint32_t width, std::uint32_t height) {
  if (slot >= entries_.size()) {
    return {};
  }

  Entry &entry = entries_[slot];
  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4u;
  const std::uint64_t expected_bytes =
      row_bytes * static_cast<std::uint64_t>(height);
  if (material_identity.empty() || edit_session_generation == 0u ||
      decoded_rgba8 == nullptr || width == 0u || height == 0u ||
      row_bytes > static_cast<std::uint64_t>(
                      std::numeric_limits<int>::max()) ||
      height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      expected_bytes > std::numeric_limits<std::size_t>::max() ||
      decoded_rgba8->size() != static_cast<std::size_t>(expected_bytes)) {
    entry = {};
    return {};
  }

  if (entry.valid && entry.material_identity == material_identity &&
      entry.edit_session_generation == edit_session_generation &&
      entry.content_revision == content_revision && entry.width == width &&
      entry.height == height) {
    return entry.image;
  }

  const QImage borrowed(
      reinterpret_cast<const uchar *>(decoded_rgba8->data()),
      static_cast<int>(width), static_cast<int>(height),
      static_cast<int>(row_bytes), QImage::Format_RGBA8888);
  QImage owned = borrowed.copy();
  if (owned.isNull()) {
    entry = {};
    return {};
  }
  entry = Entry{
      .material_identity = std::string(material_identity),
      .edit_session_generation = edit_session_generation,
      .content_revision = content_revision,
      .width = width,
      .height = height,
      .image = std::move(owned),
      .valid = true,
  };
  return entry.image;
}

void QtStandardMaterialPreviewImageCache::clear_slot(std::size_t slot) {
  if (slot < entries_.size()) {
    entries_[slot] = {};
  }
}

void QtStandardMaterialPreviewImageCache::clear() { entries_ = {}; }

} // namespace quader
