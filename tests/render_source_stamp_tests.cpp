////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_source_stamp.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>

using namespace quader;

static_assert(std::is_trivially_copyable_v<RenderSourceStamp>);
static_assert(std::is_trivially_copyable_v<RenderSourceDependencyMask>);

TEST(RenderSourceStampTests, DependencyMasksCompareOnlyDeclaredFacts) {
  const RenderSourceStamp before{
      .scene_binding = {11u, 12u, 13u},
      .scene_revision = 21u,
      .selection_revision = 22u,
      .asset_revision = 23u,
  };
  RenderSourceStamp after = before;
  after.selection_revision = 25u;
  after.asset_revision = 26u;

  const RenderSourceDependencyMask selection_dependencies =
      RenderSourceDependencyMask::from({
          RenderSourceDependency::SceneBinding,
          RenderSourceDependency::SelectionRevision,
      });
  const RenderSourceDependencyMask changed =
      render_source_changed_dependencies(before, after,
                                         selection_dependencies);
  EXPECT_TRUE(changed.contains(RenderSourceDependency::SelectionRevision));
  EXPECT_FALSE(changed.contains(RenderSourceDependency::AssetRevision));
  EXPECT_FALSE(render_source_dependencies_match(before, after,
                                                selection_dependencies));
  EXPECT_TRUE(render_source_dependencies_match(
      before, after, RenderSourceDependency::SceneBinding));
}

TEST(RenderSourceStampTests,
     AdvanceTogetherRequiresEqualPositiveScalarRevisionDeltas) {
  const RenderSourceDependencyMask publication_dependencies =
      RenderSourceDependencyMask::from({
          RenderSourceDependency::SceneRevision,
          RenderSourceDependency::CurveBindingRevision,
          RenderSourceDependency::RenderComponentRevision,
      });
  const RenderSourceStamp before{
      .scene_revision = 5u,
      .curve_binding_revision = 8u,
      .render_component_revision = 13u,
  };
  RenderSourceStamp after = before;
  after.scene_revision += 2u;
  after.curve_binding_revision += 2u;
  after.render_component_revision += 2u;
  EXPECT_TRUE(render_source_dependencies_advanced_together(
      before, after, publication_dependencies));

  ++after.render_component_revision;
  EXPECT_FALSE(render_source_dependencies_advanced_together(
      before, after, publication_dependencies));
  EXPECT_FALSE(render_source_dependencies_advanced_together(
      before, before, publication_dependencies));
  EXPECT_FALSE(render_source_dependencies_advanced_together(
      before, after, RenderSourceDependency::SceneBinding));
}

TEST(RenderSourceStampTests, CaptureIncludesOwnerAndExplicitContextStamps) {
  SceneDocument document;
  AssetRegistry assets;
  const EntityId entity = document.create_entity("Stamp Source");
  ASSERT_NE(entity, kInvalidEntityId);

  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 1> members{entity};
  ASSERT_TRUE(workspace.enter_local_visibility(members, members));
  const ViewportLocalVisibilitySnapshot local =
      workspace.local_visibility_snapshot();
  const RenderSceneBindingKey scene_binding{31u, 32u, 33u};
  const RenderInstanceEditScope edit_scope{entity, entity};

  const RenderSourceStamp stamp = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = scene_binding,
          .local_visibility = &local,
          .project_material_revision = 34u,
          .instance_edit_scope = edit_scope,
          .backface_culling_enabled = false,
      });

  EXPECT_EQ(stamp.scene_binding, scene_binding);
  EXPECT_EQ(stamp.scene_revision, document.revision());
  EXPECT_EQ(stamp.selection_revision, document.selection_revision());
  EXPECT_EQ(stamp.hierarchy_presentation_revision,
            document.hierarchy_presentation_revision());
  EXPECT_EQ(stamp.visibility_revision, document.visibility_revision());
  EXPECT_EQ(stamp.local_visibility_revision, local.revision());
  EXPECT_EQ(stamp.transform_revision, document.transform_revision());
  EXPECT_EQ(stamp.texture_mapping_revision,
            document.texture_mapping_revision());
  EXPECT_EQ(stamp.mesh_binding_revision, document.mesh_binding_revision());
  EXPECT_EQ(stamp.curve_binding_revision, document.curve_binding_revision());
  EXPECT_EQ(stamp.material_binding_revision,
            document.material_binding_revision());
  EXPECT_EQ(stamp.render_component_revision,
            document.render_component_revision());
  EXPECT_EQ(stamp.layer_revision, document.layer_revision());
  EXPECT_EQ(stamp.asset_revision, assets.revision());
  EXPECT_EQ(stamp.mesh_material_usage_revision,
            assets.mesh_material_usage_revision());
  EXPECT_EQ(stamp.material_catalog_revision,
            assets.material_catalog_revision());
  EXPECT_EQ(stamp.project_material_revision, 34u);
  EXPECT_EQ(stamp.instance_edit_scope, edit_scope);
  EXPECT_FALSE(stamp.backface_culling_enabled);
}

