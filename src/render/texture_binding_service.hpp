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
#include <filament/Texture.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>

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

    filament::Texture *fallback_albedo_texture() const {
      return fallback_albedo_texture_;
    }

    filament::Texture *fallback_roughness_texture() const {
      return fallback_roughness_texture_;
    }

    filament::Texture *resolve_texture(const AssetRegistry &assets,
                                       const MaterialDocument *material,
                                       const char *slot_name,
                                       filament::Texture *fallback);
    filament::Texture *resolve_texture(const RenderMaterialAssetSnapshot *material,
                                       const char *slot_name,
                                       filament::Texture *fallback);

  private:
    /**
     * Stores the Texture Cache Key data contract used by the render extraction and Filament resource layer.
     */
    struct TextureCacheKey {
      AssetId id = kInvalidAssetId;
      std::uint64_t revision = 0;
      TextureColorSpace color_space = TextureColorSpace::Srgb;

      bool operator==(const TextureCacheKey &other) const {
        return id == other.id && revision == other.revision &&
               color_space == other.color_space;
      }
    };

    /**
     * Represents a Texture Cache Key Hash value used by the render extraction and Filament resource layer.
     */
    struct TextureCacheKeyHash {
        std::size_t operator()(const TextureCacheKey& key) const {
          const auto color_space = static_cast<std::size_t>(key.color_space);
          std::size_t hash = std::hash<AssetId>{}(key.id);
          hash ^= std::hash<std::uint64_t>{}(key.revision) +
                  0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
          hash ^=
              color_space + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
          return hash;
        }
    };

    filament::Texture *load_texture_asset(const TextureAsset &texture);
    filament::Texture *create_solid_texture(std::uint8_t r, std::uint8_t g,
                                            std::uint8_t b, std::uint8_t a,
                                            TextureColorSpace color_space);
    void evict_obsolete_texture_revisions(const TextureCacheKey &current_key);
    void destroy_texture(filament::Texture *&texture);

    filament::Engine* engine_ = nullptr;
    std::filesystem::path resource_root_;
    std::unordered_map<TextureCacheKey, filament::Texture *,
                       TextureCacheKeyHash>
        texture_cache_;
    filament::Texture *fallback_albedo_texture_ = nullptr;
    filament::Texture *fallback_roughness_texture_ = nullptr;
};

} // namespace quader
