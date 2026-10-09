////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "assets/default_material_catalog.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "assets/texture_asset.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/color_space.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
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
#include "render/overlay/overlay_face_fill_composition.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_bounds.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/primitive_creation_controls.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
/// Makes native fixture mode transitions explicit before changing selection kind.
quader::QuaderApiVoidResult enter_selection_context(
    quader::QuaderEditorApi &editor, quader::QuaderSelectionMode kind) {
  const bool component = kind == quader::QuaderSelectionMode::Vertex ||
      kind == quader::QuaderSelectionMode::Edge ||
      kind == quader::QuaderSelectionMode::Face;
  const auto transition = editor.mode().set(component
      ? quader::QuaderEditorMode::Edit : quader::QuaderEditorMode::Object);
  if (!transition.ok()) return transition;
  return editor.selection().set_mode(kind);
}


/// Receives the real coordinator streams without requiring a GPU backend.
class DimensionExtractionTarget final : public ViewportOverlaySubmissionTarget {
public:
  [[nodiscard]] ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame,
                                 ViewportOverlayCamera) override {}
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame,
                              ViewportOverlayCamera) override {}
};

/// Uses the same modeling publication consumed by the native viewport.
class DimensionModelingFrameProvider final
    : public ViewportOverlaySelectionFrameProvider {
public:
  explicit DimensionModelingFrameProvider(const ModelingSessionService &modeling)
      : modeling_(modeling) {}

  [[nodiscard]] ViewportOverlayFrame
  build_selection_overlay_frame() const override {
    return modeling_.selection_overlay_frame();
  }

private:
  const ModelingSessionService &modeling_;
};

void expect_dimension_extents(const ViewportOverlaySubmissionResult &result,
                              QVec3 min, QVec3 max) {
  ASSERT_TRUE(result.selection_dimension_bounds.has_value());
  const auto &bounds = *result.selection_dimension_bounds;
  EXPECT_NEAR(bounds.min.x, min.x, 0.0001f);
  EXPECT_NEAR(bounds.min.y, min.y, 0.0001f);
  EXPECT_NEAR(bounds.min.z, min.z, 0.0001f);
  EXPECT_NEAR(bounds.max.x, max.x, 0.0001f);
  EXPECT_NEAR(bounds.max.y, max.y, 0.0001f);
  EXPECT_NEAR(bounds.max.z, max.z, 0.0001f);
}

/// Keeps extraction snapshots alive for the duration of each native submission.
ViewportOverlaySubmissionResult extract_selection_dimensions(
    const SceneDocument &scene, const AssetRegistry &assets,
    RenderOverlaySelectionMode mode,
    ViewportOverlaySubmissionCoordinator &coordinator,
    DimensionExtractionTarget &target) {
  const auto selection = build_render_overlay_selection_snapshot(scene, assets);
  const auto objects =
      build_render_overlay_selected_object_snapshot(scene, assets, true);
  return coordinator.submit(
      {.scene_overlay_snapshot = &selection,
       .selected_mesh_outline_snapshot = &objects,
       .scene_selection_mode = mode,
       .overlays_visible = true,
       .scene_overlay_requested = true},
      ViewportSettings::defaults(), target);
}

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

TEST(RenderSignatureBuilderTests,
     LineOcclusionStyleInvalidatesItsLayerWithoutCameraPoseOrGeometryChanges) {
  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::HoverEdge,
      .start = {-0.5f, 0.0f, -1.0f},
      .end = {0.5f, 0.0f, -4.0f},
      .source = ViewportOverlaySource::Tool,
  });
  frame.points.push_back({.layer = ViewportOverlayLayer::ToolHandle});
  const auto ordinary = OverlayRenderableBuilder::frame_signatures(frame);
  frame.lines.front().occlusion_style = ViewportOverlayLineOcclusionStyle::Dashed;
  const auto dashed = OverlayRenderableBuilder::frame_signatures(frame);
  const auto repeated = OverlayRenderableBuilder::frame_signatures(frame);
  EXPECT_NE(dashed.frame_signature, ordinary.frame_signature);
  EXPECT_EQ(dashed.frame_signature, repeated.frame_signature);
  for (std::size_t index = 0u; index < kViewportOverlayLayerCount; ++index) {
    if (index == static_cast<std::size_t>(ViewportOverlayLayer::HoverEdge)) {
      EXPECT_NE(dashed.layer_signatures[index], ordinary.layer_signatures[index]);
    } else {
      EXPECT_EQ(dashed.layer_signatures[index], ordinary.layer_signatures[index]);
    }
  }
  frame.lines.front().occluded_dash_period_pixels = 16.0f;
  const auto scaled = OverlayRenderableBuilder::frame_signatures(frame);
  EXPECT_NE(scaled.frame_signature, dashed.frame_signature);
  EXPECT_NE(OverlayRenderableBuilder::layer_signature(scaled, ViewportOverlayLayer::HoverEdge),
            OverlayRenderableBuilder::layer_signature(dashed, ViewportOverlayLayer::HoverEdge));
  EXPECT_EQ(OverlayRenderableBuilder::layer_signature(scaled, ViewportOverlayLayer::ToolHandle),
            OverlayRenderableBuilder::layer_signature(dashed, ViewportOverlayLayer::ToolHandle));

  auto camera = test_perspective_overlay_camera();
  const auto signature = OverlayRenderableBuilder::layer_build_signature(
      dashed, camera, ViewportOverlayLayer::HoverEdge);
  camera.eye = {3.0f, -2.0f, 1.0f};
  camera.forward = {-0.2f, 0.1f, -1.0f};
  EXPECT_EQ(signature, OverlayRenderableBuilder::layer_build_signature(
                           dashed, camera, ViewportOverlayLayer::HoverEdge));
  camera.near_plane *= 2.0f;
  EXPECT_NE(signature, OverlayRenderableBuilder::layer_build_signature(
                           dashed, camera, ViewportOverlayLayer::HoverEdge));
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
  EXPECT_FALSE(renderable_entry->double_sided());
  EXPECT_TRUE(renderable_entry->mesh_component.double_sided);
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
  ASSERT_TRUE(document.set_selected(curve, true));
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
      mesh, {.location = {3.0f, 4.0f, 5.0f}}));
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
      other_mesh, MeshComponent{.mesh = mesh, .cast_shadows = false}));
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
      entity, TransformComponent{.location = {3.0f, 2.0f, 1.0f}});
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
     CanonicalProjectMaterialIdentityControlsBindingsAndRejectsMismatchedDescriptors) {
  /// Models a host's tombstone mapping independently of project persistence.
  class CanonicalMaterialResolver final : public RenderProjectMaterialResolver {
  public:
    std::uint64_t revision() const noexcept override { return 7u; }
    ProjectAssetId canonical_material_id(ProjectAssetId id) const override {
      return remap_enabled && id == deleted_id ? default_id : id;
    }
    std::optional<RenderProjectMaterialSnapshot>
    resolve(ProjectAssetId) const override {
      ++resolve_count;
      return snapshot;
    }
    ProjectAssetId deleted_id;
    ProjectAssetId default_id;
    RenderProjectMaterialSnapshot snapshot;
    bool remap_enabled = true;
    mutable std::size_t resolve_count = 0u;
  } resolver;
  resolver.deleted_id =
      *ProjectAssetId::parse("11441144-1144-4144-8144-114411441144");
  resolver.default_id =
      *ProjectAssetId::parse("1d8d1fd2-2953-4cb4-a66b-1f879259b50d");
  resolver.snapshot = RenderProjectMaterialSnapshot{
      .evaluation = RenderMaterialEvaluationSnapshot{
          .project_material_id = resolver.default_id,
          .kind = RenderMaterialEvaluationKind::Standard,
          .identity_revision = 11u,
          .content_revision = 13u,
          .payload = RenderStandardMaterialEvaluationSnapshot{
              .base_color = {0.25f, 0.5f, 0.75f},
          },
      },
      .catalog_revision = 7u,
  };
  const MaterialReference deleted = MaterialReference::project(resolver.deleted_id);
  const MaterialReference fallback = MaterialReference::project(resolver.default_id);
  const MaterialComponent old_component{
      .material = deleted, .material_slots = {{1u, deleted}}};
  const MaterialComponent default_component{
      .material = fallback, .material_slots = {{1u, fallback}}};
  AssetRegistry assets;
  const auto mapped = build_render_material_binding_snapshot(
      assets, &old_component, &resolver);
  const auto explicit_default = build_render_material_binding_snapshot(
      assets, &default_component, &resolver);
  ASSERT_TRUE(mapped.primary_material.present);
  EXPECT_EQ(mapped.primary_material.reference, fallback);
  EXPECT_EQ(mapped.component, default_component);
  ASSERT_NE(mapped.material_for_slot(1u), nullptr);
  EXPECT_EQ(mapped.material_for_slot(1u)->reference, fallback);
  EXPECT_EQ(mapped.revision, explicit_default.revision);
  EXPECT_FLOAT_EQ(mapped.primary_material.document.base_color.x, 0.25f);
  EXPECT_EQ(old_component.material, deleted);

  SceneDocument document;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId restored = document.create_entity("Restored material", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(restored, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_material(restored, old_component));
  const auto single = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  ASSERT_EQ(single.meshes.size(), 1u);
  EXPECT_EQ(single.meshes.front().material_reference, fallback);
  EXPECT_EQ(document.material(restored)->material, deleted);

  const EntityId current = document.create_entity("Default material", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(current, MeshComponent{.mesh = mesh}));
  ASSERT_TRUE(document.set_material(current, default_component));
  const auto grouped = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  EXPECT_TRUE(grouped.meshes.empty());
  ASSERT_EQ(grouped.instance_primitives.size(), 1u);
  EXPECT_EQ(grouped.instance_primitives.front().occurrences.size(), 2u);
  const std::size_t resolved_before = resolver.resolve_count;
  const auto unchanged = build_render_scene_snapshot(
      document, assets, {.previous = &grouped, .project_material_resolver = &resolver});
  EXPECT_EQ(resolver.resolve_count, resolved_before);
  EXPECT_EQ(unchanged.instance_primitives.size(), grouped.instance_primitives.size());

  const auto previews = build_render_tool_preview_snapshot(
      {ToolPreviewMesh{.material = old_component, .mesh = make_overlay_triangle_mesh()}},
      assets, nullptr, &resolver);
  ASSERT_EQ(previews.size(), 1u);
  EXPECT_EQ(previews.front().material_reference, fallback);
  EXPECT_EQ(previews.front().material.revision, explicit_default.revision);

  resolver.remap_enabled = false;
  EXPECT_FALSE(build_render_material_binding_snapshot(
      assets, &old_component, &resolver).primary_material.present);
  resolver.remap_enabled = true;
  resolver.snapshot.evaluation.project_material_id =
      *ProjectAssetId::parse("77557755-7755-4755-8755-775577557755");
  EXPECT_FALSE(build_render_material_binding_snapshot(
      assets, &old_component, &resolver).primary_material.present);
}

TEST(RenderSceneSnapshotTests,
     MaterialBindingsSurviveOrdinaryInstanceBatchMembershipChanges) {
  SceneDocument document;
  AssetRegistry assets;
  const ProjectAssetId material_id =
      *ProjectAssetId::parse("55555555-5555-4555-8555-555555555555");
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
  const MaterialComponent material{
      .material = MaterialReference::project(material_id)};
  std::vector<EntityId> entities;
  std::vector<AssetId> meshes;
  constexpr std::size_t kObjectCount = 200u;
  for (std::size_t index = 0u; index < kObjectCount; ++index) {
    const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
    const EntityId entity =
        document.create_entity("Shared binding", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(entity, {.mesh = mesh}));
    ASSERT_TRUE(document.set_material(entity, material));
    entities.push_back(entity);
    meshes.push_back(mesh);
  }

  const auto initial = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  EXPECT_TRUE(initial.meshes.empty());
  ASSERT_EQ(initial.instance_primitives.size(), 1u);
  EXPECT_EQ(initial.instance_primitives.front().occurrences.size(), kObjectCount);
  EXPECT_EQ(initial.extraction_work.material_binding_build_count, 1u);
  EXPECT_EQ(initial.extraction_work.material_binding_reuse_count,
            kObjectCount - 1u);
  EXPECT_EQ(resolver.resolve_count, 1u);

  ASSERT_TRUE(assets.replace_mesh(meshes.front(), make_authored_quad_mesh()));
  ASSERT_TRUE(document.notify_mesh_payload_changed(entities.front()));
  const auto split = build_render_scene_snapshot(
      document, assets,
      {.previous = &initial, .project_material_resolver = &resolver});
  ASSERT_EQ(split.meshes.size(), 1u);
  EXPECT_EQ(split.meshes.front().entity_id, entities.front());
  EXPECT_EQ(split.meshes.front().mesh.indices.size(), 6u);
  ASSERT_EQ(split.instance_primitives.size(), 1u);
  EXPECT_EQ(split.instance_primitives.front().occurrences.size(),
            kObjectCount - 1u);
  EXPECT_EQ(split.extraction_work.material_binding_build_count, 0u);
  EXPECT_EQ(split.extraction_work.material_binding_reuse_count, kObjectCount);
  EXPECT_EQ(resolver.resolve_count, 1u);
  EXPECT_EQ(initial.instance_primitives.front().mesh.indices.size(), 3u);

  ASSERT_TRUE(assets.replace_mesh(meshes.front(), make_overlay_triangle_mesh()));
  ASSERT_TRUE(document.notify_mesh_payload_changed(entities.front()));
  const auto rejoined = build_render_scene_snapshot(
      document, assets,
      {.previous = &split, .project_material_resolver = &resolver});
  EXPECT_TRUE(rejoined.meshes.empty());
  ASSERT_EQ(rejoined.instance_primitives.size(), 1u);
  EXPECT_EQ(rejoined.instance_primitives.front().occurrences.size(), kObjectCount);
  EXPECT_EQ(rejoined.extraction_work.material_binding_build_count, 0u);
  EXPECT_EQ(resolver.resolve_count, 1u);

  MaterialComponent slotted = material;
  slotted.material_slots.emplace(1u, material.material);
  ASSERT_TRUE(document.set_material(entities.front(), slotted));
  const auto changed_slot = build_render_scene_snapshot(
      document, assets,
      {.previous = &rejoined, .project_material_resolver = &resolver});
  EXPECT_EQ(changed_slot.extraction_work.material_binding_build_count, 1u);
  EXPECT_EQ(changed_slot.extraction_work.material_binding_reuse_count,
            kObjectCount - 1u);
  EXPECT_EQ(resolver.resolve_count, 3u);

  resolver.material->evaluation.content_revision += 1u;
  std::get<RenderStandardMaterialEvaluationSnapshot>(
      resolver.material->evaluation.payload).base_color = {0.2f, 0.4f, 0.6f};
  ++resolver.revision_value;
  const auto changed_project = build_render_scene_snapshot(
      document, assets,
      {.previous = &changed_slot, .project_material_resolver = &resolver});
  EXPECT_EQ(changed_project.extraction_work.material_binding_build_count, 2u);
  EXPECT_EQ(resolver.resolve_count, 6u);
  ASSERT_EQ(changed_project.instance_primitives.size(), 1u);
  EXPECT_FLOAT_EQ(changed_project.instance_primitives.front()
                      .material.primary_material.document.base_color.x,
                  0.2f);
}

TEST(RenderSceneSnapshotTests,
     MaterialBindingCacheTracksCatalogAndSceneBindingRevisions) {
  SceneDocument document;
  AssetRegistry assets;
  TextureAsset texture;
  texture.name = "Binding cache texture";
  texture.source_path = "binding-cache-first.png";
  const AssetId texture_id = assets.add_texture(texture);
  MaterialDocument material = make_default_quader_material("Binding cache");
  set_material_texture_slot(material, material_texture_slot::kBaseColor,
                            texture_id);
  const AssetId material_id = assets.add_material(material);
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId entity =
      document.create_entity("Binding cache", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, {.mesh = mesh}));
  ASSERT_TRUE(document.set_material(entity, {.material = material_id}));
  const RenderSceneBindingKey binding{.scene_session_id = 17u};
  const auto initial = build_render_scene_snapshot(
      document, assets, {.scene_binding = binding});
  ASSERT_EQ(initial.meshes.size(), 1u);
  EXPECT_EQ(initial.extraction_work.material_binding_build_count, 1u);

  ASSERT_TRUE(assets.update_texture(texture_id, [](TextureAsset &updated) {
    updated.source_path = "binding-cache-replaced.png";
  }));
  const auto changed_texture = build_render_scene_snapshot(
      document, assets, {.previous = &initial, .scene_binding = binding});
  ASSERT_EQ(changed_texture.meshes.size(), 1u);
  EXPECT_EQ(changed_texture.extraction_work.material_binding_build_count, 1u);
  const auto *resolved = changed_texture.meshes.front()
                             .material.primary_material.texture_for_slot(
                                 material_texture_slot::kBaseColor);
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->texture.source_path,
            std::filesystem::path("binding-cache-replaced.png"));
  EXPECT_NE(changed_texture.meshes.front().material.revision,
            initial.meshes.front().material.revision);

  ASSERT_TRUE(assets.update_material(material_id, [](MaterialDocument &updated) {
    updated.base_color = {0.25f, 0.5f, 0.75f};
  }));
  const auto changed_material = build_render_scene_snapshot(
      document, assets,
      {.previous = &changed_texture, .scene_binding = binding});
  ASSERT_EQ(changed_material.meshes.size(), 1u);
  EXPECT_EQ(changed_material.extraction_work.material_binding_build_count, 1u);
  EXPECT_FLOAT_EQ(changed_material.meshes.front()
                      .material.primary_material.document.base_color.x,
                  0.25f);
  const auto rebound = build_render_scene_snapshot(
      document, assets,
      {.previous = &changed_material,
       .scene_binding = {.scene_session_id = 18u}});
  EXPECT_EQ(rebound.extraction_work.material_binding_build_count, 1u);
  EXPECT_EQ(rebound.extraction_work.material_binding_reuse_count, 0u);
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
      entity, TransformComponent{.location = {1.0f, 2.0f, 3.0f}});
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      initial, document, assets, 2u, {}, resolver.revision_value));
}

TEST(RenderDefaultMaterialTests,
     ResolverTracksTextureChangesWithoutAnOpenProject) {
  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  ASSERT_FALSE(project.has_project());
  ASSERT_EQ(resolver.default_material_override(), nullptr);
  const std::uint64_t initial_revision = resolver.revision();
  resolver.set_default_material_texture({});
  EXPECT_EQ(resolver.revision(), initial_revision);

  const std::filesystem::path first_path =
      std::filesystem::temp_directory_path() / "quader-default-first.png";
  resolver.set_default_material_texture(first_path);
  ASSERT_NE(resolver.default_material_override(), nullptr);
  const RenderMaterialAssetSnapshot first =
      *resolver.default_material_override();
  const auto *first_texture =
      first.texture_for_slot(material_texture_slot::kBaseColor);
  ASSERT_NE(first_texture, nullptr);
  EXPECT_EQ(first_texture->texture.source_path, first_path);
  EXPECT_TRUE(first_texture->texture.source_path.is_absolute());
  EXPECT_EQ(first_texture->texture.usage, TextureUsage::BaseColor);
  const std::uint64_t first_revision = resolver.revision();
  EXPECT_GT(first_revision, initial_revision);

  const auto *const retained_material = resolver.default_material_override();
  resolver.set_default_material_texture(first_path);
  EXPECT_EQ(resolver.revision(), first_revision);
  EXPECT_EQ(resolver.default_material_override(), retained_material);
  EXPECT_EQ(resolver.default_material_override()->revision, first.revision);

  const std::filesystem::path second_path =
      first_path.parent_path() / "quader-default-second.png";
  resolver.set_default_material_texture(second_path);
  const std::uint64_t second_revision = resolver.revision();
  EXPECT_GT(second_revision, first_revision);
  ASSERT_NE(resolver.default_material_override(), nullptr);
  EXPECT_GT(resolver.default_material_override()->revision, first.revision);
  const auto *second_texture =
      resolver.default_material_override()->texture_for_slot(
          material_texture_slot::kBaseColor);
  ASSERT_NE(second_texture, nullptr);
  EXPECT_EQ(second_texture->texture.source_path, second_path);
  EXPECT_GT(second_texture->texture.revision.value,
            first_texture->texture.revision.value);
  EXPECT_EQ(first_texture->texture.source_path, first_path)
      << "published snapshots retain the prior texture until replaced";

  resolver.set_default_material_texture({});
  EXPECT_EQ(resolver.default_material_override(), nullptr);
  EXPECT_GT(resolver.revision(), second_revision);
  const std::uint64_t reset_revision = resolver.revision();
  resolver.set_default_material_texture({});
  EXPECT_EQ(resolver.revision(), reset_revision);
  const auto *packaged_texture =
      packaged_default_render_material().texture_for_slot(
          material_texture_slot::kBaseColor);
  ASSERT_NE(packaged_texture, nullptr);
  EXPECT_EQ(packaged_texture->texture.source_path,
            std::filesystem::path(kDefaultMaterialAlbedoTexturePackagePath));
  EXPECT_FALSE(project.has_project());
}

