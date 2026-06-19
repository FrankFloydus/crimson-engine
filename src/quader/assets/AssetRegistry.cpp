#include "quader/assets/AssetRegistry.h"

namespace quader {

AssetId AssetRegistry::addMesh(MeshAsset mesh) {
    const AssetId id = nextId_++;
    mesh.id = id;
    meshes_.emplace(id, std::move(mesh));
    return id;
}

AssetId AssetRegistry::addMaterial(MaterialDocument material) {
    const AssetId id = nextId_++;
    material.id = id;
    materials_.emplace(id, std::move(material));
    return id;
}

AssetId AssetRegistry::addTexture(TextureAsset texture) {
    const AssetId id = nextId_++;
    texture.id = id;
    textures_.emplace(id, std::move(texture));
    return id;
}

MeshAsset* AssetRegistry::mesh(AssetId id) {
    auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

MaterialDocument* AssetRegistry::material(AssetId id) {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
}

TextureAsset* AssetRegistry::texture(AssetId id) {
    auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : &it->second;
}

const MeshAsset* AssetRegistry::mesh(AssetId id) const {
    auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

const MaterialDocument* AssetRegistry::material(AssetId id) const {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
}

const TextureAsset* AssetRegistry::texture(AssetId id) const {
    auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : &it->second;
}

} // namespace quader
