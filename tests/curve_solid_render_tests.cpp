////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "render/curve_solid_geometry_cache.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

CurveAsset make_straight_curve_asset() {
  const std::array<modeling::CurvePointSpec, 2u> points{{
      {.position = {0.0f, 0.0f, 0.0f}},
      {.position = {4.0f, 0.0f, 0.0f}},
  }};
  modeling::Result<modeling::CurveDocument> document =
      modeling::CurveDocument::make_bezier(points);
  EXPECT_TRUE(document.ok());
  CurveAsset asset;
  asset.name = "Straight Curve";
  if (document.ok()) {
    asset.document = std::make_shared<const modeling::CurveDocument>(
        std::move(document).value());
  }
  return asset;
}

CurveAsset make_repeated_anchor_curve_asset() {
  const std::array<modeling::CurvePointSpec, 2u> points{{
      {.position = {1.0f, 2.0f, 3.0f}},
      {.position = {1.0f, 2.0f, 3.0f}},
  }};
  modeling::Result<modeling::CurveDocument> document =
      modeling::CurveDocument::make_bezier(points);
  EXPECT_TRUE(document.ok());
  CurveAsset asset;
  asset.name = "Repeated Anchor Curve";
  if (document.ok()) {
    asset.document = std::make_shared<const modeling::CurveDocument>(
        std::move(document).value());
  }
  return asset;
}

void expect_bounds_near(const SceneBounds &actual, const SceneBounds &expected) {
  EXPECT_NEAR(actual.min.x, expected.min.x, 1.0e-5f);
  EXPECT_NEAR(actual.min.y, expected.min.y, 1.0e-5f);
  EXPECT_NEAR(actual.min.z, expected.min.z, 1.0e-5f);
  EXPECT_NEAR(actual.max.x, expected.max.x, 1.0e-5f);
  EXPECT_NEAR(actual.max.y, expected.max.y, 1.0e-5f);
  EXPECT_NEAR(actual.max.z, expected.max.z, 1.0e-5f);
}

TEST(CurveSolidGeometryCacheTests,
     KeysExactCurveContentAndEveryTessellationSetting) {
  const CurveAsset asset = make_straight_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveSolidGeometryCache cache;

  const auto initial = cache.geometry(*asset.document, 0.1, 8u);
  ASSERT_NE(initial, nullptr);
  EXPECT_FALSE(initial->vertices.empty());
  EXPECT_FALSE(initial->indices.empty());
  EXPECT_EQ(cache.geometry(*asset.document, 0.1, 8u), initial);

  const auto wider = cache.geometry(*asset.document, 0.2, 8u);
  const auto denser = cache.geometry(*asset.document, 0.1, 12u);
  modeling::CurveSamplingOptions sampling;
  sampling.tolerance = 0.005;
  const auto finer = cache.geometry(*asset.document, 0.1, 8u, sampling);
  ASSERT_NE(wider, nullptr);
  ASSERT_NE(denser, nullptr);
  ASSERT_NE(finer, nullptr);
  EXPECT_NE(wider, initial);
  EXPECT_NE(denser, initial);
  EXPECT_NE(finer, initial);

  modeling::CurveDocument edited = *asset.document;
  const modeling::CurveControlPoint endpoint = edited.points().back();
  ASSERT_TRUE(edited.set_point(
      endpoint.id,
      {.position = {5.0f, 0.0f, 0.0f},
       .incoming_handle = endpoint.incoming_handle,
       .outgoing_handle = endpoint.outgoing_handle}).ok());
  const auto edited_geometry = cache.geometry(edited, 0.1, 8u);
  ASSERT_NE(edited_geometry, nullptr);
  EXPECT_NE(edited_geometry, initial);

  const CurveSolidGeometryCacheStats stats = cache.stats();
  EXPECT_EQ(stats.build_count, 5u);
  EXPECT_EQ(stats.hit_count, 1u);
  EXPECT_EQ(stats.failure_count, 0u);
  EXPECT_EQ(stats.entry_count, 5u);
}

TEST(CurveSolidGeometryCacheTests, RetainsFailedDerivationsForTheExactKey) {
  const modeling::CurveDocument degenerate;
  CurveSolidGeometryCache cache;
  EXPECT_EQ(cache.geometry(degenerate, 0.1, 8u), nullptr);
  EXPECT_EQ(cache.geometry(degenerate, 0.1, 8u), nullptr);
  const CurveSolidGeometryCacheStats stats = cache.stats();
  EXPECT_EQ(stats.build_count, 1u);
  EXPECT_EQ(stats.hit_count, 1u);
  EXPECT_EQ(stats.failure_count, 1u);
  EXPECT_EQ(stats.entry_count, 1u);
}

