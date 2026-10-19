////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include <crimson/renderer/EditorRendering.hpp>

#include <cstdint>
#include <memory>

namespace quader {

struct RenderToolPreviewSceneSnapshot;

/** Measures CPU overlay work independently of retained scene translation. */
struct CrimsonEditorProjectionCounters final {
  std::uint64_t layer_builds = 0u;
  std::uint64_t position_packs = 0u;
  std::uint64_t attribute_packs = 0u;
  std::uint64_t index_packs = 0u;
  std::uint64_t visibility_packs = 0u;
};

/** Projects existing semantic overlay publications into optional GPU batches. */
class CrimsonEditorProjection final : public ViewportOverlaySubmissionTarget {
public:
  CrimsonEditorProjection();
  ~CrimsonEditorProjection() override;
  [[nodiscard]] ViewportOverlaySubmissionResult submit(
      ViewportOverlaySubmissionRequest request, const ViewportSettings &settings);
  void set_active_viewports(std::uint8_t mask);
  void set_preview(std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview);
  void sync_wireframe(const RenderOverlaySceneWireframeSnapshot &snapshot,
      std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview = {});
  void sync_collision(const RenderOverlayCollisionSnapshot &snapshot,
      const RenderOverlaySelectedObjectSnapshot &presentation,
      std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview = {});
  bool update_selected(std::uint8_t mask,
      const RenderOverlaySelectedObjectSnapshot *snapshot,
      const RenderOverlaySelectionSnapshot *hierarchy,
      const ViewportPersistentOverlayStyleSnapshot &style, bool visible,
      std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview = {});
  void clear();
  [[nodiscard]] std::shared_ptr<const crimson::shared::EditorFrame>
  frame(std::uint8_t layer_mask, const crimson::shared::Matrix4 &clip_from_view,
        crimson::shared::Rect crop, crimson::shared::Extent backing,
        std::shared_ptr<const crimson::shared::EditorGrid> grid = {},
        bool overlays_visible = true, std::uint64_t scene_generation = 0u,
        bool scene_visible = true);
  [[nodiscard]] CrimsonEditorProjectionCounters counters() const;

  [[nodiscard]] ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t index, std::uint8_t mask, ViewportOverlayCamera camera) override;
  void submit_persistent_overlay(std::size_t index, std::uint8_t mask,
      ViewportOverlayFrame frame, ViewportOverlayCamera camera) override;
  void submit_dynamic_overlay(std::size_t index, std::uint8_t mask,
      ViewportOverlayFrame frame, ViewportOverlayCamera camera) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
