#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/ColorSpace.h"
#include "quader/core/Types.h"
#include "quader/render/ViewportOverlayFrame.h"
#include "quader/scene/SceneDocument.h"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneRenderBridge {
public:
    ~SceneRenderBridge();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            filament::Material& meshMaterial,
            filament::Material& overlayMaterial,
            std::filesystem::path resourceRoot);
    void shutdown();
    void sync(const SceneDocument& document, const AssetRegistry& assets);
    void setOverlayFrame(ViewportOverlayFrame frame);

private:
    struct MeshProxy {
        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
        AssetId mesh = InvalidAssetId;
        AssetId material = InvalidAssetId;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        filament::MaterialInstance* materialInstance = nullptr;
    };

    struct OverlayProxy {
        utils::Entity entity;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        filament::MaterialInstance* materialInstance = nullptr;
    };

    struct LightProxy {
        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
    };

    void rebuild(const SceneDocument& document, const AssetRegistry& assets);
    void rebuildOverlayProxies();
    void destroyMeshProxy(MeshProxy& proxy);
    void destroyOverlayProxy(OverlayProxy& proxy);
    void destroyLightProxy(LightProxy& proxy);
    filament::Texture* resolveTexture(const AssetRegistry& assets,
            const MaterialDocument* material,
            const char* slotName,
            filament::Texture* fallback);
    filament::Texture* loadTextureAsset(const TextureAsset& texture);
    filament::Texture* createSolidTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, TextureColorSpace colorSpace);
    void destroyTexture(filament::Texture*& texture);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material* meshMaterial_ = nullptr;
    filament::Material* overlayMaterial_ = nullptr;
    std::filesystem::path resourceRoot_;
    std::uint64_t syncedRevision_ = 0;
    bool hasSynced_ = false;
    std::unordered_map<EntityId, MeshProxy> meshProxies_;
    std::vector<OverlayProxy> overlayProxies_;
    std::unordered_map<EntityId, LightProxy> lightProxies_;
    struct TextureCacheKey {
        AssetId id = InvalidAssetId;
        TextureColorSpace colorSpace = TextureColorSpace::Srgb;

        bool operator==(const TextureCacheKey& other) const {
            return id == other.id && colorSpace == other.colorSpace;
        }
    };

    struct TextureCacheKeyHash {
        std::size_t operator()(const TextureCacheKey& key) const {
            const auto colorSpace = static_cast<std::size_t>(key.colorSpace);
            return std::hash<AssetId>{}(key.id) ^ (colorSpace + 0x9e3779b97f4a7c15ull + (std::hash<AssetId>{}(key.id) << 6u) + (std::hash<AssetId>{}(key.id) >> 2u));
        }
    };

    std::unordered_map<TextureCacheKey, filament::Texture*, TextureCacheKeyHash> textureCache_;
    filament::Texture* fallbackAlbedoTexture_ = nullptr;
    filament::Texture* fallbackRoughnessTexture_ = nullptr;
    ViewportOverlayFrame selectedOverlayFrame_;
    ViewportOverlayFrame externalOverlayFrame_;
    std::uint64_t externalOverlayTriangleSignature_ = 0;
};

} // namespace quader
