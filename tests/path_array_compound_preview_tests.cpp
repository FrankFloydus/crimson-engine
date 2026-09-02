////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/modeling/modeling_tool_preview_scene_adapter.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "render/curve_solid_geometry_cache.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "scene/scene_document.hpp"
#include "support/curve_modeling_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using test::curve::create_box;
using test::curve::create_curve;

[[nodiscard]] QMat4 translated(float x, float y, float z) {
  QMat4 result;
  result.values[12] = x;
  result.values[13] = y;
  result.values[14] = z;
  return result;
}

[[nodiscard]] std::vector<EntityId>
sorted_entities(std::span<const EntityId> entities) {
  std::vector<EntityId> sorted(entities.begin(), entities.end());
  std::ranges::sort(sorted);
  return sorted;
}

void expect_vec3_equal(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

void expect_overlay_frames_equal(const ViewportOverlayFrame &actual,
                                 const ViewportOverlayFrame &expected) {
  ASSERT_EQ(actual.lines.size(), expected.lines.size());
  for (std::size_t index = 0U; index < actual.lines.size(); ++index) {
    const ViewportOverlayLine &left = actual.lines[index];
    const ViewportOverlayLine &right = expected.lines[index];
    EXPECT_EQ(left.layer, right.layer);
    expect_vec3_equal(left.start, right.start);
    expect_vec3_equal(left.end, right.end);
    EXPECT_EQ(left.color, right.color);
    EXPECT_FLOAT_EQ(left.width_pixels, right.width_pixels);
    EXPECT_EQ(left.style_role, right.style_role);
    EXPECT_EQ(left.source, right.source);
  }
  ASSERT_EQ(actual.points.size(), expected.points.size());
  for (std::size_t index = 0U; index < actual.points.size(); ++index) {
    const ViewportOverlayPoint &left = actual.points[index];
    const ViewportOverlayPoint &right = expected.points[index];
    EXPECT_EQ(left.layer, right.layer);
    expect_vec3_equal(left.position, right.position);
    EXPECT_EQ(left.color, right.color);
    EXPECT_FLOAT_EQ(left.size_pixels, right.size_pixels);
    EXPECT_EQ(left.style_role, right.style_role);
    EXPECT_EQ(left.source, right.source);
  }
  ASSERT_EQ(actual.icons.size(), expected.icons.size());
  for (std::size_t index = 0U; index < actual.icons.size(); ++index) {
    const ViewportOverlayIcon &left = actual.icons[index];
    const ViewportOverlayIcon &right = expected.icons[index];
    EXPECT_EQ(left.layer, right.layer);
    EXPECT_EQ(left.kind, right.kind);
    expect_vec3_equal(left.position, right.position);
    EXPECT_EQ(left.color, right.color);
    EXPECT_FLOAT_EQ(left.size_pixels, right.size_pixels);
    EXPECT_EQ(left.source, right.source);
  }
  EXPECT_EQ(actual.triangles.size(), expected.triangles.size());
}

struct CompoundPreviewHarness final {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  SceneRenderBridge bridge;
};

struct MixedHierarchy final {
  QuaderModelingObjectRef path;
  EntityId root = kInvalidEntityId;
  EntityId mesh = kInvalidEntityId;
  EntityId curve = kInvalidEntityId;
  EntityId point = kInvalidEntityId;
  EntityId directional = kInvalidEntityId;
  EntityId spot = kInvalidEntityId;
  EntityId generic = kInvalidEntityId;
  EntityId hidden_mesh = kInvalidEntityId;
  EntityId hidden_curve = kInvalidEntityId;
  EntityId hidden_light = kInvalidEntityId;
  LightComponent point_component;
  LightComponent directional_component;
  LightComponent spot_component;
};

[[nodiscard]] MixedHierarchy
create_mixed_hierarchy(CompoundPreviewHarness &harness) {
  MixedHierarchy result;
  result.path = create_curve(harness.editor);
  const QuaderModelingObjectRef mesh = create_box(harness.editor);
  const QuaderModelingObjectRef curve = create_curve(harness.editor);
  const QuaderModelingObjectRef hidden_mesh = create_box(harness.editor);
  const QuaderModelingObjectRef hidden_curve = create_curve(harness.editor);

  SceneDocument &scene = harness.session.scene();
  result.root = scene.create_empty("Path Array Mixed Root");
  result.mesh = mesh.entity;
  result.curve = curve.entity;
  result.hidden_mesh = hidden_mesh.entity;
  result.hidden_curve = hidden_curve.entity;
  result.point = scene.create_entity("Preview Point", EntityKind::Light);
  result.directional =
      scene.create_entity("Preview Directional", EntityKind::Light);
  result.spot = scene.create_entity("Preview Spot", EntityKind::Light);
  result.generic = scene.create_entity("Preview Metadata", EntityKind::Generic);
  result.hidden_light =
      scene.create_entity("Hidden Preview Light", EntityKind::Light);

  result.point_component = {
      .type = LightType::Point,
      .color = {0.25F, 0.5F, 0.75F},
      .intensity = 7.0F,
      .range = 13.0F,
      .casts_shadows = true,
  };
  result.directional_component = {
      .type = LightType::Directional,
      .color = {0.9F, 0.8F, 0.7F},
      .intensity = 3.5F,
      .range = 41.0F,
      .casts_shadows = false,
  };
  result.spot_component = {
      .type = LightType::Spot,
      .color = {0.6F, 0.3F, 0.1F},
      .intensity = 11.0F,
      .range = 23.0F,
      .casts_shadows = true,
  };
  EXPECT_TRUE(scene.set_light(result.point, result.point_component));
  EXPECT_TRUE(
      scene.set_light(result.directional, result.directional_component));
  EXPECT_TRUE(scene.set_light(result.spot, result.spot_component));
  EXPECT_TRUE(scene.set_light(
      result.hidden_light,
      LightComponent{.type = LightType::Point,
                     .intensity = 99.0F,
                     .range = 99.0F,
                     .casts_shadows = false}));

  const std::array<EntityId, 9U> children{
      result.mesh,         result.curve,        result.point,
      result.directional,  result.spot,         result.generic,
      result.hidden_mesh,  result.hidden_curve, result.hidden_light,
  };
  for (const EntityId child : children) {
    EXPECT_TRUE(scene.set_parent(child, result.root, false));
  }
  EXPECT_TRUE(scene.set_transform(
      result.mesh, TransformComponent{.position = {1.0F, 0.0F, 0.0F}}));
  EXPECT_TRUE(scene.set_transform(
      result.curve, TransformComponent{.position = {0.0F, 1.0F, 0.0F}}));
  EXPECT_TRUE(scene.set_transform(
      result.point, TransformComponent{.position = {0.0F, 0.0F, 1.0F}}));
  EXPECT_TRUE(scene.set_transform(
      result.directional,
      TransformComponent{.position = {2.0F, 0.0F, 0.0F},
                         .rotation_euler = {0.1F, 0.2F, 0.3F}}));
  EXPECT_TRUE(scene.set_transform(
      result.spot,
      TransformComponent{.position = {0.0F, 2.0F, 0.0F},
                         .rotation_euler = {-0.2F, 0.4F, 0.1F}}));
  EXPECT_TRUE(scene.set_visible(result.hidden_mesh, false));
  EXPECT_TRUE(scene.set_visible(result.hidden_curve, false));
  EXPECT_TRUE(scene.set_visible(result.hidden_light, false));

  EXPECT_TRUE(harness.editor.selection().select_object(result.path).ok());
  EXPECT_TRUE(harness.session.path_array().activate().ok);
  EXPECT_TRUE(harness.session.path_array().select_source(result.root).ok);
  return result;
}

[[nodiscard]] EntityId create_light_group(SceneDocument &scene,
                                          std::string name,
                                          std::size_t light_count,
                                          bool casts_shadows,
                                          bool visible = true) {
  const EntityId root = scene.create_empty(std::move(name));
  for (std::size_t index = 0U; index < light_count; ++index) {
    const EntityId light = scene.create_entity(
        "Budget Light " + std::to_string(index + 1U), EntityKind::Light);
    EXPECT_TRUE(scene.set_light(
        light, LightComponent{.type = LightType::Point,
                              .intensity = 1.0F,
                              .range = 10.0F,
                              .casts_shadows = casts_shadows}));
    EXPECT_TRUE(scene.set_parent(light, root, false));
    if (!visible) {
      EXPECT_TRUE(scene.set_visible(light, false));
    }
  }
  return root;
}

struct MarkerBudgetHierarchy final {
  EntityId root = kInvalidEntityId;
  EntityId nested_empty = kInvalidEntityId;
  EntityId nested_generic = kInvalidEntityId;
  EntityId occurrence = kInvalidEntityId;
};

[[nodiscard]] MarkerBudgetHierarchy create_marker_budget_hierarchy(
    SceneDocument &scene, AssetId mesh, std::size_t mesh_participant_count,
    std::string_view name) {
  const std::string prefix{name};
  const EntityId definition = scene.create_empty(prefix + " Definition");
  EXPECT_TRUE(scene.set_instance_definition(definition));
  for (std::size_t index = 0U; index < mesh_participant_count; ++index) {
    const EntityId member = scene.create_entity(
        prefix + " Definition Mesh " + std::to_string(index),
        EntityKind::Mesh);
    EXPECT_TRUE(scene.set_mesh(member, MeshComponent{.mesh = mesh}));
    EXPECT_TRUE(scene.set_parent(member, definition, false));
  }

  MarkerBudgetHierarchy result;
  result.root = scene.create_empty(prefix + " Root");
  result.nested_empty = scene.create_empty(prefix + " Nested Empty");
  result.nested_generic =
      scene.create_entity(prefix + " Nested Generic", EntityKind::Generic);
  result.occurrence =
      scene.create_entity(prefix + " Occurrence", EntityKind::Instance);
  EXPECT_TRUE(scene.set_instance_occurrence(
      result.occurrence,
      InstanceOccurrenceComponent{.definition = definition}));
  EXPECT_TRUE(scene.set_parent(result.nested_empty, result.root, false));
  EXPECT_TRUE(
      scene.set_parent(result.nested_generic, result.nested_empty, false));
  EXPECT_TRUE(
      scene.set_parent(result.occurrence, result.nested_generic, false));
  return result;
}

TEST(PathArrayCompoundPreviewTests,
     MixedHierarchyProjectsCompleteCameraNeutralPreviewAndSuppression) {
  CompoundPreviewHarness harness;
  const MixedHierarchy hierarchy = create_mixed_hierarchy(harness);
  const PathArrayPreviewState &preview =
      harness.session.path_array().preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.root_world_transforms.size(), kPathArrayDefaultCount);
  ASSERT_EQ(preview.scene.meshes.size(), 1U);
  ASSERT_EQ(preview.scene.curves.size(), 1U);
  ASSERT_EQ(preview.scene.lights.size(), 3U);
  ASSERT_EQ(preview.scene.group_markers.size(), 2U);

  const ModelingToolPreviewMesh &mesh = preview.scene.meshes.front();
  EXPECT_EQ(mesh.source_entity, hierarchy.mesh);
  EXPECT_TRUE(mesh.cast_shadows);
  EXPECT_TRUE(mesh.suppress_source);
  EXPECT_EQ(mesh.instance_transforms.size(), kPathArrayDefaultCount);
  EXPECT_EQ(preview.scene.curves.front().source_entity, hierarchy.curve);
  EXPECT_FALSE(preview.scene.curves.front().solidify);
  EXPECT_EQ(preview.scene.curves.front().instance_transforms.size(),
            kPathArrayDefaultCount);
  for (const EntityId marker_entity : {hierarchy.root, hierarchy.generic}) {
    const auto marker = std::ranges::find(
        preview.scene.group_markers, marker_entity,
        &ModelingToolPreviewGroupMarker::source_entity);
    ASSERT_NE(marker, preview.scene.group_markers.end());
    EXPECT_EQ(marker->instance_transforms.size(), kPathArrayDefaultCount);
  }

  const auto preview_light = [&](EntityId entity)
      -> const ModelingToolPreviewLight * {
    const auto found = std::ranges::find(preview.scene.lights, entity,
                                         &ModelingToolPreviewLight::source_entity);
    return found == preview.scene.lights.end() ? nullptr : &*found;
  };
  const ModelingToolPreviewLight *point = preview_light(hierarchy.point);
  const ModelingToolPreviewLight *directional =
      preview_light(hierarchy.directional);
  const ModelingToolPreviewLight *spot = preview_light(hierarchy.spot);
  ASSERT_NE(point, nullptr);
  ASSERT_NE(directional, nullptr);
  ASSERT_NE(spot, nullptr);
  EXPECT_TRUE(point->light == hierarchy.point_component);
  EXPECT_TRUE(directional->light == hierarchy.directional_component);
  EXPECT_TRUE(spot->light == hierarchy.spot_component);
  EXPECT_EQ(point->instance_transforms.size(), kPathArrayDefaultCount);
  EXPECT_EQ(directional->instance_transforms.size(), kPathArrayDefaultCount);
  EXPECT_EQ(spot->instance_transforms.size(), kPathArrayDefaultCount);

  const std::array<EntityId, 10U> complete_subtree{
      hierarchy.root,         hierarchy.mesh,        hierarchy.curve,
      hierarchy.point,        hierarchy.directional, hierarchy.spot,
      hierarchy.generic,      hierarchy.hidden_mesh, hierarchy.hidden_curve,
      hierarchy.hidden_light,
  };
  EXPECT_EQ(sorted_entities(preview.scene.suppression.entities),
            sorted_entities(complete_subtree));
  EXPECT_EQ(preview_light(hierarchy.hidden_light), nullptr);
  EXPECT_EQ(std::ranges::find(preview.scene.meshes, hierarchy.hidden_mesh,
                              &ModelingToolPreviewMesh::source_entity),
            preview.scene.meshes.end());
  EXPECT_EQ(std::ranges::find(preview.scene.curves, hierarchy.hidden_curve,
                              &ModelingToolPreviewCurve::source_entity),
            preview.scene.curves.end());

  ToolPreviewScene neutral = project_modeling_tool_preview_scene(
      preview.scene, harness.session.scene());
  RenderToolPreviewSceneSnapshot snapshot =
      build_render_tool_preview_scene_snapshot(std::move(neutral),
                                               harness.session.assets());
  ASSERT_TRUE(snapshot.complete);
  EXPECT_EQ(snapshot.revision, preview.scene.revision);
  ASSERT_EQ(snapshot.meshes.size(), 1U);
  EXPECT_TRUE(snapshot.meshes.front().cast_shadows);
  ASSERT_EQ(snapshot.curves.size(), 1U);
  ASSERT_NE(snapshot.curves.front().sample_payload, nullptr);
  EXPECT_FALSE(snapshot.curves.front().solidify);
  EXPECT_FALSE(snapshot.curves.front().sample_payload->samples.empty());
  ASSERT_EQ(snapshot.lights.size(), 3U);
  EXPECT_TRUE(snapshot.lights[0].light == preview.scene.lights[0].light);
  EXPECT_TRUE(snapshot.lights[1].light == preview.scene.lights[1].light);
  EXPECT_TRUE(snapshot.lights[2].light == preview.scene.lights[2].light);
  EXPECT_EQ(snapshot.suppression.entities,
            sorted_entities(complete_subtree));
  EXPECT_FALSE(snapshot.suppression.empty());
  EXPECT_NE(snapshot.suppression.signature, 0U);

  const ViewportOverlayFrame first =
      build_render_tool_preview_overlay_frame(snapshot);
  const ViewportOverlayFrame second =
      build_render_tool_preview_overlay_frame(snapshot);
  expect_overlay_frames_equal(first, second);
  EXPECT_EQ(std::ranges::count(first.icons,
                               ViewportOverlayLayer::PointLightGizmo,
                               &ViewportOverlayIcon::layer),
            kPathArrayDefaultCount);
  EXPECT_EQ(std::ranges::count(first.points, ViewportOverlayLayer::ToolHandle,
                               &ViewportOverlayPoint::layer),
            kPathArrayDefaultCount * 2U);
  EXPECT_EQ(std::ranges::count(first.points, ViewportOverlayLayer::GroupBounds,
                               &ViewportOverlayPoint::layer),
            kPathArrayDefaultCount * 2U);
  EXPECT_EQ(std::ranges::count(first.lines, ViewportOverlayLayer::GroupBounds,
                               &ViewportOverlayLine::layer),
            kPathArrayDefaultCount * 2U * 12U);
  EXPECT_TRUE(std::ranges::all_of(
      first.icons,
      [](const ViewportOverlayIcon &icon) {
        return icon.source == ViewportOverlaySource::Tool;
      }));
}

