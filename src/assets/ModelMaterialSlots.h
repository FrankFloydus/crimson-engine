#pragma once

#include "ModelAsset.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace crimson {
// Old importers exposed one unused trailing fallback. Its saved override has no
// geometry to affect; tolerate that exact entry without making it a real slot.
inline bool isLegacyUnusedMaterialSlot(const ModelAsset& model,std::size_t slot) {
    return model.legacyUnusedMaterialSlot && slot==*model.legacyUnusedMaterialSlot && slot==model.materials.size();
}

// Keep source slot indices: compacting the material table would remap saved overrides.
// Primitive metadata survives geometry streaming, including its material index.
inline std::vector<std::size_t> usedModelMaterialSlots(const ModelAsset& model) {
    std::vector<bool> used(model.materials.size());
    for(const auto& primitive:model.primitives)
        if(primitive.material<used.size())used[primitive.material]=true;
    std::vector<std::size_t> slots;
    slots.reserve(std::min(model.materials.size(),model.primitives.size()));
    for(std::size_t slot=0;slot<used.size();++slot)
        if(used[slot])slots.push_back(slot);
    return slots;
}
} // namespace crimson