TEST(RenderDefaultMaterialTests,
     FullOverridesEveryMaterialSlotWithoutEditingAuthoredState) {
  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  SceneDocument document;
  AssetRegistry assets;
  MeshAsset mesh_asset = make_authored_quad_mesh();
  mesh_asset.edit_geometry().primitives = {
      {.index_offset = 0u, .index_count = 3u, .material_slot = 0u},
      {.index_offset = 3u, .index_count = 3u, .material_slot = 3u},
  };
  const AssetId mesh = assets.add_mesh(std::move(mesh_asset));
  MaterialDocument authored_material =
      make_default_quader_material("Authored Default Override Fixture");
  authored_material.base_color = {0.2f, 0.4f, 0.8f};
  const AssetId primary = assets.add_material(authored_material);
  const AssetId slot =
      assets.add_material(make_default_quader_material("Authored Slot"));
  const MaterialComponent component{
      .material = primary,
      .material_slots = {{3u, slot}},
  };
  const EntityId entity =
      document.create_entity("Existing Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, {.mesh = mesh}));
  ASSERT_TRUE(document.set_material(entity, component));
  const auto scene_revision = document.revision();
  const auto asset_revision = assets.revision();
  const RenderSceneSnapshot authored = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  ASSERT_EQ(authored.meshes.size(), 1u);
  EXPECT_EQ(authored.extraction_work.material_binding_build_count, 1u);
  const auto &authored_binding = authored.meshes.front().material;
  EXPECT_EQ(authored_binding.primary_material.material_id, primary);
  ASSERT_NE(authored_binding.material_for_slot(3u), nullptr);
  EXPECT_EQ(authored_binding.material_for_slot(3u)->material_id, slot);

  const std::filesystem::path texture_path =
      std::filesystem::temp_directory_path() / "quader-default-override.png";
  resolver.set_default_material_texture(texture_path);
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      authored, document, assets, {}, resolver.revision()));
  RenderSceneSnapshot stale = authored;
  EXPECT_FALSE(try_refresh_render_scene_snapshot_selection(
      stale, document, assets, 1u, {}, resolver.revision()));
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      stale, document, assets, 1u, {}, resolver.revision()));
  const RenderSceneSnapshot overridden = build_render_scene_snapshot(
      document, assets,
      {.previous = &authored, .project_material_resolver = &resolver});
  EXPECT_FALSE(overridden.reused_previous_records);
  EXPECT_EQ(overridden.extraction_work.material_binding_build_count, 1u);
  EXPECT_TRUE(overridden.dirty.project_materials_changed);
  ASSERT_EQ(overridden.meshes.size(), 1u);
  EXPECT_TRUE(overridden.meshes.front().material_dirty);
  EXPECT_FALSE(overridden.meshes.front().mesh_dirty);
  EXPECT_EQ(overridden.meshes.front().mesh.geometry_payload,
            authored.meshes.front().mesh.geometry_payload);
  const auto &binding = overridden.meshes.front().material;
  EXPECT_TRUE(binding.material_slots.empty());
  for (const auto slot_id : {0u, 3u, 99u}) {
    const auto *material = binding.material_for_slot(slot_id);
    ASSERT_NE(material, nullptr);
    const auto *texture =
        material->texture_for_slot(material_texture_slot::kBaseColor);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->texture.source_path, texture_path);
  }
  EXPECT_NE(binding.revision, authored_binding.revision);
  EXPECT_EQ(document.revision(), scene_revision);
  EXPECT_EQ(assets.revision(), asset_revision);
  ASSERT_NE(document.material(entity), nullptr);
  EXPECT_EQ(*document.material(entity), component);

  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      overridden, document, assets, {}, resolver.revision()));
  const RenderSceneSnapshot unchanged = build_render_scene_snapshot(
      document, assets,
      {.previous = &overridden, .project_material_resolver = &resolver});
  EXPECT_TRUE(unchanged.reused_previous_records);
  EXPECT_EQ(unchanged.extraction_work.material_binding_build_count, 0u);
  EXPECT_FALSE(unchanged.dirty.any_dirty());
  EXPECT_EQ(unchanged.meshes.front().material.revision, binding.revision);

  const std::filesystem::path replacement_path =
      texture_path.parent_path() / "quader-replacement-default.png";
  resolver.set_default_material_texture(replacement_path);
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      unchanged, document, assets, {}, resolver.revision()));
  const RenderSceneSnapshot replaced = build_render_scene_snapshot(
      document, assets,
      {.previous = &unchanged, .project_material_resolver = &resolver});
  EXPECT_TRUE(replaced.dirty.project_materials_changed);
  EXPECT_EQ(replaced.extraction_work.material_binding_build_count, 1u);
  ASSERT_EQ(replaced.meshes.size(), 1u);
  EXPECT_TRUE(replaced.meshes.front().material_dirty);
  EXPECT_NE(replaced.meshes.front().material.revision, binding.revision);
  const auto *replacement_texture =
      replaced.meshes.front().material.primary_material.texture_for_slot(
          material_texture_slot::kBaseColor);
  ASSERT_NE(replacement_texture, nullptr);
  EXPECT_EQ(replacement_texture->texture.source_path, replacement_path);
  EXPECT_EQ(replaced.meshes.front().mesh.geometry_payload,
            authored.meshes.front().mesh.geometry_payload);

  resolver.set_default_material_texture({});
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      replaced, document, assets, {}, resolver.revision()));
  const RenderSceneSnapshot restored = build_render_scene_snapshot(
      document, assets,
      {.previous = &replaced, .project_material_resolver = &resolver});
  ASSERT_EQ(restored.meshes.size(), 1u);
  EXPECT_EQ(restored.extraction_work.material_binding_build_count, 1u);
  EXPECT_TRUE(restored.meshes.front().material_dirty);
  EXPECT_EQ(restored.meshes.front().material.component, component);
  EXPECT_EQ(restored.meshes.front().material.revision, authored_binding.revision);
  EXPECT_EQ(document.revision(), scene_revision);
  EXPECT_EQ(assets.revision(), asset_revision);
}

TEST(RenderDefaultMaterialTests,
     ExistingInstancesNewMeshesAndToolPreviewsShareTheSelectedTexture) {
  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const AssetId material =
      assets.add_material(make_default_quader_material("Instance Material"));
  const MaterialComponent component{
      .material = material,
      .material_slots = {{3u, material}},
  };
  const EntityId definition =
      document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, {.mesh = mesh}));
  ASSERT_TRUE(document.set_material(member, component));
  ASSERT_TRUE(document.set_parent(member, definition, false));
  for (int index = 0; index < 2; ++index) {
    const EntityId occurrence =
        document.create_entity("Occurrence", EntityKind::Instance);
    ASSERT_TRUE(document.set_instance_occurrence(
        occurrence, {.definition = definition}));
  }
  const std::filesystem::path texture_path =
      std::filesystem::temp_directory_path() / "quader-shared-default.png";
  resolver.set_default_material_texture(texture_path);
  const EntityId created_after_change =
      document.create_entity("New Mesh", EntityKind::Mesh);
  const AssetId new_mesh = assets.add_mesh(make_authored_quad_mesh());
  ASSERT_TRUE(document.set_mesh(created_after_change, {.mesh = new_mesh}));
  const auto scene_revision = document.revision();
  const auto asset_revision = assets.revision();

  const auto expect_texture = [&](const RenderMaterialBindingSnapshot &binding) {
    for (const auto slot_id : {0u, 3u}) {
      const auto *resolved = binding.material_for_slot(slot_id);
      ASSERT_NE(resolved, nullptr);
      const auto *texture =
          resolved->texture_for_slot(material_texture_slot::kBaseColor);
      ASSERT_NE(texture, nullptr);
      EXPECT_EQ(texture->texture.source_path, texture_path);
    }
  };
  const RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.project_material_resolver = &resolver});
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  EXPECT_EQ(snapshot.meshes.front().entity_id, created_after_change);
  expect_texture(snapshot.meshes.front().material);
  ASSERT_EQ(snapshot.instance_primitives.size(), 1u);
  EXPECT_EQ(snapshot.instance_primitives.front().occurrences.size(), 2u);
  expect_texture(snapshot.instance_primitives.front().material);

  const auto previews = build_render_tool_preview_snapshot(
      {ToolPreviewMesh{.source_entity = member,
                       .material = component,
                       .mesh = make_overlay_triangle_mesh(),
                       .instance_transforms = {QMat4{}, QMat4{}}},
       ToolPreviewMesh{.mesh = make_authored_quad_mesh()}},
      assets, nullptr, &resolver);
  ASSERT_EQ(previews.size(), 2u);
  EXPECT_EQ(previews.front().instance_transforms.size(), 2u);
  for (const auto &preview : previews) {
    expect_texture(preview.material);
  }
  EXPECT_EQ(document.revision(), scene_revision);
  EXPECT_EQ(assets.revision(), asset_revision);
  EXPECT_EQ(*document.material(member), component);
  EXPECT_EQ(document.material(created_after_change), nullptr);
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

TEST(RenderSceneSnapshotTests, ToolPreviewSnapshotsUseGlobalCullingPolicy) {
  AssetRegistry assets;
  QMat4 mapping;
  mapping.values[12] = 3.0f;
  for (const bool culling_enabled : {true, false}) {
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
            assets, nullptr, nullptr, culling_enabled);

    ASSERT_EQ(records.size(), 3u);
    for (const auto &record : records) {
      EXPECT_EQ(record.double_sided, !culling_enabled);
    }
    EXPECT_EQ(records[0].sampling_from_local.values, mapping.values);
    ASSERT_EQ(records[1].instance_sampling_from_local.size(), 2u);
    EXPECT_EQ(records[1].instance_sampling_from_local[0].values, mapping.values);
    EXPECT_EQ(records[1].instance_sampling_from_local[1].values, QMat4{}.values);
  }
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
      visible, TransformComponent{.location = {0.0f, 0.0f, -3.0f}});

  const EntityId missing =
      document.create_entity("Missing Mesh", EntityKind::Mesh);
  document.set_mesh(missing, MeshComponent{.mesh = mesh + 1000u});
  document.set_transform(
      missing, TransformComponent{.location = {0.0f, 0.0f, -3.0f}});

  const EntityId hidden =
      document.create_entity("Hidden Mesh", EntityKind::Mesh);
  document.set_mesh(hidden, MeshComponent{.mesh = mesh});
  document.set_transform(hidden,
                         TransformComponent{.location = {0.0f, 0.0f, -3.0f}});
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
      isolated, TransformComponent{.location = {0.0f, 0.0f, -3.0f}}));
  ASSERT_TRUE(document.set_transform(
      other, TransformComponent{.location = {0.5f, 0.0f, -3.0f}}));

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
  ASSERT_TRUE(document.set_selected(group, true));
  const EntityId curve = document.create_entity("Moving Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, CurveComponent{.curve = asset}));
  ASSERT_TRUE(document.set_parent(curve, group, false));
  ASSERT_TRUE(document.set_transform(
      curve, TransformComponent{.location = {1.0f, 2.0f, 3.0f}}));
  RenderOverlaySnapshotCache cache;
  const auto &initial = cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.curves.size(), 1u);
  EXPECT_EQ(initial.curves.front().selection_root, group);
  ASSERT_TRUE(initial.curves.front().selection_dimension_bounds.has_value());
  const auto initial_bounds = *initial.curves.front().selection_dimension_bounds;
  EXPECT_FLOAT_EQ(initial_bounds.min.x, 1.0f);
  EXPECT_FLOAT_EQ(initial_bounds.max.x, 5.0f);
  EXPECT_FLOAT_EQ(initial_bounds.min.y, 2.0f);
  EXPECT_FLOAT_EQ(initial_bounds.max.y, 3.5f);
  EXPECT_FLOAT_EQ(initial_bounds.min.z, 3.0f);
  const auto samples = initial.curves.front().sample_payload;
  const auto *records = initial.curves.data();
  ASSERT_NE(samples, nullptr);
  ASSERT_FALSE(samples->samples.empty());
  EXPECT_EQ(cache.selection_snapshot(document, assets).curves.data(), records);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);

  ASSERT_TRUE(document.set_transform(
      group, TransformComponent{.location = {5.0f, -2.0f, -7.0f}}));
  const auto &moved = cache.selection_snapshot(document, assets);
  ASSERT_EQ(moved.curves.size(), 1u);
  EXPECT_EQ(moved.curves.data(), records);
  EXPECT_EQ(moved.curves.front().sample_payload, samples);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_transform_refresh_count, 1u);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[12], 6.0f);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[13], 0.0f);
  EXPECT_FLOAT_EQ(moved.curves.front().world_transform.values[14], -4.0f);
  ASSERT_TRUE(moved.curves.front().selection_dimension_bounds.has_value());
  const auto moved_bounds = *moved.curves.front().selection_dimension_bounds;
  EXPECT_FLOAT_EQ(moved_bounds.min.x, 6.0f);
  EXPECT_FLOAT_EQ(moved_bounds.max.x, 10.0f);
  EXPECT_FLOAT_EQ(moved_bounds.min.y, 0.0f);
  EXPECT_FLOAT_EQ(moved_bounds.max.y, 1.5f);
  EXPECT_FLOAT_EQ(moved_bounds.min.z, -4.0f);
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

TEST(RenderSceneExtractionTests,
     CurvePreviewDimensionsIncludeExactCubicExtentsAndEverySolidifiedPlacement) {
  AssetRegistry assets;
  const AssetId curve = assets.add_curve(make_overlay_curve_asset());
  QMat4 first;
  first.values[12] = 10.0f;
  QMat4 second;
  second.values[0] = 2.0f;
  second.values[5] = 3.0f;
  second.values[10] = 4.0f;
  second.values[12] = -3.0f;
  second.values[13] = 5.0f;
  second.values[14] = 2.0f;
  ToolPreviewScene preview;
  preview.curves.push_back({
      .source_entity = 7u,
      .curve = curve,
      .instance_transforms = {first, second},
      .solidify = true,
      .solidify_radius = 0.25f,
      .suppress_source = true,
  });
  const auto snapshot =
      build_render_tool_preview_scene_snapshot(std::move(preview), assets);
  ASSERT_TRUE(snapshot.complete);
  ASSERT_EQ(snapshot.curves.size(), 1u);
  ASSERT_TRUE(snapshot.curves.front().selection_dimension_bounds.has_value());
  const auto bounds = *snapshot.curves.front().selection_dimension_bounds;
  EXPECT_FLOAT_EQ(bounds.min.x, -3.5f);
  EXPECT_FLOAT_EQ(bounds.max.x, 14.25f);
  EXPECT_FLOAT_EQ(bounds.min.y, -0.25f);
  EXPECT_FLOAT_EQ(bounds.max.y, 10.25f);
  EXPECT_FLOAT_EQ(bounds.min.z, -0.25f);
  EXPECT_FLOAT_EQ(bounds.max.z, 3.0f);
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
  ASSERT_TRUE(document.set_selected(curve, true));
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
  ASSERT_TRUE(initial.curves.front().selection_dimension_bounds.has_value());
  const float initial_curve_max_y =
      initial.curves.front().selection_dimension_bounds->max.y;

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
  ASSERT_TRUE(refreshed.curves.front().selection_dimension_bounds.has_value());
  EXPECT_EQ(refreshed.curves.front().selection_root, curve);
  EXPECT_GT(refreshed.curves.front().selection_dimension_bounds->max.y,
            initial_curve_max_y);
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
      direct_mesh, TransformComponent{.location = {3.0f, 0.0f, 0.0f}}));
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

TEST(RenderOverlaySnapshotCacheTests,
     CurvePublicationRefreshesSelectedGroupDimensionsWithoutReplacingStorage) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_overlay_curve_asset());
  const EntityId group = document.create_entity("Curve Group", EntityKind::Empty);
  const EntityId curve = document.create_entity("Curve Child", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(curve, {.curve = curve_asset}));
  ASSERT_TRUE(document.set_parent(curve, group, false));
  ASSERT_TRUE(document.set_selected(group, true));
  const EntityId other_group =
      document.create_entity("Unchanged Group", EntityKind::Empty);
  const EntityId mesh = document.create_entity("Unchanged Mesh", EntityKind::Mesh);
  const AssetId mesh_asset = assets.add_mesh(make_overlay_triangle_mesh());
  ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_asset}));
  ASSERT_TRUE(document.set_parent(mesh, other_group, false));
  ASSERT_TRUE(document.set_selected(other_group, true));

  RenderOverlaySnapshotCache cache;
  ViewportOverlaySubmissionCoordinator coordinator;
  DimensionExtractionTarget target;
  const auto submit = [&]() {
    const auto &snapshot = cache.selection_snapshot(document, assets);
    return coordinator.submit(
        {.scene_overlay_snapshot = &snapshot,
         .scene_selection_mode = RenderOverlaySelectionMode::Group,
         .overlays_visible = true,
         .scene_overlay_requested = true},
        ViewportSettings::defaults(), target);
  };
  const auto initial_submission = submit();
  expect_dimension_extents(initial_submission, {-0.5f, 0.0f, 0.0f},
                           {4.0f, 1.5f, 1.0f});
  const auto &initial = cache.selection_snapshot(document, assets);
  ASSERT_EQ(initial.group_bounds.size(), 2u);
  const auto *group_storage = initial.group_bounds.data();
  const auto *wire_storage = initial.hierarchy_selection_wires.data();
  const auto old_group_record = std::ranges::find_if(initial.group_bounds,
      [other_group](const auto &bounds) { return bounds.entity == other_group; });
  ASSERT_NE(old_group_record, initial.group_bounds.end());
  const auto old_group = *old_group_record;

  CurveAsset replacement = *assets.curve(curve_asset);
  modeling::CurveDocument edited = *replacement.document;
  ASSERT_TRUE(edited.transform(
      modeling::Transform3{.origin = {10.0f, 3.0f, 0.0f}}).ok());
  replacement.document =
      std::make_shared<const modeling::CurveDocument>(std::move(edited));
  ASSERT_TRUE(assets.replace_curve(curve_asset, std::move(replacement)));
  ASSERT_TRUE(document.notify_curve_payload_changed(curve));

  const auto refreshed_submission = submit();
  expect_dimension_extents(refreshed_submission, {-0.5f, 0.0f, 0.0f},
                           {14.0f, 4.5f, 1.0f});
  EXPECT_NE(refreshed_submission.selection_dimension_signature,
            initial_submission.selection_dimension_signature);
  const auto &refreshed = cache.selection_snapshot(document, assets);
  EXPECT_EQ(refreshed.group_bounds.data(), group_storage);
  EXPECT_EQ(refreshed.hierarchy_selection_wires.data(), wire_storage);
  const auto unchanged = std::ranges::find_if(refreshed.group_bounds,
      [other_group](const auto &bounds) { return bounds.entity == other_group; });
  ASSERT_NE(unchanged, refreshed.group_bounds.end());
  EXPECT_FLOAT_EQ(unchanged->min.x, old_group.min.x);
  EXPECT_FLOAT_EQ(unchanged->max.x, old_group.max.x);
  EXPECT_FLOAT_EQ(unchanged->max.z, old_group.max.z);
  EXPECT_EQ(cache.stats().selection_build_count, 1u);
  EXPECT_EQ(cache.stats().selection_curve_refresh_count, 1u);
  EXPECT_EQ(submit().selection_dimension_signature,
            refreshed_submission.selection_dimension_signature);
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

