////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/material_presentation_service.hpp"

#include "assets/material_document.hpp"
#include "assets/project_material_document.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"

#include <algorithm>
#include <ranges>
#include <span>
#include <utility>

namespace quader {

MaterialPresentationRevision MaterialPresentationService::revision(
    const ProjectSession &project, const SceneSession *scene) {
  return {
      .project_activation_generation = project.activation_generation(),
      .project_material_revision =
          project.material_catalog().content_revision(),
      .active_scene_id = project.active_scene_id().packed(),
      .embedded_material_revision =
          scene != nullptr ? scene->assets().material_catalog_revision() : 0u,
  };
}

MaterialPresentationSnapshot MaterialPresentationService::snapshot(
    ProjectSession &project, const SceneSession *scene) {
  MaterialPresentationSnapshot result{
      .revision = revision(project, scene),
  };
  const std::span<const ProjectMaterialCatalogEntry> project_materials =
      project.material_catalog().entries();
  const std::size_t embedded_count =
      scene != nullptr ? scene->assets().material_count() : 0u;
  result.materials.reserve(project_materials.size() + embedded_count);

  for (const ProjectMaterialCatalogEntry &entry : project_materials) {
    const ProjectStandardMaterialDefinition *standard =
        entry.material.standard_material();
    MaterialPresentationItem item{
        .reference = MaterialReference::project(entry.material.id),
        .kind = entry.material.kind == ProjectMaterialKind::Standard
                    ? MaterialPresentationKind::Standard
                    : MaterialPresentationKind::Graph,
        .name = entry.material.name,
        .project_relative_path = entry.path.portable_string(),
        .qmat_size_bytes = entry.file_size,
        .fallback_color =
            standard != nullptr
                ? standard->base_color
                : std::array<float, 3>{1.0f, 1.0f, 1.0f},
        .content_revision = entry.content_revision,
        .available = true,
        .editable = entry.material.kind == ProjectMaterialKind::Standard,
    };
    ProjectIoResult<ProjectMaterialPreview> preview =
        project.material_preview(entry.material.id);
    if (preview.ok()) {
      ProjectMaterialPreview value = preview.take_value();
      item.encoded_albedo = std::move(value.encoded_bytes);
      item.content_revision = value.content_revision;
    }
    result.materials.push_back(std::move(item));
  }

  if (scene == nullptr) {
    return result;
  }
  std::vector<AssetId> embedded_ids = scene->assets().material_ids();
  std::ranges::sort(embedded_ids);
  for (const AssetId id : embedded_ids) {
    const MaterialDocument *material = scene->assets().material(id);
    if (material == nullptr ||
        !scene->assets().validate_material(*material).ok) {
      continue;
    }
    result.materials.push_back(MaterialPresentationItem{
        .reference = MaterialReference(id),
        .kind = MaterialPresentationKind::Embedded,
        .name = material->name,
        .fallback_color = {material->base_color.x, material->base_color.y,
                           material->base_color.z},
        .content_revision = material->revision.value,
        .available = true,
        .editable = false,
    });
  }
  return result;
}

} // namespace quader
