////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "scene/components.hpp"

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
    MaterialBindingService() = default;
    MaterialBindingService(const MaterialBindingService&) = delete;
    MaterialBindingService& operator=(const MaterialBindingService&) = delete;
    MaterialBindingService(MaterialBindingService&&) = delete;
    MaterialBindingService& operator=(MaterialBindingService&&) = delete;

    void initialize(
            filament::Material& shadedMeshMaterial,
            filament::Material& renderedMeshMaterial,
            TextureBindingService& textureBinding);
    void shutdown();

    bool initialized() const {
        return shadedMeshMaterial_ != nullptr && renderedMeshMaterial_ != nullptr && textureBinding_ != nullptr;
    }

    std::uint64_t materialRevision(const AssetRegistry& assets, const MaterialComponent* material) const;
    filament::MaterialInstance* createMeshMaterialInstance(
            const AssetRegistry& assets,
            const MaterialDocument* material,
            bool doubleSided,
            bool editorUnlit = false) const;

private:
    filament::Material* shadedMeshMaterial_ = nullptr;
    filament::Material* renderedMeshMaterial_ = nullptr;
    TextureBindingService* textureBinding_ = nullptr;
};

} // namespace quader
