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
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {

TextureBindingService::~TextureBindingService() {
    shutdown();
}

void TextureBindingService::initialize(filament::Engine &engine,
                                       std::filesystem::path resource_root,
                                       filament::Texture &solid_color_texture) {
  shutdown();
  engine_ = &engine;
  upload_count_ = 0u;
  destroy_count_ = 0u;
  resource_root_ = std::move(resource_root);
  solid_color_texture_ = &solid_color_texture;
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

    // Proxy owners must destroy live and retired material instances first.
    assert(material_textures_.empty());
    for (auto &[id, entry] : texture_cache_) {
      if (entry.texture != nullptr) {
        ++destroy_count_;
      }
      destroy_texture(entry.texture);
    }
    texture_cache_.clear();
    material_textures_.clear();
    destroy_texture(fallback_albedo_texture_);
    destroy_texture(fallback_roughness_texture_);
    destroy_texture(fallback_metallic_texture_);
    destroy_texture(fallback_normal_texture_);
    solid_color_texture_ = nullptr;
    scene_binding_ = {};
    resource_root_.clear();
    engine_ = nullptr;
}

void TextureBindingService::bind_scene(
    RenderSceneBindingKey scene_binding) {
  if (scene_binding_ == scene_binding) {
    return;
  }
  // Scene identity changes future lookups. An outgoing instance can still be
  // alive in a paced retirement queue; its final release reclaims the texture.
  scene_binding_ = scene_binding;
}

filament::Texture *TextureBindingService::resolve_texture(
    const filament::MaterialInstance &consumer, const AssetRegistry &assets,
    const MaterialDocument *material,
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
    retain_texture(consumer, cache_key);
    return cached->second.texture ? cached->second.texture : fallback;
  }
  bool opaque = false;
  filament::Texture *loaded = load_texture_asset(*texture, opaque);
  texture_cache_[cache_key] = {.texture = loaded, .opaque = opaque};
  upload_count_ += loaded != nullptr ? 1u : 0u;
  retain_texture(consumer, cache_key);
  return loaded ? loaded : fallback;
}

filament::Texture *TextureBindingService::resolve_texture(
    const filament::MaterialInstance &consumer,
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
    retain_texture(consumer, cache_key);
    return cached->second.texture ? cached->second.texture : fallback;
  }
  bool opaque = false;
  filament::Texture *loaded = texture->project_owned()
                                  ? load_project_texture_asset(*texture, opaque)
                                  : load_texture_asset(texture->texture, opaque);
  texture_cache_[cache_key] = {.texture = loaded, .opaque = opaque};
  upload_count_ += loaded != nullptr ? 1u : 0u;
  retain_texture(consumer, cache_key);
  return loaded ? loaded : fallback;
}

bool TextureBindingService::base_color_is_known_opaque(
    const RenderMaterialAssetSnapshot *material) const {
  return base_color_opacity(material) == MaterialOpacity::Opaque;
}

MaterialOpacity TextureBindingService::base_color_opacity(
    const RenderMaterialAssetSnapshot *material) const {
  if (!material || !material->present) {
    return MaterialOpacity::Opaque;
  }
  const auto *texture = material->texture_for_slot(material_texture_slot::kBaseColor);
  if (!texture || !texture->present) {
    return MaterialOpacity::Opaque;
  }
  const TextureCacheKey cache_key{
      .scene_binding = scene_binding_,
      .id = texture->texture_id,
      .project_id = texture->project_texture_id,
      .revision = texture->project_owned() ? texture->content_revision
                                           : texture->texture.revision.value,
      .color_space = texture->texture.color_space,
  };
  const auto cached = texture_cache_.find(cache_key);
  if (cached == texture_cache_.end()) return MaterialOpacity::Unknown;
  return cached->second.texture == nullptr || cached->second.opaque
      ? MaterialOpacity::Opaque : MaterialOpacity::Transparent;
}

void TextureBindingService::retain_texture(
    const filament::MaterialInstance &consumer, const TextureCacheKey &key) {
  auto &keys = material_textures_[&consumer];
  if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
    keys.push_back(key);
    ++texture_cache_.at(key).consumer_count;
  }
}

void TextureBindingService::retain_material_textures(
    const filament::MaterialInstance &source,
    const filament::MaterialInstance &duplicate) {
  const auto found = material_textures_.find(&source);
  if (found == material_textures_.end()) {
    return;
  }
  // Copy first: inserting the duplicate can rehash the consumer map.
  const std::vector<TextureCacheKey> keys = found->second;
  for (const TextureCacheKey &key : keys) {
    retain_texture(duplicate, key);
  }
}

void TextureBindingService::release_material_textures(
    const filament::MaterialInstance *consumer) {
  const auto found = material_textures_.find(consumer);
  if (found == material_textures_.end()) {
    return;
  }
  for (const TextureCacheKey &key : found->second) {
    const auto cached = texture_cache_.find(key);
    assert(cached != texture_cache_.end() &&
           cached->second.consumer_count != 0u);
    if (--cached->second.consumer_count == 0u) {
      if (cached->second.texture != nullptr) {
        ++destroy_count_;
      }
      destroy_texture(cached->second.texture);
      texture_cache_.erase(cached);
    }
  }
  material_textures_.erase(found);
}

TextureBindingService::CacheStats TextureBindingService::cache_stats() const {
  std::size_t resource_count = 0u;
  for (const auto &[key, entry] : texture_cache_) {
    (void)key;
    resource_count += entry.texture != nullptr ? 1u : 0u;
  }
  return {.resource_count = resource_count,
          .material_consumer_count = material_textures_.size(),
          .upload_count = upload_count_,
          .destroy_count = destroy_count_};
}

filament::Texture *
TextureBindingService::load_texture_asset(const TextureAsset &texture,
                                          bool &opaque) {
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
          .opaque_output = &opaque,
      });
}

filament::Texture *TextureBindingService::load_project_texture_asset(
    const RenderTextureAssetSnapshot &texture, bool &opaque) {
  if (!engine_) {
    return nullptr;
  }
  const PngTextureLoadOptions options{
      .color_space = texture.texture.color_space == TextureColorSpace::Srgb
                         ? PngTextureColorSpace::Srgb
                         : PngTextureColorSpace::Linear,
      .generate_mipmaps = true,
      .label = "project material texture",
      .opaque_output = &opaque,
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
