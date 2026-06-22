#pragma once

#include "assets/AssetRegistry.h"
#include "assets/MaterialDocument.h"

#include <filament/Material.h>
#include <filament/MaterialInstance.h>

#include <cstdint>

namespace quader {

class TextureBindingService;

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