TEST(RenderSourceStampTests,
     CameraOnlySceneChangeReusesSceneAndOverlayExtractionStorage) {
  SceneDocument document;
  AssetRegistry assets;
  SceneSettings settings;
  settings.camera.near_plane = 0.1f;
  settings.camera.far_plane = 100.0f;
  ASSERT_TRUE(document.set_scene_settings(settings));

  const RenderSceneBindingKey scene_binding{41u, 42u, 43u};
  const RenderSceneSnapshot scene_snapshot = build_render_scene_snapshot(
      document, assets, {.scene_binding = scene_binding});
  RenderOverlaySnapshotCache overlay_cache;
  const RenderOverlayPersistentSnapshotUpdate initial_persistent =
      overlay_cache.update_persistent(document, assets, true, 1u, {},
                                      scene_binding);
  ASSERT_NE(initial_persistent.selected_object, nullptr);
  ASSERT_NE(initial_persistent.scene_wireframe, nullptr);
  (void)overlay_cache.selection_snapshot(document, assets, scene_binding);
  const RenderOverlaySnapshotCacheStats initial_stats = overlay_cache.stats();

  settings.camera.near_plane = 0.25f;
  settings.camera.far_plane = 250.0f;
  ASSERT_TRUE(document.set_scene_settings(settings));
  ASSERT_GT(document.revision(), scene_snapshot.scene_revision);

  EXPECT_TRUE(render_scene_snapshot_matches_sources(
      scene_snapshot, document, assets, scene_binding));
  const RenderOverlayPersistentSnapshotUpdate camera_update =
      overlay_cache.update_persistent(document, assets, true, 1u, {},
                                      scene_binding);
  EXPECT_FALSE(camera_update.selected_object_changed);
  EXPECT_TRUE(camera_update.scene_wireframe_changed);
  ASSERT_NE(camera_update.scene_wireframe, nullptr);
  EXPECT_FLOAT_EQ(camera_update.scene_wireframe->clip_planes.near_plane,
                  0.25f);
  EXPECT_FLOAT_EQ(camera_update.scene_wireframe->clip_planes.far_plane,
                  250.0f);
  (void)overlay_cache.selection_snapshot(document, assets, scene_binding);

  const RenderOverlaySnapshotCacheStats camera_stats = overlay_cache.stats();
  EXPECT_EQ(camera_stats.selected_object_build_count,
            initial_stats.selected_object_build_count);
  EXPECT_EQ(camera_stats.scene_wireframe_build_count,
            initial_stats.scene_wireframe_build_count);
  EXPECT_EQ(camera_stats.selection_build_count,
            initial_stats.selection_build_count);
  EXPECT_EQ(camera_stats.selection_curve_refresh_count,
            initial_stats.selection_curve_refresh_count);
  EXPECT_EQ(camera_stats.selection_transform_refresh_count,
            initial_stats.selection_transform_refresh_count);
}


TEST(RenderSourceStampTests, GlobalCullingInvalidatesOnlySurfaceConsumers) {
  const RenderSourceStamp before;
  RenderSourceStamp after = before;
  after.backface_culling_enabled = false;
  EXPECT_FALSE(render_source_dependencies_match(
      before, after, kRenderSceneSnapshotSourceDependencies));
  EXPECT_FALSE(render_source_dependencies_match(
      before, after, kRenderSceneSyncDocumentDependencies));
  EXPECT_FALSE(render_source_dependencies_match(
      before, after, kRenderSceneSyncContextDependencies));
  EXPECT_TRUE(render_source_dependencies_match(
      before, after, kRenderOverlaySceneWireframeSourceDependencies));
  EXPECT_TRUE(render_source_dependencies_match(
      before, after, kRenderOverlaySelectedObjectSourceDependencies));
  EXPECT_FALSE(render_source_dependencies_advanced_together(
      before, after, RenderSourceDependency::BackfaceCulling));
}
