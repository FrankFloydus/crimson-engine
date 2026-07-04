////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/scene_render_sync_service.hpp"
#include "render/texture_binding_service.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/selected_object_overlay_cache.hpp"

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Scene.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace quader {

/**
 * Stores the viewport-pane presentation data needed for render-owned scene
 * overlay extraction.
 */
struct ViewportSceneOverlayFrameRequest {
    ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
    bool wireframe_pane = false;
    bool point_light_gizmos_visible = false;
};

/**
 * Owns Scene Render Bridge behavior within the render extraction and Filament resource layer.
 */
class SceneRenderBridge {
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
                    filament::Material &overlay_line_material,
                    filament::Material &selected_object_line_material,
                    filament::Material &overlay_point_material,
                    filament::Material &overlay_icon_material,
                    std::filesystem::path resource_root);
    void shutdown();
    void sync(const RenderSceneSnapshot &snapshot);
    void sync_scene_asset_overlays(
        const RenderOverlaySelectedObjectSnapshot &selected_object_snapshot,
        const RenderOverlaySceneWireframeSnapshot &wireframe_snapshot);
    [[nodiscard]] ViewportOverlayFrame build_scene_overlay_frame(
        ViewportOverlayFrame base_frame,
        const RenderOverlaySelectionSnapshot &snapshot,
        const ViewportSceneOverlayFrameRequest &request) const;
    void
    set_tool_preview_meshes(std::vector<RenderToolPreviewMeshRecord> meshes);
    void set_overlay_frame(ViewportOverlayFrame frame,
                           ViewportOverlayCamera camera);
    void set_overlay_frame(std::size_t viewport_index, std::uint8_t layer_mask,
                           ViewportOverlayFrame frame,
                           ViewportOverlayCamera camera);

  private:
    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureBindingService texture_binding_;
    MaterialBindingService material_binding_;
    MeshRenderableFactory mesh_factory_;
    MeshRenderableCache mesh_cache_;
    ToolPreviewRenderableCache tool_preview_cache_;
    OverlayProxyFactory overlay_proxy_factory_;
    OverlayRenderableCache overlay_cache_;
    SceneWireframeOverlayCache scene_wireframe_overlay_cache_;
    SelectedObjectOverlayCache selected_object_overlay_cache_;
    SceneRenderSyncService sync_service_;
};

} // namespace quader
