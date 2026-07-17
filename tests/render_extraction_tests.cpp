////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/instance_batch_planner.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_info_stats.hpp"
#include "render/viewport_render_pass_coordinator.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_bounds.hpp"
#include "support/guardrail_file_scan.hpp"
#include "viewport/viewport_settings.hpp"

#include <mesh/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace quader;

namespace {

using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_slice;

/// Supplies deterministic project-material snapshots to render tests.
class TestProjectMaterialResolver final
    : public RenderProjectMaterialResolver {
public:
  [[nodiscard]] std::uint64_t revision() const noexcept override {
    return revision_value;
  }

  [[nodiscard]] std::optional<RenderProjectMaterialSnapshot>
  resolve(ProjectAssetId id) const override {
    return material.has_value() &&
                   material->evaluation.project_material_id == id
               ? material
               : std::nullopt;
  }

  std::uint64_t revision_value = 1u;
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

TEST(RenderSignatureBuilderTests, KeepsRenderHashPolicyInSingleOwner) {
  const std::array<std::string_view, 4u> cache_sources = {
      "src/render/material_binding_service.cpp",
      "src/render/mesh_renderable_factory.cpp",
      "src/render/tool_preview_renderable_cache.cpp",
      "src/render/overlay/overlay_renderable_builder.cpp",
  };
  for (const std::string_view source : cache_sources) {
    const std::string text = read_source_file({source});
    EXPECT_EQ(text.find("kFnv"), std::string::npos);
    EXPECT_EQ(text.find("hash_combine"), std::string::npos);
    EXPECT_EQ(text.find("hash_float"), std::string::npos);
    EXPECT_EQ(text.find("hashVec3"), std::string::npos);
    EXPECT_NE(text.find("RenderSignatureBuilder"), std::string::npos);
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
  const RenderSceneSnapshot changed = build_render_scene_snapshot(
      document, assets,
      {.previous = &unchanged, .project_material_resolver = &resolver});
  EXPECT_FALSE(changed.reused_previous_records);
  EXPECT_TRUE(changed.dirty.project_materials_changed);
  EXPECT_TRUE(changed.meshes.front().material_dirty);

  document.set_transform(
      entity, TransformComponent{.position = {1.0f, 2.0f, 3.0f}});
  EXPECT_FALSE(try_refresh_render_scene_snapshot_transforms(
      initial, document, assets, 2u, {}, resolver.revision_value));
}

TEST(RenderSceneSnapshotTests,
     ToolPreviewSnapshotsKeepEmbeddedMaterialBindings) {
  AssetRegistry assets;
  const AssetId material =
      assets.add_material(make_default_quader_material("Preview Material"));
  const std::vector<RenderToolPreviewMeshRecord> records =
      build_render_tool_preview_snapshot(
          {ToolPreviewMesh{
              .source_entity = 7u,
              .material = material,
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
  EXPECT_EQ(records.front().material_revision,
            records.front().material.revision);
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
  const ViewportInfoSceneStats visible_stats =
      collect_viewport_info_scene_stats(document, assets, camera);

  EXPECT_EQ(visible_stats.objects, 1u);
  EXPECT_EQ(visible_stats.primitives, 1u);
  EXPECT_EQ(visible_stats.draw_calls, 1u);

  ViewportOverlayCamera looked_away = camera;
  looked_away.forward = {0.0f, 1.0f, 0.0f};
  looked_away.right = {1.0f, 0.0f, 0.0f};
  looked_away.up = {0.0f, 0.0f, -1.0f};
  const ViewportInfoSceneStats empty_view_stats =
      collect_viewport_info_scene_stats(document, assets, looked_away);

  EXPECT_EQ(empty_view_stats.objects, 0u);
  EXPECT_EQ(empty_view_stats.primitives, 0u);
  EXPECT_EQ(empty_view_stats.draw_calls, 0u);
}

TEST(RenderSceneSnapshotTests, MeshAndLightRecordsAreValueSnapshotInputs) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string extraction_header =
      read_source_file("src/render/scene_render_extraction.hpp");
  const std::string extraction_source =
      read_source_file("src/render/scene_render_extraction.cpp");
  const std::string mesh_record =
      source_slice(snapshot_header, "struct RenderSceneMeshRecord",
                   "struct RenderSceneLightRecord");
  const std::string light_record =
      source_slice(snapshot_header, "struct RenderSceneLightRecord",
                   "struct RenderSceneSnapshotDirtyState");

  EXPECT_NE(snapshot_header.find("struct RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneMeshRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneLightRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find("std::vector<RenderSceneMeshRecord>"),
            std::string::npos);
  EXPECT_NE(snapshot_header.find("std::vector<RenderSceneLightRecord>"),
            std::string::npos);
  EXPECT_NE(snapshot_header.find("scene_revision"), std::string::npos);
  EXPECT_NE(snapshot_header.find("asset_revision"), std::string::npos);
  EXPECT_NE(snapshot_header.find("mesh_revision"), std::string::npos);
  EXPECT_NE(snapshot_header.find("material_revision"), std::string::npos);

  EXPECT_EQ(mesh_record.find("const MeshAsset *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshComponent *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MaterialComponent *"), std::string::npos);
  EXPECT_EQ(light_record.find("const LightComponent *"), std::string::npos);
  EXPECT_NE(extraction_header.find("build_render_scene_snapshot"),
            std::string::npos);
  EXPECT_EQ(extraction_source.find(".mesh = assets.mesh("),
            std::string::npos);
  EXPECT_EQ(extraction_source.find(".mesh_component = mesh_component"),
            std::string::npos);
  EXPECT_EQ(extraction_source.find(".material_component = material_component"),
            std::string::npos);
  EXPECT_EQ(extraction_source.find(".light = light"), std::string::npos);
}

TEST(RenderSceneSnapshotTests, OverlayRecordsStayBehindRenderBoundary) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string render_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");

  for (std::string_view token : {
           std::string_view{"SelectedObject"},
           std::string_view{"SceneWireframe"},
           std::string_view{"HierarchySelection"},
           std::string_view{"GroupBounds"},
           std::string_view{"SelectedLight"},
           std::string_view{"ToolOverlay"},
       }) {
    EXPECT_NE(snapshot_header.find(token), std::string::npos) << token;
  }

  EXPECT_NE(render_bridge_source.find("RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("ViewportSceneOverlayBuilder::"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("build_render_overlay_"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("sync_selected_object_overlay("),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("sync_scene_wireframe_overlay("),
            std::string::npos);
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

  const ViewportOverlayFrame frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot);

  EXPECT_EQ(
      line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
      3u);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::GroupBounds), 12u);
  EXPECT_EQ(point_count_for(frame, ViewportOverlayLayer::SelectedVertex), 1u);
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 2u);
  EXPECT_EQ(icon_count_for(frame, ViewportOverlayLayer::PointLightGizmo), 0u);
  EXPECT_EQ(line_source_count_for(frame, ViewportOverlaySource::Tool), 1u);

  const ViewportOverlayFrame gizmo_frame =
      ViewportSceneOverlayBuilder::build_selection_frame({}, snapshot, true);
  EXPECT_EQ(icon_count_for(gizmo_frame, ViewportOverlayLayer::PointLightGizmo),
            1u);
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
          {}, ModelingSelectionMode::Object, wireframe_snapshot);
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
      .selection_edge_line_width_pixels = 3.25f,
      .wire_signature = 91u,
  };
  const RenderOverlaySceneWireframeSnapshot unselected_snapshot =
      build_render_overlay_scene_wireframe_snapshot(document, assets, 1u,
                                                    style);
  ASSERT_EQ(unselected_snapshot.meshes.size(), 1u);
  EXPECT_EQ(unselected_snapshot.meshes.front().wireframe_color,
            kLayerWireColor);
  const ViewportOverlayFrame unselected_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, ModelingSelectionMode::Object, unselected_snapshot);
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
  EXPECT_EQ(selected_wire_snapshot.meshes.front().wireframe_color,
            kLayerWireColor);
  const ViewportOverlayFrame selected_wire_frame =
      ViewportSceneOverlayBuilder::build_wireframe_frame(
          {}, ModelingSelectionMode::Object, selected_wire_snapshot);
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
                    style.selection_edge_line_width_pixels);
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
  EXPECT_EQ(restored_wire_snapshot.meshes.front().wireframe_color,
            kLayerWireColor);

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

TEST(RenderOverlaySnapshotTests,
     PersistentSceneWireframeLineCacheUsesSnapshotClipPlanes) {
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string snapshot_header =
      read_source_file("src/render/overlay/render_overlay_snapshot.hpp");

  EXPECT_NE(snapshot_header.find("ViewportCameraClipPlanes clip_planes"),
            std::string::npos);
  EXPECT_EQ(selected_cache_source.find("kSelectedObjectLineNearPlane"),
            std::string::npos);
  EXPECT_EQ(wireframe_cache_source.find("kSceneWireframeLineNearPlane"),
            std::string::npos);
  EXPECT_EQ(selected_cache_source.find("setParameter(\"nearPlane\", 0.05"),
            std::string::npos);
  EXPECT_EQ(wireframe_cache_source.find("setParameter(\"nearPlane\", 0.05"),
            std::string::npos);
  EXPECT_EQ(selected_cache_source.find("SelectedObjectLineVertex"),
            std::string::npos);
  EXPECT_EQ(selected_cache_source.find("update_line_proxy"),
            std::string::npos);
  EXPECT_EQ(selected_cache_source.find("setParameter(\"nearPlane\""),
            std::string::npos);
  EXPECT_NE(wireframe_cache_source.find("clip_planes.near_plane"),
            std::string::npos);
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
  EXPECT_EQ(changed.scene_wireframe->meshes.front().wireframe_color,
            rgba8(32, 128, 255, 230));
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
  ASSERT_EQ(initial.hierarchy_selection_wires.size(), 1u);
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
          {}, transformed, false, ModelingSelectionMode::Object);
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

TEST(RenderOverlaySnapshotTests, SceneOverlayBuildersConsumeSnapshotsOnly) {
  const std::string builder_source = read_source_file(
      "src/render/overlay/viewport_scene_overlay_builder.cpp");
  const std::string builder_header = read_source_file(
      "src/render/overlay/viewport_scene_overlay_builder.hpp");
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string bridge_header =
      read_source_file("src/render/scene_render_bridge.hpp");
  const std::string overlay_snapshot_header =
      read_source_file("src/render/overlay/render_overlay_snapshot.hpp");
  const std::string render_snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");

  EXPECT_EQ(builder_header.find("const SceneDocument &"), std::string::npos);
  EXPECT_EQ(builder_header.find("const AssetRegistry &"), std::string::npos);
  EXPECT_EQ(builder_source.find("build_render_overlay_selection_snapshot"),
            std::string::npos);
  EXPECT_EQ(builder_source.find("build_render_overlay_scene_wireframe_snapshot"),
            std::string::npos);
  EXPECT_NE(
      builder_source.find("const RenderOverlaySelectionSnapshot &snapshot"),
      std::string::npos);
  EXPECT_NE(builder_source.find(
                "const RenderOverlaySceneWireframeSnapshot &snapshot"),
            std::string::npos);
  EXPECT_NE(selected_cache_source.find(
                "const RenderOverlaySelectedObjectSnapshot &snapshot"),
            std::string::npos);
  EXPECT_NE(wireframe_cache_source.find(
                "const RenderOverlaySceneWireframeSnapshot &snapshot"),
            std::string::npos);
  EXPECT_NE(bridge_header.find("sync(const SceneDocument &document"),
            std::string::npos);
  EXPECT_NE(bridge_header.find("const AssetRegistry &assets"),
            std::string::npos);
  EXPECT_EQ(bridge_source.find("build_render_overlay_selected_object_snapshot"),
            std::string::npos);
  EXPECT_EQ(bridge_source.find("build_render_overlay_scene_wireframe_snapshot"),
            std::string::npos);
  EXPECT_EQ(bridge_header.find("RenderOverlaySnapshotCache"),
            std::string::npos);
  EXPECT_NE(overlay_snapshot_header.find(
                "class RenderOverlaySnapshotCache final"),
            std::string::npos);
  EXPECT_NE(bridge_header.find("const RenderOverlaySelectedObjectSnapshot"),
            std::string::npos);
  EXPECT_NE(bridge_header.find("const RenderOverlaySceneWireframeSnapshot"),
            std::string::npos);
  EXPECT_NE(render_snapshot_header.find(
                "using RenderSelectedObjectOverlaySnapshotRecord"),
            std::string::npos);
  EXPECT_NE(render_snapshot_header.find("using OverlaySnapshot"),
            std::string::npos);
}

TEST(RenderSceneBindingGuardrailTests,
     SceneAndOverlayEarlyOutsQualifyReuseBySceneActivation) {
  const std::string binding_header =
      read_source_file("src/render/render_scene_binding.hpp");
  const std::string extraction_source =
      read_source_file("src/render/scene_render_extraction.cpp");
  const std::string sync_source =
      read_source_file("src/render/scene_render_sync_service.cpp");
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string texture_cache_source =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string render_api_source =
      read_source_file("src/api/quader_render_api.cpp");

  EXPECT_NE(binding_header.find("scene_session_id"), std::string::npos);
  EXPECT_NE(binding_header.find("activation_generation"), std::string::npos);
  EXPECT_NE(binding_header.find("binding_revision"), std::string::npos);
  EXPECT_NE(extraction_source.find(
                "previous->scene_binding == snapshot.scene_binding"),
            std::string::npos);
  EXPECT_NE(sync_source.find(
                "synced_scene_binding_ != snapshot.scene_binding"),
            std::string::npos);
  const std::size_t retire_scene_meshes =
      sync_source.find("mesh_cache_->remove_except(no_scene_entities)");
  const std::size_t destroy_retired_materials = sync_source.find(
      "mesh_cache_->drain_retired_destroy_queue(\n"
      "      mesh_cache_->retired_proxy_count())");
  const std::size_t invalidate_scene_textures =
      sync_source.find("material_binding_->bind_scene(scene_binding)");
  ASSERT_NE(retire_scene_meshes, std::string::npos);
  ASSERT_NE(destroy_retired_materials, std::string::npos);
  ASSERT_NE(invalidate_scene_textures, std::string::npos);
  EXPECT_LT(retire_scene_meshes, destroy_retired_materials);
  EXPECT_LT(destroy_retired_materials, invalidate_scene_textures)
      << "A scene switch must destroy retired material instances before "
         "invalidating the textures they still reference.";
  const std::size_t clear_tool_previews =
      bridge_source.find("tool_preview_cache_.clear()");
  const std::size_t clear_viewport_overlays =
      bridge_source.find("overlay_cache_.clear_slot(index)");
  const std::size_t bind_scene_resources =
      bridge_source.find("sync_service_.bind_scene(scene_binding)");
  ASSERT_NE(clear_tool_previews, std::string::npos);
  ASSERT_NE(clear_viewport_overlays, std::string::npos);
  ASSERT_NE(bind_scene_resources, std::string::npos);
  EXPECT_LT(clear_tool_previews, bind_scene_resources);
  EXPECT_LT(clear_viewport_overlays, bind_scene_resources)
      << "Preview and overlay material instances must be destroyed before "
         "the outgoing scene texture cache is invalidated.";
  EXPECT_NE(bridge_source.find("bind_scene(snapshot.scene_binding)"),
            std::string::npos);
  EXPECT_NE(selected_cache_source.find(
                "synced_scene_binding_ != snapshot.scene_binding"),
            std::string::npos);
  EXPECT_NE(wireframe_cache_source.find(
                "synced_scene_binding_ != snapshot.scene_binding"),
            std::string::npos);
  EXPECT_NE(texture_cache_source.find("texture_cache_.clear()"),
            std::string::npos);
  EXPECT_EQ(texture_cache_source.find("evict_obsolete_texture_revisions"),
            std::string::npos)
      << "Without texture reference tracking, old revisions must remain "
         "alive until the scene cache is safely released.";
  EXPECT_NE(render_api_source.find(".scene_session_id ="),
            std::string::npos);
  EXPECT_NE(render_api_source.find(".activation_generation ="),
            std::string::npos);
  EXPECT_NE(render_api_source.find(".binding_revision ="),
            std::string::npos);
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
    LightComponent point;
    point.type = LightType::Point;
    point.intensity = 1.0f;
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(point),
                    10000000.0f);

