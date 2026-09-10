////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/viewport/viewport_modeling_tool_commit_adapter.hpp"
#include "math/glm_bridge.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "support/modeling_test_events.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kTestPositionEpsilon = 0.0001f;

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kTestPositionEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kTestPositionEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kTestPositionEpsilon);
}

void expect_bounds_near(const quader::SceneBounds &actual,
                        const quader::SceneBounds &expected) {
  expect_vec3_near(actual.min, expected.min);
  expect_vec3_near(actual.max, expected.max);
}

quader::SceneBounds preview_world_bounds(
    const quader::RenderToolPreviewMeshRecord &record) {
  const auto bounds = quader::mesh_world_bounds(record.mesh, record.transform);
  EXPECT_TRUE(bounds.has_value());
  return bounds.value_or(quader::SceneBounds{});
}

void expect_mesh_positions_and_normals_near(
    const quader::MeshGeometryPayload &actual,
    const quader::MeshGeometryPayload &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  EXPECT_EQ(actual.indices, expected.indices);
  for (std::size_t index = 0u; index < actual.vertices.size(); ++index) {
    expect_vec3_near(actual.vertices[index].position,
                     expected.vertices[index].position);
    expect_vec3_near(actual.vertices[index].normal,
                     expected.vertices[index].normal);
  }
}

void expect_inward_box_geometry(const quader::MeshAsset &mesh) {
  const auto bounds = quader::local_mesh_bounds(mesh);
  ASSERT_TRUE(bounds.has_value());
  const quader::QVec3 center = (bounds->min + bounds->max) * 0.5f;
  const auto &geometry = mesh.geometry();
  ASSERT_FALSE(geometry.vertices.empty());
  ASSERT_FALSE(geometry.indices.empty());
  ASSERT_EQ(geometry.indices.size() % 3u, 0u);
  for (const auto &vertex : geometry.vertices) {
    EXPECT_LT(quader::dot(vertex.normal, vertex.position - center),
              -kTestPositionEpsilon);
  }
  for (std::size_t offset = 0u; offset < geometry.indices.size(); offset += 3u) {
    ASSERT_LT(geometry.indices[offset], geometry.vertices.size());
    ASSERT_LT(geometry.indices[offset + 1u], geometry.vertices.size());
    ASSERT_LT(geometry.indices[offset + 2u], geometry.vertices.size());
    const auto &first = geometry.vertices[geometry.indices[offset]];
    const auto &second = geometry.vertices[geometry.indices[offset + 1u]];
    const auto &third = geometry.vertices[geometry.indices[offset + 2u]];
    const quader::QVec3 normal = quader::cross(second.position - first.position,
                                             third.position - first.position);
    const quader::QVec3 triangle_center =
        (first.position + second.position + third.position) / 3.0f;
    EXPECT_LT(quader::dot(normal, triangle_center - center),
              -kTestPositionEpsilon);
    EXPECT_GT(quader::dot(normal, first.normal + second.normal + third.normal),
              kTestPositionEpsilon);
  }
}

quader::QuaderModelingObjectRef
create_selected_box(quader::QuaderEditorApi &editor, std::string name) {
  const auto created = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  if (!created.ok()) {
    return {};
  }
  EXPECT_TRUE(editor.selection().select_object(created.value()).ok());
  return created.value();
}

quader::ModelingToolRayEvent mirror_plane_event(float x, float z = 0.0f,
                                                bool dragging = false) {
  auto event = quader::test::drawn_plane_event(x, dragging);
  event.ray.origin = {x, 20.0f, z};
  event.camera.origin = {0.0f, 20.0f, 10.0f};
  event.camera.viewport_size = {800.0f, 600.0f};
  return event;
}

/// Exercises render extraction and its cache gates without claiming GPU
/// publication; the injected sink retains only successfully published records.
struct RenderPreviewTestHarness final {
  quader::SceneSession session;
  quader::QuaderEditorApi editor{
      quader::QuaderApiSessionInternalAccess::from_scene_session(session)};
  quader::SceneRenderBridge bridge;
  std::vector<quader::RenderToolPreviewMeshRecord> published;
  std::size_t publication_attempts = 0u;
  bool reject_next_publication = false;
  const quader::QuaderRenderApiRuntimeAccess::ToolPreviewPublisher publisher =
      [this](std::vector<quader::RenderToolPreviewMeshRecord> records) {
        ++publication_attempts;
        if (std::exchange(reject_next_publication, false)) {
          return quader::ToolPreviewSubmissionResult::Failed;
        }
        published = std::move(records);
        return quader::ToolPreviewSubmissionResult::Published;
      };