TEST(PathArrayCompoundPreviewTests,
     SolidifiedCurveParticipantPublishesCachedTransientTubeAtEveryStation) {
  CompoundPreviewHarness harness;
  const QuaderModelingObjectRef path = create_curve(harness.editor);
  const QuaderModelingObjectRef source_curve = create_curve(harness.editor);
  SceneDocument &scene = harness.session.scene();
  const AssetId pipe_material = harness.session.assets().add_material(
      make_default_quader_material("Path Array Pipe"));
  ASSERT_NE(pipe_material, kInvalidAssetId);
  const EntityId source_root = scene.create_empty("Solid Curve Source");
  ASSERT_TRUE(scene.set_parent(source_curve.entity, source_root, false));
  ASSERT_TRUE(scene.set_material(
      source_curve.entity, MaterialComponent{.material = pipe_material}));
  const CurveComponent *authored = scene.curve(source_curve.entity);
  ASSERT_NE(authored, nullptr);
  CurveComponent solid = *authored;
  solid.solidify = true;
  solid.solidify_resolution = 12;
  solid.solidify_radius = 0.35F;
  ASSERT_TRUE(scene.set_curve(source_curve.entity, solid));
  ASSERT_EQ(scene.mesh(source_curve.entity), nullptr);

  ASSERT_TRUE(harness.editor.selection().select_object(path).ok());
  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(source_root).ok);

  const PathArrayPreviewState &first_preview = tool.preview_state();
  ASSERT_TRUE(first_preview.active);
  ASSERT_EQ(first_preview.scene.curves.size(), 1U);
  const ModelingToolPreviewCurve &modeled_curve =
      first_preview.scene.curves.front();
  EXPECT_EQ(modeled_curve.source_entity, source_curve.entity);
  EXPECT_TRUE(modeled_curve.solidify);
  EXPECT_EQ(modeled_curve.solidify_resolution, 12);
  EXPECT_FLOAT_EQ(modeled_curve.solidify_radius, 0.35F);
  EXPECT_EQ(modeled_curve.material.material.embedded_asset(), pipe_material);
  EXPECT_EQ(modeled_curve.instance_transforms.size(),
            kPathArrayDefaultCount);

  CurveSolidGeometryCache geometry_cache;
  ToolPreviewScene neutral = project_modeling_tool_preview_scene(
      first_preview.scene, scene);
  ASSERT_EQ(neutral.curves.size(), 1U);
  EXPECT_TRUE(neutral.curves.front().solidify);
  EXPECT_EQ(neutral.curves.front().solidify_resolution, 12);
  EXPECT_FLOAT_EQ(neutral.curves.front().solidify_radius, 0.35F);
  RenderToolPreviewSceneSnapshot first =
      build_render_tool_preview_scene_snapshot(
          std::move(neutral), harness.session.assets(), nullptr, nullptr,
          &geometry_cache);
  ASSERT_TRUE(first.complete);
  ASSERT_EQ(first.curves.size(), 1U);
  EXPECT_TRUE(first.curves.front().solidify);
  EXPECT_EQ(first.curves.front().solidify_resolution, 12);
  EXPECT_FLOAT_EQ(first.curves.front().solidify_radius, 0.35F);
  ASSERT_EQ(first.meshes.size(), 1U);
  const RenderToolPreviewMeshRecord &tube = first.meshes.front();
  EXPECT_EQ(tube.source_entity, source_curve.entity);
  EXPECT_EQ(tube.material.component.material.embedded_asset(), pipe_material);
  EXPECT_TRUE(tube.cast_shadows);
  EXPECT_FALSE(tube.double_sided);
  EXPECT_EQ(tube.instance_transforms.size(), kPathArrayDefaultCount);
  ASSERT_TRUE(tube.renderable());
  const std::shared_ptr<const MeshGeometryPayload> geometry =
      tube.mesh.geometry_payload();
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(geometry_cache.stats().build_count, 1U);
  EXPECT_FALSE(build_render_tool_preview_overlay_frame(first).lines.empty())
      << "solid Curve previews retain the authored centerline overlay";
  EXPECT_EQ(scene.mesh(source_curve.entity), nullptr)
      << "preview derivation must not author a persistent Mesh component";

  ASSERT_TRUE(tool.set_count(3U).ok);
  ToolPreviewScene moved = project_modeling_tool_preview_scene(
      tool.preview_state().scene, scene);
  RenderToolPreviewSceneSnapshot second =
      build_render_tool_preview_scene_snapshot(
          std::move(moved), harness.session.assets(), nullptr, nullptr,
          &geometry_cache);
  ASSERT_TRUE(second.complete);
  ASSERT_EQ(second.meshes.size(), 1U);
  EXPECT_EQ(second.meshes.front().instance_transforms.size(), 3U);
  EXPECT_EQ(second.meshes.front().mesh.geometry_payload(), geometry);
  EXPECT_EQ(geometry_cache.stats().build_count, 1U)
      << "station-only changes reuse the exact derived tube";
  EXPECT_EQ(geometry_cache.stats().hit_count, 1U);
}

