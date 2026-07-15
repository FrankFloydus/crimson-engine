////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/texture_asset.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/modeling/linear_array_layout.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace quader {
namespace {

void expect_vec3_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001f);
  EXPECT_NEAR(actual.y, expected.y, 0.0001f);
  EXPECT_NEAR(actual.z, expected.z, 0.0001f);
}

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t component = 0; component < actual.values.size();
       ++component) {
    EXPECT_NEAR(actual.values[component], expected.values[component], 0.0001f);
  }
}

QMat4 effective_instance_member_world(const SceneDocument &scene,
                                      EntityId occurrence,
                                      EntityId definition,
                                      EntityId member) {
  const std::optional<QMat4> definition_inverse =
      try_scene_matrix_inverse(scene.world_transform_matrix(definition));
  EXPECT_TRUE(definition_inverse.has_value());
  if (!definition_inverse.has_value()) {
    return {};
  }
  return scene_transform_multiply(
      scene.world_transform_matrix(occurrence),
      scene_transform_multiply(*definition_inverse,
                               scene.world_transform_matrix(member)));
}

TEST(LinearArrayLayoutTests, PreservesOrientationAndIncludesSharedEndpoint) {
  const QMat4 source_world = compose_transform(QTransform{
      .position = {4.0f, 3.0f, 2.0f},
      .scale = {2.0f, 1.0f, 0.5f},
  });
  const LinearArrayLayoutResult layout = build_linear_array_layout({
      .source_world_transform = source_world,
      .span_world = {6.0f, -3.0f, 9.0f},
      .count = 4u,
  });
  ASSERT_TRUE(layout.ok) << layout.message;
  ASSERT_EQ(layout.generated_world_transforms.size(), 3u);
  expect_vec3_near(transform_point(layout.generated_world_transforms[0], {}),
                   {6.0f, 2.0f, 5.0f});
  expect_vec3_near(transform_point(layout.generated_world_transforms[2], {}),
                   {10.0f, 0.0f, 11.0f});
  expect_vec3_near(
      transform_vector(layout.generated_world_transforms[1],
                       {1.0f, 0.0f, 0.0f}),
      transform_vector(source_world, {1.0f, 0.0f, 0.0f}));
}

TEST(LinearArrayToolServiceTests,
     MultiSourcePreviewCommitAndWheelAdjustmentUseIdenticalMatrices) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> primary =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Primary Source");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Second Source");
  ASSERT_TRUE(primary.ok()) << primary.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.value().entity,
                                 {.position = {10.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {primary.value().entity, second.value().entity},
                      primary.value().entity)
                  .ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());

  LinearArrayToolService &tool = session.linear_array();
  const LinearArrayHandleState handle = tool.handle_state();
  ASSERT_TRUE(handle.visible);
  expect_vec3_near(handle.source_world, {});
  expect_vec3_near(handle.endpoint_world, {36.0f, 0.0f, 0.0f});
  ASSERT_TRUE(tool.adjust_count(2).ok);
  EXPECT_EQ(tool.count(), 6u);
  ASSERT_TRUE(tool.adjust_count(-100).ok);
  EXPECT_EQ(tool.count(), 2u);
  EXPECT_FALSE(tool.adjust_count(-1).changed);
  ASSERT_TRUE(tool.set_count(3u).ok);

  const LinearArrayPreviewState &preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), 2u);
  EXPECT_EQ(preview.sources[0].source_entity, second.value().entity);
  EXPECT_EQ(preview.sources[1].source_entity, primary.value().entity);
  ASSERT_EQ(preview.sources[0].generated_world_transforms.size(), 2u);
  ASSERT_EQ(preview.sources[1].generated_world_transforms.size(), 2u);
  const QMat4 *const cached_transform_storage =
      preview.sources[0].generated_world_transforms.data();
  const LinearArrayPreviewState &unchanged_preview = tool.preview_state();
  EXPECT_EQ(&unchanged_preview, &preview);
  EXPECT_EQ(unchanged_preview.sources[0].generated_world_transforms.data(),
            cached_transform_storage);
  std::vector<QMat4> preview_world;
  for (const LinearArrayPreviewSource &source : preview.sources) {
    preview_world.insert(preview_world.end(),
                         source.generated_world_transforms.begin(),
                         source.generated_world_transforms.end());
  }

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  EXPECT_FALSE(tool.has_session());
  EXPECT_EQ(editor.query().history().undo_label, "Linear Array");

  std::vector<EntityId> generated;
  for (const EntityId entity : session.scene().entities()) {
    if (entity != primary.value().entity && entity != second.value().entity &&
        session.scene().modeling_object(entity) != nullptr) {
      generated.push_back(entity);
    }
  }
  std::sort(generated.begin(), generated.end());
  ASSERT_EQ(generated.size(), preview_world.size());
  for (std::size_t index = 0; index < generated.size(); ++index) {
    const QMat4 committed_world =
        session.scene().world_transform_matrix(generated[index]);
    for (std::size_t component = 0;
         component < committed_world.values.size(); ++component) {
      EXPECT_NEAR(committed_world.values[component],
                  preview_world[index].values[component], 0.0001f);
    }
  }
  EXPECT_EQ(session.scene().selected_entities().size(), 6u);
  EXPECT_EQ(session.scene().primary_selection(), generated.back());
}