  quader::QuaderApiVoidResult sync() {
    return quader::QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, publisher);
  }
};

} // namespace

TEST(ToolPreviewSubmissionTests,
     RadialCandidateAndLockedSettingsPublishWithoutMutatingScene) {
  RenderPreviewTestHarness harness;
  auto &editor = harness.editor;
  const auto source = create_selected_box(editor, "Live radial preview source");
  ASSERT_TRUE(editor.scene().set_transform(source.entity, {.position = {6, 0, 0}}).ok());
  harness.session.history().clear();
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(harness.published.empty());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::RadialArray).ok());
  auto &tool = harness.session.radial_array();
  ASSERT_TRUE(tool.set_copies(3).ok);
  ASSERT_TRUE(harness.sync().ok());
  const auto before_candidate = harness.publication_attempts;

  ASSERT_TRUE(tool.update_center_candidate({}, {0, 1, 0}, 1.0).ok);
  ASSERT_FALSE(tool.center_locked());
  ASSERT_TRUE(tool.preview_state().active);
  ASSERT_EQ(tool.preview_state().sources.size(), 1u);
  const auto first_revision = tool.preview_state().revision;
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, before_candidate + 1u);
  ASSERT_EQ(harness.published.size(), 1u);
  EXPECT_EQ(harness.published.front().source_entity, source.entity);
  const auto first_transforms = harness.published.front().instance_transforms;
  ASSERT_EQ(first_transforms.size(), 2u);
  for (int frame = 0; frame < 16; ++frame) {
    EXPECT_FALSE(tool.update_center_candidate({}, {0, 1, 0}, 1.0).changed);
    ASSERT_TRUE(harness.sync().ok());
    EXPECT_EQ(tool.preview_state().revision, first_revision);
  }
  EXPECT_EQ(harness.publication_attempts, before_candidate + 1u);

  ASSERT_TRUE(tool.update_center_candidate({0, 0, 2}, {0, 1, 0}, 1.0).changed);
  const auto moved_revision = tool.preview_state().revision;
  EXPECT_GT(moved_revision, first_revision);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, before_candidate + 2u);
  ASSERT_EQ(harness.published.size(), 1u);
  ASSERT_EQ(harness.published.front().instance_transforms.size(), first_transforms.size());
  EXPECT_NE(harness.published.front().instance_transforms.front().values,
            first_transforms.front().values);
  EXPECT_FALSE(tool.center_locked());
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, before_candidate + 2u);
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);

  ASSERT_TRUE(tool.lock_center().ok);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(tool.active());
  EXPECT_TRUE(tool.center_locked());
  ASSERT_TRUE(tool.preview_state().active);
  const auto locked_revision = tool.preview_state().revision;
  const auto locked_publications = harness.publication_attempts;
  ASSERT_TRUE(tool.set_copies(4).ok);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_GT(tool.preview_state().revision, locked_revision);
  EXPECT_EQ(harness.publication_attempts, locked_publications + 1u);
  ASSERT_EQ(harness.published.size(), 1u);
  EXPECT_EQ(harness.published.front().instance_transforms.size(), 3u);
  EXPECT_FALSE(tool.update_center_candidate({5, 0, 5}, {0, 1, 0}, 1.0).changed);
  expect_vec3_near(tool.center_state().world_position, {0, 0, 2});
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, locked_publications + 1u);
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);

  const auto before_cancel = harness.publication_attempts;
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
  EXPECT_FALSE(tool.preview_state().active);
  EXPECT_EQ(harness.publication_attempts, before_cancel + 1u);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, before_cancel + 1u);
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
}