TEST(PathArrayCompoundPreviewTests,
     NestedEmptyAndGenericMarkersConsumeExactPreviewParticipantBudget) {
  CompoundPreviewHarness harness;
  const QuaderModelingObjectRef path = create_curve(harness.editor);
  const QuaderModelingObjectRef mesh_template = create_box(harness.editor);
  SceneDocument &scene = harness.session.scene();
  const MeshComponent *mesh_component = scene.mesh(mesh_template.entity);
  ASSERT_NE(mesh_component, nullptr);

  constexpr std::size_t kMarkerCount = 3U;
  constexpr std::size_t kExactMeshCount =
      kPathArrayMaximumPreviewParticipants / kPathArrayMinimumCount -
      kMarkerCount;
  static_assert(kExactMeshCount == 509U);
  const MarkerBudgetHierarchy exact = create_marker_budget_hierarchy(
      scene, mesh_component->mesh, kExactMeshCount, "Exact Marker Budget");
  const MarkerBudgetHierarchy excess = create_marker_budget_hierarchy(
      scene, mesh_component->mesh, kExactMeshCount + 1U,
      "Excess Marker Budget");

  ASSERT_TRUE(harness.editor.selection().select_object(path).ok());
  PathArrayToolService &tool = harness.session.path_array();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.select_source(exact.root).ok);
  EXPECT_EQ(tool.maximum_count(), kPathArrayMinimumCount);
  EXPECT_EQ(tool.count(), kPathArrayMinimumCount);

  const PathArrayPreviewState &preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.scene.meshes.size(), kExactMeshCount);
  ASSERT_EQ(preview.scene.group_markers.size(), kMarkerCount);
  EXPECT_EQ(preview.scene.suppression.entities.size(), 4U)
      << "the hierarchy-node budget is far below its limit";
  EXPECT_EQ(preview.scene.suppression.occurrences.size(), kExactMeshCount);

  const std::array expected_markers{
      exact.root,
      exact.nested_empty,
      exact.nested_generic,
  };
  for (const EntityId marker_entity : expected_markers) {
    const auto marker = std::ranges::find(
        preview.scene.group_markers, marker_entity,
        &ModelingToolPreviewGroupMarker::source_entity);
    ASSERT_NE(marker, preview.scene.group_markers.end());
    EXPECT_EQ(marker->instance_transforms.size(), kPathArrayMinimumCount);
  }

  std::size_t visible_preview_placements = 0U;
  for (const ModelingToolPreviewMesh &mesh : preview.scene.meshes) {
    visible_preview_placements += mesh.instance_transforms.size();
  }
  for (const ModelingToolPreviewGroupMarker &marker :
       preview.scene.group_markers) {
    visible_preview_placements += marker.instance_transforms.size();
  }
  EXPECT_EQ(visible_preview_placements,
            kPathArrayMaximumPreviewParticipants);

  const std::uint64_t exact_revision = preview.revision;
  const CommandResult rejected = tool.select_source(excess.root);
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(tool.source_root(), exact.root);
  EXPECT_EQ(tool.preview_state().revision, exact_revision);
}

