////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/MaterialBindingService.h"

#include "render/TextureBindingService.h"

#include <filament/TextureSampler.h>
#include <math/vec4.h>

#include <algorithm>
#include <bit>
#include <functional>
#include <string>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr float kMeshTextureAnisotropy = 8.0f;

std::uint64_t hashCombine(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= kFnvPrime;
    return hash;
}

} // namespace

void MaterialBindingService::initialize(filament::Material& meshMaterial, TextureBindingService& textureBinding) {
    meshMaterial_ = &meshMaterial;
    textureBinding_ = &textureBinding;
}

void MaterialBindingService::shutdown() {
    meshMaterial_ = nullptr;
    textureBinding_ = nullptr;
}

std::uint64_t MaterialBindingService::materialRevision(const AssetRegistry& assets, const MaterialDocument* material) const {
    if (!material) {
        return 0;
    }

    std::uint64_t hash = hashCombine(kFnvOffset, material->revision.value);
    std::vector<std::string> textureSlots;
    textureSlots.reserve(material->textureSlots.size());
    for (const auto& [slot, texture] : material->textureSlots) {
        textureSlots.push_back(slot);
    }
    std::sort(textureSlots.begin(), textureSlots.end());
    for (const std::string& slot : textureSlots) {
        const AssetId textureId = material->textureSlots.at(slot);
        hash = hashCombine(hash, std::hash<std::string>{}(slot));
        hash = hashCombine(hash, textureId);
        hash = hashCombine(hash, assets.textureRevision(textureId).value);
    }
    return hash;
}

filament::MaterialInstance* MaterialBindingService::createMeshMaterialInstance(
        const AssetRegistry& assets,
        const MaterialDocument* material,
        bool doubleSided) const {
    if (!meshMaterial_ || !textureBinding_) {
        return nullptr;
    }

    filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::REPEAT);
    sampler.setAnisotropy(kMeshTextureAnisotropy);

    filament::MaterialInstance* materialInstance = meshMaterial_->createInstance();
    materialInstance->setCullingMode(doubleSided
                    ? filament::MaterialInstance::CullingMode::NONE
                    : filament::MaterialInstance::CullingMode::BACK);
    materialInstance->setParameter(MaterialTextureSlot::BaseColor,
            textureBinding_->resolveTexture(assets, material, MaterialTextureSlot::BaseColor, textureBinding_->fallbackAlbedoTexture()),
            sampler);
    materialInstance->setParameter(MaterialTextureSlot::Roughness,
            textureBinding_->resolveTexture(assets, material, MaterialTextureSlot::Roughness, textureBinding_->fallbackRoughnessTexture()),
            sampler);
    const Vec3 baseColor = material ? material->baseColor : Vec3{1.0f, 1.0f, 1.0f};
    materialInstance->setParameter("baseColorFactor", filament::math::float4{baseColor.x, baseColor.y, baseColor.z, 1.0f});
    materialInstance->setParameter("metallicFactor", material ? material->metallic : 0.0f);
    materialInstance->setParameter("roughnessFactor", material ? material->roughness : 1.0f);
    return materialInstance;
}

} // namespace quader
