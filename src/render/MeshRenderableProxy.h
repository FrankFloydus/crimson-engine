#pragma once

#include "core/Types.h"

#include <filament/IndexBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>
#include <vector>

namespace quader {

struct MeshRenderableProxy {
    utils::Entity entity;
    EntityId sourceEntity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
    std::uint64_t meshRevision = 0;
    std::uint64_t materialRevision = 0;
    bool castShadows = false;
    bool doubleSided = false;
    filament::VertexBuffer* vertexBuffer = nullptr;
    filament::IndexBuffer* indexBuffer = nullptr;
    std::vector<std::uint32_t> materialSlots;
    std::vector<filament::MaterialInstance*> materialInstances;
};

} // namespace quader
