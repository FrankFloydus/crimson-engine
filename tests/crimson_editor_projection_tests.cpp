////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "render/crimson/crimson_editor_projection.hpp"
#include "assets/mesh_collision_geometry_payload.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "render/overlay/overlay_face_fill_policy.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_settings.hpp"
#include "core/color_space.hpp"
#include "assets/curve_asset_payload.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace quader {
namespace {

class FaceDimensionSelectionProvider final
    : public ViewportOverlaySelectionFrameProvider {
public:
  [[nodiscard]] ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++build_count;
    ViewportOverlayFrame frame;
    frame.triangles.push_back({.layer = ViewportOverlayLayer::SelectedVisibleFace,
        .a = {-1, -1, -0.5f}, .b = {1, -1, 0.5f}, .c = {1, 1, 0},
        .source = ViewportOverlaySource::ComponentSelection,
        .element = {.object_id = 10, .vertex_id = 1, .edge_a = 2, .edge_b = 3,
                    .face_id = 1}});
    return frame;
  }

  mutable int build_count = 0;
};

TEST(CrimsonEditorProjectionTests,
     FaceDimensionsFollowPreviewEpochWithStableSelectionAndRemainEnabled) {
  FaceDimensionSelectionProvider provider;
  CrimsonEditorProjection projection;
  const auto settings = ViewportSettings::defaults();
  ViewportOverlaySubmissionRequest request;
  request.layer_mask = 4u;
  request.camera.eye = {0, 0, 10};
  request.camera.viewport_size = {800, 600};
  request.camera.projection = ViewportOverlayProjection::Orthographic;
  request.camera.orthographic_size = 16.0f;
  request.selection_source.source_identity = 10u;
  request.selection_source.content_revision = 1u;
  request.selection_source.selection_revision = 1u;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Face;
  request.selection_frame_provider = &provider;
  request.scene_selection_mode = RenderOverlaySelectionMode::Component;
  request.style_selection_context = ViewportOverlaySelectionContext::Face;
  request.overlays_visible = true;
  request.selection_dimensions_visible = true;
  const auto verify_submission = [&](const ViewportOverlaySubmissionResult &submission,
                                     float delta) {
    ASSERT_TRUE(submission.selection_dimension_bounds);
    const auto &bounds = *submission.selection_dimension_bounds;
    EXPECT_FLOAT_EQ(bounds.min.x, delta - 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, delta + 1.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 1.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -0.5f);
    EXPECT_FLOAT_EQ(bounds.max.z, 0.5f);
    const auto frame = projection.frame(4u, {}, {0u, 0u, 800u, 600u},
                                        {800u, 600u}, {}, true, 1u);
    ASSERT_TRUE(frame);
    std::size_t dimension_vertices = 0u;
    for (const auto &batch : frame->batches) {
      if (batch.primitive != crimson::shared::EditorPrimitive::Ribbon ||
          batch.stencil.read_mask != kOverlayActiveWireStencilMask) continue;
      for (const auto &position : batch.geometry.positions.values) {
        for (const auto endpoint : {position.position, position.endpoint}) {
          EXPECT_GE(endpoint[0], delta - 1.00001f);
          EXPECT_LE(endpoint[0], delta + 1.00001f);
          EXPECT_GE(endpoint[1], -1.00001f);
          EXPECT_LE(endpoint[1], 1.00001f);
          EXPECT_GE(endpoint[2], -0.50001f);
          EXPECT_LE(endpoint[2], 0.50001f);
        }
        ++dimension_vertices;
      }
    }
    EXPECT_GT(dimension_vertices, 0u);
  };
  verify_submission(projection.submit(request, settings), 0.0f);
  auto geometry = std::make_shared<MeshGeometryPayload>();
  geometry->vertices = {
      {.position = {-1, -1, -0.5f}, .source_vertex_id = 1},
      {.position = {1, -1, 0.5f}, .source_vertex_id = 2},
      {.position = {1, 1, 0}, .source_vertex_id = 3}};
  geometry->indices = {0u, 1u, 2u};
  for (const float delta : {3.0f, 6.0f}) {
    SCOPED_TRACE(delta);
    auto preview = std::make_shared<RenderToolPreviewSceneSnapshot>();
    auto coordinates = std::make_shared<MeshGeometryProjection>();
    coordinates->source_geometry = geometry;
    coordinates->positions = std::make_shared<const std::vector<QVec3>>(
        std::vector<QVec3>{{delta - 1, -1, -0.5f}, {delta + 1, -1, 0.5f},
                           {delta + 1, 1, 0}});
    RenderToolPreviewMeshRecord mesh;
    mesh.source_object_id = 10;
    mesh.source_entity = 101u;
    mesh.mesh.share_geometry(geometry);
    mesh.dynamic_positions = true;
    mesh.suppress_source = true;
    mesh.dynamic_position_payload = coordinates->positions;
    mesh.geometry_projection = coordinates;
    preview->meshes.push_back(std::move(mesh));
    projection.set_preview(preview);
    request.selection_dimension_preview_meshes = preview->meshes;
    const auto moved = projection.submit(request, settings);
    EXPECT_TRUE(moved.dynamic_submitted);
    EXPECT_FALSE(moved.selection_source_refreshed);
    verify_submission(moved, delta);
    const auto retained = projection.frame(4u, {}, {0u, 0u, 800u, 600u},
                                           {800u, 600u}, {}, true, 1u);
    const auto work = projection.counters();
    for (int unchanged = 0; unchanged < 3; ++unchanged) {
      projection.set_preview(preview);
      EXPECT_TRUE(projection.submit(request, settings).camera_only);
      EXPECT_EQ(projection.frame(4u, {}, {0u, 0u, 800u, 600u},
                                 {800u, 600u}, {}, true, 1u), retained);
    }
    EXPECT_EQ(projection.counters().layer_builds, work.layer_builds);
    EXPECT_EQ(projection.counters().position_packs, work.position_packs);
    EXPECT_EQ(projection.counters().attribute_packs, work.attribute_packs);
    EXPECT_EQ(projection.counters().index_packs, work.index_packs);
    EXPECT_EQ(provider.build_count, 1);
  }
  projection.set_preview({});
  request.selection_dimension_preview_meshes = {};
  const auto canceled = projection.submit(request, settings);
  EXPECT_TRUE(canceled.dynamic_submitted);
  verify_submission(canceled, 0.0f);
  EXPECT_EQ(provider.build_count, 1);
}