    LightComponent brighter_point = point;
    brighter_point.intensity = 2.5f;
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(brighter_point),
                    25000000.0f);

    LightComponent spot = point;
    spot.type = LightType::Spot;
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(spot),
                    10000000.0f);

    LightComponent directional = point;
    directional.type = LightType::Directional;
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(directional),
                    100000.0f);
}

TEST(LightRenderableCacheTests, OwnsIncrementalLightSyncPolicy) {
  const std::string header =
      read_source_file({"src", "render", "light_renderable_cache.hpp"});
  const std::string source =
      read_source_file({"src", "render", "light_renderable_cache.cpp"});
  const std::string sync_source =
      read_source_file({"src", "render", "scene_render_sync_service.cpp"});

  EXPECT_NE(header.find("rebuild_signature"), std::string::npos);
  EXPECT_NE(header.find("transform_signature"), std::string::npos);
  EXPECT_NE(header.find("filament_intensity"), std::string::npos);
  EXPECT_NE(source.find("kGodotPunctualLightLumensPerIntensity = 1000.0f"),
            std::string::npos);
  EXPECT_NE(
      source.find("kViewportPunctualLightExposureCompensation = 10000.0f"),
      std::string::npos);
  EXPECT_NE(source.find("needs_rebuild(light)"), std::string::npos);
  EXPECT_NE(source.find("update_light_transform(existing->second, "
                        "light.world_transform)"),
            std::string::npos);
  EXPECT_NE(source.find(".intensity(filament_intensity(light))"),
            std::string::npos);
  EXPECT_NE(source.find(".position(position_from_matrix(world_transform))"),
            std::string::npos);
  EXPECT_NE(source.find(".falloff(light_falloff_radius(light))"),
            std::string::npos);
  EXPECT_NE(source.find("light_manager.setPosition(instance, "
                        "position_from_matrix(world_transform))"),
            std::string::npos);
  EXPECT_NE(sync_source.find("light_cache_.sync_light"), std::string::npos);
  EXPECT_EQ(sync_source.find("light_signature"), std::string::npos);
  EXPECT_EQ(sync_source.find("update_light_transform"), std::string::npos);
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
    const ViewportOverlayFrame frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            std::move(base_frame), snapshot);

    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 1u);
    EXPECT_EQ(
        line_source_count_for(frame, ViewportOverlaySource::HierarchySelection),
        3u);
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
            ModelingSelectionMode::Object);
    EXPECT_EQ(line_count_for(object_frame, ViewportOverlayLayer::GroupBounds),
              12u);
    const std::optional<SceneBounds> cage_bounds =
        line_bounds_for(object_frame, ViewportOverlayLayer::GroupBounds);
    ASSERT_TRUE(cage_bounds.has_value());
    expect_scene_bounds_near(*cage_bounds, *expected_bounds);

    const ViewportOverlayFrame face_frame =
        ViewportSceneOverlayBuilder::build_selection_frame(
            ViewportOverlayFrame{}, snapshot, false,
            ModelingSelectionMode::Face);
    EXPECT_EQ(line_count_for(face_frame, ViewportOverlayLayer::GroupBounds),
              0u);
}