TEST(CurveToolOverlayCacheTests, ActiveAnchorAndHandleUseConfiguredColorsAndKeepMarkerSize) {
  const CurveAsset asset = make_overlay_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveToolPreviewState preview;
  preview.active = true;
  preview.document = *asset.document;
  const CurveControlTarget anchor{preview.document->points().front().id, CurveControlKind::Anchor};
  const CurveControlTarget handle{preview.document->points().back().id, CurveControlKind::IncomingHandle};
  preview.control_selection.controls = {handle};
  preview.control_selection.active = handle;
  CurveToolOverlayCache cache;
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectedVertex,
      {40.0f / 255.0f, 80.0f / 255.0f, 120.0f / 255.0f, 0.4f}));
  auto frame = ViewportOverlayStyleResolver::resolve(cache.frame(preview), settings);
  ASSERT_EQ(frame.points.size(), 4u);
  EXPECT_EQ(frame.points[0].color, kOverlayVertexColor);
  EXPECT_EQ(frame.points[3].color, rgba8(40, 80, 120, 102));
  EXPECT_EQ(frame.points[3].style_role, ViewportOverlayStyleRole::SelectedVertex);
  EXPECT_EQ(frame.points[3].size_pixels, kOverlayDrawnPlanePointSizePixels);
  const auto builds = cache.build_count();
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), builds);
  preview.control_selection.controls = {anchor, handle};
  ++preview.revision;
  frame = ViewportOverlayStyleResolver::resolve(cache.frame(preview), settings);
  EXPECT_EQ(frame.points[0].color, rgba8(40, 80, 120, 102));
  EXPECT_EQ(frame.points[3].color, viewport_active_selection_color(rgba8(40, 80, 120, 102)));
  EXPECT_EQ(frame.points[3].style_role, ViewportOverlayStyleRole::ActiveSelectedVertex);
  EXPECT_EQ(frame.points[3].size_pixels, kOverlayDrawnPlanePointSizePixels);
  EXPECT_EQ(cache.build_count(), builds + 1u);
  preview.control_selection.active = anchor;
  ++preview.revision;
  frame = ViewportOverlayStyleResolver::resolve(cache.frame(preview), settings);
  EXPECT_EQ(frame.points[0].style_role, ViewportOverlayStyleRole::ActiveSelectedVertex);
  EXPECT_EQ(frame.points[3].style_role, ViewportOverlayStyleRole::SelectedVertex);
  EXPECT_EQ(cache.build_count(), builds + 2u);
  preview.control_selection.controls = {anchor};
  ++preview.revision;
  frame = ViewportOverlayStyleResolver::resolve(cache.frame(preview), settings);
  EXPECT_EQ(frame.points[0].style_role, ViewportOverlayStyleRole::SelectedVertex);
  EXPECT_EQ(frame.points[0].color, rgba8(40, 80, 120, 102));
  EXPECT_EQ(frame.points[0].size_pixels, kOverlayDrawnPlanePointSizePixels);
  EXPECT_TRUE(std::ranges::none_of(frame.points, [](const auto &point) {
    return point.style_role == ViewportOverlayStyleRole::ActiveSelectedVertex;
  }));
  EXPECT_EQ(cache.build_count(), builds + 3u);
  (void)cache.frame(preview);
  EXPECT_EQ(cache.build_count(), builds + 3u);
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
      point_light, TransformComponent{.location = {2.0f, 3.0f, 4.0f}});
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
      .location = {4.0f, -2.0f, 1.0f}, .scale = {3.0f, 2.0f, 4.0f}});
  const EntityId entity = document.create_entity("Point", EntityKind::Light);
  document.set_light(entity, LightComponent{.type = LightType::Point, .range = 5.0f});
  document.set_transform(entity, TransformComponent{
      .location = {2.0f, 1.0f, -1.0f}, .scale = {2.0f, 3.0f, 0.5f}});
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
      .location = {1.0f, 4.0f, -2.0f}, .scale = {5.0f, 3.0f, 2.0f}});
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

  ASSERT_TRUE(document.set_selectable(parent, false));
  const auto &locked = cache.selection_snapshot(document, assets);
  const auto locked_frame = ViewportSceneOverlayBuilder::build_point_light_radius_frame(
      locked.point_light_gizmos, camera);
  EXPECT_EQ(locked_frame.lines.size(), 120u);
  EXPECT_TRUE(locked_frame.triangles.empty());
  ASSERT_TRUE(document.set_selectable(parent, true));
  ASSERT_TRUE(document.set_selectability(parent, false));
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
  SceneSettings scene_settings;
  scene_settings.camera.near_plane = 0.01f;
  scene_settings.camera.far_plane = 250.0f;
  ASSERT_TRUE(document.set_scene_settings(scene_settings));
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
  EXPECT_EQ(document.primary_selection(), hidden_selected_mesh);
  EXPECT_FALSE(selected_snapshot.meshes.front().active_selected)
      << "A hidden active entity does not promote another selected entity.";
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
  ASSERT_TRUE(object_map.bind(kModelingObjectId, entity));
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
  ASSERT_NE(document.selectable(entity), nullptr);
  EXPECT_TRUE(document.selectable(entity)->selectable)
      << "layer locking must not rewrite the local lock component";
  EXPECT_FALSE(document.is_effectively_selectable(entity));
  EXPECT_TRUE(build_render_scene_snapshot(document, assets)
                  .live_mesh_entities.contains(entity))
      << "locking affects interaction, not rendering";
  EXPECT_TRUE(PickingCacheProjection::scene_transformed_objects_for_picking(
                  modeling_objects, document, object_map, {},
                  ModelingSelectionMode::Object, {})
                  .objects.empty());

  ASSERT_TRUE(document.set_layer_locked(layer, false));
  EXPECT_TRUE(document.is_effectively_selectable(entity));
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
      entity, TransformComponent{.location = {3.0f, 2.0f, 1.0f}});
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
      TransformComponent{.location = {2.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(definition_member, definition, false));
  const EntityId occurrence =
      document.create_entity("Moving Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 0.0f, 0.0f}});
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
              .location = {4.0f, -2.0f, 1.0f},
              .rotation_euler = {0.2f, 0.4f, -0.1f},
          },
      },
      {
          .entity = selected_mesh,
          .transform = TransformComponent{
              .location = {-3.0f, 5.0f, 2.0f},
              .rotation_euler = {-0.3f, 0.1f, 0.6f},
              .scale = {1.5f, 0.75f, 2.0f},
          },
      },
      {
          .entity = directional_light,
          .transform = TransformComponent{.location = {7.0f, 8.0f, 9.0f}},
      },
      {
          .entity = point_light,
          .transform = TransformComponent{.location = {-7.0f, 3.0f, 6.0f}},
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
      selected, TransformComponent{.location = {1.0f, 2.0f, 3.0f}});
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
          .location = {3.0f, -2.0f, 5.0f},
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
          .location = {8.0f, 1.0f, -4.0f},
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
          .location = {1.0f, 2.0f, 3.0f},
          .rotation_euler = {0.2f, -0.3f, 0.4f},
          .scale = {1.5f, 0.8f, 1.2f},
      });
  document.set_transform(
      child, TransformComponent{.location = {2.0f, -1.0f, 0.5f}});
  ASSERT_TRUE(document.set_selected(child, true));

  RenderOverlaySnapshotCache cache;
  (void)cache.selection_snapshot(document, assets);
  EXPECT_EQ(cache.stats().selection_world_bounds_build_count, 1u);

  document.set_transform(
      parent,
      TransformComponent{
          .location = {6.0f, -4.0f, 8.0f},
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
          .location = {6.0f, -4.0f, 8.0f},
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
  EXPECT_FALSE(renderable_record->double_sided());
  EXPECT_TRUE(renderable_record->mesh_component.double_sided);
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
                         TransformComponent{.location = {2.0f, 0.0f, 0.0f}});
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
      entity, TransformComponent{.location = {1.0f, 2.0f, 3.0f}});
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
      TransformComponent{.location = {3.0f, 4.0f, 5.0f}});
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
  ASSERT_TRUE(document.set_transform(member, {.location = {2.0f, 0.0f, 0.0f}}));
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
  moved.location.x = 3.0f;
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
      first, TransformComponent{.location = {3.0f, 4.0f, 5.0f}});
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
    moved.location.x += 1.0f;
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

TEST(RenderSelectionDimensionIntegrationTests,
     SingleAndMultipleMeshesMeasureTransformedAggregateExtents) {
  SceneDocument scene;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
  const EntityId first = scene.create_entity("First Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(first, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_transform(first, {.location = {4.0f, 2.0f, -3.0f},
                                          .scale = {2.0f, 1.0f, 3.0f}}));
  const EntityId second = scene.create_entity("Second Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(second, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_transform(second, {.location = {-5.0f, -4.0f, 10.0f}}));
  ASSERT_TRUE(scene.set_selected(first, true));

  ViewportOverlaySubmissionCoordinator coordinator;
  DimensionExtractionTarget target;
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Object,
                               coordinator, target),
                           {2.0f, 2.0f, -6.0f}, {6.0f, 2.0f, 0.0f});
  ASSERT_TRUE(scene.set_selected(second, true));
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Object,
                               coordinator, target),
                           {-6.0f, -4.0f, -6.0f}, {6.0f, 2.0f, 11.0f});
}

TEST(RenderSelectionDimensionIntegrationTests,
     SingleAndMultipleGroupsIncludeNestedInstanceContent) {
  SceneDocument scene;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
  const EntityId definition = scene.create_empty("Hidden Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  ASSERT_TRUE(scene.set_transform(definition, {.location = {100.0f, 0.0f, 0.0f}}));
  const EntityId member = scene.create_entity("Definition Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(scene.set_transform(member, {.location = {1.0f, 0.0f, 0.0f}}));

  const EntityId first = scene.create_empty("First Group");
  ASSERT_TRUE(scene.set_transform(first, {.location = {10.0f, 0.0f, 0.0f}}));
  const EntityId child = scene.create_entity("Group Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(child, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(child, first, false));
  ASSERT_TRUE(scene.set_transform(child, {.location = {2.0f, 1.0f, 0.0f}}));
  const EntityId occurrence = scene.create_entity("Nested Instance", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_parent(occurrence, first, false));
  ASSERT_TRUE(scene.set_transform(occurrence, {.location = {-4.0f, 3.0f, 5.0f}}));

  const EntityId second = scene.create_empty("Second Group");
  ASSERT_TRUE(scene.set_transform(second, {.location = {-20.0f, -2.0f, -5.0f}}));
  const EntityId second_child = scene.create_entity("Second Group Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(second_child, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(second_child, second, false));
  ASSERT_TRUE(scene.set_selected(first, true));

  ViewportOverlaySubmissionCoordinator coordinator;
  DimensionExtractionTarget target;
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Group,
                               coordinator, target),
                           {6.0f, 1.0f, -1.0f}, {13.0f, 3.0f, 6.0f});
  ASSERT_TRUE(scene.set_selected(second, true));
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Group,
                               coordinator, target),
                           {-21.0f, -2.0f, -6.0f}, {13.0f, 3.0f, 6.0f});
}

TEST(RenderSelectionDimensionIntegrationTests,
     FormalInstancesAndMixedSelectionMeasurePublicOccurrences) {
  SceneDocument scene;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_authored_quad_mesh());
  const EntityId definition = scene.create_empty("Hidden Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  ASSERT_TRUE(scene.set_transform(definition, {.location = {100.0f, 0.0f, 0.0f}}));
  for (const QVec3 position : {QVec3{}, QVec3{4.0f, 2.0f, 6.0f}}) {
    const EntityId member = scene.create_entity("Definition Member", EntityKind::Mesh);
    ASSERT_TRUE(scene.set_mesh(member, {.mesh = mesh}));
    ASSERT_TRUE(scene.set_parent(member, definition, false));
    ASSERT_TRUE(scene.set_transform(member, {.location = position}));
  }
  const EntityId first = scene.create_entity("First Instance", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(first, {.definition = definition}));
  ASSERT_TRUE(scene.set_transform(first, {.location = {10.0f, -3.0f, 5.0f},
                                          .scale = {2.0f, 1.0f, 3.0f}}));
  const EntityId second = scene.create_entity("Second Instance", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(second, {.definition = definition}));
  ASSERT_TRUE(scene.set_transform(second, {.location = {-10.0f, 7.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_selected(first, true));

  ViewportOverlaySubmissionCoordinator coordinator;
  DimensionExtractionTarget target;
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Object,
                               coordinator, target),
                           {8.0f, -3.0f, 2.0f}, {20.0f, -1.0f, 26.0f});
  ASSERT_TRUE(scene.set_selected(second, true));
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Object,
                               coordinator, target),
                           {-11.0f, -3.0f, -1.0f}, {20.0f, 9.0f, 26.0f});

  const EntityId group = scene.create_empty("Mixed Group");
  const EntityId child = scene.create_entity("Group Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(child, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(child, group, false));
  ASSERT_TRUE(scene.set_transform(child, {.location = {30.0f, -8.0f, -5.0f}}));
  const EntityId loose_mesh = scene.create_entity("Mixed Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(loose_mesh, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_transform(loose_mesh, {.location = {-30.0f, 12.0f, 40.0f}}));
  ASSERT_TRUE(scene.set_selected(group, true));
  ASSERT_TRUE(scene.set_selected(loose_mesh, true));
  expect_dimension_extents(extract_selection_dimensions(
                               scene, assets, RenderOverlaySelectionMode::Object,
                               coordinator, target),
                           {-31.0f, -8.0f, -6.0f}, {31.0f, 12.0f, 41.0f});
}

TEST(RenderSelectionDimensionIntegrationTests,
     SelectedFacesMeasureOnlyChosenVerticesAcrossMeshes) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  quader_poly::Document polygon = *make_authored_quad_mesh().authored_polygon_document;
  polygon.vertices.insert(polygon.vertices.end(), {
      {.id = 5u, .position = {-1.0f, 10.0f, -1.0f}},
      {.id = 6u, .position = {1.0f, 10.0f, -1.0f}},
      {.id = 7u, .position = {1.0f, 10.0f, 1.0f}},
      {.id = 8u, .position = {-1.0f, 10.0f, 1.0f}},
  });
  polygon.faces.push_back({.id = 2u, .vertices = {5u, 6u, 7u, 8u}});
  polygon.next_vertex_id = 9u;
  polygon.next_face_id = 3u;
  const auto first = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(polygon), {.name = "First Faces"});
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto second = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(polygon), {.name = "Second Faces"});
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.object(second.value()).set_transform(
      {.location = {10.0f, 5.0f, -4.0f}}).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first.value().entity, second.value().entity}, first.value().entity).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(first.value(), 1).ok());

  DimensionModelingFrameProvider provider(session.modeling());
  DimensionExtractionTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  const auto submit = [&] {
    const auto scene = build_render_overlay_selection_snapshot(session.scene(), session.assets());
    const auto revisions = editor.revisions();
    return coordinator.submit(
        {.selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&session.modeling()),
             .content_revision = revisions.modeling_content_revision,
             .selection_revision = revisions.modeling_selection_revision,
             .scene_revision = revisions.scene_revision,
             .asset_revision = revisions.asset_revision,
             .overlay_signature = session.modeling().selection_overlay_signature(),
             .selection_context = ViewportOverlaySelectionContext::Face},
         .selection_frame_provider = &provider,
         .scene_overlay_snapshot = &scene,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Face,
         .overlays_visible = true,
         .scene_overlay_requested = true},
        ViewportSettings::defaults(), target);
  };
  expect_dimension_extents(submit(), {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f});
  ASSERT_TRUE(editor.selection().select_face(
      second.value(), 1, QuaderSelectionEdit::Add).ok());
  expect_dimension_extents(submit(), {-1.0f, 0.0f, -5.0f}, {11.0f, 5.0f, 1.0f});
  ASSERT_TRUE(editor.selection().select_face(
      first.value(), 2, QuaderSelectionEdit::Add).ok());
  expect_dimension_extents(submit(), {-1.0f, 0.0f, -5.0f}, {11.0f, 10.0f, 1.0f});
}

TEST(RenderSelectionDimensionIntegrationTests,
     LiveFaceExtrusionRefreshesDimensionsAndCancelRestoresCommittedBounds) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  quader_poly::Document polygon;
  const std::array vertices{
      quader_poly::add_vertex(polygon, {0.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(polygon, {2.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(polygon, {2.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(polygon, {0.0f, 1.0f, 0.0f})};
  const auto face = quader_poly::add_face(polygon, vertices);
  const auto object = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(polygon)),
      {.name = "Extrusion Dimensions"});
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(object.value(),
      static_cast<QuaderElementId>(face)).ok());

  DimensionModelingFrameProvider provider(session.modeling());
  DimensionExtractionTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  const auto submit = [&] {
    const auto revisions = editor.revisions();
    return coordinator.submit(
        {.selection_source = {
             .source_identity = reinterpret_cast<std::uintptr_t>(&session.modeling()),
             .content_revision = revisions.modeling_content_revision,
             .selection_revision = revisions.modeling_selection_revision,
             .scene_revision = revisions.scene_revision,
             .asset_revision = revisions.asset_revision,
             .overlay_signature = session.modeling().selection_overlay_signature(),
             .selection_context = ViewportOverlaySelectionContext::Face},
         .selection_frame_provider = &provider,
         .scene_selection_mode = RenderOverlaySelectionMode::Component,
         .style_selection_context = ViewportOverlaySelectionContext::Face,
         .overlays_visible = true},
        ViewportSettings::defaults(), target);
  };
  expect_dimension_extents(submit(), {0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f});
  ASSERT_TRUE(session.extrude().set_mode(modeling::FaceExtrudeMode::Region).ok);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(session.extrude().set_amount(1.0).ok);
  const auto durable = editor.revisions();
  for (const double amount : {2.15, 2.1501, 3.25, 0.0, -1.25}) {
    SCOPED_TRACE(amount);
    ASSERT_TRUE(session.extrude().set_amount(amount).ok);
    const auto revisions = editor.revisions();
    EXPECT_EQ(revisions.modeling_content_revision, durable.modeling_content_revision);
    EXPECT_EQ(revisions.asset_revision, durable.asset_revision);
    const auto result = submit();
    EXPECT_TRUE(result.selection_source_refreshed);
    const float height = static_cast<float>(
        amount / viewport_host_defaults().map_units_per_world_unit);
    expect_dimension_extents(result, {0.0f, 0.0f, height}, {2.0f, 1.0f, height});
    const auto work = coordinator.work_counters();
    EXPECT_FALSE(submit().selection_source_refreshed);
    EXPECT_EQ(coordinator.work_counters().selection_dimension_bounds_build_count,
              work.selection_dimension_bounds_build_count);
  }
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(session.extrude().preview_active());
  const auto canceled = submit();
  EXPECT_TRUE(canceled.selection_source_refreshed);
  expect_dimension_extents(canceled, {0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f});
}

TEST(ViewportSceneOverlayBuilderTests, ActiveStylesUseIndependentPinkAndPreserveFillOpacityAndGeometry) {
  ViewportSettings settings = ViewportSettings::defaults();
  const ViewportSettingColor configured{0.2f, 0.4f, 0.6f, 0.4f};
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionFace, configured));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire, configured));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectedVertex, configured));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::BoundingBoxWire, configured));
  ViewportOverlayFrame frame;
  frame.triangles.push_back({.style_role = ViewportOverlayStyleRole::ActiveSelectionFace});
  frame.lines.push_back({.layer = ViewportOverlayLayer::SelectedEdge,
      .style_role = ViewportOverlayStyleRole::ActiveSelectionWire});
  frame.lines.push_back({.layer = ViewportOverlayLayer::GroupBounds,
      .style_role = ViewportOverlayStyleRole::ActiveGroupBounds});
  frame.points.push_back({.layer = ViewportOverlayLayer::SelectedVertex,
      .style_role = ViewportOverlayStyleRole::ActiveSelectedVertex});
  ViewportOverlayStyleResolver::apply(frame, settings);
  const auto expected = rgba8(255, 105, 180, 102);
  EXPECT_EQ(frame.triangles.front().color, expected);
  EXPECT_EQ(frame.lines.front().color, rgba8(255, 105, 180));
  EXPECT_EQ(frame.lines.back().color, expected);
  EXPECT_EQ(frame.points.front().color, expected);
  EXPECT_EQ(viewport_active_selection_color(rgba8(255, 255, 255, 73)), rgba8(255, 105, 180, 73));
  EXPECT_EQ(viewport_active_selection_color(rgba8(0, 0, 0, 0)), rgba8(255, 105, 180, 0));
}