TEST(CrimsonEditorProjectionTests, ActiveObjectSwapMovesMaskClassAndRetainsSourceStreams) {
  auto geometry = std::make_shared<MeshGeometryPayload>();
  geometry->vertices = {
      {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
      {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
      {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
  geometry->indices = {0u, 1u, 2u, 0u, 2u, 3u};
  RenderOverlaySelectedObjectSnapshot selected;
  selected.selection_revision = 1u;
  for (EntityId entity : {101u, 102u}) {
    RenderOverlayMeshSnapshot mesh;
    mesh.entity = entity;
    mesh.selected = true;
    mesh.active_selected = entity == 101u;
    mesh.geometry_payload = geometry;
    mesh.vertices = geometry->vertices;
    mesh.indices = geometry->indices;
    mesh.world_transform.values[12] = entity == 101u ? -2.0f : 2.0f;
    selected.meshes.push_back(mesh);
  }
  ViewportPersistentOverlayStyleSnapshot style;
  style.selection_wire_color = 0xff00ffffu;
  style.active_selection_wire_color = 0xffb469ffu;
  style.selection_edge_line_width_pixels = 2.0f;
  style.active_selection_line_width_pixels = 2.0f;
  CrimsonEditorProjection projection;
  EXPECT_TRUE(projection.update_selected(4u, &selected, nullptr, style, true));
  const auto first = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u);
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->selection);
  ASSERT_EQ(first->selection->ordinary.size(), 1u);
  ASSERT_EQ(first->selection->active.size(), 1u);
  EXPECT_FLOAT_EQ(first->selection->active.front().world_from_local.values[12], -2.0f);
  EXPECT_GT(first->selection->active_color[2], first->selection->active_color[1]);
  const auto work = projection.counters();

  ++selected.selection_revision;
  selected.meshes[0].active_selected = false;
  selected.meshes[1].active_selected = true;
  EXPECT_TRUE(projection.update_selected(4u, &selected, nullptr, style, true));
  const auto second = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u);
  ASSERT_TRUE(second);
  ASSERT_TRUE(second->selection);
  ASSERT_EQ(second->selection->ordinary.size(), 1u);
  ASSERT_EQ(second->selection->active.size(), 1u);
  EXPECT_FLOAT_EQ(second->selection->active.front().world_from_local.values[12], 2.0f);
  EXPECT_FLOAT_EQ(second->selection->ordinary.front().world_from_local.values[12], -2.0f);
  EXPECT_EQ(second->selection->active_color, first->selection->active_color);
  EXPECT_EQ(second->selection->ordinary_color, first->selection->ordinary_color);
  EXPECT_EQ(second->selection->ordinary.front().geometry.positions.pin,
            first->selection->active.front().geometry.positions.pin);
  EXPECT_EQ(second->selection->active.front().geometry.positions.pin,
            first->selection->ordinary.front().geometry.positions.pin);
  EXPECT_EQ(projection.counters().position_packs, work.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs, work.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs, work.index_packs);
  EXPECT_FALSE(selected.meshes[0].active_selected);
  EXPECT_TRUE(selected.meshes[1].active_selected);
  EXPECT_EQ(geometry->indices.back(), 3u);
}

TEST(CrimsonEditorProjectionTests, CollisionTrianglesAndWiresRetainStreamsAcrossPresentationChanges) {
  auto geometry = std::make_shared<MeshCollisionGeometryPayload>();
  geometry->content_id = 10u;
  geometry->faces = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0},
                     {0, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  RenderOverlayCollisionSnapshot collision;
  collision.visible = true;
  collision.scene_revision = 1u;
  collision.selection_revision = 1u;
  for (EntityId entity : {101u, 102u}) {
    RenderOverlayCollisionMeshSnapshot mesh;
    mesh.entity = entity;
    mesh.collision_payload = geometry;
    mesh.selected = entity == 102u;
    mesh.world_transform.values[12] = entity == 101u ? 0.0f : 2.0f;
    if (entity == 102u) mesh.world_transform.values[0] = -1.0f;
    collision.meshes.push_back(mesh);
  }
  RenderOverlaySelectedObjectSnapshot presentation;
  presentation.style.raster_scale = 1.75f;
  ASSERT_TRUE(presentation.raster_groups.add(4u, presentation.style));
  CrimsonEditorProjection projection;
  ViewportOverlayCamera camera;
  (void)projection.update_overlay_cameras(0u, 4u, camera);
  projection.sync_collision(collision, presentation);
  const auto first = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u);
  ASSERT_TRUE(first);
  ASSERT_EQ(first->batches.size(), 4u);
  const auto &fill = first->batches[0];
  EXPECT_EQ(fill.primitive, crimson::shared::EditorPrimitive::Face);
  EXPECT_EQ(fill.depth, crimson::shared::EditorDepth::GreaterOrEqual);
  EXPECT_TRUE(fill.cull_back);
  EXPECT_FALSE(fill.depth_write);
  EXPECT_FALSE(fill.polygon_offset);
  EXPECT_EQ(fill.transparency, crimson::shared::EditorTransparency::Default);
  EXPECT_FLOAT_EQ(fill.alpha, collision.color.a * 0.0625f);
  EXPECT_FLOAT_EQ(fill.tint[1],srgb_to_linear(collision.color.g));
  EXPECT_EQ(fill.geometry.vertices.values.front().rgba,0xffffffffu);
  EXPECT_EQ(fill.geometry.indices.values.size(), 6u);
  EXPECT_EQ(fill.geometry.positions.pin, first->batches[1].geometry.positions.pin);
  const auto batch_at=[](const auto &frame, crimson::shared::EditorPrimitive primitive,float x) {
    const auto found=std::ranges::find_if(frame.batches,[&](const auto &batch) {
      return batch.primitive==primitive && batch.world_from_local.values[12]==x;
    });
    return found==frame.batches.end() ? nullptr : &*found;
  };
  const auto *mirrored=batch_at(*first,crimson::shared::EditorPrimitive::Face,2.0f);
  ASSERT_NE(mirrored,nullptr);
  EXPECT_FLOAT_EQ(mirrored->world_from_local.values[0], -1.0f);
  const auto *ordinary_pointer=batch_at(*first,crimson::shared::EditorPrimitive::Ribbon,0.0f);
  const auto *selected_pointer=batch_at(*first,crimson::shared::EditorPrimitive::Ribbon,2.0f);
  ASSERT_NE(ordinary_pointer,nullptr);
  ASSERT_NE(selected_pointer,nullptr);
  const auto &ordinary=*ordinary_pointer;
  const auto &selected=*selected_pointer;
  EXPECT_EQ(ordinary.primitive, crimson::shared::EditorPrimitive::Ribbon);
  EXPECT_EQ(ordinary.depth, crimson::shared::EditorDepth::GreaterOrEqual);
  EXPECT_FLOAT_EQ(ordinary.width_pixels, 1.75f);
  EXPECT_FLOAT_EQ(ordinary.alpha, collision.color.a * 0.4f);
  EXPECT_FLOAT_EQ(selected.alpha, collision.color.a * 1.6f);
  EXPECT_FALSE(ordinary.depth_write);
  EXPECT_FALSE(ordinary.surface_depth);
  EXPECT_FALSE(ordinary.polygon_offset);
  // Five deduplicated triangle edges include the collision diagonal once.
  EXPECT_EQ(ordinary.geometry.positions.values.size(), 20u);
  EXPECT_EQ(ordinary.geometry.indices.values.size(), 30u);
  EXPECT_EQ(ordinary.geometry.positions.pin, selected.geometry.positions.pin);
  const auto work = projection.counters();

  camera.eye.x += 1.0f;
  (void)projection.update_overlay_cameras(0u, 4u, camera);
  const auto camera_frame = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u);
  ASSERT_TRUE(camera_frame);
  EXPECT_EQ(camera_frame->batches[0].geometry.positions.pin, fill.geometry.positions.pin);
  ++collision.selection_revision;
  collision.meshes[0].selected = true;
  collision.meshes[1].selected = false;
  projection.sync_collision(collision, presentation);
  const auto swapped = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u);
  ASSERT_TRUE(swapped);
  ASSERT_EQ(swapped->batches.size(), 4u);
  const auto *now_selected=batch_at(*swapped,crimson::shared::EditorPrimitive::Ribbon,0.0f);
  const auto *now_ordinary=batch_at(*swapped,crimson::shared::EditorPrimitive::Ribbon,2.0f);
  ASSERT_NE(now_selected,nullptr);
  ASSERT_NE(now_ordinary,nullptr);
  EXPECT_FLOAT_EQ(now_selected->alpha, collision.color.a * 1.6f);
  EXPECT_FLOAT_EQ(now_ordinary->alpha, collision.color.a * 0.4f);
  EXPECT_EQ(projection.counters().position_packs, work.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs, work.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs, work.index_packs);

  const auto wireframe = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 1u, false);
  ASSERT_TRUE(wireframe);
  ASSERT_EQ(wireframe->batches.size(), 2u);
  EXPECT_EQ(wireframe->batches[0].primitive, crimson::shared::EditorPrimitive::Ribbon);
  const auto hidden = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, false, 1u);
  ASSERT_TRUE(hidden);
  EXPECT_TRUE(hidden->batches.empty());

  auto preview = std::make_shared<RenderToolPreviewSceneSnapshot>();
  preview->suppression.entities = {101u};
  preview->suppression.signature = 99u;
  projection.set_preview(preview);
  const auto suppressed = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 2u);
  ASSERT_TRUE(suppressed);
  ASSERT_EQ(suppressed->batches.size(), 2u);
  EXPECT_FLOAT_EQ(suppressed->batches[0].world_from_local.values[12], 2.0f);
  EXPECT_EQ(suppressed->batches[0].geometry.positions.pin, fill.geometry.positions.pin);
  projection.set_preview({});
  const auto restored = projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 3u);
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->batches.size(), 4u);
  EXPECT_EQ(restored->batches[0].geometry.positions.pin, fill.geometry.positions.pin);
  collision.color.a=1.0f;
  projection.sync_collision(collision,presentation);
  const auto opaque=projection.frame(4u, {}, {0u, 0u, 64u, 64u}, {64u, 64u}, {}, true, 3u);
  ASSERT_TRUE(opaque);
  const auto *opaque_wire=batch_at(*opaque,crimson::shared::EditorPrimitive::Ribbon,0.0f);
  ASSERT_NE(opaque_wire,nullptr);
  EXPECT_FLOAT_EQ(opaque_wire->alpha,1.6f);
  EXPECT_EQ(projection.counters().position_packs,work.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs,work.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs,work.index_packs);
  collision.color={0.8f,0.2f,0.1f,1.0f};
  projection.sync_collision(collision,presentation);
  const auto recolored=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,3u);
  ASSERT_TRUE(recolored);
  const auto *recolored_wire=batch_at(*recolored,crimson::shared::EditorPrimitive::Ribbon,0.0f);
  ASSERT_NE(recolored_wire,nullptr);
  EXPECT_FLOAT_EQ(recolored_wire->tint[0],srgb_to_linear(0.8f));
  EXPECT_FLOAT_EQ(recolored_wire->tint[1],srgb_to_linear(0.2f));
  EXPECT_FLOAT_EQ(recolored_wire->tint[2],srgb_to_linear(0.1f));
  EXPECT_EQ(recolored_wire->geometry.positions.pin,ordinary.geometry.positions.pin);
  EXPECT_EQ(recolored_wire->geometry.vertices.pin,ordinary.geometry.vertices.pin);
  EXPECT_EQ(recolored_wire->geometry.indices.pin,ordinary.geometry.indices.pin);
  EXPECT_EQ(projection.counters().position_packs,work.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs,work.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs,work.index_packs);
  EXPECT_EQ(geometry->faces.size(), 6u);
}