TEST(LinearArrayToolServiceTests,
     AnySourceWorldTransformChangeInvalidatesTheCapturedPreview) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Stale Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  ASSERT_TRUE(session.linear_array().preview_state().active);

  ASSERT_TRUE(editor.scene()
                  .set_transform(source.value().entity,
                                 {.position = {0.000001f, 0.0f, 0.0f}})
                  .ok());
  const CommandResult refreshed = session.linear_array().refresh();
  EXPECT_TRUE(refreshed.ok);
  EXPECT_TRUE(refreshed.changed);
  EXPECT_FALSE(session.linear_array().has_session());
  EXPECT_FALSE(session.linear_array().preview_state().active);
}

TEST(LinearArrayToolServiceTests,
     UnchangedNativeArrayPreviewDoesNotResubmitRenderExtraction) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Cached Preview");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());

  SceneRenderBridge bridge;
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  const std::uint64_t initial_submission =
      bridge.tool_preview_submission_revision();
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  EXPECT_EQ(bridge.tool_preview_submission_revision(), initial_submission);

  TextureAsset texture;
  texture.name = "Array Preview Asset Revision";
  texture.width = 1u;
  texture.height = 1u;
  texture.generated = true;
  ASSERT_NE(session.assets().add_texture(std::move(texture)),
            kInvalidAssetId);
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  EXPECT_GT(bridge.tool_preview_submission_revision(), initial_submission);
  const std::uint64_t asset_submission =
      bridge.tool_preview_submission_revision();

  ASSERT_TRUE(session.linear_array().set_count(5u).ok);
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  EXPECT_GT(bridge.tool_preview_submission_revision(), asset_submission);
  const std::uint64_t changed_submission =
      bridge.tool_preview_submission_revision();

  ASSERT_TRUE(session.linear_array().cancel().ok);
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  EXPECT_GT(bridge.tool_preview_submission_revision(), changed_submission);
}

TEST(LinearArrayToolServiceTests,
     CancelAndReactivateCannotReuseAnOldRenderPreviewRevision) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear First Session");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Second Session");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity}, first.value().entity)
                  .ok());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  LinearArrayToolService &tool = session.linear_array();
  SceneRenderBridge bridge;
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  const std::uint64_t first_submission =
      bridge.tool_preview_submission_revision();

  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(editor.selection()
                  .select_objects({second.value().entity},
                                  second.value().entity)
                  .ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                               bridge)
                  .ok());
  EXPECT_GT(bridge.tool_preview_submission_revision(), first_submission);
  const LinearArrayPreviewState &preview = tool.preview_state();
  ASSERT_EQ(preview.sources.size(), 1u);
  EXPECT_EQ(preview.sources.front().source_entity, second.value().entity);
}

