#pragma once

#include "quader/assets/MaterialDocument.h"
#include "quader/assets/MeshAsset.h"
#include "quader/assets/TextureAsset.h"
#include "quader/core/Types.h"

#include <optional>
#include <unordered_map>

namespace quader {

class AssetRegistry {
public:
    AssetId addMesh(MeshAsset mesh);
    AssetId addMaterial(MaterialDocument material);
    AssetId addTexture(TextureAsset texture);

    MeshAsset* mesh(AssetId id);
    MaterialDocument* material(AssetId id);
    TextureAsset* texture(AssetId id);

    const MeshAsset* mesh(AssetId id) const;
    const MaterialDocument* material(AssetId id) const;
    const TextureAsset* texture(AssetId id) const;

    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t materialCount() const { return materials_.size(); }
    std::size_t textureCount() const { return textures_.size(); }

private:
    AssetId nextId_ = 1;
    std::unordered_map<AssetId, MeshAsset> meshes_;
    std::unordered_map<AssetId, MaterialDocument> materials_;
    std::unordered_map<AssetId, TextureAsset> textures_;
};

} // namespace quader
