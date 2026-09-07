////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_mesh_occurrence_projection.hpp"

#include "assets/asset_registry.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/viewport_info_stats.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <array>
#include <gtest/gtest.h>

namespace quader {
namespace {

MeshAsset make_occurrence_bounds_mesh() {
  MeshAsset mesh;
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {-1.0f, -2.0f, -3.0f}},
      {.position = {2.0f, -2.0f, -3.0f}},
      {.position = {-1.0f, 4.0f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  return mesh;
}

void expect_bounds_equal(const SceneBounds &actual,
                         const SceneBounds &expected) {
  EXPECT_FLOAT_EQ(actual.min.x, expected.min.x);
  EXPECT_FLOAT_EQ(actual.min.y, expected.min.y);
  EXPECT_FLOAT_EQ(actual.min.z, expected.min.z);
  EXPECT_FLOAT_EQ(actual.max.x, expected.max.x);
  EXPECT_FLOAT_EQ(actual.max.y, expected.max.y);
  EXPECT_FLOAT_EQ(actual.max.z, expected.max.z);
}

TEST(SceneMeshOccurrenceProjectionTests,
     ProjectsOrdinaryAndFormalPlacementsWithStableFactsAndOrders) {
  SceneDocument scene;
  const EntityId direct =
      scene.create_entity("Direct Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(direct, {.mesh = 101u}));
  ASSERT_TRUE(scene.set_transform(
      direct, TransformComponent{.position = {1.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_selectable(direct, false));

  const EntityId definition =
      scene.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  ASSERT_TRUE(scene.set_locked(definition, true));

  const EntityId first_member =
      scene.create_entity("First Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(first_member, {.mesh = 102u}));
  ASSERT_TRUE(scene.set_parent(first_member, definition, false));
  ASSERT_TRUE(scene.set_transform(
      first_member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_locked(first_member, true));

  const EntityId second_member =
      scene.create_entity("Second Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(second_member, {.mesh = 103u}));
  ASSERT_TRUE(scene.set_parent(second_member, definition, false));
  ASSERT_TRUE(scene.set_transform(
      second_member, TransformComponent{.position = {4.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_selectable(second_member, false));

  const EntityId first_occurrence =
      scene.create_entity("First Occurrence", EntityKind::Instance);
  const EntityId second_occurrence =
      scene.create_entity("Second Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      first_occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_instance_occurrence(
      second_occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_transform(
      first_occurrence,
      TransformComponent{.position = {10.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_transform(
      second_occurrence,
      TransformComponent{.position = {20.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_selectable(second_occurrence, false));

  SceneMeshOccurrenceProjectionWorkStats work;
  const SceneMeshOccurrenceProjectionSnapshot projection =
      build_scene_mesh_occurrence_projection(scene, &work);

  ASSERT_EQ(projection.records().size(), 5u);
  EXPECT_EQ(projection.records()[0].display_entity, direct);
  EXPECT_EQ(projection.records()[1].display_entity, first_occurrence);
  EXPECT_EQ(projection.records()[1].geometry_entity, first_member);
  EXPECT_EQ(projection.records()[2].display_entity, first_occurrence);
  EXPECT_EQ(projection.records()[2].geometry_entity, second_member);
  EXPECT_EQ(projection.records()[3].display_entity, second_occurrence);
  EXPECT_EQ(projection.records()[4].display_entity, second_occurrence);

  const auto first_placements =
      projection.records_for_display(first_occurrence);
  ASSERT_EQ(first_placements.size(), 2u);
  EXPECT_FLOAT_EQ(first_placements[0].world_from_geometry.values[12], 12.0f);
  EXPECT_FLOAT_EQ(first_placements[1].world_from_geometry.values[12], 14.0f);
  EXPECT_TRUE(first_placements[0].effectively_locked);
  EXPECT_TRUE(first_placements[0].selectable);
  EXPECT_FALSE(first_placements[1].selectable);
  EXPECT_FALSE(first_placements[1].effectively_locked);
  EXPECT_FALSE(projection.records_for_display(second_occurrence)[0].selectable);
  EXPECT_FALSE(projection.records().front().selectable);

  ASSERT_EQ(projection.formal_geometry_groups().size(), 2u);
  EXPECT_EQ(projection.formal_geometry_groups()[0].geometry_entity,
            first_member);
  EXPECT_EQ(projection.formal_geometry_groups()[1].geometry_entity,
            second_member);
  ASSERT_EQ(
      projection.formal_geometry_groups()[0].participant_indices.size(), 2u);
  EXPECT_EQ(projection.record(
                projection.formal_geometry_groups()[0]
                    .participant_indices[0])
                ->display_entity,
            first_occurrence);
  EXPECT_EQ(projection.record(
                projection.formal_geometry_groups()[0]
                    .participant_indices[1])
                ->display_entity,
            second_occurrence);

  ASSERT_EQ(projection.formal_placement_indices().size(), 4u);
  EXPECT_EQ(projection.record(projection.formal_placement_indices()[0])
                ->identity(),
            (SceneMeshOccurrenceIdentity{first_occurrence, definition,
                                         first_member}));
  EXPECT_EQ(projection.record(projection.formal_placement_indices()[1])
                ->identity(),
            (SceneMeshOccurrenceIdentity{first_occurrence, definition,
                                         second_member}));
  EXPECT_EQ(projection.record(projection.formal_placement_indices()[2])
                ->identity(),
            (SceneMeshOccurrenceIdentity{second_occurrence, definition,
                                         first_member}));

  const SceneMeshOccurrenceProjectionRecord *found = projection.find({
      .display_entity = second_occurrence,
      .definition_entity = definition,
      .geometry_entity = second_member,
  });
  ASSERT_NE(found, nullptr);
  EXPECT_FLOAT_EQ(found->world_from_geometry.values[12], 24.0f);
  EXPECT_EQ(work.ordinary_placement_count, 1u);
  EXPECT_EQ(work.formal_geometry_group_count, 2u);
  EXPECT_EQ(work.formal_placement_count, 4u);
  EXPECT_EQ(work.world_transform_multiply_count, 4u);
  EXPECT_EQ(work.singular_definition_count, 0u);
  for (const SceneMeshOccurrenceProjectionRecord &record :
       projection.records()) {
    EXPECT_TRUE(record.effectively_visible);
  }
}

TEST(SceneMeshOccurrenceProjectionTests,
     ExcludesHiddenPlacementsAndSingularDefinitionsWithoutAssets) {
  SceneDocument scene;
  const EntityId definition =
      scene.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  const EntityId member =
      scene.create_entity("Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = 201u}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  const EntityId occurrence =
      scene.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, {.definition = definition}));

  ASSERT_EQ(build_scene_mesh_occurrence_projection(scene).records().size(),
            1u);
  ASSERT_TRUE(scene.set_visible(member, false));
  EXPECT_TRUE(build_scene_mesh_occurrence_projection(scene).records().empty());
  ASSERT_TRUE(scene.set_visible(member, true));
  ASSERT_TRUE(scene.set_visible(occurrence, false));
  EXPECT_TRUE(build_scene_mesh_occurrence_projection(scene).records().empty());
  ASSERT_TRUE(scene.set_visible(occurrence, true));

  TransformComponent singular;
  singular.scale = {0.0f, 1.0f, 1.0f};
  ASSERT_TRUE(scene.set_transform(definition, singular));
  SceneMeshOccurrenceProjectionWorkStats work;
  EXPECT_TRUE(
      build_scene_mesh_occurrence_projection(scene, &work).records().empty());
  EXPECT_EQ(work.singular_definition_count, 1u);
  EXPECT_TRUE(build_scene_mesh_occurrence_projection(
                  scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden)
                  .records()
                  .empty());
  EXPECT_EQ(work.singular_definition_count, 1u);
}

TEST(SceneMeshOccurrenceProjectionTests,
     SingularDiagnosticsRequireAnIncludedOccurrenceAndMeshMember) {
  SceneDocument scene;
  const EntityId definition = scene.create_entity("Singular Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  TransformComponent singular;
  singular.scale = {0.0f, 1.0f, 1.0f};
  ASSERT_TRUE(scene.set_transform(definition, singular));
  SceneMeshOccurrenceProjectionWorkStats work;
  const auto project_all = [&]() {
    return build_scene_mesh_occurrence_projection(
        scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden);
  };
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 0u);

  const EntityId member = scene.create_entity("Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = 205u}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 0u)
      << "unreferenced definition storage omits no actual placement";

  const EntityId occurrence =
      scene.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, {.definition = definition}));
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 1u);

  ASSERT_TRUE(scene.set_visible(occurrence, false));
  EXPECT_TRUE(build_scene_mesh_occurrence_projection(scene, &work)
                  .records()
                  .empty());
  EXPECT_EQ(work.singular_definition_count, 0u);
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 1u);
  ASSERT_TRUE(scene.set_visible(occurrence, true));
  ASSERT_TRUE(scene.set_visible(member, false));
  EXPECT_TRUE(build_scene_mesh_occurrence_projection(scene, &work)
                  .records()
                  .empty());
  EXPECT_EQ(work.singular_definition_count, 0u);
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 1u);

  ASSERT_TRUE(scene.clear_mesh(member));
  EXPECT_TRUE(project_all().records().empty());
  EXPECT_EQ(work.singular_definition_count, 0u)
      << "an occurrence of a definition without meshes omits no mesh placement";
}

TEST(SceneMeshOccurrenceProjectionTests,
     IncludeHiddenRetainsOrdinaryVisibilityFromObjectsAncestorsAndLayers) {
  SceneDocument scene;
  const EntityId visible = scene.create_entity("Visible", EntityKind::Mesh);
  const EntityId hidden = scene.create_entity("Hidden", EntityKind::Mesh);
  const EntityId hidden_parent = scene.create_entity("Hidden Parent");
  const EntityId hidden_by_parent =
      scene.create_entity("Hidden By Parent", EntityKind::Mesh);
  const EntityId hidden_by_layer =
      scene.create_entity("Hidden By Layer", EntityKind::Mesh);
  for (const EntityId mesh :
       {visible, hidden, hidden_by_parent, hidden_by_layer}) {
    ASSERT_TRUE(scene.set_mesh(mesh, {.mesh = 301u}));
  }
  ASSERT_TRUE(scene.set_visible(hidden, false));
  ASSERT_TRUE(scene.set_visible(hidden_parent, false));
  ASSERT_TRUE(scene.set_parent(hidden_by_parent, hidden_parent, false));
  ASSERT_TRUE(scene.set_transform(
      hidden_parent, TransformComponent{.position = {7.0f, 0.0f, 0.0f}}));
  const SceneLayerId hidden_layer = scene.create_layer("Hidden Layer");
  ASSERT_NE(hidden_layer, kInvalidSceneLayerId);
  ASSERT_TRUE(scene.set_entity_layer(hidden_by_layer, hidden_layer));
  ASSERT_TRUE(scene.set_layer_visible(hidden_layer, false));

  const auto visible_only = build_scene_mesh_occurrence_projection(scene);
  ASSERT_EQ(visible_only.records().size(), 1u);
  EXPECT_EQ(visible_only.records().front().display_entity, visible);

  SceneMeshOccurrenceProjectionWorkStats work;
  const auto all = build_scene_mesh_occurrence_projection(
      scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden);
  ASSERT_EQ(all.records().size(), 4u);
  ASSERT_EQ(all.ordinary_placement_indices().size(), 4u);
  EXPECT_EQ(work.ordinary_placement_count, 4u);
  EXPECT_TRUE(all.formal_geometry_groups().empty());
  for (const EntityId mesh :
       {visible, hidden, hidden_by_parent, hidden_by_layer}) {
    const auto placements = all.records_for_display(mesh);
    ASSERT_EQ(placements.size(), 1u);
    EXPECT_EQ(placements.front().effectively_visible, mesh == visible);
    EXPECT_FALSE(placements.front().formal_instance());
  }
  EXPECT_FLOAT_EQ(all.records_for_display(hidden_by_parent)
                      .front()
                      .world_from_geometry.values[12],
                  7.0f);
}

TEST(SceneMeshOccurrenceProjectionTests,
     IncludeHiddenCombinesOccurrenceAndMemberVisibilityWithoutStorageCopies) {
  SceneDocument scene;
  const EntityId definition = scene.create_entity("Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  const EntityId visible_member =
      scene.create_entity("Visible Member", EntityKind::Mesh);
  const EntityId hidden_member =
      scene.create_entity("Hidden Member", EntityKind::Mesh);
  const EntityId member_parent =
      scene.create_entity("Hidden Member Parent", EntityKind::Empty);
  const EntityId hidden_by_parent_member =
      scene.create_entity("Hidden By Parent Member", EntityKind::Mesh);
  for (const EntityId mesh :
       {visible_member, hidden_member, hidden_by_parent_member}) {
    ASSERT_TRUE(scene.set_mesh(mesh, {.mesh = 302u}));
  }
  ASSERT_TRUE(scene.set_parent(visible_member, definition, false));
  ASSERT_TRUE(scene.set_parent(hidden_member, definition, false));
  ASSERT_TRUE(scene.set_parent(member_parent, definition, false));
  ASSERT_TRUE(scene.set_parent(hidden_by_parent_member, member_parent, false));
  ASSERT_TRUE(scene.set_visible(hidden_member, false));
  ASSERT_TRUE(scene.set_visible(member_parent, false));
  for (const EntityId mesh :
       {visible_member, hidden_member, hidden_by_parent_member}) {
    ASSERT_TRUE(scene.set_transform(
        mesh, TransformComponent{.position = {2.0f, 0.0f, 0.0f}}));
  }

  const EntityId visible_occurrence =
      scene.create_entity("Visible Occurrence", EntityKind::Instance);
  const EntityId hidden_occurrence =
      scene.create_entity("Hidden Occurrence", EntityKind::Instance);
  const EntityId occurrence_parent = scene.create_entity("Hidden Parent");
  const EntityId hidden_by_parent_occurrence =
      scene.create_entity("Hidden By Parent Occurrence", EntityKind::Instance);
  for (const EntityId occurrence :
       {visible_occurrence, hidden_occurrence, hidden_by_parent_occurrence}) {
    ASSERT_TRUE(scene.set_instance_occurrence(
        occurrence, {.definition = definition}));
    ASSERT_TRUE(scene.set_transform(
        occurrence, TransformComponent{.position = {10.0f, 0.0f, 0.0f}}));
  }
  ASSERT_TRUE(scene.set_visible(hidden_occurrence, false));
  ASSERT_TRUE(scene.set_parent(hidden_by_parent_occurrence, occurrence_parent,
                               false));
  ASSERT_TRUE(scene.set_visible(occurrence_parent, false));

  const auto visible_only = build_scene_mesh_occurrence_projection(scene);
  ASSERT_EQ(visible_only.records().size(), 1u);
  EXPECT_EQ(visible_only.records().front().identity(),
            (SceneMeshOccurrenceIdentity{visible_occurrence, definition,
                                         visible_member}));
  EXPECT_TRUE(visible_only.records().front().effectively_visible);

  SceneMeshOccurrenceProjectionWorkStats work;
  const auto all = build_scene_mesh_occurrence_projection(
      scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden);
  ASSERT_EQ(all.records().size(), 9u);
  EXPECT_TRUE(all.ordinary_placement_indices().empty());
  ASSERT_EQ(all.formal_geometry_groups().size(), 3u);
  ASSERT_EQ(all.formal_placement_indices().size(), 9u);
  EXPECT_EQ(work.formal_placement_count, 9u);
  EXPECT_EQ(work.formal_geometry_group_count, 3u);
  for (const EntityId occurrence :
       {visible_occurrence, hidden_occurrence, hidden_by_parent_occurrence}) {
    const auto placements = all.records_for_display(occurrence);
    ASSERT_EQ(placements.size(), 3u);
    EXPECT_EQ(placements[0].geometry_entity, visible_member);
    EXPECT_EQ(placements[1].geometry_entity, hidden_member);
    EXPECT_EQ(placements[2].geometry_entity, hidden_by_parent_member);
    for (const SceneMeshOccurrenceProjectionRecord &record : placements) {
      EXPECT_EQ(record.effectively_visible,
                occurrence == visible_occurrence &&
                    record.geometry_entity == visible_member);
      EXPECT_EQ(record.definition_entity, definition);
      EXPECT_FLOAT_EQ(record.world_from_geometry.values[12], 12.0f);
    }
  }
  for (const SceneMeshOccurrenceGeometryGroup &group :
       all.formal_geometry_groups()) {
    ASSERT_EQ(group.participant_indices.size(), 3u);
    EXPECT_EQ(all.record(group.participant_indices[0])->display_entity,
              visible_occurrence);
    EXPECT_EQ(all.record(group.participant_indices[1])->display_entity,
              hidden_occurrence);
    EXPECT_EQ(all.record(group.participant_indices[2])->display_entity,
              hidden_by_parent_occurrence);
  }
  EXPECT_TRUE(all.records_for_display(definition).empty());
  EXPECT_TRUE(all.records_for_display(visible_member).empty());
  EXPECT_TRUE(all.records_for_display(hidden_member).empty());
  EXPECT_TRUE(all.records_for_display(hidden_by_parent_member).empty());
}

TEST(SceneMeshOccurrenceProjectionTests,
     IncludeHiddenDoesNotReplaceVisibleOnlyCacheOrMutateSceneRevisions) {
  SceneDocument scene;
  const EntityId visible = scene.create_entity("Visible", EntityKind::Mesh);
  const EntityId hidden = scene.create_entity("Hidden", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(visible, {.mesh = 303u}));
  ASSERT_TRUE(scene.set_mesh(hidden, {.mesh = 304u}));
  ASSERT_TRUE(scene.set_visible(hidden, false));
  const auto cached_before = scene.mesh_occurrence_projection();
  ASSERT_EQ(cached_before->records().size(), 1u);
  const auto stats_before = scene.spatial_projection_cache_stats();
  const std::array<std::uint64_t, 4> revisions_before{
      scene.hierarchy_presentation_revision(), scene.visibility_revision(),
      scene.transform_revision(), scene.mesh_binding_revision()};

  const auto all = build_scene_mesh_occurrence_projection(
      scene, nullptr, SceneMeshOccurrenceVisibility::IncludeHidden);
  EXPECT_EQ(all.records().size(), 2u);
  const auto cached_after = scene.mesh_occurrence_projection();
  EXPECT_EQ(cached_before.get(), cached_after.get());
  EXPECT_EQ(cached_after->records().size(), 1u);
  const auto stats_after = scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats_before.hierarchy_build_count,
            stats_after.hierarchy_build_count);
  EXPECT_EQ(stats_before.mesh_occurrence_build_count,
            stats_after.mesh_occurrence_build_count);
  const std::array<std::uint64_t, 4> revisions_after{
      scene.hierarchy_presentation_revision(), scene.visibility_revision(),
      scene.transform_revision(), scene.mesh_binding_revision()};
  EXPECT_EQ(revisions_before, revisions_after);
}

TEST(SceneMeshOccurrenceProjectionTests,
     FormalOccurrenceBoundsReuseProjectedTransformsAndVisibility) {
  SceneDocument scene;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_occurrence_bounds_mesh());
  ASSERT_NE(mesh, kInvalidAssetId);

  const EntityId definition =
      scene.create_entity("Bounds Definition", EntityKind::Empty);
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  ASSERT_TRUE(scene.set_transform(
      definition,
      TransformComponent{.position = {100.0f, -50.0f, 7.0f}}));

  const EntityId member =
      scene.create_entity("Visible Bounds Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(scene.set_transform(
      member, TransformComponent{.position = {2.0f, 3.0f, 4.0f}}));

  const EntityId hidden_member =
      scene.create_entity("Hidden Bounds Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(hidden_member, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(hidden_member, definition, false));
  ASSERT_TRUE(scene.set_transform(
      hidden_member,
      TransformComponent{.position = {1000.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(scene.set_visible(hidden_member, false));

  const EntityId group = scene.create_entity("Occurrence Parent");
  ASSERT_TRUE(scene.set_transform(
      group, TransformComponent{.position = {5.0f, 0.0f, 0.0f}}));
  const EntityId occurrence =
      scene.create_entity("Bounds Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_parent(occurrence, group, false));
  ASSERT_TRUE(scene.set_transform(
      occurrence,
      TransformComponent{.position = {10.0f, 20.0f, 30.0f}}));

  const SceneBounds expected{
      .min = {16.0f, 21.0f, 31.0f},
      .max = {19.0f, 27.0f, 35.0f},
  };
  const std::optional<SceneBounds> occurrence_bounds =
      entity_world_bounds(scene, assets, occurrence);
  ASSERT_TRUE(occurrence_bounds.has_value());
  expect_bounds_equal(*occurrence_bounds, expected);

  const std::optional<SceneBounds> hierarchy_bounds =
      entity_hierarchy_world_bounds(scene, assets, group);
  ASSERT_TRUE(hierarchy_bounds.has_value());
  expect_bounds_equal(*hierarchy_bounds, expected);

  ASSERT_TRUE(scene.set_selected(occurrence, true));
  const std::optional<SceneBounds> selection_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(selection_bounds.has_value());
  expect_bounds_equal(*selection_bounds, expected);

  const std::optional<SceneBounds> filtered_document_bounds =
      document_world_bounds(scene, assets, [occurrence](EntityId entity) {
        return entity == occurrence;
      });
  ASSERT_TRUE(filtered_document_bounds.has_value());
  expect_bounds_equal(*filtered_document_bounds, expected);

  ASSERT_TRUE(scene.set_visible(member, false));
  const std::optional<SceneBounds> empty_occurrence_bounds =
      entity_world_bounds(scene, assets, occurrence);
  ASSERT_TRUE(empty_occurrence_bounds.has_value());
  expect_bounds_equal(
      *empty_occurrence_bounds,
      SceneBounds{.min = {15.0f, 20.0f, 30.0f},
                  .max = {15.0f, 20.0f, 30.0f}});

  ASSERT_TRUE(scene.set_visible(member, true));
  ASSERT_TRUE(scene.set_visible(occurrence, false));
  EXPECT_FALSE(entity_world_bounds(scene, assets, occurrence).has_value());
  EXPECT_FALSE(selected_world_bounds(scene, assets).has_value());
}

TEST(SceneMeshOccurrenceProjectionTests,
     UnchangedMultiConsumerReadsShareOneSceneOwnedPublication) {
  SceneDocument scene;
  AssetRegistry assets;
  const AssetId mesh = assets.add_mesh(make_occurrence_bounds_mesh());
  ASSERT_NE(mesh, kInvalidAssetId);

  const EntityId definition =
      scene.create_entity("Shared Definition", EntityKind::Empty);
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  const EntityId member =
      scene.create_entity("Shared Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = mesh}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(scene.set_transform(
      member, TransformComponent{.position = {2.0f, 0.0f, 0.0f}}));
  const EntityId occurrence =
      scene.create_entity("Shared Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_transform(
      occurrence,
      TransformComponent{.position = {10.0f, 0.0f, 0.0f}}));

  EXPECT_EQ(scene.spatial_projection_cache_stats().hierarchy_build_count, 0u);
  EXPECT_EQ(
      scene.spatial_projection_cache_stats().mesh_occurrence_build_count,
      0u);
  const RenderSceneSnapshot render = build_render_scene_snapshot(scene, assets);
  ASSERT_EQ(render.instance_primitives.size(), 1u);
  SceneSpatialProjectionCacheStats stats =
      scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats.hierarchy_build_count, 1u);
  EXPECT_EQ(stats.mesh_occurrence_build_count, 1u);

  const ViewportInfoSceneSnapshot viewport_info =
      build_viewport_info_scene_snapshot(scene, assets);
  EXPECT_EQ(viewport_info.renderables.size(), 1u);
  const RenderOverlaySceneWireframeSnapshot wireframe =
      build_render_overlay_scene_wireframe_snapshot(scene, assets, 1u);
  EXPECT_EQ(wireframe.meshes.size(), 1u);
  EXPECT_TRUE(entity_world_bounds(scene, assets, occurrence).has_value());
  EXPECT_FALSE(scene.entity_records_tree_order().empty());
  stats = scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats.hierarchy_build_count, 1u);
  EXPECT_EQ(stats.mesh_occurrence_build_count, 1u);
  EXPECT_GT(stats.hierarchy_cache_hit_count, 0u);
  EXPECT_GT(stats.mesh_occurrence_cache_hit_count, 0u);

  ASSERT_TRUE(scene.set_selected(occurrence, true));
  (void)build_render_scene_snapshot(scene, assets);
  (void)build_viewport_info_scene_snapshot(scene, assets);
  (void)build_render_overlay_scene_wireframe_snapshot(scene, assets, 1u);
  (void)selected_world_bounds(scene, assets);
  stats = scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats.hierarchy_build_count, 1u);
  EXPECT_EQ(stats.mesh_occurrence_build_count, 1u);

  ASSERT_TRUE(scene.set_selectable(member, false));
  (void)scene.mesh_occurrence_projection();
  stats = scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats.hierarchy_build_count, 1u)
      << "selectability is not a hierarchy projection fact";
  EXPECT_EQ(stats.mesh_occurrence_build_count, 2u);
}

TEST(SceneMeshOccurrenceProjectionTests,
     RevisionRewindCannotReuseDivergentSpatialPublications) {
  SceneDocument scene;
  const SceneModelingProjectionCheckpoint checkpoint =
      scene.capture_modeling_projection_checkpoint(
          std::span<const EntityId>{},
          SceneModelingProjectionDomain::MeshBindings |
              SceneModelingProjectionDomain::Structure);

  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(first, {.mesh = 501u}));
  ASSERT_TRUE(scene.set_transform(
      first, TransformComponent{.position = {1.0f, 0.0f, 0.0f}}));
  const auto first_hierarchy = scene.hierarchy_projection();
  const auto first_occurrences = scene.mesh_occurrence_projection();
  const std::array<std::uint64_t, 4> first_revisions{
      scene.hierarchy_presentation_revision(), scene.visibility_revision(),
      scene.transform_revision(), scene.mesh_binding_revision()};
  ASSERT_NE(first_hierarchy->find(first), nullptr);
  ASSERT_NE(first_occurrences->find({.display_entity = first,
                                     .geometry_entity = first}),
            nullptr);
  EXPECT_FLOAT_EQ(first_hierarchy->find(first)->world_transform.values[12],
                  1.0f);

  ASSERT_TRUE(scene.restore_modeling_projection_checkpoint(checkpoint,
                                                           {&first, 1u}));
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  ASSERT_EQ(second, first);
  ASSERT_TRUE(scene.set_mesh(second, {.mesh = 777u}));
  ASSERT_TRUE(scene.set_transform(
      second, TransformComponent{.position = {9.0f, 0.0f, 0.0f}}));
  const std::array<std::uint64_t, 4> second_revisions{
      scene.hierarchy_presentation_revision(), scene.visibility_revision(),
      scene.transform_revision(), scene.mesh_binding_revision()};
  EXPECT_EQ(second_revisions, first_revisions);

  const auto second_hierarchy = scene.hierarchy_projection();
  const auto second_occurrences = scene.mesh_occurrence_projection();
  EXPECT_NE(second_hierarchy.get(), first_hierarchy.get());
  EXPECT_NE(second_occurrences.get(), first_occurrences.get());
  ASSERT_NE(second_hierarchy->find(second), nullptr);
  const SceneMeshOccurrenceProjectionRecord *second_placement =
      second_occurrences->find({.display_entity = second,
                                .geometry_entity = second});
  ASSERT_NE(second_placement, nullptr);
  EXPECT_FLOAT_EQ(second_hierarchy->find(second)->world_transform.values[12],
                  9.0f);
  EXPECT_FLOAT_EQ(second_placement->world_from_geometry.values[12], 9.0f);
  EXPECT_FLOAT_EQ(first_hierarchy->find(first)->world_transform.values[12],
                  1.0f)
      << "externally held immutable publications retain their old facts";
  const SceneSpatialProjectionCacheStats stats =
      scene.spatial_projection_cache_stats();
  EXPECT_EQ(stats.hierarchy_build_count, 2u);
  EXPECT_EQ(stats.mesh_occurrence_build_count, 2u);
}

} // namespace
} // namespace quader
