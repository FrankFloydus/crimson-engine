////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/texture_binding_service.hpp"

#include "core/logger.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/png_texture_loader.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {

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
  fallback_metallic_texture_ =
      create_solid_texture(255, 255, 255, 255, TextureColorSpace::Linear);
  fallback_normal_texture_ =
      create_solid_texture(128, 128, 255, 255, TextureColorSpace::Linear);
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
    destroy_texture(fallback_metallic_texture_);
    destroy_texture(fallback_normal_texture_);
    scene_binding_ = {};
    resource_root_.clear();
    engine_ = nullptr;
}

void TextureBindingService::bind_scene(
    RenderSceneBindingKey scene_binding) {
  if (scene_binding_ == scene_binding) {
    return;
  }
  for (auto &[key, texture] : texture_cache_) {
    (void)key;
    destroy_texture(texture);
  }
  texture_cache_.clear();
  scene_binding_ = scene_binding;
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
      .scene_binding = scene_binding_,
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
  return loaded ? loaded : fallback;
}

filament::Texture *TextureBindingService::resolve_texture(
    const RenderMaterialAssetSnapshot *material, const char *slot_name,
    filament::Texture *fallback) {
  if (!material || !material->present) {
    return fallback;
  }
  const RenderTextureAssetSnapshot *texture =
      material->texture_for_slot(slot_name);
  if (!texture || !texture->present) {
    return fallback;
  }
  const TextureCacheKey cache_key{
      .scene_binding = scene_binding_,
      .id = texture->texture_id,
      .project_id = texture->project_texture_id,
      .revision = texture->project_owned() ? texture->content_revision
                                           : texture->texture.revision.value,
      .color_space = texture->texture.color_space,
  };
  if (const auto cached = texture_cache_.find(cache_key);
      cached != texture_cache_.end()) {
    return cached->second ? cached->second : fallback;
  }
  filament::Texture *loaded = texture->project_owned()
                                  ? load_project_texture_asset(*texture)
                                  : load_texture_asset(texture->texture);
  texture_cache_[cache_key] = loaded;
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

  return load_png_texture(
      *engine_, path,
      PngTextureLoadOptions{
          .color_space = texture.color_space == TextureColorSpace::Srgb
                             ? PngTextureColorSpace::Srgb
                             : PngTextureColorSpace::Linear,
          .generate_mipmaps = true,
          .label = "material texture",
      });
}

filament::Texture *TextureBindingService::load_project_texture_asset(
    const RenderTextureAssetSnapshot &texture) {
  if (!engine_) {
    return nullptr;
  }
  const PngTextureLoadOptions options{
      .color_space = texture.texture.color_space == TextureColorSpace::Srgb
                         ? PngTextureColorSpace::Srgb
                         : PngTextureColorSpace::Linear,
      .generate_mipmaps = true,
      .label = "project material texture",
  };
  if (texture.decoded_rgba8 != nullptr &&
      !texture.decoded_rgba8->empty()) {
    return load_rgba_texture(*engine_, *texture.decoded_rgba8, texture.width,
                             texture.height, options);
  }
  if (texture.encoded_file_bytes == nullptr ||
      texture.encoded_file_bytes->empty()) {
    return nullptr;
  }
  return load_png_texture(
      *engine_, std::span<const std::byte>(*texture.encoded_file_bytes),
      options);
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

} // namespace quader
