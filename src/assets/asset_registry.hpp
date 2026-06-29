////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "assets/typed_asset_store.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

using MeshAssetStore = TypedAssetStore<MeshAsset, MeshAssetRevision>;
using MaterialAssetStore = TypedAssetStore<MaterialDocument, MaterialAssetRevision>;
using TextureAssetStore = TypedAssetStore<TextureAsset, TextureAssetRevision>;

/**
 * Stores asset-registry validation status for caller-facing asset mutations.
 */
struct AssetRegistryValidationResult {
    bool ok = true;
    std::string message;
};

/**
 * Stores asset-registry mutation status for owner-level asset edits.
 */
struct AssetRegistryMutationResult {
    bool ok = true;
    bool changed = false;
    std::string message;
};

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
    [[nodiscard]] AssetRegistryMutationResult setMaterialTexture(
            AssetId material,
            std::string_view slot,
            AssetId texture);
    [[nodiscard]] AssetRegistryValidationResult validateMaterial(const MaterialDocument& material) const;

    const MeshAsset* mesh(AssetId id) const;
    const MaterialDocument* material(AssetId id) const;
    const TextureAsset* texture(AssetId id) const;
    MeshAssetRevision meshRevision(AssetId id) const;
    MaterialAssetRevision materialRevision(AssetId id) const;
    TextureAssetRevision textureRevision(AssetId id) const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t materialCount() const { return materials_.size(); }
    std::size_t textureCount() const { return textures_.size(); }
    std::vector<AssetId> meshIds() const { return meshes_.ids(); }
    std::vector<AssetId> materialIds() const { return materials_.ids(); }
    std::vector<AssetId> textureIds() const { return textures_.ids(); }
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