TEST(CurveSolidGeometryCacheTests,
     EvictsLeastRecentPayloadsToRespectTheCumulativeByteBudget) {
  const CurveAsset asset = make_straight_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveSolidGeometryCache measuring_cache;
  const auto narrow = measuring_cache.geometry(*asset.document, 0.1, 8u);
  const auto wide = measuring_cache.geometry(*asset.document, 0.2, 8u);
  ASSERT_NE(narrow, nullptr);
  ASSERT_NE(wide, nullptr);
  const std::size_t smaller =
      std::min(narrow->byte_size(), wide->byte_size());
  const std::size_t byte_capacity =
      std::max(narrow->byte_size(), wide->byte_size()) + smaller / 2u;

  CurveSolidGeometryCache cache(8u, byte_capacity);
  ASSERT_NE(cache.geometry(*asset.document, 0.1, 8u), nullptr);
  ASSERT_NE(cache.geometry(*asset.document, 0.2, 8u), nullptr);
  CurveSolidGeometryCacheStats stats = cache.stats();
  EXPECT_EQ(stats.build_count, 2u);
  EXPECT_EQ(stats.eviction_count, 1u);
  EXPECT_EQ(stats.entry_count, 1u);
  EXPECT_LE(stats.cached_bytes, byte_capacity);

  ASSERT_NE(cache.geometry(*asset.document, 0.1, 8u), nullptr);
  stats = cache.stats();
  EXPECT_EQ(stats.build_count, 3u);
  EXPECT_EQ(stats.eviction_count, 2u);
  EXPECT_EQ(stats.entry_count, 1u);
  EXPECT_LE(stats.cached_bytes, byte_capacity);
}

TEST(CurveSolidGeometryCacheTests,
     OversizePayloadsRemainUsableWithoutBeingPinned) {
  const CurveAsset asset = make_straight_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  CurveSolidGeometryCache cache(8u, 1u);
  ASSERT_NE(cache.geometry(*asset.document, 0.1, 8u), nullptr);
  ASSERT_NE(cache.geometry(*asset.document, 0.1, 8u), nullptr);
  const CurveSolidGeometryCacheStats stats = cache.stats();
  EXPECT_EQ(stats.build_count, 2u);
  EXPECT_EQ(stats.oversize_bypass_count, 2u);
  EXPECT_EQ(stats.entry_count, 0u);
  EXPECT_EQ(stats.cached_bytes, 0u);
}

TEST(CurveSolidRenderExtractionTests,
     PublishesPinnedMeshAndRetainsAuthoredCurveOverlay) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_straight_curve_asset());
  ASSERT_NE(curve_asset, kInvalidAssetId);
  const EntityId curve =
      document.create_entity("Solid Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(
      curve, {.curve = curve_asset,
              .solidify = true,
              .solidify_resolution = 6,
              .solidify_radius = 0.25f}));

  CurveSolidGeometryCache cache;
  const RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.curve_solid_geometry_cache = &cache});
  ASSERT_TRUE(snapshot.has_solidified_curves);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const RenderSceneMeshRecord &record = snapshot.meshes.front();
  EXPECT_EQ(record.entity_id, curve);
  EXPECT_EQ(record.mesh.mesh_id, curve_asset);
  EXPECT_TRUE(record.renderable());
  EXPECT_TRUE(record.cast_shadows());
  EXPECT_FALSE(record.double_sided());
  EXPECT_FALSE(record.material.present);
  EXPECT_FALSE(record.material_reference.valid());
  EXPECT_TRUE(snapshot.live_mesh_entities.contains(curve));
  ASSERT_NE(record.mesh.geometry_payload, nullptr);
  EXPECT_EQ(record.mesh.vertices.data(),
            record.mesh.geometry_payload->vertices.data());

  RenderOverlaySnapshotCache overlay_cache;
  const RenderOverlaySelectionSnapshot &overlay =
      overlay_cache.selection_snapshot(document, assets);
  ASSERT_EQ(overlay.curves.size(), 1u);
  EXPECT_EQ(overlay.curves.front().entity, curve);
}

