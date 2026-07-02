////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types/q_vec3.hpp"

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

inline constexpr std::size_t kSelectedObjectFaceFillSelectionLimit = 64u;

bool selectedObjectFaceFillsEnabledForSelectionCount(std::size_t selectedMeshCount);

/**
 * Caches selected object overlay renderables outside the transient viewport overlay frame.
 */
class SelectedObjectOverlayCache {
public:
    SelectedObjectOverlayCache() = default;
    SelectedObjectOverlayCache(const SelectedObjectOverlayCache&) = delete;
    SelectedObjectOverlayCache& operator=(const SelectedObjectOverlayCache&) = delete;
    SelectedObjectOverlayCache(SelectedObjectOverlayCache&&) = delete;
    SelectedObjectOverlayCache& operator=(SelectedObjectOverlayCache&&) = delete;
    ~SelectedObjectOverlayCache();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            filament::Material& faceFillMaterial,
            filament::Material& lineMaterial);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr &&
                scene_ != nullptr &&
                faceFillMaterial_ != nullptr &&
                lineMaterial_ != nullptr;
    }

    void sync(const SceneDocument& document, const AssetRegistry& assets, bool visible);

    /**
     * Owns one Filament renderable and its GPU buffers for a cached selected-object overlay part.
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
     * Tracks the cached selected-object face proxies for one scene mesh entity.
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
        std::vector<Proxy> faceProxies;
    };

private:
    /**
     * Tracks the selected entity render inputs that require a selected-overlay geometry rebuild.
     */
    struct SelectedEntityState {
        EntityId entity = InvalidEntityId;
        AssetId meshId = InvalidAssetId;
        std::uint64_t meshRevision = 0;

        bool operator==(const SelectedEntityState&) const = default;
    };

    struct SelectedObjectLineSegment {
        QVec3 start;
        QVec3 end;
    };

    struct LineEntry {
        AssetId meshId = InvalidAssetId;
        std::uint64_t meshRevision = 0;
        std::vector<SelectedObjectLineSegment> segments;
    };

    std::vector<SelectedEntityState> selectedEntityStates(
            const SceneDocument& document,
            const AssetRegistry& assets) const;
    LineEntry createLineEntry(AssetId meshId, const AssetRegistry& assets) const;
    void syncLineEntries(
            const std::vector<SelectedEntityState>& states,
            const AssetRegistry& assets);
    void updateLineProxy(
            const std::vector<SelectedEntityState>& states,
            const SceneDocument& document);
    Entry createEntry(EntityId entityId, AssetId meshId, const SceneDocument& document, const AssetRegistry& assets);
    void destroyEntry(Entry& entry);
    void destroyProxy(Proxy& proxy, bool removeFromScene);
    void destroyLineBatchRoot();
    void destroyFaceEntries();
    void destroyTransformRoot(Entry& entry);
    void setProxyLayerMask(const Proxy& proxy, std::uint8_t layerMask) const;
    void updateEntryTransform(const Entry& entry, const SceneDocument& document, EntityId entityId) const;
    void updateEntryTransforms(const SceneDocument& document);
    void setVisible(bool visible);
    void removeExcept(const std::unordered_set<EntityId>& liveEntities);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material* faceFillMaterial_ = nullptr;
    filament::Material* lineMaterial_ = nullptr;
    utils::Entity lineBatchTransformRoot_;
    Proxy lineProxy_;
    std::unordered_map<EntityId, Entry> entries_;
    std::unordered_map<EntityId, LineEntry> lineEntries_;
    std::vector<SelectedEntityState> syncedStates_;
    std::uint64_t syncedSceneRevision_ = 0;
    std::uint64_t syncedAssetRevision_ = 0;
    std::uint64_t syncedSelectionRevision_ = 0;
    std::uint64_t syncedVisibilityRevision_ = 0;
    std::uint64_t syncedTransformRevision_ = 0;
    std::uint64_t syncedMeshBindingRevision_ = 0;
    bool syncedFaceFillsEnabled_ = false;
    bool hasSynced_ = false;
    bool visible_ = true;
};

} // namespace quader
