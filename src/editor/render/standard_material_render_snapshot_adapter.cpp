////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/render/standard_material_render_snapshot_adapter.hpp"

#include "assets/project_material_document.hpp"
#include "editor/material/project_material_texture_snapshot.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "render/scene_render_extraction.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace quader {
namespace {

RenderMaterialEvaluationSnapshot make_evaluation(
    ProjectAssetId id, std::uint64_t identity_revision,
    std::uint64_t content_revision,
    RenderStandardMaterialEvaluationSnapshot standard) {
  return RenderMaterialEvaluationSnapshot{
      .project_material_id = std::move(id),
      .kind = RenderMaterialEvaluationKind::Standard,
      .identity_revision = identity_revision,
      .content_revision = content_revision,
      .payload = std::move(standard),
  };
}

constexpr std::array<StandardMaterialTextureSlot, 4> kProjectSlots{
    StandardMaterialTextureSlot::Albedo,
    StandardMaterialTextureSlot::Roughness,
    StandardMaterialTextureSlot::Metallic,
    StandardMaterialTextureSlot::Normal,
};

StandardMaterialRenderValues project_standard_values(
    const ProjectStandardMaterialDefinition &definition) {
  return StandardMaterialRenderValues{
      .base_color = definition.base_color,
      .roughness = definition.roughness,
      .metallic = definition.metallic,
      .normal_strength = definition.normal_strength,
      .directx_normal =
          definition.normal_convention == NormalMapConvention::DirectX,
      .texture_center = definition.texture_transform.center,
      .texture_scale = definition.texture_transform.scale,
      .texture_offset = definition.texture_transform.offset,
      .texture_rotation_degrees =
          definition.texture_transform.rotation_degrees,
      .diffuse_model = definition.diffuse_model,
  };
}

} // namespace

RenderStandardMaterialEvaluationSnapshot
make_standard_material_render_evaluation(
    const StandardMaterialRenderValues &values) {
  return RenderStandardMaterialEvaluationSnapshot{
      .base_color = {values.base_color[0], values.base_color[1],
                     values.base_color[2]},
      .roughness = values.roughness,
      .metallic = values.metallic,
      .normal_strength = values.normal_strength,
      .directx_normal = values.directx_normal,
      .texture_transform =
          RenderStandardMaterialTextureTransformSnapshot{
              .center = {values.texture_center[0], values.texture_center[1]},
              .scale = {values.texture_scale[0], values.texture_scale[1]},
              .offset = {values.texture_offset[0], values.texture_offset[1]},
              .rotation_degrees = values.texture_rotation_degrees,
          },
      .diffuse_model = values.diffuse_model,
  };
}

std::uint64_t ProjectSessionRenderMaterialResolver::revision() const noexcept {
  if (project_ == nullptr || !project_->has_project() ||
      project_->manifest() == nullptr) {
    return default_material_revision_;
  }
  return project_->material_texture_revision() +
         project_->standard_material_editor().scene_preview_revision() +
         default_material_revision_;
}

void ProjectSessionRenderMaterialResolver::set_default_material_texture(
    const std::filesystem::path &path) {
  if (default_material_texture_ == path) {
    return;
  }
  default_material_texture_ = path;
  ++default_material_revision_;
  if (path.empty()) {
    default_material_override_.reset();
    return;
  }
  default_material_override_ = packaged_default_render_material();
  auto &material = *default_material_override_;
  material.revision += default_material_revision_;
  auto &albedo = material.textures.front();
  albedo.texture.source_path = path;
  // This private texture shares no authored asset identity. Its revision also
  // distinguishes it from the packaged texture while retired GPU consumers live.
  albedo.texture.revision.value += default_material_revision_;
}

const RenderMaterialAssetSnapshot *
ProjectSessionRenderMaterialResolver::default_material_override() const noexcept {
  return default_material_override_.has_value()
             ? &*default_material_override_
             : nullptr;
}

ProjectAssetId ProjectSessionRenderMaterialResolver::canonical_material_id(
    ProjectAssetId id) const {
  return project_ != nullptr ? project_->canonical_material_id(id) : id;
}

std::optional<RenderProjectMaterialSnapshot>
ProjectSessionRenderMaterialResolver::resolve(ProjectAssetId id) const {
  if (project_ == nullptr || !project_->has_project()) {
    return std::nullopt;
  }
  id = canonical_material_id(id);
  const ProjectMaterialCatalogEntry *entry =
      project_->material_catalog().find(id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  const ProjectStandardMaterialDefinition *standard =
      entry->material.standard_material();
  if (standard == nullptr) {
    return std::nullopt;
  }

  const std::uint64_t catalog_revision = revision();
  // Draft albedo changes invalidate evaluation without replacing texture keys.
  const std::uint64_t saved_revision = project_->material_texture_revision();
  const auto &editor = project_->standard_material_editor();
  const auto preview_color = editor.scene_base_color_preview(id);
  StandardMaterialRenderValues values = project_standard_values(*standard);
  if (preview_color.has_value()) {
    values.base_color = *preview_color;
  }
  RenderProjectMaterialSnapshot result{
      .evaluation =
          make_evaluation(
              id,
              saved_revision ^ render_project_asset_identity_revision(id),
              entry->content_revision +
                  (preview_color.has_value() ? editor.scene_preview_revision()
                                             : 0u),
              make_standard_material_render_evaluation(values)),
      .catalog_revision = catalog_revision,
  };
  RenderStandardMaterialEvaluationSnapshot *render_standard =
      result.evaluation.standard_material();
  if (render_standard == nullptr) {
    return std::nullopt;
  }

  for (std::size_t index = 0u; index < kProjectSlots.size(); ++index) {
    const StandardMaterialTextureSlot slot = kProjectSlots[index];
    const std::optional<ProjectMaterialTextureSlot> *texture =
        standard->texture(slot);
    if (texture == nullptr || !texture->has_value()) {
      continue;
    }
    const std::shared_ptr<const ProjectMaterialTextureSnapshot> cached =
        project_->material_texture_image(id, slot);
    if (cached == nullptr) {
      continue;
    }
    const ProjectMaterialTextureSlot &definition = **texture;
    render_standard->textures[index] =
        RenderMaterialTextureEvaluationSnapshot{
            .project_texture_id = definition.id,
            .identity_revision =
                saved_revision ^
                render_project_asset_identity_revision(definition.id),
            .content_revision = cached->content_revision,
            .color_space =
                definition.color_space == ProjectTextureColorSpace::Srgb
                    ? TextureColorSpace::Srgb
                    : TextureColorSpace::Linear,
            .encoded_file_bytes = cached->encoded,
            .decoded_rgba8 = cached->decoded,
            .width = cached->width,
            .height = cached->height,
        };
  }
  return result;
}

} // namespace quader