TEST(CurveSolidRenderExtractionTests,
     ReusesGeometryAcrossUnchangedAndTransformOnlyFrames) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_straight_curve_asset());
  const EntityId curve =
      document.create_entity("Moving Solid Curve", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(
      curve, {.curve = curve_asset,
              .solidify = true,
              .solidify_resolution = 8,
              .solidify_radius = 0.1f}));
  CurveSolidGeometryCache cache;
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = 1u,
                         .curve_solid_geometry_cache = &cache});
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const auto geometry = snapshot.meshes.front().mesh.geometry_payload;
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(cache.stats().build_count, 1u);

  RenderSceneSnapshot unchanged = build_render_scene_snapshot(
      document, assets,
      {.previous = &snapshot,
       .frame_index = 2u,
       .curve_solid_geometry_cache = &cache});
  ASSERT_TRUE(unchanged.reused_previous_records);
  ASSERT_EQ(unchanged.meshes.size(), 1u);
  EXPECT_EQ(unchanged.meshes.front().mesh.geometry_payload, geometry);
  EXPECT_EQ(cache.stats().build_count, 1u);

  ASSERT_TRUE(document.set_transform(
      curve, {.location = {3.0f, 4.0f, 5.0f},
              .scale = {2.0f, 3.0f, 4.0f}}));
  ASSERT_TRUE(try_refresh_render_scene_snapshot_transforms(
      unchanged, document, assets, 3u));
  ASSERT_EQ(unchanged.meshes.size(), 1u);
  EXPECT_EQ(unchanged.meshes.front().mesh.geometry_payload, geometry);
  EXPECT_TRUE(unchanged.meshes.front().transform_dirty);
  EXPECT_FLOAT_EQ(unchanged.meshes.front().world_transform.values[12], 3.0f);
  EXPECT_FLOAT_EQ(unchanged.meshes.front().world_transform.values[13], 4.0f);
  EXPECT_FLOAT_EQ(unchanged.meshes.front().world_transform.values[14], 5.0f);
  EXPECT_EQ(cache.stats().build_count, 1u);
}

TEST(CurveSolidRenderExtractionTests,
     MaterialChangesRebindTheTubeWithoutRetessellatingIt) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_straight_curve_asset());
  const AssetId first_material =
      assets.add_material(make_default_quader_material("First Pipe Material"));
  const AssetId second_material =
      assets.add_material(make_default_quader_material("Second Pipe Material"));
  ASSERT_NE(curve_asset, kInvalidAssetId);
  ASSERT_NE(first_material, kInvalidAssetId);
  ASSERT_NE(second_material, kInvalidAssetId);

  const EntityId curve =
      document.create_entity("Material Pipe", EntityKind::Curve);
  ASSERT_TRUE(document.set_curve(
      curve, {.curve = curve_asset,
              .solidify = true,
              .solidify_resolution = 12,
              .solidify_radius = 0.2f}));
  ASSERT_TRUE(document.set_material(
      curve, MaterialComponent{.material =
                                   MaterialReference(first_material)}));

  CurveSolidGeometryCache cache;
  RenderSceneSnapshot first = build_render_scene_snapshot(
      document, assets,
      {.frame_index = 1u, .curve_solid_geometry_cache = &cache});
  ASSERT_EQ(first.meshes.size(), 1u);
  const auto geometry = first.meshes.front().mesh.geometry_payload;
  ASSERT_NE(geometry, nullptr);
  EXPECT_TRUE(first.meshes.front().material.present);
  EXPECT_EQ(first.meshes.front().material_reference,
            MaterialReference(first_material));
  EXPECT_EQ(first.meshes.front().material_id, first_material);
  EXPECT_EQ(cache.stats().build_count, 1u);

  ASSERT_TRUE(document.set_material(
      curve, MaterialComponent{.material =
                                   MaterialReference(second_material)}));
  const RenderSceneSnapshot second = build_render_scene_snapshot(
      document, assets,
      {.previous = &first,
       .frame_index = 2u,
       .curve_solid_geometry_cache = &cache});
  ASSERT_EQ(second.meshes.size(), 1u);
  EXPECT_EQ(second.meshes.front().mesh.geometry_payload, geometry);
  EXPECT_TRUE(second.meshes.front().material.present);
  EXPECT_EQ(second.meshes.front().material_reference,
            MaterialReference(second_material));
  EXPECT_EQ(second.meshes.front().material_id, second_material);
  EXPECT_TRUE(second.meshes.front().material_dirty);
  EXPECT_EQ(cache.stats().build_count, 1u);
}

