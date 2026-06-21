#pragma once

#include "assets/AssetRegistry.h"
#include "assets/MaterialDocument.h"
#include "assets/TextureAsset.h"
#include "core/ColorSpace.h"
#include "core/Types.h"

#include <filament/Engine.h>
#include <filament/Texture.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>

namespace quader {

class TextureBindingService {
public:
    ~TextureBindingService();

    void initialize(filament::Engine& engine, std::filesystem::path resourceRoot);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr;
    }

    filament::Texture* fallbackAlbedoTexture() const {
        return fallbackAlbedoTexture_;
    }

    filament::Texture* fallbackRoughnessTexture() const {
        return fallbackRoughnessTexture_;
    }

    filament::Texture* resolveTexture(const AssetRegistry& assets,
            const MaterialDocument* material,
            const char* slotName,
            filament::Texture* fallback);

private:
    struct TextureCacheKey {
        AssetId id = InvalidAssetId;
        std::uint64_t revision = 0;
        TextureColorSpace colorSpace = TextureColorSpace::Srgb;

        bool operator==(const TextureCacheKey& other) const {
            return id == other.id && revision == other.revision && colorSpace == other.colorSpace;
        }
    };

    struct TextureCacheKeyHash {
        std::size_t operator()(const TextureCacheKey& key) const {
            const auto colorSpace = static_cast<std::size_t>(key.colorSpace);
            std::size_t hash = std::hash<AssetId>{}(key.id);
            hash ^= std::hash<std::uint64_t>{}(key.revision) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            hash ^= colorSpace + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    filament::Texture* loadTextureAsset(const TextureAsset& texture);
    filament::Texture* createSolidTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, TextureColorSpace colorSpace);
    void destroyTexture(filament::Texture*& texture);

    filament::Engine* engine_ = nullptr;
    std::filesystem::path resourceRoot_;
    std::unordered_map<TextureCacheKey, filament::Texture*, TextureCacheKeyHash> textureCache_;
    filament::Texture* fallbackAlbedoTexture_ = nullptr;
    filament::Texture* fallbackRoughnessTexture_ = nullptr;
};

} // namespace quader
