////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/material_binding_service.hpp"

#include "core/color_space.hpp"
#include "core/release_features.hpp"
#include "render/instance_edit_style.hpp"
#include "render/render_signature_builder.hpp"
#include "render/texture_binding_service.hpp"
#include "viewport/viewport_settings.hpp"

#include <filament/TextureSampler.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec4.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>

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

void bind_shaded_lighting_parameters(
    filament::MaterialInstance &material_instance,
    const ViewportSettings &settings) {
  material_instance.setParameter("shadedLightingEnabled", 1.0f);
  material_instance.setParameter(
      "shadedKeyDirectionX",
      std::clamp(settings.float_value(
                     ViewportFloatSettingId::ShadedKeyDirectionX),
                 -1.0f, 1.0f));
  material_instance.setParameter(
      "shadedKeyDirectionY",
      std::clamp(settings.float_value(
                     ViewportFloatSettingId::ShadedKeyDirectionY),
                 -1.0f, 1.0f));
  material_instance.setParameter(
      "shadedKeyDirectionZ",
      std::clamp(settings.float_value(
                     ViewportFloatSettingId::ShadedKeyDirectionZ),
                 -1.0f, 1.0f));
  material_instance.setParameter(
      "shadedKeyIntensity",
      std::max(settings.float_value(
                   ViewportFloatSettingId::ShadedKeyIntensity),
               0.0f));
  material_instance.setParameter(
      "shadedDiffuseWrap",
      std::clamp(settings.float_value(
                     ViewportFloatSettingId::ShadedDiffuseWrap),
                 0.0f, 0.8f));
  material_instance.setParameter(
      "shadedGroundFill",
      std::clamp(settings.float_value(
                     ViewportFloatSettingId::ShadedGroundFill),
                 0.0f, 1.0f));
  material_instance.setParameter(
      "shadedSkyFill",
      std::clamp(settings.float_value(ViewportFloatSettingId::ShadedSkyFill),
                 0.0f, 1.0f));
}

} // namespace

void bind_mesh_texture_mappings(
    filament::MaterialInstance &material_instance,
    std::span<const QMat4> sampling_from_local) {
  assert(sampling_from_local.size() <= kMeshTextureMappingCapacity);
  if (sampling_from_local.size() > kMeshTextureMappingCapacity) {
    return;
  }
  std::array<filament::math::mat4f, kMeshTextureMappingCapacity> matrices;
  std::array<filament::math::mat3f, kMeshTextureMappingCapacity> cofactors;
  const std::size_t count =
      std::max(sampling_from_local.size(), std::size_t{1u});
  for (std::size_t index = 0u; index < count; ++index) {
    const QMat4 source = sampling_from_local.empty()
                            ? QMat4{}
                            : sampling_from_local[index];
    for (std::size_t column = 0u; column < 4u; ++column) {
      for (std::size_t row = 0u; row < 4u; ++row) {
        matrices[index][column][row] = source.values[column * 4u + row];
      }
    }
    const QVec3 first{source.values[0], source.values[1], source.values[2]};
    const QVec3 second{source.values[4], source.values[5], source.values[6]};
    const QVec3 third{source.values[8], source.values[9], source.values[10]};
    const std::array<QVec3, 3u> columns{
        cross(second, third), cross(third, first), cross(first, second)};
    for (std::size_t column = 0u; column < columns.size(); ++column) {
      cofactors[index][column] = {columns[column].x, columns[column].y,
                                 columns[column].z};
    }
  }
  material_instance.setParameter("textureSamplingFromLocal", matrices.data(),
                                 count);
  material_instance.setParameter("textureSamplingCofactor", cofactors.data(),
                                 count);
}

void bind_standard_material_texture_transform(
    filament::MaterialInstance &material_instance,
    const RenderStandardMaterialTextureTransformSnapshot &transform) {
  static const RenderStandardMaterialTextureTransformSnapshot kDefault;
  const auto finite_pair = [](QVec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
  };
  const QVec2 center = finite_pair(transform.center) ? transform.center
                                                     : kDefault.center;
  const QVec2 scale = finite_pair(transform.scale) ? transform.scale
                                                   : kDefault.scale;
  const QVec2 offset = finite_pair(transform.offset) ? transform.offset
                                                     : kDefault.offset;
  const float rotation_degrees = std::isfinite(transform.rotation_degrees)
                                     ? transform.rotation_degrees
                                     : kDefault.rotation_degrees;
  material_instance.setParameter(
      "textureTransformCenter",
      filament::math::float2{center.x, center.y});
  material_instance.setParameter(
      "textureTransformScale", filament::math::float2{scale.x, scale.y});
  material_instance.setParameter(
      "textureTransformOffset", filament::math::float2{offset.x, offset.y});
  material_instance.setParameter(
      "textureTransformRotationRadians",
      rotation_degrees * std::numbers::pi_v<float> / 180.0f);
}

void bind_default_shaded_lighting(
    filament::MaterialInstance &material_instance) {
  bind_shaded_lighting_parameters(material_instance,
                                   ViewportSettings::defaults());
}

