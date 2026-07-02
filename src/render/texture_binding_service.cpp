////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/texture_binding_service.hpp"

#include "core/logger.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <lodepng.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::uint8_t mip_level_count(unsigned width, unsigned height) {
  unsigned dimension = std::max(width, height);
  std::uint8_t levels = 1;
  while (dimension > 1u) {
    dimension /= 2u;
    ++levels;
  }
  return levels;
}

} // namespace

TextureBindingService::~TextureBindingService() {
    shutdown();
}

void TextureBindingService::initialize(filament::Engine &engine,
                                       std::filesystem::path resource_root) {
  shutdown();
  engine_ = &engine;
  resource_root_ = std::move(resource_root);
  fallback_albedo_texture_ =
      create_solid_texture(255, 255, 255, 255, TextureColorSpace::Srgb);
  fallback_roughness_texture_ =
      create_solid_texture(255, 255, 255, 255, TextureColorSpace::Linear);
}

void TextureBindingService::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto &[id, texture] : texture_cache_) {
      destroy_texture(texture);
    }
    texture_cache_.clear();
    destroy_texture(fallback_albedo_texture_);
    destroy_texture(fallback_roughness_texture_);
    resource_root_.clear();
    engine_ = nullptr;
}

filament::Texture *TextureBindingService::resolve_texture(
    const AssetRegistry &assets, const MaterialDocument *material,
    const char *slot_name, filament::Texture *fallback) {
  if (!material) {
    return fallback;
  }
  const std::optional<AssetId> texture_id =
      material_texture_slot_asset(*material, slot_name);
  if (!texture_id.has_value()) {
    return fallback;
  }
  const TextureAsset *texture = assets.texture(*texture_id);
  if (!texture) {
    return fallback;
  }
  const TextureCacheKey cache_key{
      .id = *texture_id,
      .revision = texture->revision.value,
      .color_space = texture->color_space,
  };
  if (const auto cached = texture_cache_.find(cache_key);
      cached != texture_cache_.end()) {
    return cached->second ? cached->second : fallback;
  }
  filament::Texture *loaded = load_texture_asset(*texture);
  texture_cache_[cache_key] = loaded;
  evict_obsolete_texture_revisions(cache_key);
  return loaded ? loaded : fallback;
}

filament::Texture *
TextureBindingService::load_texture_asset(const TextureAsset &texture) {
  if (!engine_ || texture.source_path.empty()) {
    return nullptr;
  }

  std::filesystem::path path = texture.source_path;
  if (path.is_relative()) {
    path = resource_root_ / path;
  }

  std::vector<unsigned char> pixels;
  unsigned width = 0;
  unsigned height = 0;
  const unsigned error = lodepng::decode(pixels, width, height, path.string());
  if (error != 0u) {
    LOG_ERROR("Failed to decode material texture '{}': {}", path.string(),
              lodepng_error_text(error));
    return nullptr;
  }

  const std::uint8_t levels = mip_level_count(width, height);
  filament::Texture *loaded =
      filament::Texture::Builder()
          .width(width)
          .height(height)
          .levels(levels)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(texture.color_space == TextureColorSpace::Srgb
                      ? filament::Texture::InternalFormat::SRGB8_A8
                      : filament::Texture::InternalFormat::RGBA8)
          .usage(filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::SAMPLEABLE |
                 filament::Texture::Usage::GEN_MIPMAPPABLE)
          .build(*engine_);
  loaded->setImage(*engine_, 0,
                   FilamentBufferDescriptorFactory::rgba_pixels(pixels.data(),
                                                                pixels.size()));
  if (levels > 1u) {
    loaded->generateMipmaps(*engine_);
  }
  LOG_INFO("Loaded material texture '{}' ({}x{})", path.string(), width,
           height);
  return loaded;
}

filament::Texture *
TextureBindingService::create_solid_texture(std::uint8_t r, std::uint8_t g,
                                            std::uint8_t b, std::uint8_t a,
                                            TextureColorSpace color_space) {
  if (!engine_) {
    return nullptr;
  }
  filament::Texture *texture =
      filament::Texture::Builder()
          .width(1)
          .height(1)
          .levels(1)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(color_space == TextureColorSpace::Srgb
                      ? filament::Texture::InternalFormat::SRGB8_A8
                      : filament::Texture::InternalFormat::RGBA8)
          .usage(filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::SAMPLEABLE)
          .build(*engine_);
  const std::array<unsigned char, 4> pixel{{r, g, b, a}};
  texture->setImage(
      *engine_, 0,
      FilamentBufferDescriptorFactory::rgba_pixels(pixel.data(), pixel.size()));
  return texture;
}

void TextureBindingService::destroy_texture(filament::Texture *&texture) {
  if (engine_ && texture) {
    engine_->destroy(texture);
  }
  texture = nullptr;
}

void TextureBindingService::evict_obsolete_texture_revisions(
    const TextureCacheKey &current_key) {
  for (auto it = texture_cache_.begin(); it != texture_cache_.end();) {
    if (it->first.id == current_key.id &&
        it->first.color_space == current_key.color_space &&
        it->first.revision != current_key.revision) {
      destroy_texture(it->second);
      it = texture_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace quader
