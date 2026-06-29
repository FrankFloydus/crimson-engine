////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/render_layers.hpp"
#include "scene/scene_document.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class MaterialBindingService;

/**
 * Creates Mesh Renderable Factory resources for the render extraction and Filament resource layer.
 */
class MeshRenderableFactory {
public:
    MeshRenderableFactory() = default;
    MeshRenderableFactory(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory& operator=(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory(MeshRenderableFactory&&) = delete;
    MeshRenderableFactory& operator=(MeshRenderableFactory&&) = delete;

    void initialize(filament::Engine& engine, filament::Scene& scene, MaterialBindingService& materialBinding);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr && materialBinding_ != nullptr;
    }

    MeshRenderableProxy create(EntityId entityId,
            AssetId meshId,
            const MeshAsset& mesh,
            const MaterialComponent* material,
            const AssetRegistry& assets,
            const QMat4& worldTransform,
            bool castShadows,
            bool doubleSided,
            std::uint64_t materialRevision,
            std::uint8_t layerMask = kViewportRenderedSceneLayerMask,
            bool editorUnlit = false,
            bool depthOnly = false,
            std::uint8_t channel = kViewportDefaultRenderableChannel);
    void updateTransform(const MeshRenderableProxy& proxy, const QMat4& worldTransform);
    void destroy(MeshRenderableProxy& proxy);

    static std::uint64_t meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh);

private:
    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MaterialBindingService* materialBinding_ = nullptr;
};

} // namespace quader
