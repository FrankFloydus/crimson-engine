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

#include <array>
#include <cstddef>
#include <cstdint>
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
            filament::Material& overlayLineMaterial,
            filament::Material& overlayPointMaterial,
            std::filesystem::path resourceRoot);
    void shutdown();
    void sync(const SceneDocument& document, const AssetRegistry& assets);
    void setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera);
    void setOverlayFrame(std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayFrame frame,
            ViewportOverlayCamera camera);

private:
    static constexpr std::size_t kMaxViewportOverlaySlots = 4u;

    struct MeshProxy {
        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
        AssetId mesh = InvalidAssetId;
        AssetId material = InvalidAssetId;
        std::uint64_t meshRevision = 0;
        std::uint64_t materialRevision = 0;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        std::vector<std::uint32_t> materialSlots;
        std::vector<filament::MaterialInstance*> materialInstances;
    };

    struct OverlayProxy {
        utils::Entity entity;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        filament::MaterialInstance* materialInstance = nullptr;
    };

    struct OverlayProxyGroup {
        std::uint64_t signature = 0;
        std::vector<OverlayProxy> proxies;
    };

    struct OverlaySlot {
        ViewportOverlayFrame frame;
        std::uint64_t frameSignature = 0;
        ViewportOverlayCamera camera;
        std::uint64_t cameraSignature = 0;
        std::uint8_t layerMask = 0;
        std::array<OverlayProxyGroup, 15> proxyGroups;
    };

    struct LightProxy {
        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
    };

    void rebuild(const SceneDocument& document, const AssetRegistry& assets);
    void rebuildOverlayProxies(OverlaySlot& slot);
    void destroyOverlaySlot(OverlaySlot& slot);
    bool renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const;
    MeshProxy createMeshProxy(EntityId entityId,
            AssetId meshId,
            const MeshAsset& mesh,
            AssetId materialId,
            const MaterialDocument* material,
            const AssetRegistry& assets,
            const TransformComponent& transform);
    void updateMeshProxyTransform(const MeshProxy& proxy, const TransformComponent& transform);
    filament::MaterialInstance* createMeshMaterialInstance(const AssetRegistry& assets, const MaterialDocument* material);
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
    filament::Material* overlayLineMaterial_ = nullptr;
    filament::Material* overlayPointMaterial_ = nullptr;
    std::filesystem::path resourceRoot_;
    std::uint64_t syncedRevision_ = 0;
    std::uint64_t syncedAssetRevision_ = 0;
    bool hasSynced_ = false;
    std::unordered_map<EntityId, MeshProxy> meshProxies_;
    std::array<OverlaySlot, kMaxViewportOverlaySlots> overlaySlots_;
    std::unordered_map<EntityId, LightProxy> lightProxies_;
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

    std::unordered_map<TextureCacheKey, filament::Texture*, TextureCacheKeyHash> textureCache_;
    filament::Texture* fallbackAlbedoTexture_ = nullptr;
    filament::Texture* fallbackRoughnessTexture_ = nullptr;
};

} // namespace quader
