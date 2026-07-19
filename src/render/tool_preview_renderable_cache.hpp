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

#include <cstdint>
#include <vector>

namespace quader {

class MaterialBindingService;
class ViewportSettings;

/**
 * Caches Tool Preview Renderable Cache derived state for the render extraction and Filament resource layer.
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

    bool set_meshes(const std::vector<RenderToolPreviewMeshRecord> &meshes);
    void update_shaded_lighting(const ViewportSettings &settings);
    void clear();

    std::uint64_t signature() const { return mesh_signature_; }

    [[nodiscard]] std::size_t proxy_count() const;

  private:
    /**
     * Owns one uploaded preview geometry plus transform-only shared instances.
     */
    struct PreviewGroup {
      std::uint64_t geometry_signature = 0;
      std::uint64_t topology_signature = 0;
      std::uint64_t layout_key = 0;
      std::vector<QMat4> transforms;
      bool dynamic_positions = false;
      MeshRenderableProxy prototype;
      std::vector<MeshRenderableProxy> instances;
      MeshRenderableProxy overlay_depth_prototype;
      std::vector<MeshRenderableProxy> overlay_depth_instances;
    };

    static std::uint64_t
    preview_geometry_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t preview_topology_signature(
        const RenderToolPreviewMeshRecord &preview_mesh);
    static std::uint64_t
    preview_meshes_signature(
        const std::vector<RenderToolPreviewMeshRecord> &preview_meshes);
    void set_preview_groups_visible(bool visible);

    MeshRenderableFactory* factory_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::vector<PreviewGroup> preview_groups_;
    std::uint64_t mesh_signature_ = 0;
    bool preview_groups_visible_ = true;
};

} // namespace quader
