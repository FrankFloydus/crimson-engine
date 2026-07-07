////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types/qvec3.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

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

inline constexpr std::size_t kSelectedObjectFaceFillSelectionLimit = 64u;

bool selected_object_face_fills_enabled_for_selection_count(
    std::size_t selected_mesh_count);

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

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    filament::Material &face_fill_material);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && scene_ != nullptr &&
             face_fill_material_ != nullptr;
    }

    void sync(const RenderOverlaySelectedObjectSnapshot &snapshot);

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
        filament::VertexBuffer *vertex_buffer = nullptr;
        filament::IndexBuffer *index_buffer = nullptr;
        filament::MaterialInstance *material_instance = nullptr;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
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

        utils::Entity transform_root;
        AssetId mesh_id = kInvalidAssetId;
        std::uint64_t mesh_revision = 0;
        std::vector<Proxy> face_proxies;
    };

private:
    /**
     * Tracks the selected entity render inputs that require a selected-overlay geometry rebuild.
     */
    struct SelectedEntityState {
      EntityId entity = kInvalidEntityId;
      AssetId mesh_id = kInvalidAssetId;
      std::uint64_t mesh_revision = 0;

      bool operator==(const SelectedEntityState &) const = default;
    };

    std::vector<SelectedEntityState> selected_entity_states(
        const RenderOverlaySelectedObjectSnapshot &snapshot) const;
    Entry create_entry(const RenderOverlayMeshSnapshot &mesh);
    void destroy_entry(Entry &entry);
    void destroy_proxy(Proxy &proxy, bool remove_from_scene);
    void destroy_face_entries();
    void destroy_transform_root(Entry &entry);
    void set_proxy_layer_mask(const Proxy &proxy,
                              std::uint8_t layer_mask) const;
    void update_entry_transform(const Entry &entry,
                                const QMat4 &world_transform) const;
    void update_entry_transforms(
        const RenderOverlaySelectedObjectSnapshot &snapshot);
    void set_visible(bool visible);
    void remove_except(const std::unordered_set<EntityId> &live_entities);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material *face_fill_material_ = nullptr;
    std::unordered_map<EntityId, Entry> entries_;
    std::vector<SelectedEntityState> synced_states_;
    std::uint64_t synced_scene_revision_ = 0;
    std::uint64_t synced_asset_revision_ = 0;
    std::uint64_t synced_selection_revision_ = 0;
    std::uint64_t synced_visibility_revision_ = 0;
    std::uint64_t synced_transform_revision_ = 0;
    std::uint64_t synced_mesh_binding_revision_ = 0;
    bool synced_face_fills_enabled_ = false;
    bool has_synced_ = false;
    bool visible_ = true;
};

} // namespace quader
