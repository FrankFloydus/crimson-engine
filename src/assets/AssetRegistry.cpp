#include "assets/AssetRegistry.h"

#include <utility>

namespace quader {

AssetId AssetRegistry::addMesh(MeshAsset mesh) {
    const AssetId id = nextId_++;
    meshes_.add(id, std::move(mesh));
    markDirty();
    return id;
}

AssetId AssetRegistry::addMaterial(MaterialDocument material) {
    const AssetId id = nextId_++;
    materials_.add(id, std::move(material));
    markDirty();
    return id;
}

AssetId AssetRegistry::addTexture(TextureAsset texture) {
    const AssetId id = nextId_++;
    textures_.add(id, std::move(texture));
    markDirty();
    return id;
}

bool AssetRegistry::replaceMesh(AssetId id, MeshAsset mesh) {
    if (!meshes_.replace(id, std::move(mesh))) {
        return false;
    }
    markDirty();
    return true;
}

bool AssetRegistry::replaceMaterial(AssetId id, MaterialDocument material) {
    if (!materials_.replace(id, std::move(material))) {
        return false;
    }
    markDirty();
    return true;
}

bool AssetRegistry::replaceTexture(AssetId id, TextureAsset texture) {
    if (!textures_.replace(id, std::move(texture))) {
        return false;
    }
    markDirty();
    return true;
}

bool AssetRegistry::updateMesh(AssetId id, const std::function<void(MeshAsset&)>& update) {
    if (!meshes_.update(id, update)) {
        return false;
    }
    markDirty();
    return true;
}

bool AssetRegistry::updateMaterial(AssetId id, const std::function<void(MaterialDocument&)>& update) {
    if (!materials_.update(id, update)) {
        return false;
    }
    markDirty();
    return true;
}

bool AssetRegistry::updateTexture(AssetId id, const std::function<void(TextureAsset&)>& update) {
    if (!textures_.update(id, update)) {
        return false;
    }
    markDirty();
    return true;
}

MeshAsset* AssetRegistry::mesh(AssetId id) {
    return meshes_.get(id);
}

MaterialDocument* AssetRegistry::material(AssetId id) {
    return materials_.get(id);
}

TextureAsset* AssetRegistry::texture(AssetId id) {
    return textures_.get(id);
}

const MeshAsset* AssetRegistry::mesh(AssetId id) const {
    return meshes_.get(id);
}

const MaterialDocument* AssetRegistry::material(AssetId id) const {
    return materials_.get(id);
}

const TextureAsset* AssetRegistry::texture(AssetId id) const {
    return textures_.get(id);
}

MeshAssetRevision AssetRegistry::meshRevision(AssetId id) const {
    return meshes_.revision(id);
}

MaterialAssetRevision AssetRegistry::materialRevision(AssetId id) const {
    return materials_.revision(id);
}

TextureAssetRevision AssetRegistry::textureRevision(AssetId id) const {
    return textures_.revision(id);
}

void AssetRegistry::markDirty() {
    ++revision_;
}

} // namespace quader
