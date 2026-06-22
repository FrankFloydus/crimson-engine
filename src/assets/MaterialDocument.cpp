////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/MaterialDocument.h"

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