TEST(ViewportSceneOverlayBuilderTests, HierarchyFeedbackUsesNearestExplicitSelection) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId outer = document.create_empty("Outer");
  const EntityId inner = document.create_empty("Inner");
  const EntityId direct = document.create_entity("Direct", EntityKind::Mesh);
  const EntityId inherited = document.create_entity("Inherited", EntityKind::Mesh);
  ASSERT_TRUE(document.set_parent(inner, outer));
  ASSERT_TRUE(document.set_parent(direct, inner));
  ASSERT_TRUE(document.set_parent(inherited, inner));
  ASSERT_TRUE(document.set_mesh(direct, {.mesh = mesh}));
  ASSERT_TRUE(document.set_mesh(inherited, {.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(outer, true));
  ASSERT_TRUE(document.set_selected(inner, true));
  ASSERT_TRUE(document.set_selected(direct, true));
  ASSERT_TRUE(document.set_active_selection(outer));
  auto scene = build_render_overlay_selection_snapshot(document, assets);
  ASSERT_EQ(scene.hierarchy_selection_wires.size(), 1u);
  EXPECT_EQ(scene.hierarchy_selection_wires.front().selection_root, inner);
  EXPECT_FALSE(scene.hierarchy_selection_wires.front().active_selected);
  auto objects = build_render_overlay_selected_object_snapshot(document, assets, true);
  ASSERT_EQ(objects.meshes.size(), 1u);
  EXPECT_FALSE(objects.meshes.front().active_selected);
  ASSERT_TRUE(document.set_active_selection(inner));
  scene = build_render_overlay_selection_snapshot(document, assets);
  EXPECT_TRUE(scene.hierarchy_selection_wires.front().active_selected);
  objects = build_render_overlay_selected_object_snapshot(document, assets, true);
  EXPECT_FALSE(objects.meshes.front().active_selected);
  const auto components = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, scene, false, RenderOverlaySelectionMode::Component);
  EXPECT_FALSE(std::ranges::any_of(components.lines, [](const auto &line) {
    return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire ||
           line.style_role == ViewportOverlayStyleRole::ActiveGroupBounds;
  }));
}

TEST(ViewportSceneOverlayBuilderTests,
     HierarchyExpansionKeepsSingletonOrdinaryAcrossObjectAndGroupModes) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId group = document.create_empty("Group");
  for (int index = 0; index < 2; ++index) {
    const EntityId child = document.create_entity(
        "Child " + std::to_string(index), EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(child, {.mesh = mesh}));
    ASSERT_TRUE(document.set_parent(child, group));
  }
  const EntityId peer = document.create_entity("Peer", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(peer, {.mesh = mesh}));
  ASSERT_TRUE(document.set_selected(group, true));
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));

  for (const bool multiple : {false, true, false}) {
    ASSERT_TRUE(document.set_selected(peer, multiple));
    ASSERT_TRUE(document.set_active_selection(group));
    const auto snapshot = build_render_overlay_selection_snapshot(document, assets);
    EXPECT_EQ(snapshot.selected_element_count, multiple ? 2u : 1u);
    ASSERT_EQ(snapshot.hierarchy_selection_wires.size(), 2u);
    ASSERT_EQ(snapshot.group_bounds.size(), 1u);
    EXPECT_EQ(snapshot.group_bounds.front().active_selected, multiple);
    EXPECT_TRUE(std::ranges::all_of(snapshot.hierarchy_selection_wires,
        [multiple](const auto &record) { return record.active_selected == multiple; }));
    const auto objects = build_render_overlay_selected_object_snapshot(document, assets, true);
    EXPECT_EQ(objects.meshes.size(), multiple ? 1u : 0u);
    EXPECT_TRUE(std::ranges::none_of(objects.meshes,
        [](const auto &record) { return record.active_selected; }));

    for (const auto mode : {RenderOverlaySelectionMode::Object,
                            RenderOverlaySelectionMode::Group}) {
      SCOPED_TRACE(static_cast<int>(mode));
      auto frame = ViewportSceneOverlayBuilder::build_selection_frame(
          {}, snapshot, false, mode);
      ViewportOverlayStyleResolver::apply(frame, settings);
      EXPECT_EQ(line_source_count_for(frame, ViewportOverlaySource::HierarchySelection), 6u);
      EXPECT_TRUE(std::ranges::all_of(frame.lines, [multiple](const auto &line) {
        return line.source != ViewportOverlaySource::HierarchySelection ||
               (line.style_role == (multiple ? ViewportOverlayStyleRole::ActiveSelectionWire
                                             : ViewportOverlayStyleRole::SelectionWire) &&
                line.color == (multiple ? viewport_active_selection_color(kOverlaySelectionWireColor)
                                        : kOverlaySelectionWireColor) &&
                line.width_pixels == (multiple ? 6.0f : 2.0f));
      }));
      EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::GroupBounds), 12u);
      EXPECT_TRUE(std::ranges::all_of(frame.lines, [multiple](const auto &line) {
        return line.layer != ViewportOverlayLayer::GroupBounds ||
               line.style_role == (multiple ? ViewportOverlayStyleRole::ActiveGroupBounds
                                            : ViewportOverlayStyleRole::GroupBounds);
      }));
    }
  }
}

TEST(ViewportSceneOverlayBuilderTests,
     LightSingletonMultiSelectionAndReturnUseSemanticSelectionCount) {
  AssetRegistry assets;
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
  for (const auto type : {LightType::Point, LightType::Directional}) {
    SCOPED_TRACE(static_cast<int>(type));
    SceneDocument document;
    const EntityId light = document.create_entity("Light", EntityKind::Light);
    const EntityId peer = document.create_empty("Peer");
    ASSERT_TRUE(document.set_light(light, {.type = type}));
    ASSERT_TRUE(document.set_selected(light, true));
    for (const bool multiple : {false, true, false}) {
      ASSERT_TRUE(document.set_selected(peer, multiple));
      ASSERT_TRUE(document.set_active_selection(light));
      const auto snapshot = build_render_overlay_selection_snapshot(document, assets);
      EXPECT_EQ(snapshot.selected_element_count, multiple ? 2u : 1u);
      auto frame = ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
      ViewportOverlayStyleResolver::apply(frame, settings);
      if (type == LightType::Point) {
        ASSERT_EQ(snapshot.point_light_gizmos.size(), 1u);
        EXPECT_EQ(snapshot.point_light_gizmos.front().active_selected, multiple);
        ASSERT_EQ(frame.icons.size(), 1u);
        EXPECT_EQ(frame.icons.front().style_role,
            multiple ? ViewportOverlayStyleRole::ActiveSelectionWire
                     : ViewportOverlayStyleRole::SelectionWire);
        EXPECT_EQ(frame.icons.front().color,
            multiple ? viewport_active_selection_color(kOverlaySelectionWireColor)
                     : kOverlaySelectionWireColor);
      } else {
        ASSERT_EQ(snapshot.selected_lights.size(), 1u);
        EXPECT_EQ(snapshot.selected_lights.front().active_selected, multiple);
        ASSERT_EQ(frame.points.size(), 1u);
        EXPECT_EQ(frame.points.front().style_role,
            multiple ? ViewportOverlayStyleRole::ActiveSelectedVertex
                     : ViewportOverlayStyleRole::SelectedVertex);
        EXPECT_EQ(frame.points.front().color,
            multiple ? viewport_active_selection_color(kOverlaySelectedVertexColor)
                     : kOverlaySelectedVertexColor);
        ASSERT_EQ(frame.lines.size(), 2u);
        EXPECT_TRUE(std::ranges::all_of(frame.lines, [multiple](const auto &line) {
          return line.style_role == (multiple ? ViewportOverlayStyleRole::ActiveSelectionWire
                                              : ViewportOverlayStyleRole::SelectionWire) &&
                 line.width_pixels == (multiple ? 6.0f : 2.0f);
        }));
      }
    }
  }
}

TEST(ViewportSceneOverlayBuilderTests, ActiveLightUsesExistingMarkersWithoutChangingRadius) {
  SceneDocument document;
  AssetRegistry assets;
  const EntityId directional = document.create_entity("Directional", EntityKind::Light);
  const EntityId point = document.create_entity("Point", EntityKind::Light);
  ASSERT_TRUE(document.set_light(directional, {.type = LightType::Directional}));
  ASSERT_TRUE(document.set_light(point, {.type = LightType::Point}));
  ASSERT_TRUE(document.set_selected(directional, true));
  ASSERT_TRUE(document.set_selected(point, true));
  ASSERT_TRUE(document.set_active_selection(directional));
  auto snapshot = build_render_overlay_selection_snapshot(document, assets);
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
      {0.2f, 0.4f, 0.6f, 0.4f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::ActiveSelection,
      {17.0f / 255.0f, 187.0f / 255.0f, 221.0f / 255.0f, 1.0f}));
  auto frame = ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
  ViewportOverlayStyleResolver::apply(frame, settings);
  ASSERT_EQ(frame.icons.size(), 1u);
  EXPECT_EQ(frame.icons.front().color, rgba8(51, 102, 153, 102));
  EXPECT_EQ(frame.points.front().style_role, ViewportOverlayStyleRole::ActiveSelectedVertex);
  EXPECT_TRUE(std::ranges::all_of(frame.lines, [](const auto &line) {
    return line.source != ViewportOverlaySource::Gizmo ||
           line.color == kOverlayPointLightRadiusWireColor;
  }));
  ASSERT_TRUE(document.set_active_selection(point));
  snapshot = build_render_overlay_selection_snapshot(document, assets);
  frame = ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
  ViewportOverlayStyleResolver::apply(frame, settings);
  EXPECT_EQ(frame.icons.front().color, rgba8(17, 187, 221));
  EXPECT_EQ(frame.points.front().style_role, ViewportOverlayStyleRole::SelectedVertex);
  frame = ViewportSceneOverlayBuilder::build_selection_frame(
      {}, snapshot, true, RenderOverlaySelectionMode::Component);
  EXPECT_EQ(frame.icons.front().style_role, ViewportOverlayStyleRole::SelectionWire);
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
        ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
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
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
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
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
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
     CoSelectedGroupsDistinguishActiveHierarchyAndCageStyle) {
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
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::ActiveSelection,
                                {26.0f / 255.0f, 204.0f / 255.0f, 153.0f / 255.0f, 1.0f}));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));

  ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);
  ViewportOverlayStyleResolver::apply(frame, settings);
  const std::uint32_t selection_color = rgba8(40, 80, 120);
  const std::uint32_t bounds_color = rgba8(20, 60, 100);
  const std::uint32_t active_color = rgba8(26, 204, 153);
  EXPECT_EQ(std::ranges::count_if(
                frame.lines, [&](const ViewportOverlayLine &line) {
                  return line.source ==
                             ViewportOverlaySource::HierarchySelection &&
                         line.style_role ==
                             ViewportOverlayStyleRole::SelectionWire &&
                         line.color == selection_color &&
                         line.width_pixels == 2.25f;
                }),
            3u);
  EXPECT_EQ(std::ranges::count_if(
                frame.lines, [&](const ViewportOverlayLine &line) {
                  return line.source == ViewportOverlaySource::GroupBounds &&
                         line.style_role ==
                             ViewportOverlayStyleRole::GroupBounds &&
                         line.color == bounds_color &&
                         line.width_pixels == 4.5f;
                }),
            12u);
  EXPECT_EQ(std::ranges::count_if(frame.lines, [&](const auto &line) {
    return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire &&
           line.color == active_color &&
           line.width_pixels == 6.0f;
  }), 3u);
  EXPECT_EQ(std::ranges::count_if(frame.lines, [&](const auto &line) {
    return line.style_role == ViewportOverlayStyleRole::ActiveGroupBounds &&
           line.color == active_color &&
           line.width_pixels == 6.0f;
  }), 12u);
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
      occurrence, TransformComponent{.location = {4.0f, 0.0f, 0.0f}});
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
               (line.style_role == ViewportOverlayStyleRole::ActiveGroupBounds &&
                line.color == viewport_active_selection_color(rgba8(180, 40, 140)));
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
            .location = {3.0f, 0.5f, -2.0f},
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
          .location = {3.0f, 0.5f, -2.0f},
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
      first_member, TransformComponent{.location = {-2.0f, 0.0f, 0.0f}});
  document.set_transform(
      second_member, TransformComponent{.location = {4.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_parent(first_member, definition, false));
  ASSERT_TRUE(document.set_parent(second_member, definition, false));

  const EntityId occurrence =
      document.create_entity("Selected Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 1.0f, -3.0f}});
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
      occurrence, TransformComponent{.location = {20.0f, 2.0f, 5.0f}});
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
      second_member, TransformComponent{.location = {8.0f, 0.0f, 0.0f}});
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
      ordinary, TransformComponent{.location = {-10.0f, -4.0f, 2.0f}});
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
        first, TransformComponent{.location = {-3.0f, 1.0f, 2.0f}});
    document.set_transform(
        second, TransformComponent{.location = {4.0f, -2.0f, -1.0f}});
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
                           TransformComponent{.location = {2.0f, 3.0f, 4.0f}});
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
              ViewportOverlayStyleRole::ActiveSelectedVertex);
    EXPECT_EQ(frame.points.front().color, kOverlaySelectedVertexColor);
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 2u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::Selection &&
                 line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire &&
                 line.color == kOverlaySelectionWireColor;
        }));
    const auto styled = ViewportOverlayStyleResolver::resolve(frame, ViewportSettings::defaults());
    EXPECT_EQ(styled.points.front().color, viewport_active_selection_color(kOverlaySelectedVertexColor));
    EXPECT_TRUE(std::ranges::all_of(styled.lines, [](const auto &line) {
      return line.color == viewport_active_selection_color(kOverlaySelectionWireColor);
    }));
}

TEST(ViewportSceneOverlayBuilderTests, AddsPointLightBillboardGizmosWhenRequested) {
    SceneDocument document;
    AssetRegistry assets;
    const EntityId selected_light =
        document.create_entity("Selected Point Light", EntityKind::Light);
    document.set_transform(
        selected_light, TransformComponent{.location = {2.0f, 3.0f, 4.0f}});
    document.set_light(selected_light, LightComponent{.type = LightType::Point});
    ASSERT_TRUE(document.set_selected(selected_light, true));
    const EntityId unselected_light =
        document.create_entity("Unselected Point Light", EntityKind::Light);
    document.set_transform(
        unselected_light, TransformComponent{.location = {-1.0f, 0.5f, 6.0f}});
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

TEST(OverlayLineMeshBuilderTests, ActiveComponentWirePrioritySurvivesEqualColorsAndCameraMotion) {
  for (const auto layer : {ViewportOverlayLayer::SelectedEdge,
                           ViewportOverlayLayer::SelectedFaceEdge}) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({.layer = layer,
        .start = {-0.5f, -0.5f, -1.0f}, .end = {0.5f, 0.5f, -1.0f},
        .color = rgba8(255, 128, 192), .width_pixels = 2.0f,
        .style_role = ViewportOverlayStyleRole::SelectionWire,
        .source = ViewportOverlaySource::ComponentSelection,
        .element = {.object_id = 1, .edge_a = 1, .edge_b = 2}});
    frame.lines.push_back(frame.lines.front());
    frame.lines.back().element.object_id = 2;
    frame.lines.back().style_role = ViewportOverlayStyleRole::ActiveSelectionWire;
    const auto camera = test_overlay_camera();
    const auto plan = OverlayRenderableBuilder::build_layer(frame, camera, layer);
    ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
    const auto &mesh = plan.edit_wire_batches.front().mesh;
    ASSERT_EQ(mesh.vertices.size(), 8u);
    for (std::size_t index = 0u; index < 4u; ++index) {
      EXPECT_FLOAT_EQ(mesh.vertices[index].control.w, 0.0f);
      EXPECT_FLOAT_EQ(mesh.vertices[index + 4u].control.w, 1.0f);
      EXPECT_EQ(mesh.vertices[index].color, mesh.vertices[index + 4u].color);
    }
    EXPECT_FALSE(plan.edit_wire_batches.front().state.depth_write);
    EXPECT_TRUE(plan.edit_wire_batches.front().state.depth_culling);
    const auto signature = OverlayRenderableBuilder::layer_build_signature(frame, camera, layer);
    auto distant_camera = camera;
    distant_camera.eye.z += 2048.0f;
    EXPECT_EQ(signature, OverlayRenderableBuilder::layer_build_signature(frame, distant_camera, layer));
    frame.lines.back().style_role = ViewportOverlayStyleRole::SelectionWire;
    EXPECT_NE(signature, OverlayRenderableBuilder::layer_build_signature(frame, camera, layer));
  }
}

TEST(OverlayLineMeshBuilderTests, SelectionDimensionsKeepActiveWireExclusionSeparateFromGroupCages) {
  for (const auto occlusion : {ViewportOverlayLineOcclusionStyle::Default,
                               ViewportOverlayLineOcclusionStyle::Dashed}) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({.layer = ViewportOverlayLayer::GroupBounds,
        .start = {-0.5f, 0.0f, -1.0f}, .end = {0.5f, 0.0f, -1.0f},
        .color = rgba8(255, 211, 31), .width_pixels = 2.0f,
        .source = ViewportOverlaySource::GroupBounds, .occlusion_style = occlusion});
    frame.lines.push_back(frame.lines.front());
    frame.lines.back().source = ViewportOverlaySource::SelectionDimensions;
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), ViewportOverlayLayer::GroupBounds);
    ASSERT_EQ(plan.line_batches.size(), 2u);
    for (std::size_t index = 0u; index < plan.line_batches.size(); ++index) {
      const auto &batch = plan.line_batches[index];
      EXPECT_EQ(batch.mesh.vertices.size(), 4u);
      EXPECT_EQ(batch.state.suppressed_by_active_wire, index == 1u);
      EXPECT_FALSE(batch.state.depth_write);
      EXPECT_EQ(batch.occluded_state.has_value(),
          occlusion == ViewportOverlayLineOcclusionStyle::Dashed);
      if (batch.occluded_state) {
        EXPECT_EQ(batch.occluded_state->suppressed_by_active_wire, index == 1u);
      }
    }
  }
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

TEST(OverlayLineMeshBuilderTests,
     DashedPreviewSharesOneWorldRibbonWithComplementaryFullOpacityPasses) {
  for (const std::uint8_t alpha : {std::uint8_t{255}, std::uint8_t{93},
                                  std::uint8_t{0}}) {
    SCOPED_TRACE(static_cast<int>(alpha));
    ViewportOverlayFrame frame;
    const auto color = rgba8(51, 102, 153, alpha);
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::HoverEdge,
        .start = {-0.5f, 0.0f, -1.0f},
        .end = {0.5f, 0.0f, -4.0f},
        .color = color,
        .width_pixels = kOverlayToolPreviewLineWidthPixels,
        .style_role = ViewportOverlayStyleRole::ToolPreviewLine,
        .source = ViewportOverlaySource::Tool,
        .surface_support = {ViewportOverlaySurfaceSupport{{0.0f, 1.0f, -2.5f}, 7}},
        .occlusion_style = ViewportOverlayLineOcclusionStyle::Dashed,
    });
    const auto camera = test_perspective_overlay_camera();
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, camera, ViewportOverlayLayer::HoverEdge);
    ASSERT_EQ(plan.line_batches.size(), 1u);
    EXPECT_TRUE(plan.edit_wire_batches.empty());
    const auto &batch = plan.line_batches.front();
    ASSERT_TRUE(batch.occluded_state.has_value());
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_EQ(batch.mesh.indices.size(), 6u);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_TRUE(batch.occluded_state->depth_culling);
    EXPECT_FALSE(batch.state.depth_write);
    EXPECT_FALSE(batch.occluded_state->depth_write);
    EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::None);
    EXPECT_EQ(batch.occluded_state->silhouette_pass, OverlaySilhouettePass::None);
    EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::None);
    EXPECT_EQ(batch.occluded_state->face_fill_class, OverlayFaceFillClass::None);
    EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier, 1.0f);
    EXPECT_FLOAT_EQ(batch.width_pixels, kOverlayToolPreviewLineWidthPixels);
    EXPECT_EQ(batch.occlusion_style, ViewportOverlayLineOcclusionStyle::Dashed);
    for (const auto &vertex : batch.mesh.vertices) {
      EXPECT_EQ(vertex.color, rgba8_srgb_to_linear(color));
      EXPECT_FLOAT_EQ(vertex.support0.x, 0.0f);
      EXPECT_FLOAT_EQ(vertex.support0.y, 1.0f);
      EXPECT_FLOAT_EQ(vertex.support0.z, -2.5f);
    }
    auto moved_camera = camera;
    moved_camera.eye = {3.0f, 2.0f, 1.0f};
    const auto moved = OverlayRenderableBuilder::build_layer(
        frame, moved_camera, ViewportOverlayLayer::HoverEdge);
    ASSERT_EQ(moved.line_batches.size(), 1u);
    EXPECT_EQ(moved.line_batches.front().mesh.indices, batch.mesh.indices);
    ASSERT_EQ(moved.line_batches.front().mesh.vertices.size(), 4u);
    for (std::size_t index = 0u; index < 4u; ++index) {
      const auto &before = batch.mesh.vertices[index];
      const auto &after = moved.line_batches.front().mesh.vertices[index];
      EXPECT_FLOAT_EQ(before.start.x, after.start.x);
      EXPECT_FLOAT_EQ(before.start.z, after.start.z);
      EXPECT_FLOAT_EQ(before.end.x, after.end.x);
      EXPECT_FLOAT_EQ(before.end.z, after.end.z);
      EXPECT_FLOAT_EQ(before.control.y, after.control.y);
    }
  }
}

