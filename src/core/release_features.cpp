////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/release_features.hpp"

#include <array>
#include <cstddef>

namespace quader {
namespace {

// Assign the first public release here when publishing a feature. Published
// release availability is cumulative; never move a released entry forward.
constexpr std::array kFeatures{
    ReleaseFeatureDescriptor{ReleaseFeature::Materials, "materials", "Material system", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::UvEditing, "uv_editing", "UV editing", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::MeshGrid, "mesh_grid", "Mesh Grid", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::RenderedViewport, "rendered_viewport", "Rendered and material-debug viewport modes", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Lights, "lights", "Lights", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Environment, "environment", "Environment, fog and post processing", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::BevelVertices, "bevel_vertices", "Bevel Vertices", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::BevelEdges, "bevel_edges", "Bevel Edges", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::SliceQuads, "slice_quads", "Slice Quads", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::ThickenFaces, "thicken_faces", "Thicken Faces", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::OuterHull, "outer_hull", "Create Outer Hull", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::HalfCut, "half_cut", "Half Cut", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Arrangement, "arrangement", "Align and Distribute", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::RadialAlign, "radial_align", "Radial Align", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::SnapToFloor, "snap_to_floor", "Snap to Floor", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::TransformBaking, "transform_baking", "Transform baking", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::MirrorTool, "mirror_tool", "Mirror Tool", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::MeshMirror, "mesh_mirror", "Mesh mirroring", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::MeshFlip, "mesh_flip", "Mesh flips", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::CutTool, "cut_tool", "Cut Tool", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::PolyTool, "poly_tool", "Poly Tool", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::LinearArray, "linear_array", "Linear Array", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::RadialArray, "radial_array", "Radial Array", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::PathArray, "path_array", "Path Array", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Curves, "curves", "Curves", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Pivot, "pivot", "Pivot editing", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::LassoSelection, "lasso_selection", "Lasso selection", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::CameraSettings, "camera_settings", "Camera settings controls", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::CastShadows, "cast_shadows", "Mesh Cast Shadow control", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::TorusPrimitive, "torus_primitive", "Torus primitive", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::ConePrimitive, "cone_primitive", "Cone primitive", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::QuadSpherePrimitive, "quad_sphere_primitive", "Quad Sphere primitive", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::Layers, "layers", "Layers", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::ExtrudeTool, "extrude_tool", "Persistent Extrude Tool", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::AdvancedSelectionUi, "advanced_selection_ui", "Advanced selection controls", kUnreleasedProductRelease},
    ReleaseFeatureDescriptor{ReleaseFeature::Instancing, "instancing", "Scene instancing", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::PaintSelection, "paint_selection", "Paint selection", kPublicRelease01},
    ReleaseFeatureDescriptor{ReleaseFeature::StairsAndRamps, "stairs_and_ramps", "Stairs & Ramps", kUnreleasedProductRelease},
};

static_assert(kFeatures.size() == static_cast<std::size_t>(ReleaseFeature::Count));
static_assert([] {
  for (std::size_t index = 0u; index < kFeatures.size(); ++index) {
    if (static_cast<std::size_t>(kFeatures[index].feature) != index) {
      return false;
    }
  }
  return true;
}());

} // namespace

std::span<const ReleaseFeatureDescriptor> release_feature_catalog() noexcept {
  return kFeatures;
}

ProductRelease release_feature_first_available(ReleaseFeature feature) noexcept {
  const auto index = static_cast<std::size_t>(feature);
  return index < kFeatures.size() ? kFeatures[index].first_release
                                 : kUnreleasedProductRelease;
}

bool product_release_has_feature(ProductRelease release,
                                 ReleaseFeature feature) noexcept {
  const auto index = static_cast<std::size_t>(feature);
  if (index >= kFeatures.size() || release == 0u) {
    return false;
  }
  if (release == kInternalFullRelease) {
    return true;
  }
  if (kFeatures[index].first_release == kUnreleasedProductRelease ||
      kFeatures[index].first_release > release) {
    return false;
  }
  // Path Array additionally requires public curve editing support.
  return feature != ReleaseFeature::PathArray ||
         product_release_has_feature(release, ReleaseFeature::Curves);
}

std::string product_release_label(ProductRelease release) {
  if (release == kInternalFullRelease || release == 0u) {
    return "Internal Full";
  }
  return std::to_string(release >> 32u) + "." +
         std::to_string(release & 0xffffffffu);
}

std::string release_feature_unavailable_message() {
  return "This feature is not available in this release.";
}

std::string product_release_unavailable_message(ProductRelease required) {
  return "This file requires Quader " + product_release_label(required) + ".";
}

} // namespace quader
