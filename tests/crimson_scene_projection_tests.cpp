////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "render/crimson/crimson_scene_projection.hpp"
#include "render/crimson/crimson_render_host.hpp"
#include "viewport/viewport_settings.hpp"
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "render/curve_solid_geometry_cache.hpp"
#include "render/scene_render_extraction.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <limits>

namespace quader {
namespace {

std::shared_ptr<const MeshGeometryPayload> make_quad_geometry() {
  auto geometry = std::make_shared<MeshGeometryPayload>();
  geometry->vertices = {
      {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
      {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
      {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
  geometry->indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry->primitives = {{0u, 3u, 4u}, {3u, 3u, 9u}};
  return geometry;
}

RenderMeshAssetSnapshot mesh_snapshot(
    const std::shared_ptr<const MeshGeometryPayload> &geometry) {
  RenderMeshAssetSnapshot mesh;
  mesh.mesh_id = 27u;
  mesh.mesh_revision = 1u;
  mesh.geometry_payload = geometry;
  mesh.vertices = geometry->vertices;
  mesh.indices = geometry->indices;
  mesh.primitives = geometry->primitives;
  mesh.present = true;
  return mesh;
}

RenderMaterialBindingSnapshot untextured_material() {
  RenderMaterialBindingSnapshot material;
  material.present = true;
  material.primary_material.present = true;
  material.primary_material.material_id = 83u;
  material.primary_material.revision = 1u;
  return material;
}

std::shared_ptr<RenderSceneSnapshot> make_scene(
    const std::shared_ptr<const MeshGeometryPayload> &geometry) {
  auto scene = std::make_shared<RenderSceneSnapshot>();
  RenderSceneMeshRecord record;
  record.entity_id = 101u;
  record.mesh = mesh_snapshot(geometry);
  record.material = untextured_material();
  record.transform_revision = 1u;
  scene->meshes.push_back(std::move(record));
  return scene;
}

TEST(CrimsonSceneProjectionTests, InstanceContextFlagsRetainAuthoritativeOccurrenceMetadataWithoutGeometryUploads){
  const auto geometry=make_quad_geometry();auto scene=std::make_shared<RenderSceneSnapshot>();
  RenderSceneInstancePrimitiveRecord group;group.mesh=mesh_snapshot(geometry);group.primitive={0,6,4};group.material=untextured_material();group.key.double_sided=true;
  group.occurrences.push_back({.entity_id=11,.occurrence=1,.dimmed=false});group.occurrences.push_back({.entity_id=12,.occurrence=2,.dimmed=true});scene->instance_primitives.push_back(group);
  CrimsonSceneProjectionCache cache({});std::shared_ptr<const CrimsonSceneProjection> first,second;std::string error;
  ASSERT_TRUE(cache.prepare(scene,{},1,first,error))<<error;ASSERT_EQ(first->instances.size(),2u);
  EXPECT_FALSE(first->instances[0].context_dimmed);EXPECT_TRUE(first->instances[1].context_dimmed);
  const auto work=cache.stats();auto reset=std::make_shared<RenderSceneSnapshot>(*scene);reset->instance_primitives[0].occurrences[1].dimmed=false;
  ASSERT_TRUE(cache.prepare(reset,{},2,second,error))<<error;EXPECT_FALSE(second->instances[1].context_dimmed);
  EXPECT_EQ(first->instances[1].id,second->instances[1].id);EXPECT_TRUE(first->instances[1].context_dimmed);
  EXPECT_EQ(first->meshes[0].positions.pin,second->meshes[0].positions.pin);EXPECT_EQ(cache.stats().position_packs,work.position_packs);EXPECT_EQ(cache.stats().attribute_packs,work.attribute_packs);
}

TEST(CrimsonSceneProjectionTests, TransformOnlyPublicationRetainsPinnedStreams) {
  const auto geometry = make_quad_geometry();
  auto scene = make_scene(geometry);
  CrimsonSceneProjectionCache cache({});
  std::shared_ptr<const CrimsonSceneProjection> first;
  std::string error;
  ASSERT_TRUE(cache.prepare(scene, {}, 1u, first, error)) << error;
  ASSERT_EQ(first->meshes.size(), 1u);
  ASSERT_EQ(first->instances.size(), 1u);
  const auto work = cache.stats();

  std::shared_ptr<const CrimsonSceneProjection> unchanged;
  ASSERT_TRUE(cache.prepare(scene, {}, 2u, unchanged, error)) << error;
  EXPECT_EQ(cache.stats().position_packs, work.position_packs);
  EXPECT_EQ(cache.stats().attribute_packs, work.attribute_packs);
  EXPECT_EQ(cache.stats().index_packs, work.index_packs);

  auto moved = std::make_shared<RenderSceneSnapshot>(*scene);
  moved->meshes.front().world_transform.values[12] = 7.0f;
  ++moved->meshes.front().transform_revision;
  std::shared_ptr<const CrimsonSceneProjection> translated;
  ASSERT_TRUE(cache.prepare(moved, {}, 3u, translated, error)) << error;
  EXPECT_FLOAT_EQ(translated->instances.front().world_from_local.values[12], 7.0f);
  EXPECT_EQ(translated->instances.front().id, first->instances.front().id);
  EXPECT_EQ(translated->meshes.front().positions.pin, first->meshes.front().positions.pin);
  EXPECT_EQ(translated->meshes.front().attributes.pin, first->meshes.front().attributes.pin);
  EXPECT_EQ(translated->meshes.front().indices.pin, first->meshes.front().indices.pin);
  EXPECT_EQ(cache.stats().position_packs, work.position_packs);
  EXPECT_EQ(cache.stats().attribute_packs, work.attribute_packs);
  EXPECT_EQ(cache.stats().index_packs, work.index_packs);
  ASSERT_EQ(translated->meshes.front().ranges.values.size(), 2u);
  EXPECT_EQ(translated->meshes.front().ranges.values[0].material_slot, 4u);
  EXPECT_EQ(translated->meshes.front().ranges.values[1].material_slot, 9u);
}

TEST(CrimsonSceneProjectionTests, InstanceSuppressionKeepsOtherOccurrencesAndSharesStreams) {
  const auto geometry = make_quad_geometry();
  auto scene = std::make_shared<RenderSceneSnapshot>();
  for (const auto &primitive : geometry->primitives) {
    RenderSceneInstancePrimitiveRecord record;
    record.definition_entity = 50u;
    record.member_entity = 60u;
    record.mesh = mesh_snapshot(geometry);
    record.material = untextured_material();
    record.primitive = primitive;
    RenderSceneInstanceOccurrenceRecord occurrence;
    occurrence.entity_id = 401u;
    occurrence.occurrence = 1u;
    record.occurrences.push_back(occurrence);
    occurrence.entity_id = 402u;
    occurrence.occurrence = 2u;
    occurrence.world_transform.values[12] = 8.0f;
    record.occurrences.push_back(occurrence);
    scene->instance_primitives.push_back(std::move(record));
  }
  auto preview = std::make_shared<RenderToolPreviewSceneSnapshot>();
  preview->revision = 1u;
  preview->suppression.occurrences.push_back({60u, 401u});
  CrimsonSceneProjectionCache cache({});
  std::shared_ptr<const CrimsonSceneProjection> projection;
  std::string error;
  ASSERT_TRUE(cache.prepare(scene, preview, 1u, projection, error)) << error;
  ASSERT_EQ(projection->instances.size(), 2u);
  ASSERT_EQ(projection->meshes.size(), 2u);
  for (const auto &instance : projection->instances) {
    EXPECT_FLOAT_EQ(instance.world_from_local.values[12], 8.0f);
  }
  EXPECT_EQ(projection->meshes[0].positions.pin, projection->meshes[1].positions.pin);
  EXPECT_EQ(projection->meshes[0].attributes.pin, projection->meshes[1].attributes.pin);
  EXPECT_EQ(projection->meshes[0].indices.pin, projection->meshes[1].indices.pin);
  ASSERT_EQ(projection->meshes[0].ranges.values.size(), 1u);
  ASSERT_EQ(projection->meshes[1].ranges.values.size(), 1u);
  EXPECT_NE(projection->meshes[0].ranges.values[0].first_index,
            projection->meshes[1].ranges.values[0].first_index);
}

TEST(CrimsonSceneProjectionTests, PositionPreviewReplacesOnlyItsSourceAndRetainsStaticStreams) {
  const auto geometry = make_quad_geometry();
  auto scene = make_scene(geometry);
  CrimsonSceneProjectionCache cache({});
  std::shared_ptr<const CrimsonSceneProjection> committed;
  std::string error;
  ASSERT_TRUE(cache.prepare(scene, {}, 1u, committed, error)) << error;
  auto preview = std::make_shared<RenderToolPreviewSceneSnapshot>();
  preview->revision = 4u;
  preview->suppression.entities.push_back(101u);
  RenderToolPreviewMeshRecord record;
  record.source_entity = 101u;
  record.suppress_source = true;
  record.layout_key = 12u;
  record.mesh.share_geometry(geometry);
  record.material = untextured_material();
  record.position_revision = 4u;
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : geometry->vertices) {
    positions->push_back(vertex.position + QVec3{0.0f, 0.0f, 2.0f});
  }
  record.dynamic_position_payload = positions;
  record.dynamic_positions = true;
  preview->meshes.push_back(std::move(record));
  std::shared_ptr<const CrimsonSceneProjection> projected;
  ASSERT_TRUE(cache.prepare(scene, preview, 2u, projected, error)) << error;
  ASSERT_EQ(projected->meshes.size(), 1u);
  ASSERT_EQ(projected->instances.size(), 1u);
  EXPECT_EQ(projected->preview, preview);
  EXPECT_NE(projected->meshes.front().positions.pin, committed->meshes.front().positions.pin);
  EXPECT_EQ(projected->meshes.front().attributes.pin, committed->meshes.front().attributes.pin);
  EXPECT_EQ(projected->meshes.front().indices.pin, committed->meshes.front().indices.pin);
  for (const auto &position : projected->meshes.front().positions.values) {
    EXPECT_FLOAT_EQ(position[2], 2.0f);
  }
  EXPECT_FLOAT_EQ(geometry->vertices.front().position.z, 0.0f);

  const auto work = cache.stats();
  std::shared_ptr<const CrimsonSceneProjection> repeated;
  ASSERT_TRUE(cache.prepare(scene, preview, 3u, repeated, error)) << error;
  EXPECT_EQ(repeated->meshes.front().positions.pin, projected->meshes.front().positions.pin);
  EXPECT_EQ(cache.stats().position_packs, work.position_packs);
  EXPECT_EQ(cache.stats().attribute_packs, work.attribute_packs);
  EXPECT_EQ(cache.stats().index_packs, work.index_packs);
}

TEST(CrimsonSceneProjectionTests, RejectedWholeCandidatesKeepOutputAndAcceptedOwnersAlive) {
  const auto geometry = make_quad_geometry();
  auto scene = make_scene(geometry);
  CrimsonSceneProjectionCache cache({});
  std::shared_ptr<const CrimsonSceneProjection> projection;
  std::string error;
  ASSERT_TRUE(cache.prepare(scene, {}, 1u, projection, error)) << error;
  const auto accepted = projection;
  auto incomplete = std::make_shared<RenderToolPreviewSceneSnapshot>();
  incomplete->complete = false;
  EXPECT_FALSE(cache.prepare(scene, incomplete, 2u, projection, error));
  EXPECT_EQ(projection, accepted);
  EXPECT_FALSE(error.empty());

  auto malformed_geometry = std::make_shared<MeshGeometryPayload>(*geometry);
  malformed_geometry->indices.back() = 99u;
  auto malformed = make_scene(malformed_geometry);
  ++malformed->meshes.front().mesh.mesh_revision;
  EXPECT_FALSE(cache.prepare(malformed, {}, 3u, projection, error));
  EXPECT_EQ(projection, accepted);
  EXPECT_FALSE(error.empty());
  ASSERT_TRUE(cache.prepare(scene, {}, 4u, projection, error)) << error;
  EXPECT_EQ(projection->meshes.front().positions.pin, accepted->meshes.front().positions.pin);
  EXPECT_EQ(projection->meshes.front().attributes.pin, accepted->meshes.front().attributes.pin);
  EXPECT_EQ(projection->meshes.front().indices.pin, accepted->meshes.front().indices.pin);
  scene.reset();
  cache.clear();
  EXPECT_EQ(accepted->meshes.front().indices.values.back(), 3u);
  EXPECT_FLOAT_EQ(accepted->meshes.front().positions.values.front()[0], -1.0f);
}

TEST(CrimsonSceneProjectionTests, DefaultImagePinsSurviveFactorChangesWithoutDecoding) {
  const auto geometry = make_quad_geometry();
  auto scene = make_scene(geometry);
  auto &material = scene->meshes.front().material.primary_material;
  material.material_id = 83u;
  material.present = true;
  material.revision = 1u;
  material.document.base_color = {0.25f, 0.5f, 0.75f};
  auto pixels = std::make_shared<const std::vector<std::byte>>(
      std::vector<std::byte>{std::byte{32}, std::byte{96}, std::byte{192}, std::byte{255}});
  RenderTextureAssetSnapshot texture;
  texture.slot = material_texture_slot::kBaseColor;
  texture.texture_id = 89u;
  texture.content_revision = 3u;
  texture.present = true;
  texture.decoded_rgba8 = pixels;
  texture.width = 1u;
  texture.height = 1u;
  texture.texture.color_space = TextureColorSpace::Srgb;
  material.textures.push_back(texture);
  ASSERT_TRUE(set_material_texture_slot(material.document,
      material_texture_slot::kBaseColor, texture.texture_id));
  CrimsonSceneProjectionCache cache({});
  std::shared_ptr<const CrimsonSceneProjection> first;
  std::string error;
  ASSERT_TRUE(cache.prepare(scene, {}, 1u, first, error)) << error;
  ASSERT_EQ(first->surfaces.size(), 1u);
  EXPECT_EQ(first->surfaces.front().image.rgba8.pin, pixels);
  EXPECT_TRUE(first->surfaces.front().image.srgb);
  EXPECT_FLOAT_EQ(first->surfaces.front().base_color[1], 0.5f);
  EXPECT_EQ(cache.stats().image_decodes, 0u);

  auto changed = std::make_shared<RenderSceneSnapshot>(*scene);
  auto &changed_material = changed->meshes.front().material.primary_material;
  changed_material.document.base_color.x = 0.6f;
  ++changed_material.revision;
  std::shared_ptr<const CrimsonSceneProjection> second;
  ASSERT_TRUE(cache.prepare(changed, {}, 2u, second, error)) << error;
  ASSERT_EQ(second->surfaces.size(), 1u);
  EXPECT_EQ(second->surfaces.front().id, first->surfaces.front().id);
  EXPECT_FLOAT_EQ(second->surfaces.front().base_color[0], 0.6f);
  EXPECT_EQ(second->surfaces.front().image.rgba8.pin, pixels);
  EXPECT_EQ(second->surfaces.front().image.revision, first->surfaces.front().image.revision);
  EXPECT_EQ(cache.stats().image_decodes, 0u);

  const auto geometry_work = cache.stats();
  auto reloaded = std::make_shared<RenderSceneSnapshot>(*changed);
  auto &reloaded_material = reloaded->meshes.front().material.primary_material;
  ++reloaded_material.textures.front().texture.revision.value;
  ++reloaded_material.revision;
  std::shared_ptr<const CrimsonSceneProjection> third;
  ASSERT_TRUE(cache.prepare(reloaded, {}, 3u, third, error)) << error;
  ASSERT_EQ(third->surfaces.size(), 1u);
  EXPECT_EQ(third->surfaces.front().image.id, second->surfaces.front().image.id);
  EXPECT_NE(third->surfaces.front().image.revision, second->surfaces.front().image.revision);
  EXPECT_NE(third->surfaces.front().image.rgba8.revision, second->surfaces.front().image.rgba8.revision);
  EXPECT_EQ(third->surfaces.front().image.rgba8.pin, pixels);
  EXPECT_EQ(cache.stats().position_packs, geometry_work.position_packs);
  EXPECT_EQ(cache.stats().attribute_packs, geometry_work.attribute_packs);
  EXPECT_EQ(cache.stats().index_packs, geometry_work.index_packs);
  EXPECT_EQ(cache.stats().image_decodes, 0u);

  auto invalid = std::make_shared<RenderSceneSnapshot>(*reloaded);
  auto &invalid_material = invalid->meshes.front().material.primary_material;
  invalid_material.textures.front().width = 2u;
  ++invalid_material.revision;
  const auto accepted = second;
  EXPECT_FALSE(cache.prepare(invalid, {}, 4u, second, error));
  EXPECT_EQ(second, accepted);
  EXPECT_FALSE(error.empty());
}

TEST(CrimsonSceneProjectionTests, CurveAndLightPreviewsPublishOneCompleteRetainedGeneration) {
  AssetRegistry assets;CurveAsset curve;curve.name="Preview Curve";
  const std::array<modeling::CurvePointSpec,2> points{{{.position={0,0,0}},{.position={2,0,0}}}};
  auto document=modeling::CurveDocument::make_bezier(points);ASSERT_TRUE(document.ok());curve.document=std::make_shared<const modeling::CurveDocument>(std::move(document).value());
  const auto curveId=assets.add_curve(std::move(curve));CurveSolidGeometryCache solids;
  ToolPreviewScene input;input.revision=7;ToolPreviewCurve tube;tube.source_entity=601;tube.curve=curveId;tube.solidify=true;tube.solidify_radius=.2f;tube.solidify_resolution=8;tube.layout_key=91;tube.instance_transforms.resize(1);tube.instance_transforms[0].values[12]=3;input.curves.push_back(tube);
  ToolPreviewLight lamp;lamp.source_entity=701;lamp.light.type=LightType::Point;lamp.light.intensity=12;lamp.suppress_source=true;lamp.layout_key=81;lamp.instance_transforms.resize(1);lamp.instance_transforms[0].values[12]=5;input.lights.push_back(lamp);
  auto preview=std::make_shared<RenderToolPreviewSceneSnapshot>(build_render_tool_preview_scene_snapshot(std::move(input),assets,nullptr,nullptr,&solids));ASSERT_TRUE(preview->complete);ASSERT_EQ(preview->meshes.size(),1u);ASSERT_EQ(preview->curves.size(),1u);
  auto scene=make_scene(make_quad_geometry());scene->meshes.front().mesh_component.cast_shadows=false;scene->lights.push_back({701,LightComponent{},QMat4{}});scene->lights.push_back({702,LightComponent{},QMat4{}});
  CrimsonSceneProjectionCache cache({});std::shared_ptr<const CrimsonSceneProjection> projected;std::string error;ASSERT_TRUE(cache.prepare(scene,preview,1,projected,error))<<error;
  ASSERT_EQ(projected->meshes.size(),2u);ASSERT_EQ(projected->lights.size(),2u);EXPECT_EQ(projected->lights[0].entity_id,702u);EXPECT_NE(projected->lights[1].entity_id,701u);EXPECT_FLOAT_EQ(projected->lights[1].world_transform.values[12],5);ASSERT_EQ(projected->instance_cast_shadows.size(),2u);EXPECT_FALSE(projected->instance_cast_shadows[0].second);EXPECT_TRUE(projected->instance_cast_shadows[1].second);
  const auto transientId=projected->lights[1].entity_id;const auto stats=cache.stats();auto repeated=std::make_shared<RenderToolPreviewSceneSnapshot>(*preview);EXPECT_TRUE(crimson_tool_preview_snapshots_equal(*preview,*repeated));ASSERT_TRUE(cache.prepare(scene,repeated,2,projected,error))<<error;EXPECT_EQ(projected->lights[1].entity_id,transientId);EXPECT_EQ(cache.stats().position_packs,stats.position_packs);EXPECT_EQ(cache.stats().attribute_packs,stats.attribute_packs);EXPECT_EQ(cache.stats().index_packs,stats.index_packs);
  repeated->lights.front().light.intensity=17;EXPECT_FALSE(crimson_tool_preview_snapshots_equal(*preview,*repeated));repeated->lights.front().instance_transforms[0].values[12]=8;ASSERT_TRUE(cache.prepare(scene,repeated,3,projected,error))<<error;EXPECT_EQ(projected->lights[1].entity_id,transientId);EXPECT_FLOAT_EQ(projected->lights[1].world_transform.values[12],8);
  const auto accepted=projected;auto invalid=std::make_shared<RenderToolPreviewSceneSnapshot>(*repeated);invalid->lights.front().instance_transforms[0].values[0]=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(cache.prepare(scene,invalid,4,projected,error));EXPECT_EQ(projected,accepted);EXPECT_FALSE(error.empty());
  invalid=std::make_shared<RenderToolPreviewSceneSnapshot>(*repeated);invalid->curves.front().sample_payload.reset();EXPECT_FALSE(cache.prepare(scene,invalid,5,projected,error));EXPECT_EQ(projected,accepted);
}

} // namespace
TEST(CrimsonContextPresentationTests, StyleChangesAndContextResetInvalidateOnlyTheirView){
  auto host=create_crimson_render_host();
  auto first=host->create_view(1,ViewportRenderSurface::Texture),second=host->create_view(2,ViewportRenderSurface::Texture);
  auto settings=ViewportSettings::defaults();settings.set_float_value(ViewportFloatSettingId::InstanceEditContextStrength,.8f);
  settings.set_float_value(ViewportFloatSettingId::InstanceEditBackgroundBlend,.4f);
  SceneSettings scene;first->apply_scene_settings(scene,false,settings);second->apply_scene_settings(scene,false,settings);
  const auto first_before=first->scene_settings_apply_count(),second_before=second->scene_settings_apply_count();
  first->set_instance_edit_context_active(true,settings);
  EXPECT_GT(first->scene_settings_apply_count(),first_before);EXPECT_EQ(second->scene_settings_apply_count(),second_before);
  const auto active=first->scene_settings_apply_count();first->apply_scene_settings(scene,false,settings);EXPECT_EQ(first->scene_settings_apply_count(),active);
  settings.set_float_value(ViewportFloatSettingId::InstanceEditContextDesaturation,.37f);
  first->apply_scene_settings(scene,false,settings);EXPECT_GT(first->scene_settings_apply_count(),active);
  const auto changed=first->scene_settings_apply_count();first->set_instance_edit_context_active(false,settings);EXPECT_GT(first->scene_settings_apply_count(),changed);
  EXPECT_EQ(second->scene_settings_apply_count(),second_before);
}

} // namespace quader