TEST(CrimsonEditorProjectionTests, ObjectFaceTintUsesMaskGeometryAndTracksFaceOnlyStyleChanges) {
  auto geometry=std::make_shared<MeshGeometryPayload>();
  geometry->vertices={{{0,0,0},{0,0,1},{0,0}},{{1,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{0,1}}};
  geometry->indices={0u,1u,2u};
  RenderOverlaySelectedObjectSnapshot selected;
  RenderOverlayMeshSnapshot mesh;
  mesh.entity=101u;
  mesh.mesh_id=1u;
  mesh.selected=true;
  mesh.geometry_payload=geometry;
  mesh.vertices=geometry->vertices;
  mesh.indices=geometry->indices;
  selected.meshes.push_back(mesh);
  ViewportPersistentOverlayStyleSnapshot style;
  style.selection_face_color=0x081facffu;
  style.face_signature=1u;
  CrimsonEditorProjection projection;
  ASSERT_TRUE(projection.update_selected(4u,&selected,nullptr,style,true));
  const auto first=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->selection);
  ASSERT_EQ(first->selection->ordinary.size(),1u);
  ASSERT_EQ(first->batches.size(),4u);
  std::size_t mask_count=0u,color_count=0u;
  for (const auto &batch : first->batches) {
    EXPECT_EQ(batch.primitive,crimson::shared::EditorPrimitive::Face);
    EXPECT_EQ(batch.depth,crimson::shared::EditorDepth::GreaterOrEqual);
    EXPECT_EQ(batch.transparency,crimson::shared::EditorTransparency::TwoSides);
    EXPECT_TRUE(batch.polygon_offset);
    EXPECT_FALSE(batch.depth_write);
    EXPECT_EQ(batch.geometry.positions.pin,first->selection->ordinary.front().geometry.positions.pin);
    EXPECT_EQ(batch.geometry.indices.pin,first->selection->ordinary.front().geometry.indices.pin);
    ASSERT_FALSE(batch.geometry.vertices.values.empty());
    EXPECT_EQ(batch.geometry.vertices.pin,first->selection->ordinary.front().geometry.vertices.pin);
    EXPECT_EQ(batch.geometry.vertices.values.front().rgba,0xffffffffu);
    const auto linear=rgba8_srgb_to_linear(style.selection_face_color);
    EXPECT_FLOAT_EQ(batch.tint[0],static_cast<float>(linear&0xffu)/255.0f);
    EXPECT_FLOAT_EQ(batch.tint[1],static_cast<float>((linear>>8u)&0xffu)/255.0f);
    EXPECT_FLOAT_EQ(batch.tint[2],static_cast<float>((linear>>16u)&0xffu)/255.0f);
    EXPECT_FLOAT_EQ(batch.alpha,8.0f/255.0f);
    mask_count+=!batch.color_write;
    color_count+=batch.color_write;
  }
  EXPECT_EQ(mask_count,2u);
  EXPECT_EQ(color_count,2u);
  const auto work=projection.counters();
  style.selection_face_color=0x590000ffu;
  ++style.face_signature;
  ASSERT_TRUE(projection.update_selected(4u,&selected,nullptr,style,true));
  const auto tinted=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(tinted);
  ASSERT_EQ(tinted->batches.size(),4u);
  EXPECT_EQ(tinted->batches.front().geometry.positions.pin,first->batches.front().geometry.positions.pin);
  EXPECT_EQ(tinted->batches.front().geometry.indices.pin,first->batches.front().geometry.indices.pin);
  EXPECT_EQ(tinted->batches.front().geometry.vertices.pin,first->batches.front().geometry.vertices.pin);
  EXPECT_FLOAT_EQ(tinted->batches.front().tint[0],1.0f);
  EXPECT_FLOAT_EQ(tinted->batches.front().tint[1],0.0f);
  EXPECT_FLOAT_EQ(tinted->batches.front().alpha,89.0f/255.0f);
  EXPECT_EQ(projection.counters().position_packs,work.position_packs);
  EXPECT_EQ(projection.counters().index_packs,work.index_packs);
  EXPECT_EQ(projection.counters().attribute_packs,work.attribute_packs);
  EXPECT_FALSE(projection.update_selected(4u,&selected,nullptr,style,true));
  EXPECT_EQ(projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u),tinted);
  const auto wireframe=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u,false);
  ASSERT_TRUE(wireframe);
  EXPECT_TRUE(wireframe->batches.empty());
  style.selection_face_color=0x000000ffu;
  ++style.face_signature;
  ASSERT_TRUE(projection.update_selected(4u,&selected,nullptr,style,true));
  const auto transparent=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(transparent);
  for (const auto &batch : transparent->batches) {
    EXPECT_EQ(batch.stencil.write_mask,0u);
    EXPECT_FLOAT_EQ(batch.alpha,0.0f);
  }
  auto wires=std::make_shared<MeshWireGeometryPayload>();
  wires->segments.push_back({.start={0,0,0},.end={1,0,0}});
  auto wire_mesh=mesh;
  wire_mesh.selected=false;
  wire_mesh.shaded_wire=true;
  wire_mesh.wire_geometry_payload=wires;
  wire_mesh.wire_segments=wires->segments;
  RenderOverlaySceneWireframeSnapshot shaded_snapshot;
  shaded_snapshot.wireframe_on_shaded_layer_mask=4u;
  shaded_snapshot.meshes.push_back(wire_mesh);
  projection.sync_wireframe(shaded_snapshot);
  const auto one_x=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(one_x);
  const auto ribbon=[](const auto &frame) {
    return std::ranges::find_if(frame.batches,[](const auto &batch) {
      return batch.primitive==crimson::shared::EditorPrimitive::Ribbon;
    });
  };
  const auto one_x_wire=ribbon(*one_x);
  ASSERT_NE(one_x_wire,one_x->batches.end());
  const auto before_scale=projection.counters();
  shaded_snapshot.style.raster_scale=2.0f;
  projection.sync_wireframe(shaded_snapshot);
  const auto two_x=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(two_x);
  const auto two_x_wire=ribbon(*two_x);
  ASSERT_NE(two_x_wire,two_x->batches.end());
  EXPECT_FLOAT_EQ(two_x_wire->width_pixels,one_x_wire->width_pixels*2.0f);
  EXPECT_FLOAT_EQ(two_x_wire->feather_pixels,one_x_wire->feather_pixels);
  EXPECT_FLOAT_EQ(two_x_wire->feather_pixels,kOverlayLineFeatherPixels);
  EXPECT_EQ(two_x_wire->geometry.positions.pin,one_x_wire->geometry.positions.pin);
  EXPECT_EQ(projection.counters().position_packs,before_scale.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs,before_scale.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs,before_scale.index_packs);
  shaded_snapshot.wireframe_on_shaded_layer_mask=0u;
  shaded_snapshot.layer_mask=4u;
  shaded_snapshot.style.raster_scale=1.0f;
  shaded_snapshot.style.selection_edge_line_width_pixels=3.25f;
  shaded_snapshot.meshes.front().shaded_wire=false;
  auto selected_wire=shaded_snapshot.meshes.front();
  selected_wire.selected=true;
  shaded_snapshot.meshes.push_back(selected_wire);
  projection.sync_wireframe(shaded_snapshot);
  const auto ordinary_one_x=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(ordinary_one_x);
  const auto widths=[](const auto &frame) {
    std::vector<float> result;
    for (const auto &batch : frame.batches) if (batch.primitive==crimson::shared::EditorPrimitive::Ribbon) {
      result.push_back(batch.width_pixels);
      EXPECT_FLOAT_EQ(batch.feather_pixels,kOverlayLineFeatherPixels);
    }
    std::ranges::sort(result);
    return result;
  };
  const auto one_x_widths=widths(*ordinary_one_x);
  ASSERT_EQ(one_x_widths.size(),2u);
  EXPECT_FLOAT_EQ(one_x_widths[0],kOverlayWireframeXrayLineWidthPixels);
  EXPECT_FLOAT_EQ(one_x_widths[1],3.25f);
  const auto before_wire_scale=projection.counters();
  shaded_snapshot.style.raster_scale=2.0f;
  projection.sync_wireframe(shaded_snapshot);
  const auto ordinary_two_x=projection.frame(4u,{}, {0u,0u,64u,64u},{64u,64u},{},true,1u);
  ASSERT_TRUE(ordinary_two_x);
  const auto two_x_widths=widths(*ordinary_two_x);
  ASSERT_EQ(two_x_widths.size(),2u);
  EXPECT_FLOAT_EQ(two_x_widths[0],one_x_widths[0]*2.0f);
  // Selected style widths already contain their physical raster scale.
  EXPECT_FLOAT_EQ(two_x_widths[1],one_x_widths[1]);
  EXPECT_EQ(projection.counters().position_packs,before_wire_scale.position_packs);
  EXPECT_EQ(projection.counters().attribute_packs,before_wire_scale.attribute_packs);
  EXPECT_EQ(projection.counters().index_packs,before_wire_scale.index_packs);
}

