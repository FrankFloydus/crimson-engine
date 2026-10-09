////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_texture.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace quader {
namespace {

void expect_sampling_equal(const MeshAsset &actual, const QMat4 &actual_mapping,
                           const MeshAsset &expected, const QMat4 &expected_mapping) {
  const auto actual_transform = MeshProjectionService::texture_mapping_transform(actual_mapping);
  const auto expected_transform = MeshProjectionService::texture_mapping_transform(expected_mapping);
  ASSERT_EQ(actual.geometry().vertices.size(), expected.geometry().vertices.size());
  for (std::size_t index = 0; index < actual.geometry().vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const MeshVertex &a = actual.geometry().vertices[index];
    const MeshVertex &b = expected.geometry().vertices[index];
    const QVec2 actual_uv = quader_poly::evaluate_texture_mapping(
        a.uv, a.position, a.uv_geometric_normal, a.uv_carrier, actual_transform);
    const QVec2 expected_uv = quader_poly::evaluate_texture_mapping(
        b.uv, b.position, b.uv_geometric_normal, b.uv_carrier, expected_transform);
    EXPECT_NEAR(actual_uv.x, expected_uv.x, 0.002f);
    EXPECT_NEAR(actual_uv.y, expected_uv.y, 0.002f);
  }
}

/** Exercises transient render extraction and accepted arrays through real owners. */
class UvArrayHarness final {
public:
  UvArrayHarness()
      : editor(QuaderApiSessionInternalAccess::from_scene_session(session)) {
    session.document_commands().set_texture_lock_settings_provider([this] { return locks; });
    session.modeling().set_texture_lock_settings_provider([this] { return locks; });
  }

  EntityId source(bool instance, float x = 4.0f) {
    const auto created = editor.modeling().create_box(
        {.min = {-1, -0.5f, -0.25f}, .max = {2, 1.5f, 0.75f}}, "UV Array Source");
    EXPECT_TRUE(created.ok()) << created.error_message();
    if (!created.ok()) return kInvalidEntityId;
    const EntityId member = created.value().entity;
    const EntityId parent = session.scene().create_entity("UV Array Parent", EntityKind::Empty);
    EXPECT_TRUE(session.scene().set_transform(parent,
        {.location = {0.5f, 2, -1}, .rotation_euler = {0, 0.2f, 0},
         .scale = {1.2f, 1.2f, 1.2f}}));
    EXPECT_TRUE(session.scene().set_parent(member, parent, false));
    EXPECT_TRUE(editor.scene().set_transform(member,
        {.location = {x, 1, 2}, .rotation_euler = {0.2f, 0.35f, -0.1f},
         .scale = {1.25f, 0.75f, 1.5f}}).ok());
    QMat4 member_mapping;
    member_mapping.values[0] = 1.75f;
    member_mapping.values[5] = 0.625f;
    member_mapping.values[10] = 1.25f;
    member_mapping.values[12] = 0.375f;
    member_mapping.values[13] = -0.25f;
    EXPECT_TRUE(session.scene().set_texture_mapping(member, {member_mapping}));
    if (!instance) return member;
    EXPECT_TRUE(editor.selection().select_object(created.value()).ok());
    const auto converted = editor.modeling().create_instance_from_selection();
    EXPECT_TRUE(converted.ok()) << converted.error_message();
    const EntityId occurrence = session.scene().primary_selection();
    QMat4 occurrence_mapping;
    occurrence_mapping.values[0] = 0.75f;
    occurrence_mapping.values[5] = 1.25f;
    occurrence_mapping.values[12] = -0.625f;
    occurrence_mapping.values[14] = 0.5f;
    EXPECT_TRUE(session.scene().set_texture_mapping(occurrence, {occurrence_mapping}));
    return occurrence;
  }