void bind_instance_edit_context_style(
    filament::MaterialInstance &material_instance,
    const InstanceEditStyle &style) {
  material_instance.setParameter("instanceEditContextColor",
                                 setting_color(style.context));
  material_instance.setParameter("instanceEditContextStrength",
                                 style.context_strength);
  material_instance.setParameter("instanceEditContextTintAmount",
                                 style.context_tint_amount);
  material_instance.setParameter("instanceEditContextDesaturation",
                                 style.context_desaturation);
}

std::uint64_t mesh_surface_grid_signature(
    float grid_world_spacing, const ViewportSettings &settings) {
  const ViewportSettingColor minor =
      settings.color(ViewportColorSettingId::MeshGridMinor);
  const ViewportSettingColor major =
      settings.color(ViewportColorSettingId::MeshGridMajor);
  const InstanceEditStyle instance_edit_style =
      InstanceEditStyle::from_settings(settings);
  const ViewportSettingColor &instance_context = instance_edit_style.context;
  RenderSignatureBuilder signature;
  signature.add_float(std::max(grid_world_spacing, 0.0001f))
      .add_float(minor.r)
      .add_float(minor.g)
      .add_float(minor.b)
      .add_float(minor.a)
      .add_float(major.r)
      .add_float(major.g)
      .add_float(major.b)
      .add_float(major.a)
      .add_float(instance_context.r)
      .add_float(instance_context.g)
      .add_float(instance_context.b)
      .add_float(instance_context.a)
      .add_float(instance_edit_style.context_strength)
      .add_float(instance_edit_style.context_tint_amount)
      .add_float(instance_edit_style.context_desaturation)
      .add_float(
          settings.float_value(ViewportFloatSettingId::GridMinorLineSize))
      .add_float(
          settings.float_value(ViewportFloatSettingId::GridMajorLineSize))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionX))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionY))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedKeyDirectionZ))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedKeyIntensity))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedDiffuseWrap))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedGroundFill))
      .add_float(
          settings.float_value(ViewportFloatSettingId::ShadedSkyFill))
      .add_bool(settings.bool_value(ViewportBoolSettingId::ShowMeshGrid));
  return signature.value();
}

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
  project_material_resolver_ = nullptr;
}

void MaterialBindingService::bind_scene(RenderSceneBindingKey scene_binding) {
  if (texture_binding_ != nullptr) {
    texture_binding_->bind_scene(scene_binding);
  }
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
  if (project_material_resolver_ != nullptr &&
      project_material_resolver_->default_material_override() != nullptr) {
    return create_mesh_material_instance(
        project_material_resolver_->default_material_override(), double_sided,
        editor_unlit);
  }
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return create_mesh_material_instance(&packaged_default_render_material(),
                                         double_sided, editor_unlit);
  }
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
  bind_mesh_texture_mappings(*material_instance);
  material_instance->setCullingMode(
      double_sided ? filament::MaterialInstance::CullingMode::NONE
                   : filament::MaterialInstance::CullingMode::BACK);
  material_instance->setParameter(
      material_texture_slot::kBaseColor,
      texture_binding_->resolve_texture(
          *material_instance, assets, material, material_texture_slot::kBaseColor,
          texture_binding_->fallback_albedo_texture()),
      sampler);
  if (editor_unlit) {
    material_instance->setParameter("solidColorTexture",
                                    texture_binding_->solid_color_texture(),
                                    sampler);
  }
  const QVec3 base_color =
      material ? material->base_color : QVec3{1.0f, 1.0f, 1.0f};
  material_instance->setParameter(
      "baseColorFactor",
      filament::math::float4{base_color.x, base_color.y, base_color.z, 1.0f});
  bind_standard_material_texture_transform(
      *material_instance, RenderStandardMaterialTextureTransformSnapshot{});
  bind_default_mesh_surface_grid(*this, material_instance);
  if (editor_unlit) {
    bind_default_shaded_lighting(*material_instance);
  }
  apply_instance_edit_dim(*material_instance, false);
  material_instance->setParameter(
      material_texture_slot::kNormal,
      texture_binding_->resolve_texture(
          *material_instance, assets, material, material_texture_slot::kNormal,
          texture_binding_->fallback_normal_texture()),
      sampler);
  material_instance->setParameter("normalStrength", 1.0f);
  material_instance->setParameter("normalYSign", 1.0f);
  if (!editor_unlit) {
    material_instance->setParameter(
        material_texture_slot::kRoughness,
        texture_binding_->resolve_texture(
            *material_instance, assets, material, material_texture_slot::kRoughness,
            texture_binding_->fallback_roughness_texture()),
        sampler);
    material_instance->setParameter(
        material_texture_slot::kMetallic,
        texture_binding_->resolve_texture(
            *material_instance, assets, material, material_texture_slot::kMetallic,
            texture_binding_->fallback_metallic_texture()),
        sampler);
    material_instance->setParameter("metallicFactor",
                                    material ? material->metallic : 0.0f);
    material_instance->setParameter("roughnessFactor",
                                    material ? material->roughness : 1.0f);
    material_instance->setParameter(
        "diffuseModel",
        material && material->diffuse_model == MaterialDiffuseModel::Lambert
            ? 1
            : 0);
  }
  return material_instance;
}

