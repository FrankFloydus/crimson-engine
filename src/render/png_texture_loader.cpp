////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/png_texture_loader.hpp"

#include "core/logger.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <lodepng.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace quader {
namespace {

constexpr unsigned kRgbaChannelCount = 4u;

std::uint8_t mip_level_count(unsigned width, unsigned height) {
  unsigned dimension = width > height ? width : height;
  std::uint8_t levels = 1u;
  while (dimension > 1u) {
    dimension /= 2u;
    ++levels;
  }
  return levels;
}

std::vector<unsigned char> flip_rgba_rows(const std::vector<unsigned char> &pixels,
                                          unsigned width, unsigned height) {
  const std::size_t row_byte_count =
      static_cast<std::size_t>(width) * kRgbaChannelCount;
  std::vector<unsigned char> flipped(pixels.size());
  for (unsigned y = 0; y < height; ++y) {
    const std::size_t source_offset =
        static_cast<std::size_t>(y) * row_byte_count;
    const std::size_t target_offset =
        static_cast<std::size_t>(height - 1u - y) * row_byte_count;
    std::memcpy(flipped.data() + target_offset, pixels.data() + source_offset,
                row_byte_count);
  }
  return flipped;
}

filament::Texture::InternalFormat
to_texture_format(PngTextureColorSpace color_space) {
  return color_space == PngTextureColorSpace::Srgb
             ? filament::Texture::InternalFormat::SRGB8_A8
             : filament::Texture::InternalFormat::RGBA8;
}

} // namespace

filament::Texture *
load_png_texture(filament::Engine &engine, const std::filesystem::path &path,
                 PngTextureLoadOptions options) {
  std::vector<unsigned char> pixels;
  unsigned width = 0u;
  unsigned height = 0u;
  const unsigned error = lodepng::decode(pixels, width, height, path.string());
  if (error != 0u || pixels.empty() || width == 0u || height == 0u) {
    LOG_WARN("Unable to load {} {}: {}", options.label, path.string(),
             lodepng_error_text(error));
    return nullptr;
  }

  const std::vector<unsigned char> flipped =
      options.flip_y ? flip_rgba_rows(pixels, width, height)
                     : std::vector<unsigned char>{};
  const std::vector<unsigned char> &upload_pixels =
      options.flip_y ? flipped : pixels;

  const std::uint8_t levels =
      options.generate_mipmaps ? mip_level_count(width, height) : 1u;
  filament::Texture::Usage usage = filament::Texture::Usage::UPLOADABLE |
                                   filament::Texture::Usage::SAMPLEABLE;
  if (options.generate_mipmaps) {
    usage = usage | filament::Texture::Usage::GEN_MIPMAPPABLE;
  }
  filament::Texture *texture =
      filament::Texture::Builder()
          .width(width)
          .height(height)
          .levels(levels)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(to_texture_format(options.color_space))
          .usage(usage)
          .build(engine);
  if (texture == nullptr) {
    LOG_WARN("Unable to create render texture for {} {}", options.label,
             path.string());
    return nullptr;
  }

  texture->setImage(engine, 0, FilamentBufferDescriptorFactory::rgba_pixels(
                                   upload_pixels.data(), upload_pixels.size()));
  if (levels > 1u) {
    texture->generateMipmaps(engine);
  }
  LOG_INFO("Loaded {} '{}' ({}x{})", options.label, path.string(), width,
           height);
  return texture;
}

} // namespace quader