  void begin(std::vector<EntityId> sources, bool radial, bool linked) {
    ASSERT_TRUE(editor.selection().select_objects(sources, sources.back()).ok());
    session.history().clear();
    ASSERT_TRUE(editor.tools().activate(radial ? ViewportToolId::RadialArray
                                               : ViewportToolId::LinearArray).ok());
    if (radial) {
      ASSERT_TRUE(session.radial_array().set_create_instances(linked).ok);
      ASSERT_TRUE(session.radial_array().set_copies(3u).ok);
      ASSERT_TRUE(session.radial_array().set_degrees(150).ok);
      ASSERT_TRUE(session.radial_array().update_center_candidate({-1, 0, -2}, {0, 1, 0}, 1).ok);
    } else {
      ASSERT_TRUE(session.linear_array().set_count(3u).ok);
      const auto handle = session.linear_array().handle_state();
      ASSERT_TRUE(session.linear_array().set_endpoint(
          handle.source_world + QVec3{6, 1, -2}, false, 1).ok);
    }
  }

  std::vector<RenderToolPreviewMeshRecord> render_preview() {
    std::vector<RenderToolPreviewMeshRecord> result;
    const auto status = QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge,
        [&](std::vector<RenderToolPreviewMeshRecord> candidate) {
          ++publication_count;
          result = std::move(candidate);
          return ToolPreviewSubmissionResult::Published;
        });
    EXPECT_TRUE(status.ok()) << status.error_message();
    return result;
  }

  void expect_committed_preview(const std::vector<RenderToolPreviewMeshRecord> &preview) {
    const auto projection = session.scene().mesh_occurrence_projection();
    const auto selected = session.scene().selected_entities();
    for (const auto &mesh : preview) {
      ASSERT_EQ(mesh.instance_sampling_from_local.size(), mesh.instance_transforms.size());
      for (std::size_t index = 0; index < mesh.instance_transforms.size(); ++index) {
        const auto records = projection->records();
        const auto found = std::ranges::find_if(records, [&](const auto &record) {
          return (std::ranges::find(selected, record.display_entity) != selected.end() ||
                  std::ranges::find(selected, record.geometry_entity) != selected.end()) &&
              scene_matrix_near_equal(record.world_from_geometry, mesh.instance_transforms[index]);
        });
        ASSERT_NE(found, records.end()) << "No accepted mesh matches its preview placement";
        const auto *component = session.scene().mesh(found->geometry_entity);
        ASSERT_NE(component, nullptr);
        const auto *asset = session.assets().mesh(component->mesh);
        ASSERT_NE(asset, nullptr);
        const auto mapping = effective_texture_sampling_from_local(
            session.scene(), found->geometry_entity,
            found->formal_instance() ? found->display_entity : kInvalidEntityId);
        ASSERT_TRUE(mapping.has_value());
        expect_sampling_equal(*asset, *mapping, mesh.mesh, mesh.instance_sampling_from_local[index]);
      }
    }
  }

  SceneSession session;
  QuaderEditorApi editor;
  SceneRenderBridge bridge;
  TextureLockSettings locks;
  std::size_t publication_count = 0u;
};

TEST(UvInstancePreviewTests, RadialSamplingMatchesLinkedAndIndependentAcceptanceAndRedo) {
  for (const bool occurrence : {false, true}) {
    for (const bool linked : {false, true}) {
      for (const bool lock : {false, true}) {
        for (const bool scale_lock : {false, true}) {
          SCOPED_TRACE(testing::Message() << occurrence << linked << lock << scale_lock);
          UvArrayHarness h;
          const EntityId source = h.source(occurrence);
          h.locks = {lock, scale_lock};
          h.begin({source}, true, linked);
          const auto entities_before = h.session.scene().entities();
          const auto preview = h.render_preview();
          ASSERT_EQ(preview.size(), 1u);
          EXPECT_EQ(h.session.scene().entities(), entities_before);
          EXPECT_EQ(h.session.history().undo_count(), 0u);
          if (occurrence) {
            EXPECT_NE(preview.front().source_entity, source);
            EXPECT_EQ(preview.front().visibility_entity, source);
            const auto mapping = effective_texture_sampling_from_local(
                h.session.scene(), preview.front().source_entity, source);
            ASSERT_TRUE(mapping.has_value());
            EXPECT_EQ(preview.front().sampling_from_local.values, mapping->values);
          }
          const auto accepted = h.editor.tools().commit_active_tool_preview();
          ASSERT_TRUE(accepted.ok()) << accepted.error_message();
          ASSERT_EQ(h.session.history().undo_count(), 1u);
          h.expect_committed_preview(preview);
          const auto selected_after = h.session.scene().selected_entities();
          ASSERT_TRUE(h.editor.history().undo().ok());
          EXPECT_EQ(h.session.scene().entities(), entities_before);
          ASSERT_TRUE(h.editor.history().redo().ok());
          EXPECT_EQ(h.session.scene().selected_entities(), selected_after);
          h.expect_committed_preview(preview);
        }
      }
    }
  }
}

