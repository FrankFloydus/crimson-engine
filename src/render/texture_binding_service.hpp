////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "core/types.hpp"
#include "render/scene_render_extraction.hpp"

#include <filament/Engine.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>

namespace quader {

/**
 * Coordinates Texture Binding Service responsibilities for the render extraction and Filament resource layer.
 */
class TextureBindingService {
public:
    TextureBindingService() = default;
    TextureBindingService(const TextureBindingService&) = delete;
    TextureBindingService& operator=(const TextureBindingService&) = delete;
    TextureBindingService(TextureBindingService&&) = delete;
    TextureBindingService& operator=(TextureBindingService&&) = delete;
    ~TextureBindingService();

    void initialize(filament::Engine &engine,
                    std::filesystem::path resource_root);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr;
    }

    void bind_scene(RenderSceneBindingKey scene_binding);

    filament::Texture *fallback_albedo_texture() const {
      return fallback_albedo_texture_;
    }

    filament::Texture *fallback_roughness_texture() const {
      return fallback_roughness_texture_;
    }

    filament::Texture *fallback_metallic_texture() const {
      return fallback_metallic_texture_;
    }

    filament::Texture *fallback_normal_texture() const {
      return fallback_normal_texture_;
    }

    // Every non-fallback texture is pinned by the material instance that uses it.
    // Release only after destroying that instance, including detached previews.
    filament::Texture *resolve_texture(const filament::MaterialInstance &consumer,
                                       const AssetRegistry &assets,
                                       const MaterialDocument *material,
                                       const char *slot_name,
                                       filament::Texture *fallback);
    filament::Texture *resolve_texture(const filament::MaterialInstance &consumer,
                                       const RenderMaterialAssetSnapshot *material,
                                       const char *slot_name,
                                       filament::Texture *fallback);

    void retain_material_textures(const filament::MaterialInstance &source,
                                  const filament::MaterialInstance &duplicate);
    void release_material_textures(const filament::MaterialInstance *consumer);

    /// Reports image allocations and material pins; permanent fallbacks excluded.
    struct CacheStats final {
      std::size_t resource_count = 0u;
      std::size_t material_consumer_count = 0u;
      std::uint64_t upload_count = 0u;
      std::uint64_t destroy_count = 0u;
    };
    [[nodiscard]] CacheStats cache_stats() const;

  private:
    /**
     * Stores the Texture Cache Key data contract used by the render extraction and Filament resource layer.
     */
    struct TextureCacheKey {
      RenderSceneBindingKey scene_binding;
      AssetId id = kInvalidAssetId;
      ProjectAssetId project_id;
      std::uint64_t revision = 0;
      TextureColorSpace color_space = TextureColorSpace::Srgb;

      bool operator==(const TextureCacheKey &other) const {
        return scene_binding == other.scene_binding && id == other.id &&
               project_id == other.project_id &&
               revision == other.revision &&
               color_space == other.color_space;
      }
    };

    /**
     * Represents a Texture Cache Key Hash value used by the render extraction and Filament resource layer.
     */
    struct TextureCacheKeyHash {
        std::size_t operator()(const TextureCacheKey& key) const {
          const auto color_space = static_cast<std::size_t>(key.color_space);
          std::size_t hash =
              std::hash<std::uint64_t>{}(key.scene_binding.scene_session_id);
          const auto combine = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) +
                    (hash >> 2u);
          };
          combine(std::hash<std::uint64_t>{}(
              key.scene_binding.activation_generation));
          combine(std::hash<std::uint64_t>{}(
              key.scene_binding.binding_revision));
          combine(std::hash<AssetId>{}(key.id));
          combine(ProjectAssetIdHash{}(key.project_id));
          combine(std::hash<std::uint64_t>{}(key.revision));
          combine(color_space);
          return hash;
        }
    };

    /// Keeps an image alive through its final live or retired material instance.
    struct TextureCacheEntry final {
      filament::Texture *texture = nullptr;
      std::size_t consumer_count = 0u;
    };
    void retain_texture(const filament::MaterialInstance &consumer,
                        const TextureCacheKey &key);

    filament::Texture *load_texture_asset(const TextureAsset &texture);
    filament::Texture *load_project_texture_asset(
        const RenderTextureAssetSnapshot &texture);
    filament::Texture *create_solid_texture(std::uint8_t r, std::uint8_t g,
                                            std::uint8_t b, std::uint8_t a,
                                            TextureColorSpace color_space);
    void destroy_texture(filament::Texture *&texture);

    filament::Engine* engine_ = nullptr;
    std::filesystem::path resource_root_;
    std::unordered_map<TextureCacheKey, TextureCacheEntry,
                       TextureCacheKeyHash>
        texture_cache_;
    std::unordered_map<const filament::MaterialInstance *,
                       std::vector<TextureCacheKey>> material_textures_;
    std::uint64_t upload_count_ = 0u;
    std::uint64_t destroy_count_ = 0u;
    RenderSceneBindingKey scene_binding_;
    filament::Texture *fallback_albedo_texture_ = nullptr;
    filament::Texture *fallback_roughness_texture_ = nullptr;
    filament::Texture *fallback_metallic_texture_ = nullptr;
    filament::Texture *fallback_normal_texture_ = nullptr;
};

} // namespace quader
