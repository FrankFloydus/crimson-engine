////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_scene_snapshot.hpp"
#include "render/tool_preview_renderable_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader {

class MaterialBindingService;
class MeshRenderableFactory;
class ViewportSettings;

/// Counts accepted coarse surface coverage and explicit compatibility fallbacks.
struct EditableMeshSurfaceWorkCounters final {
  std::size_t source_record_count = 0u;
  std::size_t eligible_record_count = 0u;
  std::size_t active_batch_count = 0u;
  std::size_t active_retained_object_count = 0u;
  std::size_t active_preview_object_count = 0u;
  std::size_t fallback_static_count = 0u;
  std::size_t fallback_nonrenderable_count = 0u;
  std::size_t fallback_unknown_opacity_count = 0u;
  std::size_t fallback_transparent_count = 0u;
  std::size_t fallback_reflected_count = 0u;
  std::size_t fallback_singular_count = 0u;
  std::size_t fallback_nonconformal_count = 0u;
  std::size_t fallback_unmapped_source_count = 0u;
  std::size_t fallback_presentation_count = 0u;
  std::size_t fallback_oversized_geometry_count = 0u;
  std::size_t retired_proxy_count = 0u;
  std::uint64_t batch_create_count = 0u;
};

/// Retains derived coarse surfaces across preview, commit, cancel and subset edits.
/// Source snapshots remain authoritative. Preparation owns every fallible CPU
/// allocation and hidden replacement; publication changes validated GPU resources
/// only after the caller has accepted its remaining preview partitions.
class EditableMeshSurfaceCache final {
public:
  EditableMeshSurfaceCache();
  ~EditableMeshSurfaceCache();
  EditableMeshSurfaceCache(const EditableMeshSurfaceCache &) = delete;
  EditableMeshSurfaceCache &operator=(const EditableMeshSurfaceCache &) = delete;
  EditableMeshSurfaceCache(EditableMeshSurfaceCache &&) = delete;
  EditableMeshSurfaceCache &operator=(EditableMeshSurfaceCache &&) = delete;

  void initialize(MeshRenderableFactory &factory,
                  MaterialBindingService &material_binding);
  void shutdown();
  [[nodiscard]] bool initialized() const noexcept;

  [[nodiscard]] ToolPreviewSubmissionResult prepare(
      const RenderSceneSnapshot &base,
      const std::vector<RenderToolPreviewMeshRecord> &previews,
      const RenderToolPreviewSuppressionSnapshot &suppression = {});
  [[nodiscard]] const std::vector<RenderToolPreviewMeshRecord> &
  prepared_fallback_meshes() const noexcept;
  [[nodiscard]] const RenderToolPreviewSuppressionSnapshot &
  prepared_scene_exclusion() const noexcept;
  void publish_prepared() noexcept;
  void discard_prepared();

  /// Membership alone controls ordinary-cache exclusion, never position revisions.
  [[nodiscard]] const RenderToolPreviewSuppressionSnapshot &
  scene_exclusion() const noexcept;
  [[nodiscard]] EditableMeshSurfaceWorkCounters work_counters() const noexcept;
  [[nodiscard]] std::size_t proxy_count() const noexcept;
  void update_surface_grid(float grid_world_spacing,
                           const ViewportSettings &settings);
  void drain_retired_destroy_queue(std::size_t max_proxy_count);
  void clear();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