TEST(OverlayLineMeshBuilderTests,
     DashPeriodsPartitionBatchesAndInvalidPeriodsUseThePolicyDefault) {
  ViewportOverlayFrame frame;
  for (const float period : {0.0f, -1.0f,
                            std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), 16.0f}) {
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::HoverEdge,
        .start = {-0.5f, 0.0f, -4.0f},
        .end = {0.5f, 0.0f, -4.0f},
        .width_pixels = 2.0f,
        .source = ViewportOverlaySource::Tool,
        .occlusion_style = ViewportOverlayLineOcclusionStyle::Dashed,
        .occluded_dash_period_pixels = period,
    });
  }
  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, test_perspective_overlay_camera(), ViewportOverlayLayer::HoverEdge);
  ASSERT_EQ(plan.line_batches.size(), 2u);
  EXPECT_FLOAT_EQ(plan.line_batches[0].occluded_dash_period_pixels,
                  kOverlayOccludedLineDashPeriodPixels);
  EXPECT_EQ(plan.line_batches[0].mesh.vertices.size(), 16u);
  EXPECT_FLOAT_EQ(plan.line_batches[1].occluded_dash_period_pixels, 16.0f);
  EXPECT_EQ(plan.line_batches[1].mesh.vertices.size(), 4u);
}

TEST(OverlayLineMeshBuilderTests,
     DashedPreviewDoesNotMergeWithDefaultOrSelectionSilhouetteBatches) {
  ViewportOverlayFrame frame;
  const ViewportOverlayLine ordinary{
      .layer = ViewportOverlayLayer::HoverEdge,
      .start = {-0.5f, 0.0f, -4.0f},
      .end = {0.5f, 0.0f, -4.0f},
      .width_pixels = 2.0f,
      .source = ViewportOverlaySource::Tool,
  };
  frame.lines.push_back(ordinary);
  auto dashed = ordinary;
  dashed.occlusion_style = ViewportOverlayLineOcclusionStyle::Dashed;
  frame.lines.push_back(dashed);
  auto silhouette = ordinary;
  silhouette.source = ViewportOverlaySource::MeshOutline;
  frame.lines.push_back(silhouette);
  auto component = ordinary;
  component.source = ViewportOverlaySource::ComponentHover;
  frame.lines.push_back(component);

  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, test_perspective_overlay_camera(), ViewportOverlayLayer::HoverEdge);
  ASSERT_EQ(plan.line_batches.size(), 3u);
  ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
  for (const auto &batch : plan.line_batches) {
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    if (batch.occlusion_style == ViewportOverlayLineOcclusionStyle::Dashed) {
      ASSERT_TRUE(batch.occluded_state.has_value());
      EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier, 1.0f);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::None);
    } else if (batch.occluded_state) {
      EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier,
                      kOverlayOccludedSilhouetteAlphaMultiplier);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::Visible);
    } else {
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
    }
  }
}

TEST(OverlayLineMeshBuilderTests,
     ObjectSilhouettesShareGeometryForComplementaryVisibleAndOccludedPasses) {
  for (const std::uint8_t alpha : {std::uint8_t{255}, std::uint8_t{93},
                                  std::uint8_t{0}}) {
    SCOPED_TRACE(static_cast<int>(alpha));
    ViewportOverlayFrame frame;
    const auto color = rgba8(170, 90, 220, alpha);
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {-0.5f, 0.0f, -4.0f},
        .end = {0.0f, 0.0f, -4.0f},
        .color = color,
        .width_pixels = 2.0f,
        .source = ViewportOverlaySource::MeshOutline,
    });
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {0.0f, 0.0f, -4.0f},
        .end = {0.5f, 0.0f, -4.0f},
        .color = viewport_active_selection_color(color),
        .width_pixels = 2.0f,
        .source = ViewportOverlaySource::MeshOutline,
    });

    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, test_perspective_overlay_camera(),
        ViewportOverlayLayer::SelectedEdge);

    ASSERT_EQ(plan.line_batches.size(), 1u);
    const auto &batch = plan.line_batches.front();
    ASSERT_EQ(batch.mesh.vertices.size(), 8u);
    EXPECT_EQ(batch.mesh.indices.size(), 12u);
    EXPECT_EQ(batch.mesh.vertices[0].color, rgba8_srgb_to_linear(color));
    EXPECT_EQ(batch.mesh.vertices[4].color,
              rgba8_srgb_to_linear(viewport_active_selection_color(color)));
    ASSERT_TRUE(batch.occluded_state.has_value());
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_TRUE(batch.occluded_state->depth_culling);
    EXPECT_FALSE(batch.state.depth_write);
    EXPECT_FALSE(batch.occluded_state->depth_write);
    EXPECT_TRUE(batch.state.color_write);
    EXPECT_TRUE(batch.occluded_state->color_write);
    EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::Visible);
    EXPECT_EQ(batch.occluded_state->silhouette_pass,
              OverlaySilhouettePass::Occluded);
    EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::None);
    EXPECT_EQ(batch.occluded_state->face_fill_class, OverlayFaceFillClass::None);
    EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier, 0.5f);
    EXPECT_LT(batch.state.blend_order, batch.occluded_state->blend_order);
    EXPECT_TRUE(plan.edit_wire_batches.empty());
  }
}

TEST(OverlayLineMeshBuilderTests,
     OccludedSilhouettesRemainSeparateFromSameWidthComponentAndGenericLines) {
  ViewportOverlayFrame frame;
  for (const auto source : {ViewportOverlaySource::MeshOutline,
                            ViewportOverlaySource::ComponentSelection,
                            ViewportOverlaySource::Selection,
                            ViewportOverlaySource::External}) {
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {-0.5f, 0.0f, -4.0f},
        .end = {0.5f, 0.0f, -4.0f},
        .color = rgba8(190, 115, 240, 137),
        .width_pixels = 2.0f,
        .source = source,
    });
  }
  auto wide_outline = frame.lines.front();
  wide_outline.width_pixels = 3.0f;
  frame.lines.push_back(wide_outline);

  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, test_perspective_overlay_camera(),
      ViewportOverlayLayer::SelectedEdge);

  ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
  EXPECT_EQ(plan.edit_wire_batches.front().mesh.vertices.size(), 4u);
  EXPECT_EQ(plan.edit_wire_batches.front().state.depth_func,
            OverlayDepthFunc::GreaterOrEqual);
  EXPECT_TRUE(plan.edit_wire_batches.front().state.depth_culling);
  EXPECT_EQ(plan.edit_wire_batches.front().state.silhouette_pass,
            OverlaySilhouettePass::None);
  ASSERT_EQ(plan.line_batches.size(), 3u);
  std::uint16_t last_visible_order = 0u;
  std::uint16_t first_occluded_order =
      std::numeric_limits<std::uint16_t>::max();
  std::size_t outlines = 0u;
  for (const auto &batch : plan.line_batches) {
    if (batch.occluded_state) {
      ++outlines;
      EXPECT_EQ(batch.mesh.vertices.size(), 4u);
      EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::Visible);
      EXPECT_EQ(batch.occluded_state->silhouette_pass,
                OverlaySilhouettePass::Occluded);
      last_visible_order = std::max(last_visible_order,
                                    batch.state.blend_order);
      first_occluded_order = std::min(first_occluded_order,
                                      batch.occluded_state->blend_order);
    } else {
      EXPECT_EQ(batch.mesh.vertices.size(), 8u);
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
      EXPECT_FALSE(batch.state.depth_culling);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::None);
    }
  }
  EXPECT_EQ(outlines, 2u);
  EXPECT_LT(last_visible_order, first_occluded_order);
}

TEST(OverlayLineMeshBuilderTests,
     OccludedSilhouettesDoNotMergeWithOtherDepthTestedLines) {
  ViewportOverlayFrame frame;
  for (const auto source : {ViewportOverlaySource::MeshOutline,
                            ViewportOverlaySource::ComponentSelection,
                            ViewportOverlaySource::Selection,
                            ViewportOverlaySource::Tool}) {
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::OpenEdge,
        .start = {-0.5f, 0.0f, -4.0f},
        .end = {0.5f, 0.0f, -4.0f},
        .width_pixels = 2.0f,
        .source = source,
    });
  }

  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, test_perspective_overlay_camera(), ViewportOverlayLayer::OpenEdge);

  ASSERT_EQ(plan.line_batches.size(), 2u);
  EXPECT_TRUE(plan.edit_wire_batches.empty());
  std::size_t outlines = 0u;
  for (const auto &batch : plan.line_batches) {
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    if (batch.occluded_state) {
      ++outlines;
      EXPECT_EQ(batch.mesh.vertices.size(), 4u);
      EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
    } else {
      EXPECT_EQ(batch.mesh.vertices.size(), 12u);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::None);
    }
  }
  EXPECT_EQ(outlines, 1u);
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

TEST(OverlayFaceFillBuilderTests, PreservesFaceTintWithoutWritingDepth) {
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
    for (const auto &batch : plan.face_fill_batches) {
      EXPECT_EQ(batch.mesh.vertices.size(), 3u);
      EXPECT_EQ(batch.mesh.indices.size(), 3u);
      EXPECT_FALSE(batch.state.depth_write);
      EXPECT_EQ(batch.state.color_write,
                batch.state.face_fill_pass == OverlayFaceFillPass::Color);
      EXPECT_TRUE(batch.state.depth_culling);
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
      EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::Selection);
      EXPECT_EQ(batch.mesh.vertices[0].color,
                rgba8_srgb_to_linear(rgba8(255, 172, 31, 8)));
    }
}

TEST(RenderSceneSnapshotTests,
     ArrayWireframePreviewPinsSharedEdgesAndDistinctRetainedPlacements) {
  AssetRegistry assets;
  std::vector<QMat4> transforms(257u);
  for (std::size_t index = 0u; index < transforms.size(); ++index) {
    transforms[index].values[12] = static_cast<float>(index) * 2.0f;
  }
  const auto records = build_render_tool_preview_snapshot(
      {ToolPreviewMesh{.source_entity = 7u,
                       .mesh = make_overlay_triangle_mesh(),
                       .instance_transforms = transforms,
                       .suppress_source = true},
       ToolPreviewMesh{.source_entity = 8u,
                       .mesh = make_overlay_triangle_mesh()}}, assets);
  auto wires = build_render_tool_preview_wireframe_snapshot(records);
  ASSERT_EQ(wires.meshes.size(), transforms.size() + 1u);
  wires.layer_mask = viewport_grid_layer_mask(0u);
  const auto payload = wires.meshes.front().wire_geometry_payload;
  for (std::size_t index = 0u; index < transforms.size(); ++index) {
    const auto &mesh = wires.meshes[index];
    EXPECT_EQ(mesh.entity, 7u);
    EXPECT_EQ(mesh.world_transform.values, transforms[index].values);
    EXPECT_EQ(mesh.wire_geometry_payload, payload);
    EXPECT_EQ(mesh.geometry_payload, wires.meshes.front().geometry_payload);
    EXPECT_EQ(mesh.wire_segments.size(), 3u);
    if (index != 0u) {
      EXPECT_NE(mesh.participant_token, wires.meshes[index - 1u].participant_token);
    }
  }
  const auto &single = wires.meshes.back();
  EXPECT_EQ(single.entity, 8u);
  EXPECT_EQ(single.world_transform.values, QMat4{}.values);
  EXPECT_EQ(single.wire_segments.size(), 3u);
  EXPECT_FALSE(single.shaded_wire);
  EXPECT_NE(single.participant_token, wires.meshes[transforms.size() - 1u].participant_token);
  const auto plan = plan_scene_wireframe_overlay_batches(wires, 128u);
  EXPECT_EQ(plan.participant_count, 258u);
  EXPECT_EQ(plan.unique_geometry_style_count, 1u);
  EXPECT_EQ(plan.batches.chunks.size(), 3u);
  EXPECT_EQ(plan.work.local_bounds_vertex_visit_count, 6u);
  EXPECT_EQ(plan.work.local_bounds_cache_hit_count, 256u);

  auto moved = records;
  moved.front().instance_transforms.back().values[13] = 4.0f;
  const auto changed = build_render_tool_preview_wireframe_snapshot(moved);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(wires.meshes, changed.meshes));
  ASSERT_EQ(changed.meshes.size(), transforms.size() + 1u);
  const auto &moved_instance = changed.meshes[transforms.size() - 1u];
  EXPECT_EQ(moved_instance.wire_geometry_payload, payload);
  EXPECT_EQ(moved_instance.entity, 7u);
  EXPECT_FLOAT_EQ(moved_instance.world_transform.values[13], 4.0f);
  EXPECT_EQ(changed.meshes.back().entity, 8u);
  EXPECT_EQ(changed.meshes.back().world_transform.values, single.world_transform.values);
  EXPECT_EQ(changed.meshes.back().wire_geometry_payload, single.wire_geometry_payload);
  EXPECT_TRUE(build_render_tool_preview_wireframe_snapshot({}).meshes.empty());
}

TEST(OverlayFaceFillBuilderTests,
     SelectedAndHoverFaceFillsReadSceneDepthAndRenderTwoSided) {
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
        EXPECT_EQ(plan.face_fill_batches.front().state.face_fill_pass,
                  OverlayFaceFillPass::Mask);
        EXPECT_EQ(plan.face_fill_batches.back().state.face_fill_pass,
                  OverlayFaceFillPass::Color);
        for (const auto &batch : plan.face_fill_batches) {
          const bool mask = batch.state.face_fill_pass == OverlayFaceFillPass::Mask;
          EXPECT_FALSE(batch.state.depth_write);
          EXPECT_EQ(batch.state.color_write, !mask);
          EXPECT_TRUE(batch.state.depth_culling);
          EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
          EXPECT_EQ(batch.state.culling_mode, OverlayCullingMode::None);
          EXPECT_EQ(batch.state.transparency_mode,
                    OverlayTransparencyMode::TwoPassesTwoSides);
          EXPECT_EQ(batch.state.blend_order,
                    mask ? 1u : viewport_overlay_draw_order(layer) * 64u);
          EXPECT_EQ(batch.mesh.vertices[0].color,
                    rgba8_srgb_to_linear(rgba8(31, 255, 102, 22)));
        }
    }
}

TEST(OverlayFaceFillBuilderTests,
     EqualColorsKeepSemanticPrioritiesAndAllMasksPrecedeEveryColorStream) {
  const std::array roles{
      ViewportOverlayStyleRole::SelectionFace,
      ViewportOverlayStyleRole::ActiveSelectionFace,
      ViewportOverlayStyleRole::HoverFace,
      ViewportOverlayStyleRole::FacePlaneHoverFace,
      ViewportOverlayStyleRole::RemoveSelectionFace};
  const std::array priorities{
      OverlayFaceFillClass::Selection, OverlayFaceFillClass::ActiveSelection,
      OverlayFaceFillClass::Hover, OverlayFaceFillClass::FacePlaneHover,
      OverlayFaceFillClass::RemoveSelection};
  constexpr auto kColor = rgba8(63, 127, 191, 70);
  std::uint16_t last_mask = 0u;
  auto first_color = std::numeric_limits<std::uint16_t>::max();
  for (const auto layer : {ViewportOverlayLayer::SelectedVisibleFace,
                           ViewportOverlayLayer::HoverFace}) {
    ViewportOverlayFrame frame;
    // Reverse priority and vary world depth: neither input order, depth nor
    // equal user colors may collapse the semantic winner classes together.
    for (std::size_t index = roles.size(); index > 0u; --index) {
      const float z = -static_cast<float>(index);
      frame.triangles.push_back({.layer = layer,
          .a = {-1, -1, z}, .b = {1, -1, z}, .c = {0, 1, z},
          .color = kColor, .style_role = roles[index - 1u]});
    }
    // These are the persistent and dynamic cache blend-order reservations.
    for (const std::uint16_t bias : {0u, 32u}) {
      const auto plan = OverlayRenderableBuilder::build_layer(
          frame, test_overlay_camera(), layer, nullptr, bias);
      ASSERT_EQ(plan.face_fill_batches.size(), roles.size() * 2u);
      for (std::size_t index = 0u; index < priorities.size(); ++index) {
        const auto &mask = plan.face_fill_batches[index * 2u];
        const auto &tint = plan.face_fill_batches[index * 2u + 1u];
        EXPECT_EQ(mask.state.face_fill_class, priorities[index]);
        EXPECT_EQ(tint.state.face_fill_class, priorities[index]);
        EXPECT_EQ(mask.state.face_fill_pass, OverlayFaceFillPass::Mask);
        EXPECT_EQ(tint.state.face_fill_pass, OverlayFaceFillPass::Color);
        EXPECT_FALSE(mask.state.color_write);
        EXPECT_TRUE(tint.state.color_write);
        EXPECT_FALSE(mask.state.depth_write);
        EXPECT_FALSE(tint.state.depth_write);
        EXPECT_TRUE(mask.state.depth_culling);
        EXPECT_TRUE(tint.state.depth_culling);
        EXPECT_EQ(mask.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
        EXPECT_EQ(tint.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
        ASSERT_EQ(mask.mesh.vertices.size(), 3u);
        ASSERT_EQ(tint.mesh.vertices.size(), 3u);
        EXPECT_EQ(mask.mesh.vertices.front().color, rgba8_srgb_to_linear(kColor));
        EXPECT_EQ(tint.mesh.vertices.front().color, rgba8_srgb_to_linear(kColor));
        last_mask = std::max(last_mask, mask.state.blend_order);
        first_color = std::min(first_color, tint.state.blend_order);
      }
    }
  }
  EXPECT_LT(last_mask, first_color);
}

TEST(OverlayFaceFillBuilderTests,
     BothPassesRetainTransformBindingsAndSemanticChangesInvalidateCachedPlans) {
  ViewportOverlayFrame frame;
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::HoverFace,
      .a = {-1, -1, -2}, .b = {1, -1, -2}, .c = {0, 1, -2},
      .color = rgba8(63, 127, 191, 70),
      .style_role = ViewportOverlayStyleRole::HoverFace,
      .source = ViewportOverlaySource::Hover,
      .element = {.object_id = 42, .vertex_id = 11, .edge_a = 12, .edge_b = 13}});
  const auto layer = ViewportOverlayLayer::HoverFace;
  const auto camera = test_overlay_camera();
  const auto initial_signature =
      OverlayRenderableBuilder::layer_build_signature(frame, camera, layer);
  auto moved_camera = camera;
  moved_camera.eye.x += 10.0f;
  EXPECT_EQ(OverlayRenderableBuilder::layer_build_signature(frame, moved_camera, layer),
            initial_signature);
  const auto plan = OverlayRenderableBuilder::build_layer(frame, camera, layer);
  ASSERT_EQ(plan.face_fill_batches.size(), 2u);
  for (const auto &batch : plan.face_fill_batches) {
    ASSERT_EQ(batch.mesh.position_stream_bindings.size(), 3u);
    EXPECT_EQ(batch.mesh.indices, (std::vector<std::uint32_t>{0u, 1u, 2u}));
    for (std::size_t index = 0u; index < 3u; ++index) {
      const auto &binding = batch.mesh.position_stream_bindings[index];
      EXPECT_EQ(binding.object_id, 42);
      EXPECT_EQ(binding.vertex_id, 11 + static_cast<int>(index));
      EXPECT_EQ(binding.first_vertex, index);
      EXPECT_EQ(binding.vertex_count, 1u);
      EXPECT_EQ(binding.stream, OverlayPositionStream::Primary);
      EXPECT_FLOAT_EQ(binding.base_position.x, batch.mesh.vertices[index].position.x);
      EXPECT_FLOAT_EQ(binding.base_position.y, batch.mesh.vertices[index].position.y);
      EXPECT_FLOAT_EQ(binding.base_position.z, batch.mesh.vertices[index].position.z);
    }
  }
  frame.triangles.front().style_role = ViewportOverlayStyleRole::RemoveSelectionFace;
  EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(frame, camera, layer),
            initial_signature);
  const auto remove = OverlayRenderableBuilder::build_layer(frame, camera, layer);
  ASSERT_EQ(remove.face_fill_batches.size(), 2u);
  for (const auto &batch : remove.face_fill_batches) {
    EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::RemoveSelection);
    EXPECT_EQ(batch.mesh.indices, plan.face_fill_batches.front().mesh.indices);
    EXPECT_EQ(batch.mesh.vertices.front().color,
              plan.face_fill_batches.front().mesh.vertices.front().color);
  }
}

