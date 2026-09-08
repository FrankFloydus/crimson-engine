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
#include <span>
#include <vector>

namespace quader {

class MaterialBindingService;
class ToolPreviewRenderableCacheTestPeer;
class ViewportSettings;

/// Describes whether a preview submission changed the retained render state.
enum class ToolPreviewSubmissionResult : std::uint8_t {
  Unchanged,
  Published,
  Failed,
};

/// Records candidate and retirement work for deterministic scale tests.
struct ToolPreviewCacheWorkCounters final {
  std::uint64_t exact_match_candidate_visits = 0u;
  std::uint64_t structural_match_candidate_visits = 0u;
  std::uint64_t retired_proxy_destroy_attempts = 0u;
  std::uint64_t explicit_instance_chunk_creations = 0u;
  std::uint64_t explicit_instance_prefix_updates = 0u;
};

/** One active-prefix slice backed by a retained fixed-capacity InstanceBuffer. */
struct ToolPreviewInstanceChunkPlan final {
  std::size_t transform_offset = 0u;
  std::size_t active_count = 0u;
  std::size_t capacity = 0u;
  bool retained = false;
};

/** GPU-free capacity plan shared by preview construction and scale tests. */
struct ToolPreviewInstancePlan final {
  std::vector<ToolPreviewInstanceChunkPlan> chunks;
  std::size_t active_count = 0u;
  std::size_t allocated_capacity = 0u;
  std::size_t new_chunk_count = 0u;
};

/**
 * Retains every supplied chunk as a high-water pool and appends device-sized
 * chunks only when the requested active prefix no longer fits.
 */
[[nodiscard]] ToolPreviewInstancePlan plan_tool_preview_instances(
    std::size_t active_count, std::size_t maximum_chunk_capacity,
    std::span<const std::size_t> retained_capacities = {});

/** Describes per-record retained capacity and bounded light-pool growth. */
struct ToolPreviewLightPoolPlan final {
  std::vector<std::size_t> requested_counts;
  std::vector<std::size_t> retained_capacities;
  std::size_t new_proxy_count = 0u;
};

/** Computes exact delta growth while preserving every prior light capacity. */
[[nodiscard]] ToolPreviewLightPoolPlan plan_tool_preview_light_pools(
    std::span<const std::size_t> requested_counts,
    std::span<const std::size_t> retained_capacities = {});

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
                    MaterialBindingService &material_binding,
                    std::size_t maximum_instances_per_chunk);
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
    [[nodiscard]] ToolPreviewCacheWorkCounters work_counters() const noexcept {
      return work_counters_;
    }
    void reset_work_counters() noexcept { work_counters_ = {}; }

  private:
    /// Owns the render proxies and identity facts for one preview participant.
    struct PreviewGroup {
      /** One primitive/pass triplet backed by one active-prefix chunk. */
      struct ExplicitInstanceChunk final {
        MeshPrimitiveSpan primitive;
        std::size_t transform_offset = 0u;
        std::size_t active_count = 0u;
        std::size_t capacity = 0u;
        MeshRenderableProxy rendered;
        MeshRenderableProxy shaded;
        MeshRenderableProxy overlay_depth;
      };

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
      std::vector<QMat4> sampling_from_local;
      bool dynamic_positions = false;
      bool explicit_instances = false;
      bool visible = false;
      bool material_uniforms_initialized = false;
      // Updated only when a proxy is created or retired. Idle retirement must
      // not rediscover this value by scanning all remaining instance vectors.
      std::size_t live_proxy_count = 0u;
      MeshRenderableProxy rendered_prototype;
      std::vector<MeshRenderableProxy> rendered_instances;
      MeshRenderableProxy shaded_prototype;
      std::vector<MeshRenderableProxy> shaded_instances;
      MeshRenderableProxy overlay_depth_prototype;
      std::vector<MeshRenderableProxy> overlay_depth_instances;
      std::vector<std::size_t> explicit_instance_capacities;
      std::vector<ExplicitInstanceChunk> explicit_instance_chunks;
    };

    /// Collects all preview groups published as one atomic generation.
    struct PreviewGeneration {
      std::vector<PreviewGroup> groups;
      std::vector<std::size_t> active_group_indices;
      // Filled while proxies are created so retirement can remove the whole
      // generation from Filament with one bulk Scene interaction.
      std::vector<utils::Entity> scene_entities;
      bool detached = false;
    };

    /// Captures normalized producer state used to match retained preview groups.
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
      QMat4 sampling_from_local;
      std::vector<QMat4> instance_sampling_from_local;
      bool dynamic_positions = false;
      bool cast_shadows = false;
      bool suppress_source = false;
    };

    /// Classifies reuse compatibility between a submission and a generation.
    enum class GenerationMatchResult : std::uint8_t {
      None,
      Exact,
      Structural,
    };

    friend class ToolPreviewRenderableCacheTestPeer;

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
    static std::uint64_t preview_exact_reuse_base_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t group_exact_reuse_base_signature(
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
    [[nodiscard]] bool group_explicit_update_is_valid(
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
        std::vector<std::size_t> &candidate_indices,
        bool allow_explicit_count_change);
    GenerationMatchResult match_generation_groups(
        const PreviewGeneration &generation,
        std::span<const RenderToolPreviewMeshRecord *const> renderable_meshes,
        std::vector<std::size_t> &candidate_indices);
    [[nodiscard]] bool update_group(
        PreviewGroup &group,
        const RenderToolPreviewMeshRecord &preview_mesh);
    void publish_generation_subset(
        PreviewGeneration &generation,
        std::vector<std::size_t> candidate_indices);
    void set_group_visible(PreviewGroup &group, bool visible);
    void hide_generation(PreviewGeneration &generation);
    void detach_generation(PreviewGeneration &generation);
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
    ToolPreviewCacheWorkCounters work_counters_;
    std::size_t maximum_instances_per_chunk_ = 1u;
};

