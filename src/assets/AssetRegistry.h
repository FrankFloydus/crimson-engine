////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/MaterialDocument.h"
#include "assets/MeshAsset.h"
#include "assets/TextureAsset.h"
#include "assets/TypedAssetStore.h"
#include "core/Types.h"

#include <cstdint>
#include <functional>

namespace quader {

using MeshAssetStore = TypedAssetStore<MeshAsset, MeshAssetRevision>;
using MaterialAssetStore = TypedAssetStore<MaterialDocument, MaterialAssetRevision>;
using TextureAssetStore = TypedAssetStore<TextureAsset, TextureAssetRevision>;

/**
 * Owns Asset Registry lookup and registration for the asset document and registry layer.
 */
class AssetRegistry {
public:
    AssetId addMesh(MeshAsset mesh);
    AssetId addMesh(AssetId id, MeshAsset mesh);
    AssetId addMaterial(MaterialDocument material);
    AssetId addMaterial(AssetId id, MaterialDocument material);
    AssetId addTexture(TextureAsset texture);
    AssetId addTexture(AssetId id, TextureAsset texture);
    bool replaceMesh(AssetId id, MeshAsset mesh);
    bool replaceMaterial(AssetId id, MaterialDocument material);
    bool replaceTexture(AssetId id, TextureAsset texture);
    bool updateMesh(AssetId id, const std::function<void(MeshAsset&)>& update);
    bool updateMaterial(AssetId id, const std::function<void(MaterialDocument&)>& update);
    bool updateTexture(AssetId id, const std::function<void(TextureAsset&)>& update);

    MeshAsset* mesh(AssetId id);
    MaterialDocument* material(AssetId id);
    TextureAsset* texture(AssetId id);

    const MeshAsset* mesh(AssetId id) const;
    const MaterialDocument* material(AssetId id) const;
    const TextureAsset* texture(AssetId id) const;
    MeshAssetRevision meshRevision(AssetId id) const;
    MaterialAssetRevision materialRevision(AssetId id) const;
    TextureAssetRevision textureRevision(AssetId id) const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t materialCount() const { return materials_.size(); }
    std::size_t textureCount() const { return textures_.size(); }
    std::uint64_t revision() const { return revision_; }

private:
    void markDirty();

    AssetId nextId_ = 1;
    std::uint64_t revision_ = 0;
    MeshAssetStore meshes_;
    MaterialAssetStore materials_;
    TextureAssetStore textures_;
};

} // namespace quader