TEST(OverlayFaceFillBuilderTests,
     TransparentHoverCannotClaimSelectionCoverageAndOpacityRestoresItsMask) {
  ViewportOverlayFrame frame;
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::SelectedVisibleFace,
      .a = {-1, -1, -2}, .b = {1, -1, -2}, .c = {0, 1, -2},
      .color = rgba8(63, 127, 191, 70),
      .style_role = ViewportOverlayStyleRole::SelectionFace});
  frame.triangles.push_back(frame.triangles.front());
  auto &hover = frame.triangles.back();
  hover.layer = ViewportOverlayLayer::HoverFace;
  hover.style_role = ViewportOverlayStyleRole::HoverFace;
  hover.color = rgba8(63, 127, 191, 0);
  const auto camera = test_overlay_camera();
  const auto selected_layer = ViewportOverlayLayer::SelectedVisibleFace;
  const auto hover_layer = ViewportOverlayLayer::HoverFace;
  const auto selection_signature =
      OverlayRenderableBuilder::layer_build_signature(frame, camera, selected_layer);
  const auto transparent_signature =
      OverlayRenderableBuilder::layer_build_signature(frame, camera, hover_layer);
  const auto selection = OverlayRenderableBuilder::build_layer(frame, camera, selected_layer);
  ASSERT_EQ(selection.face_fill_batches.size(), 2u);
  EXPECT_EQ(selection.face_fill_batches.front().state.face_fill_class,
            OverlayFaceFillClass::Selection);
  EXPECT_EQ(selection.face_fill_batches.front().state.face_fill_pass,
            OverlayFaceFillPass::Mask);
  const auto transparent = OverlayRenderableBuilder::build_layer(frame, camera, hover_layer);
  ASSERT_EQ(transparent.face_fill_batches.size(), 1u);
  const auto &batch = transparent.face_fill_batches.front();
  // None disables stencil for this color pass, so invisible hover cannot
  // consume coverage belonging to the selected face beneath it.
  EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::None);
  EXPECT_EQ(batch.state.face_fill_pass, OverlayFaceFillPass::Color);
  EXPECT_TRUE(batch.state.color_write);
  EXPECT_FALSE(batch.state.depth_write);
  ASSERT_EQ(batch.mesh.vertices.size(), 3u);
  EXPECT_EQ(batch.mesh.vertices.front().color >> 24u, 0u);

  hover.color = rgba8(63, 127, 191, 1);
  EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(frame, camera, hover_layer),
            transparent_signature);
  EXPECT_EQ(OverlayRenderableBuilder::layer_build_signature(frame, camera, selected_layer),
            selection_signature);
  const auto visible = OverlayRenderableBuilder::build_layer(frame, camera, hover_layer);
  ASSERT_EQ(visible.face_fill_batches.size(), 2u);
  EXPECT_EQ(visible.face_fill_batches.front().state.face_fill_pass,
            OverlayFaceFillPass::Mask);
  EXPECT_FALSE(visible.face_fill_batches.front().state.color_write);
  EXPECT_EQ(visible.face_fill_batches.back().state.face_fill_pass,
            OverlayFaceFillPass::Color);
  for (const auto &pass : visible.face_fill_batches) {
    EXPECT_EQ(pass.state.face_fill_class, OverlayFaceFillClass::Hover);
    EXPECT_EQ(pass.mesh.vertices.front().color >> 24u, 1u);
  }
}

TEST(OverlayFaceFillBuilderTests,
     HighlightedFacesPreserveDepthTestedComponentLayers) {
  ViewportOverlayFrame frame;
  for (const ViewportOverlayLayer layer : {
           ViewportOverlayLayer::SelectedVisibleFace,
           ViewportOverlayLayer::SelectedFaceMask,
           ViewportOverlayLayer::SelectedXrayFace,
           ViewportOverlayLayer::HoverFace,
       }) {
    frame.triangles.push_back({
        .layer = layer,
        .a = {-1.0f, -1.0f, -1.0f},
        .b = {0.0f, 1.0f, -1.0f},
        .c = {1.0f, -1.0f, -1.0f},
        .color = rgba8(31, 255, 102, 22),
    });
  }

  // The foreground face is a translucent highlight, while component wires
  // and vertices behind it still depend on the real scene's depth.
  for (const ViewportOverlayLayer layer : {
           ViewportOverlayLayer::SourceWire,
           ViewportOverlayLayer::SelectedFaceEdge,
           ViewportOverlayLayer::SelectedEdge,
           ViewportOverlayLayer::HoverFaceEdge,
           ViewportOverlayLayer::HoverEdge,
       }) {
    SCOPED_TRACE(static_cast<int>(layer));
    frame.lines.push_back({
        .layer = layer,
        .start = {-0.25f, 0.0f, -2.0f},
        .end = {0.25f, 0.0f, -2.0f},
        .color = kOverlaySourceWireColor,
        .width_pixels = 2.0f,
        .source = ViewportOverlaySource::ComponentSelection,
    });
    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), layer);
    ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
    const OverlayEditWireBatch &batch = plan.edit_wire_batches.front();
    EXPECT_FALSE(batch.mesh.empty());
    EXPECT_FALSE(batch.state.depth_write);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_GT(batch.state.blend_order,
              viewport_overlay_draw_order(ViewportOverlayLayer::HoverFace) *
                  64u);
  }

  for (const ViewportOverlayLayer layer : {
           ViewportOverlayLayer::UnselectedVertex,
           ViewportOverlayLayer::SelectedVertex,
           ViewportOverlayLayer::HoverVertex,
       }) {
    SCOPED_TRACE(static_cast<int>(layer));
    frame.points.push_back({
        .layer = layer,
        .position = {0.0f, 0.0f, -2.0f},
        .color = kOverlayVertexColor,
        .size_pixels = 6.0f,
        .source = ViewportOverlaySource::ComponentSelection,
        .outline_growth_pixels = 0.0f,
    });
    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        frame, test_overlay_camera(), layer);
    ASSERT_EQ(plan.point_batches.size(), 1u);
    const OverlayPointBatch &batch = plan.point_batches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_FALSE(batch.state.depth_write);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_GT(batch.state.blend_order,
              viewport_overlay_draw_order(ViewportOverlayLayer::HoverFace) *
                  64u);
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
     ClippedSourceFacesDoNotHideVertexMarkersBeyondTheCameraNearPlane) {
  for (const auto projection : {ViewportOverlayProjection::Perspective,
                                ViewportOverlayProjection::Orthographic}) {
    SCOPED_TRACE(static_cast<int>(projection));
    for (const auto layer : {ViewportOverlayLayer::UnselectedVertex,
                             ViewportOverlayLayer::SelectedVertex,
                             ViewportOverlayLayer::HoverVertex}) {
      SCOPED_TRACE(static_cast<int>(layer));
      auto camera = test_perspective_overlay_camera();
      camera.eye = {7.0f, -3.0f, 2.0f};
      camera.forward = {0.0f, 0.0f, -2.0f};
      camera.projection = projection;
      camera.orthographic_size = 4.0f;
      camera.near_plane = 2.0f;
      ViewportOverlayFrame frame;
      frame.points.push_back({
          .layer = layer,
          .position = camera.eye + QVec3{3.0f, 0.25f, -4.0f},
          .color = rgba8(255, 211, 31, 255),
          .size_pixels = 6.0f,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = 1, .vertex_id = 1},
          .outline_growth_pixels = 0.0f,
      });
      const auto add_occluder = [&](float view_depth) {
        append_source_wire_depth_stamp_quad(
            frame, 1, camera.eye + QVec3{-5.0f, -5.0f, -view_depth},
            camera.eye + QVec3{5.0f, -5.0f, -view_depth},
            camera.eye + QVec3{5.0f, 5.0f, -view_depth},
            camera.eye + QVec3{-5.0f, 5.0f, -view_depth}, false);
      };
      // The off-axis perspective ray hits this face more than two units from
      // the eye, but its view depth is still before the two-unit near plane.
      add_occluder(1.9f);
      auto plan = OverlayRenderableBuilder::build_layer(frame, camera, layer);
      ASSERT_EQ(plan.point_batches.size(), 1u);
      EXPECT_EQ(point_mesh_vertex_count(plan), 4u);
      EXPECT_EQ(plan.point_batches.front().state.depth_func,
                OverlayDepthFunc::GreaterOrEqual);
      EXPECT_TRUE(plan.point_batches.front().state.depth_culling);

      auto unclipped_camera = camera;
      unclipped_camera.near_plane = 1.0f;
      EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(frame, camera,
                                                                 layer),
                OverlayRenderableBuilder::layer_build_signature(
                    frame, unclipped_camera, layer));
      EXPECT_TRUE(OverlayRenderableBuilder::build_layer(
                      frame, unclipped_camera, layer).point_batches.empty());

      // Discarding a clipped hit must not discard a second valid occluder.
      const auto visible_signature =
          OverlayRenderableBuilder::layer_build_signature(frame, camera, layer);
      const auto tool_signature = OverlayRenderableBuilder::layer_build_signature(
          frame, camera, ViewportOverlayLayer::ToolHandle);
      add_occluder(2.1f);
      EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(frame, camera,
                                                                 layer),
                visible_signature);
      EXPECT_EQ(OverlayRenderableBuilder::layer_build_signature(
                    frame, camera, ViewportOverlayLayer::ToolHandle),
                tool_signature);
      EXPECT_TRUE(OverlayRenderableBuilder::build_layer(frame, camera, layer)
                      .point_batches.empty());
      frame.triangles.clear();
      EXPECT_NE(OverlayRenderableBuilder::layer_build_signature(frame, camera,
                                                                 layer),
                visible_signature);
      EXPECT_EQ(point_mesh_vertex_count(OverlayRenderableBuilder::build_layer(
                    frame, camera, layer)), 4u);
    }
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

TEST(OverlayRenderableBuilderTests, ExplicitWireSurfaceSupportsParticipateInSignaturesAndBindings) {
  ViewportOverlayFrame frame;
  frame.lines.push_back({.layer = ViewportOverlayLayer::SelectedEdge,
      .start = {-0.5f, 0.0f, -1.0f}, .end = {0.5f, 0.0f, -1.0f},
      .color = rgba8(79, 219, 235), .width_pixels = 1.5f,
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 9, .edge_a = 10, .edge_b = 11},
      .surface_support = {ViewportOverlaySurfaceSupport{{0.0f, 1.0f, -2.0f}, 12},
                          ViewportOverlaySurfaceSupport{{0.0f, -1.0f, -3.0f}, 13}}});
  const auto camera = test_overlay_camera();
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, camera, ViewportOverlayLayer::SelectedEdge);
  ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
  const auto &mesh = plan.edit_wire_batches[0].mesh;
  ASSERT_EQ(mesh.vertices.size(), 4u);
  EXPECT_FLOAT_EQ(mesh.vertices[0].support0.z, -2.0f);
  EXPECT_FLOAT_EQ(mesh.vertices[0].support1.z, -3.0f);
  ASSERT_EQ(mesh.position_stream_bindings.size(), 4u);
  EXPECT_EQ(mesh.position_stream_bindings[2].vertex_id, 12);
  EXPECT_EQ(mesh.position_stream_bindings[2].stream, OverlayPositionStream::Support0);
  EXPECT_EQ(mesh.position_stream_bindings[3].vertex_id, 13);
  EXPECT_EQ(mesh.position_stream_bindings[3].stream, OverlayPositionStream::Support1);
  const auto build_signature = OverlayRenderableBuilder::layer_build_signature(
      signatures, camera, ViewportOverlayLayer::SelectedEdge);
  auto moved_camera = camera;
  moved_camera.eye.x += 10.0f;
  EXPECT_EQ(build_signature, OverlayRenderableBuilder::layer_build_signature(
      signatures, moved_camera, ViewportOverlayLayer::SelectedEdge));

  frame.lines[0].surface_support[0].position.z -= 2.0f;
  auto changed = OverlayRenderableBuilder::frame_signatures(frame);
  EXPECT_NE(signatures.frame_signature, changed.frame_signature);
  EXPECT_NE(build_signature, OverlayRenderableBuilder::layer_build_signature(
      changed, camera, ViewportOverlayLayer::SelectedEdge));
  frame.lines[0].surface_support[0].vertex_id = 14;
  EXPECT_NE(changed.frame_signature, OverlayRenderableBuilder::frame_signature(frame));
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
                        {.position = {0.0f, 0.0f, 0.0f, 1.0f},
                         .color = rgba8(255, 255, 255, 255)},
                        {.position = {1.0f, 0.0f, 0.0f, 1.0f},
                         .color = rgba8(255, 255, 255, 255)},
                        {.position = {1.0f, 1.0f, 0.0f, 1.0f},
                         .color = rgba8(255, 255, 255, 255)},
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
     RepeatedGeometrySplitAndRestoreCoalescesDeviceChunks) {
  constexpr std::size_t kOccurrenceCount = 130u;
  constexpr std::size_t kDeviceCapacity = 64u;
  const RenderInstancePrimitiveKey shared_key{
      .geometry_payload = 17u,
      .material_payload = 29u,
      .index_count = 3u,
  };
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(kOccurrenceCount);
  for (std::size_t index = 0u; index < kOccurrenceCount; ++index) {
    QMat4 transform;
    transform.values[12] = static_cast<float>(index * 2u);
    QMat4 sampling;
    sampling.values[13] = static_cast<float>(index);
    occurrences.push_back({
        .key = shared_key,
        .occurrence = index + 1u,
        .world_transform = transform,
        .sampling_from_local = sampling,
        .local_bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}, .present = true},
        .visible = index % 3u != 0u,
    });
  }
  RenderInstanceBatchPlan retained =
      plan_render_instance_batches(occurrences, kDeviceCapacity);
  ASSERT_EQ(retained.chunks.size(), 3u);
  for (std::size_t edit = 0u; edit < 3u; ++edit) {
    SCOPED_TRACE(edit);
    for (std::size_t index = 0u; index < occurrences.size(); ++index) {
      occurrences[index].key.geometry_payload = 100u + index;
    }
    const auto split = plan_render_instance_batches(occurrences, kDeviceCapacity, &retained);
    ASSERT_EQ(split.chunks.size(), kOccurrenceCount);
    EXPECT_FALSE(split.work.retained_layout);
    EXPECT_EQ(split.work.morton_ordered_occurrence_count, kOccurrenceCount);
    EXPECT_EQ(classify_instance_batch_update(retained, split),
              RenderInstanceBatchUpdateKind::Rebuild);

    for (auto &occurrence : occurrences) {
      occurrence.key = shared_key;
    }
    const auto restored = plan_render_instance_batches(occurrences, kDeviceCapacity, &split);
    ASSERT_EQ(restored.chunks.size(), 3u);
    EXPECT_FALSE(restored.work.retained_layout);
    EXPECT_EQ(restored.work.morton_ordered_occurrence_count, kOccurrenceCount);
    EXPECT_EQ(restored.work.chunk_storage_growth_count, 1u);
    EXPECT_EQ(restored.work.relocated_chunk_count, 0u);
    EXPECT_EQ(classify_instance_batch_update(split, restored),
              RenderInstanceBatchUpdateKind::Rebuild);
    for (const auto &chunk : restored.chunks) {
      EXPECT_EQ(chunk.key, shared_key);
      ASSERT_LE(chunk.occurrences.size(), kDeviceCapacity);
      for (std::size_t slot = 0u; slot < chunk.occurrences.size(); ++slot) {
        const auto index = chunk.occurrences[slot] - 1u;
        EXPECT_FLOAT_EQ(chunk.world_transforms[slot].values[12], static_cast<float>(index * 2u));
        EXPECT_FLOAT_EQ(chunk.sampling_from_local[slot].values[13], static_cast<float>(index));
        EXPECT_EQ(chunk.visibility[slot], index % 3u != 0u ? 1u : 0u);
      }
    }
    retained = plan_render_instance_batches(occurrences, kDeviceCapacity, &restored);
    EXPECT_TRUE(retained.work.retained_layout);
    EXPECT_EQ(retained.work.morton_ordered_occurrence_count, 0u);
    EXPECT_EQ(classify_instance_batch_update(restored, retained),
              RenderInstanceBatchUpdateKind::Unchanged);
  }
}

TEST(RenderInstanceBatchPlannerTests,
     CompatibleGeometryMergesKeepAlreadyFullDeviceChunks) {
  constexpr std::size_t kDeviceCapacity = 4u;
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  for (std::size_t index = 0u; index < kDeviceCapacity * 2u; ++index) {
    occurrences.push_back({
        .key = {.geometry_payload = index / kDeviceCapacity + 1u,
                .material_payload = 3u, .index_count = 6u},
        .occurrence = index + 1u,
    });
  }
  const auto before = plan_render_instance_batches(occurrences, kDeviceCapacity);
  ASSERT_EQ(before.chunks.size(), 2u);
  for (auto &occurrence : occurrences) {
    occurrence.key.geometry_payload = 7u;
  }
  const auto merged = plan_render_instance_batches(occurrences, kDeviceCapacity, &before);
  ASSERT_EQ(merged.chunks.size(), 2u);
  EXPECT_TRUE(merged.work.retained_layout);
  EXPECT_EQ(merged.work.morton_ordered_occurrence_count, 0u);
  EXPECT_EQ(classify_instance_batch_update(before, merged), RenderInstanceBatchUpdateKind::Rebuild);
  for (std::size_t index = 0u; index < merged.chunks.size(); ++index) {
    EXPECT_EQ(merged.chunks[index].key.geometry_payload, 7u);
    EXPECT_EQ(merged.chunks[index].occurrences, before.chunks[index].occurrences);
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
            .location = {static_cast<float>(index) * 2.0f, 0.0f, 0.0f},
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
      curve, {.location = {10.0f, 0.0f, 0.0f}}));
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
  ASSERT_TRUE(document.set_transform(mesh, {.location = {3.0f, 0.0f, 0.0f}}));
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
     PrimaryAndCoSelectedMeshesKeepDistinctWireStylesWithSharedGeometry) {
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
      .active_selection_wire_color = viewport_active_selection_color(normal_color),
      .selection_edge_logical_width_pixels = 3.5f,
      .selection_edge_line_width_pixels = 3.5f,
      .active_selection_logical_width_pixels = 6.0f,
      .active_selection_line_width_pixels = 6.0f,
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
  EXPECT_EQ(plan.unique_geometry_style_count, 2u);
  ASSERT_EQ(plan.batches.chunks.size(), 2u);
  EXPECT_EQ(plan.batches.chunks.front().occurrences.size(), 1u);

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, RenderOverlaySelectionMode::Object, snapshot);
  ASSERT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 6u);
  EXPECT_EQ(plan.batches.chunks[0].key.geometry_payload, plan.batches.chunks[1].key.geometry_payload);
  EXPECT_NE(plan.batches.chunks[0].key.index_offset, plan.batches.chunks[1].key.index_offset);
  EXPECT_EQ(std::ranges::count_if(frame.lines, [&](const auto &line) {
    return line.style_role == ViewportOverlayStyleRole::SelectionWire &&
           line.color == normal_color && line.width_pixels == 3.5f;
  }), 3);
  EXPECT_EQ(std::ranges::count_if(frame.lines, [&](const auto &line) {
    return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire &&
           line.color == viewport_active_selection_color(normal_color) && line.width_pixels == 6.0f;
  }), 3);
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
      member, TransformComponent{.location = {2.0f, 0.0f, 0.0f}});
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 0.0f, 0.0f}});
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
      member, TransformComponent{.location = {2.0f, 0.0f, 0.0f}});

  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 0.0f, 0.0f}});
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
  EXPECT_FALSE(fill.active_selected);
  EXPECT_FALSE(wire.active_selected);
  EXPECT_EQ(wire.wire_color, kOverlaySelectionWireColor);
  EXPECT_FLOAT_EQ(wire.wire_width_pixels,
                  kOverlaySelectionWireLineWidthPixels);

  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::ActiveSelection,
                                {0.2f, 0.4f, 0.6f, 1.0f}));
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
  const auto styled = build_render_overlay_selected_object_snapshot(document, assets, true,
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings, 2.0f));
  ASSERT_EQ(styled.instance_selection_wires.size(), 1u);
  EXPECT_EQ(styled.instance_selection_wires.front().wire_color, kOverlaySelectionWireColor);
  EXPECT_FLOAT_EQ(styled.instance_selection_wires.front().logical_wire_width_pixels,
                  kOverlaySelectionWireLineWidthPixels);
  EXPECT_FLOAT_EQ(styled.instance_selection_wires.front().wire_width_pixels,
                  kOverlaySelectionWireLineWidthPixels * 2.0f);
  EXPECT_EQ(styled.instance_selection_wires.front().geometry_payload, wire.geometry_payload);
  EXPECT_EQ(styled.instance_selection_wires.front().world_transform.values, wire.world_transform.values);
}

