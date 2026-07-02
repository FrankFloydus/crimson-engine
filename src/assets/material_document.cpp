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

constexpr std::array<MaterialTextureSlotDescriptor, 2>
    kMaterialTextureSlotDescriptors{{
        {
            .id = material_texture_slot::kBaseColor,
            .legacy_alias = "baseColor",
        },
        {
            .id = material_texture_slot::kRoughness,
            .legacy_alias = "roughness",
        },
    }};

bool matches_slot(std::string_view slot,
                  const MaterialTextureSlotDescriptor &descriptor) {
  return slot == descriptor.id ||
         (!std::string_view(descriptor.legacy_alias).empty() &&
          slot == descriptor.legacy_alias);
}

} // namespace

std::span<const MaterialTextureSlotDescriptor>
material_texture_slot_descriptors() {
  return kMaterialTextureSlotDescriptors;
}

const MaterialTextureSlotDescriptor *
material_texture_slot_descriptor(std::string_view slot) {
  for (const MaterialTextureSlotDescriptor &descriptor :
       kMaterialTextureSlotDescriptors) {
    if (matches_slot(slot, descriptor)) {
      return &descriptor;
    }
  }
  return nullptr;
}

std::optional<std::string>
canonical_material_texture_slot(std::string_view slot) {
  const MaterialTextureSlotDescriptor *descriptor =
      material_texture_slot_descriptor(slot);
  if (descriptor == nullptr) {
    return std::nullopt;
  }
  return std::string(descriptor->id);
}

bool material_texture_slot_supported(std::string_view slot) {
  return material_texture_slot_descriptor(slot) != nullptr;
}

void normalize_material_texture_slots(MaterialDocument &material) {
  std::unordered_map<std::string, AssetId> normalized;
  for (const MaterialTextureSlotDescriptor &descriptor :
       kMaterialTextureSlotDescriptors) {
    if (const auto exact = material.texture_slots.find(descriptor.id);
        exact != material.texture_slots.end()) {
      normalized.emplace(descriptor.id, exact->second);
      continue;
    }
    if (std::string_view(descriptor.legacy_alias).empty()) {
      continue;
    }
    if (const auto legacy =
            material.texture_slots.find(descriptor.legacy_alias);
        legacy != material.texture_slots.end()) {
      normalized.emplace(descriptor.id, legacy->second);
    }
  }
  material.texture_slots = std::move(normalized);
}

std::optional<AssetId>
material_texture_slot_asset(const MaterialDocument &material,
                            std::string_view slot) {
  const MaterialTextureSlotDescriptor *descriptor =
      material_texture_slot_descriptor(slot);
  if (descriptor == nullptr) {
    return std::nullopt;
  }
  if (const auto exact = material.texture_slots.find(descriptor->id);
      exact != material.texture_slots.end()) {
    return exact->second;
  }
  if (std::string_view(descriptor->legacy_alias).empty()) {
    return std::nullopt;
  }
  if (const auto legacy = material.texture_slots.find(descriptor->legacy_alias);
      legacy != material.texture_slots.end()) {
    return legacy->second;
  }
  return std::nullopt;
}

bool set_material_texture_slot(MaterialDocument &material,
                               std::string_view slot, AssetId texture) {
  const std::optional<std::string> canonical =
      canonical_material_texture_slot(slot);
  if (!canonical.has_value()) {
    return false;
  }
  material.texture_slots[*canonical] = texture;
  normalize_material_texture_slots(material);
  return true;
}

std::vector<std::string>
unsupported_material_texture_slots(const MaterialDocument &material) {
  std::vector<std::string> slots;
  for (const auto &[slot, texture] : material.texture_slots) {
    (void)texture;
    if (!material_texture_slot_supported(slot)) {
      slots.push_back(slot);
    }
  }
  return slots;
}

MaterialDocument make_default_quader_material(std::string name) {
  MaterialDocument material;
  material.name = std::move(name);
  material.base_color = {1.0f, 1.0f, 1.0f};
  material.roughness = 1.0f;
  material.metallic = 0.0f;
  return material;
}

MaterialDocument make_default_quader_material(std::string name,
                                              AssetId base_color_texture,
                                              AssetId roughness_texture) {
  MaterialDocument material = make_default_quader_material(std::move(name));
  set_material_texture_slot(material, material_texture_slot::kBaseColor,
                            base_color_texture);
  set_material_texture_slot(material, material_texture_slot::kRoughness,
                            roughness_texture);
  return material;
}

} // namespace quader
