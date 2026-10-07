////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace quader {

/** Ordered public feature release, independent of app and file-format versions. */
using ProductRelease = std::uint64_t;

/** Packs a public major/minor release into the durable compatibility identifier. */
[[nodiscard]] constexpr ProductRelease product_release(std::uint32_t major,
                                                       std::uint32_t minor) noexcept {
  return (static_cast<ProductRelease>(major) << 32u) | minor;
}

inline constexpr ProductRelease kInternalFullRelease =
    std::numeric_limits<ProductRelease>::max();
inline constexpr ProductRelease kUnreleasedProductRelease = kInternalFullRelease;
inline constexpr ProductRelease kPublicRelease01 = product_release(0u, 1u);

/** Features with independently scheduled public availability. */
enum class ReleaseFeature {
  Materials,
  UvEditing,
  MeshGrid,
  RenderedViewport,
  Lights,
  Environment,
  BevelVertices,
  BevelEdges,
  SliceQuads,
  ThickenFaces,
  OuterHull,
  HalfCut,
  Arrangement,
  RadialAlign,
  SnapToFloor,
  TransformBaking,
  MirrorTool,
  MeshMirror,
  MeshFlip,
  CutTool,
  PolyTool,
  LinearArray,
  RadialArray,
  PathArray,
  Curves,
  Pivot,
  LassoSelection,
  CameraSettings,
  CastShadows,
  TorusPrimitive,
  ConePrimitive,
  QuadSpherePrimitive,
  Layers,
  ExtrudeTool,
  AdvancedSelectionUi,
  Instancing,
  PaintSelection,
  Count,
};

/** One permanent feature identity and its first cumulative public release. */
struct ReleaseFeatureDescriptor {
  ReleaseFeature feature;
  std::string_view id;
  std::string_view label;
  ProductRelease first_release;
};

/** Defined exactly once by the final executable's immutable profile object. */
[[nodiscard]] ProductRelease current_product_release() noexcept;
[[nodiscard]] std::span<const ReleaseFeatureDescriptor>
release_feature_catalog() noexcept;
[[nodiscard]] ProductRelease
release_feature_first_available(ReleaseFeature feature) noexcept;
[[nodiscard]] bool product_release_has_feature(ProductRelease release,
                                               ReleaseFeature feature) noexcept;
[[nodiscard]] inline bool release_has_feature(ReleaseFeature feature) noexcept {
  return product_release_has_feature(current_product_release(), feature);
}

/** Untagged legacy documents require the internal Full executable. */
[[nodiscard]] constexpr bool product_release_can_open(
    ProductRelease current, ProductRelease required) noexcept {
  return current == kInternalFullRelease ||
         (current != 0u && required != 0u &&
          required != kInternalFullRelease && required <= current);
}

[[nodiscard]] std::string product_release_label(ProductRelease release);
[[nodiscard]] std::string release_feature_unavailable_message();
[[nodiscard]] std::string
product_release_unavailable_message(ProductRelease required);

} // namespace quader