/**
 * Owns transient Point, Directional, and Spot lights for one compound preview.
 *
 * Structural candidates are built at zero intensity and remain inert until
 * publish_prepared(). This gives SceneRenderBridge a two-phase publication
 * boundary shared with the mesh preview cache.
 */
class ToolPreviewLightCache final {
public:
  ToolPreviewLightCache() = default;
  ToolPreviewLightCache(const ToolPreviewLightCache &) = delete;
  ToolPreviewLightCache &operator=(const ToolPreviewLightCache &) = delete;
  ToolPreviewLightCache(ToolPreviewLightCache &&) = delete;
  ToolPreviewLightCache &operator=(ToolPreviewLightCache &&) = delete;
  ~ToolPreviewLightCache();

  void initialize(filament::Engine &engine, filament::Scene &scene);
  void shutdown();
  [[nodiscard]] bool initialized() const noexcept {
    return engine_ != nullptr && scene_ != nullptr;
  }

  [[nodiscard]] ToolPreviewSubmissionResult
  prepare(const std::vector<RenderToolPreviewLightRecord> &lights);
  void publish_prepared();
  void discard_prepared();
  void clear();

  [[nodiscard]] std::size_t proxy_count() const noexcept;
  [[nodiscard]] std::uint64_t rebuild_count() const noexcept {
    return rebuild_count_;
  }
  [[nodiscard]] std::uint64_t transform_update_count() const noexcept {
    return transform_update_count_;
  }
  [[nodiscard]] std::uint64_t created_proxy_count() const noexcept {
    return created_proxy_count_;
  }

private:
  /** Owns one transient Filament light and the values needed to update it. */
  struct LightProxy final {
    utils::Entity entity;
    EntityId source_entity = kInvalidEntityId;
    LightComponent light;
    QMat4 world_transform;
    bool enabled = false;
  };

  /** Retains one record's active prefix and its high-water proxy capacity. */
  struct LightPool final {
    EntityId source_entity = kInvalidEntityId;
    LightComponent light;
    std::uint64_t layout_key = 0u;
    std::size_t active_count = 0u;
    std::vector<LightProxy> proxies;
  };

  /** Owns one atomically published transient-light generation. */
  struct Generation final {
    std::vector<LightPool> pools;
  };

  /** Classifies the minimum work needed to publish the pending generation. */
  enum class PendingKind : std::uint8_t {
    None,
    Unchanged,
    RetainedUpdate,
    Grow,
    Rebuild,
  };

  [[nodiscard]] bool same_structure(
      const std::vector<RenderToolPreviewLightRecord> &lights) const;
  [[nodiscard]] bool same_submission(
      const std::vector<RenderToolPreviewLightRecord> &lights) const;
  [[nodiscard]] std::unique_ptr<Generation> build_inert_generation(
      const std::vector<RenderToolPreviewLightRecord> &lights,
      bool &complete);
  [[nodiscard]] std::unique_ptr<Generation> build_inert_growth(
      const std::vector<RenderToolPreviewLightRecord> &lights,
      bool &complete);
  [[nodiscard]] bool create_inert_proxy(
      const RenderToolPreviewLightRecord &record,
      const QMat4 &world_transform, LightProxy &proxy);
  void apply_updates(
      const std::vector<RenderToolPreviewLightRecord> &lights);
  void set_generation_intensity(Generation &generation, bool enabled);
  void destroy_generation(std::unique_ptr<Generation> generation);
  void destroy_proxy(LightProxy &proxy);

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  std::unique_ptr<Generation> current_;
  std::unique_ptr<Generation> prepared_;
  std::vector<RenderToolPreviewLightRecord> submitted_;
  std::vector<RenderToolPreviewLightRecord> pending_submission_;
  PendingKind pending_kind_ = PendingKind::None;
  std::uint64_t rebuild_count_ = 0u;
  std::uint64_t transform_update_count_ = 0u;
  std::uint64_t created_proxy_count_ = 0u;
};

} // namespace quader