TEST(ViewportSceneOverlayBuilderTests,
     MultipleSelectedMeshesUseOneAggregateBoundsCageWithoutObjectDetail) {
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

    ViewportOverlayFrame base_frame;
    base_frame.lines.push_back({
        .layer = ViewportOverlayLayer::SelectedEdge,
        .start = {-3.0f, 1.0f, 2.0f},
        .end = {-2.0f, 1.0f, 2.0f},
        .source = ViewportOverlaySource::Selection,
        .element = {.object_id = 1},
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
            ModelingSelectionMode::Object);

    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 0u);
    EXPECT_EQ(
        triangle_count_for(frame, ViewportOverlayLayer::SelectedVisibleFace),
        0u);
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::HoverEdge), 1u)
        << "multi-selection detail suppression must preserve hover feedback";
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
    EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::SelectedEdge), 2u);
    EXPECT_TRUE(std::all_of(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::Selection &&
                 line.style_role == ViewportOverlayStyleRole::SelectionWire;
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
                 icon.size_pixels == kOverlayPointLightGizmoSizePixels;
        }));
    const auto selected_icon =
        std::find_if(frame.icons.begin(), frame.icons.end(),
                     [](const ViewportOverlayIcon &icon) {
                       return icon.position.x == 2.0f &&
                              icon.position.y == 3.0f &&
                              icon.position.z == 4.0f;
                     });
    EXPECT_NE(selected_icon, frame.icons.end());
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
            base_frame, ModelingSelectionMode::Object, snapshot);

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
            base_frame, ModelingSelectionMode::Face, snapshot);

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
            {}, ModelingSelectionMode::Object, snapshot);

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

