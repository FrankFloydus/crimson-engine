////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_persistent_overlay_style.hpp"
#include "viewport/material_preview_state.hpp"
#include "viewport/viewport_selection_depth.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader {

class ViewportCameraController;
class ViewportSettings;
struct SceneSettings;

/** Identifies whether final color encoding belongs to a texture or presentation target. */
enum class ViewportRenderSurface { Texture, Swapchain, SrgbSwapchain };

/** Selects a per-view material diagnostic without modifying the scene material. */
enum class ViewportMaterialDebugMode : std::uint8_t {
  None = 0, DiffuseOnly = 1, SpecularOnly = 2, Albedo = 3,
  Roughness = 4, Metalness = 5, Normal = 6, Uv = 7,
};

/** Measures bounded GPU certificate work separately from ordinary viewport submissions. */
struct ComponentSelectionRendererStats final {
  std::uint64_t submission_count = 0u;
  std::uint64_t completed_readbacks = 0u;
  std::uint64_t readback_bytes = 0u;
  double readback_latency_ms = 0.0;
};

/** Optional conservative GPU certificates; exact selection stays with its CPU owner. */
class RenderSelectionDepth {
public:
  virtual ~RenderSelectionDepth() = default;
  [[nodiscard]] virtual bool wants_geometry(
      const ViewportSelectionDepthCamera &camera, bool eligible) = 0;
  virtual void prepare(std::shared_ptr<const ViewportSelectionDepthGeometry> geometry,
                       const ViewportSelectionDepthCamera &camera) = 0;
  [[nodiscard]] virtual std::shared_ptr<const ViewportSelectionDepthSource> source() const = 0;
  [[nodiscard]] virtual ComponentSelectionRendererStats stats() const = 0;
};

/** One pane's retained presentation; it never owns document, camera or selection truth. */
class RenderView {
public:
  virtual ~RenderView() = default;
  virtual void set_active(bool active) { (void)active; }
  virtual void shutdown() = 0;
  virtual void resize(std::uint32_t width, std::uint32_t height) = 0;
  virtual void set_presentation_bounds(std::uint32_t backing_width,
      std::uint32_t backing_height, std::uint32_t crop_left, std::uint32_t crop_top) = 0;
  virtual void update_camera(const ViewportCameraController &camera) = 0;
  virtual void apply_scene_settings(const SceneSettings &settings, bool rendered,
                                    const ViewportSettings &viewport_settings) = 0;
  virtual void update_ground_grid(const ViewportCameraController &camera,
      float spacing, const ViewportSettings &settings, bool visible = true) = 0;
  virtual void set_instance_edit_context_active(bool active, const ViewportSettings &settings) = 0;
  virtual void set_overlay_visible(bool visible) = 0;
  virtual void set_scene_visible(bool visible) = 0;
  virtual void set_scene_layer_mask(std::uint8_t mask) = 0;
  virtual void set_shadowing_enabled(bool enabled) = 0;
  virtual void set_material_presentation(ViewportMaterialDebugMode mode,
                                       bool solid, bool mesh_grid_visible = true) = 0;
  virtual bool update_selected_mesh_outlines(
      const RenderOverlaySelectedObjectSnapshot *snapshot,
      const RenderOverlaySelectionSnapshot *hierarchy,
      const ViewportPersistentOverlayStyleSnapshot &style, bool visible) = 0;
  [[nodiscard]] virtual RenderSelectionDepth &component_selection() = 0;
  [[nodiscard]] virtual std::uint32_t width() const = 0;
  [[nodiscard]] virtual std::uint32_t height() const = 0;
  [[nodiscard]] virtual std::uint64_t scene_settings_apply_count() const = 0;
};

using MaterialPreviewTextureSlot = RenderStandardMaterialTextureSlot;
using MaterialPreviewTextureSnapshot = RenderMaterialTextureEvaluationSnapshot;
using StandardMaterialPreviewSnapshot = RenderMaterialEvaluationSnapshot;

/** Optional isolated material presentation supplied by the selected renderer. */
class RenderMaterialPreviewView {
public:
  virtual ~RenderMaterialPreviewView() = default;
  virtual void shutdown() = 0;
  virtual void resize(std::uint32_t width, std::uint32_t height) = 0;
  virtual void set_preview_state(const MaterialPreviewState &state) = 0;
  virtual bool set_material(StandardMaterialPreviewSnapshot snapshot) = 0;
  [[nodiscard]] virtual bool initialized() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
};

inline constexpr std::uint32_t kMaterialThumbnailMaximumSize = 1024u;
inline constexpr std::uint32_t kMaterialThumbnailPresetRevision = 2u;

/** Immutable premultiplied RGBA8 bytes published after asynchronous GPU completion. */
struct MaterialThumbnailReadback final {
  std::uint32_t size = 0u;
  std::vector<std::byte> rgba8;
  std::atomic<bool> complete{false};
  [[nodiscard]] bool ready() const noexcept {
    return complete.load(std::memory_order_acquire);
  }
};

} // namespace quader
