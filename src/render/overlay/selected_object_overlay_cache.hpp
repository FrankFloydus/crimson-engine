////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_face_fill_composition.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

[[nodiscard]] constexpr bool selected_object_face_style_changed(
    bool has_synced, std::uint64_t synced_signature,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return !has_synced || synced_signature != style.face_signature;
}

/// Retains selected face batches independently of camera navigation.
class SelectedObjectOverlayCache {
public:
  SelectedObjectOverlayCache() = default;
  SelectedObjectOverlayCache(const SelectedObjectOverlayCache &) = delete;
  SelectedObjectOverlayCache &operator=(const SelectedObjectOverlayCache &) = delete;
  SelectedObjectOverlayCache(SelectedObjectOverlayCache &&) = delete;
  SelectedObjectOverlayCache &operator=(SelectedObjectOverlayCache &&) = delete;
  ~SelectedObjectOverlayCache();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  filament::Material &face_fill_material);
  void shutdown();

  [[nodiscard]] bool initialized() const {
    return engine_ != nullptr && scene_ != nullptr && face_fill_material_ != nullptr;
  }

  void sync(const RenderOverlaySelectedObjectSnapshot &snapshot);

private:
  /// One composition pass borrowing its batch's shared geometry buffers.
  struct FaceProxy {
    utils::Entity entity;
    filament::MaterialInstance *material_instance = nullptr;
    OverlayFaceFillPass face_fill_pass = OverlayFaceFillPass::Color;
  };

  /// Owns one shared geometry stream and its four existing depth/stencil passes.
  struct FaceBatch {
    FaceBatch() = default;
    FaceBatch(const FaceBatch &) = delete;
    FaceBatch &operator=(const FaceBatch &) = delete;
    FaceBatch(FaceBatch &&) noexcept = default;
    FaceBatch &operator=(FaceBatch &&) noexcept = delete;

    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    std::array<FaceProxy, 4> proxies;
    std::uint32_t face_color = 0u;
    bool active_selected = false;
  };

  /// Scalar geometry and transform inputs compared only after owner stamps change.
  struct SelectedEntityState {
    EntityId entity = kInvalidEntityId;
    AssetId mesh_id = kInvalidAssetId;
    std::uint64_t mesh_revision = 0u;
    MeshGeometryContentId geometry_content_id;
    std::array<float, 16> world_transform;
    std::size_t vertex_count = 0u;
    std::size_t index_count = 0u;
    bool active_selected = false;

    bool operator==(const SelectedEntityState &) const = default;
  };

  [[nodiscard]] std::vector<SelectedEntityState> selected_entity_states(
      const RenderOverlaySelectedObjectSnapshot &snapshot) const;
  void rebuild_face_batches(const RenderOverlaySelectedObjectSnapshot &snapshot);
  void create_face_batch(std::vector<OverlayFaceFillVertex> vertices,
                         std::vector<std::uint32_t> indices,
                         bool active_selected,
                         const ViewportPersistentOverlayStyleSnapshot &style,
                         const QMat4 &world_transform = {});
  void update_face_colors(const ViewportPersistentOverlayStyleSnapshot &style);
  void destroy_face_batches();
  void set_visible(bool visible);
  void record_resource_change();
  void flush_resource_commands();

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Material *face_fill_material_ = nullptr;
  std::vector<FaceBatch> batches_;
  std::vector<SelectedEntityState> synced_states_;
  RenderSourceStamp synced_source_stamp_;
  std::uint64_t synced_face_style_signature_ = 0u;
  bool has_synced_ = false;
  bool visible_ = true;
  std::size_t pending_resource_changes_ = 0u;
};

} // namespace quader