TEST(ToolPreviewSubmissionTests,
     MirrorPreviewRenderSubmissionTracksPlaneEditsAfterIdleSync) {
  RenderPreviewTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  quader::ModelingSessionService &modeling = harness.session.modeling();
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Placed Mirror Preview Source");
  ASSERT_TRUE(editor.scene()
                  .set_transform(source.entity,
                                 {.position = {3.0f, 2.0f, 5.0f}})
                  .ok());
  const auto material = editor.assets().add_material(quader::QuaderMaterialAsset{
      .name = "Mirror Preview Material",
      .base_color = {0.2f, 0.4f, 0.8f},
  });
  ASSERT_TRUE(material.ok()) << material.error_message();
  ASSERT_TRUE(editor.assets()
                  .assign_material_to_entity(source.entity, material.value().id)
                  .ok());
  const quader::QuaderHistorySnapshot history_before = editor.query().history();
  const auto source_before = editor.query().compiled_object_mesh(source);
  ASSERT_TRUE(source_before.has_value());

  // A normal viewport has already submitted an empty preview before Mirror
  // starts; the new plane must invalidate that successful idle submission.
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(harness.published.empty());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Mirror).ok());
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(modeling.click_mirror(mirror_plane_event(0.0f)).ok);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(1.0f, 0.0f, true)).ok);
  ASSERT_TRUE(editor.query().tool_status().mirror_preview_valid);
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_EQ(harness.published.size(), 1u);
  const quader::RenderToolPreviewMeshRecord &first = harness.published.front();
  EXPECT_TRUE(first.renderable());
  EXPECT_EQ(first.source_entity, source.entity);
  EXPECT_EQ(first.visibility_entity, source.entity);
  EXPECT_EQ(first.material_id, material.value().id);
  ASSERT_TRUE(first.material.primary_material.present);
  expect_vec3_near(first.material.primary_material.document.base_color,
                   {0.2f, 0.4f, 0.8f});
  EXPECT_FALSE(first.double_sided);
  EXPECT_FALSE(first.suppress_source);
  EXPECT_TRUE(first.instance_transforms.empty());
  expect_vec3_near(quader::transform_point(first.transform, {}),
                   {3.0f, 2.0f, 5.0f});
  expect_bounds_near(preview_world_bounds(first),
                     {.min = {2.0f, 1.0f, -6.0f},
                      .max = {4.0f, 3.0f, -4.0f}});

  ASSERT_TRUE(modeling.end_mirror_drag().ok);
  const std::size_t published_attempts = harness.publication_attempts;
  const std::uint64_t published_compiles =
      modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(20.0f, 8.0f)).ok);
  ASSERT_TRUE(harness.sync().ok());
  const quader::ModelingToolPreviewWorkStats unchanged_work =
      modeling.tool_preview_work_stats();
  for (int frame = 0; frame < 4; ++frame) {
    ASSERT_TRUE(harness.sync().ok());
  }
  EXPECT_EQ(harness.publication_attempts, published_attempts);
  EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            published_compiles);
  EXPECT_EQ(modeling.tool_preview_work_stats().signature_enumeration_count,
            unchanged_work.signature_enumeration_count);
  EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
            unchanged_work.mesh_extraction_count);

  // Move the already placed plane while scene/assets stay unchanged. A failed
  // publication retains the old mesh and caches the failed attempt until a
  // source revision changes.
  ASSERT_TRUE(
      modeling.begin_mirror_line_drag(mirror_plane_event(0.5f)).ok);
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(0.5f, 2.0f, true)).ok);
  ASSERT_TRUE(modeling.end_mirror_drag().ok);
  harness.reject_next_publication = true;
  EXPECT_FALSE(harness.sync().ok());
  ASSERT_EQ(harness.published.size(), 1u);
  expect_bounds_near(preview_world_bounds(harness.published.front()),
                     {.min = {2.0f, 1.0f, -6.0f},
                      .max = {4.0f, 3.0f, -4.0f}});
  const std::size_t failed_attempts = harness.publication_attempts;
  const quader::ModelingToolPreviewWorkStats failed_work =
      modeling.tool_preview_work_stats();
  for (int frame = 0; frame < 4; ++frame) {
    EXPECT_FALSE(harness.sync().ok());
  }
  EXPECT_EQ(harness.publication_attempts, failed_attempts);
  EXPECT_EQ(modeling.tool_preview_work_stats().signature_enumeration_count,
            failed_work.signature_enumeration_count);
  EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
            failed_work.mesh_extraction_count);

  const std::uint64_t failed_source_revision =
      modeling.tool_preview_source_revision();
  ASSERT_TRUE(modeling.begin_mirror_line_drag(mirror_plane_event(0.5f)).ok);
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(0.5f, 4.0f, true)).ok);
  ASSERT_TRUE(modeling.end_mirror_drag().ok);
  EXPECT_GT(modeling.tool_preview_source_revision(), failed_source_revision);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, failed_attempts + 1u);
  EXPECT_GT(modeling.tool_preview_work_stats().signature_enumeration_count,
            failed_work.signature_enumeration_count);
  EXPECT_GT(modeling.tool_preview_work_stats().mesh_extraction_count,
            failed_work.mesh_extraction_count);
  ASSERT_EQ(harness.published.size(), 1u);
  expect_bounds_near(preview_world_bounds(harness.published.front()),
                     {.min = {2.0f, 1.0f, 2.0f},
                      .max = {4.0f, 3.0f, 4.0f}});

  const std::size_t moved_attempts = harness.publication_attempts;
  const std::uint64_t moved_compiles =
      modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, moved_attempts);
  EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            moved_compiles);

  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
  const std::size_t canceled_attempts = harness.publication_attempts;
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, canceled_attempts);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Mirror).ok());
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(modeling.click_mirror(mirror_plane_event(0.0f, -2.0f)).ok);
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(1.0f, -2.0f, true)).ok);
  ASSERT_TRUE(modeling.end_mirror_drag().ok);
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_EQ(harness.published.size(), 1u);
  EXPECT_FALSE(harness.published.front().suppress_source);
  expect_bounds_near(preview_world_bounds(harness.published.front()),
                     {.min = {2.0f, 1.0f, -10.0f},
                      .max = {4.0f, 3.0f, -8.0f}});
  const auto source_during_preview = editor.query().entity(source.entity);
  ASSERT_TRUE(source_during_preview.has_value());
  EXPECT_TRUE(source_during_preview->visible);
  const auto source_world = quader::entity_world_bounds(
      harness.session.scene(), harness.session.assets(), source.entity);
  ASSERT_TRUE(source_world.has_value());
  expect_bounds_near(*source_world, {.min = {2.0f, 1.0f, 4.0f},
                                    .max = {4.0f, 3.0f, 6.0f}});
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  const auto source_after = editor.query().compiled_object_mesh(source);
  ASSERT_TRUE(source_after.has_value());
  ASSERT_EQ(source_after->vertices.size(), source_before->vertices.size());
  EXPECT_EQ(source_after->indices, source_before->indices);
  for (std::size_t index = 0; index < source_before->vertices.size(); ++index) {
    expect_vec3_near(source_after->vertices[index].position,
                     source_before->vertices[index].position);
  }
  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
}

