////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"

#include <array>
#include <utility>

namespace quader {
namespace {

constexpr std::array<MaterialTextureSlotDescriptor, 2> kMaterialTextureSlotDescriptors{{
        {
                .id = MaterialTextureSlot::BaseColor,
                .legacyAlias = "baseColor",
        },
        {
                .id = MaterialTextureSlot::Roughness,
                .legacyAlias = "roughness",
        },
}};

bool matchesSlot(std::string_view slot, const MaterialTextureSlotDescriptor& descriptor) {
    return slot == descriptor.id ||
            (!std::string_view(descriptor.legacyAlias).empty() && slot == descriptor.legacyAlias);
}

} // namespace

std::span<const MaterialTextureSlotDescriptor> materialTextureSlotDescriptors() {
    return kMaterialTextureSlotDescriptors;
}

const MaterialTextureSlotDescriptor* materialTextureSlotDescriptor(std::string_view slot) {
    for (const MaterialTextureSlotDescriptor& descriptor : kMaterialTextureSlotDescriptors) {
        if (matchesSlot(slot, descriptor)) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::optional<std::string> canonicalMaterialTextureSlot(std::string_view slot) {
    const MaterialTextureSlotDescriptor* descriptor = materialTextureSlotDescriptor(slot);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    return std::string(descriptor->id);
}

bool materialTextureSlotSupported(std::string_view slot) {
    return materialTextureSlotDescriptor(slot) != nullptr;
}

void normalizeMaterialTextureSlots(MaterialDocument& material) {
    std::unordered_map<std::string, AssetId> normalized;
    for (const MaterialTextureSlotDescriptor& descriptor : kMaterialTextureSlotDescriptors) {
        if (const auto exact = material.textureSlots.find(descriptor.id); exact != material.textureSlots.end()) {
            normalized.emplace(descriptor.id, exact->second);
            continue;
        }
        if (std::string_view(descriptor.legacyAlias).empty()) {
            continue;
        }
        if (const auto legacy = material.textureSlots.find(descriptor.legacyAlias);
                legacy != material.textureSlots.end()) {
            normalized.emplace(descriptor.id, legacy->second);
        }
    }
    material.textureSlots = std::move(normalized);
}

std::optional<AssetId> materialTextureSlotAsset(const MaterialDocument& material, std::string_view slot) {
    const MaterialTextureSlotDescriptor* descriptor = materialTextureSlotDescriptor(slot);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    if (const auto exact = material.textureSlots.find(descriptor->id); exact != material.textureSlots.end()) {
        return exact->second;
    }
    if (std::string_view(descriptor->legacyAlias).empty()) {
        return std::nullopt;
    }
    if (const auto legacy = material.textureSlots.find(descriptor->legacyAlias);
            legacy != material.textureSlots.end()) {
        return legacy->second;
    }
    return std::nullopt;
}

bool setMaterialTextureSlot(MaterialDocument& material, std::string_view slot, AssetId texture) {
    const std::optional<std::string> canonical = canonicalMaterialTextureSlot(slot);
    if (!canonical.has_value()) {
        return false;
    }
    material.textureSlots[*canonical] = texture;
    normalizeMaterialTextureSlots(material);
    return true;
}

std::vector<std::string> unsupportedMaterialTextureSlots(const MaterialDocument& material) {
    std::vector<std::string> slots;
    for (const auto& [slot, texture] : material.textureSlots) {
        (void)texture;
        if (!materialTextureSlotSupported(slot)) {
            slots.push_back(slot);
        }
    }
    return slots;
}

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
    setMaterialTextureSlot(material, MaterialTextureSlot::BaseColor, baseColorTexture);
    setMaterialTextureSlot(material, MaterialTextureSlot::Roughness, roughnessTexture);
    return material;
}

} // namespace quader
