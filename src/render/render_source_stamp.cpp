////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_source_stamp.hpp"

#include "assets/asset_registry.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <array>

namespace quader {
namespace {

constexpr std::array kDependencies{
    RenderSourceDependency::SceneBinding,
    RenderSourceDependency::SceneRevision,
    RenderSourceDependency::SelectionRevision,
    RenderSourceDependency::HierarchyPresentationRevision,
    RenderSourceDependency::VisibilityRevision,
    RenderSourceDependency::LocalVisibilityRevision,
    RenderSourceDependency::TransformRevision,
    RenderSourceDependency::TextureMappingRevision,
    RenderSourceDependency::MeshBindingRevision,
    RenderSourceDependency::CurveBindingRevision,
    RenderSourceDependency::MaterialBindingRevision,
    RenderSourceDependency::RenderComponentRevision,
    RenderSourceDependency::LayerRevision,
    RenderSourceDependency::AssetRevision,
    RenderSourceDependency::MeshMaterialUsageRevision,
    RenderSourceDependency::MaterialCatalogRevision,
    RenderSourceDependency::ProjectMaterialRevision,
    RenderSourceDependency::InstanceEditScope,
};

bool dependency_matches(const RenderSourceStamp &left,
                        const RenderSourceStamp &right,
                        RenderSourceDependency dependency) noexcept {
  switch (dependency) {
  case RenderSourceDependency::SceneBinding:
    return left.scene_binding == right.scene_binding;
  case RenderSourceDependency::SceneRevision:
    return left.scene_revision == right.scene_revision;
  case RenderSourceDependency::SelectionRevision:
    return left.selection_revision == right.selection_revision;
  case RenderSourceDependency::HierarchyPresentationRevision:
    return left.hierarchy_presentation_revision ==
           right.hierarchy_presentation_revision;
  case RenderSourceDependency::VisibilityRevision:
    return left.visibility_revision == right.visibility_revision;
  case RenderSourceDependency::LocalVisibilityRevision:
    return left.local_visibility_revision == right.local_visibility_revision;
  case RenderSourceDependency::TransformRevision:
    return left.transform_revision == right.transform_revision;
  case RenderSourceDependency::TextureMappingRevision:
    return left.texture_mapping_revision == right.texture_mapping_revision;
  case RenderSourceDependency::MeshBindingRevision:
    return left.mesh_binding_revision == right.mesh_binding_revision;
  case RenderSourceDependency::CurveBindingRevision:
    return left.curve_binding_revision == right.curve_binding_revision;
  case RenderSourceDependency::MaterialBindingRevision:
    return left.material_binding_revision == right.material_binding_revision;
  case RenderSourceDependency::RenderComponentRevision:
    return left.render_component_revision == right.render_component_revision;
  case RenderSourceDependency::LayerRevision:
    return left.layer_revision == right.layer_revision;
  case RenderSourceDependency::AssetRevision:
    return left.asset_revision == right.asset_revision;
  case RenderSourceDependency::MeshMaterialUsageRevision:
    return left.mesh_material_usage_revision ==
           right.mesh_material_usage_revision;
  case RenderSourceDependency::MaterialCatalogRevision:
    return left.material_catalog_revision == right.material_catalog_revision;
  case RenderSourceDependency::ProjectMaterialRevision:
    return left.project_material_revision == right.project_material_revision;
  case RenderSourceDependency::InstanceEditScope:
    return left.instance_edit_scope == right.instance_edit_scope;
  }
  return false;
}

bool revision_value(const RenderSourceStamp &stamp,
                    RenderSourceDependency dependency,
                    std::uint64_t &value) noexcept {
  switch (dependency) {
  case RenderSourceDependency::SceneRevision:
    value = stamp.scene_revision;
    return true;
  case RenderSourceDependency::SelectionRevision:
    value = stamp.selection_revision;
    return true;
  case RenderSourceDependency::HierarchyPresentationRevision:
    value = stamp.hierarchy_presentation_revision;
    return true;
  case RenderSourceDependency::VisibilityRevision:
    value = stamp.visibility_revision;
    return true;
  case RenderSourceDependency::LocalVisibilityRevision:
    value = stamp.local_visibility_revision;
    return true;
  case RenderSourceDependency::TransformRevision:
    value = stamp.transform_revision;
    return true;
  case RenderSourceDependency::TextureMappingRevision:
    value = stamp.texture_mapping_revision;
    return true;
  case RenderSourceDependency::MeshBindingRevision:
    value = stamp.mesh_binding_revision;
    return true;
  case RenderSourceDependency::CurveBindingRevision:
    value = stamp.curve_binding_revision;
    return true;
  case RenderSourceDependency::MaterialBindingRevision:
    value = stamp.material_binding_revision;
    return true;
  case RenderSourceDependency::RenderComponentRevision:
    value = stamp.render_component_revision;
    return true;
  case RenderSourceDependency::LayerRevision:
    value = stamp.layer_revision;
    return true;
  case RenderSourceDependency::AssetRevision:
    value = stamp.asset_revision;
    return true;
  case RenderSourceDependency::MeshMaterialUsageRevision:
    value = stamp.mesh_material_usage_revision;
    return true;
  case RenderSourceDependency::MaterialCatalogRevision:
    value = stamp.material_catalog_revision;
    return true;
  case RenderSourceDependency::ProjectMaterialRevision:
    value = stamp.project_material_revision;
    return true;
  case RenderSourceDependency::SceneBinding:
  case RenderSourceDependency::InstanceEditScope:
    return false;
  }
  return false;
}

} // namespace

RenderSourceStamp capture_render_source_stamp(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSourceStampCaptureOptions options) {
  return {
      .scene_binding = options.scene_binding,
      .scene_revision = document.revision(),
      .selection_revision = document.selection_revision(),
      .hierarchy_presentation_revision =
          document.hierarchy_presentation_revision(),
      .visibility_revision = document.visibility_revision(),
      .local_visibility_revision =
          options.local_visibility != nullptr
              ? options.local_visibility->revision()
              : 0u,
      .transform_revision = document.transform_revision(),
      .texture_mapping_revision = document.texture_mapping_revision(),
      .mesh_binding_revision = document.mesh_binding_revision(),
      .curve_binding_revision = document.curve_binding_revision(),
      .material_binding_revision = document.material_binding_revision(),
      .render_component_revision = document.render_component_revision(),
      .layer_revision = document.layer_revision(),
      .asset_revision = assets.revision(),
      .mesh_material_usage_revision = assets.mesh_material_usage_revision(),
      .material_catalog_revision = assets.material_catalog_revision(),
      .project_material_revision = options.project_material_revision,
      .instance_edit_scope = options.instance_edit_scope,
  };
}

RenderSourceDependencyMask render_source_changed_dependencies(
    const RenderSourceStamp &left, const RenderSourceStamp &right,
    RenderSourceDependencyMask dependencies) noexcept {
  RenderSourceDependencyMask changed;
  for (const RenderSourceDependency dependency : kDependencies) {
    if (dependencies.contains(dependency) &&
        !dependency_matches(left, right, dependency)) {
      changed.include(dependency);
    }
  }
  return changed;
}

bool render_source_dependencies_match(
    const RenderSourceStamp &left, const RenderSourceStamp &right,
    RenderSourceDependencyMask dependencies) noexcept {
  return render_source_changed_dependencies(left, right, dependencies).empty();
}

bool render_source_dependencies_advanced_together(
    const RenderSourceStamp &before, const RenderSourceStamp &after,
    RenderSourceDependencyMask dependencies) noexcept {
  if (dependencies.empty()) {
    return false;
  }

  std::uint64_t shared_delta = 0u;
  bool has_shared_delta = false;
  for (const RenderSourceDependency dependency : kDependencies) {
    if (!dependencies.contains(dependency)) {
      continue;
    }
    std::uint64_t before_value = 0u;
    std::uint64_t after_value = 0u;
    if (!revision_value(before, dependency, before_value) ||
        !revision_value(after, dependency, after_value) ||
        after_value <= before_value) {
      return false;
    }
    const std::uint64_t delta = after_value - before_value;
    if (has_shared_delta && shared_delta != delta) {
      return false;
    }
    shared_delta = delta;
    has_shared_delta = true;
  }
  return has_shared_delta;
}

} // namespace quader