TEST(ToolPreviewSubmissionTests,
     InvertedMirrorPreservesWindingNormalsAndLiveSourceCullingThroughCommit) {
  for (const bool initial_double_sided : {false, true}) {
    SCOPED_TRACE(::testing::Message()
                 << "initial_double_sided=" << initial_double_sided);
    RenderPreviewTestHarness harness;
    quader::QuaderEditorApi &editor = harness.editor;
    quader::ModelingSessionService &modeling = harness.session.modeling();
    const quader::QuaderModelingObjectRef source =
        create_selected_box(editor, "Inverted Mirror Preview Source");
    const auto inverted =
        editor.commands().execute(quader::quader_command::kModelingInvertMesh);
    ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    ASSERT_TRUE(inverted.changed());
    ASSERT_TRUE(editor.scene()
                    .set_transform(source.entity,
                                   {.position = {3.0f, 2.0f, 5.0f}})
                    .ok());
    const auto *source_binding = harness.session.scene().mesh(source.entity);
    ASSERT_NE(source_binding, nullptr);
    const auto *inverted_source =
        harness.session.assets().mesh(source_binding->mesh);
    ASSERT_NE(inverted_source, nullptr);
    expect_inward_box_geometry(*inverted_source);
    const quader::MeshGeometryPayload source_geometry =
        inverted_source->geometry();

    // Mesh presentation metadata has no public editor setter. Keep the
    // integration fixture on the existing undoable document-command owner.
    quader::MeshComponent presentation = *source_binding;
    presentation.double_sided = initial_double_sided;
    const auto configured = harness.session.document_commands().set_mesh_component(
        source.entity, presentation);
    ASSERT_TRUE(configured.ok) << configured.message;
    ASSERT_TRUE(harness.sync().ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Mirror).ok());
    ASSERT_TRUE(modeling.click_mirror(mirror_plane_event(0.0f)).ok);
    ASSERT_TRUE(
        modeling.update_mirror_hover(mirror_plane_event(1.0f, 0.0f, true)).ok);
    ASSERT_TRUE(modeling.end_mirror_drag().ok);
    ASSERT_TRUE(harness.sync().ok());
    ASSERT_EQ(harness.published.size(), 1u);
    EXPECT_EQ(harness.published.front().double_sided, initial_double_sided);
    EXPECT_FALSE(harness.published.front().suppress_source);
    expect_inward_box_geometry(harness.published.front().mesh);
    expect_bounds_near(preview_world_bounds(harness.published.front()),
                       {.min = {2.0f, 1.0f, -6.0f},
                        .max = {4.0f, 3.0f, -4.0f}});
    const quader::MeshGeometryPayload preview_geometry =
        harness.published.front().mesh.geometry();

    const std::size_t previous_attempts = harness.publication_attempts;
    const std::uint64_t previous_render_revision =
        harness.session.scene().render_component_revision();
    presentation.double_sided = !initial_double_sided;
    const auto changed = harness.session.document_commands().set_mesh_component(
        source.entity, presentation);
    ASSERT_TRUE(changed.ok) << changed.message;
    ASSERT_TRUE(changed.changed);
    EXPECT_GT(harness.session.scene().render_component_revision(),
              previous_render_revision);
    ASSERT_TRUE(harness.sync().ok());
    EXPECT_GT(harness.publication_attempts, previous_attempts);
    ASSERT_EQ(harness.published.size(), 1u);
    EXPECT_EQ(harness.published.front().double_sided, presentation.double_sided);
    expect_mesh_positions_and_normals_near(
        harness.published.front().mesh.geometry(), preview_geometry);
    const quader::RenderToolPreviewMeshRecord saved_preview =
        harness.published.front();
    const std::size_t changed_attempts = harness.publication_attempts;
    const std::uint64_t changed_compiles =
        modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
    ASSERT_TRUE(harness.sync().ok());
    EXPECT_EQ(harness.publication_attempts, changed_attempts);
    EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
              changed_compiles);

    quader::ViewportModelingToolCommitAdapter tool_port(editor.tools());
    const auto committed =
        tool_port.finalize_drawn_plane_tool(quader::ModelingViewportTool::Mirror);
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.changed);
    ASSERT_TRUE(harness.sync().ok());
    EXPECT_TRUE(harness.published.empty());
    const auto objects = editor.query().modeling_objects();
    ASSERT_EQ(objects.size(), 2u);
    const auto reflected = std::find_if(
        objects.begin(), objects.end(), [&](const auto &object) {
          return object.entity != source.entity;
        });
    ASSERT_NE(reflected, objects.end());
    const auto *committed_binding =
        harness.session.scene().mesh(reflected->entity);
    ASSERT_NE(committed_binding, nullptr);
    EXPECT_EQ(committed_binding->double_sided, saved_preview.double_sided);
    quader::MeshComponent expected_duplicate = presentation;
    expected_duplicate.mesh = committed_binding->mesh;
    EXPECT_EQ(*committed_binding, expected_duplicate);
    const auto *committed_mesh =
        harness.session.assets().mesh(committed_binding->mesh);
    ASSERT_NE(committed_mesh, nullptr);
    expect_inward_box_geometry(*committed_mesh);
    expect_mesh_positions_and_normals_near(committed_mesh->geometry(),
                                           saved_preview.mesh.geometry());

    const auto *source_after = harness.session.scene().mesh(source.entity);
    ASSERT_NE(source_after, nullptr);
    EXPECT_EQ(*source_after, presentation);
    const auto *source_after_mesh =
        harness.session.assets().mesh(source_after->mesh);
    ASSERT_NE(source_after_mesh, nullptr);
    expect_inward_box_geometry(*source_after_mesh);
    expect_mesh_positions_and_normals_near(source_after_mesh->geometry(),
                                           source_geometry);
  }
}