filament::MaterialInstance *
MaterialBindingService::create_mesh_material_instance(
    const RenderMaterialAssetSnapshot *material, bool double_sided,
    bool editor_unlit) const {
  const RenderMaterialAssetSnapshot *default_override =
      project_material_resolver_ != nullptr
          ? project_material_resolver_->default_material_override()
          : nullptr;
  if (default_override != nullptr) {
    material = default_override;
  } else if (!release_has_feature(ReleaseFeature::Materials)) {
    material = &packaged_default_render_material();
  }
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
  bind_mesh_texture_mappings(*material_instance);
  material_instance->setCullingMode(
      double_sided ? filament::MaterialInstance::CullingMode::NONE
                   : filament::MaterialInstance::CullingMode::BACK);
  material_instance->setParameter(
      material_texture_slot::kBaseColor,
      texture_binding_->resolve_texture(
          *material_instance, material, material_texture_slot::kBaseColor,
          texture_binding_->fallback_albedo_texture()),
      sampler);
  if (editor_unlit) {
    material_instance->setParameter("solidColorTexture",
                                    texture_binding_->solid_color_texture(),
                                    sampler);
  }
  const QVec3 base_color = material && material->present
                               ? material->document.base_color
                               : QVec3{1.0f, 1.0f, 1.0f};
  material_instance->setParameter(
      "baseColorFactor",
      filament::math::float4{base_color.x, base_color.y, base_color.z, 1.0f});
  bind_standard_material_texture_transform(
      *material_instance,
      material && material->present
          ? material->texture_transform
          : RenderStandardMaterialTextureTransformSnapshot{});
  bind_default_mesh_surface_grid(*this, material_instance);
  if (editor_unlit) {
    bind_default_shaded_lighting(*material_instance);
  }
  apply_instance_edit_dim(*material_instance, false);
  material_instance->setParameter(
      material_texture_slot::kNormal,
      texture_binding_->resolve_texture(
          *material_instance, material, material_texture_slot::kNormal,
          texture_binding_->fallback_normal_texture()),
      sampler);
  material_instance->setParameter(
      "normalStrength",
      material && material->present ? material->normal_strength : 1.0f);
  material_instance->setParameter(
      "normalYSign",
      material && material->present && material->directx_normal ? -1.0f : 1.0f);
  if (!editor_unlit) {
    material_instance->setParameter(
        material_texture_slot::kRoughness,
        texture_binding_->resolve_texture(
            *material_instance, material, material_texture_slot::kRoughness,
            texture_binding_->fallback_roughness_texture()),
        sampler);
    material_instance->setParameter(
        material_texture_slot::kMetallic,
        texture_binding_->resolve_texture(
            *material_instance, material, material_texture_slot::kMetallic,
            texture_binding_->fallback_metallic_texture()),
        sampler);
    material_instance->setParameter(
        "metallicFactor",
        material && material->present ? material->document.metallic : 0.0f);
    material_instance->setParameter(
        "roughnessFactor",
        material && material->present ? material->document.roughness : 1.0f);
    material_instance->setParameter(
        "diffuseModel",
        material && material->present &&
                material->document.diffuse_model == MaterialDiffuseModel::Lambert
            ? 1
            : 0);
  }
  return material_instance;
}

filament::MaterialInstance *
MaterialBindingService::duplicate_mesh_material_instance(
    const filament::MaterialInstance *source) const {
  if (source == nullptr) {
    return nullptr;
  }
  filament::MaterialInstance *duplicate =
      filament::MaterialInstance::duplicate(source);
  if (duplicate != nullptr && texture_binding_ != nullptr) {
    texture_binding_->retain_material_textures(*source, *duplicate);
  }
  return duplicate;
}

void MaterialBindingService::destroy_mesh_material_instance(
    filament::Engine &engine,
    filament::MaterialInstance *&material_instance) const {
  if (material_instance == nullptr) {
    return;
  }
  // Filament prepares detached instances too. Queue their destruction before
  // releasing the final references to their textures.
  const filament::MaterialInstance *consumer = material_instance;
  engine.destroy(material_instance);
  material_instance = nullptr;
  if (texture_binding_ != nullptr) {
    texture_binding_->release_material_textures(consumer);
  }
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
      release_has_feature(ReleaseFeature::MeshGrid) &&
              settings.bool_value(ViewportBoolSettingId::ShowMeshGrid)
          ? 1.0f
          : 0.0f);
  bind_instance_edit_context_style(
      material_instance, InstanceEditStyle::from_settings(settings));
}

void MaterialBindingService::apply_shaded_lighting(
    filament::MaterialInstance &material_instance,
    const ViewportSettings &settings) const {
  bind_shaded_lighting_parameters(material_instance, settings);
}

void MaterialBindingService::apply_instance_edit_dim(
    filament::MaterialInstance &material_instance, bool dimmed) const {
  material_instance.setParameter("instanceEditDim", dimmed ? 1.0f : 0.0f);
}

} // namespace quader
