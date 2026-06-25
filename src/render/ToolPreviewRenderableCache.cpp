////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ToolPreviewRenderableCache.h"

#include "render/MaterialBindingService.h"
#include "render/RenderSignatureBuilder.h"

namespace quader {

ToolPreviewRenderableCache::~ToolPreviewRenderableCache() {
    shutdown();
}

void ToolPreviewRenderableCache::initialize(MeshRenderableFactory& factory, MaterialBindingService& materialBinding) {
    factory_ = &factory;
    materialBinding_ = &materialBinding;
}

void ToolPreviewRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    clear();
    factory_ = nullptr;
    materialBinding_ = nullptr;
}

bool ToolPreviewRenderableCache::setMeshes(const std::vector<ToolPreviewMesh>& meshes, const AssetRegistry& assets) {
    if (!factory_ || !materialBinding_) {
        return false;
    }

    const std::uint64_t nextSignature = previewMeshesSignature(meshes, assets, *materialBinding_);
    if (nextSignature == meshSignature_) {
        return false;
    }

    clear();
    meshSignature_ = nextSignature;
    meshProxies_.reserve(meshes.size());
    for (const ToolPreviewMesh& previewMesh : meshes) {
        if (previewMesh.mesh.vertices.empty() || previewMesh.mesh.indices.empty()) {
            continue;
        }
        const MaterialDocument* material = assets.material(previewMesh.material);
        meshProxies_.push_back(factory_->create(
                previewMesh.sourceEntity,
                InvalidAssetId,
                previewMesh.mesh,
                previewMesh.material,
                material,
                assets,
                previewMesh.transform,
                false,
                true,
                materialBinding_->materialRevision(assets, material)));
    }
    return true;
}

void ToolPreviewRenderableCache::clear() {
    if (factory_) {
        for (MeshRenderableProxy& proxy : meshProxies_) {
            factory_->destroy(proxy);
        }
    }
    meshProxies_.clear();
    meshSignature_ = 0;
}

std::uint64_t ToolPreviewRenderableCache::previewMeshSignature(
        const ToolPreviewMesh& previewMesh,
        const AssetRegistry& assets,
        const MaterialBindingService& materialBinding) {
    const MaterialDocument* material = assets.material(previewMesh.material);
    RenderSignatureBuilder signature;
    signature.addValue(previewMesh.sourceEntity)
            .addValue(previewMesh.material)
            .addValue(materialBinding.materialRevision(assets, material))
            .addMat4(previewMesh.transform);
    return MeshRenderableFactory::meshAssetSignature(signature.value(), previewMesh.mesh);
}

std::uint64_t ToolPreviewRenderableCache::previewMeshesSignature(
        const std::vector<ToolPreviewMesh>& previewMeshes,
        const AssetRegistry& assets,
        const MaterialBindingService& materialBinding) {
    RenderSignatureBuilder signature;
    signature.addSize(previewMeshes.size());
    for (const ToolPreviewMesh& previewMesh : previewMeshes) {
        signature.addValue(previewMeshSignature(previewMesh, assets, materialBinding));
    }
    return signature.value();
}

} // namespace quader