TEST(CrimsonEditorProjectionTests, CurveAndLightPreviewsUseCanonicalGeometryAndSuppressCommittedCurve) {
  auto samples=std::make_shared<CurveSamplePayload>();samples->samples.resize(2);samples->samples[0].position={0,0,0};samples->samples[1].position={2,0,0};samples->samples[1].parameter=1;
  RenderOverlaySelectionSnapshot scene;scene.scene_revision=1;RenderOverlayCurveSnapshot committed;committed.entity=601;committed.sample_payload=samples;scene.curves.push_back(committed);
  auto preview=std::make_shared<RenderToolPreviewSceneSnapshot>();preview->revision=2;preview->suppression.entities.push_back(601);RenderToolPreviewCurveRecord curve;curve.source_entity=601;curve.sample_payload=samples;curve.instance_transforms.resize(1);curve.instance_transforms[0].values[12]=5;curve.suppress_source=true;preview->curves.push_back(curve);RenderToolPreviewLightRecord light;light.source_entity=701;light.light.type=LightType::Directional;light.instance_transforms.resize(1);light.instance_transforms[0].values[12]=8;preview->lights.push_back(light);
  CrimsonEditorProjection projection;projection.set_preview(preview);ViewportOverlaySubmissionRequest request;request.viewport_index=0;request.layer_mask=4;request.camera.eye={0,0,10};request.camera.viewport_size={800,600};request.camera.projection=ViewportOverlayProjection::Orthographic;request.camera.orthographic_size=20;request.overlays_visible=true;request.scene_overlay_requested=true;request.selection_overlays_visible=false;request.selection_dimensions_visible=false;request.scene_overlay_snapshot=&scene;
  const auto settings=ViewportSettings::defaults();projection.submit(request,settings);const auto frame=projection.frame(4,{}, {0,0,800,600},{800,600},{},true,1);ASSERT_TRUE(frame);std::size_t ribbons=0;for(const auto& batch:frame->batches)if(batch.primitive==crimson::shared::EditorPrimitive::Ribbon)for(const auto& position:batch.geometry.positions.values){EXPECT_GE(position.position[0],4.99f);EXPECT_GE(position.endpoint[0],4.99f);++ribbons;}EXPECT_GT(ribbons,0u);
  const auto before=projection.counters();const auto repeated=projection.submit(request,settings);EXPECT_TRUE(repeated.camera_only);projection.frame(4,{}, {0,0,800,600},{800,600},{},true,1);EXPECT_EQ(projection.counters().position_packs,before.position_packs);EXPECT_EQ(projection.counters().layer_builds,before.layer_builds);
  projection.set_preview({});projection.submit(request,settings);const auto restored=projection.frame(4,{}, {0,0,800,600},{800,600},{},true,2);ASSERT_TRUE(restored);bool original=false;for(const auto& batch:restored->batches)if(batch.primitive==crimson::shared::EditorPrimitive::Ribbon)for(const auto& position:batch.geometry.positions.values)if(position.position[0]<.01f)original=true;EXPECT_TRUE(original);
}

} // namespace
} // namespace quader
