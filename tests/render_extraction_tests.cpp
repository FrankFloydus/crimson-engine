////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "math/glm_bridge.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/instance_batch_planner.hpp"
#include "render/instance_edit_style.hpp"
#include "render/instance_renderable_cache.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_quad_uv_interpolation.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_surface_grid_projection.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_light_refresh.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_info_stats.hpp"
#include "render/viewport_render_pass_coordinator.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/curve_tool_overlay_cache.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_bounds.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/primitive_creation_controls.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace quader;

namespace {

/// Supplies deterministic project-material snapshots to render tests.
class TestProjectMaterialResolver final
    : public RenderProjectMaterialResolver {
public:
  [[nodiscard]] std::uint64_t revision() const noexcept override {
    return revision_value;
  }

  [[nodiscard]] std::optional<RenderProjectMaterialSnapshot>
  resolve(ProjectAssetId id) const override {
    ++resolve_count;
    return material.has_value() &&
                   material->evaluation.project_material_id == id
               ? material
               : std::nullopt;
  }

  std::uint64_t revision_value = 1u;
  mutable std::size_t resolve_count = 0u;
  std::optional<RenderProjectMaterialSnapshot> material;
};

ViewportOverlayCamera test_overlay_camera() {
  return {
      .eye = {0.0f, 0.0f, 0.0f},
      .forward = {0.0f, 0.0f, -1.0f},
      .right = {1.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .viewport_size = {200.0f, 100.0f},
      .projection = ViewportOverlayProjection::Orthographic,
      .orthographic_size = 2.0f,
      .near_plane = 0.1f,
      .far_plane = 10.0f,
  };
}

ViewportOverlayCamera test_perspective_overlay_camera() {
  return {
      .eye = {0.0f, 0.0f, 0.0f},
      .forward = {0.0f, 0.0f, -1.0f},
      .right = {1.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .viewport_size = {200.0f, 100.0f},
      .projection = ViewportOverlayProjection::Perspective,
      .fov_degrees = 60.0f,
      .near_plane = 0.1f,
      .far_plane = 10.0f,
  };
}

MeshAsset make_overlay_triangle_mesh() {
  MeshAsset mesh;
  mesh.name = "Overlay Triangle";
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {-0.5f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {0.5f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 0.0f, 1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.5f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 3u, .material_slot = 0u}};
  return mesh;
}

CurveAsset make_overlay_curve_asset() {
  const std::array<modeling::CurvePointSpec, 2> points{{
      {.position = {0.0f, 0.0f, 0.0f},
       .incoming_handle = {-1.0f, 0.0f, 0.0f},
       .outgoing_handle = {0.0f, 2.0f, 0.0f}},
      {.position = {4.0f, 0.0f, 0.0f},
       .incoming_handle = {0.0f, 2.0f, 0.0f},
       .outgoing_handle = {1.0f, 0.0f, 0.0f}},
  }};
  auto document = modeling::CurveDocument::make_bezier(points);
  EXPECT_TRUE(document.ok());
  CurveAsset asset;
  asset.name = "Overlay Curve";
  if (document.ok()) {
    asset.document = std::make_shared<const modeling::CurveDocument>(
        std::move(document).value());
  }
  return asset;
}

MeshAsset make_authored_quad_mesh() {
  MeshAsset mesh;
  mesh.name = "Authored Quad";
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {-1.0f, 0.0f, -1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 0.0f, -1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 0.0f, 1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {1.0f, 1.0f}},
      MeshVertex{.position = {-1.0f, 0.0f, 1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 6u, .material_slot = 0u}};
  quader_poly::Document document;
  document.vertices = {
      {.id = 1u, .position = geometry.vertices[0].position},
      {.id = 2u, .position = geometry.vertices[1].position},
      {.id = 3u, .position = geometry.vertices[2].position},
      {.id = 4u, .position = geometry.vertices[3].position},
  };
  document.faces.push_back({.id = 1u, .vertices = {1u, 2u, 3u, 4u}});
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(std::move(document));
  return mesh;
}

MeshAsset make_authored_split_quad_mesh() {
  MeshAsset mesh = make_authored_quad_mesh();
  quader_poly::Document document = *mesh.authored_polygon_document;
  document.faces = {
      {.id = 1u, .vertices = {1u, 2u, 3u}},
      {.id = 2u, .vertices = {1u, 3u, 4u}},
  };
  mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(std::move(document));
  return mesh;
}

MeshGeometryPayload make_non_affine_uv_quad_geometry() {
  MeshGeometryPayload geometry;
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {-0.25f, 0.125f}},
      {.position = {3.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.5f, -0.375f}},
      {.position = {2.0f, 2.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.125f, 1.875f}},
      {.position = {0.0f, 1.5f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {-0.625f, 1.0f}},
  };
  std::uint32_t source_vertex_id = 1u;
  for (MeshVertex &vertex : geometry.vertices) {
    vertex.source_vertex_id = source_vertex_id++;
    vertex.uv_carrier = {1.25f, 0.125f, -0.25f, 0.75f};
    vertex.uv_geometric_normal = {0.0f, 0.0f, 1.0f};
  }
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {{.index_offset = 0u, .index_count = 6u}};
  return geometry;
}

constexpr std::array<std::array<std::uint32_t, 6>, 2> kQuadDiagonals{{
    {{0u, 1u, 2u, 0u, 2u, 3u}},
    {{0u, 1u, 3u, 1u, 2u, 3u}},
}};

std::array<double, 4> bilinear_weights(double s, double t) {
  return {(1.0 - s) * (1.0 - t), s * (1.0 - t), s * t,
          (1.0 - s) * t};
}

QVec3 bilinear_quad_position(const MeshGeometryPayload &geometry, double s,
                             double t,
                             std::span<const QVec3> positions = {}) {
  const auto weights = bilinear_weights(s, t);
  std::array<double, 3> result{};
  for (std::size_t index = 0; index < weights.size(); ++index) {
    const QVec3 position = positions.empty()
                              ? geometry.vertices[index].position
                              : positions[index];
    result[0] += weights[index] * position.x;
    result[1] += weights[index] * position.y;
    result[2] += weights[index] * position.z;
  }
  return {static_cast<float>(result[0]), static_cast<float>(result[1]),
          static_cast<float>(result[2])};
}

QVec2 bilinear_quad_uv(const MeshGeometryPayload &geometry, double s, double t) {
  const auto weights = bilinear_weights(s, t);
  std::array<double, 2> result{};
  for (std::size_t index = 0; index < weights.size(); ++index) {
    result[0] += weights[index] * geometry.vertices[index].uv.x;
    result[1] += weights[index] * geometry.vertices[index].uv.y;
  }
  return {static_cast<float>(result[0]), static_cast<float>(result[1])};
}

/// Captures triangle and corrected UVs at the same raster sample.
struct QuadUvTriangleSample final {
  QVec2 triangle_uv;
  QVec2 corrected_uv;
};

/// Emulates raster interpolation without using the production inverse solve.
std::vector<QuadUvTriangleSample> sample_quad_uv_triangles(
    const MeshGeometryPayload &geometry,
    std::span<const MeshQuadUvInterpolation> interpolations, QVec3 point,
    std::span<const QVec3> positions = {}) {
  std::vector<QuadUvTriangleSample> result;
  for (std::size_t offset = 0; offset + 2u < geometry.indices.size();
       offset += 3u) {
    const std::array<std::uint32_t, 3> indices{
        geometry.indices[offset], geometry.indices[offset + 1u],
        geometry.indices[offset + 2u]};
    std::array<QVec3, 3> triangle;
    for (std::size_t index = 0; index < indices.size(); ++index) {
      triangle[index] = positions.empty()
                            ? geometry.vertices[indices[index]].position
                            : positions[indices[index]];
    }
    const auto difference = [](QVec3 left, QVec3 right) {
      return std::array<double, 3>{static_cast<double>(left.x) - right.x,
                                   static_cast<double>(left.y) - right.y,
                                   static_cast<double>(left.z) - right.z};
    };
    const auto dot = [](const auto &left, const auto &right) {
      return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
    };
    const auto edge0 = difference(triangle[1], triangle[0]);
    const auto edge1 = difference(triangle[2], triangle[0]);
    const auto relative = difference(point, triangle[0]);
    const double a = dot(edge0, edge0);
    const double b = dot(edge0, edge1);
    const double c = dot(edge1, edge1);
    const double d = dot(relative, edge0);
    const double e = dot(relative, edge1);
    const double determinant = a * c - b * b;
    if (determinant <= 0.0) {
      continue;
    }
    const double weight1 = (d * c - e * b) / determinant;
    const double weight2 = (e * a - d * b) / determinant;
    const std::array<double, 3> weights{1.0 - weight1 - weight2, weight1,
                                        weight2};
    if (std::ranges::any_of(weights, [](double weight) {
          return weight < -0.000002;
        })) {
      continue;
    }
    MeshQuadUvInterpolation interpolated = interpolations[indices[0]];
    interpolated.coordinates = {};
    QVec2 triangle_uv{};
    for (std::size_t index = 0; index < indices.size(); ++index) {
      const auto weight = static_cast<float>(weights[index]);
      const auto &coordinates = interpolations[indices[index]].coordinates;
      interpolated.coordinates.x += weight * coordinates.x;
      interpolated.coordinates.y += weight * coordinates.y;
      interpolated.coordinates.z += weight * coordinates.z;
      triangle_uv.x += weight * geometry.vertices[indices[index]].uv.x;
      triangle_uv.y += weight * geometry.vertices[indices[index]].uv.y;
    }
    result.push_back({triangle_uv, evaluate_mesh_quad_uv_interpolation(
                                       triangle_uv, interpolated)});
  }
  return result;
}

void expect_quad_uv_fallback(const MeshGeometryPayload &geometry) {
  const auto interpolations = build_mesh_quad_uv_interpolations(
      geometry.vertices, geometry.indices, geometry.primitives);
  ASSERT_EQ(interpolations.size(), geometry.vertices.size());
  const QVec2 unchanged{-0.0f, 1234.625f};
  for (const auto &interpolation : interpolations) {
    EXPECT_FALSE(interpolation.enabled);
    EXPECT_FLOAT_EQ(interpolation.coordinates.x, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.coordinates.y, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.coordinates.z, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.geometry_warp.x, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.geometry_warp.y, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.uv_correction.x, 0.0f);
    EXPECT_FLOAT_EQ(interpolation.uv_correction.y, 0.0f);
    const auto actual =
        evaluate_mesh_quad_uv_interpolation(unchanged, interpolation);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.x),
              std::bit_cast<std::uint32_t>(unchanged.x));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.y),
              std::bit_cast<std::uint32_t>(unchanged.y));
  }
}

std::size_t line_count_for(const ViewportOverlayFrame &frame,
                           ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(
      std::count_if(frame.lines.begin(), frame.lines.end(),
                    [layer](const ViewportOverlayLine &line) {
                      return line.layer == layer;
                    }));
}

std::size_t line_source_count_for(const ViewportOverlayFrame &frame,
                                  ViewportOverlaySource source) {
  return static_cast<std::size_t>(
      std::count_if(frame.lines.begin(), frame.lines.end(),
                    [source](const ViewportOverlayLine &line) {
                      return line.source == source;
                    }));
}

std::optional<SceneBounds>
line_bounds_for(const ViewportOverlayFrame &frame, ViewportOverlayLayer layer) {
  std::optional<SceneBounds> bounds;
  for (const ViewportOverlayLine &line : frame.lines) {
    if (line.layer != layer) {
      continue;
    }
    if (!bounds.has_value()) {
      bounds = SceneBounds{.min = line.start, .max = line.start};
    } else {
      include_point(*bounds, line.start);
    }
    include_point(*bounds, line.end);
  }
  return bounds;
}

void expect_scene_bounds_near(const SceneBounds &actual,
                              const SceneBounds &expected) {
  constexpr float kTolerance = 0.0001f;
  EXPECT_NEAR(actual.min.x, expected.min.x, kTolerance);
  EXPECT_NEAR(actual.min.y, expected.min.y, kTolerance);
  EXPECT_NEAR(actual.min.z, expected.min.z, kTolerance);
  EXPECT_NEAR(actual.max.x, expected.max.x, kTolerance);
  EXPECT_NEAR(actual.max.y, expected.max.y, kTolerance);
  EXPECT_NEAR(actual.max.z, expected.max.z, kTolerance);
}

void expect_bounds_snapshot_near(const RenderOverlayBoundsSnapshot &actual,
                                 const SceneBounds &expected) {
  constexpr float kTolerance = 0.0001f;
  EXPECT_NEAR(actual.min.x, expected.min.x, kTolerance);
  EXPECT_NEAR(actual.min.y, expected.min.y, kTolerance);
  EXPECT_NEAR(actual.min.z, expected.min.z, kTolerance);
  EXPECT_NEAR(actual.max.x, expected.max.x, kTolerance);
  EXPECT_NEAR(actual.max.y, expected.max.y, kTolerance);
  EXPECT_NEAR(actual.max.z, expected.max.z, kTolerance);
}

SceneBounds exact_transformed_geometry_bounds(const MeshAsset &mesh,
                                              const QMat4 &world_transform) {
  const std::vector<MeshVertex> &vertices = mesh.geometry().vertices;
  EXPECT_FALSE(vertices.empty());
  if (vertices.empty()) {
    return {};
  }

  const QVec3 first = transform_point(world_transform,
                                      vertices.front().position);
  SceneBounds bounds{.min = first, .max = first};
  for (const MeshVertex &vertex : vertices) {
    include_point(bounds, transform_point(world_transform, vertex.position));
  }
  return bounds;
}

std::size_t triangle_count_for(const ViewportOverlayFrame &frame,
                               ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(
      std::count_if(frame.triangles.begin(), frame.triangles.end(),
                    [layer](const ViewportOverlayTriangle &triangle) {
                      return triangle.layer == layer;
                    }));
}

std::size_t point_count_for(const ViewportOverlayFrame &frame,
                            ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(
      std::count_if(frame.points.begin(), frame.points.end(),
                    [layer](const ViewportOverlayPoint &point) {
                      return point.layer == layer;
                    }));
}

std::size_t icon_count_for(const ViewportOverlayFrame &frame,
                           ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(
      std::count_if(frame.icons.begin(), frame.icons.end(),
                    [layer](const ViewportOverlayIcon &icon) {
                      return icon.layer == layer;
                    }));
}

std::size_t point_mesh_vertex_count(const OverlayLayerBuildPlan &plan) {
  std::size_t count = 0u;
  for (const OverlayPointBatch &batch : plan.point_batches) {
    count += batch.mesh.vertices.size();
  }
  return count;
}

void append_source_wire_depth_stamp_triangle(ViewportOverlayFrame &frame,
                                             int object_id, QVec3 a, QVec3 b,
                                             QVec3 c, bool inside_out) {
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::SourceWireDepthStamp,
      .a = a,
      .b = inside_out ? c : b,
      .c = inside_out ? b : c,
      .color = rgba8(255, 224, 92, 235),
      .element = {.object_id = object_id},
  });
}

void append_source_wire_depth_stamp_quad(ViewportOverlayFrame &frame,
                                         int object_id, QVec3 a, QVec3 b,
                                         QVec3 c, QVec3 d, bool inside_out) {
  append_source_wire_depth_stamp_triangle(frame, object_id, a, b, c,
                                          inside_out);
  append_source_wire_depth_stamp_triangle(frame, object_id, a, c, d,
                                          inside_out);
}

void append_source_wire_depth_stamp_box(ViewportOverlayFrame &frame,
                                        int object_id, bool inside_out) {
  const QVec3 front_bottom_left{-1.0f, -1.0f, -1.0f};
  const QVec3 front_bottom_right{1.0f, -1.0f, -1.0f};
  const QVec3 front_top_right{1.0f, 1.0f, -1.0f};
  const QVec3 front_top_left{-1.0f, 1.0f, -1.0f};
  const QVec3 back_bottom_left{-1.0f, -1.0f, -3.0f};
  const QVec3 back_bottom_right{1.0f, -1.0f, -3.0f};
  const QVec3 back_top_right{1.0f, 1.0f, -3.0f};
  const QVec3 back_top_left{-1.0f, 1.0f, -3.0f};

  append_source_wire_depth_stamp_quad(frame, object_id, front_bottom_left,
                                      front_bottom_right, front_top_right,
                                      front_top_left, inside_out);
  append_source_wire_depth_stamp_quad(frame, object_id, back_bottom_left,
                                      back_top_left, back_top_right,
                                      back_bottom_right, inside_out);
  append_source_wire_depth_stamp_quad(frame, object_id, front_bottom_right,
                                      back_bottom_right, back_top_right,
                                      front_top_right, inside_out);
  append_source_wire_depth_stamp_quad(frame, object_id, front_bottom_left,
                                      front_top_left, back_top_left,
                                      back_bottom_left, inside_out);
  append_source_wire_depth_stamp_quad(frame, object_id, front_top_left,
                                      front_top_right, back_top_right,
                                      back_top_left, inside_out);
  append_source_wire_depth_stamp_quad(frame, object_id, front_bottom_left,
                                      back_bottom_left, back_bottom_right,
                                      front_bottom_right, inside_out);
}

} // namespace

TEST(RenderSignatureBuilderTests, CentralizesMaterialMeshAndOverlaySignatures) {
    AssetRegistry assets;
    TextureAsset base_color;
    base_color.name = "Base Color";
    const AssetId base_color_texture = assets.add_texture(base_color);
    TextureAsset roughness;
    roughness.name = "Roughness";
    const AssetId roughness_texture = assets.add_texture(roughness);

    MaterialDocument material;
    material.texture_slots.emplace(material_texture_slot::kRoughness,
                                   roughness_texture);
    material.texture_slots.emplace(material_texture_slot::kBaseColor,
                                   base_color_texture);
    const AssetId material_id = assets.add_material(std::move(material));
    ASSERT_NE(material_id, kInvalidAssetId);

    MaterialBindingService material_binding;
    const MaterialComponent material_component{.material = material_id};
    const MaterialComponent ordered_slot_component{
        .material = material_id,
        .material_slots = {{1u, material_id}, {2u, material_id}},
    };
    const MaterialComponent reordered_slot_component{
        .material = material_id,
        .material_slots = {{2u, material_id}, {1u, material_id}},
    };
    EXPECT_EQ(material_binding.material_revision(assets, &material_component),
              RenderSignatureBuilder::material_signature(assets,
                                                         &material_component));
    EXPECT_EQ(RenderSignatureBuilder::material_signature(
                  assets, &ordered_slot_component),
              RenderSignatureBuilder::material_signature(
                  assets, &reordered_slot_component));

    MeshAsset mesh = make_overlay_triangle_mesh();
    RenderSignatureBuilder mesh_seed;
    mesh_seed.add_value(42u).add_vec3({1.0f, 2.0f, 3.0f});
    EXPECT_EQ(
        MeshRenderableFactory::mesh_asset_signature(mesh_seed.value(), mesh),
        RenderSignatureBuilder::mesh_asset_signature(mesh_seed.value(), mesh));
    const std::uint64_t original_mesh_signature =
        RenderSignatureBuilder::mesh_asset_signature(mesh_seed.value(), mesh);
    mesh.edit_geometry().vertices.front().uv_carrier[0] = 2.0f;
    const std::uint64_t carrier_signature =
        RenderSignatureBuilder::mesh_asset_signature(mesh_seed.value(), mesh);
    EXPECT_NE(carrier_signature, original_mesh_signature);
    mesh.edit_geometry().vertices.front().uv_geometric_normal = {1.0f, 0.0f, 0.0f};
    EXPECT_NE(RenderSignatureBuilder::mesh_asset_signature(mesh_seed.value(), mesh),
              carrier_signature);

    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 128, 64, 255),
        .width_pixels = 2.0f,
    });
    EXPECT_EQ(OverlayRenderableBuilder::frame_signature(frame),
              RenderSignatureBuilder::overlay_frame_signature(frame));
    EXPECT_EQ(OverlayRenderableBuilder::camera_signature(test_overlay_camera()),
              RenderSignatureBuilder::overlay_camera_signature(
                  test_overlay_camera()));
    EXPECT_EQ(OverlayRenderableBuilder::layer_signature(
                  frame, ViewportOverlayLayer::SourceWire),
              RenderSignatureBuilder::overlay_layer_signature(
                  frame, ViewportOverlayLayer::SourceWire));
}

TEST(RenderSignatureBuilderTests, BuildsOverlayFrameLayerSignaturesInOnePass) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SelectedXrayFace,
        .a = {-0.5f, -0.5f, -1.0f},
        .b = {0.5f, -0.5f, -1.0f},
        .c = {0.0f, 0.5f, -1.0f},
        .color = rgba8(255, 211, 31, 96),
        .element = {.object_id = 7},
    });
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = {-0.5f, -0.5f, -1.0f},
        .b = {0.5f, -0.5f, -1.0f},
        .c = {0.0f, 0.5f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .element = {.object_id = 7},
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 7},
    });
    frame.points.push_back({
        .layer = ViewportOverlayLayer::SelectedVertex,
        .position = {0.0f, 0.0f, -1.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 8.0f,
        .element = {.object_id = 7, .vertex_id = 1},
    });
    frame.icons.push_back({
        .layer = ViewportOverlayLayer::PointLightGizmo,
        .kind = ViewportOverlayIconKind::PointLight,
        .position = {0.25f, 0.5f, -2.0f},
        .color = rgba8(255, 255, 255, 255),
        .size_pixels = kOverlayPointLightGizmoSizePixels,
        .element = {.object_id = 12},
    });

    const OverlayFrameSignatures signatures =
        OverlayRenderableBuilder::frame_signatures(frame);

    EXPECT_EQ(signatures.frame_signature,
              OverlayRenderableBuilder::frame_signature(frame));
    EXPECT_EQ(signatures.layer_signatures[static_cast<std::size_t>(
                  ViewportOverlayLayer::SourceWire)],
              OverlayRenderableBuilder::layer_signature(
                  frame, ViewportOverlayLayer::SourceWire));
    EXPECT_TRUE(signatures.has_source_wire_depth_stamp);
    EXPECT_EQ(signatures.triangle_counts[static_cast<std::size_t>(
                  ViewportOverlayLayer::SelectedXrayFace)],
              1u);
    EXPECT_EQ(signatures.triangle_counts[static_cast<std::size_t>(
                  ViewportOverlayLayer::SourceWireDepthStamp)],
              1u);
    EXPECT_EQ(signatures.line_counts[static_cast<std::size_t>(
                  ViewportOverlayLayer::SourceWire)],
              1u);
    EXPECT_EQ(signatures.point_counts[static_cast<std::size_t>(
                  ViewportOverlayLayer::SelectedVertex)],
              1u);
    EXPECT_EQ(signatures.icon_counts[static_cast<std::size_t>(
                  ViewportOverlayLayer::PointLightGizmo)],
              1u);
    EXPECT_TRUE(OverlayRenderableBuilder::layer_has_renderable_primitives(
        signatures, ViewportOverlayLayer::SourceWire));
    EXPECT_TRUE(OverlayRenderableBuilder::layer_has_renderable_primitives(
        signatures, ViewportOverlayLayer::PointLightGizmo));
    EXPECT_FALSE(OverlayRenderableBuilder::layer_has_renderable_primitives(
        signatures, ViewportOverlayLayer::SourceWireDepthStamp));

}

TEST(MeshAssetTests, CoalescesContiguousPrimitiveSpansByMaterialSlotForRenderSubmission) {
    MeshAsset mesh;
    MeshGeometryPayload &geometry = mesh.edit_geometry();
    geometry.indices = {
            0u, 1u, 2u,
            3u, 4u, 5u,
            6u, 7u, 8u,
            9u, 10u, 11u,
            12u, 13u, 14u,
    };
    geometry.primitives = {
        MeshPrimitiveSpan{
            .index_offset = 0u, .index_count = 3u, .material_slot = 0u},
        MeshPrimitiveSpan{
            .index_offset = 3u, .index_count = 6u, .material_slot = 0u},
        MeshPrimitiveSpan{
            .index_offset = 9u, .index_count = 3u, .material_slot = 2u},
        MeshPrimitiveSpan{
            .index_offset = 12u, .index_count = 3u, .material_slot = 0u},
    };

    const std::vector<MeshPrimitiveSpan> spans =
        coalesced_mesh_primitive_spans(mesh);

    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[0].index_offset, 0u);
    EXPECT_EQ(spans[0].index_count, 9u);
    EXPECT_EQ(spans[0].material_slot, 0u);
    EXPECT_EQ(spans[1].index_offset, 9u);
    EXPECT_EQ(spans[1].index_count, 3u);
    EXPECT_EQ(spans[1].material_slot, 2u);
    EXPECT_EQ(spans[2].index_offset, 12u);
    EXPECT_EQ(spans[2].index_count, 3u);
    EXPECT_EQ(spans[2].material_slot, 0u);
}

TEST(MeshSurfaceGridProjectionTests,
     FaceProjectsOneUnsharedTriangleWithAConstantAreaNormal) {
  const std::vector<MeshVertex> vertices{
      {.position = {0.0f, 0.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}},
  };
  const std::vector<std::uint32_t> indices{0u, 1u, 2u};
  const std::vector<MeshPrimitiveSpan> primitives{{
      .index_offset = 0u,
      .index_count = 3u,
  }};

  const std::vector<MeshSurfaceGridVertexProjection> projections =
      build_mesh_surface_grid_vertex_projections(vertices, indices,
                                                 primitives);

  ASSERT_EQ(projections.size(), 3u);
  for (const MeshSurfaceGridVertexProjection &projection : projections) {
    EXPECT_TRUE(projection.face_projected);
    EXPECT_NEAR(projection.local_normal.x, 0.0f, 0.000001f);
    EXPECT_NEAR(projection.local_normal.y, -0.70710677f, 0.000001f);
    EXPECT_NEAR(projection.local_normal.z, 0.70710677f, 0.000001f);
  }
}

TEST(MeshSurfaceGridProjectionTests,
     SmoothQuadKeepsInterpolatedProjectionButFlatQuadUsesItsFaceNormal) {
  std::vector<MeshVertex> vertices{
      {.position = {-1.0f, -1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
      {.position = {1.0f, -1.0f, 0.0f}, .normal = {0.6f, 0.0f, 0.8f}},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.6f, 0.8f}},
      {.position = {-1.0f, 1.0f, 0.0f}, .normal = {-0.6f, 0.0f, 0.8f}},
  };
  const std::vector<std::uint32_t> indices{0u, 1u, 2u, 0u, 2u, 3u};
  const std::vector<MeshPrimitiveSpan> primitives{{
      .index_offset = 0u,
      .index_count = 6u,
  }};

  const std::vector<MeshSurfaceGridVertexProjection> smooth =
      build_mesh_surface_grid_vertex_projections(vertices, indices,
                                                 primitives);
  ASSERT_EQ(smooth.size(), 4u);
  EXPECT_TRUE(std::ranges::none_of(
      smooth, [](const MeshSurfaceGridVertexProjection &projection) {
        return projection.face_projected;
      }));

  for (MeshVertex &vertex : vertices) {
    vertex.normal = {0.0f, 0.0f, 1.0f};
  }
  const std::vector<MeshSurfaceGridVertexProjection> flat =
      build_mesh_surface_grid_vertex_projections(vertices, indices,
                                                 primitives);
  ASSERT_EQ(flat.size(), 4u);
  for (const MeshSurfaceGridVertexProjection &projection : flat) {
    EXPECT_TRUE(projection.face_projected);
    EXPECT_NEAR(projection.local_normal.x, 0.0f, 0.000001f);
    EXPECT_NEAR(projection.local_normal.y, 0.0f, 0.000001f);
    EXPECT_NEAR(projection.local_normal.z, 1.0f, 0.000001f);
  }
}

TEST(MeshSurfaceGridProjectionTests,
     SharedVerticesRejectPerFaceProjectionThatCannotStayConstant) {
  const std::vector<MeshVertex> vertices{
      {.position = {-1.0f, -1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
      {.position = {1.0f, -1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
      {.position = {-1.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
  };
  const std::vector<std::uint32_t> indices{0u, 1u, 2u, 0u, 2u, 3u};
  const std::vector<MeshPrimitiveSpan> primitives{
      {.index_offset = 0u, .index_count = 3u},
      {.index_offset = 3u, .index_count = 3u},
  };

  const std::vector<MeshSurfaceGridVertexProjection> projections =
      build_mesh_surface_grid_vertex_projections(vertices, indices,
                                                 primitives);

  ASSERT_EQ(projections.size(), 4u);
  EXPECT_TRUE(std::ranges::none_of(
      projections, [](const MeshSurfaceGridVertexProjection &projection) {
        return projection.face_projected;
      }));
}

TEST(MeshSurfaceGridProjectionTests,
     PrimitivePartitionChangesProjectionAndGeometryCacheIdentity) {
  MeshAsset smooth_mesh;
  MeshGeometryPayload &smooth_geometry = smooth_mesh.edit_geometry();
  smooth_geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
      {.position = {2.0f, 0.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}},
      {.position = {3.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {2.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
  };
  smooth_geometry.indices = {0u, 1u, 2u, 3u, 4u, 5u};
  smooth_geometry.primitives = {{
      .index_offset = 0u,
      .index_count = 6u,
  }};

  MeshAsset face_mesh = smooth_mesh;
  face_mesh.edit_geometry().primitives = {
      {.index_offset = 0u, .index_count = 3u},
      {.index_offset = 3u, .index_count = 3u},
  };

  const std::vector<MeshSurfaceGridVertexProjection> smooth =
      build_mesh_surface_grid_vertex_projections(
          smooth_mesh.geometry().vertices, smooth_mesh.geometry().indices,
          smooth_mesh.geometry().primitives);
  const std::vector<MeshSurfaceGridVertexProjection> face_projected =
      build_mesh_surface_grid_vertex_projections(
          face_mesh.geometry().vertices, face_mesh.geometry().indices,
          face_mesh.geometry().primitives);

  EXPECT_TRUE(std::ranges::none_of(
      smooth, [](const MeshSurfaceGridVertexProjection &projection) {
        return projection.face_projected;
      }));
  EXPECT_TRUE(std::ranges::all_of(
      face_projected,
      [](const MeshSurfaceGridVertexProjection &projection) {
        return projection.face_projected;
      }));
  EXPECT_NE(smooth_mesh.geometry_content_id(),
            face_mesh.geometry_content_id());
  EXPECT_NE(MeshRenderableFactory::mesh_asset_signature(0u, smooth_mesh),
            MeshRenderableFactory::mesh_asset_signature(0u, face_mesh));
}

TEST(MeshQuadUvInterpolationTests,
     MatchesBilinearCornersInteriorAndEdgesAcrossEitherDiagonal) {
  for (const bool parallelogram : {false, true}) {
    SCOPED_TRACE(parallelogram ? "parallelogram" : "warped quad");
    for (const auto &diagonal : kQuadDiagonals) {
      SCOPED_TRACE(diagonal[2]);
      MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
      if (parallelogram) {
        geometry.vertices[2].position = {3.0f, 1.5f, 0.0f};
      }
      geometry.indices.assign(diagonal.begin(), diagonal.end());
      const auto interpolations = build_mesh_quad_uv_interpolations(
          geometry.vertices, geometry.indices, geometry.primitives);
      ASSERT_EQ(interpolations.size(), 4u);
      for (const auto &interpolation : interpolations) {
        ASSERT_TRUE(interpolation.enabled);
        EXPECT_FLOAT_EQ(interpolation.geometry_warp.x,
                        interpolations.front().geometry_warp.x);
        EXPECT_FLOAT_EQ(interpolation.geometry_warp.y,
                        interpolations.front().geometry_warp.y);
        EXPECT_FLOAT_EQ(interpolation.uv_correction.x,
                        interpolations.front().uv_correction.x);
        EXPECT_FLOAT_EQ(interpolation.uv_correction.y,
                        interpolations.front().uv_correction.y);
      }
      bool observed_triangle_distortion = false;
      for (int row = 0; row <= 16; ++row) {
        for (int column = 0; column <= 16; ++column) {
          SCOPED_TRACE(::testing::Message() << "sample " << column << ","
                                           << row);
          const double s = static_cast<double>(column) / 16.0;
          const double t = static_cast<double>(row) / 16.0;
          const auto expected = bilinear_quad_uv(geometry, s, t);
          const auto samples = sample_quad_uv_triangles(
              geometry, interpolations,
              bilinear_quad_position(geometry, s, t));
          ASSERT_FALSE(samples.empty());
          for (const auto &sample : samples) {
            EXPECT_NEAR(sample.corrected_uv.x, expected.x, 0.00002f);
            EXPECT_NEAR(sample.corrected_uv.y, expected.y, 0.00002f);
            observed_triangle_distortion =
                observed_triangle_distortion ||
                std::abs(sample.triangle_uv.x - expected.x) > 0.001f ||
                std::abs(sample.triangle_uv.y - expected.y) > 0.001f;
          }
        }
      }
      EXPECT_TRUE(observed_triangle_distortion);
    }
  }
}

TEST(MeshQuadUvInterpolationTests,
     IndependentAdjacentFacesKeepTheSameLinearUvAlongTheirSharedEdge) {
  for (const auto &diagonal : kQuadDiagonals) {
    MeshGeometryPayload left = make_non_affine_uv_quad_geometry();
    left.indices.assign(diagonal.begin(), diagonal.end());
    MeshGeometryPayload right = left;
    right.vertices[0] = left.vertices[1];
    right.vertices[3] = left.vertices[2];
    right.vertices[1].position = {5.0f, 0.25f, 0.0f};
    right.vertices[2].position = {5.0f, 2.5f, 0.0f};
    right.vertices[1].source_vertex_id = 5u;
    right.vertices[2].source_vertex_id = 6u;
    right.vertices[1].uv = {4.0f, -0.625f};
    right.vertices[2].uv = {3.25f, 2.75f};
    const auto left_interpolations = build_mesh_quad_uv_interpolations(
        left.vertices, left.indices, left.primitives);
    const auto right_interpolations = build_mesh_quad_uv_interpolations(
        right.vertices, right.indices, right.primitives);
    ASSERT_EQ(left_interpolations.size(), 4u);
    ASSERT_EQ(right_interpolations.size(), 4u);
    ASSERT_TRUE(left_interpolations.front().enabled);
    ASSERT_TRUE(right_interpolations.front().enabled);
    for (int step = 0; step <= 32; ++step) {
      const double t = static_cast<double>(step) / 32.0;
      const auto position = bilinear_quad_position(left, 1.0, t);
      const auto expected = bilinear_quad_uv(left, 1.0, t);
      const auto left_samples =
          sample_quad_uv_triangles(left, left_interpolations, position);
      const auto right_samples =
          sample_quad_uv_triangles(right, right_interpolations, position);
      ASSERT_FALSE(left_samples.empty());
      ASSERT_FALSE(right_samples.empty());
      for (const auto &samples : {left_samples, right_samples}) {
        for (const auto &sample : samples) {
          EXPECT_NEAR(sample.corrected_uv.x, expected.x, 0.00002f);
          EXPECT_NEAR(sample.corrected_uv.y, expected.y, 0.00002f);
        }
      }
    }
  }
}

TEST(MeshQuadUvInterpolationTests,
     AffineGeneratedChartsAndIdentityFallbackKeepTheOriginalUvBits) {
  for (const auto &diagonal : kQuadDiagonals) {
    MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
    geometry.indices.assign(diagonal.begin(), diagonal.end());
    for (MeshVertex &vertex : geometry.vertices) {
      vertex.uv = {-1.25f * vertex.position.x +
                       0.5f * vertex.position.y + 0.25f,
                   0.75f * vertex.position.x +
                       2.0f * vertex.position.y - 0.125f};
    }
    expect_quad_uv_fallback(geometry);
  }
  expect_quad_uv_fallback(make_authored_quad_mesh().geometry());
}

TEST(MeshQuadUvInterpolationTests,
     UnsafeQuadGeometryKeepsTriangleInterpolationWithoutPartialAttributes) {
  /// Names an unsupported quad geometry fixture for interpolation fallback checks.
  struct QuadShapeCase final {
    std::string_view name;
    std::array<QVec3, 4> positions;
  };
  const std::array<QuadShapeCase, 5> cases{{
      {"concave", {{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f},
                    {0.5f, 0.5f, 0.0f}, {0.0f, 1.5f, 0.0f}}}},
      {"nonplanar", {{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f},
                      {2.0f, 2.0f, 0.25f}, {0.0f, 1.5f, 0.0f}}}},
      {"collapsed edge", {{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f},
                           {3.0f, 0.0f, 0.0f}, {0.0f, 1.5f, 0.0f}}}},
      {"collinear triangle", {{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f},
                               {2.0f, 0.0f, 0.0f}, {0.0f, 1.5f, 0.0f}}}},
      {"self intersection", {{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f},
                              {0.0f, 1.5f, 0.0f}, {2.0f, 2.0f, 0.0f}}}},
  }};
  for (const auto &test_case : cases) {
    SCOPED_TRACE(test_case.name);
    MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
    for (std::size_t index = 0; index < test_case.positions.size(); ++index) {
      geometry.vertices[index].position = test_case.positions[index];
    }
    expect_quad_uv_fallback(geometry);
  }
}

TEST(MeshQuadUvInterpolationTests,
     TrianglesNgonsAndSharedCompiledVerticesRetainTheirExistingSampling) {
  MeshGeometryPayload triangle = make_non_affine_uv_quad_geometry();
  triangle.indices = {0u, 1u, 2u};
  triangle.primitives.front().index_count = 3u;
  expect_quad_uv_fallback(triangle);

  MeshGeometryPayload ngon = make_non_affine_uv_quad_geometry();
  ngon.vertices.push_back({.position = {-0.5f, 0.75f, 0.0f},
                           .source_vertex_id = 5u});
  ngon.indices = {0u, 1u, 2u, 0u, 2u, 3u, 0u, 3u, 4u};
  ngon.primitives.front().index_count = 9u;
  expect_quad_uv_fallback(ngon);

  MeshGeometryPayload adjacent = make_non_affine_uv_quad_geometry();
  MeshVertex bottom = adjacent.vertices[1];
  bottom.position = {5.0f, 0.0f, 0.0f};
  bottom.source_vertex_id = 5u;
  MeshVertex top = adjacent.vertices[2];
  top.position = {4.0f, 2.0f, 0.0f};
  top.source_vertex_id = 6u;
  adjacent.vertices.push_back(bottom);
  adjacent.vertices.push_back(top);
  adjacent.indices.insert(adjacent.indices.end(), {1u, 4u, 5u, 1u, 5u, 2u});
  adjacent.primitives.push_back({.index_offset = 6u, .index_count = 6u});
  expect_quad_uv_fallback(adjacent);
}

TEST(MeshQuadUvInterpolationTests,
     NonconstantOccurrenceCarriersAndGeometricNormalsDisableTheFace) {
  MeshGeometryPayload carrier = make_non_affine_uv_quad_geometry();
  carrier.vertices[1].uv_carrier[0] += 0.25f;
  expect_quad_uv_fallback(carrier);

  MeshGeometryPayload normal = make_non_affine_uv_quad_geometry();
  normal.vertices[2].uv_geometric_normal = {0.0f, 1.0f, 0.0f};
  expect_quad_uv_fallback(normal);
}

TEST(MeshQuadUvInterpolationTests,
     AnonymousOrFoldedUvFacesKeepTheirExistingTriangleMapping) {
  MeshGeometryPayload anonymous = make_non_affine_uv_quad_geometry();
  anonymous.vertices[0].source_vertex_id = 0u;
  expect_quad_uv_fallback(anonymous);

  MeshGeometryPayload repeated_id = make_non_affine_uv_quad_geometry();
  repeated_id.vertices[1].source_vertex_id =
      repeated_id.vertices[0].source_vertex_id;
  expect_quad_uv_fallback(repeated_id);

  MeshGeometryPayload folded = make_non_affine_uv_quad_geometry();
  std::swap(folded.vertices[2].uv, folded.vertices[3].uv);
  expect_quad_uv_fallback(folded);
}

TEST(MeshQuadUvInterpolationTests,
     OccurrenceAffineUvAdjustmentsReuseTheOriginalCorrectionAttributes) {
  for (const auto &diagonal : kQuadDiagonals) {
    MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
    geometry.indices.assign(diagonal.begin(), diagonal.end());
    const auto interpolations = build_mesh_quad_uv_interpolations(
        geometry.vertices, geometry.indices, geometry.primitives);
    ASSERT_EQ(interpolations.size(), 4u);
    ASSERT_TRUE(interpolations.front().enabled);
    for (MeshVertex &vertex : geometry.vertices) {
      vertex.uv.x += 1.5f * vertex.position.x -
                     0.375f * vertex.position.y +
                     0.25f * vertex.position.z + 2.0f;
      vertex.uv.y += -0.25f * vertex.position.x +
                     2.0f * vertex.position.y -
                     0.5f * vertex.position.z - 1.0f;
    }
    for (int row = 0; row <= 8; ++row) {
      for (int column = 0; column <= 8; ++column) {
        const double s = static_cast<double>(column) / 8.0;
        const double t = static_cast<double>(row) / 8.0;
        const auto expected = bilinear_quad_uv(geometry, s, t);
        const auto samples = sample_quad_uv_triangles(
            geometry, interpolations,
            bilinear_quad_position(geometry, s, t));
        ASSERT_FALSE(samples.empty());
        for (const auto &sample : samples) {
          EXPECT_NEAR(sample.corrected_uv.x, expected.x, 0.00004f);
          EXPECT_NEAR(sample.corrected_uv.y, expected.y, 0.00004f);
        }
      }
    }
  }
}

TEST(MeshQuadUvInterpolationTests,
     AffinePlacementAndScaleDoNotChangeTheBilinearUvChart) {
  for (const float scale : {0.00001f, 1.0f, 100000.0f}) {
    SCOPED_TRACE(scale);
    for (const float reflection : {-1.0f, 1.0f}) {
      SCOPED_TRACE(reflection);
      for (const auto &diagonal : kQuadDiagonals) {
        MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
        geometry.indices.assign(diagonal.begin(), diagonal.end());
        std::vector<QVec3> positions;
        for (const MeshVertex &vertex : geometry.vertices) {
          const auto &position = vertex.position;
          positions.push_back({
              scale * reflection *
                  (2.0f * position.x + 0.5f * position.y + 4.0f),
              scale * (-0.25f * position.x + 1.5f * position.y - 2.0f),
              scale * (0.75f * position.x - 0.5f * position.y + 1.0f),
          });
        }
        const auto interpolations = build_mesh_quad_uv_interpolations(
            geometry.vertices, geometry.indices, geometry.primitives,
            positions);
        ASSERT_EQ(interpolations.size(), 4u);
        ASSERT_TRUE(std::ranges::all_of(interpolations, [](const auto &value) {
          return value.enabled;
        }));
        for (int row = 0; row <= 4; ++row) {
          for (int column = 0; column <= 4; ++column) {
            const double s = static_cast<double>(column) / 4.0;
            const double t = static_cast<double>(row) / 4.0;
            const auto expected = bilinear_quad_uv(geometry, s, t);
            const auto samples = sample_quad_uv_triangles(
                geometry, interpolations,
                bilinear_quad_position(geometry, s, t, positions), positions);
            ASSERT_FALSE(samples.empty());
            for (const auto &sample : samples) {
              EXPECT_NEAR(sample.corrected_uv.x, expected.x, 0.00004f);
              EXPECT_NEAR(sample.corrected_uv.y, expected.y, 0.00004f);
            }
          }
        }
      }
    }
  }
}

TEST(MeshQuadUvInterpolationTests,
     PositionOverridesRebuildTheWarpForNonAffinePreviewChanges) {
  MeshGeometryPayload geometry = make_non_affine_uv_quad_geometry();
  const std::array<QVec3, 4> positions{{
      geometry.vertices[0].position, geometry.vertices[1].position,
      {3.25f, 2.75f, 0.0f}, geometry.vertices[3].position,
  }};
  const auto interpolations = build_mesh_quad_uv_interpolations(
      geometry.vertices, geometry.indices, geometry.primitives, positions);
  ASSERT_EQ(interpolations.size(), 4u);
  ASSERT_TRUE(interpolations.front().enabled);
  for (const QVec2 chart_point : {QVec2{0.125f, 0.25f}, QVec2{0.31f, 0.67f},
                                 QVec2{0.75f, 0.5f}, QVec2{0.875f, 0.875f}}) {
    const auto expected =
        bilinear_quad_uv(geometry, chart_point.x, chart_point.y);
    const auto samples = sample_quad_uv_triangles(
        geometry, interpolations,
        bilinear_quad_position(geometry, chart_point.x, chart_point.y,
                               positions),
        positions);
    ASSERT_FALSE(samples.empty());
    for (const auto &sample : samples) {
      EXPECT_NEAR(sample.corrected_uv.x, expected.x, 0.00002f);
      EXPECT_NEAR(sample.corrected_uv.y, expected.y, 0.00002f);
    }
  }
}

TEST(RenderSceneSnapshotTests, BuildsVisibleMeshLightAndMaterialRecords) {
  SceneDocument document;
  AssetRegistry assets;

  TextureAsset texture;
  texture.name = "Snapshot Base Color";
  texture.color_space = TextureColorSpace::Srgb;
  const AssetId base_color_texture = assets.add_texture(texture);

  MaterialDocument material_document;
  material_document.name = "Snapshot Material";
  material_document.texture_slots.emplace(material_texture_slot::kBaseColor,
                                          base_color_texture);
  const AssetId material = assets.add_material(std::move(material_document));
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId renderable =
      document.create_entity("Renderable", EntityKind::Mesh);
  document.set_mesh(renderable, MeshComponent{
                                    .mesh = mesh,
                                    .cast_shadows = false,
                                    .double_sided = true,
                                });
  document.set_material(renderable, MaterialComponent{.material = material});

  const EntityId missing_mesh =
      document.create_entity("Missing Mesh", EntityKind::Mesh);
  document.set_mesh(missing_mesh, MeshComponent{.mesh = mesh + 1000u});

  const EntityId hidden = document.create_entity("Hidden Mesh", EntityKind::Mesh);
  document.set_mesh(hidden, MeshComponent{.mesh = mesh});
  document.set_visible(hidden, false);

  const EntityId light = document.create_entity("Light", EntityKind::Light);
  const LightComponent light_component{
      .type = LightType::Point,
      .color = {0.25f, 0.5f, 0.75f},
      .intensity = 3.0f,
      .range = 7.0f,
      .casts_shadows = true,
  };
  document.set_light(light, light_component);

  const RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets);

  EXPECT_EQ(snapshot.scene_revision, document.revision());
  EXPECT_EQ(snapshot.asset_revision, assets.revision());
  ASSERT_EQ(snapshot.meshes.size(), 2u);
  const auto renderable_entry =
      std::ranges::find_if(snapshot.meshes, [renderable](const auto &entry) {
        return entry.entity_id == renderable;
      });
  ASSERT_NE(renderable_entry, snapshot.meshes.end());
  EXPECT_TRUE(renderable_entry->renderable());
  EXPECT_EQ(renderable_entry->mesh.mesh_id, mesh);
  EXPECT_EQ(renderable_entry->mesh.mesh_revision,
            assets.mesh_revision(mesh).value);
  ASSERT_NE(assets.mesh(mesh), nullptr);
  EXPECT_EQ(renderable_entry->mesh.vertices.data(),
            assets.mesh(mesh)->geometry().vertices.data());
  EXPECT_EQ(renderable_entry->material_id, material);
  EXPECT_EQ(renderable_entry->material_revision,
            render_material_binding_signature(renderable_entry->material));
  EXPECT_FALSE(renderable_entry->cast_shadows());
  EXPECT_TRUE(renderable_entry->double_sided());
  ASSERT_TRUE(renderable_entry->material.present);
  EXPECT_EQ(renderable_entry->material.primary_material.material_id, material);
  const RenderTextureAssetSnapshot *texture_snapshot =
      renderable_entry->material.primary_material.texture_for_slot(
          material_texture_slot::kBaseColor);
  ASSERT_NE(texture_snapshot, nullptr);
  EXPECT_TRUE(texture_snapshot->present);
  EXPECT_EQ(texture_snapshot->texture_id, base_color_texture);
  EXPECT_TRUE(snapshot.live_mesh_entities.contains(renderable));

  const auto missing_entry =
      std::ranges::find_if(snapshot.meshes, [missing_mesh](const auto &entry) {
        return entry.entity_id == missing_mesh;
      });
  ASSERT_NE(missing_entry, snapshot.meshes.end());
  EXPECT_FALSE(missing_entry->renderable());
  EXPECT_FALSE(missing_entry->mesh.present);
  EXPECT_FALSE(snapshot.live_mesh_entities.contains(missing_mesh));
  EXPECT_FALSE(snapshot.live_mesh_entities.contains(hidden));

  ASSERT_EQ(snapshot.lights.size(), 1u);
  EXPECT_EQ(snapshot.lights.front().entity_id, light);
  EXPECT_EQ(snapshot.lights.front().light, light_component);
  EXPECT_EQ(snapshot.lights.front().rebuild_revision,
            LightRenderableCache::light_signature(light_component));
  EXPECT_TRUE(snapshot.live_light_entities.contains(light));
}

TEST(RenderSceneSnapshotTests,
     CurvePublicationRefreshAdvancesStampsWithoutTouchingMainRecords) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh_asset = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId material_asset =
      assets.add_material(make_default_quader_material("Curve Gate Material"));
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId mesh = document.create_entity("Retained Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_asset}));
  ASSERT_TRUE(document.set_material(mesh, {.material = material_asset}));
  const EntityId light = document.create_entity("Retained Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(light, {.type = LightType::Point}));
  const EntityId curve = document.create_entity("Edited Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, {.curve = curve_asset}));
  const RenderSceneBindingKey binding{
      .scene_session_id = 17u,
      .activation_generation = 19u,
      .binding_revision = 23u,
  };
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = 1u, .scene_binding = binding});
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  ASSERT_EQ(snapshot.lights.size(), 1u);
  const auto *const mesh_records = snapshot.meshes.data();
  const auto *const light_records = snapshot.lights.data();
  const auto geometry = snapshot.meshes.front().mesh.geometry_payload;
  const std::uint64_t old_scene_revision = snapshot.scene_revision;
  const std::uint64_t old_curve_revision = snapshot.curve_binding_revision;
  const std::uint64_t old_render_revision = snapshot.render_component_revision;
  const std::uint64_t old_asset_revision = snapshot.asset_revision;

  CurveAsset replacement = *assets.curve(curve_asset);
  modeling::CurveDocument edited = *replacement.document;
  const modeling::CurveControlPoint point = edited.points().front();
  ASSERT_TRUE(edited.set_point(
      point.id,
      {.position = {point.position.x, point.position.y + 1.0f,
                    point.position.z},
       .incoming_handle = point.incoming_handle,
       .outgoing_handle = point.outgoing_handle}).ok());
  replacement.document =
      std::make_shared<const modeling::CurveDocument>(std::move(edited));
  ASSERT_TRUE(assets.replace_curve(curve_asset, std::move(replacement)));
  ASSERT_TRUE(document.notify_curve_payload_changed(curve));

  ASSERT_TRUE(try_refresh_render_scene_snapshot_curve_publication(
      snapshot, document, assets, 2u, binding));
  EXPECT_EQ(snapshot.frame_index, 2u);
  EXPECT_GT(snapshot.scene_revision, old_scene_revision);
  EXPECT_GT(snapshot.curve_binding_revision, old_curve_revision);
  EXPECT_GT(snapshot.render_component_revision, old_render_revision);
  EXPECT_GT(snapshot.asset_revision, old_asset_revision);
  EXPECT_EQ(snapshot.meshes.data(), mesh_records);
  EXPECT_EQ(snapshot.lights.data(), light_records);
  EXPECT_EQ(snapshot.meshes.front().mesh.geometry_payload, geometry);
  EXPECT_TRUE(snapshot.reused_previous_records);
  EXPECT_TRUE(snapshot.dirty.scene_changed);
  EXPECT_FALSE(snapshot.dirty.render_components_changed);
  EXPECT_FALSE(snapshot.dirty.assets_changed);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      snapshot, document, assets, binding));
}

TEST(RenderSceneSnapshotTests,
     CurvePublicationRefreshRejectsEveryMainSceneDependencyChange) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh_asset = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId material_asset =
      assets.add_material(make_default_quader_material("Bound Material"));
  const AssetId alternate_material =
      assets.add_material(make_default_quader_material("Alternate Material"));
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId mesh = document.create_entity("Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_asset}));
  ASSERT_TRUE(document.set_material(mesh, {.material = material_asset}));
  const EntityId light = document.create_entity("Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(light, {.type = LightType::Point}));
  const EntityId curve = document.create_entity("Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, {.curve = curve_asset}));
  const RenderSceneBindingKey binding{
      .scene_session_id = 29u,
      .activation_generation = 31u,
      .binding_revision = 37u,
  };
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = 1u, .scene_binding = binding});

  float edit_offset = 1.0f;
  const auto publish_curve = [&]() {
    CurveAsset replacement = *assets.curve(curve_asset);
    modeling::CurveDocument edited = *replacement.document;
    const modeling::CurveControlPoint point = edited.points().front();
    const auto changed = edited.set_point(
        point.id,
        {.position = {point.position.x, edit_offset++, point.position.z},
         .incoming_handle = point.incoming_handle,
         .outgoing_handle = point.outgoing_handle});
    EXPECT_TRUE(changed.ok());
    replacement.document =
        std::make_shared<const modeling::CurveDocument>(std::move(edited));
    EXPECT_TRUE(assets.replace_curve(curve_asset, std::move(replacement)));
    EXPECT_TRUE(document.notify_curve_payload_changed(curve));
  };
  std::uint64_t frame_index = 2u;
  const auto expect_rejected_and_rebuild = [&](std::string_view cause) {
    SCOPED_TRACE(std::string(cause));
    const std::uint64_t retained_scene_revision = snapshot.scene_revision;
    const auto *const retained_mesh_records = snapshot.meshes.data();
    EXPECT_FALSE(try_refresh_render_scene_snapshot_curve_publication(
        snapshot, document, assets, frame_index, binding));
    EXPECT_EQ(snapshot.scene_revision, retained_scene_revision);
    EXPECT_EQ(snapshot.meshes.data(), retained_mesh_records);
    snapshot = build_render_scene_snapshot(
        document, assets,
        {.previous = &snapshot,
         .frame_index = frame_index++,
         .scene_binding = binding});
    EXPECT_FALSE(snapshot.reused_previous_records);
  };

  publish_curve();
  ASSERT_TRUE(assets.replace_mesh(mesh_asset, make_authored_quad_mesh()));
  expect_rejected_and_rebuild("mesh payload");

  publish_curve();
  ASSERT_TRUE(assets.replace_material(
      material_asset, make_default_quader_material("Edited Material")));
  expect_rejected_and_rebuild("material payload");

  publish_curve();
  LightComponent changed_light = *document.light(light);
  changed_light.intensity += 1.0f;
  ASSERT_TRUE(document.set_light(light, changed_light));
  expect_rejected_and_rebuild("light component");

  publish_curve();
  ASSERT_TRUE(document.set_material(mesh, {.material = alternate_material}));
  expect_rejected_and_rebuild("material binding");

  publish_curve();
  ASSERT_TRUE(document.set_visible(mesh, false));
  expect_rejected_and_rebuild("visibility");
  ASSERT_TRUE(document.set_visible(mesh, true));
  snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = frame_index++, .scene_binding = binding});

  publish_curve();
  ASSERT_TRUE(document.set_transform(
      mesh, {.position = {3.0f, 4.0f, 5.0f}}));
  expect_rejected_and_rebuild("transform");

  publish_curve();
  RenderSceneBindingKey replacement_binding = binding;
  ++replacement_binding.activation_generation;
  EXPECT_FALSE(try_refresh_render_scene_snapshot_curve_publication(
      snapshot, document, assets, frame_index, replacement_binding));
}

TEST(RenderSceneSnapshotTests,
     LocalVisibilityFiltersMeshesLightsAndInvalidatesRecordReuse) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId isolated_mesh =
      document.create_entity("Local Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(isolated_mesh, MeshComponent{.mesh = mesh}));
  const EntityId other_mesh =
      document.create_entity("Other Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(
      other_mesh, MeshComponent{.mesh = mesh, .double_sided = true}));
  const EntityId persistently_hidden_mesh =
      document.create_entity("Persistently Hidden Local Mesh",
                             EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(persistently_hidden_mesh,
                                MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_visible(persistently_hidden_mesh, false));

  const EntityId isolated_light =
      document.create_entity("Local Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      isolated_light, LightComponent{.type = LightType::Point}));
  const EntityId other_light =
      document.create_entity("Other Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      other_light, LightComponent{.type = LightType::Directional}));

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 5> baseline{
      isolated_mesh, other_mesh, persistently_hidden_mesh, isolated_light,
      other_light};
  const std::array<ViewportLocalVisibilityEntityId, 3> members{
      isolated_mesh, persistently_hidden_mesh, isolated_light};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  const ViewportLocalVisibilitySnapshot entered =
      workspace.local_visibility_snapshot();

  const RenderSceneSnapshot isolated = build_render_scene_snapshot(
      document, assets, {.local_visibility = &entered});
  EXPECT_EQ(isolated.local_visibility_revision, entered.revision());
  ASSERT_EQ(isolated.meshes.size(), 1u);
  EXPECT_EQ(isolated.meshes.front().entity_id, isolated_mesh);
  ASSERT_EQ(isolated.lights.size(), 1u);
  EXPECT_EQ(isolated.lights.front().entity_id, isolated_light);
  EXPECT_FALSE(isolated.live_mesh_entities.contains(other_mesh));
  EXPECT_FALSE(isolated.live_mesh_entities.contains(persistently_hidden_mesh));
  EXPECT_FALSE(isolated.live_light_entities.contains(other_light));
  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      isolated, document, assets, {}, 0u, {}, &entered));

  const RenderSceneSnapshot stable = build_render_scene_snapshot(
      document, assets,
      {.previous = &isolated, .local_visibility = &entered});
  EXPECT_TRUE(stable.reused_previous_records);
  EXPECT_FALSE(stable.dirty.any_dirty());

  ASSERT_TRUE(workspace.exit_local_visibility());
  const ViewportLocalVisibilitySnapshot exited =
      workspace.local_visibility_snapshot();
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      stable, document, assets, {}, 0u, {}, &exited));
  const RenderSceneSnapshot restored = build_render_scene_snapshot(
      document, assets,
      {.previous = &stable, .local_visibility = &exited});
  EXPECT_FALSE(restored.reused_previous_records);
  EXPECT_TRUE(restored.dirty.local_visibility_changed);
  EXPECT_EQ(restored.local_visibility_revision, exited.revision());
  EXPECT_EQ(restored.meshes.size(), 2u);
  EXPECT_EQ(restored.lights.size(), 2u);
  EXPECT_FALSE(restored.live_mesh_entities.contains(persistently_hidden_mesh));
}

TEST(RenderHotPathTests, SceneSnapshotSourceMatchTracksOwnerRevisions) {
  SceneDocument document;
  AssetRegistry assets;
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(document, assets);

  EXPECT_TRUE(render_scene_snapshot_matches_sources(snapshot, document,
                                                    assets));

  const EntityId entity =
      document.create_entity("Revision Probe", EntityKind::Empty);
  EXPECT_FALSE(render_scene_snapshot_matches_sources(snapshot, document,
                                                     assets));

  snapshot = build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(snapshot, document,
                                                    assets));

  document.set_transform(
      entity, TransformComponent{.position = {3.0f, 2.0f, 1.0f}});
  EXPECT_FALSE(render_scene_snapshot_matches_sources(snapshot, document,
                                                     assets));

  snapshot = build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(snapshot, document,
                                                    assets));

  (void)assets.add_mesh(make_overlay_triangle_mesh());
  EXPECT_FALSE(render_scene_snapshot_matches_sources(snapshot, document,
                                                      assets));
}

TEST(RenderSceneSnapshotTests,
     ResolvesProjectMaterialsAndCarriesStableTextureContent) {
  const ProjectAssetId material_id =
      *ProjectAssetId::parse("11111111-1111-4111-8111-111111111111");
  const ProjectAssetId texture_id =
      *ProjectAssetId::parse("22222222-2222-4222-8222-222222222222");
  const auto encoded_texture =
      std::make_shared<const std::vector<std::byte>>(
          std::vector<std::byte>{std::byte{0x89}, std::byte{0x50},
                                 std::byte{0x4e}, std::byte{0x47}});
  TestProjectMaterialResolver resolver;
  resolver.revision_value = 17u;
  resolver.material = RenderProjectMaterialSnapshot{
      .evaluation =
          RenderMaterialEvaluationSnapshot{
              .project_material_id = material_id,
              .kind = RenderMaterialEvaluationKind::Standard,
              .identity_revision = 19u,
              .content_revision = 23u,
              .payload = RenderStandardMaterialEvaluationSnapshot{
                  .base_color = {0.25f, 0.5f, 0.75f},
                  .roughness = 0.35f,
                  .metallic = 0.65f,
                  .texture_transform =
                      RenderStandardMaterialTextureTransformSnapshot{
                          .center = {0.25f, 0.75f},
                          .scale = {2.0f, 0.5f},
                          .offset = {-0.1f, 0.2f},
                          .rotation_degrees = 45.0f,
                      },
                  .textures = {RenderMaterialTextureEvaluationSnapshot{
                      .project_texture_id = texture_id,
                      .identity_revision = 27u,
                      .content_revision = 31u,
                      .color_space = TextureColorSpace::Srgb,
                      .encoded_file_bytes = encoded_texture,
                  }},
                  .diffuse_model = MaterialDiffuseModel::Lambert,
              },
          },
      .catalog_revision = 29u,
  };

  const MaterialReference reference = MaterialReference::project(material_id);
  const MaterialComponent component{.material = reference};
  const RenderMaterialBindingSnapshot binding =
      build_render_material_binding_snapshot(AssetRegistry{}, &component,
                                             &resolver);

  ASSERT_TRUE(binding.present);
  EXPECT_EQ(binding.component.material, reference);
  EXPECT_EQ(binding.primary_material.reference, reference);
  EXPECT_EQ(binding.primary_material.material_id, kInvalidAssetId);
  EXPECT_TRUE(binding.primary_material.present);
  EXPECT_EQ(binding.primary_material.revision, 23u);
  EXPECT_EQ(binding.primary_material.project_catalog_revision, 29u);
  EXPECT_FLOAT_EQ(binding.primary_material.document.base_color.x, 0.25f);
  EXPECT_FLOAT_EQ(binding.primary_material.document.base_color.y, 0.5f);
  EXPECT_FLOAT_EQ(binding.primary_material.document.base_color.z, 0.75f);
  EXPECT_FLOAT_EQ(binding.primary_material.document.roughness, 0.35f);
  EXPECT_FLOAT_EQ(binding.primary_material.document.metallic, 0.65f);
  EXPECT_EQ(binding.primary_material.document.diffuse_model,
            MaterialDiffuseModel::Lambert);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.center.x,
                  0.25f);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.center.y,
                  0.75f);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.scale.x, 2.0f);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.scale.y, 0.5f);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.offset.x,
                  -0.1f);
  EXPECT_FLOAT_EQ(binding.primary_material.texture_transform.offset.y, 0.2f);
  EXPECT_FLOAT_EQ(
      binding.primary_material.texture_transform.rotation_degrees, 45.0f);

  const RenderTextureAssetSnapshot *texture =
      binding.primary_material.texture_for_slot(
          material_texture_slot::kBaseColor);
  ASSERT_NE(texture, nullptr);
  EXPECT_TRUE(texture->project_owned());
  EXPECT_TRUE(texture->present);
  EXPECT_EQ(texture->project_texture_id, texture_id);
  EXPECT_EQ(texture->content_revision, 31u);
  EXPECT_EQ(texture->texture.color_space, TextureColorSpace::Srgb);
  EXPECT_EQ(texture->encoded_file_bytes, encoded_texture);

  const std::uint64_t original_signature = binding.revision;
  resolver.material->catalog_revision += 1u;
  const RenderMaterialBindingSnapshot changed_catalog =
      build_render_material_binding_snapshot(AssetRegistry{}, &component,
                                             &resolver);
  EXPECT_NE(changed_catalog.revision, original_signature);

  resolver.material->evaluation.standard_material()
      ->textures.front()
      .content_revision += 1u;
  const RenderMaterialBindingSnapshot changed_texture =
      build_render_material_binding_snapshot(AssetRegistry{}, &component,
                                             &resolver);
  EXPECT_NE(changed_texture.revision, original_signature);

  resolver.material.reset();
  const RenderMaterialBindingSnapshot unresolved =
      build_render_material_binding_snapshot(AssetRegistry{}, &component,
                                             &resolver);
  EXPECT_TRUE(unresolved.present);
  EXPECT_EQ(unresolved.primary_material.reference, reference);
  EXPECT_FALSE(unresolved.primary_material.present);
  EXPECT_NE(unresolved.revision, 0u);
}

TEST(RenderSceneSnapshotTests,
     LightPropertyRefreshAvoidsMaterialResolutionAndMatchesFullExtraction) {
  SceneDocument document;
  AssetRegistry assets;
  const ProjectAssetId material_id =
      *ProjectAssetId::parse("33333333-3333-4333-8333-333333333333");
  TestProjectMaterialResolver resolver;
  resolver.material = RenderProjectMaterialSnapshot{
      .evaluation = RenderMaterialEvaluationSnapshot{
          .project_material_id = material_id,
          .kind = RenderMaterialEvaluationKind::Standard,
          .identity_revision = 2u,
          .content_revision = 3u,
          .payload = RenderStandardMaterialEvaluationSnapshot{},
      },
      .catalog_revision = 4u,
  };
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity = document.create_entity("Radius Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_material(entity, MaterialComponent{
      .material = MaterialReference::project(material_id)}));
  const EntityId light = document.create_entity("Radius Light", EntityKind::Light);
  LightComponent component{.type = LightType::Point};
  component.range = 2.0f;
  ASSERT_TRUE(document.set_light(light, component));
  auto retained = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  ASSERT_EQ(retained.meshes.size(), 1u);
  ASSERT_EQ(retained.lights.size(), 1u);
  ASSERT_GT(resolver.resolve_count, 0u);
  const auto initial_resolutions = resolver.resolve_count;
  const auto *const mesh_storage = retained.meshes.data();
  const auto *const geometry_storage = retained.meshes.front().mesh.vertices.data();
  const auto geometry_owner = retained.meshes.front().mesh.geometry_payload;
  const auto *const material_storage = &retained.meshes.front().material;
  for (std::uint64_t sample = 0u; sample < 128u; ++sample) {
    component.range = 2.0f + static_cast<float>(sample + 1u) * 0.05f;
    ASSERT_TRUE(document.set_light(light, component));
    ASSERT_TRUE(try_refresh_render_scene_snapshot_light_properties(
        retained, document, assets, sample, {}, resolver.revision()));
    EXPECT_EQ(resolver.resolve_count, initial_resolutions);
    EXPECT_EQ(retained.meshes.data(), mesh_storage);
    EXPECT_EQ(retained.meshes.front().mesh.vertices.data(), geometry_storage);
    EXPECT_EQ(retained.meshes.front().mesh.geometry_payload, geometry_owner);
    EXPECT_EQ(&retained.meshes.front().material, material_storage);
  }
  const auto full = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  EXPECT_GT(resolver.resolve_count, initial_resolutions);
  EXPECT_EQ(static_cast<const RenderSourceStamp &>(retained),
            static_cast<const RenderSourceStamp &>(full));
  ASSERT_EQ(full.lights.size(), retained.lights.size());
  EXPECT_EQ(retained.lights.front().light, full.lights.front().light);
  EXPECT_EQ(retained.lights.front().rebuild_revision,
            full.lights.front().rebuild_revision);
  EXPECT_EQ(retained.lights.front().world_transform.values,
            full.lights.front().world_transform.values);
  EXPECT_EQ(retained.meshes.front().mesh.geometry_payload,
            full.meshes.front().mesh.geometry_payload);
  EXPECT_EQ(retained.meshes.front().material.revision,
            full.meshes.front().material.revision);
  EXPECT_EQ(retained.meshes.front().world_transform.values,
            full.meshes.front().world_transform.values);
  EXPECT_EQ(retained.live_mesh_entities, full.live_mesh_entities);
  EXPECT_EQ(retained.live_light_entities, full.live_light_entities);
}

TEST(RenderSceneSnapshotTests,
     ProjectMaterialRevisionInvalidatesReuseAndTransformRefresh) {
  SceneDocument document;
  AssetRegistry assets;
  const ProjectAssetId material_id =
      *ProjectAssetId::parse("33333333-3333-4333-8333-333333333333");
  TestProjectMaterialResolver resolver;
  resolver.revision_value = 41u;
  resolver.material = RenderProjectMaterialSnapshot{
      .evaluation =
          RenderMaterialEvaluationSnapshot{
              .project_material_id = material_id,
              .kind = RenderMaterialEvaluationKind::Standard,
              .identity_revision = 42u,
              .content_revision = 43u,
              .payload = RenderStandardMaterialEvaluationSnapshot{},
          },
      .catalog_revision = 47u,
  };

  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Project Material Mesh", EntityKind::Mesh);
  document.set_mesh(entity, MeshComponent{.mesh = mesh});
  document.set_material(
      entity,
      MaterialComponent{.material = MaterialReference::project(material_id)});

  RenderSceneSnapshot initial = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  ASSERT_EQ(initial.meshes.size(), 1u);
  EXPECT_EQ(initial.project_material_revision, 41u);
  EXPECT_TRUE(initial.meshes.front().material_reference.project_owned());
  EXPECT_EQ(initial.meshes.front().material.primary_material.document.diffuse_model,
            MaterialDiffuseModel::Burley);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(initial, document, assets,
                                                    {}, 41u));
  EXPECT_FALSE(render_scene_snapshot_matches_sources(initial, document, assets,
                                                     {}, 42u));

  const RenderSceneSnapshot unchanged = build_render_scene_snapshot(
      document, assets,
      {.previous = &initial, .project_material_resolver = &resolver});
  EXPECT_TRUE(unchanged.reused_previous_records);
  EXPECT_FALSE(unchanged.dirty.any_dirty());

  resolver.revision_value = 53u;
  resolver.material->evaluation.content_revision = 59u;
  resolver.material->evaluation.standard_material()->diffuse_model =
      MaterialDiffuseModel::Lambert;
  const RenderSceneSnapshot changed = build_render_scene_snapshot(
      document, assets,
      {.previous = &unchanged, .project_material_resolver = &resolver});
  EXPECT_FALSE(changed.reused_previous_records);
  EXPECT_TRUE(changed.dirty.project_materials_changed);
  EXPECT_TRUE(changed.meshes.front().material_dirty);
  EXPECT_FALSE(changed.meshes.front().mesh_dirty);
  EXPECT_EQ(changed.meshes.front().mesh.geometry_payload,
            initial.meshes.front().mesh.geometry_payload);
  EXPECT_EQ(changed.meshes.front().material.primary_material.document.diffuse_model,
            MaterialDiffuseModel::Lambert);
  const auto resolutions = resolver.resolve_count;
  const RenderSceneSnapshot retained = build_render_scene_snapshot(
      document, assets,
      {.previous = &changed, .project_material_resolver = &resolver});
  EXPECT_TRUE(retained.reused_previous_records);
  EXPECT_FALSE(retained.dirty.any_dirty());
  EXPECT_EQ(resolver.resolve_count, resolutions);

  document.set_transform(
      entity, TransformComponent{.position = {1.0f, 2.0f, 3.0f}});
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      initial, document, assets, 2u, {}, resolver.revision_value));
}

TEST(RenderSceneSnapshotTests,
     ToolPreviewSnapshotsKeepEmbeddedMaterialBindings) {
  AssetRegistry assets;
  MaterialDocument definition = make_default_quader_material("Preview Material");
  definition.diffuse_model = MaterialDiffuseModel::Lambert;
  const AssetId material = assets.add_material(definition);
  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {ToolPreviewMesh{
              .source_entity = 7u,
              .material = MaterialComponent{.material = material},
              .mesh = make_overlay_triangle_mesh(),
          }},
          assets);

  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records.front().material_id, material);
  ASSERT_TRUE(records.front().material.present);
  EXPECT_EQ(records.front().material.component.material, material);
  EXPECT_TRUE(
      records.front().material.primary_material.reference.embedded());
  EXPECT_EQ(records.front().material.primary_material.material_id, material);
  EXPECT_EQ(records.front().material.primary_material.document.diffuse_model,
            MaterialDiffuseModel::Lambert);
  EXPECT_EQ(records.front().material_revision,
            records.front().material.revision);
}

TEST(RenderSceneSnapshotTests, ToolPreviewSnapshotsPreserveCullingPolicy) {
  AssetRegistry assets;
  QMat4 mapping;
  mapping.values[12] = 3.0f;
  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {
              ToolPreviewMesh{.source_entity = 7u,
                              .mesh = make_overlay_triangle_mesh(),
                              .sampling_from_local = mapping,
                              .double_sided = false,
                              .suppress_source = true},
              ToolPreviewMesh{.source_entity = 8u,
                              .mesh = make_overlay_triangle_mesh(),
                              .instance_transforms = {QMat4{}, QMat4{}},
                              .instance_sampling_from_local = {mapping, QMat4{}},
                              .double_sided = true,
                              .suppress_source = true},
              ToolPreviewMesh{.source_entity = kInvalidEntityId,
                              .mesh = make_overlay_triangle_mesh()},
          },
          assets);

  ASSERT_EQ(records.size(), 3u);
  EXPECT_FALSE(records[0].double_sided);
  EXPECT_TRUE(records[1].double_sided);
  EXPECT_TRUE(records[2].double_sided)
      << "source-less generated previews retain their two-sided default";
  EXPECT_EQ(records[0].sampling_from_local.values, mapping.values);
  ASSERT_EQ(records[1].instance_sampling_from_local.size(), 2u);
  EXPECT_EQ(records[1].instance_sampling_from_local[0].values, mapping.values);
  EXPECT_EQ(records[1].instance_sampling_from_local[1].values, QMat4{}.values);
}

TEST(RenderSceneSnapshotTests,
     ToolPreviewSnapshotsPreserveProjectMaterialsAndSlotOverrides) {
  AssetRegistry assets;
  const AssetId first_slot_material =
      assets.add_material(make_default_quader_material("Preview Slot A"));
  const AssetId second_slot_material =
      assets.add_material(make_default_quader_material("Preview Slot B"));
  const ProjectAssetId project_material =
      *ProjectAssetId::parse("44444444-4444-4444-8444-444444444444");
  TestProjectMaterialResolver resolver;
  resolver.revision_value = 61u;
  resolver.material = RenderProjectMaterialSnapshot{
      .evaluation =
          RenderMaterialEvaluationSnapshot{
              .project_material_id = project_material,
              .kind = RenderMaterialEvaluationKind::Standard,
              .identity_revision = 67u,
              .content_revision = 71u,
              .payload = RenderStandardMaterialEvaluationSnapshot{
                  .base_color = {0.2f, 0.4f, 0.8f},
                  .roughness = 0.3f,
                  .metallic = 0.7f,
              },
          },
      .catalog_revision = 73u,
  };
  const MaterialComponent first_material{
      .material = MaterialReference::project(project_material),
      .material_slots = {{3u, first_slot_material}},
  };
  const MaterialComponent second_material{
      .material = MaterialReference::project(project_material),
      .material_slots = {{3u, second_slot_material}},
  };

  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {
              ToolPreviewMesh{
                  .source_entity = 7u,
                  .material = first_material,
                  .mesh = make_overlay_triangle_mesh(),
              },
              ToolPreviewMesh{
                  .source_entity = 8u,
                  .material = second_material,
                  .mesh = make_overlay_triangle_mesh(),
              },
          },
          assets, nullptr, &resolver);

  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records.front().material_reference, first_material.material);
  EXPECT_EQ(records.front().material_id, kInvalidAssetId);
  EXPECT_EQ(records.front().material.component, first_material);
  ASSERT_TRUE(records.front().material.primary_material.present);
  EXPECT_EQ(records.front().material.primary_material.reference,
            first_material.material);
  const RenderMaterialAssetSnapshot *first_slot =
      records.front().material.material_for_slot(3u);
  ASSERT_NE(first_slot, nullptr);
  EXPECT_TRUE(first_slot->reference.embedded());
  EXPECT_EQ(first_slot->material_id, first_slot_material);
  EXPECT_EQ(records.front().material_revision,
            records.front().material.revision);
  EXPECT_EQ(records.back().material.component, second_material);
  const RenderMaterialAssetSnapshot *second_slot =
      records.back().material.material_for_slot(3u);
  ASSERT_NE(second_slot, nullptr);
  EXPECT_EQ(second_slot->material_id, second_slot_material);
  EXPECT_NE(records.front().material_revision,
            records.back().material_revision);
}

TEST(RenderSceneSnapshotTests,
     LocalVisibilityFiltersOwnedToolPreviewsButKeepsCreationPreviews) {
  AssetRegistry assets;
  const AssetId material =
      assets.add_material(make_default_quader_material("Local Preview"));
  ViewportWorkspaceService workspace;
  constexpr std::array<ViewportLocalVisibilityEntityId, 2> kBaseline{7u,
                                                                    8u};
  constexpr std::array<ViewportLocalVisibilityEntityId, 1> kMembers{7u};
  ASSERT_TRUE(workspace.enter_local_visibility(kBaseline, kMembers));
  const ViewportLocalVisibilitySnapshot local =
      workspace.local_visibility_snapshot();

  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {
              ToolPreviewMesh{.source_entity = 7u,
                              .material =
                                  MaterialComponent{.material = material},
                              .mesh = make_overlay_triangle_mesh()},
              ToolPreviewMesh{.source_entity = 8u,
                              .material =
                                  MaterialComponent{.material = material},
                              .mesh = make_overlay_triangle_mesh()},
              ToolPreviewMesh{.source_entity = kInvalidEntityId,
                              .material =
                                  MaterialComponent{.material = material},
                              .mesh = make_overlay_triangle_mesh()},
          },
          assets, &local);

  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records[0].source_entity, 7u);
  EXPECT_EQ(records[1].source_entity, kInvalidEntityId);
}

TEST(RenderSceneSnapshotTests,
     LocalVisibilityFiltersInstanceEditPreviewsByPublicOccurrence) {
  AssetRegistry assets;
  const AssetId material =
      assets.add_material(make_default_quader_material("Instance Preview"));
  ViewportWorkspaceService workspace;
  constexpr EntityId kOccurrence = 7u;
  constexpr EntityId kCanonicalMember = 8u;
  constexpr std::array<ViewportLocalVisibilityEntityId, 2> kBaseline{
      kOccurrence, kCanonicalMember};
  constexpr std::array<ViewportLocalVisibilityEntityId, 1> kMembers{
      kOccurrence};
  ASSERT_TRUE(workspace.enter_local_visibility(kBaseline, kMembers));
  const ViewportLocalVisibilitySnapshot local =
      workspace.local_visibility_snapshot();

  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {ToolPreviewMesh{.source_entity = kCanonicalMember,
                           .visibility_entity = kOccurrence,
                           .material =
                               MaterialComponent{.material = material},
                           .mesh = make_overlay_triangle_mesh()}},
          assets, &local);

  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records.front().source_entity, kCanonicalMember);
}

TEST(RenderHotPathTests, MeshSurfaceGridSignatureIsNarrowAndStable) {
  ViewportSettings settings = ViewportSettings::defaults();
  const std::uint64_t initial = mesh_surface_grid_signature(1.0f, settings);

  ASSERT_TRUE(settings.set_bool_value(
      ViewportBoolSettingId::ShowInfo,
      !settings.bool_value(ViewportBoolSettingId::ShowInfo)));
  EXPECT_EQ(mesh_surface_grid_signature(1.0f, settings), initial);

  ASSERT_TRUE(settings.set_bool_value(
      ViewportBoolSettingId::ShowMeshGrid,
      !settings.bool_value(ViewportBoolSettingId::ShowMeshGrid)));
  EXPECT_NE(mesh_surface_grid_signature(1.0f, settings), initial);

  const ViewportSettings defaults = ViewportSettings::defaults();
  EXPECT_NE(mesh_surface_grid_signature(2.0f, defaults), initial);

  ViewportSettings recolored = defaults;
  ASSERT_TRUE(recolored.set_color(ViewportColorSettingId::MeshGridMinor,
                                  {0.2f, 0.4f, 0.6f, 0.8f}));
  EXPECT_NE(mesh_surface_grid_signature(1.0f, recolored), initial);

  ViewportSettings instance_context_recolored = defaults;
  ASSERT_TRUE(instance_context_recolored.set_color(
      ViewportColorSettingId::InstanceEditContext,
      {0.15f, 0.25f, 0.35f, 0.30f}));
  EXPECT_NE(mesh_surface_grid_signature(1.0f, instance_context_recolored),
            initial)
      << "the configurable edit-context tint must invalidate material state";

  for (const auto id : {ViewportFloatSettingId::InstanceEditContextStrength,
                        ViewportFloatSettingId::InstanceEditContextTintAmount,
                        ViewportFloatSettingId::InstanceEditContextDesaturation}) {
    SCOPED_TRACE(static_cast<int>(id));
    ViewportSettings changed = defaults;
    ASSERT_TRUE(changed.set_float_value(id, 0.75f));
    EXPECT_NE(mesh_surface_grid_signature(1.0f, changed), initial);
  }
  for (const auto id : {ViewportFloatSettingId::InstanceEditBackgroundBlend,
                        ViewportFloatSettingId::InstanceEditGridFade}) {
    SCOPED_TRACE(static_cast<int>(id));
    ViewportSettings changed = defaults;
    ASSERT_TRUE(changed.set_float_value(id, 0.75f));
    EXPECT_EQ(mesh_surface_grid_signature(1.0f, changed), initial)
        << "background and ground-grid controls must not rewrite mesh uniforms";
  }
}

TEST(RenderHotPathTests, SrgbByteLookupPreservesReferenceConversion) {
  for (std::uint32_t value = 0; value <= 255u; ++value) {
    const std::uint8_t channel = static_cast<std::uint8_t>(value);
    const std::uint8_t reference = normalized_to_byte(
        srgb_to_linear(static_cast<float>(channel) / 255.0f));
    EXPECT_EQ(srgb_byte_to_linear_byte(channel), reference) << value;
  }
}

TEST(ViewportInfoStatsTests, CountsOnlyCameraVisibleRenderableMeshes) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId visible =
      document.create_entity("Visible Mesh", EntityKind::Mesh);
  document.set_mesh(visible, MeshComponent{.mesh = mesh});
  document.set_transform(
      visible, TransformComponent{.position = {0.0f, 0.0f, -3.0f}});

  const EntityId missing =
      document.create_entity("Missing Mesh", EntityKind::Mesh);
  document.set_mesh(missing, MeshComponent{.mesh = mesh + 1000u});
  document.set_transform(
      missing, TransformComponent{.position = {0.0f, 0.0f, -3.0f}});

  const EntityId hidden =
      document.create_entity("Hidden Mesh", EntityKind::Mesh);
  document.set_mesh(hidden, MeshComponent{.mesh = mesh});
  document.set_transform(hidden,
                         TransformComponent{.position = {0.0f, 0.0f, -3.0f}});
  document.set_visible(hidden, false);

  const ViewportOverlayCamera camera = test_perspective_overlay_camera();
  const ViewportInfoSceneSnapshot snapshot =
      build_viewport_info_scene_snapshot(document, assets);
  ASSERT_EQ(snapshot.renderables.size(), 1u);
  const ViewportInfoSceneStats visible_stats =
      collect_viewport_info_scene_stats(snapshot, camera);

  EXPECT_EQ(visible_stats.objects, 1u);
  EXPECT_EQ(visible_stats.vertices, 3u);
  EXPECT_EQ(visible_stats.edges, 3u);
  EXPECT_EQ(visible_stats.faces, 1u);
  EXPECT_EQ(visible_stats.triangles, 1u);
  EXPECT_EQ(visible_stats.primitives, 1u);
  EXPECT_EQ(visible_stats.draw_calls, 1u);

  ViewportOverlayCamera looked_away = camera;
  looked_away.forward = {0.0f, 1.0f, 0.0f};
  looked_away.right = {1.0f, 0.0f, 0.0f};
  looked_away.up = {0.0f, 0.0f, -1.0f};
  const ViewportInfoSceneStats empty_view_stats =
      collect_viewport_info_scene_stats(snapshot, looked_away);

  EXPECT_EQ(empty_view_stats.objects, 0u);
  EXPECT_EQ(empty_view_stats.vertices, 0u);
  EXPECT_EQ(empty_view_stats.edges, 0u);
  EXPECT_EQ(empty_view_stats.faces, 0u);
  EXPECT_EQ(empty_view_stats.triangles, 0u);
  EXPECT_EQ(empty_view_stats.primitives, 0u);
  EXPECT_EQ(empty_view_stats.draw_calls, 0u);
}

TEST(ViewportInfoStatsTests,
     LocalVisibilityCountsFormalInstancesByPublicOccurrence) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Stats Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Stats Definition Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));

  const EntityId isolated =
      document.create_entity("Stats Local Occurrence", EntityKind::Instance);
  const EntityId other =
      document.create_entity("Stats Other Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      isolated, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(
      other, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_transform(
      isolated, TransformComponent{.position = {0.0f, 0.0f, -3.0f}}));
  ASSERT_TRUE(document.set_transform(
      other, TransformComponent{.position = {0.5f, 0.0f, -3.0f}}));

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 2> baseline{isolated,
                                                               other};
  const std::array<ViewportLocalVisibilityEntityId, 1> members{isolated};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  const ViewportLocalVisibilitySnapshot local =
      workspace.local_visibility_snapshot();
  const ViewportOverlayCamera camera = test_perspective_overlay_camera();

  const ViewportInfoSceneStats isolated_stats =
      collect_viewport_info_scene_stats(document, assets, camera, &local);
  EXPECT_EQ(isolated_stats.objects, 1u);
  EXPECT_EQ(isolated_stats.vertices, 3u);
  EXPECT_EQ(isolated_stats.edges, 3u);
  EXPECT_EQ(isolated_stats.faces, 1u);
  EXPECT_EQ(isolated_stats.triangles, 1u);
  EXPECT_EQ(isolated_stats.primitives, 1u);
  EXPECT_EQ(isolated_stats.draw_calls, 1u);

  ASSERT_TRUE(document.set_visible(isolated, false));
  EXPECT_EQ(collect_viewport_info_scene_stats(document, assets, camera, &local)
                .objects,
            0u);
  ASSERT_TRUE(document.set_visible(isolated, true));
  ASSERT_TRUE(document.set_visible(member, false));
  EXPECT_EQ(collect_viewport_info_scene_stats(document, assets, camera, &local)
                .objects,
            0u);
  ASSERT_TRUE(document.set_visible(member, true));

  const ViewportInfoSceneStats all_occurrences =
      collect_viewport_info_scene_stats(document, assets, camera);
  EXPECT_EQ(all_occurrences.objects, 2u);
  EXPECT_EQ(all_occurrences.vertices, 6u);
  EXPECT_EQ(all_occurrences.edges, 6u);
  EXPECT_EQ(all_occurrences.faces, 2u);
  EXPECT_EQ(all_occurrences.triangles, 2u);
  EXPECT_EQ(all_occurrences.primitives, 2u);
  EXPECT_EQ(all_occurrences.draw_calls, 2u);
}

TEST(RenderOverlaySnapshotTests,
     WireGeometryPayloadIsDeterministicCachedAndLifetimePinned) {
  MeshAsset mesh = make_authored_quad_mesh();
  const MeshTopologyCounts topology = mesh_topology_counts(mesh);
  EXPECT_EQ(topology.vertices, 4u);
  EXPECT_EQ(topology.edges, 4u);
  EXPECT_EQ(topology.faces, 1u);
  EXPECT_EQ(topology.triangles, 2u);
  const std::shared_ptr<const MeshWireGeometryPayload> first =
      mesh.wire_geometry_payload();
  const std::shared_ptr<const MeshWireGeometryPayload> second =
      mesh.wire_geometry_payload();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, second);
  EXPECT_TRUE(first->authored);
  ASSERT_EQ(first->segments.size(), 4u);
  EXPECT_NE(first->content_id, 0u);

  const MeshWireSegment pinned_first_segment = first->segments.front();
  quader_poly::Document &authored = mesh.edit_authored_polygon_document();
  ASSERT_FALSE(authored.vertices.empty());
  authored.vertices.front().position.x -= 3.0f;
  const std::shared_ptr<const MeshWireGeometryPayload> changed =
      mesh.wire_geometry_payload();
  ASSERT_NE(changed, nullptr);
  EXPECT_NE(changed, first);
  EXPECT_NE(changed->content_id, first->content_id);
  EXPECT_FLOAT_EQ(first->segments.front().start.x, pinned_first_segment.start.x);
  EXPECT_FLOAT_EQ(first->segments.front().start.y, pinned_first_segment.start.y);
  EXPECT_FLOAT_EQ(first->segments.front().start.z, pinned_first_segment.start.z)
      << "the old payload remains immutable while a render snapshot pins it";
}

TEST(CurveSamplePayloadTests,
     ReusesPinnedSamplesAcrossDocumentWrappersAndAssetReplacement) {
  CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  const auto original_document = asset.document;
  const auto original = curve_sample_payload(asset);
  ASSERT_NE(original, nullptr);
  ASSERT_TRUE(original->error_message.empty());
  ASSERT_GT(original->samples.size(), 2u);
  EXPECT_EQ(original->content_identity, asset.document->content_identity());
  EXPECT_EQ(original->content_revision, asset.document->content_revision());
  EXPECT_EQ(curve_sample_payload(asset), original);

  asset.document =
      std::make_shared<const modeling::CurveDocument>(*original_document);
  EXPECT_NE(asset.document, original_document);
  EXPECT_EQ(curve_sample_payload(asset), original);
  const CurveAsset copied = asset;
  EXPECT_EQ(curve_sample_payload(copied), original);

  CurveAsset replacement;
  replacement.document =
      std::make_shared<const modeling::CurveDocument>(*original_document);
  asset = std::move(replacement);
  EXPECT_EQ(curve_sample_payload(asset), original);

  modeling::CurveDocument edited = *asset.document;
  ASSERT_TRUE(edited.set_point(edited.points().front().id,
      {.position = {0.0f, 1.0f, 0.0f},
       .outgoing_handle = {0.0f, 2.0f, 0.0f}}).ok());
  asset.document =
      std::make_shared<const modeling::CurveDocument>(std::move(edited));
  const auto changed = curve_sample_payload(asset);
  ASSERT_NE(changed, nullptr);
  ASSERT_FALSE(changed->samples.empty());
  EXPECT_NE(changed, original);
  EXPECT_NE(changed->content_identity, original->content_identity);
  EXPECT_FLOAT_EQ(changed->samples.front().position.y, 1.0f);
  EXPECT_FLOAT_EQ(original->samples.front().position.y, 0.0f);
  EXPECT_EQ(curve_sample_payload(copied), original)
      << "snapshots retain immutable samples after a different wrapper is edited";
}

TEST(CurveSamplePayloadTests,
     RetainsSamplingErrorsAndInvalidatesOnOptionsOrAuthoredChanges) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  modeling::CurveDocument document = *asset.document;
  std::shared_ptr<const CurveSamplePayload> cached;
  modeling::CurveSamplingOptions options;
  options.max_samples = 1u;
  const auto budget_error = curve_sample_payload(document, cached, options);
  ASSERT_NE(budget_error, nullptr);
  EXPECT_FALSE(budget_error->error_message.empty());
  EXPECT_TRUE(budget_error->samples.empty());
  EXPECT_EQ(curve_sample_payload(document, cached, options), budget_error);

  options.max_samples = 1024u;
  const auto sampled = curve_sample_payload(document, cached, options);
  ASSERT_TRUE(sampled->error_message.empty());
  EXPECT_NE(sampled, budget_error);
  options.tolerance = 0.02;
  const auto different_tolerance = curve_sample_payload(document, cached, options);
  ASSERT_TRUE(different_tolerance->error_message.empty());
  EXPECT_NE(different_tolerance, sampled);
  options.max_depth = 20u;
  const auto different_depth = curve_sample_payload(document, cached, options);
  EXPECT_NE(different_depth, different_tolerance);
  EXPECT_EQ(curve_sample_payload(document, cached, options), different_depth);

  options.tolerance = std::numeric_limits<double>::quiet_NaN();
  const auto invalid_options = curve_sample_payload(document, cached, options);
  EXPECT_FALSE(invalid_options->error_message.empty());
  EXPECT_TRUE(invalid_options->samples.empty());
  EXPECT_EQ(curve_sample_payload(document, cached, options), invalid_options);
  ASSERT_TRUE(document.set_point(document.points().front().id,
      {.position = {0.0f, 1.0f, 0.0f}}).ok());
  const auto edited_error = curve_sample_payload(document, cached, options);
  EXPECT_NE(edited_error, invalid_options);
  EXPECT_NE(edited_error->content_identity, invalid_options->content_identity);
  EXPECT_FALSE(edited_error->error_message.empty());
  EXPECT_EQ(curve_sample_payload(document, cached, options), edited_error);
  const auto recovered = curve_sample_payload(document, cached);
  EXPECT_TRUE(recovered->error_message.empty());
  EXPECT_FALSE(recovered->samples.empty());

  const CurveAsset missing;
  const auto missing_document = curve_sample_payload(missing);
  EXPECT_FALSE(missing_document->error_message.empty());
  EXPECT_TRUE(missing_document->samples.empty());
  EXPECT_EQ(curve_sample_payload(missing), missing_document);
}

TEST(RenderOverlaySnapshotTests,
     AuthoredCurvesRemainDepthTestedWithSelectionGizmoAndToolOverlaysHidden) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_curve(make_overlay_curve_asset());
  ASSERT_NE(asset, kInvalidAssetId);
  const EntityId curve = document.create_entity("Visible Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, CurveComponent{.curve = asset}));
  ASSERT_TRUE(document.set_modeling_object(curve, {.object_id = 27}));
  const EntityId light = document.create_entity("Selected Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(light, LightComponent{.type = LightType::Point}));
  ASSERT_TRUE(document.set_selected(light, true));
  // Point lights use the billboard bucket even when selected; a directional
  // light exercises the independently gated selection-marker bucket.
  const EntityId directional =
      document.create_entity("Selected Directional", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      directional, LightComponent{.type = LightType::Directional}));
  ASSERT_TRUE(document.set_selected(directional, true));
  ViewportOverlayFrame tool_frame;
  tool_frame.lines.push_back({.layer = ViewportOverlayLayer::ToolHandle,
                             .start = {0.0f, 0.0f, 0.0f},
                             .end = {1.0f, 0.0f, 0.0f},
                             .source = ViewportOverlaySource::Tool});
  const auto snapshot =
      build_render_overlay_selection_snapshot(document, assets, tool_frame);
  ASSERT_EQ(snapshot.curves.size(), 1u);
  const auto &record = snapshot.curves.front();
  EXPECT_EQ(record.entity, curve);
  EXPECT_EQ(record.curve_id, asset);
  EXPECT_FALSE(record.selected);
  ASSERT_NE(record.sample_payload, nullptr);
  ASSERT_GT(record.sample_payload->samples.size(), 2u);
  ASSERT_FALSE(snapshot.selected_lights.empty());
  ASSERT_FALSE(snapshot.point_light_gizmos.empty());

  const auto frame = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, snapshot, false, RenderOverlaySelectionMode::Object, false, false);
  ASSERT_EQ(frame.lines.size(), record.sample_payload->samples.size() - 1u);
  EXPECT_TRUE(frame.points.empty());
  EXPECT_TRUE(frame.triangles.empty());
  EXPECT_TRUE(frame.icons.empty());
  for (const auto &line : frame.lines) {
    EXPECT_EQ(line.layer, ViewportOverlayLayer::WireframeBase);
    EXPECT_EQ(line.source, ViewportOverlaySource::SceneCurve);
    EXPECT_EQ(line.element.object_id, 27);
    EXPECT_EQ(line.color, kOverlayCurveColor);
    EXPECT_EQ(viewport_overlay_depth_mode(line),
              ViewportOverlayDepthMode::DepthTested);
  }
  EXPECT_FLOAT_EQ(frame.lines.front().start.x, 0.0f);
  EXPECT_FLOAT_EQ(frame.lines.back().end.x, 4.0f);
}

TEST(RenderOverlaySnapshotCacheTests,
     CurveTransformRefreshRetainsSamplesAndUpdatesWorldPlacement) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_curve(make_overlay_curve_asset());
  ASSERT_NE(asset, kInvalidAssetId);
  const EntityId group = document.create_entity("Curve Parent", EntityKind::Empty);
  const EntityId curve = document.create_entity("Moving Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, CurveComponent{.curve = asset}));
  ASSERT_TRUE(document.set_parent(curve, group, false));
  ASSERT_TRUE(document.set_transform(
      curve, TransformComponent{.position = {1.0f, 2.0f, 3.0f}}));
  RenderOverlaySnapshotCache cache;
  const auto &initial = cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.curves.size(), 1u);
  const auto samples = initial.curves.front().sample_payload;
  const auto *records = initial.curves.data();
  ASSERT_NE(samples, nullptr);
  ASSERT_FALSE(samples->samples.empty());
  EXPECT_EQ(cache.selection_snapshot(document, assets).curves.data(), records);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  ASSERT_TRUE(document.set_transform(
      group, TransformComponent{.position = {5.0f, -2.0f, -7.0f}}));
  const auto &moved = cache.selection_snapshot(document, assets);
  ASSERT_EQ(moved.curves.size(), 1u);
  EXPECT_EQ(moved.curves.data(), records);
  EXPECT_EQ(moved.curves.front().sample_payload, samples);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[12], 6.0f);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[13], 0.0f);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[14], -4.0f);
  EXPECT_FLOAT_EQ(samples->samples.front().position.x, 0.0f);
  const auto frame = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, moved, false, RenderOverlaySelectionMode::Object, false, false);
  ASSERT_FALSE(frame.lines.empty());
  EXPECT_FLOAT_EQ(frame.lines.front().start.x, 6.0f);
  EXPECT_FLOAT_EQ(frame.lines.front().start.y, 0.0f);
  EXPECT_FLOAT_EQ(frame.lines.front().start.z, -4.0f);
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     CurveExtractionHonorsAncestorAndViewportLocalVisibility) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_curve(make_overlay_curve_asset());
  ASSERT_NE(asset, kInvalidAssetId);
  const EntityId isolated = document.create_entity("Isolated Curve", EntityKind::Curve);
  const EntityId other = document.create_entity("Other Curve", EntityKind::Curve);
  const EntityId hidden = document.create_entity("Hidden Curve", EntityKind::Curve);
  const EntityId group = document.create_entity("Hidden Parent", EntityKind::Empty);
  for (const EntityId curve : {isolated, other, hidden}) {
    ASSERT_TRUE(document.set_curve(curve, CurveComponent{.curve = asset}));
  }
  ASSERT_TRUE(document.set_parent(hidden, group, false));
  ASSERT_TRUE(document.set_visible(group, false));
  RenderOverlaySnapshotCache cache;
  ASSERT_EQ(cache.selection_snapshot(document, assets).curves.size(), 2u);

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 3> baseline{isolated, other, hidden};
  const std::array<ViewportLocalVisibilityEntityId, 2> members{isolated, hidden};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  const auto local = workspace.local_visibility_snapshot();
  const auto &filtered = cache.selection_snapshot(document, assets, {}, &local);
  ASSERT_EQ(filtered.curves.size(), 1u);
  EXPECT_EQ(filtered.curves.front().entity, isolated);
  EXPECT_EQ(filtered.local_visibility_revision, local.revision());
  const auto samples = filtered.curves.front().sample_payload;
  const auto builds = cache.stats().selection_build_count;
  EXPECT_EQ(cache.selection_snapshot(document, assets, {}, &local)
                .curves.front().sample_payload,
            samples);
  EXPECT_EQ(cache.stats().selection_build_count, builds);

  ASSERT_TRUE(document.set_visible(isolated, false));
  EXPECT_TRUE(cache.selection_snapshot(document, assets, {}, &local).curves.empty());
  ASSERT_TRUE(document.set_visible(isolated, true));
  ASSERT_EQ(cache.selection_snapshot(document, assets, {}, &local).curves.size(), 1u);
  ASSERT_TRUE(workspace.exit_local_visibility());
  const auto restored_local = workspace.local_visibility_snapshot();
  const auto &restored =
      cache.selection_snapshot(document, assets, {}, &restored_local);
  ASSERT_EQ(restored.curves.size(), 2u);
  EXPECT_TRUE(std::ranges::none_of(restored.curves, [hidden](const auto &curve) {
    return curve.entity == hidden;
  }));
  for (const auto &curve : restored.curves) {
    EXPECT_EQ(curve.sample_payload, samples);
  }
}

TEST(RenderOverlaySnapshotCacheTests,
     CurvePublicationRetains512MeshAndNonCurveOverlayStorage) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh_asset = assets.add_mesh(make_overlay_triangle_mesh());
  ASSERT_NE(mesh_asset, kInvalidAssetId);
  const EntityId group = document.create_entity("Selected Group", EntityKind::Empty);
  EntityId direct_mesh = kInvalidEntityId;
  for (std::size_t index = 0; index < 512u; ++index) {
    const EntityId mesh =
        document.create_entity("Unrelated Mesh", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_asset}));
    if (index == 0u) {
      ASSERT_TRUE(document.set_parent(mesh, group, false));
    } else if (index == 1u) {
      direct_mesh = mesh;
    }
  }
  ASSERT_TRUE(document.set_selected(group, true));
  ASSERT_TRUE(document.set_selected(direct_mesh, true));
  const EntityId directional =
      document.create_entity("Selected Directional", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      directional, LightComponent{.type = LightType::Directional}));
  ASSERT_TRUE(document.set_selected(directional, true));
  const EntityId point = document.create_entity("Point", EntityKind::Light);
  ASSERT_TRUE(
      document.set_light(point, LightComponent{.type = LightType::Point}));
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId curve = document.create_entity("Edited Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, {.curve = curve_asset}));

  RenderOverlaySnapshotCache cache;
  const auto persistent = cache.update_persistent(document, assets, true, 1u, {});
  ASSERT_NE(persistent.scene_wireframe, nullptr);
  ASSERT_EQ(persistent.scene_wireframe->meshes.size(), 512u);
  const auto *wire_storage = persistent.scene_wireframe->meshes.data();
  const auto &initial = cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.curves.size(), 1u);
  ASSERT_FALSE(initial.hierarchy_selection_wires.empty());
  ASSERT_FALSE(initial.group_bounds.empty());
  ASSERT_FALSE(initial.selected_mesh_bounds.empty());
  ASSERT_FALSE(initial.selected_lights.empty());
  ASSERT_FALSE(initial.point_light_gizmos.empty());
  const auto *hierarchy_storage = initial.hierarchy_selection_wires.data();
  const auto *group_storage = initial.group_bounds.data();
  const auto *mesh_bounds_storage = initial.selected_mesh_bounds.data();
  const auto *selected_light_storage = initial.selected_lights.data();
  const auto *point_light_storage = initial.point_light_gizmos.data();
  const auto initial_curve_samples = initial.curves.front().sample_payload;

  CurveAsset replacement = *assets.curve(curve_asset);
  modeling::CurveDocument edited = *replacement.document;
  const modeling::CurveControlPoint control = edited.points().front();
  ASSERT_TRUE(edited.set_point(
      control.id,
      {.position = {control.position.x, control.position.y + 1.0f,
                    control.position.z},
       .incoming_handle = control.incoming_handle,
       .outgoing_handle = control.outgoing_handle}).ok());
  replacement.document =
      std::make_shared<const modeling::CurveDocument>(std::move(edited));
  ASSERT_TRUE(assets.replace_curve(curve_asset, std::move(replacement)));
  ASSERT_TRUE(document.notify_curve_payload_changed(curve));

  const auto refreshed_persistent =
      cache.update_persistent(document, assets, true, 1u, {});
  ASSERT_FALSE(refreshed_persistent.scene_wireframe_changed);
  ASSERT_NE(refreshed_persistent.scene_wireframe, nullptr);
  EXPECT_EQ(refreshed_persistent.scene_wireframe->meshes.data(), wire_storage);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_curve_publication_reuse_count, 1u);
  const auto &refreshed = cache.selection_snapshot(document, assets);
  ASSERT_EQ(refreshed.curves.size(), 1u);
  EXPECT_NE(refreshed.curves.front().sample_payload, initial_curve_samples);
  EXPECT_EQ(refreshed.hierarchy_selection_wires.data(), hierarchy_storage);
  EXPECT_EQ(refreshed.group_bounds.data(), group_storage);
  EXPECT_EQ(refreshed.selected_mesh_bounds.data(), mesh_bounds_storage);
  EXPECT_EQ(refreshed.selected_lights.data(), selected_light_storage);
  EXPECT_EQ(refreshed.point_light_gizmos.data(), point_light_storage);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_curve_refresh_count, 1u);
  EXPECT_EQ(cache.stats().selection_curve_entity_visit_count, 1u)
      << "curve publication must not visit the 512 unrelated meshes";

  const auto expect_rejected = [&]() {
    ASSERT_TRUE(document.notify_curve_payload_changed(curve));
    const auto persistent_update =
        cache.update_persistent(document, assets, true, 1u, {});
    EXPECT_TRUE(persistent_update.scene_wireframe_changed);
    (void)cache.selection_snapshot(document, assets);
  };
  ASSERT_TRUE(document.set_transform(
      direct_mesh, TransformComponent{.position = {3.0f, 0.0f, 0.0f}}));
  expect_rejected();
  ASSERT_TRUE(document.set_selected(direct_mesh, false));
  expect_rejected();
  ASSERT_TRUE(assets.replace_mesh(mesh_asset, make_authored_quad_mesh()));
  expect_rejected();
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 4u);
  EXPECT_EQ(cache.stats().selection_build_count, 4u);
  EXPECT_EQ(cache.stats().selection_curve_refresh_count, 1u)
      << "non-curve revisions must reject the curve-only refresh";
}

TEST(RenderOverlaySnapshotCacheTests,
     CurveRefreshUsesNarrowMembershipQueryAndRejectsStructuralRemoval) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId first = document.create_entity("First Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(first, {.curve = curve_asset}));
  // This entity already has Curve kind, so adding its component later does not
  // advance the hierarchy revision. The narrow query must still discover it.
  const EntityId latent = document.create_entity("Latent Curve", EntityKind::Curve);
  RenderOverlaySnapshotCache cache;
  ASSERT_EQ(cache.selection_snapshot(document, assets).curves.size(), 1u);

  ASSERT_TRUE(document.set_curve(latent, {.curve = curve_asset}));
  const auto &added = cache.selection_snapshot(document, assets);
  ASSERT_EQ(added.curves.size(), 2u);
  EXPECT_EQ(added.curves.back().entity, latent);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_curve_refresh_count, 1u);
  EXPECT_EQ(cache.stats().selection_curve_entity_visit_count, 2u);

  ASSERT_TRUE(document.clear_curve(latent));
  const auto &removed = cache.selection_snapshot(document, assets);
  ASSERT_EQ(removed.curves.size(), 1u);
  EXPECT_EQ(removed.curves.front().entity, first);
  EXPECT_EQ(cache.stats().selection_build_count, 2u)
      << "membership removal changes structural revisions and must rebuild";
  EXPECT_EQ(cache.stats().selection_curve_refresh_count, 1u);
}

TEST(CurveToolOverlayCacheTests,
     ReusesUnchangedPreviewAndRefreshesTransformControlAndRevisionChanges) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveToolPreviewState preview;
  preview.active = true;
  preview.document = *asset.document;
  preview.world_transform.values[12] = 3.0f;
  preview.revision = 7u;
  CurveToolOverlayCache cache;
  const auto &initial = cache.frame(preview);
  ASSERT_EQ(initial.points.size(), 4u);
  ASSERT_EQ(initial.lines.size(), 2u);
  const auto *points = initial.points.data();
  const auto *lines = initial.lines.data();
  EXPECT_FLOAT_EQ(initial.points.front().position.x, 3.0f);
  EXPECT_EQ(cache.build_count(), 1u);

  preview.document = modeling::CurveDocument(*asset.document);
  EXPECT_EQ(cache.frame(preview).points.data(), points);
  EXPECT_EQ(cache.frame(preview).lines.data(), lines);
  EXPECT_EQ(cache.build_count(), 1u);
  preview.world_transform.values[12] = 8.0f;
  ++preview.revision;
  EXPECT_FLOAT_EQ(cache.frame(preview).points.front().position.x, 8.0f);
  EXPECT_EQ(cache.build_count(), 2u);
  preview.control_selection.controls = {
      CurveControlTarget{preview.document->points().front().id,
                         CurveControlKind::OutgoingHandle},
      CurveControlTarget{preview.document->points().back().id,
                         CurveControlKind::IncomingHandle},
  };
  preview.control_selection.active = preview.control_selection.controls.back();
  ++preview.revision;
  EXPECT_EQ(cache.frame(preview).points[1].color, kOverlaySelectedVertexColor);
  EXPECT_EQ(cache.frame(preview).points[3].color, kOverlaySelectedVertexColor);
  EXPECT_NE(cache.frame(preview).points[0].color, kOverlaySelectedVertexColor);
  EXPECT_NE(cache.frame(preview).points[2].color, kOverlaySelectedVertexColor);
  EXPECT_EQ(cache.build_count(), 3u);
  ++preview.revision;
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), 4u);
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), 4u);

  preview.active = false;
  ++preview.revision;
  const auto &inactive = cache.frame(preview);
  EXPECT_TRUE(inactive.lines.empty());
  EXPECT_TRUE(inactive.points.empty());
  EXPECT_EQ(cache.build_count(), 5u);
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), 5u);
}

TEST(CurveToolOverlayCacheTests,
     SelectedAnchorHighlightsItsTangentAndPreservesHandleSelectionColor) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveToolPreviewState preview;
  preview.active = true;
  preview.document = *asset.document;
  preview.control_selection.controls = {
      CurveControlTarget{preview.document->points().front().id,
                         CurveControlKind::Anchor},
      CurveControlTarget{preview.document->points().back().id,
                         CurveControlKind::IncomingHandle},
  };
  preview.control_selection.active = preview.control_selection.controls.back();

  CurveToolOverlayCache cache;
  const auto &frame = cache.frame(preview);
  ASSERT_EQ(frame.points.size(), 4u);
  ASSERT_EQ(frame.lines.size(), 2u);

  EXPECT_EQ(frame.lines[0].color, kOverlaySelectedVertexColor);
  EXPECT_EQ(frame.lines[1].color, kOverlayToolPlaneLineColor);
  EXPECT_EQ(frame.points[0].color, kOverlaySelectedVertexColor);
  EXPECT_EQ(frame.points[1].color, kOverlayToolHandleColor);
  EXPECT_EQ(frame.points[2].color, kOverlayVertexColor);
  EXPECT_EQ(frame.points[3].color, kOverlaySelectedVertexColor);
}

TEST(CurveToolOverlayCacheTests,
     ShowsSnappedHoverBeforeFirstAnchorAndClearsSuppressedPresentation) {
  CurveToolPreviewState preview;
  preview.active = true;
  preview.drawing = true;
  preview.document.emplace();
  preview.world_transform.values[12] = 10.0f;
  preview.hover_world = QVec3{3.0f, 4.0f, -2.0f};
  CurveToolOverlayCache cache;
  const auto &hover = cache.frame(preview);
  ASSERT_EQ(hover.points.size(), 1u);
  EXPECT_TRUE(hover.lines.empty());
  const auto &point = hover.points.front();
  EXPECT_FLOAT_EQ(point.position.x, 3.0f);
  EXPECT_FLOAT_EQ(point.position.y, 4.0f);
  EXPECT_FLOAT_EQ(point.position.z, -2.0f);
  EXPECT_EQ(point.layer, ViewportOverlayLayer::HoverVertex);
  EXPECT_EQ(point.style_role, ViewportOverlayStyleRole::HoverVertex);
  EXPECT_EQ(point.color, kOverlayHoverVertexColor);
  EXPECT_FLOAT_EQ(point.size_pixels, kOverlayDrawnPlanePointSizePixels);
  EXPECT_EQ(point.source, ViewportOverlaySource::Tool);
  EXPECT_EQ(viewport_overlay_depth_mode(point), ViewportOverlayDepthMode::DepthTested);
  const auto *hover_points = hover.points.data();
  EXPECT_EQ(cache.frame(preview).points.data(), hover_points);
  EXPECT_EQ(cache.build_count(), 1u);

  // Navigation, leaving the pane and canceled placement clear the neutral hover.
  preview.hover_world.reset();
  ++preview.revision;
  EXPECT_TRUE(cache.frame(preview).points.empty());
  EXPECT_TRUE(cache.frame(preview).lines.empty());
  EXPECT_EQ(cache.build_count(), 2u);
  preview.hover_world = QVec3{5.0f, 4.0f, -2.0f};
  ++preview.revision;
  ASSERT_EQ(cache.frame(preview).points.size(), 1u);
  EXPECT_FLOAT_EQ(cache.frame(preview).points.front().position.x, 5.0f);

  // A stale hover must not leak into a drag, existing-curve edit or inactive tool.
  preview.dragging = true;
  ++preview.revision;
  EXPECT_TRUE(cache.frame(preview).points.empty());
  preview.dragging = false;
  preview.drawing = false;
  ++preview.revision;
  EXPECT_TRUE(cache.frame(preview).points.empty());
  preview.drawing = true;
  preview.active = false;
  ++preview.revision;
  EXPECT_TRUE(cache.frame(preview).points.empty());

  preview.active = true;
  ++preview.revision;
  ASSERT_TRUE(preview.document->append_point({.position = {1.0f, 0.0f, 0.0f}}).ok());
  const auto &next_point = cache.frame(preview);
  ASSERT_EQ(next_point.points.size(), 2u);
  EXPECT_EQ(point_count_for(next_point, ViewportOverlayLayer::HoverVertex), 1u);
  ASSERT_EQ(next_point.lines.size(), 1u);
  EXPECT_FLOAT_EQ(next_point.lines.front().start.x, 11.0f);
  EXPECT_FLOAT_EQ(next_point.lines.front().end.x, 5.0f);
  EXPECT_FLOAT_EQ(next_point.points.back().position.x, 5.0f);
}

TEST(CurveToolOverlayCacheTests,
     PresetHoverIsStrokeOnlyAndRetainsSamplesAcrossPlacementTransforms) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  const auto expected_samples = curve_sample_payload(asset);
  ASSERT_TRUE(expected_samples->error_message.empty());
  ASSERT_GT(expected_samples->samples.size(), 2u);

  CurveToolPreviewState preview;
  preview.active = true;
  preview.drawing = true;
  preview.preset_hover_document = *asset.document;
  preview.preset_hover_transform.values[12] = 2.0f;
  preview.hover_world = QVec3{2.0f, 3.0f, 0.0f};
  preview.revision = 1u;

  CurveToolOverlayCache cache;
  const auto &initial = cache.frame(preview);
  ASSERT_EQ(initial.lines.size(), expected_samples->samples.size() - 1u);
  ASSERT_EQ(initial.points.size(), 1u);
  EXPECT_EQ(line_count_for(initial, ViewportOverlayLayer::ToolHandle), 0u);
  EXPECT_EQ(point_count_for(initial, ViewportOverlayLayer::ToolHandle), 0u);
  EXPECT_EQ(initial.lines.front().layer, ViewportOverlayLayer::WireframeBase);
  EXPECT_EQ(initial.lines.front().style_role,
            ViewportOverlayStyleRole::HoverWire);
  EXPECT_EQ(initial.lines.front().color, kOverlayHoverWireColor);
  EXPECT_FLOAT_EQ(initial.lines.front().width_pixels,
                  kOverlayHoverWireLineWidthPixels);
  EXPECT_EQ(initial.lines.front().source, ViewportOverlaySource::Tool);
  EXPECT_FLOAT_EQ(initial.lines.front().start.x, 2.0f);
  EXPECT_EQ(initial.points.front().layer, ViewportOverlayLayer::HoverVertex);
  EXPECT_FLOAT_EQ(initial.points.front().position.x, 2.0f);
  EXPECT_FLOAT_EQ(initial.points.front().position.y, 3.0f);
  EXPECT_EQ(cache.build_count(), 1u);
  EXPECT_EQ(cache.preset_hover_sample_refresh_count(), 1u);

  const auto *retained_lines = initial.lines.data();
  EXPECT_EQ(cache.frame(preview).lines.data(), retained_lines);
  EXPECT_EQ(cache.build_count(), 1u);
  EXPECT_EQ(cache.preset_hover_sample_refresh_count(), 1u);

  preview.preset_hover_transform.values[12] = 7.0f;
  preview.hover_world = QVec3{7.0f, 3.0f, 0.0f};
  ++preview.revision;
  const auto &moved = cache.frame(preview);
  ASSERT_FALSE(moved.lines.empty());
  ASSERT_EQ(moved.points.size(), 1u);
  EXPECT_FLOAT_EQ(moved.lines.front().start.x, 7.0f);
  EXPECT_FLOAT_EQ(moved.points.front().position.x, 7.0f);
  EXPECT_EQ(cache.build_count(), 2u);
  EXPECT_EQ(cache.preset_hover_sample_refresh_count(), 1u)
      << "world placement must not resample unchanged preset geometry";

  modeling::CurveDocument changed = *asset.document;
  ASSERT_TRUE(changed.append_point({.position = {8.0f, 0.0f, 0.0f}}).ok());
  preview.preset_hover_document = std::move(changed);
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), 3u)
      << "content identity changes invalidate even without a tool revision";
  EXPECT_EQ(cache.preset_hover_sample_refresh_count(), 2u);

  preview.dragging = true;
  ++preview.revision;
  const auto &dragging = cache.frame(preview);
  EXPECT_TRUE(dragging.lines.empty());
  EXPECT_TRUE(dragging.points.empty());
  EXPECT_EQ(cache.preset_hover_sample_refresh_count(), 2u);
}

TEST(CurveToolOverlayCacheTests,
     DraftSamplesItsPathWhileExistingCurveEditingAddsOnlyControls) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  const auto samples = curve_sample_payload(asset);
  ASSERT_TRUE(samples->error_message.empty());
  ASSERT_GT(samples->samples.size(), 2u);
  CurveToolPreviewState preview;
  preview.active = true;
  preview.drawing = true;
  preview.document = *asset.document;
  preview.hover_world = QVec3{6.0f, 1.0f, 0.0f};
  CurveToolOverlayCache cache;
  const auto &draft = cache.frame(preview);
  EXPECT_EQ(line_count_for(draft, ViewportOverlayLayer::WireframeBase),
            samples->samples.size() - 1u);
  EXPECT_EQ(line_count_for(draft, ViewportOverlayLayer::ToolHandle), 3u);
  EXPECT_EQ(line_count_for(draft, ViewportOverlayLayer::ToolPlaneLine), 1u);
  ASSERT_EQ(draft.points.size(), 6u);
  EXPECT_EQ(point_count_for(draft, ViewportOverlayLayer::HoverVertex), 1u);
  EXPECT_FLOAT_EQ(draft.points.back().position.x, 6.0f);
  EXPECT_FLOAT_EQ(draft.points.back().position.y, 1.0f);
  EXPECT_EQ(line_source_count_for(draft, ViewportOverlaySource::Tool),
            draft.lines.size());
  EXPECT_FLOAT_EQ(draft.lines.back().end.x, 6.0f);
  EXPECT_FLOAT_EQ(draft.lines.back().end.y, 1.0f);

  preview.dragging = true;
  ++preview.revision;
  const auto &dragging = cache.frame(preview);
  EXPECT_EQ(point_count_for(dragging, ViewportOverlayLayer::HoverVertex), 0u);
  EXPECT_EQ(line_count_for(dragging, ViewportOverlayLayer::ToolPlaneLine), 0u);
  EXPECT_EQ(line_count_for(dragging, ViewportOverlayLayer::WireframeBase),
            samples->samples.size() - 1u);
  preview.dragging = false;

  preview.drawing = false;
  ++preview.revision;
  const auto &editing = cache.frame(preview);
  EXPECT_EQ(line_count_for(editing, ViewportOverlayLayer::WireframeBase), 0u);
  EXPECT_EQ(line_count_for(editing, ViewportOverlayLayer::ToolPlaneLine), 0u);
  EXPECT_EQ(line_count_for(editing, ViewportOverlayLayer::ToolHandle), 2u);
  EXPECT_EQ(editing.points.size(), 4u);
  RenderOverlaySelectionSnapshot snapshot;
  snapshot.curves.push_back({.sample_payload = samples});
  snapshot.tool_overlays = editing;
  const auto combined =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);
  EXPECT_EQ(line_count_for(combined, ViewportOverlayLayer::WireframeBase),
            samples->samples.size() - 1u);
  EXPECT_EQ(line_source_count_for(combined, ViewportOverlaySource::SceneCurve),
            samples->samples.size() - 1u);
  EXPECT_EQ(line_source_count_for(combined, ViewportOverlaySource::Tool), 2u);
}

TEST(RenderOverlaySnapshotTests,
     CapturesSelectionGroupLightHierarchyAndToolOverlayRecords) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId group = document.create_empty("Selected Group");
  const EntityId child =
      document.create_entity("Child Mesh", EntityKind::Mesh);
  document.set_mesh(child, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(child, group));
  ASSERT_TRUE(document.set_selected(group, true));

  const EntityId point_light =
      document.create_entity("Point Light", EntityKind::Light);
  document.set_transform(
      point_light, TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
  document.set_light(point_light, LightComponent{.type = LightType::Point});
  ASSERT_TRUE(document.set_selected(point_light, true));

  const EntityId directional_light =
      document.create_entity("Directional Light", EntityKind::Light);
  document.set_light(directional_light,
                     LightComponent{.type = LightType::Directional});
  ASSERT_TRUE(document.set_selected(directional_light, true));

  const EntityId hidden_light =
      document.create_entity("Hidden Light", EntityKind::Light);
  document.set_light(hidden_light, LightComponent{.type = LightType::Point});
  document.set_visible(hidden_light, false);
  ASSERT_TRUE(document.set_selected(hidden_light, true));

  ViewportOverlayFrame tool_frame;
  tool_frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewLine,
      .start = {},
      .end = {1.0f, 0.0f, 0.0f},
      .source = ViewportOverlaySource::Tool,
  });
  tool_frame.hover_edge_dimension_context =
      ViewportHoverEdgeDimensionContext{
          .inside_reference = {2.0F, 3.0F, 4.0F},
          .element = {
              .object_id = 44,
              .edge_a = 8,
              .edge_b = 9,
              .face_id = 10,
          },
      };

  const RenderOverlaySelectionSnapshot snapshot =
      build_render_overlay_selection_snapshot(document, assets, tool_frame);

  EXPECT_EQ(snapshot.scene_revision, document.revision());
  EXPECT_EQ(snapshot.asset_revision, assets.revision());
  ASSERT_EQ(snapshot.hierarchy_selection_wires.size(), 1u);
  EXPECT_EQ(snapshot.hierarchy_selection_wires.front().entity, child);
  EXPECT_EQ(snapshot.hierarchy_selection_wires.front().mesh_id, mesh);
  EXPECT_EQ(snapshot.hierarchy_selection_wires.front().vertices.size(), 3u);
  ASSERT_EQ(snapshot.group_bounds.size(), 1u);
  EXPECT_EQ(snapshot.selected_lights.size(), 1u);
  ASSERT_EQ(snapshot.point_light_gizmos.size(), 1u);
  EXPECT_EQ(snapshot.point_light_gizmos.front().entity, point_light);
  EXPECT_EQ(snapshot.tool_overlays.lines.size(), 1u);
  ASSERT_TRUE(
      snapshot.tool_overlays.hover_edge_dimension_context.has_value());

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);

  EXPECT_EQ(
      line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
      3u);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::GroupBounds), 12u);
  EXPECT_EQ(point_count_for(frame, ViewportOverlayLayer::SelectedVertex), 1u);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 5u);
  EXPECT_EQ(icon_count_for(frame, ViewportOverlayLayer::PointLightGizmo), 0u);
  EXPECT_EQ(line_source_count_for(frame, ViewportOverlaySource::Tool), 1u);
  ASSERT_TRUE(frame.hover_edge_dimension_context.has_value());
  EXPECT_FLOAT_EQ(frame.hover_edge_dimension_context->inside_reference.x,
                  2.0F);
  EXPECT_EQ(frame.hover_edge_dimension_context->element.object_id, 44);
  EXPECT_EQ(frame.hover_edge_dimension_context->element.face_id, 10);

  const ViewportOverlayFrame gizmo_frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
  EXPECT_EQ(icon_count_for(gizmo_frame, ViewportOverlayLayer::PointLightGizmo),
            1u);

  const ViewportOverlayFrame gizmo_only_frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, snapshot, true, RenderOverlaySelectionMode::Object, true, false);
  EXPECT_EQ(
      icon_count_for(gizmo_only_frame, ViewportOverlayLayer::PointLightGizmo),
      1u);
  EXPECT_EQ(line_source_count_for(gizmo_only_frame,
                                  ViewportOverlaySource::HierarchySelection),
            0u);
  EXPECT_EQ(line_count_for(gizmo_only_frame,
                           ViewportOverlayLayer::GroupBounds),
            0u);
  EXPECT_TRUE(gizmo_only_frame.points.empty());
  EXPECT_TRUE(gizmo_only_frame.triangles.empty());
  EXPECT_TRUE(std::all_of(
      gizmo_only_frame.lines.begin(), gizmo_only_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.source == ViewportOverlaySource::Tool;
      }));
}

TEST(RenderOverlaySnapshotTests,
     SelectedPointLightRadiusUsesWorldFalloffAndRefreshesEditability) {
  SceneDocument document;
  AssetRegistry assets;
  const EntityId parent = document.create_empty("Scaled Light Parent");
  document.set_transform(parent, TransformComponent{
      .position = {4.0f, -2.0f, 1.0f}, .scale = {3.0f, 2.0f, 4.0f}});
  const EntityId entity = document.create_entity("Point", EntityKind::Light);
  document.set_light(entity, LightComponent{.type = LightType::Point, .range = 5.0f});
  document.set_transform(entity, TransformComponent{
      .position = {2.0f, 1.0f, -1.0f}, .scale = {2.0f, 3.0f, 0.5f}});
  ASSERT_TRUE(document.set_parent(entity, parent, false));
  ASSERT_TRUE(document.set_selected(entity, true));
  const EntityId other = document.create_entity("Unselected", EntityKind::Light);
  document.set_light(other, LightComponent{.type = LightType::Point});

  RenderOverlaySnapshotCache cache;
  const auto &snapshot = cache.selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.point_light_gizmos.size(), 2u);
  EXPECT_TRUE(snapshot.selected_lights.empty());
  const auto marker = std::find_if(snapshot.point_light_gizmos.begin(),
                                   snapshot.point_light_gizmos.end(),
      [entity](const auto &light) { return light.entity == entity; });
  ASSERT_NE(marker, snapshot.point_light_gizmos.end());
  EXPECT_TRUE(marker->selected);
  EXPECT_TRUE(marker->radius_editable);
  EXPECT_FLOAT_EQ(marker->radius, 5.0f);
  QVec3 center = transform_point(document.world_transform_matrix(entity), {});
  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
  ASSERT_EQ(frame.lines.size(), 360u);
  ASSERT_EQ(frame.icons.size(), 2u);
  for (const auto &line : frame.lines) {
    EXPECT_EQ(line.element.object_id, static_cast<int>(entity));
    EXPECT_NEAR(length(line.start - center), 5.0f, 0.00001f);
    EXPECT_NEAR(length(line.end - center), 5.0f, 0.00001f);
    EXPECT_EQ(line.color, kOverlayPointLightRadiusWireColor);
  }

  const ViewportOverlayCamera camera{.eye = center + QVec3{10.0f, 0.0f, 0.0f},
                                      .forward = {-1.0f, 0.0f, 0.0f},
                                      .right = {0.0f, 0.0f, -1.0f},
                                      .viewport_size = {800.0f, 600.0f}};
  const ViewportOverlayFrame billboard =
      ViewportSceneOverlayBuilder::build_point_light_radius_frame(
          snapshot.point_light_gizmos, camera);
  ASSERT_EQ(billboard.lines.size(), 120u);
  ASSERT_EQ(billboard.triangles.size(), 96u);
  EXPECT_TRUE(billboard.points.empty());
  const auto handle_plan = OverlayRenderableBuilder::build_layer(
      billboard, camera, ViewportOverlayLayer::ToolHandle);
  ASSERT_EQ(handle_plan.face_fill_batches.size(), 1u);
  EXPECT_TRUE(handle_plan.point_batches.empty());
  EXPECT_EQ(handle_plan.face_fill_batches.front().state.depth_func,
            OverlayDepthFunc::Always);
  const QVec3 handle = billboard.triangles.front().a;
  EXPECT_FLOAT_EQ(handle.x, center.x);
  EXPECT_FLOAT_EQ(handle.y, center.y);
  EXPECT_FLOAT_EQ(handle.z, center.z - 5.0f);
  const OverlayCameraProjector projector(camera);
  const auto projected = projector.project(projector.view_point_for(handle));
  PrimitiveCreationControlsFrame controls;
  controls.visible = true;
  controls.controls.front() = {
      .world_position = handle,
      .screen_position = {projected.pixel.x, camera.viewport_size.y - projected.pixel.y},
      .depth = -projector.view_point_for(handle).z,
      .radius_pixels = 6.0f,
      .visible = true,
  };
  ViewportOverlayFrame primitive_handle;
  ViewportPrimitiveCreationOverlayBuilder::append(primitive_handle, camera, controls);
  ASSERT_EQ(primitive_handle.triangles.size(), billboard.triangles.size());
  for (std::size_t index = 0u; index < billboard.triangles.size(); ++index) {
    const auto &actual = billboard.triangles[index];
    const auto &expected = primitive_handle.triangles[index];
    EXPECT_NEAR(length(actual.a - expected.a), 0.0f, 0.00001f);
    EXPECT_NEAR(length(actual.b - expected.b), 0.0f, 0.00001f);
    EXPECT_NEAR(length(actual.c - expected.c), 0.0f, 0.00001f);
    EXPECT_EQ(actual.color, expected.color);
    EXPECT_EQ(actual.layer, expected.layer);
    EXPECT_EQ(actual.source, ViewportOverlaySource::Gizmo);
    EXPECT_EQ(actual.element.object_id, static_cast<int>(entity));
  }
  for (const auto &line : billboard.lines) {
    EXPECT_FLOAT_EQ(line.start.x, center.x);
    EXPECT_NEAR(length(line.start - center), 5.0f, 0.00001f);
  }

  document.set_transform(parent, TransformComponent{
      .position = {1.0f, 4.0f, -2.0f}, .scale = {5.0f, 3.0f, 2.0f}});
  center = transform_point(document.world_transform_matrix(entity), {});
  const auto &moved = cache.selection_snapshot(document, assets);
  const auto moved_frame = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, moved, true);
  ASSERT_EQ(moved_frame.lines.size(), 360u);
  EXPECT_NEAR(length(moved_frame.lines.front().start - center), 5.0f, 0.00001f);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);

  document.set_light(entity, LightComponent{.type = LightType::Point, .range = 8.0f});
  const auto &updated = cache.selection_snapshot(document, assets);
  const auto updated_frame = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, updated, true);
  ASSERT_EQ(updated_frame.lines.size(), 360u);
  EXPECT_NEAR(length(updated_frame.lines.front().start - center), 8.0f, 0.00001f);

  ASSERT_TRUE(document.set_locked(parent, true));
  const auto &locked = cache.selection_snapshot(document, assets);
  const auto locked_frame = ViewportSceneOverlayBuilder::build_point_light_radius_frame(
      locked.point_light_gizmos, camera);
  EXPECT_EQ(locked_frame.lines.size(), 120u);
  EXPECT_TRUE(locked_frame.triangles.empty());
  ASSERT_TRUE(document.set_locked(parent, false));
  ASSERT_TRUE(document.set_selectable(parent, false));
  const auto &unselectable = cache.selection_snapshot(document, assets);
  EXPECT_TRUE(ViewportSceneOverlayBuilder::build_point_light_radius_frame(
      unselectable.point_light_gizmos, camera).triangles.empty());

  ASSERT_TRUE(document.set_visible(parent, false));
  const auto &hidden = cache.selection_snapshot(document, assets);
  EXPECT_TRUE(ViewportSceneOverlayBuilder::build_selection_frame({}, hidden, true)
                  .lines.empty());
  ASSERT_EQ(hidden.point_light_gizmos.size(), 1u);
  EXPECT_EQ(hidden.point_light_gizmos.front().entity, other);
}

TEST(RenderOverlaySnapshotTests,
     PointLightRadiusClampAndOverlayVisibilityMatchLightMarkers) {
  SceneDocument document;
  AssetRegistry assets;
  const EntityId entity = document.create_entity("Point", EntityKind::Light);
  document.set_light(entity, LightComponent{.type = LightType::Point, .range = 0.0f});
  ASSERT_TRUE(document.set_selected(entity, true));
  const auto snapshot = build_render_overlay_selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.point_light_gizmos.size(), 1u);
  EXPECT_FLOAT_EQ(snapshot.point_light_gizmos.front().radius, 0.001f);
  EXPECT_TRUE(ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, false)
                  .lines.empty());
  const auto hidden = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, snapshot, true, RenderOverlaySelectionMode::Object, true, false);
  EXPECT_TRUE(hidden.lines.empty());
  EXPECT_EQ(hidden.icons.size(), 1u);
}

TEST(RenderOverlaySnapshotTests,
     CapturesSelectedObjectAndSceneWireframeRecordsWithDirtyRevisions) {
  SceneDocument document;
  AssetRegistry assets;
  const EntityId root =
      document.create_entity("Scene Settings", EntityKind::Empty);
  SceneSettingsComponent scene_settings;
  scene_settings.camera.near_plane = 0.01f;
  scene_settings.camera.far_plane = 250.0f;
  ASSERT_TRUE(document.set_scene_settings(root, scene_settings));
  const AssetId triangle_mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId quad_mesh = assets.add_mesh(make_authored_quad_mesh());
  const EntityId selected_mesh =
      document.create_entity("Selected Mesh", EntityKind::Mesh);
  document.set_mesh(selected_mesh, MeshComponent{.mesh = triangle_mesh});
  ASSERT_TRUE(document.set_selected(selected_mesh, true));
  const EntityId wireframe_mesh =
      document.create_entity("Wireframe Mesh", EntityKind::Mesh);
  document.set_mesh(wireframe_mesh, MeshComponent{.mesh = quad_mesh});
  const EntityId hidden_selected_mesh =
      document.create_entity("Hidden Selected Mesh", EntityKind::Mesh);
  document.set_mesh(hidden_selected_mesh,
                    MeshComponent{.mesh = triangle_mesh});
  document.set_visible(hidden_selected_mesh, false);
  ASSERT_TRUE(document.set_selected(hidden_selected_mesh, true));

  const ViewportPersistentOverlayStyleSnapshot persistent_style{
      .selection_face_color = rgba8(12, 34, 56, 78),
      .selection_wire_color = rgba8(90, 87, 65, 43),
      .selection_edge_logical_width_pixels = 4.5f,
      .selection_edge_line_width_pixels = 4.5f,
      .face_signature = 101u,
      .wire_signature = 202u,
  };
  const RenderOverlaySelectedObjectSnapshot selected_snapshot =
      build_render_overlay_selected_object_snapshot(document, assets, true,
                                                    persistent_style);

  EXPECT_TRUE(selected_snapshot.visible);
  EXPECT_EQ(selected_snapshot.style.selection_face_color,
            persistent_style.selection_face_color);
  EXPECT_EQ(selected_snapshot.style.face_signature,
            persistent_style.face_signature);
  EXPECT_EQ(selected_snapshot.scene_revision, document.revision());
  EXPECT_EQ(selected_snapshot.asset_revision, assets.revision());
  EXPECT_EQ(selected_snapshot.selection_revision,
            document.selection_revision());
  EXPECT_EQ(selected_snapshot.visibility_revision,
            document.visibility_revision());
  EXPECT_EQ(selected_snapshot.transform_revision,
            document.transform_revision());
  EXPECT_EQ(selected_snapshot.mesh_binding_revision,
            document.mesh_binding_revision());
  EXPECT_FLOAT_EQ(selected_snapshot.clip_planes.near_plane, 0.01f);
  EXPECT_FLOAT_EQ(selected_snapshot.clip_planes.far_plane, 250.0f);
  ASSERT_EQ(selected_snapshot.meshes.size(), 1u);
  EXPECT_EQ(selected_snapshot.meshes.front().entity, selected_mesh);
  EXPECT_EQ(selected_snapshot.meshes.front().mesh_id, triangle_mesh);
  EXPECT_EQ(selected_snapshot.meshes.front().mesh_revision,
            assets.mesh(triangle_mesh)->revision.value);

  const RenderOverlaySelectedObjectSnapshot hidden_snapshot =
      build_render_overlay_selected_object_snapshot(document, assets, false);
  EXPECT_FALSE(hidden_snapshot.visible);
  EXPECT_TRUE(hidden_snapshot.meshes.empty());

  const RenderOverlaySceneWireframeSnapshot wireframe_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 0x03u,
                                                    persistent_style);

  EXPECT_EQ(wireframe_snapshot.scene_revision, document.revision());
  EXPECT_EQ(wireframe_snapshot.asset_revision, assets.revision());
  EXPECT_EQ(wireframe_snapshot.style.selection_wire_color,
            persistent_style.selection_wire_color);
  EXPECT_FLOAT_EQ(
      wireframe_snapshot.style.selection_edge_line_width_pixels,
      persistent_style.selection_edge_line_width_pixels);
  EXPECT_EQ(wireframe_snapshot.style.wire_signature,
            persistent_style.wire_signature);
  EXPECT_FLOAT_EQ(wireframe_snapshot.clip_planes.near_plane, 0.01f);
  EXPECT_FLOAT_EQ(wireframe_snapshot.clip_planes.far_plane, 250.0f);
  EXPECT_EQ(wireframe_snapshot.layer_mask, 0x03u);
  ASSERT_EQ(wireframe_snapshot.meshes.size(), 2u);
  const auto authored_record =
      std::find_if(wireframe_snapshot.meshes.begin(),
                   wireframe_snapshot.meshes.end(),
                   [wireframe_mesh](const RenderOverlayMeshSnapshot &record) {
                     return record.entity == wireframe_mesh;
                   });
  ASSERT_NE(authored_record, wireframe_snapshot.meshes.end());
  EXPECT_TRUE(authored_record->has_authored_polygon_geometry());

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, wireframe_snapshot);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 3u);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::WireframeXray), 4u);
  EXPECT_TRUE(std::all_of(
      frame.lines.begin(), frame.lines.end(),
      [&persistent_style](const ViewportOverlayLine &line) {
        return line.layer != ViewportOverlayLayer::SelectedEdge ||
               (line.color == persistent_style.selection_wire_color &&
                line.width_pixels ==
                    persistent_style.selection_edge_line_width_pixels &&
                line.style_role ==
                    ViewportOverlayStyleRole::SelectionWire);
      }));
}

TEST(RenderOverlaySnapshotTests,
     LayerVisibilityAndWireColorFlowThroughSceneExtraction) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Layered Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  const SceneLayerId layer =
      document.create_layer("Orange", {1.0f, 0.25f, 0.0f});
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(document.set_entity_layer(entity, layer));
  const EntityId point_light =
      document.create_entity("Layered Point Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      point_light, LightComponent{.type = LightType::Point}));
  ASSERT_TRUE(document.set_entity_layer(point_light, layer));

  constexpr int kModelingObjectId = 71;
  ModelingObjectRuntimeMap object_map;
  object_map.bind(kModelingObjectId, entity);
  const std::vector<quader_editor_core::PolygonObject> modeling_objects{
      quader_editor_core::PolygonObject{.id = kModelingObjectId},
  };

  constexpr std::uint32_t kLayerWireColor = rgba8(255, 64, 0, 230);
  const ViewportPersistentOverlayStyleSnapshot style{
      .selection_wire_color = rgba8(12, 34, 56, 255),
      .selection_edge_logical_width_pixels = 3.25f,
      .selection_edge_line_width_pixels = 3.25f,
      .wire_signature = 91u,
  };
  const RenderOverlaySceneWireframeSnapshot unselected_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                    style);
  ASSERT_EQ(unselected_snapshot.meshes.size(), 1u);
  EXPECT_FLOAT_EQ(unselected_snapshot.meshes.front().wireframe_tint.x, 1.0f);
  EXPECT_FLOAT_EQ(unselected_snapshot.meshes.front().wireframe_tint.y, 0.25f);
  EXPECT_FLOAT_EQ(unselected_snapshot.meshes.front().wireframe_tint.z, 0.0f);
  const ViewportOverlayFrame unselected_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, unselected_snapshot);
  EXPECT_EQ(line_count_for(unselected_frame,
                           ViewportOverlayLayer::WireframeXray),
            3u);
  EXPECT_TRUE(std::all_of(
      unselected_frame.lines.begin(), unselected_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer != ViewportOverlayLayer::WireframeXray ||
               line.color == kLayerWireColor;
      }));

  const RenderSceneSnapshot visible_scene =
      build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(visible_scene.live_mesh_entities.contains(entity));
  EXPECT_TRUE(visible_scene.live_light_entities.contains(point_light));
  EXPECT_TRUE(entity_world_bounds(document, assets, entity).has_value());
  EXPECT_EQ(build_render_overlay_selection_snapshot(document, assets)
                .point_light_gizmos.size(),
            1u);
  const ModelingPickingProjection visible_picking =
      PickingCacheProjection::scene_transformed_objects_for_picking(
          modeling_objects, document, object_map, {},
          ModelingSelectionMode::Object, {});
  EXPECT_EQ(visible_picking.objects.size(), 1u);
  ASSERT_TRUE(document.set_selected(entity, true));
  const RenderOverlaySceneWireframeSnapshot selected_wire_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                    style);
  ASSERT_EQ(selected_wire_snapshot.meshes.size(), 1u);
  EXPECT_TRUE(selected_wire_snapshot.meshes.front().selected);
  EXPECT_FLOAT_EQ(selected_wire_snapshot.meshes.front().wireframe_tint.x,
                  1.0f);
  EXPECT_FLOAT_EQ(selected_wire_snapshot.meshes.front().wireframe_tint.y,
                  0.25f);
  EXPECT_FLOAT_EQ(selected_wire_snapshot.meshes.front().wireframe_tint.z,
                  0.0f);
  const ViewportOverlayFrame selected_wire_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, selected_wire_snapshot);
  EXPECT_EQ(line_count_for(selected_wire_frame,
                           ViewportOverlayLayer::SelectedEdge),
            3u);
  EXPECT_EQ(line_count_for(selected_wire_frame,
                           ViewportOverlayLayer::WireframeXray),
            0u);
  EXPECT_TRUE(std::all_of(
      selected_wire_frame.lines.begin(), selected_wire_frame.lines.end(),
      [&style](const ViewportOverlayLine &line) {
        return line.layer != ViewportOverlayLayer::SelectedEdge ||
               (line.color == style.selection_wire_color &&
                line.width_pixels ==
                    style.selection_edge_line_width_pixels &&
                line.style_role ==
                    ViewportOverlayStyleRole::SelectionWire);
      }));

  const std::uint64_t visible_revision = document.visibility_revision();
  ASSERT_TRUE(document.set_layer_visible(layer, false));
  EXPECT_EQ(document.visibility_revision(), visible_revision + 1u);
  ASSERT_NE(document.visibility(entity), nullptr);
  EXPECT_TRUE(document.visibility(entity)->visible)
      << "layer visibility must not overwrite object-local visibility";
  EXPECT_FALSE(document.is_effectively_visible(entity));
  ASSERT_NE(document.selection(entity), nullptr);
  EXPECT_TRUE(document.selection(entity)->selected);
  const RenderSceneSnapshot hidden_scene =
      build_render_scene_snapshot(document, assets);
  EXPECT_FALSE(hidden_scene.live_mesh_entities.contains(entity));
  EXPECT_TRUE(hidden_scene.meshes.empty());
  EXPECT_TRUE(hidden_scene.instance_primitives.empty());
  EXPECT_TRUE(hidden_scene.lights.empty());
  EXPECT_FALSE(hidden_scene.live_light_entities.contains(point_light));
  EXPECT_FALSE(entity_world_bounds(document, assets, entity).has_value());
  const RenderOverlaySelectionSnapshot hidden_selection_snapshot =
      build_render_overlay_selection_snapshot(document, assets);
  EXPECT_TRUE(hidden_selection_snapshot.selected_mesh_bounds.empty());
  EXPECT_TRUE(hidden_selection_snapshot.point_light_gizmos.empty());
  const ModelingPickingProjection hidden_picking =
      PickingCacheProjection::scene_transformed_objects_for_picking(
          modeling_objects, document, object_map, {},
          ModelingSelectionMode::Object, {});
  EXPECT_TRUE(hidden_picking.objects.empty());
  EXPECT_TRUE(build_render_overlay_scene_wireframe_snapshot(document, assets,
                                                            1u)
                  .meshes.empty());
  EXPECT_TRUE(build_render_overlay_selected_object_snapshot(document, assets,
                                                            true)
                  .meshes.empty());

  ASSERT_TRUE(document.set_layer_visible(layer, true));
  EXPECT_TRUE(document.is_effectively_visible(entity));
  EXPECT_TRUE(document.visibility(entity)->visible);
  const RenderSceneSnapshot restored_scene =
      build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(restored_scene.live_mesh_entities.contains(entity));
  EXPECT_TRUE(restored_scene.live_light_entities.contains(point_light));
  EXPECT_TRUE(entity_world_bounds(document, assets, entity).has_value());
  EXPECT_EQ(build_render_overlay_selection_snapshot(document, assets)
                .selected_mesh_bounds.size(),
            1u);
  EXPECT_EQ(PickingCacheProjection::scene_transformed_objects_for_picking(
                modeling_objects, document, object_map, {},
                ModelingSelectionMode::Object, {})
                .objects.size(),
            1u);
  const RenderOverlaySceneWireframeSnapshot restored_wire_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  ASSERT_EQ(restored_wire_snapshot.meshes.size(), 1u);
  EXPECT_FLOAT_EQ(restored_wire_snapshot.meshes.front().wireframe_tint.x,
                  1.0f);
  EXPECT_FLOAT_EQ(restored_wire_snapshot.meshes.front().wireframe_tint.y,
                  0.25f);
  EXPECT_FLOAT_EQ(restored_wire_snapshot.meshes.front().wireframe_tint.z,
                  0.0f);

  const std::uint64_t layer_revision = document.layer_revision();
  ASSERT_TRUE(document.set_layer_locked(layer, true));
  EXPECT_EQ(document.layer_revision(), layer_revision + 1u);
  ASSERT_NE(document.lock(entity), nullptr);
  EXPECT_FALSE(document.lock(entity)->locked)
      << "layer locking must not rewrite the local lock component";
  EXPECT_TRUE(document.is_effectively_locked(entity));
  EXPECT_TRUE(build_render_scene_snapshot(document, assets)
                  .live_mesh_entities.contains(entity))
      << "locking affects interaction, not rendering";
  EXPECT_TRUE(PickingCacheProjection::scene_transformed_objects_for_picking(
                  modeling_objects, document, object_map, {},
                  ModelingSelectionMode::Object, {})
                  .objects.empty());

  ASSERT_TRUE(document.set_layer_locked(layer, false));
  EXPECT_FALSE(document.is_effectively_locked(entity));
  EXPECT_EQ(PickingCacheProjection::scene_transformed_objects_for_picking(
                modeling_objects, document, object_map, {},
                ModelingSelectionMode::Object, {})
                .objects.size(),
            1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     StableSourcesReuseAllOverlayExtractionsUntilOwnerRevisionChanges) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Cached Overlay Mesh", EntityKind::Mesh);
  document.set_mesh(entity, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_selected(entity, true));

  const ViewportPersistentOverlayStyleSnapshot style{
      .face_signature = 101u,
      .wire_signature = 202u,
  };
  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate first =
      cache.update_persistent(document, assets, true, 0x01u, style);
  ASSERT_NE(first.selected_object, nullptr);
  ASSERT_NE(first.scene_wireframe, nullptr);
  EXPECT_TRUE(first.selected_object_changed);
  EXPECT_TRUE(first.scene_wireframe_changed);
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  const RenderOverlayPersistentSnapshotUpdate stable =
      cache.update_persistent(document, assets, true, 0x01u, style);
  EXPECT_FALSE(stable.changed());
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  const auto *wire_storage = stable.scene_wireframe->meshes.data();
  const auto wire_geometry =
      stable.scene_wireframe->meshes.front().wire_geometry_payload;
  ViewportPersistentOverlayStyleSnapshot context_style = style;
  context_style.instance_edit_context_signature = 303u;
  context_style.instance_edit_style.context_strength = 0.7f;
  const RenderOverlayPersistentSnapshotUpdate recolored =
      cache.update_persistent(document, assets, true, 0x01u, context_style);
  EXPECT_FALSE(recolored.selected_object_changed);
  ASSERT_TRUE(recolored.scene_wireframe_changed);
  ASSERT_NE(recolored.scene_wireframe, nullptr);
  EXPECT_EQ(recolored.scene_wireframe->meshes.data(), wire_storage);
  EXPECT_EQ(recolored.scene_wireframe->meshes.front().wire_geometry_payload,
            wire_geometry);
  EXPECT_FLOAT_EQ(recolored.scene_wireframe->style.instance_edit_style
                      .context_strength,
                  0.7f);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_FALSE(cache.update_persistent(document, assets, true, 0x01u,
                                      context_style)
                   .changed());

  document.set_transform(
      entity, TransformComponent{.position = {3.0f, 2.0f, 1.0f}});
  const RenderOverlayPersistentSnapshotUpdate transformed =
      cache.update_persistent(document, assets, true, 0x01u, style);
  EXPECT_TRUE(transformed.selected_object_changed);
  EXPECT_TRUE(transformed.scene_wireframe_changed);
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selected_object_build_count, 2u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     DistinctPaneRasterGroupsInvalidateMetadataWithoutRebuildingGeometry) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Scaled Persistent Wire", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(entity, true));

  const ViewportSettings settings = ViewportSettings::defaults();
  const ViewportPersistentOverlayStyleSnapshot semantic_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings, 1.0f);
  const ViewportPersistentOverlayStyleSnapshot scale_one = semantic_style;
  const ViewportPersistentOverlayStyleSnapshot scale_two =
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings, 2.0f);
  ViewportPersistentOverlayRasterGroupSet raster_groups;
  ASSERT_TRUE(raster_groups.add(0x04u, scale_one));
  ASSERT_TRUE(raster_groups.add(0x08u, scale_two));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate initial =
      cache.update_persistent(document, assets, true, 0u, semantic_style, {},
                              nullptr, true, {}, raster_groups);
  ASSERT_TRUE(initial.selected_object_changed);
  ASSERT_TRUE(initial.scene_wireframe_changed);
  ASSERT_NE(initial.selected_object, nullptr);
  ASSERT_NE(initial.scene_wireframe, nullptr);
  ASSERT_EQ(initial.selected_object->raster_groups.size(), 2u);
  ASSERT_EQ(initial.scene_wireframe->raster_groups.size(), 2u);
  ASSERT_EQ(initial.scene_wireframe->meshes.size(), 1u);
  EXPECT_TRUE(initial.scene_wireframe->meshes.front().selected)
      << "selected whole-mesh wires remain relevant without a wireframe pane";
  const auto *selected_mesh_storage = initial.selected_object->meshes.data();
  const auto *wire_mesh_storage = initial.scene_wireframe->meshes.data();
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);

  const RenderOverlayPersistentSnapshotUpdate stable =
      cache.update_persistent(document, assets, true, 0u, semantic_style, {},
                              nullptr, true, {}, raster_groups);
  EXPECT_FALSE(stable.changed());
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);

  ViewportPersistentOverlayRasterGroupSet changed_groups;
  ASSERT_TRUE(changed_groups.add(0x04u, scale_one));
  const ViewportPersistentOverlayStyleSnapshot scale_three =
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings, 3.0f);
  ASSERT_TRUE(changed_groups.add(0x08u, scale_three));
  const RenderOverlayPersistentSnapshotUpdate scale_changed =
      cache.update_persistent(document, assets, true, 0u, semantic_style, {},
                              nullptr, true, {}, changed_groups);
  EXPECT_TRUE(scale_changed.selected_object_changed);
  EXPECT_TRUE(scale_changed.scene_wireframe_changed);
  ASSERT_EQ(scale_changed.scene_wireframe->raster_groups.size(), 2u);
  EXPECT_NE(scale_changed.scene_wireframe->raster_groups.groups()[0]
                .style.wire_signature,
            scale_changed.scene_wireframe->raster_groups.groups()[1]
                .style.wire_signature);
  EXPECT_EQ(scale_changed.selected_object->meshes.data(),
            selected_mesh_storage);
  EXPECT_EQ(scale_changed.scene_wireframe->meshes.data(), wire_mesh_storage);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u)
      << "a pane-only raster change must preserve borrowed geometry storage";
}

TEST(RenderOverlaySnapshotCacheTests,
     GroupSelectionWireStaysCurrentWithoutASelectedFaceOrWireframePane) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Group Persistent Wire", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(entity, true));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate selected =
      cache.update_persistent(document, assets, false, 0u, {}, {}, nullptr,
                              true);
  ASSERT_TRUE(selected.scene_wireframe_changed);
  ASSERT_NE(selected.scene_wireframe, nullptr);
  ASSERT_EQ(selected.scene_wireframe->meshes.size(), 1u);
  EXPECT_TRUE(selected.scene_wireframe->meshes.front().selected);

  ASSERT_TRUE(document.set_selected(entity, false));
  const RenderOverlayPersistentSnapshotUpdate deselected =
      cache.update_persistent(document, assets, false, 0u, {}, {}, nullptr,
                              true);
  ASSERT_TRUE(deselected.scene_wireframe_changed);
  ASSERT_NE(deselected.scene_wireframe, nullptr);
  ASSERT_EQ(deselected.scene_wireframe->meshes.size(), 1u);
  EXPECT_FALSE(deselected.scene_wireframe->meshes.front().selected);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);
}

TEST(RenderOverlaySnapshotCacheTests,
     ComponentPresentationDoesNotPromoteTheWholeSceneWireToSelected) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Component Wire Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(entity, true));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate component =
      cache.update_persistent(document, assets, false, 0x01u, {}, {}, nullptr,
                              false);
  ASSERT_TRUE(component.scene_wireframe_changed);
  ASSERT_NE(component.scene_wireframe, nullptr);
  ASSERT_EQ(component.scene_wireframe->meshes.size(), 1u);
  EXPECT_FALSE(component.scene_wireframe->selected_mesh_wire_visible);
  EXPECT_FALSE(component.scene_wireframe->meshes.front().selected)
      << "Component overlays own selected face/edge/vertex highlighting; the "
         "persistent scene wire must remain the ordinary source color.";

  const RenderOverlayPersistentSnapshotUpdate object =
      cache.update_persistent(document, assets, true, 0x01u, {});
  EXPECT_TRUE(object.scene_wireframe_changed);
  ASSERT_NE(object.scene_wireframe, nullptr);
  ASSERT_EQ(object.scene_wireframe->meshes.size(), 1u);
  EXPECT_TRUE(object.scene_wireframe->selected_mesh_wire_visible);
  EXPECT_TRUE(object.scene_wireframe->meshes.front().selected);
}

TEST(RenderOverlaySnapshotCacheTests,
     LocalVisibilityRevisionFiltersEveryBranchAndGatesStableFrames) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId isolated_mesh =
      document.create_entity("Local Overlay Mesh", EntityKind::Mesh);
  const EntityId other_mesh =
      document.create_entity("Other Overlay Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(isolated_mesh, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_mesh(other_mesh, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(isolated_mesh, true));

  const EntityId isolated_light =
      document.create_entity("Local Overlay Light", EntityKind::Light);
  const EntityId other_light =
      document.create_entity("Other Overlay Light", EntityKind::Light);
  ASSERT_TRUE(document.set_light(
      isolated_light, LightComponent{.type = LightType::Point}));
  ASSERT_TRUE(document.set_light(
      other_light, LightComponent{.type = LightType::Point}));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate unfiltered =
      cache.update_persistent(document, assets, true, 1u, {});
  ASSERT_NE(unfiltered.scene_wireframe, nullptr);
  EXPECT_EQ(unfiltered.scene_wireframe->meshes.size(), 2u);
  EXPECT_EQ(cache.selection_snapshot(document, assets)
                .point_light_gizmos.size(),
            2u);

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 4> baseline{
      isolated_mesh, other_mesh, isolated_light, other_light};
  const std::array<ViewportLocalVisibilityEntityId, 2> members{
      isolated_mesh, isolated_light};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  const ViewportLocalVisibilitySnapshot entered =
      workspace.local_visibility_snapshot();
  const RenderOverlayPersistentSnapshotUpdate isolated =
      cache.update_persistent(document, assets, true, 1u, {}, {}, &entered);
  EXPECT_TRUE(isolated.selected_object_changed);
  EXPECT_TRUE(isolated.scene_wireframe_changed);
  ASSERT_NE(isolated.selected_object, nullptr);
  ASSERT_NE(isolated.scene_wireframe, nullptr);
  EXPECT_EQ(isolated.selected_object->local_visibility_revision,
            entered.revision());
  EXPECT_EQ(isolated.selected_object->meshes.size(), 1u);
  EXPECT_EQ(isolated.scene_wireframe->meshes.size(), 1u);
  const RenderOverlaySelectionSnapshot &isolated_selection =
      cache.selection_snapshot(document, assets, {}, &entered);
  EXPECT_EQ(isolated_selection.local_visibility_revision,
            entered.revision());
  ASSERT_EQ(isolated_selection.point_light_gizmos.size(), 1u);
  EXPECT_EQ(isolated_selection.point_light_gizmos.front().entity,
            isolated_light);
  EXPECT_EQ(cache.stats().selected_object_build_count, 2u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_build_count, 2u);

  const RenderOverlayPersistentSnapshotUpdate stable =
      cache.update_persistent(document, assets, true, 1u, {}, {}, &entered);
  EXPECT_FALSE(stable.changed());
  (void)cache.selection_snapshot(document, assets, {}, &entered);
  EXPECT_EQ(cache.stats().selected_object_build_count, 2u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_build_count, 2u);

  ASSERT_TRUE(workspace.exit_local_visibility());
  const ViewportLocalVisibilitySnapshot exited =
      workspace.local_visibility_snapshot();
  const RenderOverlayPersistentSnapshotUpdate restored =
      cache.update_persistent(document, assets, true, 1u, {}, {}, &exited);
  EXPECT_TRUE(restored.changed());
  ASSERT_NE(restored.scene_wireframe, nullptr);
  EXPECT_EQ(restored.scene_wireframe->meshes.size(), 2u);
  EXPECT_EQ(cache.selection_snapshot(document, assets, {}, &exited)
                .point_light_gizmos.size(),
            2u);
  EXPECT_EQ(cache.stats().selected_object_build_count, 3u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 3u);
  EXPECT_EQ(cache.stats().selection_build_count, 3u);
}

TEST(RenderOverlaySnapshotCacheTests,
     LayerColorChangeRebuildsOnlyWireframeAndEquivalentColorIsStable) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Layer Color Cache Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
  const SceneLayerId layer = document.create_layer("Cache Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(document.set_entity_layer(entity, layer));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate initial =
      cache.update_persistent(document, assets, true, 1u, {});
  ASSERT_TRUE(initial.selected_object_changed);
  ASSERT_TRUE(initial.scene_wireframe_changed);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);

  const QVec3 changed_color{0.125f, 0.5f, 1.0f};
  ASSERT_TRUE(document.set_layer_wireframe_color(layer, changed_color));
  const RenderOverlayPersistentSnapshotUpdate changed =
      cache.update_persistent(document, assets, true, 1u, {});
  EXPECT_FALSE(changed.selected_object_changed);
  ASSERT_TRUE(changed.scene_wireframe_changed);
  ASSERT_NE(changed.scene_wireframe, nullptr);
  ASSERT_EQ(changed.scene_wireframe->meshes.size(), 1u);
  EXPECT_FLOAT_EQ(changed.scene_wireframe->meshes.front().wireframe_tint.x,
                  changed_color.x);
  EXPECT_FLOAT_EQ(changed.scene_wireframe->meshes.front().wireframe_tint.y,
                  changed_color.y);
  EXPECT_FLOAT_EQ(changed.scene_wireframe->meshes.front().wireframe_tint.z,
                  changed_color.z);
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);

  const std::uint64_t revision = document.revision();
  ASSERT_TRUE(document.set_layer_wireframe_color(layer, changed_color));
  EXPECT_EQ(document.revision(), revision);
  const RenderOverlayPersistentSnapshotUpdate stable =
      cache.update_persistent(document, assets, true, 1u, {});
  EXPECT_FALSE(stable.changed());
  EXPECT_EQ(cache.stats().selected_object_build_count, 1u);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 2u);
}

TEST(RenderOverlaySnapshotCacheTests,
     TransformOnlyRefreshMatchesFullSelectionSnapshotWithoutReordering) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId group = document.create_empty("Moving Group");
  const EntityId group_child =
      document.create_entity("Moving Group Child", EntityKind::Mesh);
  document.set_mesh(group_child, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(group_child, group, false));

  const EntityId definition =
      document.create_entity("Moving Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId definition_member =
      document.create_entity("Moving Definition Member", EntityKind::Mesh);
  document.set_mesh(definition_member, MeshComponent{.mesh = mesh});
  document.set_transform(
      definition_member,
      TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(definition_member, definition, false));
  const EntityId occurrence =
      document.create_entity("Moving Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(occurrence, group, false));
  ASSERT_TRUE(document.set_selected(group, true));

  const EntityId selected_mesh =
      document.create_entity("Moving Selected Mesh", EntityKind::Mesh);
  document.set_mesh(selected_mesh, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_selected(selected_mesh, true));

  const EntityId directional_light =
      document.create_entity("Moving Directional Light", EntityKind::Light);
  document.set_light(
      directional_light, LightComponent{.type = LightType::Directional});
  ASSERT_TRUE(document.set_selected(directional_light, true));

  const EntityId point_light =
      document.create_entity("Moving Point Light", EntityKind::Light);
  document.set_light(point_light, LightComponent{.type = LightType::Point});

  RenderOverlaySnapshotCache cache;
  const RenderOverlaySelectionSnapshot &initial =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.hierarchy_selection_wires.size(), 2u);
  ASSERT_EQ(initial.group_bounds.size(), 1u);
  ASSERT_EQ(initial.selected_mesh_bounds.size(), 1u);
  ASSERT_EQ(initial.selected_lights.size(), 1u);
  ASSERT_EQ(initial.point_light_gizmos.size(), 1u);

  const std::array<SceneTransformAssignment, 4> transforms{{
      {
          .entity = group,
          .transform = TransformComponent{
              .position = {4.0f, -2.0f, 1.0f},
              .rotation_euler = {0.2f, 0.4f, -0.1f},
          },
      },
      {
          .entity = selected_mesh,
          .transform = TransformComponent{
              .position = {-3.0f, 5.0f, 2.0f},
              .rotation_euler = {-0.3f, 0.1f, 0.6f},
              .scale = {1.5f, 0.75f, 2.0f},
          },
      },
      {
          .entity = directional_light,
          .transform = TransformComponent{.position = {7.0f, 8.0f, 9.0f}},
      },
      {
          .entity = point_light,
          .transform = TransformComponent{.position = {-7.0f, 3.0f, 6.0f}},
      },
  }};
  document.set_transforms(transforms);

  const RenderOverlaySelectionSnapshot &refreshed =
      cache.selection_snapshot(document, assets);
  const RenderOverlaySelectionSnapshot rebuilt =
      build_render_overlay_selection_snapshot(document, assets);

  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
  EXPECT_EQ(refreshed.scene_revision, rebuilt.scene_revision);
  EXPECT_EQ(refreshed.asset_revision, rebuilt.asset_revision);
  EXPECT_EQ(refreshed.selection_revision, rebuilt.selection_revision);
  EXPECT_EQ(refreshed.visibility_revision, rebuilt.visibility_revision);
  EXPECT_EQ(refreshed.transform_revision, rebuilt.transform_revision);
  EXPECT_EQ(refreshed.mesh_binding_revision,
            rebuilt.mesh_binding_revision);
  EXPECT_EQ(refreshed.render_component_revision,
            rebuilt.render_component_revision);

  ASSERT_EQ(refreshed.hierarchy_selection_wires.size(),
            rebuilt.hierarchy_selection_wires.size());
  for (std::size_t index = 0;
       index < refreshed.hierarchy_selection_wires.size(); ++index) {
    const RenderOverlayMeshSnapshot &actual =
        refreshed.hierarchy_selection_wires[index];
    const RenderOverlayMeshSnapshot &expected =
        rebuilt.hierarchy_selection_wires[index];
    EXPECT_EQ(actual.entity, expected.entity);
    EXPECT_EQ(actual.mesh_id, expected.mesh_id);
    EXPECT_EQ(actual.mesh_revision, expected.mesh_revision);
    EXPECT_EQ(actual.selected, expected.selected);
    EXPECT_EQ(actual.world_transform.values,
              expected.world_transform.values);
  }

  ASSERT_EQ(refreshed.group_bounds.size(), rebuilt.group_bounds.size());
  for (std::size_t index = 0; index < refreshed.group_bounds.size(); ++index) {
    expect_bounds_snapshot_near(
        refreshed.group_bounds[index],
        SceneBounds{.min = rebuilt.group_bounds[index].min,
                    .max = rebuilt.group_bounds[index].max});
  }
  ASSERT_EQ(refreshed.selected_mesh_bounds.size(),
            rebuilt.selected_mesh_bounds.size());
  for (std::size_t index = 0; index < refreshed.selected_mesh_bounds.size();
       ++index) {
    expect_bounds_snapshot_near(
        refreshed.selected_mesh_bounds[index],
        SceneBounds{.min = rebuilt.selected_mesh_bounds[index].min,
                    .max = rebuilt.selected_mesh_bounds[index].max});
  }

  ASSERT_EQ(refreshed.selected_lights.size(), rebuilt.selected_lights.size());
  for (std::size_t index = 0; index < refreshed.selected_lights.size();
       ++index) {
    EXPECT_EQ(refreshed.selected_lights[index].entity,
              rebuilt.selected_lights[index].entity);
    EXPECT_EQ(refreshed.selected_lights[index].type,
              rebuilt.selected_lights[index].type);
    EXPECT_EQ(refreshed.selected_lights[index].world_transform.values,
              rebuilt.selected_lights[index].world_transform.values);
  }
  ASSERT_EQ(refreshed.point_light_gizmos.size(),
            rebuilt.point_light_gizmos.size());
  for (std::size_t index = 0; index < refreshed.point_light_gizmos.size();
       ++index) {
    EXPECT_EQ(refreshed.point_light_gizmos[index].entity,
              rebuilt.point_light_gizmos[index].entity);
    EXPECT_EQ(refreshed.point_light_gizmos[index].type,
              rebuilt.point_light_gizmos[index].type);
    EXPECT_EQ(refreshed.point_light_gizmos[index].world_transform.values,
              rebuilt.point_light_gizmos[index].world_transform.values);
    EXPECT_EQ(refreshed.point_light_gizmos[index].radius,
              rebuilt.point_light_gizmos[index].radius);
    EXPECT_EQ(refreshed.point_light_gizmos[index].selected,
              rebuilt.point_light_gizmos[index].selected);
    EXPECT_EQ(refreshed.point_light_gizmos[index].radius_editable,
              rebuilt.point_light_gizmos[index].radius_editable);
  }
}

TEST(RenderOverlaySnapshotCacheTests,
     TransformFastPathRejectsEveryStructuralOverlaySourceChange) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId first_mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId second_mesh = assets.add_mesh(make_authored_quad_mesh());
  const EntityId parent = document.create_empty("Hierarchy Parent");
  const EntityId selected =
      document.create_entity("Selected Mesh", EntityKind::Mesh);
  document.set_mesh(selected, MeshComponent{.mesh = first_mesh});
  ASSERT_TRUE(document.set_selected(selected, true));
  const EntityId second =
      document.create_entity("Second Mesh", EntityKind::Mesh);
  document.set_mesh(second, MeshComponent{.mesh = first_mesh});
  const EntityId light =
      document.create_entity("Point Light", EntityKind::Light);
  document.set_light(light, LightComponent{.type = LightType::Point});

  RenderOverlaySnapshotCache cache;
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  ASSERT_TRUE(document.set_selected(second, true));
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 0u);

  document.set_transform(
      selected, TransformComponent{.position = {1.0f, 2.0f, 3.0f}});
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);

  ASSERT_TRUE(document.set_visible(second, false));
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 3u);

  document.set_mesh(selected, MeshComponent{.mesh = second_mesh});
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 4u);

  document.set_light(light,
                     LightComponent{.type = LightType::Point,
                                    .intensity = 2.0f});
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 5u);

  (void)assets.add_mesh(make_overlay_triangle_mesh());
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 6u);

  ASSERT_TRUE(document.set_parent(selected, parent, false));
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_build_count, 7u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     SelectionWorldBoundsTranslateExactlyAndFallBackForLinearChanges) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Moving Selection Cage", EntityKind::Mesh);
  document.set_mesh(entity, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_selected(entity, true));

  RenderOverlaySnapshotCache cache;
  const RenderOverlaySelectionSnapshot &initial =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.selected_mesh_bounds.size(), 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 0u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 0u);

  document.set_transform(
      entity,
      TransformComponent{
          .position = {3.0f, -2.0f, 5.0f},
          .rotation_euler = {0.31f, -0.47f, 0.19f},
          .scale = {1.7f, 0.6f, 2.2f},
      });
  const std::optional<SceneBounds> expected =
      entity_world_bounds(document, assets, entity);
  ASSERT_TRUE(expected.has_value());

  const RenderOverlaySelectionSnapshot &transformed =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(transformed.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(transformed.selected_mesh_bounds.front(),
                              *expected);
  const ViewportOverlayFrame overlay =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, transformed, false, RenderOverlaySelectionMode::Object);
  EXPECT_EQ(line_source_count_for(overlay, ViewportOverlaySource::GroupBounds),
            12u);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 0u);

  document.set_transform(
      entity,
      TransformComponent{
          .position = {8.0f, 1.0f, -4.0f},
          .rotation_euler = {0.31f, -0.47f, 0.19f},
          .scale = {1.7f, 0.6f, 2.2f},
      });
  const std::optional<SceneBounds> translated_expected =
      entity_world_bounds(document, assets, entity);
  ASSERT_TRUE(translated_expected.has_value());
  const RenderOverlaySelectionSnapshot &translated =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(translated.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(translated.selected_mesh_bounds.front(),
                              *translated_expected);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 2u);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     ParentedMoveTranslatesWorldBoundsAndParentRotationFallsBackExactly) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId parent =
      document.create_entity("Selection Cage Parent", EntityKind::Empty);
  const EntityId child =
      document.create_entity("Parented Selection Cage", EntityKind::Mesh);
  document.set_mesh(child, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(child, parent, false));
  document.set_transform(
      parent,
      TransformComponent{
          .position = {1.0f, 2.0f, 3.0f},
          .rotation_euler = {0.2f, -0.3f, 0.4f},
          .scale = {1.5f, 0.8f, 1.2f},
      });
  document.set_transform(
      child, TransformComponent{.position = {2.0f, -1.0f, 0.5f}});
  ASSERT_TRUE(document.set_selected(child, true));

  RenderOverlaySnapshotCache cache;
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);

  document.set_transform(
      parent,
      TransformComponent{
          .position = {6.0f, -4.0f, 8.0f},
          .rotation_euler = {0.2f, -0.3f, 0.4f},
          .scale = {1.5f, 0.8f, 1.2f},
      });
  const std::optional<SceneBounds> moved_expected =
      entity_world_bounds(document, assets, child);
  ASSERT_TRUE(moved_expected.has_value());
  const RenderOverlaySelectionSnapshot &moved =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(moved.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(moved.selected_mesh_bounds.front(),
                              *moved_expected);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);

  document.set_transform(
      parent,
      TransformComponent{
          .position = {6.0f, -4.0f, 8.0f},
          .rotation_euler = {0.5f, -0.1f, 0.25f},
          .scale = {1.5f, 0.8f, 1.2f},
      });
  const std::optional<SceneBounds> rotated_expected =
      entity_world_bounds(document, assets, child);
  ASSERT_TRUE(rotated_expected.has_value());
  const RenderOverlaySelectionSnapshot &rotated =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(rotated.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(rotated.selected_mesh_bounds.front(),
                              *rotated_expected);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     LocalMeshBoundsReuseUnrelatedAssetChangesAndInvalidateOnMeshRevision) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Revision Selection Cage", EntityKind::Mesh);
  document.set_mesh(entity, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_selected(entity, true));

  RenderOverlaySnapshotCache cache;
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 0u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);

  (void)assets.add_mesh(make_authored_quad_mesh());
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 0u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);

  ASSERT_TRUE(assets.update_mesh(mesh, [](MeshAsset &updated) {
    MeshGeometryPayload &geometry = updated.edit_geometry();
    geometry.vertices.front().position = {-4.0f, -3.0f, -2.0f};
    geometry.vertices.back().position = {6.0f, 5.0f, 7.0f};
  }));
  const std::optional<SceneBounds> expected =
      entity_world_bounds(document, assets, entity);
  ASSERT_TRUE(expected.has_value());

  const RenderOverlaySelectionSnapshot &revised =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(revised.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(revised.selected_mesh_bounds.front(), *expected);
  EXPECT_EQ(cache.stats().selection_build_count, 3u);
  EXPECT_EQ(cache.stats().selection_local_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_local_bounds_reuse_count, 0u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);
}

TEST(RenderOverlaySnapshotCacheTests,
     SceneBindingChangeInvalidatesEqualRevisionOverlaySnapshots) {
  SceneDocument document;
  AssetRegistry assets;
  RenderOverlaySnapshotCache cache;
  const RenderSceneBindingKey first_binding{
      .scene_session_id = 0x100000000ull,
      .activation_generation = 7u,
      .binding_revision = 11u,
  };
  const RenderSceneBindingKey second_binding{
      .scene_session_id = first_binding.scene_session_id,
      .activation_generation = first_binding.activation_generation + 1u,
      .binding_revision = first_binding.binding_revision + 1u,
  };

  const RenderOverlaySelectionSnapshot &first =
      cache.selection_snapshot(document, assets, first_binding);
  EXPECT_EQ(first.scene_binding, first_binding);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  (void)cache.selection_snapshot(document, assets, first_binding);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  const RenderOverlaySelectionSnapshot &rebound =
      cache.selection_snapshot(document, assets, second_binding);
  EXPECT_EQ(rebound.scene_binding, second_binding);
  EXPECT_EQ(cache.stats().selection_build_count, 2u);

  const RenderOverlayPersistentSnapshotUpdate initial =
      cache.update_persistent(document, assets, true, 1u, {},
                              second_binding);
  EXPECT_TRUE(initial.selected_object_changed);
  EXPECT_TRUE(initial.scene_wireframe_changed);
  const RenderOverlayPersistentSnapshotUpdate unchanged =
      cache.update_persistent(document, assets, true, 1u, {},
                              second_binding);
  EXPECT_FALSE(unchanged.changed());

  const RenderSceneBindingKey third_binding{
      .scene_session_id = first_binding.scene_session_id + 1u,
      .activation_generation = second_binding.activation_generation + 1u,
      .binding_revision = second_binding.binding_revision + 1u,
  };
  const RenderOverlayPersistentSnapshotUpdate switched =
      cache.update_persistent(document, assets, true, 1u, {}, third_binding);
  EXPECT_TRUE(switched.selected_object_changed);
  EXPECT_TRUE(switched.scene_wireframe_changed);
  ASSERT_NE(switched.selected_object, nullptr);
  ASSERT_NE(switched.scene_wireframe, nullptr);
  EXPECT_EQ(switched.selected_object->scene_binding, third_binding);
  EXPECT_EQ(switched.scene_wireframe->scene_binding, third_binding);
}

TEST(RenderSceneSnapshotTests, BuildsValueSpanRecordsAndTracksDirtyState) {
  SceneDocument document;
  AssetRegistry assets;

  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId material =
      assets.add_material(make_default_quader_material("Render Material"));
  const EntityId renderable =
      document.create_entity("Renderable", EntityKind::Mesh);
  document.set_mesh(renderable, MeshComponent{.mesh = mesh,
                                              .cast_shadows = false,
                                              .double_sided = true});
  document.set_material(renderable, MaterialComponent{.material = material});

  const EntityId missing =
      document.create_entity("Missing Mesh", EntityKind::Mesh);
  document.set_mesh(missing, MeshComponent{.mesh = mesh + 1000u});

  const EntityId hidden =
      document.create_entity("Hidden Mesh", EntityKind::Mesh);
  document.set_mesh(hidden, MeshComponent{.mesh = mesh});
  document.set_visible(hidden, false);

  const EntityId light = document.create_entity("Light", EntityKind::Light);
  document.set_light(light, LightComponent{.type = LightType::Point});

  const RenderSceneSnapshot initial =
      build_render_scene_snapshot(document, assets, {.frame_index = 1u});
  ASSERT_EQ(initial.meshes.size(), 2u);
  ASSERT_EQ(initial.lights.size(), 1u);
  EXPECT_EQ(initial.frame_index, 1u);
  EXPECT_EQ(initial.scene_revision, document.revision());
  EXPECT_EQ(initial.selection_revision, document.selection_revision());
  EXPECT_EQ(initial.visibility_revision, document.visibility_revision());
  EXPECT_EQ(initial.transform_revision, document.transform_revision());
  EXPECT_EQ(initial.mesh_binding_revision, document.mesh_binding_revision());
  EXPECT_EQ(initial.asset_revision, assets.revision());
  EXPECT_TRUE(initial.dirty.any_dirty());
  EXPECT_FALSE(initial.reused_previous_records);

  const auto renderable_record =
      std::ranges::find_if(initial.meshes, [renderable](const auto &record) {
        return record.entity_id == renderable;
      });
  ASSERT_NE(renderable_record, initial.meshes.end());
  ASSERT_NE(assets.mesh(mesh), nullptr);
  EXPECT_TRUE(renderable_record->renderable());
  EXPECT_EQ(renderable_record->mesh.mesh_id, mesh);
  EXPECT_EQ(renderable_record->mesh.mesh_revision,
            assets.mesh(mesh)->revision.value);
  EXPECT_EQ(renderable_record->mesh.vertices.data(),
            assets.mesh(mesh)->geometry().vertices.data());
  EXPECT_EQ(renderable_record->mesh.indices.size(),
            assets.mesh(mesh)->geometry().indices.size());
  EXPECT_EQ(renderable_record->material_id, material);
  EXPECT_EQ(renderable_record->material_revision,
            render_material_binding_signature(renderable_record->material));
  EXPECT_NE(&renderable_record->mesh_component, document.mesh(renderable));
  ASSERT_TRUE(renderable_record->material.present);
  EXPECT_EQ(renderable_record->material.component.material, material);
  EXPECT_FALSE(renderable_record->cast_shadows());
  EXPECT_TRUE(renderable_record->double_sided());
  EXPECT_TRUE(initial.live_mesh_entities.contains(renderable));

  const auto missing_record =
      std::ranges::find_if(initial.meshes, [missing](const auto &record) {
        return record.entity_id == missing;
      });
  ASSERT_NE(missing_record, initial.meshes.end());
  EXPECT_FALSE(missing_record->renderable());
  EXPECT_FALSE(missing_record->mesh.present);
  EXPECT_FALSE(initial.live_mesh_entities.contains(missing));
  EXPECT_FALSE(initial.live_mesh_entities.contains(hidden));

  EXPECT_EQ(initial.lights.front().entity_id, light);
  EXPECT_EQ(initial.lights.front().rebuild_revision,
            RenderSignatureBuilder::light_signature(
                initial.lights.front().light));
  EXPECT_TRUE(initial.live_light_entities.contains(light));

  const RenderSceneSnapshot unchanged =
      build_render_scene_snapshot(document, assets,
                                  {.previous = &initial, .frame_index = 2u});
  ASSERT_EQ(unchanged.meshes.size(), 2u);
  EXPECT_EQ(unchanged.frame_index, 2u);
  EXPECT_FALSE(unchanged.dirty.any_dirty());
  EXPECT_TRUE(unchanged.reused_previous_records);
  EXPECT_FALSE(unchanged.meshes.front().mesh_dirty);
  EXPECT_FALSE(unchanged.meshes.front().material_dirty);
  EXPECT_FALSE(unchanged.meshes.front().transform_dirty);
  EXPECT_FALSE(unchanged.meshes.front().binding_dirty);
  EXPECT_FALSE(unchanged.lights.front().light_dirty);
  EXPECT_FALSE(unchanged.lights.front().transform_dirty);

  document.set_transform(renderable,
                         TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  const RenderSceneSnapshot moved =
      build_render_scene_snapshot(document, assets,
                                  {.previous = &unchanged, .frame_index = 3u});
  const auto moved_record =
      std::ranges::find_if(moved.meshes, [renderable](const auto &record) {
        return record.entity_id == renderable;
      });
  ASSERT_NE(moved_record, moved.meshes.end());
  EXPECT_FALSE(moved.reused_previous_records);
  EXPECT_TRUE(moved.dirty.scene_changed);
  EXPECT_TRUE(moved.dirty.transforms_changed);
  EXPECT_TRUE(moved_record->transform_dirty);
  EXPECT_FALSE(moved_record->mesh_dirty);
  EXPECT_FALSE(moved_record->material_dirty);
  EXPECT_FALSE(moved_record->binding_dirty);

  MeshAsset replacement = make_overlay_triangle_mesh();
  replacement.edit_geometry().vertices.front().rgba = 0xff00ffffu;
  ASSERT_TRUE(assets.replace_mesh(mesh, std::move(replacement)));
  const RenderSceneSnapshot replaced =
      build_render_scene_snapshot(document, assets,
                                  {.previous = &moved, .frame_index = 4u});
  const auto replaced_record =
      std::ranges::find_if(replaced.meshes, [renderable](const auto &record) {
        return record.entity_id == renderable;
      });
  ASSERT_NE(replaced_record, replaced.meshes.end());
  EXPECT_FALSE(replaced.reused_previous_records);
  EXPECT_TRUE(replaced.dirty.assets_changed);
  EXPECT_TRUE(replaced_record->mesh_dirty);
  EXPECT_FALSE(replaced_record->transform_dirty);
}

TEST(SceneDocumentRevisionTests,
     RenderComponentRevisionTracksStructuralAndComponentMutations) {
  SceneDocument document;
  const std::uint64_t initial_revision =
      document.render_component_revision();

  const EntityId entity =
      document.create_entity("Revision Probe", EntityKind::Mesh);
  EXPECT_GT(document.render_component_revision(), initial_revision);

  const std::uint64_t after_create = document.render_component_revision();
  document.set_transform(
      entity, TransformComponent{.position = {1.0f, 2.0f, 3.0f}});
  EXPECT_EQ(document.render_component_revision(), after_create);
  ASSERT_TRUE(document.set_visible(entity, false));
  EXPECT_EQ(document.render_component_revision(), after_create);

  const EntityId parent =
      document.create_entity("Revision Parent", EntityKind::Empty);
  const std::uint64_t after_parent_create =
      document.render_component_revision();
  ASSERT_TRUE(document.set_parent(entity, parent, false));
  EXPECT_EQ(document.render_component_revision(), after_parent_create);

  document.set_mesh(entity, MeshComponent{.mesh = 17u});
  const std::uint64_t after_mesh = document.render_component_revision();
  EXPECT_GT(after_mesh, after_parent_create);

  document.set_material(entity, MaterialComponent{.material = 23u});
  const std::uint64_t after_material =
      document.render_component_revision();
  EXPECT_GT(after_material, after_mesh);
  ASSERT_TRUE(document.clear_material(entity));
  const std::uint64_t after_clear_material =
      document.render_component_revision();
  EXPECT_GT(after_clear_material, after_material);

  document.set_light(entity, LightComponent{.type = LightType::Point});
  const std::uint64_t after_light = document.render_component_revision();
  EXPECT_GT(after_light, after_clear_material);

  const EntityId explicit_entity = document.create_entity_with_id(
      9001u, "Explicit Revision Probe", EntityKind::Empty);
  ASSERT_NE(explicit_entity, kInvalidEntityId);
  const std::uint64_t after_explicit_create =
      document.render_component_revision();
  EXPECT_GT(after_explicit_create, after_light);
  ASSERT_TRUE(document.destroy_entity(explicit_entity));
  const std::uint64_t after_single_destroy =
      document.render_component_revision();
  EXPECT_GT(after_single_destroy, after_explicit_create);

  const std::array<EntityId, 2u> batch{{
      document.create_entity("Batch Revision A", EntityKind::Empty),
      document.create_entity("Batch Revision B", EntityKind::Empty),
  }};
  const std::uint64_t before_batch_destroy =
      document.render_component_revision();
  ASSERT_TRUE(document.destroy_entities(batch));
  EXPECT_GT(document.render_component_revision(), before_batch_destroy);

  const std::uint64_t before_replacement =
      document.render_component_revision();
  document.advance_revision_for_replacement(document.revision());
  EXPECT_GT(document.render_component_revision(), before_replacement);
}

TEST(RenderSceneSnapshotTests,
     EqualRevisionScenesCannotReuseRecordsAcrossBindings) {
  SceneDocument first_document;
  AssetRegistry first_assets;
  SceneDocument second_document;
  AssetRegistry second_assets;

  MeshAsset first_geometry = make_overlay_triangle_mesh();
  MeshAsset second_geometry = make_overlay_triangle_mesh();
  second_geometry.edit_geometry().vertices.front().position.x += 100.0f;
  const AssetId first_mesh = first_assets.add_mesh(std::move(first_geometry));
  const AssetId second_mesh =
      second_assets.add_mesh(std::move(second_geometry));
  ASSERT_EQ(first_mesh, second_mesh);

  const EntityId first_entity =
      first_document.create_entity("Same Id, First Scene", EntityKind::Mesh);
  const EntityId second_entity = second_document.create_entity(
      "Same Id, Second Scene", EntityKind::Mesh);
  ASSERT_EQ(first_entity, second_entity);
  first_document.set_mesh(first_entity, MeshComponent{.mesh = first_mesh});
  second_document.set_mesh(second_entity,
                           MeshComponent{.mesh = second_mesh});
  ASSERT_EQ(first_document.revision(), second_document.revision());
  ASSERT_EQ(first_assets.revision(), second_assets.revision());

  const RenderSceneBindingKey first_binding{
      .scene_session_id = 0x100000000ull,
      .activation_generation = 23u,
      .binding_revision = 31u,
  };
  const RenderSceneBindingKey second_binding{
      .scene_session_id = 0x100000001ull,
      .activation_generation = 24u,
      .binding_revision = 32u,
  };
  const RenderSceneSnapshot first = build_render_scene_snapshot(
      first_document, first_assets, {.scene_binding = first_binding});
  ASSERT_EQ(first.meshes.size(), 1u);

  RenderSceneSnapshot second = build_render_scene_snapshot(
      second_document, second_assets,
      {.previous = &first, .scene_binding = second_binding});
  ASSERT_EQ(second.meshes.size(), 1u);
  EXPECT_EQ(second.scene_binding, second_binding);
  EXPECT_FALSE(second.reused_previous_records);
  EXPECT_TRUE(second.dirty.any_dirty());
  EXPECT_NE(second.meshes.front().mesh.vertices.data(),
            first.meshes.front().mesh.vertices.data());
  EXPECT_FLOAT_EQ(second.meshes.front().mesh.vertices.front().position.x,
                  first.meshes.front().mesh.vertices.front().position.x +
                      100.0f);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      second, second_document, second_assets, second_binding));
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      second, second_document, second_assets, first_binding));

  second_document.set_transform(
      second_entity,
      TransformComponent{.position = {3.0f, 4.0f, 5.0f}});
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      second, second_document, second_assets, 2u, first_binding));
  EXPECT_TRUE(try_refresh_render_scene_snapshot_transforms(
      second, second_document, second_assets, 2u, second_binding));
}

TEST(RenderSceneSnapshotTests,
     TextureMappingOnlyRefreshRetainsGeometryAndMaterialSnapshots) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity = document.create_entity("Mapped", EntityKind::Mesh);
  document.set_mesh(entity, MeshComponent{.mesh = mesh});
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(document, assets);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const auto geometry = snapshot.meshes.front().mesh.geometry_payload;
  const std::uint64_t transform_revision = document.transform_revision();
  const std::uint64_t render_revision = document.render_component_revision();
  QMat4 mapping;
  mapping.values[0] = 2.0f;
  mapping.values[12] = 3.0f;
  ASSERT_TRUE(document.set_texture_mapping(entity, {mapping}));

  EXPECT_EQ(document.transform_revision(), transform_revision);
  EXPECT_EQ(document.render_component_revision(), render_revision);
  EXPECT_FALSE(render_scene_snapshot_matches_sources(snapshot, document, assets));
  EXPECT_FALSE(try_refresh_render_scene_snapshot_selection(snapshot, document, assets));
  ASSERT_TRUE(try_refresh_render_scene_snapshot_transforms(snapshot, document, assets));
  EXPECT_EQ(snapshot.meshes.front().mesh.geometry_payload, geometry);
  EXPECT_EQ(snapshot.meshes.front().sampling_from_local.values, mapping.values);
  EXPECT_TRUE(snapshot.meshes.front().texture_mapping_dirty);
  EXPECT_FALSE(snapshot.meshes.front().mesh_dirty);
  EXPECT_FALSE(snapshot.meshes.front().material_dirty);
  EXPECT_FALSE(snapshot.meshes.front().transform_dirty);
  EXPECT_TRUE(snapshot.dirty.texture_mappings_changed);
  EXPECT_FALSE(snapshot.dirty.transforms_changed);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(snapshot, document, assets));
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(snapshot, document, assets));

  const RenderSceneSnapshot idle = build_render_scene_snapshot(
      document, assets, {.previous = &snapshot});
  EXPECT_TRUE(idle.reused_previous_records);
  EXPECT_FALSE(idle.dirty.any_dirty());
  EXPECT_FALSE(idle.meshes.front().texture_mapping_dirty);
}

TEST(RenderSceneSnapshotTests,
     OrdinaryObjectsWithDifferentTextureMappingsKeepOneSharedBatch) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId first = document.create_entity("First", EntityKind::Mesh);
  const EntityId second = document.create_entity("Second", EntityKind::Mesh);
  document.set_mesh(first, MeshComponent{.mesh = mesh});
  document.set_mesh(second, MeshComponent{.mesh = mesh});
  QMat4 mapping;
  mapping.values[5] = 3.0f;
  ASSERT_TRUE(document.set_texture_mapping(first, {mapping}));
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(snapshot.meshes.empty());
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  const auto geometry = snapshot.instance_primitives.front().mesh.geometry_payload;
  const auto &before = snapshot.instance_primitives.front().occurrences;
  ASSERT_EQ(before.size(), 2u);
  const auto mapped = std::ranges::find(before, first,
                                      &RenderSceneInstanceOccurrenceRecord::entity_id);
  ASSERT_NE(mapped, before.end());
  EXPECT_EQ(mapped->sampling_from_local.values, mapping.values);
  mapping.values[13] = 8.0f;
  ASSERT_TRUE(document.set_texture_mapping(first, {mapping}));
  ASSERT_TRUE(try_refresh_render_scene_snapshot_transforms(snapshot, document, assets));
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  EXPECT_EQ(snapshot.instance_primitives.front().mesh.geometry_payload, geometry);
  for (const auto &occurrence : snapshot.instance_primitives.front().occurrences) {
    EXPECT_EQ(occurrence.sampling_from_local.values,
              occurrence.entity_id == first ? mapping.values : QMat4{}.values);
  }
}

TEST(RenderSceneSnapshotTests,
     FormalInstanceTextureMappingComposesPerOccurrenceInMemberCoordinates) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition = document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member = document.create_entity("Member", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  ASSERT_TRUE(document.set_transform(member, {.position = {2.0f, 0.0f, 0.0f}}));
  QMat4 member_mapping;
  member_mapping.values[12] = 1.0f;
  ASSERT_TRUE(document.set_texture_mapping(member, {member_mapping}));
  const EntityId first = document.create_entity("First", EntityKind::Instance);
  const EntityId second = document.create_entity("Second", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(first, {.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(second, {.definition = definition}));
  QMat4 occurrence_mapping;
  occurrence_mapping.values[0] = 2.0f;
  ASSERT_TRUE(document.set_texture_mapping(first, {occurrence_mapping}));
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(document, assets);
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  const auto geometry = snapshot.instance_primitives.front().mesh.geometry_payload;
  for (const auto &occurrence : snapshot.instance_primitives.front().occurrences) {
    EXPECT_FLOAT_EQ(occurrence.sampling_from_local.values[0],
                    occurrence.entity_id == first ? 2.0f : 1.0f);
    EXPECT_FLOAT_EQ(occurrence.sampling_from_local.values[12],
                    occurrence.entity_id == first ? 3.0f : 1.0f);
  }
  occurrence_mapping.values[0] = 3.0f;
  ASSERT_TRUE(document.set_texture_mapping(first, {occurrence_mapping}));
  ASSERT_TRUE(try_refresh_render_scene_snapshot_transforms(snapshot, document, assets));
  EXPECT_EQ(snapshot.instance_primitives.front().mesh.geometry_payload, geometry);
  for (const auto &occurrence : snapshot.instance_primitives.front().occurrences) {
    EXPECT_FLOAT_EQ(occurrence.sampling_from_local.values[0],
                    occurrence.entity_id == first ? 3.0f : 1.0f);
    EXPECT_FLOAT_EQ(occurrence.sampling_from_local.values[12],
                    occurrence.entity_id == first ? 5.0f : 1.0f);
  }
}

TEST(RenderSceneSnapshotTests,
     RefreshesOrdinaryInstanceBatchSelectionWithoutReextractingSceneRecords) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId first =
      document.create_entity("First Duplicate", EntityKind::Mesh);
  const EntityId second =
      document.create_entity("Second Duplicate", EntityKind::Mesh);
  document.set_mesh(first, MeshComponent{.mesh = mesh});
  document.set_mesh(second, MeshComponent{.mesh = mesh});

  RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets, {.frame_index = 1u});
  ASSERT_TRUE(snapshot.meshes.empty());
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  ASSERT_EQ(snapshot.instance_primitives.front().occurrences.size(), 2u);
  const MeshGeometryPayload *const retained_geometry =
      snapshot.instance_primitives.front().mesh.geometry_payload.get();

  ASSERT_TRUE(document.set_selected(first, true));
  ASSERT_TRUE(try_refresh_render_scene_snapshot_selection(
      snapshot, document, assets, 2u));

  EXPECT_EQ(snapshot.frame_index, 2u);
  EXPECT_EQ(snapshot.scene_revision, document.revision());
  EXPECT_EQ(snapshot.selection_revision, document.selection_revision());
  EXPECT_TRUE(snapshot.dirty.scene_changed);
  EXPECT_TRUE(snapshot.dirty.selection_changed);
  EXPECT_FALSE(snapshot.dirty.visibility_changed);
  EXPECT_FALSE(snapshot.dirty.local_visibility_changed);
  EXPECT_FALSE(snapshot.dirty.transforms_changed);
  EXPECT_FALSE(snapshot.dirty.mesh_bindings_changed);
  EXPECT_FALSE(snapshot.dirty.render_components_changed);
  EXPECT_FALSE(snapshot.dirty.assets_changed);
  EXPECT_FALSE(snapshot.dirty.project_materials_changed);
  EXPECT_EQ(snapshot.instance_primitives.front().mesh.geometry_payload.get(),
            retained_geometry);
  const auto selected_first = std::ranges::find_if(
      snapshot.instance_primitives.front().occurrences,
      [first](const auto &occurrence) { return occurrence.entity_id == first; });
  const auto unselected_second = std::ranges::find_if(
      snapshot.instance_primitives.front().occurrences,
      [second](const auto &occurrence) {
        return occurrence.entity_id == second;
      });
  ASSERT_NE(selected_first,
            snapshot.instance_primitives.front().occurrences.end());
  ASSERT_NE(unselected_second,
            snapshot.instance_primitives.front().occurrences.end());
  EXPECT_TRUE(selected_first->selected);
  EXPECT_FALSE(unselected_second->selected);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(snapshot, document,
                                                    assets));
  EXPECT_FALSE(try_refresh_render_scene_snapshot_selection(
      snapshot, document, assets, 3u));

  TransformComponent moved = *document.transform(first);
  moved.position.x = 3.0f;
  document.set_transform(first, moved);
  const std::uint64_t retained_scene_revision = snapshot.scene_revision;
  const std::uint64_t retained_frame_index = snapshot.frame_index;
  EXPECT_FALSE(try_refresh_render_scene_snapshot_selection(
      snapshot, document, assets, 3u));
  EXPECT_EQ(snapshot.scene_revision, retained_scene_revision);
  EXPECT_EQ(snapshot.frame_index, retained_frame_index);
}

TEST(RenderSceneSnapshotTests,
     RefreshesFormalInstanceOccurrenceSelectionInPlace) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Definition Member", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId first =
      document.create_entity("First Occurrence", EntityKind::Instance);
  const EntityId second =
      document.create_entity("Second Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      first, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(
      second, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(first, true));

  RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets, {.frame_index = 1u});
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  ASSERT_EQ(snapshot.instance_primitives.front().occurrences.size(), 2u);
  document.clear_selection();
  ASSERT_TRUE(document.set_selected(second, true));

  ASSERT_TRUE(try_refresh_render_scene_snapshot_selection(
      snapshot, document, assets, 2u));
  const auto refreshed_first = std::ranges::find_if(
      snapshot.instance_primitives.front().occurrences,
      [first](const auto &occurrence) { return occurrence.entity_id == first; });
  const auto refreshed_second = std::ranges::find_if(
      snapshot.instance_primitives.front().occurrences,
      [second](const auto &occurrence) {
        return occurrence.entity_id == second;
      });
  ASSERT_NE(refreshed_first,
            snapshot.instance_primitives.front().occurrences.end());
  ASSERT_NE(refreshed_second,
            snapshot.instance_primitives.front().occurrences.end());
  EXPECT_FALSE(refreshed_first->selected);
  EXPECT_TRUE(refreshed_second->selected);

  const RenderSceneSnapshot rebuilt =
      build_render_scene_snapshot(document, assets, {.frame_index = 2u});
  ASSERT_EQ(rebuilt.instance_primitives.size(), 1u);
  ASSERT_EQ(rebuilt.instance_primitives.front().occurrences.size(), 2u);
  const auto rebuilt_first = std::ranges::find_if(
      rebuilt.instance_primitives.front().occurrences,
      [first](const auto &occurrence) { return occurrence.entity_id == first; });
  const auto rebuilt_second = std::ranges::find_if(
      rebuilt.instance_primitives.front().occurrences,
      [second](const auto &occurrence) {
        return occurrence.entity_id == second;
      });
  ASSERT_NE(rebuilt_first,
            rebuilt.instance_primitives.front().occurrences.end());
  ASSERT_NE(rebuilt_second,
            rebuilt.instance_primitives.front().occurrences.end());
  EXPECT_EQ(refreshed_first->selected, rebuilt_first->selected);
  EXPECT_EQ(refreshed_second->selected, rebuilt_second->selected);
}

TEST(RenderSceneSnapshotTests,
     RefreshesOnlyPureTransformSnapshotsAndRejectsOtherSourceChanges) {
  SceneDocument document;
  AssetRegistry assets;

  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId alternate_mesh = assets.add_mesh(make_authored_quad_mesh());
  const AssetId material =
      assets.add_material(make_default_quader_material("Render Material"));
  const EntityId first = document.create_entity("First", EntityKind::Mesh);
  document.set_mesh(first, MeshComponent{.mesh = mesh});
  document.set_material(first, MaterialComponent{.material = material});
  const EntityId second = document.create_entity("Second", EntityKind::Mesh);
  // Keep this transform-refresh fixture on the standalone-record path. Equal
  // ordinary payloads are now intentionally folded into explicit instance
  // batches and have dedicated coverage below.
  document.set_mesh(second, MeshComponent{.mesh = alternate_mesh});
  document.set_material(second, MaterialComponent{.material = material});
  const EntityId light = document.create_entity("Light", EntityKind::Light);
  document.set_light(light, LightComponent{.type = LightType::Point});

  RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets, {.frame_index = 1u});
  ASSERT_EQ(snapshot.meshes.size(), 2u);
  ASSERT_EQ(snapshot.lights.size(), 1u);
  EXPECT_EQ(snapshot.render_component_revision,
            document.render_component_revision());
  const MeshVertex *const retained_vertices =
      snapshot.meshes.front().mesh.vertices.data();
  const std::uint64_t second_transform_revision =
      snapshot.meshes[1].transform_revision;

  document.set_transform(
      first, TransformComponent{.position = {3.0f, 4.0f, 5.0f}});
  ASSERT_TRUE(try_refresh_render_scene_snapshot_transforms(
      snapshot, document, assets, 2u));
  EXPECT_EQ(snapshot.frame_index, 2u);
  EXPECT_EQ(snapshot.scene_revision, document.revision());
  EXPECT_EQ(snapshot.transform_revision, document.transform_revision());
  EXPECT_TRUE(snapshot.dirty.scene_changed);
  EXPECT_TRUE(snapshot.dirty.transforms_changed);
  EXPECT_FALSE(snapshot.dirty.visibility_changed);
  EXPECT_FALSE(snapshot.dirty.mesh_bindings_changed);
  EXPECT_FALSE(snapshot.dirty.render_components_changed);
  EXPECT_FALSE(snapshot.dirty.assets_changed);
  EXPECT_TRUE(snapshot.meshes[0].transform_dirty);
  EXPECT_FALSE(snapshot.meshes[0].mesh_dirty);
  EXPECT_FALSE(snapshot.meshes[0].material_dirty);
  EXPECT_FALSE(snapshot.meshes[0].binding_dirty);
  EXPECT_FALSE(snapshot.meshes[1].transform_dirty);
  EXPECT_EQ(snapshot.meshes[1].transform_revision,
            second_transform_revision);
  EXPECT_EQ(snapshot.meshes.front().mesh.vertices.data(), retained_vertices);
  EXPECT_FALSE(snapshot.lights.front().light_dirty);
  EXPECT_FALSE(snapshot.lights.front().transform_dirty);

  std::uint64_t next_frame_index = 3u;
  const auto expect_rejected_and_rebuild = [&](std::string_view cause) {
    SCOPED_TRACE(std::string(cause));
    TransformComponent moved = *document.transform(first);
    moved.position.x += 1.0f;
    document.set_transform(first, moved);
    const std::uint64_t retained_scene_revision = snapshot.scene_revision;
    const std::uint64_t retained_frame_index = snapshot.frame_index;
    EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
        snapshot, document, assets, next_frame_index++));
    EXPECT_EQ(snapshot.scene_revision, retained_scene_revision);
    EXPECT_EQ(snapshot.frame_index, retained_frame_index);
    snapshot = build_render_scene_snapshot(
        document, assets, {.frame_index = next_frame_index++});
  };

  MaterialComponent changed_material{.material = material};
  changed_material.material_slots.emplace(3u, material);
  document.set_material(first, changed_material);
  EXPECT_NE(snapshot.render_component_revision,
            document.render_component_revision());
  expect_rejected_and_rebuild("material edit");

  ASSERT_TRUE(document.clear_material(first));
  expect_rejected_and_rebuild("material removal");
  document.set_material(first, MaterialComponent{.material = material});
  expect_rejected_and_rebuild("material addition");

  LightComponent changed_light = *document.light(light);
  changed_light.intensity += 2.0f;
  document.set_light(light, changed_light);
  expect_rejected_and_rebuild("light edit");

  document.set_mesh(second, MeshComponent{.mesh = mesh});
  expect_rejected_and_rebuild("mesh binding edit");

  const EntityId created =
      document.create_entity("Created After Snapshot", EntityKind::Empty);
  expect_rejected_and_rebuild("entity creation");
  ASSERT_TRUE(document.destroy_entity(created));
  expect_rejected_and_rebuild("entity destruction");

  ASSERT_TRUE(document.set_visible(second, false));
  expect_rejected_and_rebuild("visibility edit");
  ASSERT_TRUE(document.set_visible(second, true));
  snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = next_frame_index++});

  ASSERT_TRUE(document.set_parent(second, first, false));
  expect_rejected_and_rebuild("hierarchy edit");

  MeshAsset replacement = make_authored_quad_mesh();
  replacement.edit_geometry().vertices.front().rgba = 0xff00ffffu;
  ASSERT_TRUE(assets.replace_mesh(alternate_mesh, std::move(replacement)));
  expect_rejected_and_rebuild("asset edit");

  document.advance_revision_for_replacement(document.revision());
  expect_rejected_and_rebuild("document replacement");
}

TEST(LightRenderableCacheTests, SeparatesRebuildAndTransformSignatures) {
    LightComponent light;
    QMat4 transform;
    const std::uint64_t rebuild_signature =
        LightRenderableCache::light_signature(light);
    const std::uint64_t transform_signature =
        LightRenderableCache::light_transform_signature(transform);

    QMat4 moved_transform = transform;
    moved_transform.values[12] = 5.0f;
    EXPECT_EQ(LightRenderableCache::light_signature(light), rebuild_signature);
    EXPECT_NE(LightRenderableCache::light_transform_signature(moved_transform),
              transform_signature);

    LightComponent recolored_light = light;
    recolored_light.color.x = 0.25f;
    EXPECT_NE(LightRenderableCache::light_signature(recolored_light),
              rebuild_signature);

    LightComponent ranged_light = light;
    ranged_light.range = 25.0f;
    EXPECT_NE(LightRenderableCache::light_signature(ranged_light),
              rebuild_signature);
}

TEST(LightRenderableCacheTests, ConvertsEditorIntensityToViewportFilamentUnits) {
    // Reference strength 16 resolves to Godot energy 1. Filament's Lambert
    // response and fixed 1/38400 exposure require pi*38400 lux or candela.
    // Point and Filament SPOT convert candela to flux by 4*pi and pi.
    constexpr double kPi = 3.14159265358979323846;
    for (float intensity : {1.0f, 16.0f, 32.0f}) {
        LightComponent light;
        light.intensity = intensity;
        const double physical = kPi * 38400.0 * intensity / 16.0;
        light.type = LightType::Directional;
        EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light),
                        static_cast<float>(physical));
        light.type = LightType::Point;
        EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light),
                        static_cast<float>(physical * 4.0 * kPi));
        light.type = LightType::Spot;
        EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light),
                        static_cast<float>(physical * kPi));
    }
}

TEST(ViewportSceneOverlayBuilderTests, AddsHierarchySelectionWiresAndGroupBounds) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
    const EntityId group = document.create_empty("Selected Group");
    const EntityId child =
        document.create_entity("Child Mesh", EntityKind::Mesh);
    document.set_mesh(child, MeshComponent{.mesh = mesh});
    ASSERT_TRUE(document.set_parent(child, group));
    ASSERT_TRUE(document.set_selected(group, true));

    ViewportOverlayFrame base_frame;
    base_frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {},
        .end = {1.0f, 0.0f, 0.0f},
        .source = ViewportOverlaySource::Selection,
    });

    const RenderOverlaySelectionSnapshot snapshot =
        build_render_overlay_selection_snapshot(document, assets);
    ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            std::move(base_frame), snapshot);
    ViewportSettings settings = ViewportSettings::defaults();
    ASSERT_TRUE(settings.set_float_value(
        ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
    ASSERT_TRUE(settings.set_float_value(
        ViewportFloatSettingId::SelectionFaceWireLineSize, 7.0f));
    ViewportOverlayStyleResolver::apply(frame, settings);

    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 4u);
    EXPECT_EQ(
        line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
        3u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.source != ViewportOverlaySource::HierarchySelection ||
                 (line.style_role ==
                      ViewportOverlayStyleRole::SelectionWire &&
                   line.width_pixels == 2.0f);
        }));
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::GroupBounds), 12u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer != ViewportOverlayLayer::GroupBounds ||
                 (line.color == kOverlayGroupBoundsColor &&
                  line.style_role == ViewportOverlayStyleRole::GroupBounds &&
                  line.source == ViewportOverlaySource::GroupBounds);
        }));
}

TEST(ViewportSceneOverlayBuilderTests,
     GroupHierarchyOutlinePreservesAuthoredPolygonEdges) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_authored_split_quad_mesh());
  const EntityId group = document.create_empty("Selected Group");
  const EntityId child =
      document.create_entity("Child Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(child, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(child, group));
  ASSERT_TRUE(document.set_selected(group, true));

  const RenderOverlaySelectionSnapshot snapshot =
      build_render_overlay_selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.hierarchy_selection_wires.size(), 1u);
  ASSERT_EQ(snapshot.hierarchy_selection_wires.front().wire_segments.size(),
            5u);

  ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionFaceWireLineSize, 7.0f));
  ViewportOverlayStyleResolver::apply(frame, settings);

  EXPECT_EQ(
      line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
      5u);
  EXPECT_TRUE(std::all_of(
      frame.lines.begin(), frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.source != ViewportOverlaySource::HierarchySelection ||
               (line.layer == ViewportOverlayLayer::SelectedEdge &&
                line.style_role ==
                    ViewportOverlayStyleRole::SelectionWire &&
                line.width_pixels == 2.0f);
      }));
}

TEST(ViewportSceneOverlayBuilderTests,
     GroupHierarchyOutlineExpandsFormalInstanceMemberEdges) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Definition Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));

  const EntityId group = document.create_empty("Selected Group");
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_parent(occurrence, group));
  ASSERT_TRUE(document.set_selected(group, true));

  const RenderOverlaySelectionSnapshot snapshot =
      build_render_overlay_selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.hierarchy_selection_wires.size(), 1u);
  EXPECT_EQ(snapshot.hierarchy_selection_wires.front().entity, occurrence);
  EXPECT_EQ(snapshot.hierarchy_selection_wires.front().member, member);

  ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
  ViewportOverlayStyleResolver::apply(frame, settings);

  EXPECT_EQ(
      line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
      3u);
  EXPECT_TRUE(std::all_of(
      frame.lines.begin(), frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.source != ViewportOverlaySource::HierarchySelection ||
               (line.layer == ViewportOverlayLayer::SelectedEdge &&
                line.style_role ==
                    ViewportOverlayStyleRole::SelectionWire &&
                line.width_pixels == 2.0f);
      }));
}

TEST(ViewportSceneOverlayBuilderTests,
     CoSelectedGroupsUseTheSameHierarchyAndCageStyle) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId first_group = document.create_empty("First Group");
  const EntityId first_child =
      document.create_entity("First Child", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(first_child, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(first_child, first_group));
  const EntityId second_group = document.create_empty("Second Group");
  const EntityId second_child =
      document.create_entity("Second Child", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(second_child, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(second_child, second_group));
  ASSERT_TRUE(document.set_selected(first_group, true));
  ASSERT_TRUE(document.set_selected(second_group, true));
  ASSERT_EQ(document.primary_selection(), second_group);

  const RenderOverlaySelectionSnapshot snapshot =
      build_render_overlay_selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.hierarchy_selection_wires.size(), 2u);
  ASSERT_EQ(snapshot.group_bounds.size(), 2u);

  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                 {40.0f / 255.0f, 80.0f / 255.0f,
                                  120.0f / 255.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::BoundingBoxWire,
                                 {20.0f / 255.0f, 60.0f / 255.0f,
                                  100.0f / 255.0f, 1.0f}));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.25f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::BoundingBoxWireThickness, 4.5f));

  ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);
  ViewportOverlayStyleResolver::apply(frame, settings);
  const std::uint32_t selection_color = rgba8(40, 80, 120);
  const std::uint32_t bounds_color = rgba8(20, 60, 100);
  EXPECT_EQ(std::ranges::count_if(
                frame.lines, [&](const ViewportOverlayLine &line) {
                  return line.source ==
                             ViewportOverlaySource::HierarchySelection &&
                         line.style_role ==
                             ViewportOverlayStyleRole::SelectionWire &&
                         line.color == selection_color &&
                         line.width_pixels == 2.25f;
                }),
            6u);
  EXPECT_EQ(std::ranges::count_if(
                frame.lines, [&](const ViewportOverlayLine &line) {
                  return line.source == ViewportOverlaySource::GroupBounds &&
                         line.style_role ==
                             ViewportOverlayStyleRole::GroupBounds &&
                         line.color == bounds_color &&
                         line.width_pixels == 4.5f;
                }),
            24u);
}

TEST(ViewportSceneOverlayBuilderTests,
     SelectedGroupAndCoSelectedMembersUseNormalStyleAndOneExactCage) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Definition Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));

  const EntityId group = document.create_empty("Primary Group");
  const EntityId child =
      document.create_entity("Selected Child", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(child, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(child, group));
  const EntityId occurrence =
      document.create_entity("Selected Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {4.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(occurrence, group));

  ASSERT_TRUE(document.set_selected(child, true));
  ASSERT_TRUE(document.set_selected(occurrence, true));
  ASSERT_TRUE(document.set_selected(group, true));
  ASSERT_EQ(document.primary_selection(), group);

  const RenderOverlaySelectionSnapshot selection =
      build_render_overlay_selection_snapshot(document, assets);
  EXPECT_TRUE(selection.hierarchy_selection_wires.empty())
      << "co-selected descendants own their normal selection wires";
  ASSERT_EQ(selection.group_bounds.size(), 1u);
  ASSERT_EQ(selection.selected_mesh_bounds.size(), 2u);

  const RenderOverlaySelectedObjectSnapshot retained =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(retained.meshes.size(), 1u);
  EXPECT_TRUE(retained.meshes.front().selected);
  ASSERT_EQ(retained.instance_selection_fills.size(), 1u);
  ASSERT_EQ(retained.instance_selection_wires.size(), 1u);
  EXPECT_EQ(retained.instance_selection_wires.front().wire_color,
            retained.style.selection_wire_color);

  const RenderOverlaySceneWireframeSnapshot wireframe =
      build_render_overlay_scene_wireframe_snapshot(
          document, assets, static_cast<std::uint8_t>(0xffu));
  EXPECT_EQ(std::ranges::count_if(
                wireframe.meshes,
                [](const RenderOverlayMeshSnapshot &record) {
                  return record.selected;
                }),
            2u);
  const ViewportOverlayFrame normal_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, wireframe);
  EXPECT_EQ(std::ranges::count_if(
                normal_frame.lines,
                [](const ViewportOverlayLine &line) {
                  return line.style_role ==
                         ViewportOverlayStyleRole::SelectionWire;
                }),
            6u);

  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(
      ViewportColorSettingId::SelectionWire,
      {220.0f / 255.0f, 30.0f / 255.0f, 150.0f / 255.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(
      ViewportColorSettingId::BoundingBoxWire,
      {180.0f / 255.0f, 40.0f / 255.0f, 140.0f / 255.0f, 1.0f}));
  ViewportOverlayFrame selected_wire_frame = normal_frame;
  ViewportOverlayStyleResolver::apply(selected_wire_frame, settings);
  EXPECT_EQ(std::ranges::count_if(
                selected_wire_frame.lines,
                [](const ViewportOverlayLine &line) {
                  return line.style_role ==
                             ViewportOverlayStyleRole::SelectionWire &&
                         line.color == rgba8(220, 30, 150);
                }),
            6u);

  ViewportOverlayFrame styled_frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, selection);
  ViewportOverlayStyleResolver::apply(styled_frame, settings);

  EXPECT_EQ(line_source_count_for(
                styled_frame, ViewportOverlaySource::HierarchySelection),
            0u);
  EXPECT_EQ(line_count_for(styled_frame, ViewportOverlayLayer::GroupBounds),
            12u)
      << "coincident group and selected-descendant cages must merge";
  EXPECT_TRUE(std::ranges::all_of(
      styled_frame.lines, [](const ViewportOverlayLine &line) {
        return line.layer != ViewportOverlayLayer::GroupBounds ||
               (line.style_role == ViewportOverlayStyleRole::GroupBounds &&
                line.color == rgba8(180, 40, 140));
      }));
}

TEST(ViewportSceneOverlayBuilderTests,
     AddsSelectedMeshWorldAabbBoundsInObjectMode) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
    const EntityId selected_mesh =
        document.create_entity("Rotated Mesh", EntityKind::Mesh);
    document.set_mesh(selected_mesh, MeshComponent{.mesh = mesh});
    document.set_transform(
        selected_mesh,
        TransformComponent{
            .position = {3.0f, 0.5f, -2.0f},
            .rotation_euler = {0.0f, 0.0f, 0.5f},
        });
    ASSERT_TRUE(document.set_selected(selected_mesh, true));

    const std::optional<SceneBounds> expected_bounds =
        entity_world_bounds(document, assets, selected_mesh);
    ASSERT_TRUE(expected_bounds.has_value());

    const RenderOverlaySelectionSnapshot snapshot =
        build_render_overlay_selection_snapshot(document, assets);
    EXPECT_TRUE(snapshot.group_bounds.empty());
    ASSERT_EQ(snapshot.selected_mesh_bounds.size(), 1u);
    expect_bounds_snapshot_near(snapshot.selected_mesh_bounds.front(),
                                *expected_bounds);

    const ViewportOverlayFrame object_frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, false,
            RenderOverlaySelectionMode::Object);
    EXPECT_EQ(line_count_for(object_frame, ViewportOverlayLayer::GroupBounds),
              12u);
    const std::optional<SceneBounds> cage_bounds =
        line_bounds_for(object_frame, ViewportOverlayLayer::GroupBounds);
    ASSERT_TRUE(cage_bounds.has_value());
    expect_scene_bounds_near(*cage_bounds, *expected_bounds);

    const ViewportOverlayFrame face_frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, false,
            RenderOverlaySelectionMode::Component);
    EXPECT_EQ(line_count_for(face_frame, ViewportOverlayLayer::GroupBounds),
              0u);

    const ViewportOverlayFrame hidden_overlay_frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, false,
            RenderOverlaySelectionMode::Object, true, false);
    EXPECT_EQ(
        line_count_for(hidden_overlay_frame, ViewportOverlayLayer::GroupBounds),
        0u);
}

TEST(ViewportSceneOverlayBuilderTests,
     SelectedCageUsesExactTransformedGeometryExtents) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId selected_mesh =
      document.create_entity("Rotated Asymmetric Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(selected_mesh, MeshComponent{.mesh = mesh}));
  document.set_transform(
      selected_mesh,
      TransformComponent{
          .position = {3.0f, 0.5f, -2.0f},
          .rotation_euler = {0.0f, 0.78539816339f, 0.0f},
      });
  ASSERT_TRUE(document.set_selected(selected_mesh, true));

  const MeshAsset *asset = assets.mesh(mesh);
  ASSERT_NE(asset, nullptr);
  const QMat4 world_transform =
      document.world_transform_matrix(selected_mesh);
  const SceneBounds expected =
      exact_transformed_geometry_bounds(*asset, world_transform);
  const std::optional<SceneBounds> conservative_local_box =
      world_bounds_from_local_mesh_bounds(world_transform,
                                          local_mesh_bounds(*asset));
  ASSERT_TRUE(conservative_local_box.has_value());
  EXPECT_GT(conservative_local_box->max.x, expected.max.x + 0.1f)
      << "rotating imaginary local-AABB corners overstates the mesh extent";

  RenderOverlaySnapshotCache cache;
  const RenderOverlaySelectionSnapshot &snapshot =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(snapshot.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(snapshot.selected_mesh_bounds.front(), expected);

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, snapshot, false, RenderOverlaySelectionMode::Object);
  const std::optional<SceneBounds> cage =
      line_bounds_for(frame, ViewportOverlayLayer::GroupBounds);
  ASSERT_TRUE(cage.has_value());
  expect_scene_bounds_near(*cage, expected);
}

TEST(ViewportSceneOverlayBuilderTests,
     SelectedMultiMeshInstanceAndOrdinaryMeshShareOneExtentCage) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Bounds Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId first_member =
      document.create_entity("First Instance Member", EntityKind::Mesh);
  const EntityId second_member =
      document.create_entity("Second Instance Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(first_member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_mesh(second_member, MeshComponent{.mesh = mesh}));
  document.set_transform(
      first_member, TransformComponent{.position = {-2.0f, 0.0f, 0.0f}});
  document.set_transform(
      second_member, TransformComponent{.position = {4.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(first_member, definition, false));
  ASSERT_TRUE(document.set_parent(second_member, definition, false));

  const EntityId occurrence =
      document.create_entity("Selected Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 1.0f, -3.0f}});
  ASSERT_TRUE(document.set_selected(occurrence, true));

  RenderOverlaySnapshotCache cache;
  const RenderOverlaySelectionSnapshot &instance_snapshot =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(instance_snapshot.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(
      instance_snapshot.selected_mesh_bounds.front(),
      SceneBounds{.min = {7.5f, 1.0f, -3.0f},
                  .max = {14.5f, 1.0f, -2.0f}});
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 0u);
  const ViewportOverlayFrame instance_frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, instance_snapshot, false, RenderOverlaySelectionMode::Object);
  EXPECT_EQ(line_count_for(instance_frame, ViewportOverlayLayer::GroupBounds),
            12u);

  document.set_transform(
      occurrence, TransformComponent{.position = {20.0f, 2.0f, 5.0f}});
  const RenderOverlaySelectionSnapshot &moved_snapshot =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(moved_snapshot.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(
      moved_snapshot.selected_mesh_bounds.front(),
      SceneBounds{.min = {17.5f, 2.0f, 5.0f},
                  .max = {24.5f, 2.0f, 6.0f}});
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);

  document.set_transform(
      second_member, TransformComponent{.position = {8.0f, 0.0f, 0.0f}});
  const RenderOverlaySelectionSnapshot &member_moved_snapshot =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(member_moved_snapshot.selected_mesh_bounds.size(), 1u);
  expect_bounds_snapshot_near(
      member_moved_snapshot.selected_mesh_bounds.front(),
      SceneBounds{.min = {17.5f, 2.0f, 5.0f},
                  .max = {28.5f, 2.0f, 6.0f}});
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 2u);
  EXPECT_EQ(cache.stats().selection_world_bounds_translation_reuse_count, 1u);

  const EntityId ordinary =
      document.create_entity("Selected Ordinary Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(ordinary, MeshComponent{.mesh = mesh}));
  document.set_transform(
      ordinary, TransformComponent{.position = {-10.0f, -4.0f, 2.0f}});
  ASSERT_TRUE(document.set_selected(ordinary, true));
  const RenderOverlaySelectionSnapshot &mixed_snapshot =
      cache.selection_snapshot(document, assets);
  ASSERT_EQ(mixed_snapshot.selected_mesh_bounds.size(), 2u);
  const ViewportOverlayFrame mixed_frame =
      ViewportSceneOverlayBuilder::build_selection_frame(
          {}, mixed_snapshot, false, RenderOverlaySelectionMode::Object);
  EXPECT_EQ(line_count_for(mixed_frame, ViewportOverlayLayer::GroupBounds),
            12u);
  const std::optional<SceneBounds> mixed_cage =
      line_bounds_for(mixed_frame, ViewportOverlayLayer::GroupBounds);
  ASSERT_TRUE(mixed_cage.has_value());
  expect_scene_bounds_near(
      *mixed_cage,
      SceneBounds{.min = {-10.5f, -4.0f, 2.0f},
                  .max = {28.5f, 2.0f, 6.0f}});
}

TEST(ViewportSceneOverlayBuilderTests,
     MultipleSelectedMeshesRemainFillEligibleWithIndividualOutlinesAndCage) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
    const EntityId first =
        document.create_entity("First Selected Mesh", EntityKind::Mesh);
    const EntityId second =
        document.create_entity("Second Selected Mesh", EntityKind::Mesh);
    document.set_mesh(first, MeshComponent{.mesh = mesh});
    document.set_mesh(second, MeshComponent{.mesh = mesh});
    document.set_transform(
        first, TransformComponent{.position = {-3.0f, 1.0f, 2.0f}});
    document.set_transform(
        second, TransformComponent{.position = {4.0f, -2.0f, -1.0f}});
    ASSERT_TRUE(document.set_selected(first, true));
    ASSERT_TRUE(document.set_selected(second, true));

    const std::optional<SceneBounds> expected =
        selected_world_bounds(document, assets);
    ASSERT_TRUE(expected.has_value());
    const RenderOverlaySelectionSnapshot snapshot =
        build_render_overlay_selection_snapshot(document, assets);
    ASSERT_EQ(snapshot.selected_mesh_bounds.size(), 2u)
        << "the transform cache retains one bounds record per selected mesh";
    const RenderOverlaySelectedObjectSnapshot retained_snapshot =
        build_render_overlay_selected_object_snapshot(document, assets, true);
    ASSERT_EQ(retained_snapshot.meshes.size(), 2u);
    EXPECT_TRUE(selected_object_face_fills_enabled_for_selection_count(
        retained_snapshot.meshes.size()))
        << "the retained face-fill cache must remain active for every "
           "selected mesh";

    ViewportOverlayFrame base_frame;
    base_frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {-3.0f, 1.0f, 2.0f},
        .end = {-2.0f, 1.0f, 2.0f},
        .source = ViewportOverlaySource::Selection,
        .element = {.object_id = 1},
    });
    base_frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {4.0f, -2.0f, -1.0f},
        .end = {5.0f, -2.0f, -1.0f},
        .source = ViewportOverlaySource::Selection,
        .element = {.object_id = 2},
    });
    base_frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SelectedVisibleFace,
        .a = {-3.0f, 1.0f, 2.0f},
        .b = {-2.0f, 1.0f, 2.0f},
        .c = {-3.0f, 1.0f, 3.0f},
        .source = ViewportOverlaySource::Selection,
        .element = {.object_id = 1},
    });
    base_frame.lines.push_back({
        .layer = ViewportOverlayLayer::HoverEdge,
        .start = {4.0f, -2.0f, -1.0f},
        .end = {5.0f, -2.0f, -1.0f},
        .source = ViewportOverlaySource::Hover,
        .element = {.object_id = 2},
    });

    const ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            std::move(base_frame), snapshot, false,
            RenderOverlaySelectionMode::Object);

    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 2u)
        << "the aggregate cage must not replace each selected mesh outline";
    EXPECT_EQ(
        triangle_count_for(frame, ViewportOverlayLayer::SelectedVisibleFace),
        0u)
        << "transient duplicates stay stripped because the retained cache "
           "owns object face fills";
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::HoverEdge), 1u)
        << "stripping duplicate transient fills must preserve hover feedback";
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::GroupBounds), 12u);
    const std::optional<SceneBounds> cage =
        line_bounds_for(frame, ViewportOverlayLayer::GroupBounds);
    ASSERT_TRUE(cage.has_value());
    expect_scene_bounds_near(*cage, *expected);
}

TEST(ViewportSceneOverlayBuilderTests,
     AddsSelectedDirectionalLightMarkerAndRetiresPointLightCross) {
    SceneDocument document;
    AssetRegistry assets;
    const EntityId light =
        document.create_entity("Point Light", EntityKind::Light);
    document.set_transform(light,
                           TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
    document.set_light(light, LightComponent{.type = LightType::Point});
    ASSERT_TRUE(document.set_selected(light, true));
    const EntityId unselected_light =
        document.create_entity("Unselected Point Light", EntityKind::Light);
    document.set_light(unselected_light,
                       LightComponent{.type = LightType::Point});
    const EntityId hidden_light =
        document.create_entity("Hidden Point Light", EntityKind::Light);
    document.set_light(hidden_light, LightComponent{.type = LightType::Point});
    document.set_visible(hidden_light, false);
    ASSERT_TRUE(document.set_selected(hidden_light, true));
    const EntityId selected_directional =
        document.create_entity("Selected Directional Light", EntityKind::Light);
    document.set_light(selected_directional,
                       LightComponent{.type = LightType::Directional});
    ASSERT_TRUE(document.set_selected(selected_directional, true));

    const RenderOverlaySelectionSnapshot snapshot =
        build_render_overlay_selection_snapshot(document, assets);
    const ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot);

    EXPECT_EQ(point_count_for(frame, ViewportOverlayLayer::SelectedVertex), 1u);
    ASSERT_EQ(frame.points.size(), 1u);
    const auto point_marker =
        std::find_if(frame.points.begin(), frame.points.end(),
                     [](const ViewportOverlayPoint &point) {
                       return point.position.x == 2.0f &&
                              point.position.y == 3.0f &&
                              point.position.z == 4.0f;
                     });
    EXPECT_EQ(point_marker, frame.points.end());
    EXPECT_EQ(frame.points.front().style_role,
              ViewportOverlayStyleRole::SelectedVertex);
    EXPECT_EQ(frame.points.front().color, kOverlaySelectedVertexColor);
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 2u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::Selection &&
                 line.style_role == ViewportOverlayStyleRole::SelectionWire &&
                 line.color == kOverlaySelectionWireColor;
        }));
}

TEST(ViewportSceneOverlayBuilderTests, AddsPointLightBillboardGizmosWhenRequested) {
    SceneDocument document;
    AssetRegistry assets;
    const EntityId selected_light =
        document.create_entity("Selected Point Light", EntityKind::Light);
    document.set_transform(
        selected_light, TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
    document.set_light(selected_light, LightComponent{.type = LightType::Point});
    ASSERT_TRUE(document.set_selected(selected_light, true));
    const EntityId unselected_light =
        document.create_entity("Unselected Point Light", EntityKind::Light);
    document.set_transform(
        unselected_light, TransformComponent{.position = {-1.0f, 0.5f, 6.0f}});
    document.set_light(unselected_light,
                       LightComponent{.type = LightType::Point});
    const EntityId hidden_light =
        document.create_entity("Hidden Point Light", EntityKind::Light);
    document.set_light(hidden_light, LightComponent{.type = LightType::Point});
    document.set_visible(hidden_light, false);
    const EntityId directional_light =
        document.create_entity("Directional Light", EntityKind::Light);
    document.set_light(directional_light,
                       LightComponent{.type = LightType::Directional});
    ASSERT_TRUE(document.set_selected(directional_light, true));
    ASSERT_TRUE(document.set_active_selection(selected_light));

    const RenderOverlaySelectionSnapshot snapshot =
        build_render_overlay_selection_snapshot(document, assets);
    EXPECT_EQ(snapshot.selected_lights.size(), 1u);
    EXPECT_EQ(snapshot.point_light_gizmos.size(), 2u);

    const ViewportOverlayFrame hidden_frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, false);
    EXPECT_TRUE(hidden_frame.icons.empty());

    const ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, true);
    ASSERT_EQ(icon_count_for(frame, ViewportOverlayLayer::PointLightGizmo), 2u);
    EXPECT_TRUE(std::all_of(
        frame.icons.begin(), frame.icons.end(), [](const ViewportOverlayIcon &icon) {
          return icon.kind == ViewportOverlayIconKind::PointLight &&
                 icon.source == ViewportOverlaySource::Gizmo &&
                 icon.size_pixels == kOverlayPointLightGizmoSizePixels &&
                 icon.color == rgba8(255, 255, 255);
        }));
    const auto selected_icon =
        std::find_if(frame.icons.begin(), frame.icons.end(),
                     [&](const ViewportOverlayIcon &icon) {
                       return icon.element.object_id ==
                              static_cast<int>(selected_light);
                      });
    ASSERT_NE(selected_icon, frame.icons.end());
    const auto unselected_icon =
        std::find_if(frame.icons.begin(), frame.icons.end(),
                     [&](const ViewportOverlayIcon &icon) {
                       return icon.element.object_id ==
                              static_cast<int>(unselected_light);
                      });
    ASSERT_NE(unselected_icon, frame.icons.end());
    EXPECT_EQ(selected_icon->color, unselected_icon->color);
}

TEST(ViewportSceneOverlayBuilderTests, BuildsWireframeOverlayWithoutObjectFaceFills) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
    const EntityId entity =
        document.create_entity("Wire Mesh", EntityKind::Mesh);
    document.set_mesh(entity, MeshComponent{.mesh = mesh});

    ViewportOverlayFrame base_frame;
    base_frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SelectedVisibleFace,
        .a = {},
        .b = {1.0f, 0.0f, 0.0f},
        .c = {0.0f, 1.0f, 0.0f},
    });
    base_frame.triangles.push_back({
        .layer = ViewportOverlayLayer::HoverFace,
        .a = {},
        .b = {1.0f, 0.0f, 0.0f},
        .c = {0.0f, 0.0f, 1.0f},
    });
    base_frame.triangles.push_back({
        .layer = ViewportOverlayLayer::ToolPreviewFill,
        .a = {},
        .b = {0.0f, 1.0f, 0.0f},
        .c = {0.0f, 0.0f, 1.0f},
        .source = ViewportOverlaySource::Tool,
    });

    const RenderOverlaySceneWireframeSnapshot snapshot =
        build_render_overlay_scene_wireframe_snapshot(
            document, assets, static_cast<std::uint8_t>(0xffu));
    const ViewportOverlayFrame object_frame =
        ViewportSceneOverlayBuilder::build_wireframe_frame(
            base_frame, RenderOverlaySelectionMode::Object, snapshot);

    EXPECT_EQ(triangle_count_for(object_frame,
                                 ViewportOverlayLayer::SelectedVisibleFace),
              0u);
    EXPECT_EQ(triangle_count_for(object_frame, ViewportOverlayLayer::HoverFace),
              0u);
    EXPECT_EQ(
        triangle_count_for(object_frame, ViewportOverlayLayer::ToolPreviewFill),
        1u);
    EXPECT_EQ(line_count_for(object_frame, ViewportOverlayLayer::WireframeBase),
              0u);
    EXPECT_EQ(line_count_for(object_frame, ViewportOverlayLayer::WireframeXray),
              3u);
    EXPECT_TRUE(std::all_of(
        object_frame.lines.begin(), object_frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer != ViewportOverlayLayer::WireframeXray ||
                 (line.color == kOverlayWireframeXrayColor &&
                  line.style_role == ViewportOverlayStyleRole::WireframeBase &&
                  line.source == ViewportOverlaySource::SceneWireframe);
        }));

    const ViewportOverlayFrame face_frame =
        ViewportSceneOverlayBuilder::build_wireframe_frame(
            base_frame, RenderOverlaySelectionMode::Component, snapshot);

    EXPECT_EQ(triangle_count_for(face_frame,
                                 ViewportOverlayLayer::SelectedVisibleFace),
              1u);
    EXPECT_EQ(triangle_count_for(face_frame, ViewportOverlayLayer::HoverFace),
              1u);
    EXPECT_EQ(
        triangle_count_for(face_frame, ViewportOverlayLayer::ToolPreviewFill),
        1u);
    EXPECT_EQ(line_count_for(face_frame, ViewportOverlayLayer::WireframeBase),
              0u);
    EXPECT_EQ(line_count_for(face_frame, ViewportOverlayLayer::WireframeXray),
              3u);
}

TEST(ViewportSceneOverlayBuilderTests, WireframeModeUsesAuthoredTopologyWithoutTriangleDiagonal) {
    AssetRegistry assets;
    SceneDocument document;
    const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
    const EntityId entity =
        document.create_entity("Authored Quad", EntityKind::Mesh);
    document.set_mesh(entity, MeshComponent{.mesh = mesh});

    const RenderOverlaySceneWireframeSnapshot snapshot =
        build_render_overlay_scene_wireframe_snapshot(
            document, assets, static_cast<std::uint8_t>(0xffu));
    const ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_wireframe_frame(
            {}, RenderOverlaySelectionMode::Object, snapshot);

    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::WireframeBase), 0u);
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::WireframeXray), 4u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer != ViewportOverlayLayer::WireframeXray ||
                 (line.style_role == ViewportOverlayStyleRole::WireframeBase &&
                  line.source == ViewportOverlaySource::SceneWireframe);
        }));
}

TEST(OverlayLineMeshBuilderTests,
     KeepsWorldEndpointsForShaderClippingAndPixelExpansion) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::OpenEdge,
        .start = {-0.5f, 0.0f, -0.05f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 128, 64, 255),
        .width_pixels = 2.0f,
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::OpenEdge,
        .start = {-0.25f, 0.25f, -1.0f},
        .end = {0.25f, 0.25f, -1.0f},
        .color = rgba8(255, 128, 64, 255),
        .width_pixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::OpenEdge);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 8u);
    EXPECT_EQ(batch.mesh.indices.size(), 12u);
    EXPECT_FLOAT_EQ(batch.width_pixels, 2.0f);
    EXPECT_FLOAT_EQ(batch.depth_bias, kOverlayLineDepthBias);
    EXPECT_FLOAT_EQ(batch.near_plane, test_overlay_camera().near_plane);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_FALSE(batch.state.depth_write);
    for (const OverlayLineVertex &vertex : batch.mesh.vertices) {
        EXPECT_FLOAT_EQ(vertex.position.x, 0.0f);
        EXPECT_FLOAT_EQ(vertex.position.y, 0.0f);
        EXPECT_FLOAT_EQ(vertex.position.z, 0.0f);
        EXPECT_FLOAT_EQ(vertex.position.w, 1.0f);
        EXPECT_EQ(vertex.color, rgba8_srgb_to_linear(rgba8(255, 128, 64, 255)));
    }
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].start.x, -0.5f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].start.z, -0.05f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].end.x, 0.5f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].end.z, -1.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].control.x, -1.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].control.y, -1.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[0].control.z, 0.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[2].control.x, 1.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[2].control.y, 1.0f);
    EXPECT_FLOAT_EQ(batch.mesh.vertices[2].control.z, 1.0f);
}

TEST(OverlayLineMeshBuilderTests, EmitsCanonicalShaderRibbonControls) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, -0.5f, -1.0f},
        .end = {0.5f, 0.5f, -1.0f},
        .color = rgba8(255, 128, 64, 255),
        .width_pixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_FLOAT_EQ(batch.depth_bias, 0.0f);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depth_culling);
    const OverlayLineMesh& mesh = batch.mesh;
    ASSERT_EQ(mesh.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(mesh.vertices[0].start.x, -0.5f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].start.y, -0.5f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].end.x, 0.5f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].end.y, 0.5f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].control.x, -1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[1].control.y, 1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].control.x, 1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[3].control.y, -1.0f);
}

TEST(OverlayLineMeshBuilderTests, BatchesSameWidthLinesWithDifferentColors) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.0f, 0.0f, -1.0f},
        .color = rgba8(255, 211, 31, 255),
        .width_pixels = 2.0f,
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {0.0f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(90, 170, 255, 255),
        .width_pixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SelectedEdge);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineMesh &mesh = plan.line_batches.front().mesh;
    ASSERT_EQ(mesh.vertices.size(), 8u);
    EXPECT_EQ(mesh.vertices[0].color,
              rgba8_srgb_to_linear(rgba8(255, 211, 31, 255)));
    EXPECT_EQ(mesh.vertices[4].color,
              rgba8_srgb_to_linear(rgba8(90, 170, 255, 255)));
}

TEST(OverlayLineMeshBuilderTests,
     SplitsComponentSelectionLinesIntoEditWireAndDrawOnTopBatches) {
  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedEdge,
      .start = {-0.5f, 0.0f, -1.0f},
      .end = {0.0f, 0.0f, -1.0f},
      .color = rgba8(255, 224, 92, 235),
      .width_pixels = 2.0f,
      .source = ViewportOverlaySource::ComponentSelection,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedEdge,
      .start = {0.0f, 0.0f, -1.0f},
      .end = {0.5f, 0.0f, -1.0f},
      .color = rgba8(255, 224, 92, 235),
      .width_pixels = 2.0f,
      .source = ViewportOverlaySource::Selection,
  });

  const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
      frame, test_overlay_camera(), ViewportOverlayLayer::SelectedEdge);

  ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
  EXPECT_FLOAT_EQ(plan.edit_wire_batches.front().width_pixels, 2.0f);
  EXPECT_EQ(plan.edit_wire_batches.front().state.depth_func,
            OverlayDepthFunc::GreaterOrEqual);
  EXPECT_TRUE(plan.edit_wire_batches.front().state.depth_culling);

  ASSERT_EQ(plan.line_batches.size(), 1u);
  const OverlayLineBatch &draw_on_top = plan.line_batches.front();
  EXPECT_EQ(draw_on_top.state.depth_func, OverlayDepthFunc::Always);
  EXPECT_FLOAT_EQ(draw_on_top.depth_bias, 0.0f);
  EXPECT_FALSE(draw_on_top.state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDepthStampsRemainInactiveForVisibility) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -2.0f},
        .end = {0.5f, 0.0f, -2.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    append_source_wire_depth_stamp_box(frame, 1, false);

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 8u);
    EXPECT_EQ(batch.mesh.indices.size(), 12u);
    EXPECT_FLOAT_EQ(batch.depth_bias, 0.0f);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depth_culling);
    EXPECT_FALSE(batch.state.depth_write);
    for (const OverlayLineVertex& vertex : batch.mesh.vertices) {
      EXPECT_EQ(vertex.color, rgba8_srgb_to_linear(rgba8(255, 224, 92, 235)));
    }
}

TEST(OverlayLineMeshBuilderTests, SourceWireDepthStampsKeepInsideOutTopologyVisible) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -2.0f},
        .end = {0.5f, 0.0f, -2.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    append_source_wire_depth_stamp_box(frame, 1, true);

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_EQ(batch.mesh.indices.size(), 6u);
    EXPECT_FLOAT_EQ(batch.depth_bias, 0.0f);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDrawsInvertedInternalEdges) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = {-0.5f, 0.0f, -1.0f},
        .b = {0.5f, 0.0f, -1.0f},
        .c = {0.0f, 0.5f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .element = {.object_id = 1},
    });
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = {0.5f, 0.0f, -1.0f},
        .b = {-0.5f, 0.0f, -1.0f},
        .c = {0.0f, -0.5f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .element = {.object_id = 1},
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(batch.depth_bias, 0.0f);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDrawsBackFacingBoundaryEdges) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, -0.5f, -1.0f},
        .end = {0.5f, -0.5f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = {-0.5f, -0.5f, -1.0f},
        .b = {0.5f, -0.5f, -1.0f},
        .c = {0.0f, -1.0f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .element = {.object_id = 1},
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const OverlayLineBatch &batch = plan.line_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(batch.depth_bias, 0.0f);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests,
     SourceWireUsesOneOnTopBatchBesideDepthStamps) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 1},
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.5f, -1.0f},
        .end = {0.5f, 0.5f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .width_pixels = 2.0f,
        .element = {.object_id = 2},
    });
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = {-0.5f, 0.0f, -1.0f},
        .b = {0.5f, 0.0f, -1.0f},
        .c = {0.0f, 0.25f, -1.0f},
        .color = rgba8(255, 224, 92, 235),
        .element = {.object_id = 1},
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    EXPECT_EQ(plan.line_batches[0].mesh.vertices.size(), 8u);
    EXPECT_FLOAT_EQ(plan.line_batches[0].depth_bias, 0.0f);
    EXPECT_EQ(plan.line_batches[0].state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(plan.line_batches[0].state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, ReusesWorldGeometryAcrossCameraMotion) {
  const ViewportOverlayCamera camera = test_overlay_camera();
  const QVec3 line_start{-0.25f, 0.0f, -1.0f};
  const QVec3 line_end{0.25f, 0.0f, -1.0f};
  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedEdge,
      .start = line_start,
      .end = line_end,
      .color = rgba8(255, 211, 31, 255),
      .width_pixels = 2.0f,
  });

  const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
      frame, camera, ViewportOverlayLayer::SelectedEdge);

  ASSERT_EQ(plan.line_batches.size(), 1u);
  const OverlayLineMesh &mesh = plan.line_batches.front().mesh;
  ASSERT_EQ(mesh.vertices.size(), 4u);

  ViewportOverlayCamera moved_camera = camera;
  moved_camera.eye = {7.0f, -3.0f, 4.0f};
  moved_camera.forward = {-0.5f, 0.25f, -1.0f};
  const OverlayLayerBuildPlan moved_plan =
      OverlayRenderableBuilder::build_layer(
          frame, moved_camera, ViewportOverlayLayer::SelectedEdge);
  ASSERT_EQ(moved_plan.line_batches.size(), 1u);
  const OverlayLineMesh &moved_mesh = moved_plan.line_batches.front().mesh;
  ASSERT_EQ(moved_mesh.vertices.size(), mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const OverlayLineVertex &before = mesh.vertices[index];
    const OverlayLineVertex &after = moved_mesh.vertices[index];
    EXPECT_FLOAT_EQ(before.position.x, after.position.x);
    EXPECT_FLOAT_EQ(before.position.y, after.position.y);
    EXPECT_FLOAT_EQ(before.position.z, after.position.z);
    EXPECT_FLOAT_EQ(before.position.w, after.position.w);
    EXPECT_FLOAT_EQ(before.start.x, after.start.x);
    EXPECT_FLOAT_EQ(before.start.y, after.start.y);
    EXPECT_FLOAT_EQ(before.start.z, after.start.z);
    EXPECT_FLOAT_EQ(before.end.x, after.end.x);
    EXPECT_FLOAT_EQ(before.end.y, after.end.y);
    EXPECT_FLOAT_EQ(before.end.z, after.end.z);
    EXPECT_FLOAT_EQ(before.control.x, after.control.x);
    EXPECT_FLOAT_EQ(before.control.y, after.control.y);
    EXPECT_FLOAT_EQ(before.control.z, after.control.z);
    EXPECT_EQ(before.color, after.color);
  }
}

TEST(OverlayLineMeshBuilderTests,
     PerspectiveDepthBiasMovesTowardCameraWithoutMovingScreenPosition) {
  const ViewportOverlayCamera camera = test_perspective_overlay_camera();
  const QVec3 line_start{-0.5f, 0.2f, -1.0f};
  const QVec3 line_end{0.5f, 0.2f, -1.0f};
  const QVec3 point_position{0.35f, -0.25f, -1.0f};

  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::OpenEdge,
      .start = line_start,
      .end = line_end,
      .color = rgba8(255, 128, 64, 255),
      .width_pixels = 2.0f,
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::UnselectedVertex,
      .position = point_position,
      .color = rgba8(255, 211, 31, 255),
      .size_pixels = 6.0f,
  });

  const OverlayLayerBuildPlan line_plan = OverlayRenderableBuilder::build_layer(
      frame, camera, ViewportOverlayLayer::OpenEdge);
  const OverlayLayerBuildPlan point_plan =
      OverlayRenderableBuilder::build_layer(
          frame, camera, ViewportOverlayLayer::UnselectedVertex);

  ASSERT_EQ(line_plan.line_batches.size(), 1u);
  ASSERT_EQ(point_plan.point_batches.size(), 1u);
  const OverlayLineMesh &line_mesh = line_plan.line_batches.front().mesh;
  const OverlayPointMesh &point_mesh = point_plan.point_batches.front().mesh;
  ASSERT_EQ(line_mesh.vertices.size(), 4u);
  ASSERT_EQ(point_mesh.vertices.size(), 4u);

  EXPECT_FLOAT_EQ(line_plan.line_batches.front().near_plane,
                  camera.near_plane);
  EXPECT_FLOAT_EQ(line_plan.line_batches.front().depth_bias,
                  kOverlayLineDepthBias);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].start.x, line_start.x);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].start.y, line_start.y);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].start.z, line_start.z);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].end.x, line_end.x);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].end.y, line_end.y);
  EXPECT_FLOAT_EQ(line_mesh.vertices[0].end.z, line_end.z);
  EXPECT_FLOAT_EQ(point_plan.point_batches.front().near_plane,
                  camera.near_plane);
  EXPECT_FLOAT_EQ(point_plan.point_batches.front().depth_bias,
                  kOverlayPointDepthBias);
  for (const OverlayPointVertex &vertex : point_mesh.vertices) {
    EXPECT_FLOAT_EQ(vertex.position.x, 0.0f);
    EXPECT_FLOAT_EQ(vertex.position.y, 0.0f);
    EXPECT_FLOAT_EQ(vertex.position.z, 0.0f);
    EXPECT_FLOAT_EQ(vertex.position.w, 1.0f);
    EXPECT_FLOAT_EQ(vertex.center.x, point_position.x);
    EXPECT_FLOAT_EQ(vertex.center.y, point_position.y);
    EXPECT_FLOAT_EQ(vertex.center.z, point_position.z);
    EXPECT_FLOAT_EQ(vertex.center.w, 1.0f);
  }
}

TEST(OverlayCameraProjectorTests,
     TowardCameraDepthBiasClampsAtFilamentDeviceNearPlane) {
  ViewportOverlayCamera camera = test_perspective_overlay_camera();
  camera.near_plane = 0.05f;
  const OverlayCameraProjector projector(camera);
  OverlayProjectedPoint projected{
      .device = {0.25f, -0.5f, -0.99999f, 1.0f},
      .pixel = {100.0f, 200.0f},
      .valid = true,
  };

  projector.apply_device_depth_bias(projected, 1000.0f);

  EXPECT_FLOAT_EQ(projected.device.x, 0.25f);
  EXPECT_FLOAT_EQ(projected.device.y, -0.5f);
  EXPECT_FLOAT_EQ(projected.device.z, -1.0f);
  EXPECT_FLOAT_EQ(projected.device.w, 1.0f);
}

TEST(OverlayFaceFillBuilderTests, CreatesDepthStampedFaceFillPasses) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SelectedVisibleFace,
            .a = {-1.0f, 0.0f, -1.0f},
            .b = {1.0f, 0.0f, -1.0f},
            .c = {0.0f, 1.0f, -1.0f},
            .color = rgba8(255, 172, 31, 8),
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(),
        ViewportOverlayLayer::SelectedVisibleFace);

    ASSERT_EQ(plan.face_fill_batches.size(), 2u);
    EXPECT_EQ(plan.face_fill_batches[0].mesh.vertices.size(), 3u);
    EXPECT_EQ(plan.face_fill_batches[0].mesh.indices.size(), 3u);
    EXPECT_TRUE(plan.face_fill_batches[0].state.depth_write);
    EXPECT_FALSE(plan.face_fill_batches[0].state.color_write);
    EXPECT_EQ(plan.face_fill_batches[0].state.depth_func,
              OverlayDepthFunc::GreaterOrEqual);
    EXPECT_FALSE(plan.face_fill_batches[1].state.depth_write);
    EXPECT_TRUE(plan.face_fill_batches[1].state.color_write);
    EXPECT_EQ(plan.face_fill_batches[1].state.depth_func,
              OverlayDepthFunc::Equal);
    EXPECT_EQ(plan.face_fill_batches[1].mesh.vertices[0].color,
              rgba8_srgb_to_linear(rgba8(255, 172, 31, 8)));
}

TEST(OverlayFaceFillBuilderTests, SelectedAndHoverFaceFillsRenderTwoSided) {
    const std::array<ViewportOverlayLayer, 4u> layers{{
            ViewportOverlayLayer::SelectedVisibleFace,
            ViewportOverlayLayer::SelectedFaceMask,
            ViewportOverlayLayer::SelectedXrayFace,
            ViewportOverlayLayer::HoverFace,
    }};

    for (const ViewportOverlayLayer layer : layers) {
        SCOPED_TRACE(static_cast<int>(layer));
        ViewportOverlayFrame frame;
        frame.triangles.push_back({
                .layer = layer,
                .a = {-1.0f, 0.0f, -1.0f},
                .b = {0.0f, 1.0f, -1.0f},
                .c = {1.0f, 0.0f, -1.0f},
                .color = rgba8(31, 255, 102, 22),
        });

        const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::build_layer(frame, test_overlay_camera(),
                                                  layer);

        ASSERT_EQ(plan.face_fill_batches.size(), 2u);
        EXPECT_TRUE(plan.face_fill_batches[0].state.depth_write);
        EXPECT_FALSE(plan.face_fill_batches[0].state.color_write);
        EXPECT_EQ(plan.face_fill_batches[0].state.depth_func,
                  OverlayDepthFunc::GreaterOrEqual);
        EXPECT_FALSE(plan.face_fill_batches[1].state.depth_write);
        EXPECT_TRUE(plan.face_fill_batches[1].state.color_write);
        EXPECT_EQ(plan.face_fill_batches[1].state.depth_func,
                  OverlayDepthFunc::Equal);
        for (const OverlayFaceFillBatch &batch : plan.face_fill_batches) {
          EXPECT_EQ(batch.state.culling_mode, OverlayCullingMode::None);
          EXPECT_EQ(batch.state.transparency_mode,
                    OverlayTransparencyMode::TwoPassesTwoSides);
        }
    }
}

TEST(OverlayFaceFillBuilderTests, SourceWireDepthStampDoesNotRenderDepthPass) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {-1.0f, 0.0f, -1.0f},
            .b = {1.0f, 0.0f, -1.0f},
            .c = {0.0f, 1.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(),
        ViewportOverlayLayer::SourceWireDepthStamp);

    EXPECT_TRUE(plan.face_fill_batches.empty());
    EXPECT_TRUE(plan.line_batches.empty());
    EXPECT_TRUE(plan.point_batches.empty());
}

TEST(OverlayPointMeshBuilderTests, BuildsOutlineAndFillPointPasses) {
    ViewportOverlayFrame frame;
    frame.points.push_back({
        .layer = ViewportOverlayLayer::SelectedVertex,
        .position = {0.0f, 0.0f, -1.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .outline_growth_pixels = 3.0f,
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::SelectedVertex);

    ASSERT_EQ(plan.point_batches.size(), 2u);
    EXPECT_FLOAT_EQ(plan.point_batches[0].size_pixels, 9.0f);
    EXPECT_EQ(plan.point_batches[0].mesh.vertices[0].color,
              rgba8_srgb_to_linear(kOverlayVertexOutlineColor));
    EXPECT_FLOAT_EQ(plan.point_batches[1].size_pixels, 6.0f);
    EXPECT_EQ(plan.point_batches[1].mesh.vertices[0].color,
              rgba8_srgb_to_linear(rgba8(255, 211, 31, 255)));
    for (const OverlayPointBatch &batch : plan.point_batches) {
      EXPECT_EQ(batch.mesh.vertices.size(), 4u);
      EXPECT_EQ(batch.mesh.indices.size(), 6u);
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
      EXPECT_FLOAT_EQ(batch.depth_bias, kOverlayPointDepthBias);
      EXPECT_TRUE(batch.state.depth_culling);
    }
}

TEST(OverlayPointMeshBuilderTests,
     KeepsComponentSelectionPointsInDepthTestedBatch) {
  ViewportOverlayFrame frame;
  frame.points.push_back({
      .layer = ViewportOverlayLayer::SelectedVertex,
      .position = {-0.25f, 0.0f, -1.0f},
      .color = rgba8(255, 211, 31, 255),
      .size_pixels = 6.0f,
      .source = ViewportOverlaySource::ComponentSelection,
      .outline_growth_pixels = 0.0f,
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::SelectedVertex,
      .position = {0.25f, 0.0f, -1.0f},
      .color = rgba8(255, 211, 31, 255),
      .size_pixels = 6.0f,
      .source = ViewportOverlaySource::Selection,
      .outline_growth_pixels = 0.0f,
  });

  const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
      frame, test_overlay_camera(), ViewportOverlayLayer::SelectedVertex);

  ASSERT_EQ(plan.point_batches.size(), 1u);
  const OverlayPointBatch &batch = plan.point_batches.front();
  EXPECT_EQ(batch.mesh.vertices.size(), 8u);
  EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
  EXPECT_FLOAT_EQ(batch.depth_bias, kOverlayPointDepthBias);
  EXPECT_TRUE(batch.state.depth_culling);
}

TEST(OverlayPointMeshBuilderTests,
     SourceWireDepthStampsCullOutwardOccludedVertexPoints) {
  const std::array<ViewportOverlayLayer, 3u> layers{{
      ViewportOverlayLayer::UnselectedVertex,
      ViewportOverlayLayer::SelectedVertex,
      ViewportOverlayLayer::HoverVertex,
  }};

  for (const ViewportOverlayLayer layer : layers) {
    SCOPED_TRACE(static_cast<int>(layer));
    ViewportOverlayFrame frame;
    frame.points.push_back({
        .layer = layer,
        .position = {0.25f, 0.25f, -1.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 1},
        .outline_growth_pixels = 0.0f,
    });
    frame.points.push_back({
        .layer = layer,
        .position = {0.25f, 0.25f, -3.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 2},
        .outline_growth_pixels = 0.0f,
    });
    append_source_wire_depth_stamp_box(frame, 1, false);

    const OverlayLayerBuildPlan plan =
        OverlayRenderableBuilder::build_layer(frame, test_overlay_camera(),
                                              layer);

    ASSERT_EQ(plan.point_batches.size(), 1u);
    EXPECT_EQ(point_mesh_vertex_count(plan), 4u);
    EXPECT_EQ(plan.point_batches.front().state.depth_func,
              OverlayDepthFunc::GreaterOrEqual);
    EXPECT_FLOAT_EQ(plan.point_batches.front().depth_bias,
                    kOverlayPointDepthBias);
    EXPECT_TRUE(plan.point_batches.front().state.depth_culling);
  }
}

TEST(OverlayPointMeshBuilderTests,
     KeepsOutwardNearestCornerVertexVisibleFromDiagonalCamera) {
  const std::array<ViewportOverlayLayer, 3u> layers{{
      ViewportOverlayLayer::UnselectedVertex,
      ViewportOverlayLayer::SelectedVertex,
      ViewportOverlayLayer::HoverVertex,
  }};
  const QVec3 target{0.0f, 0.0f, -2.0f};
  const QVec3 eye = target + QVec3{6.0f, 5.0f, 8.0f};
  const QVec3 forward = normalize(target - eye);
  const QVec3 right = normalize(cross(forward, QVec3{0.0f, 1.0f, 0.0f}));
  const ViewportOverlayCamera camera{
      .eye = eye,
      .forward = forward,
      .right = right,
      .up = normalize(cross(right, forward)),
      .viewport_size = {945.0f, 710.0f},
      .projection = ViewportOverlayProjection::Perspective,
      .fov_degrees = 60.0f,
      .near_plane = 0.05f,
      .far_plane = 1000.0f,
  };

  for (const ViewportOverlayLayer layer : layers) {
    SCOPED_TRACE(static_cast<int>(layer));
    ViewportOverlayFrame frame;
    frame.points.push_back({
        .layer = layer,
        .position = {1.0f, 1.0f, -1.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 1},
        .outline_growth_pixels = 0.0f,
    });
    append_source_wire_depth_stamp_box(frame, 1, false);

    const OverlayLayerBuildPlan plan =
        OverlayRenderableBuilder::build_layer(frame, camera, layer);

    ASSERT_EQ(plan.point_batches.size(), 1u);
    EXPECT_EQ(point_mesh_vertex_count(plan), 4u);
  }
}

TEST(OverlayPointMeshBuilderTests,
     SourceWireDepthStampsDoNotCpuCullInsideOutVertexPoints) {
  ViewportOverlayFrame frame;
  frame.points.push_back({
      .layer = ViewportOverlayLayer::UnselectedVertex,
      .position = {0.25f, 0.25f, -1.0f},
      .color = rgba8(255, 211, 31, 255),
      .size_pixels = 6.0f,
      .element = {.object_id = 1, .vertex_id = 1},
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::UnselectedVertex,
      .position = {0.25f, 0.25f, -3.0f},
      .color = rgba8(255, 211, 31, 255),
      .size_pixels = 6.0f,
      .element = {.object_id = 1, .vertex_id = 2},
  });
  append_source_wire_depth_stamp_box(frame, 1, true);

  const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
      frame, test_overlay_camera(), ViewportOverlayLayer::UnselectedVertex);

  ASSERT_EQ(plan.point_batches.size(), 1u);
  EXPECT_EQ(point_mesh_vertex_count(plan), 8u);
  EXPECT_EQ(plan.point_batches.front().state.depth_func,
            OverlayDepthFunc::GreaterOrEqual);
  EXPECT_FLOAT_EQ(plan.point_batches.front().depth_bias,
                  kOverlayPointDepthBias);
  EXPECT_TRUE(plan.point_batches.front().state.depth_culling);
}

TEST(OverlayPointMeshBuilderTests,
     InwardRoomVertexPointsKeepSceneDepthInsideAndOutsideTheRoom) {
  const std::array<ViewportOverlayLayer, 3u> layers{{
      ViewportOverlayLayer::UnselectedVertex,
      ViewportOverlayLayer::SelectedVertex,
      ViewportOverlayLayer::HoverVertex,
  }};

  for (const ViewportOverlayLayer layer : layers) {
    SCOPED_TRACE(static_cast<int>(layer));
    ViewportOverlayFrame frame;
    frame.points.push_back({
        .layer = layer,
        .position = {0.25f, 0.25f, -3.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 1},
        .outline_growth_pixels = 0.0f,
    });
    // Retain a visible back-wall corner alongside the interior wall marker.
    // CPU filtering must not throw away inward-room boundary handles: the
    // scene depth buffer resolves their fragments against any intervening wall.
    frame.points.push_back({
        .layer = layer,
        .position = {1.0f, 1.0f, -3.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 2},
        .outline_growth_pixels = 0.0f,
    });
    append_source_wire_depth_stamp_box(frame, 1, true);
    // This separate wall is in front of vertex 1, including while the camera
    // is within the inward room. Volume containment cannot grant X-ray drawing.
    append_source_wire_depth_stamp_quad(
        frame, 2, {-0.75f, -0.75f, -2.5f}, {0.75f, -0.75f, -2.5f},
        {0.75f, 0.75f, -2.5f}, {-0.75f, 0.75f, -2.5f}, false);

    for (const ViewportOverlayProjection projection : {
             ViewportOverlayProjection::Perspective,
             ViewportOverlayProjection::Orthographic}) {
      SCOPED_TRACE(static_cast<int>(projection));
      for (const float eye_z : {-2.0f, -0.5f, 15.0f}) {
        SCOPED_TRACE(eye_z);
        ViewportOverlayCamera camera = test_perspective_overlay_camera();
        camera.eye = {0.0f, 0.0f, eye_z};
        camera.forward = {0.0f, 0.0f, -1.0f};
        camera.projection = projection;

        const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::build_layer(frame, camera, layer);

        ASSERT_EQ(plan.point_batches.size(), 1u);
        EXPECT_EQ(point_mesh_vertex_count(plan), 8u);
        const OverlayPointBatch &batch = plan.point_batches.front();
        EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
        EXPECT_TRUE(batch.state.depth_culling);
        EXPECT_FLOAT_EQ(batch.depth_bias, kOverlayPointDepthBias);
        ASSERT_EQ(batch.mesh.position_stream_bindings.size(), 2u);
        EXPECT_EQ(batch.mesh.position_stream_bindings[0].vertex_id, 1);
        EXPECT_EQ(batch.mesh.position_stream_bindings[1].vertex_id, 2);
      }
    }
  }
}

TEST(OverlayPointMeshBuilderTests,
     InwardRoomVertexOutlinesRetainDepthWhileSnapTargetsStayOnTop) {
  ViewportOverlayCamera camera = test_perspective_overlay_camera();
  camera.eye = {0.0f, 0.0f, -2.0f};
  camera.forward = {0.0f, 0.0f, -1.0f};

  for (const ViewportOverlayLayer layer : {
           ViewportOverlayLayer::SelectedVertex,
           ViewportOverlayLayer::HoverVertex,
           ViewportOverlayLayer::SnapTargetVertex}) {
    SCOPED_TRACE(static_cast<int>(layer));
    ViewportOverlayFrame frame;
    append_source_wire_depth_stamp_box(frame, 1, true);
    frame.points.push_back({
        .layer = layer,
        .position = {0.25f, 0.25f, -3.0f},
        .color = rgba8(255, 211, 31, 255),
        .size_pixels = 6.0f,
        .element = {.object_id = 1, .vertex_id = 1},
    });

    const OverlayLayerBuildPlan plan =
        OverlayRenderableBuilder::build_layer(frame, camera, layer);
    const bool snap_target = layer == ViewportOverlayLayer::SnapTargetVertex;
    ASSERT_EQ(plan.point_batches.size(), snap_target ? 1u : 2u);
    for (const OverlayPointBatch &batch : plan.point_batches) {
      EXPECT_EQ(batch.state.depth_func,
                snap_target ? OverlayDepthFunc::Always
                            : OverlayDepthFunc::GreaterOrEqual);
      EXPECT_EQ(batch.state.depth_culling, !snap_target);
      EXPECT_FLOAT_EQ(batch.depth_bias,
                      snap_target ? 0.0f : kOverlayPointDepthBias);
      EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    }
  }
}

TEST(OverlayPointMeshBuilderTests, BuildsPointLightIconBillboardBatch) {
    ViewportOverlayFrame frame;
    frame.icons.push_back({
        .layer = ViewportOverlayLayer::PointLightGizmo,
        .kind = ViewportOverlayIconKind::PointLight,
        .position = {0.0f, 0.0f, -1.0f},
        .color = rgba8(255, 255, 255, 255),
        .size_pixels = kOverlayPointLightGizmoSizePixels,
    });

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::PointLightGizmo);

    ASSERT_EQ(plan.icon_batches.size(), 1u);
    EXPECT_EQ(plan.icon_batches[0].icon, ViewportOverlayIconKind::PointLight);
    EXPECT_FLOAT_EQ(plan.icon_batches[0].size_pixels,
                    kOverlayPointLightGizmoSizePixels);
    EXPECT_EQ(plan.icon_batches[0].mesh.vertices.size(), 4u);
    EXPECT_EQ(plan.icon_batches[0].mesh.indices.size(), 6u);
    EXPECT_EQ(plan.icon_batches[0].state.depth_func,
              OverlayDepthFunc::GreaterOrEqual);
    EXPECT_TRUE(plan.icon_batches[0].state.depth_culling);
    EXPECT_FLOAT_EQ(plan.icon_batches[0].depth_bias, 0.0f);
    EXPECT_TRUE(OverlayRenderableBuilder::layer_build_depends_on_camera(
        ViewportOverlayLayer::PointLightGizmo));
}

TEST(OverlayRenderableBuilderTests, SanitizesCameraBeforeSigning) {
  ViewportOverlayCamera camera = test_overlay_camera();
  camera.viewport_size = {0.0f, -3.0f};
  camera.forward = {};
  camera.right = {};
  camera.near_plane = -1.0f;
  camera.far_plane = -0.5f;

  const ViewportOverlayCamera sanitized =
      OverlayCameraProjector::sanitized_camera(camera);

  EXPECT_GE(sanitized.viewport_size.x, 1.0f);
  EXPECT_GE(sanitized.viewport_size.y, 1.0f);
  EXPECT_GT(sanitized.near_plane, 0.0f);
  EXPECT_GT(sanitized.far_plane, sanitized.near_plane);
  EXPECT_NE(OverlayRenderableBuilder::camera_signature(sanitized),
            OverlayRenderableBuilder::camera_signature(test_overlay_camera()));
}

TEST(OverlayRenderableBuilderTests,
     LineLayerSignatureIgnoresPoseButTracksClipState) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SelectedVisibleFace,
            .a = {-0.5f, -0.5f, -1.0f},
            .b = {0.5f, -0.5f, -1.0f},
            .c = {0.0f, 0.5f, -1.0f},
            .color = kOverlaySelectionFaceColor,
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = kOverlaySourceWireColor,
        .width_pixels = 1.5f,
    });

    ViewportOverlayCamera camera = test_overlay_camera();
    ViewportOverlayCamera moved_camera = camera;
    moved_camera.eye.x += 3.0f;
    moved_camera.forward = {0.25f, 0.0f, -1.0f};

    EXPECT_FALSE(OverlayRenderableBuilder::layer_build_depends_on_camera(
        ViewportOverlayLayer::SelectedVisibleFace));
    EXPECT_EQ(
        OverlayRenderableBuilder::layer_build_signature(
            frame, camera, ViewportOverlayLayer::SelectedVisibleFace),
        OverlayRenderableBuilder::layer_build_signature(
            frame, moved_camera, ViewportOverlayLayer::SelectedVisibleFace));

    EXPECT_TRUE(OverlayRenderableBuilder::layer_build_depends_on_camera(
        ViewportOverlayLayer::SourceWire));
    EXPECT_EQ(OverlayRenderableBuilder::layer_build_signature(
                  frame, camera, ViewportOverlayLayer::SourceWire),
              OverlayRenderableBuilder::layer_build_signature(
                  frame, moved_camera, ViewportOverlayLayer::SourceWire));
    moved_camera.near_plane *= 2.0f;
    EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(
                  frame, camera, ViewportOverlayLayer::SourceWire),
              OverlayRenderableBuilder::layer_build_signature(
                  frame, moved_camera, ViewportOverlayLayer::SourceWire));
}

TEST(OverlayProxyFactoryTests, CountsOnlyNonEmptyRenderableBatches) {
    OverlayLayerBuildPlan plan;
    plan.face_fill_batches.push_back({});
    plan.face_fill_batches.push_back({
        .mesh =
            {
                .vertices =
                    {
                        {{0.0f, 0.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                        {{1.0f, 0.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                        {{0.0f, 1.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                    },
                .indices = {0u, 1u, 2u},
            },
    });
    plan.line_batches.push_back({
        .mesh =
            {
                .vertices =
                    {
                        {{0.0f, 0.0f, 0.0f, 1.0f}, {}, {}, {},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 0.0f, 0.0f, 1.0f}, {}, {}, {},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 1.0f, 0.0f, 1.0f}, {}, {}, {},
                         rgba8(255, 255, 255, 255)},
                    },
                .indices = {0u, 1u, 2u},
            },
    });
    plan.point_batches.push_back({});
    plan.icon_batches.push_back({});
    plan.icon_batches.push_back({
        .mesh =
            {
                .vertices =
                    {
                        {{0.0f, 0.0f, 0.0f, 1.0f},
                         {0.0f, 0.0f},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 0.0f, 0.0f, 1.0f},
                         {1.0f, 0.0f},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 1.0f, 0.0f, 1.0f},
                         {1.0f, 1.0f},
                         rgba8(255, 255, 255, 255)},
                    },
                .indices = {0u, 1u, 2u},
            },
    });

    EXPECT_EQ(OverlayProxyFactory::planned_proxy_count(plan), 3u);
}

TEST(OverlayRenderableCacheTests, TracksSlotSignaturesWithoutOwningFilamentProxies) {
    OverlayRenderableCache cache;
    ViewportOverlayFrame frame;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SourceWire,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 128, 64, 255),
        .width_pixels = 2.0f,
    });

    const ViewportOverlayCamera camera = test_overlay_camera();
    EXPECT_TRUE(cache.set_overlay_frame(1u, 0u, frame, camera));

    const OverlayRenderableCacheStats stats = cache.stats(1u);
    EXPECT_EQ(stats.frame_signature,
              OverlayRenderableBuilder::frame_signature(frame));
    EXPECT_EQ(stats.camera_signature,
              OverlayRenderableBuilder::camera_signature(
                  OverlayCameraProjector::sanitized_camera(camera)));
    EXPECT_EQ(stats.layer_mask, kViewportOverlayLayerMask);
    EXPECT_EQ(stats.proxy_count, 0u);

    EXPECT_FALSE(cache.set_overlay_frame(1u, 0u, frame, camera));

    frame.lines.front().width_pixels = 4.0f;
    EXPECT_TRUE(cache.set_overlay_frame(1u, 0u, frame, camera));
    EXPECT_EQ(cache.stats(1u).frame_signature,
              OverlayRenderableBuilder::frame_signature(frame));
}

TEST(ViewportRenderPassCoordinatorTests, SeparatesSceneAndOverlayLayerMasks) {
    ViewportRenderPassCoordinator coordinator;
    const std::uint8_t grid_layer_mask = viewport_grid_layer_mask(2);
    coordinator.initialize(grid_layer_mask);

    EXPECT_EQ(coordinator.scene_visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask));
    EXPECT_EQ(
        coordinator.overlay_visible_layer_mask(),
        static_cast<std::uint8_t>(grid_layer_mask | kViewportOverlayLayerMask));
    EXPECT_EQ(coordinator.visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask |
                                        grid_layer_mask |
                                        kViewportOverlayLayerMask));

    EXPECT_TRUE(coordinator.set_grid_visible(false));
    EXPECT_EQ(coordinator.scene_visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask));
    EXPECT_EQ(coordinator.overlay_visible_layer_mask(), 0u);
    EXPECT_EQ(coordinator.visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask));

    EXPECT_TRUE(coordinator.set_overlay_visible(true));
    EXPECT_EQ(coordinator.scene_visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask));
    EXPECT_EQ(
        coordinator.overlay_visible_layer_mask(),
        static_cast<std::uint8_t>(grid_layer_mask | kViewportOverlayLayerMask));
    EXPECT_EQ(coordinator.visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportShadedSceneLayerMask |
                                        kViewportToolPreviewLayerMask |
                                        grid_layer_mask |
                                        kViewportOverlayLayerMask));

    EXPECT_TRUE(
        coordinator.set_scene_layer_mask(kViewportRenderedSceneLayerMask));
    EXPECT_EQ(coordinator.scene_visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportRenderedSceneLayerMask |
                                        kViewportToolPreviewLayerMask));
    EXPECT_EQ(
        coordinator.overlay_visible_layer_mask(),
        static_cast<std::uint8_t>(grid_layer_mask | kViewportOverlayLayerMask));
    EXPECT_EQ(coordinator.visible_layer_mask(),
              static_cast<std::uint8_t>(kViewportRenderedSceneLayerMask |
                                        kViewportToolPreviewLayerMask |
                                        grid_layer_mask |
                                        kViewportOverlayLayerMask));

    EXPECT_TRUE(coordinator.set_scene_visible(false));
    EXPECT_EQ(coordinator.scene_visible_layer_mask(), 0u);
    EXPECT_EQ(coordinator.overlay_visible_layer_mask(), grid_layer_mask);
    EXPECT_EQ(coordinator.visible_layer_mask(), grid_layer_mask);
}

TEST(RenderInstanceBatchPlannerTests,
     ChunksTwoHundredPrimitiveOccurrencesAtDeviceLimit) {
  RenderInstancePrimitiveKey key{
      .geometry_payload = 101u,
      .material_payload = 202u,
      .index_offset = 0u,
      .index_count = 36u,
      .layer_mask = kViewportRenderedSceneLayerMask,
      .cast_shadows = true,
      .receive_shadows = true,
  };
  const RenderInstanceBounds local_bounds{
      .min = {-1.0f, -1.0f, -1.0f},
      .max = {1.0f, 1.0f, 1.0f},
      .present = true,
  };
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(200u);
  for (std::uint64_t index = 0u; index < 200u; ++index) {
    QMat4 transform;
    transform.values[12] = static_cast<float>(index * 2u);
    QMat4 mapping;
    mapping.values[13] = static_cast<float>(index) + 0.5f;
    occurrences.push_back({
        .key = key,
        .occurrence = 1'000u + index,
        .world_transform = transform,
        .sampling_from_local = mapping,
        .local_bounds = local_bounds,
    });
  }

  const RenderInstanceBatchPlan plan =
      plan_render_instance_batches(occurrences, 64u);

  ASSERT_EQ(plan.chunks.size(), 4u);
  EXPECT_EQ(plan.occurrence_count, 200u);
  EXPECT_EQ(plan.chunks[0].world_transforms.size(), 64u);
  EXPECT_EQ(plan.chunks[1].world_transforms.size(), 64u);
  EXPECT_EQ(plan.chunks[2].world_transforms.size(), 64u);
  EXPECT_EQ(plan.chunks[3].world_transforms.size(), 8u);
  for (const RenderInstanceBatchChunk &chunk : plan.chunks) {
    ASSERT_EQ(chunk.sampling_from_local.size(), chunk.occurrences.size());
    for (std::size_t slot = 0u; slot < chunk.occurrences.size(); ++slot) {
      EXPECT_FLOAT_EQ(chunk.sampling_from_local[slot].values[13],
                      static_cast<float>(chunk.occurrences[slot] - 1'000u) + 0.5f);
    }
  }
  EXPECT_EQ(plan.chunks[0].occurrences.front(), 1'000u);
  EXPECT_EQ(plan.chunks[3].occurrences.back(), 1'199u);
  ASSERT_TRUE(plan.chunks[0].world_bounds.present);
  EXPECT_FLOAT_EQ(plan.chunks[0].world_bounds.min.x, -1.0f);
  EXPECT_FLOAT_EQ(plan.chunks[0].world_bounds.max.x, 127.0f);
  ASSERT_TRUE(plan.chunks[3].world_bounds.present);
  EXPECT_FLOAT_EQ(plan.chunks[3].world_bounds.min.x, 383.0f);
  EXPECT_FLOAT_EQ(plan.chunks[3].world_bounds.max.x, 399.0f);
}

TEST(RenderInstanceBatchPlannerTests,
     GroupsByPayloadAndClassifiesTransformOnlyUpdates) {
  const RenderInstancePrimitiveKey first_key{
      .geometry_payload = 11u,
      .material_payload = 22u,
      .index_count = 3u,
  };
  RenderInstancePrimitiveKey second_key = first_key;
  second_key.material_payload = 33u;
  const RenderInstanceBounds bounds{
      .min = {0.0f, 0.0f, 0.0f},
      .max = {1.0f, 1.0f, 1.0f},
      .present = true,
  };
  std::vector<RenderInstancePrimitiveOccurrence> occurrences{
      {.key = first_key, .occurrence = 7u, .local_bounds = bounds},
      {.key = second_key, .occurrence = 9u, .local_bounds = bounds},
      {.key = first_key, .occurrence = 8u, .local_bounds = bounds},
  };
  const RenderInstanceBatchPlan before =
      plan_render_instance_batches(occurrences, 64u);
  ASSERT_EQ(before.chunks.size(), 2u);
  EXPECT_EQ(before.chunks[0].occurrences,
            (std::vector<std::uint64_t>{7u, 8u}));
  EXPECT_EQ(before.chunks[1].occurrences,
            (std::vector<std::uint64_t>{9u}));
  EXPECT_EQ(classify_instance_batch_update(before, before),
            RenderInstanceBatchUpdateKind::Unchanged);

  occurrences[2].sampling_from_local.values[12] = 9.0f;
  const RenderInstanceBatchPlan mapped =
      plan_render_instance_batches(occurrences, 64u, &before);
  EXPECT_EQ(classify_instance_batch_update(before, mapped),
            RenderInstanceBatchUpdateKind::TransformsOnly);
  EXPECT_EQ(mapped.chunks.front().occurrences, before.chunks.front().occurrences);
  EXPECT_EQ(mapped.chunks.front().key, before.chunks.front().key);
  EXPECT_FLOAT_EQ(mapped.chunks.front().sampling_from_local.back().values[12], 9.0f);

  occurrences[2].world_transform.values[13] = 5.0f;
  const RenderInstanceBatchPlan moved =
      plan_render_instance_batches(occurrences, 64u);
  EXPECT_EQ(classify_instance_batch_update(before, moved),
            RenderInstanceBatchUpdateKind::TransformsOnly);

  occurrences[2].key.index_count = 6u;
  const RenderInstanceBatchPlan changed_payload =
      plan_render_instance_batches(occurrences, 64u);
  EXPECT_EQ(classify_instance_batch_update(moved, changed_payload),
            RenderInstanceBatchUpdateKind::Rebuild);
}

TEST(RenderInstanceBatchPlannerTests,
     DistinctPayloadsReserveChunkStorageOnceAndRetainSemanticLayout) {
  for (const std::size_t count : {128u, 512u}) {
    SCOPED_TRACE(count);
    std::vector<RenderInstancePrimitiveOccurrence> occurrences;
    occurrences.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
      occurrences.push_back({
          .key = {.geometry_payload = index + 1u,
                  .material_payload = 7u,
                  .index_count = 3u},
          .occurrence = index + 1u,
          .local_bounds = {.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f},
                           .present = true},
      });
    }
    const RenderInstanceBatchPlan initial =
        plan_render_instance_batches(occurrences, 64u);
    ASSERT_EQ(initial.chunks.size(), count);
    EXPECT_EQ(initial.work.chunk_storage_growth_count, 1u);
    EXPECT_EQ(initial.work.relocated_chunk_count, 0u)
        << "distinct meshes must not repeatedly relocate all earlier chunks";
    EXPECT_EQ(initial.work.morton_ordered_occurrence_count, count);
    EXPECT_FALSE(initial.work.retained_layout);

    occurrences.front().world_transform.values[12] = 11.0f;
    const RenderInstanceBatchPlan moved =
        plan_render_instance_batches(occurrences, 64u, &initial);
    ASSERT_EQ(moved.chunks.size(), count);
    EXPECT_EQ(moved.work.chunk_storage_growth_count, 1u);
    EXPECT_EQ(moved.work.relocated_chunk_count, 0u);
    EXPECT_EQ(moved.work.morton_ordered_occurrence_count, 0u);
    EXPECT_TRUE(moved.work.retained_layout);
    EXPECT_EQ(classify_instance_batch_update(initial, moved),
              RenderInstanceBatchUpdateKind::TransformsOnly);
    EXPECT_FLOAT_EQ(moved.chunks.front().world_bounds.min.x, 10.0f);
    EXPECT_FLOAT_EQ(moved.chunks.front().world_bounds.max.x, 12.0f);
  }
}

TEST(RenderInstanceBatchPlannerTests,
     VisibilityDeltaRetainsLayoutAndFiniteSourceTransforms) {
  const RenderInstancePrimitiveKey key{
      .geometry_payload = 71u,
      .material_payload = 73u,
      .index_count = 3u,
  };
  const RenderInstanceBounds bounds{
      .min = {-1.0f, -1.0f, -1.0f},
      .max = {1.0f, 1.0f, 1.0f},
      .present = true,
  };
  std::array<RenderInstancePrimitiveOccurrence, 3u> occurrences;
  for (std::size_t index = 0u; index < occurrences.size(); ++index) {
    QMat4 transform;
    transform.values[12] = static_cast<float>(index * 10u);
    occurrences[index] = {
        .key = key,
        .occurrence = static_cast<std::uint64_t>(index + 1u),
        .world_transform = transform,
        .local_bounds = bounds,
    };
  }

  const RenderInstanceBatchPlan visible =
      plan_render_instance_batches(occurrences, 64u);
  occurrences[2].visible = false;
  const RenderInstanceBatchPlan partially_hidden =
      plan_render_instance_batches(occurrences, 64u);

  ASSERT_EQ(visible.chunks.size(), 1u);
  ASSERT_EQ(partially_hidden.chunks.size(), 1u);
  const RenderInstanceBatchChunk &before = visible.chunks.front();
  const RenderInstanceBatchChunk &after =
      partially_hidden.chunks.front();
  EXPECT_EQ(after.occurrences, before.occurrences);
  EXPECT_EQ(after.world_transforms.size(), before.world_transforms.size());
  EXPECT_EQ(after.visible_occurrence_count, 2u);
  EXPECT_EQ(after.visibility,
            (std::vector<std::uint8_t>{1u, 1u, 0u}));
  EXPECT_FLOAT_EQ(after.world_transforms[2].values[12], 20.0f)
      << "suppression metadata must not inject a singular or far transform";
  ASSERT_TRUE(after.world_bounds.present);
  EXPECT_FLOAT_EQ(after.world_bounds.max.x, 11.0f);
  EXPECT_EQ(classify_instance_batch_update(visible, partially_hidden),
            RenderInstanceBatchUpdateKind::TransformsOnly);

  occurrences[2].sampling_from_local.values[12] = 52.0f;
  const RenderInstanceBatchPlan hidden_mapping_changed =
      plan_render_instance_batches(occurrences, 64u, &partially_hidden);
  EXPECT_EQ(classify_instance_batch_update(partially_hidden, hidden_mapping_changed),
            RenderInstanceBatchUpdateKind::Unchanged)
      << "A hidden slot is retained without rewriting a visible GPU prefix";
  EXPECT_FLOAT_EQ(hidden_mapping_changed.chunks.front()
                      .sampling_from_local[2].values[12], 52.0f);
  occurrences[2].visible = true;
  const RenderInstanceBatchPlan revealed =
      plan_render_instance_batches(occurrences, 64u, &hidden_mapping_changed);
  EXPECT_EQ(classify_instance_batch_update(hidden_mapping_changed, revealed),
            RenderInstanceBatchUpdateKind::TransformsOnly);
  EXPECT_FLOAT_EQ(revealed.chunks.front().sampling_from_local[2].values[12], 52.0f);

  for (RenderInstancePrimitiveOccurrence &occurrence : occurrences) {
    occurrence.visible = false;
  }
  const RenderInstanceBatchPlan fully_hidden =
      plan_render_instance_batches(occurrences, 64u);
  ASSERT_EQ(fully_hidden.chunks.size(), 1u);
  EXPECT_EQ(fully_hidden.chunks.front().occurrences, before.occurrences);
  EXPECT_EQ(fully_hidden.chunks.front().visible_occurrence_count, 0u);
  EXPECT_FALSE(fully_hidden.chunks.front().world_bounds.present);
  EXPECT_EQ(classify_instance_batch_update(partially_hidden, fully_hidden),
            RenderInstanceBatchUpdateKind::TransformsOnly);
}

TEST(RenderInstanceBatchPlannerTests,
     CoversOneSixtyFourSixtyFiveAndTwoHundredOccurrences) {
  const RenderInstancePrimitiveKey key{
      .geometry_payload = 5u,
      .material_payload = 6u,
      .index_count = 3u,
  };
  const RenderInstanceBounds bounds{
      .min = {-0.5f, -0.5f, -0.5f},
      .max = {0.5f, 0.5f, 0.5f},
      .present = true,
  };
  /// Describes one participant count and its expected device-cap chunks.
  struct Case final {
    std::size_t occurrence_count;
    std::vector<std::size_t> chunk_sizes;
  };
  const std::array<Case, 4u> cases{{
      {1u, {1u}},
      {64u, {64u}},
      {65u, {64u, 1u}},
      {200u, {64u, 64u, 64u, 8u}},
  }};
  for (const Case &test_case : cases) {
    std::vector<RenderInstancePrimitiveOccurrence> occurrences;
    occurrences.reserve(test_case.occurrence_count);
    for (std::size_t index = 0u; index < test_case.occurrence_count; ++index) {
      QMat4 transform;
      transform.values[12] = static_cast<float>(index);
      occurrences.push_back({
          .key = key,
          .occurrence = static_cast<std::uint64_t>(index + 1u),
          .world_transform = transform,
          .local_bounds = bounds,
      });
    }
    const RenderInstanceBatchPlan plan =
        plan_render_instance_batches(occurrences, 64u);
    ASSERT_EQ(plan.chunks.size(), test_case.chunk_sizes.size());
    for (std::size_t index = 0u; index < test_case.chunk_sizes.size(); ++index) {
      EXPECT_EQ(plan.chunks[index].occurrences.size(),
                test_case.chunk_sizes[index]);
    }
  }
}

TEST(RenderInstanceBatchPlannerTests,
     SplitsMaterialAndDimPresentationPayloads) {
  RenderInstancePrimitiveKey base{
      .geometry_payload = 17u,
      .material_payload = 29u,
      .index_count = 3u,
  };
  RenderInstancePrimitiveKey second_material = base;
  second_material.material_payload = 30u;
  RenderInstancePrimitiveKey dimmed = base;
  dimmed.presentation_flags =
      static_cast<std::uint8_t>(RenderInstancePresentationFlag::Dimmed);
  const RenderInstanceBounds bounds{
      .min = {-1.0f, -1.0f, -1.0f},
      .max = {1.0f, 1.0f, 1.0f},
      .present = true,
  };
  const std::array<RenderInstancePrimitiveOccurrence, 3u> occurrences{{
      {.key = base, .occurrence = 1u, .local_bounds = bounds},
      {.key = second_material, .occurrence = 2u, .local_bounds = bounds},
      {.key = dimmed, .occurrence = 3u, .local_bounds = bounds},
  }};

  const RenderInstanceBatchPlan plan =
      plan_render_instance_batches(occurrences, 64u);
  ASSERT_EQ(plan.chunks.size(), 3u);
  for (const RenderInstanceBatchChunk &chunk : plan.chunks) {
    EXPECT_EQ(chunk.occurrences.size(), 1u);
  }
}

TEST(RenderInstanceBatchPlannerTests,
     MortonSortsCompatibleOccurrencesBeforeChunking) {
  const RenderInstancePrimitiveKey key{
      .geometry_payload = 41u,
      .material_payload = 43u,
      .index_count = 3u,
  };
  const RenderInstanceBounds bounds{
      .min = {0.0f, 0.0f, 0.0f},
      .max = {1.0f, 1.0f, 1.0f},
      .present = true,
  };
  std::array<RenderInstancePrimitiveOccurrence, 4u> occurrences;
  const std::array<float, 4u> positions{{30.0f, 0.0f, 20.0f, 10.0f}};
  for (std::size_t index = 0u; index < occurrences.size(); ++index) {
    QMat4 transform;
    transform.values[12] = positions[index];
    occurrences[index] = {
        .key = key,
        .occurrence = static_cast<std::uint64_t>(index + 1u),
        .world_transform = transform,
        .local_bounds = bounds,
    };
  }

  const RenderInstanceBatchPlan plan =
      plan_render_instance_batches(occurrences, 2u);
  ASSERT_EQ(plan.chunks.size(), 2u);
  EXPECT_EQ(plan.chunks[0].occurrences,
            (std::vector<std::uint64_t>{2u, 4u}));
  EXPECT_EQ(plan.chunks[1].occurrences,
            (std::vector<std::uint64_t>{3u, 1u}));
  EXPECT_FLOAT_EQ(plan.chunks[0].world_bounds.min.x, 0.0f);
  EXPECT_FLOAT_EQ(plan.chunks[0].world_bounds.max.x, 11.0f);
  EXPECT_FLOAT_EQ(plan.chunks[1].world_bounds.min.x, 20.0f);
  EXPECT_FLOAT_EQ(plan.chunks[1].world_bounds.max.x, 31.0f);
}

TEST(InstanceRenderableCacheLookupTests,
     ResolvesSourceRecordsWithOneExactKeyLookupPerChunk) {
  const auto exercise = [](std::size_t record_count) {
    std::vector<RenderSceneInstancePrimitiveRecord> records;
    std::vector<RenderInstanceBatchChunk> chunks;
    records.reserve(record_count + 1u);
    chunks.reserve(record_count * 3u);
    for (std::size_t index = 0u; index < record_count; ++index) {
      RenderInstancePrimitiveKey key{
          .geometry_payload = 10'000u + index,
          .material_payload = 20'000u + index,
          .index_offset = static_cast<std::uint32_t>(index * 3u),
          .index_count = 3u,
          .double_sided = index % 2u != 0u,
      };
      records.push_back({.key = key});
      for (std::uint8_t variant = 0u; variant < 3u; ++variant) {
        RenderInstanceBatchChunk chunk;
        chunk.key = key;
        chunk.key.layer_mask = static_cast<std::uint8_t>(variant + 1u);
        chunk.key.channel = variant;
        chunk.key.cast_shadows = variant == 0u;
        chunk.key.receive_shadows = variant == 0u;
        chunk.key.editor_unlit = variant != 0u;
        chunks.push_back(std::move(chunk));
      }
    }

    RenderSceneInstancePrimitiveRecord duplicate = records.front();
    duplicate.key.layer_mask = 0xffu;
    duplicate.key.channel = 0xffu;
    duplicate.key.cast_shadows = true;
    duplicate.key.receive_shadows = true;
    duplicate.key.editor_unlit = true;
    records.push_back(std::move(duplicate));

    auto result =
        instance_renderable_cache_detail::resolve_primitive_records(
            records, chunks);
    EXPECT_EQ(result.indexed_key_count, record_count);
    EXPECT_EQ(result.key_lookup_count, chunks.size());
    EXPECT_EQ(result.records_by_chunk.size(), chunks.size());
    if (result.records_by_chunk.size() == chunks.size()) {
      for (std::size_t chunk_index = 0u; chunk_index < chunks.size();
           ++chunk_index) {
        EXPECT_EQ(result.records_by_chunk[chunk_index],
                  &records[chunk_index / 3u]);
      }
    }
    return result.key_lookup_count;
  };

  const std::size_t small_lookup_count = exercise(128u);
  const std::size_t large_lookup_count = exercise(1'024u);
  EXPECT_EQ(large_lookup_count, small_lookup_count * 8u);
}

TEST(InstanceRenderableCacheLookupTests,
     ReusesEveryShiftedChunkWithLinearCandidateProbes) {
  const auto exercise = [](std::size_t old_chunk_count) {
    std::vector<RenderInstanceBatchChunk> old_chunks;
    old_chunks.reserve(old_chunk_count);
    for (std::size_t index = 0u; index < old_chunk_count; ++index) {
      RenderInstanceBatchChunk chunk;
      chunk.key = {
          .geometry_payload = 30'000u + index,
          .material_payload = 40'000u + index,
          .index_count = 3u,
      };
      chunk.occurrences.push_back(50'000u + index);
      chunk.world_transforms.emplace_back();
      chunk.visibility.push_back(1u);
      old_chunks.push_back(std::move(chunk));
    }

    std::vector<RenderInstanceBatchChunk> next_chunks;
    next_chunks.reserve(old_chunk_count + 1u);
    RenderInstanceBatchChunk inserted;
    inserted.key = {
        .geometry_payload = 1u,
        .material_payload = 2u,
        .index_count = 3u,
    };
    inserted.occurrences.push_back(3u);
    inserted.world_transforms.emplace_back();
    inserted.visibility.push_back(1u);
    next_chunks.push_back(std::move(inserted));
    next_chunks.insert(next_chunks.end(), old_chunks.begin(),
                       old_chunks.end());

    const instance_renderable_cache_detail::ChunkProxyReuseCandidate
        reusable_candidate{.instance_capacity = 1u, .valid = true};
    std::vector<
        instance_renderable_cache_detail::ChunkProxyReuseCandidate>
        candidates(old_chunk_count, reusable_candidate);
    auto result =
        instance_renderable_cache_detail::match_reusable_chunk_proxies(
            old_chunks, candidates, next_chunks);

    EXPECT_EQ(result.old_chunk_by_next_chunk.size(), next_chunks.size());
    if (result.old_chunk_by_next_chunk.size() == next_chunks.size()) {
      EXPECT_EQ(result.old_chunk_by_next_chunk.front(),
                result.no_old_chunk);
      for (std::size_t next_index = 1u; next_index < next_chunks.size();
           ++next_index) {
        EXPECT_EQ(result.old_chunk_by_next_chunk[next_index],
                  next_index - 1u);
      }
    }
    EXPECT_EQ(result.indexed_candidate_count, old_chunk_count);
    EXPECT_EQ(result.key_lookup_count, next_chunks.size());
    EXPECT_EQ(result.candidate_probe_count, old_chunk_count);
    EXPECT_LE(result.candidate_probe_count,
              result.indexed_candidate_count);
    return result.candidate_probe_count;
  };

  const std::size_t small_probe_count = exercise(128u);
  const std::size_t large_probe_count = exercise(1'024u);
  EXPECT_EQ(large_probe_count, small_probe_count * 8u);

  RenderInstanceBatchChunk duplicate;
  duplicate.key = {
      .geometry_payload = 71u,
      .material_payload = 73u,
      .index_count = 3u,
  };
  duplicate.occurrences.push_back(79u);
  duplicate.world_transforms.emplace_back();
  duplicate.visibility.push_back(1u);
  const std::array<RenderInstanceBatchChunk, 2u> duplicate_old{
      duplicate, duplicate};
  const std::array<RenderInstanceBatchChunk, 2u> duplicate_next{
      duplicate, duplicate};
  const std::array<
      instance_renderable_cache_detail::ChunkProxyReuseCandidate, 2u>
      duplicate_candidates{{
          {.instance_capacity = 1u, .valid = true},
          {.instance_capacity = 1u, .valid = true},
      }};
  const auto duplicate_result =
      instance_renderable_cache_detail::match_reusable_chunk_proxies(
          duplicate_old, duplicate_candidates, duplicate_next);
  EXPECT_EQ(duplicate_result.old_chunk_by_next_chunk,
            (std::vector<std::size_t>{0u, 1u}))
      << "same-index reuse remains preferred for duplicate exact layouts";

  const std::array<
      instance_renderable_cache_detail::ChunkProxyReuseCandidate, 4u>
      fallback_candidates{{
          {.instance_capacity = 0u, .valid = true},
          {.instance_capacity = 1u, .valid = false},
          {.instance_capacity = 1u, .valid = true},
          {.instance_capacity = 1u, .valid = true},
      }};
  const std::array<RenderInstanceBatchChunk, 4u> fallback_old{
      duplicate, duplicate, duplicate, duplicate};
  const auto fallback_result =
      instance_renderable_cache_detail::match_reusable_chunk_proxies(
          fallback_old, fallback_candidates, duplicate_next);
  EXPECT_EQ(fallback_result.old_chunk_by_next_chunk,
            (std::vector<std::size_t>{2u, 3u}))
      << "fallback keeps ascending old-index order while excluding invalid "
         "or undersized proxies";
}

TEST(InstanceRenderableCacheLookupTests,
     ReusesSameKeyCapacityWhenActiveOccurrenceCountChanges) {
  RenderInstanceBatchChunk before;
  before.key = {
      .geometry_payload = 101u,
      .material_payload = 103u,
      .index_count = 3u,
      .layer_mask = 1u,
  };
  before.occurrences = {1u, 2u, 3u, 4u};
  before.world_transforms.resize(4u);
  before.visibility.assign(4u, 1u);

  RenderInstanceBatchChunk after = before;
  after.occurrences.resize(64u);
  after.world_transforms.resize(64u);
  after.visibility.assign(64u, 1u);
  for (std::size_t index = 0u; index < after.occurrences.size(); ++index) {
    after.occurrences[index] = static_cast<std::uint64_t>(index + 20u);
  }

  const std::array<RenderInstanceBatchChunk, 1u> old_chunks{{before}};
  const std::array<RenderInstanceBatchChunk, 1u> next_chunks{{after}};
  const std::array<
      instance_renderable_cache_detail::ChunkProxyReuseCandidate, 1u>
      candidates{{{.instance_capacity = 64u, .valid = true}}};
  const auto grown =
      instance_renderable_cache_detail::match_reusable_chunk_proxies(
          old_chunks, candidates, next_chunks);
  EXPECT_EQ(grown.old_chunk_by_next_chunk,
            (std::vector<std::size_t>{0u}));
  EXPECT_EQ(grown.capacity_key_lookup_count, 1u);
  EXPECT_EQ(grown.capacity_candidate_probe_count, 1u);

  const auto shrunk =
      instance_renderable_cache_detail::match_reusable_chunk_proxies(
          next_chunks, candidates, old_chunks);
  EXPECT_EQ(shrunk.old_chunk_by_next_chunk,
            (std::vector<std::size_t>{0u}));
  EXPECT_EQ(shrunk.capacity_candidate_probe_count, 1u);

  const std::array<
      instance_renderable_cache_detail::ChunkProxyReuseCandidate, 1u>
      undersized{{{.instance_capacity = 32u, .valid = true}}};
  const auto rejected =
      instance_renderable_cache_detail::match_reusable_chunk_proxies(
          old_chunks, undersized, next_chunks);
  EXPECT_EQ(rejected.old_chunk_by_next_chunk,
            (std::vector<std::size_t>{rejected.no_old_chunk}));
}

TEST(RenderSceneSnapshotTests, DiffuseModelsKeepSeparateInstanceMaterialPayloads) {
  class DiffuseModelResolver final : public RenderProjectMaterialResolver {
  public:
    std::uint64_t revision() const noexcept override { return revision_value; }
    std::optional<RenderProjectMaterialSnapshot>
    resolve(ProjectAssetId id) const override {
      return RenderProjectMaterialSnapshot{
          .evaluation = RenderMaterialEvaluationSnapshot{
              .project_material_id = id,
              .kind = RenderMaterialEvaluationKind::Standard,
              .identity_revision = 1u,
              .content_revision = revision_value,
              .payload = RenderStandardMaterialEvaluationSnapshot{
                  .diffuse_model = separate_models && id == lambert_id
                                       ? MaterialDiffuseModel::Lambert
                                       : MaterialDiffuseModel::Burley,
              },
          },
          .catalog_revision = revision_value,
      };
    }
    ProjectAssetId lambert_id;
    std::uint64_t revision_value = 1u;
    bool separate_models = true;
  } resolver;
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const ProjectAssetId burley_id =
      *ProjectAssetId::parse("58ecaccc-cb80-4126-9d76-cebb03df1b7e");
  resolver.lambert_id =
      *ProjectAssetId::parse("74b4e78c-9a47-46e4-97bb-4776b4688f72");
  for (const ProjectAssetId material : {burley_id, resolver.lambert_id}) {
    for (std::size_t instance = 0u; instance < 2u; ++instance) {
      const EntityId entity =
          document.create_entity("Diffuse Mesh", EntityKind::Mesh);
      ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
      ASSERT_TRUE(document.set_material(entity, MaterialComponent{
          .material = MaterialReference::project(material)}));
    }
  }

  const RenderSceneSnapshot separate = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  EXPECT_TRUE(separate.meshes.empty());
  ASSERT_EQ(separate.instance_primitives.size(), 2u);
  const auto &first = separate.instance_primitives[0];
  const auto &second = separate.instance_primitives[1];
  EXPECT_EQ(first.mesh.geometry_payload, second.mesh.geometry_payload);
  EXPECT_NE(first.key.material_payload, second.key.material_payload);
  EXPECT_NE(first.material.primary_material.document.diffuse_model,
            second.material.primary_material.document.diffuse_model);
  EXPECT_EQ(first.occurrences.size(), 2u);
  EXPECT_EQ(second.occurrences.size(), 2u);

  resolver.separate_models = false;
  ++resolver.revision_value;
  const RenderSceneSnapshot combined = build_render_scene_snapshot(
      document, assets,
      {.previous = &separate, .project_material_resolver = &resolver});
  EXPECT_TRUE(combined.meshes.empty());
  ASSERT_EQ(combined.instance_primitives.size(), 1u);
  const auto &primitive = combined.instance_primitives.front();
  EXPECT_EQ(primitive.occurrences.size(), 4u);
  EXPECT_EQ(primitive.mesh.geometry_payload, first.mesh.geometry_payload);
  EXPECT_EQ(primitive.material.primary_material.document.diffuse_model,
            MaterialDiffuseModel::Burley);
}

TEST(RenderSceneWireframeBatchPlannerTests,
     TwoHundredOrdinaryLogicalAssetsShareOnePayloadAndFourChunksPerPass) {
  SceneDocument document;
  AssetRegistry assets;
  std::vector<AssetId> logical_assets;
  logical_assets.reserve(200u);
  std::uintptr_t shared_geometry_token = 0u;
  for (std::size_t index = 0u; index < 200u; ++index) {
    const AssetId asset = assets.add_mesh(make_overlay_triangle_mesh());
    ASSERT_NE(asset, kInvalidAssetId);
    const MeshAsset *mesh = assets.mesh(asset);
    ASSERT_NE(mesh, nullptr);
    if (index == 0u) {
      shared_geometry_token = mesh->geometry_token();
    }
    EXPECT_EQ(mesh->geometry_token(), shared_geometry_token);
    logical_assets.push_back(asset);

    const EntityId entity = document.create_entity(
        "Shared Mesh " + std::to_string(index + 1u), EntityKind::Mesh);
    document.set_mesh(entity, MeshComponent{.mesh = asset});
    document.set_transform(
        entity,
        TransformComponent{
            .position = {static_cast<float>(index) * 2.0f, 0.0f, 0.0f},
        });
  }

  const RenderSceneSnapshot scene =
      build_render_scene_snapshot(document, assets);
  EXPECT_TRUE(scene.meshes.empty())
      << "compatible ordinary duplicates must not leave single-mesh proxies";
  ASSERT_EQ(scene.instance_primitives.size(), 1u);
  const RenderSceneInstancePrimitiveRecord &primitive =
      scene.instance_primitives.front();
  ASSERT_EQ(primitive.occurrences.size(), 200u);
  EXPECT_EQ(primitive.key.geometry_payload, shared_geometry_token);

  std::vector<RenderInstancePrimitiveOccurrence> base_occurrences;
  base_occurrences.reserve(primitive.occurrences.size());
  for (const RenderSceneInstanceOccurrenceRecord &occurrence :
       primitive.occurrences) {
    base_occurrences.push_back({
        .key = primitive.key,
        .occurrence = occurrence.occurrence,
        .world_transform = occurrence.world_transform,
        .local_bounds = primitive.local_bounds,
    });
  }
  const RenderInstanceBatchPlan base_plan =
      plan_render_instance_batches(base_occurrences, 64u);
  ASSERT_EQ(base_plan.chunks.size(), 4u);
  EXPECT_EQ(base_plan.chunks[0].occurrences.size(), 64u);
  EXPECT_EQ(base_plan.chunks[1].occurrences.size(), 64u);
  EXPECT_EQ(base_plan.chunks[2].occurrences.size(), 64u);
  EXPECT_EQ(base_plan.chunks[3].occurrences.size(), 8u);

  const RenderOverlaySceneWireframeSnapshot wire_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  ASSERT_EQ(wire_snapshot.meshes.size(), 200u);
  const MeshWireGeometryPayload *shared_wire_geometry =
      wire_snapshot.meshes.front().wire_geometry_payload.get();
  ASSERT_NE(shared_wire_geometry, nullptr);
  for (const RenderOverlayMeshSnapshot &mesh : wire_snapshot.meshes) {
    ASSERT_NE(mesh.geometry_payload, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(mesh.geometry_payload.get()),
              shared_geometry_token);
    EXPECT_EQ(mesh.wire_geometry_payload.get(), shared_wire_geometry);
    EXPECT_EQ(mesh.wire_segments.data(), shared_wire_geometry->segments.data());
  }
  const SceneWireframeOverlayBatchPlan wire_plan =
      plan_scene_wireframe_overlay_batches(wire_snapshot, 64u);
  EXPECT_EQ(wire_plan.participant_count, 200u);
  EXPECT_EQ(wire_plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(wire_plan.batches.chunks.size(), 4u);
  EXPECT_EQ(wire_plan.batches.chunks[0].occurrences.size(), 64u);
  EXPECT_EQ(wire_plan.batches.chunks[1].occurrences.size(), 64u);
  EXPECT_EQ(wire_plan.batches.chunks[2].occurrences.size(), 64u);
  EXPECT_EQ(wire_plan.batches.chunks[3].occurrences.size(), 8u);
}

TEST(RenderSceneWireframeBatchPlannerTests,
     SharedGeometryPrepareWorkIsLinearAtDoubleOccurrenceCount) {
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_overlay_triangle_mesh());
  const MeshAsset *mesh = assets.mesh(asset);
  ASSERT_NE(mesh, nullptr);
  const std::shared_ptr<const MeshGeometryPayload> geometry =
      mesh->geometry_payload();
  const std::shared_ptr<const MeshWireGeometryPayload> wire_geometry =
      mesh->wire_geometry_payload();
  ASSERT_NE(geometry, nullptr);
  ASSERT_NE(wire_geometry, nullptr);

  const auto exercise = [&](std::size_t participant_count) {
    RenderOverlaySceneWireframeSnapshot snapshot{.layer_mask = 1u};
    snapshot.meshes.reserve(participant_count);
    for (std::size_t index = 0u; index < participant_count; ++index) {
      snapshot.meshes.push_back({
          .entity = static_cast<EntityId>(index + 1u),
          .participant_token = static_cast<std::uint64_t>(index + 1u),
          .mesh_id = asset,
          .mesh_revision = mesh->revision.value,
          .geometry_payload = geometry,
          .geometry_content_id = mesh->geometry_content_id(),
          .wire_geometry_payload = wire_geometry,
          .vertices = geometry->vertices,
          .indices = geometry->indices,
          .wire_segments = wire_geometry->segments,
      });
    }
    const SceneWireframeOverlayBatchPlan plan =
        plan_scene_wireframe_overlay_batches(snapshot, 1u);
    EXPECT_EQ(plan.participant_count, participant_count);
    EXPECT_EQ(plan.batches.chunks.size(), participant_count);
    EXPECT_EQ(plan.work.primitive_record_index_entry_count, 1u);
    EXPECT_EQ(plan.work.chunk_record_lookup_count, participant_count);
    EXPECT_EQ(plan.work.local_bounds_cache_hit_count,
              participant_count - 1u);
    EXPECT_EQ(plan.work.local_bounds_vertex_visit_count,
              geometry->vertices.size());
    return plan.work;
  };

  const SceneWireframeOverlayPlanWorkStats work_n = exercise(64u);
  const SceneWireframeOverlayPlanWorkStats work_2n = exercise(128u);
  EXPECT_EQ(work_2n.chunk_record_lookup_count,
            work_n.chunk_record_lookup_count * 2u);
  EXPECT_EQ(work_2n.local_bounds_vertex_visit_count,
            work_n.local_bounds_vertex_visit_count);
}

TEST(RenderSceneWireframeBatchPlannerTests,
     CoversDeviceBoundaryStylesAndTransformOnlyUpdates) {
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_overlay_triangle_mesh());
  const MeshAsset *mesh = assets.mesh(asset);
  ASSERT_NE(mesh, nullptr);
  const std::shared_ptr<const MeshGeometryPayload> payload =
      mesh->geometry_payload();
  const std::shared_ptr<const MeshWireGeometryPayload> wire_payload =
      mesh->wire_geometry_payload();
  ASSERT_NE(wire_payload, nullptr);

  const std::array<std::pair<std::size_t, std::vector<std::size_t>>, 4u>
      cases{{
          {1u, {1u}},
          {64u, {64u}},
          {65u, {64u, 1u}},
          {200u, {64u, 64u, 64u, 8u}},
      }};
  for (const auto &[participant_count, expected_chunks] : cases) {
    RenderOverlaySceneWireframeSnapshot snapshot{.layer_mask = 1u};
    snapshot.meshes.reserve(participant_count);
    for (std::size_t index = 0u; index < participant_count; ++index) {
      QMat4 transform;
      transform.values[12] = static_cast<float>(index * 2u);
      snapshot.meshes.push_back({
          .entity = static_cast<EntityId>(index + 1u),
          .participant_token = static_cast<std::uint64_t>(index + 1u),
          .mesh_id = asset,
          .mesh_revision = mesh->revision.value,
          .geometry_payload = payload,
          .geometry_content_id = mesh->geometry_content_id(),
          .wire_geometry_payload = wire_payload,
          .world_transform = transform,
          .vertices = payload->vertices,
          .indices = payload->indices,
          .wire_segments = wire_payload->segments,
      });
    }
    const SceneWireframeOverlayBatchPlan plan =
        plan_scene_wireframe_overlay_batches(snapshot, 64u);
    ASSERT_EQ(plan.batches.chunks.size(), expected_chunks.size());
    for (std::size_t index = 0u; index < expected_chunks.size(); ++index) {
      EXPECT_EQ(plan.batches.chunks[index].occurrences.size(),
                expected_chunks[index]);
    }

    RenderOverlaySceneWireframeSnapshot moved = snapshot;
    for (RenderOverlayMeshSnapshot &record : moved.meshes) {
      record.world_transform.values[13] += 10.0f;
    }
    const SceneWireframeOverlayBatchPlan moved_plan =
        plan_scene_wireframe_overlay_batches(moved, 64u, &plan.batches);
    EXPECT_TRUE(moved_plan.batches.work.retained_layout);
    EXPECT_EQ(moved_plan.batches.work.morton_ordered_occurrence_count, 0u);
    EXPECT_EQ(classify_instance_batch_update(plan.batches,
                                             moved_plan.batches),
              RenderInstanceBatchUpdateKind::TransformsOnly);
    if (participant_count > 64u) {
      moved.meshes.front().world_transform.values[12] = 10'000.0f;
      const SceneWireframeOverlayBatchPlan crossed =
          plan_scene_wireframe_overlay_batches(moved, 64u,
                                                &moved_plan.batches);
      EXPECT_TRUE(crossed.batches.work.retained_layout);
      EXPECT_EQ(classify_instance_batch_update(moved_plan.batches,
                                               crossed.batches),
                RenderInstanceBatchUpdateKind::TransformsOnly)
          << "moving past another Morton cell must retain semantic wire slots";
      EXPECT_EQ(crossed.batches.chunks.front().occurrences,
                moved_plan.batches.chunks.front().occurrences);
    }
  }

  RenderOverlaySceneWireframeSnapshot styled{.layer_mask = 1u};
  styled.meshes.reserve(200u);
  for (std::size_t index = 0u; index < 200u; ++index) {
    styled.meshes.push_back({
        .entity = static_cast<EntityId>(index + 1u),
        .participant_token = static_cast<std::uint64_t>(index + 1u),
        .mesh_id = asset,
        .mesh_revision = mesh->revision.value,
        .geometry_payload = payload,
        .geometry_content_id = mesh->geometry_content_id(),
        .wire_geometry_payload = wire_payload,
        .selected = index == 0u,
        .wireframe_tint = index >= 100u ? QVec3{1.0f, 0.5f, 0.0f}
                                       : kDefaultSceneLayerWireframeColor,
        .vertices = payload->vertices,
        .indices = payload->indices,
        .wire_segments = wire_payload->segments,
    });
  }
  const SceneWireframeOverlayBatchPlan split =
      plan_scene_wireframe_overlay_batches(styled, 64u);
  EXPECT_EQ(split.unique_geometry_style_count, 3u);
  EXPECT_EQ(split.batches.chunks.size(), 5u)
      << "one selected chunk plus two chunks for each layer color";
}

TEST(RenderSceneWireframeBatchPlannerTests,
     CurveChangesRetainPinnedMeshInputsButMeshPresentationChangesInvalidate) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh_asset = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId mesh = document.create_entity("Background Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_asset}));
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId curve = document.create_entity("Edited Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, {.curve = curve_asset}));
  const auto initial =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  ASSERT_EQ(initial.meshes.size(), 1u);
  const auto pinned_geometry = initial.meshes.front().geometry_payload;
  const auto pinned_wires = initial.meshes.front().wire_geometry_payload;
  ASSERT_NE(pinned_geometry, nullptr);
  ASSERT_NE(pinned_wires, nullptr);

  ASSERT_TRUE(document.set_transform(
      curve, {.position = {10.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(document.set_selected(curve, true));
  const auto moved_curve =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_NE(moved_curve.scene_revision, initial.scene_revision);
  EXPECT_TRUE(scene_wireframe_overlay_mesh_inputs_match(initial.meshes,
                                                        moved_curve.meshes));

  CurveAsset edited_asset = *assets.curve(curve_asset);
  modeling::CurveDocument edited = *edited_asset.document;
  ASSERT_TRUE(edited.set_point(edited.points().front().id,
      {.position = {0.0f, 2.0f, 0.0f},
       .outgoing_handle = {0.0f, 3.0f, 0.0f}}).ok());
  edited_asset.document =
      std::make_shared<const modeling::CurveDocument>(std::move(edited));
  ASSERT_TRUE(assets.replace_curve(curve_asset, std::move(edited_asset)));
  ASSERT_TRUE(document.notify_curve_payload_changed(curve));
  const auto edited_curve =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_NE(edited_curve.asset_revision, initial.asset_revision);
  EXPECT_TRUE(scene_wireframe_overlay_mesh_inputs_match(initial.meshes,
                                                        edited_curve.meshes));
  EXPECT_EQ(edited_curve.meshes.front().geometry_payload, pinned_geometry);
  EXPECT_EQ(edited_curve.meshes.front().wire_geometry_payload, pinned_wires);

  ASSERT_TRUE(document.set_selected(mesh, true));
  const auto selected =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(initial.meshes,
                                                         selected.meshes));
  ASSERT_TRUE(document.set_selected(mesh, false));
  ASSERT_TRUE(document.set_transform(mesh, {.position = {3.0f, 0.0f, 0.0f}}));
  const auto moved_mesh =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(initial.meshes,
                                                         moved_mesh.meshes));

  const SceneLayerId layer = document.create_layer("Tint", {0.2f, 0.5f, 0.8f});
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(document.set_entity_layer(mesh, layer));
  const auto tinted =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(moved_mesh.meshes,
                                                         tinted.meshes));
  ASSERT_TRUE(document.set_layer_visible(layer, false));
  const auto hidden =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(tinted.meshes,
                                                         hidden.meshes));
  ASSERT_TRUE(document.set_layer_visible(layer, true));
  ASSERT_TRUE(assets.replace_mesh(mesh_asset, make_authored_quad_mesh()));
  const auto replaced =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(tinted.meshes,
                                                         replaced.meshes));
  EXPECT_EQ(initial.meshes.front().vertices.data(), pinned_geometry->vertices.data());
  EXPECT_EQ(initial.meshes.front().wire_segments.data(), pinned_wires->segments.data());
  EXPECT_EQ(initial.meshes.front().vertices.size(), 3u)
      << "cached inputs keep the former payload alive after replacement";
}

TEST(RenderSceneWireframeBatchPlannerTests,
     ExactMeshInputGateRejectsPreviewMembershipIdentityAndUnpinnedSpans) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity = document.create_entity("Wire Input", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, {.mesh = asset}));
  const auto snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  EXPECT_TRUE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes,
                                                        snapshot.meshes));
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, {}));

  auto edited = snapshot.meshes;
  ++edited.front().participant_token;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, edited));
  edited = snapshot.meshes;
  edited.front().occurrence = entity + 1u;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, edited));
  edited = snapshot.meshes;
  edited.front().member = entity + 1u;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, edited));
  edited = snapshot.meshes;
  ++edited.front().mesh_revision;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, edited));

  edited = snapshot.meshes;
  edited.front().geometry_payload.reset();
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(edited, edited));
  edited = snapshot.meshes;
  edited.front().wire_geometry_payload.reset();
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(edited, edited));
  edited = snapshot.meshes;
  std::vector<MeshVertex> foreign_vertices(edited.front().vertices.begin(),
                                         edited.front().vertices.end());
  edited.front().vertices = foreign_vertices;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(edited, edited))
      << "an unrelated pin does not make mutable foreign spans immutable";
}

TEST(RenderSceneWireframeBatchPlannerTests,
     PrimaryAndCoSelectedMeshesShareOneWireframeStyleAndBatch) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId first =
      document.create_entity("First Selected Mesh", EntityKind::Mesh);
  const EntityId second =
      document.create_entity("Second Selected Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(first, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_mesh(second, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(first, true));
  ASSERT_TRUE(document.set_selected(second, true));
  ASSERT_EQ(document.primary_selection(), second);

  const std::uint32_t normal_color = rgba8(30, 70, 110);
  const ViewportPersistentOverlayStyleSnapshot style{
      .selection_wire_color = normal_color,
      .selection_edge_logical_width_pixels = 3.5f,
      .selection_edge_line_width_pixels = 3.5f,
      .wire_signature = 17u,
  };
  const RenderOverlaySceneWireframeSnapshot snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                     style);
  ASSERT_EQ(snapshot.meshes.size(), 2u);
  EXPECT_TRUE(std::ranges::all_of(
      snapshot.meshes,
      [](const RenderOverlayMeshSnapshot &record) { return record.selected; }));

  const SceneWireframeOverlayBatchPlan plan =
      plan_scene_wireframe_overlay_batches(snapshot, 64u);
  EXPECT_EQ(plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(plan.batches.chunks.size(), 1u);
  EXPECT_EQ(plan.batches.chunks.front().occurrences.size(), 2u);

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, snapshot);
  ASSERT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 6u);
  EXPECT_TRUE(std::ranges::all_of(
      frame.lines, [&](const ViewportOverlayLine &line) {
        return line.style_role == ViewportOverlayStyleRole::SelectionWire &&
               line.color == normal_color && line.width_pixels == 3.5f;
      }));
}

TEST(RenderSceneWireframeBatchPlannerTests,
     RasterScaleKeepsPersistentWireBatchIdentityWhileScalingWidth) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Raster-scaled wire", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));

  const ViewportPersistentOverlayStyleSnapshot baseline_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(
          ViewportSettings::defaults(), 1.0f);
  const ViewportPersistentOverlayStyleSnapshot scaled_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(
          ViewportSettings::defaults(), 2.0f);
  const RenderOverlaySceneWireframeSnapshot baseline =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                     baseline_style);
  const RenderOverlaySceneWireframeSnapshot scaled =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                     scaled_style);
  const SceneWireframeOverlayBatchPlan baseline_plan =
      plan_scene_wireframe_overlay_batches(baseline, 64u);
  const SceneWireframeOverlayBatchPlan scaled_plan =
      plan_scene_wireframe_overlay_batches(scaled, 64u);

  ASSERT_EQ(baseline_plan.batches.chunks.size(), 1u);
  ASSERT_EQ(scaled_plan.batches.chunks.size(), 1u);
  EXPECT_EQ(baseline_plan.batches.chunks.front().key,
            scaled_plan.batches.chunks.front().key);
  EXPECT_EQ(classify_instance_batch_update(baseline_plan.batches,
                                           scaled_plan.batches),
            RenderInstanceBatchUpdateKind::Unchanged)
      << "line width is a mutable material uniform, not proxy identity";
  EXPECT_NE(baseline_style.wire_signature, scaled_style.wire_signature);

  ViewportOverlayFrame baseline_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Component, baseline);
  ViewportOverlayFrame scaled_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Component, scaled);
  ViewportOverlayStyleResolver::apply(
      baseline_frame, ViewportSettings::defaults(), {.raster_scale = 1.0f});
  ViewportOverlayStyleResolver::apply(
      scaled_frame, ViewportSettings::defaults(), {.raster_scale = 2.0f});
  ASSERT_FALSE(baseline_frame.lines.empty());
  ASSERT_EQ(scaled_frame.lines.size(), baseline_frame.lines.size());
  EXPECT_FLOAT_EQ(baseline_frame.lines.front().width_pixels,
                  kOverlayWireframeXrayLineWidthPixels);
  EXPECT_FLOAT_EQ(scaled_frame.lines.front().width_pixels,
                  kOverlayWireframeXrayLineWidthPixels * 2.0f);

  RenderOverlaySceneWireframeSnapshot recolored = scaled;
  recolored.meshes.front().wireframe_tint = {0.9f, 0.1f, 0.2f};
  const SceneWireframeOverlayBatchPlan recolored_plan =
      plan_scene_wireframe_overlay_batches(recolored, 64u);
  EXPECT_EQ(classify_instance_batch_update(scaled_plan.batches,
                                           recolored_plan.batches),
            RenderInstanceBatchUpdateKind::Rebuild)
      << "baked vertex color remains part of persistent proxy identity";
}

TEST(RenderSceneWireframeBatchPlannerTests,
     ExpandsFormalOccurrenceMembersAtComposedWorldTransforms) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Member", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  document.set_transform(
      member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  const SceneLayerId layer =
      document.create_layer("Instance Layer", {0.25f, 0.5f, 0.75f});
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(document.set_entity_layer(occurrence, layer));

  const RenderOverlaySceneWireframeSnapshot snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  EXPECT_EQ(snapshot.meshes.front().entity, occurrence);
  EXPECT_NE(snapshot.meshes.front().participant_token, 0u);
  EXPECT_FLOAT_EQ(snapshot.meshes.front().world_transform.values[12], 12.0f);
  EXPECT_FLOAT_EQ(snapshot.meshes.front().wireframe_tint.x, 0.25f);
  EXPECT_FLOAT_EQ(snapshot.meshes.front().wireframe_tint.y, 0.5f);
  EXPECT_FLOAT_EQ(snapshot.meshes.front().wireframe_tint.z, 0.75f);
  const SceneWireframeOverlayBatchPlan plan =
      plan_scene_wireframe_overlay_batches(snapshot, 64u);
  EXPECT_EQ(plan.participant_count, 1u);
  EXPECT_EQ(plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(plan.batches.chunks.size(), 1u);

  ASSERT_TRUE(document.set_layer_visible(layer, false));
  EXPECT_TRUE(build_render_overlay_scene_wireframe_snapshot(document, assets,
                                                            1u)
                  .meshes.empty());
  EXPECT_TRUE(build_render_scene_snapshot(document, assets)
                  .instance_primitives.empty());
  EXPECT_FALSE(entity_world_bounds(document, assets, occurrence).has_value());
}

TEST(RenderSceneWireframeBatchPlannerTests,
     LocalVisibilityFiltersFormalInstancesByPublicOccurrence) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Local Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Local Definition Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));

  const EntityId isolated =
      document.create_entity("Local Occurrence", EntityKind::Instance);
  const EntityId other =
      document.create_entity("Other Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      isolated, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(
      other, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(isolated, true));
  ASSERT_TRUE(document.set_selected(other, true));

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 2> baseline{isolated,
                                                               other};
  const std::array<ViewportLocalVisibilityEntityId, 1> members{isolated};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  const ViewportLocalVisibilitySnapshot local =
      workspace.local_visibility_snapshot();

  const RenderSceneSnapshot scene = build_render_scene_snapshot(
      document, assets, {.local_visibility = &local});
  ASSERT_EQ(scene.instance_primitives.size(), 1u);
  ASSERT_EQ(scene.instance_primitives.front().occurrences.size(), 1u);
  EXPECT_EQ(scene.instance_primitives.front().occurrences.front().entity_id,
            isolated);

  const RenderOverlaySceneWireframeSnapshot wireframe =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u, {},
                                                    &local);
  ASSERT_EQ(wireframe.meshes.size(), 1u);
  EXPECT_EQ(wireframe.meshes.front().entity, isolated);

  const RenderOverlaySelectedObjectSnapshot selection =
      build_render_overlay_selected_object_snapshot(document, assets, true,
                                                    {}, &local);
  ASSERT_EQ(selection.instance_selection_fills.size(), 1u);
  EXPECT_EQ(selection.instance_selection_fills.front().occurrence, isolated);
  ASSERT_EQ(selection.instance_selection_wires.size(), 1u);
  EXPECT_EQ(selection.instance_selection_wires.front().occurrence, isolated);

  const RenderOverlaySelectionSnapshot bounds =
      build_render_overlay_selection_snapshot(document, assets, {}, {},
                                              &local);
  ASSERT_EQ(bounds.selected_mesh_bounds.size(), 1u);
  const std::optional<SceneBounds> isolated_bounds =
      entity_world_bounds(document, assets, isolated);
  ASSERT_TRUE(isolated_bounds.has_value());
  expect_bounds_snapshot_near(bounds.selected_mesh_bounds.front(),
                              *isolated_bounds);
}

TEST(RenderInstanceEditPresentationTests,
     ContextEffectPreservesMaterialAppearanceAndSupportsZeroToFullStrength) {
  const ViewportSettingColor source{0.7f, 0.3f, 0.2f, 0.8f};
  const InstanceEditStyle defaults =
      InstanceEditStyle::from_settings(ViewportSettings::defaults());
  EXPECT_GT(defaults.resolve_context_color(source, true).a, source.a * 0.8f)
      << "the default context treatment should retain most surface opacity";

  ViewportSettings settings = ViewportSettings::defaults();
  const ViewportSettingColor target{0.2f, 0.8f, 0.4f, 2.0f / 255.0f};
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::InstanceEditContext,
                                target));
  InstanceEditStyle style = InstanceEditStyle::from_settings(settings);
  EXPECT_FLOAT_EQ(style.context.a, target.a);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_context_color(source, false), source));
  style.context_strength = 0.0f;
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_context_color(source, true), source));

  style.context_strength = 1.0f;
  style.context_tint_amount = 1.0f;
  const ViewportSettingColor fully_tinted =
      style.resolve_context_color(source, true);
  EXPECT_NEAR(fully_tinted.r, target.r, 0.00001f);
  EXPECT_NEAR(fully_tinted.g, target.g, 0.00001f);
  EXPECT_NEAR(fully_tinted.b, target.b, 0.00001f);
  EXPECT_FLOAT_EQ(fully_tinted.a, source.a * target.a);
  EXPECT_LT(fully_tinted.a, 0.05f)
      << "saved low-alpha colors must not encounter a hidden opacity floor";

  style.context_tint_amount = 0.0f;
  style.context_desaturation = 0.0f;
  const ViewportSettingColor opacity_only =
      style.resolve_context_color(source, true);
  EXPECT_NEAR(opacity_only.r, source.r, 0.00001f);
  EXPECT_NEAR(opacity_only.g, source.g, 0.00001f);
  EXPECT_NEAR(opacity_only.b, source.b, 0.00001f);
  EXPECT_FLOAT_EQ(opacity_only.a, source.a * target.a);

  style.context_desaturation = 1.0f;
  const float gray = linear_to_srgb(srgb_to_linear(source.r) * 0.2126f +
                                    srgb_to_linear(source.g) * 0.7152f +
                                    srgb_to_linear(source.b) * 0.0722f);
  const ViewportSettingColor desaturated =
      style.resolve_context_color(source, true);
  EXPECT_NEAR(desaturated.r, gray, 0.00001f);
  EXPECT_NEAR(desaturated.g, gray, 0.00001f);
  EXPECT_NEAR(desaturated.b, gray, 0.00001f);

  style.context_strength = 0.25f;
  style.context_tint_amount = 1.0f;
  const ViewportSettingColor partial =
      style.resolve_context_color(source, true);
  EXPECT_NEAR(partial.r,
              linear_to_srgb(std::lerp(srgb_to_linear(source.r),
                                       srgb_to_linear(target.r), 0.25f)),
              0.00001f);
  EXPECT_FLOAT_EQ(partial.a, source.a * std::lerp(1.0f, target.a, 0.25f));
  ViewportSettingColor transparent_source = source;
  transparent_source.a = 0.0f;
  EXPECT_FLOAT_EQ(style.resolve_context_color(transparent_source, true).a,
                  0.0f);
  style.context_strength = 1.0f;
  style.context.a = 0.0f;
  EXPECT_FLOAT_EQ(style.resolve_context_color(source, true).a, 0.0f);
}

TEST(RenderInstanceEditPresentationTests,
     BackgroundTintHonorsStrengthAndAlphaAndRestoresOriginalOnExit) {
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::InstanceEditBackground,
                                {0.8f, 0.6f, 0.4f, 0.5f}));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::InstanceEditBackgroundBlend, 0.4f));
  InstanceEditStyle style = InstanceEditStyle::from_settings(settings);
  const ViewportSettingColor source{0.1f, 0.2f, 0.3f, 0.6f};
  const ViewportSettingColor blended =
      style.resolve_background_color(source, true);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      blended, {0.24f, 0.28f, 0.32f, source.a}));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_background_color(source, false), source));
  style.background_blend = 0.0f;
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_background_color(source, true), source));
  style.background_blend = 1.0f;
  style.background.a = 0.0f;
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_background_color(source, true), source));
  style.background.a = 1.0f;
  EXPECT_TRUE(viewport_setting_colors_approximately(
      style.resolve_background_color(source, true),
      {style.background.r, style.background.g, style.background.b, source.a}));
}

TEST(RenderInstanceEditPresentationTests,
     GridFadePreservesGridAndAxisColorsAndRestoresTheirOriginalOpacity) {
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::InstanceEditGridFade,
                                      0.35f));
  const InstanceEditStyle style = InstanceEditStyle::from_settings(settings);
  for (const auto id : {ViewportColorSettingId::GridMinor,
                        ViewportColorSettingId::GridMajor,
                        ViewportColorSettingId::GridXAxis,
                        ViewportColorSettingId::GridYAxis,
                        ViewportColorSettingId::GridZAxis}) {
    SCOPED_TRACE(static_cast<int>(id));
    const ViewportSettingColor original = settings.color(id);
    const ViewportSettingColor faded = style.resolve_grid_color(original, true);
    EXPECT_FLOAT_EQ(faded.r, original.r);
    EXPECT_FLOAT_EQ(faded.g, original.g);
    EXPECT_FLOAT_EQ(faded.b, original.b);
    EXPECT_FLOAT_EQ(faded.a, original.a * 0.65f);
    EXPECT_TRUE(viewport_setting_colors_approximately(
        style.resolve_grid_color(original, false), original));
    InstanceEditStyle unmodified = style;
    unmodified.grid_fade = 0.0f;
    EXPECT_TRUE(viewport_setting_colors_approximately(
        unmodified.resolve_grid_color(original, true), original));
    unmodified.grid_fade = 1.0f;
    EXPECT_FLOAT_EQ(unmodified.resolve_grid_color(original, true).a, 0.0f);
  }
}

TEST(RenderInstanceEditPresentationTests,
     ClassifiesActiveOccurrenceAndDimsEveryOtherParticipant) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));

  const EntityId active =
      document.create_entity("Active Occurrence", EntityKind::Instance);
  const EntityId sibling =
      document.create_entity("Sibling Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      active, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(
      sibling, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(active, true));

  const RenderInstanceEditScope scope{
      .occurrence = active,
      .definition = definition,
  };
  const RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.instance_edit_scope = scope});

  ASSERT_EQ(snapshot.instance_edit_scope, scope);
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  const RenderSceneInstancePrimitiveRecord &primitive =
      snapshot.instance_primitives.front();
  EXPECT_EQ(primitive.key.presentation_flags, 0u)
      << "selection remains overlay-only and never contaminates the base key";
  ASSERT_EQ(primitive.occurrences.size(), 2u);
  const auto active_record = std::ranges::find_if(
      primitive.occurrences,
      [active](const auto &occurrence) {
        return occurrence.entity_id == active;
      });
  const auto sibling_record = std::ranges::find_if(
      primitive.occurrences,
      [sibling](const auto &occurrence) {
        return occurrence.entity_id == sibling;
      });
  ASSERT_NE(active_record, primitive.occurrences.end());
  ASSERT_NE(sibling_record, primitive.occurrences.end());
  EXPECT_TRUE(active_record->selected);
  EXPECT_FALSE(active_record->dimmed);
  EXPECT_FALSE(sibling_record->selected);
  EXPECT_TRUE(sibling_record->dimmed);

  ASSERT_TRUE(document.set_selected(active, false));
  const EntityId direct =
      document.create_entity("Direct Context Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(direct, MeshComponent{.mesh = mesh}));
  ViewportSettings settings = ViewportSettings::defaults();
  const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(
      settings);
  RenderOverlaySnapshotCache cache;
  const auto initial = cache.update_persistent(
      document, assets, false, 1u, style, {}, nullptr, false, scope);
  ASSERT_NE(initial.scene_wireframe, nullptr);
  const RenderOverlaySceneWireframeSnapshot wireframe = *initial.scene_wireframe;
  ASSERT_EQ(wireframe.meshes.size(), 3u);
  EXPECT_EQ(wireframe.instance_edit_scope, scope);
  for (const auto &record : wireframe.meshes) {
    EXPECT_EQ(is_instance_edit_context(scope, record.occurrence, record.selected),
              record.entity != active);
    EXPECT_FALSE(is_instance_edit_context(scope, record.occurrence, true));
  }
  const auto original_plan = plan_scene_wireframe_overlay_batches(wireframe, 64u);
  EXPECT_EQ(original_plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(original_plan.batches.chunks.size(), 2u)
      << "active and context participants share geometry but have separate uniforms";

  RenderOverlaySceneWireframeSnapshot normal = wireframe;
  normal.instance_edit_scope = {};
  const auto normal_frame = ViewportSceneOverlayBuilder::build_wireframe_frame(
      {}, RenderOverlaySelectionMode::Component, normal);
  const auto edit_frame = ViewportSceneOverlayBuilder::build_wireframe_frame(
      {}, RenderOverlaySelectionMode::Component, wireframe);
  ASSERT_EQ(edit_frame.lines.size(), normal_frame.lines.size());
  std::size_t line_index = 0u;
  for (const auto &record : wireframe.meshes) {
    for (std::size_t segment = 0u; segment < record.wire_segments.size();
         ++segment, ++line_index) {
      ASSERT_LT(line_index, edit_frame.lines.size());
      if (record.entity == active) {
        EXPECT_EQ(edit_frame.lines[line_index].color,
                  normal_frame.lines[line_index].color);
      } else {
        EXPECT_NE(edit_frame.lines[line_index].color,
                  normal_frame.lines[line_index].color);
      }
    }
  }
  EXPECT_EQ(line_index, edit_frame.lines.size());

  const auto *retained_meshes = initial.scene_wireframe->meshes.data();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::InstanceEditContextStrength, 0.0f));
  const auto disabled_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
  const auto disabled = cache.update_persistent(
      document, assets, false, 1u, disabled_style, {}, nullptr, false, scope);
  EXPECT_FALSE(disabled.selected_object_changed);
  ASSERT_TRUE(disabled.scene_wireframe_changed);
  ASSERT_NE(disabled.scene_wireframe, nullptr);
  EXPECT_EQ(disabled.scene_wireframe->meshes.data(), retained_meshes);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  const auto disabled_plan =
      plan_scene_wireframe_overlay_batches(*disabled.scene_wireframe, 64u);
  EXPECT_EQ(disabled_plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(disabled_plan.batches.chunks.size(), original_plan.batches.chunks.size());
  for (std::size_t index = 0u; index < original_plan.batches.chunks.size(); ++index) {
    EXPECT_EQ(disabled_plan.batches.chunks[index].key,
              original_plan.batches.chunks[index].key)
        << "appearance strength must not alter batch or geometry identity";
  }
  const auto disabled_frame = ViewportSceneOverlayBuilder::build_wireframe_frame(
      {}, RenderOverlaySelectionMode::Component, *disabled.scene_wireframe);
  ASSERT_EQ(disabled_frame.lines.size(), normal_frame.lines.size());
  for (std::size_t index = 0u; index < normal_frame.lines.size(); ++index) {
    EXPECT_EQ(disabled_frame.lines[index].color, normal_frame.lines[index].color);
  }

  const RenderInstanceEditScope sibling_scope{.occurrence = sibling,
                                              .definition = definition};
  const auto switched = cache.update_persistent(
      document, assets, false, 1u, style, {}, nullptr, false, sibling_scope);
  EXPECT_FALSE(switched.selected_object_changed);
  EXPECT_TRUE(switched.scene_wireframe_changed);
  ASSERT_NE(switched.scene_wireframe, nullptr);
  EXPECT_EQ(switched.scene_wireframe->instance_edit_scope, sibling_scope);
  EXPECT_EQ(switched.scene_wireframe->meshes.data(), retained_meshes);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, 1u);
  EXPECT_FALSE(cache.update_persistent(document, assets, false, 1u, style, {},
                                      nullptr, false, sibling_scope)
                   .changed());
}

TEST(RenderInstanceEditPresentationTests,
     EmitsSelectedOccurrenceFillBeforeWireWithExactRenderPolicy) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  document.set_transform(
      member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});

  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_selected(occurrence, true));

  const RenderOverlaySelectedObjectSnapshot overlay =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  EXPECT_TRUE(overlay.meshes.empty());
  ASSERT_EQ(overlay.instance_selection_fills.size(), 1u);
  ASSERT_EQ(overlay.instance_selection_wires.size(), 1u);
  const RenderOverlayInstanceSelectionFillRecord &fill =
      overlay.instance_selection_fills.front();
  const RenderOverlayInstanceSelectionWireRecord &wire =
      overlay.instance_selection_wires.front();
  EXPECT_EQ(fill.semantic, RenderOverlaySemantic::InstanceSelectionFill);
  EXPECT_EQ(fill.occurrence, occurrence);
  EXPECT_EQ(fill.definition, definition);
  EXPECT_EQ(fill.member, member);
  ASSERT_NE(fill.geometry_payload, nullptr);
  EXPECT_NE(fill.geometry_content_id.value, 0u);
  EXPECT_FALSE(fill.vertices.empty());
  EXPECT_GE(fill.indices.size(), 3u);
  EXPECT_FLOAT_EQ(fill.world_transform.values[12], 12.0f);
  EXPECT_EQ(wire.semantic, RenderOverlaySemantic::InstanceSelectionWire);
  EXPECT_EQ(wire.occurrence, occurrence);
  EXPECT_EQ(wire.definition, definition);
  EXPECT_EQ(wire.member, member);
  ASSERT_NE(wire.geometry_payload, nullptr);
  ASSERT_NE(wire.wire_geometry_payload, nullptr);
  EXPECT_EQ(wire.geometry_content_id, fill.geometry_content_id);
  EXPECT_FALSE(wire.wire_segments.empty());
  EXPECT_FLOAT_EQ(wire.world_transform.values[12], 12.0f);
  EXPECT_EQ(wire.wire_color, kOverlaySelectionWireColor);
  EXPECT_FLOAT_EQ(wire.wire_width_pixels,
                  kOverlaySelectionWireLineWidthPixels);
}

TEST(RenderInstanceEditPresentationTests,
     UnselectedOccurrencesEmitNoFillAndCompatibleSelectionsBatchTogether) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));

  std::array<EntityId, 3u> occurrences{};
  for (std::size_t index = 0u; index < occurrences.size(); ++index) {
    occurrences[index] = document.create_entity(
        "Occurrence " + std::to_string(index + 1u), EntityKind::Instance);
    ASSERT_TRUE(document.set_instance_occurrence(
        occurrences[index],
        InstanceOccurrenceComponent{.definition = definition}));
    document.set_transform(
        occurrences[index],
        TransformComponent{
            .position = {static_cast<float>(index) * 10.0f, 0.0f, 0.0f},
        });
  }

  const RenderOverlaySelectedObjectSnapshot unselected =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  EXPECT_TRUE(unselected.instance_selection_fills.empty());
  EXPECT_TRUE(unselected.instance_selection_wires.empty());

  ASSERT_TRUE(document.set_selected(occurrences[0], true));
  ASSERT_TRUE(document.set_selected(occurrences[2], true));
  const RenderSceneSnapshot selected_base =
      build_render_scene_snapshot(document, assets);
  ASSERT_EQ(selected_base.instance_primitives.size(), 1u);
  ASSERT_EQ(selected_base.instance_primitives.front().occurrences.size(), 3u)
      << "selection overlays must not split the compatible base batch";

  const RenderOverlaySelectedObjectSnapshot selected =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(selected.instance_selection_fills.size(), 2u);
  ASSERT_EQ(selected.instance_selection_wires.size(), 2u);
  EXPECT_TRUE(std::ranges::none_of(
      selected.instance_selection_fills,
      [&](const RenderOverlayInstanceSelectionFillRecord &fill) {
        return fill.occurrence == occurrences[1];
      }));
  EXPECT_TRUE(std::ranges::none_of(
      selected.instance_selection_wires,
      [&](const RenderOverlayInstanceSelectionWireRecord &wire) {
        return wire.occurrence == occurrences[1];
      }));
  EXPECT_EQ(selected.instance_selection_fills[0].geometry_content_id,
            selected.instance_selection_fills[1].geometry_content_id);
  EXPECT_EQ(selected.instance_selection_wires[0].geometry_content_id,
            selected.instance_selection_wires[1].geometry_content_id);
  EXPECT_TRUE(std::ranges::all_of(
      selected.instance_selection_wires,
      [](const RenderOverlayInstanceSelectionWireRecord &wire) {
        return wire.wire_color == kOverlaySelectionWireColor;
      }));

  const InstanceSelectionOverlayBatchPlans batches =
      plan_instance_selection_overlay_batches(selected, 64u);
  ASSERT_EQ(batches.fills.chunks.size(), 1u);
  ASSERT_EQ(batches.wires.chunks.size(), 1u);
  EXPECT_EQ(batches.fills.chunks.front().occurrences.size(), 2u);
  EXPECT_EQ(batches.wires.chunks.front().occurrences.size(), 2u);
  EXPECT_EQ(batches.fills.occurrence_count, 2u);
  EXPECT_EQ(batches.wires.occurrence_count, 2u);
}

TEST(RenderInstanceEditPresentationTests,
     SelectedInstanceWidthKeepsFillAndWireBatchIdentity) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(occurrence, true));

  RenderOverlaySelectedObjectSnapshot baseline =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(baseline.instance_selection_wires.size(), 1u);
  ASSERT_NE(baseline.instance_selection_wires.front().wire_geometry_payload,
            nullptr);
  constexpr float kLogicalWidth = 0.257f;
  constexpr float kBaselineRasterScale = 1.5f;
  baseline.style.raster_scale = kBaselineRasterScale;
  baseline.style.selection_edge_logical_width_pixels = kLogicalWidth;
  baseline.style.selection_edge_line_width_pixels =
      kLogicalWidth * kBaselineRasterScale;
  baseline.instance_selection_wires.front().logical_wire_width_pixels =
      kLogicalWidth;
  baseline.instance_selection_wires.front().wire_width_pixels =
      kLogicalWidth * kBaselineRasterScale;
  const InstanceSelectionOverlayBatchPlans baseline_plans =
      plan_instance_selection_overlay_batches(baseline, 64u);

  RenderOverlaySelectedObjectSnapshot scaled = baseline;
  constexpr float kScaledRasterScale = 3.9750001f;
  scaled.style.raster_scale = kScaledRasterScale;
  scaled.style.selection_edge_line_width_pixels =
      kLogicalWidth * kScaledRasterScale;
  ++scaled.style.wire_signature;
  for (RenderOverlayInstanceSelectionWireRecord &wire :
       scaled.instance_selection_wires) {
    wire.wire_width_pixels =
        wire.logical_wire_width_pixels * kScaledRasterScale;
  }
  const InstanceSelectionOverlayBatchPlans scaled_plans =
      plan_instance_selection_overlay_batches(scaled, 64u);
  EXPECT_EQ(classify_instance_batch_update(baseline_plans.fills,
                                           scaled_plans.fills),
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(classify_instance_batch_update(baseline_plans.wires,
                                           scaled_plans.wires),
            RenderInstanceBatchUpdateKind::Unchanged)
      << "a scale-only width update must retain both persistent proxy sets";

  RenderOverlaySelectedObjectSnapshot scaled_and_moved = scaled;
  scaled_and_moved.instance_selection_fills.front()
      .world_transform.values[12] += 5.0f;
  scaled_and_moved.instance_selection_wires.front()
      .world_transform.values[12] += 5.0f;
  const InstanceSelectionOverlayBatchPlans scaled_and_moved_plans =
      plan_instance_selection_overlay_batches(scaled_and_moved, 64u);
  EXPECT_EQ(classify_instance_batch_update(scaled_plans.fills,
                                           scaled_and_moved_plans.fills),
            RenderInstanceBatchUpdateKind::TransformsOnly);
  EXPECT_EQ(classify_instance_batch_update(scaled_plans.wires,
                                           scaled_and_moved_plans.wires),
            RenderInstanceBatchUpdateKind::TransformsOnly)
      << "a transform concurrent with scale must still use retained proxies";

  RenderOverlaySelectedObjectSnapshot mixed_widths = scaled;
  RenderOverlayInstanceSelectionWireRecord second_width =
      mixed_widths.instance_selection_wires.front();
  second_width.occurrence += 1u;
  second_width.logical_wire_width_pixels += 0.125f;
  second_width.wire_width_pixels =
      second_width.logical_wire_width_pixels * kScaledRasterScale;
  mixed_widths.instance_selection_wires.push_back(std::move(second_width));
  const InstanceSelectionOverlayBatchPlans mixed_width_plans =
      plan_instance_selection_overlay_batches(mixed_widths, 64u);
  EXPECT_EQ(mixed_width_plans.wires.chunks.size(), 2u)
      << "distinct logical widths must still receive distinct materials";

  RenderOverlaySelectedObjectSnapshot recolored = scaled;
  recolored.instance_selection_wires.front().wire_color ^= 0x00010101u;
  const InstanceSelectionOverlayBatchPlans recolored_plans =
      plan_instance_selection_overlay_batches(recolored, 64u);
  EXPECT_EQ(classify_instance_batch_update(scaled_plans.wires,
                                           recolored_plans.wires),
            RenderInstanceBatchUpdateKind::Rebuild)
      << "wire color is baked into derived geometry";

  RenderOverlaySelectedObjectSnapshot changed_geometry = scaled;
  auto replacement_wire_geometry = std::make_shared<MeshWireGeometryPayload>(
      *changed_geometry.instance_selection_wires.front()
           .wire_geometry_payload);
  ++replacement_wire_geometry->content_id;
  changed_geometry.instance_selection_wires.front().wire_geometry_payload =
      replacement_wire_geometry;
  changed_geometry.instance_selection_wires.front().wire_segments =
      replacement_wire_geometry->segments;
  const InstanceSelectionOverlayBatchPlans changed_geometry_plans =
      plan_instance_selection_overlay_batches(changed_geometry, 64u);
  EXPECT_EQ(classify_instance_batch_update(scaled_plans.wires,
                                           changed_geometry_plans.wires),
            RenderInstanceBatchUpdateKind::Rebuild);
}

TEST(RenderInstanceEditPresentationTests,
     SharedGeometryPrepareWorkIsLinearAcrossFillAndWireAtDoubleCount) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(occurrence, true));

  const RenderOverlaySelectedObjectSnapshot seed =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(seed.instance_selection_fills.size(), 1u);
  ASSERT_EQ(seed.instance_selection_wires.size(), 1u);
  ASSERT_NE(seed.instance_selection_fills.front().geometry_payload, nullptr);
  const std::size_t shared_vertex_count =
      seed.instance_selection_fills.front().vertices.size();

  const auto exercise = [&](std::size_t selected_member_count) {
    RenderOverlaySelectedObjectSnapshot snapshot = seed;
    const RenderOverlayInstanceSelectionFillRecord fill_template =
        seed.instance_selection_fills.front();
    const RenderOverlayInstanceSelectionWireRecord wire_template =
        seed.instance_selection_wires.front();
    snapshot.instance_selection_fills.clear();
    snapshot.instance_selection_wires.clear();
    snapshot.instance_selection_fills.reserve(selected_member_count);
    snapshot.instance_selection_wires.reserve(selected_member_count);
    for (std::size_t index = 0u; index < selected_member_count; ++index) {
      RenderOverlayInstanceSelectionFillRecord fill = fill_template;
      RenderOverlayInstanceSelectionWireRecord wire = wire_template;
      fill.occurrence = static_cast<EntityId>(index + 1u);
      wire.occurrence = fill.occurrence;
      fill.world_transform.values[12] = static_cast<float>(index);
      wire.world_transform = fill.world_transform;
      snapshot.instance_selection_fills.push_back(std::move(fill));
      snapshot.instance_selection_wires.push_back(std::move(wire));
    }

    const InstanceSelectionOverlayBatchPlans plans =
        plan_instance_selection_overlay_batches(snapshot, 1u);
    EXPECT_EQ(plans.fills.chunks.size(), selected_member_count);
    EXPECT_EQ(plans.wires.chunks.size(), selected_member_count);
    EXPECT_EQ(plans.work.fill_record_index_entry_count, 1u);
    EXPECT_EQ(plans.work.wire_record_index_entry_count, 1u);
    EXPECT_EQ(plans.work.fill_chunk_record_lookup_count,
              selected_member_count);
    EXPECT_EQ(plans.work.wire_chunk_record_lookup_count,
              selected_member_count);
    EXPECT_EQ(plans.work.local_bounds_cache_hit_count,
              selected_member_count * 2u - 1u);
    EXPECT_EQ(plans.work.local_bounds_vertex_visit_count,
              shared_vertex_count);
    return plans.work;
  };

  const InstanceSelectionOverlayPlanWorkStats work_n = exercise(64u);
  const InstanceSelectionOverlayPlanWorkStats work_2n = exercise(128u);
  EXPECT_EQ(work_2n.fill_chunk_record_lookup_count,
            work_n.fill_chunk_record_lookup_count * 2u);
  EXPECT_EQ(work_2n.wire_chunk_record_lookup_count,
            work_n.wire_chunk_record_lookup_count * 2u);
  EXPECT_EQ(work_2n.local_bounds_vertex_visit_count,
            work_n.local_bounds_vertex_visit_count);
}

TEST(RenderInstanceEditPresentationTests,
     SelectedInstanceWireUsesAuthoredBoundariesWithoutTriangleDiagonal) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member = document.create_entity("Quad", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  ASSERT_TRUE(document.set_selected(occurrence, true));

  const RenderOverlaySelectedObjectSnapshot overlay =
      build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(overlay.instance_selection_wires.size(), 1u);
  const RenderOverlayInstanceSelectionWireRecord &wire =
      overlay.instance_selection_wires.front();
  ASSERT_TRUE(wire.has_authored_polygon_geometry());
  ASSERT_EQ(wire.indices.size(), 6u)
      << "the shaded fill remains compiled as two triangles";

  const std::vector<InstanceSelectionWireEdge> edges =
      instance_selection_wire_edges(wire);
  ASSERT_EQ(edges.size(), 4u)
      << "only the authored quad boundary belongs in the selection wire";
  const auto is_point = [](QVec3 value, float x, float z) {
    return value.x == x && value.y == 0.0f && value.z == z;
  };
  const bool contains_compiled_diagonal = std::ranges::any_of(
      edges, [&](const InstanceSelectionWireEdge &edge) {
        return (is_point(edge.start, -1.0f, -1.0f) &&
                is_point(edge.end, 1.0f, 1.0f)) ||
               (is_point(edge.start, 1.0f, 1.0f) &&
                is_point(edge.end, -1.0f, -1.0f));
      });
  EXPECT_FALSE(contains_compiled_diagonal);
}

TEST(RenderInstanceEditPresentationTests,
     TransformOnlyMoveRefreshesPersistentSelectedFillAndWire) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());

  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = mesh});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  document.set_transform(
      member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});

  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_selected(occurrence, true));

  RenderOverlaySnapshotCache cache;
  const RenderOverlayPersistentSnapshotUpdate initial =
      cache.update_persistent(document, assets, true, 0u, {});
  ASSERT_TRUE(initial.selected_object_changed);
  ASSERT_NE(initial.selected_object, nullptr);
  ASSERT_EQ(initial.selected_object->instance_selection_fills.size(), 1u);
  ASSERT_EQ(initial.selected_object->instance_selection_wires.size(), 1u);
  const MeshGeometryContentId content_id =
      initial.selected_object->instance_selection_fills.front()
          .geometry_content_id;
  EXPECT_FLOAT_EQ(initial.selected_object->instance_selection_fills.front()
                      .world_transform.values[12],
                  12.0f);
  EXPECT_FLOAT_EQ(initial.selected_object->instance_selection_wires.front()
                      .world_transform.values[12],
                  12.0f);

  document.set_transform(
      occurrence, TransformComponent{.position = {30.0f, 0.0f, 0.0f}});
  const RenderOverlayPersistentSnapshotUpdate moved =
      cache.update_persistent(document, assets, true, 0u, {});
  ASSERT_TRUE(moved.selected_object_changed)
      << "selected instance overlays must not freeze during gizmo drags";
  ASSERT_NE(moved.selected_object, nullptr);
  ASSERT_EQ(moved.selected_object->instance_selection_fills.size(), 1u);
  ASSERT_EQ(moved.selected_object->instance_selection_wires.size(), 1u);
  EXPECT_EQ(moved.selected_object->instance_selection_fills.front()
                .geometry_content_id,
            content_id);
  EXPECT_EQ(moved.selected_object->instance_selection_wires.front()
                .geometry_content_id,
            content_id);
  EXPECT_FLOAT_EQ(moved.selected_object->instance_selection_fills.front()
                      .world_transform.values[12],
                  32.0f);
  EXPECT_FLOAT_EQ(moved.selected_object->instance_selection_wires.front()
                      .world_transform.values[12],
                  32.0f);
  EXPECT_EQ(cache.stats().selected_object_build_count, 2u);

  const RenderOverlayPersistentSnapshotUpdate stable =
      cache.update_persistent(document, assets, true, 0u, {});
  EXPECT_FALSE(stable.selected_object_changed);
  EXPECT_EQ(cache.stats().selected_object_build_count, 2u);
}

TEST(RenderInstanceEditPresentationTests,
     ActiveCanonicalMemberOverlayProjectsThroughOccurrenceTransform) {
  SceneDocument document;
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  document.set_transform(
      definition, TransformComponent{.position = {100.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = 1u});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  document.set_transform(
      member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});

  constexpr int kObjectId = 77;
  ModelingObjectRuntimeMap object_map;
  (void)object_map.bind(kObjectId, member);
  quader_editor_core::SelectionOverlayFrame runtime_frame;
  runtime_frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::SourceWire,
      .start = {0.0f, 0.0f, 0.0f},
      .end = {1.0f, 0.0f, 0.0f},
      .object_id = kObjectId,
  });

  const ViewportOverlayFrame outside_edit =
      SelectionOverlayProjection::project_selection_overlay_frame(
          runtime_frame, object_map, document,
          quader_editor_core::SelectionMode::Edge);
  EXPECT_TRUE(outside_edit.lines.empty())
      << "hidden canonical definition geometry is not directly rendered";

  const ViewportOverlayFrame in_edit =
      SelectionOverlayProjection::project_selection_overlay_frame(
          runtime_frame, object_map, document,
          quader_editor_core::SelectionMode::Edge,
          ModelingInstanceEditScope{
              .occurrence = occurrence,
              .definition = definition,
          });
  ASSERT_EQ(in_edit.lines.size(), 1u);
  EXPECT_FLOAT_EQ(in_edit.lines.front().start.x, 12.0f);
  EXPECT_FLOAT_EQ(in_edit.lines.front().end.x, 13.0f);
  EXPECT_EQ(in_edit.lines.front().source,
            ViewportOverlaySource::ComponentSelection);
}

TEST(RenderInstanceEditPresentationTests,
     HoverEdgeDimensionContextProjectsThroughTheObjectWorldTransform) {
  SceneDocument document;
  const EntityId entity =
      document.create_entity("Dimension Context Mesh", EntityKind::Mesh);
  document.set_transform(
      entity, TransformComponent{.position = {10.0F, 20.0F, 30.0F}});
  constexpr int kObjectId = 77;
  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(kObjectId, entity));

  quader_editor_core::SelectionOverlayFrame runtime_frame;
  runtime_frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::HoverEdge,
      .start = {1.0F, 2.0F, 3.0F},
      .end = {3.0F, 2.0F, 3.0F},
      .object_id = kObjectId,
      .face_id = 9,
      .edge = quader_poly::make_edge(4, 5),
  });
  runtime_frame.hover_edge_dimension_context =
      quader_editor_core::SelectionHoverEdgeDimensionContext{
          .inside_reference = {2.0F, 2.5F, 3.0F},
          .object_id = kObjectId,
          .face_id = 9,
          .edge = quader_poly::make_edge(4, 5),
      };

  const ViewportOverlayFrame projected =
      SelectionOverlayProjection::project_selection_overlay_frame(
          runtime_frame, object_map, document,
          quader_editor_core::SelectionMode::Edge);

  ASSERT_TRUE(projected.hover_edge_dimension_context.has_value());
  const ViewportHoverEdgeDimensionContext &context =
      *projected.hover_edge_dimension_context;
  EXPECT_FLOAT_EQ(context.inside_reference.x, 12.0F);
  EXPECT_FLOAT_EQ(context.inside_reference.y, 22.5F);
  EXPECT_FLOAT_EQ(context.inside_reference.z, 33.0F);
  EXPECT_EQ(context.element.object_id, kObjectId);
  EXPECT_EQ(context.element.edge_a, 4);
  EXPECT_EQ(context.element.edge_b, 5);
  EXPECT_EQ(context.element.face_id, 9);
}

TEST(RenderSceneExtractionScaleTests,
     StableCompatibilityGroupsKeepFirstSeenAndOccurrenceOrder) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  ASSERT_NE(mesh, kInvalidAssetId);

  std::array<AssetId, 3u> materials{};
  const std::array<QVec3, 3u> colors{{
      {0.8f, 0.1f, 0.1f},
      {0.1f, 0.8f, 0.1f},
      {0.1f, 0.1f, 0.8f},
  }};
  for (std::size_t index = 0u; index < materials.size(); ++index) {
    MaterialDocument material =
        make_default_quader_material("Grouped Material");
    material.base_color = colors[index];
    materials[index] = assets.add_material(std::move(material));
    ASSERT_NE(materials[index], kInvalidAssetId);
  }

  constexpr std::array<std::size_t, 6u> kMaterialOrder{
      0u, 1u, 0u, 2u, 1u, 2u};
  std::array<EntityId, kMaterialOrder.size()> entities{};
  for (std::size_t index = 0u; index < kMaterialOrder.size(); ++index) {
    entities[index] = document.create_entity(
        "Stable Group " + std::to_string(index), EntityKind::Mesh);
    ASSERT_TRUE(
        document.set_mesh(entities[index], MeshComponent{.mesh = mesh}));
    ASSERT_TRUE(document.set_material(
        entities[index],
        MaterialComponent{.material = materials[kMaterialOrder[index]]}));
  }

  const RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets);

  EXPECT_TRUE(snapshot.meshes.empty());
  ASSERT_EQ(snapshot.instance_primitives.size(), materials.size());
  constexpr std::array<std::array<std::size_t, 2u>, 3u> kExpectedMembers{{
      {{0u, 2u}},
      {{1u, 4u}},
      {{3u, 5u}},
  }};
  for (std::size_t group = 0u; group < materials.size(); ++group) {
    const RenderSceneInstancePrimitiveRecord &record =
        snapshot.instance_primitives[group];
    EXPECT_EQ(record.material.component.material, materials[group]);
    ASSERT_EQ(record.occurrences.size(), 2u);
    EXPECT_EQ(record.occurrences[0].entity_id,
              entities[kExpectedMembers[group][0]]);
    EXPECT_EQ(record.occurrences[1].entity_id,
              entities[kExpectedMembers[group][1]]);
  }
}

TEST(RenderSceneExtractionScaleTests,
     KeepsOneThousandTwentyFourUniqueMeshesInEntityOrder) {
  constexpr std::size_t kMeshCount = 1'024u;
  SceneDocument document;
  AssetRegistry assets;
  std::vector<EntityId> entities;
  entities.reserve(kMeshCount);

  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    MeshAsset unique_mesh = make_overlay_triangle_mesh();
    unique_mesh.edit_geometry().vertices.front().position.x -=
        static_cast<float>(index) * 0.001f;
    const AssetId mesh = assets.add_mesh(std::move(unique_mesh));
    ASSERT_NE(mesh, kInvalidAssetId);
    const EntityId entity = document.create_entity(
        "Unique Mesh " + std::to_string(index), EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
    entities.push_back(entity);
  }

  const RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets);

  EXPECT_TRUE(snapshot.instance_primitives.empty());
  ASSERT_EQ(snapshot.meshes.size(), kMeshCount);
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    EXPECT_EQ(snapshot.meshes[index].entity_id, entities[index]);
  }
}

TEST(RenderSceneExtractionScaleTests,
     KeepsSharedGeometryWithUniqueMaterialsInEntityOrder) {
  constexpr std::size_t kMaterialCount = 1'024u;
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  ASSERT_NE(mesh, kInvalidAssetId);
  std::vector<EntityId> entities;
  std::vector<AssetId> materials;
  entities.reserve(kMaterialCount);
  materials.reserve(kMaterialCount);

  for (std::size_t index = 0u; index < kMaterialCount; ++index) {
    MaterialDocument material =
        make_default_quader_material("Unique Render Material");
    material.base_color.x =
        static_cast<float>(index + 1u) /
        static_cast<float>(kMaterialCount + 1u);
    const AssetId material_id = assets.add_material(std::move(material));
    ASSERT_NE(material_id, kInvalidAssetId);
    const EntityId entity = document.create_entity(
        "Unique Material Mesh " + std::to_string(index),
        EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
    ASSERT_TRUE(document.set_material(
        entity, MaterialComponent{.material = material_id}));
    entities.push_back(entity);
    materials.push_back(material_id);
  }

  const RenderSceneSnapshot snapshot =
      build_render_scene_snapshot(document, assets);

  EXPECT_TRUE(snapshot.instance_primitives.empty());
  ASSERT_EQ(snapshot.meshes.size(), kMaterialCount);
  for (std::size_t index = 0u; index < kMaterialCount; ++index) {
    EXPECT_EQ(snapshot.meshes[index].entity_id, entities[index]);
    EXPECT_EQ(snapshot.meshes[index].material.component.material,
              materials[index]);
  }
}

TEST(RenderSceneExtractionScaleTests,
     PreviewMaterialBucketsReuseEveryExactComponentInALargeSet) {
  /// Counts project-material resolutions while exercising preview caching.
  class CountingProjectMaterialResolver final
      : public RenderProjectMaterialResolver {
  public:
    [[nodiscard]] std::uint64_t revision() const noexcept override {
      return revision_value;
    }

    [[nodiscard]] std::optional<RenderProjectMaterialSnapshot>
    resolve(ProjectAssetId id) const override {
      ++resolve_count;
      return snapshot.has_value() &&
                     snapshot->evaluation.project_material_id == id
                 ? snapshot
                 : std::nullopt;
    }

    std::uint64_t revision_value = 1u;
    mutable std::size_t resolve_count = 0u;
    std::optional<RenderProjectMaterialSnapshot> snapshot;
  };

  constexpr std::size_t kUniqueMaterialCount = 512u;
  const ProjectAssetId project_material =
      *ProjectAssetId::parse("77777777-7777-4777-8777-777777777777");
  CountingProjectMaterialResolver resolver;
  resolver.snapshot = RenderProjectMaterialSnapshot{
      .evaluation =
          RenderMaterialEvaluationSnapshot{
              .project_material_id = project_material,
              .kind = RenderMaterialEvaluationKind::Standard,
              .identity_revision = 3u,
              .content_revision = 5u,
              .payload = RenderStandardMaterialEvaluationSnapshot{},
          },
      .catalog_revision = 7u,
  };

  AssetRegistry assets;
  std::vector<MaterialComponent> components;
  std::vector<ToolPreviewMesh> previews;
  components.reserve(kUniqueMaterialCount);
  previews.reserve(kUniqueMaterialCount * 2u);
  for (std::size_t index = 0u; index < kUniqueMaterialCount; ++index) {
    MaterialDocument slot_material =
        make_default_quader_material("Preview Slot");
    slot_material.base_color.y =
        static_cast<float>(index + 1u) /
        static_cast<float>(kUniqueMaterialCount + 1u);
    const AssetId slot = assets.add_material(std::move(slot_material));
    ASSERT_NE(slot, kInvalidAssetId);
    components.push_back({
        .material = MaterialReference::project(project_material),
        .material_slots = {{3u, slot}},
    });
    previews.push_back({
        .source_entity = static_cast<EntityId>(index + 1u),
        .material = components.back(),
    });
  }
  for (std::size_t index = kUniqueMaterialCount; index > 0u; --index) {
    previews.push_back({
        .source_entity =
            static_cast<EntityId>(kUniqueMaterialCount + index),
        .material = components[index - 1u],
    });
  }

  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          std::move(previews), assets, nullptr, &resolver);

  ASSERT_EQ(records.size(), kUniqueMaterialCount * 2u);
  EXPECT_EQ(resolver.resolve_count, kUniqueMaterialCount)
      << "each exact material component is expanded only once";
  for (std::size_t index = 0u; index < kUniqueMaterialCount; ++index) {
    EXPECT_EQ(records[index].material.component, components[index]);
    EXPECT_EQ(records[kUniqueMaterialCount + index].material.component,
              components[kUniqueMaterialCount - index - 1u]);
  }
}
