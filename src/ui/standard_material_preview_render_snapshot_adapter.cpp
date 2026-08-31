////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/standard_material_preview_render_snapshot_adapter.hpp"

#include "api/quader_standard_material_documents_api.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"

#include <cstddef>

namespace quader {
namespace {

bool preview_matches(const QuaderStandardMaterialDocumentInfo &document,
                     const QuaderStandardMaterialPreviewInfo &preview) {
  return preview.session_id == document.session_id && preview.id == document.id &&
         preview.draft_revision == document.draft_revision;
}

StandardMaterialRenderValues public_standard_values(
    const QuaderStandardMaterialDefinition &definition) {
  return StandardMaterialRenderValues{
      .base_color = definition.base_color,
      .roughness = definition.roughness,
      .metallic = definition.metallic,
      .normal_strength = definition.normal_strength,
      .directx_normal =
          definition.normal_convention == QuaderNormalMapConvention::DirectX,
      .texture_center = definition.texture_transform.center,
      .texture_scale = definition.texture_transform.scale,
      .texture_offset = definition.texture_transform.offset,
      .texture_rotation_degrees =
          definition.texture_transform.rotation_degrees,
  };
}

} // namespace

RenderMaterialEvaluationSnapshot standard_material_preview_render_snapshot(
    const QuaderStandardMaterialDocumentInfo &document,
    const QuaderStandardMaterialPreviewInfo *preview) {
  const bool matching_preview =
      preview != nullptr && preview_matches(document, *preview);
  const QuaderStandardMaterialDefinition &definition =
      matching_preview ? preview->material : document.material;
  RenderMaterialEvaluationSnapshot result{
      .project_material_id = document.id,
      .kind = RenderMaterialEvaluationKind::Standard,
      .identity_revision =
          render_project_asset_identity_revision(document.id) ^
          (static_cast<std::uint64_t>(document.session_id.generation()) << 32u),
      .content_revision = document.draft_revision,
      .payload = make_standard_material_render_evaluation(
          public_standard_values(definition)),
  };
  if (!matching_preview) {
    return result;
  }

  RenderStandardMaterialEvaluationSnapshot *render_standard =
      result.standard_material();
  if (render_standard == nullptr) {
    return result;
  }
  for (std::size_t index = 0u; index < preview->textures.size(); ++index) {
    if (!preview->textures[index].has_value()) {
      continue;
    }
    const QuaderStandardMaterialPreviewTexture &texture =
        *preview->textures[index];
    render_standard->textures[index] =
        RenderMaterialTextureEvaluationSnapshot{
            .project_texture_id = texture.id.value_or(ProjectAssetId{}),
            .identity_revision =
                texture.id.has_value()
                    ? render_project_asset_identity_revision(*texture.id)
                    : result.identity_revision ^
                          (0x9e3779b97f4a7c15ull + index),
            .content_revision = texture.content_revision,
            .color_space =
                texture.color_space == QuaderProjectTextureColorSpace::Srgb
                    ? TextureColorSpace::Srgb
                    : TextureColorSpace::Linear,
            .decoded_rgba8 = texture.rgba8,
            .width = texture.width,
            .height = texture.height,
        };
  }
  return result;
}

} // namespace quader