TEST(ToolPreviewSubmissionTests,
     MirrorPreviewInInstanceEditMatchesCommittedOccurrenceGeometry) {
  RenderPreviewTestHarness harness;
  quader::QuaderEditorApi &editor = harness.editor;
  quader::ModelingSessionService &modeling = harness.session.modeling();
  const quader::QuaderModelingObjectRef source =
      create_selected_box(editor, "Instance Mirror Source");
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  const quader::EntityId definition = instance.value().definition.root.id;
  ASSERT_TRUE(editor.scene().rename(occurrence, std::string(512u, 'M')).ok());
  ASSERT_TRUE(editor.scene()
                  .set_transform(occurrence,
                                 {.position = {3.0f, 2.0f, 8.0f}})
                  .ok());
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.selection().select_object(source).ok());
  const auto source_before = editor.query().compiled_object_mesh(source);
  const auto source_world_before =
      harness.session.instances().effective_member_world_transform(
          source.entity, occurrence);
  ASSERT_TRUE(source_before.has_value());
  ASSERT_TRUE(source_world_before.has_value());

  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Mirror).ok());
  ASSERT_TRUE(modeling.click_mirror(mirror_plane_event(0.0f)).ok);
  ASSERT_TRUE(
      modeling.update_mirror_hover(mirror_plane_event(1.0f, 0.0f, true)).ok);
  ASSERT_TRUE(modeling.end_mirror_drag().ok);
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_EQ(harness.published.size(), 1u);
  const quader::RenderToolPreviewMeshRecord preview = harness.published.front();
  EXPECT_EQ(preview.source_entity, source.entity);
  EXPECT_EQ(preview.visibility_entity, occurrence);
  EXPECT_FALSE(preview.suppress_source);
  expect_bounds_near(preview_world_bounds(preview),
                     {.min = {2.0f, 1.0f, -9.0f},
                      .max = {4.0f, 3.0f, -7.0f}});

  const std::size_t published_attempts = harness.publication_attempts;
  const std::uint64_t published_compiles =
      modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
  const quader::ModelingToolPreviewWorkStats published_work =
      modeling.tool_preview_work_stats();
  constexpr int kUnchangedFrames = 50000;
  const auto sync_begin = std::chrono::steady_clock::now();
  for (int frame = 0; frame < kUnchangedFrames; ++frame) {
    ASSERT_TRUE(harness.sync().ok());
  }
  const auto sync_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - sync_begin)
                           .count();
  // Diagnostic only: wall time is never a flaky correctness threshold.
  RecordProperty("unchanged_sync_ns_per_frame",
                 std::to_string(sync_ns / kUnchangedFrames));
  EXPECT_EQ(harness.publication_attempts, published_attempts);
  EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            published_compiles);
  EXPECT_EQ(modeling.tool_preview_work_stats().signature_enumeration_count,
            published_work.signature_enumeration_count);
  EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
            published_work.mesh_extraction_count);

  // Drawn-plane tools finalize through the viewport commit port; the public
  // operation-preview Apply route does not own Mirror's modal interaction.
  quader::ViewportModelingToolCommitAdapter tool_port(editor.tools());
  const quader::ModelingCommandResult committed =
      tool_port.finalize_drawn_plane_tool(quader::ModelingViewportTool::Mirror);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_TRUE(committed.changed);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
  EXPECT_FALSE(editor.query().tool_status().mirror_active);

  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 2u);
  const auto reflected = std::find_if(
      objects.begin(), objects.end(), [&](const auto &object) {
        return object.entity != source.entity;
      });
  ASSERT_NE(reflected, objects.end());
  const auto reflected_ref =
      editor.query().modeling_object_for_entity(reflected->entity);
  ASSERT_TRUE(reflected_ref.has_value());
  const auto reflected_entity = editor.query().entity(reflected->entity);
  ASSERT_TRUE(reflected_entity.has_value());
  EXPECT_EQ(reflected_entity->parent, definition);
  const auto reflected_mesh =
      editor.query().compiled_object_mesh(*reflected_ref);
  const auto reflected_world =
      harness.session.instances().effective_member_world_transform(
          reflected->entity, occurrence);
  ASSERT_TRUE(reflected_mesh.has_value());
  ASSERT_TRUE(reflected_world.has_value());
  EXPECT_EQ(reflected_mesh->indices.size(),
            preview.mesh.geometry().indices.size());
  ASSERT_EQ(reflected_mesh->vertices.size(),
            preview.mesh.geometry().vertices.size());
  for (const auto &vertex : reflected_mesh->vertices) {
    const quader::QVec3 committed_world =
        quader::transform_point(*reflected_world, vertex.position);
    EXPECT_TRUE(std::any_of(
        preview.mesh.geometry().vertices.begin(),
        preview.mesh.geometry().vertices.end(), [&](const auto &candidate) {
          const quader::QVec3 preview_world =
              quader::transform_point(preview.transform, candidate.position);
          return quader::length_squared(committed_world - preview_world) <
                 kTestPositionEpsilon * kTestPositionEpsilon;
        }));
  }

  const auto source_after = editor.query().compiled_object_mesh(source);
  const auto source_world_after =
      harness.session.instances().effective_member_world_transform(
          source.entity, occurrence);
  ASSERT_TRUE(source_after.has_value());
  ASSERT_TRUE(source_world_after.has_value());
  ASSERT_EQ(source_after->vertices.size(), source_before->vertices.size());
  EXPECT_EQ(source_after->indices, source_before->indices);
  for (std::size_t index = 0; index < source_before->vertices.size(); ++index) {
    expect_vec3_near(
        quader::transform_point(*source_world_after,
                                source_after->vertices[index].position),
        quader::transform_point(*source_world_before,
                                source_before->vertices[index].position));
  }
  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
}