TEST(RenderInstanceEditPresentationTests,
     MultipleDefinitionMembersKeepOneOccurrenceSemanticallySingleton) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const EntityId definition = document.create_empty("Definition");
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  for (int index = 0; index < 2; ++index) {
    const EntityId member = document.create_entity(
        "Member " + std::to_string(index), EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(member, {.mesh = mesh}));
    ASSERT_TRUE(document.set_parent(member, definition, false));
    ASSERT_TRUE(document.set_transform(member, {
        .location = {static_cast<float>(index) * 2.0f, 0.0f, 0.0f}}));
  }
  const EntityId occurrence = document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(occurrence, {.definition = definition}));
  const EntityId peer = document.create_empty("Peer");
  ASSERT_TRUE(document.set_selected(occurrence, true));
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
  const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings, 2.0f);
  for (const bool multiple : {false, true, false}) {
    ASSERT_TRUE(document.set_selected(peer, multiple));
    ASSERT_TRUE(document.set_active_selection(occurrence));
    EXPECT_EQ(document.selected_entities().size(), multiple ? 2u : 1u);
    const auto snapshot = build_render_overlay_selected_object_snapshot(
        document, assets, true, style);
    ASSERT_EQ(snapshot.instance_selection_fills.size(), 2u);
    ASSERT_EQ(snapshot.instance_selection_wires.size(), 2u);
    EXPECT_TRUE(std::ranges::all_of(snapshot.instance_selection_fills,
        [multiple](const auto &record) { return record.active_selected == multiple; }));
    EXPECT_TRUE(std::ranges::all_of(snapshot.instance_selection_wires,
        [multiple](const auto &record) {
          return record.active_selected == multiple &&
                 record.wire_color == (multiple ? viewport_active_selection_color(kOverlaySelectionWireColor)
                                                : kOverlaySelectionWireColor) &&
                 record.logical_wire_width_pixels == (multiple ? 6.0f : 2.0f) &&
                 record.wire_width_pixels == (multiple ? 12.0f : 4.0f);
        }));
    const auto scene = build_render_overlay_scene_wireframe_snapshot(document, assets, 1u, style);
    ASSERT_EQ(scene.meshes.size(), 2u);
    EXPECT_TRUE(std::ranges::all_of(scene.meshes,
        [multiple](const auto &record) { return record.active_selected == multiple; }));
    const auto frame = ViewportSceneOverlayBuilder::build_wireframe_frame(
        {}, RenderOverlaySelectionMode::Object, scene);
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 6u);
    EXPECT_TRUE(std::ranges::all_of(frame.lines, [multiple](const auto &line) {
      return line.style_role == (multiple ? ViewportOverlayStyleRole::ActiveSelectionWire
                                          : ViewportOverlayStyleRole::SelectionWire) &&
             line.width_pixels == (multiple ? 12.0f : 4.0f);
    }));
  }
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
            .location = {static_cast<float>(index) * 10.0f, 0.0f, 0.0f},
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
        return wire.wire_color == (wire.active_selected
            ? viewport_active_selection_color(kOverlaySelectionWireColor)
            : kOverlaySelectionWireColor);
      }));

  const InstanceSelectionOverlayBatchPlans batches =
      plan_instance_selection_overlay_batches(selected, 64u);
  ASSERT_EQ(batches.fills.chunks.size(), 2u);
  ASSERT_EQ(batches.wires.chunks.size(), 2u);
  EXPECT_EQ(batches.fills.chunks.front().occurrences.size(), 1u);
  EXPECT_EQ(batches.wires.chunks.front().occurrences.size(), 1u);
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
      member, TransformComponent{.location = {2.0f, 0.0f, 0.0f}});

  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 0.0f, 0.0f}});
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
      occurrence, TransformComponent{.location = {30.0f, 0.0f, 0.0f}});
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
      definition, TransformComponent{.location = {100.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member =
      document.create_entity("Canonical Mesh", EntityKind::Mesh);
  document.set_mesh(member, MeshComponent{.mesh = 1u});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  document.set_transform(
      member, TransformComponent{.location = {2.0f, 0.0f, 0.0f}});
  const EntityId occurrence =
      document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));
  document.set_transform(
      occurrence, TransformComponent{.location = {10.0f, 0.0f, 0.0f}});

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
      entity, TransformComponent{.location = {10.0F, 20.0F, 30.0F}});
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

TEST(RenderWireframeOnShadedTests, MasterAndMeshFlagsPreserveSelectionAndRetainedExtraction) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_authored_quad_mesh());
  const EntityId enabled = document.create_entity("Enabled", EntityKind::Mesh);
  const EntityId disabled = document.create_entity("Disabled", EntityKind::Mesh);
  const EntityId selected = document.create_entity("Selected", EntityKind::Mesh);
  document.set_mesh(enabled, {.mesh = asset});
  document.set_mesh(disabled, {.mesh = asset, .wireframe_on_shaded = false});
  document.set_mesh(selected, {.mesh = asset});
  ASSERT_TRUE(document.set_selected(selected, true));
  const auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(ViewportSettings{});
  RenderOverlaySnapshotCache cache;
  const auto update = [&](std::uint8_t shaded_mask, std::uint8_t wireframe_mask = 0u) {
    return cache.update_persistent(document, assets, true, wireframe_mask, style,
        {}, nullptr, false, {}, {}, true, false, {}, shaded_mask);
  };
  EXPECT_TRUE(update(0u).scene_wireframe->meshes.empty());
  const auto shown = update(viewport_grid_layer_mask(1u));
  ASSERT_NE(shown.scene_wireframe, nullptr);
  const auto count_shaded = [](const auto &snapshot) {
    return std::ranges::count_if(snapshot.meshes, [](const auto &mesh) {
      return mesh.shaded_wire;
    });
  };
  EXPECT_EQ(count_shaded(*shown.scene_wireframe), 2);
  const auto plan = plan_scene_wireframe_overlay_batches(*shown.scene_wireframe, 64u);
  EXPECT_EQ(plan.participant_count, 2u);
  ASSERT_EQ(plan.batches.chunks.size(), 1u);
  EXPECT_EQ(plan.batches.chunks.front().key.layer_mask,
            static_cast<std::uint8_t>(ViewportOverlayLayer::WireframeBase));
  for (const auto &mesh : shown.scene_wireframe->meshes) {
    ASSERT_EQ(mesh.wire_segments.size(), 4u) << "A quad has no triangulation wire.";
    if (mesh.shaded_wire) EXPECT_NE(mesh.entity, disabled);
  }
  ASSERT_EQ(shown.selected_object->meshes.size(), 1u);
  EXPECT_EQ(shown.selected_object->meshes.front().entity, selected);
  const auto builds = cache.stats().scene_wireframe_build_count;
  const auto *retained = shown.scene_wireframe->meshes.data();
  EXPECT_FALSE(update(viewport_grid_layer_mask(1u)).changed());
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, builds);
  EXPECT_EQ(update(viewport_grid_layer_mask(1u)).scene_wireframe->meshes.data(), retained);

  document.set_mesh(selected, {.mesh = asset, .wireframe_on_shaded = false});
  const auto changed = update(viewport_grid_layer_mask(1u));
  EXPECT_TRUE(changed.scene_wireframe_changed);
  EXPECT_EQ(count_shaded(*changed.scene_wireframe), 1);
  ASSERT_EQ(changed.selected_object->meshes.size(), 1u);
  EXPECT_EQ(changed.selected_object->meshes.front().entity, selected);
  const auto wireframe = update(0u, viewport_grid_layer_mask(0u));
  const auto wireframe_plan = plan_scene_wireframe_overlay_batches(*wireframe.scene_wireframe, 64u);
  EXPECT_EQ(wireframe_plan.participant_count, 3u);
  ASSERT_EQ(wireframe.scene_wireframe->meshes.size(), 3u);
  for (const auto entity : {enabled, disabled, selected}) {
    EXPECT_EQ(std::ranges::count_if(wireframe.scene_wireframe->meshes,
        [entity](const auto &mesh) { return mesh.entity == entity; }), 1);
  }
  for (const auto &mesh : wireframe.scene_wireframe->meshes) {
    EXPECT_FALSE(mesh.selected);
    EXPECT_FALSE(mesh.active_selected);
    EXPECT_EQ(mesh.wire_segments.size(), 4u);
  }
  ASSERT_EQ(wireframe_plan.batches.chunks.size(), 1u);
  EXPECT_EQ(wireframe_plan.batches.chunks.front().key.layer_mask,
            static_cast<std::uint8_t>(ViewportOverlayLayer::WireframeXray));
  const auto wireframe_builds = cache.stats().scene_wireframe_build_count;
  EXPECT_FALSE(update(0u, viewport_grid_layer_mask(0u)).changed());
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, wireframe_builds);
  const auto solid = update(0u);
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(*solid.scene_wireframe, 64u).participant_count, 0u);
  EXPECT_TRUE(update(0u, viewport_grid_layer_mask(0u)).scene_wireframe_changed);
  EXPECT_EQ(count_shaded(*wireframe.scene_wireframe), 0);
  const auto editing = build_render_overlay_scene_wireframe_snapshot(document, assets,
      0u, style, {}, nullptr, true, {}, false, viewport_grid_layer_mask(1u));
  EXPECT_EQ(count_shaded(editing), 1);
  EXPECT_EQ(std::ranges::count_if(editing.meshes, [selected](const auto &mesh) {
    return mesh.entity == selected && mesh.selected && !mesh.shaded_wire;
  }), 1) << "Disabling the display preference must not suppress editing wire.";
}

TEST(RenderWireframeOnShadedTests, FormalInstancesShareAuthoredEdgesAndRetainReflections) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_authored_quad_mesh());
  const EntityId definition = document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const EntityId member = document.create_entity("Member", EntityKind::Mesh);
  document.set_mesh(member, {.mesh = asset});
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId first = document.create_entity("First", EntityKind::Instance);
  const EntityId reflected = document.create_entity("Reflected", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(first, {.definition = definition}));
  ASSERT_TRUE(document.set_instance_occurrence(reflected, {.definition = definition}));
  document.set_transform(reflected, {.scale = {-1.0f, 1.0f, 1.0f}});
  const auto snapshot = build_render_overlay_scene_wireframe_snapshot(document, assets,
      0u, ViewportPersistentOverlayStyleSnapshot{}, {}, nullptr, false, {}, false,
      viewport_grid_layer_mask(2u));
  std::vector<const RenderOverlayMeshSnapshot *> shaded;
  for (const auto &mesh : snapshot.meshes) if (mesh.shaded_wire) shaded.push_back(&mesh);
  ASSERT_EQ(shaded.size(), 2u);
  EXPECT_EQ(shaded[0]->wire_geometry_payload, shaded[1]->wire_geometry_payload);
  EXPECT_EQ(shaded[0]->wire_segments.size(), 4u);
  EXPECT_EQ(shaded[1]->member, member);
  const auto reflected_mesh = std::ranges::find_if(shaded, [reflected](const auto *mesh) {
    return mesh->entity == reflected;
  });
  ASSERT_NE(reflected_mesh, shaded.end());
  EXPECT_FLOAT_EQ((*reflected_mesh)->world_transform.values[0], -1.0f);
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(snapshot, 64u).participant_count, 2u);
  document.set_mesh(member, {.mesh = asset, .wireframe_on_shaded = false});
  const auto hidden = build_render_overlay_scene_wireframe_snapshot(document, assets,
      0u, ViewportPersistentOverlayStyleSnapshot{}, {}, nullptr, false, {}, false,
      viewport_grid_layer_mask(2u));
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(hidden, 64u).participant_count, 0u);
}

TEST(RenderWireframeOnShadedTests, DynamicPreviewUsesEffectivePositionsAndRetainedAuthoredTopology) {
  MeshAsset mesh = make_authored_quad_mesh();
  auto &vertices = mesh.edit_geometry().vertices;
  for (std::size_t index = 0u; index < vertices.size(); ++index) {
    vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1u);
  }
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : vertices) positions->push_back(vertex.position + QVec3{0.0f, 3.0f, 0.0f});
  auto projection = std::make_shared<MeshGeometryProjection>();
  projection->source_geometry = mesh.geometry_payload();
  projection->positions = positions;
  std::vector<RenderToolPreviewMeshRecord> preview{{
      .source_entity = 7u,
      .mesh = mesh,
      .dynamic_positions = true,
      .geometry_projection = projection,
  }};
  auto snapshot = build_render_tool_preview_wireframe_snapshot(preview, true);
  snapshot.layer_mask = viewport_grid_layer_mask(1u);
  snapshot.wireframe_on_shaded_layer_mask = viewport_grid_layer_mask(0u);
  ASSERT_EQ(snapshot.meshes.size(), 2u);
  EXPECT_FALSE(snapshot.meshes.front().shaded_wire);
  const auto &wire = snapshot.meshes.back();
  ASSERT_TRUE(wire.shaded_wire);
  EXPECT_EQ(snapshot.meshes.front().entity, 7u);
  EXPECT_EQ(snapshot.meshes.front().wire_geometry_payload, wire.wire_geometry_payload);
  ASSERT_EQ(wire.wire_segments.size(), 4u);
  EXPECT_EQ(wire.geometry_projection, projection);
  EXPECT_EQ(wire.geometry_payload, projection->source_geometry);
  EXPECT_EQ(wire.vertices.data(), projection->source_geometry->vertices.data());
  for (const auto &edge : wire.wire_segments) {
    EXPECT_FLOAT_EQ(edge.start.y, 3.0f);
    EXPECT_FLOAT_EQ(edge.end.y, 3.0f);
  }
  EXPECT_FLOAT_EQ(mesh.geometry_vertices().front().position.y, 0.0f);
  const auto plan = plan_scene_wireframe_overlay_batches(snapshot, 64u);
  ASSERT_EQ(plan.participant_count, 2u);
  ASSERT_EQ(plan.batches.chunks.size(), 2u);
  for (const auto &chunk : plan.batches.chunks) {
    EXPECT_FLOAT_EQ(chunk.world_bounds.min.y, 3.0f);
  }
  EXPECT_TRUE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, snapshot.meshes));
  auto moved_positions = std::make_shared<std::vector<QVec3>>(*positions);
  for (auto &position : *moved_positions) position.y = 5.0f;
  auto moved_projection = std::make_shared<MeshGeometryProjection>();
  moved_projection->source_geometry = projection->source_geometry;
  moved_projection->positions = moved_positions;
  preview.front().geometry_projection = moved_projection;
  const auto moved = build_render_tool_preview_wireframe_snapshot(preview, true);
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, moved.meshes));
  ASSERT_EQ(moved.meshes.size(), 2u);
  for (const auto &record : moved.meshes) {
    EXPECT_FLOAT_EQ(record.wire_segments.front().start.y, 5.0f);
    EXPECT_EQ(record.geometry_projection, moved_projection);
  }
  EXPECT_FLOAT_EQ(snapshot.meshes.front().wire_segments.front().start.y, 3.0f);
  preview.front().wireframe_on_shaded = false;
  for (const bool include_shaded : {false, true}) {
    const auto ordinary = build_render_tool_preview_wireframe_snapshot(preview, include_shaded);
    ASSERT_EQ(ordinary.meshes.size(), 1u);
    EXPECT_FALSE(ordinary.meshes.front().shaded_wire);
    EXPECT_EQ(ordinary.meshes.front().entity, 7u);
    EXPECT_FLOAT_EQ(ordinary.meshes.front().wire_segments.front().start.y, 5.0f);
  }
}

TEST(RenderWireframeOnShadedTests, PreviewPreparationGatesOrdinaryAndShadedRolesIndependently) {
  for (const bool dynamic : {false, true}) {
    for (const bool instanced : {false, true}) {
      SCOPED_TRACE(dynamic);
      SCOPED_TRACE(instanced);
      std::vector<RenderToolPreviewMeshRecord> records{{
          .source_entity = 9u,
          .mesh = make_authored_quad_mesh(),
          .dynamic_positions = dynamic}};
      if (instanced) records.front().instance_transforms = {QMat4{}, QMat4{}};
      const std::size_t placements = instanced ? 2u : 1u;
      for (const bool include_ordinary : {false, true}) {
        for (const bool include_shaded : {false, true}) {
          SCOPED_TRACE(include_ordinary);
          SCOPED_TRACE(include_shaded);
          const auto snapshot = build_render_tool_preview_wireframe_snapshot(
              records, include_shaded, include_ordinary);
          EXPECT_EQ(snapshot.meshes.size(), placements *
              (static_cast<std::size_t>(include_ordinary) +
               static_cast<std::size_t>(include_shaded)));
          EXPECT_EQ(std::ranges::count_if(snapshot.meshes,
              [](const auto &mesh) { return mesh.shaded_wire; }),
              include_shaded ? placements : 0u);
          for (const auto &mesh : snapshot.meshes) {
            EXPECT_EQ(mesh.entity, 9u);
            EXPECT_TRUE(mesh.has_authored_polygon_geometry());
            EXPECT_EQ(mesh.wire_segments.size(), 4u);
          }
        }
      }
      records.front().wireframe_on_shaded = false;
      EXPECT_TRUE(build_render_tool_preview_wireframe_snapshot(records, true, false).meshes.empty());
      const auto ordinary = build_render_tool_preview_wireframe_snapshot(records, true, true);
      ASSERT_EQ(ordinary.meshes.size(), placements);
      EXPECT_TRUE(std::ranges::none_of(ordinary.meshes,
          [](const auto &mesh) { return mesh.shaded_wire; }));
    }
  }
}

TEST(RenderWireframeOnShadedTests, DefaultAndOverrideColorsRetainGlobalOpacityAndSelectionStyle) {
  ViewportSettings settings;
  const auto initial = ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
  RenderOverlayMeshSnapshot inherited{.shaded_wire = true};
  const auto black = scene_wireframe_shaded_color(inherited, initial);
  EXPECT_FLOAT_EQ(black.r, 0.0f);
  EXPECT_FLOAT_EQ(black.g, 0.0f);
  EXPECT_FLOAT_EQ(black.b, 0.0f);
  EXPECT_FLOAT_EQ(black.a, 1.0f);
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::WireframeOnShaded,
                                {0.25f, 0.5f, 0.75f, 0.4f}));
  const auto changed = ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
  const auto global = scene_wireframe_shaded_color(inherited, changed);
  EXPECT_FLOAT_EQ(global.r, 0.25f);
  EXPECT_FLOAT_EQ(global.g, 0.5f);
  EXPECT_FLOAT_EQ(global.b, 0.75f);
  EXPECT_FLOAT_EQ(global.a, 0.4f);
  inherited.wireframe_on_shaded_color = QVec3{0.125f, 0.375f, 0.625f};
  const auto override_color = scene_wireframe_shaded_color(inherited, changed);
  EXPECT_FLOAT_EQ(override_color.r, 0.125f);
  EXPECT_FLOAT_EQ(override_color.g, 0.375f);
  EXPECT_FLOAT_EQ(override_color.b, 0.625f);
  EXPECT_FLOAT_EQ(override_color.a, 0.4f);
  EXPECT_EQ(initial.wire_signature, changed.wire_signature);
  EXPECT_EQ(initial.face_signature, changed.face_signature);
  EXPECT_EQ(initial.selection_wire_color, changed.selection_wire_color);
  EXPECT_EQ(initial.active_selection_wire_color, changed.active_selection_wire_color);
  EXPECT_FALSE(viewport_persistent_overlay_wire_styles_match(initial, changed));
  ViewportPersistentOverlayRasterGroupSet panes;
  ASSERT_TRUE(panes.add(viewport_grid_layer_mask(0u), initial));
  ASSERT_TRUE(panes.add(viewport_grid_layer_mask(1u), changed));
  ASSERT_EQ(panes.size(), 2u);
  EXPECT_FLOAT_EQ(scene_wireframe_shaded_color({}, panes.groups()[0].style).r, 0.0f);
  EXPECT_FLOAT_EQ(scene_wireframe_shaded_color({}, panes.groups()[1].style).r, 0.25f);
}