TEST(UvInstancePreviewTests, LinearSamplingMatchesAcceptanceForMultipleSourcesAndEveryLockPolicy) {
  for (const bool occurrence : {false, true}) {
    for (const bool lock : {false, true}) {
      for (const bool scale_lock : {false, true}) {
        SCOPED_TRACE(testing::Message() << occurrence << lock << scale_lock);
        UvArrayHarness h;
        const EntityId first = h.source(occurrence);
        const EntityId second = h.source(occurrence, 14.0f);
        h.locks = {lock, scale_lock};
        h.begin({first, second}, false, occurrence);
        const auto preview = h.render_preview();
        ASSERT_EQ(preview.size(), 2u);
        const auto accepted = h.editor.tools().commit_active_tool_preview();
        ASSERT_TRUE(accepted.ok()) << accepted.error_message();
        ASSERT_EQ(h.session.history().undo_count(), 1u);
        h.expect_committed_preview(preview);
        ASSERT_TRUE(h.editor.history().undo().ok());
        ASSERT_TRUE(h.editor.history().redo().ok());
        h.expect_committed_preview(preview);
      }
    }
  }
}

TEST(UvInstancePreviewTests, RetainedSamplingSkipsUnchangedFramesAndUpdatesWithLockPreferences) {
  for (const bool radial : {false, true}) {
    UvArrayHarness h;
    const EntityId source = h.source(true);
    h.begin({source}, radial, true);
    const auto first = h.render_preview();
    ASSERT_EQ(first.size(), 1u);
    const auto *sampling_data = radial
        ? h.session.radial_array().preview_state().sources.front().generated_sampling_from_local.data()
        : h.session.linear_array().preview_state().sources.front().generated_sampling_from_local.data();
    for (int frame = 0; frame < 8; ++frame) {
      EXPECT_TRUE(h.render_preview().empty());
      const auto *retained_data = radial
          ? h.session.radial_array().preview_state().sources.front().generated_sampling_from_local.data()
          : h.session.linear_array().preview_state().sources.front().generated_sampling_from_local.data();
      EXPECT_EQ(retained_data, sampling_data);
    }
    EXPECT_EQ(h.publication_count, 1u);
    h.locks.texture_lock = false;
    const auto changed = h.render_preview();
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_NE(changed.front().instance_sampling_from_local.front().values,
              first.front().instance_sampling_from_local.front().values);
    EXPECT_EQ(h.publication_count, 2u);
    EXPECT_TRUE(h.render_preview().empty());
    EXPECT_EQ(h.publication_count, 2u);
    ASSERT_TRUE(h.editor.tools().commit_active_tool_preview().ok());
    h.expect_committed_preview(changed);
  }
}

TEST(UvInstancePreviewTests, LinearMemberEditUsesTheVisibleOccurrenceTextureFrame) {
  for (const bool lock : {false, true}) {
    UvArrayHarness h;
    const EntityId occurrence = h.source(true);
    const auto projection = h.session.scene().mesh_occurrence_projection();
    const auto records = projection->records_for_display(occurrence);
    ASSERT_EQ(records.size(), 1u);
    const EntityId member = records.front().geometry_entity;
    ASSERT_TRUE(h.editor.modeling().enter_instance_edit(occurrence).ok());
    h.locks = {lock, false};
    h.begin({member}, false, false);
    const auto preview = h.render_preview();
    ASSERT_EQ(preview.size(), 1u);
    EXPECT_EQ(preview.front().source_entity, member);
    EXPECT_EQ(preview.front().visibility_entity, occurrence);
    const auto accepted = h.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(accepted.ok()) << accepted.error_message();
    h.expect_committed_preview(preview);
    ASSERT_TRUE(h.editor.history().undo().ok());
    ASSERT_TRUE(h.editor.history().redo().ok());
    h.expect_committed_preview(preview);
  }
}

} // namespace
} // namespace quader
