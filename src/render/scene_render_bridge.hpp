////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/curve_solid_geometry_cache.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/material_binding_service.hpp"
#include "render/instance_renderable_cache.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/scene_render_sync_service.hpp"
#include "render/texture_binding_service.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/collision_shape_overlay_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/selected_object_overlay_cache.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Scene.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;
class ViewportSettings;

/// Aggregates render-resource sharing and explicit-batch diagnostic counters.
struct SceneRenderResourceDiagnostics final {
  std::size_t device_instance_limit = 1u;
  std::size_t unique_compiled_payload_count = 0u;
  std::uint64_t unique_compiled_payload_bytes = 0u;
  std::size_t gpu_geometry_resource_count = 0u;
  std::uint64_t gpu_geometry_upload_count = 0u;
  std::uint64_t gpu_geometry_reuse_count = 0u;
  std::uint64_t gpu_uploaded_vertex_bytes = 0u;
  std::uint64_t gpu_uploaded_index_bytes = 0u;
  std::size_t base_instance_participant_count = 0u;
  std::size_t base_instance_batch_chunk_count = 0u;
  std::size_t wire_participant_count = 0u;
  std::size_t wire_batch_chunk_count = 0u;
  std::size_t selection_fill_batch_chunk_count = 0u;
  std::size_t selection_wire_batch_chunk_count = 0u;
  std::size_t standalone_primitive_submissions = 0u;
  std::size_t planned_primitive_submissions = 0u;
};

/**
 * Owns Scene Render Bridge behavior within the render extraction and Filament resource layer.
 */
