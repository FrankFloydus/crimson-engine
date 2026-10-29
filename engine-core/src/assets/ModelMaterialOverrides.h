#pragma once
#include "resources/AssetRef.h"
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace crimson {
struct MaterialAsset;
// Numbered source material slots. An absent/empty entry inherits that slot's
// source material; instance assignment never changes shared model geometry.
using ModelMaterialOverrides = std::vector<AssetRef<MaterialAsset>>;
inline constexpr std::size_t MaximumModelMaterialSlots = 100001;
inline void trimModelMaterialOverrides(ModelMaterialOverrides& materials) {
    while(!materials.empty() && materials.back().empty())materials.pop_back();
}
inline void setModelMaterialOverride(ModelMaterialOverrides& materials,std::size_t slot,AssetRef<MaterialAsset> material) {
    if(slot>=MaximumModelMaterialSlots)throw std::runtime_error("Model material slot exceeds the supported limit.");
    if(slot>=materials.size()) {
        if(material.empty())return;
        materials.resize(slot+1);
    }
    materials[slot]=std::move(material);trimModelMaterialOverrides(materials);
}
}
