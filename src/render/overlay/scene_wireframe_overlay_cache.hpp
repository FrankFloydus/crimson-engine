////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <utils/Entity.h>

namespace filament {
class Engine;
class IndexBuffer;
class Material;
class MaterialInstance;
class Scene;
class VertexBuffer;
} // namespace filament

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Caches full-scene wireframe renderables outside the transient viewport overlay frame.
 */
class SceneWireframeOverlayCache {
public:
    SceneWireframeOverlayCache() = default;
    SceneWireframeOverlayCache(const SceneWireframeOverlayCache&) = delete;
    SceneWireframeOverlayCache& operator=(const SceneWireframeOverlayCache&) = delete;
    SceneWireframeOverlayCache(SceneWireframeOverlayCache&&) = delete;
    SceneWireframeOverlayCache& operator=(SceneWireframeOverlayCache&&) = delete;
    ~SceneWireframeOverlayCache();

    void initialize(filament::Engine& engine, filament::Scene& scene, filament::Material& lineMaterial);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr && lineMaterial_ != nullptr;
    }

    void sync(const SceneDocument& document, const AssetRegistry& assets, std::uint8_t layerMask);

    /**
     * Owns one persistent wireframe line renderable and its GPU buffers.
     */
    struct Proxy {
        Proxy() = default;
        Proxy(const Proxy&) = delete;
        Proxy& operator=(const Proxy&) = delete;
        Proxy(Proxy&& other) noexcept;
        Proxy& operator=(Proxy&& other) noexcept;

        utils::Entity entity;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        filament::MaterialInstance* materialInstance = nullptr;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
    };

    /**
     * Tracks the cached wireframe proxy for one visible scene mesh entity.
     */
    struct Entry {
        Entry() = default;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) noexcept = default;
        Entry& operator=(Entry&&) noexcept = delete;

        utils::Entity transformRoot;
        AssetId meshId = InvalidAssetId;
        std::uint64_t meshRevision = 0;
        bool selected = false;
        Proxy lineProxy;
    };

private:
    /**
     * Captures the visible scene mesh inputs that require wireframe geometry rebuilds.
     */
    struct WireframeEntityState {
        EntityId entity = InvalidEntityId;
        AssetId meshId = InvalidAssetId;
        std::uint64_t meshRevision = 0;
        bool selected = false;

        bool operator==(const WireframeEntityState&) const = default;
    };

    std::vector<WireframeEntityState> wireframeEntityStates(
            const SceneDocument& document,
            const AssetRegistry& assets) const;
    Entry createEntry(
            EntityId entityId,
            AssetId meshId,
            bool selected,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void destroyEntry(Entry& entry);
    void destroyProxy(Proxy& proxy, bool removeFromScene);
    void destroyTransformRoot(Entry& entry);
    void setProxyLayerMask(const Proxy& proxy, std::uint8_t layerMask) const;
    void setLayerMask(std::uint8_t layerMask);
    void updateEntryTransform(const Entry& entry, const SceneDocument& document, EntityId entityId) const;
    void updateEntryTransforms(const SceneDocument& document);
    void removeExcept(const std::unordered_set<EntityId>& liveEntities);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material* lineMaterial_ = nullptr;
    std::unordered_map<EntityId, Entry> entries_;
    std::vector<WireframeEntityState> syncedStates_;
    std::uint64_t syncedSceneRevision_ = 0;
    std::uint64_t syncedAssetRevision_ = 0;
    std::uint8_t layerMask_ = 0u;
    bool hasSynced_ = false;
};

} // namespace quader