TEST(CurveSolidRenderExtractionTests,
     SettingAndCurveEditsRebuildButRejectOverlayOnlyPublication) {
  SceneDocument document;
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_straight_curve_asset());
  const EntityId curve =
      document.create_entity("Edited Solid Curve", EntityKind::Curve);
  CurveComponent component{.curve = curve_asset,
                           .solidify = true,
                           .solidify_resolution = 8,
                           .solidify_radius = 0.1f};
  ASSERT_TRUE(document.set_curve(curve, component));
  CurveSolidGeometryCache cache;
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets, {.frame_index = 1u,
                         .curve_solid_geometry_cache = &cache});
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const auto initial_geometry = snapshot.meshes.front().mesh.geometry_payload;

  component.solidify_radius = 0.2f;
  ASSERT_TRUE(document.set_curve(curve, component));
  snapshot = build_render_scene_snapshot(
      document, assets,
      {.previous = &snapshot,
       .frame_index = 2u,
       .curve_solid_geometry_cache = &cache});
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  EXPECT_NE(snapshot.meshes.front().mesh.geometry_payload, initial_geometry);
  EXPECT_EQ(cache.stats().build_count, 2u);

  CurveAsset replacement = *assets.curve(curve_asset);
  modeling::CurveDocument edited = *replacement.document;
  const modeling::CurveControlPoint endpoint = edited.points().back();
  ASSERT_TRUE(edited.set_point(
      endpoint.id,
      {.position = {5.0f, 0.0f, 0.0f},
       .incoming_handle = endpoint.incoming_handle,
       .outgoing_handle = endpoint.outgoing_handle}).ok());
  replacement.document = std::make_shared<const modeling::CurveDocument>(
      std::move(edited));
  ASSERT_TRUE(assets.replace_curve(curve_asset, std::move(replacement)));
  ASSERT_TRUE(document.notify_curve_payload_changed(curve));
  const std::uint64_t retained_revision = snapshot.scene_revision;
  EXPECT_FALSE(try_refresh_render_scene_snapshot_curve_publication(
      snapshot, document, assets, 3u));
  EXPECT_EQ(snapshot.scene_revision, retained_revision);

  snapshot = build_render_scene_snapshot(
      document, assets,
      {.previous = &snapshot,
       .frame_index = 3u,
       .curve_solid_geometry_cache = &cache});
  EXPECT_EQ(cache.stats().build_count, 3u);
  EXPECT_TRUE(snapshot.meshes.front().mesh_dirty);
}