TEST(PathArrayCompoundPreviewTests,
     HiddenLightsRemainSuppressedWithoutConsumingTransientLightBudgets) {
  CompoundPreviewHarness harness;
  const QuaderModelingObjectRef path = create_curve(harness.editor);
  SceneDocument &scene = harness.session.scene();
  const EntityId root = scene.create_empty("Hidden Light Budget Root");
  const EntityId visible = scene.create_entity("Visible", EntityKind::Light);
  ASSERT_TRUE(scene.set_light(
      visible, LightComponent{.type = LightType::Point,
                              .casts_shadows = true}));
  ASSERT_TRUE(scene.set_parent(visible, root, false));
  for (std::size_t index = 0U; index < 8U; ++index) {
    const EntityId hidden =
        scene.create_entity("Hidden " + std::to_string(index),
                            EntityKind::Light);
    ASSERT_TRUE(scene.set_light(
        hidden, LightComponent{.type = LightType::Point,
                               .casts_shadows = true}));
    ASSERT_TRUE(scene.set_parent(hidden, root, false));
    ASSERT_TRUE(scene.set_visible(hidden, false));
  }

  ASSERT_TRUE(harness.editor.selection().select_object(path).ok());
  ASSERT_TRUE(harness.session.path_array().activate().ok);
  ASSERT_TRUE(harness.session.path_array().select_source(root).ok);
  EXPECT_EQ(harness.session.path_array().maximum_count(), 16U);
  const PathArrayPreviewState &preview =
      harness.session.path_array().preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.scene.lights.size(), 1U);
  EXPECT_EQ(preview.scene.lights.front().source_entity, visible);
  EXPECT_EQ(preview.scene.lights.front().instance_transforms.size(),
            kPathArrayDefaultCount);
  EXPECT_EQ(preview.scene.suppression.entities.size(), 10U);
}

