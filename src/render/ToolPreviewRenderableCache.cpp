////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ToolPreviewRenderableCache.h"

#include "render/MaterialBindingService.h"

#include <bit>

namespace quader {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t hashCombine(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= kFnvPrime;
    return hash;
}

std::uint64_t hashFloat(std::uint64_t hash, float value) {
    return hashCombine(hash, std::bit_cast<std::uint32_t>(value));
}

} // namespace

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
    std::uint64_t hash = hashCombine(kFnvOffset, previewMesh.sourceEntity);
    hash = hashCombine(hash, previewMesh.material);
    hash = hashCombine(hash, materialBinding.materialRevision(assets, material));
    for (const float value : previewMesh.transform.values) {
        hash = hashFloat(hash, value);
    }
    return MeshRenderableFactory::meshAssetSignature(hash, previewMesh.mesh);
}

std::uint64_t ToolPreviewRenderableCache::previewMeshesSignature(
        const std::vector<ToolPreviewMesh>& previewMeshes,
        const AssetRegistry& assets,
        const MaterialBindingService& materialBinding) {
    std::uint64_t hash = hashCombine(kFnvOffset, previewMeshes.size());
    for (const ToolPreviewMesh& previewMesh : previewMeshes) {
        hash = hashCombine(hash, previewMeshSignature(previewMesh, assets, materialBinding));
    }
    return hash;
}

} // namespace quader