TEST(ToolPreviewSubmissionTests, BoxPreviewRenderSubmissionStartsAfterIdleSync) {
  RenderPreviewTestHarness harness;
  quader::ModelingSessionService &modeling = harness.session.modeling();
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_TRUE(harness.published.empty());
  ASSERT_TRUE(harness.editor.tools()
                  .activate(quader::ViewportToolId::CreatePrimitive)
                  .ok());
  const quader::ModelingBoxToolEvent start{
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {4.0f, 4.0f, 4.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  quader::ModelingBoxToolEvent footprint_end = start;
  footprint_end.ray.origin = {6.0f, 4.0f, 7.0f};
  ASSERT_TRUE(modeling.begin_box_footprint(start).ok);
  ASSERT_TRUE(modeling.update_box_footprint(footprint_end).ok);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
  ASSERT_TRUE(modeling.begin_box_height(start).ok);
  quader::ModelingBoxToolEvent height_end = start;
  height_end.screen_position.y = 240.0f;
  const quader::ModelingBoxPreview preview =
      modeling.update_box_height(height_end);
  ASSERT_TRUE(preview.ok);
  ASSERT_TRUE(preview.valid);
  ASSERT_GT(preview.distance, 0.0);
  ASSERT_TRUE(harness.sync().ok());
  ASSERT_EQ(harness.published.size(), 1u);
  EXPECT_TRUE(harness.published.front().renderable());
  EXPECT_EQ(harness.published.front().source_entity, quader::kInvalidEntityId);
  EXPECT_FALSE(harness.published.front().suppress_source);
  expect_bounds_near(preview_world_bounds(harness.published.front()),
                     {.min = {4.0f, 0.0f, 4.0f},
                      .max = {6.0f, static_cast<float>(preview.distance), 7.0f}});
  const std::size_t height_attempts = harness.publication_attempts;
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_EQ(harness.publication_attempts, height_attempts);

  height_end.screen_position.y = 180.0f;
  const quader::ModelingBoxPreview taller =
      modeling.update_box_height(height_end);
  ASSERT_TRUE(taller.ok);
  ASSERT_GT(taller.distance, preview.distance);
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_GT(harness.publication_attempts, height_attempts);
  ASSERT_EQ(harness.published.size(), 1u);
  expect_bounds_near(preview_world_bounds(harness.published.front()),
                     {.min = {4.0f, 0.0f, 4.0f},
                      .max = {6.0f, static_cast<float>(taller.distance), 7.0f}});
  EXPECT_EQ(harness.editor.query().modeling_objects().size(), 0u);
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(harness.sync().ok());
  EXPECT_TRUE(harness.published.empty());
}
