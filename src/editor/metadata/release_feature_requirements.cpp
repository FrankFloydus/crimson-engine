////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/release_feature_requirements.hpp"

#include "core/release_features.hpp"

#include <array>

namespace quader {
namespace {

/// Associates a stable native action identity with its published capability.
struct FeatureRequirement {
  std::string_view id;
  ReleaseFeature feature;
};

constexpr auto kActionRequirements = std::to_array<FeatureRequirement>({
    {"assign_material", ReleaseFeature::Materials},
    {"bevel_vertices", ReleaseFeature::BevelVertices},
    {"bevel_edges", ReleaseFeature::BevelEdges},
    {"edge_bevel", ReleaseFeature::BevelEdges},
    {"slice_quad", ReleaseFeature::SliceQuads},
    {"thicken_faces", ReleaseFeature::ThickenFaces},
    {"create_outer_hull", ReleaseFeature::OuterHull},
    {"half_cut", ReleaseFeature::HalfCut},
    {"radial_align", ReleaseFeature::RadialAlign},
    {"radial_align_vertices", ReleaseFeature::RadialAlign},
    {"radial_align_edges", ReleaseFeature::RadialAlign},
    {"radial_align_faces", ReleaseFeature::RadialAlign},
    {"snap_meshes_to_floor", ReleaseFeature::SnapToFloor},
    {"snap_vertices_to_floor", ReleaseFeature::SnapToFloor},
    {"bake_transforms", ReleaseFeature::TransformBaking},
    {"bake_location", ReleaseFeature::TransformBaking},
    {"bake_rotation", ReleaseFeature::TransformBaking},
    {"bake_scale", ReleaseFeature::TransformBaking},
    {"copy_pivot_from_active", ReleaseFeature::Pivot},
    {"mirror_selection_x", ReleaseFeature::MeshMirror},
    {"mirror_selection_y", ReleaseFeature::MeshMirror},
    {"mirror_selection_z", ReleaseFeature::MeshMirror},
    {"flip_horizontal", ReleaseFeature::MeshFlip},
    {"flip_vertical", ReleaseFeature::MeshFlip},
    {"cut_tool", ReleaseFeature::CutTool},
    {"mirror_tool", ReleaseFeature::MirrorTool},
    {"poly_tool", ReleaseFeature::PolyTool},
    {"uv_peel", ReleaseFeature::UvEditing},
});

constexpr auto kToolRequirements = std::to_array<FeatureRequirement>({
    {"create_cone", ReleaseFeature::ConePrimitive},
    {"create_torus", ReleaseFeature::TorusPrimitive},
    {"extrude", ReleaseFeature::ExtrudeTool},
    {"extrude_individual", ReleaseFeature::ExtrudeTool},
    {"extrude_along_normals", ReleaseFeature::ExtrudeTool},
    {"lasso_select", ReleaseFeature::LassoSelection},
    {"mirror", ReleaseFeature::MirrorTool},
    {"cut", ReleaseFeature::CutTool},
    {"poly", ReleaseFeature::PolyTool},
    {"linear_array", ReleaseFeature::LinearArray},
    {"radial_array", ReleaseFeature::RadialArray},
    {"curve", ReleaseFeature::Curves},
    {"create_curve", ReleaseFeature::Curves},
    {"pivot", ReleaseFeature::Pivot},
    {"uv_peel", ReleaseFeature::UvEditing},
});

template <std::size_t Size>
bool allows(std::string_view id,
            const std::array<FeatureRequirement, Size> &requirements) noexcept {
  for (const FeatureRequirement &requirement : requirements) {
    if (requirement.id == id) {
      return release_has_feature(requirement.feature);
    }
  }
  return true;
}

} // namespace

bool release_allows_tool(std::string_view id) noexcept {
  if (id == "path_array") {
    return release_has_feature(ReleaseFeature::PathArray) &&
           release_has_feature(ReleaseFeature::Curves);
  }
  return allows(id, kToolRequirements);
}

bool release_allows_action(std::string_view id) noexcept {
  if (id.starts_with("texture.") || id.starts_with("uv_")) {
    return release_has_feature(ReleaseFeature::UvEditing);
  }
  if (id.starts_with("align.") || id.starts_with("distribute.")) {
    return release_has_feature(ReleaseFeature::Arrangement);
  }
  return allows(id, kActionRequirements);
}

bool release_allows_operation(std::string_view id) noexcept {
  // Both actions use this operation; their distinct action gates still apply.
  if (id == "thicken") {
    return release_has_feature(ReleaseFeature::ThickenFaces) ||
           release_has_feature(ReleaseFeature::OuterHull);
  }
  return release_allows_action(id);
}

bool release_allows_command(std::string_view id) noexcept {
  constexpr std::string_view kToolPrefix = "tools.activate.";
  constexpr std::string_view kModelingPrefix = "modeling.";
  if (id.starts_with(kToolPrefix)) {
    return release_allows_tool(id.substr(kToolPrefix.size()));
  }
  if (id.starts_with(kModelingPrefix)) {
    return release_allows_action(id.substr(kModelingPrefix.size()));
  }
  if (id.starts_with("scene.layers.")) {
    return release_has_feature(ReleaseFeature::Layers);
  }
  if (id == "scene.create_point_light" ||
      id == "scene.create_directional_light") {
    return release_has_feature(ReleaseFeature::Lights);
  }
  if (id == "viewport.toggle_mesh_grid") {
    return release_has_feature(ReleaseFeature::MeshGrid);
  }
  if (id == "viewport.shading.rendered") {
    return release_has_feature(ReleaseFeature::RenderedViewport);
  }
  if (id == "viewport.toggle_texture_lock" ||
      id == "viewport.toggle_texture_lock_scale") {
    return release_has_feature(ReleaseFeature::UvEditing);
  }
  if (id == "select.face.same_material") {
    return release_has_feature(ReleaseFeature::Materials);
  }
  return true;
}

bool release_presents_command(std::string_view id) noexcept {
  if (!release_allows_command(id)) {
    return false;
  }
  if (!release_has_feature(ReleaseFeature::AdvancedSelectionUi) &&
      (id == "select.edge.direction" || id == "select.face.coplanar" ||
       id == "select.face.normal" || id == "select.mesh.ngons" ||
       id == "select.mesh.similar")) {
    return false;
  }
  return true;
}

bool release_allows_input_target(std::string_view id) noexcept {
  if (id == "viewport.quick_pivot") {
    return release_has_feature(ReleaseFeature::Pivot);
  }
  if (id.starts_with("viewport.material.")) {
    return release_has_feature(id == "viewport.material.transfer_face_texture"
                                   ? ReleaseFeature::UvEditing
                                   : ReleaseFeature::Materials);
  }
  if (id.starts_with("viewport.curve.")) {
    return release_has_feature(ReleaseFeature::Curves);
  }
  if (id.starts_with("viewport.linear_array.")) {
    return release_has_feature(ReleaseFeature::LinearArray);
  }
  if (id.starts_with("viewport.radial_array.")) {
    return release_has_feature(ReleaseFeature::RadialArray);
  }
  if (id == "viewport.modal_tool.mode_cycle") {
    return release_has_feature(ReleaseFeature::CutTool);
  }
  return release_allows_command(id);
}

} // namespace quader
