////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"

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

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    filament::Material &line_material);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && scene_ != nullptr &&
             line_material_ != nullptr;
    }

    void sync(const RenderOverlaySceneWireframeSnapshot &snapshot);

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
        filament::VertexBuffer *vertex_buffer = nullptr;
        filament::IndexBuffer *index_buffer = nullptr;
        filament::MaterialInstance *material_instance = nullptr;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
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

        utils::Entity transform_root;
        AssetId mesh_id = kInvalidAssetId;
        std::uint64_t mesh_revision = 0;
        bool selected = false;
        Proxy line_proxy;
    };

private:
    /**
     * Captures the visible scene mesh inputs that require wireframe geometry rebuilds.
     */
    struct WireframeEntityState {
      EntityId entity = kInvalidEntityId;
      AssetId mesh_id = kInvalidAssetId;
      std::uint64_t mesh_revision = 0;
      bool selected = false;

      bool operator==(const WireframeEntityState &) const = default;
    };

    std::vector<WireframeEntityState>
    wireframe_entity_states(
        const RenderOverlaySceneWireframeSnapshot &snapshot) const;
    Entry create_entry(const RenderOverlayMeshSnapshot &mesh);
    void destroy_entry(Entry &entry);
    void destroy_proxy(Proxy &proxy, bool remove_from_scene);
    void destroy_transform_root(Entry &entry);
    void set_proxy_layer_mask(const Proxy &proxy,
                              std::uint8_t layer_mask) const;
    void set_layer_mask(std::uint8_t layer_mask);
    void set_proxy_clip_planes(ViewportCameraClipPlanes clip_planes);
    void update_entry_transform(const Entry &entry,
                                const QMat4 &world_transform) const;
    void update_entry_transforms(
        const RenderOverlaySceneWireframeSnapshot &snapshot);
    void remove_except(const std::unordered_set<EntityId> &live_entities);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material *line_material_ = nullptr;
    std::unordered_map<EntityId, Entry> entries_;
    std::vector<WireframeEntityState> synced_states_;
    std::uint64_t synced_scene_revision_ = 0;
    std::uint64_t synced_asset_revision_ = 0;
    ViewportCameraClipPlanes synced_clip_planes_;
    std::uint8_t layer_mask_ = 0u;
    bool has_synced_ = false;
};

} // namespace quader