TEST(OverlayLineMeshBuilderTests, ClipsAndExpandsLinesToDeviceSpaceQuads) {
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
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_FALSE(batch.state.depth_write);
    for (const OverlayLineVertex& vertex : batch.mesh.vertices) {
        EXPECT_FLOAT_EQ(vertex.position.w, 1.0f);
        EXPECT_EQ(vertex.color, rgba8_srgb_to_linear(rgba8(255, 128, 64, 255)));
    }
}

TEST(OverlayLineMeshBuilderTests, ExpandsDiagonalLinesPerpendicularToScreenDirection) {
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
    EXPECT_GT(std::fabs(mesh.vertices[0].position.x - mesh.vertices[3].position.x), 0.0001f);
    EXPECT_GT(std::fabs(mesh.vertices[0].position.y - mesh.vertices[3].position.y), 0.0001f);
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
  EXPECT_EQ(plan.edit_wire_batches.front().state.depth_func,
            OverlayDepthFunc::GreaterOrEqual);
  EXPECT_TRUE(plan.edit_wire_batches.front().state.depth_culling);

  ASSERT_EQ(plan.line_batches.size(), 1u);
  const OverlayLineBatch &draw_on_top = plan.line_batches.front();
  EXPECT_EQ(draw_on_top.state.depth_func, OverlayDepthFunc::Always);
  EXPECT_FLOAT_EQ(draw_on_top.depth_bias, 0.0f);
  EXPECT_FALSE(draw_on_top.state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, EditWireBatchesDoNotOwnPrivateClipPlane) {
  const std::string builder_header =
      read_source_file("src/render/overlay/overlay_renderable_builder.hpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string proxy_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string edit_wire_material =
      read_source_file("resources/materials/quader_edit_wire_line.mat");

  const std::string edit_wire_batch =
      source_slice(builder_header, "struct OverlayEditWireBatch",
                   "struct OverlayPointBatch");
  EXPECT_EQ(edit_wire_batch.find("near_plane"), std::string::npos);
  EXPECT_EQ(builder_source.find("kOverlayEditWireNearPlane"),
            std::string::npos);
  EXPECT_EQ(proxy_source.find("batch.near_plane"), std::string::npos);
  EXPECT_EQ(edit_wire_material.find("nearPlane"), std::string::npos);
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

TEST(OverlayLineMeshBuilderTests, ExtendsLineCapsInScreenSpace) {
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

  const OverlayCameraProjector projector(camera);
  const OverlayProjectedPoint projected_start =
      projector.project(projector.view_point_for(line_start));
  const OverlayProjectedPoint projected_end =
      projector.project(projector.view_point_for(line_end));
  ASSERT_TRUE(projected_start.valid);
  ASSERT_TRUE(projected_end.valid);

  const float cap_pixels = (2.0f + kOverlayLineFeatherPixels) * 0.5f;
  const float cap_device_x = cap_pixels * 2.0f / camera.viewport_size.x;
  const float start_cap_center_x =
      (mesh.vertices[0].position.x + mesh.vertices[3].position.x) * 0.5f;
  const float end_cap_center_x =
      (mesh.vertices[1].position.x + mesh.vertices[2].position.x) * 0.5f;
  EXPECT_NEAR(start_cap_center_x, projected_start.device.x - cap_device_x,
              0.00001f);
  EXPECT_NEAR(end_cap_center_x, projected_end.device.x + cap_device_x,
              0.00001f);
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

  const OverlayCameraProjector projector(camera);
  const OverlayProjectedPoint projected_line_start =
      projector.project(projector.view_point_for(line_start));
  const OverlayProjectedPoint projected_line_end =
      projector.project(projector.view_point_for(line_end));
  const OverlayProjectedPoint projected_point =
      projector.project(projector.view_point_for(point_position));
  ASSERT_TRUE(projected_line_start.valid);
  ASSERT_TRUE(projected_line_end.valid);
  ASSERT_TRUE(projected_point.valid);

  const float expected_line_depth =
      projected_line_start.device.z - camera.near_plane *
                                          kOverlayDeviceDepthBiasPerNearPlane *
                                          kOverlayLineDepthBias;
  const float line_center_x =
      (line_mesh.vertices[0].position.x + line_mesh.vertices[1].position.x +
       line_mesh.vertices[2].position.x + line_mesh.vertices[3].position.x) *
      0.25f;
  const float line_center_y =
      (line_mesh.vertices[0].position.y + line_mesh.vertices[1].position.y +
       line_mesh.vertices[2].position.y + line_mesh.vertices[3].position.y) *
      0.25f;
  EXPECT_NEAR(line_center_x,
              (projected_line_start.device.x + projected_line_end.device.x) *
                  0.5f,
              0.00001f);
  EXPECT_NEAR(line_center_y,
              (projected_line_start.device.y + projected_line_end.device.y) *
                  0.5f,
              0.00001f);
  EXPECT_NEAR(line_mesh.vertices[0].position.z, expected_line_depth, 0.00001f);
  EXPECT_LT(line_mesh.vertices[0].position.z,
            projected_line_start.device.z);
  EXPECT_GT(line_mesh.vertices[0].position.z * -0.5f + 0.5f,
            projected_line_start.device.z * -0.5f + 0.5f);

  const float expected_point_depth =
      projected_point.device.z - camera.near_plane *
                                     kOverlayDeviceDepthBiasPerNearPlane *
                                     kOverlayPointDepthBias;
  const float point_center_x =
      (point_mesh.vertices[0].position.x + point_mesh.vertices[1].position.x +
       point_mesh.vertices[2].position.x + point_mesh.vertices[3].position.x) *
      0.25f;
  const float point_center_y =
      (point_mesh.vertices[0].position.y + point_mesh.vertices[1].position.y +
       point_mesh.vertices[2].position.y + point_mesh.vertices[3].position.y) *
      0.25f;
  EXPECT_NEAR(point_center_x, projected_point.device.x, 0.00001f);
  EXPECT_NEAR(point_center_y, projected_point.device.y, 0.00001f);
  EXPECT_NEAR(point_mesh.vertices[0].position.z, expected_point_depth,
              0.00001f);
  EXPECT_LT(point_mesh.vertices[0].position.z, projected_point.device.z);
  EXPECT_GT(point_mesh.vertices[0].position.z * -0.5f + 0.5f,
            projected_point.device.z * -0.5f + 0.5f);
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
     InsideOutVertexPointsDrawOnTopWhenCameraIsInsideStampVolume) {
  const std::array<ViewportOverlayLayer, 3u> layers{{
      ViewportOverlayLayer::UnselectedVertex,
      ViewportOverlayLayer::SelectedVertex,
      ViewportOverlayLayer::HoverVertex,
  }};

  ViewportOverlayCamera camera = test_perspective_overlay_camera();
  camera.eye = {0.0f, 0.0f, -2.0f};
  camera.forward = {0.0f, 0.0f, -1.0f};

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
    append_source_wire_depth_stamp_box(frame, 1, true);

    const OverlayLayerBuildPlan plan =
        OverlayRenderableBuilder::build_layer(frame, camera, layer);

    ASSERT_EQ(plan.point_batches.size(), 1u);
    EXPECT_EQ(point_mesh_vertex_count(plan), 4u);
    EXPECT_EQ(plan.point_batches.front().state.depth_func,
              OverlayDepthFunc::Always);
    EXPECT_FLOAT_EQ(plan.point_batches.front().depth_bias, 0.0f);
    EXPECT_FALSE(plan.point_batches.front().state.depth_culling);
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
    EXPECT_EQ(plan.icon_batches[0].state.depth_func, OverlayDepthFunc::Always);
    EXPECT_FALSE(plan.icon_batches[0].state.depth_culling);
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

TEST(OverlayRenderableBuilderTests, LayerBuildSignatureOnlyUsesCameraForScreenSpaceLayers) {
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
                        {{0.0f, 0.0f, 0.0f, 1.0f},
                         {},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 0.0f, 0.0f, 1.0f},
                         {},
                         rgba8(255, 255, 255, 255)},
                        {{1.0f, 1.0f, 0.0f, 1.0f},
                         {},
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

TEST(SceneRenderBridgeExtractionTests, DelegatesOverlayAndPreviewProxyOwnership) {
  const std::string header =
      read_source_file({"src", "render", "scene_render_bridge.hpp"});
  const std::string source =
      read_source_file({"src", "render", "scene_render_bridge.cpp"});

  EXPECT_NE(header.find("OverlayRenderableCache overlay_cache_"),
            std::string::npos);
  EXPECT_NE(header.find("ToolPreviewRenderableCache tool_preview_cache_"),
            std::string::npos);
  EXPECT_NE(source.find("overlay_cache_.set_overlay_frame"), std::string::npos);
  EXPECT_NE(source.find("tool_preview_cache_.set_meshes"), std::string::npos);

  EXPECT_EQ(header.find("struct OverlayProxy"), std::string::npos);
  EXPECT_EQ(header.find("struct OverlaySlot"), std::string::npos);
  EXPECT_EQ(header.find("struct OverlayProxyGroup"), std::string::npos);
  EXPECT_EQ(header.find("overlaySlots_"), std::string::npos);
  EXPECT_EQ(header.find("toolPreviewMeshProxies_"), std::string::npos);
  EXPECT_EQ(source.find("rebuildOverlayProxies"), std::string::npos);
  EXPECT_EQ(source.find("destroyOverlayProxy"), std::string::npos);
  EXPECT_EQ(source.find("destroyToolPreviewMeshes"), std::string::npos);
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
    occurrences.push_back({
        .key = key,
        .occurrence = 1'000u + index,
        .world_transform = transform,
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
  for (const RenderOverlayMeshSnapshot &mesh : wire_snapshot.meshes) {
    ASSERT_NE(mesh.geometry_payload, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(mesh.geometry_payload.get()),
              shared_geometry_token);
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
     CoversDeviceBoundaryStylesAndTransformOnlyUpdates) {
  AssetRegistry assets;
  const AssetId asset = assets.add_mesh(make_overlay_triangle_mesh());
  const MeshAsset *mesh = assets.mesh(asset);
  ASSERT_NE(mesh, nullptr);
  const std::shared_ptr<const MeshGeometryPayload> payload =
      mesh->geometry_payload();

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
          .world_transform = transform,
          .vertices = payload->vertices,
          .indices = payload->indices,
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
        plan_scene_wireframe_overlay_batches(moved, 64u);
    EXPECT_EQ(classify_instance_batch_update(plan.batches,
                                             moved_plan.batches),
              RenderInstanceBatchUpdateKind::TransformsOnly);
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
        .selected = index == 0u,
        .wireframe_color =
            index >= 100u ? rgba8(255, 128, 0, 230)
                          : kOverlayWireframeXrayColor,
        .vertices = payload->vertices,
        .indices = payload->indices,
    });
  }
  const SceneWireframeOverlayBatchPlan split =
      plan_scene_wireframe_overlay_batches(styled, 64u);
  EXPECT_EQ(split.unique_geometry_style_count, 3u);
  EXPECT_EQ(split.batches.chunks.size(), 5u)
      << "one selected chunk plus two chunks for each layer color";
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
  EXPECT_EQ(snapshot.meshes.front().wireframe_color,
            rgba8(64, 128, 191, 230));
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
     UsesInstancedDeviceDomainEndpointExpansionMaterial) {
  const std::string material =
      read_source_file("resources/materials/quader_selected_object_line.mat");
  const std::string cache = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  EXPECT_NE(material.find("instanced : true"), std::string::npos);
  EXPECT_NE(material.find("getWorldFromModelMatrix"), std::string::npos);
  EXPECT_NE(material.find("getResolution"), std::string::npos);
  EXPECT_NE(cache.find("filament::InstanceBuffer::Builder"),
            std::string::npos);
  EXPECT_NE(cache.find(".instances(transforms.size(), proxy.instance_buffer)"),
            std::string::npos);
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
  EXPECT_EQ(fill.color, rgba8(201, 92, 88, 89));
  EXPECT_TRUE(fill.depth_test);
  EXPECT_FALSE(fill.depth_write);
  EXPECT_FLOAT_EQ(fill.polygon_offset_scale, -1.0f);
  EXPECT_FLOAT_EQ(fill.polygon_offset_constant, -1.0f);
  EXPECT_EQ(fill.draws_before, ViewportOverlayLayer::SourceWire);
  EXPECT_LT(fill.blend_order,
            viewport_overlay_draw_order(ViewportOverlayLayer::SourceWire) *
                64);
  EXPECT_FLOAT_EQ(fill.world_transform.values[12], 12.0f);
  EXPECT_EQ(wire.semantic, RenderOverlaySemantic::InstanceSelectionWire);
  EXPECT_EQ(wire.occurrence, occurrence);
  EXPECT_EQ(wire.definition, definition);
  EXPECT_EQ(wire.member, member);
  EXPECT_EQ(wire.color, kOverlaySelectionWireColor);
  EXPECT_FLOAT_EQ(wire.line_width_pixels,
                  kOverlaySelectionWireLineWidthPixels);
  EXPECT_EQ(wire.layer, ViewportOverlayLayer::SourceWire);
  EXPECT_GT(wire.blend_order, fill.blend_order);
  EXPECT_EQ(wire.blend_order,
            viewport_overlay_draw_order(ViewportOverlayLayer::SourceWire) *
                64);
  EXPECT_FLOAT_EQ(wire.world_transform.values[12], 12.0f);
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
  object_map.bind(kObjectId, member);
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
