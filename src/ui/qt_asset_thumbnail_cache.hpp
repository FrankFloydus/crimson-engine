////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"
#include "render/material_thumbnail_renderer.hpp"

#include <QImage>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quader {

class RenderSystem;
struct QuaderProjectMaterialThumbnailInfo;

/// Converts saved catalog values only, without consulting a material edit draft.
[[nodiscard]] RenderMaterialEvaluationSnapshot
asset_material_thumbnail_render_snapshot(
    const QuaderProjectMaterialThumbnailInfo &source);

/// A bounded paint-demand queue and byte-budgeted LRU for saved material spheres.
class QtAssetThumbnailCache final {
public:
  using SnapshotLoader = std::function<std::optional<RenderMaterialEvaluationSnapshot>(
      ProjectAssetId)>;
  using Submit = std::function<std::shared_ptr<const MaterialThumbnailReadback>(
      RenderMaterialEvaluationSnapshot, std::uint32_t)>;
  using Pump = std::function<void()>;

  static constexpr std::size_t kDefaultByteBudget = 64u * 1024u * 1024u;
  static constexpr std::size_t kMaximumPending = 64u;

  explicit QtAssetThumbnailCache(std::size_t byte_budget = kDefaultByteBudget);
  ~QtAssetThumbnailCache();
  QtAssetThumbnailCache(const QtAssetThumbnailCache &) = delete;
  QtAssetThumbnailCache &operator=(const QtAssetThumbnailCache &) = delete;

  void initialize(RenderSystem &renderer, SnapshotLoader loader);
  /// Injectable render port allows deterministic lifetime and retained-work tests.
  void initialize(SnapshotLoader loader, Submit submit, Pump pump = {});
  void shutdown();
  void set_visible(bool visible);
  void set_project_generation(std::uint64_t generation);
  /// Revalidates visible saved values/dependencies without dropping retained pixels.
  /// Must include texture catalog changes as well as saved material changes.
  void set_catalog_revision(std::uint64_t revision);
  /// Call only for visible tile paint requests; a miss queues metadata, not pixels.
  [[nodiscard]] QImage request(ProjectAssetId id, std::uint64_t content_revision,
                               QSize logical_size, qreal device_pixel_ratio);
  /// Cancel queued work after a scroll/filter change without touching the catalog.
  void retain_visible_requests(std::span<const ProjectAssetId> visible_ids);
  /// Runs on the UI/render owner thread; submits at most one new job per call.
  [[nodiscard]] std::vector<ProjectAssetId> poll();
  [[nodiscard]] std::size_t cached_bytes() const noexcept;
  [[nodiscard]] std::size_t pending_count() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