TEST(PathArrayCompoundPreviewTests,
     ExactLightAndShadowBudgetsClampCountAndRejectOverflowAtomically) {
  CompoundPreviewHarness harness;
  const QuaderModelingObjectRef path = create_curve(harness.editor);
  SceneDocument &scene = harness.session.scene();
  const EntityId sixty_four =
      create_light_group(scene, "Exactly 128 Preview Lights", 64U, false);
  const EntityId sixty_five =
      create_light_group(scene, "Over 128 Preview Lights", 65U, false);
  const EntityId eight_shadows =
      create_light_group(scene, "Exactly 16 Preview Shadows", 8U, true);
  const EntityId nine_shadows =
      create_light_group(scene, "Over 16 Preview Shadows", 9U, true);

  ASSERT_TRUE(harness.editor.selection().select_object(path).ok());
  ASSERT_TRUE(harness.session.path_array().activate().ok);
  ASSERT_TRUE(harness.session.path_array().select_source(sixty_four).ok);
  EXPECT_EQ(harness.session.path_array().maximum_count(), 2U);
  EXPECT_EQ(harness.session.path_array().count(), 2U);
  const PathArrayPreviewState &light_boundary =
      harness.session.path_array().preview_state();
  ASSERT_TRUE(light_boundary.active);
  ASSERT_EQ(light_boundary.scene.lights.size(), 64U);
  EXPECT_TRUE(std::ranges::all_of(
      light_boundary.scene.lights,
      [](const ModelingToolPreviewLight &light) {
        return light.instance_transforms.size() == 2U;
      }));

  const std::uint64_t light_boundary_revision = light_boundary.revision;
  const CommandResult excess_lights =
      harness.session.path_array().select_source(sixty_five);
  EXPECT_FALSE(excess_lights.ok);
  EXPECT_EQ(harness.session.path_array().source_root(), sixty_four);
  EXPECT_EQ(harness.session.path_array().preview_state().revision,
            light_boundary_revision);

  ASSERT_TRUE(harness.session.path_array().select_source(eight_shadows).ok);
  EXPECT_EQ(harness.session.path_array().maximum_count(), 2U);
  const PathArrayPreviewState &shadow_boundary =
      harness.session.path_array().preview_state();
  ASSERT_EQ(shadow_boundary.scene.lights.size(), 8U);
  EXPECT_EQ(std::ranges::count_if(
                shadow_boundary.scene.lights,
                [](const ModelingToolPreviewLight &light) {
                  return light.light.casts_shadows;
                }),
            8U);
  EXPECT_TRUE(std::ranges::all_of(
      shadow_boundary.scene.lights,
      [](const ModelingToolPreviewLight &light) {
        return light.instance_transforms.size() == 2U;
      }));

  const std::uint64_t shadow_boundary_revision = shadow_boundary.revision;
  const CommandResult excess_shadows =
      harness.session.path_array().select_source(nine_shadows);
  EXPECT_FALSE(excess_shadows.ok);
  EXPECT_EQ(harness.session.path_array().source_root(), eight_shadows);
  EXPECT_EQ(harness.session.path_array().preview_state().revision,
            shadow_boundary_revision);
}