TEST(RenderWireframeOnShadedTests, ColorChangesReuseAuthoredTopologyAndSeparateExactInstanceOverrides) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_authored_quad_mesh());
  const EntityId inherited = document.create_entity("Inherited", EntityKind::Mesh);
  const EntityId red = document.create_entity("Red", EntityKind::Mesh);
  const EntityId blue = document.create_entity("Blue", EntityKind::Mesh);
  document.set_mesh(inherited, {.mesh = asset});
  document.set_mesh(red, {.mesh = asset, .wireframe_on_shaded_color = QVec3{1.0f, 0.0f, 0.0f}});
  document.set_mesh(blue, {.mesh = asset, .wireframe_on_shaded_color = QVec3{0.0f, 0.0f, 1.0f}});
  ViewportSettings settings;
  auto style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
  RenderOverlaySnapshotCache cache;
  const auto update = [&]() {
    return cache.update_persistent(document, assets, false, 0u, style,
        {}, nullptr, false, {}, {}, false, false, {}, viewport_grid_layer_mask(0u));
  };
  const auto first = update();
  ASSERT_NE(first.scene_wireframe, nullptr);
  const auto initial_plan = plan_scene_wireframe_overlay_batches(*first.scene_wireframe, 64u);
  EXPECT_EQ(initial_plan.participant_count, 3u);
  EXPECT_EQ(initial_plan.unique_geometry_style_count, 1u);
  ASSERT_EQ(initial_plan.batches.chunks.size(), 3u);
  const auto *retained_meshes = first.scene_wireframe->meshes.data();
  const auto payload = first.scene_wireframe->meshes.front().wire_geometry_payload;
  const auto source_geometry = first.scene_wireframe->meshes.front().geometry_payload;
  const auto builds = cache.stats().scene_wireframe_build_count;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::WireframeOnShaded,
                                {0.25f, 0.5f, 0.75f, 0.5f}));
  style = ViewportOverlayStyleResolver::persistent_style_snapshot(settings);
  const auto recolored = update();
  EXPECT_TRUE(recolored.scene_wireframe_changed);
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, builds);
  EXPECT_EQ(recolored.scene_wireframe->meshes.data(), retained_meshes);
  const auto recolored_plan = plan_scene_wireframe_overlay_batches(*recolored.scene_wireframe, 64u);
  EXPECT_EQ(classify_instance_batch_update(initial_plan.batches, recolored_plan.batches),
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_FALSE(update().changed());

  document.set_mesh(red, {.mesh = asset,
      .wireframe_on_shaded_color = QVec3{0.0f, 0.0f, 0.99999f}});
  const auto changed = update();
  const auto changed_plan = plan_scene_wireframe_overlay_batches(*changed.scene_wireframe, 64u);
  EXPECT_EQ(changed_plan.unique_geometry_style_count, 1u);
  EXPECT_EQ(changed_plan.batches.chunks.size(), 3u)
      << "Distinct normalized RGB values must not collapse through byte quantization.";
  for (const auto &mesh : changed.scene_wireframe->meshes) {
    EXPECT_EQ(mesh.wire_geometry_payload, payload);
    EXPECT_EQ(mesh.geometry_payload, source_geometry);
    ASSERT_EQ(mesh.wire_segments.size(), 4u);
    if (!mesh.shaded_wire) continue;
    const auto color = scene_wireframe_shaded_color(mesh, style);
    EXPECT_FLOAT_EQ(color.a, 0.5f);
    if (mesh.entity == inherited) EXPECT_FLOAT_EQ(color.r, 0.25f);
    if (mesh.entity == red) EXPECT_FLOAT_EQ(color.b, 0.99999f);
    if (mesh.entity == blue) EXPECT_FLOAT_EQ(color.b, 1.0f);
  }
}

TEST(RenderWireframeOnShadedTests, PreviewColorsFollowOverridesAndGlobalPaneOpacity) {
  MeshAsset mesh = make_authored_quad_mesh();
  std::vector<RenderToolPreviewMeshRecord> previews{{
      .source_entity = 4u,
      .mesh = mesh,
      .instance_transforms = {QMat4{}, QMat4{}},
      .wireframe_on_shaded_color = QVec3{0.2f, 0.4f, 0.6f},
  }};
  const auto snapshot = build_render_tool_preview_wireframe_snapshot(previews, true);
  ASSERT_EQ(snapshot.meshes.size(), 4u);
  const auto original_wire = snapshot.meshes.front().wire_geometry_payload;
  ASSERT_NE(original_wire, nullptr);
  ViewportPersistentOverlayStyleSnapshot style;
  style.wireframe_on_shaded_color = {0.9f, 0.8f, 0.7f, 0.3f};
  std::size_t shaded_count = 0u;
  for (const auto &record : snapshot.meshes) {
    EXPECT_EQ(record.entity, 4u);
    EXPECT_EQ(record.wire_geometry_payload, original_wire);
    if (!record.shaded_wire) continue;
    ++shaded_count;
    const auto color = scene_wireframe_shaded_color(record, style);
    EXPECT_FLOAT_EQ(color.r, 0.2f);
    EXPECT_FLOAT_EQ(color.g, 0.4f);
    EXPECT_FLOAT_EQ(color.b, 0.6f);
    EXPECT_FLOAT_EQ(color.a, 0.3f);
  }
  EXPECT_EQ(shaded_count, 2u);
  previews.front().instance_transforms.clear();
  previews.front().dynamic_positions = true;
  previews.front().wireframe_on_shaded_color.reset();
  const auto dynamic = build_render_tool_preview_wireframe_snapshot(previews, true);
  ASSERT_EQ(dynamic.meshes.size(), 2u);
  EXPECT_EQ(dynamic.meshes.front().entity, 4u);
  EXPECT_FALSE(dynamic.meshes.front().shaded_wire);
  EXPECT_TRUE(dynamic.meshes.back().shaded_wire);
  EXPECT_EQ(dynamic.meshes.front().wire_geometry_payload, dynamic.meshes.back().wire_geometry_payload);
  const auto inherited = scene_wireframe_shaded_color(dynamic.meshes.back(), style);
  EXPECT_FLOAT_EQ(inherited.r, 0.9f);
  EXPECT_FLOAT_EQ(inherited.g, 0.8f);
  EXPECT_FLOAT_EQ(inherited.b, 0.7f);
  EXPECT_FLOAT_EQ(inherited.a, 0.3f);
  const auto dynamic_wire = dynamic.meshes.front().wire_geometry_payload;
  ASSERT_NE(dynamic_wire, nullptr);
  EXPECT_EQ(dynamic.meshes.front().geometry_payload, snapshot.meshes.front().geometry_payload);
  EXPECT_EQ(dynamic_wire->content_id, original_wire->content_id);
  EXPECT_TRUE(dynamic_wire->authored);
  EXPECT_TRUE(original_wire->authored);
  ASSERT_EQ(original_wire->segments.size(), 4u);
  ASSERT_EQ(dynamic_wire->segments.size(), original_wire->segments.size());
  const auto same_point = [](QVec3 first, QVec3 second) {
    return first.x == second.x && first.y == second.y && first.z == second.z;
  };
  for (std::size_t index = 0u; index < original_wire->segments.size(); ++index) {
    const auto &original_segment = original_wire->segments[index];
    const auto &dynamic_segment = dynamic_wire->segments[index];
    EXPECT_TRUE(same_point(dynamic_segment.start, original_segment.start));
    EXPECT_TRUE(same_point(dynamic_segment.end, original_segment.end));
    ASSERT_EQ(dynamic_segment.surface_support_count, original_segment.surface_support_count);
    for (std::size_t support = 0u; support < original_segment.surface_support_count; ++support) {
      EXPECT_TRUE(same_point(dynamic_segment.surface_support[support],
                             original_segment.surface_support[support]));
    }
  }
}

TEST(MeshWireGeometrySupportTests, NonplanarBoundaryUsesItsRenderedTriangle) {
  MeshAsset mesh = make_authored_quad_mesh();
  auto &geometry = mesh.edit_geometry();
  geometry.vertices[2].position.y = 0.25f;
  geometry.vertices[3].position.y = 10.0f;
  auto &document = mesh.edit_authored_polygon_document();
  for (std::size_t index = 0u; index < geometry.vertices.size(); ++index) {
    geometry.vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1u);
    document.vertices[index].position = geometry.vertices[index].position;
  }

  const auto payload = mesh.wire_geometry_payload();
  ASSERT_TRUE(payload->authored);
  ASSERT_EQ(payload->segments.size(), 4u);
  const auto &edge = payload->segments.front();
  EXPECT_FLOAT_EQ(edge.start.x, -1.0f);
  EXPECT_FLOAT_EQ(edge.end.x, 1.0f);
  ASSERT_EQ(edge.surface_support_count, 1u);
  EXPECT_FLOAT_EQ(edge.surface_support[0].x, 1.0f);
  EXPECT_FLOAT_EQ(edge.surface_support[0].y, 0.25f);
  EXPECT_FLOAT_EQ(edge.surface_support[0].z, 1.0f);
  EXPECT_EQ(mesh.wire_geometry_payload(), payload);
}

TEST(MeshWireGeometrySupportTests, SharedEdgeRetainsBothIncidentSurfaces) {
  MeshAsset mesh;
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}, .source_vertex_id = 1u},
      {.position = {1.0f, 0.0f, 0.0f}, .source_vertex_id = 2u},
      {.position = {0.0f, 1.0f, 0.0f}, .source_vertex_id = 3u},
      {.position = {0.0f, 0.0f, 1.0f}, .source_vertex_id = 4u},
  };
  geometry.indices = {0u, 1u, 2u, 1u, 0u, 3u};
  quader_poly::Document document;
  document.vertices = {
      {.id = 1u, .position = geometry.vertices[0].position},
      {.id = 2u, .position = geometry.vertices[1].position},
      {.id = 3u, .position = geometry.vertices[2].position},
      {.id = 4u, .position = geometry.vertices[3].position},
  };
  document.faces = {
      {.id = 1u, .vertices = {1u, 2u, 3u}},
      {.id = 2u, .vertices = {2u, 1u, 4u}},
  };
  mesh.replace_authored_polygon_document(std::move(document));

  for (const bool authored : {true, false}) {
    mesh.has_authored_polygon_document = authored;
    const auto payload = mesh.wire_geometry_payload();
    EXPECT_EQ(payload->authored, authored);
    ASSERT_EQ(payload->segments.size(), 5u);
    const auto &edge = payload->segments.front();
    EXPECT_FLOAT_EQ(edge.start.x, 0.0f);
    EXPECT_FLOAT_EQ(edge.end.x, 1.0f);
    ASSERT_EQ(edge.surface_support_count, 2u);
    EXPECT_FLOAT_EQ(edge.surface_support[0].x, 0.0f);
    EXPECT_FLOAT_EQ(edge.surface_support[0].y, 1.0f);
    EXPECT_FLOAT_EQ(edge.surface_support[0].z, 0.0f);
    EXPECT_FLOAT_EQ(edge.surface_support[1].x, 0.0f);
    EXPECT_FLOAT_EQ(edge.surface_support[1].y, 0.0f);
    EXPECT_FLOAT_EQ(edge.surface_support[1].z, 1.0f);
  }
}

TEST(MeshWireGeometrySupportTests, TriangulationSupportChangesInvalidateContentIdentity) {
  MeshAsset mesh = make_authored_quad_mesh();
  auto &geometry = mesh.edit_geometry();
  for (std::size_t index = 0u; index < geometry.vertices.size(); ++index) {
    geometry.vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1u);
  }
  const auto original = mesh.wire_geometry_payload();
  mesh.edit_geometry().indices = {0u, 1u, 3u, 1u, 2u, 3u};
  const auto changed = mesh.wire_geometry_payload();

  ASSERT_EQ(original->segments.size(), 4u);
  ASSERT_EQ(changed->segments.size(), 4u);
  for (std::size_t index = 0u; index < original->segments.size(); ++index) {
    const auto &before = original->segments[index];
    const auto &after = changed->segments[index];
    EXPECT_FLOAT_EQ(after.start.x, before.start.x);
    EXPECT_FLOAT_EQ(after.start.y, before.start.y);
    EXPECT_FLOAT_EQ(after.start.z, before.start.z);
    EXPECT_FLOAT_EQ(after.end.x, before.end.x);
    EXPECT_FLOAT_EQ(after.end.y, before.end.y);
    EXPECT_FLOAT_EQ(after.end.z, before.end.z);
    ASSERT_EQ(before.surface_support_count, 1u);
    ASSERT_EQ(after.surface_support_count, 1u);
  }
  EXPECT_FLOAT_EQ(original->segments.front().surface_support[0].x, 1.0f);
  EXPECT_FLOAT_EQ(changed->segments.front().surface_support[0].x, -1.0f);
  EXPECT_NE(original->content_id, changed->content_id);
  EXPECT_EQ(mesh.wire_geometry_payload(), changed);
}

TEST(MeshWireGeometrySupportTests, LiveProjectionMovesEndpointsAndIncidentSurfaceTogether) {
  MeshAsset mesh = make_authored_quad_mesh();
  auto &geometry = mesh.edit_geometry();
  for (std::size_t index = 0u; index < geometry.vertices.size(); ++index) {
    geometry.vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1u);
  }
  const auto original = mesh.wire_geometry_payload();
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : mesh.geometry_vertices()) {
    positions->push_back(vertex.position + QVec3{0.0f, 3.0f, 0.0f});
  }
  auto projection = std::make_shared<MeshGeometryProjection>();
  projection->source_geometry = mesh.geometry_payload();
  projection->positions = std::move(positions);
  mesh.share_geometry_projection(std::move(projection));
  const auto preview = mesh.wire_geometry_payload();

  ASSERT_EQ(preview->segments.size(), 4u);
  for (const auto &segment : preview->segments) {
    EXPECT_FLOAT_EQ(segment.start.y, 3.0f);
    EXPECT_FLOAT_EQ(segment.end.y, 3.0f);
    ASSERT_EQ(segment.surface_support_count, 1u);
    EXPECT_FLOAT_EQ(segment.surface_support[0].y, 3.0f);
  }
  EXPECT_FLOAT_EQ(original->segments.front().surface_support[0].y, 0.0f);
  EXPECT_NE(original->content_id, preview->content_id);
  EXPECT_EQ(mesh.wire_geometry_payload(), preview);
}

TEST(MeshWireGeometrySupportTests, MissingProvenanceUsesStableFaceSupportAndRejectsDegeneratePlanes) {
  MeshAsset mesh = make_authored_quad_mesh();
  auto &document = mesh.edit_authored_polygon_document();
  document.vertices[2].position.z = -0.9999999f;
  document.vertices[3].position.z = 2.0f;
  const auto fallback = mesh.wire_geometry_payload();
  ASSERT_EQ(fallback->segments.size(), 4u);
  ASSERT_EQ(fallback->segments.front().surface_support_count, 1u);
  EXPECT_FLOAT_EQ(fallback->segments.front().surface_support[0].x, -1.0f);
  EXPECT_FLOAT_EQ(fallback->segments.front().surface_support[0].z, 2.0f);

  auto &collinear = mesh.edit_authored_polygon_document();
  for (std::size_t index = 0u; index < collinear.vertices.size(); ++index) {
    collinear.vertices[index].position = {static_cast<float>(index), 0.0f, 0.0f};
  }
  const auto degenerate = mesh.wire_geometry_payload();
  ASSERT_EQ(degenerate->segments.size(), 4u);
  for (const auto &segment : degenerate->segments) {
    EXPECT_EQ(segment.surface_support_count, 0u);
  }
}

TEST(RenderSceneSnapshotTests, GlobalCullingOverridesLegacyMeshFlagsAndRetainsUnchangedExtraction) {
  SceneDocument document;
  AssetRegistry assets;
  const auto mesh = assets.add_mesh(make_overlay_triangle_mesh());
  const auto ordinary = document.create_entity("Ordinary culling", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(ordinary,
      {.mesh = mesh, .cast_shadows = false, .double_sided = true}));
  const auto definition = document.create_entity("Culling definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  ASSERT_TRUE(document.set_visible(definition, false));
  const auto member = document.create_entity("Culling member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, {.mesh = mesh, .double_sided = false}));
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const auto occurrence = document.create_entity("Culling occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(occurrence, {.definition = definition}));
  const auto scene_revision = document.revision();
  const auto asset_revision = assets.revision();
  const auto initial = build_render_scene_snapshot(document, assets);
  ASSERT_EQ(initial.meshes.size(), 1u);
  ASSERT_FALSE(initial.instance_primitives.empty());
  EXPECT_FALSE(initial.meshes.front().double_sided());
  EXPECT_TRUE(initial.meshes.front().mesh_component.double_sided);
  EXPECT_FALSE(initial.instance_primitives.front().key.double_sided);

  const auto two_sided = build_render_scene_snapshot(document, assets,
      {.previous = &initial, .backface_culling_enabled = false});
  EXPECT_FALSE(two_sided.reused_previous_records);
  EXPECT_TRUE(two_sided.dirty.render_components_changed);
  ASSERT_EQ(two_sided.meshes.size(), 1u);
  EXPECT_TRUE(two_sided.meshes.front().double_sided());
  EXPECT_TRUE(two_sided.meshes.front().binding_dirty);
  EXPECT_EQ(two_sided.meshes.front().mesh.geometry_payload,
            initial.meshes.front().mesh.geometry_payload);
  for (const auto &primitive : two_sided.instance_primitives) {
    EXPECT_TRUE(primitive.key.double_sided);
  }
  const auto unchanged = build_render_scene_snapshot(document, assets,
      {.previous = &two_sided, .backface_culling_enabled = false});
  EXPECT_TRUE(unchanged.reused_previous_records);
  EXPECT_FALSE(unchanged.dirty.any_dirty());
  EXPECT_FALSE(unchanged.meshes.front().binding_dirty);
  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      unchanged, document, assets, {}, 0u, {}, nullptr, false));
  EXPECT_FALSE(render_scene_snapshot_matches_sources(
      unchanged, document, assets, {}, 0u, {}, nullptr, true));
  auto refresh_candidate = unchanged;
  EXPECT_FALSE(try_refresh_render_scene_snapshot_selection(
      refresh_candidate, document, assets, 0u, {}, 0u, {}, nullptr, true));
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      refresh_candidate, document, assets, 0u, {}, 0u, {}, nullptr, true));
  const auto restored = build_render_scene_snapshot(document, assets,
      {.previous = &unchanged, .backface_culling_enabled = true});
  EXPECT_FALSE(restored.meshes.front().double_sided());
  EXPECT_FALSE(restored.instance_primitives.front().key.double_sided);
  EXPECT_EQ(document.revision(), scene_revision);
  EXPECT_EQ(assets.revision(), asset_revision);
  EXPECT_TRUE(document.mesh(ordinary)->double_sided);
  EXPECT_FALSE(document.mesh(member)->double_sided);
}

TEST(RenderInstanceBatchPlannerTests, PerObjectTransformsRetainSingletonSlotsBesideBatchedPeers) {
  for (const bool double_sided : {false, true}) {
    RenderInstancePrimitiveKey key{
        .geometry_payload = 101u, .material_payload = 103u,
        .index_count = 3u, .double_sided = double_sided};
    const RenderInstanceBounds bounds{
        .min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}, .present = true};
    std::array<RenderInstancePrimitiveOccurrence, 6u> occurrences;
    for (std::size_t index = 0u; index < occurrences.size(); ++index) {
      occurrences[index] = {.key = key, .occurrence = index + 1u, .local_bounds = bounds};
      occurrences[index].world_transform.values[12] = static_cast<float>(index) * 4.0f;
      if (index >= 3u) {
        occurrences[index].key.presentation_flags = static_cast<std::uint8_t>(
            RenderInstancePresentationFlag::PerObjectTransform);
        occurrences[index].world_transform.values[0] = -1.0f;
      }
    }
    const auto initial = plan_render_instance_batches(occurrences, 64u);
    ASSERT_EQ(initial.chunks.size(), 4u);
    EXPECT_EQ(initial.chunks.front().occurrences.size(), 3u);
    for (std::size_t index = 1u; index < initial.chunks.size(); ++index) {
      EXPECT_EQ(initial.chunks[index].occurrences.size(), 1u);
    }
    occurrences.back().world_transform.values[13] = 2.0f;
    const auto moved = plan_render_instance_batches(occurrences, 64u, &initial);
    EXPECT_TRUE(moved.work.retained_layout);
    EXPECT_EQ(classify_instance_batch_update(initial, moved),
              RenderInstanceBatchUpdateKind::TransformsOnly);
    occurrences.back().visible = false;
    const auto suppressed = plan_render_instance_batches(occurrences, 64u, &moved);
    EXPECT_TRUE(suppressed.work.retained_layout);
    EXPECT_EQ(suppressed.chunks.size(), 4u);
    occurrences.back().visible = true;
    const auto restored = plan_render_instance_batches(occurrences, 64u, &suppressed);
    EXPECT_TRUE(restored.work.retained_layout);
    const auto unchanged = plan_render_instance_batches(occurrences, 64u, &restored);
    EXPECT_EQ(classify_instance_batch_update(restored, unchanged),
              RenderInstanceBatchUpdateKind::Unchanged);
    for (auto &occurrence : occurrences) {
      occurrence.key.presentation_flags = 0u;
      occurrence.world_transform.values[0] = 1.0f;
    }
    const auto merged = plan_render_instance_batches(occurrences, 64u, &unchanged);
    ASSERT_EQ(merged.chunks.size(), 1u);
    EXPECT_EQ(merged.chunks.front().occurrences.size(), occurrences.size());
    EXPECT_EQ(classify_instance_batch_update(unchanged, merged),
              RenderInstanceBatchUpdateKind::Rebuild);
  }
}