TEST(LinearArrayToolServiceTests,
     LinkedInstanceOccurrencesPreviewMembersAndUndoAsOneHistoryItem) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderEntityRef> parent =
      editor.scene().create_entity(QuaderCreateEntityArgs{
          .name = "Linear Instance Parent",
          .kind = QuaderEntityKind::Empty,
      });
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Linear Instance Member A");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-0.5f, -0.5f, -0.5f},
                       .max = {0.5f, 0.5f, 0.5f}},
          "Linear Instance Member B");
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene()
                  .set_transform(second.value().entity,
                                 {.position = {4.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects(
                      {first.value().entity, second.value().entity},
                      first.value().entity)
                  .ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  ASSERT_EQ(session.scene().selected_entities().size(), 1u);
  const EntityId occurrence = session.scene().selected_entities().front();
  const InstanceOccurrenceComponent *source_component =
      session.scene().instance_occurrence(occurrence);
  ASSERT_NE(source_component, nullptr);
  const EntityId definition = source_component->definition;
  const std::vector<EntityId> members = session.scene().children(definition);
  ASSERT_EQ(members.size(), 2u);

  ASSERT_TRUE(editor.scene()
                  .set_transform(parent.value().id,
                                 {.position = {7.0f, 1.0f, -2.0f}})
                  .ok());
  ASSERT_TRUE(editor.scene().reparent(occurrence, parent.value().id, false).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(occurrence,
                                 {.position = {3.0f, 2.0f, 1.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence}, occurrence)
                  .ok());

  LinearArrayToolService &tool = session.linear_array();
  ASSERT_TRUE(tool.activate(1.0).ok);
  ASSERT_TRUE(tool.set_count(3u).ok);
  const LinearArrayHandleState handle = tool.handle_state();
  ASSERT_TRUE(handle.visible);
  ASSERT_TRUE(tool.set_endpoint(handle.source_world + QVec3{8.0f, 0.0f, 0.0f},
                                false, 1.0)
                  .ok);
  const LinearArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), members.size());
  for (const LinearArrayPreviewSource &source : preview.sources) {
    EXPECT_EQ(source.source_entity, occurrence);
    EXPECT_EQ(source.generated_world_transforms.size(), 2u);
  }

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Linear Array");

  const std::vector<EntityId> selected = session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  std::vector<EntityId> copies;
  for (const EntityId entity : selected) {
    if (entity != occurrence) {
      copies.push_back(entity);
    }
  }
  ASSERT_EQ(copies.size(), 2u);
  ASSERT_NE(session.scene().name(occurrence), nullptr);
  const std::string source_name = session.scene().name(occurrence)->name;
  for (std::size_t copy_index = 0; copy_index < copies.size(); ++copy_index) {
    const EntityId copy = copies[copy_index];
    const InstanceOccurrenceComponent *component =
        session.scene().instance_occurrence(copy);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->definition, definition);
    EXPECT_EQ(session.scene().parent(copy), parent.value().id);
    ASSERT_NE(session.scene().visibility(copy), nullptr);
    EXPECT_TRUE(session.scene().visibility(copy)->visible);
    ASSERT_NE(session.scene().lock(copy), nullptr);
    EXPECT_FALSE(session.scene().lock(copy)->locked);
    ASSERT_NE(session.scene().name(copy), nullptr);
    EXPECT_NE(session.scene().name(copy)->name.find(source_name),
              std::string::npos);

    for (const EntityId member : members) {
      const MeshComponent *member_mesh = session.scene().mesh(member);
      ASSERT_NE(member_mesh, nullptr);
      const auto source = std::find_if(
          preview.sources.begin(), preview.sources.end(),
          [member_mesh](const LinearArrayPreviewSource &candidate) {
            return candidate.mesh == member_mesh->mesh;
          });
      ASSERT_NE(source, preview.sources.end());
      expect_matrix_near(
          effective_instance_member_world(session.scene(), copy, definition,
                                          member),
          source->generated_world_transforms[copy_index]);
    }
  }

  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_EQ(session.scene().selected_entities(),
            std::vector<EntityId>{occurrence});
  for (const EntityId copy : copies) {
    EXPECT_FALSE(session.scene().exists(copy));
  }
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_EQ(session.scene().selected_entities().size(), 3u);
  for (const EntityId copy : copies) {
    ASSERT_TRUE(session.scene().exists(copy));
    ASSERT_NE(session.scene().instance_occurrence(copy), nullptr);
    EXPECT_EQ(session.scene().instance_occurrence(copy)->definition,
              definition);
  }
}

