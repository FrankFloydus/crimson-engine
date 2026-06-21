#pragma once

#include "assets/MaterialDocument.h"
#include "assets/MeshAsset.h"
#include "assets/TextureAsset.h"
#include "assets/TypedAssetStore.h"
#include "core/Types.h"

#include <cstdint>
#include <functional>

namespace quader {

class MeshAssetStore final : public TypedAssetStore<MeshAsset, MeshAssetRevision> {};
class MaterialAssetStore final : public TypedAssetStore<MaterialDocument, MaterialAssetRevision> {};
class TextureAssetStore final : public TypedAssetStore<TextureAsset, TextureAssetRevision> {};

class AssetRegistry {
public:
    AssetId addMesh(MeshAsset mesh);
    AssetId addMaterial(MaterialDocument material);
    AssetId addTexture(TextureAsset texture);
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
