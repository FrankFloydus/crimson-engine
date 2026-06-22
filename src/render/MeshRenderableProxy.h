////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"

#include <filament/IndexBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Represents a Mesh Renderable Proxy value used by the render extraction and Filament resource layer.
 */
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
