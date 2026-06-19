#include "quader/assets/MaterialDocument.h"

#include <utility>

namespace quader {

MaterialDocument makeDefaultQuaderMaterial(std::string name) {
    MaterialDocument material;
    material.name = std::move(name);
    material.baseColor = {1.0f, 1.0f, 1.0f};
    material.roughness = 1.0f;
    material.metallic = 0.0f;
    return material;
}

MaterialDocument makeDefaultQuaderMaterial(std::string name, AssetId baseColorTexture, AssetId roughnessTexture) {
    MaterialDocument material = makeDefaultQuaderMaterial(std::move(name));
    material.textureSlots[MaterialTextureSlot::BaseColor] = baseColorTexture;
    material.textureSlots[MaterialTextureSlot::Roughness] = roughnessTexture;
    return material;
}

} // namespace quader