class SceneRenderBridge : private ViewportOverlaySubmissionTarget {
public:
    SceneRenderBridge() = default;
    SceneRenderBridge(const SceneRenderBridge&) = delete;
    SceneRenderBridge& operator=(const SceneRenderBridge&) = delete;
    SceneRenderBridge(SceneRenderBridge&&) = delete;
    SceneRenderBridge& operator=(SceneRenderBridge&&) = delete;
    ~SceneRenderBridge();

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    filament::Material &shaded_mesh_material,
                    filament::Material &rendered_mesh_material,
                    filament::Material &overlay_material,
                    filament::Material &instance_selection_fill_material,
                    filament::Material &overlay_line_material,
                    filament::Material &selected_object_line_material,
                    filament::Material &edit_wire_line_material,
                    filament::Material &overlay_point_material,
                    filament::Material &overlay_icon_material,
                    std::filesystem::path resource_root);
    void shutdown();
    void set_project_material_resolver(
        const RenderProjectMaterialResolver *resolver);
    [[nodiscard]] const RenderProjectMaterialResolver *
    project_material_resolver() const {
      return project_material_resolver_;
    }
    void bind_scene(RenderSceneBindingKey scene_binding);
    void sync(const SceneDocument &document, const AssetRegistry &assets,
              std::uint64_t frame_index = 0,
              RenderSceneBindingKey scene_binding = {},
              RenderInstanceEditScope instance_edit_scope = {},
              const ViewportLocalVisibilitySnapshot *local_visibility =
                  nullptr);
    void sync(RenderSceneSnapshot snapshot);
    void update_mesh_surface_grid(float grid_world_spacing,
                                  const ViewportSettings &settings);
    void sync_scene_asset_overlays(
        const RenderOverlaySelectedObjectSnapshot &selected_object_snapshot,
        const RenderOverlaySceneWireframeSnapshot &wireframe_snapshot,
        const RenderOverlayCollisionSnapshot &collision_snapshot = {});
    /**
     * Pins and derives one compound preview with this bridge's retained render
     * caches, including transient Curve solid geometry.
     */
    [[nodiscard]] RenderToolPreviewSceneSnapshot
    build_tool_preview_scene_snapshot(
        ToolPreviewScene preview, const AssetRegistry &assets,
        const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);
    [[nodiscard]] ToolPreviewSubmissionResult
    set_tool_preview_meshes(std::vector<RenderToolPreviewMeshRecord> meshes);
    [[nodiscard]] ToolPreviewSubmissionResult set_tool_preview_scene(
        RenderToolPreviewSceneSnapshot preview);
    [[nodiscard]] const RenderToolPreviewSuppressionSnapshot &
    tool_preview_suppression() const noexcept {
      return tool_preview_suppression_;
    }
    [[nodiscard]] std::uint64_t tool_preview_submission_revision() const {
      return tool_preview_submission_revision_;
    }
    [[nodiscard]] bool scene_asset_overlay_sync_pending() const {
      return tool_preview_overlay_suppression_dirty_;
    }
    [[nodiscard]] SceneRenderResourceDiagnostics diagnostics() const;
    /** Drains one retired preview proxy during an explicitly idle UI slice. */
    void perform_idle_resource_maintenance();
    /// Restricts transient overlay work to currently visible workspace panes;
    /// slots leaving the layout are released and lazily resubmitted if shown.
    void set_active_overlay_viewports(std::uint8_t viewport_mask);
    [[nodiscard]] ViewportOverlaySubmissionResult submit_viewport_overlay(
        ViewportOverlaySubmissionRequest request,
        const ViewportSettings &settings);

  private:
    [[nodiscard]] ViewportOverlayCameraUpdate
    update_overlay_cameras(
        std::size_t viewport_index, std::uint8_t layer_mask,
        ViewportOverlayCamera camera) override;
    void submit_persistent_overlay(
        std::size_t viewport_index, std::uint8_t layer_mask,
        ViewportOverlayFrame frame, ViewportOverlayCamera camera) override;
    void submit_dynamic_overlay(
        std::size_t viewport_index, std::uint8_t layer_mask,
        ViewportOverlayFrame frame, ViewportOverlayCamera camera) override;

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureBindingService texture_binding_;
    MaterialBindingService material_binding_;
    MeshRenderableFactory mesh_factory_;
    MeshRenderableCache mesh_cache_;
    InstanceRenderableCache instance_cache_;
    ToolPreviewRenderableCache tool_preview_cache_;
    ToolPreviewLightCache tool_preview_light_cache_;
    OverlayProxyFactory overlay_proxy_factory_;
    OverlayRenderableCache overlay_cache_;
    OverlayRenderableCache dynamic_overlay_cache_;
    ViewportOverlaySubmissionCoordinator overlay_submission_coordinator_;
    InstanceSelectionOverlayCache instance_selection_overlay_cache_;
    CollisionShapeOverlayCache collision_shape_overlay_cache_;
    SceneWireframeOverlayCache scene_wireframe_overlay_cache_;
    SelectedObjectOverlayCache selected_object_overlay_cache_;
    SceneRenderSyncService sync_service_;
    CurveSolidGeometryCache curve_solid_geometry_cache_;
    RenderSceneSnapshot previous_scene_snapshot_;
    bool has_previous_scene_snapshot_ = false;
    RenderSceneBindingKey scene_binding_;
    const RenderProjectMaterialResolver *project_material_resolver_ = nullptr;
    bool has_scene_binding_ = false;
    std::uint64_t tool_preview_submission_revision_ = 0;
    std::uint64_t tool_preview_suppression_signature_ = 0;
    bool tool_preview_suppression_valid_ = false;
    std::unordered_set<EntityId> tool_preview_suppressed_entities_;
    std::vector<RenderInstancePreviewSuppression>
        tool_preview_occurrence_suppressions_;
    RenderToolPreviewSuppressionSnapshot tool_preview_suppression_;
    ViewportOverlayFrame tool_preview_overlay_frame_;
    std::uint64_t tool_preview_overlay_revision_ = 0u;
    RenderOverlaySelectionSnapshot filtered_scene_overlay_snapshot_;
    RenderOverlayCollisionSnapshot filtered_collision_snapshot_;
    RenderSourceStamp filtered_collision_source_stamp_;
    bool filtered_collision_valid_ = false;
    std::uint64_t filtered_scene_overlay_signature_ = 0u;
    bool filtered_scene_overlay_valid_ = false;
    bool tool_preview_overlay_suppression_dirty_ = false;
    std::uint8_t active_overlay_viewport_mask_ =
        static_cast<std::uint8_t>(
            (1u << OverlayRenderableCache::kMaxViewportOverlaySlots) - 1u);
};

} // namespace quader
