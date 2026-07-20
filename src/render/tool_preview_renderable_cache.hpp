////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/scene_render_extraction.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader {

class MaterialBindingService;
class ViewportSettings;

enum class ToolPreviewSubmissionResult : std::uint8_t {
  Unchanged,
  Published,
  Failed,
};

/**
 * Owns transient renderables derived from tool preview meshes.
 *
 * A published generation has Rendered, Shaded, and overlay-depth siblings
 * sharing one dynamic geometry allocation. The immediately preceding
 * generation remains hidden and reusable. A replacement is built completely
 * while hidden, then published before its predecessor is hidden, so a failed
 * allocation never leaves suppressed source geometry without a replacement.
 */
class ToolPreviewRenderableCache {
public:
    ToolPreviewRenderableCache() = default;
    ToolPreviewRenderableCache(const ToolPreviewRenderableCache&) = delete;
    ToolPreviewRenderableCache& operator=(const ToolPreviewRenderableCache&) = delete;
    ToolPreviewRenderableCache(ToolPreviewRenderableCache&&) = delete;
    ToolPreviewRenderableCache& operator=(ToolPreviewRenderableCache&&) = delete;
    ~ToolPreviewRenderableCache();

    void initialize(MeshRenderableFactory &factory,
                    MaterialBindingService &material_binding);
    void shutdown();

    bool initialized() const {
      return factory_ != nullptr && material_binding_ != nullptr;
    }

    [[nodiscard]] ToolPreviewSubmissionResult
    set_meshes(const std::vector<RenderToolPreviewMeshRecord> &meshes);
    void update_surface_grid(float grid_world_spacing,
                             const ViewportSettings &settings);
    void clear();

    /**
     * Explicit idle-maintenance hook. The caller chooses a fixed budget; a
     * preview publication never destroys a retired Filament proxy.
     */
    void drain_retired_destroy_queue(std::size_t max_proxy_count);

    [[nodiscard]] std::uint64_t signature() const { return mesh_signature_; }
    [[nodiscard]] std::size_t proxy_count() const;

  private:
    struct PreviewGroup {
      int source_object_id = -1;
      EntityId source_entity = kInvalidEntityId;
      EntityId visibility_entity = kInvalidEntityId;
      std::uint64_t geometry_signature = 0u;
      std::uint64_t topology_signature = 0u;
      std::uint64_t material_signature = 0u;
      std::uint64_t material_uniform_signature = 0u;
      std::uint64_t layout_key = 0u;
      // Participant-local payload identity. Dynamic position payload ownership
      // deliberately remains with the producer so its workspace pool can reuse
      // buffers immediately after submission.
      std::uint64_t position_revision = 0u;
      std::shared_ptr<const MeshGeometryPayload> topology_payload;
      std::vector<QMat4> transforms;
      bool dynamic_positions = false;
      bool visible = false;
      bool material_uniforms_initialized = false;
      MeshRenderableProxy rendered_prototype;
      std::vector<MeshRenderableProxy> rendered_instances;
      MeshRenderableProxy shaded_prototype;
      std::vector<MeshRenderableProxy> shaded_instances;
      MeshRenderableProxy overlay_depth_prototype;
      std::vector<MeshRenderableProxy> overlay_depth_instances;
    };

    struct PreviewGeneration {
      std::vector<PreviewGroup> groups;
      std::vector<std::size_t> active_group_indices;
      // Filled while proxies are created so retirement can remove the whole
      // generation from Filament with one bulk Scene interaction.
      std::vector<utils::Entity> scene_entities;
      bool detached = false;
    };

    struct PreviewSubmissionState {
      int source_object_id = -1;
      EntityId source_entity = kInvalidEntityId;
      EntityId visibility_entity = kInvalidEntityId;
      MaterialReference material_reference;
      std::uint64_t material_signature = 0u;
      std::uint64_t material_revision = 0u;
      std::uint64_t mesh_revision = 0u;
      std::uint64_t layout_key = 0u;
      std::uint64_t position_revision = 0u;
      std::shared_ptr<const MeshGeometryPayload> geometry_payload;
      QMat4 transform;
      std::vector<QMat4> instance_transforms;
      bool dynamic_positions = false;
      bool suppress_source = false;
    };

    static std::uint64_t preview_material_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t preview_geometry_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t preview_topology_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t preview_meshes_signature(
        const std::vector<RenderToolPreviewMeshRecord> &preview_meshes);
    static std::uint64_t preview_structural_reuse_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t group_structural_reuse_signature(
        const PreviewGroup &group);
    static std::size_t group_proxy_count(const PreviewGroup &group);
    static bool group_matches_exact(
        const PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh);
    static bool group_matches_structure(
        const PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh);
    static bool group_topology_equals(
        const PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh);
    static bool group_submission_unchanged(
        const PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh);
    bool group_dynamic_update_is_valid(
        const PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh) const;
    static bool submitted_meshes_equal(
        const std::vector<PreviewSubmissionState> &submitted,
        const std::vector<RenderToolPreviewMeshRecord> &meshes);
    static std::vector<PreviewSubmissionState> capture_submission(
        const std::vector<RenderToolPreviewMeshRecord> &meshes);
    bool active_material_uniforms_are_current(
        const PreviewGeneration &generation) const;

    [[nodiscard]] std::unique_ptr<PreviewGeneration>
    build_generation(
        const std::vector<RenderToolPreviewMeshRecord> &meshes,
        bool &complete);
    bool prepare_generation(
        PreviewGeneration &generation,
        const std::vector<RenderToolPreviewMeshRecord> &meshes,
        std::vector<std::size_t> &candidate_indices);
    void update_group(PreviewGroup &group,
                      const RenderToolPreviewMeshRecord &preview_mesh);
    void publish_generation_subset(
        PreviewGeneration &generation,
        std::vector<std::size_t> candidate_indices);
    void set_group_visible(PreviewGroup &group, bool visible);
    void hide_generation(PreviewGeneration &generation);
    void retire_generation(std::unique_ptr<PreviewGeneration> generation);
    bool destroy_one_proxy(PreviewGeneration &generation);
    void destroy_group(PreviewGroup &group, bool detached);

    MeshRenderableFactory* factory_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::unique_ptr<PreviewGeneration> current_generation_;
    std::unique_ptr<PreviewGeneration> warm_generation_;
    std::vector<std::unique_ptr<PreviewGeneration>> retired_generations_;
    std::vector<PreviewSubmissionState> submitted_meshes_;
    std::uint64_t mesh_signature_ = 0u;
    std::uint64_t material_uniform_signature_ = 0u;
    bool material_uniforms_dirty_ = true;
};

} // namespace quader