TEST(PathArrayCompoundPreviewTests,
     FailedCompoundPublicationRetainsPriorGenerationAndCachesTheFailure) {
  CompoundPreviewHarness harness;
  (void)create_mixed_hierarchy(harness);
  std::optional<RenderToolPreviewSceneSnapshot> retained;
  std::size_t publication_attempts = 0U;
  bool reject_next = false;
  const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
      [&](RenderToolPreviewSceneSnapshot candidate) {
        ++publication_attempts;
        if (std::exchange(reject_next, false)) {
          return ToolPreviewSubmissionResult::Failed;
        }
        retained = std::move(candidate);
        return ToolPreviewSubmissionResult::Published;
      };

  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
                  harness.editor.render(), harness.bridge, {}, publisher)
                  .ok());
  ASSERT_TRUE(retained.has_value());
  ASSERT_TRUE(retained->complete);
  const std::uint64_t first_revision = retained->revision;
  const std::size_t first_mesh_placements =
      retained->meshes.front().instance_transforms.size();
  ASSERT_EQ(first_mesh_placements, kPathArrayDefaultCount);
  const std::size_t first_attempt_count = publication_attempts;

  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
                  harness.editor.render(), harness.bridge, {}, publisher)
                  .ok());
  EXPECT_EQ(publication_attempts, first_attempt_count)
      << "a second pane consumes the already published semantic generation";

  ASSERT_TRUE(harness.session.path_array().set_count(3U).ok);
  reject_next = true;
  EXPECT_FALSE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
                   harness.editor.render(), harness.bridge, {}, publisher)
                   .ok());
  EXPECT_EQ(retained->revision, first_revision);
  EXPECT_EQ(retained->meshes.front().instance_transforms.size(),
            first_mesh_placements);
  const std::size_t failed_attempt_count = publication_attempts;

  EXPECT_FALSE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
                   harness.editor.render(), harness.bridge, {}, publisher)
                   .ok());
  EXPECT_EQ(publication_attempts, failed_attempt_count)
      << "an unchanged failed generation is not rebuilt or resubmitted";
  EXPECT_EQ(retained->revision, first_revision);

  ASSERT_TRUE(harness.session.path_array().set_count(2U).ok);
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
                  harness.editor.render(), harness.bridge, {}, publisher)
                  .ok());
  ASSERT_TRUE(retained.has_value());
  EXPECT_GT(retained->revision, first_revision);
  ASSERT_EQ(retained->meshes.size(), 1U);
  EXPECT_EQ(retained->meshes.front().instance_transforms.size(), 2U);
  EXPECT_EQ(retained->curves.front().instance_transforms.size(), 2U);
  EXPECT_TRUE(std::ranges::all_of(
      retained->lights, [](const RenderToolPreviewLightRecord &light) {
        return light.instance_transforms.size() == 2U;
      }));
}

