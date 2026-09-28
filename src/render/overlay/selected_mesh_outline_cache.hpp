////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace quader {

/// Counts source preparation and camera classification separately from topology builds.
struct SelectedMeshOutlineWorkStats {
  std::uint64_t source_refreshes = 0;
  std::uint64_t topology_builds = 0;
  std::uint64_t camera_classifications = 0;
  std::uint64_t visibility_classifications = 0;
  std::uint64_t occurrence_bounds_tests = 0;
};

/// Retains welded adjacency across panes and projects separate ordinary/active silhouettes.
/// Active boundaries remain visible within the ordinary selection's projected coverage.
class SelectedMeshOutlineCache final {
public:
  SelectedMeshOutlineCache();
  ~SelectedMeshOutlineCache();
  SelectedMeshOutlineCache(SelectedMeshOutlineCache &&) noexcept;
  SelectedMeshOutlineCache &operator=(SelectedMeshOutlineCache &&) noexcept;
  [[nodiscard]] const ViewportOverlayFrame &frame(
      const RenderOverlaySelectedObjectSnapshot *snapshot,
      const ViewportOverlayCamera &camera, std::size_t pane,
      const RenderOverlaySelectionSnapshot *hierarchy = nullptr,
      float outline_width_pixels = kOverlaySelectionWireLineWidthPixels);
  [[nodiscard]] std::uint64_t revision(std::size_t pane) const;
  [[nodiscard]] SelectedMeshOutlineWorkStats work_stats() const;
  void reset();

private:
  /// Keeps adjacency and per-pane scratch buffers out of the public render contract.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
