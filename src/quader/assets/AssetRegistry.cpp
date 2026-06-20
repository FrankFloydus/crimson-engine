#include "quader/assets/AssetRegistry.h"

#include <utility>

namespace quader {

AssetId AssetRegistry::addMesh(MeshAsset mesh) {
    const AssetId id = nextId_++;
    mesh.id = id;
    nextMeshRevision_ = nextRevision(nextMeshRevision_);
    mesh.revision = nextMeshRevision_;
    meshes_.emplace(id, std::move(mesh));
    markDirty();
    return id;
}

AssetId AssetRegistry::addMaterial(MaterialDocument material) {
    const AssetId id = nextId_++;
    material.id = id;
    nextMaterialRevision_ = nextRevision(nextMaterialRevision_);
    material.revision = nextMaterialRevision_;
    materials_.emplace(id, std::move(material));
    markDirty();
    return id;
}

AssetId AssetRegistry::addTexture(TextureAsset texture) {
    const AssetId id = nextId_++;
    texture.id = id;
    nextTextureRevision_ = nextRevision(nextTextureRevision_);
    texture.revision = nextTextureRevision_;
    textures_.emplace(id, std::move(texture));
    markDirty();
    return id;
}

bool AssetRegistry::replaceMesh(AssetId id, MeshAsset mesh) {
    auto it = meshes_.find(id);
    if (it == meshes_.end()) {
        return false;
    }
    mesh.id = id;
    nextMeshRevision_ = nextRevision(nextMeshRevision_);
    mesh.revision = nextMeshRevision_;
    it->second = std::move(mesh);
    markDirty();
    return true;
}

bool AssetRegistry::replaceMaterial(AssetId id, MaterialDocument material) {
    auto it = materials_.find(id);
    if (it == materials_.end()) {
        return false;
    }
    material.id = id;
    nextMaterialRevision_ = nextRevision(nextMaterialRevision_);
    material.revision = nextMaterialRevision_;
    it->second = std::move(material);
    markDirty();
    return true;
}

bool AssetRegistry::replaceTexture(AssetId id, TextureAsset texture) {
    auto it = textures_.find(id);
    if (it == textures_.end()) {
        return false;
    }
    texture.id = id;
    nextTextureRevision_ = nextRevision(nextTextureRevision_);
    texture.revision = nextTextureRevision_;
    it->second = std::move(texture);
    markDirty();
    return true;
}

bool AssetRegistry::updateMesh(AssetId id, const std::function<void(MeshAsset&)>& update) {
    auto it = meshes_.find(id);
    if (it == meshes_.end()) {
        return false;
    }
    update(it->second);
    nextMeshRevision_ = nextRevision(nextMeshRevision_);
    it->second.id = id;
    it->second.revision = nextMeshRevision_;
    markDirty();
    return true;
}

bool AssetRegistry::updateMaterial(AssetId id, const std::function<void(MaterialDocument&)>& update) {
    auto it = materials_.find(id);
    if (it == materials_.end()) {
        return false;
    }
    update(it->second);
    nextMaterialRevision_ = nextRevision(nextMaterialRevision_);
    it->second.id = id;
    it->second.revision = nextMaterialRevision_;
    markDirty();
    return true;
}

bool AssetRegistry::updateTexture(AssetId id, const std::function<void(TextureAsset&)>& update) {
    auto it = textures_.find(id);
    if (it == textures_.end()) {
        return false;
    }
    update(it->second);
    nextTextureRevision_ = nextRevision(nextTextureRevision_);
    it->second.id = id;
    it->second.revision = nextTextureRevision_;
    markDirty();
    return true;
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

MeshAssetRevision AssetRegistry::meshRevision(AssetId id) const {
    const MeshAsset* asset = mesh(id);
    return asset ? asset->revision : MeshAssetRevision{};
}

MaterialAssetRevision AssetRegistry::materialRevision(AssetId id) const {
    const MaterialDocument* asset = material(id);
    return asset ? asset->revision : MaterialAssetRevision{};
}

TextureAssetRevision AssetRegistry::textureRevision(AssetId id) const {
    const TextureAsset* asset = texture(id);
    return asset ? asset->revision : TextureAssetRevision{};
}

void AssetRegistry::markDirty() {
    ++revision_;
}

} // namespace quader