TEST(PathArrayCompoundPreviewTests,
     MissingRequestedParticipantsMarkTheWholeExtractedCandidateIncomplete) {
  AssetRegistry assets;
  ToolPreviewScene preview;
  preview.revision = 71U;
  preview.meshes.push_back({
      .source_entity = 10U,
      .mesh = MeshAsset{},
      .instance_transforms = {QMat4{}, translated(1.0F, 0.0F, 0.0F)},
      .instance_sampling_from_local = {QMat4{}},
      .suppress_source = true,
  });
  preview.curves.push_back({
      .source_entity = 11U,
      .curve = 9999U,
      .instance_transforms = {translated(1.0F, 2.0F, 3.0F)},
      .suppress_source = true,
  });
  preview.lights.push_back({
      .source_entity = 12U,
      .light = LightComponent{.type = LightType::Spot,
                              .intensity = 5.0F,
                              .casts_shadows = true},
      .suppress_source = true,
  });
  preview.group_markers.push_back({
      .source_entity = 13U,
      .local_min = {-1.0F, -1.0F, -1.0F},
      .local_max = {1.0F, 1.0F, 1.0F},
  });
  preview.suppressed_entities = {13U, 12U, 11U, 10U};

  const RenderToolPreviewSceneSnapshot snapshot =
      build_render_tool_preview_scene_snapshot(std::move(preview), assets);
  EXPECT_FALSE(snapshot.complete);
  EXPECT_EQ(snapshot.revision, 71U);
  ASSERT_EQ(snapshot.meshes.size(), 1U);
  EXPECT_TRUE(snapshot.curves.empty());
  EXPECT_TRUE(snapshot.lights.empty());
  EXPECT_TRUE(snapshot.group_markers.empty());
  EXPECT_EQ(snapshot.suppression.entities,
            (std::vector<EntityId>{10U, 11U, 12U, 13U}));
}

} // namespace
} // namespace quader
