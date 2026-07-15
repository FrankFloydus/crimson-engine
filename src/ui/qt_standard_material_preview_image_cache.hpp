////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QImage>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Retains the owned Qt image for each decoded Standard Material texture.
 *
 * Public material preview bytes are immutable and shared, but QImage must own
 * its storage before those bytes leave the API snapshot. This cache makes that
 * ownership copy once per edit-session texture revision instead of once per
 * scalar-slider presentation refresh.
 */
class QtStandardMaterialPreviewImageCache final {
public:
  static constexpr std::size_t kTextureSlotCount = 4u;

  [[nodiscard]] QImage image(
      std::size_t slot, std::string_view material_identity,
      std::uint32_t edit_session_generation, std::uint64_t content_revision,
      const std::shared_ptr<const std::vector<std::byte>> &decoded_rgba8,
      std::uint32_t width, std::uint32_t height);

  void clear_slot(std::size_t slot);
  void clear();

private:
  /// Owns one image together with the complete key that produced its pixels.
  struct Entry final {
    std::string material_identity;
    std::uint32_t edit_session_generation = 0u;
    std::uint64_t content_revision = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    QImage image;
    bool valid = false;
  };

  std::array<Entry, kTextureSlotCount> entries_;
};

} // namespace quader
