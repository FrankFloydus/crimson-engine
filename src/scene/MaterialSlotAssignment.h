#pragma once
#include "resources/AssetRef.h"
#include <cstdint>

namespace crimson {
struct MaterialAsset;

// A transient viewport target or an authored per-entity assignment. The slot is
// the imported primitive's material index, never an editor selection-root ID.
struct MaterialSlotAssignment {
    std::uint64_t entityId{};
    std::uint32_t slot{};
    AssetRef<MaterialAsset> materialId;
    bool operator==(const MaterialSlotAssignment&) const = default;
};
}
