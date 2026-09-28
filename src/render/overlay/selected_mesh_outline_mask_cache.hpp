////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace filament {
class Engine;
class Material;
class Scene;
} // namespace filament

namespace quader {

struct RenderOverlaySelectedObjectSnapshot;
struct RenderOverlaySelectionSnapshot;

/// Separates source inspection and geometry uploads from unchanged camera frames.
struct SelectedMeshOutlineMaskWorkStats final {
  std::uint64_t source_refreshes = 0u;
  std::uint64_t geometry_builds = 0u;
  std::uint64_t uploaded_vertices = 0u;
  std::uint64_t uploaded_indices = 0u;
  std::uint64_t command_submissions = 0u;
  std::size_t ordinary_batches = 0u;
  std::size_t active_batches = 0u;
};

/// Retains world-space triangle batches in a dedicated selection occupancy scene.
/// Source snapshots remain authoritative; camera and style never rebuild geometry.
class SelectedMeshOutlineMaskCache final {
public:
  static constexpr std::uint8_t kOrdinaryLayer = 0x01u;
  static constexpr std::uint8_t kActiveLayer = 0x02u;
  static constexpr std::uint32_t kMaximumBatchVertices = 65'536u;

  SelectedMeshOutlineMaskCache();
  ~SelectedMeshOutlineMaskCache();
  SelectedMeshOutlineMaskCache(const SelectedMeshOutlineMaskCache &) = delete;
  SelectedMeshOutlineMaskCache &operator=(const SelectedMeshOutlineMaskCache &) = delete;

  [[nodiscard]] bool initialize(filament::Engine &engine,
                                filament::Material &mask_material);
  void shutdown();
  /// Returns true only when the retained mask geometry changed.
  bool sync(const RenderOverlaySelectedObjectSnapshot *snapshot,
             const RenderOverlaySelectionSnapshot *hierarchy = nullptr);
  [[nodiscard]] filament::Scene *scene() const noexcept;
  /// False after initialization/upload failure or shutdown, distinct from an empty mask.
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool has_geometry(bool active) const noexcept;
  [[nodiscard]] SelectedMeshOutlineMaskWorkStats work_stats() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