TEST(RadialArrayToolServiceTests,
     LinkedInstanceOccurrencesUsePreviewMatricesAndStableRedoIdentities) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> member =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Radial Instance Member");
  ASSERT_TRUE(member.ok()) << member.error_message();
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  ASSERT_EQ(session.scene().selected_entities().size(), 1u);
  const EntityId occurrence = session.scene().selected_entities().front();
  const InstanceOccurrenceComponent *source_component =
      session.scene().instance_occurrence(occurrence);
  ASSERT_NE(source_component, nullptr);
  const EntityId definition = source_component->definition;
  const std::vector<EntityId> members = session.scene().children(definition);
  ASSERT_EQ(members.size(), 1u);
  ASSERT_TRUE(editor.scene()
                  .set_transform(occurrence,
                                 {.position = {4.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence}, occurrence)
                  .ok());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::RadialArray).ok());
  RadialArrayToolService &tool = session.radial_array();
  ASSERT_TRUE(tool.update_center_candidate({}, {0.0f, 1.0f, 0.0f}, 1.0).ok);
  ASSERT_TRUE(tool.set_degrees(180.0).ok);
  ASSERT_TRUE(tool.set_copies(3u).ok);
  ASSERT_TRUE(tool.lock_center().ok);
  const RadialArrayPreviewState preview = tool.preview_state();
  ASSERT_TRUE(preview.active);
  ASSERT_EQ(preview.sources.size(), 1u);
  ASSERT_EQ(preview.sources.front().generated_world_transforms.size(), 2u);

  const QuaderApiVoidResult committed =
      editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Radial Array");

  const std::vector<EntityId> selected = session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  std::vector<EntityId> copies;
  for (const EntityId entity : selected) {
    if (entity != occurrence) {
      copies.push_back(entity);
    }
  }
  ASSERT_EQ(copies.size(), 2u);
  for (std::size_t index = 0; index < copies.size(); ++index) {
    const InstanceOccurrenceComponent *component =
        session.scene().instance_occurrence(copies[index]);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->definition, definition);
    expect_matrix_near(
        effective_instance_member_world(session.scene(), copies[index],
                                        definition, members.front()),
        preview.sources.front().generated_world_transforms[index]);
  }

  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_EQ(session.scene().selected_entities(),
            std::vector<EntityId>{occurrence});
  for (const EntityId copy : copies) {
    EXPECT_FALSE(session.scene().exists(copy));
  }
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_EQ(session.scene().selected_entities().size(), 3u);
  for (const EntityId copy : copies) {
    ASSERT_TRUE(session.scene().exists(copy));
    ASSERT_NE(session.scene().instance_occurrence(copy), nullptr);
    EXPECT_EQ(session.scene().instance_occurrence(copy)->definition,
              definition);
  }
}

TEST(LinearArrayToolServiceTests,
     MixedOrdinaryMeshesAndOccurrencesAreRejectedExplicitly) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> member =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Mixed Array Instance Member");
  ASSERT_TRUE(member.ok()) << member.error_message();
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  const EntityId occurrence = session.scene().selected_entities().front();
  const QuaderApiResult<QuaderModelingObjectRef> ordinary =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Mixed Array Ordinary Mesh");
  ASSERT_TRUE(ordinary.ok()) << ordinary.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({occurrence, ordinary.value().entity},
                                  occurrence)
                  .ok());

  EXPECT_FALSE(session.linear_array().can_activate());
  EXPECT_FALSE(session.radial_array().can_activate());
  const QuaderApiVoidResult activated =
      editor.tools().activate(ViewportToolId::LinearArray);
  EXPECT_FALSE(activated.ok());
}

} // namespace
} // namespace quader
