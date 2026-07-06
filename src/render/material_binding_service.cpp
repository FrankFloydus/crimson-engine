////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/material_binding_service.hpp"

#include "core/color_space.hpp"
#include "render/render_signature_builder.hpp"
#include "render/texture_binding_service.hpp"
#include "viewport/viewport_settings.hpp"

#include <filament/TextureSampler.h>
#include <math/vec4.h>

#include <algorithm>

namespace quader {
namespace {

constexpr float kMeshTextureAnisotropy = 8.0f;
constexpr float kMeshSurfaceMajorGridMultiplier = 4.0f;

filament::math::float4 setting_color(ViewportSettingColor color) {
  return {
      srgb_to_linear(color.r),
      srgb_to_linear(color.g),
      srgb_to_linear(color.b),
      color.a,
  };
}

void bind_default_mesh_surface_grid(const MaterialBindingService &binding,
                                    filament::MaterialInstance *instance) {
  if (!instance) {
    return;
  }
  const ViewportSettings default_settings = ViewportSettings::defaults();
  binding.apply_mesh_surface_grid(*instance, 1.0f, default_settings);
}

} // namespace

void MaterialBindingService::initialize(
    filament::Material &shaded_mesh_material,
    filament::Material &rendered_mesh_material,
    TextureBindingService &texture_binding) {
  shaded_mesh_material_ = &shaded_mesh_material;
  rendered_mesh_material_ = &rendered_mesh_material;
  texture_binding_ = &texture_binding;
}

void MaterialBindingService::shutdown() {
  shaded_mesh_material_ = nullptr;
  rendered_mesh_material_ = nullptr;
  texture_binding_ = nullptr;
}

std::uint64_t MaterialBindingService::material_revision(
    const AssetRegistry &assets, const MaterialComponent *material) const {
  return RenderSignatureBuilder::material_signature(assets, material);
}

std::uint64_t MaterialBindingService::material_revision(
    const RenderMaterialBindingSnapshot &material) const {
  return material.revision;
}

filament::MaterialInstance *
MaterialBindingService::create_mesh_material_instance(
    const AssetRegistry &assets, const MaterialDocument *material,
    bool double_sided, bool editor_unlit) const {
  if (!shaded_mesh_material_ || !rendered_mesh_material_ || !texture_binding_) {
    return nullptr;
  }

  filament::TextureSampler sampler(
      filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::REPEAT);
  sampler.setAnisotropy(kMeshTextureAnisotropy);

  filament::Material &mesh_material =
      editor_unlit ? *shaded_mesh_material_ : *rendered_mesh_material_;
  filament::MaterialInstance *material_instance =
      mesh_material.createInstance();
  material_instance->setCullingMode(
      double_sided ? filament::MaterialInstance::CullingMode::NONE
                   : filament::MaterialInstance::CullingMode::BACK);
  material_instance->setParameter(
      material_texture_slot::kBaseColor,
      texture_binding_->resolve_texture(
          assets, material, material_texture_slot::kBaseColor,
          texture_binding_->fallback_albedo_texture()),
      sampler);
  const QVec3 base_color =
      material ? material->base_color : QVec3{1.0f, 1.0f, 1.0f};
  material_instance->setParameter(
      "baseColorFactor",
      filament::math::float4{base_color.x, base_color.y, base_color.z, 1.0f});
  bind_default_mesh_surface_grid(*this, material_instance);
  if (!editor_unlit) {
    material_instance->setParameter(
        material_texture_slot::kRoughness,
        texture_binding_->resolve_texture(
            assets, material, material_texture_slot::kRoughness,
            texture_binding_->fallback_roughness_texture()),
        sampler);
    material_instance->setParameter("metallicFactor",
                                    material ? material->metallic : 0.0f);
    material_instance->setParameter("roughnessFactor",
                                    material ? material->roughness : 1.0f);
  }
  return material_instance;
}

filament::MaterialInstance *
MaterialBindingService::create_mesh_material_instance(
    const RenderMaterialAssetSnapshot *material, bool double_sided,
    bool editor_unlit) const {
  if (!shaded_mesh_material_ || !rendered_mesh_material_ || !texture_binding_) {
    return nullptr;
  }

  filament::TextureSampler sampler(
      filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::REPEAT);
  sampler.setAnisotropy(kMeshTextureAnisotropy);

  filament::Material &mesh_material =
      editor_unlit ? *shaded_mesh_material_ : *rendered_mesh_material_;
  filament::MaterialInstance *material_instance =
      mesh_material.createInstance();
  material_instance->setCullingMode(
      double_sided ? filament::MaterialInstance::CullingMode::NONE
                   : filament::MaterialInstance::CullingMode::BACK);
  material_instance->setParameter(
      material_texture_slot::kBaseColor,
      texture_binding_->resolve_texture(
          material, material_texture_slot::kBaseColor,
          texture_binding_->fallback_albedo_texture()),
      sampler);
  const QVec3 base_color = material && material->present
                               ? material->document.base_color
                               : QVec3{1.0f, 1.0f, 1.0f};
  material_instance->setParameter(
      "baseColorFactor",
      filament::math::float4{base_color.x, base_color.y, base_color.z, 1.0f});
  bind_default_mesh_surface_grid(*this, material_instance);
  if (!editor_unlit) {
    material_instance->setParameter(
        material_texture_slot::kRoughness,
        texture_binding_->resolve_texture(
            material, material_texture_slot::kRoughness,
            texture_binding_->fallback_roughness_texture()),
        sampler);
    material_instance->setParameter(
        "metallicFactor",
        material && material->present ? material->document.metallic : 0.0f);
    material_instance->setParameter(
        "roughnessFactor",
        material && material->present ? material->document.roughness : 1.0f);
  }
  return material_instance;
}

void MaterialBindingService::apply_mesh_surface_grid(
    filament::MaterialInstance &material_instance, float grid_world_spacing,
    const ViewportSettings &settings) const {
  const float spacing = std::max(grid_world_spacing, 0.0001f);
  material_instance.setParameter(
      "surfaceGridMinorColor",
      setting_color(settings.color(ViewportColorSettingId::MeshGridMinor)));
  material_instance.setParameter(
      "surfaceGridMajorColor",
      setting_color(settings.color(ViewportColorSettingId::MeshGridMajor)));
  material_instance.setParameter("surfaceGridSize", spacing);
  material_instance.setParameter(
      "surfaceGridMajorSize",
      std::max(spacing * kMeshSurfaceMajorGridMultiplier, spacing));
  material_instance.setParameter(
      "surfaceGridMinorLineThickness",
      settings.float_value(ViewportFloatSettingId::GridMinorLineSize));
  material_instance.setParameter(
      "surfaceGridMajorLineThickness",
      settings.float_value(ViewportFloatSettingId::GridMajorLineSize));
  material_instance.setParameter(
      "surfaceGridEnabled",
      settings.bool_value(ViewportBoolSettingId::ShowMeshGrid) ? 1.0f : 0.0f);
}

} // namespace quader
