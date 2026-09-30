////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/components.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quader {

/**
 * Stores one-pass overlay frame and layer signatures for render proxy invalidation.
 */
struct OverlayFrameSignatures {
  std::uint64_t frame_signature = 0;
  std::array<std::uint64_t, kViewportOverlayLayerCount> layer_signatures{};
  std::array<std::size_t, kViewportOverlayLayerCount> triangle_counts{};
  std::array<std::size_t, kViewportOverlayLayerCount> line_counts{};
  std::array<std::size_t, kViewportOverlayLayerCount> point_counts{};
  std::array<std::size_t, kViewportOverlayLayerCount> icon_counts{};
  std::uint64_t component_point_geometry_signature = 0u;
  bool has_source_wire_depth_stamp = false;
};

/**
 * Builds stable render cache signatures for render extraction and proxy invalidation.
 */
class RenderSignatureBuilder final {
public:
    RenderSignatureBuilder();
    explicit RenderSignatureBuilder(std::uint64_t seed);

    RenderSignatureBuilder &add_value(std::uint64_t value);
    RenderSignatureBuilder &add_size(std::size_t value);
    RenderSignatureBuilder &add_bool(bool value);
    RenderSignatureBuilder &add_float(float value);
    RenderSignatureBuilder &add_string(std::string_view value);
    RenderSignatureBuilder &add_vec2(QVec2 value);
    RenderSignatureBuilder &add_vec3(QVec3 value);
    RenderSignatureBuilder &add_mat4(const QMat4 &value);

    [[nodiscard]] std::uint64_t value() const {
        return hash_;
    }

    [[nodiscard]] static std::uint64_t
    material_signature(const AssetRegistry &assets,
                       const MaterialComponent *material);
    [[nodiscard]] static std::uint64_t
    mesh_asset_signature(std::uint64_t seed, const MeshAsset &mesh);
    [[nodiscard]] static std::uint64_t
    light_signature(const LightComponent &light);
    [[nodiscard]] static std::uint64_t
    light_transform_signature(const QMat4 &world_transform);
    [[nodiscard]] static std::uint64_t
    overlay_frame_signature(const ViewportOverlayFrame &frame);
    [[nodiscard]] static OverlayFrameSignatures
    overlay_frame_signatures(const ViewportOverlayFrame &frame);
    [[nodiscard]] static std::uint64_t
    overlay_camera_signature(const ViewportOverlayCamera &camera);
    [[nodiscard]] static std::uint64_t
    overlay_layer_signature(const ViewportOverlayFrame &frame,
                            ViewportOverlayLayer layer);

  private:
    std::uint64_t hash_ = 0;
};

} // namespace quader