TEST(CurveSolidRenderPreviewTests,
     DerivesOnlyEnabledValidCurvesIntoTransientMeshRecords) {
  AssetRegistry assets;
  const AssetId curve_asset = assets.add_curve(make_straight_curve_asset());
  ASSERT_NE(curve_asset, kInvalidAssetId);
  CurveSolidGeometryCache cache;

  ToolPreviewScene solid_preview;
  solid_preview.revision = 17U;
  solid_preview.curves.push_back({
      .source_entity = 41U,
      .curve = curve_asset,
      .instance_transforms = {QMat4{}, QMat4{}},
      .solidify = true,
      .solidify_resolution = 16,
      .solidify_radius = 0.5F,
      .suppress_source = true,
      .layout_key = 73U,
  });
  const RenderToolPreviewSceneSnapshot solid =
      build_render_tool_preview_scene_snapshot(
          std::move(solid_preview), assets, nullptr, nullptr, &cache);
  ASSERT_TRUE(solid.complete);
  EXPECT_EQ(solid.revision, 17U);
  ASSERT_EQ(solid.curves.size(), 1U);
  EXPECT_TRUE(solid.curves.front().solidify);
  EXPECT_EQ(solid.curves.front().solidify_resolution, 16);
  EXPECT_FLOAT_EQ(solid.curves.front().solidify_radius, 0.5F);
  ASSERT_EQ(solid.meshes.size(), 1U);
  EXPECT_EQ(solid.meshes.front().source_entity, 41U);
  EXPECT_EQ(solid.meshes.front().layout_key, 73U);
  EXPECT_EQ(solid.meshes.front().instance_transforms.size(), 2U);
  EXPECT_TRUE(solid.meshes.front().renderable());
  EXPECT_TRUE(solid.meshes.front().suppress_source);
  EXPECT_EQ(cache.stats().build_count, 1U);

  ToolPreviewScene ordinary_preview;
  ordinary_preview.curves.push_back({
      .source_entity = 42U,
      .curve = curve_asset,
      .instance_transforms = {QMat4{}},
      .solidify = false,
  });
  const RenderToolPreviewSceneSnapshot ordinary =
      build_render_tool_preview_scene_snapshot(
          std::move(ordinary_preview), assets, nullptr, nullptr, &cache);
  ASSERT_TRUE(ordinary.complete);
  ASSERT_EQ(ordinary.curves.size(), 1U);
  EXPECT_FALSE(ordinary.curves.front().solidify);
  EXPECT_TRUE(ordinary.meshes.empty());
  EXPECT_FALSE(build_render_tool_preview_overlay_frame(ordinary).lines.empty());
  EXPECT_EQ(cache.stats().build_count, 1U)
      << "ordinary Curve strokes never enter tube tessellation";

  const AssetId degenerate_curve =
      assets.add_curve(make_repeated_anchor_curve_asset());
  ASSERT_NE(degenerate_curve, kInvalidAssetId);
  ToolPreviewScene fallback_preview;
  fallback_preview.curves.push_back({
      .source_entity = 43U,
      .curve = degenerate_curve,
      .instance_transforms = {QMat4{}},
      .solidify = true,
      .solidify_resolution = 8,
      .solidify_radius = 0.25F,
      .suppress_source = true,
  });
  const RenderToolPreviewSceneSnapshot fallback =
      build_render_tool_preview_scene_snapshot(
          std::move(fallback_preview), assets, nullptr, nullptr, &cache);
  ASSERT_TRUE(fallback.complete);
  ASSERT_EQ(fallback.curves.size(), 1U);
  EXPECT_TRUE(fallback.curves.front().solidify);
  ASSERT_NE(fallback.curves.front().sample_payload, nullptr);
  EXPECT_TRUE(fallback.curves.front().sample_payload->error_message.empty());
  EXPECT_TRUE(fallback.meshes.empty())
      << "a failed tube derivation falls back to the authored centerline";
  EXPECT_EQ(fallback.suppression.entities, (std::vector<EntityId>{43U}));
  EXPECT_EQ(cache.stats().failure_count, 1U);

  ToolPreviewScene malformed_preview;
  malformed_preview.curves.push_back({
      .source_entity = 44U,
      .curve = curve_asset,
      .instance_transforms = {QMat4{}},
      .solidify = true,
      .solidify_resolution = kCurveSolidifyMinimumResolution - 1,
      .solidify_radius = 0.5F,
      .suppress_source = true,
  });
  const RenderToolPreviewSceneSnapshot malformed =
      build_render_tool_preview_scene_snapshot(
          std::move(malformed_preview), assets, nullptr, nullptr, &cache);
  EXPECT_FALSE(malformed.complete);
  EXPECT_TRUE(malformed.curves.empty());
  EXPECT_TRUE(malformed.meshes.empty());
  EXPECT_EQ(cache.stats().build_count, 2U)
      << "invalid settings fail before derived geometry work";
}

TEST(CurveSolidBoundsTests,
     ExpandsLocalAndHierarchyWorldBoundsUnderNonuniformScale) {
  SceneDocument document;
  AssetRegistry assets;
  CurveAsset asset = make_straight_curve_asset();
  ASSERT_NE(asset.document, nullptr);
  const AssetId curve_asset = assets.add_curve(asset);
  const EntityId parent =
      document.create_entity("Curve Group", EntityKind::Empty);
  const EntityId curve =
      document.create_entity("Bounded Solid Curve", EntityKind::Curve);
  const CurveComponent component{.curve = curve_asset,
                                 .solidify = true,
                                 .solidify_resolution = 8,
                                 .solidify_radius = 0.5f};
  ASSERT_TRUE(document.set_curve(curve, component));
  ASSERT_TRUE(document.set_parent(curve, parent, false));
  ASSERT_TRUE(document.set_transform(
      curve, {.scale = {2.0f, 3.0f, 4.0f}}));
  ASSERT_TRUE(document.set_transform(
      parent, {.location = {10.0f, 20.0f, 30.0f}}));

  const std::optional<SceneBounds> local =
      local_curve_bounds(asset, component);
  ASSERT_TRUE(local.has_value());
  expect_bounds_near(*local,
                     {.min = {-0.5f, -0.5f, -0.5f},
                      .max = {4.5f, 0.5f, 0.5f}});

  const std::optional<SceneBounds> world =
      entity_world_bounds(document, assets, curve);
  ASSERT_TRUE(world.has_value());
  const SceneBounds expected{.min = {9.0f, 18.5f, 28.0f},
                             .max = {19.0f, 21.5f, 32.0f}};
  expect_bounds_near(*world, expected);
  const std::optional<SceneBounds> hierarchy =
      entity_hierarchy_world_bounds(document, assets, parent);
  ASSERT_TRUE(hierarchy.has_value());
  expect_bounds_near(*hierarchy, expected);
}

} // namespace
} // namespace quader
