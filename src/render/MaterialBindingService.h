////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "assets/MaterialDocument.h"

#include <filament/Material.h>
#include <filament/MaterialInstance.h>

#include <cstdint>

namespace quader {

class TextureBindingService;

/**
 * Coordinates Material Binding Service responsibilities for the render extraction and Filament resource layer.
 */
class MaterialBindingService {
public:
    void initialize(filament::Material& meshMaterial, TextureBindingService& textureBinding);
    void shutdown();

    bool initialized() const {
        return meshMaterial_ != nullptr && textureBinding_ != nullptr;
    }

    std::uint64_t materialRevision(const AssetRegistry& assets, const MaterialDocument* material) const;
    filament::MaterialInstance* createMeshMaterialInstance(
            const AssetRegistry& assets,
            const MaterialDocument* material,
            bool doubleSided) const;

private:
    filament::Material* meshMaterial_ = nullptr;
    TextureBindingService* textureBinding_ = nullptr;
};

} // namespace quader
