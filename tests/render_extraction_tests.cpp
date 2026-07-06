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
#include "render/light_renderable_cache.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_render_pass_coordinator.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "scene/scene_document.hpp"
#include "support/guardrail_file_scan.hpp"

#include <mesh/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace quader;

namespace {

using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_slice;

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
  mesh.vertices = {
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
  mesh.indices = {0u, 1u, 2u};
  mesh.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 3u, .material_slot = 0u}};
  return mesh;
}

MeshAsset make_authored_quad_mesh() {
  MeshAsset mesh;
  mesh.name = "Authored Quad";
  mesh.vertices = {
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
  mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  mesh.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 6u, .material_slot = 0u}};
  quader_poly::Document document;
  document.vertices = {
      {.id = 1u, .position = mesh.vertices[0].position},
      {.id = 2u, .position = mesh.vertices[1].position},
      {.id = 3u, .position = mesh.vertices[2].position},
      {.id = 4u, .position = mesh.vertices[3].position},
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
    mesh.indices = {
            0u, 1u, 2u,
            3u, 4u, 5u,
            6u, 7u, 8u,
            9u, 10u, 11u,
            12u, 13u, 14u,
    };
    mesh.primitives = {
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
            assets.mesh(mesh)->vertices.data());
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
      read_source_file("src/ui/viewport_panel.cpp");

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

  const RenderOverlaySelectedObjectSnapshot selected_snapshot =
      build_render_overlay_selected_object_snapshot(document, assets, true);

  EXPECT_TRUE(selected_snapshot.visible);
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
      build_render_overlay_scene_wireframe_snapshot(document, assets, 0x03u);

  EXPECT_EQ(wireframe_snapshot.scene_revision, document.revision());
  EXPECT_EQ(wireframe_snapshot.asset_revision, assets.revision());
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
  EXPECT_EQ(line_count_for(frame, ViewportOverlayLayer::WireframeXray), 7u);
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
  EXPECT_EQ(bridge_header.find("const SceneDocument &"), std::string::npos);
  EXPECT_EQ(bridge_header.find("const AssetRegistry &"), std::string::npos);
  EXPECT_EQ(bridge_source.find("build_render_overlay_selected_object_snapshot"),
            std::string::npos);
  EXPECT_EQ(bridge_source.find("build_render_overlay_scene_wireframe_snapshot"),
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
            assets.mesh(mesh)->vertices.data());
  EXPECT_EQ(renderable_record->mesh.indices.size(),
            assets.mesh(mesh)->indices.size());
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
  EXPECT_TRUE(moved.dirty.scene_changed);
  EXPECT_TRUE(moved.dirty.transforms_changed);
  EXPECT_TRUE(moved_record->transform_dirty);
  EXPECT_FALSE(moved_record->mesh_dirty);
  EXPECT_FALSE(moved_record->material_dirty);
  EXPECT_FALSE(moved_record->binding_dirty);

  MeshAsset replacement = make_overlay_triangle_mesh();
  replacement.vertices.front().rgba = 0xff00ffffu;
  ASSERT_TRUE(assets.replace_mesh(mesh, std::move(replacement)));
  const RenderSceneSnapshot replaced =
      build_render_scene_snapshot(document, assets,
                                  {.previous = &moved, .frame_index = 4u});
  const auto replaced_record =
      std::ranges::find_if(replaced.meshes, [renderable](const auto &record) {
        return record.entity_id == renderable;
      });
  ASSERT_NE(replaced_record, replaced.meshes.end());
  EXPECT_TRUE(replaced.dirty.assets_changed);
  EXPECT_TRUE(replaced_record->mesh_dirty);
  EXPECT_FALSE(replaced_record->transform_dirty);
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
     SplitsComponentSelectionLinesIntoDepthTestedBatches) {
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

  ASSERT_EQ(plan.line_batches.size(), 2u);
  const auto depth_tested =
      std::find_if(plan.line_batches.begin(), plan.line_batches.end(),
                   [](const OverlayLineBatch &batch) {
                     return batch.state.depth_func ==
                            OverlayDepthFunc::GreaterOrEqual;
                   });
  ASSERT_NE(depth_tested, plan.line_batches.end());
  EXPECT_FLOAT_EQ(depth_tested->depth_bias, kOverlayLineDepthBias);
  EXPECT_TRUE(depth_tested->state.depth_culling);

  const auto draw_on_top =
      std::find_if(plan.line_batches.begin(), plan.line_batches.end(),
                   [](const OverlayLineBatch &batch) {
                     return batch.state.depth_func == OverlayDepthFunc::Always;
                   });
  ASSERT_NE(draw_on_top, plan.line_batches.end());
  EXPECT_FLOAT_EQ(draw_on_top->depth_bias, 0.0f);
  EXPECT_FALSE(draw_on_top->state.depth_culling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDepthStampsCullOutwardOccludedHandles) {
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
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_EQ(batch.mesh.indices.size(), 6u);
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

TEST(OverlayLineMeshBuilderTests, PerspectiveDepthBiasDoesNotMoveLineOrPointScreenPosition) {
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
      projected_line_start.device.z + camera.near_plane *
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

  const float expected_point_depth =
      projected_point.device.z + camera.near_plane *
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
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
      EXPECT_FALSE(batch.state.depth_culling);
    }
}

TEST(OverlayPointMeshBuilderTests,
     SplitsComponentSelectionPointsIntoDepthTestedBatches) {
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

  ASSERT_EQ(plan.point_batches.size(), 2u);
  const auto depth_tested =
      std::find_if(plan.point_batches.begin(), plan.point_batches.end(),
                   [](const OverlayPointBatch &batch) {
                     return batch.state.depth_func ==
                            OverlayDepthFunc::GreaterOrEqual;
                   });
  ASSERT_NE(depth_tested, plan.point_batches.end());
  EXPECT_FLOAT_EQ(depth_tested->depth_bias, kOverlayPointDepthBias);
  EXPECT_TRUE(depth_tested->state.depth_culling);

  const auto draw_on_top =
      std::find_if(plan.point_batches.begin(), plan.point_batches.end(),
                   [](const OverlayPointBatch &batch) {
                     return batch.state.depth_func == OverlayDepthFunc::Always;
                   });
  ASSERT_NE(draw_on_top, plan.point_batches.end());
  EXPECT_FLOAT_EQ(draw_on_top->depth_bias, 0.0f);
  EXPECT_FALSE(draw_on_top->state.depth_culling);
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
